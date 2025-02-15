////////////////////////////////////////////////////////////////////////////////
// Living Room Blinds Control Code
// written by Travis Llado
// last edited 2025-02-14
////////////////////////////////////////////////////////////////////////////////

#include <vector>
#include "hardware/gpio.h"
#include "hardware/rtc.h"
#include "pico/stdlib.h"
#include "pico/util/datetime.h"
#include "llado_time.h"

////////////////////////////////////////////////////////////////////////////////

#define POSITION_OPEN   true
#define POSITION_CLOSED false

#define STARTUP_YEAR      2025
#define STARTUP_MONTH     2
#define STARTUP_DAY       15
#define STARTUP_HOUR      12
#define STARTUP_MINUTE    00
#define STARTUP_POSITION  POSITION_CLOSED

#define TIME_OPEN   7
#define TIME_CLOSE  6

#define SUMMER_SOLSTICE_MONTH 6
#define SUMMER_SOLSTICE_DAY   20
#define SUMMER_SOLSTICE_SUNRISE_HOUR    5
#define SUMMER_SOLSTICE_SUNRISE_MINUTE  47
#define SUMMER_SOLSTICE_SUNSET_HOUR     20
#define SUMMER_SOLSTICE_SUNSET_MINUTE   34

#define WINTER_SOLSTICE_MONTH 12
#define WINTER_SOLSTICE_DAY   20
#define WINTER_SOLSTICE_SUNRISE_HOUR    8
#define WINTER_SOLSTICE_SUNRISE_MINUTE  21
#define WINTER_SOLSTICE_SUNSET_HOUR     5
#define WINTER_SOLSTICE_SUNSET_MINUTE   53

#define TURNS_TO_OPEN   5.5F
#define DELAY_WINDOW    250U  // ms
#define DELAY_TICK      100U  // ms
#define TEST_WAVE_HIGH  20U   // us
#define TEST_WAVE_LOW   200U  // us
#define TEST_WAVE_LOW_SLOW_START 2000U //us
#define SLOW_START_STEP 5U // us/step

#define STEPS_PER_TURN  1600U
#define STEPS_PER_OPEN  (TURNS_TO_OPEN * STEPS_PER_TURN)
#define DIRECTION_CLOSE true
#define DIRECTION_OPEN  false

#define PIN_STEPPER_DIRECTION 16U
#define PIN_STEPPER_STEP      17U
#define PIN_STEPPER_ENABLE_1  18U
#define PIN_STEPPER_ENABLE_2  19U
#define PIN_STEPPER_ENABLE_3  20U
// #define PIN_BUTTON            21U
// #define PIN_LED               CYW43_WL_GPIO_LED_PIN

// Stepper motor wiring
// B1 Red
// A2 Blue
// B2 Green
// A1 Black 

////////////////////////////////////////////////////////////////////////////////

void stepper_direction(const uint32_t direction) {
  gpio_put(PIN_STEPPER_DIRECTION, direction);
}

void stepper_disable(const uint32_t pin) {
  gpio_put(pin, true);
}

void stepper_enable(const uint32_t pin) {
  gpio_put(pin, false);
}

void stepper_init(const uint32_t pin, const bool state) {
  gpio_init(pin);
  gpio_set_dir(pin, GPIO_OUT);
  gpio_put(pin, state);
}

void stepper_step(const uint32_t low_time) {
  gpio_put(PIN_STEPPER_STEP, true);
  sleep_us(TEST_WAVE_HIGH);
  gpio_put(PIN_STEPPER_STEP, false);
  sleep_us(low_time);
}

void blinds_turn(const uint32_t direction) {
  const uint32_t pins[]{PIN_STEPPER_ENABLE_1, PIN_STEPPER_ENABLE_2, PIN_STEPPER_ENABLE_3};
  stepper_direction(direction);

  for (auto pin : pins) {
    sleep_ms(DELAY_WINDOW);
    stepper_enable(pin);
    int hold_time{TEST_WAVE_LOW_SLOW_START};
    for (uint32_t i = 0U; i < STEPS_PER_OPEN; i++) {
      hold_time = (hold_time < TEST_WAVE_LOW) ? TEST_WAVE_LOW : (hold_time - SLOW_START_STEP);
      stepper_step(hold_time);
    }
    stepper_disable(pin);
  }
}

////////////////////////////////////////////////////////////////////////////////

int main() {
  datetime_t now = {
    .year  = STARTUP_YEAR,
    .month = STARTUP_MONTH,
    .day   = STARTUP_DAY,
    .dotw  = 0,
    .hour  = STARTUP_HOUR,
    .min   = STARTUP_MINUTE,
    .sec   = 0
  };

  rtc_init();
  rtc_set_datetime(&now);

  stepper_init(PIN_STEPPER_ENABLE_1, true);
  stepper_init(PIN_STEPPER_ENABLE_2, true);
  stepper_init(PIN_STEPPER_ENABLE_3, true);
  stepper_init(PIN_STEPPER_DIRECTION, false);
  stepper_init(PIN_STEPPER_STEP, false);

  bool blinds_are_open{STARTUP_POSITION};
  uint32_t previous_hour{0U};

  while (true) {
    rtc_get_datetime(&now);

    if (now.hour != previous_hour) {
      switch(now.hour) {
        case TIME_OPEN:
          blinds_turn(DIRECTION_OPEN);
          blinds_are_open = true;
          break;
        case TIME_CLOSE:
          blinds_turn(DIRECTION_CLOSE);
          blinds_are_open = false;
          break;
      }
    }

    previous_hour = now.hour;

    sleep_ms(100U);
  }
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
