#include "harness/Harness.h"

#include "geometry/Geometry.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonValue>
#include <algorithm>
#include <cmath>
#include <array>
#include <vector>

namespace Loopwire {

namespace {
struct WireColor { const char* code; const char* name; const char* hex; };
const std::array<WireColor, 16> kWireColors = {{
    {"WH", "White", "#e8e8e8"},   {"BK", "Black", "#000000"},
    {"BN", "Brown", "#8B4513"},   {"RD", "Red", "#CC2222"},
    {"OG", "Orange", "#FF6600"},  {"YE", "Yellow", "#DDCC00"},
    {"GN", "Green", "#007700"},   {"BU", "Blue", "#0055CC"},
    {"VT", "Violet", "#7700CC"},  {"GY", "Grey", "#888888"},
    {"PK", "Pink", "#FF69B4"},    {"TQ", "Turquoise", "#00CED1"},
    {"GNYE", "Green/Yellow", "#99CC00"},
    // Not colours so much as kinds of conductor, offered in the same list because that is
    // where they are chosen. A coax has no colour: it has a SIGNAL and a SHIELD, and which of
    // the two a row is decides how it is drawn. A cable can carry a shield core as well.
    {"DRAIN", "Drain", "#b8b8b0"},
    {"SIGNAL", "Signal", "#e8e8e8"},
    {"SHIELD", "Shield", "#b8b8b0"},
}};

// User-defined colours, appended to the standard list. Kept as a plain vector so the order
// they were added in is the order they are offered in.
struct CustomColor { QString code, name, hex; };
std::vector<CustomColor>& customColors() {
    static std::vector<CustomColor> v;
    return v;
}
} // namespace

void registerCustomWireColor(const QString& code, const QString& name, const QString& hex) {
    const QString c = code.trimmed();
    if (c.isEmpty() || hex.trimmed().isEmpty()) return;
    // A standard code is never shadowed — the harness codes have to keep meaning what they
    // mean — and re-registering a custom one updates it in place instead of duplicating it.
    for (const auto& k : kWireColors)
        if (c == QLatin1String(k.code)) return;
    for (auto& e : customColors())
        if (e.code == c) {
            e.name = name.isEmpty() ? c : name;
            e.hex = hex.trimmed();
            return;
        }
    customColors().push_back({c, name.isEmpty() ? c : name, hex.trimmed()});
}

QStringList customWireColorCodes() {
    QStringList out;
    for (const auto& e : customColors()) out << e.code;
    return out;
}

namespace {
// A code can carry a NUMBER: one cable may hold several coaxes, and their conductors have to be
// told apart - SIGNAL1 / SHIELD1 / SIGNAL2 / SHIELD2. The number is not a colour, so it is
// split off before the code is looked up and put back when the code is named.
void splitCodeNumber(const QString& code, QString& base, QString& number) {
    base = code;
    number.clear();
    int i = code.size();
    while (i > 0 && code[i - 1].isDigit()) --i;
    // Only a code that is letters-then-digits, and only when some letters are left.
    if (i > 0 && i < code.size()) {
        base = code.left(i);
        number = code.mid(i);
    }
}
} // namespace

QString wireColorHex(const QString& code, const QString& fallback) {
    if (code.isEmpty() || code == "BYLAYER") return fallback;
    if (code.startsWith('#')) return code;
    for (const auto& c : kWireColors)
        if (code == QLatin1String(c.code)) return QString::fromLatin1(c.hex);
    for (const auto& e : customColors())
        if (code == e.code) return e.hex;
    // A numbered conductor of a multi-coax cable: the number does not change the colour.
    QString base, number;
    splitCodeNumber(code, base, number);
    if (!number.isEmpty() && base != code) return wireColorHex(base, fallback);
    return fallback;
}

QString wireColorName(const QString& code) {
    for (const auto& c : kWireColors)
        if (code == QLatin1String(c.code)) return QString::fromLatin1(c.name);
    for (const auto& e : customColors())
        if (code == e.code) return e.name;
    QString base, number;
    splitCodeNumber(code, base, number);
    if (!number.isEmpty() && base != code) {
        const QString name = wireColorName(base);
        if (name != base) return name + QStringLiteral(" ") + number; // "Signal 2"
    }
    return code;
}

QStringList wireColorCodes() {
    QStringList out;
    for (const auto& c : kWireColors) out << QString::fromLatin1(c.code);
    out << customWireColorCodes();
    return out;
}

namespace {
// The standard sizes, paired: a cross-section is stored as its mm2 figure and shown in
// whichever notation the drawing is set to.
struct GaugePair { const char* mm2; const char* awg; };
const GaugePair kGaugePairs[] = {
    {"0.03", "32"}, {"0.05", "30"}, {"0.08", "28"}, {"0.14", "26"}, {"0.25", "24"},
    {"0.34", "22"},
    {"0.38", "21"}, {"0.5", "20"},  {"0.75", "18"}, {"1.0", "16"},  {"1.5", "15"},
    {"2.5", "14"},  {"4.0", "12"},  {"6.0", "10"},  {"10.0", "8"},  {"16.0", "6"},
    {"25.0", "4"},  {"35.0", "2"},  {"50.0", "1"},
};
bool g_gaugeAwg = false;
} // namespace

void setWireGaugeNotationAwg(bool awg) { g_gaugeAwg = awg; }
bool wireGaugeNotationAwg() { return g_gaugeAwg; }

QString wireGaugeAwg(const QString& mm2) {
    for (const auto& g : kGaugePairs)
        if (mm2 == QLatin1String(g.mm2)) return QString::fromLatin1(g.awg);
    // Not one of the standard figures: fall back to the nearest by area, so a hand-typed
    // cross-section still reads as an AWG size rather than as nothing.
    bool ok = false;
    const double want = mm2.toDouble(&ok);
    if (!ok || want <= 0.0) return {};
    QString best;
    double bestDiff = 1e18;
    for (const auto& g : kGaugePairs) {
        const double diff = std::abs(QString::fromLatin1(g.mm2).toDouble() - want);
        if (diff < bestDiff) { bestDiff = diff; best = QString::fromLatin1(g.awg); }
    }
    return best;
}

QString wireGaugeLabel(const QString& mm2, bool withUnit) {
    if (mm2.isEmpty()) return {};
    if (g_gaugeAwg) {
        const QString awg = wireGaugeAwg(mm2);
        if (awg.isEmpty()) return mm2;
        return withUnit ? awg + QStringLiteral(" AWG") : awg;
    }
    return withUnit ? mm2 + QString::fromUtf8(" mm\xC2\xB2") : mm2;
}

QStringList wireGaugeChoices() {
    // 4 AWG to 32 AWG: the range a harness is actually built from, thin end first. The mm2
    // figure is what gets stored; wireGaugeLabel() decides how it reads.
    QStringList out;
    for (const auto& g : kGaugePairs) {
        const int awg = QString::fromLatin1(g.awg).toInt();
        if (awg >= 4 && awg <= 32) out << QString::fromLatin1(g.mm2);
    }
    return out;
}

double twistBandWidth(int repeats, double wireBand, double flatLen) {
    const double band = std::max(1.0, wireBand);
    // Width grows with the number of crossings and the flat length between them, so the braid
    // is not cramped when more twists / longer flats are asked for.
    return std::max(28.0, std::max(1, repeats) * (band * 0.6 + flatLen) + 16.0);
}

double wireGaugeWeight(const QString& gauge) {
    static const QHash<QString, double> w = {
        {"0.03", 0.7}, {"0.05", 0.8}, {"0.08", 0.9}, {"0.14", 1}, {"0.25", 1.1},
        {"0.34", 1.2},
        {"0.38", 1.2}, {"0.5", 1.5}, {"0.75", 1.8}, {"1.0", 2}, {"1.5", 2.5},
        {"2.5", 3}, {"4.0", 4}, {"6.0", 5}, {"10.0", 6}, {"16.0", 7},
        {"25.0", 8}, {"35.0", 9}, {"50.0", 10}};
    return w.value(gauge, 2.0);
}

// ---- Wire annotations ------------------------------------------------------

bool wireShowsColorName(const Entity& wire) {
    return wire.meta.value(QStringLiteral("showColorName")).toBool(false);
}

bool wireShowsGaugeName(const Entity& wire) {
    return wire.meta.value(QStringLiteral("showGaugeName")).toBool(false);
}

int wireAnnotCount(const Entity& wire) {
    if (!wireShowsColorName(wire) && !wireShowsGaugeName(wire)) return 0;
    return std::clamp(wire.meta.value(QStringLiteral("annCount")).toInt(1), 1, 20);
}

QString wireAnnotColorText(const Entity& wire) {
    if (!wireShowsColorName(wire)) return {};
    const QString code = wire.meta.value(QStringLiteral("wireColor")).toString();
    if (code.isEmpty()) return {};
    // "full" spells the colour out; the default is the 2-letter code used on harnesses.
    // Full name by default: "White" reads on a sheet where "WH" has to be looked up. A drawing
    // that asks for the 2-letter code says so.
    return wire.meta.value(QStringLiteral("colorNameMode")).toString(QStringLiteral("full")) ==
                   QLatin1String("code")
               ? code
               : wireColorName(code);
}

QString wireAnnotGaugeText(const Entity& wire) {
    if (!wireShowsGaugeName(wire)) return {};
    return wireGaugeLabel(wire.meta.value(QStringLiteral("wireGauge")).toString());
}

QString wireAnnotText(const Entity& wire) {
    const QString c = wireAnnotColorText(wire);
    const QString g = wireAnnotGaugeText(wire);
    if (c.isEmpty()) return g;
    if (g.isEmpty()) return c;
    return c + QStringLiteral("  ") + g; // one line: "RD  0.5 mm²"
}

double wireTextGap(const Entity& wire) {
    return std::max(0.0, wire.meta.value(QStringLiteral("textGap")).toDouble(0.0));
}

double wireLabelFraction(const Entity& wire) {
    return std::clamp(wire.meta.value(QStringLiteral("labelT")).toDouble(0.5), 0.0, 1.0);
}

void setWireLabelFraction(Entity& wire, double t) {
    wire.meta.insert(QStringLiteral("labelT"), std::clamp(t, 0.0, 1.0));
}

bool wirePointAtFraction(const Entity& wire, double t, Vec2& pos, Vec2& dir) {
    const std::vector<Vec2>& pts = wire.points;
    if (pts.size() < 2) return false;
    double total = 0.0;
    for (size_t i = 1; i < pts.size(); ++i) total += distance(pts[i - 1], pts[i]);
    if (total < 1e-9) return false;
    const double want = std::clamp(t, 0.0, 1.0) * total;
    double walked = 0.0;
    for (size_t i = 1; i < pts.size(); ++i) {
        const double seg = distance(pts[i - 1], pts[i]);
        if (seg < 1e-12) continue;
        if (walked + seg >= want - 1e-12) {
            const double f = (want - walked) / seg;
            pos = lerp(pts[i - 1], pts[i], f);
            dir = (pts[i] - pts[i - 1]).normalized();
            return true;
        }
        walked += seg;
    }
    pos = pts.back();
    dir = (pts.back() - pts[pts.size() - 2]).normalized();
    return true;
}

double breakLeanFactor(const Entity& mark) {
    // The lean is stated as an ANGLE from square, because that is how a draughtsman thinks of it; the
    // renderer wants it as a fraction of the reach, which is its tangent.
    const double deg = mark.meta.value(QStringLiteral("angle")).toDouble(-1.0);
    const bool curved =
        mark.meta.value(QStringLiteral("breakStyle")).toString() == QLatin1String("curved");
    if (deg < -0.5) return curved ? 0.0 : 0.55;     // nothing said: the shapes as they have always been
    return std::tan(std::clamp(deg, -75.0, 75.0) * 3.14159265358979323846 / 180.0);
}

std::vector<std::vector<Vec2>> breakStrokes(const Entity& mark) {
    if (mark.type != EntityType::LengthBreak) return {};
    const double half = std::max(0.5, mark.meta.value(QStringLiteral("size")).toDouble(6.0));
    const double gap = mark.meta.value(QStringLiteral("gap")).toDouble(half * 0.9);
    const double rot = mark.rotation;
    const Vec2 along{std::cos(rot), std::sin(rot)};
    const Vec2 across{-std::sin(rot), std::cos(rot)};
    const bool curved =
        mark.meta.value(QStringLiteral("breakStyle")).toString() == QLatin1String("curved");
    const double lean = half * breakLeanFactor(mark);
    std::vector<std::vector<Vec2>> out;
    for (int k = 0; k < 2; ++k) {
        const Vec2 mid = mark.a + along * (gap * (k == 0 ? -0.5 : 0.5));
        const Vec2 e1 = mid + across * half + along * lean;
        const Vec2 e2 = mid - across * half - along * lean;
        if (!curved) {
            out.push_back({e1, e2});
            continue;
        }
        // The same two cubics the renderer draws, sampled: a long lazy S whose sweep happens where it
        // crosses the run. Sampled rather than approximated by its chord, so a joint lands where the
        // curve actually meets the run and not where a straight line between its ends would.
        const Vec2 bow = along * (half * 0.45);
        const Vec2 c1 = e1 - bow * 0.35;
        const Vec2 c2 = mid + across * (half * 0.45) - bow;
        const Vec2 c3 = mid - across * (half * 0.45) + bow;
        const Vec2 c4 = e2 + bow * 0.35;
        auto cubic = [](const Vec2& p0, const Vec2& p1, const Vec2& p2, const Vec2& p3, double t) {
            const double u = 1.0 - t;
            const double w0 = u * u * u, w1 = 3 * u * u * t, w2 = 3 * u * t * t, w3 = t * t * t;
            return Vec2{p0.x * w0 + p1.x * w1 + p2.x * w2 + p3.x * w3,
                        p0.y * w0 + p1.y * w1 + p2.y * w2 + p3.y * w3};
        };
        std::vector<Vec2> s;
        constexpr int kSteps = 12;
        for (int i = 0; i <= kSteps; ++i) s.push_back(cubic(e1, c1, c2, mark.a + along * (gap * (k == 0 ? -0.5 : 0.5)), double(i) / kSteps));
        for (int i = 1; i <= kSteps; ++i) s.push_back(cubic(mid, c3, c4, e2, double(i) / kSteps));
        out.push_back(std::move(s));
    }
    return out;
}

namespace {

// A twisted pair, measured. The whole shape - what is drawn, what a click picks, what a zone catches -
// comes from this one description, so none of them can drift apart.
//
// A pair is TWO WIRES, each drawn as a band of its own thickness. At the ends they lie side by side and
// run straight, because that is where the pair is untwisted and the two wires are stripped, crimped and
// landed separately; in between they change places over and over, which is what laying up a pair does.
// The straight ends and the weave are the SAME lines - the wires simply stop weaving - so there is
// nothing to join at the change-over and nothing that can fail to meet.
struct TwistFrames {
    std::vector<Vec2> pos;       // where the path is, at each place it is measured
    std::vector<Vec2> n;         // and which way is across it
    std::vector<double> u;       // how far along it that is
    // HOW TIGHTLY THE ROUTE ITSELF IS TURNING at each of those places: the radius of the circle
    // through it and its two neighbours, signed - positive with the centre of the turn on the side
    // the normal points to. Nought is straight. A wire sitting further off the middle than that
    // radius would have to bend backwards to stay in its lane, which draws as a loop.
    std::vector<double> bend;
    // ...and, worked out from it once for the whole run, how far off the middle a wire is ALLOWED to
    // sit at each place, either side. A wire further off than the turn's own radius would have to
    // bend backwards to stay in its lane, and draws as a loop. The limits ease in and out, so the
    // wires crowd together going into a tight bend and open again coming out, which is what a loom
    // does; stepping straight to the limit at one place and back at the next draws a burst of
    // crossed lines at exactly the corner that needed the holding.
    std::vector<double> holdHi, holdLo;
    // ...and whether the route was ROUNDED at all. Where a corner has been asked for, a corner is
    // what the wires make of it; the holding-back below is for bends too tight to carry a bundle,
    // not for corners somebody has asked for.
    bool bendsRounded = false;
    int wires = 2;               // how many are laid up together
    double reach = 0.0;          // how far a wire's middle swings from the middle of the bundle
    double thick = 0.0;          // half a wire's own thickness
    // THE TWO ENDS OF A RUN ARE NOT THE SAME END. It leaves one thing and arrives at another, and
    // the two need not fan alike - eight ways out of a header at one end and two into a ring
    // terminal at the other is one run, not two. So the fan is described twice: the START side, the
    // end drawn first, and the FINISH side. Unless it is told otherwise the finish side simply
    // repeats the start, which is how nearly every run is drawn and what every run drawn so far is.
    double lead = 0.0;           // the straight run at the start end
    double leadB = 0.0;          // ...and at the finish end
    double len = 0.0;
    double turn = 0.0;           // the length of one whole crossing, as actually used
    int turns = 0;
    bool spreadStart = true;     // fan the wires out at the start end
    bool spreadEnd = true;       // ...and at the other
    // STRAIGHT, not laid up: the wires run parallel the whole way instead of crossing. A loom is
    // mostly drawn this way - the wires only get laid up where the drawing says they are twisted -
    // and the two are the same object with the same handles, so one can be made the other.
    bool straight = false;
    double lane = 0.0;           // between wires where they are spread out, centre to centre
    double laneB = 0.0;          // ...at the finish end
    double packed = 0.0;         // ...and the pitch they run at inside the bundle
    // HOW MANY WIRES CARRY ON once the spread ones have converged, and which trunk line each of them
    // runs into. A bundle is drawn with the number of lines it takes to read as a bundle, not with one
    // line per wire in it: five wires landing on pins converge into a trunk drawn as three, and the
    // two that are not one of the three run into it and stop. That is how these drawings are drawn.
    int trunk = 0;
    std::vector<int> trunkLane;  // per wire: which trunk line it becomes, or -1 if it stops
    double tail = 0.0;           // straight run at the very end, before the fan starts
    double tailB = 0.0;          // ...at the finish end
    // HOW MUCH INSULATION IS OFF THE END OF EACH WIRE. A wire is landed by stripping the last few
    // millimetres of it back to bare conductor and crimping that into a contact, and a harness
    // drawing says how much: it is the strip length, and it is a made figure like any other. Nought
    // is a drawing that does not say - the wires are drawn cut and nothing else.
    double strip = 0.0;
    double stripB = 0.0;
    // AND THE TWO RADII AS THEY CAME OUT, which is not what was asked for when nothing was: they are
    // worked out from the bundle and from the fan, and a field that says "Auto" has to be able to
    // show the figure it stands for. Both are the radius a WIRE turns on, not the centreline's.
    double kneeUsed = 0.0;
    double fanKneeUsed = 0.0;
    // WHAT THE CONVERGED STRETCH IS MADE OF: the wires themselves, a cable with a boot at each end,
    // or a length of tubing over the lot. The fans are untouched by all three.
    RunBody body = RunBody::Wires;
    bool cableBody = false;   // either of the two that draw a body rather than the wires
    // AND THE FIGURES IT IS DRAWN TO. How wide the cable is, and at each end how far across its face
    // is and over what length it comes down to it - the shrunk length. The two ends are described
    // separately where they are told to be: a cover over a loom is shrunk onto a connector at one
    // end and onto a breakout at the other, and those are not the same size.
    double bodyHalf = 0.0;
    // HOW WIDE THE TRANSITION IS, half of it, when it is said (twistBootWidth) - never narrower than the
    // cable it is fitted over. Below nought: as wide as it has always been drawn.
    double bootHalf = -1.0;
    double bootHalfB = -1.0;   // at the finish, where it is said apart (twistBootWidthFinish)
    double bodyFace = 0.0, bodyFaceB = 0.0;
    double bodyShrink = 0.0, bodyShrinkB = 0.0;
    // AND HOW FAR THE BOOT REACHES back along the cable at each end: the transition heatshrink that
    // takes the one cable down to the bundle of wires, measured from the cable's end inward.
    double bootA = 0.0, bootB = 0.0;
    // A TRANSITION ON THE END THE RUN DOES NOT FAN AT. Where a run fans, the boot is what takes the
    // cable down to the wires coming out of it. Where it does not - the end that is simply cable
    // carrying on - a boot is still fitted often enough to be worth drawing, and there it has no
    // wires to come down to: its free end is either recovered onto the cable or cut square across.
    double endBootA = 0.0, endBootB = 0.0;
    bool endBootRecovered = true;
    // A LENGTH OF TUBING COMES TWO WAYS. ALONG the cable - one sleeve the whole converged stretch
    // is made of, recovered at each end onto the fan - or OVER it: the cable is drawn, and the
    // heatshrink is a piece fitted over each of its ends, standing proud of it and recovered down
    // toward the fan. The second is the part that grips a breakout, which is why a cable has one
    // of its own.
    bool bodyOverCable = false;
    RunEnds transitionEnds = RunEnds::None;   // which ends of a cable wear the boot
    RunLay lay = RunLay::Straight;            // and how the wires run where they are gathered
    // AND WHAT THE FAN RUNS INTO AT EACH END, as a scale on where a wire sits in the run: one is
    // the bundle the wires would have made on their own, and anything else is the face of whatever
    // covers them. See the working in twistFrames.
    double entryA = 1.0, entryB = 1.0;
    // THE RADIUS THE FAN'S OWN CORNERS TURN ON. Below nought is nobody having said: the fan is
    // eased end to end, which is what every run drawn before there was a field for it has. NOUGHT
    // IS A SHARP CORNER, asked for - a drawing that wants the knee shown as a corner and not as a
    // bend, which there was no way to ask for while nought meant "you decide".
    double fanKnee = -1.0;
    double fanKneeB = -1.0;      // ...at the finish end
};

bool twistFrames(const Entity& pair, TwistFrames& f) {
    if (pair.type != EntityType::TwistedPair) return false;
    // The path it runs along: two points for a straight pair, the whole route for one drawn with knees.
    std::vector<Vec2> path =
        pair.points.size() >= 2 ? pair.points : std::vector<Vec2>{pair.a, pair.b};
    const double w =
        std::max(0.2, pair.meta.value(QStringLiteral("twistWidth")).toDouble(kTwistedPairWidth));
    const double half = w / 2.0;
    // Every wire gets the same share of the bundle, with a parting either side of it, and swings from
    // the middle by whatever is left over. Two wires come out exactly where they did before this took a
    // count: a pair is the two-wire case of the same drawing, not a different drawing.
    f.wires = std::clamp(pair.meta.value(QStringLiteral("twistWires")).toInt(2), 2, kTwistedMaxWires);
    // HOW BIG A WIRE IS, AND HOW FAR APART THEY RUN - and the two lays answer it oppositely.
    //
    // Laid up, the bundle is a cable of a given thickness and the wires share it out between them: a
    // twelve-wire cable of that thickness has thinner wires in it than a four-wire one, which is what
    // a cable is.
    //
    // Running straight there is no cable, only wires lying side by side - so the wire is the size a
    // wire is, and six of them take up more room than three. Drawing those the first way made a run
    // of many wires a bundle of hairs at the width of a run of few, which is not what any of the
    // drawings this was taken from show.
    if (pair.meta.value(QStringLiteral("twistStraight")).toBool()) {
        const double wire =
            std::max(0.1, pair.meta.value(QStringLiteral("twistWireWidth")).toDouble(kWiresRunWire));
        f.thick = wire / 2.0;
        // HOW FAR APART THEY RUN WHERE THE BUNDLE IS GATHERED. A pitch of its own where the drawing
        // gives one - wires bundled loosely and wires pulled up tight are both drawn, and this is
        // the difference between them - and otherwise the pitch these drawings are drawn at, which
        // is a wire and a half. Never closer than the wires are thick, or they would be drawn
        // through one another.
        const double askedPitch =
            pair.meta.value(QStringLiteral("twistBodyPitch")).toDouble(0.0);
        f.packed = askedPitch > 1e-9 ? std::max(askedPitch, wire)
                                     : wire * (kWiresRunPitch / kWiresRunWire);
        f.reach = f.wires > 1 ? f.packed * (f.wires - 1) / 2.0 : 0.0;
    } else {
        f.thick = (w / (2.0 * f.wires)) * (1.0 - kTwistedPairPart * 2.0);
        f.reach = half - f.thick;
        // The pitch they run at packed in the bundle, which is what the spread pitch falls back to a
        // half as much again of.
        f.packed = f.wires > 1 ? 2.0 * f.reach / (f.wires - 1) : 0.0;
    }

    // HOW TIGHTLY IT TURNS AT A KNEE. A cable does not kink at a corner, it bends - and a bend keeps
    // the wires from folding over one another on the inside of the turn, which a sharp corner cannot.
    //
    // The radius used to be a fixed share of the bundle width, which is a reasonable guess and only
    // that: how tightly a loom may be turned is set by what is in it and by what it is being dressed
    // round, and neither of those is the width of the line on the paper. Absent, the old figure
    // stands, so every run drawn before this turns exactly as it did.
    // THE RADIUS ASKED FOR IS THE SMALLEST ONE IN THE BUNDLE, not the centreline's.
    //
    // Every wire follows the centreline offset sideways by its own lane, so on the inside of a turn a
    // wire bends TIGHTER than the centreline by however far off it sits - half a bundle, at the
    // worst. A radius is a build constraint: it is the tightest the harness may be bent anywhere in
    // it, which is the innermost wire and never the line down the middle. Taking the figure as the
    // centreline's let the inner wires bend tighter than the number somebody had just typed in to
    // stop exactly that.
    //
    // So the centreline is rounded at the radius plus half the bundle as drawn - which on a
    // straight run is the wires side by side, not the cable width - and every wire in it then
    // turns on that radius or wider. Unset, the old figure stands and nothing already drawn moves.
    //
    // AND UNSET IS 1.2 TIMES THE BUNDLE AS DRAWN, which for a laid-up pair is the figure it always
    // was and for a straight run is not. A straight run's width is its wires side by side and has
    // nothing to do with the cable width the old figure was taken from: nine four-millimetre wires
    // are fifty-two across and were being turned on a ten-millimetre radius, so the wires on the
    // inside of the corner were bent round a radius of MINUS thirteen - inside out - which is where
    // the little triangles at every knee of a wide run came from.
    //
    // AND NOUGHT IS A SHARP CORNER. Nought used to mean "nobody has said", which left no way at all
    // to ask for a corner - every run was bent whether the drawing wanted it bent or not. Saying
    // nothing is now saying nothing, and nought is a figure like any other: no fillet.
    const double bundleHalf = f.reach + f.thick;
    const QJsonValue kneeAsked = pair.meta.value(QStringLiteral("twistKnee"));
    const double knee = kneeAsked.isUndefined() ? -1.0 : std::max(0.0, kneeAsked.toDouble(0.0));
    f.bendsRounded = path.size() > 2 && knee != 0.0;
    const double kneeCentre = knee > 0.0 ? knee + bundleHalf : std::max(w * 1.2, bundleHalf * 2.4);
    // What a WIRE turns on, which is the centreline's radius less how far off the middle the
    // innermost of them sits. Nought where a corner was asked for, because a corner is nought.
    f.kneeUsed = knee == 0.0 ? 0.0 : std::max(0.0, kneeCentre - bundleHalf);
    std::vector<double> pathBend(path.size(), 0.0);
    if (f.bendsRounded) path = roundedPolyline(path, kneeCentre, 10, &pathBend);

    std::vector<double> at{0.0};
    for (size_t i = 1; i < path.size(); ++i)
        at.push_back(at.back() + (path[i] - path[i - 1]).length());
    f.len = at.back();
    if (f.len < 1e-9) return false;

    double period =
        std::max(0.05, pair.meta.value(QStringLiteral("twistPitch")).toDouble(kTwistedPairPitch));
    const bool straightLay = pair.meta.value(QStringLiteral("twistStraight")).toBool();
    // EACH SIDE OF THE RUN IS DESCRIBED, and by default the finish side is described by the start.
    //
    // A run usually fans the same way at both ends and is drawn by saying it once, so that is the
    // default and every run already on a sheet keeps it. Unticked, the finish end takes values of
    // its own: the same three settings under their own names, and anything it has not been given
    // separately still falls back to the start's rather than to nothing.
    const bool sidesSame = !pair.meta.contains(QStringLiteral("twistSidesSame")) ||
                           pair.meta.value(QStringLiteral("twistSidesSame")).toBool();
    auto sideValue = [&](const QString& key, bool finish) {
        if (finish && !sidesSame) {
            const QString own = key + QStringLiteral("Finish");
            if (pair.meta.contains(own)) return pair.meta.value(own);
        }
        return pair.meta.value(key);
    };
    // HOW FAR APART THE WIRES SIT WHERE THEY ARE SPREAD. In the bundle they are packed across its
    // width, which is what the width is for; spread out at an end they go to a pitch of their own,
    // because what settles that is the thing they are being landed on and not how thick the bundle
    // is. Absent, it is half again the pitch they run at in the bundle - which is what the drawings
    // this was taken from were drawn at.
    f.lane = std::max(0.0, sideValue(QStringLiteral("twistLane"), false).toDouble(0.0));
    f.laneB = std::max(0.0, sideValue(QStringLiteral("twistLane"), true).toDouble(0.0));
    if (f.lane < 1e-9) f.lane = f.packed * 1.5;
    if (f.laneB < 1e-9) f.laneB = f.packed * 1.5;
    // A FAN NOBODY HAS SIZED IS AS LONG AS IT NEEDS TO BE.
    //
    // The default lead is a fixed length, which is fine while the wires only have to move a little
    // way apart. Open the pitch up and they have the same short stretch to travel much further in,
    // so they cross the run at seventy or eighty degrees - and a wire slanting that steeply is drawn
    // either far too wide or, once the widening is capped, thinner the wider the pitch is set. That
    // is the wrong answer to a pitch: opening a fan out should spread the wires, not pinch them.
    //
    // So where no fan length has been asked for, it grows with the distance the outermost wire has
    // to cover. Roughly one across to one along, which is the slant a fan is drawn at by hand.
    // Asking for a length still gets exactly that length - somebody who sets one has said so.
    auto fanLength = [&](bool finish) {
        const QJsonValue asked = sideValue(QStringLiteral("twistEnds"), finish);
        double lead = std::max(0.0, asked.toDouble(kTwistedPairLead));
        if (straightLay && asked.isUndefined()) {
            const double pitch = finish ? f.laneB : f.lane;
            const double travel = f.wires > 1 ? pitch * (f.wires - 1) / 2.0 : 0.0;
            lead = std::max(lead, travel);
        }
        return lead;
    };
    f.lead = fanLength(false);
    f.leadB = fanLength(true);
    // The two straight ends and at least one whole crossing have to fit in what was drawn; a pair drawn
    // shorter keeps its proportions rather than losing its weave or running past its own ends.
    // The two straight ends have to fit in what was drawn. Laid up, a whole crossing has to fit
    // between them as well; running straight there is no crossing to make room for, so the ends keep
    // what they were asked for until they meet in the middle.
    // Both of them at once, and each gives up its share: a run too short for its two fans keeps
    // their proportions rather than cutting the longer one back to the shorter.
    if (straightLay) {
        const double want = f.lead + f.leadB;
        if (want > f.len && want > 1e-9) {
            const double k = f.len / want;
            f.lead *= k;
            f.leadB *= k;
        }
    } else if (f.lead + f.leadB + period > f.len) {
        const double k = f.len / (f.lead + f.leadB + period);
        f.lead *= k;
        f.leadB *= k;
        period *= k;
    }
    const double woven = f.len - f.lead - f.leadB;
    // The ends the wires fan out at. "Left" and "right" are sheet directions, not click order, so a
    // run drawn right-to-left still spreads on the side asked for. On a vertical run there is no left
    // or right end to tell apart; the first point takes "left" and the second "right".
    const QString spread = twistedPairSpread(pair);
    const bool startIsLeft = path.front().x <= path.back().x;
    f.spreadStart = spread == QLatin1String("both") ||
                    (spread == QLatin1String("left") && startIsLeft) ||
                    (spread == QLatin1String("right") && !startIsLeft) ||
                    spread == QLatin1String("start");
    f.spreadEnd = spread == QLatin1String("both") ||
                  (spread == QLatin1String("right") && startIsLeft) ||
                  (spread == QLatin1String("left") && !startIsLeft) ||
                  spread == QLatin1String("finish");
    f.straight = pair.meta.value(QStringLiteral("twistStraight")).toBool();
    // AND HOW MANY OF THEM CARRY ON. All of them unless the drawing says fewer; never more than there
    // are, and never none.
    f.trunk = std::clamp(pair.meta.value(QStringLiteral("twistTrunkWires")).toInt(f.wires), 1,
                         f.wires);
    if (!f.straight) f.trunk = f.wires;   // a laid-up bundle is every wire in it, all the way
    // WHICH of them carry on: spread evenly through the fan, so the ones that stop are the ones
    // between them. Taking the first few instead would send the whole trunk to one side of the fan.
    f.trunkLane.assign(static_cast<size_t>(f.wires), -1);
    for (int j = 0; j < f.trunk; ++j) {
        const int k = f.trunk > 1
                          ? static_cast<int>(std::llround(double(j) * (f.wires - 1) / (f.trunk - 1)))
                          : (f.wires - 1) / 2;
        f.trunkLane[static_cast<size_t>(std::clamp(k, 0, f.wires - 1))] = j;
    }
    // THE STRAIGHT BIT AT THE VERY END - the FLUSH END. The wires are landed on something, and what
    // they land on is square to the run, so they come out of the fan and run straight into it rather
    // than arriving still spreading. Every wire is then parallel to the run where it is cut, so
    // every cut end lines up square across the fan: flush. The fan takes the part of the end nearer
    // the bundle, the flush run the part nearer the end.
    //
    // HOW LONG IT IS can be asked for, and it can be turned off. It was a fixed share of the fan and
    // neither: a drawing that wants the wires cut where they are still spreading - fanned right to
    // the pins - had no way to say so, and one that wants a stated length of parallel wire to land
    // had no way to say that either. Left alone it is the share it always was.
    auto flushOf = [&](bool finish) {
        if (!f.straight) return 0.0;
        if (!sideValue(QStringLiteral("twistFlush"), finish).toBool(true)) return 0.0;
        const double lead = finish ? f.leadB : f.lead;
        const QJsonValue asked = sideValue(QStringLiteral("twistFlushLen"), finish);
        const double want =
            asked.isUndefined() ? lead * kWiresRunTailShare : std::max(0.0, asked.toDouble(0.0));
        // Never the whole end: the wires still have to get to their lanes, and a fan of no length at
        // all is a corner where the drawing wanted a fan.
        return std::min(want, lead * 0.95);
    };
    f.tail = flushOf(false);
    f.tailB = flushOf(true);
    // AND HOW MUCH OF THE INSULATION IS OFF THE END. Asked for, it is a length; left to the drawing
    // it is five millimetres or whatever the end can spare, which is what these are stripped to.
    // Never more than the end is long: a strip mark past the fan is a mark on the bundle.
    auto stripOf = [&](bool finish) {
        if (!f.straight) return 0.0;
        // AND ONLY WHERE THE WIRES ARE SPREAD. A strip mark is a mark on a wire end that is on
        // show; an end that does not fan has no wire ends drawn at it, only the bundle running
        // into whatever it runs into, and a step in the bundle's own edges there says the whole
        // loom has been stripped.
        if (!(finish ? f.spreadEnd : f.spreadStart)) return 0.0;
        if (!sideValue(QStringLiteral("twistStrip"), finish).toBool(false)) return 0.0;
        const double lead = finish ? f.leadB : f.lead;
        const QJsonValue asked = sideValue(QStringLiteral("twistStripLen"), finish);
        const double want = asked.isUndefined() ? std::min(5.0, lead * 0.4)
                                                : std::max(0.0, asked.toDouble(0.0));
        return std::min(want, lead * 0.9);
    };
    f.strip = stripOf(false);
    f.stripB = stripOf(true);
    f.body = runBodyOf(pair);
    // HOW THE WIRES RUN WHERE THEY ARE GATHERED. Only a straight run has a bundle to lay up - a
    // laid-up one is already a braid of the lot.
    f.lay = f.straight ? runLayOf(pair) : RunLay::Straight;
    f.cableBody = f.straight && f.body != RunBody::Wires;
    if (f.cableBody) {
        // WHAT IT IS DRAWN TO, and what each figure falls back to when nobody has said.
        //
        // A CABLE IS DRAWN AS TWO LINES, whatever it carries. The stretch between the fans is one
        // part on the sheet - a cable, a length of tubing - and a drawing office draws that as a
        // pair of lines the width of two hook-up wires, whether there are four wires inside it or
        // forty. Drawn to the BUNDLE, as it was, a forty-way loom came out as a slab half the
        // height of the sheet with its fans hanging off the ends of it: the width of the thing
        // drawn was standing for a count nobody reads off the drawing anyway, and the part itself
        // had stopped looking like a part.
        //
        // It is shrunk ONTO what it grips at each end, so the face falls back to a little inside
        // the cable and the length it takes to get there is three times the standing-off. Every one
        // of them can be said instead, and the two ends said apart.
        const double over = std::max(0.2, f.thick * 0.8);
        const double twoWires = f.packed + 2.0 * f.thick;
        const double asked =
            std::max(0.0, pair.meta.value(QStringLiteral("twistBodyWidth")).toDouble(0.0));
        f.bodyHalf = (asked > 1e-9 ? asked : twoWires) / 2.0;
        const bool bothSides = !pair.meta.contains(QStringLiteral("twistBodySidesSame")) ||
                               pair.meta.value(QStringLiteral("twistBodySidesSame")).toBool();
        auto bodySide = [&](const QString& key, bool finish) {
            if (finish && !bothSides) {
                const QString own = key + QStringLiteral("Finish");
                if (pair.meta.contains(own)) return pair.meta.value(own);
            }
            return pair.meta.value(key);
        };
        auto faceOf = [&](bool finish) {
            const QJsonValue v = bodySide(QStringLiteral("twistBodyFace"), finish);
            const double across =
                v.isUndefined() ? std::max(0.1, twoWires - 2.0 * over) : std::max(0.0, v.toDouble(0.0));
            // Never wider than the cable it is the end of: a face that flares is not a shrunk end.
            return std::clamp(across / 2.0, 0.05, f.bodyHalf);
        };
        auto shrinkOf = [&](bool finish) {
            const QJsonValue v = bodySide(QStringLiteral("twistBodyShrink"), finish);
            return v.isUndefined() ? over * 3.0 : std::max(0.0, v.toDouble(0.0));
        };
        f.bodyFace = faceOf(false);
        f.bodyFaceB = faceOf(true);
        if (pair.meta.contains(QStringLiteral("twistBootWidth")) &&
            pair.meta.value(QStringLiteral("twistBootWidth")).toDouble(0.0) > 1e-9)
            f.bootHalf = std::max(f.bodyHalf, pair.meta.value(QStringLiteral("twistBootWidth")).toDouble() / 2.0);
        f.bootHalfB = f.bootHalf;
        if (pair.meta.value(QStringLiteral("twistBootWidthFinish")).toDouble(0.0) > 1e-9)
            f.bootHalfB = std::max(f.bodyHalf, pair.meta.value(QStringLiteral("twistBootWidthFinish")).toDouble() / 2.0);
        f.bodyShrink = shrinkOf(false);
        f.bodyShrinkB = shrinkOf(true);
        // THE TRANSITION HEATSHRINK AT EACH END, which only a cable has: a length of tubing over
        // the whole run IS the transition, and a second one at its end would be a part fitted over
        // itself. It can be turned off - a cable can be drawn running straight out into its wires -
        // and it is only ever at an end the wires FAN at, because an end that does not fan has
        // nothing to make the transition to.
        //
        // AND IT REACHES ALONG THE CABLE, not out over the fan. Fitted the other way it stood over
        // the first of the fan and hid it: the wires came out of its mouth already spread, and the
        // part of the drawing that says how they get there - the fan itself - was underneath the
        // boot. Sat back on the cable instead, the whole fan is on show and it starts at one
        // vertical face, which is the face of the transition.
        //
        // Left alone it is three times across the face it grips - which is how a part like this is
        // sized: long enough to hold, and in proportion to the thing it is holding. Taken off the
        // fan's length instead, the same boot grew and shrank with a figure that has nothing to do
        // with it.
        f.bodyOverCable = f.body == RunBody::Heatshrink &&
                          pair.meta.value(QStringLiteral("twistBodyOverCable")).toBool();
        // EVERY WIRE HAS ITS OWN PLACE AT THE FACE. Converging onto a few trunk lines is how a
        // bundle of hook-up wires is DRAWN - a handful of lines standing for the lot of them - and
        // under a cover there are no such lines: what the wires run into is a face, and each of
        // them arrives at its own place on it. Left trunked, six wires piled onto two lanes and
        // went into the cable three deep.
        f.trunk = f.wires;
        for (int j = 0; j < f.wires; ++j) f.trunkLane[static_cast<size_t>(j)] = j;
        // WHICH ENDS WEAR ONE. A cable is told outright - neither, one, or both - and a length of
        // tubing fitted OVER a cable wears one at each end by the nature of the thing.
        f.transitionEnds =
            f.body == RunBody::Cable ? runTransitionEndsOf(pair)
                                     : (f.bodyOverCable ? RunEnds::Both : RunEnds::None);
        auto bootOf = [&](bool finish) {
            if (!runEndsHas(f.transitionEnds, finish)) return 0.0;
            if (!(finish ? f.spreadEnd : f.spreadStart)) return 0.0;
            const QJsonValue v = bodySide(QStringLiteral("twistBoot"), finish);
            const double face = 2.0 * (finish ? f.bodyFaceB : f.bodyFace);
            const double want = v.isUndefined() ? face * 3.0 : std::max(0.0, v.toDouble(0.0));
            // Never more than its share of the CABLE, which is what it sits on: two boots on a
            // short run would otherwise meet in the middle and be drawn as one solid thing.
            return std::min(want, std::max(0.0, f.len - f.lead - f.leadB) * 0.45);
        };
        f.bootA = bootOf(false);
        f.bootB = bootOf(true);
        // AND THE SAME PART ON A BODY END. Asked for separately, because it is a different thing to
        // ask: a boot at a fan is the step from cable to wires and comes with the fan, while this one
        // is the end of the cable itself and is there only if the drawing says so.
        f.endBootRecovered =
            !pair.meta.contains(QStringLiteral("twistEndBootRecovered")) ||
            pair.meta.value(QStringLiteral("twistEndBootRecovered")).toBool();
        auto endBootOf = [&](bool finish) {
            if (!pair.meta.value(QStringLiteral("twistEndBoot")).toBool()) return 0.0;
            if (finish ? f.spreadEnd : f.spreadStart) return 0.0;   // that end fans: bootOf has it
            const double face = 2.0 * (finish ? f.bodyFaceB : f.bodyFace);
            const QJsonValue v = bodySide(QStringLiteral("twistBoot"), finish);
            const double want = v.isUndefined() ? face * 3.0 : std::max(0.0, v.toDouble(0.0));
            return std::min(want, std::max(0.0, f.len - f.lead - f.leadB) * 0.45);
        };
        f.endBootA = endBootOf(false);
        f.endBootB = endBootOf(true);
        // AND THE FAN RUNS INTO THE FACE OF THE COVER, not into the bundle it would have made
        // without one. The two are the same size while every figure is left alone - the cover is
        // drawn to the bundle it covers - but set a width, or an end size, or put a boot on it, and
        // they part company: the wires then stood out past the face, or stopped short inside it
        // with daylight between the last of the fan and the thing it goes into.
        //
        // Said as a SCALE on where each wire sits in the run, so everything else about the fan -
        // the slant, the corner radius, the flush end - is worked out from it exactly as before,
        // and a run with everything on Auto is the run that has always been drawn.
        auto entryOf = [&](bool finish) {
            const double boot = finish ? f.bootB : f.bootA;
            // What the wires actually meet: the recovered mouth of the boot where there is one, the
            // shrunk end of a sleeve laid along the run, or the square face of a bare cable.
            const bool cut = f.body == RunBody::Cable || f.bodyOverCable;
            const double face = (boot > 1e-9 || !cut) ? (finish ? f.bodyFaceB : f.bodyFace)
                                                      : f.bodyHalf;
            const double want = std::max(0.0, face - f.thick);
            const double have = f.wires > 1 ? f.packed * (f.wires - 1) / 2.0 : 0.0;
            return have > 1e-9 ? want / have : 1.0;
        };
        f.entryA = entryOf(false);
        f.entryB = entryOf(true);
    }
    // AND WHAT RADIUS THE FAN'S CORNERS CAME OUT ON, for the outermost wire - the one that travels
    // furthest and so is the one a radius is read off. Left alone the fan gives a quarter of the
    // slant to each corner, and this is the length that share comes to.
    {
        const double fanLen = std::max(1e-9, f.lead - f.tail);
        const double spread0 = f.wires > 1 ? f.lane * (f.wires - 1) / 2.0 : 0.0;
        const double inRun0 = f.packed * (f.trunk - 1) / 2.0;
        const double turn0 = std::atan(std::abs(inRun0 - spread0) / fanLen);
        const double half0 = std::tan(turn0 / 2.0);
        const double ease0 =
            f.fanKnee < 0.0
                ? 0.25
                : (f.fanKnee > 1e-9 ? std::clamp(f.fanKnee * half0 / fanLen, 0.0, 0.5) : 0.0);
        f.fanKneeUsed = half0 > 1e-9 ? ease0 * fanLen / half0 : 0.0;
    }
    // AND HOW TIGHTLY THE FAN'S OWN CORNERS TURN, which is not the run's knee radius: the run
    // bends where the loom is routed round something, the fan bends where a wire leaves the bundle
    // for its pin. They are set on different things and are different sizes, and one figure for
    // both would be wrong for whichever of them it was not chosen for. Unset, the fan is eased
    // across its whole length, which is what every run drawn so far has; nought is a sharp corner.
    auto fanKneeOf = [&](bool finish) {
        const QJsonValue asked = sideValue(QStringLiteral("twistFanKnee"), finish);
        return asked.isUndefined() ? -1.0 : std::max(0.0, asked.toDouble(0.0));
    };
    f.fanKnee = fanKneeOf(false);
    f.fanKneeB = fanKneeOf(true);
    // Whole crossings, so each wire comes back to the side it started on and runs level into the
    // straight end: half a crossing would leave the two wires swapped and the ends kinked.
    f.turns = std::max(1, static_cast<int>(std::llround(woven / period)));
    f.turn = woven / f.turns;

    // Every place the shape is measured: the ends, where the weave starts and stops, and a grid through
    // the weave fine enough that each half crossing is measured the same number of times (so a crossing
    // always lands exactly on a measured place).
    // 48 places per turn draw a smooth helix (7.5 degrees a step) at drawing zoom;
    // a long route with a tight weave is measured more coarsely rather than without bound, because
    // everything that asks about the shape walks these places.
    int perTurn = std::clamp(4800 / std::max(1, f.turns), 16, 48);
    // A BUNDLE IS CUT EVERY 1/4N OF A TURN (see twistedPairWeave) - where each wire goes round the side
    // of it - so every one of those places has to be a measured one, with some between them, or a
    // strand's front half is drawn from whichever places happen to fall inside it, and most of them
    // are drawn as nothing.
    if (!f.straight && f.wires >= 3) {
        const int q = 4 * f.wires;
        perTurn = q * std::max(2, (perTurn + q - 1) / q);
    }
    const int grid = f.turns * perTurn;
    std::vector<double> us{0.0, f.len};
    if (f.straight) {
        // MEASURED WHERE THE SHAPE IS, which for a straight run is the two ENDS. The middle is one
        // straight stretch and needs nothing but its knees; the fan is a curve and needs a place
        // every degree or so of it, and it used to get none at all - the grid below covers the
        // weave, which a straight run has not got, so the fan was drawn as one flat slant and the
        // easing on it never showed.
        us.push_back(f.lead);
        us.push_back(f.len - f.leadB);
        // AND WHERE THE INSULATION STOPS, exactly, so the wire can step in to its bare conductor
        // there rather than somewhere near there.
        if (f.strip > 1e-9) us.push_back(f.strip);
        if (f.stripB > 1e-9) us.push_back(f.len - f.stripB);
        for (int i = 0; i <= 24; ++i) {
            us.push_back(f.lead * i / 24.0);
            us.push_back(f.len - f.leadB * i / 24.0);
        }
        // AND THROUGH THE BUNDLE TOO, where a pair of lanes is laid up in it: the middle of a
        // straight run needs no places at all while every wire in it runs straight, and needs them
        // every few degrees the moment two of them start going round each other.
        if (f.lay != RunLay::Straight)
            for (int i = 0; i <= grid; ++i) us.push_back(f.lead + woven * i / grid);
    } else {
        for (int i = 0; i <= grid; ++i) us.push_back(f.lead + woven * i / grid);
    }
    for (size_t i = 1; i + 1 < path.size(); ++i) us.push_back(at[i]);
    std::sort(us.begin(), us.end());
    us.erase(std::unique(us.begin(), us.end(),
                         [&](double a, double b) { return std::abs(a - b) < f.len * 1e-9; }),
             us.end());

    // Where the path is, at any distance along it.
    auto posAt = [&](double u) {
        const double uu = std::clamp(u, 0.0, f.len);
        // The leg this distance falls on: the last one that starts at or before it.
        size_t seg = static_cast<size_t>(std::upper_bound(at.begin(), at.end(), uu) - at.begin());
        if (seg > 0) --seg;
        seg = std::min(seg, path.size() - 2);
        Vec2 d = path[seg + 1] - path[seg];
        const double dl = d.length();
        if (dl < 1e-12) return path[seg];
        return path[seg] + Vec2{d.x / dl, d.y / dl} * (uu - at[seg]);
    };
    // Which way is across the path, measured over a FIXED short step either side of the place rather
    // than from the neighbouring places. A bend is a run of short straight bits and the places are not
    // evenly spaced along it, so taking the direction from the neighbours skews it differently at each
    // one - and the two wires then jog sideways at every one, a saw edge all the way round the corner.
    const double delta = std::max(1e-9, std::min(0.02 * f.len, 0.05 * w));
    f.pos.reserve(us.size());
    f.n.reserve(us.size());
    f.bend.reserve(us.size());
    f.u = us;
    // HOW TIGHTLY THE ROUTE TURNS at a distance along it, taken from the rounding that made it. It
    // cannot be measured off the result: the result is a polyline, and three points off it a
    // millimetre apart describe the corner between two of an arc's own ten segments rather than the
    // arc - which reads as a radius of three where the arc is twenty-five.
    auto bendAt = [&](double u) {
        const double uu = std::clamp(u, 0.0, f.len);
        size_t seg = static_cast<size_t>(std::upper_bound(at.begin(), at.end(), uu) - at.begin());
        if (seg > 0) --seg;
        seg = std::min(seg, path.size() - 2);
        const double a = pathBend[seg], b = pathBend[seg + 1];
        // A step with a straight end is a straight step: the joints at either end of an arc carry
        // nought, so the run up to a bend is not treated as part of it.
        if (std::abs(a) < 1e-12 || std::abs(b) < 1e-12) return 0.0;
        return std::abs(a) < std::abs(b) ? a : b;
    };
    for (const double u : us) {
        f.pos.push_back(posAt(u));
        Vec2 d = posAt(u + delta) - posAt(u - delta);
        const double dl = d.length();
        f.n.push_back(dl < 1e-12 ? Vec2{0.0, 1.0} : Vec2{d.x / dl, d.y / dl}.perp());
        f.bend.push_back(bendAt(u));
    }

    // HOW FAR OFF THE MIDDLE A WIRE MAY SIT, at each place. Its own radius is the middle's less how
    // far off it is, so a wire further off than the middle's radius is turning on a NEGATIVE one -
    // inside out - and draws as a loop round the corner, one loop per wire. That is the spiral a run
    // turned back on itself a few millimetres from where it started used to make, and the little
    // triangles at the knee of any run wide enough for its bend. A loom cannot do it either: the
    // wires on the inside of a tight bend crowd together, so that is what is drawn.
    //
    // Only where the route was BENT. Somebody who asked for a corner is asking for what a corner
    // does to the wires round it, and gets it.
    const double hold = f.thick * 1.5;
    const size_t places = f.bend.size();
    f.holdHi.assign(places, 1e18);
    f.holdLo.assign(places, -1e18);
    if (f.bendsRounded)
        for (size_t i = 0; i < places; ++i) {
            const double r = f.bend[i];
            if (r > 1e-9) f.holdHi[i] = std::max(0.0, r - hold);
            else if (r < -1e-9) f.holdLo[i] = std::min(0.0, r + hold);
        }
    // AND IT COMES ON GRADUALLY: a wire closes on the bundle at no more than one across to one
    // along, coming and going, which is the slant everything else in the drawing crowds at.
    for (size_t i = 1; i < places; ++i) {
        const double reach = f.u[i] - f.u[i - 1];
        f.holdHi[i] = std::min(f.holdHi[i], f.holdHi[i - 1] + reach);
        f.holdLo[i] = std::max(f.holdLo[i], f.holdLo[i - 1] - reach);
    }
    for (size_t i = places; i-- > 1;) {
        const double reach = f.u[i] - f.u[i - 1];
        f.holdHi[i - 1] = std::min(f.holdHi[i - 1], f.holdHi[i] + reach);
        f.holdLo[i - 1] = std::max(f.holdLo[i - 1], f.holdLo[i] - reach);
    }
    return true;
}

// Each wire has its own place in the lay-up: they are spread evenly round a whole turn of it, which is
// what laying up a bundle does - at any point along it the wires are spread across the bundle, and each
// one is at a different depth. (Spread over HALF a turn instead, the phases crowd together as the count
// goes up and a twelve-wire bundle sweeps up and down as one thick ribbon rather than reading as a
// bundle.) Two wires give half a turn apart either way, so a pair is unchanged.
double twistPhase(const TwistFrames& f, int k) {
    return f.wires > 0 ? 2.0 * M_PI * k / f.wires : 0.0;
}

// WHICH LANE OF A GATHERED BUNDLE WIRE k RUNS IN. A wire that is not one of the trunk lines heads
// for the nearest one and stops there, so it has a lane too, all the way in.
int laneOf(const TwistFrames& f, int k) {
    const int mine = k < static_cast<int>(f.trunkLane.size()) ? f.trunkLane[size_t(k)] : -1;
    if (mine >= 0) return mine;
    const double where = f.wires > 1 ? double(k) * (f.trunk - 1) / (f.wires - 1) : 0.0;
    return std::clamp(static_cast<int>(std::llround(where)), 0, std::max(0, f.trunk - 1));
}

// AND WHICH LANE IS LAID UP WITH WHICH. Neighbours, two by two, the way a pair is made: lane 0 with
// 1, 2 with 3, and so on. An odd lane left over at the end has nobody to twist with and runs
// straight, which is what the odd wire in a real loom does.
//
// MIXED lays up every other pair: a harness carrying a few signal pairs among its power wires,
// which is the commonest loom there is and the one a drawing cannot show at all if the only choice
// is all of them or none of them.
int laneTwinOf(const TwistFrames& f, int j) {
    if (f.lay == RunLay::Straight || j < 0) return -1;
    const int twin = (j % 2 == 0) ? j + 1 : j - 1;
    if (twin < 0 || twin >= f.trunk) return -1;
    if (f.lay == RunLay::Mixed && ((std::min(j, twin) / 2) % 2) != 0) return -1;
    return twin;
}

// How far round its twist a laid-up lane is at u. The weave is a WHOLE number of turns between the
// fans, so at both ends every wire is back on its own lane and the fan runs into the same place it
// always did - which is what lets a pair be laid up without touching anything else about the run.
double lanePhase(const TwistFrames& f, int j, double u) {
    const double at = std::clamp(u, f.lead, f.len - f.leadB);
    return 2.0 * M_PI * (at - f.lead) / std::max(1e-9, f.turn) + (j % 2 == 0 ? 0.0 : M_PI);
}

// Where wire k's middle is, across the path, at distance u along it.
//
// Through the weave it swings from side to side, a whole turn at a time. At the ends it runs straight,
// and there the wires are EVENLY SPACED across the bundle rather than left where the weave puts them -
// that is the fan-out: the ends are what gets stripped and landed on pins, so they are spread out
// squarely, and the straight run from there into the weave is the fan. With two wires those two places
// are the same one, so a pair's ends run parallel as they always have.
double twistOffset(const TwistFrames& f, int k, double u) {
    const double phase = twistPhase(f, k);
    // WHERE THE WIRE SITS ACROSS THE RUN, which is the one thing the two lays differ in.
    //
    // Laid up, a wire swings from side to side and is at its own phase of the crossing. Running
    // straight it stays in its own lane - the wires never cross, so there is nothing to swing.
    // Everything else about the two is the same: the same ends, the same fan, the same handles.
    //
    // IN THE BUNDLE, a straight run sits in one of the TRUNK lines rather than in a line of its own:
    // a wire that is not one of them is drawn running into the bundle and stopping (see the ranges in
    // twistCached), and until it stops it is heading for the trunk line nearest it.
    double lane;
    int j = -1, twin = -1;
    double centre = 0.0, swing = 0.0;
    if (!f.straight) {
        lane = f.wires > 1 ? f.reach * (1.0 - 2.0 * double(k) / (f.wires - 1)) : 0.0;
    } else {
        const double half = f.packed * (f.trunk - 1) / 2.0;
        j = laneOf(f, k);
        lane = half - f.packed * j;
        // LAID UP WITH ITS NEIGHBOUR, where the drawing says so: the two of them swing about the
        // middle of the pair, half a lane either way, crossing twice a turn. At the ends of the
        // weave the swing is back on the lane itself, so the fan is untouched by any of it.
        twin = laneTwinOf(f, j);
        if (twin >= 0) {
            centre = half - f.packed * (j + twin) / 2.0;
            swing = f.packed / 2.0;
        }
    }
    const double inRun = f.straight ? lane : f.reach * std::cos(phase);
    if (u > f.lead && u < f.len - f.leadB)
        return !f.straight ? f.reach * std::cos(2.0 * M_PI * (u - f.lead) / f.turn + phase)
               : twin >= 0 ? centre + swing * std::cos(lanePhase(f, j, u))
                           : lane;
    // WHERE A SPREAD END PUTS IT. Laid up, the wires are spread evenly across the bundle - they are
    // being stripped and landed, so they come out square. Running straight they open out to a pitch
    // of their own instead, because the bundle's width is not what settles how far apart the things
    // they land on are - and the two ends need not be landing on the same sort of thing, so each
    // has its own pitch.
    auto spreadAt = [&](double pitch) {
        return f.wires > 1
                   ? (f.straight ? pitch * (double(f.wires - 1) / 2.0 - double(k))
                                 : f.reach * (1.0 - 2.0 * double(k) / (f.wires - 1)))
                   : 0.0;
    };
    auto leadOffset = [&](double into, bool fan, double lead, double tail, double pitch,
                          double fanKnee, double entry) {
        // WHERE THE FAN RUNS IN TO. The bundle, ordinarily - and the face of whatever covers the
        // run where there is one, which is the same place until somebody sizes the cover. See
        // entryA in twistFrames.
        const double atBundle = inRun * entry;
        // A spread end lands the wires evenly across the bundle. An unspread end simply carries each
        // wire's place in the run straight out to the end, so the bundle does not open there at all.
        const double target = fan ? spreadAt(pitch) : atBundle;
        double t;
        if (f.straight) {
            // THE STRAIGHT BIT FIRST, THEN THE FAN. Out to the tail the wire is where it is landed and
            // does not move at all; from there it fans back into the run. Without it the wires arrive
            // at the end still spreading, and a run drawn along the sheet does not end level.
            const double fanLen = std::max(1e-9, lead - tail);
            t = std::clamp((into - tail) / fanLen, 0.0, 1.0);
            // A FAN IS A STRAIGHT RUN OF WIRE WITH A RADIUS AT EACH END OF IT. That is what one
            // looks like on paper and what one looks like in a loom: the wire leaves the bundle,
            // crosses to its lane in a straight line, and settles. It used to be eased across its
            // whole length instead - one long S with no straight anywhere in it - which reads as a
            // wire that is bending the entire way to its pin and never simply going there.
            //
            // HOW BIG THAT RADIUS IS CAN BE ASKED FOR, and then it is a length, like every other
            // radius on the sheet, rather than a share of whatever the fan happens to be. A fillet
            // of radius r at a corner turning through an angle takes r*tan(angle/2) of run either
            // side of the corner, so that is how much of the fan each of its two corners is given
            // and the stretch between them is the slant. A wire with further to travel slants more
            // steeply and so spends more of the fan on the same radius, which is what a fillet does.
            // Ask for more than the fan can hold and the two corners meet in the middle: one curve
            // end to end, and no tighter than that.
            //
            // NOUGHT IS A SHARP CORNER, asked for, and nobody having said is a quarter of the slant
            // to each corner - which is a radius that suits the fan it is in whatever size the fan
            // is, and leaves the middle half of the slant dead straight.
            double ease = 0.25;
            if (fanKnee >= 0.0) {
                const double turn = std::atan(std::abs(atBundle - target) / fanLen);
                ease = fanKnee > 1e-9
                           ? std::clamp(fanKnee * std::tan(turn / 2.0) / fanLen, 0.0, 0.5)
                           : 0.0;
            }
            if (ease > 1e-9 && tail > 1e-9) {
                // Turn in over the first ease of the fan, run straight, turn out over the last - how
                // far a thing gets at a speed that ramps up, holds, and ramps down again.
                const double raw = t < ease ? t * t / (2.0 * ease)
                                   : t > 1.0 - ease
                                       ? 1.0 - ease - (1.0 - t) * (1.0 - t) / (2.0 * ease)
                                       : t - ease / 2.0;
                t = raw / (1.0 - ease);
            } else if (ease > 1e-9) {
                // NO FLUSH END, SO NO CORNER AT THAT END TO ROUND. A fillet rounds the corner where
                // the slant meets the straight, and with the flush end turned off there is no
                // straight for it to meet - the wire is cut while it is still crossing. Rounding it
                // anyway levelled the wire off over the last millimetre or two, which is the one
                // thing turning the flush end off is asking not to happen. The corner into the
                // bundle is still a corner, and still rounded.
                const double raw = t > 1.0 - ease
                                       ? 1.0 - ease / 2.0 - (1.0 - t) * (1.0 - t) / (2.0 * ease)
                                       : t;
                t = raw / (1.0 - ease / 2.0);
            }
        } else {
            t = lead > 1e-9 ? std::clamp(into / lead, 0.0, 1.0) : 1.0;
        }
        return target + (atBundle - target) * t;
    };
    if (u <= f.lead)
        return leadOffset(u, f.spreadStart, f.lead, f.tail, f.lane, f.fanKnee, f.entryA);
    if (u >= f.len - f.leadB)
        return leadOffset(f.len - u, f.spreadEnd, f.leadB, f.tailB, f.laneB, f.fanKneeB, f.entryB);
    return inRun;
}

// How near the front of the bundle wire k is at distance u - the other half of going round, and what
// says which wire hides which where they cross.
double twistDepth(const TwistFrames& f, int k, double u) {
    // Running straight, no wire is ever in front of another - they lie side by side the whole way -
    // so the order is just the lanes, and it never changes along the run. Unless a pair of lanes is
    // LAID UP: those two do cross, and which of them is in front changes at every crossing, which is
    // the whole of what a twist looks like on paper.
    if (f.straight) {
        const int j = laneOf(f, k);
        if (laneTwinOf(f, j) < 0) return double(k);
        return double(k) + std::sin(lanePhase(f, j, u));
    }
    const double uu = std::clamp(u, f.lead, f.len - f.leadB);
    return std::sin(2.0 * M_PI * (uu - f.lead) / f.turn + twistPhase(f, k));
}

// One wire's edge, over the measured places [from, to].
//
// AN EDGE IS THE WIRE'S OWN LINE MOVED SIDEWAYS, and sideways means across THE WIRE.
//
// Both edges used to be offset along the normal to the RUN, stretched by one over the cosine of the
// slant to put back the width that that costs. The width came out right and nothing else did: the
// two edges land at different points along their own wire, so a slant that is dead straight is
// drawn with edges that are not, and a corner in the fan is drawn with a lip on it, because the
// stretch changes over the corner faster than the wire turns. Capping the stretch stopped the lips
// being spikes; it did not stop them being lips.
//
// So the wire's own line is walked first and then OFFSET, the way any line is offset: across each
// STEP of it, and at each place the mitre of the two steps meeting there - reaching out by one over
// the cosine of half the turn, which is what keeps both edges half a width from the wire on both
// sides of it. On a straight slant every mitre is one and the edges are exactly parallel to the
// wire, so a straight slant is drawn straight. Through a curve they turn with it and are no wider.
// Nothing is capped except the mitre itself, at four, so a wire doubling back is blunted rather
// than thrown out to infinity - and nothing spikes.
//
// The ENDS do not move, which is what this has to leave alone: a wire is landed square, so it runs
// parallel to the run where it ends and across the wire and across the run are the same direction
// there. The strand still finishes flush with the run it belongs to.
// Where wire k's middle is at the place measured at i - its lane, held back where the bend cannot
// carry it. Asked in one place because the edges are drawn off it and the lines across the wire are
// drawn off it, and the two must agree to the last figure or the end of a wire is drawn open.
Vec2 twistCentre(const TwistFrames& f, int k, size_t i) {
    if (i >= f.pos.size()) return {};
    double off = twistOffset(f, k, f.u[i]);
    if (i < f.holdHi.size()) off = std::clamp(off, f.holdLo[i], f.holdHi[i]);
    return f.pos[i] + f.n[i] * off;
}

std::vector<Vec2> twistEdge(const TwistFrames& f, int k, double edge, size_t from, size_t to) {
    std::vector<Vec2> line;
    line.reserve(to - from + 1);
    //
    // AND NO WIRE IS BENT BACKWARDS - see holdHi and holdLo, which say how far off the middle a wire
    // may sit at each place for the turn there to be able to carry it. That is in twistCentre.
    const size_t last = std::min(to, f.pos.size() - 1);
    if (f.pos.empty() || last < from) return line;
    // WHERE THE WIRE ACTUALLY IS, at each place, held back where the bend cannot carry it - and HOW
    // THICK IT IS DRAWN THERE, which is not the same all the way along.
    //
    // A STRIPPED END IS THINNER THAN THE WIRE. What is left when the insulation comes off is the
    // strands, and they are thinner than the wire was: that is the whole of how the eye tells the
    // bare part from the insulated part. So over the stripped length the edge runs in at the
    // conductor's half width, and at the place the insulation stops it STEPS - the same place
    // appears twice, once at each width, and the little line between the two of them is the end of
    // the insulation. One polyline, still, so a wire is still its two edges and everything that
    // walks them in pairs still can.
    std::vector<Vec2> mid;
    std::vector<double> half;
    mid.reserve(last - from + 3);
    half.reserve(last - from + 3);
    const double bare = f.thick * kWiresRunConductor;
    const double eps = std::max(1e-9, f.len * 1e-9);
    for (size_t i = from; i <= last; ++i) {
        const double u = f.u[i];
        const Vec2 at = twistCentre(f, k, i);
        const bool stripped = (f.strip > 1e-9 && u < f.strip - eps) ||
                              (f.stripB > 1e-9 && u > f.len - f.stripB + eps);
        // The step itself: at the start end the bare part is BEHIND this place, at the finish end it
        // is ahead, so which width comes first is which way the wire is being walked.
        const bool stepHere = (f.strip > 1e-9 && std::abs(u - f.strip) <= eps) ||
                              (f.stripB > 1e-9 && std::abs(u - (f.len - f.stripB)) <= eps);
        if (stepHere) {
            const bool bareFirst = std::abs(u - f.strip) <= eps;
            mid.push_back(at);
            half.push_back(bareFirst ? bare : f.thick);
            mid.push_back(at);
            half.push_back(bareFirst ? f.thick : bare);
            continue;
        }
        mid.push_back(at);
        half.push_back(stripped ? bare : f.thick);
    }
    const size_t count = mid.size();
    // Laid up, the offset stays along the run's normal exactly as it always was. A woven pair is a
    // cable of a stated width and the drawing of it is that width; the wires crossing inside it are
    // not landed on anything and their edges were never the thing being read.
    if (!f.straight || count < 2) {
        for (size_t j = 0; j < count; ++j) line.push_back(mid[j] + f.n[from + j] * (edge * half[j]));
        return line;
    }
    // WHICH WAY IS ACROSS EACH STEP of the wire, taken the same way round as the run's own normal so
    // that the two edges stay the same two edges they were.
    std::vector<Vec2> across(count - 1);
    for (size_t j = 0; j + 1 < count; ++j) {
        const Vec2 d = mid[j + 1] - mid[j];
        const double dl = d.length();
        across[j] = dl < 1e-12 ? (j > 0 ? across[j - 1] : f.n[from]) : (d / dl).perp();
    }
    for (size_t j = 0; j < count; ++j) {
        const Vec2& step = across[std::min(j, across.size() - 1)];
        Vec2 m = j == 0 ? across.front()
                 : j + 1 == count ? across.back()
                                  : across[j - 1] + across[j];
        const double ml = m.length();
        m = ml < 1e-12 ? step : m / ml;
        // THE MITRE: at a place where the wire turns, an edge has to reach further out than half a
        // width to stay half a width from the wire on both sides of the turn - by one over the
        // cosine of HALF the turn. Capped at four, so a wire doubling back on itself is blunted
        // rather than thrown out to infinity.
        const double cosHalf = std::max(0.25, dot(m, step));
        line.push_back(mid[j] + m * (edge * half[j] / cosHalf));
    }
    return line;
}

// What the pair is, kept from the last time it was worked out.
//
// Every mouse movement asks the same questions of the same objects: what does a click pick, what can be
// snapped to, what is on screen, how far does each object reach. Answered by measuring a woven pair
// again each time, a sheet with a few pairs on it made the cursor lag behind the mouse - the drawing was
// being re-derived dozens of times a move. So a pair is measured once and the answer kept until the pair
// itself changes, which is what the key below says.
struct TwistCached {
    TwistFrames frames;
    std::vector<std::vector<Vec2>> strands;
    std::vector<std::vector<Vec2>> caps;   // the line across each wire where it is cut
    std::vector<std::vector<Vec2>> body;   // and the converged stretch, where it is drawn as one
    // HOW MUCH OF THE RUN EACH WIRE IS DRAWN OVER, per wire, as ranges of measured places.
    //
    // Kept HERE because two different things ask it and they must not answer differently. What a
    // click picks and what a cursor snaps to come from the strands below; what is actually drawn
    // comes from twistedPairWeave. The weave worked it out for itself - and did not know about the
    // body at all, so a run with a cable on it was PICKED as a cable with its wires stopping at the
    // fans and DRAWN as the bare wires, running the whole length and splaying out from the boot face
    // to their full width in the middle. That is the fault in the drawing: a cable that is a cable to
    // everything except the eye.
    std::vector<std::vector<std::pair<size_t, size_t>>> spans;
    // THE LAID-UP STRETCH SEEN FROM THE SIDE, when the run asks for that look (twistLook "side"): each strand
    // on the near side of the bundle, closed outline by outline. Empty for the woven look.
    std::vector<std::vector<Vec2>> side;
    // What the cursor can catch on it, worked out the first time it is asked (see twistedPairSnap).
    mutable bool snapBuilt = false;
    mutable RunSnap snap;
};

// WHERE PLACE `u` ALONG THE RUN IS, and which way is across it there: between the two measured places either
// side of it, so a shape laid along a run that bends bends with it.
void frameAt(const TwistFrames& f, double u, Vec2& pos, Vec2& n) {
    const size_t count = std::min({f.u.size(), f.pos.size(), f.n.size()});
    if (count == 0) {
        pos = {};
        n = {0, 1};
        return;
    }
    if (u <= f.u.front() || count == 1) {
        pos = f.pos.front() + (f.pos.size() > 1 ? (f.pos[1] - f.pos[0]).normalized() * (u - f.u.front()) : Vec2{});
        n = f.n.front();
        return;
    }
    if (u >= f.u[count - 1]) {
        const Vec2 d = (f.pos[count - 1] - f.pos[count - 2]).normalized();
        pos = f.pos[count - 1] + d * (u - f.u[count - 1]);
        n = f.n[count - 1];
        return;
    }
    const size_t hi = static_cast<size_t>(std::upper_bound(f.u.begin(), f.u.begin() + count, u) - f.u.begin());
    const size_t lo = hi - 1;
    const double span = f.u[hi] - f.u[lo];
    const double t = span > 1e-12 ? (u - f.u[lo]) / span : 0.0;
    pos = lerp(f.pos[lo], f.pos[hi], t);
    const Vec2 m = lerp(f.n[lo], f.n[hi], t);
    n = m.lengthSq() > 1e-18 ? m.normalized() : f.n[lo];
}

// A LAID-UP BUNDLE SEEN FROM THE SIDE: the strands on the near side, all lying the same way, one beside the
// next - what a bundle looks like rather than a diagram of how its wires cross. Every strand is a straight
// band with a rounded end top and bottom, where it goes round the back of the bundle out of sight; the strands
// behind are not drawn at all. Laid out across the run (u along it, v across), then put on the route.
//
// One strand crosses the bundle from edge to edge in half a lay; with N wires laid up, the next one along is a
// lay over N further on. THE STRANDS TOUCH, as the wires of a laid-up bundle do: each is a wire thick, and the
// lay is the one that puts a wire's thickness between one strand and the next - so nothing of the strands
// behind is seen between them. A lay asked for that is already tighter than that keeps its own figure, and its
// strands share the room between them exactly instead.
std::vector<std::vector<Vec2>> sideViewStrands(const TwistFrames& f) {
    std::vector<std::vector<Vec2>> out;
    const double from = f.lead, till = f.len - f.leadB;
    if (f.straight || f.wires < 2 || f.turn <= 1e-9 || till - from <= 1e-9) return out;
    // HOW WIDE THE BUNDLE IS: as far out as any wire goes, over the laid-up stretch.
    double half = 0.0;
    for (size_t i = 0; i < f.u.size() && i < f.pos.size() && i < f.n.size(); ++i) {
        if (f.u[i] < from - 1e-9 || f.u[i] > till + 1e-9) continue;
        for (int wire = 0; wire < f.wires; ++wire)
            half = std::max(half, std::abs(dot(twistCentre(f, wire, i) - f.pos[i], f.n[i])) + f.thick);
    }
    if (half <= 1e-9) half = f.reach + f.thick;
    if (half <= 1e-9) return out;
    const double wire = 2.0 * f.thick;
    double lay = f.turn;
    // Between two neighbours, square to them, for a given lay: a lay over N along, of which the strand climbs
    // the whole width in half.
    auto roomFor = [&](double l) {
        const double a = l / 2.0;
        return (l / f.wires) * (2.0 * half) / std::sqrt(a * a + 4.0 * half * half);
    };
    double hw = roomFor(lay) / 2.0;
    if (wire > 1e-9 && roomFor(lay) > wire) {
        // The lay that leaves exactly a wire between neighbours: (2H/N)^2 l^2 = d^2 (l^2/4 + 4H^2).
        const double k = 4.0 * half * half / (f.wires * f.wires) - wire * wire / 4.0;
        if (k > 1e-12) {
            lay = 2.0 * half * wire / std::sqrt(k);
            hw = wire / 2.0;
        }
    }
    hw = std::max(0.05, std::min(hw, half * 0.95));
    const double along = lay / 2.0;                    // how far along one strand gets crossing the bundle
    const double step = lay / f.wires;                 // and how far on the next one is
    const double slope = std::sqrt(along * along + 4.0 * half * half);
    const Vec2 dir{along / slope, 2.0 * half / slope};  // up the strand, in (u, v)
    const Vec2 side{-dir.y, dir.x};
    const double reachV = half - hw;                    // where the middle of a rounded end sits
    if (reachV <= 1e-9) return out;
    const double alongEnd = reachV * along / half;      // how far along the strand's middle line goes
    // Clipped to the laid-up stretch: a strand that starts before it or runs on past it is cut where it ends.
    auto clip = [](std::vector<Vec2> poly, double bound, bool keepAbove) {
        std::vector<Vec2> kept;
        const size_t n = poly.size();
        for (size_t i = 0; i < n; ++i) {
            const Vec2 a = poly[i], b = poly[(i + 1) % n];
            const bool inA = keepAbove ? a.x >= bound : a.x <= bound;
            const bool inB = keepAbove ? b.x >= bound : b.x <= bound;
            if (inA) kept.push_back(a);
            if (inA != inB && std::abs(b.x - a.x) > 1e-12) {
                const double t = (bound - a.x) / (b.x - a.x);
                kept.push_back(lerp(a, b, t));
            }
        }
        return kept;
    };
    constexpr int kArc = 8;
    for (double s = from - along; s < till + step; s += step) {
        // The strand's middle line, from its lower end to its upper one, centred on s.
        const Vec2 lo{s - alongEnd / 2.0, -reachV}, hi{s + alongEnd / 2.0, reachV};
        std::vector<Vec2> band;
        // Up one side, round the top, down the other, round the bottom - each long side in pieces, so on a
        // route that bends it bends too.
        for (int k = 0; k <= 8; ++k) band.push_back(lerp(lo, hi, k / 8.0) - side * hw);
        for (int k = 1; k < kArc; ++k) {
            const double a = -M_PI / 2.0 + M_PI * k / kArc;
            band.push_back(hi + dir * (hw * std::cos(a)) + side * (hw * std::sin(a)));
        }
        for (int k = 0; k <= 8; ++k) band.push_back(lerp(hi, lo, k / 8.0) + side * hw);
        for (int k = 1; k < kArc; ++k) {
            const double a = M_PI / 2.0 + M_PI * k / kArc;
            band.push_back(lo + dir * (hw * std::cos(a)) + side * (hw * std::sin(a)));
        }
        band = clip(clip(band, from, true), till, false);
        if (band.size() < 3) continue;
        std::vector<Vec2> world;
        world.reserve(band.size() + 1);
        for (const Vec2& p : band) {
            Vec2 pos, n;
            frameAt(f, p.x, pos, n);
            world.push_back(pos + n * p.y);
        }
        world.push_back(world.front());   // closed
        out.push_back(std::move(world));
    }
    return out;
}

QString twistKey(const Entity& e) {
    QString k = e.id;
    k += QLatin1Char('|');
    const auto& path = e.points.size() >= 2 ? e.points : std::vector<Vec2>{e.a, e.b};
    for (const Vec2& p : path) {
        k += QString::number(p.x, 'g', 12);
        k += QLatin1Char(',');
        k += QString::number(p.y, 'g', 12);
        k += QLatin1Char(';');
    }
    // SAID AND NOT SAID ARE DIFFERENT THINGS, now that nought is a figure somebody can mean: a radius
    // of nought is a sharp corner and no radius at all is the drawing's own, and a key that wrote
    // both as "0" handed one of them the other one's shape out of the cache.
    auto put = [&](const char* m) {
        const QString name = QLatin1String(m);
        k += e.meta.contains(name) ? QString::number(e.meta.value(name).toDouble(), 'g', 12)
                                   : QStringLiteral("-");
        k += QLatin1Char('/');
    };
    for (const char* m : {"twistWidth", "twistPitch", "twistEnds", "twistWires"}) put(m);
    // How it is drawn: woven, or seen from the side. A shape, so a key - or the old look would be kept.
    k += QStringLiteral("|look:") + e.meta.value(QStringLiteral("twistLook")).toString();
    k += twistedPairSpread(e);
    k += e.meta.value(QLatin1String("twistStraight")).toBool() ? QLatin1String("|straight")
                                                                : QLatin1String("|laid");
    for (const char* m : {"twistLane", "twistWireWidth", "twistTrunkWires", "twistKnee",
                          "twistFanKnee", "twistLaneFinish", "twistEndsFinish",
                          "twistFanKneeFinish", "twistFlushLen", "twistFlushLenFinish",
                          "twistStripLen", "twistStripLenFinish", "twistBodyPitch"})
        put(m);
    // The transition on a body end, and how its free end is finished: both change the shape, so both
    // have to reach the key or a run keeps the shape it was last drawn with.
    k += e.meta.value(QLatin1String("twistEndBoot")).toBool() ? QLatin1String("|eb")
                                                              : QLatin1String("|e-");
    k += e.meta.value(QLatin1String("twistEndBootRecovered")).toBool(true) ? QLatin1String("r")
                                                                          : QLatin1String("s");
    k += e.meta.value(QLatin1String("twistEndBootSquareStart")).toBool() ? QLatin1String("|qs") : QLatin1String("|q-");
    k += e.meta.value(QLatin1String("twistEndBootSquareFinish")).toBool() ? QLatin1String("f") : QLatin1String("-");
    for (const char* m : {"twistFlush", "twistFlushFinish", "twistStrip", "twistStripFinish"})
        k += e.meta.contains(QLatin1String(m))
                 ? (e.meta.value(QLatin1String(m)).toBool() ? QLatin1String("|f1")
                                                            : QLatin1String("|f0"))
                 : QLatin1String("|f-");
    k += e.meta.value(QLatin1String("twistSidesSame")).toBool(true) ? QLatin1String("|same")
                                                                   : QLatin1String("|apart");
    k += e.meta.value(QLatin1String("twistBody")).toString();
    for (const char* m : {"twistBodyWidth", "twistBodyFace", "twistBodyShrink",
                          "twistBodyFaceFinish", "twistBodyShrinkFinish", "twistBoot",
                          "twistBootFinish", "twistBootWidth", "twistBootWidthFinish"})
        put(m);
    k += e.meta.value(QLatin1String("twistBodyTransition")).toBool(true) ? QLatin1String("|tr")
                                                                        : QLatin1String("|nt");
    k += e.meta.value(QLatin1String("twistBodyOverCable")).toBool() ? QLatin1String("|oc")
                                                                   : QLatin1String("|al");
    k += e.meta.value(QLatin1String("twistBodyTransitionSides")).toString();
    k += e.meta.value(QLatin1String("twistLay")).toString();
    k += e.meta.value(QLatin1String("twistBodySidesSame")).toBool(true) ? QLatin1String("|bs")
                                                                       : QLatin1String("|ba");
    return k;
}

const TwistCached* twistCached(const Entity& pair) {
    static QHash<QString, TwistCached> cache;
    const QString key = twistKey(pair);
    const auto it = cache.constFind(key);
    if (it != cache.constEnd()) return &*it;
    TwistCached c;
    if (!twistFrames(pair, c.frames)) return nullptr;
    const TwistFrames& f = c.frames;
    // HOW MUCH OF THE RUN EACH WIRE IS DRAWN OVER.
    //
    // Every wire of a laid-up bundle runs the whole length of it. A straight run is drawn the way
    // these drawings are drawn: the bundle carries the number of lines it takes to read as a bundle,
    // and a wire that is not one of them is drawn coming out of the end it is landed at, running into
    // the bundle, and stopping there. So it exists over the END it fans at, and nowhere else - and at
    // an end that does not fan there is nothing but the bundle itself.
    // The measured place a span starts or stops at. Asked for as AT OR BEFORE / AT OR AFTER, never as
    // "the one before": the middle of a straight run is not measured at all - it is one straight
    // stretch and needs nothing - so the place before the far end's fan is the NEAR end's fan, and a
    // span started there ran the whole way through the bundle instead of stopping at it.
    auto lastAtOrBefore = [&f](double u) {
        size_t best = 0;
        for (size_t i = 0; i < f.u.size(); ++i)
            if (f.u[i] <= u + 1e-9) best = i;
        return best;
    };
    auto firstAtOrAfter = [&f](double u) {
        for (size_t i = 0; i < f.u.size(); ++i)
            if (f.u[i] >= u - 1e-9) return i;
        return f.u.empty() ? size_t(0) : f.u.size() - 1;
    };
    // Where the insulation stops, as a measured place: twistFrames puts one exactly there, so this
    // finds it rather than settling for the nearest.
    auto placeAt = [&f](double u) {
        size_t best = f.pos.size();
        double bestOff = 1e300;
        for (size_t i = 0; i < f.u.size(); ++i) {
            const double off = std::abs(f.u[i] - u);
            if (off < bestOff) {
                bestOff = off;
                best = i;
            }
        }
        return bestOff <= std::max(1e-6, f.len * 1e-9) ? best : f.pos.size();
    };
    const size_t stripStart = f.strip > 1e-9 ? placeAt(f.strip) : f.pos.size();
    const size_t stripEnd = f.stripB > 1e-9 ? placeAt(f.len - f.stripB) : f.pos.size();
    // Where a distance along the run is, taken off the places it was measured at - which is all the
    // cover and its boots need: they sit on the run and on the fan, and both are measured often.
    auto posOn = [&f](double u) {
        if (f.pos.empty()) return Vec2{};
        const double uu = std::clamp(u, 0.0, f.len);
        for (size_t i = 1; i < f.u.size(); ++i) {
            if (f.u[i] < uu) continue;
            const double span = f.u[i] - f.u[i - 1];
            return span < 1e-12 ? f.pos[i]
                                : lerp(f.pos[i - 1], f.pos[i], (uu - f.u[i - 1]) / span);
        }
        return f.pos.back();
    };
    // CUT SQUARE, OR RECOVERED. A cable does not shrink onto anything and neither does the cable a
    // length of tubing is fitted OVER - both stop at a face square across them, and what takes
    // either down to the wires is the boot. A sleeve laid ALONG the run is the cover itself and is
    // recovered at each end, because there is nothing else there to do it.
    const bool squareBody = f.body == RunBody::Cable || f.bodyOverCable;
    // THE COVER IS ALWAYS DRAWN. A cable is a cable at every bend it is taken round.
    //
    // It used to be built here, LOOKED AT, and thrown away where a point of it had run too far from
    // the route - the run then being drawn as its bare wires instead. That was guarding against a
    // real thing: the two sides of a cover are MITRED offsets of its centre line, and an unclipped
    // mitre at a sharp corner meets a long way off the paper, so what came out was a line across the
    // whole drawing. But the cure was worse than the disease. A short last leg - which is every
    // knee, for the moment between clicking it and drawing away from it - tripped the test, and the
    // cable one was drawing turned into a fan of loose wires under the cursor and turned back again
    // when the leg grew. A part that comes and goes while you draw it is not a part.
    //
    // The mitre is clipped where it is made now (see offsetPolyline), so a cover cannot fly off the
    // sheet however the route doubles back, and there is nothing left to guard against.
    std::vector<Vec2> along;
    std::vector<Vec2> cover;
    bool drawBody = f.cableBody && f.pos.size() >= 2;
    if (drawBody) {
        // From the end of one fan to the start of the other - and to the very end of the run at an
        // end that does not fan, because there is no fan there for the cover to stop short of.
        //
        // Stopping short of the BOOT as well, where there is one. The boot sits on the end of the
        // cable and is shrunk down onto it, so the two meet at one face of one width; carry the
        // cable on under it as well and that face is drawn a second time, inside the boot, which on
        // the sheet is a line across a part for no reason at all.
        const double bodyFrom = f.spreadStart ? f.lead + f.bootA : f.endBootA;
        const double bodyTo = f.spreadEnd ? f.len - f.leadB - f.bootB : f.len - f.endBootB;
        if (bodyTo > bodyFrom + 1e-6) {
            along.push_back(posOn(bodyFrom));
            for (size_t i = 0; i < f.u.size() && i < f.pos.size(); ++i)
                if (f.u[i] > bodyFrom + 1e-9 && f.u[i] < bodyTo - 1e-9) along.push_back(f.pos[i]);
            along.push_back(posOn(bodyTo));
        }
        if (along.size() >= 2) {
            // A BODY END THAT HAS NOT RECOVERED is cut square at the cover's own width. A sleeve laid
            // along the run IS the cover, so what finishes it at an end with no fan is the one thing
            // there is to say about it: it has either shrunk down onto what comes out of it, or it
            // has been cut off square. (A cable is square at both ends whatever happens - what takes
            // it down to the wires is the boot - so this only ever reaches the sleeve.)
            const bool bareStart = !f.spreadStart && !f.endBootRecovered;
            const bool bareEnd = !f.spreadEnd && !f.endBootRecovered;
            const double faceA = bareStart ? f.bodyHalf : f.bodyFace;
            const double faceB = bareEnd ? f.bodyHalf : f.bodyFaceB;
            cover = squareBody
                        ? shrunkSleeveOutline(along, f.bodyHalf, f.bodyHalf, f.bodyHalf, 0.0, 0.0)
                        : shrunkSleeveOutline(along, f.bodyHalf, faceA, faceB,
                                              bareStart ? 0.0 : f.bodyShrink,
                                              bareEnd ? 0.0 : f.bodyShrinkB);
        }
        drawBody = cover.size() >= 3;
    }
    c.spans.assign(static_cast<size_t>(std::max(0, f.wires)), {});
    for (int k = 0; k < f.wires; ++k) {
        std::vector<std::pair<size_t, size_t>>& spans = c.spans[static_cast<size_t>(k)];
        const bool carriesOn =
            !f.straight || (k < static_cast<int>(f.trunkLane.size()) && f.trunkLane[size_t(k)] >= 0);
        // DRAWN AS A BODY, every wire stops at the fan: what runs between the two fans is the body,
        // and a wire drawn through it as well would be a wire drawn through a cable.
        if (carriesOn && !drawBody) {
            spans.push_back({0, f.pos.size() - 1});
        } else {
            // AND THE WHOLE FAN IS ON SHOW, boot or no boot. The transition sits back on the cable,
            // so what it covers is cable and not wires: every wire runs from its own end right up
            // to the face of the boot, and that face is where the fan visibly starts.
            if (f.spreadStart) spans.push_back({0, lastAtOrBefore(f.lead)});
            if (f.spreadEnd) spans.push_back({firstAtOrAfter(f.len - f.leadB), f.pos.size() - 1});
        }
        for (const auto& span : spans) {
            if (span.second <= span.first) continue;
            std::vector<Vec2> edgeA = twistEdge(f, k, 1.0, span.first, span.second);
            std::vector<Vec2> edgeB = twistEdge(f, k, -1.0, span.first, span.second);
            // THE CUT ENDS, at the ends of the RUN only. A wire that stops inside the bundle is not
            // cut there - it is carrying on inside the lines the bundle is drawn as - so it gets no
            // line across it, and a wire landed on a pin gets one.
            if (!edgeA.empty() && !edgeB.empty()) {
                // AND WHERE THE INSULATION ENDS, ONE LINE RIGHT ACROSS THE WIRE.
                //
                // The edges step in there, from the wire's half width to the conductor's, and those
                // two little steps are most of that end - but not the middle of it, where the bare
                // conductor passes through. Two stubs with a gap between them is not what the end of
                // an insulation looks like and is not something the cursor can catch: it is one line
                // across the wire, and it is drawn as one.
                auto acrossAt = [&](size_t i) {
                    const Vec2 mid = twistCentre(f, k, i);
                    return std::vector<Vec2>{mid + f.n[i] * f.thick, mid - f.n[i] * f.thick};
                };
                if (span.first == 0) {
                    c.caps.push_back({edgeA.front(), edgeB.front()});
                    if (f.strip > 1e-9 && stripStart < f.pos.size())
                        c.caps.push_back(acrossAt(stripStart));
                }
                if (span.second + 1 == f.pos.size()) {
                    c.caps.push_back({edgeA.back(), edgeB.back()});
                    if (f.stripB > 1e-9 && stripEnd < f.pos.size())
                        c.caps.push_back(acrossAt(stripEnd));
                }
            }
            c.strands.push_back(std::move(edgeA));
            c.strands.push_back(std::move(edgeB));
        }
    }
    // THE BODY OVER THE CONVERGED STRETCH, from the end of one fan to the start of the other. Its
    // own centre line is the route between those two places, knees and all, so a run that bends
    // bends its cable with it.
    if (drawBody) {
        {
            // A CABLE IS CUT SQUARE AND A HEATSHRINK IS RECOVERED. A cable running the length of the
            // run does not shrink onto anything: it is one width end to end and it stops at a face
            // square across it, and what takes it down to the wires is the BOOT at each end. A
            // length of tubing over the run has no boot and does shrink onto what it grips, which is
            // the face and the shrunk length.
            const bool square = squareBody;
            std::vector<Vec2> outline = cover;
            if (outline.size() >= 3) {
                outline.push_back(outline.front());   // closed: it is a body, not a run of lines
                c.body.push_back(std::move(outline));
            }
            // AND THE TRANSITION AT EACH END OF A CABLE: the boot that takes a bundle of wires down
            // to one cable.
            if (square) {
                // How far across the wires are at a place along the run - what a boot has to cover.
                auto spreadHalfAt = [&](double u) {
                    double most = 0.0;
                    for (int k = 0; k < f.wires; ++k)
                        most = std::max(most, std::abs(twistOffset(f, k, u)));
                    return most + f.thick;
                };
                // IT IS A SLEEVE OVER THE END OF THE CABLE. It is fitted over the cable, standing
                // proud of it, and it steps straight onto it at the end that sits on the body -
                // that end is not recovered, it is simply where the part begins. THE SHRUNK END IS
                // THE ONE FACING THE FAN, because that is the end that grips: a transition pulls in
                // onto what comes out of it. So the shrunk length is measured from the fan face
                // INWARD, and setting a longer one moves the shoulder toward the body while the
                // face stays where the wires leave.
                //
                // The step at the body end is drawn at the width of the cable, which is exactly
                // where the cable stops: the two faces are one line, drawn twice in the same place,
                // rather than a short line nested inside a tall one.
                auto bootAt = [&](double atFan, double reach, double face, double shrink,
                                  double intoTheBody) {
                    if (reach < 1e-6) return;
                    const double onCable = atFan + intoTheBody * reach;
                    // Wide enough to read as a part fitted over the cable, and wide enough to take
                    // the wires where they gather into it.
                    const double said = intoTheBody > 0.0 ? f.bootHalf : f.bootHalfB;
                    const double mouth = said > 0.0 ? said : std::max(f.bodyHalf * 1.25, spreadHalfAt(atFan));
                    std::vector<Vec2> run{posOn(atFan), posOn(onCable)};
                    std::vector<Vec2> boot = shrunkSleeveOutline(run, mouth, std::min(face, mouth),
                                                                 f.bodyHalf, shrink, 0.0);
                    if (boot.size() < 3) return;
                    boot.push_back(boot.front());
                    c.body.push_back(std::move(boot));
                };
                bootAt(f.lead, f.bootA, f.bodyFace, f.bodyShrink, +1.0);
                bootAt(f.len - f.leadB, f.bootB, f.bodyFaceB, f.bodyShrinkB, -1.0);
                // THE ONE ON A BODY END, which has no fan to open onto. Its free end is the end of
                // the part: RECOVERED, where it has shrunk down onto the cable coming out of it, or
                // CUT SQUARE across at its own width. The other end is the cable's width, as the
                // fan boots' are, so cable and boot meet on one line.
                // CUT SQUARE AT ONE END ONLY where it is said: carried on over the part there by another piece.
                const bool squareA = pair.meta.value(QStringLiteral("twistEndBootSquareStart")).toBool();
                const bool squareB = pair.meta.value(QStringLiteral("twistEndBootSquareFinish")).toBool();
                auto endBootAt = [&](double atEnd, double reach, double face, double shrink,
                                     double intoTheBody) {
                    if (reach < 1e-6) return;
                    const double said = intoTheBody > 0.0 ? f.bootHalf : f.bootHalfB;
                    const double wide = said > 0.0 ? said : f.bodyHalf * 1.25;
                    const bool recovered = f.endBootRecovered && !(intoTheBody > 0.0 ? squareA : squareB);
                    const double free = recovered ? std::min(face, wide) : wide;
                    std::vector<Vec2> run{posOn(atEnd), posOn(atEnd + intoTheBody * reach)};
                    std::vector<Vec2> boot = shrunkSleeveOutline(
                        run, wide, free, f.bodyHalf, recovered ? shrink : 0.0, 0.0);
                    if (boot.size() < 3) return;
                    boot.push_back(boot.front());
                    c.body.push_back(std::move(boot));
                };
                endBootAt(0.0, f.endBootA, f.bodyFace, f.bodyShrink, +1.0);
                endBootAt(f.len, f.endBootB, f.bodyFaceB, f.bodyShrinkB, -1.0);
            }
        }
    }
    if (pair.meta.value(QStringLiteral("twistLook")).toString() == QLatin1String("side")) c.side = sideViewStrands(f);
    // Bounded: a session that draws and re-draws pairs all day does not keep every one of them.
    if (cache.size() > 64) cache.clear();
    return &*cache.insert(key, std::move(c));
}

} // namespace

std::vector<std::vector<Vec2>> twistedPairStrands(const Entity& pair) {
    if (pair.type != EntityType::TwistedPair) return {};
    const TwistCached* c = twistCached(pair);
    return c ? c->strands : std::vector<std::vector<Vec2>>{};
}

std::vector<std::vector<Vec2>> twistedPairEndCaps(const Entity& pair) {
    if (pair.type != EntityType::TwistedPair) return {};
    const TwistCached* c = twistCached(pair);
    return c ? c->caps : std::vector<std::vector<Vec2>>{};
}

std::vector<std::vector<Vec2>> twistedPairEndSleeves(const Entity& pair) {
    if (pair.type != EntityType::TwistedPair || !pair.meta.value(QStringLiteral("twistEndShrink")).toBool()) return {};
    const TwistCached* c = twistCached(pair);
    if (!c) return {};
    const TwistFrames& f = c->frames;
    const double len = std::max(0.5, pair.meta.value(QStringLiteral("twistEndShrinkLen")).toDouble(10.0));
    const double half = f.thick + 0.5;
    std::vector<std::vector<Vec2>> out;
    auto sleeve = [&](std::vector<Vec2> path, bool atEnd) {
        if (path.size() < 2) return;
        if (atEnd) std::reverse(path.begin(), path.end());
        // The last `len` of the wire, from its end.
        std::vector<Vec2> piece{path.front()};
        double acc = 0.0;
        for (size_t k = 1; k < path.size() && acc < len; ++k) {
            const double seg = distance(path[k - 1], path[k]);
            if (seg < 1e-9) continue;
            if (acc + seg >= len) {
                piece.push_back(path[k - 1] + (path[k] - path[k - 1]).normalized() * (len - acc));
                acc = len;
            } else {
                piece.push_back(path[k]);
                acc += seg;
            }
        }
        if (piece.size() < 2) return;
        std::vector<Vec2> ring = offsetPolyline(piece, half);
        const std::vector<Vec2> other = offsetPolyline(piece, -half);
        ring.insert(ring.end(), other.rbegin(), other.rend());
        ring.push_back(ring.front());
        out.push_back(std::move(ring));
    };
    // Each wire is two of the strands, its two edges: its middle is between them.
    for (size_t j = 0; j + 1 < c->strands.size() && j / 2 < static_cast<size_t>(f.wires); j += 2) {
        const auto& ea = c->strands[j];
        const auto& eb = c->strands[j + 1];
        if (ea.size() != eb.size() || ea.size() < 2) continue;
        std::vector<Vec2> mid(ea.size());
        for (size_t q = 0; q < ea.size(); ++q) mid[q] = (ea[q] + eb[q]) * 0.5;
        if (f.spreadStart) sleeve(mid, false);
        if (f.spreadEnd) sleeve(mid, true);
    }
    return out;
}

std::vector<std::vector<Vec2>> twistedPairSideView(const Entity& pair) {
    if (pair.type != EntityType::TwistedPair) return {};
    const TwistCached* c = twistCached(pair);
    return c ? c->side : std::vector<std::vector<Vec2>>{};
}

std::vector<std::vector<Vec2>> twistedPairBody(const Entity& pair) {
    if (pair.type != EntityType::TwistedPair) return {};
    const TwistCached* c = twistCached(pair);
    return c ? c->body : std::vector<std::vector<Vec2>>{};
}

std::vector<std::vector<Vec2>> runDrawnLines(const Entity& pair) {
    std::vector<std::vector<Vec2>> out = twistedPairStrands(pair);
    for (std::vector<Vec2>& outline : twistedPairBody(pair)) out.push_back(std::move(outline));
    return out;
}

RunEnds runTransitionEndsOf(const Entity& pair) {
    // The older drawings said it with a tick, and a tick that was off meant neither end. Read that
    // way round still, so a cable drawn before there was a choice opens as the cable it was.
    const QString said =
        pair.meta.value(QStringLiteral("twistBodyTransitionSides")).toString().toLower();
    if (said == QLatin1String("none")) return RunEnds::None;
    if (said == QLatin1String("start")) return RunEnds::Start;
    if (said == QLatin1String("finish")) return RunEnds::Finish;
    if (said == QLatin1String("both")) return RunEnds::Both;
    return pair.meta.value(QStringLiteral("twistBodyTransition")).toBool(true) ? RunEnds::Both
                                                                              : RunEnds::None;
}

bool runEndsHas(RunEnds ends, bool finish) {
    return ends == RunEnds::Both || ends == (finish ? RunEnds::Finish : RunEnds::Start);
}

RunLay runLayOf(const Entity& pair) {
    const QString said = pair.meta.value(QStringLiteral("twistLay")).toString().toLower();
    if (said == QLatin1String("pairs")) return RunLay::Pairs;
    if (said == QLatin1String("mixed")) return RunLay::Mixed;
    return RunLay::Straight;
}

RunBody runBodyOf(const Entity& pair) {
    const QString said = pair.meta.value(QStringLiteral("twistBody")).toString();
    if (said == QLatin1String("cable")) return RunBody::Cable;
    if (said == QLatin1String("heatshrink")) return RunBody::Heatshrink;
    return RunBody::Wires;
}

bool runRouteCarries(const std::vector<Vec2>& route, double bundleWidth, double fanStart,
                     double fanEnd, QString* why) {
    auto no = [why](const QString& reason) {
        if (why) *why = reason;
        return false;
    };
    if (route.size() < 2) return true;
    const double half = std::max(0.05, bundleWidth / 2.0);
    // A LEG SHORTER THAN THE BUNDLE IS WIDE is not a leg: there is nowhere in it for the bundle to
    // be straight, and two corners' worth of bend would have to share it.
    for (size_t i = 1; i < route.size(); ++i) {
        const double leg = distance(route[i - 1], route[i]);
        if (leg < bundleWidth)
            return no(QStringLiteral("a %1 mm leg is shorter than the %2 mm bundle is wide")
                          .arg(leg, 0, 'f', 1)
                          .arg(bundleWidth, 0, 'f', 1));
    }
    // AND A CORNER HAS TO HAVE ROOM FOR THE BEND. The tightest the bundle may be bent is half its
    // own width; that bend eats R/tan(T/2) of the straight either side of the corner, and it may
    // not eat more than half of either leg - the other half belongs to whatever is at the far end
    // of it.
    for (size_t i = 1; i + 1 < route.size(); ++i) {
        const Vec2 back = route[i - 1] - route[i], on = route[i + 1] - route[i];
        const double lb = back.length(), lo = on.length();
        if (lb < 1e-9 || lo < 1e-9) continue;
        const double turn = std::acos(std::clamp(dot(back / lb, on / lo), -1.0, 1.0));
        if (turn > M_PI - 1e-3) continue;   // straight through: no corner at all
        const double eats = half / std::max(1e-6, std::tan(turn / 2.0));
        if (eats > 0.5 * std::min(lb, lo))
            return no(QStringLiteral("a %1 degree turn is too sharp for a %2 mm bundle here")
                          .arg(turn * 180.0 / M_PI, 0, 'f', 0)
                          .arg(bundleWidth, 0, 'f', 1));
    }
    // AND THE FAN AT EACH END NEEDS ITS OWN STRAIGHT. The wires open out over the first stretch of
    // the run and that stretch has to BE a stretch: a knee inside it bends the fan-out itself, where
    // every wire is at its furthest from the route and each is turning on a radius of its own. A fan
    // drawn across a corner is the worst of the two things this refuses, and it is the one that
    // looks like a mistake rather than like a tight bend.
    if (route.size() > 2) {
        const double first = distance(route[0], route[1]);
        const double last = distance(route[route.size() - 2], route.back());
        if (fanStart > 1e-9 && first < fanStart)
            return no(QStringLiteral("the %1 mm fan does not fit in a %2 mm first leg")
                          .arg(fanStart, 0, 'f', 1)
                          .arg(first, 0, 'f', 1));
        if (fanEnd > 1e-9 && last < fanEnd)
            return no(QStringLiteral("the %1 mm fan does not fit in a %2 mm last leg")
                          .arg(fanEnd, 0, 'f', 1)
                          .arg(last, 0, 'f', 1));
    }
    return true;
}

TwistFigures twistedPairFigures(const Entity& pair) {
    TwistFigures out;
    if (pair.type != EntityType::TwistedPair) return out;
    const TwistCached* c = twistCached(pair);
    if (!c) return out;
    const TwistFrames& f = c->frames;
    out.ok = true;
    out.lane = f.lane;
    out.fanLen = f.lead;
    out.fanLenFinish = f.leadB;
    out.turn = f.turn;
    out.wireWidth = f.thick * 2.0;
    out.kneeRadius = f.kneeUsed;
    out.fanKnee = f.fanKneeUsed;
    out.flushLen = f.tail;
    out.stripLen = f.strip;
    out.trunk = f.trunk;
    out.bodyWidth = f.bodyHalf * 2.0;
    out.bodyFace = f.bodyFace * 2.0;
    out.bodyShrink = f.bodyShrink;
    out.boot = f.bootA;
    out.packed = f.packed;
    return out;
}

std::vector<std::pair<Vec2, Vec2>> twistedPairStraights(const Entity& pair) {
    std::vector<std::pair<Vec2, Vec2>> out;
    if (pair.type != EntityType::TwistedPair) return out;
    const TwistCached* c = twistCached(pair);
    // A LAID-UP BUNDLE HAS NO STRAIGHT IN IT TO SPEAK OF. The weave is a cosine drawn as a few
    // hundred little facets, and none of them is a stretch of anything: taking every one of them for
    // a straight would hand the snap system hundreds of segments per pair, each with a midpoint and
    // a perpendicular foot and a place in every crossing test, which is what it used to cost.
    if (!c || !c->frames.straight) return out;
    const TwistFrames& f = c->frames;
    // Straight to within a hundred-thousandth of the run, which a drawn arc's facets are not - they
    // stand off their own chord by a good fraction of a wire.
    //
    // AND LONG ENOUGH TO BE A STRETCH OF ANYTHING, which is a wire and a half - short enough that a
    // slant on a small fan is one, long enough that the ten little facets a fillet is drawn as are
    // not. A floor of four wires was leaving the short ones out, and a short slant is a line on the
    // drawing like any other.
    const double tol = std::max(1e-9, f.len * 1e-5);
    const double atLeast = std::max(f.thick * 3.0, f.len * 0.004);
    // EVERYTHING THE RUN DRAWS, not only its wires. Where the stretch between the fans is a cable or
    // a length of tubing there are no wires along it at all - the body is what is there, and it was
    // the one part of such a run with nothing to catch and nothing to take hold of. A body is
    // mostly straight line like everything else here.
    std::vector<std::vector<Vec2>> drawn = c->strands;
    for (const std::vector<Vec2>& outline : c->body) drawn.push_back(outline);
    for (const std::vector<Vec2>& line : drawn) {
        if (line.size() < 2) continue;
        size_t from = 0;
        auto keep = [&](size_t a, size_t b) {
            if (b > a && distance(line[a], line[b]) >= atLeast)
                out.emplace_back(line[a], line[b]);
        };
        for (size_t i = 2; i < line.size(); ++i) {
            // Does everything between where this stretch started and here still lie on the line
            // between the two of them? The moment one does not, the stretch ended at the one before.
            bool onIt = distance(line[from], line[i]) > 1e-12;
            for (size_t j = from + 1; onIt && j < i; ++j)
                onIt = closestPointOnSegment(line[j], line[from], line[i]).distance <= tol;
            if (!onIt) {
                keep(from, i - 1);
                from = i - 1;
            }
        }
        keep(from, line.size() - 1);
    }
    return out;
}

const RunSnap* twistedPairSnap(const Entity& pair) {
    if (pair.type != EntityType::TwistedPair) return nullptr;
    const TwistCached* c = twistCached(pair);
    if (!c) return nullptr;
    if (c->snapBuilt) return c->snap.lines.empty() ? nullptr : &c->snap;
    c->snapBuilt = true;
    RunSnap& s = c->snap;
    const TwistFrames& f = c->frames;
    std::vector<std::vector<Vec2>> drawn = c->strands;
    // SEEN FROM THE SIDE, the laid-up stretch is its strands' outlines and not the weave: the woven lines are
    // kept only where they are still drawn, outside it - the fans and the straight ends.
    if (!c->side.empty()) {
        drawn.clear();
        auto placeOf = [&f](const Vec2& p) {
            double bestU = 0.0, best = 1e300;
            for (size_t i = 1; i < f.pos.size() && i < f.u.size(); ++i) {
                const auto hit = closestPointOnSegment(p, f.pos[i - 1], f.pos[i]);
                if (hit.distance < best) {
                    best = hit.distance;
                    bestU = f.u[i - 1] + (f.u[i] - f.u[i - 1]) * hit.t;
                }
            }
            return bestU;
        };
        for (const std::vector<Vec2>& strand : c->strands) {
            std::vector<Vec2> piece;
            for (size_t i = 0; i < strand.size(); ++i) {
                const double u = placeOf(strand[i]);
                if (u > f.lead + 1e-9 && u < f.len - f.leadB - 1e-9) {
                    if (!piece.empty()) piece.push_back(strand[i]);   // to the edge of the stretch
                    if (piece.size() >= 2) drawn.push_back(piece);
                    piece.clear();
                } else {
                    if (piece.empty() && i > 0) piece.push_back(strand[i - 1]);
                    piece.push_back(strand[i]);
                }
            }
            if (piece.size() >= 2) drawn.push_back(piece);
        }
        for (const std::vector<Vec2>& band : c->side) drawn.push_back(band);
    }
    for (const std::vector<Vec2>& cap : c->caps) drawn.push_back(cap);
    for (const std::vector<Vec2>& outline : c->body) drawn.push_back(outline);
    // A LAID-UP BUNDLE'S OUTLINE, the line along the outside of its outer strands - drawn, so caught.
    // Worked out as twistedPairWeave draws it.
    if (!f.straight && f.wires >= 3 && !f.cableBody && c->side.empty()) {
        std::vector<Vec2> high, low;
        for (size_t i = 0; i < f.u.size() && i < f.pos.size() && i < f.n.size(); ++i) {
            if (f.u[i] < f.lead - 1e-9 || f.u[i] > f.len - f.leadB + 1e-9) continue;
            double hi = -1e18, lo = 1e18;
            for (int wire = 0; wire < f.wires; ++wire) {
                const double offset = dot(twistCentre(f, wire, i) - f.pos[i], f.n[i]);
                hi = std::max(hi, offset + f.thick);
                lo = std::min(lo, offset - f.thick);
            }
            high.push_back(f.pos[i] + f.n[i] * hi);
            low.push_back(f.pos[i] + f.n[i] * lo);
        }
        drawn.push_back(std::move(high));
        drawn.push_back(std::move(low));
    }
    // A CORNER is where a drawn line turns by more than a facet of a curve ever does: a fan's knee, the
    // step where the insulation stops, the corner of a boot. A curve's own facets are not corners - a
    // weave would otherwise offer a point every few hundredths of a millimetre.
    constexpr double kCornerCos = 0.94;   // about 20 degrees
    bool first = true;
    auto reach = [&](const Vec2& p) {
        if (first) {
            s.lo = s.hi = p;
            first = false;
        }
        s.lo.x = std::min(s.lo.x, p.x);
        s.lo.y = std::min(s.lo.y, p.y);
        s.hi.x = std::max(s.hi.x, p.x);
        s.hi.y = std::max(s.hi.y, p.y);
    };
    for (const std::vector<Vec2>& line : drawn) {
        if (line.size() < 2) continue;
        s.points.push_back(line.front());
        s.points.push_back(line.back());
        Vec2 lastDir{0.0, 0.0};
        for (size_t i = 1; i < line.size(); ++i) {
            reach(line[i - 1]);
            reach(line[i]);
            const Vec2 d = line[i] - line[i - 1];
            const double len = d.length();
            if (len < 1e-9) continue;
            s.lines.emplace_back(line[i - 1], line[i]);
            const Vec2 dir = d / len;
            if (lastDir.lengthSq() > 0.0 && dot(lastDir, dir) < kCornerCos) s.points.push_back(line[i - 1]);
            lastDir = dir;
        }
    }
    // Far enough round it to take in the heatshrink a wire's end may have on it (twistedPairEndSleeves).
    const double margin = f.thick + 1.0;
    s.lo = s.lo - Vec2{margin, margin};
    s.hi = s.hi + Vec2{margin, margin};
    return s.lines.empty() ? nullptr : &s;
}

QString twistedPairSpread(const Entity& pair) {
    const QString spread =
        pair.meta.value(QStringLiteral("twistSpread")).toString(QStringLiteral("both")).toLower();
    // Two ways of naming one end, and they are not the same way. LEFT and RIGHT are sheet
    // directions - which end of the run is further left on the paper - and a run drawn backwards
    // still fans on the side asked for. START and FINISH are the order it was DRAWN in, which is
    // what somebody means when they say the run leaves the connector and arrives at a cable: the
    // first end they clicked, whichever way it points.
    if (spread == QLatin1String("left") || spread == QLatin1String("right") ||
        spread == QLatin1String("start") || spread == QLatin1String("finish"))
        return spread;
    return QStringLiteral("both");
}

std::vector<TwistedPairStretch> twistedPairWeave(const Entity& pair) {
    if (pair.type != EntityType::TwistedPair) return {};
    const TwistCached* cached = twistCached(pair);
    if (!cached) return {};
    const TwistFrames& f = cached->frames;
    // Split every half turn - where the wires have just changed places - so which of them is in front
    // is settled once per stretch, and changes where they are furthest apart. A straight run is ONE
    // stretch from end to end: nothing crosses, so there is no place along it where the answer to
    // which wire is in front changes, and cutting it up anyway would only draw the same wires in the
    // same order several times over.
    //
    // EVERY WIRE HAS A PROJECTED HELIX. Depth order gives each crossing its front strand;
    // the far strand remains visible wherever it is not covered by one nearer the viewer.
    const bool rope = !f.straight && f.wires >= 3;
    std::vector<double> cuts{0.0};
    if (!f.straight || f.lay != RunLay::Straight) {
        cuts.push_back(f.lead);
        const int perTurn = rope ? 4 * f.wires : 2;
        for (int k = 1; k < perTurn * f.turns; ++k)
            cuts.push_back(f.lead + f.turn * k / double(perTurn));
        cuts.push_back(f.len - f.leadB);
    }
    std::sort(cuts.begin(), cuts.end());
    cuts.push_back(f.len);
    cuts.erase(std::unique(cuts.begin(), cuts.end(),
                           [&](double a, double b) { return std::abs(a - b) < f.len * 1e-9; }),
               cuts.end());

    std::vector<TwistedPairStretch> out;
    size_t idx = 0;
    for (size_t c = 0; c + 1 < cuts.size(); ++c) {
        while (idx + 1 < f.u.size() && f.u[idx] < cuts[c] - f.len * 1e-9) ++idx;
        size_t to = idx;
        while (to + 1 < f.u.size() && f.u[to + 1] <= cuts[c + 1] + f.len * 1e-9) ++to;
        if (to <= idx) continue;
        // Which wire is nearest the front over this stretch, taken at the middle of it.
        const double mid = (cuts[c] + cuts[c + 1]) / 2.0;
        std::vector<int> order(static_cast<size_t>(f.wires));
        for (int k = 0; k < f.wires; ++k) order[static_cast<size_t>(k)] = k;
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            return twistDepth(f, a, mid) < twistDepth(f, b, mid);
        });
        TwistedPairStretch s;
        s.wires.reserve(order.size());
        // WITHIN THE LAID-UP STRETCH, which a side view draws in its own way (twistedPairSideView).
        s.laid = !f.straight && mid > f.lead - 1e-9 && mid < f.len - f.leadB + 1e-9;
        for (const int k : order) {   // back to front, so the near ones are laid over the far ones
            // ONLY OVER THE PART OF THE RUN THIS WIRE IS DRAWN ON - the same answer the strands are
            // made from, kept once (see TwistCached::spans). Taking the whole stretch instead drew
            // every wire from one end of the run to the other, body or no body.
            const auto& spans = size_t(k) < cached->spans.size()
                                    ? cached->spans[size_t(k)]
                                    : std::vector<std::pair<size_t, size_t>>{};
            for (const auto& span : spans) {
                // The part of this wire that falls in this stretch. For a straight run there is one
                // stretch and this is simply the span; a laid-up bundle is cut every half crossing
                // and each piece of the wire belongs to the stretch it lies in.
                const size_t from = std::max(span.first, idx);
                const size_t till = std::min(span.second, to);
                if (till <= from) continue;
                TwistedPairStretch::Wire wire;
                wire.edgeA = twistEdge(f, k, 1.0, from, till);
                wire.edgeB = twistEdge(f, k, -1.0, from, till);
                wire.outline = wire.edgeA;
                for (auto it = wire.edgeB.rbegin(); it != wire.edgeB.rend(); ++it)
                    wire.outline.push_back(*it);
                s.wires.push_back(std::move(wire));
            }
        }
        out.push_back(std::move(s));
        idx = to;
    }
    // THE BUNDLE'S SILHOUETTE follows the outer strands as they wind round the route.
    // Drawn last, it finishes the outline without hiding the wire crossings inside.
    if (rope && !f.cableBody) {
        TwistedPairStretch side;
        side.laid = true;
        TwistedPairStretch::Wire edges;
        // THE ENVELOPE FOLLOWS THE STRANDS instead of being two straight rails. Its shallow
        // scallops follow the lay, and the held wire positions keep it inside a tight routed knee.
        for (size_t i = 0; i < f.u.size() && i < f.pos.size() && i < f.n.size(); ++i) {
            if (f.u[i] < f.lead - 1e-9 || f.u[i] > f.len - f.leadB + 1e-9) continue;
            double high = -1e18, low = 1e18;
            for (int wire = 0; wire < f.wires; ++wire) {
                const double offset = dot(twistCentre(f, wire, i) - f.pos[i], f.n[i]);
                high = std::max(high, offset + f.thick);
                low = std::min(low, offset - f.thick);
            }
            edges.edgeA.push_back(f.pos[i] + f.n[i] * high);
            edges.edgeB.push_back(f.pos[i] + f.n[i] * low);
        }
        if (edges.edgeA.size() >= 2) {
            side.wires.push_back(std::move(edges));
            out.push_back(std::move(side));
        }
    }
    return out;
}

