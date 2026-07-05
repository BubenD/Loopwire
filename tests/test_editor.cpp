#include "app/Editor.h"
#include "geometry/Geometry.h"
#include "geometry/Snap.h"
#include "model/Document.h"
#include "tools/EntityTransforms.h"
#include "tools/ModifyOps.h"

#include <QJsonArray>
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

    // --- A SINGLE click on a double polyline edits the nearest knee directly (no
    //     second pick), and re-clicking with a new radius redefines it.
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
        ed.onPointerPress({60, 5}, Qt::LeftButton, Qt::NoModifier); // ONE click -> knee filleted
        const Entity* dd = nullptr;
        for (const auto& e : d.entities()) if (e.id == "dp") dd = &e;
        const bool one = dd && near(dd->meta.value("cornerRadii").toArray()[1].toDouble(), 7.0);
        ed.setFilletRadius(3.0);
        ed.onPointerPress({60, 5}, Qt::LeftButton, Qt::NoModifier); // click again -> redefine
        for (const auto& e : d.entities()) if (e.id == "dp") dd = &e;
        check(one && dd && near(dd->meta.value("cornerRadii").toArray()[1].toDouble(), 3.0),
              "fillet-dpoly-1click: single click on a double polyline edits/redefines the knee");
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
        ed.onPointerPress({12, 0}, Qt::LeftButton, Qt::NoModifier);  // grab B's centre
        ed.onPointerPress({12, 30}, Qt::LeftButton, Qt::NoModifier); // drag it upward
        const Entity *A = nullptr, *B = nullptr;
        for (const auto& e : d.entities()) { if (e.id == "A") A = &e; if (e.id == "B") B = &e; }
        check(A && B && near(distance(A->center, B->center), 12.0),
              "leader-tangent: a coupled circle stays tangent to its neighbour when moved");
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

    // --- Trim removes only up to the NEXT joint (not to the line end), and the
    //     reference stays selected for further cuts.
    {
        Document d; d.entities().clear();
        Entity ref; ref.id = "ref"; ref.type = EntityType::Line; ref.a = {50, -100}; ref.b = {50, 100};
        Entity h; h.id = "h"; h.type = EntityType::Line; h.a = {0, 0}; h.b = {100, 0};
        Entity j; j.id = "j"; j.type = EntityType::Line; j.a = {70, -10}; j.b = {70, 10}; // a joint at x=70
        d.entities().push_back(ref);
        d.entities().push_back(h);
        d.entities().push_back(j);
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(3);
        ed.selectById("ref");
        ed.setTool(ToolId::Trim);
        // Cut line at x=60 (between the reference x=50 and the joint x=70).
        ed.onPointerPress({60, -5}, Qt::LeftButton, Qt::NoModifier); // cut-line start
        ed.onPointerMove({60, 5});
        ed.onPointerPress({60, 5}, Qt::LeftButton, Qt::NoModifier);  // cut-line end -> apply
        // h now has two pieces: [0..50] and [70..100]; the [50..70] middle is removed.
        int pieces = 0; bool left = false, right = false;
        for (const auto& e : d.entities()) {
            if (e.type != EntityType::Line || !(near(e.a.y, 0.0) && near(e.b.y, 0.0))) continue;
            ++pieces;
            if (near(std::min(e.a.x, e.b.x), 0.0) && near(std::max(e.a.x, e.b.x), 50.0)) left = true;
            if (near(std::min(e.a.x, e.b.x), 70.0) && near(std::max(e.a.x, e.b.x), 100.0)) right = true;
        }
        check(pieces == 2 && left && right, "trim-joint: removed from reference (50) up to the next joint (70)");
        // The reference is still selected for further cuts (until Esc).
        check(ed.selection().size() == 1 && ed.selection().front() == "ref",
              "trim-joint: reference stays selected after trimming");
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

    // --- Polyline trim stops at the nearest knee and splits into two pieces.
    {
        std::vector<Vec2> pts = {{0, 0}, {100, 0}, {100, 100}}; // an L (knee at 100,0)
        Entity ref; ref.id = "r"; ref.type = EntityType::Line; ref.a = {50, -10}; ref.b = {50, 10};
        std::vector<Entity> cutters = {ref};
        auto pieces = trimPolylinePieces(pts, {70, 0} /* on seg0, past the ref */, cutters, "");
        check(pieces.size() == 2, "trim-poly-knee: splits into two independent pieces");
        // Piece 1 = [0..ref(50)], piece 2 = [knee(100,0)..(100,100)] — not the whole tail removed.
        check(near(pieces[0].back().x, 50.0) && near(pieces[1].front().x, 100.0) &&
                  near(pieces[1].front().y, 0.0),
              "trim-poly-knee: removed only ref..knee; the far side survives");
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

    // --- A knee INSIDE the circle bounds the trim: the removed span stops at the
    //     nearest knee, and the rest survives as a separate object.
    {
        std::vector<Vec2> pts = {{0, 0}, {150, 0}, {150, 100}}; // knee at (150,0)
        Entity circ; circ.id = "c"; circ.type = EntityType::Circle; circ.center = {100, 0}; circ.radius = 60;
        std::vector<Entity> cutters = {circ}; // ring crosses seg0 at x=40 and seg1 at (150,~33.2)
        auto pieces = trimPolylinePieces(pts, {100, 0} /* inside, before the knee */, cutters, "");
        check(pieces.size() == 2 && near(pieces[0].back().x, 40.0),
              "trim-poly-circle-knee: first piece ends at the ring entry (x=40)");
        check(pieces.size() == 2 && near(pieces[1].front().x, 150.0) &&
                  near(pieces[1].front().y, 0.0),
              "trim-poly-circle-knee: trim stops at the knee (second piece starts at the knee)");
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

    // --- Rectangular array count.
    {
        Entity c; c.type = EntityType::Circle; c.center = {0, 0}; c.radius = 1;
        auto clones = rectArray(c, 2, 3, 10, 10);
        check(clones.size() == 5, "array: 2x3 minus origin = 5");
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

    // --- Line tool draws several CONNECTED straight segments until Enter/Esc.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setTool(ToolId::Line);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);   // start
        ed.onPointerPress({10, 0}, Qt::LeftButton, Qt::NoModifier);  // commits segment 1
        ed.onPointerPress({10, 10}, Qt::LeftButton, Qt::NoModifier); // commits segment 2
        check(d.entities().size() == 2, "line-cont: each click adds a connected segment");
        check(ed.tool() == ToolId::Line && ed.isDrafting(), "line-cont: stays in Line during the run");
        check(near(d.entities()[0].b.x, 10.0) && near(d.entities()[1].a.x, 10.0) &&
                  near(d.entities()[1].a.y, 0.0),
              "line-cont: segments share the joint (connected)");
        ed.onKey(Qt::Key_Return, Qt::NoModifier); // ends the run, stays in Line
        check(ed.tool() == ToolId::Line && !ed.isDrafting(), "line-cont: Enter ends run, stays in Line");
        ed.onPointerPress({20, 20}, Qt::LeftButton, Qt::NoModifier); // start a new run
        ed.onKey(Qt::Key_Escape, Qt::NoModifier);
        check(ed.tool() == ToolId::Select, "line-cont: Esc exits to Select");
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

    std::printf(g_fail ? "  -> %d failed\n" : "  -> ok\n", g_fail);
    return g_fail;
}
