#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ArduinoJson.h>

// Wi-Fi Credentials
const char* ssid = "";
const char* password = "";

// GPS Coordinates for San Francisco
const float latitude = 37.7749;
const float longitude = -122.4194;
const int timezone_offset = -8 * 3600; // Pacific Standard Time (UTC-8)

// NTP Client for time sync
WiFiUDP udp;
NTPClient timeClient(udp, "pool.ntp.org", timezone_offset, 60000);

// Time tracking
unsigned long epochTime;
unsigned long lastSync;

// Sunrise/Sunset times
int sunrise_time = 0;
int sunset_time = 0;

// LED Pin
const int LED_PIN = 8;

void ensureWiFiConnected() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Wi-Fi lost. Reconnecting...");
        WiFi.disconnect();
        WiFi.reconnect();
        unsigned long startAttemptTime = millis();
        
        while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
            delay(500);
            Serial.print(".");
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\nReconnected to Wi-Fi.");
        } else {
            Serial.println("\nFailed to reconnect.");
        }
    }
}

void updateTime() {
    Serial.println("Updating NTP time...");
    for (int i = 0; i < 5; i++) {
        if (timeClient.update()) {
            epochTime = timeClient.getEpochTime();
            lastSync = millis();
            Serial.println("Time update successful.");
            return;
        }
        Serial.println("NTP update failed, retrying...");
        delay(2000);
    }
    Serial.println("NTP update failed after multiple attempts.");
}

void fetchSunriseSunset() {
    HTTPClient http;
    String url = "http://api.sunrise-sunset.org/json?lat=" + String(latitude) + "&lng=" + String(longitude) + "&formatted=0";
    
    for (int i = 0; i < 5; i++) {
        Serial.println("Fetching sunrise/sunset times...");
        http.begin(url);
        int httpCode = http.GET();
        
        if (httpCode == 200) {
            String payload = http.getString();
            Serial.println("Received JSON: " + payload);

            DynamicJsonDocument doc(1024);
            deserializeJson(doc, payload);

            String sunrise = doc["results"]["sunrise"];
            String sunset = doc["results"]["sunset"];

            sunrise_time = parseTime(sunrise) + timezone_offset;
            sunset_time = parseTime(sunset) + timezone_offset;
            http.end();
            return;
        }
        
        Serial.println("Failed to fetch sunrise/sunset. Retrying...");
        http.end();
        delay(2000);
    }
    Serial.println("Failed to fetch sunrise/sunset after multiple attempts.");
}

int parseTime(String timeStr) {
    int hour = timeStr.substring(11, 13).toInt();
    int minute = timeStr.substring(14, 16).toInt();
    int second = timeStr.substring(17, 19).toInt();
    return hour * 3600 + minute * 60 + second;
}

void setup() {
    Serial.begin(115200);
    Serial.println("Starting...");

    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);

    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nConnected to Wi-Fi");

    timeClient.begin();
    updateTime();
    fetchSunriseSunset();
}

void loop() {
    ensureWiFiConnected();
    epochTime += (millis() - lastSync) / 1000;
    lastSync = millis();
    int secondsSinceMidnight = epochTime % 86400;

    Serial.print("Current Time (UTC): ");
    Serial.print(epochTime);
    Serial.print(" | Daytime: ");
    
    bool isDaytime = (secondsSinceMidnight >= sunrise_time && secondsSinceMidnight < sunset_time);
    Serial.println(isDaytime ? "YES" : "NO");

    digitalWrite(LED_PIN, isDaytime ? LOW : HIGH);

    if (secondsSinceMidnight == 2 * 3600) { 
        Serial.println("Updating time and sunrise/sunset times...");
        updateTime();
        fetchSunriseSunset();
    }

    delay(1000);
}
