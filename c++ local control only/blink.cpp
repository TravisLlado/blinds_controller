/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

////////////////////////////////////////////////////////////////////////////////

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
#define PIN_BUTTON            21U
#define PIN_LED               CYW43_WL_GPIO_LED_PIN

// Stepper motor wiring
// B1 Red
// A2 Blue
// B2 Green
// A1 Black 

////////////////////////////////////////////////////////////////////////////////

void button_init(void) {
  gpio_init(PIN_BUTTON);
  gpio_set_dir(PIN_BUTTON, GPIO_IN);
  gpio_pull_up(PIN_BUTTON);
}

bool button_position(void) {
  return !gpio_get(PIN_BUTTON);
}

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

void blinds_lower(void) {
  // can't do this yet
}

void blinds_raise(void) {
  // can't do this yet
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
    // Initialize radio
  if (cyw43_arch_init()) {
		return -1;
	}

  stepper_init(PIN_STEPPER_ENABLE_1, true);
  stepper_init(PIN_STEPPER_ENABLE_2, true);
  stepper_init(PIN_STEPPER_ENABLE_3, true);
  stepper_init(PIN_STEPPER_DIRECTION, false);
  stepper_init(PIN_STEPPER_STEP, false);
  button_init();

  // Initialize Onboard LED
  gpio_init(PIN_LED);
  gpio_set_dir(PIN_LED, GPIO_OUT);

  bool blinds_are_open{false};

  while(true) {

    const bool button_is_pressed = button_position();

    if (button_is_pressed) {
      if (blinds_are_open) {
        blinds_turn(DIRECTION_CLOSE);
        blinds_are_open = false;
      } else {
        blinds_turn(DIRECTION_OPEN);
        blinds_are_open = true;
      }
      
      cyw43_arch_gpio_put(PIN_LED, blinds_are_open);
    }

    sleep_ms(100U);
  }
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
