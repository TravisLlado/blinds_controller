// main.cpp ////////////////////////////////////////////////////////////////////

// Includes ////////////////////////////////////////////////////////////////////

#include <Arduino.h>
#include "blinds.h"
#include "driver/gpio.h"
#include "esp_timer.h"

// Constants ///////////////////////////////////////////////////////////////////

constexpr gpio_num_t BTN_PIN{GPIO_NUM_5};
constexpr uint32_t TICK_PERIOD{10U}; // ms
constexpr uint32_t BUTTON_PRESS_LENGTH{200000U}; // us

constexpr uint32_t OPEN_TICKS{4000U};
constexpr uint32_t TIMEOUT{60000U}; // ms
constexpr float DUTY_CYCLE{0.4F};

// Globals /////////////////////////////////////////////////////////////////////

volatile bool global_button_triggered = false;

//                     Direction   Speed       Encoder      Limit        Open Length Timeout Duty Cycle   PWM Channel
Blinds global_blinds_1(GPIO_NUM_6, GPIO_NUM_2, GPIO_NUM_3,  GPIO_NUM_4,  OPEN_TICKS, TIMEOUT, DUTY_CYCLE, LEDC_CHANNEL_0);
Blinds global_blinds_2(GPIO_NUM_7, GPIO_NUM_1, GPIO_NUM_10, GPIO_NUM_9,  OPEN_TICKS, TIMEOUT, DUTY_CYCLE, LEDC_CHANNEL_1);
Blinds global_blinds_3(GPIO_NUM_8, GPIO_NUM_0, GPIO_NUM_21, GPIO_NUM_20, OPEN_TICKS, TIMEOUT, DUTY_CYCLE, LEDC_CHANNEL_2);

// Functions ///////////////////////////////////////////////////////////////////

extern "C" void IRAM_ATTR onButtonPress(void* arg) {
  global_button_triggered = true;
}

// main ////////////////////////////////////////////////////////////////////////

extern "C" void app_main() {
  initArduino();  // Required for sun.cpp Arduino networking
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

// Loop ////////////////////////////////////////////////////////////////////////
  uint64_t button_press_time_us = 0;

  while (true) {
    if (global_button_triggered) {
      if (button_press_time_us == 0) {
        button_press_time_us = esp_timer_get_time();
      } else if ((esp_timer_get_time() - button_press_time_us) >= BUTTON_PRESS_LENGTH) {
        if (global_blinds_1.blindsAreClosed() && 
            global_blinds_2.blindsAreClosed() && 
            global_blinds_3.blindsAreClosed()) {
          global_blinds_1.open();
          global_blinds_2.open();
          global_blinds_3.open();
        } else {
          global_blinds_1.close();
          global_blinds_2.close();
          global_blinds_3.close();
        }

        global_button_triggered = false;
        button_press_time_us = 0;
      }
    } else {
      button_press_time_us = 0;
    }

    vTaskDelay(pdMS_TO_TICKS(TICK_PERIOD));
  }
}

// End of file /////////////////////////////////////////////////////////////////
