// main.cpp ////////////////////////////////////////////////////////////////////

#include <Arduino.h>
#include "TimeSync.h"
#include <time.h>
#include "credentials.h" // Contains WiFi credentials

// Time constants
constexpr uint32_t SECOND{1UL};
constexpr uint32_t MINUTE{60UL * SECOND};
constexpr uint32_t HOUR{60UL * MINUTE};
constexpr uint32_t DAY{24UL * HOUR};

// Location constants
constexpr float LATITUDE{37.7749F};
constexpr float LONGITUDE{-122.4194F};

// UTC offset in seconds (-7 hours for PDT)
constexpr int32_t TIMEZONE_OFFSET{-7L * static_cast<int32_t>(HOUR)};

// GPIO pin for onboard LED (inverted logic)
constexpr uint8_t LED_PIN{8U};

// Timing offsets for sunrise/sunset actions
constexpr uint32_t SUNRISE_ACTION_OFFSET{30UL * MINUTE};  // 30 minutes after sunrise
constexpr uint32_t SUNSET_ACTION_OFFSET{30UL * MINUTE};   // 30 minutes before sunset

// Update time constants
constexpr uint8_t UPDATE_HOUR{2U};  // 2 AM
constexpr uint32_t UPDATE_WINDOW{5UL * MINUTE};  // 5-minute window for updates
constexpr uint32_t RETRY_INTERVAL_SHORT{15UL * MINUTE * 1000UL};  // 15 minutes in milliseconds
constexpr uint32_t RETRY_INTERVAL_LONG{6UL * HOUR * 1000UL};  // 6 hours in milliseconds
constexpr uint8_t MAX_RETRY_COUNT_SHORT{5U};  // Number of short interval retries

// Global variables for storing the time data
uint32_t g_currentTime{0};  // Current time in seconds since midnight
uint32_t g_sunrise{0};      // Sunrise in seconds since midnight
uint32_t g_sunset{0};       // Sunset in seconds since midnight

// Action tracking flags
bool g_sunriseActionDone{false};
bool g_sunsetActionDone{false};

// LED blinking variables
unsigned long lastLedToggleTime{0};
bool ledState{false};

// Update tracking
bool updatedToday{false};          // Flag to track if we've already done the 2 AM update today
uint8_t failedUpdateAttempts{0};   // Count failed update attempts
unsigned long lastUpdateAttempt{0}; // Track when we last attempted an update

// Function prototypes
void updateAllTimes();
void printTimes();
void formatTimeOfDay(uint32_t timeOfDay, char* buffer);
void updateLed();
bool isUpdateTime();
void checkTimeActions();
void sunriseAction();
void sunsetAction();

// Create TimeSync instance
TimeSync timeSync(LATITUDE, LONGITUDE, TIMEZONE_OFFSET);

// Track when we last printed
unsigned long lastPrintTime{0};

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\nTimeSync Example");
  
  // Set up LED pin
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // Start with LED off (inverted logic)
  
  // Fetch time data on startup
  updateAllTimes();
}

void loop() {
  // Get current time from RTC and update our global current time variable
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    g_currentTime = (timeinfo.tm_hour * 3600) + 
                   (timeinfo.tm_min * 60) + 
                    timeinfo.tm_sec;
    
    // Check if it's time for the daily update (2 AM)
    if (isUpdateTime()) {
      updateAllTimes();
    }
    
    // Check if it's time to run sunrise/sunset actions
    checkTimeActions();
  }
  
  // Print times every 5 seconds
  if (millis() - lastPrintTime >= 5000UL) {
    printTimes();
    lastPrintTime = millis();
  }
  
  // Update LED state (non-blocking)
  updateLed();
  
  // Small delay to prevent CPU hogging
  delay(10UL);
}

