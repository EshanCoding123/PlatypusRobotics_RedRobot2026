#pragma once

#include <stdint.h>

// Starts the background task that multiplexes the 3-digit battery monitor
// display. Call once from setup(). Display shows "00.0" until the first
// displayNumber() call.
void displayInit();

// Shows `value` (0-999) as "XX.X" (i.e. value is the number in tenths).
// Values above 999 are clamped to 999.
void displayNumber(uint16_t value);
