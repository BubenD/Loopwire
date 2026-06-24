#include "app/Editor.h"
#include "geometry/Geometry.h"
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
        check(near(d.entities()[0].meta.value(QStringLiteral("cornerRadius")).toDouble(), 5.0),
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
        check(d2.entities()[0].meta.value(QStringLiteral("cornerRadius")).toDouble() == 0.0,
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
                  near(e.meta.value(QStringLiteral("cornerRadius")).toDouble(), 6.0),
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
                  near(e.meta.value(QStringLiteral("cornerRadius")).toDouble(), 8.0),
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
        ed.onPointerPress({-10, 0}, Qt::LeftButton, Qt::NoModifier); // finishes
        const Entity& e = d.entities()[0];
        // Un-mirrored would be start=0,end=pi (upper). Mirrored swaps -> lower half.
        check(near(e.startAngle, M_PI) && near(e.endAngle, 0.0),
              "arc-mirror: mirrored arc takes the opposite half");
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

    // --- Double line: the centerline joint at an END moves the whole end to the
    // cursor (both lines stay centered on it). beginGripGrab must pick the NEAREST
    // grip, so the centerline end isn't shadowed by a nearby corner grip.
    {
        Document d; d.entities().clear();
        Entity dl; dl.id = "dl"; dl.type = EntityType::DoubleLine;
        dl.a = {0, 0}; dl.b = {40, 0}; dl.meta.insert("gap", 10.0);
        d.entities().push_back(dl);
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.selectAll();
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier); // centerline end A
        ed.onPointerMove({5, 10});
        ed.onPointerPress({5, 10}, Qt::LeftButton, Qt::NoModifier);
        const Entity& e = d.entities()[0];
        check(near(e.a.x, 5.0) && near(e.a.y, 10.0), "dl-midend: centerline end A follows the cursor");
        check(near(e.b.x, 40.0) && near(e.b.y, 0.0), "dl-midend: end B unchanged");
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