// Check if actions need to be triggered based on time of day
void checkTimeActions() {
  // Only proceed if we have valid sunrise/sunset data
  if (g_sunrise == 0 || g_sunset == 0) return;
  
  // Reset flags at midnight
  if (g_currentTime < MINUTE) { // Within the first minute of the day
    g_sunriseActionDone = false;
    g_sunsetActionDone = false;
  }
  
  // Check for sunrise action (sunrise + offset)
  uint32_t sunriseActionTime{g_sunrise + SUNRISE_ACTION_OFFSET};
  if (!g_sunriseActionDone && g_currentTime >= sunriseActionTime) {
    sunriseAction();
    g_sunriseActionDone = true;
  }
  
  // Check for sunset action (sunset - offset)
  uint32_t sunsetActionTime{g_sunset - SUNSET_ACTION_OFFSET};
  if (!g_sunsetActionDone && g_currentTime >= sunsetActionTime) {
    sunsetAction();
    g_sunsetActionDone = true;
  }
}

// Action to perform after sunrise + offset
void sunriseAction() {
  // Dummy function - replace with your desired action
  char timeStr[9];
  formatTimeOfDay(g_currentTime, timeStr);
  Serial.print("SUNRISE ACTION TRIGGERED at ");
  Serial.println(timeStr);
}

// Action to perform before sunset - offset
void sunsetAction() {
  // Dummy function - replace with your desired action
  char timeStr[9];
  formatTimeOfDay(g_currentTime, timeStr);
  Serial.print("SUNSET ACTION TRIGGERED at ");
  Serial.println(timeStr);
}

bool isUpdateTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return false;
  }
  
  // Get current time in milliseconds
  const unsigned long currentMillis{millis()};
  
  // Check for regular update time (2:00-2:05 AM window)
  const bool isRegularUpdateTime{(timeinfo.tm_hour == UPDATE_HOUR && 
                              timeinfo.tm_min < (UPDATE_WINDOW / MINUTE))};
  
  // Check for retry after failure based on number of previous failures
  bool isRetryTime{false};
  if (failedUpdateAttempts > 0U) {
    unsigned long retryInterval;
    
    if (failedUpdateAttempts < MAX_RETRY_COUNT_SHORT) {
      // Retry every 15 minutes for the first several attempts
      retryInterval = RETRY_INTERVAL_SHORT;
    } else {
      // After MAX_RETRY_COUNT_SHORT failures, retry at longer intervals
      retryInterval = RETRY_INTERVAL_LONG;
    }
    
    isRetryTime = (currentMillis - lastUpdateAttempt >= retryInterval);
  }
  
  // If we're past the update window, reset the daily flag for tomorrow
  if (timeinfo.tm_hour > UPDATE_HOUR) {
    updatedToday = false;
  }
  
  // Determine if we should update
  if ((isRegularUpdateTime && !updatedToday) || isRetryTime) {
    // If this is the regular update, mark as updated for today
    if (isRegularUpdateTime) {
      updatedToday = true;
    }
    return true;
  }
  
  return false;
}

void updateAllTimes() {
  Serial.println("Attempting to fetch fresh time data...");
  lastUpdateAttempt = millis();
  
  // This function connects to WiFi, gets all time data, sets the RTC, then disconnects
  DaylightTimes times = timeSync.fetchTimeData();
  
  // Check if update was successful
  if (times.current == 0 && times.sunrise == 0 && times.sunset == 0) {
    // Update failed
    failedUpdateAttempts++;
    Serial.print("Time data update FAILED! Attempt #");
    Serial.println(failedUpdateAttempts);
    
    // If we've failed multiple times, schedule another attempt soon
    if (failedUpdateAttempts < 5) {
      // Try again in 15 minutes
      Serial.println("Will retry in 15 minutes");
    } else {
      // After 5 failed attempts, try again in 6 hours
      Serial.println("Will retry in 6 hours");
      // Reset the counter to avoid integer overflow during long-term operation
      if (failedUpdateAttempts >= 10) {
        failedUpdateAttempts = 5;
      }
    }
    return;
  }
  
  // Update was successful - store the fetched times
  g_currentTime = times.current;
  g_sunrise = times.sunrise;
  g_sunset = times.sunset;
  
  // Reset the failed attempts counter
  failedUpdateAttempts = 0;
  
  // When we update, determine if we've already passed the action times for today
  uint32_t sunriseActionTime = g_sunrise + SUNRISE_ACTION_OFFSET;
  uint32_t sunsetActionTime = g_sunset - SUNSET_ACTION_OFFSET;
  
  // If we've already passed the action times, mark them as done for today
  if (g_currentTime > sunriseActionTime) {
    g_sunriseActionDone = true;
  }
  
  if (g_currentTime > sunsetActionTime) {
    g_sunsetActionDone = true;
  }
  
  Serial.println("Time data updated successfully!");
}

