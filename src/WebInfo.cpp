#include "WebInfo.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>

// Convert an ISO 8601 UTC timestamp ("...THH:MM:SSZ") to local seconds since
// midnight by applying tzOffsetMinutes. Returns -1 on parse failure.
static int isoUtcToLocalSeconds(const String& isoUtc, int tzOffsetMinutes) {
  if (isoUtc.length() < 19) return -1;

  const int h{isoUtc.substring(11, 13).toInt()};
  const int m{isoUtc.substring(14, 16).toInt()};
  const int s{isoUtc.substring(17, 19).toInt()};

  long totalMin{h * 60L + m + tzOffsetMinutes};
  while (totalMin < 0)    totalMin += 1440;
  while (totalMin >= 1440) totalMin -= 1440;

  const int lh{(int)(totalMin / 60L)};
  const int lm{(int)(totalMin % 60L)};
  return lh * 3600 + lm * 60 + s;
}

namespace WebInfo {

bool fetchGeo(float& lat, float& lon, int& tzOffsetMinutes) {
  HTTPClient http;
  if (!http.begin("http://ip-api.com/line/?fields=status,lat,lon,offset")) return false;

  const int code{http.GET()};
  if (code != 200) { http.end(); return false; }

  String body{http.getString()};
  http.end();

  const int firstNL{body.indexOf('\n')};
  if (firstNL <= 0) return false;

  String status{body.substring(0, firstNL)};
  status.trim();
  if (status != "success") return false;

  const int secondNL{body.indexOf('\n', firstNL + 1)};
  if (secondNL <= firstNL) return false;

  const int thirdNL{body.indexOf('\n', secondNL + 1)};
  if (thirdNL <= secondNL) return false;

  String latStr{body.substring(firstNL + 1, secondNL)};
  String lonStr{body.substring(secondNL + 1, thirdNL)};
  String offsetStr{body.substring(thirdNL + 1)};
  latStr.trim();
  lonStr.trim();
  offsetStr.trim();

  const float parsedLat{latStr.toFloat()};
  const float parsedLon{lonStr.toFloat()};
  if (isnan(parsedLat) || isnan(parsedLon)) return false;

  lat = parsedLat;
  lon = parsedLon;
  tzOffsetMinutes = offsetStr.toInt() / 60;
  return true;
}

bool initTime(int tzOffsetMinutes) {
  configTime((long)tzOffsetMinutes * 60, 0, "pool.ntp.org", "time.nist.gov");

  struct tm timeinfo;
  for (int i{0}; i < 10; ++i) {
    if (getLocalTime(&timeinfo)) return true;
    delay(500);
  }
  return false;
}

bool fetchSunTimes(float lat, float lon, int tzOffsetMinutes,
                   int& sunriseSec, int& sunsetSec) {
  String url{"https://api.sunrise-sunset.org/json?formatted=0&lat="};
  url += String(lat, 4);
  url += "&lng=";
  url += String(lon, 4);

  WiFiClientSecure client;
  client.setInsecure();  // skip certificate verification for demo

  HTTPClient http;
  if (!http.begin(client, url)) return false;

  const int code{http.GET()};
  if (code != 200) { http.end(); return false; }

  String body{http.getString()};
  http.end();

  int srKey{body.indexOf("\"sunrise\":\"")};
  int ssKey{body.indexOf("\"sunset\":\"")};
  if (srKey < 0 || ssKey < 0) return false;

  srKey += 11;
  const int srEnd{body.indexOf('"', srKey)};
  ssKey += 10;
  const int ssEnd{body.indexOf('"', ssKey)};
  if (srEnd <= srKey || ssEnd <= ssKey) return false;

  const int rise{isoUtcToLocalSeconds(body.substring(srKey, srEnd), tzOffsetMinutes)};
  const int set {isoUtcToLocalSeconds(body.substring(ssKey, ssEnd), tzOffsetMinutes)};
  if (rise < 0 || set < 0) return false;

  sunriseSec = rise;
  sunsetSec  = set;
  return true;
}

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
