// main.cpp ////////////////////////////////////////////////////////////////////

// Includes ////////////////////////////////////////////////////////////////////

#include <time.h>

#include "config.h"
#include "credentials.h"
#include "blinds.h"
#include "TimeSync.h"

// Global Variables ////////////////////////////////////////////////////////////

Blinds g_blinds1(PIN_DIR_1, PIN_SPD_1, PIN_ENC_1, PIN_LIM_1, OPEN_TICKS_1, TIMEOUT_1, DUTY_CYCLE_1, 0);
Blinds g_blinds2(PIN_DIR_2, PIN_SPD_2, PIN_ENC_2, PIN_LIM_2, OPEN_TICKS_2, TIMEOUT_2, DUTY_CYCLE_2, 1);
Blinds g_blinds3(PIN_DIR_3, PIN_SPD_3, PIN_ENC_3, PIN_LIM_3, OPEN_TICKS_3, TIMEOUT_3, DUTY_CYCLE_3, 2);

// Button variables
uint64_t buttonPressStartTime{0}; // Micros timestamp when button was pressed
bool buttonIsPressed{false};      // Current button state
bool buttonWasHandled{false};     // Whether the current press has been handled

// Global variables for storing the time data
int32_t g_currentTime{0};  // Current time in seconds since midnight
int32_t g_sunrise{0};      // Sunrise in seconds since midnight
int32_t g_sunset{0};       // Sunset in seconds since midnight

// Pre-calculated action times (updated once per day)
int32_t g_sunriseActionTime{0};  // Sunrise + offset
int32_t g_sunsetActionTime{0};   // Sunset - offset

// Action tracking flags
bool g_sunriseActionDone{false};
bool g_sunsetActionDone{false};

// Pre-calculated daytime bounds for efficient checking
bool g_isDaytime{false};  // Cache daytime status to avoid recalculation

// Update tracking
bool updatedToday{false};         // Flag to track if we've already done the 2 AM update today
uint8_t failedUpdateAttempts{0U}; // Count failed update attempts
uint32_t lastUpdateAttempt{0};    // Track when we last attempted an update

TimeSync timeSync(WIFI_SSID, WIFI_PASSWORD, LATITUDE, LONGITUDE, TIMEZONE_OFFSET);
uint32_t lastPrintTime{0};

// Function Prototypes /////////////////////////////////////////////////////////

void updateAllTimes();
void calculateActionTimes();
void updateDaytimeStatus();
void printTimes();
void formatTimeOfDay(uint32_t timeOfDay, char* buffer);
bool isUpdateTime();
void checkTimeActions();
void closeAllBlinds();
void openAllBlinds();
void sunriseAction();
void sunsetAction();
void handleButton();
void buttonHeldFunction();

// Main Functions //////////////////////////////////////////////////////////////

void setup() {
  delay(500U);
  Serial.begin(115200);
  Serial.println("\n\nBlinds Control System");
  
  // Set up button pin with internal pull-up resistor
  pinMode(PIN_BTN, INPUT_PULLUP);
  
  // Fetch time data on startup
  #if SCHEDULING
    updateAllTimes();
  #endif
}

void loop() {
  // Handle button with debouncing
  handleButton();
  
  #if SCHEDULING
    // Get current time from RTC and update our global current time variable
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      g_currentTime = (timeinfo.tm_hour * HOUR) + 
                    (timeinfo.tm_min * MINUTE) + 
                      timeinfo.tm_sec * SECOND;
      
      // Update daytime status efficiently
      updateDaytimeStatus();
      
      // Check if it's time for the daily update (2 AM)
      if (isUpdateTime()) {
        updateAllTimes();
      }
      
      // Check if it's time to run sunrise/sunset actions
      checkTimeActions();
    }
    
    // Print times every 5 seconds
    if (millis() - lastPrintTime >= 5000U) {
      printTimes();
      lastPrintTime = millis();
    }
  #endif
  
  // Small delay to prevent CPU hogging
  delay(TICK_PERIOD);
}

// Helper Functions ////////////////////////////////////////////////////////////

// Minimum time for sunrise action (don't open blinds too early)
constexpr int32_t MIN_SUNRISE_ACTION_TIME = 7 * HOUR; // 7:00 AM

// Calculate action times once when sunrise/sunset data is updated
void calculateActionTimes() {
  // Calculate sunrise action time (sunrise + offset), but not before minimum time
  const int32_t calculatedSunriseTime = g_sunrise + SUNRISE_ACTION_OFFSET;
  g_sunriseActionTime = (calculatedSunriseTime < MIN_SUNRISE_ACTION_TIME) ? 
                        MIN_SUNRISE_ACTION_TIME : calculatedSunriseTime;
  
  // Calculate sunset action time (sunset - offset)
  g_sunsetActionTime = g_sunset - SUNSET_ACTION_OFFSET;
  
  // Validate the calculated times
  if (g_sunriseActionTime < 0) g_sunriseActionTime = 0;
  if (g_sunriseActionTime >= DAY) g_sunriseActionTime = DAY - 1;
  if (g_sunsetActionTime < 0) g_sunsetActionTime = 0;
  if (g_sunsetActionTime >= DAY) g_sunsetActionTime = DAY - 1;
}

