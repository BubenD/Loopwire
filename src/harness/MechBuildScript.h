#pragma once

#include "harness/MechanicalHarness.h"
#include "harness/PartsLibrary.h"

#include <QHash>
#include <QJsonObject>
#include <QString>
#include <vector>

namespace Edraw_Desktop {

// ==================================================================================================
// THE BUILD SCRIPT: a harness as the ORDER IT IS BUILT IN, rather than as the tree it ends up being.
//
// WHY THIS EXISTS AT ALL
//
// MechHarnessDef describes the finished object - wires, nodes, segments - and it is the right thing to
// hand a renderer, a bill of materials or a fit check. What it cannot say is the ORDER somebody built
// it in, and on a harness drawing that order IS the document: the numbered steps down the side of the
// sheet are what the shop works to.
//
// The old wizard got round this with a fixed script - eleven steps in a hard-coded enum, walked over
// whatever tree it found. That works only while every harness has the same shape as every other. The
// moment a step sequence is the user's to choose - two heatshrinks in a row, a label between them, a
// node that goes three ways - a tree can hold the RESULT but cannot tell you the order, and reopening
// the wizard has to guess at what somebody meant.
//
// So the script is authored and the tree is COMPILED from it. Everything downstream of MechHarnessDef
// carries on unchanged; what is added is a thing that remembers how the harness was described.
//
// WHAT THE APPLICATION DOES AND DOES NOT KNOW
//
// It does NOT know what comes next. There is no suggested step and no enforced order: the user says
// what to place and the script records it. Guessing would be wrong as often as it was right, and a
// wizard that argues with a harness engineer about what follows a backshell is worse than a blank
// list.
//
// What it DOES do is check the neighbours. When two adjacent steps both carry real figures - a
// contact's wire range against the conductor's gauge, a boot's entry against the bundle it has to
// pass - the pair can be compared and a mismatch reported. That is worth having precisely because it
// needs no guess: it is arithmetic on numbers the library already states.
//
// And when the figures are NOT there, nothing is claimed. A part typed in by hand with no dimensions,
// or a library part whose datasheet never gave a range, produces a GAP - "this could not be checked" -
// and never a pass. Reporting a pass on absent data is how a boot that will not go on reaches a
// released drawing.
// ==================================================================================================

// A part counted in the bill of materials but never drawn.
//
// A cable body's braided shield and the heatshrink over it are bought, priced and issued to the floor,
// and drawing them would say something false: they are inside the jacket, or they are the jacket's own
// construction, and the drawing shows the cable. So they are carried on the step that owns them, reach
// the bill, and are invisible to the renderer.
struct MechSubPart {
    QString libPart;                        // the library part, when it has one
    PartKind kind = PartKind::Other;
    QString name;
    double qty = 1.0;
    QString unit;                           // "each", or "mm" for anything cut off a reel
    // ALWAYS true today, and named rather than assumed so the day something wants to be drawn as well
    // as counted, the question is asked here instead of the flag being discovered to mean two things.
    bool bomOnly = true;
};

// ONE GROUP OF CONDUCTORS in a run: so many of one size.
//
// A hook-up run is not "four of 0.5". It is whatever the circuit needs - two of 0.5 and one of 1.5,
// with a screened pair beside them - and asking for a single count and a single gauge forced that to
// be described as several runs, which drew several legs of harness where there is one bundle.
// SOMETHING A RUN CARRIES OVER ITSELF: a braided screen, a sleeve, a length of heatshrink.
//
// The same three numbers a fitting has, because that is what it becomes - see fittingFor. A length of
// zero means the whole run, which is what a screen over a branch usually is; `atMm` is how far along
// the run it starts, for the heatshrink that covers only the last stretch of it.
struct MechStepCovering {
    PartKind kind = PartKind::ShieldBraid;
    QString libPart;                        // from the library, or empty for the standard drawing
    QString name;                           // what the drawing calls it
    double lengthMm = 0.0;                  // 0 -> the whole run
    double atMm = 0.0;                      // how far along the run it starts
    double idMm = 0.0;
    double odMm = 0.0;
    bool balloon = true;

    QJsonObject toJson() const;
    static MechStepCovering fromJson(const QJsonObject& o);
};

struct MechConductorGroup {
    int count = 0;
    QString gauge;              // "0.5" or "22 AWG"
    QString libPart;            // the wire or cable part, when one is chosen
    QString name;               // what the schematic calls them

    // OVER THE INSULATION, as the library part stated it. 0 means "work it out from the gauge" - the
    // nominal table - which is what a group typed rather than picked from the library falls back to.
    //
    // Carried on the group because the compiler has no library to ask: compileMechScript takes a
    // script and nothing else, on purpose, so a script compiles to the same harness on a machine
    // where that library is not connected. The diameter is looked up ONCE, when the wires are taken
    // from the library, and travels with them - which is also what makes the bundle the drawing shows
    // the bundle the data sheets actually add up to, rather than the nominal for the gauge.
    double odMm = 0.0;

