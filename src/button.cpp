// button.cpp //////////////////////////////////////////////////////////////////

#include "button.h"

// Constructor /////////////////////////////////////////////////////////////////

Button::Button(uint8_t pin, uint32_t holdTimeMs, PinLogic logic, ButtonCallback callback)
    : button_pin(pin),
      hold_time_us(holdTimeMs * 1000U),
      pin_logic(logic),
      action_callback(callback) {
}

// Public Methods //////////////////////////////////////////////////////////////

void Button::begin() {
    // Configure pin based on logic type
    if (pin_logic == ACTIVE_LOW) {
        pinMode(button_pin, INPUT_PULLUP);
    } else {
        pinMode(button_pin, INPUT);
    }
    
    // Initialize state
    current_state = readPin();
    last_state = current_state;
    press_handled = false;
    press_start_time = 0;
    press_edge = false;
    release_edge = false;
}

void Button::update() {
    // Read current pin state
    last_state = current_state;
    current_state = readPin();
    
    // Update edge detection flags
    updateEdgeFlags();
    
    // Handle button press logic
    if (current_state) {
        // Button is currently pressed
        if (press_edge) {
            // New press started
            press_start_time = micros();
            press_handled = false;
        }
        
        // Check if button has been held long enough and not yet handled
        if (!press_handled && (micros() - press_start_time) >= hold_time_us) {
            if (action_callback) {
                action_callback();
            }
            press_handled = true;
        }
    }
    // Note: Release handling could be added here if needed
}

bool Button::isPressed() const {
    return current_state;
}

bool Button::wasPressed() const {
    return press_edge;
}

bool Button::wasReleased() const {
    return release_edge;
}

uint32_t Button::getPressTime() const {
    if (current_state && press_start_time > 0) {
        return (micros() - press_start_time) / 1000U; // Convert to ms
    }
    return 0;
}

// Private Methods /////////////////////////////////////////////////////////////

bool Button::readPin() const {
    const bool pin_state = digitalRead(button_pin);
    
    if (pin_logic == ACTIVE_LOW) {
        return !pin_state; // Invert for active low
    } else {
        return pin_state;  // Direct for active high
    }
}

void Button::updateEdgeFlags() {
    press_edge = (current_state && !last_state);   // Rising edge (pressed)
    release_edge = (!current_state && last_state); // Falling edge (released)
}

// End of file /////////////////////////////////////////////////////////////////
