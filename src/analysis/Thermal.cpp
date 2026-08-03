#include "analysis/Thermal.h"

#include "harness/MechanicalHarness.h"

#include <algorithm>
#include <cmath>

namespace Edraw_Desktop {
namespace {

constexpr double kPi = 3.14159265358979323846;

// A conductor's resistance at temperature, over its own length. The same relation the voltage-drop
// engine uses, so the two agree about the copper.
double wireOhmsAt(const ThWire& w, double tempC) {
    VdSegment seg;
    seg.mm2 = w.mm2;
    seg.lengthM = w.lengthM;
    seg.material = w.material;
    seg.stranded = w.stranded;
    return wireResistanceOhms(seg, tempC);
}

// What the mounting does to the heat getting out, and to the air right around the bundle. A bundle
// in trunking sits in air that is itself warmer than the room.
struct MountingEffect {
    double surfaceFactor;   // multiplies the surface coefficient
    double enclosureKW;     // added in series when the request does not give its own figure
    const char* note;
};

MountingEffect mountingEffect(ThMounting m) {
    switch (m) {
        case ThMounting::FreeAir:
            return {1.15, 0.0, "In free air: the whole surface is losing heat."};
        case ThMounting::Bundled:
            return {1.0, 0.0,
                    "Bundled: the conductors shield one another, so only the outside of the bundle "
                    "is losing heat."};
        case ThMounting::InConduit:
            return {0.75, 0.6,
                    "In conduit: the wall is another resistance in series, and the air inside it is "
                    "not moving."};
        case ThMounting::InEnclosure:
            return {0.6, 1.2,
                    "In an enclosure: little air movement, and the air inside sits above the room's "
                    "own temperature."};
    }
    return {1.0, 0.0, ""};
}

} // namespace

QString thMountingName(ThMounting m) {
    switch (m) {
        case ThMounting::FreeAir:     return QStringLiteral("Free air");
        case ThMounting::Bundled:     return QStringLiteral("Bundled");
        case ThMounting::InConduit:   return QStringLiteral("In conduit");
        case ThMounting::InEnclosure: return QStringLiteral("In an enclosure");
    }
    return QStringLiteral("Bundled");
}

const std::vector<double>& thermalStandardMm2() {
    static const std::vector<double> sizes = {0.13, 0.22, 0.35, 0.5,  0.75, 1.0,  1.5, 2.0,
                                              2.5,  4.0,  6.0,  10.0, 16.0, 25.0, 35.0, 50.0};
    return sizes;
}

QString ThReport::summary() const {
    if (blocked())
        return QStringLiteral("%1 value(s) missing before this can be worked out").arg(gaps.size());
    if (!solved) return problem;
    return QStringLiteral("%1 °C at the conductors — %2 K over %3 °C ambient, from %4 W. "
                          "The limit is %5 °C, so there is %6 K %7.")
        .arg(conductorTempC, 0, 'f', 1)
        .arg(riseC, 0, 'f', 1)
        .arg(ambientC, 0, 'f', 1)
        .arg(totalPowerW, 0, 'f', 2)
        .arg(limitC, 0, 'f', 0)
        .arg(std::abs(marginC()), 0, 'f', 1)
        .arg(marginC() >= 0.0 ? QStringLiteral("in hand") : QStringLiteral("OVER"));
}

QString ThReport::hottestContributor() const {
    const ThWireResult* worst = nullptr;
    for (const ThWireResult& w : wires)
        if (!worst || w.powerW > worst->powerW) worst = &w;
    if (!worst) return QString();
    return QStringLiteral("%1 (%2 mm², %3 A) makes %4 % of the heat")
        .arg(worst->label.isEmpty() ? worst->id : worst->label)
        .arg(worst->mm2, 0, 'f', 2)
        .arg(worst->currentA, 0, 'f', 2)
        .arg(worst->shareOfHeat * 100.0, 0, 'f', 0);
}

ThReport analyzeThermal(const ThRequest& req) {
    ThReport out;
    out.ambientC = req.ambientC;

    // ---- What has to be known before anything can be worked out ----
    if (req.wires.empty()) {
        out.problem = QStringLiteral("No conductors: add the wires that share this bundle.");
        return out;
    }
    for (const ThWire& w : req.wires) {
        const QString name = w.label.isEmpty() ? w.id : w.label;
        if (w.mm2 <= 0.0)
            out.gaps.push_back({w.id, name, QStringLiteral("Cross-section"), QStringLiteral("mm2"),
                                QStringLiteral("Thermal"),
                                QStringLiteral("the conductor's area, or its AWG size")});
        if (w.lengthM <= 0.0)
            out.gaps.push_back({w.id, name, QStringLiteral("Length"), QStringLiteral("m"),
                                QStringLiteral("Thermal"),
                                QStringLiteral("the length being considered; 1 m gives a per-meter "
                                               "answer")});
    }
    if (!out.gaps.empty()) return out;

    // ---- The bundle's own size, which is what the heat has to get out through ----
    out.bundleDiameterMm = req.bundleDiameterMm;
    if (out.bundleDiameterMm <= 0.0) {
        std::vector<double> ods;
        for (const ThWire& w : req.wires)
            ods.push_back(w.outerDiameterMm > 0.0
                              ? w.outerDiameterMm
                              : wireOuterDiameterMm(QString::number(w.mm2)));
        const BundleCalc calc = bundleCalculation(ods, /*auto*/ -1.0, 1.0);
        out.bundleDiameterMm = calc.result;
        out.assumptions << QStringLiteral(
            "Bundle diameter %1 mm, worked out from the conductors (k_packing %2).")
                               .arg(out.bundleDiameterMm, 0, 'f', 2)
                               .arg(calc.packingFactor, 0, 'f', 2);
    }

    // ---- The three thermal resistances, core to air ----
    // Surface area of the bundle: a cylinder of that diameter over the longest conductor in it.
    double lengthM = 0.0;
    for (const ThWire& w : req.wires) lengthM = std::max(lengthM, w.lengthM);
    const double dM = out.bundleDiameterMm / 1000.0;
    const double areaM2 = kPi * dM * lengthM;
    const MountingEffect eff = mountingEffect(req.mounting);
    const double h = std::max(1.0, req.surfaceCoefficient * eff.surfaceFactor);

    // Into the air: 1 / (h·A).
    out.rthAmbientKW = 1.0 / (h * areaM2);
    // Through the bundle to its own surface. A solid cylinder generating heat throughout itself has
    // an effective centre-to-surface resistance of 1/(4πkL); that is the figure used here.
    out.rthBundleKW = 1.0 / (4.0 * kPi * std::max(0.01, req.bundleConductivity) * lengthM);
    // Across whatever it sits in.
    out.rthEnclosureKW = req.enclosureResistanceKW > 0.0 ? req.enclosureResistanceKW
                                                         : eff.enclosureKW / std::max(0.05, lengthM);

    // ---- The limit this is measured against ----
    double rating = 0.0;
    for (const ThWire& w : req.wires)
        if (w.insulationRatingC > 0.0)
            rating = rating > 0.0 ? std::min(rating, w.insulationRatingC) : w.insulationRatingC;
    if (rating > 0.0) {
        out.limitC = rating;
        out.limitFrom = QStringLiteral("the lowest insulation rating in the bundle");
    } else {
        out.limitC = req.targetMaxC;
        out.limitFrom = QStringLiteral("the target set here (no wire states a rating)");
        out.gaps.push_back({QString(), QStringLiteral("the bundle"),
                            QStringLiteral("Insulation rating"), QStringLiteral("°C"),
                            QStringLiteral("Thermal"),
                            QStringLiteral("what the insulation will take, per wire")});
    }

    // ---- The loop ----
    const double rthTotal = out.rthTotalKW();
    double t = req.ambientC;
    for (int i = 1; i <= std::max(1, req.maxIterations); ++i) {
        double power = 0.0;
        for (const ThWire& w : req.wires) {
            const double r = wireOhmsAt(w, t);
            power += w.currentA * w.currentA * r;
        }
        const double next = req.ambientC + power * rthTotal;
        const double moved = std::abs(next - t);
        out.steps.push_back({i, next, power, moved});
        t = next;
        out.iterations = i;
        out.totalPowerW = power;
        if (moved < req.convergenceC) {
            out.converged = true;
            break;
        }
        // Runaway: past the point where more heat makes more resistance faster than it can escape,
        // there is no steady state to find, and saying so is the answer.
        if (!std::isfinite(t) || t > 2000.0) {
            out.problem = QStringLiteral(
                "No steady temperature: the heat rises faster than it escapes. Reduce the current, "
                "increase the cross-section, or get the heat out better.");
            return out;
        }
    }
    if (!out.converged)
        out.assumptions << QStringLiteral(
            "Stopped after %1 rounds without settling to within %2 K - treat the temperature as a "
            "lower bound.")
                               .arg(out.iterations)
                               .arg(req.convergenceC, 0, 'f', 2);

    // The step table above is the honest convergence history, stopped where `convergenceC` said to
    // stop. The figures REPORTED, though, have to satisfy the formulas printed beside them exactly -
    // P at T, and T = ambient + P·R_th, with the same T in both - so the fixed point is taken the
    // rest of the way here. It moves the answer by less than the tolerance already met.
    if (out.converged) {
        for (int i = 0; i < 200; ++i) {
            double power = 0.0;
            for (const ThWire& w : req.wires)
                power += w.currentA * w.currentA * wireOhmsAt(w, t);
            const double next = req.ambientC + power * rthTotal;
            if (std::abs(next - t) < 1e-12) { t = next; break; }
            t = next;
        }
    }
    out.conductorTempC = t;
    out.riseC = t - req.ambientC;
    out.solved = true;
    out.totalPowerW = 0.0;
    for (const ThWire& w : req.wires)
        out.totalPowerW += w.currentA * w.currentA * wireOhmsAt(w, t);

    // ---- Where the heat comes from ----
    for (const ThWire& w : req.wires) {
        ThWireResult r;
        r.id = w.id;
        r.label = w.label;
        r.mm2 = w.mm2;
        r.currentA = w.currentA;
        r.resistanceOhms = wireOhmsAt(w, t);
        r.powerW = w.currentA * w.currentA * r.resistanceOhms;
        r.shareOfHeat = out.totalPowerW > 0.0 ? r.powerW / out.totalPowerW : 0.0;
        r.currentDensity = w.mm2 > 0.0 ? w.currentA / w.mm2 : 0.0;
        out.wires.push_back(r);
    }

    // ---- The formulas, with this run's own numbers in them ----
    auto n = [](double v, int dp) { return QString::number(v, 'f', dp); };
    out.workings
        << QStringLiteral("R(T) = R₂₀ · [1 + α₂₀ (T − 20)] · L · k_strand")
        << QStringLiteral("P    = Σ I² · R(T)  =  %1 W  at %2 °C")
               .arg(n(out.totalPowerW, 3), n(out.conductorTempC, 1))
        << QStringLiteral("R_th,bundle    = 1 / (4π · k · L) = 1 / (4π × %1 × %2) = %3 K/W")
               .arg(n(req.bundleConductivity, 2), n(lengthM, 3), n(out.rthBundleKW, 3))
        << QStringLiteral("R_th,enclosure = %1 K/W  (%2)")
               .arg(n(out.rthEnclosureKW, 3), thMountingName(req.mounting))
        << QStringLiteral("R_th,ambient   = 1 / (h · A) = 1 / (%1 × %2) = %3 K/W")
               .arg(n(h, 1), n(areaM2, 5), n(out.rthAmbientKW, 3))
        << QStringLiteral("   A = π · D · L = π × %1 m × %2 m = %3 m²")
               .arg(n(dM, 4), n(lengthM, 3), n(areaM2, 5))
        << QStringLiteral("R_th,total     = %1 + %2 + %3 = %4 K/W")
               .arg(n(out.rthBundleKW, 3), n(out.rthEnclosureKW, 3), n(out.rthAmbientKW, 3),
                    n(rthTotal, 3))
        << QStringLiteral("ΔT   = P · R_th,total = %1 × %2 = %3 K")
               .arg(n(out.totalPowerW, 3), n(rthTotal, 3), n(out.riseC, 1))
        << QStringLiteral("T    = T_ambient + ΔT = %1 + %2 = %3 °C")
               .arg(n(req.ambientC, 1), n(out.riseC, 1), n(out.conductorTempC, 1))
        << QStringLiteral("settled in %1 round(s), the last moving %2 K")
               .arg(out.iterations)
               .arg(out.steps.empty() ? 0.0 : out.steps.back().movedC, 0, 'f', 3);

    out.assumptions << QString::fromLatin1(eff.note);
    out.assumptions << QStringLiteral(
        "Surface coefficient %1 W/m²K (convection and radiation together), bundle conductivity "
        "%2 W/mK.")
                           .arg(n(h, 1), n(req.bundleConductivity, 2));
    out.assumptions << QStringLiteral(
        "One temperature for the whole bundle: the middle of a big bundle runs hotter than this "
        "average, so treat a small margin as no margin.");
    return out;
}

double thermalMaxCurrentA(const ThRequest& request, double targetC) {
    // The model is monotonic in current, so bisect it. Everything keeps its share: doubling the
    // answer doubles every conductor's current, which is what "how much more could this carry" means.
    ThRequest probe = request;
    double baseTotal = 0.0;
    for (const ThWire& w : request.wires) baseTotal += w.currentA;
    if (baseTotal <= 0.0) {
        // Nothing is carrying anything: scale from 1 A each instead, so the question still has an
        // answer.
        for (ThWire& w : probe.wires) w.currentA = 1.0;
        baseTotal = static_cast<double>(probe.wires.size());
    }
    auto tempAt = [&](double scale) {
        ThRequest r = probe;
        for (size_t i = 0; i < r.wires.size(); ++i) r.wires[i].currentA = probe.wires[i].currentA * scale;
        const ThReport rep = analyzeThermal(r);
        return rep.solved ? rep.conductorTempC : 1e9;
    };
    if (tempAt(0.0) > targetC) return 0.0;   // the ambient alone is already over it

    double lo = 0.0, hi = 1.0;
    for (int i = 0; i < 40 && tempAt(hi) < targetC; ++i) hi *= 2.0;
    for (int i = 0; i < 60; ++i) {
        const double mid = (lo + hi) / 2.0;
        if (tempAt(mid) < targetC) lo = mid;
        else hi = mid;
        if (hi - lo < 1e-4) break;
    }
    // The biggest single conductor current at that scale, which is the figure a wire is chosen by.
    double biggest = 0.0;
    for (const ThWire& w : probe.wires) biggest = std::max(biggest, w.currentA * lo);
    return biggest;
}

double thermalSuggestedMm2(const ThRequest& request, double targetC) {
    for (double mm2 : thermalStandardMm2()) {
        ThRequest r = request;
        for (ThWire& w : r.wires) {
            if (w.mm2 >= mm2) continue;   // a conductor already bigger is left as it is
            w.mm2 = mm2;
            w.outerDiameterMm = 0.0;      // its diameter follows its new size
        }
        r.bundleDiameterMm = 0.0;         // and so does the bundle's
        const ThReport rep = analyzeThermal(r);
        if (rep.solved && rep.conductorTempC <= targetC) return mm2;
    }
    return 0.0;
}

} // namespace Edraw_Desktop