    // A DESIGNATOR PER WIRE, where the wires are named one at a time rather than as a group.
    //
    // A harness that starts at a connector needs none: the wires are known by the way they go into, and
    // "P1-3" names a conductor without anybody typing it. A harness that starts OPEN - bare wires,
    // ferrules, ring terminals landing on a terminal block - has no connector to be named from, and the
    // designators are then the only thing identifying which wire is which. On those drawings they are
    // not a convenience, they are the document.
    //
    // Shorter than the group's count means the rest fall back to the group name; longer is harmless.
    QStringList designators;

    QJsonObject toJson() const;
    static MechConductorGroup fromJson(const QJsonObject& obj);
};

// One step of the build. What is placed, what it is, which way it goes, and whether it is dimensioned.
struct MechStep {
    QString id;
    PartKind kind = PartKind::Other;

    // The step this one FOLLOWS. Empty on the first step. This is what makes the script a tree: two
    // steps naming the same parent are two branches off it, which is how a node goes several ways
    // without a separate branch table to keep in step with anything.
    QString parentStep;

    // From the library, or not. Empty means "the default drawing for this kind" - the same hybrid the
    // mechanical module engine already uses for fittings, so a step can be placed before anybody has
    // decided which part number it will be.
    QString libPart;
    QString name;                           // what the drawing calls it

    // THE DIRECTION ARROW, relative to the step before it. 0 carries straight on, +90 turns up.
    // Relative rather than absolute for the same reason MechNode::angleDeg is: moving a branch takes
    // its own sub-branches with it.
    double turnDeg = 0.0;

    // ---- What the step is made of, by kind -------------------------------------------------------
    double lengthMm = 0.0;                  // a run's length; a fitting's own length
    int wireCount = 0;                      // a run of loose wires: how many
    QString gauge;                          // and of what cross-section ("0.5", "22 AWG")
    double odMm = 0.0;                      // a cable's jacket diameter; a fitting's outside
    double idMm = 0.0;                      // a fitting's inside
    int pinCount = 0;                       // a connector's positions
    QString contactPart;                    // the contact fitted in them, when one is chosen
    int contactQty = 0;                     // how many positions are populated (0 = all of them)

    // ---- Dimension this step ---------------------------------------------------------------------
    // Per step and not per sheet, because which sizes belong on a drawing is a decision each time:
    // where a boot sits earns a dimension, twenty ties along a trunk would bury the sheet. `dimText`
    // is what the dimension is to SAY, which on a build drawing is the nominal figure rather than
    // whatever the model measures after tolerances.
    bool dimension = false;
    QString dimText;

    // SEVERAL GROUPS OF CONDUCTORS, for a run that carries a mixture. When this is empty the older
    // single-group fields above (wireCount + gauge) are used instead, which keeps a step that only
    // ever carried one size as simple as it was.
    std::vector<MechConductorGroup> conductors;

    // WHAT THIS RUN CARRIES OVER ITSELF, described with the run rather than as steps hung off it.
    // See MechStepCovering. Empty for a plain run, which is every run described before there was a
    // branch to describe.
    std::vector<MechStepCovering> coverings;

    // THE TRUNK, as opposed to a branch: the length of harness the branches gather INTO.
    //
    // A trunk that says nothing about what it carries carries what arrives at it - which is the whole
    // point of a trunk, and what stops somebody having to type the same forty conductors twice. It is
    // marked rather than guessed at, because a run that happens to have two runs arriving at it is not
    // necessarily the trunk: a splice has that shape too.
    bool mainBody = false;

    // HOW MUCH BARE WIRE SHOWS AT EACH END of this run - the stretch the screen, the sleeve or the
    // heatshrink stops short of. Zero takes the figure from the end it is at. See
    // MechSegment::openStartMm.
    // Negative: not said, so the end's own figure stands. Zero: none. See MechSegment::openStartMm.
    double openStartMm = -1.0;
    double openEndMm = -1.0;

    // IS THE BUNDLE INSIDE ANYTHING - a jacket, a sleeve, a length of heatshrink?
    //
    // A branch of loose hook-up wire is not: it is so many conductors running side by side the whole
    // way, and drawing a body round them says there is a covering there that the build sheet never
    // called for. Unticked, the run is conductors from end to end and there is nothing to cover; the
    // coverings above are then not offered, because they would have nothing to be over.
    bool bundled = true;

    // THE SAME CONDUCTORS CARRY ON THROUGH THIS RUN.
    //
    // This is what stops a harness inventing splices. A run that declares its own conductors starts
    // NEW ones, and two consecutive runs with different gauges are then - correctly - a gauge change
    // with a splice in it, which the drawing covers with heatshrink and notes.
    //
    // But two lengths of the same bundle are not that. Describing a harness as "200 mm, then 20 mm"
    // used to create two sets of wires, and if their gauges differed at all the drawing sprouted a
    // splice and a NOTES entry that nobody asked for. When this is set the run reuses the conductors
    // of the run before it, so the wires simply continue - which is what discrete wires running
    // through a harness actually do.
    bool continuesConductors = false;

    // A BALLOON POINTING AT THIS STEP'S BOM LINE. On by default, because a released harness drawing
    // balloons everything and a part that cannot be looked up in the table might as well not be in
    // it. Per step because the exceptions are real: nobody balloons all twenty ties along a trunk.
    bool balloon = true;

