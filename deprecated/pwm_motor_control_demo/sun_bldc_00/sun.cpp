// sun.cpp /////////////////////////////////////////////////////////////////////

// Includes ////////////////////////////////////////////////////////////////////

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <NTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
#include "sun.h"

// Constants ///////////////////////////////////////////////////////////////////

constexpr float LATITUDE = 37.7749F;
constexpr float LONGITUDE = -122.4194F;
constexpr int TIMEZONE_OFFSET_SEC = (-7 * 3600);

constexpr int REPOSITION_THRESHOLD_SEC = 30;
constexpr int SUNRISE_OFFSET_SEC = (30U * 60U);
constexpr int SUNSET_OFFSET_SEC  = (30U * 60U);

constexpr const char* WIFI_PASSWORD = "VituperativePrognosticator";
constexpr const char* WIFI_SSID = "OurWifi";

constexpr unsigned long UPDATE_WINDOW_END_SEC   = (5U * 3600U);  // 5:00 AM
constexpr unsigned long UPDATE_WINDOW_START_SEC = (2U * 3600U);  // 2:00 AM

// Prototypes //////////////////////////////////////////////////////////////////

static void connectWifi();
static bool fetchSunriseSunsetSuccess();
static int parseTime(const String& timeStr);
static bool updateTimeSuccess();

// Globals /////////////////////////////////////////////////////////////////////

bool global_update_done_for_day = false;
unsigned long global_last_sync_epoch = 0;
int global_sunrise_time = (6 * 3600);
int global_sunset_time  = (18 * 3600);

WiFiUDP global_udp;
NTPClient global_time_client(global_udp, "pool.ntp.org", TIMEZONE_OFFSET_SEC, 60000U);

// Internal Functions //////////////////////////////////////////////////////////

void connectWifi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  const unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000U) {
    delay(500);
  }
}

int parseTime(const String& timeStr) {
  int hour = timeStr.substring(11, 13).toInt();
  int minute = timeStr.substring(14, 16).toInt();
  int second = timeStr.substring(17, 19).toInt();
  return hour * 3600 + minute * 60 + second;
}

bool updateTimeSuccess() {
  Serial.println("Updating NTP time...");
  for (int i = 0; i < 5; i++) {
    if (global_time_client.update()) {
      global_last_sync_epoch = global_time_client.getEpochTime();
      Serial.println("Time update successful.");
      return true;
    }
    Serial.println("Retrying NTP...");
    delay(2000);
  }
  Serial.println("Time update failed.");
  return false;
}

bool fetchSunriseSunsetSuccess() {
  HTTPClient http;
  String url = "http://api.sunrise-sunset.org/json?lat=" + String(LATITUDE) + "&lng=" + String(LONGITUDE) + "&formatted=0";
  for (int i = 0; i < 5; i++) {
    http.begin(url);
    int code = http.GET();
    if (code == 200) {
      StaticJsonDocument<1024> doc;
      DeserializationError err = deserializeJson(doc, http.getString());
      if (err) {
        http.end();
        continue;
      }
      global_sunrise_time = (parseTime(doc["results"]["sunrise"]) + TIMEZONE_OFFSET_SEC + 86400) % 86400;
      global_sunset_time  = (parseTime(doc["results"]["sunset"])  + TIMEZONE_OFFSET_SEC + 86400) % 86400;
      http.end();
      return true;
    }
    http.end();
    delay(2000);
  }
  return false;
}

// Public API //////////////////////////////////////////////////////////////////

void setupNetworking() {
  connectWifi();
  global_time_client.begin();
  updateTimeSuccess();
  fetchSunriseSunsetSuccess();
  WiFi.disconnect();
}

unsigned long getCurrentEpoch() {
  return global_time_client.getEpochTime();
}

void updateSunData(int secondsSinceMidnight) {
  if (!(secondsSinceMidnight >= UPDATE_WINDOW_START_SEC && secondsSinceMidnight < UPDATE_WINDOW_END_SEC && !global_update_done_for_day)) {
    if (secondsSinceMidnight >= UPDATE_WINDOW_END_SEC && !global_update_done_for_day) {
      global_update_done_for_day = true;
    }
    return;
  }

  connectWifi();

  if (WiFi.status() == WL_CONNECTED) {
    updateTimeSuccess();
    fetchSunriseSunsetSuccess();
    global_update_done_for_day = true;
  }

  WiFi.disconnect();
}

bool shouldResetDailyFlags(int secondsSinceMidnight) {
  return secondsSinceMidnight < 3600;
}

bool sunIsUp(int secondsSinceMidnight) {
  return secondsSinceMidnight >= global_sunrise_time && secondsSinceMidnight < global_sunset_time;
}

bool isSunriseTriggerTime(int secondsSinceMidnight) {
  int target = global_sunrise_time + SUNRISE_OFFSET_SEC;
  return abs(secondsSinceMidnight - target) < REPOSITION_THRESHOLD_SEC;
}

bool isSunsetTriggerTime(int secondsSinceMidnight) {
  int target = global_sunset_time - SUNSET_OFFSET_SEC;
  return abs(secondsSinceMidnight - target) < REPOSITION_THRESHOLD_SEC;
}

// End of file /////////////////////////////////////////////////////////////////
