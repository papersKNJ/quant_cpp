#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

// ============================================================
// 1) Date: 실제 달력 기반 serial(int32)  (1970-01-01 => 0)
//    Howard Hinnant civil calendar 알고리즘 계열(업계에서 널리 사용)
// ============================================================
struct Date {
    int y{1970}, m{1}, d{1};
    int32_t serial{0};

    Date() = default;
    Date(int yy, int mm, int dd) : y(yy), m(mm), d(dd) {
        serial = daysFromCivil(y, static_cast<unsigned>(m), static_cast<unsigned>(d));
    }

    static Date fromSerial(int32_t s) {
        Date dt;
        dt.serial = s;
        unsigned mm{}, dd{};
        int yy{};
        civilFromDays(s, yy, mm, dd);
        dt.y = yy; dt.m = static_cast<int>(mm); dt.d = static_cast<int>(dd);
        return dt;
    }

    bool operator<(const Date& rhs) const { return serial < rhs.serial; }
    bool operator==(const Date& rhs) const { return serial == rhs.serial; }

    Date addDays(int n) const { return fromSerial(static_cast<int32_t>(serial + n)); }

    // 0=Mon..6=Sun
    int weekday() const {
        // 1970-01-01 is Thursday
        int w = (static_cast<int>(serial) + 3) % 7;
        if (w < 0) w += 7;
        return w;
    }

    static bool isLeap(int yy) {
        return (yy % 4 == 0 && yy % 100 != 0) || (yy % 400 == 0);
    }

    static int daysInMonth(int yy, int mm) {
        static const int mdays[12] = {31,28,31,30,31,30,31,31,30,31,30,31};
        if (mm == 2) return isLeap(yy) ? 29 : 28;
        return mdays[mm - 1];
    }

    static bool isEndOfMonth(const Date& dt) {
        return dt.d == daysInMonth(dt.y, dt.m);
    }

    static Date endOfMonth(int yy, int mm) {
        return Date(yy, mm, daysInMonth(yy, mm));
    }

private:
    static int32_t daysFromCivil(int y, unsigned m, unsigned d) {
        y -= (m <= 2);
        const int era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = static_cast<unsigned>(y - era * 400);
        const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return static_cast<int32_t>(era * 146097 + static_cast<int>(doe) - 719468);
    }

    static void civilFromDays(int32_t z, int& y, unsigned& m, unsigned& d) {
        z += 719468;
        const int era = (z >= 0 ? z : z - 146096) / 146097;
        const unsigned doe = static_cast<unsigned>(z - era * 146097);
        const unsigned yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
        y = static_cast<int>(yoe) + era * 400;
        const unsigned doy = doe - (365*yoe + yoe/4 - yoe/100);
        const unsigned mp = (5*doy + 2)/153;
        d = doy - (153*mp + 2)/5 + 1;
        m = mp + (mp < 10 ? 3 : -9);
        y += (m <= 2);
    }
};

// ============================================================
// 2) Calendar / BDC (학습용: 주말만 비영업일)
// ============================================================
enum class BDC { Following, ModifiedFollowing, Preceding };

struct WeekendCalendar {
    bool isBiz(const Date& dt) const {
        int w = dt.weekday();
        return (w <= 4);
    }

    Date adjust(const Date& dt, BDC bdc) const {
        if (isBiz(dt)) return dt;

        if (bdc == BDC::Following || bdc == BDC::ModifiedFollowing) {
            Date x = dt;
            while (!isBiz(x)) x = x.addDays(1);
            if (bdc == BDC::ModifiedFollowing && x.m != dt.m) {
                x = dt;
                while (!isBiz(x)) x = x.addDays(-1);
            }
            return x;
        }

        Date x = dt;
        while (!isBiz(x)) x = x.addDays(-1);
        return x;
    }
};

// ============================================================
// 3) DayCount (ACT/360)
// ============================================================
inline double act360(const Date& a, const Date& b) {
    return static_cast<double>(b.serial - a.serial) / 360.0;
}

