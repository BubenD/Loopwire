// The bundle-diameter working and the lumped thermal model. Every figure here is one that can be
// checked by hand from the formulas the tools display.
#include "analysis/Thermal.h"
#include "app/BundleDiameterDialog.h"
#include "app/Editor.h"
#include "harness/MechanicalHarness.h"
#include "model/Document.h"

#include <QComboBox>
#include <QPushButton>
#include <QStyleOptionViewItem>
#include <QTableWidget>

#include <cmath>
#include <cstdio>

using namespace Loopwire;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("  [FAIL] %s\n", what);
        ++failures;
    }
}

bool near(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }

ThWire wireOf(const char* id, double mm2, double amps, double lengthM = 1.0) {
    ThWire w;
    w.id = QString::fromLatin1(id);
    w.label = w.id;
    w.mm2 = mm2;
    w.currentA = amps;
    w.lengthM = lengthM;
    w.material = ConductorMaterial::BareCopper;
    w.stranded = false;
    return w;
}

} // namespace

int run_thermal_tests() {
    std::printf("bundle diameter and thermal tests:\n");

    // ---- k_packing: loose when there are few, tighter as the bundle grows ----
    {
        check(near(autoPackingFactor(2), 1.25), "k_packing: a handful packs loosely");
        check(near(autoPackingFactor(6), 1.22), "k_packing: 1.22 up to six");
        check(near(autoPackingFactor(12), 1.20), "k_packing: 1.20 up to twelve");
        check(near(autoPackingFactor(30), 1.18), "k_packing: 1.18 up to thirty");
        check(near(autoPackingFactor(60), 1.15), "k_packing: 1.15 for a big bundle");
        // Always inside the range every standard quotes.
        for (int n = 1; n <= 200; ++n)
            check(autoPackingFactor(n) >= 1.15 && autoPackingFactor(n) <= 1.25,
                  "k_packing: never outside 1.15 to 1.25");
    }

    // ---- The working behind a bundle diameter ----
    {
        // Six 1.5 mm conductors: ΣD² = 6 × 2.25 = 13.5, √13.5 = 3.6742, × 1.22 = 4.4825.
        const BundleCalc c = bundleCalculation({1.5, 1.5, 1.5, 1.5, 1.5, 1.5}, -1.0, 1.0);
        std::printf("    (six 1.5 mm: ΣD² %.3f, root %.4f, k %.2f -> %.3f mm)\n", c.sumOfSquares,
                    c.root, c.packingFactor, c.result);
        check(c.counted == 6 && c.unknown == 0, "bundle calc: it counts what it was given");
        check(near(c.sumOfSquares, 13.5, 1e-9), "bundle calc: the sum of the squares");
        check(near(c.root, std::sqrt(13.5), 1e-9), "bundle calc: and its root");
        check(near(c.packingFactor, 1.22, 1e-9), "bundle calc: k came from the count");
        check(c.packingAuto, "bundle calc: and says it was automatic");
        check(near(c.result, std::sqrt(13.5) * 1.22, 1e-9),
              "bundle calc: D = root(sum of squares) x k");
        check(c.rule == QLatin1String("random-lay"), "bundle calc: by the random-lay rule");
        check(c.workings.size() >= 5, "bundle calc: the formula is shown line by line");
        check(c.workings.join(QStringLiteral(" ")).contains(QLatin1String("k_packing")),
              "bundle calc: and names k_packing in it");
        check(!c.explanation().isEmpty(), "bundle calc: with an explanation beside it");

        // ---- THE SUM IS WRITTEN COUNTED, not one conductor at a time. ----
        {
            // Three of one size, two of another and one on its own. Written out in full that is six
            // terms saying what the wire table already says; counted it is a line somebody can check.
            const BundleCalc mixed =
                bundleCalculation({2.5, 1.8, 2.5, 1.8, 0.9, 2.5}, -1.0, 1.0);
            QString sum;
            for (const QString& line : mixed.workings)
                if (line.startsWith(QStringLiteral("Σ D²_wire ="))) sum = line;
            std::printf("    (mixed bundle: %s)\n", qPrintable(sum));
            // QStringLiteral and not QLatin1String for anything with a × or a ² in it: the source is
            // UTF-8, and read as Latin-1 each of those becomes two characters that match nothing.
            check(sum.contains(QStringLiteral("3 × 2.500²")),
                  "bundle working: three of a size are one counted term");
            check(sum.contains(QStringLiteral("2 × 1.800²")),
                  "bundle working: and so are two of the next");
            // A size that turns up once carries no count - "1 ×" in front of it counts nothing.
            check(sum.contains(QStringLiteral("0.900²")) && !sum.contains(QStringLiteral("1 × ")),
                  "bundle working: a lone conductor is written as itself");
            // LARGEST FIRST, the same order the conductors are counted in.
            check(sum.indexOf(QLatin1String("2.500")) < sum.indexOf(QLatin1String("1.800")) &&
                      sum.indexOf(QLatin1String("1.800")) < sum.indexOf(QLatin1String("0.900")),
                  "bundle working: the terms run largest first");
            // AND IT STILL ADDS UP TO WHAT WAS SQUARED: grouping is how it is written, not what it
            // is worth.
            check(near(mixed.sumOfSquares, 3 * 2.5 * 2.5 + 2 * 1.8 * 1.8 + 0.9 * 0.9, 1e-9),
                  "bundle working: the counted terms are the same sum as before");
        }

        // A k given by hand is used instead, and says so.
        const BundleCalc byHand = bundleCalculation({1.5, 1.5, 1.5, 1.5}, 1.25, 1.0);
        check(near(byHand.packingFactor, 1.25) && !byHand.packingAuto,
              "bundle calc: a k entered by hand is used as given");
        check(near(byHand.result, std::sqrt(4 * 2.25) * 1.25, 1e-9),
              "bundle calc: and applied to the root");

        // The small counts use their own geometry, and the explanation says why.
        const BundleCalc one = bundleCalculation({2.1}, -1.0, 1.0);
        check(near(one.result, 2.1) && one.rule == QLatin1String("single"),
              "bundle calc: one conductor is its own bundle");
        const BundleCalc two = bundleCalculation({1.5, 1.5}, -1.0, 1.0);
        check(near(two.result, 3.0) && two.rule == QLatin1String("pair"),
              "bundle calc: two lie side by side");
        check(two.explanation().contains(QLatin1String("12")),
              "bundle calc: and it says how far the formula would be out there");
        const BundleCalc three = bundleCalculation({1.5, 1.5, 1.5}, -1.0, 1.0);
        check(near(three.result, 3.0 * 1.077, 1e-9) && three.rule == QLatin1String("triple"),
              "bundle calc: three settle into a triangle");

        // A conductor of unknown size is left out and reported, not counted as nothing.
        const BundleCalc gap = bundleCalculation({1.5, 1.5, 1.5, 1.5, 0.0}, -1.0, 1.0);
        check(gap.counted == 4 && gap.unknown == 1, "bundle calc: an unknown conductor is set aside");
        check(gap.explanation().contains(QLatin1String("LEFT OUT")),
              "bundle calc: and the answer says it is short by that much");
        // Nothing at all falls back to the floor and says so.
        const BundleCalc none = bundleCalculation({}, -1.0, 2.0);
        check(near(none.result, 2.0) && none.clampedToMinimum,
              "bundle calc: nothing countable gives the minimum, flagged");

        // It agrees with the figure the drawing is generated from.
        check(near(bundleCalculation({1.5, 1.5, 1.5, 1.5}, 1.2, 1.0).result,
                   bundleDiameterMm({1.5, 1.5, 1.5, 1.5}, 1.2, 1.0), 1e-9),
              "bundle calc: the working and the drawn bundle are the same figure");
    }

    // ---- The gauge cell offers the sizes that exist, and still takes a typed one ------------------
    {
        // A cross-section is not free text: there are a dozen or so a harness is ever built from, and
        // typing one out is a chance to write 0.35 where the table says 0.34 and get the diameter of
        // a wire that is not the wire. The cell drops down the sizes; it stays editable so a
        // conductor that is not a standard size can still go in the bundle.
        Document doc;
        Editor editor;
        editor.setDocument(&doc);
        BundleDiameterDialog dlg(&editor);
        QTableWidget* t = dlg.findChild<QTableWidget*>();
        check(t != nullptr, "gauge list: the window has its table");
        if (!t) return failures;
        // A conductor to work on.
        for (QPushButton* b : dlg.findChildren<QPushButton*>())
            if (b->text().startsWith(QStringLiteral("Conductor"))) { b->click(); break; }
        check(t->rowCount() == 1, "gauge list: one conductor to work on");
        if (t->rowCount() != 1) return failures;

        // The gauge column is column 3, the unit beside it column 4 - see the Col enum. Opening the
        // editor is what the table does on a double click; here it is asked for directly.
        const int kGauge = 3, kUnit = 4;
        QAbstractItemDelegate* deleg = t->itemDelegateForColumn(kGauge);
        check(deleg != nullptr, "gauge list: the gauge column has an editor of its own");
        if (!deleg) return failures;
        auto openEditor = [&](int row) {
            QStyleOptionViewItem opt;
            return qobject_cast<QComboBox*>(
                deleg->createEditor(t->viewport(), opt, t->model()->index(row, kGauge)));
        };

        // ---- AWG: 8 down to 32, which is the range the conversion table covers ----
        auto* unit = qobject_cast<QComboBox*>(t->cellWidget(0, kUnit));
        check(unit && unit->currentIndex() == 0, "gauge list: a new conductor is in AWG");
        QComboBox* box = openEditor(0);
        check(box != nullptr, "gauge list: and the cell drops down");
        if (!box) return failures;
        QStringList sizes;
        for (int i = 0; i < box->count(); ++i) sizes << box->itemText(i);
        std::printf("    (gauge list, AWG: %s)\n", qPrintable(sizes.join(QStringLiteral(" "))));
        check(box->count() == 13, "gauge list: thirteen sizes in AWG");
        check(sizes.first() == QLatin1String("8") && sizes.last() == QLatin1String("32"),
              "gauge list: 8 at the top and 32 at the bottom, as asked for");
        check(sizes.contains(QLatin1String("22")) && sizes.contains(QLatin1String("16")),
              "gauge list: with the sizes in between");
        // IT IS EDITABLE. A size that is not on the list is still a size.
        check(box->isEditable(), "gauge list: and a size can be typed instead of picked");
        box->setEditText(QStringLiteral("23"));
        deleg->setModelData(box, t->model(), t->model()->index(0, kGauge));
        std::printf("    (typed 23 AWG -> cell \"%s\", od %.2f mm)\n",
                    qPrintable(t->item(0, kGauge)->text()), dlg.rowDiameterMm(0));
        check(t->item(0, kGauge)->text() == QLatin1String("23"),
              "gauge list: a typed size goes in exactly as typed");
        delete box;

        // ---- mm2: the same sizes, said the other way ----
        t->item(0, kGauge)->setText(QStringLiteral("22"));
        unit->setCurrentIndex(1);
        box = openEditor(0);
        check(box != nullptr, "gauge list: the mm2 row drops down too");
        if (!box) return failures;
        QStringList metric;
        for (int i = 0; i < box->count(); ++i) metric << box->itemText(i);
        std::printf("    (gauge list, mm2: %s)\n", qPrintable(metric.join(QStringLiteral(" "))));
        check(box->count() == 13, "gauge list: thirteen sizes in mm2 as well");
        // 8 AWG is 8.37 mm2 and 32 AWG is 0.032 - the two ends of the same table, so the list a row
        // offers always matches the unit that row is written in.
        check(metric.first() == QLatin1String("8.37"),
              "gauge list: 8 AWG at the top reads as 8.37 mm2");
        check(metric.last() == QLatin1String("0.032"),
              "gauge list: and 32 AWG at the bottom as 0.032");
        // THE LIST AND THE UNIT SWITCH AGREE. Changing the unit converted 22 AWG to a number, and
        // that number has to be one the list offers - otherwise the same wire is spelled two ways
        // depending on how it got there.
        const QString converted = t->item(0, kGauge)->text();
        std::printf("    (22 AWG converted to \"%s\"; on the list: %s)\n", qPrintable(converted),
                    metric.contains(converted) ? "yes" : "no");
        check(metric.contains(converted),
              "gauge list: a size arrived at by switching the unit is one the list offers");
        delete box;

        // ---- AND 32 AWG IS A REAL SIZE NOW ----
        // It was missing from the conversion table, so it could not be offered and a conductor
        // written as 32 AWG came out with no diameter at all and was left out of the sum.
        check(mechAwgMm2(32) > 0.0, "gauge list: 32 AWG has a cross-section to convert to");
        check(wireOuterDiameterMm(QStringLiteral("32 AWG")) > 0.0,
              "gauge list: and therefore a diameter the bundle can count");
    }

    // ---- The thermal loop ----
    {
        // One 1 mm² bare copper conductor, 1 m, 10 A. R20 = 0.017241 ohm, so 1.72 W cold - and more
        // once it warms up, which is the whole point of iterating.
        ThRequest req;
        req.wires = {wireOf("W1", 1.0, 10.0)};
        req.ambientC = 25.0;
        req.targetMaxC = 105.0;
        const ThReport rep = analyzeThermal(req);
        std::printf("    (one 1 mm2 at 10 A: %s)\n", qPrintable(rep.summary()));
        check(rep.solved, "thermal: it solves");
        check(rep.converged, "thermal: and settles");
        check(rep.iterations > 1, "thermal: taking more than one round to do it");
        check(rep.conductorTempC > rep.ambientC, "thermal: current makes it hotter than the room");

        // The loop must land where the formulas say: T = ambient + P·R_th, with P at that same T.
        const double rAtT = 0.017241 * (1.0 + 0.00393 * (rep.conductorTempC - 20.0));
        const double pAtT = 10.0 * 10.0 * rAtT;
        check(near(rep.totalPowerW, pAtT, 1e-4),
              "thermal: the heat is I²R at the temperature it settled at");
        check(near(rep.conductorTempC, rep.ambientC + rep.totalPowerW * rep.rthTotalKW(), 1e-3),
              "thermal: and the temperature is ambient + P x R_th");
        check(near(rep.riseC, rep.conductorTempC - rep.ambientC, 1e-9),
              "thermal: the rise is the difference");

        // Iterating matters: the cold-resistance answer is lower than the settled one.
        const double coldRise = 10.0 * 10.0 * 0.017241 *
                               (1.0 + 0.00393 * (25.0 - 20.0)) * rep.rthTotalKW();
        std::printf("    (cold estimate %.1f K against settled %.1f K)\n", coldRise, rep.riseC);
        check(rep.riseC > coldRise,
              "thermal: a warmer conductor makes more heat, so iterating raises the answer");

        // The three resistances are in series and all present.
        check(rep.rthBundleKW > 0.0 && rep.rthAmbientKW > 0.0,
              "thermal: the bundle and the air are both resistances");
        check(near(rep.rthTotalKW(), rep.rthBundleKW + rep.rthEnclosureKW + rep.rthAmbientKW, 1e-12),
              "thermal: and they add in series");
        check(rep.workings.size() >= 8, "thermal: the formulas are shown with the numbers in them");
        check(rep.workings.join(QStringLiteral(" ")).contains(QLatin1String("R_th,total")),
              "thermal: including the total");

        // More current is hotter; a bigger conductor is cooler.
        ThRequest more = req;
        more.wires[0].currentA = 20.0;
        const ThReport hot = analyzeThermal(more);
        check(hot.conductorTempC > rep.conductorTempC, "thermal: twice the current is hotter");
        ThRequest fatter = req;
        fatter.wires[0].mm2 = 2.5;
        check(analyzeThermal(fatter).conductorTempC < rep.conductorTempC,
              "thermal: a bigger conductor is cooler at the same current");

        // Enclosed is hotter than free air, because the heat has further to go.
        ThRequest free = req, boxed = req;
        free.mounting = ThMounting::FreeAir;
        boxed.mounting = ThMounting::InEnclosure;
        const ThReport freeRep = analyzeThermal(free);
        const ThReport boxedRep = analyzeThermal(boxed);
        std::printf("    (free air %.1f °C, in an enclosure %.1f °C)\n", freeRep.conductorTempC,
                    boxedRep.conductorTempC);
        check(boxedRep.conductorTempC > freeRep.conductorTempC,
              "thermal: shut in a box it runs hotter than in free air");
        check(boxedRep.rthEnclosureKW > 0.0 && near(freeRep.rthEnclosureKW, 0.0),
              "thermal: an enclosure is a resistance of its own; free air is not");
    }

    // ---- Where the heat comes from, over several conductors ----
    {
        ThRequest req;
        req.wires = {wireOf("power", 1.0, 15.0), wireOf("signal", 0.5, 0.1),
                     wireOf("signal2", 0.5, 0.1)};
        req.ambientC = 40.0;
        const ThReport rep = analyzeThermal(req);
        check(rep.solved && rep.wires.size() == 3, "thermal: every conductor is reported");
        double share = 0.0;
        for (const ThWireResult& w : rep.wires) share += w.shareOfHeat;
        check(near(share, 1.0, 1e-9), "thermal: the shares of the heat add up to all of it");
        std::printf("    (%s)\n", qPrintable(rep.hottestContributor()));
        check(rep.hottestContributor().contains(QLatin1String("power")),
              "thermal: and it names the one to change");
        // Current density is the figure that shows the problem at a glance.
        for (const ThWireResult& w : rep.wires)
            if (w.id == QLatin1String("power"))
                check(near(w.currentDensity, 15.0, 1e-9),
                      "thermal: 15 A through 1 mm² is 15 A/mm²");
    }

    // ---- The limit, and what is missing ----
    {
        ThRequest req;
        req.wires = {wireOf("W1", 0.5, 12.0)};
        req.ambientC = 85.0;   // hot to begin with
        req.targetMaxC = 105.0;
        const ThReport rep = analyzeThermal(req);
        std::printf("    (0.5 mm2 at 12 A in 85 °C: %.1f °C, limit %.0f)\n", rep.conductorTempC,
                    rep.limitC);
        check(rep.exceedsLimit(), "limit: 12 A through 0.5 mm² in a hot place is over the limit");
        check(rep.marginC() < 0.0, "limit: and the margin says so");
        // No wire states a rating, so the target is used - and that is reported as a gap.
        bool sawRatingGap = false;
        for (const VdGap& g : rep.gaps)
            if (g.property == QLatin1String("Insulation rating")) sawRatingGap = true;
        check(sawRatingGap, "limit: an unstated insulation rating is a gap, not an assumption");

        // A stated rating is used instead of the target, and the lowest in the bundle wins.
        ThRequest rated = req;
        rated.wires[0].insulationRatingC = 150.0;
        rated.wires.push_back(wireOf("W2", 0.5, 0.0));
        rated.wires[1].insulationRatingC = 105.0;
        const ThReport ratedRep = analyzeThermal(rated);
        check(near(ratedRep.limitC, 105.0), "limit: the lowest rating in the bundle is the limit");
        check(ratedRep.limitFrom.contains(QLatin1String("lowest")),
              "limit: and the report says where that came from");

        // A conductor with no cross-section blocks it rather than being guessed at.
        ThRequest blocked;
        blocked.wires = {wireOf("W1", 0.0, 5.0)};
        const ThReport blockedRep = analyzeThermal(blocked);
        check(blockedRep.blocked() && !blockedRep.solved,
              "limit: no cross-section means no answer, not a guessed one");
        check(!blockedRep.gaps.empty() &&
                  blockedRep.gaps.front().property == QLatin1String("Cross-section"),
              "limit: and it says which figure is missing");
    }

    // ---- What it could carry, and what size it should be ----
    {
        ThRequest req;
        req.wires = {wireOf("W1", 1.0, 10.0)};
        req.ambientC = 25.0;
        const double maxA = thermalMaxCurrentA(req, 105.0);
        std::printf("    (1 mm2, 25 °C ambient, 105 °C limit: up to %.1f A)\n", maxA);
        check(maxA > 0.0, "max current: there is an answer");
        // Check it by running the model at that current: it should land on the limit.
        ThRequest at = req;
        at.wires[0].currentA = maxA;
        const ThReport atRep = analyzeThermal(at);
        check(std::abs(atRep.conductorTempC - 105.0) < 1.0,
              "max current: at that current it lands on the limit");
        // And a shade more is over it.
        ThRequest over = req;
        over.wires[0].currentA = maxA * 1.1;
        check(analyzeThermal(over).conductorTempC > 105.0,
              "max current: ten per cent more is over the limit");
        // A hotter room leaves less room for current.
        ThRequest hotRoom = req;
        hotRoom.ambientC = 85.0;
        check(thermalMaxCurrentA(hotRoom, 105.0) < maxA,
              "max current: a hot ambient leaves less current in hand");
        // An ambient already over the limit has no answer at all.
        ThRequest impossible = req;
        impossible.ambientC = 120.0;
        check(near(thermalMaxCurrentA(impossible, 105.0), 0.0),
              "max current: an ambient over the limit leaves none");

        // The suggestion is a standard size, and it is enough.
        ThRequest small;
        small.wires = {wireOf("W1", 0.35, 20.0)};
        small.ambientC = 25.0;
        const double mm2 = thermalSuggestedMm2(small, 105.0);
        std::printf("    (20 A wants at least %.2f mm2)\n", mm2);
        check(mm2 > 0.35, "suggestion: 20 A needs more than 0.35 mm²");
        bool standard = false;
        for (double s : thermalStandardMm2())
            if (near(s, mm2)) standard = true;
        check(standard, "suggestion: and it is a size that can be bought");
        ThRequest fixed = small;
        for (ThWire& w : fixed.wires) w.mm2 = mm2;
        fixed.bundleDiameterMm = 0.0;
        check(analyzeThermal(fixed).conductorTempC <= 105.0,
              "suggestion: at that size it is under the limit");
    }

    if (failures == 0) std::printf("  -> ok\n");
    else std::printf("  -> %d failed\n", failures);
    return failures;
}