// Update daytime status efficiently
void updateDaytimeStatus() {
  g_isDaytime = (g_currentTime >= g_sunrise && g_currentTime < g_sunset);
}

// Check if actions need to be triggered based on time of day
void checkTimeActions() {
  // Only proceed if we have valid time data
  if (g_sunrise == 0 || g_sunset == 0 || g_currentTime == 0) {
    Serial.println("WARNING: Cannot check time actions - invalid time data");
    return;
  }
  
  // Reset flags at midnight
  if (g_currentTime < MINUTE) { // Within the first minute of the day
    g_sunriseActionDone = false;
    g_sunsetActionDone = false;
  }
  
  // Check for sunrise action using pre-calculated time
  if (!g_sunriseActionDone && (g_currentTime >= g_sunriseActionTime)) {
    #if AUTO_OPEN
      sunriseAction();
    #endif
    g_sunriseActionDone = true;
  }
  
  // Check for sunset action using pre-calculated time
  if (!g_sunsetActionDone && g_currentTime >= g_sunsetActionTime) {
    sunsetAction();
    g_sunsetActionDone = true;
  }
}

// Action to perform after sunrise + offset
void sunriseAction() {
  char timeStr[9];
  formatTimeOfDay(g_currentTime, timeStr);
  Serial.print("SUNRISE ACTION TRIGGERED at ");
  Serial.println(timeStr);

  openAllBlinds();
}

// Action to perform before sunset - offset
void sunsetAction() {
  char timeStr[9];
  formatTimeOfDay(g_currentTime, timeStr);
  Serial.print("SUNSET ACTION TRIGGERED at ");
  Serial.println(timeStr);

  // Close all blinds multiple times to ensure they're fully closed
  constexpr uint8_t CLOSE_ATTEMPTS = 3;
  for (uint8_t attempt = 1; attempt <= CLOSE_ATTEMPTS; ++attempt) {
    Serial.print("Closing attempt ");
    Serial.print(attempt);
    Serial.println(":");
    
    closeAllBlinds();
  }
}

