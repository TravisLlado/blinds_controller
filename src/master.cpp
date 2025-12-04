/*
 * ESP32-C3 RS485 Master - Window Blind Controller with Scheduling
 */

#include <Arduino.h>
#include <HardwareSerial.h>
#include <time.h>
#include "config.h"
#include "credentials.h"
#include "button.h"
#include "timesync.h"

enum SystemState {
  IDLE,
  MOVING_BLINDS
};

uint8_t currentSlaveIndex = 0;  // Track which slave we're currently moving

// Function prototypes
void buttonAction();
void startOperation();
void startScheduledOperation(bool openBlinds);
void querySlaves();
void configureSlaves();
bool sendConfig(uint8_t slave, const ConfigPacket& config);
bool waitForResponse(uint8_t slave, uint8_t expectedCmd, uint8_t &data);
void sendCommand(uint8_t slave, uint8_t cmd);
void processIncoming();
void updateAllTimes();
void printTimes();
void formatTimeOfDay(uint32_t timeOfDay, char* buffer);
bool isUpdateTime();
void checkTimeActions();
void sunriseAction();
void sunsetAction();

// RS485 Communication
HardwareSerial rs485(0);
SystemState systemState = IDLE;
bool slaveStates[NUM_SLAVES] = {false};  // false=closed, true=open
bool targetOpen = false;  // What we're moving blinds to
unsigned long currentSlaveStart = 0;  // Track start time for current slave
unsigned long lastHeartbeat = 0;

// Button instance
Button button(PIN_BTN, BUTTON_PRESS_LENGTH, Button::ACTIVE_LOW, buttonAction);

// Time synchronization
TimeSync timeSync(WIFI_SSID, WIFI_PASSWORD, LATITUDE, LONGITUDE, TIMEZONE_OFFSET);

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
uint32_t lastPrintTime{0};

