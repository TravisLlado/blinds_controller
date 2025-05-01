////////////////////////////////////////////////////////////////////////////////
// Living Room Blinds Control Code, now without Alexa!
// written by Travis Llado
// last edited 2025-02-20
////////////////////////////////////////////////////////////////////////////////

#include <cmath>
#include <cstdarg>
#include "blinds.h"
#include "config.h"
#include "hardware/rtc.h"
#include "pico/stdlib.h"
#include "pwm_rtc.h"

#if DEBUG_SERIAL
  #include <stdio.h>
#endif

#if DEBUG_LED
  #include "pico/cyw43_arch.h"
#endif

////////////////////////////////////////////////////////////////////////////////

#define SUMMER_SOLSTICE_SUNRISE_TIME  static_cast<float>(SUMMER_SOLSTICE_SUNRISE_HOUR * 3600U + SUMMER_SOLSTICE_SUNRISE_MIN * 60U)
#define SUMMER_SOLSTICE_SUNSET_TIME   static_cast<float>(SUMMER_SOLSTICE_SUNSET_HOUR  * 3600U + SUMMER_SOLSTICE_SUNSET_MIN  * 60U)
#define WINTER_SOLSTICE_SUNRISE_TIME  static_cast<float>(WINTER_SOLSTICE_SUNRISE_HOUR * 3600U + WINTER_SOLSTICE_SUNRISE_MIN * 60U)
#define WINTER_SOLSTICE_SUNSET_TIME   static_cast<float>(WINTER_SOLSTICE_SUNSET_HOUR  * 3600U + WINTER_SOLSTICE_SUNSET_MIN  * 60U)

#define SUNRISE_VARIATION (WINTER_SOLSTICE_SUNRISE_TIME - SUMMER_SOLSTICE_SUNRISE_TIME)
#define SUNSET_VARIATION  (SUMMER_SOLSTICE_SUNSET_TIME - WINTER_SOLSTICE_SUNSET_TIME)

blinds_motors blinds;
std::tm todays_sunrise{};
std::tm todays_sunset{};

////////////////////////////////////////////////////////////////////////////////

float convert_date_to_float(const std::tm& date);
float convert_time_to_float(const std::tm& time);
std::tm convert_uint_to_time(const uint32_t time);
std::tm calc_sunrise(const std::tm& date);
std::tm calc_sunset(const std::tm& date);

////////////////////////////////////////////////////////////////////////////////

float convert_date_to_float(const std::tm& now) {
  static constexpr uint32_t days_before_month[12U] = {0U, 31U, 59U, 90U, 120U, 151U, 181U, 212U, 243U, 273U, 304U, 334U};
  return static_cast<float>(days_before_month[now.tm_mon] + now.tm_mday);
}

////////////////////////////////////////////////////////////////////////////////

float convert_time_to_float(const std::tm& now) {
  return static_cast<float>(now.tm_hour*3600 + now.tm_min*60 + now.tm_sec);
}

////////////////////////////////////////////////////////////////////////////////

std::tm convert_uint_to_time(const uint32_t time) {
  std::tm datetime{};

  datetime.tm_hour = static_cast<int8_t>((time % (3600U * 24U)) / 3600U);
  datetime.tm_min = static_cast<int8_t>((time % 3600U) / 60U);
  datetime.tm_sec = static_cast<int8_t>(time % 60U);

  return datetime;
}

////////////////////////////////////////////////////////////////////////////////

std::tm calc_sunrise(const std::tm& datetime) {
  const float day_offset{convert_date_to_float(datetime) - SUMMER_SOLSTICE_DATE};
  const float time_offset{SUNRISE_VARIATION*(-cosf(2.0F*static_cast<float>(M_PI)*day_offset/365.0F)/2.0F + 0.5F)};
  const float sunrise_time{SUMMER_SOLSTICE_SUNRISE_TIME + time_offset + SUNRISE_MARGIN};
  std::tm sunrise{convert_uint_to_time(sunrise_time)};
  sunrise.tm_year = datetime.tm_year;
  sunrise.tm_mon = datetime.tm_mon;
  sunrise.tm_mday = datetime.tm_mday;

  return sunrise;
}

////////////////////////////////////////////////////////////////////////////////

