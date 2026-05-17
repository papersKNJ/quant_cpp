#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace trf {

constexpr double DATE_EPS = 1.0e-12;
constexpr double SABR_EPS = 1.0e-12;
constexpr double DEFAULT_S0 = 1348.50;
constexpr double DEFAULT_BETA = 1.0;
constexpr int DEFAULT_N_PERIODS = 12;
constexpr double DEFAULT_NOTIONAL = 1'000'000.0;
constexpr double DEFAULT_LEVERAGE = 2.0;
constexpr double DEFAULT_TARGET_KRW = 300'000'000.0;

struct Date {
    int y{1970};
    int m{1};
    int d{1};

    static Date parse(const std::string& iso) {
        if (iso.size() != 10 || iso[4] != '-' || iso[7] != '-') {
            throw std::runtime_error("Date must be YYYY-MM-DD: " + iso);
        }
        return Date{std::stoi(iso.substr(0, 4)), std::stoi(iso.substr(5, 2)), std::stoi(iso.substr(8, 2))};
    }

    std::string str() const {
        std::ostringstream os;
        os << std::setfill('0') << std::setw(4) << y << '-' << std::setw(2) << m << '-' << std::setw(2) << d;
        return os.str();
    }

    static int daysInMonth(int yy, int mm) {
        static constexpr std::array<int, 12> mdays{31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (mm == 2 && ((yy % 4 == 0 && yy % 100 != 0) || (yy % 400 == 0))) return 29;
        return mdays.at(static_cast<size_t>(mm - 1));
    }

    Date addMonths(int months) const {
        int yy = y;
        int mm = m + months;
        while (mm > 12) { mm -= 12; ++yy; }
        while (mm < 1) { mm += 12; --yy; }
        return Date{yy, mm, std::min(d, daysInMonth(yy, mm))};
    }

    // Howard Hinnant civil calendar algorithm: days from 1970-01-01.
    int serial() const {
        int yy = y - (m <= 2);
        const int era = (yy >= 0 ? yy : yy - 399) / 400;
        const auto yoe = static_cast<unsigned>(yy - era * 400);
        const auto mp = static_cast<unsigned>(m + (m > 2 ? -3 : 9));
        const auto doy = (153 * mp + 2) / 5 + static_cast<unsigned>(d) - 1;
        const auto doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + static_cast<int>(doe) - 719468;
    }
};

static double yearFraction365(const Date& from, const Date& to) {
    return static_cast<double>(to.serial() - from.serial()) / 365.0;
}

class LinearInterpolator {
public:
    LinearInterpolator() = default;
    LinearInterpolator(std::vector<double> x, std::vector<double> y) : x_(std::move(x)), y_(std::move(y)) {
        if (x_.size() < 2 || x_.size() != y_.size()) throw std::runtime_error("invalid interpolation nodes");
        for (size_t i = 1; i < x_.size(); ++i) {
            if (!(x_[i] > x_[i - 1])) throw std::runtime_error("interpolation x nodes must be strictly increasing");
        }
    }

    double operator()(double x) const {
        if (x <= x_.front()) return interp(x, 0, 1);
        if (x >= x_.back()) return interp(x, x_.size() - 2, x_.size() - 1);
        auto it = std::upper_bound(x_.begin(), x_.end(), x);
        const size_t idx = static_cast<size_t>(it - x_.begin());
        return interp(x, idx - 1, idx);
    }

private:
    double interp(double x, size_t i0, size_t i1) const {
        const double w = (x - x_[i0]) / (x_[i1] - x_[i0]);
        return y_[i0] + w * (y_[i1] - y_[i0]);
    }

    std::vector<double> x_;
    std::vector<double> y_;
};

class LoadedCurve {
public:
    LoadedCurve() = default;
    LoadedCurve(std::vector<double> t, std::vector<double> df) {
        if (t.size() < 2 || t.size() != df.size()) throw std::runtime_error("invalid curve nodes");
        std::vector<size_t> order(t.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return t[a] < t[b]; });
        T_.reserve(t.size());
        logDf_.reserve(df.size());
        for (size_t idx : order) {
            if (df[idx] <= 0.0) throw std::runtime_error("discount factors must be positive");
            T_.push_back(t[idx]);
            logDf_.push_back(std::log(df[idx]));
        }
        logDfInterp_ = LinearInterpolator(T_, logDf_);
    }

    double df(double t) const {
        if (t <= 0.0) return 1.0;
        return std::exp(logDfInterp_(t));
    }

    double zero(double t) const {
        return -std::log(df(t)) / std::max(t, 1.0e-12);
    }

private:
    std::vector<double> T_;
    std::vector<double> logDf_;
    LinearInterpolator logDfInterp_;
};

struct MarketCurves {
    LoadedCurve usd;
    LoadedCurve krw;
};

struct SabrNode {
    std::string expiry;
    double T{};
    double alpha{};
    double rho{};
    double nu{};
    double beta{1.0};
};

