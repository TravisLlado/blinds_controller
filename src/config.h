// config.h ////////////////////////////////////////////////////////////////////

#include <Arduino.h>

constexpr bool SCHEDULING {true};
constexpr bool AUTO_OPEN  {false};

// RS485 ///////////////////////////////////////////////////////////////////////

const uint8_t NUM_SLAVES {3};
const uint8_t RX_TIMEOUT {200};

#ifndef SLAVE_ADDRESS
  constexpr uint32_t SLAVE_ADDRESS {1};
#endif

constexpr uint32_t RS485_DE_RE   {1};
constexpr uint32_t RS485_RX      {2};
constexpr uint32_t RS485_TX      {0};

constexpr uint8_t CMD_QUERY    {0x01};
constexpr uint8_t CMD_OPEN     {0x02};
constexpr uint8_t CMD_CLOSE    {0x03};
constexpr uint8_t RSP_STATE    {0x80};
constexpr uint8_t RSP_COMPLETE {0x81};
constexpr uint8_t START_BYTE   {0xAA};
constexpr uint8_t END_BYTE     {0x55};

// Blinds Behavior /////////////////////////////////////////////////////////////

constexpr uint32_t OPEN_TICKS     {2200U};  // encoder ticks
constexpr uint32_t MOTION_TIMEOUT {140000U}; // ms
constexpr uint32_t RAMP_LENGTH    {1000U};   // ms
constexpr float OPEN_SPEED        {0.25F};   // duty factor
constexpr float CLOSE_SPEED       {0.6F};    // duty factor

// Blinds Hardware /////////////////////////////////////////////////////////////

constexpr uint8_t PIN_BRK {7U};
constexpr uint8_t PIN_DIR {10U};
constexpr uint8_t PIN_ENC {4U};
constexpr uint8_t PIN_LIM {6U};
constexpr uint8_t PIN_SPD {3U};

// Program /////////////////////////////////////////////////////////////////////

constexpr uint32_t TICK_PERIOD {10U}; // ms

// Button //////////////////////////////////////////////////////////////////////

constexpr uint32_t PIN_BTN {9U};
constexpr uint32_t BUTTON_PRESS_LENGTH {150U};  // ms

// End of file /////////////////////////////////////////////////////////////////
