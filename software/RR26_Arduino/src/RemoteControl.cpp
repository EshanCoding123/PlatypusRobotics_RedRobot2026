#include "../RedRobot.h"
#include "../MechanismConfig.h"

bool useTankDrive = false;

namespace {
  RobotControl::Mode activeMode = RobotControl::Mode::Drive;
  bool bluefruitDirections[4] = {};
  bool rrDirections[5] = {};

  void ClearHeldDirectionInputs() {
    for (bool &pressed : bluefruitDirections) pressed = false;
    for (bool &pressed : rrDirections) pressed = false;
  }

}

namespace RobotControl {
  void SelectMode(Mode mode) {
    if (activeMode == mode) return;

    activeMode = mode;
    // Switching mechanisms clears input held in the previous mode.
    ClearHeldDirectionInputs();
    RedRobot::WriteDCMotor(RAMP_MOTOR_CHANNEL, 0);
    if (RAMP_MOTOR_CHANNEL != 0) RedRobot::WriteDCMotor(0, 0);
    RedRobot::WriteDCMotor(2, 0);
    RedRobot::WriteDCMotor(3, 0);
  }

  void ResetOnDisconnect() {
    activeMode = Mode::Drive;
    ClearHeldDirectionInputs();
  }
}

namespace Bluefruit
{
  /* DPad_Pressed is called everytime a D-Pad button is pressed or released.
   *
   * Only activates after RedRobot::StartRemoteControl() is called.
   * Button is one of {UP = 0, DOWN = 1, LEFT = 2, RIGHT = 3}
   */
  void DPad_Pressed(button_t Button, bool buttonPressedDown)
  {
    if (!RedRobot::IsRemoteControlled())
      return;

    if (Button < UP || Button > RIGHT) return;
    auto &button_pressed = bluefruitDirections;
    button_pressed[Button] = buttonPressedDown;

    if (activeMode == RobotControl::Mode::RampMotor)
    {
      // Numbered button 3 combines both mechanisms: Up/Down runs the ramp
      // while held, and Left/Right sends the claw once to a fixed position.
      if (buttonPressedDown && (Button == LEFT || Button == RIGHT))
      {
        int16_t angle = Button == LEFT ? CLAW_LEFT_ANGLE_DEGREES
                                      : CLAW_RIGHT_ANGLE_DEGREES;
        // Button 2 previously drove the non-claw servo channels. Move those
        // channels into the combined button-3 mode as requested. Include the
        // configured claw channel so the mapping works for any servo connector.
        for (uint8_t channel = 0; channel < 4; ++channel)
          RedRobot::SetServo(channel, angle);
      }

      int ramp_speed = 0;
      if (button_pressed[UP] && !button_pressed[DOWN])
      {
        ramp_speed = RAMP_UP_POWER;
      }
      if (button_pressed[DOWN] && !button_pressed[UP])
      {
        ramp_speed = RAMP_DOWN_POWER;
      }
      RedRobot::WriteDCMotor(RAMP_MOTOR_CHANNEL, ramp_speed);
      return;
    }

    if (activeMode == RobotControl::Mode::ClawServo)
    {
      if (!buttonPressedDown) return;

      if (Button == LEFT)
      {
        RedRobot::SetServo(CLAW_SERVO_CHANNEL, CLAW_LEFT_ANGLE_DEGREES);
      }
      if (Button == RIGHT)
      {
        RedRobot::SetServo(CLAW_SERVO_CHANNEL, CLAW_RIGHT_ANGLE_DEGREES);
      }
      return;
    }

    // TODO: edit your speed output to the yellow motors below!
    // stop = 0, slow = 50, fast = 100
    // range [-100,100]

    int left_speed = 0;
    int right_speed = 0;

    if (button_pressed[UP])
    { // Forward
      left_speed += 100;
      right_speed += 100;
    }
    if (button_pressed[DOWN])
    { // Backward
      left_speed -= 100;
      right_speed -= 100;
    }
    if (button_pressed[LEFT])
    { // Turn left_speed
      left_speed -= 50;
      right_speed += 50;
    }
    if (button_pressed[RIGHT])
    { // Turn right_speed
      left_speed += 50;
      right_speed -= 50;
    }

    // Clamp and write speeds
    RedRobot::WriteDCMotor(2, constrain(left_speed, -100, 100));
    RedRobot::WriteDCMotor(3, constrain(right_speed, -100, 100));
  }

  void Button1_Pressed(bool buttonPressedDown)
  {
    if (buttonPressedDown) RobotControl::SelectMode(RobotControl::Mode::Drive);
  }

  void Button2_Pressed(bool buttonPressedDown)
  {
    if (buttonPressedDown) RobotControl::SelectMode(RobotControl::Mode::Drive);
  }

  void Button3_Pressed(bool buttonPressedDown)
  {
    if (buttonPressedDown) RobotControl::SelectMode(RobotControl::Mode::RampMotor);
  }

