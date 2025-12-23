#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

// ============================================================
// 1) Date: 실제 달력 기반 serial(int32)  (1970-01-01 => 0)
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
        dt.y = yy;
        dt.m = static_cast<int>(mm);
        dt.d = static_cast<int>(dd);
        return dt;
    }

    bool operator<(const Date& rhs) const { return serial < rhs.serial; }
    bool operator==(const Date& rhs) const { return serial == rhs.serial; }

    Date addDays(int n) const { return fromSerial(static_cast<int32_t>(serial + n)); }

    // 0=Mon..6=Sun
    int weekday() const {
        int w = (static_cast<int>(serial) + 3) % 7; // 1970-01-01 is Thursday
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
        const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        y = static_cast<int>(yoe) + era * 400;
        const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const unsigned mp = (5 * doy + 2) / 153;
        d = doy - (153 * mp + 2) / 5 + 1;
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
// 4) Schedule generator
// ============================================================
struct CouponPeriod {
    Date start;
    Date end;
    Date pay;
    Date reset;
    double accrual{0.0};
};

static int clampDay(int y, int m, int d) {
    int dim = Date::daysInMonth(y, m);
    return (d > dim) ? dim : d;
}

static Date addMonths(const Date& dt, int months, bool eom) {
    int y = dt.y;
    int m = dt.m + months;
    while (m > 12) { m -= 12; y += 1; }
    while (m < 1) { m += 12; y -= 1; }

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
    int totalMonths = (maturity.y - issue.y) * 12 + (maturity.m - issue.m);
    int roughPeriods = (totalMonths + tenor_months - 1) / tenor_months + 2;
    size_t expectedPeriods = static_cast<size_t>(std::max(1, roughPeriods));

    std::vector<Date> dates;
    dates.reserve(expectedPeriods + 1);

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
        p.end = dates[i + 1];
        p.reset = p.start;
        p.pay = cal.adjust(p.end, pay_bdc);
        p.accrual = act360(p.start, p.end);
        periods.push_back(p);
    }
    return periods;
}

// ============================================================
// 5) DF Curve: log-linear interpolation on DF
//    - 최소 diff: dfFrom을 lnDF 차로 계산 (ratio 대신)
// ============================================================
class DFCurve {
public:
    DFCurve(const Date& origin, std::vector<double> times, std::vector<double> dfs)
        : origin_(origin), t_(std::move(times)) {
        if (t_.size() < 2 || t_.size() != dfs.size()) throw std::runtime_error("DFCurve: invalid sizes");
        ln_df_.reserve(dfs.size());

        for (size_t i = 0; i < t_.size(); ++i) {
            if (t_[i] <= 0.0) throw std::runtime_error("DFCurve: time must be > 0");
            if (dfs[i] <= 0.0) throw std::runtime_error("DFCurve: DF must be > 0");
            if (i && !(t_[i] > t_[i - 1])) throw std::runtime_error("DFCurve: times increasing");
            ln_df_.push_back(std::log(dfs[i]));
        }
    }

    static DFCurve fromZeroCC(const Date& origin,
                              const std::vector<double>& pillars,
                              const std::vector<double>& zero_cc) {
        if (pillars.size() != zero_cc.size()) throw std::runtime_error("fromZeroCC size mismatch");
        std::vector<double> dfs;
        dfs.reserve(pillars.size());
        for (size_t i = 0; i < pillars.size(); ++i) dfs.push_back(std::exp(-zero_cc[i] * pillars[i]));
        return DFCurve(origin, std::vector<double>(pillars.begin(), pillars.end()), std::move(dfs));
    }

    double df(double t) const {
        if (t <= 0.0) return 1.0;

        if (t <= t_.front()) {
            return interpolateDF(t, 0, 1);
        }
        if (t >= t_.back()) {
            size_t n = t_.size();
            return interpolateDF(t, n - 2, n - 1);
        }

        auto it = std::upper_bound(t_.begin(), t_.end(), t);
        size_t idx = static_cast<size_t>(it - t_.begin());
        return interpolateDF(t, idx - 1, idx);
    }

