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
const std::array<TypeName, 25> kTypeNames = {{
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
    {EntityType::Image, "image"},
    {EntityType::Wire, "wire"},
    {EntityType::Splice, "splice"},
    {EntityType::LengthBreak, "lengthbreak"},
    {EntityType::TwistedPair, "twistedpair"},
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
    case EntityType::TwistedPair: {
        // The path it runs along, out by half the cable's width - worked out from the path rather than
        // by measuring the weave. How far an object reaches is asked constantly: once per object per
        // repaint, per hit test, per selection zone. Answered by building a woven pair each time, a
        // sheet with a few pairs on it made the cursor lag behind the mouse. Rounding the knees only
        // ever cuts a corner inwards, so this still contains the whole of it.
        const double reach =
            std::max(0.2, meta.value("twistWidth").toDouble(kTwistedPairWidth)) / 2.0;
        const std::vector<Vec2>& along = points.size() >= 2 ? points : std::vector<Vec2>{a, b};
        for (const auto& p : along) {
            bnds.expand({p.x - reach, p.y - reach});
            bnds.expand({p.x + reach, p.y + reach});
        }
        break;
    }
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
    case EntityType::Image:
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
        // With no box the anchor is the point the text sits ABOUT, sideways as well as vertically:
        // centre shifts it left by half its width, right by all of it. This is what the renderer does,
        // and the two have to agree or the box that selects a label is not the label.
        // See the renderer: text read from a DXF is justified ABOUT its anchor, not inside its box.
        const bool anchorJust = meta.value(QStringLiteral("anchorJustify")).toBool();
        double hOff = 0.0, vOff = 0.0;
        {
            const double avail = (bw > 0.0 && !anchorJust) ? bw : 0.0;
            if (just[1] == QLatin1Char('C')) hOff = (avail - textW) / 2.0;
            else if (just[1] == QLatin1Char('R')) hOff = avail - textW;
        }
        // No box height: the anchor is the point the block sits ABOUT, so a middle attachment lifts
        // it by half and a bottom one by all of it. (See the renderer.)
        {
            const double avail = (bh > 0.0 && !anchorJust) ? bh : 0.0;
            if (just[0] == QLatin1Char('M')) vOff = (avail - textH) / 2.0;
            else if (just[0] == QLatin1Char('B')) vOff = avail - textH;
        }
        bnds.expand({a.x + hOff, a.y - vOff});
        bnds.expand({a.x + hOff + textW, a.y - vOff - textH});
        break;
    }
    case EntityType::Splice:
        bnds.expand(a);
        // A two-conductor splice reaches down to its second joint.
        if (meta.value("pair").toBool(false)) bnds.expand(b);
        break;
    case EntityType::LengthBreak: {
        // The pair of strokes, whichever way the run points: a square box of the mark's own reach is
        // right for either style and for any rotation.
        const double reach = meta.value("size").toDouble(6.0) +
                             meta.value("gap").toDouble(meta.value("size").toDouble(6.0) * 0.9);
        bnds.expand({a.x - reach, a.y - reach});
        bnds.expand({a.x + reach, a.y + reach});
        break;
    }
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
    case EntityType::Twist: {
        // The rendered braid/motif spans a wider band than the raw crossing points
        // (matches EntityRenderer's twist band formula) — use that for bounds/selection
        // so a region that only touches the drawn glyph (not the narrow crossing points)
        // still counts.
        if (points.empty()) break;
        double cx = 0.0, yMin = 1e18, yMax = -1e18;
        for (const auto& p : points) {
            cx += p.x;
            yMin = std::min(yMin, p.y);
            yMax = std::max(yMax, p.y);
        }
        cx /= static_cast<double>(points.size());
        const double band = std::max(1.0, yMax - yMin);
        const double w = std::max(28.0, band * 1.4);
        bnds.expand({cx - w / 2.0, yMin});
        bnds.expand({cx + w / 2.0, yMax});
        break;
    }
    case EntityType::Screen: {
        // The ellipse body + the drain leg's full extent (not just the stored joints).
        const int k = std::min(meta.value("catch").toInt(static_cast<int>(points.size())),
                               static_cast<int>(points.size()));
        if (k < 1) break;
        double cx = 0.0, yMin = 1e18, yMax = -1e18;
        for (int i = 0; i < k; ++i) {
            cx += points[i].x;
            yMin = std::min(yMin, points[i].y);
            yMax = std::max(yMax, points[i].y);
        }
        cx /= k;
        const double cyMid = (yMin + yMax) / 2.0;
        const double rx = meta.value("rx").toDouble(12.0);
        const double ry = meta.contains("ry") ? meta.value("ry").toDouble()
                                              : (yMax - yMin) / 2.0 + 6.0;
        bnds.expand({cx - rx, cyMid - ry});
        bnds.expand({cx + rx, cyMid + ry});
        if (static_cast<int>(points.size()) > k) bnds.expand(points.back()); // drain point
        break;
    }
    case EntityType::Dimension: {
        // Which stored points a dimension is made of depends on the kind of one it is. A radius,
        // diameter or jogged one is a leader from the RING'S CENTRE out, and `points` holds only the far
        // end of it; an ordinate one runs from a to b and has no points at all; an angular one is its
        // three points, and never sets a or b - so reading a and b on one of those reaches back to the
        // origin of the drawing. Answering with less than the object left dimensions out of Zoom All and
        // let the canvas cull one whose leader was on screen.
        const QString dty = meta.value("dimType").toString(QStringLiteral("linear"));
        for (const auto& p : points) bnds.expand(p);
        if (dty == QLatin1String("angular")) break;
        bnds.expand(b);
        if (dty == QLatin1String("radius") || dty == QLatin1String("jogged") ||
            dty == QLatin1String("diameter")) {
            bnds.expand(center);
            // A diameter one is drawn right across the ring, so it reaches as far the other side.
            if (dty == QLatin1String("diameter") && radius > 0.0) {
                bnds.expand({center.x - radius, center.y - radius});
                bnds.expand({center.x + radius, center.y + radius});
            }
            break;
        }
        bnds.expand(a);
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
           t == EntityType::Connector || t == EntityType::LengthBreak;
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
        obj["a"] = pointToJson(a);
        obj["b"] = pointToJson(b);
        break;
    case EntityType::Dimension:
        // A dimension is more than the two points it measures between. Where its line stands off the
        // geometry is a third point; an angular one is three points; a radius, diameter or jogged one is
        // a centre and a radius. Writing only a and b saved none of that, so a dimension placed and
        // saved came back flattened onto what it measured - the stand-off lost, a radius dimension drawn
        // at the origin. It only ever looked right on a file it had been RE-saved from, because then the
        // points came back through the preserved extras.
        obj["a"] = pointToJson(a);
        obj["b"] = pointToJson(b);
        if (!points.empty()) {
            QJsonArray arr;
            for (const auto& p : points) arr.append(pointToJson(p));
            obj["points"] = arr;
        } else {
            obj.remove("points");
        }
        if (center.x != 0.0 || center.y != 0.0) obj["center"] = pointToJson(center);
        else obj.remove("center");
        if (radius != 0.0) obj["radius"] = radius;
        else obj.remove("radius");
        if (startAngle != 0.0 || endAngle != 0.0) {
            obj["start"] = startAngle;
            obj["end"] = endAngle;
        }
        if (rotation != 0.0) obj["rotation"] = rotation;
        break;
    case EntityType::TwistedPair: {
        obj["a"] = pointToJson(a);
        obj["b"] = pointToJson(b);
        // A pair routed through knees is those knees; a straight one has none to write.
        if (points.size() >= 2) {
            QJsonArray arr;
            for (const auto& p : points) arr.append(pointToJson(p));
            obj["points"] = arr;
        }
        break;
    }
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
    case EntityType::LengthBreak:
        // Which way it lies is the run's direction, and a mark that forgets it comes back lying
        // across the horizontal whatever it was placed on.
        obj["rotation"] = rotation;
        obj["position"] = pointToJson(a);
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
