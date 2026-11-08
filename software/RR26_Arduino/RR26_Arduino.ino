/**
 * @brief Red Robot 2026 main code!
 * 
 * Read the comments below to figure out how to get started!
 */

// Libraries with driver code (don't remove these!)
#include <Arduino.h>                // make sure this is first
#include "RedRobot.h"
#include "MechanismConfig.h"

namespace {

constexpr uint8_t LINE_SENSOR_COUNT = 6;

// LF0 through LF5 are ordered from the robot's left to its right. These
// references are slightly above the largest non-black readings observed on
// the competition floor. Each channel needs its own value because the raw RC
// discharge times differ significantly from sensor to sensor.
constexpr uint16_t LIGHT_REFERENCE[LINE_SENSOR_COUNT] = {
  5800, 4200, 3600, 3700, 3550, 3300
};
constexpr uint16_t DARK_REFERENCE = 10000;

// Position of each sensor across the front of the robot. A positive error
// means the line is to the robot's right.
constexpr float SENSOR_POSITION[LINE_SENSOR_COUNT] = {
  -1.0f, -0.6f, -0.2f, 0.2f, 0.6f, 1.0f
};

constexpr float SENSOR_NOISE_FLOOR = 0.12f;
constexpr float MINIMUM_LINE_STRENGTH = 0.18f;
constexpr float WIDE_BLACK_STRENGTH = 2.5f;
constexpr uint8_t WIDE_BLACK_SENSOR_COUNT = 5;

// Run at full speed while the line is visible. Steering works by slowing the
// inside wheel; the outside wheel remains capped at 100.
constexpr int16_t BASE_DRIVE_SPEED = 100;
constexpr int16_t MAX_DRIVE_SPEED = 100;
constexpr int16_t MAX_STEERING_CORRECTION = 100;
constexpr float STEERING_KP = 100.0f;
constexpr float STEERING_KD = 40.0f;

constexpr int16_t RECOVERY_FORWARD_SPEED = 15;
constexpr int16_t RECOVERY_TURN_SPEED = 24;
constexpr unsigned long LOST_LINE_TIMEOUT_MS = 1000;
constexpr unsigned long DIAGNOSTIC_PERIOD_MS = 250;

enum class LineState {
  Tracking,
  Crossing,
  Recovering,
  Stopped
};

float lastLineError = 0.0f;
int8_t lastTurnDirection = 1;
unsigned long lastLineSeenMs = 0;
unsigned long lastDiagnosticMs = 0;

const char *LineStateName(LineState state) {
  switch (state) {
    case LineState::Tracking:   return "TRACK";
    case LineState::Crossing:   return "CROSS";
    case LineState::Recovering: return "SEARCH";
    case LineState::Stopped:    return "STOP";
  }
  return "UNKNOWN";
}

void SetDriveSpeed(int16_t leftSpeed, int16_t rightSpeed) {
  leftSpeed = constrain(leftSpeed, -MAX_DRIVE_SPEED, MAX_DRIVE_SPEED);
  rightSpeed = constrain(rightSpeed, -MAX_DRIVE_SPEED, MAX_DRIVE_SPEED);

  RedRobot::WriteDCMotor(2, leftSpeed);
  RedRobot::WriteDCMotor(3, rightSpeed);
}

float NormalizedDarkness(uint8_t sensor, uint16_t rawValue) {
  int32_t valueAboveLight = static_cast<int32_t>(rawValue) - LIGHT_REFERENCE[sensor];
  int32_t calibrationRange = DARK_REFERENCE - LIGHT_REFERENCE[sensor];
  float darkness = static_cast<float>(valueAboveLight) / calibrationRange;

  darkness = constrain(darkness, 0.0f, 1.0f);
  return darkness >= SENSOR_NOISE_FLOOR ? darkness : 0.0f;
}

void PrintLineDiagnostics(const uint16_t values[LINE_SENSOR_COUNT],
                          LineState state, float error,
                          int16_t leftSpeed, int16_t rightSpeed,
                          unsigned long now) {
  if (now - lastDiagnosticMs < DIAGNOSTIC_PERIOD_MS)
    return;

  lastDiagnosticMs = now;
  Serial.print("[LINE] ");
  for (uint8_t i = 0; i < LINE_SENSOR_COUNT; ++i) {
    Serial.print("LF");
    Serial.print(i);
    Serial.print("=");
    Serial.print(values[i]);
    Serial.print(" ");
  }
  Serial.print("state=");
  Serial.print(LineStateName(state));
  Serial.print(" error=");
  Serial.print(error, 3);
  Serial.print(" left=");
  Serial.print(leftSpeed);
  Serial.print(" right=");
  Serial.println(rightSpeed);
}

} // namespace

