// The brick harness: the model the two-pane wizard edits, and the drawing it makes.
//
// WHAT IS WORTH TESTING HERE
//
// Data in, drawing out, no window: which links make a harness and which are refused, the symmetric
// layout, the route a run takes from one entry face to another, and that a run leaves and arrives
// square to its faces with its fans spread over them.
#include "app/BrickWizardDialog.h"
#include "app/DrawingPreview.h"
#include "app/Editor.h"
#include "app/MechanicalValidationDialog.h"
#include "app/RuleExplainer.h"
#include "app/View3DPane.h"
#include "geometry/Environment3D.h"
#include "geometry/Mesh3D.h"
#include "harness/Harness3D.h"
#include "harness/Route3D.h"
#include "harness/RouteLink.h"
#include "render/Render3D.h"
#include "harness/BrickHarness.h"
#include "harness/PartsLibrary.h"
#include "model/Block.h"
#include "model/Document.h"

#include <QApplication>
#include <QDropEvent>
#include <QJsonArray>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QCheckBox>
#include <QLabel>
#include <QComboBox>
#include <QTableWidget>
#include <QTreeWidget>
#include <QListWidget>
#include <QSplitter>
#include <QTabWidget>
#include <QToolButton>
#include <QMenu>
#include <QWheelEvent>
#include <QElapsedTimer>
#include <QSlider>

#ifdef EDRAW_HAVE_OCCT
// tests/test_occt_fixture.cpp
bool writeTestStepAssembly(const QString& path);
#endif
#include "harness/Bom.h"
#include "io/Dwx.h"
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLineEdit>
#include <QGroupBox>

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace Edraw_Desktop;

namespace {

int fails = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("  [FAIL] %s\n", what);
        ++fails;
    }
}

bool near(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }
bool near(const Vec2& a, const Vec2& b, double eps = 1e-6) { return distance(a, b) < eps; }

// Every leg square or on its slant, and the first and last square to their faces.
bool leavesSquare(const std::vector<Vec2>& r, const Vec2& aOut, const Vec2& bOut) {
    if (r.size() < 2) return false;
    const Vec2 first = (r[1] - r[0]).normalized();
    const Vec2 last = (r[r.size() - 1] - r[r.size() - 2]).normalized();
    return near(first, aOut.normalized(), 1e-9) && near(last, bOut.normalized() * -1.0, 1e-9);
}

void testLinks() {
    BrickDesign d;
    const QString x1 = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
    const QString x2 = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
    const QString x3 = d.addBrick(BrickType::End, PartKind::Ferrule, 2, 1);
    const QString run = d.addBrick(BrickType::TwistedBundle, PartKind::Wire, 1, 0);
    const QString run2 = d.addBrick(BrickType::Run, PartKind::Wire, 1, 1);
    QString why;
    check(!d.connect(x1, x2, &why) && !why.isEmpty(), "links: two end parts are not linked directly");
    const QString fan = d.addBrick(BrickType::Fanout, PartKind::Wire, 1, 2);
    check(!d.connect(run, fan, &why), "links: two specials only when one is a cable / heatshrink run");
    check(d.connect(x1, run, &why) && d.connect(run, x2, &why), "links: an end, a run, an end");
    check(!d.connect(run, x3, &why), "links: a run goes between two ends and no more");
    check(!d.connect(x1, run, &why), "links: the same link is not made twice");
    check(d.endsOf(run) == QStringList({x1, x2}), "links: the ends of a run, the left one first");
    d.removeBrick(x2);
    check(d.endsOf(run) == QStringList({x1}), "links: removing a brick takes its links with it");
    check(d.brick(run2)->run.outermost() == QLatin1String("cable"),
          "links: a cable / heatshrink run starts out as a cable");
}

void testJson() {
    BrickDesign d;
    const QString x1 = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
    const QString x2 = d.addBrick(BrickType::End, PartKind::Ferrule, 2, 0);
    const QString run = d.addBrick(BrickType::Run, PartKind::Wire, 1, 0);
    d.connect(x1, run);
    d.connect(run, x2);
    d.brick(x1)->libPart = QStringLiteral("connector-11");
    d.brick(x1)->name = QStringLiteral("X1");
    d.brick(run)->run.wires = 6;
    d.brick(run)->run.stack = {QStringLiteral("wires"), QStringLiteral("heatshrink")};
    d.brick(run)->run.kneeRadiusMm = 12.5;
    const BrickDesign e = BrickDesign::fromJson(d.toJson());
    check(e.bricks.size() == 3 && e.links.size() == 2, "json: every brick and link comes back");
    const Brick* r = e.brick(run);
    check(r && r->type == BrickType::Run && r->run.wires == 6 && r->run.outermost() == QLatin1String("heatshrink") &&
              near(r->run.kneeRadiusMm, 12.5),
          "json: a run keeps what it is drawn with");
    const Brick* c = e.brick(x1);
    check(c && c->kind == PartKind::Connector && c->libPart == QLatin1String("connector-11") &&
              c->name == QLatin1String("X1"),
          "json: an end keeps its part and its name");
}

void testRoutes() {
    const double lead = 20.0;
    // Looking straight at each other: no bend.
    auto r = routeBetweenFaces({0, 0}, {1, 0}, {200, 0}, {-1, 0}, lead);
    check(r.size() == 2, "route: two faces looking at each other are joined straight");
    // Off-line: a level shift, square out and square in, the slant at 45.
    r = routeBetweenFaces({0, 0}, {1, 0}, {200, -40}, {-1, 0}, lead);
    std::printf("  route: level shift in %d points\n", int(r.size()));
    check(r.size() == 4 && leavesSquare(r, {1, 0}, {-1, 0}),
          "route: off-line, a level shift that leaves and arrives square");
    if (r.size() == 4) {
        const Vec2 slant = r[2] - r[1];
        check(near(std::abs(slant.x), std::abs(slant.y), 1e-6),
              "route: and it crosses over on the slant at 45");
    }
    // Square to one another: one bend.
    r = routeBetweenFaces({0, 0}, {1, 0}, {150, -120}, {0, 1}, lead);
    check(r.size() == 3 && leavesSquare(r, {1, 0}, {0, 1}) && near(r[1], Vec2{150, 0}),
          "route: faces square to each other take one bend");
    // The same way round: out past the further one and back.
    r = routeBetweenFaces({0, 0}, {1, 0}, {50, 60}, {1, 0}, lead);
    check(leavesSquare(r, {1, 0}, {1, 0}) && r.size() == 4,
          "route: faces turned the same way go out and come back in");
    // Behind one another, facing: still square at both ends.
    r = routeBetweenFaces({0, 0}, {1, 0}, {-30, 50}, {-1, 0}, lead);
    check(leavesSquare(r, {1, 0}, {-1, 0}), "route: even behind one another it leaves square");
}

void testArrange() {
    // One connector, two ferrules, a run to each: the connector on the axis, the ferrules either
    // side of it, each run halfway.
    BrickDesign d;
    const QString x1 = d.addBrick(BrickType::End, PartKind::Connector, 3, 5);
    const QString f1 = d.addBrick(BrickType::End, PartKind::Ferrule, 7, 1);
    const QString f2 = d.addBrick(BrickType::End, PartKind::Ferrule, 9, 4);
    const QString r1 = d.addBrick(BrickType::Fanout, PartKind::Wire, 0, 0);
    const QString r2 = d.addBrick(BrickType::Fanout, PartKind::Wire, 0, 0);
    d.connect(x1, r1);
    d.connect(r1, f1);
    d.connect(x1, r2);
    d.connect(r2, f2);
    arrangeBricks(d);
    const Brick* c = d.brick(x1);
    const Brick* a = d.brick(f1);
    const Brick* b = d.brick(f2);
    std::printf("  arrange: X1 %.1f,%.1f  F1 %.1f,%.1f  F2 %.1f,%.1f\n", c->col, c->row, a->col, a->row,
                b->col, b->row);
    check(near(a->col, b->col) && a->col > c->col, "arrange: the two ferrules in one column past it");
    check(near((a->row + b->row) / 2.0, c->row) && !near(a->row, b->row),
          "arrange: either side of the connector's axis - symmetric");
    check(a->row < b->row, "arrange: in the order they were in");
    const Brick* s = d.brick(r1);
    check(near(s->col, (c->col + a->col) / 2.0) && near(s->row, (c->row + a->row) / 2.0),
          "arrange: each run halfway between its ends");
}

void testBuild() {
    BrickDesign d;
    const QString x1 = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
    const QString f1 = d.addBrick(BrickType::End, PartKind::Ferrule, 2, -0.5);
    const QString f2 = d.addBrick(BrickType::End, PartKind::Ferrule, 2, 0.5);
    const QString r1 = d.addBrick(BrickType::Fanout, PartKind::Wire, 1, 0);
    const QString r2 = d.addBrick(BrickType::Run, PartKind::Wire, 1, 0);
    d.connect(x1, r1);
    d.connect(r1, f1);
    d.connect(x1, r2);
    d.connect(r2, f2);
    d.brick(r1)->run.wires = 3;
    d.brick(r2)->run.wires = 3;
    const BrickBuild b = buildBrickHarness(d, nullptr);
    const BrickFace fx = b.faces.value(x1 + QStringLiteral(":0"));
    check(near(fx.out, Vec2{1, 0}) && near(b.faces.value(f1 + QStringLiteral(":0")).out, Vec2{-1, 0}),
          "build: each end faces what it is joined to, on the port it is linked on");
    std::vector<const Entity*> runs;
    for (const Entity& e : b.entities)
        if (e.type == EntityType::TwistedPair) runs.push_back(&e);
    check(runs.size() == 2, "build: a run for each special");
    if (runs.size() != 2) return;
    // THE CONNECTOR'S FACE IS SHARED: all six wires landing on it spread together over 95% of it,
    // each run taking its own three - so together they never exceed it.
    const double common = (0.95 * fx.lengthMm - 2.0) / 5.0;
    const Vec2 s1 = runs[0]->points.front();
    const Vec2 s2 = runs[1]->points.front();
    std::printf("  build: runs leave X1 at %.2f and %.2f (face %.1f long)\n", s1.y, s2.y,
                fx.lengthMm);
    check(near(std::abs(s1.y - s2.y), 3.0 * common) && near(s1.x, fx.centre.x),
          "build: two runs on one face leave side by side, three wires apart");
    for (const Entity* r : runs) {
        const Vec2 first = (r->points[1] - r->points[0]).normalized();
        check(near(first, Vec2{1, 0}, 1e-9), "build: every run leaves its face square to it");
        const double lane = r->meta.value(QStringLiteral("twistLane")).toDouble();
        const double ww = r->meta.value(QStringLiteral("twistWireWidth")).toDouble();
        check(near(lane, common, 1e-9) && near(common * 5.0 + ww, 0.95 * fx.lengthMm, 1e-9),
              "build: every wire on the face at one pitch, the outermost 95% of the face apart");
        check(r->meta.contains(QStringLiteral("brickId")), "build: and it says which brick it is");
    }
    const Entity* cable = runs[0]->meta.value(QStringLiteral("brickId")).toString() == r2 ? runs[0]
                                                                                          : runs[1];
    check(cable->meta.value(QStringLiteral("twistBody")).toString() == QLatin1String("cable"),
          "build: a cable run is drawn with a cable body");
}

// A FANOUT from one connector to three ferrules, one of them given two wires; a JOINT between two
// runs; ports turned; and the dimensions, labels, stack and transitions a run asks for.
void testFanoutJointAndExtras() {
    BrickDesign d;
    const QString x1 = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
    const QString fan = d.addBrick(BrickType::Fanout, PartKind::Wire, 1, 0);
    QStringList fs;
    for (int i = 0; i < 3; ++i) fs << d.addBrick(BrickType::End, PartKind::Ferrule, 2, i - 1.0);
    d.brick(fan)->run.wires = 5;
    QString why;
    check(d.connect(x1, fan, &why), "fanout: linked to its connector");
    for (const QString& f : fs) check(d.connect(fan, f, &why), "fanout: and to each ferrule");
    d.link(fan, fs[1])->wires = 2;
    check(d.startsOf(fan) == QStringList({x1}) && d.finishesOf(fan).size() == 3,
          "fanout: it starts on one part and finishes on three");
    // A plain run refuses a second part at the same end.
    const QString run = d.addBrick(BrickType::Fanout, PartKind::Wire, 5, 0);
    const QString runOnly = d.addBrick(BrickType::Run, PartKind::Wire, 5, 2);
    const QString a = d.addBrick(BrickType::End, PartKind::Ferrule, 6, 2);
    const QString c = d.addBrick(BrickType::End, PartKind::Ferrule, 6, 3);
    d.brick(runOnly)->run.fanFinishMm = 0.0;
    d.connect(runOnly, a, &why);
    check(!d.connect(runOnly, c, &why, kPortRight, -1), "run: a cable with no fan at an end takes one part there");
    d.brick(runOnly)->run.fanFinishMm = -1.0;
    check(d.connect(runOnly, c, &why, kPortRight, -1), "run: with a fan there, it takes several - the fan split between them");
    d.removeBrick(run);
    d.removeBrick(runOnly);
    d.removeBrick(a);
    d.removeBrick(c);

    BrickBuild b = buildBrickHarness(d, nullptr);
    std::vector<const Entity*> branches;
    for (const Entity& e : b.entities)
        if (e.meta.value(QStringLiteral("brickId")).toString() == fan && !e.meta.contains(QStringLiteral("brickTrunk")) &&
            (e.type == EntityType::TwistedPair || e.type == EntityType::Polyline))
            branches.push_back(&e);
    int wiresDrawn = 0;
    for (const Entity* e : branches)
        wiresDrawn += e->type == EntityType::TwistedPair ? e->meta.value(QStringLiteral("twistWires")).toInt() : 1;
    std::printf("  fanout: %d branch(es), %d wire(s) drawn\n", int(branches.size()), wiresDrawn);
    check(branches.size() == 3, "fanout: a branch to each part it finishes on");
    check(wiresDrawn == 5, "fanout: two to the one given two, the other three shared out");

    // A JOINT has a face each side, and a run on each lands on its own face.
    BrickDesign j;
    const QString left = j.addBrick(BrickType::End, PartKind::Connector, 0, 0);
    const QString r1 = j.addBrick(BrickType::Fanout, PartKind::Wire, 1, 0);
    const QString sp = j.addBrick(BrickType::End, PartKind::Splice, 2, 0);
    const QString r2 = j.addBrick(BrickType::Fanout, PartKind::Wire, 3, 0);
    const QString right = j.addBrick(BrickType::End, PartKind::Ferrule, 4, 0);
    j.connect(left, r1);
    j.connect(r1, sp);
    j.connect(sp, r2);
    j.connect(r2, right);
    check(j.portOn(sp, r1) == kPortLeft && j.portOn(sp, r2) == kPortRight,
          "joint: linked on its left and its right");
    b = buildBrickHarness(j, nullptr);
    const BrickFace fl = b.faces.value(sp + QStringLiteral(":0"));
    const BrickFace fr = b.faces.value(sp + QStringLiteral(":1"));
    check(near(fl.out, Vec2{-1, 0}) && near(fr.out, Vec2{1, 0}) && fr.centre.x > fl.centre.x,
          "joint: a face each side, looking out each way");
    // TURNED a half round, its left port faces right.
    j.brick(sp)->rot = 2;
    check(near(j.portWorldDir(*j.brick(sp), 0), Vec2{1, 0}),
          "rotate: turned twice, the left port faces right");
    j.brick(sp)->rot = 1;
    check(near(j.portWorldDir(*j.brick(sp), 1), Vec2{0, -1}),
          "rotate: turned once, the right port faces down the sheet");
    j.brick(sp)->rot = 0;

    // THE EXTRAS on one run.
    BrickRunSpec& s = j.brick(r1)->run;
    s.stack = {QStringLiteral("wires"), QStringLiteral("braid"), QStringLiteral("heatshrink")};
    s.transitions = RunEnds::Finish;
    s.dimTotal = s.dimFans = s.dimBetween = true;
    s.labels.push_back({-1, false, 60.0, QStringLiteral("W1")});
    s.labels.push_back({1, false, 5.0, QStringLiteral("2")});
    b = buildBrickHarness(j, nullptr);
    const Entity* body = nullptr;
    int dims = 0, labels = 0, braids = 0;
    for (const Entity& e : b.entities) {
        if (e.meta.value(QStringLiteral("brickId")).toString() != r1) continue;
        if (e.type == EntityType::TwistedPair) body = &e;
        if (e.type == EntityType::Dimension) ++dims;
        if (e.meta.value(QStringLiteral("mechRole")).toString() == QLatin1String("label") &&
            e.type == EntityType::Polyline)
            ++labels;
        if (e.meta.value(QStringLiteral("mechRole")).toString() == QLatin1String("braid")) ++braids;
    }
    std::printf("  extras: %d dimension(s), %d label(s), %d braid(s)\n", dims, labels, braids);
    check(body && body->meta.value(QStringLiteral("twistBody")).toString() == QLatin1String("heatshrink"),
          "stack: the outermost is what the body is - a heatshrink over a braid");
    check(braids == 0, "stack: and the braid under it is not drawn");
    check(body && body->meta.value(QStringLiteral("twistBodyTransitionSides")).toString() ==
                      QLatin1String("finish"),
          "transition: on the finish end only, as asked");
    check(dims == 3, "dimensions: the whole run, the fan (a fanout spreads at one end), and between");
    check(labels == 2, "labels: one on the whole harness, one on a wire of the fan");
    s.stack = {QStringLiteral("wires"), QStringLiteral("heatshrink"), QStringLiteral("braid")};
    b = buildBrickHarness(j, nullptr);
    braids = 0;
    for (const Entity& e : b.entities)
        if (e.meta.value(QStringLiteral("mechRole")).toString() == QLatin1String("braid")) ++braids;
    check(braids == 1, "stack: reordered so the braid is outermost, the braid is shown");
    // The whole-run dimension from the parts' far points is longer than from their near ones.
    s.stack = {QStringLiteral("wires")};
    auto totalDim = [&]() {
        for (const Entity& e : buildBrickHarness(j, nullptr).entities)
            if (e.meta.value(QStringLiteral("brickDim")).toString() == QLatin1String("total"))
                return distance(e.a, e.b);
        return 0.0;
    };
    const double nearLen = totalDim();
    j.dimFromFar = true;
    const double farLen = totalDim();
    check(farLen > nearLen + 1.0, "dimensions: from the far point of the part, the whole run is longer");
}

// A PORT FOR EVERY CONNECTION FACE the part has: by kind until a part is chosen, then the part's own.
void testPortsPerFace() {
    BrickDesign d;
    const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
    const QString s = d.addBrick(BrickType::End, PartKind::Splice, 2, 0);
    const QString y = d.addBrick(BrickType::End, PartKind::YSplit, 4, 0);
    const QString f = d.addBrick(BrickType::Fanout, PartKind::Wire, 1, 0);
    std::printf("  ports: connector %d, splice %d, Y-split %d, special %d\n", d.portCount(*d.brick(x)),
                d.portCount(*d.brick(s)), d.portCount(*d.brick(y)), d.portCount(*d.brick(f)));
    check(d.portCount(*d.brick(x)) == 1 && d.portCount(*d.brick(s)) == 2 && d.portCount(*d.brick(y)) == 3,
          "ports: a connector one, a splice two, a Y-split three");
    check(d.portCount(*d.brick(f)) == 2, "ports: a special starts on one side and finishes on the other");

    // A LIBRARY PART WITH THREE MARKED FACES has three ports, where its faces are.
    PartsCatalog cat;
    cat.setRemembering(false);
    QString pid;
    {
        LibraryPart& p = cat.active().add(PartKind::Connector);
        pid = p.id;
        QJsonArray drawing;
        for (const auto& [a, b] : {std::pair<Vec2, Vec2>{{0, -15}, {40, -15}}, std::pair<Vec2, Vec2>{{40, -15}, {40, 15}},
                                   std::pair<Vec2, Vec2>{{40, 15}, {0, 15}}, std::pair<Vec2, Vec2>{{0, 15}, {0, -15}}}) {
            Entity l;
            l.type = EntityType::Line;
            l.a = a;
            l.b = b;
            drawing.append(l.toJson());
        }
        p.mechanical = drawing;
        // one face on the left, two on the right one above the other
        p.outputs = {{{0, -10}, {0, 10}}, {{40, 4}, {40, 14}}, {{40, -14}, {40, -4}}};
    }
    d.catalog = &cat;
    d.brick(x)->libPart = pid;
    const BrickShape sh = d.shapeOf(*d.brick(x));
    check(sh.ports() == 3, "ports: a library part with three faces has three ports");
    if (sh.ports() == 3) {
        check(near(sh.faceOut[0], Vec2{-1, 0}) && near(sh.faceOut[1], Vec2{1, 0}) && near(sh.faceOut[2], Vec2{1, 0}),
              "ports: each looking the way its face does - one left, two right");
        check(sh.faceMid[1].y * sh.faceMid[2].y < 0.0 && near(sh.faceLen[1], 10.0),
              "ports: the two on the right one above the other, each its face's length");
    }
    d.brick(x)->libPart.clear();

    // RUNS ON A Y-SPLIT land on its three faces: one in, two out.
    BrickDesign j;
    const QString in = j.addBrick(BrickType::End, PartKind::Connector, 0, 0);
    const QString r0 = j.addBrick(BrickType::Fanout, PartKind::Wire, 1, 0);
    const QString ys = j.addBrick(BrickType::End, PartKind::YSplit, 2, 0);
    const QString r1 = j.addBrick(BrickType::Fanout, PartKind::Wire, 3, -1);
    const QString r2 = j.addBrick(BrickType::Fanout, PartKind::Wire, 3, 1);
    const QString o1 = j.addBrick(BrickType::End, PartKind::Ferrule, 4, -1);
    const QString o2 = j.addBrick(BrickType::End, PartKind::Ferrule, 4, 1);
    QString why;
    j.connect(in, r0);
    check(j.connect(r0, ys, &why, -1, 0) && j.connect(ys, r1, &why, 1, -1) && j.connect(ys, r2, &why, 2, -1),
          "Y-split: linked on each of its three ports");
    j.connect(r1, o1);
    j.connect(r2, o2);
    const BrickBuild b = buildBrickHarness(j, nullptr);
    const BrickFace f0 = b.faces.value(ys + QStringLiteral(":0"));
    const BrickFace f1 = b.faces.value(ys + QStringLiteral(":1"));
    const BrickFace f2 = b.faces.value(ys + QStringLiteral(":2"));
    check(b.faces.contains(ys + QStringLiteral(":2")) && near(f0.out, Vec2{-1, 0}) && near(f1.out, Vec2{1, 0}) &&
              f1.centre.y > f2.centre.y,
          "Y-split: a face in on the left, two out on the right one above the other");
    int onUpper = 0, onLower = 0;
    for (const Entity& e : b.entities) {
        if (e.type != EntityType::TwistedPair) continue;
        const QString bid = e.meta.value(QStringLiteral("brickId")).toString();
        if (bid == r1 && distance(e.points.front(), f1.centre) < 0.5 * f1.lengthMm) ++onUpper;
        if (bid == r2 && distance(e.points.front(), f2.centre) < 0.5 * f2.lengthMm) ++onLower;
    }
    check(onUpper == 1 && onLower == 1, "Y-split: each run out leaves from its own face");

    // AN END TURNS TO FACE ITS FIRST LINK: a connector to the right of its run looks left.
    BrickDesign t;
    const QString run = t.addBrick(BrickType::Run, PartKind::Wire, 0, 0);
    const QString xr = t.addBrick(BrickType::End, PartKind::Connector, 2, 0);
    t.connect(run, xr);
    check(near(t.portWorldDir(*t.brick(xr), 0), Vec2{-1, 0}), "turn: linked from its left, it looks left");
    t.brick(xr)->rot = (t.brick(xr)->rot + 1) % 4;
    check(near(t.portWorldDir(*t.brick(xr), 0), Vec2{0, 1}), "turn: and turned a quarter, its one port with it");
}

// The third round: labels on wires, nets, the label model, a cable split between parts, and the
// balloon's leader - in the model; Enter, the live bill and the saved wizards - in the window.
void testRoundThreeModel() {
    // A connector, a four-wire fanout, a ferrule - labels on wire 1 and wire 3 of the start fan.
    BrickDesign d;
    const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
    const QString f = d.addBrick(BrickType::Fanout, PartKind::Wire, 1, 0);
    const QString e = d.addBrick(BrickType::End, PartKind::Ferrule, 2, 0);
    d.connect(x, f);
    d.connect(f, e);
    BrickRunSpec& r = d.brick(f)->run;
    r.wires = 4;
    r.wireWidthMm = 2.0;
    r.nets = {QStringLiteral("GND"), QStringLiteral("+12V"), QStringLiteral("CAN_H"), QStringLiteral("CAN_L")};
    r.labels.push_back({0, false, 6.0, QString()});
    r.labels.push_back({2, false, 6.0, QStringLiteral("L3")});
    r.labels.push_back({-1, false, 60.0, QStringLiteral("W1")});
    const BrickBuild b = buildBrickHarness(d, nullptr);
    const Entity* run = nullptr;
    std::vector<const Entity*> texts;
    int modelParts = 0;
    for (const Entity& en : b.entities) {
        if (en.type == EntityType::TwistedPair) run = &en;
        if (en.meta.value(QStringLiteral("mechRole")).toString() != QLatin1String("label")) continue;
        if (en.meta.contains(QStringLiteral("labelText"))) texts.push_back(&en);
        else ++modelParts;
    }
    QStringList said;
    for (const Entity* t : texts) said << t->meta.value(QStringLiteral("labelText")).toString();
    std::printf("  labels: %d text(s) [%s], %d model part(s)\n", int(texts.size()), qPrintable(said.join(", ")),
                modelParts);
    check(said.contains(QStringLiteral("GND")) && said.contains(QStringLiteral("L3")) &&
              said.contains(QStringLiteral("W1")),
          "labels: a wire label with no text says its net; the others say what they were given");
    check(modelParts >= 3 * 3, "labels: each drawn as the house label model, not a plain box");
    // CENTRED ON ITS WIRE: the label's middle lies on that wire's own path, and wire 1 is the top one.
    if (run) {
        const auto strands = twistedPairStrands(*run);
        auto onAWire = [&](const Vec2& p) {
            double best = 1e300;
            for (size_t j = 0; j + 1 < strands.size(); j += 2)
                for (size_t q = 1; q < strands[j].size() && q < strands[j + 1].size(); ++q) {
                    const Vec2 c0 = (strands[j][q - 1] + strands[j + 1][q - 1]) * 0.5;
                    const Vec2 c1 = (strands[j][q] + strands[j + 1][q]) * 0.5;
                    best = std::min(best, closestPointOnSegment(p, c0, c1).distance);
                }
            return best;
        };
        const Entity* gnd = nullptr;
        const Entity* l3 = nullptr;
        for (const Entity* t : texts) {
            if (t->meta.value(QStringLiteral("labelText")).toString() == QLatin1String("GND")) gnd = t;
            if (t->meta.value(QStringLiteral("labelText")).toString() == QLatin1String("L3")) l3 = t;
        }
        auto midOf = [](const Entity* t) {
            return Vec2{t->meta.value(QStringLiteral("labelMidX")).toDouble(), t->meta.value(QStringLiteral("labelMidY")).toDouble()};
        };
        check(gnd && l3 && onAWire(midOf(gnd)) < 1e-6 && onAWire(midOf(l3)) < 1e-6,
              "labels: a label on a wire is centred on that wire");
        check(gnd && l3 && midOf(gnd).y > midOf(l3).y, "labels: the wires are counted from the top down");
    }

    // A CABLE WITH A FAN FINISHING ON TWO PARTS: one cable to where it splits, the wires on from there.
    BrickDesign c;
    const QString cx = c.addBrick(BrickType::End, PartKind::Connector, 0, 0);
    const QString cable = c.addBrick(BrickType::Run, PartKind::Wire, 1, 0);
    const QString c1 = c.addBrick(BrickType::End, PartKind::Connector, 2, -1);
    const QString c2 = c.addBrick(BrickType::End, PartKind::Connector, 2, 1);
    c.brick(cable)->run.wires = 6;
    QString why;
    c.connect(cx, cable);
    check(c.connect(cable, c1, &why) && c.connect(cable, c2, &why), "split: a cable with a fan takes two parts");
    c.link(cable, c1)->wires = 2;
    const BrickBuild cb = buildBrickHarness(c, nullptr);
    std::vector<const Entity*> runs;
    for (const Entity& en : cb.entities)
        if (en.type == EntityType::TwistedPair && en.meta.value(QStringLiteral("brickId")).toString() == cable)
            runs.push_back(&en);
    const Entity* trunk = nullptr;
    int outWires = 0;
    // The trunk is the one carrying every wire (each branch has the cable's body too, on to its part).
    for (const Entity* en : runs)
        if (!trunk || en->meta.value(QStringLiteral("twistWires")).toInt() > trunk->meta.value(QStringLiteral("twistWires")).toInt())
            trunk = en;
    for (const Entity* en : runs)
        if (en != trunk) outWires += en->meta.value(QStringLiteral("twistWires")).toInt();
    std::printf("  split: %d run(s), trunk %s, %d wire(s) out of it\n", int(runs.size()), trunk ? "yes" : "no",
                outWires);
    check(runs.size() == 3 && trunk && trunk->meta.value(QStringLiteral("twistWires")).toInt() == 6,
          "split: one cable of all six wires up to where it splits");
    check(outWires == 6, "split: and its wires on from there, two to one part and four to the other");
    if (trunk) {
        const Vec2 end = trunk->points.back();
        bool fromSplit = true;
        for (const Entity* en : runs)
            if (en != trunk && distance(en->points.front(), end) > 10.0) fromSplit = false;
        check(fromSplit, "split: each run of wires starts where the cable ends");
    }

    // A BALLOON'S LEADER lands on the part's edge from a balloon standing clear of it.
    d.brick(x)->balloon = true;
    for (const Entity& en : buildBrickHarness(d, nullptr).entities)
        if (en.meta.value(QStringLiteral("balloon")).toBool()) {
            const QJsonArray leaders = en.meta.value(QStringLiteral("leaders")).toArray();
            const bool nested = leaders.size() == 1 && leaders.at(0).isArray() && leaders.at(0).toArray().size() == 2;
            check(nested, "balloon: its leader is a list of tips, each an [x, y] - what the renderer draws");
            const QJsonArray tip = leaders.at(0).toArray();
            const Vec2 t{tip.at(0).toDouble(), tip.at(1).toDouble()};
            check(nested && distance(t, en.center) > en.radius + 10.0 && t.length() > 1.0,
                  "balloon: its leader is a line, from clear of the part to the part");
        }
}

void testRoundThree() {
    testRoundThreeModel();
    Document doc;
    // A schematic connector X1 with a wire on its first pin, net "CAN_H".
    {
        ConnectorSpec spec;
        spec.designator = QStringLiteral("X1");
        spec.pinCount = 2;
        Entity conn;
        conn.id = QStringLiteral("C1");
        conn.type = EntityType::Connector;
        conn.a = {0, 0};
        writeConnectorSpec(conn.meta, spec);
        const ConnectorGeometry g = connectorGeometry(conn.a, spec);
        Entity w;
        w.id = QStringLiteral("W1");
        w.type = EntityType::Wire;
        w.points = {g.pinEnd[0], g.pinEnd[0] + Vec2{50, 0}};
        w.meta.insert(QStringLiteral("netName"), QStringLiteral("CAN_H"));
        doc.entities().push_back(conn);
        doc.entities().push_back(w);
    }
    Editor ed;
    ed.setDocument(&doc);
    BrickWizardDialog dlg(&ed);
    dlg.show();
    QApplication::processEvents();
    check(dlg.schematicNets({QStringLiteral("X1")}) == QStringList({QStringLiteral("CAN_H")}),
          "nets: the schematic's nets on the connector of that designator");

    BrickDesign& d = dlg.design();
    const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
    d.brick(x)->name = QStringLiteral("X1");
    const QString f = d.addBrick(BrickType::Fanout, PartKind::Wire, 1, 0);
    const QString e = d.addBrick(BrickType::End, PartKind::Ferrule, 2, 0);
    d.connect(x, f);
    d.connect(f, e);
    dlg.selectBrick(f);
    auto* nets = dlg.findChild<QTableWidget*>(QStringLiteral("brickNets"));
    auto* net0 = nets ? qobject_cast<QComboBox*>(nets->cellWidget(0, 1)) : nullptr;
    check(net0 && net0->findText(QStringLiteral("CAN_H")) >= 0,
          "nets: each wire offers the schematic's nets on the parts it is linked to");
    if (net0) net0->setCurrentText(QStringLiteral("CAN_H"));
    check(!d.brick(f)->run.nets.isEmpty() && d.brick(f)->run.nets.front() == QLatin1String("CAN_H"),
          "nets: picked for a wire, it is that wire's net");

    // ENTER in a label's text does not turn the harness round.
    const int rotWas = d.brick(f)->rot;
    if (auto* add = dlg.findChild<QPushButton*>(QStringLiteral("brickLabelAdd"))) add->click();
    auto* labels = dlg.findChild<QTableWidget*>(QStringLiteral("brickLabels"));
    auto* text = labels ? qobject_cast<QLineEdit*>(labels->cellWidget(0, 6)) : nullptr;
    if (text) {
        text->setFocus();
        text->setText(QStringLiteral("W7"));
        emit text->textEdited(QStringLiteral("W7"));
        QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(text, &press);
    }
    check(text && d.brick(f)->rot == rotWas && dlg.isVisible(), "enter: typing a label and Enter turns nothing");
    bool anyDefault = false;
    for (QPushButton* b : dlg.findChildren<QPushButton*>()) anyDefault = anyDefault || b->autoDefault() || b->isDefault();
    check(!anyDefault, "enter: no button in the window is pressed by Enter");

    // THE BILL, LIVE: a part added shows in the table at once.
    auto* bom = dlg.findChild<QTableWidget*>(QStringLiteral("brickBom"));
    const int rowsWas = bom ? bom->rowCount() : -1;
    QString partId;
    for (const LibraryPart* p : dlg.catalog().merged().ofKind(PartKind::Ferrule)) {
        partId = p->id;
        break;
    }
    if (!partId.isEmpty() && bom) {
        dlg.addPartToBom(partId);
        check(bom->rowCount() == rowsWas + 1, "bom: the bill under the preview shows the line added");
    }

    // SAVED IN THE DRAWING, by name, two of them; opened again; deleted; and through a file.
    check(dlg.saveToDrawing(QStringLiteral("W1")), "saved: the harness saved in the drawing");
    d.brick(f)->run.wires = 7;
    check(dlg.saveToDrawing(QStringLiteral("W2")), "saved: and a second one");
    check(dlg.savedNames() == QStringList({QStringLiteral("W1"), QStringLiteral("W2")}),
          "saved: both are kept, by name");
    check(dlg.loadFromDrawing(QStringLiteral("W1")) && dlg.design().brick(f) &&
              dlg.design().brick(f)->run.wires == 4,
          "saved: opened again, it is the harness as it was saved");
    const QString path = QDir::temp().filePath(QStringLiteral("brick-wizards-test.dwx"));
    check(dwx::save(doc, path), "saved: the drawing written to a file");
    Document back;
    check(dwx::load(back, path) &&
              back.wizards().value(QStringLiteral("harnesses")).toObject().keys().size() == 2,
          "saved: the wizards come back with the drawing");
    QFile::remove(path);
    check(dlg.deleteFromDrawing(QStringLiteral("W2")) && dlg.savedNames() == QStringList({QStringLiteral("W1")}),
          "saved: one deleted, the other kept");
}

// The fourth round.
void testRoundFour() {
    auto runsOf = [](const BrickBuild& b, const QString& id) {
        std::vector<const Entity*> out;
        for (const Entity& e : b.entities)
            if (e.type == EntityType::TwistedPair && e.meta.value(QStringLiteral("brickId")).toString() == id)
                out.push_back(&e);
        return out;
    };

    // 1. THE DESIGNATOR, in front of the part: past its end away from the harness.
    {
        BrickDesign d;
        const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        d.brick(x)->name = QStringLiteral("X7");
        d.brick(x)->showDesignator = true;
        const QString r = d.addBrick(BrickType::Run, PartKind::Wire, 1, 0);
        d.connect(x, r);
        const BrickBuild b = buildBrickHarness(d, nullptr);
        const Entity* des = nullptr;
        Bounds part;
        for (const Entity& e : b.entities) {
            if (e.meta.value(QStringLiteral("brickId")).toString() != x) continue;
            if (e.meta.value(QStringLiteral("mechRole")).toString() == QLatin1String("designator")) des = &e;
            else part.expand(e.bounds());
        }
        check(des && des->meta.value(QStringLiteral("text")).toString() == QLatin1String("X7") && part.valid &&
                  des->a.x < part.min.x,
              "designator: its eye on, written in front of the part - past its end away from the harness");
    }

    // 3. TWO CONNECTORS ON THE SAME SIDE OF A CABLE are both joined to it.
    {
        BrickDesign d;
        const QString a = d.addBrick(BrickType::End, PartKind::Connector, 0, -1);
        const QString b = d.addBrick(BrickType::End, PartKind::Connector, 0, 1);
        const QString c = d.addBrick(BrickType::Run, PartKind::Wire, 2, 0);
        QString why;
        check(d.connect(a, c, &why, -1, kPortLeft) && d.connect(b, c, &why, -1, kPortLeft),
              "same side: two connectors on the cable's start");
        const BrickBuild bb = buildBrickHarness(d, nullptr);
        const auto runs = runsOf(bb, c);
        int onA = 0, onB = 0;
        const BrickFace fa = bb.faces.value(a + QStringLiteral(":0"));
        const BrickFace fb = bb.faces.value(b + QStringLiteral(":0"));
        for (const Entity* r : runs) {
            for (const Vec2& p : {r->points.front(), r->points.back()}) {
                if (distance(p, fa.centre) < fa.lengthMm) ++onA;
                if (distance(p, fb.centre) < fb.lengthMm) ++onB;
            }
        }
        std::printf("  same side: %d run(s), %d on one connector, %d on the other\n", int(runs.size()), onA, onB);
        check(onA == 1 && onB == 1, "same side: both are joined - the cable splits to them");
    }

    // 4. A FANOUT spreads at the end on a part; turned, at the other.
    // 9. and it may be carried on into a cable, the wires passing straight through.
    {
        BrickDesign d;
        const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString f = d.addBrick(BrickType::Fanout, PartKind::Wire, 1, 0);
        const QString c = d.addBrick(BrickType::Run, PartKind::Wire, 3, 0);
        const QString y = d.addBrick(BrickType::End, PartKind::Connector, 4, 0);
        QString why;
        d.connect(x, f);
        check(d.connect(f, c, &why), "join: a fanout carried on into a cable");
        d.connect(c, y);
        const BrickDesign keep = d;
        BrickBuild b = buildBrickHarness(d, nullptr);
        const auto fr = runsOf(b, f);
        const auto cr = runsOf(b, c);
        check(fr.size() == 1 && cr.size() == 1, "join: a run each side of the junction");
        if (fr.size() == 1 && cr.size() == 1) {
            check(fr[0]->meta.value(QStringLiteral("twistSpread")).toString() == QLatin1String("start") &&
                      fr[0]->meta.value(QStringLiteral("twistEndsFinish")).toDouble() == 0.0,
                  "fanout: spreads at the connector, packed where it joins the cable");
            check(distance(fr[0]->points.back(), cr[0]->points.front()) < 1e-6,
                  "join: the cable starts where the fanout ends");
        }
        d.brick(f)->run.fanSide = 1;
        b = buildBrickHarness(d, nullptr);
        const auto turned = runsOf(b, f);
        check(!turned.empty() && turned[0]->meta.value(QStringLiteral("twistSpread")).toString() == QLatin1String("finish"),
              "fanout: turned round, it spreads at the other end");
    }

    // 10. PLACED BY LENGTH: two runs of different lengths to one connector.
    {
        BrickDesign d;
        const QString a = d.addBrick(BrickType::End, PartKind::Connector, 0, -1);
        const QString b = d.addBrick(BrickType::End, PartKind::Connector, 0, 1);
        const QString m = d.addBrick(BrickType::End, PartKind::Connector, 4, 0);
        const QString r1 = d.addBrick(BrickType::Run, PartKind::Wire, 2, -1);
        const QString r2 = d.addBrick(BrickType::TwistedPair, PartKind::Wire, 2, 1);
        d.connect(a, r1);
        d.connect(r1, m);
        d.connect(b, r2);
        d.connect(r2, m);
        d.brick(r1)->run.lengthMm = 200.0;
        d.brick(r2)->run.lengthMm = 300.0;
        const BrickBuild bb = buildBrickHarness(d, nullptr);
        const double xa = bb.faces.value(a + QStringLiteral(":0")).centre.x;
        const double xb = bb.faces.value(b + QStringLiteral(":0")).centre.x;
        const double xm = bb.faces.value(m + QStringLiteral(":0")).centre.x;
        std::printf("  by length: %.0f and %.0f from the common connector\n", xm - xa, xm - xb);
        check(std::abs((xm - xb) - (xm - xa) - 100.0) < 30.0 && xb < xa,
              "by length: the longer run's connector sits further off");
    }

    // 5-8. A LABEL masks what is under it, calls its text out on a leader (or writes it on itself),
    // is kept off a bend; a dimension says what it is told and keeps the measurement.
    {
        BrickDesign d;
        const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString r = d.addBrick(BrickType::Run, PartKind::Wire, 1, 0);
        const QString y = d.addBrick(BrickType::End, PartKind::Connector, 2, 3);
        d.connect(x, r);
        d.connect(r, y);
        BrickRunSpec& s = d.brick(r)->run;
        s.labels.push_back({-1, false, 60.0, QStringLiteral("A"), 0.0, true});
        s.labels.push_back({-1, false, 90.0, QStringLiteral("B"), 400.0, false});
        s.dimTotal = true;
        s.dimTextTotal = QStringLiteral("250 ±5");
        const BrickBuild b = buildBrickHarness(d, nullptr);
        int masks = 0;
        const Entity* a = nullptr;
        const Entity* bText = nullptr;
        double longest = 0.0;
        const Entity* total = nullptr;
        for (const Entity& e : b.entities) {
            if (e.type == EntityType::Wipeout && e.meta.value(QStringLiteral("labelMask")).toBool()) {
                ++masks;
                longest = std::max(longest, distance(e.points[0], e.points[1]));
            }
            if (e.meta.value(QStringLiteral("labelText")).toString() == QLatin1String("A")) a = &e;
            if (e.meta.value(QStringLiteral("labelText")).toString() == QLatin1String("B")) bText = &e;
            if (e.meta.value(QStringLiteral("brickDim")).toString() == QLatin1String("total")) total = &e;
        }
        check(masks == 2, "label: each masks what is under it - it is over the wire, not see-through");
        check(a && a->meta.value(QStringLiteral("textLeader")).toBool(), "label: shown on the drawing, its text on a leader");
        check(bText && bText->type == EntityType::Text, "label: not shown with a leader, its text on the label");
        std::printf("  label: asked 400 long, drawn %.1f\n", longest);
        check(longest < 399.0, "label: kept off the bend - shortened to the straight it sits on");
        check(total && total->meta.value(QStringLiteral("textOverride")).toString() == s.dimTextTotal &&
                  total->meta.value(QStringLiteral("brickMeasured")).toDouble() > 1.0,
              "dimension: says what it is told, the real length kept with it");
    }

    // 2 and 4 in the window: the parts list's columns, and Shift turning a fanout.
    {
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        BrickWizardDialog dlg(&ed);
        dlg.show();
        QApplication::processEvents();
        auto* parts = dlg.findChild<QTreeWidget*>(QStringLiteral("brickParts"));
        check(parts && parts->columnCount() == 7 &&
                  parts->headerItem()->text(5) == QLatin1String("Entry faces"),
              "parts list: says which parts have a drawing, entry faces and near/far points");
        BrickDesign& d = dlg.design();
        const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString f = d.addBrick(BrickType::Fanout, PartKind::Wire, 1, 0);
        d.connect(x, f);
        dlg.selectBrick(f);
        const int was = d.fanoutSpreadSide(*d.brick(f));
        QKeyEvent down(QEvent::KeyPress, Qt::Key_Shift, Qt::ShiftModifier);
        QKeyEvent up(QEvent::KeyRelease, Qt::Key_Shift, Qt::NoModifier);
        QApplication::sendEvent(dlg.canvas(), &down);
        QApplication::sendEvent(dlg.canvas(), &up);
        check(d.fanoutSpreadSide(*d.brick(f)) == 1 - was, "fanout: Shift on its own turns its spread round");
    }
}

void mouse(QWidget* w, QEvent::Type t, const QPointF& at, Qt::KeyboardModifiers mods = Qt::NoModifier);

// The fifth round.
void testRoundFive() {
    auto of = [](const BrickBuild& b, const QString& id, auto pred) {
        std::vector<const Entity*> out;
        for (const Entity& e : b.entities)
            if (e.meta.value(QStringLiteral("brickId")).toString() == id && pred(e)) out.push_back(&e);
        return out;
    };
    auto isRun = [](const Entity& e) { return e.type == EntityType::TwistedPair; };

    // 1. A LABEL UNDER CLEAR HEATSHRINK is the label model that has the heatshrink on it.
    {
        BrickDesign d;
        const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString r = d.addBrick(BrickType::Run, PartKind::Wire, 1, 0);
        const QString y = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        d.connect(x, r);
        d.connect(r, y);
        d.brick(r)->run.labels.push_back({-1, false, 60.0, QStringLiteral("A"), 0.0, false, false});
        auto parts = [&]() {
            int n = 0;
            for (const Entity& e : buildBrickHarness(d, nullptr).entities)
                if (e.meta.value(QStringLiteral("mechRole")).toString() == QLatin1String("label")) ++n;
            return n;
        };
        const int plain = parts();
        d.brick(r)->run.labels.front().clearShrink = true;
        const int covered = parts();
        std::printf("  clear heatshrink: %d label part(s), %d covered\n", plain, covered);
        check(covered > plain, "label: under clear heatshrink, the heatshrink is drawn on it");
    }

    // 3. A FANOUT TO SEVERAL PARTS: one gathered body, only the spread divided.
    // 4. and a fan into a cable enters across the drawing's share of the cable's width.
    {
        BrickDesign d;
        const QString p1 = d.addBrick(BrickType::End, PartKind::Connector, 0, -1);
        const QString p2 = d.addBrick(BrickType::End, PartKind::Connector, 0, 1);
        const QString f = d.addBrick(BrickType::Fanout, PartKind::Wire, 2, 0);
        const QString c = d.addBrick(BrickType::Run, PartKind::Wire, 4, 0);
        const QString p3 = d.addBrick(BrickType::End, PartKind::Connector, 6, 0);
        QString why;
        d.connect(p1, f, &why, -1, kPortLeft);
        d.connect(p2, f, &why, -1, kPortLeft);
        d.connect(f, c, &why);
        d.connect(c, p3);
        d.brick(c)->run.bodyWidthMm = 10.0;
        d.faceFanPct = 80.0;
        const BrickBuild b = buildBrickHarness(d, nullptr);
        const auto runs = of(b, f, isRun);
        int trunks = 0;
        const Entity* trunk = nullptr;
        for (const Entity* e : runs)
            if (e->meta.value(QStringLiteral("brickTrunk")).toBool()) {
                ++trunks;
                trunk = e;
            }
        std::printf("  fanout body: %d run(s), %d body\n", int(runs.size()), trunks);
        check(runs.size() == 3 && trunks == 1, "fanout: one gathered body, and a spread to each part");
        if (trunk) {
            // The body's end at the cable: its wires across 80% of the cable's 10 mm.
            const double laneAtCable = trunk->meta.value(QStringLiteral("twistLane")).toDouble();
            const double ww = trunk->meta.value(QStringLiteral("twistWireWidth")).toDouble();
            const int n = trunk->meta.value(QStringLiteral("twistWires")).toInt();
            std::printf("  into the cable: %.2f across of 10\n", laneAtCable * (n - 1) + ww);
            check(std::abs(laneAtCable * (n - 1) + ww - 8.0) < 1e-6,
                  "fanout: enters the cable across the drawing's fan share of its width (80%)");
        }
        // 6. THE CABLE'S TRANSITIONS AT BOTH ENDS - at the end joined to the fanout too.
        d.brick(c)->run.transitions = RunEnds::Both;
        const BrickBuild withBoth = buildBrickHarness(d, nullptr);
        const auto cr = of(withBoth, c, isRun);
        check(!cr.empty() && cr[0]->meta.value(QStringLiteral("twistEndBoot")).toBool() &&
                  cr[0]->meta.value(QStringLiteral("twistBodyTransitionSides")).toString() == QLatin1String("both"),
              "transition: at both ends, the joined end as its end boot");
    }

    // A BRICK'S PART DRAWN BIGGER OR SMALLER: its faces go with it, so what joins it meets it there - and on
    // Auto, as big as the bundle coming in wants.
    {
        BrickDesign d;
        const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString y = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        d.connect(x, c);
        d.connect(c, y);
        const BrickBuild plain = buildBrickHarness(d, nullptr);
        const double face1 = plain.faces.value(QStringLiteral("%1:0").arg(x)).lengthMm;
        d.brick(x)->scale = 2.0;
        d.shapeCache.clear();
        const BrickBuild big = buildBrickHarness(d, nullptr);
        const double face2 = big.faces.value(QStringLiteral("%1:0").arg(x)).lengthMm;
        std::printf("  scale: face %.2f at 100%%, %.2f at 200%%\n", face1, face2);
        check(face1 > 0.0 && std::abs(face2 - 2.0 * face1) < 1e-6, "scale: the part drawn twice as big, its face with it");
        // AUTO: the part made the drawing's usual entry size - here the other end is drawn at 200 %, so the
        // entries are 20 and 40 and the mean is 30.
        d.brick(x)->scale = 1.0;
        d.brick(x)->autoScale = true;
        d.brick(y)->scale = 2.0;
        d.shapeCache.clear();
        const BrickBuild autoB = buildBrickHarness(d, nullptr);
        const double k = autoB.scales.value(x, 1.0);
        const double face3 = autoB.faces.value(QStringLiteral("%1:0").arg(x)).lengthMm;
        std::printf("  scale: auto %.2f - face %.2f, the entries on the drawing averaging 30\n", k, face3);
        check(std::abs(face3 - 30.0) < 0.3, "scale: on Auto, the part's entry is the drawing's mean entry size");
        d.brick(y)->scale = 1.0;
        // ROOM TO SEE: two connectors facing each other on a very short run are still drawn apart, and the
        // run's length is still what its dimension says.
        {
            d.brick(x)->autoScale = false;
            d.brick(c)->run.lengthMm = 5.0;
            d.brick(c)->run.dimTotal = true;
            d.shapeCache.clear();
            const BrickBuild tight = buildBrickHarness(d, nullptr);
            const BrickFace fa = tight.faces.value(QStringLiteral("%1:0").arg(x));
            const BrickFace fb = tight.faces.value(QStringLiteral("%1:0").arg(y));
            const double gap = distance(fa.centre, fb.centre);
            double said = -1.0;
            for (const Entity& e : tight.entities)
                if (e.meta.value(QStringLiteral("brickDim")).toString() == QStringLiteral("total"))
                    said = e.meta.value(QStringLiteral("brickMeasured")).toDouble();
            std::printf("  spacing: faces %.1f apart for a 5 mm run, dimension says %.1f\n", gap, said);
            check(gap < 40.0, "spacing: a short run brings its connectors as close as it says - down to nought");
            check(said >= 0.0 && said < 40.0, "spacing: and the dimension still gives the run's own length");
            d.brick(c)->run.lengthMm = 150.0;
            d.brick(c)->run.dimTotal = false;
            d.brick(x)->autoScale = true;
        }
        // Saved and read back.
        const BrickDesign back = BrickDesign::fromJson(d.toJson());
        check(back.brick(x) && back.brick(x)->autoScale, "scale: Auto kept with the design");
    }

    // 7 (round six). A FAN CONVERGES TO SO MANY WIRES.
    {
        BrickDesign d;
        const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString fo = d.addBrick(BrickType::Fanout, PartKind::Wire, 1, 0);
        const QString y = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        d.connect(x, fo);
        d.connect(fo, y);
        d.brick(fo)->run.wires = 6;
        d.brick(fo)->run.trunkWires = 2;
        const BrickBuild b = buildBrickHarness(d, nullptr);
        const auto runs = of(b, fo, isRun);
        check(!runs.empty() && runs[0]->meta.value(QStringLiteral("twistTrunkWires")).toInt() == 2 &&
                  b.actual.value(fo).trunk == 2.0,
              "converge: the fan's wires gathered to the number asked for");
    }

    // 8, 9. THE WHOLE LENGTH: at its angle, tracing the run, single-ended.
    {
        BrickDesign d;
        const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString r = d.addBrick(BrickType::Run, PartKind::Wire, 1, 0);
        const QString y = d.addBrick(BrickType::End, PartKind::Connector, 2, 3);
        d.connect(x, r);
        d.connect(r, y);
        BrickRunSpec& s = d.brick(r)->run;
        s.dimTotal = true;
        auto total = [&]() {
            BrickBuild b = buildBrickHarness(d, nullptr);
            std::vector<Entity> out;
            for (const Entity& e : b.entities)
                if (e.meta.value(QStringLiteral("brickDim")).toString() == QLatin1String("total") ||
                    e.meta.value(QStringLiteral("dimTrace")).toBool())
                    out.push_back(e);
            return out;
        };
        auto lin = total();
        s.dimLinear = false;
        auto ang = total();
        // Linear: it measures along the harness, level - and its two points are the run's two real ends, so its
        // extension lines go down to them (the end a row lower than the start).
        check(!lin.empty() && !ang.empty() && lin[0].meta.value(QStringLiteral("dimType")).toString() == QLatin1String("linear") &&
                  lin[0].meta.value(QStringLiteral("dimAcross")).toBool() && std::abs(lin[0].b.y - lin[0].a.y) > 1.0 &&
                  ang[0].meta.value(QStringLiteral("dimType")).toString() == QLatin1String("aligned") &&
                  std::abs(ang[0].b.y - ang[0].a.y) > 1.0,
              "dimension: linear along the harness to the real end points; not linear, at the angle between its points");
        s.dimTrace = true;
        auto tr = total();
        bool traced = false;
        for (const Entity& e : tr) traced = traced || (e.type == EntityType::Polyline && e.points.size() > 3);
        check(traced, "dimension: traced, its line follows the run round its bends");
        s.dimTrace = false;
        s.dimSingle = true;
        auto single = total();
        int lines = 0, texts = 0;
        for (const Entity& e : single) {
            if (e.type == EntityType::Line) ++lines;
            if (e.type == EntityType::Text) ++texts;
        }
        check(lines >= 3 && texts == 1, "dimension: single-ended - a short line, an arrow, the figure over it");
    }

    // THE WINDOW: 2, 5, 7, 10, 11, 12.
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    BrickWizardDialog dlg(&ed);
    dlg.show();
    QApplication::processEvents();
    // 10. A brick from the palette lands in view.
    const QString x = dlg.addFromPalette(brickTypeKey(BrickType::End) + QStringLiteral("|") + partKindKey(PartKind::Connector));
    const QString f = dlg.addFromPalette(QStringLiteral("fanout|wire"));
    BrickCanvas* canvas = dlg.canvas();
    check(QRectF(canvas->rect()).intersects(canvas->brickRect(*dlg.design().brick(x))) &&
              QRectF(canvas->rect()).intersects(canvas->brickRect(*dlg.design().brick(f))),
          "palette: a brick added lands in view");
    check(!(canvas->brickRect(*dlg.design().brick(x)).intersects(canvas->brickRect(*dlg.design().brick(f)))),
          "palette: and not on top of another");
    // 2. The maps: a click on the pane's map goes there.
    auto* map = canvas->findChild<QToolButton*>(QStringLiteral("brickMap"));
    auto* pmap = dlg.preview()->findChild<QToolButton*>(QStringLiteral("previewMap"));
    check(map && pmap, "map: a map button on the pane and on the preview");
    if (map) map->click();
    const QPointF before = canvas->viewCentreGrid();
    mouse(canvas, QEvent::MouseButtonPress, QPointF(canvas->width() - 20.0, canvas->height() - 20.0));
    mouse(canvas, QEvent::MouseButtonRelease, QPointF(canvas->width() - 20.0, canvas->height() - 20.0));
    check(canvas->minimap() && canvas->viewCentreGrid() != before, "map: a click on it moves the view there");
    if (map) map->click();
    // ZOOM, EACH PANE ON ITS OWN, and a Fit button on each that brings everything back into view.
    {
        auto* fitB = canvas->findChild<QToolButton*>(QStringLiteral("brickFit"));
        auto* fitP = dlg.preview()->findChild<QToolButton*>(QStringLiteral("previewFit"));
        check(fitB && fitP, "zoom: a Fit button on the pane and on the preview");
        check(fitP && pmap && !fitP->geometry().intersects(pmap->geometry()),
              "zoom: the preview's Fit stands beside its Map, not over it");
        const double z0 = canvas->zoom();
        const QPointF at(canvas->width() / 2.0, canvas->height() / 2.0);
        for (int i = 0; i < 6; ++i) {
            QWheelEvent wheel(at, canvas->mapToGlobal(at), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                              Qt::NoScrollPhase, false);
            QApplication::sendEvent(canvas, &wheel);
        }
        std::printf("  zoom: pane %.2f -> %.2f, preview %.2f\n", z0, canvas->zoom(), dlg.preview()->zoom());
        check(canvas->zoom() > z0 * 2.0 && std::abs(dlg.preview()->zoom() - 1.0) < 1e-9,
              "zoom: the wheel zooms the pane, and only the pane");
        if (fitB) fitB->click();
        bool allIn = true;
        for (const Brick& b : dlg.design().bricks) {
            const QRectF r = canvas->brickRect(b);
            const QRectF onScreen(r.topLeft() * canvas->zoom(), r.size() * canvas->zoom());
            allIn = allIn && QRectF(canvas->rect()).contains(onScreen);
        }
        check(allIn, "zoom: Fit puts every brick in view");
        canvas->resetZoom();   // the clicks below are aimed at the pane's own size
    }
    // THE PREVIEW STAYS PUT when the drawing changes - choosing a part that is bigger or smaller changes the
    // drawing's extent, and a refit to it made the whole drawing jump to the middle.
    {
        auto line = [](Vec2 a, Vec2 b) {
            Entity l;
            l.type = EntityType::Line;
            l.a = a;
            l.b = b;
            return l;
        };
        DrawingPreview kept(QStringLiteral("kept"));
        kept.setKeepView(true);
        kept.resize(400, 300);
        kept.setEntities({line({0, 0}, {100, 0}), line({0, 0}, {0, 50})});
        kept.grab();
        const QPointF at(120.0, 140.0);
        const Vec2 w0 = kept.worldAt(at);
        kept.setEntities({line({0, 0}, {100, 0}), line({0, 0}, {0, 50}), line({100, 0}, {400, 200})});
        kept.grab();
        const Vec2 w1 = kept.worldAt(at);
        std::printf("  preview: point under the cursor (%.2f, %.2f) -> (%.2f, %.2f)\n", w0.x, w0.y, w1.x, w1.y);
        check(distance(w0, w1) < 1e-6, "preview: a bigger drawing does not move what is already shown");
        kept.fitToView();
        kept.grab();
        check(distance(w0, kept.worldAt(at)) > 1.0, "preview: and Fit still fits the new drawing");
    }
    // 7. The Auto buttons.
    dlg.design().connect(x, f);
    dlg.selectBrick(f);
    auto* fan = dlg.findChild<QDoubleSpinBox*>(QStringLiteral("brickFanStart"));
    auto* fanA = dlg.findChild<QToolButton*>(QStringLiteral("brickFanStartAuto"));
    check(fan && fanA && fanA->isChecked() && fan->value() > 0.0,
          "auto: left alone, \"A\" is lit and the field shows the fan's actual length");
    if (fan) fan->setValue(33.0);
    check(fanA && !fanA->isChecked() && dlg.design().brick(f)->run.fanStartMm == 33.0,
          "auto: a figure typed in takes it over");
    if (fanA) fanA->click();
    check(dlg.design().brick(f)->run.fanStartMm < 0.0, "auto: \"A\" gives it back to the drawing");
    // 5. The sections fold.
    auto* foldLabels = dlg.findChild<QToolButton*>(QStringLiteral("brickFoldLabels"));
    auto* labels = dlg.findChild<QTableWidget*>(QStringLiteral("brickLabels"));
    if (foldLabels) foldLabels->click();
    check(foldLabels && labels && !labels->isVisible(), "sections: a section folds away");
    if (foldLabels) foldLabels->click();
    check(labels && labels->isVisible(), "sections: and back");
    // FOLDED, A SECTION IS ITS HEADER, and the next follows straight after it.
    auto* foldStack = dlg.findChild<QToolButton*>(QStringLiteral("brickFoldStack"));
    // The section right after the stack is the branches and wire ends now.
    auto* foldDims = dlg.findChild<QToolButton*>(QStringLiteral("brickFoldBranches"));
    auto* foldNets = dlg.findChild<QToolButton*>(QStringLiteral("brickFoldNets"));
    auto yOf = [&dlg](QWidget* w) { return w->mapTo(&dlg, QPoint(0, 0)).y(); };
    if (foldStack && foldDims) {
        foldStack->click();
        QApplication::processEvents();
        const int gap = yOf(foldDims) - (yOf(foldStack) + foldStack->height());
        std::printf("  sections: folded, the next %d px after its header\n", gap);
        check(gap >= 0 && gap <= 8, "sections: folded to its header, the next right after it");
        foldStack->click();
        QApplication::processEvents();
    }
    // PULLED TALLER BY ITS GRIP, it pushes everything after it down.
    auto* grip = dlg.findChild<QWidget*>(QStringLiteral("brickFoldNetsGrip"));
    if (grip && foldNets && foldDims) {
        const int dimsWas = yOf(foldDims);
        const QPointF g(grip->width() / 2.0, 3.0);
        QMouseEvent press(QEvent::MouseButtonPress, g, grip->mapToGlobal(g), Qt::LeftButton, Qt::LeftButton,
                          Qt::NoModifier);
        QApplication::sendEvent(grip, &press);
        const QPointF to = g + QPointF(0, 60);
        QMouseEvent move(QEvent::MouseMove, to, grip->mapToGlobal(to), Qt::NoButton, Qt::LeftButton,
                         Qt::NoModifier);
        QApplication::sendEvent(grip, &move);
        QApplication::processEvents();
        std::printf("  sections: pulled 60 taller, the next moved %d\n", yOf(foldDims) - dimsWas);
        check(std::abs(yOf(foldDims) - dimsWas - 60) <= 2, "sections: pulled taller, it pushes the rest down");
    } else {
        check(false, "sections: a grip to pull a section taller");
    }
    // 11. A part from the list, assigned to the picked end part and put on the bill.
    QString pid;
    for (const LibraryPart* p : dlg.catalog().merged().ofKind(PartKind::Ferrule)) {
        pid = p->id;
        break;
    }
    if (!pid.isEmpty()) {
        dlg.setAsk([](const QString&) { return false; });
        dlg.selectBrick(x);
        check(dlg.assignPart(pid) && dlg.design().brick(x)->libPart == pid &&
                  dlg.design().brick(x)->kind == PartKind::Ferrule && dlg.bomItemOf(pid) > 0,
              "parts list: assigned to the picked brick - its part, its kind - and on the bill");
    }
    // 12. The bill moves.
    auto* bom = dlg.findChild<QTableWidget*>(QStringLiteral("brickBom"));
    const int keep = dlg.bomPlace();
    auto* box = dlg.findChild<QWidget*>(QStringLiteral("brickBomBox"));
    dlg.placeBom(2);
    check(box && qobject_cast<QSplitter*>(box->parentWidget()) && dlg.bomPlace() == 2,
          "bom: moved under the preview");
    dlg.placeBom(0);
    auto* tabs = box ? qobject_cast<QTabWidget*>(box->parentWidget() ? box->parentWidget()->parentWidget() : nullptr)
                     : nullptr;
    check(tabs && tabs->indexOf(box) >= 0, "bom: and back to a tab on the left");
    // THE LEFT'S BOTTOM HALF.
    dlg.placeBom(1);
    QApplication::processEvents();
    auto* left = box ? qobject_cast<QSplitter*>(box->parentWidget()) : nullptr;
    check(left && left->indexOf(box) == 1 && std::abs(left->sizes().value(0) - left->sizes().value(1)) <= 2,
          "bom: in the bottom half of the left");
    // REMOVED from the bill: the part assigned above.
    if (!pid.isEmpty()) {
        int row = -1;
        for (int i = 0; bom && i < bom->rowCount(); ++i)
            if (dlg.catalog().byId(pid) && bom->item(i, 3)->text() == dlg.catalog().byId(pid)->mfrPn) row = i;
        check(row >= 0 && dlg.removeFromBom(row) && dlg.bomItemOf(pid) == 0,
              "bom: a line taken off the bill");
    }

    // THE RIGHT-CLICK MENU: turning, moving, duplicating, unlinking, deleting what is picked.
    {
        dlg.selectBrick(f);
        QMenu menu;
        canvas->fillContextMenu(menu);
        QStringList said;
        std::function<void(QMenu*)> collect = [&](QMenu* m) {
            for (QAction* a : m->actions()) {
                if (a->menu()) collect(a->menu());
                else if (!a->isSeparator()) said << a->text().section(QLatin1Char('\t'), 0, 0);
            }
        };
        collect(&menu);
        std::printf("  menu: [%s]\n", qPrintable(said.join(QStringLiteral(", "))));
        check(said.contains(QStringLiteral("Rotate a quarter")) && said.contains(QStringLiteral("Left")) &&
                  said.contains(QStringLiteral("Delete")) && said.contains(QStringLiteral("Turn the fan round")),
              "menu: rotate, move, turn a fan round, delete");
        auto trigger = [&](const QString& text) {
            std::function<bool(QMenu*)> find = [&](QMenu* m) {
                for (QAction* a : m->actions()) {
                    if (a->menu() && find(a->menu())) return true;
                    if (a->text().section(QLatin1Char('\t'), 0, 0) == text) {
                        a->trigger();
                        return true;
                    }
                }
                return false;
            };
            find(&menu);
        };
        const int rotWas = dlg.design().brick(f)->rot;
        trigger(QStringLiteral("Rotate a quarter"));
        check(dlg.design().brick(f)->rot == (rotWas + 1) % 4, "menu: rotate turns it");
        const size_t count = dlg.design().bricks.size();
        trigger(QStringLiteral("Duplicate"));
        check(dlg.design().bricks.size() == count + 1, "menu: duplicate makes another");
    }
    dlg.placeBom(keep);
}

void mouse(QWidget* w, QEvent::Type t, const QPointF& at, Qt::KeyboardModifiers mods) {
    QMouseEvent e(t, at, w->mapToGlobal(at), Qt::LeftButton,
                  t == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, mods);
    QApplication::sendEvent(w, &e);
}

void testDialog() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    BrickWizardDialog dlg(&ed);
    dlg.show();
    QApplication::processEvents();

    // THE PALETTE: the end parts and the four specials.
    auto* palette = dlg.findChild<BrickPalette*>(QStringLiteral("brickPalette"));
    int ends = 0, specials = 0;
    for (int i = 0; palette && i < palette->count(); ++i) {
        const QString what = palette->item(i)->data(Qt::UserRole).toString();
        if (what.startsWith(QLatin1String("end|"))) ++ends;
        else if (!what.isEmpty()) ++specials;
    }
    std::printf("  wizard: palette of %d end part(s) and %d special(s)\n", ends, specials);
    check(ends >= 5 && specials == 7, "wizard: the palette holds the end parts, the four specials, the bridge wire, the branch and the joint");

    // A BRICK DROPPED ON THE PANE lands on the grid where it was dropped.
    BrickCanvas* canvas = dlg.canvas();
    QMimeData mime;
    mime.setData(QString::fromLatin1(kBrickMime), QByteArray("end|connector"));
    const QPointF dropAt = canvas->toScreen(0, 0) + QPointF(3, 2);
    QDragEnterEvent enter(dropAt.toPoint(), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &enter);
    QDropEvent drop(dropAt, Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &drop);
    check(dlg.design().bricks.size() == 1 && dlg.design().bricks[0].type == BrickType::End &&
              dlg.design().bricks[0].kind == PartKind::Connector &&
              near(dlg.design().bricks[0].col, 0.0) && near(dlg.design().bricks[0].row, 0.0),
          "wizard: a connector dropped on the pane lands on the grid where it was dropped");
    const QString x1 = dlg.design().bricks.empty() ? QString() : dlg.design().bricks[0].id;

    // Two more from the palette, and LINKED by dragging from a brick's port onto another.
    const QString run = dlg.addFromPalette(QStringLiteral("fanout|wire"));
    const QString f1 = dlg.addFromPalette(brickTypeKey(BrickType::End) + QStringLiteral("|") + partKindKey(PartKind::Ferrule));
    canvas->update();
    auto drag = [&](const QString& from, const QString& to, Qt::KeyboardModifiers mods) {
        const Brick* a = dlg.design().brick(from);
        const Brick* b = dlg.design().brick(to);
        const QPointF start = mods & Qt::ControlModifier ? canvas->brickRect(*a).center()
                                                         : canvas->portPoint(*a, dlg.design().facingPort(*a, QPointF(canvas->brickRect(*b).center() - canvas->brickRect(*a).center()).x() > 0 ? Vec2{1, 0} : Vec2{-1, 0}));
        mouse(canvas, QEvent::MouseButtonPress, start, mods);
        mouse(canvas, QEvent::MouseMove, canvas->brickRect(*b).center(), mods);
        mouse(canvas, QEvent::MouseButtonRelease, canvas->brickRect(*b).center(), mods);
    };
    drag(x1, run, Qt::NoModifier);
    drag(run, f1, Qt::ControlModifier);
    check(dlg.design().linked(x1, run) && dlg.design().linked(run, f1),
          "wizard: dragging from a brick's port - or with Ctrl - onto another links them");
    // Dragging a brick's middle MOVES it instead.
    const Brick* fb = dlg.design().brick(f1);
    const double wasCol = fb->col;
    const double wasRow = fb->row;
    const QPointF mid = canvas->brickRect(*fb).center();
    mouse(canvas, QEvent::MouseButtonPress, mid);
    mouse(canvas, QEvent::MouseMove, mid + QPointF(0, 58.0));
    mouse(canvas, QEvent::MouseButtonRelease, mid + QPointF(0, 58.0));
    check(near(dlg.design().brick(f1)->col, wasCol) && near(dlg.design().brick(f1)->row, wasRow + 1.0),
          "wizard: dragging a brick by its middle moves it a grid step");

    // THE PREVIEW is the real drawing: a run between the two parts.
    auto runs = [&]() {
        std::vector<Entity> out;
        for (const Entity& e : dlg.preview()->entities())
            if (e.type == EntityType::TwistedPair) out.push_back(e);
        return out;
    };
    check(runs().size() == 1, "wizard: the preview shows the run between the linked parts");

    // PICKING the run shows its settings; changing one changes the drawing.
    dlg.selectBrick(run);
    auto* panel = dlg.findChild<QStackedWidget*>(QStringLiteral("brickPanel"));
    auto* wires = dlg.findChild<QSpinBox*>(QStringLiteral("brickWires"));
    check(panel && panel->currentIndex() == 2 && wires, "wizard: a picked special shows its settings");
    if (wires) wires->setValue(6);
    check(dlg.design().brick(run)->run.wires == 6 && !runs().empty() &&
              runs().front().meta.value(QStringLiteral("twistWires")).toInt() == 6,
          "wizard: six wires set, six wires drawn");
    dlg.selectBrick(x1);
    check(panel && panel->currentIndex() == 1, "wizard: a picked end part shows its part");

    // MOVED with the buttons, then ARRANGED.
    dlg.selectBrick(f1);
    if (auto* right = dlg.findChild<QPushButton*>(QStringLiteral("brickRight"))) right->click();
    check(near(dlg.design().brick(f1)->col, wasCol + 1.0), "wizard: the arrow buttons move the picked brick");
    // SYMMETRIC LAYS OUT THE DRAWING, NOT THE BRICKS: they stay where they are; on the drawing a single run is
    // laid straight along the axis.
    {
        const double col0 = dlg.design().brick(f1)->col, row0 = dlg.design().brick(f1)->row;
        dlg.arrange();
        canvas->resetZoom();
        canvas->centreView();
        check(near(dlg.design().brick(f1)->col, col0) && near(dlg.design().brick(f1)->row, row0),
              "wizard: Symmetric leaves the bricks where they are");
        // On the drawing both parts on one row: each drawn at -row x pitch + its offset, the same for the two.
        const double pitch = buildBrickHarness(dlg.design(), &dlg.catalog()).rowPitch;
        const double yf = -dlg.design().brick(f1)->row * pitch + dlg.design().brick(f1)->drawDyMm;
        const double yx = -dlg.design().brick(x1)->row * pitch + dlg.design().brick(x1)->drawDyMm;
        // Where they are really set down - a part pulled across by a short run included.
        const BrickBuild laid = buildBrickHarness(dlg.design(), &dlg.catalog());
        std::printf("  symmetric: drawn at %.1f and %.1f (set down at %.1f and %.1f)\n", yf, yx, laid.partY.value(f1),
                    laid.partY.value(x1));
        check(std::abs(laid.partY.value(f1) - laid.partY.value(x1)) < 1.0,
              "wizard: and on the drawing a single run is laid straight along the axis");
    }

    // PLACED: onto the editor's cursor, the window out of the way.
    dlg.place();
    check(ed.placingPaste() && !dlg.isVisible(), "wizard: placing hands the drawing to the cursor");
    ed.cancelPastePlacement();

    // A link picked and deleted.
    dlg.selectBrick(QString());
    const Brick* ra = dlg.design().brick(run);
    const Brick* rb = dlg.design().brick(f1);
    // Halfway along the link itself - from port to port, wherever the bricks were left.
    const QPointF onLink = (canvas->portPoint(*ra, dlg.design().portOn(run, f1)) +
                            canvas->portPoint(*rb, dlg.design().portOn(f1, run))) /
                           2.0;
    mouse(canvas, QEvent::MouseButtonPress, onLink);
    mouse(canvas, QEvent::MouseButtonRelease, onLink);
    canvas->deleteSelected();
    check(!dlg.design().linked(run, f1), "wizard: a link picked on the pane is deleted");
}

// Picking several, turning, a fanout's wires per part, the bill, and a run's extras - through the window.
void testDialogMore() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    BrickWizardDialog dlg(&ed);
    dlg.show();
    QApplication::processEvents();
    BrickCanvas* canvas = dlg.canvas();
    BrickDesign& d = dlg.design();
    const QString x1 = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
    const QString fan = d.addBrick(BrickType::Fanout, PartKind::Wire, 1, 0);
    const QString f1 = d.addBrick(BrickType::End, PartKind::Ferrule, 2, -1);
    const QString f2 = d.addBrick(BrickType::End, PartKind::Ferrule, 2, 1);
    d.connect(x1, fan);
    d.connect(fan, f1);
    d.connect(fan, f2);
    d.brick(fan)->run.wires = 6;
    canvas->centreView();   // in view at the pane's own size - the clicks below are aimed at that
    canvas->update();
    QApplication::processEvents();

    // SCALED TO ANOTHER PART: the ferrule's entry made as long as the connector's.
    {
        check(dlg.scalePartToMatch(f1, x1), "match: a part scaled to another brick");
        const BrickBuild b = buildBrickHarness(d, nullptr);
        double fl = 0.0, xl = 0.0;
        for (auto it = b.faces.cbegin(); it != b.faces.cend(); ++it) {
            if (it.key().startsWith(f1 + QLatin1Char(':'))) fl = it.value().lengthMm;
            if (it.key().startsWith(x1 + QLatin1Char(':'))) xl = it.value().lengthMm;
        }
        std::printf("  match: ferrule entry %.2f, connector entry %.2f\n", fl, xl);
        check(xl > 0.0 && std::abs(fl - xl * 0.5) < 0.02 * xl, "match: a ferrule matched to a connector - its entry half the connector's");
        d.brick(f1)->scale = 1.0;
    }

    // COPY AND PASTE: a brick with its part and settings, and the links between those picked.
    {
        d.brick(f1)->libPart = QStringLiteral("some-part");
        d.brick(f1)->name = QStringLiteral("F1");
        const size_t bricksWere = d.bricks.size(), linksWere = d.links.size();
        canvas->setSelection({fan, f1});
        canvas->copySelection();
        canvas->pasteClipboard();
        check(d.bricks.size() == bricksWere + 2 && d.links.size() == linksWere + 1,
              "paste: the two bricks come back, and the link between them");
        bool keptPart = false;
        for (const QString& id : canvas->selection())
            if (const Brick* b = d.brick(id); b && b->libPart == QStringLiteral("some-part")) keptPart = true;
        check(keptPart, "paste: a pasted brick keeps its part");
        for (const QString& id : canvas->selection()) d.removeBrick(id);
        canvas->setSelection({});
        d.brick(f1)->libPart.clear();
        d.brick(f1)->name.clear();
    }

    // A BLUE ZONE, left to right: only what is wholly inside - the two ferrules.
    const QRectF r1 = canvas->brickRect(*d.brick(f1)), r2 = canvas->brickRect(*d.brick(f2));
    const QPointF a = r1.topLeft() - QPointF(6, 6), b = r2.bottomRight() + QPointF(6, 6);
    mouse(canvas, QEvent::MouseButtonPress, a);
    mouse(canvas, QEvent::MouseMove, b);
    mouse(canvas, QEvent::MouseButtonRelease, b);
    QStringList sel = canvas->selection();
    std::printf("  zone: window picked %d brick(s)\n", int(sel.size()));
    check(sel.size() == 2 && sel.contains(f1) && sel.contains(f2),
          "zone: left to right picks what is wholly inside it");
    // A GREEN ZONE, right to left, just touching the fanout and a ferrule: both.
    const QRectF rf = canvas->brickRect(*d.brick(fan));
    const QPointF c0(r1.right() + 12.0, r1.center().y()), c1(rf.center().x(), rf.center().y());
    mouse(canvas, QEvent::MouseButtonPress, QPointF(c0.x(), c0.y() - 2));
    mouse(canvas, QEvent::MouseMove, QPointF(c1.x(), c1.y() + 2));
    mouse(canvas, QEvent::MouseButtonRelease, QPointF(c1.x(), c1.y() + 2));
    sel = canvas->selection();
    check(sel.contains(fan) && sel.contains(f1) && !sel.contains(x1),
          "zone: right to left picks whatever it touches");
    // THE PICKED MOVE TOGETHER, and TURN together.
    const double c1Was = d.brick(f1)->col, cfWas = d.brick(fan)->col;
    dlg.moveSelected(0.0, 1.0);
    check(near(d.brick(f1)->row, 0.0) && near(d.brick(fan)->row, 1.0) &&
              near(d.brick(f1)->col, c1Was) && near(d.brick(fan)->col, cfWas),
          "move: everything picked moves together");
    const int f1Rot = d.brick(f1)->rot, fanRot = d.brick(fan)->rot, x1Rot = d.brick(x1)->rot;
    if (auto* rot = dlg.findChild<QPushButton*>(QStringLiteral("brickRotate"))) rot->click();
    check(d.brick(f1)->rot == (f1Rot + 1) % 4 && d.brick(fan)->rot == (fanRot + 1) % 4 && d.brick(x1)->rot == x1Rot,
          "rotate: the bar button turns what is picked a quarter");
    dlg.arrange();   // Symmetric no longer touches the bricks: turned, they stay turned
    canvas->resetZoom();
    canvas->centreView();
    check(d.brick(f1)->rot != f1Rot || d.brick(fan)->rot != fanRot, "symmetric: the bricks are not turned or moved by it");
    d.brick(f1)->rot = f1Rot;   // back, for the clicks below
    d.brick(fan)->rot = fanRot;

    // THE FANOUT'S WIRES PER PART: picking its link to a ferrule, and saying two.
    canvas->setSelection({});
    const QPointF pa = canvas->portPoint(*d.brick(fan), kPortRight);
    const QPointF pb = canvas->portPoint(*d.brick(f1), kPortLeft);
    mouse(canvas, QEvent::MouseButtonPress, (pa + pb) / 2.0);
    mouse(canvas, QEvent::MouseButtonRelease, (pa + pb) / 2.0);
    auto* panel = dlg.findChild<QStackedWidget*>(QStringLiteral("brickPanel"));
    auto* linkWires = dlg.findChild<QSpinBox*>(QStringLiteral("brickLinkWires"));
    check(panel && panel->currentIndex() == 3 && linkWires, "fanout link: picked, it shows its wires");
    if (linkWires) linkWires->setValue(2);
    check(d.link(fan, f1) && d.link(fan, f1)->wires == 2, "fanout link: two wires to that ferrule");
    // f1 is the upper ferrule: its branch ends higher up the sheet.
    std::vector<std::pair<double, int>> ends;
    for (const Entity& e : dlg.preview()->entities())
        if (e.type == EntityType::TwistedPair && e.meta.value(QStringLiteral("brickId")).toString() == fan &&
            !e.meta.contains(QStringLiteral("brickTrunk")))
            ends.push_back({e.points.back().y, e.meta.value(QStringLiteral("twistWires")).toInt()});
    std::sort(ends.begin(), ends.end());
    const int toF1 = ends.size() == 2 ? ends[1].second : 0;
    const int toF2 = ends.size() == 2 ? ends[0].second : 0;
    std::printf("  fanout link: %d wire(s) to one ferrule, %d to the other\n", toF1, toF2);
    check(toF1 == 2 && toF2 == 4, "fanout link: the preview draws two to it and four to the other");

    // THE BILL: a part added from the end's panel, marked, and asked about before a second time.
    PartsLibrary lib;
    const PartsLibrary all = dlg.catalog().merged();
    QString partId;
    for (const LibraryPart* p : all.ofKind(PartKind::Connector)) {
        partId = p->id;
        break;
    }
    if (!partId.isEmpty()) {
        d.brick(x1)->libPart = partId;
        d.brick(x1)->balloon = true;
        int asked = 0;
        dlg.setAsk([&asked](const QString&) {
            ++asked;
            return false;
        });
        check(dlg.bomItemOf(partId) == 0, "bom: not in the bill to begin with");
        dlg.selectBrick(x1);
        if (auto* add = dlg.findChild<QPushButton*>(QStringLiteral("brickBomAdd"))) add->click();
        const int item = dlg.bomItemOf(partId);
        auto* state = dlg.findChild<QLabel*>(QStringLiteral("brickBomState"));
        std::printf("  bom: in the bill as item %d - \"%s\"\n", item,
                    state ? qPrintable(state->text()) : "");
        check(item > 0 && state && state->text().contains(QString::number(item)),
              "bom: added from the part's panel, and marked with its line");
        const bool again = dlg.addPartToBom(partId);
        check(asked >= 1 && !again, "bom: already there, it asks - and a no adds nothing");
        // The balloon on the part says that line.
        QString said;
        for (const Entity& e : dlg.drawing())
            if (e.meta.value(QStringLiteral("balloon")).toBool()) said = e.meta.value(QStringLiteral("text")).toString();
        check(said == QString::number(item), "balloon: numbered with the part's line of the bill");
        // FROM THE PARTS LIST: pick a line, Add to BOM.
        auto* parts = dlg.findChild<QTreeWidget*>(QStringLiteral("brickParts"));
        QString other;
        for (int i = 0; parts && i < parts->topLevelItemCount(); ++i) {
            const QString id = parts->topLevelItem(i)->data(0, Qt::UserRole).toString();
            if (id != partId) {
                other = id;
                parts->topLevelItem(i)->setSelected(true);
                break;
            }
        }
        if (auto* add = dlg.findChild<QPushButton*>(QStringLiteral("brickPartsToBom")); add && !other.isEmpty())
            add->click();
        check(!other.isEmpty() && dlg.bomItemOf(other) > 0, "bom: a part added from the parts list");
        // DRAGGED TO A NEW PLACE: the second line put first - the item numbers and the balloon follow.
        if (!other.isEmpty()) {
            const int wasPart = dlg.bomItemOf(partId), wasOther = dlg.bomItemOf(other);
            check(dlg.findChild<QWidget*>(QStringLiteral("brickBomDropLine")) != nullptr,
                  "bom order: a white line to show where a dragged line goes");
            const bool moved = dlg.moveBomRow(wasOther - 1, wasPart - 1);
            std::printf("  bom order: part %d -> %d, other %d -> %d\n", wasPart, dlg.bomItemOf(partId), wasOther,
                        dlg.bomItemOf(other));
            check(moved && dlg.bomItemOf(other) == wasPart && dlg.bomItemOf(partId) == wasPart + 1,
                  "bom order: a line dragged up takes the place, the others make room");
            QString said2;
            for (const Entity& e : dlg.drawing())
                if (e.meta.value(QStringLiteral("balloon")).toBool()) said2 = e.meta.value(QStringLiteral("text")).toString();
            check(said2 == QString::number(dlg.bomItemOf(partId)), "bom order: and the balloon is renumbered with it");
        }
    } else {
        std::printf("  bom: no connector in the library here - the bill checks are skipped\n");
    }

    // A RUN'S EXTRAS through its panel: a heatshrink over a braid, the dimensions, a label.
    dlg.selectBrick(fan);
    auto* addLayer = dlg.findChild<QComboBox*>(QStringLiteral("brickStackAdd"));
    if (addLayer) {
        addLayer->setCurrentIndex(addLayer->findData(QStringLiteral("braid")));
        emit addLayer->activated(addLayer->findData(QStringLiteral("braid")));
        emit addLayer->activated(addLayer->findData(QStringLiteral("heatshrink")));
    }
    check(d.brick(fan)->run.outermost() == QLatin1String("heatshrink") &&
              d.brick(fan)->run.stack.contains(QStringLiteral("braid")),
          "stack: layers added through the panel, the last on outermost");
    auto* stack = dlg.findChild<QListWidget*>(QStringLiteral("brickStack"));
    check(stack && stack->count() == 3 && stack->item(0)->text().contains(QStringLiteral("Wires")) &&
              stack->item(2)->text().contains(QStringLiteral("Heatshrink")),
          "stack: listed from the inside out - the wires first, the one seen last");
    if (stack) stack->setCurrentRow(1);   // the braid
    if (auto* out = dlg.findChild<QPushButton*>(QStringLiteral("brickStackOut"))) out->click();
    check(d.brick(fan)->run.outermost() == QLatin1String("braid"), "stack: moved outward, the braid is seen");
    if (auto* dt = dlg.findChild<QCheckBox*>(QStringLiteral("brickDimTotal"))) dt->setChecked(true);
    if (auto* addLab = dlg.findChild<QPushButton*>(QStringLiteral("brickLabelAdd"))) addLab->click();
    check(d.brick(fan)->run.dimTotal && d.brick(fan)->run.labels.size() == 1,
          "run panel: a dimension ticked and a label added");
    auto* from = dlg.findChild<QComboBox*>(QStringLiteral("brickDimFrom"));
    if (from) from->setCurrentIndex(1);
    check(d.dimFromFar, "dimensions: the general menu takes them from the far point");
}

} // namespace

// ROUND SIX: parts on other rows spread out along the harness by what is left of the run's length, and a
// run's end with no fan goes into its part as the bundle.
void testRoundSix() {
    BrickDesign d;
    const QString x0 = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
    const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
    const QString x1 = d.addBrick(BrickType::End, PartKind::Connector, 2, 1);
    QString why;
    check(d.connect(x0, c, &why) && d.connect(c, x1, &why), "spread: two connectors a row apart, on one cable");
    d.brick(c)->run.lengthMm = 300.0;
    auto runOf = [&c](const BrickBuild& b) {
        for (const Entity& e : b.entities)
            if (e.type == EntityType::TwistedPair && e.meta.value(QStringLiteral("brickId")).toString() == c)
                return e;
        return Entity{};
    };
    {
        const BrickBuild b = buildBrickHarness(d, nullptr);
        const BrickFace fa = b.faces.value(QStringLiteral("%1:0").arg(x0));
        const BrickFace fb = b.faces.value(QStringLiteral("%1:0").arg(x1));
        const double along = std::abs(fb.centre.x - fa.centre.x);
        const double across = std::abs(fb.centre.y - fa.centre.y);
        std::printf("  spread: faces %.1f along and %.1f across for a 300 mm run\n", along, across);
        check(across > 1.0 && std::abs(along + across * (std::sqrt(2.0) - 1.0) - 300.0) < 1.0,
              "spread: the far part is as far along as the run's length less the climb across, on the slant");
        const Entity r = runOf(b);
        bool forward = r.points.size() >= 2;
        for (size_t i = 1; i < r.points.size(); ++i)
            if (r.points[i].x < r.points[i - 1].x - 1e-6) forward = false;
        check(forward, "spread: the wires never turn back to reach it");
        check(r.meta.value(QStringLiteral("twistSpread")).toString() == QStringLiteral("both"),
              "fan: by default the run fans at both parts");
    }
    {
        d.brick(c)->run.fanFinishMm = 0.0;
        const Entity r = runOf(buildBrickHarness(d, nullptr));
        check(r.meta.value(QStringLiteral("twistSpread")).toString() == QStringLiteral("start") &&
                  r.meta.value(QStringLiteral("twistEndsFinish")).toDouble() == 0.0,
              "fan: with no fan at the finish, the wires go into that part as the bundle");
        d.brick(c)->run.fanFinishMm = -1.0;
    }
    // THE FAN BUTTON beside the "A".
    {
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        BrickWizardDialog dlg(&ed);
        dlg.show();
        dlg.design().bricks = d.bricks;
        dlg.design().links = d.links;
        dlg.selectBrick(c);
        QApplication::processEvents();
        auto* on = dlg.findChild<QToolButton*>(QStringLiteral("brickFanFinishOn"));
        auto* len = dlg.findChild<QDoubleSpinBox*>(QStringLiteral("brickFanFinish"));
        check(on && on->isChecked() && len && len->isEnabled(), "fan: a Fan button beside the finish length, lit");
        if (on) on->click();
        check(dlg.design().brick(c)->run.fanFinishMm == 0.0 && len && !len->isEnabled(),
              "fan: off, the run has no fan at that end and the length is not asked for");
        if (on) on->click();
        check(dlg.design().brick(c)->run.fanFinishMm < 0.0 && len && len->isEnabled(),
              "fan: on again, the fan is back and the drawing sizes it");
    }
}

// ROUND SEVEN: routes with as few bends as there can be, designators that can be put away, and a transition
// heatshrink with a width of its own that never goes below the cable's.
void testRoundSeven() {
    // A LEG ON THE SLANT into a part ahead: one bend, where the two lines meet.
    {
        const Vec2 u = Vec2{1.0, 1.0}.normalized();
        const std::vector<Vec2> r = routeBetweenFaces({0, 0}, u, {150, 50}, {-1, 0}, 15.0);
        std::printf("  bends: %d points on the slant into a part ahead\n", static_cast<int>(r.size()));
        check(r.size() == 3 && std::abs(r[1].x - 50.0) < 1e-6 && std::abs(r[1].y - 50.0) < 1e-6,
              "bends: a leg on the slant makes one bend into the part");
        // Too close for that: across to the other's lead, never back on itself.
        const std::vector<Vec2> t = routeBetweenFaces({0, 0}, u, {30, 50}, {-1, 0}, 15.0);
        bool back = false;
        for (size_t i = 1; i + 1 < t.size(); ++i)
            if (t[i + 1].x < t[i].x - 1e-6 && t[i + 1].x > 30.0 + 15.0 + 1e-6) back = true;
        check(!back && t.back().x == 30.0, "bends: too close, it goes across - no hook");
    }
    // LEVEL BUT FOR A HAIR: straight across, not a level shift of two bends.
    {
        const std::vector<Vec2> r = routeBetweenFaces({0, 0}, {1, 0}, {150, 1.5}, {-1, 0}, 15.0);
        check(r.size() == 2, "bends: faces a millimetre and a half apart are joined straight");
        const std::vector<Vec2> s = routeBetweenFaces({0, 0}, {1, 0}, {150, 20}, {-1, 0}, 15.0);
        check(s.size() > 2, "bends: a real step across is still a level shift");
    }
    BrickDesign d;
    const QString x0 = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
    const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
    const QString x1 = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
    d.brick(x0)->name = QStringLiteral("P1");
    d.brick(x1)->name = QStringLiteral("P2");
    d.connect(x0, c);
    d.connect(c, x1);
    d.brick(c)->run.stack = {QStringLiteral("wires"), QStringLiteral("cable")};
    d.brick(c)->run.transitions = RunEnds::Both;
    auto designators = [&d] {
        int n = 0;
        for (const Entity& e : buildBrickHarness(d, nullptr).entities)
            if (e.meta.value(QStringLiteral("mechRole")).toString() == QStringLiteral("designator")) ++n;
        return n;
    };
    check(designators() == 0, "designators: not written until a part's eye is on");
    d.brick(x0)->showDesignator = true;
    d.brick(x0)->designatorHeightMm = 6.0;
    check(designators() == 1, "designators: written for the part whose eye is on, and only that one");
    double tall = 0.0;
    for (const Entity& e : buildBrickHarness(d, nullptr).entities)
        if (e.meta.value(QStringLiteral("mechRole")).toString() == QStringLiteral("designator"))
            tall = e.meta.value(QStringLiteral("size")).toDouble(e.meta.value(QStringLiteral("height")).toDouble());
    std::printf("  designators: written %.2f tall\n", tall);
    check(std::abs(tall - 6.0) < 1e-9, "designators: at the height the part says");
    {
        const BrickDesign back = BrickDesign::fromJson(d.toJson());
        check(back.brick(x0)->showDesignator && !back.brick(x1)->showDesignator &&
                  std::abs(back.brick(x0)->designatorHeightMm - 6.0) < 1e-9,
              "designators: the eye and the height are kept with the design");
    }
    d.brick(x0)->showDesignator = false;
    auto bootOf = [&d, &c](double& body) {
        for (const Entity& e : buildBrickHarness(d, nullptr).entities)
            if (e.type == EntityType::TwistedPair && e.meta.value(QStringLiteral("brickId")).toString() == c) {
                body = e.meta.value(QStringLiteral("twistBodyWidth")).toDouble();
                return e.meta.contains(QStringLiteral("twistBootWidth"))
                           ? e.meta.value(QStringLiteral("twistBootWidth")).toDouble()
                           : -1.0;
            }
        return -2.0;
    };
    double body = 0.0;
    // Left alone: the body's width and a quarter, whatever the wires going into it.
    {
        const double w = bootOf(body);
        std::printf("  transition: left alone %.2f on a cable %.2f wide\n", w, body);
        check(w == -1.0 || std::abs(w - body * 1.25) < 1e-9, "transition: left alone, the body's width and a quarter");
    }
    d.brick(c)->run.transitionWidthMm = 0.5;
    const double held = bootOf(body);
    std::printf("  transition: asked 0.5, drawn %.2f on a cable %.2f wide\n", held, body);
    check(body > 0.5 && std::abs(held - body) < 1e-9, "transition: asked narrower than the cable, it is the cable's width");
    d.brick(c)->run.transitionWidthMm = body + 4.0;
    check(std::abs(bootOf(body) - (body + 4.0)) < 1e-9, "transition: wider, it is as wide as asked");
    check(std::abs(BrickDesign::fromJson(d.toJson()).brick(c)->run.transitionWidthMm - (body + 4.0)) < 1e-9,
          "transition: the width is kept with the design");
    // THE WIZARD: the eye, the width field and the Symmetric button.
    {
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        BrickWizardDialog dlg(&ed);
        dlg.show();
        dlg.design().bricks = d.bricks;
        dlg.design().links = d.links;
        dlg.selectBrick(c);
        QApplication::processEvents();
        check(!dlg.findChild<QToolButton*>(QStringLiteral("brickShowDesignators")),
              "wizard: no eye for every designator at once on the bar");
        dlg.selectBrick(x0);
        QApplication::processEvents();
        auto* eye = dlg.findChild<QToolButton*>(QStringLiteral("brickShowDesignator"));
        auto* height = dlg.findChild<QDoubleSpinBox*>(QStringLiteral("brickDesignatorHeight"));
        auto* name = dlg.findChild<QLineEdit*>(QStringLiteral("brickName"));
        check(eye && height && name && !eye->isChecked() &&
                  std::abs(eye->mapTo(&dlg, QPoint(0, 0)).y() - name->mapTo(&dlg, QPoint(0, 0)).y()) < 12,
              "wizard: the eye is on the part's Designator line, off to begin with");
        if (eye) eye->click();
        if (height) height->setValue(5.0);
        check(dlg.design().brick(x0)->showDesignator && std::abs(dlg.design().brick(x0)->designatorHeightMm - 5.0) < 1e-9,
              "wizard: pressed, that part's designator is written, as tall as asked");
        dlg.selectBrick(c);
        QApplication::processEvents();
        auto* width = dlg.findChild<QDoubleSpinBox*>(QStringLiteral("brickTransitionWidth"));
        auto* widthAuto = dlg.findChild<QToolButton*>(QStringLiteral("brickTransitionWidthAuto"));
        check(width && widthAuto && width->isEnabled(), "wizard: a transition width, with an Auto");
        if (width) {
            width->setValue(0.0);   // turned right down: it stops at the cable
            std::printf("  wizard: transition width turned down to %.2f\n", dlg.design().brick(c)->run.transitionWidthMm);
            check(dlg.design().brick(c)->run.transitionWidthMm >= body - 1e-6,
                  "wizard: turned below the cable, the width stops at the cable's");
        }
        auto* sym = dlg.findChild<QPushButton*>(QStringLiteral("brickArrange"));
        check(sym && sym->toolTip().startsWith(QStringLiteral("Symmetric")), "wizard: a Symmetric button");
    }
    // SYMMETRIC WITH NOTHING LINKED YET: one connector on the left, two on the right, dropped anywhere - the two
    // are put the same distance either side of the one.
    {
        BrickDesign s;
        const QString p1 = s.addBrick(BrickType::End, PartKind::Connector, 0, 3.0);
        const QString p2 = s.addBrick(BrickType::End, PartKind::Connector, 6, 0.0);
        const QString p3 = s.addBrick(BrickType::End, PartKind::Connector, 6, 4.5);
        arrangeBricks(s);
        const double r1 = s.brick(p1)->row, r2 = s.brick(p2)->row, r3 = s.brick(p3)->row;
        std::printf("  symmetric: rows %.1f / %.1f, %.1f\n", r1, r2, r3);
        check(std::abs((r2 + r3) / 2.0 - r1) < 1e-9 && r3 - r2 > 1.0 && s.brick(p2)->col == 6.0,
              "symmetric: parts not linked yet are spread evenly either side of one axis, in their columns");
    }
}

// A PART DRAWN TO PROPORTION, sized by its entry face: known, from its figures, or left to the drawing.
void testPartSizeByEntry() {
    PartsCatalog cat;
    cat.setRemembering(false);
    QString pid;
    {
        LibraryPart& p = cat.active().add(PartKind::Connector);
        pid = p.id;
        Entity body;
        body.type = EntityType::Polyline;
        body.points = {{0, -5}, {20, -5}, {20, 5}, {0, 5}, {0, -5}};
        body.a = body.points.front();
        body.b = body.points.back();
        p.mechanical = partDrawingFromEntities({body});
        p.outputs = {{{20, -4}, {20, 4}}};   // an entry face 8 long
    }
    LibraryPart& p = *cat.active().byId(pid);
    check(!partIsNominal(p) && std::abs(partSizeFactor(p) - 1.0) < 1e-12 && std::abs(partDrawnEntryMm(p) - 8.0) < 1e-9,
          "size: as drawn, the drawing is the part's size");
    setPartSizeMode(p, PartSizeMode::Known);
    setPartSizeKnownMm(p, 12.0);
    const LibraryPart sized = partSized(p);
    Bounds bb;
    for (const Entity& e : partDrawingEntities(sized.mechanical)) bb.expand(e.bounds());
    std::printf("  size: known 12 on a face drawn 8 - x%.2f, the part %.1f long\n", partSizeFactor(p), bb.width());
    check(partIsNominal(p) && std::abs(partSizeFactor(p) - 1.5) < 1e-9 &&
              std::abs(distance(sized.outputs.front().a, sized.outputs.front().b) - 12.0) < 1e-9 &&
              std::abs(bb.width() - 30.0) < 1e-6,
          "size: the entry face known, the whole drawing is scaled so the face is that long");
    setPartSizeMode(p, PartSizeMode::FromParams);
    check(std::abs(partSizeFactor(p) - 1.0) < 1e-12, "size: from its figures with none given, it stays as drawn");
    p.params[QStringLiteral("cableEntryMin")] = QStringLiteral("6");
    p.params[QStringLiteral("cableEntryMax")] = QStringLiteral("10");
    check(std::abs(partEntryFromParamsMm(p) - 10.0) < 1e-9 && std::abs(partSizeFactor(p) - 1.25) < 1e-9,
          "size: from its figures, by the most its cable entry takes");
    // PLACED from the library at that size, and its faces with it.
    {
        Bounds put;
        for (const Entity& e : partBlockInstance(p, true, Vec2{0, 0})) put.expand(e.bounds());
        check(std::abs(put.width() - 25.0) < 1e-6, "size: placed from the library, it goes down at that size");
    }
    // IN THE HARNESS: the part's face is the size it was given.
    BrickDesign d;
    d.catalog = &cat;
    const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
    const QString r = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
    const QString y = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
    d.connect(x, r);
    d.connect(r, y);
    d.brick(x)->libPart = pid;
    {
        const BrickBuild b = buildBrickHarness(d, &cat);
        const double face = b.faces.value(QStringLiteral("%1:0").arg(x)).lengthMm;
        std::printf("  size: in the harness its face is %.2f\n", face);
        check(std::abs(face - 10.0) < 1e-6, "size: in the harness, the part is drawn at its size");
    }
    // FROM THE FAR POINT OF ANY PART, a nominal one too: as the window says, whatever the part.
    {
        d.brick(r)->run.dimTotal = true;
        d.dimFromFar = true;
        auto said = [&] {
            for (const Entity& e : buildBrickHarness(d, &cat).entities)
                if (e.meta.value(QStringLiteral("brickDim")).toString() == QStringLiteral("total"))
                    return e.meta.value(QStringLiteral("brickMeasured")).toDouble();
            return -1.0;
        };
        const double far = said();
        d.dimFromFar = false;
        const double near = said();
        std::printf("  size: on the nominal part from its far point %.1f, from its near point %.1f\n", far, near);
        check(near > 0.0 && far > near + 1.0, "size: the far point of a nominal part is measured from, as asked");
        d.brick(r)->run.dimTotal = false;
    }
    // AUTO: chosen in the wizard, it comes in on Auto.
    {
        setPartSizeMode(p, PartSizeMode::Auto);
        check(partIsNominal(p) && std::abs(partSizeFactor(p) - 1.0) < 1e-12,
              "size: on Auto the library leaves it as drawn - the harness sizes it");
    }
}

// A FERRULE MARKED ONLY WITH A CONNECTION POINT, its arrow running into the body (how a new part starts marked):
// in the harness its connection is on the side its brick is joined on, and the wires do not run through it.
void testConnectPointFacesBrick() {
    PartsCatalog cat;
    cat.setRemembering(false);
    QString pid;
    {
        LibraryPart& p = cat.active().add(PartKind::Ferrule);
        pid = p.id;
        Entity body;
        body.type = EntityType::Polyline;
        body.points = {{0, -3}, {20, -3}, {20, 3}, {0, 3}, {0, -3}};
        body.a = body.points.front();
        body.b = body.points.back();
        p.mechanical = partDrawingFromEntities({body});
        p.hasConnect = true;
        p.connectPoint = Vec2{0, 0};
        p.connectAngleRad = 0.0;   // into the body
    }
    BrickDesign d;
    d.catalog = &cat;
    const QString f = d.addBrick(BrickType::End, PartKind::Ferrule, 0, 0);
    d.brick(f)->libPart = pid;
    const BrickShape s = d.shapeOf(*d.brick(f));
    Bounds bb;
    for (const Entity& e : s.drawing) bb.expand(e.bounds());
    std::printf("  ferrule: face at %.1f looking %.0f, the body %.1f..%.1f\n", s.faceMid.empty() ? 0.0 : s.faceMid[0].x,
                s.faceOut.empty() ? 0.0 : s.faceOut[0].x, bb.min.x, bb.max.x);
    check(s.ports() == 1 && s.faceOut[0].x > 0.5 && std::abs(s.faceMid[0].x - bb.max.x) < 1e-6,
          "ferrule: its connection is on the end its brick's port looks out of, not the far end");
    // Joined on the left of a run: the wires come in from the right, onto the connection.
    const QString x = d.addBrick(BrickType::End, PartKind::Connector, 3, 0);
    const QString r = d.addBrick(BrickType::Fanout, PartKind::Wire, 1.5, 0);
    d.connect(f, r);
    d.connect(r, x);
    const BrickBuild b = buildBrickHarness(d, &cat);
    const BrickFace face = b.faces.value(QStringLiteral("%1:0").arg(f));
    Bounds part;
    for (const Entity& e : b.entities)
        if (e.meta.value(QStringLiteral("brickId")).toString() == f && e.type == EntityType::Polyline) part.expand(e.bounds());
    check(part.valid && face.out.x > 0.5 && std::abs(face.centre.x - part.max.x) < 1e-6,
          "ferrule: on the drawing, the run meets it at its connection, facing the run");

    // NEAR AND FAR SAY THE WAY, whatever the arrow says; the CONNECTION POINT is where it sits; the ENTRY FACE is
    // only where the wires land. Near on the left, far on the right, the arrow pointing right, the face in the middle.
    {
        LibraryPart& p = *cat.active().byId(pid);
        p.hasNearPoint = true;
        p.nearPoint = Vec2{0, 0};
        p.hasFarPoint = true;
        p.farPoint = Vec2{20, 0};
        p.outputs = {{{8, -2}, {8, 2}}};
        d.shapeCache.clear();
        const BrickShape t = d.shapeOf(*d.brick(f));
        Bounds tb;
        for (const Entity& e : t.drawing) tb.expand(e.bounds());
        std::printf("  marks: near %.1f far %.1f, face %.1f looking %.0f, body %.1f..%.1f\n", t.nearAt.x, t.farAt.x,
                    t.faceMid.empty() ? 0.0 : t.faceMid[0].x, t.faceOut.empty() ? 0.0 : t.faceOut[0].x, tb.min.x, tb.max.x);
        check(t.haveEnds && t.nearAt.x > t.farAt.x && std::abs(t.nearAt.x - tb.max.x) < 1e-6,
              "marks: near-far turned to look out of the port - the near end toward the harness");
        check(t.ports() == 1 && t.faceOut[0].x > 0.5 && t.faceMid[0].x < tb.max.x - 1.0 && t.faceMid[0].x > tb.min.x + 1.0,
              "marks: and the wires land on the entry face where it is on the part, not at its end");
        p.hasNearPoint = p.hasFarPoint = false;
        p.outputs.clear();
        d.shapeCache.clear();
    }
}

// LENGTHS ON THE PANE, the bend where the special sits, the wires gathered with no cable, and bridge wires.
void testLengthsAndBridges() {
    // 1. Linked, a part is put at the run's length from the other; moved, the length follows; the length
    //    changed, the part - and what is beyond it - is pulled or pushed along.
    BrickDesign d;
    const QString x0 = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
    const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
    const QString x1 = d.addBrick(BrickType::End, PartKind::Connector, 5, 0);
    const QString c2 = d.addBrick(BrickType::Run, PartKind::Cable, 6, 0);
    const QString x2 = d.addBrick(BrickType::End, PartKind::Connector, 7, 0);
    d.connect(x0, c);
    d.connect(c, x1);
    d.connect(x1, c2);
    d.connect(c2, x2);
    d.brick(c)->run.lengthMm = 300.0;
    check(fitEndToLength(d, c, x1) && std::abs(d.brick(x1)->col - 300.0 / kBrickMmPerCol) < 1e-9 &&
              std::abs(d.brick(c)->col - 2.0) < 1e-9,
          "lengths: linked, the part is put at the run's length from the other, the run halfway");
    d.brick(x1)->col = 6.0;   // dragged out to 450 mm
    check(lengthFromPositions(d, c, 0.5) && std::abs(d.brick(c)->run.lengthMm - 450.0) < 1e-9,
          "lengths: the part moved along, the run is as long as they are apart");
    const double x2Was = d.brick(x2)->col;
    const double old = d.brick(c)->run.lengthMm;
    d.brick(c)->run.lengthMm = 150.0;
    check(pushForLength(d, c, old) && std::abs(d.brick(x1)->col - 2.0) < 1e-9 &&
              std::abs(d.brick(x2)->col - (x2Was - 4.0)) < 1e-9 && d.brick(x0)->col == 0.0,
          "lengths: the run shortened pulls its part in, and everything beyond it, and leaves the start alone");
    d.brick(x1)->col = 9.0;
    d.brick(x2)->col = 20.0;
    fitPositionsToLengths(d);
    std::printf("  lengths: fitted to %.2f and %.2f\n", d.brick(x1)->col, d.brick(x2)->col);
    check(std::abs(std::abs(d.brick(x1)->col - d.brick(x0)->col) - 2.0) < 1e-9 &&
              std::abs(std::abs(d.brick(x2)->col - d.brick(x1)->col) - d.brick(c2)->run.lengthMm / kBrickMmPerCol) < 1e-9,
          "lengths: arranged, every part is put at its runs' lengths again");

    // 2. The bend where the special's brick sits between its parts.
    {
        const std::vector<Vec2> early = routeBetweenFaces({0, 0}, {1, 0}, {200, 40}, {-1, 0}, 10.0, 45.0, 0.2);
        const std::vector<Vec2> late = routeBetweenFaces({0, 0}, {1, 0}, {200, 40}, {-1, 0}, 10.0, 45.0, 0.8);
        std::printf("  bend: first bend at %.1f near the start, %.1f near the end\n", early.size() > 1 ? early[1].x : -1.0,
                    late.size() > 1 ? late[1].x : -1.0);
        check(early.size() == 4 && late.size() == 4 && early[1].x < 60.0 && late[1].x > 100.0,
              "bend: the slant goes where the special is between its parts");
        BrickDesign s;
        const QString a = s.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString r = s.addBrick(BrickType::Run, PartKind::Wire, 1, 0.5);
        const QString b = s.addBrick(BrickType::End, PartKind::Connector, 4, 1);
        s.connect(a, r);
        s.connect(r, b);
        auto firstBend = [&] {
            for (const Entity& e : buildBrickHarness(s, nullptr).entities)
                if (e.type == EntityType::TwistedPair && e.points.size() > 2) return e.points[1].x;
            return -1.0;
        };
        s.brick(r)->col = 0.6;
        const double nearStart = firstBend();
        s.brick(r)->col = 3.4;
        const double nearEnd = firstBend();
        check(nearStart >= 0.0 && nearEnd > nearStart + 20.0, "bend: moving the special's brick along moves its bend");
    }

    // 3. No cable: the wires gathered narrower than they fan, the fan filling the face.
    {
        BrickDesign w;
        const QString a = w.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString r = w.addBrick(BrickType::Run, PartKind::Wire, 1, 0);
        const QString b = w.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        w.connect(a, r);
        w.connect(r, b);
        w.brick(r)->run.wires = 6;
        w.brick(r)->run.stack = {QStringLiteral("wires")};
        for (const Entity& e : buildBrickHarness(w, nullptr).entities)
            if (e.type == EntityType::TwistedPair) {
                const double lane = e.meta.value(QStringLiteral("twistLane")).toDouble();
                const double pitch = e.meta.value(QStringLiteral("twistBodyPitch")).toDouble();
                std::printf("  no cable: fanned %.2f apart at the face, gathered %.2f apart\n", lane, pitch);
                check(pitch > 0.0 && pitch < lane, "no cable: the wires gather into a bundle and fan out to the face");
            }
    }

    // 4. Bridge wires.
    check(parseWireList(QStringLiteral("1-3,5")) == QList<int>({1, 2, 3, 5}) &&
              parseWireList(QStringLiteral("7-9, 2, 2"), 8) == QList<int>({7, 8, 2}),
          "bridge: \"1-3,5\" is wires 1, 2, 3 and 5 - none past the cable's, none twice");
    {
        BrickDesign j;
        const QString a = j.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString cab = j.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString fan = j.addBrick(BrickType::Fanout, PartKind::Wire, 2, 0);
        const QString f1 = j.addBrick(BrickType::End, PartKind::Ferrule, 3, 0);
        j.connect(a, cab);
        QString why;
        check(j.connect(cab, fan, &why), "bridge: a fanout joined to the cable");
        j.connect(fan, f1);
        j.brick(cab)->run.wires = 8;
        for (int i = 1; i <= 8; ++i) j.brick(cab)->run.nets << QStringLiteral("N%1").arg(i);
        j.brick(fan)->run.bridgeWires = QStringLiteral("2,4-5");
        check(joinedSpecial(j, fan) == cab && syncBridgeWires(j) == 1, "bridge: the fanout takes its wires from the cable");
        std::printf("  bridge: %d wire(s), nets %s\n", j.brick(fan)->run.wires, qPrintable(j.brick(fan)->run.nets.join(',')));
        check(j.brick(fan)->run.wires == 3 &&
                  j.brick(fan)->run.nets == QStringList({QStringLiteral("N2"), QStringLiteral("N4"), QStringLiteral("N5")}),
              "bridge: as many wires as it takes, each with the net of the wire it takes");
        check(BrickDesign::fromJson(j.toJson()).brick(fan)->run.bridgeWires == QStringLiteral("2,4-5"),
              "bridge: kept with the design");
    }

    // 5. The wizard: linked on the pane, put at the length; the length typed, the part moves; the wheel over a
    //    number does not change it.
    {
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        BrickWizardDialog dlg(&ed);
        dlg.show();
        BrickDesign& w = dlg.design();
        const QString a = w.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString r = w.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString b = w.addBrick(BrickType::End, PartKind::Connector, 3, 0);
        w.brick(r)->run.lengthMm = 225.0;
        w.connect(a, r);
        w.connect(r, b);
        // THE PANE IS WHAT IS JOINED TO WHAT: linking, moving bricks and typing a length move nothing on it.
        emit dlg.canvas()->linked(r, b);
        check(std::abs(w.brick(b)->col - 3.0) < 1e-9, "wizard: linked, the part stays where it was put on the pane");
        QHash<QString, QPointF> from;
        from.insert(b, QPointF(w.brick(b)->col, w.brick(b)->row));
        w.brick(b)->col = 4.0;
        emit dlg.canvas()->bricksMoved(from);
        check(std::abs(w.brick(r)->run.lengthMm - 225.0) < 1e-9, "wizard: a brick moved on the pane changes no length");
        dlg.selectBrick(r);
        QApplication::processEvents();
        auto* len = dlg.findChild<QDoubleSpinBox*>(QStringLiteral("brickLength"));
        if (len) len->setValue(150.0);
        check(len && std::abs(w.brick(b)->col - 4.0) < 1e-9 && std::abs(w.brick(r)->run.lengthMm - 150.0) < 1e-9,
              "wizard: the length typed, the drawing follows and the pane is left as it is");
        auto* wires = dlg.findChild<QSpinBox*>(QStringLiteral("brickWires"));
        if (wires) {
            const int was = wires->value();
            QWheelEvent wheel(QPointF(5, 5), wires->mapToGlobal(QPoint(5, 5)), QPoint(), QPoint(0, 120), Qt::NoButton,
                              Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(wires, &wheel);
            check(wires->value() == was, "wizard: the wheel over a number scrolls past it, and does not change it");
        }
        auto* bridge = dlg.findChild<QComboBox*>(QStringLiteral("brickBridgeWires"));
        check(bridge && !bridge->isVisibleTo(&dlg), "wizard: a cable's own panel has no bridge wires");
    }
}

// ROUND EIGHT: Esc keeps the wizard open; a part and its special in one line move together; the special's brick
// is the knee; a bridge wire takes some of a cable's wires, the rest going on to its part.
void testRoundEight() {
    // IN ONE LINE, MOVING TOGETHER.
    {
        BrickDesign d;
        const QString e = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString s = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString y = d.addBrick(BrickType::Run, PartKind::Cable, 3, 0);
        d.connect(e, s);
        d.connect(s, y);   // its other end a cable: not strung between two parts
        const QHash<QString, BrickFollow> f = bricksFollowing(d, {e});
        check(f.contains(s) && f.value(s).x && f.value(s).y, "follow: a special on one part goes with it, along and across");
        const QString e2 = d.addBrick(BrickType::End, PartKind::Connector, 4, 1);
        const QString s2 = d.addBrick(BrickType::Run, PartKind::Cable, 5, 1);
        const QString e3 = d.addBrick(BrickType::End, PartKind::Connector, 6, 2);
        d.connect(e2, s2);
        d.connect(s2, e3);
        const QHash<QString, BrickFollow> g = bricksFollowing(d, {e2});
        check(g.contains(s2) && !g.value(s2).x && g.value(s2).y,
              "follow: strung between two parts, it goes up and down with the part in its line, and not along");
        const QHash<QString, BrickFollow> h = bricksFollowing(d, {s2});
        check(h.contains(e2) && h.value(e2).y && !h.contains(e3), "follow: the special moved, the part in its line goes too");
    }
    // THE KNEE AT THE BRICK: in line with its start part, the first bend is where the brick is.
    {
        const std::vector<Vec2> r = routeBetweenFaces({0, 0}, {1, 0}, {200, 40}, {-1, 0}, 10.0, 45.0, 0.3, 0);
        const std::vector<Vec2> q = routeBetweenFaces({0, 0}, {1, 0}, {200, 40}, {-1, 0}, 10.0, 45.0, 0.7, 1);
        std::printf("  knee: first bend at %.1f, second bend at %.1f\n", r.size() > 1 ? r[1].x : -1.0,
                    q.size() > 2 ? q[2].x : -1.0);
        check(r.size() == 4 && std::abs(r[1].x - 60.0) < 1e-6, "knee: the run leaves its part's line where the brick is");
        check(q.size() == 4 && std::abs(q[2].x - 140.0) < 1e-6, "knee: in line with its finish part, it joins that line there");
    }
    // A BRIDGE WIRE.
    {
        BrickDesign d;
        const QString x0 = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString x1 = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        const QString br = d.addBrick(BrickType::Run, PartKind::Wire, 3, 1);
        const QString f = d.addBrick(BrickType::End, PartKind::Ferrule, 4, 1);
        makeBridgeWire(*d.brick(br));
        d.brick(br)->run.bridgeWires = QStringLiteral("2,3");
        QString why;
        d.connect(x0, c);
        check(d.connect(c, x1, &why), "bridge wire: the cable goes on to its connector");
        check(brickIsBridge(*d.brick(br)) && d.connect(c, br, &why), "bridge wire: and a bridge wire joins the same end of it");
        check(d.connect(br, f, &why), "bridge wire: out to its own part");
        syncBridgeWires(d);
        check(d.brick(br)->run.wires == 2 && d.link(c, br) && d.link(c, br)->wires == 2,
              "bridge wire: it takes the two wires it is told, and the cable gives them to it");
        // OUT OF THE CABLE'S FAN, not a split of it: the cable runs whole to its connector with the wires left,
        // and the bridge starts on the cable where its fan begins.
        int cableRuns = 0, toX1 = -1;
        std::vector<Vec2> cableRoute, bridgeRoute;
        for (const Entity& e : buildBrickHarness(d, nullptr).entities) {
            if (e.type != EntityType::TwistedPair) continue;
            const QString id = e.meta.value(QStringLiteral("brickId")).toString();
            if (id == c) {
                ++cableRuns;
                toX1 = e.meta.value(QStringLiteral("twistWires")).toInt();
                cableRoute = e.points;
            } else if (id == br) {
                bridgeRoute = e.points;
            }
        }
        double off = 1e300;
        if (!bridgeRoute.empty())
            for (size_t k = 0; k + 1 < cableRoute.size(); ++k)
                off = std::min(off, closestPointOnSegment(bridgeRoute.front(), cableRoute[k], cableRoute[k + 1]).distance);
        std::printf("  bridge wire: cable in %d run(s), %d wire(s) on to the connector, the bridge %.2f off the cable\n",
                    cableRuns, toX1, off);
        check(cableRuns == 1 && toX1 == 2, "bridge wire: the cable is not split - the rest of its wires go on to its part");
        // Out of the cable's END FACE, square to it: on the face (within the cover's width of the middle), where the
        // fan begins, its first stretch along the cable before it turns.
        check(off < 12.0 && !cableRoute.empty() && bridgeRoute.front().x < cableRoute.back().x && bridgeRoute.size() >= 2 &&
                  std::abs(bridgeRoute[1].y - bridgeRoute[0].y) < 1e-6 && bridgeRoute[1].x > bridgeRoute[0].x,
              "bridge wire: and it comes out of the cable's end face, square to it, where the fan begins");
        // An ordinary special still may not share a cable's end.
        const QString fan = d.addBrick(BrickType::Fanout, PartKind::Wire, 3, -1);
        check(!d.connect(c, fan, &why), "bridge wire: an ordinary special still does not share an end already used");
        // ITS PART DRAGGED ON THE DRAWING: the bridge wire's length follows, as any run's does.
        {
            const BrickBuild built = buildBrickHarness(d, nullptr);
            const double was = d.brick(br)->run.lengthMm;
            const QString moved = dragPartOnDrawing(d, built, f, 40.0, 0.0);
            std::printf("  bridge wire: its ferrule dragged 40 along, %.0f -> %.0f\n", was, d.brick(br)->run.lengthMm);
            check(moved == br && d.brick(br)->run.lengthMm > was, "bridge wire: its part can be dragged, and its length follows");
        }
    }
    // THE WIZARD: Esc does not close it; the palette has the bridge wire; linked, a special comes into its part's line.
    {
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        BrickWizardDialog dlg(&ed);
        dlg.show();
        QApplication::processEvents();
        QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(&dlg, &esc);
        QApplication::processEvents();
        check(dlg.isVisible(), "wizard: Esc does not close it");
        bool bridgeItem = false;
        for (QListWidget* l : dlg.findChildren<QListWidget*>())
            for (int i = 0; i < l->count(); ++i)
                if (l->item(i)->text().trimmed() == QStringLiteral("Bridge wire")) bridgeItem = true;
        check(bridgeItem, "wizard: a Bridge wire among the specials");
        const QString made = dlg.addFromPalette(brickTypeKey(BrickType::Run) + QStringLiteral("|") +
                                                partKindKey(PartKind::Wire) + QStringLiteral("|bridge"));
        check(dlg.design().brick(made) && brickIsBridge(*dlg.design().brick(made)),
              "wizard: dropped from the palette, it is a bridge wire");
        BrickDesign& w = dlg.design();
        const QString e = w.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString s = w.addBrick(BrickType::Run, PartKind::Cable, 2, 2.0);
        w.connect(e, s);
        emit dlg.canvas()->linked(e, s);
        // THE PANE NO LONGER PLACES THE DRAWING: a special is tied to no part there, each stays where it is put.
        check(std::abs(w.brick(s)->row - 2.0) < 1e-9, "wizard: linked, the special stays where it was put");
        dlg.canvas()->setSelection({e});
        dlg.canvas()->moveSelection(0.0, 1.0);
        check(std::abs(w.brick(s)->row - 2.0) < 1e-9 && std::abs(w.brick(e)->row - 1.0) < 1e-9,
              "wizard: the part moved down, the special is not moved with it");
    }
}

// ROUND NINE: balloons touch their part, one height for every designator, joints stand alone, undo and redo,
// dimensions put on by hand.
void testRoundNine() {
    // A BALLOON'S LEADER ENDS ON THE PART - here a tall piece and a low one, the box's top middle empty paper.
    {
        PartsCatalog cat;
        cat.setRemembering(false);
        LibraryPart& p = cat.active().add(PartKind::Connector);
        const QString pid = p.id;
        auto box = [](double x0, double y0, double x1, double y1) {
            Entity e;
            e.type = EntityType::Polyline;
            e.points = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}, {x0, y0}};
            e.a = e.points.front();
            e.b = e.points.back();
            return e;
        };
        p.mechanical = partDrawingFromEntities({box(0, -5, 6, 5), box(6, -2, 30, 2)});
        p.outputs = {{{30, -2}, {30, 2}}};
        BrickDesign d;
        d.catalog = &cat;
        const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        d.brick(x)->libPart = pid;
        d.brick(x)->balloon = true;
        const BrickBuild b = buildBrickHarness(d, &cat);
        Vec2 tip;
        bool have = false;
        std::vector<const Entity*> outline;
        for (const Entity& e : b.entities) {
            if (e.meta.value(QStringLiteral("balloon")).toBool() && e.type == EntityType::Circle) {
                const QJsonArray lead = e.meta.value(QStringLiteral("leaders")).toArray();
                if (!lead.isEmpty()) {
                    tip = Vec2{lead[0].toArray()[0].toDouble(), lead[0].toArray()[1].toDouble()};
                    have = true;
                }
            } else if (e.type == EntityType::Polyline && e.meta.value(QStringLiteral("brickId")).toString() == x) {
                outline.push_back(&e);
            }
        }
        double off = 1e300;
        for (const Entity* e : outline)
            for (size_t k = 0; k + 1 < e->points.size(); ++k)
                off = std::min(off, closestPointOnSegment(tip, e->points[k], e->points[k + 1]).distance);
        std::printf("  balloon: its leader ends %.3f off the part\n", off);
        check(have && off < 1e-6, "balloon: its leader touches the part's own outline");
    }
    // JOINTS STAND ALONE.
    {
        BrickDesign d;
        const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString r = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString j = d.addBrick(BrickType::End, PartKind::Splice, 2, 0);
        d.connect(x, r);
        d.connect(r, j);
        check(!brickIsTied(*d.brick(j)) && brickIsTied(*d.brick(x)), "joint: a joint stands alone, a connector is tied");
        check(!bricksFollowing(d, {j}).contains(r) && !bricksFollowing(d, {r}).contains(j),
              "joint: it takes no special with it and is taken by none");
        check(!specialOneToOne(d, r, nullptr, nullptr) && !fitEndToLength(d, r, j),
              "joint: a run to it is not measured off the pane, and linking does not move it");
    }
    // DIMENSIONS PUT ON BY HAND: above or below the two parts, saying the run's length.
    {
        BrickDesign d;
        const QString x0 = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString r = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString x1 = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        d.connect(x0, r);
        d.connect(r, x1);
        d.brick(r)->run.lengthMm = 250.0;
        d.dims.push_back({x0, x1, false});
        d.dims.push_back({x0, x1, true});
        const BrickBuild b = buildBrickHarness(d, nullptr);
        Bounds parts;
        std::vector<const Entity*> dims;
        for (const Entity& e : b.entities) {
            if (e.meta.value(QStringLiteral("brickDim")).toString() == QStringLiteral("manual")) dims.push_back(&e);
            else if (e.meta.value(QStringLiteral("brickId")).toString() == x0 || e.meta.value(QStringLiteral("brickId")).toString() == x1)
                parts.expand(e.bounds());
        }
        check(dims.size() == 2, "dims: each one put on by hand is drawn");
        if (dims.size() == 2 && parts.valid) {
            const double above = dims[0]->points[2].y, below = dims[1]->points[2].y;
            std::printf("  dims: above at %.1f, below at %.1f, the parts %.1f..%.1f, saying %s\n", above, below,
                        parts.min.y, parts.max.y, qPrintable(dims[0]->meta.value(QStringLiteral("textOverride")).toString()));
            check(above > parts.max.y && below < parts.min.y, "dims: one above the parts, one below them");
            check(std::abs(dims[0]->meta.value(QStringLiteral("brickMeasured")).toDouble() - 250.0) < 1e-6,
                  "dims: between two parts on one run, it says the run's length");
        }
        check(BrickDesign::fromJson(d.toJson()).dims.size() == 2, "dims: kept with the design");
    }
    // THE WIZARD: one height for all; undo and redo; the Dimension tool.
    {
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        BrickWizardDialog dlg(&ed);
        dlg.show();
        BrickDesign& w = dlg.design();
        const QString a = dlg.addFromPalette(brickTypeKey(BrickType::End) + QStringLiteral("|") + partKindKey(PartKind::Connector));
        const QString b = dlg.addFromPalette(brickTypeKey(BrickType::End) + QStringLiteral("|") + partKindKey(PartKind::Connector));
        dlg.selectBrick(a);
        QApplication::processEvents();
        auto* height = dlg.findChild<QDoubleSpinBox*>(QStringLiteral("brickDesignatorHeight"));
        auto* all = dlg.findChild<QToolButton*>(QStringLiteral("brickDesignatorHeightAll"));
        if (height) height->setValue(7.0);
        if (all) all->click();
        check(all && w.brick(a) && w.brick(b) && std::abs(w.brick(b)->designatorHeightMm - 7.0) < 1e-9,
              "wizard: All gives every designator on the drawing that height");
        const size_t had = w.bricks.size();
        dlg.addFromPalette(brickTypeKey(BrickType::End) + QStringLiteral("|") + partKindKey(PartKind::Ferrule));
        check(dlg.design().bricks.size() == had + 1 && dlg.canUndo(), "undo: a brick added can be undone");
        dlg.undo();
        check(dlg.design().bricks.size() == had, "undo: Ctrl+Z takes it off again");
        dlg.redo();
        check(dlg.design().bricks.size() == had + 1, "redo: Ctrl+Y puts it back");
        auto* tool = dlg.findChild<QToolButton*>(QStringLiteral("brickDimTool"));
        check(tool && tool->isCheckable(), "wizard: a Dimension tool - two bricks clicked put one on");
        const size_t dimsWas = dlg.design().dims.size();
        emit dlg.canvas()->dimensionPicked(a, b);
        check(dlg.design().dims.size() == dimsWas + 1, "wizard: the two bricks picked get a dimension between them");
    }
}

// ROUND TEN: parts moved on the drawing set their run's length; the bridge wire picks its wire (or the shield);
// Symmetric lays a harness with a bridge out without piling bricks on one another.
void testRoundTen() {
    // DRAGGED ON THE DRAWING: along, the run's length follows; down, farther off, more cable - and the pane stays.
    {
        BrickDesign d;
        const QString x0 = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString r = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString x1 = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        d.connect(x0, r);
        d.connect(r, x1);
        BrickBuild b = buildBrickHarness(d, nullptr);
        check(dragPartOnDrawing(d, b, x1, 50.0, 0.0) == r && std::abs(d.brick(r)->run.lengthMm - 200.0) < 1e-9,
              "drag: a part dragged 50 along its run makes the run 50 longer");
        b = buildBrickHarness(d, nullptr);
        const double was = d.brick(r)->run.lengthMm;
        dragPartOnDrawing(d, b, x1, 0.0, -40.0);
        std::printf("  drag: 40 down takes the run from %.0f to %.0f\n", was, d.brick(r)->run.lengthMm);
        check(d.brick(r)->run.lengthMm > was + 10.0 && std::abs(d.brick(x1)->drawDyMm + 40.0) < 1e-9 &&
                  d.brick(x1)->row == 0.0 && d.brick(x1)->col == 2.0,
              "drag: moved down, farther off, more cable - the part goes down on the drawing and not on the pane");
        const BrickBuild after = buildBrickHarness(d, nullptr);
        const double y1 = after.faces.value(QStringLiteral("%1:0").arg(x1)).centre.y;
        const double y0 = after.faces.value(QStringLiteral("%1:0").arg(x0)).centre.y;
        check(y1 < y0 - 30.0, "drag: and the drawing puts it there");
        check(std::abs(BrickDesign::fromJson(d.toJson()).brick(x1)->drawDyMm + 40.0) < 1e-9, "drag: kept with the design");
        // DRAGGED RIGHT UP TO THE OTHER PART: the length stops at what the drawing can show, not one millimetre.
        {
            BrickDesign e = d;
            e.brick(x1)->drawDyMm = 0.0;
            const BrickBuild near = buildBrickHarness(e, nullptr);
            dragPartOnDrawing(e, near, x1, -200.0, 0.0);   // to within a few mm of it, not out past it
            std::printf("  drag: pushed right up to its other part, the run stops at %.0f\n", e.brick(r)->run.lengthMm);
            check(e.brick(r)->run.lengthMm < 30.0,
                  "drag: pushed right up to the other part, the run comes down with it - towards nought");
        }
    }
    // THE BRIDGE WIRE PICKS ITS WIRE: the cable's wires by net, and its shield when it has a braid.
    {
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        BrickWizardDialog dlg(&ed);
        dlg.show();
        BrickDesign& w = dlg.design();
        const QString c = w.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString x = w.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString br = w.addBrick(BrickType::Run, PartKind::Wire, 3, 1);
        makeBridgeWire(*w.brick(br));
        w.connect(x, c);
        w.connect(c, br);
        w.brick(c)->run.wires = 3;
        w.brick(c)->run.nets = {QStringLiteral("VCC"), QStringLiteral("GND"), QStringLiteral("SIG")};
        w.brick(c)->run.stack = {QStringLiteral("wires"), QStringLiteral("braid"), QStringLiteral("cable")};
        dlg.selectBrick(br);
        QApplication::processEvents();
        auto* pick = dlg.findChild<QComboBox*>(QStringLiteral("brickBridgeWires"));
        check(pick && pick->isVisibleTo(&dlg) && pick->count() == 5 && pick->itemText(1).contains(QStringLiteral("GND")) &&
                  pick->itemData(3).toString() == QStringLiteral("1-3") &&
                  pick->itemData(4).toString() == QString::fromLatin1(kBridgeShield),
              "bridge pick: the cable's wires by net, all of them, and its shield");
        if (pick) {
            pick->setCurrentIndex(4);
            emit pick->activated(4);
        }
        syncBridgeWires(w);
        check(w.brick(br)->run.bridgeWires == QString::fromLatin1(kBridgeShield) && w.brick(br)->run.wires == 1 &&
                  w.brick(br)->run.nets == QStringList({QStringLiteral("SHIELD")}),
              "bridge pick: the shield taken - one drain wire, off none of the cable's wires");
        dlg.selectBrick(c);
        QApplication::processEvents();
        check(pick && !pick->isVisibleTo(&dlg), "bridge pick: not on the cable's own panel");
    }
    // SYMMETRIC with a Y-split, a bridge and short runs: no brick on top of another, the bridge's part beside its
    // connector rather than off on its own.
    {
        BrickDesign d;
        const QString p1 = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString c1 = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString y = d.addBrick(BrickType::End, PartKind::YSplit, 2, 0);
        const QString c2 = d.addBrick(BrickType::Run, PartKind::Cable, 3, -1);
        const QString c3 = d.addBrick(BrickType::Run, PartKind::Cable, 3, 1);
        const QString c4 = d.addBrick(BrickType::Run, PartKind::Cable, 1, 1);
        const QString x2 = d.addBrick(BrickType::End, PartKind::Connector, 4, -1);
        const QString x3 = d.addBrick(BrickType::End, PartKind::Connector, 4, 1);
        const QString x4 = d.addBrick(BrickType::End, PartKind::Connector, 0, 1);
        const QString br = d.addBrick(BrickType::Run, PartKind::Wire, 1, 3);
        const QString fe = d.addBrick(BrickType::End, PartKind::Ferrule, 0, 3);
        makeBridgeWire(*d.brick(br));
        for (const auto& [a, b] : {std::pair<QString, QString>{p1, c1}, {c1, y}, {y, c2}, {y, c3}, {c2, x2}, {c3, x3},
                                   {x4, c4}, {c4, y}})
            d.connect(a, b);
        d.brick(c1)->run.lengthMm = 50.0;
        d.connect(c4, br);
        d.connect(br, fe);
        arrangeBricks(d);
        int clash = 0;
        for (size_t i = 0; i < d.bricks.size(); ++i)
            for (size_t j = i + 1; j < d.bricks.size(); ++j)
                if (std::abs(d.bricks[i].col - d.bricks[j].col) < 0.5 && std::abs(d.bricks[i].row - d.bricks[j].row) < 0.5)
                    ++clash;
        std::printf("  symmetric: %d brick(s) on top of another; ferrule at %.1f,%.1f\n", clash, d.brick(fe)->col,
                    d.brick(fe)->row);
        check(clash == 0, "symmetric: no brick is put on top of another");
    }
}

// ROUND ELEVEN: one wire has no tail; the parts show where to take hold of them; bricks moved on the pane leave
// the drawing alone; the drawing enlarged to the whole window and back.
void testRoundEleven() {
    // ONE WIRE, NO TAIL: it bends straight away out of its part, no fan's worth of lead left sticking out.
    {
        BrickDesign d;
        const QString a = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString r = d.addBrick(BrickType::Run, PartKind::Wire, 1, 0);
        const QString b = d.addBrick(BrickType::End, PartKind::Connector, 2, 1);
        d.connect(a, r);
        d.connect(r, b);
        d.brick(r)->run.wires = 1;
        d.brick(r)->run.stack = {QStringLiteral("wires")};
        std::vector<Vec2> route;
        for (const Entity& e : buildBrickHarness(d, nullptr).entities)
            if (e.meta.value(QStringLiteral("brickId")).toString() == r && e.points.size() >= 2) route = e.points;
        std::printf("  one wire: %d point(s), leaving its part over %.1f\n", static_cast<int>(route.size()),
                    route.size() >= 2 ? distance(route[0], route[1]) : -1.0);
        check(route.size() >= 2 && (route.size() == 2 || distance(route[0], route[1]) > 0.0),
              "one wire: drawn as one wire");
        (void)b;
    }
    // BRICKS MOVED ON THE PANE: the parts stay where they are on the drawing.
    {
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        BrickWizardDialog dlg(&ed);
        dlg.show();
        BrickDesign& w = dlg.design();
        const QString a = w.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString r = w.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString b = w.addBrick(BrickType::End, PartKind::Connector, 2, 1);
        w.connect(a, r);
        w.connect(r, b);
        auto faceOf = [&w](const QString& id) {
            return buildBrickHarness(w, nullptr).faces.value(id + QStringLiteral(":0")).centre;
        };
        const Vec2 bWas = faceOf(b), aWas = faceOf(a);
        dlg.canvas()->setSelection({b});
        dlg.canvas()->moveSelection(0.0, 2.0);
        const Vec2 bNow = faceOf(b);
        std::printf("  pane move: part drawn at %.1f,%.1f then %.1f,%.1f\n", bWas.x, bWas.y, bNow.x, bNow.y);
        check(distance(bWas, bNow) < 1e-6 && w.brick(b)->row == 3.0, "pane move: a brick moved down on the pane stays put on the drawing");
        dlg.canvas()->setSelection({a});
        dlg.canvas()->moveSelection(-3.0, 0.0);   // along, and not past the other part (that turns the harness round)
        check(distance(aWas, faceOf(a)) < 1e-6 && distance(bWas, faceOf(b)) < 1e-6,
              "pane move: the first part moved along the pane does not move the drawing");
        // THE GRAB POINT: on the part picked (and the one pointed at), in the preview only - not on every part.
        dlg.selectBrick(a);   // the preview rebuilt
        int rings = 0;
        for (const Entity& e : dlg.preview()->entities())
            if (!e.meta.value(QStringLiteral("brickHandle")).toString().isEmpty()) ++rings;
        check(rings == 1, "grab: the part picked shows its grab point in the preview, the others none");
        bool inDrawing = false;
        for (const Entity& e : dlg.drawing())
            if (e.meta.contains(QStringLiteral("brickHandle"))) inDrawing = true;
        check(!inDrawing, "grab: and it is not part of the drawing placed");
        // ENLARGED: the drawing across the window, the side columns away; back again.
        auto* big = dlg.findChild<QToolButton*>(QStringLiteral("brickEnlarge"));
        auto* palette = dlg.findChild<BrickPalette*>(QStringLiteral("brickPalette"));
        const int wideWas = dlg.preview()->width();
        dlg.setDrawingEnlarged(true);
        QApplication::processEvents();
        std::printf("  enlarge: preview %d wide, then %d\n", wideWas, dlg.preview()->width());
        check(big && big->isChecked() && palette && !palette->isVisibleTo(&dlg) && dlg.preview()->width() > wideWas,
              "enlarge: the drawing takes the window, the bricks small above it");
        check(dlg.canvas()->isVisibleTo(&dlg), "enlarge: the brick pane is still there, small");
        dlg.setDrawingEnlarged(false);
        QApplication::processEvents();
        check(big && !big->isChecked() && palette && palette->isVisibleTo(&dlg), "enlarge: and back as it was");
    }
}

// ROUND TWELVE: the drawing order of bricks; each layer of a run with its part number, copied from the cables next
// along.
void testRoundTwelve() {
    // DRAW ORDER: a brick told to come to the front is drawn after the others.
    {
        BrickDesign d;
        const QString a = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString r = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString b = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        d.connect(a, r);
        d.connect(r, b);
        auto lastOf = [&d] {
            const BrickBuild bb = buildBrickHarness(d, nullptr);
            QString last;
            for (const Entity& e : bb.entities)
                if (!e.meta.value(QStringLiteral("brickId")).toString().isEmpty()) last = e.meta.value(QStringLiteral("brickId")).toString();
            return last;
        };
        d.brick(a)->z = 5;
        check(lastOf() == a, "draw order: a brick brought to the front is drawn over the rest");
        d.brick(a)->z = -5;
        d.brick(r)->z = 3;
        check(lastOf() == r, "draw order: and one sent back is drawn under them");
        check(BrickDesign::fromJson(d.toJson()).brick(r)->z == 3, "draw order: kept with the design");
    }
    // LAYER PART NUMBERS: set on one cable, copied to the cable on the other side of a part; on the drawing too.
    {
        BrickDesign d;
        const QString x0 = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString c1 = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString j = d.addBrick(BrickType::End, PartKind::Splice, 2, 0);
        const QString c2 = d.addBrick(BrickType::Run, PartKind::Cable, 3, 0);
        const QString x1 = d.addBrick(BrickType::End, PartKind::Connector, 4, 0);
        for (const auto& [a, b] : {std::pair<QString, QString>{x0, c1}, {c1, j}, {j, c2}, {c2, x1}}) d.connect(a, b);
        for (const QString& c : {c1, c2}) d.brick(c)->run.stack = {QStringLiteral("wires"), QStringLiteral("braid"),
                                                                    QStringLiteral("heatshrink")};
        d.brick(c1)->run.stackParts.insert(QStringLiteral("heatshrink"), QStringLiteral("HS-1"));
        d.brick(c1)->run.stackParts.insert(QStringLiteral("braid"), QStringLiteral("BR-1"));
        check(BrickDesign::fromJson(d.toJson()).brick(c1)->run.stackParts.value(QStringLiteral("braid")) == QStringLiteral("BR-1"),
              "layer P/N: kept with the design");
        QStringList onDrawing;
        for (const Entity& e : buildBrickHarness(d, nullptr).entities)
            if (e.meta.value(QStringLiteral("brickId")).toString() == c1)
                for (const QJsonValue v : e.meta.value(QStringLiteral("stackParts")).toArray()) onDrawing << v.toString();
        check(onDrawing.contains(QStringLiteral("HS-1")) && onDrawing.contains(QStringLiteral("BR-1")),
              "layer P/N: and on the drawing, so its lines of the bill can be found from it");
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        BrickWizardDialog dlg(&ed);
        dlg.design().bricks = d.bricks;
        dlg.design().links = d.links;
        const int given = dlg.layerPartsFromNeighbours(c2);
        std::printf("  layer P/N: %d layer(s) copied from the cable past the splice\n", given);
        check(given == 2 && dlg.design().brick(c2)->run.stackParts.value(QStringLiteral("heatshrink")) == QStringLiteral("HS-1"),
              "layer P/N: As neighbours copies them from the cable next along, the splice between ignored");
    }
}

// ROUND THIRTEEN: a brick in front covers what it crosses; dimensions per part, stacked clear of each other, from
// the middle too; a bridge wire shorter than the fan and with its bend rounded; fans that stop short of a bend, and
// a fan as long as the run leaving no cover.
void testRoundThirteen() {
    // IN FRONT, COVERING: a mask goes down before the brick brought forward.
    {
        BrickDesign d;
        const QString a = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString r = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString b = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        d.connect(a, r);
        d.connect(r, b);
        d.brick(r)->z = 2;
        int masks = 0, maskBeforeRun = -1, firstRun = -1, k = 0;
        for (const Entity& e : buildBrickHarness(d, nullptr).entities) {
            if (e.meta.value(QStringLiteral("brickMask")).toBool()) {
                ++masks;
                if (maskBeforeRun < 0) maskBeforeRun = k;
            }
            if (e.type == EntityType::TwistedPair && firstRun < 0) firstRun = k;
            ++k;
        }
        check(masks > 0 && maskBeforeRun >= 0 && maskBeforeRun < firstRun,
              "front: a brick brought to the front covers what is under it - its mask goes down first");
    }
    // DIMENSIONS: each part's own side, stacked where they would cross, from the middle of a part.
    {
        BrickDesign d;
        const QString x0 = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString r1 = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString x1 = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        const QString r2 = d.addBrick(BrickType::Run, PartKind::Cable, 3, 0);
        const QString x2 = d.addBrick(BrickType::End, PartKind::Connector, 4, 0);
        for (const auto& [a, b] : {std::pair<QString, QString>{x0, r1}, {r1, x1}, {x1, r2}, {r2, x2}}) d.connect(a, b);
        d.dims.push_back({x0, x1, false});
        d.dims.push_back({x0, x2, false});
        auto dims = [&d] {
            std::vector<Entity> out;
            for (const Entity& e : buildBrickHarness(d, nullptr).entities)
                if (e.meta.value(QStringLiteral("brickDim")).toString() == QStringLiteral("manual")) out.push_back(e);
            return out;
        };
        std::vector<Entity> two = dims();
        if (two.size() == 2)
            std::printf("  dims: %.1f..%.1f at %.1f, %.1f..%.1f at %.1f\n", two[0].points[0].x, two[0].points[1].x,
                        two[0].points[2].y, two[1].points[0].x, two[1].points[1].x, two[1].points[2].y);
        check(two.size() == 2 && std::abs(two[0].points[2].y - two[1].points[2].y) > 5.0,
              "dims: two that would cross are put at different heights");
        check(two.size() == 2 && two[0].points[2].y < two[1].points[2].y,
              "dims: the shorter nearer the parts, the longer further out");
        d.brick(x0)->dimBelow = true;   // as the part's panel does: its dimensions go below with it
        for (BrickDim& dm : d.dims)
            if (dm.a == x0) dm.below = true;
        std::vector<Entity> below = dims();
        check(below.size() == 2 && below[0].points[2].y < 0.0 && below[1].points[2].y < 0.0,
              "dims: a part set to Below puts its dimensions below");
        d.brick(x0)->dimBelow = false;
        const double nearX = dims()[0].points[0].x;
        d.dimFromMid = true;
        const double midX = dims()[0].points[0].x;
        std::printf("  dims: from the near point %.1f, from the middle %.1f\n", nearX, midX);
        check(std::abs(midX - nearX) > 1.0, "dims: from the middle of the part, halfway between near and far");
        check(BrickDesign::fromJson(d.toJson()).dimFromMid, "dims: the middle kept with the design");
    }
    // A BRIDGE WIRE SHORTER THAN THE FAN, and its bend rounded.
    {
        BrickDesign d;
        const QString x0 = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString x1 = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        const QString br = d.addBrick(BrickType::Run, PartKind::Wire, 3, 1);
        const QString f = d.addBrick(BrickType::End, PartKind::Ferrule, 4, 1);
        makeBridgeWire(*d.brick(br));
        d.connect(x0, c);
        d.connect(c, x1);
        d.connect(c, br);
        d.connect(br, f);
        d.brick(c)->run.stack = {QStringLiteral("wires"), QStringLiteral("cable")};
        d.brick(br)->run.lengthMm = 10.0;
        syncBridgeWires(d);
        const BrickBuild b = buildBrickHarness(d, nullptr);
        const double fx = b.faces.value(f + QStringLiteral(":0")).centre.x;
        const double xx = b.faces.value(x1 + QStringLiteral(":0")).centre.x;
        std::printf("  bridge: ferrule face at %.1f, the connector's at %.1f\n", fx, xx);
        check(fx < xx, "bridge: a short bridge wire puts its part nearer than the part its cable's fan goes into");
        int points = 0;
        for (const Entity& e : b.entities)
            if (e.meta.value(QStringLiteral("brickId")).toString() == br && e.type == EntityType::Polyline) points = int(e.points.size());
        check(points > 4, "bridge: its bends rounded, not sharp corners");
    }
    // FANS: stopped short of a bend; as long as the whole run, no cover.
    {
        BrickDesign d;
        const QString a = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString r = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString b = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        d.connect(a, r);
        d.connect(r, b);
        d.brick(r)->run.stack = {QStringLiteral("wires"), QStringLiteral("cable")};
        d.brick(r)->run.fanStartMm = 500.0;
        d.brick(r)->run.fanFinishMm = 500.0;
        QString body = QStringLiteral("?");
        for (const Entity& e : buildBrickHarness(d, nullptr).entities)
            if (e.type == EntityType::TwistedPair) body = e.meta.value(QStringLiteral("twistBody")).toString();
        check(body.isEmpty(), "fans: fans as long as the run leave no cable - wires fanned out of one face into the other");
        d.brick(r)->run.fanStartMm = 40.0;
        d.brick(r)->run.fanFinishMm = -1.0;
        d.brick(b)->row = 2.0;   // a bend on the way
        for (const Entity& e : buildBrickHarness(d, nullptr).entities)
            if (e.type == EntityType::TwistedPair && e.points.size() > 2) {
                const double first = distance(e.points[0], e.points[1]);
                const double fan = e.meta.value(QStringLiteral("twistEnds")).toDouble();
                std::printf("  fans: the fan %.1f on a first straight of %.1f\n", fan, first);
                check(fan < first, "fans: a fan stops before the bend - no spiral round it");
            }
    }
}

// A CONNECTOR ON THE BILL BRINGS ITS CONTACTS: several tied, the one chosen, one per way; one already on the bill,
// nothing more.
void testConnectorContactsOnBill() {
    const QString libFile = QDir(QDir::tempPath()).filePath(QStringLiteral("edraw_bill_contacts.plb"));
    QFile::remove(libFile);
    {
        PartsLibrary lib;
        LibraryPart conn;
        conn.id = QStringLiteral("BILL-CONN");
        conn.kind = PartKind::Connector;
        conn.mfrPn = QStringLiteral("HOUSING-6");
        conn.params[QStringLiteral("pins")] = QStringLiteral("6");
        LibraryPart a;
        a.id = QStringLiteral("BILL-PIN-A");
        a.kind = PartKind::ContactPin;
        a.mfrPn = QStringLiteral("PIN-A");
        LibraryPart b = a;
        b.id = QStringLiteral("BILL-PIN-B");
        b.mfrPn = QStringLiteral("PIN-B");
        lib.parts = {conn, a, b};
        addContactTie(lib, conn.id, a.id);
        addContactTie(lib, conn.id, b.id);
        lib.save(libFile);
    }
    PartsCatalog cat;
    cat.openRemembered();
    cat.connectLibrary(libFile);
    cat.remember();
    {
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        BrickWizardDialog dlg(&ed);
        QStringList offered;
        dlg.setContactChooser([&offered](const QStringList& ids) {
            offered = ids;
            return ids.value(1);   // the second of them
        });
        check(dlg.addPartToBom(QStringLiteral("BILL-CONN")), "bill contacts: the connector goes on the bill");
        BomTable table;
        table.fromJson(doc.bom());
        double qtyB = 0.0;
        bool hasA = false;
        for (const BomRow& r : table.rows) {
            if (r.partId == QStringLiteral("BILL-PIN-B")) qtyB = r.qty;
            if (r.partId == QStringLiteral("BILL-PIN-A")) hasA = true;
        }
        std::printf("  bill contacts: offered %d, PIN-B x %.0f\n", static_cast<int>(offered.size()), qtyB);
        check(offered.size() == 2 && std::abs(qtyB - 6.0) < 1e-9 && !hasA,
              "bill contacts: several tied, asked which - the one chosen goes on, one per way");
        const size_t rows = table.rows.size();
        offered.clear();
        dlg.setAsk([](const QString&) { return true; });   // "add it again?" - yes
        dlg.addPartToBom(QStringLiteral("BILL-CONN"));
        table.fromJson(doc.bom());
        check(table.rows.size() == rows + 1 && offered.isEmpty(),
              "bill contacts: a contact already on the bill - nothing more is added, and nothing is asked");
    }
    cat.disconnectLibrary(libFile);
    cat.remember();
    QFile::remove(libFile);
}

// ROUND FOURTEEN: pairs on a twisted pair; the run's length on the drawing whatever the pane's columns; a cable no
// wider than the face it goes into; the brick double-clicked brought up on the preview.
void testRoundFourteen() {
    {
        BrickDesign d;
        const QString a = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString t = d.addBrick(BrickType::TwistedPair, PartKind::Wire, 1, 0);
        const QString b = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        d.connect(a, t);
        d.connect(t, b);
        d.brick(t)->run.pairs = 3;
        int wires = 0;
        for (const Entity& e : buildBrickHarness(d, nullptr).entities)
            if (e.type == EntityType::TwistedPair) wires = e.meta.value(QStringLiteral("twistWires")).toInt();
        check(wires == 6, "pairs: three twisted pairs are six wires");
        check(BrickDesign::fromJson(d.toJson()).brick(t)->run.pairs == 3, "pairs: kept with the design");
    }
    {
        // Both parts in one column of the pane: the drawing still puts them the run's length apart, along the way
        // the face looks.
        BrickDesign d;
        const QString a = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString r = d.addBrick(BrickType::Run, PartKind::Cable, 0, 1);
        const QString b = d.addBrick(BrickType::End, PartKind::Connector, 0, 2);
        d.connect(a, r);
        d.connect(r, b);
        auto apart = [&d, &a, &b] {
            const BrickBuild bb = buildBrickHarness(d, nullptr);
            return std::abs(bb.faces.value(b + QStringLiteral(":0")).centre.x - bb.faces.value(a + QStringLiteral(":0")).centre.x);
        };
        const double was = apart();
        d.brick(r)->run.lengthMm += 100.0;
        const double now = apart();
        std::printf("  length: parts %.1f apart, %.1f after 100 more\n", was, now);
        check(now > was + 50.0, "length: typed into the run, it is on the drawing - the pane's columns do not stop it");
    }
    {
        // A cable into small faces: never wider than the face.
        BrickDesign d;
        const QString a = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString r = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString b = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        d.connect(a, r);
        d.connect(r, b);
        d.brick(r)->run.stack = {QStringLiteral("wires"), QStringLiteral("cable")};
        d.brick(r)->run.wires = 12;
        d.brick(a)->scale = 0.3;
        const BrickBuild bb = buildBrickHarness(d, nullptr);
        const double face = bb.faces.value(a + QStringLiteral(":0")).lengthMm;
        double body = 0.0;
        for (const Entity& e : bb.entities)
            if (e.type == EntityType::TwistedPair) body = e.meta.value(QStringLiteral("twistBodyWidth")).toDouble();
        std::printf("  cable: %.2f wide into a face %.2f\n", body, face);
        check(body > 0.0 && body <= face + 1e-6, "cable: never wider than the face of the part scaled down");
    }
    {
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        BrickWizardDialog dlg(&ed);
        dlg.resize(1400, 900);
        dlg.show();
        BrickDesign& w = dlg.design();
        const QString a = w.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString r = w.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString b = w.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        w.connect(a, r);
        w.connect(r, b);
        dlg.selectBrick(r);
        dlg.preview()->grab();
        const double z0 = dlg.preview()->zoom();
        emit dlg.canvas()->brickDoubleClicked(a);
        dlg.preview()->grab();
        std::printf("  double-click: preview zoom %.2f -> %.2f\n", z0, dlg.preview()->zoom());
        check(dlg.preview()->zoom() > z0 * 1.2, "double-click: the brick's part brought up on the preview");
    }
}

// ROUND FIFTEEN: a fanout between a joint and a part spreads at the part; the whole length to a joint ends at its
// face; each run dimension on the side it is told; a bridge wire in front covers the wires it crosses.
void testRoundFifteen() {
    {
        BrickDesign d;
        const QString y = d.addBrick(BrickType::End, PartKind::YSplit, 0, 0);
        const QString f = d.addBrick(BrickType::Fanout, PartKind::Wire, 1, 0);
        const QString x = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        d.connect(y, f);
        d.connect(f, x);
        const int side = d.fanoutSpreadSide(*d.brick(f));
        const bool xAtFinish = d.finishesOf(f).contains(x);
        check(side == (xAtFinish ? 1 : 0),
              "fanout: between a Y-split and a connector it spreads at the connector - the wires a bundle at the joint");
    }
    {
        BrickDesign d;
        const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString r = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString y = d.addBrick(BrickType::End, PartKind::YSplit, 2, 0);
        d.connect(x, r);
        d.connect(r, y);
        d.brick(r)->run.dimTotal = true;
        d.brick(r)->run.dimFans = true;
        const BrickBuild b = buildBrickHarness(d, nullptr);
        const BrickFace yf = b.faces.value(QStringLiteral("%1:%2").arg(y).arg(d.portOn(y, r)));
        const Entity* total = nullptr;
        const Entity* fan = nullptr;
        double runY = 0.0;
        for (const Entity& e : b.entities) {
            const QString kind = e.meta.value(QStringLiteral("brickDim")).toString();
            if (kind == QStringLiteral("total") && e.type == EntityType::Dimension) total = &e;
            if (kind == QStringLiteral("fanStart")) fan = &e;
            if (e.type == EntityType::TwistedPair) runY = e.points.front().y;
        }
        check(total && std::abs(std::max(total->a.x, total->b.x) - yf.centre.x) < 1.0,
              "dims: the whole length to a Y-split ends where the cable goes into it");
        check(total && fan && total->points[2].y < runY && fan->points[2].y > runY,
              "dims: by default the whole length below the run, the fans above");
        d.brick(r)->run.dimTotalBelow = false;
        d.brick(r)->run.dimFansBelow = true;
        for (const Entity& e : buildBrickHarness(d, nullptr).entities) {
            const QString kind = e.meta.value(QStringLiteral("brickDim")).toString();
            if (kind == QStringLiteral("total") && e.type == EntityType::Dimension)
                check(e.points[2].y > runY, "dims: the whole length told above goes above");
            if (kind == QStringLiteral("fanStart")) check(e.points[2].y < runY, "dims: the fans told below go below");
        }
        check(!BrickDesign::fromJson(d.toJson()).brick(r)->run.dimTotalBelow, "dims: the side kept with the design");
    }
    {
        BrickDesign d;
        const QString x0 = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString x1 = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        const QString br = d.addBrick(BrickType::Run, PartKind::Wire, 3, 1);
        const QString f = d.addBrick(BrickType::End, PartKind::Ferrule, 4, 1);
        makeBridgeWire(*d.brick(br));
        d.connect(x0, c);
        d.connect(c, x1);
        d.connect(c, br);
        d.connect(br, f);
        syncBridgeWires(d);
        d.brick(br)->z = 3;
        int masks = 0;
        for (const Entity& e : buildBrickHarness(d, nullptr).entities)
            if (e.meta.value(QStringLiteral("brickMask")).toBool() &&
                e.meta.value(QStringLiteral("brickId")).toString() == br)
                ++masks;
        check(masks > 0, "front: a bridge wire brought to the front covers the wires it crosses");
    }
}

// ROUND SIXTEEN: the live bill; the quantity typed; runs sharing faces kept in order and close; a fanout's bundle
// fitted to a joint's face; a boot linked to a part; a part on a fanout joined to a cable dragged.
void testRoundSixteen() {
    // THE LIVE BILL.
    {
        const QString libFile = QDir(QDir::tempPath()).filePath(QStringLiteral("edraw_live_bill.plb"));
        QFile::remove(libFile);
        {
            PartsLibrary lib;
            LibraryPart conn;
            conn.id = QStringLiteral("LIVE-CONN");
            conn.kind = PartKind::Connector;
            conn.mfrPn = QStringLiteral("LIVE-HOUSING");
            conn.params[QStringLiteral("pins")] = QStringLiteral("4");
            LibraryPart pin;
            pin.id = QStringLiteral("LIVE-PIN");
            pin.kind = PartKind::ContactPin;
            pin.mfrPn = QStringLiteral("LIVE-CONTACT");
            LibraryPart other;
            other.id = QStringLiteral("LIVE-OTHER");
            other.kind = PartKind::Connector;
            other.mfrPn = QStringLiteral("LIVE-OTHER");
            other.params[QStringLiteral("includePins")] = QStringLiteral("1");
            lib.parts = {conn, pin, other};
            addContactTie(lib, conn.id, pin.id);
            lib.save(libFile);
        }
        PartsCatalog cat;
        cat.openRemembered();
        cat.connectLibrary(libFile);
        cat.remember();
        {
            Document doc;
            Editor ed;
            ed.setDocument(&doc);
            BrickWizardDialog dlg(&ed);
            BrickDesign& w = dlg.design();
            auto qtyOf = [&doc](const QString& id) {
                BomTable t;
                t.fromJson(doc.bom());
                for (const BomRow& r : t.rows)
                    if (r.partId == id) return r.qty;
                return 0.0;
            };
            const QString a = w.addBrick(BrickType::End, PartKind::Connector, 0, 0);
            w.brick(a)->libPart = QStringLiteral("LIVE-CONN");
            dlg.selectBrick(a);
            check(qtyOf(QStringLiteral("LIVE-CONN")) == 1.0 && qtyOf(QStringLiteral("LIVE-PIN")) == 4.0,
                  "live bill: a part given to a brick goes on the bill, its contacts with it (one per way)");
            const QString b = w.addBrick(BrickType::End, PartKind::Connector, 0, 2);
            w.brick(b)->libPart = QStringLiteral("LIVE-CONN");
            dlg.selectBrick(b);
            check(qtyOf(QStringLiteral("LIVE-CONN")) == 2.0 && qtyOf(QStringLiteral("LIVE-PIN")) == 8.0,
                  "live bill: another brick of it, one more - and its contacts");
            w.brick(a)->libPart = QStringLiteral("LIVE-OTHER");
            dlg.selectBrick(a);
            check(qtyOf(QStringLiteral("LIVE-CONN")) == 1.0 && qtyOf(QStringLiteral("LIVE-OTHER")) == 1.0 &&
                      qtyOf(QStringLiteral("LIVE-PIN")) == 4.0,
                  "live bill: a part changed - the old one comes off, the new one goes on");
            dlg.canvas()->setSelection({b});
            dlg.canvas()->deleteSelected();
            dlg.selectBrick(a);
            check(qtyOf(QStringLiteral("LIVE-CONN")) == 0.0 && qtyOf(QStringLiteral("LIVE-PIN")) == 0.0,
                  "live bill: the brick deleted, its part and contacts off the bill");
            // THE QUANTITY TYPED.
            auto* bom = dlg.findChild<QTableWidget*>(QStringLiteral("brickBom"));
            int row = -1;
            for (int i = 0; bom && i < bom->rowCount(); ++i)
                if (bom->item(i, 3) && bom->item(i, 3)->text() == QStringLiteral("LIVE-OTHER")) row = i;
            if (bom && row >= 0) bom->item(row, 1)->setText(QStringLiteral("3"));
            check(row >= 0 && qtyOf(QStringLiteral("LIVE-OTHER")) == 3.0, "bom: the quantity typed over is the bill's");
        }
        cat.disconnectLibrary(libFile);
        cat.remember();
        QFile::remove(libFile);
    }
    // TWO RUNS BETWEEN THE SAME FACES: in the same order at both, and close between their fans.
    {
        BrickDesign d;
        const QString x0 = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString r1 = d.addBrick(BrickType::Fanout, PartKind::Wire, 1, -0.5);
        const QString r2 = d.addBrick(BrickType::TwistedPair, PartKind::Wire, 1, 0.5);
        const QString x1 = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        d.connect(x0, r1);
        d.connect(r1, x1);
        d.connect(x0, r2);
        d.connect(r2, x1);
        d.brick(r1)->run.wires = 4;
        d.brick(x0)->scale = 3.0;   // faces wide enough that the two runs start well apart
        d.brick(x1)->scale = 3.0;
        std::vector<Vec2> p1, p2;
        for (const Entity& e : buildBrickHarness(d, nullptr).entities)
            if (e.type == EntityType::TwistedPair) {
                if (e.meta.value(QStringLiteral("brickId")).toString() == r1) p1 = e.points;
                if (e.meta.value(QStringLiteral("brickId")).toString() == r2) p2 = e.points;
            }
        check(p1.size() >= 2 && p2.size() >= 2 &&
                  ((p1.front().y > p2.front().y) == (p1.back().y > p2.back().y)),
              "shared faces: two runs keep their order at both faces - they do not cross");
        if (p1.size() >= 4 && p2.size() >= 4) {
            auto midY = [](const std::vector<Vec2>& r) {
                double total = 0.0;
                for (size_t k = 1; k < r.size(); ++k) total += distance(r[k - 1], r[k]);
                double at = 0.0;
                for (size_t k = 1; k < r.size(); ++k) {
                    const double seg = distance(r[k - 1], r[k]);
                    if (at + seg >= total / 2.0 && seg > 1e-9) return lerp(r[k - 1], r[k], (total / 2.0 - at) / seg).y;
                    at += seg;
                }
                return r.back().y;
            };
            const double midGap = std::abs(midY(p1) - midY(p2));
            const double endGap = std::abs(p1.front().y - p2.front().y);
            std::printf("  shared faces: %.1f apart at the face, %.1f between the fans\n", endGap, midGap);
            check(midGap < endGap * 0.7, "shared faces: between their fans they run close together");
        }
    }
    // A BOOT LINKED TO A PART: face on face.
    {
        BrickDesign d;
        const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString boot = d.addBrick(BrickType::End, PartKind::HeatshrinkBoot, 1, 0);
        const QString r = d.addBrick(BrickType::Run, PartKind::Cable, 2, 0);
        const QString y = d.addBrick(BrickType::End, PartKind::Connector, 3, 0);
        QString why;
        check(d.portCount(*d.brick(boot)) == 2, "boot: a heatshrink boot has two sides");
        check(d.connect(x, boot, &why), "boot: linked straight to a part");
        d.connect(boot, r);
        d.connect(r, y);
        const BrickBuild b = buildBrickHarness(d, nullptr);
        const Vec2 onPart = b.faces.value(QStringLiteral("%1:%2").arg(x).arg(d.portOn(x, boot))).centre;
        const Vec2 onBoot = b.faces.value(QStringLiteral("%1:%2").arg(boot).arg(d.portOn(boot, x))).centre;
        std::printf("  boot: its face %.1f,%.1f on the part's %.1f,%.1f\n", onBoot.x, onBoot.y, onPart.x, onPart.y);
        check(distance(onPart, onBoot) < 1e-6, "boot: drawn with its face on the part's face");
    }
    // A PART ON A FANOUT JOINED TO A CABLE can be dragged.
    {
        BrickDesign d;
        const QString x0 = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString f = d.addBrick(BrickType::Fanout, PartKind::Wire, 2, 0);
        const QString x1 = d.addBrick(BrickType::End, PartKind::Connector, 3, 0);
        d.connect(x0, c);
        d.connect(c, f);
        d.connect(f, x1);
        const BrickBuild b = buildBrickHarness(d, nullptr);
        const double was = d.brick(f)->run.lengthMm;
        check(dragPartOnDrawing(d, b, x1, 30.0, 0.0) == f && d.brick(f)->run.lengthMm > was,
              "drag: a part on a fanout joined to a cable moves, the fanout's length following");
    }
}

// ROUND SEVENTEEN: runs sharing a Y-split's leg and a connector keep their order all the way (no crossing) and go
// straight; a part pulled in too close to bend lines up with the face it meets; R / Space / M turn and mirror a part
// on the drawing only.
void testRoundSeventeen() {
    // How high a route is a share of the way along it.
    auto yAlong = [](const std::vector<Vec2>& r, double t) {
        double total = 0.0;
        for (size_t i = 1; i < r.size(); ++i) total += distance(r[i - 1], r[i]);
        double s = total * t;
        for (size_t i = 1; i < r.size(); ++i) {
            const double d = distance(r[i - 1], r[i]);
            if (s <= d && d > 1e-12) return r[i - 1].y + (r[i].y - r[i - 1].y) * s / d;
            s -= d;
        }
        return r.back().y;
    };
    // Two connectors on cables into a Y-split; from its leg a twisted pair and a fanout to one connector - and a third
    // run of wires - with the pair above or below on the pane.
    auto yHarness = [](BrickDesign& d, int variant, QString& tp, QString& fo, QString& wr, QString& far) {
        const QString c1 = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString k1 = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString c2 = d.addBrick(BrickType::End, PartKind::Connector, 0, 1);
        const QString k2 = d.addBrick(BrickType::Run, PartKind::Cable, 1, 1);
        const QString y = d.addBrick(BrickType::End, PartKind::YSplit, 2, 0.4);
        tp = d.addBrick(BrickType::TwistedPair, PartKind::Wire, 3, variant == 1 ? 0.7 : 0.0);
        fo = d.addBrick(BrickType::Fanout, PartKind::Wire, 3, variant == 1 ? 0.0 : 0.7);
        far = d.addBrick(BrickType::End, PartKind::Connector, 4, 0.3);
        d.connect(c1, k1);
        d.connect(k1, y);
        d.connect(c2, k2);
        d.connect(k2, y);
        d.connect(y, tp);
        d.connect(tp, far);
        d.connect(y, fo);
        d.connect(fo, far);
        d.brick(fo)->run.wires = 4;
        d.brick(tp)->run.lengthMm = d.brick(fo)->run.lengthMm = 150;
        if (variant >= 2) {
            wr = d.addBrick(BrickType::Run, PartKind::Wire, 3, variant == 2 ? 0.35 : -0.3);
            d.connect(y, wr);
            d.connect(wr, far);
            d.brick(wr)->run.wires = 3;
            d.brick(wr)->run.lengthMm = 150;
        }
    };
    for (int variant = 0; variant < 4; ++variant) {
        BrickDesign d;
        QString tp, fo, wr, far;
        yHarness(d, variant, tp, fo, wr, far);
        std::vector<std::vector<Vec2>> routes;
        for (const Entity& e : buildBrickHarness(d, nullptr).entities) {
            const QString id = e.meta.value(QStringLiteral("brickId")).toString();
            if (e.type == EntityType::TwistedPair && (id == tp || id == fo || (!wr.isEmpty() && id == wr)))
                routes.push_back(e.points);
        }
        bool inOrder = routes.size() == (wr.isEmpty() ? 2u : 3u);
        size_t most = 0;
        for (size_t i = 0; inOrder && i < routes.size(); ++i) {
            most = std::max(most, routes[i].size());
            for (size_t j = i + 1; j < routes.size(); ++j) {
                const auto& p = routes[i];
                const auto& q = routes[j];
                // Above or below at the start, and the same at every station along the way.
                const bool above = p.front().y > q.front().y;
                for (int k = 0; k <= 20; ++k) {
                    const double t = k / 20.0;
                    if ((yAlong(p, t) > yAlong(q, t)) != above && std::abs(yAlong(p, t) - yAlong(q, t)) > 1e-6)
                        inOrder = false;
                }
            }
        }
        std::printf("  shared leg, variant %d: %s, at most %zu points\n", variant, inOrder ? "in order" : "CROSSING", most);
        check(inOrder, "shared faces: the runs off a Y-split's leg keep their order all the way - they never cross");
        check(most <= 6, "shared faces: no bends that are not needed - out of the fan, into the bundle, straight on");
    }
    // TWO CABLES OF VERY DIFFERENT LENGTHS off a Y-split's legs are drawn at their lengths - the short one's part
    // pulled in off its row, not left level with the long one's.
    {
        BrickDesign d;
        const QString p1 = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString k1 = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString p2 = d.addBrick(BrickType::End, PartKind::Connector, 0, 1);
        const QString k2 = d.addBrick(BrickType::Run, PartKind::Cable, 1, 1);
        const QString y = d.addBrick(BrickType::End, PartKind::YSplit, 2, 0.5);
        const QString k3 = d.addBrick(BrickType::Run, PartKind::Cable, 3, 0.5);
        const QString p3 = d.addBrick(BrickType::End, PartKind::Connector, 4, 0.5);
        d.connect(p1, k1);
        d.connect(k1, y);
        d.connect(p2, k2);
        d.connect(k2, y);
        d.connect(y, k3);
        d.connect(k3, p3);
        d.brick(k1)->run.lengthMm = 107.0;
        d.brick(k2)->run.lengthMm = 32.0;
        double l1 = 0.0, l2 = 0.0;
        for (const Entity& e : buildBrickHarness(d, nullptr).entities) {
            if (e.type != EntityType::TwistedPair) continue;
            double len = 0.0;
            for (size_t i = 1; i < e.points.size(); ++i) len += distance(e.points[i - 1], e.points[i]);
            if (e.meta.value(QStringLiteral("brickId")).toString() == k1) l1 = len;
            if (e.meta.value(QStringLiteral("brickId")).toString() == k2) l2 = len;
        }
        std::printf("  y-split legs: 107 drawn %.1f, 32 drawn %.1f\n", l1, l2);
        // (Within where each lands across the one face they share on this placeholder Y-split.)
        check(std::abs(l1 - 107.0) < 5.0 && std::abs(l2 - 32.0) < 5.0,
              "lengths: a long and a short cable off a Y-split are each drawn at their own length");
    }
    // PULLED IN TOO CLOSE TO BEND: lined up with the face it meets.
    {
        auto lined = [](double length, double turnDeg) {
            BrickDesign d;
            const QString a = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
            const QString r = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0.5);
            const QString b = d.addBrick(BrickType::End, PartKind::Connector, 2, 1);
            d.connect(a, r);
            d.connect(r, b);
            d.brick(r)->run.lengthMm = length;
            d.brick(a)->drawTurnDeg = turnDeg;
            const BrickBuild bb = buildBrickHarness(d, nullptr);
            const BrickFace fa = bb.faces.value(QStringLiteral("%1:%2").arg(a).arg(d.portOn(a, r)));
            const BrickFace fb = bb.faces.value(QStringLiteral("%1:%2").arg(b).arg(d.portOn(b, r)));
            return std::pair<BrickFace, BrickFace>{fa, fb};
        };
        {
            const auto [fa, fb] = lined(8.0, 0.0);
            std::printf("  lined up: faces %.1f,%.1f and %.1f,%.1f\n", fa.centre.x, fa.centre.y, fb.centre.x, fb.centre.y);
            check(std::abs(fa.centre.y - fb.centre.y) < 1e-6 && std::abs(distance(fa.centre, fb.centre) - 8.0) < 1e-6 &&
                      dot(fa.out, fb.out) < -0.999,
                  "lined up: a part too close to bend is set straight out of the face it meets, at the run's length");
        }
        {
            const auto [fa, fb] = lined(400.0, 0.0);
            check(std::abs(fa.centre.y - fb.centre.y) > 1.0, "lined up: with room to bend it stays on its own row");
        }
        {
            const auto [fa, fb] = lined(6.0, 45.0);
            std::printf("  lined up at a slant: out %.2f,%.2f and %.2f,%.2f\n", fa.out.x, fa.out.y, fb.out.x, fb.out.y);
            check(dot(fa.out, fb.out) < -0.999 && distance(fa.centre + fa.out * 6.0, fb.centre) < 1e-6,
                  "lined up: to a face at a slant it turns to the face's angle");
        }
    }
    // TURNED AND MIRRORED ON THE DRAWING ONLY.
    {
        BrickDesign d;
        const QString y = d.addBrick(BrickType::End, PartKind::YSplit, 0, 0);
        const QString r = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        d.connect(y, r);
        auto faceOf = [&](int port) {
            return buildBrickHarness(d, nullptr).faces.value(QStringLiteral("%1:%2").arg(y).arg(port));
        };
        const BrickFace plain1 = faceOf(1);
        d.brick(y)->drawTurnDeg = 90.0;
        const BrickFace turned1 = faceOf(1);
        check(std::abs(turned1.out.x - plain1.out.y) < 1e-6 && std::abs(turned1.out.y + plain1.out.x) < 1e-6,
              "on the drawing: turned 90 degrees clockwise, its faces look a quarter round");
        d.brick(y)->drawTurnDeg = 0.0;
        d.brick(y)->drawMirror = true;
        const BrickFace mirrored1 = faceOf(1);
        check(std::abs(mirrored1.out.x + plain1.out.x) < 1e-6 && std::abs(mirrored1.out.y - plain1.out.y) < 1e-6,
              "on the drawing: mirrored, left for right");
        d.brick(y)->drawTurnDeg = 45.0;
        const BrickDesign back = BrickDesign::fromJson(d.toJson());
        check(back.brick(y) && back.brick(y)->drawMirror && std::abs(back.brick(y)->drawTurnDeg - 45.0) < 1e-9,
              "on the drawing: its turn and mirror are saved with the harness");
    }
    // THE KEYS on the pane: R 45, Space 90, M mirror - the brick itself not turned.
    {
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        BrickWizardDialog dlg(&ed);
        BrickDesign& w = dlg.design();
        const QString x = w.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        dlg.canvas()->setSelection({x});
        auto press = [&](int key) {
            QKeyEvent ev(QEvent::KeyPress, key, Qt::NoModifier);
            QApplication::sendEvent(dlg.canvas(), &ev);
        };
        const Vec2 dir0 = w.portPaneDir(*w.brick(x), 0);
        press(Qt::Key_R);
        check(std::abs(w.brick(x)->drawTurnDeg - 45.0) < 1e-9 && w.brick(x)->rot == 0,
              "keys: R turns the part 45 degrees on the drawing");
        const Vec2 dir45 = w.portPaneDir(*w.brick(x), 0);
        check(std::abs(dot(dir45, dir0) - std::sqrt(0.5)) < 1e-6,
              "keys: R turns the brick's connection 45 degrees round it too");
        press(Qt::Key_Space);
        check(std::abs(w.brick(x)->drawTurnDeg - 135.0) < 1e-9, "keys: Space turns it 90 more");
        check(std::abs(dot(w.portPaneDir(*w.brick(x), 0), dir45)) < 1e-6,
              "keys: Space turns the brick's connection a quarter round it too");
        const QRectF box = dlg.canvas()->brickRect(*w.brick(x));
        const QPointF on = dlg.canvas()->portPoint(*w.brick(x), 0);
        check(on.x() >= box.left() - 1e-6 && on.x() <= box.right() + 1e-6 && on.y() >= box.top() - 1e-6 &&
                  on.y() <= box.bottom() + 1e-6,
              "keys: the turned connection still sits on the brick");
        // THE TICK: untied, the turn is the drawing's alone - the brick's connection back as it was.
        auto* tie = dlg.findChild<QCheckBox*>(QStringLiteral("brickTieTurn"));
        check(tie && tie->isChecked(), "tie: the bricks turn with the drawing unless told not to");
        if (tie) tie->setChecked(false);
        check(!w.tieBrickTurn && std::abs(dot(w.portPaneDir(*w.brick(x), 0), dir0) - 1.0) < 1e-6 &&
                  std::abs(w.brick(x)->drawTurnDeg - 135.0) < 1e-9,
              "tie: unticked, the brick is not turned - the drawing still is");
        check(!BrickDesign::fromJson(w.toJson()).tieBrickTurn, "tie: kept with the harness");
        if (tie) tie->setChecked(true);
        const Vec2 dirWas = w.portPaneDir(*w.brick(x), 0);
        const QPointF dotWas = dlg.canvas()->portPoint(*w.brick(x), 0);
        const QPointF mid = dlg.canvas()->brickRect(*w.brick(x)).center();
        press(Qt::Key_M);
        check(w.brick(x)->drawMirror, "keys: M mirrors it");
        const Vec2 dirNow = w.portPaneDir(*w.brick(x), 0);
        const QPointF dotNow = dlg.canvas()->portPoint(*w.brick(x), 0);
        check(std::abs(dirNow.x + dirWas.x) < 1e-6 && std::abs(dirNow.y - dirWas.y) < 1e-6 &&
                  (dotNow.x() - mid.x()) * (dotWas.x() - mid.x()) < 0.0,
              "keys: mirrored, the brick's connection is mirrored too - on its other side");
    }
}

// ROUND EIGHTEEN: dimensions measure only the runs between two bricks, a joint at an end from its face toward the
// other end; every dimension carries its figure as a text override; one control puts them all above or below; and
// the dimension wizard shows them.
void testRoundEighteen() {
    BrickDesign d;
    const QString x1 = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
    const QString k1 = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
    const QString y = d.addBrick(BrickType::End, PartKind::HeatshrinkBoot, 2, 0);
    const QString k2 = d.addBrick(BrickType::Run, PartKind::Cable, 3, 0);
    const QString x2 = d.addBrick(BrickType::End, PartKind::Connector, 4, 0);
    const QString s = d.addBrick(BrickType::End, PartKind::Splice, 2, 1);
    d.connect(x1, k1);
    d.connect(k1, y);
    d.connect(y, k2);
    d.connect(k2, x2);
    (void)s;
    d.brick(k1)->run.lengthMm = 100.0;
    d.brick(k2)->run.lengthMm = 50.0;
    d.brick(k1)->run.dimTotal = d.brick(k1)->run.dimFans = d.brick(k1)->run.dimBetween = true;
    d.dims.push_back({x1, x2, false});
    d.dims.push_back({x1, y, false});
    d.dims.push_back({y, x2, false});
    const BrickBuild b = buildBrickHarness(d, nullptr);
    QHash<int, double> manual;
    bool allWritten = true;
    int dims = 0;
    for (const Entity& e : b.entities) {
        if (e.type != EntityType::Dimension) continue;
        ++dims;
        if (e.meta.value(QStringLiteral("textOverride")).toString().isEmpty()) allWritten = false;
        if (e.meta.value(QStringLiteral("brickDim")).toString() == QLatin1String("manual"))
            manual.insert(e.meta.value(QStringLiteral("brickDimIndex")).toInt(),
                          e.meta.value(QStringLiteral("brickMeasured")).toDouble());
    }
    std::printf("  dims: connector to connector %.1f, to the joint %.1f, from the joint %.1f\n", manual.value(0),
                manual.value(1), manual.value(2));
    check(std::abs(manual.value(0) - 150.0) < 1e-6,
          "dims: connector to connector measures only the cables between - the part in the middle adds nothing");
    check(std::abs(manual.value(1) - 100.0) < 1e-6 && std::abs(manual.value(2) - 50.0) < 1e-6,
          "dims: to or from a joint, from its face nearest the other end - only the cable between");
    check(dims >= 6 && allWritten, "dims: every dimension says its figure as a text override");
    // ALL TO ONE SIDE.
    setAllDimensionsBelow(d, true);
    check(allDimensionsBelow(d) && d.dims[0].below && d.brick(k1)->run.dimFansBelow && d.brick(x1)->dimBelow,
          "dims: one setting puts every dimension below");
    setAllDimensionsBelow(d, false);
    check(!allDimensionsBelow(d) && !d.dims[1].below && !d.brick(k2)->run.dimTotalBelow,
          "dims: and every one above");
    // THE WIZARD'S CONTROLS.
    {
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        BrickWizardDialog dlg(&ed);
        dlg.design() = d;
        dlg.selectBrick(x1);
        auto* side = dlg.findChild<QComboBox*>(QStringLiteral("brickDimSide"));
        check(side != nullptr, "dims: a general Above / Below on the wizard's bar");
        if (side) {
            side->setCurrentIndex(1);
            emit side->activated(1);
        }
        check(allDimensionsBelow(dlg.design()), "dims: Below on the bar puts them all below");
        auto* open = dlg.findChild<QPushButton*>(QStringLiteral("brickDimensions"));
        check(open != nullptr, "dims: a Dimensions button at the bottom of the wizard");
        if (open) open->click();
        QDialog* wiz = dlg.dimensionWizard();
        auto* hand = wiz ? wiz->findChild<QTableWidget*>(QStringLiteral("dimWizHand")) : nullptr;
        auto* runs = wiz ? wiz->findChild<QTableWidget*>(QStringLiteral("dimWizRuns")) : nullptr;
        check(wiz && wiz->isVisible() && hand && hand->rowCount() == 3 && runs && runs->rowCount() == 2,
              "dims: the dimension wizard opens with the runs and the dimensions between bricks");
        if (hand && hand->rowCount() == 3)
            check(hand->item(0, 2) && hand->item(0, 2)->text().startsWith(QStringLiteral("150")),
                  "dims: the wizard says what each measures");
        auto* del = wiz ? wiz->findChild<QPushButton*>(QStringLiteral("dimWizDelete")) : nullptr;
        if (hand && del) {
            hand->setCurrentCell(0, 0);
            del->click();
        }
        check(dlg.design().dims.size() == 2, "dims: one deleted in the wizard");
    }
}

// ROUND NINETEEN: the dots on the bricks (on the bill; suits what it is linked to), loose fanouts and bridge wires
// with stripped ends, a transition at the start of a run built from its finish, a ferrule half any part's size, a
// bridge wire taking several wires and drawn over what it crosses, and the dimensions off the bar.
void testRoundNineteen() {
    // THE DOTS.
    {
        const QString libFile = QDir(QDir::tempPath()).filePath(QStringLiteral("edraw_marks.plb"));
        QFile::remove(libFile);
        {
            PartsLibrary lib;
            LibraryPart conn;
            conn.id = QStringLiteral("MARK-CONN");
            conn.kind = PartKind::Connector;
            conn.mfrPn = QStringLiteral("MARK-CONN");
            conn.params[QStringLiteral("pins")] = QStringLiteral("4");
            LibraryPart pin;
            pin.id = QStringLiteral("MARK-PIN");
            pin.kind = PartKind::ContactPin;
            pin.mfrPn = QStringLiteral("MARK-PIN");
            pin.params[QStringLiteral("gaugeMode")] = QStringLiteral("AWG");
            pin.params[QStringLiteral("gaugeMin")] = QStringLiteral("24");
            pin.params[QStringLiteral("gaugeMax")] = QStringLiteral("20");
            LibraryPart thin;
            thin.id = QStringLiteral("MARK-WIRE22");
            thin.kind = PartKind::Wire;
            thin.mfrPn = QStringLiteral("MARK-WIRE22");
            thin.params[QStringLiteral("gaugeMode")] = QStringLiteral("AWG");
            thin.params[QStringLiteral("gauge")] = QStringLiteral("22");
            LibraryPart thick = thin;
            thick.id = thick.mfrPn = QStringLiteral("MARK-WIRE16");
            thick.params[QStringLiteral("gauge")] = QStringLiteral("16");
            lib.parts = {conn, pin, thin, thick};
            addContactTie(lib, conn.id, pin.id);
            lib.save(libFile);
        }
        PartsCatalog cat;
        cat.openRemembered();
        cat.connectLibrary(libFile);
        cat.remember();
        {
            Document doc;
            Editor ed;
            ed.setDocument(&doc);
            BrickWizardDialog dlg(&ed);
            BrickDesign& w = dlg.design();
            const QString x = w.addBrick(BrickType::End, PartKind::Connector, 0, 0);
            const QString bare = w.addBrick(BrickType::End, PartKind::Connector, 0, 2);
            dlg.selectBrick(x);
            check(dlg.canvas()->markOf(bare).bom == -1 && dlg.canvas()->markOf(bare).compat == 1,
                  "dots: no part yet - nothing for the bill, and orange: not known yet");
            w.brick(x)->libPart = QStringLiteral("MARK-CONN");
            dlg.selectBrick(x);   // the live bill puts it on, with its contacts
            check(dlg.canvas()->markOf(x).bom == 1, "dots: green - on the BOM");
            check(dlg.canvas()->markOf(x).compat == 1, "dots: orange - not linked to anything yet");
            const QString c = w.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
            const QString y = w.addBrick(BrickType::End, PartKind::Connector, 2, 0);
            w.connect(x, c);
            w.connect(c, y);
            w.brick(c)->run.wires = 4;
            dlg.selectBrick(c);
            std::printf("  dots: wire size unknown - %d (%s)\n", dlg.canvas()->markOf(x).compat,
                        qPrintable(dlg.canvas()->markOf(x).why));
            check(dlg.canvas()->markOf(x).compat == 1, "dots: orange - the wires' size is not known");
            w.brick(c)->run.stackParts.insert(QStringLiteral("wires"), QStringLiteral("MARK-WIRE22"));
            dlg.selectBrick(c);
            std::printf("  dots: 22 AWG into 20-24 AWG contacts - %d (%s)\n", dlg.canvas()->markOf(x).compat,
                        qPrintable(dlg.canvas()->markOf(x).why));
            check(dlg.canvas()->markOf(x).compat == 0, "dots: green - its contacts on the BOM take the wires' size");
            w.brick(c)->run.stackParts.insert(QStringLiteral("wires"), QStringLiteral("MARK-WIRE16"));
            dlg.selectBrick(c);
            check(dlg.canvas()->markOf(x).compat == 2, "dots: red - wires too big for its contacts");
            w.brick(c)->run.stackParts.insert(QStringLiteral("wires"), QStringLiteral("MARK-WIRE22"));
            w.brick(c)->run.wires = 6;
            dlg.selectBrick(c);
            std::printf("  dots: six wires into four ways - %d (%s)\n", dlg.canvas()->markOf(x).compat,
                        qPrintable(dlg.canvas()->markOf(x).why));
            check(dlg.canvas()->markOf(x).compat == 2, "dots: red - more wires than the connector has ways");
            // A run's own dot: its layers' parts on the bill - put there by length, at its longest, by the live bill.
            check(dlg.canvas()->markOf(c).bom == 1, "dots: a run's wire put on the BOM by its length - green");
        }
        cat.disconnectLibrary(libFile);
        cat.remember();
        QFile::remove(libFile);
    }
    // THE STACK FITS TOGETHER, AND GOES INTO WHAT IT GOES INTO.
    {
        const QString libFile = QDir(QDir::tempPath()).filePath(QStringLiteral("edraw_stack.plb"));
        QFile::remove(libFile);
        {
            PartsLibrary lib;
            auto part = [](const char* id, PartKind k, std::initializer_list<std::pair<const char*, const char*>> ps) {
                LibraryPart p;
                p.id = p.mfrPn = QString::fromLatin1(id);
                p.kind = k;
                for (const auto& [key, v] : ps) p.params[QString::fromLatin1(key)] = QString::fromLatin1(v);
                return p;
            };
            lib.parts = {part("ST-WIRE", PartKind::Wire, {{"od", "1.5"}}),
                         part("ST-BRAID", PartKind::ShieldBraid, {{"diameterMin", "3"}, {"diameterMax", "6"}}),
                         part("ST-BRAIDBIG", PartKind::ShieldBraid, {{"diameterMin", "10"}, {"diameterMax", "20"}}),
                         part("ST-SHRINK", PartKind::Heatshrink, {{"entryMin", "3"}, {"entryMax", "9"}, {"wall", "0.5"}}),
                         part("ST-Y", PartKind::YSplit, {{"mainMin", "4"}, {"mainMax", "12"}, {"leg1Min", "4"},
                                                          {"leg1Max", "9"}, {"leg2Min", "1"}, {"leg2Max", "3"}})};
            lib.save(libFile);
        }
        PartsCatalog cat;
        cat.openRemembered();
        cat.connectLibrary(libFile);
        cat.remember();
        {
            Document doc;
            Editor ed;
            ed.setDocument(&doc);
            BrickWizardDialog dlg(&ed);
            BrickDesign& w = dlg.design();
            const QString c = w.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
            const QString y = w.addBrick(BrickType::End, PartKind::YSplit, 2, 0);
            const QString x = w.addBrick(BrickType::End, PartKind::Connector, 0, 0);
            w.connect(x, c);
            w.connect(c, y, nullptr, 1, 1);
            w.brick(y)->libPart = QStringLiteral("ST-Y");
            BrickRunSpec& r = w.brick(c)->run;
            r.wires = 4;
            r.stack = {QStringLiteral("wires"), QStringLiteral("braid"), QStringLiteral("heatshrink")};
            r.stackParts.insert(QStringLiteral("wires"), QStringLiteral("ST-WIRE"));
            r.stackParts.insert(QStringLiteral("braid"), QStringLiteral("ST-BRAID"));
            r.stackParts.insert(QStringLiteral("heatshrink"), QStringLiteral("ST-SHRINK"));
            dlg.selectBrick(c);
            std::printf("  stack: %d (%s); into the leg: %d (%s)\n", dlg.canvas()->markOf(c).compat,
                        qPrintable(dlg.canvas()->markOf(c).why), dlg.canvas()->markOf(y).compat,
                        qPrintable(dlg.canvas()->markOf(y).why));
            check(dlg.canvas()->markOf(c).compat == 0, "stack: wires, braid and heatshrink that fit one over another - green");
            check(dlg.canvas()->markOf(y).compat != 2, "stack: the whole bundle goes into its Y-split leg");
            r.stackParts.insert(QStringLiteral("braid"), QStringLiteral("ST-BRAIDBIG"));
            dlg.selectBrick(c);
            check(dlg.canvas()->markOf(c).compat == 2, "stack: a braid too big for the bundle under it - red");
            r.stackParts.insert(QStringLiteral("braid"), QStringLiteral("ST-BRAID"));
            w.disconnect(c, y);
            w.connect(c, y, nullptr, 1, 2);
            dlg.selectBrick(c);
            std::printf("  stack: into the small leg: %d (%s)\n", dlg.canvas()->markOf(y).compat,
                        qPrintable(dlg.canvas()->markOf(y).why));
            check(dlg.canvas()->markOf(y).compat == 2, "stack: a bundle too big for the leg it goes into - red");
        }
        cat.disconnectLibrary(libFile);
        cat.remember();
        QFile::remove(libFile);
    }
    // A LOOSE FANOUT: its wires stripped at the free end.
    {
        BrickDesign d;
        const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString f = d.addBrick(BrickType::Fanout, PartKind::Wire, 1, 0);
        d.connect(x, f);
        d.brick(f)->run.wires = 4;
        bool stripped = false;
        for (const Entity& e : buildBrickHarness(d, nullptr).entities)
            if (e.type == EntityType::TwistedPair && e.meta.value(QStringLiteral("brickId")).toString() == f)
                stripped = e.meta.value(QStringLiteral("twistStripFinish")).toBool() &&
                           e.meta.value(QStringLiteral("twistSpread")).toString() != QLatin1String("start");
        check(stripped, "loose: a fanout with nothing at its end spreads there, its wires stripped");
    }
    // A LOOSE BRIDGE WIRE: off its cable's face, a stripped end; several wires taken; over what it crosses.
    {
        BrickDesign d;
        const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString y = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        const QString br = d.addBrick(BrickType::Run, PartKind::Wire, 1, 1);
        d.connect(x, c);
        d.connect(c, y);
        d.brick(c)->run.wires = 4;
        makeBridgeWire(*d.brick(br));
        QString why;
        check(d.connect(c, br, &why), "loose: a bridge wire linked to its cable alone");
        syncBridgeWires(d);
        const BrickBuild b = buildBrickHarness(d, nullptr);
        bool bare = false, masked = false, drawn = false;
        for (size_t k = 0; k < b.entities.size(); ++k) {
            const Entity& e = b.entities[k];
            if (e.meta.value(QStringLiteral("mechRole")).toString() == QLatin1String("strip")) bare = true;
            if (e.type == EntityType::Wipeout && e.meta.value(QStringLiteral("brickMask")).toBool() &&
                e.meta.value(QStringLiteral("brickId")).toString() == br)
                masked = true;
            if (e.meta.value(QStringLiteral("brickId")).toString() == br && e.type == EntityType::Polyline) drawn = true;
        }
        check(drawn && bare, "loose: a bridge wire with nothing at its end is drawn, its end stripped");
        check(masked, "order: a bridge wire covers what it crosses - a mask goes down under it");
        d.brick(br)->run.bridgeWires = QStringLiteral("1,3");
        syncBridgeWires(d);
        check(d.brick(br)->run.wires == 2, "bridge: takes several of the cable's wires");
    }
    // A TRANSITION AT THE START of a run on a part at its finish only: at the loose end, its start.
    {
        auto sideOfBoot = [](bool linkAtFinish) {
            BrickDesign d;
            const QString x = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
            const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
            d.connect(c, x, nullptr, linkAtFinish ? 1 : 0, -1);
            d.brick(c)->run.transitions = RunEnds::Start;
            QString side;
            Vec2 first;
            for (const Entity& e : buildBrickHarness(d, nullptr).entities)
                if (e.type == EntityType::TwistedPair) {
                    side = e.meta.value(QStringLiteral("twistBodyTransitionSides")).toString();
                    first = e.points.front();
                }
            return side;
        };
        const QString atFinish = sideOfBoot(true);
        std::printf("  transition: on a part at its finish - drawn at the run's %s\n", qPrintable(atFinish));
        check(atFinish == QLatin1String("finish"),
              "transition: asked at the start of a run linked at its finish, it is at the loose end");
        check(sideOfBoot(false) == QLatin1String("start"), "transition: linked at its start, at the part");
    }
    // A FERRULE MATCHED TO ANY PART: half its size across.
    {
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        BrickWizardDialog dlg(&ed);
        BrickDesign& w = dlg.design();
        const QString x = w.addBrick(BrickType::End, PartKind::YSplit, 0, 0);
        const QString f = w.addBrick(BrickType::Fanout, PartKind::Wire, 1, 0);
        const QString fe = w.addBrick(BrickType::End, PartKind::Ferrule, 2, 0);
        w.connect(x, f);
        w.connect(f, fe);
        dlg.selectBrick(f);
        check(dlg.scalePartToMatch(fe, x), "match: a ferrule matched to a Y-split");
        dlg.selectBrick(f);
        const BrickBuild b = buildBrickHarness(w, nullptr);
        auto height = [&b](const QString& id) {
            Bounds box;
            for (const Entity& e : b.entities)
                if (e.meta.value(QStringLiteral("brickId")).toString() == id && e.type != EntityType::Text &&
                    e.type != EntityType::Dimension && e.meta.value(QStringLiteral("mechRole")).toString() != QLatin1String("dim"))
                    box.expand(e.bounds());
            return box.valid ? box.height() : 0.0;
        };
        std::printf("  match: ferrule %.1f across, the part %.1f\n", height(fe), height(x));
        check(height(x) > 0.0 && std::abs(height(fe) - 0.5 * height(x)) < 0.1 * height(x),
              "match: a ferrule is half the size of any part it is matched to");
    }
    // THE BAR: no dimension controls - only the Dimensions button.
    {
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        BrickWizardDialog dlg(&ed);
        dlg.show();
        auto* tool = dlg.findChild<QToolButton*>(QStringLiteral("brickDimTool"));
        auto* from = dlg.findChild<QComboBox*>(QStringLiteral("brickDimFrom"));
        check(tool && !tool->isVisible() && from && !from->isVisible() &&
                  dlg.findChild<QPushButton*>(QStringLiteral("brickDimensions")),
              "bar: the dimension controls are in the Dimensions wizard, not on the bar");
    }
}

// ROUND TWENTY: a dimension's line taken hold of anywhere along it; a dimension between two bricks goes the side it is
// set, whatever its part says; a bridge wire's mask one piece round its bend.
void testRoundTwenty() {
    // THE LINE OF A PICKED DIMENSION, pressed anywhere along it and pulled, moves off with the pointer.
    {
        Document d;
        d.entities().clear();
        Entity dim;
        dim.id = QStringLiteral("dm");
        dim.type = EntityType::Dimension;
        dim.meta.insert(QStringLiteral("dimType"), QStringLiteral("linear"));
        dim.meta.insert(QStringLiteral("dimAcross"), true);
        dim.a = {0, 0};
        dim.b = {100, 0};
        dim.points = {{0, 0}, {100, 0}, {50, 40}};
        d.entities().push_back(dim);
        Editor ed;
        ed.setDocument(&d);
        ed.selectById(QStringLiteral("dm"));
        ed.onPointerPress({20, 40}, Qt::LeftButton, Qt::NoModifier);   // on the line, far from its middle joint
        ed.onPointerMove({20, 70});
        ed.onPointerPress({20, 70}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerRelease({20, 70}, Qt::LeftButton);
        const Entity& after = d.entities().front();
        std::printf("  dimension line: taken at 20,40, now at y %.1f\n", after.points.size() > 2 ? after.points[2].y : -1.0);
        check(after.points.size() > 2 && std::abs(after.points[2].y - 70.0) < 1.0 && std::abs(after.a.x) < 1e-6 &&
                  std::abs(after.b.x - 100.0) < 1e-6,
              "dimension: pulled by its line anywhere along it, the line moves - what it measures does not");
    }
    // A DIMENSION'S OWN SIDE: set above, it is above even from a part set to Below.
    {
        BrickDesign d;
        const QString x0 = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString r = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString x1 = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        d.connect(x0, r);
        d.connect(r, x1);
        d.brick(x0)->dimBelow = true;
        d.dims.push_back({x0, x1, false});
        double y = 0.0;
        for (const Entity& e : buildBrickHarness(d, nullptr).entities)
            if (e.meta.value(QStringLiteral("brickDim")).toString() == QLatin1String("manual")) y = e.points[2].y;
        check(y > 0.0, "dims: a dimension set Above is above, though its part says Below");
    }
    // UPDATE FROM LIBRARY: the part changed in the library, the brick and its line on the bill follow.
    {
        const QString libFile = QDir(QDir::tempPath()).filePath(QStringLiteral("edraw_update.plb"));
        QFile::remove(libFile);
        auto write = [&libFile](const QString& description) {
            PartsLibrary lib;
            LibraryPart p;
            p.id = QStringLiteral("UPD-CONN");
            p.kind = PartKind::Connector;
            p.mfrPn = QStringLiteral("UPD-CONN");
            p.description = description;
            p.params[QStringLiteral("includePins")] = QStringLiteral("1");
            lib.parts = {p};
            lib.save(libFile);
        };
        write(QStringLiteral("Old description"));
        PartsCatalog cat;
        cat.openRemembered();
        cat.connectLibrary(libFile);
        cat.remember();
        {
            Document doc;
            Editor ed;
            ed.setDocument(&doc);
            BrickWizardDialog dlg(&ed);
            BrickDesign& w = dlg.design();
            const QString x = w.addBrick(BrickType::End, PartKind::Connector, 0, 0);
            w.brick(x)->libPart = QStringLiteral("UPD-CONN");
            dlg.selectBrick(x);   // on the bill
            write(QStringLiteral("New description"));
            check(dlg.updateBrickFromLibrary(x), "update: the brick's part read again from the library");
            QString said;
            BomTable t;
            t.fromJson(doc.bom());
            for (const BomRow& r : t.rows)
                if (r.partId == QLatin1String("UPD-CONN")) said = r.description;
            std::printf("  update: the bill now says \"%s\"\n", qPrintable(said));
            check(said.contains(QStringLiteral("New description")), "update: its line on the bill says what the library says now");
        }
        cat.disconnectLibrary(libFile);
        cat.remember();
        QFile::remove(libFile);
    }
    // A BRIDGE WIRE'S MASK: one piece.
    {
        BrickDesign d;
        const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString y = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        const QString br = d.addBrick(BrickType::Run, PartKind::Wire, 1, 1);
        const QString f = d.addBrick(BrickType::End, PartKind::Ferrule, 0, 1);
        d.connect(x, c);
        d.connect(c, y);
        d.brick(c)->run.wires = 4;
        makeBridgeWire(*d.brick(br));
        d.connect(c, br, nullptr, 0, 1);
        d.connect(br, f);
        syncBridgeWires(d);
        int masks = 0;
        for (const Entity& e : buildBrickHarness(d, nullptr).entities)
            if (e.type == EntityType::Wipeout && e.meta.value(QStringLiteral("brickMask")).toBool() &&
                e.meta.value(QStringLiteral("brickId")).toString() == br)
                ++masks;
        check(masks == 1, "bridge: its mask is one piece round its bend - no joins drawn across the wire");
    }
}

// ROUND TWENTY-ONE: a cable to two parts at one end - their lengths locked together until Shift gives one its own,
// the longest what is bought; the cable on to each part; the transition at the end it was set at; a bridge wire out
// of the branch it is told.
void testRoundTwentyOne() {
    BrickDesign d;
    const QString sa = d.addBrick(BrickType::End, PartKind::Connector, 0, 1);
    const QString a2 = d.addBrick(BrickType::End, PartKind::Connector, 0, 3);
    const QString sb = d.addBrick(BrickType::End, PartKind::Connector, 4, 2);
    const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 2, 2);
    d.connect(sa, c, nullptr, -1, 0);
    d.connect(a2, c, nullptr, -1, 0);
    d.connect(c, sb, nullptr, 1, -1);
    d.brick(c)->run.wires = 4;
    d.brick(c)->run.lengthMm = 200;
    d.brick(c)->run.transitions = RunEnds::Finish;
    check(lengthLockedWith(d, sa) == QStringList{a2}, "lengths: two parts at one end of a cable are locked together");
    BrickBuild b = buildBrickHarness(d, nullptr);
    QString trunkSides;
    int bodies = 0;
    for (const Entity& e : b.entities)
        if (e.type == EntityType::TwistedPair && e.meta.value(QStringLiteral("brickId")).toString() == c) {
            if (e.meta.value(QStringLiteral("twistBody")).toString() == QLatin1String("cable")) ++bodies;
            if (!e.meta.value(QStringLiteral("twistBodyTransitionSides")).toString().isEmpty() &&
                e.meta.value(QStringLiteral("twistBodyTransitionSides")).toString() != QLatin1String("none"))
                trunkSides = e.meta.value(QStringLiteral("twistBodyTransitionSides")).toString();
        }
    std::printf("  split: %d runs with the cable's body, the transition at the %s\n", bodies, qPrintable(trunkSides));
    check(bodies == 1, "split: the cable's body ends where it divides - its branches are wires (or heatshrink, when asked)");
    check(trunkSides == QLatin1String("start"),
          "split: a transition set at the finish is at that end of the cable (the far part), not where it divides");
    // SHIFT AND DRAG: that part gets a length of its own; the other keeps the run's.
    const QString moved = dragPartOnDrawing(d, b, a2, -60.0, 0.0, true);
    check(moved == c && d.brick(c)->run.lengthMm == 200.0 && d.link(c, a2)->lengthMm > 200.0,
          "lengths: Shift and drag - this part's branch its own length, the cable's length as it was");
    check(lengthLockedWith(d, sa).isEmpty() && lengthLockedWith(d, a2).isEmpty(), "lengths: no longer locked together");
    std::printf("  lengths: run %.0f, the branch to the second part %.0f, the longest %.0f\n", d.brick(c)->run.lengthMm,
                d.link(c, a2)->lengthMm, runLongestMm(d, *d.brick(c)));
    check(std::abs(runLongestMm(d, *d.brick(c)) - d.link(c, a2)->lengthMm) < 1e-9, "lengths: the longest branch is the cable's length to buy");
    b = buildBrickHarness(d, nullptr);
    const double xSa = b.faces.value(sa + QStringLiteral(":0")).centre.x;
    const double xA2 = b.faces.value(a2 + QStringLiteral(":0")).centre.x;
    check(xA2 < xSa - 30.0, "lengths: the part with the longer branch is drawn further off");
    check(BrickDesign::fromJson(d.toJson()).link(c, a2)->lengthMm == d.link(c, a2)->lengthMm, "lengths: kept with the harness");
    // A BRIDGE WIRE OUT OF THE BRANCH IT IS TOLD.
    {
        const QString fe = d.addBrick(BrickType::End, PartKind::Ferrule, 0, 0);
        const QString br = d.addBrick(BrickType::Run, PartKind::Wire, 1, 0);
        makeBridgeWire(*d.brick(br));
        d.connect(c, br, nullptr, 0, 1);
        d.connect(br, fe);
        syncBridgeWires(d);
        auto startOfBridge = [&] {
            for (const Entity& e : buildBrickHarness(d, nullptr).entities)
                if (e.meta.value(QStringLiteral("brickId")).toString() == br && !e.points.empty() &&
                    e.type != EntityType::Wipeout)
                    return e.points.back().y > e.points.front().y ? e.points.front() : e.points.back();
            return Vec2{};
        };
        d.brick(br)->run.bridgeAt = sa;
        const BrickBuild bb = buildBrickHarness(d, nullptr);
        const Vec2 faceSa = bb.faces.value(sa + QStringLiteral(":0")).centre;
        const Vec2 faceA2 = bb.faces.value(a2 + QStringLiteral(":0")).centre;
        const Vec2 atSa = startOfBridge();
        d.brick(br)->run.bridgeAt = a2;
        const Vec2 atA2 = startOfBridge();
        std::printf("  bridge: out of the first branch at %.1f,%.1f, of the second at %.1f,%.1f\n", atSa.x, atSa.y, atA2.x, atA2.y);
        check(distance(atSa, faceSa) < distance(atSa, faceA2) && distance(atA2, faceA2) < distance(atA2, faceSa),
              "bridge: it comes out of the branch it is told");
    }
}

// ROUND TWENTY-TWO: after a cable divides, bare wires or wires under heatshrink - with a transition where the body ends
// and at a branch's end when asked - the branches from the middle of its end, the wires each picked for a branch;
// heatshrink over each wire end where the wires fan out.
void testRoundTwentyTwo() {
    auto harness = [](BrickDesign& d, QString& c, QString& sa, QString& a2) {
        sa = d.addBrick(BrickType::End, PartKind::Connector, 0, 1);
        a2 = d.addBrick(BrickType::End, PartKind::Connector, 0, 3);
        const QString sb = d.addBrick(BrickType::End, PartKind::Connector, 4, 2);
        c = d.addBrick(BrickType::Run, PartKind::Cable, 2, 2);
        d.connect(sa, c, nullptr, -1, 0);
        d.connect(a2, c, nullptr, -1, 0);
        d.connect(c, sb, nullptr, 1, -1);
        d.brick(c)->run.wires = 6;
    };
    {
        BrickDesign d;
        QString c, sa, a2;
        harness(d, c, sa, a2);
        BrickBuild b = buildBrickHarness(d, nullptr);
        std::vector<const Entity*> runs;
        for (const Entity& e : b.entities)
            if (e.type == EntityType::TwistedPair && e.meta.value(QStringLiteral("brickId")).toString() == c) runs.push_back(&e);
        int bodied = 0;
        Vec2 trunkEnd;
        std::vector<Vec2> starts;
        for (const Entity* e : runs) {
            if (e->meta.value(QStringLiteral("twistWires")).toInt() == 6) trunkEnd = e->points.back();
            else starts.push_back(e->points.front());
            if (!e->meta.value(QStringLiteral("twistBody")).toString().isEmpty()) ++bodied;
        }
        check(runs.size() == 3 && bodied == 1, "branches: after the cable's body the wires fan out bare - the body is on the trunk only");
        check(starts.size() == 2 && distance(starts[0], starts[1]) < 1e-6 && distance(starts[0], trunkEnd) < 1e-6,
              "branches: every branch starts at the middle of the cable body's end");
        // UNDER HEATSHRINK: a transition where the body ends, heatshrink on each branch, one at a branch's end.
        d.brick(c)->run.branchCover = 1;
        d.link(c, a2)->endBoot = true;
        b = buildBrickHarness(d, nullptr);
        int shrunk = 0;
        for (const Entity& e : b.entities)
            if (e.type == EntityType::TwistedPair && e.meta.value(QStringLiteral("brickId")).toString() == c &&
                e.meta.value(QStringLiteral("twistBody")).toString() == QLatin1String("heatshrink"))
                ++shrunk;
        std::printf("  branches: %d under heatshrink, %d transitions\n", shrunk, b.transitionCount.value(c));
        check(shrunk == 2, "branches: under heatshrink, each branch is heatshrink over its wires");
        check(b.transitionCount.value(c) == 2, "branches: a transition where the body ends, and one at the end of the branch asked");
        // THE WIRES PICKED FOR A BRANCH.
        d.link(c, sa)->wireList = QStringLiteral("1,2,3,4");
        b = buildBrickHarness(d, nullptr);
        int toSa = 0;
        for (const Entity& e : b.entities)
            if (e.type == EntityType::TwistedPair && e.meta.value(QStringLiteral("brickId")).toString() == c &&
                e.meta.value(QStringLiteral("twistWires")).toInt() != 6 && distance(e.points.back(), b.faces.value(sa + QStringLiteral(":0")).centre) < 1.0)
                toSa = e.meta.value(QStringLiteral("twistWires")).toInt();
        check(toSa == 4, "branches: the wires picked for a branch are its wires");
        check(BrickDesign::fromJson(d.toJson()).link(c, sa)->wireList == QStringLiteral("1,2,3,4") &&
                  BrickDesign::fromJson(d.toJson()).brick(c)->run.branchCover == 1,
              "branches: kept with the harness");
    }
    // A TRANSITION AT A LOOSE END: a cable on a part at one end only, its transition asked at both ends, has it at
    // the loose end too.
    {
        BrickDesign d;
        const QString x = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        d.connect(c, x, nullptr, 1, -1);
        d.brick(c)->run.transitions = RunEnds::Both;
        const BrickBuild b = buildBrickHarness(d, nullptr);
        check(b.transitionCount.value(c) == 2, "transition: at the loose end of a cable too, not only at its part");
    }
    // A FAN-OUT DRAWN ON THE SHEET (the ribbon's): heatshrink over the end of each wire where it fans out.
    {
        Entity fan;
        fan.type = EntityType::TwistedPair;
        fan.id = QStringLiteral("fan-shrink");
        fan.points = {{0.0, 0.0}, {100.0, 0.0}};
        fan.a = fan.points.front();
        fan.b = fan.points.back();
        fan.meta.insert(QStringLiteral("twistWires"), 4);
        fan.meta.insert(QStringLiteral("twistStraight"), true);
        fan.meta.insert(QStringLiteral("twistSpread"), QStringLiteral("finish"));
        check(twistedPairEndSleeves(fan).empty(), "fanout: no heatshrink over the wire ends unless asked");
        fan.meta.insert(QStringLiteral("twistEndShrink"), true);
        fan.meta.insert(QStringLiteral("twistEndShrinkLen"), 8.0);
        const auto sleeves = twistedPairEndSleeves(fan);
        double reach = 0.0;
        for (const auto& ring : sleeves)
            for (const Vec2& v : ring) reach = std::max(reach, 100.0 - v.x);
        std::printf("  fanout: %zu sleeves, reaching %.1f back from the end\n", sleeves.size(), reach);
        check(sleeves.size() == 4 && reach > 7.0 && reach < 9.5,
              "fanout: a heatshrink over each wire's end where it fans out, as long as said");
    }
    // HEATSHRINK OVER EACH WIRE END where they fan out.
    {
        BrickDesign d;
        const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString r = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString y = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        d.connect(x, r);
        d.connect(r, y);
        d.brick(r)->run.wires = 4;
        d.brick(r)->run.fanEndShrink = true;
        d.brick(r)->run.fanEndShrinkMm = 6.0;
        const BrickBuild b = buildBrickHarness(d, nullptr);
        int sleeves = 0;
        for (const Entity& e : b.entities)
            if (e.meta.value(QStringLiteral("mechRole")).toString() == QLatin1String("fanShrink")) ++sleeves;
        std::printf("  wire ends: %d sleeved\n", sleeves);
        check(sleeves == 8 && b.fanEndCount.value(r) == 8, "wire ends: a heatshrink over each wire's end, at both fans");
    }
}

// ROUND TWENTY-THREE: a heatshrink's recovered wall in the stack's size (0.30 mm assumed, and said, when the library
// has none); solid wire into crimp contacts is a fault; a connector says how its contacts take a wire; the run's
// figures framed by what they are about.
void testRoundTwentyThree() {
    auto hasField = [](PartKind k, const char* key) {
        for (const PartField& f : partFields(k))
            if (f.key == QLatin1String(key)) return true;
        return false;
    };
    check(hasField(PartKind::Connector, "termination"), "library: a connector says whether its contacts crimp or solder");
    check(hasField(PartKind::HeatshrinkBoot, "wall") && hasField(PartKind::YSplit, "wall") && hasField(PartKind::Heatshrink, "wall"),
          "library: a heatshrink, a boot and a Y-split each have a recovered wall thickness");
    const QString libFile = QDir(QDir::tempPath()).filePath(QStringLiteral("edraw_walls.plb"));
    QFile::remove(libFile);
    {
        PartsLibrary lib;
        auto part = [](const char* id, PartKind k, std::initializer_list<std::pair<const char*, const char*>> ps) {
            LibraryPart p;
            p.id = p.mfrPn = QString::fromLatin1(id);
            p.kind = k;
            for (const auto& [key, v] : ps) p.params[QString::fromLatin1(key)] = QString::fromLatin1(v);
            return p;
        };
        lib.parts = {part("W-SOLID", PartKind::Wire, {{"od", "1.5"}, {"conductor", "Solid"}, {"gaugeMode", "AWG"}, {"gauge", "22"}}),
                     part("W-STRAND", PartKind::Wire, {{"od", "1.5"}, {"conductor", "Stranded"}, {"gaugeMode", "AWG"}, {"gauge", "22"}}),
                     part("HS-NOWALL", PartKind::Heatshrink, {{"entryMin", "3"}, {"entryMax", "9"}}),
                     part("HS-WALL", PartKind::Heatshrink, {{"entryMin", "3"}, {"entryMax", "9"}, {"wall", "0.6"}}),
                     part("C-CRIMP", PartKind::Connector, {{"pins", "4"}, {"termination", "Crimp"}, {"includePins", "1"}}),
                     part("C-SOLDER", PartKind::Connector, {{"pins", "4"}, {"termination", "Solder"}, {"includePins", "1"}})};
        lib.save(libFile);
    }
    PartsCatalog cat;
    cat.openRemembered();
    cat.connectLibrary(libFile);
    cat.remember();
    {
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        BrickWizardDialog dlg(&ed);
        BrickDesign& w = dlg.design();
        const QString x = w.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString c = w.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString y = w.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        w.connect(x, c);
        w.connect(c, y);
        BrickRunSpec& r = w.brick(c)->run;
        r.wires = 4;
        r.stack = {QStringLiteral("wires"), QStringLiteral("heatshrink")};
        r.stackParts.insert(QStringLiteral("wires"), QStringLiteral("W-STRAND"));
        r.stackParts.insert(QStringLiteral("heatshrink"), QStringLiteral("HS-NOWALL"));
        dlg.selectBrick(c);
        const BrickMark noWall = dlg.canvas()->markOf(c);
        std::printf("  wall: %d (%s)\n", noWall.compat, qPrintable(noWall.why));
        check(noWall.compat == 1 && noWall.why.contains(QStringLiteral("0.30 mm")),
              "wall: not in the library - 0.30 mm assumed, and said");
        r.stackParts.insert(QStringLiteral("heatshrink"), QStringLiteral("HS-WALL"));
        dlg.selectBrick(c);
        check(dlg.canvas()->markOf(c).compat == 0, "wall: given in the library, it is used - nothing to say");
        // SOLID WIRE INTO CRIMP CONTACTS.
        w.brick(x)->libPart = QStringLiteral("C-CRIMP");
        r.stackParts.insert(QStringLiteral("wires"), QStringLiteral("W-SOLID"));
        dlg.selectBrick(c);
        std::printf("  solid into crimp: %d (%s)\n", dlg.canvas()->markOf(x).compat, qPrintable(dlg.canvas()->markOf(x).why));
        check(dlg.canvas()->markOf(x).compat == 2 && dlg.canvas()->markOf(x).why.contains(QStringLiteral("solid")),
              "crimp: solid wire into crimp contacts - red, and why");
        w.brick(x)->libPart = QStringLiteral("C-SOLDER");
        dlg.selectBrick(c);
        check(!dlg.canvas()->markOf(x).why.contains(QStringLiteral("solid")), "crimp: into solder contacts, solid wire is fine");
        // THE RUN'S FIGURES IN FRAMES.
        check(dlg.findChild<QGroupBox*>(QStringLiteral("brickGroupFans")) && dlg.findChild<QGroupBox*>(QStringLiteral("brickGroupBody")) &&
                  dlg.findChild<QGroupBox*>(QStringLiteral("brickGroupTransition")),
              "run: its fans, body and transition heatshrink each framed together");
    }
    cat.disconnectLibrary(libFile);
    cat.remember();
    QFile::remove(libFile);
}

// ROUND TWENTY-FOUR: a dimension picked on the preview and dragged in or out; branches off at once where the cable's
// body ends.
void testRoundTwentyFour() {
    {
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        BrickWizardDialog dlg(&ed);
        dlg.resize(1500, 900);
        dlg.show();
        BrickDesign& w = dlg.design();
        const QString x = w.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString c = w.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString y = w.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        w.connect(x, c);
        w.connect(c, y);
        w.brick(c)->run.dimTotal = true;
        w.brick(c)->run.dimTotalBelow = false;
        dlg.selectBrick(c);
        QApplication::processEvents();
        dlg.preview()->grab();
        const Entity* dim = nullptr;
        for (const Entity& e : dlg.preview()->entities())
            if (e.type == EntityType::Dimension && !e.meta.value(QStringLiteral("brickDimKey")).toString().isEmpty()) dim = &e;
        check(dim != nullptr, "dimension: a run's dimension carries its key");
        if (dim) {
            Vec2 d1, d2;
            dimensionLineEnds(*dim, d1, d2);
            const double y0 = dim->points[2].y;
            const QString key = dim->meta.value(QStringLiteral("brickDimKey")).toString();
            const QPointF at = dlg.preview()->screenAt(d1 + (d2 - d1) * 0.25);
            const QPointF to = at + QPointF(0, -40);   // up the screen
            QMouseEvent press(QEvent::MouseButtonPress, at, dlg.preview()->mapToGlobal(at), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(dlg.preview(), &press);
            check(dlg.selectedDimension() == key, "dimension: pressed on its line, it is picked");
            QMouseEvent move(QEvent::MouseMove, to, dlg.preview()->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(dlg.preview(), &move);
            QMouseEvent release(QEvent::MouseButtonRelease, to, dlg.preview()->mapToGlobal(to), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(dlg.preview(), &release);
            double y1 = y0;
            for (const Entity& e : dlg.preview()->entities())
                if (e.meta.value(QStringLiteral("brickDimKey")).toString() == key) y1 = e.points[2].y;
            std::printf("  dimension: dragged from %.1f to %.1f (shift %.1f)\n", y0, y1, w.dimShift.value(key));
            check(y1 > y0 + 1.0 && w.dimShift.value(key) > 1.0, "dimension: dragged up, its line goes up - and stays there");
            check(BrickDesign::fromJson(w.toJson()).dimShift.value(key) == w.dimShift.value(key), "dimension: kept with the harness");
        }
    }
    // BRANCHES OFF AT ONCE where the cable's body ends: each branch's first leg already on the slant.
    {
        BrickDesign d;
        const QString sa = d.addBrick(BrickType::End, PartKind::Connector, 0, 1);
        const QString a2 = d.addBrick(BrickType::End, PartKind::Connector, 0, 3);
        const QString sb = d.addBrick(BrickType::End, PartKind::Connector, 4, 2);
        const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 2, 2);
        d.connect(sa, c, nullptr, -1, 0);
        d.connect(a2, c, nullptr, -1, 0);
        d.connect(c, sb, nullptr, 1, -1);
        d.brick(c)->run.wires = 6;
        bool slanted = true;
        int branches = 0;
        for (const Entity& e : buildBrickHarness(d, nullptr).entities)
            if (e.type == EntityType::TwistedPair && e.meta.value(QStringLiteral("brickId")).toString() == c &&
                e.meta.value(QStringLiteral("twistWires")).toInt() != 6 && e.points.size() >= 2) {
                ++branches;
                // STRAIGHT OUT A LITTLE FIRST, then on the slant.
                const Vec2 first = (e.points[1] - e.points[0]).normalized();
                const Vec2 second = e.points.size() >= 3 ? (e.points[2] - e.points[1]).normalized() : first;
                if (std::abs(first.y) > 0.3 || std::abs(second.y) < 0.3) slanted = false;
            }
        check(branches == 2 && slanted, "branches: each straight out of the cable's body a little, then toward its part");
    }
}

// ROUND TWENTY-FIVE: the BRANCH brick - a cable dividing into branches each with its own stack and transitions; the
// cable's body over the wires it divides into; a bridge wire on a cable's end with nothing else there; heatshrinks
// inside the backshell counted, not drawn; Match lit when matched.
void testRoundTwentyFive() {
    BrickDesign d;
    const QString sa = d.addBrick(BrickType::End, PartKind::Connector, 0, 1);
    const QString a2 = d.addBrick(BrickType::End, PartKind::Connector, 0, 3);
    const QString sb = d.addBrick(BrickType::End, PartKind::Connector, 4, 2);
    const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 3, 2);
    const QString b1 = d.addBrick(BrickType::Run, PartKind::Wire, 1, 1);
    const QString b2 = d.addBrick(BrickType::Run, PartKind::Wire, 1, 3);
    makeBranch(*d.brick(b1));
    makeBranch(*d.brick(b2));
    d.connect(c, sb, nullptr, 1, -1);
    d.brick(c)->run.wires = 6;
    QString why;
    check(d.connect(b1, c, &why, 1, 0) && d.connect(b2, c, &why, 1, 0), "branch: two branches joined to one end of a cable");
    d.connect(sa, b1, nullptr, -1, 0);
    d.connect(a2, b2, nullptr, -1, 0);
    d.brick(b1)->run.bridgeWires = QStringLiteral("1-4");
    d.brick(b1)->run.stack = {QStringLiteral("wires"), QStringLiteral("heatshrink")};
    d.brick(b1)->run.transitions = RunEnds::Finish;
    syncBridgeWires(d);
    BrickBuild b = buildBrickHarness(d, nullptr);
    int w1 = 0, w2 = 0, trunkWires = 0;
    size_t trunkAt = 0, lastBranchAt = 0;
    QString b1Body, b1Sides;
    for (size_t k = 0; k < b.entities.size(); ++k) {
        const Entity& e = b.entities[k];
        if (e.type != EntityType::TwistedPair) continue;
        const QString id = e.meta.value(QStringLiteral("brickId")).toString();
        if (id == b1) {
            w1 = e.meta.value(QStringLiteral("twistWires")).toInt();
            b1Body = e.meta.value(QStringLiteral("twistBody")).toString();
            b1Sides = e.meta.value(QStringLiteral("twistBodyTransitionSides")).toString();
            lastBranchAt = std::max(lastBranchAt, k);
        }
        if (id == b2) {
            w2 = e.meta.value(QStringLiteral("twistWires")).toInt();
            lastBranchAt = std::max(lastBranchAt, k);
        }
        if (id == c) {
            trunkWires = e.meta.value(QStringLiteral("twistWires")).toInt();
            trunkAt = k;
        }
    }
    std::printf("  branch: %d and %d wires out of the cable's %d; the first under %s, transition at its %s\n", w1, w2,
                trunkWires, qPrintable(b1Body), qPrintable(b1Sides));
    check(w1 == 4 && w2 == 2 && trunkWires == 6, "branch: each takes its wires from the cable - the ones picked, the rest an even share");
    check(!b1Body.isEmpty() && b1Sides == QLatin1String("finish"), "branch: its own stack and its own transition at its part");
    check(trunkAt > lastBranchAt, "branch: the cable's body drawn over the wires it divides into");
    // INSIDE THE BACKSHELL: the transition counted, not drawn.
    d.brick(b1)->run.transitionInside = true;
    b = buildBrickHarness(d, nullptr);
    QString sides;
    for (const Entity& e : b.entities)
        if (e.type == EntityType::TwistedPair && e.meta.value(QStringLiteral("brickId")).toString() == b1)
            sides = e.meta.value(QStringLiteral("twistBodyTransitionSides")).toString();
    check(b.transitionCount.value(b1) == 1 && (sides.isEmpty() || sides == QLatin1String("none")),
          "inside: a transition inside the backshell is on the bill, not drawn");
    // A BRIDGE WIRE ON A CABLE'S END WITH NOTHING ELSE THERE: out of the cable's body at its end.
    {
        BrickDesign e;
        const QString x = e.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString cab = e.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString br = e.addBrick(BrickType::Run, PartKind::Wire, 2, 1);
        const QString fe = e.addBrick(BrickType::End, PartKind::Ferrule, 3, 1);
        e.connect(x, cab);
        e.brick(cab)->run.wires = 4;
        makeBridgeWire(*e.brick(br));
        e.connect(cab, br, nullptr, 1, 0);
        e.connect(br, fe);
        syncBridgeWires(e);
        const BrickBuild bb = buildBrickHarness(e, nullptr);
        Vec2 cableEnd, bridgeStart;
        bool haveCable = false, haveBridge = false;
        for (const Entity& en : bb.entities) {
            const QString id = en.meta.value(QStringLiteral("brickId")).toString();
            if (id == cab && en.type == EntityType::TwistedPair) {
                cableEnd = en.points.back();
                haveCable = true;
            }
            if (id == br && en.type == EntityType::Polyline && en.meta.value(QStringLiteral("double")).toBool()) {
                bridgeStart = en.points.front();
                haveBridge = true;
            }
        }
        std::printf("  bridge on a bare end: cable ends %.1f,%.1f, bridge starts %.1f,%.1f\n", cableEnd.x, cableEnd.y,
                    bridgeStart.x, bridgeStart.y);
        check(haveCable && haveBridge && distance(cableEnd, bridgeStart) < 15.0,
              "bridge: alone on a cable's end, it comes out of the cable's body there");
    }
    // MATCH LIT WHEN MATCHED; the part's dimensions row gone; the bill one line a row.
    {
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        BrickWizardDialog dlg(&ed);
        BrickDesign& w = dlg.design();
        const QString x = w.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString f = w.addBrick(BrickType::Fanout, PartKind::Wire, 1, 0);
        const QString fe = w.addBrick(BrickType::End, PartKind::Ferrule, 2, 0);
        w.connect(x, f);
        w.connect(f, fe);
        dlg.selectBrick(f);
        check(dlg.scalePartToMatch(fe, x) && w.brick(fe)->matchedTo == x, "match: the part remembers it was matched");
        dlg.selectBrick(fe);
        auto* match = dlg.findChild<QToolButton*>(QStringLiteral("brickPartScaleMatch"));
        check(match && match->styleSheet().contains(QStringLiteral("#2b6cb0")), "match: lit blue while matched");
        auto* bom = dlg.findChild<QTableWidget*>(QStringLiteral("brickBom"));
        check(bom && !bom->wordWrap(), "bom: one line a row");
    }
}

// ROUND TWENTY-SIX: a JOINT tied to a run - its splices drawn where it is made, as long as the part, staggered for a
// step joint; the stretch of cable it takes for the preview only; its splice parts and wires out on the bill; the
// bundle at the joint against the cover over it; kept with the harness; and the run itself not changed by it.
void testRoundTwentySix() {
    const QString libFile = QDir(QDir::tempPath()).filePath(QStringLiteral("edraw_joint.plb"));
    QFile::remove(libFile);
    {
        PartsLibrary lib;
        auto part = [](const char* id, PartKind k, std::initializer_list<std::pair<const char*, const char*>> ps) {
            LibraryPart p;
            p.id = p.mfrPn = QString::fromLatin1(id);
            p.kind = k;
            for (const auto& [key, v] : ps) p.params[QString::fromLatin1(key)] = QString::fromLatin1(v);
            return p;
        };
        lib.parts = {part("SP-BUTT", PartKind::Splice, {{"length", "12"}, {"od", "4"}, {"type", "Crimp"}}),
                     part("JW-WIRE", PartKind::Wire, {{"od", "1.5"}}),
                     part("JW-SHRINK", PartKind::Heatshrink, {{"entryMin", "2"}, {"entryMax", "5"}, {"wall", "0.5"}})};
        lib.save(libFile);
    }
    PartsCatalog cat;
    cat.openRemembered();
    cat.connectLibrary(libFile);
    cat.remember();
    {
        BrickDesign d;
        d.catalog = &cat;
        const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString y = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        const QString j = d.addBrick(BrickType::Joint, PartKind::Splice, 1, 1);
        d.connect(x, c);
        d.connect(c, y);
        d.brick(c)->run.wires = 4;
        d.brick(c)->run.lengthMm = 200;
        d.brick(c)->run.stack = {QStringLiteral("wires")};   // bare wires: a joint in them is seen
        const BrickBuild before = buildBrickHarness(d, &cat);
        QString why;
        check(d.connect(j, c, &why), "joint: tied to a run");
        check(!d.connect(j, x, &why), "joint: not to a part");
        check(d.sideOf(c, 0).size() == 1 && d.sideOf(c, 1).size() == 1, "joint: not at either end of the run - in it");
        BrickJoint& jt = d.brick(j)->joint;
        jt.parts = {QStringLiteral("SP-BUTT")};
        jt.inWires = QStringLiteral("1,2");
        jt.outCount = 1;
        jt.atMm = 60.0;
        BrickBuild b = buildBrickHarness(d, &cat);
        // The run is the same run, joint or no joint.
        double runLenBefore = 0.0, runLenAfter = 0.0;
        for (const Entity& e : before.entities)
            if (e.type == EntityType::TwistedPair) runLenBefore = e.points.back().x - e.points.front().x;
        for (const Entity& e : b.entities)
            if (e.type == EntityType::TwistedPair) runLenAfter = e.points.back().x - e.points.front().x;
        check(std::abs(runLenBefore - runLenAfter) < 1e-6, "joint: the run is laid out as it was");
        auto splices = [&b, &j](double* firstX, double* boxLen) {
            int n = 0;
            for (const Entity& e : b.entities)
                if (e.meta.value(QStringLiteral("brickId")).toString() == j && e.type == EntityType::Polyline &&
                    e.meta.value(QStringLiteral("mechRole")).toString() == QLatin1String("splice")) {
                    if (n == 0 && firstX) {
                        double lo = 1e300, hi = -1e300;
                        for (const Vec2& v : e.points) {
                            lo = std::min(lo, v.x);
                            hi = std::max(hi, v.x);
                        }
                        *firstX = lo;
                        if (boxLen) *boxLen = hi - lo;
                    }
                    ++n;
                }
            return n;
        };
        double x0 = 0.0, len = 0.0;
        const int one = splices(&x0, &len);
        double runStart = 0.0;
        for (const Entity& e : b.entities)
            if (e.type == EntityType::TwistedPair) runStart = e.points.front().x;
        std::printf("  joint: %d splice, %.1f long, starting %.1f along the run\n", one, len, x0 - runStart);
        check(one == 1 && std::abs(len - 12.0) < 0.5, "joint: one splice, as long as its part says");
        check(std::abs((x0 - runStart) - 60.0) < 1.0, "joint: made where it is said, measured from the run's start");
        bool spanHidden = false;
        for (const Entity& e : b.entities)
            if (e.meta.value(QStringLiteral("jointSpan")).toString() == j) spanHidden = e.meta.value(QStringLiteral("previewOnly")).toBool();
        check(spanHidden, "joint: the stretch of cable it takes is for the preview only");
        // A STEP JOINT of two wires in and two out: two splices, staggered.
        jt.outCount = 2;
        jt.stepped = true;
        jt.stepMm = 20.0;
        b = buildBrickHarness(d, &cat);
        check(splices(nullptr, nullptr) == 2 && jointSpliceCount(d, *d.brick(j)) == 2, "joint: a step joint staggers its splices");
        check(BrickDesign::fromJson(d.toJson()).brick(j)->joint.stepped &&
                  BrickDesign::fromJson(d.toJson()).brick(j)->joint.parts == QStringList{QStringLiteral("SP-BUTT")},
              "joint: kept with the harness");
    }
    // THE WIZARD: its mark - the bundle at the joint against the heatshrink over the run - and its parts on the bill.
    {
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        BrickWizardDialog dlg(&ed);
        BrickDesign& w = dlg.design();
        const QString x = w.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString c = w.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString y = w.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        const QString j = w.addBrick(BrickType::Joint, PartKind::Splice, 1, 1);
        w.connect(x, c);
        w.connect(c, y);
        w.connect(j, c);
        BrickRunSpec& r = w.brick(c)->run;
        r.wires = 4;
        r.stack = {QStringLiteral("wires"), QStringLiteral("heatshrink")};
        r.stackParts.insert(QStringLiteral("wires"), QStringLiteral("JW-WIRE"));
        r.stackParts.insert(QStringLiteral("heatshrink"), QStringLiteral("JW-SHRINK"));
        BrickJoint& jt = w.brick(j)->joint;
        jt.parts = {QStringLiteral("SP-BUTT")};
        jt.inWires = QStringLiteral("1,2");
        jt.outWirePart = QStringLiteral("JW-WIRE");
        dlg.selectBrick(j);
        std::printf("  joint mark: %d (%s)\n", dlg.canvas()->markOf(j).compat, qPrintable(dlg.canvas()->markOf(j).why));
        check(dlg.canvas()->markOf(j).compat == 2, "joint: the bundle at the joint too big for the heatshrink over it - red");
        BomTable t;
        t.fromJson(doc.bom());
        bool splice = false, outWire = false;
        for (const BomRow& row : t.rows) {
            splice = splice || row.partId == QLatin1String("SP-BUTT");
            outWire = outWire || row.partId == QLatin1String("JW-WIRE");
        }
        check(splice && outWire, "joint: its splice and the wire out of it on the bill");
        auto* page = dlg.findChild<QWidget*>(QStringLiteral("brickJointSections"));
        auto* span = dlg.findChild<QPushButton*>(QStringLiteral("brickJointShowSpan"));
        check(page && span, "joint: a page of its own, with the button that shows the length it takes");
        if (span) span->setChecked(true);
        bool lit = false;
        for (const Entity& e : dlg.preview()->entities()) lit = lit || e.meta.value(QStringLiteral("jointSpan")).toString() == j;
        check(lit && dlg.jointShown() == j, "joint: its stretch of cable lit on the preview when asked");
        bool placed = false;
        for (const Entity& e : dlg.drawing()) placed = placed || e.meta.value(QStringLiteral("previewOnly")).toBool();
        check(!placed, "joint: and never on the drawing placed");
    }
    cat.disconnectLibrary(libFile);
    cat.remember();
    QFile::remove(libFile);
}

// ROUND TWENTY-SEVEN: a joint is a small thing tied to its run, moving with it, and not seen inside a cable's body; a
// transition stretched over its end part; the run brick called a harness run.
void testRoundTwentySeven() {
    check(brickTypeName(BrickType::Run) == QLatin1String("Harness run"), "name: the run brick is a harness run");
    // NOT SEEN INSIDE THE CABLE.
    {
        BrickDesign d;
        const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString y = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        const QString j = d.addBrick(BrickType::Joint, PartKind::Splice, 1, 1);
        d.connect(x, c);
        d.connect(c, y);
        d.connect(j, c);
        d.brick(c)->run.wires = 4;
        d.brick(c)->run.lengthMm = 200;
        d.brick(c)->run.stack = {QStringLiteral("wires"), QStringLiteral("cable")};
        d.brick(j)->joint.atMm = 80.0;
        auto drawnSplices = [&d, &j] {
            int n = 0;
            for (const Entity& e : buildBrickHarness(d, nullptr).entities)
                if (e.meta.value(QStringLiteral("brickId")).toString() == j &&
                    e.meta.value(QStringLiteral("mechRole")).toString() == QLatin1String("splice") && e.type == EntityType::Polyline)
                    ++n;
            return n;
        };
        check(drawnSplices() == 0, "joint: inside the cable's body it is not drawn");
        d.brick(j)->joint.atMm = 2.0;   // out in the fan, where the wires are seen
        check(drawnSplices() == 1, "joint: in the fan, where the wires are on show, it is drawn");
    }
    // A JOINT MOVES WITH ITS RUN on the pane.
    {
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        BrickWizardDialog dlg(&ed);
        BrickDesign& w = dlg.design();
        const QString c = w.addBrick(BrickType::Run, PartKind::Cable, 2, 2);
        const QString j = w.addBrick(BrickType::Joint, PartKind::Splice, 2, 1);
        w.connect(j, c);
        const double jc = w.brick(j)->col;
        dlg.canvas()->setSelection({c});
        dlg.canvas()->moveSelection(1.0, 0.0);
        check(std::abs(w.brick(j)->col - (jc + 1.0)) < 1e-9, "joint: tied to its run, it moves with it");
    }
    // A TRANSITION STRETCHED OVER ITS END PART: an end with no fan.
    {
        BrickDesign d;
        const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString y = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        d.connect(x, c);
        d.connect(c, y);
        BrickRunSpec& r = d.brick(c)->run;
        r.fanStartMm = 0.0;   // no fan at the start: the cable goes into its part
        r.transitions = RunEnds::Start;
        // The piece over the part: how far into it, and how far across it goes - and the part's own height.
        auto overPart = [&d, &x, &c](double& along, double& across, double& partAcross, bool& masked) {
            along = across = partAcross = 0.0;
            masked = false;
            Bounds pb;
            // Into the part from its face - where the run starts.
            double faceX = 0.0;
            const BrickBuild built = buildBrickHarness(d, nullptr);
            for (const Entity& e : built.entities)
                if (e.type == EntityType::TwistedPair && e.meta.value(QStringLiteral("brickId")).toString() == c)
                    faceX = e.points.front().x;
            for (const Entity& e : built.entities) {
                const QString role = e.meta.value(QStringLiteral("mechRole")).toString();
                if (e.meta.value(QStringLiteral("brickId")).toString() == x && role != QLatin1String("designator") &&
                    e.type != EntityType::Text)
                    pb.expand(e.bounds());
                if (role != QLatin1String("transitionOverPart")) continue;
                if (e.type == EntityType::Wipeout) masked = true;
                if (e.type != EntityType::Polyline) continue;
                const Bounds bb = e.bounds();
                along = faceX - bb.min.x;   // it starts back over the transition: how far it goes into the part
                across = bb.max.y - bb.min.y;
            }
            if (pb.valid) partAcross = pb.max.y - pb.min.y;
        };
        double along, across, partAcross;
        bool masked;
        overPart(along, across, partAcross, masked);
        check(along == 0.0 && !masked, "stretch: nothing over the part until asked");
        r.transitionStretch = true;
        const BrickBuild b = buildBrickHarness(d, nullptr);
        overPart(along, across, partAcross, masked);
        std::printf("  stretch: over the part %.1f along, %.1f across (the part %.1f), auto %.1f\n", along, across, partAcross,
                    b.actual.value(c).transitionStretch);
        check(along > 0.5 && std::abs(along - b.actual.value(c).transitionStretch) < 1e-6,
              "stretch: the transition carried on over the end part - a tenth of its length on Auto");
        check(masked && across >= partAcross - 1e-6, "stretch: a wipeout over the whole of the part across");
        r.transitionStretchMm = 6.0;
        overPart(along, across, partAcross, masked);
        check(std::abs(along - 6.0) < 1e-6, "stretch: as far as said");
    }
}

// ROUND TWENTY-EIGHT: a bridge wire on a cable's end where it divides into branches; each transition's length shown on
// Auto; which transition is inside the backshell - and its length on the bill; Clear.
void testRoundTwentyEight() {
    {
        BrickDesign d;
        const QString sa = d.addBrick(BrickType::End, PartKind::Connector, 0, 1);
        const QString a2 = d.addBrick(BrickType::End, PartKind::Connector, 0, 3);
        const QString sb = d.addBrick(BrickType::End, PartKind::Connector, 4, 2);
        const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 3, 2);
        const QString b1 = d.addBrick(BrickType::Run, PartKind::Wire, 1, 1);
        const QString b2 = d.addBrick(BrickType::Run, PartKind::Wire, 1, 3);
        const QString bw = d.addBrick(BrickType::Run, PartKind::Wire, 2, 0);
        const QString f = d.addBrick(BrickType::End, PartKind::Ferrule, 1, 0);
        makeBranch(*d.brick(b1));
        makeBranch(*d.brick(b2));
        makeBridgeWire(*d.brick(bw));
        d.brick(bw)->run.bridgeWires = QStringLiteral("1,2");
        d.connect(c, sb, nullptr, 1, -1);
        d.brick(c)->run.wires = 6;
        d.connect(b1, c, nullptr, 1, 0);
        d.connect(b2, c, nullptr, 1, 0);
        d.connect(sa, b1, nullptr, -1, 0);
        d.connect(a2, b2, nullptr, -1, 0);
        QString why;
        check(d.connect(bw, c, &why, 1, 0), "bridge: joined to the cable's end where it divides");
        d.connect(f, bw, nullptr, -1, 0);
        syncBridgeWires(d);
        std::vector<Vec2> bridgeRoute;
        std::vector<std::vector<Vec2>> cable;
        for (const Entity& e : buildBrickHarness(d, nullptr).entities) {
            if (e.type != EntityType::TwistedPair) continue;
            const QString id = e.meta.value(QStringLiteral("brickId")).toString();
            if (id == bw) bridgeRoute = e.points;
            else if (id == c) cable.push_back(e.points);
        }
        double off = 1e300;
        if (!bridgeRoute.empty())
            for (const auto& rt : cable)
                for (size_t k = 0; k + 1 < rt.size(); ++k)
                    off = std::min({off, closestPointOnSegment(bridgeRoute.front(), rt[k], rt[k + 1]).distance,
                                    closestPointOnSegment(bridgeRoute.back(), rt[k], rt[k + 1]).distance});
        std::printf("  bridge: off the cable where it divides by %.2f\n", off);
        check(!bridgeRoute.empty() && off < 6.0, "bridge: comes out of the cable where it divides, not left off by itself");
    }
    {
        BrickDesign d;
        const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString y = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        d.connect(x, c);
        d.connect(c, y);
        BrickRunSpec& r = d.brick(c)->run;
        r.transitions = RunEnds::Both;
        r.lengthMm = 300.0;
        BrickBuild b = buildBrickHarness(d, nullptr);
        const BrickActual a = b.actual.value(c);
        std::printf("  transition lengths on Auto: %.1f, %.1f; all told %.1f\n", a.transitionLenStart, a.transitionLenFinish,
                    b.transitionMm.value(c));
        check(a.transitionLenStart > 1.0 && a.transitionLenFinish > 1.0, "transition: each length on Auto, as drawn");
        check(std::abs(b.transitionMm.value(c) - (a.transitionLenStart + a.transitionLenFinish)) < 1e-6,
              "transition: on the bill as long as both");
        r.transitionLenStartMm = 15.0;
        b = buildBrickHarness(d, nullptr);
        check(std::abs(b.actual.value(c).transitionLenStart - 15.0) < 1e-6, "transition: a length said is the length");
        // INSIDE THE BACKSHELL, the start's only: the finish still drawn, both on the bill as long as they are.
        auto sides = [&d, &c] {
            for (const Entity& e : buildBrickHarness(d, nullptr).entities)
                if (e.type == EntityType::TwistedPair && e.meta.value(QStringLiteral("brickId")).toString() == c)
                    return e.meta.value(QStringLiteral("twistBodyTransitionSides")).toString();
            return QString();
        };
        check(sides() == QStringLiteral("both"), "inside: both drawn before");
        r.transitionInside = true;
        r.transitionInsideEnds = RunEnds::Start;
        b = buildBrickHarness(d, nullptr);
        check(sides() == QStringLiteral("finish") && b.transitionCount.value(c) == 2 &&
                  std::abs(b.transitionMm.value(c) - (15.0 + b.actual.value(c).transitionLenFinish)) < 1e-6,
              "inside: the start's not drawn, both counted and their lengths on the bill");
        r.transitionInsideEnds = RunEnds::Both;
        check(sides() == QStringLiteral("none"), "inside: both, neither drawn");
        const BrickDesign back = BrickDesign::fromJson(d.toJson());
        check(back.brick(c)->run.transitionInsideEnds == RunEnds::Both, "inside: which, kept");
    }
    {
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        BrickWizardDialog dlg(&ed);
        BrickDesign& w = dlg.design();
        w.addBrick(BrickType::Run, PartKind::Cable, 2, 2);
        w.addBrick(BrickType::End, PartKind::Connector, 1, 2);
        auto* clr = dlg.findChild<QPushButton*>(QStringLiteral("brickClear"));
        check(clr != nullptr, "clear: a Clear button");
        if (clr) clr->click();
        check(dlg.design().bricks.empty(), "clear: every brick gone");
        check(dlg.findChild<QToolButton*>(QStringLiteral("brickTransLenStartAuto")) &&
                  dlg.findChild<QToolButton*>(QStringLiteral("brickTransLenFinishAuto")) &&
                  dlg.findChild<QComboBox*>(QStringLiteral("brickTransInsideEnds")),
              "transition: A on each length, and which one is inside");
    }
}

// ROUND TWENTY-NINE: a bridge on a branch brick takes its wires from that branch; the cable its own length to where it
// divides, each branch its own from there (none at all at nought); the wires out of a transition where it divides side
// by side across it; a part dragged moves alone; layers over the transition; the palette's joints.
void testRoundTwentyNine() {
    auto harness = [](BrickDesign& d, QString& sa, QString& a2, QString& sb, QString& c, QString& b1, QString& b2) {
        sa = d.addBrick(BrickType::End, PartKind::Connector, 0, 1);
        a2 = d.addBrick(BrickType::End, PartKind::Connector, 0, 3);
        sb = d.addBrick(BrickType::End, PartKind::Connector, 4, 2);
        c = d.addBrick(BrickType::Run, PartKind::Cable, 3, 2);
        b1 = d.addBrick(BrickType::Run, PartKind::Wire, 1, 1);
        b2 = d.addBrick(BrickType::Run, PartKind::Wire, 1, 3);
        makeBranch(*d.brick(b1));
        makeBranch(*d.brick(b2));
        d.connect(c, sb, nullptr, 1, -1);
        d.brick(c)->run.wires = 6;
        d.brick(c)->run.lengthMm = 150.0;
        d.connect(b1, c, nullptr, 1, 0);
        d.connect(b2, c, nullptr, 1, 0);
        d.connect(sa, b1, nullptr, -1, 0);
        d.connect(a2, b2, nullptr, -1, 0);
        d.brick(b1)->run.bridgeWires = QStringLiteral("1-4");
        d.brick(b1)->run.wires = 4;
        d.brick(b1)->run.lengthMm = 100.0;
        d.brick(b2)->run.lengthMm = 100.0;
    };
    auto runsOf = [](const BrickBuild& b, const QString& id) {
        std::vector<std::vector<Vec2>> out;
        for (const Entity& e : b.entities)
            if (e.type == EntityType::TwistedPair && e.meta.value(QStringLiteral("brickId")).toString() == id)
                out.push_back(e.points);
        return out;
    };
    // A BRIDGE ON A BRANCH BRICK: out of that branch's fan, its two wires off the branch's four.
    {
        BrickDesign d;
        QString sa, a2, sb, c, b1, b2;
        harness(d, sa, a2, sb, c, b1, b2);
        const QString bw = d.addBrick(BrickType::Run, PartKind::Wire, 2, 0);
        const QString f = d.addBrick(BrickType::End, PartKind::Ferrule, 1, 0);
        makeBridgeWire(*d.brick(bw));
        d.brick(bw)->run.bridgeWires = QStringLiteral("1,2");
        QString why;
        check(d.connect(bw, b1, &why, 1, 0), "bridge on a branch: joined to the branch at its part");
        d.connect(f, bw, nullptr, -1, 0);
        syncBridgeWires(d);
        const BrickBuild b = buildBrickHarness(d, nullptr);
        const auto bridge = runsOf(b, bw);
        const auto branch = runsOf(b, bw).empty() ? std::vector<std::vector<Vec2>>{} : runsOf(b, b1);
        double off = 1e300;
        int branchWires = 0;
        for (const Entity& e : b.entities)
            if (e.type == EntityType::TwistedPair && e.meta.value(QStringLiteral("brickId")).toString() == b1)
                branchWires = e.meta.value(QStringLiteral("twistWires")).toInt();
        if (!bridge.empty())
            for (const auto& rt : branch)
                for (size_t k = 0; k + 1 < rt.size(); ++k)
                    off = std::min({off, closestPointOnSegment(bridge.front().front(), rt[k], rt[k + 1]).distance,
                                    closestPointOnSegment(bridge.front().back(), rt[k], rt[k + 1]).distance});
        std::printf("  bridge on a branch: %.2f off it, the branch left %d wire(s)\n", off, branchWires);
        check(!bridge.empty() && off < 6.0, "bridge on a branch: comes out of the branch, not left off by itself");
        check(branchWires == 2, "bridge on a branch: its wires come off the branch's");
    }
    // THE CABLE ITS OWN LENGTH to where it divides; a branch of nought not drawn; the wires side by side out of the
    // transition there; layers over the transitions.
    {
        BrickDesign d;
        QString sa, a2, sb, c, b1, b2;
        harness(d, sa, a2, sb, c, b1, b2);
        d.brick(c)->run.transitions = RunEnds::Both;
        BrickBuild b = buildBrickHarness(d, nullptr);
        const auto trunk = runsOf(b, c);
        double trunkLen = 0.0;
        for (const auto& rt : trunk) {
            double len = 0.0;
            for (size_t k = 1; k < rt.size(); ++k) len += distance(rt[k - 1], rt[k]);
            trunkLen = std::max(trunkLen, len);
        }
        std::printf("  divides: the cable drawn %.1f to where it divides (said 150)\n", trunkLen);
        check(std::abs(trunkLen - 150.0) < 1.0, "divides: the cable its own length to where it divides");
        const auto r1 = runsOf(b, b1), r2 = runsOf(b, b2);
        check(!r1.empty() && !r2.empty() && distance(r1.front().front(), r2.front().front()) > 0.5,
              "divides: out of the transition side by side, not from one point");
        d.brick(c)->run.transitionLayers = {QStringLiteral("LAYER-A"), QStringLiteral("LAYER-B")};
        b = buildBrickHarness(d, nullptr);
        int layers = 0;
        for (const Entity& e : b.entities)
            if (e.type == EntityType::Polyline && e.meta.value(QStringLiteral("mechRole")).toString() == QLatin1String("transitionLayer"))
                ++layers;
        std::printf("  layers: %d drawn, %d of the first on the bill (%.1f mm)\n", layers,
                    b.layerCount.value(c + QStringLiteral("|LAYER-A")), b.layerMm.value(c + QStringLiteral("|LAYER-A")));
        check(layers >= 2 && b.layerCount.value(c + QStringLiteral("|LAYER-A")) >= 1 &&
                  b.layerCount.value(c + QStringLiteral("|LAYER-B")) == b.layerCount.value(c + QStringLiteral("|LAYER-A")),
              "layers: one over another over each transition, each on the bill");
        const BrickDesign back = BrickDesign::fromJson(d.toJson());
        check(back.brick(c)->run.transitionLayers.size() == 2, "layers: kept");
        d.brick(b2)->run.lengthMm = 0.0;
        b = buildBrickHarness(d, nullptr);
        check(runsOf(b, b2).empty(), "divides: a branch of no length is not drawn - its part on the cable");
    }
    // A PART DRAGGED MOVES ALONE: the branch's own length, or the cable's, and nothing else's; the rest held.
    {
        BrickDesign d;
        QString sa, a2, sb, c, b1, b2;
        harness(d, sa, a2, sb, c, b1, b2);
        BrickBuild b = buildBrickHarness(d, nullptr);
        BrickDesign moved = d;
        dragPartOnDrawing(moved, b, sa, -20.0, 0.0, false);
        std::printf("  drag: branch %.0f -> %.0f, cable %.0f -> %.0f\n", d.brick(b1)->run.lengthMm, moved.brick(b1)->run.lengthMm,
                    d.brick(c)->run.lengthMm, moved.brick(c)->run.lengthMm);
        check(std::abs(moved.brick(b1)->run.lengthMm - 120.0) < 1e-6 && moved.brick(c)->run.lengthMm == 150.0 &&
                  moved.brick(b2)->run.lengthMm == 100.0,
              "drag: a branch's part changes the branch's length alone");
        moved = d;
        dragPartOnDrawing(moved, b, sb, 30.0, 0.0, false);
        check(std::abs(moved.brick(c)->run.lengthMm - 180.0) < 1e-6 && moved.brick(b1)->run.lengthMm == 100.0,
              "drag: the cable's part changes the cable's length alone");
        moved.holdPart = a2;
        moved.holdX = b.partX.value(a2);
        const BrickBuild after = buildBrickHarness(moved, nullptr);
        check(std::abs(after.partX.value(a2) - b.partX.value(a2)) < 1e-6 && std::abs(after.partX.value(sa) - b.partX.value(sa)) < 1e-6 &&
                  std::abs(std::abs(after.partX.value(sb) - b.partX.value(sb)) - 30.0) < 1.0,
              "drag: held, only the part dragged moves");
    }
    // A BRIDGE OUT OF WHERE THE CABLE DIVIDES: its part moves with the bridge's length.
    {
        BrickDesign d;
        QString sa, a2, sb, c, b1, b2;
        harness(d, sa, a2, sb, c, b1, b2);
        const QString bw = d.addBrick(BrickType::Run, PartKind::Wire, 2, 0);
        const QString f = d.addBrick(BrickType::End, PartKind::Ferrule, 1, 0);
        makeBridgeWire(*d.brick(bw));
        d.brick(bw)->run.bridgeWires = QStringLiteral("5,6");
        d.connect(bw, c, nullptr, 1, 0);
        d.connect(f, bw, nullptr, -1, 0);
        syncBridgeWires(d);
        d.holdPart = sb;
        const double x0 = buildBrickHarness(d, nullptr).partX.value(f);
        d.brick(bw)->run.lengthMm += 40.0;
        const double x1 = buildBrickHarness(d, nullptr).partX.value(f);
        std::printf("  bridge at the divide: its part %.1f -> %.1f\n", x0, x1);
        check(std::abs(x1 - x0) > 10.0, "bridge at the divide: its part moves with the bridge's length");
    }
    // THE JOINTS ON THE PALETTE: a backshell, no heatshrink.
    {
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        BrickWizardDialog dlg(&ed);
        auto* palette = dlg.findChild<BrickPalette*>(QStringLiteral("brickPalette"));
        bool backshell = false, heatshrink = false;
        for (int i = 0; palette && i < palette->count(); ++i) {
            const QString what = palette->item(i)->data(Qt::UserRole).toString();
            if (what.endsWith(QLatin1Char('|') + partKindKey(PartKind::Backshell))) backshell = true;
            if (what.endsWith(QLatin1Char('|') + partKindKey(PartKind::Heatshrink))) heatshrink = true;
        }
        check(backshell && !heatshrink, "palette: a backshell among the joints, no heatshrink");
        check(dlg.findChild<QTableWidget*>(QStringLiteral("brickTransLayers")) &&
                  dlg.findChild<QToolButton*>(QStringLiteral("brickTransLayerAdd")),
              "layers: a list of them on the transition, and + to add one");
    }
}

// ROUND THIRTY: the transition's Auto width the body's alone; each end its own (width, part, stretch); the piece over the
// part rising from halfway along the transition; a branch straight out a little before it bends; the whole length to
// each part a cable divides into; one Electronic component on the palette; the dimension wizard out of the way while
// two bricks are clicked.
void testRoundThirty() {
    auto twist = [](const BrickBuild& b, const QString& id) {
        for (const Entity& e : b.entities)
            if (e.type == EntityType::TwistedPair && e.meta.value(QStringLiteral("brickId")).toString() == id) return e;
        return Entity{};
    };
    {
        BrickDesign d;
        const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString y = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        d.connect(x, c);
        d.connect(c, y);
        BrickRunSpec& r = d.brick(c)->run;
        r.transitions = RunEnds::Both;
        r.wires = 8;
        r.lengthMm = 300.0;
        BrickBuild b = buildBrickHarness(d, nullptr);
        const double body = b.actual.value(c).bodyWidth;
        const Entity run = twist(b, c);
        std::printf("  transition: body %.2f, Auto width %.2f / %.2f\n", body,
                    run.meta.value(QStringLiteral("twistBootWidth")).toDouble(),
                    run.meta.value(QStringLiteral("twistBootWidthFinish")).toDouble());
        check(std::abs(run.meta.value(QStringLiteral("twistBootWidth")).toDouble() - body * 1.25) < 1e-6 &&
                  std::abs(run.meta.value(QStringLiteral("twistBootWidthFinish")).toDouble() - body * 1.25) < 1e-6,
              "transition: on Auto the body's width and a quarter at both ends, not the wires'");
        // EACH END ITS OWN.
        r.transitionPart = QStringLiteral("P-START");
        r.transitionPartFinish = QStringLiteral("P-FINISH");
        r.transitionWidthFinishMm = body + 4.0;
        b = buildBrickHarness(d, nullptr);
        check(b.transitionPartCount.value(c + QStringLiteral("|P-START")) == 2,
              "transition: the same at both ends - the start's part at both");
        r.transitionSame = false;
        b = buildBrickHarness(d, nullptr);
        check(std::abs(twist(b, c).meta.value(QStringLiteral("twistBootWidthFinish")).toDouble() - (body + 4.0)) < 1e-6 &&
                  std::abs(b.actual.value(c).transitionWidthFinish - (body + 4.0)) < 1e-6,
              "transition: each its own - the finish's width");
        check(b.transitionPartCount.value(c + QStringLiteral("|P-START")) == 1 &&
                  b.transitionPartCount.value(c + QStringLiteral("|P-FINISH")) == 1,
              "transition: each its own - each end's part on the bill");
        const BrickDesign back = BrickDesign::fromJson(d.toJson());
        check(!back.brick(c)->run.transitionSame && back.brick(c)->run.transitionPartFinish == QStringLiteral("P-FINISH") &&
                  std::abs(back.brick(c)->run.transitionWidthFinishMm - (body + 4.0)) < 1e-9,
              "transition: each its own, kept");
        // STRETCHED AT THE FINISH ONLY: no fans, the start's stretch off and the finish's on.
        r.fanStartMm = 0.0;
        r.fanFinishMm = 0.0;
        r.transitionStretch = false;
        r.transitionStretchFinish = true;
        b = buildBrickHarness(d, nullptr);
        const Entity cab = twist(b, c);
        double minX = 1e300, maxX = -1e300;
        int pieces = 0;
        for (const Entity& e : b.entities)
            if (e.type == EntityType::Polyline &&
                e.meta.value(QStringLiteral("mechRole")).toString() == QLatin1String("transitionOverPart")) {
                ++pieces;
                const Bounds bb = e.bounds();
                minX = std::min(minX, bb.min.x);
                maxX = std::max(maxX, bb.max.x);
            }
        const double faceB = cab.points.empty() ? 0.0 : cab.points.back().x;
        std::printf("  over the part at the finish: %d piece(s), %.1f..%.1f, the face at %.1f, its transition %.1f\n", pieces,
                    minX, maxX, faceB, b.actual.value(c).transitionLenFinish);
        check(pieces == 1 && minX < faceB - 0.5 * b.actual.value(c).transitionLenFinish + 0.6 && maxX > faceB + 0.5,
              "over the part: at the finish only, rising from halfway along its transition");
    }
    // A BRANCH STRAIGHT OUT A LITTLE, then its own way; THE WHOLE LENGTH to each part the cable divides into.
    {
        BrickDesign d;
        const QString sa = d.addBrick(BrickType::End, PartKind::Connector, 0, 1);
        const QString a2 = d.addBrick(BrickType::End, PartKind::Connector, 0, 3);
        const QString sb = d.addBrick(BrickType::End, PartKind::Connector, 4, 2);
        const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 3, 2);
        const QString b1 = d.addBrick(BrickType::Run, PartKind::Wire, 1, 1);
        const QString b2 = d.addBrick(BrickType::Run, PartKind::Wire, 1, 3);
        makeBranch(*d.brick(b1));
        makeBranch(*d.brick(b2));
        d.connect(c, sb, nullptr, 1, -1);
        d.brick(c)->run.wires = 6;
        d.brick(c)->run.lengthMm = 150.0;
        d.brick(c)->run.dimTotal = true;
        d.brick(b1)->run.lengthMm = 100.0;
        d.brick(b2)->run.lengthMm = 100.0;
        d.connect(b1, c, nullptr, 1, 0);
        d.connect(b2, c, nullptr, 1, 0);
        d.connect(sa, b1, nullptr, -1, 0);
        d.connect(a2, b2, nullptr, -1, 0);
        const BrickBuild b = buildBrickHarness(d, nullptr);
        const Entity br1 = twist(b, b1);
        const bool straightOut = br1.points.size() >= 3 && std::abs(br1.points[1].y - br1.points[0].y) < 1e-6 &&
                                 std::abs(br1.points[1].x - br1.points[0].x) > 2.0;
        check(straightOut, "branch: straight out of the cable a little before it bends");
        int totals = 0;
        double measured = 0.0;
        for (const Entity& e : b.entities)
            if (e.type == EntityType::Dimension && e.meta.value(QStringLiteral("brickDim")).toString() == QLatin1String("total")) {
                ++totals;
                measured = e.meta.value(QStringLiteral("brickMeasured")).toDouble();
            }
        std::printf("  whole length where the cable divides: %d dimension(s), %.1f\n", totals, measured);
        check(totals == 2 && measured >= 250.0 - 1e-6, "dims: the whole length from the cable's part to each part it divides into");
    }
    {
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        BrickWizardDialog dlg(&ed);
        auto* palette = dlg.findChild<BrickPalette*>(QStringLiteral("brickPalette"));
        int electronic = 0, capacitor = 0;
        for (int i = 0; palette && i < palette->count(); ++i) {
            if (palette->item(i)->text().trimmed() == QStringLiteral("Electronic component")) ++electronic;
            if (palette->item(i)->data(Qt::UserRole).toString().endsWith(QLatin1Char('|') + partKindKey(PartKind::Capacitor)))
                ++capacitor;
        }
        check(electronic == 1 && capacitor == 0, "palette: one Electronic component for the resistor, capacitor, coil and bead");
        const QString part = dlg.addFromPalette(brickTypeKey(BrickType::End) + QLatin1Char('|') + partKindKey(PartKind::Resistor));
        dlg.selectBrick(part);
        auto* kind = dlg.findChild<QComboBox*>(QStringLiteral("brickComponentKind"));
        check(kind != nullptr, "electronic component: which one it is, on its panel");
        if (kind) {
            const int at = kind->findData(partKindKey(PartKind::Capacitor));
            kind->setCurrentIndex(at);
            emit kind->activated(at);
            check(dlg.design().brick(part) && dlg.design().brick(part)->kind == PartKind::Capacitor,
                  "electronic component: made a capacitor");
        }
        // THE DIMENSION WIZARD put away while two bricks are clicked, back on Esc.
        dlg.show();
        dlg.openDimensionWizard();
        auto* wiz = dlg.findChild<QDialog*>(QStringLiteral("brickDimWizard"));
        auto* add = dlg.findChild<QPushButton*>(QStringLiteral("dimWizAdd"));
        if (add) add->setChecked(true);
        check(wiz && !wiz->isVisible(), "dimension wizard: out of the way while two bricks are clicked");
        QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(&dlg, &esc);
        check(wiz && wiz->isVisible() && add && !add->isChecked(), "dimension wizard: back on Esc, the pick called off");
        if (wiz) wiz->hide();
    }
}

// ROUND THIRTY-ONE: each end's transition framed with all its figures, its layers included; each layer's step from the
// bottom transition's cable-side end; Symmetric about a part - Auto or the one picked.
void testRoundThirtyOne() {
    {
        BrickDesign d;
        const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString y = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        d.connect(x, c);
        d.connect(c, y);
        BrickRunSpec& r = d.brick(c)->run;
        r.transitions = RunEnds::Both;
        r.lengthMm = 300.0;
        r.transitionLayers = {QStringLiteral("L-A")};
        BrickBuild b = buildBrickHarness(d, nullptr);
        const double autoMm = b.layerMm.value(c + QStringLiteral("|L-A"));
        r.transitionLayerSteps = {5.0};
        b = buildBrickHarness(d, nullptr);
        const double both = b.actual.value(c).transitionLenStart + b.actual.value(c).transitionLenFinish;
        std::printf("  layer step: Auto %.1f mm, 5 mm short of the cable side %.1f (the transitions %.1f)\n", autoMm,
                    b.layerMm.value(c + QStringLiteral("|L-A")), both);
        check(std::abs(b.layerMm.value(c + QStringLiteral("|L-A")) - (both - 10.0)) < 1e-6 && autoMm < both - 10.0,
              "layer step: each layer stops its step short of the bottom transition's cable-side end");
        // EACH END ITS OWN LAYERS.
        r.transitionSame = false;
        r.transitionLayersFinish = {QStringLiteral("L-B"), QStringLiteral("L-C")};
        b = buildBrickHarness(d, nullptr);
        check(b.layerCount.value(c + QStringLiteral("|L-A")) == 1 && b.layerCount.value(c + QStringLiteral("|L-B")) == 1 &&
                  b.layerCount.value(c + QStringLiteral("|L-C")) == 1,
              "layers: each end its own, when the two are not the same");
        const BrickDesign back = BrickDesign::fromJson(d.toJson());
        check(back.brick(c)->run.transitionLayersFinish.size() == 2 && back.brick(c)->run.transitionLayerSteps.size() == 1 &&
                  std::abs(back.brick(c)->run.transitionLayerSteps.front() - 5.0) < 1e-9,
              "layers: each end's and their steps kept");
        // INSIDE THE BACKSHELL AT ONE END.
        BrickRunSpec t;
        setTransitionInside(t, false, true);
        check(t.transitionInside && transitionInsideAt(t, true) && !transitionInsideAt(t, false),
              "inside: the finish's alone");
    }
    // FRAMED BY SIDE in the window.
    {
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        BrickWizardDialog dlg(&ed);
        dlg.show();
        BrickDesign& w = dlg.design();
        const QString x = w.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString c = w.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString y = w.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        w.connect(x, c);
        w.connect(c, y);
        w.brick(c)->run.transitions = RunEnds::Both;
        dlg.selectBrick(c);
        auto* start = dlg.findChild<QGroupBox*>(QStringLiteral("brickTransSideStart"));
        auto* finish = dlg.findChild<QGroupBox*>(QStringLiteral("brickTransSideFinish"));
        auto* same = dlg.findChild<QCheckBox*>(QStringLiteral("brickTransSame"));
        check(start && finish && same && start->title() == QStringLiteral("Both sides") && !finish->isVisibleTo(&dlg),
              "frames: one for both sides while they are the same");
        if (same) same->setChecked(false);
        check(start && finish && start->title() == QStringLiteral("Start") && finish->isVisibleTo(&dlg) &&
                  dlg.findChild<QTableWidget*>(QStringLiteral("brickTransLayersFin")) &&
                  dlg.findChild<QCheckBox*>(QStringLiteral("brickTransInsideFin")),
              "frames: unticked, the start's and the finish's - each with its layers and inside the backshell");
        check(dlg.findChild<QComboBox*>(QStringLiteral("brickSymRef")) != nullptr, "symmetric: a list of the part it is about");
        dlg.hide();
    }
    // SYMMETRIC ABOUT A PART.
    {
        BrickDesign d;
        const QString a = d.addBrick(BrickType::End, PartKind::Connector, 0, 1);
        const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 2, 1);
        const QString p = d.addBrick(BrickType::End, PartKind::Connector, 5, 0);
        const QString q = d.addBrick(BrickType::End, PartKind::Connector, 5, 6);
        d.connect(a, c, nullptr, -1, 0);
        d.connect(c, p, nullptr, 1, -1);
        d.connect(c, q, nullptr, 1, -1);
        d.brick(c)->run.lengthMm = 400.0;
        const QString about = symmetricAbout(d, QString(), nullptr);
        BrickBuild b = buildBrickHarness(d, nullptr);
        std::printf("  symmetric: about %s at %.1f, the two at %.1f and %.1f\n", qPrintable(about == a ? QStringLiteral("the one") : about),
                    b.partY.value(a), b.partY.value(p), b.partY.value(q));
        check(about == a && std::abs((b.partY.value(p) + b.partY.value(q)) / 2.0 - b.partY.value(a)) < 1.0 &&
                  std::abs(b.partY.value(p) - b.partY.value(q)) > 10.0,
              "symmetric: Auto, the two either side of the one at the main body's other end");
        symmetricAbout(d, p, nullptr);
        b = buildBrickHarness(d, nullptr);
        check(std::abs(b.partY.value(a) - b.partY.value(p)) < 1.0, "symmetric: about the part picked, the one across on its line");
    }
}

// ROUND THIRTY-TWO: the navigation round on the pane's corner; Shift turns the other way; the branches converge onto the
// transition's entry face where the cable divides.
void testRoundThirtyTwo() {
    {
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        BrickWizardDialog dlg(&ed);
        dlg.resize(1400, 900);
        dlg.show();
        auto* nav = dlg.findChild<QWidget*>(QStringLiteral("brickNav"));
        auto* canvas = dlg.canvas();
        auto* wheel = nav ? nav->findChild<NavWheel*>(QStringLiteral("brickNavWheel")) : nullptr;
        check(nav && wheel && wheel->count() == 8 && nav->findChild<QComboBox*>(QStringLiteral("brickSymRef")) &&
                  nav->parentWidget() == canvas,
              "nav: a wheel of eight on the pane itself - arrows, turns, Symmetric and the tie - its part list under it");
        if (nav && canvas) {
            const QRect r = nav->geometry();
            std::printf("  nav: at %d,%d %dx%d on a pane %dx%d\n", r.x(), r.y(), r.width(), r.height(), canvas->width(),
                        canvas->height());
            check(r.right() <= canvas->width() && r.right() >= canvas->width() - 20 && r.top() < 60,
                  "nav: in the pane's top right corner");
        }
        BrickDesign& w = dlg.design();
        const QString x = w.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        canvas->setSelection({x});
        // ITS SEGMENTS DO WHAT THEY SAY: right moves the brick a step, the tie toggles.
        if (wheel) {
            const double col0 = w.brick(x)->col;
            const QPointF at = wheel->segmentCentre(2);
            check(wheel->segmentAt(at) == 2 && wheel->segmentAt(QRectF(wheel->rect()).center()) == -1,
                  "nav: a segment found where it is drawn, nothing in the middle");
            mouse(wheel, QEvent::MouseButtonPress, at);
            mouse(wheel, QEvent::MouseButtonRelease, at);
            check(std::abs(w.brick(x)->col - (col0 + 1.0)) < 1e-9, "nav: the right segment moves the brick a step right");
            const bool tied = w.tieBrickTurn;
            mouse(wheel, QEvent::MouseButtonPress, wheel->segmentCentre(5));
            mouse(wheel, QEvent::MouseButtonRelease, wheel->segmentCentre(5));
            check(w.tieBrickTurn == !tied, "nav: the tie segment (bottom left) turns the tie over");
            mouse(wheel, QEvent::MouseButtonPress, wheel->segmentCentre(5));
            mouse(wheel, QEvent::MouseButtonRelease, wheel->segmentCentre(5));
        }
        // THE SYMMETRIC LIST names each part by its designator and P/N - or, with neither, its kind.
        if (auto* about = dlg.findChild<QComboBox*>(QStringLiteral("brickSymRef"))) {
            w.brick(x)->name = QStringLiteral("P7");
            const QString bare = dlg.addFromPalette(brickTypeKey(BrickType::End) + QLatin1Char('|') + partKindKey(PartKind::Ferrule));
            bool named = false, kind = false;
            for (int i = 0; i < about->count(); ++i) {
                if (about->itemData(i).toString() == x && about->itemText(i) == QStringLiteral("about: P7")) named = true;
                if (about->itemData(i).toString() == bare &&
                    about->itemText(i) == QStringLiteral("about: ") + partKindName(PartKind::Ferrule))
                    kind = true;
            }
            check(named && kind, "symmetric list: a part by its designator (and P/N), one with neither by its kind");
            canvas->setSelection({x});   // the one dropped from the palette was picked: back to the first
        }
        dlg.rotateSelected(true);
        check(w.brick(x)->rot == 3, "turn: with Shift, a quarter counter-clockwise");
        dlg.rotateSelected(false);
        check(w.brick(x)->rot == 0, "turn: without, clockwise");
        const double was = w.brick(x)->drawTurnDeg;
        QKeyEvent r(QEvent::KeyPress, Qt::Key_R, Qt::ShiftModifier);
        QApplication::sendEvent(canvas, &r);
        std::printf("  turn: Shift+R takes the drawing from %.0f to %.0f\n", was, w.brick(x)->drawTurnDeg);
        check(std::abs(std::remainder(w.brick(x)->drawTurnDeg - was + 45.0, 360.0)) < 1e-9,
              "turn: Shift+R turns the drawing 45 degrees counter-clockwise");
        dlg.hide();
    }
    {
        BrickDesign d;
        const QString sa = d.addBrick(BrickType::End, PartKind::Connector, 0, 1);
        const QString a2 = d.addBrick(BrickType::End, PartKind::Connector, 0, 3);
        const QString sb = d.addBrick(BrickType::End, PartKind::Connector, 4, 2);
        const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 3, 2);
        const QString b1 = d.addBrick(BrickType::Run, PartKind::Wire, 1, 1);
        const QString b2 = d.addBrick(BrickType::Run, PartKind::Wire, 1, 3);
        makeBranch(*d.brick(b1));
        makeBranch(*d.brick(b2));
        d.connect(c, sb, nullptr, 1, -1);
        d.brick(c)->run.wires = 8;
        d.brick(c)->run.lengthMm = 150.0;
        d.brick(c)->run.transitions = RunEnds::Both;
        d.brick(b1)->run.lengthMm = 100.0;
        d.brick(b2)->run.lengthMm = 100.0;
        d.brick(b1)->run.wireWidthMm = 2.0;   // fatter wires than the cable's: packed, wider than its face
        d.brick(b2)->run.wireWidthMm = 2.0;
        d.connect(b1, c, nullptr, 1, 0);
        d.connect(b2, c, nullptr, 1, 0);
        d.connect(sa, b1, nullptr, -1, 0);
        d.connect(a2, b2, nullptr, -1, 0);
        const BrickBuild b = buildBrickHarness(d, nullptr);
        Vec2 split;
        double face = 0.0;
        std::vector<Vec2> starts;
        for (const Entity& e : b.entities) {
            if (e.type != EntityType::TwistedPair) continue;
            const QString id = e.meta.value(QStringLiteral("brickId")).toString();
            if (id == c && e.meta.value(QStringLiteral("brickTrunk")).toBool()) {
                split = e.points.back();
                face = e.meta.value(QStringLiteral("twistBodyFace")).toDouble();
            } else if (id == b1 || id == b2) {
                starts.push_back(e.points.front());
            }
        }
        double widest = 0.0;
        for (const Vec2& p : starts) widest = std::max(widest, std::abs(p.y - split.y));
        std::printf("  converge: the face %.2f across, the branches start up to %.2f off its middle\n", face, widest);
        check(starts.size() == 2 && face > 0.0 && widest <= face / 2.0 + 1e-6,
              "converge: every branch starts on the transition's entry face where the cable divides");
    }
}

// ROUND THIRTY-THREE: the harness DRC - its logical rules and compatibility, each finding under a rule that can be
// turned off and a key that can be bypassed (its own, not the schematic's); the wheel a twentieth bigger.
void testRoundThirtyThree() {
    auto part = [](const char* id, PartKind k, std::initializer_list<std::pair<const char*, const char*>> ps) {
        LibraryPart p;
        p.id = p.mfrPn = QString::fromLatin1(id);
        p.kind = k;
        for (const auto& [key, v] : ps) p.params[QString::fromLatin1(key)] = QString::fromLatin1(v);
        return p;
    };
    PartsLibrary lib;
    lib.parts = {part("DRC-CONN2", PartKind::Connector, {{"pins", "2"}}), part("DRC-WIRE", PartKind::Wire, {}),
                 part("DRC-CABLE", PartKind::Cable, {{"od", "6"}})};
    // A HARNESS WITH SOMETHING WRONG EVERYWHERE.
    BrickDesign d;
    const QString x1 = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
    const QString x2 = d.addBrick(BrickType::End, PartKind::Connector, 4, 0);
    const QString lone = d.addBrick(BrickType::End, PartKind::Connector, 0, 4);
    const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 2, 0);
    const QString open = d.addBrick(BrickType::Run, PartKind::Cable, 2, 3);
    const QString br = d.addBrick(BrickType::Run, PartKind::Wire, 2, 5);
    const QString bw = d.addBrick(BrickType::Run, PartKind::Wire, 3, 1);
    const QString j = d.addBrick(BrickType::Joint, PartKind::Splice, 1, 6);
    makeBranch(*d.brick(br));
    makeBridgeWire(*d.brick(bw));
    d.connect(x1, c);
    d.connect(c, x2);
    d.connect(x2, open);   // nothing at its other end
    d.connect(bw, c, nullptr, 1, 0);
    d.brick(x1)->name = QStringLiteral("X1");
    d.brick(x2)->name = QStringLiteral("X1");   // the same designator twice
    d.brick(x1)->libPart = QStringLiteral("DRC-CONN2");
    d.brick(x2)->libPart = QStringLiteral("NOT-IN-LIB");
    BrickRunSpec& r = d.brick(c)->run;
    r.wires = 4;   // four wires into a two-way connector
    r.stack = {QStringLiteral("wires"), QStringLiteral("cable")};
    r.stackParts.insert(QStringLiteral("wires"), QStringLiteral("DRC-WIRE"));
    r.lengthMm = 20.0;
    r.fanStartMm = 15.0;
    r.fanFinishMm = 15.0;   // fans longer than the run
    r.transitions = RunEnds::Both;   // and no part number for its transitions
    d.brick(bw)->run.bridgeWires = QStringLiteral("7");   // the cable has four
    d.brick(open)->run.lengthMm = 0.0;
    const std::vector<BrickIssue> found = brickDrc(d, QStringLiteral("W9"), lib, [](const QString&) { return 0; });
    QSet<QString> rules;
    for (const BrickIssue& i : found) rules.insert(i.rule);
    std::printf("  drc: %d finding(s) under %d rule(s):", static_cast<int>(found.size()), static_cast<int>(rules.size()));
    for (const QString& rule : rules) std::printf(" %s", qPrintable(rule));
    std::printf("\n");
    for (const char* rule : {"dupdesig", "nodesig", "nopart", "notinlib", "unlinked", "openrun", "nolength", "shortrun",
                             "nolayerpn", "bridge", "branch", "joint", "transpn", "notonbom", "compat-ways", "compat-data"})
        check(rules.contains(QLatin1String(rule)), qPrintable(QStringLiteral("drc: the %1 rule finds its fault").arg(QLatin1String(rule))));
    QSet<QString> known;
    for (const BrickDrcRule& rule : brickDrcRules()) known.insert(rule.id);
    bool allKnown = true;
    for (const QString& rule : rules) allKnown = allKnown && known.contains(rule);
    check(allKnown, "drc: every finding is under a rule the rules list has");
    QSet<QString> keys;
    for (const BrickIssue& i : found) keys.insert(i.key);
    check(keys.size() == static_cast<int>(found.size()) && !found.front().key.startsWith(QStringLiteral("|")),
          "drc: each finding its own key, its harness first");
    (void)lone;
    (void)j;

    // THE DRC WINDOW: a harness saved in the drawing checked; bypass one, turn a rule off.
    {
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        QJsonObject all;
        QJsonObject saved;
        saved.insert(QStringLiteral("W9"), d.toJson());
        all.insert(QStringLiteral("harnesses"), saved);
        doc.wizards() = all;
        MechanicalValidationDialog drc(&ed);
        drc.readFromDrawing();
        const int before = static_cast<int>(drc.shownHarnessIssues().size());
        auto* table = drc.findChild<QTableWidget*>(QStringLiteral("harnessDrcTable"));
        std::printf("  drc window: %d finding(s) shown, %d row(s)\n", before, table ? table->rowCount() : -1);
        check(before > 0 && table && table->rowCount() == before, "drc window: the saved harness's findings listed");
        auto* by = table ? qobject_cast<QPushButton*>(table->cellWidget(0, 4)) : nullptr;
        if (by) by->click();
        check(static_cast<int>(drc.shownHarnessIssues().size()) == before - 1 && ed.drcBypassed().size() == 1 &&
                  ed.bypassedIssues().isEmpty(),
              "drc window: bypassed, it leaves the list - in the DRC's own bypasses, not the schematic's");
        drc.showBypass();
        auto* bt = drc.findChild<QTableWidget*>(QStringLiteral("harnessDrcBypassTable"));
        check(bt && bt->rowCount() == 1, "drc window: and is in its bypass table");
        ed.setDrcRuleEnabled(QStringLiteral("dupdesig"), false);
        drc.revalidate();
        bool dup = false;
        for (const BrickIssue& i : drc.shownHarnessIssues()) dup = dup || i.rule == QLatin1String("dupdesig");
        check(!dup && ed.ruleEnabled(QStringLiteral("dup")), "drc window: a rule turned off finds nothing - the schematic's untouched");
        drc.showRules();
        check(drc.findChild<QToolButton*>(QStringLiteral("harnessDrcRule_dupdesig")) &&
                  drc.findChild<QToolButton*>(QStringLiteral("harnessDrcRule_dupdesig"))->property("severity").toInt() == 0 &&
                  drc.findChild<QToolButton*>(QStringLiteral("harnessDrcRule_compat-gauge")),
              "drc window: its rules, logical and compatibility, each a tick");
    }
    // THE WHEEL a twentieth bigger.
    {
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        BrickWizardDialog dlg(&ed);
        auto* wheel = dlg.findChild<NavWheel*>(QStringLiteral("brickNavWheel"));
        check(wheel && wheel->width() >= 117, "nav: the wheel a twentieth bigger");
    }
}

// ROUND THIRTY-FOUR: entry sizes from the parts' own parameters; the rules by their trade names, each explained;
// hovered, a rule shows its animated explainer; the DRC's rules and bypass table open on their own.
void testRoundThirtyFour() {
    auto part = [](const char* id, PartKind k, std::initializer_list<std::pair<const char*, const char*>> ps) {
        LibraryPart p;
        p.id = p.mfrPn = QString::fromLatin1(id);
        p.kind = k;
        for (const auto& [key, v] : ps) p.params[QString::fromLatin1(key)] = QString::fromLatin1(v);
        return p;
    };
    PartsLibrary lib;
    lib.parts = {part("ES-WIRE", PartKind::Wire, {{"od", "1.0"}}), part("ES-CABLE", PartKind::Cable, {{"od", "8"}}),
                 part("ES-SMALL", PartKind::Heatshrink, {{"entryMin", "2"}, {"entryMax", "5"}, {"wall", "0.5"}}),
                 part("ES-BIG", PartKind::Heatshrink, {{"entryMin", "2"}, {"entryMax", "12"}, {"wall", "0.5"}}),
                 part("ES-END", PartKind::Heatshrink, {{"entryMin", "2"}, {"entryMax", "4"}}),
                 part("ES-CONN", PartKind::Connector, {{"pins", "4"}})};
    BrickDesign d;
    const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
    const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
    const QString y = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
    d.connect(x, c);
    d.connect(c, y);
    d.brick(x)->libPart = QStringLiteral("ES-CONN");
    BrickRunSpec& r = d.brick(c)->run;
    r.wires = 4;
    r.stack = {QStringLiteral("wires"), QStringLiteral("cable")};
    r.stackParts.insert(QStringLiteral("wires"), QStringLiteral("ES-WIRE"));
    r.stackParts.insert(QStringLiteral("cable"), QStringLiteral("ES-CABLE"));
    r.transitions = RunEnds::Both;
    r.transitionPart = QStringLiteral("ES-SMALL");   // opens to 5 over an 8 mm cable
    r.fanEndShrink = true;
    r.fanEndShrinkPart = QStringLiteral("ES-END");    // recovers to 2 on a 1 mm wire
    auto said = [&] {
        QStringList out;
        for (const BrickIssue& i : brickDrc(d, QStringLiteral("W1"), lib, [](const QString&) { return 1; }))
            if (i.rule == QLatin1String("compat-entrysize") || i.rule == QLatin1String("compat-data")) out << i.message;
        return out;
    };
    const QStringList wrong = said();
    for (const QString& m : wrong) std::printf("  entry size: %s\n", qPrintable(m));
    auto has = [&](const QStringList& l, const char* bit) {
        for (const QString& m : l)
            if (m.contains(QLatin1String(bit))) return true;
        return false;
    };
    check(has(wrong, "opens to 5.0 mm - the cable body is 8.0 mm"), "entry size: a transition too small to go over the cable");
    check(has(wrong, "wire-end sleeve ES-END recovers only to 2.0 mm"), "entry size: a wire-end sleeve too big to grip its wire");
    check(has(wrong, "cable entry of ES-CONN is not in the library"), "entry size: a connector's cable entry missing from the library");
    r.transitionPart = QStringLiteral("ES-BIG");
    r.fanEndShrink = false;
    check(!has(said(), "opens to"), "entry size: a transition that goes over the cable passes");

    // THE RULES BY THEIR TRADE NAMES, every one explained, every one with its picture.
    bool named = false, explained = true, pictured = true;
    for (const BrickDrcRule& rule : brickDrcRules()) {
        named = named || rule.label == QStringLiteral("Duplicate reference designator");
        explained = explained && rule.description.size() > 20;
        pictured = pictured && RuleExplainer::hasScene(rule.id);
    }
    for (const char* id : {"nc", "openwire", "dup", "mixedpins", "norefdes", "nopn", "shieldpair", "twistpair", "shielddrain"})
        pictured = pictured && RuleExplainer::hasScene(QLatin1String(id));
    check(named && explained, "rules: by their trade names, each with what it checks");
    check(pictured, "rules: every harness and schematic rule has its own animated picture");

    // HOVERED IN THE RULES LIST, the explainer shows beside it; left, it goes. And the rules open on their own.
    {
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        MechanicalValidationDialog drc(&ed);
        drc.showRules();
        auto* rules = drc.findChild<QDialog*>(QStringLiteral("harnessDrcRulesDialog"));
        check(rules && rules->isVisible() && !drc.isVisible(), "rules: open on their own, without the DRC window");
        drc.showBypass();
        auto* bypass = drc.findChild<QDialog*>(QStringLiteral("harnessDrcBypassDialog"));
        check(bypass && bypass->isVisible() && !drc.isVisible(), "bypass table: opens on its own too");
        auto* cb = drc.findChild<QLabel*>(QStringLiteral("harnessDrcRuleName_compat-entrysize"));
        if (cb) {
            QEvent enter(QEvent::Enter);
            QApplication::sendEvent(cb, &enter);
            RuleExplainer* x = RuleExplainer::instance();
            check(x->isVisible() && x->rule() == QStringLiteral("compat-entrysize"), "rules: hovered, its explainer shows");
            QEvent leave(QEvent::Leave);
            QApplication::sendEvent(cb, &leave);
            check(!x->isVisible(), "rules: left, it goes");
        } else {
            check(false, "rules: the entry size rule in the list");
        }
        if (rules) rules->close();
        if (bypass) bypass->close();
    }
}

// ROUND THIRTY-FIVE: each rule's severity a square - red fail, yellow warning, green pass - pressed round, with a
// legend; a part moved off its wires on the drawing fails; a finding double-clicked goes to its part.
void testRoundThirtyFive() {
    // THE HARNESS AS PLACED: its objects marked by part, as placing marks them.
    BrickDesign d;
    const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
    const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
    const QString y = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
    d.connect(x, c);
    d.connect(c, y);
    d.brick(x)->name = QStringLiteral("X1");
    d.brick(y)->name = QStringLiteral("X2");
    std::vector<Entity> placed = buildBrickHarness(d, nullptr).entities;
    for (Entity& e : placed) {
        const QString bid = e.meta.value(QStringLiteral("brickId")).toString();
        e.meta.insert(QStringLiteral("partCopy"), QStringLiteral("copy-") + bid);
        e.meta.insert(QStringLiteral("harness"), QStringLiteral("W1"));
        e.meta.insert(QStringLiteral("harnessBrick"), bid);
        e.meta.remove(QStringLiteral("brickId"));
    }
    check(drawingConnectivity(placed).empty(), "drawn: every part on its run - nothing found");
    // X2 moved off its wires on the sheet.
    for (Entity& e : placed)
        if (e.meta.value(QStringLiteral("harnessBrick")).toString() == y) {
            e.a = e.a + Vec2{0.0, 300.0};
            e.b = e.b + Vec2{0.0, 300.0};
            e.center = e.center + Vec2{0.0, 300.0};
            for (Vec2& v : e.points) v = v + Vec2{0.0, 300.0};
        }
    const std::vector<BrickIssue> off = drawingConnectivity(placed);
    for (const BrickIssue& i : off) std::printf("  drawn: %s\n", qPrintable(i.message));
    check(off.size() == 1 && off.front().rule == QStringLiteral("drawn-open") && off.front().brickId == y &&
              off.front().located && off.front().atY > 200.0,
          "drawn: a part moved off its wires found, and where it is");

    // THE DRC WINDOW: the drawing checked too; double-clicked, a finding asks to be found; the squares.
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    for (const Entity& e : placed) doc.entities().push_back(e);
    MechanicalValidationDialog drc(&ed);
    drc.readFromDrawing();
    bool drawnShown = false;
    for (const BrickIssue& i : drc.shownHarnessIssues()) drawnShown = drawnShown || i.rule == QLatin1String("drawn-open");
    check(drawnShown, "drc window: the part off its wires on the drawing fails");
    auto* table = drc.findChild<QTableWidget*>(QStringLiteral("harnessDrcTable"));
    QString askedFor;
    bool where = false;
    QObject::connect(&drc, &MechanicalValidationDialog::locateRequested,
                     [&](const QString&, const QString& brickId, double, double, bool located) {
                         askedFor = brickId;
                         where = located;
                     });
    if (table && table->rowCount() > 0) emit table->cellDoubleClicked(0, 0);
    check(askedFor == y && where, "drc window: double-clicked, the finding asks for its part on the drawing");
    drc.showRules();
    auto* sq = drc.findChild<QToolButton*>(QStringLiteral("harnessDrcRule_drawn-open"));
    check(sq && sq->property("severity").toInt() == 2, "rules: a fail is red as it comes");
    if (sq) sq->click();
    check(ed.drcSeverity(QStringLiteral("drawn-open"), 2) == 1 && sq && sq->property("severity").toInt() == 1,
          "rules: pressed, red turns yellow - a warning");
    int level = -1;
    for (const BrickIssue& i : drc.shownHarnessIssues())
        if (i.rule == QLatin1String("drawn-open")) level = i.level;
    check(level == 1, "rules: and its findings are warnings now");
    if (sq) sq->click();
    bool gone = true;
    for (const BrickIssue& i : drc.shownHarnessIssues()) gone = gone && i.rule != QLatin1String("drawn-open");
    check(sq && sq->property("severity").toInt() == 0 && gone, "rules: pressed again, green - a pass, not shown");
    if (sq) sq->click();
    check(sq && sq->property("severity").toInt() == 2, "rules: and round to red again");
    auto* legend = drc.findChild<QDialog*>(QStringLiteral("harnessDrcRulesDialog"));
    bool hasLegend = false;
    if (legend)
        for (QLabel* l : legend->findChildren<QLabel*>()) hasLegend = hasLegend || l->text() == QStringLiteral("Fail (error)");
    check(hasLegend, "rules: with its colours' legend");
    if (legend) legend->close();
}

// ROUND THIRTY-SIX: the 3D preview - models read (STL, OBJ, STEP), the harness built in 3D (tubes for its runs, a part's
// own model or a box), drawn by the software renderer, turned, moved and zoomed in its pane.
void testRoundThirtySix() {
    const QString dir = QDir::tempPath() + QStringLiteral("/edraw3d");
    QDir().mkpath(dir);
    // ---- SHAPES ----
    {
        const Mesh3D box = boxMesh({0, 0, 0}, {2, 3, 4});
        V3 lo, hi;
        check(box.f.size() == 12 && box.bounds(lo, hi) && std::abs(hi.z - 4.0) < 1e-9, "3d: a box of twelve triangles");
        const Mesh3D tube = tubeMesh({{0, 0, 0}, {10, 0, 0}, {10, 10, 0}}, 1.0, 12, true);
        check(!tube.empty() && tube.bounds(lo, hi) && std::abs(hi.y - 10.0) < 0.05 && std::abs(hi.x - 11.0) < 0.2 && std::abs(lo.z + 1.0) < 0.05,
              "3d: a tube along a path, round it by its radius");
    }
    // ---- READING MODELS ----
    {
        QFile a(dir + QStringLiteral("/tri.stl"));
        a.open(QIODevice::WriteOnly);
        a.write("solid t\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nvertex 0 1 0\nendloop\nendfacet\n"
                "facet normal 0 0 1\nouter loop\nvertex 1 0 0\nvertex 1 1 0\nvertex 0 1 0\nendloop\nendfacet\nendsolid t\n");
        a.close();
        QString how;
        check(loadMesh3D(a.fileName(), &how).f.size() == 2, "3d: a text STL read");
        // Binary: one triangle.
        QFile b(dir + QStringLiteral("/tri_b.stl"));
        b.open(QIODevice::WriteOnly);
        QByteArray bin(80, ' ');
        quint32 n = 1;
        bin.append(reinterpret_cast<const char*>(&n), 4);
        float f[12] = {0, 0, 1, 0, 0, 0, 5, 0, 0, 0, 5, 0};
        bin.append(reinterpret_cast<const char*>(f), 48);
        bin.append(2, '\0');
        b.write(bin);
        b.close();
        V3 lo, hi;
        const Mesh3D bm = loadMesh3D(b.fileName(), &how);
        check(bm.f.size() == 1 && bm.bounds(lo, hi) && std::abs(hi.x - 5.0) < 1e-6, "3d: a binary STL read");
        QFile o(dir + QStringLiteral("/quad.obj"));
        o.open(QIODevice::WriteOnly);
        o.write("v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nf 1 2 3 4\n");
        o.close();
        check(loadMesh3D(o.fileName(), &how).f.size() == 2, "3d: an OBJ read, its quad as two triangles");
        // STEP: a cylinder between two circles, with its caps.
        QByteArray s = "ISO-10303-21;\nHEADER;\nENDSEC;\nDATA;\n";
        int id = 1;
        auto add = [&](const QByteArray& body) {
            s += "#" + QByteArray::number(id) + "=" + body + ";\n";
            return id++;
        };
        auto ref = [](int i) { return "#" + QByteArray::number(i); };
        auto pt = [&](double x, double y, double z) {
            return add("CARTESIAN_POINT('',(" + QByteArray::number(x) + "," + QByteArray::number(y) + "," + QByteArray::number(z) + "))");
        };
        const int ax = add("DIRECTION('',(1.,0.,0.))"), rd = add("DIRECTION('',(0.,1.,0.))");
        const int pl0 = add("AXIS2_PLACEMENT_3D(''," + ref(pt(0, 0, 0)) + "," + ref(ax) + "," + ref(rd) + ")");
        const int pl1 = add("AXIS2_PLACEMENT_3D(''," + ref(pt(40, 0, 0)) + "," + ref(ax) + "," + ref(rd) + ")");
        const int c0 = add("CIRCLE(''," + ref(pl0) + ",8.)"), c1 = add("CIRCLE(''," + ref(pl1) + ",8.)");
        const int v0 = add("VERTEX_POINT(''," + ref(pt(0, 8, 0)) + ")"), v1 = add("VERTEX_POINT(''," + ref(pt(40, 8, 0)) + ")");
        const int e0 = add("EDGE_CURVE(''," + ref(v0) + "," + ref(v0) + "," + ref(c0) + ",.T.)");
        const int e1 = add("EDGE_CURVE(''," + ref(v1) + "," + ref(v1) + "," + ref(c1) + ",.T.)");
        const int l0 = add("EDGE_LOOP('',(" + ref(add("ORIENTED_EDGE('',*,*," + ref(e0) + ",.T.)")) + "))");
        const int l1 = add("EDGE_LOOP('',(" + ref(add("ORIENTED_EDGE('',*,*," + ref(e1) + ",.F.)")) + "))");
        add("ADVANCED_FACE('',(" + ref(add("FACE_OUTER_BOUND(''," + ref(l0) + ",.T.)")) + "," + ref(add("FACE_BOUND(''," + ref(l1) + ",.T.)")) +
            ")," + ref(add("CYLINDRICAL_SURFACE(''," + ref(pl0) + ",8.)")) + ",.T.)");
        add("ADVANCED_FACE('',(" + ref(add("FACE_OUTER_BOUND(''," + ref(l0) + ",.T.)")) + ")," + ref(add("PLANE(''," + ref(pl0) + ")")) + ",.T.)");
        add("ADVANCED_FACE('',(" + ref(add("FACE_OUTER_BOUND(''," + ref(l1) + ",.T.)")) + ")," + ref(add("PLANE(''," + ref(pl1) + ")")) + ",.T.)");
        s += "ENDSEC;\nEND-ISO-10303-21;\n";
        const Mesh3D step = loadStepText(s, &how);
        std::printf("  3d: %s\n", qPrintable(how));
        check(!step.empty() && step.bounds(lo, hi) && std::abs(hi.x - 40.0) < 1e-6 && std::abs(hi.y - 8.0) < 0.05 &&
                  std::abs(lo.z + 8.0) < 0.05,
              "3d: a STEP cylinder read from its circles - as long and as round as it is");
        check(loadStepText("ISO-10303-21;\nHEADER;\nENDSEC;\n", &how).empty() && how.contains(QStringLiteral("DATA")),
              "3d: not a STEP solid - nothing, and why");
        QFile sf(dir + QStringLiteral("/cyl.stp"));
        sf.open(QIODevice::WriteOnly);
        sf.write(s);
        sf.close();
    }
    // ---- THE HARNESS IN 3D ----
    BrickDesign d;
    const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
    const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
    const QString y = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
    d.connect(x, c);
    d.connect(c, y);
    d.brick(c)->run.wires = 4;
    d.brick(c)->run.transitions = RunEnds::Both;
    std::vector<Entity> ents = buildBrickHarness(d, nullptr).entities;
    for (Entity& e : ents)
        if (e.meta.value(QStringLiteral("brickId")).toString() == y) e.meta.insert(QStringLiteral("libPart"), QStringLiteral("HAS-3D"));
    const QString stp = dir + QStringLiteral("/cyl.stp");
    const Harness3DScene sc = buildHarness3D(ents, [&](const QString& lib) { return lib == QLatin1String("HAS-3D") ? stp : QString(); });
    int cubes = 0, models = 0, sleeves = 0, wires = 0;
    for (const Harness3DItem& it : sc.items) {
        cubes += it.cube ? 1 : 0;
        models += it.model ? 1 : 0;
        sleeves += it.what == QLatin1String("transition") ? 1 : 0;
        wires += it.what == QLatin1String("wire") ? 1 : 0;
    }
    std::printf("  3d harness: %d part(s), %d model(s), %d cube(s), %d run(s), %d sleeve(s), %d wire piece(s)\n", sc.parts,
                models, cubes, sc.runs, sleeves, wires);
    check(sc.parts == 2 && models == 1 && cubes == 1 && sc.modelled == 1, "3d harness: a part with a 3D model shown by it, one without as a box");
    check(sc.runs == 1 && sleeves == 2 && wires >= 4, "3d harness: the run's body, its two transitions and its wires");
    // The model fitted to the part as drawn: as long as it is, where it is.
    {
        Bounds drawn;
        for (const Entity& e : ents)
            if (e.meta.value(QStringLiteral("brickId")).toString() == y && e.type != EntityType::Text &&
                e.meta.value(QStringLiteral("mechRole")).toString() != QLatin1String("designator"))
                drawn.expand(e.bounds());
        for (const Harness3DItem& it : sc.items)
            if (it.model) {
                V3 lo, hi;
                it.mesh.bounds(lo, hi);
                std::printf("  3d model: %.1f..%.1f across a part drawn %.1f..%.1f\n", lo.x, hi.x, drawn.min.x, drawn.max.x);
                check(std::abs((hi.x - lo.x) - (drawn.max.x - drawn.min.x)) < 0.5 &&
                          std::abs((hi.x + lo.x) / 2.0 - (drawn.max.x + drawn.min.x) / 2.0) < 0.5,
                      "3d model: fitted to the part - as long as it is drawn, where it is");
            }
    }
    // A RUN OF ONE WIRE - drawn as a wire, not a bundle - is a run in 3D too, not a part: no box over its length; and on
    // the placed drawing its parts are on it.
    {
        BrickDesign one;
        const QString p = one.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString w = one.addBrick(BrickType::Run, PartKind::Wire, 1, 0);
        const QString q = one.addBrick(BrickType::End, PartKind::Ferrule, 2, 0);
        one.connect(p, w);
        one.connect(w, q);
        one.brick(w)->run.wires = 1;
        std::vector<Entity> oe = buildBrickHarness(one, nullptr).entities;
        const Harness3DScene os = buildHarness3D(oe, {});
        std::printf("  3d one wire: %d part(s), %d run(s)\n", os.parts, os.runs);
        check(os.parts == 2 && os.runs == 1, "3d: a run of one wire is a run, not a part boxed over its length");
        for (Entity& e : oe) {
            e.meta.insert(QStringLiteral("partCopy"), QStringLiteral("c-") + e.meta.value(QStringLiteral("brickId")).toString());
            e.meta.remove(QStringLiteral("brickId"));
        }
        check(drawingConnectivity(oe).empty(), "drawn: the parts on a run of one wire are on it");
    }
    // ---- DRAWN ----
    {
        std::vector<DrawItem3D> items;
        for (const Harness3DItem& it : sc.items) items.push_back({&it.mesh, it.color});
        Camera3D cam;
        V3 lo{-50, -50, -20}, hi{250, 50, 20};
        cam.fit(lo, hi, 1.5);
        QImage img(300, 200, QImage::Format_RGB32);
        const QColor bg(30, 36, 44);
        renderScene3D(img, items, cam, false, bg);
        int lit = 0;
        for (int yy = 0; yy < img.height(); ++yy)
            for (int xx = 0; xx < img.width(); ++xx) lit += img.pixel(xx, yy) != bg.rgb() ? 1 : 0;
        std::printf("  3d picture: %d of %d pixels drawn\n", lit, img.width() * img.height());
        check(lit > 500, "3d picture: the harness drawn");
    }
    // ---- THE PANE ----
    {
        View3DPane pane;
        pane.resize(640, 420);
        pane.setSources([&] { return ents; }, [] { return std::vector<Entity>{}; });
        pane.setModelLookup([&](const QString& lib) { return lib == QLatin1String("HAS-3D") ? stp : QString(); });
        pane.show();
        pane.rebuild(true);
        auto* status = pane.findChild<QLabel*>(QStringLiteral("view3dStatus"));
        check(pane.scene().parts == 2 && status && status->text().contains(QStringLiteral("1 with a 3D model")),
              "3d pane: built from the drawing, and says what it shows");
        View3DCanvas* canvas = pane.canvas();
        const double yaw = canvas->camera().yawDeg, dist = canvas->camera().distance;
        mouse(canvas, QEvent::MouseButtonPress, QPointF(100, 100));
        QMouseEvent mv(QEvent::MouseMove, QPointF(160, 120), canvas->mapToGlobal(QPointF(160, 120)), Qt::NoButton, Qt::LeftButton,
                       Qt::NoModifier);
        QApplication::sendEvent(canvas, &mv);
        mouse(canvas, QEvent::MouseButtonRelease, QPointF(160, 120));
        check(std::abs(canvas->camera().yawDeg - yaw) > 1.0, "3d pane: dragged, it turns");
        QWheelEvent wheel(QPointF(100, 100), canvas->mapToGlobal(QPointF(100, 100)), QPoint(), QPoint(0, 120), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(canvas, &wheel);
        check(canvas->camera().distance < dist, "3d pane: wheeled, it zooms in");
        if (auto* top = pane.findChild<QToolButton*>(QStringLiteral("view3dTop"))) top->click();
        check(canvas->camera().pitchDeg > 80.0, "3d pane: Top - from above");
        if (auto* wf = pane.findChild<QCheckBox*>(QStringLiteral("view3dWireframe"))) wf->setChecked(true);
        const QImage pic = canvas->render(QSize(200, 150));
        check(!pic.isNull(), "3d pane: drawn in wireframe too");
    }
}

// ROUND THIRTY-SEVEN: the wires in their own colours (the schematic's by net, the wire part's), general colours where
// none is known; the wizard's pane switched to 3D; the bricks picked lit in 3D.
void testRoundThirtySeven() {
    PartsCatalog cat;
    cat.setRemembering(false);
    QString red;
    {
        LibraryPart& w = cat.active().add(PartKind::Wire);
        w.mfrPn = QStringLiteral("RED-WIRE");
        w.params[QStringLiteral("colour")] = QStringLiteral("Red");
        red = w.id;
    }
    BrickDesign d;
    d.catalog = &cat;
    const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
    const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
    const QString y = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
    d.connect(x, c);
    d.connect(c, y);
    BrickRunSpec& r = d.brick(c)->run;
    r.wires = 3;
    auto coloursOf = [&] {
        for (const Entity& e : buildBrickHarness(d, &cat).entities)
            if (e.type == EntityType::TwistedPair) return e.meta.value(QStringLiteral("wireColours")).toArray();
        return QJsonArray();
    };
    check(coloursOf().isEmpty(), "colours: none known - none said, the general colours shown");
    r.stackParts.insert(QStringLiteral("wires"), red);
    QJsonArray said = coloursOf();
    std::printf("  colours: from the wire part %s %s %s\n", qPrintable(said.at(0).toString()), qPrintable(said.at(1).toString()),
                qPrintable(said.at(2).toString()));
    check(said.size() == 3 && QColor(said.at(0).toString()).red() > 150 && QColor(said.at(0).toString()).green() < 100,
          "colours: the wire part's colour on every wire");
    // THE SCHEMATIC'S COLOUR BY NET comes first.
    r.nets = {QStringLiteral("N1"), QString(), QString()};
    d.netColours.insert(QStringLiteral("N1"), QStringLiteral("BU"));
    said = coloursOf();
    check(said.size() == 3 && QColor(said.at(0).toString()).blue() > QColor(said.at(0).toString()).red() &&
              QColor(said.at(1).toString()).red() > 150,
          "colours: the schematic's wire on a wire's net first, the wire part's for the rest");
    // In 3D, each wire in its colour.
    {
        std::vector<Entity> ents = buildBrickHarness(d, &cat).entities;
        const Harness3DScene sc = buildHarness3D(ents, {});
        bool blue = false, redWire = false;
        for (const Harness3DItem& it : sc.items)
            if (it.what == QLatin1String("wire")) {
                blue = blue || (it.color.blue() > 150 && it.color.red() < 100);
                redWire = redWire || (it.color.red() > 150 && it.color.green() < 100);
                check(it.brick == c, "3d: each wire knows its run's brick");
            }
        check(blue && redWire, "3d: the wires drawn in their own colours");
    }
    // THE WIZARD: its pane switched to 3D, the bricks picked lit there.
    {
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        BrickWizardDialog dlg(&ed);
        dlg.resize(1300, 850);
        dlg.show();
        BrickDesign& w = dlg.design();
        const QString a = w.addBrick(BrickType::End, PartKind::Connector, 0, 0);
        const QString run = w.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
        const QString b = w.addBrick(BrickType::End, PartKind::Connector, 2, 0);
        w.connect(a, run);
        w.connect(run, b);
        auto* sw = dlg.findChild<QComboBox*>(QStringLiteral("brickPaneSwitch"));
        auto* stack = dlg.findChild<QStackedWidget*>(QStringLiteral("brickPaneStack"));
        auto* v3 = dlg.findChild<View3DPane*>(QStringLiteral("brickView3d"));
        check(sw && stack && v3 && sw->count() == 3, "wizard: a switch at the bottom left - bricks and preview, or 3D in either's place");
        if (sw && stack && v3) {
            sw->setCurrentIndex(1);
            emit sw->activated(1);
            check(stack->currentWidget() == v3 && v3->scene().parts == 2 && v3->scene().runs == 1,
                  "wizard: 3D in the pane's place, the harness as it is being built");
            dlg.selectBrick(b);
            check(v3->canvas()->highlighted().contains(b), "wizard: the brick picked lit in 3D");
            // 3D IN THE PREVIEW'S PLACE: the bricks beside it, picked there and lit in it.
            auto* pstack = dlg.findChild<QStackedWidget*>(QStringLiteral("brickPreviewStack"));
            sw->setCurrentIndex(2);
            emit sw->activated(2);
            check(pstack && pstack->currentWidget() == v3 && stack->currentWidget() == dlg.canvas(),
                  "wizard: 3D in the preview's place, the bricks beside it");
            dlg.selectBrick(a);
            check(v3->canvas()->highlighted().contains(a), "wizard: a brick clicked there lit in the 3D beside it");
            sw->setCurrentIndex(0);
            emit sw->activated(0);
            check(stack->currentWidget() == dlg.canvas() && pstack && pstack->currentWidget() == dlg.preview(),
                  "wizard: and back to the bricks and the preview");
        }
        dlg.hide();
    }
}

// ROUND THIRTY-EIGHT: a twisted pair and a twisted bundle in 3D, their wires laid up round one another for real.
void testRoundThirtyEight() {
    BrickDesign d;
    const QString a = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
    const QString tp = d.addBrick(BrickType::TwistedPair, PartKind::Wire, 1, 0);
    const QString b = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
    const QString c = d.addBrick(BrickType::End, PartKind::Connector, 0, 2);
    const QString tb = d.addBrick(BrickType::TwistedBundle, PartKind::Wire, 1, 2);
    const QString e = d.addBrick(BrickType::End, PartKind::Connector, 2, 2);
    d.connect(a, tp);
    d.connect(tp, b);
    d.connect(c, tb);
    d.connect(tb, e);
    d.brick(tb)->run.wires = 4;
    const Harness3DScene sc = buildHarness3D(buildBrickHarness(d, nullptr).entities, {});
    int pair = 0, bundle = 0;
    bool rises = true;
    for (const Harness3DItem& it : sc.items) {
        if (it.what != QLatin1String("wire")) continue;
        (it.brick == tp ? pair : bundle)++;
        V3 lo, hi;
        // Laid up round its middle: up out of the sheet and down under it, not flat in it.
        rises = rises && it.mesh.bounds(lo, hi) && hi.z > 0.4 && lo.z < -0.4;
    }
    std::printf("  3d twist: the pair %d wire(s), the bundle %d\n", pair, bundle);
    check(sc.runs == 2 && pair == 2 && bundle == 4, "3d twist: a twisted pair and a twisted bundle shown, each wire of them");
    check(rises, "3d twist: each wire a helix round the run - over and under, not flat on the sheet");
}

// ROUND THIRTY-NINE: a note or a revision box - its text height set, the frame grows to hold the words.
void testRoundThirtyNine() {
    Document doc;
    Editor ed;
    ed.setDocument(&doc);
    auto box = [&](const QString& id, bool rev, const QString& text) {
        Entity e;
        e.id = id;
        e.type = EntityType::Rect;
        e.points = {{0, 0}, {40, 0}, {40, 20}, {0, 20}};
        e.a = {0, 20};
        e.meta.insert(QStringLiteral("textSize"), 4.0);
        e.meta.insert(rev ? QStringLiteral("revision") : QStringLiteral("note"), true);
        e.meta.insert(QStringLiteral("text"), text);
        if (rev) e.meta.insert(QStringLiteral("rev"), QStringLiteral("A"));
        doc.entities().push_back(e);
    };
    auto boundsOfId = [&](const QString& id) {
        for (const Entity& e : doc.entities())
            if (e.id == id) return e.bounds();
        return Bounds{};
    };
    box(QStringLiteral("n1"), false, QStringLiteral("qwe"));
    box(QStringLiteral("r1"), true, QStringLiteral("Revision description"));
    // THE HEIGHT MADE BIGGER: the note grows to hold its word, its top left corner kept.
    ed.selectById(QStringLiteral("n1"));
    ed.setSelectionMetaNumber(QStringLiteral("textSize"), 12.0);
    Bounds b = boundsOfId(QStringLiteral("n1"));
    std::printf("  note at 12: %.1f x %.1f, top left %.1f,%.1f\n", b.max.x - b.min.x, b.max.y - b.min.y, b.min.x, b.max.y);
    check(b.max.y - b.min.y > 20.0 && std::abs(b.max.y - 20.0) < 1e-9 && std::abs(b.min.x) < 1e-9,
          "note: its text made bigger, the frame grows downward to hold it - its top left kept");
    // A LONG WORD: wider.
    ed.setSelectionAnnotationText(QStringLiteral("Supercalifragilisticexpialidocious"));
    b = boundsOfId(QStringLiteral("n1"));
    check(b.max.x - b.min.x > 40.0, "note: a word too long for it - the frame widens to hold it");
    // MORE WORDS: taller.
    const double tall = b.max.y - b.min.y;
    ed.setSelectionAnnotationText(QStringLiteral("one two three four five six seven eight nine ten eleven twelve thirteen "
                                                 "fourteen fifteen sixteen seventeen eighteen nineteen twenty"));
    b = boundsOfId(QStringLiteral("n1"));
    check(b.max.y - b.min.y > tall, "note: more words than it holds - the frame grows to hold every line");
    // NEVER SMALLER.
    const Bounds before = b;
    ed.setSelectionMetaNumber(QStringLiteral("textSize"), 2.0);
    b = boundsOfId(QStringLiteral("n1"));
    check(std::abs((b.max.x - b.min.x) - (before.max.x - before.min.x)) < 1e-9 &&
              std::abs((b.max.y - b.min.y) - (before.max.y - before.min.y)) < 1e-9,
          "note: made smaller, the frame stays as it is");
    // A REVISION BOX: its title strip and its words.
    ed.selectById(QStringLiteral("r1"));
    ed.setSelectionMetaNumber(QStringLiteral("textSize"), 10.0);
    b = boundsOfId(QStringLiteral("r1"));
    std::printf("  revision at 10: %.1f x %.1f\n", b.max.x - b.min.x, b.max.y - b.min.y);
    check(b.max.y - b.min.y >= 10.0 * 1.8 + 2.0 * 10.0 && b.max.x - b.min.x > 40.0,
          "revision: its text made bigger - the frame grows for the strip and the words");
}

// ROUND FORTY: a twisted pair drawn on the sheet twisted in 3D too; a heatshrink sleeve a tube; a part with no 3D model
// made from its 2D drawing - turned round the way it goes in, or extruded.
void testRoundForty() {
    std::vector<Entity> ents;
    Entity t;
    t.type = EntityType::TwistedPair;
    t.points = {{0, 0}, {200, 0}};
    t.meta.insert(QStringLiteral("twistWidth"), 4.0);
    t.meta.insert(QStringLiteral("twistPitch"), 18.0);
    t.meta.insert(QStringLiteral("twistEnds"), 15.0);
    ents.push_back(t);
    Entity s;
    s.type = EntityType::Polyline;
    s.points = {{60, 0}, {120, 0}};
    s.meta.insert(QStringLiteral("double"), true);
    s.meta.insert(QStringLiteral("shrunkEnds"), true);
    s.meta.insert(QStringLiteral("shrinkWidth"), 8.0);
    ents.push_back(s);
    const Harness3DScene sc = buildHarness3D(ents, {});
    int wires = 0, sleeves = 0;
    bool rises = true;
    for (const Harness3DItem& it : sc.items) {
        V3 lo, hi;
        it.mesh.bounds(lo, hi);
        if (it.what == QLatin1String("wire")) {
            ++wires;
            rises = rises && hi.z > 0.5 && lo.z < -0.5;
        }
        if (it.what == QLatin1String("sleeve")) {
            ++sleeves;
            check(std::abs(hi.z - 4.0) < 0.1, "3d: a heatshrink sleeve as round as it is drawn wide");
        }
    }
    check(wires == 2 && rises, "3d: a twisted pair drawn on the sheet - said neither straight nor twisted - twisted for real");
    check(sleeves == 1, "3d: a heatshrink sleeve drawn on the sheet a tube");
    // A PART FROM ITS DRAWING: a connector drawn 30 long and 20 across, on a run.
    BrickDesign d;
    const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
    const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
    const QString y = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
    d.connect(x, c);
    d.connect(c, y);
    const std::vector<Entity> h = buildBrickHarness(d, nullptr).entities;
    for (PartLook look : {PartLook::Revolved, PartLook::Extruded}) {
        const Harness3DScene ps = buildHarness3D(h, {}, look);
        int made = 0, cubes = 0;
        for (const Harness3DItem& it : ps.items) {
            if (it.cube) ++cubes;
            if (it.brick != x && it.brick != y) continue;
            if (it.cube) continue;
            V3 lo, hi;
            it.mesh.bounds(lo, hi);
            ++made;
            if (look == PartLook::Revolved)
                check(std::abs((hi.z - lo.z) - (hi.y - lo.y)) < 0.5, "3d part: turned from its drawing - as tall as it is wide, round");
        }
        std::printf("  3d parts %s: %d from the drawing, %d box(es)\n", look == PartLook::Revolved ? "turned" : "extruded", made, cubes);
        check(made == 2 && cubes == 0, "3d part: each made from its 2D drawing, no box");
    }
    View3DPane pane;
    check(pane.findChild<QComboBox*>(QStringLiteral("view3dPartLook")) != nullptr,
          "3d pane: a choice of how a part with no 3D model is shown");
}

// ROUND FORTY-ONE: a twisted pair drawn by its two ends; a fanout's wires at its drawn pitch where they fan and in a
// round bundle where they gather, shrink over their ends; a sleeve's shrunk ends; a connector's pinout; a lone mark at
// a part's end no disc.
void testRoundFortyOne() {
    std::vector<Entity> ents;
    Entity t;
    t.type = EntityType::TwistedPair;
    t.a = {0, 0};
    t.b = {200, 0};
    t.meta.insert(QStringLiteral("twistWidth"), 4.0);
    ents.push_back(t);
    Harness3DScene sc = buildHarness3D(ents, {});
    int wires = 0;
    for (const Harness3DItem& it : sc.items) wires += it.what == QLatin1String("wire") ? 1 : 0;
    check(sc.runs == 1 && wires == 2, "3d: a twisted pair drawn by its two ends alone - shown");
    // A FANOUT: seven wires, 4.5 apart where they fan, converged to two lines in the drawing.
    ents.clear();
    Entity f;
    f.type = EntityType::TwistedPair;
    f.points = {{0, 0}, {200, 0}};
    f.meta.insert(QStringLiteral("twistStraight"), true);
    f.meta.insert(QStringLiteral("twistWires"), 7);
    f.meta.insert(QStringLiteral("twistTrunkWires"), 2);
    f.meta.insert(QStringLiteral("twistEnds"), 15.0);
    f.meta.insert(QStringLiteral("twistLane"), 4.5);
    f.meta.insert(QStringLiteral("twistWireWidth"), 2.0);
    f.meta.insert(QStringLiteral("twistEndShrink"), true);
    f.meta.insert(QStringLiteral("twistEndShrinkLen"), 10.0);
    ents.push_back(f);
    sc = buildHarness3D(ents, {});
    double endLo = 1e300, endHi = -1e300, midLo = 1e300, midHi = -1e300;
    int sleeves = 0;
    wires = 0;
    for (const Harness3DItem& it : sc.items) {
        if (it.what == QLatin1String("sleeve")) ++sleeves;
        if (it.what != QLatin1String("wire")) continue;
        ++wires;
        for (const V3& v : it.mesh.v) {
            if (v.x < 0.5) endLo = std::min(endLo, v.y), endHi = std::max(endHi, v.y);
            if (std::abs(v.x - 100.0) < 1.0) midLo = std::min(midLo, v.y), midHi = std::max(midHi, v.y);
        }
    }
    std::printf("  3d fanout: %d wire(s), %.1f across at its end, %.1f in the middle, %d sleeve(s)\n", wires, endHi - endLo,
                midHi - midLo, sleeves);
    check(wires == 7, "3d fanout: every wire, converged in the drawing or not");
    // As far apart at its end as the drawing has them there.
    double drawnLo = 1e300, drawnHi = -1e300;
    {
        const std::vector<std::vector<Vec2>> lanes = twistedPairStrands(f);
        for (size_t j = 0; j + 1 < lanes.size() && j / 2 < 7; j += 2)
            if (!lanes[j].empty() && !lanes[j + 1].empty()) {
                const double yy = (lanes[j].front().y + lanes[j + 1].front().y) / 2.0;
                drawnLo = std::min(drawnLo, yy);
                drawnHi = std::max(drawnHi, yy);
            }
    }
    std::printf("  3d fanout: the drawing has its wire ends %.1f apart end to end\n", drawnHi - drawnLo);
    check(std::abs((endHi - endLo) - (drawnHi - drawnLo + 2.0)) < 0.6,
          "3d fanout: at its end the wires as far apart as the drawing has them");
    check(midHi - midLo < 6 * 4.5 * 0.6 && midHi - midLo > 2.0, "3d fanout: gathered into a round bundle between the fans");
    check(sleeves == 14, "3d fanout: shrink over each wire's end, at both ends");
    // A SLEEVE'S SHRUNK ENDS.
    ents.clear();
    Entity s;
    s.type = EntityType::Polyline;
    s.points = {{0, 0}, {100, 0}};
    s.meta.insert(QStringLiteral("double"), true);
    s.meta.insert(QStringLiteral("shrunkEnds"), true);
    s.meta.insert(QStringLiteral("shrinkWidth"), 10.0);
    s.meta.insert(QStringLiteral("shrinkFaceStart"), 4.0);
    s.meta.insert(QStringLiteral("shrinkTaperStart"), 10.0);
    ents.push_back(s);
    sc = buildHarness3D(ents, {});
    double atEnd = 0.0, atMid = 0.0;
    for (const Harness3DItem& it : sc.items)
        for (const V3& v : it.mesh.v) {
            if (std::abs(v.x) < 0.01) atEnd = std::max(atEnd, std::abs(v.z));
            if (std::abs(v.x - 50.0) < 1.0) atMid = std::max(atMid, std::abs(v.z));
        }
    std::printf("  3d sleeve: %.2f across at its end, %.2f in the middle\n", 2 * atEnd, 2 * atMid);
    check(std::abs(atMid - 5.0) < 0.1 && std::abs(atEnd - 2.0) < 0.1, "3d sleeve: its end shrunk down to its face");
    // A CONNECTOR'S PINOUT, and a lone mark at a part's end not a disc.
    BrickDesign d;
    const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
    const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
    const QString y = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
    d.connect(x, c);
    d.connect(c, y);
    std::vector<Entity> h = buildBrickHarness(d, nullptr).entities;
    Bounds xb;
    for (Entity& e : h)
        if (e.meta.value(QStringLiteral("brickId")).toString() == x) {
            e.meta.insert(QStringLiteral("libPart"), QStringLiteral("M7"));
            if (e.type != EntityType::Text) xb.expand(e.bounds());
        }
    // A tall thin mark at the far end of X, as a ferrule's drawing might have.
    Entity mark;
    mark.type = EntityType::Line;
    mark.a = {xb.min.x, (xb.min.y + xb.max.y) / 2.0 - 30.0};
    mark.b = {xb.min.x, (xb.min.y + xb.max.y) / 2.0 + 30.0};
    mark.meta.insert(QStringLiteral("brickId"), x);
    h.push_back(mark);
    sc = buildHarness3D(h, {}, PartLook::Revolved, [](const QString& lib) {
        return lib == QLatin1String("M7") ? PartPins{7, false} : PartPins{};
    });
    int pins = 0;
    double xR = 0.0;
    for (const Harness3DItem& it : sc.items) {
        if (it.what == QLatin1String("pins")) pins += static_cast<int>(it.mesh.f.size()) > 0 ? 1 : 0;
        if (it.brick == x && it.what != QLatin1String("pins")) {
            V3 lo, hi;
            it.mesh.bounds(lo, hi);
            xR = std::max(xR, hi.z);
        }
    }
    std::printf("  3d part: turned to %.1f across (drawn %.1f), %d pinout(s)\n", 2 * xR, xb.max.y - xb.min.y, pins);
    check(pins == 1, "3d part: a connector's pins on its front face");
    // INSIDE IT: every pin within the connector's own length, its tip at the far face, nothing standing out.
    {
        V3 plo, phi, blo{1e300, 1e300, 1e300}, bhi{-1e300, -1e300, -1e300};
        bool inside = false;
        for (const Harness3DItem& it : sc.items)
            if (it.brick == x && it.what != QLatin1String("pins")) {
                V3 a, b;
                it.mesh.bounds(a, b);
                blo = {std::min(blo.x, a.x), std::min(blo.y, a.y), std::min(blo.z, a.z)};
                bhi = {std::max(bhi.x, b.x), std::max(bhi.y, b.y), std::max(bhi.z, b.z)};
            }
        for (const Harness3DItem& it : sc.items)
            if (it.what == QLatin1String("pins") && it.mesh.bounds(plo, phi))
                inside = plo.x >= blo.x - 0.05 && phi.x <= bhi.x + 0.05 && (std::abs(plo.x - blo.x) < 0.05 || std::abs(phi.x - bhi.x) < 0.05);
        check(inside, "3d part: its pins inside the connector, ending at its far face - none standing out of it");
    }
    check(xR < (xb.max.y - xb.min.y) * 0.75, "3d part: a lone mark at its end is not turned into a disc");
}

// ROUND FORTY-TWO: THE ENVIRONMENT - the CAD model a harness is routed in. Read from a STEP assembly through Open
// CASCADE (solids, names, colours, curved surfaces), picked by a ray, cut open by a section, seen through; loaded into
// the 3D view at once and in the background.
void testRoundFortyTwo() {
    // ---- PICKING: a ray finds the first face it meets, and says which mesh and which way the face looks ----
    {
        const Mesh3D a = boxMesh({0, 0, 0}, {10, 10, 10});
        const Mesh3D b = boxMesh({20, 0, 0}, {40, 10, 5});
        RayIndex3D index;
        index.build({&a, &b});
        Pick3D p = index.cast({5, 5, 100}, {0, 0, -1});
        check(p.hit && p.part == 0 && std::abs(p.point.z - 10.0) < 1e-9 && std::abs(p.normal.z - 1.0) < 1e-9,
              "3d pick: straight down onto the first box - its top, facing up");
        p = index.cast({-50, 5, 2}, {1, 0, 0});
        check(p.hit && p.part == 0 && std::abs(p.point.x) < 1e-9 && std::abs(p.normal.x + 1.0) < 1e-9,
              "3d pick: from the side - the nearer box, its face turned back towards the ray");
        p = index.cast({30, 5, 100}, {0, 0, -1});
        check(p.hit && p.part == 1 && std::abs(p.point.z - 5.0) < 1e-9, "3d pick: the second box, where it is lower");
        check(!index.cast({15, 5, 100}, {0, 0, -1}).hit, "3d pick: between them - nothing");
        // Down the diagonal the top's two triangles share, and on a corner: still the top.
        p = index.cast({5, 5, 100}, {0, 0, -1});
        const Pick3D corner = index.cast({10, 10, 100}, {0, 0, -1});
        check(p.hit && std::abs(p.point.z - 10.0) < 1e-9 && corner.hit && std::abs(corner.point.z - 10.0) < 1e-9,
              "3d pick: down the edge two triangles share, or on a corner - the face, not through it");
        // Many triangles: the tree still finds the right one.
        Mesh3D many;
        for (int i = 0; i < 400; ++i) many.append(boxMesh({i * 3.0, 0, 0}, {i * 3.0 + 2.0, 2, 1.0 + i * 0.01}));
        RayIndex3D big;
        big.build({&many});
        p = big.cast({300.5, 1, 50}, {0, 0, -1});
        check(p.hit && std::abs(p.point.z - (1.0 + 100 * 0.01)) < 1e-9, "3d pick: among 4800 triangles, the one under the ray");
    }
    // ---- THE CAMERA'S RAY through a pixel goes through what is drawn at that pixel ----
    {
        Camera3D cam;
        cam.fit({0, 0, 0}, {100, 50, 20}, 1.5);
        const int w = 600, h = 400;
        V3 right, up, forward;
        cam.axes(right, up, forward);
        const V3 eye = cam.eye();
        const double focal = 0.5 * h / std::tan(cam.fovDeg * 3.14159265358979323846 / 360.0);
        const V3 c{100, 50, 20};
        const V3 d = c - eye;
        const double z = dot3(d, forward);
        const double px = w * 0.5 + focal * dot3(d, right) / z, py = h * 0.5 - focal * dot3(d, up) / z;
        V3 o, dir;
        cam.rayAt(px, py, w, h, o, dir);
        const V3 off = (c - o) - dir * dot3(c - o, dir);
        check(off.length() < 1e-6, "3d: the ray through a pixel passes through the point drawn there");
    }
    // ---- THE SECTION AND SEE-THROUGH, in the software picture ----
    {
        const Mesh3D box = boxMesh({-50, -10, -20}, {50, 10, 20});
        Camera3D cam;
        cam.yawDeg = -90.0;
        cam.pitchDeg = 0.0;
        cam.fit({-50, -10, -20}, {50, 10, 20}, 1.5);
        const QColor bg(30, 36, 44);
        QImage img(300, 200, QImage::Format_RGB32);
        Section3D s;
        s.on = true;
        s.normal = {1, 0, 0};
        s.offset = 0.0;
        renderScene3D(img, {{&box, QColor(200, 200, 200), 1.0, true}}, cam, false, bg, s);
        const bool leftShown = img.pixel(90, 100) != bg.rgb(), rightCut = img.pixel(210, 100) == bg.rgb();
        check(leftShown && rightCut, "3d section: the model beyond the plane is cut away, the rest shown");
        renderScene3D(img, {{&box, QColor(200, 200, 200), 1.0, false}}, cam, false, bg, s);
        check(img.pixel(210, 100) != bg.rgb(), "3d section: what is not to be cut (the harness) stays whole");
        // See-through: a red box behind a half see-through blue one shows through it.
        const Mesh3D back = boxMesh({-50, 30, -20}, {50, 40, 20});
        renderScene3D(img, {{&box, QColor(0, 0, 255), 0.5, true}, {&back, QColor(255, 0, 0), 1.0, true}}, cam, false, bg);
        const QRgb mixed = img.pixel(150, 100);
        check(qRed(mixed) > 40 && qBlue(mixed) > 40, "3d: a see-through model shows what is behind it, blended with its own colour");
    }
    // ---- A STEP ASSEMBLY, READ WHOLE ----
    const QString dir = QDir::tempPath() + QStringLiteral("/edraw3d");
    QDir().mkpath(dir);
    QString stepPath;
#ifdef EDRAW_HAVE_OCCT
    stepPath = dir + QStringLiteral("/assembly.step");
    check(writeTestStepAssembly(stepPath), "3d env: a STEP assembly written for the test");
    const Environment3D env = loadEnvironment3D(stepPath);
    std::printf("  3d env: %s\n", qPrintable(env.how));
    check(env.parts.size() == 3, "3d env: every solid of the assembly, each a part of its own");
    const EnvPart3D* bracket = nullptr;
    const EnvPart3D* ball = nullptr;
    for (const EnvPart3D& p : env.parts) {
        if (p.name == QLatin1String("Bracket")) bracket = &p;
        if (p.name == QLatin1String("Ball")) ball = &p;
    }
    check(bracket && ball, "3d env: the solids by the names the model gives them");
    check(bracket && bracket->color.isValid() && bracket->color.red() > 200 && bracket->color.green() < 40,
          "3d env: and in the colours it gives them");
    V3 lo, hi;
    check(ball && ball->mesh.bounds(lo, hi) && std::abs(hi.x - 20.0) < 0.3 && std::abs(lo.z + 20.0) < 0.3 &&
              std::abs((lo.y + hi.y) / 2.0 - 40.0) < 0.3,
          "3d env: a sphere - a surface Edraw's own reader cannot take - read whole, as big as it is and where it is");
    check(fullStepReader(), "3d env: this build reads STEP whole");
    QString how;
    check(loadMesh3D(stepPath, &how).f.size() == env.triangles(), "3d: a part's STEP model read whole too");
    // A hand-written STEP Open CASCADE will not take still reads, by Edraw's own reader.
    check(!loadMesh3D(dir + QStringLiteral("/cyl.stp"), &how).empty(), "3d: a STEP file Open CASCADE refuses - Edraw's own reader");
#else
    std::printf("  3d env: built without Open CASCADE - STEP read by Edraw's own reader\n");
    stepPath = dir + QStringLiteral("/envquad.obj");
    {
        QFile o(stepPath);
        o.open(QIODevice::WriteOnly);
        o.write("v 0 0 10\nv 10 0 10\nv 10 10 10\nv 0 10 10\nf 1 2 3 4\n");
    }
#endif
    // ---- IN THE 3D VIEW ----
    View3DPane pane;
    pane.resize(900, 640);
    pane.show();
    QApplication::processEvents();
    check(pane.loadEnvironment(stepPath, true) && !pane.environment().empty(), "3d view: an environment loaded");
    View3DCanvas* canvas = pane.canvas();
    canvas->setUseGpu(false);
    canvas->viewFrom(QStringLiteral("top"));
    // The bracket from above: picked at its middle, where it is drawn.
    V3 right, up, forward;
    canvas->camera().axes(right, up, forward);
    const V3 eye = canvas->camera().eye();
    const V3 aim{5, 5, 10};
    const double focal = 0.5 * canvas->height() / std::tan(canvas->camera().fovDeg * 3.14159265358979323846 / 360.0);
    const double z = dot3(aim - eye, forward);
    const QPointF at(canvas->width() * 0.5 + focal * dot3(aim - eye, right) / z,
                     canvas->height() * 0.5 - focal * dot3(aim - eye, up) / z);
    QMouseEvent press(QEvent::MouseButtonPress, at, canvas->mapToGlobal(at), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease, at, canvas->mapToGlobal(at), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(canvas, &press);
    QApplication::sendEvent(canvas, &release);
    const Pick3D& picked = canvas->lastPick();
    const QString said = pane.findChild<QLabel*>(QStringLiteral("view3dStatus"))->text();
    std::printf("  3d view: %s\n", qPrintable(said.section(QLatin1Char('\n'), -1)));
    check(picked.hit && std::abs(picked.point.z - 10.0) < 0.05 && picked.normal.z > 0.99,
          "3d view: a click on the environment picks the point under it, and its face's normal");
#ifdef EDRAW_HAVE_OCCT
    check(said.contains(QStringLiteral("Picked Bracket")), "3d view: and says which solid it is, by its name");
#endif
    // A SECTION across Z below the bracket's top: the click goes on to the first face left standing.
    auto* section = pane.findChild<QComboBox*>(QStringLiteral("view3dSection"));
    auto* sectionAt = pane.findChild<QSlider*>(QStringLiteral("view3dSectionAt"));
    section->setCurrentIndex(3);
    emit section->activated(3);
    pane.environment().bounds(lo, hi);
    sectionAt->setValue(static_cast<int>(std::lround((7.0 - lo.z) / (hi.z - lo.z) * 1000.0)));
    check(canvas->section().on && std::abs(canvas->section().offset - 7.0) < 0.1, "3d view: a section across Z, where it was set");
    const Pick3D cut = canvas->pickAt(at);
    check(cut.hit && cut.point.z < 7.01, "3d view: a pick through a section - past what is cut away");
    // Opacity, the harness hidden: the picture still drawn.
    pane.findChild<QSlider*>(QStringLiteral("view3dEnvOpacity"))->setValue(40);
    pane.findChild<QCheckBox*>(QStringLiteral("view3dHarnessShown"))->setChecked(false);
    const QImage pic = canvas->render(QSize(320, 240));
    check(!pic.isNull() && canvas->rendererSaid().startsWith(QStringLiteral("software")), "3d view: drawn in software where asked");
    pane.clearEnvironment();
    check(pane.environment().empty() && !canvas->lastPick().hit, "3d view: the environment cleared, and the pick with it");
    // IN THE BACKGROUND: asked for, refused a second time while it reads, and there when it is done.
    check(pane.loadEnvironment(stepPath), "3d view: an environment read in the background");
    check(!pane.loadEnvironment(stepPath), "3d view: a second read while one is going - refused");
    QElapsedTimer clock;
    clock.start();
    while (pane.loadingEnvironment() && clock.elapsed() < 60000) QApplication::processEvents(QEventLoop::AllEvents, 50);
    check(!pane.loadingEnvironment() && !pane.environment().empty(), "3d view: read in the background, and shown when done");
    // The wizard's own 3D view has no environment controls.
    View3DPane wizardView;
    wizardView.setEnvironmentChoice(false);
    check(!wizardView.findChild<QToolButton*>(QStringLiteral("view3dEnvLoad"))->isVisibleTo(&wizardView),
          "3d view: the wizard's view - no environment row");
}

// ROUND FORTY-THREE: THE HARNESS ROUTED IN 3D - connectors put on the environment's faces, bundles laid between them
// over the faces clicked, kept off them by their radius, their corners turned on the bend radius; branches and clips
// dividing a bundle; each bundle's length off its path; saved with the drawing and read back.
void testRoundFortyThree() {
    // ---- THE PATH: out of a connector along its face's normal, the corner turned on the bend radius ----
    {
        Route3D r;
        r.diameter = 8.0;
        r.bendRadius = 5.0;
        r.lead = 10.0;
        const QString x1 = r.addNode(RouteNodeKind::Connector, {0, 0, 0}, {0, 0, 1});
        const QString x2 = r.addNode(RouteNodeKind::Connector, {100, 0, 0}, {0, 0, 1});
        const QString s = r.addSegment(x1, x2, {});
        check(r.node(x1)->label == QLatin1String("X1") && r.node(x2)->label == QLatin1String("X2"), "route: connectors labelled X1, X2");
        const double expect = 10.0 + 100.0 + 10.0 - 4.0 * 5.0 + 2.0 * (3.14159265358979323846 / 2.0 * 5.0);
        std::printf("  route: an up-over-down bundle %.2f mm (%.2f by hand)\n", r.length(*r.segment(s)), expect);
        check(std::abs(r.length(*r.segment(s)) - expect) < 0.05, "route: up out of one connector, over, down into the other - "
                                                                 "its corners on arcs of the bend radius, its length along them");
        int tight = 0;
        r.bendRadius = 100.0;
        r.centreline(*r.segment(s), &tight);
        check(tight == 2, "route: a bend radius there is no room for - said, corner by corner");
        // Laid over a face: its middle its radius off it.
        Route3D f;
        f.diameter = 8.0;
        const QString a = f.addNode(RouteNodeKind::Branch, {0, 0, 0}, {0, 0, 1});
        const QString b = f.addNode(RouteNodeKind::Branch, {50, 0, 0}, {0, 0, 1});
        const QString fs = f.addSegment(a, b, {{{25, 0, 0}, {0, 0, 1}}});
        bool lifted = true;
        for (const V3& p : f.corners(*f.segment(fs))) lifted = lifted && std::abs(p.z - 4.0) < 1e-9;
        check(lifted && std::abs(f.length(*f.segment(fs)) - 50.0) < 1e-6, "route: a bundle over a face lies on it - its middle its radius off");
        // Divided at its laid point by a clip: two bundles, the clip between them, the length kept.
        const QString clip = f.splitSegment(fs, {25, 0, 0}, {0, 0, 1}, 1, RouteNodeKind::Clip);
        double sum = 0.0;
        for (const RouteSegment3D& g : f.segments) sum += f.length(g);
        check(f.segments.size() == 2 && f.degree(clip) == 2 && f.node(clip)->label == QLatin1String("C1") && std::abs(sum - 50.0) < 1e-6,
              "route: a clip on a bundle divides it in two, the length the same");
        check(Route3D::fromJson(f.toJson()).toJson() == f.toJson(), "route: written and read back the same");
        f.removeNode(clip);
        check(f.segments.empty() && f.nodes.size() == 2, "route: a node taken away takes its bundles with it");
        // In the drawing's file.
        Document doc(QStringLiteral("routed"));
        doc.routing3d() = r.toJson();
        Document back(QStringLiteral("back"));
        dwx::fromJson(back, dwx::toJson(doc));
        check(back.routing3d() == r.toJson(), "route: saved in the drawing and read back with it");
        Document plain(QStringLiteral("plain"));
        check(!dwx::toJson(plain).contains(QStringLiteral("routing3d")), "route: a drawing with none saves none - old files as they were");
    }
#ifdef EDRAW_HAVE_OCCT
    // ---- IN THE 3D VIEW: connectors clicked on, a bundle laid, a branch put on it, taken back, saved, read back ----
    const QString dir = QDir::tempPath() + QStringLiteral("/edraw3d");
    const QString stepPath = dir + QStringLiteral("/routing.step");
    QDir().mkpath(dir);
    writeTestStepAssembly(stepPath);
    QJsonObject stored;
    int saves = 0;
    View3DPane pane;
    pane.setRoutingStore([&] { return stored; }, [&](const QJsonObject& o) { stored = o; ++saves; }, [&] { return dir; });
    pane.resize(1000, 700);
    pane.show();
    QApplication::processEvents();
    pane.loadEnvironment(stepPath, true);
    View3DCanvas* canvas = pane.canvas();
    canvas->setUseGpu(false);
    canvas->viewFrom(QStringLiteral("top"));
    auto click = [&](const V3& p) {
        QPointF at;
        canvas->toScreen(p, at);
        QMouseEvent press(QEvent::MouseButtonPress, at, canvas->mapToGlobal(at), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, at, canvas->mapToGlobal(at), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &press);
        QApplication::sendEvent(canvas, &release);
    };
    pane.setRouteTool(RouteTool::Connector);
    click({5, 5, 10});
    click({30, 5, 5});
    const Route3D& route = pane.route();
    check(route.nodes.size() == 2 && route.nodes[0].kind == RouteNodeKind::Connector &&
              std::abs(route.nodes[0].at.z - 10.0) < 0.05 && route.nodes[0].normal.z > 0.99 && std::abs(route.nodes[1].at.z - 5.0) < 0.05,
          "3d route: connectors put on the faces clicked, facing out of them");
    const QString x1 = route.nodes[0].id, x2 = route.nodes[1].id;
    // A bundle from X1, over the ground between the two solids, into X2.
    pane.setRouteTool(RouteTool::Route);
    click(route.nodes[0].at + route.nodes[0].normal * (route.lead * 0.5));
    check(canvas->layingRoute(), "3d route: a bundle started from the connector clicked");
    click({15, 5, 0});
    click(route.nodes[1].at + route.nodes[1].normal * (route.lead * 0.5));
    check(!canvas->layingRoute() && route.segments.size() == 1 && route.segments[0].a == x1 && route.segments[0].b == x2 &&
              route.segments[0].via.size() == 1,
          "3d route: laid from X1 over the point clicked into X2");
    auto* table = pane.findChild<QTableWidget*>(QStringLiteral("view3dRoutes"));
    const double len = route.segments.empty() ? 0.0 : route.length(route.segments[0]);
    std::printf("  3d route: X1 - X2 %.1f mm; table says %s\n", len, table->rowCount() ? qPrintable(table->item(0, 1)->text()) : "-");
    check(table->rowCount() == 1 && table->item(0, 0)->text() == QLatin1String("X1 - X2") && len > 30.0 &&
              table->item(0, 1)->text() == QStringLiteral("%1 mm").arg(len, 0, 'f', 1),
          "3d route: the bundle in the table, by its ends, with its length");
    check(stored.value(QStringLiteral("segments")).toArray().size() == 1 &&
              stored.value(QStringLiteral("environment")).toObject().value(QStringLiteral("file")).toString() == QLatin1String("routing.step"),
          "3d route: written to the drawing as it is laid - and the environment's file with it");
    // A BRANCH on the bundle, between the point laid and X2: two bundles.
    pane.setRouteTool(RouteTool::Branch);
    const std::vector<V3> c = route.corners(route.segments[0]);
    click((c[2] + c[3]) * 0.5);
    check(route.segments.size() == 2 && route.nodes.size() == 3 && route.nodes[2].kind == RouteNodeKind::Branch &&
              route.degree(route.nodes[2].id) == 2,
          "3d route: a branch put on the bundle divides it there");
    double sum = 0.0;
    for (const RouteSegment3D& g : route.segments) sum += route.length(g);
    std::printf("  3d route: divided, %.1f mm in all (was %.1f)\n", sum, len);
    check(std::abs(sum - len) < len * 0.08, "3d route: and the two halves are as long as the whole was, near enough (the corners redone)");
    // Taken back; X2 deleted, its bundle with it.
    pane.undoRoute();
    check(route.segments.size() == 1 && route.nodes.size() == 2, "3d route: undone - the branch taken back");
    pane.setRouteTool(RouteTool::Delete);
    click(route.nodes[1].at + route.nodes[1].normal * (route.lead * 0.5));
    check(route.nodes.size() == 1 && route.segments.empty(), "3d route: a connector deleted, its bundle with it");
    pane.undoRoute();
    // Esc gives up a bundle being laid.
    pane.setRouteTool(RouteTool::Route);
    click(route.nodes[0].at + route.nodes[0].normal * (route.lead * 0.5));
    click({15, 20, 0});
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(canvas, &esc);
    check(!canvas->layingRoute() && route.segments.size() == 1, "3d route: Esc gives up the bundle being laid");
    // READ BACK: another view of the same drawing - the route, and the environment it names, read again.
    View3DPane again;
    again.setRoutingStore([&] { return stored; }, [&](const QJsonObject& o) { stored = o; }, [&] { return dir; });
    const int before = saves;
    again.reloadRouting(true);
    check(again.route().toJson() == pane.route().toJson() && !again.environment().empty(),
          "3d route: read back from the drawing - the route, and its environment read again");
    check(saves == before, "3d route: reading it back does not count as a change to the drawing");
    // The environment moved along with the drawing: found beside it.
    QJsonObject moved = stored;
    QJsonObject envRef = moved.value(QStringLiteral("environment")).toObject();
    envRef[QStringLiteral("path")] = QStringLiteral("Z:/gone/elsewhere/routing.step");
    moved[QStringLiteral("environment")] = envRef;
    View3DPane third;
    third.setRoutingStore([&] { return moved; }, [&](const QJsonObject&) {}, [&] { return dir; });
    third.reloadRouting(true);
    check(!third.environment().empty(), "3d route: an environment not where it was - found beside the drawing");
#endif
}

// ROUND FORTY-FOUR: THE 3D ROUTE AND THE HARNESS - each harness part put at a node of the route, from the list of its
// parts, and each cable given its length along the route between the parts it joins: the longest where it goes to
// several at one end, the shorter ones their own.
void testRoundFortyFour() {
    // ---- THE LENGTHS, from the route to the design ----
    BrickDesign d;
    const QString x = d.addBrick(BrickType::End, PartKind::Connector, 0, 0);
    const QString c = d.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
    const QString y1 = d.addBrick(BrickType::End, PartKind::Connector, 2, 0);
    const QString y2 = d.addBrick(BrickType::End, PartKind::Connector, 2, 1);
    d.brick(x)->name = QStringLiteral("X1");
    d.brick(y1)->name = QStringLiteral("X2");
    d.brick(y2)->name = QStringLiteral("X3");
    d.brick(c)->name = QStringLiteral("W1");
    d.connect(x, c);
    d.connect(c, y1);
    check(d.finishesOf(c).size() == 1, "route link: a cable from X1 to X2");
    Route3D r;
    r.diameter = 6.0;
    r.lead = 10.0;
    auto put = [&](const QString& brick, const V3& at) {
        const QString id = r.addNode(RouteNodeKind::Connector, at, {0, 0, 1});
        r.node(id)->brick = brick;
        r.node(id)->part = d.brick(brick)->name;
        return id;
    };
    const QString nx = put(x, {0, 0, 0});
    const QString ny1 = put(y1, {400, 0, 0});
    const QString b = r.addNode(RouteNodeKind::Branch, {300, 0, 0}, {0, 0, 1});
    r.addSegment(nx, b, {{{100, 0, 0}, {0, 0, 1}}});
    r.addSegment(b, ny1, {});
    const double whole = routeDistance(r, nx, ny1);
    check(std::abs(whole - (r.length(r.segments[0]) + r.length(r.segments[1]))) < 1e-9, "route link: X1 to X2 along the bundles, through the branch");
    RouteLengths lengths = routeLengthsFor(d, r);
    check(lengths.measured == 1 && lengths.changes.size() == 1 && lengths.changes[0].part.isEmpty() &&
              std::abs(lengths.changes[0].now - std::round(whole)) < 1e-9,
          "route link: the cable's length is its way along the route, to the millimetre");
    applyRouteLengths(d, lengths);
    check(std::abs(d.brick(c)->run.lengthMm - std::round(whole)) < 1e-9 && routeLengthsFor(d, r).changes.empty(),
          "route link: given to the design - and asked again, nothing more to change");
    // TO TWO PARTS AT ONE END: the longer is the cable's length, the shorter that branch's own.
    d.connect(c, y2);
    const QString ny2 = put(y2, {300, 120, 0});
    r.addSegment(b, ny2, {});
    lengths = routeLengthsFor(d, r);
    applyRouteLengths(d, lengths);
    const double toX3 = routeDistance(r, nx, ny2);
    std::printf("  route link: W1 %.0f mm; own lengths X2 %.0f, X3 %.0f (route %.1f / %.1f)\n", d.brick(c)->run.lengthMm,
                d.link(c, y1)->lengthMm, d.link(c, y2)->lengthMm, whole, toX3);
    // Which is the longer is as the route has it - the connectors' leads count too.
    const QString longer = toX3 > whole ? y2 : y1, shorter = toX3 > whole ? y1 : y2;
    check(std::abs(d.brick(c)->run.lengthMm - std::round(std::max(whole, toX3))) < 1e-9 &&
              std::abs(d.link(c, shorter)->lengthMm - std::round(std::min(whole, toX3))) < 1e-9 && d.link(c, longer)->lengthMm == 0.0,
          "route link: to two parts at an end - the longer the cable's length, the shorter its own branch length");
    check(std::abs(runLengthTo(d, *d.brick(c), shorter) - std::round(std::min(whole, toX3))) < 1e-9 &&
              std::abs(runLengthTo(d, *d.brick(c), longer) - std::round(std::max(whole, toX3))) < 1e-9,
          "route link: and the drawing reads each branch's length");
    // A part not put in 3D: the cable left alone, and said why.
    r.removeNode(ny2);
    lengths = routeLengthsFor(d, r);
    check(lengths.changes.empty() && lengths.notes.join(QLatin1Char(' ')).contains(QStringLiteral("X3 not put in 3D")),
          "route link: a cable with a part not put in 3D - left as it is, and why");
    check(Route3D::fromJson(r.toJson()).node(nx)->brick == x, "route link: a node's part saved with the route");

    // ---- IN THE 3D VIEW: the parts put from the list, the bundle laid, the lengths given ----
    BrickDesign design;
    const QString px = design.addBrick(BrickType::End, PartKind::Connector, 0, 0);
    const QString pc = design.addBrick(BrickType::Run, PartKind::Cable, 1, 0);
    const QString py = design.addBrick(BrickType::End, PartKind::Connector, 2, 0);
    design.brick(px)->name = QStringLiteral("P1");
    design.brick(py)->name = QStringLiteral("P2");
    design.connect(px, pc);
    design.connect(pc, py);
    BrickDesign given;
    QString givenName;
    QJsonObject stored;
    View3DPane pane;
    pane.setRoutingStore([&] { return stored; }, [&](const QJsonObject& o) { stored = o; });
    pane.setHarnessLink(
        [&] {
            RouteHarness h;
            h.name = QStringLiteral("Main");
            h.design = design;
            h.ok = true;
            return h;
        },
        [&](const QString& name, const BrickDesign& out) {
            givenName = name;
            given = out;
            design = out;
        });
    pane.resize(1000, 700);
    pane.show();
    QApplication::processEvents();
    View3DCanvas* canvas = pane.canvas();
    canvas->setUseGpu(false);
    canvas->viewFrom(QStringLiteral("top"));
    auto click = [&](const V3& p) {
        QPointF at;
        canvas->toScreen(p, at);
        QMouseEvent press(QEvent::MouseButtonPress, at, canvas->mapToGlobal(at), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, at, canvas->mapToGlobal(at), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &press);
        QApplication::sendEvent(canvas, &release);
    };
    auto* parts = pane.findChild<QListWidget*>(QStringLiteral("view3dParts"));
    check(parts && parts->count() == 2 && parts->item(0)->text().contains(QStringLiteral("not put in 3D")),
          "3d link: the harness's parts listed, none put in 3D yet");
    parts->setCurrentRow(0);
    pane.setRouteTool(RouteTool::Connector);
    click({-30, 0, 0});
    const Route3D& route = pane.route();
    check(route.nodes.size() == 1 && route.nodes[0].brick == px && route.nodes[0].label == QLatin1String("P1"),
          "3d link: the part chosen put where the Connector tool clicked, by its designator");
    check(parts->currentRow() == 1, "3d link: and the next part not put yet chosen for the next click");
    click({30, 0, 0});
    check(route.nodes.size() == 2 && route.nodes[1].brick == py, "3d link: P2 put");
    // Put again: moved, not doubled.
    parts->setCurrentRow(1);
    click({35, 0, 0});
    check(route.nodes.size() == 2 && std::abs(route.nodes[1].at.x - 35.0) < 0.5, "3d link: a part put again is moved there, not put twice");
    pane.setRouteTool(RouteTool::Route);
    click(route.nodes[0].at + route.nodes[0].normal * (route.lead * 0.5));
    click({0, 25, 0});
    click(route.nodes[1].at + route.nodes[1].normal * (route.lead * 0.5));
    check(route.segments.size() == 1, "3d link: a bundle laid from P1 to P2");
    const double along = route.length(route.segments[0]);
    const RouteLengths proposed = pane.proposedLengths();
    check(proposed.changes.size() == 1 && std::abs(proposed.changes[0].now - std::round(along)) < 1e-9,
          "3d link: the length the route would give the cable");
    check(pane.applyLengths(false) && givenName == QLatin1String("Main") &&
              std::abs(given.brick(pc)->run.lengthMm - std::round(along)) < 1e-9,
          "3d link: given to the harness - its cable now as long as the route");
    check(!pane.applyLengths(false), "3d link: given again - nothing left to change");
    // Unlinked: the node put for no part.
    pane.setRouteTool(RouteTool::Select);
    click(route.nodes[1].at + route.nodes[1].normal * (route.lead * 0.5));
    check(canvas->selectedNode() == route.nodes[1].id && pane.linkSelectedNode(QString()) && route.nodes[1].brick.isEmpty() &&
              parts->item(1)->text().contains(QStringLiteral("not put in 3D")),
          "3d link: a node unlinked - its part listed as not put in 3D again");
}

int run_brickharness_tests() {
    std::printf("Running brick harness tests...\n");
    fails = 0;
    testLinks();
    testJson();
    testRoutes();
    testArrange();
    testBuild();
    testFanoutJointAndExtras();
    testPortsPerFace();
    testDialog();
    testDialogMore();
    testRoundThree();
    testRoundFour();
    testRoundFive();
    testRoundSix();
    testRoundSeven();
    testPartSizeByEntry();
    testConnectPointFacesBrick();
    testLengthsAndBridges();
    testRoundEight();
    testRoundNine();
    testRoundTen();
    testRoundEleven();
    testRoundTwelve();
    testRoundThirteen();
    testConnectorContactsOnBill();
    testRoundFourteen();
    testRoundFifteen();
    testRoundSixteen();
    testRoundSeventeen();
    testRoundEighteen();
    testRoundNineteen();
    testRoundTwenty();
    testRoundTwentyOne();
    testRoundTwentyTwo();
    testRoundTwentyThree();
    testRoundTwentyFour();
    testRoundTwentyFive();
    testRoundTwentySix();
    testRoundTwentySeven();
    testRoundTwentyEight();
    testRoundTwentyNine();
    testRoundThirty();
    testRoundThirtyOne();
    testRoundThirtyTwo();
    testRoundThirtyThree();
    testRoundThirtyFour();
    testRoundThirtyFive();
    testRoundThirtySix();
    testRoundThirtySeven();
    testRoundThirtyEight();
    testRoundThirtyNine();
    testRoundForty();
    testRoundFortyOne();
    testRoundFortyTwo();
    testRoundFortyThree();
    testRoundFortyFour();
    if (fails == 0) std::printf("  brick harness: OK\n");
    return fails;
}
