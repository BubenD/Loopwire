// The heat-shrink shelf: the families a harness shop actually keeps, in every size they are made in.
//
// What is being checked is not "are there parts" but that each family is complete and self-consistent: the
// sizes it is made in, the shrink ratio worked through into what bundle it fits, the adhesive-lined one
// marked as such, the clear one marked clear, the identification sleeve filed as a marker rather than as
// tubing - and that adding the range twice does not put a second DR-25 3/8 on every bill of materials.
#include "harness/PartsLibrary.h"

#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace Edraw_Desktop;

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

    if (shrinkFailures == 0) std::printf("  -> ok\n");
    else std::printf("  -> %d failed\n", shrinkFailures);
    return shrinkFailures;
}