// ============================================================
// 4) Schedule generator: tenor_months + EOM + BDC
// ============================================================
struct CouponPeriod {
    Date start;
    Date end;
    Date pay;
    Date reset;        // 학습용: start
    double accrual{0};
};

static int clampDay(int y, int m, int d) {
    int dim = Date::daysInMonth(y, m);
    return (d > dim) ? dim : d;
}

static Date addMonths(const Date& dt, int months, bool eom) {
    int y = dt.y;
    int m = dt.m + months;
    while (m > 12) { m -= 12; y += 1; }
    while (m < 1)  { m += 12; y -= 1; }

    if (eom && Date::isEndOfMonth(dt)) {
        return Date::endOfMonth(y, m);
    }
    int d = clampDay(y, m, dt.d);
    return Date(y, m, d);
}

static std::vector<CouponPeriod> buildSchedule(
    const Date& issue,
    const Date& maturity,
    int tenor_months,
    bool eom,
    BDC pay_bdc,
    const WeekendCalendar& cal
) {
    std::vector<Date> dates;
    dates.reserve(64);

    Date cur = issue;
    dates.push_back(cur);

    while (true) {
        Date next = addMonths(cur, tenor_months, eom);
        if (!(next < maturity) && !(next == maturity)) next = maturity;
        dates.push_back(next);
        if (next == maturity) break;
        cur = next;
    }

    std::vector<CouponPeriod> periods;
    periods.reserve(dates.size() - 1);

    for (size_t i = 0; i + 1 < dates.size(); ++i) {
        CouponPeriod p;
        p.start = dates[i];
        p.end   = dates[i+1];
        p.reset = p.start;
        p.pay   = cal.adjust(p.end, pay_bdc);
        p.accrual = act360(p.start, p.end);
        periods.push_back(p);
    }
    return periods;
}

// ============================================================
// 5) DF Curve: log-linear interpolation on DF
//    성능: lnDF를 저장해서 df조회 시 log를 제거
// ============================================================
class DFCurve {
public:
    DFCurve(std::vector<double> times, std::vector<double> dfs)
        : t_(std::move(times)) {
        if (t_.size() < 2 || t_.size() != dfs.size()) throw std::runtime_error("DFCurve: invalid sizes");
        ln_df_.reserve(dfs.size());

        for (size_t i = 0; i < t_.size(); ++i) {
            if (t_[i] <= 0.0) throw std::runtime_error("DFCurve: time must be > 0");
            if (dfs[i] <= 0.0) throw std::runtime_error("DFCurve: DF must be > 0");
            if (i && !(t_[i] > t_[i-1])) throw std::runtime_error("DFCurve: times increasing");
            ln_df_.push_back(std::log(dfs[i]));
        }
    }

    static DFCurve fromZeroCC(const std::vector<double>& pillars, const std::vector<double>& zero_cc) {
        if (pillars.size() != zero_cc.size()) throw std::runtime_error("fromZeroCC size mismatch");
        std::vector<double> dfs;
        dfs.reserve(pillars.size());
        for (size_t i = 0; i < pillars.size(); ++i) dfs.push_back(std::exp(-zero_cc[i] * pillars[i]));
        return DFCurve(std::vector<double>(pillars.begin(), pillars.end()), std::move(dfs));
    }

    // DF from curve origin to time t (years). df(0)=1 handled outside for speed.
    double df(double t) const {
        if (t <= 0.0) return 1.0;

        if (t <= t_.front()) {
            // left extrap using first segment
            double t0 = t_.front(), t1 = t_[1];
            double y0 = ln_df_.front(), y1 = ln_df_[1];
            double w = (t - t0) / (t1 - t0);
            return std::exp(y0 + w * (y1 - y0));
        }
        if (t >= t_.back()) {
            // right extrap using last segment
            size_t n = t_.size();
            double t0 = t_[n-2], t1 = t_[n-1];
            double y0 = ln_df_[n-2], y1 = ln_df_[n-1];
            double w = (t - t1) / (t1 - t0);
            return std::exp(y1 + w * (y1 - y0));
        }

        auto it = std::upper_bound(t_.begin(), t_.end(), t);
        size_t idx = static_cast<size_t>(it - t_.begin());
        double t0 = t_[idx-1], t1 = t_[idx];
        double y0 = ln_df_[idx-1], y1 = ln_df_[idx];
        double w = (t - t0) / (t1 - t0);
        return std::exp(y0 + w * (y1 - y0));
    }

private:
    std::vector<double> t_;
    std::vector<double> ln_df_;
};