struct SabrSurface {
    explicit SabrSurface(std::vector<SabrNode> nodes) : nodes_(std::move(nodes)) {
        std::sort(nodes_.begin(), nodes_.end(), [](const SabrNode& a, const SabrNode& b) { return a.T < b.T; });
        if (nodes_.size() < 2) throw std::runtime_error("need at least two SABR parameter nodes");
        std::vector<double> t, alphaVar, rho, nu;
        for (const auto& n : nodes_) {
            t.push_back(n.T);
            alphaNodes_.push_back(n.alpha);
            alphaVar.push_back(n.alpha * n.alpha * n.T);
            rho.push_back(n.rho);
            nu.push_back(n.nu);
        }
        alphaT_ = t;
        alphaVarInterp_ = LinearInterpolator(t, alphaVar);
        rhoInterp_ = LinearInterpolator(t, rho);
        nuInterp_ = LinearInterpolator(t, nu);
    }

    double alpha(double t) const {
        t = std::max(t, 1.0e-8);
        if (t <= alphaT_.front()) return alphaNodes_.front();
        if (t >= alphaT_.back()) return alphaNodes_.back();
        return std::sqrt(std::max(alphaVarInterp_(t), 0.0) / t);
    }

    double rho(double t) const { return std::clamp(rhoInterp_(t), -0.9999, 0.9999); }
    double nu(double t) const { return std::max(nuInterp_(t), 1.0e-12); }
    const std::vector<SabrNode>& nodes() const { return nodes_; }

private:
    std::vector<SabrNode> nodes_;
    std::vector<double> alphaT_;
    std::vector<double> alphaNodes_;
    LinearInterpolator alphaVarInterp_;
    LinearInterpolator rhoInterp_;
    LinearInterpolator nuInterp_;
};

static std::vector<std::string> splitCsvLine(const std::string& line) {
    std::vector<std::string> out;
    std::string cell;
    bool quoted = false;
    for (char c : line) {
        if (c == '"') quoted = !quoted;
        else if (c == ',' && !quoted) { out.push_back(cell); cell.clear(); }
        else cell.push_back(c);
    }
    out.push_back(cell);
    return out;
}

static std::vector<std::unordered_map<std::string, std::string>> readCsv(const std::string& path) {
    std::ifstream in(path);
    if (!in) return {};
    std::string line;
    if (!std::getline(in, line)) return {};
    const auto headers = splitCsvLine(line);
    std::vector<std::unordered_map<std::string, std::string>> rows;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        const auto cells = splitCsvLine(line);
        std::unordered_map<std::string, std::string> row;
        for (size_t i = 0; i < headers.size() && i < cells.size(); ++i) row[headers[i]] = cells[i];
        rows.push_back(std::move(row));
    }
    return rows;
}

static MarketCurves loadMarketCurves(const std::string& path) {
    const auto rows = readCsv(path);
    if (rows.empty()) {
        std::cerr << "warning: " << path << " not found; using embedded demonstration curves\n";
        const std::vector<double> t{1.0 / 12.0, 2.0 / 12.0, 3.0 / 12.0, 6.0 / 12.0, 9.0 / 12.0, 1.0, 2.0};
        const std::vector<double> usdZ{0.052, 0.052, 0.052, 0.051, 0.050, 0.049, 0.046};
        const std::vector<double> krwZ{0.036, 0.0365, 0.037, 0.0375, 0.038, 0.0385, 0.039};
        std::vector<double> usdDf, krwDf;
        for (size_t i = 0; i < t.size(); ++i) {
            usdDf.push_back(std::exp(-usdZ[i] * t[i]));
            krwDf.push_back(std::exp(-krwZ[i] * t[i]));
        }
        return MarketCurves{LoadedCurve(t, usdDf), LoadedCurve(t, krwDf)};
    }
    std::vector<double> t, usdDf, krwDf;
    for (const auto& row : rows) {
        t.push_back(std::stod(row.at("T")));
        usdDf.push_back(std::stod(row.at("df_usd")));
        krwDf.push_back(std::stod(row.at("df_krw")));
    }
    return MarketCurves{LoadedCurve(t, usdDf), LoadedCurve(t, krwDf)};
}

static SabrSurface loadSabrSurface(const std::string& path, double beta) {
    const auto rows = readCsv(path);
    std::vector<SabrNode> nodes;
    if (rows.empty()) {
        std::cerr << "warning: " << path << " not found; using embedded demonstration SABR parameters\n";
        for (int i = 1; i <= 12; ++i) {
            const double t = static_cast<double>(i) / 12.0;
            nodes.push_back(SabrNode{std::to_string(i) + "M", t, 0.105 + 0.004 * std::sqrt(t), -0.20 + 0.02 * t, 0.45, beta});
        }
        return SabrSurface(std::move(nodes));
    }
    for (const auto& row : rows) {
        SabrNode node;
        node.expiry = row.at("expiry");
        node.T = std::stod(row.at("T"));
        node.alpha = std::stod(row.at("alpha"));
        node.rho = std::stod(row.at("rho"));
        node.nu = std::stod(row.at("nu"));
        node.beta = std::stod(row.at("beta"));
        if (std::abs(node.beta - beta) > 1.0e-10) throw std::runtime_error("SABR beta mismatch");
        nodes.push_back(node);
    }
    return SabrSurface(std::move(nodes));
}

