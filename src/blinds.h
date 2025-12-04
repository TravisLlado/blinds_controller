// blinds.h ////////////////////////////////////////////////////////////////////

#pragma once
#include <Arduino.h>
#include <cmath>

// Class Definition ////////////////////////////////////////////////////////////
class Blinds {
public:
    Blinds(uint32_t pin_a, uint32_t pin_b, uint32_t pin_c, uint32_t pin_enable,
        uint32_t pin_limit, uint32_t num_pole_pairs, float motion_length,
        float motion_timeout, float motor_speed_max); 
    
    bool blindsAreClosed() const;
    void close() const;
    void open() const;
    void toggle() const;
    
    // Mutators for configurable parameters
    void setMotorSpeedMax(float speed_max);
    void setMotionLength(float length);
    
    // Getters (optional, for verification)
    float getMotorSpeedMax() const { return motor_speed_max; }
    float getMotionLength() const { return motion_length; }
    float getMotionTimeout() const { return motion_timeout; }

private:
    const uint32_t pin_motor_phase_a;
    const uint32_t pin_motor_phase_b;
    const uint32_t pin_motor_phase_c;
    const uint32_t pin_enable;
    const uint32_t pin_limit;
    const uint32_t num_pole_pairs;
    float          motor_speed_max;  // Changed from const
    float          motion_length;    // Changed from const
    float          motion_timeout;   // Changed from const

    const uint32_t PWM_FREQUENCY          {20000U}; // hz
    const uint32_t PWM_RESOLUTION         {10U};    // bits
    const uint32_t PHASE_A_PWM_CHANNEL    {0U};
    const uint32_t PHASE_B_PWM_CHANNEL    {1U};
    const uint32_t PHASE_C_PWM_CHANNEL    {2U};
    const uint32_t TICK_FREQ              {5000U};  // Hz
    const float    SLOW_APPROACH_DISTANCE {10.0F};  // motor turns from expected stop
    const float    SLOW_APPROACH_SPEED    {0.25F};  // factor of max speed
    const float    RAMP_RATE              {600.0F}; // RPM/sec
    const float    MOTOR_AMPLITUDE_MAX    {0.8F};
    const float    MOTOR_AMPLITUDE_MIN    {0.2F};

    const uint32_t duty_max{(1U << PWM_RESOLUTION) - 1U};
    mutable float increment_max;           // Changed from const, made mutable
    const float increment_increment{RAMP_RATE / 60.0F / TICK_FREQ / TICK_FREQ};

    void motorMove(const bool close) const;
    void motorDisable() const;
    void motorEnable() const;
    void motorPosition(float electricalAngle, float amplitude) const;
    void recalculateIncrementMax();  // Helper to recalculate derived values
    void recalculateTimeout();       // Helper to recalculate timeout based on motion params
};

// End of file /////////////////////////////////////////////////////////////////