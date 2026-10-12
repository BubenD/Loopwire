#pragma once

#include "harness/MechBuildScript.h"
#include "model/Entity.h"
#include "harness/PartsLibrary.h"

#include <QDialog>
#include <QHash>
#include <QString>

#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QToolButton;

namespace Loopwire {

class DrawingPreview;
class Editor;

// ==================================================================================================
// The harness build wizard: one step at a time, and the application does NOT know what comes next.
//
// HOW THIS DIFFERS FROM THE ONE IT REPLACES
//
// HarnessStepsDialog asked eleven fixed questions in a fixed order and showed all eleven in its list
// before a single one had been answered. That works while every harness has the same shape as every
// other, and stops working the moment somebody wants two heatshrinks in a row, a label between them,
// or a branch off a branch. It also asked the user to read a plan of a harness they had not described
// yet.
//
// So nothing is planned here. The list on the left grows as steps are added and shows what HAS been
// built, never what the application imagines will be. At each step the user picks a CATEGORY, then a
// part and its figures, then presses Next - and the step just described is added to the script.
//
// WHAT THE APPLICATION CONTRIBUTES
//
// Not prediction. It never suggests the next step, because a wrong suggestion in a harness engineer's
// way is worse than a blank list.
//
// What it contributes is CHECKING THE NEIGHBOURS: when two adjacent steps both carry real figures -
// a contact's gauge range against the conductor it is crimped onto, a boot's entry against the bundle
// it has to pass - the pair is compared and a mismatch is reported. And when the figures are not
// there it says so instead: a gap, never a pass. See mechScriptIssues().
//
// DIRECTION IS PER STEP AND STICKY
//
// Each step is drawn left or right along the sheet, and a new step starts out going whichever way the
// last one went - which is what makes a straight harness no work at all and a turn one click. The
// choice is stored as MechStep::turnDeg, which is RELATIVE to the step before it, so the wizard
// converts: what the user picks is an absolute direction on the paper, what the script keeps is the
// turn. That is the right way round, because moving a branch later has to take its own sub-branches
// with it.
// ==================================================================================================
class MechWizardDialog : public QDialog {
    Q_OBJECT

public:
    explicit MechWizardDialog(Editor* editor, QWidget* parent = nullptr);

    // ---- The categories offered ------------------------------------------------------------------
    //
    // A CATEGORY IS NOT A PartKind, quite. "Harness body" and "harness branch" are both a run of
    // conductors and compile to the same thing; what differs is where the user is in the tree and
    // what the wizard should therefore ask. Keeping the two apart here means the palette can speak
    // the language of the shop while the script stays in the language of the model.
    enum class Category {
        Connector,      // a connector at this end
        Terminal,       // a ring, fork or blade terminal - also an end
        Backshell,
        Boot,           // heatshrink boot
        Label,          // a marker or printed sleeve
        Heatshrink,     // over the run that arrives
        Braid,          // braided screen over the run
        Wires,          // loose hook-up wires: a run with no jacket
        Cable,          // a jacketed cable: a run drawn at its jacket
        HarnessBody,    // the trunk: the bundle the conductors already declared make
        HarnessBranch,  // a run leaving a node
        NodePoint,      // a breakout: the harness goes several ways from here
    };

    // What each category is called, and what it becomes.
    static QString categoryName(Category c);
    static PartKind categoryKind(Category c);
    // Every category, in the order the palette lists them: ends, then things fitted at a point, then
    // things over the run, then the runs themselves, then the tree.
    static std::vector<Category> categories();

    // ---- Driving it, from a test or from the keyboard ---------------------------------------------

    const MechBuildScript& script() const { return script_; }
    void setScript(const MechBuildScript& script);

    Category currentCategory() const { return category_; }
    void setCurrentCategory(Category c);

    // Which way the step being described is drawn. True = to the right along the sheet.
    bool currentGoesRight() const { return goRight_; }
    void setCurrentGoesRight(bool right);

    // Add the step as described and move on - what Next does. Always adds a NEW step after the
    // anchor, whether or not a step is currently being edited.
    void commitStep();
    // Take the last step back off.
    void undoStep();

