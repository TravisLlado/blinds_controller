#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ArduinoJson.h>

// ----- Wi-Fi & Time Configuration -----
const char* ssid = "OurWifi";
const char* password = "VituperativePrognosticator";

// GPS coordinates for San Francisco
const float latitude = 37.7749;
const float longitude = -122.4194;
// Fixed timezone offset for PDT (-7 hours)
const int timezone_offset = -7 * 3600;

// ----- Pin Definitions (shared) -----
#define PIN_LIMIT_SWITCH_DOWN   7
#define PIN_LIMIT_SWITCH_UP     6
#define PIN_BUTTON              9
#define PIN_MOTOR_SPEED         0
#define PIN_MOTOR_FORWARD       1
#define PIN_MOTOR_REVERSE       2
#define PIN_LED                 8

// ----- PWM & Blinds Timing Definitions -----
#define PWM_FREQUENCY       20000 // Hz
#define PWM_DUTY_CYCLE      60    // %
#define PWM_RESOLUTION      10    // 10-bit resolution

#define SLEEP_PERIOD_MS     50    // Main loop sleep time
#define ACTIVE_PERIOD_MS    1     // Motor active period per loop iteration
#define MOVE_TIME_LIMIT_MS  60000 // Max time to move blinds (20 seconds)
#define RAMP_LENGTH_MS      2000  // Length of time in which we ramp up motor speed at start of move

// ----- Sun & Update Timing -----
const unsigned long updateWindowStart = 7200;   // 2:00 AM (seconds since midnight)
const unsigned long updateWindowEnd   = 18000;  // 5:00 AM (seconds since midnight)
const int repositionThreshold = 30; // seconds window to trigger repositioning

// Offsets for repositioning relative to sunrise/sunset.
const int sunrise_offset = 30 * 60; // 30 minutes after sunrise
const int sunset_offset  = 30 * 60; // 30 minutes before sunset

// ----- Global Variables -----
WiFiUDP udp;
NTPClient timeClient(udp, "pool.ntp.org", timezone_offset, 60000);
unsigned long lastSync = 0;  // Last successful NTP sync epoch time (seconds)

int sunrise_time = 6 * 3600;   // Default sunrise at 06:00 (in seconds since midnight)
int sunset_time  = 18 * 3600;  // Default sunset at 18:00 (in seconds since midnight)

bool updateDoneForDay = false;
bool repositionDoneForSunrise = false;
bool repositionDoneForSunset = false;

// ----- Enum for Blinds Position -----
enum BlindsPositionType {
  OPEN,
  CLOSED
};

// ----- Helper Functions for Time -----
// Parse an ISO8601 string (e.g., "2025-03-22T14:07:45+00:00") and return seconds since midnight.
int parseTime(String timeStr) {
  int hour = timeStr.substring(11, 13).toInt();
  int minute = timeStr.substring(14, 16).toInt();
  int second = timeStr.substring(17, 19).toInt();
  return hour * 3600 + minute * 60 + second;
}

// Format seconds as "HH:MM:SS"
String formatTimeOfDay(unsigned long sec) {
  unsigned long secOfDay = sec % 86400;
  int hour = secOfDay / 3600;
  int minute = (secOfDay % 3600) / 60;
  int second = secOfDay % 60;
  char buf[9];
  sprintf(buf, "%02d:%02d:%02d", hour, minute, second);
  return String(buf);
}

// Ensure Wi-Fi connection is active.
void ensureWiFiConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi lost. Reconnecting...");
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
      delay(500);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED)
      Serial.println("\nReconnected to Wi-Fi.");
    else
      Serial.println("\nFailed to reconnect.");
  }
}

// Attempt to update NTP time. Returns true on success.
bool updateTimeSuccess() {
  Serial.println("Updating NTP time...");
  bool success = false;
  for (int i = 0; i < 5; i++) {
    if (timeClient.update()) {
      lastSync = timeClient.getEpochTime();
      Serial.println("Time update successful.");
      success = true;
      break;
    }
    Serial.println("NTP update failed, retrying...");
    delay(2000);
  }
  if (!success) Serial.println("NTP update failed after multiple attempts.");
  return success;
}

