#pragma once

#include <Arduino.h>

namespace WebInfo {

  // Fetch approximate geolocation from the device's public IP (ip-api.com).
  // Returns true on success; fills lat, lon in decimal degrees.
  bool fetchGeo(float& lat, float& lon);

  // Start SNTP and set the local timezone (POSIX TZ string, DST rules
  // included -- see TIMEZONE_POSIX in config.h). Call once at boot. The ESP32
  // keeps re-syncing in the background after this.
  void initTime(const char* posixTz);

  // Request an NTP sync now and wait up to timeoutMs for it to complete.
  // Returns true if the clock was updated from NTP. On failure the clock keeps
  // running from its last sync, and local time/DST stay correct.
  bool syncClock(uint32_t timeoutMs);

  // True once the clock has been set from NTP at least once since boot.
  bool clockValid();

  // Format a local seconds-since-midnight value as "HH:MM:SS".
  // Returns "??:??:??" for negative input.
  String formatLocalTime(int seconds);

}  // namespace WebInfo
