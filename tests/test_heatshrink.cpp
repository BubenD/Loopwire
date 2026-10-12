// The heat-shrink shelf: the families a harness shop actually keeps, in every size they are made in.
//
// What is being checked is not "are there parts" but that each family is complete and self-consistent: the
// sizes it is made in, the shrink ratio worked through into what bundle it fits, the adhesive-lined one
// marked as such, the clear one marked clear, the identification sleeve filed as a marker rather than as
// tubing - and that adding the range twice does not put a second DR-25 3/8 on every bill of materials.
#include "harness/PartsLibrary.h"

#include "harness/CableSpec.h"

#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace Loopwire;

namespace {

int shrinkFailures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("  [FAIL] %s\n", what);
        ++shrinkFailures;
    }
}

std::vector<const LibraryPart*> family(const PartsLibrary& lib, const QString& prefix) {
    std::vector<const LibraryPart*> out;
    for (const LibraryPart& part : lib.parts)
        if (part.mfrPn.startsWith(prefix)) out.push_back(&part);
    return out;
}

} // namespace

int run_heatshrink_tests() {
    std::printf("heatshrink library tests:\n");

    PartsLibrary lib;
    const int added = addStandardHeatshrink(lib);
    std::printf("  %d parts added\n", added);
    check(added > 60, "shelf: every size of every family goes in, not a token few");

    // ---- the tubing families ---------------------------------------------------------------------
    struct Expect {
        const char* prefix;
        const char* ratio;
        bool adhesive;
        const char* colour;
        int leastSizes;
        // What the two ends of "what it fits" may come to. A 2:1 family is exactly that. ATUM is sold as
        // 4:1 but its designations are not all 4:1 - 6/2 is three, 52/16 is a shade over three - because
        // the pairs are the sizes it is actually made in, not arithmetic.
        double leastFitRatio;
        double mostFitRatio;
    };
    const Expect tubing[] = {{"DR-25", "2:1", false, "Black", 10, 1.9, 2.1},
                             {"RNF-100", "2:1", false, "Black", 10, 1.9, 2.1},
                             {"RT-375", "2:1", false, "Clear", 8, 1.9, 2.1},
                             {"ATUM", "4:1", true, "Black", 10, 2.9, 4.2}};
    for (const Expect& e : tubing) {
        const std::vector<const LibraryPart*> parts = family(lib, QString::fromLatin1(e.prefix));
        std::printf("  %s: %zu sizes\n", e.prefix, parts.size());
        check(static_cast<int>(parts.size()) >= e.leastSizes,
              "family: the whole size range is there");
        bool everyOne = !parts.empty();
        QSet<QString> pns;
        for (const LibraryPart* part : parts) {
            if (part->kind != PartKind::Heatshrink) everyOne = false;
            if (part->value(QStringLiteral("ratio")) != QString::fromLatin1(e.ratio)) everyOne = false;
            if (part->value(QStringLiteral("adhesive")) !=
                (e.adhesive ? QStringLiteral("1") : QStringLiteral("0")))
                everyOne = false;
            if (part->value(QStringLiteral("colour")) != QString::fromLatin1(e.colour)) everyOne = false;
            if (part->mfrName.isEmpty() || part->description.isEmpty()) everyOne = false;
            if (part->value(QStringLiteral("spec")).isEmpty()) everyOne = false;
            if (part->unit != QStringLiteral("m")) everyOne = false;
            pns.insert(part->mfrPn);
            // What it fits: from fully recovered up to as supplied, and the ratio between the two is the
            // family's ratio. A size whose two ends do not agree with its ratio is a typing mistake.
            const double fitMin = part->value(QStringLiteral("entryMin")).toDouble();
            const double fitMax = part->value(QStringLiteral("entryMax")).toDouble();
            if (fitMin <= 0.0 || fitMax <= fitMin) everyOne = false;
            const double fitRatio = fitMax / std::max(1e-9, fitMin);
            if (fitRatio < e.leastFitRatio || fitRatio > e.mostFitRatio) everyOne = false;
            // Nominal, and it says so: a build held to a tolerance is checked against the datasheet.
            if (!part->value(QStringLiteral("notes")).contains(QStringLiteral("NOMINAL"),
                                                              Qt::CaseInsensitive))
                everyOne = false;
            // Wall thickness is left blank rather than guessed at.
            if (!part->value(QStringLiteral("wall")).isEmpty()) everyOne = false;
        }
        check(everyOne,
              "family: every size of it is filed as heatshrink, at the family's ratio and colour, with "
              "what it fits worked through from the ratio, the wall left blank, and its figures marked "
              "nominal");
        check(pns.size() == parts.size(), "family: no size is in twice");
    }

    // The sizes are the ones the reels are labelled with.
    bool sizesRead = false;
    for (const LibraryPart* part : family(lib, QStringLiteral("DR-25")))
        if (part->mfrPn == QStringLiteral("DR-25-3/8-0")) sizesRead = true;
    check(sizesRead, "sizes: they are designated as the reel is - DR-25-3/8-0");
    bool atumRead = false;
    for (const LibraryPart* part : family(lib, QStringLiteral("ATUM")))
        if (part->mfrPn == QStringLiteral("ATUM-12/3-0")) {
            atumRead = true;
            check(part->value(QStringLiteral("entryMax")) == QStringLiteral("12.0") &&
                      part->value(QStringLiteral("entryMin")) == QStringLiteral("3.0"),
                  "sizes: an ATUM designation IS its size - 12 mm as supplied, 3 mm recovered");
        }
    check(atumRead, "sizes: ATUM is designated in millimetres, expanded over recovered");

    // ---- the identification sleeve, in both colours ----------------------------------------------
    {
        const std::vector<const LibraryPart*> sleeves = family(lib, QStringLiteral("TMS-SCE"));
        std::printf("  TMS-SCE: %zu parts\n", sleeves.size());
        check(sleeves.size() >= 20, "sleeve: every size, in both colours");
        int white = 0, yellow = 0;
        bool allMarkers = !sleeves.empty();
        for (const LibraryPart* part : sleeves) {
            if (part->kind != PartKind::Marker) allMarkers = false;
            if (part->value(QStringLiteral("type")) != QStringLiteral("Heatshrink sleeve"))
                allMarkers = false;
            if (part->value(QStringLiteral("width")).isEmpty()) allMarkers = false;
            if (part->description.contains(QStringLiteral("white"))) ++white;
            if (part->description.contains(QStringLiteral("yellow"))) ++yellow;
        }
        check(allMarkers,
              "sleeve: filed as a marker, not as tubing - it goes on to be printed, and that is what it "
              "is on a drawing and on a bill of materials");
        check(white > 0 && white == yellow, "sleeve: the same sizes in white and in yellow");
        bool bothCodes = false, one = false;
        for (const LibraryPart* part : sleeves) {
            if (part->mfrPn.endsWith(QStringLiteral("-9"))) bothCodes = true;
            if (part->mfrPn.endsWith(QStringLiteral("-4"))) one = true;
        }
        check(bothCodes && one, "sleeve: the colour is in the part number, as it is ordered");
    }

    // ---- adding it twice ---------------------------------------------------------------------------
    {
        const size_t was = lib.parts.size();
        const int again = addStandardHeatshrink(lib);
        check(again == 0 && lib.parts.size() == was,
              "twice: nothing is added a second time - a second DR-25 3/8 would be a second line on every "
              "bill of materials that used it");
        // But a library missing one gets that one back.
        lib.parts.erase(std::remove_if(lib.parts.begin(), lib.parts.end(),
                                       [](const LibraryPart& p) {
                                           return p.mfrPn == QStringLiteral("DR-25-1/2-0");
                                       }),
                        lib.parts.end());
        const int back = addStandardHeatshrink(lib);
        check(back == 1, "twice: and one that was taken out comes back, and only that one");
    }

    // ---- a library that already existed gets them, once ------------------------------------------
    {
        // This is the case that matters: a library made before the shelf existed. It has no flag, so the
        // shelf goes in the first time it is opened - and the flag it then carries means a size somebody
        // deliberately deleted does not come back the next time.
        PartsLibrary old;
        LibraryPart& mine = old.add(PartKind::Wire);
        mine.description = QStringLiteral("Something the shop put in itself");
        mine.mfrPn = QStringLiteral("HOUSE-1");
        check(!old.standardHeatshrink, "existing: a library written before the shelf has no flag");
        const int first = ensureStandardHeatshrink(old);
        check(first > 60, "existing: opening it puts the shelf in");
        check(old.standardHeatshrink, "existing: and records that it was done");
        check(!family(old, QStringLiteral("HOUSE-1")).empty(),
              "existing: what the shop put in itself is still there");
        const size_t after = old.parts.size();
        check(ensureStandardHeatshrink(old) == 0 && old.parts.size() == after,
              "existing: opening it again adds nothing");
        // A size taken out on purpose stays out.
        old.parts.erase(std::remove_if(old.parts.begin(), old.parts.end(),
                                       [](const LibraryPart& p) {
                                           return p.mfrPn == QStringLiteral("ATUM-12/3-0");
                                       }),
                        old.parts.end());
        check(ensureStandardHeatshrink(old) == 0,
              "existing: and a size deleted on purpose is not put back");

        // The flag travels in the file, or none of the above holds after a save and a load.
        PartsLibrary back;
        back.fromJson(old.toJson());
        check(back.standardHeatshrink,
              "existing: the flag is written to the file and read back - otherwise the shelf would be "
              "put back every time the library was opened");
        PartsLibrary older;
        QJsonObject noFlag = old.toJson();
        noFlag.remove(QStringLiteral("standardHeatshrink"));
        older.fromJson(noFlag);
        check(!older.standardHeatshrink,
              "existing: a file written before the flag existed reads as not done, which is what brings "
              "the shelf to it");
    }

    // ---- a new library has them from the start ----------------------------------------------------
    {
        const PartsLibrary seeded = seedLibrary();
        check(!family(seeded, QStringLiteral("DR-25")).empty() &&
                  !family(seeded, QStringLiteral("TMS-SCE")).empty(),
              "seed: a library that starts from nothing starts with the shelf on it");
    }

    // ---- the Spec 55 shelf: the wire, the pair, and the screened pair ----------------------------
    {
        // Every number checked here is read straight off TE's specification control drawings, because
        // that is the whole point of the shelf: the outside diameter is what a bundle is added up
        // from, and a diameter somebody guessed at gives a harness that does not fit its gland.
        PartsLibrary lib;
        const int added = addStandardSpec55(lib);
        const auto wires = family(lib, QStringLiteral("55A0111-"));
        const auto pairs = family(lib, QStringLiteral("55A0121-"));
        const auto screened = family(lib, QStringLiteral("55A1121-"));
        std::printf("  spec55: %d parts - %d wire, %d pair, %d screened pair\n", added,
                    int(wires.size()), int(pairs.size()), int(screened.size()));
        // Twelve sizes, 30 through 8. The wire in three colours, the two cables in one each.
        check(wires.size() == 36, "spec55: twelve sizes of wire in three colours");
        check(pairs.size() == 12, "spec55: twelve sizes of the unscreened pair");
        check(screened.size() == 12, "spec55: twelve sizes of the screened pair");
        check(added == 60, "spec55: and that is the whole shelf");

        auto byPn = [&lib](const QString& pn) -> const LibraryPart* {
            for (const LibraryPart& p : lib.parts)
                if (p.mfrPn == pn) return &p;
            return nullptr;
        };
        // EVERY SIZE THE DRAWINGS ARE MADE IN, and no size they are not.
        const int madeIn[] = {30, 28, 26, 24, 22, 20, 18, 16, 14, 12, 10, 8};
        bool allSizes = true;
        for (int awg : madeIn)
            if (!byPn(QStringLiteral("55A0111-%1-9").arg(awg)) ||
                !byPn(QStringLiteral("55A0121-%1-6/9").arg(awg)) ||
                !byPn(QStringLiteral("55A1121-%1-6/9-9").arg(awg)))
                allSizes = false;
        check(allSizes, "spec55: 30 through 8 AWG, in all three families");

        // THE PART NUMBERS ARE THE DRAWINGS' OWN. 55A0111 page 2 gives "AWG 22, white: 55A0111-22-9"
        // as its worked example, and 55A1121 gives "AWG 22, blue and white component wires; white
        // jacket: 55A1121-22-6/9-9" as its. Both are on the shelf, spelled that way.
        const LibraryPart* w22 = byPn(QStringLiteral("55A0111-22-9"));
        const LibraryPart* p20 = byPn(QStringLiteral("55A0121-20-6/9"));
        const LibraryPart* s22 = byPn(QStringLiteral("55A1121-22-6/9-9"));
        check(w22 && p20 && s22, "spec55: the drawings' own worked examples are the part numbers");
        if (!w22 || !p20 || !s22) return shrinkFailures;

        // AND THE FIGURES ARE THE DRAWINGS', CONVERTED. 22 AWG wire is .043 in over the insulation,
        // which is 1.09 mm; the 20 AWG pair is .100 in, which is 2.54 mm; the 22 AWG screened pair is
        // .119 in, which is 3.02 mm.
        std::printf("  spec55: 55A0111-22-9 od %s mm, R %s ohm/m; 55A0121-20 od %s mm; "
                    "55A1121-22 od %s mm\n",
                    qPrintable(w22->params.value(QStringLiteral("od"))),
                    qPrintable(w22->params.value(QStringLiteral("resistance"))),
                    qPrintable(p20->params.value(QStringLiteral("od"))),
                    qPrintable(s22->params.value(QStringLiteral("od"))));
        check(w22->params.value(QStringLiteral("od")) == QLatin1String("1.09"),
              "spec55: .043 in over the wire is 1.09 mm");
        check(p20->params.value(QStringLiteral("od")) == QLatin1String("2.54"),
              "spec55: .100 in over the pair is 2.54 mm");
        check(s22->params.value(QStringLiteral("od")) == QLatin1String("3.02"),
              "spec55: .119 in over the screened pair is 3.02 mm");
        // The biggest and the smallest, so a transcription slip at either end of the table shows.
        check(byPn(QStringLiteral("55A0111-30-9"))->params.value(QStringLiteral("od")) ==
                  QLatin1String("0.61"),
              "spec55: and 30 AWG, the thinnest, is .024 in = 0.61 mm");
        check(byPn(QStringLiteral("55A1121-8-6/9-9"))->params.value(QStringLiteral("od")) ==
                  QLatin1String("10.59"),
              "spec55: and the 8 AWG screened pair, the fattest, is .417 in = 10.59 mm");
        // Resistance is given per 1000 ft and kept per metre: 16.2 / 304.8.
        check(w22->params.value(QStringLiteral("resistance")) == QLatin1String("0.0531"),
              "spec55: 16.2 ohm/1000 ft is 0.0531 ohm/m");
        check(w22->params.value(QStringLiteral("strands")) == QLatin1String("19 x 34"),
              "spec55: with the stranding the drawing gives");
        check(w22->params.value(QStringLiteral("voltage")) == QLatin1String("600") &&
                  w22->params.value(QStringLiteral("temp")) == QLatin1String("-65 to +150"),
              "spec55: 600 V and 150 C, off page 2");

        // A WIRE IS A WIRE AND A CABLE IS A CABLE, or the bundle tool offers the wrong shelf.
        check(w22->kind == PartKind::Wire, "spec55: the single conductor is filed as wire");
        check(p20->kind == PartKind::Cable && s22->kind == PartKind::Cable,
              "spec55: the two-conductor cables are filed as cable");
        check(p20->params.value(QStringLiteral("screened")) == QLatin1String("0"),
              "spec55: 55A0121 is the unscreened one");
        check(s22->params.value(QStringLiteral("screened")) == QLatin1String("1") &&
                  s22->params.value(QStringLiteral("shield")).contains(QLatin1String("38 AWG")),
              "spec55: 55A1121 carries its 38 AWG braid");
        check(byPn(QStringLiteral("55A1121-8-6/9-9"))
                  ->params.value(QStringLiteral("shield"))
                  .contains(QLatin1String("36 AWG")),
              "spec55: and the 8 AWG one its 36 AWG braid, which is the one size that differs");

        // THE MAKE-UP READS BACK. The structure line is not decoration: the harness builder expands a
        // cable into its conductors from it, so a line it cannot parse is a cable nobody can use.
        const CableSpec unscreened = cableSpecOf(*p20);
        const CableSpec withBraid = cableSpecOf(*s22);
        std::printf("  spec55: 55A0121 reads as \"%s\", 55A1121 as \"%s\"\n",
                    qPrintable(unscreened.summary()), qPrintable(withBraid.summary()));
        check(unscreened.conductorCount() == 2 && withBraid.conductorCount() == 2,
              "spec55: two conductors each, as the title says");
        check(unscreened.elements.size() == 1 &&
                  unscreened.elements.front().kind == CableElementKind::Pair &&
                  unscreened.elements.front().twisted,
              "spec55: 55A0121 is one twisted pair");
        check(!unscreened.overallFoil && !unscreened.overallBraid,
              "spec55: with nothing over it");
        check(withBraid.overallBraid, "spec55: and 55A1121 has the braid over the lot");

        // ADDING IT TWICE DOES NOT PUT A SECOND ONE ON EVERY BILL OF MATERIALS.
        const size_t was = lib.parts.size();
        check(addStandardSpec55(lib) == 0 && lib.parts.size() == was,
              "spec55: putting the shelf in again adds nothing");
        // And a size taken out on purpose comes back only if the whole shelf is re-offered, which is
        // what the flag on the library is for.
        lib.parts.erase(std::remove_if(lib.parts.begin(), lib.parts.end(),
                                       [](const LibraryPart& p) {
                                           return p.mfrPn == QStringLiteral("55A0111-22-9");
                                       }),
                        lib.parts.end());
        check(addStandardSpec55(lib) == 1, "spec55: one taken out comes back, and only that one");

        // A LIBRARY MADE BEFORE THE SHELF EXISTED gets it once, and remembers.
        PartsLibrary old;
        check(!old.standardSpec55, "spec55: a library written before it has no flag");
        check(ensureStandardSpec55(old) == 60 && old.standardSpec55,
              "spec55: opening it puts the shelf in and records that it was done");
        check(ensureStandardSpec55(old) == 0, "spec55: opening it again adds nothing");
        // And the flag survives a trip through the file.
        PartsLibrary reread;
        reread.fromJson(old.toJson());
        check(reread.standardSpec55, "spec55: the flag is written and read back");
        PartsLibrary older;
        QJsonObject noFlag = old.toJson();
        noFlag.remove(QStringLiteral("standardSpec55"));
        older.fromJson(noFlag);
        check(!older.standardSpec55,
              "spec55: a file written before the flag reads as not done, which brings the shelf to it");

        // AND A LIBRARY THAT STARTS FROM NOTHING STARTS WITH IT.
        const PartsLibrary seeded = seedLibrary();
        check(!family(seeded, QStringLiteral("55A0111-")).empty() &&
                  !family(seeded, QStringLiteral("55A1121-")).empty(),
              "spec55: a freshly seeded library has the shelf on it");
    }

    // ---- the Alpha hook-up shelf: 2626 and 3079, in every colour they are made in ----------------
    {
        // Two wires at the far ends of what a harness needs: 2626 is 26 AWG on nickel-plated strands
        // under a fluoropolymer wall and goes to 300 C, and 3079 is 14 AWG on tinned strands under
        // PVC and carries the current. Every figure checked here is off Alpha's own customer
        // specification for the part, because the outside diameter is what a bundle is added up from.
        PartsLibrary lib;
        const int added = addStandardAlphaHookup(lib);
        const auto a2626 = family(lib, QStringLiteral("2626 "));
        const auto a3079 = family(lib, QStringLiteral("3079 "));
        std::printf("  alpha: %d parts - %d of 2626, %d of 3079\n", added, int(a2626.size()),
                    int(a3079.size()));
        // The specification lists eight colours for 2626 and fifteen for 3079.
        check(a2626.size() == 8, "alpha: 2626 in the eight colours its specification lists");
        check(a3079.size() == 15, "alpha: 3079 in the fifteen its specification lists");
        check(added == 23, "alpha: and that is the whole shelf");

        auto byPn = [&lib](const QString& pn) -> const LibraryPart* {
            for (const LibraryPart& p : lib.parts)
                if (p.mfrPn == pn) return &p;
            return nullptr;
        };
        // THE TWO PART NUMBERS THAT WERE ASKED FOR BY NAME are spelled the way they were asked for.
        const LibraryPart* red = byPn(QStringLiteral("2626 RD005"));
        const LibraryPart* black = byPn(QStringLiteral("3079 BK005"));
        check(red && black, "alpha: 2626 RD005 and 3079 BK005 are on the shelf, spelled that way");
        if (!red || !black) return shrinkFailures;

        // AND THE FIGURES ARE THE SPECIFICATION'S, CONVERTED ONCE. 2626 is .033 in over the
        // insulation, which is 0.84 mm; 3079 is .138 in, which is 3.51 mm. Resistance is given per
        // 1000 ft and kept per metre: 40 / 304.8 and 2.8 / 304.8.
        std::printf("  alpha: 2626 od %s mm R %s ohm/m; 3079 od %s mm R %s ohm/m\n",
                    qPrintable(red->params.value(QStringLiteral("od"))),
                    qPrintable(red->params.value(QStringLiteral("resistance"))),
                    qPrintable(black->params.value(QStringLiteral("od"))),
                    qPrintable(black->params.value(QStringLiteral("resistance"))));
        check(red->params.value(QStringLiteral("od")) == QLatin1String("0.84"),
              "alpha: .033 in over 2626 is 0.84 mm");
        check(black->params.value(QStringLiteral("od")) == QLatin1String("3.51"),
              "alpha: .138 in over 3079 is 3.51 mm");
        check(red->params.value(QStringLiteral("resistance")) == QLatin1String("0.1312"),
              "alpha: 40 ohm/1000 ft is 0.1312 ohm/m");
        check(black->params.value(QStringLiteral("resistance")) == QLatin1String("0.0092"),
              "alpha: and 2.8 ohm/1000 ft is 0.0092 ohm/m");
        check(red->params.value(QStringLiteral("gauge")) == QLatin1String("26") &&
                  red->params.value(QStringLiteral("strands")) == QLatin1String("19 x 38"),
              "alpha: 2626 is 26 AWG, 19 x 38");
        check(black->params.value(QStringLiteral("gauge")) == QLatin1String("14") &&
                  black->params.value(QStringLiteral("strands")) == QLatin1String("41 x 30"),
              "alpha: 3079 is 14 AWG, 41 x 30");
        check(red->params.value(QStringLiteral("temp")) == QLatin1String("-150 to +300") &&
                  black->params.value(QStringLiteral("temp")) == QLatin1String("-40 to +105"),
              "alpha: with the temperature range each is rated over");
        // FIVE TIMES THE DIAMETER, which is what both specifications say - the rest of the shelf
        // assumes ten for want of being told, and these were told.
        check(red->params.value(QStringLiteral("bendRadius")) == QLatin1String("4.2") &&
                  black->params.value(QStringLiteral("bendRadius")) == QLatin1String("17.5"),
              "alpha: and a bend radius of five times the finished diameter, as each says");
        check(red->kind == PartKind::Wire && black->kind == PartKind::Wire,
              "alpha: both are filed as wire, so the bundle tool offers them as wire");

        // A COLOUR WHOSE ALPHA SUFFIX IS NOT ON THE SPECIFICATION IS NOT GIVEN ONE HERE. The
        // specification names its colours in words; the two-letter suffix is Alpha's own and is not
        // on it. For the ten solid colours the suffix is the standard one, and for the light blue
        // and the four striped ones it is not filled in with a guess: the part is stocked, because
        // the specification says the wire is made in it, and the part number carries the colour in
        // the specification's own words so that it reads as a question rather than as a code.
        const LibraryPart* lightBlue = byPn(QStringLiteral("3079 LT BLUE 005"));
        check(lightBlue != nullptr,
              "alpha: the light blue 3079 is stocked, because the specification says it is made");
        check(lightBlue &&
                  lightBlue->params.value(QStringLiteral("notes"))
                      .contains(QLatin1String("NOT ON THE SPECIFICATION")),
              "alpha: and it says so, rather than carrying a suffix nobody has checked");
        check(byPn(QStringLiteral("3079 GREEN/YELLOW 005")) &&
                  byPn(QStringLiteral("3079 WHITE/BLACK 005")),
              "alpha: the striped colours are stocked the same way");
        check(byPn(QStringLiteral("3079 SL005")) && byPn(QStringLiteral("3079 VI005")),
              "alpha: and slate and violet, which 3079 has and 2626 has not, carry their suffixes");
        check(byPn(QStringLiteral("2626 SL005")) == nullptr,
              "alpha: 2626 is not stocked in a colour its specification does not list");

        // ADDING IT TWICE DOES NOT PUT A SECOND ONE ON EVERY BILL OF MATERIALS.
        const size_t was = lib.parts.size();
        check(addStandardAlphaHookup(lib) == 0 && lib.parts.size() == was,
              "alpha: putting the shelf in again adds nothing");

        // A LIBRARY MADE BEFORE THE SHELF EXISTED gets it once, and remembers.
        PartsLibrary old;
        check(!old.standardAlphaHookup, "alpha: a library written before it has no flag");
        check(ensureStandardAlphaHookup(old) == 23 && old.standardAlphaHookup,
              "alpha: opening it puts the shelf in and records that it was done");
        check(ensureStandardAlphaHookup(old) == 0, "alpha: opening it again adds nothing");
        PartsLibrary reread;
        reread.fromJson(old.toJson());
        check(reread.standardAlphaHookup, "alpha: the flag is written and read back");

        // AND A LIBRARY THAT STARTS FROM NOTHING STARTS WITH IT.
        const PartsLibrary fresh = seedLibrary();
        check(!family(fresh, QStringLiteral("2626 ")).empty() &&
                  !family(fresh, QStringLiteral("3079 ")).empty(),
              "alpha: a freshly seeded library has both of them on it");
    }

    if (shrinkFailures == 0) std::printf("  -> ok\n");
    else std::printf("  -> %d failed\n", shrinkFailures);
    return shrinkFailures;
}
