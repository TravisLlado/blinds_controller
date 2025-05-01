////////////////////////////////////////////////////////////////////////////////
// Living Room Blinds Control Code, now without Alexa!
// written by Travis Llado
// last edited 2025-02-20
////////////////////////////////////////////////////////////////////////////////

#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "blinds.h"

////////////////////////////////////////////////////////////////////////////////

// Stepper motor wiring
// B1 Red
// A2 Blue
// B2 Green
// A1 Black

#define STEPS_PER_TURN  1600U
#define STEPS_PER_OPEN  (TURNS_TO_OPEN * STEPS_PER_TURN)
#define VALUE_CLOSE     true
#define VALUE_OPEN      false
#define VALUE_DISABLE   true
#define VALUE_ENABLE    false
#define VALUE_STEP_HIGH true
#define VALUE_STEP_LOW  false

////////////////////////////////////////////////////////////////////////////////

blinds_motors::blinds_motors(void) {
  pin_init(PIN_STEPPER_ENABLE_1, VALUE_DISABLE);
  pin_init(PIN_STEPPER_ENABLE_2, VALUE_DISABLE);
  pin_init(PIN_STEPPER_ENABLE_3, VALUE_DISABLE);
  pin_init(PIN_STEPPER_DIRECTION, VALUE_OPEN);
  pin_init(PIN_STEPPER_STEP, VALUE_STEP_LOW);
}

////////////////////////////////////////////////////////////////////////////////

void blinds_motors::close(void) {
    turn(VALUE_CLOSE);
}

////////////////////////////////////////////////////////////////////////////////

void blinds_motors::open(void) {
    turn(VALUE_OPEN);
}

////////////////////////////////////////////////////////////////////////////////

void blinds_motors::turn(const bool direction) {
  gpio_put(PIN_STEPPER_DIRECTION, direction);
  gpio_put(PIN_STEPPER_ENABLE_1, VALUE_ENABLE);
  gpio_put(PIN_STEPPER_ENABLE_2, VALUE_ENABLE);
  gpio_put(PIN_STEPPER_ENABLE_3, VALUE_ENABLE);

  sleep_ms(DELAY_ENABLE);

  int hold_time{STEPPER_TICK_RAMP};
  for (uint32_t i = 0U; i < STEPS_PER_OPEN; i++) {
    gpio_put(PIN_STEPPER_STEP, VALUE_STEP_HIGH);
    sleep_us(STEPPER_TICK_HIGH);
    gpio_put(PIN_STEPPER_STEP, VALUE_STEP_LOW);
    sleep_us(hold_time);

    if (hold_time > STEPPER_TICK_LOW) {
      hold_time -= STEPPER_TICK_STEP;
    }
  }

  sleep_ms(DELAY_DISABLE);

  gpio_put(PIN_STEPPER_ENABLE_1, VALUE_DISABLE);
  gpio_put(PIN_STEPPER_ENABLE_2, VALUE_DISABLE);
  gpio_put(PIN_STEPPER_ENABLE_3, VALUE_DISABLE);
}

////////////////////////////////////////////////////////////////////////////////

void blinds_motors::pin_init(const uint32_t pin, const bool state) {
  gpio_init(pin);
  gpio_set_dir(pin, GPIO_OUT);
  gpio_put(pin, state);
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
