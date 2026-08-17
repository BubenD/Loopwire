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

// ---- Merged table cells --------------------------------------------------------------------------
// A block of a table's cells joined into one: where it starts, and how many rows and columns it covers.
// The cell it starts at is the one that holds the words and is the one picked; the rest are covered, and
// the grid lines between them are not drawn.
//
// Kept on the table as meta "merges" - an array of [row, col, rowSpan, colSpan] - rather than as a span
// on each cell, because a merge is one fact about a block and storing it per cell means four ways to say
// the same thing and four chances to disagree.
struct TableMerge {
    int row = 0, col = 0, rowSpan = 1, colSpan = 1;
    bool covers(int r, int c) const {
        return r >= row && r < row + rowSpan && c >= col && c < col + colSpan;
    }
    bool isAnchor(int r, int c) const { return r == row && c == col; }
};
// How wide a piece of a table's text is, and how it breaks when the column is narrower than that.
//
// Estimated from the character count and the text height rather than measured with a font, deliberately:
// the same estimate decides where a drag STOPS and how tall a row has to be, and a limit worked out by
// measuring glyphs would answer differently at different zooms, so the same drag would stop in a different
// place depending on how far in somebody was looking. One estimate, used everywhere, agrees with itself.
double tableTextWidth(const QString& text, double textHeight);
// The lines `text` breaks into inside a column `avail` wide. Never fewer than one line, and never breaks
// a word: a column narrower than its longest word simply overflows, which is visible and honest.
QStringList tableWrapText(const QString& text, double avail, double textHeight);
// The width below which a column starts to break its words - the longest single word in it. A column may
// be narrowed past its whole STRING (the words wrap and the row grows) but not past a word.
double tableLongestWord(const QString& text, double textHeight);

std::vector<TableMerge> tableMerges(const Entity& e);
void setTableMerges(Entity& e, const std::vector<TableMerge>& merges);
// The merge covering this cell, if any. True when one does - `out` is then the whole block, whose
// row/col is the cell that holds the words.
bool tableMergeAt(const Entity& e, int row, int col, TableMerge& out);

// The two ends of a linear or aligned dimension's line: the points the arrows are at, which is also
// where its joints for moving the dimension line are. False when the object is not one of those kinds,
// so the caller can leave it alone.
bool dimensionLineEnds(const Entity& e, Vec2& d1, Vec2& d2);

} // namespace Edraw_Desktop
