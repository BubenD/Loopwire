#include "analysis/Cma.h"

#include <algorithm>
#include <cmath>

namespace Loopwire {

namespace {
constexpr double kCmPerMm2 = 1973.525;
constexpr double kFtPerM = 3.280839895;
// IEC 60228 conductor sizes, mm2.
const double kMetric[] = {0.05, 0.08, 0.14, 0.25, 0.34, 0.5, 0.75, 1.0, 1.5, 2.5, 4.0, 6.0, 10.0, 16.0,
                          25.0, 35.0, 50.0, 70.0, 95.0, 120.0, 150.0, 185.0, 240.0};

double awgDiameterMils(int awg) { return 5.0 * std::pow(92.0, (36.0 - awg) / 39.0); }
} // namespace

QString cmaUnitName(CmaUnit unit) {
    switch (unit) {
    case CmaUnit::Awg: return QStringLiteral("AWG");
    case CmaUnit::Mm2: return QStringLiteral("mm2");
    case CmaUnit::Kcmil: return QStringLiteral("kcmil");
    case CmaUnit::Cmil: return QStringLiteral("CM");
    case CmaUnit::DiameterMm: return QStringLiteral("dia mm");
    }
    return QString();
}

double cmaFromMm2(double mm2) { return mm2 * kCmPerMm2; }
double mm2FromCma(double cm) { return cm / kCmPerMm2; }

double cmaOf(double size, CmaUnit unit) {
    switch (unit) {
    case CmaUnit::Awg: {
        if (size < -3.0 || size > 40.0) return -1.0;
        const double d = 5.0 * std::pow(92.0, (36.0 - size) / 39.0);
        return d * d;
    }
    case CmaUnit::Mm2: return size > 0.0 ? cmaFromMm2(size) : -1.0;
    case CmaUnit::Kcmil: return size > 0.0 ? size * 1000.0 : -1.0;
    case CmaUnit::Cmil: return size > 0.0 ? size : -1.0;
    case CmaUnit::DiameterMm: {
        if (size <= 0.0) return -1.0;
        const double mils = size / 0.0254;   // a mil is 0.0254 mm
        return mils * mils;
    }
    }
    return -1.0;
}

int awgForCma(double cm) {
    if (cm <= 0.0) return 40;
    // From the thinnest up: the first whose area is at least what is wanted.
    for (int awg = 40; awg >= -3; --awg) {
        const double d = awgDiameterMils(awg);
        if (d * d >= cm * (1.0 - 1e-9)) return awg;
    }
    return 999;   // bigger than 0000
}

double mm2ForCma(double cm) {
    const double want = mm2FromCma(cm);
    for (double s : kMetric)
        if (s >= want * (1.0 - 1e-9)) return s;
    return -1.0;   // past 240
}

QString awgName(int awg) {
    if (awg == 999) return QStringLiteral("bigger than 4/0");
    if (awg <= 0) return QStringLiteral("%1/0").arg(1 - awg);
    return QString::number(awg);
}

double cmaRequired(double amps, double lengthM, double dropVolts, double kOhmCmilPerFt) {
    if (amps <= 0.0 || lengthM <= 0.0 || dropVolts <= 0.0 || kOhmCmilPerFt <= 0.0) return -1.0;
    return 2.0 * kOhmCmilPerFt * amps * lengthM * kFtPerM / dropVolts;
}

double cmaTotal(const std::vector<CmaConductor>& conductors) {
    double sum = 0.0;
    for (const CmaConductor& c : conductors) sum += c.total();
    return sum;
}

} // namespace Loopwire
