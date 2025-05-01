#ifndef PWM_RTC_H
#define PWM_RTC_H

#include <ctime>
#include <cstdint>
#include <vector>
#include <functional>
#include "hardware/pwm.h"
#include "hardware/irq.h"

#define PWM_SLICE_NUM 0

class PWM_RTC {
public:
    // Singleton access
    static PWM_RTC& getInstance();

    // Set the start time (epoch) to a specific date and time
    void setStartTime(int year, int month, int day, int hour, int minute, int second);

    // Accessor method to get current time based on internal tick count
    std::tm getCurrentTime() const;

    // Method to register a callback to be called every second
    void registerCallback(std::function<void(const std::tm&)> callback);

private:
    PWM_RTC();  // Private constructor for Singleton
    static void pwmClockHandler();  // ISR for PWM interrupt
    static bool is_pwm_initialized; // Flag to ensure PWM is initialized only once

    // Initialize PWM, called only once
    void initializePWM();

    static volatile uint32_t tick_count;  // Track the tick count (incremented in the ISR)
    std::time_t start_time;  // Epoch time at initialization

    // Store callbacks to be executed every second
    std::vector<std::function<void(const std::tm&)>> callbacks;
};

#endif // PWM_RTC_H
