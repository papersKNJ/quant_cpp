#include <iostream>
#include "date.hpp"
#include "cashflow.hpp"

int main() {
    Date today{2024, 10, 1};
    Date maturity{2025, 10, 1};

    Cashflow zcb{maturity, 100.0};
    double r = 0.05;

    double T = yearFraction_ACT365(today, maturity);
    double pv = presentValueSimple(zcb, r, today);

    std::cout << "T (ACT/365 approx) = " << T << std::endl;
    std::cout << "ZCB PV (simple)    = " << pv << std::endl;

    double bump = 0.0001;
    double pv_up = presentValueSimple(zcb, r + bump, today);
    double pv01 = pv_up - pv;

    std::cout << "PV01 (1bp, approx) = " << pv01 << std::endl;
    return 0;
}
