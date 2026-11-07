/**
 * @brief Red Robot 2026 main code!
 * 
 * Read the comments below to figure out how to get started!
 */

// Libraries with driver code (don't remove these!)
#include <Arduino.h>                // make sure this is first
#include "RedRobot.h"
#include "MechanismConfig.h"

// Declare some global variables
bool started; // boolean (true or false) to keep track of if we should start
int counter;  // integer to hold a count value
const bool TEST_IR_SENSORS = true; // Set false to run the autonomous driving demo.

// This function runs ONCE, when the microcontroller boots up
void setup() {
    // Initialize all components on the robot before doing any work
  RedRobot::InitRedRobot();         // call this first
  RedRobot::InitDCMotors();
  RedRobot::InitServo();
  RedRobot::SetServo(CLAW_SERVO_CHANNEL, CLAW_CENTER_ANGLE_DEGREES * 180 / 100);
  RedRobot::InitLineSensors();

  // Initialize our variables
  started = false;
  counter = 0;
}

// This function runs over and over (in a loop) when you are in autonomous mode.
// As an example, this code moves out of the starting box at start, and 
// Look at RedRobot.h to see the robot interface: what functions you can call and how.
void AutonomousLoop() {

    if (TEST_IR_SENSORS) {
        RedRobot::WriteDCMotor(0, 0);
        RedRobot::WriteDCMotor(2, 0);
        RedRobot::WriteDCMotor(3, 0);

        uint16_t sensorValues[6];
        RedRobot::ReadLineSensor(sensorValues);

        // Raw discharge time in microseconds: darker surfaces usually read higher.
        Serial.print("[IR us] ");
        for (int i = 0; i < 6; ++i) {
            Serial.print("LF");
            Serial.print(i);
            Serial.print("=");
            Serial.print(sensorValues[i]);
            Serial.print(" ");
        }
        Serial.println();
        delay(200);
        return;
    }

    if (!started) { // e.g. get out of the starting box
        started = true;

        // move forward - set motors 2 and 3 to speed 30
        RedRobot::WriteDCMotor(2, 30);
        RedRobot::WriteDCMotor(3, 30);

        // wait 1000 milliseconds = 1 second
        delay(1000);

        // turn right - set motor 2 to speed 30 and motor 3 to -30
        RedRobot::WriteDCMotor(2, 30);
        RedRobot::WriteDCMotor(3, -30);

        delay(500); // half a second

        // stop the motors
        RedRobot::WriteDCMotor(2, 0);
        RedRobot::WriteDCMotor(3, 0);
    }
    
    // repeatedly move the servo and get in scoring position
    // remember that the below code will run repeatedly since we are in
    // the AutonomousLoop!

    // The claw servo is owned by its manual controls.
    for (uint8_t channel = 0; channel < 4; ++channel)
      if (channel != CLAW_SERVO_CHANNEL)
        RedRobot::SetServo(channel, 0);

    // move forward at speed 50
    RedRobot::WriteDCMotor(2, 50);
    RedRobot::WriteDCMotor(3, 50);

    delay(1000);

    // stop the motors
    RedRobot::WriteDCMotor(2, 0);
    RedRobot::WriteDCMotor(3, 0);

    // Set the other servos to the demo positions.
    for (uint8_t channel = 0; channel < 4; ++channel)
      if (channel != CLAW_SERVO_CHANNEL)
        RedRobot::SetServo(channel, channel % 2 == 0 ? 90 : 180);

    delay(500);

    // move backward
    RedRobot::WriteDCMotor(2, -50);
    RedRobot::WriteDCMotor(3, -50);

    delay(1000);
    
    // Print to the serial console to debug!
    Serial.print("[AUTO] Running ");
    Serial.print(counter);
    Serial.println();

    // increment the counter
    counter = counter + 1;
}
