#include "harness/Harness.h"

#include <QHash>
#include <QJsonArray>
#include <algorithm>
#include <array>
#include <vector>

namespace Edraw_Desktop {

namespace {
struct WireColor { const char* code; const char* name; const char* hex; };
const std::array<WireColor, 13> kWireColors = {{
    {"WH", "White", "#e8e8e8"},   {"BK", "Black", "#888888"},
    {"BN", "Brown", "#8B4513"},   {"RD", "Red", "#CC2222"},
    {"OG", "Orange", "#FF6600"},  {"YE", "Yellow", "#DDCC00"},
    {"GN", "Green", "#007700"},   {"BU", "Blue", "#0055CC"},
    {"VT", "Violet", "#7700CC"},  {"GY", "Grey", "#888888"},
    {"PK", "Pink", "#FF69B4"},    {"TQ", "Turquoise", "#00CED1"},
    {"GNYE", "Green/Yellow", "#99CC00"},
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

QString wireColorHex(const QString& code, const QString& fallback) {
    if (code.isEmpty() || code == "BYLAYER") return fallback;
    if (code.startsWith('#')) return code;
    for (const auto& c : kWireColors)
        if (code == QLatin1String(c.code)) return QString::fromLatin1(c.hex);
    for (const auto& e : customColors())
        if (code == e.code) return e.hex;
    return fallback;
}

QString wireColorName(const QString& code) {
    for (const auto& c : kWireColors)
        if (code == QLatin1String(c.code)) return QString::fromLatin1(c.name);
    for (const auto& e : customColors())
        if (code == e.code) return e.name;
    return code;
}

QStringList wireColorCodes() {
    QStringList out;
    for (const auto& c : kWireColors) out << QString::fromLatin1(c.code);
    out << customWireColorCodes();
    return out;
}

double wireGaugeWeight(const QString& gauge) {
    static const QHash<QString, double> w = {
        {"0.05", 0.8}, {"0.08", 0.8}, {"0.14", 1}, {"0.25", 1}, {"0.34", 1.2},
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
    return wire.meta.value(QStringLiteral("colorNameMode")).toString() == QLatin1String("full")
               ? wireColorName(code)
               : code;
}

QString wireAnnotGaugeText(const Entity& wire) {
    if (!wireShowsGaugeName(wire)) return {};
    const QString g = wire.meta.value(QStringLiteral("wireGauge")).toString();
    if (g.isEmpty()) return {};
    return g + QStringLiteral(" mm²");
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
        // Keep the text the right way up: a run heading left reads better flipped, and
        // a vertical run reads bottom-to-top rather than upside down.
        if (a.dir.x < -1e-9 || (std::abs(a.dir.x) <= 1e-9 && a.dir.y < 0.0))
            a.dir = a.dir * -1.0;
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
