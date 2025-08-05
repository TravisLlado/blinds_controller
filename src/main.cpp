// main.cpp ////////////////////////////////////////////////////////////////////

// Includes ////////////////////////////////////////////////////////////////////
#include "config.h"
#include "credentials.h"
#include "blinds.h"
#include "scheduler.h"
#include "button.h"

// Function Prototypes /////////////////////////////////////////////////////////
void buttonAction();

#if SCHEDULING
void sunriseAction();
void sunsetAction();
#endif

// Global Variables ////////////////////////////////////////////////////////////
Blinds g_blinds(PIN_DIR, PIN_SPD, PIN_ENC, PIN_LIM, PIN_BRK, OPEN_TICKS, TIMEOUT, 
                OPEN_SPEED, CLOSE_SPEED, RAMP_LENGTH);

#if SCHEDULING
Scheduler g_scheduler(WIFI_SSID, WIFI_PASSWORD, LATITUDE, LONGITUDE, TIMEZONE_OFFSET,
                      SUNRISE_ACTION_OFFSET, SUNSET_ACTION_OFFSET,
                      sunriseAction, sunsetAction);
#endif

Button g_button(PIN_BTN, BUTTON_PRESS_LENGTH, Button::ACTIVE_LOW, buttonAction);

#if SCHEDULING
void sunriseAction();
void sunsetAction();
#endif

void setup() {
    delay(500U);
    Serial.begin(115200);
    Serial.println("\n\nBlinds Control System");
    
    // Initialize button
    g_button.begin();
    
    #if SCHEDULING
        // Initialize scheduler
        g_scheduler.begin();
    #endif
    
    Serial.println("System ready!");
}

void loop() {
    // Update button (handles debouncing and hold detection)
    g_button.update();
    
    #if SCHEDULING
        // Update scheduler (handles all time-based actions)
        g_scheduler.update();
    #endif
    
    // Small delay to prevent CPU hogging
    delay(TICK_PERIOD);
}

// Action Functions ////////////////////////////////////////////////////////////
void buttonAction() {
    Serial.println("Button pressed!");
    Serial.println("Toggling window ...");
    g_blinds.toggle();
    Serial.println("Done");
}

// Scheduling Action Functions /////////////////////////////////////////////////
#if SCHEDULING
void sunriseAction() {
    Serial.println("Opening window 1 ...");
    #if AUTO_OPEN
        g_blinds.open();
    #endif
    Serial.println("Done");
}

void sunsetAction() {
    Serial.println("Closing window 1 ...");
    g_blinds.close();
    Serial.println("Done");
}
#endif

// End of file /////////////////////////////////////////////////////////////////
