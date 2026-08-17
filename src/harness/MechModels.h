#pragma once

#include "model/Entity.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <vector>

namespace Edraw_Desktop {

// ---- Default mechanical models -------------------------------------------------------------------
// The standard pieces a harness drawing is made of, each drawn ONCE and used everywhere.
//
// A library part is a particular thing you buy - this connector, that backshell - and its drawing is
// its own. These are the other half: the pieces that are not a part number but a kind of thing. A
// length of wire. A cable. Heatshrink over a transition. The wires between a connector and the cable
// they come out of. A ring terminal. Whatever house style a drawing office has for those, it has ONE,
// and every drawing it releases uses it.
//
// So they are drawn once, here, and stored as default models. The generator draws a heatshrink by
// fetching the model for heatshrink rather than by making a rectangle up, and a drawing office that
// draws its heatshrink with a chamfer at each end gets that everywhere without touching the generator.
//
// What makes a model different from a block: LENGTH IS NOT PART OF IT. A wire model is drawn at some
// convenient length, and the harness says how long the wire actually is. So a model carries the two
// ends that must NOT stretch - the flush end of a wire, the crimp of a ferrule, the taper of a boot -
// and everything between them is stretched to make up the length. The ends stay as drawn at any
// length, which is the whole difficulty with scaling a drawing and the reason a model is not a block.
enum class MechModelKind {
    RingInsulated,          // a ring terminal, insulated
    RingUninsulated,
    FerruleInsulated,       // a wire-end ferrule, insulated
    FerruleUninsulated,
    TransitionHeatshrink,   // over the step from a cable to the conductors out of it
    Cable,                  // a length of cable: jacket, and its ends
    Wire,                   // a length of conductor
    OpenWires,              // conductors running bare, as a set
    ConnectorToCableWires,  // the wires between a connector and the cable they come out of
    YSplit,                 // the split itself
    Heatshrink,             // a length of heatshrink over a bundle
    Boot,                   // a boot behind a connector
    CableBody,              // the body of a cable between its ends
};

// The stamp that makes a model ONE object rather than the loose lines it happens to be drawn with: on its
// own editing canvas, so it can be picked up and placed as the part it is, and on a generated drawing, so
// a boot on a released sheet is a boot and not fifteen lines. Shared by both, and deliberately not a
// block NAME - a model is not a file in the block folder, so a double-click must not go looking for one.
inline constexpr char kMechModelGroup[] = "«model»";

std::vector<MechModelKind> allMechModelKinds();
// "Ring terminal" / "Ferrule" - insulation is a model property shown in the properties panel.
QString mechModelName(MechModelKind kind);
// The stable key a file stores ("ring" / "ferrule"), so renaming a label never breaks a file.
QString mechModelKey(MechModelKind kind);
bool mechModelKindFromKey(const QString& key, MechModelKind& out);
// Whether this kind is drawn to a LENGTH the harness decides - a wire, a cable, heatshrink - or is a
// fixed thing that is only placed - a ring, a ferrule, a Y-split.
bool mechModelStretches(MechModelKind kind);
// Whether it is drawn for so many conductors at once (the open wires, and the wires between a
// connector and its cable), in which case the count comes from the harness too.
bool mechModelPerWire(MechModelKind kind);

// One model: the drawing, and what must not stretch in it.
struct MechModel {
    MechModelKind kind = MechModelKind::Wire;
    // Its NAME, so a kind can have more than one of them: a straight backshell and a right-angled one are
    // both backshells, and an office that draws two keeps two. Empty is the kind's own first model, which is
    // what every file written before this had, so those read back as the one model they were.
    QString name;
    // The one a kind uses when nothing else is said. Exactly one per kind: setDefault() sees to that.
    bool isDefault = false;
    bool insulated = true;
    bool hasNearPoint = false;
    bool hasFarPoint = false;
    Vec2 nearPoint;
    Vec2 farPoint;
    // As drawn, in millimetres, running along +x from the origin. Captured off the sheet like a part's
    // drawing, so it is drawn with the same tools as everything else.
    QJsonArray drawing;
    // The stretch: the length it was drawn at, and the piece at each end that keeps its own size
    // however long the thing has to be. Zero at both ends stretches the whole of it evenly, which is
    // right for plain heatshrink and wrong for anything with an end on it.
    double drawnLengthMm = 0.0;   // 0 = measured off the drawing
    double headMm = 0.0;
    double tailMm = 0.0;
    // Drawn at this diameter, so a model can be scaled across as well when it goes over a bundle of
    // another size. 0 = do not scale across.
    double drawnAcrossMm = 0.0;
    // For the per-wire kinds: how many conductors the drawing shows, so it can be redrawn for another
    // number by spacing them out rather than by squashing the picture.
    int drawnWireCount = 0;
    // WHERE IT CONNECTS, and WHICH WAY IT RUNS from there. Marked on the drawing rather than assumed: a
    // model drawn along +x from its own origin was the assumption, and a part drawn any other way - a
    // right-angled backshell, a boot that leaves at 45 degrees - could not be placed by it at all.
    //
    // The point is where the part meets what it is placed on, and the angle is the direction the part runs
    // FROM that point, as drawn. Placing it turns the drawing by (the run's direction - this angle), so it
    // comes out running along the run whichever way it happens to have been drawn.
    bool hasConnect = false;
    Vec2 connectPoint;
    double connectAngleRad = 0.0;

