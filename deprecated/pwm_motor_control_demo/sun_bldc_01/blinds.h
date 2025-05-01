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

  Blinds(const uint8_t dirPin, const uint8_t spdPin, const uint8_t encPin, const uint8_t limPin,
         const uint32_t openTicks, const uint32_t timeoutMs);

  void open();
  void close();
  void toggle();
  bool blindsAreOpen() const;

private:
  uint32_t open_ticks;
  uint32_t timeout_ms;
  uint8_t dir_pin;
  uint8_t spd_pin;
  uint8_t enc_pin;
  uint8_t lim_pin;

  volatile uint32_t tick_count;

  static void IRAM_ATTR isrHandler(void* arg);
  void runMotor(const Direction dir);
  void stopMotor();
};

// End of file /////////////////////////////////////////////////////////////////