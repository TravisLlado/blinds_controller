/*
 * ESP32-C3 RS485 Master - Window Blind Controller with Scheduling
 */

#include <Arduino.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <WebSerial.h>
#include <time.h>
#include "config.h"
#include "credentials.h"
#include "button.h"
#include "WebInfo.h"

enum SystemState {
  IDLE,
  MOVING_BLINDS
};

// Which (if any) scheduled action is currently mid-chain, waiting on
// processIncoming() to confirm the remaining slaves. ACTION_NONE means either
// nothing is running or a manual (button/WebSerial) operation is running --
// manual operations never latch a "done" flag so they don't need tracking.
enum ScheduledAction : uint8_t {
  ACTION_NONE,
  ACTION_SUNRISE,
  ACTION_SUNSET,
  ACTION_MIDDAY_CLOSE,
  ACTION_MIDDAY_OPEN,
};

// Outcome of synchronously attempting to start an operation.
enum AttemptResult : uint8_t {
  ATTEMPT_DONE,         // fully resolved already -- e.g. nothing needed to move
  ATTEMPT_FAILED,       // failed before any slave could be confirmed -- retry later
  ATTEMPT_IN_PROGRESS,  // first slave confirmed and moving -- resolves asynchronously
};

uint8_t currentSlaveIndex = 0;  // Track which slave we're currently moving

// Function prototypes
void buttonAction();
void buttonLongPressAction();
void startOperation();
AttemptResult startScheduledOperation(bool openBlinds, ScheduledAction action = ACTION_NONE);
AttemptResult attemptOperationOnSlaves(const uint8_t* slaveIndices, uint8_t count, bool openBlinds, ScheduledAction action);
void markScheduledActionResult(bool success);
void querySlaves();
void querySlavesWithRetries(unsigned long delayMs, uint8_t maxRetries);
bool sendConfig(uint8_t slave, const ConfigPacket& config);
bool sendConfigRetried(uint8_t slave, const ConfigPacket& config);
bool waitForResponse(uint8_t slave, uint8_t expectedCmd, uint8_t &data);
void sendCommand(uint8_t slave, uint8_t cmd);
bool sendCommandConfirmed(uint8_t slave, uint8_t cmd);
bool commandSlaveConfirmed(uint8_t slave, uint8_t cmd);
void processIncoming();
void printTimes();
void formatTimeOfDay(uint32_t timeOfDay, char* buffer);
void checkTimeActions();
AttemptResult sunriseAction();
AttemptResult sunsetAction();
void fetchGeoBlocking();
bool syncTimeAndSun();
void onWebSerialMessage(uint8_t* data, size_t len);

// HTTP server on configured port (URL: http://<MDNS_HOSTNAME>.local/)
AsyncWebServer server(SERVER_PORT);

// RS485 Communication
HardwareSerial rs485(0);
SystemState systemState = IDLE;
bool slaveStates[NUM_SLAVES] = {false};  // false=closed, true=open
bool slaveStateValid[NUM_SLAVES] = {false};  // Track if we have valid state data
bool targetOpen = false;  // What we're moving blinds to
unsigned long currentSlaveStart = 0;  // Track start time for current slave

// Responsive windows: only operate slaves that responded at query time
uint8_t responsiveSlaveIndices[NUM_SLAVES];  // Slave indices (0-based) that responded
uint8_t numResponsiveSlaves = 0;             // Count of responsive slaves for current operation

// Button instance (short press = toggle all; long press = toggle third window only)
Button button(PIN_BTN, BUTTON_DEBOUNCE_MS, BUTTON_SHORT_PRESS_MS, BUTTON_LONG_PRESS_MS,
              Button::ACTIVE_LOW, buttonAction, buttonLongPressAction);

// Global variables for storing the time data
int32_t g_currentTime{0};  // Current time in seconds since midnight
int32_t g_sunrise{0};      // Sunrise in seconds since midnight
int32_t g_sunset{0};       // Sunset in seconds since midnight

// Action tracking flags -- only set true once an action is CONFIRMED complete,
// never merely attempted. On failure the corresponding *RetryAt is set instead
// so checkTimeActions() re-attempts later rather than silently giving up for
// the day or hammering the bus every tick.
bool g_sunriseActionDone{false};
bool g_sunsetActionDone{false};
bool g_middayCloseDone{false};   // 10:45 AM: close window 3 (bedroom only)
bool g_middayOpenDone{false};    // 12:45 PM: re-open window 3 (bedroom only)

uint32_t g_sunriseRetryAt{0};
uint32_t g_sunsetRetryAt{0};
uint32_t g_middayCloseRetryAt{0};
uint32_t g_middayOpenRetryAt{0};

// Which scheduled action (if any) is currently mid-chain
ScheduledAction g_activeScheduledAction{ACTION_NONE};

uint32_t lastPrintTime{0};

// Single config for all slaves (from config.h; never changes at runtime)
static const ConfigPacket kSlaveConfig = {
  MOTION_LENGTH,
  MOTOR_SPEED_OPEN,
  MOTOR_SPEED_CLOSE
};

