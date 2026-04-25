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

constexpr Room ACTIVE_ROOM {LIVINGROOM};

constexpr bool        AUTO_OPEN     {ROOM_CONFIGS[ACTIVE_ROOM].autoOpen};
constexpr const char* MDNS_HOSTNAME {ROOM_CONFIGS[ACTIVE_ROOM].mdnsHostname};

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

// Daily NTP/sun-time refresh window (24-hour clock): [START, END)
constexpr int DAILY_REFRESH_START_HOUR {2};
constexpr int DAILY_REFRESH_END_HOUR   {4};

// Decimal places used when displaying GPS coordinates
constexpr int GEO_COORD_DECIMALS {4};

// How often (ms) to check WiFi connectivity and re-join if disconnected
constexpr uint32_t WIFI_CHECK_INTERVAL_MS {60000};

// RS485 ///////////////////////////////////////////////////////////////////////

const uint8_t NUM_SLAVES  {3};
const uint8_t RX_TIMEOUT  {200};  // ms

#ifndef SLAVE_ADDRESS
  constexpr uint32_t SLAVE_ADDRESS  {1};
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
constexpr uint8_t START_BYTE      {0xAA};
constexpr uint8_t END_BYTE        {0x55};

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

// Scheduling //////////////////////////////////////////////////////////////////

// Timing offsets for sunrise/sunset actions
constexpr int32_t SUNRISE_ACTION_OFFSET{30 * MINUTE};  // 30 minutes after sunrise
constexpr int32_t SUNSET_ACTION_OFFSET{30 * MINUTE};   // 30 minutes before sunset

// Earliest time of day the sunrise action may run — never open blinds before this,
// even if sunrise + offset is earlier (matters in summer at high latitudes).
constexpr int32_t MIN_SUNRISE_ACTION_TIME{7 * HOUR};   // 7:00 AM

// Program /////////////////////////////////////////////////////////////////////

constexpr uint32_t TICK_PERIOD {10U}; // ms

// Button //////////////////////////////////////////////////////////////////////

constexpr uint32_t PIN_BTN {9U};
constexpr uint32_t BUTTON_DEBOUNCE_MS    {100U};  // lower limit for short press
constexpr uint32_t BUTTON_SHORT_PRESS_MS {500U};  // upper limit for short press
constexpr uint32_t BUTTON_LONG_PRESS_MS  {1000U}; // single limit for long press

// End of file /////////////////////////////////////////////////////////////////
