/*
 * ESP32-C3 RS485 Controller - Window Blind Controller with Scheduling
 */

#include <Arduino.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <ESPAsyncWebServer.h>
#include <WebConsole.h>
#include <time.h>
#include "config.h"
#include "credentials.h"
#include "button.h"
#include "WebInfo.h"
#include "Solar.h"

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
void calculateSunSchedule();
void printMiddayTimes();
void onConsoleLine(const char* line, size_t len);
void runPendingCommand();
void handleCommand(String cmd);
const char* rs485CmdName(uint8_t cmd);
void logRs485Traffic(const char* direction, uint8_t actuator, uint8_t cmd, int data);
void logRs485ConfigTx(uint8_t actuator, const ConfigPacket& config);
void flushRs485TrafficLog();

// HTTP server on configured port (URL: http://<MDNS_HOSTNAME>.local/)
AsyncWebServer server(SERVER_PORT);

// RS485 Communication
HardwareSerial rs485(0);

// Browser console at http://<MDNS_HOSTNAME>.local/
WebConsole webConsole;

// The console's line handler runs on the AsyncTCP task. It only copies the
// line here; loop() runs it, so every RS485 exchange happens on the loop task
// and two can never interleave on the bus.
static portMUX_TYPE s_cmdMux = portMUX_INITIALIZER_UNLOCKED;
static char         s_pendingCmd[CONSOLE_CMD_MAX_LEN + 1];
static bool         s_hasPendingCmd{false};

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

// Site location, looked up from the public IP at power-on
float geoLat{NAN};
float geoLon{NAN};

// Midday sun-protection window, in local seconds since midnight.
// Calculated each morning from when the sun passes MIDDAY_CLOSE_AZIMUTH and
// MIDDAY_OPEN_AZIMUTH (rooms with MIDDAY_SUN_PROTECT only).
int32_t g_middayClose{0};
int32_t g_middayOpen {0};
bool    g_middayValid{false};

// Bed-occupancy open (rooms with BED_OCCUPANCY_OPEN) -- see config.h.
// g_bedOpenAt is the local time of day today's open was triggered (-1 = not
// yet); the open is then enforced for SCHEDULE_ACTION_WINDOW like the other
// scheduled actions. Reset by the daily refresh.
int32_t  g_bedOpenAt   {-1};
uint32_t lastBedPollMs {0};

unsigned long lastWifiCheckMs     {0};
int           lastDailyRefreshDay {-1};

// Sunrise action time, clamped to MIN_SUNRISE_ACTION_TIME (never before 7 AM).
static inline int32_t sunriseActionTimeClamped() {
  const int32_t t{g_sunrise + SUNRISE_ACTION_OFFSET};
  return (t < MIN_SUNRISE_ACTION_TIME) ? MIN_SUNRISE_ACTION_TIME : t;
}

// True during the midday sun-protection window -- window 3 should
// be left closed when opening blinds here (long press can still force it).
static inline bool middayCloseActive() {
  return MIDDAY_SUN_PROTECT && g_middayValid &&
         g_currentTime >= g_middayClose && g_currentTime < g_middayOpen;
}

// Asks the bed sensor (network) whether nobody's in bed now and the last 5+
// minute occupancy ended more than BED_EMPTY_BEFORE_OPEN ago -- or there's
// been none since the sensor booted (lastInBed 0). No response or a stale
// reading counts as "no". A sensor reboot under a sleeper also reads 0, but
// the occupied-now check still blocks that.
static bool bedEmptyLongEnough() {
  WebInfo::BedState bed;
  if (!WebInfo::fetchBedState(BED_OCCUPANCY_URL, BED_HTTP_TIMEOUT_MS, bed)) return false;
  const int64_t nowUtc{static_cast<int64_t>(time(nullptr))};
  const int64_t age{nowUtc - bed.time};
  if (age > BED_MAX_READING_AGE || age < -BED_MAX_READING_AGE) return false;
  return !bed.occupied &&
         (bed.lastInBed == 0 || nowUtc - bed.lastInBed > BED_EMPTY_BEFORE_OPEN);
}

// ============================================================================
// Sun schedule
// ============================================================================

