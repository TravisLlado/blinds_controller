#include "pwm_rtc.h"

bool PWM_RTC::is_pwm_initialized = false;  // Static flag to ensure PWM is initialized only once
volatile uint32_t PWM_RTC::tick_count = 0;  // Static member for tick count

// Singleton instance access method
PWM_RTC& PWM_RTC::getInstance() {
    static PWM_RTC instance;
    return instance;
}

// Private constructor for PWM_RTC Singleton class
PWM_RTC::PWM_RTC() : start_time(0) {
    if (!is_pwm_initialized) {
        initializePWM();  // Initialize PWM only once
        is_pwm_initialized = true;
    }
}

// Set the start time (epoch) to a specific date and time
void PWM_RTC::setStartTime(int year, int month, int day, int hour, int minute, int second) {
    std::tm start_tm = {};
    start_tm.tm_year = year - 1900;
    start_tm.tm_mon = month - 1;
    start_tm.tm_mday = day;
    start_tm.tm_hour = hour;
    start_tm.tm_min = minute;
    start_tm.tm_sec = second;

    start_time = std::mktime(&start_tm);  // Convert to time_t
}

// Get the current time based on the internal tick count
std::tm PWM_RTC::getCurrentTime() const {
    // Incremented in the interrupt handler
    std::time_t new_time = start_time + tick_count / 8U;  // Convert tick count to seconds and add to start_time
    return *std::gmtime(&new_time);  // Convert back to tm struct (UTC)
}

// Method to register a callback to be executed every second
void PWM_RTC::registerCallback(std::function<void(const std::tm&)> callback) {
    callbacks.push_back(callback);
}

// PWM interrupt handler for clock updates
void PWM_RTC::pwmClockHandler() {
    pwm_clear_irq(PWM_SLICE_NUM);
    tick_count++;  // Increment tick count on every interrupt

    if ((tick_count % 8U) == 0U) {
        const std::tm current_time = getInstance().getCurrentTime();  // Get the current time directly

        // Execute each registered callback
        for (const auto& callback : getInstance().callbacks) {
            callback(current_time);  // Call the callback with the current time
        }
    }
}

// Initialize the PWM settings
void PWM_RTC::initializePWM() {
    pwm_config config = pwm_get_default_config();
    pwm_config_set_clkdiv(&config, 250.0F);  // Set the clock divider for the desired frequency
    pwm_config_set_wrap(&config, (62500U - 1U));  // Set wrap value for 1-second tick rate
    pwm_init(PWM_SLICE_NUM, &config, true);
    pwm_clear_irq(PWM_SLICE_NUM);
    pwm_set_irq_enabled(PWM_SLICE_NUM, true);
    irq_set_exclusive_handler(PWM_IRQ_WRAP, pwmClockHandler);
    irq_set_enabled(PWM_IRQ_WRAP, true);
}
