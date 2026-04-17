#pragma once

#include <Arduino.h>

namespace WebInfo {

  // Fetch approximate geolocation and current UTC offset from the device's public IP.
  // Returns true on success; fills lat, lon in decimal degrees, tzOffsetMinutes as the
  // current UTC offset in minutes (DST-aware, e.g. -480 for PST, -420 for PDT).
  bool fetchGeo(float& lat, float& lon, int& tzOffsetMinutes);

  // Configure the ESP32 system clock via NTP using the given UTC offset.
  // Returns true if NTP sync succeeded.
  bool initTime(int tzOffsetMinutes);

  // Fetch today's sunrise and sunset times for the given location.
  // tzOffsetMinutes should be the value filled by fetchGeo().
  // Returns true on success; fills sunriseSec and sunsetSec as local seconds
  // since midnight (0-86399).
  bool fetchSunTimes(float lat, float lon, int tzOffsetMinutes,
                     int& sunriseSec, int& sunsetSec);

  // Format a local seconds-since-midnight value as "HH:MM:SS".
  // Returns "??:??:??" for negative input.
  String formatLocalTime(int seconds);

}  // namespace WebInfo
