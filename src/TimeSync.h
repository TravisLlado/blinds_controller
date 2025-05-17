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
  TimeSync(float latitude, float longitude, int timezone_offset_sec);
  
  // Function prototypes
  DaylightTimes fetchTimeData();
  
private:
  bool connectToWiFi();
  void disconnectWiFi();
  bool fetchNTPTime(uint32_t& currentTime);
  bool fetchSunriseSunset(uint32_t& sunrise, uint32_t& sunset);
  int parseTime(String timeStr);
  
  // Location parameters
  float _lat;
  float _lon;
  int _tz_offset;
  
  // UDP for NTP
  WiFiUDP _udp;
};

// End of file /////////////////////////////////////////////////////////////////
