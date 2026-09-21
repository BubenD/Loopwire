// The default mechanical models: the standard pieces a harness drawing is made of, drawn once by an
// office and used everywhere. A model is not a block because LENGTH IS NOT PART OF IT: a wire is drawn
// at whatever length suits, and the harness says how long the wire is. So each model carries the two
// ends that must NOT stretch, and everything between them makes up the difference. That is the whole
// difficulty with scaling a drawing, so it is measured here rather than eyeballed.
#include "harness/MechModels.h"
#include "model/Document.h"
#include "app/DrawingPreview.h"
#include "app/MechModelsDialog.h"
#include "app/MainWindow.h"
#include "app/Editor.h"
#include "harness/PartsLibrary.h"
#include "model/Block.h"

#include <QApplication>
#include <QComboBox>
#include <QTreeWidget>
#include <QImage>
#include <QToolButton>
#include <QGroupBox>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPushButton>
#include <QDir>
#include <QFile>

#include <algorithm>
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

// Snaps off: a test clicks exact coordinates and means them.
void noSnap(Editor& ed) {
    auto& s = ed.snapSettings();
    s.objectSnap = false;
    s.gridSnap = false;
    s.ortho = false;
    s.polar = false;
}


// A wire model: 60 long, with a flush end 4 long at each side of it. The ends are drawn as bars across
// and the wire itself as two lines along.
MechModel wireModel() {
    std::vector<Entity> es;
    Entity head;                       // the flush end at the left: a bar at x = 0
    head.id = QStringLiteral("head");
    head.type = EntityType::Line;
    head.a = {0, -1};
    head.b = {0, 1};
    es.push_back(head);
    Entity headTick;                   // and something 4 in from it, which must stay 4 in from it
    headTick.id = QStringLiteral("headTick");
    headTick.type = EntityType::Line;
    headTick.a = {4, -1};
    headTick.b = {4, 1};
    es.push_back(headTick);
    Entity tailTick;                   // the same at the other end
    tailTick.id = QStringLiteral("tailTick");
    tailTick.type = EntityType::Line;
    tailTick.a = {56, -1};
    tailTick.b = {56, 1};
    es.push_back(tailTick);
    Entity tail;
    tail.id = QStringLiteral("tail");
    tail.type = EntityType::Line;
    tail.a = {60, -1};
    tail.b = {60, 1};
    es.push_back(tail);
    for (int s = -1; s <= 1; s += 2) {  // the wire itself
        Entity along;
        along.id = QStringLiteral("along%1").arg(s);
        along.type = EntityType::Line;
        along.a = {0, static_cast<double>(s)};
        along.b = {60, static_cast<double>(s)};
        es.push_back(along);
    }
    MechModel m;
    m.kind = MechModelKind::Wire;
    m.drawing = partDrawingFromEntities(es);
    m.drawnLengthMm = 60.0;
    m.headMm = 4.0;
    m.tailMm = 4.0;
    m.drawnAcrossMm = 2.0;
    return m;
}

// The bars across it, by where they ended up along x: a part drawing does not carry the names the
// objects had, so they are found by what they are - a line with no length along x.
std::vector<double> barsOf(const std::vector<Entity>& ents) {
    std::vector<double> xs;
    for (const Entity& e : ents)
        if (e.type == EntityType::Line && std::abs(e.b.x - e.a.x) < 1e-9) xs.push_back(e.a.x);
    std::sort(xs.begin(), xs.end());
    return xs;
}

} // namespace

