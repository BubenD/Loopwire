#pragma once

#include "harness/Bom.h"
#include "harness/MechanicalHarness.h"
#include "harness/PartsLibrary.h"

#include <QDialog>
#include <QString>

#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;

namespace Loopwire {

class DrawingPreview;
class Editor;

// Building a harness the way it is built: one step after another, from the end in your hand to the
// ends the branches finish at.
//
// The mechanical builder proper is an editor - a tree of runs and nodes, every figure of every one of
// them reachable at once - which is the right window for changing a harness that already exists and
// the wrong one for describing a harness that does not. Nobody builds a harness by filling in a tree.
// They start at one end, put a connector on it, decide how far back the wires run bare, put a
// backshell on, run the bundle down to where it splits, ask how many ways it splits, and take each
// branch to its own end. That is the order the steps are numbered in on the build sheet and it is the
// order this window asks in, one question at a time, with the drawing being built in front of you.
//
// The steps, and what each one is for:
//   1  the end you start from    - open leads, a connector (with or without a cap), or a terminal
//   2  backshell / open wires    - what dresses that end, and how far the conductors run bare
//   3  transition heatshrink     - over the step from the cable to the wires coming out of it
//   4  the main run              - a cable or a bundle of loose wires, and how long
//   5  over the main run         - braid, heatshrink, either, both
//   6  the split                 - how many branches, and what the split itself is made of
//   7  each branch               - which conductors go that way, and how long it is
//   8  over each branch          - braid, heatshrink
//   9  transition heatshrink     - at the far end of the branch
//   10 backshell / open wires    - dressing the branch's end
//   11 the end it finishes at    - open leads, a connector, or a terminal
// Steps 7 to 11 repeat for every branch; the list on the left shows them under each branch's own
// heading, so where you are in the build is where you are in the list.
class HarnessStepsDialog : public QDialog {
    Q_OBJECT
public:
    explicit HarnessStepsDialog(Editor* editor, QWidget* parent = nullptr);

    // The harness as described so far.
    const MechHarnessDef& definition() const { return def_; }
    void setDefinition(const MechHarnessDef& def);

    // Driving it from a test or from a keyboard: which step is showing, and moving between them.
    int stepCount() const { return static_cast<int>(steps_.size()); }
    int currentStep() const { return current_; }
    void goToStep(int index);
    void next();
    void back();

    // What the steps are, as the build sheet numbers them.
    enum class Step {
        StartEnd,            // #1
        StartDress,          // #2  backshell / wires without jacket + length
        StartTransition,     // #3  transition heatshrink (for a cable)
        MainRun,             // #4  main wires bundle / cable + length
        MainOver,            // #5  braided shield over? heatshrink over?
        Split,               // #6  branches split? how many? heatshrink split / y-split?
        BranchRun,           // #7  branch #n: wires bundle / cable + length
        BranchOver,          // #8  braided shield over? heatshrink over?
        BranchTransition,    // #9  transition heatshrink
        BranchDress,         // #10 backshell / wires without jacket + length
        BranchEnd,           // #11 the end it finishes at
    };
    // Every step in the list, in order: which kind it is and which branch it belongs to (-1 for the
    // steps before the split).
    struct StepRef {
        Step step;
        int branch = -1;
    };
    StepRef stepAt(int index) const;

    // Generate and hand to the editor to be placed by a click. With "add the parts to the bill of
    // materials" ticked, the parts the harness is made of go onto the drawing's bill at the same time.
    void place();

    // ---- the bill of materials ------------------------------------------------------------------
    // Whether placing the harness also puts its parts on the drawing's bill of materials. Remembered
    // between sittings, and off to begin with: writing to the bill is a second thing done to the
    // drawing, and it is asked for rather than assumed.
    bool addPartsToBom() const;
    void setAddPartsToBom(bool on);
    // The parts the harness is made of, as the lines they would go onto the bill as. What the tick box
    // would add - so it can be looked at, and tested, without a drawing to add it to.
    std::vector<BomRow> partsForBom() const;
    // The parts put onto the drawing's bill now, duplicates put to the person first (see
    // approveAlreadyListed). Returns how many lines were added; 0 when there was nothing to add or the
    // question was answered with Cancel.
    int addPartsToBomNow();

signals:
    // The drawing's bill of materials has been written to. The panel and the bill's own window show that
    // same bill, and a line added here has to appear in both - two views of one list that disagree is
    // worse than one view.
    void bomChanged();

public:
    // Driving it from a test: the harness the preview/summary actually show right now - trimmed to
    // what has been said so far (see previewDefinition()) - as opposed to definition(), which is the
    // whole thing including every step's eagerly-created default.
    MechHarnessDef previewDefinitionForTest() { return previewDefinition(); }

private:
    void buildUi();
    QWidget* buildStartEndPage();
    QWidget* buildDressPage();
    QWidget* buildTransitionPage();
    QWidget* buildRunPage();
    QWidget* buildOverPage();
    QWidget* buildSplitPage();
    QWidget* buildEndPage();

    // The list of steps, rebuilt when the number of branches changes.
    void rebuildSteps();
    void refreshList();
    void showStep();          // the page for the current step, filled from the definition
    void refreshPreview();
    void refreshSummary();
    // The harness as far as it has actually been SAID: every node/segment steps_[0..current_] touches,
    // plus - for a run reached but not yet finished at its far end - the node that far end needs to
    // exist at all. Nothing beyond that, however eagerly setBranchCount() built the rest of the
    // definition for the pages ahead to edit into: a branch not yet reached is not drawn, and neither
    // is "2 branch(es)" said about a harness that has only said what its first end is.
    MechHarnessDef previewDefinition();
    void apply();             // the controls of the current page written back into the definition

