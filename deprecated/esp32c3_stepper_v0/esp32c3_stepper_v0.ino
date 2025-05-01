#include <TMCStepper.h>

#define ENBL_PIN  0
#define STEP_PIN  1
#define DIR_PIN   2
#define LED_PIN   8
#define UART_RX   21 // Connected to TMC2209 PDN_UART
#define UART_TX   21 // Same pin for TX/RX in single-wire mode
#define R_SENSE   0.11f  // Sense resistor value on your driver board

TMC2209Stepper driver = TMC2209Stepper(&Serial1, R_SENSE, 0);

const uint32_t ppr = 16576U;
const uint32_t max_velocity = 16576U;     // steps/sec = 1 rev/sec
const uint32_t acceleration = 100U;       // velocity increment per step
const uint32_t min_velocity = 100U;       // ensure we never divide by 0
const uint32_t dwell_ms = 1000U;

bool direction = false;
bool connected = false;

void step_delay(uint32_t delay_us) {
  digitalWrite(STEP_PIN, HIGH);
  delayMicroseconds(2);
  digitalWrite(STEP_PIN, LOW);
  delayMicroseconds(delay_us);
}

void setup() {
  // Serial for debug output
  Serial.begin(115200);
  while (!Serial);  // Wait for serial connection

  // Pins for stepper control
  pinMode(STEP_PIN, OUTPUT);
  pinMode(ENBL_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(ENBL_PIN, HIGH); // Start disabled

  // UART setup
  Serial1.begin(115200, SERIAL_8N1, UART_RX, UART_TX);
  driver.begin();
  driver.toff(3);               // Enables driver (required)
  driver.rms_current(2800);     // Set motor RMS current in mA
  // driver.microsteps(16);        // Microstepping level
  // driver.en_spreadCycle(true);  // Use spreadCycle for higher torque
  // driver.pwm_autoscale(true);   // Required for stealthChop (even when off)
  driver.push();                // Apply shadow register changes

  // Debug: check UART communication
  connected = driver.test_connection();
}

void loop() {
  direction = !direction;
  digitalWrite(DIR_PIN, direction);
  digitalWrite(LED_PIN, !direction);
  digitalWrite(ENBL_PIN, LOW);  // Enable motor

  uint32_t velocity = min_velocity;
  uint32_t ramp_steps = (max_velocity - min_velocity) / acceleration;
  if (2 * ramp_steps >= ppr) ramp_steps = ppr / 2;
  uint32_t cruise_steps = ppr - 2 * ramp_steps;

  // --- Ramp up ---
  for (uint32_t i = 0; i < ramp_steps; i++) {
    velocity += acceleration;
    if (velocity > max_velocity) velocity = max_velocity;
    uint32_t delay_us = 1000000UL / velocity;
    if (delay_us < 10) delay_us = 10;
    step_delay(delay_us);
  }

  // --- Cruise ---
  uint32_t cruise_delay = 1000000UL / max_velocity;
  for (uint32_t i = 0; i < cruise_steps; i++) {
    step_delay(cruise_delay);
  }

  // --- Ramp down ---
  for (uint32_t i = 0; i < ramp_steps; i++) {
    if (velocity > acceleration) velocity -= acceleration;
    else velocity = min_velocity;
    uint32_t delay_us = 1000000UL / velocity;
    if (delay_us < 10) delay_us = 10;
    step_delay(delay_us);
  }

  digitalWrite(ENBL_PIN, HIGH); // Disable motor

  if (connected) {
    Serial.println("✅ TMC2209 connection OK");
  } else {
    Serial.println("❌ TMC2209 connection failed");
  }

  // Debug: print out config
  Serial.print("rms_current = ");
  Serial.println(driver.rms_current());

  // Serial.print("microsteps = ");
  // Serial.println(driver.microsteps());

  // Serial.print("spreadCycle enabled = ");
  // Serial.println(driver.en_spreadCycle());

  // Serial.print("DRV_STATUS = 0x");
  // Serial.println(driver.DRV_STATUS(), HEX);

  delay(dwell_ms);
}
