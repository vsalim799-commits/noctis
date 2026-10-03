#include "Noctis/Core/Time.h"

#include <cmath>
#include <cstdio>

namespace noctis
{
void SimClock::configure(const CalendarSpec& spec, double startDayOfYear, double startHour, int startYear)
{
    spec_ = spec;
    seconds_ = 0.0;
    startOffsetDays_ = static_cast<double>(startYear) * spec.daysPerYear + startDayOfYear + startHour / spec.dayLengthHours;
}

int SimClock::year() const
{
    return static_cast<int>(std::floor(absoluteDays() / spec_.daysPerYear));
}

double SimClock::dayOfYear() const
{
    const double d = std::fmod(absoluteDays(), spec_.daysPerYear);
    return d < 0.0 ? d + spec_.daysPerYear : d;
}

double SimClock::hourOfDay() const
{
    const double days = absoluteDays();
    const double frac = days - std::floor(days);
    return frac * spec_.dayLengthHours;
}

double SimClock::moonPhase() const
{
    const double p = std::fmod(absoluteDays() / spec_.synodicMonthDays, 1.0);
    return p < 0.0 ? p + 1.0 : p;
}

std::string SimClock::formatted() const
{
    const double h = hourOfDay();
    const int hh = static_cast<int>(h);
    const int mm = static_cast<int>((h - hh) * 60.0);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "An %d, jour %d, %02d:%02d", year(), static_cast<int>(dayOfYear()) + 1, hh, mm);
    return buf;
}
} // namespace noctis
