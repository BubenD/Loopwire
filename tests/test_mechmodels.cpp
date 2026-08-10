// The default mechanical models: the standard pieces a harness drawing is made of, drawn once by an
// office and used everywhere. A model is not a block because LENGTH IS NOT PART OF IT: a wire is drawn
// at whatever length suits, and the harness says how long the wire is. So each model carries the two
// ends that must NOT stretch, and everything between them makes up the difference. That is the whole
// difficulty with scaling a drawing, so it is measured here rather than eyeballed.
#include "harness/MechModels.h"
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
        const std::vector<Entity> ents = mechModelEntities(ring, Vec2{0.0, 0.0}, 0.0, 500.0);
        const std::vector<double> bars = barsOf(ents);
        check(!bars.empty() && near(bars.back(), 60.0),
              "a ring terminal is placed at its own size, not stretched to the run");
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

    if (failures == 0) std::printf("  -> ok\n");
    else std::printf("  -> %d failed\n", failures);
    return failures;
}
