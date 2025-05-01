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

  enum class Position {
    OPENED,
    CLOSED,
    UNKNOWN
  };

  Blinds(const uint8_t dirPin, const uint8_t spdPin, const uint8_t encPin, const uint8_t limPin);

  void open();
  void close();
  void toggle();
  Position getPosition() const;

private:
  static constexpr uint32_t OPEN_TICKS = 4200U;
  static constexpr uint32_t TIMEOUT_MS = 15000U;
  uint8_t dir_pin;
  uint8_t spd_pin;
  uint8_t enc_pin;
  uint8_t lim_pin;

  volatile uint32_t tick_count;
  Position position;

  static void IRAM_ATTR isrHandler(void* arg);
  void runMotor(const Direction dir);
  void stopMotor();
};

// End of file /////////////////////////////////////////////////////////////////
