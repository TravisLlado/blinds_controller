// scheduler.cpp ////////////////////////////////////////////////////////////////
#include "scheduler.h"
#include "config.h"

// Constructor /////////////////////////////////////////////////////////////////
Scheduler::Scheduler(const char* wifiSSID, const char* wifiPassword, 
                     float latitude, float longitude, int32_t timezoneOffset,
                     int32_t sunriseOffset, int32_t sunsetOffset,
                     ActionCallback sunriseAction, ActionCallback sunsetAction)
    : timeSync(wifiSSID, wifiPassword, latitude, longitude, timezoneOffset),
      sunrise_action(sunriseAction),
      sunset_action(sunsetAction),
      sunrise_offset(sunriseOffset),
      sunset_offset(sunsetOffset) {
}

// Public Methods //////////////////////////////////////////////////////////////
void Scheduler::begin() {
    Serial.println("Scheduler: Initializing...");
    updateAllTimes();
}

void Scheduler::update() {
    // Get current time from RTC and update our global current time variable
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        current_time = (timeinfo.tm_hour * HOUR) + 
                      (timeinfo.tm_min * MINUTE) + 
                      timeinfo.tm_sec * SECOND;
        
        // Check if it's time for the daily update (2 AM)
        if (isUpdateTime()) {
            updateAllTimes();
        }
        
        // Check if it's time to run sunrise/sunset actions
        checkTimeActions();
    }
    
    // Print times every 5 seconds
    if (millis() - last_print_time >= 5000U) {
        printStatus();
        last_print_time = millis();
    }
}

void Scheduler::printStatus() {
    char currentStr[9], sunriseStr[9], sunsetStr[9];
    char sunriseActionStr[9], sunsetActionStr[9];
    
    formatTimeOfDay(current_time, currentStr);
    formatTimeOfDay(sunrise, sunriseStr);
    formatTimeOfDay(sunset, sunsetStr);
    
    // Format action times
    int32_t sunriseActionTime{sunrise + sunrise_offset};
    int32_t sunsetActionTime{sunset - sunset_offset};
    formatTimeOfDay(sunriseActionTime, sunriseActionStr);
    formatTimeOfDay(sunsetActionTime, sunsetActionStr);
    
    // Check if it's daytime
    const bool isDaytime{(current_time >= sunrise && current_time < sunset)};
    
    Serial.print("Current: ");
    Serial.print(currentStr);
    Serial.print(" | Sunrise: ");
    Serial.print(sunriseStr);
    Serial.print(" | Sunset: ");
    Serial.print(sunsetStr);
    Serial.print(" | It's ");
    Serial.println(isDaytime ? "daytime" : "nighttime");
}

void Scheduler::triggerSunriseAction() {
    if (sunrise_action) {
        char timeStr[9];
        formatTimeOfDay(current_time, timeStr);
        Serial.print("MANUAL SUNRISE ACTION at ");
        Serial.println(timeStr);
        sunrise_action();
    }
}

void Scheduler::triggerSunsetAction() {
    if (sunset_action) {
        char timeStr[9];
        formatTimeOfDay(current_time, timeStr);
        Serial.print("MANUAL SUNSET ACTION at ");
        Serial.println(timeStr);
        sunset_action();
    }
}

