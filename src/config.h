// config.h ////////////////////////////////////////////////////////////////////

#pragma once

#include <Arduino.h>

// Time units //////////////////////////////////////////////////////////////////

constexpr int32_t SECOND{1};
constexpr int32_t MINUTE{60 * SECOND};
constexpr int32_t HOUR{60 * MINUTE};
constexpr int32_t DAY{24 * HOUR};

// Room configuration //////////////////////////////////////////////////////////

enum Room : uint8_t { BEDROOM, LIVINGROOM, DEMO };

struct RoomConfig {
  const char* mdnsHostname;
  bool        autoOpen;
};

constexpr RoomConfig ROOM_CONFIGS[] = {
  {"bedroomblinds",    false},  // BEDROOM
  {"livingroomblinds", true},   // LIVINGROOM
  {"demoblinds",       true},   // DEMO
};

// Room is selected per build environment in platformio.ini (-DBLINDS_ROOM=...),
// e.g. `pio run -e living_room_controller`. Actuator builds don't need one.
#ifdef BLINDS_ROOM
constexpr Room ACTIVE_ROOM {BLINDS_ROOM};

constexpr bool        AUTO_OPEN     {ROOM_CONFIGS[ACTIVE_ROOM].autoOpen};
constexpr const char* MDNS_HOSTNAME {ROOM_CONFIGS[ACTIVE_ROOM].mdnsHostname};
#endif

// LED logical state — active-low: ON = LOW, OFF = HIGH
enum LedState : uint8_t {
  LED_ON  = LOW,
  LED_OFF = HIGH,
};

// HTTP server port
constexpr uint16_t SERVER_PORT {80};

// Onboard LED pin (GPIO 8 on Adafruit Qt Py ESP32-C3)
constexpr uint8_t LED_PIN {8};

// Serial baud rate
constexpr uint32_t SERIAL_BAUD_RATE {115200};

// Retry delay (ms) for WiFi, geo, and time-sync failures
constexpr uint32_t RETRY_DELAY_MS {5000};

// Interval (ms) between status log messages in the main loop
constexpr uint32_t STATUS_INTERVAL_MS {10000};

// RS485 traffic debug log: cached messages are flushed to WebSerial as one
// block on this interval (independent of STATUS_INTERVAL_MS), only when
// non-empty. Buffer size bounds how much traffic can be cached between
// flushes; excess is dropped rather than overflowing.
constexpr uint32_t TRAFFIC_LOG_INTERVAL_MS {1000};
constexpr size_t   TRAFFIC_LOG_BUF_SIZE    {2048};

// Daily NTP/sun-time refresh window (24-hour clock): [START, END)
constexpr int DAILY_REFRESH_START_HOUR {2};
constexpr int DAILY_REFRESH_END_HOUR   {4};

// Decimal places used when displaying GPS coordinates
constexpr int GEO_COORD_DECIMALS {4};

// How often (ms) to check WiFi connectivity and re-join if disconnected
constexpr uint32_t WIFI_CHECK_INTERVAL_MS {60000};

// RS485 ///////////////////////////////////////////////////////////////////////

const uint8_t NUM_ACTUATORS  {3};
const uint8_t RX_TIMEOUT  {200};  // ms

#ifndef ACTUATOR_ADDRESS
  constexpr uint32_t ACTUATOR_ADDRESS  {1};
#endif

constexpr uint32_t RS485_DE_RE  {1};
constexpr uint32_t RS485_RX     {2};
constexpr uint32_t RS485_TX     {0};

constexpr uint8_t CMD_QUERY       {0x01};
constexpr uint8_t CMD_OPEN        {0x02};
constexpr uint8_t CMD_CLOSE       {0x03};
constexpr uint8_t CMD_SET_CONFIG  {0x04};
constexpr uint8_t CMD_CLOSE_HALF  {0x05};
constexpr uint8_t RSP_STATE       {0x80};
constexpr uint8_t RSP_COMPLETE    {0x81};
constexpr uint8_t RSP_CONFIG_ACK  {0x82};
constexpr uint8_t RSP_COMMAND_ACK {0x83};
constexpr uint8_t START_BYTE      {0xAA};
constexpr uint8_t END_BYTE        {0x55};

