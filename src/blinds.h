// blinds.h ////////////////////////////////////////////////////////////////////
#pragma once
#include <Arduino.h>

// Class Definition ////////////////////////////////////////////////////////////
class Blinds {
public:
    enum MotorDirection {
        CLOSE = LOW,
        OPEN = HIGH
    };
    
    enum LimitSwitchState {
        PRESSED = LOW,
        RELEASED = HIGH
    };
    
    enum BrakeState {
        ENGAGED = LOW,
        DISENGAGED = HIGH
    };
    
    Blinds(uint8_t dirPin, uint8_t spdPin, uint8_t encPin,
           uint8_t limPin, uint8_t brakePin, uint32_t travelLength,
           uint32_t timeoutMs, float openSpeed, float closeSpeed, uint32_t rampDurationMs);
    
    bool blindsAreClosed() const;
    void close() const;
    void open() const;
    void toggle() const;

private:
    const uint8_t dir_pin;
    const uint8_t spd_pin;
    const uint8_t enc_pin;
    const uint8_t lim_pin;
    const uint8_t brake_pin;
    const uint32_t travel_length;
    const uint32_t timeout_us;
    const uint32_t open_speed_pwm;
    const uint32_t close_speed_pwm;
    const uint32_t ramp_duration_ms;
    
    // Motor control methods
    void brakeDisengage() const;
    void brakeEngage() const;
    void motorMove(MotorDirection direction, uint32_t target_speed_pwm, bool (*exitCondition)(const Blinds*)) const;
    void motorStop() const;
};

// Global interrupt handlers
void IRAM_ATTR encoderInterrupt();

// End of file /////////////////////////////////////////////////////////////////
