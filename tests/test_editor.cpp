#include "app/Editor.h"
#include "geometry/Geometry.h"
#include "model/Document.h"
#include "tools/EntityTransforms.h"
#include "tools/ModifyOps.h"

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

    // --- Fillet a right-angle corner: tangent points at distance r from corner.
    {
        Entity h; h.type = EntityType::Line; h.a = {0, 0}; h.b = {100, 0};
        Entity v; v.type = EntityType::Line; v.a = {0, 0}; v.b = {0, 100};
        // Pick points choose the far (kept) ends.
        CornerResult r = filletLines(h, v, 10.0, {100, 0}, {0, 100});
        check(r.ok, "fillet: ok");
        check(r.connector.type == EntityType::Arc && near(r.connector.radius, 10.0), "fillet: arc radius");
        check(near(r.connector.center.x, 10.0) && near(r.connector.center.y, 10.0), "fillet: arc center");
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

    // --- Trim a line at an intersection.
    {
        Entity h; h.type = EntityType::Line; h.a = {0, 0}; h.b = {100, 0};
        Entity cut; cut.type = EntityType::Line; cut.a = {40, -10}; cut.b = {40, 10};
        std::vector<Entity> others = {h, cut};
        // Trim the right portion of h (pick near x=80).
        bool ok = trimLineAt(others[0], {80, 0}, others);
        check(ok && near(others[0].b.x, 40.0), "trim: shortened to intersection");
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
        // Move whole object by dragging its body.
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
            check(near(d.entities()[0].a.y, 8.0) && near(d.entities()[0].b.y, 8.0) &&
                      near(d.entities()[0].b.x, 20.0),
                  "edit: drag body moves whole object");
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

    // --- After finishing a draw, the tool returns to Select; Space repeats it.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setTool(ToolId::Line);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({10, 0}, Qt::LeftButton, Qt::NoModifier); // finishes the line
        check(d.entities().size() == 1, "finish: line created");
        check(ed.tool() == ToolId::Select, "finish: returned to Select mode");
        ed.onKey(Qt::Key_Space, Qt::NoModifier);
        check(ed.tool() == ToolId::Line, "finish: Space repeats last tool");
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
                      near(d.entities()[0].meta.value("cornerRadius").toDouble(), 5.0),
                  "f: polyline keeps cornerRadius");
        }
        // roundedPolyline inserts arc points at the corner.
        {
            std::vector<Vec2> pts = {{0, 0}, {10, 0}, {10, 10}};
            auto r = roundedPolyline(pts, 3.0);
            check(r.size() > pts.size(), "roundedPolyline: adds fillet arc points");
        }
    }

    // --- Purge separates an object into single primitives.
    {
        // Double line -> two lines.
        {
            Document d; d.entities().clear();
            Entity dl; dl.id = "dl"; dl.type = EntityType::DoubleLine;
            dl.a = {0, 0}; dl.b = {40, 0}; dl.meta.insert("gap", 8.0);
            d.entities().push_back(dl);
            Editor ed; ed.setDocument(&d); noSnap(ed);
            ed.selectAll();
            ed.purgeSelected();
            check(d.entities().size() == 2, "purge: double line -> 2 lines");
            for (const auto& e : d.entities()) check(e.type == EntityType::Line, "purge: parts are lines");
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
            ed.purgeSelected();
            // 4 body lines + 3 pin lines = 7 lines, plus designator + 3 pin labels = texts.
            int lines = 0;
            for (const auto& e : d.entities()) if (e.type == EntityType::Line) ++lines;
            check(lines >= 7, "purge: connector -> body + pin lines");
            check(d.entities().size() > 7, "purge: connector keeps labels as text");
        }
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

    std::printf(g_fail ? "  -> %d failed\n" : "  -> ok\n", g_fail);
    return g_fail;
}