// Geolocation and time state
float geoLat          {NAN};
float geoLon          {NAN};
bool  geoValid        {false};
int   tzOffsetMinutes {0};
bool  timeValid       {false};
bool  sunTimesValid   {false};

unsigned long lastWifiCheckMs     {0};
int           lastDailyRefreshDay {-1};

// Sunrise action time, clamped to MIN_SUNRISE_ACTION_TIME (never before 7 AM).
static inline int32_t sunriseActionTimeClamped() {
  const int32_t t{g_sunrise + SUNRISE_ACTION_OFFSET};
  return (t < MIN_SUNRISE_ACTION_TIME) ? MIN_SUNRISE_ACTION_TIME : t;
}

// ============================================================================
// WiFi / Geo / Time
// ============================================================================

// Fetch geolocation — blocks with retries until success.
void fetchGeoBlocking() {
  while (!geoValid) {
    geoValid = WebInfo::fetchGeo(geoLat, geoLon, tzOffsetMinutes);
    if (geoValid) {
      Serial.printf("Geo approx: %.4f, %.4f\n", geoLat, geoLon);
    } else {
      Serial.println("Geo failed, retrying in 5s...");
      delay(RETRY_DELAY_MS);
    }
  }
}

// Sync NTP clock and fetch today's sunrise/sunset in one shot.
// Returns true only if both steps succeed.
bool syncTimeAndSun() {
  if (!WebInfo::initTime(tzOffsetMinutes)) {
    Serial.println("Time sync failed.");
    return false;
  }
  int newRise{-1}, newSet{-1};
  if (!WebInfo::fetchSunTimes(geoLat, geoLon, tzOffsetMinutes, newRise, newSet)) {
    Serial.println("Sun times sync failed.");
    return false;
  }
  g_sunrise     = newRise;
  g_sunset      = newSet;
  timeValid     = true;
  sunTimesValid = true;
  return true;
}

// ============================================================================
// WebSerial command handler
// ============================================================================

void onWebSerialMessage(uint8_t* data, size_t len) {
  String cmd;
  for (size_t i{0}; i < len; i++) {
    cmd += (char)data[i];
  }
  cmd.trim();
  cmd.toLowerCase();

  // Parse optional --one/--two/--three flag from command
  static const char* slaveNames[] = {"one", "two", "three"};
  int targetSlave = 0;  // 0 = all, 1-3 = specific slave
  String baseCmd = cmd;
  int dashPos = cmd.indexOf("--");
  if (dashPos >= 0) {
    String flag = cmd.substring(dashPos + 2);
    flag.trim();
    baseCmd = cmd.substring(0, dashPos);
    baseCmd.trim();
    for (int i = 0; i < NUM_SLAVES; i++) {
      if (flag == slaveNames[i]) {
        targetSlave = i + 1;
        break;
      }
    }
    if (targetSlave == 0) {
      WebSerial.println("Unknown window. Use --one, --two, or --three.");
      return;
    }
  }

  if (baseCmd == "open" || baseCmd == "close") {
    bool opening = (baseCmd == "open");
    uint8_t cmdByte = opening ? CMD_OPEN : CMD_CLOSE;
    if (targetSlave > 0) {
      if (systemState != IDLE) {
        WebSerial.println("System busy.");
        return;
      }
      WebSerial.printf("%s window %s...\n", opening ? "Opening" : "Closing", slaveNames[targetSlave - 1]);
      numResponsiveSlaves = 1;
      responsiveSlaveIndices[0] = targetSlave - 1;
      targetOpen = opening;
      currentSlaveIndex = 0;
      systemState = MOVING_BLINDS;
      currentSlaveStart = millis();
      if (!commandSlaveConfirmed(targetSlave, cmdByte)) {
        WebSerial.printf("Window %s did not confirm command - aborting.\n", slaveNames[targetSlave - 1]);
        systemState = IDLE;
        currentSlaveIndex = 0;
      }
    } else {
      WebSerial.printf("%s all blinds...\n", opening ? "Opening" : "Closing");
      startScheduledOperation(opening);
    }
  } else if (baseCmd == "half") {
    uint8_t slave = targetSlave > 0 ? targetSlave : 3;
    WebSerial.printf("Closing window %s halfway...\n", slaveNames[slave - 1]);
    sendConfigRetried(slave, kSlaveConfig);
    sendCommand(slave, CMD_CLOSE_HALF);
  } else if (baseCmd == "status") {
    WebSerial.printf("State=%d Blinds=[", systemState);
    for (int i = 0; i < NUM_SLAVES; i++) {
      if (slaveStateValid[i]) {
        WebSerial.print(slaveStates[i] ? "OPEN" : "CLOSED");
      } else {
        WebSerial.print("?");
      }
      if (i < (NUM_SLAVES - 1)) WebSerial.print(",");
    }
    WebSerial.println("]");
    printTimes();
  } else if (baseCmd == "help" || baseCmd == "?") {
    WebSerial.println("Commands: open, close, half, status, help");
    WebSerial.println("Add --one, --two, --three for individual windows.");
    WebSerial.println("Example: open --two");
  } else if (baseCmd.length() > 0) {
    WebSerial.println("Unknown command. Type 'help' for commands.");
  }
}

