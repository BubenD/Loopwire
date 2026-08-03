// Cables: what one is made of, what that comes to, and what a harness gets when one is placed.
#include "harness/CableSpec.h"
#include "harness/Harness.h"
#include "harness/PartsLibrary.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QFile>
#include <QJsonDocument>
#include <QSet>
#include <cmath>
#include <cstdio>

using namespace Edraw_Desktop;

namespace {
int g_fail = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("  [FAIL] %s\n", what); ++g_fail; }
}
bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) < eps; }

CableElement element(CableElementKind kind, int count, const char* gauge, bool twisted, bool foil,
                     bool braid, const QStringList& colours = {}) {
    CableElement e;
    e.kind = kind;
    e.count = count;
    e.gauge = QString::fromLatin1(gauge);
    e.twisted = twisted;
    e.foil = foil;
    e.braid = braid;
    e.colours = colours;
    return e;
}

// A real cable to work with: four screened twisted pairs, two singles, a coax, an overall braid and a
// drain wire - the sort of thing a motorsport or aerospace loom actually carries.
CableSpec sampleCable() {
    CableSpec s;
    s.odMm = 9.4;
    s.jacket = QStringLiteral("PTFE");
    s.conductor = QStringLiteral("Silver-plated copper");
    s.overallBraid = true;
    s.braidCoverage = 85;
    s.drainGauge = QStringLiteral("0.34");
    s.elements.push_back(element(CableElementKind::Pair, 4, "0.34", true, true, false));
    s.elements.push_back(element(CableElementKind::Single, 2, "0.5", false, false, false));
    s.elements.push_back(element(CableElementKind::Coax, 1, "0.22", false, false, false));
    return s;
}
} // namespace

