/*
 * ESP32-C3 RS485 Controller - Window Blind Controller with Scheduling
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

// Function prototypes
void buttonAction();
void buttonLongPressAction();
bool operateActuators(const uint8_t* actuatorIndices, uint8_t count, bool openBlinds);
bool commandActuatorReliable(uint8_t actuator, uint8_t cmd, bool& outOpen);
void queryActuators();
void queryActuatorsWithRetries(unsigned long delayMs, uint8_t maxRetries);
bool sendConfig(uint8_t actuator, const ConfigPacket& config);
bool sendConfigRetried(uint8_t actuator, const ConfigPacket& config);
bool waitForResponse(uint8_t actuator, uint8_t expectedCmd, uint8_t &data, uint32_t timeoutMs = RX_TIMEOUT);
void sendCommand(uint8_t actuator, uint8_t cmd);
bool sendCommandConfirmed(uint8_t actuator, uint8_t cmd);
void printTimes();
void formatTimeOfDay(uint32_t timeOfDay, char* buffer);
void checkTimeActions();
void fetchGeoBlocking();
bool syncTimeAndSun();
void onWebSerialMessage(uint8_t* data, size_t len);
const char* rs485CmdName(uint8_t cmd);
void logRs485Traffic(const char* direction, uint8_t actuator, uint8_t cmd, int data);
void logRs485ConfigTx(uint8_t actuator, const ConfigPacket& config);
void flushRs485TrafficLog();

// HTTP server on configured port (URL: http://<MDNS_HOSTNAME>.local/)
AsyncWebServer server(SERVER_PORT);

// RS485 Communication
HardwareSerial rs485(0);

// True while a multi-step RS485 exchange (query/config/command + response)
// is in flight. The RS485 bus and DE/RE pin are shared, single-outstanding-
// request resources, and WebSerial's message callback can fire from a
// separate task (AsyncTCP) concurrently with the main loop -- this flag is
// the guard that keeps two exchanges from interleaving on the wire.
bool busy{false};

bool actuatorStates[NUM_ACTUATORS] = {false};  // false=closed, true=open
bool actuatorStateValid[NUM_ACTUATORS] = {false};  // Track if we have valid state data

// Reusable actuator-index lists for the different operations below.
static const uint8_t kAllActuators[NUM_ACTUATORS]       = {0, 1, 2};
static const uint8_t kSkipWindow3[NUM_ACTUATORS - 1] = {0, 1};
static const uint8_t kWindow3Only[1]              = {2};

// Button instance (short press = toggle all; long press = toggle third window only)
Button button(PIN_BTN, BUTTON_DEBOUNCE_MS, BUTTON_SHORT_PRESS_MS, BUTTON_LONG_PRESS_MS,
              Button::ACTIVE_LOW, buttonAction, buttonLongPressAction);

// Global variables for storing the time data
int32_t g_currentTime{0};  // Current time in seconds since midnight
int32_t g_sunrise{0};      // Sunrise in seconds since midnight
int32_t g_sunset{0};       // Sunset in seconds since midnight

uint32_t lastPrintTime{0};

// RS485 traffic debug log -- see logRs485Traffic()/flushRs485TrafficLog()
char     g_trafficLogBuf[TRAFFIC_LOG_BUF_SIZE];
size_t   g_trafficLogLen{0};
uint32_t g_lastTrafficFlush{0};

// Single config for all actuators (from config.h; never changes at runtime)
static const ConfigPacket kActuatorConfig = {
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

// True during the bedroom's midday sun-protection window -- window 3 should
// be left closed rather than opened here, since opening it would just have
// it closed again minutes later by checkTimeActions().
static inline bool bedroomMiddayCloseActive() {
  return ACTIVE_ROOM == BEDROOM &&
         g_currentTime >= MIDDAY_CLOSE_TIME && g_currentTime < MIDDAY_OPEN_TIME;
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
  static const char* actuatorNames[] = {"one", "two", "three"};
  int targetActuator = 0;  // 0 = all, 1-3 = specific actuator
  String baseCmd = cmd;
  int dashPos = cmd.indexOf("--");
  if (dashPos >= 0) {
    String flag = cmd.substring(dashPos + 2);
    flag.trim();
    baseCmd = cmd.substring(0, dashPos);
    baseCmd.trim();
    for (int i = 0; i < NUM_ACTUATORS; i++) {
      if (flag == actuatorNames[i]) {
        targetActuator = i + 1;
        break;
      }
    }
    if (targetActuator == 0) {
      WebSerial.println("Unknown window. Use --one, --two, or --three.");
      return;
    }
  }

  if (baseCmd == "open" || baseCmd == "close") {
    if (busy) {
      WebSerial.println("System busy.");
      return;
    }
    const bool opening = (baseCmd == "open");
    busy = true;
    if (targetActuator > 0) {
      WebSerial.printf("%s window %s...\n", opening ? "Opening" : "Closing", actuatorNames[targetActuator - 1]);
      const uint8_t idx[1] = {static_cast<uint8_t>(targetActuator - 1)};
      if (!operateActuators(idx, 1, opening)) {
        WebSerial.printf("Window %s did not confirm.\n", actuatorNames[targetActuator - 1]);
      }
    } else {
      WebSerial.printf("%s all blinds...\n", opening ? "Opening" : "Closing");
      operateActuators(kAllActuators, NUM_ACTUATORS, opening);
    }
    busy = false;
  } else if (baseCmd == "half") {
    uint8_t actuator = targetActuator > 0 ? targetActuator : 3;
    WebSerial.printf("Closing window %s halfway...\n", actuatorNames[actuator - 1]);
    sendConfigRetried(actuator, kActuatorConfig);
    sendCommand(actuator, CMD_CLOSE_HALF);
  } else if (baseCmd == "status") {
    WebSerial.printf("Busy=%d Blinds=[", busy);
    for (int i = 0; i < NUM_ACTUATORS; i++) {
      if (actuatorStateValid[i]) {
        WebSerial.print(actuatorStates[i] ? "OPEN" : "CLOSED");
      } else {
        WebSerial.print("?");
      }
      if (i < (NUM_ACTUATORS - 1)) WebSerial.print(",");
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

// Button callback - short press: toggle all blinds (any open -> close all,
// all closed -> open all).
void buttonAction() {
  if (busy) {
    Serial.println("Controller: Button pressed but system busy - ignoring");
    return;
  }
  Serial.println("Controller: Manual button pressed");
  queryActuators();

  bool anyOpen = false;
  bool anyValid = false;
  for (int i = 0; i < NUM_ACTUATORS; i++) {
    if (actuatorStateValid[i]) {
      anyValid = true;
      if (actuatorStates[i]) anyOpen = true;
    }
  }
  if (!anyValid) {
    Serial.println("Controller: Cannot operate - no actuators responding");
    return;
  }

  const bool openTarget = !anyOpen;

  busy = true;
  // Bedroom sun-protection: leave window 3 closed during the midday window
  // instead of opening it just to have it auto-close again minutes later.
  // (Long press still lets the user force it open.)
  if (openTarget && bedroomMiddayCloseActive()) {
    Serial.println("Controller: Midday window active - leaving window 3 closed");
    operateActuators(kSkipWindow3, NUM_ACTUATORS - 1, openTarget);
  } else {
    Serial.printf("Controller: %s all\r\n", openTarget ? "Opening" : "Closing");
    operateActuators(kAllActuators, NUM_ACTUATORS, openTarget);
  }
  busy = false;
}

// Button callback - long press: cycle through window states
// All closed -> open 1 & 2; All open -> close 3; 1 & 2 open, 3 closed -> open 3
void buttonLongPressAction() {
  if (busy) {
    Serial.println("Controller: Long press ignored - system busy");
    return;
  }

  queryActuators();

  if (!actuatorStateValid[0] || !actuatorStateValid[1] || !actuatorStateValid[2]) {
    Serial.println("Controller: Long press - not all actuators responding");
    return;
  }

  const bool open1{actuatorStates[0]};
  const bool open2{actuatorStates[1]};
  const bool open3{actuatorStates[2]};

  busy = true;
  if (!open1 && !open2 && !open3) {
    Serial.println("Controller: Long press - all closed, opening 1 & 2");
    operateActuators(kSkipWindow3, NUM_ACTUATORS - 1, true);
  } else if (open1 && open2 && open3) {
    Serial.println("Controller: Long press - all open, closing 3");
    operateActuators(kWindow3Only, 1, false);
  } else if (open1 && open2 && !open3) {
    Serial.println("Controller: Long press - 1 & 2 open, opening 3");
    operateActuators(kWindow3Only, 1, true);
  } else {
    Serial.println("Controller: Long press - mixed state, no action");
  }
  busy = false;
}

// ============================================================================
// Setup
// ============================================================================

void setup() {
  Serial.begin(SERIAL_BAUD_RATE);
  Serial.println("\r\n\r\nRS485 Controller with Scheduling");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LED_OFF);

  // Initialize button
  button.begin();

  // Initialize RS485
  pinMode(RS485_DE_RE, OUTPUT);
  digitalWrite(RS485_DE_RE, LOW);  // Receive mode
  rs485.begin(9600, SERIAL_8N1, RS485_RX, RS485_TX);

  delay(1000);

  // Initial actuator query with retries so slow-to-boot actuators get a chance to respond
  queryActuatorsWithRetries(2000, 2);

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

  // Record today's date so the daily-refresh window doesn't refire until
  // tomorrow. Blind position vs. target is entirely time-of-day driven (see
  // checkTimeActions()), so there's no "already ran today" state to set up
  // here -- the first loop() tick figures out where the blinds should be.
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

    // Drive blinds toward wherever they should currently be
    checkTimeActions();
  }

  // Status tick: poll actuators and print combined status
  if (now - lastPrintTime >= STATUS_INTERVAL_MS) {
    lastPrintTime = now;

    // Poll actuator state when idle (avoid interfering with active operations)
    if (!busy) {
      queryActuators();
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
    for (int i = 0; i < NUM_ACTUATORS; i++) {
      if (actuatorStateValid[i]) {
        msg += actuatorStates[i] ? "O" : "C";
      } else {
        msg += "?";
      }
      if (i < (NUM_ACTUATORS - 1)) msg += ",";
    }
    msg += "]";

    if (busy) {
      msg += " MOVING";
    }

    Serial.println(msg);
    WebSerial.println(msg);
  }

  // RS485 traffic debug log: independent 1s tick, only published when
  // there was traffic in that window (separate from the 10s status tick above)
  if (now - g_lastTrafficFlush >= TRAFFIC_LOG_INTERVAL_MS) {
    g_lastTrafficFlush = now;
    flushRs485TrafficLog();
  }

  delay(TICK_PERIOD);
}

// ============================================================================
// RS485 traffic debug log
// ============================================================================
// Caches every RS485 message sent/received by the controller (independent of the
// regular per-actuator Serial/WebSerial logging already scattered through the
// send/receive functions) and flushes it to WebSerial as one human-readable
// block on a fixed interval, only when non-empty. Purely for debugging --
// does not affect any control-flow decision.

const char* rs485CmdName(uint8_t cmd) {
  switch (cmd) {
    case CMD_QUERY:       return "QUERY";
    case CMD_OPEN:        return "OPEN";
    case CMD_CLOSE:       return "CLOSE";
    case CMD_CLOSE_HALF:  return "CLOSE_HALF";
    case CMD_SET_CONFIG:  return "SET_CONFIG";
    case RSP_STATE:       return "RSP_STATE";
    case RSP_COMPLETE:    return "RSP_COMPLETE";
    case RSP_CONFIG_ACK:  return "RSP_CONFIG_ACK";
    case RSP_COMMAND_ACK: return "RSP_COMMAND_ACK";
    default:               return "UNKNOWN";
  }
}

// Appends one line to the cache. `data` is the packet's data byte, or -1 for
// packets that don't carry one (e.g. the CMD_OPEN/CMD_CLOSE request itself).
// Silently drops the line if the buffer is full -- this is a debug aid, not
// something that should ever affect control flow or block on space.
void logRs485Traffic(const char* direction, uint8_t actuator, uint8_t cmd, int data) {
  char line[64];
  int n = (data >= 0)
    ? snprintf(line, sizeof(line), "[%8lu] %s actuator=%u %s data=%d\n",
               (unsigned long)millis(), direction, actuator, rs485CmdName(cmd), data)
    : snprintf(line, sizeof(line), "[%8lu] %s actuator=%u %s\n",
               (unsigned long)millis(), direction, actuator, rs485CmdName(cmd));
  if (n <= 0 || (g_trafficLogLen + (size_t)n) >= TRAFFIC_LOG_BUF_SIZE) return;
  memcpy(g_trafficLogBuf + g_trafficLogLen, line, n);
  g_trafficLogLen += n;
  g_trafficLogBuf[g_trafficLogLen] = '\0';
}

// CMD_SET_CONFIG carries a ConfigPacket rather than a single data byte, so it
// gets its own formatting.
void logRs485ConfigTx(uint8_t actuator, const ConfigPacket& config) {
  char line[96];
  int n = snprintf(line, sizeof(line), "[%8lu] TX actuator=%u SET_CONFIG len=%.1f openSpd=%.0f closeSpd=%.0f\n",
                    (unsigned long)millis(), actuator, config.motion_length,
                    config.motor_speed_open, config.motor_speed_close);
  if (n <= 0 || (g_trafficLogLen + (size_t)n) >= TRAFFIC_LOG_BUF_SIZE) return;
  memcpy(g_trafficLogBuf + g_trafficLogLen, line, n);
  g_trafficLogLen += n;
  g_trafficLogBuf[g_trafficLogLen] = '\0';
}

// Publishes the cached traffic as one block and clears it -- but only if
// there's anything to publish. Called on its own fixed interval from loop(),
// independent of the regular STATUS_INTERVAL_MS status tick.
void flushRs485TrafficLog() {
  if (g_trafficLogLen == 0) return;
  WebSerial.println("--- RS485 traffic (last 1s) ---");
  WebSerial.println(g_trafficLogBuf);
  g_trafficLogLen = 0;
  g_trafficLogBuf[0] = '\0';
}

// ============================================================================
// RS485 Communication
// ============================================================================

bool sendConfig(uint8_t actuator, const ConfigPacket& config) {
  logRs485ConfigTx(actuator, config);
  digitalWrite(RS485_DE_RE, HIGH);
  delayMicroseconds(10);

  // Pack floats into bytes (4 bytes each, now 12 bytes total)
  uint8_t buffer[17];  // START + ADDR + CMD + 12 bytes data + CHECKSUM + END
  buffer[0] = START_BYTE;
  buffer[1] = actuator;
  buffer[2] = CMD_SET_CONFIG;

  // Copy motion_length (4 bytes)
  memcpy(&buffer[3], &config.motion_length, sizeof(float));

  // Copy motor_speed_open (4 bytes)
  memcpy(&buffer[7], &config.motor_speed_open, sizeof(float));

  // Copy motor_speed_close (4 bytes)
  memcpy(&buffer[11], &config.motor_speed_close, sizeof(float));

  // Calculate checksum over all data bytes
  uint8_t checksum = START_BYTE ^ actuator ^ CMD_SET_CONFIG;
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
  return waitForResponse(actuator, RSP_CONFIG_ACK, dummy);
}

// Retries sendConfig() up to COMMAND_RETRY_COUNT times if it goes unacknowledged.
bool sendConfigRetried(uint8_t actuator, const ConfigPacket& config) {
  for (uint8_t attempt = 0; attempt < COMMAND_RETRY_COUNT; attempt++) {
    if (sendConfig(actuator, config)) return true;
    if (attempt + 1 < COMMAND_RETRY_COUNT) delay(COMMAND_RETRY_DELAY_MS);
  }
  Serial.printf("Controller: Actuator %d did not ack config after %d attempts\r\n", actuator, COMMAND_RETRY_COUNT);
  return false;
}

// Sends CMD_OPEN/CMD_CLOSE and waits for the actuator's immediate RSP_COMMAND_ACK
// (sent before motion starts), retrying the send up to COMMAND_RETRY_COUNT
// times if unacknowledged. This only confirms receipt -- motion completion
// is confirmed separately, by commandActuatorReliable() below.
bool sendCommandConfirmed(uint8_t actuator, uint8_t cmd) {
  for (uint8_t attempt = 0; attempt < COMMAND_RETRY_COUNT; attempt++) {
    sendCommand(actuator, cmd);
    uint8_t dummy;
    if (waitForResponse(actuator, RSP_COMMAND_ACK, dummy)) return true;
    if (attempt + 1 < COMMAND_RETRY_COUNT) delay(COMMAND_RETRY_DELAY_MS);
  }
  Serial.printf("Controller: Actuator %d did not confirm command 0x%02X after %d attempts\r\n",
                actuator, cmd, COMMAND_RETRY_COUNT);
  return false;
}

// The protocol-layer primitive: reliably gets a CMD_OPEN/CMD_CLOSE through to
// one actuator. Sends the config and command, retrying each until acknowledged,
// then waits for the actuator to report the motion actually finished. Returns
// whether the whole exchange succeeded and, if so, the blind's resulting
// position via outOpen. Callers never need to track retries or completion
// themselves -- this either comes back confirmed or it doesn't.
//
// No resending once the command is acked: per actuator.cpp, the actuator is fully
// synchronous while it moves and won't be listening for a resend anyway.
bool commandActuatorReliable(uint8_t actuator, uint8_t cmd, bool& outOpen) {
  if (!sendConfigRetried(actuator, kActuatorConfig)) return false;
  if (!sendCommandConfirmed(actuator, cmd)) return false;

  const uint32_t motionTimeoutMs{static_cast<uint32_t>(MOTION_TIMEOUT * 1000.0F) + 1000U};
  uint8_t data;
  if (!waitForResponse(actuator, RSP_COMPLETE, data, motionTimeoutMs)) {
    Serial.printf("Controller: Actuator %d did not report motion complete\r\n", actuator);
    return false;
  }
  outOpen = (data == 1);
  return true;
}

// Drives each of the given actuators (0-based indices) toward openBlinds,
// skipping any already known to be there. Safe to call every tick with the
// same target: once every actuator is confirmed in place it's a no-op, and a
// actuator commanded to a state it's already in just gets an immediate
// RSP_COMPLETE back from the actuator without moving (see actuator.cpp).
// Returns true only if every actuator that needed commanding confirmed it.
bool operateActuators(const uint8_t* actuatorIndices, uint8_t count, bool openBlinds) {
  bool allOk = true;
  for (uint8_t j = 0; j < count; j++) {
    const uint8_t i = actuatorIndices[j];
    if (actuatorStateValid[i] && actuatorStates[i] == openBlinds) continue;

    bool nowOpen;
    const uint8_t actuator = i + 1;
    if (commandActuatorReliable(actuator, openBlinds ? CMD_OPEN : CMD_CLOSE, nowOpen)) {
      actuatorStates[i] = nowOpen;
      actuatorStateValid[i] = true;
    } else {
      allOk = false;
    }
  }
  return allOk;
}

void queryActuators() {
  Serial.println("Controller: Querying all actuators...");

  for (int i = 0; i < NUM_ACTUATORS; i++) {
    uint8_t actuator = i + 1;
    sendCommand(actuator, CMD_QUERY);

    uint8_t data;
    if (waitForResponse(actuator, RSP_STATE, data)) {
      actuatorStates[i] = (data == 1);
      actuatorStateValid[i] = true;
      Serial.printf("  Actuator %d: %s\r\n", actuator, (actuatorStates[i] ? "OPEN" : "CLOSED"));
    } else {
      actuatorStateValid[i] = false;
      Serial.printf("  Actuator %d: No response\r\n", actuator);
    }
  }
}

// Query actuators, then retry after delay for any that didn't respond (e.g. slow boot)
void queryActuatorsWithRetries(unsigned long delayMs, uint8_t maxRetries) {
  queryActuators();
  for (uint8_t r = 0; r < maxRetries; r++) {
    bool anyInvalid = false;
    for (int i = 0; i < NUM_ACTUATORS; i++) {
      if (!actuatorStateValid[i]) {
        anyInvalid = true;
        break;
      }
    }
    if (!anyInvalid) break;
    Serial.printf("Controller: Retrying query in %lu ms for unresponsive actuator(s)...\r\n", (unsigned long)delayMs);
    delay(delayMs);
    queryActuators();
  }
}

bool waitForResponse(uint8_t actuator, uint8_t expectedCmd, uint8_t &data, uint32_t timeoutMs) {
  unsigned long start = millis();
  uint8_t rxBuffer[6];
  uint8_t rxIndex = 0;

  while ((millis() - start) < timeoutMs) {
    if (rs485.available()) {
      uint8_t b = rs485.read();

      if (rxIndex == 0 && b != START_BYTE) continue;

      rxBuffer[rxIndex++] = b;

      if (rxIndex == 6) {
        if (rxBuffer[5] == END_BYTE &&
            rxBuffer[1] == actuator &&
            rxBuffer[2] == expectedCmd) {
          uint8_t checksum = START_BYTE ^ actuator ^ expectedCmd ^ rxBuffer[3];
          if (rxBuffer[4] == checksum) {
            data = rxBuffer[3];
            logRs485Traffic("RX", actuator, expectedCmd, data);
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

void sendCommand(uint8_t actuator, uint8_t cmd) {
  logRs485Traffic("TX", actuator, cmd, -1);

  digitalWrite(RS485_DE_RE, HIGH);
  delayMicroseconds(10);

  uint8_t checksum = START_BYTE ^ actuator ^ cmd;

  rs485.write(START_BYTE);
  rs485.write(actuator);
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

// Drives the blinds toward wherever they should currently be, purely as a
// function of time of day. No done-flags, no retry timers: "should this
// actuator be open right now" is answered fresh every call by comparing the
// current time to the schedule, and operateActuators() only sends a command
// when the actuator's last-known state doesn't already match. Call this as
// often as you like -- once the blinds are where they belong, it's a no-op.
void checkTimeActions() {
  // Only proceed if we have valid sunrise/sunset data
  if (g_sunrise == 0 || g_sunset == 0 || g_currentTime == 0) {
    return;  // Don't spam warnings - just skip silently
  }

  if (busy) return;

  int32_t openTime{sunriseActionTimeClamped()};
  if (openTime < 0) openTime = 0;
  if (openTime >= DAY) openTime = DAY - 1;

  int32_t closeTime{g_sunset - SUNSET_ACTION_OFFSET};
  if (closeTime < 0) closeTime = 0;
  if (closeTime >= DAY) closeTime = DAY - 1;

  const bool shouldBeOpen{g_currentTime >= openTime && g_currentTime < closeTime};

  // LED indicates scheduled state: ON = blinds should be open
  digitalWrite(LED_PIN, shouldBeOpen ? LED_ON : LED_OFF);

  busy = true;

  if (shouldBeOpen && AUTO_OPEN) {
    // Bedroom sun-protection: don't open window 3 during the midday window
    // -- it would just be closed again minutes later by the block below.
    if (bedroomMiddayCloseActive()) {
      operateActuators(kSkipWindow3, NUM_ACTUATORS - 1, true);
    } else {
      operateActuators(kAllActuators, NUM_ACTUATORS, true);
    }
  } else if (!shouldBeOpen) {
    // Blinds always auto-close at the scheduled time regardless of AUTO_OPEN
    // -- only automatic opening is gated per-room.
    operateActuators(kAllActuators, NUM_ACTUATORS, false);
  }

  // Bedroom fixed-time sun protection: window 3 gets direct sun during
  // [MIDDAY_CLOSE_TIME, MIDDAY_OPEN_TIME), so it closes for that window and
  // reopens after, once 1 & 2 are confirmed open.
  if (ACTIVE_ROOM == BEDROOM) {
    if (bedroomMiddayCloseActive()) {
      operateActuators(kWindow3Only, 1, false);
    } else if (g_currentTime >= MIDDAY_OPEN_TIME &&
               actuatorStateValid[0] && actuatorStateValid[1] &&
               actuatorStates[0] && actuatorStates[1]) {
      operateActuators(kWindow3Only, 1, true);
    }
  }

  busy = false;
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