// Look up site location from the public IP -- blocks with retries until
// success, since the sun calculations need it.
void fetchGeoBlocking() {
  while (!WebInfo::fetchGeo(geoLat, geoLon)) {
    Serial.println("Geo lookup failed, retrying in 5s...");
    delay(RETRY_DELAY_MS);
  }
  Serial.printf("Geo approx: %.*f, %.*f\n", GEO_COORD_DECIMALS, geoLat,
                GEO_COORD_DECIMALS, geoLon);
}

// Local seconds since midnight for a UTC instant.
static int32_t localSecondsOfDay(time_t when) {
  struct tm lt;
  localtime_r(&when, &lt);
  return lt.tm_hour * HOUR + lt.tm_min * MINUTE + lt.tm_sec * SECOND;
}

// Calculate today's sunrise/sunset (and, where MIDDAY_SUN_PROTECT is set, the
// midday window-3 close/open times) onboard from the clock and site location.
// Searches local midnight to the next local midnight, so DST-change days
// (23 or 25 hours long) are handled by the TZ rules. If an event can't be
// found (only possible near the poles) the previous values are kept.
void calculateSunSchedule() {
  const time_t now{time(nullptr)};
  struct tm lt;
  localtime_r(&now, &lt);
  lt.tm_hour  = 0;
  lt.tm_min   = 0;
  lt.tm_sec   = 0;
  lt.tm_isdst = -1;
  const time_t dayStart{mktime(&lt)};
  lt.tm_mday += 1;
  lt.tm_isdst = -1;
  const time_t dayEnd{mktime(&lt)};

  time_t rise, set;
  if (Solar::sunriseSunset(dayStart, dayEnd, geoLat, geoLon, rise, set)) {
    g_sunrise = localSecondsOfDay(rise);
    g_sunset  = localSecondsOfDay(set);
    Serial.printf("Sunrise %s, sunset %s\n",
                  WebInfo::formatLocalTime(g_sunrise).c_str(),
                  WebInfo::formatLocalTime(g_sunset).c_str());
  } else {
    Serial.println("Sunrise/sunset not found today, keeping previous times.");
  }

  if (MIDDAY_SUN_PROTECT) {
    time_t closeAt, openAt;
    g_middayValid =
      Solar::azimuthCrossing(dayStart, dayEnd, geoLat, geoLon,
                             MIDDAY_CLOSE_AZIMUTH, closeAt) &&
      Solar::azimuthCrossing(dayStart, dayEnd, geoLat, geoLon,
                             MIDDAY_OPEN_AZIMUTH, openAt) &&
      closeAt < openAt;
    if (g_middayValid) {
      g_middayClose = localSecondsOfDay(closeAt);
      g_middayOpen  = localSecondsOfDay(openAt);
    }
    printMiddayTimes();
  }
}

// ============================================================================
// Console commands
// ============================================================================

// AsyncTCP task: queue the line for loop(). Dropped if the previous one
// hasn't run yet or it's too long to be a command.
void onConsoleLine(const char* line, size_t len) {
  if (len > CONSOLE_CMD_MAX_LEN) return;
  portENTER_CRITICAL(&s_cmdMux);
  if (!s_hasPendingCmd) {
    memcpy(s_pendingCmd, line, len);
    s_pendingCmd[len] = '\0';
    s_hasPendingCmd = true;
  }
  portEXIT_CRITICAL(&s_cmdMux);
}

// Loop task: run the queued console command, if any.
void runPendingCommand() {
  char line[CONSOLE_CMD_MAX_LEN + 1];
  bool have{false};
  portENTER_CRITICAL(&s_cmdMux);
  if (s_hasPendingCmd) {
    memcpy(line, s_pendingCmd, sizeof(line));
    s_hasPendingCmd = false;
    have = true;
  }
  portEXIT_CRITICAL(&s_cmdMux);
  if (have) handleCommand(String(line));
}