[[maybe_unused]] static double normalCdf(double x) {
    return 0.5 * std::erfc(-x / std::sqrt(2.0));
}

[[maybe_unused]] static double sabrVol(double F, double K, double T, double a, double b, double r, double v) {
    T = std::max(T, 1.0e-8);
    a = std::max(a, 1.0e-12);
    r = std::clamp(r, -0.9999, 0.9999);
    v = std::max(v, 1.0e-12);
    const double logFK = std::log(F / K);
    const double omb = 1.0 - b;
    if (std::abs(logFK) < 1.0e-8) {
        return (a / std::pow(F, omb)) *
               (1.0 + ((omb * omb / 24.0) * a * a / std::pow(F, 2.0 * omb) +
                       (r * b * v * a / 4.0) / std::pow(F, omb) +
                       ((2.0 - 3.0 * r * r) / 24.0) * v * v) * T);
    }
    const double FK = F * K;
    const double FKb = std::pow(FK, omb / 2.0);
    const double z = (v / a) * FKb * logFK;
    const double sq = std::max(1.0 - 2.0 * r * z + z * z, 0.0);
    const double arg = std::max((std::sqrt(sq) + z - r) / (1.0 - r), 1.0e-10);
    const double xz = std::log(arg);
    const double zOverXz = (std::abs(xz) < 1.0e-12) ? 1.0 : z / xz;
    const double A = a / (FKb * (1.0 + (omb * omb / 24.0) * logFK * logFK +
                                  (std::pow(omb, 4.0) / 1920.0) * std::pow(logFK, 4.0)));
    const double B = (omb * omb / 24.0) * a * a / std::pow(FK, omb) +
                     (r * b * v * a / 4.0) / std::pow(FK, omb / 2.0) +
                     ((2.0 - 3.0 * r * r) / 24.0) * v * v;
    return A * zOverXz * (1.0 + B * T);
}

[[maybe_unused]] static double gkPut(double F, double K, double T, double sigma, double dfDomestic) {
    if (T <= 0.0 || sigma <= 0.0) return dfDomestic * std::max(K - F, 0.0);
    const double sq = sigma * std::sqrt(T);
    const double d1 = (std::log(F / K) + 0.5 * sigma * sigma * T) / sq;
    return dfDomestic * (K * normalCdf(-(d1 - sq)) - F * normalCdf(-d1));
}

struct MarketModel {
    std::vector<double> T;
    std::vector<double> F;
    std::vector<double> df;
    std::vector<double> alpha;
    std::vector<double> rho;
    std::vector<double> nu;
    double S0{};
};

static MarketModel buildSabrModel(const std::vector<double>& fixT,
                                  const MarketCurves& curves,
                                  const SabrSurface& sabr,
                                  double S0,
                                  double alphaShift,
                                  double elapsed) {
    MarketModel mm;
    mm.S0 = S0;
    const double usdElapsed = curves.usd.df(elapsed);
    const double krwElapsed = curves.krw.df(elapsed);
    for (double originalT : fixT) {
        const double remainingT = originalT - elapsed;
        if (remainingT <= DATE_EPS) continue;
        mm.T.push_back(remainingT);
        const double usdForwardDf = curves.usd.df(originalT) / usdElapsed;
        const double krwForwardDf = curves.krw.df(originalT) / krwElapsed;
        mm.F.push_back(S0 * usdForwardDf / krwForwardDf);
        mm.df.push_back(krwForwardDf);
        mm.alpha.push_back(std::max(sabr.alpha(originalT) + alphaShift, SABR_EPS));
        mm.rho.push_back(sabr.rho(originalT));
        mm.nu.push_back(sabr.nu(originalT));
    }
    if (mm.T.empty()) throw std::runtime_error("no remaining fixing dates after elapsed bump");
    return mm;
}

struct Paths {
    std::vector<std::vector<double>> S;
    std::vector<std::vector<double>> alpha;
};

struct RandomNormals {
    std::vector<std::vector<double>> spot;
    std::vector<std::vector<double>> vol;
};

