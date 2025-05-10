// blinds.cpp //////////////////////////////////////////////////////////////////

#include "blinds.h"

// Constants ///////////////////////////////////////////////////////////////////

constexpr uint32_t PWM_FREQ_HZ{20000U};
constexpr uint8_t PWM_RES_BITS{8U};
constexpr uint32_t ramp_step{1U};
constexpr uint32_t ramp_period{10U};  // ms

// Static member initialization
Blinds* Blinds::instance0 = nullptr;
Blinds* Blinds::instance1 = nullptr;
Blinds* Blinds::instance2 = nullptr;

// Public //////////////////////////////////////////////////////////////////////

Blinds::Blinds(uint8_t dirPin, uint8_t spdPin, uint8_t encPin,
    uint8_t limPin, uint32_t openTicks, uint32_t timeoutMs, 
    float dutyCycle, uint8_t pwmChannel)
    : dir_pin(dirPin), spd_pin(spdPin), enc_pin(encPin), lim_pin(limPin),
      open_ticks(openTicks), timeout_us(timeoutMs * 1000U),
      duty_pwm(static_cast<uint8_t>(dutyCycle * 255.0F)), // 8-bit resolution (0-255)
      ledc_channel(pwmChannel),
      tick_count(0U) {

  // Configure GPIOs
  pinMode(dir_pin, OUTPUT);
  pinMode(spd_pin, OUTPUT);
  pinMode(enc_pin, INPUT_PULLUP);
  pinMode(lim_pin, INPUT_PULLUP);

  // PWM Setup using Arduino's ledcSetup function
  ledcSetup(ledc_channel, PWM_FREQ_HZ, PWM_RES_BITS);
  ledcAttachPin(spd_pin, ledc_channel);
  stopMotor();

  // Register this instance to the appropriate static pointer based on pwmChannel
  // This assumes pwmChannel is 0, 1, or 2
  switch (pwmChannel) {
    case 0:
      instance0 = this;
      attachInterrupt(digitalPinToInterrupt(enc_pin), handleInterrupt0, RISING);
      break;
    case 1:
      instance1 = this;
      attachInterrupt(digitalPinToInterrupt(enc_pin), handleInterrupt1, RISING);
      break;
    case 2:
      instance2 = this;
      attachInterrupt(digitalPinToInterrupt(enc_pin), handleInterrupt2, RISING);
      break;
    default:
      break;
  }
}

void Blinds::open() {
  if (blindsAreClosed()) {
    const uint64_t start{micros()};
    uint32_t current_duty{0U};
    tick_count = 0U;

    digitalWrite(dir_pin, Direction::OPEN);

    while ((tick_count < open_ticks) && ((micros() - start) < timeout_us)) {
      current_duty = std::min((current_duty + ramp_step), (duty_pwm));
      ledcWrite(ledc_channel, current_duty);
      delay(ramp_period);
    }
    
    stopMotor();
  }
}

void Blinds::close() {
  if (!blindsAreClosed()) {
    const uint64_t start{micros()};
    uint32_t current_duty{0U};

    digitalWrite(dir_pin, Direction::CLOSE);

    while (!blindsAreClosed() && ((micros() - start) < timeout_us)) {
      current_duty = std::min((current_duty + ramp_step), (duty_pwm));
      ledcWrite(ledc_channel, current_duty);
      delay(ramp_period);
    }
    
    stopMotor();
  }
}
  
void Blinds::toggle() {
  if (blindsAreClosed()) {
    open();
  } else {
    close();
  }
}

bool Blinds::blindsAreClosed() const {
  return (digitalRead(lim_pin) == LimitSwitchState::PRESSED);
}

// Private /////////////////////////////////////////////////////////////////////

void IRAM_ATTR Blinds::handleInterrupt0() {
  if (instance0) {
    instance0->incrementTickCount();
  }
}

void IRAM_ATTR Blinds::handleInterrupt1() {
  if (instance1) {
    instance1->incrementTickCount();
  }
}

void IRAM_ATTR Blinds::handleInterrupt2() {
  if (instance2) {
    instance2->incrementTickCount();
  }
}

void IRAM_ATTR Blinds::incrementTickCount() {
  tick_count++;
}
  
void Blinds::stopMotor() {
  ledcWrite(ledc_channel, 0);
}

// End of file /////////////////////////////////////////////////////////////////
