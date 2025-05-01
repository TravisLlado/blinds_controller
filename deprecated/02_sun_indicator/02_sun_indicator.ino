#include <Arduino.h>

////////////////////////////////////////////////////////////////////////////////
// Pin Definitions
////////////////////////////////////////////////////////////////////////////////

#define PIN_LIMIT_SWITCH_DOWN   7
#define PIN_LIMIT_SWITCH_UP     6
#define PIN_BUTTON              9
#define PIN_MOTOR_SPEED         0
#define PIN_MOTOR_FORWARD       1
#define PIN_MOTOR_REVERSE       2
#define PIN_LED                 8

////////////////////////////////////////////////////////////////////////////////
// PWM & Timing Definitions
////////////////////////////////////////////////////////////////////////////////

#define PWM_FREQUENCY   20000      // Hz
#define PWM_DUTY_CYCLE  95         // %
#define PWM_RESOLUTION  10         // 10-bit resolution (LEDC_TIMER_10_BIT)

#define SLEEP_PERIOD_MS     50      // ms
#define ACTIVE_PERIOD_MS    1       // ms
#define MOVE_TIME_LIMIT_MS  20000   // ms

enum BlindsPositionType {
  OPEN,
  CLOSED
};

////////////////////////////////////////////////////////////////////////////////
// Function Prototypes
////////////////////////////////////////////////////////////////////////////////

bool blinds_are_closed(void);
bool blinds_are_open(void);
bool button_is_pressed(void);
void led_off(void);
void led_on(void);
void move_blinds(const BlindsPositionType direction);

////////////////////////////////////////////////////////////////////////////////
// Functions
////////////////////////////////////////////////////////////////////////////////

bool blinds_are_closed(void) {
  // Returns the level of the "closed" limit switch.
  return digitalRead(PIN_LIMIT_SWITCH_DOWN);
}

bool blinds_are_open(void) {
  // Returns the level of the "open" limit switch.
  return digitalRead(PIN_LIMIT_SWITCH_UP);
}

bool button_is_pressed(void) {
  // The button is active low.
  return !digitalRead(PIN_BUTTON);
}

void led_off(void) {
  digitalWrite(PIN_LED, HIGH);
}

void led_on(void) {
  digitalWrite(PIN_LED, LOW);
}

void move_blinds(const BlindsPositionType direction) {
  const size_t direction_pin = (direction == OPEN) ? PIN_MOTOR_FORWARD : PIN_MOTOR_REVERSE;
  bool (* const blinds_are_positioned)(void) = (direction == OPEN) ? blinds_are_open : blinds_are_closed;

  int count = 0;
  
  digitalWrite(direction_pin, HIGH);
  while (!blinds_are_positioned() && (count++ < (MOVE_TIME_LIMIT_MS / ACTIVE_PERIOD_MS))) {
    delay(ACTIVE_PERIOD_MS);
  }
  digitalWrite(direction_pin, LOW);
}

////////////////////////////////////////////////////////////////////////////////
// Setup and Loop
////////////////////////////////////////////////////////////////////////////////

void setup() {
  // Configure input pins with pull-ups (same as GPIO_PULLUP_ENABLE)
  pinMode(PIN_LIMIT_SWITCH_DOWN, INPUT_PULLUP);
  pinMode(PIN_LIMIT_SWITCH_UP, INPUT_PULLUP);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  // Configure output pins
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_MOTOR_FORWARD, OUTPUT);
  pinMode(PIN_MOTOR_REVERSE, OUTPUT);

  // Initialize motor control pins to LOW
  digitalWrite(PIN_MOTOR_FORWARD, LOW);
  digitalWrite(PIN_MOTOR_REVERSE, LOW);
  led_off();

  // Configure PWM (LEDC) for motor speed control
  // Channel 0 is used here.
  ledcAttach(PIN_MOTOR_SPEED, PWM_FREQUENCY, PWM_RESOLUTION);
  int duty = (PWM_DUTY_CYCLE * ((1 << PWM_RESOLUTION) - 1)) / 100;
  ledcWrite(PIN_MOTOR_SPEED, duty);
}

void loop() {
  if (button_is_pressed()) {
    led_on();
    const BlindsPositionType desired_position = blinds_are_closed() ? OPEN : CLOSED;
    move_blinds(desired_position);
    led_off();
  }
  delay(SLEEP_PERIOD_MS);
}