bool isUpdateTime() {
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
  if (failedUpdateAttempts > 0U) {
    uint32_t retryInterval;
    
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
  const DaylightTimes times = timeSync.fetchTimeData();
  
  // Check if update was successful
  if ((times.current == 0) && (times.sunrise == 0) && (times.sunset == 0)) {
    // Update failed
    failedUpdateAttempts++;
    Serial.print("Time data update FAILED! Attempt #");
    Serial.println(failedUpdateAttempts);
    
    // If this is our first time running and we have no time data at all,
    // set reasonable default values to prevent crashes
    if (g_currentTime == 0) {
      // Read from RTC if possible
      struct tm timeinfo;
      if (getLocalTime(&timeinfo)) {
        g_currentTime = (timeinfo.tm_hour * HOUR) + 
                      (timeinfo.tm_min * MINUTE) + 
                      timeinfo.tm_sec;
        Serial.print("Using RTC time as fallback: ");
        Serial.println(g_currentTime);
      } else {
        // No RTC time available, use a default
        g_currentTime = 8 * HOUR; // Default to 8:00 AM
        Serial.println("Using default time: 08:00:00");
      }
    }
    
    // Set default sunrise/sunset if we don't have any
    if (g_sunrise == 0) {
      g_sunrise = 6 * HOUR + 30 * MINUTE; // Default to 6:30 AM
      Serial.println("Using default sunrise: 06:30:00");
    }
    
    if (g_sunset == 0) {
      g_sunset = 19 * HOUR + 30 * MINUTE; // Default to 7:30 PM
      Serial.println("Using default sunset: 19:30:00");
    }
    
    // Calculate action times with the default values
    calculateActionTimes();
    
    // If we've failed multiple times, schedule another attempt soon
    if (failedUpdateAttempts < MAX_RETRY_COUNT_SHORT) {
      // Try again in 15 minutes
      Serial.println("Will retry in 15 minutes");
    } else {
      // After MAX_RETRY_COUNT_SHORT failed attempts, try again in 6 hours
      Serial.println("Will retry in 6 hours");
      // Reset the counter to avoid integer overflow during long-term operation
      if (failedUpdateAttempts >= 10U) {
        failedUpdateAttempts = 5U;
      }
    }
    return;
  }
  
  // Update was successful - store the fetched times
  g_currentTime = times.current;
  g_sunrise = times.sunrise;
  g_sunset = times.sunset;
  
  // Make sure none of the time values are zero, which could cause issues
  if (g_currentTime == 0 || g_sunrise == 0 || g_sunset == 0) {
    Serial.println("WARNING: Received some zero time values despite 'successful' update");
    
    // Fix any zero values with defaults
    if (g_currentTime == 0) {
      struct tm timeinfo;
      if (getLocalTime(&timeinfo)) {
        g_currentTime = (timeinfo.tm_hour * HOUR) + 
                      (timeinfo.tm_min * MINUTE) + 
                      timeinfo.tm_sec;
      } else {
        g_currentTime = 8 * HOUR; // Default to 8:00 AM
      }
    }
    
    if (g_sunrise == 0) g_sunrise = 6 * HOUR + 30 * MINUTE; // Default to 6:30 AM
    if (g_sunset == 0) g_sunset = 19 * HOUR + 30 * MINUTE; // Default to 7:30 PM
  }
  
  // Calculate action times with the new data
  calculateActionTimes();
  
  // Reset the failed attempts counter
  failedUpdateAttempts = 0U;
  
  // When we update, determine if we've already passed the action times for today
  // Now using pre-calculated action times
  if (g_currentTime > g_sunriseActionTime) {
    g_sunriseActionDone = true;
  }
  
  if (g_currentTime > g_sunsetActionTime) {
    g_sunsetActionDone = true;
  }
  
  Serial.println("Time data updated successfully!");
  
  // Print the updated times for debugging
  char currentStr[9], sunriseStr[9], sunsetStr[9];
  char sunriseActionStr[9], sunsetActionStr[9];
  
  formatTimeOfDay(g_currentTime, currentStr);
  formatTimeOfDay(g_sunrise, sunriseStr);
  formatTimeOfDay(g_sunset, sunsetStr);
  formatTimeOfDay(g_sunriseActionTime, sunriseActionStr);
  formatTimeOfDay(g_sunsetActionTime, sunsetActionStr);
  
  Serial.print("Current: ");
  Serial.print(currentStr);
  Serial.print(" | Sunrise: ");
  Serial.print(sunriseStr);
  Serial.print(" (Action: ");
  Serial.print(sunriseActionStr);
  Serial.print(") | Sunset: ");
  Serial.print(sunsetStr);
  Serial.print(" (Action: ");
  Serial.print(sunsetActionStr);
  Serial.println(")");
}

void printTimes() {
  char currentStr[9], sunriseStr[9], sunsetStr[9];
  char sunriseActionStr[9], sunsetActionStr[9];
  
  formatTimeOfDay(g_currentTime, currentStr);
  formatTimeOfDay(g_sunrise, sunriseStr);
  formatTimeOfDay(g_sunset, sunsetStr);
  formatTimeOfDay(g_sunriseActionTime, sunriseActionStr);
  formatTimeOfDay(g_sunsetActionTime, sunsetActionStr);
  
  Serial.print("Current: ");
  Serial.print(currentStr);
  Serial.print(" | Sunrise: ");
  Serial.print(sunriseStr);
  Serial.print(" | Sunset: ");
  Serial.print(sunsetStr);
  Serial.print(" | It's ");
  Serial.println(g_isDaytime ? "daytime" : "nighttime");
}

void formatTimeOfDay(uint32_t timeOfDay, char* buffer) {
  const int hours{static_cast<int>(timeOfDay / 3600)};
  const int minutes{static_cast<int>((timeOfDay % 3600) / 60)};
  const int seconds{static_cast<int>(timeOfDay % 60)};
  
  sprintf(buffer, "%02d:%02d:%02d", hours, minutes, seconds);
}

// Function to handle button press with debouncing
void handleButton() {
  // Read button state (LOW when pressed, HIGH when released due to pull-up)
  const int buttonState = digitalRead(PIN_BTN);
  
  // Button is pressed (LOW)
  if (buttonState == LOW) {
    // If this is a new press
    if (!buttonIsPressed) {
      buttonIsPressed = true;
      buttonPressStartTime = micros();
      buttonWasHandled = false;
    } 
    // Check if button has been held long enough and not yet handled
    else if (!buttonWasHandled && 
             (micros() - buttonPressStartTime) >= BUTTON_PRESS_LENGTH) {
      buttonHeldFunction();
      buttonWasHandled = true;
    }
  }
  // Button is released
  else {
    buttonIsPressed = false;
  }
}

// Function to be called when button is held for the required time
void buttonHeldFunction() {
  Serial.println("Button held for 200ms - executing special function");

  #if SINGLE_ACTUATOR
    Serial.println("Toggling window 1 ...");
    g_blinds1.toggle();
    Serial.println("Done");
  #else
  // Toggle all blinds at once
  if (g_blinds1.blindsAreClosed() && 
      g_blinds2.blindsAreClosed() && 
      g_blinds3.blindsAreClosed()) {
    openAllBlinds();
  } else {
    closeAllBlinds();
  }
  #endif
}

void closeAllBlinds() {
  Serial.println("Closing window 1 ...");
  g_blinds1.close();

  Serial.println("Closing window 2 ...");
  g_blinds2.close();

  Serial.println("Closing window 3 ...");
  g_blinds3.close();

  Serial.println("Done");
}

void openAllBlinds() {
  Serial.println("Opening window 1 ...");
  g_blinds1.open();

  Serial.println("Opening window 2 ...");
  g_blinds2.open();

  Serial.println("Opening window 3 ...");
  g_blinds3.open();

  Serial.println("Done");
}

// End of file /////////////////////////////////////////////////////////////////