// Button callback - manual override
void buttonAction() {
  if (systemState == IDLE) {
    Serial.println("Master: Manual button pressed");
    startOperation();
  } else {
    Serial.println("Master: Button pressed but system busy - ignoring");
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n\nRS485 Master with Scheduling");
  
  // Initialize button
  button.begin();
  
  // Initialize RS485
  pinMode(RS485_DE_RE, OUTPUT);
  digitalWrite(RS485_DE_RE, LOW);  // Receive mode
  rs485.begin(9600, SERIAL_8N1, RS485_RX, RS485_TX);
  
  delay(1000);
  
  // Configure all slaves with parameters from config.h
  Serial.println("Configuring slaves...");
  configureSlaves();
  
  // Initial slave query
  querySlaves();
  
  // Fetch time data on startup
  #if SCHEDULING
    updateAllTimes();
    Serial.println("Automatic scheduling based on sunrise/sunset active.");
  #endif
  
  Serial.println("Ready. Manual button control available.");
}

void loop() {
  // Update button (handles debouncing and triggers callback)
  button.update();
  
  #if SCHEDULING
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
    
    // Print times every 30 seconds (less frequent than original to reduce spam)
    if (millis() - lastPrintTime >= 30000U) {
      printTimes();
      lastPrintTime = millis();
    }
  #endif
  
  // Heartbeat for system status
  if (millis() - lastHeartbeat >= 10000) {  // Every 10 seconds
    lastHeartbeat = millis();
    Serial.printf("Master: State=%d Blinds=[", systemState);
    for (int i = 0; i < NUM_SLAVES; i++) {
      Serial.print(slaveStates[i] ? "OPEN" : "CLOSED");
      if (i < (NUM_SLAVES - 1)) Serial.print(",");
    }
    Serial.println("]");
  }
  
  // Process incoming RS485 messages
  processIncoming();
  
  // Timeout check - for current slave
  if (systemState != IDLE) {
    if ((millis() - currentSlaveStart) > (MOTION_TIMEOUT * 1000U + 1000U)) {
      Serial.printf("Master: Slave %d timeout - aborting operation\r\n", currentSlaveIndex + 1);
      systemState = IDLE;
      currentSlaveIndex = 0;
    }
  }
  
  delay(TICK_PERIOD);
}

void configureSlaves() {
  ConfigPacket config;
  config.motion_length = MOTION_LENGTH;
  config.motor_speed_max = MOTOR_SPEED_MAX;
  
  Serial.println("Sending configuration to all slaves:");
  Serial.printf("  Motion Length: %.2f turns\r\n", config.motion_length);
  Serial.printf("  Motor Speed Max: %.2f RPM\r\n", config.motor_speed_max);
  
  for (int i = 0; i < NUM_SLAVES; i++) {
    uint8_t slave = i + 1;
    if (sendConfig(slave, config)) {
      Serial.printf("  Slave %d: Config OK\r\n", slave);
    } else {
      Serial.printf("  Slave %d: Config FAILED\r\n", slave);
    }
    delay(100);  // Small delay between slaves
  }
}

bool sendConfig(uint8_t slave, const ConfigPacket& config) {
  digitalWrite(RS485_DE_RE, HIGH);
  delayMicroseconds(10);
  
  // Pack floats into bytes (4 bytes each)
  uint8_t buffer[13];  // START + ADDR + CMD + 8 bytes data + CHECKSUM + END
  buffer[0] = START_BYTE;
  buffer[1] = slave;
  buffer[2] = CMD_SET_CONFIG;
  
  // Copy motion_length (4 bytes)
  memcpy(&buffer[3], &config.motion_length, sizeof(float));
  
  // Copy motor_speed_max (4 bytes)
  memcpy(&buffer[7], &config.motor_speed_max, sizeof(float));
  
  // Calculate checksum over all data bytes
  uint8_t checksum = START_BYTE ^ slave ^ CMD_SET_CONFIG;
  for (int i = 3; i < 11; i++) {
    checksum ^= buffer[i];
  }
  buffer[11] = checksum;
  buffer[12] = END_BYTE;
  
  // Send the packet
  rs485.write(buffer, 13);
  rs485.flush();
  delayMicroseconds(10);
  digitalWrite(RS485_DE_RE, LOW);
  
  // Wait for acknowledgment
  uint8_t dummy;
  return waitForResponse(slave, RSP_CONFIG_ACK, dummy);
}

// Manual button operation - toggles all blinds to synchronized state
void startOperation() {
  querySlaves();
  
  // Simple logic: if all closed, open them. Otherwise, close them.
  bool allClosed = true;
  for (int i = 0; i < NUM_SLAVES; i++) {
    if (slaveStates[i]) {
      allClosed = false;
      break;
    }
  }
  
  if (allClosed) {
    Serial.println("Master: All closed - opening all");
    targetOpen = true;
  } else {
    Serial.println("Master: At least one open - closing all");
    targetOpen = false;
  }
  
  // Start with first slave
  currentSlaveIndex = 0;
  systemState = MOVING_BLINDS;
  currentSlaveStart = millis();
  
  sendCommand(currentSlaveIndex + 1, (targetOpen ? CMD_OPEN : CMD_CLOSE));
  Serial.printf("Master: Moving slave %d\r\n", currentSlaveIndex + 1);
}

// Scheduled operation - opens or closes all blinds to specified state
void startScheduledOperation(bool openBlinds) {
  if (systemState != IDLE) {
    Serial.println("Master: Cannot start scheduled operation - system busy");
    return;
  }
  
  // Query current states
  querySlaves();
  
  // Check if we're already in the desired state
  bool allInTargetState = true;
  for (int i = 0; i < NUM_SLAVES; i++) {
    if (slaveStates[i] != openBlinds) {
      allInTargetState = false;
      break;
    }
  }
  
  if (allInTargetState) {
    Serial.printf("Master: All blinds already %s - no action needed\r\n", 
                  openBlinds ? "open" : "closed");
    return;
  }
  
  Serial.printf("Master: Scheduled operation - %s all blinds\r\n", 
                openBlinds ? "opening" : "closing");
  
  targetOpen = openBlinds;
  
  // Start with first slave
  currentSlaveIndex = 0;
  systemState = MOVING_BLINDS;
  currentSlaveStart = millis();
  
  sendCommand(currentSlaveIndex + 1, (targetOpen ? CMD_OPEN : CMD_CLOSE));
  Serial.printf("Master: Moving slave %d\r\n", currentSlaveIndex + 1);
}

void querySlaves() {
  Serial.println("Master: Querying all slaves...");
  
  for (int i = 0; i < NUM_SLAVES; i++) {
    uint8_t slave = i + 1;
    sendCommand(slave, CMD_QUERY);
    
    uint8_t data;
    if (waitForResponse(slave, RSP_STATE, data)) {
      slaveStates[i] = (data == 1);
      Serial.printf("  Slave %d: %s\r\n", slave, (slaveStates[i] ? "OPEN" : "CLOSED"));
    } else {
      Serial.printf("  Slave %d: No response\r\n", slave);
    }
  }
}

void processIncoming() {
  static uint8_t rxBuffer[6];
  static uint8_t rxIndex = 0;
  
  while (rs485.available()) {
    uint8_t b = rs485.read();
    
    if ((rxIndex == 0) && (b != START_BYTE)) continue;
    
    rxBuffer[rxIndex++] = b;
    
    if (rxIndex == 6) {
      rxIndex = 0;
      
      if (rxBuffer[5] == END_BYTE) {
        uint8_t slave = rxBuffer[1];
        uint8_t cmd = rxBuffer[2];
        uint8_t data = rxBuffer[3];
        uint8_t checksum = rxBuffer[4];
        uint8_t expected = START_BYTE ^ slave ^ cmd ^ data;
        
        if (checksum == expected && cmd == RSP_COMPLETE) {
          Serial.printf("Master: Slave %d movement complete\r\n", slave);
          
          // Update state
          if (slave >= 1 && slave <= NUM_SLAVES) {
            slaveStates[slave-1] = (data == 1);
          }
          
          // If we're in moving state and this is the slave we're waiting for
          if (systemState == MOVING_BLINDS && slave == (currentSlaveIndex + 1)) {
            // Move to next slave
            currentSlaveIndex++;
            
            if (currentSlaveIndex >= NUM_SLAVES) {
              // All done
              Serial.println("Master: All operations complete");
              systemState = IDLE;
              currentSlaveIndex = 0;
            } else {
              // Start next slave
              currentSlaveStart = millis();
              sendCommand(currentSlaveIndex + 1, (targetOpen ? CMD_OPEN : CMD_CLOSE));
              Serial.printf("Master: Moving slave %d\r\n", currentSlaveIndex + 1);
            }
          }
        }
      }
    }
  }
}

bool waitForResponse(uint8_t slave, uint8_t expectedCmd, uint8_t &data) {
  unsigned long start = millis();
  uint8_t rxBuffer[6];
  uint8_t rxIndex = 0;
  
  while ((millis() - start) < RX_TIMEOUT) {
    if (rs485.available()) {
      uint8_t b = rs485.read();
      
      if (rxIndex == 0 && b != START_BYTE) continue;
      
      rxBuffer[rxIndex++] = b;
      
      if (rxIndex == 6) {
        if (rxBuffer[5] == END_BYTE &&
            rxBuffer[1] == slave &&
            rxBuffer[2] == expectedCmd) {
          uint8_t checksum = START_BYTE ^ slave ^ expectedCmd ^ rxBuffer[3];
          if (rxBuffer[4] == checksum) {
            data = rxBuffer[3];
            return true;
          }
        }
        rxIndex = 0;
      }
    }
    delay(1);
  }
  return false;
}

void sendCommand(uint8_t slave, uint8_t cmd) {
  digitalWrite(RS485_DE_RE, HIGH);
  delayMicroseconds(10);
  
  uint8_t checksum = START_BYTE ^ slave ^ cmd;
  
  rs485.write(START_BYTE);
  rs485.write(slave);
  rs485.write(cmd);
  rs485.write(checksum);
  rs485.write(END_BYTE);
  
  rs485.flush();
  delayMicroseconds(10);
  digitalWrite(RS485_DE_RE, LOW);
}

// ============================================================================
// SCHEDULING FUNCTIONALITY (Integrated from old program)
// ============================================================================

// Check if actions need to be triggered based on time of day
void checkTimeActions() {
  // Only proceed if we have valid sunrise/sunset data and system is idle
  if (g_sunrise == 0 || g_sunset == 0 || g_currentTime == 0) {
    return;  // Don't spam warnings - just skip silently
  }
  
  // Only run scheduled actions when system is idle
  if (systemState != IDLE) {
    return;
  }
  
  // Reset flags at midnight
  if (g_currentTime < MINUTE) { // Within the first minute of the day
    g_sunriseActionDone = false;
    g_sunsetActionDone = false;
  }
  
  // Check for sunrise action (sunrise + offset)
  int32_t sunriseActionTime{g_sunrise + SUNRISE_ACTION_OFFSET};
  
  // Validate the calculated time
  if (sunriseActionTime < 0) sunriseActionTime = 0;
  if (sunriseActionTime >= DAY) sunriseActionTime = DAY - 1;
  
  if (!g_sunriseActionDone && g_currentTime >= sunriseActionTime) {
    #if AUTO_OPEN
      sunriseAction();
    #endif
    g_sunriseActionDone = true;
  }
  
  // Check for sunset action (sunset - offset)
  int32_t sunsetActionTime{g_sunset - SUNSET_ACTION_OFFSET};
  
  // Validate the calculated time
  if (sunsetActionTime < 0) sunsetActionTime = 0;
  if (sunsetActionTime >= DAY) sunsetActionTime = DAY - 1;
  
  if (!g_sunsetActionDone && g_currentTime >= sunsetActionTime) {
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

  // Use RS485 distributed operation instead of direct control
  startScheduledOperation(true);  // Open all blinds
}

// Action to perform before sunset - offset
void sunsetAction() {
  char timeStr[9];
  formatTimeOfDay(g_currentTime, timeStr);
  Serial.print("SUNSET ACTION TRIGGERED at ");
  Serial.println(timeStr);

  // Use RS485 distributed operation instead of direct control
  startScheduledOperation(false); // Close all blinds
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
  
  // Reset the failed attempts counter
  failedUpdateAttempts = 0U;
  
  // When we update, determine if we've already passed the action times for today
  int32_t sunriseActionTime{g_sunrise + SUNRISE_ACTION_OFFSET};
  int32_t sunsetActionTime{g_sunset - SUNSET_ACTION_OFFSET};
  
  // Validate calculated times to prevent issues
  if (sunriseActionTime < 0) sunriseActionTime = 0;
  if (sunriseActionTime > DAY) sunriseActionTime = DAY - 1;
  if (sunsetActionTime < 0) sunsetActionTime = 0;
  if (sunsetActionTime > DAY) sunsetActionTime = DAY - 1;
  
  // If we've already passed the action times, mark them as done for today
  if (g_currentTime > sunriseActionTime) {
    g_sunriseActionDone = true;
  }
  
  if (g_currentTime > sunsetActionTime) {
    g_sunsetActionDone = true;
  }
  
  Serial.println("Time data updated successfully!");
  
  // Print the updated times for debugging
  char currentStr[9], sunriseStr[9], sunsetStr[9];
  formatTimeOfDay(g_currentTime, currentStr);
  formatTimeOfDay(g_sunrise, sunriseStr);
  formatTimeOfDay(g_sunset, sunsetStr);
  
  Serial.print("Current: ");
  Serial.print(currentStr);
  Serial.print(" | Sunrise: ");
  Serial.print(sunriseStr);
  Serial.print(" | Sunset: ");
  Serial.println(sunsetStr);
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
  Serial.print(" (Action at ");
  Serial.print(sunriseActionStr);
  Serial.print(") | Sunset: ");
  Serial.print(sunsetStr);
  Serial.print(" (Action at ");
  Serial.print(sunsetActionStr);
  Serial.print(") | ");
  Serial.println(isDaytime ? "Daytime" : "Nighttime");
}

void formatTimeOfDay(uint32_t timeOfDay, char* buffer) {
  const int hours{static_cast<int>(timeOfDay / 3600)};
  const int minutes{static_cast<int>((timeOfDay % 3600) / 60)};
  const int seconds{static_cast<int>(timeOfDay % 60)};
  
  sprintf(buffer, "%02d:%02d:%02d", hours, minutes, seconds);
}

// End of file /////////////////////////////////////////////////////////////////