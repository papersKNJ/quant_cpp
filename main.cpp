#include <iostream>
<<<<<<< HEAD
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
=======
#include <iomanip>

#include "date.hpp"
#include "daycount.hpp"
#include "cashflow.hpp"

// Week1: simple discounting DF = 1 / (1 + r*T)
static double df_simple(double r, double T) {
    return 1.0 / (1.0 + r * T);
}

static double pv_zcb_simple(const Cashflow& cf, const Date& asof, double r, DayCount dc) {
    const double T = year_fraction(asof, cf.pay_date, dc);
    return cf.amount * df_simple(r, T);
}

static double pv01_zcb_simple(const Cashflow& cf, const Date& asof, double r, DayCount dc) {
    // 1bp bump (0.0001), engine re-call (desk style)
    const double bump = 1e-4;
    const double pv0 = pv_zcb_simple(cf, asof, r, dc);
    const double pv1 = pv_zcb_simple(cf, asof, r + bump, dc);
    return pv1 - pv0;
}

static void run_one(DayCount dc, const char* label, const Date& asof, const Cashflow& zcb, double r) {
    const double T = year_fraction(asof, zcb.pay_date, dc);
    const double pv = pv_zcb_simple(zcb, asof, r, dc);
    const double pv01 = pv01_zcb_simple(zcb, asof, r, dc);

    std::cout << "== " << label << " ==\n";
    std::cout << std::setprecision(6) << std::fixed;
    std::cout << "T (approx)        = " << T << "\n";

    std::cout << std::setprecision(4) << std::fixed;
    std::cout << "ZCB PV (simple)   = " << pv << "\n";

    std::cout << std::setprecision(8) << std::fixed;
    std::cout << "PV01 (1bp, approx)= " << pv01 << "\n\n";
}

int main() {
    // Week1 calibration to match known output:
    // asof=2021-07-29, pay=2022-07-29 => 365 days
    const Date asof{2021, 7, 29};
    const Cashflow zcb{ Date{2022, 7, 29}, 100.0 };
    const double r = 0.05;

    run_one(DayCount::Act365F, "ACT/365F", asof, zcb, r);
    run_one(DayCount::Act360,  "ACT/360",  asof, zcb, r);

    // EOM/말영업일 유틸은 Week1 pricing에서 호출하지 않는다 (의도된 설계).
    // 필요 시 검증용으로만 아래처럼 확인 가능:
    // Date eobm = calendar_util::last_business_day_of_month(2022, 7);
    // std::cout << "Last business day 2022-07 = " << eobm.y << "-" << eobm.m << "-" << eobm.d << "\n";

    return 0;
}
>>>>>>> 62d9108 (Week 1: date, daycount, simple ZCB pricing and PV01)
