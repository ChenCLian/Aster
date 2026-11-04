#include "math/astronomy/seasonal_insolation.h"
#include "math/astronomy/astronomical_curves.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace aster::astronomy
{
namespace
{
double positive(double angle)
{
    angle = std::fmod(angle, 2 * pi);
    return angle < 0 ? angle + 2 * pi : angle;
}

double meanAnomaly(const OrbitalElements &orbit, double longitude)
{
    const double nu = std::remainder(longitude - orbit.perihelion - pi, 2 * pi);
    const double eccentric = 2 * std::atan2(std::sqrt(1 - orbit.eccentricity) * std::sin(nu / 2),
                                            std::sqrt(1 + orbit.eccentricity) * std::cos(nu / 2));
    return eccentric - orbit.eccentricity * std::sin(eccentric);
}

// Adaptive Simpson on short initial panels also detects polar daylight windows.
// Geometry is dimensionless: the tolerance is independent of S0 and output units.
class Integrator
{
  public:
    Integrator(std::function<double(double)> function, std::function<bool()> cancel)
        : m_function(std::move(function)), m_cancel(std::move(cancel))
    {
    }

    double integrate(double a, double b)
    {
        double sum = 0;
        const int panels = std::max(1, int(std::ceil((b - a) / (pi / 16))));
        for (int i = 0; i < panels; ++i)
        {
            const double left = a + (b - a) * i / panels;
            const double right = a + (b - a) * (i + 1) / panels;
            const double l = value(left), c = value((left + right) / 2), r = value(right);
            sum += refine(left, right, l, c, r, (right - left) * (l + 4 * c + r) / 6,
                          1e-10 / panels, 22);
        }
        return sum;
    }

  private:
    double value(double x)
    {
        if ((++m_evaluations & 31) == 0 && m_cancel && m_cancel())
            throw std::runtime_error("Seasonal integration cancelled");
        if (m_evaluations > 100000)
            throw std::runtime_error("Seasonal integration evaluation limit exceeded");
        return m_function(x);
    }

    double refine(double a, double b, double fa, double fm, double fb, double previous,
                  double tolerance, int depth)
    {
        const double m = (a + b) / 2, l = value((a + m) / 2), r = value((m + b) / 2);
        const double left = (m - a) * (fa + 4 * l + fm) / 6;
        const double right = (b - m) * (fm + 4 * r + fb) / 6;
        const double delta = left + right - previous;
        if (std::abs(delta) <= 15 * tolerance)
            return left + right + delta / 15;
        if (depth == 0)
            throw std::runtime_error("Seasonal integration did not converge");
        return refine(a, m, fa, l, fm, left, tolerance / 2, depth - 1) +
               refine(m, b, fm, r, fb, right, tolerance / 2, depth - 1);
    }

    std::function<double(double)> m_function;
    std::function<bool()> m_cancel;
    int m_evaluations = 0;
};
} // namespace

int calendarDay365(int month, int day)
{
    constexpr int lengths[]{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12 || day < 1 || day > lengths[month - 1])
        throw std::invalid_argument(
            "Invalid date in the fixed 365-day calendar (February has 28 days)");
    int ordinal = day;
    for (int i = 0; i < month - 1; ++i)
        ordinal += lengths[i];
    return ordinal;
}

double solarLongitudeAtDay(const OrbitalElements &orbit, double day)
{
    (void)climaticPrecession(orbit); // Reuse physical orbital-element validation.
    if (!std::isfinite(day) || std::abs(day) > 10000)
        throw std::invalid_argument("Invalid calendar day");
    const double mean = std::remainder(meanAnomaly(orbit, 0) + 2 * pi * (day - 80) / 365,
                                       2 * pi);
    // Monotone Kepler equation, bracketed Newton: stable even for e near one.
    double low = -pi, high = pi, eccentric = mean;
    for (int i = 0; i < 80; ++i)
    {
        const double f = eccentric - orbit.eccentricity * std::sin(eccentric) - mean;
        if (std::abs(f) < 2e-15)
            break;
        if (f > 0)
            high = eccentric;
        else
            low = eccentric;
        const double next = eccentric - f / (1 - orbit.eccentricity * std::cos(eccentric));
        eccentric = next > low && next < high ? next : (low + high) / 2;
    }
    const double nu = 2 * std::atan2(std::sqrt(1 + orbit.eccentricity) * std::sin(eccentric / 2),
                                     std::sqrt(1 - orbit.eccentricity) * std::cos(eccentric / 2));
    return positive(nu + orbit.perihelion + pi);
}

SeasonalInsolation integrateSeason(const OrbitalElements &orbit, double latitude,
                                   double start, double end, double solar,
                                   const std::function<bool()> &cancelled)
{
    (void)dailyMeanInsolation(orbit, latitude, start, solar);
    if (!std::isfinite(end) || start < 0 || start > 2 * pi || end < 0 || end > 2 * pi)
        throw std::invalid_argument("Seasonal solar longitudes must be in 0..360 degrees");
    if (cancelled && cancelled())
        throw std::runtime_error("Seasonal integration cancelled");
    double span = end - start;
    if (span <= 0)
        span += 2 * pi;
    if (span == 0) // 360 -> 0 also explicitly means a complete year.
        span = 2 * pi;
    const bool full = std::abs(span - 2 * pi) < 1e-14;
    const double deltaMean = full ? 2 * pi :
        positive(meanAnomaly(orbit, start + span) - meanAnomaly(orbit, start));
    if (!(deltaMean > 0))
        throw std::invalid_argument("Season interval is below numerical resolution");
    const double duration = tropicalYearDays * deltaMean / (2 * pi);

    // Q * dt/dlambda cancels the distance factors exactly. Averaging Q
    // uniformly in longitude without this time weighting would be incorrect.
    const OrbitalElements circular{0, orbit.obliquity, 0};
    Integrator integral([&](double lambda) {
        return dailyMeanInsolation(circular, latitude, lambda, 1);
    }, cancelled);
    const double fluxDays = solar * tropicalYearDays /
        (2 * pi * std::sqrt(1 - orbit.eccentricity * orbit.eccentricity)) *
        integral.integrate(start, start + span);
    const double energy = std::max(0.0, fluxDays) * 86400 / 1e6;
    const double mean = energy * 1e6 / (duration * 86400);
    if (!std::isfinite(energy) || !std::isfinite(mean))
        throw std::invalid_argument("Seasonal result exceeds numeric range");
    return {mean, energy, duration};
}

SeasonalInsolation integrateCalendarSeason(const OrbitalElements &orbit, double latitude,
    int month1, int day1, int month2, int day2, double solar,
    const std::function<bool()> &cancelled)
{
    const int first = calendarDay365(month1, day1), last = calendarDay365(month2, day2);
    const double a = solarLongitudeAtDay(orbit, first - .5);
    const double b = solarLongitudeAtDay(orbit, last - .5);
    auto result = integrateSeason(orbit, latitude, a, b, solar, cancelled);
    const int days = last > first ? last - first : last + 365 - first;
    result.durationDays = days * tropicalYearDays / 365;
    result.meanWm2 = result.energyMJm2 * 1e6 / (result.durationDays * 86400);
    return result;
}
} // namespace aster::astronomy
