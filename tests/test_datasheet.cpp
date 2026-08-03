// Datasheets: filed by content, shared between parts, and removed only when nothing points at them.
#include "harness/Datasheet.h"
#include "io/AppPaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cmath>
#include <cstdio>

using namespace Edraw_Desktop;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("  [FAIL] %s\n", what);
        ++failures;
    }
}

// A file to attach, written in the temp area rather than anywhere that matters.
QString writeTemp(const QString& name, const QByteArray& body) {
    const QString path = QDir::tempPath() + QLatin1Char('/') + name;
    QFile f(path);
    f.open(QIODevice::WriteOnly | QIODevice::Truncate);
    f.write(body);
    f.close();
    return path;
}

} // namespace

int run_datasheet_tests() {
    std::printf("datasheet tests:\n");

    // ---- Filed by content ----
    {
        const QString a = writeTemp(QStringLiteral("sheet-a.txt"), "Resistance: 100 ohm\n");
        const QString b = writeTemp(QStringLiteral("sheet-b.txt"), "Resistance: 100 ohm\n");
        const QString c = writeTemp(QStringLiteral("sheet-c.txt"), "Capacitance: 10 uF\n");
        QString err;
        const QString sa = DatasheetStore::attach(a, &err);
        check(!sa.isEmpty() && err.isEmpty(), "attach: a file is taken in");
        check(DatasheetStore::exists(sa), "attach: and is there afterwards");
        check(DatasheetStore::pathOf(sa).contains(QLatin1String("/datasheet/")),
              "attach: in the datasheet folder");
        check(DatasheetStore::originalName(sa) == QLatin1String("sheet-a.txt"),
              "attach: the name a person recognises is kept");
        // The same file attached twice is one file, not two.
        const QString again = DatasheetStore::attach(a, &err);
        check(again == sa, "attach: attaching the same file twice files it once");
        const QString sb = DatasheetStore::attach(b, &err);
        check(!sb.isEmpty(), "attach: another file is taken in too");
        const QString sc = DatasheetStore::attach(c, &err);
        check(sc != sa && !sc.isEmpty(), "attach: different content is a different datasheet");
        // Nothing there, nothing invented.
        const QString missing =
            DatasheetStore::attach(QDir::tempPath() + QStringLiteral("/no-such-sheet.txt"), &err);
        check(missing.isEmpty() && !err.isEmpty(), "attach: a file that is not there says so");

        // ---- Shared between parts, and removed only when nothing points at it ----
        PartsCatalog cat;
        cat.setRemembering(false);
        // The id is taken straight away: add() grows the vector, so a reference into it does not
        // survive the next add.
        auto addPart = [&cat](PartKind kind, const char* pn, const QString& sheet) {
            LibraryPart& p = cat.active().add(kind);
            p.mfrPn = QString::fromLatin1(pn);
            if (!sheet.isEmpty()) p.datasheets << sheet;
            return p.id;
        };
        const QString r1Id = addPart(PartKind::Resistor, "R-100-A", sa);
        const QString r2Id = addPart(PartKind::Resistor, "R-100-B", sa);   // a family shares one
        const QString c1Id = addPart(PartKind::Capacitor, "C-10", sc);

        check(DatasheetStore::tiedParts(cat, sa).size() == 2,
              "tied: two parts point at the shared datasheet");
        const QStringList titles = DatasheetStore::tiedPartTitles(cat, sa);
        check(titles.size() == 2 &&
                  titles.join(QStringLiteral(" ")).contains(QLatin1String("R-100-A")),
              "tied: and it can say which ones");

        // Taking it off one part keeps the file, and says who it is kept for.
        const DatasheetStore::DetachResult first = DatasheetStore::detach(cat, r1Id);
        std::printf("    (first removal: kept, still tied to %s)\n",
                    qPrintable(first.stillTiedTo.join(QStringLiteral(", "))));
        check(first.wasAttached, "detach: it was attached");
        check(!first.fileDeleted, "detach: the file is KEPT while another part points at it");
        check(DatasheetStore::exists(sa), "detach: and is still on disk");
        check(first.stillTiedTo.size() == 1 &&
                  first.stillTiedTo.front().contains(QLatin1String("R-100-B")),
              "detach: naming the part it is kept for");
        check(!cat.active().byId(r1Id)->hasDatasheet(),
              "detach: the part no longer points at it");

        // Taking it off the last part deletes the file.
        const DatasheetStore::DetachResult last = DatasheetStore::detach(cat, r2Id);
        check(last.wasAttached && last.stillTiedTo.isEmpty(),
              "detach: nothing is tied to it any more");
        check(last.fileDeleted, "detach: so the file goes");
        check(!DatasheetStore::exists(sa), "detach: and is gone from disk");
        check(DatasheetStore::exists(sc), "detach: another part's datasheet is not touched");

        // A part with none is not a failure, just nothing to do.
        const DatasheetStore::DetachResult none = DatasheetStore::detach(cat, r1Id);
        check(!none.wasAttached && !none.fileDeleted, "detach: a part with no datasheet is a no-op");

        // Orphans: a file nothing points at.
        const QStringList orphans = DatasheetStore::orphans(cat);
        check(orphans.contains(sb), "orphans: a datasheet no part points at is listed");
        check(!orphans.contains(sc), "orphans: one that is pointed at is not");

        DatasheetStore::detach(cat, c1Id);
        QFile::remove(DatasheetStore::pathOf(sb));
        QFile::remove(a);
        QFile::remove(b);
        QFile::remove(c);
    }

    // ---- Reading one ----
    {
        const QString txt = writeTemp(QStringLiteral("ds.txt"),
                                      "Nominal resistance: 4k7 ohm\nTolerance: 1 %\n");
        QString how;
        const QString text = datasheetText(txt, &how);
        check(text.contains(QLatin1String("4k7")), "read: a text file is read as it is");
        check(how.contains(QLatin1String("text")), "read: and says how it was read");

        const QString html =
            writeTemp(QStringLiteral("ds.html"),
                      "<html><body><p>Capacitance: 10 uF</p><script>ignore()</script></body></html>");
        const QString htext = datasheetText(html, &how);
        check(htext.contains(QLatin1String("Capacitance")), "read: HTML has its tags taken off");
        check(!htext.contains(QLatin1String("ignore")), "read: and its scripts left out");

        // A file that is not there is not read, and does not pretend to be empty.
        const QString nothing = datasheetText(QDir::tempPath() + QStringLiteral("/nope.txt"), &how);
        check(nothing.isEmpty() && how.contains(QLatin1String("no file")),
              "read: a missing file says so rather than reading as blank");
        QFile::remove(txt);
        QFile::remove(html);
    }

    // ---- Pulling the figures out ----
    {
        const QString text = QStringLiteral(
            "MFR-25FBF Series\n"
            "Nominal resistance: 4.7 kohm\n"
            "Tolerance: 1 %\n"
            "Rated power: 250 mW\n"
            "Temperature coefficient: 50 ppm/K\n"
            "Working voltage: 250 V\n");
        const std::vector<PulledField> got = pullTechnicalFields(text, PartKind::Resistor);
        std::printf("    (pulled %zu field(s) from a resistor datasheet)\n", got.size());
        for (const PulledField& f : got)
            std::printf("      %-12s %-8s from: %s\n", qPrintable(f.key), qPrintable(f.value),
                        qPrintable(f.evidence));
        auto valueOf = [&got](const char* key) {
            for (const PulledField& f : got)
                if (f.key == QLatin1String(key)) return f.value;
            return QString();
        };
        check(valueOf("value") == QLatin1String("4700"),
              "pull: 4.7 kohm becomes 4700 - the prefix is multiplied out");
        check(valueOf("tolerance") == QLatin1String("1"), "pull: the tolerance");
        check(valueOf("power") == QLatin1String("0.25"), "pull: 250 mW becomes 0.25 W");
        check(valueOf("tempco") == QLatin1String("50"), "pull: the temperature coefficient");
        check(valueOf("voltage") == QLatin1String("250"), "pull: the working voltage");
        // Every figure comes with the line it was read off, so it can be judged before it is taken.
        for (const PulledField& f : got)
            check(!f.evidence.isEmpty(), "pull: with the line it came from as evidence");
        // And each goes in a field that kind actually has.
        for (const PulledField& f : got) {
            bool real = false;
            for (const PartField& pf : partFields(PartKind::Resistor))
                if (pf.key == f.key) real = true;
            check(real, "pull: into a field the kind really has");
        }

        // A capacitor in nanofarads comes back in the microfarads its field is written in.
        const std::vector<PulledField> caps = pullTechnicalFields(
            QStringLiteral("Capacitance: 100 nF\nVoltage rating: 50 V\nTolerance: 10 %\n"),
            PartKind::Capacitor);
        QString cval, cv;
        for (const PulledField& f : caps) {
            if (f.key == QLatin1String("value")) cval = f.value;
            if (f.key == QLatin1String("voltage")) cv = f.value;
        }
        std::printf("    (100 nF -> %s uF, %s V)\n", qPrintable(cval), qPrintable(cv));
        check(cval == QLatin1String("0.1"), "pull: 100 nF is 0.1 uF, in the field's own unit");
        check(cv == QLatin1String("50"), "pull: and the voltage rating with it");

        // A bead's impedance and the frequency it is quoted at.
        const std::vector<PulledField> bead = pullTechnicalFields(
            QStringLiteral("Impedance: 600 ohm at 100 MHz\nRated current: 200 mA\n"),
            PartKind::FerriteBead);
        QString z, atf, cur;
        for (const PulledField& f : bead) {
            if (f.key == QLatin1String("impedance")) z = f.value;
            if (f.key == QLatin1String("atFreq")) atf = f.value;
            if (f.key == QLatin1String("current")) cur = f.value;
        }
        std::printf("    (bead: %s ohm at %s MHz, %s A)\n", qPrintable(z), qPrintable(atf),
                    qPrintable(cur));
        check(z == QLatin1String("600"), "pull: the bead's impedance");
        check(atf == QLatin1String("100"), "pull: and the frequency it is quoted at");
        check(cur == QLatin1String("0.2"), "pull: 200 mA becomes 0.2 A");

        // Nothing found is nothing returned - no figure is invented to fill a field.
        check(pullTechnicalFields(QStringLiteral("A datasheet with no numbers in it at all."),
                                  PartKind::Resistor)
                  .empty(),
              "pull: a datasheet with nothing in it fills nothing in");
        check(pullTechnicalFields(text, PartKind::Backshell).empty(),
              "pull: and a kind with no patterns of its own pulls nothing");
        check(pullTechnicalFields(QString(), PartKind::Resistor).empty(),
              "pull: no text, no figures");
    }

    if (failures == 0) std::printf("  -> ok\n");
    else std::printf("  -> %d failed\n", failures);
    return failures;
}
