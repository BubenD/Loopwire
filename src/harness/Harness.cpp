#include "harness/Harness.h"

#include <QHash>
#include <QJsonArray>
#include <algorithm>
#include <array>

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
} // namespace

QString wireColorHex(const QString& code, const QString& fallback) {
    if (code.isEmpty() || code == "BYLAYER") return fallback;
    if (code.startsWith('#')) return code;
    for (const auto& c : kWireColors)
        if (code == QLatin1String(c.code)) return QString::fromLatin1(c.hex);
    return fallback;
}

QString wireColorName(const QString& code) {
    for (const auto& c : kWireColors)
        if (code == QLatin1String(c.code)) return QString::fromLatin1(c.name);
    return code;
}

QStringList wireColorCodes() {
    QStringList out;
    for (const auto& c : kWireColors) out << QString::fromLatin1(c.code);
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
    return spec.annotation == QLatin1String("letters") ? ordinalToLetters(ordinal)
                                                        : QString::number(ordinal);
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
    s.annotation = meta.value("annotation").toString(QStringLiteral("numbers"));
    s.pinOrder = meta.value("pinOrder").toString(QStringLiteral("top"));
    s.rectMode = meta.value("rectMode").toString(QStringLiteral("dynamic"));
    s.associative = meta.value("associative").toBool(true);
    s.pinFont = meta.value("pinFont").toString(QStringLiteral("Segoe UI"));
    s.desFont = meta.value("desFont").toString(QStringLiteral("Segoe UI"));
    s.pnFont = meta.value("pnFont").toString(QStringLiteral("Segoe UI"));
    s.pinSize = meta.value("pinSize").toDouble(s.textSize);
    s.desSize = meta.value("desSize").toDouble(s.textSize);
    s.pnSize = meta.value("pnSize").toDouble(s.textSize);
    const QJsonArray pins = meta.value("pins").toArray();
    for (const auto& v : pins) s.pins << v.toString();
    while (s.pins.size() < s.pinCount) s.pins << connectorPinLabel(s.pins.size(), s);
    s.pins = s.pins.mid(0, s.pinCount);
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
    QJsonArray pins;
    for (const QString& p : spec.pins) pins.append(p);
    meta["pins"] = pins;
}

ConnectorGeometry connectorGeometry(const Vec2& origin, const ConnectorSpec& spec) {
    ConnectorGeometry g;
    // Dynamic body: fixed 40-wide, height auto-fits the pins + text clearance.
    // Custom body: the user's W / H (falling back to the auto height if unset).
    const bool dynamic = spec.rectMode != QLatin1String("custom");
    const double autoH = std::max(spec.pitch * (spec.pinCount + 1),
                                  spec.pitch * std::max(1, spec.pinCount - 1) + spec.textSize * 2.4);
    const double w = dynamic ? 40.0 : std::max(1.0, spec.rectWidth);
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
    const double pinStartY = top - (h - spec.pitch * (spec.pinCount - 1)) / 2.0;
    const double pinTextX = spec.mirrorX ? left + w * 0.38 : left + w * 0.62;

    for (int i = 0; i < spec.pinCount; ++i) {
        const double y = pinStartY - i * spec.pitch;
        g.pinTip.push_back({tipX, y});
        g.pinEnd.push_back({tipX + xDir * spec.pinLength, y});
        g.pinLabelPos.push_back({pinTextX, y});
    }
    g.designatorPos = {left + w / 2.0, bottom - spec.textSize * 1.8};
    g.partPos = {left, top + spec.textSize * 2.3};
    return g;
}

} // namespace Edraw_Desktop
