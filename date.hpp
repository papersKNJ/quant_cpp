#pragma once

struct Date {
    int year;
    int month;
    int day;
};

inline int daysDiff_30_365(const Date& d1, const Date& d2) {
    return (d2.year - d1.year) * 365
         + (d2.month - d1.month) * 30
         + (d2.day - d1.day);
}

inline double yearFraction_ACT365(const Date& d1, const Date& d2) {
    return daysDiff_30_365(d1, d2) / 365.0;
}

