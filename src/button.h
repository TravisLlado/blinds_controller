// button.h ////////////////////////////////////////////////////////////////////
#pragma once
#include <Arduino.h>

// Function pointer type for button action
typedef void (*ButtonCallback)();

// Button class with debouncing and hold detection
class Button {
public:
    enum PinLogic {
        ACTIVE_LOW = 0,   // Button pressed = LOW (with pullup)
        ACTIVE_HIGH = 1   // Button pressed = HIGH
    };
    
    Button(uint8_t pin, uint32_t holdTimeMs, PinLogic logic, ButtonCallback callback);
    
    void begin();
    void update();
    
    // Status queries
    bool isPressed() const;
    bool wasPressed() const;    // Returns true once per press
    bool wasReleased() const;   // Returns true once per release
    uint32_t getPressTime() const; // How long current press has lasted (ms)

private:
    const uint8_t button_pin;
    const uint32_t hold_time_us;
    const PinLogic pin_logic;
    const ButtonCallback action_callback;
    
    // Button state tracking
    bool current_state{false};
    bool last_state{false};
    bool press_handled{false};
    uint64_t press_start_time{0};
    
    // Edge detection flags
    bool press_edge{false};
    bool release_edge{false};
    
    // Helper methods
    bool readPin() const;
    void updateEdgeFlags();
};

// End of file /////////////////////////////////////////////////////////////////