std::vector<Vec2> runCenterline(const Entity& e) {
    switch (e.type) {
    case EntityType::Line:
    case EntityType::DoubleLine:
        return {e.a, e.b};
    case EntityType::Wire:
    case EntityType::Polyline:
    case EntityType::Spline:
        return e.points;
    default:
        return {};
    }
}

bool runPointAtFraction(const Entity& e, double t, Vec2& pos, Vec2& dir) {
    Entity probe;
    probe.type = EntityType::Wire;
    probe.points = runCenterline(e);
    if (probe.points.size() < 2) return false;
    return wirePointAtFraction(probe, t, pos, dir);
}

double runFractionNearest(const Entity& e, const Vec2& p) {
    Entity probe;
    probe.type = EntityType::Wire;
    probe.points = runCenterline(e);
    if (probe.points.size() < 2) return 0.0;
    return wireFractionNearest(probe, p);
}

double wireFractionNearest(const Entity& wire, const Vec2& p) {
    const std::vector<Vec2>& pts = wire.points;
    if (pts.size() < 2) return 0.0;
    double total = 0.0;
    for (size_t i = 1; i < pts.size(); ++i) total += distance(pts[i - 1], pts[i]);
    if (total < 1e-9) return 0.0;
    double walked = 0.0, bestDist = 1e300, bestAt = 0.0;
    for (size_t i = 1; i < pts.size(); ++i) {
        const Vec2 a = pts[i - 1], b = pts[i];
        const double seg = distance(a, b);
        if (seg < 1e-12) continue;
        const Vec2 ab = b - a;
        double f = dot(p - a, ab) / (seg * seg);
        f = std::clamp(f, 0.0, 1.0);
        const Vec2 on = lerp(a, b, f);
        const double d = distance(p, on);
        if (d < bestDist) { bestDist = d; bestAt = walked + f * seg; }
        walked += seg;
    }
    return std::clamp(bestAt / total, 0.0, 1.0);
}