void handleCommand(String cmd) {
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
      webConsole.println("Unknown window. Use --one, --two, or --three.");
      return;
    }
  }

  if (baseCmd == "open" || baseCmd == "close") {
    const bool opening = (baseCmd == "open");
    if (targetActuator > 0) {
      webConsole.printf("%s window %s...\n", opening ? "Opening" : "Closing", actuatorNames[targetActuator - 1]);
      const uint8_t idx[1] = {static_cast<uint8_t>(targetActuator - 1)};
      if (!operateActuators(idx, 1, opening)) {
        webConsole.printf("Window %s did not confirm.\n", actuatorNames[targetActuator - 1]);
      }
    } else {
      webConsole.printf("%s all blinds...\n", opening ? "Opening" : "Closing");
      operateActuators(kAllActuators, NUM_ACTUATORS, opening);
    }
  } else if (baseCmd == "half") {
    uint8_t actuator = targetActuator > 0 ? targetActuator : 3;
    webConsole.printf("Closing window %s halfway...\n", actuatorNames[actuator - 1]);
    sendConfigRetried(actuator, kActuatorConfig);
    sendCommand(actuator, CMD_CLOSE_HALF);
  } else if (baseCmd == "status") {
    webConsole.print("Blinds=[");
    for (int i = 0; i < NUM_ACTUATORS; i++) {
      if (actuatorStateValid[i]) {
        webConsole.print(actuatorStates[i] ? "OPEN" : "CLOSED");
      } else {
        webConsole.print("?");
      }
      if (i < (NUM_ACTUATORS - 1)) webConsole.print(",");
    }
    webConsole.println("]");
    webConsole.printf("Location %.*f, %.*f (from IP)\n", GEO_COORD_DECIMALS, geoLat,
                      GEO_COORD_DECIMALS, geoLon);
    printTimes();
  } else if (baseCmd == "help" || baseCmd == "?") {
    webConsole.println("Commands: open, close, half, status, help");
    webConsole.println("Add --one, --two, --three for individual windows.");
    webConsole.println("Example: open --two");
  } else if (baseCmd.length() > 0) {
    webConsole.println("Unknown command. Type 'help' for commands.");
  }
}

// ============================================================================
// Button callbacks
// ============================================================================

// Button callback - short press: toggle all blinds (any open -> close all,
// all closed -> open all).
void buttonAction() {
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

  // Sun protection: leave window 3 closed during the midday window.
  // (Long press still lets the user force it open.)
  if (openTarget && middayCloseActive()) {
    Serial.println("Controller: Midday window active - leaving window 3 closed");
    operateActuators(kSkipWindow3, NUM_ACTUATORS - 1, openTarget);
  } else {
    Serial.printf("Controller: %s all\r\n", openTarget ? "Opening" : "Closing");
    operateActuators(kAllActuators, NUM_ACTUATORS, openTarget);
  }
}

// Button callback - long press: cycle through window states
// All closed -> open 1 & 2; All open -> close 3; 1 & 2 open, 3 closed -> open 3
void buttonLongPressAction() {
  queryActuators();

  if (!actuatorStateValid[0] || !actuatorStateValid[1] || !actuatorStateValid[2]) {
    Serial.println("Controller: Long press - not all actuators responding");
    return;
  }

  const bool open1{actuatorStates[0]};
  const bool open2{actuatorStates[1]};
  const bool open3{actuatorStates[2]};

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
  Serial.println("Console: http://" + WiFi.localIP().toString() + "/");

  // Set the clock from NTP -- there's no battery-backed RTC, so the first
  // sync after boot is required. Retry until it succeeds.
  WebInfo::initTime(TIMEZONE_POSIX);
  while (!WebInfo::clockValid() && !WebInfo::syncClock(NTP_SYNC_TIMEOUT_MS)) {
    Serial.println("Time sync failed, retrying in 5s...");
    delay(RETRY_DELAY_MS);
  }

  // Look up location once at power-on (the house doesn't move)
  fetchGeoBlocking();

  // Sun times are calculated onboard -- no network needed
  calculateSunSchedule();

  // Record today's date so the daily-refresh window doesn't refire until
  // tomorrow. Scheduled actions are entirely time-of-day driven (see
  // checkTimeActions()), so there's no "already ran today" state to set up
  // here -- the first loop() tick enforces whichever action window (if any)
  // is currently open.
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
  Serial.println("Automatic scheduling based on onboard sun calculations active.");

  webConsole.begin(server, "/", onConsoleLine);
  server.begin();

  webConsole.println("Console ready.");
  webConsole.println(String("URL: http://") + MDNS_HOSTNAME + ".local/");
  webConsole.println("Alt: http://" + WiFi.localIP().toString() + "/");
  webConsole.println("Commands: open, close, half, status, help");
  printMiddayTimes();

  Serial.println("Ready. Manual button control available.");
}

