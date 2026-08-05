#include "harness/Harness.h"

#include "geometry/Geometry.h"

#include <QHash>
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <array>
#include <vector>

namespace Edraw_Desktop {

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
    double mid = 0.0;            // where each wire's middle lies at the ends
    double thick = 0.0;          // half a wire's own thickness
    double lead = 0.0;           // the straight run at each end
    double len = 0.0;
    double turn = 0.0;           // the length of one whole crossing, as actually used
    int turns = 0;
};

bool twistFrames(const Entity& pair, TwistFrames& f) {
    if (pair.type != EntityType::TwistedPair) return false;
    // The path it runs along: two points for a straight pair, the whole route for one drawn with knees.
    std::vector<Vec2> path =
        pair.points.size() >= 2 ? pair.points : std::vector<Vec2>{pair.a, pair.b};
    const double w =
        std::max(0.2, pair.meta.value(QStringLiteral("twistWidth")).toDouble(kTwistedPairWidth));
    // A cable does not kink at a corner, it bends - and a bend keeps the two wires from folding over
    // each other on the inside of the turn, which a sharp corner cannot.
    if (path.size() > 2) path = roundedPolyline(path, w * 1.2, 10);

    std::vector<double> at{0.0};
    for (size_t i = 1; i < path.size(); ++i)
        at.push_back(at.back() + (path[i] - path[i - 1]).length());
    f.len = at.back();
    if (f.len < 1e-9) return false;

    const double half = w / 2.0;
    const double part = w * kTwistedPairPart;      // half the parting between the two wires
    f.mid = (half + part) / 2.0;
    f.thick = (half - part) / 2.0;
    double period =
        std::max(0.05, pair.meta.value(QStringLiteral("twistPitch")).toDouble(kTwistedPairPitch));
    f.lead = std::max(0.0, pair.meta.value(QStringLiteral("twistEnds")).toDouble(kTwistedPairLead));
    // The two straight ends and at least one whole crossing have to fit in what was drawn; a pair drawn
    // shorter keeps its proportions rather than losing its weave or running past its own ends.
    if (2.0 * f.lead + period > f.len) {
        const double k = f.len / (2.0 * f.lead + period);
        f.lead *= k;
        period *= k;
    }
    const double woven = f.len - 2.0 * f.lead;
    // Whole crossings, so each wire comes back to the side it started on and runs level into the
    // straight end: half a crossing would leave the two wires swapped and the ends kinked.
    f.turns = std::max(1, static_cast<int>(std::llround(woven / period)));
    f.turn = woven / f.turns;

    // Every place the shape is measured: the ends, where the weave starts and stops, and a grid through
    // the weave fine enough that each half crossing is measured the same number of times (so a crossing
    // always lands exactly on a measured place).
    constexpr int kPerTurn = 48;
    std::vector<double> us{0.0, f.len};
    for (int i = 0; i <= f.turns * kPerTurn; ++i) us.push_back(f.lead + woven * i / (f.turns * kPerTurn));
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
    f.u = us;
    for (const double u : us) {
        f.pos.push_back(posAt(u));
        Vec2 d = posAt(u + delta) - posAt(u - delta);
        const double dl = d.length();
        f.n.push_back(dl < 1e-12 ? Vec2{0.0, 1.0} : Vec2{d.x / dl, d.y / dl}.perp());
    }
    return true;
}

// Where a wire's middle is, across the path, at distance u along it: out at the ends, and one whole
// crossing between them.
double twistOffset(const TwistFrames& f, double u) {
    if (u <= f.lead || u >= f.len - f.lead) return f.mid;
    return f.mid * std::cos(2.0 * M_PI * (u - f.lead) / f.turn);
}

// One wire's edge, over the measured places [from, to].
std::vector<Vec2> twistEdge(const TwistFrames& f, double wire, double edge, size_t from, size_t to) {
    std::vector<Vec2> line;
    line.reserve(to - from + 1);
    for (size_t i = from; i <= to && i < f.pos.size(); ++i)
        line.push_back(f.pos[i] + f.n[i] * (wire * twistOffset(f, f.u[i]) + edge * f.thick));
    return line;
}

} // namespace

std::vector<std::vector<Vec2>> twistedPairStrands(const Entity& pair) {
    TwistFrames f;
    if (!twistFrames(pair, f)) return {};
    std::vector<std::vector<Vec2>> out;
    for (const double wire : {1.0, -1.0})
        for (const double edge : {1.0, -1.0})
            out.push_back(twistEdge(f, wire, edge, 0, f.pos.size() - 1));
    return out;
}

std::vector<TwistedPairStretch> twistedPairWeave(const Entity& pair) {
    TwistFrames f;
    if (!twistFrames(pair, f)) return {};
    // Split at the places where the two wires are FARTHEST APART, so each stretch holds exactly one
    // crossing and the change of which wire is in front happens where they are not touching.
    std::vector<double> cuts{0.0, f.lead};
    for (int k = 1; k < 2 * f.turns; ++k) cuts.push_back(f.lead + f.turn * k / 2.0);
    cuts.push_back(f.len - f.lead);
    cuts.push_back(f.len);
    cuts.erase(std::unique(cuts.begin(), cuts.end(),
                           [&](double a, double b) { return std::abs(a - b) < f.len * 1e-9; }),
               cuts.end());

    std::vector<TwistedPairStretch> out;
    size_t idx = 0;
    int stretch = 0;
    for (size_t c = 0; c + 1 < cuts.size(); ++c, ++stretch) {
        while (idx + 1 < f.u.size() && f.u[idx] < cuts[c] - f.len * 1e-9) ++idx;
        size_t to = idx;
        while (to + 1 < f.u.size() && f.u[to + 1] <= cuts[c + 1] + f.len * 1e-9) ++to;
        if (to <= idx) continue;
        // The wires take it in turns to pass in front, which is what weaving is.
        const double front = (stretch % 2 == 0) ? 1.0 : -1.0;
        TwistedPairStretch s;
        s.frontEdgeA = twistEdge(f, front, 1.0, idx, to);
        s.frontEdgeB = twistEdge(f, front, -1.0, idx, to);
        s.backEdgeA = twistEdge(f, -front, 1.0, idx, to);
        s.backEdgeB = twistEdge(f, -front, -1.0, idx, to);
        s.frontOutline = s.frontEdgeA;
        for (auto it = s.frontEdgeB.rbegin(); it != s.frontEdgeB.rend(); ++it)
            s.frontOutline.push_back(*it);
        out.push_back(std::move(s));
        idx = to;
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
// 1 -> "A", 26 -> "Z", 27 -> "AA" (spreadsheet-style, matching Edraw's letters mode).
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

} // namespace Edraw_Desktop