std::vector<double> wireAnnotFractions(const Entity& wire, int count) {
    count = std::max(0, count);
    std::vector<double> ts;
    for (const auto& v : wire.meta.value(QStringLiteral("annT")).toArray())
        ts.push_back(std::clamp(v.toDouble(), 0.0, 1.0));
    // Missing entries fall back to an even spread, so raising the count just adds
    // instances without disturbing the ones already positioned by hand.
    for (int i = static_cast<int>(ts.size()); i < count; ++i)
        ts.push_back(count == 1 ? 0.5 : (i + 0.5) / count);
    ts.resize(count);
    return ts;
}

void setWireAnnotFractions(Entity& wire, const std::vector<double>& ts) {
    QJsonArray arr;
    for (double t : ts) arr.append(std::clamp(t, 0.0, 1.0));
    wire.meta.insert(QStringLiteral("annT"), arr);
}

int dragWireEndKeepingRightAngle(Entity& wire, int endIdx, const Vec2& delta) {
    constexpr double kSq = 1e-6; // tolerance for "this run is axis-aligned"
    const int n = static_cast<int>(wire.points.size());
    if (n < 1 || (endIdx != 0 && endIdx != n - 1)) return -1;
    const Vec2 before = wire.points[endIdx];
    wire.points[endIdx] = before + delta;
    // A straight two-point run has no knee to keep square, so a move with any component
    // ACROSS it would leave the whole wire diagonal. Give it a knee instead: break the run
    // into H-V-H (or V-H-V) so it stays 0/90 — a schematic wire has to leave its pin square,
    // and without this every connector move slowly turned the drawing into diagonals.
    if (n == 2) {
        const int fixedIdx = (endIdx == 0) ? 1 : 0;
        const Vec2 moved = wire.points[endIdx];
        const Vec2 fixed = wire.points[fixedIdx];
        const bool wasHoriz = std::abs(before.y - fixed.y) < kSq;
        const bool wasVert = std::abs(before.x - fixed.x) < kSq;
        const bool nowDiagonal =
            std::abs(moved.y - fixed.y) > kSq && std::abs(moved.x - fixed.x) > kSq;
        if ((wasHoriz || wasVert) && nowDiagonal) {
            // Leave the pin the way the run always left it, turn in the middle, and arrive
            // at the far end the same way.
            Vec2 k1, k2;
            if (wasHoriz) {
                const double midX = (moved.x + fixed.x) / 2.0;
                k1 = {midX, moved.y};
                k2 = {midX, fixed.y};
            } else {
                const double midY = (moved.y + fixed.y) / 2.0;
                k1 = {moved.x, midY};
                k2 = {fixed.x, midY};
            }
            if (endIdx == 0) wire.points = {moved, k1, k2, fixed};
            else wire.points = {fixed, k2, k1, moved};
            return (endIdx == 0) ? 1 : 2; // the knee now beside the moved end
        }
        return -1; // still straight (the move ran along the wire): nothing to do
    }
    if (n < 3) return -1; // a single point: nothing to keep square
    const int knee = (endIdx == 0) ? 1 : n - 2;
    if (knee <= 0 || knee >= n - 1) return -1;
    if (std::abs(before.y - wire.points[knee].y) < kSq) {
        wire.points[knee].y = wire.points[endIdx].y; // the run left the pin horizontally
        return knee;
    }
    if (std::abs(before.x - wire.points[knee].x) < kSq) {
        wire.points[knee].x = wire.points[endIdx].x; // ...or vertically
        return knee;
    }
    return -1; // diagonal run: no right angle to hold
}