static RandomNormals makeAntitheticNormals(size_t nSim, size_t nSteps, uint64_t seed) {
    if (nSim < 2) throw std::runtime_error("nSim must be at least 2");
    if (nSim % 2 != 0) ++nSim;
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> nd(0.0, 1.0);
    RandomNormals z;
    z.spot.assign(nSim, std::vector<double>(nSteps));
    z.vol.assign(nSim, std::vector<double>(nSteps));
    const size_t half = nSim / 2;
    for (size_t i = 0; i < half; ++i) {
        for (size_t j = 0; j < nSteps; ++j) {
            const double zs = nd(rng);
            const double zv = nd(rng);
            z.spot[i][j] = zs;
            z.vol[i][j] = zv;
            z.spot[i + half][j] = -zs;
            z.vol[i + half][j] = -zv;
        }
    }
    return z;
}

static Paths sampleSabrModel(const MarketModel& mm, const RandomNormals& z) {
    const size_t nPaths = z.spot.size();
    const size_t nSteps = mm.T.size();
    if (z.vol.size() != nPaths || z.spot.front().size() < nSteps || z.vol.front().size() < nSteps) {
        throw std::runtime_error("normal matrix shape does not match market model");
    }
    Paths paths;
    paths.S.assign(nPaths, std::vector<double>(nSteps));
    paths.alpha.assign(nPaths, std::vector<double>(nSteps));

    std::vector<double> spot(nPaths, mm.S0);
    std::vector<double> alphaState(nPaths, std::max(mm.alpha.front(), SABR_EPS));

    double tPrev = 0.0;
    double fPrev = mm.S0;
    for (size_t step = 0; step < nSteps; ++step) {
        const double tCur = mm.T[step];
        const double dt = tCur - tPrev;
        if (dt <= 0.0) throw std::runtime_error("non-positive SABR time step");
        const double sqrtDt = std::sqrt(dt);
        const double fCur = mm.F[step];
        const double alphaAnchor = std::max(mm.alpha[step], SABR_EPS);
        const double rho = std::clamp(mm.rho[step], -0.999999, 0.999999);
        const double nu = std::max(mm.nu[step], 0.0);
        if (step > 0) {
            const double prevAnchor = std::max(mm.alpha[step - 1], SABR_EPS);
            const double ratio = alphaAnchor / prevAnchor;
            for (double& a : alphaState) a = std::max(a * ratio, SABR_EPS);
        }
        const double rhoPerp = std::sqrt(std::max(1.0 - rho * rho, 0.0));
        const double fwdDrift = std::log(fCur / fPrev) / dt;
        for (size_t p = 0; p < nPaths; ++p) {
            const double zSpot = z.spot[p][step];
            const double zAlpha = rho * zSpot + rhoPerp * z.vol[p][step];
            const double alphaNext = std::max(alphaState[p] * std::exp(-0.5 * nu * nu * dt + nu * sqrtDt * zAlpha), SABR_EPS);
            const double instVol = 0.5 * (alphaState[p] + alphaNext);
            spot[p] = std::max(spot[p] * std::exp((fwdDrift - 0.5 * instVol * instVol) * dt + instVol * sqrtDt * zSpot), SABR_EPS);
            paths.S[p][step] = spot[p];
            paths.alpha[p][step] = alphaNext;
            alphaState[p] = alphaNext;
        }
        tPrev = tCur;
        fPrev = fCur;
    }
    return paths;
}

struct PathBundle {
    Paths paths;
    MarketModel model;
};

static PathBundle buildMarketPaths(const std::vector<double>& fixT,
                                   const MarketCurves& curves,
                                   const SabrSurface& sabr,
                                   const RandomNormals& z,
                                   double S0,
                                   double alphaShift = 0.0,
                                   double elapsed = 0.0) {
    MarketModel model = buildSabrModel(fixT, curves, sabr, S0, alphaShift, elapsed);
    Paths paths = sampleSabrModel(model, z);
    return PathBundle{std::move(paths), std::move(model)};
}

struct PvResult {
    double mean{};
    double se{};
};

static PvResult pvMc(double K,
                     const std::vector<std::vector<double>>& S,
                     const std::vector<double>& df,
                     double leverage,
                     double target,
                     double notional,
                     int nPer,
                     double kiBarrier = std::numeric_limits<double>::quiet_NaN()) {
    const size_t nPaths = S.size();
    const size_t np = static_cast<size_t>(nPer <= 0 ? static_cast<int>(S.front().size()) : nPer);
    double sum = 0.0;
    double sum2 = 0.0;
    const bool hasKi = !std::isnan(kiBarrier);

    for (size_t p = 0; p < nPaths; ++p) {
        double cumGain = 0.0;
        double pv = 0.0;
        bool knockedIn = false;
        for (size_t i = 0; i < np; ++i) {
            const double s = S[p][i];
            if (hasKi && s > kiBarrier) knockedIn = true;
            double gain = std::max(K - s, 0.0) * notional;
            const double rawLoss = std::max(s - K, 0.0) * leverage * notional;
            const double loss = (!hasKi || knockedIn) ? rawLoss : 0.0;
            if (cumGain + gain >= target) gain = std::max(target - cumGain, 0.0);
            pv += (gain - loss) * df[i];
            cumGain += std::max(K - s, 0.0) * notional;
            if (cumGain >= target) break;
        }
        sum += pv;
        sum2 += pv * pv;
    }
    const double mean = sum / static_cast<double>(nPaths);
    const double var = std::max(sum2 / static_cast<double>(nPaths) - mean * mean, 0.0);
    return PvResult{mean, std::sqrt(var / static_cast<double>(nPaths))};
}

