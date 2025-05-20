// config.h ////////////////////////////////////////////////////////////////////

#include <Arduino.h>
#include "TimeSync.h"

// Program /////////////////////////////////////////////////////////////////////

constexpr uint32_t TICK_PERIOD{10U}; // ms

// Button //////////////////////////////////////////////////////////////////////

constexpr uint32_t PIN_BTN{5U};
constexpr uint32_t BUTTON_PRESS_LENGTH{200000U}; // us

// Wire Mappings ///////////////////////////////////////////////////////////////

// Orange -    Speed (Blue)
// Orange +    Direction (White)
// Green  -    Encoder (Yellow)
// Blue   +    Vcc (Red)
// Blue   -    Gnd (Black)
// Green  +    Limit (Orange)
// Brown  -    Gnd (Black)
// Brown  +    Vcc (Red)

// Actuator 1 //////////////////////////////////////////////////////////////////

constexpr uint8_t PIN_LIM_1{2U};
constexpr uint8_t PIN_ENC_1{3U};
constexpr uint8_t PIN_DIR_1{4U};
constexpr uint8_t PIN_SPD_1{6U};

constexpr uint32_t OPEN_TICKS_1{4250};
constexpr float DUTY_CYCLE_1{0.40F};
constexpr int32_t TIMEOUT_1{30000}; // ms

// Actuator 2 //////////////////////////////////////////////////////////////////

constexpr uint8_t PIN_LIM_2{10U};
constexpr uint8_t PIN_ENC_2{7U};
constexpr uint8_t PIN_DIR_2{8U};
constexpr uint8_t PIN_SPD_2{1U};

constexpr uint32_t OPEN_TICKS_2{4250};
constexpr float DUTY_CYCLE_2{0.40F};
constexpr int32_t TIMEOUT_2{30000}; // ms

// Actuator 3 //////////////////////////////////////////////////////////////////

constexpr uint8_t PIN_LIM_3{21U};
constexpr uint8_t PIN_ENC_3{20U};
constexpr uint8_t PIN_DIR_3{9U};
constexpr uint8_t PIN_SPD_3{0U};

constexpr uint32_t OPEN_TICKS_3{4250};
constexpr float DUTY_CYCLE_3{0.40F};
constexpr int32_t TIMEOUT_3{30000}; // ms

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

// End of file /////////////////////////////////////////////////////////////////
