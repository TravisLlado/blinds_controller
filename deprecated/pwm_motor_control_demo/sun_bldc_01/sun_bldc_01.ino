// sun_bldc.ino ////////////////////////////////////////////////////////////////

// Includes ////////////////////////////////////////////////////////////////////

#include <Arduino.h>
#include "blinds.h"
#include "sun.h"

// Constants ///////////////////////////////////////////////////////////////////

// Blinds pin assignments
constexpr uint8_t DIR_PIN = 6U;
constexpr uint8_t SPD_PIN = 5U;
constexpr uint8_t ENC_PIN = 7U;
constexpr uint8_t LIM_PIN = 10U;

constexpr uint8_t BTN_PIN = 20U;

constexpr uint32_t TICK_PERIOD_MS = 10U;

// Globals /////////////////////////////////////////////////////////////////////

volatile bool global_button_triggered = false;

static bool global_reposition_done_sunrise = false;
static bool global_reposition_done_sunset = false;

constexpr uint32_t OPEN_TICKS = 4200U;
constexpr uint32_t TIMEOUT_MS = 15000U;

Blinds global_blinds(DIR_PIN, SPD_PIN, ENC_PIN, LIM_PIN, OPEN_TICKS, TIMEOUT_MS);

// Functions ///////////////////////////////////////////////////////////////////

void IRAM_ATTR onButtonPress() {
  global_button_triggered = true;
  Serial.println("Button has been pressed!");
}

// Setup ///////////////////////////////////////////////////////////////////////

void setup() {
  Serial.begin(115200);

  pinMode(BTN_PIN, INPUT_PULLUP);
  // Install ISR service and attach low-level interrupt
  attachInterrupt(digitalPinToInterrupt(BTN_PIN), onButtonPress, FALLING);

  setupNetworking();
}

// Loop ////////////////////////////////////////////////////////////////////////

void loop() {
  static unsigned long button_press_time = 0;
  static unsigned long last_debug_time = 0;
  const unsigned long now = getCurrentEpoch();
  const int seconds_since_midnight = now % (24 * 3600);

  // Time-Based Events /////////////////////////////////////////////////////////

  updateSunData(seconds_since_midnight);

  if (shouldResetDailyFlags(seconds_since_midnight)) {
    global_reposition_done_sunrise = false;
    global_reposition_done_sunset = false;
  }

  if (!global_reposition_done_sunrise && isSunriseTriggerTime(seconds_since_midnight)) {
    global_blinds.open();
    global_reposition_done_sunrise = true;
  }

  if (!global_reposition_done_sunset && isSunsetTriggerTime(seconds_since_midnight)) {
    global_blinds.close();
    global_reposition_done_sunset = true;
  }

  if (millis() - last_debug_time >= 1000U) {
    auto format_time = [](unsigned long seconds) {
      int h = (seconds / 3600) % 24;
      int m = (seconds / 60) % 60;
      int s = seconds % 60;
      char buf[9];
      snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
      return String(buf);
    };

    Serial.print("time: ");
    Serial.print(format_time(seconds_since_midnight));
    Serial.print(" position: ");
    Serial.print(global_blinds.blindsAreOpen() ? "OPEN" : "CLOSED");
    Serial.print(" sunrise: ");
    Serial.print(format_time(getSunriseTime()));
    Serial.print(" sunset: ");
    Serial.println(format_time(getSunsetTime()));

    last_debug_time = millis();
  }
  
  if (global_button_triggered) {
    if (button_press_time == 0) {
      button_press_time = millis();
    } else if (millis() - button_press_time >= 200U) {
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
