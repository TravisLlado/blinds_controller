// blinds.cpp //////////////////////////////////////////////////////////////////
#include "blinds.h"

// Constants ///////////////////////////////////////////////////////////////////
constexpr uint32_t MOTION_TICK_MS{1U}; // Control loop delay
constexpr uint8_t LIMIT_DEBOUNCE_COUNT{5U};
constexpr float SLOW_APPROACH_CYCLES{10.0F}; // Electrical cycles before target to start ramp-down
constexpr float SLOW_APPROACH_SPEED_RPM_FACTOR{0.25F}; // Slow approach speed as a factor of max speed

// Global variables ////////////////////////////////////////////////////////////
volatile Blinds::MotorDirection g_current_direction = Blinds::MotorDirection::OPEN;

// BLDC PWM Channel Map
constexpr uint8_t PHASE_A_PWM_CHANNEL{0};
constexpr uint8_t PHASE_B_PWM_CHANNEL{1};
constexpr uint8_t PHASE_C_PWM_CHANNEL{2};

// Public //////////////////////////////////////////////////////////////////////
Blinds::Blinds(uint8_t phaseAPin, uint8_t phaseBPin, uint8_t phaseCPin,
               uint8_t enablePin, uint8_t limPin,
               float polePairs, float cyclesPerTravel, uint32_t timeoutMs,
               float openSpeedRpm, float closeSpeedRpm, uint32_t rampDurationMs)
    : phase_a_pin(phaseAPin), phase_b_pin(phaseBPin), phase_c_pin(phaseCPin),
      enable_pin(enablePin), lim_pin(limPin),
      motor_pole_pairs(polePairs), travel_cycles(cyclesPerTravel),
      timeout_us(timeoutMs * 1000U),
      open_speed_rpm(openSpeedRpm),
      close_speed_rpm(closeSpeedRpm),
      ramp_duration_ms(rampDurationMs) {
    
    Serial.println("Blinds constructor started (Open-Loop BLDC version)");
    
    // Initialize position tracking to 0 (closed)
    current_electrical_angle = 0.0f;
    current_cycle_count = 0.0f;
    g_current_direction = MotorDirection::OPEN;
    Serial.println("Position initialized to 0 cycles");
    
    // Configure GPIOs
    Serial.println("Configuring GPIO pins...");
    pinMode(enable_pin, OUTPUT);
    digitalWrite(enable_pin, LOW); // Disable motor controller initially
    pinMode(lim_pin, INPUT_PULLUP);
    Serial.println("GPIO pins configured");
    
    // PWM Setup for 3 BLDC Phases
    Serial.println("Setting up 3-Phase PWM...");
    ledcSetup(PHASE_A_PWM_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION); 
    ledcAttachPin(phase_a_pin, PHASE_A_PWM_CHANNEL);
    ledcSetup(PHASE_B_PWM_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION); 
    ledcAttachPin(phase_b_pin, PHASE_B_PWM_CHANNEL);
    ledcSetup(PHASE_C_PWM_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION); 
    ledcAttachPin(phase_c_pin, PHASE_C_PWM_CHANNEL);
    Serial.println("3-Phase PWM setup complete");
    
    // Initialize motor stopped/disabled
    Serial.println("Initializing motor state...");
    motorStop(); 
    Serial.println("Blinds constructor completed successfully");
}

bool Blinds::blindsAreClosed() const {
    // Software debounce
    uint8_t pressed_count = 0;
    
    for (uint8_t i = 0; i < LIMIT_DEBOUNCE_COUNT; i++) {
        if (digitalRead(lim_pin) == LimitSwitchState::PRESSED) {
            pressed_count++;
        }
        if (i < (LIMIT_DEBOUNCE_COUNT - 1)) {
            delay(1); // 1ms between readings
        }
    }
    
    // If limit switch is pressed (debounced), zero the position/cycle count
    const bool is_pressed = (pressed_count == LIMIT_DEBOUNCE_COUNT);
    if (is_pressed) {
        // Update mutable member variables
        const_cast<Blinds*>(this)->current_cycle_count = 0.0f;
        const_cast<Blinds*>(this)->current_electrical_angle = 0.0f;
    }
    
    return is_pressed;
}