    double dfTo(const Date& dt) const {
        double t = act360(origin_, dt);
        return df(t);
    }

    // NEW: lnDF를 직접 사용해서 ratio를 exp(ln_to - ln_from)로 계산
    double dfFrom(const Date& from, const Date& to) const {
        double ln_from = lnDfTo(from);
        double ln_to   = lnDfTo(to);
        return std::exp(ln_to - ln_from);
    }

private:
    double interpolateLnDF(double t, size_t i0, size_t i1) const {
        double t0 = t_[i0], t1 = t_[i1];
        double y0 = ln_df_[i0], y1 = ln_df_[i1];
        double w = (t - t0) / (t1 - t0);
        return (y0 + w * (y1 - y0));
    }

    double interpolateDF(double t, size_t i0, size_t i1) const {
        return std::exp(interpolateLnDF(t, i0, i1));
    }

    double lnDf(double t) const {
        if (t <= 0.0) return 0.0; // ln(1)=0

        if (t <= t_.front()) return interpolateLnDF(t, 0, 1);
        if (t >= t_.back())  {
            size_t n = t_.size();
            return interpolateLnDF(t, n - 2, n - 1);
        }
        auto it = std::upper_bound(t_.begin(), t_.end(), t);
        size_t idx = static_cast<size_t>(it - t_.begin());
        return interpolateLnDF(t, idx - 1, idx);
    }

    double lnDfTo(const Date& dt) const {
        double t = act360(origin_, dt);
        return lnDf(t);
    }

    Date origin_;
    std::vector<double> t_;
    std::vector<double> ln_df_;
};

// ============================================================
// 6) Fixing store (sorted vector)
// ============================================================
class FixingStore {
public:
    using FixingPair = std::pair<int32_t, double>;

    void add(const Date& reset, double rate) {
        int32_t key = reset.serial;
        auto it = std::lower_bound(
            data_.begin(),
            data_.end(),
            key,
            [](const FixingPair& pair, int32_t value) { return pair.first < value; }
        );
        if (it != data_.end() && it->first == key) {
            it->second = rate;
        } else {
            data_.insert(it, {key, rate});
        }
    }

    bool has(const Date& reset) const {
        double rate = 0.0;
        return tryGet(reset, rate);
    }

    double get(const Date& reset) const {
        double rate = 0.0;
        if (!tryGet(reset, rate)) {
            throw std::runtime_error("Fixing missing for reset date");
        }
        return rate;
    }

    bool tryGet(const Date& reset, double& out) const {
        auto it = find(reset);
        if (it == data_.end()) return false;
        out = it->second;
        return true;
    }

private:
    std::vector<FixingPair>::const_iterator find(const Date& reset) const {
        int32_t key = reset.serial;
        auto it = std::lower_bound(
            data_.begin(),
            data_.end(),
            key,
            [](const FixingPair& pair, int32_t value) { return pair.first < value; }
        );
        if (it == data_.end() || it->first != key) return data_.end();
        return it;
    }

    std::vector<FixingPair> data_;
};

// ============================================================
// NEW (diff #2): origin 기준 start/end forward를 함수로 고정
// ============================================================
static double forwardSimpleFromCurve(const DFCurve& fwd, const Date& start, const Date& end, double accrual) {
    // 1 + L*alpha = DF(start)/DF(end) (origin 기준 term structure에서 start/end forward)
    double df_start = fwd.dfTo(start);
    double df_end   = fwd.dfTo(end);
    return (df_start / df_end - 1.0) / accrual;
}