// Attempt to fetch sunrise/sunset times from the API. Returns true on success.
bool fetchSunriseSunsetSuccess() {
  HTTPClient http;
  String url = "http://api.sunrise-sunset.org/json?lat=" + String(latitude) +
               "&lng=" + String(longitude) + "&formatted=0";
  bool success = false;
  for (int i = 0; i < 5; i++) {
    Serial.println("Fetching sunrise/sunset times...");
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode == 200) {
      String payload = http.getString();
      Serial.println("Received JSON: " + payload);
      StaticJsonDocument<1024> doc;
      DeserializationError error = deserializeJson(doc, payload);
      if (error) {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.f_str());
        http.end();
        continue;
      }
      String sunrise = doc["results"]["sunrise"];
      String sunset  = doc["results"]["sunset"];
      int s = parseTime(sunrise) + timezone_offset;
      int t = parseTime(sunset)  + timezone_offset;
      // Adjust to 0–86399 range.
      if (s < 0) s += 86400;
      if (t < 0) t += 86400;
      if (s >= 86400) s -= 86400;
      if (t >= 86400) t -= 86400;
      sunrise_time = s;
      sunset_time  = t;
      Serial.print("Parsed sunrise (sec since midnight): ");
      Serial.println(sunrise_time);
      Serial.print("Parsed sunset  (sec since midnight): ");
      Serial.println(sunset_time);
      success = true;
      http.end();
      break;
    }
    Serial.println("Failed to fetch sunrise/sunset. Retrying...");
    http.end();
    delay(2000);
  }
  if (!success) Serial.println("Failed to fetch sunrise/sunset after multiple attempts.");
  return success;
}

// Combined update function for time and sunrise/sunset.
bool updateEverything() {
  return updateTimeSuccess() && fetchSunriseSunsetSuccess();
}

// ----- Blinds Control Functions -----
bool blinds_are_closed(void) {
  return digitalRead(PIN_LIMIT_SWITCH_DOWN);
}

bool blinds_are_open(void) {
  return digitalRead(PIN_LIMIT_SWITCH_UP);
}

bool button_is_pressed(void) {
  // Button is active LOW.
  return !digitalRead(PIN_BUTTON);
}

void led_off(void) {
  digitalWrite(PIN_LED, HIGH);  // Off since LED is active LOW
}

void led_on(void) {
  digitalWrite(PIN_LED, LOW);
}

// Move the blinds based on the desired direction.
void move_blinds(const BlindsPositionType direction) {
  const size_t direction_pin = (direction == OPEN) ? PIN_MOTOR_FORWARD : PIN_MOTOR_REVERSE;
  bool (* const blinds_are_positioned)(void) = (direction == OPEN) ? blinds_are_open : blinds_are_closed;

  const int ramp_steps = RAMP_LENGTH_MS / ACTIVE_PERIOD_MS;
  const int max_duty = (PWM_DUTY_CYCLE * ((1 << PWM_RESOLUTION) - 1)) / 100;

  digitalWrite(direction_pin, HIGH);

  for (int count = 0; count < (MOVE_TIME_LIMIT_MS / ACTIVE_PERIOD_MS); ++count) {
    if (blinds_are_positioned()) break;

    const int duty = (count < ramp_steps) ? (max_duty * count / ramp_steps) : max_duty;
    ledcWrite(PIN_MOTOR_SPEED, duty);
    delay(ACTIVE_PERIOD_MS);
  }

  ledcWrite(PIN_MOTOR_SPEED, 0);
  digitalWrite(direction_pin, LOW);
}

// ----- Setup -----
void setup() {
  Serial.begin(115200);
  Serial.println("Starting combined program...");

  // Configure input pins with pull-ups.
  pinMode(PIN_LIMIT_SWITCH_DOWN, INPUT_PULLUP);
  pinMode(PIN_LIMIT_SWITCH_UP, INPUT_PULLUP);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  // Configure output pins.
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_MOTOR_FORWARD, OUTPUT);
  pinMode(PIN_MOTOR_REVERSE, OUTPUT);

  // Initialize motor control pins to LOW.
  digitalWrite(PIN_MOTOR_FORWARD, LOW);
  digitalWrite(PIN_MOTOR_REVERSE, LOW);
  led_off();

  // Configure PWM for motor speed control (using new API).
  ledcAttach(PIN_MOTOR_SPEED, PWM_FREQUENCY, PWM_RESOLUTION);
  int duty = (PWM_DUTY_CYCLE * ((1 << PWM_RESOLUTION) - 1)) / 100;
  ledcWrite(PIN_MOTOR_SPEED, duty);

  // Connect to Wi-Fi.
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to Wi-Fi");

  // Initialize NTP and perform initial time and sunrise/sunset update.
  timeClient.begin();
  updateTimeSuccess();
  fetchSunriseSunsetSuccess();

  // Reset daily update flag if before update window.
  unsigned long currentEpoch = timeClient.getEpochTime();
  int secondsSinceMidnight = currentEpoch % 86400;
  if (secondsSinceMidnight < updateWindowStart) {
    updateDoneForDay = false;
  }
}

