// blinds.cpp //////////////////////////////////////////////////////////////////
#include "blinds.h"
#include "fast_trig.h"

// Public //////////////////////////////////////////////////////////////////////
Blinds::Blinds(uint32_t pin_a, uint32_t pin_b, uint32_t pin_c, uint32_t pin_enable,
    uint32_t pin_limit, uint32_t num_pole_pairs, float motion_length,
    float motion_timeout, float motor_speed_max) 
    : pin_motor_phase_a(pin_a), pin_motor_phase_b(pin_b), pin_motor_phase_c(pin_c),
      pin_enable(pin_enable), pin_limit(pin_limit),
      num_pole_pairs(num_pole_pairs), motion_length(motion_length),
      motion_timeout(motion_timeout),
      motor_speed_max(motor_speed_max),
      increment_max(motor_speed_max / 60.0F / TICK_FREQ) {
    Serial.println("Blinds constructor started");
    
    pinMode(pin_enable, OUTPUT);
    digitalWrite(pin_enable, LOW);
    pinMode(pin_limit, INPUT_PULLUP);
    
    ledcSetup(PHASE_A_PWM_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION); 
    ledcAttachPin(pin_motor_phase_a, PHASE_A_PWM_CHANNEL);
    ledcSetup(PHASE_B_PWM_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION); 
    ledcAttachPin(pin_motor_phase_b, PHASE_B_PWM_CHANNEL);
    ledcSetup(PHASE_C_PWM_CHANNEL, PWM_FREQUENCY, PWM_RESOLUTION); 
    ledcAttachPin(pin_motor_phase_c, PHASE_C_PWM_CHANNEL);
    
    motorDisable(); 
    Serial.println("Blinds constructor completed successfully");
}

bool Blinds::blindsAreClosed() const {
    return !digitalRead(pin_limit);
}

void Blinds::close() const {
    motorMove(true);
}

void Blinds::open() const {
    motorMove(false);
}

void Blinds::toggle() const {
    if (blindsAreClosed()) {
        open();
    } else {
        close();
    }
}

void Blinds::setMotorSpeedMax(float speed_max) {
    motor_speed_max = speed_max;
    recalculateIncrementMax();
    recalculateTimeout();
    Serial.print("Motor speed max updated to: ");
    Serial.println(motor_speed_max);
}

void Blinds::setMotionLength(float length) {
    motion_length = length;
    recalculateTimeout();
    Serial.print("Motion length updated to: ");
    Serial.println(motion_length);
}

// Private /////////////////////////////////////////////////////////////////////

void Blinds::recalculateIncrementMax() {
    increment_max = motor_speed_max / 60.0F / TICK_FREQ;
}

void Blinds::recalculateTimeout() {
    // Calculate timeout based on motion parameters
    // Time = (motion_length turns) / (motor_speed_max RPM / 60 sec/min)
    // Add 10% safety margin
    motion_timeout = (motion_length / (motor_speed_max / 60.0F)) * 1.1F;
    Serial.print("Motion timeout recalculated to: ");
    Serial.print(motion_timeout);
    Serial.println(" seconds");
}

void Blinds::motorMove(const bool close) const {    
    if (!close || !blindsAreClosed()) {
        Serial.println(close ? "Closing blinds ..." : "Opening blinds ...");
        
        float position{0.0F};
        float increment{0.0F};

        motorPosition(0.0F, MOTOR_AMPLITUDE_MAX);
        motorEnable();
        delay(10U);

        const uint32_t timestamp_start{micros()};
        const uint32_t timestamp_timeout{timestamp_start + (uint32_t)(motion_timeout * 1000000U)};
        const uint32_t tick_period{1000000U / TICK_FREQ};
        uint32_t end_tick{micros() + tick_period};

        while ((position < motion_length) && (micros() < timestamp_timeout) && (close ? !blindsAreClosed() : true)) {
            increment = ((increment + increment_increment) > increment_max) ? (increment_max) : (increment + increment_increment);
            position += increment;
            const float velocity_scaled_amplitude{(MOTOR_AMPLITUDE_MIN + (MOTOR_AMPLITUDE_MAX - MOTOR_AMPLITUDE_MIN) * (increment / increment_max))};
            motorPosition((close ? -position : position), velocity_scaled_amplitude);
            
            while(micros() < end_tick) {}
            end_tick += tick_period;
        }
        motorDisable();
    }

    Serial.println(close ? "Blinds are closed" : "Blinds are open");
}

void Blinds::motorPosition(const float motor_position, const float amplitude) const {
    const float electrical_position{motor_position * 2.0F * (float)PI * num_pole_pairs};
    
    const float phaseA_norm = fast_sin_interp(electrical_position);
    const float phaseB_norm = fast_sin_interp(electrical_position + (2.0F * PI / 3.0F));
    const float phaseC_norm = fast_sin_interp(electrical_position - (2.0F * PI / 3.0F));
    
    const int phaseA_pwm = constrain((int)(duty_max / 2.0F + amplitude * duty_max / 2.0F * phaseA_norm), 0, duty_max);
    const int phaseB_pwm = constrain((int)(duty_max / 2.0F + amplitude * duty_max / 2.0F * phaseB_norm), 0, duty_max);
    const int phaseC_pwm = constrain((int)(duty_max / 2.0F + amplitude * duty_max / 2.0F * phaseC_norm), 0, duty_max);
    
    ledcWrite(PHASE_A_PWM_CHANNEL, phaseA_pwm);
    ledcWrite(PHASE_B_PWM_CHANNEL, phaseB_pwm);
    ledcWrite(PHASE_C_PWM_CHANNEL, phaseC_pwm);
}

void Blinds::motorDisable() const {
    digitalWrite(pin_enable, LOW);
}

void Blinds::motorEnable() const {
    digitalWrite(pin_enable, HIGH);
}

// End of file /////////////////////////////////////////////////////////////////