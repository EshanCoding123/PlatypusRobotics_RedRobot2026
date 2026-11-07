#include <Arduino.h>
#include "pinouts.h"
#include "display.h"

void helloTask(void *)
{
  for (;;)
  {
    Serial.println("Hello world");
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void setup()
{
  // put your setup code here, to run once:
  Serial.begin(115200);

  displayInit(); // starts showing "00.0"

  xTaskCreatePinnedToCore(helloTask, "hello", 2048, nullptr, 1, nullptr, 1);
}

void loop()
{
  // put your main code here, to run repeatedly:
}