// ----- Main Loop -----
void loop() {
  ensureWiFiConnected();
  timeClient.update();
  unsigned long currentEpoch = timeClient.getEpochTime();
  int secondsSinceMidnight = currentEpoch % 86400;
  bool isDaytime = (secondsSinceMidnight >= sunrise_time && secondsSinceMidnight < sunset_time);

  // Build and print a status line.
  String output = "";
  output += "Current: " + formatTimeOfDay(currentEpoch);
  output += " | Sunrise: " + formatTimeOfDay(sunrise_time);
  output += " | Sunset: " + formatTimeOfDay(sunset_time);
  output += " | Sun Out: " + String(isDaytime ? "YES" : "NO");
  output += " | Last Sync: " + formatTimeOfDay(lastSync);
  Serial.println(output);

  // LED flash pattern (1-second cycle).
  // When sun is up: LED on for 90% of cycle, off for 10%.
  // When sun is down: LED on for 10% of cycle, off for 90%.
  int period = 1000;  // ms
  int onTime = isDaytime ? period * 0.9 : period * 0.1;
  int offTime = period - onTime;
  digitalWrite(PIN_LED, HIGH); // LED off
  delay(offTime);
  digitalWrite(PIN_LED, LOW);  // LED on
  delay(onTime);

  // Daily update: if current time is between updateWindowStart and updateWindowEnd and update not done.
  if (secondsSinceMidnight >= updateWindowStart && secondsSinceMidnight < updateWindowEnd && !updateDoneForDay) {
    Serial.println("Attempting daily update...");
    if (updateEverything()) {
      updateDoneForDay = true;
      Serial.println("Daily update succeeded.");
    } else {
      Serial.println("Daily update failed. Will retry until window closes.");
    }
  }
  if (secondsSinceMidnight >= updateWindowEnd && !updateDoneForDay) {
    Serial.println("Update window expired. Using last known values.");
    updateDoneForDay = true;
  }
  // Reset daily flags when a new day starts (e.g., before 1:00 AM).
  if (secondsSinceMidnight < 3600) {
    updateDoneForDay = false;
    repositionDoneForSunrise = false;
    repositionDoneForSunset = false;
  }

  // Automatically reposition blinds 30 minutes after sunrise.
  int targetSunriseReposition = sunrise_time + sunrise_offset;
  if (!repositionDoneForSunrise && (abs(secondsSinceMidnight - targetSunriseReposition) < repositionThreshold)) {
    Serial.println("30 minutes after sunrise reached. Checking blinds for opening...");
    if (!blinds_are_open()) {
      Serial.println("Blinds are not open. Opening blinds...");
      move_blinds(OPEN);
    }
    repositionDoneForSunrise = true;
  }
  // Automatically reposition blinds 30 minutes before sunset.
  int targetSunsetReposition = sunset_time - sunset_offset;
  if (!repositionDoneForSunset && (abs(secondsSinceMidnight - targetSunsetReposition) < repositionThreshold)) {
    Serial.println("30 minutes before sunset reached. Checking blinds for closing...");
    if (!blinds_are_closed()) {
      Serial.println("Blinds are not closed. Closing blinds...");
      move_blinds(CLOSED);
    }
    repositionDoneForSunset = true;
  }

  // Manual control: if the button is pressed, toggle blinds position.
  if (button_is_pressed()) {
    led_on();
    BlindsPositionType desired_position = blinds_are_closed() ? OPEN : CLOSED;
    move_blinds(desired_position);
    led_off();
  }
}