    // ---- Editing a step already described --------------------------------------------------------
    //
    // Clicking a step in the list edits it rather than starting a new one. That is why Next is not the
    // dialog's default button: pressing Enter after changing a figure used to commit a whole new step,
    // which is the opposite of what somebody correcting a length wants.
    void editStep(const QString& stepId);
    QString editingStepId() const { return editing_; }
    // Write the fields back onto the step being edited. Does nothing when a new step is being
    // described - there is nothing to write back to yet.
    void applyToEditedStep();

    // DELETE ANY STEP, not only the last.
    //
    // Anything that followed it would be orphaned, so its children are re-parented onto ITS parent -
    // the harness closes up round the gap, which is what deleting a step out of a build sheet means.
    // Removing the step and leaving the branches pointing at nothing would silently drop them from the
    // drawing.
    void deleteStep(const QString& stepId);

    // ---- Conductor groups (a run carrying a mixture) ----------------------------------------------
    void addConductorGroup();
    void removeConductorGroup();

    // ---- THE BUNDLE THE CONDUCTORS MAKE, worked out and shown as the run is described --------------
    //
    // A trunk or a branch is described by what it CARRIES - so many of this wire, so many of that -
    // and the one figure that follows from that and nothing else is how thick the result is. It
    // decides the backshell, the boot, the braid and the heatshrink chosen at both its ends, so
    // leaving it to be discovered after the drawing is placed is leaving it too late.
    //
    // Every conductor's diameter, largest first: the LIBRARY part's own figure where the wires were
    // taken from the library (see libraryWireOdMm), and the nominal for the gauge where they were
    // typed. Read the same way the compiler reads the run - the groups where there are groups, the
    // count/gauge shorthand where there are none - so the readout cannot disagree with the drawing
    // beside it.
    std::vector<double> conductorOds() const;
    // The bundle those conductors make, with its working. Ø over the JACKET on a cable, because that
    // is what a cable is drawn and measured at whatever its cores add up to.
    BundleCalc bundleForDescribedRun() const;
    // What the readout beside the run says right now, for a test.
    QString shownBundleText() const;

    // ---- WHAT A BRANCH CARRIES OVER ITSELF -------------------------------------------------------
    //
    // A branch is described as one thing: so many wires or cables, running so far, screened, sleeved,
    // with heatshrink where it needs it. These add those coverings TO the run being described rather
    // than as steps hung off it - which is also what stops the "there is no run before it yet"
    // complaint, since a covering on the run is over that run by construction.
    //
    // `libPart` empty takes the standard drawing for the kind; a length of 0 covers the whole run.
    void addCovering(PartKind kind, const QString& libPart = QString(), double lengthMm = 0.0,
                     double atMm = 0.0);
    void removeCovering(int index);
    const std::vector<MechStepCovering>& pendingCoverings() const { return pendingCoverings_; }
    const std::vector<MechConductorGroup>& pendingConductors() const { return pending_; }

    // ---- A PART OF THIS STEP, rather than the next station along -----------------------------------
    //
    // What "Add subpart" does. It replaced a connector-only button that could bank a second housing and
    // nothing else; a station on a harness has any number of parts OF it - a second connector, the
    // backshell on it, the boot behind that, the wires leaving it - and they nest to whatever depth the
    // harness actually has.
    //
    // The difference from commitStep is DEPTH, not direction: commitStep is the next thing at the level
    // you are on, this is one level in. 1 -> 1.1; Next on 1.1 -> 1.2; this on 1.1 -> 1.1.1.
    void commitSubpart();

    // APPLY WHAT IS DESCRIBED TO THE DRAWING - what the Apply button does.
    //
    // While a step is being described it is drawn as a ghost (see kPreviewGhostKey) and is not part of
    // the harness; this is what makes it real. It differs from commitStep in what happens next: this
    // KEEPS the step selected so it can go on being corrected now it can be seen properly against the
    // rest, where Next settles it and opens the following step. On a step already applied it means what
    // it always meant - write the fields back onto it.
    void applyToDrawing();

