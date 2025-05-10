// main.cpp //////////////////////////////////////////////////////////////////

#include <Arduino.h>
#include <time.h>
#include "TimeSync.h"
#include "credentials.h"
#include "blinds.h"

// Blinds Constants ////////////////////////////////////////////////////////////

constexpr uint32_t BTN_PIN{5U};

constexpr uint32_t TICK_PERIOD{10U}; // ms
constexpr uint32_t BUTTON_PRESS_LENGTH{200000U}; // us

constexpr uint32_t OPEN_TICKS{4200U};
constexpr int32_t TIMEOUT{60000}; // ms
constexpr float DUTY_CYCLE{0.40F};

//                     Dir Spd Enc Lim Opn         Tmt      Dty         Pwm
Blinds global_blinds_1(6,  2,  3,  4,  OPEN_TICKS, TIMEOUT, DUTY_CYCLE, 0);
Blinds global_blinds_2(7,  1,  10, 9,  OPEN_TICKS, TIMEOUT, DUTY_CYCLE, 1);
Blinds global_blinds_3(8,  0,  21, 20, OPEN_TICKS, TIMEOUT, DUTY_CYCLE, 2);

// Button variables
uint64_t buttonPressStartTime{0}; // Micros timestamp when button was pressed
bool buttonIsPressed{false};      // Current button state
bool buttonWasHandled{false};     // Whether the current press has been handled

// Time constants
constexpr int32_t SECOND{1};
constexpr int32_t MINUTE{60 * SECOND};
constexpr int32_t HOUR{60 * MINUTE};
constexpr int32_t DAY{24 * HOUR};

// Location constants
constexpr float LATITUDE{37.7749F};
constexpr float LONGITUDE{-122.4194F};

// UTC offset in seconds (-7 hours for PDT)
constexpr int32_t TIMEZONE_OFFSET{-7 * HOUR};

// Timing offsets for sunrise/sunset actions
constexpr int32_t SUNRISE_ACTION_OFFSET{30 * MINUTE};  // 30 minutes after sunrise
constexpr int32_t SUNSET_ACTION_OFFSET{30 * MINUTE};   // 30 minutes before sunset

// Update time constants
constexpr int32_t UPDATE_HOUR{2};  // 2 AM
constexpr int32_t UPDATE_WINDOW{5 * MINUTE};  // 5-minute window for updates
constexpr int32_t RETRY_INTERVAL_SHORT{15 * MINUTE * 1000};  // 15 minutes in milliseconds
constexpr int32_t RETRY_INTERVAL_LONG{6 * HOUR * 1000};  // 6 hours in milliseconds
constexpr uint8_t MAX_RETRY_COUNT_SHORT{5U};  // Number of short interval retries

// Global variables for storing the time data
int32_t g_currentTime{0};  // Current time in seconds since midnight
int32_t g_sunrise{0};      // Sunrise in seconds since midnight
int32_t g_sunset{0};       // Sunset in seconds since midnight

// Action tracking flags
bool g_sunriseActionDone{false};
bool g_sunsetActionDone{false};

// Update tracking
bool updatedToday{false};         // Flag to track if we've already done the 2 AM update today
uint8_t failedUpdateAttempts{0U}; // Count failed update attempts
uint32_t lastUpdateAttempt{0};    // Track when we last attempted an update

// Function prototypes
void updateAllTimes();
void printTimes();
void formatTimeOfDay(uint32_t timeOfDay, char* buffer);
bool isUpdateTime();
void checkTimeActions();
void sunriseAction();
void sunsetAction();
void handleButton();
void buttonHeldFunction();

// Create TimeSync instance
TimeSync timeSync(LATITUDE, LONGITUDE, TIMEZONE_OFFSET);

// Track when we last printed
uint32_t lastPrintTime{0};

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\nBlinds Control System");
  
  // Set up button pin with internal pull-up resistor
  pinMode(BTN_PIN, INPUT_PULLUP);
  
  // Fetch time data on startup
  updateAllTimes();
}

void loop() {
  // Handle button with debouncing
  handleButton();
  
  // Get current time from RTC and update our global current time variable
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    g_currentTime = (timeinfo.tm_hour * HOUR) + 
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
  if (millis() - lastPrintTime >= 5000U) {
    printTimes();
    lastPrintTime = millis();
  }
  
  // Small delay to prevent CPU hogging
  delay(10U);
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
  int32_t sunriseActionTime{g_sunrise + SUNRISE_ACTION_OFFSET};
  if (!g_sunriseActionDone && g_currentTime >= sunriseActionTime) {
    sunriseAction();
    g_sunriseActionDone = true;
  }
  
  // Check for sunset action (sunset - offset)
  int32_t sunsetActionTime{g_sunset - SUNSET_ACTION_OFFSET};
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

  global_blinds_1.open();
  global_blinds_2.open();
  global_blinds_3.open();
}

// Action to perform before sunset - offset
void sunsetAction() {
  // Dummy function - replace with your desired action
  char timeStr[9];
  formatTimeOfDay(g_currentTime, timeStr);
  Serial.print("SUNSET ACTION TRIGGERED at ");
  Serial.println(timeStr);

  global_blinds_1.close();
  global_blinds_2.close();
  global_blinds_3.close();
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
  DaylightTimes times = timeSync.fetchTimeData();
  
  // Check if update was successful
  if (times.current == 0 && times.sunrise == 0 && times.sunset == 0) {
    // Update failed
    failedUpdateAttempts++;
    Serial.print("Time data update FAILED! Attempt #");
    Serial.println(failedUpdateAttempts);
    
    // If we've failed multiple times, schedule another attempt soon
    if (failedUpdateAttempts < MAX_RETRY_COUNT_SHORT) {
      // Try again in 15 minutes
      Serial.println("Will retry in 15 minutes");
    } else {
      // After 5 failed attempts, try again in 6 hours
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
  
  // Reset the failed attempts counter
  failedUpdateAttempts = 0U;
  
  // When we update, determine if we've already passed the action times for today
  int32_t sunriseActionTime{g_sunrise + SUNRISE_ACTION_OFFSET};
  int32_t sunsetActionTime{g_sunset - SUNSET_ACTION_OFFSET};
  
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
  int32_t sunriseActionTime{g_sunrise + SUNRISE_ACTION_OFFSET};
  int32_t sunsetActionTime{g_sunset - SUNSET_ACTION_OFFSET};
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
  const int hours{static_cast<int>(timeOfDay / 3600)};
  const int minutes{static_cast<int>((timeOfDay % 3600) / 60)};
  const int seconds{static_cast<int>(timeOfDay % 60)};
  
  sprintf(buffer, "%02d:%02d:%02d", hours, minutes, seconds);
}

// Function to handle button press with debouncing
void handleButton() {
  // Read button state (LOW when pressed, HIGH when released due to pull-up)
  int buttonState = digitalRead(BTN_PIN);
  
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
  else if (buttonIsPressed) {
    buttonIsPressed = false;
  }
}

// Function to be called when button is held for the required time
void buttonHeldFunction() {
  Serial.println("Button held for 200ms - executing special function");
  
  // Toggle all blinds at once
  if (global_blinds_1.blindsAreClosed() && 
      global_blinds_2.blindsAreClosed() && 
      global_blinds_3.blindsAreClosed()) {
    Serial.println("Opening all blinds...");
    global_blinds_1.open();
    global_blinds_2.open();
    global_blinds_3.open();
  } else {
    Serial.println("Closing all blinds...");
    global_blinds_1.close();
    global_blinds_2.close();
    global_blinds_3.close();
  }
}

// End of file /////////////////////////////////////////////////////////////////