QStringList wireAnnotParts() {
    // Bottom row first: the cross-section sits nearest the wire, then the colour, then the label.
    return {QStringLiteral("gauge"), QStringLiteral("color"), QStringLiteral("label")};
}

namespace {
// "color" -> "Color", for the per-part meta keys (annCountColor, annTColor, ...).
QString partKey(const QString& part) {
    QString k = part;
    if (!k.isEmpty()) k[0] = k[0].toUpper();
    return k;
}
} // namespace

QString wireAnnotPartText(const Entity& wire, const QString& part) {
    if (part == QLatin1String("color")) return wireAnnotColorText(wire);
    if (part == QLatin1String("gauge")) return wireAnnotGaugeText(wire);
    if (part != QLatin1String("label")) return {};
    if (!wire.meta.value(QStringLiteral("showWireLabel")).toBool(true)) return {};
    return wire.meta.value(QStringLiteral("wireLabel")).toString();
}

int wireAnnotCountFor(const Entity& wire, const QString& part) {
    if (wireAnnotPartText(wire, part).isEmpty()) return 0;
    const QString key = QStringLiteral("annCount") + partKey(part);
    // Its own amount if it has been set; otherwise the shared one a drawing made before the
    // markings were separated (and 1 for the label, which was drawn once).
    if (wire.meta.contains(key)) return std::clamp(wire.meta.value(key).toInt(1), 1, 20);
    if (part == QLatin1String("label")) return 1;
    return std::clamp(wire.meta.value(QStringLiteral("annCount")).toInt(1), 1, 20);
}