    // The objects the preview is showing, ghost flags and all. For a test: whether a step reads as
    // applied is a fact about the drawing, not about the script.
    std::vector<Entity> shownEntities() const;

    // THE SCRIPT AS THE PREVIEW SHOWS IT: everything described, PLUS the step being typed right now.
    //
    // Without that last part the preview cannot answer the only question it is ever asked - "what does
    // the thing I am choosing look like?" - because the step being described is not in the script until
    // Next is pressed. So it is compiled as if it had been, and the preview redraws on every change.
    MechBuildScript previewScript() const;

    // The library this window offers parts from. Public so a test can put a known part in it: the
    // wizard opens whatever libraries the machine remembers, which is right for a person and useless
    // for a test that needs to assert something about a specific part.
    PartsCatalog& catalogForTest() { return catalog_; }
    // What a part is CALLED in a picker - and therefore what can be typed to find it. Public so a
    // test can check every one of a part's names is in there without driving a combo box.
    static QString partLabelForTest(const LibraryPart& p);
    // Rebuild the part list after changing that library.
    void reloadPartsForTest() { refreshPartCombo(); }
    // Choose a part by its library id, as picking it from the list does.
    bool choosePart(const QString& libPartId);
    bool chooseContact(const QString& contactId);
    QString chosenPart() const;
    QString chosenContact() const;

    // ---- What the window currently shows -----------------------------------------------------------
    //
    // Several of the requirements answered here are about what is ON THE SCREEN - the list showing the
    // step being described, the name field being called "Designator" on a connector, the ways arriving
    // from the part's own pin table - and none of those can be checked through the script alone. So the
    // few things a test needs to read and type are named, rather than the test reaching into widgets.
    // THE GAUGE AS THE SCRIPT KEEPS IT: "20 AWG", never the bracketed caption the list shows. The
    // brackets are a reading aid; gaugeMm2() has to parse what is stored, and a size that only makes
    // sense to a human is a size no check can be run against.
    QString gaugeCanon() const;
    void setGauge(const QString& gauge);

    // THE NEXT FREE RUN DESIGNATOR - W1, W2, W3. A run's designator is a number in a series rather than
    // a decision, so the wizard offers the next one instead of asking; typing over it is still the
    // answer where a drawing letters its runs some other way.
    QString nextRunDesignator() const;
    // Is this run BETWEEN TWO PARTS, so that writing its designator on the sheet would be a third name
    // for a conductor the wire list already identifies by its ends? See MechSegment::showName.
    bool runIsBetweenTwoParts() const;

    // DOES THE DIRECTION CHOICE SET THE HARNESS'S HANDEDNESS rather than a turn?
    //
    // True on the FIRST step, which has nothing before it to turn away from: "left" or "right" there is
    // not a turn at all, it is which way the whole harness is built across the sheet. It has to be
    // stored as the script's own handedness - which reaches the drawing as
    // MechDrawOptions::baseDirectionDeg - because the compiler DISCARDS a turn on a step that lands on
    // the root node, and that is why pressing Left on the first connector changed nothing at all.
    bool directionSetsHandedness() const;

    // WHERE A DESCRIBED STEP LANDS - its parent, and whether it is a part of it.
    //
    // One function because the answer was written out three times, in the preview and in both commits,
    // and three copies of a rule is two chances for it to drift. A preview that puts a step somewhere
    // other than where committing it puts it is a picture of a harness nobody is going to get.
    void placeDescribedStep(MechStep& s) const;

    // ROLLING A STATION UP, and which ones are rolled up. A station with parts can be folded away;
    // the state is a fact about the window, so a test reads it rather than inferring it from the rows.
    // THE SCRIPT TAKES THE ORDER THE TREE IS NOW SHOWING, after something has been dragged. Public so a
    // test can drive a reorder without a mouse.
    void reorderFromList();

