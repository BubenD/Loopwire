#pragma once

#include "geometry/Geometry.h"
#include "geometry/Vec2.h"

#include <QJsonObject>
#include <QString>
#include <vector>

namespace Edraw_Desktop {

// Discriminated entity type. Mirrors the Edraw `type` string values so DWX/DXF
// round-trips stay 1:1 with the original web app.
enum class EntityType {
    Unknown,
    Line,
    DoubleLine,
    Polyline,
    Rect,
    Polygon,
    Circle,
    Arc,
    Ellipse,
    Spline,
    Text,
    Dimension,
    Hatch,
    Wipeout,
    Image,   // a placed picture: points = its four corners, meta carries the bytes
    Wire,
    Splice,
    Connector,
    Breakout,
    Bundle,
    NetLabel,
    NotConnected,
    Twist,   // twisted-pair/bundle annotation over a set of wires (points = crossing pts)
    Screen,  // shield/screen annotation enclosing a set of wires (points = bbox corners)
};

EntityType entityTypeFromString(const QString& s);
QString entityTypeToString(EntityType t);

// A single drawing entity. The model deliberately follows Edraw's dynamic
// object: a typed common core plus shared geometry holders, with `meta` carrying
// type-specific and unknown fields so nothing is lost across a load/save cycle.
struct Entity {
    QString id;
    EntityType type = EntityType::Unknown;

    // Common properties.
    QString layer = "0";
    QString color;      // explicit hex; empty => ByLayer
    double weight = 0.0; // 0 => ByLayer

    // Shared geometry. Which fields are meaningful depends on `type`:
    //   Line/DoubleLine      -> a, b
    //   Polyline/Spline/Wire -> points
    //   Rect/Polygon/Hatch   -> points (corners)
    //   Image                -> points (corners), meta["image"] (base64 bytes) + ["imageFormat"]
    //   Circle/Arc           -> center, radius (+ start/end angle for Arc)
    //   Ellipse              -> center, radius (rx), radiusY (ry), rotation
    //   Text                 -> a (anchor), meta["text"], meta["height"]
    Vec2 a;
    Vec2 b;
    Vec2 center;
    double radius = 0.0;
    double radiusY = 0.0;
    double startAngle = 0.0; // radians
    double endAngle = 0.0;   // radians
    double rotation = 0.0;   // radians
    std::vector<Vec2> points;

    // Type-specific / harness / unknown fields, preserved verbatim.
    QJsonObject meta;

    bool hasExplicitColor() const { return !color.isEmpty(); }

    Bounds bounds() const;

    QJsonObject toJson() const;
    static Entity fromJson(const QJsonObject& obj);
};

} // namespace Edraw_Desktop