template <typename F>
static double solveRootExpand(F objective,
                              double center,
                              double width,
                              double loFloor,
                              double hiCap,
                              double xtol,
                              const std::string& label) {
    double lo = std::max(loFloor, center - width);
    double hi = std::min(hiCap, center + width);
    double flo = objective(lo);
    double fhi = objective(hi);
    for (int k = 0; k < 12 && flo * fhi > 0.0; ++k) {
        width *= 1.6;
        lo = std::max(loFloor, center - width);
        hi = std::min(hiCap, center + width);
        flo = objective(lo);
        fhi = objective(hi);
    }
    if (flo * fhi > 0.0) {
        std::ostringstream os;
        os << "could not bracket root for " << label << ": f(" << lo << ")=" << flo << ", f(" << hi << ")=" << fhi;
        throw std::runtime_error(os.str());
    }
    for (int iter = 0; iter < 100; ++iter) {
        const double mid = 0.5 * (lo + hi);
        const double fmid = objective(mid);
        if (std::abs(hi - lo) <= xtol || std::abs(fmid) < 1.0e-8) return mid;
        if (flo * fmid <= 0.0) {
            hi = mid;
            fhi = fmid;
        } else {
            lo = mid;
            flo = fmid;
        }
    }
    return 0.5 * (lo + hi);
}

static double solveBarrierMc(const std::vector<std::vector<double>>& S,
                             const std::vector<double>& df,
                             double K,
                             double leverage,
                             double target,
                             double notional,
                             int nPer,
                             double bInit,
                             double bracket,
                             double xtol,
                             const std::string& label) {
    const double center = std::isnan(bInit) ? K + 200.0 : bInit;
    auto objective = [&](double B) {
        return pvMc(K, S, df, leverage, target, notional, nPer, B).mean;
    };
    return solveRootExpand(objective, center, bracket, K + 1.0, std::max(1900.0, center + 3.0 * bracket), xtol, label);
}

[[maybe_unused]] static std::vector<double> percentilesAtTimeZeroPrep(const std::vector<double>& values, const std::array<double, 5>& qs) {
    std::vector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    std::vector<double> out;
    out.reserve(qs.size());
    for (double q : qs) {
        const double pos = q * static_cast<double>(sorted.size() - 1);
        const size_t lo = static_cast<size_t>(std::floor(pos));
        const size_t hi = static_cast<size_t>(std::ceil(pos));
        const double w = pos - static_cast<double>(lo);
        out.push_back(sorted[lo] + w * (sorted[hi] - sorted[lo]));
    }
    return out;
}

struct Config {
    std::string date = "2023-09-26";
    std::string marketCurvesFile = "market_curves.csv";
    std::string sabrParamsFile = "sabr_params.csv";
    double S0 = DEFAULT_S0;
    double beta = DEFAULT_BETA;
    int nPeriods = DEFAULT_N_PERIODS;
    double notional = DEFAULT_NOTIONAL;
    double leverage = DEFAULT_LEVERAGE;
    double targetKrw = DEFAULT_TARGET_KRW;
    size_t nSim = 500'000;
    uint64_t seed = 42;
    bool greeks = false;
    bool sensitivity = false;
};

static Config parseArgs(int argc, char** argv) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto requireValue = [&](const std::string& flag) -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("missing value for " + flag);
            return argv[++i];
        };
        if (arg == "--n-sim") cfg.nSim = static_cast<size_t>(std::stoull(requireValue(arg)));
        else if (arg == "--seed") cfg.seed = static_cast<uint64_t>(std::stoull(requireValue(arg)));
        else if (arg == "--market-curves") cfg.marketCurvesFile = requireValue(arg);
        else if (arg == "--sabr-params") cfg.sabrParamsFile = requireValue(arg);
        else if (arg == "--greeks") cfg.greeks = true;
        else if (arg == "--sensitivity") cfg.sensitivity = true;
        else if (arg == "--all") { cfg.greeks = true; cfg.sensitivity = true; }
        else if (arg == "--help") {
            std::cout << "Usage: trf_sabr [--n-sim N] [--seed N] [--market-curves file] [--sabr-params file] [--greeks] [--sensitivity] [--all]\n";
            std::exit(0);
        } else {
            throw std::runtime_error("unknown argument: " + arg);
        }
    }
    if (cfg.nSim % 2 != 0) ++cfg.nSim;
    return cfg;
}