    // The parts the bill already lists, put to the person before anything is ordered twice: one line
    // per such part, saying how much is on the bill and how much this harness would add, each with its
    // own tick for "add it anyway". `takeIt` comes back one entry per row of `adding` - true for a row
    // to add. False from the whole function means Cancel: the bill is left exactly as it was.
    bool approveAlreadyListed(const std::vector<BomRow>& adding, const std::vector<BomRow>& have,
                              const std::vector<int>& already, std::vector<bool>& takeIt);

    // ---- the definition, by step ----------------------------------------------------------------
    // The node a step is about: the root for steps 1-3, the breakout for 6, a branch's tip for 9-11.
    MechNode* nodeForStep(const StepRef& ref);
    // And the run: the trunk for steps 4-5, a branch's own run for 7-8.
    MechSegment* runForStep(const StepRef& ref);
    // A fitting of this kind on a node or a run, made if it is not there and removed when it is
    // turned off. What the tick boxes on the pages are wired to.
    MechFitting* fittingOn(std::vector<MechFitting>& list, PartKind kind, bool make);
    void setFitting(std::vector<MechFitting>& list, PartKind kind, bool on);
    // The conductors: the list is described once, in step 4, as so many of a gauge.
    void setConductors(int count, const QString& gauge);
    // The branches: made and unmade as the number in step 6 changes, each with its own run and tip.
    void setBranchCount(int count);
    int branchCount() const;
    MechSegment* branchRun(int branch);
    // The short leg that turns a branch off the trunk, before its straight run.
    MechSegment* branchSpread(int branch);
    // Which way branch `index` of `count` leaves the split.
    static double branchAngleFor(int index, int count);
    MechNode* branchTip(int branch);

    Editor* editor_ = nullptr;
    PartsCatalog catalog_;
    MechHarnessDef def_;
    std::vector<StepRef> steps_;
    int current_ = 0;
    bool syncing_ = false;
    // Nothing is drawn until something has been SAID. The window opens on step 1 with an empty canvas
    // beside it - the harness does not exist yet, and a drawing of a harness nobody has described is a
    // drawing of the defaults, which is worse than nothing because it looks like an answer. The first
    // field changed, or the first step moved on from, starts it.
    bool started_ = false;

    QListWidget* list_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QLabel* heading_ = nullptr;
    QLabel* hint_ = nullptr;
    QLabel* summary_ = nullptr;
    DrawingPreview* preview_ = nullptr;
    QPushButton* backBtn_ = nullptr;
    QPushButton* nextBtn_ = nullptr;
    QCheckBox* bomBox_ = nullptr;
    // Whether a part the bill already lists is still put to the person. Turned off by the "do not ask
    // again" tick in that question, for the rest of the sitting: somebody placing the same harness at
    // four places on a sheet has answered it once and means it.
    bool askAlreadyListed_ = true;

    // #1 / #11 - an end
    QComboBox* endKind_ = nullptr;
    QLineEdit* endName_ = nullptr;
    QComboBox* endPart_ = nullptr;
    QSpinBox* endPins_ = nullptr;
    QDoubleSpinBox* endBodyW_ = nullptr;
    QDoubleSpinBox* endBodyH_ = nullptr;
    QCheckBox* endCap_ = nullptr;
    QDoubleSpinBox* endLeadLen_ = nullptr;

    // #2 / #10 - what dresses an end
    QCheckBox* dressBackshell_ = nullptr;
    QComboBox* dressBackshellPart_ = nullptr;
    QDoubleSpinBox* dressBackshellLen_ = nullptr;
    QDoubleSpinBox* dressBackshellOd_ = nullptr;
    QCheckBox* dressBoot_ = nullptr;
    QComboBox* dressBootPart_ = nullptr;
    QDoubleSpinBox* dressBootLen_ = nullptr;
    QDoubleSpinBox* dressBootOd_ = nullptr;
    QDoubleSpinBox* dressOpen_ = nullptr;

    // #3 / #9 - the transition heatshrink
    QCheckBox* transOn_ = nullptr;
    QComboBox* transPart_ = nullptr;
    QDoubleSpinBox* transLen_ = nullptr;
    QDoubleSpinBox* transOd_ = nullptr;
    QDoubleSpinBox* transStrip_ = nullptr;

    // #4 / #7 - a run
    QComboBox* runMake_ = nullptr;      // loose wires / a cable
    QSpinBox* runCount_ = nullptr;
    QComboBox* runGauge_ = nullptr;
    QComboBox* runCablePart_ = nullptr;
    QDoubleSpinBox* runJacket_ = nullptr;
    QDoubleSpinBox* runLength_ = nullptr;
    QDoubleSpinBox* runAngle_ = nullptr;
    QLabel* runBundle_ = nullptr;

    // #5 / #8 - over a run
    QCheckBox* overBraid_ = nullptr;
    QComboBox* overBraidPart_ = nullptr;
    QDoubleSpinBox* overBraidFrom_ = nullptr;
    QDoubleSpinBox* overBraidLen_ = nullptr;
    QCheckBox* overShrink_ = nullptr;
    QComboBox* overShrinkPart_ = nullptr;
    QDoubleSpinBox* overShrinkFrom_ = nullptr;
    QDoubleSpinBox* overShrinkLen_ = nullptr;

    // #6 - the split
    QSpinBox* splitCount_ = nullptr;
    QComboBox* splitKind_ = nullptr;
    QComboBox* splitPart_ = nullptr;
    QDoubleSpinBox* splitLen_ = nullptr;
    QDoubleSpinBox* splitOd_ = nullptr;
};

} // namespace Loopwire