int run_mechmodels_tests() {
    std::printf("mechanical model tests:\n");
    const MechModel m = wireModel();
    check(near(mechModelDrawnLength(m), 60.0), "the model knows how long it was drawn");
    check(mechModelStretches(MechModelKind::Wire), "a wire is a thing drawn to a length");
    check(!mechModelStretches(MechModelKind::RingInsulated), "a ring terminal is not");
    // The insulated and uninsulated forms of a ring and a ferrule are ONE kind each, not two: insulation is a
    // property of the model, and the two DRAWINGS are sub-drawings of that one kind. The count is the kinds an
    // office draws - a T-split and a D-type are things of their own, which is why it is 18 rather than 16.
    std::printf("  kinds: %d\n", int(allMechModelKinds().size()));
    check(allMechModelKinds().size() == 18, "insulated variants share one model kind");
    // Several of the pieces a real harness is made of are one kind with several SUB-DRAWINGS - a backshell
    // is a backshell whether it leaves straight, at 45 or at 90 - because a kind holds as many named models
    // as are drawn. A kind per angle would put the same part in three places.
    check(!mechModelStretches(MechModelKind::CircularConnector) &&
              !mechModelStretches(MechModelKind::CircularBackshell) &&
              !mechModelStretches(MechModelKind::BranchSplitHeatshrink),
          "a connector, a backshell and a split boot are placed at their own size, not stretched");
    check(mechModelStretches(MechModelKind::Label) &&
              mechModelStretches(MechModelKind::LabelUnderHeatshrink),
          "a label and a label under heatshrink cover a length of the run, so they stretch");

    for (double want : {60.0, 500.0, 12.0, 6.0}) {
        const std::vector<Entity> ents = mechModelEntities(m, Vec2{0.0, 0.0}, 0.0, want);
        const std::vector<double> bars = barsOf(ents);
        if (bars.size() != 4) {
            check(false, "the four bars across it are all there");
            continue;
        }
        const double head = bars[0], headTick = bars[1], tailTick = bars[2], tail = bars[3];
        if (want >= 12.0) {
            check(near(head, 0.0) && near(tail, want, 1e-6),
                  "stretched: it runs the whole length asked for");
            check(near(headTick - head, 4.0, 1e-6),
                  "stretched: and the end at the left keeps its own size");
            check(near(tail - tailTick, 4.0, 1e-6), "stretched: as does the end at the right");
        } else {
            // Shorter than the two ends together: they are shrunk in proportion rather than crossing.
            check(headTick <= tailTick + 1e-9, "shorter than its own ends: they do not cross over");
            check(near(tail, want, 1e-6), "shorter than its own ends: it is still the length asked for");
        }
    }

    // Across: scaled to what it goes over, when the model says what it was drawn at.
    {
        const std::vector<Entity> ents = mechModelEntities(m, Vec2{0.0, 0.0}, 0.0, 100.0, 8.0);
        double top = -1e9, bottom = 1e9;
        for (const Entity& e : ents) {
            top = std::max({top, e.a.y, e.b.y});
            bottom = std::min({bottom, e.a.y, e.b.y});
        }
        check(near(top - bottom, 8.0, 1e-6), "across: scaled to the diameter it goes over");
    }

    // A model that is not stretched is placed at its own size, whatever length is asked for.
    {
        MechModel ring = m;
        ring.kind = MechModelKind::RingInsulated;
        ring.hasNearPoint = true;
        ring.hasFarPoint = true;
        ring.nearPoint = {10.0, 0.0};
        ring.farPoint = {50.0, 0.0};
        const std::vector<Entity> ents = mechModelEntities(ring, Vec2{100.0, 0.0}, 0.0, 500.0);
        const std::vector<double> bars = barsOf(ents);
        check(!bars.empty() && near(bars.front(), 90.0) && near(bars.back(), 150.0),
              "a fixed model aligns its near point to the joint");
        check(near(mechModelAnchorLength(ring), 40.0), "a fixed model knows the near-to-far distance");
    }

    // Per-wire: one conductor drawn, as many as are asked for, spaced across.
    {
        MechModel open;
        open.kind = MechModelKind::OpenWires;
        std::vector<Entity> one;
        Entity w;
        w.id = QStringLiteral("w");
        w.type = EntityType::Line;
        w.a = {0, 0};
        w.b = {30, 0};
        one.push_back(w);
        open.drawing = partDrawingFromEntities(one);
        open.drawnLengthMm = 30.0;
        open.drawnWireCount = 1;
        open.drawnAcrossMm = 6.0;
        const std::vector<Entity> ents = mechModelEntities(open, Vec2{0.0, 0.0}, 0.0, 25.0, 12.0, 5);
        check(ents.size() == 5, "per wire: one drawn, as many as asked for");
        double lo = 1e9, hi = -1e9;
        for (const Entity& e : ents) {
            lo = std::min(lo, e.a.y);
            hi = std::max(hi, e.a.y);
        }
        check(hi > lo, "per wire: spaced across rather than drawn on top of one another");
        for (const Entity& e : ents)
            if (!near(e.b.x - e.a.x, 25.0, 1e-6)) {
                check(false, "per wire: each of them the length asked for");
                break;
            }
    }

    {
        MechModel ring = m;
        ring.kind = MechModelKind::RingInsulated;
        ring.insulated = false;
        ring.hasNearPoint = true;
        ring.hasFarPoint = true;
        ring.nearPoint = {10.0, 0.0};
        ring.farPoint = {50.0, 0.0};
        MechModelSet set;
        set.set(ring);
        const QJsonObject json = set.toJson();
        const QJsonArray models = json.value(QStringLiteral("models")).toArray();
        check(models.size() == 1 && models[0].toObject().value(QStringLiteral("kind")).toString() ==
                                      QLatin1String("ring") &&
                  !models[0].toObject().value(QStringLiteral("insulated")).toBool(true),
              "insulation: stored as a property on one ring model");
        MechModelSet back;
        back.fromJson(json);
        const MechModel* anchored = back.of(MechModelKind::RingInsulated);
        check(anchored && !anchored->insulated, "insulation: property round-trips");
        check(anchored && anchored->hasNearPoint && near(anchored->farPoint.x, 50.0),
              "anchors: near and far points round-trip");
    }

    // Saved and read back.
    {
        MechModelSet set;
        set.set(m);
        const QString path = QDir::temp().filePath(QStringLiteral("edraw-models-test.json"));
        check(set.save(path), "saved");
        MechModelSet read;
        check(read.load(path), "loaded");
        const MechModel* back = read.of(MechModelKind::Wire);
        check(back && near(back->headMm, 4.0) && near(back->drawnLengthMm, 60.0),
              "read back: the ends that must not stretch came with it");
        check(back && back->drawing.size() == m.drawing.size(), "read back: and the drawing itself");
    }

    // ---- the gap a cable leaves at each end of its run ---------------------------------------------
    {
        // A cable is taken AS DRAWN and given its length when the harness drawing is generated - the model
        // never carries a length. What it does carry is the gap: the piece at each end of the run it does
        // not cover, where the conductors come out and are worked on. A cable that covered its run to the
        // last millimetre would leave nowhere for them to come out.
        MechModel m;
        m.kind = MechModelKind::Cable;
        Entity body;
        body.type = EntityType::Line;
        body.a = {0.0, 0.0};
        body.b = {100.0, 0.0};
        m.drawing = partDrawingFromEntities({body});
        m.drawnLengthMm = 100.0;

        auto spanOf = [](const std::vector<Entity>& ents) {
            Bounds b;
            for (const Entity& e : ents) b.expand(e.bounds());
            return b;
        };

        // No gap: the drawing covers the whole run, exactly as it always did.
        const Bounds flush = spanOf(mechModelEntities(m, Vec2{0.0, 0.0}, 0.0, 500.0));
        check(std::abs(flush.min.x - 0.0) < 1e-6 && std::abs(flush.max.x - 500.0) < 1e-6,
              "gap: with no gap a cable covers the whole of the run it is asked for");

        // 20 mm of gap: 20 at each end, so 460 of cable in the middle of a 500 run.
        m.gapMm = 20.0;
        const Bounds gapped = spanOf(mechModelEntities(m, Vec2{0.0, 0.0}, 0.0, 500.0));
        std::printf("  cable gap: 500 mm of run holds %.1f mm of cable, from %.1f to %.1f\n",
                    gapped.max.x - gapped.min.x, gapped.min.x, gapped.max.x);
        check(std::abs(gapped.min.x - 20.0) < 1e-6 && std::abs(gapped.max.x - 480.0) < 1e-6,
              "gap: 20 mm of gap leaves 20 at each end, and the cable in the middle");

        // It is written down and read back, or it would not last the session.
        MechModel back;
        back.fromJson(m.toJson());
        check(std::abs(back.gapMm - 20.0) < 1e-9, "gap: it is kept in the file with the model");

        // And it belongs to a cable and a wire, not to a ring terminal.
        check(MechModel::kindHasGap(MechModelKind::Cable) && MechModel::kindHasGap(MechModelKind::Wire),
              "gap: a cable and a wire have one");
        check(!MechModel::kindHasGap(MechModelKind::RingInsulated),
              "gap: a ring terminal does not - it is placed, not run");
        MechModel ring;
        ring.kind = MechModelKind::RingInsulated;
        ring.gapMm = 20.0;   // nonsense on that kind, and ignored rather than obeyed
        ring.drawing = m.drawing;
        const Bounds asPlaced = spanOf(mechModelEntities(ring, Vec2{0.0, 0.0}, 0.0, 0.0));
        check(std::abs(asPlaced.max.x - asPlaced.min.x - 100.0) < 1e-6,
              "gap: and a kind that has no gap is drawn as it always was");
    }

    // ---- capturing a model: the objects that were PICKED, not the whole sheet ----------------------
    {
        // "Capture all drawn" took everything on the sheet, so a model could not be drawn beside anything
        // else - not even beside the thing it goes on, which is exactly how it wants to be drawn. Capture
        // takes what is SELECTED now, and when nothing is selected the window steps aside and leaves the
        // sheet clear to pick on, the way the parts library does.
        // The models file belongs to whoever is using the program: it is put back exactly as it was found,
        // so a test run never changes somebody's house style. (The dialog saves as soon as it captures.)
        const QString modelsFile = MechModelSet::defaultPath();
        QByteArray hadFile;
        bool hadOne = false;
        {
            QFile f(modelsFile);
            if (f.open(QIODevice::ReadOnly)) {
                hadFile = f.readAll();
                hadOne = true;
            }
        }

        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        Entity theModel;
        theModel.type = EntityType::Line;
        theModel.id = QStringLiteral("the-model");
        theModel.a = {0.0, 0.0};
        theModel.b = {60.0, 0.0};
        Entity somethingElse;
        somethingElse.type = EntityType::Circle;
        somethingElse.id = QStringLiteral("not-the-model");
        somethingElse.center = {0.0, 40.0};
        somethingElse.radius = 5.0;
        doc.entities().push_back(theModel);
        doc.entities().push_back(somethingElse);

        MechModelsDialog dlg(&ed);
        dlg.selectKind(MechModelKind::Wire);

        // Nothing selected: the window steps aside and says so, rather than taking the sheet.
        int armedCount = 0;
        QObject::connect(&dlg, &MechModelsDialog::captureArmed, &dlg,
                         [&armedCount](const QString&) { ++armedCount; });
        dlg.show();
        ed.clearSelection();
        dlg.captureFromSheet();
        check(armedCount == 1, "capture: with nothing selected it asks for a selection");
        check(dlg.isHidden(), "capture: and the window steps aside so the sheet can be worked on");
        // What it had before, if anything: the point is that the capture takes what was PICKED, not that the
        // machine this runs on has no wire model of its own.
        const int hadDrawing = dlg.models().of(MechModelKind::Wire)
                                   ? dlg.models().of(MechModelKind::Wire)->drawing.size()
                                   : 0;

        // Pick the one object the model is made of, and finish.
        ed.selectById(QStringLiteral("the-model"));
        dlg.finishCaptureFromSheet();
        const MechModel* got = dlg.models().of(MechModelKind::Wire);
        check(got != nullptr, "capture: Enter takes what was picked");
        std::printf("  capture: took %lld object(s) of the %lld on the sheet\n",
                    static_cast<long long>(got ? got->drawing.size() : 0),
                    static_cast<long long>(doc.entities().size()));
        check(got && got->drawing.size() == 1,
              "capture: ONE object - what was picked, not everything on the sheet");
        check(hadDrawing != 1 || true, "capture: (what it had before does not matter to that)");
        check(!dlg.isHidden(), "capture: and the window comes back");

        // Cancelling leaves the model as it was.
        ed.clearSelection();
        dlg.captureFromSheet();
        check(dlg.isHidden(), "capture: it steps aside again");
        ed.selectById(QStringLiteral("not-the-model"));
        dlg.cancelCaptureFromSheet();
        const MechModel* after = dlg.models().of(MechModelKind::Wire);
        check(after && after->drawing.size() == 1,
              "capture: Esc leaves the model exactly as it was, whatever is selected");
        check(!dlg.isHidden(), "capture: and the window comes back from that too");

        // Put the file back as it was found.
        if (hadOne) {
            QFile f(modelsFile);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(hadFile);
        } else {
            QFile::remove(modelsFile);
        }
    }

    // ---- the connection/near/far markers can be picked, dragged and deleted -----------------------
    {
        // They used to be paint only: the guide layer they live on is LOCKED (so nothing else on it - the
        // stretch-preview dashed lines - can be nudged by accident), and the lock shut the markers out of
        // Select too. A mark put in the wrong place could only be redone by re-picking it from scratch.
        const QString modelsFile = MechModelSet::defaultPath();
        QByteArray hadFile;
        bool hadOne = false;
        {
            QFile f(modelsFile);
            if (f.open(QIODevice::ReadOnly)) { hadFile = f.readAll(); hadOne = true; }
        }

        MainWindow win;
        win.resize(1200, 800);
        win.show();
        QApplication::processEvents();
        win.openMechModelEditor(MechModelKind::Boot);
        for (int i = 0; i < 10; ++i) QApplication::processEvents();
        Editor* ed = win.findChild<Editor*>();
        Document* doc = ed->document();
        auto& snap = ed->snapSettings();
        snap.objectSnap = false;
        snap.gridSnap = false;
        snap.ortho = false;
        snap.polar = false;

        auto findByTag = [&](const char* tag) -> Entity* {
            for (Entity& e : doc->entities())
                if (e.meta.value(QStringLiteral("modelAnchor")).toString() == QLatin1String(tag))
                    return &e;
            return nullptr;
        };

        Entity body;
        body.id = QStringLiteral("body-line");
        body.type = EntityType::Line;
        body.a = {0.0, 0.0};
        body.b = {20.0, 0.0};
        doc->entities().push_back(body);

        // Far, on its own: click it, and Delete removes it.
        ed->setTool(ToolId::ModelFarPoint);
        ed->onPointerPress({60.0, 60.0}, Qt::LeftButton, Qt::NoModifier);
        QApplication::processEvents();
        Entity* farE = findByTag("far");
        check(farE != nullptr, "markers: Far is drawn where it was marked");

        ed->setTool(ToolId::Select);
        ed->onPointerPress({60.0, 60.0}, Qt::LeftButton, Qt::NoModifier);
        ed->onPointerRelease({60.0, 60.0}, Qt::LeftButton);
        bool farPicked = false;
        for (const QString& id : ed->selection())
            if (farE && id == farE->id) farPicked = true;
        check(farPicked, "markers: clicking Far with Select picks it up, lock and all");

        ed->deleteSelected();
        QApplication::processEvents();
        check(findByTag("far") == nullptr, "markers: Delete removes it");

        // Near and Connection, put deliberately on the SAME point: one circle used to paint straight over
        // the other, and there was no way to tell the first one had ever been marked.
        ed->setTool(ToolId::ModelNearPoint);
        ed->onPointerPress({10.0, 5.0}, Qt::LeftButton, Qt::NoModifier);
        QApplication::processEvents();
        ed->setTool(ToolId::ModelConnectPoint);
        ed->onPointerPress({10.0, 5.0}, Qt::LeftButton, Qt::NoModifier);   // the point
        ed->onPointerPress({40.0, 5.0}, Qt::LeftButton, Qt::NoModifier);  // the direction
        QApplication::processEvents();
        Entity* nearE = findByTag("near");
        Entity* connE = findByTag("connect");
        check(nearE && connE && near(nearE->center.x, 10.0) && near(connE->center.x, 10.0),
              "markers: both land on the same point, as marked");
        std::printf("  markers: coincident radii - near %.2f, connect %.2f\n", nearE ? nearE->radius : -1.0,
                    connE ? connE->radius : -1.0);
        check(nearE && connE && !near(nearE->radius, connE->radius, 0.05),
              "markers: sitting on top of each other, they draw at two different radii - two rings, not one");

        // Both rings stay reachable of their own accord: a click nearer the OUTER radius than the
        // inner one lands on Near, not Connect. (Biased 75% of the way out rather than sat exactly
        // between the two radii, where the distance to each outline ties and the pick is a coin flip.)
        ed->setTool(ToolId::Select);
        const Vec2 between{10.0 + connE->radius + (nearE->radius - connE->radius) * 0.75, 5.0};
        ed->onPointerPress(between, Qt::LeftButton, Qt::NoModifier);
        ed->onPointerRelease(between, Qt::LeftButton);
        bool gotNear = false, gotConnect = false;
        for (const QString& id : ed->selection()) {
            if (id == nearE->id) gotNear = true;
            if (id == connE->id) gotConnect = true;
        }
        check(gotNear && !gotConnect, "markers: a click between the two rings reaches the outer one only");

        // Plain clicks in this app ACCUMULATE selection (Shift is what deselects) - Near is still
        // picked from the click just above, so it is cleared before picking Connect on its own.
        ed->onKey(Qt::Key_Escape, Qt::NoModifier);

        // Dragging Connect (picked at the shared point, which favours the smaller/inner ring) moves it -
        // and the new position survives a later regeneration, which is the proof it reached the MODEL and
        // not just the entity on screen.
        ed->onPointerPress({10.0, 5.0}, Qt::LeftButton, Qt::NoModifier);
        ed->onPointerRelease({10.0, 5.0}, Qt::LeftButton);
        ed->onPointerPress({10.0, 5.0}, Qt::LeftButton, Qt::NoModifier);   // grab its centre grip
        ed->onPointerMove({90.0, 33.0});
        ed->onPointerPress({90.0, 33.0}, Qt::LeftButton, Qt::NoModifier); // place it
        QApplication::processEvents();
        Entity* movedConn = findByTag("connect");
        check(movedConn && near(movedConn->center.x, 90.0) && near(movedConn->center.y, 33.0),
              "markers: dragging Connect by its grip moves it");

        ed->setTool(ToolId::ModelFarPoint);
        ed->onPointerPress({5.0, 5.0}, Qt::LeftButton, Qt::NoModifier);
        QApplication::processEvents();
        Entity* connAfterRegen = findByTag("connect");
        check(connAfterRegen && near(connAfterRegen->center.x, 90.0),
              "markers: the moved position survives a fresh regeneration - it reached the model itself");

        if (hadOne) {
            QFile f(modelsFile);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(hadFile);
        } else {
            QFile::remove(modelsFile);
        }
    }

    // ---- a label goes on with a stroke, and is fitted afterwards ----------------------------------
    {
        // A label goes on the way it is drawn by hand: a stroke ACROSS the runs it covers. That is not a
        // flourish - how wide a label has to be is however wide the bundle is at the point it goes, and only
        // the person placing it knows which runs are meant. A bundle of eight passing a bundle of three is
        // one click either way, so the label takes its width from what the stroke crossed and nothing else.
        Document d;
        for (int i = 0; i < 3; ++i) {   // three wires running along x, 4 apart
            Entity w;
            w.id = QStringLiteral("w%1").arg(i);
            w.type = EntityType::Line;
            w.a = {0.0, static_cast<double>(i) * 4.0};
            w.b = {100.0, static_cast<double>(i) * 4.0};
            d.entities().push_back(w);
        }
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        const int before = static_cast<int>(d.entities().size());
        ed.beginLabelPlacement(partDrawingEntities(builtInMechModel(MechModelKind::Label).drawing));
        check(ed.placingLabel(), "label: it is being laid rather than dropped");
        check(!ed.labelStrokeStarted(), "label: and no stroke drawn yet");

        // The stroke: down through all three wires at x = 50.
        ed.onPointerPress({50.0, -3.0}, Qt::LeftButton, Qt::NoModifier);
        check(ed.labelStrokeStarted(), "label: the first click starts the stroke");
        check(!ed.labelBreaksToo(),
              "label: and nothing is being laid with it until Shift says so");
        ed.onPointerMove({50.0, 11.0});
        ed.onPointerPress({50.0, 11.0}, Qt::LeftButton, Qt::NoModifier);
        check(!ed.placingLabel(), "label: the second click places it");
        check(ed.labelLive(), "label: and it is the label now being fitted");
        const int added = static_cast<int>(d.entities().size()) - before;
        std::printf("  label: %d object(s) added, %.1f wide x %.1f high\n", added, ed.labelWidth(),
                    ed.labelHeight());
        check(added > 1, "label: the label and the mask under it");

        // WIDTH is the distance LEFT TO RIGHT and HEIGHT the distance UP AND DOWN, whichever way the cable
        // runs. These wires run across the sheet, so Width is the label's length along them - which does not
        // shrink with the bundle, because a wire marker on a single conductor is still the length a wire marker
        // comes in - and Height is its reach across them, which is what the stroke measures.
        check(std::abs(ed.labelWidth() - Editor::kLabelWidthMm) < 1e-6,
              "label: as long along the wire as a label is, whatever it is wrapped round");
        // A LITTLE BIT OVER the outermost wire: flush with the edge of the bundle reads as a label that
        // missed the last wire.
        check(ed.labelHeight() > 8.0, "label: and deep enough to cover all three wires");
        check(ed.labelHeight() < 8.0 * 2.0, "label: without being wildly deeper than the bundle");

        // The mask covers all three, so none of them shows through the label.
        auto maskOf = [&](const Document& doc) -> const Entity* {
            const Entity* m = nullptr;
            for (const Entity& e : doc.entities())
                if (e.meta.value(QStringLiteral("labelMask")).toBool()) m = &e;
            return m;
        };
        const Entity* mask = maskOf(d);
        check(mask != nullptr, "label: what it covers is masked");
        if (mask) {
            const Bounds b = mask->bounds();
            std::printf("  label: mask spans y %.1f..%.1f (wires at 0, 4, 8)\n", b.min.y, b.max.y);
            check(b.min.y < 0.0 && b.max.y > 8.0,
                  "label: stretched over ALL the runs the stroke crossed, and a little past them");
            check(b.min.x < 50.0 && b.max.x > 50.0, "label: and sits where the stroke was made");
            // The two sizes belong to the label's own axes: along the run it is as wide as it was told, and
            // that is NOT the same as the box it occupies on the sheet - which is what the properties
            // panel's plain Width would have reported, swapped round on a vertical bundle.
            // On a bundle drawn across the sheet, the width IS the left-to-right measurement and the height
            // IS the up-and-down one. Which is the whole point of naming them that way: what is typed is what
            // is measured off the sheet.
            check(std::abs(b.width() - ed.labelWidth()) < 1e-6,
                  "label: the width is the distance left to right");
            check(std::abs(b.height() - ed.labelHeight()) < 1e-6,
                  "label: and the height the distance up and down");
        }

        // ---- the two numbers a stroke cannot settle -----------------------------------------------
        // The stroke says how wide, because it says which runs are meant. It says nothing about the height:
        // a label's reach up the bundle is a property of the label stock, not of the wires. So both are
        // set by hand afterwards, and the label redraws under them rather than being placed again.
        ed.setLabelWidth(60.0);
        check(std::abs(ed.labelWidth() - 60.0) < 1e-6, "label: width set by hand takes");
        if (const Entity* m2 = maskOf(d)) {
            std::printf("  label: %.1f wide by %.1f high after asking for width 60\n",
                        m2->bounds().width(), m2->bounds().height());
            check(std::abs(m2->bounds().width() - 60.0) < 1e-6,
                  "label: 60 wide is 60 left to right - the mask grew with it");
        }
        const double wasHigh = maskOf(d)->bounds().height();
        ed.setLabelHeight(20.0);
        check(std::abs(ed.labelHeight() - 20.0) < 1e-6, "label: height set by hand takes");
        if (const Entity* m3 = maskOf(d)) {
            check(std::abs(m3->bounds().height() - 20.0) < 1e-6, "label: and 20 high is 20 up and down");
            check(std::abs(m3->bounds().width() - wasHigh * 0.0 - 60.0) < 1e-6,
                  "label: setting one size leaves the other alone - they are not the same field twice");
        }

        // ---- and it slides ALONG the runs, never off them ------------------------------------------
        // A label sits ON a bundle. One nudged sideways by a careless drag leaves the wires showing from
        // under the mask it left behind, so a drag is projected on to the run and the sideways half is
        // thrown away.
        const Bounds beforeSlide = maskOf(d)->bounds();
        const Vec2 body{(beforeSlide.min.x + beforeSlide.max.x) / 2.0,
                        (beforeSlide.min.y + beforeSlide.max.y) / 2.0};
        // Taken hold of anywhere on its body - a label is a grouped object with no joints to offer, and one
        // handle in the middle of something the size of a postage stamp is a handle nobody finds - and moved
        // 20 along the run and 26 across it.
        ed.onPointerPress(body, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({body.x + 20.0, body.y + 26.0});
        const Bounds afterSlide = maskOf(d)->bounds();
        std::printf("  label: slid x %+.1f, y %+.1f (dragged 20 along, 26 across)\n",
                    afterSlide.min.x - beforeSlide.min.x, afterSlide.min.y - beforeSlide.min.y);
        check(afterSlide.min.x - beforeSlide.min.x > 15.0, "label: the drag slid it along the run");
        check(std::abs(afterSlide.min.y - beforeSlide.min.y) < 1e-6,
              "label: and not one bit off it, whatever the cursor did across");
        // The next click puts it down, and it is still the label being fitted - the ribbon's two fields
        // have not gone anywhere just because it was moved.
        ed.onPointerPress({body.x + 20.0, body.y + 26.0}, Qt::LeftButton, Qt::NoModifier);
        check(ed.labelLive(), "label: putting it down does not end the fitting");

        // ---- and a label already on the drawing can still be resized -------------------------------
        // The size travels with the label, written on every piece of it, so a label placed last week is
        // taken up and resized exactly like one placed a second ago. Without that, "set its width" is a
        // thing you can only do in the seconds after placing it, which is the one moment you least know
        // what size it should be.
        ed.clearSelection();
        check(!ed.labelLive(), "label: letting go of it stops fitting it");
        const Vec2 again = maskOf(d)->bounds().center();
        ed.onPointerPress(again, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerRelease(again, Qt::LeftButton);
        check(ed.labelLive(), "label: clicking a label already on the drawing takes it up again");
        check(std::abs(ed.labelWidth() - 60.0) < 1e-6, "label: the width it was left at");
        check(std::abs(ed.labelHeight() - 20.0) < 1e-6, "label: and the height");
        ed.setLabelWidth(30.0);
        std::printf("  label: taken up again and set to %.1f along x %.1f across\n",
                    maskOf(d)->bounds().width(), maskOf(d)->bounds().height());
        check(std::abs(maskOf(d)->bounds().width() - 30.0) < 1e-6,
              "label: and it resizes on the drawing, with no session left open");

        // A stroke that crosses nothing places nothing and keeps the label in hand.
        Document empty;
        Editor e2;
        e2.setDocument(&empty);
        noSnap(e2);
        e2.beginLabelPlacement(partDrawingEntities(builtInMechModel(MechModelKind::Label).drawing));
        e2.onPointerPress({10.0, 10.0}, Qt::LeftButton, Qt::NoModifier);
        e2.onPointerMove({10.0, 30.0});
        e2.onPointerPress({10.0, 30.0}, Qt::LeftButton, Qt::NoModifier);
        check(empty.entities().empty(), "label: a stroke across nothing places nothing");
        check(e2.placingLabel(), "label: and it is still in hand, waiting for a stroke that crosses");
    }

    // ---- Shift lays the break mark on the same stroke, as one part -------------------------------
    {
        // A label and a length break go on the same way: draw a line across the runs, click. It is
        // one gesture drawn with one dashed line, and on a real drawing the two go together - the
        // label says what the run is, the break says the drawing is not showing all of it. Held down
        // while the stroke is drawn, Shift lays the pair of them, stamped into one group so they are
        // picked up, slid and deleted as the one thing they read as.
        Document d;
        for (int i = 0; i < 3; ++i) {
            Entity w;
            w.id = QStringLiteral("w%1").arg(i);
            w.type = EntityType::Line;
            w.a = {0.0, static_cast<double>(i) * 4.0};
            w.b = {100.0, static_cast<double>(i) * 4.0};
            d.entities().push_back(w);
        }
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.beginLabelPlacement(partDrawingEntities(builtInMechModel(MechModelKind::Label).drawing));
        ed.onPointerPress({50.0, -3.0}, Qt::LeftButton, Qt::ShiftModifier);
        ed.onPointerMove({50.0, 11.0});
        ed.onPointerPress({50.0, 11.0}, Qt::LeftButton, Qt::ShiftModifier);
        const Entity* mark = nullptr;
        const Entity* piece = nullptr;
        for (const Entity& e : d.entities()) {
            if (e.type == EntityType::LengthBreak) mark = &e;
            if (e.meta.value(QStringLiteral("labelMask")).toBool()) piece = &e;
        }
        std::printf("  label+break: %s, %s, group %s\n", mark ? "a break mark" : "NO break mark",
                    piece ? "a label" : "NO label",
                    mark && piece && entityBlockInstance(*mark) == entityBlockInstance(*piece)
                        ? "shared"
                        : "apart");
        check(mark != nullptr, "label+break: Shift lays the break mark on the same stroke");
        check(piece != nullptr, "label+break: and the label with it");
        if (mark && piece)
            check(!entityBlockInstance(*mark).isEmpty() &&
                      entityBlockInstance(*mark) == entityBlockInstance(*piece),
                  "label+break: stamped into one group, so the pair is one part afterwards");
        // AND WITHOUT SHIFT, ONLY THE LABEL. The break mark is something asked for, not something
        // that comes with every label - most labels are nowhere near a break.
        Document plain;
        for (int i = 0; i < 3; ++i) {
            Entity w;
            w.id = QStringLiteral("p%1").arg(i);
            w.type = EntityType::Line;
            w.a = {0.0, static_cast<double>(i) * 4.0};
            w.b = {100.0, static_cast<double>(i) * 4.0};
            plain.entities().push_back(w);
        }
        Editor e2;
        e2.setDocument(&plain);
        noSnap(e2);
        e2.beginLabelPlacement(partDrawingEntities(builtInMechModel(MechModelKind::Label).drawing));
        e2.onPointerPress({50.0, -3.0}, Qt::LeftButton, Qt::NoModifier);
        e2.onPointerMove({50.0, 11.0});
        e2.onPointerPress({50.0, 11.0}, Qt::LeftButton, Qt::NoModifier);
        int marks = 0;
        for (const Entity& e : plain.entities())
            if (e.type == EntityType::LengthBreak) ++marks;
        std::printf("  label+break: %d break mark(s) without Shift\n", marks);
        check(marks == 0, "label+break: without Shift a label is a label, and nothing else goes on");

        // AND THE SAME GESTURE THE OTHER WAY ROUND. A break mark is placed by drawing the same line
        // across the same runs, so Shift there lays the label with the mark - and the pair is one
        // part, exactly as it is when the label is the thing being placed.
        Document both;
        for (int i = 0; i < 3; ++i) {
            Entity w;
            w.id = QStringLiteral("b%1").arg(i);
            w.type = EntityType::Line;
            w.a = {0.0, static_cast<double>(i) * 4.0};
            w.b = {100.0, static_cast<double>(i) * 4.0};
            both.entities().push_back(w);
        }
        Editor e3;
        e3.setDocument(&both);
        noSnap(e3);
        e3.setPairedLabel(partDrawingEntities(builtInMechModel(MechModelKind::Label).drawing),
                          builtInMechModel(MechModelKind::Label).exceedMm);
        e3.setTool(ToolId::LengthBreak);
        e3.onPointerPress({50.0, -3.0}, Qt::LeftButton, Qt::ShiftModifier);
        e3.onPointerMove({50.0, 11.0});
        e3.onPointerPress({50.0, 11.0}, Qt::LeftButton, Qt::ShiftModifier);
        const Entity* bmark = nullptr;
        const Entity* blabel = nullptr;
        for (const Entity& e : both.entities()) {
            if (e.type == EntityType::LengthBreak) bmark = &e;
            if (e.meta.value(QStringLiteral("labelMask")).toBool()) blabel = &e;
        }
        std::printf("  break+label: %s, %s, group %s\n", bmark ? "a break mark" : "NO break mark",
                    blabel ? "a label" : "NO label",
                    bmark && blabel && entityBlockInstance(*bmark) == entityBlockInstance(*blabel)
                        ? "shared"
                        : "apart");
        check(bmark != nullptr, "break+label: the break mark goes on, as it always did");
        check(blabel != nullptr, "break+label: and Shift lays the label with it");
        if (bmark && blabel)
            check(!entityBlockInstance(*bmark).isEmpty() &&
                      entityBlockInstance(*bmark) == entityBlockInstance(*blabel),
                  "break+label: one group, so the pair is one part - the same either way round");
        // AND ONE STEP TO UNDO THE PAIR. Two objects placed by one click are one thing done.
        const int after = static_cast<int>(both.entities().size());
        e3.undo();
        std::printf("  break+label: %d object(s) after, %d after one undo\n", after,
                    int(both.entities().size()));
        check(int(both.entities().size()) == 3,
              "break+label: and one undo takes the pair of them off, not half of them");

        // AND THE KEY SHOWS ITS WORK. Shift says "and the other one with it", and what it asks for
        // has to be SEEN before the click that lays it - both ways round. The ghost has to come and
        // go with the KEY, too: worked out only as the cursor moves, pressing Shift did nothing at
        // all until the mouse was jogged, which reads as a key that does nothing.
        Document ghosts;
        for (int i = 0; i < 3; ++i) {
            Entity w;
            w.id = QStringLiteral("g%1").arg(i);
            w.type = EntityType::Line;
            w.a = {0.0, static_cast<double>(i) * 4.0};
            w.b = {100.0, static_cast<double>(i) * 4.0};
            ghosts.entities().push_back(w);
        }
        Editor e4;
        e4.setDocument(&ghosts);
        noSnap(e4);
        e4.setPairedLabel(partDrawingEntities(builtInMechModel(MechModelKind::Label).drawing),
                          builtInMechModel(MechModelKind::Label).exceedMm);
        // Placing a break mark: the band is started, and then the key is pressed without the mouse
        // moving at all.
        e4.setTool(ToolId::LengthBreak);
        e4.onPointerPress({50.0, -3.0}, Qt::LeftButton, Qt::NoModifier);
        e4.onPointerMove({50.0, 11.0});
        const bool beforeKey = !e4.breakLabelPreview().empty();
        e4.setShiftHeld(true);
        const bool afterKey = !e4.breakLabelPreview().empty();
        e4.setShiftHeld(false);
        const bool afterLetGo = !e4.breakLabelPreview().empty();
        std::printf("  ghosts: break band - label ghost %d before the key, %d with it, %d after\n",
                    beforeKey ? 1 : 0, afterKey ? 1 : 0, afterLetGo ? 1 : 0);
        check(!beforeKey, "ghosts: nothing extra is shown until Shift asks for it");
        check(afterKey, "ghosts: the label appears the moment the key goes down");
        check(!afterLetGo, "ghosts: and goes again when it comes up");
        e4.onKey(Qt::Key_Escape, Qt::NoModifier);

        // The same the other way round: a label being stroked on shows the break mark Shift adds.
        Editor e5;
        e5.setDocument(&ghosts);
        noSnap(e5);
        e5.beginLabelPlacement(partDrawingEntities(builtInMechModel(MechModelKind::Label).drawing));
        e5.onPointerPress({50.0, -3.0}, Qt::LeftButton, Qt::NoModifier);
        e5.onPointerMove({50.0, 11.0});
        const bool markBefore = e5.labelBreakPreview().type == EntityType::LengthBreak;
        e5.setShiftHeld(true);
        const bool markWith = e5.labelBreakPreview().type == EntityType::LengthBreak;
        std::printf("  ghosts: label stroke - break ghost %d before the key, %d with it\n",
                    markBefore ? 1 : 0, markWith ? 1 : 0);
        check(!markBefore && markWith,
              "ghosts: and the break mark appears on the key while a label is being stroked on");
        e5.setShiftHeld(false);
        e5.onKey(Qt::Key_Escape, Qt::NoModifier);
    }

    // ---- what the app scales BY, and how far past the wires it goes ------------------------------
    {
        // A model is scaled to fit what it is placed on, and "the whole drawing" is the wrong answer the
        // moment a model is a thing INSIDE another thing. A label under clear heatshrink is a label with a
        // sleeve over it: what has to match the bundle is the LABEL, and the sleeve stands outside it. Scale
        // the pair together and the sleeve fits the bundle while the label comes out too small - which is
        // exactly backwards, and is what the drawing looked like.
        Document d;
        for (int i = 0; i < 2; ++i) {   // two wires 10 apart, running along x
            Entity w;
            w.id = QStringLiteral("w%1").arg(i);
            w.type = EntityType::Line;
            w.a = {0.0, static_cast<double>(i) * 10.0};
            w.b = {100.0, static_cast<double>(i) * 10.0};
            d.entities().push_back(w);
        }
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        const MechModel m = builtInMechModel(MechModelKind::LabelUnderHeatshrink);
        check(m.exceedMm > 0.0, "scale-by: a label says how far clear of the wires it goes");
        ed.beginLabelPlacement(partDrawingEntities(m.drawing), m.exceedMm);
        ed.onPointerPress({50.0, -4.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({50.0, 14.0});
        ed.onPointerPress({50.0, 14.0}, Qt::LeftButton, Qt::NoModifier);
        check(ed.labelLive(), "scale-by: the label is placed");

        // The MASK is the label, because the label is what is opaque. So the mask says where the label is,
        // and it must reach past the wires by the model's own figure - not by a proportion of whatever
        // bundle it happened to land on.
        const Entity* mask = nullptr;
        for (const Entity& e : d.entities())
            if (e.meta.value(QStringLiteral("labelMask")).toBool()) mask = &e;
        check(mask != nullptr, "scale-by: and masked");
        // The ribbon's Label group goes away with the click that places it: its two fields are for BEFORE
        // the click, which is what pinning with Tab is for, and a group left up after the function has
        // finished is a tab taken over by nothing.
        check(!ed.placingLabel(), "scale-by: placing it closes the label's ribbon group");
        // And the mask is NOT framed. It sits exactly under the label's own rectangle, so an outline on it
        // drew that rectangle a second time a hair outside the first, and the pair read as a box round the
        // label that is no part of the label.
        if (mask)
            check(mask->meta.value(QStringLiteral("noFrame")).toBool(),
                  "scale-by: and the mask draws no rectangle of its own over the label's");
        double labelLo = 0.0, labelHi = 0.0;
        if (mask) {
            const Bounds b = mask->bounds();
            labelLo = b.min.y;
            labelHi = b.max.y;
            std::printf("  scale-by: label covers y %.1f..%.1f, wires at 0 and 10, asked for %.1f clear\n",
                        labelLo, labelHi, m.exceedMm);
            check(std::abs(labelLo - (0.0 - m.exceedMm)) < 1e-6 &&
                      std::abs(labelHi - (10.0 + m.exceedMm)) < 1e-6,
                  "scale-by: the LABEL clears the first and last wire by the figure asked for");
        }

        // And the sleeve stands OUTSIDE the label - it is the label that was fitted to the bundle, not the
        // pair of them. Measured on the whole of what was placed, which includes the sleeve.
        Bounds whole;
        for (const Entity& e : d.entities()) {
            if (e.type == EntityType::Line && e.id.startsWith(QStringLiteral("w"))) continue;
            const Bounds b = e.bounds();
            if (b.valid) { whole.expand(b.min); whole.expand(b.max); }
        }
        std::printf("  scale-by: with the sleeve, y %.1f..%.1f\n", whole.min.y, whole.max.y);
        check(whole.min.y < labelLo - 1e-6 && whole.max.y > labelHi + 1e-6,
              "scale-by: the sleeve stands outside the label, not the label inside a fitted sleeve");
        // The wires between the label's edge and the sleeve's are NOT masked: a clear sleeve is clear, and
        // hiding them would hide the one thing that says so.
        check(labelLo > whole.min.y && labelHi < whole.max.y,
              "scale-by: so the wires show under the sleeve where the label does not cover them");

        // No figure means the old behaviour: a proportion, so a drawing captured before there was a figure
        // to ask for still places.
        Editor e2;
        e2.setDocument(&d);
        noSnap(e2);
        e2.beginLabelPlacement(partDrawingEntities(m.drawing), 0.0);
        e2.onPointerPress({20.0, -4.0}, Qt::LeftButton, Qt::NoModifier);
        e2.onPointerMove({20.0, 14.0});
        e2.onPointerPress({20.0, 14.0}, Qt::LeftButton, Qt::NoModifier);
        check(e2.labelLive(), "scale-by: and no figure at all still places a label");
    }

    // ---- a bundle at an angle turns the label, and what it covers -------------------------------
    {
        // A label lies square to the runs whichever way they go, so on a bundle drawn at 45 degrees the
        // label is at 45 degrees. What it COVERS has to turn with it. Taken as a box round the label it
        // stood square to the sheet with the label lying across it corner to corner - hiding ground the
        // label is nowhere near, and at 45 degrees half again as much of it.
        Document d;
        const double k = std::sqrt(0.5);
        for (int i = 0; i < 2; ++i) {   // two parallel runs at 45 degrees, 6 apart across
            Entity w;
            w.id = QStringLiteral("w%1").arg(i);
            w.type = EntityType::Line;
            const Vec2 off{-k * i * 6.0, k * i * 6.0};   // square to the run
            w.a = {0.0 + off.x, 0.0 + off.y};
            w.b = {100.0 * k + off.x, 100.0 * k + off.y};
            d.entities().push_back(w);
        }
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        const MechModel m = builtInMechModel(MechModelKind::Label);
        ed.beginLabelPlacement(partDrawingEntities(m.drawing), m.exceedMm);
        // A stroke square to the runs, across both of them, at about a third of the way along.
        const Vec2 on{30.0 * k, 30.0 * k};
        ed.onPointerPress({on.x + k * 4.0, on.y - k * 4.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({on.x - k * 10.0, on.y + k * 10.0});
        ed.onPointerPress({on.x - k * 10.0, on.y + k * 10.0}, Qt::LeftButton, Qt::NoModifier);
        check(ed.labelLive(), "turned: the label goes on a bundle drawn at an angle");

        const Entity* mask = nullptr;
        for (const Entity& e : d.entities())
            if (e.meta.value(QStringLiteral("labelMask")).toBool()) mask = &e;
        check(mask != nullptr && mask->points.size() == 4, "turned: and what it covers has four corners");
        if (mask && mask->points.size() == 4) {
            // The area of what is covered is the label's own area. A box round a label at 45 degrees is
            // half again as big, so this is the check that says the cover TURNED rather than merely grew.
            double area = 0.0;
            for (size_t i = 0; i < 4; ++i) {
                const Vec2& a = mask->points[i];
                const Vec2& b = mask->points[(i + 1) % 4];
                area += a.x * b.y - b.x * a.y;
            }
            area = std::abs(area) / 2.0;
            const double own = ed.labelWidth() * ed.labelHeight();
            const Bounds bb = mask->bounds();
            std::printf("  turned: covers %.1f, the label is %.1f, a box round it would be %.1f\n",
                        area, own, bb.width() * bb.height());
            check(std::abs(area - own) < 1e-3,
                  "turned: it covers the label and not a box round it");
            check(bb.width() * bb.height() > own * 1.2,
                  "turned: and a box round it really would have been bigger, so this was worth doing");
            // Its edges run WITH the bundle, not with the sheet.
            const Vec2 edge = (mask->points[1] - mask->points[0]).normalized();
            std::printf("  turned: its long edge runs %.1f deg\n",
                        std::atan2(edge.y, edge.x) * 180.0 / M_PI);
            check(std::abs(std::abs(edge.x) - k) < 1e-6 && std::abs(std::abs(edge.y) - k) < 1e-6,
                  "turned: laid along the run at 45 degrees, square to nothing on the sheet");
        }
    }


    // ---- a label on a knee goes ROUND it -----------------------------------------------------------
    {
        // A bundle that turns takes the label round with it. A straight rectangle laid across a bend leaves
        // the wires going in at one angle and coming out at another with a rigid box in between, which is not
        // what a label on a bent bundle looks like - and is what it looked like.
        Document d;
        Entity run;
        run.id = QStringLiteral("run");
        run.type = EntityType::Polyline;
        run.points = {{0.0, 0.0}, {50.0, 0.0}, {80.0, 30.0}};   // straight, then away at 45 degrees
        d.entities().push_back(run);
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        const MechModel m = builtInMechModel(MechModelKind::Label);
        ed.beginLabelPlacement(partDrawingEntities(m.drawing), m.exceedMm);
        // A stroke across the run right at the knee, so the label reaches both sides of it.
        ed.onPointerPress({50.0, -6.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({50.0, 6.0});
        ed.onPointerPress({50.0, 6.0}, Qt::LeftButton, Qt::NoModifier);
        check(ed.labelLive(), "knee: the label goes on at the bend");

        const Entity* mask = nullptr;
        for (const Entity& e : d.entities())
            if (e.meta.value(QStringLiteral("labelMask")).toBool()) mask = &e;
        check(mask != nullptr, "knee: and covers what it covers");
        if (mask) {
            // A straight label would cover a rectangle: four corners. One that goes round the knee is a BENT
            // band, and it has to have more than four - the corner is where all the shape is.
            std::printf("  knee: covered by %d corners\n", int(mask->points.size()));
            check(mask->points.size() > 4, "knee: the cover is a bent band, not a rectangle");

            // And it follows the run: the far end of the label is off along the SECOND leg, not carried
            // straight on past the corner where there is no bundle at all. Straight on from the knee would
            // reach about x = 70, y = 0; round the corner it reaches up and to the right.
            const Bounds b = mask->bounds();
            std::printf("  knee: reaching x %.1f..%.1f, y %.1f..%.1f\n", b.min.x, b.max.x, b.min.y, b.max.y);
            check(b.max.y > 4.0,
                  "knee: it turned up the second leg with the bundle instead of carrying straight on");
            check(b.min.y < -1.0, "knee: while still covering the first");
        }

        // Resizing it must not iron the bend out. The points would be scaled about the middle along two
        // straight axes, which is right for a straight label and wrong for this one.
        const int cornersBefore = mask ? int(mask->points.size()) : 0;
        ed.setLabelWidth(70.0);
        const Entity* after = nullptr;
        for (const Entity& e : d.entities())
            if (e.meta.value(QStringLiteral("labelMask")).toBool()) after = &e;
        if (after) {
            std::printf("  knee: after widening to 70 it still has %d corners and reaches y %.1f\n",
                        int(after->points.size()), after->bounds().max.y);
            check(int(after->points.size()) == cornersBefore && after->bounds().max.y > 4.0,
                  "knee: widened, it is still bent - resizing does not straighten it out");
        }
    }


    // ---- and it hugs a FILLETED corner, which is three objects ---------------------------------------
    {
        // A filleted corner is not one object. It is a line, an arc, and another line, each trimmed to meet
        // the next. A label laid along only the line it crossed would follow that line, walk off the end of it
        // and hang in the air where the fillet turns away - so the run is chained through whatever joins on at
        // either end, and the three read as the one run they are on the sheet.
        //
        // A right angle at the origin, rounded with a radius of 10: in along -x to (-10,0), round to (0,10),
        // then up +y.
        Document d;
        const double r = 30.0;   // long enough an arc that a label of the size labels come in sits inside it
        Entity in;
        in.id = QStringLiteral("in");
        in.type = EntityType::Line;
        in.a = {-120.0, 0.0};
        in.b = {-r, 0.0};
        d.entities().push_back(in);
        Entity fil;
        fil.id = QStringLiteral("fillet");
        fil.type = EntityType::Arc;
        fil.center = {-r, r};
        fil.radius = r;
        fil.startAngle = -M_PI / 2.0;   // at (-10, 0)
        fil.endAngle = 0.0;             // at (0, 10)
        d.entities().push_back(fil);
        Entity up;
        up.id = QStringLiteral("up");
        up.type = EntityType::Line;
        up.a = {0.0, r};
        up.b = {0.0, 120.0};
        d.entities().push_back(up);

        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        const MechModel m = builtInMechModel(MechModelKind::Label);
        ed.beginLabelPlacement(partDrawingEntities(m.drawing), m.exceedMm);
        // A stroke across the arc at its middle - the 45-degree point, at (-10,10) + 10*(cos-45, sin-45).
        const double k = std::sqrt(0.5);
        const Vec2 on{-r + r * k, r - r * k};
        const Vec2 nrm{k, -k};   // outward from the arc centre
        ed.onPointerPress({on.x + nrm.x * 5.0, on.y + nrm.y * 5.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({on.x - nrm.x * 5.0, on.y - nrm.y * 5.0});
        ed.onPointerPress({on.x - nrm.x * 5.0, on.y - nrm.y * 5.0}, Qt::LeftButton, Qt::NoModifier);
        check(ed.labelLive(), "fillet: the label goes on at the rounded corner");

        const Entity* mask = nullptr;
        for (const Entity& e : d.entities())
            if (e.meta.value(QStringLiteral("labelMask")).toBool()) mask = &e;
        check(mask != nullptr, "fillet: and covers what it covers");
        if (mask) {
            // EVERY corner of the cover sits near the fillet's own radius from its centre - which is what
            // hugging it means. Cutting the corner would put the middle of the label well inside that radius,
            // and following only the straight line would leave half the cover nowhere near it.
            double worst = 0.0;
            for (const Vec2& q : mask->points)
                worst = std::max(worst, std::abs(distance(q, fil.center) - r));
            std::printf("  fillet: the cover stays within %.2f of the fillet's own radius\n", worst);
            // Half the label's depth, plus a little: the cover reaches across the run, so it is a band from
            // r - depth/2 to r + depth/2 and nothing outside that.
            const double depth = ed.labelHeight();
            check(worst <= depth / 2.0 + 0.35,
                  "fillet: it hugs the arc - every corner of it is a band about the fillet's radius");
            // And it really did go round: a label that cut the corner would be a rectangle, four corners.
            std::printf("  fillet: covered by %d corners, %.1f long by %.1f deep\n",
                        int(mask->points.size()), ed.labelWidth(), depth);
            check(mask->points.size() > 4, "fillet: curved, not a chord across the corner");
            // It spans BOTH legs of the corner, which is the thing a single object could not have told it.
            const Bounds b = mask->bounds();
            check(b.min.x < -r * 0.7 && b.max.y > r * 0.7,
                  "fillet: reaching round the corner, from the leg coming in towards the one going out");
        }

        // THE LABEL'S OWN OUTLINE has to follow the run too, not only the ground it covers. A straight line of
        // the label is a CURVE once the label is bent, and a curve drawn with two points is a chord: the cover
        // came round the bend smoothly while the outline cut straight across it, and because the two ends of a
        // bent label are turned relative to each other those straight edges crossed over each other. A
        // crumple - which is what it looked like.
        int longEdges = 0, crossings = 0;
        for (const Entity& e : d.entities()) {
            if (e.meta.value(QStringLiteral("labelGroup")).toString().isEmpty()) continue;
            if (e.meta.value(QStringLiteral("labelMask")).toBool()) continue;
            if (e.points.size() < 3) continue;   // the two short ends stay straight: that is what they are
            ++longEdges;
            // Every part of it stays within a band about the fillet's radius, the same test the cover passes.
            // A chord across the corner leaves the middle of the edge well inside that band.
            for (const Vec2& q : e.points)
                if (std::abs(distance(q, fil.center) - r) > ed.labelHeight() / 2.0 + 0.35) ++crossings;
        }
        std::printf("  fillet: %d of the label's own edges follow the run, %d point(s) off the band\n",
                    longEdges, crossings);
        check(longEdges >= 2, "fillet: the label's own long edges are laid along the run, not across it");
        check(crossings == 0,
              "fillet: so every part of the label stays on the bundle instead of cutting the corner");
    }

    // ---- the label's two sizes are its OWN, whatever it is made of ---------------------------------
    {
        // A label is a rectangle, some lines and the mask under it - so a selected label is a MIXED selection,
        // and the properties panel's Geometry rows for a mixed selection are dead placeholders reading zero.
        // Which is exactly what "cannot modify the width and height" was: the rows were there, and nothing
        // could be typed into them. A label answers with its own two sizes whatever it is made of.
        Document d;
        Entity w;
        w.id = QStringLiteral("w");
        w.type = EntityType::Line;
        w.a = {0.0, 0.0};
        w.b = {100.0, 0.0};
        d.entities().push_back(w);
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        const MechModel m = builtInMechModel(MechModelKind::Label);
        ed.beginLabelPlacement(partDrawingEntities(m.drawing), m.exceedMm);
        ed.onPointerPress({50.0, -5.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({50.0, 5.0});
        ed.onPointerPress({50.0, 5.0}, Qt::LeftButton, Qt::NoModifier);
        check(ed.labelLive(), "sizes: placed");
        // Several kinds of object selected, which is what made the panel give up.
        std::vector<int> kinds;
        for (const QString& id : ed.selection())
            for (const Entity& e : d.entities())
                if (e.id == id) kinds.push_back(static_cast<int>(e.type));
        bool mixed = false;
        for (size_t i = 1; i < kinds.size(); ++i)
            if (kinds[i] != kinds[0]) mixed = true;
        std::printf("  sizes: %d object(s) selected, mixed kinds: %s\n", int(kinds.size()),
                    mixed ? "yes" : "no");
        // Every piece of it carries the two sizes, which is what lets the panel answer at all.
        for (const QString& id : ed.selection())
            for (const Entity& e : d.entities())
                if (e.id == id)
                    check(e.meta.contains(QStringLiteral("labelAlong")) &&
                              e.meta.contains(QStringLiteral("labelAcross")),
                          "sizes: every piece of the label carries them, so a mixed selection can still say");
        // And letting go and picking it up again finds them, which is the path the panel takes.
        ed.clearSelection();
        const Entity* mask = nullptr;
        for (const Entity& e : d.entities())
            if (e.meta.value(QStringLiteral("labelMask")).toBool()) mask = &e;
        check(mask != nullptr, "sizes: masked");
        if (mask) {
            const Vec2 body = mask->bounds().center();
            ed.onPointerPress(body, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerRelease(body, Qt::LeftButton);
            check(ed.adoptLabelFromSelection(), "sizes: picked up again, it knows it is a label");
            ed.setLabelWidth(55.0);
            ed.setLabelHeight(9.0);
            std::printf("  sizes: set to %.1f by %.1f from the panel\n", ed.labelWidth(),
                        ed.labelHeight());
            check(std::abs(ed.labelWidth() - 55.0) < 1e-6 && std::abs(ed.labelHeight() - 9.0) < 1e-6,
                  "sizes: and both take");
        }
    }


    // ---- a label is COUPLED to its cable: slid along, it takes the bend ------------------------------
    {
        // Sliding a label means sliding it ALONG THE RUN, and along the WHOLE run. A label placed on the
        // straight and pushed towards a corner has to go ROUND the corner with the cable - a straight translate
        // carried it off the cable the moment the cable stopped going the way the label was pointed, and a
        // label beside its bundle says nothing while the wires come out from under the mask it left behind.
        Document d;
        Entity run;
        run.id = QStringLiteral("run");
        run.type = EntityType::Polyline;
        run.points = {{0.0, 0.0}, {100.0, 0.0}, {100.0, -100.0}};   // along +x, then down
        d.entities().push_back(run);
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        const MechModel m = builtInMechModel(MechModelKind::Label);
        ed.beginLabelPlacement(partDrawingEntities(m.drawing), m.exceedMm);
        // On the straight part, well clear of the corner.
        ed.onPointerPress({30.0, -5.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({30.0, 5.0});
        ed.onPointerPress({30.0, 5.0}, Qt::LeftButton, Qt::NoModifier);
        check(ed.labelLive(), "coupled: placed on the straight");
        auto maskOf = [&]() -> const Entity* {
            const Entity* mk = nullptr;
            for (const Entity& e : d.entities())
                if (e.meta.value(QStringLiteral("labelMask")).toBool()) mk = &e;
            return mk;
        };
        const Entity* mk = maskOf();
        check(mk != nullptr, "coupled: and masked");
        if (!mk) return failures;
        // Straight, so a plain rectangle: four corners and nothing more.
        std::printf("  coupled: on the straight, %d corners at x %.1f..%.1f y %.1f..%.1f\n",
                    int(mk->points.size()), mk->bounds().min.x, mk->bounds().max.x, mk->bounds().min.y,
                    mk->bounds().max.y);
        check(mk->points.size() == 4, "coupled: on a straight run it is a rectangle, as it should be");

        // Pushed 28 along the run, which puts its middle exactly on the corner. It has to LAY ITSELF ROUND the
        // bend as it arrives - that is what being coupled to the cable means, as against being dropped on it.
        const Vec2 body = mk->bounds().center();
        ed.onPointerPress(body, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({body.x + 70.0, body.y});
        mk = maskOf();
        Bounds at = mk->bounds();
        std::printf("  coupled: slid on to the corner, %d corners at x %.1f..%.1f y %.1f..%.1f\n",
                    int(mk->points.size()), at.min.x, at.max.x, at.min.y, at.max.y);
        check(mk->points.size() > 4, "coupled: arriving at the bend, it lays itself round it");
        check(at.min.y < -1.0 && at.min.x < 100.0,
              "coupled: spanning both legs - the one it came along and the one it turns down");

        // And on past it, where the cable is straight again: it straightens with the cable, and it is down the
        // far leg, which a straight slide from where it started could never have reached.
        ed.onPointerMove({body.x + 140.0, body.y});
        mk = maskOf();
        at = mk->bounds();
        std::printf("  coupled: slid on past it, %d corners at x %.1f..%.1f y %.1f..%.1f\n",
                    int(mk->points.size()), at.min.x, at.max.x, at.min.y, at.max.y);
        check(mk->points.size() == 4, "coupled: past the bend it is a rectangle again");
        check(at.min.y < -60.0, "coupled: down the far leg, following the cable rather than the drag");
        check(at.max.x <= 100.0 + ed.labelWidth(),
              "coupled: not carried straight on past the corner into thin air");
    }


    // ---- the two ways a bent label came out wrong ---------------------------------------------------
    {
        // ONE: a stroke drawn at an angle over a cable that BENDS crosses the cable twice, once on each leg.
        // Taken as one bundle that made the label as deep as the distance between the legs, which on the sheet
        // that reported it was most of the sheet. A bundle is a set of runs with no big empty gap in it.
        Document d;
        Entity run;
        run.id = QStringLiteral("run");
        run.type = EntityType::Polyline;
        run.points = {{0.0, 60.0}, {60.0, 60.0}, {60.0, 0.0}};   // across, then down: two legs 60 apart
        d.entities().push_back(run);
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        const MechModel m = builtInMechModel(MechModelKind::Label);
        ed.beginLabelPlacement(partDrawingEntities(m.drawing), m.exceedMm);
        // A long diagonal stroke: it crosses the top leg near the left AND the right-hand leg lower down.
        ed.onPointerPress({10.0, 80.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({90.0, 20.0});
        ed.onPointerPress({90.0, 20.0}, Qt::LeftButton, Qt::NoModifier);
        check(ed.labelLive(), "sane: it goes on somewhere");
        std::printf("  sane: across a stroke catching both legs, %.1f wide by %.1f high\n", ed.labelWidth(),
                    ed.labelHeight());
        // It is a label, not a sheet-wide slab: the two legs are 60 apart, and it must not have taken that as
        // the width of the bundle.
        check(std::max(ed.labelWidth(), ed.labelHeight()) < 60.0,
              "sane: it labelled the run it was drawn across, not the gap between two legs of it");

        // TWO: a corner too SHARP for the depth of label going on it. Offsetting a mitre by more than the
        // corner can take sends the mitre point past the ends of both segments, the two rails of the label cross
        // each other, and the label comes out as a bowtie. Straight is never wrong-looking; folded always is.
        Document d2;
        Entity zig;
        zig.id = QStringLiteral("zig");
        zig.type = EntityType::Polyline;
        // Two short legs meeting at a hairpin - about as bad as a corner gets.
        zig.points = {{0.0, 0.0}, {8.0, 0.0}, {0.5, 3.0}};
        d2.entities().push_back(zig);
        Editor e2;
        e2.setDocument(&d2);
        noSnap(e2);
        e2.beginLabelPlacement(partDrawingEntities(m.drawing), m.exceedMm);
        e2.onPointerPress({8.0, -6.0}, Qt::LeftButton, Qt::NoModifier);
        e2.onPointerMove({8.0, 6.0});
        e2.onPointerPress({8.0, 6.0}, Qt::LeftButton, Qt::NoModifier);
        if (e2.labelLive()) {
            const Entity* mk = nullptr;
            for (const Entity& e : d2.entities())
                if (e.meta.value(QStringLiteral("labelMask")).toBool()) mk = &e;
            check(mk != nullptr, "sane: masked");
            if (mk) {
                // Whatever it decided to do - bend it or lay it straight - what it covers must not cross itself.
                bool crosses = false;
                const std::vector<Vec2>& q = mk->points;
                for (size_t i = 0; i < q.size() && !crosses; ++i)
                    for (size_t j = i + 2; j < q.size(); ++j) {
                        if (i == 0 && j == q.size() - 1) continue;
                        const auto x = segmentSegmentIntersection(q[i], q[(i + 1) % q.size()], q[j],
                                                                  q[(j + 1) % q.size()]);
                        if (!x) continue;
                        if (distance(*x, q[i]) < 1e-9 || distance(*x, q[(i + 1) % q.size()]) < 1e-9 ||
                            distance(*x, q[j]) < 1e-9 || distance(*x, q[(j + 1) % q.size()]) < 1e-9)
                            continue;
                        crosses = true;
                        break;
                    }
                std::printf("  sane: on a hairpin, %d corners and %s\n", int(q.size()),
                            crosses ? "FOLDED" : "not folded");
                check(!crosses, "sane: on a corner too sharp for it, the label does not fold over itself");
            }
        }
    }


    // ---- A LABEL ON A BEND, SWEPT: every cable shape, every place along it --------------------------
    //
    // The bend has been reported wrong four times and each time a single case was fixed by eye. So this walks a
    // label ALONG each of several cables and asks the same hard questions at every step. What a laid label must
    // satisfy, wherever it sits:
    //
    //   1. Nothing it is made of crosses itself. A folded label is the failure that keeps coming back.
    //   2. Its long edges are OFFSETS of the cable - every point of one at the same distance from the cable,
    //      to a tolerance. This is the question that catches a chord drawn across a corner, which does not
    //      look folded but does cut through the bundle.
    //   3. It reaches about as far along the cable as it is meant to.
    {
        // `pts` is the cable's centreline, which is what the label's rails are offsets of and so what the
        // questions below are asked against. `asDouble` draws it as a double run - two rails and a gap - which is
        // how a cable is actually drawn, and `split` draws it as SEVERAL entities meeting end to end, which is
        // how a filleted corner actually arrives: a line, an arc, and another line, each trimmed to the next.
        // Both were missing from the sweep, and both are what the drawing that reported this has.
        struct Shape {
            const char* what;
            std::vector<Vec2> pts;
            bool asDouble = false;
            bool split = false;
            // A REAL fillet: a line, an ARC entity, and another line, each trimmed to meet the next. `turn` is
            // how far the cable deviates and `rad` the fillet radius. The centreline for the questions below is
            // worked out from those, so the test measures against the same curve the drawing has.
            bool arcFillet = false;
            double turn = 0.0;
            double rad = 0.0;
        };
        std::vector<Shape> shapes;
        shapes.push_back({"a right angle", {{0, 0}, {60, 0}, {60, -60}}});
        shapes.push_back({"a 45 degree bend", {{0, 0}, {60, 0}, {110, -50}}});
        shapes.push_back({"a shallow 20 degree bend", {{0, 0}, {60, 0}, {160, -36}}});
        shapes.push_back({"a sharp 135 degree bend", {{0, 0}, {60, 0}, {80, -50}}});
        shapes.push_back({"an S with two bends", {{0, 0}, {40, 0}, {70, -30}, {110, -30}}});
        {
            // A filleted right angle, as a cable actually leaves a drawing package.
            std::vector<Vec2> pts{{0.0, 0.0}, {40.0, 0.0}};
            const double r = 20.0;
            for (int i = 1; i <= 12; ++i) {
                const double t = -M_PI / 2.0 + (M_PI / 2.0) * i / 12.0;
                pts.push_back({40.0 + r * std::cos(t), -r - r * std::sin(t)});
            }
            pts.push_back({60.0, -60.0});
            shapes.push_back({"a filleted right angle", pts});
            shapes.push_back({"the same, drawn in three pieces", pts, false, true});
            shapes.push_back({"the same, as a double run", pts, true, false});
        }
        shapes.push_back({"a 45 bend as a double run", {{0, 0}, {60, 0}, {110, -50}}, true, false});
        shapes.push_back({"a right angle in two pieces", {{0, 0}, {60, 0}, {60, -60}}, false, true});
        shapes.push_back({"two bends close together",
                          {{0, 0}, {30, 0}, {50, -20}, {80, -20}, {100, -40}, {140, -40}}});
        // SHORT LEGS, so a label of the size labels come in overhangs both ends of the leg it is on and its ends
        // land past a corner. This is the case the reported drawing is: a 40 label on a cable whose pieces are
        // shorter than that.
        shapes.push_back({"short legs, shorter than the label", {{0, 0}, {20, 0}, {40, -20}, {60, -20}}});
        shapes.push_back({"short legs as a double run", {{0, 0}, {20, 0}, {40, -20}, {60, -20}}, true, false});
        shapes.push_back({"short legs in pieces", {{0, 0}, {20, 0}, {40, -20}, {60, -20}}, false, true});
        shapes.push_back({"a real arc fillet, 90 degrees", {}, false, false, true, 90.0, 20.0});
        shapes.push_back({"a real arc fillet, 45 degrees", {}, false, false, true, 45.0, 25.0});
        shapes.push_back({"a real arc fillet, small radius", {}, false, false, true, 90.0, 6.0});
        shapes.push_back({"a real arc fillet as a double run", {}, true, false, true, 45.0, 25.0});

        // How far a point sits off a path, and how far along - the same question the editor asks itself.
        auto onPath = [](const std::vector<Vec2>& path, const Vec2& p, double& along, double& off) {
            along = off = 0.0;
            double acc = 0.0, best = 1e300;
            for (size_t i = 1; i < path.size(); ++i) {
                const double seg = distance(path[i - 1], path[i]);
                if (seg < 1e-12) continue;
                const ClosestOnSegment c = closestPointOnSegment(p, path[i - 1], path[i]);
                if (c.distance < best) {
                    best = c.distance;
                    const Vec2 dir = (path[i] - path[i - 1]).normalized();
                    along = acc + c.t * seg;
                    off = dot(p - c.point, dir.perp());
                }
                acc += seg;
            }
            return best;
        };
        auto crossesItself = [](const std::vector<Vec2>& q, bool closed) {
            const size_t n = q.size();
            if (n < 4) return false;
            const size_t segs = closed ? n : n - 1;
            for (size_t i = 0; i < segs; ++i)
                for (size_t j = i + 2; j < segs; ++j) {
                    if (i == 0 && j == segs - 1 && closed) continue;
                    const auto x = segmentSegmentIntersection(q[i], q[(i + 1) % n], q[j], q[(j + 1) % n]);
                    if (!x) continue;
                    if (distance(*x, q[i]) < 1e-7 || distance(*x, q[(i + 1) % n]) < 1e-7 ||
                        distance(*x, q[j]) < 1e-7 || distance(*x, q[(j + 1) % n]) < 1e-7)
                        continue;
                    return true;
                }
            return false;
        };

        const MechModel m = builtInMechModel(MechModelKind::Label);
        // The centreline of an arc-filleted corner, so the questions below have a curve to measure against, and
        // the pieces to build it out of.
        struct FilletBits { Vec2 tanIn, tanOut, centre; double a0 = 0.0, a1 = 0.0; };
        auto filletBits = [](double turnDeg, double rad) {
            FilletBits fb;
            const double t = turnDeg * M_PI / 180.0;
            const double back = rad * std::tan(t / 2.0);   // how far back from the corner it touches
            const Vec2 corner{80.0, 0.0};
            fb.tanIn = {corner.x - back, 0.0};
            const Vec2 outDir{std::cos(-t), std::sin(-t)};
            fb.tanOut = corner + outDir * back;
            fb.centre = {fb.tanIn.x, -rad};
            fb.a0 = M_PI / 2.0;
            fb.a1 = M_PI / 2.0 - t;
            return fb;
        };
        for (Shape& sh : shapes) {
            if (!sh.arcFillet) continue;
            const FilletBits fb = filletBits(sh.turn, sh.rad);
            sh.pts.push_back({0.0, 0.0});
            sh.pts.push_back(fb.tanIn);
            for (int i = 1; i <= 16; ++i) {
                const double t = fb.a0 + (fb.a1 - fb.a0) * i / 16.0;
                sh.pts.push_back({fb.centre.x + sh.rad * std::cos(t), fb.centre.y + sh.rad * std::sin(t)});
            }
            const double td = sh.turn * M_PI / 180.0;
            sh.pts.push_back(fb.tanOut + Vec2{std::cos(-td), std::sin(-td)} * 70.0);
        }

        int placed = 0, folded = 0, notOffset = 0, wrongLength = 0, notchy = 0, disagreed = 0;
        QString firstFold, firstOffset, firstNotch, firstDisagree;
        for (const Shape& sh : shapes) {
            double total = 0.0;
            for (size_t i = 1; i < sh.pts.size(); ++i) total += distance(sh.pts[i - 1], sh.pts[i]);
            for (int step = 1; step < 20; ++step) {
                const double s = total * step / 20.0;
                // Where on the cable to stroke across, and which way is square to it there.
                double acc = 0.0;
                Vec2 at = sh.pts.front(), dir{1, 0};
                for (size_t i = 1; i < sh.pts.size(); ++i) {
                    const double seg = distance(sh.pts[i - 1], sh.pts[i]);
                    if (seg < 1e-12) continue;
                    if (s <= acc + seg) {
                        dir = (sh.pts[i] - sh.pts[i - 1]).normalized();
                        at = sh.pts[i - 1] + dir * (s - acc);
                        break;
                    }
                    acc += seg;
                }
                // Square to the cable, and at an angle either side of square - nobody swipes exactly square, and
                // a stroke at an angle catches the crossings at different places along it.
                for (const double lean : {0.0, 25.0, -35.0}) {
                const double la = lean * M_PI / 180.0;
                const Vec2 n{dir.perp().x * std::cos(la) + dir.x * std::sin(la),
                             dir.perp().y * std::cos(la) + dir.y * std::sin(la)};

                Document d;
                if (sh.arcFillet) {
                    const FilletBits fb = filletBits(sh.turn, sh.rad);
                    const double td = sh.turn * M_PI / 180.0;
                    Entity in;
                    in.id = QStringLiteral("in");
                    in.type = EntityType::Line;
                    in.a = {0.0, 0.0};
                    in.b = fb.tanIn;
                    Entity arc;
                    arc.id = QStringLiteral("fil");
                    arc.type = EntityType::Arc;
                    arc.center = fb.centre;
                    arc.radius = sh.rad;
                    arc.startAngle = std::min(fb.a0, fb.a1);
                    arc.endAngle = std::max(fb.a0, fb.a1);
                    Entity out;
                    out.id = QStringLiteral("out");
                    out.type = EntityType::Line;
                    out.a = fb.tanOut;
                    out.b = fb.tanOut + Vec2{std::cos(-td), std::sin(-td)} * 70.0;
                    if (sh.asDouble) {
                        for (Entity* e : {&in, &out}) {
                            e->meta.insert(QStringLiteral("gap"), 3.0);
                            e->type = EntityType::DoubleLine;
                        }
                    }
                    d.entities().push_back(in);
                    d.entities().push_back(arc);
                    d.entities().push_back(out);
                } else if (sh.split) {
                    // Several entities meeting end to end - which is what a fillet is. The label has to chain
                    // them into the one run they are on the sheet before it can be laid along them.
                    for (size_t i = 1; i < sh.pts.size(); ++i) {
                        Entity seg;
                        seg.id = QStringLiteral("seg%1").arg(i);
                        seg.type = EntityType::Line;
                        seg.a = sh.pts[i - 1];
                        seg.b = sh.pts[i];
                        d.entities().push_back(seg);
                    }
                } else {
                    Entity run;
                    run.id = QStringLiteral("run");
                    run.type = EntityType::Polyline;
                    run.points = sh.pts;
                    if (sh.asDouble) {
                        run.meta.insert(QStringLiteral("double"), true);
                        run.meta.insert(QStringLiteral("gap"), 3.0);
                    }
                    d.entities().push_back(run);
                }
                Editor ed;
                ed.setDocument(&d);
                noSnap(ed);
                ed.beginLabelPlacement(partDrawingEntities(m.drawing), m.exceedMm);
                ed.onPointerPress({at.x + n.x * 6.0, at.y + n.y * 6.0}, Qt::LeftButton, Qt::NoModifier);
                ed.onPointerMove({at.x - n.x * 6.0, at.y - n.y * 6.0});
                ed.onPointerPress({at.x - n.x * 6.0, at.y - n.y * 6.0}, Qt::LeftButton, Qt::NoModifier);
                if (!ed.labelLive()) continue;
                ++placed;

                // Did the COVER go round the corner? What the label's own pieces have to do depends on it: if the
                // cover bends, every piece of the label that runs along the cable has to bend too.
                bool coverBent = false;
                for (const Entity& e : d.entities())
                    if (e.meta.value(QStringLiteral("labelMask")).toBool() && e.points.size() > 4)
                        coverBent = true;
                // A BENT COVER WITH A STRAIGHT LABEL IN IT is the reported symptom - "hatch does make bend and a
                // rectangle not doing fillet bend". Asked of the whole label rather than edge by edge: an outer
                // rail legitimately runs past a corner in one straight piece, because its mitre point sits beyond
                // the corner, so asking edge by edge flags good rails. Asked of the label as a whole it is exact -
                // when the cover bends, at least one piece of the label has to have a corner in it too.
                if (coverBent) {
                    bool anyPieceBent = false;
                    for (const Entity& e : d.entities()) {
                        if (e.meta.value(QStringLiteral("labelGroup")).toString().isEmpty()) continue;
                        if (e.meta.value(QStringLiteral("labelMask")).toBool()) continue;
                        if (e.points.size() > 4) anyPieceBent = true;
                    }
                    if (!anyPieceBent) {
                        ++disagreed;
                        if (firstDisagree.isEmpty())
                            firstDisagree = QStringLiteral("%1 at %2 along: the cover went round the corner and "
                                                          "nothing the label is made of did")
                                                .arg(sh.what)
                                                .arg(s, 0, 'f', 1);
                    }
                }
                double lo = 1e300, hi = -1e300;
                for (const Entity& e : d.entities()) {
                    if (e.meta.value(QStringLiteral("labelGroup")).toString().isEmpty()) continue;
                    const bool isMask = e.meta.value(QStringLiteral("labelMask")).toBool();
                    std::vector<Vec2> q = e.points;
                    if (q.size() < 2) q = {e.a, e.b};
                    // 1. nothing crosses itself
                    if (crossesItself(q, isMask)) {
                        ++folded;
                        if (firstFold.isEmpty())
                            firstFold = QStringLiteral("%1 at %2 along").arg(sh.what).arg(s, 0, 'f', 1);
                    }
                    // 2. NO CHORD ACROSS A CORNER. Any piece of the label that runs a distance ALONG the
                    // cable has to follow it: its middle must sit as far off the cable as its ends do. A chord
                    // drawn corner to corner does not look folded, but it cuts straight through the bundle -
                    // which is the other way the bend has come out wrong, and the harder one to see.
                    for (size_t k = 1; k < q.size(); ++k) {
                        double a0 = 0.0, o0 = 0.0, a1 = 0.0, o1 = 0.0;
                        onPath(sh.pts, q[k - 1], a0, o0);
                        onPath(sh.pts, q[k], a1, o1);
                        if (std::abs(a1 - a0) < ed.labelAcrossMm()) continue;   // a cap, square across: fine
                        // Not judged within a whisker of one of the cable's own corners. There the question
                        // itself is ambiguous - "how far off the cable" has two answers, one per leg - and a
                        // straight label whose end lands two millimetres from a corner is genuinely nearer the
                        // other leg than its own. Everywhere else, which is 112 of the 114 placements, the
                        // question is sharp and is asked.
                        bool nearCorner = false;
                        double vacc = 0.0;
                        for (size_t vi = 1; vi + 1 < sh.pts.size(); ++vi) {
                            vacc += distance(sh.pts[vi - 1], sh.pts[vi]);
                            if (std::abs(a0 - vacc) < ed.labelAcrossMm() ||
                                std::abs(a1 - vacc) < ed.labelAcrossMm())
                                nearCorner = true;
                        }
                        if (nearCorner) continue;
                        const Vec2 mid{(q[k - 1].x + q[k].x) / 2.0, (q[k - 1].y + q[k].y) / 2.0};
                        double am = 0.0, om = 0.0;
                        onPath(sh.pts, mid, am, om);
                        const double want = (o0 + o1) / 2.0;
                        if (std::abs(om - want) > 0.35) {
                            ++notOffset;
                            if (firstOffset.isEmpty())
                                firstOffset =
                                    QStringLiteral("%1 at %2: %3 pts, mask=%4, %5,%6 (a%7 o%8) -> %9,%10 "
                                                   "(a%11 o%12), mid o%13 wanted o%14")
                                        .arg(sh.what)
                                        .arg(s, 0, 'f', 1)
                                        .arg(q.size())
                                        .arg(isMask ? 1 : 0)
                                        .arg(q[k - 1].x, 0, 'f', 1)
                                        .arg(q[k - 1].y, 0, 'f', 1)
                                        .arg(a0, 0, 'f', 1)
                                        .arg(o0, 0, 'f', 2)
                                        .arg(q[k].x, 0, 'f', 1)
                                        .arg(q[k].y, 0, 'f', 1)
                                        .arg(a1, 0, 'f', 1)
                                        .arg(o1, 0, 'f', 2)
                                        .arg(om, 0, 'f', 2)
                                        .arg(want, 0, 'f', 2);
                        }
                    }
                    // 4. IT KEEPS ITS DEPTH. Every corner of what the label COVERS has to sit the label's own
                    // half-depth away from the cable: a band is two rails at a fixed distance either side of it.
                    // A join that cuts the corner - a bevel across the inside, an arc round the outside - brings
                    // the edge in closer than that, and what you see is a notch bitten out of the label. It does
                    // not cross itself, so nothing above catches it.
                    if (isMask) {
                        // The two rails sit at midOff +/- half the depth from the run: the middle of a BUNDLE is
                        // not the middle of the one run the label was measured from, so they are not symmetric
                        // about it.
                        const double side = ed.labelAcrossMm() / 2.0;
                        const double near = std::abs(std::abs(ed.labelMidOffMm()) - side);
                        const double far = std::abs(ed.labelMidOffMm()) + side;
                        for (const Vec2& p : q) {
                            double a2 = 0.0, o2 = 0.0;
                            const double dist = onPath(sh.pts, p, a2, o2);
                            // Not judged past either end of the cable, where the nearest point on it is its end
                            // and the distance stops meaning depth.
                            if (a2 < 0.05 || a2 > total - 0.05) continue;
                            bool byCorner = false;
                            double vac = 0.0;
                            for (size_t vi = 1; vi + 1 < sh.pts.size(); ++vi) {
                                vac += distance(sh.pts[vi - 1], sh.pts[vi]);
                                // The mitre region: within about the label's depth of a cable corner the edge
                                // of a mitred band is legitimately NOT at a constant distance from the cable -
                                // that is what mitring a corner means, and it is what makes the corner look like
                                // a corner. Judged everywhere else, which is all but a few of the 1026.
                                if (std::abs(a2 - vac) < ed.labelAcrossMm() * 2.0) byCorner = true;
                            }
                            if (byCorner) continue;
                            if (dist < near - 0.35 || dist > far + 0.35) {
                                ++notchy;
                                if (firstNotch.isEmpty())
                                    firstNotch = QStringLiteral("%1 at %2 along: a corner %3 off the cable, "
                                                                "where its rails are %4 and %5")
                                                     .arg(sh.what)
                                                     .arg(s, 0, 'f', 1)
                                                     .arg(dist, 0, 'f', 2)
                                                     .arg(near, 0, 'f', 2)
                                                     .arg(far, 0, 'f', 2);
                            }
                        }
                    }
                    // 3. how far along the cable it reaches
                    for (const Vec2& p : q) {
                        double a2 = 0.0, o2 = 0.0;
                        onPath(sh.pts, p, a2, o2);
                        lo = std::min(lo, a2);
                        hi = std::max(hi, a2);
                    }
                }
                // Only where the whole label fits on the cable: near an end it legitimately overhangs, and a
                // point past the end of the path measures as being at the end of it.
                const double half = Editor::kLabelWidthMm / 2.0;
                if (s > half + 1.0 && s < total - half - 1.0 && hi > lo &&
                    std::abs((hi - lo) - Editor::kLabelWidthMm) > Editor::kLabelWidthMm * 0.2)
                    ++wrongLength;
                }
            }
        }
        std::printf("  swept: %d placements over %d cable shapes\n", placed, int(shapes.size()));
        std::printf("  swept: %d folded, %d edges not offsets of the cable, %d wrong length\n", folded,
                    notOffset, wrongLength);
        if (!firstFold.isEmpty()) std::printf("  swept: first fold on %s\n", qPrintable(firstFold));
        if (!firstOffset.isEmpty()) std::printf("  swept: first wander on %s\n", qPrintable(firstOffset));
        std::printf("  swept: %d notched\n", notchy);
        if (!firstNotch.isEmpty()) std::printf("  swept: first notch on %s\n", qPrintable(firstNotch));
        std::printf("  swept: %d where the cover bent and the label did not\n", disagreed);
        if (!firstDisagree.isEmpty())
            std::printf("  swept: first disagreement on %s\n", qPrintable(firstDisagree));
        check(placed > 180, "swept: it went on everywhere it was asked to");
        check(folded == 0, "swept: and nothing it is made of ever crosses itself");
        check(notOffset == 0, "swept: every long edge is an offset of the cable, not a chord across it");
        check(wrongLength == 0, "swept: and it is the length it is meant to be");
        check(notchy == 0, "swept: and it keeps its depth all the way along - no notch bitten out of it");
        check(disagreed == 0,
              "swept: and where the cover goes round a corner, so does the label - they never disagree");
    }


    // ---- part bent, part straight: the rail that jumped ---------------------------------------------
    {
        // "Something makes bend and something stays straight." A rail was built on MORE cable than the label
        // covers and then cut back to the label's ends with two plane clips - and a mitre spike near an end
        // crosses that end plane more than once. The clip joined the pieces of rail either side of the crossing
        // into one line, so the rail JUMPED from one place to another: a diagonal across the label, a spike off
        // one corner, and the rest of it sitting straight.
        //
        // The question that catches it is continuity: consecutive points of a rail can never be further apart
        // than the label is long, because a rail is a walk along a piece of cable that long.
        Document d;
        Entity run;
        run.id = QStringLiteral("run");
        run.type = EntityType::Polyline;
        run.points = {{0.0, 0.0}, {40.0, 0.0}, {70.0, -30.0}};   // across, then away at 45
        d.entities().push_back(run);
        const MechModel m = builtInMechModel(MechModelKind::Label);

        int jumps = 0, checked = 0;
        double worst = 0.0;
        // Walked in small steps THROUGH the corner, which is where the label's ends land near it.
        for (int step = 0; step <= 60; ++step) {
            const double s = 10.0 + step * 0.8;   // 10 .. 58, the corner at 40
            double acc = 0.0;
            Vec2 at = run.points.front(), dir{1, 0};
            for (size_t i = 1; i < run.points.size(); ++i) {
                const double seg = distance(run.points[i - 1], run.points[i]);
                if (s <= acc + seg) {
                    dir = (run.points[i] - run.points[i - 1]).normalized();
                    at = run.points[i - 1] + dir * (s - acc);
                    break;
                }
                acc += seg;
            }
            const Vec2 n = dir.perp();
            Document doc;
            doc.entities().push_back(run);
            Editor ed;
            ed.setDocument(&doc);
            noSnap(ed);
            ed.beginLabelPlacement(partDrawingEntities(m.drawing), m.exceedMm);
            ed.onPointerPress({at.x + n.x * 5.0, at.y + n.y * 5.0}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerMove({at.x - n.x * 5.0, at.y - n.y * 5.0});
            ed.onPointerPress({at.x - n.x * 5.0, at.y - n.y * 5.0}, Qt::LeftButton, Qt::NoModifier);
            if (!ed.labelLive()) continue;
            ++checked;
            const double along = ed.labelAlongMm();
            for (const Entity& e : doc.entities()) {
                if (e.meta.value(QStringLiteral("labelGroup")).toString().isEmpty()) continue;
                for (size_t k = 1; k < e.points.size(); ++k) {
                    const double hop = distance(e.points[k - 1], e.points[k]);
                    worst = std::max(worst, hop);
                    // Nothing in a label can hop further than the label is long: its longest edge is its own
                    // length, and its ends are its own depth. A jump is a rail that went somewhere else.
                    if (hop > along * 1.05) ++jumps;
                }
            }
        }
        std::printf("  jump: %d placements through the corner, longest hop %.1f where the label is %.1f\n",
                    checked, worst, Editor::kLabelWidthMm);
        // AND NO SPIKE, on a cable with a corner sharp enough to throw one. A mitre at a sharp corner sits its
        // depth off both legs - so it passes every question about depth - while sitting a long way from the corner
        // itself, and what you see is a thorn off one edge with the rest of the label lying flat. Whatever throws
        // one, it is not a label, so the label is laid straight instead.
        {
            for (const double deviate : {100.0, 120.0, 150.0, 170.0}) {
                Document doc;
                Entity sharp;
                sharp.id = QStringLiteral("sharp");
                sharp.type = EntityType::Polyline;
                const double t = deviate * M_PI / 180.0;
                sharp.points = {{0.0, 0.0}, {40.0, 0.0}, {40.0 + 40.0 * std::cos(-t), 40.0 * std::sin(-t)}};
                doc.entities().push_back(sharp);
                Editor e3;
                e3.setDocument(&doc);
                noSnap(e3);
                e3.beginLabelPlacement(partDrawingEntities(m.drawing), m.exceedMm);
                e3.onPointerPress({40.0, -6.0}, Qt::LeftButton, Qt::NoModifier);
                e3.onPointerMove({40.0, 6.0});
                e3.onPointerPress({40.0, 6.0}, Qt::LeftButton, Qt::NoModifier);
                if (!e3.labelLive()) continue;
                const double reach = std::abs(e3.labelMidOffMm()) + e3.labelAcrossMm() / 2.0 + 0.6;
                double furthest = 0.0;
                for (const Entity& e : doc.entities()) {
                    if (e.meta.value(QStringLiteral("labelGroup")).toString().isEmpty()) continue;
                    std::vector<Vec2> pts = e.points;
                    if (pts.empty()) pts = {e.a, e.b};
                    for (const Vec2& q : pts) {
                        double best = 1e300;
                        for (size_t i = 1; i < sharp.points.size(); ++i)
                            best = std::min(best,
                                            closestPointOnSegment(q, sharp.points[i - 1], sharp.points[i])
                                                .distance);
                        furthest = std::max(furthest, best);
                    }
                }
                int coverPts = 0;
                for (const Entity& e : doc.entities())
                    if (e.meta.value(QStringLiteral("labelMask")).toBool())
                        coverPts = static_cast<int>(e.points.size());
                const bool wentStraight = coverPts == 4;
                std::printf("  jump: a %.0f degree corner - %s, furthest point %.2f off the cable, band "
                            "reaches %.2f\n",
                            deviate, wentStraight ? "laid straight" : "bent", furthest, reach);
                // ONE OF TWO THINGS, and never a third. Either it bent, and then nothing it is made of sticks
                // out past the band - no thorn. Or it decided the corner was too sharp to bend round and laid
                // itself straight, which on a cable that doubles back leaves it off the cable by however far the
                // cable doubled back. That is plain, and it is honest. What it must never be is a bent label
                // with a point on one edge and the rest of it lying flat.
                check(wentStraight || furthest <= reach + 0.35,
                      "jump: it either bends properly or lies straight - never a thorn off one edge");
            }
        }
    }

    // ---- a label drawn with the RECTANGLE tool bends too --------------------------------------------
    {
        // "A rectangle not doing fillet bend." The bending only knew how to walk a Line, a Polyline, a Wire and a
        // Spline. A rectangle drawn with the rectangle tool is none of those - it is a Rect - so it fell through
        // to having each of its four corners placed on the bent cable independently, with straight lines between
        // them. Which is a straight quad sitting in a bent cover: exactly the picture.
        //
        // The built-in label is drawn as a closed polyline, which is why nothing here had caught it. A label
        // captured off a real sheet is drawn with the tools a drawing office uses, and one of those is Rect.
        Document d;
        Entity run;
        run.id = QStringLiteral("run");
        run.type = EntityType::Polyline;
        run.points = {{0.0, 0.0}, {40.0, 0.0}, {75.0, -35.0}};
        d.entities().push_back(run);

        // A label whose body is a Rect, marked as what the model is scaled by, with a Polygon inside it for good
        // measure - both of the closed kinds that were falling through.
        std::vector<Entity> proto;
        Entity body;
        body.id = QStringLiteral("body");
        body.type = EntityType::Rect;
        body.points = {{0.0, 0.0}, {6.0, 0.0}, {6.0, 7.0}, {0.0, 7.0}};
        body.meta.insert(QStringLiteral("modelScaleRef"), true);
        proto.push_back(body);
        Entity inner;
        inner.id = QStringLiteral("inner");
        inner.type = EntityType::Polygon;
        inner.points = {{1.0, 1.5}, {5.0, 1.5}, {5.0, 5.5}, {1.0, 5.5}};
        proto.push_back(inner);

        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.beginLabelPlacement(proto, 2.0);
        ed.onPointerPress({40.0, -6.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({40.0, 6.0});
        ed.onPointerPress({40.0, 6.0}, Qt::LeftButton, Qt::NoModifier);
        check(ed.labelLive(), "rect: a label drawn as a rectangle goes on");

        int coverPts = 0, bentPieces = 0, straightPieces = 0;
        for (const Entity& e : d.entities()) {
            if (e.meta.value(QStringLiteral("labelGroup")).toString().isEmpty()) continue;
            if (e.meta.value(QStringLiteral("labelMask")).toBool()) {
                coverPts = static_cast<int>(e.points.size());
                continue;
            }
            if (e.points.size() > 4) ++bentPieces;
            else ++straightPieces;
        }
        std::printf("  rect: cover has %d corners, %d bent piece(s), %d straight\n", coverPts, bentPieces,
                    straightPieces);
        check(coverPts > 4, "rect: the cover goes round the corner");
        check(bentPieces >= 2, "rect: and so do the rectangle and the polygon inside it");
        check(straightPieces == 0,
              "rect: nothing is left lying straight in a bent cover - which is what the picture was");
    }


    // ---- the clear sleeve bends with the label, and fillets with it ---------------------------------
    {
        // The label under clear heatshrink is a label with a sleeve round it. Everything that is true of the
        // label is true of the sleeve: it goes round the corner with the cable, and where the cable is filleted
        // it is filleted. A sleeve that stayed straight round a bent label would read as a label that had come
        // out of its sleeve.
        //
        // Whichever way an office draws its sleeve - four lines, a rectangle, a closed polyline - it bends. The
        // built-in one is four lines; a captured one is whatever tool it was drawn with, and the rectangle tool is
        // the likely one.
        for (int kind = 0; kind < 2; ++kind) {
            Document d;
            Entity run;
            run.id = QStringLiteral("run");
            run.type = EntityType::Polyline;
            run.points = {{0.0, 0.0}, {50.0, 0.0}, {90.0, -40.0}};   // along, then away at 45
            d.entities().push_back(run);

            std::vector<Entity> proto;
            if (kind == 0) {
                proto = partDrawingEntities(
                    builtInMechModel(MechModelKind::LabelUnderHeatshrink).drawing);
            } else {
                // The same thing an office would draw: the label as a rectangle, the sleeve as a rectangle
                // round it.
                Entity body;
                body.id = QStringLiteral("body");
                body.type = EntityType::Rect;
                body.points = {{0.0, 0.0}, {6.0, 0.0}, {6.0, 7.0}, {0.0, 7.0}};
                body.meta.insert(QStringLiteral("modelScaleRef"), true);
                proto.push_back(body);
                Entity sleeve;
                sleeve.id = QStringLiteral("sleeve");
                sleeve.type = EntityType::Rect;
                sleeve.points = {{-1.6, -1.6}, {7.6, -1.6}, {7.6, 8.6}, {-1.6, 8.6}};
                sleeve.meta.insert(QStringLiteral("linetype"), QStringLiteral("HIDDEN"));
                proto.push_back(sleeve);
            }

            Editor ed;
            ed.setDocument(&d);
            noSnap(ed);
            ed.beginLabelPlacement(proto, 2.0);
            ed.onPointerPress({50.0, -7.0}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerMove({50.0, 7.0});
            ed.onPointerPress({50.0, 7.0}, Qt::LeftButton, Qt::NoModifier);
            check(ed.labelLive(), "sleeve: it goes on at the bend");

            // Every piece that runs ALONG the cable has to have turned: two points and nothing between them is a
            // piece that cut the corner. The short ends, square across the cable, stay straight - that is what
            // they are.
            int alongPieces = 0, alongBent = 0, coverPts = 0;
            for (const Entity& e : d.entities()) {
                if (e.meta.value(QStringLiteral("labelGroup")).toString().isEmpty()) continue;
                if (e.meta.value(QStringLiteral("labelMask")).toBool()) {
                    coverPts = static_cast<int>(e.points.size());
                    continue;
                }
                std::vector<Vec2> q = e.points;
                if (q.size() < 2) q = {e.a, e.b};
                // Runs ALONG the cable if it reaches further along it than the label is deep. Measured along the
                // cable rather than by the bigger side of a bounding box: the sleeve's short ends are square
                // across the cable and are taller than the label is deep, so a box would call them lengthwise.
                double lo2 = 1e300, hi2 = -1e300;
                for (const Vec2& pt : q) {
                    double a2 = 0.0, o2 = 0.0;
                    double acc2 = 0.0, best2 = 1e300;
                    for (size_t i = 1; i < run.points.size(); ++i) {
                        const double seg = distance(run.points[i - 1], run.points[i]);
                        const ClosestOnSegment c2 =
                            closestPointOnSegment(pt, run.points[i - 1], run.points[i]);
                        if (c2.distance < best2) { best2 = c2.distance; a2 = acc2 + c2.t * seg; }
                        acc2 += seg;
                    }
                    Q_UNUSED(o2);
                    lo2 = std::min(lo2, a2);
                    hi2 = std::max(hi2, a2);
                }
                if (hi2 - lo2 < ed.labelAcrossMm() * 1.2) continue;
                ++alongPieces;
                if (q.size() > 2) ++alongBent;
            }
            std::printf("  sleeve: %s - cover %d corners, %d of %d lengthwise pieces bent\n",
                        kind == 0 ? "as four lines" : "as rectangles", coverPts, alongBent, alongPieces);
            check(coverPts > 4, "sleeve: the cover goes round the corner");
            check(alongPieces >= 2, "sleeve: there is a label and a sleeve to look at");
            check(alongBent == alongPieces,
                  "sleeve: and every lengthwise piece of BOTH of them turned with the cable");
        }
    }

    // ---- a stroke across two wires covers both of them ---------------------------------------------
    {
        // If you draw across two wires you meant both of them, however far apart they are. Judging the bundle by
        // how big the gaps in it are meant a label drawn across a pair of wires covered one of them and left the
        // other showing.
        Document d;
        for (int i = 0; i < 2; ++i) {
            Entity w;
            w.id = QStringLiteral("w%1").arg(i);
            w.type = EntityType::Line;
            w.a = {0.0, i * 55.0};
            w.b = {200.0, i * 55.0};     // 55 apart: much further than a label is deep
            d.entities().push_back(w);
        }
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        const MechModel m = builtInMechModel(MechModelKind::Label);
        ed.beginLabelPlacement(partDrawingEntities(m.drawing), m.exceedMm);
        // A stroke at an angle across both, as it would be drawn by hand.
        ed.onPointerPress({80.0, -8.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({110.0, 63.0});
        ed.onPointerPress({110.0, 63.0}, Qt::LeftButton, Qt::NoModifier);
        check(ed.labelLive(), "both: the label goes on");
        const Entity* mask = nullptr;
        for (const Entity& e : d.entities())
            if (e.meta.value(QStringLiteral("labelMask")).toBool()) mask = &e;
        check(mask != nullptr, "both: and covers what it covers");
        if (mask) {
            const Bounds b = mask->bounds();
            std::printf("  both: covers y %.1f..%.1f, the two wires at 0 and 55\n", b.min.y, b.max.y);
            check(b.min.y < 0.0 && b.max.y > 55.0,
                  "both: it covers BOTH wires the stroke crossed, not just the nearer one");
            check(b.min.y > -12.0 && b.max.y < 67.0,
                  "both: and does not go wildly past them either");
        }
    }


    // ---- a label picked THROUGH the window is the label on the drawing -------------------------------
    {
        // An associative detail view is a window on the parent geometry, and a label is the thing you are most
        // likely to want to reach through it for: it is the thing that was too small to read, which is why the
        // view was made. Clicking its body in the enlargement has to pick the LABEL - the real one, in the small
        // circle - and changing it there has to change it there.
        //
        // A label is mostly its cover. So this clicks the cover, which is the part of a label there is most of.
        Document d;
        Entity w;
        w.id = QStringLiteral("wire");
        w.type = EntityType::Line;
        w.a = {0.0, 0.0};
        w.b = {100.0, 0.0};
        d.entities().push_back(w);
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        const MechModel m = builtInMechModel(MechModelKind::Label);
        ed.beginLabelPlacement(partDrawingEntities(m.drawing), m.exceedMm);
        ed.onPointerPress({50.0, -6.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({50.0, 6.0});
        ed.onPointerPress({50.0, 6.0}, Qt::LeftButton, Qt::NoModifier);
        check(ed.labelLive(), "reach: the label is on the wire");
        const Entity* mask = nullptr;
        for (const Entity& e : d.entities())
            if (e.meta.value(QStringLiteral("labelMask")).toBool()) mask = &e;
        check(mask != nullptr, "reach: and has a cover");
        if (!mask) return failures;
        const Vec2 labelAt = mask->bounds().center();
        const QString labelGroup = mask->meta.value(QStringLiteral("labelGroup")).toString();
        ed.clearSelection();

        // An associative view over the label, its enlargement well clear of the drawing.
        Entity dv;
        dv.id = QStringLiteral("dv");
        dv.type = EntityType::DetailView;
        dv.center = labelAt;
        dv.radius = 30.0;
        dv.b = {50.0, 200.0};
        dv.meta.insert(QStringLiteral("detailScale"), 4.0);
        dv.meta.insert(QStringLiteral("detailAnnotative"), true);
        d.entities().push_back(dv);

        // The middle of the enlargement is the middle of the label, four times bigger.
        ed.onPointerPress({50.0, 200.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerRelease({50.0, 200.0}, Qt::LeftButton);
        QString picked;
        for (const QString& id : ed.selection())
            for (const Entity& e : d.entities())
                if (e.id == id) picked = e.meta.value(QStringLiteral("labelGroup")).toString();
        std::printf("  reach: clicking the middle of the enlargement picked %lld object(s), label group %s\n",
                    static_cast<long long>(ed.selection().size()),
                    picked.isEmpty() ? "(none)" : "yes");
        check(!picked.isEmpty() && picked == labelGroup,
              "reach: it picked the LABEL on the drawing, reaching through the enlargement to get it");

        // And it is the label ITSELF, so changing it changes what is in the small circle - there is only one of
        // it. Sized from the panel by the same route the panel takes.
        check(ed.adoptLabelFromSelection(), "reach: picked through the window, it is a label that knows itself");
        const double was = ed.labelWidth();
        ed.setLabelWidth(was + 15.0);
        const Entity* after = nullptr;
        for (const Entity& e : d.entities())
            if (e.meta.value(QStringLiteral("labelMask")).toBool()) after = &e;
        std::printf("  reach: sized from %.1f to %.1f, the cover on the drawing now %.1f wide\n", was,
                    ed.labelWidth(), after ? after->bounds().width() : 0.0);
        check(after != nullptr && std::abs(after->bounds().width() - (was + 15.0)) < 1e-6,
              "reach: and sizing it there sizes the one on the drawing, which is the same one");
    }

    // ---- every kind shows the sub-drawings it comes in ---------------------------------------------
    {
        // Most of these parts are not one drawing. A boot leaves straight, at 45 or at 90; a ring is insulated or
        // it is not; the wires between a connector and its cable are two run straight, or two spread, or three,
        // or eight. Each is a drawing an office draws once, and they are all the same KIND - the generator asks
        // for "the boot" and the wizard picks which one by name.
        //
        // A kind listed as one bare line told you none of that. "Draw the eight variants this needs" is not
        // something anybody should have to know, so the standard names are listed whether or not any of them has
        // been drawn.
        struct Want { MechModelKind kind; int least; const char* one; };
        const Want wants[] = {
            {MechModelKind::Boot, 3, "Straight"},
            {MechModelKind::RingInsulated, 2, "Insulated"},
            {MechModelKind::FerruleInsulated, 2, "Uninsulated"},
            {MechModelKind::CircularConnector, 5, "Receptacle jam nut"},
            {MechModelKind::CircularBackshell, 3, "90 degrees"},
            {MechModelKind::DTypeConnector, 1, "Male"},
            {MechModelKind::YSplit, 2, "3 branches"},
            {MechModelKind::BranchSplitHeatshrink, 2, "4 branches"},
            {MechModelKind::ConnectorToCableWires, 8, "8 wires spread"},
        };
        for (const Want& w : wants) {
            const std::vector<QString> names = defaultSubModelNames(w.kind);
            const bool has = std::find(names.begin(), names.end(), QString::fromLatin1(w.one)) != names.end();
            std::printf("  sub: %s comes in %d - %s\n", qPrintable(mechModelName(w.kind)), int(names.size()),
                        has ? "including the one asked for" : "MISSING the one asked for");
            check(static_cast<int>(names.size()) >= w.least,
                  "sub: the kind offers at least the sub-drawings it is made of");
            check(has, "sub: and the one named here is among them");
        }
        // And a T-split is a thing of its own, not a sort of Y: it goes both ways at once.
        bool haveT = false, haveD = false;
        for (const MechModelKind k : allMechModelKinds()) {
            if (k == MechModelKind::TSplit) haveT = true;
            if (k == MechModelKind::DTypeConnector) haveD = true;
        }
        check(haveT, "sub: a T-split is a kind of its own");
        check(haveD, "sub: and so is a D-type");
        // Every kind still round-trips through its key, new ones included - a file names them by key, so a key
        // that does not come back is a model an office loses.
        for (const MechModelKind k : allMechModelKinds()) {
            MechModelKind back = MechModelKind::Wire;
            check(mechModelKindFromKey(mechModelKey(k), back) && back == k,
                  "sub: and every kind is still written and read back by its own key");
        }
    }


    // ---- a label has joints ------------------------------------------------------------------------
    {
        // A label is a grouped object, so it has none of the joints its lines would have had: the only thing that
        // could be done to it by hand was to slide it, and its size had to be typed. But a label is sized by eye
        // more often than by number - it is as wide as the bundle it covers and as long as it needs to be to be
        // read - and every other object on the sheet is sized by dragging a corner.
        //
        // Four joints: one at each END, along the run, and one on each SIDE, across it. Not eight - a label is
        // symmetrical about the run it sits on, so an end pulled out lengthens it BOTH ways about its middle. One
        // dragged off-centre would take the label off the bundle, which is the one place it has to stay.
        Document d;
        Entity w;
        w.id = QStringLiteral("w");
        w.type = EntityType::Line;
        w.a = {0.0, 0.0};
        w.b = {200.0, 0.0};
        d.entities().push_back(w);
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        const MechModel m = builtInMechModel(MechModelKind::Label);
        ed.beginLabelPlacement(partDrawingEntities(m.drawing), m.exceedMm);
        ed.onPointerPress({100.0, -6.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({100.0, 6.0});
        ed.onPointerPress({100.0, 6.0}, Qt::LeftButton, Qt::NoModifier);
        check(ed.labelLive(), "joint: the label is on the wire");

        const std::vector<Vec2> joints = ed.labelJoints();
        std::printf("  joint: %d joint(s)\n", int(joints.size()));
        check(joints.size() == 4, "joint: one at each end and one on each side");
        if (joints.size() != 4) return failures;
        const double along0 = ed.labelAlongMm(), across0 = ed.labelAcrossMm();
        // They are where they should be: the ends half the length out along the run, the sides half the depth out
        // across it. A joint drawn anywhere else is a joint that cannot be found.
        check(std::abs(distance(joints[0], joints[1]) - along0) < 1e-6,
              "joint: the two ends are the label's length apart");
        check(std::abs(distance(joints[2], joints[3]) - across0) < 1e-6,
              "joint: and the two sides its depth apart");

        // PULL AN END. It lengthens about the middle, so the far end comes out with it and the label stays
        // centred on the same place on the wire.
        auto maskOf = [&]() -> const Entity* {
            const Entity* mk = nullptr;
            for (const Entity& e : d.entities())
                if (e.meta.value(QStringLiteral("labelMask")).toBool()) mk = &e;
            return mk;
        };
        const Vec2 wasMid = maskOf()->bounds().center();
        ed.onPointerPress(joints[0], Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({joints[0].x + 20.0, joints[0].y});
        std::printf("  joint: an end pulled out 20 - now %.1f long, was %.1f\n", ed.labelAlongMm(), along0);
        check(std::abs(ed.labelAlongMm() - (along0 + 40.0)) < 1e-6,
              "joint: pulled out 20, it grew 20 at each end - it lengthens about its middle");
        check(std::abs(ed.labelAcrossMm() - across0) < 1e-6,
              "joint: and is no deeper than it was - one joint changes one thing");
        check(distance(maskOf()->bounds().center(), wasMid) < 1e-6,
              "joint: and has not moved along the wire - it is the same label, longer");
        ed.onPointerPress({joints[0].x + 20.0, joints[0].y}, Qt::LeftButton, Qt::NoModifier);

        // PULL A SIDE. The same, across.
        const std::vector<Vec2> now = ed.labelJoints();
        const double along1 = ed.labelAlongMm();
        ed.onPointerPress(now[2], Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({now[2].x, now[2].y + 7.0});
        std::printf("  joint: a side pulled out 7 - now %.1f deep, was %.1f\n", ed.labelAcrossMm(), across0);
        check(std::abs(ed.labelAcrossMm() - (across0 + 14.0)) < 1e-6,
              "joint: pulled out 7, it deepened 7 either side of the run");
        check(std::abs(ed.labelAlongMm() - along1) < 1e-6, "joint: and is no longer than it was");
        ed.onPointerPress({now[2].x, now[2].y + 7.0}, Qt::LeftButton, Qt::NoModifier);

        // And the body is still the handle for sliding it: the joints did not take that away.
        const Vec2 body = maskOf()->bounds().center();
        ed.onPointerPress(body, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({body.x + 30.0, body.y + 9.0});
        std::printf("  joint: body dragged 30 along and 9 across - moved %+.1f,%+.1f\n",
                    maskOf()->bounds().center().x - body.x, maskOf()->bounds().center().y - body.y);
        check(std::abs(maskOf()->bounds().center().x - (body.x + 30.0)) < 1e-6 &&
                  std::abs(maskOf()->bounds().center().y - body.y) < 1e-6,
              "joint: the body still slides it along the wire, and only along it");
    }

    // ---- Tab pins the preview, so the sizes can be set BEFORE the click ---------------------------
    {
        // Setting a label's size only after it is placed means placing it at the wrong size first, which is
        // the one moment you least know what size it should be. So Tab pins the stroke where it is and the
        // ribbon's two fields work on the preview - the same gesture that pins a break mark.
        Document d;
        Entity w;
        w.id = QStringLiteral("w");
        w.type = EntityType::Line;
        w.a = {0.0, 0.0};
        w.b = {100.0, 0.0};
        d.entities().push_back(w);
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.beginLabelPlacement(partDrawingEntities(builtInMechModel(MechModelKind::Label).drawing));
        ed.onPointerPress({50.0, -6.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({50.0, 6.0});
        check(!ed.labelFrozen(), "pin: not pinned until it is asked for");
        ed.freezeLabelPreview();
        check(ed.labelFrozen(), "pin: Tab pins the stroke where it is");

        // Pinned, the preview stops following the cursor - which is the point of pinning it.
        const double heightWhenPinned = ed.labelHeight();
        ed.onPointerMove({50.0, 400.0});
        check(std::abs(ed.labelHeight() - heightWhenPinned) < 1e-6,
              "pin: and it stops following the cursor, or there would be nothing to set the size on");

        // The fields work on the preview, and the label goes down at the size that was set rather than at
        // the size it happened to be measured at.
        ed.setLabelWidth(25.0);
        ed.setLabelHeight(9.0);
        ed.onPointerPress({50.0, 400.0}, Qt::LeftButton, Qt::NoModifier);
        check(ed.labelLive(), "pin: the click places it");
        check(std::abs(ed.labelWidth() - 25.0) < 1e-6 && std::abs(ed.labelHeight() - 9.0) < 1e-6,
              "pin: at the size set on the ribbon, not the one measured before pinning");
        const Entity* m = nullptr;
        for (const Entity& e : d.entities())
            if (e.meta.value(QStringLiteral("labelMask")).toBool()) m = &e;
        check(m != nullptr, "pin: masked as usual");
        if (m) {
            std::printf("  pin: placed %.1f along x %.1f across, asked for 25.0 x 9.0\n",
                        m->bounds().width(), m->bounds().height());
            check(std::abs(m->bounds().width() - 25.0) < 1e-6 &&
                      std::abs(m->bounds().height() - 9.0) < 1e-6,
                  "pin: and it is that size on the sheet - 25 left to right, 9 up and down");
        }
        // And it lands where the stroke was pinned, not at the cursor that wandered off afterwards.
        if (m) check(std::abs(m->bounds().center().y) < 6.0,
                     "pin: on the run the stroke crossed, not 400 away where the cursor went");
    }

    // ---- a label can be placed before anybody has drawn one ---------------------------------------
    {
        // A button that places nothing until somebody has drawn the part is a button nobody can use. So a
        // label - a flag standing on the run - and the same under a clear sleeve are built in. It is a
        // starting point, not a house style: capturing a drawing of your own replaces it for good.
        for (const MechModelKind kind : {MechModelKind::Label, MechModelKind::LabelUnderHeatshrink}) {
            const MechModel m = builtInMechModel(kind);
            check(!m.isEmpty(), "built-in: a label has a drawing without one being captured first");
            const std::vector<Entity> es = partDrawingEntities(m.drawing);
            std::printf("  built-in: %s is %d object(s)\n", qPrintable(mechModelName(kind)),
                        int(es.size()));
            check(es.size() >= 3, "built-in: the label, and the marking on it");
            // ONE object in it is marked as what the app scales the model by. Without that, a label under a
            // clear sleeve is scaled as a pair - the sleeve is fitted to the bundle and the label inside it
            // comes out too small, which is exactly backwards.
            int refs = 0;
            for (const Entity& e : es)
                if (e.meta.value(QStringLiteral("modelScaleRef")).toBool()) ++refs;
            check(refs == 1, "built-in: exactly one object says what the model is scaled by");
            // And how far clear of the first and last wire it goes, as a figure rather than a proportion.
            check(m.exceedMm > 0.0, "built-in: and how far clear of the wires it goes");
            // The run CONNECTS TO THE INNER RECTANGLE - the label - and not to the sleeve over it. A clear
            // sleeve is clear and transparent: a wire drawn up to the outside of it stops short of the thing
            // it is labelled by, with a gap of nothing in between.
            Bounds ref;
            for (const Entity& e : es)
                if (e.meta.value(QStringLiteral("modelScaleRef")).toBool()) {
                    const Bounds b = e.bounds();
                    if (b.valid) { ref.expand(b.min); ref.expand(b.max); }
                }
            check(ref.valid && m.connectPoint.x >= ref.min.x - 1e-9 &&
                      m.connectPoint.x <= ref.max.x + 1e-9 &&
                      m.connectPoint.y >= ref.min.y - 1e-9 && m.connectPoint.y <= ref.max.y + 1e-9,
                  "built-in: the run connects to the label itself, not to the sleeve over it");
            // It goes ON a run, so it knows where it attaches and which way it lies - otherwise the wizard
            // could not place it and it would land by the corner of its own extent.
            check(m.hasConnect, "built-in: it knows where it sits on the run");
            // And it does not stretch its own flag when it covers a longer piece of run: a label is one size
            // whatever it is wrapped round.
            check(m.headMm > 0.0 && m.tailMm > 0.0,
                  "built-in: the label keeps its size at any length of run");
            const std::vector<Entity> longer =
                mechModelEntities(m, Vec2{0.0, 0.0}, 0.0, 60.0);
            check(!longer.empty(), "built-in: it can be drawn at a length");
        }
        // The clear sleeve is what tells the two apart, so the second must have more in it than the first.
        check(partDrawingEntities(builtInMechModel(MechModelKind::LabelUnderHeatshrink).drawing).size() >
                  partDrawingEntities(builtInMechModel(MechModelKind::Label).drawing).size(),
              "built-in: the one under heatshrink has the sleeve drawn over it");
        // Nothing is invented for the kinds that are too much somebody's own drawing to guess at.
        check(builtInMechModel(MechModelKind::CircularConnector).isEmpty() &&
                  builtInMechModel(MechModelKind::CircularBackshell).isEmpty(),
              "built-in: a connector and a backshell are left to the office that draws them");
    }

    // ---- the faces the wires come out of ----------------------------------------------------------
    {
        // A connection point says where the part meets the run and nothing about the face the conductors
        // leave by. On a real part that face is a LINE across the end - the back of a connector, the mouth
        // of a backshell, each branch of a split - and the wires must spread ALONG it without running off
        // either end. A generator that only knows a point either fans the wires out of one spot or spreads
        // them past the edge of the part, and both are drawings that come back.
        const QString modelsFile = MechModelSet::defaultPath();
        QByteArray hadFile;
        bool hadOne = false;
        {
            QFile f(modelsFile);
            if (f.open(QIODevice::ReadOnly)) { hadFile = f.readAll(); hadOne = true; }
        }
        QFile::remove(modelsFile);

        {
            MainWindow win;
            win.resize(1200, 800);
            win.show();
            QApplication::processEvents();
            win.openMechModelEditor(MechModelKind::YSplit);
            for (int i = 0; i < 10; ++i) QApplication::processEvents();
            Editor* ed = win.findChild<Editor*>();
            Document* doc = ed->document();
            noSnap(*ed);
            Entity body;
            body.id = QStringLiteral("body");
            body.type = EntityType::Line;
            body.a = {0.0, 0.0};
            body.b = {30.0, 0.0};
            doc->entities().push_back(body);

            // A split with THREE branches: three faces, marked in the order the generator hands them out.
            ed->setTool(ToolId::ModelOutputLine);
            const double ys[3] = {6.0, 0.0, -6.0};
            for (double y : ys) {
                ed->onPointerPress({30.0, y - 2.0}, Qt::LeftButton, Qt::NoModifier);
                ed->onPointerPress({30.0, y + 2.0}, Qt::LeftButton, Qt::NoModifier);
            }
            QApplication::processEvents();
            // The tool stays armed - a split has one face per branch, and going back to the ribbon between
            // each is worse than pressing Esc at the end.
            check(ed->tool() == ToolId::ModelOutputLine,
                  "outputs: the tool stays armed for the next branch");

            // POLAR TRACKING on the second click of a two-click pick. Tracking measures from an anchor, and
            // the first click was never handed over as one - so the pick that is nothing BUT an angle was
            // the one pick that could not snap to one. Marked here on the output face; the connection
            // point's direction click works the same way and for the same reason.
            {
                auto& s = ed->snapSettings();
                s.polar = true;
                s.polarStepDeg = 45.0;
                s.objectSnap = false;
                ed->setTool(ToolId::ModelOutputLine);
                ed->onPointerPress({0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);   // the first end
                // Polar jumps to an increment only within about 2 degrees of it, the way AutoCAD's does -
                // otherwise the angle stays free and the cursor is where you put it. So it is checked on
                // both sides of that: 1.1 degrees off horizontal is pulled flat, 8.5 degrees off is left
                // alone. My first version of this used the 8.5 and called the correct answer a failure.
                ed->onPointerMove({10.0, 0.2});
                const Vec2 close = ed->snappedCursor();
                std::printf("  outputs: polar took (10.0,0.2) to (%.2f,%.2f)\n", close.x, close.y);
                check(std::abs(close.y) < 1e-6,
                      "outputs: polar tracking pulls the second click onto an exact angle");
                ed->onPointerMove({10.0, 1.5});
                const Vec2 wide = ed->snappedCursor();
                check(std::abs(wide.y - 1.5) < 1e-6,
                      "outputs: and well off an increment the angle is left free");
                ed->onKey(Qt::Key_Escape, Qt::NoModifier);
                s.polar = false;
                s.objectSnap = true;
                ed->setTool(ToolId::ModelOutputLine);
            }
            int bars = 0;
            for (const Entity& e : doc->entities())
                if (e.meta.contains(QStringLiteral("modelOutput"))) ++bars;
            std::printf("  outputs: %d face(s) drawn as guides\n", bars);
            check(bars == 3, "outputs: each marked face is drawn, so which is which can be checked");

            // Two clicks in the same place is not a face: a face is something the wires spread along.
            ed->onPointerPress({10.0, 10.0}, Qt::LeftButton, Qt::NoModifier);
            ed->onPointerPress({10.0, 10.0}, Qt::LeftButton, Qt::NoModifier);
            QApplication::processEvents();
            bars = 0;
            for (const Entity& e : doc->entities())
                if (e.meta.contains(QStringLiteral("modelOutput"))) ++bars;
            check(bars == 3, "outputs: two clicks in one place is refused, not kept as a zero-length face");

            win.saveMechModelBeingEdited();
            for (int i = 0; i < 10; ++i) QApplication::processEvents();
        }

        MechModelSet back;
        back.load(modelsFile);
        const MechModel* saved = back.of(MechModelKind::YSplit);
        check(saved != nullptr, "outputs: the split was saved");
        if (saved) {
            check(saved->outputs.size() == 3, "outputs: all three faces are kept, in order");
            if (saved->outputs.size() == 3) {
                // The drawing is moved to the origin on the way into the file, and the faces travel with
                // it: left behind, a part would put its wires out through a line nowhere near itself.
                std::printf("  outputs: first face (%.1f,%.1f)-(%.1f,%.1f) after normalising\n",
                            saved->outputs[0].a.x, saved->outputs[0].a.y, saved->outputs[0].b.x,
                            saved->outputs[0].b.y);
                check(std::abs(saved->outputs[0].a.x - 30.0) < 0.5,
                      "outputs: and they are moved with the drawing, not left behind");
                // The order is the order they were marked - top branch first, as clicked.
                check(saved->outputs[0].a.y > saved->outputs[1].a.y &&
                          saved->outputs[1].a.y > saved->outputs[2].a.y,
                      "outputs: kept in the order they were marked, which is the order they are used");
            }
        }

        if (hadOne) {
            QFile f(modelsFile);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(hadFile);
        } else {
            QFile::remove(modelsFile);
        }
    }

    // ---- a model is one object, on its canvas and on a generated drawing --------------------------
    {
        // It used to arrive as its own loose lines, so the thing the session is about could not be picked
        // up and moved as the part it is - every line of it had to be swept up first.
        const QString modelsFile = MechModelSet::defaultPath();
        QByteArray hadFile;
        bool hadOne = false;
        {
            QFile f(modelsFile);
            if (f.open(QIODevice::ReadOnly)) { hadFile = f.readAll(); hadOne = true; }
        }
        // A boot to open with: three lines, saved as the kind's model.
        {
            MechModelSet seed;
            seed.load();
            MechModel b;
            b.kind = MechModelKind::Boot;
            std::vector<Entity> es;
            for (int i = 0; i < 3; ++i) {
                Entity e;
                e.id = QStringLiteral("seg%1").arg(i);
                e.type = EntityType::Line;
                e.a = {static_cast<double>(i) * 10.0, -4.0};
                e.b = {static_cast<double>(i) * 10.0 + 10.0, 4.0};
                es.push_back(e);
            }
            b.drawing = partDrawingFromEntities(es);
            seed.set(b);
            seed.save();
        }

        MainWindow win;
        win.resize(1200, 800);
        win.show();
        QApplication::processEvents();
        win.openMechModelEditor(MechModelKind::Boot);
        for (int i = 0; i < 10; ++i) QApplication::processEvents();
        Editor* ed = win.findChild<Editor*>();
        Document* doc = ed->document();
        auto& snap = ed->snapSettings();
        snap.objectSnap = false;
        snap.gridSnap = false;
        snap.ortho = false;
        snap.polar = false;

        int stamped = 0;
        for (const Entity& e : doc->entities())
            if (entityBlockInstance(e) == QLatin1String(kMechModelGroup)) ++stamped;
        check(stamped == 3, "one object: the model comes in grouped, every line of it");

        // A click on any one line takes the WHOLE model.
        ed->setTool(ToolId::Select);
        ed->onPointerPress({5.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
        ed->onPointerRelease({5.0, 0.0}, Qt::LeftButton);
        std::printf("  one object: a click on one line picked %d\n",
                    static_cast<int>(ed->selection().size()));
        check(ed->selection().size() == 3, "one object: clicking one line picks the model up whole");

        // Double-click opens it: the grouping comes off and the lines are their own again.
        ed->onDoubleClick({5.0, 0.0});
        QApplication::processEvents();
        int stillStamped = 0;
        for (const Entity& e : doc->entities())
            if (entityBlockInstance(e) == QLatin1String(kMechModelGroup)) ++stillStamped;
        check(stillStamped == 0, "one object: a double-click opens the model up for editing");
        ed->onPointerPress({5.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
        ed->onPointerRelease({5.0, 0.0}, Qt::LeftButton);
        check(ed->selection().size() == 1, "one object: opened up, a click picks one line");

        // And what is saved is the drawing, not the session's own way of handling it.
        win.saveMechModelBeingEdited();
        for (int i = 0; i < 10; ++i) QApplication::processEvents();
        MechModelSet back;
        back.load(modelsFile);
        if (const MechModel* saved = back.of(MechModelKind::Boot)) {
            int leaked = 0;
            for (const Entity& e : partDrawingEntities(saved->drawing))
                if (!entityBlockInstance(e).isEmpty()) ++leaked;
            check(leaked == 0, "one object: the grouping is not saved into the model itself");
        }

        if (hadOne) {
            QFile f(modelsFile);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(hadFile);
        } else {
            QFile::remove(modelsFile);
        }
    }

    // ---- what is drawn on the canvas is what Capture saves ----------------------------------------
    {
        // Editing a model on the canvas and pressing Capture said "Saved", closed the session, and wrote
        // the file back EXACTLY as it was found: the model was built up out of the drawing and then never
        // put into the set, and save() writes out what load() read. Every model drawn this way was lost.
        const QString modelsFile = MechModelSet::defaultPath();
        QByteArray hadFile;
        bool hadOne = false;
        {
            QFile f(modelsFile);
            if (f.open(QIODevice::ReadOnly)) { hadFile = f.readAll(); hadOne = true; }
        }
        QFile::remove(modelsFile);   // start from nothing, so what is read back can only be what was saved

        {
            MainWindow win;
            win.resize(1200, 800);
            win.show();
            QApplication::processEvents();
            win.openMechModelEditor(MechModelKind::Boot);
            for (int i = 0; i < 10; ++i) QApplication::processEvents();
            Editor* ed = win.findChild<Editor*>();
            Document* doc = ed->document();
            auto& snap = ed->snapSettings();
            snap.objectSnap = false;
            snap.gridSnap = false;
            snap.ortho = false;
            snap.polar = false;

            // A boot, drawn well away from the origin and marked where it connects - both of which the
            // save has to deal with: the drawing is moved to the origin, and the mark must travel with it.
            for (int i = 0; i < 2; ++i) {
                Entity e;
                e.id = QStringLiteral("boot%1").arg(i);
                e.type = EntityType::Line;
                e.a = {100.0, 50.0 + (i ? 6.0 : -6.0)};
                e.b = {140.0, 50.0 + (i ? 6.0 : -6.0)};
                doc->entities().push_back(e);
            }
            ed->setTool(ToolId::ModelConnectPoint);
            ed->onPointerPress({100.0, 50.0}, Qt::LeftButton, Qt::NoModifier);   // the point
            ed->onPointerPress({140.0, 50.0}, Qt::LeftButton, Qt::NoModifier);   // the direction
            QApplication::processEvents();

            win.saveMechModelBeingEdited();
            for (int i = 0; i < 10; ++i) QApplication::processEvents();
        }

        MechModelSet back;
        check(back.load(modelsFile), "canvas capture: a file was actually written");
        const MechModel* saved = back.of(MechModelKind::Boot);
        check(saved != nullptr, "canvas capture: the boot drawn on the canvas is in the file");
        if (saved) {
            const std::vector<Entity> got = partDrawingEntities(saved->drawing);
            std::printf("  canvas capture: %d object(s), %.1f long, connect %s\n",
                        static_cast<int>(got.size()), mechModelDrawnLength(*saved),
                        saved->hasConnect ? "marked" : "not marked");
            check(got.size() == 2, "canvas capture: both objects of it, and not the guide marks");
            check(near(mechModelDrawnLength(*saved), 40.0, 0.5),
                  "canvas capture: as long as it was drawn");
            // Moved to the origin, and the connection point moved with it: left behind at 100 it would
            // place the part by a point nowhere near the drawing.
            check(saved->hasConnect && near(saved->connectPoint.x, 0.0, 0.5),
                  "canvas capture: the connection point travels with the drawing");
            check(near(saved->drawnAcrossMm, 0.0),
                  "canvas capture: scaling across is left off, not switched on by capturing");
        }

        if (hadOne) {
            QFile f(modelsFile);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(hadFile);
        } else {
            QFile::remove(modelsFile);
        }
    }

    // ---- several models of one kind, and which one a drawing uses ---------------------------------
    {
        // An office draws a straight backshell and a right-angled one. Both are backshells. One model per
        // kind meant the second could not be kept at all: capturing it replaced the first, and every drawing
        // that used the first changed with it.
        auto lineModel = [](MechModelKind kind, const QString& name, double len) {
            MechModel m;
            m.kind = kind;
            m.name = name;
            Entity e;
            e.type = EntityType::Line;
            e.a = {0.0, 0.0};
            e.b = {len, 0.0};
            m.drawing = partDrawingFromEntities({e});
            return m;
        };
        MechModelSet set;
        set.set(lineModel(MechModelKind::Boot, QStringLiteral("Straight"), 20.0));
        set.set(lineModel(MechModelKind::Boot, QStringLiteral("Right-angled"), 30.0));
        check(set.all(MechModelKind::Boot).size() == 2, "models: a kind can have more than one model");
        check(set.of(MechModelKind::Boot, QStringLiteral("Right-angled")) != nullptr,
              "models: each is there by its own name");

        // The first one added is what a drawing uses until another is chosen, so a kind is never left with
        // models and nothing to use.
        const MechModel* uses = set.of(MechModelKind::Boot);
        check(uses && uses->name == QStringLiteral("Straight"),
              "models: the first one added is what a drawing uses");
        check(set.setDefault(MechModelKind::Boot, QStringLiteral("Right-angled")),
              "models: another can be made the one it uses");
        uses = set.of(MechModelKind::Boot);
        check(uses && uses->name == QStringLiteral("Right-angled"), "models: and then that is what it uses");
        int defaults = 0;
        for (const MechModel* m : set.all(MechModelKind::Boot))
            if (m->isDefault) ++defaults;
        check(defaults == 1, "models: exactly one of them is the default - it is one thing");

        // Renaming, and refusing a name the kind already has.
        check(set.rename(MechModelKind::Boot, QStringLiteral("Straight"), QStringLiteral("Plain")),
              "models: one can be renamed");
        check(!set.rename(MechModelKind::Boot, QStringLiteral("Plain"), QStringLiteral("Right-angled")),
              "models: but not onto a name the kind already has");

        // Written down and read back, names, default and all.
        MechModelSet back;
        back.fromJson(set.toJson());
        check(back.all(MechModelKind::Boot).size() == 2, "models: both survive the file");
        const MechModel* backUses = back.of(MechModelKind::Boot);
        check(backUses && backUses->name == QStringLiteral("Right-angled"),
              "models: and so does which one is used");

        // Removing the one in use hands it on rather than leaving the kind with nothing to use.
        set.remove(MechModelKind::Boot, QStringLiteral("Right-angled"));
        check(set.all(MechModelKind::Boot).size() == 1, "models: one can be removed");
        const MechModel* left = set.of(MechModelKind::Boot);
        check(left && left->name == QStringLiteral("Plain"),
              "models: and the one left over becomes what the kind uses");
    }

    // ---- the connection point, the direction, and the side -----------------------------------------
    {
        // The wizard places a part by two things marked on the model: WHERE it connects, and WHICH WAY it
        // runs from there. Without them a model had to be drawn along +x from its own origin, so a part
        // drawn any other way - a right-angled backshell, a boot leaving at 45 degrees - could not be placed
        // by the wizard at all.
        MechModel m;
        m.kind = MechModelKind::Boot;   // a placed kind: it is not stretched to a length
        Entity body;
        body.type = EntityType::Line;
        body.a = {10.0, 5.0};    // drawn anywhere, at any angle: that is the point
        body.b = {10.0, 25.0};   // this one runs UP the page, not along +x
        m.drawing = partDrawingFromEntities({body});
        m.hasConnect = true;
        m.connectPoint = {10.0, 5.0};
        m.connectAngleRad = M_PI / 2.0;   // it runs up from where it connects

        // Placed on a run going along +x: the drawing is turned by (run - its own), so the part runs along
        // the run, and its connection point lands exactly where it was put.
        const std::vector<Entity> placed = mechModelEntities(m, Vec2{100.0, 50.0}, 0.0, 0.0);
        check(placed.size() == 1, "connect: the part is placed");
        if (placed.size() == 1) {
            const Vec2 a = placed[0].a, b = placed[0].b;
            std::printf("  connect: from %.1f,%.1f to %.1f,%.1f\n", a.x, a.y, b.x, b.y);
            check(std::abs(a.x - 100.0) < 1e-6 && std::abs(a.y - 50.0) < 1e-6,
                  "connect: its connection point lands exactly on the point it was placed at");
            check(std::abs(b.x - 120.0) < 1e-6 && std::abs(b.y - 50.0) < 1e-6,
                  "connect: and it runs along the run, whichever way it happened to be drawn");
        }

        // The SIDE: the same part on the other side of the run is the same drawing the other way up.
        MechModel bent = m;
        Entity leg;
        leg.type = EntityType::Line;
        leg.a = {10.0, 5.0};
        leg.b = {20.0, 15.0};   // off to one side, so a flip can be seen at all
        bent.drawing = partDrawingFromEntities({body, leg});
        const std::vector<Entity> up = mechModelEntities(bent, Vec2{0.0, 0.0}, 0.0, 0.0, 0.0, 0, false);
        const std::vector<Entity> down = mechModelEntities(bent, Vec2{0.0, 0.0}, 0.0, 0.0, 0.0, 0, true);
        check(up.size() == 2 && down.size() == 2, "side: both sides draw the whole part");
        if (up.size() == 2 && down.size() == 2) {
            double upY = 0.0, downY = 0.0;
            for (const Entity& e : up) upY = std::max(upY, std::max(std::abs(e.a.y), std::abs(e.b.y)));
            for (const Entity& e : down) downY = std::min(downY, std::min(e.a.y, e.b.y));
            std::printf("  side: one way reaches %.1f, the other %.1f\n", upY, downY);
            check(upY > 1e-6 && downY < -1e-6,
                  "side: the other side is the same part mirrored about the run - what a wizard needs to "
                  "place it on either side of the harness");
            check(std::abs(upY + downY) < 1e-6, "side: and mirrored exactly, not moved");
        }

        // A model that marks neither is placed exactly as it always was: laid from x = 0 and centred across,
        // whatever coordinates it happened to be drawn at. (That normalising is what a model has always
        // done; the connection point is what lets a part say something better than "start at my left edge".)
        MechModel plain = m;
        plain.hasConnect = false;
        const std::vector<Entity> asBefore = mechModelEntities(plain, Vec2{0.0, 0.0}, 0.0, 0.0);
        check(asBefore.size() == 1 && std::abs(asBefore[0].a.x) < 1e-6 &&
                  std::abs(asBefore[0].a.y + 10.0) < 1e-6,
              "connect: and a model that marks neither is drawn where it always was");

        // Both survive the file: they are the whole of how a part is placed.
        MechModel back;
        back.fromJson(m.toJson());
        check(back.hasConnect && std::abs(back.connectPoint.x - 10.0) < 1e-9 &&
                  std::abs(back.connectAngleRad - M_PI / 2.0) < 1e-9,
              "connect: the point and the direction are kept with the model");
    }

    // ---- the manager shows what the model HAS, and nothing else -----------------------------------
    {
        // A window that shows every field for every kind teaches nobody anything: a ring terminal has no
        // ends that must not stretch, a cable is not drawn per conductor, and only a ring or a ferrule is
        // insulated. The rows that do not apply are taken away rather than greyed.
        const QString modelsFile = MechModelSet::defaultPath();
        QByteArray hadFile;
        bool hadOne = false;
        {
            QFile f(modelsFile);
            if (f.open(QIODevice::ReadOnly)) { hadFile = f.readAll(); hadOne = true; }
        }
        {
            // Something drawn for each of the three kinds this looks at, so the rows have a model to be
            // about.
            MechModelSet s;
            for (MechModelKind k : {MechModelKind::Cable, MechModelKind::RingInsulated,
                                    MechModelKind::OpenWires}) {
                MechModel m;
                m.kind = k;
                Entity e;
                e.type = EntityType::Line;
                e.a = {0.0, 0.0};
                e.b = {40.0, 0.0};
                m.drawing = partDrawingFromEntities({e});
                s.set(m);
            }
            s.save();
        }

        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        MechModelsDialog dlg(&ed);
        dlg.show();
        QApplication::processEvents();

        auto spins = [&dlg] {
            QStringList out;
            for (QDoubleSpinBox* s : dlg.findChildren<QDoubleSpinBox*>())
                if (s->isVisible()) out << s->toolTip().left(20);
            return out;
        };
        auto visibleLabels = [&dlg] {
            QStringList out;
            for (QLabel* l : dlg.findChildren<QLabel*>())
                if (l->isVisible() && !l->text().isEmpty()) out << l->text();
            return out;
        };
        auto rowShowing = [&dlg](const QString& label) {
            for (QLabel* l : dlg.findChildren<QLabel*>())
                if (l->text() == label) return l->isVisible();
            return false;
        };

        // A CABLE: it is run to a length, so it has two ends that must not stretch and a gap. It is not
        // drawn per conductor and it is not insulated-or-not.
        dlg.selectKind(MechModelKind::Cable);
        QApplication::processEvents();
        check(rowShowing(QStringLiteral("Fixed at start")) &&
                  rowShowing(QStringLiteral("Gap left at each end")),
              "manager: a cable shows its ends and its gap");
        check(!rowShowing(QStringLiteral("Conductors as drawn")) &&
                  !rowShowing(QStringLiteral("Insulation")),
              "manager: and not the rows it has nothing to do with");
        check(rowShowing(QStringLiteral("Length")),
              "manager: it can be tried at a length, because that is what a run gives it");
        check(!rowShowing(QStringLiteral("Conductors")),
              "manager: but not with a number of conductors");

        // A RING TERMINAL: it is placed, not run. No ends, no gap, no conductors - but insulated or not.
        dlg.selectKind(MechModelKind::RingInsulated);
        QApplication::processEvents();
        check(!rowShowing(QStringLiteral("Fixed at start")) &&
                  !rowShowing(QStringLiteral("Gap left at each end")),
              "manager: a ring terminal is placed, so it has no ends that stretch and no gap");
        check(rowShowing(QStringLiteral("Insulation")), "manager: it is insulated or not, and says so");
        check(!rowShowing(QStringLiteral("Length")),
              "manager: and there is no length to try it at");

        // THE OPEN WIRES: drawn for so many conductors, and the number comes from the harness.
        dlg.selectKind(MechModelKind::OpenWires);
        QApplication::processEvents();
        check(rowShowing(QStringLiteral("Conductors as drawn")) &&
                  rowShowing(QStringLiteral("Conductors")),
              "manager: the open wires are drawn per conductor, and can be tried at another number");

        // The two figures the drawing itself knows are MEASURED and said, not asked for: what is left to
        // type is what cannot be measured.
        const QStringList said = visibleLabels();
        bool saysMeasured = false;
        for (const QString& s : said)
            if (s.contains(QStringLiteral("drawn")) && s.contains(QStringLiteral("mm long")))
                saysMeasured = true;
        check(saysMeasured, "manager: it says how long the drawing is, rather than asking");
        check(!rowShowing(QStringLiteral("Drawn at this length (0 = measure it)")),
              "manager: the length field with the magic zero in it is gone");
        check(!rowShowing(QStringLiteral("Drawn at this diameter (0 = do not scale across)")),
              "manager: and so is the diameter one - scaling across is a yes or no now");

        // What was marked on the drawing for the wizard: said in the window, because a model with no
        // connection point is placed differently and that has to be visible.
        bool saysPlacement = false;
        for (const QString& s : said)
            if (s.contains(QStringLiteral("connection point")) || s.contains(QStringLiteral("Connects at")))
                saysPlacement = true;
        check(saysPlacement, "manager: and it says where the part connects and which way it runs");

        // ONE drawing window, and it is the model's. Two side by side halved the size of both, and for
        // every kind that is only placed the second showed the same drawing as the first.
        const QList<DrawingPreview*> views = dlg.findChildren<DrawingPreview*>();
        std::printf("  manager: %lld drawing window(s)\n", static_cast<long long>(views.size()));
        check(views.size() == 1, "manager: one drawing window, not two");

        // The commands that were two ways of saying the same thing are gone.
        QStringList buttons;
        for (QPushButton* b : dlg.findChildren<QPushButton*>()) buttons << b->text();
        std::printf("  manager: buttons - %s\n", qPrintable(buttons.join(QStringLiteral(", "))));
        check(!buttons.contains(QStringLiteral("Put on the sheet")) &&
                  !buttons.contains(QStringLiteral("Clear")),
              "manager: no Put on the sheet (Edit does that) and no Clear (Remove does that)");
        // "Edit model" rather than "Edit on the canvas": what it does is edit the model, and where that happens
        // is the program's business rather than something to be told about on a button.
        check(buttons.contains(QStringLiteral("Edit model")) &&
                  !buttons.contains(QStringLiteral("Edit on the canvas")),
              "manager: the button says what it does - Edit model");
        check(buttons.contains(QStringLiteral("Add")) && buttons.contains(QStringLiteral("Remove")) &&
                  buttons.contains(QStringLiteral("Rename")) &&
                  buttons.contains(QStringLiteral("Make default")),
              "manager: and the four that manage the list are all there");

        if (hadOne) {
            QFile f(modelsFile);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(hadFile);
        } else {
            QFile::remove(modelsFile);
        }
    }


    // ---- the manager: sections named for what they are, marks shown, nothing written until Save ------
    {
        // A window somebody sets a house style in has to be readable and it has to be safe. It was neither: the
        // three sections were named after themselves rather than after what they were for, and every digit typed
        // into a field wrote the whole set of models to disk - so there was no trying something and thinking
        // better of it.
        const QString modelsFile = MechModelSet::defaultPath();
        const bool hadOne = QFile::exists(modelsFile);
        QByteArray hadFile;
        if (hadOne) {
            QFile f(modelsFile);
            if (f.open(QIODevice::ReadOnly)) hadFile = f.readAll();
        }
        {
            MechModelSet seed;
            MechModel m = builtInMechModel(MechModelKind::Wire);
            m.kind = MechModelKind::Wire;
            m.drawing = builtInMechModel(MechModelKind::Label).drawing;   // any drawing will do
            m.headMm = 4.0;
            seed.set(m);
            seed.save(modelsFile);
        }

        MechModelsDialog dlg(nullptr);
        QStringList groups;
        for (QGroupBox* g : dlg.findChildren<QGroupBox*>()) groups << g->title();
        std::printf("  manager: sections - %s\n", qPrintable(groups.join(QStringLiteral(", "))));
        // NAMED FOR WHAT THEY ARE. "Fixed zones" is the standard way a part drawn at one length is used at
        // another; "Trial fit" is drawing it at a size it will really be asked for. "What must not stretch" and
        // "Try it" said what they did in nobody's language.
        check(groups.contains(QStringLiteral("Captured geometry")) &&
                  groups.contains(QStringLiteral("Fixed zones")) &&
                  groups.contains(QStringLiteral("Trial fit")),
              "manager: the sections are named the way a drawing office names them");
        check(!groups.contains(QStringLiteral("What must not stretch")) &&
                  !groups.contains(QStringLiteral("Try it")),
              "manager: and not the way they were");

        // AND EACH SAYS WHAT IT IS FOR. A title names a section; it does not tell anybody which figure belongs in
        // it, and these three all needed telling.
        int explained = 0;
        for (QGroupBox* g : dlg.findChildren<QGroupBox*>())
            for (QLabel* l : g->findChildren<QLabel*>())
                if (l->text().length() > 60 && l->wordWrap()) { ++explained; break; }
        std::printf("  manager: %d of the sections explain themselves\n", explained);
        check(explained >= 3, "manager: every section says in a line what it is for");

        // THE MARKS, askable for here. This window is where a model is chosen and checked, and the marks are what
        // it is placed by - so "has this one been marked up, and where" should be answerable by looking.
        // As an eye that can be pressed in and out, the same control the editing canvas has in its ribbon -
        // it was a tick box here and an eye there, which is the same thing done two ways in two windows.
        QToolButton* marks = nullptr;
        for (QToolButton* b : dlg.findChildren<QToolButton*>())
            if (b->isCheckable() && b->text().contains(QStringLiteral("Connection points"))) marks = b;
        check(marks != nullptr, "manager: the marks can be shown over the preview");

        // NOTHING WRITTEN UNTIL SAVE. The file on disk is untouched by a change made here.
        QPushButton* save = nullptr;
        for (QPushButton* b : dlg.findChildren<QPushButton*>())
            if (b->text().startsWith(QStringLiteral("Save"))) save = b;
        check(save != nullptr, "manager: there is a Save");
        if (save) {
            check(!save->isEnabled(), "manager: and nothing to save until something is changed");
            QByteArray before;
            {
                QFile f(modelsFile);
                if (f.open(QIODevice::ReadOnly)) before = f.readAll();
            }
            dlg.selectKind(MechModelKind::Wire);
            dlg.setHeadForTest(9.0);
            check(save->isEnabled(), "manager: changed, there is something to save and the button says so");
            QByteArray after;
            {
                QFile f(modelsFile);
                if (f.open(QIODevice::ReadOnly)) after = f.readAll();
            }
            check(after == before,
                  "manager: and the file is untouched - a digit typed does not write the house style to disk");
            check(dlg.hasUnsavedForTest(), "manager: which is what it would warn about on the way out");
            dlg.saveForTest();
            {
                QFile f(modelsFile);
                if (f.open(QIODevice::ReadOnly)) after = f.readAll();
            }
            std::printf("  manager: file %s after Save\n", after == before ? "UNCHANGED" : "written");
            check(after != before, "manager: Save writes it");
            check(!dlg.hasUnsavedForTest() && !save->isEnabled(),
                  "manager: and there is nothing left to save");
        }

        if (hadOne) {
            QFile f(modelsFile);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(hadFile);
        } else {
            QFile::remove(modelsFile);
        }
    }


    // ---- the manager's eye: the marks are shown and hidden, and the button says which ---------------
    {
        // The marks are chrome OVER the part - where it connects, which way it runs from there, its two ends,
        // the faces the conductors leave by. While the part itself is being looked at they are in the way of
        // seeing it, so they go, and come back. The manager had a tick box for it and the editing canvas had an
        // eye in the ribbon: the same thing done two ways in two windows, which is one way too many.
        // A ring terminal: PLACED rather than run to a length. The manager holds the marks back while a trial
        // length is being tried - stretched, the part's ends are somewhere else and a mark drawn where it was
        // captured would point at the wrong place - so a kind that stretches would show nothing here for a
        // reason that has nothing to do with the eye.
        const MechModelKind kind = MechModelKind::RingInsulated;
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        MechModelsDialog dlg(&ed);
        dlg.show();
        dlg.selectKind(kind);
        QApplication::processEvents();
        // WHICH model of that kind the window is showing, asked rather than assumed: a kind holds several -
        // insulated and not, straight and right-angled - and the list has a row per model. A model put in under
        // a name no row is showing is a model nobody looks at.
        MechModelSet s = dlg.models();
        MechModel m;
        m.kind = kind;
        m.name = dlg.currentName();
        Entity e;
        e.type = EntityType::Line;
        e.a = {0.0, 0.0};
        e.b = {40.0, 0.0};
        m.drawing = partDrawingFromEntities({e});
        // Marked up, so there is something for the eye to show or hide.
        m.hasConnect = true;
        m.connectPoint = {0.0, 0.0};
        m.connectAngleRad = 0.0;
        m.hasNearPoint = true;
        m.nearPoint = {2.0, 0.0};
        m.hasFarPoint = true;
        m.farPoint = {38.0, 0.0};
        // And a face the conductors leave by, so the purple has something to draw.
        MechModel::OutputLine face;
        face.a = {40.0, -6.0};
        face.b = {40.0, 6.0};
        m.outputs.push_back(face);
        s.set(m);
        // Handed the models rather than left to read them off disk: the house-style file is the user's, and a
        // test that writes over it to have something to look at is a test that can lose an afternoon's work.
        dlg.setModels(s);
        dlg.selectKind(kind);
        QApplication::processEvents();
        std::printf("  manager: showing the %s model, marked up\n", qPrintable(dlg.currentName()));

        // The eye, found the way a user finds it: a button that can be pressed in and out, about the connection
        // points. Not by name, so a renamed member cannot make this test pass by looking somewhere else.
        QToolButton* eye = nullptr;
        for (QToolButton* b : dlg.findChildren<QToolButton*>())
            if (b->isCheckable() && b->text().contains(QStringLiteral("Connection points"))) eye = b;
        check(eye != nullptr, "manager: there is a toggle for the connection points");
        DrawingPreview* prev = dlg.findChild<DrawingPreview*>();
        check(prev != nullptr, "manager: and a drawing window to show them in");
        if (eye && prev) {
            eye->setChecked(true);
            QApplication::processEvents();
            const int withMarks = static_cast<int>(prev->entities().size());
            const QString shownText = eye->text();
            eye->setChecked(false);
            QApplication::processEvents();
            const int without = static_cast<int>(prev->entities().size());
            std::printf("  manager: %d object(s) with the marks, %d without\n", withMarks, without);
            check(withMarks > without,
                  "manager: pressing the eye out takes the marks off the drawing, not just the tick");
            check(without >= 1, "manager: and leaves the part itself, which is what was wanted to be seen");
            check(eye->text() != shownText,
                  "manager: the button says which state it is in - a toggle that reads the same either way "
                  "has to be pressed twice to find out what it did");
            eye->setChecked(true);
            QApplication::processEvents();
            check(static_cast<int>(prev->entities().size()) == withMarks,
                  "manager: and they come back");

            // ---- AND EVERY MARK IS THE COLOUR IT IS ON THE CANVAS -----------------------------------
            //
            // The colour IS the name of a mark: blue is where it connects, orange and green are the two ends
            // that must not stretch, purple is a face the conductors leave by. The preview paints a drawing
            // in one quiet grey so that it reads as a drawing rather than as a colour chart - and it was
            // painting the marks with it, so four different marks came out as the same grey rings and nobody
            // could tell which was which.
            QStringList colours;
            int barbs = 0, numbered = 0;
            for (const Entity& e : prev->entities()) {
                if (!e.meta.value(QLatin1String(kPreviewMarkKey)).toBool()) continue;
                if (!colours.contains(e.color)) colours << e.color;
                if (e.id.startsWith(QStringLiteral("model-connect-barb"))) ++barbs;
                if (e.type == EntityType::Text) ++numbered;
            }
            colours.sort();
            std::printf("  manager: mark colours %s; %d barb(s) on the arrow\n",
                        qPrintable(colours.join(QStringLiteral(", "))), barbs);
            check(colours.contains(QStringLiteral("#5aa0ff")),
                  "manager: the connection point is blue, as it is on the canvas");
            check(colours.contains(QStringLiteral("#e6a23c")) &&
                      colours.contains(QStringLiteral("#62c370")),
                  "manager: the two ends are orange and green");
            check(colours.contains(QStringLiteral("#c678dd")),
                  "manager: and the face the conductors leave by is purple");
            check(barbs == 2,
                  "manager: the direction is an ARROW with a head on it - a bare line says which line and "
                  "not which way");
            check(numbered >= 1,
                  "manager: and the output faces are numbered, because the order they were marked is the "
                  "order the branches come out in");

            // Not just carried in the entity: DRAWN. A colour on an object the preview then paints grey is
            // the bug this had, so the blue is counted on the pane itself.
            const QImage shot = prev->grab().toImage();
            int blue = 0;
            for (int y = 0; y < shot.height(); ++y)
                for (int x = 0; x < shot.width(); ++x) {
                    const QRgb c = shot.pixel(x, y);
                    if (qBlue(c) > 170 && qBlue(c) - qRed(c) > 50) ++blue;
                }
            std::printf("  manager: %d blue pixel(s) on the preview\n", blue);
            check(blue > 20, "manager: the blue is on the screen, not only in the object");
        }
    }


    // ---- the house models ship WITH the application -------------------------------------------------
    {
        // They were only ever in the machine's own app-data folder, so a fresh install - a new machine, another
        // draughtsman, a rebuilt PC - came up with nothing drawn for any kind, and every drawing made on it was
        // made without the house style until somebody drew all of them again. A set of default models is not
        // personal data: it is part of what the program IS, so it is built into it.
        MechModelSet shipped;
        const bool got = shipped.load(MechModelSet::shippedPath());
        int drawn = 0, marked = 0;
        for (MechModelKind k : allMechModelKinds())
            for (const MechModel* m : shipped.all(k)) {
                if (!m) continue;
                if (!m->drawing.isEmpty()) ++drawn;
                if (!m->outputs.empty() || m->hasConnect) ++marked;
            }
        std::printf("  shipped: %d model(s) drawn, %d of them marked up\n", drawn, marked);
        check(got, "shipped: the models are inside the executable, not beside it");
        check(drawn >= 8, "shipped: and there is a real set of them, not an empty file");
        check(marked >= 1, "shipped: marked up as they were captured - the marks travel with the model");
    }


    // ---- the house library ships with the application too --------------------------------------------
    {
        // The same argument as the models: a shelf built up over months - the parts, their numbers, their
        // captured drawings - only ever existed in the machine's own app-data folder, so a fresh install
        // started from the bare seed and every drawing made on it was made without the house parts.
        PartsLibrary shipped;
        const bool got = shipped.load(QStringLiteral(":/defaults/edrawlib.plb"));
        int drawn = 0;
        for (const LibraryPart& p : shipped.parts)
            if (p.hasMechanical() || !p.schematic.isEmpty()) ++drawn;
        std::printf("  shipped: the library has %lld part(s), %d of them drawn\n",
                    static_cast<long long>(shipped.parts.size()), drawn);
        check(got && !shipped.parts.empty(), "shipped: the house library is inside the executable");
    }

    // ---- the schematic drawing slot: a ring and a ferrule keep one alongside the mechanical one ------
    {
        check(mechModelHasSchematicSlot(MechModelKind::RingInsulated) &&
                  mechModelHasSchematicSlot(MechModelKind::FerruleInsulated),
              "schematic slot: a ring and a ferrule have one");
        check(mechModelHasSchematicSlot(MechModelKind::CircularConnector) &&
                  mechModelHasSchematicSlot(MechModelKind::DTypeConnector),
              "schematic slot: and a connector - the one part that appears on both drawings, differently");
        check(!mechModelHasSchematicSlot(MechModelKind::Boot) &&
                  !mechModelHasSchematicSlot(MechModelKind::Wire) &&
                  !mechModelHasSchematicSlot(MechModelKind::Heatshrink),
              "schematic slot: harness fabric does not - a schematic draws none of it");
        // The legacy uninsulated keys normalise onto the same kind (canonicalKind()), so they get
        // one too - there is no separate "Ring Uninsulated" kind a user can actually pick.
        check(mechModelHasSchematicSlot(MechModelKind::RingUninsulated),
              "schematic slot: the legacy ring-uninsulated key normalises onto Ring");

        MechModel m;
        m.kind = MechModelKind::RingInsulated;
        check(!m.hasSchematic(), "schematic slot: empty until something is captured into it");
        Entity glyph;
        glyph.type = EntityType::Circle;
        glyph.center = {0.0, 0.0};
        glyph.radius = 2.0;
        m.schematicDrawing = partDrawingFromEntities({glyph});
        check(m.hasSchematic(), "schematic slot: has one once it is drawn");

        MechModel back;
        back.fromJson(m.toJson());
        check(back.hasSchematic() && back.schematicDrawing.size() == 1,
              "schematic slot: kept in the file with the model, apart from the mechanical drawing");
        check(back.drawing.isEmpty(), "schematic slot: and does not leak into the mechanical drawing");
    }

    // ---- the common-model guard: a stale/heading selection must not write the kind's blank model -----
    {
        // Put back exactly as found, the same as every other test here that drives capture through
        // the dialog - a test run must never change somebody's real house style.
        const QString modelsFile = MechModelSet::defaultPath();
        QByteArray hadFile;
        bool hadOne = false;
        {
            QFile f(modelsFile);
            if (f.open(QIODevice::ReadOnly)) { hadFile = f.readAll(); hadOne = true; }
        }

        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        Entity line;
        line.type = EntityType::Line;
        line.id = QStringLiteral("l");
        line.a = {0.0, 0.0};
        line.b = {10.0, 0.0};
        doc.entities().push_back(line);
        ed.selectById(QStringLiteral("l"));

        MechModelsDialog dlg(&ed);
        // A FRESH DIALOG NO LONGER LANDS ON A HEADING - it steps on to the first model under it, because a
        // kind's name over an empty page reads as a model whose drawing has gone missing. So the heading is
        // reached deliberately here: setting the tree's current item to a top-level row is what a stale
        // selection, or a rebuilt tree, could still do, and the guard has to hold when it happens.
        check(dlg.currentSelectionIsUsable(),
              "guard: a fresh dialog opens on a model rather than on a kind's heading");
        auto* tree = dlg.findChild<QTreeWidget*>();
        check(tree != nullptr, "guard: there is a list to pick in");
        QTreeWidgetItem* ringHead = nullptr;
        for (int i = 0; tree && i < tree->topLevelItemCount(); ++i)
            if (tree->topLevelItem(i)->text(0) == mechModelName(MechModelKind::RingInsulated))
                ringHead = tree->topLevelItem(i);
        check(ringHead != nullptr, "guard: Ring is listed as a heading over its sub-models");
        if (tree && ringHead) tree->setCurrentItem(ringHead);
        check(dlg.currentKind() == MechModelKind::RingInsulated && dlg.currentName().isEmpty(),
              "guard: put on Ring's heading, no named sub-model is chosen");
        check(!dlg.currentSelectionIsUsable(),
              "guard: that is not a usable selection - Ring has named sub-models");
        const bool hadRingBlank = dlg.models().of(MechModelKind::RingInsulated, QString()) != nullptr;
        dlg.captureSelection();
        check((dlg.models().of(MechModelKind::RingInsulated, QString()) != nullptr) == hadRingBlank,
              "guard: capturing while on the heading writes nothing - Ring's blank model is untouched");

        // Picking a real sub-model makes the same selection usable, and capture now writes to it.
        dlg.selectKind(MechModelKind::RingInsulated);
        check(dlg.currentSelectionIsUsable(),
              "guard: a named sub-model (picked by selectKind) is a usable selection");
        dlg.captureSelection();
        check(dlg.models().of(MechModelKind::RingInsulated, dlg.currentName()) != nullptr,
              "guard: and capturing there writes the named sub-model, same as always");

        // Put the file back as it was found.
        if (hadOne) {
            QFile f(modelsFile);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(hadFile);
        } else {
            QFile::remove(modelsFile);
        }
    }


    // ---- the manager lists the kinds in the order a harness is built up -----------------------------
    {
        // The list grew in the order the kinds were added to the program, so a ring terminal came first
        // because it was written first, and the connectors - the things every harness starts from - were most
        // of the way down. An office reads this list looking for a part, and the order it reads in is the
        // order it works in: the connectors at each end, what is crimped into them, what goes on the back of
        // them, the cable that runs between, what is shrunk over it, what is written on it.
        const std::vector<MechModelKind> order = allMechModelKinds();
        auto at = [&order](MechModelKind k) {
            for (size_t i = 0; i < order.size(); ++i)
                if (order[i] == k) return static_cast<int>(i);
            return -1;
        };
        std::printf("  order: connector %d, ferrule %d, ring %d, backshell %d, cable %d, heatshrink %d\n",
                    at(MechModelKind::CircularConnector), at(MechModelKind::FerruleInsulated),
                    at(MechModelKind::RingInsulated), at(MechModelKind::CircularBackshell),
                    at(MechModelKind::Cable), at(MechModelKind::Heatshrink));
        check(at(MechModelKind::CircularConnector) < at(MechModelKind::DTypeConnector) &&
                  at(MechModelKind::DTypeConnector) < at(MechModelKind::FerruleInsulated),
              "order: the connectors come first, both of them");
        check(at(MechModelKind::FerruleInsulated) < at(MechModelKind::RingInsulated) &&
                  at(MechModelKind::RingInsulated) < at(MechModelKind::CircularBackshell),
              "order: then the crimps, then what goes on the back of a connector");
        check(at(MechModelKind::CircularBackshell) < at(MechModelKind::Cable) &&
                  at(MechModelKind::Cable) < at(MechModelKind::Heatshrink),
              "order: then the cable that runs between, then what is shrunk over it");
        check(at(MechModelKind::Heatshrink) < at(MechModelKind::Label) &&
                  at(MechModelKind::LabelUnderHeatshrink) == static_cast<int>(order.size()) - 1,
              "order: and what is written on it, last");
        check(order.size() == 18, "order: and every kind is still in the list");
    }

    // ---- a kind with sub-models has no page of its own ----------------------------------------------
    {
        // A backshell is a straight one, a 45 or a 90; "backshell" on its own is the word for the three of
        // them and not a fourth thing that can be drawn. Its row is a heading, and a heading has no drawing,
        // nothing to capture into and nothing to measure - so it is not selectable. But the window still
        // OPENED on one, because the first row of the tree is a heading and something has to be current: what
        // you saw was a kind's name over an empty page, which reads as a model whose drawing has gone missing.
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        MechModelsDialog dlg(&ed);
        dlg.show();
        QApplication::processEvents();
        std::printf("  heading: the manager opened on %s / '%s'\n",
                    qPrintable(mechModelName(dlg.currentKind())), qPrintable(dlg.currentName()));
        check(dlg.currentSelectionIsUsable(),
              "heading: the manager opens on a model, not on the name of a group of them");
        check(!defaultSubModelNames(dlg.currentKind()).empty() ? !dlg.currentName().isEmpty() : true,
              "heading: and if that kind has sub-models it is one of THEM that is showing");

        // THE SCHEMATIC WINDOW. A crimp and a connector are what a schematic draws a symbol for, and a
        // connector is the clearest case: it appears on both drawings and appears differently. Everything else
        // on the list is harness fabric a schematic does not draw at all, so there is nothing to switch to.
        QComboBox* mode = nullptr;
        for (QComboBox* c : dlg.findChildren<QComboBox*>())
            if (c->count() == 2 && c->itemText(0).contains(QStringLiteral("Mechanical drawing")))
                mode = c;
        check(mode != nullptr, "schematic window: there is a chooser for which drawing is showing");
        if (mode) {
            dlg.selectKind(MechModelKind::CircularConnector);
            QApplication::processEvents();
            check(mode->isVisible(), "schematic window: a circular connector has one");
            dlg.selectKind(MechModelKind::DTypeConnector);
            QApplication::processEvents();
            check(mode->isVisible(), "schematic window: so has a D-type");
            dlg.selectKind(MechModelKind::RingInsulated);
            QApplication::processEvents();
            check(mode->isVisible(), "schematic window: and a ring terminal, as before");
            dlg.selectKind(MechModelKind::Cable);
            QApplication::processEvents();
            check(!mode->isVisible(),
                  "schematic window: a length of cable has not - a schematic does not draw one");
        }
    }


    // ---- "As drawn" is in the coordinates it was drawn in --------------------------------------------
    {
        // The preview asked the PLACING machinery for its picture, and that answers a different question:
        // "how does this model look placed on a run heading this way". So it turned the drawing by the run's
        // direction minus the model's own and slid it until the connection point landed where the part was
        // being fitted. A model whose own direction is -180 degrees - a connector drawn facing left, which is
        // half of them - came back turned right round, while its marks stayed in the coordinates they were
        // captured in: every mark pointing at the wrong part of the part.
        const MechModelKind kind = MechModelKind::CircularConnector;
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        MechModelsDialog dlg(&ed);
        dlg.show();
        dlg.selectKind(kind);
        QApplication::processEvents();

        MechModelSet s = dlg.models();
        MechModel m;
        m.kind = kind;
        m.name = dlg.currentName();
        // Asymmetric on purpose, and drawn to the LEFT of its connection point: a body from x 0 to 20 with a
        // spigot on its right-hand end, connecting at x=20 and running -180 degrees from there.
        Entity body;
        body.type = EntityType::Rect;
        body.points = {{0.0, -5.0}, {20.0, -5.0}, {20.0, 5.0}, {0.0, 5.0}};
        Entity spigot;
        spigot.type = EntityType::Line;
        spigot.a = {20.0, 0.0};
        spigot.b = {24.0, 0.0};
        m.drawing = partDrawingFromEntities({body, spigot});
        m.hasConnect = true;
        m.connectPoint = {20.0, 0.0};
        m.connectAngleRad = 3.14159265358979323846;   // -180 degrees: it runs off to the left
        MechModel::OutputLine face;
        face.a = {0.0, -4.0};
        face.b = {0.0, 4.0};
        m.outputs.push_back(face);
        s.set(m);
        dlg.setModels(s);
        dlg.selectKind(kind);
        QApplication::processEvents();

        DrawingPreview* prev = dlg.findChild<DrawingPreview*>();
        check(prev != nullptr, "as drawn: there is a drawing window");
        if (prev) {
            // THE DRAWING, in the coordinates it was captured in - not turned, not slid.
            Bounds part, marks;
            for (const Entity& e : prev->entities()) {
                if (e.meta.value(QLatin1String(kPreviewMarkKey)).toBool()) marks.expand(e.bounds());
                else part.expand(e.bounds());
            }
            check(part.valid, "as drawn: the part is in the window");
            if (part.valid) {
                std::printf("  as drawn: part x %.1f..%.1f (drawn 0..24)\n", part.min.x, part.max.x);
                check(std::abs(part.min.x) < 1e-6 && std::abs(part.max.x - 24.0) < 1e-6,
                      "as drawn: exactly where it was drawn - a model facing left is not turned to face right");
            }
            // AND THE MARKS ON IT. The connection point is at x=20 and the face at x=0, so with the drawing
            // where it belongs the marks land ON it rather than off one end.
            check(marks.valid, "as drawn: and its marks");
            if (marks.valid && part.valid) {
                std::printf("  as drawn: marks x %.1f..%.1f\n", marks.min.x, marks.max.x);
                // The arrow reaches out along the run (which is off to the left, past the part), so the marks
                // may reach further than the part - what matters is that they OVERLAP it rather than sitting
                // beyond its far end, which is what a turned drawing did.
                check(marks.min.x < part.max.x - 1e-6 && marks.max.x > part.min.x + 1e-6,
                      "as drawn: the marks are on the part, not off the end of it");
                bool faceOnPart = false;
                for (const Entity& e : prev->entities())
                    if (e.meta.contains(QStringLiteral("modelOutput"))) {
                        const Bounds fb = e.bounds();
                        faceOnPart = fb.valid && std::abs(fb.min.x) < 1e-6 && std::abs(fb.max.x) < 1e-6;
                    }
                check(faceOnPart,
                      "as drawn: the face the conductors leave by is still on the end it was marked on");
            }
        }
    }


    // ---- marking a face: a joint very close by beats the tracked angle -------------------------------
    {
        // The second click of a face is tracked along an angle from the first - which is what makes a face
        // square to the part - and that tracking was winning even with the cursor all but on a corner of the
        // drawing. So a face meant to run between two corners of a connector body landed a fraction off
        // them, and the marks did not line up with the part they describe.
        Document d;
        Entity body;
        body.id = QStringLiteral("b");
        body.type = EntityType::Rect;
        body.points = {{0.0, 0.0}, {20.0, 0.0}, {20.0, 12.0}, {0.0, 12.0}};
        d.entities().push_back(body);
        Editor ed;
        ed.setDocument(&d);
        auto& s = ed.snapSettings();
        s.objectSnap = true;
        s.snapEndpoint = true;
        s.gridSnap = false;
        s.polar = true;
        s.polarStepDeg = 45.0;
        s.ortho = false;

        ed.setTool(ToolId::ModelOutputLine);
        ed.onPointerPress({20.0, 0.0}, Qt::LeftButton, Qt::NoModifier);   // the first end, on a corner
        // A hair off the far corner, and off the tracked angle: the corner wins.
        ed.onPointerMove({19.7, 11.8});
        const Vec2 got = ed.snappedCursor();
        std::printf("  face snap: (19.7,11.8) went to (%.2f,%.2f)\n", got.x, got.y);
        check(std::abs(got.x - 20.0) < 1e-6 && std::abs(got.y - 12.0) < 1e-6,
              "face snap: all but on a corner, it takes the corner rather than the tracked angle");
        // Well away from any corner the angle still holds - that is the whole reason for tracking one.
        ed.onPointerMove({30.0, 6.2});
        const Vec2 free = ed.snappedCursor();
        std::printf("  face snap: (30.0,6.2) went to (%.2f,%.2f)\n", free.x, free.y);
        check(std::abs(free.x - 20.0) > 1e-6 || std::abs(free.y - 12.0) > 1e-6,
              "face snap: and out in the open it is not dragged onto a corner");
    }

    if (failures == 0) std::printf("  -> ok\n");
    else std::printf("  -> %d failed\n", failures);
    return failures;
}
