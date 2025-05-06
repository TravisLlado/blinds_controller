// TimeSync.h //////////////////////////////////////////////////////////////////
#pragma once

#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ArduinoJson.h>

struct DaylightTimes {
  uint32_t current;  // Seconds since midnight
  uint32_t sunrise;  // Seconds since midnight
  uint32_t sunset;   // Seconds since midnight
};

class TimeSync {
public:
  TimeSync(float latitude, float longitude, int timezone_offset_sec);

  // Initializes Wi-Fi and NTP client, performs first update
  void begin(const char* ssid, const char* password);

  // Call this once daily in the update window (e.g. 2–5 AM)
  bool updateForToday();

  // Returns current time and today's sunrise/sunset (all in seconds since midnight)
  DaylightTimes getCurrentTimes();

  // Returns true if daily update has been completed
  bool updateDone() const;

  // Call this at midnight to reset internal flags
  void resetDailyFlags();

private:
  bool ensureWiFiConnected();
  bool updateTime();
  bool fetchSunriseSunset();
  int parseTime(String timeStr);

  float _lat;
  float _lon;
  int _tz_offset;

  WiFiUDP _udp;
  NTPClient _ntp;
  uint32_t _lastSync;

  uint32_t _sunrise;
  uint32_t _sunset;
  bool _updateDoneForDay;
};

// End of file /////////////////////////////////////////////////////////////////