std::vector<double> wireAnnotFractionsFor(const Entity& wire, const QString& part, int count) {
    count = std::max(0, count);
    std::vector<double> ts;
    const QString key = QStringLiteral("annT") + partKey(part);
    if (wire.meta.contains(key)) {
        for (const auto& v : wire.meta.value(key).toArray())
            ts.push_back(std::clamp(v.toDouble(), 0.0, 1.0));
    } else if (part == QLatin1String("label") && wire.meta.contains(QStringLiteral("labelT"))) {
        ts.push_back(wireLabelFraction(wire)); // where the label was already put by hand
    } else {
        for (const auto& v : wire.meta.value(QStringLiteral("annT")).toArray())
            ts.push_back(std::clamp(v.toDouble(), 0.0, 1.0));
    }
    // Missing entries fall back to an even spread, so raising the amount just adds instances
    // without disturbing the ones already placed by hand. Each instance is one of a GROUP of
    // markings: the group sits at the even spread, and this marking sits its own distance along
    // the wire from the middle of it, so the three never land on top of each other.
    const double len = polylineLength(wire.points);
    const double off = len > 1e-9 ? wireAnnotDefaultOffset(wire, part) / len : 0.0;
    for (int i = static_cast<int>(ts.size()); i < count; ++i) {
        const double base = (count == 1) ? 0.5 : (i + 0.5) / count;
        ts.push_back(std::clamp(base + off, 0.0, 1.0));
    }
    ts.resize(count);
    return ts;
}

