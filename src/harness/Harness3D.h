#pragma once

#include "geometry/Mesh3D.h"
#include "model/Entity.h"

#include <QColor>
#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

namespace Loopwire {

// THE HARNESS IN 3D: its runs as tubes - a cable's body, the transition sleeves on it, each wire where the wires fan
// out - and each end part as its 3D model, fitted to where and how big it is drawn, or as a box where it has none.
struct Harness3DItem {
    Mesh3D mesh;
    QColor color;
    QString what;          // a part's name or P/N, or the run's
    bool model = false;    // a part shown by its own 3D model
    bool cube = false;     // a part shown as a box, having none
    int texture = 0;       // what it looks made of: a Texture3D
    double alpha = 1.0;
    bool ownLook = false;  // its colour and texture its own model's, not set by what it is
    QString brick;         // the wizard brick it is (on the sheet: the brick it was placed from)
};

struct Harness3DScene {
    std::vector<Harness3DItem> items;
    int parts = 0, modelled = 0, runs = 0;
    QStringList notes;     // what could not be read, and why
};

struct LibraryPart;

// HOW A PART WITH NO 3D MODEL IS SHOWN: a box as big as it is drawn, or a solid made from its 2D drawing - its outline
// turned round the way it goes into the harness (a round connector), or given depth across (a square one).
enum class PartLook { Box, Revolved, Extruded };

// From the objects of a harness drawing - placed on the sheet (each part by its copy mark) or straight out of the
// wizard (each by its brick). `modelOf` gives the 3D model file attached to a library part, or nothing.
// A connector's pins as its library part has them: how many, and which half of the pair it is (male pins, or female
// sockets) - for its front face.
struct PartPins {
    int count = 0;
    bool female = false;
    // EACH ONE, where it is known: its number, a socket or a pin, and where the pinout drawing has it on the face -
    // across (u) and up (v) from the face's middle, and its radius, in the face's own radius. Unplaced: laid out
    // evenly over the face, in its turn.
    struct Pin {
        int number = 0;
        bool female = false;
        bool placed = false;
        bool said = false;   // its gender said by a type of pin, not taken from the connector
        double u = 0.0, v = 0.0, r = 0.0;
    };
    std::vector<Pin> each;
    // THE MALE PINS' FRAME, where the pinout drawing outlines one round them: a pocket in the face the pins stand in.
    // A grid of cells over the face's square (-1..1 across and up, frameN a side, row by row from the bottom), set
    // where the pocket is. Empty: none.
    int frameN = 0;
    std::vector<unsigned char> frame;
    bool inFrame(double u, double v) const;
};
// THE FRAME ROUND THE MALE PINS of a pinout drawing: the region its lines and arcs close round them, found by filling out
// from each male pin to the lines - and none where that runs out to the face's edge or takes a socket in with it.
void pinoutFrame(const QJsonArray& drawing, PartPins& pins);
// WHERE A PINOUT DRAWING PUTS ITS PINS: each numbered circle, by the number written beside or in it, as a share of the
// big circle round them all - the face. Empty when the drawing has no such circles.
std::vector<PartPins::Pin> pinoutPositions(const QJsonArray& drawing);
// FOR A PART'S 3D MODEL, BUILT: a solid from its 2D drawing - turned round X, or given its width in depth - centred on
// the origin, going in along +X; and its pins and sockets on a face across X at the origin, so wide, so deep, pointing
// +X - each where its pinout drawing has it, a pin or a socket as its type says.
Mesh3D partDrawingSolid(const LibraryPart& part, bool revolve);
void partPinMeshes(const LibraryPart& part, double faceDiameter, double depth, Mesh3D& pins, Mesh3D& sockets);

// A connector part's pins as the 3D view shows them: its way count, female where it is a receptacle or a socket.
struct LibraryPart;
PartPins partPinsOf(const LibraryPart* part);

Harness3DScene buildHarness3D(const std::vector<Entity>& ents, const std::function<QString(const QString& libPart)>& modelOf,
                              PartLook look = PartLook::Box,
                              const std::function<PartPins(const QString& libPart)>& pinsOf = {});

// A model file read once and kept while it is unchanged on disk.
const Mesh3D& cachedMesh3D(const QString& path, QString* how = nullptr);

} // namespace Loopwire
