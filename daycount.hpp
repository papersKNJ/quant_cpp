#ifndef DAYCOUNT_HPP
#define DAYCOUNT_HPP

#include "date.hpp"

enum class DayCountConvention {
    ACT_365F,
    ACT_360
};

class DayCount {
public:
    static double calculate(DayCountConvention convention, const Date& start, const Date& end) {
        int days = Date::daysBetween(start, end);
        
        switch (convention) {
            case DayCountConvention::ACT_365F:
                return days / 365.0;
            case DayCountConvention::ACT_360:
                return days / 360.0;
            default:
                return 0.0;
        }
    }
};

#endif // DAYCOUNT_HPP