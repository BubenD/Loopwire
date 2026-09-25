#pragma once

#include "harness/Harness.h"
#include "harness/PartsLibrary.h"
#include "model/Entity.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace Edraw_Desktop {

// ==================================================================================================
// A HARNESS DESCRIBED AS BRICKS.
//
// Two kinds of brick. An END is a part the harness lands on - a connector, a ferrule, a crimp, a
// terminal, a splice - and is a library part, or a placeholder of that kind until one is chosen.
// A SPECIAL is what runs between two ends: the wires fanned out of the one and into the other, a
// cable or a heatshrink over that run, a twisted pair, a twisted bundle. A special is linked to the
// ends it runs between; two ends are never linked to each other directly, because what is between
// them is exactly what the harness is.
//
// Each brick has a place on a coarse grid - a column along the harness and a row across it - which is
// the low-detail picture the user arranges. The real drawing is made from it: ends in one column
// share an x, rows are a row pitch apart, and the columns are as far apart as the runs between them
// are long.
// ==================================================================================================

enum class BrickType { End, Fanout, Run, TwistedPair, TwistedBundle };

// "Connector", "Fanout", "Cable / heatshrink run", ... - what the palette and the pane show.
QString brickTypeName(BrickType type);
QString brickTypeKey(BrickType type);
BrickType brickTypeFromKey(const QString& key);
inline bool brickIsSpecial(BrickType t) { return t != BrickType::End; }

// The kinds of part an END brick can be, in the order the palette lists them.
std::vector<PartKind> brickEndKinds();

// WHAT A SPECIAL IS DRAWN WITH. Below nought is "the drawing decides"; nought is a real figure.
struct BrickRunSpec {
    int wires = 4;
    double lengthMm = 150.0;       // along the harness, from the one face to the other
    double wireWidthMm = 2.0;      // one wire across, as drawn
    double fanStartMm = -1.0;      // how long the fan is at each end; <0 = as long as it needs
    double fanFinishMm = -1.0;
    double kneeRadiusMm = -1.0;    // the bends of the body
    double fanKneeMm = -1.0;       // the corners of the fans
    double fanScalePct = 95.0;     // the fan spread over this share of the entry face
    RunBody body = RunBody::Wires; // what the gathered stretch is: the wires, a cable, a heatshrink
    double bodyScalePct = 60.0;    // a cable or heatshrink this share of the face across
    double bodyWidthMm = 0.0;      // or this wide, when set
};

struct Brick {
    QString id;
    BrickType type = BrickType::End;
    PartKind kind = PartKind::Connector;   // for an end
    QString libPart;                       // the library part, when one has been chosen
    QString name;                          // designator ("X1") or a name for a special
    double col = 0.0;                      // along the harness, in grid steps
    double row = 0.0;                      // across it; down the pane is +
    bool balloon = false;
    BrickRunSpec run;                      // for a special
};

struct BrickLink {
    QString a;
    QString b;
};

struct BrickDesign {
    std::vector<Brick> bricks;
    std::vector<BrickLink> links;

    Brick* brick(const QString& id);
    const Brick* brick(const QString& id) const;
    // A new brick at a grid place; returns its id.
    QString addBrick(BrickType type, PartKind kind, double col, double row);
    void removeBrick(const QString& id);
    // LINK an end to a special. Refused, with the reason, when it would not describe a harness: two
    // ends or two specials, a special already running between two ends, the same pair twice.
    bool connect(const QString& a, const QString& b, QString* why = nullptr);
    void disconnect(const QString& a, const QString& b);
    bool linked(const QString& a, const QString& b) const;
    // What a brick is linked to, in the order the links were made.
    QStringList linksOf(const QString& id) const;
    // The ends a special runs between, the one further left first.
    QStringList endsOf(const QString& specialId) const;

    QJsonObject toJson() const;
    static BrickDesign fromJson(const QJsonObject& o);
};

// SYMMETRIC. The ends are laid out from the one most connected: it is column 0 on the axis, what its
// runs reach is the next column out, and so on; the ends of a column are spread evenly either side
// of the axis, keeping the order they were in. Each special sits halfway between its two ends. What
// is linked to nothing is left where it was.
void arrangeBricks(BrickDesign& design);

// THE ROUTE OF A RUN from one entry face to another: it leaves `a` along `aOut` and arrives at `b`
// travelling against `bOut`, both square to their faces, with as few bends as that allows - none
// when the two faces look straight at each other, one when they are square to one another, a level
// shift at `shiftDeg` when they face each other off-line. `leadMm` is the least straight it leaves
// and arrives with, so a fan has room to be a fan before the first bend.
std::vector<Vec2> routeBetweenFaces(const Vec2& a, const Vec2& aOut, const Vec2& b, const Vec2& bOut,
                                    double leadMm, double shiftDeg = 45.0);

// Where one end brick's entry face is on the drawing, once the design is laid out.
struct BrickFace {
    Vec2 centre{0.0, 0.0};
    Vec2 out{1.0, 0.0};        // square to the face, toward the harness
    double lengthMm = 20.0;
};

struct BrickBuild {
    std::vector<Entity> entities;           // every entity carries meta "brickId"
    QHash<QString, BrickFace> faces;        // by end brick id
};

// THE DRAWING: every end as its library part (or a placeholder of its kind), set down by its entry
// face, and every special as a harness run routed square from face to face, its fans spread over the
// faces it lands on.
BrickBuild buildBrickHarness(const BrickDesign& design, const PartsCatalog* catalog);

} // namespace Edraw_Desktop
