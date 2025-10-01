// blinds.cpp //////////////////////////////////////////////////////////////////
#include "blinds.h"

// Public //////////////////////////////////////////////////////////////////////
Blinds::Blinds(uint8_t pin_a, uint8_t pin_b, uint8_t pin_c, uint8_t pin_enable,
    uint8_t pin_limit, uint8_t num_pole_pairs, float motion_length,
    uint32_t motion_timeout, float motor_speed_max) 
    : pin_motor_phase_a(pin_a), pin_motor_phase_b(pin_b), pin_motor_phase_c(pin_c),
      pin_enable(pin_enable), pin_limit(pin_limit),
      num_pole_pairs(num_pole_pairs), motion_length(motion_length),
      motion_timeout(motion_timeout),
      motor_speed_max(motor_speed_max) {
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
    if (!blindsAreClosed()) {
        Serial.println("Closing blinds ...");
        
        float position{0.0F};
        float increment{0.0F};
        uint32_t tick_count{0U};

        motorPosition(0.0F, MOTOR_AMPLITUDE_MAX);
        motorEnable();
        delay(10U);

        while ((position < motion_length) && (tick_count < (motion_timeout / TICK_PERIOD)) && !blindsAreClosed()) {
            increment = ((increment + increment_increment) > increment_max) ? (increment_max) : (increment + increment_increment);
            position += increment;
            motorPosition(-position, MOTOR_AMPLITUDE_MAX);
            delay(TICK_PERIOD);
            tick_count++;
        }
        motorDisable();
    }

    Serial.println("Blinds are closed");
}

void Blinds::open() const {
    Serial.println("Opening blinds ...");

    float position{0.0F};
    float increment{0.0F};
    uint32_t tick_count{0U};

    motorPosition(0.0F, MOTOR_AMPLITUDE_MAX);
    motorEnable();
    delay(10U);

    while ((position < motion_length) && (tick_count < (motion_timeout / TICK_PERIOD))) {
        increment = ((increment + increment_increment) > increment_max) ? (increment_max) : (increment + increment_increment);
        position += increment;
        motorPosition(position, MOTOR_AMPLITUDE_MAX);
        delay(TICK_PERIOD);
        tick_count++;
    }
    motorDisable();

    Serial.println("Blinds are open");
}

void Blinds::toggle() const {
    if (blindsAreClosed()) {
        open();
    } else {
        close();
    }
}

// Private /////////////////////////////////////////////////////////////////////

void Blinds::motorPosition(const float motor_position, const float amplitude) const {
    const float electrical_position{motor_position * 2.0F * (float)PI * num_pole_pairs};
    
    const float phaseA_norm = sin(electrical_position);
    const float phaseB_norm = sin(electrical_position + (2.0F * PI / 3.0F));
    const float phaseC_norm = sin(electrical_position - (2.0F * PI / 3.0F));
    
    const int phaseA_pwm = constrain((int)(duty_max/2 + amplitude * duty_max/2 * phaseA_norm), 0, duty_max);
    const int phaseB_pwm = constrain((int)(duty_max/2 + amplitude * duty_max/2 * phaseB_norm), 0, duty_max);
    const int phaseC_pwm = constrain((int)(duty_max/2 + amplitude * duty_max/2 * phaseC_norm), 0, duty_max);
    
    ledcWrite(PHASE_A_PWM_CHANNEL, phaseA_pwm);
    ledcWrite(PHASE_B_PWM_CHANNEL, phaseB_pwm);
    ledcWrite(PHASE_C_PWM_CHANNEL, phaseC_pwm);
}

void Blinds::motorDisable() const {
    digitalWrite(pin_enable, MotorState::DISABLE);
}

void Blinds::motorEnable() const {
    digitalWrite(pin_enable, MotorState::ENABLE);
}

// End of file /////////////////////////////////////////////////////////////////
