#pragma once

// Motor 0 is available; motors 2/3 drive the robot, and motor 1 is disabled.
constexpr int RAMP_MOTOR_CHANNEL = 0;
// Power ranges from -100 to 100; swap signs to reverse Up/Down.
constexpr int RAMP_UP_POWER = 50;
constexpr int RAMP_DOWN_POWER = -50;

constexpr int CLAW_SERVO_CHANNEL = 0;
// Driver scale: -180..180 maps to 500..2500 us, with 0 at 1500 us.
// These are driver commands, not measured physical angles; both endpoint
// pulses exceed the HS-311's specified 900..2100 us range.
constexpr int CLAW_LEFT_ANGLE_DEGREES = 80;
constexpr int CLAW_RIGHT_ANGLE_DEGREES = 180;
constexpr int CLAW_CENTER_ANGLE_DEGREES = 0;
