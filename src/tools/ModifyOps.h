#pragma once

#include "geometry/Vec2.h"
#include "model/Entity.h"

#include <optional>
#include <vector>

namespace Edraw_Desktop {

// Result of a fillet/chamfer: the two source lines are trimmed in place and a
// connector entity (arc for fillet, line for chamfer) is returned.
struct CornerResult {
    bool ok = false;
    bool tooBig = false;        // radius too large to fit on the picked lines (not applied)
    bool hasConnector = true;   // false for a radius-0 fillet (lines just meet at the corner)
    Entity connector;           // arc (fillet) or line (chamfer)
};

// Round the corner between two line entities with the given radius. `near1`/
// `near2` are pick points used to choose which side of each line is kept.
CornerResult filletLines(Entity& l1, Entity& l2, double radius,
                         const Vec2& near1, const Vec2& near2);

// Bevel the corner between two line entities by the given setbacks.
CornerResult chamferLines(Entity& l1, Entity& l2, double d1, double d2,
                         const Vec2& near1, const Vec2& near2);

// Trim a line to the cutting intersection nearest the pick point, removing the
// portion on the pick side. Returns true if modified.
bool trimLineAt(Entity& line, const Vec2& pick,
                const std::vector<Entity>& others);

// Trim a polyline centreline (given by its vertices) at `pick`, bounded by where
// `cutters` cross it (double lines/polylines use their physical lines; `selfId` is
// skipped). Returns the kept vertex-list(s): empty (nothing removed), one
// (shortened) or two (split). Used to trim polylines and double lines/polylines
// (pass [a,b] for a double line).
std::vector<std::vector<Vec2>> trimPolylinePieces(const std::vector<Vec2>& pts,
                                                  const Vec2& pick,
                                                  const std::vector<Entity>& cutters,
                                                  const QString& selfId);

// True if entity `e` (line / circle / arc) intersects any entity in `others`
// (double lines / polylines are tested on their physical lines).
bool objectCrosses(const Entity& e, const std::vector<Entity>& others);

// True when `e` lies wholly within the circle `ref` - every part of it inside the ring, its ends
// allowed to sit exactly ON the ring. A chord drawn between two points of a circle is inside it, not
// crossing it, and neither is anything that never reaches the ring at all.
//
// It matters to trimming: such a thing has NOTHING outside the boundary to keep, so trimming it back
// to the circle leaves nothing and the whole object goes. Without this it cannot be trimmed at all -
// there is no crossing to cut at.
bool whollyInsideCircle(const Entity& e, const Entity& ref);

// The boundary segment of a cutting edge in `cutters` (physical lines for double
// entities; `selfId` skipped) that passes through point `p`, or nullopt. Used to
// record the exact cut line so a trimmed double line's physical runs end on it.
std::optional<std::pair<Vec2, Vec2>> cutSegmentThrough(const Vec2& p,
                                                       const std::vector<Entity>& cutters,
                                                       const QString& selfId);

// The circle/arc cutting edge in `cutters` (skipping `selfId`) whose ring passes
// through `p`, as {center, radius}, or nullopt. Used to record a curved cut so a
// trimmed double line's physical runs end exactly on the circle.
std::optional<std::pair<Vec2, double>> cutCircleThrough(const Vec2& p,
                                                        const std::vector<Entity>& cutters,
                                                        const QString& selfId);

// Trim a circle or arc: remove the piece containing `pick` (a point on the
// curve), bounded by where the cutting edges in `cutters` cross it. A circle needs
// two cuts and becomes an arc; an arc may split — `extra` returns the second arc.
// Returns true if `e` was modified.
bool trimCircleAt(Entity& e, const Vec2& pick, const std::vector<Entity>& cutters,
                  std::optional<Entity>& extra);

// Quick trim: remove the piece of `line` the pick is in, bounded by the cutting edges in `others`.
//
//  - a cut on one side only: everything from it to the near end goes (trimming a tail off);
//  - between two boundaries: the piece between them goes. When those two are the crossings of one
//    circle, that is everything inside the ring, so both ends of what is left sit ON the circle -
//    which is what trimming to a circle has to mean.
//
// A split leaves the first piece in `line` and returns the second via `extra`. True if anything
// changed.
bool trimLineQuick(Entity& line, const Vec2& pick, const std::vector<Entity>& others,
                   std::optional<Entity>& extra);

// Extend a line's nearest end to the first boundary it would meet. Returns true
// if modified.
bool extendLineTo(Entity& line, const Vec2& pick,
                  const std::vector<Entity>& others);

// Extend a Line, DoubleLine (centreline), or Polyline (its end nearest `pick`) to
// the first boundary in `others` it would reach (any boundary type; circles/arcs/
// ellipses tessellated). Returns true if modified.
bool extendEntityTo(Entity& e, const Vec2& pick, const std::vector<Entity>& others);

// ---- Array (AutoCAD ARRAYRECT / ARRAYPOLAR / ARRAYPATH) --------------------

// Which array law lays the items out. Path is AutoCAD's ARRAYPATH — the items
// track along a picked object.
enum class ArrayKind { Rect, Polar, Path };

// Every parameter of an array, matching the fields of AutoCAD's "Array Creation"
// ribbon. `count` and the row block apply to all three kinds; the rest is
// per-kind. Elevation/levels (3D) have no meaning in a 2D drawing and are absent.
struct ArraySpec {
    ArrayKind kind = ArrayKind::Rect;