// ============================================================================
// Main loop
// ============================================================================

void loop() {
  const unsigned long now{millis()};

  // Run any command typed in the browser console
  runPendingCommand();

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

    // Daily refresh: correct clock drift from NTP, then calculate today's
    // sun schedule. An NTP failure isn't fatal -- the clock keeps running
    // from its last sync (drift is well under a second per day) and the TZ
    // rules keep DST correct, so the schedule is calculated either way.
    if (timeinfo.tm_hour >= DAILY_REFRESH_HOUR &&
        timeinfo.tm_mday != lastDailyRefreshDay) {
      Serial.println("Daily refresh...");
      lastDailyRefreshDay = timeinfo.tm_mday;
      if (WebInfo::syncClock(NTP_SYNC_TIMEOUT_MS)) {
        Serial.println("Clock synced from NTP.");
        // Re-read time so subsequent logic uses the fresh sync
        if (getLocalTime(&timeinfo)) {
          g_currentTime = (timeinfo.tm_hour * HOUR) +
                          (timeinfo.tm_min * MINUTE) +
                          timeinfo.tm_sec * SECOND;
        }
      } else {
        Serial.println("NTP sync failed, using onboard clock.");
      }
      calculateSunSchedule();
      g_bedOpenAt = -1;  // new day: bed-occupancy open can trigger again
    }

    // Drive blinds toward wherever they should currently be
    checkTimeActions();
  }

  // Status tick: poll actuators and print combined status
  if (now - lastPrintTime >= STATUS_INTERVAL_MS) {
    lastPrintTime = now;

    // Poll actuator state
    queryActuators();

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

    if (MIDDAY_SUN_PROTECT) {
      msg += " | midday ";
      if (g_middayValid) {
        char middayCloseStr[9], middayOpenStr[9];
        formatTimeOfDay(g_middayClose, middayCloseStr);
        formatTimeOfDay(g_middayOpen,  middayOpenStr);
        msg += middayCloseStr;
        msg += "-";
        msg += middayOpenStr;
      } else {
        msg += "off";
      }
    }

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

    Serial.println(msg);
    webConsole.println(msg);
  }

  // RS485 traffic debug log: independent 1s tick, only published when
  // there was traffic in that window (separate from the 10s status tick above)
  if (now - g_lastTrafficFlush >= TRAFFIC_LOG_INTERVAL_MS) {
    g_lastTrafficFlush = now;
    flushRs485TrafficLog();
  }

  // Send this tick's console output to the browser
  webConsole.loop();

  delay(TICK_PERIOD);
}

// ============================================================================
// RS485 traffic debug log
// ============================================================================
// Caches every RS485 message sent/received by the controller (independent of the
// regular per-actuator Serial/console logging already scattered through the
// send/receive functions) and flushes it to the console as one human-readable
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
  webConsole.println("--- RS485 traffic (last 1s) ---");
  webConsole.println(g_trafficLogBuf);
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

// True for SCHEDULE_ACTION_WINDOW starting at the given time of day.
static inline bool inActionWindow(int32_t startTime) {
  return g_currentTime >= startTime &&
         g_currentTime < startTime + SCHEDULE_ACTION_WINDOW;
}

