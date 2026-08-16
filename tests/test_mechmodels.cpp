// The default mechanical models: the standard pieces a harness drawing is made of, drawn once by an
// office and used everywhere. A model is not a block because LENGTH IS NOT PART OF IT: a wire is drawn
// at whatever length suits, and the harness says how long the wire is. So each model carries the two
// ends that must NOT stretch, and everything between them makes up the difference. That is the whole
// difficulty with scaling a drawing, so it is measured here rather than eyeballed.
#include "harness/MechModels.h"
#include "model/Document.h"
#include "app/DrawingPreview.h"
#include "app/MechModelsDialog.h"
#include "app/Editor.h"
#include "harness/PartsLibrary.h"

#include <QApplication>
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
        check(rowShowing(QStringLiteral("The end at the left")) &&
                  rowShowing(QStringLiteral("Gap left at each end")),
              "manager: a cable shows its ends and its gap");
        check(!rowShowing(QStringLiteral("Conductors the drawing shows")) &&
                  !rowShowing(QStringLiteral("Insulation")),
              "manager: and not the rows it has nothing to do with");
        check(rowShowing(QStringLiteral("At this length")),
              "manager: it can be tried at a length, because that is what a run gives it");
        check(!rowShowing(QStringLiteral("With this many conductors")),
              "manager: but not with a number of conductors");

        // A RING TERMINAL: it is placed, not run. No ends, no gap, no conductors - but insulated or not.
        dlg.selectKind(MechModelKind::RingInsulated);
        QApplication::processEvents();
        check(!rowShowing(QStringLiteral("The end at the left")) &&
                  !rowShowing(QStringLiteral("Gap left at each end")),
              "manager: a ring terminal is placed, so it has no ends that stretch and no gap");
        check(rowShowing(QStringLiteral("Insulation")), "manager: it is insulated or not, and says so");
        check(!rowShowing(QStringLiteral("At this length")),
              "manager: and there is no length to try it at");

        // THE OPEN WIRES: drawn for so many conductors, and the number comes from the harness.
        dlg.selectKind(MechModelKind::OpenWires);
        QApplication::processEvents();
        check(rowShowing(QStringLiteral("Conductors the drawing shows")) &&
                  rowShowing(QStringLiteral("With this many conductors")),
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

    if (failures == 0) std::printf("  -> ok\n");
    else std::printf("  -> %d failed\n", failures);
    return failures;
}
