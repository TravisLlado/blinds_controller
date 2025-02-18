////////////////////////////////////////////////////////////////////////////////
// Living Room Blinds Control Code, now without Alexa!
// written by Travis Llado
// last edited 2025-02-15
////////////////////////////////////////////////////////////////////////////////

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "blinds.h"
#include "pico/cyw43_arch.h"

////////////////////////////////////////////////////////////////////////////////

// Stepper motor wiring
// B1 Red
// A2 Blue
// B2 Green
// A1 Black

#define STEPS_PER_TURN  1600U
#define STEPS_PER_OPEN  (TURNS_TO_OPEN * STEPS_PER_TURN)
#define DIRECTION_CLOSE true
#define DIRECTION_OPEN  false

////////////////////////////////////////////////////////////////////////////////

void pin_init(const uint32_t pin, const bool state);
void stepper_direction(const bool direction);
void stepper_disable(const uint32_t pin);
void stepper_enable(const uint32_t pin);
void stepper_step(const uint32_t low_time);

////////////////////////////////////////////////////////////////////////////////

blinds_motors::blinds_motors(const bool blinds_are_open) {
  pin_init(PIN_STEPPER_ENABLE_1, true);
  pin_init(PIN_STEPPER_ENABLE_2, true);
  pin_init(PIN_STEPPER_ENABLE_3, true);
  pin_init(PIN_STEPPER_DIRECTION, false);
  pin_init(PIN_STEPPER_STEP, false);

  if (blinds_are_open) {
    position_ = OPEN;
  }
}

////////////////////////////////////////////////////////////////////////////////

void blinds_motors::close(void) {
  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
  if (position_ == OPEN) {
    turn(DIRECTION_CLOSE);
    position_ = CLOSED;
  }
  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
}

////////////////////////////////////////////////////////////////////////////////

void blinds_motors::open(void) {
  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, true);
  if (position_ == CLOSED) {
    turn(DIRECTION_OPEN);
    position_ = OPEN;
  }
  cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);
}

////////////////////////////////////////////////////////////////////////////////

void blinds_motors::turn(const bool direction) {
  stepper_direction(direction);
  stepper_enable(PIN_STEPPER_ENABLE_1);
  stepper_enable(PIN_STEPPER_ENABLE_2);
  stepper_enable(PIN_STEPPER_ENABLE_3);

  sleep_ms(DELAY_ENABLE);

  int hold_time{STEPPER_TICK_RAMP};
  for (uint32_t i = 0U; i < STEPS_PER_OPEN; i++) {
    stepper_step(hold_time);

    if (hold_time > STEPPER_TICK_LOW) {
      hold_time -= STEPPER_TICK_STEP;
    }
  }

  sleep_ms(DELAY_DISABLE);

  stepper_disable(PIN_STEPPER_ENABLE_1);
  stepper_disable(PIN_STEPPER_ENABLE_2);
  stepper_disable(PIN_STEPPER_ENABLE_3);
}

////////////////////////////////////////////////////////////////////////////////

void pin_init(const uint32_t pin, const bool state) {
  gpio_init(pin);
  gpio_set_dir(pin, GPIO_OUT);
  gpio_put(pin, state);
}

////////////////////////////////////////////////////////////////////////////////

void stepper_direction(const bool direction) {
  gpio_put(PIN_STEPPER_DIRECTION, direction);
}

////////////////////////////////////////////////////////////////////////////////

void stepper_disable(const uint32_t pin) {
  gpio_put(pin, true);
}

////////////////////////////////////////////////////////////////////////////////

void stepper_enable(const uint32_t pin) {
  gpio_put(pin, false);
}

////////////////////////////////////////////////////////////////////////////////

void stepper_step(const uint32_t low_time) {
  gpio_put(PIN_STEPPER_STEP, true);
  sleep_us(STEPPER_TICK_HIGH);
  gpio_put(PIN_STEPPER_STEP, false);
  sleep_us(low_time);
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