static void printFixingSchedule(const Date& valuationDate, int nPeriods, std::vector<Date>& fixDates, std::vector<double>& fixT) {
    std::cout << std::left << std::setw(8) << "Period" << std::setw(14) << "Date" << std::right << std::setw(8) << "T" << '\n';
    for (int i = 0; i < nPeriods; ++i) {
        Date d = valuationDate.addMonths(i + 1);
        double t = yearFraction365(valuationDate, d);
        fixDates.push_back(d);
        fixT.push_back(t);
        std::cout << std::left << std::setw(8) << (i + 1) << std::setw(14) << d.str()
                  << std::right << std::setw(8) << std::fixed << std::setprecision(4) << t << '\n';
    }
}

static void runGreeks(const Config& cfg,
                      const std::vector<double>& fixT,
                      const MarketCurves& curves,
                      const SabrSurface& sabr,
                      const RandomNormals& z,
                      double KFixed,
                      double Bmc) {
    constexpr double bumpS = 1.0;
    constexpr double bumpSig = 1.0e-4;
    constexpr double bumpT = 1.0 / 365.0;
    auto solveFor = [&](double s0, double alphaShift, double elapsed, const std::string& label) {
        auto bundle = buildMarketPaths(fixT, curves, sabr, z, s0, alphaShift, elapsed);
        return solveBarrierMc(bundle.paths.S, bundle.model.df, KFixed, cfg.leverage, cfg.targetKrw,
                              cfg.notional, static_cast<int>(bundle.model.T.size()), Bmc, 200.0, 0.05, label);
    };

    const double B_Sup = solveFor(cfg.S0 + bumpS, 0.0, 0.0, "MC spot up");
    const double B_Sdn = solveFor(cfg.S0 - bumpS, 0.0, 0.0, "MC spot down");
    const double B_Vup = solveFor(cfg.S0, +bumpSig, 0.0, "MC alpha up");
    const double B_Vdn = solveFor(cfg.S0, -bumpSig, 0.0, "MC alpha down");
    const double B_pp = solveFor(cfg.S0 + bumpS, +bumpSig, 0.0, "MC ++");
    const double B_mp = solveFor(cfg.S0 - bumpS, +bumpSig, 0.0, "MC -+");
    const double B_pm = solveFor(cfg.S0 + bumpS, -bumpSig, 0.0, "MC +-");
    const double B_mm = solveFor(cfg.S0 - bumpS, -bumpSig, 0.0, "MC --");
    const double B_roll = solveFor(cfg.S0, 0.0, bumpT, "MC roll");

    std::cout << "\n==========================================================\n";
    std::cout << "  TRF Fair-barrier Greeks -- sensitivity of zero-cost KI Barrier B\n";
    std::cout << "==========================================================\n";
    std::cout << std::setw(12) << std::left << "Greek" << std::setw(20) << "Definition" << std::right << std::setw(14) << "MC" << "  Unit\n";
    std::cout << "----------------------------------------------------------\n";
    const std::vector<std::tuple<std::string, std::string, double, std::string>> rows{
        {"Delta", "dB/dS0", (B_Sup - B_Sdn) / (2.0 * bumpS), "KRW/KRW"},
        {"Gamma", "d2B/dS0^2 x100", (B_Sup - 2.0 * Bmc + B_Sdn) / (bumpS * bumpS) * 100.0, "per 100 KRW^2"},
        {"Roll-down", "B(t+1d)-B(t)", B_roll - Bmc, "KRW/day"},
        {"Vega", "dB per 1bp alpha", (B_Vup - B_Vdn) / 2.0, "KRW/bp"},
        {"Vanna", "dVega/dS0", (B_pp - B_mp - B_pm + B_mm) / (4.0 * bumpS), "KRW/KRW/bp"},
        {"Volga", "d2B/dalpha^2", B_Vup - 2.0 * Bmc + B_Vdn, "KRW/(bp^2)"}
    };
    for (const auto& [name, def, value, unit] : rows) {
        std::cout << std::left << std::setw(12) << name << std::setw(20) << def
                  << std::right << std::showpos << std::setw(14) << std::fixed << std::setprecision(4) << value
                  << std::noshowpos << "  " << unit << '\n';
    }
}

static double safeSolveBarrier(const std::vector<std::vector<double>>& S,
                               const std::vector<double>& df,
                               double K,
                               double leverage,
                               double target,
                               double notional,
                               int nPer,
                               double Bmc) {
    try {
        return solveBarrierMc(S, df, K, leverage, target, notional, nPer, Bmc, 300.0, 0.05, "parameter sensitivity");
    } catch (const std::exception&) {
        return std::numeric_limits<double>::quiet_NaN();
    }
}

