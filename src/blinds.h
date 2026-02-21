// blinds.h ////////////////////////////////////////////////////////////////////

#pragma once
#include <Arduino.h>
#include <cmath>

// Class Definition ////////////////////////////////////////////////////////////
class Blinds {
public:
    Blinds(uint32_t pin_a, uint32_t pin_b, uint32_t pin_c, uint32_t pin_enable,
        uint32_t pin_limit, uint32_t num_pole_pairs, float motion_length,
        float motion_timeout, float motor_speed_open, float motor_speed_close); 
    
    bool blindsAreClosed() const;
    void close() const;
    void open() const;
    void closeHalfway() const;  // Close by half of full travel; stops at limit if hit sooner
    void toggle() const;
    
    // Mutators for configurable parameters
    void setMotorSpeedOpen(float speed_open);
    void setMotorSpeedClose(float speed_close);
    void setMotionLength(float length);
    
    // Getters (optional, for verification)
    float getMotorSpeedOpen() const { return motor_speed_open; }
    float getMotorSpeedClose() const { return motor_speed_close; }
    float getMotionLength() const { return motion_length; }
    float getMotionTimeout() const { return motion_timeout; }

private:
    const uint32_t pin_motor_phase_a;
    const uint32_t pin_motor_phase_b;
    const uint32_t pin_motor_phase_c;
    const uint32_t pin_enable;
    const uint32_t pin_limit;
    const uint32_t num_pole_pairs;
    float          motor_speed_open;   // RPM when opening
    float          motor_speed_close;  // RPM when closing
    float          motion_length;
    float          motion_timeout;

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
    mutable float increment_max_open;    // Maximum increment for opening
    mutable float increment_max_close;   // Maximum increment for closing
    const float increment_increment{RAMP_RATE / 60.0F / TICK_FREQ / TICK_FREQ};

    void motorMove(const bool close, float max_distance = -1.0F) const;  // max_distance < 0 means use motion_length
    void motorDisable() const;
    void motorEnable() const;
    void motorPosition(float electricalAngle, float amplitude) const;
    void recalculateIncrementMax();  // Recalculate derived values for both speeds
    void recalculateTimeout();       // Recalculate timeout based on motion params
};

// End of file /////////////////////////////////////////////////////////////////