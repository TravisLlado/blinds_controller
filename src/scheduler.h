// scheduler.h //////////////////////////////////////////////////////////////////
#pragma once
#include <Arduino.h>
#include "TimeSync.h"

// Function pointer types for actions
typedef void (*ActionCallback)();

// Scheduler class to handle all time-based automation
class Scheduler {
public:
    Scheduler(const char* wifiSSID, const char* wifiPassword, 
              float latitude, float longitude, int32_t timezoneOffset,
              int32_t sunriseOffset, int32_t sunsetOffset,
              ActionCallback sunriseAction, ActionCallback sunsetAction);
    
    void begin();
    void update();
    void printStatus();
    
    // Manual trigger functions (for testing)
    void triggerSunriseAction();
    void triggerSunsetAction();

private:
    // TimeSync instance
    TimeSync timeSync;
    
    // Action callbacks
    ActionCallback sunrise_action;
    ActionCallback sunset_action;
    
    // Timing offsets
    const int32_t sunrise_offset;
    const int32_t sunset_offset;
    
    // Time data
    int32_t current_time{0};
    int32_t sunrise{0};
    int32_t sunset{0};
    
    // Action tracking flags
    bool sunrise_action_done{false};
    bool sunset_action_done{false};
    
    // Update tracking
    bool updated_today{false};
    uint8_t failed_update_attempts{0U};
    uint32_t last_update_attempt{0};
    uint32_t last_print_time{0};
    
    // Private methods
    void updateAllTimes();
    void checkTimeActions();
    bool isUpdateTime();
    void formatTimeOfDay(uint32_t timeOfDay, char* buffer);
};

// End of file /////////////////////////////////////////////////////////////////