std::tm calc_sunset(const std::tm& datetime) {
  const float day_offset{convert_date_to_float(datetime) - SUMMER_SOLSTICE_DATE};
  const float time_offset{SUNSET_VARIATION*(-cosf(2.0F*static_cast<float>(M_PI)*day_offset/365.0F)/2.0F + 0.5F)};
  const float sunset_time{SUMMER_SOLSTICE_SUNSET_TIME - time_offset - SUNSET_MARGIN};
  std::tm sunset{convert_uint_to_time(sunset_time)};
  sunset.tm_year = datetime.tm_year;
  sunset.tm_mon = datetime.tm_mon;
  sunset.tm_mday = datetime.tm_mday;

  return sunset;
}

////////////////////////////////////////////////////////////////////////////////
  
void debug_led(const bool on) {
  #if DEBUG_LED
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
  #endif
}

////////////////////////////////////////////////////////////////////////////////

void debug_printf(const char* format, ...) {
  #if DEBUG_SERIAL
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
  #endif
}

////////////////////////////////////////////////////////////////////////////////

void callbackDebug(const std::tm& now) {
  debug_led(true);
  debug_printf("Current Time: %04d-%02d-%02d %02d:%02d:%02d\n", now.tm_year + 1900,
    now.tm_mon + 1, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec);
  debug_led(false);
}

////////////////////////////////////////////////////////////////////////////////

void blindsInitialPosition(const std::tm now) {
  const bool after_sunrise{convert_time_to_float(now) > convert_time_to_float(todays_sunrise)};
  const bool before_sunset{convert_time_to_float(now) < convert_time_to_float(todays_sunset)};
  const bool blinds_should_be_open{after_sunrise && before_sunset};

  debug_printf("now is %02u/%02u %02u:%02u:%02u\t sunrise %02u:%02u:%02u\t sunset %02u:%02u:%02u\t\n", now.tm_mon, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec, todays_sunrise.tm_hour, todays_sunrise.tm_min, todays_sunrise.tm_sec, todays_sunset.tm_hour, todays_sunset.tm_min, todays_sunset.tm_sec);
  debug_printf("after_sunrise %u before_sunset %u\n", after_sunrise, before_sunset);

  if (blinds_should_be_open && !BLINDS_ARE_OPEN) {
    debug_led(true);
    debug_printf("opening blinds ...\n");
    blinds.open();
    debug_led(false);
    debug_printf("    blinds open\n");
  } else if (!blinds_should_be_open && BLINDS_ARE_OPEN) {
    debug_led(true);
    debug_printf("closing blinds ...\n");
    blinds.close();
    debug_led(false);
    debug_printf("    blinds closed\n");
  }
}

////////////////////////////////////////////////////////////////////////////////

void callbackBlinds(const std::tm& now) {
  if ((now.tm_hour == 0U) && (now.tm_min == 0U) && (now.tm_sec == 0U)) {
    debug_printf("it's a new day with new sunrise and sunset times\n");
    todays_sunrise = calc_sunrise(now);
    todays_sunset = calc_sunset(now);
  }

  if ((now.tm_hour == todays_sunrise.tm_hour) && (now.tm_min == todays_sunrise.tm_min) && (now.tm_sec == todays_sunrise.tm_sec)) {
    debug_led(true);
    debug_printf("opening blinds ...\n");
    blinds.open();
    debug_led(false);
    debug_printf("    blinds open\n");
  } else if ((now.tm_hour == todays_sunset.tm_hour) && (now.tm_min == todays_sunset.tm_min) && (now.tm_sec == todays_sunset.tm_sec)) {
    debug_led(true);
    debug_printf("closing blinds ...\n");
    blinds.close();
    debug_led(false);
    debug_printf("    blinds closed\n");
  }
}

////////////////////////////////////////////////////////////////////////////////

int main() {
  #if DEBUG_SERIAL
    stdio_init_all();
  #endif

  #if DEBUG_LED
    cyw43_arch_init();
    debug_led(true);
  #endif

  PWM_RTC& rtc = PWM_RTC::getInstance();
  rtc.setStartTime(2000, STARTUP_MON, STARTUP_DAY, STARTUP_HOUR, STARTUP_MIN, 0);
  rtc.registerCallback(callbackDebug);

  // Sometimes motors run with USB power. Give time to unplug after flashing.
  sleep_ms(5000U);

  todays_sunrise = calc_sunrise(rtc.getCurrentTime());
  todays_sunset = calc_sunset(rtc.getCurrentTime());

  blindsInitialPosition(rtc.getCurrentTime());
  rtc.registerCallback(callbackBlinds);

  while (true) {
    tight_loop_contents();
  }
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
