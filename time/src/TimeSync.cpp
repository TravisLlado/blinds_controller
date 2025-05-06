// TimeSync.cpp ////////////////////////////////////////////////////////////////

#include "TimeSync.h"

TimeSync::TimeSync(float latitude, float longitude, int timezone_offset_sec)
  : _lat(latitude), _lon(longitude), _tz_offset(timezone_offset_sec),
    _ntp(_udp, "pool.ntp.org", timezone_offset_sec, 60000),
    _lastSync(0), _sunrise(6 * 3600), _sunset(18 * 3600), _updateDoneForDay(false) {}

void TimeSync::begin(const char* ssid, const char* password) {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected to Wi-Fi");
  _ntp.begin();
  updateTime();
  fetchSunriseSunset();

  uint32_t current = _ntp.getEpochTime();
  uint32_t secOfDay = current % 86400;
  if (secOfDay < (2 * 3600)) {
    _updateDoneForDay = false;
  }
}

bool TimeSync::updateForToday() {
  if (_updateDoneForDay) return true;
  Serial.println("Attempting daily time/sunrise/sunset update...");
  bool success = updateTime() && fetchSunriseSunset();
  _updateDoneForDay = success;
  return success;
}

bool TimeSync::updateDone() const {
  return _updateDoneForDay;
}

void TimeSync::resetDailyFlags() {
  _updateDoneForDay = false;
}

DaylightTimes TimeSync::getCurrentTimes() {
  ensureWiFiConnected();
  _ntp.update();
  uint32_t now = _ntp.getEpochTime();
  uint32_t secMidnight = now % 86400;
  return DaylightTimes{ secMidnight, _sunrise, _sunset };
}

bool TimeSync::ensureWiFiConnected() {
  if (WiFi.status() == WL_CONNECTED) return true;
  Serial.println("Wi-Fi lost. Reconnecting...");
  WiFi.disconnect();
  WiFi.begin();
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
    delay(500);
    Serial.print(".");
  }
  bool connected = WiFi.status() == WL_CONNECTED;
  Serial.println(connected ? "\nReconnected to Wi-Fi." : "\nFailed to reconnect.");
  return connected;
}

bool TimeSync::updateTime() {
  Serial.println("Updating NTP time...");
  for (int i = 0; i < 5; i++) {
    if (_ntp.update()) {
      _lastSync = _ntp.getEpochTime();
      Serial.println("Time update successful.");
      return true;
    }
    Serial.println("NTP update failed, retrying...");
    delay(2000);
  }
  Serial.println("NTP update failed after multiple attempts.");
  return false;
}

bool TimeSync::fetchSunriseSunset() {
  HTTPClient http;
  String url = "http://api.sunrise-sunset.org/json?lat=" + String(_lat) +
               "&lng=" + String(_lon) + "&formatted=0";
  for (int i = 0; i < 5; i++) {
    Serial.println("Fetching sunrise/sunset times...");
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode == 200) {
      String payload = http.getString();
      Serial.println("Received JSON: " + payload);
      StaticJsonDocument<1024> doc;
      DeserializationError error = deserializeJson(doc, payload);
      http.end();
      if (error) {
        Serial.print("deserializeJson() failed: ");
        Serial.println(error.f_str());
        continue;
      }
      String sunrise = doc["results"]["sunrise"];
      String sunset  = doc["results"]["sunset"];
      int s = parseTime(sunrise) + _tz_offset;
      int t = parseTime(sunset)  + _tz_offset;
      if (s < 0) s += 86400;
      if (t < 0) t += 86400;
      if (s >= 86400) s -= 86400;
      if (t >= 86400) t -= 86400;
      _sunrise = s;
      _sunset = t;
      Serial.print("Parsed sunrise (sec since midnight): ");
      Serial.println(_sunrise);
      Serial.print("Parsed sunset  (sec since midnight): ");
      Serial.println(_sunset);
      return true;
    }
    Serial.println("Failed to fetch sunrise/sunset. Retrying...");
    http.end();
    delay(2000);
  }
  Serial.println("Failed to fetch sunrise/sunset after multiple attempts.");
  return false;
}

int TimeSync::parseTime(String timeStr) {
  int hour = timeStr.substring(11, 13).toInt();
  int minute = timeStr.substring(14, 16).toInt();
  int second = timeStr.substring(17, 19).toInt();
  return (hour * 3600) + (minute * 60) + second;
}

// End of file /////////////////////////////////////////////////////////////////
