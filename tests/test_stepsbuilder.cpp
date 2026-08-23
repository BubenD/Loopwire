// The step builder: the window that asks for a harness one step at a time, in the order the build sheet
// numbers them. Driven here the way a person drives it - go to a step, change the field that step is
// about - and checked against the harness it is describing, because the window's whole job is to turn
// those answers into that harness.
#include "app/DrawingPreview.h"
#include "app/HarnessStepsDialog.h"
#include "harness/MechanicalHarness.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QSpinBox>

#include <cmath>
#include <cstdio>

using namespace Edraw_Desktop;

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("  [FAIL] %s\n", what);
        ++failures;
    }
}

bool near(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }

using Step = HarnessStepsDialog::Step;

// To the first step of a kind - or to that step of a given branch.
bool goTo(HarnessStepsDialog& d, Step want, int branch = -1) {
    for (int i = 0; i < d.stepCount(); ++i) {
        const auto ref = d.stepAt(i);
        if (ref.step == want && (branch < 0 || ref.branch == branch)) {
            d.goToStep(i);
            return d.currentStep() == i;
        }
    }
    return false;
}

// The field a step asks about, by the name the window gives it.
template <class W>
W* field(HarnessStepsDialog& d, const char* name) {
    W* w = d.findChild<W*>(QString::fromLatin1(name));
    if (!w) std::printf("  [FAIL] no field called %s on the step showing\n", name);
    return w;
}

// A branch is drawn in two legs: a short one that turns it off the trunk, then a straight one. Which is
// what a branch looks like - and what the sample drawing draws - so a test that wants "the branch" has
// to walk both.
struct Branch {
    const MechSegment* spread = nullptr;   // the leg off the trunk
    const MechSegment* run = nullptr;      // the straight leg after it
    const MechNode* elbow = nullptr;       // where they meet
    const MechNode* tip = nullptr;         // and what it finishes at
    double lengthMm() const {
        return (spread ? spread->lengthMm : 0.0) + (run ? run->lengthMm : 0.0);
    }
};

Branch branchNumber(const MechHarnessDef& def, int index) {
    Branch b;
    int n = 0;
    for (const MechSegment& s : def.segments) {
        if (s.fromNode != QLatin1String("split")) continue;
        if (n++ != index) continue;
        b.spread = &s;
        b.elbow = def.node(s.toNode);
        for (const MechSegment& r : def.segments)
            if (r.fromNode == s.toNode) b.run = &r;
        if (b.run) b.tip = def.node(b.run->toNode);
        break;
    }
    return b;
}

int fittingsOfKind(const std::vector<MechFitting>& list, PartKind kind) {
    int n = 0;
    for (const MechFitting& f : list)
        if (f.kind == kind) ++n;
    return n;
}

} // namespace

