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
    double rectHeight = 0.0;  // 0 => auto from pinCount * pitch
    double textSize = 10.0;
    bool mirrorX = false;     // false: pins exit right; true: exit left
    QStringList pins;         // per-pin labels (size == pinCount)
};

ConnectorSpec defaultConnectorSpec(int pinCount = 4);
ConnectorSpec connectorSpecFromMeta(const QJsonObject& meta);
void writeConnectorSpec(QJsonObject& meta, const ConnectorSpec& spec);

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
};

ConnectorGeometry connectorGeometry(const Vec2& origin, const ConnectorSpec& spec);

} // namespace Edraw_Desktop
