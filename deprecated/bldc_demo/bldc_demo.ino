#include <Arduino.h>

// Pin assignments
constexpr uint8_t LED_PIN = 8;
constexpr uint8_t SPD_PIN = 5;
constexpr uint8_t DIR_PIN = 6;
constexpr uint8_t ENC_PIN = 7;
constexpr uint8_t LIM_PIN = 10;
constexpr uint8_t BTN_PIN = 20;

// Motor config
constexpr uint32_t PWM_FREQ = 1000;
constexpr uint8_t  PWM_RES  = 8;
constexpr uint32_t PWM_MAX  = 1 << PWM_RES;
constexpr uint32_t DUTY_FIXED = static_cast<uint32_t>(0.9F * PWM_MAX);

// Logic
constexpr uint32_t OPEN_TICKS = 4200; // Encoder ticks to fully open
constexpr uint32_t TICK_PERIOD = 10;  // ms

// Direction enum
enum Direction {
  OPEN = LOW,
  CLOSED = HIGH
};

// Globals
volatile uint32_t tickCount = 0;
Direction currentDirection = CLOSED;

// ISR for encoder
void IRAM_ATTR onEncoderEdge() {
  tickCount++;
}

void stopMotor() {
  digitalWrite(SPD_PIN, LOW);
  digitalWrite(LED_PIN, LOW);
}

void runMotor(const Direction dir) {
  digitalWrite(DIR_PIN, dir);
  digitalWrite(SPD_PIN, HIGH);
  digitalWrite(LED_PIN, HIGH);
}

void open() {
  Serial.println("Opening blinds...");
  currentDirection = OPEN;
  tickCount = 0;
  runMotor(currentDirection);

  while (tickCount < OPEN_TICKS) {
    Serial.print("Open ticks: ");
    Serial.println(tickCount);
    delay(TICK_PERIOD);
  }

  stopMotor();
  Serial.println("Blinds opened.");
}

void close() {
  Serial.println("Closing blinds...");
  currentDirection = CLOSED;
  runMotor(currentDirection);

  while (digitalRead(LIM_PIN) != LOW) {
    Serial.println("Closing...");
    delay(TICK_PERIOD);
  }

  stopMotor();
  Serial.println("Blinds closed (limit switch triggered).");
}

void move() {
  const bool limitPressed = digitalRead(LIM_PIN) == LOW;
  if (limitPressed) {
    open();
  } else {
    close();
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(DIR_PIN, OUTPUT);
  pinMode(SPD_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(ENC_PIN, INPUT_PULLUP);
  pinMode(LIM_PIN, INPUT_PULLUP);
  pinMode(BTN_PIN, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(ENC_PIN), onEncoderEdge, RISING);

  stopMotor();
  Serial.println("System ready.");
}

void loop() {
  static bool lastBtnState = HIGH;
  const bool btnState = digitalRead(BTN_PIN);

  if (btnState == LOW && lastBtnState == HIGH) {
    Serial.println("Button pressed.");
    move();
  }

  lastBtnState = btnState;
  delay(TICK_PERIOD);  // Button debounce
}
