// The default mechanical models: the standard pieces a harness drawing is made of, drawn once by an
// office and used everywhere. A model is not a block because LENGTH IS NOT PART OF IT: a wire is drawn
// at whatever length suits, and the harness says how long the wire is. So each model carries the two
// ends that must NOT stretch, and everything between them makes up the difference. That is the whole
// difficulty with scaling a drawing, so it is measured here rather than eyeballed.
#include "harness/MechModels.h"
#include "model/Document.h"
#include "app/MechModelsDialog.h"
#include "app/Editor.h"
#include "harness/PartsLibrary.h"

#include <QDir>

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
    check(allMechModelKinds().size() == 11, "insulated variants share one model kind");

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
        check(dlg.models().of(MechModelKind::Wire) == nullptr,
              "capture: and nothing has been taken yet");

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
    }

    if (failures == 0) std::printf("  -> ok\n");
    else std::printf("  -> %d failed\n", failures);
    return failures;
}