// Forward rate from forward curve DF over [start,end], using ACT/360 accrual
static double fwdSimpleRate(const DFCurve& fwdCurve, double t0, double t1, double accrual) {
    // 1 + L*alpha = DF(t0)/DF(t1)
    double df0 = fwdCurve.df(t0);
    double df1 = fwdCurve.df(t1);
    return (df0 / df1 - 1.0) / accrual;
}

// ============================================================
// 6) Fixing store (reset_date.serial -> rate)
// ============================================================
class FixingStore {
public:
    void add(const Date& reset, double rate) {
        m_[reset.serial] = rate;
    }
    bool has(const Date& reset) const {
        return m_.find(reset.serial) != m_.end();
    }
    double get(const Date& reset) const {
        auto it = m_.find(reset.serial);
        if (it == m_.end()) throw std::runtime_error("Fixing missing for reset date");
        return it->second;
    }
private:
    std::unordered_map<int32_t, double> m_;
};

// ============================================================
// 7) FRN pricer (valuation date, fixing, accrued)
//    - pay<=val: 제외
//    - 현재 구간(start<=val<end): fixing 사용 + accrued 계산
//    - 미래 구간(val<=start): forward curve 추정
//    - 할인: valuation date 기준 DF_disc(t_pay)
// ============================================================
struct FRNResult {
    double dirty{0.0};
    double clean{0.0};
    double accrued{0.0};
};

static FRNResult priceFRN(
    const Date& valDate,
    const std::vector<CouponPeriod>& periods,
    double notional,
    double spread,
    const DFCurve& disc,
    const DFCurve& fwd,
    const FixingStore& fixings
) {
    if (periods.empty()) return {};

    FRNResult res{};

    // Determine current period index (at most one)
    int currentIdx = -1;
    for (int i = 0; i < static_cast<int>(periods.size()); ++i) {
        const auto& p = periods[i];
        if (!(valDate < p.start) && (valDate < p.end)) { // start <= val < end
            currentIdx = i;
            break;
        }
    }

    // Accrued interest only if inside a period
    if (currentIdx >= 0) {
        const auto& p = periods[static_cast<size_t>(currentIdx)];
        // fixing must exist for current period reset (practical simulation)
        double L_fix = fixings.get(p.reset);
        double accr = act360(p.start, valDate);
        if (accr < 0.0) accr = 0.0;
        if (accr > p.accrual) accr = p.accrual;
        res.accrued = notional * (L_fix + spread) * accr;
    }

    // PV of remaining coupons and redemption discounted from valuation date
    for (size_t i = 0; i < periods.size(); ++i) {
        const auto& p = periods[i];

        if (!(valDate < p.pay)) continue; // pay <= val => already paid

        double t_pay = act360(valDate, p.pay);
        double df_pay = disc.df(t_pay);

        double rate = 0.0;

        if (static_cast<int>(i) == currentIdx) {
            // current: use fixing
            rate = fixings.get(p.reset);
        } else if (valDate < p.start) {
            // future: forward estimate using forwarding curve
            double t0 = act360(valDate, p.start);
            double t1 = act360(valDate, p.end);
            rate = fwdSimpleRate(fwd, t0, t1, p.accrual);
        } else {
            // past accrual but not yet paid (이 구조에서는 일반적으로 발생하지 않지만, 안전장치)
            // best effort: use fixing if exists; else fallback to forward
            if (fixings.has(p.reset)) rate = fixings.get(p.reset);
            else {
                double t0 = act360(valDate, p.start);
                double t1 = act360(valDate, p.end);
                rate = fwdSimpleRate(fwd, t0, t1, p.accrual);
            }
        }

        double coupon = notional * (rate + spread) * p.accrual;
        res.dirty += coupon * df_pay;

        // redemption at maturity pay date (assume last period’s pay date)
        if (i + 1 == periods.size()) {
            res.dirty += notional * df_pay;
        }
    }

    res.clean = res.dirty - res.accrued;
    return res;
}

