#include "harness/Datasheet.h"

#include "io/AppPaths.h"
#include "io/PdfText.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <limits>
#include <cmath>
#include <algorithm>

namespace Loopwire {
namespace DatasheetStore {

QString dir() { return AppPaths::datasheetDir(); }

QString pathOf(const QString& storedName) {
    // The name comes out of a library FILE, which is data from wherever that file came from. Joined
    // blindly it could name anything on the disk - "../../..." and worse - and this path is handed to
    // QFile::remove when a datasheet is detached and to the shell when one is opened. So: a plain file
    // name inside the datasheet folder, or nothing at all.
    if (storedName.isEmpty()) return {};
    const QFileInfo asked(storedName);
    if (asked.fileName() != storedName || storedName == QLatin1String(".") ||
        storedName == QLatin1String("..") || storedName.contains(QLatin1Char('/')) ||
        storedName.contains(QLatin1Char('\\')) || asked.isAbsolute())
        return {};
    return dir() + QLatin1Char('/') + storedName;
}

bool exists(const QString& storedName) {
    return !storedName.isEmpty() && QFile::exists(pathOf(storedName));
}

QString originalName(const QString& storedName) {
    // Stored as "<8 hex of the content>-<original name>", so the name a person recognises is still
    // in there.
    const int dash = storedName.indexOf(QLatin1Char('-'));
    return dash > 0 ? storedName.mid(dash + 1) : storedName;
}

QString attach(const QString& sourcePath, QString* error) {
    auto fail = [error](const QString& why) {
        if (error) *error = why;
        return QString();
    };
    QFileInfo info(sourcePath);
    if (!info.exists() || !info.isFile()) return fail(QStringLiteral("There is no file there."));

    QFile in(sourcePath);
    if (!in.open(QIODevice::ReadOnly)) return fail(QStringLiteral("That file cannot be read."));
    const QByteArray bytes = in.readAll();
    in.close();
    if (bytes.isEmpty()) return fail(QStringLiteral("That file is empty."));

    // Named after its own content, so the same datasheet attached to six parts is one file.
    const QString hash =
        QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())
            .left(8);
    QString base = info.fileName();
    for (QChar bad : {QLatin1Char('/'), QLatin1Char('\\'), QLatin1Char(':'), QLatin1Char('*'),
                      QLatin1Char('?'), QLatin1Char('"'), QLatin1Char('<'), QLatin1Char('>'),
                      QLatin1Char('|')})
        base.replace(bad, QLatin1Char('-'));
    const QString stored = QStringLiteral("%1-%2").arg(hash, base);
    const QString target = pathOf(stored);
    if (QFile::exists(target)) {
        if (error) error->clear();
        return stored;   // already filed: the same bytes under the same name
    }
    QDir().mkpath(dir());
    QFile out(target);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return fail(QStringLiteral("Could not write into the datasheet folder."));
    const bool wrote = out.write(bytes) == bytes.size();
    out.close();
    if (!wrote) {
        QFile::remove(target);
        return fail(QStringLiteral("Could not write the whole file."));
    }
    if (error) error->clear();
    return stored;
}

std::vector<const LibraryPart*> tiedParts(const PartsCatalog& catalog, const QString& storedName) {
    std::vector<const LibraryPart*> out;
    if (storedName.isEmpty()) return out;
    // merged() is a copy, so the pointers must not come from it - walk the catalog's own libraries.
    for (PartKind kind : allPartKinds())
        for (const LibraryPart* p : catalog.ofKind(kind))
            if (p && p->datasheets.contains(storedName)) out.push_back(p);
    return out;
}

QStringList tiedPartTitles(const PartsCatalog& catalog, const QString& storedName) {
    QStringList out;
    for (const LibraryPart* p : tiedParts(catalog, storedName)) out << p->title();
    return out;
}

DetachResult detach(PartsCatalog& catalog, const QString& partId, const QString& storedName) {
    DetachResult r;
    // Find the part in whichever connected library holds it.
    LibraryPart* part = nullptr;
    for (const QString& path : catalog.paths())
        if (PartsLibrary* lib = catalog.byPath(path))
            if (LibraryPart* p = lib->byId(partId)) part = p;
    if (!part) part = catalog.active().byId(partId);
    if (!part || part->datasheets.isEmpty()) return r;
    const QString which = storedName.isEmpty() ? part->datasheets.front() : storedName;
    if (!part->datasheets.contains(which)) return r;

    r.storedName = which;
    r.wasAttached = true;
    part->datasheets.removeAll(which);

    // Now that this part no longer points at it, whoever is left is who it is kept for.
    r.stillTiedTo = tiedPartTitles(catalog, r.storedName);
    if (r.stillTiedTo.isEmpty()) {
        r.fileDeleted = QFile::remove(pathOf(r.storedName));
    }
    return r;
}

QStringList orphans(const PartsCatalog& catalog) {
    QStringList out;
    QDir d(dir());
    for (const QString& f : d.entryList(QDir::Files, QDir::Name))
        if (tiedParts(catalog, f).empty()) out << f;
    return out;
}

} // namespace DatasheetStore