void setWireAnnotFractionsFor(Entity& wire, const QString& part, const std::vector<double>& ts) {
    QJsonArray arr;
    for (double t : ts) arr.append(std::clamp(t, 0.0, 1.0));
    wire.meta.insert(QStringLiteral("annT") + partKey(part), arr);
    // The label's single position is also its own field, so a label placed with the ribbon tool
    // and one dragged here stay the same thing.
    if (part == QLatin1String("label") && !ts.empty()) setWireLabelFraction(wire, ts.front());
}

double wireAnnotDefaultGap(const QString& part) {
    Q_UNUSED(part);
    return 2.0; // all three just clear of the line; they are spread along it, not stacked
}

double wireAnnotDefaultOffset(const Entity& wire, const QString& part) {
    // Reading along the wire: the label first, then the colour, then the cross-section. Only the
    // markings actually shown take a place, so hiding one closes the space it had.
    const QStringList order = {QStringLiteral("label"), QStringLiteral("color"),
                               QStringLiteral("gauge")};
    // A rough width per marking: the app draws with a proportional font, so this is an estimate
    // from the character count - close enough to keep them apart, and it needs no font here.
    auto widthOf = [&wire](const QString& p) {
        const QString text = wireAnnotPartText(wire, p);
        if (text.isEmpty()) return 0.0;
        return 0.62 * wireAnnotSizeFor(wire, p) * static_cast<double>(text.size());
    };
    double total = 0.0;
    int shown = 0;
    for (const QString& p : order) {
        const double w = widthOf(p);
        if (w <= 0.0) continue;
        if (shown++) total += kAnnotGroupGap;
        total += w;
    }
    if (total <= 0.0) return 0.0;
    // Walk the row, centring the whole group on the place it sits at.
    double x = -total / 2.0;
    for (const QString& p : order) {
        const double w = widthOf(p);
        if (w <= 0.0) continue;
        if (p == part) return x + w / 2.0;
        x += w + kAnnotGroupGap;
    }
    return 0.0;
}

