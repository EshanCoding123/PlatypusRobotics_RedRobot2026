"""Pair the controller with your computer, then run xbox_bridge.py.

On macOS use python3. Install dependencies with: python3 -m pip install bleak pygame
"""

import argparse
import asyncio
import os
import sys
import time
from concurrent.futures import Future, ThreadPoolExecutor
from contextlib import asynccontextmanager, contextmanager, suppress
from dataclasses import dataclass


RX_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
TX_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"
FIRMWARE_ID = b"RR26-XBOX-3"
ROBOT_NAME = "RR-11-platypus"
NEUTRAL = b"!X" + bytes((100, 100, 1, 1))
AUTONOMOUS = NEUTRAL + b"\x01"


@dataclass
class Gamepad:
    right_bumper: bool = False
    left_x: int = 0
    left_y: int = 0
    right_x: int = 0
    right_y: int = 0
    dpad_up: bool = False
    dpad_down: bool = False
    dpad_left: bool = False
    dpad_right: bool = False
    menu: bool = False
    view: bool = False


def read_gamepad(controller, pygame):
    pygame.event.get()  # Pump input and drain events on the main thread.
    if not controller.attached():
        raise RuntimeError("Controller disconnected.")
    # SDL reports stick-up as negative. The robot expects positive for forward/up.
    return Gamepad(
        right_bumper=controller.get_button(pygame.CONTROLLER_BUTTON_RIGHTSHOULDER),
        left_x=controller.get_axis(pygame.CONTROLLER_AXIS_LEFTX),
        left_y=-controller.get_axis(pygame.CONTROLLER_AXIS_LEFTY),
        right_x=controller.get_axis(pygame.CONTROLLER_AXIS_RIGHTX),
        right_y=-controller.get_axis(pygame.CONTROLLER_AXIS_RIGHTY),
        dpad_up=controller.get_button(pygame.CONTROLLER_BUTTON_DPAD_UP),
        dpad_down=controller.get_button(pygame.CONTROLLER_BUTTON_DPAD_DOWN),
        dpad_left=controller.get_button(pygame.CONTROLLER_BUTTON_DPAD_LEFT),
        dpad_right=controller.get_button(pygame.CONTROLLER_BUTTON_DPAD_RIGHT),
        menu=controller.get_button(pygame.CONTROLLER_BUTTON_START),
        view=controller.get_button(pygame.CONTROLLER_BUTTON_BACK),
    )


def drive_axis(value):
    dead_zone = 8000
    magnitude = max(0, abs(value) - dead_zone)
    speed = min(100, round(magnitude * 100 / (32767 - dead_zone)))
    return -speed if value < 0 else speed


def mechanism_direction(value):
    threshold = 16000
    if value >= threshold:
        return 1
    if value <= -threshold:
        return -1
    return 0


def axis_with_buttons(stick_value, positive, negative):
    # Buttons override the corresponding stick axis. Opposite buttons cancel.
    if positive or negative:
        return 32767 * (int(positive) - int(negative))
    return stick_value


def command(gamepad):
    if not gamepad.right_bumper:
        return NEUTRAL
    turn = axis_with_buttons(gamepad.left_x, gamepad.dpad_right, gamepad.dpad_left)
    drive = axis_with_buttons(gamepad.left_y, gamepad.dpad_up, gamepad.dpad_down)
    return b"!X" + bytes((
        100 + drive_axis(turn),
        100 + drive_axis(drive),
        1 + mechanism_direction(gamepad.right_y),
        1 + mechanism_direction(gamepad.right_x),
    ))


@dataclass
class ControlState:
    autonomous: bool = False
    menu_was_pressed: bool = True
    enable_seen_released: bool = False

    def packet(self, gamepad):
        menu_pressed = gamepad.menu and not self.menu_was_pressed
        self.menu_was_pressed = gamepad.menu
        # View always stops autonomy and requires RB release before manual drive.
        if gamepad.view:
            self.autonomous = False
            self.enable_seen_released = False
            return NEUTRAL
        if menu_pressed and not gamepad.right_bumper:
            self.autonomous = True
            self.enable_seen_released = False
        if self.autonomous:
            return AUTONOMOUS
        if not gamepad.right_bumper:
            self.enable_seen_released = True
        return command(gamepad) if self.enable_seen_released else NEUTRAL


@asynccontextmanager
async def bluetooth_connection(robot_name):
    # Keep all WinRT calls on one fresh thread, separate from SDL's COM setup.
    ready = Future()

    async def keep_alive():
        stopped = asyncio.Event()
        ready.set_result((asyncio.get_running_loop(), stopped))
        await stopped.wait()

    def worker():
        try:
            asyncio.run(keep_alive())
        except BaseException as error:
            if not ready.done():
                ready.set_exception(error)
            raise

    with ThreadPoolExecutor(max_workers=1, thread_name_prefix="robot-bluetooth") as executor:
        worker_result = executor.submit(worker)
        loop, stopped = await asyncio.wrap_future(ready)

        async def invoke(operation):
            return await asyncio.wrap_future(asyncio.run_coroutine_threadsafe(operation, loop))

        async def connect():
            from bleak import BleakClient, BleakScanner

            device = await BleakScanner.find_device_by_filter(
                lambda _device, advertisement: advertisement.local_name == robot_name
            )
            if device is None:
                raise RuntimeError(f"Could not find {robot_name}. Check its BLE name and connection.")
            client = BleakClient(device)
            try:
                await client.connect()
                await asyncio.wait_for(client.write_gatt_char(RX_UUID, NEUTRAL, response=True), 2.0)
                try:
                    firmware = await asyncio.wait_for(client.read_gatt_char(TX_UUID), 2.0)
                except Exception as error:
                    raise RuntimeError("Cannot verify Xbox firmware. Upload the updated RR26_Arduino sketch, then retry.") from error
                if firmware != FIRMWARE_ID:
                    raise RuntimeError("Robot firmware does not match this bridge. Upload the updated RR26_Arduino sketch.")
            except BaseException:
                with suppress(Exception):
                    await asyncio.wait_for(client.disconnect(), 3.0)
                raise
            return client

        async def send(packet):
            await invoke(asyncio.wait_for(client.write_gatt_char(RX_UUID, packet, response=True), 2.0))

        async def disconnect():
            try:
                if client.is_connected:
                    await asyncio.wait_for(client.write_gatt_char(RX_UUID, NEUTRAL, response=True), 2.0)
            finally:
                await asyncio.wait_for(client.disconnect(), 3.0)

        try:
            client = await invoke(connect())
            try:
                yield send
            except BaseException:
                # Preserve the original controller/connection failure if cleanup also fails.
                with suppress(Exception):
                    await invoke(disconnect())
                raise
            else:
                await invoke(disconnect())
        finally:
            loop.call_soon_threadsafe(stopped.set)
            await asyncio.wrap_future(worker_result)


