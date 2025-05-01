// sun_bldc.ino ////////////////////////////////////////////////////////////////

// Includes ////////////////////////////////////////////////////////////////////

#include <Arduino.h>
#include "blinds.h"
#include "sun.h"

// Types ///////////////////////////////////////////////////////////////////////

enum ButtonState {
  PRESSED = LOW,
  RELEASED = HIGH
};

enum LedState {
  ON = LOW,
  OFF = HIGH
};

// Constants ///////////////////////////////////////////////////////////////////

// Blinds pin assignments
constexpr uint8_t DIR_PIN = 6U;
constexpr uint8_t SPD_PIN = 5U;
constexpr uint8_t ENC_PIN = 7U;
constexpr uint8_t LIM_PIN = 10U;

constexpr uint8_t BTN_PIN = 20U;
constexpr uint8_t LED_PIN = 8U;

constexpr uint32_t TICK_PERIOD_MS = 10U;

constexpr uint32_t BLINK_PERIOD_MS = 1000U;
constexpr uint32_t BLINK_DAY_ON_MS = BLINK_PERIOD_MS * 9U / 10U;   // 90% on
constexpr uint32_t BLINK_NIGHT_ON_MS = BLINK_PERIOD_MS * 1U / 10U; // 10% on

// Globals /////////////////////////////////////////////////////////////////////

static ButtonState global_last_button_state = ButtonState::RELEASED;
static LedState global_led_state = LedState::OFF;
static unsigned long global_last_led_toggle_time = 0U;

static bool global_reposition_done_sunrise = false;
static bool global_reposition_done_sunset = false;

Blinds global_blinds(DIR_PIN, SPD_PIN, ENC_PIN, LIM_PIN);

// Setup ///////////////////////////////////////////////////////////////////////

void setup() {
  Serial.begin(115200);

  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LedState::OFF);

  setupNetworking();
}

// Functions ////////////////////////////////////////////////////////////////////

void updateLedState(const bool sun_is_up) {
  const uint32_t on_time  = sun_is_up ? BLINK_DAY_ON_MS : BLINK_NIGHT_ON_MS;
  const uint32_t off_time = BLINK_PERIOD_MS - on_time;
  const uint32_t elapsed  = millis() - global_last_led_toggle_time;

  if (global_led_state == LedState::ON && elapsed >= on_time) {
    digitalWrite(LED_PIN, LedState::OFF);
    global_led_state = LedState::OFF;
    global_last_led_toggle_time = millis();
  } else if (global_led_state == LedState::OFF && elapsed >= off_time) {
    digitalWrite(LED_PIN, LedState::ON);
    global_led_state = LedState::ON;
    global_last_led_toggle_time = millis();
  }
}

// Loop ////////////////////////////////////////////////////////////////////////

void loop() {
  const unsigned long now = getCurrentEpoch();
  const int seconds_since_midnight = now % (24 * 3600);

  // LED Blink Logic ///////////////////////////////////////////////////////////

  updateLedState(sunIsUp(seconds_since_midnight));

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

  // Button Control ////////////////////////////////////////////////////////////

  const ButtonState current_button_state = static_cast<ButtonState>(digitalRead(BTN_PIN));

  if (current_button_state == ButtonState::PRESSED && global_last_button_state == ButtonState::RELEASED) {
    global_blinds.toggle();
  }

  global_last_button_state = current_button_state;

  delay(TICK_PERIOD_MS);
}

// End of file /////////////////////////////////////////////////////////////////