int run_stepsbuilder_tests() {
    std::printf("step builder tests:\n");
    HarnessStepsDialog dlg(nullptr);
    const MechHarnessDef& def = dlg.definition();

    // The steps are the build sheet's: six of them before the split, then five for every branch, in
    // that order, with each branch's own five under its own heading.
    check(dlg.stepCount() == 6 + 5 * 2, "steps: six to the split, then five for each branch");
    check(dlg.stepAt(0).step == Step::StartEnd, "steps: it starts at the end in your hand");
    check(dlg.stepAt(3).step == Step::MainRun, "steps: the main run is the fourth");
    check(dlg.stepAt(5).step == Step::Split, "steps: the split is the sixth");
    check(dlg.stepAt(6).step == Step::BranchRun && dlg.stepAt(6).branch == 0,
          "steps: then the first branch's own run");
    check(dlg.stepAt(dlg.stepCount() - 1).step == Step::BranchEnd,
          "steps: and it finishes at the last branch's end");

    // Next and Back walk it from one end to the other.
    dlg.goToStep(0);
    for (int i = 1; i < dlg.stepCount(); ++i) dlg.next();
    check(dlg.currentStep() == dlg.stepCount() - 1, "steps: Next walks to the last step");
    for (int i = dlg.stepCount() - 2; i >= 0; --i) dlg.back();
    check(dlg.currentStep() == 0, "steps: Back walks it the other way");

    // #1 the end you start from
    check(goTo(dlg, Step::StartEnd), "step 1 opens");
    field<QComboBox>(dlg, "endKind")->setCurrentIndex(1);
    field<QLineEdit>(dlg, "endName")->setText(QStringLiteral("X1"));
    emit field<QLineEdit>(dlg, "endName")->editingFinished();
    field<QSpinBox>(dlg, "endPins")->setValue(9);
    check(def.nodes.front().name == QLatin1String("X1") && def.nodes.front().pinCount == 9,
          "1: the end takes the name and the way count it is given");

    // The PREVIEW must not show the whole harness on step 1's account: definition() already holds a
    // split and two branches (setBranchCount() built them eagerly, for the pages ahead to edit into),
    // but nothing has been said about them yet - so the preview/summary must show only the one end
    // that has actually been answered.
    {
        check(def.nodes.size() > 1, "preview: definition() itself already has the split/branches built");
        const MechHarnessDef shown = dlg.previewDefinitionForTest();
        check(shown.nodes.size() == 1 && shown.nodes.front().name == QLatin1String("X1"),
              "preview: on step 1, only the end just answered is drawn");
        check(shown.segments.empty(), "preview: and nothing runs from it yet");
        int shownBranches = 0;
        for (const MechSegment& s : shown.segments)
            if (s.fromNode == QLatin1String("split")) ++shownBranches;
        check(shownBranches == 0,
              "preview: it does not say \"2 branch(es)\" before the split has even been reached");
    }

    // #2 what dresses it, and how far the conductors run bare
    check(goTo(dlg, Step::StartDress), "step 2 opens");
    field<QCheckBox>(dlg, "backshellOn")->setChecked(true);
    field<QDoubleSpinBox>(dlg, "backshellLength")->setValue(26.0);
    field<QCheckBox>(dlg, "bootOn")->setChecked(true);
    field<QDoubleSpinBox>(dlg, "openLength")->setValue(24.0);
    check(fittingsOfKind(def.nodes.front().fittings, PartKind::Backshell) == 1 &&
              fittingsOfKind(def.nodes.front().fittings, PartKind::HeatshrinkBoot) == 1,
          "2: a backshell and a boot are fitted to that end");
    check(near(def.nodes.front().openLengthMm, 24.0),
          "2: and the conductors run bare for as far as was said");
    // Turning one off takes it off the harness rather than leaving it there unused.
    field<QCheckBox>(dlg, "bootOn")->setChecked(false);
    check(fittingsOfKind(def.nodes.front().fittings, PartKind::HeatshrinkBoot) == 0,
          "2: unticking a body takes it off again");

    // #3 the transition
    check(goTo(dlg, Step::StartTransition), "step 3 opens");
    field<QCheckBox>(dlg, "transitionOn")->setChecked(true);
    field<QDoubleSpinBox>(dlg, "transitionLength")->setValue(30.0);
    {
        bool found = false;
        for (const MechFitting& f : def.nodes.front().fittings)
            if (f.kind == PartKind::Heatshrink && near(f.lengthMm, 30.0)) found = true;
        check(found, "3: heatshrink over the transition, of the length given");
    }

    // #4 the main run: the conductors are described once, here
    check(goTo(dlg, Step::MainRun), "step 4 opens");
    field<QSpinBox>(dlg, "runCount")->setValue(8);
    field<QComboBox>(dlg, "runGauge")->setCurrentText(QStringLiteral("0.75"));
    field<QDoubleSpinBox>(dlg, "runLength")->setValue(450.0);
    check(def.wires.size() == 8, "4: eight conductors in the harness");
    check(def.wires.front().gauge == QLatin1String("0.75"), "4: of the cross-section given");
    check(def.segment(QStringLiteral("main")) &&
              near(def.segment(QStringLiteral("main"))->lengthMm, 450.0),
          "4: and the run is as long as was said");
    field<QComboBox>(dlg, "runMake")->setCurrentIndex(1);
    field<QDoubleSpinBox>(dlg, "runJacket")->setValue(9.5);
    check(def.segment(QStringLiteral("main"))->isCable() &&
              near(def.segment(QStringLiteral("main"))->jacketOdMm, 9.5),
          "4: as a cable it is drawn over its jacket");

    // #5 over it
    check(goTo(dlg, Step::MainOver), "step 5 opens");
    field<QCheckBox>(dlg, "braidOn")->setChecked(true);
    field<QCheckBox>(dlg, "shrinkOn")->setChecked(true);
    field<QDoubleSpinBox>(dlg, "braidFrom")->setValue(40.0);
    field<QDoubleSpinBox>(dlg, "braidLength")->setValue(300.0);
    check(fittingsOfKind(def.segment(QStringLiteral("main"))->coverings, PartKind::ShieldBraid) == 1 &&
              fittingsOfKind(def.segment(QStringLiteral("main"))->coverings, PartKind::Heatshrink) == 1,
          "5: braid and heatshrink go over the run");

    // #6 the split: how many ways, and what the split is made of
    check(goTo(dlg, Step::Split), "step 6 opens");
    const int was = dlg.stepCount();
    field<QSpinBox>(dlg, "branchCount")->setValue(3);
    check(dlg.stepCount() == was + 5, "6: a third branch brings its own five steps with it");
    field<QComboBox>(dlg, "splitKind")->setCurrentIndex(2);
    field<QDoubleSpinBox>(dlg, "splitLength")->setValue(22.0);
    {
        bool y = false;
        for (const MechNode& n : def.nodes)
            if (n.kind == MechNodeKind::Breakout && fittingsOfKind(n.fittings, PartKind::YSplit) == 1)
                y = true;
        check(y, "6: and the split itself is a part on the drawing");
    }

    // #7 to #11, on the branch that was just added
    check(goTo(dlg, Step::BranchRun, 2), "step 7 opens on the third branch");
    field<QDoubleSpinBox>(dlg, "runLength")->setValue(275.0);
    field<QDoubleSpinBox>(dlg, "runAngle")->setValue(-55.0);
    field<QSpinBox>(dlg, "runCount")->setValue(3);
    check(goTo(dlg, Step::BranchDress, 2), "step 10 opens on the third branch");
    field<QDoubleSpinBox>(dlg, "openLength")->setValue(18.0);
    check(goTo(dlg, Step::BranchEnd, 2), "step 11 opens on the third branch");
    field<QComboBox>(dlg, "endKind")->setCurrentIndex(0);
    field<QDoubleSpinBox>(dlg, "endLeadLen")->setValue(60.0);
    {
        const Branch third = branchNumber(def, 2);
        check(third.spread && third.run, "7: a branch is drawn in two legs - off the trunk, then along");
        check(near(third.lengthMm(), 275.0), "7: and the two together are as long as was said");
        check(third.run && third.spread && third.spread->lengthMm < third.run->lengthMm,
              "7: the leg that turns it off the trunk is the shorter of the two");
        check(third.run && third.run->wireIds.size() == 3,
              "7: carrying the conductors that go that way");
        check(third.spread && third.spread->wireIds == third.run->wireIds,
              "7: the same conductors down both legs of it");
        check(third.elbow && near(third.elbow->angleDeg, -55.0),
              "7: leaving the split the way it was aimed");
        check(third.tip && near(third.tip->angleDeg, 55.0),
              "7: and the straight leg turning back to run parallel to the trunk");
        check(third.tip && third.tip->kind == MechNodeKind::FreeEnd, "11: finishing in open leads");
        check(third.tip && near(third.tip->openLengthMm, 60.0), "11: of the length given");
    }

    // And there is a drawing to look at beside every step, from the first answer to the last.
    int drew = 0;
    for (int i = 0; i < dlg.stepCount(); ++i) {
        dlg.goToStep(i);
        if (!buildMechanicalHarness(dlg.definition(), nullptr).empty()) ++drew;
    }
    check(drew == dlg.stepCount(), "there is a drawing beside every step");

    // The canvas beside the questions opens EMPTY: the harness does not exist until something has been
    // said about it, and a drawing of the defaults looks like an answer when it is not one.
    {
        HarnessStepsDialog fresh(nullptr);
        auto* canvas = fresh.findChild<DrawingPreview*>();
        check(canvas != nullptr, "empty start: the window has a canvas");
        check(canvas && canvas->isEmpty(), "empty start: and it opens with nothing on it");
        if (goTo(fresh, Step::MainRun))
            field<QDoubleSpinBox>(fresh, "runLength")->setValue(420.0);
        check(canvas && !canvas->isEmpty(), "empty start: the first answer puts the harness on it");
    }

    // A branch is drawn the way a branch goes: a short leg turning it off the trunk, then a straight one
    // parallel to the trunk again. Three of them make a Y with a straight through the middle - not three
    // legs of a fan spreading for ever, which is what one leg per branch drew.
    {
        HarnessStepsDialog three(nullptr);
        if (goTo(three, Step::Split)) field<QSpinBox>(three, "branchCount")->setValue(3);
        const MechLayout layout = layoutMechanicalHarness(three.definition());
        int turned = 0, straight = 0;
        for (int b = 0; b < 3; ++b) {
            const MechPlacedSegment* sp = layout.segment(QStringLiteral("sp%1").arg(b + 1));
            const MechPlacedSegment* run = layout.segment(QStringLiteral("br%1").arg(b + 1));
            if (!sp || !run) {
                check(false, "branch shape: each branch is drawn in two legs");
                continue;
            }
            const bool alongTrunk = std::abs(run->dirDeg) < 1.0;
            if (std::abs(sp->dirDeg) > 1.0 && alongTrunk) ++turned;
            if (std::abs(sp->dirDeg) <= 1.0 && alongTrunk) ++straight;
        }
        check(turned == 2, "branch shape: two of three turn off the trunk and then run straight");
        check(straight == 1, "branch shape: and the middle one carries straight on");
        // A quarter of the branch turns it off the trunk and the rest of it runs: the corner is a corner,
        // not half the branch.
        const MechHarnessDef& d = three.definition();
        const MechSegment* sp1 = d.segment(QStringLiteral("sp1"));
        const MechSegment* br1 = d.segment(QStringLiteral("br1"));
        check(sp1 && br1 && sp1->lengthMm < br1->lengthMm,
              "branch shape: the leg off the trunk is the shorter of the two");
        check(sp1 && br1 && sp1->wireIds == br1->wireIds,
              "branch shape: the same conductors run down both legs");
    }

    // To scale, both ways: a width taken off the drawing is the width of the thing. The builder proper
    // can exaggerate the bodies for a formboard sheet; a harness being described is drawn as it is.
    {
        HarnessStepsDialog dlg2(nullptr);
        if (goTo(dlg2, Step::MainRun)) field<QDoubleSpinBox>(dlg2, "runLength")->setValue(300.0);
        const MechHarnessDef& d = dlg2.definition();
        check(near(d.draw.diameterExaggeration, 1.0) && near(d.draw.scale, 1.0),
              "to scale: widths and lengths at the same scale");
        const MechLayout layout = layoutMechanicalHarness(d);
        double widest = 0.0;
        for (const MechPlacedSegment& ps : layout.segments) widest = std::max(widest, ps.diameterMm);
        double bar = 0.0;
        for (const Entity& e : buildMechanicalHarness(d, nullptr))
            if (e.meta.value(QStringLiteral("mechRole")).toString() == QLatin1String("bar"))
                bar = std::max(bar, distance(e.a, e.b));
        check(widest > 0.0 && bar <= widest + 0.01,
              "to scale: a bar across the outline measures the diameter itself");
    }

    if (failures == 0) std::printf("  -> ok\n");
    else std::printf("  -> %d failed\n", failures);
    return failures;
}
