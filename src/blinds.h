// blinds.h ////////////////////////////////////////////////////////////////////

#pragma once
#include <Arduino.h>
#include <cmath>

// Class Definition ////////////////////////////////////////////////////////////
class Blinds {
public:
    enum MotorDirection {
        CLOSE = LOW,
        OPEN = HIGH
    };

    enum MotorState {
        ENABLE = HIGH,
        DISABLE = LOW
    };
    
    Blinds(uint8_t pin_a, uint8_t pin_b, uint8_t pin_c, uint8_t pin_enable,
        uint8_t pin_limit, uint8_t num_pole_pairs, float motion_length,
        uint32_t motion_timeout, float motor_speed_max); 
    
    bool blindsAreClosed() const;
    void close() const;
    void open() const;
    void toggle() const;

private:
    const uint8_t pin_motor_phase_a;
    const uint8_t pin_motor_phase_b;
    const uint8_t pin_motor_phase_c;
    const uint8_t pin_enable;
    const uint8_t pin_limit;
    const uint8_t num_pole_pairs;
    const float   motor_speed_max;

    const float   motion_length;    // motor turns
    const uint32_t motion_timeout;  // ms

    const uint32_t  TICK_PERIOD     {1U};             // ms
    const uint8_t   LIMIT_DEBOUNCE_COUNT{5U};
    const float     SLOW_APPROACH_DISTANCE    {10.0F};  // motor turns from expected stop
    const float     SLOW_APPROACH_SPEED       {0.25F};     // factor of max speed
    const float     RAMP_RATE{200.0F};              // RPM/sec
    const float     MOTOR_AMPLITUDE_MAX{0.9F};
    const int       PWM_FREQUENCY{20000};             // hz
    const int       PWM_RESOLUTION{8};                // bits
    const int       PWM_MAX_VALUE{(1 << PWM_RESOLUTION) - 1};
    const uint8_t   PHASE_A_PWM_CHANNEL{0};
    const uint8_t   PHASE_B_PWM_CHANNEL{1};
    const uint8_t   PHASE_C_PWM_CHANNEL{2};

    const uint32_t duty_max = (1 << PWM_RESOLUTION) - 1;
    const float increment_max{motor_speed_max / 60.0F * TICK_PERIOD / 1000U};
    const float increment_increment{RAMP_RATE / 60.0F * TICK_PERIOD / 1000U / 1000U};

    void motorDisable() const;
    void motorEnable() const;
    void motorPosition(float electricalAngle, float amplitude) const;
};

// End of file /////////////////////////////////////////////////////////////////
