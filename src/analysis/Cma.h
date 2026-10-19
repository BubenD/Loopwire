#pragma once

#include <QString>
#include <vector>

namespace Loopwire {

// ---- CIRCULAR MIL AREA ------------------------------------------------------------------------------------------------
//
// A conductor's cross-section in circular mils: its diameter in thousandths of an inch, squared - the unit American
// wire is sized in (AWG, kcmil), and the one aerospace and power harness rules are written in. One mm2 is 1973.525 CM.

// What one size is given in.
enum class CmaUnit { Awg, Mm2, Kcmil, Cmil, DiameterMm };
QString cmaUnitName(CmaUnit unit);

// THE CIRCULAR MILS OF ONE CONDUCTOR of `size` in `unit`. AWG by its diameter (0.005 in x 92^((36 - n) / 39)), 0000 as
// -3; a diameter as the solid round conductor that size. Below zero for a size that cannot be (a negative area).
double cmaOf(double size, CmaUnit unit);
double cmaFromMm2(double mm2);
double mm2FromCma(double cm);

// THE SMALLEST STANDARD SIZE THAT HOLDS AT LEAST `cm`: the AWG (0000 as -3, up to 40), and the IEC 60228 metric size
// (0.05 to 240 mm2). An AWG below -3 or past 40 says it is off the scale: 999 bigger than 0000, -999 never.
int awgForCma(double cm);
double mm2ForCma(double cm);
QString awgName(int awg);   // "4/0" for -3, "2/0" for -1, "12" for 12

// THE CIRCULAR MILS A RUN NEEDS for `amps` over `lengthM` (one way - out and back is counted) with no more than
// `dropVolts` lost: CM = 2 x K x I x L(ft) / V, K the material's resistivity in ohm-CM per foot (copper 10.75 at 20 C,
// as the rule is usually worked; aluminium 17.0). Below zero when the figures cannot give one.
double cmaRequired(double amps, double lengthM, double dropVolts, double kOhmCmilPerFt);
constexpr double kCmaCopperK = 10.75;
constexpr double kCmaAluminiumK = 17.0;

// A CONDUCTOR IN THE LIST, and the lot added up.
struct CmaConductor {
    QString what;            // a part's P/N and description, or "manual"
    double size = 0.0;
    CmaUnit unit = CmaUnit::Awg;
    int count = 1;
    double each() const { return cmaOf(size, unit); }
    double total() const { const double e = each(); return e > 0.0 ? e * count : 0.0; }
};
double cmaTotal(const std::vector<CmaConductor>& conductors);

} // namespace Loopwire