  void Button4_Pressed(bool buttonPressedDown)
  {
    if (buttonPressedDown) RobotControl::SelectMode(RobotControl::Mode::ClawServo);
  }

}

namespace RR_Controller
{
  void Joystick1(int8_t x, int8_t y)
  {
    if (useTankDrive)
      RedRobot::WriteDCMotor(2, y);
    else
    {
      int leftMotor = y + x;
      int rightMotor = y - x;

      // Normalize to [-100, 100]
      int maxMagnitude = max(abs(leftMotor), abs(rightMotor));

      if (maxMagnitude > 100)
      {
        leftMotor = leftMotor * 75 / maxMagnitude;
        rightMotor = rightMotor * 75 / maxMagnitude;
      }
      RedRobot::WriteDCMotor(2, leftMotor);
      RedRobot::WriteDCMotor(3, rightMotor);
    }
  }

  void Joystick2(int8_t x, int8_t y)
  {
    if (useTankDrive)
      RedRobot::WriteDCMotor(3, y);
  }

  /* DPad_Pressed is called everytime a D-Pad button is pressed or released.
   *
   * Only activates after RedRobot::StartRemoteControl() is called.
   * Button is one of {UP = 0, DOWN = 1, LEFT = 2, RIGHT = 3, CENTER = 4}
   */
  void DPad_Pressed(button_t Button, bool buttonPressedDown)
  {
    if (Button < UP || Button > CENTER) return;
    auto &button_pressed = rrDirections;
    button_pressed[Button] = buttonPressedDown;

    if (activeMode == RobotControl::Mode::RampMotor)
    {
      // Numbered button 3 combines both mechanisms: Up/Down runs the ramp
      // while held, and Left/Right sends the claw once to a fixed position.
      if (buttonPressedDown && (Button == LEFT || Button == RIGHT))
      {
        int16_t angle = Button == LEFT ? CLAW_LEFT_ANGLE_DEGREES
                                      : CLAW_RIGHT_ANGLE_DEGREES;
        for (uint8_t channel = 0; channel < 4; ++channel)
          RedRobot::SetServo(channel, angle);
      }

      int ramp_speed = 0;
      if (button_pressed[UP] && !button_pressed[DOWN])
      {
        ramp_speed = RAMP_UP_POWER;
      }
      if (button_pressed[DOWN] && !button_pressed[UP])
      {
        ramp_speed = RAMP_DOWN_POWER;
      }
      RedRobot::WriteDCMotor(RAMP_MOTOR_CHANNEL, ramp_speed);
      return;
    }

    if (activeMode == RobotControl::Mode::ClawServo)
    {
      if (!buttonPressedDown) return;

      if (Button == LEFT)
      {
        RedRobot::SetServo(CLAW_SERVO_CHANNEL, CLAW_LEFT_ANGLE_DEGREES);
      }
      if (Button == RIGHT)
      {
        RedRobot::SetServo(CLAW_SERVO_CHANNEL, CLAW_RIGHT_ANGLE_DEGREES);
      }
      return;
    }

    // TODO: edit your speed output to the yellow motors below!
    // stop = 0, slow = 50, fast = 100
    // range [-100,100]

    int left_speed = 0;
    int right_speed = 0;

    if (button_pressed[UP])
    { // Forward
      left_speed += 100;
      right_speed += 100;
    }
    if (button_pressed[DOWN])
    { // Backward
      left_speed -= 100;
      right_speed -= 100;
    }
    if (button_pressed[LEFT])
    { // Turn left_speed
      left_speed -= 50;
      right_speed += 50;
    }
    if (button_pressed[RIGHT])
    { // Turn right_speed
      left_speed += 50;
      right_speed -= 50;
    }

    // Clamp and write speeds
    RedRobot::WriteDCMotor(2, constrain(left_speed, -100, 100));
    RedRobot::WriteDCMotor(3, constrain(right_speed, -100, 100));
  }
  
  void ButtonX_Pressed(bool buttonPressedDown)
  {
    if (buttonPressedDown) RobotControl::SelectMode(RobotControl::Mode::Drive);
  }

  void ButtonY_Pressed(bool buttonPressedDown)
  {
    Serial.printf("Y pressed\n");
    if (buttonPressedDown) {
      RobotControl::SelectMode(RobotControl::Mode::Drive);
      useTankDrive = !useTankDrive;
    }
  }

  void ButtonB_Pressed(bool buttonPressedDown)
  {
    if (buttonPressedDown) RobotControl::SelectMode(RobotControl::Mode::Drive);
  }

  void ButtonA_Pressed(bool buttonPressedDown)
  {
    if (buttonPressedDown)
    {
      RobotControl::SelectMode(RobotControl::Mode::Drive);
      RedRobot::WriteDCMotor(0, 100);
    }
    else if (activeMode == RobotControl::Mode::Drive)
    {
      RedRobot::WriteDCMotor(0, 0);
    }
  }
}
