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
#include "harness/Bom.h"
#include "io/Dwx.h"
#include <QDir>
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
    check(!d.connect(run, run2, &why), "links: nor two specials");
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
        if (e.meta.value(QStringLiteral("brickId")).toString() == fan &&
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
    s.labels.push_back({1, true, 5.0, QStringLiteral("2")});
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
    check(dims == 4, "dimensions: the whole run, each fan, and between the fans");
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
        if (en.type == EntityType::Text) texts.push_back(&en);
        else ++modelParts;
    }
    QStringList said;
    for (const Entity* t : texts) said << t->meta.value(QStringLiteral("text")).toString();
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
            if (t->meta.value(QStringLiteral("text")).toString() == QLatin1String("GND")) gnd = t;
            if (t->meta.value(QStringLiteral("text")).toString() == QLatin1String("L3")) l3 = t;
        }
        check(gnd && l3 && onAWire(gnd->a) < 1e-6 && onAWire(l3->a) < 1e-6,
              "labels: a label on a wire is centred on that wire");
        check(gnd && l3 && gnd->a.y > l3->a.y, "labels: the wires are counted from the top down");
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
    auto* text = labels ? qobject_cast<QLineEdit*>(labels->cellWidget(0, 3)) : nullptr;
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

void mouse(QWidget* w, QEvent::Type t, const QPointF& at, Qt::KeyboardModifiers mods = Qt::NoModifier) {
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
    check(ends >= 5 && specials == 4, "wizard: the palette holds the end parts and the four specials");

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
    const QString f1 = dlg.addFromPalette(QStringLiteral("end|ferrule"));
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
    const QPointF mid = canvas->brickRect(*fb).center();
    mouse(canvas, QEvent::MouseButtonPress, mid);
    mouse(canvas, QEvent::MouseMove, mid + QPointF(0, 58.0));
    mouse(canvas, QEvent::MouseButtonRelease, mid + QPointF(0, 58.0));
    check(near(dlg.design().brick(f1)->col, wasCol) && near(dlg.design().brick(f1)->row, 1.0),
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
    dlg.arrange();
    check(near(dlg.design().brick(f1)->row, dlg.design().brick(x1)->row) &&
              dlg.design().brick(f1)->col > dlg.design().brick(x1)->col,
          "wizard: arranged, a single run is laid straight along the axis");

    // PLACED: onto the editor's cursor, the window out of the way.
    dlg.place();
    check(ed.placingPaste() && !dlg.isVisible(), "wizard: placing hands the drawing to the cursor");
    ed.cancelPastePlacement();

    // A link picked and deleted.
    dlg.selectBrick(QString());
    const Brick* ra = dlg.design().brick(run);
    const Brick* rb = dlg.design().brick(f1);
    mouse(canvas, QEvent::MouseButtonPress,
          (canvas->brickRect(*ra).center() + canvas->brickRect(*rb).center()) / 2.0);
    mouse(canvas, QEvent::MouseButtonRelease,
          (canvas->brickRect(*ra).center() + canvas->brickRect(*rb).center()) / 2.0);
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
    canvas->fit();
    canvas->update();
    QApplication::processEvents();

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
    dlg.arrange();   // and arranging turns each to face what it is linked to again
    check(d.brick(f1)->rot == f1Rot && d.brick(fan)->rot == fanRot, "arrange: turns them back to face their links");

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
        if (e.type == EntityType::TwistedPair && e.meta.value(QStringLiteral("brickId")).toString() == fan)
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
        check(asked == 1 && !again, "bom: already there, it asks - and a no adds nothing");
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
    check(stack && stack->count() == 3 && stack->item(0)->text().contains(QStringLiteral("Heatshrink")),
          "stack: listed from the outside in, the one seen first");
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
    if (fails == 0) std::printf("  brick harness: OK\n");
    return fails;
}