// Private Methods /////////////////////////////////////////////////////////////
void Scheduler::checkTimeActions() {
    // Only proceed if we have valid sunrise/sunset data
    if (sunrise == 0 || sunset == 0 || current_time == 0) {
        Serial.println("WARNING: Cannot check time actions - invalid time data");
        return;
    }
    
    // Reset flags at midnight
    if (current_time < MINUTE) { // Within the first minute of the day
        sunrise_action_done = false;
        sunset_action_done = false;
    }
    
    // Check for sunrise action (sunrise + offset)
    int32_t sunriseActionTime{sunrise + sunrise_offset};
    
    // Validate the calculated time
    if (sunriseActionTime < 0) sunriseActionTime = 0;
    if (sunriseActionTime >= DAY) sunriseActionTime = DAY - 1;
    
    if (!sunrise_action_done && current_time >= sunriseActionTime) {
        if (sunrise_action) {
            char timeStr[9];
            formatTimeOfDay(current_time, timeStr);
            Serial.print("SUNRISE ACTION TRIGGERED at ");
            Serial.println(timeStr);
            sunrise_action();
        }
        sunrise_action_done = true;
    }
    
    // Check for sunset action (sunset - offset)
    int32_t sunsetActionTime{sunset - sunset_offset};
    
    // Validate the calculated time
    if (sunsetActionTime < 0) sunsetActionTime = 0;
    if (sunsetActionTime >= DAY) sunsetActionTime = DAY - 1;
    
    if (!sunset_action_done && current_time >= sunsetActionTime) {
        if (sunset_action) {
            char timeStr[9];
            formatTimeOfDay(current_time, timeStr);
            Serial.print("SUNSET ACTION TRIGGERED at ");
            Serial.println(timeStr);
            sunset_action();
        }
        sunset_action_done = true;
    }
}

bool Scheduler::isUpdateTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        return false;
    }
    
    // Get current time in milliseconds
    const uint32_t currentMillis{millis()};
    
    // Check for regular update time (2:00-2:05 AM window)
    const bool isRegularUpdateTime{(timeinfo.tm_hour == UPDATE_HOUR) && 
                                   (timeinfo.tm_min < (UPDATE_WINDOW / MINUTE))};
    
    // Check for retry after failure based on number of previous failures
    bool isRetryTime{false};
    if (failed_update_attempts > 0U) {
        uint32_t retryInterval;
        
        if (failed_update_attempts < MAX_RETRY_COUNT_SHORT) {
            // Retry every 15 minutes for the first several attempts
            retryInterval = RETRY_INTERVAL_SHORT;
        } else {
            // After MAX_RETRY_COUNT_SHORT failures, retry at longer intervals
            retryInterval = RETRY_INTERVAL_LONG;
        }
        
        isRetryTime = (currentMillis - last_update_attempt >= retryInterval);
    }
    
    // If we're past the update window, reset the daily flag for tomorrow
    if (timeinfo.tm_hour > UPDATE_HOUR) {
        updated_today = false;
    }
    
    // Determine if we should update
    if ((isRegularUpdateTime && !updated_today) || isRetryTime) {
        // If this is the regular update, mark as updated for today
        if (isRegularUpdateTime) {
            updated_today = true;
        }
        return true;
    }
    
    return false;
}