// ---- Reading one --------------------------------------------------------------------------------

QString datasheetText(const QString& path, QString* how) {
    auto note = [how](const QString& what) {
        if (how) *how = what;
    };
    QFileInfo info(path);
    if (!info.exists()) {
        note(QStringLiteral("there is no file there"));
        return QString();
    }
    const QString suffix = info.suffix().toLower();

    if (suffix == QLatin1String("pdf")) {
        // A PDF is a container, not text, and the application reads it itself now (io/PdfText):
        // nothing to install, and nothing launched to do it.
        QString why;
        const QString text = PdfText::extractFromFile(path, &why);
        if (!text.trimmed().isEmpty()) {
            note(why);
            return text;
        }
        // Only if the built-in reader came away empty is an external one tried, for the PDFs it
        // cannot manage. It is looked up on the PATH by full path first: starting a bare "pdftotext"
        // would run one planted in the working directory in preference to the real thing.
        const QString tool = QStandardPaths::findExecutable(QStringLiteral("pdftotext"));
        if (tool.isEmpty()) {
            note(why);
            return QString();
        }
        QProcess proc;
        proc.start(tool, {QStringLiteral("-layout"), QStringLiteral("-q"), path,
                          QStringLiteral("-")});
        if (!proc.waitForStarted(3000)) {
            note(why);
            return QString();
        }
        proc.waitForFinished(20000);
        const QString outside = QString::fromUtf8(proc.readAllStandardOutput());
        if (outside.trimmed().isEmpty()) {
            note(why);
            return QString();
        }
        note(QStringLiteral("read with pdftotext (the built-in reader found nothing: %1)").arg(why));
        return outside;
    }

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        note(QStringLiteral("that file cannot be read"));
        return QString();
    }
    QTextStream in(&f);
    QString text = in.readAll();
    f.close();
    if (suffix == QLatin1String("htm") || suffix == QLatin1String("html")) {
        // Enough to get at the text: the tags out, the entities that matter turned back.
        text.remove(QRegularExpression(QStringLiteral("<script[^>]*>.*?</script>"),
                                       QRegularExpression::DotMatchesEverythingOption |
                                           QRegularExpression::CaseInsensitiveOption));
        text.remove(QRegularExpression(QStringLiteral("<style[^>]*>.*?</style>"),
                                       QRegularExpression::DotMatchesEverythingOption |
                                           QRegularExpression::CaseInsensitiveOption));
        text.replace(QRegularExpression(QStringLiteral("<[^>]+>")), QStringLiteral(" "));
        text.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
        text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
        text.replace(QStringLiteral("&plusmn;"), QStringLiteral("±"));
        text.replace(QStringLiteral("&deg;"), QStringLiteral("°"));
        note(QStringLiteral("read as HTML, tags stripped"));
        return text;
    }
    note(QStringLiteral("read as text"));
    return text;
}