// ============================================================
// 7) FRN pricer
//    - diff #1: current fixing 1회 조회 후 캐시
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
    const FixingStore& fixings,
    bool includePayOnValDate
) {
    FRNResult res{};
    if (periods.empty()) return res;

    int currentIdx = -1;
    double currentFixingRate = 0.0; // cache

    for (size_t i = 0; i < periods.size(); ++i) {
        const auto& p = periods[i];
        if (!(valDate < p.start) && (valDate < p.end)) {
            currentIdx = static_cast<int>(i);

            // cache fixing once
            currentFixingRate = fixings.get(p.reset);

            double accr = act360(p.start, valDate);
            if (accr < 0.0) accr = 0.0;
            if (accr > p.accrual) accr = p.accrual;
            res.accrued = notional * (currentFixingRate + spread) * accr;
            break;
        }
    }

    for (size_t i = 0; i < periods.size(); ++i) {
        const auto& p = periods[i];

        bool payAfterVal = includePayOnValDate ? !(p.pay < valDate) : (valDate < p.pay);
        if (!payAfterVal) continue;

        double df_pay = disc.dfFrom(valDate, p.pay);

        double rate = 0.0;
        bool inCurrent = (static_cast<int>(i) == currentIdx);

        if (inCurrent) {
            rate = currentFixingRate; // reuse cached
        } else if (valDate < p.start) {
            rate = forwardSimpleFromCurve(fwd, p.start, p.end, p.accrual);
        } else {
            double fixingRate = 0.0;
            if (fixings.tryGet(p.reset, fixingRate)) {
                rate = fixingRate;
            } else {
                rate = forwardSimpleFromCurve(fwd, p.start, p.end, p.accrual);
            }
        }

        double coupon = notional * (rate + spread) * p.accrual;
        res.dirty += coupon * df_pay;

        if (i + 1 == periods.size()) {
            res.dirty += notional * df_pay;
        }
    }

    res.clean = res.dirty - res.accrued;
    return res;
}

// ============================================================
// 8) Demo main
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

    assert(!periods.empty());
    for (const auto& p : periods) {
        assert(p.end.serial > p.start.serial);
        assert(p.accrual > 0.0);
        assert(cal.isBiz(p.pay));
    }

    const std::vector<double> pillars = {0.25, 0.5, 1.0, 2.0, 3.0, 5.0};

    const std::vector<double> z_disc = {0.030, 0.031, 0.032, 0.033, 0.0335, 0.034};
    DFCurve disc = DFCurve::fromZeroCC(issue, pillars, z_disc);

    const std::vector<double> z_fwd = {0.032, 0.033, 0.034, 0.035, 0.0355, 0.036};
    DFCurve fwd = DFCurve::fromZeroCC(issue, pillars, z_fwd);

    FixingStore fixings;
    for (const auto& p : periods) {
        if (!(val < p.start) && (val < p.end)) {
            fixings.add(p.reset, 0.035);
            break;
        }
    }

    double notional = 10000.0;
    double spread = 0.0100; // 100bp

    FRNResult r = priceFRN(val, periods, notional, spread, disc, fwd, fixings, /*includePayOnValDate*/false);

    std::cout << "\n[ FRN Valuation Report ]\n";
    std::cout << "Issue    : " << issue.y << "-" << issue.m << "-" << issue.d << "\n";
    std::cout << "Maturity : " << maturity.y << "-" << maturity.m << "-" << maturity.d << "\n";
    std::cout << "ValDate  : " << val.y << "-" << val.m << "-" << val.d << "\n";
    std::cout << "Notional : " << notional << "\n";
    std::cout << "Spread   : " << (spread * 10000.0) << " bp\n";
    std::cout << std::fixed << std::setprecision(6);
    std::cout << "\nAccrued Interest : " << r.accrued << "\n";
    std::cout << "Dirty Price(PV)  : " << r.dirty << "\n";
    std::cout << "Clean Price      : " << r.clean << "\n";

    double spread2 = spread + 0.0010; // +10bp
    FRNResult r2 = priceFRN(val, periods, notional, spread2, disc, fwd, fixings, /*includePayOnValDate*/false);
    std::cout << "\n[ Spread +10bp Check ]\n";
    std::cout << "Dirty(PV) +10bp  : " << r2.dirty << "\n";
    std::cout << "Delta PV         : " << (r2.dirty - r.dirty) << "\n";

    return 0;
}
