#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ArduinoJson.h>

// ----- Configuration -----
const char* ssid = "OurWifi";
const char* password = "VituperativePrognosticator";

// GPS coordinates for San Francisco
const float latitude = 37.7749;
const float longitude = -122.4194;
// Using a fixed timezone offset for PDT (-7 hours)
const int timezone_offset = -7 * 3600;

// LED pin (active LOW: LOW = ON, HIGH = OFF)
const int LED_PIN = 8;

// ----- Global Variables -----
WiFiUDP udp;
NTPClient timeClient(udp, "pool.ntp.org", timezone_offset, 60000);
unsigned long lastSync = 0;  // epoch time (seconds) at last successful NTP sync

// Sunrise and sunset times (in seconds since midnight)
// Defaults if update fails: sunrise at 06:00 (21600 sec), sunset at 18:00 (64800 sec)
int sunrise_time = 6 * 3600;
int sunset_time  = 18 * 3600;

// Daily update control variables:
bool updateDoneForDay = false;
const unsigned long updateWindowStart = 7200;   // 2:00 AM (seconds since midnight)
const unsigned long updateWindowEnd   = 18000;  // 5:00 AM (seconds since midnight)

// ----- Helper Functions -----
// Parse an ISO8601 time string (e.g., "2025-03-22T14:07:45+00:00") and return seconds since midnight.
int parseTime(String timeStr) {
  int hour = timeStr.substring(11, 13).toInt();
  int minute = timeStr.substring(14, 16).toInt();
  int second = timeStr.substring(17, 19).toInt();
  return hour * 3600 + minute * 60 + second;
}

// Consolidated formatting function: formats a seconds value as "HH:MM:SS" using modulo arithmetic.
String formatTimeOfDay(unsigned long sec) {
  unsigned long secOfDay = sec % 86400;
  int hour = secOfDay / 3600;
  int minute = (secOfDay % 3600) / 60;
  int second = secOfDay % 60;
  char buf[9];
  sprintf(buf, "%02d:%02d:%02d", hour, minute, second);
  return String(buf);
}

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

// Combined update function.
bool updateEverything() {
  return updateTimeSuccess() && fetchSunriseSunsetSuccess();
}

void setup() {
  Serial.begin(115200);
  Serial.println("Starting...");
  
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // LED off (active LOW)
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to Wi-Fi");
  
  timeClient.begin();
  // Try to update time and sunrise/sunset once at startup.
  updateTimeSuccess();
  fetchSunriseSunsetSuccess();
  
  // Reset daily update flag if before 2:00 AM local.
  unsigned long currentEpoch = timeClient.getEpochTime();
  int secondsSinceMidnight = currentEpoch % 86400;
  if (secondsSinceMidnight < 7200) {
    updateDoneForDay = false;
  }
}

void loop() {
  ensureWiFiConnected();
  
  timeClient.update();
  unsigned long currentEpoch = timeClient.getEpochTime();
  int secondsSinceMidnight = currentEpoch % 86400;
  bool isDaytime = (secondsSinceMidnight >= sunrise_time && secondsSinceMidnight < sunset_time);
  
  // Build and print a single output line.
  String output = "";
  output += "Current: " + formatTimeOfDay(currentEpoch);
  output += " | Sunrise: " + formatTimeOfDay(sunrise_time);
  output += " | Sunset: " + formatTimeOfDay(sunset_time);
  output += " | Sun Out: " + String(isDaytime ? "YES" : "NO");
  output += " | Last Sync: " + formatTimeOfDay(lastSync);
  Serial.println(output);
  
  // LED flash pattern: 1-second cycle.
  // If sun is up: LED ON (active LOW) for 90% of cycle (0.9 sec on, 0.1 sec off);
  // If sun is down: LED ON for 10% (0.1 sec on, 0.9 sec off).
  int period = 1000; // ms
  int onTime = isDaytime ? period * 0.9 : period * 0.1;
  int offTime = period - onTime;
  digitalWrite(LED_PIN, HIGH); // LED off
  delay(offTime);
  digitalWrite(LED_PIN, LOW);  // LED on
  delay(onTime);
  
  // If it's between 2:00 AM and 5:00 AM (7200 <= seconds < 18000) and update hasn't succeeded yet,
  // attempt to update.
  if (secondsSinceMidnight >= 7200 && secondsSinceMidnight < 18000 && !updateDoneForDay) {
    Serial.println("Attempting to update time and sunrise/sunset values...");
    if (updateEverything()) {
      updateDoneForDay = true;
      Serial.println("Update succeeded.");
    } else {
      Serial.println("Update failed. Will retry until 5 AM.");
    }
  }
  
  // Once past 5 AM (>= 18000 seconds), if the update hasn't succeeded, stop trying for today.
  if (secondsSinceMidnight >= 18000 && !updateDoneForDay) {
    Serial.println("Update window expired. Using last known values for today.");
    updateDoneForDay = true;
  }
  
  // Reset the daily update flag when the day resets (e.g., before 1:00 AM local).
  if (secondsSinceMidnight < 3600) {
    updateDoneForDay = false;
  }
}