@contextmanager
def open_controller(slot):
    if sys.platform == "win32":
        # Prevent Python COM imports from selecting a GUI-only callback model.
        sys.coinit_flags = 0

    os.environ["SDL_JOYSTICK_ALLOW_BACKGROUND_EVENTS"] = "1"
    os.environ.setdefault("PYGAME_HIDE_SUPPORT_PROMPT", "1")
    import pygame
    from pygame._sdl2 import controller as controllers

    try:
        pygame.display.init()
        pygame.display.set_mode((1, 1), pygame.HIDDEN)
        controllers.init()
        pygame.event.get()
        connected_slots = [i for i in range(controllers.get_count()) if controllers.is_controller(i)]
        for index in connected_slots:
            print(f"Controller {index}: {controllers.name_forindex(index)}")
        if slot is None:
            if not connected_slots:
                raise RuntimeError("No supported controller detected. Connect it by Bluetooth or a USB data cable and retry.")
            if len(connected_slots) > 1:
                raise RuntimeError("Multiple controllers detected. Select one with --slot NUMBER.")
            slot = connected_slots[0]
        if slot not in connected_slots:
            raise RuntimeError(f"No supported controller at index {slot}.")
        controller = controllers.Controller(slot)
        print(f"Using controller {slot}: {controllers.name_forindex(slot)}", flush=True)
        yield controller, pygame
    finally:
        controllers.quit()
        pygame.quit()


def check_controller(slot):
    print("Controller-only check. Press buttons and move both sticks. Ctrl+C exits.", flush=True)
    with open_controller(slot) as (controller, pygame):
        print(f"SDL version: {pygame.get_sdl_version()}", flush=True)
        print(f"Controller mapping: {controller.get_mapping()}", flush=True)
        while True:
            print(read_gamepad(controller, pygame), flush=True)
            time.sleep(0.2)


async def run(robot_name, slot, debug=False):
    print("Independent dual-stick controls v4 + firmware verification", flush=True)
    print(f"Running {os.path.abspath(__file__)}", flush=True)
    with open_controller(slot) as (controller, pygame):
        print(f"Looking for robot {robot_name}...")

        async with bluetooth_connection(robot_name) as send:
            await send(NEUTRAL)
            print("Connected. Release RB, then hold RB to enable controls. Ctrl+C exits.")
            print("Left stick/D-pad: drive. Right stick up/down: ramp. Right stick left/right: claw.")
            print("Hold RB and use both sticks together. Move the right stick past halfway.")
            print("Release RB, then press Menu to start autonomous. View stops it and returns to manual.")
            print("Compatible Xbox firmware verified.")
            controls = ControlState()
            last_debug_time = 0.0
            while True:
                gamepad = read_gamepad(controller, pygame)
                previous_autonomous = controls.autonomous
                packet = controls.packet(gamepad)
                await send(packet)
                if controls.autonomous != previous_autonomous:
                    print("Requested mode: " + ("autonomous" if controls.autonomous else "manual"), flush=True)
                now = asyncio.get_running_loop().time()
                if debug and now - last_debug_time >= 0.5:
                    print(
                        f"RB={int(gamepad.right_bumper)} "
                        f"autonomous={int(controls.autonomous)} "
                        f"left=({gamepad.left_x}, {gamepad.left_y}) "
                        f"right=({gamepad.right_x}, {gamepad.right_y}) "
                        f"sent turn={packet[2] - 100} drive={packet[3] - 100} "
                        f"ramp={packet[4] - 1} claw={packet[5] - 1}",
                        flush=True,
                    )
                    last_debug_time = now
                await asyncio.sleep(0.04)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("robot_name", nargs="?", default=ROBOT_NAME, help=f"BLE device name (default: {ROBOT_NAME})")
    parser.add_argument("--slot", type=int, help="Controller index (default: detect automatically)")
    parser.add_argument("--debug", action="store_true", help="Show right-stick readings and transmitted mechanism commands")
    parser.add_argument("--check-controller", action="store_true", help="Test controller inputs without connecting to the robot")
    args = parser.parse_args()
    try:
        if args.check_controller:
            check_controller(args.slot)
        else:
            asyncio.run(run(args.robot_name, args.slot, args.debug))
    except KeyboardInterrupt:
        pass
    except ModuleNotFoundError as error:
        if error.name not in ("bleak", "pygame"):
            raise
        parser.exit(1, "Install dependencies with: python -m pip install bleak pygame (use python3 on macOS)\n")
    except Exception as error:
        parser.exit(1, f"{error}\n")