// This function runs ONCE, when the microcontroller boots up
void setup() {
    // Initialize all components on the robot before doing any work
  RedRobot::InitRedRobot();         // call this first
  RedRobot::InitDCMotors();
  RedRobot::InitServo();
  RedRobot::SetServo(CLAW_SERVO_CHANNEL, CLAW_CENTER_ANGLE_DEGREES);
  RedRobot::InitLineSensors();

  // Autonomous starts only when the black line is visible. If the robot is
  // placed off the line, it remains stopped instead of choosing a random turn.
  lastLineSeenMs = 0;
}

// Follow the black center of the white-black-white path. This function is
// called repeatedly while BLE remote control is disconnected.
void AutonomousLoop() {
  uint16_t sensorValues[LINE_SENSOR_COUNT];
  RedRobot::ReadLineSensor(sensorValues);

  float weightedPosition = 0.0f;
  float totalStrength = 0.0f;
  uint8_t activeSensorCount = 0;

  for (uint8_t i = 0; i < LINE_SENSOR_COUNT; ++i) {
    float darkness = NormalizedDarkness(i, sensorValues[i]);
    if (darkness > 0.0f)
      ++activeSensorCount;

    weightedPosition += darkness * SENSOR_POSITION[i];
    totalStrength += darkness;
  }

  unsigned long now = millis();
  float lineError = lastLineError;
  int16_t leftSpeed = 0;
  int16_t rightSpeed = 0;
  LineState state = LineState::Stopped;

  bool wideBlack = activeSensorCount >= WIDE_BLACK_SENSOR_COUNT &&
                   totalStrength >= WIDE_BLACK_STRENGTH;
  bool lineDetected = totalStrength >= MINIMUM_LINE_STRENGTH;

  if (wideBlack) {
    // A broad black reading is the central crossing. Preserve the robot's
    // heading rather than allowing small sensor differences to select a branch.
    state = LineState::Crossing;
    lineError = 0.0f;
    lastLineError = 0.0f;
    lastLineSeenMs = now;
    leftSpeed = BASE_DRIVE_SPEED;
    rightSpeed = BASE_DRIVE_SPEED;
  } else if (lineDetected) {
    state = LineState::Tracking;
    lineError = weightedPosition / totalStrength;

    float derivative = lineError - lastLineError;
    float correction = STEERING_KP * lineError + STEERING_KD * derivative;
    correction = constrain(correction,
                           -static_cast<float>(MAX_STEERING_CORRECTION),
                           static_cast<float>(MAX_STEERING_CORRECTION));

    int16_t steering = static_cast<int16_t>(correction);
    leftSpeed = BASE_DRIVE_SPEED + steering;
    rightSpeed = BASE_DRIVE_SPEED - steering;

    if (lineError > 0.05f)
      lastTurnDirection = 1;
    else if (lineError < -0.05f)
      lastTurnDirection = -1;

    lastLineError = lineError;
    lastLineSeenMs = now;
  } else if (lastLineSeenMs != 0 &&
             now - lastLineSeenMs <= LOST_LINE_TIMEOUT_MS) {
    // Search toward the side where the line was last seen. One wheel reverses
    // slightly so the robot can reacquire a sharp bend without travelling far.
    state = LineState::Recovering;
    int16_t correction = lastTurnDirection * RECOVERY_TURN_SPEED;
    leftSpeed = RECOVERY_FORWARD_SPEED + correction;
    rightSpeed = RECOVERY_FORWARD_SPEED - correction;
  } else {
    // Do not let the robot continue across the field after losing the path.
    state = LineState::Stopped;
    leftSpeed = 0;
    rightSpeed = 0;
  }

  leftSpeed = constrain(leftSpeed, -MAX_DRIVE_SPEED, MAX_DRIVE_SPEED);
  rightSpeed = constrain(rightSpeed, -MAX_DRIVE_SPEED, MAX_DRIVE_SPEED);
  SetDriveSpeed(leftSpeed, rightSpeed);
  PrintLineDiagnostics(sensorValues, state, lineError,
                       leftSpeed, rightSpeed, now);
}
