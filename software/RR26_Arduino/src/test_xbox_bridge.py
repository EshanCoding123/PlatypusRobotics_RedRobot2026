"""Run with python -m unittest discover -s src -p test_xbox_bridge.py."""

import asyncio
import sys
import threading
import types
import unittest
from unittest.mock import patch

import xbox_bridge as bridge


class ControllerTests(unittest.TestCase):
    def test_simultaneous_dpad_ramp_and_claw(self):
        for mask in range(16):
            for ramp in (-1, 0, 1):
                for claw in (-1, 0, 1):
                    gamepad = bridge.Gamepad(
                        right_bumper=True,
                        dpad_up=bool(mask & 1), dpad_down=bool(mask & 2),
                        dpad_left=bool(mask & 4), dpad_right=bool(mask & 8),
                        right_y=ramp * 32767, right_x=claw * 32767,
                    )
                    turn = 100 * (int(gamepad.dpad_right) - int(gamepad.dpad_left))
                    drive = 100 * (int(gamepad.dpad_up) - int(gamepad.dpad_down))
                    expected = b"!X" + bytes((100 + turn, 100 + drive, 1 + ramp, 1 + claw))
                    self.assertEqual(bridge.command(gamepad), expected)
                    gamepad.right_bumper = False
                    self.assertEqual(bridge.command(gamepad), bridge.NEUTRAL)

    def test_stick_release_does_not_stop_other_stick(self):
        gamepad = bridge.Gamepad(right_bumper=True, left_y=32767, right_y=32767)
        self.assertEqual(bridge.command(gamepad), b"!X" + bytes((100, 200, 2, 1)))
        gamepad.right_y = 0
        self.assertEqual(bridge.command(gamepad), b"!X" + bytes((100, 200, 1, 1)))
        gamepad.left_y, gamepad.right_y = 0, 32767
        self.assertEqual(bridge.command(gamepad), b"!X" + bytes((100, 100, 2, 1)))

    def test_autonomous_stop_and_rearming(self):
        state = bridge.ControlState()
        self.assertEqual(state.packet(bridge.Gamepad(menu=True)), bridge.NEUTRAL)
        state.packet(bridge.Gamepad())
        self.assertEqual(state.packet(bridge.Gamepad(menu=True)), bridge.AUTONOMOUS)
        self.assertEqual(state.packet(bridge.Gamepad(right_bumper=True)), bridge.AUTONOMOUS)
        self.assertEqual(state.packet(bridge.Gamepad(view=True, menu=True)), bridge.NEUTRAL)
        moving = bridge.Gamepad(right_bumper=True, left_y=32767)
        self.assertEqual(state.packet(moving), bridge.NEUTRAL)
        state.packet(bridge.Gamepad())
        self.assertEqual(state.packet(moving), bridge.command(moving))


class BluetoothTests(unittest.TestCase):
    def setUp(self):
        self.events = []
        self.threads = set()
        self.loops = set()
        self.firmware = bridge.FIRMWARE_ID
        self.found = True
        self.fail_write = False
        self.fail_disconnect = False
        owner = self

        def record(event):
            owner.events.append(event)
            owner.threads.add(threading.get_ident())
            owner.loops.add(id(asyncio.get_running_loop()))

        class Scanner:
            @staticmethod
            async def find_device_by_filter(predicate):
                record("scan")
                assert predicate(None, types.SimpleNamespace(local_name=bridge.ROBOT_NAME))
                return object() if owner.found else None

        class Client:
            def __init__(self, device):
                record("create")
                self.is_connected = False

            async def connect(self):
                record("connect")
                self.is_connected = True

            async def read_gatt_char(self, uuid):
                record("read")
                assert uuid == bridge.TX_UUID
                return owner.firmware

            async def write_gatt_char(self, uuid, packet, response):
                record(packet)
                assert uuid == bridge.RX_UUID and response
                if owner.fail_write and packet != bridge.NEUTRAL:
                    raise RuntimeError("write failed")

            async def disconnect(self):
                record("disconnect")
                self.is_connected = False
                if owner.fail_disconnect:
                    raise RuntimeError("disconnect failed")

        self.modules = patch.dict(sys.modules, {
            "bleak": types.SimpleNamespace(BleakScanner=Scanner, BleakClient=Client),
        })
        self.modules.start()
        self.addCleanup(self.modules.stop)

    def tearDown(self):
        self.assertFalse(any(t.name.startswith("robot-bluetooth") for t in threading.enumerate()))

    def test_entire_session_uses_one_bluetooth_thread(self):
        packet = bridge.command(bridge.Gamepad(right_bumper=True, left_y=32767, right_y=32767))

        async def session():
            async with bridge.bluetooth_connection(bridge.ROBOT_NAME) as send:
                await send(packet)

        asyncio.run(session())
        self.assertEqual(len(self.threads), 1)
        self.assertNotIn(threading.get_ident(), self.threads)
        self.assertEqual(len(self.loops), 1)
        self.assertEqual(self.events, ["scan", "create", "connect", bridge.NEUTRAL,
                                     "read", packet, bridge.NEUTRAL, "disconnect"])

    def test_old_firmware_is_rejected_before_movement(self):
        self.firmware = b"older firmware"

        async def session():
            async with bridge.bluetooth_connection(bridge.ROBOT_NAME):
                self.fail("Incompatible firmware was accepted")

        with self.assertRaisesRegex(RuntimeError, "firmware does not match"):
            asyncio.run(session())
        self.assertEqual(self.events[-1], "disconnect")

    def test_controller_failure_stops_before_disconnect(self):
        async def session():
            async with bridge.bluetooth_connection(bridge.ROBOT_NAME):
                raise RuntimeError("controller disconnected")

        with self.assertRaisesRegex(RuntimeError, "controller disconnected"):
            asyncio.run(session())
        self.assertEqual(self.events[-2:], [bridge.NEUTRAL, "disconnect"])

    def test_cleanup_does_not_hide_write_failure(self):
        self.fail_write = self.fail_disconnect = True

        async def session():
            async with bridge.bluetooth_connection(bridge.ROBOT_NAME) as send:
                await send(bridge.AUTONOMOUS)

        with self.assertRaisesRegex(RuntimeError, "write failed"):
            asyncio.run(session())

    def test_missing_robot_exits_worker(self):
        self.found = False

        async def session():
            async with bridge.bluetooth_connection(bridge.ROBOT_NAME):
                self.fail("Missing robot was accepted")

        with self.assertRaisesRegex(RuntimeError, "Could not find"):
            asyncio.run(session())


if __name__ == "__main__":
    unittest.main()
