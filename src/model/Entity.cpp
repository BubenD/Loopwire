#include "model/Entity.h"

#include "harness/Harness.h"

#include <QJsonArray>
#include <QStringList>
#include <algorithm>
#include <array>
#include <cmath>

namespace Edraw_Desktop {

namespace {
struct TypeName { EntityType type; const char* name; };
const std::array<TypeName, 22> kTypeNames = {{
    {EntityType::Line, "line"},
    {EntityType::DoubleLine, "doubleline"},
    {EntityType::Polyline, "polyline"},
    {EntityType::Rect, "rect"},
    {EntityType::Polygon, "polygon"},
    {EntityType::Circle, "circle"},
    {EntityType::Arc, "arc"},
    {EntityType::Ellipse, "ellipse"},
    {EntityType::Spline, "spline"},
    {EntityType::Text, "text"},
    {EntityType::Dimension, "dimension"},
    {EntityType::Hatch, "hatch"},
    {EntityType::Wipeout, "wipeout"},
    {EntityType::Wire, "wire"},
    {EntityType::Splice, "splice"},
    {EntityType::Connector, "connector"},
    {EntityType::Breakout, "breakout"},
    {EntityType::Bundle, "bundle"},
    {EntityType::NetLabel, "netlabel"},
    {EntityType::NotConnected, "notconnected"},
    {EntityType::Twist, "twist"},
    {EntityType::Screen, "screen"},
}};
} // namespace

EntityType entityTypeFromString(const QString& s) {
    const QString key = s.toLower();
    for (const auto& tn : kTypeNames)
        if (key == QLatin1String(tn.name)) return tn.type;
    return EntityType::Unknown;
}

QString entityTypeToString(EntityType t) {
    for (const auto& tn : kTypeNames)
        if (t == tn.type) return QString::fromLatin1(tn.name);
    return QStringLiteral("unknown");
}

Bounds Entity::bounds() const {
    Bounds bnds;
    switch (type) {
    case EntityType::Line:
    case EntityType::DoubleLine:
        bnds.expand(a);
        bnds.expand(b);
        break;
    case EntityType::Hatch:
        // A circle-boundary hatch carries center/radius rather than points.
        if (meta.value("shape").toString() == QLatin1String("circle")) {
            bnds.expand({center.x - radius, center.y - radius});
            bnds.expand({center.x + radius, center.y + radius});
            break;
        }
        for (const auto& p : points) bnds.expand(p);
        break;
    case EntityType::Polyline:
    case EntityType::Spline:
    case EntityType::Wire:
    case EntityType::Rect:
    case EntityType::Polygon:
    case EntityType::Wipeout:
        for (const auto& p : points) bnds.expand(p);
        break;
    case EntityType::Circle:
    case EntityType::Arc:
        bnds.expand({center.x - radius, center.y - radius});
        bnds.expand({center.x + radius, center.y + radius});
        break;
    case EntityType::Ellipse: {
        const double rx = radius, ry = radiusY;
        bnds.expand({center.x - rx, center.y - ry});
        bnds.expand({center.x + rx, center.y + ry});
        break;
    }
    case EntityType::Text: {
        // The actual TEXT extent (estimated glyph rectangle), NOT the container —
        // so selection/hit-testing target the text itself. Lines wrap to boxWidth;
        // the block is offset within the box by the justification.
        const QString txt = meta.value("text").toString();
        const double size = meta.value("size").toDouble(meta.value("height").toDouble(2.5));
        const double spacing = std::max(1.0, meta.value("lineSpacing").toDouble(1.0));
        const double bw = meta.value("boxWidth").toDouble(0.0);
        const double bh = meta.value("boxHeight").toDouble(0.0);
        const double charW = size * 0.6, lineH = size * spacing * 1.3;
        const QStringList paras = txt.split(QLatin1Char('\n'));
        int wrapped = 0;
        double textW = 0.0;
        for (const QString& para : paras) {
            const double pw = para.size() * charW;
            if (bw > 0.0 && pw > bw) {
                wrapped += std::max<int>(1, static_cast<int>(std::ceil(pw / bw)));
                textW = std::max(textW, bw);
            } else {
                wrapped += 1;
                textW = std::max(textW, pw);
            }
        }
        if (wrapped == 0) { wrapped = 1; }
        if (textW < 1e-6) textW = size; // empty text: a small caret-ish box
        const double textH = wrapped * lineH;

        // Justification offset of the block within the container box.
        QString just = meta.value("justify").toString();
        if (just.size() < 2) {
            const QString al = meta.value("align").toString();
            just = QStringLiteral("T") + (al == QLatin1String("center") ? QStringLiteral("C")
                                          : al == QLatin1String("right") ? QStringLiteral("R")
                                                                         : QStringLiteral("L"));
        }
        double hOff = 0.0, vOff = 0.0;
        if (bw > 0.0) {
            if (just[1] == QLatin1Char('C')) hOff = (bw - textW) / 2.0;
            else if (just[1] == QLatin1Char('R')) hOff = bw - textW;
        }
        if (bh > 0.0) {
            if (just[0] == QLatin1Char('M')) vOff = (bh - textH) / 2.0;
            else if (just[0] == QLatin1Char('B')) vOff = bh - textH;
        }
        bnds.expand({a.x + hOff, a.y - vOff});
        bnds.expand({a.x + hOff + textW, a.y - vOff - textH});
        break;
    }
    case EntityType::Splice:
    case EntityType::NetLabel:
    case EntityType::NotConnected:
        bnds.expand(a);
        break;
    case EntityType::Connector: {
        const ConnectorSpec spec = connectorSpecFromMeta(meta);
        const ConnectorGeometry g = connectorGeometry(a, spec);
        bnds.expand(g.rectMin);
        bnds.expand(g.rectMax);
        for (const auto& pe : g.pinEnd) bnds.expand(pe);
        break;
    }
    default:
        for (const auto& p : points) bnds.expand(p);
        if (!points.empty()) break;
        bnds.expand(a);
        break;
    }
    return bnds;
}

namespace {
QJsonObject pointToJson(const Vec2& p) {
    return QJsonObject{{"x", p.x}, {"y", p.y}};
}
Vec2 pointFromJson(const QJsonValue& v) {
    const QJsonObject o = v.toObject();
    return {o.value("x").toDouble(), o.value("y").toDouble()};
}
} // namespace

namespace {
bool usesPosition(EntityType t) {
    return t == EntityType::Text || t == EntityType::Splice ||
           t == EntityType::NotConnected || t == EntityType::NetLabel ||
           t == EntityType::Connector;
}
} // namespace

QJsonObject Entity::toJson() const {
    QJsonObject obj = meta; // start from preserved extras, then write known fields
    obj["type"] = entityTypeToString(type);
    if (!id.isEmpty()) obj["id"] = id;
    obj["layer"] = layer;
    if (hasExplicitColor()) obj["color"] = color;
    if (weight != 0.0) obj["weight"] = weight;

    // Field names mirror Edraw's DWX schema exactly so files round-trip.
    switch (type) {
    case EntityType::Line:
    case EntityType::DoubleLine:
    case EntityType::Dimension:
        obj["a"] = pointToJson(a);
        obj["b"] = pointToJson(b);
        break;
    case EntityType::Circle:
        obj["center"] = pointToJson(center);
        obj["radius"] = radius;
        break;
    case EntityType::Arc:
        obj["center"] = pointToJson(center);
        obj["radius"] = radius;
        obj["start"] = startAngle;
        obj["end"] = endAngle;
        break;
    case EntityType::Ellipse:
        obj["center"] = pointToJson(center);
        obj["rx"] = radius;
        obj["ry"] = radiusY;
        obj["rotation"] = rotation;
        if (startAngle != 0.0 || endAngle != 0.0) {
            obj["start"] = startAngle;
            obj["end"] = endAngle;
        }
        break;
    case EntityType::Text:
    case EntityType::Splice:
    case EntityType::NotConnected:
    case EntityType::NetLabel:
    case EntityType::Connector:
        obj["position"] = pointToJson(a);
        break;
    default: { // polyline / spline / wire / rect / polygon / hatch / wipeout
        // A circle-boundary hatch stores its geometry as center/radius (fromJson
        // already reads these back).
        if (type == EntityType::Hatch && meta.value("shape").toString() == QLatin1String("circle")) {
            obj["center"] = pointToJson(center);
            obj["radius"] = radius;
            break;
        }
        QJsonArray arr;
        for (const auto& p : points) arr.append(pointToJson(p));
        obj["points"] = arr;
        break;
    }
    }
    return obj;
}

Entity Entity::fromJson(const QJsonObject& obj) {
    Entity e;
    e.type = entityTypeFromString(obj.value("type").toString());
    e.id = obj.value("id").toString();
    e.layer = obj.value("layer").toString("0");
    e.color = obj.value("color").toString();
    e.weight = obj.value("weight").toDouble(0.0);

    if (usesPosition(e.type) && obj.contains("position"))
        e.a = pointFromJson(obj.value("position"));
    if (obj.contains("a")) e.a = pointFromJson(obj.value("a"));
    if (obj.contains("b")) e.b = pointFromJson(obj.value("b"));
    if (obj.contains("center")) e.center = pointFromJson(obj.value("center"));

    e.radius = obj.value("radius").toDouble(0.0);
    // Ellipse uses rx/ry; fall back to radius/radiusY for our own files.
    if (obj.contains("rx")) e.radius = obj.value("rx").toDouble(0.0);
    e.radiusY = obj.value("ry").toDouble(obj.value("radiusY").toDouble(0.0));
    // Arc/ellipse sweep uses start/end; tolerate startAngle/endAngle too.
    e.startAngle = obj.value("start").toDouble(obj.value("startAngle").toDouble(0.0));
    e.endAngle = obj.value("end").toDouble(obj.value("endAngle").toDouble(0.0));
    e.rotation = obj.value("rotation").toDouble(0.0);

    if (obj.contains("points")) {
        for (const auto& v : obj.value("points").toArray())
            e.points.push_back(pointFromJson(v));
    }

    // Keep everything else (text content, wire/connector metadata, etc.).
    e.meta = obj;
    return e;
}

} // namespace Edraw_Desktop