    // Move a step in the TREE the way a drag does, and let the script follow - which is all a drop
    // amounts to. `newParentId` empty means top level; `index` is the place among that parent's
    // children. For a test: the drop itself is the widget's business, the consequence is ours.
    bool dragStepTo(const QString& stepId, const QString& newParentId, int index);

    void rollUp(const QString& stepId, bool up);
    QStringList rolledUpSteps() const;
    // Folded if it is open, opened if it is folded - what clicking a station with parts does. By id,
    // because the tree is rebuilt on nearly every change and a row is not a lasting thing.
    void toggleFold(const QString& stepId);

    // ---- A STATION WITH PARTS IS A HEADING, AND A HEADING IS NOT EDITED ---------------------------
    //
    // Once a step has parts OF it, what that step IS has been settled - the parts were described
    // against it. Its figures then say two things at once: a designator and a size that its own parts
    // were fitted to, and changing them under the parts is how a backshell ends up sized to a
    // connector that has since become a different one. So the row stops being a form and becomes what
    // the number already says it is: the heading 1 over the parts 1.1, 1.2 - and clicking it folds
    // them away rather than opening it.
    //
    // Standing on it still does everything standing on a step means: Next gives it a sibling station,
    // Add subpart gives it another part, and Delete still removes it (deleting is not editing - the
    // parts are re-parented onto its own parent, see deleteStep). Only the figures are shut.
    //
    // To change what the station is, take its parts off it - or correct the parts, which is where the
    // figures that matter to the build actually live.
    bool stepHasSubparts(const QString& stepId) const;
    // Whether the window is standing on such a station, and so showing its figures read-only.
    bool editedStepIsLocked() const;

    // What the gauge list offers, as it is written in it.
    QStringList shownGauges() const;
    QStringList shownSteps() const;
    QString shownNameLabel() const;
    bool asksForWireDesignators() const;
    int shownWays() const;
    void typeName(const QString& text);
    void typeWays(int ways);
    void typeWireDesignators(const QString& text);
    // How long the run being described is. A run of no length is not previewed at all - see
    // previewScript - so a test about where a run is DRAWN has to give it one.
    void setRunLengthForTest(double mm);
    // How many conductors the run being described carries. A run of none draws its bundle and no wires
    // at all, so a test about where the WIRES are drawn has to say how many there are - the spin box
    // reads zero in a window that was never shown.
    void setConductorsForTest(int n);

    // The findings for the script as it stands, in step order.
    std::vector<MechStepIssue> issues() const;

    // Generate and hand to the editor to be placed by a click.
    void place();

    // The step the next Next will attach to: the last step added, or empty at the start.
    QString anchorStepId() const { return anchor_; }

private:
    void buildUi();
    QWidget* buildFieldsPanel();
    void refreshStepList();
    // Whether Add subpart has anything to be a part of - see touched_.
    void refreshSubpartButton();
    // Blank the form for a step that has not been described yet.
    void clearDescribedFields();
    // The side the first station is on, from which way the harness is being drawn.
    QString initialEndSideName() const;
    // The figures shut, and said to be shut, when the step in front of you is a station with parts -
    // see stepHasSubparts. Called from both refreshers, so however the window got into that state the
    // fields agree with it.
    void refreshStationLock();
    // The bundle readout brought up to date. Called from the category refresher AND from
    // fieldChanged, because a figure that only followed the category would sit stale while somebody
    // typed the conductor count that decides it.
    void refreshBundleReadout();
    void refreshFieldsForCategory();
    // THE PART LIST AND THE CONTACT LIST ARE REBUILT SEPARATELY, and that separation is the fix for a
    // part that would not stay chosen.
    //
    // Choosing a part used to rebuild BOTH, and rebuilding a combo clears it - so the selection
    // dropped back to Default the instant it was made. The contacts genuinely do depend on which
    // connector was picked (a connector's library entry says which contacts go in it); the part list
    // does not depend on itself.
    void refreshPartCombo();
    void refreshContactCombo();
    void refreshPreview();
    void refreshIssues();
    // The step as the fields currently describe it, ready to be added.
    MechStep stepFromFields() const;
    // Which way the step before this one was drawn, so a new step can start out the same way.
    bool inheritedGoesRight() const;
    // ANY FIELD CHANGED. Writes back to the step being edited when one is being edited, and either way
    // redraws the preview and re-runs the checks - because a preview that only updates on Next is a
    // picture of the step before the one being worked on.
    void fieldChanged();
    // Does the harness START OPEN here - bare wires, ferrules, ring terminals - with no connector to
    // take wire names from? That is the case where a designator per wire is the only identification a
    // conductor has, so it is the case where the wizard asks for them.
    bool startsOpen() const;
    // The positions a chosen library part states, taken from its pin table first. 0 when it says none.
    int declaredWaysOfChosenPart() const;
    // THE GAUGE THE CHOSEN WIRE STATES, empty where it states none or where the step is not a run. A
    // conductor whose part number says 24 AWG and whose gauge row says 20 is a run the drawing, the
    // bill and the crimp check each read differently.
    QString declaredGaugeOfChosenPart() const;


