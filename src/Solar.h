// Solar.h /////////////////////////////////////////////////////////////////////
//
// Onboard sun-position math (NOAA Solar Calculator / Meeus formulas). Pure
// computation -- no network, no lookup tables, nothing that goes stale.
// Accurate to well under 0.1 degree for years 1900-2100. All times are UTC
// epoch seconds (time_t), so DST never enters the calculation; callers convert
// results to local time with localtime_r().

#pragma once

#include <time.h>

namespace Solar {

  // Sun position at a UTC instant for the given site (decimal degrees, north
  // and east positive). Azimuth is degrees clockwise from true north
  // (90 = east, 180 = south); elevation is geometric degrees above the horizon.
  void position(time_t utc, double latDeg, double lonDeg,
                double& azimuthDeg, double& elevationDeg);

  // Sunrise/sunset within [start, end) -- normally local midnight to the next
  // local midnight. Uses the standard -0.833 degree horizon (refraction plus
  // the sun's radius), matching published sunrise/sunset tables. Returns false
  // if the sun doesn't both rise and set in that span (polar day/night).
  bool sunriseSunset(time_t start, time_t end, double latDeg, double lonDeg,
                     time_t& sunrise, time_t& sunset);

  // First daylight moment within [start, end) at which the sun's azimuth
  // sweeps past targetDeg. Returns false if that never happens while the sun
  // is above the horizon.
  bool azimuthCrossing(time_t start, time_t end, double latDeg, double lonDeg,
                       double targetDeg, time_t& when);

}  // namespace Solar
