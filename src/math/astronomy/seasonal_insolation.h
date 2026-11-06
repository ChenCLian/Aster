// Copyright (C) 2026 ZiJie Chen
// SPDX-License-Identifier: GPL-3.0-only
#ifndef ASTER_MATH_ASTRONOMY_SEASONAL_INSOLATION_H
#define ASTER_MATH_ASTRONOMY_SEASONAL_INSOLATION_H
#include "math/astronomy/orbital_elements.h"
#include <functional>
namespace aster::astronomy {
inline constexpr double tropicalYearDays = 365.24219876;
// Conventional Gregorian month lengths without leap days. Day 80 at noon is
// fixed to the March equinox, NOT a reconstruction of the historical calendar.
int calendarDay365(int month, int day);
double solarLongitudeAtDay(const OrbitalElements&, double dayOfYear);
struct SeasonalInsolation {
    double meanWm2 = 0;
    double energyMJm2 = 0;
    double durationDays = 0;
};
// Radians, positive forward season; end <= start wraps into the next year.
// Equal bounds mean a complete year. No interval longer than one year.
SeasonalInsolation integrateSeason(const OrbitalElements&, double latitude,
    double startLongitude, double endLongitude, double solarConstant,
    const std::function<bool()>& cancelled = {});
// Midnight at start date to midnight at end date (exclusive). Equal dates
// mean a full year; e.g. Jun 1 -> Sep 1 covers all Jun, Jul and Aug dates.
SeasonalInsolation integrateCalendarSeason(const OrbitalElements&, double latitude,
    int startMonth, int startDay, int endMonth, int endDay, double solarConstant,
    const std::function<bool()>& cancelled = {});
}
#endif
