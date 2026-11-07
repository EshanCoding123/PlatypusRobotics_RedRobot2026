#pragma once

// Motor 0 is available; motors 2/3 drive the robot, and motor 1 is disabled.
constexpr int RAMP_MOTOR_CHANNEL = 0;
// Power ranges from -100 to 100; swap signs to reverse Up/Down.
constexpr int RAMP_UP_POWER = 100;
constexpr int RAMP_DOWN_POWER = -100;

constexpr int CLAW_SERVO_CHANNEL = 0;
// HS-311 angles relative to center, from -60 to 60 degrees.
constexpr int CLAW_LEFT_ANGLE_DEGREES = -15;
constexpr int CLAW_RIGHT_ANGLE_DEGREES = 15;
constexpr int CLAW_CENTER_ANGLE_DEGREES = 0;