void Scheduler::updateAllTimes() {
    Serial.println("Scheduler: Attempting to fetch fresh time data...");
    last_update_attempt = millis();
    
    // This function connects to WiFi, gets all time data, sets the RTC, then disconnects
    DaylightTimes times = timeSync.fetchTimeData();
    
    // Check if update was successful
    if (times.current == 0 && times.sunrise == 0 && times.sunset == 0) {
        // Update failed
        failed_update_attempts++;
        Serial.print("Scheduler: Time data update FAILED! Attempt #");
        Serial.println(failed_update_attempts);
        
        // If this is our first time running and we have no time data at all,
        // set reasonable default values to prevent crashes
        if (current_time == 0) {
            // Read from RTC if possible
            struct tm timeinfo;
            if (getLocalTime(&timeinfo)) {
                current_time = (timeinfo.tm_hour * HOUR) + 
                              (timeinfo.tm_min * MINUTE) + 
                              timeinfo.tm_sec;
                Serial.print("Scheduler: Using RTC time as fallback: ");
                Serial.println(current_time);
            } else {
                // No RTC time available, use a default
                current_time = 8 * HOUR; // Default to 8:00 AM
                Serial.println("Scheduler: Using default time: 08:00:00");
            }
        }
        
        // Set default sunrise/sunset if we don't have any
        if (sunrise == 0) {
            sunrise = 6 * HOUR + 30 * MINUTE; // Default to 6:30 AM
            Serial.println("Scheduler: Using default sunrise: 06:30:00");
        }
        
        if (sunset == 0) {
            sunset = 19 * HOUR + 30 * MINUTE; // Default to 7:30 PM
            Serial.println("Scheduler: Using default sunset: 19:30:00");
        }
        
        // If we've failed multiple times, schedule another attempt soon
        if (failed_update_attempts < MAX_RETRY_COUNT_SHORT) {
            // Try again in 15 minutes
            Serial.println("Scheduler: Will retry in 15 minutes");
        } else {
            // After MAX_RETRY_COUNT_SHORT failed attempts, try again in 6 hours
            Serial.println("Scheduler: Will retry in 6 hours");
            // Reset the counter to avoid integer overflow during long-term operation
            if (failed_update_attempts >= 10U) {
                failed_update_attempts = 5U;
            }
        }
        return;
    }
    
    // Update was successful - store the fetched times
    current_time = times.current;
    sunrise = times.sunrise;
    sunset = times.sunset;
    
    // Make sure none of the time values are zero, which could cause issues
    if (current_time == 0 || sunrise == 0 || sunset == 0) {
        Serial.println("Scheduler: WARNING: Received some zero time values despite 'successful' update");
        
        // Fix any zero values with defaults
        if (current_time == 0) {
            struct tm timeinfo;
            if (getLocalTime(&timeinfo)) {
                current_time = (timeinfo.tm_hour * HOUR) + 
                              (timeinfo.tm_min * MINUTE) + 
                              timeinfo.tm_sec;
            } else {
                current_time = 8 * HOUR; // Default to 8:00 AM
            }
        }
        
        if (sunrise == 0) sunrise = 6 * HOUR + 30 * MINUTE; // Default to 6:30 AM
        if (sunset == 0) sunset = 19 * HOUR + 30 * MINUTE; // Default to 7:30 PM
    }
    
    // Reset the failed attempts counter
    failed_update_attempts = 0U;
    
    // When we update, determine if we've already passed the action times for today
    int32_t sunriseActionTime{sunrise + sunrise_offset};
    int32_t sunsetActionTime{sunset - sunset_offset};
    
    // Validate calculated times to prevent issues
    if (sunriseActionTime < 0) sunriseActionTime = 0;
    if (sunriseActionTime > DAY) sunriseActionTime = DAY - 1;
    if (sunsetActionTime < 0) sunsetActionTime = 0;
    if (sunsetActionTime > DAY) sunsetActionTime = DAY - 1;
    
    // If we've already passed the action times, mark them as done for today
    if (current_time > sunriseActionTime) {
        sunrise_action_done = true;
    }
    
    if (current_time > sunsetActionTime) {
        sunset_action_done = true;
    }
    
    Serial.println("Scheduler: Time data updated successfully!");
    
    // Print the updated times for debugging
    char currentStr[9], sunriseStr[9], sunsetStr[9];
    formatTimeOfDay(current_time, currentStr);
    formatTimeOfDay(sunrise, sunriseStr);
    formatTimeOfDay(sunset, sunsetStr);
    
    Serial.print("Scheduler: Current: ");
    Serial.print(currentStr);
    Serial.print(" | Sunrise: ");
    Serial.print(sunriseStr);
    Serial.print(" | Sunset: ");
    Serial.println(sunsetStr);
}

void Scheduler::formatTimeOfDay(uint32_t timeOfDay, char* buffer) {
    const int hours{static_cast<int>(timeOfDay / 3600)};
    const int minutes{static_cast<int>((timeOfDay % 3600) / 60)};
    const int seconds{static_cast<int>(timeOfDay % 60)};
    
    sprintf(buffer, "%02d:%02d:%02d", hours, minutes, seconds);
}

// End of file /////////////////////////////////////////////////////////////////