// ---- Pulling the figures out --------------------------------------------------------------------

namespace {

// A number, allowing a decimal point and an SI prefix where one makes sense.
constexpr auto kNum = R"((\d+(?:[.,]\d+)?))";

// Multiply out an SI prefix, so "4k7" and "4.7 k" both come back as 4700.
double applyPrefix(double value, const QString& prefix) {
    if (prefix.isEmpty()) return value;
    const QChar c = prefix.at(0);
    if (c == QLatin1Char('p')) return value * 1e-12;
    if (c == QLatin1Char('n')) return value * 1e-9;
    if (c == QLatin1Char('u') || c == QChar(0x00B5)) return value * 1e-6;
    if (c == QLatin1Char('m')) return value * 1e-3;
    if (c == QLatin1Char('k') || c == QLatin1Char('K')) return value * 1e3;
    if (c == QLatin1Char('M')) return value * 1e6;
    if (c == QLatin1Char('G')) return value * 1e9;
    return value;
}

QString tidy(double v) {
    QString s = QString::number(v, 'g', 6);
    return s;
}

// One thing to look for: which field it fills, and how it is written in a datasheet.
struct Pattern {
    const char* key;
    const char* regex;      // capture 1 = the number, capture 2 = an SI prefix when there is one
    bool prefixed;          // whether capture 2 is a prefix to multiply out
    const char* unit;
    // Not every figure on a datasheet is a number. A shrink ratio is "2:1", a temperature range is
    // "-75 to +150", a specification is "VG 95343 Part 5 Type D" - all of them written into text
    // fields, and all of them missed entirely by a reader that only knows how to parse a number.
    enum class Take { Number, Text, Range } take = Take::Number;
};

const std::vector<Pattern>& patternsFor(PartKind kind) {
    static const std::vector<Pattern> none;
    static const std::vector<Pattern> resistor = {
        {"value", R"((?:resistance|nominal\s+resistance|value)\D{0,20}(\d+(?:[.,]\d+)?)\s*([kKMG])?\s*(?:ohm|Ω|R\b))", true, "ohm"},
        {"tolerance", R"((?:tolerance)\D{0,20}±?\s*(\d+(?:[.,]\d+)?)\s*%)", false, "%"},
        {"power", R"((?:power(?:\s+rating)?|rated\s+power)\D{0,20}(\d+(?:[.,]\d+)?)\s*([m])?\s*W)", true, "W"},
        {"tempco", R"((?:temperature\s+coefficient|tempco|TCR)\D{0,20}±?\s*(\d+(?:[.,]\d+)?)\s*ppm)", false, "ppm/K"},
        {"voltage", R"((?:working\s+voltage|rated\s+voltage|voltage\s+rating)\D{0,20}(\d+(?:[.,]\d+)?)\s*V)", false, "V"},
    };
    static const std::vector<Pattern> capacitor = {
        {"value", R"((?:capacitance|nominal\s+capacitance|value)\D{0,20}(\d+(?:[.,]\d+)?)\s*([pnuµm])?\s*F)", true, "uF"},
        {"tolerance", R"((?:tolerance)\D{0,20}±?\s*(\d+(?:[.,]\d+)?)\s*%)", false, "%"},
        {"voltage", R"((?:voltage(?:\s+rating)?|rated\s+voltage|WVDC|working\s+voltage)\D{0,20}(\d+(?:[.,]\d+)?)\s*V)", false, "V"},
    };
    static const std::vector<Pattern> inductor = {
        {"value", R"((?:inductance|nominal\s+inductance|value)\D{0,20}(\d+(?:[.,]\d+)?)\s*([pnuµm])?\s*H)", true, "uH"},
        {"current", R"((?:rated\s+current|current\s+rating|I\s*rated|saturation\s+current)\D{0,20}(\d+(?:[.,]\d+)?)\s*([m])?\s*A)", true, "A"},
        {"dcr", R"((?:DC\s*resistance|DCR)\D{0,20}(\d+(?:[.,]\d+)?)\s*([mk])?\s*(?:ohm|Ω))", true, "ohm"},
        {"srf", R"((?:self[- ]resonant\s+frequency|SRF)\D{0,20}(\d+(?:[.,]\d+)?)\s*([kMG])?\s*Hz)", true, "MHz"},
    };
    static const std::vector<Pattern> bead = {
        {"impedance", R"((?:impedance)\D{0,20}(\d+(?:[.,]\d+)?)\s*([kM])?\s*(?:ohm|Ω))", true, "ohm"},
        {"atFreq", R"((?:at|@)\s*(\d+(?:[.,]\d+)?)\s*([kMG])?\s*Hz)", true, "MHz"},
        {"current", R"((?:rated\s+current|current\s+rating)\D{0,20}(\d+(?:[.,]\d+)?)\s*([m])?\s*A)", true, "A"},
        {"dcr", R"((?:DC\s*resistance|DCR)\D{0,20}(\d+(?:[.,]\d+)?)\s*([mk])?\s*(?:ohm|Ω))", true, "ohm"},
    };
    static const std::vector<Pattern> wire = {
        {"gauge", R"((\d+(?:[.,]\d+)?)\s*AWG)", false, "AWG"},
        {"voltage", R"((?:rated\s+voltage|voltage\s+rating|voltage)\D{0,20}(\d+(?:[.,]\d+)?)\s*V)", false, "V"},
        {"temperature", R"((?:temperature\s+rating|rated\s+temperature)\D{0,20}(?:-?\d+\s*(?:to|\.\.|–|-)\s*)?\+?(\d+(?:[.,]\d+)?)\s*°?\s*C)", false, "C"},
        {"od", R"((?:outside\s+diameter|nominal\s+O\.?D\.?|overall\s+diameter)\D{0,20}(\d+(?:[.,]\d+)?)\s*mm)", false, "mm"},
    };
    // Heatshrink, and the sleeving and boots that read the same way. What a tubing datasheet actually
    // says, in the words it says it in: "Shrink ratio 2:1", "Operating temperature range -75°C to
    // +150°C", "Minimum shrink temperature: 125°C". The per-size figures - the diameters and the wall
    // thickness - are in a table and are read by tableFigures(), not from here.
    static const std::vector<Pattern> heatshrink = {
        {"ratio", R"((?:shrink\s*ratio|recovery\s*ratio)\D{0,12}(\d+\s*:\s*\d+))", false, "",
         Pattern::Take::Text},
        // [^\d-] and not \D for the run between the label and the figure: \D swallows the minus sign of
        // a negative temperature, and "-75 C to +150 C" then came out as "75 to 150".
        // Between the two figures there may be a degree sign, a unit, and whatever character the PDF's
        // own font happens to draw a degree sign as - this datasheet's reads as "B", so "-75BC to
        // +150BC". Anything that is not a digit is allowed to sit there rather than the match failing.
        {"temp", R"((?:operating\s+temperature(?:\s+range)?|temperature\s+range|continuous\s+use)[^\d-]{0,20}(-?\d+)[^\d]{0,6}?(?:to|\.\.|–|—)[^\d+-]{0,4}\+?(-?\d+))",
         false, "C", Pattern::Take::Range},
        {"shrinkTemp", R"((?:minimum\s+shrink|shrink(?:ing)?\s+temperature|minimum\s+full\s+recovery)\D{0,24}(\d+(?:[.,]\d+)?)\s*°?\s*C)",
         false, "C"},
        {"colour", R"((?:colou?r)\s*:?\s*([A-Za-z][A-Za-z /-]{2,20}?)\s*(?:\r?\n|,|\.|;))", false, "",
         Pattern::Take::Text},
        {"spec", R"(((?:VG|MIL|SAE|AMS|AMS-DTL|BS|EN|ISO|DIN|ASTM|UL)[- ]?[A-Z]*[- ]?\d[\d\-]*(?:\s+Part\s+\d+)?(?:\s+Type\s+[A-Z0-9]+)?(?:\s+Class\s+\d+)?))",
         false, "", Pattern::Take::Text},
    };
    switch (kind) {
        case PartKind::Resistor:    return resistor;
        case PartKind::Capacitor:   return capacitor;
        case PartKind::Inductor:    return inductor;
        case PartKind::FerriteBead: return bead;
        case PartKind::Wire:        return wire;
        case PartKind::Heatshrink:
        case PartKind::HeatshrinkBoot:
        case PartKind::Sleeve:
        case PartKind::Conduit:
        case PartKind::Tape:        return heatshrink;
        default:                    return none;
    }
}

// ---- Reading a table of sizes --------------------------------------------------------------------
//
// A family datasheet does not say "inside diameter: 6.4 mm". It prints a table - a row per size, a
// column per figure - and the meaning of each figure is in the heading ABOVE it:
//
//     Size    Minimum Inside Diameter    Maximum Inside Diameter    Recovered Wall
//             as Supplied                After Recovery             Thickness
//     1/4     6.4 (0.250)                3.2 (0.125)                0.64 ± 0.10
//
// So there are two problems, and both have to be solved to read one figure: WHICH row belongs to this
// part, and WHAT each column of it means. That is what this does.

// A column of such a table: the words its heading is written in, and the field its figures go in.
struct Column {
    const char* keyword;   // lower case, as it appears in the heading
    const char* key;       // the part field (a Range field takes "Min"/"Max" after it)
};

const std::vector<Column>& columnsFor(PartKind kind) {
    static const std::vector<Column> none;
    // Tubing: "as supplied" is the biggest thing it will go OVER, "after recovery" the smallest it
    // will grip - so as-supplied is the entry maximum and after-recovery the minimum. That is the
    // right way round and it is worth being explicit about, because reading it the other way would
    // silently give every part an impossible entry range.
    static const std::vector<Column> tubing = {
        {"as supplied", "entryMax"},
        {"expanded", "entryMax"},
        {"as expanded", "entryMax"},
        {"after recovery", "entryMin"},
        {"fully recovered", "entryMin"},
        {"recovered inside diameter", "entryMin"},
        {"wall thickness", "wall"},
    };
    switch (kind) {
        case PartKind::Heatshrink:
        case PartKind::HeatshrinkBoot: return tubing;
        default:                       return none;
    }
}

// The numbers of one table row: the parenthesised inch equivalents and the ± tolerances taken out
// first, because "6.4 (0.250)" is one figure and "0.64 ± 0.08" is one figure.
std::vector<double> figuresOf(const QString& row) {
    QString clean = row;
    clean.remove(QRegularExpression(QStringLiteral("\\([^)]*\\)")));                  // (0.250)
    clean.remove(QRegularExpression(QString::fromUtf8("[±+]/?-?\\s*\\d+(?:[.,]\\d+)?")));   // ± 0.08
    static const QRegularExpression num(QStringLiteral("-?\\d+(?:[.,]\\d+)?"));
    std::vector<double> out;
    auto it = num.globalMatch(clean);
    while (it.hasNext()) {
        QString s = it.next().captured(0);
        s.replace(QLatin1Char(','), QLatin1Char('.'));
        bool ok = false;
        const double v = s.toDouble(&ok);
        if (ok) out.push_back(v);
    }
    return out;
}

// The size a row is for, taken off the front of it, and how much of the line it used. A size may be
// "1/4", "3/32", "12.7" or "1 1/2" - and "1 1/2" must not be read as the size 1, or a request for the
// 1 inch row would come back with the 1 1/2 inch figures.
// Does this line start with the size being looked for, in a way that means it IS that size's row?
//
// A PDF has no cells: the figures of a row are separate pieces of text at separate places, and when
// they are placed tightly there is nothing between them at all - "1/4" followed by "6.4 (0.250)" comes
// out as "1/46.4 (0.250)". Reading a number off the front of that gives 1/46, which is not a size.
// Matching the size we WANT against the front of the line has no such trouble - but it has to refuse
// the sizes that merely begin the same way: for "1", the row "1/8..." and the row "1 1/2..." both start
// with a 1 and neither of them is it.
//
// Returns how many characters the size took, or -1 for "not this row".
int rowStartsWithSize(const QString& row, const QString& hint) {
    if (hint.isEmpty()) return -1;
    int at = 0;
    while (at < row.size() && row.at(at).isSpace()) ++at;
    if (row.mid(at, hint.size()) != hint) return -1;
    const QString rest = row.mid(at + hint.size());
    if (rest.isEmpty()) return -1;                         // a row with no figures on it
    if (rest.startsWith(QLatin1Char('/'))) return -1;      // "1" against the 1/8 row
    static const QRegularExpression anotherFraction(QStringLiteral("^\\s*\\d+\\s*/\\s*\\d+"));
    if (anotherFraction.match(rest).hasMatch()) return -1; // "1" against the 1 1/2 row
    return at + static_cast<int>(hint.size());
}

double leadingSize(const QString& row, int* used = nullptr) {
    static const QRegularExpression lead(
        QStringLiteral("^\\s*(\\d+\\s+\\d+/\\d+|\\d+/\\d+|\\d+(?:[.,]\\d+)?)"));
    const QRegularExpressionMatch m = lead.match(row);
    if (!m.hasMatch()) return std::numeric_limits<double>::quiet_NaN();
    if (used) *used = m.capturedEnd(1);
    return sizeAsNumber(m.captured(1));
}

} // namespace

