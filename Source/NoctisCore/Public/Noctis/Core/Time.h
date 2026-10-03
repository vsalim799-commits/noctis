// Simulation clock and paleo-calendar.
//
// Day length and year length are environment parameters: in the Late Cretaceous the Earth
// rotated slightly faster than today (shorter day, more days per year). The calendar therefore
// never hard-codes 24 h / 365 d; values come from Data/Environments/<id>.json with their basis.
#pragma once

#include "Noctis/Core/Platform.h"

#include <string>

namespace noctis
{
struct CalendarSpec
{
    double dayLengthHours = 24.0;
    double daysPerYear = 365.0;
    // Synodic month in (paleo) days; drives moonlight at night.
    double synodicMonthDays = 29.53;
};

class NOCTIS_API SimClock
{
public:
    void configure(const CalendarSpec& spec, double startDayOfYear, double startHour, int startYear = 0);

    void advance(double dtSeconds) { seconds_ += dtSeconds; }
    double seconds() const { return seconds_; }
    void setSeconds(double s) { seconds_ = s; }

    double dayLengthSeconds() const { return spec_.dayLengthHours * 3600.0; }
    double yearLengthSeconds() const { return dayLengthSeconds() * spec_.daysPerYear; }
    const CalendarSpec& spec() const { return spec_; }

    // Continuous day count since simulation epoch (day 0 = start of year 0).
    double absoluteDays() const { return seconds_ / dayLengthSeconds() + startOffsetDays_; }
    int year() const;
    double dayOfYear() const;     // [0, daysPerYear)
    double hourOfDay() const;     // [0, dayLengthHours)
    double yearFraction() const { return dayOfYear() / spec_.daysPerYear; }
    double dayFraction() const { return hourOfDay() / spec_.dayLengthHours; }
    double moonPhase() const;     // 0 = new moon, 0.5 = full moon
    std::string formatted() const; // "An 2, jour 143, 06:12"

private:
    CalendarSpec spec_;
    double seconds_ = 0.0;
    double startOffsetDays_ = 0.0;
};
} // namespace noctis
