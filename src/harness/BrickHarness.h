#pragma once

#include "harness/Harness.h"
#include "harness/PartsLibrary.h"
#include "model/Entity.h"

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

namespace Edraw_Desktop {

// ==================================================================================================
// A HARNESS DESCRIBED AS BRICKS.
//
// Two kinds of brick. An END is a part the harness lands on - a connector, a ferrule, a crimp, a
// terminal - or a JOINT the harness passes through: a splice, a resistor, a heatshrink joint, a
// Y-split. It is a library part, or a placeholder of its kind until one is chosen. A SPECIAL is what
// runs between ends: the wires fanned out of one and into another, a cable or heatshrink over that
// run, a twisted pair, a twisted bundle. Two ends are never linked to each other directly, because
// what is between them is exactly what the harness is.
//
// EVERY BRICK HAS TWO PORTS, left and right, and can be turned a quarter at a time. A link joins a
// port of one brick to a port of another. On an end, the port is the side its entry face is on - a
// connector has one face and a joint has one each side. On a special, the left port is where the run
// STARTS and the right where it FINISHES. A fanout may finish on several ends, each taking so many of
// its wires.
//
// Each brick has a place on a coarse grid - a column along the harness and a row across it - which is
// the low-detail picture the user arranges. The real drawing is made from it: ends in one column
// share an x, rows are a row pitch apart, and the columns are as far apart as the runs between them
// are long.
// ==================================================================================================

enum class BrickType { End, Fanout, Run, TwistedPair, TwistedBundle };
enum BrickPort { kPortLeft = 0, kPortRight = 1 };

QString brickTypeName(BrickType type);
QString brickTypeKey(BrickType type);
BrickType brickTypeFromKey(const QString& key);
inline bool brickIsSpecial(BrickType t) { return t != BrickType::End; }

// The kinds an end brick can be: the parts a harness finishes on, then the joints it passes through.
std::vector<PartKind> brickEndKinds();
std::vector<PartKind> brickJointKinds();
// A JOINT has an entry face each side; an end part has one.
bool brickKindIsJoint(PartKind kind);

// A LABEL on a run: on the whole bundle, or on one wire of a fan, so far from one end.
struct BrickLabel {
    int wire = -1;              // -1 = the whole harness; 0.. = that wire of the fan at `fromFinish`
    bool fromFinish = false;    // measured from the finish end rather than the start
    double distanceMm = 40.0;   // from that end's entry face, along the run
    QString text = QStringLiteral("LABEL");
};

// WHAT A SPECIAL IS DRAWN WITH. Below nought is "the drawing decides"; nought is a real figure.
struct BrickRunSpec {
    int wires = 4;
    double lengthMm = 150.0;       // along the harness, from the one face to the other
    double wireWidthMm = 2.0;      // one wire across, as drawn
    double fanStartMm = -1.0;      // how long the fan is at each end; <0 = as long as it needs
    double fanFinishMm = -1.0;
    double kneeRadiusMm = -1.0;    // the bends of the body
    double fanKneeMm = -1.0;       // the corners of the fans
    double fanScalePct = 95.0;     // the fans of a face spread over this share of it
    double bodyScalePct = 60.0;    // a cable or heatshrink this share of its face across
    double bodyWidthMm = 0.0;      // or this wide, when set
    // THE STACK, from the middle out: "wires", then any of "cable", "braid", "heatshrink" in the order
    // they go on. Only the outermost is seen - a braid under a heatshrink is not drawn.
    QStringList stack{QStringLiteral("wires")};
    RunEnds transitions = RunEnds::None;   // a transition heatshrink at the start, finish or both
    // Dimensions: the whole run, each fan, and what is between the fans.
    bool dimTotal = false;
    bool dimFans = false;
    bool dimBetween = false;
    std::vector<BrickLabel> labels;

