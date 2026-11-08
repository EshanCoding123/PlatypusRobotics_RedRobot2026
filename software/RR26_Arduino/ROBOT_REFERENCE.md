# Robot code reference

This note describes the current local code. The IR test has compiled, but its
upload to the robot and physical behavior have not been confirmed.

## Controller

- Numbered **3** selects the combined mechanism mode. Hold Up to raise the ramp
  or Down to lower it; releasing the direction stops the motor. Press Left or
  Right once to send all four servo channels to driver commands -180 or 180
  (500 or 2500 microseconds); they do not need to be held. Both signals exceed
  the HS-311's specified 900 to 2100 microsecond range. Both Up and Down held
  stop the ramp.
- Numbered **4** selects claw servo mode. Left and Right select fixed positions;
  the servo holds its position after release.
- Pressing another numbered or lettered button leaves the selected mechanism
  mode. Mode changes stop the ramp and clear held directions.
- RR Controller **A** runs motor 0 at full power while held. With the current
configuration this is also the ramp motor.
- Numbered **2** returns to drive mode and no longer moves a servo.

Settings are in `MechanismConfig.h`: ramp motor 0 at +100/-100, and claw
servo commands -180 and 180, centered at 0. These are driver values, not measured
angles. Adjust the channel, power, direction signs and positions for the actual
mechanism. Motor commands are capped
at +/-100; motor 1 is disabled and motors 2 and 3 are used for driving.

`src/RemoteControl.cpp` handles the controls. `src/RedRobot.cpp` reads BLE
packets by byte so binary release values can be handled. A BLE connection
selects remote control; disconnect returns to autonomous mode.

## QTR-8RC line following

With BLE disconnected, the autonomous loop follows the black center of the
white-black-white path. It uses `RedRobot::ReadLineSensor()` and drives motors 2
and 3 with a PD steering correction. The initial calibration constants and
controller gains are at the top of `RR26_Arduino.ino`.

The serial monitor prints `[LINE]` diagnostics at 115200 baud, including all six
raw readings, the current state, steering error, and left/right motor commands.
Black generally reads higher than white.

Connect QTR VCC to 3V3, GND to GND, and outputs 1-6 to LF0-LF5 respectively.
LEDON can connect to LEDCTRL. The QTR's 3.3V bypass is for 3.3V supply only;
do not short it while using 5V. Verify the physical connector labels and wiring.

The follower continues straight when five or more sensors detect a broad black
crossing. If it loses the line, it searches toward the last detected side for
one second and then stops. Place the robot over the white-black-white strip
before autonomous mode starts. A destination stop marker is not implemented.
