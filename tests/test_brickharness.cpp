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
#include "harness/Bom.h"
#include "io/Dwx.h"
#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QLineEdit>

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
    for (const Entity* en : runs) {
        if (en->meta.contains(QStringLiteral("twistBody"))) trunk = en;
        else outWires += en->meta.value(QStringLiteral("twistWires")).toInt();
    }
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
        check(!lin.empty() && !ang.empty() && std::abs(lin[0].b.y - lin[0].a.y) < 1e-6 &&
                  std::abs(ang[0].b.y - ang[0].a.y) > 1.0,
              "dimension: linear along the harness; not linear, at the angle between its points");
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
    auto* foldDims = dlg.findChild<QToolButton*>(QStringLiteral("brickFoldDims"));
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
    check(ends >= 5 && specials == 5, "wizard: the palette holds the end parts, the four specials and the bridge wire");

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
        std::printf("  symmetric: drawn at %.1f and %.1f\n", yf, yx);
        check(std::abs(yf - yx) < 1e-6, "wizard: and on the drawing a single run is laid straight along the axis");
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
        check(xl > 0.0 && std::abs(fl - xl) < 0.02 * xl, "match: its entry now as long as the other's");
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
    check(bootOf(body) == -1.0, "transition: left alone, the drawing sizes it");
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
        check(sym && sym->text() == QStringLiteral("Symmetric"), "wizard: a Symmetric button");
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
    // A NOMINAL PART IS NOT MEASURED THROUGH: dimensioned "from far point", it is measured from its face.
    {
        d.brick(r)->run.dimTotal = true;
        d.dimFromFar = true;
        auto said = [&] {
            for (const Entity& e : buildBrickHarness(d, &cat).entities)
                if (e.meta.value(QStringLiteral("brickDim")).toString() == QStringLiteral("total"))
                    return e.meta.value(QStringLiteral("brickMeasured")).toDouble();
            return -1.0;
        };
        const double nominal = said();
        setPartSizeMode(p, PartSizeMode::AsDrawn);
        d.shapeCache.clear();
        const double real = said();
        std::printf("  size: from far point %.1f on the nominal part, %.1f on the real one\n", nominal, real);
        check(nominal > 0.0 && real > nominal + 1.0, "size: the far point of a nominal part is not measured to");
        d.dimFromFar = false;
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
        check(pick && pick->isVisibleTo(&dlg) && pick->count() == 4 && pick->itemText(1).contains(QStringLiteral("GND")) &&
                  pick->itemData(3).toString() == QString::fromLatin1(kBridgeShield),
              "bridge pick: the cable's wires by net, and its shield");
        if (pick) {
            pick->setCurrentIndex(3);
            emit pick->activated(3);
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
        d.brick(x0)->dimBelow = true;
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
        check(dlg.preview()->zoom() > z0 * 1.5, "double-click: the brick's part brought up on the preview");
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
        check(std::abs(mirrored1.out.x - plain1.out.x) < 1e-6 && std::abs(mirrored1.out.y + plain1.out.y) < 1e-6,
              "on the drawing: mirrored, top for bottom");
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
        press(Qt::Key_R);
        check(std::abs(w.brick(x)->drawTurnDeg - 45.0) < 1e-9 && w.brick(x)->rot == 0,
              "keys: R turns the part 45 degrees on the drawing, the brick not turned");
        press(Qt::Key_Space);
        check(std::abs(w.brick(x)->drawTurnDeg - 135.0) < 1e-9, "keys: Space turns it 90 more");
        press(Qt::Key_M);
        check(w.brick(x)->drawMirror, "keys: M mirrors it");
    }
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
    if (fails == 0) std::printf("  brick harness: OK\n");
    return fails;
}