    Editor* editor_ = nullptr;
    PartsCatalog catalog_;
    MechBuildScript script_;

    // Where the next step attaches. Not simply "the last step in the list": after a node, the user
    // may go back and start another branch from it, and that is a different anchor from the end of
    // the branch they just finished.
    QString anchor_;
    // AND WHETHER WHAT IS BEING DESCRIBED IS A PART OF IT.
    //
    // Add subpart means "settle this, and now a part of it". With a parent already there, the step
    // being described becomes that part and nothing more is needed. But on the VERY FIRST step there
    // is nothing to be a part of yet - so the step itself is settled first, and its part is what comes
    // next. This is how the window remembers that: the anchor is the new station, and the thing being
    // described is a part OF it rather than the station after it.
    //
    // Cleared as soon as the step is settled or somebody selects a different one, because after that
    // the anchor says everything on its own again.
    bool describingChild_ = false;
    // WHETHER ANYTHING HAS BEEN SAID ABOUT THE STEP BEING DESCRIBED.
    //
    // Add subpart on the very first step settles that step and opens its part - which is right when a
    // part has been described, and manufactures a nameless, partless connector out of an untouched form
    // when it has not. So the button waits until the form says something.
    //
    // Asked as "has a field been touched" rather than "is the name blank", because a blank name is a
    // perfectly ordinary thing for a run to have: a 200 mm cable is fully described and named nothing.
    bool touched_ = false;
    Category category_ = Category::Connector;
    bool goRight_ = true;
    // The step being edited, or empty when a new one is being described.
    QString editing_;
    // Guards the field-changed handlers while the fields are being FILLED from a step, so loading a
    // step for editing does not immediately write it back over itself.
    bool loading_ = false;

    // The conductor groups being assembled for the run about to be added.
    std::vector<MechConductorGroup> pending_;
    // WHAT THE RUN BEING DESCRIBED CARRIES OVER ITSELF: the screen, the sleeve, the heatshrink. Held
    // beside the conductor groups because it is the same kind of thing - part of the one description
    // of one branch - and cleared with them when the form is cleared.
    std::vector<MechStepCovering> pendingCoverings_;

    // HOW MUCH BARE WIRE SHOWS AT EACH END of the run being described, and what covers the rest of it.
    QLabel* openStartLabel_ = nullptr;
    QDoubleSpinBox* openStartSpin_ = nullptr;
    QLabel* openEndLabel_ = nullptr;
    QDoubleSpinBox* openEndSpin_ = nullptr;
    QLabel* coverLabel_ = nullptr;
    QWidget* coverBox_ = nullptr;
    QComboBox* coverKindCombo_ = nullptr;
    QComboBox* coverPartCombo_ = nullptr;
    QDoubleSpinBox* coverLenSpin_ = nullptr;
    QListWidget* coverList_ = nullptr;
    QPushButton* addCoverBtn_ = nullptr;
    QPushButton* dropCoverBtn_ = nullptr;
    // IS THE BUNDLE INSIDE ANYTHING. Unticked, the run is conductors end to end and the coverings are
    // not offered - they would have nothing to be over. See MechStep::bundled.
    QCheckBox* bundledCheck_ = nullptr;

