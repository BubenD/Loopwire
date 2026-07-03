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

// AutoCAD "quick" trim: remove the piece of `line` the user clicked, bounded by
// the nearest cutting intersection on each side of the pick (cutting edges =
// `others`). If the picked piece sits between two cuts the line is split — `line`
// keeps the first part and the second part is returned via `extra`. If the pick is
// past the last cut, only that end is shortened. Returns true if anything changed.
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

// Rectangular array: returns clones (excluding the original at row0/col0).
std::vector<Entity> rectArray(const Entity& src, int rows, int cols,
                              double dx, double dy);

// Explode a composite entity into its primitive parts (lines). Returns empty if
// the entity is not explodable.
std::vector<Entity> explodeEntity(const Entity& e);

// Purge: fully separate an object into single primitives — double line -> two
// lines, double/filleted polyline -> line segments of each run, connector ->
// body + pin lines + text, rect/polygon/polyline -> segments. Returns empty for
// entities that are already primitive (line, circle, arc, text...).
std::vector<Entity> purgeEntity(const Entity& e);

} // namespace Edraw_Desktop