// Retry attempts for a query/config/command send that goes unacknowledged
constexpr uint8_t COMMAND_RETRY_COUNT      {3};
// Delay (ms) between in-operation retries -- short, this is bus recovery,
// not "wait for a slow-booting actuator" like the boot-time retry delay is
constexpr uint32_t COMMAND_RETRY_DELAY_MS  {100};
// How long (ms) to wait before re-attempting a scheduled action that failed
constexpr uint32_t SCHEDULE_RETRY_INTERVAL_MS {60000};

// Configuration packet structure - now with separate open/close speeds
struct ConfigPacket {
    float motion_length;      // motor turns
    float motor_speed_open;   // RPM when opening
    float motor_speed_close;  // RPM when closing
};

// Blinds Behavior /////////////////////////////////////////////////////////////
constexpr float MOTION_LENGTH      {12.6F * 30.0F}; // motor turns
constexpr float MOTOR_SPEED_OPEN   {450.0F};  // Spool RPM
constexpr float MOTOR_SPEED_CLOSE  {600.0F}; // Spool RPM
constexpr float MOTION_TIMEOUT     {60.0F * MOTION_LENGTH / MOTOR_SPEED_OPEN * 1.5F}; // sec (conservative, based on slower open speed)

// Blinds Hardware /////////////////////////////////////////////////////////////
constexpr uint8_t PIN_PHASE_A     {5U};
constexpr uint8_t PIN_PHASE_B     {6U};
constexpr uint8_t PIN_PHASE_C     {7U};
constexpr uint8_t PIN_ENABLE      {10U};
constexpr uint8_t PIN_LIMIT       {4U};
constexpr uint8_t NUM_POLE_PAIRS  {11U};

// Actuator hardware watchdog timeout (sec). Generous margin over the tick
// period -- only needs to catch a stall between motorMove() ticks, not
// bound the whole (up to ~76s) motion, which is fed every tick.
constexpr uint8_t WATCHDOG_TIMEOUT_SEC {15U};

// Scheduling //////////////////////////////////////////////////////////////////

// Timing offsets for sunrise/sunset actions
constexpr int32_t SUNRISE_ACTION_OFFSET{30 * MINUTE};  // 30 minutes after sunrise
constexpr int32_t SUNSET_ACTION_OFFSET{30 * MINUTE};   // 30 minutes before sunset

// Earliest time of day the sunrise action may run — never open blinds before this,
// even if sunrise + offset is earlier (matters in summer at high latitudes).
constexpr int32_t MIN_SUNRISE_ACTION_TIME{7 * HOUR};   // 7:00 AM

// Bedroom sun-protection window: window 3 stays closed within
// the midday window (see offsets below), even when 1 & 2 are opened
// (manually or automatically) -- it gets excluded rather than opened and
// immediately auto-closed again.
//
// The window is anchored to solar noon (midpoint of today's sunrise/sunset in
// local time) rather than fixed clock times, so it tracks the sun across DST
// changes. Offsets were derived from the original 10:45 AM - 12:45 PM PDT
// window, tuned in April when solar noon was ~1:10 PM PDT.
constexpr int32_t MIDDAY_CLOSE_OFFSET{-(2 * HOUR + 25 * MINUTE)};  // 2h25m before solar noon
constexpr int32_t MIDDAY_OPEN_OFFSET {-(25 * MINUTE)};             // 25m before solar noon

// Each scheduled action (sunrise open, sunset close, midday close/open) is
// only actively enforced for this long after its start time -- long enough
// to retry through brief RS485 dropouts, short enough that manual overrides
// afterward stick instead of being immediately undone.
constexpr int32_t SCHEDULE_ACTION_WINDOW{5 * MINUTE};

// Program /////////////////////////////////////////////////////////////////////

constexpr uint32_t TICK_PERIOD {10U}; // ms

// Button //////////////////////////////////////////////////////////////////////

constexpr uint32_t PIN_BTN {9U};
constexpr uint32_t BUTTON_DEBOUNCE_MS    {100U};  // lower limit for short press
constexpr uint32_t BUTTON_SHORT_PRESS_MS {500U};  // upper limit for short press
constexpr uint32_t BUTTON_LONG_PRESS_MS  {1000U}; // single limit for long press

// End of file /////////////////////////////////////////////////////////////////
