#include "harness/CableSpec.h"

#include "harness/Harness.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>

namespace Edraw_Desktop {

namespace {

struct KindRow {
    CableElementKind kind;
    const char* key;
    const char* name;
    int conductors;
};

const std::vector<KindRow>& kinds() {
    static const std::vector<KindRow> rows = {
        {CableElementKind::Single, "single", "Single", 1},
        {CableElementKind::Pair, "pair", "Twisted pair", 2},
        {CableElementKind::Triple, "triple", "Triple", 3},
        {CableElementKind::Quad, "quad", "Quad", 4},
        // A coax's screen is a conductor: it carries the return, it is terminated, and in a harness it
        // lands on a pin. Counting it as one is what makes the conductor count come out right.
        {CableElementKind::Coax, "coax", "Coax (core + screen)", 2},
    };
    return rows;
}

} // namespace

QString cableElementKindName(CableElementKind kind) {
    for (const KindRow& r : kinds())
        if (r.kind == kind) return QString::fromLatin1(r.name);
    return QStringLiteral("Single");
}

QString cableElementKindKey(CableElementKind kind) {
    for (const KindRow& r : kinds())
        if (r.kind == kind) return QString::fromLatin1(r.key);
    return QStringLiteral("single");
}

CableElementKind cableElementKindFromKey(const QString& key) {
    for (const KindRow& r : kinds())
        if (key == QLatin1String(r.key)) return r.kind;
    return CableElementKind::Single;
}

const std::vector<CableElementKind>& allCableElementKinds() {
    static std::vector<CableElementKind> out = [] {
        std::vector<CableElementKind> v;
        for (const KindRow& r : kinds()) v.push_back(r.kind);
        return v;
    }();
    return out;
}

int cableConductorsPerElement(CableElementKind kind) {
    for (const KindRow& r : kinds())
        if (r.kind == kind) return r.conductors;
    return 1;
}

// ---- One element ---------------------------------------------------------------------------------

QString CableElement::summary() const {
    QStringList bits;
    const int n = std::max(1, count);
    bits << QStringLiteral("%1 × %2").arg(n).arg(cableElementKindName(kind).toLower());
    bits << wireGaugeLabel(gauge);
    QStringList screens;
    if (foil) screens << QStringLiteral("foil");
    if (braid) screens << QStringLiteral("braid");
    if (!screens.isEmpty())
        bits << QStringLiteral("(%1 screen)").arg(screens.join(QStringLiteral(" + ")));
    if (kind != CableElementKind::Single && kind != CableElementKind::Coax && !twisted)
        bits << QStringLiteral("(laid up, not twisted)");
    if (!colours.isEmpty()) bits << colours.join(QLatin1Char('/'));
    return bits.join(QLatin1Char(' '));
}

// ---- The cable -----------------------------------------------------------------------------------

int CableSpec::conductorCount() const {
    int n = 0;
    for (const CableElement& e : elements) n += e.conductors();
    if (!drainGauge.trimmed().isEmpty()) ++n;   // a drain wire is a conductor like any other
    return n;
}

bool CableSpec::screened() const {
    if (overallFoil || overallBraid) return true;
    for (const CableElement& e : elements)
        if (e.foil || e.braid || e.kind == CableElementKind::Coax) return true;
    return false;
}

QString CableSpec::summary() const {
    QStringList bits;
    for (const CableElement& e : elements) bits << e.summary();
    QStringList overall;
    if (overallFoil)
        overall << (foilCoverage > 0 ? QStringLiteral("foil %1%").arg(foilCoverage)
                                     : QStringLiteral("foil"));
    if (overallBraid)
        overall << (braidCoverage > 0 ? QStringLiteral("braid %1%").arg(braidCoverage)
                                      : QStringLiteral("braid"));
    if (!overall.isEmpty())
        bits << QStringLiteral("overall %1").arg(overall.join(QStringLiteral(" + ")));
    if (!drainGauge.trimmed().isEmpty())
        bits << QStringLiteral("drain %1").arg(wireGaugeLabel(drainGauge));
    if (odMm > 0.0) bits << QStringLiteral("%1 mm OD").arg(odMm, 0, 'f', 1);
    if (!jacketColour.isEmpty())
        bits << QStringLiteral("%1 jacket").arg(wireColorName(jacketColour).toLower());
    if (bits.isEmpty()) return QStringLiteral("nothing described yet");
    return bits.join(QStringLiteral(", "));
}

QJsonObject CableSpec::toJson() const {
    QJsonObject o;
    if (odMm > 0.0) o[QStringLiteral("od")] = odMm;
    if (!jacket.isEmpty()) o[QStringLiteral("jacket")] = jacket;
    if (!jacketColour.isEmpty()) o[QStringLiteral("jacketColour")] = jacketColour;
    if (!conductor.isEmpty()) o[QStringLiteral("conductor")] = conductor;
    if (overallFoil) o[QStringLiteral("foil")] = true;
    if (overallBraid) o[QStringLiteral("braid")] = true;
    if (foilCoverage > 0) o[QStringLiteral("foilCoverage")] = foilCoverage;
    if (braidCoverage > 0) o[QStringLiteral("coverage")] = braidCoverage;
    if (!drainGauge.isEmpty()) o[QStringLiteral("drain")] = drainGauge;
    if (colourStandard != CableColourStandard::HouseOrder)
        o[QStringLiteral("colourStandard")] = cableColourStandardKey(colourStandard);
    QJsonArray arr;
    for (const CableElement& e : elements) {
        QJsonObject eo;
        eo[QStringLiteral("kind")] = cableElementKindKey(e.kind);
        eo[QStringLiteral("count")] = e.count;
        eo[QStringLiteral("gauge")] = e.gauge;
        if (e.enteredAwg) eo[QStringLiteral("awg")] = true;
        if (e.twisted) eo[QStringLiteral("twisted")] = true;
        if (e.foil) eo[QStringLiteral("foil")] = true;
        if (e.braid) eo[QStringLiteral("braid")] = true;
        if (!e.note.isEmpty()) eo[QStringLiteral("note")] = e.note;
        if (!e.colours.isEmpty()) {
            QJsonArray cs;
            for (const QString& c : e.colours) cs.append(c);
            eo[QStringLiteral("colours")] = cs;
        }
        arr.append(eo);
    }
    if (!arr.isEmpty()) o[QStringLiteral("elements")] = arr;
    return o;
}

CableSpec CableSpec::fromJson(const QJsonObject& o) {
    CableSpec s;
    s.odMm = o.value(QStringLiteral("od")).toDouble(0.0);
    s.jacket = o.value(QStringLiteral("jacket")).toString();
    s.jacketColour = o.value(QStringLiteral("jacketColour")).toString();
    s.conductor = o.value(QStringLiteral("conductor")).toString();
    s.overallFoil = o.value(QStringLiteral("foil")).toBool(false);
    s.overallBraid = o.value(QStringLiteral("braid")).toBool(false);
    s.foilCoverage = o.value(QStringLiteral("foilCoverage")).toInt(0);
    s.braidCoverage = o.value(QStringLiteral("coverage")).toInt(0);
    s.drainGauge = o.value(QStringLiteral("drain")).toString();
    s.colourStandard =
        cableColourStandardFromKey(o.value(QStringLiteral("colourStandard")).toString());
    for (const auto& v : o.value(QStringLiteral("elements")).toArray()) {
        const QJsonObject eo = v.toObject();
        CableElement e;
        e.kind = cableElementKindFromKey(eo.value(QStringLiteral("kind")).toString());
        e.count = std::max(1, eo.value(QStringLiteral("count")).toInt(1));
        e.gauge = eo.value(QStringLiteral("gauge")).toString(QStringLiteral("0.5"));
        e.enteredAwg = eo.value(QStringLiteral("awg")).toBool(false);
        e.twisted = eo.value(QStringLiteral("twisted")).toBool(false);
        e.foil = eo.value(QStringLiteral("foil")).toBool(false);
        e.braid = eo.value(QStringLiteral("braid")).toBool(false);
        e.note = eo.value(QStringLiteral("note")).toString();
        for (const auto& c : eo.value(QStringLiteral("colours")).toArray())
            e.colours << c.toString();
        s.elements.push_back(e);
    }
    return s;
}

QString CableSpec::toCompact() const {
    if (elements.empty() && odMm <= 0.0 && jacket.isEmpty() && conductor.isEmpty() && !overallFoil &&
        !overallBraid)
        return {};
    return QString::fromUtf8(QJsonDocument(toJson()).toJson(QJsonDocument::Compact));
}

CableSpec CableSpec::fromCompact(const QString& text) {
    if (text.trimmed().isEmpty()) return {};
    const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
    return doc.isObject() ? fromJson(doc.object()) : CableSpec{};
}

// ---- Colours -------------------------------------------------------------------------------------

// ---- Colour standards ----------------------------------------------------------------------------

namespace {

struct StandardRow {
    CableColourStandard standard;
    const char* key;
    const char* name;
    const char* note;
};

const std::vector<StandardRow>& standards() {
    static const std::vector<StandardRow> rows = {
        {CableColourStandard::HouseOrder, "house", "House order",
         "The order this application walks when a cable follows no standard"},
        {CableColourStandard::Din47100, "din47100", "DIN 47100",
         "Multi-pair industrial and data cable: white/brown, green/yellow, grey/pink, blue/red…"},
        {CableColourStandard::Hd308S2, "hd308s2", "EN 50525 / HD 308 S2",
         "Installation cable cores: brown, black, grey, blue, and green-yellow for earth"},
        {CableColourStandard::MilStd681, "mil-std-681", "MIL-STD-681",
         "The ten-colour code where the colour IS the digit: 0 black … 9 white"},
    };
    return rows;
}

// DIN 47100, pair by pair. The table runs to 44 pairs; these are the first twelve, which is as far as
// any harness in practice goes, and past them it starts again - said plainly rather than invented.
const char* const kDin47100[][2] = {
    {"WH", "BN"}, {"GN", "YE"}, {"GY", "PK"}, {"BU", "RD"}, {"BK", "VT"}, {"GY", "RD"},
    {"WH", "GN"}, {"BN", "GN"}, {"WH", "YE"}, {"YE", "BN"}, {"WH", "GY"}, {"GY", "BN"},
};

// MIL-STD-681: the colour is the digit. 0 black through 9 white.
const char* const kMil681[10] = {"BK", "BN", "RD", "OG", "YE", "GN", "BU", "VT", "GY", "WH"};

} // namespace

QString cableColourStandardName(CableColourStandard s) {
    for (const StandardRow& r : standards())
        if (r.standard == s) return QString::fromLatin1(r.name);
    return QStringLiteral("House order");
}

QString cableColourStandardKey(CableColourStandard s) {
    for (const StandardRow& r : standards())
        if (r.standard == s) return QString::fromLatin1(r.key);
    return QStringLiteral("house");
}

CableColourStandard cableColourStandardFromKey(const QString& key) {
    for (const StandardRow& r : standards())
        if (key == QLatin1String(r.key)) return r.standard;
    return CableColourStandard::HouseOrder;
}

const std::vector<CableColourStandard>& allCableColourStandards() {
    static std::vector<CableColourStandard> out = [] {
        std::vector<CableColourStandard> v;
        for (const StandardRow& r : standards()) v.push_back(r.standard);
        return v;
    }();
    return out;
}

QString cableColourStandardNote(CableColourStandard s) {
    for (const StandardRow& r : standards())
        if (r.standard == s) return QString::fromLatin1(r.note);
    return {};
}

QStringList cableColoursFor(CableColourStandard standard, CableElementKind kind, int index) {
    const int per = cableConductorsPerElement(kind);
    const int n = std::max(0, index);
    switch (standard) {
    case CableColourStandard::Din47100: {
        // The table is written in PAIRS, which is what DIN 47100 is: a pair takes a row of it, and a
        // group of three or four takes the next row's colours after its own.
        const int rows = static_cast<int>(sizeof(kDin47100) / sizeof(kDin47100[0]));
        QStringList out;
        for (int c = 0; c < per; ++c) {
            const int flat = n * per + c;            // the conductor's place in the whole run
            const int row = (flat / 2) % rows;
            out << QString::fromLatin1(kDin47100[row][flat % 2]);
        }
        // A coax's second conductor is its screen, not a colour.
        if (kind == CableElementKind::Coax && out.size() == 2) out[1] = QStringLiteral("SH");
        return out;
    }
    case CableColourStandard::Hd308S2: {
        // Core identification, which is stated by how many cores there are rather than by a running
        // table: two are brown and blue, three brown, black and grey, and the earth is green-yellow.
        if (kind == CableElementKind::Coax) return {QStringLiteral("BN"), QStringLiteral("SH")};
        static const char* const two[] = {"BN", "BU"};
        static const char* const three[] = {"BN", "BK", "GY"};
        static const char* const four[] = {"BN", "BK", "GY", "BU"};
        QStringList out;
        if (per == 1) {
            // A run of singles walks the same order, with the earth colour kept for an earth.
            static const char* const singles[] = {"BN", "BK", "GY", "BU", "WH", "RD"};
            out << QString::fromLatin1(singles[n % 6]);
        } else if (per == 2) {
            for (const char* c : two) out << QString::fromLatin1(c);
        } else if (per == 3) {
            for (const char* c : three) out << QString::fromLatin1(c);
        } else {
            for (const char* c : four) out << QString::fromLatin1(c);
        }
        return out;
    }
    case CableColourStandard::MilStd681: {
        // The colour is the digit, so the conductors are numbered from one and coloured by that number.
        QStringList out;
        for (int c = 0; c < per; ++c) {
            const int number = n * per + c + 1;
            out << QString::fromLatin1(kMil681[number % 10]);
        }
        if (kind == CableElementKind::Coax && out.size() == 2) out[1] = QStringLiteral("SH");
        return out;
    }
    case CableColourStandard::HouseOrder:
    default:
        return defaultCableColours(kind, index);
    }
}

void applyCableColourStandard(CableSpec& spec, bool force) {
    // Counted per KIND, the way the defaults have always been: the second pair takes the second row of
    // the table whatever else the cable has in it.
    int seen = 0;
    for (CableElement& e : spec.elements) {
        for (int copy = 0; copy < std::max(1, e.count); ++copy) {
            const QStringList want = cableColoursFor(spec.colourStandard, e.kind, seen++);
            if (copy > 0) continue;   // the element's colours are one set, however many of it there are
            if (force || e.colours.isEmpty()) {
                e.colours = want;
                continue;
            }
            // Fill in only what has not been chosen, so a hand-set colour survives.
            while (e.colours.size() < want.size()) e.colours << QString();
            for (int c = 0; c < want.size(); ++c)
                if (e.colours.at(c).isEmpty()) e.colours[c] = want.at(c);
        }
    }
}

QStringList defaultCableColours(CableElementKind kind, int index) {
    // The pair colours a multi-pair cable actually uses: white/blue, white/orange, white/green - the
    // telecom convention, which is what a data cable is coloured to.
    static const char* kPairMates[] = {"BU", "OG", "GN", "BN", "GY", "RD", "BK", "YE", "VT", "PK"};
    static const char* kSingles[] = {"WH", "BK", "RD", "GN", "YE", "BU", "BN", "OG", "VT", "GY",
                                     "PK", "TQ"};
    const int n = index >= 0 ? index : 0;
    switch (kind) {
    case CableElementKind::Pair:
        return {QStringLiteral("WH"),
                QString::fromLatin1(kPairMates[n % 10])};
    case CableElementKind::Triple:
        return {QStringLiteral("WH"), QString::fromLatin1(kPairMates[n % 10]),
                QStringLiteral("BK")};
    case CableElementKind::Quad:
        return {QStringLiteral("WH"), QString::fromLatin1(kPairMates[n % 10]),
                QStringLiteral("BK"), QStringLiteral("RD")};
    case CableElementKind::Coax:
        // A coax's core is not coloured in the ordinary sense; its screen is the braid.
        return {QStringLiteral("WH"), QStringLiteral("SH")};
    case CableElementKind::Single:
    default:
        return {QString::fromLatin1(kSingles[n % 12])};
    }
}

// ---- Expanding a cable into the conductors a harness draws ---------------------------------------

std::vector<CableConductor> cableConductors(const CableSpec& spec, const QString& bundleName) {
    std::vector<CableConductor> out;
    const QString bundle =
        bundleName.trimmed().isEmpty() ? QStringLiteral("cable") : bundleName.trimmed();
    int elementNo = 0;
    int sameKindSeen = 0;   // how many elements of this kind so far, for the default colours
    for (const CableElement& e : spec.elements) {
        const int perElement = cableConductorsPerElement(e.kind);
        for (int copy = 0; copy < std::max(1, e.count); ++copy) {
            ++elementNo;
            const int colourIndex = sameKindSeen++;
            const QStringList colours =
                e.colours.isEmpty() ? cableColoursFor(spec.colourStandard, e.kind, colourIndex)
                                    : e.colours;
            // A group needs its own key so the harness draws it as a group: twisted together,
            // screened together. The keys carry the cable's name so two cables in one harness never
            // share a twist or a screen.
            const QString group = QStringLiteral("%1-%2").arg(bundle).arg(elementNo);
            const bool twisted = e.twisted && perElement > 1;
            const bool screened = e.foil || e.braid;
            for (int c = 0; c < perElement; ++c) {
                CableConductor cc;
                cc.element = elementNo;
                cc.gauge = e.gauge;
                cc.bundle = bundle;
                cc.colour = c < colours.size() ? colours.at(c) : QString();
                if (e.kind == CableElementKind::Coax) {
                    // A coax is TWO rows in the builder - the core and the braid - and it is drawn as
                    // a coax rather than as two wires, which is what its "coax" kind says.
                    cc.kind = QStringLiteral("coax");
                    cc.isScreen = (c == 1);
                    cc.label = c == 0 ? QStringLiteral("%1 core").arg(group)
                                      : QStringLiteral("%1 screen").arg(group);
                    if (c == 1) cc.colour = QStringLiteral("SH");
                } else {
                    cc.kind = QStringLiteral("wire");
                    cc.label = perElement > 1
                                   ? QStringLiteral("%1-%2").arg(group).arg(c + 1)
                                   : group;
                    if (twisted) cc.twistGroup = group;
                    if (screened) cc.shieldGroup = group;
                }
                out.push_back(cc);
            }
            // A screened element that is not a coax needs its screen terminated somewhere, and that
            // is what the group's own shield key is for; the drain wire below is the conductor that
            // does it when the cable has one per element.
        }
        (void)perElement;
    }
    // An overall screen is one more thing to terminate: it belongs to the whole cable, so its group
    // is the cable itself.
    if (spec.overallFoil || spec.overallBraid) {
        for (CableConductor& cc : out)
            if (cc.shieldGroup.isEmpty() && cc.kind != QLatin1String("coax"))
                cc.shieldGroup = bundle;   // everything inside the overall screen is in that screen
    }
    if (!spec.drainGauge.trimmed().isEmpty()) {
        CableConductor drain;
        drain.gauge = spec.drainGauge.trimmed();
        drain.colour = QStringLiteral("SH");
        drain.kind = QStringLiteral("wire");
        drain.bundle = bundle;
        drain.label = QStringLiteral("%1 drain").arg(bundle);
        drain.isScreen = true;
        drain.shieldGroup = bundle;
        drain.element = 0;
        out.push_back(drain);
    }
    return out;
}

// ---- Where a part keeps it -----------------------------------------------------------------------

CableSpec cableSpecOf(const LibraryPart& part) {
    // Its own key first. Failing that, the key the make-up used to be kept under - a part described
    // before the Structure field existed had its JSON in "structure", and reading it from there is what
    // makes those parts still work.
    const QString mine = part.value(QLatin1String(kCableMakeupKey));
    if (!mine.trimmed().isEmpty()) return CableSpec::fromCompact(mine);
    const QString old = part.value(QLatin1String(kCableStructureKey)).trimmed();
    if (old.startsWith(QLatin1Char('{'))) return CableSpec::fromCompact(old);
    // Otherwise "structure" holds what it says it holds: the shorthand, which can be read.
    return old.isEmpty() ? CableSpec{} : cableSpecFromText(old);
}

void setCableSpecOn(LibraryPart& part, const CableSpec& spec) {
    part.params[QLatin1String(kCableMakeupKey)] = spec.toCompact();
    // The shorthand is written from the make-up, so the field always says what the cable IS rather
    // than what somebody typed before changing it.
    part.params[QLatin1String(kCableStructureKey)] = cableSpecToText(spec);
}

// ---- The make-up written as one line -------------------------------------------------------------

namespace {

// An AWG size as the mm² figure the application stores. The standard list is the authority: whichever
// of its entries is CALLED that AWG size is the one meant, so the two notations can never disagree.
QString mm2FromAwg(const QString& awg) {
    const QString want = awg.trimmed();
    for (const QString& mm2 : wireGaugeChoices())
        if (wireGaugeAwg(mm2) == want) return mm2;
    return {};
}

// A colour named in words ("black") or by its code ("BK") as the code.
QString colourCodeFromWord(const QString& word) {
    const QString w = word.trimmed();
    if (w.isEmpty()) return {};
    for (const QString& code : wireColorCodes()) {
        if (code.compare(w, Qt::CaseInsensitive) == 0) return code;
        if (wireColorName(code).compare(w, Qt::CaseInsensitive) == 0) return code;
    }
    return {};
}

// The group size a word means: "pair" is two, "triple" three, "quad" four.
int groupSizeFromWord(const QString& word) {
    const QString w = word.trimmed().toLower();
    if (w.startsWith(QLatin1String("pair")) || w == QLatin1String("pr") ||
        w == QLatin1String("prs") || w.startsWith(QLatin1String("twisted")))
        return 2;
    if (w.startsWith(QLatin1String("triple")) || w.startsWith(QLatin1String("triad"))) return 3;
    if (w.startsWith(QLatin1String("quad"))) return 4;
    if (w.startsWith(QLatin1String("single")) || w.startsWith(QLatin1String("core"))) return 1;
    return 0;
}

CableElementKind kindForGroupSize(int n) {
    switch (n) {
    case 2: return CableElementKind::Pair;
    case 3: return CableElementKind::Triple;
    case 4: return CableElementKind::Quad;
    default: return CableElementKind::Single;
    }
}

} // namespace

CableSpec cableSpecFromText(const QString& text, QStringList* notes) {
    CableSpec s;
    auto note = [notes](const QString& what) {
        if (notes) *notes << what;
    };
    if (text.trimmed().isEmpty()) return s;

    // The terms: separated by + or by a comma, either of which people use.
    const QStringList terms = text.split(QRegularExpression(QStringLiteral("[+,;]")),
                                        Qt::SkipEmptyParts);
    // NxMxGauge, with the screen prefix a data cable is sold under: FTP is foil, STP and SFTP braid.
    static const QRegularExpression numeric(
        QStringLiteral("^(?:(F/?U?TP|S/?F?TP|SF/UTP|F/UTP|U/?UTP|UTP)\\s+)?"
                       "(?:(\\d+)\\s*[x×]\\s*)?(?:(\\d+)\\s*[x×]\\s*)?"
                       "([\\d.]+)\\s*(AWG|awg|mm2|mm²|mm\\^2)?$"),
        QRegularExpression::CaseInsensitiveOption);
    // "2 pair 28AWG", "3 singles 0.5", "1 coax 0.22" - and "2x coax 0.22", because people write the
    // count with an x on it and so does cableSpecToText.
    static const QRegularExpression worded(
        QStringLiteral("^(?:(F/?U?TP|S/?F?TP|UTP)\\s+)?(\\d+)?\\s*[x×]?\\s*"
                       "(pairs?|prs?|twisted\\s+pairs?|triples?|triads?|quads?|singles?|cores?|coax(?:ial)?"
                       "(?:\\s+cable)?)\\s*(?:of\\s*)?([\\d.]+)?\\s*(AWG|awg|mm2|mm²)?$"),
        QRegularExpression::CaseInsensitiveOption);

    for (const QString& raw : terms) {
        const QString term = raw.trimmed();
        if (term.isEmpty()) continue;
        const QString low = term.toLower();

        // The things that belong to the whole cable rather than to one element.
        if (low == QLatin1String("foil") || low == QLatin1String("foil screen") ||
            low == QLatin1String("foiled") || low == QLatin1String("al/mylar")) {
            s.overallFoil = true;
            continue;
        }
        if (low == QLatin1String("braid") || low == QLatin1String("braided") ||
            low == QLatin1String("braid screen") || low == QLatin1String("screened") ||
            low == QLatin1String("shielded")) {
            s.overallBraid = true;
            continue;
        }
        if (low == QLatin1String("unscreened") || low == QLatin1String("unshielded")) continue;
        {
            // "foil 100%", "braid 85%".
            static const QRegularExpression covered(
                QStringLiteral("^(foil|braid)\\s+(\\d+)\\s*%?$"),
                QRegularExpression::CaseInsensitiveOption);
            const QRegularExpressionMatch m = covered.match(term);
            if (m.hasMatch()) {
                if (m.captured(1).compare(QLatin1String("foil"), Qt::CaseInsensitive) == 0) {
                    s.overallFoil = true;
                    s.foilCoverage = m.captured(2).toInt();
                } else {
                    s.overallBraid = true;
                    s.braidCoverage = m.captured(2).toInt();
                }
                continue;
            }
        }
        {
            // "drain 24AWG".
            static const QRegularExpression drain(
                QStringLiteral("^drain\\s*(?:wire)?\\s*([\\d.]+)\\s*(AWG|awg|mm2|mm²)?$"),
                QRegularExpression::CaseInsensitiveOption);
            const QRegularExpressionMatch m = drain.match(term);
            if (m.hasMatch()) {
                const bool awg = m.captured(2).startsWith(QLatin1String("AWG"), Qt::CaseInsensitive);
                s.drainGauge = awg ? mm2FromAwg(m.captured(1)) : m.captured(1);
                if (s.drainGauge.isEmpty()) s.drainGauge = m.captured(1);
                continue;
            }
        }
        // A jacket colour, named or coded.
        if (const QString code = colourCodeFromWord(term); !code.isEmpty()) {
            s.jacketColour = code;
            continue;
        }

        // An element, written in figures or in words.
        auto screenFromPrefix = [](const QString& prefix, bool& foil, bool& braid) {
            const QString p = prefix.toUpper().remove(QLatin1Char('/'));
            if (p.isEmpty() || p == QLatin1String("UTP") || p == QLatin1String("UUTP")) return;
            // An F means foil, an S means braid; SFTP has both.
            if (p.startsWith(QLatin1Char('S'))) braid = true;
            if (p.contains(QLatin1Char('F'))) foil = true;
        };

        QRegularExpressionMatch m = numeric.match(term);
        if (m.hasMatch()) {
            CableElement e;
            const bool awg = m.captured(5).startsWith(QLatin1String("AWG"), Qt::CaseInsensitive);
            const QString figure = m.captured(4);
            e.gauge = awg ? mm2FromAwg(figure) : figure;
            if (e.gauge.isEmpty()) {
                note(QStringLiteral("\"%1\": %2 is not a standard size, taken as it is")
                         .arg(term, figure));
                e.gauge = figure;
            }
            e.enteredAwg = awg;
            // Two counts means "so many groups of so many"; one means so many singles.
            const int a = m.captured(2).isEmpty() ? 0 : m.captured(2).toInt();
            const int b = m.captured(3).isEmpty() ? 0 : m.captured(3).toInt();
            int count = 1, group = 1;
            if (a > 0 && b > 0) { count = a; group = b; }
            else if (a > 0) { count = a; group = 1; }
            e.kind = kindForGroupSize(group);
            e.count = std::max(1, count);
            e.twisted = group > 1;
            screenFromPrefix(m.captured(1), e.foil, e.braid);
            // A twisted pair sold as FTP is screened per pair - that is what the F is about.
            s.elements.push_back(e);
            continue;
        }
        m = worded.match(term);
        if (m.hasMatch()) {
            CableElement e;
            const QString what = m.captured(3).toLower();
            const int count = m.captured(2).isEmpty() ? 1 : m.captured(2).toInt();
            if (what.startsWith(QLatin1String("coax"))) {
                e.kind = CableElementKind::Coax;
            } else {
                const int group = groupSizeFromWord(what);
                e.kind = kindForGroupSize(group > 0 ? group : 1);
                e.twisted = group > 1;
            }
            e.count = std::max(1, count);
            const bool awg = m.captured(5).startsWith(QLatin1String("AWG"), Qt::CaseInsensitive);
            const QString figure = m.captured(4);
            if (!figure.isEmpty()) {
                e.gauge = awg ? mm2FromAwg(figure) : figure;
                if (e.gauge.isEmpty()) e.gauge = figure;
                e.enteredAwg = awg;
            }
            screenFromPrefix(m.captured(1), e.foil, e.braid);
            s.elements.push_back(e);
            continue;
        }
        // A bare "coax", with no count and no size.
        if (low.startsWith(QLatin1String("coax"))) {
            CableElement e;
            e.kind = CableElementKind::Coax;
            e.count = 1;
            s.elements.push_back(e);
            continue;
        }
        note(QStringLiteral("\"%1\" was not understood").arg(term));
    }
    return s;
}

QString cableSpecToText(const CableSpec& spec) {
    QStringList terms;
    for (const CableElement& e : spec.elements) {
        const int per = cableConductorsPerElement(e.kind);
        QString prefix;
        if (e.kind != CableElementKind::Coax && per > 1) {
            if (e.foil && e.braid) prefix = QStringLiteral("SFTP ");
            else if (e.braid) prefix = QStringLiteral("STP ");
            else if (e.foil) prefix = QStringLiteral("FTP ");
            else if (e.twisted) prefix = QStringLiteral("UTP ");
        }
        const QString size = e.gauge.isEmpty()
                                 ? QString()
                                 : (e.enteredAwg ? QStringLiteral("%1AWG").arg(wireGaugeAwg(e.gauge))
                                                 : QStringLiteral("%1mm2").arg(e.gauge));
        if (e.kind == CableElementKind::Coax) {
            terms << (size.isEmpty() ? QStringLiteral("%1x coax").arg(e.count)
                                     : QStringLiteral("%1x coax %2").arg(e.count).arg(size));
            continue;
        }
        if (per > 1)
            terms << QStringLiteral("%1%2x%3x%4").arg(prefix).arg(e.count).arg(per).arg(size);
        else
            terms << QStringLiteral("%1x%2").arg(e.count).arg(size);
        // A screened element that is not twisted still has to say so.
        if (per == 1 && (e.foil || e.braid))
            terms.back() += e.foil && e.braid ? QStringLiteral(" (foil+braid)")
                          : e.foil            ? QStringLiteral(" (foil)")
                                              : QStringLiteral(" (braid)");
    }
    QString out = terms.join(QStringLiteral(" + "));
    QStringList tail;
    if (spec.overallFoil)
        tail << (spec.foilCoverage > 0 ? QStringLiteral("foil %1%").arg(spec.foilCoverage)
                                       : QStringLiteral("foil"));
    if (spec.overallBraid)
        tail << (spec.braidCoverage > 0 ? QStringLiteral("braid %1%").arg(spec.braidCoverage)
                                        : QStringLiteral("braid"));
    if (!spec.drainGauge.isEmpty())
        tail << QStringLiteral("drain %1mm2").arg(spec.drainGauge);
    if (!spec.jacketColour.isEmpty()) tail << wireColorName(spec.jacketColour).toLower();
    if (!tail.isEmpty()) out += (out.isEmpty() ? QString() : QStringLiteral(", ")) +
                                tail.join(QStringLiteral(", "));
    return out;
}

// ---- The cross-section ---------------------------------------------------------------------------

namespace {

Entity circle(const Vec2& at, double r, const QString& colour = QString(), bool dashed = false) {
    Entity e;
    e.type = EntityType::Circle;
    e.center = at;
    e.radius = r;
    if (!colour.isEmpty()) e.color = colour;
    if (dashed) e.meta.insert(QStringLiteral("lineType"), QStringLiteral("dashed"));
    return e;
}

Entity label(const Vec2& at, const QString& text, double height) {
    Entity e;
    e.type = EntityType::Text;
    e.a = at;
    e.meta.insert(QStringLiteral("text"), text);
    e.meta.insert(QStringLiteral("height"), height);
    e.meta.insert(QStringLiteral("size"), height);
    return e;
}

} // namespace

std::vector<Entity> cableCrossSection(const CableSpec& spec, double diameterMm) {
    std::vector<Entity> out;
    const double od = diameterMm > 0.1 ? diameterMm : (spec.odMm > 0.1 ? spec.odMm : 10.0);
    const double R = od / 2.0;
    const Vec2 c{0.0, 0.0};

    // The jacket, in its own colour when it has one, then the screens inside it, innermost last -
    // the order a cable is built in.
    out.push_back(circle(c, R, spec.jacketColour.isEmpty()
                                  ? QString()
                                  : wireColorHex(spec.jacketColour)));
    double inner = R * 0.93;
    if (spec.overallBraid) {
        // A braid is drawn as two close circles with a cross-hatch between them in a real drawing;
        // here, two circles and a note, which reads correctly at this scale.
        out.push_back(circle(c, inner, QStringLiteral("#c8b273")));
        inner -= R * 0.05;
        out.push_back(circle(c, inner, QStringLiteral("#c8b273")));
        inner -= R * 0.02;
    }
    if (spec.overallFoil) {
        out.push_back(circle(c, inner, QStringLiteral("#9fb4c8"), true));
        inner -= R * 0.04;
    }

    // Every element as one circle of its own, laid up: one in the middle when there is only one, and
    // a ring of them otherwise, which is how a cable is actually made up.
    struct Placed {
        const CableElement* e;
        int copy;
    };
    std::vector<Placed> placed;
    for (const CableElement& e : spec.elements)
        for (int i = 0; i < std::max(1, e.count); ++i) placed.push_back({&e, i});
    if (placed.empty()) return out;

    const int n = static_cast<int>(placed.size());
    // How big each element can be: on a ring of n, the centres are 2·r_ring·sin(pi/n) apart, so the
    // element radius is half of that. One element sits in the middle and takes the lot.
    const double ringR = n == 1 ? 0.0 : inner * 0.55;
    const double elemR = n == 1 ? inner * 0.72
                                : std::min(inner - ringR, ringR * std::sin(M_PI / n)) * 0.95;
    for (int i = 0; i < n; ++i) {
        const double a = (n == 1) ? 0.0 : (2.0 * M_PI * i / n - M_PI / 2.0);
        const Vec2 at{c.x + ringR * std::cos(a), c.y + ringR * std::sin(a)};
        const CableElement& e = *placed[static_cast<size_t>(i)].e;
        const int per = cableConductorsPerElement(e.kind);
        // The element's own screen, drawn round its conductors.
        if (e.foil) out.push_back(circle(at, elemR, QStringLiteral("#9fb4c8"), true));
        if (e.braid) out.push_back(circle(at, elemR * 0.96, QStringLiteral("#c8b273")));
        if (e.kind == CableElementKind::Coax) {
            // A coax: the braid, the dielectric, the core.
            out.push_back(circle(at, elemR * 0.92, QStringLiteral("#c8b273")));
            out.push_back(circle(at, elemR * 0.62, QStringLiteral("#7f8896")));
            out.push_back(circle(at, elemR * 0.26, QStringLiteral("#e0c169")));
            continue;
        }
        // The conductors of the element, on their own little ring.
        const double condRing = per == 1 ? 0.0 : elemR * 0.45;
        const double condR = per == 1 ? elemR * 0.55
                                      : std::min(elemR - condRing,
                                                 condRing * std::sin(M_PI / per)) * 0.9;
        const QStringList colours =
            e.colours.isEmpty() ? defaultCableColours(e.kind, i) : e.colours;
        for (int k = 0; k < per; ++k) {
            const double ca = (per == 1) ? 0.0 : (2.0 * M_PI * k / per - M_PI / 2.0);
            const Vec2 cat{at.x + condRing * std::cos(ca), at.y + condRing * std::sin(ca)};
            out.push_back(circle(cat, condR, wireColorHex(k < colours.size() ? colours.at(k)
                                                                            : QString(),
                                                          QStringLiteral("#d7dade"))));
        }
    }
    // No caption: the make-up belongs in the part's Structure field, where it can be read, searched
    // and corrected. Written into the DRAWING it would be one more copy of the same fact, out of date
    // the moment the make-up changed, and stuck to whatever sheet the drawing was dropped on.
    return out;
}

// ---- The schematic -------------------------------------------------------------------------------

namespace {

Entity line(const Vec2& a, const Vec2& b, const QString& colour = QString(), bool dashed = false) {
    Entity e;
    e.type = EntityType::Line;
    e.a = a;
    e.b = b;
    if (!colour.isEmpty()) e.color = colour;
    if (dashed) e.meta.insert(QStringLiteral("lineType"), QStringLiteral("dashed"));
    return e;
}

// The twist sign: the two conductors crossing over, drawn where it is put rather than derived from the
// wires - a part's drawing has to stand on its own, with nothing else on the sheet for it to refer to.
void addTwistSign(std::vector<Entity>& out, double x, double yTop, double yBot, double width,
                  const QString& topColour, const QString& botColour) {
    const double half = width / 2.0;
    // One crossing each way: the top conductor goes down and the bottom one comes up, which is what a
    // twist is and what the symbol has always meant.
    out.push_back(line({x - half, yTop}, {x + half, yBot}, topColour));
    out.push_back(line({x - half, yBot}, {x + half, yTop}, botColour));
}

// The screen sign at one end of a group: a dashed bracket around the conductors with a drain leg
// leaving it, which is how a screen and its termination are drawn on a wiring diagram.
void addScreenSign(std::vector<Entity>& out, double x, double yTop, double yBot, double reach,
                   bool toTheRight) {
    const QString ink = QStringLiteral("#c8b273");
    const double pad = 1.6;
    const double dir = toTheRight ? 1.0 : -1.0;
    const double xIn = x;
    const double xOut = x + dir * reach;
    // The bracket: along the outside of the group and back in at both ends.
    out.push_back(line({xIn, yTop + pad}, {xOut, yTop + pad}, ink, true));
    out.push_back(line({xOut, yTop + pad}, {xOut, yBot - pad}, ink, true));
    out.push_back(line({xOut, yBot - pad}, {xIn, yBot - pad}, ink, true));
    // The drain: out of the middle of the screen, to be landed on something.
    const double yMid = (yTop + yBot) / 2.0;
    out.push_back(line({xOut, yMid}, {xOut + dir * reach * 0.9, yMid}, ink));
}

} // namespace

std::vector<Entity> cableSchematic(const CableSpec& spec, double lengthMm) {
    std::vector<Entity> out;
    if (spec.elements.empty()) return out;
    const double L = std::max(30.0, lengthMm);
    const double pitch = 6.0;            // between conductors
    const double groupGap = 4.0;         // extra between one element and the next
    const double x0 = 0.0, x1 = L;
    const double signWidth = 7.0;
    const double signIn = 14.0;          // how far in from each end the signs sit

    double y = 0.0;
    int elementNo = 0;
    int sameKindSeen = 0;
    double lowest = 0.0;

    for (const CableElement& e : spec.elements) {
        const int per = cableConductorsPerElement(e.kind);
        for (int copy = 0; copy < std::max(1, e.count); ++copy) {
            ++elementNo;
            const QStringList colours =
                e.colours.isEmpty() ? cableColoursFor(spec.colourStandard, e.kind, sameKindSeen++)
                                    : e.colours;
            const double yFirst = y;

            if (e.kind == CableElementKind::Coax) {
                // A coax, drawn as one: the core, and its screen carried alongside and closed at both
                // ends - the same thing the cross-section shows, seen from the side.
                const double yCore = y;
                const double yScreen = y - pitch * 0.8;
                out.push_back(line({x0, yCore}, {x1, yCore},
                                   wireColorHex(colours.isEmpty() ? QString() : colours.front(),
                                                QStringLiteral("#e0c169"))));
                const QString braid = QStringLiteral("#c8b273");
                out.push_back(line({x0 + signIn * 0.5, yScreen}, {x1 - signIn * 0.5, yScreen}, braid,
                                   true));
                // Closed onto the core at both ends, so it reads as a screen around it and not as a
                // second wire running beside it.
                out.push_back(line({x0 + signIn * 0.5, yScreen}, {x0 + signIn * 0.5, yCore}, braid,
                                   true));
                out.push_back(line({x1 - signIn * 0.5, yScreen}, {x1 - signIn * 0.5, yCore}, braid,
                                   true));
                out.push_back(label({x1 + 2.0, yCore + 1.2}, QStringLiteral("coax"), 2.4));
                y = yScreen - pitch - groupGap;
                lowest = std::min(lowest, yScreen);
                continue;
            }

            // The conductors themselves, one line each, in their own colours.
            for (int c = 0; c < per; ++c) {
                const double yc = y - c * pitch;
                const QString code = c < colours.size() ? colours.at(c) : QString();
                out.push_back(line({x0, yc}, {x1, yc},
                                   wireColorHex(code, QStringLiteral("#d7dade"))));
                if (!code.isEmpty()) out.push_back(label({x1 + 2.0, yc + 1.2}, code, 2.4));
                lowest = std::min(lowest, yc);
            }
            const double yLast = y - (per - 1) * pitch;

            // Twisted: the sign at BOTH ends, because that is where a drawing shows it and where the
            // twist is actually made off.
            if (e.twisted && per > 1) {
                for (int c = 0; c + 1 < per; ++c) {
                    const QString a = c < colours.size() ? colours.at(c) : QString();
                    const QString b = c + 1 < colours.size() ? colours.at(c + 1) : QString();
                    addTwistSign(out, x0 + signIn, y - c * pitch, y - (c + 1) * pitch, signWidth,
                                 wireColorHex(a, QStringLiteral("#d7dade")),
                                 wireColorHex(b, QStringLiteral("#d7dade")));
                    addTwistSign(out, x1 - signIn, y - c * pitch, y - (c + 1) * pitch, signWidth,
                                 wireColorHex(a, QStringLiteral("#d7dade")),
                                 wireColorHex(b, QStringLiteral("#d7dade")));
                }
            }
            // Screened: the screen and its drain at both ends.
            if (e.foil || e.braid) {
                addScreenSign(out, x0 + signIn * 0.35, yFirst, yLast, 5.0, false);
                addScreenSign(out, x1 - signIn * 0.35, yFirst, yLast, 5.0, true);
            }
            y = yLast - pitch - groupGap;
        }
    }

    // A drain wire of the cable's own is a conductor and gets a line like any other.
    if (!spec.drainGauge.trimmed().isEmpty()) {
        out.push_back(line({x0, y}, {x1, y}, QStringLiteral("#c8b273"), true));
        out.push_back(label({x1 + 2.0, y + 1.2}, QStringLiteral("drain"), 2.4));
        lowest = std::min(lowest, y);
        y -= pitch;
    }

    // The overall screen: around everything, closed at both ends, with its own drain.
    if (spec.overallFoil || spec.overallBraid) {
        const QString ink = QStringLiteral("#c8b273");
        const double top = 4.0;
        const double bot = lowest - 4.0;
        const double inset = 6.0;
        out.push_back(line({x0 + inset, top}, {x1 - inset, top}, ink, true));
        out.push_back(line({x0 + inset, bot}, {x1 - inset, bot}, ink, true));
        out.push_back(line({x0 + inset, top}, {x0 + inset, bot}, ink, true));
        out.push_back(line({x1 - inset, top}, {x1 - inset, bot}, ink, true));
        // A drain at each end, which is where an overall screen is landed.
        const double yMid = (top + bot) / 2.0;
        out.push_back(line({x0 + inset, yMid}, {x0 + inset - 6.0, yMid}, ink));
        out.push_back(line({x1 - inset, yMid}, {x1 - inset + 6.0, yMid}, ink));
        out.push_back(label({x0 + inset + 1.0, top + 3.4},
                            spec.overallFoil && spec.overallBraid
                                ? QStringLiteral("overall foil + braid")
                                : spec.overallFoil ? QStringLiteral("overall foil")
                                                   : QStringLiteral("overall braid"),
                            2.4));
    }
    return out;
}

} // namespace Edraw_Desktop
