// sun_bldc.ino ////////////////////////////////////////////////////////////////

// Includes ////////////////////////////////////////////////////////////////////

#include <Arduino.h>
#include "blinds.h"

// Constants ///////////////////////////////////////////////////////////////////

constexpr uint8_t  DIR_PIN = 6U;
constexpr uint8_t  SPD_PIN = 5U;
constexpr uint8_t  ENC_PIN = 7U;
constexpr uint8_t  LIM_PIN = 10U;
constexpr float    DUTY_CYCLE = 1.0F;
constexpr uint32_t OPEN_TICKS = 8200U;
constexpr uint32_t TIMEOUT_MS = 15000U;

constexpr uint8_t BTN_PIN = 20U;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 200U;
constexpr uint32_t TICK_PERIOD_MS = 10U;

// Globals /////////////////////////////////////////////////////////////////////

volatile bool global_button_triggered = false;

Blinds global_blinds(DIR_PIN, SPD_PIN, ENC_PIN, LIM_PIN, OPEN_TICKS, TIMEOUT_MS, DUTY_CYCLE);

// Functions ///////////////////////////////////////////////////////////////////

void IRAM_ATTR onButtonPress() {
  global_button_triggered = true;
  Serial.println("Button has been pressed!");
}

// Setup ///////////////////////////////////////////////////////////////////////

void setup() {
  Serial.begin(115200);

  pinMode(BTN_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BTN_PIN), onButtonPress, FALLING);
}

// Loop ////////////////////////////////////////////////////////////////////////

void loop() {
  static unsigned long button_press_time = 0;
  
  if (global_button_triggered) {
    if (button_press_time == 0) {
      button_press_time = millis();
    } else if (millis() - button_press_time >= BUTTON_DEBOUNCE_MS) {
      global_blinds.toggle();
      global_button_triggered = false;
      button_press_time = 0;
    }
  } else {
    button_press_time = 0;
  }
  
  delay(TICK_PERIOD_MS);
}

// End of file /////////////////////////////////////////////////////////////////
