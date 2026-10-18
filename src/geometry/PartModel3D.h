#pragma once

#include "geometry/Mesh3D.h"

#include <QColor>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <vector>

namespace Loopwire {

// ---- A part's 3D model, built in Loopwire ---------------------------------------------------------------------
//
// A library part with no CAD model of its own can be given one here, put together from simple solids - a box, a
// cylinder, a tube, a cone, a sphere, a ring, a hexagon - and from the part's own 2D drawing (turned round the way it
// goes in, or given depth) and its pinout (its pins and sockets on its face). Each solid where it is put, turned and
// scaled, in its colour and what it is made of; a solid marked CUT is taken out of the others where it meets them, a
// hole or a slot. The part goes in along +X: its face at the +X end, its wires at the -X end.
//
// Kept as a file of its own (.e3d), in the part's data, like an attached model - its recipe, so it can be opened and
// changed again, and what is made from the part's drawing as it was when made.

// SKETCH: an outline drawn on a plane in the view, given depth. PIN: one pin or socket, put on a face where it was
// clicked - a socket a hole cut into what it is put on. REVOLVED: a sketch turned round one of its plane's directions.
enum class BodyShape { Box, Cylinder, Tube, Cone, Sphere, Torus, Hex, Turned, Extruded, Pins, Sketch, Pin, Revolved };
QString bodyShapeKey(BodyShape shape);
BodyShape bodyShapeFrom(const QString& key);
// What it is called where it is chosen ("Cylinder", "Turned from the drawing").
QString bodyShapeName(BodyShape shape);
// What its three sizes are, in turn - empty where a shape has no such size.
QStringList bodySizeLabels(BodyShape shape);
// Its sizes as a new one starts, in mm.
void bodyDefaultSizes(BodyShape shape, double& a, double& b, double& c);
// Whether it is one of the simple solids - what its edges can be rounded on, with Open CASCADE.
bool bodyIsPrimitive(BodyShape shape);
// Whether it can be combined with another, or cut - every one but the pins of a pinout.
bool bodyIsSolid(BodyShape shape);
// Whether it is drawn as a sketch: an outline on a plane, given depth or turned.
inline bool bodyIsSketch(BodyShape shape) { return shape == BodyShape::Sketch || shape == BodyShape::Revolved; }

// COMBINED WITH ANOTHER SOLID: added to it, taken out of it, or only what both hold kept.
enum class BodyOp { None, Add, Subtract, Intersect };
QString bodyOpKey(BodyOp op);
BodyOp bodyOpFrom(const QString& key);
// As a button says it ("Subtract").
QString bodyOpName(BodyOp op);

using SketchLoop = std::vector<std::pair<double, double>>;

struct Body3D {
    QString id;
    QString name;
    BodyShape shape = BodyShape::Box;
    double a = 20.0, b = 10.0, c = 10.0;   // its own sizes, as bodySizeLabels says
    V3 at{0.0, 0.0, 0.0};                  // where it is put
    V3 turnDeg{0.0, 0.0, 0.0};             // turned round X, then Y, then Z
    V3 scale{1.0, 1.0, 1.0};
    QColor color{176, 182, 192};
    int texture = 1;                       // a Texture3D: metal to begin with
    bool cut = false;                      // a hole: taken out of the solids it meets
    bool shown = true;
    // ITS EDGES ROUNDED, or cut off at 45 degrees, by so much - every edge of it (Open CASCADE); 0: sharp.
    double fillet = 0.0;
    double chamfer = 0.0;
    // A SKETCH: the plane it was drawn on (0 top - X and Y, drawn up Z; 1 front - X and Z, toward -Y; 2 side - Y and Z,
    // toward +X), and its outline in that plane's two directions. Its depth is `a`.
    int plane = 0;
    SketchLoop profile;
    // ITS HOLES: closed outlines inside the outline, through it. Given depth both ways from the plane, half each side.
    // A REVOLVED one turned round the plane's first direction (0) or its second (1), through the plane's origin; its
    // angle is `a`.
    std::vector<SketchLoop> holes;
    bool both = false;
    int axis = 0;
    // COMBINED WITH ANOTHER: `op` done to the body whose id is `target`, in the order the bodies are listed. None: a
    // solid of its own (or, `cut`, taken out of every other).
    BodyOp op = BodyOp::None;
    QString target;
    // A PIN: a socket or a pin, and its number in the pinout. Its diameter is `a`, its length `b`.
    bool female = false;
    int number = 0;
    // MADE FROM THE PART: a turned or extruded drawing, or the pins of a pinout (and its sockets) - in the body's own
    // space, kept as they were when made.
    Mesh3D made;
    Mesh3D madeSockets;
};

struct PartModel3D {
    std::vector<Body3D> bodies;
    QJsonObject toJson() const;
    static PartModel3D fromJson(const QJsonObject& o);
    bool save(const QString& path, QString* error = nullptr) const;
    static bool load(const QString& path, PartModel3D& model, QString* error = nullptr);
    // A new body's id, not yet taken.
    QString freshId() const;
    // The body with an id, or -1.
    int indexOf(const QString& id) const;
    // WHETHER ONE CAN BE COMBINED INTO ANOTHER: both solids, not the same, and the other not already made from it.
    bool canCombine(int body, int target) const;
};

// The extension a built model is kept under.
inline QString partModelExtension() { return QStringLiteral("e3d"); }
bool isPartModelFile(const QString& path);

// A body's shape in its own space - before it is turned, scaled and put where it goes.
Mesh3D bodyLocalMesh(const Body3D& body);
// Turned, scaled and put where it goes.
V3 bodyPlace(const Body3D& body, const V3& local);
Mesh3D bodyMesh(const Body3D& body);
// A SKETCH PLANE's three directions: the outline's two, and the way it is given depth.
void sketchAxes(int plane, V3& u, V3& v, V3& w);
// THE TURN that stands a pin up on a face: its length (+X) along the face's normal.
V3 turnAlong(const V3& normal);
// A point turned round X, then Y, then Z (degrees) - a body's turn - and turned back again.
V3 turnPoint(const V3& p, const V3& deg);
V3 turnPointBack(const V3& p, const V3& deg);

// ---- Outlines in a sketch ----
// Its area: more than nothing counter-clockwise.
double loopArea(const SketchLoop& loop);
bool loopContains(const SketchLoop& loop, double u, double v);
// THE SOLIDS A SKETCH MAKES: each outline not inside another, with the outlines just inside it as its holes - and an
// outline inside a hole a solid again.
struct SketchRegion {
    SketchLoop outer;
    std::vector<SketchLoop> holes;
};
std::vector<SketchRegion> sketchRegions(const std::vector<SketchLoop>& loops);

// THE MODEL AS IT IS DRAWN: each shown solid in its colour and what it is made of, the cut ones taken out of the others
// (where this build has Open CASCADE; where it has not, a cut is shown see-through red, and `note` says why). Pins and
// sockets of a Pins body as two items.
struct ModelItem3D {
    Mesh3D mesh;
    QColor color;
    int texture = 0;
    QString body;        // the id of the body it is
    double alpha = 1.0;
};
std::vector<ModelItem3D> modelItems(const PartModel3D& model, QString* note = nullptr);
bool modelBounds(const std::vector<ModelItem3D>& items, V3& lo, V3& hi);

// Written out for another program: STL (one solid) or OBJ (an object a body).
bool exportModelStl(const std::vector<ModelItem3D>& items, const QString& path, QString* error = nullptr);
bool exportModelObj(const std::vector<ModelItem3D>& items, const QString& path, QString* error = nullptr);

} // namespace Loopwire