double sizeAsNumber(const QString& text) {
    const QString s = text.simplified();
    static const QRegularExpression mixed(
        QStringLiteral("^(\\d+)\\s+(\\d+)\\s*/\\s*(\\d+)$"));   // 1 1/2
    static const QRegularExpression frac(QStringLiteral("^(\\d+)\\s*/\\s*(\\d+)$"));
    QRegularExpressionMatch m = mixed.match(s);
    if (m.hasMatch()) {
        const double den = m.captured(3).toDouble();
        if (den == 0.0) return std::numeric_limits<double>::quiet_NaN();
        return m.captured(1).toDouble() + m.captured(2).toDouble() / den;
    }
    m = frac.match(s);
    if (m.hasMatch()) {
        const double den = m.captured(2).toDouble();
        if (den == 0.0) return std::numeric_limits<double>::quiet_NaN();
        return m.captured(1).toDouble() / den;
    }
    QString plain = s;
    plain.replace(QLatin1Char(','), QLatin1Char('.'));
    bool ok = false;
    const double v = plain.toDouble(&ok);
    return ok ? v : std::numeric_limits<double>::quiet_NaN();
}

QString sizeTokenOf(const LibraryPart& part) {
    // The size is the last thing in a part number: DR-25-1/4, RNF-100-3/32, ATUM-19/6. A fraction is
    // looked for before a plain number, because "DR-25-1/4" ends in "1/4" and not in "4".
    // A mixed number's whole part is a SINGLE digit: sizes run 1 1/2, 2 1/2, 3 1/2. Allowing more
    // digits read "RNF-100 3/32" as the mixed number "100 3/32" - a hundred and three thirty-seconds -
    // instead of the 3/32 size of the RNF-100 family.
    static const QRegularExpression tail(
        QStringLiteral("(?:^|[-_ /])(\\d\\s+\\d+/\\d+|\\d+/\\d+)\\s*$"));
    static const QRegularExpression tailNum(
        QStringLiteral("(?:^|[-_ ])(\\d+(?:[.,]\\d+)?)\\s*$"));
    for (const QString& from : {part.mfrPn, part.description, part.internalPn}) {
        const QString s = from.trimmed();
        if (s.isEmpty()) continue;
        QRegularExpressionMatch m = tail.match(s);
        if (m.hasMatch()) return m.captured(1).simplified();
        m = tailNum.match(s);
        if (m.hasMatch()) return m.captured(1).simplified();
    }
    // Failing that, a size parameter if the part carries one.
    for (const char* key : {"size", "nominalSize", "shellSize"}) {
        const QString v = part.value(QLatin1String(key));
        if (!v.trimmed().isEmpty()) return v.trimmed();
    }
    return {};
}