    // WRITE THIS RUN'S DESIGNATOR ON THE DRAWING. See MechSegment::showName - including why a run
    // between two parts does not get one however this is set.
    bool showName = false;

    // A BREAK MARK ON THIS RUN: "longer than it is drawn". A two-metre trunk on an A3 sheet is drawn
    // short with a break in it, and the dimension says the real length - which is why this is a
    // property of the run and not of the paper.
    bool lengthBreak = false;

    // A PART OF THE STEP BEFORE IT, rather than the next station along the harness.
    //
    // The tree could already nest to any depth - every step names its parent - but it could not say
    // WHICH KIND of following a step is doing. "P1, then its backshell, then the wires" and "P1, then
    // 300 mm of cable, then P2" are the same shape and different documents: the first is one station
    // described in three parts, the second is three stations.
    //
    // It changes two things. The step list indents a subpart under what it is a part of, so the build
    // sequence reads as the harness rather than as a flat run of everything anybody typed. And an END
    // subpart landing on a node that is ALREADY a connector becomes a SECOND HOUSING on it rather than
    // overwriting the first - which is what makes two connectors feeding one cable describable, and is
    // why MechStep no longer carries a list of extra ends of its own. See MechExtraEnd.
    bool isSubpart = false;

    // Counted, not drawn. See MechSubPart.
    std::vector<MechSubPart> subParts;

    QJsonObject toJson() const;
    static MechStep fromJson(const QJsonObject& obj);
};

// The whole sequence, and the one thing that is true of all of it.
struct MechBuildScript {
    QString name;
    // WHICH WAY THE HARNESS IS BUILT ACROSS THE SHEET. The first thing asked and the last thing worth
    // arguing about afterwards, because it decides where every subsequent step lands.
    //
    // It reaches the drawing as MechDrawOptions::baseDirectionDeg, which is the direction the ROOT is
    // laid in. Every other node is placed at its parent's direction plus its own turn, so flipping
    // this flips the whole tree and leaves every relative turn alone.
    bool leftToRight = true;

    std::vector<MechStep> steps;

    const MechStep* step(const QString& id) const;
    // The steps that follow this one, in the order they were added. More than one is a branch.
    std::vector<const MechStep*> childSteps(const QString& stepId) const;
    // The first step: the one nothing else precedes.
    const MechStep* firstStep() const;
    QString nextStepId() const;

    QJsonObject toJson() const;
    static MechBuildScript fromJson(const QJsonObject& obj);
};

// ---- What a step turns into ----------------------------------------------------------------------

// How a kind of thing takes part in the harness. Asked by kind rather than stored on the step, so a
// step whose kind is changed cannot keep a role that no longer matches it.
enum class MechStepRole {
    Run,        // a length of cable or of loose wires: a segment, and a node at the far end of it
    End,        // a connector or a terminal: the node it lands on becomes one
    Node,       // a breakout: the node it lands on goes several ways
    AtNode,     // a backshell, a boot, a label: a fitting at a point
    AlongRun,   // braid, heatshrink, a tie: a fitting over the run that arrives
};

MechStepRole mechStepRole(PartKind kind);

// ---- Compiling -----------------------------------------------------------------------------------

// The tree a script describes, plus the trail back from it.
//
// `stepToNode` and `stepToSegment` are what let a finding about a segment be shown against the step
// that created it. Without them the wizard could report "the run to BR1 is too thin" about a run the
// user never named, having only ever said "cable, 300 mm".
struct MechCompiled {
    MechHarnessDef def;
    QHash<QString, QString> stepToNode;      // step id -> the node it created or landed on
    QHash<QString, QString> stepToSegment;   // step id -> the segment it created, for runs
    QHash<QString, QString> objectToStep;    // node or segment id -> the step that made it
    QStringList errors;                      // a script that cannot be compiled at all
};

MechCompiled compileMechScript(const MechBuildScript& script);

// ---- Whether the neighbours go together ----------------------------------------------------------

// A finding about one step, in the language of the step rather than of the compiled tree.
//
// Deliberately the same three kinds validateMechanicalHarness already uses, and for the same reason:
// a figure that is MISSING and a figure that is WRONG are different news. A gap means nobody has said
// yet; a violation means what has been said does not work. Reporting the first as the second trains
// people to ignore both.
struct MechStepIssue {
    MechIssueKind kind = MechIssueKind::Gap;
    QString stepId;
    QString stepName;
    QString neighbourStepId;                 // the other half of the pair, when it is a pair
    QString message;
};

// Walks each parent/child pair in the script and compares what both of them state.
//
// Only what is actually known is compared. Where either side is silent - a part with no library
// entry, a library part whose range fields are empty, a gauge nobody has typed - the result is a Gap
// naming what was missing, never a pass. `catalog` may be null, in which case every library-derived
// figure is unknown and the whole answer is gaps.
std::vector<MechStepIssue> mechScriptIssues(const MechBuildScript& script,
                                            const MechCompiled& compiled,
                                            const PartsCatalog* catalog);

} // namespace Edraw_Desktop
