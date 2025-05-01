////////////////////////////////////////////////////////////////////////////////
// Living Room Blinds Control Code, now without Alexa!
// written by Travis Llado
// last edited 2025-02-20
////////////////////////////////////////////////////////////////////////////////

#include <cmath>
#include <stdio.h>
#include <cstdarg>
#include "blinds.h"
#include "config.h"
#include "hardware/rtc.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

////////////////////////////////////////////////////////////////////////////////

#define SUMMER_SOLSTICE_SUNRISE_TIME  static_cast<float>(SUMMER_SOLSTICE_SUNRISE_HOUR * 3600U + SUMMER_SOLSTICE_SUNRISE_MIN * 60U)
#define SUMMER_SOLSTICE_SUNSET_TIME   static_cast<float>(SUMMER_SOLSTICE_SUNSET_HOUR  * 3600U + SUMMER_SOLSTICE_SUNSET_MIN  * 60U)
#define WINTER_SOLSTICE_SUNRISE_TIME  static_cast<float>(WINTER_SOLSTICE_SUNRISE_HOUR * 3600U + WINTER_SOLSTICE_SUNRISE_MIN * 60U)
#define WINTER_SOLSTICE_SUNSET_TIME   static_cast<float>(WINTER_SOLSTICE_SUNSET_HOUR  * 3600U + WINTER_SOLSTICE_SUNSET_MIN  * 60U)

#define SUNRISE_VARIATION (WINTER_SOLSTICE_SUNRISE_TIME - SUMMER_SOLSTICE_SUNRISE_TIME)
#define SUNSET_VARIATION  (SUMMER_SOLSTICE_SUNSET_TIME - WINTER_SOLSTICE_SUNSET_TIME)

////////////////////////////////////////////////////////////////////////////////

float convert_date_to_float(const datetime_t& date);
float convert_time_to_float(const datetime_t& time);
datetime_t convert_uint_to_time(const uint32_t time);
datetime_t calc_sunrise(const datetime_t& date);
datetime_t calc_sunset(const datetime_t& date);

////////////////////////////////////////////////////////////////////////////////

float convert_date_to_float(const datetime_t& date) {
  // Ignore leap years
  static constexpr uint32_t days_before_month[12U] = {0U, 31U, 59U, 90U, 120U, 151U, 181U, 212U, 243U, 273U, 304U, 334U};
  return static_cast<float>(days_before_month[date.month - 1] + date.day);
}

////////////////////////////////////////////////////////////////////////////////

float convert_time_to_float(const datetime_t& time) {
  return static_cast<float>(time.hour*3600 + time.min*60 + time.sec);
}

////////////////////////////////////////////////////////////////////////////////

datetime_t convert_uint_to_time(const uint32_t time) {
  return datetime_t{
    .year  = 0,
    .month = 0,
    .day   = 0,
    .dotw  = 0,
    .hour  = static_cast<int8_t>((time % (3600U * 24U)) / 3600U),
    .min   = static_cast<int8_t>((time % 3600U) / 60U),
    .sec   = static_cast<int8_t>(time % 60U)
  };
}

////////////////////////////////////////////////////////////////////////////////

datetime_t calc_sunrise(const datetime_t& date) {
  const float day_offset{convert_date_to_float(date) - SUMMER_SOLSTICE_DATE};
  const float time_offset{SUNRISE_VARIATION*(-cosf(2.0F*static_cast<float>(M_PI)*day_offset/365.0F)/2.0F + 0.5F)};
  const float todays_sunrise_time{SUMMER_SOLSTICE_SUNRISE_TIME + time_offset + SUNRISE_MARGIN};
  datetime_t todays_sunrise{convert_uint_to_time(todays_sunrise_time)};
  todays_sunrise.year = date.year;
  todays_sunrise.month = date.month;
  todays_sunrise.day = date.day;

  return todays_sunrise;
}

////////////////////////////////////////////////////////////////////////////////

datetime_t calc_sunset(const datetime_t& date) {
  const float day_offset{convert_date_to_float(date) - SUMMER_SOLSTICE_DATE};
  const float time_offset{SUNSET_VARIATION*(-cosf(2.0F*static_cast<float>(M_PI)*day_offset/365.0F)/2.0F + 0.5F)};
  const float todays_sunset_time{SUMMER_SOLSTICE_SUNSET_TIME - time_offset - SUNSET_MARGIN};
  datetime_t todays_sunset{convert_uint_to_time(todays_sunset_time)};
  todays_sunset.year = date.year;
  todays_sunset.month = date.month;
  todays_sunset.day = date.day;

  return todays_sunset;
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

int main() {
  #if DEBUG_SERIAL
    stdio_init_all();
  #endif

  #if DEBUG_LED
    cyw43_arch_init();
  #endif

  datetime_t now{
    .year = 2000,
    .month = STARTUP_MON,
    .day = STARTUP_DAY,
    .dotw = 0,
    .hour = STARTUP_HOUR,
    .min = STARTUP_MIN,
    .sec = 0
  };

  rtc_init();
  rtc_set_datetime(&now);

  // Sometimes the motors run with USB power ... give time to unplug after flashing
  for (int i = 5; i > 0; i--) {
    debug_led(true);
    debug_printf("%u\n", i);
    sleep_ms(100U);
    debug_led(false);
    sleep_ms(900U);
  }

  blinds_init();

  datetime_t previous{now};
  datetime_t todays_sunrise{calc_sunrise(now)};
  datetime_t todays_sunset{calc_sunset(now)};

  {
    // Put in correct position immediately
    const bool after_sunrise{convert_time_to_float(now) > convert_time_to_float(todays_sunrise)};
    const bool before_sunset{convert_time_to_float(now) < convert_time_to_float(todays_sunset)};
    const bool blinds_should_be_open{after_sunrise && before_sunset};

    if (blinds_should_be_open && !BLINDS_ARE_OPEN) {
      debug_led(true);
      debug_printf("opening blinds ...\n");
      blinds_open();
      debug_led(false);
      debug_printf("    blinds open\n");
    } else if (!blinds_should_be_open && BLINDS_ARE_OPEN) {
      debug_led(true);
      debug_printf("closing blinds ...\n");
      blinds_close();
      debug_led(false);
      debug_printf("    blinds closed\n");
    }
  }

  while (true) {
    
    rtc_get_datetime(&now);
    if (now.sec != previous.sec) {
      debug_led(true);
      debug_printf("now is %02u/%02u %02u:%02u:%02u\t sunrise %02u:%02u:%02u\t sunset %02u:%02u:%02u\t\n", now.month, now.day, now.hour, now.min, now.sec, todays_sunrise.hour, todays_sunrise.min, todays_sunrise.sec, todays_sunset.hour, todays_sunset.min, todays_sunset.sec);

      if (now.day != previous.day) {
        debug_printf("it's a new day with new sunrise and sunset times\n");
        todays_sunrise = calc_sunrise(now);
        todays_sunset = calc_sunset(now);
      }
      if ((now.hour == todays_sunrise.hour) && (now.min == todays_sunrise.min) && (now.sec == todays_sunrise.sec)) {
        debug_led(true);
        debug_printf("opening blinds ...\n");
        blinds_open();
        debug_led(false);
        debug_printf("    blinds open\n");
      } else if ((now.hour == todays_sunset.hour) && (now.min == todays_sunset.min) && (now.sec == todays_sunset.sec)) {
        debug_led(true);
        debug_printf("closing blinds ...\n");
        blinds_close();
        debug_led(false);
        debug_printf("    blinds closed\n");
      }

      previous = now;
    } else {
      debug_led(false);
    }

    sleep_ms(100);
  }
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
