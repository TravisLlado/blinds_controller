#pragma once

#include <Arduino.h>

namespace WebInfo {

  // One reading from the bed-occupancy sensor's GET /state endpoint.
  struct BedState {
    bool    occupied;   // either side of the bed occupied right now
    int64_t time;       // when the reading was taken, Unix seconds UTC
    int64_t lastInBed;  // end of last 5+ minute occupancy, Unix seconds UTC;
                        // 0 = none since the sensor booted
  };

  // GET the sensor's /state JSON. Returns false on any network/HTTP error,
  // a malformed body, or a null `time` (sensor clock not yet set).
  bool fetchBedState(const char* url, uint32_t timeoutMs, BedState& out);

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
