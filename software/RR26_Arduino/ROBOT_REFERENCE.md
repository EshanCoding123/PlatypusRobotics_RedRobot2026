# Robot code reference

This note describes the current local code. The IR test has compiled, but its
upload to the robot and physical behavior have not been confirmed.

## Controller

- Numbered **3** selects ramp motor mode. Hold Up to raise or Down to lower;
  releasing the direction stops the motor. Both directions held stop it.
- Numbered **4** selects claw servo mode. Left and Right select fixed positions;
  the servo holds its position after release.
- Pressing another numbered or lettered button leaves the selected mechanism
  mode. Mode changes stop the ramp and clear held directions.
- RR Controller **A** runs motor 0 at full power while held. With the current
  configuration this is also the ramp motor.
- Numbered **2** retains the example servo behavior for channels other than
  the claw: 180 while held, 0 on release (values on the driver's scale).

Settings are in `MechanismConfig.h`: ramp motor 0 at +100/-100, and HS-311
claw servo 0 at -15/+15 degrees, centered at 0. Adjust the channel, power,
direction signs and angles for the actual mechanism. Motor commands are capped
at +/-100; motor 1 is disabled and motors 2 and 3 are used for driving.

`src/RemoteControl.cpp` handles the controls. `src/RedRobot.cpp` reads BLE
packets by byte so binary release values can be handled. A BLE connection
selects remote control; disconnect returns to autonomous mode.

## QTR-8RC IR test

`RR26_Arduino.ino` currently has `TEST_IR_SENSORS = true`. With BLE disconnected,
the autonomous loop stops the motors and prints `[IR us] LF0=...` through LF5
to the serial monitor at 115200 baud. It uses `RedRobot::ReadLineSensor()`.
These are raw discharge times; black generally reads higher than white.

Connect QTR VCC to 3V3, GND to GND, and outputs 1-6 to LF0-LF5 respectively.
LEDON can connect to LEDCTRL. The QTR's 3.3V bypass is for 3.3V supply only;
do not short it while using 5V. Verify the physical connector labels and wiring.

At the robot, compare and record readings over the white floor and black line.
Those readings will be used to choose a threshold and tune autonomous steering.
Line following and the destination stop marker are **not implemented yet**.
Setting `TEST_IR_SENSORS` to false runs the existing timed driving demo, which
moves the robot; it does not follow a line.
