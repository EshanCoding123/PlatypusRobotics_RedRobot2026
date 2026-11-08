"""Pair the controller with your computer, then run xbox_bridge.py ROBOT_BLE_NAME.

On macOS use python3. Install dependencies with: python3 -m pip install bleak pygame
"""

import argparse
import asyncio
import os
from dataclasses import dataclass


RX_UUID = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
NEUTRAL = b"!X" + bytes((100, 100, 1, 1))


@dataclass
class Gamepad:
    right_bumper: bool = False
    left_x: int = 0
    left_y: int = 0
    right_x: int = 0
    right_y: int = 0


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


def command(gamepad):
    if not gamepad.right_bumper:
        return NEUTRAL
    return b"!X" + bytes((
        100 + drive_axis(gamepad.left_x),
        100 + drive_axis(gamepad.left_y),
        1 + mechanism_direction(gamepad.right_y),
        1 + mechanism_direction(gamepad.right_x),
    ))


async def run(robot_name, slot):
    from bleak import BleakClient, BleakScanner

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
        print(f"Using controller {slot}.")

        device = await BleakScanner.find_device_by_filter(
            lambda _device, advertisement: advertisement.local_name == robot_name
        )
        if device is None:
            raise RuntimeError(f"Could not find {robot_name}. Check its BLE name and connection.")

        async with BleakClient(device) as client:
            try:
                await client.write_gatt_char(RX_UUID, NEUTRAL, response=True)
                print("Connected. Release RB, then hold RB to enable the sticks. Ctrl+C exits.")
                enable_seen_released = False
                while client.is_connected:
                    gamepad = read_gamepad(controller, pygame)
                    if not gamepad.right_bumper:
                        enable_seen_released = True
                    packet = command(gamepad) if enable_seen_released else NEUTRAL
                    await client.write_gatt_char(RX_UUID, packet, response=True)
                    await asyncio.sleep(0.04)
            finally:
                if client.is_connected:
                    await client.write_gatt_char(RX_UUID, NEUTRAL, response=True)
    finally:
        controllers.quit()
        pygame.quit()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("robot_name", help="BLE device name, for example RR-11-platypus")
    parser.add_argument("--slot", type=int, help="Controller index (default: detect automatically)")
    args = parser.parse_args()
    try:
        asyncio.run(run(args.robot_name, args.slot))
    except KeyboardInterrupt:
        pass
    except ModuleNotFoundError as error:
        if error.name not in ("bleak", "pygame"):
            raise
        parser.exit(1, "Install dependencies with: python -m pip install bleak pygame (use python3 on macOS)\n")
    except Exception as error:
        parser.exit(1, f"{error}\n")
