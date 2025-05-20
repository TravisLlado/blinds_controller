// TimeSync.h //////////////////////////////////////////////////////////////////

#pragma once

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// Time units //////////////////////////////////////////////////////////////////

constexpr int32_t SECOND{1};
constexpr int32_t MINUTE{60 * SECOND};
constexpr int32_t HOUR{60 * MINUTE};
constexpr int32_t DAY{24 * HOUR};

struct DaylightTimes {
  uint32_t current;  // Seconds since midnight
  uint32_t sunrise;  // Seconds since midnight
  uint32_t sunset;   // Seconds since midnight
};

class TimeSync {
public:
  // Constructor with location and timezone parameters
  TimeSync(const char* wifi_ssid, const char* wifi_password, const float latitude, const float longitude, const int timezone_offset_sec);
  
  // Function prototypes
  DaylightTimes fetchTimeData();
  
private:
  bool connectToWiFi();
  void disconnectWiFi();
  bool fetchNTPTime(uint32_t& currentTime);
  bool fetchSunriseSunset(uint32_t& sunrise, uint32_t& sunset);
  int parseTime(String timeStr);
  
  // Location parameters
  const char* wifi_ssid_;
  const char* wifi_password_;
  const float _lat;
  const float _lon;
  const int _tz_offset;
  
  // UDP for NTP
  WiFiUDP _udp;
};

// End of file /////////////////////////////////////////////////////////////////
