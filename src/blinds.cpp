// blinds.cpp //////////////////////////////////////////////////////////////////
#include "blinds.h"

// Constants ///////////////////////////////////////////////////////////////////
constexpr uint32_t PWM_FREQ_HZ{20000U};
constexpr uint8_t PWM_RES_BITS{8U};
constexpr uint8_t PWM_CHANNEL{0U};
constexpr uint32_t MOTION_TICK_MS{1U};
constexpr uint32_t STUCK_TIMEOUT_MS{2000U};
constexpr uint8_t LIMIT_DEBOUNCE_COUNT{5U};
constexpr uint32_t SLOW_APPROACH_DISTANCE{150U}; // encoder ticks
constexpr float SLOW_APPROACH_SPEED{0.25F};

// Global variables ////////////////////////////////////////////////////////////
// Encoder tracking
volatile uint32_t g_tick_count = 0;
volatile int32_t g_position = 0;
volatile Blinds::MotorDirection g_current_direction = Blinds::MotorDirection::OPEN;

// Public //////////////////////////////////////////////////////////////////////
Blinds::Blinds(uint8_t dirPin, uint8_t spdPin, uint8_t encPin,
               uint8_t limPin, uint8_t brakePin, uint32_t travelLength,
               uint32_t timeoutMs, float openSpeed, float closeSpeed, uint32_t rampDurationMs)
    : dir_pin(dirPin), spd_pin(spdPin), enc_pin(encPin), lim_pin(limPin),
      brake_pin(brakePin), travel_length(travelLength),
      timeout_us(timeoutMs * 1000U),
      open_speed_pwm(static_cast<uint32_t>((1.0F - openSpeed) * 255.0F)),
      close_speed_pwm(static_cast<uint32_t>((1.0F - closeSpeed) * 255.0F)),
      ramp_duration_ms(rampDurationMs) {
    
    Serial.println("Blinds constructor started");
    
    // Initialize position tracking
    g_position = 0;
    g_current_direction = MotorDirection::OPEN;
    Serial.println("Position initialized to 0");
    
    // Configure GPIOs
    Serial.println("Configuring GPIO pins...");
    pinMode(dir_pin, OUTPUT);
    pinMode(spd_pin, OUTPUT);
    pinMode(enc_pin, INPUT_PULLUP);
    pinMode(lim_pin, INPUT_PULLUP);
    pinMode(brake_pin, OUTPUT);
    Serial.println("GPIO pins configured");
    
    // PWM Setup
    Serial.println("Setting up PWM...");
    ledcSetup(PWM_CHANNEL, PWM_FREQ_HZ, PWM_RES_BITS);
    ledcAttachPin(spd_pin, PWM_CHANNEL);
    Serial.println("PWM setup complete");
    
    // Initialize motor stopped with brake engaged
    Serial.println("Initializing motor state...");
    motorStop();
    Serial.println("Motor initialized");
    
    // Set up interrupt handling
    Serial.println("Setting up encoder interrupt...");
    attachInterrupt(digitalPinToInterrupt(enc_pin), encoderInterrupt, RISING);
    Serial.println("Encoder interrupt attached");
    
    Serial.println("Blinds constructor completed successfully");
}

bool Blinds::blindsAreClosed() const {
    // Software debounce: require N consecutive readings of PRESSED
    uint8_t pressed_count = 0;
    
    for (uint8_t i = 0; i < LIMIT_DEBOUNCE_COUNT; i++) {
        if (digitalRead(lim_pin) == LimitSwitchState::PRESSED) {
            pressed_count++;
        }
        if (i < (LIMIT_DEBOUNCE_COUNT - 1)) { // Don't delay after last reading
            delay(1); // 1ms between readings
        }
    }
    
    // If limit switch is pressed (debounced), zero the position
    const bool is_pressed = (pressed_count == LIMIT_DEBOUNCE_COUNT);
    if (is_pressed) {
        g_position = 0;
    }
    
    return is_pressed;
}

void Blinds::close() const {
    if (!blindsAreClosed()) {
        motorMove(MotorDirection::CLOSE, close_speed_pwm, [](const Blinds* b) { 
            return b->blindsAreClosed(); 
        });
    }
}

