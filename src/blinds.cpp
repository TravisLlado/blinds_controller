// blinds.cpp //////////////////////////////////////////////////////////////////
#include <esp_task_wdt.h>
#include "blinds.h"
#include "fast_trig.h"

// Public //////////////////////////////////////////////////////////////////////
Blinds::Blinds(uint32_t pin_a, uint32_t pin_b, uint32_t pin_c, uint32_t pin_enable,
    uint32_t pin_limit, uint32_t num_pole_pairs, float motion_length,
    float motion_timeout, float motor_speed_open, float motor_speed_close) 
    : pin_motor_phase_a(pin_a), pin_motor_phase_b(pin_b), pin_motor_phase_c(pin_c),
      pin_enable(pin_enable), pin_limit(pin_limit),
      num_pole_pairs(num_pole_pairs), motion_length(motion_length),
      motion_timeout(motion_timeout),
      motor_speed_open(motor_speed_open),
      motor_speed_close(motor_speed_close),
      increment_max_open(motor_speed_open / 60.0F / TICK_FREQ),
      increment_max_close(motor_speed_close / 60.0F / TICK_FREQ) {
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

void Blinds::closeHalfway() const {
    motorMove(true, motion_length * 0.5F);
}

void Blinds::toggle() const {
    if (blindsAreClosed()) {
        open();
    } else {
        close();
    }
}

void Blinds::setMotorSpeedOpen(float speed_open) {
    motor_speed_open = speed_open;
    recalculateIncrementMax();
    recalculateTimeout();
    Serial.print("Motor speed (open) updated to: ");
    Serial.println(motor_speed_open);
}

void Blinds::setMotorSpeedClose(float speed_close) {
    motor_speed_close = speed_close;
    recalculateIncrementMax();
    recalculateTimeout();
    Serial.print("Motor speed (close) updated to: ");
    Serial.println(motor_speed_close);
}

void Blinds::setMotionLength(float length) {
    motion_length = length;
    recalculateTimeout();
    Serial.print("Motion length updated to: ");
    Serial.println(motion_length);
}

// Private /////////////////////////////////////////////////////////////////////

void Blinds::recalculateIncrementMax() {
    increment_max_open = motor_speed_open / 60.0F / TICK_FREQ;
    increment_max_close = motor_speed_close / 60.0F / TICK_FREQ;
}

void Blinds::recalculateTimeout() {
    // Calculate timeout based on motion parameters
    // Use the slower speed (opening) for timeout calculation to be conservative
    // Time = (motion_length turns) / (motor_speed_open RPM / 60 sec/min)
    // Add 10% safety margin
    motion_timeout = (motion_length / (motor_speed_open / 60.0F)) * 1.1F;
    Serial.print("Motion timeout recalculated to: ");
    Serial.print(motion_timeout);
    Serial.println(" seconds");
}

void Blinds::motorMove(const bool close, float max_distance) const {
    const float target = (max_distance >= 0.0F) ? max_distance : motion_length;
    if (!close || !blindsAreClosed()) {
        Serial.print(close ? "Closing blinds ..." : "Opening blinds ...");
        Serial.printf(" target=%.1f motion_length=%.1f timeout=%.1fs\n", target, motion_length, motion_timeout);

        float position{0.0F};
        float increment{0.0F};

        // Select the appropriate max increment based on direction
        const float increment_max = close ? increment_max_close : increment_max_open;

        motorPosition(0.0F, MOTOR_AMPLITUDE_MAX);
        motorEnable();
        delay(10U);

        const uint32_t timestamp_start{micros()};
        const uint32_t timestamp_timeout{timestamp_start + (uint32_t)(motion_timeout * 1000000U)};
        const uint32_t tick_period{1000000U / TICK_FREQ};
        uint32_t end_tick{micros() + tick_period};
        const uint32_t progress_interval_us{1000000U};  // 1 second
        uint32_t next_progress_us{timestamp_start + progress_interval_us};

        while ((position < target) && (micros() < timestamp_timeout) && (close ? !blindsAreClosed() : true)) {
            esp_task_wdt_reset();
            increment = ((increment + increment_increment) > increment_max) ? (increment_max) : (increment + increment_increment);
            position += increment;
            const float velocity_scaled_amplitude{(MOTOR_AMPLITUDE_MIN + (MOTOR_AMPLITUDE_MAX - MOTOR_AMPLITUDE_MIN) * (increment / increment_max))};
            motorPosition((close ? -position : position), velocity_scaled_amplitude);

            const uint32_t now_us = micros();
            if (now_us >= next_progress_us) {
                Serial.printf("Move progress: position=%.1f target=%.1f (%.0f%%)\n",
                              position, target, (position / target) * 100.0F);
                next_progress_us += progress_interval_us;
            }

            while(micros() < end_tick) {}
            end_tick += tick_period;
        }
        motorDisable();

        Serial.printf("Move done: position=%.1f target=%.1f\n", position, target);
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