    // What is outermost, and so what the body is drawn as.
    QString outermost() const;
};

struct Brick {
    QString id;
    BrickType type = BrickType::End;
    PartKind kind = PartKind::Connector;   // for an end
    QString libPart;                       // the library part, when one has been chosen
    QString name;                          // designator ("X1") or a name for a special
    double col = 0.0;                      // along the harness, in grid steps
    double row = 0.0;                      // across it; down the pane is +
    int rot = 0;                           // quarter turns clockwise on the pane
    bool balloon = false;
    BrickRunSpec run;                      // for a special
};

struct BrickLink {
    QString a;
    QString b;
    int portA = -1;    // the port of `a` it is on; -1 = whichever faces the other brick
    int portB = -1;
    int wires = 0;     // wires a fanout gives the end at this link; 0 = an even share
};

struct BrickDesign {
    std::vector<Brick> bricks;
    std::vector<BrickLink> links;
    // WHERE DIMENSIONS ARE TAKEN FROM at each end of a run: the end part's near point (its face) or
    // its far point (the back of it).
    bool dimFromFar = false;
    bool dimToFar = false;

    Brick* brick(const QString& id);
    const Brick* brick(const QString& id) const;
    QString addBrick(BrickType type, PartKind kind, double col, double row);
    void removeBrick(const QString& id);
    // LINK a port of an end to a port of a special. Refused, with the reason, when it would not
    // describe a harness: two ends or two specials, a port of a special already taken (a fanout may
    // finish on several ends, from one port), the same pair twice.
    bool connect(const QString& a, const QString& b, QString* why = nullptr, int portA = -1,
                 int portB = -1);
    void disconnect(const QString& a, const QString& b);
    bool linked(const QString& a, const QString& b) const;
    BrickLink* link(const QString& a, const QString& b);
    const BrickLink* link(const QString& a, const QString& b) const;
    QStringList linksOf(const QString& id) const;
    // Which port of `id` the link to `other` is on - the one stated, or the one facing it.
    int portOn(const QString& id, const QString& other) const;
    // The ends a special starts from (its left port) and finishes on (its right port).
    QStringList startsOf(const QString& specialId) const;
    QStringList finishesOf(const QString& specialId) const;
    // Every end of a special, the starts first.
    QStringList endsOf(const QString& specialId) const;

    QJsonObject toJson() const;
    static BrickDesign fromJson(const QJsonObject& o);
};

// The way a port faces, on the pane (y down) and on the drawing (y up), with the brick's turn in it.
Vec2 brickPortPaneDir(const Brick& b, int port);
Vec2 brickPortWorldDir(const Brick& b, int port);

// SYMMETRIC. The ends are laid out from the one most connected: it is column 0 on the axis, what its
// runs reach is the next column out, and so on; the ends of a column are spread evenly either side
// of the axis, keeping the order they were in. Each special sits halfway between its ends. What is
// linked to nothing is left where it was.
void arrangeBricks(BrickDesign& design);

// THE ROUTE OF A RUN from one entry face to another: it leaves `a` along `aOut` and arrives at `b`
// travelling against `bOut`, both square to their faces, with as few bends as that allows.
std::vector<Vec2> routeBetweenFaces(const Vec2& a, const Vec2& aOut, const Vec2& b, const Vec2& bOut,
                                    double leadMm, double shiftDeg = 45.0);

// One entry face of an end, once the design is laid out.
struct BrickFace {
    Vec2 centre{0.0, 0.0};
    Vec2 out{1.0, 0.0};        // square to the face, toward the harness
    double lengthMm = 20.0;
    Vec2 nearAt{0.0, 0.0};     // the part's near and far points, for dimensions
    Vec2 farAt{0.0, 0.0};
};

struct BrickBuild {
    std::vector<Entity> entities;           // every entity carries meta "brickId"
    QHash<QString, BrickFace> faces;        // by "<end id>:<port>"
    QStringList problems;                   // what could not be drawn, and why
};

// THE DRAWING: every end as its library part (or a placeholder of its kind) set down by its entry
// faces, every special as harness runs routed square from face to face, the fans of each face shared
// across it by wire count so that together they never exceed it, and the dimensions, labels, braid
// and transitions each run asks for.
// A balloon on an end is numbered by `itemOf` - the line of the bill its part is on, 0 for none ("?").
BrickBuild buildBrickHarness(const BrickDesign& design, const PartsCatalog* catalog,
                             const std::function<int(const QString& libPart)>& itemOf = nullptr);

} // namespace Edraw_Desktop
