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
