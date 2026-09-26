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
    // A DETAIL VIEW: the small thing on the drawing, drawn again bigger somewhere there is room for it.
    //
    // Two circles and a magnification. `center` / `radius` is the circle drawn round the thing itself,
    // where it actually is; `b` is where the enlarged circle goes, and its radius is `radius * scale`. What
    // is inside the source circle is drawn inside the target one, scaled about the source centre - so the
    // detail is not a copy of the geometry but a VIEW of it, and the drawing it is a view of can be changed
    // afterwards without the detail going stale.
    //
    // meta "detailTag" is the letter ("A", "B", ...) that ties the two circles together, and "detailScale"
    // the magnification. A drawing office numbers its details in the order they were made and never
    // renumbers them, so the letter is fixed when the view is placed and kept.
    DetailView,
    // A CONNECTION PIN: where a schematic symbol meets the world, marked with the number it is placed
    // by. `a` is its position; meta "pinNumber" is the number, handed out lowest-free-first so
    // deleting one and adding another reuses it rather than counting up forever.
    ConnectionPin,
    // A BLOCK REFERENCE: one object that draws a whole block, read from the block's file. What a big
    // import goes down as - a PDF is tens of thousands of lines, and as that many objects on the sheet
    // every pick, hover and panel refresh walks all of them. As one reference it is one object.
    // points = the four corners of the block's box on the sheet (local min, +x, max, +y), so moving,
    // turning, scaling and mirroring it are the ordinary point maps. meta "blockRef" is the block's
    // name and "bx0"/"by0"/"bx1"/"by1" its box in its own coordinates.
    BlockRef,
};

EntityType entityTypeFromString(const QString& s);
QString entityTypeToString(EntityType t);
// WHAT AN OBJECT IS CALLED WHERE A PERSON READS IT, which is not always what it is called in the file.
//
// The stored name is part of the format and cannot move: a drawing written today has to open in a
// version written before it. The name on the properties panel is a different thing, answerable to
// whoever is looking at the sheet - and "twistedpair" is what this object was when it could only be a
// pair. It is a harness now: two wires laid up, twelve running straight, a bundle fanning out at both
// ends. Nobody drawing one of those is drawing a twisted pair.
QString entityTypeDisplayName(EntityType t);

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

// THE SHAPE AN OBJECT ACTUALLY HAS, for the objects that work theirs out rather than storing it.
//
// A volume arrow keeps its two ends and nothing else: the outline is laid out from them when it is
// drawn, so that pulling an end re-lays the arrow instead of dragging one corner of a shape that has
// forgotten what it is. The cost was that everything ELSE which asks where an object is - the box it
// occupies, whether a click is near it, where its handles go, what a cursor can snap to - read the
// stored point list, found it empty, and concluded there was nothing there. An arrow could be drawn
// and then never picked again.
//
// So there is one place that answers the question. For an ordinary object it is the stored points; for
// a derived one it is the outline that will be drawn. Anything that needs the shape asks here.
// The outline a heatshrink sleeve has, worked out from the route it stores - empty for anything that
// is not one. See the definition: the shape on the screen is nowhere in the stored points, so a click,
// a green zone and a snap all have to ask for it rather than reading the centreline.
std::vector<Vec2> shrunkSleeveOutlineOf(const Entity& e);
// Every piece of line a sleeve draws, each run closed by repeating its first point when it is a
// polygon. An ordinary sleeve is one outline; a cable with heatshrink only at its ends is the two
// cable rails plus the outline of each sleeve section. Selection, snapping and DXF all read this so
// they cannot disagree with the renderer.
std::vector<std::vector<Vec2>> shrunkSleeveRunsOf(const Entity& e);
// The outline of one sleeve end. For a cable-with-end-sleeves object this is the section at that
// end, not the whole run; for the other two sorts it is the whole sleeve as before.
std::vector<Vec2> shrunkSleeveEndOutlineOf(const Entity& e, int end);
// How far heatshrink covers the route from each end, in drawing units. False unless the entity is
// the cable-with-end-sleeves sort; the values are clamped so the two ends cannot overlap.
bool sleeveEndCoverageOf(const Entity& e, double& start, double& end);
std::vector<Vec2> entityOutline(const Entity& e);

// WHERE AN AXIS ANGLE STARTS ITS SWEEP, and HOW LONG ITS CENTRE LINES ARE.
//
// The zero comes from the second click, which is what makes the object quick to draw, and is then a
// turn that can be typed on top of it - a mechanism whose datum is not where the cursor happened to
// land is the normal case. Held as an addition, so sliding the arc in or out never moves the zero and
// typing the zero never changes the radius.
//
// The arms are a length of their own. They used to be worked out from the radius, which tied the two
// together: a long axis with a tight arc could not be drawn at all. An object from before that carries
// no size of its own still gets the old proportional arms, so old drawings keep the look they had.
double axisAngleStartAngle(const Entity& e);
// WHICH WAY THE CENTRE LINES RUN, in radians.
//
// Their own number, so the cross can stand still while the sweep's start is dragged round it. They used
// to be laid along the start, which meant the start could never leave them - drag it sideways and the
// whole cross came too, so there was no such thing as a sweep beginning off the datum.
//
// Absent on everything drawn before that, and then it IS the start, which is exactly how those were
// drawn: no old object moves.
double axisAngleCrossAngle(const Entity& e);
// Whether a limit at `angleRad` needs a radial line drawn to stop the arc against.
//
// It does unless it lands on one of the four centre-line arms. A quarter or a half turn off the datum
// finishes on one and is stopped by it already; 45 degrees stops in mid-air, and an arc that stops in
// mid-air does not say where the travel ends.
bool axisAngleNeedsStop(double angleRad, double crossRad);
double axisAngleArm(const Entity& e);

// The line on a part's drawing where the harness attaches to it - the face of a connector, the barrel
// of a terminal - marked in the editor and kept with the drawing. What it is for: a part placed by the
// generator is turned and set down so that THIS line is across the wires and its centre is on the
// joint, rather than by the middle of whatever happens to have been drawn. Named here because the
// editor writes it, the renderer colours it and the harness builder reads it.
inline constexpr char kConnectionLineKey[] = "connectionLine";
// A mark drawn OVER a drawing rather than part of it - where a model connects, which way it runs, the faces
// its conductors leave by. A preview paints a drawing in one quiet grey so it reads as a drawing rather than
// as a colour chart; these keep their own colour, because with a mark the colour IS its name.
inline constexpr char kPreviewMarkKey[] = "previewMark";

// NOT ON THE DRAWING YET. A preview shows what has been applied AND what is being described, and the
// second must be told from the first at a glance - otherwise somebody reads a step they have not
// committed as part of the harness and stops checking it. Drawn in place, so applying changes only how
// it is drawn and never where it sits: a step that moved when it was applied would make the preview a
// picture of something else.
inline constexpr char kPreviewGhostKey[] = "previewGhost";
// THE STEP STANDING SELECTED, so it can be picked out of a drawing of everything.
//
// A list of steps and a picture of the harness are two views of one thing, and until they were joined
// up the only way to find out which box on the sheet was step 1.2 was to change it and watch. Marked
// here rather than drawn differently at source, the same way a ghost is: the builder makes the harness,
// and what is being looked at is the window's business.
inline constexpr char kPreviewSelectedKey[] = "previewSelected";

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
