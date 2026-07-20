#pragma once

#include "geometry/Vec2.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <vector>

namespace Edraw_Desktop {

// ---- Wire color codes & gauges (mirror Edraw's WIRE_COLORS / gauge table) ----

// Resolve a wire color code (e.g. "WH", "BK", "GNYE", or "#rrggbb") to hex.
QString wireColorHex(const QString& code, const QString& fallback = "#e8e8e8");
QString wireColorName(const QString& code);
QStringList wireColorCodes();

// Stroke weight (px) for a wire cross-section gauge (mm^2), e.g. "0.5", "2.5".
double wireGaugeWeight(const QString& gauge);

// ---- Connector ------------------------------------------------------------

// A connector's editable specification, stored in the entity's meta.
struct ConnectorSpec {
    QString designator;   // e.g. "X1"
    QString partNumber;   // e.g. "MOLEX-12345"
    int pinCount = 4;
    double pitch = 15.0;      // spacing between pins (world units)
    double pinLength = 40.0;  // pin stub length
    double rectWidth = 40.0;
    double rectHeight = 84.0; // custom body height (dynamic mode auto-computes)
    double textSize = 10.0;   // general size (== max of the per-text sizes)
    bool mirrorX = false;     // false: pins exit right; true: exit left (mirror across Y)
    bool mirrorY = false;     // true: pin order flipped top<->bottom (mirror across X)
    QStringList pins;         // per-pin labels (size == pinCount)
    QStringList pinGauge;     // per-pin wire cross-section (AWG or mm² string), size==pinCount
    QStringList pinAmp;       // per-pin ampere rating (string), size==pinCount
    // Web-parity build options (Schematic build "Connector build" ribbon):
    QString annotation = QStringLiteral("numbers"); // "numbers" | "letters"
    QString pinOrder = QStringLiteral("top");       // "top" (1 at top) | "bottom"
    QString rectMode = QStringLiteral("dynamic");   // "dynamic" (auto H, W=40) | "custom"
    bool associative = true;                        // re-editable as a group
    QString pinFont = QStringLiteral("Arial");
    QString desFont = QStringLiteral("Arial");
    QString pnFont = QStringLiteral("Arial");
    double pinSize = 15.0;
    double desSize = 10.0;
    double pnSize = 10.0;
    // Movable-text offsets (world units, relative to the connector). Dragged via grips;
    // pin offset applies to every pin label, the others to their single text.
    double pinTextDX = 0.0, pinTextDY = 0.0;
    double desTextDX = 0.0, desTextDY = 0.0;
    double pnTextDX = 0.0, pnTextDY = 0.0;
};

ConnectorSpec defaultConnectorSpec(int pinCount = 4);
ConnectorSpec connectorSpecFromMeta(const QJsonObject& meta);
void writeConnectorSpec(QJsonObject& meta, const ConnectorSpec& spec);

// Auto pin label for pin `index` from the annotation ("numbers"/"letters") and order
// ("top"/"bottom"): e.g. numbers top -> "1","2",…; letters -> "A","B",…
QString connectorPinLabel(int index, const ConnectorSpec& spec);
// Refill spec.pins with auto labels for the current annotation/order/pinCount.
void regenPinLabels(ConnectorSpec& spec);

// Computed geometry for rendering / hit-testing / bounds. `origin` is the body
// top corner on the non-exit side (matches Edraw's connectorEntitiesAt origin).
struct ConnectorGeometry {
    Vec2 rectMin;             // bottom-left
    Vec2 rectMax;             // top-right
    std::vector<Vec2> pinTip; // pin exit point on the body edge
    std::vector<Vec2> pinEnd; // far end of pin stub
    std::vector<Vec2> pinLabelPos;
    Vec2 designatorPos;
    Vec2 partPos;
    double effectiveHeight = 0.0;
    double effectiveWidth = 0.0;
};

ConnectorGeometry connectorGeometry(const Vec2& origin, const ConnectorSpec& spec);

} // namespace Edraw_Desktop