// ============================================================================
// Button callbacks
// ============================================================================

// Button callback - short press: toggle all blinds
void buttonAction() {
  if (systemState == IDLE) {
    Serial.println("Master: Manual button pressed");
    startOperation();
  } else {
    Serial.println("Master: Button pressed but system busy - ignoring");
  }
}

// Button callback - long press: cycle through window states
// All closed -> open 1 & 2; All open -> close 3; 1 & 2 open, 3 closed -> open 3
void buttonLongPressAction() {
  if (systemState != IDLE) {
    Serial.println("Master: Long press ignored - system busy");
    return;
  }

  // Query all slaves to get current state
  querySlaves();

  // Need valid state for all three slaves
  if (!slaveStateValid[0] || !slaveStateValid[1] || !slaveStateValid[2]) {
    Serial.println("Master: Long press - not all slaves responding");
    return;
  }

  const bool closed1 = !slaveStates[0];
  const bool closed2 = !slaveStates[1];
  const bool closed3 = !slaveStates[2];

  if (closed1 && closed2 && closed3) {
    // All closed -> open 1 & 2
    Serial.println("Master: Long press - all closed, opening 1 & 2");
    numResponsiveSlaves = 2;
    responsiveSlaveIndices[0] = 0;
    responsiveSlaveIndices[1] = 1;
    targetOpen = true;
  } else if (!closed1 && !closed2 && !closed3) {
    // All open -> close 3
    Serial.println("Master: Long press - all open, closing 3");
    numResponsiveSlaves = 1;
    responsiveSlaveIndices[0] = 2;
    targetOpen = false;
  } else if (!closed1 && !closed2 && closed3) {
    // 1 & 2 open, 3 closed -> open 3
    Serial.println("Master: Long press - 1 & 2 open, opening 3");
    numResponsiveSlaves = 1;
    responsiveSlaveIndices[0] = 2;
    targetOpen = true;
  } else {
    Serial.println("Master: Long press - mixed state, no action");
    return;
  }

  currentSlaveIndex = 0;
  systemState = MOVING_BLINDS;
  currentSlaveStart = millis();

  uint8_t slaveId = responsiveSlaveIndices[0] + 1;
  if (commandSlaveConfirmed(slaveId, (targetOpen ? CMD_OPEN : CMD_CLOSE))) {
    Serial.printf("Master: Moving slave %d\r\n", slaveId);
  } else {
    Serial.printf("Master: Slave %d did not confirm - aborting\r\n", slaveId);
    systemState = IDLE;
    currentSlaveIndex = 0;
  }
}

// ============================================================================
// Setup
// ============================================================================

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  Serial.println("\r\n\r\nRS485 Master with Scheduling");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_OFF);

  // Initialize button
  button.begin();

  // Initialize RS485
  pinMode(RS485_DE_RE, OUTPUT);
  digitalWrite(RS485_DE_RE, LOW);  // Receive mode
  rs485.begin(9600, SERIAL_8N1, RS485_RX, RS485_TX);

  delay(1000);

  // Initial slave query with retries so slow-to-boot slaves get a chance to respond
  querySlavesWithRetries(2000, 2);

  // Connect to WiFi — retry indefinitely; nothing works without a network
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("WiFi failed, retrying in 5s...");
    delay(RETRY_DELAY_MS);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
  if (MDNS.begin(MDNS_HOSTNAME)) {
    MDNS.addService("http", "tcp", SERVER_PORT);
    Serial.println(String("mDNS: http://") + MDNS_HOSTNAME + ".local/");
  } else {
    Serial.println("mDNS failed");
  }
  Serial.println("WebSerial: http://" + WiFi.localIP().toString() + "/");

  // Fetch geolocation — required by all subsequent steps
  fetchGeoBlocking();

  // Sync clock and sun times — retry until success
  while (!syncTimeAndSun()) {
    Serial.println("Retrying in 5s...");
    delay(RETRY_DELAY_MS);
  }
  Serial.print("Sunrise (local approx): ");
  Serial.println(WebInfo::formatLocalTime(g_sunrise));
  Serial.print("Sunset  (local approx): ");
  Serial.println(WebInfo::formatLocalTime(g_sunset));

  // Record today's date and determine initial action-done flags
  {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      Serial.printf(
        "Time set: %04d-%02d-%02d %02d:%02d:%02d\n",
        timeinfo.tm_year + 1900,
        timeinfo.tm_mon + 1,
        timeinfo.tm_mday,
        timeinfo.tm_hour,
        timeinfo.tm_min,
        timeinfo.tm_sec
      );
      lastDailyRefreshDay = timeinfo.tm_mday;

      g_currentTime = (timeinfo.tm_hour * HOUR) +
                      (timeinfo.tm_min * MINUTE) +
                      timeinfo.tm_sec * SECOND;

      const int32_t sunriseActionTime{sunriseActionTimeClamped()};
      const int32_t sunsetActionTime{g_sunset - SUNSET_ACTION_OFFSET};
      if (g_currentTime > sunriseActionTime) g_sunriseActionDone = true;
      if (g_currentTime > sunsetActionTime)  g_sunsetActionDone  = true;
      if (g_currentTime > 10 * HOUR + 45 * 60) g_middayCloseDone   = true;
      if (g_currentTime > 12 * HOUR + 45 * 60) g_middayOpenDone    = true;

      // LED indicates scheduled state: ON = blinds should be open
      const bool shouldBeOpen{g_currentTime >= sunriseActionTime && g_currentTime < sunsetActionTime};
      digitalWrite(LED_PIN, shouldBeOpen ? LED_ON : LED_OFF);
    }
  }
  Serial.println("Automatic scheduling based on sunrise/sunset active.");

  WebSerial.onMessage(onWebSerialMessage);
  WebSerial.begin(&server, "/");
  server.begin();

  WebSerial.println("WebSerial ready.");
  WebSerial.println(String("URL: http://") + MDNS_HOSTNAME + ".local/");
  WebSerial.println("Alt: http://" + WiFi.localIP().toString() + "/");
  WebSerial.println("Commands: open, close, half, status, help");

  Serial.println("Ready. Manual button control available.");
}