namespace {

// The figures of the table row that belongs to `sizeHint`, matched to fields by the headings above it.
std::vector<PulledField> tableFigures(const QString& text, PartKind kind, const QString& sizeHint,
                                      const std::vector<PartField>& fields) {
    std::vector<PulledField> out;
    const std::vector<Column>& columns = columnsFor(kind);
    const double want = sizeAsNumber(sizeHint);
    if (columns.empty() || std::isnan(want)) return out;

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        // Either the line starts with the size as written (a tightly set table, where the cells run
        // together) or its leading number IS that size (a table with space between the columns).
        int used = rowStartsWithSize(lines.at(i), sizeHint);
        if (used < 0) {
            const double got = leadingSize(lines.at(i), &used);
            if (std::isnan(got) || std::abs(got - want) > 1e-9) continue;
        }
        // The rest of the line is the row's figures. A row of a size table has at least one.
        const std::vector<double> figures = figuresOf(lines.at(i).mid(used));
        if (figures.empty()) continue;

        // What the columns are, from the headings above this row - ordered by WHERE ACROSS THE PAGE
        // each heading sits, not by which line it is on.
        //
        // A table heading is written over several lines, and the words of the third column are often
        // typeset above the words of the first:
        //
        //     Size    Minimum            Maximum            Recovered Wall Thickness
        //             Inside Diameter    Inside Diameter
        //             as Supplied        After Recovery
        //
        // Read line by line that says wall, then as-supplied, then after-recovery - which is not the
        // order of the columns, and would put every figure in the wrong field. Read by how far across
        // the line each heading starts it says as-supplied (13), after-recovery (34), wall (50), which
        // is the order the figures in the row are in.
        const int from = std::max(0, i - 24);
        std::vector<std::pair<int, const Column*>> found;
        for (const Column& c : columns) {
            for (int k = from; k < i; ++k) {
                const int at = lines.at(k).toLower().indexOf(QLatin1String(c.keyword));
                if (at < 0) continue;
                found.push_back({at, &c});
                break;   // the first line it appears on is the one to place it by
            }
        }
        if (found.empty()) continue;
        std::sort(found.begin(), found.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        // As many headings as figures, or the row cannot be read with any confidence: a table whose
        // columns cannot be told apart is not worth guessing at, and a wrong entry size is worse than
        // an empty one.
        if (found.size() != figures.size()) continue;

        for (size_t c = 0; c < found.size(); ++c) {
            const QString key = QString::fromLatin1(found[c].second->key);
            // A Range field's halves are "<key>Min" and "<key>Max"; the field itself is the stem.
            QString stem = key;
            QString half;
            if (stem.endsWith(QLatin1String("Min")) || stem.endsWith(QLatin1String("Max"))) {
                half = stem.right(3);
                stem.chop(3);
            }
            const PartField* field = nullptr;
            for (const PartField& f : fields)
                if (f.key == stem) field = &f;
            if (!field) continue;
            PulledField p;
            p.key = key;
            p.label = half.isEmpty()
                          ? field->label
                          : QStringLiteral("%1 (%2)").arg(field->label,
                                                          half == QLatin1String("Min")
                                                              ? QStringLiteral("smallest")
                                                              : QStringLiteral("largest"));
            p.value = tidy(figures[c]);
            p.unit = field->unit;
            p.evidence = QStringLiteral("the %1 row: %2")
                             .arg(sizeHint, lines.at(i).simplified().left(90));
            out.push_back(p);
        }
        break;   // the first row for this size is the one
    }
    return out;
}

// The line a match sits on, so the figure can be judged rather than taken on trust.
QString lineAround(const QString& text, int at) {
    const int start = text.lastIndexOf(QLatin1Char('\n'), at) + 1;
    int end = text.indexOf(QLatin1Char('\n'), at);
    if (end < 0) end = text.size();
    return text.mid(start, end - start).simplified();
}

} // namespace

