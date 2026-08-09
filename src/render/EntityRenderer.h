#pragma once

#include "geometry/Vec2.h"
#include "model/Document.h"
#include "model/Entity.h"

#include <QColor>
#include <QPointF>

#include <vector>

class QPainter;

namespace Edraw_Desktop {

// World <-> screen mapping, matching Edraw's worldToScreen/screenToWorld:
//   screen.x =  world.x * scale + view.x
//   screen.y = -world.y * scale + view.y
struct ViewTransform {
    ViewState view;

    QPointF toScreen(const Vec2& w) const {
        return QPointF(w.x * view.scale + view.x, -w.y * view.scale + view.y);
    }
    Vec2 toWorld(const QPointF& s) const {
        return {(s.x() - view.x) / view.scale, -(s.y() - view.y) / view.scale};
    }
    double scale() const { return view.scale; }
};

// One physical line of a double run, AS DRAWN: its offset from the centreline with the trim/extend caps
// applied - both slots, for that side. Anything that has to answer questions about the line has to see
// the same line the drawing shows: a line the user extended to meet something could not be trimmed AT
// that something, because the extension lives in the cap and not in the run's own points.
void applyDoubleRunCuts(std::vector<Vec2>& rail, const Entity& e, int side);

// The pieces one physical line of a double run is drawn in: the capped line, broken wherever a trim
// took a piece out of the MIDDLE of it. A middle piece cannot be said with a trimmed end - there is no
// end there - so it is kept as a hole in that one line, which is also exactly what a run crossing
// another looks like on a harness drawing. Pass the rail with its caps already applied.
std::vector<std::vector<Vec2>> splitRailAtHoles(const std::vector<Vec2>& rail, const Entity& e,
                                                int side);

// How far an object actually REACHES ON THE SHEET, which is not the same thing as Entity::bounds().
// bounds() answers "where is the object", the question selection and hit-testing ask, and for a double
// run that is its centreline - a path the drawing never puts a line on. What is drawn is two lines
// gap/2 either side of it, and further still where a trim cap stretched one out to meet something.
// Anything deciding whether an object need be PAINTED has to use this instead, or it will throw away
// an object whose lines are on screen because the path between them is not.
Bounds drawnBounds(const Entity& e);

// Stateless entity painter. Draws a single entity in screen space using the
// supplied transform; resolves color/weight against the document's layers.
class EntityRenderer {
public:
    // If `overrideColor` is valid, the entity is stroked in that color (used to
    // highlight the current selection).
    static void draw(QPainter& p, const Entity& e, const Document& doc,
                     const ViewTransform& vt,
                     const QColor& overrideColor = QColor());

    // Resolve the effective stroke color for an entity (explicit or ByLayer).
    static QColor resolveColor(const Entity& e, const Document& doc);

    // Shield helpers (shared by the renderer and the live placement preview so they match):
    //   shieldRotation  - angle of the shield's major (ry) axis from the caught-wire spread
    //                     (first->last crossing); vertical when the wires are level.
    //   shieldOutline   - world-space points of the outline (ellipse or capsule) rotated by
    //                     `rot` about `center`, ready to map through a ViewTransform.
    static double shieldRotation(const std::vector<Vec2>& crossings);
    static std::vector<Vec2> shieldOutline(const Vec2& center, double rx, double ry, double rot,
                                           bool capsule);

    // Number of decimals shown on dimension labels (Options dialog).
    static void setDimensionDecimals(int d);
    static int dimensionDecimals();

    // Measurement units (Options dialog). The drawing's world unit is 1 mm; inch mode
    // divides by 25.4 for display. With dual units on, the other unit is appended in
    // parentheses, e.g. "25.40 (1.00\")".
    static void setLengthUnitInch(bool inch); // true = inch primary, false = mm
    static bool lengthUnitInch();
    static void setDualUnits(bool on);
    static bool dualUnits();
    // Format a world-space length (mm) for a dimension label per the settings above.
    // `decimals` < 0 uses the global dimension-decimals setting.
    static QString formatLength(double world, int decimals = -1);

    // Canvas background color, so a Wipeout fills with an opaque mask that matches
    // the model-space background (kept in sync by the CanvasWidget).
    static void setBackgroundColor(const QColor& c);
    static QColor backgroundColor();
};

} // namespace Edraw_Desktop
