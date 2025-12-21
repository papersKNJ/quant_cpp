#pragma once
#include "date.hpp"
#include <stdexcept>

enum class DayCount {
    Act365F,
    Act360
};

inline double year_fraction(const Date& start, const Date& end, DayCount dc) {
    const int days = date_util::days_between(start, end);
    if (days < 0) throw std::runtime_error("year_fraction: end < start");

    switch (dc) {
        case DayCount::Act365F: return static_cast<double>(days) / 365.0;
        case DayCount::Act360:  return static_cast<double>(days) / 360.0;
        default: throw std::runtime_error("year_fraction: unsupported DayCount");
    }
}
