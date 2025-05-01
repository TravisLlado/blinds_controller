// blinds.h ////////////////////////////////////////////////////////////////////

#pragma once

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_timer.h"

// Class Definition ////////////////////////////////////////////////////////////

class Blinds {
public:
  enum Direction {
    CLOSE = 1,
    OPEN  = 0
  };

  enum LimitSwitchState {
    PRESSED  = 0,
    RELEASED = 1
  };

  enum MotorState {
    OFF = 0,
    ON  = 1
  };

  Blinds(gpio_num_t dirPin, gpio_num_t spdPin, gpio_num_t encPin,
      gpio_num_t limPin, uint32_t openTicks, uint32_t timeoutMs,
      float dutyCycle);

  void open();
  void close();
  void toggle();
  bool blindsAreOpen() const;

private:
  gpio_num_t dir_pin;
  gpio_num_t spd_pin;
  gpio_num_t enc_pin;
  gpio_num_t lim_pin;
  uint32_t open_ticks;
  uint32_t timeout_us;
  uint32_t duty_pwm;

  volatile uint32_t tick_count;

  static void IRAM_ATTR isrHandler(void* arg);
  void runMotor(const Direction dir);
  void stopMotor();
};

// End of file /////////////////////////////////////////////////////////////////