void Blinds::open() const {
    if (blindsAreClosed()) {
        motorMove(MotorDirection::OPEN, open_speed_pwm, [](const Blinds* b) { 
            return g_tick_count >= b->travel_length; 
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
void Blinds::brakeDisengage() const {
    digitalWrite(brake_pin, BrakeState::DISENGAGED);
}

void Blinds::brakeEngage() const {
    digitalWrite(brake_pin, BrakeState::ENGAGED);
}

void Blinds::motorMove(MotorDirection direction, uint32_t target_speed_pwm, bool (*exitCondition)(const Blinds*)) const {
    const uint64_t start_us{micros()};
    const uint32_t start_ms{millis()};
    uint32_t last_tick_time_ms{start_ms};
    uint32_t last_tick_count{0U};
    uint32_t last_debug_time{start_ms}; // For debug output
    g_tick_count = 0U;
    
    // Set direction for position tracking
    g_current_direction = direction;
    
    digitalWrite(dir_pin, direction);
    brakeDisengage();
    delay(10);
    
    Serial.print("Motor starting: direction=");
    Serial.print(direction == MotorDirection::OPEN ? "OPEN" : "CLOSE");
    Serial.print(" target_pwm=");
    Serial.print(target_speed_pwm);
    Serial.print(" speed=");
    Serial.print((255.0F - static_cast<float>(target_speed_pwm)) / 255.0F, 3);
    Serial.print(" position=");
    Serial.println(g_position);
    
    while (!exitCondition(this) && ((micros() - start_us) < timeout_us)) {
        const uint32_t elapsed_ms{millis() - start_ms};
        const uint32_t current_ms{millis()};
        
        // Check for encoder activity (blockage detection)
        if (g_tick_count != last_tick_count) {
            last_tick_time_ms = current_ms;
            last_tick_count = g_tick_count;
        } else if ((current_ms - last_tick_time_ms) > STUCK_TIMEOUT_MS) {
            // No encoder ticks for too long - possible blockage
            Serial.println("Motor: STUCK detected - stopping");
            break;
        }
        
        // Determine target speed based on position
        const bool near_limit = (direction == MotorDirection::CLOSE && g_position <= static_cast<int32_t>(SLOW_APPROACH_DISTANCE));
        const uint32_t effective_target = near_limit ? static_cast<uint32_t>((1.0F - SLOW_APPROACH_SPEED) * 255.0F) : target_speed_pwm;
        
        uint32_t current_duty;
        if (elapsed_ms < ramp_duration_ms) {
            // Ramping phase: interpolate from 255 (stopped) to effective_target
            const float ramp_progress{static_cast<float>(elapsed_ms) / static_cast<float>(ramp_duration_ms)};
            current_duty = static_cast<uint32_t>(255.0F - ramp_progress * (255.0F - static_cast<float>(effective_target)));
        } else {
            // Full speed phase - use the effective target
            current_duty = effective_target;
        }
        
        // Debug output every 250ms
        if (current_ms - last_debug_time >= 250) {
            Serial.print("Motor: ticks=");
            Serial.print(g_tick_count);
            Serial.print(" position=");
            Serial.print(g_position);
            Serial.print(" elapsed=");
            Serial.print(elapsed_ms);
            Serial.print("ms");
            Serial.print(" pwm=");
            Serial.print(current_duty);
            Serial.print(" speed=");
            Serial.print((255.0F - static_cast<float>(current_duty)) / 255.0F, 3);
            Serial.print(" limit=");
            Serial.println(blindsAreClosed() ? "PRESSED" : "OPEN");
            last_debug_time = current_ms;
        }
        
        ledcWrite(PWM_CHANNEL, current_duty);
        delay(MOTION_TICK_MS);
    }
    
    Serial.print("Motor stopped: final_ticks=");
    Serial.print(g_tick_count);
    Serial.print(" final_position=");
    Serial.print(g_position);
    Serial.print(" reason=");
    if (exitCondition(this)) {
        Serial.println("END_OF_TRAVEL");
    } else if ((micros() - start_us) >= timeout_us) {
        Serial.println("TIMEOUT");
    } else {
        Serial.println("STUCK");
    }
    
    motorStop();
}

void Blinds::motorStop() const {
    ledcWrite(PWM_CHANNEL, 255U); // 255 = stopped (inverted logic)
    brakeEngage();
}

// Global interrupt handlers ///////////////////////////////////////////////////
void IRAM_ATTR encoderInterrupt() {
    g_tick_count++;
    
    // Update position based on direction
    if (g_current_direction == Blinds::MotorDirection::OPEN) {
        g_position++;
    } else {
        g_position--;
    }
}

// End of file /////////////////////////////////////////////////////////////////