std::vector<PulledField> pullTechnicalFields(const QString& text, PartKind kind,
                                             const QString& sizeHint) {
    std::vector<PulledField> out;
    if (text.trimmed().isEmpty()) return out;
    // Only the fields this kind actually has: a figure that has nowhere to go is not worth finding.
    const std::vector<PartField> fields = partFields(kind);
    auto fieldFor = [&fields](const QString& key) -> const PartField* {
        for (const PartField& f : fields)
            if (f.key == key) return &f;
        return nullptr;
    };

    for (const Pattern& pat : patternsFor(kind)) {
        const PartField* field = fieldFor(QString::fromLatin1(pat.key));
        if (!field) continue;   // the kind has no such field
        // fromUtf8, not fromLatin1: these patterns carry ± and Ω, and decoding UTF-8 as Latin-1
        // turns "±?" into "Â" followed by an optional "±" - a pattern that can never match.
        QRegularExpression re(QString::fromUtf8(pat.regex),
                              QRegularExpression::CaseInsensitiveOption);
        if (!re.isValid()) continue;
        const QRegularExpressionMatch m = re.match(text);
        if (!m.hasMatch()) continue;

        // A figure that is not a number: taken as it is written, which is what its field holds.
        if (pat.take != Pattern::Take::Number) {
            PulledField p;
            p.key = field->key;
            p.label = field->label;
            p.value = pat.take == Pattern::Take::Range
                          ? QStringLiteral("%1 to %2").arg(m.captured(1).simplified(),
                                                           m.captured(2).simplified())
                          : m.captured(1).simplified();
            // A range written the wrong way round ("+150 to -75") is put right rather than stored as
            // nonsense.
            if (pat.take == Pattern::Take::Range) {
                const double lo = m.captured(1).toDouble(), hi = m.captured(2).toDouble();
                if (lo > hi)
                    p.value = QStringLiteral("%1 to %2").arg(m.captured(2).simplified(),
                                                             m.captured(1).simplified());
            }
            if (p.value.isEmpty()) continue;
            p.unit = field->unit.isEmpty() ? QString::fromUtf8(pat.unit) : field->unit;
            p.evidence = lineAround(text, m.capturedStart());
            out.push_back(p);
            continue;
        }

        QString numText = m.captured(1);
        numText.replace(QLatin1Char(','), QLatin1Char('.'));
        bool ok = false;
        double value = numText.toDouble(&ok);
        if (!ok) continue;
        if (pat.prefixed) value = applyPrefix(value, m.captured(2));
        // The field's own unit decides how the figure is written: a capacitance field in uF wants
        // microfarads, not farads.
        const QString unit = QString::fromUtf8(pat.unit);
        if (unit == QLatin1String("uF")) value *= 1e6;        // stored in microfarads
        else if (unit == QLatin1String("uH")) value *= 1e6;   // in microhenries
        else if (unit == QLatin1String("MHz")) value /= 1e6;  // in megahertz

        PulledField p;
        p.key = field->key;
        p.label = field->label;
        p.value = tidy(value);
        p.unit = field->unit.isEmpty() ? unit : field->unit;
        p.evidence = lineAround(text, m.capturedStart());
        out.push_back(p);
    }

    // And the per-size figures, from the table row this part's size names. These are added last and
    // never replace a figure already found in running text, which is the more explicit of the two.
    for (const PulledField& fromTable : tableFigures(text, kind, sizeHint, fields)) {
        bool already = false;
        for (const PulledField& had : out)
            if (had.key == fromTable.key) already = true;
        if (!already) out.push_back(fromTable);
    }
    return out;
}

} // namespace Loopwire
