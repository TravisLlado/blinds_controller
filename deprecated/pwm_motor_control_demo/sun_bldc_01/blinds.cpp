// blinds.cpp //////////////////////////////////////////////////////////////////

#include "blinds.h"

// Public //////////////////////////////////////////////////////////////////

Blinds::Blinds(const uint8_t dirPin, const uint8_t spdPin, const uint8_t encPin, const uint8_t limPin,
               const uint32_t openTicks, const uint32_t timeoutMs)
  : dir_pin(dirPin), spd_pin(spdPin), enc_pin(encPin), lim_pin(limPin),
    open_ticks(openTicks), timeout_ms(timeoutMs),
    tick_count(0) {
  pinMode(dir_pin, OUTPUT);
  pinMode(spd_pin, OUTPUT);
  pinMode(enc_pin, INPUT_PULLUP);
  pinMode(lim_pin, INPUT_PULLUP);
  attachInterruptArg(digitalPinToInterrupt(enc_pin), isrHandler, this, RISING);
}

void Blinds::open() {
  if (!blindsAreOpen()) {
    tick_count = 0;
    runMotor(Direction::OPEN);
    const unsigned long start = millis();
    while ((tick_count < open_ticks) && ((millis() - start) < timeout_ms)) {
      // wait
    }
    stopMotor();
  }
}

void Blinds::close() {
  if (blindsAreOpen()) {
    runMotor(Direction::CLOSE);
    const unsigned long start = millis();
    while (blindsAreOpen() && ((millis() - start) < timeout_ms)) {
      // wait
    }
    stopMotor();
  }
}

void Blinds::toggle() {
  if (blindsAreOpen()) {
    close();
  } else {
    open();
  }
}

bool Blinds::blindsAreOpen() const {
  return digitalRead(lim_pin) == LimitSwitchState::RELEASED;
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