int run_cable_tests() {
    g_fail = 0;
    std::printf("cable tests\n");

    // --- The kind is in the library, and it is bought by the metre.
    {
        bool found = false;
        for (PartKind k : allPartKinds())
            if (k == PartKind::Cable) found = true;
        check(found, "cable: the library has a Cable kind");
        check(partKindName(PartKind::Cable) == QLatin1String("Cable"), "cable: called Cable");
        check(partKindFromKey(QStringLiteral("cable")) == PartKind::Cable,
              "cable: and its key round-trips");
        check(defaultUnitForKind(PartKind::Cable) == QLatin1String("m"),
              "cable: bought by the metre, like wire");
        // The four things asked for are all there, and the make-up has a home.
        QStringList keys;
        for (const PartField& f : partFields(PartKind::Cable)) keys << f.key;
        check(keys.contains(QStringLiteral("od")), "cable: outer diameter");
        check(keys.contains(QStringLiteral("jacket")), "cable: jacket material");
        check(keys.contains(QStringLiteral("material")), "cable: conductor material");
        check(keys.contains(QStringLiteral("structure")), "cable: and the make-up");
        check(!partFieldChoices(PartKind::Cable, QStringLiteral("jacket")).isEmpty(),
              "cable: the jacket offers the polymers");
    }

    // --- How many conductors, and is it screened.
    {
        const CableSpec s = sampleCable();
        // 4 pairs = 8, 2 singles = 2, a coax = 2 (its screen is a conductor), a drain = 1.
        std::printf("    (%s)\n", qPrintable(s.summary()));
        check(s.conductorCount() == 13, "cable: every conductor counted, the coax screen and the "
                                        "drain included");
        check(s.screened(), "cable: screened, because it has screens");
        CableSpec plain;
        plain.elements.push_back(element(CableElementKind::Single, 3, "0.5", false, false, false));
        check(plain.conductorCount() == 3 && !plain.screened(),
              "cable: three singles and nothing else is three conductors, unscreened");
        check(s.summary().contains(QLatin1String("twisted pair")) &&
                  s.summary().contains(QLatin1String("overall braid 85%")),
              "cable: the summary says what it is");
    }

    // --- Saved, loaded, and it is the same cable.
    {
        const CableSpec s = sampleCable();
        const CableSpec back = CableSpec::fromCompact(s.toCompact());
        check(back.conductorCount() == s.conductorCount(), "cable: the make-up round-trips");
        check(near(back.odMm, s.odMm) && back.jacket == s.jacket && back.conductor == s.conductor,
              "cable: and so do its own properties");
        check(back.overallBraid == s.overallBraid && back.braidCoverage == s.braidCoverage &&
                  back.drainGauge == s.drainGauge,
              "cable: screen and drain too");
        check(back.elements.size() == s.elements.size(), "cable: every element");
        check(back.elements.front().kind == CableElementKind::Pair &&
                  back.elements.front().count == 4 && back.elements.front().foil,
              "cable: with what each one is");
        check(CableSpec::fromCompact(QString()).isEmpty(),
              "cable: nothing in, nothing out - an undescribed cable is empty, not invented");
        // It goes in a part's parameters like any other value, so it is saved with the library.
        PartsLibrary lib;
        LibraryPart& p = lib.add(PartKind::Cable);
        p.params[QStringLiteral("structure")] = s.toCompact();
        const QString path = QDir::tempPath() + QStringLiteral("/edraw-cable-test.plb");
        check(lib.save(path), "cable: the library writes");
        PartsLibrary reread;
        check(reread.load(path), "cable: and reads back");
        check(!reread.parts.empty() &&
                  CableSpec::fromCompact(reread.parts.front().value(QStringLiteral("structure")))
                          .conductorCount() == 13,
              "cable: and the make-up survives the library file");
        QFile::remove(path);
    }

    // --- The jacket's own colour, and a coverage per screen.
    {
        CableSpec s = sampleCable();
        s.jacketColour = QStringLiteral("BK");
        s.overallFoil = true;
        s.foilCoverage = 100;     // a wrap
        s.braidCoverage = 85;     // woven
        const QString said = s.summary();
        std::printf("    (%s)\n", qPrintable(said));
        check(said.contains(QLatin1String("foil 100%")) && said.contains(QLatin1String("braid 85%")),
              "cable: the foil and the braid have their own coverage, and both are said");
        check(said.contains(QLatin1String("jacket")), "cable: and the jacket colour is said");
        const CableSpec back = CableSpec::fromCompact(s.toCompact());
        check(back.jacketColour == QLatin1String("BK") && back.foilCoverage == 100 &&
                  back.braidCoverage == 85,
              "cable: all three round-trip");
        // The jacket is drawn in its colour, so a black cable looks like one.
        const std::vector<Entity> section = cableCrossSection(back, back.odMm);
        bool jacketColoured = false;
        for (const Entity& e : section)
            if (e.type == EntityType::Circle && std::abs(e.radius - back.odMm / 2.0) < 1e-9 &&
                e.hasExplicitColor())
                jacketColoured = true;
        check(jacketColoured, "cross-section: the jacket is drawn in the jacket's colour");
    }

    // --- A material added to a list is offered by every window that asks for that field.
    {
        // The added choices live in the application's settings, and this test must not touch the
        // real ones: the format and the names are pointed at a file of the test's own for the
        // duration, and put back afterwards.
        const QString wasOrg = QCoreApplication::organizationName();
        const QString wasApp = QCoreApplication::applicationName();
        const QString sandbox = QDir::tempPath() + QStringLiteral("/edraw-test-settings");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, sandbox);
        QCoreApplication::setOrganizationName(QStringLiteral("EdrawTests"));
        QCoreApplication::setApplicationName(QStringLiteral("choices"));

        const QStringList builtIn = partFieldChoices(PartKind::Cable, QStringLiteral("jacket"));
        check(!builtIn.isEmpty(), "choices: the jacket has a built-in list");
        rememberChoice(QStringLiteral("jacket"), QStringLiteral("Smoke-Test-Compound"), builtIn);
        const QStringList after = choicesFor(PartKind::Cable, QStringLiteral("jacket"));
        check(after.size() == builtIn.size() + 1 &&
                  after.contains(QStringLiteral("Smoke-Test-Compound")),
              "choices: one typed in is offered afterwards, alongside the built-in ones");
        // A built-in value is not stored a second time, and nor is the same addition twice.
        rememberChoice(QStringLiteral("jacket"), builtIn.first(), builtIn);
        rememberChoice(QStringLiteral("jacket"), QStringLiteral("Smoke-Test-Compound"), builtIn);
        check(choicesFor(PartKind::Cable, QStringLiteral("jacket")).size() == builtIn.size() + 1,
              "choices: and nothing is stored twice");
        // The sandbox goes, and the application is what it was.
        QSettings().clear();
        QCoreApplication::setOrganizationName(wasOrg);
        QCoreApplication::setApplicationName(wasApp);
        QDir(sandbox).removeRecursively();
    }

    // --- What the harness gets. This is the whole point.
    {
        const CableSpec s = sampleCable();
        const std::vector<CableConductor> cores = cableConductors(s, QStringLiteral("K1"));
        std::printf("    (%zu conductor(s) for the harness)\n", cores.size());
        // A row per conductor INSIDE the jacket. The drain is a conductor of the cable, counted by
        // conductorCount, but it is not a hook-up wire to be landed on a pin: it terminates the
        // overall screen, and that screen is asked for on the connectors instead.
        check(static_cast<int>(cores.size()) == s.conductorCount() - 1,
              "place: a row per conductor, the drain excepted - it belongs to the overall screen");

        // Every conductor belongs to the cable.
        bool allBundled = true;
        for (const CableConductor& c : cores)
            if (c.bundle != QLatin1String("K1")) allBundled = false;
        check(allBundled, "place: all of them in one bundle - they ARE one cable");

        // The pairs are twisted, two by two, and each pair has its own group.
        QSet<QString> twistGroups;
        int twisted = 0;
        for (const CableConductor& c : cores)
            if (!c.twistGroup.isEmpty()) { ++twisted; twistGroups.insert(c.twistGroup); }
        check(twisted == 8, "place: the eight conductors of the four pairs are twisted");
        check(twistGroups.size() == 4, "place: four twisted groups - a pair is twisted with its own "
                                       "mate, not with the other pairs");

        // The screened pairs are screened, each in its own screen.
        QSet<QString> pairScreens;
        for (const CableConductor& c : cores)
            if (!c.twistGroup.isEmpty()) pairScreens.insert(c.shieldGroup);
        check(pairScreens.size() == 4,
              "place: each screened pair is in a screen of its own, not all in one");

        // The coax is two rows, drawn as a coax, one of which is the screen.
        int coaxRows = 0, coaxScreens = 0;
        for (const CableConductor& c : cores)
            if (c.kind == QLatin1String("coax")) {
                ++coaxRows;
                if (c.isScreen) ++coaxScreens;
            }
        check(coaxRows == 2 && coaxScreens == 1,
              "place: the coax is a core and a screen, and says it is a coax");

        // The drain does NOT arrive as a hook-up wire, and the overall screen is not written on to
        // the conductors: it is the cable's, and it is asked for on the connectors.
        int drains = 0;
        for (const CableConductor& c : cores)
            if (c.label.contains(QLatin1String("drain"))) ++drains;
        check(drains == 0, "place: no drain row - the overall screen is not a wire");
        check(cableWantsOverallScreen(s), "place: the cable says it wants an overall screen");
        int wrongly = 0;
        for (const CableConductor& c : cores)
            if (c.shieldGroup == QLatin1String("K1")) ++wrongly;
        check(wrongly == 0, "place: no conductor is marked as if the OVERALL screen were its own");
        {
            // A cable with an overall screen but no screened elements leaves every row unscreened -
            // this is the "a small screen appeared when no wire is screened" case.
            CableSpec plain;
            plain.overallFoil = true;
            plain.drainGauge = QStringLiteral("0.34");
            plain.elements.push_back(element(CableElementKind::Single, 3, "0.5", false, false,
                                             false));
            const std::vector<CableConductor> rows = cableConductors(plain, QStringLiteral("K2"));
            int screened = 0;
            for (const CableConductor& c : rows)
                if (!c.shieldGroup.isEmpty()) ++screened;
            check(rows.size() == 3 && screened == 0,
                  "place: an overall screen alone leaves the wires unscreened - no screen symbol "
                  "around wires that have none");
            check(cableWantsOverallScreen(plain), "place: and the cable still asks for its screen");
        }

        // Colours: the pairs walk the standard order, and nothing is left unmarked.
        QSet<QString> pairColours;
        for (const CableConductor& c : cores)
            if (!c.twistGroup.isEmpty()) pairColours.insert(c.colour);
        check(pairColours.contains(QStringLiteral("WH")) &&
                  pairColours.contains(QStringLiteral("BU")) &&
                  pairColours.contains(QStringLiteral("OG")),
              "place: the pairs are coloured white-and-something, walking the usual order");
        int unmarked = 0;
        for (const CableConductor& c : cores)
            if (c.colour.isEmpty()) ++unmarked;
        check(unmarked == 0, "place: every conductor comes out with a colour");

        // Cross-sections come from the make-up, in the mm² figure a wire is stored as.
        int at034 = 0, at05 = 0, at022 = 0;
        for (const CableConductor& c : cores) {
            if (c.gauge == QLatin1String("0.34")) ++at034;
            if (c.gauge == QLatin1String("0.5")) ++at05;
            if (c.gauge == QLatin1String("0.22")) ++at022;
        }
        // 8 pair conductors at 0.34 (the drain, also 0.34, is no longer a row), the two singles at
        // 0.5, and the coax's two at 0.22.
        check(at034 == 8 && at05 == 2 && at022 == 2,
              "place: each conductor carries its element's cross-section");
    }

    // --- A colour given by hand is used as given, and never overwritten.
    {
        CableSpec s;
        s.elements.push_back(element(CableElementKind::Pair, 1, "0.25", true, false, false,
                                     {QStringLiteral("RD"), QStringLiteral("BK")}));
        const std::vector<CableConductor> cores = cableConductors(s, QStringLiteral("C"));
        check(cores.size() == 2 && cores[0].colour == QLatin1String("RD") &&
                  cores[1].colour == QLatin1String("BK"),
              "place: the colours the cable says, not the usual ones");
    }

    // --- An overall screen is the CABLE'S, and says so - it is not each wire's own screen.
    {
        CableSpec s;
        s.overallFoil = true;
        s.elements.push_back(element(CableElementKind::Single, 3, "0.5", false, false, false));
        const std::vector<CableConductor> cores = cableConductors(s, QStringLiteral("OV"));
        QSet<QString> screens;
        for (const CableConductor& c : cores) screens.insert(c.shieldGroup);
        check(screens.size() == 1 && screens.contains(QString()) && cableWantsOverallScreen(s),
              "place: an overall screen is the cable's own, not written on to its wires");
    }

    // --- The cross-section drawing: something for every conductor, inside the jacket.
    {
        const CableSpec s = sampleCable();
        const std::vector<Entity> section = cableCrossSection(s, s.odMm);
        int circles = 0;
        double widest = 0.0;
        for (const Entity& e : section) {
            if (e.type == EntityType::Circle) {
                ++circles;
                widest = std::max(widest, e.radius);
            }
        }
        std::printf("    (cross-section: %zu object(s), %d circle(s))\n", section.size(), circles);
        check(circles >= 10, "cross-section: the jacket, the screens and the conductors are drawn");
        check(near(widest, s.odMm / 2.0, 1e-6),
              "cross-section: nothing is drawn outside the jacket");
        // NO caption: the make-up belongs in the part's Structure field, not written into the drawing
        // where it would be a second copy of the same fact, stale the moment the make-up changed.
        int texts = 0;
        for (const Entity& e : section)
            if (e.type == EntityType::Text) ++texts;
        check(texts == 0, "cross-section: the make-up is NOT written into the drawing");
        check(cableCrossSection(CableSpec{}, 0.0).size() <= 1,
              "cross-section: an empty cable draws no conductors");
    }


    // --- The shorthand a cable is spoken about in, read into a make-up. The example asked for:
    //     "3x24AWG + FTP 2x2x28AWG + coax, foil, braid, black"
    {
        QStringList notes;
        const CableSpec s = cableSpecFromText(
            QStringLiteral("3x24AWG + FTP 2x2x28AWG + coax, foil,braid, black"), &notes);
        std::printf("    (read: %s)\n", qPrintable(s.summary()));
        if (!notes.isEmpty())
            std::printf("    (not understood: %s)\n", qPrintable(notes.join(QStringLiteral("; "))));
        check(notes.isEmpty(), "shorthand: the whole line was understood");
        check(s.elements.size() == 3, "shorthand: three terms, three elements");
        // 3x24AWG: three singles of 24 AWG (0.25 mm2 on the standard list).
        check(s.elements[0].kind == CableElementKind::Single && s.elements[0].count == 3,
              "shorthand: 3x24AWG is three singles");
        check(!s.elements[0].gauge.isEmpty() && s.elements[0].enteredAwg,
              "shorthand: written in AWG, stored as the mm2 figure");
        check(wireGaugeAwg(s.elements[0].gauge) == QLatin1String("24"),
              "shorthand: and it is still called 24 AWG");
        // FTP 2x2x28AWG: two foil-screened twisted pairs.
        check(s.elements[1].kind == CableElementKind::Pair && s.elements[1].count == 2,
              "shorthand: 2x2x28AWG is two pairs");
        check(s.elements[1].twisted, "shorthand: a pair is twisted");
        check(s.elements[1].foil && !s.elements[1].braid, "shorthand: FTP means foil, per pair");
        // coax.
        check(s.elements[2].kind == CableElementKind::Coax, "shorthand: coax is a coax");
        // The tail: the overall screens and the jacket colour.
        check(s.overallFoil && s.overallBraid, "shorthand: foil and braid overall");
        check(s.jacketColour == QLatin1String("BK"), "shorthand: black is the jacket colour");
        // 3 + 4 + 2 conductors.
        check(s.conductorCount() == 9, "shorthand: nine conductors in all");
    }

    // --- The other forms people write, and the ones they get wrong.
    {
        auto first = [](const QString& text) {
            const CableSpec s = cableSpecFromText(text);
            return s.elements.empty() ? CableElement{} : s.elements.front();
        };
        check(first(QStringLiteral("4x0.5mm2")).gauge == QLatin1String("0.5"),
              "shorthand: mm2 is taken as mm2");
        check(first(QStringLiteral("4x0.5")).gauge == QLatin1String("0.5"),
              "shorthand: and a bare figure is mm2");
        check(first(QStringLiteral("2 pair 28AWG")).kind == CableElementKind::Pair,
              "shorthand: written in words as well");
        check(first(QStringLiteral("STP 4x2x26AWG")).braid,
              "shorthand: STP means braid");
        check(first(QStringLiteral("SFTP 4x2x26AWG")).foil &&
                  first(QStringLiteral("SFTP 4x2x26AWG")).braid,
              "shorthand: SFTP means both");
        check(!first(QStringLiteral("UTP 4x2x24AWG")).foil &&
                  !first(QStringLiteral("UTP 4x2x24AWG")).braid,
              "shorthand: UTP is unscreened");
        check(first(QStringLiteral("1x3x24AWG")).kind == CableElementKind::Triple,
              "shorthand: a group of three is a triple");
        check(first(QStringLiteral("2x4x24AWG")).kind == CableElementKind::Quad,
              "shorthand: and of four, a quad");
        {
            const CableSpec s = cableSpecFromText(QStringLiteral("3x24AWG, drain 24AWG, foil 100%"));
            check(!s.drainGauge.isEmpty(), "shorthand: a drain wire is read");
            check(s.overallFoil && s.foilCoverage == 100, "shorthand: with its coverage");
        }
        {
            QStringList notes;
            const CableSpec s = cableSpecFromText(QStringLiteral("3x24AWG + gibberish here"), &notes);
            check(s.elements.size() == 1, "shorthand: what reads is read");
            check(notes.size() == 1 && notes.first().contains(QLatin1String("gibberish")),
                  "shorthand: and what does not is SAID, not dropped in silence");
        }
        check(cableSpecFromText(QString()).isEmpty(), "shorthand: nothing in, nothing out");
    }

    // --- And written back out, so the field and the table always agree.
    {
        const QString line = QStringLiteral("3x24AWG + FTP 2x2x28AWG + coax, foil, braid, black");
        const CableSpec once = cableSpecFromText(line);
        const QString written = cableSpecToText(once);
        const CableSpec twice = cableSpecFromText(written);
        std::printf("    (written back: %s)\n", qPrintable(written));
        check(twice.elements.size() == once.elements.size() &&
                  twice.conductorCount() == once.conductorCount(),
              "shorthand: what is written out reads back as the same cable");
        check(twice.overallFoil == once.overallFoil && twice.overallBraid == once.overallBraid &&
                  twice.jacketColour == once.jacketColour,
              "shorthand: screens and colour included");
    }

    // --- Where a part keeps it: the make-up in its own key, the line in the field.
    {
        const CableSpec s = cableSpecFromText(QStringLiteral("2x2x28AWG, foil, black"));
        PartsLibrary lib;
        LibraryPart& p = lib.add(PartKind::Cable);
        setCableSpecOn(p, s);
        check(!p.value(QStringLiteral("makeup")).isEmpty(), "part: the make-up has its own key");
        check(p.value(QStringLiteral("structure")).contains(QLatin1String("2x2x28AWG")),
              "part: and the Structure field says it in words");
        check(cableSpecOf(p).conductorCount() == s.conductorCount(), "part: read back the same");

        // A part written before the split kept its JSON in "structure". It still has to work.
        LibraryPart old;
        old.kind = PartKind::Cable;
        old.params[QStringLiteral("structure")] = s.toCompact();
        check(cableSpecOf(old).conductorCount() == s.conductorCount(),
              "part: a make-up kept where it used to be is still found");
        // And a part whose field holds only the shorthand is read from that.
        LibraryPart typed;
        typed.kind = PartKind::Cable;
        typed.params[QStringLiteral("structure")] = QStringLiteral("4x0.5mm2, braid");
        check(cableSpecOf(typed).conductorCount() == 4 && cableSpecOf(typed).overallBraid,
              "part: a field with only the shorthand in it is read as the make-up");
    }

    // --- The schematic: a line per conductor, and the signs at BOTH ends.
    {
        CableSpec s;
        s.elements.push_back(element(CableElementKind::Pair, 1, "0.25", true, true, false));
        s.elements.push_back(element(CableElementKind::Single, 2, "0.5", false, false, false));
        s.elements.push_back(element(CableElementKind::Coax, 1, "0.22", false, false, false));
        s.overallBraid = true;
        const std::vector<Entity> sch = cableSchematic(s, 90.0);
        int horizontals = 0, diagonals = 0, dashed = 0;
        double leftMost = 1e9, rightMost = -1e9;
        for (const Entity& e : sch) {
            if (e.type != EntityType::Line) continue;
            const bool level = std::abs(e.a.y - e.b.y) < 1e-9;
            if (level && std::abs(e.b.x - e.a.x) > 40.0) ++horizontals;
            if (!level && std::abs(e.b.x - e.a.x) > 1e-9) ++diagonals;
            if (e.meta.value(QStringLiteral("lineType")).toString() == QLatin1String("dashed"))
                ++dashed;
            leftMost = std::min(leftMost, std::min(e.a.x, e.b.x));
            rightMost = std::max(rightMost, std::max(e.a.x, e.b.x));
        }
        std::printf("    (schematic: %zu object(s), %d run(s), %d crossing(s), %d dashed)\n",
                    sch.size(), horizontals, diagonals, dashed);
        // Two of the pair, two singles, the coax core: five runs the length of the cable.
        check(horizontals >= 5, "schematic: a line the length of the run for every conductor");
        // A twisted pair: one crossing sign at each end, two lines each.
        check(diagonals == 4, "schematic: the twist sign at BOTH ends of the pair, and only there");
        check(dashed > 0, "schematic: the screens are drawn dashed, as screens are");
        check(rightMost > 90.0 && leftMost < 20.0,
              "schematic: the drains reach outside the run, which is where they land");
        // A cable with nothing in it draws nothing.
        check(cableSchematic(CableSpec{}).empty(), "schematic: an empty cable draws nothing");
    }


    // --- Colour standards: which colours the conductors take, and by whose rule.
    {
        // DIN 47100 is a table of PAIRS: white/brown, then green/yellow, then grey/pink, then blue/red.
        check(cableColoursFor(CableColourStandard::Din47100, CableElementKind::Pair, 0) ==
                  QStringList({QStringLiteral("WH"), QStringLiteral("BN")}),
              "DIN 47100: the first pair is white and brown");
        check(cableColoursFor(CableColourStandard::Din47100, CableElementKind::Pair, 1) ==
                  QStringList({QStringLiteral("GN"), QStringLiteral("YE")}),
              "DIN 47100: the second is green and yellow");
        check(cableColoursFor(CableColourStandard::Din47100, CableElementKind::Pair, 2) ==
                  QStringList({QStringLiteral("GY"), QStringLiteral("PK")}),
              "DIN 47100: the third grey and pink");
        check(cableColoursFor(CableColourStandard::Din47100, CableElementKind::Pair, 3) ==
                  QStringList({QStringLiteral("BU"), QStringLiteral("RD")}),
              "DIN 47100: the fourth blue and red");

        // HD 308 S2 identifies cores by how many there are: two brown and blue, three with grey.
        check(cableColoursFor(CableColourStandard::Hd308S2, CableElementKind::Pair, 0) ==
                  QStringList({QStringLiteral("BN"), QStringLiteral("BU")}),
              "HD 308 S2: two cores are brown and blue");
        check(cableColoursFor(CableColourStandard::Hd308S2, CableElementKind::Triple, 0) ==
                  QStringList({QStringLiteral("BN"), QStringLiteral("BK"), QStringLiteral("GY")}),
              "HD 308 S2: three are brown, black and grey");

        // MIL-STD-681: the colour IS the digit, so conductor 1 is brown and conductor 9 is white.
        check(cableColoursFor(CableColourStandard::MilStd681, CableElementKind::Single, 0) ==
                  QStringList({QStringLiteral("BN")}),
              "MIL-STD-681: conductor 1 is brown");
        check(cableColoursFor(CableColourStandard::MilStd681, CableElementKind::Single, 8) ==
                  QStringList({QStringLiteral("WH")}),
              "MIL-STD-681: conductor 9 is white");
        check(cableColoursFor(CableColourStandard::MilStd681, CableElementKind::Single, 9) ==
                  QStringList({QStringLiteral("BK")}),
              "MIL-STD-681: and conductor 10 is black, the nought");

        // A coax's second conductor is its screen under any standard: a braid has no colour.
        for (CableColourStandard s : allCableColourStandards()) {
            const QStringList c = cableColoursFor(s, CableElementKind::Coax, 0);
            check(c.size() == 2 && c.at(1) == QLatin1String("SH"),
                  "every standard: a coax's second conductor is its screen");
        }
        // Every standard has a name, a key that round-trips, and a note saying what it is for.
        for (CableColourStandard s : allCableColourStandards()) {
            check(!cableColourStandardName(s).isEmpty(), "standards: each has a name");
            check(cableColourStandardFromKey(cableColourStandardKey(s)) == s,
                  "standards: and a key that round-trips");
            check(!cableColourStandardNote(s).isEmpty(), "standards: and says what it is for");
        }
    }

    // --- Choosing a standard colours the make-up, and the drawings agree with the table.
    {
        CableSpec s;
        s.colourStandard = CableColourStandard::Din47100;
        s.elements.push_back(element(CableElementKind::Pair, 1, "0.25", true, false, false));
        s.elements.push_back(element(CableElementKind::Pair, 1, "0.25", true, false, false));
        applyCableColourStandard(s, false);
        std::printf("    (DIN 47100: %s | %s)\n", qPrintable(s.elements[0].colours.join('/')),
                    qPrintable(s.elements[1].colours.join('/')));
        check(s.elements[0].colours == QStringList({QStringLiteral("WH"), QStringLiteral("BN")}) &&
                  s.elements[1].colours == QStringList({QStringLiteral("GN"), QStringLiteral("YE")}),
              "standards: the make-up is coloured, pair by pair");
        // The colours a harness gets are the same ones the table shows - that is the whole point.
        const std::vector<CableConductor> cores = cableConductors(s, QStringLiteral("K"));
        check(cores.size() == 4 && cores[0].colour == QLatin1String("WH") &&
                  cores[1].colour == QLatin1String("BN") && cores[2].colour == QLatin1String("GN"),
              "standards: and the harness gets exactly those");
        // A colour set by hand survives a standard being applied; forcing it does not.
        s.elements[0].colours[1] = QStringLiteral("RD");
        applyCableColourStandard(s, false);
        check(s.elements[0].colours.at(1) == QLatin1String("RD"),
              "standards: a colour set by hand is left alone");
        applyCableColourStandard(s, true);
        check(s.elements[0].colours.at(1) == QLatin1String("BN"),
              "standards: unless the standard is applied deliberately");
        // And it travels with the cable.
        const CableSpec back = CableSpec::fromCompact(s.toCompact());
        check(back.colourStandard == CableColourStandard::Din47100,
              "standards: the cable remembers which one it follows");
    }

    if (g_fail == 0) std::printf("  -> ok\n");
    return g_fail;
}