static void runSensitivity(const Config& cfg,
                           const std::vector<std::vector<double>>& S,
                           const std::vector<double>& df,
                           double KFixed,
                           double Bmc) {
    std::cout << "\n========================================================\n";
    std::cout << "  KI Barrier Sensitivity Summary (base B_mc = " << std::fixed << std::setprecision(2) << Bmc << ")\n";
    std::cout << "========================================================\n";

    std::vector<double> levGrid;
    for (double x = 1.0; x <= 3.5001; x += 0.25) levGrid.push_back(x);
    std::vector<double> Blev;
    for (double lv : levGrid) Blev.push_back(safeSolveBarrier(S, df, KFixed, lv, cfg.targetKrw, cfg.notional, cfg.nPeriods, Bmc));

    std::vector<double> targetGrid;
    for (double x = 100.0; x <= 600.1; x += 50.0) targetGrid.push_back(x * 1'000'000.0);
    std::vector<double> Btgt;
    for (double tg : targetGrid) Btgt.push_back(safeSolveBarrier(S, df, KFixed, cfg.leverage, tg, cfg.notional, cfg.nPeriods, Bmc));

    std::vector<int> nGrid{2, 3, 4, 6, 8, 10, 12};
    std::vector<double> Bn;
    for (int n : nGrid) Bn.push_back(safeSolveBarrier(S, df, KFixed, cfg.leverage, cfg.targetKrw, cfg.notional, n, Bmc));

    auto gradientAt = [](const auto& x, const auto& y, size_t i) {
        if (i == 0) return (y[1] - y[0]) / (x[1] - x[0]);
        if (i + 1 == y.size()) return (y[i] - y[i - 1]) / (x[i] - x[i - 1]);
        return (y[i + 1] - y[i - 1]) / (x[i + 1] - x[i - 1]);
    };
    const size_t iLev = static_cast<size_t>(std::distance(levGrid.begin(), std::min_element(levGrid.begin(), levGrid.end(), [&](double a, double b) {
        return std::abs(a - cfg.leverage) < std::abs(b - cfg.leverage);
    })));
    std::vector<double> targetGridM;
    for (double tg : targetGrid) targetGridM.push_back(tg / 1.0e6);
    const size_t iTgt = static_cast<size_t>(std::distance(targetGridM.begin(), std::min_element(targetGridM.begin(), targetGridM.end(), [&](double a, double b) {
        return std::abs(a - cfg.targetKrw / 1.0e6) < std::abs(b - cfg.targetKrw / 1.0e6);
    })));
    std::vector<double> nGridDouble(nGrid.begin(), nGrid.end());
    const size_t iN = static_cast<size_t>(std::distance(nGrid.begin(), std::find(nGrid.begin(), nGrid.end(), cfg.nPeriods)));

    std::cout << "dB/dlambda at lambda=" << cfg.leverage << "    : " << std::showpos << gradientAt(levGrid, Blev, iLev) << std::noshowpos << " KRW per unit leverage\n";
    std::cout << "dB/dT* at T*=" << cfg.targetKrw / 1.0e6 << "M : " << std::showpos << gradientAt(targetGridM, Btgt, iTgt) << std::noshowpos << " KRW per MKRW target\n";
    std::cout << "dB/dN at N=" << cfg.nPeriods << "             : " << std::showpos << gradientAt(nGridDouble, Bn, iN) << std::noshowpos << " KRW per period\n";
}

} // namespace trf