// Drives the blinds toward their scheduled state, purely as a function of
// time of day. No done-flags, no retry timers: each scheduled action is
// enforced only during its SCHEDULE_ACTION_WINDOW, and operateActuators()
// only sends a command when the actuator's last-known state doesn't already
// match -- so actuators that miss a command (e.g. brief RS485 dropout) are
// retried every call until the window ends, and outside the windows manual
// overrides are left alone.
void checkTimeActions() {
  // Only proceed if we have valid sunrise/sunset data
  if (g_sunrise == 0 || g_sunset == 0 || g_currentTime == 0) {
    return;  // Don't spam warnings - just skip silently
  }

  int32_t openTime{sunriseActionTimeClamped()};
  if (openTime < 0) openTime = 0;
  if (openTime >= DAY) openTime = DAY - 1;

  int32_t closeTime{g_sunset - SUNSET_ACTION_OFFSET};
  if (closeTime < 0) closeTime = 0;
  if (closeTime >= DAY) closeTime = DAY - 1;

  const bool shouldBeOpen{g_currentTime >= openTime && g_currentTime < closeTime};

  // LED indicates scheduled state: ON = blinds should be open
  digitalWrite(LED_PIN, shouldBeOpen ? LED_ON : LED_OFF);

  // Bed-occupancy open: trigger once per day, the first time every condition
  // holds. Onboard checks first; the sensor is only asked once they all pass.
  if (BED_OCCUPANCY_OPEN && g_bedOpenAt < 0 &&
      g_currentTime >= openTime && g_currentTime < closeTime &&
      !middayCloseActive() &&
      millis() - lastBedPollMs >= BED_POLL_INTERVAL_MS) {
    lastBedPollMs = millis();
    if (bedEmptyLongEnough()) {
      g_bedOpenAt = g_currentTime;
      Serial.println("Bed empty long enough -- opening blinds");
      webConsole.println("Bed empty long enough -- opening blinds");
    }
  }
  const bool bedOpenDue{g_bedOpenAt >= 0 && inActionWindow(g_bedOpenAt)};

  if ((AUTO_OPEN && inActionWindow(openTime)) || bedOpenDue) {
    // Sun protection: don't open window 3 during the midday window.
    if (middayCloseActive()) {
      operateActuators(kSkipWindow3, NUM_ACTUATORS - 1, true);
    } else {
      operateActuators(kAllActuators, NUM_ACTUATORS, true);
    }
  } else if (inActionWindow(closeTime)) {
    // Blinds always auto-close at the scheduled time regardless of AUTO_OPEN
    // -- only automatic opening is gated per-room.
    operateActuators(kAllActuators, NUM_ACTUATORS, false);
  }

  // Sun protection: window 3 gets direct sun while the sun is
  // between MIDDAY_CLOSE_AZIMUTH and MIDDAY_OPEN_AZIMUTH, so it closes when
  // the sun reaches the first and reopens at the second, once 1 & 2 are
  // confirmed open.
  if (MIDDAY_SUN_PROTECT && g_middayValid) {
    if (inActionWindow(g_middayClose)) {
      operateActuators(kWindow3Only, 1, false);
    } else if (inActionWindow(g_middayOpen) &&
               actuatorStateValid[0] && actuatorStateValid[1] &&
               actuatorStates[0] && actuatorStates[1]) {
      operateActuators(kWindow3Only, 1, true);
    }
  }
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
  webConsole.println(msg);
  printMiddayTimes();
}

// Publish today's midday window-3 close/open times to Serial and webConsole.
// (Console output is dropped while no browser is connected.)
void printMiddayTimes() {
  if (!MIDDAY_SUN_PROTECT) return;
  char msg[96];
  if (g_middayValid) {
    char closeStr[9], openStr[9];
    formatTimeOfDay(g_middayClose, closeStr);
    formatTimeOfDay(g_middayOpen,  openStr);
    snprintf(msg, sizeof(msg), "Midday window 3: close %s, open %s", closeStr, openStr);
  } else {
    snprintf(msg, sizeof(msg), "Midday window 3: off today");
  }
  Serial.println(msg);
  webConsole.println(msg);
}

void formatTimeOfDay(uint32_t timeOfDay, char* buffer) {
  const int hours{static_cast<int>(timeOfDay / 3600)};
  const int minutes{static_cast<int>((timeOfDay % 3600) / 60)};
  const int seconds{static_cast<int>(timeOfDay % 60)};

  sprintf(buffer, "%02d:%02d:%02d", hours, minutes, seconds);
}

// End of file /////////////////////////////////////////////////////////////////
