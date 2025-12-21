#pragma once
<<<<<<< HEAD

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

=======
#include <cstdint>
#include <stdexcept>

struct Date {
    int y{0};
    int m{0};
    int d{0};

    Date() = default;
    Date(int yy, int mm, int dd) : y(yy), m(mm), d(dd) {
        if (m < 1 || m > 12) throw std::runtime_error("Invalid month");
        if (d < 1 || d > 31) throw std::runtime_error("Invalid day");
    }
};

// ---- Helpers (pure date arithmetic) ----
namespace date_util {

inline bool is_leap_year(int y) {
    if (y % 400 == 0) return true;
    if (y % 100 == 0) return false;
    return (y % 4 == 0);
}

inline int days_in_month(int y, int m) {
    static const int dim[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (m == 2) return is_leap_year(y) ? 29 : 28;
    return dim[m - 1];
}

inline int64_t days_from_civil(int y, unsigned m, unsigned d) {
    // Howard Hinnant algorithm: days since 1970-01-01
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;
}

inline int days_between(const Date& a, const Date& b) {
    const int64_t da = days_from_civil(a.y, static_cast<unsigned>(a.m), static_cast<unsigned>(a.d));
    const int64_t db = days_from_civil(b.y, static_cast<unsigned>(b.m), static_cast<unsigned>(b.d));
    return static_cast<int>(db - da);
}

// 0=Mon, 1=Tue, ... 5=Sat, 6=Sun
inline int weekday(const Date& x) {
    const int64_t z = days_from_civil(x.y, static_cast<unsigned>(x.m), static_cast<unsigned>(x.d));
    // 1970-01-01 was Thursday. Monday=0 => shift by 3
    const int w = static_cast<int>((z + 3) % 7);
    return (w >= 0) ? w : (w + 7);
}

inline bool is_end_of_month(const Date& x) {
    return x.d == days_in_month(x.y, x.m);
}

} // namespace date_util

// ---- Calendar/EOM utilities (NOT used by Week1 pricing) ----
namespace calendar_util {

inline bool is_weekend(const Date& x) {
    const int w = date_util::weekday(x);
    return (w == 5) || (w == 6); // Sat/Sun
}

inline bool is_business_day(const Date& x) {
    // Week1: holiday set not modeled; weekend only
    return !is_weekend(x);
}

inline Date last_business_day_of_month(int y, int m) {
    int d = date_util::days_in_month(y, m);
    while (d >= 1) {
        Date x{y, m, d};
        if (is_business_day(x)) return x;
        --d;
    }
    throw std::runtime_error("No business day found (unexpected)");
}

} // namespace calendar_util
>>>>>>> 62d9108 (Week 1: date, daycount, simple ZCB pricing and PV01)