void printTimes() {
  char currentStr[9], sunriseStr[9], sunsetStr[9];
  char sunriseActionStr[9], sunsetActionStr[9];
  
  formatTimeOfDay(g_currentTime, currentStr);
  formatTimeOfDay(g_sunrise, sunriseStr);
  formatTimeOfDay(g_sunset, sunsetStr);
  
  // Format action times
  uint32_t sunriseActionTime{g_sunrise + SUNRISE_ACTION_OFFSET};
  uint32_t sunsetActionTime{g_sunset - SUNSET_ACTION_OFFSET};
  formatTimeOfDay(sunriseActionTime, sunriseActionStr);
  formatTimeOfDay(sunsetActionTime, sunsetActionStr);
  
  // Check if it's daytime
  const bool isDaytime{(g_currentTime >= g_sunrise && g_currentTime < g_sunset)};
  
  Serial.print("Current: ");
  Serial.print(currentStr);
  Serial.print(" | Sunrise: ");
  Serial.print(sunriseStr);
  Serial.print(" | Sunset: ");
  Serial.print(sunsetStr);
  Serial.print(" | SunriseAction: ");
  Serial.print(sunriseActionStr);
  Serial.print(" | SunsetAction: ");
  Serial.print(sunsetActionStr);
  Serial.print(" | It's ");
  Serial.println(isDaytime ? "daytime" : "nighttime");
}

void formatTimeOfDay(uint32_t timeOfDay, char* buffer) {
  const int hours = timeOfDay / 3600;
  const int minutes = (timeOfDay % 3600) / 60;
  const int seconds = timeOfDay % 60;
  
  sprintf(buffer, "%02d:%02d:%02d", hours, minutes, seconds);
}

// Function to update LED blinking pattern based on current light conditions
void updateLed() {
  // LED blinking at 2Hz (500ms period)
  const unsigned long currentMillis{millis()};
  
  // Check if it's time to toggle the LED
  if (currentMillis - lastLedToggleTime >= 50UL) { // Check every 50ms for finer control
    lastLedToggleTime = currentMillis;
    
    // Default to nighttime pattern if we have no data yet
    bool isDaytime{false};
    
    // Only check day/night status if we have valid sunrise/sunset data
    if (g_sunrise > 0U && g_sunset > 0U) {
      isDaytime = (g_currentTime >= g_sunrise && g_currentTime < g_sunset);
    }
    
    // Calculate cycle position (0-499 ms within the 500ms period)
    const unsigned long cyclePosition{(currentMillis / 50UL) % 10UL}; // 10 * 50ms = 500ms period
    
    if (isDaytime) {
      // Daytime: 90% on, 10% off (9 steps on, 1 step off in a 10-step cycle)
      ledState = (cyclePosition < 9UL);
    } else {
      // Nighttime: 10% on, 90% off (1 step on, 9 steps off in a 10-step cycle)
      ledState = (cyclePosition < 1UL);
    }
    
    // Set LED state (inverted logic: LOW = on, HIGH = off)
    digitalWrite(LED_PIN, ledState ? LOW : HIGH);
  }
}

// End of file /////////////////////////////////////////////////////////////////