QString wireAnnotVerticalSide(const Entity& wire) {
    const QString v = wire.meta.value(QStringLiteral("annVertSide")).toString();
    return v == QLatin1String("right") ? v : QStringLiteral("left");
}

QString wireAnnotVerticalText(const Entity& wire) {
    const QString v = wire.meta.value(QStringLiteral("annVertText")).toString();
    return v == QLatin1String("down") ? v : QStringLiteral("up");
}

double wireAnnotSizeFor(const Entity& wire, const QString& part) {
    const QString key = QStringLiteral("annSize") + partKey(part);
    if (wire.meta.contains(key)) return std::max(0.5, wire.meta.value(key).toDouble());
    // The wire's own marking size if it has one, else 3 - the height a marking is drawn at unless
    // the drawing says otherwise.
    return std::max(0.5, wire.meta.value(QStringLiteral("annSize")).toDouble(3.0));
}

void setWireAnnotSizeFor(Entity& wire, const QString& part, double height) {
    wire.meta.insert(QStringLiteral("annSize") + partKey(part), std::max(0.5, height));
}

double wireAnnotGapFor(const Entity& wire, const QString& part) {
    const QString key = QStringLiteral("annGap") + partKey(part);
    if (wire.meta.contains(key)) return std::max(0.0, wire.meta.value(key).toDouble());
    // Nothing set for this marking: its default place, lifted by the wire's general text gap.
    return wireTextGap(wire) + wireAnnotDefaultGap(part);
}

