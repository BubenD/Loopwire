#include "app/Editor.h"
#include "geometry/Geometry.h"
#include "geometry/Snap.h"
#include "model/Document.h"
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

    std::printf(g_fail ? "  -> %d failed\n" : "  -> ok\n", g_fail);
    return g_fail;
}
