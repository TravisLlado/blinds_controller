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
    
    enum LimitSwitchState {
        PRESSED = LOW,
        RELEASED = HIGH
    };
    
    // Constructor uses BLDC phase pins, enable pin, limit switch, pole pairs, and cycles for distance
    Blinds(uint8_t phaseAPin, uint8_t phaseBPin, uint8_t phaseCPin,
           uint8_t enablePin, uint8_t limPin,
           float polePairs, float cyclesPerTravel, uint32_t timeoutMs,
           float openSpeedRpm, float closeSpeedRpm, uint32_t rampDurationMs);
    
    bool blindsAreClosed() const;
    void close() const;
    void open() const;
    void toggle() const;

private:
    // --- BLDC Motor Pins and Parameters ---
    const uint8_t phase_a_pin;
    const uint8_t phase_b_pin;
    const uint8_t phase_c_pin;
    const uint8_t enable_pin;
    const float motor_pole_pairs;

    // --- Blinds Pins and Parameters ---
    const uint8_t lim_pin;
    const float travel_cycles; // Total electrical cycles to fully open the blinds
    const uint32_t timeout_us;
    
    // Motor speeds in RPM
    const float open_speed_rpm;
    const float close_speed_rpm;
    const uint32_t ramp_duration_ms; 

    // --- BLDC Control State ---
    // Position tracking uses integrated electrical angle and cycles
    mutable float current_electrical_angle = 0.0f; // Stored for smooth starting of next move
    mutable float current_cycle_count = 0.0f;     // Tracks position in electrical cycles (0.0 = Closed)

    // Motor control methods
    void motorMove(MotorDirection direction, float target_speed_rpm, bool (*exitCondition)(const Blinds*)) const;
    void motorStop() const;
};

// End of file /////////////////////////////////////////////////////////////////