void setWireAnnotGapFor(Entity& wire, const QString& part, double gap) {
    wire.meta.insert(QStringLiteral("annGap") + partKey(part), std::max(0.0, gap));
}

std::vector<WireAnnot> wireAnnotsFor(const Entity& wire, const QString& part) {
    std::vector<WireAnnot> out;
    const int n = wireAnnotCountFor(wire, part);
    if (n <= 0) return out;
    for (double t : wireAnnotFractionsFor(wire, part, n)) {
        WireAnnot a;
        a.t = t;
        if (!wirePointAtFraction(wire, t, a.pos, a.dir)) return {};
        // Keep the text the right way up: a run heading left reads better flipped. On a VERTICAL
        // run neither the reading direction nor the side follows from the geometry, so the wire
        // says: it reads up or down, and the text sits left or right of the run.
        if (std::abs(a.dir.x) <= 1e-9) {
            const bool up = wireAnnotVerticalText(wire) != QLatin1String("down");
            if ((a.dir.y < 0.0) == up) a.dir = a.dir * -1.0;
            // The lift is measured along dir.perp(); flipping it puts the text on the other side.
            if (wireAnnotVerticalSide(wire) == QLatin1String("right")) a.flip = true;
        } else if (a.dir.x < -1e-9) {
            a.dir = a.dir * -1.0;
        }
        a.normal = a.dir.perp();
        out.push_back(a);
    }
    return out;
}

std::vector<WireAnnot> wireAnnots(const Entity& wire) {
    std::vector<WireAnnot> out;
    const int n = wireAnnotCount(wire);
    if (n <= 0) return out;
    if (wireAnnotColorText(wire).isEmpty() && wireAnnotGaugeText(wire).isEmpty()) return out;
    const std::vector<double> ts = wireAnnotFractions(wire, n);
    for (double t : ts) {
        WireAnnot a;
        a.t = t;
        if (!wirePointAtFraction(wire, t, a.pos, a.dir)) return {};
        // Keep the text the right way up: a run heading left reads better flipped. On a VERTICAL
        // run neither the reading direction nor the side follows from the geometry, so the wire
        // says: it reads up or down, and the text sits left or right of the run.
        if (std::abs(a.dir.x) <= 1e-9) {
            const bool up = wireAnnotVerticalText(wire) != QLatin1String("down");
            if ((a.dir.y < 0.0) == up) a.dir = a.dir * -1.0;
            // The lift is measured along dir.perp(); flipping it puts the text on the other side.
            if (wireAnnotVerticalSide(wire) == QLatin1String("right")) a.flip = true;
        } else if (a.dir.x < -1e-9) {
            a.dir = a.dir * -1.0;
        }
        a.normal = a.dir.perp();
        out.push_back(a);
    }
    return out;
}

// ---- Connector -------------------------------------------------------------

namespace {
// 1 -> "A", 26 -> "Z", 27 -> "AA" (spreadsheet-style, matching Loopwire's letters mode).
QString ordinalToLetters(int ordinal) {
    QString out;
    for (int n = ordinal; n > 0; n = (n - 1) / 26) out.prepend(QChar('A' + (n - 1) % 26));
    return out;
}
} // namespace

QString connectorPinLabel(int index, const ConnectorSpec& spec) {
    const int ordinal =
        (spec.pinOrder == QLatin1String("bottom")) ? (spec.pinCount - index) : (index + 1);
    if (spec.annotation == QLatin1String("letters")) return ordinalToLetters(ordinal);
    // "mixed": a letter for the block of ten and a number for the position inside it -
    // A1..A10, B1..B10, ... - the way a multi-row connector is usually annotated.
    if (spec.annotation == QLatin1String("mixed"))
        return ordinalToLetters((ordinal - 1) / 10 + 1) + QString::number((ordinal - 1) % 10 + 1);
    return QString::number(ordinal);
}

void regenPinLabels(ConnectorSpec& spec) {
    spec.pins.clear();
    for (int i = 0; i < spec.pinCount; ++i) spec.pins << connectorPinLabel(i, spec);
}

ConnectorSpec defaultConnectorSpec(int pinCount) {
    ConnectorSpec s;
    s.pinCount = std::clamp(pinCount, 1, 128);
    regenPinLabels(s);
    return s;
}

ConnectorSpec connectorSpecFromMeta(const QJsonObject& meta) {
    ConnectorSpec s;
    s.designator = meta.value("designator").toString();
    s.partNumber = meta.value("partNumber").toString();
    // Whether each of those two is DRAWN. A connector written before this existed is drawn as it always
    // was - both shown - which is what the defaults here say.
    s.showDesignator = meta.value("showDesignator").toBool(true);
    s.showPartNumber = meta.value("showPartNumber").toBool(true);
    s.pinCount = std::clamp(meta.value("pinCount").toInt(4), 1, 128);
    s.pitch = meta.value("pitch").toDouble(15.0);
    s.pinLength = meta.value("pinLength").toDouble(40.0);
    s.rectWidth = meta.value("rectWidth").toDouble(40.0);
    s.rectHeight = meta.value("rectHeight").toDouble(84.0);
    s.textSize = meta.value("textSize").toDouble(10.0);
    s.mirrorX = meta.value("mirrorX").toBool(false);
    s.mirrorY = meta.value("mirrorY").toBool(false);
    s.annotation = meta.value("annotation").toString(QStringLiteral("numbers"));
    s.pinOrder = meta.value("pinOrder").toString(QStringLiteral("top"));
    s.rectMode = meta.value("rectMode").toString(QStringLiteral("dynamic"));
    s.associative = meta.value("associative").toBool(true);
    s.pinFont = meta.value("pinFont").toString(QStringLiteral("Arial"));
    s.desFont = meta.value("desFont").toString(QStringLiteral("Arial"));
    s.pnFont = meta.value("pnFont").toString(QStringLiteral("Arial"));
    s.pinSize = meta.value("pinSize").toDouble(s.textSize);
    s.desSize = meta.value("desSize").toDouble(s.textSize);
    s.pnSize = meta.value("pnSize").toDouble(s.textSize);
    s.pinTextDX = meta.value("pinTextDX").toDouble(0.0);
    s.pinTextDY = meta.value("pinTextDY").toDouble(0.0);
    s.desTextDX = meta.value("desTextDX").toDouble(0.0);
    s.desTextDY = meta.value("desTextDY").toDouble(0.0);
    s.pnTextDX = meta.value("pnTextDX").toDouble(0.0);
    s.pnTextDY = meta.value("pnTextDY").toDouble(0.0);
    const QJsonArray pins = meta.value("pins").toArray();
    for (const auto& v : pins) s.pins << v.toString();
    while (s.pins.size() < s.pinCount) s.pins << connectorPinLabel(s.pins.size(), s);
    s.pins = s.pins.mid(0, s.pinCount);
    // Per-pin wire gauge + ampere (parallel to pins; padded/trimmed to pinCount).
    for (const auto& v : meta.value("pinGauge").toArray()) s.pinGauge << v.toString();
    for (const auto& v : meta.value("pinAmp").toArray()) s.pinAmp << v.toString();
    while (s.pinGauge.size() < s.pinCount) s.pinGauge << QString();
    while (s.pinAmp.size() < s.pinCount) s.pinAmp << QString();
    s.pinGauge = s.pinGauge.mid(0, s.pinCount);
    s.pinAmp = s.pinAmp.mid(0, s.pinCount);
    return s;
}

void writeConnectorSpec(QJsonObject& meta, const ConnectorSpec& spec) {
    meta["designator"] = spec.designator;
    meta["partNumber"] = spec.partNumber;
    meta["showDesignator"] = spec.showDesignator;
    meta["showPartNumber"] = spec.showPartNumber;
    meta["pinCount"] = spec.pinCount;
    meta["pitch"] = spec.pitch;
    meta["pinLength"] = spec.pinLength;
    meta["rectWidth"] = spec.rectWidth;
    meta["rectHeight"] = spec.rectHeight;
    meta["textSize"] = spec.textSize;
    meta["mirrorX"] = spec.mirrorX;
    meta["mirrorY"] = spec.mirrorY;
    meta["annotation"] = spec.annotation;
    meta["pinOrder"] = spec.pinOrder;
    meta["rectMode"] = spec.rectMode;
    meta["associative"] = spec.associative;
    meta["pinFont"] = spec.pinFont;
    meta["desFont"] = spec.desFont;
    meta["pnFont"] = spec.pnFont;
    meta["pinSize"] = spec.pinSize;
    meta["desSize"] = spec.desSize;
    meta["pnSize"] = spec.pnSize;
    meta["pinTextDX"] = spec.pinTextDX;
    meta["pinTextDY"] = spec.pinTextDY;
    meta["desTextDX"] = spec.desTextDX;
    meta["desTextDY"] = spec.desTextDY;
    meta["pnTextDX"] = spec.pnTextDX;
    meta["pnTextDY"] = spec.pnTextDY;
    QJsonArray pins;
    for (const QString& p : spec.pins) pins.append(p);
    meta["pins"] = pins;
    QJsonArray gauge, amp;
    for (const QString& g : spec.pinGauge) gauge.append(g);
    for (const QString& a : spec.pinAmp) amp.append(a);
    meta["pinGauge"] = gauge;
    meta["pinAmp"] = amp;
}

ConnectionPinShape connectionPinShape(const Entity& e) {
    ConnectionPinShape s;
    s.circle = !connectionPinIsStub(e);
    if (s.circle) {
        s.end = s.tip = s.armA = s.armB = s.label = e.a;
        s.radius = 2.0;
        s.textSize = s.radius * 1.3;
        return s;
    }
    // A connector's pin in small: the cup about a third of the leg, the number as tall as the cup is wide.
    const double k = std::max(0.05, e.meta.value(QStringLiteral("pinScale")).toDouble(1.0));
    const double fork = 6.5 * k, forkY = 4.4 * k;
    Vec2 d = e.b - e.a;
    d = d.lengthSq() > 1e-18 ? d.normalized() : Vec2{1.0, 0.0};
    const Vec2 n{-d.y, d.x};
    s.tip = e.a;
    s.end = e.b;
    const Vec2 root = s.tip - d * fork;
    s.armA = root + n * forkY;
    s.armB = root - n * forkY;
    s.textSize = 8.5 * k;
    s.label = root - d * (s.textSize * 0.75);
    return s;
}

bool connectionPinTouches(const Entity& e, const Vec2& q, double tol) {
    const ConnectionPinShape s = connectionPinShape(e);
    if (s.circle) return std::abs(distance(q, s.end) - s.radius) <= tol;
    return distance(q, s.end) <= tol;
}

Vec2 connectionPinLanding(const Entity& e, const Vec2& q) {
    const ConnectionPinShape s = connectionPinShape(e);
    if (!s.circle) return s.end;
    const Vec2 v = q - s.end;
    const double len = std::sqrt(v.x * v.x + v.y * v.y);
    if (len < 1e-9) return {s.end.x + s.radius, s.end.y};
    return s.end + v * (s.radius / len);
}

ConnectorGeometry connectorGeometry(const Vec2& origin, const ConnectorSpec& spec) {
    ConnectorGeometry g;
    // Dynamic body: auto-fits the pins + text. To keep the primitives in proportion as
    // the pin-text size grows, the pin spacing is at least ~1.25x the pin text height
    // and the body width grows with the text; custom body uses the user's W / H exactly.
    const bool dynamic = spec.rectMode != QLatin1String("custom");
    const double pitch = dynamic ? std::max(spec.pitch, spec.pinSize) : spec.pitch;
    const double autoH = std::max(pitch * (spec.pinCount + 1),
                                  pitch * std::max(1, spec.pinCount - 1) + spec.textSize * 2.4);
    const double dynW = std::max(40.0, spec.textSize * 2.6); // wide enough for big pin text
    const double w = dynamic ? dynW : std::max(1.0, spec.rectWidth);
    const double h = dynamic ? autoH : (spec.rectHeight > 0.0 ? spec.rectHeight : autoH);
    g.effectiveHeight = h;
    g.effectiveWidth = w;

    const double left = spec.mirrorX ? origin.x - w : origin.x;
    const double right = spec.mirrorX ? origin.x : origin.x + w;
    const double top = origin.y;
    const double bottom = top - h;
    g.rectMin = {std::min(left, right), bottom};
    g.rectMax = {std::max(left, right), top};

    const double tipX = spec.mirrorX ? left : right;
    const double xDir = spec.mirrorX ? -1.0 : 1.0;
    const double pinStartY = top - (h - pitch * (spec.pinCount - 1)) / 2.0;
    const double pinTextX = spec.mirrorX ? left + w * 0.38 : left + w * 0.62;

    const Vec2 pinOff{spec.pinTextDX, spec.pinTextDY};
    for (int i = 0; i < spec.pinCount; ++i) {
        // mirrorY flips the vertical order (reflect across the X axis); text stays upright.
        const int slot = spec.mirrorY ? (spec.pinCount - 1 - i) : i;
        const double y = pinStartY - slot * pitch;
        g.pinTip.push_back({tipX, y});
        g.pinEnd.push_back({tipX + xDir * spec.pinLength, y});
        g.pinLabelPos.push_back({pinTextX + pinOff.x, y + pinOff.y}); // movable pin labels
    }
    // Designator: in front of the connector (the face opposite the pins), vertically
    // centred and clear of the body. Part number: centred just above the top edge.
    const double midY = (g.rectMin.y + g.rectMax.y) / 2.0;
    const double frontX = spec.mirrorX ? g.rectMax.x + spec.desSize * 2.0
                                       : g.rectMin.x - spec.desSize * 2.0;
    g.designatorPos = {frontX + spec.desTextDX, midY + spec.desTextDY};
    g.partPos = {(g.rectMin.x + g.rectMax.x) / 2.0 + spec.pnTextDX,
                 top + spec.pnSize * 0.9 + spec.pnTextDY};
    return g;
}

} // namespace Loopwire