// ============================================================================
// Main loop
// ============================================================================

void loop() {
  const unsigned long now{millis()};

  // Update button (handles debouncing and triggers callback)
  button.update();

  // Periodic WiFi watchdog: re-join if the connection has dropped
  if (now - lastWifiCheckMs >= WIFI_CHECK_INTERVAL_MS) {
    lastWifiCheckMs = now;
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi lost, reconnecting...");
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
  }

  // Get current time from RTC and update our global current time variable
  struct tm timeinfo;
  bool haveTime{getLocalTime(&timeinfo)};

  if (haveTime) {
    g_currentTime = (timeinfo.tm_hour * HOUR) +
                  (timeinfo.tm_min * MINUTE) +
                    timeinfo.tm_sec * SECOND;

    // Daily refresh window: re-fetch geo (picks up DST changes), then
    // re-sync NTP and re-fetch today's sunrise/sunset.
    if (timeinfo.tm_hour >= DAILY_REFRESH_END_HOUR && timeinfo.tm_mday != lastDailyRefreshDay) {
      Serial.println("Daily refresh timed out, skipping until tomorrow.");
      lastDailyRefreshDay = timeinfo.tm_mday;
    } else if (timeinfo.tm_hour >= DAILY_REFRESH_START_HOUR &&
               timeinfo.tm_hour < DAILY_REFRESH_END_HOUR &&
               timeinfo.tm_mday != lastDailyRefreshDay) {
      Serial.println("Daily refresh...");
      WebInfo::fetchGeo(geoLat, geoLon, tzOffsetMinutes);
      if (syncTimeAndSun()) {
        lastDailyRefreshDay = timeinfo.tm_mday;
        // Re-read time so subsequent logic uses the fresh NTP sync
        if (getLocalTime(&timeinfo)) {
          g_currentTime = (timeinfo.tm_hour * HOUR) +
                          (timeinfo.tm_min * MINUTE) +
                          timeinfo.tm_sec * SECOND;
        }
        Serial.println("Daily refresh complete.");
      } else {
        Serial.println("Daily refresh failed, will retry next loop.");
      }
    }

    // Check if it's time to run sunrise/sunset actions
    checkTimeActions();
  }

  // Status tick: poll actuators and print combined status
  if (now - lastPrintTime >= STATUS_INTERVAL_MS) {
    lastPrintTime = now;

    // Poll actuator state when idle (avoid interfering with active operations)
    if (systemState == IDLE) {
      querySlaves();
    }

    // Build combined status line
    String msg;

    // Time info
    char currentStr[9];
    formatTimeOfDay(g_currentTime, currentStr);
    msg += currentStr;

    // Sunrise/sunset action times
    char openStr[9], closeStr[9];
    formatTimeOfDay(sunriseActionTimeClamped(), openStr);
    formatTimeOfDay(g_sunset  - SUNSET_ACTION_OFFSET,  closeStr);
    msg += " | open ";
    msg += openStr;
    msg += " close ";
    msg += closeStr;

    // Actuator states
    msg += " | [";
    for (int i = 0; i < NUM_SLAVES; i++) {
      if (slaveStateValid[i]) {
        msg += slaveStates[i] ? "O" : "C";
      } else {
        msg += "?";
      }
      if (i < (NUM_SLAVES - 1)) msg += ",";
    }
    msg += "]";

    // System state
    if (systemState != IDLE) {
      msg += " MOVING";
    }

    Serial.println(msg);
    WebSerial.println(msg);
  }

  // Process incoming RS485 messages
  processIncoming();

  // Timeout check - for current slave
  if (systemState != IDLE && currentSlaveIndex < numResponsiveSlaves) {
    if ((millis() - currentSlaveStart) > (MOTION_TIMEOUT * 1000U + 1000U)) {
      uint8_t timedOutSlave = responsiveSlaveIndices[currentSlaveIndex] + 1;
      Serial.printf("Master: Slave %d timeout - aborting operation\r\n", timedOutSlave);
      systemState = IDLE;
      currentSlaveIndex = 0;
      markScheduledActionResult(false);
    }
  }

  delay(TICK_PERIOD);
}

