// blinds.h ////////////////////////////////////////////////////////////////////

#pragma once

#include <Arduino.h>

// Class Definition ////////////////////////////////////////////////////////////

class Blinds {
public:
  enum Direction {
    CLOSE = HIGH,
    OPEN  = LOW
  };

  enum LimitSwitchState {
    PRESSED  = LOW,
    RELEASED = HIGH
  };

  enum MotorState {
    OFF = LOW,
    ON  = HIGH
  };

  Blinds(uint8_t dirPin, uint8_t spdPin, uint8_t encPin,
      uint8_t limPin, uint32_t travelLength, uint32_t timeoutMs,
      float dutyCycle, uint8_t pwmChannel);

  void open();
  void close();
  void toggle();
  bool blindsAreClosed() const;

private:
  const uint8_t dir_pin;
  const uint8_t spd_pin;
  const uint8_t enc_pin;
  const uint8_t lim_pin;
  const uint32_t travel_length;
  const uint32_t timeout_us;
  const uint32_t duty_pwm;
  const uint8_t ledc_channel;

  volatile uint32_t tick_count;
  
  // Static interrupt handlers - one for each possible instance
  static void IRAM_ATTR handleInterrupt0();
  static void IRAM_ATTR handleInterrupt1();
  static void IRAM_ATTR handleInterrupt2();
  
  // Static pointers to instances
  static Blinds* instance0;
  static Blinds* instance1;
  static Blinds* instance2;
  
  // Increment tick counter
  void IRAM_ATTR incrementTickCount();
  
  // Regular methods
  void runMotor(const Direction dir);
  void stopMotor();
};

// End of file /////////////////////////////////////////////////////////////////
