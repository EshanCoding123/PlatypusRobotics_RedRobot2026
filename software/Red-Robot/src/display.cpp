#include <Arduino.h>

#include "display.h"
#include "pinouts.h"

namespace
{
  // Segment bit layout matches the STP08CP05M output order (OUT7..OUT0):
  // bit7..bit1 = a,b,c,d,e,f,g ; bit0 = decimal point.
  const uint8_t kSegmentTable[10] = {
      0b11111100, // 0
      0b01100000, // 1
      0b11011010, // 2
      0b11110010, // 3
      0b01100110, // 4
      0b10110110, // 5
      0b10111110, // 6
      0b11100000, // 7
      0b11111110, // 8
      0b11110110, // 9
  };

  // Left-to-right digit order: DP3 (tens), DP2 (ones), DP1 (tenths).
  const uint8_t kDigitSelectPins[3] = {BMS_D0, BMS_D1, BMS_D2};

  // "XX.X" -> decimal point lives on the ones digit (the middle one).
  constexpr uint8_t kDecimalPointDigit = 1;

  volatile uint8_t g_digitPattern[3] = {
      kSegmentTable[0],
      (uint8_t)(kSegmentTable[0] | 1),
      kSegmentTable[0],
  };

  void shiftOutByte(uint8_t value)
  {
    digitalWrite(BATMON_LATCH, LOW);
    for (int8_t i = 7; i >= 0; i--)
    {
      digitalWrite(BATMON_CLK, LOW);
      digitalWrite(BATMON_DATA, !!(value & (1 << i)));
      delayMicroseconds(2);
      digitalWrite(BATMON_CLK, HIGH);
      delayMicroseconds(2);
    }
    digitalWrite(BATMON_LATCH, HIGH);
  }

  void refreshTask(void *)
  {
    uint8_t digit = 0;
    for (;;)
    {
      for (uint8_t d = 0; d < 3; d++)
        digitalWrite(kDigitSelectPins[d], LOW);

      shiftOutByte(g_digitPattern[digit]);
      digitalWrite(kDigitSelectPins[digit], HIGH);

      digit = (digit + 1) % 3;
      vTaskDelay(pdMS_TO_TICKS(3)); // ~83Hz per-digit refresh, no visible flicker
    }
  }
} // namespace

void displayInit()
{
  pinMode(BATMON_LATCH, OUTPUT);
  pinMode(BATMON_CLK, OUTPUT);
  pinMode(BATMON_DATA, OUTPUT);

  for (uint8_t d = 0; d < 3; d++)
  {
    pinMode(kDigitSelectPins[d], OUTPUT);
    digitalWrite(kDigitSelectPins[d], LOW);
  }

  xTaskCreatePinnedToCore(refreshTask, "disp_refresh", 2048, nullptr, 1, nullptr, 1);
}

void displayNumber(uint16_t value)
{
  if (value > 999)
    value = 999;

  uint8_t digits[3] = {
      (uint8_t)(value / 100),
      (uint8_t)((value / 10) % 10),
      (uint8_t)(value % 10),
  };

  for (uint8_t d = 0; d < 3; d++)
  {
    uint8_t dp = (d == kDecimalPointDigit) ? 1 : 0;
    g_digitPattern[d] = kSegmentTable[digits[d]] | dp;
  }
}