// ============================================================
// 8) Demo main
// Scenario:
// Issue: 2024-01-01, Maturity: 2026-01-01, 3M
// Valuation date input
// Fixing: current period reset has known rate (simulate already fixed)
// ============================================================
int main() {
    Date issue(2024, 1, 1);
    Date maturity(2026, 1, 1);

    int vy, vm, vd;
    std::cout << "평가일을 입력하세요 (예: 2024 07 15): ";
    if (!(std::cin >> vy >> vm >> vd)) return 1;
    Date val(vy, vm, vd);

    WeekendCalendar cal;
    auto periods = buildSchedule(issue, maturity, 3, /*eom*/false, BDC::ModifiedFollowing, cal);

    // Sanity
    assert(!periods.empty());
    for (const auto& p : periods) {
        assert(p.end.serial > p.start.serial);
        assert(p.accrual > 0.0);
        assert(cal.isBiz(p.pay));
    }

    // Synthetic curves (pillars in years)
    const std::vector<double> pillars = {0.25, 0.5, 1.0, 2.0, 3.0, 5.0};

    // discount: slightly lower
    const std::vector<double> z_disc = {0.030, 0.031, 0.032, 0.033, 0.0335, 0.034};
    DFCurve disc = DFCurve::fromZeroCC(pillars, z_disc);

    // forward: slightly higher
    const std::vector<double> z_fwd  = {0.032, 0.033, 0.034, 0.035, 0.0355, 0.036};
    DFCurve fwd = DFCurve::fromZeroCC(pillars, z_fwd);

    // Determine current period, then seed fixing for that reset date
    FixingStore fixings;
    int currentIdx = -1;
    for (int i = 0; i < static_cast<int>(periods.size()); ++i) {
        const auto& p = periods[static_cast<size_t>(i)];
        if (!(val < p.start) && (val < p.end)) { currentIdx = i; break; }
    }

    // Simulate: if val inside a coupon period, that period’s reset was already fixed at 3.5%
    if (currentIdx >= 0) {
        fixings.add(periods[static_cast<size_t>(currentIdx)].reset, 0.035);
    }

    // Product terms
    double notional = 10000.0;
    double spread = 0.0100; // 100bp

    FRNResult r = priceFRN(val, periods, notional, spread, disc, fwd, fixings);

    // Report
    std::cout << "\n[ FRN Valuation Report ]\n";
    std::cout << "Issue    : " << issue.y << "-" << issue.m << "-" << issue.d << "\n";
    std::cout << "Maturity : " << maturity.y << "-" << maturity.m << "-" << maturity.d << "\n";
    std::cout << "ValDate  : " << val.y << "-" << val.m << "-" << val.d << "\n";
    std::cout << "Notional : " << notional << "\n";
    std::cout << "Spread   : " << (spread * 10000.0) << " bp\n";
    if (currentIdx >= 0) std::cout << "Current Fixing used: 3.50%\n";
    else std::cout << "ValDate is not inside any accrual period (no accrued, no fixing needed).\n";

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "\nAccrued Interest : " << r.accrued << "\n";
    std::cout << "Dirty Price(PV)  : " << r.dirty << "\n";
    std::cout << "Clean Price      : " << r.clean << "\n";

    // Optional: quick structural check (spread linearity around small changes)
    double spread2 = spread + 0.0010; // +10bp
    FRNResult r2 = priceFRN(val, periods, notional, spread2, disc, fwd, fixings);
    std::cout << "\n[ Spread +10bp Check ]\n";
    std::cout << "Dirty(PV) +10bp  : " << r2.dirty << "\n";
    std::cout << "Delta PV         : " << (r2.dirty - r.dirty) << "\n";

    return 0;
}
