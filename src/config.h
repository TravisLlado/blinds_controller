// config.h ////////////////////////////////////////////////////////////////////

#include <Arduino.h>
#include "timesync.h"

#define SCHEDULING true
#define AUTO_OPEN  true

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

// Program /////////////////////////////////////////////////////////////////////

constexpr uint32_t TICK_PERIOD {10U}; // ms

// Button //////////////////////////////////////////////////////////////////////

constexpr uint32_t PIN_BTN {9U};
constexpr uint32_t BUTTON_DEBOUNCE_MS    {100U};  // lower limit for short press
constexpr uint32_t BUTTON_SHORT_PRESS_MS {500U};  // upper limit for short press
constexpr uint32_t BUTTON_LONG_PRESS_MS  {1000U}; // single limit for long press

// End of file /////////////////////////////////////////////////////////////////