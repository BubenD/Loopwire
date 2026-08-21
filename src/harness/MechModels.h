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
    // The pieces a harness drawing is actually made of, each drawn once by an office. Several of these are
    // ONE KIND with several SUB-DRAWINGS rather than a kind each - a backshell is a backshell whether it
    // leaves straight, at 45 or at 90 degrees - because a kind holds as many named models as are drawn and
    // the wizard picks between them by name. A kind per angle would put the same part in three places and
    // give the generator three things to keep in step.
    CircularConnector,      // a circular connector, MIL-DTL-38999 and its like
    CircularBackshell,      // straight / 45 degrees / 90 degrees, as named models of this one kind
    BranchSplitHeatshrink,  // the moulded boot over a branch, as opposed to plain shrink over a bundle
    Label,                  // a label on a run
    LabelUnderHeatshrink,   // the same, covered over with clear heatshrink
    TSplit,                 // a split that goes both ways at once, as against a Y
    DTypeConnector,         // a D-type, which is a different shape of thing from a circular
};

// THE SUB-DRAWINGS A KIND COMES IN, by name.
//
// Most of these parts are not one drawing. A boot leaves straight, at 45 or at 90; a ring is insulated or it is
// not; the wires between a connector and its cable are two run straight, or two spread, or three, or eight. Each
// of those is a drawing of its own that an office draws once - and they are all the same KIND, because the
// generator asks for "the boot" and the wizard picks which one by name.
//
// They are listed here so the manager can show them before anybody has drawn one: a kind with nothing under it
// tells you nothing about what it wants, and "draw the eight variants this needs" is not something to have to
// know. An office that wants another one adds it; one it never uses it simply never draws.
std::vector<QString> defaultSubModelNames(MechModelKind kind);

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

    // WHERE THE WIRES COME OUT, and how much room they have.
    //
    // A connection point says where the part meets the run; it says nothing about the face the conductors
    // leave by. On a real part that face is a LINE across the end - the back of a connector, the mouth of a
    // backshell, each branch of a split - and the wires leaving it must be spread ALONG that line and must
    // not run off either end of it. A generator that only knows a point either fans the wires out of one
    // spot or spreads them past the edge of the part, and both are drawings that get sent back.
    //
    // So a model marks its output lines: two points each, in the model's own coordinates. A part with one
    // face has one; a Y-split has one per branch, in the order they were marked, which is the order the
    // generator hands the branches out in.
    struct OutputLine {
        Vec2 a, b;
    };
    std::vector<OutputLine> outputs;

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

    // HOW FAR PAST THE OUTERMOST RUN it goes, at each end, when it is placed across a set of them.
    //
    // A label is laid across the runs it labels and has to cover all of them, and a label that stops flush
    // with the first and last wire reads as a label that missed them - so it goes a little over. How much is
    // a house style, not a calculation: an office that draws 2 mm clear draws 2 mm clear on every sheet it
    // releases, whether the bundle is three wires or thirty. Guessing it from the bundle's own width gave a
    // different answer every time, which is the one thing a house style must not do.
    //
    // Zero falls back to a proportion of the size, so a model captured before this existed places as it did.
    double exceedMm = 0.0;
    // Which kinds it means anything for: the two that are placed ACROSS a set of runs by a stroke rather
    // than along one run.
    static bool kindHasExceed(MechModelKind kind) {
        return kind == MechModelKind::Label || kind == MechModelKind::LabelUnderHeatshrink;
    }

    bool isEmpty() const { return drawing.isEmpty(); }
    QJsonObject toJson() const;
    void fromJson(const QJsonObject& o);
};

// A drawing the program itself knows how to make, for the kinds where a sensible one can be stated in
// geometry rather than having to be drawn by hand.
//
// It is a STARTING POINT, not a house style: the whole reason default models exist is that every office
// draws these its own way, and one captured off a real sheet always beats one written in code. But a button
// that places nothing until somebody has drawn a part is a button nobody can use, so a label - a flag on the
// run, and the same under a clear sleeve - is built in. Capturing over it replaces it for good.
//
// Returns an empty model for a kind with nothing built in, which is most of them: a connector, a backshell
// or a Y-split is too much somebody's own drawing to be guessed at here.
MechModel builtInMechModel(MechModelKind kind);

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
    // The set the application was BUILT with - the house style, inside the executable. Used when the
    // machine has no set of its own, so a fresh install draws in the house style from the first drawing
    // rather than from whenever somebody gets round to drawing all the models again.
    static QString shippedPath();
    // Whether the shipped set is used when a machine has no file of its own. On, always, in the
    // application - that is the point of shipping it. OFF for a test that starts from "nothing is drawn
    // for this kind yet" and checks what the program does about it: such a test has to start from
    // nothing, and with the house style shipped inside the binary there is no such thing as nothing
    // unless it can be asked for.
    static void setShippedDefaultsEnabled(bool on);
    static bool shippedDefaultsEnabled();
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

// THE LAYER THE MARKS ARE DRAWN ON. They are chrome over the part, not part of it: a layer of their own is
// what keeps them out of the part's extent, out of a capture, and out of the way of a selection.
inline constexpr char kMechModelGuideLayer[] = "«guide»";

// THE MARKS A MODEL IS PLACED BY, DRAWN. Where it connects and which way it runs from there (blue, with an
// arrow, because the direction is half of what that mark says), the two ends that must not stretch (orange
// and green), and the faces the conductors leave by (purple, barred at each end and numbered in the order
// they were marked, which is the order the generator hands the branches out in).
//
// ONE place decides what a marked-up model looks like, because it is shown in two: on the canvas while it is
// being marked up, and in the manager's preview where a model is chosen and checked. Those two disagreeing
// is worse than either being wrong - a mark seen in one window has to be the same mark in the other, or
// neither can be trusted.
//
// `markerRadius` is how big a mark is on this part (a ring three millimetres across on a part two metres
// long is a speck, so it cannot be a constant); `armMm` is how long the direction arrow reaches.
std::vector<Entity> mechModelMarkEntities(const MechModel& model, double markerRadius, double armMm);

// How long the model was drawn: what it says, or the extent of its own drawing.
double mechModelDrawnLength(const MechModel& model);
double mechModelDrawnAcross(const MechModel& model);
// Distance between the two marked connection points, or 0 when the model has no complete pair.
double mechModelAnchorLength(const MechModel& model);

} // namespace Edraw_Desktop
