// Reading a family datasheet: the figures for ONE size out of a table of sizes, and the general ones
// out of the prose around it. The text below is the DR-25 heat-shrink datasheet as it reads - a table
// keyed by size, and key features written as sentences - which is the shape almost every tubing,
// sleeving and boot datasheet takes, and the shape a "label: value" reader cannot see at all.
#include "harness/Datasheet.h"
#include "harness/PartsLibrary.h"

#include <QString>
#include <cmath>
#include <cstdio>

using namespace Loopwire;

namespace {
int g_fail = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("  [FAIL] %s\n", what); ++g_fail; }
}
bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }

// The datasheet, as the PDF reader gives it: the table headings across several lines, then a row per
// size with the inch equivalents in brackets and a tolerance on the wall thickness.
const char* kDr25 = R"(DR-25-TW (Continued)

Product Dimensions

                          Inside Diameter
   Size      Minimum              Maximum               Recovered Wall Thickness
             Inside Diameter      Inside Diameter
             as Supplied          After Recovery
   3/32      2.4 (0.094)          1.2 (0.047)           0.51 ± 0.08
   1/8       3.2 (0.125)          1.6 (0.062)           0.51 ± 0.10
   3/16      4.8 (0.187)          2.4 (0.093)           0.51 ± 0.10
   1/4       6.4 (0.250)          3.2 (0.125)           0.64 ± 0.10
   3/8       9.5 (0.375)          4.8 (0.187)           0.64 ± 0.10
   1/2       12.7 (0.500)         6.4 (0.250)           0.64 ± 0.10
   3/4       19.0 (0.748)         9.5 (0.375)           0.76 ± 0.12
   1         25.4 (1.000)         12.7 (0.500)          0.89 ± 0.16
   1 1/2     38.0 (1.500)         19.0 (0.748)          1.02 ± 0.18

Key Features
Operating temperature range -75 C to +150 C (-103 F to +302 F) (per VG 95343 Part 5 Type D)
Flame-retardant
System 25 tubing
Shrink ratio 2:1
Thin wall

Installation
Minimum shrink temperature: 125 C [257 F]
Minimum full recovery temperature: 175 C [347 F]
)";

QString valueOf(const std::vector<PulledField>& found, const char* key) {
    for (const PulledField& f : found)
        if (f.key == QLatin1String(key)) return f.value;
    return {};
}
} // namespace