    // The library parts of the kind the covering row is set to, put into its part list.
    void refreshCoverPartCombo();
    // Read the row and put it on the list of what this run carries over itself.
    void addCoveringFromFields();

    // WHETHER TO BALLOON, remembered per category. The answer is a habit rather than a decision - a
    // shop balloons its connectors and does not balloon its ties - so asking again every time is
    // asking the same question repeatedly.
    QHash<int, bool> balloonByCategory_;

    QTreeWidget* stepList_ = nullptr;
    QListWidget* catList_ = nullptr;
    // EVERY FIGURE OF THE STEP, IN ONE BOX, so they can be shut together - see refreshStationLock. The
    // heading, the hint and the Checks box sit outside it and stay readable: a greyed-out explanation
    // of why everything is greyed out is no explanation at all.
    QWidget* fieldsBox_ = nullptr;
    QLabel* heading_ = nullptr;
    QLabel* hint_ = nullptr;
    QComboBox* partCombo_ = nullptr;
    // WHAT THE NAME FIELD IS CALLED depends on what is being named. On a connector or a terminal the
    // trade word is DESIGNATOR - P1, J3, E7 - and it is not a nickname: it is the reference every wire
    // list, every schematic and every crimp record on that harness refers to the part by. "Called"
    // reads like a caption, and on the one field where the answer is a controlled reference that is the
    // wrong word.
    QLabel* nameLabel_ = nullptr;
    QLineEdit* nameEdit_ = nullptr;
    QToolButton* leftBtn_ = nullptr;
    QToolButton* rightBtn_ = nullptr;

    // The kind-specific figures. Shown and hidden by category rather than rebuilt, so the value a
    // user typed is still there if they change category and change back.
    QLabel* lengthLabel_ = nullptr;
    QDoubleSpinBox* lengthSpin_ = nullptr;
    QLabel* countLabel_ = nullptr;
    QSpinBox* countSpin_ = nullptr;
    QLabel* gaugeLabel_ = nullptr;
    QComboBox* gaugeCombo_ = nullptr;
    QLabel* odLabel_ = nullptr;
    QDoubleSpinBox* odSpin_ = nullptr;
    QLabel* waysLabel_ = nullptr;
    QSpinBox* waysSpin_ = nullptr;
    QLabel* contactLabel_ = nullptr;
    QComboBox* contactCombo_ = nullptr;

    // WRITE THE RUN'S DESIGNATOR ON THE DRAWING. Greyed on a run between two parts, where the answer
    // cannot be yes - see runIsBetweenTwoParts.
    QCheckBox* showNameCheck_ = nullptr;

    QCheckBox* dimCheck_ = nullptr;
    QLineEdit* dimText_ = nullptr;
    QCheckBox* balloonCheck_ = nullptr;
    QCheckBox* breakCheck_ = nullptr;
    QCheckBox* continueCheck_ = nullptr;
    QLabel* groupsLabel_ = nullptr;
    QListWidget* groupList_ = nullptr;
    // The bundle the run being described comes to, beside the conductors that make it.
    QLabel* bundleLabel_ = nullptr;
    QLabel* bundleInfo_ = nullptr;
    QPushButton* addGroupBtn_ = nullptr;
    QPushButton* dropGroupBtn_ = nullptr;

    // A DESIGNATOR PER WIRE, for a harness that starts open. See startsOpen().
    QLabel* desigLabel_ = nullptr;
    QLineEdit* desigEdit_ = nullptr;

    QPushButton* deleteBtn_ = nullptr;
    QPushButton* applyBtn_ = nullptr;

    QLabel* issueLabel_ = nullptr;
    DrawingPreview* preview_ = nullptr;
    QLabel* summary_ = nullptr;
    QPushButton* subpartBtn_ = nullptr;
    QPushButton* nextBtn_ = nullptr;
    QPushButton* backBtn_ = nullptr;
};

} // namespace Loopwire
