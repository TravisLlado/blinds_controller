// blinds.cpp //////////////////////////////////////////////////////////////////

#include "blinds.h"

// Constructor //////////////////////////////////////////////////////////////////

Blinds::Blinds(const uint8_t dirPin, const uint8_t spdPin, const uint8_t encPin, const uint8_t limPin)
  : dir_pin(dirPin), spd_pin(spdPin), enc_pin(encPin), lim_pin(limPin),
    tick_count(0), position(Position::UNKNOWN) {
  pinMode(dir_pin, OUTPUT);
  pinMode(spd_pin, OUTPUT);
  pinMode(enc_pin, INPUT_PULLUP);
  pinMode(lim_pin, INPUT_PULLUP);
  attachInterruptArg(digitalPinToInterrupt(enc_pin), isrHandler, this, RISING);
}

// Behavior /////////////////////////////////////////////////////////////////////

void Blinds::open() {
  if (position == Position::CLOSED) {
    tick_count = 0;
    runMotor(Direction::OPEN);

    const unsigned long start = millis();
    while ((tick_count < OPEN_TICKS) && ((millis() - start) < TIMEOUT_MS)) {
      // wait
    }

    stopMotor();
    position = Position::OPENED;
  }
}

void Blinds::close() {
  if (position != Position::CLOSED) {
    runMotor(Direction::CLOSE);
    const unsigned long start = millis();

    while ((digitalRead(lim_pin) == LimitSwitchState::RELEASED) &&
           ((millis() - start) < TIMEOUT_MS)) {
      // wait
    }

    stopMotor();
    position = Position::CLOSED;
  }
}

void Blinds::toggle() {
  if (digitalRead(lim_pin) == LimitSwitchState::PRESSED) {
    open();
  } else {
    close();
  }
}

Blinds::Position Blinds::getPosition() const {
  return position;
}

// Private //////////////////////////////////////////////////////////////////////

void IRAM_ATTR Blinds::isrHandler(void* arg) {
  static_cast<Blinds*>(arg)->tick_count++;
}

void Blinds::runMotor(const Direction dir) {
  digitalWrite(dir_pin, dir);
  digitalWrite(spd_pin, MotorState::ON);
}

void Blinds::stopMotor() {
  digitalWrite(spd_pin, MotorState::OFF);
}

// End of file /////////////////////////////////////////////////////////////////
