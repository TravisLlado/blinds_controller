#include "WebInfo.h"

#include <HTTPClient.h>
#include <esp_sntp.h>
#include <time.h>

#include "config.h"

// Incremented by the SNTP task each time the clock is set from NTP.
static volatile uint32_t s_syncCount{0};

static void onTimeSync(struct timeval*) { s_syncCount = s_syncCount + 1; }

namespace WebInfo {

bool fetchGeo(float& lat, float& lon) {
  HTTPClient http;
  if (!http.begin("http://ip-api.com/line/?fields=status,lat,lon")) return false;

  const int code{http.GET()};
  if (code != 200) { http.end(); return false; }

  String body{http.getString()};
  http.end();

  // Response is three lines: status, lat, lon
  const int firstNL{body.indexOf('\n')};
  if (firstNL <= 0) return false;
  const int secondNL{body.indexOf('\n', firstNL + 1)};
  if (secondNL <= firstNL) return false;

  String status{body.substring(0, firstNL)};
  String latStr{body.substring(firstNL + 1, secondNL)};
  String lonStr{body.substring(secondNL + 1)};
  status.trim();
  latStr.trim();
  lonStr.trim();
  if (status != "success" || latStr.length() == 0 || lonStr.length() == 0) return false;

  lat = latStr.toFloat();
  lon = lonStr.toFloat();
  return true;
}

// Raw text of a top-level JSON field's value (number/bool/null), e.g. "true"
// or "1763512345". Enough for the sensor's flat, fixed-format object.
static bool jsonValue(const String& body, const char* key, String& out) {
  const String pattern{String("\"") + key + "\":"};
  const int start{body.indexOf(pattern)};
  if (start < 0) return false;
  int i{start + static_cast<int>(pattern.length())};
  int end{i};
  while (end < static_cast<int>(body.length()) && body[end] != ',' && body[end] != '}') end++;
  out = body.substring(i, end);
  out.trim();
  return out.length() > 0;
}

static bool jsonBool(const String& body, const char* key, bool& out) {
  String v;
  if (!jsonValue(body, key, v)) return false;
  if (v == "true")  { out = true;  return true; }
  if (v == "false") { out = false; return true; }
  return false;
}

static bool jsonInt64(const String& body, const char* key, int64_t& out) {
  String v;
  if (!jsonValue(body, key, v)) return false;
  char* endPtr{nullptr};
  const long long n{strtoll(v.c_str(), &endPtr, 10)};
  if (endPtr == v.c_str() || *endPtr != '\0') return false;  // also rejects null
  out = n;
  return true;
}

bool fetchBedState(const char* url, uint32_t timeoutMs, BedState& out) {
  HTTPClient http;
  http.setConnectTimeout(timeoutMs);
  http.setTimeout(timeoutMs);
  if (!http.begin(url)) return false;

  const int code{http.GET()};
  if (code != 200) { http.end(); return false; }

  String body{http.getString()};
  http.end();

  bool left, right;
  BedState s;
  if (!jsonBool(body, "left", left) || !jsonBool(body, "right", right) ||
      !jsonInt64(body, "time", s.time) || !jsonInt64(body, "lastInBed", s.lastInBed)) {
    return false;
  }
  s.occupied = left || right;
  out = s;
  return true;
}

void initTime(const char* posixTz) {
  sntp_set_time_sync_notification_cb(onTimeSync);
  configTzTime(posixTz, NTP_SERVER_1, NTP_SERVER_2);
}

bool syncClock(uint32_t timeoutMs) {
  const uint32_t before{s_syncCount};
  sntp_restart();  // send a request now rather than at the next poll interval
  const uint32_t start{millis()};
  while (millis() - start < timeoutMs) {
    if (s_syncCount != before) return true;
    delay(100);
  }
  return false;
}

bool clockValid() { return s_syncCount > 0; }

String formatLocalTime(int seconds) {
  if (seconds < 0) return String("??:??:??");
  seconds %= 86400;
  const int h{seconds / 3600};
  const int m{(seconds % 3600) / 60};
  const int s{seconds % 60};
  char buf[16];
  snprintf(buf, sizeof(buf), "%02d:%02d:%02d", h, m, s);
  return String(buf);
}

}  // namespace WebInfo
