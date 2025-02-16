#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "blinds.h"

////////////////////////////////////////////////////////////////////////////////

// Stepper motor wiring
// B1 Red
// A2 Blue
// B2 Green
// A1 Black

#define TURNS_TO_OPEN     5.5F          // full rotations
#define DELAY_ENABLE      100U          // ms
#define DELAY_DISABLE     DELAY_ENABLE  // ms
#define DELAY_TICK        100U          // ms
#define STEPPER_TICK_HIGH 20U           // μs
#define STEPPER_TICK_LOW  200U          // μs
#define STEPPER_TICK_RAMP 2000U         // μs
#define STEPPER_TICK_STEP 5U            // μs/step

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

void pin_init(const uint32_t pin, const bool state) {
  gpio_init(pin);
  gpio_set_dir(pin, GPIO_OUT);
  gpio_put(pin, state);
}

void stepper_direction(const bool direction) {
  gpio_put(PIN_STEPPER_DIRECTION, direction);
}

void stepper_disable(const uint32_t pin) {
  gpio_put(pin, true);
}

void stepper_enable(const uint32_t pin) {
  gpio_put(pin, false);
}

void stepper_step(const uint32_t low_time) {
  gpio_put(PIN_STEPPER_STEP, true);
  sleep_us(STEPPER_TICK_HIGH);
  gpio_put(PIN_STEPPER_STEP, false);
  sleep_us(low_time);
}

////////////////////////////////////////////////////////////////////////////////

blinds::blinds(const bool blinds_are_open) {
  pin_init(PIN_STEPPER_ENABLE_1, true);
  pin_init(PIN_STEPPER_ENABLE_2, true);
  pin_init(PIN_STEPPER_ENABLE_3, true);
  pin_init(PIN_STEPPER_DIRECTION, false);
  pin_init(PIN_STEPPER_STEP, false);

  if (blinds_are_open) {
    position = OPEN;
  }
}

void blinds::open(void) {
  if (position == CLOSED) {
    turn(DIRECTION_OPEN);
    position = OPEN;
  }
}

void blinds::close(void) {
  if (position == OPEN) {
    turn(DIRECTION_CLOSE);
    position = CLOSED;
  }
}

void blinds::turn(const bool direction) {
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
// End of file
////////////////////////////////////////////////////////////////////////////////
