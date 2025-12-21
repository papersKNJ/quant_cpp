#pragma once
#include "date.hpp"

struct Cashflow {
<<<<<<< HEAD
    Date paymentDate;
    double amount;
};

inline double discountFactorSimple(double r, double T) {
    return 1.0 / (1.0 + r * T);
}

inline double presentValueSimple(const Cashflow& cf, double r, const Date& today) {
    double T = yearFraction_ACT365(today, cf.paymentDate);
    return cf.amount * discountFactorSimple(r, T);
}
=======
    Date pay_date{};
    double amount{0.0};
};
>>>>>>> 62d9108 (Week 1: date, daycount, simple ZCB pricing and PV01)