int run_sizetable_tests() {
    g_fail = 0;
    std::printf("size-table datasheet tests\n");

    // --- A size, however it is written.
    {
        check(near(sizeAsNumber(QStringLiteral("1/4")), 0.25), "size: a fraction is a number");
        check(near(sizeAsNumber(QStringLiteral("3/32")), 0.09375), "size: and a small one");
        check(near(sizeAsNumber(QStringLiteral("1 1/2")), 1.5), "size: one and a half is 1.5");
        check(near(sizeAsNumber(QStringLiteral("12.7")), 12.7), "size: a plain number is itself");
        check(std::isnan(sizeAsNumber(QStringLiteral("black"))), "size: a word is not a size");
        check(std::isnan(sizeAsNumber(QStringLiteral("1/0"))), "size: and neither is a division by 0");
    }

    // --- The size out of a part number.
    {
        LibraryPart p;
        p.kind = PartKind::Heatshrink;
        p.mfrPn = QStringLiteral("DR-25-1/4");
        check(sizeTokenOf(p) == QLatin1String("1/4"), "size: DR-25-1/4 is the 1/4 size");
        p.mfrPn = QStringLiteral("RNF-100-3/32-BK");
        p.description = QStringLiteral("Heatshrink RNF-100 3/32");
        std::printf("    (RNF-100-3/32-BK -> \"%s\")\n", qPrintable(sizeTokenOf(p)));
        check(sizeTokenOf(p) == QLatin1String("3/32"),
              "size: taken from the description when the number ends in something else");
        p.mfrPn = QStringLiteral("SLEEVE-12");
        p.description.clear();
        check(sizeTokenOf(p) == QLatin1String("12"), "size: a plain number at the end counts");
        p.mfrPn = QStringLiteral("DR-25");
        check(sizeTokenOf(p) == QLatin1String("25"),
              "size: a number in the family name is all there is to go on");
    }

    // --- DR-25-1/4: the 1/4 row, and nothing from any other row.
    {
        const std::vector<PulledField> found =
            pullTechnicalFields(QString::fromUtf8(kDr25), PartKind::Heatshrink,
                                QStringLiteral("1/4"));
        QStringList got;
        for (const PulledField& f : found)
            got << QStringLiteral("%1=%2%3").arg(f.key, f.value, f.unit);
        std::printf("    (1/4: %s)\n", qPrintable(got.join(QStringLiteral("  "))));

        // From the table: as-supplied is the biggest it goes over, after-recovery the smallest it grips.
        check(valueOf(found, "entryMax") == QLatin1String("6.4"),
              "table: the 1/4 row's as-supplied diameter is the entry maximum");
        check(valueOf(found, "entryMin") == QLatin1String("3.2"),
              "table: and its recovered diameter is the minimum");
        check(valueOf(found, "wall") == QLatin1String("0.64"),
              "table: the recovered wall thickness, without its tolerance");
        // From the prose.
        check(valueOf(found, "ratio") == QLatin1String("2:1"), "prose: the shrink ratio, as written");
        check(valueOf(found, "temp") == QLatin1String("-75 to 150"),
              "prose: the operating temperature range");
        check(valueOf(found, "shrinkTemp") == QLatin1String("125"),
              "prose: the minimum shrink temperature");
        check(valueOf(found, "spec").contains(QLatin1String("95343")),
              "prose: the specification it is qualified to");
        // Every figure carries the line it came from.
        for (const PulledField& f : found)
            check(!f.evidence.isEmpty(), "every figure says where it came from");
    }

    // --- Another size gets that size's figures. This is the whole point of matching the row.
    {
        const std::vector<PulledField> a =
            pullTechnicalFields(QString::fromUtf8(kDr25), PartKind::Heatshrink,
                                QStringLiteral("3/32"));
        check(valueOf(a, "entryMax") == QLatin1String("2.4") &&
                  valueOf(a, "wall") == QLatin1String("0.51"),
              "table: the 3/32 row is the 3/32 row");
        const std::vector<PulledField> b =
            pullTechnicalFields(QString::fromUtf8(kDr25), PartKind::Heatshrink,
                                QStringLiteral("1 1/2"));
        check(valueOf(b, "entryMax") == QLatin1String("38") &&
                  valueOf(b, "entryMin") == QLatin1String("19"),
              "table: and 1 1/2 is not read as 1");
        const std::vector<PulledField> c =
            pullTechnicalFields(QString::fromUtf8(kDr25), PartKind::Heatshrink,
                                QStringLiteral("1"));
        check(valueOf(c, "entryMax") == QLatin1String("25.4"),
              "table: while 1 is the 1 inch row, not the 1 1/2 one");
    }

    // --- No size to go on: the prose still reads, the table does not - and nothing is invented.
    {
        const std::vector<PulledField> found =
            pullTechnicalFields(QString::fromUtf8(kDr25), PartKind::Heatshrink, QString());
        check(valueOf(found, "ratio") == QLatin1String("2:1"),
              "no size: what is written in words is still read");
        check(valueOf(found, "entryMax").isEmpty() && valueOf(found, "wall").isEmpty(),
              "no size: and no row is guessed at");
    }

    // --- A size that is not in the table gets nothing from it, rather than the nearest row.
    {
        const std::vector<PulledField> found =
            pullTechnicalFields(QString::fromUtf8(kDr25), PartKind::Heatshrink,
                                QStringLiteral("5/8"));
        check(valueOf(found, "entryMax").isEmpty(),
              "table: a size the datasheet does not list gives nothing");
    }

    // --- A kind with no size table is unaffected, and a resistor still reads as it did.
    {
        const std::vector<PulledField> found = pullTechnicalFields(
            QStringLiteral("Nominal resistance: 4.7 kohm\nTolerance: 1 %\nRated power: 250 mW"),
            PartKind::Resistor, QStringLiteral("0805"));
        check(valueOf(found, "value") == QLatin1String("4700"),
              "a resistor reads as it always did, size hint or no size hint");
    }

    if (g_fail == 0) std::printf("  -> ok\n");
    return g_fail;
}
