////////////////////////////////////////////////////////////////////////////////
// Living Room Blinds Control Code, now without Alexa!
// written by Travis Llado
// last edited 2025-02-16
////////////////////////////////////////////////////////////////////////////////

#include <cmath>
#include "blinds.h"
#include "config.h"
#include "hardware/rtc.h"
#include "pico/stdlib.h"

////////////////////////////////////////////////////////////////////////////////

static constexpr datetime_t summer_solstice_sunrise{
  .year  = 0,
  .month = 6,
  .day   = 20,
  .dotw  = 0,
  .hour  = 4,
  .min   = 47,
  .sec   = 0
};

static constexpr datetime_t summer_solstice_sunset{
  .year  = 0,
  .month = 6,
  .day   = 20,
  .dotw  = 0,
  .hour  = 19,
  .min   = 34,
  .sec   = 0
};

static constexpr datetime_t winter_solstice_sunrise{
  .year  = 0,
  .month = 12,
  .day   = 20,
  .dotw  = 0,
  .hour  = 7,
  .min   = 21,
  .sec   = 0
};

static constexpr datetime_t winter_solstice_sunset{
  .year  = 0,
  .month = 12,
  .day   = 20,
  .dotw  = 0,
  .hour  = 16,
  .min   = 53,
  .sec   = 0
};

////////////////////////////////////////////////////////////////////////////////

float convert_date_to_float(const datetime_t& date) {
  static constexpr uint32_t days_before_month[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
  return static_cast<float>(days_before_month[date.month - 1] + date.day);  // ignore leap years
}

float convert_time_to_float(const datetime_t& time) {
  return static_cast<float>(time.hour*3600 + time.min*60 + time.sec);
}

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

datetime_t sunrise(datetime_t today) {
  const float variation_in_secs{convert_time_to_float(winter_solstice_sunrise) - convert_time_to_float(summer_solstice_sunrise)};
  const float day_offset{2.0F*static_cast<float>(M_PI)*(convert_date_to_float(today) - convert_date_to_float(summer_solstice_sunrise))/365.0F};
  const float time_offset{variation_in_secs*(-cosf(day_offset)/2.0F + 0.5F)};
  const float todays_sunrise{convert_time_to_float(summer_solstice_sunrise) + time_offset};

  return convert_uint_to_time(todays_sunrise);
}

datetime_t sunset(datetime_t today) {
  const float variation_in_secs{convert_time_to_float(summer_solstice_sunset) - convert_time_to_float(winter_solstice_sunset)};
  const float day_offset{2.0F*static_cast<float>(M_PI)*(convert_date_to_float(today) - convert_date_to_float(summer_solstice_sunset))/365.0F};
  const float time_offset{variation_in_secs*(-cosf(day_offset)/2.0F + 0.5F)};
  const float todays_sunset{convert_time_to_float(summer_solstice_sunset) - time_offset};

  return convert_uint_to_time(todays_sunset);
}

////////////////////////////////////////////////////////////////////////////////

int main() {
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

  blinds blinds(BLINDS_ARE_OPEN);
  uint32_t todays_day{0U};
  float todays_sunrise{0U};
  float todays_sunset{0U};

  while (true) {
    rtc_get_datetime(&now);

    if (now.day != todays_day) {
      todays_sunrise = convert_time_to_float(sunrise(now)) + SUNRISE_MARGIN;
      todays_sunset = convert_time_to_float(sunset(now)) - SUNSET_MARGIN;
      todays_day = now.day;
    }

    const bool after_sunrise{convert_time_to_float(now) > todays_sunrise};
    const bool before_sunset{convert_time_to_float(now) < todays_sunset};
    const bool blinds_should_be_open{after_sunrise && before_sunset};

    if (blinds_should_be_open) {
      blinds.open();
    } else {
      blinds.close();
    }

    sleep_ms(500U);
  }
}

////////////////////////////////////////////////////////////////////////////////
// End of file
////////////////////////////////////////////////////////////////////////////////