// ============================================================================
// RS485 Communication
// ============================================================================

bool sendConfig(uint8_t slave, const ConfigPacket& config) {
  digitalWrite(RS485_DE_RE, HIGH);
  delayMicroseconds(10);

  // Pack floats into bytes (4 bytes each, now 12 bytes total)
  uint8_t buffer[17];  // START + ADDR + CMD + 12 bytes data + CHECKSUM + END
  buffer[0] = START_BYTE;
  buffer[1] = slave;
  buffer[2] = CMD_SET_CONFIG;

  // Copy motion_length (4 bytes)
  memcpy(&buffer[3], &config.motion_length, sizeof(float));

  // Copy motor_speed_open (4 bytes)
  memcpy(&buffer[7], &config.motor_speed_open, sizeof(float));

  // Copy motor_speed_close (4 bytes)
  memcpy(&buffer[11], &config.motor_speed_close, sizeof(float));

  // Calculate checksum over all data bytes
  uint8_t checksum = START_BYTE ^ slave ^ CMD_SET_CONFIG;
  for (int i = 3; i < 15; i++) {
    checksum ^= buffer[i];
  }
  buffer[15] = checksum;
  buffer[16] = END_BYTE;

  // Send the packet
  rs485.write(buffer, 17);
  rs485.flush();
  delayMicroseconds(10);
  digitalWrite(RS485_DE_RE, LOW);

  // Wait for acknowledgment
  uint8_t dummy;
  return waitForResponse(slave, RSP_CONFIG_ACK, dummy);
}

// Retries sendConfig() up to COMMAND_RETRY_COUNT times if it goes unacknowledged.
bool sendConfigRetried(uint8_t slave, const ConfigPacket& config) {
  for (uint8_t attempt = 0; attempt < COMMAND_RETRY_COUNT; attempt++) {
    if (sendConfig(slave, config)) return true;
    if (attempt + 1 < COMMAND_RETRY_COUNT) delay(COMMAND_RETRY_DELAY_MS);
  }
  Serial.printf("Master: Slave %d did not ack config after %d attempts\r\n", slave, COMMAND_RETRY_COUNT);
  return false;
}

// Sends CMD_OPEN/CMD_CLOSE and waits for the slave's immediate RSP_COMMAND_ACK
// (sent before motion starts), retrying the send up to COMMAND_RETRY_COUNT
// times if unacknowledged. This only confirms receipt -- it does not wait for
// or retry based on motion completion (RSP_COMPLETE), which is handled
// separately and asynchronously in processIncoming().
bool sendCommandConfirmed(uint8_t slave, uint8_t cmd) {
  for (uint8_t attempt = 0; attempt < COMMAND_RETRY_COUNT; attempt++) {
    sendCommand(slave, cmd);
    uint8_t dummy;
    if (waitForResponse(slave, RSP_COMMAND_ACK, dummy)) return true;
    if (attempt + 1 < COMMAND_RETRY_COUNT) delay(COMMAND_RETRY_DELAY_MS);
  }
  Serial.printf("Master: Slave %d did not confirm command 0x%02X after %d attempts\r\n",
                slave, cmd, COMMAND_RETRY_COUNT);
  return false;
}

// Sends config then an open/close command to a single slave, confirming both.
// Returns false if either step failed after retries -- the slave was not
// reliably commanded and the caller should not assume it's moving.
bool commandSlaveConfirmed(uint8_t slave, uint8_t cmd) {
  if (!sendConfigRetried(slave, kSlaveConfig)) return false;
  return sendCommandConfirmed(slave, cmd);
}