    // The GAP a cable or a wire leaves at each end: the piece of the run it does not cover, where the
    // conductors come out and are worked on. It is a property of how the office draws them - the LENGTH is
    // not, because that comes from the harness when the drawing is generated - so it is kept with the model
    // and taken off the length asked for, at each end, whenever the model is drawn. Zero for everything
    // else, and zero draws exactly as it always did.
    double gapMm = 0.0;
    // Which kinds a gap means anything for: a cable and a wire are drawn to a length the harness decides and
    // are the two that leave one.
    static bool kindHasGap(MechModelKind kind) {
        return kind == MechModelKind::Cable || kind == MechModelKind::Wire;
    }

    bool isEmpty() const { return drawing.isEmpty(); }
    QJsonObject toJson() const;
    void fromJson(const QJsonObject& o);
};

// Every model there is, saved as one file in the application's data folder - a drawing office's house
// style, in one place, moved from machine to machine as one file.
class MechModelSet {
public:
    // The one a kind uses: the one marked default, or the first there is. Every caller that only wants "the
    // heatshrink model" keeps working whether an office keeps one or six.
    const MechModel* of(MechModelKind kind) const;
    // A particular one, by name - and every one there is of a kind, in the order they were added.
    const MechModel* of(MechModelKind kind, const QString& name) const;
    std::vector<const MechModel*> all(MechModelKind kind) const;
    // Add or replace BY NAME. A model with a name a kind already has replaces that one; a new name is
    // another model of that kind.
    void set(const MechModel& model);
    void remove(MechModelKind kind);                          // every model of that kind
    void remove(MechModelKind kind, const QString& name);     // just that one
    bool rename(MechModelKind kind, const QString& from, const QString& to);
    // Which one the kind uses. Marking one unmarks the rest, because "the default" is one thing.
    bool setDefault(MechModelKind kind, const QString& name);
    bool has(MechModelKind kind) const { return of(kind) != nullptr; }
    int count() const { return static_cast<int>(models_.size()); }

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& o);
    // <app data>/mech-models.json
    static QString defaultPath();
    bool load(const QString& path = QString());
    bool save(const QString& path = QString()) const;

private:
    std::vector<MechModel> models_;
};

// A model's drawing, stretched to `lengthMm` and placed: from `at`, running in direction `dirRad`.
// `acrossMm` scales it across as well when the model says what it was drawn at (0 = leave it).
// `wireCount` redraws a per-wire model for that many conductors (0 = as drawn).
//
// The stretch: a point in the head keeps its distance from the start, a point in the tail keeps its
// distance from the end, and everything between is scaled. So a wire drawn 60 long with 4 mm flush
// ends, asked for at 500, comes out with the same two ends and 492 of wire between them.
std::vector<Entity> mechModelEntities(const MechModel& model, const Vec2& at, double dirRad,
                                      double lengthMm, double acrossMm = 0.0, int wireCount = 0,
                                      bool flip = false);

// How long the model was drawn: what it says, or the extent of its own drawing.
double mechModelDrawnLength(const MechModel& model);
double mechModelDrawnAcross(const MechModel& model);
// Distance between the two marked connection points, or 0 when the model has no complete pair.
double mechModelAnchorLength(const MechModel& model);

} // namespace Edraw_Desktop
