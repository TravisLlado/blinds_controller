// main.cpp ////////////////////////////////////////////////////////////////////

// Includes ////////////////////////////////////////////////////////////////////

#include <Arduino.h>
#include "blinds.h"
#include "sun.h"
#include "driver/gpio.h"
#include "esp_timer.h"

// Constants ///////////////////////////////////////////////////////////////////

// Blinds pin assignments
constexpr gpio_num_t DIR_PIN = GPIO_NUM_6;
constexpr gpio_num_t SPD_PIN = GPIO_NUM_5;
constexpr gpio_num_t ENC_PIN = GPIO_NUM_7;
constexpr gpio_num_t LIM_PIN = GPIO_NUM_10;

constexpr gpio_num_t BTN_PIN = GPIO_NUM_20;

constexpr uint32_t TICK_PERIOD_MS = 10U;

// Globals /////////////////////////////////////////////////////////////////////

volatile bool global_button_triggered = false;

static bool global_reposition_done_sunrise = false;
static bool global_reposition_done_sunset = false;

constexpr uint32_t OPEN_TICKS = 4200U;
constexpr uint32_t TIMEOUT_MS = 15000U;

Blinds global_blinds(DIR_PIN, SPD_PIN, ENC_PIN, LIM_PIN, OPEN_TICKS,
    TIMEOUT_MS);

// Functions ///////////////////////////////////////////////////////////////////

extern "C" void IRAM_ATTR onButtonPress() {
  global_button_triggered = true;
  Serial.println("Button has been pressed!");
}

// main ////////////////////////////////////////////////////////////////////////

extern "C" void app_main() {
  initArduino();  // Required for sun.cpp Arduino networking
  Serial.begin(115200);
  delay(100);

  // Configure button input with pull-up
  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_NEGEDGE;
  io_conf.mode = GPIO_MODE_INPUT;
  io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
  io_conf.pin_bit_mask = (1ULL << BTN_PIN);
  gpio_config(&io_conf);

  // Install ISR and attach button handler
  gpio_install_isr_service(0);
  gpio_isr_handler_add(BTN_PIN, onButtonPress, nullptr);

  setupNetworking();

// Loop ////////////////////////////////////////////////////////////////////////
  uint64_t last_debug_time_us = 0;
  uint64_t button_press_time_us = 0;

  while (true) {
    const unsigned long now = getCurrentEpoch();
    const int seconds_since_midnight = now % (24 * 3600);

// Scheduled Events ////////////////////////////////////////////////////////////

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

    if ((esp_timer_get_time() - last_debug_time_us) >= 1000000U) {
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

      last_debug_time_us = esp_timer_get_time();
    }

// Button handling /////////////////////////////////////////////////////////////

    if (global_button_triggered) {
      if (button_press_time_us == 0) {
        button_press_time_us = esp_timer_get_time();
      } else if ((esp_timer_get_time() - button_press_time_us) >= 200000U) {
        global_blinds.toggle();
        global_button_triggered = false;
        button_press_time_us = 0;
      }
    } else {
      button_press_time_us = 0;
    }

    vTaskDelay(pdMS_TO_TICKS(TICK_PERIOD_MS));
  }
}

// End of file /////////////////////////////////////////////////////////////////
