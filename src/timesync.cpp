// TimeSync.cpp ////////////////////////////////////////////////////////////////

#include "timesync.h"

TimeSync::TimeSync(const char* wifi_ssid, const char* wifi_password, const float latitude, const float longitude, const int timezone_offset_sec)
  : wifi_ssid_(wifi_ssid), wifi_password_(wifi_password), _lat(latitude), _lon(longitude), _tz_offset(timezone_offset_sec) {
  // Nothing else to initialize
}

DaylightTimes TimeSync::fetchTimeData() {
  DaylightTimes result = {0, 0, 0}; // Initialize with zeros
  
  // Step 1: Connect to WiFi
  if (!connectToWiFi()) {
    Serial.println("Failed to connect to WiFi");
    disconnectWiFi();
    return result;
  }
  
  // Step 2: Get current time via NTP and set system time
  bool timeSuccess = fetchNTPTime(result.current);
  
  // Step 3: Get sunrise/sunset times
  bool sunSuccess = fetchSunriseSunset(result.sunrise, result.sunset);
  
  // Step 4: Disconnect from WiFi
  disconnectWiFi();
  
  // Return the data (will contain zeros for any values that failed to fetch)
  return result;
}

bool TimeSync::connectToWiFi() {
  Serial.print("Connecting to WiFi...");
  
  // Check if already connected
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Already connected");
    return true;
  }
  
  // Start connection - using credentials from credentials.h
  WiFi.begin(wifi_ssid_, wifi_password_);
  
  // Wait for connection with timeout
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 20000) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\r\nConnected to WiFi");
    return true;
  } else {
    Serial.println("\r\nFailed to connect to WiFi");
    return false;
  }
}

void TimeSync::disconnectWiFi() {
  Serial.println("Disconnecting from WiFi");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

bool TimeSync::fetchNTPTime(uint32_t& currentTime) {
  Serial.println("Fetching NTP time...");
  
  // Configure time
  configTime(_tz_offset, 0, "pool.ntp.org");
  
  // Wait for time to be set
  Serial.println("Waiting for system time to be set...");
  int retry = 0;
  time_t now = 0;
  struct tm timeinfo;
  
  while (!getLocalTime(&timeinfo) && retry < 10) {
    Serial.print(".");
    delay(500);
    retry++;
  }
  
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return false;
  }
  
  // Convert to seconds since midnight
  currentTime = (timeinfo.tm_hour * 3600) + 
               (timeinfo.tm_min * 60) + 
                timeinfo.tm_sec;
  
  char timeString[50];
  strftime(timeString, sizeof(timeString), "%A, %B %d %Y %H:%M:%S", &timeinfo);
  Serial.print("System time set: ");
  Serial.println(timeString);
  
  return true;
}

bool TimeSync::fetchSunriseSunset(uint32_t& sunrise, uint32_t& sunset) {
  HTTPClient http;
  String url = "http://api.sunrise-sunset.org/json?lat=" + String(_lat, 6) +
               "&lng=" + String(_lon, 6) + "&formatted=0";
               
  Serial.println("Fetching sunrise/sunset times...");
  Serial.println(url);
  
  bool success = false;
  
  // Try multiple times
  for (int attempt = 0; attempt < 5 && !success; attempt++) {
    http.begin(url);
    int httpCode = http.GET();
    
    if (httpCode == 200) {
      String payload = http.getString();
      http.end();
      
      // Parse JSON response
      StaticJsonDocument<1024> doc;
      DeserializationError error = deserializeJson(doc, payload);
      
      if (error) {
        Serial.print("JSON parsing failed: ");
        Serial.println(error.f_str());
        delay(1000);
        continue;
      }
      
      // Extract data
      String sunriseStr = doc["results"]["sunrise"];
      String sunsetStr = doc["results"]["sunset"];
      
      // Convert to seconds since midnight with timezone adjustment
      int sunriseSec = parseTime(sunriseStr) + _tz_offset;
      int sunsetSec = parseTime(sunsetStr) + _tz_offset;
      
      // Normalize to 0-86399 range (seconds in a day)
      if (sunriseSec < 0) sunriseSec += 86400;
      if (sunsetSec < 0) sunsetSec += 86400;
      if (sunriseSec >= 86400) sunriseSec -= 86400;
      if (sunsetSec >= 86400) sunsetSec -= 86400;
      
      sunrise = sunriseSec;
      sunset = sunsetSec;
      
      Serial.print("Sunrise: ");
      Serial.print(sunrise / 3600);
      Serial.print(":");
      Serial.print((sunrise % 3600) / 60);
      Serial.print(":");
      Serial.println(sunrise % 60);
      
      Serial.print("Sunset: ");
      Serial.print(sunset / 3600);
      Serial.print(":");
      Serial.print((sunset % 3600) / 60);
      Serial.print(":");
      Serial.println(sunset % 60);
      
      success = true;
    } else {
      Serial.print("HTTP request failed with code: ");
      Serial.println(httpCode);
      http.end();
      delay(1000);
    }
  }
  
  if (!success) {
    Serial.println("Failed to fetch sunrise/sunset after multiple attempts");
  }
  
  return success;
}

int TimeSync::parseTime(String timeStr) {
  // Parse time in ISO 8601 format (e.g., "2022-04-01T05:45:28+00:00")
  int hour = timeStr.substring(11, 13).toInt();
  int minute = timeStr.substring(14, 16).toInt();
  int second = timeStr.substring(17, 19).toInt();
  
  return (hour * 3600) + (minute * 60) + second;
}

// End of file /////////////////////////////////////////////////////////////////
