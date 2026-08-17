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
    // "This run is longer than it is drawn": the break mark, a pair of strokes across a run. Two
    // styles, both conventional - straight strokes or S-curves - chosen by meta["breakStyle"].
    // a = where it sits on the run, rotation = the run's direction.
    LengthBreak,
    // Two wires laid up round each other, drawn: straight ends with a run of crossings between them.
    // a / b are the ends of the run; meta twistWidth / twistPitch / twistEnds shape it.
    TwistedPair,
    // An arrow that points at something, and the words it carries: a callout. ONE object, because that
    // is what it is to anybody reading the sheet - the arrow left behind after dragging its line away is
    // exactly the sort of thing that gets released. It used to arrive from a DXF as a polyline and a
    // separate filled triangle and a separate piece of text, three things to find and move.
    //
    // points[0] is the TIP - the thing being pointed AT - and the rest are the knees it turns at, the
    // last of which is where the words sit. meta "text" / "size" carry the words, "arrowSize" the head.
    Leader,
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

// The line on a part's drawing where the harness attaches to it - the face of a connector, the barrel
// of a terminal - marked in the editor and kept with the drawing. What it is for: a part placed by the
// generator is turned and set down so that THIS line is across the wires and its centre is on the
// joint, rather than by the middle of whatever happens to have been drawn. Named here because the
// editor writes it, the renderer colours it and the harness builder reads it.
inline constexpr char kConnectionLineKey[] = "connectionLine";

// Which way a LINEAR dimension was placed: true when it measures ACROSS x, so its dimension line lies
// horizontal and it stands off in y. The object remembers it ("dimAcross"), because working it out from
// the geometry each time meant a dimension turned itself round when the geometry moved; only a drawing
// saved before that was written has to have it worked out, and reading one stamps it on.
bool dimensionAcrossX(const Entity& e);

// The two ends of a linear or aligned dimension's line: the points the arrows are at, which is also
// where its joints for moving the dimension line are. False when the object is not one of those kinds,
// so the caller can leave it alone.
bool dimensionLineEnds(const Entity& e, Vec2& d1, Vec2& d2);

} // namespace Edraw_Desktop
