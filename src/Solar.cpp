// Solar.cpp ///////////////////////////////////////////////////////////////////

#include "Solar.h"

#include <math.h>

namespace {

constexpr double kDegToRad {M_PI / 180.0};
constexpr double kRadToDeg {180.0 / M_PI};

// Horizon elevation for sunrise/sunset: atmospheric refraction (34') plus the
// sun's apparent radius (16').
constexpr double kSunriseElevation {-0.833};

// Coarse scan step for locating events, then refined by bisection to 1 s.
constexpr time_t kScanStep {5 * 60};

double wrap360(double deg) {
  deg = fmod(deg, 360.0);
  return deg < 0.0 ? deg + 360.0 : deg;
}

// Bisect [lo, hi] down to 1 s, given pred(lo) == false and pred(hi) == true.
template <typename Pred>
time_t bisect(time_t lo, time_t hi, Pred pred) {
  while (hi - lo > 1) {
    const time_t mid{lo + (hi - lo) / 2};
    if (pred(mid)) hi = mid; else lo = mid;
  }
  return hi;
}

}  // namespace

namespace Solar {

void position(time_t utc, double latDeg, double lonDeg,
              double& azimuthDeg, double& elevationDeg) {
  // Julian centuries since J2000.0
  const double jd{static_cast<double>(utc) / 86400.0 + 2440587.5};
  const double t {(jd - 2451545.0) / 36525.0};

  // Sun's mean longitude and mean anomaly (deg), orbit eccentricity
  const double l0 {wrap360(280.46646 + t * (36000.76983 + t * 0.0003032))};
  const double m  {357.52911 + t * (35999.05029 - 0.0001537 * t)};
  const double e  {0.016708634 - t * (0.000042037 + 0.0000001267 * t)};
  const double mr {m * kDegToRad};

  // Equation of center -> true and apparent longitude
  const double c {sin(mr)       * (1.914602 - t * (0.004817 + 0.000014 * t)) +
                  sin(2.0 * mr) * (0.019993 - 0.000101 * t) +
                  sin(3.0 * mr) * 0.000289};
  const double omega  {(125.04 - 1934.136 * t) * kDegToRad};
  const double lambda {(l0 + c - 0.00569 - 0.00478 * sin(omega)) * kDegToRad};

  // Obliquity of the ecliptic (corrected) and solar declination
  const double eps0 {23.0 + (26.0 + (21.448 - t * (46.815 + t * (0.00059 - t * 0.001813))) / 60.0) / 60.0};
  const double eps  {(eps0 + 0.00256 * cos(omega)) * kDegToRad};
  const double decl {asin(sin(eps) * sin(lambda))};

  // Equation of time (minutes)
  const double y   {tan(eps / 2.0) * tan(eps / 2.0)};
  const double l0r {l0 * kDegToRad};
  const double eqTime {4.0 * kRadToDeg *
                       (y * sin(2.0 * l0r) - 2.0 * e * sin(mr) +
                        4.0 * e * y * sin(mr) * cos(2.0 * l0r) -
                        0.5 * y * y * sin(4.0 * l0r) -
                        1.25 * e * e * sin(2.0 * mr))};

  // True solar time -> hour angle (negative before solar noon)
  const double utcMinutes {fmod(static_cast<double>(utc), 86400.0) / 60.0};
  const double solarMin   {fmod(utcMinutes + eqTime + 4.0 * lonDeg + 1440.0, 1440.0)};
  const double ha         {(solarMin / 4.0 - 180.0) * kDegToRad};

  const double lat {latDeg * kDegToRad};
  const double cosZenith {sin(lat) * sin(decl) + cos(lat) * cos(decl) * cos(ha)};
  elevationDeg = 90.0 - acos(fmax(-1.0, fmin(1.0, cosZenith))) * kRadToDeg;
  azimuthDeg   = wrap360(180.0 + kRadToDeg *
                         atan2(sin(ha), cos(ha) * sin(lat) - tan(decl) * cos(lat)));
}

bool sunriseSunset(time_t start, time_t end, double latDeg, double lonDeg,
                   time_t& sunrise, time_t& sunset) {
  auto isUp = [&](time_t when) {
    double az, el;
    position(when, latDeg, lonDeg, az, el);
    return el > kSunriseElevation;
  };

  bool foundRise{false}, foundSet{false};
  bool prevUp{isUp(start)};
  for (time_t t0{start}; t0 < end; t0 += kScanStep) {
    const time_t t1{t0 + kScanStep < end ? t0 + kScanStep : end};
    const bool up{isUp(t1)};
    if (!foundRise && !prevUp && up) {
      sunrise   = bisect(t0, t1, isUp);
      foundRise = true;
    } else if (!foundSet && prevUp && !up) {
      sunset   = bisect(t0, t1, [&](time_t w) { return !isUp(w); });
      foundSet = true;
    }
    prevUp = up;
  }
  return foundRise && foundSet && sunrise < sunset;
}

bool azimuthCrossing(time_t start, time_t end, double latDeg, double lonDeg,
                     double targetDeg, time_t& when) {
  targetDeg = wrap360(targetDeg);

  // Azimuth relative to target, in (-180, 180]: crosses zero (negative ->
  // positive) as the sun sweeps clockwise past the target. The far side of
  // the circle (+/-180 flip) is excluded by requiring a small step.
  auto relAz = [&](time_t w, double& el) {
    double az;
    position(w, latDeg, lonDeg, az, el);
    double d{wrap360(az - targetDeg)};
    return d > 180.0 ? d - 360.0 : d;
  };

  double el0;
  double prev{relAz(start, el0)};
  for (time_t t0{start}; t0 < end; t0 += kScanStep) {
    const time_t t1{t0 + kScanStep < end ? t0 + kScanStep : end};
    double el1;
    const double cur{relAz(t1, el1)};
    if (prev < 0.0 && cur >= 0.0 && (cur - prev) < 90.0) {
      double el;
      const time_t hit{bisect(t0, t1, [&](time_t w) { return relAz(w, el) >= 0.0; })};
      relAz(hit, el);
      if (el > 0.0) {
        when = hit;
        return true;
      }
    }
    prev = cur;
  }
  return false;
}

}  // namespace Solar
