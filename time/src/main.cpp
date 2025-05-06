#include <Arduino.h>
#include "TimeSync.h"
#include "credentials.h"

constexpr int long SECOND{1U};
constexpr int long MINUTE{60U * SECOND};
constexpr int long HOUR{60U * MINUTE};

// San Francisco coordinates and PDT offset
constexpr float latitude{37.7749F};
constexpr float longitude{-122.4194F};
constexpr int timezone_offset{-7 * HOUR};

constexpr unsigned long updateWindowStart{2U * HOUR};
constexpr unsigned long updateWindowEnd{5U * HOUR};

constexpr uint8_t PIN_LED{8U};

TimeSync timeSync(latitude, longitude, timezone_offset);

void led_on()  { digitalWrite(PIN_LED, LOW); }
void led_off() { digitalWrite(PIN_LED, HIGH); }

String formatTimeOfDay(unsigned long sec) {
  unsigned long secOfDay = sec % (24U * HOUR);
  int hour = secOfDay / HOUR;
  int minute = (secOfDay % HOUR) / MINUTE;
  int second = secOfDay % MINUTE;
  char buf[9];
  sprintf(buf, "%02d:%02d:%02d", hour, minute, second);
  return String(buf);
}

void printDebug(const DaylightTimes times, const bool isDaytime) {
    // Build and print status line
    Serial.print("Current: ");
    Serial.print(formatTimeOfDay(times.current));
    Serial.print(" | Sunrise: ");
    Serial.print(formatTimeOfDay(times.sunrise));
    Serial.print(" | Sunset: ");
    Serial.print(formatTimeOfDay(times.sunset));
    Serial.print(" | Sun Out: ");
    Serial.print(isDaytime ? "YES" : "NO");
    Serial.println();
}

void flashLed(const bool isDaytime) {
  const int period = 500; // ms
  const int onTime  = period * (isDaytime ? 0.9F : 0.1F);
  const int offTime = period - onTime;
  led_off(); delay(offTime);
  led_on();  delay(onTime);
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  led_off();

  Serial.println("Starting time-sync controller...");
  timeSync.begin(ssid, password);
}

void loop() {
  const DaylightTimes times = timeSync.getCurrentTimes();
  const bool isDaytime = (times.current >= times.sunrise && times.current < times.sunset);

  printDebug(times, isDaytime);
  flashLed(isDaytime);  // blocks for 1sec 

  // Daily update window
  if ((times.current >= updateWindowStart) && (times.current < updateWindowEnd) && !timeSync.updateDone()) {
    if (timeSync.updateForToday()) {
      Serial.println("Daily time/sunrise/sunset update succeeded.");
    } else {
      Serial.println("Daily update failed. Will retry.");
    }
  }

  // If window passed without update, give up
  if ((times.current >= updateWindowEnd) && !timeSync.updateDone()) {
    Serial.println("Update window expired. Using previous values.");
  }

  // Reset daily flags at start of new day
  if (times.current < HOUR) {
    timeSync.resetDailyFlags();
  }
}