    // Shared: the item count along the array's main direction (Rect = columns,
    // Polar = items around the centre, Path = items along the path), plus the
    // parallel rows every kind can have.
    int count = 4;
    int rows = 1;
    double rowSpacing = 30.0; // Rect: along the row axis; Polar: radial; Path: perpendicular

    // Rectangular.
    double colSpacing = 30.0; // along the column axis (negative = leftward)
    double axisAngle = 0.0;   // degrees CCW; rotates BOTH grid axes

    // Polar.
    Vec2 center{0.0, 0.0};
    double fillAngle = 360.0; // total sweep in degrees
    bool rotateItems = true;  // items spin with their orbit (AutoCAD "Rotate items")
    bool ccw = true;          // sweep direction

    // Path.
    bool divide = true;        // spread `count` evenly over the path ("Divide"); else "Measure"
    double pathSpacing = 40.0; // distance between items when !divide
    double startOffset = 0.0;  // arclength before the first item
    bool alignToPath = true;   // items rotate to follow the tangent
};

// One array item, as a transform of the source: rotate by `rotate` radians about
// the array pivot (arrayPivot), THEN translate by `delta`. Item 0 is the source
// item itself — identity for Rect/Polar, and for Path the move that drops the
// source onto the path (as AutoCAD's ARRAYPATH does).
struct ArrayPlacement {
    double rotate = 0.0;
    Vec2 delta{0.0, 0.0};
};

// The point every placement's rotation turns about: the polar centre for a polar
// array, otherwise the array's base point.
Vec2 arrayPivot(const ArraySpec& s, const Vec2& base);

// The transforms of every item of the array. `base` is the base point of the
// source selection (its bounding-box centre); `path`/`pathClosed` are only read
// for ArrayKind::Path (see arrayPathPoints). Items that would fall off the end of
// an open path are omitted, so the result can be shorter than count*rows.
std::vector<ArrayPlacement> arrayPlacements(const ArraySpec& s, const Vec2& base,
                                            const std::vector<Vec2>& path = {},
                                            bool pathClosed = false);

// A fresh clone of `src` with one placement applied (rotation about `pivot`).
Entity arrayItem(const Entity& src, const ArrayPlacement& pl, const Vec2& pivot);

// The distance stepped along the path between consecutive items of a path array:
// "Divide" derives it from the path length (a closed path divides by the item
// count, an open one by the gaps), "Measure" uses the spec's own spacing. Shared by
// arrayPlacements and the ribbon, so the number shown is the number used.
double arrayPathStep(const ArraySpec& s, double pathLength, bool pathClosed);

// The drawn centreline of `e` as an ordered polyline, for use as a path-array
// path: curves are tessellated, bulged/filleted polylines follow their real
// outline, and closed shapes repeat their first point at the end (with
// `*closedOut` set). Empty for entity types that are not a usable path.
std::vector<Vec2> arrayPathPoints(const Entity& e, bool* closedOut = nullptr);

// Rectangular array: returns clones (excluding the original at row0/col0).
std::vector<Entity> rectArray(const Entity& src, int rows, int cols,
                              double dx, double dy);

// Explode a composite entity into its drawn primitives (AutoCAD EXPLODE): double
// line -> two lines, double/filleted polyline -> line segments of each run,
// connector -> body + pin lines + text, rect/polygon/polyline -> segments. Returns
// empty for entities that are already primitive (line, circle, arc, text...).
std::vector<Entity> explodeEntity(const Entity& e);

} // namespace Edraw_Desktop