// Manual button operation - toggles responsive blinds: if all closed then open all, else close all
void startOperation() {
  querySlaves();

  // Build list of responsive slaves (only operate those that responded)
  numResponsiveSlaves = 0;
  for (int i = 0; i < NUM_SLAVES; i++) {
    if (slaveStateValid[i]) {
      responsiveSlaveIndices[numResponsiveSlaves++] = i;
    }
  }

  if (numResponsiveSlaves == 0) {
    Serial.println("Master: Cannot operate - no slaves responding");
    return;
  }

  Serial.printf("Master: Operating %d responsive window(s)\r\n", numResponsiveSlaves);

  // Same logic: if all responsive windows are closed, open all; otherwise close all
  bool allClosed = true;
  for (uint8_t j = 0; j < numResponsiveSlaves; j++) {
    int i = responsiveSlaveIndices[j];
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

  // Start with first responsive slave
  currentSlaveIndex = 0;
  systemState = MOVING_BLINDS;
  currentSlaveStart = millis();

  uint8_t slaveId = responsiveSlaveIndices[currentSlaveIndex] + 1;
  if (commandSlaveConfirmed(slaveId, (targetOpen ? CMD_OPEN : CMD_CLOSE))) {
    Serial.printf("Master: Moving slave %d\r\n", slaveId);
  } else {
    Serial.printf("Master: Slave %d did not confirm - aborting\r\n", slaveId);
    systemState = IDLE;
    currentSlaveIndex = 0;
  }
}

// Attempts to move a given subset of slaves (by 0-based index) to the target
// open/closed state: queries with retry, skips if already in the target
// state, otherwise starts the (sequential, one-at-a-time) motion chain,
// confirming the first slave's receipt of the command. `action` tags this
// attempt for markScheduledActionResult() to resolve later if the chain runs
// asynchronously (ATTEMPT_IN_PROGRESS) -- pass ACTION_NONE for manual/one-off
// operations that don't need a done-flag latched.
AttemptResult attemptOperationOnSlaves(const uint8_t* slaveIndices, uint8_t count, bool openBlinds, ScheduledAction action) {
  querySlavesWithRetries(COMMAND_RETRY_DELAY_MS, COMMAND_RETRY_COUNT);

  numResponsiveSlaves = 0;
  for (uint8_t j = 0; j < count; j++) {
    uint8_t i = slaveIndices[j];
    if (slaveStateValid[i]) {
      responsiveSlaveIndices[numResponsiveSlaves++] = i;
    }
  }

  if (numResponsiveSlaves == 0) {
    Serial.println("Master: Cannot run operation - no slaves responding");
    return ATTEMPT_FAILED;
  }

  // Check if all responsive blinds are already in the desired state
  bool allInTargetState = true;
  for (uint8_t j = 0; j < numResponsiveSlaves; j++) {
    int i = responsiveSlaveIndices[j];
    if (slaveStates[i] != openBlinds) {
      allInTargetState = false;
      break;
    }
  }

  if (allInTargetState) {
    Serial.printf("Master: All responsive blinds already %s - no action needed\r\n",
                  openBlinds ? "open" : "closed");
    return ATTEMPT_DONE;
  }

  Serial.printf("Master: Operation - %s %d blind(s)\r\n",
                openBlinds ? "opening" : "closing", numResponsiveSlaves);

  targetOpen = openBlinds;

  // Start with first responsive slave
  currentSlaveIndex = 0;
  systemState = MOVING_BLINDS;
  currentSlaveStart = millis();

  uint8_t slaveId = responsiveSlaveIndices[currentSlaveIndex] + 1;
  if (!commandSlaveConfirmed(slaveId, (targetOpen ? CMD_OPEN : CMD_CLOSE))) {
    Serial.printf("Master: Slave %d did not confirm - aborting operation\r\n", slaveId);
    systemState = IDLE;
    currentSlaveIndex = 0;
    return ATTEMPT_FAILED;
  }

  Serial.printf("Master: Moving slave %d\r\n", slaveId);
  g_activeScheduledAction = action;
  return ATTEMPT_IN_PROGRESS;
}

// Scheduled operation - opens or closes all responsive blinds to specified
// state. Used both by the scheduler (which passes a ScheduledAction to track)
// and directly by the WebSerial "open"/"close" (all) command (which doesn't).
AttemptResult startScheduledOperation(bool openBlinds, ScheduledAction action) {
  if (systemState != IDLE) {
    Serial.println("Master: Cannot start scheduled operation - system busy");
    return ATTEMPT_FAILED;
  }

  static const uint8_t allSlaves[NUM_SLAVES] = {0, 1, 2};
  return attemptOperationOnSlaves(allSlaves, NUM_SLAVES, openBlinds, action);
}

// Resolves the currently in-flight scheduled action (if any) once its whole
// chain finishes -- either every slave was confirmed commanded (success) or
// the chain was aborted (failure). No-op for manual operations, which never
// set g_activeScheduledAction in the first place.
void markScheduledActionResult(bool success) {
  if (g_activeScheduledAction == ACTION_NONE) return;

  const uint32_t retryAt = millis() + SCHEDULE_RETRY_INTERVAL_MS;
  switch (g_activeScheduledAction) {
    case ACTION_SUNRISE:
      if (success) g_sunriseActionDone = true; else g_sunriseRetryAt = retryAt;
      break;
    case ACTION_SUNSET:
      if (success) g_sunsetActionDone = true; else g_sunsetRetryAt = retryAt;
      break;
    case ACTION_MIDDAY_CLOSE:
      if (success) g_middayCloseDone = true; else g_middayCloseRetryAt = retryAt;
      break;
    case ACTION_MIDDAY_OPEN:
      if (success) g_middayOpenDone = true; else g_middayOpenRetryAt = retryAt;
      break;
    default:
      break;
  }

  Serial.printf("Master: Scheduled action %s\r\n", success ? "completed" : "FAILED - will retry in 60s");
  WebSerial.printf("Scheduled action %s\r\n", success ? "completed" : "FAILED - will retry in 60s");
  g_activeScheduledAction = ACTION_NONE;
}

void querySlaves() {
  Serial.println("Master: Querying all slaves...");

  for (int i = 0; i < NUM_SLAVES; i++) {
    uint8_t slave = i + 1;
    sendCommand(slave, CMD_QUERY);

    uint8_t data;
    if (waitForResponse(slave, RSP_STATE, data)) {
      slaveStates[i] = (data == 1);
      slaveStateValid[i] = true;
      Serial.printf("  Slave %d: %s\r\n", slave, (slaveStates[i] ? "OPEN" : "CLOSED"));
    } else {
      slaveStateValid[i] = false;
      Serial.printf("  Slave %d: No response\r\n", slave);
    }
  }
}

// Query slaves, then retry after delay for any that didn't respond (e.g. slow boot)
void querySlavesWithRetries(unsigned long delayMs, uint8_t maxRetries) {
  querySlaves();
  for (uint8_t r = 0; r < maxRetries; r++) {
    bool anyInvalid = false;
    for (int i = 0; i < NUM_SLAVES; i++) {
      if (!slaveStateValid[i]) {
        anyInvalid = true;
        break;
      }
    }
    if (!anyInvalid) break;
    Serial.printf("Master: Retrying query in %lu ms for unresponsive slave(s)...\r\n", (unsigned long)delayMs);
    delay(delayMs);
    querySlaves();
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
            slaveStateValid[slave-1] = true;
          }

          // If we're in moving state and this is the slave we're waiting for
          if (systemState == MOVING_BLINDS && currentSlaveIndex < numResponsiveSlaves &&
              slave == (responsiveSlaveIndices[currentSlaveIndex] + 1)) {
            // Move to next responsive slave
            currentSlaveIndex++;

            if (currentSlaveIndex >= numResponsiveSlaves) {
              // All responsive slaves done
              Serial.println("Master: All operations complete");
              systemState = IDLE;
              currentSlaveIndex = 0;
              markScheduledActionResult(true);
            } else {
              // Start next responsive slave
              currentSlaveStart = millis();
              uint8_t nextSlaveId = responsiveSlaveIndices[currentSlaveIndex] + 1;
              if (commandSlaveConfirmed(nextSlaveId, (targetOpen ? CMD_OPEN : CMD_CLOSE))) {
                Serial.printf("Master: Moving slave %d\r\n", nextSlaveId);
              } else {
                Serial.printf("Master: Slave %d did not confirm - aborting operation\r\n", nextSlaveId);
                systemState = IDLE;
                currentSlaveIndex = 0;
                markScheduledActionResult(false);
              }
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
// SCHEDULING FUNCTIONALITY
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

  // Reset flags (and any pending retries) at midnight
  if (g_currentTime < MINUTE) { // Within the first minute of the day
    g_sunriseActionDone = false;
    g_sunsetActionDone = false;
    g_middayCloseDone = false;
    g_middayOpenDone = false;
    g_sunriseRetryAt = 0;
    g_sunsetRetryAt = 0;
    g_middayCloseRetryAt = 0;
    g_middayOpenRetryAt = 0;
  }

  const uint32_t nowMs{millis()};

  // Check for sunrise action (sunrise + offset, clamped to MIN_SUNRISE_ACTION_TIME)
  int32_t sunriseActionTime{sunriseActionTimeClamped()};

  // Validate the calculated time
  if (sunriseActionTime < 0) sunriseActionTime = 0;
  if (sunriseActionTime >= DAY) sunriseActionTime = DAY - 1;

  if (!g_sunriseActionDone && g_currentTime >= sunriseActionTime && nowMs >= g_sunriseRetryAt) {
    if (AUTO_OPEN) {
      const AttemptResult result{sunriseAction()};
      if (result == ATTEMPT_DONE) g_sunriseActionDone = true;
      else if (result == ATTEMPT_FAILED) g_sunriseRetryAt = nowMs + SCHEDULE_RETRY_INTERVAL_MS;
      // ATTEMPT_IN_PROGRESS resolves later via markScheduledActionResult()
    } else {
      g_sunriseActionDone = true;  // AUTO_OPEN disabled for this room -- nothing to retry
    }
  }

  // Check for sunset action (sunset - offset)
  int32_t sunsetActionTime{g_sunset - SUNSET_ACTION_OFFSET};

  // Validate the calculated time
  if (sunsetActionTime < 0) sunsetActionTime = 0;
  if (sunsetActionTime >= DAY) sunsetActionTime = DAY - 1;

  if (!g_sunsetActionDone && g_currentTime >= sunsetActionTime && nowMs >= g_sunsetRetryAt) {
    const AttemptResult result{sunsetAction()};
    if (result == ATTEMPT_DONE) g_sunsetActionDone = true;
    else if (result == ATTEMPT_FAILED) g_sunsetRetryAt = nowMs + SCHEDULE_RETRY_INTERVAL_MS;
  }

  // Bedroom fixed-time actions: close window 3 at 10:45 AM, re-open at 12:45 PM
  if (ACTIVE_ROOM == BEDROOM) {
    static const uint8_t window3[1] = {2};  // slave index 2 = window 3

    // 10:45 AM: if all three windows are open, close window 3
    if (!g_middayCloseDone && g_currentTime >= 10 * HOUR + 45 * 60 && nowMs >= g_middayCloseRetryAt) {
      querySlavesWithRetries(COMMAND_RETRY_DELAY_MS, COMMAND_RETRY_COUNT);
      if (slaveStateValid[0] && slaveStateValid[1] && slaveStateValid[2] &&
          slaveStates[0] && slaveStates[1] && slaveStates[2]) {
        Serial.println("MIDDAY CLOSE: Closing window 3 (sun protection)");
        WebSerial.println("MIDDAY CLOSE: Closing window 3 (sun protection)");
        const AttemptResult result{attemptOperationOnSlaves(window3, 1, false, ACTION_MIDDAY_CLOSE)};
        if (result == ATTEMPT_DONE) g_middayCloseDone = true;
        else if (result == ATTEMPT_FAILED) g_middayCloseRetryAt = nowMs + SCHEDULE_RETRY_INTERVAL_MS;
      } else {
        // Precondition not met (not all three responding/open yet) -- try again shortly
        g_middayCloseRetryAt = nowMs + SCHEDULE_RETRY_INTERVAL_MS;
      }
    }

    // 12:45 PM: if windows 1 & 2 are open and window 3 is closed, re-open window 3
    if (!g_middayOpenDone && g_currentTime >= 12 * HOUR + 45 * 60 && nowMs >= g_middayOpenRetryAt) {
      querySlavesWithRetries(COMMAND_RETRY_DELAY_MS, COMMAND_RETRY_COUNT);
      if (slaveStateValid[0] && slaveStateValid[1] && slaveStateValid[2] &&
          slaveStates[0] && slaveStates[1] && !slaveStates[2]) {
        Serial.println("MIDDAY OPEN: Re-opening window 3");
        WebSerial.println("MIDDAY OPEN: Re-opening window 3");
        const AttemptResult result{attemptOperationOnSlaves(window3, 1, true, ACTION_MIDDAY_OPEN)};
        if (result == ATTEMPT_DONE) g_middayOpenDone = true;
        else if (result == ATTEMPT_FAILED) g_middayOpenRetryAt = nowMs + SCHEDULE_RETRY_INTERVAL_MS;
      } else {
        g_middayOpenRetryAt = nowMs + SCHEDULE_RETRY_INTERVAL_MS;
      }
    }
  }
}

// Action to perform after sunrise + offset
AttemptResult sunriseAction() {
  char timeStr[9];
  formatTimeOfDay(g_currentTime, timeStr);
  Serial.print("SUNRISE ACTION TRIGGERED at ");
  Serial.println(timeStr);
  WebSerial.println(String("SUNRISE ACTION TRIGGERED at ") + timeStr);

  digitalWrite(LED_PIN, LED_ON);
  return startScheduledOperation(true, ACTION_SUNRISE);  // Open all blinds
}

// Action to perform before sunset - offset
AttemptResult sunsetAction() {
  char timeStr[9];
  formatTimeOfDay(g_currentTime, timeStr);
  Serial.print("SUNSET ACTION TRIGGERED at ");
  Serial.println(timeStr);
  WebSerial.println(String("SUNSET ACTION TRIGGERED at ") + timeStr);

  digitalWrite(LED_PIN, LED_OFF);
  return startScheduledOperation(false, ACTION_SUNSET);  // Close all blinds
}

void printTimes() {
  char currentStr[9], sunriseStr[9], sunsetStr[9];
  char sunriseActionStr[9], sunsetActionStr[9];

  formatTimeOfDay(g_currentTime, currentStr);
  formatTimeOfDay(g_sunrise, sunriseStr);
  formatTimeOfDay(g_sunset, sunsetStr);

  // Format action times
  int32_t sunriseActionTime{sunriseActionTimeClamped()};
  int32_t sunsetActionTime{g_sunset - SUNSET_ACTION_OFFSET};
  formatTimeOfDay(sunriseActionTime, sunriseActionStr);
  formatTimeOfDay(sunsetActionTime, sunsetActionStr);

  // Check if it's daytime
  const bool isDaytime{(g_currentTime >= g_sunrise && g_currentTime < g_sunset)};

  String msg;
  msg += currentStr;
  msg += " | rise ";
  msg += sunriseStr;
  msg += " (open ";
  msg += sunriseActionStr;
  msg += ") | set ";
  msg += sunsetStr;
  msg += " (close ";
  msg += sunsetActionStr;
  msg += ") | ";
  msg += isDaytime ? "Day" : "Night";

  Serial.println(msg);
  WebSerial.println(msg);
}

void formatTimeOfDay(uint32_t timeOfDay, char* buffer) {
  const int hours{static_cast<int>(timeOfDay / 3600)};
  const int minutes{static_cast<int>((timeOfDay % 3600) / 60)};
  const int seconds{static_cast<int>(timeOfDay % 60)};

  sprintf(buffer, "%02d:%02d:%02d", hours, minutes, seconds);
}

// End of file /////////////////////////////////////////////////////////////////