int main(int argc, char** argv) {
    try {
        using namespace trf;
        const Config cfg = parseArgs(argc, argv);
        const Date valuationDate = Date::parse(cfg.date);

        std::vector<Date> fixDates;
        std::vector<double> fixT;
        printFixingSchedule(valuationDate, cfg.nPeriods, fixDates, fixT);

        const MarketCurves curves = loadMarketCurves(cfg.marketCurvesFile);
        const SabrSurface sabr = loadSabrSurface(cfg.sabrParamsFile, cfg.beta);

        std::cout << "\n" << std::left << std::setw(8) << "Period" << std::right << std::setw(8) << "T"
                  << std::setw(12) << "Forward" << std::setw(12) << "DF_KRW" << '\n';
        for (int i = 0; i < cfg.nPeriods; ++i) {
            const double fwd = cfg.S0 * curves.usd.df(fixT[static_cast<size_t>(i)]) / curves.krw.df(fixT[static_cast<size_t>(i)]);
            const double df = curves.krw.df(fixT[static_cast<size_t>(i)]);
            std::cout << std::left << std::setw(8) << (i + 1) << std::right << std::fixed << std::setprecision(4)
                      << std::setw(8) << fixT[static_cast<size_t>(i)] << std::setprecision(2) << std::setw(12) << fwd
                      << std::setprecision(6) << std::setw(12) << df << '\n';
        }

        std::cout << "\nSABR parameters\n";
        std::cout << std::left << std::setw(10) << "expiry" << std::right << std::setw(8) << "T"
                  << std::setw(12) << "alpha" << std::setw(12) << "rho" << std::setw(12) << "nu" << '\n';
        for (const auto& n : sabr.nodes()) {
            std::cout << std::left << std::setw(10) << n.expiry << std::right << std::fixed << std::setprecision(4)
                      << std::setw(8) << n.T << std::setw(12) << n.alpha << std::setw(12) << n.rho << std::setw(12) << n.nu << '\n';
        }

        const auto started = std::chrono::steady_clock::now();
        RandomNormals z = makeAntitheticNormals(cfg.nSim, static_cast<size_t>(cfg.nPeriods), cfg.seed);
        auto base = buildMarketPaths(fixT, curves, sabr, z, cfg.S0);
        std::cout << "\nSimulated full SABR paths : " << base.paths.S.size() << " (seed=" << cfg.seed << ")\n";
        std::cout << std::right << std::setw(7) << "Period" << std::setw(8) << "T" << std::setw(12) << "F(0,t_i)"
                  << std::setw(12) << "E[S]_MC" << std::setw(10) << "bias" << std::setw(12) << "E[alpha]" << '\n';
        for (size_t i = 0; i < base.model.T.size(); ++i) {
            double meanS = 0.0;
            double meanAlpha = 0.0;
            for (size_t p = 0; p < base.paths.S.size(); ++p) {
                meanS += base.paths.S[p][i];
                meanAlpha += base.paths.alpha[p][i];
            }
            meanS /= static_cast<double>(base.paths.S.size());
            meanAlpha /= static_cast<double>(base.paths.S.size());
            const double bias = (meanS / base.model.F[i] - 1.0) * 100.0;
            std::cout << std::setw(7) << (i + 1) << std::fixed << std::setprecision(4) << std::setw(8) << base.model.T[i]
                      << std::setprecision(2) << std::setw(12) << base.model.F[i] << std::setw(12) << meanS
                      << std::showpos << std::setprecision(3) << std::setw(9) << bias << std::noshowpos << "%"
                      << std::setprecision(3) << std::setw(11) << meanAlpha * 100.0 << "%\n";
        }

        const double KFixed = std::round(cfg.S0 * 1.02 * 100.0) / 100.0;
        const double Bmc = solveBarrierMc(base.paths.S, base.model.df, KFixed, cfg.leverage, cfg.targetKrw, cfg.notional,
                                          static_cast<int>(base.model.T.size()), KFixed + 150.0, 200.0, 0.01, "base full SABR B");
        const PvResult pvAtB = pvMc(KFixed, base.paths.S, base.model.df, cfg.leverage, cfg.targetKrw, cfg.notional,
                                    static_cast<int>(base.model.T.size()), Bmc);
        const double dPvDb = (pvMc(KFixed, base.paths.S, base.model.df, cfg.leverage, cfg.targetKrw, cfg.notional,
                                   static_cast<int>(base.model.T.size()), Bmc + 1.0).mean -
                             pvMc(KFixed, base.paths.S, base.model.df, cfg.leverage, cfg.targetKrw, cfg.notional,
                                  static_cast<int>(base.model.T.size()), Bmc - 1.0).mean) / 2.0;
        const double bSe = pvAtB.se / std::max(std::abs(dPvDb), 1.0e-12);

        std::cout << "\n================================================================\n";
        std::cout << "  TRF KI Barrier Pricing -- Results\n";
        std::cout << "================================================================\n";
        std::cout << "  Spot S0             : " << std::fixed << std::setprecision(2) << cfg.S0 << "\n";
        std::cout << "  Strike K (fixed)    : " << KFixed << " (= S0 * 1.02)\n";
        std::cout << "  Notional / period   : USD " << std::fixed << std::setprecision(0) << cfg.notional << "\n";
        std::cout << "  Leverage            : " << std::setprecision(2) << cfg.leverage << "x\n";
        std::cout << "  Target              : KRW " << std::setprecision(0) << cfg.targetKrw << "\n";
        std::cout << "  Periods             : " << base.model.T.size() << " (monthly)\n";
        std::cout << "  KO convention       : pay current fixing payoff, then test target\n";
        std::cout << "  KI convention       : once S_i > B, loss leg is activated thereafter\n\n";
        std::cout << "  KI Barrier B (SABR MC) : " << std::fixed << std::setprecision(4) << Bmc << " KRW/USD (+/-" << std::setprecision(2) << bSe << ")\n";
        std::cout << "  B - K spread           : " << std::setprecision(4) << (Bmc - KFixed) << " KRW/USD\n";
        std::cout << "  PV at B                : " << std::fixed << std::setprecision(0) << pvAtB.mean << " KRW\n";
        std::cout << "================================================================\n";

        if (cfg.greeks) runGreeks(cfg, fixT, curves, sabr, z, KFixed, Bmc);
        if (cfg.sensitivity) runSensitivity(cfg, base.paths.S, base.model.df, KFixed, Bmc);

        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        std::cout << "\nCompleted in " << std::fixed << std::setprecision(2) << elapsed << " seconds\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << '\n';
        return 1;
    }
}
