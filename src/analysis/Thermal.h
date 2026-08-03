#pragma once

#include "analysis/VoltageDrop.h"

#include <QString>
#include <QStringList>
#include <vector>

namespace Edraw_Desktop {

// ---- The lumped thermal model -------------------------------------------------------------------
// How hot a bundle gets, and what that does back to the copper.
//
// The loop is the whole point. Current through resistance makes heat; heat raises the conductor's
// temperature; a warmer conductor has MORE resistance, so it makes more heat. Solving that in one
// step underestimates the temperature, so it is iterated until the answer stops moving:
//
//   P     = Σ I² · R(T)                       the heat the conductors make
//   R(T)  = R20 · [1 + α20 (T − 20)] · L · k_strand
//   ΔT    = P · R_th_total                    the rise that heat produces
//   T_new = T_ambient + ΔT                    and round again
//
// R_th_total is three resistances in series, core to air:
//   R_th_bundle    - through the bundle to its own surface
//   R_th_enclosure - across the trunking or conduit it sits in (zero in free air)
//   R_th_ambient   - from that surface into the air by convection and radiation
//
// Nothing here is guessed. A figure the drawing does not state becomes a gap, exactly as in the
// voltage-drop engine, because a temperature computed on an invented thermal resistance is a number
// nobody can sign.

// One conductor in the bundle, with what it carries.
struct ThWire {
    QString id;
    QString label;
    double mm2 = 0.0;                 // cross-section; 0 is a gap, not a zero
    double outerDiameterMm = 0.0;     // over the insulation, for the bundle's own size
    double lengthM = 1.0;             // the length being considered (1 m makes it a per-metre answer)
    double currentA = 0.0;
    ConductorMaterial material = ConductorMaterial::TinnedCopper;
    bool stranded = true;
    // What the insulation will take. 0 means the drawing does not say.
    double insulationRatingC = 0.0;
};

// How the bundle sits: what it is surrounded by, and how the heat gets out.
enum class ThMounting { FreeAir, Bundled, InConduit, InEnclosure };

QString thMountingName(ThMounting m);

struct ThRequest {
    std::vector<ThWire> wires;
    double ambientC = 25.0;
    // The bundle's own outside diameter. 0 means work it out from the conductors.
    double bundleDiameterMm = 0.0;
    ThMounting mounting = ThMounting::Bundled;
    // The surface heat-transfer coefficient, W/m²K: natural convection plus radiation off a small
    // cylinder in still air is about 8 to 12. Overridden when the installation is known.
    double surfaceCoefficient = 10.0;
    // Through the bundle itself, W/mK. A bundle is copper, polymer and air together; 0.25 is the
    // figure usually taken for the mixture.
    double bundleConductivity = 0.25;
    // Across whatever it sits in, K/W. 0 in free air.
    double enclosureResistanceKW = 0.0;
    // What the answer is measured against: the insulation rating when the wires state one, else this.
    double targetMaxC = 105.0;
    int maxIterations = 60;
    double convergenceC = 0.1;        // stop when the temperature moves less than this
};

// One turn of the loop, so the convergence can be shown rather than promised.
struct ThStep {
    int iteration = 0;
    double conductorTempC = 0.0;
    double totalPowerW = 0.0;
    double movedC = 0.0;
};

struct ThWireResult {
    QString id;
    QString label;
    double mm2 = 0.0;
    double currentA = 0.0;
    double resistanceOhms = 0.0;      // at the converged temperature
    double powerW = 0.0;
    double shareOfHeat = 0.0;         // 0..1
    double currentDensity = 0.0;      // A/mm², the figure that says whether this is sane at a glance
};

struct ThReport {
    bool solved = false;
    QString problem;
    bool converged = false;
    int iterations = 0;
    std::vector<ThStep> steps;
    std::vector<ThWireResult> wires;

    double ambientC = 0.0;
    double conductorTempC = 0.0;
    double riseC = 0.0;
    double totalPowerW = 0.0;
    double bundleDiameterMm = 0.0;
    // The three in series, and their total.
    double rthBundleKW = 0.0;
    double rthEnclosureKW = 0.0;
    double rthAmbientKW = 0.0;
    double rthTotalKW() const { return rthBundleKW + rthEnclosureKW + rthAmbientKW; }

    double limitC = 0.0;              // what it is measured against, and where that came from
    QString limitFrom;
    bool exceedsLimit() const { return solved && conductorTempC > limitC; }
    double marginC() const { return limitC - conductorTempC; }

    std::vector<VdGap> gaps;          // the same idea as the voltage-drop engine's
    QStringList assumptions;
    QStringList workings;             // the formulas with this run's numbers in them
    // A gap only BLOCKS when it stopped the calculation. Once it has solved, what is left is
    // advisory - an unstated insulation rating does not make the temperature wrong, it just leaves
    // nothing firm to measure it against.
    bool blocked() const { return !solved && !gaps.empty(); }
    QString summary() const;
    // The wire doing most of the heating - which is what to change.
    QString hottestContributor() const;
};

ThReport analyzeThermal(const ThRequest& request);

// The most every conductor could carry and still land on `targetC`, found by bisection on the model
// above (so it accounts for the resistance rising with temperature). Returns 0 when even no current
// exceeds the target - an ambient already over it.
double thermalMaxCurrentA(const ThRequest& request, double targetC);

// The smallest standard cross-section that keeps the bundle under the target at the currents asked
// for. 0 when nothing in the standard range is enough.
double thermalSuggestedMm2(const ThRequest& request, double targetC);

// The standard cross-sections, thinnest first - what a suggestion is chosen from.
const std::vector<double>& thermalStandardMm2();

} // namespace Edraw_Desktop