void Blinds::close() const {
    if (!blindsAreClosed()) {
        motorMove(MotorDirection::CLOSE, close_speed_rpm, [](const Blinds* b) { 
            return b->blindsAreClosed(); 
        });
    }
}

void Blinds::open() const {
    if (blindsAreClosed()) {
        motorMove(MotorDirection::OPEN, open_speed_rpm, [this](const Blinds* b) { 
            // Exit condition is based on integrated cycles exceeding the required travel
            return this->current_cycle_count >= b->travel_cycles; 
        });
    }
}

void Blinds::toggle() const {
    if (blindsAreClosed()) {
        open();
    } else {
        close();
    }
}

// Private /////////////////////////////////////////////////////////////////////

void Blinds::motorMove(MotorDirection direction, float target_speed_rpm, bool (*exitCondition)(const Blinds*)) const {
    // References to mutable members to update state within the const method
    float& currentElectricalAngle = const_cast<Blinds*>(this)->current_electrical_angle;
    float& currentCycleCount = const_cast<Blinds*>(this)->current_cycle_count;
    
    unsigned long lastUpdateTime = millis();
    const uint64_t start_us{micros()};
    const uint32_t start_ms{millis()};
    uint32_t last_debug_time{start_ms};
    
    g_current_direction = direction;
    
    // Enable the motor controller
    digitalWrite(enable_pin, HIGH);
    delay(10);
    
    Serial.print("Motor starting: direction=");
    Serial.print(direction == MotorDirection::OPEN ? "OPEN" : "CLOSE");
    Serial.print(" target_rpm=");
    Serial.println(target_speed_rpm);
    Serial.print(" initial_cycles=");
    Serial.println(currentCycleCount);

    const float slow_approach_rpm = target_speed_rpm * SLOW_APPROACH_SPEED_RPM_FACTOR;

    // Movement loop
    while (!exitCondition(this) && ((micros() - start_us) < timeout_us)) {
        const uint32_t elapsed_ms{millis() - start_ms};
        const uint32_t current_ms{millis()};
        float deltaTime_s = (current_ms - lastUpdateTime) / 1000.0f;
        lastUpdateTime = current_ms;

        // Determine target speed based on time (ramp up) and position (slow approach)
        float desired_rpm;
        
        if (direction == MotorDirection::CLOSE) {
            // Ramp down if cycle count is low (approaching limit switch from above 0 cycles)
            const bool near_limit_cycle_check = (currentCycleCount < SLOW_APPROACH_CYCLES) && (currentCycleCount > 0.0f);
            
            if (near_limit_cycle_check) {
                desired_rpm = slow_approach_rpm;
            } else if (elapsed_ms < ramp_duration_ms) {
                // Ramping up phase
                const float ramp_progress{static_cast<float>(elapsed_ms) / static_cast<float>(ramp_duration_ms)};
                desired_rpm = ramp_progress * target_speed_rpm;
            } else {
                desired_rpm = target_speed_rpm;
            }
        } else { // MotorDirection::OPEN
            // Ramp down if cycles remaining is low (approaching open limit)
            const float cycles_remaining = travel_cycles - currentCycleCount;
            const bool near_limit_cycle_check = (cycles_remaining <= SLOW_APPROACH_CYCLES) && (cycles_remaining > 0.0f);
            
            if (near_limit_cycle_check) {
                desired_rpm = slow_approach_rpm;
            } else if (elapsed_ms < ramp_duration_ms) {
                // Ramping up phase
                const float ramp_progress{static_cast<float>(elapsed_ms) / static_cast<float>(ramp_duration_ms)};
                desired_rpm = ramp_progress * target_speed_rpm;
            } else {
                desired_rpm = target_speed_rpm;
            }
        }

        // Apply direction to RPM
        float signed_current_rpm = (direction == MotorDirection::OPEN) ? desired_rpm : -desired_rpm;
        
        // Update the total electrical angle and cycle count
        float delta_angle = (signed_current_rpm / 60.0f) * motor_pole_pairs * 2.0f * PI * deltaTime_s;
        currentElectricalAngle += delta_angle;
        currentCycleCount += delta_angle / (2.0f * PI);

        // Constrain cycle count to valid range (0 to travel_cycles)
        currentCycleCount = constrain(currentCycleCount, 0.0f, travel_cycles);
        
        // Generate and write PWM signals (Sinusoidal Commutation)
        float amplitude = (desired_rpm > 0.0f) ? MOTOR_AMPLITUDE : 0.0f;
        
        float phaseA_norm = sin(currentElectricalAngle);
        float phaseB_norm = sin(currentElectricalAngle - (2.0f * PI / 3.0f));
        float phaseC_norm = sin(currentElectricalAngle + (2.0f * PI / 3.0f));
        
        int phaseA_pwm = (int)(PWM_MAX_VALUE/2 + amplitude * PWM_MAX_VALUE/2 * phaseA_norm);
        int phaseB_pwm = (int)(PWM_MAX_VALUE/2 + amplitude * PWM_MAX_VALUE/2 * phaseB_norm);
        int phaseC_pwm = (int)(PWM_MAX_VALUE/2 + amplitude * PWM_MAX_VALUE/2 * phaseC_norm);
        
        phaseA_pwm = constrain(phaseA_pwm, 0, PWM_MAX_VALUE);
        phaseB_pwm = constrain(phaseB_pwm, 0, PWM_MAX_VALUE);
        phaseC_pwm = constrain(phaseC_pwm, 0, PWM_MAX_VALUE);
        
        ledcWrite(PHASE_A_PWM_CHANNEL, phaseA_pwm);
        ledcWrite(PHASE_B_PWM_CHANNEL, phaseB_pwm);
        ledcWrite(PHASE_C_PWM_CHANNEL, phaseC_pwm);
        
        // Debug output every 250ms
        if (current_ms - last_debug_time >= 250) {
            Serial.print("Motor: cycles=");
            Serial.print(currentCycleCount, 2);
            Serial.print("/");
            Serial.print(travel_cycles, 2);
            Serial.print(" elapsed=");
            Serial.print(elapsed_ms);
            Serial.print("ms");
            Serial.print(" target_rpm=");
            Serial.print(desired_rpm);
            Serial.print(" limit=");
            Serial.println(blindsAreClosed() ? "PRESSED" : "OPEN");
            last_debug_time = current_ms;
        }
        
        delay(MOTION_TICK_MS); // Control loop delay
    }
    
    Serial.print("Motor stopped: final_cycles=");
    Serial.print(currentCycleCount, 2);
    Serial.print(" reason=");
    if (exitCondition(this)) {
        Serial.println("END_OF_TRAVEL");
    } else if ((micros() - start_us) >= timeout_us) {
        Serial.println("TIMEOUT/STUCK");
    } else {
        Serial.println("UNKNOWN");
    }
    
    motorStop();
}

void Blinds::motorStop() const {
    // Set all phases to center PWM (PWM_MAX_VALUE/2) to stop current flow
    int center_pwm = PWM_MAX_VALUE / 2;
    ledcWrite(PHASE_A_PWM_CHANNEL, center_pwm);
    ledcWrite(PHASE_B_PWM_CHANNEL, center_pwm);
    ledcWrite(PHASE_C_PWM_CHANNEL, center_pwm);

    // Disable the motor controller
    digitalWrite(enable_pin, LOW);
}

// End of file /////////////////////////////////////////////////////////////////