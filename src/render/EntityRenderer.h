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
// WHERE A DETAIL VIEW'S WORDS SIT, in world coordinates: the letter beside the circle round the thing
// (big = false) or the heading on the enlargement (big = true). An angle round the circle and a distance out
// from it, both stored on the view and both there to be moved - a letter fixed at one o'clock lands on the
// geometry about half the time.
//
// Asked by the renderer that draws them AND by the handle that drags them, because a handle in one place and
// the text it moves in another is a handle nobody can find.
Vec2 detailViewTextAt(const Entity& e, bool big);

// A detail view's words, when nobody has said otherwise. The height is what a drawing office writes at rather
// than what happens to fit; the source label goes straight ABOVE the boundary, where it is clear of the geometry
// the boundary is drawn round - at an angle it lands on whatever the circle was put over.
inline constexpr double kDetailTextMm = 5.0;
inline constexpr double kDetailTagAngleDeg = 90.0;
inline constexpr double kDetailViewAngleDeg = -90.0;

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

// The dash pattern a NAMED linetype draws with - CENTER, CENTERX2, HIDDEN, DOT2, PHANTOM, DASHDOT - in
// units of the PEN WIDTH, which is how Qt measures one, or an empty list for a solid line.
//
// One table, in one place, because the Properties dropdown draws a little sample of each and the drawing
// draws the line itself: two tables would let the sample and the line disagree, and the sample is the only
// thing anybody has to go on when choosing. `scale` is the object's own linetype scale.
QVector<qreal> linetypeDashes(const QString& name, double scale, double penWidth);

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

    // What a dimension SAYS. Normally the measurement it takes off its own geometry - a length, a
    // diameter, an angle - and where the object carries a text override, that instead: what the drawing
    // has to say where the number is not it ("REF", "SEE NOTE 4", "2x 12"). "<>" in an override stands
    // for the measurement, so "<> REF" is the number followed by REF, which is the convention AutoCAD
    // uses. The measurement itself is never changed by an override.
    static QString dimensionText(const Entity& e);

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
    static void setMonochrome(bool on);
    static bool monochrome();
    // Whether object lineweights are drawn at all. Off for a check print, where the geometry is what is
    // wanted and every line comes out the same thin one.
    static void setPlotLineweights(bool on);
    static bool plotLineweights();
    // How much bigger ANNOTATION is drawn than it is written. A drawing plotted at 1:10 needs its dimension
    // text ten times the height in the model for it to come off the paper the size it says - which is what
    // the scale in the corner of the strip means in model space. Geometry is never touched by it: the harness
    // is the size it is, and only what is written about it grows.
    static void setAnnotationScale(double factor);
    static double annotationScale();
    // On a white export page, a white drawing stroke must remain visible. It is
    // treated as black in color mode; other colors remain unchanged.
    static void setWhitePaper(bool on);
    static bool whitePaper();
};

} // namespace Edraw_Desktop
