#include "app/Editor.h"
#include "geometry/Geometry.h"
#include "io/Dwx.h"
#include "geometry/Snap.h"
#include "model/Document.h"
#include "app/ribbon/RibbonIcons.h"
#include "model/PartSymbols.h"
#include "render/EntityRenderer.h"
#include "tools/EntityTransforms.h"
#include "tools/ModifyOps.h"
#include "tools/QuickSelect.h"

#include <QImage>
#include <QPointF>
#include <QJsonArray>
#include <QPainter>
#include <QtMath>
#include <cmath>
#include <cstdio>

using namespace Edraw_Desktop;

namespace {
int g_fail = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("  [FAIL] %s\n", what); ++g_fail; }
}
bool near(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }

// Disable object snapping so test coordinates are exact.
void noSnap(Editor& ed) {
    auto& s = ed.snapSettings();
    s.objectSnap = false;
    s.gridSnap = false;
    s.ortho = false;
    s.polar = false;
}
} // namespace

int run_editor_tests() {
    g_fail = 0;
    std::printf("editor tests:\n");

    // --- Draw a line via two presses.
    {
        Document doc;
        doc.entities().clear();
        Editor ed;
        ed.setDocument(&doc);
        noSnap(ed);
        ed.setTool(ToolId::Line);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({10, 0}, Qt::LeftButton, Qt::NoModifier);
        check(doc.entities().size() == 1, "line: one entity");
        check(doc.entities()[0].type == EntityType::Line, "line: type");
        check(near(doc.entities()[0].b.x, 10.0), "line: endpoint");
    }

    // --- Rectangle from two corners -> 4 points.
    {
        Document doc;
        doc.entities().clear();
        Editor ed;
        ed.setDocument(&doc);
        noSnap(ed);
        ed.setTool(ToolId::Rect);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({40, 20}, Qt::LeftButton, Qt::NoModifier);
        check(doc.entities().size() == 1, "rect: one entity");
        check(doc.entities()[0].points.size() == 4, "rect: 4 corners");
    }

    // --- Circle center+radius.
    {
        Document doc;
        doc.entities().clear();
        Editor ed;
        ed.setDocument(&doc);
        noSnap(ed);
        ed.setTool(ToolId::Circle);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({5, 0}, Qt::LeftButton, Qt::NoModifier);
        check(doc.entities().size() == 1 && near(doc.entities()[0].radius, 5.0),
              "circle: radius");
    }

    // --- Move selection by delta + undo restores.
    {
        Document doc;
        doc.entities().clear();
        Editor ed;
        ed.setDocument(&doc);
        noSnap(ed);
        ed.setTool(ToolId::Line);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({10, 0}, Qt::LeftButton, Qt::NoModifier);
        const QString id = doc.entities()[0].id;

        ed.selectAll();
        ed.setTool(ToolId::Move);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({0, 5}, Qt::LeftButton, Qt::NoModifier);
        check(near(doc.entities()[0].a.y, 5.0), "move: translated");

        ed.undo();
        check(near(doc.entities()[0].a.y, 0.0), "move: undo restores");
        (void)id;
    }

    // --- Rotate 90 degrees about origin.
    {
        Entity e;
        e.type = EntityType::Line;
        e.a = {0, 0};
        e.b = {10, 0};
        rotateEntity(e, {0, 0}, M_PI / 2.0);
        check(near(e.b.x, 0.0, 1e-9) && near(e.b.y, 10.0, 1e-9), "rotate: 90deg");
    }

    // --- Mirror across the Y axis.
    {
        Entity e;
        e.type = EntityType::Line;
        e.a = {2, 3};
        e.b = {5, 7};
        mirrorEntity(e, {0, 0}, {0, 1});
        check(near(e.a.x, -2.0) && near(e.b.x, -5.0), "mirror: across Y");
    }

    // --- Mirror tool: after picking the line it waits for a Copy/Move/Cancel choice.
    {
        Document doc; doc.entities().clear();
        Editor ed; ed.setDocument(&doc); noSnap(ed);
        ed.setTool(ToolId::Line);
        ed.onPointerPress({2, 3}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({5, 7}, Qt::LeftButton, Qt::NoModifier);
        ed.selectAll();
        // Pick a vertical mirror line (x=0): the second click must NOT apply yet.
        ed.setTool(ToolId::Mirror);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({0, 1}, Qt::LeftButton, Qt::NoModifier);
        check(doc.entities().size() == 1 && near(doc.entities()[0].a.x, 2.0),
              "mirror menu: second click defers — original untouched until a choice");
        // Copy: original stays, a mirrored copy is added.
        ed.commitMirror(true);
        check(doc.entities().size() == 2, "mirror menu: Copy adds a mirrored duplicate");
        const Entity& orig = doc.entities()[0];
        const Entity& copy = doc.entities()[1];
        check(near(orig.a.x, 2.0) && near(copy.a.x, -2.0) && near(copy.b.x, -5.0),
              "mirror menu: Copy keeps the original and mirrors the copy");

        // Move: mirror the selection in place (no new entity).
        Document doc2; doc2.entities().clear();
        Editor ed2; ed2.setDocument(&doc2); noSnap(ed2);
        ed2.setTool(ToolId::Line);
        ed2.onPointerPress({2, 3}, Qt::LeftButton, Qt::NoModifier);
        ed2.onPointerPress({5, 7}, Qt::LeftButton, Qt::NoModifier);
        ed2.selectAll();
        ed2.setTool(ToolId::Mirror);
        ed2.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed2.onPointerPress({0, 1}, Qt::LeftButton, Qt::NoModifier);
        ed2.commitMirror(false);
        check(doc2.entities().size() == 1 && near(doc2.entities()[0].a.x, -2.0),
              "mirror menu: Move mirrors in place (no duplicate)");

        // Cancel: nothing changes.
        Document doc3; doc3.entities().clear();
        Editor ed3; ed3.setDocument(&doc3); noSnap(ed3);
        ed3.setTool(ToolId::Line);
        ed3.onPointerPress({2, 3}, Qt::LeftButton, Qt::NoModifier);
        ed3.onPointerPress({5, 7}, Qt::LeftButton, Qt::NoModifier);
        ed3.selectAll();
        ed3.setTool(ToolId::Mirror);
        ed3.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed3.onPointerPress({0, 1}, Qt::LeftButton, Qt::NoModifier);
        ed3.cancelMirror();
        check(doc3.entities().size() == 1 && near(doc3.entities()[0].a.x, 2.0),
              "mirror menu: Cancel leaves everything unchanged");
    }

    // --- Shift + drop a double line's end onto another line caps both runs to it.
    {
        Document d; d.entities().clear();
        // Target: a vertical line at x=0 spanning the drop point.
        Entity t; t.id = "T"; t.type = EntityType::Line; t.a = {0, -50}; t.b = {0, 50};
        d.entities().push_back(t);
        // Double line whose A end we'll drag to the target.
        Entity dl; dl.id = "DL"; dl.type = EntityType::DoubleLine;
        dl.a = {50, 0}; dl.b = {150, 0}; dl.meta.insert("gap", 10.0);
        d.entities().push_back(dl);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setTool(ToolId::Select);
        ed.selectById("DL");
        // Grab the A-end visible corner grip (a + perp*gap/2 = (50,5)), no shift.
        ed.onPointerPress({50, 5}, Qt::LeftButton, Qt::NoModifier);
        // Place it on the target line WITH shift held -> cap both runs to the target.
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::ShiftModifier);
        const Entity* dd = nullptr;
        for (const auto& e : d.entities()) if (e.id == "DL") dd = &e;
        check(dd && dd->meta.value("cutStart").toBool() &&
                  dd->meta.value("cutStartKind").toString() == QLatin1String("line"),
              "double-connect: shift-drop caps the moved end to the target line");

        // Without shift, the same drop moves the end but adds no cap.
        Document d2; d2.entities().clear();
        Entity t2 = t; d2.entities().push_back(t2);
        Entity dl2 = dl; dl2.meta = QJsonObject(); dl2.meta.insert("gap", 10.0);
        d2.entities().push_back(dl2);
        Editor ed2; ed2.setDocument(&d2); noSnap(ed2);
        ed2.setTool(ToolId::Select);
        ed2.selectById("DL");
        ed2.onPointerPress({50, 5}, Qt::LeftButton, Qt::NoModifier);
        ed2.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        const Entity* dd2 = nullptr;
        for (const auto& e : d2.entities()) if (e.id == "DL") dd2 = &e;
        check(dd2 && !dd2->meta.value("cutStart").toBool(),
              "double-connect: without shift the end moves but is not capped");
    }

    // --- Drawing a double line: shift-click the end onto a line caps that run-end.
    {
        Document d; d.entities().clear();
        Entity t; t.id = "T"; t.type = EntityType::Line; t.a = {100, -50}; t.b = {100, 50};
        d.entities().push_back(t);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setTool(ToolId::DoubleLine);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);          // start (no cap)
        ed.onPointerPress({100, 0}, Qt::LeftButton, Qt::ShiftModifier);     // end on T, shift -> cap
        const Entity* dl = nullptr;
        for (const auto& e : d.entities())
            if (e.type == EntityType::DoubleLine) dl = &e;
        check(dl && !dl->meta.value("cutStart").toBool() && dl->meta.value("cutEnd").toBool() &&
                  dl->meta.value("cutEndKind").toString() == QLatin1String("line"),
              "double-draw: shift-click end onto a line caps that run-end only");
    }

    // --- Delete removes selection.
    {
        Document doc;
        doc.entities().clear();
        Editor ed;
        ed.setDocument(&doc);
        noSnap(ed);
        ed.setTool(ToolId::Circle);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({3, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.selectAll();
        ed.deleteSelected();
        check(doc.entities().empty(), "delete: emptied");
        ed.undo();
        check(doc.entities().size() == 1, "delete: undo restores");
    }

    // --- Fillet by clicking two lines, redefining the radius with "r" mid-command.
    {
        Document d; d.entities().clear();
        Entity h; h.id = "h"; h.type = EntityType::Line; h.a = {0, 0}; h.b = {100, 0};
        Entity v; v.id = "v"; v.type = EntityType::Line; v.a = {0, 0}; v.b = {0, 100};
        d.entities().push_back(h);
        d.entities().push_back(v);
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(3);
        ed.setTool(ToolId::Fillet);
        ed.onPointerPress({60, 0}, Qt::LeftButton, Qt::NoModifier); // click first line
        check(ed.acceptCommandInput(QStringLiteral("r")), "fillet-r: 'r' enters radius mode mid-command");
        check(ed.acceptCommandInput(QStringLiteral("8")), "fillet-r: radius value accepted");
        ed.onPointerPress({0, 60}, Qt::LeftButton, Qt::NoModifier); // click second line -> apply
        bool arc8 = false;
        for (const auto& e : d.entities())
            if (e.type == EntityType::Arc && near(e.radius, 8.0)) arc8 = true;
        check(arc8, "fillet-r: fillet applied with the redefined radius (8)");
    }

    // --- Fillet a POLYLINE knee: pick the two segments that share it -> that vertex
    //     gets a per-corner radius.
    {
        Document d; d.entities().clear();
        Entity pl; pl.id = "p"; pl.type = EntityType::Polyline;
        pl.points = {{0, 0}, {100, 0}, {100, 100}}; // knee at vertex 1
        d.entities().push_back(pl);
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(3);
        ed.setFilletRadius(5.0);
        ed.setTool(ToolId::Fillet);
        ed.onPointerPress({50, 0}, Qt::LeftButton, Qt::NoModifier);   // segment 0
        ed.onPointerPress({100, 50}, Qt::LeftButton, Qt::NoModifier); // segment 1 -> fillet knee
        const Entity* pp = nullptr;
        for (const auto& e : d.entities()) if (e.id == "p") pp = &e;
        check(pp && near(pp->meta.value("cornerRadii").toArray()[1].toDouble(), 5.0),
              "fillet-knee: the shared vertex gets the fillet radius");
    }

    // --- Fillet a POLYGON corner (closed shape, wrap-around): pick the two edges
    //     that share the corner.
    {
        Document d; d.entities().clear();
        Entity pg; pg.id = "pg"; pg.type = EntityType::Polygon;
        pg.points = {{0, 0}, {100, 0}, {100, 100}, {0, 100}}; // square; corner 0 = (0,0)
        d.entities().push_back(pg);
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(3);
        ed.setFilletRadius(5.0);
        ed.setTool(ToolId::Fillet);
        ed.onPointerPress({50, 0}, Qt::LeftButton, Qt::NoModifier); // bottom edge (edge 0)
        ed.onPointerPress({0, 50}, Qt::LeftButton, Qt::NoModifier); // left edge (edge 3) -> corner 0
        const Entity* gg = nullptr;
        for (const auto& e : d.entities()) if (e.id == "pg") gg = &e;
        check(gg && near(gg->meta.value("cornerRadii").toArray()[0].toDouble(), 5.0),
              "fillet-polygon: the shared corner (wrap-around) gets the fillet radius");
    }

    // --- A DOUBLE polyline is pickable by clicking a PHYSICAL run, and its knee
    //     fillets (fixes "can't select the double polyline lines").
    {
        Document d; d.entities().clear();
        Entity dp; dp.id = "dp"; dp.type = EntityType::Polyline;
        dp.points = {{0, 0}, {100, 0}, {100, 100}};
        dp.meta.insert("double", true);
        dp.meta.insert("gap", 10.0); // runs offset +/-5
        d.entities().push_back(dp);
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(3);
        ed.setFilletRadius(5.0);
        ed.setTool(ToolId::Fillet);
        ed.onPointerPress({50, 5}, Qt::LeftButton, Qt::NoModifier);   // on seg0's +run
        ed.onPointerPress({105, 50}, Qt::LeftButton, Qt::NoModifier); // on seg1's +run -> knee
        const Entity* dd = nullptr;
        for (const auto& e : d.entities()) if (e.id == "dp") dd = &e;
        check(dd && near(dd->meta.value("cornerRadii").toArray()[1].toDouble(), 5.0),
              "fillet-dpoly: double polyline picked on a physical run, knee filleted");
    }

    // --- Fillet TWO connected DOUBLE LINES: they merge into one filleted double
    //     polyline at the shared connection.
    {
        Document d; d.entities().clear();
        Entity a; a.id = "a"; a.type = EntityType::DoubleLine; a.a = {0, 0}; a.b = {100, 0};
        a.meta.insert("gap", 10.0); // runs y=+/-5
        Entity b; b.id = "b"; b.type = EntityType::DoubleLine; b.a = {100, 0}; b.b = {100, 100};
        b.meta.insert("gap", 10.0); // runs x=95/105; shares (100,0)
        d.entities().push_back(a);
        d.entities().push_back(b);
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(3);
        ed.setFilletRadius(5.0);
        ed.setTool(ToolId::Fillet);
        ed.onPointerPress({50, 5}, Qt::LeftButton, Qt::NoModifier);   // pick line a (physical run)
        ed.onPointerPress({105, 50}, Qt::LeftButton, Qt::NoModifier); // pick line b -> merge + fillet
        int dpolys = 0, doubleLines = 0;
        const Entity* merged = nullptr;
        for (const auto& e : d.entities()) {
            if (e.type == EntityType::DoubleLine) ++doubleLines;
            if (e.type == EntityType::Polyline && e.meta.value("double").toBool()) { ++dpolys; merged = &e; }
        }
        check(doubleLines == 0 && dpolys == 1 && merged && merged->points.size() == 3 &&
                  near(merged->meta.value("cornerRadii").toArray()[1].toDouble(), 5.0),
              "fillet-2double: connected double lines merge into a filleted double polyline");
    }

    // --- A SINGLE click AT A KNEE of a double polyline rounds that knee, and clicking it again with a
    //     new radius redefines it. A click out along an EDGE does not: that click is the first of a pair,
    //     so a double polyline can be rounded to another run as well (it could not before, because any
    //     click on one was swallowed by this shortcut and rounded a knee somewhere else).
    {
        Document d; d.entities().clear();
        Entity dp; dp.id = "dp"; dp.type = EntityType::Polyline;
        dp.points = {{0, 0}, {100, 0}, {100, 100}};
        dp.meta.insert("double", true);
        dp.meta.insert("gap", 10.0);
        d.entities().push_back(dp);
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(3);
        ed.setFilletRadius(7.0);
        ed.setTool(ToolId::Fillet);
        auto radiusAt = [&](int knee) {
            for (const auto& e : d.entities())
                if (e.id == QLatin1String("dp")) {
                    const QJsonArray arr = e.meta.value("cornerRadii").toArray();
                    return knee < arr.size() ? arr[knee].toDouble() : -1.0;
                }
            return -1.0;
        };
        // On the rail, right at the knee: the rails mitre at the corner, so the nearest point of the
        // drawing to the knee (100,0) is (95,5).
        ed.onPointerPress({95, 5}, Qt::LeftButton, Qt::NoModifier);
        const bool rounded = near(radiusAt(1), 7.0);
        ed.setFilletRadius(3.0);
        ed.onPointerPress({95, 5}, Qt::LeftButton, Qt::NoModifier);   // again -> redefined
        check(rounded && near(radiusAt(1), 3.0),
              "fillet-dpoly-1click: one click at a knee of a double polyline rounds it, and again "
              "redefines it");
        // And a click out along an edge is a first pick, not a knee edit.
        Document d2; d2.entities().clear();
        Entity dp2 = dp;
        dp2.meta.remove("cornerRadii");
        d2.entities().push_back(dp2);
        Editor e2; e2.setDocument(&d2); noSnap(e2); e2.setPixelTolerance(3);
        e2.setFilletRadius(5.0);
        e2.setTool(ToolId::Fillet);
        e2.onPointerPress({30, 5}, Qt::LeftButton, Qt::NoModifier);    // far from the knee
        const QJsonArray after = d2.entities().front().meta.value("cornerRadii").toArray();
        check(after.isEmpty(),
              "fillet-dpoly-1click: a click out along an edge rounds nothing - it is the first of a "
              "pair");
    }

    // --- Chamfer a CLOSED figure (rectangle) BY THE TWO CLICKED POINTS: the setback
    //     on each wall is that click's distance from the shared corner (asymmetric).
    {
        Document d; d.entities().clear();
        Entity r; r.id = "r"; r.type = EntityType::Rect;
        r.points = {{0, 0}, {100, 0}, {100, 60}, {0, 60}};
        d.entities().push_back(r);
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(3);
        ed.setTool(ToolId::Chamfer);
        ed.onPointerPress({50, 0}, Qt::LeftButton, Qt::NoModifier);   // bottom wall, 50 from corner
        ed.onPointerPress({100, 30}, Qt::LeftButton, Qt::NoModifier); // right wall, 30 from corner (100,0)
        const Entity* rr = nullptr;
        for (const auto& e : d.entities()) if (e.id == "r") rr = &e;
        const QJsonArray in = rr ? rr->meta.value("cornerChamfers").toArray() : QJsonArray{};
        const QJsonArray out = rr ? rr->meta.value("cornerChamfers2").toArray() : QJsonArray{};
        check(rr && near(in[1].toDouble(), 50.0) && near(out[1].toDouble(), 30.0),
              "chamfer-rect-points: rectangle corner chamfers exactly between the clicked points");
        std::vector<double> radii(4, 0.0), cin(4, 0.0), cout(4, 0.0);
        for (int i = 0; i < in.size(); ++i) cin[i] = in[i].toDouble();
        for (int i = 0; i < out.size(); ++i) cout[i] = out[i].toDouble();
        const auto outline = filletChamferPolyline(rr->points, radii, cin, cout, true);
        check(outline.size() == rr->points.size() + 1,
              "chamfer-rect-points: chamfered rectangle outline has one extra vertex");
    }

    // --- Chamfer a POLYGON corner likewise (symmetric clicks -> equal setbacks).
    {
        Document d; d.entities().clear();
        Entity g; g.id = "g"; g.type = EntityType::Polygon;
        g.points = {{0, 0}, {80, 0}, {80, 80}, {0, 80}};
        d.entities().push_back(g);
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(3);
        ed.setTool(ToolId::Chamfer);
        ed.onPointerPress({40, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({80, 40}, Qt::LeftButton, Qt::NoModifier); // corner (80,0) = vertex 1
        const Entity* gg = nullptr;
        for (const auto& e : d.entities()) if (e.id == "g") gg = &e;
        check(gg && near(gg->meta.value("cornerChamfers").toArray()[1].toDouble(), 40.0) &&
                  near(gg->meta.value("cornerChamfers2").toArray()[1].toDouble(), 40.0),
              "chamfer-polygon-points: polygon corner chamfered between the clicked points");
    }

    // --- OFFSET (AutoCAD-style): type a distance, pick a line, click a side -> an
    //     offset copy is placed on the clicked side; the source stays put.
    {
        Document d; d.entities().clear();
        Entity l; l.id = "ol"; l.type = EntityType::Line; l.a = {0, 0}; l.b = {100, 0};
        d.entities().push_back(l);
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(3);
        ed.setTool(ToolId::Offset);
        const bool dist = ed.acceptCommandInput("10"); // type the offset distance
        ed.onPointerPress({50, 0}, Qt::LeftButton, Qt::NoModifier);  // pick the line
        ed.onPointerPress({50, 40}, Qt::LeftButton, Qt::NoModifier); // side = above -> +y
        int lines = 0; const Entity* copy = nullptr;
        for (const auto& e : d.entities()) {
            if (e.type == EntityType::Line) ++lines;
            if (e.type == EntityType::Line && e.id != "ol") copy = &e;
        }
        check(dist && lines == 2 && copy && near(copy->a.y, 10.0) && near(copy->b.y, 10.0) &&
                  near(copy->a.x, 0.0) && near(copy->b.x, 100.0),
              "offset-line: typed distance + side click places an offset copy above the line");
    }

    // --- OFFSET a rectangle: clicking OUTSIDE grows it by the distance on every side.
    {
        Document d; d.entities().clear();
        Entity r; r.id = "orr"; r.type = EntityType::Rect;
        r.points = {{0, 0}, {40, 0}, {40, 20}, {0, 20}};
        d.entities().push_back(r);
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(3);
        ed.setTool(ToolId::Offset);
        ed.acceptCommandInput("5");
        ed.onPointerPress({20, 0}, Qt::LeftButton, Qt::NoModifier);    // pick the rect edge
        ed.onPointerPress({20, -30}, Qt::LeftButton, Qt::NoModifier);  // side = outside -> grow
        const Entity* copy = nullptr;
        for (const auto& e : d.entities())
            if (e.type == EntityType::Rect && e.id != "orr") copy = &e;
        // Grown rect spans (-5,-5)..(45,25): area 50x30.
        const Bounds b = copy ? boundsOf(copy->points) : Bounds{};
        check(copy && near(b.width(), 50.0) && near(b.height(), 30.0),
              "offset-rect: offsetting a rectangle outward grows it by the distance all round");
    }

    // --- Insert Table: single-click placement from a spec (Title + Header + data
    //     rows), then edit a data cell; cell lookup + write round-trip correctly.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        Editor::TableSpec s;
        s.cols = 3; s.colWidth = 20.0; s.dataRows = 1; s.rowHeight = 10.0;
        s.title = true; s.header = true; // total rows = 1 + 1 + 1 = 3
        ed.setTableSpec(s);
        ed.setTool(ToolId::Table);
        ed.onPointerPress({0, 30}, Qt::LeftButton, Qt::NoModifier); // insertion point (top-left)
        const Entity* t = nullptr;
        for (const auto& e : d.entities()) if (e.meta.value("table").toBool()) t = &e;
        check(t && t->type == EntityType::Rect && t->meta.value("rows").toInt() == 3 &&
                  t->meta.value("cols").toInt() == 3 && t->meta.value("titleRow").toBool() &&
                  t->meta.value("headerRow").toBool() && t->meta.value("cells").toArray().size() == 9,
              "table: Insert Table builds a Title+Header+data grid at the insertion point");
        // Table spans x[0..60], y[0..30]; row 2 (bottom data row) is y[0..10].
        Vec2 tl; double cw = 0, ch = 0;
        const int idx = t ? ed.tableCellAt(*t, {30, 5}, tl, cw, ch) : -1; // middle col, data row
        check(idx == 7, "table: cell lookup maps a point to the right data cell");
        // A click in the merged title band maps to the single title cell (index 0).
        const int titleIdx = t ? ed.tableCellAt(*t, {50, 25}, tl, cw, ch) : -1;
        check(titleIdx == 0, "table: the merged title row is one cell (index 0)");
        if (t) ed.applyTableCellEdit(t->id, idx, QStringLiteral("hi"));
        for (const auto& e : d.entities()) if (e.meta.value("table").toBool()) t = &e;
        check(t && t->meta.value("cells").toArray()[7].toString() == QLatin1String("hi"),
              "table: editing a cell stores its text");
    }

    // --- Table cell selection + contextual row/column/align operations.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        Editor::TableSpec s;
        s.cols = 3; s.colWidth = 20.0; s.dataRows = 2; s.rowHeight = 10.0;
        s.title = false; s.header = false; // plain 2x3 grid
        ed.setTableSpec(s);
        ed.setTool(ToolId::Table);
        ed.onPointerPress({0, 20}, Qt::LeftButton, Qt::NoModifier);
        auto table = [&]() -> const Entity* {
            for (const auto& e : d.entities()) if (e.meta.value("table").toBool()) return &e;
            return nullptr;
        };
        const QString tid = table()->id;
        // Clicking a cell in Select mode selects the cell only, not the whole table.
        ed.setTool(ToolId::Select);
        ed.onPointerPress({30, 15}, Qt::LeftButton, Qt::NoModifier); // top-middle cell (idx 1)
        check(ed.selectedCellTable() == tid && ed.selectedCellIndex() == 1,
              "table cell: clicking a cell selects that cell (index 1)");
        // Insert a row below the selected cell's row: rows 2 -> 3, cells 6 -> 9.
        ed.tableInsertRow(true);
        check(table()->meta.value("rows").toInt() == 3 &&
                  table()->meta.value("cells").toArray().size() == 9,
              "table cell: insert row grows the grid");
        // Insert a column to the right: cols 3 -> 4, cells 9 -> 12.
        ed.tableInsertColumn(true);
        check(table()->meta.value("cols").toInt() == 4 &&
                  table()->meta.value("cells").toArray().size() == 12,
              "table cell: insert column grows the grid");
        // Set alignment on the currently selected cell.
        const int ai = ed.selectedCellIndex();
        ed.setCellAlignment(QStringLiteral("MC"));
        check(table()->meta.value("cellAlign").toArray()[ai].toString() == QLatin1String("MC"),
              "table cell: alignment code is stored on the cell");
        // Delete row / column shrink back.
        ed.tableDeleteRow();
        check(table()->meta.value("rows").toInt() == 2, "table cell: delete row shrinks the grid");
        ed.tableDeleteColumn();
        check(table()->meta.value("cols").toInt() == 3, "table cell: delete column shrinks the grid");
    }

    // --- Resize a table COLUMN by dragging its grid line: only the column left of the
    //     line resizes; the others keep their width and shift, the table grows.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        Editor::TableSpec s;
        s.cols = 3; s.colWidth = 20.0; s.dataRows = 2; s.rowHeight = 10.0;
        s.title = false; s.header = false; // 3x2 grid, spans x[0..60], y[0..20]
        ed.setTableSpec(s);
        ed.setTool(ToolId::Table);
        ed.onPointerPress({0, 20}, Qt::LeftButton, Qt::NoModifier);
        auto table = [&]() -> const Entity* {
            for (const auto& e : d.entities()) if (e.meta.value("table").toBool()) return &e;
            return nullptr;
        };
        auto tableWidth = [&] {
            double lo = table()->points[0].x, hi = lo;
            for (const auto& p : table()->points) { lo = std::min(lo, p.x); hi = std::max(hi, p.x); }
            return hi - lo;
        };
        const QString tid = table()->id;
        ed.setTool(ToolId::Select);
        ed.selectById(tid);
        // Grid line between col 0 and col 1 at x=20 (mid-y=10). Drag it right to x=30:
        // col 0 grows 20->30, cols 1 & 2 stay 20 wide, table widens 60->70.
        ed.onPointerPress({20, 10}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({30, 10});
        ed.onPointerPress({30, 10}, Qt::LeftButton, Qt::NoModifier);
        const QJsonArray cf = table()->meta.value("colFrac").toArray();
        const double W = tableWidth();
        check(std::abs(W - 70.0) < 1e-6, "table resize: column drag widens the table (60->70)");
        check(cf.size() == 3 && std::abs(cf[0].toDouble() * W - 30.0) < 1e-6 &&
                  std::abs(cf[1].toDouble() * W - 20.0) < 1e-6 &&
                  std::abs(cf[2].toDouble() * W - 20.0) < 1e-6,
              "table resize: only the left column resizes; the others keep their width");
    }

    // --- Resize a table ROW by dragging its grid line: only the row above resizes; the
    //     others keep their height and shift, the table grows.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        Editor::TableSpec s;
        s.cols = 3; s.colWidth = 20.0; s.dataRows = 2; s.rowHeight = 10.0;
        s.title = false; s.header = false; // 3x2 grid, spans x[0..60], y[0..20]
        ed.setTableSpec(s);
        ed.setTool(ToolId::Table);
        ed.onPointerPress({0, 20}, Qt::LeftButton, Qt::NoModifier);
        auto table = [&]() -> const Entity* {
            for (const auto& e : d.entities()) if (e.meta.value("table").toBool()) return &e;
            return nullptr;
        };
        auto tableHeight = [&] {
            double lo = table()->points[0].y, hi = lo;
            for (const auto& p : table()->points) { lo = std::min(lo, p.y); hi = std::max(hi, p.y); }
            return hi - lo;
        };
        ed.setTool(ToolId::Select);
        ed.selectById(table()->id);
        // Row line between row 0 and row 1 at y=10 (mid-x=30). Drag it down to y=5:
        // row 0 grows 10->15, row 1 stays 10 tall, table grows 20->25.
        ed.onPointerPress({30, 10}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({30, 5});
        ed.onPointerPress({30, 5}, Qt::LeftButton, Qt::NoModifier);
        const QJsonArray rf = table()->meta.value("rowFrac").toArray();
        const double H = tableHeight();
        check(std::abs(H - 25.0) < 1e-6, "table resize: row drag grows the table (20->25)");
        check(rf.size() == 2 && std::abs(rf[0].toDouble() * H - 15.0) < 1e-6 &&
                  std::abs(rf[1].toDouble() * H - 10.0) < 1e-6,
              "table resize: only the row above resizes; the other keeps its height");
    }

    // --- Committing an inline edit on a balloon saves its number (balloons aren't
    //     Text objects, so applyTextEdit must still store their text).
    {
        Document d; d.entities().clear();
        Entity b; b.id = "b"; b.type = EntityType::Circle; b.center = {0, 0}; b.a = {0, 0};
        b.radius = 6; b.meta.insert("balloon", true); b.meta.insert("text", QStringLiteral("1"));
        d.entities().push_back(b);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.applyTextEdit("b", QStringLiteral("42"), Editor::TextStyle{});
        const Entity* bb = nullptr;
        for (const auto& e : d.entities()) if (e.id == "b") bb = &e;
        check(bb && bb->meta.value("text").toString() == QLatin1String("42"),
              "balloon-edit: committing the inline editor saves the balloon number");
    }

    // --- A dimension's moved text position (textPosX/Y meta) follows the dimension
    //     when it is moved, so the dashed leader stays attached.
    {
        Document d; d.entities().clear();
        Entity dim; dim.id = "dm"; dim.type = EntityType::Dimension;
        dim.meta.insert("dimType", QStringLiteral("linear"));
        dim.a = {0, 0}; dim.b = {100, 0};
        dim.points = {{0, 0}, {100, 0}, {50, 20}};
        dim.meta.insert("textPosX", 50.0);
        dim.meta.insert("textPosY", 30.0);
        d.entities().push_back(dim);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.selectById("dm");
        ed.setTool(ToolId::Move);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);  // base point
        ed.onPointerPress({10, 5}, Qt::LeftButton, Qt::NoModifier); // -> delta (10,5)
        const Entity* dd = nullptr;
        for (const auto& e : d.entities()) if (e.id == "dm") dd = &e;
        check(dd && near(dd->meta.value("textPosX").toDouble(), 60.0) &&
                  near(dd->meta.value("textPosY").toDouble(), 35.0),
              "dim-textmove: moved dimension text follows a Move (leader stays attached)");
    }

    // --- Balloon: two clicks (arrow tip, then centre) make an auto-numbered circle
    //     with a leader tip + editable landing length.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setTool(ToolId::Balloon);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);   // tip
        ed.onPointerPress({20, 20}, Qt::LeftButton, Qt::NoModifier); // centre -> balloon 1
        ed.setTool(ToolId::Balloon);
        ed.onPointerPress({40, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({60, 20}, Qt::LeftButton, Qt::NoModifier); // balloon 2
        int balloons = 0; QStringList nums; const Entity* first = nullptr;
        for (const auto& e : d.entities())
            if (e.type == EntityType::Circle && e.meta.value("balloon").toBool()) {
                ++balloons; nums << e.meta.value("text").toString();
                if (!first) first = &e;
            }
        check(balloons == 2 && nums.contains("1") && nums.contains("2"),
              "balloon: two-click placements are auto-numbered circles");
        check(first && first->meta.value("leaders").toArray().size() == 1 &&
                  first->meta.value("landing").toDouble() > 0.0,
              "balloon: carries a leader arrow tip and an editable landing length");
    }

    // --- New object-snap options apply and each honors its own toggle.
    {
        Document d; d.entities().clear();
        Entity r; r.id = "r"; r.type = EntityType::Rect;
        r.points = {{0, 0}, {40, 0}, {40, 20}, {0, 20}}; // centroid (20,10)
        d.entities().push_back(r);
        Entity c; c.id = "c"; c.type = EntityType::Circle; c.center = {100, 0}; c.radius = 10;
        d.entities().push_back(c);
        // Geometric centre of the rectangle.
        {
            SnapSettings s; s.snapGeometricCenter = true;
            auto rr = resolveSnap(d, {21, 11}, std::nullopt, 5.0, s, {});
            check(rr.snapped && rr.kind == SnapKind::GeometricCenter && near(rr.point.x, 20.0) &&
                      near(rr.point.y, 10.0),
                  "snap-geomcenter: snaps to a rectangle centroid");
            s.snapGeometricCenter = false; s.snapNearest = false; s.snapEndpoint = false;
            s.snapMidpoint = false; s.snapIntersection = false;
            auto off = resolveSnap(d, {21, 11}, std::nullopt, 5.0, s, {});
            check(!off.snapped, "snap-geomcenter: off when its toggle is off");
        }
        // Quadrant of the circle (right quadrant at (110,0)). Endpoint off so it
        // doesn't shadow the coincident quadrant point.
        {
            SnapSettings s; s.snapQuadrant = true; s.snapNearest = false; s.snapEndpoint = false;
            auto q = resolveSnap(d, {109, 1}, std::nullopt, 5.0, s, {});
            check(q.snapped && q.kind == SnapKind::Quadrant && near(q.point.x, 110.0),
                  "snap-quadrant: snaps to a circle quadrant point");
        }
        // Nearest snaps onto the circle RING (not only straight segments).
        {
            SnapSettings s; // nearest on by default; higher-priority snaps off nearby
            s.snapEndpoint = false; s.snapCenter = false; s.snapQuadrant = false;
            s.snapMidpoint = false; s.snapIntersection = false; s.snapGeometricCenter = false;
            s.snapNode = false;
            // Cursor just outside the circle ring (circle at (100,0) r10) near (112,0).
            auto nr = resolveSnap(d, {112, 0}, std::nullopt, 5.0, s, {});
            check(nr.snapped && nr.kind == SnapKind::Nearest && near(nr.point.x, 110.0) &&
                      near(nr.point.y, 0.0),
                  "snap-nearest-curve: Nearest snaps onto the circle ring");
        }
        // Tangent from an anchor above the circle to the ring (~(90.3, 2.5)).
        {
            SnapSettings s; s.snapTangent = true; s.snapNearest = false; s.snapEndpoint = false;
            auto t = resolveSnap(d, {90, 3}, {Vec2{100, 40}}, 5.0, s, {});
            check(t.snapped && t.kind == SnapKind::Tangent,
                  "snap-tangent: snaps to a tangent point from the anchor");
        }
    }

    // --- Object snap finds a dimension's joints (measured points + dim-line ends),
    //     and ONLY when object snap is enabled (request 2 + 3).
    {
        Document d; d.entities().clear();
        Entity dim; dim.id = "dm"; dim.type = EntityType::Dimension;
        dim.meta.insert("dimType", QStringLiteral("linear"));
        dim.a = {0, 0}; dim.b = {100, 0};
        dim.points = {{0, 0}, {100, 0}, {50, 40}}; // offset up -> dim line at y=40
        d.entities().push_back(dim);
        SnapSettings s; // defaults: objectSnap on, endpoint on
        // Near the measured point (0,0):
        auto r1 = resolveSnap(d, {2, 1}, std::nullopt, 5.0, s, {});
        check(r1.snapped && near(r1.point.x, 0.0) && near(r1.point.y, 0.0),
              "dim-snap: snaps to a dimension measured point");
        // Near the dimension-line end (100,40):
        auto r2 = resolveSnap(d, {98, 41}, std::nullopt, 5.0, s, {});
        check(r2.snapped && near(r2.point.x, 100.0) && near(r2.point.y, 40.0),
              "dim-snap: snaps to a dimension line end");
        // With object snap OFF, no object snapping happens (request 3).
        s.objectSnap = false; s.gridSnap = false;
        auto r3 = resolveSnap(d, {2, 1}, std::nullopt, 5.0, s, {});
        check(!r3.snapped, "dim-snap: no object snap when object snap is off");
    }

    // --- Snap to a table's grid joints (cell corners) and cell-edge midpoints.
    {
        Document d; d.entities().clear();
        Entity t; t.id = "tb"; t.type = EntityType::Rect;
        t.points = {{0, 0}, {40, 0}, {40, 20}, {0, 20}}; // 2x2, x[0..40] y[0..20]
        t.meta.insert("table", true);
        t.meta.insert("rows", 2); t.meta.insert("cols", 2);
        QJsonArray cf; cf.append(0.5); cf.append(0.5);
        QJsonArray rf; rf.append(0.5); rf.append(0.5);
        t.meta.insert("colFrac", cf); t.meta.insert("rowFrac", rf);
        d.entities().push_back(t);
        SnapSettings s; // endpoint on by default
        auto r = resolveSnap(d, {21, 11}, std::nullopt, 5.0, s, {});
        check(r.snapped && near(r.point.x, 20.0) && near(r.point.y, 10.0),
              "table-snap: snaps to an interior grid joint");
        SnapSettings m; m.snapEndpoint = false; m.snapNearest = false; m.snapMidpoint = true;
        auto rm = resolveSnap(d, {10, 19}, std::nullopt, 5.0, m, {});
        check(rm.snapped && near(rm.point.x, 10.0) && near(rm.point.y, 20.0),
              "table-snap: snaps to a cell-edge midpoint");
    }

    // --- A leader can be re-connected to another balloon by dropping its landing
    //     joint on the other circle (request 5).
    {
        Document d; d.entities().clear();
        auto mk = [&](const QString& id, Vec2 c, bool withLeader) {
            Entity e; e.id = id; e.type = EntityType::Circle; e.center = c; e.a = c; e.radius = 6;
            e.meta.insert("balloon", true); e.meta.insert("text", QStringLiteral("1"));
            e.meta.insert("landing", 8.0);
            QJsonArray leaders;
            if (withLeader) { QJsonArray t; t.append(c.x - 40); t.append(c.y); leaders.append(t); }
            e.meta.insert("leaders", leaders);
            d.entities().push_back(e);
        };
        mk("A", {0, 0}, true);   // has the leader
        mk("B", {40, 0}, false); // target circle
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(6);
        ed.selectById("A");
        // Grab A's landing joint (near A, on the leader side ~x=-14) and drop on B.
        // Landing joint now sits ON the ring at the attach point: center+dir(pi)*r = (-6,0).
        ed.onPointerPress({-6, 0}, Qt::LeftButton, Qt::NoModifier); // grab the landing grip
        ed.onPointerPress({40, 0}, Qt::LeftButton, Qt::NoModifier); // drop on B's centre
        const Entity *A = nullptr, *B = nullptr;
        for (const auto& e : d.entities()) { if (e.id == "A") A = &e; if (e.id == "B") B = &e; }
        check(A && B && A->meta.value("leaders").toArray().isEmpty() &&
                  B->meta.value("leaders").toArray().size() == 1,
              "leader-reconnect: dropping the landing joint on another circle moves the leader");
    }

    // --- Dragging the knee changes the HORIZONTAL landing length (stays horizontal).
    {
        Document d; d.entities().clear();
        Entity a; a.id = "A"; a.type = EntityType::Circle; a.center = {0, 0}; a.a = {0, 0};
        a.radius = 6; a.meta.insert("balloon", true); a.meta.insert("text", QStringLiteral("1"));
        a.meta.insert("landing", 8.0);
        QJsonArray leaders; QJsonArray t; t.append(40); t.append(0); leaders.append(t);
        a.meta.insert("leaders", leaders); // tip (40,0) -> side right, knee at (14,0)
        d.entities().push_back(a);
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(4);
        ed.selectById("A");
        ed.onPointerPress({14, 0}, Qt::LeftButton, Qt::NoModifier);  // grab the knee
        ed.onPointerPress({25, 12}, Qt::LeftButton, Qt::NoModifier); // drag it out (y ignored)
        const Entity* aa = nullptr;
        for (const auto& e : d.entities()) if (e.id == "A") aa = &e;
        // landing = |25 - 0| - radius(6) = 19.
        check(aa && near(aa->meta.value("landing").toDouble(), 19.0),
              "leader-knee: dragging the knee sets the horizontal landing length");
    }

    // --- Moving a batch circle drags it + all circles further from the arrow (the
    //     tail); Shift moves only that one.
    {
        auto build = [](Document& d) {
            d.entities().clear();
            auto mk = [&](const QString& id, Vec2 c, bool leader) {
                Entity e; e.id = id; e.type = EntityType::Circle; e.center = c; e.a = c; e.radius = 6;
                e.meta.insert("balloon", true); e.meta.insert("text", QStringLiteral("1"));
                e.meta.insert("chain", QStringLiteral("cx"));
                if (leader) { QJsonArray l; QJsonArray t; t.append(60); t.append(0); l.append(t); e.meta.insert("leaders", l); }
                d.entities().push_back(e);
            };
            mk("A", {0, 0}, true);   // arrow (rightward leader)
            mk("B", {12, 0}, false);
            mk("C", {24, 0}, false);
            mk("D", {36, 0}, false);
        };
        auto centerOf = [](Document& d, const QString& id) {
            for (const auto& e : d.entities()) if (e.id == id) return e.center;
            return Vec2{};
        };
        // No Shift: dragging C moves C and D (further from A), not A/B.
        {
            Document d; build(d);
            Editor ed; ed.setDocument(&d); noSnap(ed);
            ed.selectById("A"); ed.selectById("B", true); ed.selectById("C", true); ed.selectById("D", true);
            ed.onPointerPress({24, 0}, Qt::LeftButton, Qt::NoModifier);  // grab C
            ed.onPointerPress({24, 20}, Qt::LeftButton, Qt::NoModifier); // move up
            check(near(centerOf(d, "A").y, 0) && near(centerOf(d, "B").y, 0) &&
                      near(centerOf(d, "C").y, 20) && near(centerOf(d, "D").y, 20),
                  "batch-tail: moving a circle drags it + the circles beyond it");
        }
        // With Shift: dragging C moves only C.
        {
            Document d; build(d);
            Editor ed; ed.setDocument(&d); noSnap(ed);
            ed.selectById("A"); ed.selectById("B", true); ed.selectById("C", true); ed.selectById("D", true);
            ed.onPointerPress({24, 0}, Qt::LeftButton, Qt::ShiftModifier);
            ed.onPointerPress({24, 20}, Qt::LeftButton, Qt::ShiftModifier);
            check(near(centerOf(d, "D").y, 0),
                  "batch-tail: Shift moves only the grabbed circle (D stays)");
        }
    }

    // --- A moved batch circle may not overlap ANY other circle in the batch.
    {
        Document d; d.entities().clear();
        auto mk = [&](const QString& id, Vec2 c, bool leader) {
            Entity e; e.id = id; e.type = EntityType::Circle; e.center = c; e.a = c; e.radius = 6;
            e.meta.insert("balloon", true); e.meta.insert("text", QStringLiteral("1"));
            e.meta.insert("chain", QStringLiteral("cx"));
            if (leader) { QJsonArray l; QJsonArray t; t.append(-40); t.append(0); l.append(t); e.meta.insert("leaders", l); }
            d.entities().push_back(e);
        };
        mk("A", {0, 0}, true);    // arrow
        mk("B", {12, 0}, false);
        mk("C", {6, 10}, false);  // this one we move onto the others
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(4);
        ed.selectById("C");
        // Shift = move only this circle (tangent + no-overlap constrained).
        ed.onPointerPress({6, 10}, Qt::LeftButton, Qt::ShiftModifier); // grab C
        ed.onPointerPress({6, 0}, Qt::LeftButton, Qt::ShiftModifier);  // drop between A and B
        const Entity *A = nullptr, *B = nullptr, *C = nullptr;
        for (const auto& e : d.entities()) { if (e.id=="A") A=&e; if (e.id=="B") B=&e; if (e.id=="C") C=&e; }
        check(A && B && C && distance(A->center, C->center) >= 12.0 - 1e-3 &&
                  distance(B->center, C->center) >= 12.0 - 1e-3,
              "batch-nooverlap: a moved circle never overlaps another batch circle");
    }

    // --- A non-arrow batch circle stays TANGENT to a neighbour when moved.
    {
        Document d; d.entities().clear();
        auto mk = [&](const QString& id, Vec2 c, bool leader) {
            Entity e; e.id = id; e.type = EntityType::Circle; e.center = c; e.a = c; e.radius = 6;
            e.meta.insert("balloon", true); e.meta.insert("text", QStringLiteral("1"));
            e.meta.insert("chain", QStringLiteral("cx"));
            if (leader) { QJsonArray l; QJsonArray t; t.append(-40); t.append(0); l.append(t); e.meta.insert("leaders", l); }
            d.entities().push_back(e);
        };
        mk("A", {0, 0}, true);   // arrow circle
        mk("B", {12, 0}, false); // coupled, tangent (2r = 12)
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(4);
        ed.selectById("B");
        // Shift = move only B (stays tangent to its neighbour).
        ed.onPointerPress({12, 0}, Qt::LeftButton, Qt::ShiftModifier);  // grab B's centre
        ed.onPointerPress({12, 30}, Qt::LeftButton, Qt::ShiftModifier); // drag it upward
        const Entity *A = nullptr, *B = nullptr;
        for (const auto& e : d.entities()) { if (e.id == "A") A = &e; if (e.id == "B") B = &e; }
        check(A && B && near(distance(A->center, B->center), 12.0),
              "leader-tangent: a coupled circle stays tangent to its neighbour when moved");
    }

    // --- Text leader: two clicks make a leader with a text label and NO number/circle.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setTool(ToolId::TextLeader);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);   // tip
        ed.onPointerPress({30, 30}, Qt::LeftButton, Qt::NoModifier); // label location
        const Entity* t = nullptr;
        for (const auto& e : d.entities()) if (e.meta.value("balloon").toBool()) t = &e;
        check(t && t->meta.value("textLeader").toBool() &&
                  t->meta.value("leaders").toArray().size() == 1 &&
                  !t->meta.value("text").toString().isEmpty(),
              "text-leader: a leader with a text label (no numbered circle) is placed");
    }

    // --- Ctrl+click a balloon circle adds a COUPLED circle (chained, no leader), and
    //     dragging one circle's centre moves its chain-mate while leaders stay put.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(4);
        ed.setTool(ToolId::Balloon);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);   // tip
        ed.onPointerPress({40, 40}, Qt::LeftButton, Qt::NoModifier); // centre -> balloon 1
        const Entity* b1 = nullptr;
        for (const auto& e : d.entities()) if (e.meta.value("balloon").toBool()) b1 = &e;
        // Ctrl+click on the balloon circle -> add a coupled circle.
        ed.setTool(ToolId::Select);
        ed.onPointerPress(b1->center, Qt::LeftButton, Qt::ControlModifier);
        int balloons = 0; QString chain;
        for (const auto& e : d.entities())
            if (e.meta.value("balloon").toBool()) {
                ++balloons;
                if (chain.isEmpty()) chain = e.meta.value("chain").toString();
            }
        bool sameChain = !chain.isEmpty();
        for (const auto& e : d.entities())
            if (e.meta.value("balloon").toBool() && e.meta.value("chain").toString() != chain)
                sameChain = false;
        check(balloons == 2 && sameChain,
              "coupled-balloon: Ctrl+click adds a second circle sharing a chain id");
        // Exactly one balloon (the coupled circle) carries no leader.
        int noLeader = 0;
        for (const auto& e : d.entities())
            if (e.meta.value("balloon").toBool() && e.meta.value("leaders").toArray().isEmpty())
                ++noLeader;
        check(noLeader == 1, "coupled-balloon: the coupled circle has no leader of its own");
    }

    // --- Multileader: Add Leader appends a second arrow to a balloon; Remove Leader
    //     drops one; a balloon keeps at least one.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setTool(ToolId::Balloon);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({20, 20}, Qt::LeftButton, Qt::NoModifier);
        const Entity* b = nullptr;
        for (const auto& e : d.entities()) if (e.meta.value("balloon").toBool()) b = &e;
        ed.selectById(b->id);
        ed.addLeaderToSelection();
        for (const auto& e : d.entities()) if (e.meta.value("balloon").toBool()) b = &e;
        check(b && b->meta.value("leaders").toArray().size() == 2,
              "multileader: Add Leader appends a second leader");
        ed.removeLeaderFromSelection();
        for (const auto& e : d.entities()) if (e.meta.value("balloon").toBool()) b = &e;
        check(b && b->meta.value("leaders").toArray().size() == 1,
              "multileader: Remove Leader drops one leader");
    }

    // --- Order a balloon batch into a single touching row (numeric order, arrow at
    //     the leader end) — collapses a cluster to minimum area.
    {
        Document d; d.entities().clear();
        auto mk = [&](const QString& id, int num, Vec2 c, bool leader) {
            Entity e; e.id = id; e.type = EntityType::Circle; e.center = c; e.a = c; e.radius = 6;
            e.meta.insert("balloon", true); e.meta.insert("text", QString::number(num));
            e.meta.insert("chain", QStringLiteral("cx")); e.meta.insert("landing", 8.0);
            if (leader) {
                e.meta.insert("landingSide", -1.0); // leader exits LEFT -> row grows right
                QJsonArray l; QJsonArray t; t.append(-40); t.append(0); l.append(t);
                e.meta.insert("leaders", l);
            }
            d.entities().push_back(e);
        };
        mk("c1", 1, {0, 0}, true);    // arrow circle (leader end)
        mk("c2", 2, {0, 12}, false);  // stacked above -> should move into the row
        mk("c3", 3, {12, 0}, false);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.orderBalloonRow("c1");
        const Entity *a = nullptr, *b = nullptr, *cc = nullptr;
        for (const auto& e : d.entities()) {
            if (e.id == "c1") a = &e; if (e.id == "c2") b = &e; if (e.id == "c3") cc = &e;
        }
        // Row grows right from c1 at x=0: c1=0, c2=12, c3=24, all at y=0.
        check(a && b && cc && near(a->center.y, 0) && near(b->center.y, 0) && near(cc->center.y, 0),
              "order-row: batch collapses to one horizontal row");
        check(near(a->center.x, 0) && near(b->center.x, 12) && near(cc->center.x, 24),
              "order-row: circles placed touching in numeric order from the arrow");
    }

    // --- A window/crossing zone over the batch selects ALL its circles, even if the
    //     arrow circle's leader runs outside the zone.
    {
        Document d; d.entities().clear();
        auto mk = [&](const QString& id, int num, Vec2 c, bool leader) {
            Entity e; e.id = id; e.type = EntityType::Circle; e.center = c; e.a = c; e.radius = 6;
            e.meta.insert("balloon", true); e.meta.insert("text", QString::number(num));
            e.meta.insert("chain", QStringLiteral("cx")); e.meta.insert("landing", 8.0);
            if (leader) { QJsonArray l; QJsonArray t; t.append(-200); t.append(-200); l.append(t); e.meta.insert("leaders", l); }
            d.entities().push_back(e);
        };
        mk("c1", 1, {0, 0}, true);   // leader runs far out to (-200,-200)
        mk("c2", 2, {14, 0}, false);
        mk("c3", 3, {28, 0}, false);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        // Window from (-10,-10) to (40,10): encloses all 3 circles; the leader exits it.
        ed.onPointerPress({-10, -10}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({40, 10});
        ed.onPointerRelease({40, 10}, Qt::LeftButton);
        check(ed.selection().size() == 3,
              "batch-window: a zone over the batch selects all its circles");
    }

    // --- Reorder-to-minimize packs a 3-balloon batch into a triangle (2 top, 1 below).
    {
        Document d; d.entities().clear();
        auto mk = [&](const QString& id, int num, Vec2 c, bool leader) {
            Entity e; e.id = id; e.type = EntityType::Circle; e.center = c; e.a = c; e.radius = 6;
            e.meta.insert("balloon", true); e.meta.insert("text", QString::number(num));
            e.meta.insert("chain", QStringLiteral("cx")); e.meta.insert("landing", 8.0);
            if (leader) { QJsonArray l; QJsonArray t; t.append(-40); t.append(0); l.append(t); e.meta.insert("leaders", l); }
            d.entities().push_back(e);
        };
        mk("c1", 1, {0, 0}, true);
        mk("c2", 2, {80, 80}, false);
        mk("c3", 3, {200, 5}, false);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.reorderBalloonMinimize("c1");
        int top = 0, bottom = 0;
        const double botY = -6.0 * std::sqrt(3.0);
        for (const auto& e : d.entities()) {
            if (!e.meta.value("balloon").toBool()) continue;
            if (near(e.center.y, 0.0)) ++top;
            else if (near(e.center.y, botY, 1e-4)) ++bottom;
        }
        check(top == 2 && bottom == 1,
              "reorder-min: 3 balloons pack into a triangle (2 on top, 1 below)");
    }

    // --- Reorder-to-minimize moves the leader to the circle nearest its arrow tip
    //     (e.g. a rightward leader ends up on balloon 2, the right one of the top row).
    {
        Document d; d.entities().clear();
        auto mk = [&](const QString& id, int num, Vec2 c, bool leader) {
            Entity e; e.id = id; e.type = EntityType::Circle; e.center = c; e.a = c; e.radius = 6;
            e.meta.insert("balloon", true); e.meta.insert("text", QString::number(num));
            e.meta.insert("chain", QStringLiteral("cx")); e.meta.insert("landing", 8.0);
            if (leader) { QJsonArray l; QJsonArray t; t.append(200); t.append(5); l.append(t); e.meta.insert("leaders", l); }
            d.entities().push_back(e);
        };
        mk("c1", 1, {0, 0}, true);   // leader points right (200,5)
        mk("c2", 2, {80, 80}, false);
        mk("c3", 3, {200, -5}, false);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.reorderBalloonMinimize("c1");
        const Entity *c1 = nullptr, *c2 = nullptr;
        for (const auto& e : d.entities()) { if (e.id=="c1") c1=&e; if (e.id=="c2") c2=&e; }
        check(c1 && c2 && c1->meta.value("leaders").toArray().isEmpty() &&
                  c2->meta.value("leaders").toArray().size() == 1,
              "reorder-min: the leader moves to the circle nearest its tip");
    }

    // --- Align lines selected balloons up evenly on their major axis.
    {
        Document d; d.entities().clear();
        auto mkBalloon = [&](const QString& id, Vec2 c) {
            Entity e; e.id = id; e.type = EntityType::Circle; e.center = c; e.a = c; e.radius = 6;
            e.meta.insert("balloon", true); e.meta.insert("text", QStringLiteral("1"));
            QJsonArray leaders; QJsonArray t; t.append(c.x - 20); t.append(c.y); leaders.append(t);
            e.meta.insert("leaders", leaders);
            d.entities().push_back(e);
        };
        mkBalloon("a", {0, 0}); mkBalloon("b", {50, 8}); mkBalloon("c", {100, -5});
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.selectById("a"); ed.selectById("b", true); ed.selectById("c", true);
        ed.alignBalloons();
        double y0 = 0; bool sameY = true;
        for (const auto& e : d.entities()) {
            if (e.id == "a") y0 = e.center.y;
            if (e.meta.value("balloon").toBool() && std::abs(e.center.y - y0) > 1e-6) sameY = false;
        }
        check(sameY, "align: balloons line up on a common axis");
    }

    // --- Note / Revision: rubber-band a rectangle that carries editable text; a
    //     revision box also gets an auto revision id.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setTool(ToolId::Note);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({50, 30}, Qt::LeftButton, Qt::NoModifier);
        ed.setTool(ToolId::Revision);
        ed.onPointerPress({0, 40}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({50, 70}, Qt::LeftButton, Qt::NoModifier);
        const Entity* note = nullptr; const Entity* rev = nullptr;
        for (const auto& e : d.entities()) {
            if (e.meta.value("note").toBool()) note = &e;
            if (e.meta.value("revision").toBool()) rev = &e;
        }
        check(note && note->type == EntityType::Rect && note->points.size() == 4 &&
                  !note->meta.value("text").toString().isEmpty(),
              "note: creates a rectangle carrying note text");
        check(rev && rev->type == EntityType::Rect && rev->meta.value("rev").toString() == "A",
              "revision: creates a titled revision box (rev A)");
    }

    // --- Scale by a TYPED factor (no size click): 2 doubles about the selection
    //     centre; the shape's bbox diagonal grows 2x.
    {
        Document d; d.entities().clear();
        Entity r; r.id = "sc"; r.type = EntityType::Rect;
        r.points = {{0, 0}, {40, 0}, {40, 20}, {0, 20}};
        d.entities().push_back(r);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.selectById("sc");
        ed.setTool(ToolId::Scale);
        const bool consumed = ed.acceptCommandInput("2"); // typed scale factor
        const Entity* rr = nullptr;
        for (const auto& e : d.entities()) if (e.id == "sc") rr = &e;
        const Bounds b = rr ? rr->bounds() : Bounds{};
        check(consumed && rr && near(b.width(), 80.0) && near(b.height(), 40.0),
              "scale-typed: typing a scale factor scales the selection about its centre");
    }

    // --- Scaling a double line/polyline scales its GAP (and fillet/chamfer metas)
    //     too, so it stays proportional.
    {
        Document d; d.entities().clear();
        Entity dl; dl.id = "dg"; dl.type = EntityType::DoubleLine; dl.a = {0, 0}; dl.b = {100, 0};
        dl.meta.insert("gap", 10.0);
        d.entities().push_back(dl);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.selectById("dg");
        ed.setTool(ToolId::Scale);
        ed.acceptCommandInput("2"); // scale x2 about the selection centre
        const Entity* dd = nullptr;
        for (const auto& e : d.entities()) if (e.id == "dg") dd = &e;
        check(dd && near(dd->meta.value("gap").toDouble(), 20.0) &&
                  near(distance(dd->a, dd->b), 200.0),
              "scale-gap: scaling a double line scales its gap with the geometry");
    }

    // --- visibleWall clips a marked wall past an existing chamfer (no stale sharp
    //     stub): a chamfered rect corner shortens the two adjoining walls.
    {
        Document d; d.entities().clear();
        Entity r; r.id = "vw"; r.type = EntityType::Rect;
        r.points = {{0, 0}, {100, 0}, {100, 60}, {0, 60}};
        QJsonArray in, out;
        in.append(0.0); in.append(20.0); in.append(0.0); in.append(0.0);   // vertex 1 chamfer in
        out.append(0.0); out.append(30.0); out.append(0.0); out.append(0.0); // vertex 1 chamfer out
        r.meta.insert("cornerChamfers", in);
        r.meta.insert("cornerChamfers2", out);
        d.entities().push_back(r);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        // Edge 0 (bottom, verts 0->1): end near vertex 1 is clipped by 20 (in) -> ends at (80,0).
        const auto w0 = ed.visibleWall(d.entities()[0], 0);
        // Edge 1 (right, verts 1->2): start near vertex 1 clipped by 30 (out) -> starts at (100,30).
        const auto w1 = ed.visibleWall(d.entities()[0], 1);
        check(near(w0.second.x, 80.0) && near(w0.second.y, 0.0) &&
                  near(w1.first.x, 100.0) && near(w1.first.y, 30.0),
              "visiblewall: marked wall is clipped to the chamfered corner, not the sharp vertex");
    }

    // --- Fillet works clicking MID-line (not just near the far ends): the pick's
    //     SIDE of the corner is what matters, so a rounded knee is still produced.
    {
        Entity h; h.type = EntityType::Line; h.a = {0, 0}; h.b = {100, 0};
        Entity v; v.type = EntityType::Line; v.a = {0, 0}; v.b = {0, 100};
        CornerResult r = filletLines(h, v, 10.0, {50, 0}, {0, 50}); // mid-line picks
        check(r.ok && r.hasConnector && r.connector.type == EntityType::Arc &&
                  near(r.connector.radius, 10.0),
              "fillet-mid: rounded knee produced when clicking mid-line");
        check(near(h.a.x, 10.0) && near(v.a.y, 10.0), "fillet-mid: both lines trimmed to the tangent");
    }

    // --- Fillet a right-angle corner: tangent points at distance r from corner.
    {
        Entity h; h.type = EntityType::Line; h.a = {0, 0}; h.b = {100, 0};
        Entity v; v.type = EntityType::Line; v.a = {0, 0}; v.b = {0, 100};
        // Pick points choose the far (kept) ends.
        CornerResult r = filletLines(h, v, 10.0, {100, 0}, {0, 100});
        check(r.ok, "fillet: ok");
        check(r.connector.type == EntityType::Arc && near(r.connector.radius, 10.0), "fillet: arc radius");
        check(near(r.connector.center.x, 10.0) && near(r.connector.center.y, 10.0), "fillet: arc center");
        // The connector must be the SHORT arc (a fillet spans < π, never a circle).
        {
            double span = r.connector.endAngle - r.connector.startAngle;
            while (span <= 0.0) span += 2.0 * M_PI;
            check(span < M_PI + 1e-6 && near(span, M_PI / 2.0),
                  "fillet: arc spans the short way (quarter circle), not a full circle");
        }
        // Horizontal line trimmed so its near end is at the tangent point (10,0).
        check(near(h.a.x, 10.0) && near(h.a.y, 0.0), "fillet: h trimmed");
        check(near(v.a.x, 0.0) && near(v.a.y, 10.0), "fillet: v trimmed");
    }

    // --- Chamfer a right-angle corner.
    {
        Entity h; h.type = EntityType::Line; h.a = {0, 0}; h.b = {100, 0};
        Entity v; v.type = EntityType::Line; v.a = {0, 0}; v.b = {0, 100};
        CornerResult r = chamferLines(h, v, 8.0, 8.0, {100, 0}, {0, 100});
        check(r.ok && r.connector.type == EntityType::Line, "chamfer: line connector");
        check(near(distance(r.connector.a, r.connector.b), std::sqrt(128.0)), "chamfer: span");
    }

    // --- Fillet radius too big for the lines: rejected (not applied), flagged tooBig.
    {
        Entity h; h.type = EntityType::Line; h.a = {0, 0}; h.b = {5, 0};
        Entity v; v.type = EntityType::Line; v.a = {0, 0}; v.b = {0, 5};
        CornerResult r = filletLines(h, v, 100.0, {5, 0}, {0, 5}); // radius >> segment length
        check(!r.ok && r.tooBig, "fillet: oversize radius is rejected (tooBig), not applied");
        // The lines must be untouched when the radius doesn't fit.
        check(near(h.b.x, 5.0) && near(v.b.y, 5.0), "fillet: lines untouched on reject");
    }

    // --- Polyline fillet fit check: small radius fits, oversize does not.
    {
        std::vector<Vec2> pts = {{0, 0}, {10, 0}, {10, 10}};
        check(filletRadiusFits(pts, 3.0), "polyfillet: small radius fits");
        check(!filletRadiusFits(pts, 20.0), "polyfillet: oversize radius does not fit");
    }

    // --- Rotate is two-click: base point, then the angle point (absolute angle).
    {
        Document d; d.entities().clear();
        Entity l; l.id = "l"; l.type = EntityType::Line; l.a = {0, 0}; l.b = {10, 0};
        d.entities().push_back(l);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.selectById("l");
        ed.setTool(ToolId::Rotate);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);  // pivot
        ed.onPointerPress({0, 10}, Qt::LeftButton, Qt::NoModifier); // angle = +90deg
        check(near(d.entities()[0].b.x, 0.0) && near(d.entities()[0].b.y, 10.0),
              "rotate: two-click rotates by the base->point angle");
    }

    // --- Scale is two-click about the base; factor = dist / bbox-diagonal.
    {
        Document d; d.entities().clear();
        Entity l; l.id = "l"; l.type = EntityType::Line; l.a = {0, 0}; l.b = {10, 0};
        d.entities().push_back(l); // bbox diagonal = 10
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.selectById("l");
        ed.setTool(ToolId::Scale);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);  // base
        ed.onPointerPress({20, 0}, Qt::LeftButton, Qt::NoModifier); // dist 20 / diag 10 = x2
        check(near(d.entities()[0].b.x, 20.0), "scale: two-click factor = dist / bbox diagonal");
    }

    // --- Chamfer (Editor): click a point on each line -> straight cut between them.
    {
        Document d; d.entities().clear();
        Entity h; h.id = "h"; h.type = EntityType::Line; h.a = {0, 0}; h.b = {100, 0};
        Entity v; v.id = "v"; v.type = EntityType::Line; v.a = {0, 0}; v.b = {0, 100};
        d.entities().push_back(h);
        d.entities().push_back(v);
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(3);
        ed.setTool(ToolId::Chamfer);
        ed.onPointerPress({30, 0}, Qt::LeftButton, Qt::NoModifier); // point on h
        ed.onPointerPress({0, 30}, Qt::LeftButton, Qt::NoModifier); // point on v -> commit
        bool gotChamfer = false;
        for (const auto& e : d.entities())
            if (e.type == EntityType::Line && near(distance(e.a, e.b), std::sqrt(1800.0), 1.0))
                gotChamfer = true;
        check(d.entities().size() == 3 && gotChamfer,
              "chamfer: straight cut between the two picked points");
    }

    // --- Trim a line at an intersection.
    {
        Entity h; h.type = EntityType::Line; h.a = {0, 0}; h.b = {100, 0};
        Entity cut; cut.type = EntityType::Line; cut.a = {40, -10}; cut.b = {40, 10};
        std::vector<Entity> others = {h, cut};
        // Trim the right portion of h (pick near x=80).
        bool ok = trimLineAt(others[0], {80, 0}, others);
        check(ok && near(others[0].b.x, 40.0), "trim: shortened to intersection");
    }

    // --- trimLineQuick: a piece between two cutting edges splits the line, removing
    //     the clicked middle and leaving the two outer pieces.
    {
        Entity h; h.type = EntityType::Line; h.a = {0, 0}; h.b = {100, 0};
        Entity c1; c1.type = EntityType::Line; c1.a = {30, -10}; c1.b = {30, 10};
        Entity c2; c2.type = EntityType::Line; c2.a = {70, -10}; c2.b = {70, 10};
        std::vector<Entity> others = {c1, c2};
        std::optional<Entity> extra;
        bool ok = trimLineQuick(h, {50, 0}, others, extra);
        check(ok && extra, "trim-quick: middle piece split into two");
        check(near(std::max(h.a.x, h.b.x), 30.0) && extra &&
                  near(std::min(extra->a.x, extra->b.x), 70.0),
              "trim-quick: outer pieces are [0..30] and [70..100]");
    }

    // --- Trimming against a CIRCLE cuts on the circle, not on a chord of one.
    //     The cutter is reduced to a 96-sided polygon for the general case, and every chord of that
    //     polygon lies inside the ring - so a trim that used it would stop short by R(1-cos(pi/96)),
    //     which is small enough to look right and wrong enough that snapping to that end misses.
    {
        // A circle of radius 50 at the origin, and a line along y = 0 from x = -100 to x = 100. The
        // crossings are exactly (-50,0) and (50,0).
        Entity circ;
        circ.type = EntityType::Circle;
        circ.center = {0, 0};
        circ.radius = 50.0;
        std::vector<Entity> cutters = {circ};

        // Picking outside the circle trims that end back to the ring.
        Entity h;
        h.type = EntityType::Line;
        h.a = {-100, 0};
        h.b = {100, 0};
        std::optional<Entity> extra;
        bool ok = trimLineQuick(h, {90, 0}, cutters, extra);
        check(ok && !extra, "trim-circle: the piece outside the circle is removed");
        const double cutX = std::max(h.a.x, h.b.x);
        std::printf("    (trimmed to x = %.9f; the ring is at 50)\n", cutX);
        check(near(cutX, 50.0, 1e-9), "trim-circle: and the end lands ON the ring, to nine places");

        // A fence INSIDE the ring takes everything inside it, wherever in the ring the fence fell.
        // Both ends of what is left sit ON the circle - a piece surviving inside with a raw end where
        // the fence happened to land is not a trim.
        for (double fenceX : {-20.0, 0.0, 20.0}) {
            Entity h2;
            h2.type = EntityType::Line;
            h2.a = {-100, 0};
            h2.b = {100, 0};
            std::optional<Entity> extra2;
            const bool split = trimLineQuick(h2, {fenceX, 0}, cutters, extra2);
            std::printf("    (fence inside at %+5.1f: kept [%.1f..%.1f] and [%.1f..%.1f])\n", fenceX,
                        std::min(h2.a.x, h2.b.x), std::max(h2.a.x, h2.b.x),
                        extra2 ? std::min(extra2->a.x, extra2->b.x) : 0.0,
                        extra2 ? std::max(extra2->a.x, extra2->b.x) : 0.0);
            check(split && extra2, "trim-circle: a fence inside the ring splits the line in two");
            check(near(std::max(h2.a.x, h2.b.x), -50.0, 1e-9),
                  "trim-circle: the first piece ends ON the ring");
            check(extra2 && near(std::min(extra2->a.x, extra2->b.x), 50.0, 1e-9),
                  "trim-circle: and the second starts ON it - all of the inside is gone");
        }

        // Off-centre, where a chord and the ring differ most: a line at y = 30 crosses at x = +-40.
        Entity h3;
        h3.type = EntityType::Line;
        h3.a = {-100, 30};
        h3.b = {100, 30};
        std::optional<Entity> extra3;
        ok = trimLineQuick(h3, {90, 30}, cutters, extra3);
        check(ok && near(std::max(h3.a.x, h3.b.x), 40.0, 1e-9),
              "trim-circle: an off-centre chord cuts at 40, exactly");

        // A line that misses the circle is not cut at all.
        Entity miss;
        miss.type = EntityType::Line;
        miss.a = {-100, 80};
        miss.b = {100, 80};
        std::optional<Entity> extraMiss;
        check(!trimLineQuick(miss, {0, 80}, cutters, extraMiss),
              "trim-circle: a line that misses it is left alone");

        // An ARC only cuts where it actually sweeps. The right half (-90 to +90 degrees) crosses
        // y = 0 at x = +50 only, so the left end has nothing to trim it.
        Entity arc;
        arc.type = EntityType::Arc;
        arc.center = {0, 0};
        arc.radius = 50.0;
        arc.startAngle = -1.5707963267948966;
        arc.endAngle = 1.5707963267948966;
        std::vector<Entity> arcCutter = {arc};
        Entity h4;
        h4.type = EntityType::Line;
        h4.a = {-100, 0};
        h4.b = {100, 0};
        std::optional<Entity> extra4;
        ok = trimLineQuick(h4, {90, 0}, arcCutter, extra4);
        check(ok && !extra4 && near(std::max(h4.a.x, h4.b.x), 50.0, 1e-9),
              "trim-arc: an arc cuts where it sweeps");
        Entity h5;
        h5.type = EntityType::Line;
        h5.a = {-100, 0};
        h5.b = {100, 0};
        std::optional<Entity> extra5;
        // Picking on the left: the arc does not reach there, so the only cut is the one at +50, and
        // the piece the pick is in - everything from -100 up to +50 - is what goes. (Quick trim
        // removes the piece that was clicked, so the survivor is the far side.)
        ok = trimLineQuick(h5, {-90, 0}, arcCutter, extra5);
        check(ok && !extra5 && near(std::min(h5.a.x, h5.b.x), 50.0, 1e-9) &&
                  near(std::max(h5.a.x, h5.b.x), 100.0, 1e-9),
              "trim-arc: the clicked piece goes, bounded by the one cut the arc gives");

        // Extending onto a circle lands on the ring too, for the same reason.
        Entity stub;
        stub.type = EntityType::Line;
        stub.a = {0, 0};
        stub.b = {20, 0};
        check(extendLineTo(stub, {20, 0}, cutters) && near(stub.b.x, 50.0, 1e-9),
              "extend-circle: an extended end lands on the ring, not on a chord");
    }

    // --- A primitive with NOTHING outside a circular reference goes whole.
    //     Trimming it back to the ring would leave nothing, and without this it could not be
    //     trimmed at all: a chord between two points of the circle has no crossing to cut at,
    //     because its crossings are its own endpoints.
    {
        Entity ref;
        ref.type = EntityType::Circle;
        ref.center = {0, 0};
        ref.radius = 50.0;

        auto lineFrom = [](Vec2 a, Vec2 b) {
            Entity e;
            e.type = EntityType::Line;
            e.a = a;
            e.b = b;
            return e;
        };
        // A chord: both ends exactly on the ring.
        check(whollyInsideCircle(lineFrom({-50, 0}, {50, 0}), ref),
              "inside-circle: a chord between two points of the ring is inside it");
        // Wholly within, touching nothing.
        check(whollyInsideCircle(lineFrom({-30, 0}, {30, 0}), ref),
              "inside-circle: and so is something that never reaches the ring");
        // One end out: that crosses, and must NOT be swallowed whole.
        check(!whollyInsideCircle(lineFrom({0, 0}, {100, 0}), ref),
              "inside-circle: one end outside means it crosses, not that it is inside");
        check(!whollyInsideCircle(lineFrom({-100, 0}, {100, 0}), ref),
              "inside-circle: and a line right through it is not inside it");

        // A polyline is inside only when every vertex is.
        Entity poly;
        poly.type = EntityType::Polyline;
        poly.points = {{-30, -30}, {0, 20}, {30, -30}};
        check(whollyInsideCircle(poly, ref), "inside-circle: every vertex of a polyline inside");
        poly.points[1] = {0, 80};
        check(!whollyInsideCircle(poly, ref), "inside-circle: one vertex out and it is not");

        // A smaller circle within the ring; and one that pokes out.
        Entity small;
        small.type = EntityType::Circle;
        small.center = {10, 0};
        small.radius = 20.0;
        check(whollyInsideCircle(small, ref), "inside-circle: a smaller ring within it");
        small.radius = 45.0;
        check(!whollyInsideCircle(small, ref), "inside-circle: unless it pokes out");

        // An ARC is not a closed boundary, so nothing is "inside" it - trimming to an arc has to go
        // through the ordinary crossing path.
        Entity arcRef;
        arcRef.type = EntityType::Arc;
        arcRef.center = {0, 0};
        arcRef.radius = 50.0;
        arcRef.startAngle = 0.0;
        arcRef.endAngle = 3.14159265358979323846;
        check(!whollyInsideCircle(lineFrom({-30, 0}, {30, 0}), arcRef),
              "inside-circle: an arc encloses nothing, so nothing is inside it");
    }

    // --- The Trim tool removes such an object outright, and only when the fence crossed it.
    {
        Document d;
        d.entities().clear();
        Entity circ;
        circ.id = "c";
        circ.type = EntityType::Circle;
        circ.center = {0, 0};
        circ.radius = 50.0;
        d.entities().push_back(circ);
        Entity chord;              // both ends on the ring: nothing outside to keep
        chord.id = "chord";
        chord.type = EntityType::Line;
        chord.a = {-50, 0};
        chord.b = {50, 0};
        d.entities().push_back(chord);
        Entity bystander;          // also inside, but the fence will not touch it
        bystander.id = "bystander";
        bystander.type = EntityType::Line;
        bystander.a = {-30, 30};
        bystander.b = {30, 30};
        d.entities().push_back(bystander);

        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.setPixelTolerance(3);
        ed.selectById("c");
        ed.setTool(ToolId::Trim);
        // A fence across the chord only, well below the bystander.
        ed.onPointerPress({20, -12}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({20, 12});
        ed.onPointerPress({20, 12}, Qt::LeftButton, Qt::NoModifier);

        bool hasChord = false, hasBystander = false, hasCircle = false;
        for (const Entity& e : d.entities()) {
            if (e.id == QLatin1String("chord")) hasChord = true;
            if (e.id == QLatin1String("bystander")) hasBystander = true;
            if (e.id == QLatin1String("c")) hasCircle = true;
        }
        check(!hasChord, "trim-inside: the chord the fence crossed is removed outright");
        check(hasBystander,
              "trim-inside: another object inside the circle is left alone - the fence decides");
        check(hasCircle, "trim-inside: and the reference itself is never touched");
    }

    // --- Moving a trimmed double line carries its cut line along (no distortion).
    {
        Entity dl; dl.type = EntityType::DoubleLine; dl.a = {0, 0}; dl.b = {100, 0};
        dl.meta.insert("gap", 10.0);
        dl.meta.insert("cutEnd", true);
        dl.meta.insert("cutEndKind", QStringLiteral("line"));
        dl.meta.insert("cutEndAx", 50.0); dl.meta.insert("cutEndAy", -10.0);
        dl.meta.insert("cutEndBx", 50.0); dl.meta.insert("cutEndBy", 10.0);
        translateEntity(dl, {10, 20});
        check(near(dl.a.x, 10.0) && near(dl.meta.value("cutEndAx").toDouble(), 60.0) &&
                  near(dl.meta.value("cutEndAy").toDouble(), 10.0),
              "move-doublecut: the trim cut line moves with the double line");
    }

    // --- Reference + fence trim: select a cutting edge, then drag a cut line across
    //     several objects; each is trimmed back to the reference.
    {
        Document d; d.entities().clear();
        Entity ref; ref.id = "ref"; ref.type = EntityType::Line; ref.a = {50, -100}; ref.b = {50, 100};
        Entity a; a.id = "a"; a.type = EntityType::Line; a.a = {0, 10}; a.b = {100, 10};
        Entity b; b.id = "b"; b.type = EntityType::Line; b.a = {0, 20}; b.b = {100, 20};
        Entity c; c.id = "c"; c.type = EntityType::Line; c.a = {0, 30}; c.b = {100, 30};
        d.entities().push_back(ref);
        d.entities().push_back(a);
        d.entities().push_back(b);
        d.entities().push_back(c);
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(3);
        ed.selectById("ref");          // the cutting edge / reference
        ed.setTool(ToolId::Trim);      // captures "ref" as the reference
        // Cut line by two clicks: a vertical line at x=80 (right of the reference)
        // across all three horizontals.
        ed.onPointerPress({80, 5}, Qt::LeftButton, Qt::NoModifier);   // cut-line start
        ed.onPointerMove({80, 35});
        ed.onPointerPress({80, 35}, Qt::LeftButton, Qt::NoModifier);  // cut-line end -> apply
        auto endX = [&](const QString& id) {
            const Entity* e = nullptr;
            for (const auto& en : d.entities()) if (en.id == id) e = &en;
            return e ? std::max(e->a.x, e->b.x) : -1.0;
        };
        check(near(endX("a"), 50.0) && near(endX("b"), 50.0) && near(endX("c"), 50.0),
              "trim-fence: every crossed line trimmed back to the reference");
        // The reference itself is untouched.
        const Entity* r = nullptr;
        for (const auto& en : d.entities()) if (en.id == "ref") r = &en;
        check(r && near(std::abs(r->b.y - r->a.y), 200.0), "trim-fence: reference is not trimmed");
    }

    // --- Only the REFERENCE bounds a trim. Another object lying across the same line is not a
    //     boundary, however close it is: trimming to an object means trimming to THAT object.
    //     (It used to stop at any object on the sheet, which meant a second circle overlapping the
    //     reference cut the removal short and left material inside the reference.)
    {
        Document d; d.entities().clear();
        Entity ref; ref.id = "ref"; ref.type = EntityType::Line; ref.a = {50, -100}; ref.b = {50, 100};
        Entity h; h.id = "h"; h.type = EntityType::Line; h.a = {0, 0}; h.b = {100, 0};
        Entity j; j.id = "j"; j.type = EntityType::Line; j.a = {70, -10}; j.b = {70, 10}; // NOT selected
        d.entities().push_back(ref);
        d.entities().push_back(h);
        d.entities().push_back(j);
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(3);
        ed.selectById("ref");
        ed.setTool(ToolId::Trim);
        // Cut line at x=60: past the reference at x=50, and past the unselected line at x=70.
        ed.onPointerPress({60, -5}, Qt::LeftButton, Qt::NoModifier); // cut-line start
        ed.onPointerMove({60, 5});
        ed.onPointerPress({60, 5}, Qt::LeftButton, Qt::NoModifier);  // cut-line end -> apply
        // The reference gives one cut, at x=50, and the fence is past it - so the tail goes and one
        // piece [0..50] is left. The line at x=70 does not bound it.
        int pieces = 0;
        bool trimmedToRef = false;
        for (const auto& e : d.entities()) {
            if (e.type != EntityType::Line || !(near(e.a.y, 0.0) && near(e.b.y, 0.0))) continue;
            ++pieces;
            if (near(std::min(e.a.x, e.b.x), 0.0) && near(std::max(e.a.x, e.b.x), 50.0))
                trimmedToRef = true;
        }
        check(pieces == 1 && trimmedToRef,
              "trim-ref-only: trimmed to the reference at 50, ignoring the object at 70");
        // The reference is still selected for further cuts (until Esc).
        check(ed.selection().size() == 1 && ed.selection().front() == "ref",
              "trim-ref-only: reference stays selected after trimming");
    }

    // --- Two overlapping circles, one of them the reference: only the reference's inside goes.
    {
        Document d;
        d.entities().clear();
        Entity small;                       // the reference
        small.id = "small";
        small.type = EntityType::Circle;
        small.center = {0, 0};
        small.radius = 50.0;
        d.entities().push_back(small);
        Entity big;                         // overlapping it, NOT selected
        big.id = "big";
        big.type = EntityType::Circle;
        big.center = {70, -55};
        big.radius = 75.0;                  // its ring meets y=0 at x = +19.01 and +120.99
        d.entities().push_back(big);
        Entity h;
        h.id = "h";
        h.type = EntityType::Line;
        h.a = {-160, 0};
        h.b = {200, 0};
        d.entities().push_back(h);

        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.setPixelTolerance(3);
        ed.selectById("small");
        ed.setTool(ToolId::Trim);
        ed.onPointerPress({-20, -12}, Qt::LeftButton, Qt::NoModifier);   // fence inside the small one
        ed.onPointerMove({-20, 12});
        ed.onPointerPress({-20, 12}, Qt::LeftButton, Qt::NoModifier);

        bool left = false, right = false;
        int pieces = 0;
        for (const Entity& e : d.entities()) {
            if (e.type != EntityType::Line) continue;
            ++pieces;
            if (near(std::max(e.a.x, e.b.x), -50.0, 1e-6)) left = true;
            if (near(std::min(e.a.x, e.b.x), 50.0, 1e-6)) right = true;
        }
        check(pieces == 2, "trim-two-circles: the line is split in two");
        check(left && right,
              "trim-two-circles: both new ends are on the REFERENCE ring (-50 and +50)");
        // The unselected circle's crossing at +19.01 must not appear as an end.
        bool onBigRing = false;
        for (const Entity& e : d.entities()) {
            if (e.type != EntityType::Line) continue;
            if (near(std::min(e.a.x, e.b.x), 19.01, 0.05) ||
                near(std::max(e.a.x, e.b.x), 19.01, 0.05))
                onBigRing = true;
        }
        check(!onBigRing, "trim-two-circles: and the circle nobody selected did not bound it");
    }

    // --- Trim to a DOUBLE LINE reference stops at the near PHYSICAL line, not the
    //     centerline between the two lines.
    {
        Document d; d.entities().clear();
        Entity dl; dl.id = "dl"; dl.type = EntityType::DoubleLine;
        dl.a = {50, -100}; dl.b = {50, 100}; dl.meta.insert("gap", 20.0); // physical lines x=40 and x=60
        Entity h; h.id = "h"; h.type = EntityType::Line; h.a = {0, 0}; h.b = {100, 0};
        d.entities().push_back(dl);
        d.entities().push_back(h);
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(3);
        ed.selectById("dl");           // double line is the reference
        ed.setTool(ToolId::Trim);
        // Cut line crossing h on the right side (x=80).
        ed.onPointerPress({80, -5}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({80, 5});
        ed.onPointerPress({80, 5}, Qt::LeftButton, Qt::NoModifier);
        const Entity* hh = nullptr;
        for (const auto& e : d.entities()) if (e.id == "h") hh = &e;
        // The right end is removed up to the NEAR physical line at x=60 (not 50).
        check(hh && near(std::max(hh->a.x, hh->b.x), 60.0),
              "trim-double: trims to the near physical line (x=60), not the centerline (x=50)");
    }

    // --- Trim a DOUBLE LINE object at a reference line (the centerline is trimmed;
    //     both physical lines follow).
    {
        Document d; d.entities().clear();
        Entity ref; ref.id = "ref"; ref.type = EntityType::Line; ref.a = {50, -100}; ref.b = {50, 100};
        Entity dl; dl.id = "dl"; dl.type = EntityType::DoubleLine;
        dl.a = {0, 0}; dl.b = {100, 0}; dl.meta.insert("gap", 10.0);
        d.entities().push_back(ref);
        d.entities().push_back(dl);
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(3);
        ed.selectById("ref");          // the single line is the reference
        ed.setTool(ToolId::Trim);
        // Cut line across the double line on the right side (x=80).
        ed.onPointerPress({80, -8}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({80, 8});
        ed.onPointerPress({80, 8}, Qt::LeftButton, Qt::NoModifier);
        const Entity* dd = nullptr;
        for (const auto& e : d.entities()) if (e.id == "dl") dd = &e;
        check(dd && dd->type == EntityType::DoubleLine && near(std::max(dd->a.x, dd->b.x), 50.0),
              "trim-doubleobj: double line trimmed back to the reference (centerline to x=50)");
        // The trimmed end records the exact cut line so both physical runs meet it.
        check(dd && dd->meta.value("cutEnd").toBool() &&
                  near(dd->meta.value("cutEndAx").toDouble(), 50.0),
              "trim-doubleobj: exact cut line (the reference) recorded at the trimmed end");
    }

    // --- A polyline's own CORNER is not a cutting edge: one fence, and everything from it to the end
    //     of what was picked goes - corner and all. It used to stop at the corner and leave a stub, so
    //     trimming a bent run took two or three goes and left ends nobody asked for.
    {
        std::vector<Vec2> pts = {{0, 0}, {100, 0}, {100, 100}}; // an L (corner at 100,0)
        Entity ref; ref.id = "r"; ref.type = EntityType::Line; ref.a = {50, -10}; ref.b = {50, 10};
        std::vector<Entity> cutters = {ref};
        auto pieces = trimPolylinePieces(pts, {70, 0} /* on seg0, past the ref */, cutters, "");
        check(pieces.size() == 1, "trim-poly-corner: one cutting edge leaves ONE piece");
        check(pieces.size() == 1 && pieces[0].size() == 2 && near(pieces[0].back().x, 50.0) &&
                  near(pieces[0].back().y, 0.0),
              "trim-poly-corner: what is left ends at the fence, not at the corner");
    }

    // --- The piece BETWEEN two crossings goes even when the run bends inside it, and the cutter can
    //     itself be a polyline. This is the case that used to come away in parts.
    {
        std::vector<Vec2> pts = {{0, 0}, {100, 0}, {200, 60}};   // bends at (100,0)
        Entity cut; cut.id = "p"; cut.type = EntityType::Polyline;
        cut.points = {{60, -40}, {60, 40}, {140, 40}, {140, -40}};  // crosses at x=60 and x=140
        for (const Vec2& pick : {Vec2{80, 0}, Vec2{100, 0}}) {      // before the bend, and at it
            auto pieces = trimPolylinePieces(pts, pick, {cut}, "");
            check(pieces.size() == 2, "trim-poly-poly: the picked span comes away in one go");
            if (pieces.size() != 2) continue;
            check(near(pieces[0].back().x, 60.0) && near(pieces[0].back().y, 0.0),
                  "trim-poly-poly: the near piece ends at the first crossing");
            check(near(pieces[1].front().x, 140.0) && near(pieces[1].front().y, 24.0),
                  "trim-poly-poly: the far piece starts at the second, and the bend between them "
                  "went with the span");
        }
    }

    // --- Trim a DOUBLE LINE with a CIRCLE reference: it splits at the ring and each
    //     new end records a CIRCLE cut so both physical runs clip to the arc.
    {
        Document d; d.entities().clear();
        Entity circ; circ.id = "c"; circ.type = EntityType::Circle; circ.center = {100, 0}; circ.radius = 40;
        Entity dl; dl.id = "dl"; dl.type = EntityType::DoubleLine;
        dl.a = {0, 0}; dl.b = {200, 0}; dl.meta.insert("gap", 10.0); // crosses ring at x=60,140
        d.entities().push_back(circ);
        d.entities().push_back(dl);
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(3);
        ed.selectById("c");
        ed.setTool(ToolId::Trim);
        ed.onPointerPress({100, -8}, Qt::LeftButton, Qt::NoModifier); // cut inside the circle
        ed.onPointerMove({100, 8});
        ed.onPointerPress({100, 8}, Qt::LeftButton, Qt::NoModifier);
        const Entity* dd = nullptr;
        for (const auto& e : d.entities()) if (e.id == "dl") dd = &e;
        check(dd && near(std::max(dd->a.x, dd->b.x), 60.0) &&
                  dd->meta.value("cutEnd").toBool() &&
                  dd->meta.value("cutEndKind").toString() == QLatin1String("circle") &&
                  near(dd->meta.value("cutEndR").toDouble(), 40.0),
              "trim-circle-ref: double line clips to the circle (records a circle cut)");
    }

    // --- Trim a polyline with a CIRCLE as the reference (cutting edge).
    {
        std::vector<Vec2> pts = {{0, 0}, {200, 0}};
        Entity circ; circ.id = "c"; circ.type = EntityType::Circle; circ.center = {100, 0}; circ.radius = 20;
        std::vector<Entity> cutters = {circ};
        auto pieces = trimPolylinePieces(pts, {140, 0} /* past the circle */, cutters, "");
        check(pieces.size() == 1 && near(pieces[0].back().x, 120.0),
              "trim-poly-circle: trims to the circle crossing (x=120)");
    }

    // --- Trim a polyline with an ELLIPSE as the reference (now tessellated).
    {
        std::vector<Vec2> pts = {{-200, 0}, {200, 0}};
        Entity el; el.id = "e"; el.type = EntityType::Ellipse;
        el.center = {0, 0}; el.radius = 100; el.radiusY = 50; el.rotation = 0; // crosses at x=+/-100
        std::vector<Entity> cutters = {el};
        auto pieces = trimPolylinePieces(pts, {0, 0} /* inside */, cutters, "");
        check(pieces.size() == 2 && near(pieces[0].back().x, -100.0, 1.0) &&
                  near(pieces[1].front().x, 100.0, 1.0),
              "trim-poly-ellipse: split at the ellipse crossings (x=+/-100)");
    }

    // --- A single polyline segment that passes THROUGH a circle (entry+exit on the
    //     same segment) is split, removing the inside span.
    {
        std::vector<Vec2> pts = {{-200, 0}, {200, 0}};
        Entity circ; circ.id = "c"; circ.type = EntityType::Circle; circ.center = {0, 0}; circ.radius = 50;
        std::vector<Entity> cutters = {circ}; // one segment crosses the ring at x=-50 and x=50
        auto pieces = trimPolylinePieces(pts, {0, 0} /* inside */, cutters, "");
        check(pieces.size() == 2 && near(pieces[0].back().x, -50.0, 1.0) &&
                  near(pieces[1].front().x, 50.0, 1.0),
              "trim-thru-circle: one segment crossing a circle twice splits at both crossings");
    }

    // --- A corner INSIDE the circle goes with the span: trimming to a ring removes what is inside
    //     THAT ring, which is the whole point of picking it, corner included.
    {
        std::vector<Vec2> pts = {{0, 0}, {150, 0}, {150, 100}}; // corner at (150,0)
        Entity circ; circ.id = "c"; circ.type = EntityType::Circle; circ.center = {100, 0}; circ.radius = 60;
        std::vector<Entity> cutters = {circ}; // ring crosses seg0 at x=40 and seg1 at (150,~33.2)
        auto pieces = trimPolylinePieces(pts, {100, 0} /* inside, before the corner */, cutters, "");
        check(pieces.size() == 2 && near(pieces[0].back().x, 40.0),
              "trim-poly-circle-corner: the first piece ends at the ring entry (x=40)");
        check(pieces.size() == 2 && near(pieces[1].front().x, 150.0) &&
                  near(pieces[1].front().y, 33.166, 0.01),
              "trim-poly-circle-corner: and the second starts where the ring leaves, not at the "
              "corner inside it");
    }

    // --- Trim a CIRCLE: two cutting lines through it; the picked quadrant is
    //     removed and the circle becomes an arc spanning the rest.
    {
        Entity circ; circ.id = "c"; circ.type = EntityType::Circle; circ.center = {0, 0}; circ.radius = 10;
        Entity v; v.type = EntityType::Line; v.a = {0, -20}; v.b = {0, 20};   // cuts at 90 & 270 deg
        Entity hl; hl.type = EntityType::Line; hl.a = {-20, 0}; hl.b = {20, 0}; // cuts at 0 & 180 deg
        std::vector<Entity> cutters = {v, hl};
        std::optional<Entity> extra;
        bool ok = trimCircleAt(circ, {7.07, 7.07} /* ~45 deg */, cutters, extra);
        check(ok && circ.type == EntityType::Arc && !extra, "trim-circle: circle becomes an arc");
        check(near(circ.startAngle, M_PI / 2, 1e-3) && near(circ.endAngle, 0.0, 1e-3),
              "trim-circle: the picked quadrant (45 deg) is removed");
    }

    // --- Trim an ARC: a cut inside the sweep removes the picked end piece.
    {
        Entity arc; arc.id = "a"; arc.type = EntityType::Arc; arc.center = {0, 0}; arc.radius = 10;
        arc.startAngle = 0; arc.endAngle = M_PI; // upper semicircle
        Entity v; v.type = EntityType::Line; v.a = {0, -20}; v.b = {0, 20}; // crosses arc at 90 deg
        std::vector<Entity> cutters = {v};
        std::optional<Entity> extra;
        bool ok = trimCircleAt(arc, {7.07, 7.07} /* 45 deg, on the arc */, cutters, extra);
        check(ok && !extra && near(arc.startAngle, M_PI / 2, 1e-3) && near(arc.endAngle, M_PI, 1e-3),
              "trim-arc: the start-side piece up to the cut is removed");
    }

    // --- Extend with a reference + fence line: a line the fence crosses is extended
    //     to reach the selected boundary.
    {
        Document d; d.entities().clear();
        Entity ref; ref.id = "ref"; ref.type = EntityType::Line; ref.a = {100, -100}; ref.b = {100, 100};
        Entity h; h.id = "h"; h.type = EntityType::Line; h.a = {0, 0}; h.b = {50, 0}; // stops short of x=100
        d.entities().push_back(ref);
        d.entities().push_back(h);
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(3);
        ed.selectById("ref");          // the boundary to extend to
        ed.setTool(ToolId::Extend);
        // Fence line across h near its right end (x=40).
        ed.onPointerPress({40, -5}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({40, 5});
        ed.onPointerPress({40, 5}, Qt::LeftButton, Qt::NoModifier);
        const Entity* hh = nullptr;
        for (const auto& e : d.entities()) if (e.id == "h") hh = &e;
        check(hh && near(std::max(hh->a.x, hh->b.x), 100.0),
              "extend-fence: the crossed line is extended to the reference (x=100)");
    }

    // --- Extending a DOUBLE line to a boundary records a cut cap so its physical runs
    //     end flush on the boundary (they must touch the target).
    {
        Document d; d.entities().clear();
        Entity ref; ref.id = "ref"; ref.type = EntityType::Line; ref.a = {100, -100}; ref.b = {100, 100};
        Entity dl; dl.id = "dl"; dl.type = EntityType::DoubleLine; dl.a = {0, 0}; dl.b = {50, 0};
        dl.meta.insert("gap", 10.0);
        d.entities().push_back(ref);
        d.entities().push_back(dl);
        std::vector<Entity> refs = {ref};
        // Fence pick nearer b (right end) so b extends to x=100.
        extendEntityTo(*[&]{ for (auto& e : d.entities()) if (e.id=="dl") return &e; return (Entity*)nullptr; }(),
                       Vec2{50, 0}, refs);
        const Entity* dd = nullptr;
        for (const auto& e : d.entities()) if (e.id == "dl") dd = &e;
        const QJsonArray cap = dd ? dd->meta.value("cutEndPoly").toArray() : QJsonArray{};
        check(dd && near(dd->b.x, 100.0) && dd->meta.value("cutEnd").toBool() &&
                  dd->meta.value("cutEndKind").toString() == QLatin1String("poly") &&
                  cap.size() >= 4 && near(cap[0].toDouble(), 100.0),
              "extend-double: physical runs are capped flush to the extend target");
    }

    // --- Fillet radius 0 (AutoCAD): no arc; both lines are trimmed to meet at the
    //     corner.
    {
        Document d; d.entities().clear();
        Entity h; h.id = "h"; h.type = EntityType::Line; h.a = {0, 0}; h.b = {100, 0};
        Entity v; v.id = "v"; v.type = EntityType::Line; v.a = {0, 0}; v.b = {0, 100};
        d.entities().push_back(h);
        d.entities().push_back(v);
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(3);
        ed.setFilletRadius(0.0);
        ed.setTool(ToolId::Fillet);
        ed.onPointerPress({60, 0}, Qt::LeftButton, Qt::NoModifier);  // pick h (keep far end)
        ed.onPointerPress({0, 60}, Qt::LeftButton, Qt::NoModifier);  // pick v -> apply
        bool anyArc = false;
        for (const auto& e : d.entities()) if (e.type == EntityType::Arc) anyArc = true;
        check(d.entities().size() == 2 && !anyArc,
              "fillet-r0: no arc added (sharp corner join)");
    }

    // --- Polygon centre object-snap joint.
    {
        Document d; d.entities().clear();
        Entity poly; poly.id = "p"; poly.type = EntityType::Polygon;
        poly.points = {{0, 0}, {10, 0}, {10, 10}, {0, 10}}; // centroid (5,5)
        d.entities().push_back(poly);
        SnapSettings s; s.objectSnap = true; s.gridSnap = false;
        SnapResult r = resolveSnap(d, {5.4, 5.4}, std::nullopt, 2.0, s);
        check(near(r.point.x, 5.0) && near(r.point.y, 5.0),
              "polygon: object snap catches the centre joint");
    }


    // --- New polyline starts with no fillet; a radius set mid-draw applies only to
    // corners formed afterward (per-corner cornerRadii), never retroactively.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setTool(ToolId::Polyline);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({10, 0}, Qt::LeftButton, Qt::NoModifier); // corner@v1 forms next
        ed.setCornerRadius(2.0);                                    // define fillet now
        ed.onPointerPress({10, 10}, Qt::LeftButton, Qt::NoModifier); // completes corner@v1 (r=2)
        ed.onPointerPress({20, 10}, Qt::LeftButton, Qt::NoModifier); // completes corner@v2 (r=2)
        ed.onKey(Qt::Key_Return, Qt::NoModifier);                    // finish the polyline
        check(d.entities().size() == 1, "polyfillet: one polyline created");
        const QJsonArray rArr = d.entities()[0].meta.value("cornerRadii").toArray();
        check(rArr.size() == 4, "polyfillet: per-corner radii stored for every vertex");
        check(near(rArr[0].toDouble(), 0.0) && near(rArr[1].toDouble(), 2.0) &&
                  near(rArr[2].toDouble(), 2.0) && near(rArr[3].toDouble(), 0.0),
              "polyfillet: radius applies from the next joint onward, not retroactively");
    }

    // --- Per-corner rounding: only the corner with a radius gets an arc inserted.
    {
        std::vector<Vec2> pts = {{0, 0}, {10, 0}, {10, 10}, {20, 10}};
        std::vector<double> radii = {0, 3, 0, 0}; // round only the corner at index 1
        auto out = roundedPolylinePerCorner(pts, radii);
        check(out.size() > pts.size(), "perCorner: filleted corner adds arc points");
        // The sharp corner (index 2 = (10,10)) must remain a literal vertex.
        bool keptSharp = false;
        for (const auto& v : out) if (near(v.x, 10.0) && near(v.y, 10.0)) keptSharp = true;
        check(keptSharp, "perCorner: un-filleted corner stays sharp");
    }

    // --- Match properties: click a source, then paint targets with its properties.
    {
        Document doc;
        doc.entities().clear();
        Entity src;
        src.id = "src";
        src.type = EntityType::Line;
        src.a = {0, 0};
        src.b = {10, 0};
        src.layer = "trim";
        src.color = "#ff0000";
        src.weight = 1.4;
        Entity dst;
        dst.id = "dst";
        dst.type = EntityType::Line;
        dst.a = {0, 20};
        dst.b = {10, 20};
        doc.entities().push_back(src);
        doc.entities().push_back(dst);
        Editor ed;
        ed.setDocument(&doc);
        noSnap(ed);
        ed.beginMatchProps();
        check(ed.awaitingMatchProps(), "match: armed by beginMatchProps");
        ed.onPointerPress({5, 0}, Qt::LeftButton, Qt::NoModifier); // pick the source
        check(ed.awaitingMatchProps() && ed.selection().size() == 1 &&
                  ed.selection().front() == QLatin1String("src"),
              "match: the first click captures the source and stays armed");
        ed.onPointerPress({5, 20}, Qt::LeftButton, Qt::NoModifier); // paint the target
        const Entity* t = nullptr;
        for (const auto& e : doc.entities())
            if (e.id == QLatin1String("dst")) t = &e;
        check(t && t->color == QLatin1String("#ff0000") && t->layer == QLatin1String("trim") &&
                  near(t->weight, 1.4),
              "match: colour / layer / lineweight copied onto the target");
        check(ed.awaitingMatchProps(), "match: stays armed for more targets");
    }

    // --- Match properties the other way round: select the object, THEN press Match.
    // The pre-selected object must become the source, so the very next click paints.
    {
        Document doc;
        doc.entities().clear();
        Entity src;
        src.id = "src";
        src.type = EntityType::Circle;
        src.center = {0, 0};
        src.radius = 5;
        src.color = "#00ff00";
        src.weight = 0.7;
        Entity dst;
        dst.id = "dst";
        dst.type = EntityType::Circle;
        dst.center = {40, 0};
        dst.radius = 12;
        doc.entities().push_back(src);
        doc.entities().push_back(dst);
        Editor ed;
        ed.setDocument(&doc);
        noSnap(ed);
        ed.selectById("src");
        ed.beginMatchProps();
        check(ed.awaitingMatchProps() && ed.matchSource() == QLatin1String("src"),
              "match: a pre-selected object is taken as the source");
        ed.onPointerPress({45, 0}, Qt::LeftButton, Qt::NoModifier); // one click paints
        const Entity* t = nullptr;
        for (const auto& e : doc.entities())
            if (e.id == QLatin1String("dst")) t = &e;
        check(t && t->color == QLatin1String("#00ff00") && near(t->weight, 0.7),
              "match: the first click after arming paints, it does not re-pick the source");
        check(t && near(t->radius, 5.0),
              "match: same-type objects also take the source's geometry");
    }

    // --- Quick Select: the property list offered follows the object's type.
    {
        Entity c;
        c.type = EntityType::Circle;
        c.center = {0, 0};
        c.radius = 4;
        const auto cp = quickSelectProps(c);
        auto hasKey = [](const std::vector<QuickSelectProp>& ps, const char* k) {
            for (const auto& p : ps)
                if (p.key == QLatin1String(k)) return true;
            return false;
        };
        check(hasKey(cp, "layer") && hasKey(cp, "color") && hasKey(cp, "weight"),
              "qselect: the general properties are always offered");
        check(hasKey(cp, "radius") && hasKey(cp, "diameter") && hasKey(cp, "area"),
              "qselect: a circle offers radius / diameter / area");
        check(!hasKey(cp, "meta:wireGauge"), "qselect: a circle does not offer wire properties");

        Entity w;
        w.type = EntityType::Wire;
        w.points = {{0, 0}, {30, 0}};
        w.meta.insert("wireGauge", QStringLiteral("0.75"));
        w.meta.insert("netName", QStringLiteral("GND"));
        const auto wp = quickSelectProps(w);
        check(hasKey(wp, "meta:wireGauge") && hasKey(wp, "meta:netName") && hasKey(wp, "length"),
              "qselect: a wire offers its gauge / net / length");
        check(!hasKey(wp, "radius"), "qselect: a wire does not offer a radius");

        // Values read back as the text the dialog compares against.
        check(quickSelectValue(c, QStringLiteral("radius")) == QLatin1String("4"),
              "qselect: numeric values format without trailing zeros");
        check(quickSelectValue(c, QStringLiteral("diameter")) == QLatin1String("8"),
              "qselect: diameter is derived from the radius");
        check(quickSelectValue(c, QStringLiteral("color")) == QLatin1String("ByLayer"),
              "qselect: an unset colour reads as ByLayer");
        check(quickSelectValue(w, QStringLiteral("meta:netName")) == QLatin1String("GND"),
              "qselect: meta values read through");
        check(near(quickSelectValue(w, QStringLiteral("length")).toDouble(), 30.0),
              "qselect: a wire's length is its run length");
    }

    // --- Quick Select: filtering, operators, type restriction, exclude and append.
    {
        std::vector<Entity> pool;
        auto circle = [&pool](const char* id, double r, const char* layer) {
            Entity e;
            e.id = QString::fromLatin1(id);
            e.type = EntityType::Circle;
            e.radius = r;
            e.layer = QString::fromLatin1(layer);
            pool.push_back(e);
        };
        circle("c1", 5, "0");
        circle("c2", 10, "0");
        circle("c3", 10, "trim");
        Entity ln;
        ln.id = "l1";
        ln.type = EntityType::Line;
        ln.a = {0, 0};
        ln.b = {10, 0};
        ln.layer = "0";
        pool.push_back(ln);

        QuickSelectQuery q;
        q.key = QStringLiteral("layer");
        q.op = QsOp::Equal;
        q.value = QStringLiteral("0");
        auto ids = quickSelectMatchIds(pool, q, {});
        check(ids.size() == 3, "qselect: layer = 0 matches every type on that layer");

        q.type = QStringLiteral("circle");
        ids = quickSelectMatchIds(pool, q, {});
        check(ids.size() == 2, "qselect: the object type narrows the filter");

        q.type.clear();
        q.key = QStringLiteral("radius");
        q.op = QsOp::Greater;
        q.value = QStringLiteral("6");
        ids = quickSelectMatchIds(pool, q, {});
        check(ids.size() == 2 && ids[0] == QLatin1String("c2"),
              "qselect: numeric operators compare as numbers");

        q.exclude = true;
        ids = quickSelectMatchIds(pool, q, {});
        check(ids.size() == 2, "qselect: exclude selects what does not match");

        // Select All ignores the property and matches on type alone.
        q.exclude = false;
        q.op = QsOp::All;
        q.type = QStringLiteral("line");
        ids = quickSelectMatchIds(pool, q, {});
        check(ids.size() == 1 && ids[0] == QLatin1String("l1"),
              "qselect: Select All matches every object of the type");

        // Applying to the current selection filters within it; append keeps it.
        q.op = QsOp::Equal;
        q.type.clear();
        q.key = QStringLiteral("layer");
        q.value = QStringLiteral("0");
        q.wholeDrawing = false;
        ids = quickSelectMatchIds(pool, q, {QStringLiteral("c1"), QStringLiteral("c3")});
        check(ids.size() == 1 && ids[0] == QLatin1String("c1"),
              "qselect: applying to the current selection filters within it");
        q.wholeDrawing = true;
        q.append = true;
        ids = quickSelectMatchIds(pool, q, {QStringLiteral("c3")});
        check(ids.size() == 4 && ids[0] == QLatin1String("c3"),
              "qselect: append keeps the current selection and adds the matches once each");

        const auto vals = quickSelectDistinctValues(pool, QStringLiteral("radius"));
        check(vals.size() == 3 && vals.front() == QLatin1String("0"),
              "qselect: distinct values are collected and sorted numerically");
    }

    // --- Quick Select session: armed, Enter opens it, and the filter applies.
    {
        Document doc;
        doc.entities().clear();
        for (int i = 0; i < 3; ++i) {
            Entity e;
            e.id = QStringLiteral("c%1").arg(i);
            e.type = EntityType::Circle;
            e.center = {i * 30.0, 0};
            e.radius = (i == 2) ? 9 : 4;
            doc.entities().push_back(e);
        }
        Editor ed;
        ed.setDocument(&doc);
        noSnap(ed);
        int opened = 0;
        QString sampleSeen;
        QObject::connect(&ed, &Editor::quickSelectRequested, &ed,
                         [&opened, &sampleSeen](const QString& id) {
                             ++opened;
                             sampleSeen = id;
                         });
        ed.beginQuickSelect();
        check(ed.awaitingQuickSelect() && opened == 0,
              "qselect session: with nothing selected it waits for the pick");
        ed.onPointerPress({0, 4}, Qt::LeftButton, Qt::NoModifier); // click the first circle
        ed.onKey(Qt::Key_Return, Qt::NoModifier);
        check(opened == 1 && sampleSeen == QLatin1String("c0"),
              "qselect session: Enter opens the dialog on the picked object");
        check(!ed.awaitingQuickSelect(), "qselect session: opening ends the pick phase");

        // The other order: something already selected opens it straight away.
        ed.selectById("c2");
        ed.beginQuickSelect();
        check(opened == 2 && sampleSeen == QLatin1String("c2"),
              "qselect session: a pre-selected object opens the dialog at once");

        QuickSelectQuery q;
        q.key = QStringLiteral("radius");
        q.op = QsOp::Equal;
        q.value = QStringLiteral("4");
        check(ed.applyQuickSelect(q) == 2 && ed.selection().size() == 2,
              "qselect session: applying the filter selects the matches");
    }

    // --- Rectangular array count.
    {
        Entity c; c.type = EntityType::Circle; c.center = {0, 0}; c.radius = 1;
        auto clones = rectArray(c, 2, 3, 10, 10);
        check(clones.size() == 5, "array: 2x3 minus origin = 5");
    }

    // --- Rectangular array placements: order, spacing, and the axis angle.
    {
        ArraySpec s;
        s.kind = ArrayKind::Rect;
        s.count = 3; // columns
        s.rows = 2;
        s.colSpacing = 10;
        s.rowSpacing = 5;
        auto pl = arrayPlacements(s, {0, 0});
        check(pl.size() == 6, "arrayrect: 3 cols x 2 rows = 6 items");
        check(near(pl[0].delta.x, 0) && near(pl[0].delta.y, 0) && near(pl[0].rotate, 0),
              "arrayrect: item 0 is the source, untouched");
        check(near(pl[2].delta.x, 20) && near(pl[2].delta.y, 0), "arrayrect: columns step in X");
        check(near(pl[3].delta.x, 0) && near(pl[3].delta.y, 5), "arrayrect: rows step in Y");
        s.axisAngle = 90.0; // the whole grid turns, staying rectangular
        pl = arrayPlacements(s, {0, 0});
        check(near(pl[1].delta.x, 0) && near(pl[1].delta.y, 10),
              "arrayrect: axis angle turns the column axis");
        check(near(pl[3].delta.x, -5) && near(pl[3].delta.y, 0),
              "arrayrect: the row axis stays perpendicular to it");
        s.axisAngle = 0.0;
        s.colSpacing = -10; // negative spacing arrays the other way
        pl = arrayPlacements(s, {0, 0});
        check(near(pl[1].delta.x, -10), "arrayrect: negative spacing goes leftward");
    }

    // --- Polar array: how the sweep divides, direction, rotation and rings.
    {
        ArraySpec s;
        s.kind = ArrayKind::Polar;
        s.center = {0, 0};
        s.count = 4;
        s.fillAngle = 360.0;
        auto pl = arrayPlacements(s, {10, 0});
        check(pl.size() == 4, "arraypolar: 4 items");
        check(near(pl[0].rotate, 0) && near(pl[0].delta.x, 0) && near(pl[0].delta.y, 0),
              "arraypolar: item 0 is the source, untouched");
        check(near(pl[1].rotate, M_PI / 2),
              "arraypolar: a full sweep divides by the item count");
        s.count = 3;
        s.fillAngle = 180.0;
        pl = arrayPlacements(s, {10, 0});
        check(near(pl[1].rotate, M_PI / 2), "arraypolar: a partial sweep divides by the gaps");
        s.ccw = false;
        pl = arrayPlacements(s, {10, 0});
        check(near(pl[1].rotate, -M_PI / 2), "arraypolar: CW sweeps the other way");
        s.ccw = true;
        s.rotateItems = false;
        pl = arrayPlacements(s, {10, 0});
        check(near(pl[1].rotate, 0) && near(pl[1].delta.x, -10) && near(pl[1].delta.y, 10),
              "arraypolar: un-rotated items still orbit the centre");
        // Rings: one item per ring, stepping outward along its own radius.
        ArraySpec rings;
        rings.kind = ArrayKind::Polar;
        rings.center = {0, 0};
        rings.count = 1;
        rings.rows = 3;
        rings.rowSpacing = 5;
        pl = arrayPlacements(rings, {10, 0});
        check(pl.size() == 3 && near(pl[1].delta.x, 5) && near(pl[1].delta.y, 0) &&
                  near(pl[2].delta.x, 10),
              "arraypolar: rows are concentric rings");
    }

    // --- Path array: Divide vs Measure, alignment, and closed paths.
    {
        const std::vector<Vec2> line = {{0, 0}, {100, 0}};
        ArraySpec s;
        s.kind = ArrayKind::Path;
        s.count = 5;
        s.divide = true;
        auto pl = arrayPlacements(s, {0, 50}, line, false);
        check(pl.size() == 5, "arraypath: 5 items");
        check(near(pl[0].rotate, 0) && near(pl[0].delta.x, 0) && near(pl[0].delta.y, -50),
              "arraypath: the source drops onto the start of the path");
        check(near(pl[2].delta.x, 50) && near(pl[4].delta.x, 100),
              "arraypath: Divide spreads the items over the whole path");
        s.startOffset = 10;
        pl = arrayPlacements(s, {0, 50}, line, false);
        check(near(pl[0].delta.x, 10), "arraypath: the start offset shifts the whole run");
        // Measure steps by a fixed distance; items past the end of an open path
        // simply don't exist (0 / 40 / 80 fit in 100, 120 and 160 do not).
        s.startOffset = 0;
        s.divide = false;
        s.pathSpacing = 40;
        pl = arrayPlacements(s, {0, 0}, line, false);
        check(pl.size() == 3, "arraypath: Measure stops at the end of an open path");
        // Rows run across the path.
        s.rows = 2;
        s.rowSpacing = 7;
        pl = arrayPlacements(s, {0, 0}, line, false);
        check(pl.size() == 6 && near(pl[3].delta.y, 7),
              "arraypath: rows offset perpendicular to the path");
        // A closed path divides by the item count and wraps; aligned items follow
        // the tangent, so turning a corner turns the item.
        const std::vector<Vec2> loop = {{0, 0}, {10, 0}, {10, 10}, {0, 10}, {0, 0}};
        ArraySpec cs;
        cs.kind = ArrayKind::Path;
        cs.count = 4;
        cs.divide = true;
        cs.alignToPath = true;
        pl = arrayPlacements(cs, {0, 0}, loop, true);
        check(pl.size() == 4, "arraypath: a closed path divides by the item count");
        check(near(pl[2].rotate, M_PI / 2), "arraypath: aligned items follow the tangent");
        cs.alignToPath = false;
        pl = arrayPlacements(cs, {0, 0}, loop, true);
        check(near(pl[2].rotate, 0), "arraypath: un-aligned items keep their angle");
    }

    // --- Path extraction: which objects can be a path, and their length.
    {
        Entity r;
        r.type = EntityType::Rect;
        r.points = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
        bool closed = false;
        const auto pts = arrayPathPoints(r, &closed);
        check(closed && pts.size() == 5 && near(polylineLength(pts), 40.0),
              "arrayPathPoints: a rect closes its loop");
        Entity c;
        c.type = EntityType::Circle;
        c.center = {0, 0};
        c.radius = 10;
        bool circClosed = false;
        const auto cpts = arrayPathPoints(c, &circClosed);
        check(circClosed && near(polylineLength(cpts), 2.0 * M_PI * 10.0, 0.05),
              "arrayPathPoints: a circle tessellates to its circumference");
        Entity ln;
        ln.type = EntityType::Line;
        ln.a = {0, 0};
        ln.b = {3, 4};
        bool lineClosed = true;
        check(near(polylineLength(arrayPathPoints(ln, &lineClosed)), 5.0) && !lineClosed,
              "arrayPathPoints: a line is an open path");
        Entity tx;
        tx.type = EntityType::Text;
        check(arrayPathPoints(tx).empty(), "arrayPathPoints: text is not a path");
    }

    // --- Array session: arming, the polar centre pick, commit and cancel.
    {
        Document doc;
        doc.entities().clear();
        Entity c;
        c.id = "c1";
        c.type = EntityType::Circle;
        c.center = {10, 0};
        c.radius = 2;
        doc.entities().push_back(c);
        Editor ed;
        ed.setDocument(&doc);
        noSnap(ed);

        ed.beginArray(ArrayKind::Polar);
        check(!ed.arrayActive(), "array session: nothing selected arms nothing");

        ed.selectById("c1");
        ed.beginArray(ArrayKind::Polar);
        check(ed.arrayActive() && ed.arrayAwaitingCenter(),
              "array session: a polar array first wants a centre");
        ed.arraySpec().count = 4;
        ed.arraySpec().fillAngle = 360.0;
        ed.arraySpec().rows = 1;
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier); // the centre
        check(!ed.arrayAwaitingCenter(), "array session: the click sets the centre");
        check(ed.arrayItemCount() == 4, "array session: 4 items previewed");
        check(doc.entities().size() == 1, "array session: nothing placed until committed");
        ed.commitArray();
        check(!ed.arrayActive(), "array session: committing ends the session");
        check(doc.entities().size() == 4, "array session: 4 objects after the commit");
        bool atTop = false, atLeft = false;
        for (const auto& e : doc.entities()) {
            if (near(e.center.x, 0, 1e-6) && near(e.center.y, 10, 1e-6)) atTop = true;
            if (near(e.center.x, -10, 1e-6) && near(e.center.y, 0, 1e-6)) atLeft = true;
        }
        check(atTop && atLeft, "array session: items orbit the picked centre");
        check(ed.selection().size() == 4, "array session: the finished array is left selected");

        // Esc during a session throws the preview away and touches nothing.
        ed.beginArray(ArrayKind::Rect);
        check(ed.arrayActive(), "array session: rect arms straight away");
        const size_t before = doc.entities().size();
        ed.onKey(Qt::Key_Escape, Qt::NoModifier);
        check(!ed.arrayActive() && doc.entities().size() == before,
              "array session: Esc cancels without placing anything");
    }

    // --- Path array session: pick the path by clicking it, then Enter places it.
    {
        Document doc;
        doc.entities().clear();
        Entity path;
        path.id = "p1";
        path.type = EntityType::Line;
        path.a = {0, 0};
        path.b = {100, 0};
        doc.entities().push_back(path);
        Entity item;
        item.id = "s1";
        item.type = EntityType::Circle;
        item.center = {0, 50};
        item.radius = 1;
        doc.entities().push_back(item);
        Editor ed;
        ed.setDocument(&doc);
        noSnap(ed);
        ed.selectById("s1");
        ed.beginArray(ArrayKind::Path);
        check(ed.arrayAwaitingPath(), "path session: a path array first wants a path");
        check(ed.arrayItemCount() == 0, "path session: nothing to preview without a path");
        ed.onPointerPress({50, 0}, Qt::LeftButton, Qt::NoModifier); // click the line
        check(!ed.arrayAwaitingPath(), "path session: the click picks the path");
        ed.arraySpec().count = 5;
        ed.arraySpec().divide = true;
        ed.arraySpec().rows = 1;
        check(ed.arrayItemCount() == 5, "path session: 5 items previewed");
        ed.onKey(Qt::Key_Return, Qt::NoModifier); // Enter = Close Array
        check(!ed.arrayActive(), "path session: Enter places the array");
        check(doc.entities().size() == 6, "path session: the path plus 5 items");
        const Entity* src = nullptr;
        for (const auto& e : doc.entities())
            if (e.id == QLatin1String("s1")) src = &e;
        check(src && near(src->center.x, 0) && near(src->center.y, 0),
              "path session: the original moves onto the path");
        int onPath = 0;
        for (const auto& e : doc.entities())
            if (e.type == EntityType::Circle && near(e.center.y, 0)) ++onPath;
        check(onPath == 5, "path session: every item sits on the path");
    }

    // --- Explode a rect into 4 lines.
    {
        Entity r; r.type = EntityType::Rect;
        r.points = {{0, 0}, {10, 0}, {10, 5}, {0, 5}};
        auto parts = explodeEntity(r);
        check(parts.size() == 4, "explode: rect -> 4 lines");
    }

    // --- Selection: click, window, crossing, additive, Esc-clears.
    {
        auto makeDoc = [](Document& d) {
            d.entities().clear();
            Entity l1; l1.id = "l1"; l1.type = EntityType::Line; l1.a = {0, 0}; l1.b = {20, 0};
            Entity l2; l2.id = "l2"; l2.type = EntityType::Line; l2.a = {15, 0}; l2.b = {60, 0};
            d.entities().push_back(l1);
            d.entities().push_back(l2);
        };
        // Two-click rectangle: first click on empty anchors it, second click
        // commits (no lasso since the hold timer never fires in a unit test).
        auto regionDrag = [](Editor& ed, Vec2 a, Vec2 b, Qt::KeyboardModifiers m) {
            ed.setTool(ToolId::Select);
            ed.onPointerPress(a, Qt::LeftButton, m);   // first click -> Deciding
            ed.onPointerRelease(a, Qt::LeftButton);    // -> RectPending
            ed.onPointerMove(b);                       // rubber-band to opposite
            ed.onPointerPress(b, Qt::LeftButton, m);   // second click -> commit
        };

        // Click pick selects exactly the line under the cursor.
        {
            Document d; makeDoc(d);
            Editor ed; ed.setDocument(&d); noSnap(ed);
            ed.setTool(ToolId::Select);
            ed.onPointerPress({5, 0}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerRelease({5, 0}, Qt::LeftButton);
            check(ed.selection().size() == 1 && ed.selection().front() == "l1", "select: click picks one");
        }
        // Window (left->right) selects only fully-enclosed entities.
        {
            Document d; makeDoc(d);
            Editor ed; ed.setDocument(&d); noSnap(ed);
            regionDrag(ed, {-10, -10}, {40, 10}, Qt::NoModifier);
            check(ed.selection().size() == 1 && ed.selection().front() == "l1",
                  "select: window encloses l1 only");
        }
        // Crossing (right->left) selects anything it touches.
        {
            Document d; makeDoc(d);
            Editor ed; ed.setDocument(&d); noSnap(ed);
            regionDrag(ed, {40, 40}, {-10, -10}, Qt::NoModifier); // start in empty space
            check(ed.selection().size() == 2, "select: crossing grabs both");
        }
        // Plain clicks accumulate selection one-by-one; Shift-click deselects.
        {
            Document d; makeDoc(d);
            Editor ed; ed.setDocument(&d); noSnap(ed);
            ed.setTool(ToolId::Select);
            ed.onPointerPress({5, 0}, Qt::LeftButton, Qt::NoModifier);   // pick l1
            ed.onPointerRelease({5, 0}, Qt::LeftButton);
            ed.onPointerPress({50, 0}, Qt::LeftButton, Qt::NoModifier);  // add l2
            ed.onPointerRelease({50, 0}, Qt::LeftButton);
            check(ed.selection().size() == 2, "select: plain clicks accumulate");
            ed.onPointerPress({5, 0}, Qt::LeftButton, Qt::ShiftModifier); // shift removes l1
            ed.onPointerRelease({5, 0}, Qt::LeftButton);
            check(ed.selection().size() == 1 && ed.selection().front() == "l2",
                  "select: shift-click deselects");
            // Esc clears all.
            ed.onKey(Qt::Key_Escape, Qt::NoModifier);
            check(ed.selection().empty(), "select: Esc clears all");
        }
    }

    // --- Direct manipulation: drag body to move, drag grip to edit a joint.
    {
        // Dragging an object's BODY no longer moves it — moving needs the Move
        // command (or a grip). A body drag just leaves the geometry unchanged.
        {
            Document d; d.entities().clear();
            Entity l; l.id = "ln"; l.type = EntityType::Line; l.a = {0, 0}; l.b = {20, 0};
            d.entities().push_back(l);
            Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(2);
            ed.setTool(ToolId::Select);
            ed.selectById("ln");
            ed.onPointerPress({5, 0}, Qt::LeftButton, Qt::NoModifier); // body, not a grip
            ed.onPointerMove({5, 8});
            ed.onPointerRelease({5, 8}, Qt::LeftButton);
            check(near(d.entities()[0].a.y, 0.0) && near(d.entities()[0].b.y, 0.0),
                  "edit: dragging the body does NOT move the object");
        }
        // A double polyline exposes joints on its PHYSICAL runs: grabbing a run-end
        // joint drags the matching centerline vertex (both runs follow).
        {
            Document d; d.entities().clear();
            Entity dp; dp.id = "dp"; dp.type = EntityType::Polyline;
            dp.points = {{0, 0}, {40, 0}};
            dp.meta.insert("double", true);
            dp.meta.insert("gap", 10.0);
            d.entities().push_back(dp);
            Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(2);
            ed.setTool(ToolId::Select);
            ed.selectById("dp");
            // The far run-end joint sits at (40, +/-5); grab the +5 one and place it.
            ed.onPointerPress({40, 5}, Qt::LeftButton, Qt::NoModifier); // grab run joint
            ed.onPointerMove({60, 5});
            ed.onPointerPress({60, 5}, Qt::LeftButton, Qt::NoModifier); // click to place
            check(near(d.entities()[0].points[1].x, 60.0),
                  "dpoly-grip: a physical-run end joint drags the centerline vertex");
        }
        // Move a joint by dragging an endpoint grip.
        {
            Document d; d.entities().clear();
            Entity l; l.id = "ln"; l.type = EntityType::Line; l.a = {0, 0}; l.b = {20, 0};
            d.entities().push_back(l);
            Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(2);
            ed.setTool(ToolId::Select);
            ed.selectById("ln");
            ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier); // endpoint A grip
            ed.onPointerMove({0, 12});
            ed.onPointerRelease({0, 12}, Qt::LeftButton);
            check(near(d.entities()[0].a.y, 12.0) && near(d.entities()[0].b.x, 20.0) &&
                      near(d.entities()[0].b.y, 0.0),
                  "edit: drag grip moves only that joint");
            ed.undo(); // the drag is one undo step
            check(near(d.entities()[0].a.y, 0.0), "edit: drag is undoable");
        }
        // Drag a circle radius grip resizes it.
        {
            Document d; d.entities().clear();
            Entity c; c.id = "c"; c.type = EntityType::Circle; c.center = {0, 0}; c.radius = 5;
            d.entities().push_back(c);
            Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(2);
            ed.setTool(ToolId::Select);
            ed.selectById("c");
            ed.onPointerPress({5, 0}, Qt::LeftButton, Qt::NoModifier); // +x radius grip
            ed.onPointerMove({12, 0});
            ed.onPointerRelease({12, 0}, Qt::LeftButton);
            check(near(d.entities()[0].radius, 12.0), "edit: drag radius grip resizes circle");
        }
        // Drag a rectangle wall (edge midpoint) grip moves that side.
        {
            Document d; d.entities().clear();
            Entity r; r.id = "r"; r.type = EntityType::Rect;
            r.points = {{0, 0}, {40, 0}, {40, 20}, {0, 20}}; // BL,BR,TR,TL
            d.entities().push_back(r);
            Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(2);
            ed.setTool(ToolId::Select);
            ed.selectById("r");
            ed.onPointerPress({20, 0}, Qt::LeftButton, Qt::NoModifier); // bottom-wall midpoint
            ed.onPointerMove({20, -10});
            ed.onPointerRelease({20, -10}, Qt::LeftButton);
            check(near(d.entities()[0].points[0].y, -10.0) &&
                      near(d.entities()[0].points[1].y, -10.0) &&
                      near(d.entities()[0].points[2].y, 20.0),
                  "edit: rect wall grip moves one side");
        }
        // Click-grab a joint, move, click to place (no button held between).
        {
            Document d; d.entities().clear();
            Entity l; l.id = "ln"; l.type = EntityType::Line; l.a = {0, 0}; l.b = {20, 0};
            d.entities().push_back(l);
            Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(2);
            ed.setTool(ToolId::Select);
            ed.selectById("ln");
            ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier); // grab endpoint A
            ed.onPointerMove({0, 15});                                 // follows cursor
            ed.onPointerRelease({0, 15}, Qt::LeftButton);              // release does NOT place
            ed.onPointerMove({0, 30});                                 // keeps following
            ed.onPointerPress({0, 30}, Qt::LeftButton, Qt::NoModifier); // click places
            check(near(d.entities()[0].a.y, 30.0) && near(d.entities()[0].b.x, 20.0),
                  "edit: click-grab joint follows cursor and places on click");
        }
    }

    // --- After finishing a (single-shot) draw, the tool returns to Select; Space repeats it.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setTool(ToolId::Rect);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({10, 10}, Qt::LeftButton, Qt::NoModifier); // finishes the rect
        check(d.entities().size() == 1, "finish: rect created");
        check(ed.tool() == ToolId::Select, "finish: returned to Select mode");
        ed.onKey(Qt::Key_Space, Qt::NoModifier);
        check(ed.tool() == ToolId::Rect, "finish: Space repeats last tool");
    }

    // --- A line is two points, and then the pointer comes back: the same as after every other drawing.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setTool(ToolId::Line);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({10, 0}, Qt::LeftButton, Qt::NoModifier);   // that is the line
        check(d.entities().size() == 1, "line: two points make one line");
        check(near(d.entities()[0].a.x, 0.0) && near(d.entities()[0].b.x, 10.0),
              "line: between the two points that were clicked");
        check(ed.tool() == ToolId::Select && !ed.isDrafting(),
              "line: and the tool hands back the pointer, as every other drawing does");
        // Space re-arms it, which is how several lines are drawn one after another.
        ed.onKey(Qt::Key_Space, Qt::NoModifier);
        check(ed.tool() == ToolId::Line, "line: Space draws another");
        ed.onPointerPress({20, 20}, Qt::LeftButton, Qt::NoModifier);   // a line with only its start
        ed.onKey(Qt::Key_Return, Qt::NoModifier);
        check(ed.tool() == ToolId::Select && d.entities().size() == 1,
              "line: Enter on a half-drawn line gives up on it rather than leaving half a line");
        ed.setTool(ToolId::Line);
        ed.onPointerPress({30, 30}, Qt::LeftButton, Qt::NoModifier);
        ed.onKey(Qt::Key_Escape, Qt::NoModifier);
        check(ed.tool() == ToolId::Select, "line: Esc exits to Select");
    }

    // --- "f"/"g" while the Line tool is active are consumed (hint), NOT run as
    // commands that switch tools (regression: "f" jumped into the Fillet tool).
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setTool(ToolId::Line);
        check(ed.acceptCommandInput(QStringLiteral("f")), "line-f: 'f' consumed by the Line tool");
        check(ed.tool() == ToolId::Line, "line-f: tool stays Line (no jump to Fillet)");
        check(ed.acceptCommandInput(QStringLiteral("g")), "line-g: 'g' consumed by the Line tool");
        check(ed.tool() == ToolId::Line, "line-g: tool stays Line");
    }

    // --- Text: place styled multiline text, then edit its content + style.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setTool(ToolId::MText);
        ed.onPointerPress({10, 20}, Qt::LeftButton, Qt::NoModifier); // sets the insert point
        Editor::TextStyle st;
        st.size = 7.5; st.font = QStringLiteral("Arial"); st.bold = true;
        st.justify = QStringLiteral("MC"); st.lineSpacing = 1.5; st.list = QStringLiteral("bullet");
        ed.commitText(QStringLiteral("Hello\nWorld"), st);
        check(d.entities().size() == 1, "text: created");
        const Entity& e = d.entities()[0];
        const QString id = e.id;
        check(e.type == EntityType::Text &&
                  e.meta.value(QStringLiteral("text")).toString() == QLatin1String("Hello\nWorld") &&
                  near(e.meta.value(QStringLiteral("size")).toDouble(), 7.5) &&
                  e.meta.value(QStringLiteral("font")).toString() == QLatin1String("Arial") &&
                  e.meta.value(QStringLiteral("bold")).toBool() &&
                  e.meta.value(QStringLiteral("justify")).toString() == QLatin1String("MC") &&
                  near(e.meta.value(QStringLiteral("lineSpacing")).toDouble(), 1.5) &&
                  e.meta.value(QStringLiteral("list")).toString() == QLatin1String("bullet"),
              "text: content + font/size/bold + justify/spacing/list stored");
        Editor::TextStyle st2;
        st2.size = 3.0; st2.font = QStringLiteral("Times"); st2.italic = true;
        ed.applyTextEdit(id, QStringLiteral("BYE"), st2);
        const Entity& e2 = d.entities()[0];
        check(e2.meta.value(QStringLiteral("text")).toString() == QLatin1String("BYE") &&
                  near(e2.meta.value(QStringLiteral("size")).toDouble(), 3.0) &&
                  e2.meta.value(QStringLiteral("italic")).toBool() &&
                  !e2.meta.value(QStringLiteral("bold")).toBool(),
              "text: edit updates content + style");
        // Properties-panel style edit (one field at a time).
        ed.selectById(id);
        ed.setSelectionTextMeta(QStringLiteral("bold"), true);
        check(d.entities()[0].meta.value(QStringLiteral("bold")).toBool(),
              "text: setSelectionTextMeta toggles a single field");
    }

    // --- MText: dragging a box defines the wrap-width container.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setTool(ToolId::MText);
        ed.onPointerPress({0, 40}, Qt::LeftButton, Qt::NoModifier);   // first corner
        ed.onPointerPress({100, 0}, Qt::LeftButton, Qt::NoModifier);  // opposite corner (100 wide)
        ed.commitText(QStringLiteral("a fairly long run of words"), Editor::TextStyle{});
        const Entity& e = d.entities()[0];
        check(e.type == EntityType::Text &&
                  near(e.meta.value(QStringLiteral("boxWidth")).toDouble(), 100.0) &&
                  near(e.a.x, 0.0) && near(e.a.y, 40.0),
              "mtext: box defines wrap width and top-left anchor");
    }

    // --- MText container resize: drag the right-edge width grip.
    {
        Document d; d.entities().clear();
        Entity t; t.id = "t"; t.type = EntityType::Text; t.a = {0, 40};
        t.meta.insert("text", QStringLiteral("abc"));
        t.meta.insert("size", 5.0);
        t.meta.insert("boxWidth", 100.0);
        t.meta.insert("boxHeight", 40.0);
        d.entities().push_back(t);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.selectAll();
        ed.onPointerPress({100, 40}, Qt::LeftButton, Qt::NoModifier); // right-edge width grip
        ed.onPointerMove({160, 40});
        ed.onPointerPress({160, 40}, Qt::LeftButton, Qt::NoModifier);
        check(near(d.entities()[0].meta.value(QStringLiteral("boxWidth")).toDouble(), 160.0),
              "mtext-grip: the width grip resizes the container");
        // Bottom-edge height grip at (a.x, a.y - boxHeight) = (0, 0); drag to y=-30 -> height 70.
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({0, -30});
        ed.onPointerPress({0, -30}, Qt::LeftButton, Qt::NoModifier);
        check(near(d.entities()[0].meta.value(QStringLiteral("boxHeight")).toDouble(), 70.0),
              "mtext-grip: the height grip resizes the container in Y");
    }

    // --- Text selection by its rectangle (window encloses, crossing touches).
    {
        Document d; d.entities().clear();
        Entity t; t.id = "t"; t.type = EntityType::Text; t.a = {0, 0};
        t.meta.insert("text", QStringLiteral("Hello"));
        t.meta.insert("size", 10.0);
        t.meta.insert("boxWidth", 60.0);
        t.meta.insert("boxHeight", 20.0); // rect: (0,0) .. (60,-20)
        d.entities().push_back(t);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        auto regionSelect = [&](Vec2 c1, Vec2 c2) {
            ed.clearSelection();
            ed.onPointerPress(c1, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerRelease(c1, Qt::LeftButton);
            ed.onPointerMove(c2);
            ed.onPointerPress(c2, Qt::LeftButton, Qt::NoModifier);
        };
        regionSelect({-10, 10}, {70, -30}); // window enclosing the rect
        check(ed.selection().size() == 1, "text-sel: window enclosing the text rect selects");
        regionSelect({30, -5}, {-10, -15}); // crossing through the left edge
        check(ed.selection().size() == 1, "text-sel: crossing touching the text selects");
        regionSelect({200, 200}, {150, 150}); // far away
        check(ed.selection().empty(), "text-sel: a region away from the text does not select");
        // Inside the empty CONTAINER (box 0..60 x 0..-20) but past the text extent
        // (~30 x 13): must NOT select — selection is on the text, not the box.
        regionSelect({58, -16}, {45, -19});
        check(ed.selection().empty(),
              "text-sel: a region in the empty container (not on the text) does not select");
    }

    // --- Polygon: default 5 sides; a typed count sets the side number.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setTool(ToolId::Polygon);
        check(ed.polygonSides() == 5, "polygon: default is 5 sides");
        check(ed.acceptCommandInput(QStringLiteral("8")), "polygon: side count consumed");
        check(ed.polygonSides() == 8, "polygon: typed count sets the sides");
    }

    // --- Polyline Arc mode records a bulged (curved) segment.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setTool(ToolId::Polyline);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({10, 0}, Qt::LeftButton, Qt::NoModifier);
        check(ed.acceptCommandInput(QStringLiteral("a")), "pline-arc: 'a' enters arc mode");
        ed.onPointerPress({20, 10}, Qt::LeftButton, Qt::NoModifier); // arc tangent to +x
        ed.onKey(Qt::Key_Return, Qt::NoModifier);                    // finish
        const Entity& e = d.entities()[0];
        const QJsonArray bulges = e.meta.value(QStringLiteral("bulges")).toArray();
        bool anyArc = false;
        for (const auto& v : bulges) if (std::abs(v.toDouble()) > 1e-9) anyArc = true;
        check(e.type == EntityType::Polyline && anyArc, "pline-arc: a curved segment is stored");
    }

    // --- Polyline fillet: "f" + value rounds corners; "fc" cancels it.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setTool(ToolId::Polyline);
        ed.acceptCommandInput(QStringLiteral("f"));  // enter fillet-radius mode
        ed.acceptCommandInput(QStringLiteral("5"));  // radius 5
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({40, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({40, 40}, Qt::LeftButton, Qt::NoModifier);
        ed.onKey(Qt::Key_Return, Qt::NoModifier);
        // Per-corner storage: the interior corner (index 1) carries the radius.
        check(near(d.entities()[0].meta.value(QStringLiteral("cornerRadii")).toArray()[1].toDouble(), 5.0),
              "pline-fillet: radius stored on the polyline");

        Document d2; d2.entities().clear();
        Editor ed2; ed2.setDocument(&d2); noSnap(ed2);
        ed2.setTool(ToolId::Polyline);
        ed2.acceptCommandInput(QStringLiteral("f"));
        ed2.acceptCommandInput(QStringLiteral("5"));
        check(ed2.acceptCommandInput(QStringLiteral("fc")), "pline-fillet: 'fc' consumed");
        ed2.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed2.onPointerPress({40, 0}, Qt::LeftButton, Qt::NoModifier);
        ed2.onPointerPress({40, 40}, Qt::LeftButton, Qt::NoModifier);
        ed2.onKey(Qt::Key_Return, Qt::NoModifier);
        check(!d2.entities()[0].meta.contains(QStringLiteral("cornerRadii")),
              "pline-fillet: 'fc' returns to sharp corners");
    }

    // --- Double polyline (multiline) fillet: "f" + value rounds the knees.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setTool(ToolId::DoublePolyline);
        check(ed.acceptCommandInput(QStringLiteral("f")), "mline-fillet: 'f' enters fillet mode");
        ed.acceptCommandInput(QStringLiteral("6"));  // knee radius
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({40, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({40, 40}, Qt::LeftButton, Qt::NoModifier);
        ed.onKey(Qt::Key_Return, Qt::NoModifier);
        const Entity& e = d.entities()[0];
        check(e.type == EntityType::Polyline && e.meta.value(QStringLiteral("double")).toBool() &&
                  near(e.meta.value(QStringLiteral("cornerRadii")).toArray()[1].toDouble(), 6.0),
              "mline-fillet: double polyline keeps the knee radius");
    }

    // --- Fillet defined MID-polyline, then keep drawing (knees round, draft continues).
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setTool(ToolId::Polyline);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({40, 0}, Qt::LeftButton, Qt::NoModifier);
        check(ed.acceptCommandInput(QStringLiteral("f")), "pline-fillet-mid: 'f' mid-draw enters fillet");
        ed.acceptCommandInput(QStringLiteral("8")); // knee radius
        check(ed.isDrafting(), "pline-fillet-mid: still drawing after setting the radius");
        ed.onPointerPress({40, 40}, Qt::LeftButton, Qt::NoModifier); // continue adding points
        ed.onPointerPress({0, 40}, Qt::LeftButton, Qt::NoModifier);
        ed.onKey(Qt::Key_Return, Qt::NoModifier);
        const Entity& e = d.entities()[0];
        check(e.type == EntityType::Polyline && e.points.size() == 4 &&
                  near(e.meta.value(QStringLiteral("cornerRadii")).toArray()[1].toDouble(), 8.0),
              "pline-fillet-mid: radius set and the polyline continued (4 pts)");
    }

    // --- Polyline options: "u" Undo drops the last point; "c" Close finishes the loop.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setTool(ToolId::Polyline);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({10, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({10, 10}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({99, 99}, Qt::LeftButton, Qt::NoModifier); // a stray 4th point
        check(ed.acceptCommandInput(QStringLiteral("u")), "pline: Undo consumed");      // drop (99,99)
        check(ed.acceptCommandInput(QStringLiteral("c")), "pline: Close consumed");
        check(d.entities().size() == 1, "pline: closed polyline created");
        const Entity& e = d.entities()[0];
        check(e.type == EntityType::Polyline && e.points.size() == 4 &&
                  near(e.points.front().x, e.points.back().x) &&
                  near(e.points.front().y, e.points.back().y),
              "pline: Close appends the start point (loop closed)");
    }

    // --- Picking a DRAW tool clears the selection; a MODIFY tool keeps it.
    {
        Document d; d.entities().clear();
        Entity ln; ln.id = "l"; ln.type = EntityType::Line; ln.a = {0, 0}; ln.b = {10, 0};
        d.entities().push_back(ln);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.selectAll();
        check(ed.selection().size() == 1, "deselect-on-draw: object selected first");
        ed.setTool(ToolId::Circle);
        check(ed.selection().empty(), "deselect-on-draw: draw tool clears the selection");
        ed.selectAll();
        ed.setTool(ToolId::Move);
        check(ed.selection().size() == 1, "deselect-on-draw: modify tool keeps the selection");
    }

    // --- Dynamic input reference fields (AutoCAD-style Tab cycling).
    {
        // Line segment: Length + Angle. Lock 50, Tab to Angle, lock 90 -> (0,50).
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setTool(ToolId::Line);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        const auto labels = ed.drawInputFieldLabels();
        check(labels.size() == 2 && labels[0] == QStringLiteral("Length") &&
                  labels[1] == QStringLiteral("Angle"),
              "dynin: line offers Length + Angle");
        ed.lockDrawField(QStringLiteral("50"));
        ed.cycleDrawInputField(); // Tab -> Angle
        check(ed.currentDrawFieldLabel() == QStringLiteral("Angle"), "dynin: Tab switches to Angle");
        ed.cycleDrawInputField(); // Tab again -> back to Length
        check(ed.currentDrawFieldLabel() == QStringLiteral("Length"),
              "dynin: a second Tab jumps back to Length");
        ed.cycleDrawInputField(); // -> Angle again, ready to type the angle
        ed.lockDrawField(QStringLiteral("90"));
        check(ed.commitDrawFields(), "dynin: commit consumed");
        check(d.entities().size() == 1 && near(d.entities()[0].b.x, 0.0) &&
                  near(d.entities()[0].b.y, 50.0),
              "dynin: Length 50 @ Angle 90 -> (0,50)");
    }
    {
        // Circle: a typed Radius builds the circle directly.
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setCircleMode(Editor::CircleMode::CenterRadius);
        ed.setTool(ToolId::Circle);
        ed.onPointerPress({10, 10}, Qt::LeftButton, Qt::NoModifier); // center
        const auto labels = ed.drawInputFieldLabels();
        check(labels.size() == 1 && labels[0] == QStringLiteral("Radius"), "dynin: circle offers Radius");
        ed.lockDrawField(QStringLiteral("25"));
        check(ed.commitDrawFields(), "dynin: circle commit");
        check(d.entities().size() == 1 && d.entities()[0].type == EntityType::Circle &&
                  near(d.entities()[0].radius, 25.0),
              "dynin: typed Radius 25");
    }
    {
        // Ellipse (3-point): axis via Length+Angle, then the other half-axis Distance.
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setEllipseMode(Editor::EllipseMode::Center);
        ed.setTool(ToolId::Ellipse);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier); // center
        auto labels = ed.drawInputFieldLabels();
        check(labels.size() == 2 && labels[0] == QStringLiteral("Length") &&
                  labels[1] == QStringLiteral("Angle"),
              "dynin: ellipse axis = Length + Angle");
        ed.lockDrawField(QStringLiteral("30"));   // axis length
        ed.cycleDrawInputField();
        ed.lockDrawField(QStringLiteral("0"));    // axis angle 0 -> axis along +X
        check(ed.commitDrawFields(), "dynin: ellipse axis commit");
        labels = ed.drawInputFieldLabels();
        check(labels.size() == 1 && labels[0] == QStringLiteral("Distance"),
              "dynin: ellipse minor = Distance");
        ed.lockDrawField(QStringLiteral("10"));
        check(ed.commitDrawFields(), "dynin: ellipse minor commit");
        const Entity& e = d.entities()[0];
        check(e.type == EntityType::Ellipse && near(e.radius, 30.0) && near(e.radiusY, 10.0) &&
                  near(e.rotation, 0.0),
              "dynin: ellipse a=30 b=10 rotation=0");
    }

    // --- Arc construction: a 3-point arc passes through the middle point (the
    // renderer sweeps CCW, so start/end are ordered to cover the mid).
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setArcMode(Editor::ArcMode::ThreePoint);
        ed.setTool(ToolId::Arc);
        ed.onPointerPress({10, 0}, Qt::LeftButton, Qt::NoModifier);   // start
        ed.onPointerPress({0, 10}, Qt::LeftButton, Qt::NoModifier);   // point on arc (upper)
        ed.onPointerPress({-10, 0}, Qt::LeftButton, Qt::NoModifier);  // end
        check(d.entities().size() == 1, "arc: created");
        const Entity& e = d.entities()[0];
        check(e.type == EntityType::Arc && near(e.center.x, 0.0) && near(e.center.y, 0.0) &&
                  near(e.radius, 10.0),
              "arc: center origin, radius 10");
        // CCW from start(0) to end(pi) sweeps the UPPER half through (0,10).
        check(near(e.startAngle, 0.0) && near(e.endAngle, M_PI), "arc: CCW spans the upper half");
    }
    {
        // Same start/end but the on-arc point is BELOW: the sweep must flip so it
        // covers the lower half (start/end swapped).
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setArcMode(Editor::ArcMode::ThreePoint);
        ed.setTool(ToolId::Arc);
        ed.onPointerPress({10, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({0, -10}, Qt::LeftButton, Qt::NoModifier);  // lower
        ed.onPointerPress({-10, 0}, Qt::LeftButton, Qt::NoModifier);
        const Entity& e = d.entities()[0];
        check(near(e.startAngle, M_PI) && near(e.endAngle, 0.0),
              "arc: flips to span the lower half through (0,-10)");
    }
    {
        // "mi" mirror flips the arc to the opposite sweep (swaps start/end).
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setArcMode(Editor::ArcMode::ThreePoint);
        ed.setTool(ToolId::Arc);
        ed.onPointerPress({10, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({0, 10}, Qt::LeftButton, Qt::NoModifier);
        check(ed.acceptCommandInput(QStringLiteral("mi")), "arc-mirror: 'mi' consumed");
        check(ed.isArcMirrored(), "arc-mirror: toggled on");
        // Ctrl while drafting also flips the arc.
        check(ed.onKey(Qt::Key_Control, Qt::ControlModifier), "arc-mirror: Ctrl consumed while drafting");
        check(!ed.isArcMirrored(), "arc-mirror: Ctrl toggled it back off");
        ed.onKey(Qt::Key_Control, Qt::ControlModifier); // back on for the placement below
        check(ed.isArcMirrored(), "arc-mirror: Ctrl re-toggled on");
        ed.onPointerPress({-10, 0}, Qt::LeftButton, Qt::NoModifier); // finishes
        const Entity& e = d.entities()[0];
        // Un-mirrored would be start=0,end=pi (upper). Mirrored swaps -> lower half.
        check(near(e.startAngle, M_PI) && near(e.endAngle, 0.0),
              "arc-mirror: mirrored arc takes the opposite half");
    }

    // --- Arc Center,Start,End: type Radius + Angle in the dynamic-input fields.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setArcMode(Editor::ArcMode::CenterStartEnd);
        ed.setTool(ToolId::Arc);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier); // centre
        auto labels = ed.drawInputFieldLabels();
        check(labels.size() == 2 && labels[0] == QStringLiteral("Radius") &&
                  labels[1] == QStringLiteral("Angle"),
              "arc-center: offers Radius + Angle for the start point");
        ed.lockDrawField(QStringLiteral("50")); // radius
        ed.cycleDrawInputField();
        ed.lockDrawField(QStringLiteral("90")); // angle -> start at (0,50)
        check(ed.commitDrawFields(), "arc-center: start committed from Radius+Angle");
        ed.lockDrawField(QStringLiteral("0")); // end angle -> (50,0)
        check(ed.commitDrawFields(), "arc-center: end committed from Angle");
        check(d.entities().size() == 1 && d.entities()[0].type == EntityType::Arc &&
                  near(d.entities()[0].center.x, 0.0) && near(d.entities()[0].center.y, 0.0) &&
                  near(d.entities()[0].radius, 50.0),
              "arc-center: arc at centre (0,0) radius 50 from typed values");
    }

    // --- Circle Tan-Tan-Radius: tangent to two perpendicular lines, radius 10.
    {
        Document d; d.entities().clear();
        Entity l1; l1.id = "l1"; l1.type = EntityType::Line; l1.a = {-100, 0}; l1.b = {100, 0};
        Entity l2; l2.id = "l2"; l2.type = EntityType::Line; l2.a = {0, -100}; l2.b = {0, 100};
        d.entities().push_back(l1);
        d.entities().push_back(l2);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setCircleMode(Editor::CircleMode::TanTanRadius);
        ed.setTool(ToolId::Circle);
        ed.onPointerPress({50, 0}, Qt::LeftButton, Qt::NoModifier); // pick line 1 (first quadrant side)
        ed.onPointerPress({0, 50}, Qt::LeftButton, Qt::NoModifier); // pick line 2
        ed.acceptCommandInput(QStringLiteral("10"));                // radius
        const Entity& c = d.entities().back();
        check(c.type == EntityType::Circle && near(c.radius, 10.0) && near(c.center.x, 10.0) &&
                  near(c.center.y, 10.0),
              "ttr: circle r=10 tangent to both axes sits at (10,10)");
    }

    // --- Circle Tan-Tan-Tan: incircle of a right triangle (legs 60).
    {
        Document d; d.entities().clear();
        Entity l1; l1.id = "l1"; l1.type = EntityType::Line; l1.a = {-100, 0}; l1.b = {100, 0};
        Entity l2; l2.id = "l2"; l2.type = EntityType::Line; l2.a = {0, -100}; l2.b = {0, 100};
        Entity l3; l3.id = "l3"; l3.type = EntityType::Line; l3.a = {60, 0}; l3.b = {0, 60};
        d.entities().push_back(l1);
        d.entities().push_back(l2);
        d.entities().push_back(l3);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setCircleMode(Editor::CircleMode::TanTanTan);
        ed.setTool(ToolId::Circle);
        ed.onPointerPress({30, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({0, 30}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({30, 30}, Qt::LeftButton, Qt::NoModifier); // on x+y=60; finishes
        const double r = (60.0 + 60.0 - 60.0 * std::sqrt(2.0)) / 2.0; // inradius ~17.57
        const Entity& c = d.entities().back();
        check(c.type == EntityType::Circle && near(c.radius, r, 1e-3) &&
                  near(c.center.x, r, 1e-3) && near(c.center.y, r, 1e-3),
              "ttt: incircle of the right triangle");
    }

    // --- Ellipse construction: a rotated ellipse keeps its first-axis angle.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setEllipseMode(Editor::EllipseMode::AxisEnd);
        ed.setTool(ToolId::Ellipse);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);   // axis end 1
        ed.onPointerPress({0, 10}, Qt::LeftButton, Qt::NoModifier);  // axis end 2 (vertical)
        ed.onPointerPress({-3, 5}, Qt::LeftButton, Qt::NoModifier);  // other half-axis (perp dist 3)
        const Entity& e = d.entities()[0];
        check(e.type == EntityType::Ellipse && near(e.center.x, 0.0) && near(e.center.y, 5.0) &&
                  near(e.radius, 5.0) && near(e.radiusY, 3.0) && near(e.rotation, M_PI / 2.0),
              "ellipse: vertical axis -> a=5 b=3 rotation=90deg");
    }

    // --- Elliptical arc: define the ellipse, then pick start + end angles.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setEllipseMode(Editor::EllipseMode::EllipticalArc);
        ed.setTool(ToolId::Ellipse);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);   // axis end 1
        ed.onPointerPress({20, 0}, Qt::LeftButton, Qt::NoModifier);  // axis end 2 -> a=10, centre (10,0)
        ed.onPointerPress({10, 5}, Qt::LeftButton, Qt::NoModifier);  // minor distance -> b=5
        check(d.entities().empty(), "ellarc: ellipse not committed until the angles are picked");
        ed.onPointerPress({20, 0}, Qt::LeftButton, Qt::NoModifier);  // start angle -> 0
        ed.onPointerPress({10, 5}, Qt::LeftButton, Qt::NoModifier);  // end angle -> pi/2 (finishes)
        check(d.entities().size() == 1, "ellarc: created after 5 picks");
        const Entity& e = d.entities()[0];
        check(e.type == EntityType::Ellipse &&
                  e.meta.value(QStringLiteral("ellipticalArc")).toBool() &&
                  near(e.radius, 10.0) && near(e.radiusY, 5.0) &&
                  near(e.startAngle, 0.0) && near(e.endAngle, M_PI / 2.0),
              "ellarc: full ellipse opened to a 0..90deg arc");
    }

    // --- Crossing region that merely TOUCHES a curve (circle ring) selects it.
    {
        Document d; d.entities().clear();
        Entity c; c.id = "c"; c.type = EntityType::Circle; c.center = {0, 0}; c.radius = 100;
        d.entities().push_back(c);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        auto regionSelect = [&](Vec2 c1, Vec2 c2) { // c2.x<c1.x => crossing
            ed.clearSelection();
            ed.onPointerPress(c1, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerRelease(c1, Qt::LeftButton);
            ed.onPointerMove(c2);
            ed.onPointerPress(c2, Qt::LeftButton, Qt::NoModifier);
        };
        regionSelect({40, 110}, {20, 90}); // straddles the ring near (31,95)
        check(ed.selection().size() == 1, "crossing-curve: box touching the circle ring selects it");
        regionSelect({40, 40}, {20, 20});  // well inside, far from ring and center
        check(ed.selection().empty(), "crossing-curve: box not touching the ring does not select");
    }

    // --- A region catching ONLY the circle centre (not the ring) must NOT select;
    //     a window enclosing the whole ring still selects.
    {
        Document d; d.entities().clear();
        Entity c; c.id = "c"; c.type = EntityType::Circle; c.center = {0, 0}; c.radius = 100;
        d.entities().push_back(c);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        auto regionSelect = [&](Vec2 c1, Vec2 c2) {
            ed.clearSelection();
            ed.onPointerPress(c1, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerRelease(c1, Qt::LeftButton);
            ed.onPointerMove(c2);
            ed.onPointerPress(c2, Qt::LeftButton, Qt::NoModifier);
        };
        regionSelect({10, 10}, {-10, -10});   // crossing box around the centre only
        check(ed.selection().empty(), "circle-center: crossing only the centre does NOT select");
        regionSelect({-10, -10}, {10, 10});   // window over the centre only (not the ring)
        check(ed.selection().empty(), "circle-center: window over the centre only does NOT select");
        regionSelect({-120, -120}, {120, 120}); // window enclosing the whole ring
        check(ed.selection().size() == 1, "circle-center: window enclosing the ring selects");
    }

    // --- An arc's off-curve quadrant point caught by a region must NOT select.
    {
        Document d; d.entities().clear();
        Entity a; a.id = "a"; a.type = EntityType::Arc; a.center = {0, 0}; a.radius = 100;
        a.startAngle = 0.0; a.endAngle = M_PI / 2.0; // upper-right quarter only
        d.entities().push_back(a);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        auto regionSelect = [&](Vec2 c1, Vec2 c2) {
            ed.clearSelection();
            ed.onPointerPress(c1, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerRelease(c1, Qt::LeftButton);
            ed.onPointerMove(c2);
            ed.onPointerPress(c2, Qt::LeftButton, Qt::NoModifier);
        };
        // Small box around the arc CENTRE (0,0) — an off-curve point; the press at
        // (10,10) is in empty space (far from the radius-100 ring), so a region
        // (not a click) is started.
        regionSelect({10, 10}, {-10, -10});
        check(ed.selection().empty(), "arc-offcurve: region on the off-curve centre does NOT select");
        // Box straddling the real arc near 45deg (~70,70).
        regionSelect({80, 80}, {60, 60});
        check(ed.selection().size() == 1, "arc-offcurve: region touching the real arc selects");
    }

    // --- Arc joints: dragging the start endpoint sets its angle (and radius).
    {
        Document d; d.entities().clear();
        Entity a; a.id = "a"; a.type = EntityType::Arc; a.center = {0, 0}; a.radius = 10;
        a.startAngle = 0.0; a.endAngle = M_PI / 2.0;
        d.entities().push_back(a);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.selectAll();
        ed.onPointerPress({10, 0}, Qt::LeftButton, Qt::NoModifier); // start endpoint grip
        ed.onPointerMove({0, -10});
        ed.onPointerPress({0, -10}, Qt::LeftButton, Qt::NoModifier);
        const Entity& e = d.entities()[0];
        check(e.type == EntityType::Arc && near(e.radius, 10.0) && near(e.startAngle, -M_PI / 2.0),
              "arc-grip: start endpoint drag sets angle + radius");
    }

    // --- Ellipse joints: dragging a major-axis endpoint sets size + rotation.
    {
        Document d; d.entities().clear();
        Entity el; el.id = "e"; el.type = EntityType::Ellipse; el.center = {0, 0};
        el.radius = 10; el.radiusY = 5; el.rotation = 0;
        d.entities().push_back(el);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.selectAll();
        ed.onPointerPress({10, 0}, Qt::LeftButton, Qt::NoModifier); // major-axis endpoint grip
        ed.onPointerMove({0, 20});
        ed.onPointerPress({0, 20}, Qt::LeftButton, Qt::NoModifier);
        const Entity& e = d.entities()[0];
        check(e.type == EntityType::Ellipse && near(e.radius, 20.0) && near(e.rotation, M_PI / 2.0),
              "ellipse-grip: major axis follows the cursor (size + rotation)");
    }

    // --- Spline Fit vs CV: a CV spline records its mode; Fit is the default.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setSplineMode(Editor::SplineMode::CV);
        ed.setTool(ToolId::Spline);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({10, 10}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({20, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onKey(Qt::Key_Return, Qt::NoModifier); // finish
        check(d.entities().size() == 1 && d.entities()[0].type == EntityType::Spline &&
                  d.entities()[0].meta.value("splineMode").toString() == QLatin1String("cv"),
              "spline-mode: CV spline tagged 'cv'");

        Document d2; d2.entities().clear();
        Editor ed2; ed2.setDocument(&d2); noSnap(ed2);
        ed2.setSplineMode(Editor::SplineMode::Fit);
        ed2.setTool(ToolId::Spline);
        ed2.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed2.onPointerPress({10, 10}, Qt::LeftButton, Qt::NoModifier);
        ed2.onPointerPress({20, 0}, Qt::LeftButton, Qt::NoModifier);
        ed2.onKey(Qt::Key_Return, Qt::NoModifier);
        check(d2.entities().size() == 1 &&
                  d2.entities()[0].meta.value("splineMode").toString().isEmpty(),
              "spline-mode: Fit spline has no cv tag (default)");
    }

    // --- Editable properties: geometry + general (meta).
    {
        Document d; d.entities().clear();
        Entity r; r.id = "r"; r.type = EntityType::Rect;
        r.points = {{0, 0}, {40, 0}, {40, 20}, {0, 20}};
        d.entities().push_back(r);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.selectById("r");

        ed.setGeometryValue(QStringLiteral("Width"), 80.0); // scales about center (x=20)
        const Bounds b1 = ed.document()->entities()[0].bounds();
        check(near(b1.width(), 80.0), "props: edit Width resizes");

        ed.setGeometryValue(QStringLiteral("Center X"), 100.0);
        const Bounds b2 = ed.document()->entities()[0].bounds();
        check(near(b2.center().x, 100.0), "props: edit Center X moves");

        ed.setSelectionLayer(QStringLiteral("Wires"));
        check(ed.document()->entities()[0].layer == "Wires", "props: layer dropdown applies");
        ed.setSelectionColor(QStringLiteral("#ff0000"));
        check(ed.document()->entities()[0].color == "#ff0000", "props: color applies");
        ed.setSelectionWeight(0.5);
        check(near(ed.document()->entities()[0].weight, 0.5), "props: lineweight applies");
        ed.setSelectionMeta(QStringLiteral("linetype"), QStringLiteral("Dashed"));
        check(ed.document()->entities()[0].meta.value("linetype").toString() == "Dashed",
              "props: linetype meta applies");

        // Circle radius edit.
        Document d2; d2.entities().clear();
        Entity c; c.id = "c"; c.type = EntityType::Circle; c.center = {0, 0}; c.radius = 5;
        d2.entities().push_back(c);
        Editor ed2; ed2.setDocument(&d2); noSnap(ed2);
        ed2.selectById("c");
        ed2.setGeometryValue(QStringLiteral("Radius"), 15.0);
        check(near(ed2.document()->entities()[0].radius, 15.0), "props: edit Radius");
    }

    // --- Match properties: source style + size copies onto a clicked same-type target.
    {
        Document d; d.entities().clear();
        // Source: a red, thick, radius-15 circle at (0,0). Target: a default circle far away.
        Entity src; src.id = "src"; src.type = EntityType::Circle; src.center = {0, 0};
        src.radius = 15.0; src.color = "#ff0000"; src.weight = 0.7; src.layer = "Wires";
        Entity tgt; tgt.id = "tgt"; tgt.type = EntityType::Circle; tgt.center = {100, 0};
        tgt.radius = 3.0;
        d.entities().push_back(src);
        d.entities().push_back(tgt);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.beginMatchProps();
        check(ed.awaitingMatchProps(), "match: armed after beginMatchProps");
        ed.onPointerPress({15, 0}, Qt::LeftButton, Qt::NoModifier);  // pick source (on its outline)
        check(ed.awaitingMatchProps(), "match: still armed after picking the source");
        ed.onPointerPress({103, 0}, Qt::LeftButton, Qt::NoModifier); // paint the target (on its outline)
        const Entity* t = nullptr;
        for (const auto& e : d.entities()) if (e.id == "tgt") t = &e;
        check(t && t->color == "#ff0000", "match: color copied to target");
        check(t && near(t->weight, 0.7), "match: lineweight copied to target");
        check(t && t->layer == "Wires", "match: layer copied to target");
        check(t && near(t->radius, 15.0), "match: geometry (radius) copied for same type");
        // The target keeps its own position — only style/size is matched, not location.
        check(t && near(t->center.x, 100.0), "match: target position is unchanged");
        // Esc ends the mode.
        ed.onKey(Qt::Key_Escape, Qt::NoModifier);
        check(!ed.awaitingMatchProps(), "match: Esc ends the mode");
    }

    // Match properties across DIFFERENT types copies only style, never geometry size.
    {
        Document d; d.entities().clear();
        Entity src; src.id = "src"; src.type = EntityType::Circle; src.center = {0, 0};
        src.radius = 15.0; src.color = "#00ff00";
        Entity tgt; tgt.id = "tgt"; tgt.type = EntityType::Rect;
        tgt.points = {{100, 0}, {110, 0}, {110, 10}, {100, 10}};
        d.entities().push_back(src);
        d.entities().push_back(tgt);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.beginMatchProps();
        ed.onPointerPress({15, 0}, Qt::LeftButton, Qt::NoModifier);  // source on its outline
        ed.onPointerPress({105, 0}, Qt::LeftButton, Qt::NoModifier); // on the rect's bottom edge
        const Entity* t = nullptr;
        for (const auto& e : d.entities()) if (e.id == "tgt") t = &e;
        check(t && t->color == "#00ff00", "match: color copies across different types");
        check(t && t->points.size() == 4 && near(t->points[0].x, 100.0),
              "match: a rect target keeps its own geometry (no radius bleed-over)");
    }

    // --- Direct distance entry while drawing a line.
    {
        // Bare distance along the cursor direction.
        {
            Document d; d.entities().clear();
            Editor ed; ed.setDocument(&d); noSnap(ed);
            ed.setTool(ToolId::Line);
            ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier); // first point
            ed.onPointerMove({1, 0});                                  // direction = +x
            const bool consumed = ed.applyDrawInput(QStringLiteral("50"));
            check(consumed, "dde: input consumed");
            check(d.entities().size() == 1 && near(d.entities()[0].b.x, 50.0) &&
                      near(d.entities()[0].b.y, 0.0),
                  "dde: distance along cursor direction");
        }
        // Relative @dx,dy.
        {
            Document d; d.entities().clear();
            Editor ed; ed.setDocument(&d); noSnap(ed);
            ed.setTool(ToolId::Line);
            ed.onPointerPress({10, 10}, Qt::LeftButton, Qt::NoModifier);
            ed.applyDrawInput(QStringLiteral("@30,40"));
            check(d.entities().size() == 1 && near(d.entities()[0].b.x, 40.0) &&
                      near(d.entities()[0].b.y, 50.0),
                  "dde: relative @dx,dy");
        }
        // Double line records its gap.
        {
            Document d; d.entities().clear();
            Editor ed; ed.setDocument(&d); noSnap(ed);
            ed.doubleLineGap() = 7.0;
            ed.setTool(ToolId::DoubleLine);
            ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerMove({1, 0});
            ed.applyDrawInput(QStringLiteral("20"));
            check(d.entities().size() == 1 &&
                      d.entities()[0].type == EntityType::DoubleLine &&
                      near(d.entities()[0].meta.value("gap").toDouble(), 7.0),
                  "dde: double line keeps gap");
        }
    }

    // --- A double run is drawn ALONG the cursor: the rail under the crosshair goes through the points
    //     that were clicked, and the gap goes to the other side of it. The clicked points used to
    //     become the CENTRELINE, which put the crosshair in the empty space between the two lines.
    {
        // A double line, left to right with a 10 mm gap: the top rail is what was drawn.
        {
            Document d; d.entities().clear();
            Editor ed; ed.setDocument(&d); noSnap(ed);
            ed.doubleLineGap() = 10.0;
            ed.setTool(ToolId::DoubleLine);
            ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerPress({100, 0}, Qt::LeftButton, Qt::NoModifier);
            check(d.entities().size() == 1, "dline-justify: one entity");
            const Entity& e = d.entities()[0];
            // Stored centreline half a gap BELOW the line that was drawn...
            check(e.type == EntityType::DoubleLine && near(e.a.y, -5.0) && near(e.b.y, -5.0),
                  "dline-justify: the centreline sits half a gap off the drawn line");
            // ...so the rail on the cursor's side runs exactly through the clicked points.
            const double gap = e.meta.value("gap").toDouble();
            check(near(e.a.y + gap / 2.0, 0.0) && near(e.b.y + gap / 2.0, 0.0),
                  "dline-justify: the rail the cursor rode is the line that was drawn");
            check(near(e.a.x, 0.0) && near(e.b.x, 100.0),
                  "dline-justify: and it is not shifted along its own length");
        }
        // The same for a double polyline, with a bend in it.
        {
            Document d; d.entities().clear();
            Editor ed; ed.setDocument(&d); noSnap(ed);
            ed.doubleLineGap() = 10.0;
            ed.setTool(ToolId::DoublePolyline);
            const std::vector<Vec2> drawn = {{0, 0}, {100, 0}, {100, 100}};
            for (const Vec2& v : drawn) ed.onPointerPress(v, Qt::LeftButton, Qt::NoModifier);
            ed.onKey(Qt::Key_Return, Qt::NoModifier);
            check(d.entities().size() == 1, "dpoly-justify: one entity");
            const Entity& e = d.entities()[0];
            check(e.points.size() == drawn.size(),
                  "dpoly-justify: a vertex for every point clicked");
            // The rail on the cursor's side is the polyline that was drawn - mitred corner included.
            const std::vector<Vec2> rail =
                offsetPolyline(e.points, e.meta.value("gap").toDouble() / 2.0);
            bool onIt = rail.size() == drawn.size();
            for (size_t i = 0; onIt && i < rail.size(); ++i)
                onIt = near(rail[i].x, drawn[i].x, 1e-6) && near(rail[i].y, drawn[i].y, 1e-6);
            check(onIt, "dpoly-justify: the rail the cursor rode passes through every clicked point");
            // And nothing is drawn where the cursor was NOT: the centreline is off to one side.
            check(!near(e.points[0].y, 0.0), "dpoly-justify: the centreline is not under the cursor");
            // It is still pickable by clicking the line that was drawn, which is the whole point.
            Editor pick; pick.setDocument(&d); noSnap(pick); pick.setPixelTolerance(3);
            pick.onPointerPress({50, 0}, Qt::LeftButton, Qt::NoModifier);
            check(pick.selection().size() == 1,
                  "dpoly-justify: and clicking the line that was drawn selects it");
        }
    }

    // --- Ctrl swaps which side the second line of a double run goes.
    {
        // Same three clicks twice: once as it comes, once with Ctrl tapped part way through.
        auto drawIt = [](Document& d, bool tapCtrl) {
            Editor ed; ed.setDocument(&d); noSnap(ed);
            ed.doubleLineGap() = 10.0;
            ed.setTool(ToolId::DoublePolyline);
            check(ed.doubleSecondBelow(), "dpoly-ctrl: the second line starts out below");
            ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
            if (tapCtrl) {
                check(ed.onKey(Qt::Key_Control, Qt::ControlModifier),
                      "dpoly-ctrl: Ctrl is taken while drawing");
                check(!ed.doubleSecondBelow(), "dpoly-ctrl: and swaps the side");
            }
            ed.onPointerPress({100, 0}, Qt::LeftButton, Qt::NoModifier);
            ed.onKey(Qt::Key_Return, Qt::NoModifier);
        };
        Document a; a.entities().clear();
        Document b; b.entities().clear();
        drawIt(a, false);
        drawIt(b, true);
        check(a.entities().size() == 1 && b.entities().size() == 1, "dpoly-ctrl: one run each");
        const Entity& below = a.entities()[0];
        const Entity& above = b.entities()[0];
        // Either way the line that was DRAWN is a rail: what changes is which side its partner is on.
        check(near(below.points[0].y, -5.0), "dpoly-ctrl: second line below -> centreline below");
        check(near(above.points[0].y, 5.0), "dpoly-ctrl: swapped -> centreline above");
        // So the drawn line y=0 is a rail of both, and the other rail is on opposite sides.
        const double gap = 10.0;
        check(near(below.points[0].y + gap / 2.0, 0.0) && near(above.points[0].y - gap / 2.0, 0.0),
              "dpoly-ctrl: the line under the cursor is a rail either way");
        // Ctrl again puts it back.
        {
            Document c; c.entities().clear();
            Editor ed; ed.setDocument(&c); noSnap(ed);
            ed.setTool(ToolId::DoublePolyline);
            ed.onKey(Qt::Key_Control, Qt::ControlModifier);
            ed.onKey(Qt::Key_Control, Qt::ControlModifier);
            check(ed.doubleSecondBelow(), "dpoly-ctrl: twice returns to where it was");
        }
    }

    // --- Shift-clicking the start onto a line starts BOTH physical lines flush on it.
    {
        Document d; d.entities().clear();
        // A SLANTED line, deliberately: on a vertical one the shifted centreline still happens to land
        // on it, so a vertical reference could not tell the fixed code from the broken code.
        Entity ref;
        ref.id = QStringLiteral("ref");
        ref.type = EntityType::Line;
        ref.a = {-40, -40};
        ref.b = {40, 40};                 // 45 degrees, through the origin (its midpoint)
        d.entities().push_back(ref);
        Editor ed; ed.setDocument(&d);
        auto& s = ed.snapSettings();      // object snap ON: the click lands on the line
        s.objectSnap = true; s.gridSnap = false; s.ortho = false; s.polar = false;
        ed.setPixelTolerance(4);
        ed.doubleLineGap() = 10.0;
        ed.setTool(ToolId::DoublePolyline);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::ShiftModifier);   // Shift: start flush on it
        ed.onPointerPress({120, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onKey(Qt::Key_Return, Qt::NoModifier);
        const Entity* run = nullptr;
        for (const Entity& e : d.entities())
            if (e.type == EntityType::Polyline && e.meta.value("double").toBool()) run = &e;
        check(run != nullptr, "dpoly-shift-start: the run was placed");
        if (run) {
            check(run->meta.value(QStringLiteral("cutStart")).toBool(),
                  "dpoly-shift-start: the start is capped to something");
            check(run->meta.value(QStringLiteral("cutStartKind")).toString() ==
                      QLatin1String("line"),
                  "dpoly-shift-start: and that something is a line");
            // The line recorded IS the one clicked, so both rails are clipped onto it.
            check(near(run->meta.value(QStringLiteral("cutStartAx")).toDouble(), -40.0) &&
                      near(run->meta.value(QStringLiteral("cutStartBx")).toDouble(), 40.0),
                  "dpoly-shift-start: the reference is the line that was clicked");
        }
        // Without Shift nothing is capped - the run just starts where it was clicked.
        Document d2; d2.entities().clear();
        d2.entities().push_back(ref);
        Editor e2; e2.setDocument(&d2);
        auto& s2 = e2.snapSettings();
        s2.objectSnap = true; s2.gridSnap = false; s2.ortho = false; s2.polar = false;
        e2.setPixelTolerance(4);
        e2.setTool(ToolId::DoublePolyline);
        e2.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        e2.onPointerPress({120, 0}, Qt::LeftButton, Qt::NoModifier);
        e2.onKey(Qt::Key_Return, Qt::NoModifier);
        for (const Entity& e : d2.entities())
            if (e.type == EntityType::Polyline && e.meta.value("double").toBool())
                check(!e.meta.value(QStringLiteral("cutStart")).toBool(),
                      "dpoly-shift-start: no Shift, no cap");
    }

    // --- Trimming ONE line of a double run: the fence crosses that line and not the centreline, so
    //     that line alone comes back to the reference and the other keeps its length. Crossing only one
    //     line used to find nothing at all - the trim looked at the centreline, which the fence missed.
    {
        auto build = [](Document& d) {
            d.entities().clear();
            Entity wall;                       // the reference: a vertical line
            wall.id = QStringLiteral("wall");
            wall.type = EntityType::Line;
            wall.a = {100, -60};
            wall.b = {100, 60};
            d.entities().push_back(wall);
            Entity run;                        // a double run straight through it
            run.id = QStringLiteral("run");
            run.type = EntityType::Polyline;
            run.points = {{0, 0}, {200, 0}};   // centreline on y=0, lines at y=+/-10
            run.meta.insert(QStringLiteral("double"), true);
            run.meta.insert(QStringLiteral("gap"), 20.0);
            d.entities().push_back(run);
        };
        // A fence across the TOP line only (y=10), well clear of the centreline.
        Document d; build(d);
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(3);
        ed.selectById(QStringLiteral("wall"));
        ed.setTool(ToolId::Trim);
        ed.onPointerPress({150, 4}, Qt::LeftButton, Qt::NoModifier);   // fence from just above centre
        ed.onPointerMove({150, 16});
        ed.onPointerPress({150, 16}, Qt::LeftButton, Qt::NoModifier);  // up across the top line
        const Entity* run = nullptr;
        for (const Entity& e : d.entities()) if (e.id == QLatin1String("run")) run = &e;
        check(run != nullptr, "trim-one-rail: the run is still there");
        if (run) {
            // The centreline is untouched: the other line keeps its full length.
            check(run->points.size() == 2 && near(run->points[0].x, 0.0) &&
                      near(run->points[1].x, 200.0),
                  "trim-one-rail: the centreline is not moved, so the other line is not shortened");
            // The end that came back is recorded as a cap on ONE side.
            check(run->meta.value(QStringLiteral("cutEnd")).toBool(),
                  "trim-one-rail: the far end of that line is capped to the reference");
            check(run->meta.value(QStringLiteral("cutEndSide")).toInt() == +1,
                  "trim-one-rail: and the cap belongs to the line that was crossed, not to both");
            check(near(run->meta.value(QStringLiteral("cutEndAx")).toDouble(), 100.0),
                  "trim-one-rail: it comes back to the reference line at x=100");
        }
        // The case from the drawing: the reference is reached by ONE line only - the centreline never
        // gets near it - and the fence comes at that line from INSIDE the run, so it crosses the
        // centreline on the way. Both of those used to stop the trim dead: the object was not even
        // considered (its centreline does not meet the reference) and the gesture was read as a
        // whole-run trim (the fence crosses the centreline).
        {
            Document d3; d3.entities().clear();
            Entity ref;                        // only the bottom line reaches this
            ref.id = QStringLiteral("ref");
            ref.type = EntityType::Line;
            ref.a = {150, -60};
            ref.b = {150, -5};                 // stops short of the centreline at y=0
            d3.entities().push_back(ref);
            Entity run;
            run.id = QStringLiteral("run");
            run.type = EntityType::Polyline;
            run.points = {{0, 0}, {200, 0}};   // lines at y=+/-15
            run.meta.insert(QStringLiteral("double"), true);
            run.meta.insert(QStringLiteral("gap"), 30.0);
            d3.entities().push_back(run);
            Editor e3; e3.setDocument(&d3); noSnap(e3); e3.setPixelTolerance(3);
            e3.selectById(QStringLiteral("ref"));
            e3.setTool(ToolId::Trim);
            // From inside the run (y=5, above the centreline) down across the bottom line only.
            e3.onPointerPress({120, 5}, Qt::LeftButton, Qt::NoModifier);
            e3.onPointerMove({180, -20});
            e3.onPointerPress({180, -20}, Qt::LeftButton, Qt::NoModifier);
            const Entity* r = nullptr;
            for (const Entity& e : d3.entities()) if (e.id == QLatin1String("run")) r = &e;
            check(r && r->meta.value(QStringLiteral("cutEnd")).toBool(),
                  "trim-one-rail: a reference only one line reaches still trims that line");
            check(r && r->meta.value(QStringLiteral("cutEndSide")).toInt() == -1,
                  "trim-one-rail: the bottom line is the one that comes back");
            check(r && near(r->meta.value(QStringLiteral("cutEndAx")).toDouble(), 150.0),
                  "trim-one-rail: back to the reference at x=150");
            check(r && r->points.size() == 2 && near(r->points[1].x, 200.0),
                  "trim-one-rail: and the top line keeps its full length");
        }

        // One line, then the OTHER: each end has a slot per line, so the second trim does not replace
        // the first. With a single slot the line trimmed first came back the moment the other was done.
        {
            Document d4; d4.entities().clear();
            Entity wall;                       // both lines run to this
            wall.id = QStringLiteral("wall");
            wall.type = EntityType::Line;
            wall.a = {200, -80};
            wall.b = {200, 80};
            d4.entities().push_back(wall);
            Entity nearer;                     // and this stands short of it
            nearer.id = QStringLiteral("nearer");
            nearer.type = EntityType::Line;
            nearer.a = {150, -80};
            nearer.b = {150, 80};
            d4.entities().push_back(nearer);
            Entity run;
            run.id = QStringLiteral("run");
            run.type = EntityType::Polyline;
            run.points = {{0, 0}, {200, 0}};
            run.meta.insert(QStringLiteral("double"), true);
            run.meta.insert(QStringLiteral("gap"), 30.0);
            d4.entities().push_back(run);
            auto trimRail = [&d4](double fromY, double toY) {
                Editor ed; ed.setDocument(&d4); noSnap(ed); ed.setPixelTolerance(3);
                ed.selectById(QStringLiteral("nearer"));
                ed.setTool(ToolId::Trim);
                ed.onPointerPress({175, fromY}, Qt::LeftButton, Qt::NoModifier);
                ed.onPointerMove({175, toY});
                ed.onPointerPress({175, toY}, Qt::LeftButton, Qt::NoModifier);
            };
            trimRail(5, 25);                   // the top line
            const Entity* r = nullptr;
            for (const Entity& e : d4.entities()) if (e.id == QLatin1String("run")) r = &e;
            check(r && r->meta.value(QStringLiteral("cutEnd")).toBool() &&
                      r->meta.value(QStringLiteral("cutEndSide")).toInt() == +1,
                  "trim-both-rails: the top line is capped in the first slot");
            check(r && !r->meta.value(QStringLiteral("cutEnd2")).toBool(),
                  "trim-both-rails: and the second slot is still free");
            trimRail(-5, -25);                 // now the bottom line
            r = nullptr;
            for (const Entity& e : d4.entities()) if (e.id == QLatin1String("run")) r = &e;
            check(r && r->meta.value(QStringLiteral("cutEnd")).toBool() &&
                      r->meta.value(QStringLiteral("cutEndSide")).toInt() == +1,
                  "trim-both-rails: the TOP line's cut is still there afterwards");
            check(r && r->meta.value(QStringLiteral("cutEnd2")).toBool() &&
                      r->meta.value(QStringLiteral("cutEnd2Side")).toInt() == -1,
                  "trim-both-rails: and the bottom line has its own, in the other slot");
            check(r && near(r->meta.value(QStringLiteral("cutEndAx")).toDouble(), 150.0) &&
                      near(r->meta.value(QStringLiteral("cutEnd2Ax")).toDouble(), 150.0),
                  "trim-both-rails: both back to the reference");
        }

        // The case from the drawing: both lines already end on a wall - one cap, for both of them - and
        // then ONE of them is trimmed back to something nearer. The other must stay on the wall.
        {
            Document d5; d5.entities().clear();
            Entity wall;
            wall.id = QStringLiteral("wall");
            wall.type = EntityType::Line;
            wall.a = {200, -80};
            wall.b = {200, 80};
            d5.entities().push_back(wall);
            Entity nearer;
            nearer.id = QStringLiteral("nearer");
            nearer.type = EntityType::Line;
            nearer.a = {150, -80};
            nearer.b = {150, 80};
            d5.entities().push_back(nearer);
            Entity run;
            run.id = QStringLiteral("run");
            run.type = EntityType::Polyline;
            run.points = {{0, 0}, {200, 0}};
            run.meta.insert(QStringLiteral("double"), true);
            run.meta.insert(QStringLiteral("gap"), 30.0);
            // Both lines already capped to the wall, as a Shift-drawn or whole-run-trimmed end is.
            run.meta.insert(QStringLiteral("cutEnd"), true);
            run.meta.insert(QStringLiteral("cutEndKind"), QStringLiteral("line"));
            run.meta.insert(QStringLiteral("cutEndAx"), 200.0);
            run.meta.insert(QStringLiteral("cutEndAy"), -80.0);
            run.meta.insert(QStringLiteral("cutEndBx"), 200.0);
            run.meta.insert(QStringLiteral("cutEndBy"), 80.0);
            d5.entities().push_back(run);
            Editor ed; ed.setDocument(&d5); noSnap(ed); ed.setPixelTolerance(3);
            ed.selectById(QStringLiteral("nearer"));
            ed.setTool(ToolId::Trim);
            ed.onPointerPress({175, -5}, Qt::LeftButton, Qt::NoModifier);   // the bottom line only
            ed.onPointerMove({175, -25});
            ed.onPointerPress({175, -25}, Qt::LeftButton, Qt::NoModifier);
            const Entity* r = nullptr;
            for (const Entity& e : d5.entities()) if (e.id == QLatin1String("run")) r = &e;
            check(r && near(r->meta.value(QStringLiteral("cutEndAx")).toDouble(), 200.0) &&
                      r->meta.value(QStringLiteral("cutEndSide")).toInt() == +1,
                  "trim-kept-cap: the cap that was there stays, and becomes the OTHER line's");
            check(r && r->meta.value(QStringLiteral("cutEnd2")).toBool() &&
                      r->meta.value(QStringLiteral("cutEnd2Side")).toInt() == -1 &&
                      near(r->meta.value(QStringLiteral("cutEnd2Ax")).toDouble(), 150.0),
                  "trim-kept-cap: and the trimmed line comes back to the nearer reference");
        }

        // (A test of the Shift-extend-then-trim sequence was here and has been taken out. It never
        // failed on the code it was meant to guard, and its geometry was wrong about the extension: a cap
        // reaches past a run's end by about the gap times the tangent of the angle between the run and
        // what it is capped to - a fraction of a millimetre on a shallow fan-out, not something a unit
        // test can click inside. What the change was verified against instead was the sequence driven on
        // both versions, reading the status line.)

        // Clicking the EXTENSION of a line - the part only a cap provides, where it was stretched to meet
        // something - takes the extension off. There is nothing out there to cut back to (an extension
        // reaches something, and between that and the line's own end there is normally nothing at all),
        // so a trim there used to refuse and the only way back was undo.
        {
            Document d; d.entities().clear();
            Entity wall;
            wall.id = QStringLiteral("wall");
            wall.type = EntityType::Line;
            wall.a = {160, -50};
            wall.b = {160, 250};
            d.entities().push_back(wall);
            Entity run;
            run.id = QStringLiteral("run");
            run.type = EntityType::DoubleLine;
            run.a = {0, 0};
            run.b = {100, 100};                 // 45 degrees, so the extension is a whole gap long
            run.meta.insert(QStringLiteral("gap"), 20.0);
            // ONE line extended to the wall, the other left where the run ends.
            run.meta.insert(QStringLiteral("cutEnd"), true);
            run.meta.insert(QStringLiteral("cutEndKind"), QStringLiteral("line"));
            run.meta.insert(QStringLiteral("cutEndAx"), 160.0);
            run.meta.insert(QStringLiteral("cutEndAy"), -50.0);
            run.meta.insert(QStringLiteral("cutEndBx"), 160.0);
            run.meta.insert(QStringLiteral("cutEndBy"), 250.0);
            run.meta.insert(QStringLiteral("cutEndSide"), +1);
            d.entities().push_back(run);
            // where that line ends on its own, and where the cap draws it to
            const Vec2 o = (run.b - run.a).normalized().perp() * 10.0;
            std::vector<Vec2> rail = {run.a + o, run.b + o};
            const Vec2 raw = rail.back();
            applyDoubleRunCuts(rail, run, +1);
            check(distance(raw, rail.back()) > 10.0, "trim-extension: the line IS extended to start with");
            Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(4);
            ed.selectById(QStringLiteral("run"));
            ed.setTool(ToolId::Trim);
            const Vec2 click = lerp(raw, rail.back(), 0.5);   // half way along the extension
            ed.onPointerPress(click, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerPress(click, Qt::LeftButton, Qt::NoModifier);
            const Entity* r = nullptr;
            for (const Entity& e : d.entities()) if (e.id == QLatin1String("run")) r = &e;
            check(r && !r->meta.value(QStringLiteral("cutEnd")).toBool(),
                  "trim-extension: clicking the extension takes it off");
            if (r) {
                std::vector<Vec2> after = {r->a + o, r->b + o};
                applyDoubleRunCuts(after, *r, +1);
                check(distance(after.back(), raw) < 1e-6,
                      "trim-extension: that line ends where its own run ends again");
                std::vector<Vec2> other = {r->a - o, r->b - o};
                applyDoubleRunCuts(other, *r, -1);
                check(distance(other.back(), r->b - o) < 1e-6,
                      "trim-extension: and the other line is untouched");
            }
        }

        // A line crossed TWICE - once part way along and once at its own end, where it meets the body -
        // and the piece between them picked. This is the case from the real drawing: taking the nearest
        // crossing to the pick found the one at the end, and cutting back to THAT is the whole line, so
        // it was refused and something else got trimmed instead. The piece the pick is IN is what goes.
        {
            Document d; d.entities().clear();
            Entity body;                       // the body edge: crosses both lines at their ends
            body.id = QStringLiteral("body");
            body.type = EntityType::Line;
            body.a = {100, -30};
            body.b = {100, 30};
            d.entities().push_back(body);
            Entity fan;                        // and this crosses the LOWER line part way along
            fan.id = QStringLiteral("fan");
            fan.type = EntityType::Line;
            fan.a = {80, -30};
            fan.b = {80, -5};
            d.entities().push_back(fan);
            Entity run;
            run.id = QStringLiteral("run");
            run.type = EntityType::DoubleLine;
            run.a = {0, 0};
            run.b = {100, 0};                  // lines at y=+/-10, both ending on the body
            run.meta.insert(QStringLiteral("gap"), 20.0);
            d.entities().push_back(run);
            Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(4);
            // The run is what is selected - as it is on screen - and the piece clicked is the last
            // 20 units of its lower line, between the fan crossing and the body.
            ed.selectById(QStringLiteral("run"));
            ed.setTool(ToolId::Trim);
            ed.onPointerPress({95, -10}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerPress({95, -10}, Qt::LeftButton, Qt::NoModifier);
            const Entity* r = nullptr;
            for (const Entity& e : d.entities()) if (e.id == QLatin1String("run")) r = &e;
            check(r && r->meta.value(QStringLiteral("cutEnd")).toBool() &&
                      r->meta.value(QStringLiteral("cutEndSide")).toInt() == -1,
                  "trim-two-crossings: the piece between the two crossings comes off the lower line");
            check(r && near(r->meta.value(QStringLiteral("cutEndAx")).toDouble(), 80.0),
                  "trim-two-crossings: back to the crossing part way along, not the one at the end");
            // And nothing else was touched: the fan and the body are as they were.
            for (const Entity& e : d.entities()) {
                if (e.id == QLatin1String("fan"))
                    check(near(e.a.y, -30.0) && near(e.b.y, -5.0),
                          "trim-two-crossings: the boundary that bounds it is left alone");
                if (e.id == QLatin1String("body"))
                    check(near(e.a.y, -30.0) && near(e.b.y, 30.0),
                          "trim-two-crossings: and so is the body edge");
            }
        }

        // A crossing region over one LINE of a double polyline selects the run. It is picked on its two
        // visible lines, like a double line; the centreline between them is not drawn and never was the
        // thing to test against, so a small green box over a line used to select nothing at all.
        {
            Document d; d.entities().clear();
            Entity run;
            run.id = QStringLiteral("run");
            run.type = EntityType::Polyline;
            run.points = {{0, 0}, {100, 0}, {200, 60}};
            run.meta.insert(QStringLiteral("double"), true);
            run.meta.insert(QStringLiteral("gap"), 30.0);   // lines at +/-15
            d.entities().push_back(run);
            // A region needs press-release-move-press: the release is what arms the rectangle.
            auto box = [](Editor& ed, Vec2 a, Vec2 b) {
                ed.setTool(ToolId::Select);
                ed.onPointerPress(a, Qt::LeftButton, Qt::NoModifier);
                ed.onPointerRelease(a, Qt::LeftButton);
                ed.onPointerMove(b);
                ed.onPointerPress(b, Qt::LeftButton, Qt::NoModifier);
            };
            // A crossing box (right to left) over the UPPER line only, clear of the centreline: the
            // line runs through y=15 between x=40 and x=60.
            Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(3);
            box(ed, {60, 25}, {40, 10});
            check(ed.selection().size() == 1,
                  "select-rail: a crossing box over one line of a double polyline selects the run");
            // And a box entirely inside the GAP still selects nothing: the centreline is not drawn.
            Editor e2; e2.setDocument(&d); noSnap(e2); e2.setPixelTolerance(3);
            box(e2, {60, 6}, {40, -6});
            check(e2.selection().empty(),
                  "select-rail: and a box in the gap between the lines still selects nothing");
        }

        // A boundary that crosses the line PAST A KNEE. A cut used to be able to clip only the line's
        // last segment, so anything further along was refused - "the boundary does not reach that line"
        // - and since the runs in a real harness drawing all have knees, that was most of them.
        {
            Document d; d.entities().clear();
            Entity wall;                       // crosses both lines on their FIRST segment
            wall.id = QStringLiteral("wall");
            wall.type = EntityType::Line;
            wall.a = {50, -40};
            wall.b = {50, 40};
            d.entities().push_back(wall);
            Entity run;                        // an L: lines at +/-10, so a knee between the crossing
            run.id = QStringLiteral("run");     // and the far end
            run.type = EntityType::Polyline;
            run.points = {{0, 0}, {100, 0}, {100, 100}};
            run.meta.insert(QStringLiteral("double"), true);
            run.meta.insert(QStringLiteral("gap"), 20.0);
            d.entities().push_back(run);
            Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(4);
            QStringList said;
            QObject::connect(&ed, &Editor::message, [&said](const QString& m) { said << m; });
            ed.selectById(QStringLiteral("wall"));
            ed.setTool(ToolId::Trim);
            // Click the + line well past the knee: the piece from the wall onwards, knee included, goes.
            ed.onPointerPress({90, 60}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerPress({90, 60}, Qt::LeftButton, Qt::NoModifier);
            const Entity* r = nullptr;
            for (const Entity& e : d.entities()) if (e.id == QLatin1String("run")) r = &e;
            check(r && r->meta.value(QStringLiteral("cutEnd")).toBool(),
                  "trim-past-knee: a boundary crossing the line before its knee still trims it");
            check(r && r->meta.value(QStringLiteral("cutEndSide")).toInt() == +1,
                  "trim-past-knee: and only the line that was clicked");
            check(r && near(r->meta.value(QStringLiteral("cutEndAx")).toDouble(), 50.0),
                  "trim-past-knee: back to the boundary at x=50");
            check(!said.join(QStringLiteral(" | ")).contains(QLatin1String("does not reach")),
                  "trim-past-knee: and it is not refused for being out of reach");
        }

        // Two double runs crossing, which is the drawing: the selected run is the boundary and ONE line
        // of the other run - a double polyline with a rounded knee - is cut back to it. Both gestures.
        {
            auto scene = [](Document& d) {
                d.entities().clear();
                Entity flat;                       // the selection: lines at y = +/-50
                flat.id = QStringLiteral("flat");
                flat.type = EntityType::DoubleLine;
                flat.a = {-400, 0};
                flat.b = {400, 0};
                flat.meta.insert(QStringLiteral("gap"), 100.0);
                d.entities().push_back(flat);
                Entity slant;                      // flat, then rising, with a rounded knee
                slant.id = QStringLiteral("slant");
                slant.type = EntityType::Polyline;
                slant.points = {{-900, -500}, {-500, -500}, {400, -20}};
                slant.meta.insert(QStringLiteral("double"), true);
                slant.meta.insert(QStringLiteral("gap"), 120.0);
                QJsonArray radii;
                radii.append(0.0);
                radii.append(150.0);
                radii.append(0.0);
                slant.meta.insert(QStringLiteral("cornerRadii"), radii);
                d.entities().push_back(slant);
            };
            auto check1 = [](Document& d, const char* what) {
                const Entity* s = nullptr;
                for (const Entity& e : d.entities())
                    if (e.id == QLatin1String("slant")) s = &e;
                check(s && s->meta.value(QStringLiteral("cutEnd")).toBool() &&
                          s->meta.value(QStringLiteral("cutEndSide")).toInt() == +1,
                      what);
                check(s && near(s->meta.value(QStringLiteral("cutEndAy")).toDouble(), -50.0),
                      "trim-crossing-runs: back to the boundary's own line, at y=-50");
            };
            // With a fence across that line only.
            Document dA; scene(dA);
            {
                Editor ed; ed.setDocument(&dA); noSnap(ed); ed.setPixelTolerance(4);
                ed.selectById(QStringLiteral("flat"));
                ed.setTool(ToolId::Trim);
                ed.onPointerPress({330, 25}, Qt::LeftButton, Qt::NoModifier);
                ed.onPointerMove({270, -35});
                ed.onPointerPress({270, -35}, Qt::LeftButton, Qt::NoModifier);
            }
            check1(dA, "trim-crossing-runs: a fence across one line of the other run cuts that line");
            // And with a single click on it.
            Document dB; scene(dB);
            {
                Editor ed; ed.setDocument(&dB); noSnap(ed); ed.setPixelTolerance(4);
                ed.selectById(QStringLiteral("flat"));
                ed.setTool(ToolId::Trim);
                ed.onPointerPress({300, -8}, Qt::LeftButton, Qt::NoModifier);
                ed.onPointerPress({300, -8}, Qt::LeftButton, Qt::NoModifier);
            }
            check1(dB, "trim-crossing-runs: and so does a click on it");
        }

        // A plain CLICK on the piece that should be gone - which is how trimming is asked for first, and
        // the one gesture that used to do nothing at all, the command wanting two clicks that straddle
        // something.
        {
            auto scene = [](Document& d) {
                d.entities().clear();
                Entity wall;                   // both lines run up to this
                wall.id = QStringLiteral("wall");
                wall.type = EntityType::Line;
                wall.a = {160, -120};
                wall.b = {160, 120};
                d.entities().push_back(wall);
                Entity slant;                  // crosses the LOWER line at x=110
                slant.id = QStringLiteral("slant");
                slant.type = EntityType::Line;
                slant.a = {60, -60};
                slant.b = {140, 20};
                d.entities().push_back(slant);
                Entity run;
                run.id = QStringLiteral("run");
                run.type = EntityType::DoubleLine;
                run.a = {0, 0};
                run.b = {160, 0};              // lines at y=+/-10
                run.meta.insert(QStringLiteral("gap"), 20.0);
                d.entities().push_back(run);
            };
            auto click = [](Document& d, const char* selectId, Vec2 at) {
                Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(4);
                ed.selectById(QString::fromLatin1(selectId));
                ed.setTool(ToolId::Trim);
                ed.onPointerPress(at, Qt::LeftButton, Qt::NoModifier);
                ed.onPointerPress(at, Qt::LeftButton, Qt::NoModifier);   // same point: a click
            };
            // Clicking the lower line beyond the slant takes that piece, and only that line.
            Document d9; scene(d9);
            click(d9, "slant", {130, -10});
            const Entity* r = nullptr;
            for (const Entity& e : d9.entities()) if (e.id == QLatin1String("run")) r = &e;
            check(r && r->meta.value(QStringLiteral("cutEnd")).toBool() &&
                      r->meta.value(QStringLiteral("cutEndSide")).toInt() == -1,
                  "trim-click: clicking one line trims that line back to the boundary");
            check(r && near(r->b.x, 160.0),
                  "trim-click: and the run itself is left alone, so the other line is untouched");

            // Clicking the WALL between the two lines opens it, which is what a wire entering a body
            // needs. The run is the selection here - it is the boundary - and the wall is what goes.
            Document d10; scene(d10);
            click(d10, "run", {160, 0});
            int walls = 0;
            bool openedAbove = false, openedBelow = false;
            for (const Entity& e : d10.entities()) {
                if (e.type != EntityType::Line || e.id == QLatin1String("slant")) continue;
                ++walls;
                if (near(e.a.y, 10.0) || near(e.b.y, 10.0)) openedAbove = true;
                if (near(e.a.y, -10.0) || near(e.b.y, -10.0)) openedBelow = true;
            }
            check(walls == 2, "trim-click: the wall between the two lines comes away, leaving two pieces");
            check(openedAbove && openedBelow,
                  "trim-click: and the opening is exactly the gap between them");
        }

        // Whichever way round the two are picked. Selecting the RUN and fencing across one of its lines
        // is what a person does - the selection is the thing being worked on - and it used to do nothing
        // at all, the run being taken for the boundary and skipped.
        {
            Document d7; d7.entities().clear();
            Entity slant;                      // the boundary, crossing the lower line at x=110
            slant.id = QStringLiteral("slant");
            slant.type = EntityType::Line;
            slant.a = {60, -60};
            slant.b = {140, 20};
            d7.entities().push_back(slant);
            Entity run;
            run.id = QStringLiteral("run");
            run.type = EntityType::DoubleLine;
            run.a = {0, 0};
            run.b = {160, 0};                  // lines at y=+/-10
            run.meta.insert(QStringLiteral("gap"), 20.0);
            d7.entities().push_back(run);
            Editor ed; ed.setDocument(&d7); noSnap(ed); ed.setPixelTolerance(3);
            ed.selectById(QStringLiteral("run"));      // the RUN, not the boundary
            ed.setTool(ToolId::Trim);
            ed.onPointerPress({130, -4}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerMove({130, -20});
            ed.onPointerPress({130, -20}, Qt::LeftButton, Qt::NoModifier);
            const Entity* r = nullptr;
            for (const Entity& e : d7.entities()) if (e.id == QLatin1String("run")) r = &e;
            check(r && r->meta.value(QStringLiteral("cutEnd")).toBool() &&
                      r->meta.value(QStringLiteral("cutEndSide")).toInt() == -1,
                  "trim-either-way: selecting the run and fencing its lower line trims that line");
            check(ed.selection().size() == 1,
                  "trim-either-way: and what was selected stays selected for the next one");
        }

        // A trim that does nothing must not cost the selection either: re-picking the reference before
        // every retry is most of the work of trimming.
        {
            Document d8; d8.entities().clear();
            Entity far;
            far.id = QStringLiteral("far");
            far.type = EntityType::Line;
            far.a = {900, -50};
            far.b = {900, 50};                 // nowhere near the run
            d8.entities().push_back(far);
            Entity run;
            run.id = QStringLiteral("run");
            run.type = EntityType::Polyline;
            run.points = {{0, 0}, {160, 0}};
            run.meta.insert(QStringLiteral("double"), true);
            run.meta.insert(QStringLiteral("gap"), 20.0);
            d8.entities().push_back(run);
            Editor ed; ed.setDocument(&d8); noSnap(ed); ed.setPixelTolerance(3);
            ed.selectById(QStringLiteral("far"));
            ed.setTool(ToolId::Trim);
            ed.onPointerPress({80, 4}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerMove({80, 20});
            ed.onPointerPress({80, 20}, Qt::LeftButton, Qt::NoModifier);
            check(ed.selection().size() == 1 &&
                      ed.selection().front() == QLatin1String("far"),
                  "trim-keeps-selection: a trim that does nothing keeps the reference selected");
        }

        // A line that already ENDS on the boundary has nothing past it to remove, and the piece the pick
        // asks for is the whole line. That is refused - and said out loud - rather than recorded as a cap
        // that collapses the line to a point.
        {
            Document d6; d6.entities().clear();
            Entity wall;
            wall.id = QStringLiteral("wall");
            wall.type = EntityType::Line;
            wall.a = {200, -100};
            wall.b = {200, 100};
            d6.entities().push_back(wall);
            Entity run;
            run.id = QStringLiteral("run");
            run.type = EntityType::Polyline;
            run.points = {{0, 0}, {200, 0}};   // both lines end ON the wall
            run.meta.insert(QStringLiteral("double"), true);
            run.meta.insert(QStringLiteral("gap"), 30.0);
            d6.entities().push_back(run);
            Editor ed; ed.setDocument(&d6); noSnap(ed); ed.setPixelTolerance(3);
            QStringList said;   // every message, since the command says more than one thing
            QObject::connect(&ed, &Editor::message, [&said](const QString& m) { said << m; });
            ed.selectById(QStringLiteral("wall"));
            ed.setTool(ToolId::Trim);
            ed.onPointerPress({190, 5}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerMove({190, 25});
            ed.onPointerPress({190, 25}, Qt::LeftButton, Qt::NoModifier);
            const Entity* r = nullptr;
            for (const Entity& e : d6.entities()) if (e.id == QLatin1String("run")) r = &e;
            check(r && !r->meta.value(QStringLiteral("cutStart")).toBool() &&
                      !r->meta.value(QStringLiteral("cutEnd")).toBool(),
                  "trim-nothing-past: a line ending on the boundary is left alone, not collapsed");
            check(r && r->points.size() == 2 && near(r->points[1].x, 200.0),
                  "trim-nothing-past: and the run itself is untouched");
            check(said.join(QStringLiteral(" | ")).contains(QLatin1String("whole of that line")),
                  "trim-nothing-past: and it says why nothing happened");
        }

        // A fence across the CENTRELINE still trims the whole run, as it always did.
        Document d2; build(d2);
        Editor e2; e2.setDocument(&d2); noSnap(e2); e2.setPixelTolerance(3);
        e2.selectById(QStringLiteral("wall"));
        e2.setTool(ToolId::Trim);
        e2.onPointerPress({150, -4}, Qt::LeftButton, Qt::NoModifier);
        e2.onPointerMove({150, 4});
        e2.onPointerPress({150, 4}, Qt::LeftButton, Qt::NoModifier);
        for (const Entity& e : d2.entities())
            if (e.id == QLatin1String("run")) {
                check(near(e.points[1].x, 100.0),
                      "trim-one-rail: a fence over the centreline still takes the whole run back");
                check(!e.meta.contains(QStringLiteral("cutEndSide")) ||
                          e.meta.value(QStringLiteral("cutEndSide")).toInt() == 0,
                      "trim-one-rail: and that cap belongs to both lines");
            }
    }

    // --- Shift-starting at a CORNER picks the segment both lines can actually reach, and only within
    //     that segment's own length - up to its knee or its end.
    {
        // A wall with a SHORT step off the top: the run is drawn up-left from the corner, so the far
        // rail would meet the step's line well past the end of it. The step is the nearest segment to
        // the click, and it is the wrong one: the vertical is what both rails can reach.
        auto runFrom = [](double stepLength) {
            Document d; d.entities().clear();
            Entity wall;
            wall.id = QStringLiteral("wall");
            wall.type = EntityType::Polyline;
            // The step comes FIRST, so it is also the first candidate found at the corner: the old code
            // took the first one and capped to it however far past its end the meeting point fell. The
            // wall runs UP from the corner, which is the side the run is drawn towards - so it is a
            // segment both rails can reach when the step is too short for them.
            wall.points = {{stepLength, 0}, {0, 0}, {0, 90}};
            d.entities().push_back(wall);
            Editor ed; ed.setDocument(&d);
            auto& s = ed.snapSettings();
            s.objectSnap = true; s.gridSnap = false; s.ortho = false; s.polar = false;
            ed.setPixelTolerance(4);
            ed.doubleLineGap() = 14.0;
            ed.setTool(ToolId::DoublePolyline);
            ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::ShiftModifier);
            ed.onPointerPress({-120, 55}, Qt::LeftButton, Qt::NoModifier);
            ed.onKey(Qt::Key_Return, Qt::NoModifier);
            Entity run;
            for (const Entity& e : d.entities())
                if (e.type == EntityType::Polyline && e.meta.value("double").toBool()) run = e;
            return run;
        };
        // A step 60 long: the second rail meets it about 33 along, inside it - so the step is used.
        const Entity wide = runFrom(60.0);
        check(wide.meta.value(QStringLiteral("cutStart")).toBool(),
              "dpoly-reach: a long enough step is what the run starts from");
        check(near(wide.meta.value(QStringLiteral("cutStartAy")).toDouble(), 0.0) &&
                  near(wide.meta.value(QStringLiteral("cutStartBy")).toDouble(), 0.0),
              "dpoly-reach: and that is the horizontal step");
        // The same step only 8 long: the second rail cannot reach it within its length, so the segment
        // the run starts from is the vertical one instead - the other segment through the same corner.
        const Entity narrow = runFrom(8.0);
        check(narrow.meta.value(QStringLiteral("cutStart")).toBool(),
              "dpoly-reach: a step too short to reach is not the end of it");
        check(near(narrow.meta.value(QStringLiteral("cutStartAx")).toDouble(), 0.0) &&
                  near(narrow.meta.value(QStringLiteral("cutStartBx")).toDouble(), 0.0),
              "dpoly-reach: the segment chosen is the one both lines can reach - the vertical");
        // Nothing under the click: nothing is capped, and the run simply starts where it was drawn.
        {
            Document d; d.entities().clear();
            Editor ed; ed.setDocument(&d); noSnap(ed);
            ed.setTool(ToolId::DoublePolyline);
            ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::ShiftModifier);
            ed.onPointerPress({-120, 55}, Qt::LeftButton, Qt::NoModifier);
            ed.onKey(Qt::Key_Return, Qt::NoModifier);
            for (const Entity& e : d.entities())
                if (e.meta.value("double").toBool())
                    check(!e.meta.value(QStringLiteral("cutStart")).toBool(),
                          "dpoly-reach: Shift on empty space caps nothing");
        }
    }

    // --- Pulling a double run tracks along the RAIL that was grabbed, not up the middle of it. The
    //     green polar/ortho lock line is drawn from this point, and the angle is measured from it.
    {
        Document d; d.entities().clear();
        Entity dp;
        dp.id = QStringLiteral("dp");
        dp.type = EntityType::Polyline;
        dp.meta.insert(QStringLiteral("double"), true);
        dp.meta.insert(QStringLiteral("gap"), 10.0);
        dp.points = {{0, 0}, {200, 0}, {200, 120}};   // centreline; rails at y=+/-5
        d.entities().push_back(dp);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setPixelTolerance(4);
        ed.selectById(QStringLiteral("dp"));
        // Grab the far end of the TOP rail: the run's last vertex offset to +5 across, which on the
        // vertical leg means x = 200 - 5 = 195.
        ed.onPointerPress({195, 120}, Qt::LeftButton, Qt::NoModifier);
        const auto anchor = ed.trackingAnchor();
        check(anchor.has_value(), "dpoly-track: a joint was grabbed");
        if (anchor) {
            // The adjacent point on THE SAME rail - the mitred corner of that rail, at (195, 5) -
            // and emphatically not the centreline corner at (200, 0).
            check(near(anchor->x, 195.0) && near(anchor->y, 5.0),
                  "dpoly-track: tracking is measured from the adjacent point of the rail pulled");
            check(!near(anchor->x, 200.0) || !near(anchor->y, 0.0),
                  "dpoly-track: and not from the centreline between the two lines");
        }
        // The other rail's joint tracks along the OTHER rail.
        Editor e2; e2.setDocument(&d); noSnap(e2); e2.setPixelTolerance(4);
        e2.selectById(QStringLiteral("dp"));
        e2.onPointerPress({205, 120}, Qt::LeftButton, Qt::NoModifier);
        const auto a2 = e2.trackingAnchor();
        check(a2 && near(a2->x, 205.0) && near(a2->y, -5.0),
              "dpoly-track: the bottom rail tracks from the bottom rail");

        // A double LINE has parallel rails, so the anchor is simply the other end of the same rail.
        Document d2; d2.entities().clear();
        Entity dl;
        dl.id = QStringLiteral("dl");
        dl.type = EntityType::DoubleLine;
        dl.a = {0, 0};
        dl.b = {100, 0};
        dl.meta.insert(QStringLiteral("gap"), 10.0);
        d2.entities().push_back(dl);
        Editor e3; e3.setDocument(&d2); noSnap(e3); e3.setPixelTolerance(4);
        e3.selectById(QStringLiteral("dl"));
        e3.onPointerPress({100, 5}, Qt::LeftButton, Qt::NoModifier);   // the top rail's B end
        const auto a3 = e3.trackingAnchor();
        check(a3 && near(a3->x, 0.0) && near(a3->y, 5.0),
              "dline-track: tracking runs along the rail that was grabbed");
    }

    // --- Mirroring SEVERAL objects as a copy: every copy survives being placed.
    //     The copies used to be appended one at a time while the code still held pointers into the
    //     very vector it was appending to; the first reallocation left the rest dangling, so the
    //     second polyline onwards was copied from freed memory and drew nothing.
    {
        Document d; d.entities().clear();
        // Enough of them that appending is certain to reallocate at some point.
        const int n = 8;
        for (int i = 0; i < n; ++i) {
            Entity pl;
            pl.id = QStringLiteral("p%1").arg(i);
            pl.type = EntityType::Polyline;
            pl.points = {{0, static_cast<double>(i) * 10},
                         {100, static_cast<double>(i) * 10},
                         {100, static_cast<double>(i) * 10 + 5}};
            d.entities().push_back(pl);
        }
        Editor ed; ed.setDocument(&d); noSnap(ed);
        for (int i = 0; i < n; ++i) ed.selectById(QStringLiteral("p%1").arg(i), i > 0);
        check(ed.selection().size() == static_cast<size_t>(n), "mirror-many: all of them selected");
        ed.setTool(ToolId::Mirror);
        ed.onPointerPress({200, 0}, Qt::LeftButton, Qt::NoModifier);     // mirror line x=200,
        ed.onPointerPress({200, 100}, Qt::LeftButton, Qt::NoModifier);   // vertical
        ed.commitMirror(/*copy=*/true);
        check(d.entities().size() == static_cast<size_t>(2 * n),
              "mirror-many: a copy of every one of them is on the drawing");
        // Every copy is a real polyline, mirrored about x=200 - not garbage that happens to be there.
        int good = 0;
        for (const Entity& e : d.entities()) {
            if (e.points.size() != 3) continue;
            bool sane = true;
            for (const Vec2& v : e.points)
                if (!std::isfinite(v.x) || !std::isfinite(v.y)) sane = false;
            if (!sane) continue;
            // A mirrored copy has its long leg running from x=400 back to x=300.
            if (near(e.points[0].x, 400.0) && near(e.points[1].x, 300.0)) ++good;
        }
        check(good == n, qPrintable(QStringLiteral("mirror-many: %1 of %2 copies came out as real "
                                                  "geometry").arg(good).arg(n)));
        // And the copies are what is left selected, so the next command acts on them.
        check(ed.selection().size() == static_cast<size_t>(n),
              "mirror-many: the copies are the selection");
        for (const QString& id : ed.selection()) {
            const Entity* e = nullptr;
            for (const Entity& x : d.entities()) if (x.id == id) e = &x;
            check(e && e->points.size() == 3 && near(e->points[0].x, 400.0),
                  "mirror-many: every selected id names a mirrored copy");
        }
    }

    // --- Double polyline + "g" gap command.
    {
        // Double polyline records double=true and the gap.
        {
            Document d; d.entities().clear();
            Editor ed; ed.setDocument(&d); noSnap(ed);
            ed.doubleLineGap() = 6.0;
            ed.setTool(ToolId::DoublePolyline);
            ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerPress({20, 0}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerPress({20, 20}, Qt::LeftButton, Qt::NoModifier);
            ed.onKey(Qt::Key_Return, Qt::NoModifier); // finish
            check(d.entities().size() == 1, "dpoly: one entity");
            const Entity& e = d.entities()[0];
            check(e.type == EntityType::Polyline && e.points.size() == 3 &&
                      e.meta.value("double").toBool() && near(e.meta.value("gap").toDouble(), 6.0),
                  "dpoly: double polyline with gap");
        }
        // "g" then a value sets the double-line gap via the command line.
        {
            Document d; d.entities().clear();
            Editor ed; ed.setDocument(&d); noSnap(ed);
            ed.setTool(ToolId::DoubleLine);
            check(ed.acceptCommandInput(QStringLiteral("g")), "g: enters gap mode");
            check(ed.acceptCommandInput(QStringLiteral("15")), "g: value consumed");
            check(near(ed.doubleLineGap(), 15.0), "g: gap updated to 15");
        }
        // "ls" line-start: the next click picks a reference line; the double poly
        // then stores it (both runs start from it). The reference click adds no vertex.
        {
            Document d; d.entities().clear();
            Entity ref; ref.id = "ref"; ref.type = EntityType::Line;
            ref.a = {0, -100}; ref.b = {0, 100}; // a vertical reference line at x=0
            d.entities().push_back(ref);
            Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(3);
            ed.setTool(ToolId::DoublePolyline);
            check(ed.acceptCommandInput(QStringLiteral("ls")), "ls: consumed by double polyline");
            ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);   // picks the ref line
            ed.onPointerPress({40, 0}, Qt::LeftButton, Qt::NoModifier);  // first vertex
            ed.onPointerPress({40, 40}, Qt::LeftButton, Qt::NoModifier); // second vertex
            ed.onKey(Qt::Key_Return, Qt::NoModifier);
            check(d.entities().size() == 2, "ls: reference click adds no vertex");
            const Entity& dp = d.entities().back();
            check(dp.type == EntityType::Polyline && dp.meta.value("double").toBool() &&
                      dp.meta.value("lsRef").toBool() && dp.points.size() == 2,
                  "ls: double polyline stores the reference line");
        }
        // offsetPolyline produces a parallel run on each side.
        {
            std::vector<Vec2> pts = {{0, 0}, {10, 0}, {10, 10}};
            auto left = offsetPolyline(pts, 2.0);
            auto right = offsetPolyline(pts, -2.0);
            check(left.size() == 3 && right.size() == 3, "offsetPolyline: vertex count");
            check(near(left[0].y, 2.0) && near(right[0].y, -2.0), "offsetPolyline: sides");
        }
        // "f" sets a fillet radius; the polyline stores it and rounds corners.
        {
            Document d; d.entities().clear();
            Editor ed; ed.setDocument(&d); noSnap(ed);
            ed.setTool(ToolId::Polyline);
            check(ed.acceptCommandInput(QStringLiteral("f")), "f: enters fillet mode");
            check(ed.acceptCommandInput(QStringLiteral("5")), "f: radius consumed");
            ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerPress({20, 0}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerPress({20, 20}, Qt::LeftButton, Qt::NoModifier);
            ed.onKey(Qt::Key_Return, Qt::NoModifier);
            check(d.entities().size() == 1 &&
                      near(d.entities()[0].meta.value("cornerRadii").toArray()[1].toDouble(), 5.0),
                  "f: polyline keeps cornerRadius");
        }
        // roundedPolyline inserts arc points at the corner.
        {
            std::vector<Vec2> pts = {{0, 0}, {10, 0}, {10, 10}};
            auto r = roundedPolyline(pts, 3.0);
            check(r.size() > pts.size(), "roundedPolyline: adds fillet arc points");
        }
    }

    // --- Double line: grabbing a VISIBLE end joint moves only that end.
    // Regression: with object snap on, the grab used to snap the cursor onto the
    // invisible centerline (off the grips), so it jumped to the centerline
    // midpoint or translated the whole line. Snap now follows the visible lines
    // and the grab is tested against the raw click.
    {
        Document d; d.entities().clear();
        Entity dl; dl.id = "dl"; dl.type = EntityType::DoubleLine;
        dl.a = {0, 0}; dl.b = {40, 0}; dl.meta.insert("gap", 10.0);
        d.entities().push_back(dl);
        Editor ed; ed.setDocument(&d);
        auto& s = ed.snapSettings();         // object snap ON (the buggy config)
        s.objectSnap = true; s.snapEndpoint = true; s.snapMidpoint = true;
        s.gridSnap = false; s.ortho = false; s.polar = false;
        ed.selectAll();
        // Click the top-left VISIBLE end (a + perpendicular offset gap/2 = (0,5)),
        // then move and click to place it up-and-right.
        ed.onPointerPress({0, 5}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({10, 20});
        ed.onPointerPress({10, 20}, Qt::LeftButton, Qt::NoModifier);
        const Entity& e = d.entities()[0];
        check(e.type == EntityType::DoubleLine, "dl-grab: still a double line");
        check(near(e.b.x, 40.0) && near(e.b.y, 0.0), "dl-grab: end B unchanged (not a whole-line move)");
        // The GRABBED visible corner (a ± perp*gap/2), not the centerline end,
        // must land on the cursor; the pair stays parallel with the gap kept.
        const Vec2 nrm = (e.b - e.a).normalized().perp() * 5.0; // gap/2 = 5
        const double dTop = distance(e.a + nrm, {10, 20});
        const double dBot = distance(e.a - nrm, {10, 20});
        check(std::min(dTop, dBot) < 1e-3, "dl-grab: grabbed corner follows the cursor");
    }

    // --- Double line joints are ONLY on the two physical lines (no centerline joint
    // between them). Grabbing a PHYSICAL-line midpoint moves the whole double line.
    {
        Document d; d.entities().clear();
        Entity dl; dl.id = "dl"; dl.type = EntityType::DoubleLine;
        dl.a = {0, 0}; dl.b = {40, 0}; dl.meta.insert("gap", 10.0);
        d.entities().push_back(dl);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.selectAll();
        // The top physical line's midpoint is (20, 5) (gap/2 = 5); grab and move it.
        ed.onPointerPress({20, 5}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({20, 15});
        ed.onPointerPress({20, 15}, Qt::LeftButton, Qt::NoModifier);
        const Entity& e = d.entities()[0];
        check(near(e.a.y, 10.0) && near(e.b.y, 10.0) && near(e.a.x, 0.0) && near(e.b.x, 40.0),
              "dl-midjoint: physical-line midpoint moves the whole double line");
    }

    // --- Double line region selection: a crossing box BETWEEN the lines (not
    // touching either) must NOT select; touching a visible line (or fully
    // enclosing the pair) selects.
    {
        Document d; d.entities().clear();
        Entity dl; dl.id = "dl"; dl.type = EntityType::DoubleLine;
        // gap 40 => visible lines at y = +/-20; box corners stay >10 (the click
        // tolerance) from the lines so a press in the gap starts a region instead
        // of click-picking the line.
        dl.a = {0, 0}; dl.b = {40, 0}; dl.meta.insert("gap", 40.0);
        d.entities().push_back(dl);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        auto regionSelect = [&](Vec2 c1, Vec2 c2) {       // c2.x<c1.x => crossing, else window
            ed.clearSelection();
            ed.onPointerPress(c1, Qt::LeftButton, Qt::NoModifier); // anchor on empty space
            ed.onPointerRelease(c1, Qt::LeftButton);               // -> RectPending
            ed.onPointerMove(c2);
            ed.onPointerPress(c2, Qt::LeftButton, Qt::NoModifier); // opposite corner commits
        };
        regionSelect({20, 5}, {10, -5});    // crossing, entirely inside the gap (y in [-5,5])
        check(ed.selection().empty(), "dl-region: crossing the gap (no line touched) does NOT select");
        regionSelect({20, 35}, {10, -35});  // crossing, spans through both visible lines
        check(ed.selection().size() == 1, "dl-region: crossing a visible line selects");
        regionSelect({-5, -35}, {45, 35});  // window enclosing the whole double line
        check(ed.selection().size() == 1, "dl-region: window enclosing both lines selects");
    }

    // --- Explode breaks a compound object into its drawn primitives.
    {
        // Double line -> two lines.
        {
            Document d; d.entities().clear();
            Entity dl; dl.id = "dl"; dl.type = EntityType::DoubleLine;
            dl.a = {0, 0}; dl.b = {40, 0}; dl.meta.insert("gap", 8.0);
            d.entities().push_back(dl);
            Editor ed; ed.setDocument(&d); noSnap(ed);
            ed.selectAll();
            ed.explodeSelected();
            check(d.entities().size() == 2, "explode: double line -> 2 lines");
            for (const auto& e : d.entities()) check(e.type == EntityType::Line, "explode: parts are lines");
        }
        // Connector -> body + pin lines (+ text).
        {
            Document d; d.entities().clear();
            Entity c; c.id = "c"; c.type = EntityType::Connector; c.a = {0, 0};
            ConnectorSpec spec = defaultConnectorSpec(3);
            spec.designator = "X1";
            writeConnectorSpec(c.meta, spec);
            d.entities().push_back(c);
            Editor ed; ed.setDocument(&d); noSnap(ed);
            ed.selectAll();
            ed.explodeSelected();
            // 4 body lines + 3 pin lines = 7 lines, plus designator + 3 pin labels = texts.
            int lines = 0;
            for (const auto& e : d.entities()) if (e.type == EntityType::Line) ++lines;
            check(lines >= 7, "explode: connector -> body + pin lines");
            check(d.entities().size() > 7, "explode: connector keeps labels as text");
        }
    }

    // --- Purge (AutoCAD sense): drop empty layers and stray/degenerate objects,
    //     keeping layer 0, the active layer, and real geometry.
    {
        Document d; d.entities().clear();
        d.layers() = Document::defaultLayers();
        d.layers().push_back(Layer{"Empty", "#ffffff", true, false, 2.0}); // no entities -> purged
        d.setActiveLayer("0");
        Entity ok; ok.id = "ok"; ok.type = EntityType::Line; ok.a = {0, 0}; ok.b = {10, 0};
        ok.layer = "0";
        Entity zero; zero.id = "z"; zero.type = EntityType::Line; zero.a = {5, 5}; zero.b = {5, 5}; // stray
        zero.layer = "0";
        d.entities().push_back(ok);
        d.entities().push_back(zero);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.purgeUnused();
        bool emptyGone = true, zeroKept = false;
        for (const auto& l : d.layers()) {
            if (l.name == "Empty") emptyGone = false;
            if (l.name == "0") zeroKept = true;
        }
        check(d.entities().size() == 1 && d.entities()[0].id == "ok",
              "purge: removes the zero-length stray line, keeps real geometry");
        check(emptyGone && zeroKept,
              "purge: removes every unused layer but keeps layer 0 (and the active layer)");
    }

    // --- Ctrl+X cuts to the clipboard (removes originals); Ctrl+V re-places them.
    {
        Document d; d.entities().clear();
        Entity l; l.id = "l"; l.type = EntityType::Line; l.a = {0, 0}; l.b = {10, 0};
        d.entities().push_back(l);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.selectAll();
        ed.onKey(Qt::Key_X, Qt::ControlModifier); // cut
        check(d.entities().empty(), "cut: originals removed from the document");
        ed.onKey(Qt::Key_V, Qt::ControlModifier);                 // paste -> place mode
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier); // drop at the base point
        check(d.entities().size() == 1 && d.entities()[0].type == EntityType::Line,
              "cut: Ctrl+V re-places the cut object");
        ed.onKey(Qt::Key_Z, Qt::ControlModifier); // undo the cut deletion is recoverable
        // (no assertion on undo state; just exercising it)
    }

    // --- Linear dimension: two points + the dimension-line height (3 clicks).
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setDimMode(Editor::DimMode::Linear);
        ed.setTool(ToolId::Dimension);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({40, 0}, Qt::LeftButton, Qt::NoModifier);
        check(d.entities().empty(), "dim-linear: not placed until the height click");
        ed.onPointerPress({40, 15}, Qt::LeftButton, Qt::NoModifier); // dimension-line height
        check(d.entities().size() == 1 && d.entities()[0].type == EntityType::Dimension &&
                  d.entities()[0].meta.value("dimType").toString() == QLatin1String("linear") &&
                  d.entities()[0].points.size() == 3,
              "dim-linear: created with the height offset (3 points)");
    }

    // --- Radius dimension: pick a circle, then place the text (2 clicks).
    {
        Document d; d.entities().clear();
        Entity c; c.id = "c"; c.type = EntityType::Circle; c.center = {0, 0}; c.radius = 30;
        d.entities().push_back(c);
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(2);
        ed.setDimMode(Editor::DimMode::Radius);
        ed.setTool(ToolId::Dimension);
        ed.onPointerPress({30, 0}, Qt::LeftButton, Qt::NoModifier); // on the ring -> picks circle
        check(d.entities().size() == 1, "dim-radius: picking the circle doesn't create yet");
        ed.onPointerPress({45, 10}, Qt::LeftButton, Qt::NoModifier); // place text
        check(d.entities().size() == 2 && d.entities().back().type == EntityType::Dimension &&
                  d.entities().back().meta.value("dimType").toString() == QLatin1String("radius") &&
                  near(d.entities().back().radius, 30.0),
              "dim-radius: created from the picked circle");
    }

    // --- Dimension selectable by touching its dimension line; text height editable.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(3);
        ed.setDimMode(Editor::DimMode::Linear);
        ed.setTool(ToolId::Dimension);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({40, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({40, 20}, Qt::LeftButton, Qt::NoModifier); // dim line at y=20
        const QString id = d.entities()[0].id;
        check(near(d.entities()[0].meta.value("textHeight").toDouble(), 5.0),
              "dim-props: default text height is 5");
        ed.setTool(ToolId::Select);
        ed.onPointerPress({20, 20}, Qt::LeftButton, Qt::NoModifier); // on the dimension line
        ed.onPointerRelease({20, 20}, Qt::LeftButton);
        check(ed.selection().size() == 1 && ed.selection().front() == id,
              "dim-select: clicking the dimension line selects it");
        ed.setSelectionDimMeta(QStringLiteral("textHeight"), 8.0);
        check(near(d.entities()[0].meta.value("textHeight").toDouble(), 8.0),
              "dim-props: text height edits via setSelectionDimMeta");

        // A green crossing zone touching the dimension line selects it.
        auto regionSelect = [&](Vec2 c1, Vec2 c2) {
            ed.clearSelection();
            ed.onPointerPress(c1, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerRelease(c1, Qt::LeftButton);
            ed.onPointerMove(c2);
            ed.onPointerPress(c2, Qt::LeftButton, Qt::NoModifier);
        };
        regionSelect({50, 25}, {10, 15}); // crossing across the dim line at y=20
        check(ed.selection().size() == 1, "dim-region: green zone touching the dim line selects");
        regionSelect({60, 60}, {52, 52}); // far from every line
        check(ed.selection().empty(), "dim-region: a zone touching no line does not select");
    }

    // --- Repeat last tool (Space / Enter when idle).
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setTool(ToolId::Circle);
        ed.setTool(ToolId::Select);                 // e.g. after an operation returns to Select
        ed.onKey(Qt::Key_Space, Qt::NoModifier);    // repeat last
        check(ed.tool() == ToolId::Circle, "repeat: Space re-invokes last tool");
    }

    // --- Hatch Pick Points: one click inside an enclosed area fills it; pattern/
    //     angle/scale/transparency are stored in meta.
    {
        Document d; d.entities().clear();
        Entity r; r.id = "r"; r.type = EntityType::Rect;
        r.points = {{0, 0}, {50, 0}, {50, 30}, {0, 30}};
        d.entities().push_back(r);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.hatchStyle().pattern = QStringLiteral("ansi31");
        ed.hatchStyle().angle = 30.0;
        ed.hatchStyle().scale = 2.0;
        ed.hatchStyle().transparency = 25;
        ed.hatchStyle().boundary = Editor::HatchBoundary::PickPoints;
        ed.setTool(ToolId::Hatch);
        ed.onPointerPress({25, 15}, Qt::LeftButton, Qt::NoModifier); // one click INSIDE the rect
        check(d.entities().size() == 2, "hatch-pick: one click fills the enclosing rect");
        const Entity& e = d.entities().back();
        check(e.type == EntityType::Hatch && e.points.size() == 4 &&
                  e.meta.value("pattern").toString() == QLatin1String("ansi31") &&
                  near(e.meta.value("hatchAngle").toDouble(), 30.0) &&
                  near(e.meta.value("hatchScale").toDouble(), 2.0) &&
                  e.meta.value("transparency").toInt() == 25,
              "hatch-pick: boundary = rect + pattern/angle/scale/transparency stored");
        check(ed.tool() == ToolId::Hatch, "hatch-pick: stays in the tool for more regions");
        // A click in empty space (no enclosing boundary) adds nothing.
        ed.onPointerPress({500, 500}, Qt::LeftButton, Qt::NoModifier);
        check(d.entities().size() == 2, "hatch-pick: click outside any boundary fills nothing");
    }

    // --- Hatch Pick Points picks the SMALLEST enclosing region (innermost island).
    {
        Document d; d.entities().clear();
        Entity big; big.id = "big"; big.type = EntityType::Rect;
        big.points = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
        Entity small; small.id = "small"; small.type = EntityType::Rect;
        small.points = {{40, 40}, {60, 40}, {60, 60}, {40, 60}};
        d.entities().push_back(big);
        d.entities().push_back(small);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.hatchStyle().boundary = Editor::HatchBoundary::PickPoints;
        ed.setTool(ToolId::Hatch);
        ed.onPointerPress({50, 50}, Qt::LeftButton, Qt::NoModifier); // inside both rects
        const Entity& h = d.entities().back();
        check(h.type == EntityType::Hatch && near(h.bounds().width(), 20.0),
              "hatch-island: fills the smallest enclosing region");
    }

    // --- Hatch: "select object" mode fills a clicked closed entity.
    {
        Document d; d.entities().clear();
        Entity r; r.id = "r"; r.type = EntityType::Rect;
        r.points = {{0, 0}, {50, 0}, {50, 30}, {0, 30}};
        d.entities().push_back(r);
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(2);
        ed.hatchStyle().boundary = Editor::HatchBoundary::SelectObject;
        ed.hatchStyle().pattern = QStringLiteral("solid");
        ed.setTool(ToolId::Hatch);
        ed.onPointerPress({25, 0}, Qt::LeftButton, Qt::NoModifier); // click ON the rect's edge
        check(d.entities().size() == 2, "hatch-select: hatch added from the rect");
        const Entity& h = d.entities().back();
        check(h.type == EntityType::Hatch && h.points.size() == 4 &&
                  h.meta.value("pattern").toString() == QLatin1String("solid"),
              "hatch-select: boundary taken from the closed object");
    }

    // --- Hatch pick-points fills only the SUB-CELL when an object crosses a closed
    //     shape (a line cutting a rectangle), not the whole rectangle.
    {
        Document d; d.entities().clear();
        Entity r; r.id = "r"; r.type = EntityType::Rect;
        r.points = {{0, 0}, {100, 0}, {100, 100}, {0, 100}}; // area 10000
        Entity cut; cut.id = "c"; cut.type = EntityType::Line; cut.a = {-10, 50}; cut.b = {110, 50};
        d.entities().push_back(r);
        d.entities().push_back(cut);
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(2);
        ed.hatchStyle().boundary = Editor::HatchBoundary::PickPoints;
        ed.setTool(ToolId::Hatch);
        ed.onPointerPress({50, 25}, Qt::LeftButton, Qt::NoModifier); // lower half
        const Entity& h = d.entities().back();
        double area = 0.0;
        for (size_t i = 0, n = h.points.size(); i < n; ++i) {
            const Vec2& a = h.points[i];
            const Vec2& b = h.points[(i + 1) % n];
            area += a.x * b.y - b.x * a.y;
        }
        area = std::abs(area) * 0.5;
        check(h.type == EntityType::Hatch && near(area, 5000.0, 1.0),
              "hatch-cell: fills only the half cut off by the crossing line, not the whole rect");
    }

    // --- Wipeout: AutoCAD-style polygon draw (click vertices, Enter to finish);
    //     the filled region is interior-pickable.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(2);
        ed.setTool(ToolId::Wipeout);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({30, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({30, 30}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({0, 30}, Qt::LeftButton, Qt::NoModifier);
        ed.onKey(Qt::Key_Return, Qt::NoModifier); // finish the masking polygon
        check(d.entities().size() == 1 && d.entities().back().type == EntityType::Wipeout &&
                  d.entities().back().points.size() == 4,
              "wipeout: polygon draw creates a 4-point mask");
        ed.setTool(ToolId::Select);
        ed.onPointerPress({16, 16}, Qt::LeftButton, Qt::NoModifier); // inside the mask
        ed.onPointerRelease({16, 16}, Qt::LeftButton);
        check(ed.selection().size() == 1, "wipeout: interior click selects the filled region");
    }

    // --- Right-button drag: move and copy.
    {
        // Move the selection by the drag delta.
        {
            Document d; d.entities().clear();
            Entity l; l.id = "l"; l.type = EntityType::Line; l.a = {0, 0}; l.b = {10, 0};
            d.entities().push_back(l);
            Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(2);
            ed.setTool(ToolId::Select);
            ed.selectAll();
            check(ed.beginRightDrag({0, 0}), "rdrag: begins on a selection");
            check(ed.isRightDragging(), "rdrag: dragging flag set");
            ed.updateRightDrag({0, 5});
            ed.commitRightDrag(false); // Move Here
            check(near(d.entities()[0].a.y, 5.0) && near(d.entities()[0].b.y, 5.0) &&
                      d.entities().size() == 1,
                  "rdrag: Move translates the originals");
        }
        // Copy leaves the original and adds a translated duplicate.
        {
            Document d; d.entities().clear();
            Entity l; l.id = "l"; l.type = EntityType::Line; l.a = {0, 0}; l.b = {10, 0};
            d.entities().push_back(l);
            Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(2);
            ed.setTool(ToolId::Select);
            ed.selectAll();
            ed.beginRightDrag({5, 0});
            ed.updateRightDrag({5, 20});
            ed.commitRightDrag(true); // Copy Here
            check(d.entities().size() == 2, "rdrag: Copy adds a duplicate");
            check(near(d.entities()[0].a.y, 0.0), "rdrag: Copy keeps the original");
            check(near(d.entities()[1].a.y, 20.0) && near(d.entities()[1].b.y, 20.0),
                  "rdrag: Copy is translated by the delta");
        }
        // Mirror during a right-drag: X axis (horizontal line through the catch
        // point) flips Y; reference is the catch location.
        {
            Document d; d.entities().clear();
            Entity l; l.id = "l"; l.type = EntityType::Line; l.a = {0, 10}; l.b = {20, 30};
            d.entities().push_back(l);
            Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(2);
            ed.setTool(ToolId::Select);
            ed.selectAll();
            ed.beginRightDrag({0, 0});       // catch at the origin
            ed.rightDragMirror(true);        // mirror across the X axis (flip Y about y=0)
            ed.updateRightDrag({0, 0});      // no translation
            ed.commitRightDrag(false);
            check(near(d.entities()[0].a.y, -10.0) && near(d.entities()[0].b.y, -30.0) &&
                      near(d.entities()[0].a.x, 0.0) && near(d.entities()[0].b.x, 20.0),
                  "rdrag-mirror: X flips Y about the catch point");
        }
        // Mirror text moves only its position, not its glyph orientation.
        {
            Document d; d.entities().clear();
            Entity t; t.id = "t"; t.type = EntityType::Text; t.a = {10, 5};
            t.meta.insert("text", QStringLiteral("Hi"));
            t.rotation = 0.0;
            d.entities().push_back(t);
            Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(2);
            ed.setTool(ToolId::Select);
            ed.selectAll();
            ed.beginRightDrag({0, 0});
            ed.rightDragMirror(false); // Y axis -> flip X about x=0
            ed.updateRightDrag({0, 0});
            ed.commitRightDrag(false);
            check(near(d.entities()[0].a.x, -10.0) && near(d.entities()[0].a.y, 5.0) &&
                      near(d.entities()[0].rotation, 0.0),
                  "rdrag-mirror: text reflects position only (rotation unchanged)");
        }
        // With nothing selected, the drag grabs the object under the start point.
        {
            Document d; d.entities().clear();
            Entity l; l.id = "l"; l.type = EntityType::Line; l.a = {0, 0}; l.b = {10, 0};
            d.entities().push_back(l);
            Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(2);
            ed.setTool(ToolId::Select);
            check(ed.beginRightDrag({5, 0}), "rdrag: grabs the object under the cursor");
            check(ed.selection().size() == 1, "rdrag: selects the grabbed object");
            ed.cancelRightDrag();
            check(!ed.isRightDragging(), "rdrag: cancel clears the drag");
            check(near(d.entities()[0].a.x, 0.0), "rdrag: cancel leaves geometry unchanged");
        }
    }

    // --- Editing a selected hatch's pattern in place (Hatch Editor tab).
    {
        Document d; d.entities().clear();
        Entity h; h.id = "h"; h.type = EntityType::Hatch;
        h.points = {{0, 0}, {40, 0}, {40, 40}, {0, 40}};
        h.meta.insert("pattern", QStringLiteral("ansi31"));
        d.entities().push_back(h);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.selectById("h");
        ed.setSelectionHatchMeta(QStringLiteral("pattern"), QStringLiteral("grid"));
        check(d.entities()[0].meta.value("pattern").toString() == QLatin1String("grid"),
              "hatch-edit: setSelectionHatchMeta changes the selected hatch's pattern");
        ed.setSelectionHatchMeta(QStringLiteral("hatchAngle"), 45.0);
        check(near(d.entities()[0].meta.value("hatchAngle").toDouble(), 45.0),
              "hatch-edit: hatch angle updates");
    }

    // --- Associative hatch has a single centre grip (moves whole hatch); a
    //     non-associative hatch has boundary vertex grips (reshape).
    {
        // Associative: dragging the centre grip translates every point.
        Document d; d.entities().clear();
        Entity h; h.id = "h"; h.type = EntityType::Hatch;
        h.points = {{0, 0}, {40, 0}, {40, 40}, {0, 40}};
        h.meta.insert("pattern", QStringLiteral("ansi31"));
        h.meta.insert("associative", true);
        d.entities().push_back(h);
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(2);
        ed.selectById("h");
        ed.onPointerPress({20, 20}, Qt::LeftButton, Qt::NoModifier); // centre grip
        ed.onPointerMove({30, 30});
        ed.onPointerRelease({30, 30}, Qt::LeftButton);
        check(near(d.entities()[0].points[0].x, 10.0) && near(d.entities()[0].points[0].y, 10.0) &&
                  near(d.entities()[0].points[2].x, 50.0),
              "hatch-assoc: centre grip moves the whole associative hatch");
        // Dragging from a boundary corner moves the WHOLE hatch (no per-corner grip):
        // the rectangle stays congruent (width 40) rather than one corner stretching.
        ed.onPointerPress({10, 10}, Qt::LeftButton, Qt::NoModifier); // corner, now at (10,10)
        ed.onPointerMove({0, 0});
        ed.onPointerRelease({0, 0}, Qt::LeftButton);
        check(near(d.entities()[0].points[1].x - d.entities()[0].points[0].x, 40.0),
              "hatch-assoc: no per-corner grip (boundary stays congruent)");
    }
    {
        // Non-associative: dragging a corner reshapes the boundary.
        Document d; d.entities().clear();
        Entity h; h.id = "h"; h.type = EntityType::Hatch;
        h.points = {{0, 0}, {40, 0}, {40, 40}, {0, 40}};
        h.meta.insert("associative", false);
        d.entities().push_back(h);
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(2);
        ed.selectById("h");
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier); // corner vertex grip
        ed.onPointerMove({-5, -5});
        ed.onPointerRelease({-5, -5}, Qt::LeftButton);
        check(near(d.entities()[0].points[0].x, -5.0) && near(d.entities()[0].points[0].y, -5.0) &&
                  near(d.entities()[0].points[1].x, 40.0),
              "hatch-nonassoc: corner grip reshapes the boundary");
    }

    // --- "Multiple joints" toggle: select-object on a circle yields a dense (64)
    //     or coarse (4) boundary.
    {
        auto fillCircle = [](bool many) -> Entity {
            Document d; d.entities().clear();
            Entity c; c.id = "c"; c.type = EntityType::Circle; c.center = {0, 0}; c.radius = 50;
            d.entities().push_back(c);
            Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(2);
            ed.hatchStyle().boundary = Editor::HatchBoundary::SelectObject;
            ed.hatchStyle().manyJoints = many;
            ed.setTool(ToolId::Hatch);
            ed.onPointerPress({50, 0}, Qt::LeftButton, Qt::NoModifier); // on the ring
            return d.entities().back();
        };
        const Entity few = fillCircle(false);
        check(few.meta.value("shape").toString() == QLatin1String("circle") &&
                  near(few.radius, 50.0),
              "hatch-joints: off -> circle boundary stays a true circle (4 resize joints)");
        const Entity many = fillCircle(true);
        check(many.points.size() == 64,
              "hatch-joints: on -> circle boundary is a dense polygon");
    }

    // --- A few-joints circle hatch fills the circle and resizes via a radius grip.
    {
        Document d; d.entities().clear();
        Entity c; c.id = "c"; c.type = EntityType::Circle; c.center = {0, 0}; c.radius = 50;
        d.entities().push_back(c);
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(3);
        ed.hatchStyle().boundary = Editor::HatchBoundary::SelectObject;
        ed.hatchStyle().manyJoints = false;
        ed.hatchStyle().associative = false; // so the 4 radius grips are shown
        ed.setTool(ToolId::Hatch);
        ed.onPointerPress({50, 0}, Qt::LeftButton, Qt::NoModifier);
        const QString id = d.entities().back().id;
        ed.setTool(ToolId::Select);
        ed.selectById(id);
        ed.onPointerPress({50, 0}, Qt::LeftButton, Qt::NoModifier); // +x radius grip
        ed.onPointerMove({80, 0});
        ed.onPointerRelease({80, 0}, Qt::LeftButton);
        check(near(d.entities().back().radius, 80.0),
              "hatch-circle: dragging a radius joint enlarges the circle uniformly");
    }

    // --- Object snap finds a circle's quadrant joints (not just the ring).
    {
        Document d; d.entities().clear();
        Entity c; c.id = "c"; c.type = EntityType::Circle; c.center = {0, 0}; c.radius = 50;
        d.entities().push_back(c);
        Editor ed; ed.setDocument(&d);
        auto& s = ed.snapSettings();
        s.objectSnap = true; s.snapEndpoint = true; s.snapMidpoint = false;
        s.gridSnap = false; s.ortho = false; s.polar = false;
        ed.setTool(ToolId::Line);
        ed.onPointerMove({48, 2}); // near the +x quadrant joint (50,0)
        check(ed.currentSnap().snapped && near(ed.currentSnap().point.x, 50.0) &&
                  near(ed.currentSnap().point.y, 0.0),
              "circle-snap: snaps to the +x quadrant joint");
        ed.onPointerMove({2, -48}); // near the -y quadrant joint (0,-50)
        check(ed.currentSnap().snapped && near(ed.currentSnap().point.x, 0.0) &&
                  near(ed.currentSnap().point.y, -50.0),
              "circle-snap: snaps to the -y quadrant joint");
    }

    // --- While in Hatch mode you can still select other objects (click + zone).
    {
        Document d; d.entities().clear();
        Entity l; l.id = "l"; l.type = EntityType::Line; l.a = {0, 0}; l.b = {20, 0};
        d.entities().push_back(l);
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(2);
        ed.hatchStyle().boundary = Editor::HatchBoundary::SelectObject;
        ed.setTool(ToolId::Hatch);
        // Clicking a non-closed object (a line) selects it instead of hatching.
        ed.onPointerPress({10, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerRelease({10, 0}, Qt::LeftButton);
        check(ed.selection().size() == 1 && ed.selection().front() == "l",
              "hatch-select: clicking a line in hatch mode selects it");
        check(d.entities().size() == 1, "hatch-select: no hatch was created from a line");
        // A crossing zone in hatch mode selects too.
        ed.clearSelection();
        ed.onPointerPress({40, 20}, Qt::LeftButton, Qt::NoModifier); // empty -> region
        ed.onPointerRelease({40, 20}, Qt::LeftButton);               // -> RectPending
        ed.onPointerMove({-10, -10});
        ed.onPointerPress({-10, -10}, Qt::LeftButton, Qt::NoModifier); // commit (crossing)
        check(ed.selection().size() == 1, "hatch-select: select-zone works in hatch mode");
    }

    // --- Routing on to something: the connection wins over the rules that would move it, and a run
    //     landed on in the middle of another is a JOINT.
    {
        Document d;
        Entity run;
        run.id = QStringLiteral("w1");
        run.type = EntityType::Wire;
        run.points = {{0, 0}, {200, 0}};
        d.entities().push_back(run);
        Editor ed;
        ed.setDocument(&d);
        ed.wireClearanceOn() = true;
        ed.wireGapRule() = QStringLiteral("custom");
        ed.wireGapCustom() = 10.0;
        ed.setTool(ToolId::Wire);
        ed.wireOrtho() = true;                       // "90° only" on: the case that used to miss
        ed.onPointerPress({60, 60}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({120, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onDoubleClick({120, 0});

        const Entity* made = nullptr;
        for (const auto& e : d.entities())
            if (e.type == EntityType::Wire && e.id != QLatin1String("w1")) made = &e;
        check(made != nullptr, "routing: the wire was drawn");
        if (made) {
            check(made->points.size() == 3, "routing: a knee is put in so the corners stay square");
            bool square = true;
            for (size_t i = 1; i < made->points.size(); ++i) {
                const Vec2 a = made->points[i - 1], b = made->points[i];
                if (std::abs(a.x - b.x) > 1e-6 && std::abs(a.y - b.y) > 1e-6) square = false;
            }
            check(square, "routing: every leg is horizontal or vertical");
            check(std::abs(made->points.back().x - 120.0) < 1e-6 &&
                      std::abs(made->points.back().y) < 1e-6,
                  "routing: it lands EXACTLY on the run it was aimed at, unmoved by ortho or "
                  "clearance");
        }
        int joints = 0;
        for (const auto& e : d.entities())
            if (e.type == EntityType::Splice && std::abs(e.a.x - 120.0) < 1e-6 &&
                std::abs(e.a.y) < 1e-6)
                ++joints;
        check(joints == 1, "routing: a T-junction is jointed where it meets the run");

        // And the clearance rule still does its job on a run that is merely NEAR a neighbour.
        const Vec2 pushed = ed.applyWireClearance({0, 3}, {80, 3});
        check(near(pushed.y, 10.0), "routing: a run drawn too close is still pushed to the clearance");
    }

    // --- A screen's drain leg follows the side it is told to leave from.
    {
        Document d;
        Entity sc;
        sc.id = QStringLiteral("s1");
        sc.type = EntityType::Screen;
        sc.points = {{100, 0}, {100, 30}, {100, -40}}; // two crossings, then the drain end below
        sc.meta.insert("catch", 2);
        sc.meta.insert("rx", 8.0);
        sc.meta.insert("ry", 22.0);
        sc.meta.insert("rot", 1.5707963267948966);
        sc.meta.insert("legDir", QStringLiteral("bottom"));
        d.entities().push_back(sc);
        Editor ed;
        ed.setDocument(&d);
        ed.selectById(QStringLiteral("s1"));

        const Vec2 was = d.entities().front().points.back();
        ed.setSelectionScreenLegDir(QStringLiteral("top"));
        const Vec2 now = d.entities().front().points.back();
        check(now.y > was.y, "leg side: set to Top, the drain end moves to the top");
        // The centre of the band is y = 15 and ry = 22, so either edge is 33 from the old tip.
        check(near(std::abs(now.y - 15.0), 55.0) && near(std::abs(was.y - 15.0), 55.0),
              "leg side: and it keeps the length it had");
        check(d.entities().front().meta.value("legDir").toString() == QLatin1String("top"),
              "leg side: the side is recorded too");
        ed.setSelectionScreenLegDir(QStringLiteral("bottom"));
        check(near(d.entities().front().points.back().y, was.y),
              "leg side: back to Bottom puts it where it started");
    }

    // --- The break mark: placed on a run, lying across it, and it remembers which way.
    {
        Document d;
        Entity run;
        run.id = QStringLiteral("r1");
        run.type = EntityType::Wire;
        run.points = {{0, 0}, {200, 100}};   // a slope, so the direction is not the default
        d.entities().push_back(run);
        Editor ed;
        ed.setDocument(&d);
        ed.setTool(ToolId::LengthBreak);
        ed.breakStyle() = QStringLiteral("curved");
        ed.breakSize() = 9.0;
        // Marked out by a LINE DRAWN ACROSS the run: two clicks, either side of it. The run goes up at
        // atan(100/200), so a line across it here runs the other way.
        ed.onPointerPress({112, 44}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({88, 56});
        ed.onPointerPress({88, 56}, Qt::LeftButton, Qt::NoModifier);

        const Entity* mark = nullptr;
        for (const auto& e : d.entities())
            if (e.type == EntityType::LengthBreak) mark = &e;
        check(mark != nullptr, "break: the mark is placed");
        if (mark) {
            check(mark->meta.value("breakStyle").toString() == QLatin1String("curved") &&
                      near(mark->meta.value("size").toDouble(), 9.0),
                  "break: it carries the style and size it was placed with");
            check(near(mark->rotation, std::atan2(100.0, 200.0)),
                  "break: it lies along the run it was placed on, not along the horizontal");
            const Entity back = Entity::fromJson(mark->toJson());
            check(back.type == EntityType::LengthBreak && near(back.rotation, mark->rotation) &&
                      back.meta.value("breakStyle").toString() == QLatin1String("curved"),
                  "break: saved and read back, it still lies the same way");
        }
        // Placed, the app is back in Select - a mark is placed on a run and then adjusted, so being
        // handed back the pointer is what is wanted rather than being left armed.
        check(ed.tool() == ToolId::Select, "break: placing one hands back the Select tool");
    }

    // --- A spline cutter is cut where it is DRAWN, not along the chords between its points.
    {
        Document d;
        Entity line;
        line.id = QStringLiteral("l1");
        line.type = EntityType::Line;
        line.a = {-100, 0};
        line.b = {100, 0};
        d.entities().push_back(line);
        // An S through four points. Its middle chord crosses y = 0 well away from where the curve
        // does, and leans the other way - which is what mis-tilted a trimmed double line's cut face.
        Entity s;
        s.id = QStringLiteral("s1");
        s.type = EntityType::Spline;
        s.points = {{20, -40}, {-10, -12}, {10, 12}, {-20, 40}};
        d.entities().push_back(s);

        Editor ed;
        ed.setDocument(&d);
        ed.selectById(QStringLiteral("s1"));
        ed.setTool(ToolId::Trim);
        ed.onPointerPress({60, -30}, Qt::LeftButton, Qt::NoModifier);   // fence, first point
        ed.onPointerPress({60, 30}, Qt::LeftButton, Qt::NoModifier);    // and its end
        const Entity* cut = nullptr;
        for (const auto& e : d.entities())
            if (e.id == QLatin1String("l1")) cut = &e;
        check(cut != nullptr, "spline cutter: the line is still there");
        if (cut) {
            // The drawn curve crosses y = 0 at x = 0 (the S is symmetric about the origin); the chord
            // between its two middle points crosses at x = 0 as well, but its NEIGHBOURS do not, and
            // the tessellated curve is what decides. Either way the piece kept must end ON the curve.
            const double endX = cut->b.x;
            check(std::abs(endX) < 6.0,
                  "spline cutter: the line is trimmed where the curve is actually drawn");
        }
    }

    // --- The break mark: sized to the run it is placed on, and easy to pick.
    {
        Document d;
        Entity bundle;
        bundle.id = QStringLiteral("bundle");
        bundle.type = EntityType::DoubleLine;
        bundle.a = {0, 0};
        bundle.b = {320, 0};
        bundle.meta.insert("gap", 40.0);      // a bundle 40 wide
        d.entities().push_back(bundle);
        Editor ed;
        ed.setDocument(&d);
        ed.setTool(ToolId::LengthBreak);
        ed.breakStyle() = QStringLiteral("straight");
        ed.breakSize() = 0.0;                 // "as wide as the run needs"
        ed.breakGap() = 0.0;
        ed.breakAngle() = -1.0;
        // A line straight across the bundle, from outside one rail to outside the other.
        ed.onPointerPress({100, 30}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({100, -30});
        ed.onPointerPress({100, -30}, Qt::LeftButton, Qt::NoModifier);

        const Entity* mark = nullptr;
        for (const auto& e : d.entities())
            if (e.type == EntityType::LengthBreak) mark = &e;
        check(mark != nullptr, "break: placed on the bundle");
        if (mark) {
            // It has to CROSS the run to read as a break: half the bundle's width plus a margin.
            check(mark->meta.value("size").toDouble() > 20.0,
                  "break: sized to the bundle it was placed on when no size is asked for");
            check(mark->meta.value("size").toDouble() * 2.0 > 40.0,
                  "break: and it reaches beyond both rails");
            check(mark->meta.value("mask").toBool(true),
                  "break: it interrupts the run between its strokes");
        }
        // A click ON A STROKE picks it. The strokes are where anyone would click; the empty middle
        // belongs to the run it interrupts.
        const QString markId = mark ? mark->id : QString();
        ed.setTool(ToolId::Select);
        ed.clearSelection();
        const double half = mark ? mark->meta.value("size").toDouble(6.0) : 6.0;
        const double gap = mark ? mark->meta.value("gap").toDouble(half * 0.9) : 5.4;
        ed.onPointerPress({100.0 - gap / 2.0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerRelease({100.0 - gap / 2.0, 0}, Qt::LeftButton);
        bool picked = false;
        for (const QString& id : ed.selection()) picked = picked || (id == markId);
        check(picked, "break: a click on one of its strokes selects it");

        // Numbers given on the ribbon are used as given: this is the same mark, said outright.
        {
            Document d2;
            Entity b2 = bundle;
            d2.entities().push_back(b2);
            Editor e2;
            e2.setDocument(&d2);
            e2.setTool(ToolId::LengthBreak);
            e2.breakStyle() = QStringLiteral("straight");
            e2.breakSize() = 14.0;
            e2.breakGap() = 9.0;
            e2.breakAngle() = 40.0;
            e2.onPointerPress({100, 30}, Qt::LeftButton, Qt::NoModifier);
            e2.onPointerMove({100, -30});
            e2.onPointerPress({100, -30}, Qt::LeftButton, Qt::NoModifier);
            const Entity* m3 = nullptr;
            for (const auto& e : d2.entities())
                if (e.type == EntityType::LengthBreak) m3 = &e;
            check(m3 && near(m3->meta.value("size").toDouble(), 14.0) &&
                      near(m3->meta.value("gap").toDouble(), 9.0) &&
                      near(m3->meta.value("angle").toDouble(), 40.0),
                  "break: a size, a distance between the strokes and an angle are used as asked for");
            // 40 degrees of lean is a real lean: the stroke ends are offset along the run by
            // reach * tan(40) either way.
            const auto strokes = m3 ? breakStrokes(*m3) : std::vector<std::vector<Vec2>>{};
            check(strokes.size() == 2 && strokes[0].size() == 2 &&
                      std::abs(strokes[0].front().x - strokes[0].back().x) >
                          2.0 * 14.0 * std::tan(35.0 * 3.14159265358979323846 / 180.0),
                  "break: and the angle leans the strokes by it");
        }

        // And a selection zone - two clicks, both off the geometry - takes it.
        ed.clearSelection();
        const Vec2 c1{100.0 - half - 20.0, -half - 20.0};
        const Vec2 c2{100.0 + half + 20.0, half + 20.0};
        ed.onPointerMove(c1);
        ed.onPointerPress(c1, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerRelease(c1, Qt::LeftButton);
        ed.onPointerMove(c2);
        ed.onPointerPress(c2, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerRelease(c2, Qt::LeftButton);
        bool zoned = false;
        for (const QString& id : ed.selection()) zoned = zoned || (id == markId);
        check(zoned, "break: a zone drawn round it takes it");

        // It is ATTACHED to the run it was placed on, and it rides it.
        const Entity* m2 = nullptr;
        for (const auto& e : d.entities())
            if (e.id == markId) m2 = &e;
        check(m2 && m2->meta.value("host").toString() == QLatin1String("bundle"),
              "break: it is attached to the run it was placed on");
        const double t0 = m2 ? m2->meta.value("annT").toDouble(-1.0) : -1.0;
        check(t0 > 0.2 && t0 < 0.4, "break: and it remembers how far along that run it sits");

        // Dragged well off the run, it slides ALONG it and stays on it.
        ed.clearSelection();
        ed.selectById(markId);
        const Vec2 grab{100, 0};
        ed.onPointerPress(grab, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({grab.x + 80, grab.y - 60});
        ed.onPointerRelease({grab.x + 80, grab.y - 60}, Qt::LeftButton);
        for (const auto& e : d.entities())
            if (e.id == markId) {
                check(e.meta.value("annT").toDouble() > t0 + 0.1,
                      "break: dragging it slides it along the run");
                check(near(e.a.y, 0.0),
                      "break: and never off it - what the cursor did across the run is ignored");
            }

        // What the mark HIDES is not there to be picked: the drawing does not show that stretch, so a
        // click in it belongs to the mark. Where the run IS drawn, a click still takes the run.
        {
            const Entity* m = nullptr;
            for (const auto& e : d.entities())
                if (e.id == markId) m = &e;
            const double railY = 20.0;    // the bundle's gap is 40, so its rails are at +/-20
            const double at = m ? m->a.x : 100.0;
            ed.setTool(ToolId::Select);
            ed.clearSelection();
            ed.onPointerPress({at, railY}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerRelease({at, railY}, Qt::LeftButton);
            bool tookMark = false, tookRun = false;
            for (const QString& id : ed.selection()) {
                tookMark = tookMark || id == markId;
                tookRun = tookRun || id == QLatin1String("bundle");
            }
            check(tookMark && !tookRun,
                  "break: a click where the run is hidden takes the mark, not the run");
            ed.clearSelection();
            ed.onPointerPress({20, railY}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerRelease({20, railY}, Qt::LeftButton);
            bool runBack = false;
            for (const QString& id : ed.selection()) runBack = runBack || id == QLatin1String("bundle");
            check(runBack, "break: and where the run is drawn, the run is still picked");
        }

        // A line drawn across SEVERAL runs breaks all of them, and what it covers on each of them is
        // out of reach - not only on the one the mark is attached to.
        {
            Document d3;
            for (int k = 0; k < 3; ++k) {
                Entity w;
                w.id = QStringLiteral("w%1").arg(k + 1);
                w.type = EntityType::Wire;
                w.points = {{0, -k * 30.0}, {300, -k * 30.0}};
                d3.entities().push_back(w);
            }
            Editor e3;
            e3.setDocument(&d3);
            e3.setTool(ToolId::LengthBreak);
            e3.breakSize() = 0.0;
            e3.breakGap() = 0.0;
            e3.breakAngle() = -1.0;
            e3.onPointerMove({150, 20});
            e3.onPointerPress({150, 20}, Qt::LeftButton, Qt::NoModifier);
            e3.onPointerMove({150, -80});
            e3.onPointerPress({150, -80}, Qt::LeftButton, Qt::NoModifier);
            const Entity* m = nullptr;
            for (const auto& e : d3.entities())
                if (e.type == EntityType::LengthBreak) m = &e;
            check(m != nullptr, "band: a line across three wires makes a mark");
            if (m) {
                check(m->meta.value("size").toDouble() * 2.0 > 60.0,
                      "band: sized to span every wire the line crossed");
                // The middle wire is the one it is attached to; the outer two are covered all the same.
                e3.setTool(ToolId::Select);
                for (const QString& id : {QStringLiteral("w1"), QStringLiteral("w2"),
                                          QStringLiteral("w3")}) {
                    double y = 0.0;
                    for (const auto& e : d3.entities())
                        if (e.id == id) y = e.points.front().y;
                    e3.clearSelection();
                    e3.onPointerPress({m->a.x, y}, Qt::LeftButton, Qt::NoModifier);
                    e3.onPointerRelease({m->a.x, y}, Qt::LeftButton);
                    bool tookRun = false;
                    for (const QString& s : e3.selection()) tookRun = tookRun || s == id;
                    check(!tookRun, "band: the stretch it covers is out of reach on every wire it "
                                    "crosses, not just its own");
                }
            }
        }

        // A zone in the GAP between the rails, touching neither, takes nothing: a double line is
        // selected by the lines that are drawn, never by the space between them.
        {
            ed.setTool(ToolId::Select);
            ed.clearSelection();
            const Vec2 c1{240, -9}, c2{300, 9};   // inside the 40-wide gap, clear of both rails
            ed.onPointerMove(c1);
            ed.onPointerPress(c1, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerRelease(c1, Qt::LeftButton);
            ed.onPointerMove(c2);
            ed.onPointerPress(c2, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerRelease(c2, Qt::LeftButton);
            check(ed.selection().empty(),
                  "double line: a zone in the gap between its lines selects nothing");
        }

        // The joints at its cut ends are SELECTION feedback: an unselected mark draws none, so a
        // finished drawing carries only the interruption and the strokes.
        {
            auto amberDots = [&d, &ed](const Vec2& viewOffset) {
                ViewTransform vt;
                vt.view.scale = 1.0;
                vt.view.x = viewOffset.x;
                vt.view.y = viewOffset.y;
                QImage img(420, 260, QImage::Format_RGB32);
                img.fill(QColor("#1e2832"));
                QPainter p(&img);
                for (const auto& e : d.entities()) EntityRenderer::draw(p, e, d, vt);
                ed.drawOverlay(p, vt);
                p.end();
                int lit = 0;
                for (int y = 0; y < img.height(); ++y)
                    for (int x = 0; x < img.width(); ++x) {
                        const QColor c = img.pixelColor(x, y);
                        if (c.red() > 180 && c.green() > 120 && c.green() < 230 && c.blue() < 140)
                            ++lit;
                    }
                return lit;
            };
            ed.clearSelection();
            const int none = amberDots({40.0, 130.0});
            ed.selectById(markId);
            const int some = amberDots({40.0, 130.0});
            check(none == 0, "break: an unselected mark draws no joints - the drawing stays clean");
            check(some > 0, "break: a selected one shows where it cuts the run");
        }

        // Its properties can be set one at a time, the way the panel sets them.
        {
            ed.clearSelection();
            ed.selectById(markId);
            ed.setSelectionMeta(QStringLiteral("breakStyle"), QStringLiteral("curved"));
            ed.setSelectionMetaNumber(QStringLiteral("gap"), 12.0);
            ed.setSelectionMetaNumber(QStringLiteral("size"), 18.0);
            ed.setSelectionMetaNumber(QStringLiteral("angle"), 25.0);
            ed.setSelectionMetaBool(QStringLiteral("mask"), false);
            const Entity* m = nullptr;
            for (const auto& e : d.entities())
                if (e.id == markId) m = &e;
            check(m && m->meta.value("breakStyle").toString() == QLatin1String("curved") &&
                      near(m->meta.value("gap").toDouble(), 12.0) &&
                      near(m->meta.value("size").toDouble(), 18.0) &&
                      near(m->meta.value("angle").toDouble(), 25.0) &&
                      m->meta.value("mask").toBool(true) == false,
                  "break: every one of its properties can be set on the placed mark");
            // The shape follows what was set: 25 degrees of lean on an 18 reach.
            const auto strokes = m ? breakStrokes(*m) : std::vector<std::vector<Vec2>>{};
            check(strokes.size() == 2, "break: and it still has its two strokes afterwards");
            // Put it back, so what follows sees the mark it expects.
            ed.setSelectionMeta(QStringLiteral("breakStyle"), QStringLiteral("straight"));
            ed.setSelectionMetaBool(QStringLiteral("mask"), true);
        }

        // The run moving carries the mark: it marks a place on the run, not a coordinate.
        for (auto& e : d.entities())
            if (e.id == QLatin1String("bundle")) { e.a.y += 50.0; e.b.y += 50.0; }
        for (const auto& e : d.entities()) {
            if (e.id != markId) continue;
            const Entity* h = nullptr;
            for (const auto& x : d.entities())
                if (x.id == QLatin1String("bundle")) h = &x;
            Vec2 at, dir;
            check(h && runPointAtFraction(*h, e.meta.value("annT").toDouble(), at, dir) &&
                      near(at.y, 50.0),
                  "break: the run moved and the mark is drawn on its new place");
        }
    }

    // --- A twisted pair: two wires, straight at the ends and woven in between, all one length.
    {
        Document d;
        Editor ed;
        ed.setDocument(&d);
        ed.setTool(ToolId::TwistedPair);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({120, 0});
        ed.onPointerPress({120, 0}, Qt::LeftButton, Qt::NoModifier);

        const Entity* pair = nullptr;
        for (const auto& e : d.entities())
            if (e.type == EntityType::TwistedPair) pair = &e;
        check(pair != nullptr, "twisted pair: two clicks draw one");
        if (pair) {
            const double half = kTwistedPairWidth / 2.0;
            const double part = kTwistedPairWidth * kTwistedPairPart;
            const auto lines = twistedPairStrands(*pair);
            check(lines.size() == 4,
                  "twisted pair: it is two wires - four lines, one wire's two edges then the other's");

            // THE POINT of drawing it this way: every line runs the whole length, so the straight ends
            // and the weave are the same lines and there is nothing to join up at the change-over.
            bool wholeLength = true;
            double maxAbsY = 0.0;
            for (const auto& line : lines) {
                if (!near(line.front().x, 0.0) || !near(line.back().x, 120.0)) wholeLength = false;
                for (const Vec2& v : line) {
                    maxAbsY = std::max(maxAbsY, std::abs(v.y));
                    if (v.x < -1e-6 || v.x > 120.0 + 1e-6) wholeLength = false;
                }
            }
            check(wholeLength,
                  "twisted pair: each line runs end to end, so the straight ends are joined to the "
                  "weave rather than butted against it");
            check(near(maxAbsY, half), "twisted pair: and it is as wide as the cable, no wider");

            // Where a line is at a given distance along, so the ends and the crossings can be read.
            auto atX = [](const std::vector<Vec2>& s, double x) {
                double y = 0.0, bestD = 1e18;
                for (const Vec2& v : s)
                    if (std::abs(v.x - x) < bestD) { bestD = std::abs(v.x - x); y = v.y; }
                return y;
            };
            // The straight ends: the four lines sit at the cable's outside and either side of the
            // parting, and they are LEVEL over the whole lead.
            std::vector<double> ends;
            bool straightLead = true;
            for (const auto& line : lines) {
                ends.push_back(line.front().y);
                if (!near(atX(line, kTwistedPairLead), line.front().y)) straightLead = false;
                if (!near(atX(line, 120.0 - kTwistedPairLead), line.back().y)) straightLead = false;
            }
            std::sort(ends.begin(), ends.end());
            check(near(ends[0], -half) && near(ends[1], -part) && near(ends[2], part) &&
                      near(ends[3], half),
                  "twisted pair: the ends are the two wires side by side - the cable's own outside and "
                  "the parting between them");
            check(straightLead, "twisted pair: and they run straight over the whole of each end");

            // The weave: the wire that starts on top is underneath somewhere in the middle.
            const std::vector<Vec2>* top = nullptr;
            for (const auto& line : lines)
                if (near(line.front().y, half)) top = &line;
            // Half a crossing along, the wire that started on top has changed places with the other:
            // its top edge is where the other wire's top edge was, on the far side of the parting.
            bool swapped = false;
            for (double x = kTwistedPairLead; x < 120.0 - kTwistedPairLead; x += 0.5)
                if (top && atX(*top, x) <= -part + 1e-6) swapped = true;
            check(swapped, "twisted pair: the two wires change places along it - it is twisted");

            const Bounds b = pair->bounds();
            check(near(b.min.y, -half) && near(b.max.y, half),
                  "twisted pair: it reaches half its width either side of the line it was drawn on");
            const Entity back = Entity::fromJson(pair->toJson());
            check(back.type == EntityType::TwistedPair && near(back.b.x, 120.0),
                  "twisted pair: saved and read back, it is the same pair");
        }
        // A pair drawn shorter than one crossing keeps its proportions rather than losing its weave.
        {
            Entity tiny;
            tiny.type = EntityType::TwistedPair;
            tiny.a = {0, 0};
            tiny.b = {8, 0};
            const auto lines = twistedPairStrands(tiny);
            check(lines.size() == 4 && lines[0].size() > 8, "twisted pair: a short one still weaves");
            double maxX = 0.0;
            for (const auto& line : lines)
                for (const Vec2& v : line) maxX = std::max(maxX, v.x);
            check(maxX < 8.0 + 1e-6, "twisted pair: and stays inside the length it was drawn");
        }
    }

    // --- A twisted pair ROUTED through knees, like a polyline.
    {
        Document d;
        Editor ed;
        ed.setDocument(&d);
        ed.setTool(ToolId::TwistedRoute);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({100, 0});
        ed.onPointerPress({100, 0}, Qt::LeftButton, Qt::NoModifier);   // a knee
        ed.onPointerMove({100, 80});
        ed.onPointerPress({100, 80}, Qt::LeftButton, Qt::NoModifier);
        ed.onKey(Qt::Key_Return, Qt::NoModifier);                      // Enter finishes the run

        const Entity* pair = nullptr;
        for (const auto& e : d.entities())
            if (e.type == EntityType::TwistedPair) pair = &e;
        check(pair != nullptr, "twisted route: the clicks draw one pair");
        if (pair) {
            check(pair->points.size() == 3,
                  "twisted route: it is ONE pair with a knee in it, not a pair per leg");
            const auto lines = twistedPairStrands(*pair);
            check(lines.size() == 4, "twisted route: still two wires");
            // It follows the route: the far end runs up the second leg, and every line stays within
            // half a width of the path it was drawn along - round the knee included.
            bool endsUpTheLeg = true, followsPath = true, kinks = false;
            const double half = kTwistedPairWidth / 2.0;
            // The knee is a BEND of the cable's own size, not a fold, so the lines cut the corner by
            // about that much - and must never leave the route by more than the cable and its bend.
            const double bend = kTwistedPairWidth * 1.2;
            for (const auto& line : lines) {
                if (!near(line.back().y, 80.0)) endsUpTheLeg = false;
                for (const Vec2& v : line) {
                    const double dLeg1 = std::abs(v.y);             // the leg along y = 0
                    const double dLeg2 = std::abs(v.x - 100.0);     // the leg along x = 100
                    if (std::min(dLeg1, dLeg2) > half + bend) followsPath = false;
                }
                // Nothing doubles back on itself: a mitred corner folds the inside edge over, which
                // shows up as a step that turns nearly all the way round. (A shallow turn is just the
                // weave crossing the path while the path itself is bending.)
                for (size_t i = 2; i < line.size(); ++i) {
                    const Vec2 d0 = line[i - 1] - line[i - 2], d1 = line[i] - line[i - 1];
                    const double l0 = d0.length(), l1 = d1.length();
                    if (l0 > 1e-9 && l1 > 1e-9 && dot(d0, d1) / (l0 * l1) < -0.5) kinks = true;
                }
            }
            check(endsUpTheLeg, "twisted route: it finishes at the end of the last leg");
            check(followsPath,
                  "twisted route: and every line stays on the route, round the knee included");
            check(!kinks, "twisted route: the knee is a bend, not a fold in the wires");
            const Entity back = Entity::fromJson(pair->toJson());
            check(back.points.size() == 3,
                  "twisted route: saved and read back, it still has its knees");
        }
    }

    // --- A twisted HARNESS: every wire of a bundle laid up together, fanned out at the ends.
    {
        Document d;
        Editor ed;
        ed.setDocument(&d);
        ed.twistWires() = 6;
        ed.setTool(ToolId::TwistedHarness);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({200, 0});
        // Tab pins it where it is, so the ribbon can be set up without the cursor moving it.
        ed.freezeTwistedPreview();
        check(ed.twistedFrozen(), "twisted harness: Tab pins it while it is being drawn");
        ed.onPointerMove({500, 90});   // the cursor wanders off; the pinned end must not follow
        ed.onPointerPress({500, 90}, Qt::LeftButton, Qt::NoModifier);

        const Entity* bundle = nullptr;
        for (const auto& e : d.entities())
            if (e.type == EntityType::TwistedPair) bundle = &e;
        check(bundle != nullptr, "twisted harness: the second click places it");
        if (bundle) {
            check(near(bundle->b.x, 200.0) && near(bundle->b.y, 0.0),
                  "twisted harness: pinned, it is placed where it was pinned and not where the cursor "
                  "ended up");
            check(bundle->meta.value("twistWires").toInt() == 6,
                  "twisted harness: it carries the number of wires the ribbon was set to");
            const auto lines = twistedPairStrands(*bundle);
            check(lines.size() == 12, "twisted harness: six wires, two edges each");

            const double half = bundle->meta.value("twistWidth").toDouble() / 2.0;
            // Every line runs the whole length - the fan-out and the weave are the same lines - and
            // nothing is drawn outside the bundle.
            bool wholeLength = true;
            double maxAbsY = 0.0;
            for (const auto& line : lines) {
                if (!near(line.front().x, 0.0) || !near(line.back().x, 200.0)) wholeLength = false;
                for (const Vec2& v : line) maxAbsY = std::max(maxAbsY, std::abs(v.y));
            }
            check(wholeLength, "twisted harness: every wire runs end to end, fan-out included");
            check(maxAbsY < half + 1e-6, "twisted harness: and none of it is drawn wider than the bundle");

            // The ends are FANNED OUT: the wire middles are evenly spaced there, which is what gets
            // stripped and landed on pins.
            std::vector<double> mids;
            for (size_t k = 0; k + 1 < lines.size(); k += 2)
                mids.push_back((lines[k].front().y + lines[k + 1].front().y) / 2.0);
            std::sort(mids.begin(), mids.end());
            bool evenlySpaced = mids.size() == 6;
            for (size_t k = 2; k < mids.size(); ++k)
                if (std::abs((mids[k] - mids[k - 1]) - (mids[1] - mids[0])) > 1e-6) evenlySpaced = false;
            check(evenlySpaced, "twisted harness: the wires fan out evenly across the end");

            // And they are laid up: each wire is somewhere else across the bundle in the middle.
            auto atX = [](const std::vector<Vec2>& s, double x) {
                double y = 0.0, bestD = 1e18;
                for (const Vec2& v : s)
                    if (std::abs(v.x - x) < bestD) { bestD = std::abs(v.x - x); y = v.y; }
                return y;
            };
            int moved = 0;
            for (size_t k = 0; k + 1 < lines.size(); k += 2) {
                const double atEnd = (lines[k].front().y + lines[k + 1].front().y) / 2.0;
                double travel = 0.0;
                for (double x = 20.0; x < 180.0; x += 2.0)
                    travel = std::max(travel, std::abs(atX(lines[k], x) - atEnd));
                if (travel > half * 0.5) ++moved;
            }
            check(moved == 6, "twisted harness: every wire of it travels across the bundle - all of "
                              "them are twisted, not just the outer two");
        }
        // A pair is the two-wire case of the same drawing, not a different one.
        {
            Entity two;
            two.type = EntityType::TwistedPair;
            two.a = {0, 0};
            two.b = {120, 0};
            two.meta.insert("twistWires", 2);
            Entity noCount = two;
            noCount.meta.remove("twistWires");
            const auto a = twistedPairStrands(two);
            const auto b = twistedPairStrands(noCount);
            bool same = a.size() == b.size() && a.size() == 4;
            for (size_t i = 0; same && i < a.size(); ++i) {
                if (a[i].size() != b[i].size()) { same = false; break; }
                for (size_t j = 0; j < a[i].size(); ++j)
                    if (!near(a[i][j].x, b[i][j].x) || !near(a[i][j].y, b[i][j].y)) same = false;
            }
            check(same, "twisted harness: two wires draws exactly what a twisted pair always did");
        }
    }

    // --- The stock electronic parts: drawn at size, dropped by a click.
    {
        // Every one of them draws something, is built about the point it is dropped at, and says which
        // part it belongs to - so a drawing can be asked what is on it.
        for (const bool schematic : {false, true}) {
            const auto list = schematic ? schematicPartSymbols() : mechanicalPartSymbols();
            check(!list.empty(), "parts: the menu has entries");
            for (const PartSymbol s : list) {
                const auto ents = partSymbolEntities(s, Vec2{100.0, 50.0});
                const QString what = partSymbolName(s);
                check(!ents.empty(), qPrintable(QStringLiteral("parts: %1 draws something").arg(what)));
                check(partSymbolIsSchematic(s) == schematic,
                      qPrintable(QStringLiteral("parts: %1 knows which drawing it belongs on").arg(what)));
                Bounds b;
                bool stamped = true;
                for (const Entity& e : ents) {
                    b.expand(e.bounds());
                    if (e.meta.value(kPartSymbolKey).toString() != what) stamped = false;
                }
                check(stamped,
                      qPrintable(QStringLiteral("parts: every line of %1 says what it is").arg(what)));
                check(b.valid && std::abs((b.min.x + b.max.x) / 2.0 - 100.0) < 3.0 &&
                          std::abs((b.min.y + b.max.y) / 2.0 - 50.0) < 3.0,
                      qPrintable(QStringLiteral("parts: %1 is drawn about where it is dropped").arg(what)));
            }
        }

        // At size, where size is the point: an 1206 chip is 3.2 by 1.6, and a ferrite ring is 12 across
        // with a 6 mm hole - which is what has to fit over the bundle.
        auto span = [](PartSymbol s) {
            Bounds b;
            for (const Entity& e : partSymbolEntities(s, Vec2{0.0, 0.0})) b.expand(e.bounds());
            return QPointF(b.max.x - b.min.x, b.max.y - b.min.y);
        };
        const QPointF chip = span(PartSymbol::SmtResistor);
        check(near(chip.x(), 3.2) && near(chip.y(), 1.6),
              "parts: an 1206 chip resistor is 3.2 by 1.6 mm, because that is what it is");
        const QPointF cap = span(PartSymbol::SmtCapacitor);
        check(near(cap.x(), 3.2) && near(cap.y(), 1.6), "parts: and so is the chip capacitor");
        const QPointF ring = span(PartSymbol::FerriteRingTop);
        check(near(ring.x(), 12.0) && near(ring.y(), 12.0), "parts: a ferrite ring is 12 mm across");
        int holes = 0;
        for (const Entity& e : partSymbolEntities(PartSymbol::FerriteRingTop, Vec2{0.0, 0.0}))
            if (e.type == EntityType::Circle && near(e.radius, 3.0)) ++holes;
        check(holes == 1, "parts: with a 6 mm hole through it - the ring is a ring");

        // Both resistors: the box (IEC) and the zigzag (ANSI). The same part drawn two ways, so which one
        // a sheet uses is the office's convention - and they have to sit at the same weight beside each
        // other, or a schematic that uses both looks like two schematics.
        {
            const QPointF box = span(PartSymbol::SchResistor);
            const QPointF zig = span(PartSymbol::SchResistorZigzag);
            std::printf("  resistor symbols: box %.1f x %.1f, zigzag %.1f x %.1f\n", box.x(), box.y(),
                        zig.x(), zig.y());
            check(near(box.x(), zig.x()),
                  "resistors: the zigzag is exactly as long as the box, leads and all");
            check(near(box.y(), zig.y()),
                  "resistors: and as tall, so the two read at the same weight on a sheet");

            // A zigzag, and one that reads as a resistor: six legs, which is three full peaks. Fewer and
            // it reads as a kink in a wire.
            int zigLines = 0, peaks = 0, troughs = 0;
            const std::vector<Entity> zents = partSymbolEntities(PartSymbol::SchResistorZigzag,
                                                                 Vec2{0.0, 0.0});
            for (const Entity& e : zents) {
                if (e.type != EntityType::Polyline) continue;
                ++zigLines;
                for (const Vec2& v : e.points) {
                    if (near(v.y, 1.5)) ++peaks;
                    if (near(v.y, -1.5)) ++troughs;
                }
                // It leaves the leads level: the two ends of the zigzag are on the centre line, or the
                // symbol joins its wire with a step in it.
                check(near(e.points.front().y, 0.0) && near(e.points.back().y, 0.0),
                      "resistors: the zigzag starts and ends on the centre line, level with its leads");
            }
            check(zigLines == 1, "resistors: the zigzag is one polyline");
            check(peaks == 3 && troughs == 3,
                  "resistors: three peaks and three troughs - six legs, which is what reads as a resistor");
            // And it is a resistor, not a box: no rectangle in it.
            bool anyBox = false;
            for (const Entity& e : zents)
                if (e.type == EntityType::Rect) anyBox = true;
            check(!anyBox, "resistors: and there is no box in it");

            // The ICON has to show the zigzag, and it did not: an icon is drawn from the part's own drawing,
            // and the painter knew lines, circles and arcs but not POLYLINES - so the zigzag came out as a
            // pair of leads with a gap between them, and the axial resistor's waisted body had been missing
            // from its icon since the day it was drawn. Measured as ink away from the centre line, which is
            // the whole difference between a zigzag and a dash.
            const QImage img = makePartSymbolIcon(PartSymbol::SchResistorZigzag, QColor("#ffffff"))
                                   .pixmap(28, 28)
                                   .toImage();
            // How TALL the ink is, which is the whole difference: a dash spans two or three pixels, a
            // zigzag spans most of the icon. Asking for ink in two fixed bands instead put the answer at the
            // mercy of where a peak happens to round to.
            int top = img.height(), bottom = -1, inked = 0;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x) {
                    if (qAlpha(img.pixel(x, y)) < 40) continue;
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                    ++inked;
                }
            const int tall = bottom - top;
            std::printf("  zigzag icon: %d px of ink, %d px tall\n", inked, tall);
            check(tall >= 10,
                  "resistors: its icon is a zigzag and not a dash - the ink stands up off the centre line");
        }

        // A component dropped on a net goes INTO the net: the wire is cut between its two leads, so the
        // current has to go through the part. A line drawn straight through a resistor is a resistor with a
        // wire shorting it out, which is not what putting one there means.
        {
            Document doc;
            Editor ed;
            ed.setDocument(&doc);
            noSnap(ed);   // the points in this check are the points, not what a snap would prefer
            // A net across the sheet, and a resistor dropped on the middle of it.
            Entity wire;
            wire.type = EntityType::Line;
            wire.id = QStringLiteral("net");
            wire.a = {0.0, 0.0};
            wire.b = {100.0, 0.0};
            doc.entities().push_back(wire);

            ed.placeEntities(partSymbolEntities(PartSymbol::SchResistorZigzag, Vec2{0.0, 0.0}),
                             Vec2{0.0, 0.0});
            ed.onPointerPress(Vec2{50.0, 0.0}, Qt::LeftButton, Qt::NoModifier);

            // The net is two pieces now, and the gap between them is the symbol.
            std::vector<const Entity*> netPieces;
            for (const Entity& e : doc.entities())
                if (e.type == EntityType::Line &&
                    e.meta.value(kPartSymbolKey).toString().isEmpty())
                    netPieces.push_back(&e);
            std::printf("  net cut: %lld piece(s) of net left\n",
                        static_cast<long long>(netPieces.size()));
            check(netPieces.size() == 2, "net: the wire under a dropped component is cut in two");
            if (netPieces.size() == 2) {
                // Each piece runs from an end of the net to a lead of the symbol, and nothing crosses the
                // symbol itself: the leads are 9 units apart either side of the middle (18 long overall).
                double leftEnd = 0.0, rightStart = 0.0;
                for (const Entity* e : netPieces) {
                    const double lo = std::min(e->a.x, e->b.x), hi = std::max(e->a.x, e->b.x);
                    if (near(lo, 0.0)) leftEnd = hi;
                    if (near(hi, 100.0)) rightStart = lo;
                }
                std::printf("  net cut: kept 0..%.1f and %.1f..100\n", leftEnd, rightStart);
                check(near(leftEnd, 41.0) && near(rightStart, 59.0),
                      "net: cut exactly at the symbol's leads, and the piece between them is gone");
            }

            // A symbol dropped where it touches nothing cuts nothing.
            const size_t before = doc.entities().size();
            ed.placeEntities(partSymbolEntities(PartSymbol::SchResistorZigzag, Vec2{0.0, 0.0}),
                             Vec2{0.0, 0.0});
            ed.onPointerPress(Vec2{50.0, 60.0}, Qt::LeftButton, Qt::NoModifier);
            int stillTwo = 0;
            for (const Entity& e : doc.entities())
                if (e.type == EntityType::Line && e.meta.value(kPartSymbolKey).toString().isEmpty())
                    ++stillTwo;
            check(stillTwo == 2, "net: a component dropped clear of the net leaves it alone");
            check(doc.entities().size() > before, "net: and is still placed");

            // One lead on the net and one off it: drawn beside the net, not in it. Cutting there would be
            // inventing an intention.
            Entity other;
            other.type = EntityType::Line;
            other.id = QStringLiteral("net2");
            other.a = {0.0, 200.0};
            other.b = {100.0, 200.0};
            doc.entities().push_back(other);
            ed.placeEntities(partSymbolEntities(PartSymbol::SchResistor, Vec2{0.0, 0.0}), Vec2{0.0, 0.0});
            // Held by its middle: put down so its LEFT lead lands on the net's end and the rest runs off it.
            ed.onPointerPress(Vec2{100.0, 200.0}, Qt::LeftButton, Qt::NoModifier);
            int piecesOfTheSecond = 0;
            for (const Entity& e : doc.entities())
                if (e.type == EntityType::Line && e.meta.value(kPartSymbolKey).toString().isEmpty() &&
                    near(e.a.y, 200.0) && near(e.b.y, 200.0))
                    ++piecesOfTheSecond;
            std::printf("  net cut: the second net is in %d piece(s)\n", piecesOfTheSecond);
            check(piecesOfTheSecond == 1,
                  "net: a symbol lying across the END of a net, one lead off it, does not cut it");
        }

        // The ring from the side cannot show its hole, so those two edges are drawn hidden.
        int hidden = 0;
        for (const Entity& e : partSymbolEntities(PartSymbol::FerriteRingSide, Vec2{0.0, 0.0}))
            if (e.meta.value("lineType").toString() == QLatin1String("dashed")) ++hidden;
        check(hidden == 2,
              "parts: seen from the side, the ring's hole is drawn as two hidden edges");

        // The through-hole resistor is shaped like one: WAISTED, not a box. Its body is fuller at the
        // ends, where the caps are, than through the middle - which is what tells it from a chip
        // resistor lying on its side.
        {
            double atEnd = 0.0, atMiddle = 1e18;
            for (const Entity& e : partSymbolEntities(PartSymbol::ThResistor, Vec2{0.0, 0.0})) {
                if (e.type != EntityType::Polyline) continue;   // the two edges of the body
                for (const Vec2& v : e.points) {
                    if (std::abs(std::abs(v.x) - 3.5) < 1e-6) atEnd = std::max(atEnd, std::abs(v.y));
                    if (std::abs(v.x) < 1e-6) atMiddle = std::min(atMiddle, std::abs(v.y));
                }
            }
            check(atEnd > 1.2 && atMiddle < atEnd - 0.2,
                  "parts: the through-hole resistor is waisted - fuller at its ends than through the "
                  "middle");
        }

        // The chip resistor is told from the chip capacitor on the drawing: the resistor has its
        // printed face, the capacitor has none.
        check(partSymbolEntities(PartSymbol::SmtResistor, Vec2{0.0, 0.0}).size() >
                  partSymbolEntities(PartSymbol::SmtCapacitor, Vec2{0.0, 0.0}).size(),
              "parts: the chip resistor carries its printed face, so it is not the same drawing as the "
              "capacitor");

        // And a part is placed the way anything else is: handed to the cursor, dropped by a click.
        Document d;
        Editor ed;
        ed.setDocument(&d);
        ed.placeEntities(partSymbolEntities(PartSymbol::SchResistor, Vec2{0.0, 0.0}), Vec2{0.0, 0.0});
        check(ed.placementPreviewActive(), "parts: a chosen part is held under the cursor");
        ed.rotatePlacementPreview(true);   // R, before it lands
        ed.onPointerMove({60, 40});
        ed.onPointerPress({60, 40}, Qt::LeftButton, Qt::NoModifier);
        int placed = 0;
        Bounds b;
        for (const Entity& e : d.entities())
            if (e.meta.value(kPartSymbolKey).toString() == partSymbolName(PartSymbol::SchResistor)) {
                ++placed;
                b.expand(e.bounds());
            }
        check(placed > 0, "parts: and a click puts it on the drawing");
        check(b.valid && std::abs((b.min.x + b.max.x) / 2.0 - 60.0) < 3.0 &&
                  std::abs((b.min.y + b.max.y) / 2.0 - 40.0) < 3.0,
              "parts: where the cursor was");
        // Turned a quarter turn before landing, it stands up instead of lying down.
        check(b.max.y - b.min.y > b.max.x - b.min.x,
              "parts: turned with R first, it lands turned");
    }

    // --- Trimming an ellipse: half of it comes off between the lines that cross it.
    {
        Document d;
        Entity el;
        el.id = QStringLiteral("el");
        el.type = EntityType::Ellipse;
        el.center = {0, 0};
        el.radius = 20.0;    // across
        el.radiusY = 30.0;   // up
        d.entities().push_back(el);
        Entity cut;          // a line straight down through it: the ellipse is now two halves
        cut.id = QStringLiteral("cut");
        cut.type = EntityType::Line;
        cut.a = {0, -40};
        cut.b = {0, 40};
        d.entities().push_back(cut);

        Editor ed;
        ed.setDocument(&d);
        // Where a line crosses an ellipse, exactly: the top and the bottom of this one.
        const auto xs = lineEllipseIntersection(cut.a, cut.b - cut.a, el.center, el.radius,
                                                el.radiusY, el.rotation);
        check(xs.size() == 2, "trim ellipse: a line across it crosses it twice");
        bool topAndBottom = false;
        if (xs.size() == 2)
            topAndBottom = (near(std::abs(xs[0].y), 30.0) && near(std::abs(xs[1].y), 30.0) &&
                            near(xs[0].x, 0.0) && near(xs[1].x, 0.0));
        check(topAndBottom, "trim ellipse: and the crossings are ON the ellipse, not near it");

        // Trim: the line is the reference, and a cut line is drawn across the LEFT half.
        ed.setTool(ToolId::Trim);
        ed.onPointerPress({0, 20}, Qt::LeftButton, Qt::NoModifier);      // pick the reference line
        ed.onPointerPress({-30, 0}, Qt::LeftButton, Qt::NoModifier);     // the cut line, left of it
        ed.onPointerPress({-5, 0}, Qt::LeftButton, Qt::NoModifier);

        const Entity* left = nullptr;
        for (const auto& e : d.entities())
            if (e.id == QLatin1String("el")) left = &e;
        check(left != nullptr, "trim ellipse: the ellipse is still there - trimmed, not deleted");
        if (left) {
            check(left->meta.value("ellipticalArc").toBool(),
                  "trim ellipse: what is left is a piece of the ellipse");
            // The right half is kept: the rightmost point is still on it, the leftmost is gone.
            auto onKeptPiece = [&](double px, double py) {
                const double t = ellipseParamAt({px, py}, left->center, left->radius, left->radiusY,
                                                left->rotation);
                double sweep = left->endAngle - left->startAngle;
                while (sweep <= 0) sweep += 2.0 * M_PI;
                double rel = t - left->startAngle;
                while (rel < 0) rel += 2.0 * M_PI;
                return rel <= sweep + 1e-9;
            };
            check(onKeptPiece(20.0, 0.0), "trim ellipse: the side away from the cut is kept");
            check(!onKeptPiece(-20.0, 0.0), "trim ellipse: and the side that was cut is gone");
            // A click on the half that is gone must not select it any more.
            ed.setTool(ToolId::Select);
            ed.clearSelection();
            ed.onPointerPress({-20, 0}, Qt::LeftButton, Qt::NoModifier);
            const bool pickedEmpty = ed.selection().empty();
            ed.clearSelection();
            ed.onPointerPress({20, 0}, Qt::LeftButton, Qt::NoModifier);
            check(pickedEmpty && !ed.selection().empty(),
                  "trim ellipse: the half that is gone cannot be clicked; the half that is left can");
        }
    }

    // --- ... and with TWO lines across it, the piece between them comes off.
    {
        Document d;
        Entity el;
        el.id = QStringLiteral("el");
        el.type = EntityType::Ellipse;
        el.center = {0, 0};
        el.radius = 30.0;
        el.radiusY = 15.0;
        d.entities().push_back(el);
        for (const double y : {8.0, -8.0}) {   // two lines across it, above and below the middle
            Entity l;
            l.id = QStringLiteral("l%1").arg(y);
            l.type = EntityType::Line;
            l.a = {-40, y};
            l.b = {40, y};
            d.entities().push_back(l);
        }
        std::vector<Entity> cutters;
        for (const auto& e : d.entities())
            if (e.type == EntityType::Line) cutters.push_back(e);
        Entity work = el;
        // Picking the right-hand end takes off the stretch between the two lines on that side.
        check(trimEllipseAt(work, Vec2{30.0, 0.0}, cutters),
              "trim ellipse: two lines across it, and the piece between them comes off");
        auto on = [&](double px, double py) {
            const double t = ellipseParamAt({px, py}, work.center, work.radius, work.radiusY,
                                            work.rotation);
            double sweep = work.endAngle - work.startAngle;
            while (sweep <= 0) sweep += 2.0 * M_PI;
            double rel = t - work.startAngle;
            while (rel < 0) rel += 2.0 * M_PI;
            return rel <= sweep + 1e-9;
        };
        check(!on(30.0, 0.0), "trim ellipse: the picked stretch is the one that goes");
        check(on(-30.0, 0.0) && on(0.0, 15.0) && on(0.0, -15.0),
              "trim ellipse: and the rest of it - both ends and the far side - stays");
        // Trimming what is left again: the top comes off the piece.
        check(trimEllipseAt(work, Vec2{0.0, 15.0}, cutters),
              "trim ellipse: a piece of an ellipse can be trimmed again");
    }

    // --- Trimming to a curve: exactly onto it, and the piece between the crossings is what goes.
    {
        auto ellipseAt = [](const QString& id, double rx, double ry) {
            Entity e;
            e.id = id;
            e.type = EntityType::Ellipse;
            e.center = {0, 0};
            e.radius = rx;
            e.radiusY = ry;
            return e;
        };
        // The line crosses the ellipse at x = 30: y = +-60*sqrt(1-(30/100)^2) = +-57.2364...
        const double yCross = 60.0 * std::sqrt(1.0 - 0.09);

        // 1) A whole ellipse as the reference: the part of the line inside it goes, and the two ends
        // left behind sit ON the ellipse rather than a hair inside it.
        {
            Document d;
            d.entities().push_back(ellipseAt(QStringLiteral("el"), 100.0, 60.0));
            Entity l;
            l.id = QStringLiteral("cut");
            l.type = EntityType::Line;
            l.a = {30, -90};
            l.b = {30, 90};
            d.entities().push_back(l);
            Editor ed;
            ed.setDocument(&d);
            ed.setTool(ToolId::Trim);
            ed.onPointerPress({0, 60}, Qt::LeftButton, Qt::NoModifier);    // the ellipse: reference
            ed.onPointerPress({10, 0}, Qt::LeftButton, Qt::NoModifier);    // a cut line across the line
            ed.onPointerPress({50, 0}, Qt::LeftButton, Qt::NoModifier);
            std::vector<double> ends;
            int lines = 0;
            for (const auto& e : d.entities())
                if (e.type == EntityType::Line) {
                    ++lines;
                    ends.push_back(e.a.y);
                    ends.push_back(e.b.y);
                }
            check(lines == 2, "trim to a curve: the stretch inside the ellipse goes, leaving both ends");
            bool onTheCurve = true;
            for (const double y : ends)
                if (!near(std::abs(y), 90.0) && std::abs(std::abs(y) - yCross) > 1e-6)
                    onTheCurve = false;
            check(onTheCurve,
                  "trim to a curve: and the new ends are ON the ellipse - not the fraction short that "
                  "cutting against a many-sided polygon leaves");
        }

        // 2) The reference is HALF an ellipse whose two ends sit exactly on the line - which is what is
        // left after trimming that ellipse with that line. Both crossings still have to be found: one
        // of them going missing took everything from the cut to the far end of the line.
        {
            Document d;
            Entity half = ellipseAt(QStringLiteral("el"), 100.0, 60.0);
            half.meta.insert("ellipticalArc", true);
            half.startAngle = ellipseParamAt({30.0, -yCross}, half.center, half.radius, half.radiusY, 0.0);
            half.endAngle = ellipseParamAt({30.0, yCross}, half.center, half.radius, half.radiusY, 0.0);
            d.entities().push_back(half);
            Entity l;
            l.id = QStringLiteral("cut");
            l.type = EntityType::Line;
            l.a = {30, -90};
            l.b = {30, 90};
            d.entities().push_back(l);
            Editor ed;
            ed.setDocument(&d);
            ed.setTool(ToolId::Trim);
            ed.onPointerPress({100, 0}, Qt::LeftButton, Qt::NoModifier);   // the arc's far side
            ed.onPointerPress({10, 0}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerPress({50, 0}, Qt::LeftButton, Qt::NoModifier);
            int lines = 0;
            double kept = 0.0;
            for (const auto& e : d.entities())
                if (e.type == EntityType::Line) {
                    ++lines;
                    kept += std::abs(e.b.y - e.a.y);
                }
            check(lines == 2,
                  "trim to a curve: an arc whose own ends touch the line still cuts it in both places");
            check(std::abs(kept - 2.0 * (90.0 - yCross)) < 1e-6,
                  "trim to a curve: so what is taken off is the piece between them and nothing more");
        }

        // 3) Extending a line onto an ellipse lands exactly on it, for the same reason.
        {
            Document d;
            d.entities().push_back(ellipseAt(QStringLiteral("el"), 100.0, 60.0));
            Entity l;
            l.id = QStringLiteral("stub");
            l.type = EntityType::Line;
            l.a = {30, 0};
            l.b = {30, 20};
            d.entities().push_back(l);
            std::vector<Entity> bounds;
            for (const auto& e : d.entities())
                if (e.type == EntityType::Ellipse) bounds.push_back(e);
            Entity line = l;
            check(extendLineTo(line, Vec2{30.0, 20.0}, bounds),
                  "extend to a curve: a line can be extended onto an ellipse");
            check(std::abs(line.b.y - yCross) < 1e-6,
                  "extend to a curve: and it stops exactly on it");
        }
    }

    // --- A run with a CORNER in it, trimmed by half an ellipse: only the piece between the ellipse's
    // two ends goes - not everything down to the corner.
    {
        for (const bool asPolyline : {true, false}) {
            Document d;
            Entity el;
            el.id = QStringLiteral("el");
            el.type = EntityType::Ellipse;
            el.center = {0, 0};
            el.radius = 60.0;
            el.radiusY = 100.0;
            el.meta.insert("ellipticalArc", true);
            el.startAngle = -M_PI / 2.0;          // its ends sit ON the upright, which is where the
            el.endAngle = M_PI / 2.0 + 0.25;      // line that trimmed it was; one end runs a little past
            d.entities().push_back(el);
            if (asPolyline) {
                Entity pl;                        // an L: down the upright, then out along the foot
                pl.id = QStringLiteral("run");
                pl.type = EntityType::Polyline;
                pl.points = {{0, 200}, {0, -200}, {260, -200}};
                d.entities().push_back(pl);
            } else {
                Entity up;
                up.id = QStringLiteral("run");
                up.type = EntityType::Line;
                up.a = {0, 200};
                up.b = {0, -200};
                d.entities().push_back(up);
                Entity foot;
                foot.id = QStringLiteral("foot");
                foot.type = EntityType::Line;
                foot.a = {0, -200};
                foot.b = {260, -200};
                d.entities().push_back(foot);
            }
            Editor ed;
            ed.setDocument(&d);
            ed.setTool(ToolId::Trim);
            ed.onPointerPress({60, 0}, Qt::LeftButton, Qt::NoModifier);     // the ellipse: reference
            ed.onPointerPress({-25, 40}, Qt::LeftButton, Qt::NoModifier);   // fence across the upright,
            ed.onPointerPress({25, 40}, Qt::LeftButton, Qt::NoModifier);    // between the arc's ends

            // What is left, measured on the upright: the piece above the top end and the piece below
            // the bottom end, and the corner and the foot untouched.
            bool upperKept = false, lowerKept = false, footKept = false, insideLeft = false;
            for (const auto& e : d.entities()) {
                std::vector<Vec2> run;
                if (e.type == EntityType::Line) run = {e.a, e.b};
                else if (e.type == EntityType::Polyline) run = e.points;
                else continue;
                for (size_t i = 1; i < run.size(); ++i) {
                    const Vec2 A = run[i - 1], B = run[i];
                    if (!near(A.x, 0.0) || !near(B.x, 0.0)) {              // the foot
                        if (std::abs(B.x - 260.0) < 1e-6 || std::abs(A.x - 260.0) < 1e-6)
                            footKept = true;
                        continue;
                    }
                    const double lo = std::min(A.y, B.y), hi = std::max(A.y, B.y);
                    if (hi > 150.0) upperKept = true;
                    if (lo < -150.0) lowerKept = true;
                    // Nothing of the upright may be left between the arc's two ends.
                    if (lo < 90.0 && hi > -90.0) insideLeft = true;
                }
            }
            const char* kind = asPolyline ? "one polyline" : "two lines";
            check(upperKept, qPrintable(QStringLiteral("trim past a corner (%1): the piece above the "
                                                      "ellipse is kept").arg(kind)));
            check(lowerKept, qPrintable(QStringLiteral("trim past a corner (%1): and the piece BELOW it "
                                                      "is kept - the trim stops at the ellipse, not at "
                                                      "the corner").arg(kind)));
            check(footKept, qPrintable(QStringLiteral("trim past a corner (%1): the corner and what "
                                                     "follows it are untouched").arg(kind)));
            check(!insideLeft, qPrintable(QStringLiteral("trim past a corner (%1): and the piece "
                                                        "between the ellipse's ends is gone").arg(kind)));
        }
    }

    // --- Every pairing a corner can be made of, and a refusal that SHOWS where it happened.
    {
        // A shape is drawn out of whatever is to hand - a polyline for three sides, a line for the
        // fourth, a notch made of half an ellipse - and every corner of it is between two objects.
        auto lineDown = [] {
            Entity e;
            e.id = QStringLiteral("a");
            e.type = EntityType::Line;
            e.a = {0, 40};
            e.b = {0, 300};      // up to the joint at (0,300)
            return e;
        };
        auto polyDown = [] {
            Entity e;
            e.id = QStringLiteral("a");
            e.type = EntityType::Polyline;
            e.points = {{0, 40}, {0, 300}};
            return e;
        };
        auto polyRight = [] {
            Entity e;
            e.id = QStringLiteral("b");
            e.type = EntityType::Polyline;
            e.points = {{0, 300}, {220, 300}, {220, 100}};
            return e;
        };
        auto lineRight = [] {
            Entity e;
            e.id = QStringLiteral("b");
            e.type = EntityType::Line;
            e.a = {0, 300};
            e.b = {220, 300};
            return e;
        };
        struct Pair { const char* what; Entity first, second; };
        std::vector<Pair> pairs;
        pairs.push_back({"polyline + line", polyDown(), lineRight()});
        pairs.push_back({"line + polyline", lineDown(), polyRight()});
        pairs.push_back({"polyline + polyline", polyDown(), polyRight()});
        for (const Pair& pr : pairs) {
            Document d;
            d.entities().push_back(pr.first);
            d.entities().push_back(pr.second);
            Editor ed;
            ed.setDocument(&d);
            ed.setTool(ToolId::Fillet);
            ed.setFilletRadius(20.0);
            ed.onPointerPress({0, 250}, Qt::LeftButton, Qt::NoModifier);    // up the first
            ed.onPointerPress({40, 300}, Qt::LeftButton, Qt::NoModifier);   // along the second
            int arcs = 0;
            for (const auto& e : d.entities())
                if (e.type == EntityType::Arc) ++arcs;
            check(arcs == 1, qPrintable(QStringLiteral("pairings: %1 rounds at their joint")
                                            .arg(QString::fromLatin1(pr.what))));
            check(!ed.refused(), qPrintable(QStringLiteral("pairings: %1 is not refused")
                                                .arg(QString::fromLatin1(pr.what))));
        }

        // Two DOUBLE runs - a double line, a double polyline, either way round - join into one run with
        // the corner rounded, so both rails turn together and the gap stays.
        {
            Document d;
            Entity a;
            a.id = QStringLiteral("a");
            a.type = EntityType::DoubleLine;
            a.a = {0, 0};
            a.b = {100, 0};
            a.meta.insert("gap", 10.0);
            d.entities().push_back(a);
            Entity b;
            b.id = QStringLiteral("b");
            b.type = EntityType::Polyline;
            b.points = {{100, 0}, {100, 100}, {200, 100}};
            b.meta.insert("double", true);
            b.meta.insert("gap", 10.0);
            d.entities().push_back(b);
            Editor ed;
            ed.setDocument(&d);
            noSnap(ed);
            ed.setPixelTolerance(3);
            ed.setFilletRadius(5.0);
            ed.setTool(ToolId::Fillet);
            ed.onPointerPress({50, 5}, Qt::LeftButton, Qt::NoModifier);     // the double line's rail
            ed.onPointerPress({105, 50}, Qt::LeftButton, Qt::NoModifier);   // the double polyline's
            const Entity* merged = nullptr;
            int doubles = 0;
            for (const auto& e : d.entities()) {
                if (e.type == EntityType::DoubleLine) ++doubles;
                if (e.type == EntityType::Polyline && e.meta.value("double").toBool()) merged = &e;
            }
            check(doubles == 0 && merged && merged->points.size() == 4,
                  "pairings: a double line and a double polyline join into ONE double run");
            if (merged) {
                const QJsonArray radii = merged->meta.value("cornerRadii").toArray();
                check(radii.size() == 4 && near(radii[1].toDouble(), 5.0),
                      "pairings: with the radius at the join and nowhere else");
                check(near(radii[2].toDouble(), 0.0),
                      "pairings: the corners the two runs already had are left as they were");
            }
        }

        // And a refusal is SHOWN on the drawing, where it happened - not only said at the bottom of the
        // window, which is easy to miss with the eye on the sheet.
        {
            Document d;
            Entity dbl;
            dbl.id = QStringLiteral("dbl");
            dbl.type = EntityType::DoubleLine;
            dbl.a = {0, 0};
            dbl.b = {100, 0};
            dbl.meta.insert("gap", 10.0);
            d.entities().push_back(dbl);
            Entity ln;
            ln.id = QStringLiteral("ln");
            ln.type = EntityType::Line;
            ln.a = {100, 0};
            ln.b = {100, 100};
            d.entities().push_back(ln);
            Editor ed;
            ed.setDocument(&d);
            noSnap(ed);
            ed.setPixelTolerance(3);
            ed.setFilletRadius(5.0);
            ed.setTool(ToolId::Fillet);
            check(!ed.refused(), "refusal: nothing is marked to begin with");
            ed.onPointerPress({50, 5}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerPress({100, 50}, Qt::LeftButton, Qt::NoModifier);
            check(ed.refused(), "refusal: a double run and a single line cannot be rounded, and it says "
                                "so ON the drawing");
            check(near(ed.refusedAt().x, 100.0) && near(ed.refusedAt().y, 50.0),
                  "refusal: marked where the click was");
            ed.setTool(ToolId::Select);
            check(!ed.refused(), "refusal: and cleared when the next command starts");
        }
    }

    // --- Rounding where the END OF A RUN meets something else: a polyline and a line, or a curve.
    {
        // A shape drawn as a polyline for three sides and a separate line (or a notch) for the fourth is
        // the ordinary way one is drawn, and its corners are then between TWO OBJECTS, not inside one.
        // Filleting knew how to round two lines, and how to round a run's own corner, and nothing about
        // this - so the corner where the run met the line could not be rounded at all.
        for (const bool curve : {false, true}) {
            Document d;
            Entity run;
            run.id = QStringLiteral("run");
            run.type = EntityType::Polyline;
            run.points = {{0, 300}, {220, 300}, {220, 0}, {0, 0}};   // starting at the top-left corner
            d.entities().push_back(run);
            Entity other;
            other.id = QStringLiteral("other");
            if (curve) {
                other.type = EntityType::Ellipse;
                other.center = {0, 240};
                other.radius = 40.0;
                other.radiusY = 60.0;
                other.meta.insert("ellipticalArc", true);
                other.startAngle = -M_PI / 2.0;   // bulging into the shape, top end at (0,300)
                other.endAngle = M_PI / 2.0;
            } else {
                other.type = EntityType::Line;
                other.a = {0, 300};               // the joint
                other.b = {0, 40};
            }
            d.entities().push_back(other);

            Editor ed;
            ed.setDocument(&d);
            ed.setTool(ToolId::Fillet);
            ed.setFilletRadius(20.0);
            ed.onPointerPress({20, 300}, Qt::LeftButton, Qt::NoModifier);            // the run's top edge
            ed.onPointerPress(curve ? Vec2{30.0, 270.0} : Vec2{0.0, 240.0}, Qt::LeftButton,
                              Qt::NoModifier);                                        // the other object
            const Entity* r = nullptr;
            const Entity* fillet = nullptr;
            for (const auto& e : d.entities()) {
                if (e.id == QLatin1String("run")) r = &e;
                else if (e.type == EntityType::Arc && e.id != QLatin1String("other")) fillet = &e;
            }
            const char* kind = curve ? "a half ellipse" : "a line";
            check(fillet != nullptr,
                  qPrintable(QStringLiteral("run fillet: the end of a run rounds to %1").arg(kind)));
            if (r && fillet) {
                check(r->points.size() == 4,
                      qPrintable(QStringLiteral("run fillet (%1): the run keeps its shape").arg(kind)));
                check(r->points[0].x > 1.0 && near(r->points[0].y, 300.0),
                      qPrintable(QStringLiteral("run fillet (%1): its END is taken back along its own "
                                                "edge to where the fillet starts").arg(kind)));
                check(near(r->points[1].x, 220.0) && near(r->points[3].x, 0.0),
                      qPrintable(QStringLiteral("run fillet (%1): and the rest of it is untouched")
                                     .arg(kind)));
                check(near(fillet->radius, 20.0),
                      qPrintable(QStringLiteral("run fillet (%1): the arc is the radius asked for")
                                     .arg(kind)));
            }
        }

        // Picked in the middle of a run, there is nothing to take back: say so rather than doing
        // something surprising.
        {
            Document d;
            Entity run;
            run.id = QStringLiteral("run");
            run.type = EntityType::Polyline;
            run.points = {{0, 300}, {220, 300}, {220, 0}, {0, 0}};
            d.entities().push_back(run);
            Entity line;
            line.id = QStringLiteral("l");
            line.type = EntityType::Line;
            line.a = {300, 150};
            line.b = {400, 150};
            d.entities().push_back(line);
            Editor ed;
            ed.setDocument(&d);
            QString msg;
            QObject::connect(&ed, &Editor::message, [&msg](const QString& m) { msg = m; });
            ed.setTool(ToolId::Fillet);
            ed.setFilletRadius(20.0);
            ed.onPointerPress({220, 150}, Qt::LeftButton, Qt::NoModifier);   // the run's MIDDLE edge
            ed.onPointerPress({350, 150}, Qt::LeftButton, Qt::NoModifier);
            int arcs = 0;
            for (const auto& e : d.entities())
                if (e.type == EntityType::Arc) ++arcs;
            check(arcs == 0 && msg.contains(QLatin1String("END")),
                  "run fillet: picked in the middle of a run, it says which end to pick instead");
        }
    }

    // --- Rounding a corner where two objects MEET, clicked at the corner - which is where one clicks.
    {
        // Two separate lines sharing an end. The pick lands ON the corner (the snap puts it there), and
        // a pick at the corner says nothing about which side of each line to keep - read as "no side",
        // the command used to answer "these do not form a corner" and do nothing.
        {
            Document d;
            Entity v;
            v.id = QStringLiteral("v");
            v.type = EntityType::Line;
            v.a = {0, 300};
            v.b = {0, 100};
            d.entities().push_back(v);
            Entity h;
            h.id = QStringLiteral("h");
            h.type = EntityType::Line;
            h.a = {0, 100};      // the joint
            h.b = {200, 100};
            d.entities().push_back(h);
            Editor ed;
            ed.setDocument(&d);
            ed.setTool(ToolId::Fillet);
            ed.setFilletRadius(10.0);
            ed.onPointerPress({0, 106}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerPress({6, 100}, Qt::LeftButton, Qt::NoModifier);
            const Entity* vert = nullptr;
            const Entity* horz = nullptr;
            const Entity* arc = nullptr;
            for (const auto& e : d.entities()) {
                if (e.id == QLatin1String("v")) vert = &e;
                else if (e.id == QLatin1String("h")) horz = &e;
                else if (e.type == EntityType::Arc) arc = &e;
            }
            check(arc != nullptr,
                  "corner fillet: two lines with a mutual joint round at the joint, clicked there");
            if (vert && horz && arc) {
                check(near(vert->a.y, 300.0) && near(vert->b.y, 110.0),
                      "corner fillet: each line keeps its far end and stops a radius short of the "
                      "corner");
                check(near(horz->a.x, 10.0) && near(horz->b.x, 200.0),
                      "corner fillet: on both lines");
                check(near(arc->radius, 10.0) && near(arc->center.x, 10.0) &&
                          near(arc->center.y, 110.0),
                      "corner fillet: and the arc between them is tangent to both");
            }
        }

        // A line meeting a SEMICIRCLE at its end, and a line meeting HALF AN ELLIPSE at its end. In both
        // the corner is one of the curve's own ends, so what comes off the curve is the bit between the
        // fillet and THAT end - not the rest of it, which is what taking the side from the pick did (a
        // click at the joint names the very bit that is about to go).
        for (const bool ellipse : {false, true}) {
            Document d;
            Entity cv;
            cv.id = QStringLiteral("cv");
            if (ellipse) {
                cv.type = EntityType::Ellipse;
                cv.radius = 40.0;
                cv.radiusY = 60.0;
                cv.meta.insert("ellipticalArc", true);
            } else {
                cv.type = EntityType::Arc;
                cv.radius = 60.0;
            }
            cv.center = {0, 0};
            cv.startAngle = -M_PI / 2.0;
            cv.endAngle = M_PI / 2.0;        // its top end is (0, 60)
            d.entities().push_back(cv);
            Entity up;
            up.id = QStringLiteral("up");
            up.type = EntityType::Line;
            up.a = {0, 260};
            up.b = {0, 60};                  // meeting that end
            d.entities().push_back(up);
            Editor ed;
            ed.setDocument(&d);
            ed.setTool(ToolId::Fillet);
            ed.setFilletRadius(10.0);
            ed.onPointerPress({0, 66}, Qt::LeftButton, Qt::NoModifier);   // the line, at the joint
            ed.onPointerPress({4, 57}, Qt::LeftButton, Qt::NoModifier);   // the curve, at the joint
            const Entity* curve = nullptr;
            const Entity* line = nullptr;
            const Entity* fillet = nullptr;
            for (const auto& e : d.entities()) {
                if (e.id == QLatin1String("cv")) curve = &e;
                else if (e.id == QLatin1String("up")) line = &e;
                else if (e.type == EntityType::Arc) fillet = &e;
            }
            const char* kind = ellipse ? "half ellipse" : "semicircle";
            check(fillet != nullptr,
                  qPrintable(QStringLiteral("corner fillet: a line and a %1 round where they meet")
                                 .arg(kind)));
            if (curve && line && fillet) {
                check(near(line->a.y, 260.0) && line->b.y > 60.0 && line->b.y < 90.0,
                      qPrintable(QStringLiteral("corner fillet (%1): the line keeps its far end and is "
                                                "taken BACK from the joint to where the fillet starts")
                                     .arg(kind)));
                // The curve keeps the long side: its sweep loses only the bit at the corner.
                double sweep = curve->endAngle - curve->startAngle;
                while (sweep <= 0.0) sweep += 2.0 * M_PI;
                check(sweep > M_PI * 0.7 && sweep < M_PI,
                      qPrintable(QStringLiteral("corner fillet (%1): and the curve gives up the bit at "
                                                "the corner, not most of itself").arg(kind)));
                check(near(fillet->radius, 10.0),
                      qPrintable(QStringLiteral("corner fillet (%1): with an arc of the radius asked "
                                                "for").arg(kind)));
            }
        }
    }

    // --- At a joint, the topmost object is the one picked - and a two-object command finds the other.
    {
        Document d;
        Entity el;
        el.id = QStringLiteral("el");
        el.type = EntityType::Ellipse;
        el.center = {0, 0};
        el.radius = 60.0;
        el.radiusY = 100.0;
        el.meta.insert("ellipticalArc", true);
        el.startAngle = -M_PI / 2.0;
        el.endAngle = M_PI / 2.0;
        d.entities().push_back(el);
        Entity line;                       // drawn after it, and ENDING on its top end
        line.id = QStringLiteral("line");
        line.type = EntityType::Line;
        line.a = {0, 300};
        line.b = {0, 100};
        d.entities().push_back(line);

        Editor ed;
        ed.setDocument(&d);
        ed.setTool(ToolId::Select);
        ed.onPointerPress({0, 104}, Qt::LeftButton, Qt::NoModifier);
        check(!ed.selection().empty() && ed.selection().front() == QLatin1String("line"),
              "pick at a joint: the topmost object wins the tie, not whatever is underneath it");

        // The fillet needs two DIFFERENT objects, and both clicks are at the joint - which is how it is
        // actually used. Asked again for the other one, it finds the ellipse.
        Editor f;
        f.setDocument(&d);
        f.setTool(ToolId::Fillet);
        f.setFilletRadius(15.0);
        f.onPointerPress({0, 104}, Qt::LeftButton, Qt::NoModifier);
        f.onPointerPress({3, 97}, Qt::LeftButton, Qt::NoModifier);
        int fillets = 0;
        for (const auto& e : d.entities())
            if (e.type == EntityType::Arc) ++fillets;
        check(fillets == 1,
              "pick at a joint: so a line meeting a curve there can be filleted, both clicks at the "
              "joint");
    }

    // --- The joints of a curve: an end SWINGS, the size is a different handle.
    {
        // A piece of an ellipse: dragging one of its ends moves that end round the ellipse and leaves
        // the ellipse's own size alone. Before, the only handles a piece had were its ellipse's axis
        // points, so pulling what looked like its end resized the whole ellipse.
        Document d;
        Entity el;
        el.id = QStringLiteral("el");
        el.type = EntityType::Ellipse;
        el.center = {0, 0};
        el.radius = 60.0;
        el.radiusY = 100.0;
        el.meta.insert("ellipticalArc", true);
        el.startAngle = -M_PI / 2.0;
        el.endAngle = M_PI / 2.0;
        d.entities().push_back(el);
        Editor ed;
        ed.setDocument(&d);
        ed.setTool(ToolId::Select);
        ed.onPointerPress({60, 0}, Qt::LeftButton, Qt::NoModifier);      // pick it
        ed.onPointerPress({0, 100}, Qt::LeftButton, Qt::NoModifier);     // grab its top end
        ed.onPointerMove({42, 71});                                      // swing it round to about 45
        ed.onPointerRelease({42, 71}, Qt::LeftButton);
        const Entity& piece = d.entities().front();
        check(near(piece.radius, 60.0) && near(piece.radiusY, 100.0),
              "curve joints: dragging an end of a piece leaves the ellipse the size it was");
        check(piece.endAngle < M_PI / 2.0 - 0.1 && piece.endAngle > 0.0,
              "curve joints: and moves that end round it");

        // An arc's end likewise: it swings, and the radius is the middle handle's business.
        Document d2;
        Entity a;
        a.id = QStringLiteral("arc");
        a.type = EntityType::Arc;
        a.center = {0, 0};
        a.radius = 100.0;
        a.startAngle = 0.0;
        a.endAngle = M_PI / 2.0;
        d2.entities().push_back(a);
        Editor e2;
        e2.setDocument(&d2);
        e2.setTool(ToolId::Select);
        e2.onPointerPress({70.7, 70.7}, Qt::LeftButton, Qt::NoModifier);
        e2.onPointerPress({0, 100}, Qt::LeftButton, Qt::NoModifier);     // its end
        e2.onPointerMove({-40, 60});                                     // round, and nearer in
        e2.onPointerRelease({-40, 60}, Qt::LeftButton);
        const Entity& arc = d2.entities().front();
        check(near(arc.radius, 100.0),
              "curve joints: an arc's end swings without changing its radius");
        check(arc.endAngle > M_PI / 2.0,
              "curve joints: and it does swing - the end is where it was dragged to");
    }

    // --- Space turns what is being moved: a quarter turn clockwise, Shift+Space the other way.
    {
        Document d;
        Entity l;
        l.id = QStringLiteral("l");
        l.type = EntityType::Line;
        l.a = {0, 0};
        l.b = {40, 0};   // pointing along +x
        d.entities().push_back(l);
        Editor ed;
        ed.setDocument(&d);
        ed.setTool(ToolId::Select);
        ed.onPointerPress({20, 0}, Qt::LeftButton, Qt::NoModifier);   // pick it
        ed.onPointerPress({20, 0}, Qt::LeftButton, Qt::NoModifier);   // and grab it
        ed.onPointerMove({120, 0});                                   // carry it to the right
        check(ed.movingSomething(), "turn while moving: a move is under way");
        ed.rotateMoveBy90(true);                                      // Space
        ed.onPointerRelease({120, 0}, Qt::LeftButton);
        const Entity& moved = d.entities().front();
        check(near(moved.a.x, 120.0) && near(moved.b.x, 120.0),
              "turn while moving: it is standing up now, turned about the cursor");
        check(std::abs(std::abs(moved.a.y - moved.b.y) - 40.0) < 1e-6,
              "turn while moving: and it is the same line, not a resized one");
        check(near((moved.a.y + moved.b.y) / 2.0, 0.0),
              "turn while moving: with the point under the cursor still under it");
        // The other way round, and while dragging by a JOINT rather than the whole object.
        Editor e2;
        e2.setDocument(&d);
        e2.setTool(ToolId::Select);
        e2.onPointerPress({120, 20}, Qt::LeftButton, Qt::NoModifier);
        const int before = int(d.entities().size());
        e2.onPointerPress({120, 20}, Qt::LeftButton, Qt::NoModifier);
        e2.onPointerMove({120, 25});
        e2.rotateMoveBy90(false);                                     // Shift+Space
        e2.onPointerRelease({120, 25}, Qt::LeftButton);
        check(int(d.entities().size()) == before,
              "turn while moving: turning does not add or lose anything");
    }

    // --- Mirroring a PIECE of an ellipse gives its mirror image, whatever piece it is.
    {
        // A piece of an ellipse says where it starts and stops in the ellipse's own parameter, and
        // reflecting the ellipse turns that parameter round - so the piece has to be turned round with
        // it. A half happens to be symmetric in its own parameter and came out right without that,
        // which is why this only showed on a piece that was not: the copy was the WRONG PIECE of the
        // right ellipse.
        struct Piece { double rot, from, to; };
        const Piece pieces[] = {{0.0, -90.0, 90.0},   // a half: symmetric, right either way
                                {0.0, 0.0, 90.0},     // a quarter
                                {0.0, 10.0, 120.0},   // lopsided
                                {20.0, 10.0, 120.0}}; // ... and turned as well
        for (const Piece& pc : pieces) {
            for (const bool slanted : {false, true}) {
                Entity el;
                el.type = EntityType::Ellipse;
                el.center = {0, 0};
                el.radius = 40.0;
                el.radiusY = 25.0;
                el.rotation = pc.rot * M_PI / 180.0;
                el.meta.insert("ellipticalArc", true);
                el.startAngle = pc.from * M_PI / 180.0;
                el.endAngle = pc.to * M_PI / 180.0;
                // The mirror line: straight up, or leaning, through (90, 0).
                const Vec2 mA{90.0, 0.0};
                const Vec2 mB = slanted ? Vec2{92.0, 1.0} : Vec2{90.0, 1.0};
                Entity m = el;
                mirrorEntity(m, mA, mB);

                // By hand: mirror each point of the original, and the copy must pass through it.
                const Vec2 u = (mB - mA).normalized();
                auto byHand = [&](const Vec2& p) {
                    const Vec2 d = p - mA;
                    const Vec2 onLine = mA + u * dot(d, u);
                    return onLine * 2.0 - p;
                };
                double sweep = el.endAngle - el.startAngle;
                while (sweep <= 0.0) sweep += 2.0 * M_PI;
                double msweep = m.endAngle - m.startAngle;
                while (msweep <= 0.0) msweep += 2.0 * M_PI;
                double worst = 0.0;
                for (int k = 0; k <= 24; ++k) {
                    const Vec2 want =
                        byHand(ellipsePointAt(el.startAngle + sweep * k / 24, el.center, el.radius,
                                              el.radiusY, el.rotation));
                    double best = 1e18;
                    for (int j = 0; j <= 240; ++j)
                        best = std::min(best,
                                        distance(want, ellipsePointAt(m.startAngle + msweep * j / 240,
                                                                      m.center, m.radius, m.radiusY,
                                                                      m.rotation)));
                    worst = std::max(worst, best);
                }
                check(worst < 0.05,
                      "mirror an ellipse piece: the copy runs where the mirror image of the original "
                      "runs");
                check(std::abs(msweep - sweep) < 1e-9,
                      "mirror an ellipse piece: and it is the same amount of ellipse, not more or less");
            }
        }
        // The plain statement of it: a quarter mirrored is the opposite quarter.
        Entity q;
        q.type = EntityType::Ellipse;
        q.center = {0, 0};
        q.radius = 40.0;
        q.radiusY = 25.0;
        q.meta.insert("ellipticalArc", true);
        q.startAngle = 0.0;
        q.endAngle = M_PI / 2.0;
        mirrorEntity(q, Vec2{90.0, 0.0}, Vec2{90.0, 1.0});
        check(near(q.startAngle, -M_PI / 2.0) && std::abs(q.endAngle) < 1e-9,
              "mirror an ellipse piece: the parameter is turned round, not left as it was");
    }

    // --- Filleting a line to a curve: a half ellipse, a whole circle, and the radius that will not fit.
    {
        auto halfEllipse = [] {
            Entity el;
            el.id = QStringLiteral("el");
            el.type = EntityType::Ellipse;
            el.center = {0, 0};
            el.radius = 60.0;
            el.radiusY = 100.0;
            el.meta.insert("ellipticalArc", true);
            el.startAngle = -M_PI / 2.0;   // its ends on x = 0, bulging to the right
            el.endAngle = M_PI / 2.0;
            return el;
        };
        auto lineDownTo = [] {
            Entity l;
            l.id = QStringLiteral("line");
            l.type = EntityType::Line;
            l.a = {0, 260};
            l.b = {0, 100};   // ending at the ellipse's top end
            return l;
        };

        // 1) The reported case: a line meeting the top end of a half ellipse.
        {
            Entity el = halfEllipse(), line = lineDownTo();
            const CornerResult r =
                filletLineCurve(line, el, 25.0, Vec2{0.0, 200.0}, Vec2{45.0, 60.0});
            check(r.ok && r.hasConnector,
                  "fillet to a curve: a line and a half ellipse can be filleted");
            if (r.ok) {
                const Entity& arc = r.connector;
                check(arc.type == EntityType::Arc && near(arc.radius, 25.0),
                      "fillet to a curve: what is added is an arc of the radius asked for");
                // Tangent to the line: its centre is exactly the radius away from it.
                check(std::abs(std::abs(arc.center.x) - 25.0) < 1e-6,
                      "fillet to a curve: tangent to the line - its centre is the radius clear of it");
                // Tangent to the ellipse: the centre is the radius from the point it touches, and that
                // point is ON the ellipse.
                const Vec2 touch = ellipsePointAt(el.endAngle, el.center, el.radius, el.radiusY,
                                                  el.rotation);
                check(std::abs(distance(arc.center, touch) - 25.0) < 1e-4,
                      "fillet to a curve: and tangent to the ellipse, at the end it was trimmed back to");
                // Both objects were taken back to where the fillet meets them, and the fillet's own two
                // ends are exactly those places - no gap, no overlap.
                const Vec2 e0 = pointOnCircle(arc.center, arc.radius, arc.startAngle);
                const Vec2 e1 = pointOnCircle(arc.center, arc.radius, arc.endAngle);
                const double toLineEnd = std::min(distance(e0, line.b), distance(e1, line.b));
                const double toCurve = std::min(distance(e0, touch), distance(e1, touch));
                check(toLineEnd < 1e-4 && toCurve < 1e-4,
                      "fillet to a curve: the fillet starts where the line now ends and finishes where "
                      "the ellipse now does");
                check(near(line.a.y, 260.0) && line.b.y < 260.0,
                      "fillet to a curve: the picked end of the line is the end that is kept");
                check(el.endAngle < M_PI / 2.0 - 1e-9,
                      "fillet to a curve: and the ellipse is shortened, not left crossing the fillet");
            }
        }

        // 2) A WHOLE circle has no end to shorten, so only the line moves - as everywhere else.
        {
            Entity c;
            c.id = QStringLiteral("c");
            c.type = EntityType::Circle;
            c.center = {0, 0};
            c.radius = 80.0;
            Entity line;
            line.id = QStringLiteral("l");
            line.type = EntityType::Line;
            line.a = {100, 200};   // 20 clear of the ring, so a 30 fillet can touch both
            line.b = {100, -200};
            const Entity before = c;
            const CornerResult r =
                filletLineCurve(line, c, 30.0, Vec2{100.0, 60.0}, Vec2{78.0, 20.0});
            check(r.ok && r.hasConnector, "fillet to a curve: a line and a circle can be filleted");
            check(near(c.radius, before.radius) && near(c.center.x, before.center.x),
                  "fillet to a curve: a whole circle is left as it was");
            if (r.ok)
                check(std::abs(distance(r.connector.center, c.center) - (c.radius + 30.0)) < 1e-4 ||
                          std::abs(distance(r.connector.center, c.center) - std::abs(c.radius - 30.0)) <
                              1e-4,
                      "fillet to a curve: with the fillet tangent to the ring, inside it or outside it");
        }

        // 3) A radius that cannot reach across the gap is refused rather than half-applied: a line 120
        // clear of a ring cannot be joined to it by an arc of 30.
        {
            Entity c;
            c.type = EntityType::Circle;
            c.center = {0, 0};
            c.radius = 80.0;
            Entity line;
            line.type = EntityType::Line;
            line.a = {200, 200};
            line.b = {200, -200};
            const Entity lineBefore = line;
            const CornerResult r =
                filletLineCurve(line, c, 30.0, Vec2{200.0, 40.0}, Vec2{75.0, 25.0});
            check(!r.ok && r.tooBig, "fillet to a curve: a radius that cannot reach both is refused");
            check(near(line.a.y, lineBefore.a.y) && near(line.b.y, lineBefore.b.y),
                  "fillet to a curve: and nothing is moved when it is");
        }

        // 4) Radius 0 is not a fillet but "bring these together": both to the crossing, nothing added.
        {
            Entity el = halfEllipse();
            Entity line;
            line.id = QStringLiteral("l");
            line.type = EntityType::Line;
            line.a = {30, 300};
            line.b = {30, 40};   // stops short of the ellipse, which it would cross at y = 80
            const CornerResult r = filletLineCurve(line, el, 0.0, Vec2{30.0, 280.0}, Vec2{45.0, 60.0});
            check(r.ok && !r.hasConnector,
                  "fillet to a curve: radius 0 brings them together and adds nothing");
            if (r.ok) {
                const double onEllipse =
                    std::hypot(line.b.x / el.radius, line.b.y / el.radiusY);
                check(std::abs(onEllipse - 1.0) < 1e-6,
                      "fillet to a curve: with the line's end taken exactly onto the curve");
            }
        }
    }

    // --- The crossing at an arc's OWN END must count, whichever side of it the rounding falls.
    {
        // Trim an ellipse with a line and the half left behind ENDS on that line: trimming the line by
        // it then crosses it exactly at both of its ends, and the angle of such a crossing carries a
        // rounding error whose sign used to decide whether it counted at all. A hair one way and the
        // crossing was dropped, so the trim ran past that end to the next boundary along - the corner of
        // the run - and took the whole of it.
        for (const double nudge : {0.0, 1e-12, -1e-12, 1e-9, -1e-9, 1e-8, -1e-8}) {
            Document d;
            Entity el;
            el.id = QStringLiteral("el");
            el.type = EntityType::Ellipse;
            el.center = {0, 0};
            el.radius = 60.0;
            el.radiusY = 100.0;
            el.meta.insert("ellipticalArc", true);
            el.startAngle = -M_PI / 2.0 + nudge;
            el.endAngle = M_PI / 2.0 - nudge;
            d.entities().push_back(el);
            Entity run;                     // an L, so an overrun shows up as "down to the corner"
            run.id = QStringLiteral("run");
            run.type = EntityType::Polyline;
            run.points = {{0, 300}, {0, -300}, {400, -300}};
            d.entities().push_back(run);
            Editor ed;
            ed.setDocument(&d);
            ed.setTool(ToolId::Trim);
            ed.onPointerPress({60, 0}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerPress({-25, 30}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerPress({25, 30}, Qt::LeftButton, Qt::NoModifier);
            bool lowerKept = false, cornerKept = false;
            for (const auto& e : d.entities()) {
                if (e.type != EntityType::Polyline) continue;
                for (const Vec2& v : e.points) {
                    if (near(v.x, 0.0) && v.y < -150.0) lowerKept = true;
                    if (near(v.x, 400.0)) cornerKept = true;
                }
            }
            check(lowerKept && cornerKept,
                  "trim at an arc's end: the crossing counts however the rounding falls, so the trim "
                  "stops there instead of running on to the corner");
        }
    }

    // --- A closed shape can be trimmed too: what is left is the open run it has become.
    {
        Document d;
        Entity r;
        r.id = QStringLiteral("box");
        r.type = EntityType::Rect;
        r.points = {{0, 0}, {100, 0}, {100, 60}, {0, 60}};
        d.entities().push_back(r);
        Entity l;
        l.id = QStringLiteral("ref");
        l.type = EntityType::Line;
        l.a = {-20, 30};
        l.b = {120, 30};      // straight through the box: it crosses both sides
        d.entities().push_back(l);
        Editor ed;
        ed.setDocument(&d);
        ed.setTool(ToolId::Trim);
        ed.onPointerPress({-10, 30}, Qt::LeftButton, Qt::NoModifier);   // the line: reference
        ed.onPointerPress({110, 10}, Qt::LeftButton, Qt::NoModifier);   // a cut line over the box's
        ed.onPointerPress({90, 10}, Qt::LeftButton, Qt::NoModifier);    // bottom right side
        const Entity* box = nullptr;
        for (const auto& e : d.entities())
            if (e.id == QLatin1String("box")) box = &e;
        check(box != nullptr, "trim a box: it is trimmed, not deleted");
        if (box) {
            check(box->type == EntityType::Polyline,
                  "trim a box: what is left is an open run, so it is a polyline now");
            bool below = false;
            for (const Vec2& v : box->points)
                if (v.y < 30.0 - 1e-6) below = true;
            check(box->points.size() >= 2, "trim a box: with the rest of its edges kept");
            check(!below || box->points.size() < 6,
                  "trim a box: and the piece the cut line crossed is gone");
        }
    }

    // --- R turns whatever is held under the cursor before it is dropped.
    {
        Document d;
        Editor ed;
        ed.setDocument(&d);
        // A part held under the cursor: a line pointing along +x from the point it is held by.
        Entity arm;
        arm.type = EntityType::Line;
        arm.layer = QStringLiteral("0");
        arm.a = {0, 0};
        arm.b = {10, 0};
        ed.placeEntities({arm});
        check(ed.placementPreviewActive(),
              "turn while placing: something held under the cursor is a placement");
        check(near(ed.placementPreviewRotation(), 0.0),
              "turn while placing: it starts the way it was drawn");

        ed.rotatePlacementPreview(true);   // R
        check(near(ed.placementPreviewRotation(), -M_PI / 2.0),
              "turn while placing: R turns it a quarter turn clockwise");
        ed.rotatePlacementPreview(false);  // Shift+R
        ed.rotatePlacementPreview(false);
        check(near(ed.placementPreviewRotation(), M_PI / 2.0),
              "turn while placing: Shift+R turns it back the other way");

        // Dropped: what lands is turned the way it was held. Held by (0,0), turned a quarter turn
        // anticlockwise, dropped at (50,50): the arm now points along +y from there.
        ed.onPointerMove({50, 50});
        ed.onPointerPress({50, 50}, Qt::LeftButton, Qt::NoModifier);
        const Entity* placed = nullptr;
        for (const auto& e : d.entities())
            if (e.type == EntityType::Line) placed = &e;
        check(placed != nullptr, "turn while placing: a click drops it");
        if (placed)
            check(near(placed->a.x, 50.0) && near(placed->a.y, 50.0) && near(placed->b.x, 50.0) &&
                      near(placed->b.y, 60.0),
                  "turn while placing: and it lands turned the way it was held, about the point it "
                  "was held by");
        check(near(ed.placementPreviewRotation(), 0.0),
              "turn while placing: the turn is forgotten once it is dropped");
        check(!ed.placementPreviewActive(),
              "turn while placing: and there is nothing held any more");
    }

    // How far an object REACHES ON THE SHEET, which is the question the canvas has to ask before it
    // decides an object is off screen and skips it. A double run's lines are gap/2 either side of its
    // path, so the path can be off screen while both lines fill it - that is what made lines vanish as
    // the view zoomed in, because the margin the canvas allowed shrank with the zoom while the gap did
    // not.
    {
        Document d; d.entities().clear();
        Entity run;
        run.id = QStringLiteral("run");
        run.type = EntityType::Polyline;
        run.points = {{0, 100}, {200, 100}};
        run.meta.insert(QStringLiteral("double"), true);
        run.meta.insert(QStringLiteral("gap"), 10.0);
        const Bounds path = run.bounds();
        check(near(path.min.y, 100.0) && near(path.max.y, 100.0),
              "drawn bounds: the geometry of a double run is its centreline, flat");
        const Bounds drawn = drawnBounds(run);
        check(drawn.min.y <= 95.0 + 1e-9 && drawn.max.y >= 105.0 - 1e-9,
              "drawn bounds: what is DRAWN reaches both lines, gap/2 either side");
        // The canvas's own test, zoomed in on the top line: 1200x800 px at 256 px per unit is under
        // 5 units across, so the centreline is well outside the view and the line fills it.
        auto offScreen = [](const Bounds& b, const Vec2& at, double zoom) {
            double vMinX = at.x - 600.0 / zoom, vMaxX = at.x + 600.0 / zoom;
            double vMinY = at.y - 400.0 / zoom, vMaxY = at.y + 400.0 / zoom;
            const double mx = (vMaxX - vMinX) * 0.05 + 2.0, my = (vMaxY - vMinY) * 0.05 + 2.0;
            vMinX -= mx; vMaxX += mx; vMinY -= my; vMaxY += my;
            return b.valid && (b.max.x < vMinX || b.min.x > vMaxX || b.max.y < vMinY ||
                               b.min.y > vMaxY);
        };
        const Vec2 onTheLine{100, 105};
        check(offScreen(path, onTheLine, 256.0),
              "drawn bounds: by its geometry the run counts as off screen at that zoom");
        check(!offScreen(drawn, onTheLine, 256.0),
              "drawn bounds: by what is drawn it does not - the line is the whole view");
    }

    // A cap that stretched a line out to meet something reaches past the end of the run's own points,
    // and no fixed pad would contain that - it is as far as the thing it was extended to.
    {
        Document d; d.entities().clear();
        Entity run;
        run.id = QStringLiteral("run");
        run.type = EntityType::DoubleLine;
        run.a = {0, 0};
        run.b = {100, 0};
        run.meta.insert(QStringLiteral("gap"), 10.0);
        check(near(drawnBounds(run).max.x, 100.0),
              "drawn bounds: with no cap the run reaches its own end");
        run.meta.insert(QStringLiteral("cutEnd"), true);
        run.meta.insert(QStringLiteral("cutEndKind"), QStringLiteral("line"));
        run.meta.insert(QStringLiteral("cutEndAx"), 300.0);   // a wall 200 further on
        run.meta.insert(QStringLiteral("cutEndAy"), -50.0);
        run.meta.insert(QStringLiteral("cutEndBx"), 300.0);
        run.meta.insert(QStringLiteral("cutEndBy"), 50.0);
        run.meta.insert(QStringLiteral("cutEndSide"), 1);
        check(drawnBounds(run).max.x >= 300.0 - 1e-9,
              "drawn bounds: a line extended to meet something reaches all the way to it");
    }

    // Two double runs crossing, and the piece of one line that runs THROUGH the other run's gap taken
    // out - on BOTH lines, at the same joint. This is the everyday trim on a harness drawing: it is how
    // a run that passes behind another is shown. A trimmed END cannot say a piece out of the middle of a
    // line, which is why the second line "did not trim from the joint": its piece was always a middle
    // one. The line keeps it as a hole instead.
    {
        auto makeRun = [](const QString& id, std::vector<Vec2> pts, double gap) {
            Entity e;
            e.id = id;
            e.type = EntityType::Polyline;
            e.points = std::move(pts);
            e.meta.insert(QStringLiteral("double"), true);
            e.meta.insert(QStringLiteral("gap"), gap);
            return e;
        };
        // how much of one physical line is drawn, holes taken off
        auto drawn = [](const Entity& e, int side) {
            const double gap = e.meta.value(QStringLiteral("gap")).toDouble(10.0);
            std::vector<Vec2> rail = offsetPolyline(e.points, side * gap / 2.0);
            applyDoubleRunCuts(rail, e, side);
            double total = 0.0;
            for (const auto& piece : splitRailAtHoles(rail, e, side)) total += polylineLength(piece);
            return total;
        };
        Document d; d.entities().clear();
        d.entities().push_back(makeRun(QStringLiteral("A"), {{0, 0}, {200, 0}}, 20.0));
        d.entities().push_back(makeRun(QStringLiteral("B"), {{100, -50}, {100, 50}}, 30.0));
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(4);
        auto runA = [&]() -> const Entity& {
            for (const Entity& e : d.entities())
                if (e.id == QLatin1String("A")) return e;
            return d.entities().front();
        };
        auto trimAt = [&](const Vec2& at) {
            ed.setTool(ToolId::Select);
            ed.clearSelection();
            ed.selectById(QStringLiteral("B"));
            ed.setTool(ToolId::Trim);
            ed.onPointerMove(at);
            ed.onPointerPress(at, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerPress(at, Qt::LeftButton, Qt::NoModifier);
        };
        check(near(drawn(runA(), +1), 200.0) && near(drawn(runA(), -1), 200.0),
              "trim-joint: both lines run the whole way to start with");
        trimAt({100, 10});                       // the top line, inside B's gap
        check(near(drawn(runA(), +1), 170.0),
              "trim-joint: the piece of the top line inside the joint comes out");
        check(near(drawn(runA(), -1), 200.0),
              "trim-joint: and the other line is untouched by it");
        trimAt({100, -10});                      // the bottom line, the SAME joint
        check(near(drawn(runA(), -1), 170.0),
              "trim-joint: the second line trims at the same joint too");
        check(near(drawn(runA(), +1), 170.0),
              "trim-joint: with the first line left as it was");
        // And what is gone is gone: clicking in the hole says so and changes nothing.
        QString said;
        QObject::connect(&ed, &Editor::message, [&said](const QString& m) { said = m; });
        trimAt({100, 10});
        check(near(drawn(runA(), +1), 170.0), "trim-joint: clicking the hole again changes nothing");
        // Nor is there anything there to click: the line is not drawn in the hole, so the pointer is
        // over empty sheet and the answer says so rather than blaming a cut line that was never drawn.
        check(said.contains(QStringLiteral("nothing under the pointer")),
              "trim-joint: and a click in the hole says there is nothing there");
    }

    // A click trims the line UNDER THE POINTER or explains itself - it never trims something else that
    // happens to be nearby. This used to report "trimmed one line of a double run" while quietly cutting
    // a different run a few millimetres away, which is the worst of both: the drawing changed somewhere
    // the person was not looking, and the line they clicked did not.
    {
        Document d; d.entities().clear();
        Entity lone;                               // nothing crosses this line at all
        lone.id = QStringLiteral("lone");
        lone.type = EntityType::Line;
        lone.a = {0, 0};
        lone.b = {60, 0};
        d.entities().push_back(lone);
        Entity other;                              // ... and this one is crossed, close by
        other.id = QStringLiteral("other");
        other.type = EntityType::Line;
        other.a = {0, 8};
        other.b = {60, 8};
        d.entities().push_back(other);
        Entity edge;
        edge.id = QStringLiteral("edge");
        edge.type = EntityType::Line;
        edge.a = {30, 4};
        edge.b = {30, 30};                         // crosses `other`, not `lone`
        d.entities().push_back(edge);
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(4);
        ed.selectById(QStringLiteral("edge"));
        ed.setTool(ToolId::Trim);
        QString said;
        QObject::connect(&ed, &Editor::message, [&said](const QString& m) { said = m; });
        ed.onPointerMove({20, 0});
        ed.onPointerPress({20, 0}, Qt::LeftButton, Qt::NoModifier);   // on `lone`
        ed.onPointerPress({20, 0}, Qt::LeftButton, Qt::NoModifier);
        const Entity* o = nullptr;
        for (const Entity& e : d.entities())
            if (e.id == QLatin1String("other")) o = &e;
        check(o && near(o->a.x, 0.0) && near(o->b.x, 60.0),
              "trim-owner: a click that cannot trim what it is on leaves everything else alone");
        check(said.contains(QStringLiteral("Nothing crosses")),
              "trim-owner: and says why the line under the pointer could not be trimmed");
    }

    // A dimension is saved whole. Where its line stands off the geometry is a third point, an angular
    // one is three points, a radius or diameter one is a centre and a radius: none of that used to be
    // written, so a dimension placed and saved came back flattened onto what it measured.
    {
        auto dim = [](const QString& id, const QString& type) {
            Entity e;
            e.id = id;
            e.type = EntityType::Dimension;
            e.meta.insert(QStringLiteral("dimType"), type);
            e.meta.insert(QStringLiteral("textHeight"), 5.0);
            return e;
        };
        Document d; d.entities().clear();
        {   Entity e = dim(QStringLiteral("lin"), QStringLiteral("linear"));
            e.a = {40, 20}; e.b = {160, 20};
            e.points = {{40, 20}, {160, 20}, {100, 50}};   // the stand-off
            d.entities().push_back(e); }
        {   Entity e = dim(QStringLiteral("ang"), QStringLiteral("angular"));
            e.points = {{100, 40}, {160, 40}, {140, 90}};
            d.entities().push_back(e); }
        {   Entity e = dim(QStringLiteral("rad"), QStringLiteral("radius"));
            e.center = {100, 40}; e.radius = 35.0; e.b = {150, 75};
            e.points = {{150, 75}};
            d.entities().push_back(e); }
        // through the file format and back
        Document d2;
        dwx::fromJson(d2, dwx::toJson(d));
        check(d2.entities().size() == 3, "dim-save: every dimension came back");
        for (const Entity& was : d.entities()) {
            const Entity* now = nullptr;
            for (const Entity& e : d2.entities())
                if (e.id == was.id) now = &e;
            if (!now) { check(false, "dim-save: a dimension went missing"); continue; }
            const QString ty = was.meta.value(QStringLiteral("dimType")).toString();
            bool same = now->points.size() == was.points.size() &&
                        near(now->center.x, was.center.x) && near(now->center.y, was.center.y) &&
                        near(now->radius, was.radius);
            for (size_t i = 0; i < was.points.size() && same; ++i)
                same = near(now->points[i].x, was.points[i].x) &&
                       near(now->points[i].y, was.points[i].y);
            check(same, ty == QLatin1String("linear")
                            ? "dim-save: a linear dimension keeps the line it was drawn on"
                            : ty == QLatin1String("angular")
                                  ? "dim-save: an angular dimension keeps its three points"
                                  : "dim-save: a radius dimension keeps its centre and radius");
        }
    }

    // The text override: what the dimension SAYS, when that is not what it measures. "<>" stands in
    // for the measurement, and emptying the field puts the measurement back.
    {
        Document d; d.entities().clear();
        Entity e;
        e.id = QStringLiteral("dim");
        e.type = EntityType::Dimension;
        e.meta.insert(QStringLiteral("dimType"), QStringLiteral("linear"));
        e.meta.insert(QStringLiteral("decimals"), 1);
        e.a = {40, 20};
        e.b = {160, 20};
        e.points = {{40, 20}, {160, 20}, {100, 50}};   // stands off above, so it measures across x
        d.entities().push_back(e);
        auto says = [&d]() {
            return EntityRenderer::dimensionText(d.entities().front());
        };
        check(says() == QStringLiteral("120.0"), "dim-override: with none, it says what it measures");
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.selectById(QStringLiteral("dim"));
        ed.setSelectionDimMeta(QStringLiteral("textOverride"), QStringLiteral("REF"));
        check(says() == QStringLiteral("REF"), "dim-override: an override says that instead");
        ed.setSelectionDimMeta(QStringLiteral("textOverride"), QStringLiteral("<>"));
        check(says() == QStringLiteral("120.0"), "dim-override: <> on its own is the measurement");
        ed.setSelectionDimMeta(QStringLiteral("textOverride"), QStringLiteral("2x <> REF"));
        check(says() == QStringLiteral("2x 120.0 REF"),
              "dim-override: the measurement can be said inside your own text");
        // The measurement itself is untouched by any of it - the properties panel still reads the
        // geometry, and moving the object still re-measures.
        check(near(std::hypot(d.entities().front().b.x - d.entities().front().a.x,
                              d.entities().front().b.y - d.entities().front().a.y), 120.0),
              "dim-override: the geometry is not changed by what the dimension says");
        ed.setSelectionDimMeta(QStringLiteral("textOverride"), QString());
        check(!d.entities().front().meta.contains(QStringLiteral("textOverride")),
              "dim-override: emptying the field takes the override off the object");
        check(says() == QStringLiteral("120.0"), "dim-override: and the measurement is back");
        // A radius dimension says R and a diameter one says the diameter symbol, override or not.
        Entity r;
        r.id = QStringLiteral("rad");
        r.type = EntityType::Dimension;
        r.meta.insert(QStringLiteral("dimType"), QStringLiteral("radius"));
        r.meta.insert(QStringLiteral("decimals"), 1);
        r.center = {0, 0};
        r.radius = 12.5;
        check(EntityRenderer::dimensionText(r) == QStringLiteral("R12.5"),
              "dim-override: a radius dimension says R and its radius");
        r.meta.insert(QStringLiteral("textOverride"), QStringLiteral("<> TYP"));
        check(EntityRenderer::dimensionText(r) == QStringLiteral("R12.5 TYP"),
              "dim-override: and an override can add to it");
    }

    // A dimension is moved by its joints - and the ones that matter are at the ARROW ENDS, on the far
    // side of the extension lines from the geometry, because those are what carry the dimension line off
    // the object. Every kind of dimension has to have something to take hold of.
    {
        auto dim = [](const QString& type) {
            Entity e;
            e.id = QStringLiteral("d");
            e.type = EntityType::Dimension;
            e.meta.insert(QStringLiteral("dimType"), type);
            e.meta.insert(QStringLiteral("textHeight"), 5.0);
            return e;
        };
        // press on the joint, drag, let go, and settle it
        auto dragJoint = [](Editor& ed, const Vec2& from, const Vec2& to) {
            ed.setTool(ToolId::Select);
            ed.onPointerMove(from);
            ed.onPointerPress(from, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerMove(to);
            ed.onPointerRelease(to, Qt::LeftButton);
            ed.onPointerMove(to);
            ed.onPointerPress(to, Qt::LeftButton, Qt::NoModifier);
        };

        {   // a horizontal measurement standing 30 off above: its extension lines are vertical and the
            // arrow ends are at the top of them
            Document d; d.entities().clear();
            Entity e = dim(QStringLiteral("linear"));
            e.a = {40, 20}; e.b = {160, 20};
            e.points = {{40, 20}, {160, 20}, {100, 50}};
            d.entities().push_back(e);
            Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(6);
            ed.selectById(QStringLiteral("d"));
            dragJoint(ed, {40, 50}, {40, 85});     // the arrow end at the top of the left ext line
            const Entity& now = d.entities().front();
            check(now.points.size() >= 3 && near(now.points[2].y, 85.0),
                  "dim-joints: the joint at the arrow end carries the dimension line off");
            check(near(now.a.x, 40.0) && near(now.a.y, 20.0) && near(now.b.x, 160.0) &&
                      near(now.b.y, 20.0),
                  "dim-joints: and what it measures between is untouched by that");
        }
        {   // a dimension out of a file that saved only a and b has no stand-off point at all; taking
            // hold of an arrow end must still work, and gives it one
            Document d; d.entities().clear();
            Entity e = dim(QStringLiteral("linear"));
            e.a = {40, 20};
            e.b = {160, 20};                       // no points
            d.entities().push_back(e);
            Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(6);
            ed.selectById(QStringLiteral("d"));
            dragJoint(ed, {40, 20}, {40, 60});
            const Entity& now = d.entities().front();
            check(now.points.size() >= 3 && near(now.points[2].y, 60.0),
                  "dim-joints: one saved without a stand-off point gets one from the first drag");
        }
        {   // an angular dimension never sets a or b: its joints are its own three points
            Document d; d.entities().clear();
            Entity e = dim(QStringLiteral("angular"));
            e.points = {{100, 40}, {160, 40}, {140, 90}};
            d.entities().push_back(e);
            Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(6);
            ed.selectById(QStringLiteral("d"));
            dragJoint(ed, {160, 40}, {185, 25});
            const Entity& now = d.entities().front();
            check(now.points.size() >= 2 && near(now.points[1].x, 185.0) &&
                      near(now.points[1].y, 25.0),
                  "dim-joints: an angular dimension is held by the points its rays run to");
        }
        {   // a radius dimension: the leader end is what there is to move, and the file keeps it in
            // points as well, so the two must not drift apart
            Document d; d.entities().clear();
            Entity e = dim(QStringLiteral("radius"));
            e.center = {100, 40};
            e.radius = 35.0;
            e.b = {150, 75};
            e.points = {{150, 75}};
            d.entities().push_back(e);
            Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(6);
            ed.selectById(QStringLiteral("d"));
            dragJoint(ed, {150, 75}, {175, 95});
            const Entity& now = d.entities().front();
            check(near(now.b.x, 175.0) && near(now.b.y, 95.0),
                  "dim-joints: a radius dimension's leader end moves");
            check(now.points.size() == 1 && near(now.points[0].x, 175.0) &&
                      near(now.points[0].y, 95.0),
                  "dim-joints: and what the file keeps for it moves with it");
            check(near(now.center.x, 100.0) && near(now.radius, 35.0),
                  "dim-joints: the ring it measures is not moved by dragging the leader");
        }
    }

    // What a dimension reports as its extent has to cover what it draws, or it is left out of Zoom All
    // and the canvas is entitled to cull it while part of it is on screen.
    {
        auto dim = [](const QString& type) {
            Entity e;
            e.id = QStringLiteral("d");
            e.type = EntityType::Dimension;
            e.meta.insert(QStringLiteral("dimType"), type);
            e.meta.insert(QStringLiteral("textHeight"), 5.0);
            return e;
        };
        {   Entity e = dim(QStringLiteral("radius"));
            e.center = {100, 40};
            e.radius = 35.0;
            e.b = {150, 75};
            e.points = {{150, 75}};
            const Bounds b = e.bounds();
            check(b.valid && b.min.x <= 100.0 && b.min.y <= 40.0,
                  "dim-bounds: a radius dimension reaches back to the centre its leader starts at");
        }
        {   Entity e = dim(QStringLiteral("ordinate"));
            e.a = {70, 30};
            e.b = {70, 80};
            const Bounds b = e.bounds();
            check(b.valid && b.max.y >= 80.0 - 1e-9,
                  "dim-bounds: an ordinate dimension reaches the end of its leader");
        }
        {   Entity e = dim(QStringLiteral("angular"));
            e.points = {{100, 40}, {160, 40}, {140, 90}};
            const Bounds b = e.bounds();
            check(b.valid && b.min.x >= 99.0 && b.max.x <= 161.0 && b.min.y >= 39.0 &&
                      b.max.y <= 91.0,
                  "dim-bounds: an angular dimension is its three points, and does not reach the origin");
        }
        {   // and the drawn extent covers the dimension LINE, which on an aligned one runs past the
            // stand-off point it was placed with
            Entity e = dim(QStringLiteral("aligned"));
            e.a = {40, 20};
            e.b = {150, 60};
            e.points = {{40, 20}, {150, 60}, {80, 70}};
            const Vec2 n = (e.points[1] - e.points[0]).normalized().perp();
            const double tt = dot(e.points[2] - e.points[0], n);
            const Vec2 far = e.points[1] + n * tt;
            const Bounds b = drawnBounds(e);
            check(b.valid && b.max.y >= far.y && b.max.x >= far.x,
                  "dim-bounds: what is painted includes the far end of the dimension line");
        }
    }

    // The joint by the arrow carries the dimension line off the geometry THE WAY THE DIMENSION STANDS
    // OFF, and no other way. A length stays a length: dragging that joint sideways used to swing the
    // dimension line round past the middle and leave the object measuring its height instead, which is
    // not something anyone asks for by dragging an arrow.
    {
        auto dim = [](const QString& type, Vec2 a, Vec2 b, Vec2 off) {
            Entity e;
            e.id = QStringLiteral("d");
            e.type = EntityType::Dimension;
            e.meta.insert(QStringLiteral("dimType"), type);
            e.meta.insert(QStringLiteral("textHeight"), 5.0);
            e.meta.insert(QStringLiteral("decimals"), 1);
            e.a = a;
            e.b = b;
            e.points = {a, b, off};
            return e;
        };
        auto dragJoint = [](Editor& ed, const Vec2& from, const Vec2& to) {
            ed.setTool(ToolId::Select);
            ed.onPointerMove(from);
            ed.onPointerPress(from, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerMove(to);
            ed.onPointerRelease(to, Qt::LeftButton);
            ed.onPointerMove(to);
            ed.onPointerPress(to, Qt::LeftButton, Qt::NoModifier);
        };

        {   // a length: measured across x, standing off above, arrow ends at y = 50
            Document d; d.entities().clear();
            d.entities().push_back(dim(QStringLiteral("linear"), {40, 20}, {160, 20}, {100, 50}));
            Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(6);
            ed.selectById(QStringLiteral("d"));
            const QString said = EntityRenderer::dimensionText(d.entities().front());
            check(said == QStringLiteral("120.0"), "dim-standoff: it measures the length to start with");
            Vec2 d1, d2;

            dragJoint(ed, {40, 50}, {40, 90});         // straight out: the way it stands off
            dimensionLineEnds(d.entities().front(), d1, d2);
            check(near(d1.y, 90.0) && near(d2.y, 90.0),
                  "dim-standoff: dragged outwards, the whole dimension line goes with it");
            check(EntityRenderer::dimensionText(d.entities().front()) == said,
                  "dim-standoff: measuring the same length");

            dragJoint(ed, {40, 90}, {300, 90});        // sideways, well past the middle
            dimensionLineEnds(d.entities().front(), d1, d2);
            check(near(d1.y, 90.0) && near(d2.y, 90.0) && near(d1.x, 40.0) && near(d2.x, 160.0),
                  "dim-standoff: dragged sideways, nothing moves");
            check(EntityRenderer::dimensionText(d.entities().front()) == said,
                  "dim-standoff: and it is still the length that is measured, not the height");

            dragJoint(ed, {40, 90}, {40, -30});        // out the other side of the object
            dimensionLineEnds(d.entities().front(), d1, d2);
            check(near(d1.y, -30.0), "dim-standoff: it may stand off on the other side");
            check(EntityRenderer::dimensionText(d.entities().front()) == said,
                  "dim-standoff: measuring the same length there too");
        }
        {   // a height: measured up y, standing off to the right
            Document d; d.entities().clear();
            d.entities().push_back(dim(QStringLiteral("linear"), {40, 20}, {40, 120}, {90, 70}));
            Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(6);
            ed.selectById(QStringLiteral("d"));
            const QString said = EntityRenderer::dimensionText(d.entities().front());
            check(said == QStringLiteral("100.0"), "dim-standoff: a height measures up y");
            Vec2 d1, d2;
            dragJoint(ed, {90, 20}, {130, 300});   // outwards AND a long way along at the same time
            dimensionLineEnds(d.entities().front(), d1, d2);
            check(near(d1.x, 130.0) && near(d2.x, 130.0) && near(d1.y, 20.0) && near(d2.y, 120.0),
                  "dim-standoff: only the standing-off part of the drag is taken");
            check(EntityRenderer::dimensionText(d.entities().front()) == said,
                  "dim-standoff: the height is still the height");
        }
        {   // an aligned one stands off along the perpendicular to what it measures
            Document d; d.entities().clear();
            d.entities().push_back(dim(QStringLiteral("aligned"), {40, 20}, {150, 60}, {80, 70}));
            Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(6);
            ed.selectById(QStringLiteral("d"));
            const QString said = EntityRenderer::dimensionText(d.entities().front());
            Vec2 d1, d2;
            dimensionLineEnds(d.entities().front(), d1, d2);
            const Vec2 dir = (Vec2{150, 60} - Vec2{40, 20}).normalized();
            dragJoint(ed, d1, d1 + dir * 60.0);            // along the line
            Vec2 a1, a2;
            dimensionLineEnds(d.entities().front(), a1, a2);
            check(near(a1.x, d1.x) && near(a1.y, d1.y),
                  "dim-standoff: an aligned one does not slide along its own line");
            dragJoint(ed, a1, a1 + dir.perp() * 25.0);     // across it
            Vec2 b1, b2;
            dimensionLineEnds(d.entities().front(), b1, b2);
            check(near(distance(b1, a1), 25.0),
                  "dim-standoff: across the line it stands off by just that much");
            check(EntityRenderer::dimensionText(d.entities().front()) == said,
                  "dim-standoff: and measures the same either way");
        }
    }

    // A dimension does not turn from vertical to horizontal while it is being handled. Which axis it
    // measures along is the object's own, written down when it is placed, and nothing about dragging it
    // changes it - not the joint by the arrow, not the points it measures between, not the text.
    {
        auto dragJoint = [](Editor& ed, const Vec2& from, const Vec2& to) {
            ed.setTool(ToolId::Select);
            ed.onPointerMove(from);
            ed.onPointerPress(from, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerMove(to);
            ed.onPointerRelease(to, Qt::LeftButton);
            ed.onPointerMove(to);
            ed.onPointerPress(to, Qt::LeftButton, Qt::NoModifier);
        };
        // a HEIGHT placed with the tool: up the y axis, standing off to the right
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(6);
        ed.setTool(ToolId::Dimension);
        ed.setDimMode(Editor::DimMode::Linear);
        for (const Vec2& p : {Vec2{40, 20}, Vec2{40, 120}, Vec2{90, 70}}) {
            ed.onPointerMove(p);
            ed.onPointerPress(p, Qt::LeftButton, Qt::NoModifier);
        }
        check(d.entities().size() == 1, "dim-axis: the tool placed a dimension");
        if (d.entities().size() == 1) {
            const Entity& e = d.entities().front();
            check(e.meta.contains(QStringLiteral("dimAcross")),
                  "dim-axis: a placed dimension writes down which way it measures");
            check(!dimensionAcrossX(e), "dim-axis: and this one is a height, not a length");

            // the pull that used to turn it round: one measured point dragged past the other
            ed.setTool(ToolId::Select);
            ed.clearSelection();
            ed.selectById(e.id);
            dragJoint(ed, {40, 20}, {40, 200});
            const Entity& now = d.entities().front();
            check(!dimensionAcrossX(now),
                  "dim-axis: dragging a measured point past the other leaves it a height");
            check(EntityRenderer::dimensionText(now).startsWith(QStringLiteral("80")),
                  "dim-axis: and it measures the height it now spans");
        }
    }

    // An older drawing never wrote the axis down. Reading one works it out and writes it on, so those
    // dimensions are as steady as newly placed ones from then on.
    {
        Document d; d.entities().clear();
        Entity e;
        e.id = QStringLiteral("d");
        e.type = EntityType::Dimension;
        e.meta.insert(QStringLiteral("dimType"), QStringLiteral("linear"));
        e.meta.insert(QStringLiteral("decimals"), 1);
        e.a = {40, 20};
        e.b = {40, 120};
        e.points = {{40, 20}, {40, 120}, {90, 70}};   // a height, standing off to the right
        d.entities().push_back(e);
        QJsonObject obj = dwx::toJson(d);
        QJsonArray ents = obj.value(QStringLiteral("entities")).toArray();
        QJsonObject first = ents[0].toObject();
        first.remove(QStringLiteral("dimAcross"));     // the way an older file has it
        ents[0] = first;
        obj.insert(QStringLiteral("entities"), ents);
        Document read;
        dwx::fromJson(read, obj);
        check(read.entities().size() == 1 &&
                  read.entities().front().meta.contains(QStringLiteral("dimAcross")),
              "dim-axis: reading an older drawing writes the axis down for its dimensions");
        check(!read.entities().empty() && !dimensionAcrossX(read.entities().front()),
              "dim-axis: and works out that this one was placed as a height");
    }

    // Changed on purpose, in Properties - and turned with the object when the object is turned.
    {
        Document d; d.entities().clear();
        Entity e;
        e.id = QStringLiteral("d");
        e.type = EntityType::Dimension;
        e.meta.insert(QStringLiteral("dimType"), QStringLiteral("linear"));
        e.meta.insert(QStringLiteral("decimals"), 1);
        e.a = {40, 20};
        e.b = {40, 120};
        e.points = {{40, 20}, {40, 120}, {90, 70}};
        e.meta.insert(QStringLiteral("dimAcross"), false);
        d.entities().push_back(e);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.selectById(QStringLiteral("d"));
        ed.setSelectionDimMeta(QStringLiteral("dimAcross"), true);
        check(dimensionAcrossX(d.entities().front()),
              "dim-axis: Properties can turn it the other way about on purpose");

        Entity turned = d.entities().front();
        turned.meta.insert(QStringLiteral("dimAcross"), false);
        rotateEntity(turned, {40, 20}, M_PI / 2.0);
        check(dimensionAcrossX(turned),
              "dim-axis: a quarter turn turns which axis it measures along");
        Entity nudged = d.entities().front();
        nudged.meta.insert(QStringLiteral("dimAcross"), false);
        rotateEntity(nudged, {40, 20}, 0.2);
        check(!dimensionAcrossX(nudged), "dim-axis: a small turn does not");

        Entity flat = d.entities().front();
        flat.meta.insert(QStringLiteral("dimAcross"), false);
        mirrorEntity(flat, {0, 0}, {0, 100});         // mirrored across a vertical line
        check(!dimensionAcrossX(flat), "dim-axis: mirroring along an axis leaves it as it was");
        Entity diag = d.entities().front();
        diag.meta.insert(QStringLiteral("dimAcross"), false);
        mirrorEntity(diag, {0, 0}, {100, 100});       // ... and across a diagonal
        check(dimensionAcrossX(diag),
              "dim-axis: mirroring across a diagonal swaps the axes, and it follows");
    }

    std::printf(g_fail ? "  -> %d failed\n" : "  -> ok\n", g_fail);
    return g_fail;
}
