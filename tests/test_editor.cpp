#include "app/Chrome.h"
#include "app/Editor.h"
#include "geometry/Geometry.h"
#include "io/Dxf.h"
#include "io/Dwx.h"
#include "geometry/Snap.h"
#include "harness/PartsLibrary.h"
#include "model/Block.h"
#include "model/Document.h"
#include "app/ribbon/RibbonIcons.h"
#include "model/PartSymbols.h"
#include "render/EntityRenderer.h"
#include "tools/EntityTransforms.h"
#include "tools/ModifyOps.h"
#include "tools/QuickSelect.h"

#include <QImage>
#include <QObject>
#include <QPointF>
#include <QJsonArray>
#include <QPainter>
#include <QtMath>
#include <algorithm>
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

    // --- TWO PIECES OF DRAWING FURNITURE: a block arrow, and an axis with the turn about it.
    //
    // Both are kept as their two CLICKED POINTS and drawn from them, not as the outline they came out
    // as. That is the difference between a symbol and a shape somebody once made: pulling an end
    // re-lays the arrow, where an outline stored as the object deforms into a blob the first time a
    // corner is dragged - the same reason a heatshrink sleeve keeps its route.
    {
        Document d;
        Editor ed;
        ed.setDocument(&d);

        ed.setTool(ToolId::VolumeArrow);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);      // the tail
        ed.onPointerPress({100, 0}, Qt::LeftButton, Qt::NoModifier);    // the tip
        check(d.entities().size() == 1, "volume arrow: two clicks place one object");
        if (!d.entities().empty()) {
            const Entity& a = d.entities().front();
            check(a.meta.value(QStringLiteral("volumeArrow")).toBool(),
                  "volume arrow: marked as one, so the renderer knows to lay it out");
            check(near(a.a.x, 0.0) && near(a.b.x, 100.0),
                  "volume arrow: it keeps the tail and the tip it was drawn between");
            const std::vector<Vec2> o = volumeArrowOutline(a.a, a.b);
            check(o.size() == 7, "volume arrow: a shaft, two barbs and a point");
            double lo = 1e18, hi = -1e18, reach = -1e18;
            for (const Vec2& v : o) {
                lo = std::min(lo, v.y);
                hi = std::max(hi, v.y);
                reach = std::max(reach, v.x);
            }
            check(near(reach, 100.0), "volume arrow: the point lands exactly on the second click");
            check(hi > 0.0 && near(hi + lo, 0.0),
                  "volume arrow: and it is symmetric about the line between them");
            // PROPORTIONED FROM ITS OWN LENGTH. Drawn to fixed millimetres a short arrow is all head
            // and a long one is a hairline with a dart on it; what a reader recognises is the shape.
            const std::vector<Vec2> shortOne = volumeArrowOutline({0, 0}, {10, 0});
            double shortHi = -1e18;
            for (const Vec2& v : shortOne) shortHi = std::max(shortHi, v.y);
            std::printf("  volume arrow: 100 long is %.1f wide, 10 long is %.1f\n", hi, shortHi);
            check(shortHi > 0.0 && shortHi < hi,
                  "volume arrow: a short one is the same shape smaller, not the same head on a stub");
        }

        ed.setTool(ToolId::AxisAngle);
        ed.onPointerPress({50, 50}, Qt::LeftButton, Qt::NoModifier);    // where the axis is
        ed.onPointerPress({50, 90}, Qt::LeftButton, Qt::NoModifier);    // straight up from it
        check(d.entities().size() == 2, "axis angle: two clicks place one object");
        if (d.entities().size() == 2) {
            const Entity& x = d.entities().back();
            check(x.meta.value(QStringLiteral("axisAngle")).toBool(),
                  "axis angle: marked as one - cross, arrow and angle are ONE object");
            check(near(x.a.x, 50.0) && near(x.a.y, 50.0),
                  "axis angle: the cross sits where the axis was clicked");
            // MEASURED IN WORLD COORDINATES. On the screen y runs the other way and every angle would
            // come out mirrored - straight up would read 270.
            double deg = std::atan2(x.b.y - x.a.y, x.b.x - x.a.x) * 180.0 / M_PI;
            if (deg < 0.0) deg += 360.0;
            std::printf("  axis angle: straight up reads %.1f degrees\n", deg);
            check(near(deg, 90.0), "axis angle: straight up is 90 degrees, not 270");
        }
    }

    // --- The chrome's ground can be chosen per scheme, and stays readable whatever is chosen.
    {
        // Chrome is global, so what was set is put back before anything else runs.
        const QColor hadDark = Chrome::chosenBack(true);
        const QColor hadLight = Chrome::chosenBack(false);
        const bool wasDark = Chrome::dark();

        Chrome::setChosenBack(true, QColor());
        Chrome::setChosenBack(false, QColor());
        Chrome::setDark(true);
        const QColor builtInDark = Chrome::back();
        Chrome::setDark(false);
        const QColor builtInLight = Chrome::back();
        check(builtInDark.isValid() && builtInLight.isValid(),
              "chrome: with nothing chosen there is still a ground");
        check(builtInDark != builtInLight,
              "chrome: and the two schemes do not share it");

        // ---- PER SCHEME. A ground chosen for dark must not follow you into light. ----
        Chrome::setChosenBack(true, QColor("#101820"));
        Chrome::setDark(false);
        check(Chrome::back() == builtInLight,
              "chrome: choosing a dark ground leaves the light scheme on its own");
        Chrome::setDark(true);
        check(Chrome::back() == QColor("#101820"), "chrome: and the dark scheme takes it");

        // ---- THE INK FOLLOWS THE GROUND, NOT THE SCHEME ----
        //
        // Somebody who sets a near-black ground while in the LIGHT scheme would otherwise get
        // near-black lettering on it and a window that cannot be read at all.
        Chrome::setChosenBack(false, QColor("#101820"));
        Chrome::setDark(false);
        check(Chrome::backInk().lightness() > 128,
              "chrome: a dark ground gets light lettering even in the light scheme");
        Chrome::setChosenBack(false, QColor("#f4f4f6"));
        check(Chrome::backInk().lightness() < 128,
              "chrome: and a light ground gets dark lettering");

        // ---- THE TABLE TONES COME WITH IT ----
        //
        // They were fixed figures, which is a pale stripe across a ground somebody set to charcoal.
        for (const char* ground : {"#101820", "#f4f4f6", "#2f4f7a"}) {
            Chrome::setChosenBack(false, QColor(QString::fromLatin1(ground)));
            const QColor g = Chrome::back();
            check(Chrome::backAlt() != g,
                  "chrome: every other row is a step off the ground, so the stripe is visible");
            check(Chrome::backHeader() != g,
                  "chrome: and a column heading is too");
            // And they stay on the same side of the divide as the ground - a stripe must not invert.
            check((Chrome::backAlt().lightness() < 128) == (g.lightness() < 128) ||
                      std::abs(Chrome::backAlt().lightness() - g.lightness()) < 40,
                  "chrome: the stripe is a step, not a flip to the other end of the scale");
        }

        // ---- BUILT-IN IS A REAL ANSWER, and an invalid colour is how it is said. ----
        Chrome::setChosenBack(false, QColor());
        Chrome::setDark(false);
        check(Chrome::back() == builtInLight,
              "chrome: clearing a choice puts the built-in ground back");

        Chrome::setChosenBack(true, hadDark);
        Chrome::setChosenBack(false, hadLight);
        Chrome::setDark(wasDark);
    }

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

    // --- Fillet a LINE against a POLYLINE: BOTH of them are taken back to the fillet.
    {
        // The polyline's end vertex was taken back and the line was not - it kept its full length while the
        // arc left it early, so the piece between them stayed on the drawing: the stub the fillet is
        // supposed to have taken off. The line stood in as a COPY, the fillet trimmed the copy, and the copy
        // was thrown away.
        Document d;
        d.entities().clear();
        Entity h;
        h.id = "h";
        h.type = EntityType::Line;
        h.a = {0, 0};
        h.b = {150, 0};   // running well past the polyline's end, so a stub would be plain to see
        Entity pl;
        pl.id = "p";
        pl.type = EntityType::Polyline;
        pl.points = {{100, 0}, {170, 70}, {240, 70}};
        d.entities().push_back(h);
        d.entities().push_back(pl);
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.setPixelTolerance(3);
        ed.setFilletRadius(20.0);
        ed.setTool(ToolId::Fillet);
        ed.onPointerPress({40, 0}, Qt::LeftButton, Qt::NoModifier);    // the line
        ed.onPointerPress({130, 30}, Qt::LeftButton, Qt::NoModifier);  // the polyline, near its end

        const Entity* line = nullptr;
        const Entity* poly = nullptr;
        const Entity* arc = nullptr;
        for (const auto& e : d.entities()) {
            if (e.id == "h") line = &e;
            else if (e.id == "p") poly = &e;
            else if (e.type == EntityType::Arc) arc = &e;
        }
        check(arc != nullptr, "fillet line+run: the corner is rounded");
        check(line && poly, "fillet line+run: both objects are still there");
        if (arc && line && poly) {
            std::printf("  fillet line+run: the line ends at %.2f, the arc starts at %.2f\n", line->b.x,
                        arc->center.x);
            check(near(line->b.x, arc->center.x),
                  "fillet line+run: the LINE is taken back to where the fillet touches it - no stub");
            check(near(poly->points.front().x, 105.86, 0.01),
                  "fillet line+run: and so is the run's end vertex");
            // The two of them and the arc meet: nothing sticks out past the tangent points.
            const double toArc = std::hypot(line->b.x - arc->center.x, line->b.y - (arc->center.y - arc->radius));
            check(toArc < 1e-6, "fillet line+run: the line ends exactly on the arc");
        }

        // The same the other way round: whichever is picked first, both are trimmed.
        Document d2;
        d2.entities().clear();
        d2.entities().push_back(h);
        d2.entities().push_back(pl);
        Editor ed2;
        ed2.setDocument(&d2);
        noSnap(ed2);
        ed2.setPixelTolerance(3);
        ed2.setFilletRadius(20.0);
        ed2.setTool(ToolId::Fillet);
        ed2.onPointerPress({130, 30}, Qt::LeftButton, Qt::NoModifier);
        ed2.onPointerPress({40, 0}, Qt::LeftButton, Qt::NoModifier);
        for (const auto& e : d2.entities())
            if (e.id == "h")
                check(e.b.x < 100.0,
                      "fillet line+run: picking the run first trims the line just the same");
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

        // ---- DRAGGING ACROSS CELLS PICKS THE BLOCK BETWEEN THEM ----------------------------------
        //
        // The gesture is press inside a cell, hold, move to another, release; what it must give is the
        // RECTANGLE the two span.
        //
        // A VIEW SCALE IS SET FIRST, and without it this test measures the wrong thing entirely.
        // worldTolerance() is pixelTol_ / view scale, and a Document starts at scale 1 with a 10-pixel
        // tolerance - so every pick reaches 10 WORLD units. These cells are 20 x 10, which puts the
        // table's interior grid-line grips (live whenever the table is selected) within reach of the
        // middle of every cell, and a press meant for a cell grabs a column boundary instead. That is
        // correct behaviour at a silly zoom; it is not what a user at a sane zoom does. At scale 10 the
        // tolerance is one world unit and the cells are picked as intended.
        if (t) {
            d.view().scale = 10.0;

            // Table spans x[0..60] y[0..30] as 3 cols x 3 rows: columns x[0..20][20..40][40..60],
            // rows y[20..30] (merged title), [10..20] (header), [0..10] (data).
            // From the header row's first column to the data row's second: a 2x2 block.
            ed.onPointerPress({10, 15}, Qt::LeftButton, Qt::NoModifier);   // r1,c0
            ed.onPointerMove({30, 5});                                     // r2,c1
            ed.onPointerRelease({30, 5}, Qt::LeftButton);

            std::vector<int> got = ed.selectedCells();
            std::sort(got.begin(), got.end());
            const std::vector<int> want = {3, 4, 6, 7};
            check(got == want, "table: dragging across cells picks the rectangle between them");
            check(ed.selectedCellTableId() == t->id,
                  "table: the picked cells belong to the table dragged on");

            // Either corner may be first: they are corners, not a start and an end.
            ed.clearCellSelection();
            ed.onPointerPress({30, 5}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerMove({10, 15});
            ed.onPointerRelease({10, 15}, Qt::LeftButton);
            got = ed.selectedCells();
            std::sort(got.begin(), got.end());
            check(got == want, "table: the drag is symmetric - either corner may be first");

            // And a press with no move is one cell, not a block.
            ed.clearCellSelection();
            ed.onPointerPress({10, 15}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerRelease({10, 15}, Qt::LeftButton);
            check(ed.selectedCells().size() == 1,
                  "table: a click without a drag picks the one cell");
        }
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
        // THE WHOLE TABLE is picked here, not a cell - which is the case this covers. A grid line dragged
        // with the table picked belongs to the column on its left: col 0 goes 20->30, the columns right of
        // it keep their widths and travel, and the table's right edge goes with them, 60->70.
        //
        // When a CELL is picked the boundary means something else - the cell's own column is the one that
        // changes and the edge on the dragged side follows - which is what "table-floor" and "table-edge"
        // below cover. The two cases are different on purpose: with a cell picked, the thing being worked
        // on is that cell.
        ed.onPointerPress({20, 10}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({30, 10});
        ed.onPointerPress({30, 10}, Qt::LeftButton, Qt::NoModifier);
        const QJsonArray cf = table()->meta.value("colFrac").toArray();
        const double W = tableWidth();
        check(std::abs(W - 70.0) < 1e-6, "table resize: with the table picked, a column drag widens it");
        check(cf.size() == 3 && std::abs(cf[0].toDouble() * W - 30.0) < 1e-6 &&
                  std::abs(cf[1].toDouble() * W - 20.0) < 1e-6 &&
                  std::abs(cf[2].toDouble() * W - 20.0) < 1e-6,
              "table resize: the column left of the line resizes; the others keep their width");
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
        // Again with the whole table picked: the row line belongs to the row above it. Row 0 goes 10->15,
        // row 1 keeps its 10 and travels down, and the table grows 20->25 with it.
        ed.onPointerPress({30, 10}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({30, 5});
        ed.onPointerPress({30, 5}, Qt::LeftButton, Qt::NoModifier);
        const QJsonArray rf = table()->meta.value("rowFrac").toArray();
        const double H = tableHeight();
        check(std::abs(H - 25.0) < 1e-6, "table resize: with the table picked, a row drag grows it");
        check(rf.size() == 2 && std::abs(rf[0].toDouble() * H - 15.0) < 1e-6 &&
                  std::abs(rf[1].toDouble() * H - 10.0) < 1e-6,
              "table resize: the row above the line resizes; the other keeps its height");
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
        // THE RING IS CATCHABLE ONLY IF "NEAREST" IS ASKED FOR.
        //
        // A point on a circle chosen by where the cursor happened to be is not the centre, not a
        // quadrant, not a crossing - and unticked it must not be offered, or it drowns all three, the
        // ring being catchable along its whole length where a quadrant is catchable in four places.
        // Ticked, it is exactly what was asked for: a point anywhere along the thing.
        //
        // Either way the cursor is never DRAGGED to the centre by touching the outline. Where the
        // middle is gets said with a small cross instead - see the editor's overlay - and going to it
        // catches it as any centre is caught.
        {
            SnapSettings s;
            s.snapEndpoint = false; s.snapCenter = false; s.snapQuadrant = false;
            s.snapMidpoint = false; s.snapIntersection = false; s.snapGeometricCenter = false;
            s.snapNode = false;
            // Cursor just outside the circle ring (circle at (100,0) r10) near (112,0).
            s.snapNearest = false;
            auto nr = resolveSnap(d, {112, 0}, std::nullopt, 5.0, s, {});
            check(!nr.snapped, "snap-nearest-curve: unticked, the ring offers nothing to catch");
            s.snapCenter = true;
            nr = resolveSnap(d, {112, 0}, std::nullopt, 5.0, s, {});
            check(!nr.snapped,
                  "snap-nearest-curve: and the centre snap does not drag the cursor there either");
            // The centre is still caught by going to it.
            nr = resolveSnap(d, {101, 1}, std::nullopt, 5.0, s, {});
            check(nr.snapped && nr.kind == SnapKind::Center && near(nr.point.x, 100.0) &&
                      near(nr.point.y, 0.0),
                  "snap-nearest-curve: the centre is caught at the centre");
            // Ticked, the ring answers anywhere along it.
            s.snapNearest = true;
            nr = resolveSnap(d, {112, 0}, std::nullopt, 5.0, s, {});
            check(nr.snapped && nr.kind == SnapKind::Nearest && near(nr.point.x, 110.0) &&
                      near(nr.point.y, 0.0),
                  "snap-nearest-curve: ticked, Nearest snaps onto the circle ring");
        }
        // Tangent from an anchor above the circle to the ring (~(90.3, 2.5)).
        {
            SnapSettings s; s.snapTangent = true; s.snapNearest = false; s.snapEndpoint = false;
            auto t = resolveSnap(d, {90, 3}, {Vec2{100, 40}}, 5.0, s, {});
            check(t.snapped && t.kind == SnapKind::Tangent,
                  "snap-tangent: snaps to a tangent point from the anchor");
        }
    }

    // --- The snap marker says WHICH KIND of place was caught, not merely that one was.
    //
    // A square for a joint, a triangle for the middle of something, an hourglass for a crossing. The
    // shapes are drawn in the overlay, which a test cannot look at - but the DECISION is snapMarkerFor,
    // and a marker quietly reverting to one shape for everything is invisible in a screenshot and
    // obvious in a mapping.
    {
        // A joint is a point that is part of the object: an end, a vertex, a node, an insertion point,
        // and equally the definite places worked out on a curve - a quadrant, a tangent point, the
        // nearest point on a run.
        check(snapMarkerFor(SnapKind::Endpoint) == SnapMarker::Square,
              "snap-marker: an endpoint is a joint - a square");
        check(snapMarkerFor(SnapKind::Node) == SnapMarker::Square, "snap-marker: and so is a node");
        check(snapMarkerFor(SnapKind::Insertion) == SnapMarker::Square,
              "snap-marker: and an insertion point");
        check(snapMarkerFor(SnapKind::Quadrant) == SnapMarker::Square,
              "snap-marker: and a quadrant, which is a place ON the circle");
        check(snapMarkerFor(SnapKind::Perpendicular) == SnapMarker::Square,
              "snap-marker: and a perpendicular foot");
        check(snapMarkerFor(SnapKind::Tangent) == SnapMarker::Square,
              "snap-marker: and a tangent point");
        check(snapMarkerFor(SnapKind::Nearest) == SnapMarker::Square,
              "snap-marker: and the nearest point on the object");

        // The middle of something is derived from the object rather than built into it.
        check(snapMarkerFor(SnapKind::Midpoint) == SnapMarker::Triangle,
              "snap-marker: a midpoint is a triangle");
        check(snapMarkerFor(SnapKind::Center) == SnapMarker::Triangle,
              "snap-marker: and the centre of a circle");
        check(snapMarkerFor(SnapKind::GeometricCenter) == SnapMarker::Triangle,
              "snap-marker: and a centroid, which is the middle of something too");

        // A crossing belongs to neither object, and that is the distinction worth a shape of its own:
        // connecting a line to where two others happen to cross is a different act from connecting it
        // to the end of one of them, and the two candidates are often a pixel apart.
        check(snapMarkerFor(SnapKind::Intersection) == SnapMarker::Hourglass,
              "snap-marker: an intersection is an hourglass, NOT a joint");

        // The three must be three. A mapping where two of them coincide is the bug this replaced.
        check(SnapMarker::Square != SnapMarker::Triangle &&
                  SnapMarker::Triangle != SnapMarker::Hourglass &&
                  SnapMarker::Square != SnapMarker::Hourglass,
              "snap-marker: the three shapes are distinct");

        // AND IT REACHES A REAL SNAP. Asserting the mapping alone would still pass if intersections
        // were never caught, so a genuine crossing is snapped to and asked what it draws as.
        Document d; d.entities().clear();
        Entity a; a.id = "a"; a.type = EntityType::Line; a.a = {0, 50}; a.b = {100, 50};
        d.entities().push_back(a);
        Entity b; b.id = "b"; b.type = EntityType::Line; b.a = {30, 0}; b.b = {30, 80};
        d.entities().push_back(b);
        // Neither line's midpoint nor either end is at (30,50): a's midpoint is (50,50) and b's is
        // (30,40), so the crossing is the only thing there and the test is about the crossing.
        SnapSettings s; s.snapNearest = false;
        const auto x = resolveSnap(d, {30, 50}, std::nullopt, 3.0, s, {});
        check(x.snapped && x.kind == SnapKind::Intersection,
              "snap-marker: two crossing lines are caught as an intersection");
        check(snapMarkerFor(x.kind) == SnapMarker::Hourglass,
              "snap-marker: and that catch is drawn as an hourglass");

        // THE CROSSING BEING MADE, not only crossings that already exist.
        //
        // Drawing a line across an existing one is the commonest crossing on a board, and there is only
        // ONE entity in the document while it happens - the rubber band is not committed until the
        // click lands. So the band is tested against what is there, from the anchor the tool is
        // tracking from. Without it the cursor at that meeting caught Nearest - a point on the old line
        // rather than the meeting of the two - and drew as a joint.
        Document one; one.entities().clear();
        Entity across; across.id = "x"; across.type = EntityType::Line;
        across.a = {0, 50}; across.b = {100, 50};
        one.entities().push_back(across);

        SnapSettings rb;                       // defaults, minus the ones that would shadow it
        rb.snapNearest = false;
        // A line being drawn from below, up through the existing one at x = 40.
        const auto band = resolveSnap(one, {40, 49}, {Vec2{40, 0}}, 3.0, rb, {});
        check(band.snapped && band.kind == SnapKind::Intersection,
              "snap-marker: where the line being drawn crosses an existing one IS an intersection");
        check(near(band.point.x, 40.0) && near(band.point.y, 50.0),
              "snap-marker: and it lands on the crossing, not merely near it");
        check(snapMarkerFor(band.kind) == SnapMarker::Hourglass,
              "snap-marker: so it draws as an hourglass while the line is being drawn");

        // Reached slightly BEFORE the cursor gets there, because the crossing is usually just past
        // where the hand has got to. Nothing outside the snap radius can win, so this costs nothing.
        const auto approaching = resolveSnap(one, {40, 48}, {Vec2{40, 0}}, 3.0, rb, {});
        check(approaching.snapped && approaching.kind == SnapKind::Intersection,
              "snap-marker: caught on the approach, not only on arrival");

        // WITH NO ANCHOR there is no line being drawn, so there is no crossing to report. Guards
        // against the band being invented out of the bare cursor position.
        const auto idle = resolveSnap(one, {40, 49}, std::nullopt, 3.0, rb, {});
        check(idle.kind != SnapKind::Intersection,
              "snap-marker: with nothing being drawn there is no crossing to catch");
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

    // --- A table's INTERIOR grid is not snapped to; its outline is.
    {
        // It used to be, and thoroughly: every cell corner an endpoint, every cell edge its own
        // midpoint and nearest. On a real bill of materials - thirty-four rows by six columns - that is
        // hundreds of snap points a row apart. The cursor could not be moved across the table without
        // being seized by one, and nothing lying behind the table could be reached at all. A table's
        // grid is for reading and typing into; its outline is what a drawing gets lined up against.
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
        auto mid = resolveSnap(d, {21, 11}, std::nullopt, 5.0, s, {});
        check(!(mid.snapped && near(mid.point.x, 20.0) && near(mid.point.y, 10.0)),
              "table-snap: the middle grid joint is not snapped to");
        SnapSettings m; m.snapEndpoint = false; m.snapNearest = false; m.snapMidpoint = true;
        auto rm = resolveSnap(d, {10, 19}, std::nullopt, 5.0, m, {});
        check(!(rm.snapped && near(rm.point.y, 20.0) && near(rm.point.x, 10.0)),
              "table-snap: nor a cell-edge midpoint");

        // The four corners of it still are, like any other rectangle.
        auto corner = resolveSnap(d, {39, 1}, std::nullopt, 5.0, s, {});
        check(corner.snapped && near(corner.point.x, 40.0) && near(corner.point.y, 0.0),
              "table-snap: the table's own corners are still snapped to");
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

    // --- Leader without a balloon: two clicks make a LEADER - the same object one read out of a DXF
    //     is, rather than a balloon circle with the circle suppressed. Drawn and imported callouts are
    //     one kind of thing, so they are held, moved and edited the same way.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setTool(ToolId::TextLeader);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);   // tip: what it points at
        ed.onPointerPress({30, 30}, Qt::LeftButton, Qt::NoModifier); // where its words go
        const Entity* t = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Leader) t = &e;
        check(t != nullptr, "text-leader: a leader without a balloon is placed, as a leader");
        if (t) {
            check(t->points.size() == 2 && near(t->points[0].x, 0.0) && near(t->points[1].x, 30.0),
                  "text-leader: the tip it points with comes first, then where it runs to");
            check(!t->meta.value("text").toString().isEmpty(),
                  "text-leader: it starts with a word to replace, not blank");
            check(!t->meta.value("balloon").toBool(),
                  "text-leader: and it is not a balloon wearing a disguise");
        }
        // Its tip can be taken hold of and pointed somewhere else - the thing a balloon-with-no-circle
        // could not do, having no joints of its own.
        ed.setTool(ToolId::Select);
        ed.onPointerPress({15, 15}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerRelease({15, 15}, Qt::LeftButton);
        check(ed.selection().size() == 1, "text-leader: clicking its line picks the callout up");
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({-6, 4});
        ed.onPointerPress({-6, 4}, Qt::LeftButton, Qt::NoModifier);
        const Entity* after = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Leader) after = &e;
        check(after && near(after->points[0].x, -6.0) && near(after->points[0].y, 4.0),
              "text-leader: and its arrow can be pointed somewhere else");
    }

    // --- Leader WITH a balloon: still the numbered circle the bill of materials counts, aligns and
    //     collects. Choosing whether a callout carries a balloon must not change what a balloon is.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setTool(ToolId::Balloon);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({30, 30}, Qt::LeftButton, Qt::NoModifier);
        const Entity* b = nullptr;
        for (const auto& e : d.entities()) if (e.meta.value("balloon").toBool()) b = &e;
        check(b && b->type == EntityType::Circle &&
                  b->meta.value("leaders").toArray().size() == 1 &&
                  b->meta.value("text").toString() == QLatin1String("1"),
              "balloon-leader: with a balloon it is still the numbered circle, with its arrow on it");
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

    // --- AN EXPLODED POLYLINE'S LEFTOVERS TRIM. The reference ends ON the line (or a hair short of it,
    //     as a corner that has been through an explode usually does), and the piece past it is its own
    //     short line touching the reference only at its end. A fence across that piece takes it.
    {
        auto yAt = [](double x) { return -0.04 * x; };
        auto run = [&](std::vector<Entity> ents) {
            Document d; d.entities().clear();
            for (auto& e : ents) d.entities().push_back(e);
            Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(3);
            ed.selectById("V");
            ed.setTool(ToolId::Trim);
            ed.onPointerPress({92, 20}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerMove({96, -15});
            ed.onPointerPress({96, -15}, Qt::LeftButton, Qt::NoModifier);
            return d.entities();
        };
        auto line = [](const char* id, Vec2 a, Vec2 b) {
            Entity e; e.id = id; e.type = EntityType::Line; e.a = a; e.b = b; return e;
        };
        const Vec2 corner{90, yAt(90)};
        {
            const auto left = run({line("W", {0, 0}, corner), line("S", corner, {100, yAt(100)}),
                                   line("V", corner, {90, -80})});
            bool stubGone = true;
            for (const auto& e : left) stubGone = stubGone && e.id != "S";
            check(stubGone && left.size() == 2, "trim-touch: a leftover touching the reference only at its end goes");
        }
        {
            const auto left = run({line("W", {0, 0}, {100, yAt(100)}), line("V", {90, yAt(90) - 1e-4}, {90, -80})});
            double wEnd = -1;
            for (const auto& e : left) if (e.id == "W") wEnd = std::max(e.a.x, e.b.x);
            // At the foot of the reference's end - a few millionths off 90, since that end is not on the line.
            check(std::abs(wEnd - 90.0) < 1e-3, "trim-touch: a reference a hair short of the line still cuts it");
        }
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

    // --- ONE LINE of a double run, extended on its own. The drawing this comes from is a run
    //     reaching a cable beside it: one of its two lines has something to reach there and the
    //     other has not, and a gesture that moved both had to be pulled back by hand every time.
    //     The fence says which line - crossing one of them means that line alone.
    {
        Document d; d.entities().clear();
        Entity ref; ref.id = "ref"; ref.type = EntityType::DoubleLine;
        ref.a = {-100, 0}; ref.b = {100, 0};
        ref.meta.insert("gap", 20.0);          // its two lines at y = +10 and y = -10
        Entity run; run.id = "run"; run.type = EntityType::Polyline;
        run.points = {{0, 100}, {0, 40}};       // stops short of the cable, at y = 40
        run.meta.insert("double", true);
        run.meta.insert("gap", 10.0);          // its two lines at x = -5 and x = +5
        d.entities().push_back(ref);
        d.entities().push_back(run);
        Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(3);
        ed.selectById("ref");                  // the cable is the boundary
        ed.setTool(ToolId::Extend);
        // A fence across the x = +5 line and nothing else. It does not cross the centreline at all,
        // which is why this gesture used to do nothing whatever.
        ed.onPointerPress({2, 70}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({8, 70});
        ed.onPointerPress({8, 70}, Qt::LeftButton, Qt::NoModifier);
        const Entity* r = nullptr;
        for (const Entity& e : d.entities()) if (e.id == QLatin1String("run")) r = &e;
        check(r && r->points.size() == 2 && near(r->points.back().y, 40.0),
              "extend-one-line: the run's own points stay put - the centreline is not what grew");
        if (r) {
            std::vector<Vec2> plus = offsetPolyline(r->points, 5.0);
            applyDoubleRunCuts(plus, *r, +1);
            std::vector<Vec2> minus = offsetPolyline(r->points, -5.0);
            applyDoubleRunCuts(minus, *r, -1);
            // WHICH of the two it is follows offsetPolyline's own hand, so it is not spelt out here:
            // that one line reached the cable and the other did not is the whole of the point.
            check(near(std::min(plus.back().y, minus.back().y), 10.0),
                  "extend-one-line: the crossed line reaches the near side of the cable");
            check(near(std::max(plus.back().y, minus.back().y), 40.0),
                  "extend-one-line: and the other line is left where it was");
            check(r->meta.value(QStringLiteral("cutEnd")).toBool() &&
                      r->meta.value(QStringLiteral("cutEndSide")).toInt() != 0,
                  "extend-one-line: it is written as a cut cap carrying the line it belongs to");
        }
    }

    // --- A TRANSITION SLEEVE is a boundary by the outline it draws, and its own route can be
    //     extended without becoming a plain double run. The square face is at x=80 here.
    {
        Entity sleeve;
        sleeve.id = QStringLiteral("transition");
        sleeve.type = EntityType::Polyline;
        sleeve.points = {{80.0, 0.0}, {104.0, 0.0}};
        sleeve.meta.insert(QStringLiteral("double"), true);
        sleeve.meta.insert(QStringLiteral("shrunkEnds"), true);
        sleeve.meta.insert(QStringLiteral("shrinkTransition"), true);
        sleeve.meta.insert(QStringLiteral("gap"), 10.0);
        sleeve.meta.insert(QStringLiteral("shrinkFaceStart"), 10.0);
        sleeve.meta.insert(QStringLiteral("shrinkTaperStart"), 0.0);
        sleeve.meta.insert(QStringLiteral("shrinkFaceEnd"), 8.2);
        sleeve.meta.insert(QStringLiteral("shrinkTaperEnd"), 6.5);

        Entity stub;
        stub.id = QStringLiteral("stub");
        stub.type = EntityType::Line;
        stub.a = {0.0, 0.0};
        stub.b = {60.0, 0.0};
        check(extendEntityTo(stub, {60.0, 0.0}, {sleeve}) && near(stub.b.x, 80.0),
              "extend-transition: another line extends to the sleeve's drawn face");

        Entity boundary;
        boundary.id = QStringLiteral("boundary");
        boundary.type = EntityType::Line;
        boundary.a = {150.0, -20.0};
        boundary.b = {150.0, 20.0};
        Entity grown = sleeve;
        check(extendEntityTo(grown, {104.0, 0.0}, {boundary}) &&
                  near(grown.points.back().x, 150.0) &&
                  grown.meta.value(QStringLiteral("shrinkTransition")).toBool() &&
                  !grown.meta.contains(QStringLiteral("cutEnd")),
              "extend-transition: the sleeve route extends and stays a transition");

        Entity span;
        span.id = QStringLiteral("span");
        span.type = EntityType::Line;
        span.a = {70.0, 0.0};
        span.b = {110.0, 0.0};
        std::optional<Entity> extra;
        check(trimLineQuick(span, {90.0, 0.0}, {sleeve}, extra) && extra &&
                  near(span.b.x, 80.0) && near(extra->a.x, 104.0),
              "trim-transition: its drawn outline cuts the line running through it");
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

        // A LINE THAT ENDS ON ANOTHER ONE STILL CUTS IT. A branch meeting a run makes a T, and the
        // stub of run left sticking out past the branch is the commonest thing on these drawings
        // there is to trim off - and it could not be trimmed at all.
        //
        // The numbers are the ones out of the drawing this was found on (mytest3.dwx), to the last
        // digit, because that is the whole of the fault: the branch's line ends ON the run's line -
        // zero distance away, exactly - but the crossing test works in the two lines' own parameters
        // and that end lands on the 0 boundary at -1.5e-17. Rounded numbers land on the other side of
        // nothing and the fault does not show at all, so rounding them here would be testing something
        // else.
        {
            Document d; d.entities().clear();
            Entity cable;                       // the run: its lower line is the one with a stub
            cable.id = QStringLiteral("cable");
            cable.type = EntityType::DoubleLine;
            cable.a = {115.05625423875296, 142.67544244801954};
            cable.b = {269.3464118156991, 142.6754424480195};
            cable.meta.insert(QStringLiteral("gap"), 2.0);
            Entity branch;                      // the diagonal, landing on that lower line
            branch.id = QStringLiteral("branch");
            branch.type = EntityType::Polyline;
            branch.points = {{115.5608365997794, 142.79366262884682},
                             {105.04945916270319, 148.41616553589424},
                             {84.71999979656371, 148.41616553589424}};
            branch.meta.insert(QStringLiteral("double"), true);
            branch.meta.insert(QStringLiteral("gap"), 2.0);
            branch.meta.insert(QStringLiteral("cutStart"), true);
            branch.meta.insert(QStringLiteral("cutStartKind"), QStringLiteral("line"));
            branch.meta.insert(QStringLiteral("cutStartAx"), 115.05625423875296);
            branch.meta.insert(QStringLiteral("cutStartAy"), 141.67544244801954);
            branch.meta.insert(QStringLiteral("cutStartBx"), 269.3464118156991);
            branch.meta.insert(QStringLiteral("cutStartBy"), 141.6754424480195);
            branch.meta.insert(QStringLiteral("cutStartSide"), +1);
            d.entities().push_back(cable);
            d.entities().push_back(branch);
            const Entity* c = nullptr;
            for (const Entity& e : d.entities()) if (e.id == QLatin1String("cable")) c = &e;
            auto lowerLine = [&c] {
                const Vec2 off = (c->b - c->a).normalized().perp() * 1.0;
                std::vector<Vec2> rail = {c->a - off, c->b - off};
                applyDoubleRunCuts(rail, *c, -1);
                return rail;
            };
            const double startedAt = lowerLine().front().x;
            // Where the branch's own line comes down onto it - which is where the run should stop.
            std::vector<Vec2> bl = offsetPolyline(branch.points, 1.0);
            applyDoubleRunCuts(bl, branch, +1);
            const Vec2 tee = bl.front();
            check(near(closestPointOnSegment(tee, lowerLine().front(), lowerLine().back()).distance,
                       0.0),
                  "trim-tee: the branch's line really does end ON the run's line");
            check(tee.x > startedAt + 1e-9, "trim-tee: and the run sticks out past it");
            Editor ed; ed.setDocument(&d); noSnap(ed); ed.setPixelTolerance(4);
            ed.selectById(QStringLiteral("branch"));      // the branch is the boundary
            ed.setTool(ToolId::Trim);
            const Vec2 on{(startedAt + tee.x) / 2.0, lowerLine().front().y};   // across the stub
            ed.onPointerPress({on.x, on.y - 0.3}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerMove({on.x, on.y + 0.3});
            ed.onPointerPress({on.x, on.y + 0.3}, Qt::LeftButton, Qt::NoModifier);
            for (const Entity& e : d.entities()) if (e.id == QLatin1String("cable")) c = &e;
            check(near(lowerLine().front().x, tee.x),
                  "trim-tee: the stub comes off at the line that ends on it");
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

    // --- The harness fan-out can be put on the left end, the right end, or both.
    {
        auto makeBundle = [](const QString& spread, bool backwards = false) {
            Entity e;
            e.type = EntityType::TwistedPair;
            e.a = backwards ? Vec2{200, 0} : Vec2{0, 0};
            e.b = backwards ? Vec2{0, 0} : Vec2{200, 0};
            e.meta.insert("twistWires", 6);
            e.meta.insert("twistWidth", kTwistedPairWidth * 1.5);
            e.meta.insert("twistPitch", kTwistedPairPitch);
            e.meta.insert("twistEnds", kTwistedPairLead);
            e.meta.insert("twistSpread", spread);
            return e;
        };
        auto endCount = [](const Entity& e, bool atBack) {
            const auto lines = twistedPairStrands(e);
            std::vector<double> mids;
            for (size_t k = 0; k + 1 < lines.size(); k += 2) {
                const auto& a = lines[k];
                const auto& b = lines[k + 1];
                const Vec2 pa = atBack ? a.back() : a.front();
                const Vec2 pb = atBack ? b.back() : b.front();
                mids.push_back((pa.y + pb.y) * 0.5);
            }
            std::sort(mids.begin(), mids.end());
            int count = 0;
            for (double y : mids)
                if (count == 0 || std::abs(y - mids[static_cast<size_t>(count - 1)]) > 1e-5)
                    ++count;
            return count;
        };

        const Entity both = makeBundle(QStringLiteral("both"));
        check(endCount(both, false) == 6 && endCount(both, true) == 6,
              "harness spread: Both fans the wires at both ends");
        const Entity left = makeBundle(QStringLiteral("left"));
        check(endCount(left, false) == 6 && endCount(left, true) < 6,
              "harness spread: Left fans the wires at the left end only");
        const Entity right = makeBundle(QStringLiteral("right"));
        check(endCount(right, false) < 6 && endCount(right, true) == 6,
              "harness spread: Right fans the wires at the right end only");
        const Entity backwards = makeBundle(QStringLiteral("left"), true);
        check(endCount(backwards, false) < 6 && endCount(backwards, true) == 6,
              "harness spread: Left follows the sheet side, not the click order");
        Entity missing = makeBundle(QStringLiteral("left"));
        missing.meta.remove(QStringLiteral("twistSpread"));
        check(twistedPairSpread(missing) == QLatin1String("both") &&
                  endCount(missing, false) == 6 && endCount(missing, true) == 6,
              "harness spread: a run without the setting still fans at both ends");
        const Entity invalid = makeBundle(QStringLiteral("sideways"));
        check(twistedPairSpread(invalid) == QLatin1String("both"),
              "harness spread: a value that is not an end falls back to Both");

        // The drawing tool writes the same setting the ribbon edits, and it survives the file round
        // trip - so it can still be changed after the run has been placed.
        Document d;
        Editor ed;
        ed.setDocument(&d);
        ed.twistWires() = 6;
        ed.setTwistSpread(QStringLiteral("right"));
        ed.setTool(ToolId::TwistedHarness);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({200, 0});
        ed.freezeTwistedPreview();
        ed.onPointerPress({500, 90}, Qt::LeftButton, Qt::NoModifier);
        check(d.entities().size() == 1 &&
                  d.entities()[0].meta.value(QStringLiteral("twistSpread")).toString() ==
                      QLatin1String("right"),
              "harness spread: the ribbon choice is stored on the placed run");
        const Entity back = Entity::fromJson(d.entities()[0].toJson());
        check(twistedPairSpread(back) == QLatin1String("right") && endCount(back, true) == 6,
              "harness spread: saved and read back, the right-end fan is unchanged");

        // What the Properties panel writes: the selected object's own metadata, live and undoable.
        ed.selectById(d.entities()[0].id);
        ed.setSelectionMeta(QStringLiteral("twistSpread"), QStringLiteral("left"));
        ed.setSelectionMetaNumber(QStringLiteral("twistWires"), 7.0);
        check(d.entities()[0].meta.value(QStringLiteral("twistSpread")).toString() ==
                      QLatin1String("left") &&
                  d.entities()[0].meta.value(QStringLiteral("twistWires")).toInt() == 7 &&
                  twistedPairStrands(d.entities()[0]).size() == 14,
              "harness spread: Properties edits re-lay the placed run immediately");
    }

    // --- A WIRES RUN: the same bundle, laid STRAIGHT. The wires keep their own lanes the whole way
    //     instead of crossing, they spread out only at the end(s) asked for, and how far apart they
    //     spread is its own figure - see the drawings this was taken from, where the run is packed
    //     and the ends open out wider.
    {
        auto makeRun = [](bool straight, const QString& spread, double lane) {
            Entity e;
            e.type = EntityType::TwistedPair;
            e.a = {0, 0};
            e.b = {200, 0};
            e.meta.insert("twistWires", 4);
            e.meta.insert("twistWidth", kTwistedPairWidth * 1.5);
            e.meta.insert("twistPitch", kTwistedPairPitch);
            e.meta.insert("twistEnds", kTwistedPairLead);
            e.meta.insert("twistSpread", spread);
            e.meta.insert("twistStraight", straight);
            if (lane > 0.0) e.meta.insert("twistLane", lane);
            return e;
        };
        // Each wire is two lines; its middle is between them. Kept in WIRE ORDER unless asked to be
        // sorted: sorting answers where the wires are, in wire order it answers where each one is,
        // and only the second can tell a bundle that crossed from one that did not - a crossing
        // leaves the same set of places occupied by different wires.
        auto midsAt = [](const Entity& e, double x, bool sorted = false) {
            const auto lines = twistedPairStrands(e);
            std::vector<double> mids;
            for (size_t k = 0; k + 1 < lines.size(); k += 2) {
                // WHERE THE LINE IS AT x, which is between the places it was measured and not at one
                // of them: down the middle of a run there is nothing to measure for a hundred
                // millimetres at a time, so the nearest measured place is a hundred millimetres away
                // and in the fan. Interpolated along the step that spans x, with the nearest place
                // only as a last resort for an x the line does not reach.
                auto yAt = [x](const std::vector<Vec2>& run) {
                    for (size_t i = 1; i < run.size(); ++i) {
                        const Vec2 p = run[i - 1], q = run[i];
                        if ((p.x - x) * (q.x - x) > 0.0) continue;
                        const double span = q.x - p.x;
                        return std::abs(span) < 1e-12 ? p.y : p.y + (q.y - p.y) * (x - p.x) / span;
                    }
                    double best = 0.0, bestD = 1e300;
                    for (const Vec2& p : run)
                        if (std::abs(p.x - x) < bestD) { bestD = std::abs(p.x - x); best = p.y; }
                    return best;
                };
                mids.push_back((yAt(lines[k]) + yAt(lines[k + 1])) / 2.0);
            }
            if (sorted) std::sort(mids.begin(), mids.end());
            return mids;
        };

        const Entity straight = makeRun(true, QStringLiteral("both"), 0.0);
        // NOTHING CROSSES. Down the middle of the run, every wire is where it was a moment before:
        // that is the whole difference from the laid-up bundle, which is swinging from side to side.
        const std::vector<double> at80 = midsAt(straight, 80.0);
        const std::vector<double> at120 = midsAt(straight, 120.0);
        bool held = at80.size() == 4 && at120.size() == 4;
        for (size_t i = 0; held && i < at80.size(); ++i)
            if (std::abs(at80[i] - at120[i]) > 1e-6) held = false;
        check(held, "wires run: the wires keep their lanes down the run instead of crossing");
        const Entity laid = makeRun(false, QStringLiteral("both"), 0.0);
        const std::vector<double> laid80 = midsAt(laid, 80.0), laid120 = midsAt(laid, 120.0);
        bool moved = false;
        for (size_t i = 0; i < laid80.size() && i < laid120.size(); ++i)
            if (std::abs(laid80[i] - laid120[i]) > 1e-6) moved = true;
        check(moved, "wires run: and the laid-up bundle beside it still crosses, as it did");

        // SPREAD OUT AT THE END, and wider than the run is packed.
        const std::vector<double> ends = midsAt(straight, 0.0, /*sorted=*/true);
        const std::vector<double> packed80 = midsAt(straight, 80.0, /*sorted=*/true);
        const double packedPitch = packed80.size() > 1 ? packed80[1] - packed80[0] : 0.0;
        const double endPitch = ends.size() > 1 ? ends[1] - ends[0] : 0.0;
        check(packedPitch > 1e-6 && endPitch > packedPitch + 1e-6,
              "wires run: the ends open out wider than the wires run packed");
        // ...by exactly the spacing asked for, when one is asked for.
        const Entity spaced = makeRun(true, QStringLiteral("both"), 9.0);
        const std::vector<double> spacedEnds = midsAt(spaced, 0.0, /*sorted=*/true);
        bool evenly = spacedEnds.size() == 4;
        for (size_t i = 1; evenly && i < spacedEnds.size(); ++i)
            if (!near(spacedEnds[i] - spacedEnds[i - 1], 9.0)) evenly = false;
        check(evenly, "wires run: spread wires sit exactly the spacing asked for apart");

        // AND ONLY AT THE END ASKED FOR. The far end stays packed.
        const Entity leftOnly = makeRun(true, QStringLiteral("left"), 9.0);
        const std::vector<double> leftEnd = midsAt(leftOnly, 0.0, /*sorted=*/true);
        const std::vector<double> rightEnd = midsAt(leftOnly, 200.0, /*sorted=*/true);
        check(leftEnd.size() == 4 && near(leftEnd[1] - leftEnd[0], 9.0),
              "wires run: the left end spreads when Left is asked for");
        check(rightEnd.size() == 4 && near(rightEnd[1] - rightEnd[0], packedPitch),
              "wires run: and the right end is left packed");

        // The tool writes the lay, and it survives the file round trip so the Properties panel can
        // still change it afterwards.
        Document d;
        Editor ed;
        ed.setDocument(&d);
        ed.twistWires() = 4;
        ed.setTwistLane(9.0);
        ed.setTool(ToolId::WiresRun);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({100, 0});
        ed.onPointerPress({100, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({200, 0});
        ed.onPointerPress({200, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onKey(Qt::Key_Return, Qt::NoModifier);   // Enter finishes the run
        check(d.entities().size() == 1 &&
                  d.entities()[0].meta.value(QStringLiteral("twistStraight")).toBool() &&
                  near(d.entities()[0].meta.value(QStringLiteral("twistLane")).toDouble(), 9.0) &&
                  d.entities()[0].points.size() == 3,
              "wires run: the tool places a straight run through the knees it was clicked along");
        if (d.entities().size() == 1) {
            const Entity back = Entity::fromJson(d.entities()[0].toJson());
            // What the round trip is about: it comes back the same OBJECT - a straight run of four
            // wires that still draws. Not a strand count: how many lines a bundle is drawn as is
            // the convergence setting's business and changes with it, which has nothing to do with
            // whether the object survived being written to a file.
            check(back.meta.value(QStringLiteral("twistStraight")).toBool() &&
                      back.meta.value(QStringLiteral("twistWires")).toInt() == 4 &&
                      !twistedPairStrands(back).empty(),
                  "wires run: saved and read back, it is still a straight run of four wires");
            // The Properties panel turns one lay into the other on the object itself.
            ed.selectById(d.entities()[0].id);
            ed.setSelectionMetaBool(QStringLiteral("twistStraight"), false);
            const std::vector<double> a = midsAt(d.entities()[0], 80.0);
            const std::vector<double> b = midsAt(d.entities()[0], 95.0);
            bool nowCrosses = false;
            for (size_t i = 0; i < a.size() && i < b.size(); ++i)
                if (std::abs(a[i] - b[i]) > 1e-6) nowCrosses = true;
            check(nowCrosses, "wires run: Properties can lay the same run up without redrawing it");
        }
    }

    // --- A straight run STARTS AND ENDS LEVEL, and converges to a bundle of however many lines the
    //     drawing says. Both are what the drawings show: the wires are landed on something square to
    //     the run, so they come out of the fan and go into it straight; and a bundle is drawn with
    //     the number of lines it takes to read as a bundle, the rest running into it and stopping.
    {
        auto makeRun = [](int wires, int trunk, const QString& spread) {
            Entity e;
            e.type = EntityType::TwistedPair;
            e.points = {{0, 0}, {200, 0}};
            e.a = e.points.front();
            e.b = e.points.back();
            e.meta.insert("twistWires", wires);
            e.meta.insert("twistWidth", kTwistedPairWidth * 1.5);
            e.meta.insert("twistPitch", kTwistedPairPitch);
            e.meta.insert("twistEnds", kWiresRunEnd);
            e.meta.insert("twistSpread", spread);
            e.meta.insert("twistStraight", true);
            if (trunk > 0) e.meta.insert("twistTrunkWires", trunk);
            return e;
        };
        // How many wires the run is drawn with at a given distance along it: the lines that reach
        // that far, counted by the places they sit at (the two edges of one wire are close together).
        auto wiresAt = [](const Entity& e, double x) {
            const auto lines = twistedPairStrands(e);
            std::vector<double> ys;
            for (const auto& run : lines) {
                for (size_t i = 1; i < run.size(); ++i) {
                    if ((run[i - 1].x - x) * (run[i].x - x) > 0.0) continue;
                    ys.push_back(run[i].y);
                    break;
                }
            }
            // Every wire that reaches here contributes its TWO edges, so the wires are half the
            // lines. Counting the places instead would count each wire twice over: a wire's two
            // edges are its own width apart, the same order as the gap to the next wire along.
            return static_cast<int>(ys.size()) / 2;
        };

        const Entity run = makeRun(5, 3, QStringLiteral("both"));
        check(wiresAt(run, 100.0) == 3,
              "wires run: the bundle carries the number of wires it was told to converge to");
        check(wiresAt(run, 1.0) == 5,
              "wires run: and every wire is still there at the end they are landed at");
        const Entity all = makeRun(5, 0, QStringLiteral("both"));
        check(wiresAt(all, 100.0) == 5,
              "wires run: told nothing, every wire carries on as it did");

        // HOW FAR APART THEY RUN IN THE BUNDLE is the drawing's to set. Wires bundled loosely and
        // wires pulled up tight are both drawn, and this is the difference between them. It was
        // written onto the run, carried through the file and shown in the panel - and read by
        // nothing, so setting it moved no wire at all.
        auto acrossAt = [](const Entity& x, double at) {
            double lo = 1e9, hi = -1e9;
            for (const auto& s : twistedPairStrands(x))
                for (size_t i = 1; i < s.size(); ++i)
                    if ((s[i - 1].x - at) * (s[i].x - at) <= 0.0) {
                        lo = std::min(lo, s[i].y);
                        hi = std::max(hi, s[i].y);
                    }
            return hi > lo ? hi - lo : 0.0;
        };
        Entity loose = makeRun(5, 0, QStringLiteral("both"));
        loose.id = QStringLiteral("loose");
        loose.meta.insert(QStringLiteral("twistBodyPitch"), 6.0);
        Entity tight = makeRun(5, 0, QStringLiteral("both"));
        tight.id = QStringLiteral("tight");
        tight.meta.insert(QStringLiteral("twistBodyPitch"), 3.0);
        check(acrossAt(loose, 100.0) > acrossAt(tight, 100.0) + 1e-6,
              "wires run: the wire pitch set on the run is what the bundle is drawn at");

        // LEVEL AT THE ENDS. Each line arrives running ALONG the route rather than still spreading
        // across it, so a run drawn along the sheet starts and ends horizontal.
        const auto lines = twistedPairStrands(run);
        int ends = 0;
        bool level = !lines.empty();
        for (const auto& line : lines) {
            if (line.size() < 2) { level = false; break; }
            // Only where a line actually REACHES an end of the route. A wire that stops in the
            // bundle has a cut end in the middle of the fan, and that one is on the slant by design.
            for (const bool atBack : {false, true}) {
                const Vec2 tip = atBack ? line.back() : line.front();
                const Vec2 next = atBack ? line[line.size() - 2] : line[1];
                if (std::abs(tip.x) > 1e-6 && std::abs(tip.x - 200.0) > 1e-6) continue;
                ++ends;
                if (std::abs(tip.y - next.y) > 1e-6) level = false;
            }
        }
        check(ends > 0, "wires run: some wires do reach the ends of the route");
        check(level, "wires run: every wire runs level into both ends instead of arriving on the slant");

        // A wire that does not carry on is drawn from the end it is landed at INTO the bundle and
        // stops there; at an end that does not fan there is nothing but the bundle.
        const Entity oneEnd = makeRun(5, 3, QStringLiteral("left"));
        check(wiresAt(oneEnd, 1.0) == 5 && wiresAt(oneEnd, 100.0) == 3 &&
                  wiresAt(oneEnd, 199.0) == 3,
              "wires run: spread at one end only, the other end is just the bundle");
    }

    // --- AUTO ROUTE: take what is drawn to where it has to go. Two picks and a key - the run, the
    //     wall it lands on, Enter - and what is laid is another of WHATEVER was picked first,
    //     arriving square to that wall in as few bends as that takes.
    {
        auto scene = [](Vec2 a, Vec2 b) {
            Document d;
            Entity cab;
            cab.id = QStringLiteral("cab");
            cab.type = EntityType::DoubleLine;
            cab.a = a;
            cab.b = b;
            cab.meta.insert(QStringLiteral("gap"), 4.0);
            Entity box;
            box.id = QStringLiteral("box");
            box.type = EntityType::Rect;
            box.a = {150, 40};
            box.b = {190, 80};
            box.points = {{150, 40}, {190, 40}, {190, 80}, {150, 80}};
            d.entities().push_back(cab);
            d.entities().push_back(box);
            return d;
        };
        // A cable pointing straight at the wall needs no bend at all.
        {
            Document d = scene({10, 60}, {90, 60});
            Editor ed;
            ed.setDocument(&d);
            ed.setPixelTolerance(6);
            ed.setTool(ToolId::AutoRoute);
            ed.onPointerPress({80, 60}, Qt::LeftButton, Qt::NoModifier);
            check(ed.autoRouteFrom() == QLatin1String("cab"),
                  "auto route: the first click picks what is to be routed");
            ed.onPointerPress({150, 60}, Qt::LeftButton, Qt::NoModifier);
            check(ed.autoRouteTo() == QLatin1String("box") && ed.autoRouteWall() >= 0,
                  "auto route: the second picks one wall of what it lands on");
            const std::vector<Vec2> path = ed.autoRoutePath();
            check(path.size() == 2 && near(path.back().x, 150.0) && near(path.back().y, 60.0),
                  "auto route: pointing straight at the wall, it goes straight to the middle of it");
            ed.onKey(Qt::Key_Return, Qt::NoModifier);
            const Entity* laid = nullptr;
            for (const Entity& e : d.entities())
                if (e.id != QLatin1String("cab") && e.id != QLatin1String("box")) laid = &e;
            check(laid && laid->type == EntityType::DoubleLine &&
                      near(laid->meta.value(QStringLiteral("gap")).toDouble(), 4.0),
                  "auto route: what is laid is another of the sort that was picked, settings and all");
            check(ed.tool() == ToolId::Select, "auto route: laying it ends the function");
        }
        // Offset from the wall, or pointing the wrong way: one bend, and square in either case.
        for (const auto& start : {std::make_pair(Vec2{10, 20}, Vec2{90, 20}),
                                  std::make_pair(Vec2{10, 20}, Vec2{10, 90})}) {
            Document d = scene(start.first, start.second);
            Editor ed;
            ed.setDocument(&d);
            ed.setPixelTolerance(6);
            ed.setTool(ToolId::AutoRoute);
            ed.onPointerPress(start.second, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerPress({150, 60}, Qt::LeftButton, Qt::NoModifier);
            const std::vector<Vec2> path = ed.autoRoutePath();
            // NEVER BACK OVER ITSELF. The first route drawn for a run pointing away from the wall took
            // it straight back down the cable it came out of, which on the sheet is a line drawn on top
            // of the run. However many bends it takes, the first leg carries the run ON.
            const Vec2 out = (start.second - start.first).normalized();
            check(path.size() >= 3 && dot(path[1] - path[0], out) > -1e-9,
                  "auto route: it bends where it has to, and never back along the run it came from");
            if (path.size() >= 3) {
                // THE LAST LEG IS SQUARE TO THE WALL, which is the whole point of the function: the
                // wall here runs up the page, so the run must arrive across it.
                const Vec2 last = (path.back() - path[path.size() - 2]).normalized();
                check(std::abs(last.y) < 1e-9 && std::abs(std::abs(last.x) - 1.0) < 1e-9,
                      "auto route: and it arrives square to the wall it lands on");
                check(near(path.back().x, 150.0) && near(path.back().y, 60.0),
                      "auto route: at the middle of that wall");
            }
        }
    }

    // --- AUTO ROUTE CARRIES A RUN ON rather than laying a second one beside it: a polyline, double
    //     polyline, harness, cable or sleeve just gets longer, keeping every setting it had. A run
    //     that has to change level does it on a SLANT - never square where there is room for one.
    {
        Document d;
        Entity run;
        run.id = QStringLiteral("run");
        run.type = EntityType::Polyline;
        run.points = {{10, 20}, {90, 20}};
        run.meta.insert(QStringLiteral("double"), true);
        run.meta.insert(QStringLiteral("gap"), 4.0);
        Entity box;
        box.id = QStringLiteral("box");
        box.type = EntityType::Rect;
        box.a = {150, 40};
        box.b = {190, 80};
        box.points = {{150, 40}, {190, 40}, {190, 80}, {150, 80}};
        d.entities().push_back(run);
        d.entities().push_back(box);
        Editor ed;
        ed.setDocument(&d);
        ed.setPixelTolerance(6);
        ed.setTool(ToolId::AutoRoute);
        ed.onPointerPress({85, 22}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({150, 60}, Qt::LeftButton, Qt::NoModifier);
        const std::vector<Vec2> path = ed.autoRoutePath();
        bool slanted = path.size() == 4;
        for (size_t k = 1; slanted && k + 1 < path.size(); ++k) {
            const Vec2 a = (path[k] - path[k - 1]).normalized();
            const Vec2 b = (path[k + 1] - path[k]).normalized();
            const double deg = std::acos(std::clamp(dot(a, b), -1.0, 1.0)) * 180.0 / M_PI;
            slanted = std::abs(deg - 45.0) < 1e-6;
        }
        check(slanted, "auto route: a level shift is two 45-degree bends, not a square step");
        ed.onKey(Qt::Key_Return, Qt::NoModifier);
        check(d.entities().size() == 2,
              "auto route: a double polyline is carried on, not joined by a second one");
        const Entity* r = nullptr;
        for (const Entity& e : d.entities())
            if (e.id == QLatin1String("run")) r = &e;
        check(r && r->points.size() == 5 && near(r->points.back().x, 150.0),
              "auto route: and the same run now reaches the wall");
        if (r) {
            const QJsonArray radii = r->meta.value(QStringLiteral("cornerRadii")).toArray();
            check(radii.size() == 5 && near(radii[1].toDouble(), 6.0) &&
                      near(radii[0].toDouble(), 0.0) && near(radii[4].toDouble(), 0.0),
                  "auto route: its new knees are rounded, to its own width when left to it");
        }
    }

    // --- A PLAIN DOUBLE LINE cannot hold a knee, so it is carried on AS MORE DOUBLE LINES, one per
    //     leg, each mitred into the next so its two sides meet square across the bend.
    {
        Document d;
        Entity dl;
        dl.id = QStringLiteral("dl");
        dl.type = EntityType::DoubleLine;
        dl.a = {10, 20};
        dl.b = {90, 20};
        dl.meta.insert(QStringLiteral("gap"), 4.0);
        Entity box;
        box.id = QStringLiteral("box");
        box.type = EntityType::Rect;
        box.a = {150, 40};
        box.b = {190, 80};
        box.points = {{150, 40}, {190, 40}, {190, 80}, {150, 80}};
        d.entities().push_back(dl);
        d.entities().push_back(box);
        Editor ed;
        ed.setDocument(&d);
        ed.setPixelTolerance(6);
        // SQUARE KNEES ASKED FOR: a fillet of nought keeps it as double lines, mitred. Left to
        // itself a double line is rounded, which is the fillet test below.
        ed.setAutoRouteFillet(0.0);
        ed.setTool(ToolId::AutoRoute);
        ed.onPointerPress({85, 22}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({150, 60}, Qt::LeftButton, Qt::NoModifier);
        ed.onKey(Qt::Key_Return, Qt::NoModifier);
        int legs = 0, mitred = 0;
        for (const Entity& e : d.entities()) {
            if (e.type != EntityType::DoubleLine || e.id == QLatin1String("dl")) continue;
            ++legs;
            if (e.meta.value(QStringLiteral("cutStart")).toBool() ||
                e.meta.value(QStringLiteral("cutEnd")).toBool())
                ++mitred;
        }
        check(legs == 3, "auto route: a double line is carried on as one double line per leg");
        check(mitred == legs, "auto route: and every one of them is mitred where it meets the next");
    }

    // --- THE ROUTE ON THE SCREEN CAN BE TAKEN HOLD OF before it is laid. A knee is picked up and
    //     moved; a leg within 2 mm of level or plumb is made exactly so; the knee next to the wall
    //     only slides along the wall's perpendicular, so the run still lands square; the cursor's
    //     snaps apply to a knee as to anything placed with the mouse; and Shift on the route makes a
    //     new knee there.
    {
        auto setUp = [](Document& d) {
            Entity run;
            run.id = QStringLiteral("run");
            run.type = EntityType::Polyline;
            run.points = {{10, 20}, {90, 20}};
            Entity box;
            box.id = QStringLiteral("box");
            box.type = EntityType::Rect;
            box.a = {150, 40};
            box.b = {190, 80};
            box.points = {{150, 40}, {190, 40}, {190, 80}, {150, 80}};
            // something to snap to, off the route
            Entity mark;
            mark.id = QStringLiteral("mark");
            mark.type = EntityType::Line;
            mark.a = {120, 30};
            mark.b = {120, 0};
            d.entities().push_back(run);
            d.entities().push_back(box);
            d.entities().push_back(mark);
        };
        Document d;
        setUp(d);
        Editor ed;
        ed.setDocument(&d);
        ed.setPixelTolerance(6);
        ed.setTool(ToolId::AutoRoute);
        ed.onPointerPress({85, 20}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({150, 60}, Qt::LeftButton, Qt::NoModifier);
        const std::vector<Vec2> route = ed.autoRoutePath();
        check(route.size() == 4, "route edit: there is a route with two knees to take hold of");
        if (route.size() == 4) {
            // A LEG ALL BUT LEVEL IS MADE LEVEL.
            ed.onPointerPress(route[1], Qt::LeftButton, Qt::NoModifier);
            check(ed.autoRouteDragging(), "route edit: a click on a knee takes hold of it");
            ed.onPointerMove({108, 21.2});
            ed.onPointerRelease({108, 21.2}, Qt::LeftButton);
            const std::vector<Vec2> a = ed.autoRoutePath();
            check(!ed.autoRouteDragging() && a.size() == 4 && near(a[1].y, 20.0) && near(a[1].x, 108.0),
                  "route edit: moved to within 2 mm of level, the leg is made exactly level");

            // THE CURSOR'S SNAPS APPLY: moved to just off the end of the mark, the knee lands on it.
            ed.onPointerPress(a[1], Qt::LeftButton, Qt::NoModifier);
            ed.onPointerMove({120.3, 30.4});
            ed.onPointerRelease({120.3, 30.4}, Qt::LeftButton);
            const std::vector<Vec2> b = ed.autoRoutePath();
            check(b.size() == 4 && near(b[1].x, 120.0) && near(b[1].y, 30.0),
                  "route edit: a knee lands on a point of the drawing the way anything placed does");

            // THE KNEE NEXT TO THE WALL keeps the run arriving square.
            ed.onPointerPress(b[2], Qt::LeftButton, Qt::NoModifier);
            ed.onPointerMove({135, 75});
            ed.onPointerRelease({135, 75}, Qt::LeftButton);
            const std::vector<Vec2> c = ed.autoRoutePath();
            const Vec2 last = c.size() >= 2 ? (c.back() - c[c.size() - 2]).normalized() : Vec2{0, 0};
            check(c.size() == 4 && near(c[2].y, 60.0) && std::abs(last.y) < 1e-9,
                  "route edit: the knee by the wall slides only along its perpendicular");

            // SHIFT ON THE ROUTE makes a new knee there.
            const Vec2 onLeg = (c[1] + c[2]) * 0.5;
            ed.onPointerPress(onLeg, Qt::LeftButton, Qt::ShiftModifier);
            ed.onPointerMove({onLeg.x + 4, onLeg.y - 6});
            ed.onPointerRelease({onLeg.x + 4, onLeg.y - 6}, Qt::LeftButton);
            check(ed.autoRoutePath().size() == 5, "route edit: Shift on the route adds a knee there");

            // AND WHAT IS LAID IS THE ROUTE AS EDITED, not the one first worked out.
            const std::vector<Vec2> edited = ed.autoRoutePath();
            ed.onKey(Qt::Key_Return, Qt::NoModifier);
            const Entity* r = nullptr;
            for (const Entity& e : d.entities())
                if (e.id == QLatin1String("run")) r = &e;
            check(r && r->points.size() == 1 + edited.size() && near(r->points[2].x, edited[1].x) &&
                      near(r->points[2].y, edited[1].y),
                  "route edit: Enter lays the route as it was edited");
        }
    }

    // --- CTRL ON A KNEE TAKES IT AWAY, and THE END SLIDES ALONG THE GREEN WALL.
    {
        auto setUp = [](Document& d) {
            Entity run;
            run.id = QStringLiteral("run");
            run.type = EntityType::Polyline;
            run.points = {{10, 20}, {90, 20}};
            Entity box;
            box.id = QStringLiteral("box");
            box.type = EntityType::Rect;
            box.a = {150, 40};
            box.b = {190, 80};
            box.points = {{150, 40}, {190, 40}, {190, 80}, {150, 80}};
            d.entities().push_back(run);
            d.entities().push_back(box);
        };
        auto start = [](Editor& ed) {
            ed.setPixelTolerance(6);
            ed.setTool(ToolId::AutoRoute);
            ed.onPointerPress({85, 20}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerPress({150, 60}, Qt::LeftButton, Qt::NoModifier);
        };
        {
            Document d;
            setUp(d);
            Editor ed;
            ed.setDocument(&d);
            start(ed);
            const std::vector<Vec2> route = ed.autoRoutePath();
            check(route.size() == 4, "knee removal: a route with two knees");
            if (route.size() == 4) {
                ed.onPointerPress(route[1], Qt::LeftButton, Qt::ControlModifier);
                const std::vector<Vec2> a = ed.autoRoutePath();
                check(!ed.autoRouteDragging() && a.size() == 3 && near(a[1].x, route[2].x) &&
                          near(a[1].y, route[2].y),
                      "knee removal: Ctrl on a knee takes it away, and the other stays");
                ed.onPointerRelease(route[1], Qt::LeftButton);
                ed.onPointerPress(a[1], Qt::LeftButton, Qt::ControlModifier);
                const std::vector<Vec2> b = ed.autoRoutePath();
                std::printf("  knee removal: %d, then %d point(s)\n", int(a.size()), int(b.size()));
                check(b.size() == 2 && near(b.back().x, 150.0) && near(b.back().y, 60.0),
                      "knee removal: the last one too - it does not come back as the worked-out route");
            }
        }
        {
            Document d;
            setUp(d);
            Editor ed;
            ed.setDocument(&d);
            start(ed);
            // THE END, taken hold of on the green wall, and slid along it.
            ed.onPointerPress({150, 60}, Qt::LeftButton, Qt::NoModifier);
            check(ed.autoRouteDraggingEnd(), "route end: a click on the end on the wall takes hold of it");
            ed.onPointerMove({146, 71});
            ed.onPointerRelease({146, 71}, Qt::LeftButton);
            const std::vector<Vec2> a = ed.autoRoutePath();
            const Vec2 lastA = a.size() >= 2 ? (a.back() - a[a.size() - 2]).normalized() : Vec2{};
            std::printf("  route end: slid to %.1f,%.1f\n", a.empty() ? 0.0 : a.back().x,
                        a.empty() ? 0.0 : a.back().y);
            check(!ed.autoRouteDraggingEnd() && !a.empty() && near(a.back().x, 150.0) &&
                      near(a.back().y, 71.0) && std::abs(lastA.y) < 1e-9,
                  "route end: it stays on the wall where it was let go, and still lands square");
            // Not past the end of the wall.
            ed.onPointerPress(a.back(), Qt::LeftButton, Qt::NoModifier);
            ed.onPointerMove({150, 130});
            ed.onPointerRelease({150, 130}, Qt::LeftButton);
            check(near(ed.autoRoutePath().back().y, 80.0),
                  "route end: it goes no further than the wall does");
            // A ROUTE SHAPED BY HAND keeps landing square when its end is slid.
            const std::vector<Vec2> b = ed.autoRoutePath();
            if (b.size() >= 3) {
                ed.onPointerPress(b[1], Qt::LeftButton, Qt::NoModifier);
                ed.onPointerMove({b[1].x - 7, b[1].y + 3});
                ed.onPointerRelease({b[1].x - 7, b[1].y + 3}, Qt::LeftButton);
                ed.onPointerPress(ed.autoRoutePath().back(), Qt::LeftButton, Qt::NoModifier);
                ed.onPointerMove({150, 50});
                ed.onPointerRelease({150, 50}, Qt::LeftButton);
                const std::vector<Vec2> c = ed.autoRoutePath();
                const Vec2 lastC = (c.back() - c[c.size() - 2]).normalized();
                check(ed.autoRouteEdited() && near(c.back().y, 50.0) && std::abs(lastC.y) < 1e-9,
                      "route end: slid on a hand-shaped route, the last leg stays square");
            }
        }
    }

    // --- FILLETS ON THE KNEES OF A LINE OR DOUBLE LINE ROUTE. A line is rounded with an arc at each
    //     knee, the legs taken back to where the arc touches them. A double line has no arc of its
    //     own, so a rounded one is laid as a double polyline - the same two lines at the same gap,
    //     rounded on the radius.
    for (const bool dbl : {false, true}) {
        Document d;
        Entity l;
        l.id = QStringLiteral("l");
        l.type = dbl ? EntityType::DoubleLine : EntityType::Line;
        l.a = {10, 20};
        l.b = {90, 20};
        if (dbl) l.meta.insert(QStringLiteral("gap"), 4.0);
        Entity box;
        box.id = QStringLiteral("box");
        box.type = EntityType::Rect;
        box.a = {150, 40};
        box.b = {190, 80};
        box.points = {{150, 40}, {190, 40}, {190, 80}, {150, 80}};
        d.entities().push_back(l);
        d.entities().push_back(box);
        Editor ed;
        ed.setDocument(&d);
        ed.setPixelTolerance(6);
        ed.setAutoRouteFillet(5.0);
        ed.setTool(ToolId::AutoRoute);
        ed.onPointerPress({85, dbl ? 22.0 : 20.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({150, 60}, Qt::LeftButton, Qt::NoModifier);
        ed.onKey(Qt::Key_Return, Qt::NoModifier);
        int arcs = 0, rounded = 0;
        for (const Entity& e : d.entities()) {
            if (e.type == EntityType::Arc && near(e.radius, 5.0)) ++arcs;
            if (e.type == EntityType::Polyline && e.meta.value(QStringLiteral("double")).toBool()) {
                const QJsonArray radii = e.meta.value(QStringLiteral("cornerRadii")).toArray();
                bool all = radii.size() >= 3;
                for (int k = 1; all && k + 1 < radii.size(); ++k)
                    all = near(radii[k].toDouble(), 5.0);
                if (all) ++rounded;
            }
        }
        if (dbl)
            check(rounded == 1, "route fillet: a double line's knees are rounded on the radius asked for");
        else
            check(arcs == 2, "route fillet: each knee of a line route is an arc of the radius asked for");
    }


    // --- SHIFT ON AN END of a selected harness carries it on by hand, knee by knee, until Enter.
    {
        Document d;
        Entity h;
        h.id = QStringLiteral("h");
        h.type = EntityType::TwistedPair;
        h.points = {{10, 20}, {90, 20}};
        h.a = h.points.front();
        h.b = h.points.back();
        h.meta.insert(QStringLiteral("twistWires"), 5);
        h.meta.insert(QStringLiteral("twistStraight"), true);
        h.meta.insert(QStringLiteral("twistWidth"), kTwistedPairWidth * 1.5);
        d.entities().push_back(h);
        Editor ed;
        ed.setDocument(&d);
        ed.setPixelTolerance(6);
        ed.selectById(QStringLiteral("h"));
        ed.onPointerPress({90, 20}, Qt::LeftButton, Qt::ShiftModifier);
        check(ed.continuingRun(), "carry on: Shift on an end of a selected harness takes it up");
        ed.onPointerMove({120, 20});
        ed.onPointerPress({120, 20}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({120, 60});
        ed.onPointerPress({120, 60}, Qt::LeftButton, Qt::NoModifier);
        ed.onKey(Qt::Key_Return, Qt::NoModifier);
        check(d.entities().size() == 1, "carry on: it is still one harness, not two");
        if (d.entities().size() == 1) {
            const Entity& e = d.entities().front();
            check(e.points.size() == 4 && near(e.points.back().x, 120.0) &&
                      near(e.points.back().y, 60.0),
                  "carry on: longer by the knees that were clicked");
            check(e.meta.value(QStringLiteral("twistWires")).toInt() == 5,
                  "carry on: and it keeps every setting it had");
        }
        // Esc in the middle of it leaves the run as it was.
        ed.selectById(QStringLiteral("h"));
        ed.onPointerPress({120, 60}, Qt::LeftButton, Qt::ShiftModifier);
        ed.onPointerMove({150, 60});
        ed.onPointerPress({150, 60}, Qt::LeftButton, Qt::NoModifier);
        ed.onKey(Qt::Key_Escape, Qt::NoModifier);
        check(!ed.continuingRun() && d.entities().front().points.size() == 4,
              "carry on: Esc leaves the run exactly as it was");
    }


    // --- Tab pins a run part way through drawing it, so the ribbon can be used on the real thing.
    {
        Document d;
        Editor ed;
        ed.setDocument(&d);
        ed.twistWires() = 4;
        ed.setTool(ToolId::WiresRun);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({100, 0});
        ed.onPointerPress({100, 0}, Qt::LeftButton, Qt::NoModifier);   // a knee
        ed.onPointerMove({160, 0});
        ed.freezeTwistedPreview();
        check(ed.twistedFrozen(), "wires run: Tab pins the run part way through drawing it");
        // Off to the ribbon and back: the pointer crosses the drawing on the way, and the run must
        // not follow it - being held still is the whole point of pinning it.
        ed.onPointerMove({400, 300});
        ed.onKey(Qt::Key_Return, Qt::NoModifier);
        check(d.entities().size() == 1, "wires run: Enter finishes the pinned run");
        if (d.entities().size() == 1) {
            const Entity& e = d.entities().front();
            check(e.points.size() == 3 && near(e.points.back().x, 160.0) &&
                      near(e.points.back().y, 0.0),
                  "wires run: it ends where it was pinned, not where the pointer wandered off to");
        }
    }

    // --- A twisted HARNESS ROUTED through knees: both generalisations together.
    {
        Document d;
        Editor ed;
        ed.setDocument(&d);
        ed.twistWires() = 5;
        ed.setTwistSpread(QStringLiteral("left"));
        ed.setTool(ToolId::TwistedHarnessRoute);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({100, 0});
        ed.onPointerPress({100, 0}, Qt::LeftButton, Qt::NoModifier);   // a knee
        ed.onPointerMove({100, 80});
        ed.onPointerPress({100, 80}, Qt::LeftButton, Qt::NoModifier);
        ed.onKey(Qt::Key_Return, Qt::NoModifier);                      // Enter finishes the run

        const Entity* bundle = nullptr;
        for (const auto& e : d.entities())
            if (e.type == EntityType::TwistedPair) bundle = &e;
        check(bundle != nullptr, "twisted harness route: the clicks draw one bundle");
        if (bundle) {
            check(bundle->points.size() == 3,
                  "twisted harness route: it keeps its knees, the same as a routed pair");
            check(bundle->meta.value("twistWires").toInt() == 5,
                  "twisted harness route: and carries the wire count, the same as a straight harness");
            check(bundle->meta.value(QStringLiteral("twistSpread")).toString() ==
                      QLatin1String("left"),
                  "twisted harness route: and carries the spread side too");
            const auto lines = twistedPairStrands(*bundle);
            check(lines.size() == 10, "twisted harness route: five wires, two edges each");
            bool endsUpTheLeg = true;
            for (const auto& line : lines)
                if (!near(line.back().y, 80.0)) endsUpTheLeg = false;
            check(endsUpTheLeg, "twisted harness route: every wire finishes at the end of the last leg");
            auto endCount = [bundle](bool atBack) {
                const auto runs = twistedPairStrands(*bundle);
                std::vector<Vec2> mids;
                for (size_t k = 0; k + 1 < runs.size(); k += 2) {
                    const Vec2 a = atBack ? runs[k].back() : runs[k].front();
                    const Vec2 b = atBack ? runs[k + 1].back() : runs[k + 1].front();
                    mids.push_back((a + b) * 0.5);
                }
                int count = 0;
                for (const Vec2& p : mids) {
                    bool seen = false;
                    for (size_t k = 0; k < static_cast<size_t>(count); ++k)
                        if (distance(p, mids[k]) < 1e-5) { seen = true; break; }
                    if (!seen) ++count;
                }
                return count;
            };
            check(endCount(false) == 5 && endCount(true) < 5,
                  "twisted harness route: Left fans the left end after the knee too");
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

    // --- Esc lets go of a part being placed, without putting it on the sheet.
    {
        Document d;
        Editor ed;
        ed.setDocument(&d);
        Entity arm;
        arm.type = EntityType::Line;
        arm.a = {0, 0};
        arm.b = {10, 0};
        int started = 0;
        int finished = -1;
        QObject::connect(&ed, &Editor::pastePlacementStarted, &ed,
                         [&started] { ++started; });
        QObject::connect(&ed, &Editor::pastePlacementFinished, &ed,
                         [&finished](bool placed) { finished = placed ? 1 : 0; });

        ed.placeEntities({arm});
        check(started == 1,
              "cancel placing: starting the ghost tells the window to hand the keys to the sheet");
        check(ed.placingPaste() && ed.placementPreviewActive(),
              "cancel placing: the library part is under the cursor");
        check(ed.onKey(Qt::Key_Escape, Qt::NoModifier),
              "cancel placing: Esc is answered while the ghost is being handled");
        check(!ed.placingPaste() && !ed.placementPreviewActive(),
              "cancel placing: and there is no ghost left to place");
        check(d.entities().empty(), "cancel placing: nothing was written to the drawing");
        check(finished == 0, "cancel placing: whoever handed it over is told it was cancelled");

        // The thing was let go of, not thrown out of the clipboard: the same part can still be picked
        // up again rather than having to be found in the library a second time.
        ed.paste();
        check(ed.placingPaste(), "cancel placing: Ctrl+V can pick the same part up again");
        ed.onPointerPress({20.0, 20.0}, Qt::LeftButton, Qt::NoModifier);
        check(d.entities().size() == 1 && finished == 1,
              "cancel placing: and placing it afterwards still works");
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

    // ---- a dimension is caught where it is drawn -------------------------------------------------
    {
        // Which way a dimension's line runs is answered by dimensionLineEnds, which honours what the
        // dimension was placed measuring along. The hit test used to work it out AGAIN from the geometry -
        // taking whichever way the stand-off was further offset - and the two could disagree. A stand-off
        // further along the run than out to the side (which is most of them on a real drawing) was then
        // DRAWN across the top and CLICKABLE down the side: it could be seen and not caught, and a
        // selection window round it came up empty.
        Document d;
        Entity dim;
        dim.id = QStringLiteral("dim");
        dim.type = EntityType::Dimension;
        dim.meta.insert(QStringLiteral("dimType"), QStringLiteral("linear"));
        dim.meta.insert(QStringLiteral("dimAcross"), true);   // measured along x
        dim.a = {10.0, 0.0};
        dim.b = {30.0, 4.0};
        // The stand-off is 3 up from the middle and 10 along it. Guessing from those offsets gives the
        // WRONG axis - it would run the dimension line down the side at x = 30 - while what the dimension
        // says it measures along (x) runs it across at y = 5, which is where the renderer draws it.
        dim.points = {dim.a, dim.b, {30.0, 5.0}};
        d.entities().push_back(dim);

        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.setTool(ToolId::Select);
        // The drawn dimension line runs from (10,5) to (30,5). Click the middle of it, well clear of
        // either extension line.
        ed.onPointerPress({20.0, 5.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerRelease({20.0, 5.0}, Qt::LeftButton);
        check(ed.selection().size() == 1 && ed.selection().front() == QLatin1String("dim"),
              "dim-catch: clicking the line it draws picks the dimension up");

        // A dimension is its lines, not the area between them: the middle of the space it brackets is
        // empty canvas.
        ed.onKey(Qt::Key_Escape, Qt::NoModifier);
        ed.onPointerPress({20.0, 30.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerRelease({20.0, 30.0}, Qt::LeftButton);
        check(ed.selection().empty(), "dim-catch: and it is not lurking where nothing is drawn");
    }

    // ---- a callout can be caught and pointed somewhere else --------------------------------------
    {
        // The whole point of a leader being an object: it can be picked up and its ARROW can be pointed
        // somewhere. As a polyline plus a loose filled triangle it was two things to move; grouped, it
        // moved as one but had no joints at all, so it could be selected and still not aimed.
        Document d;
        Entity lead;
        lead.id = QStringLiteral("callout");
        lead.type = EntityType::Leader;
        lead.points = {{10.0, 10.0}, {30.0, 30.0}};   // tip first, then where the words sit
        lead.meta.insert(QStringLiteral("text"), QStringLiteral("LBL3"));
        lead.meta.insert(QStringLiteral("size"), 2.0);
        lead.meta.insert(QStringLiteral("arrowSize"), 3.0);
        d.entities().push_back(lead);

        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.setTool(ToolId::Select);
        // Clicking anywhere along the line it draws picks it up.
        ed.onPointerPress({20.0, 20.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerRelease({20.0, 20.0}, Qt::LeftButton);
        check(ed.selection().size() == 1 && ed.selection().front() == QLatin1String("callout"),
              "callout: clicking the leader picks it up");

        // And it HAS joints - one per point, the first being the tip. Grab the tip and place it
        // somewhere else: the arrow now points at what it was moved to.
        ed.onPointerPress({10.0, 10.0}, Qt::LeftButton, Qt::NoModifier);   // grab the tip
        ed.onPointerMove({4.0, 6.0});
        ed.onPointerPress({4.0, 6.0}, Qt::LeftButton, Qt::NoModifier);     // place it
        const Entity& after = d.entities().front();
        std::printf("  callout: tip moved to (%.1f,%.1f), far end still (%.1f,%.1f)\n",
                    after.points[0].x, after.points[0].y, after.points[1].x, after.points[1].y);
        check(std::abs(after.points[0].x - 4.0) < 1e-6 && std::abs(after.points[0].y - 6.0) < 1e-6,
              "callout: the arrow can be pointed somewhere else");
        check(std::abs(after.points[1].x - 30.0) < 1e-6,
              "callout: and the end the words sit on stays where it was");
        // It is still one object carrying its words - moving the arrow did not shed them.
        check(after.meta.value(QStringLiteral("text")).toString() == QLatin1String("LBL3"),
              "callout: the words moved with it, being part of it");
    }

    // ---- a named linetype actually draws dashed ---------------------------------------------------
    {
        // The Linetype property offers CENTER, HIDDEN, DOT2, PHANTOM and draws a little sample of each, and
        // choosing one had never once changed what came out. The renderer honoured meta "lineType" - a
        // capital T, and only the single value "dashed" - while the property writes meta "linetype" with a
        // name. Two keys a letter apart. So every linetype on the list was decoration, and a centre mark
        // came out as two solid strokes.
        check(linetypeDashes(QStringLiteral("CONTINUOUS"), 1.0, 1.0).isEmpty() &&
                  linetypeDashes(QStringLiteral("ByLayer"), 1.0, 1.0).isEmpty(),
              "linetype: a solid one has no pattern");
        check(!linetypeDashes(QStringLiteral("CENTER"), 1.0, 1.0).isEmpty() &&
                  !linetypeDashes(QStringLiteral("HIDDEN"), 1.0, 1.0).isEmpty(),
              "linetype: a named one has a pattern");
        // A name from a file that means nothing here draws SOLID rather than something guessed at.
        check(linetypeDashes(QStringLiteral("SOMETHING_ELSE"), 1.0, 1.0).isEmpty(),
              "linetype: an unknown name draws solid rather than invented");

        // And it reaches the paper. Counted as runs of ink across the line: a solid one is a single run, a
        // dashed one is many. Drawing it is the only way to catch the two-keys mistake.
        Document d;
        auto runsOf = [](const Entity& e) {
            QImage img(400, 40, QImage::Format_ARGB32_Premultiplied);
            img.fill(Qt::black);
            Document doc;
            ViewTransform vt;
            vt.view.scale = 1.0;
            vt.view.x = 0.0;
            vt.view.y = 20.0;
            {
                QPainter p(&img);
                EntityRenderer::draw(p, e, doc, vt);
            }
            int runs = 0;
            bool on = false;
            for (int x = 0; x < img.width(); ++x) {
                bool any = false;
                for (int y = 0; y < img.height(); ++y) {
                    const QRgb px = img.pixel(x, y);
                    if (qRed(px) + qGreen(px) + qBlue(px) > 90) any = true;
                }
                if (any && !on) ++runs;
                on = any;
            }
            return runs;
        };
        Entity plain;
        plain.id = QStringLiteral("plain");
        plain.type = EntityType::Line;
        plain.a = {10.0, 0.0};
        plain.b = {390.0, 0.0};
        Entity centred = plain;
        centred.id = QStringLiteral("centred");
        centred.meta.insert(QStringLiteral("linetype"), QStringLiteral("CENTER"));
        const int plainRuns = runsOf(plain), centredRuns = runsOf(centred);
        std::printf("  linetype: plain %d run(s) of ink, CENTER %d\n", plainRuns, centredRuns);
        check(plainRuns == 1, "linetype: a plain line is one unbroken run");
        check(centredRuns > 8, "linetype: a CENTER line is drawn broken, on the paper and not just in the "
                               "dropdown");
    }

    // ---- where the middle of something is ---------------------------------------------------------
    {
        // Both are PICKED rather than drawn: the geometry already says where the middle is, so making
        // somebody find it with a cursor is asking them to do arithmetic the program can do exactly.
        Document d;
        Entity hole;
        hole.id = QStringLiteral("hole");
        hole.type = EntityType::Circle;
        hole.center = {20.0, 30.0};
        hole.radius = 4.0;
        d.entities().push_back(hole);
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);

        ed.setTool(ToolId::CenterMark);
        ed.onPointerPress({20.0, 34.0}, Qt::LeftButton, Qt::NoModifier);   // on the ring
        int marks = 0;
        Bounds span;
        for (const Entity& e : d.entities())
            if (e.meta.value("centerMark").toBool()) {
                ++marks;
                span.expand(e.a);
                span.expand(e.b);
            }
        check(marks == 2, "centre-mark: a cross - two strokes - is put on the circle");
        check(std::abs(span.center().x - 20.0) < 1e-6 && std::abs(span.center().y - 30.0) < 1e-6,
              "centre-mark: centred on the circle's own centre");
        check(span.width() > 8.0 && span.width() < 10.0,
              "centre-mark: overshooting the ring a little, so it reads as a centre mark");

        // A centre line between two parallel runs, picked one then the other.
        Document d2;
        for (int i = 0; i < 2; ++i) {
            Entity l;
            l.id = QStringLiteral("l%1").arg(i);
            l.type = EntityType::Line;
            l.a = {0.0, i ? 10.0 : 0.0};
            l.b = {40.0, i ? 10.0 : 0.0};
            d2.entities().push_back(l);
        }
        Editor e2;
        e2.setDocument(&d2);
        noSnap(e2);
        e2.setTool(ToolId::CenterLine);
        e2.onPointerPress({20.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
        e2.onPointerPress({20.0, 10.0}, Qt::LeftButton, Qt::NoModifier);
        const Entity* cl = nullptr;
        for (const Entity& e : d2.entities())
            if (e.meta.value("centerLine").toBool()) cl = &e;
        check(cl != nullptr, "centre-line: a line is drawn between the two");
        if (cl)
            check(std::abs(cl->a.y - 5.0) < 1e-6 && std::abs(cl->b.y - 5.0) < 1e-6 &&
                      std::abs(cl->a.x) < 1e-6 && std::abs(cl->b.x - 40.0) < 1e-6,
                  "centre-line: down the middle of the gap, end to end");

        // Two that are NOT parallel are refused rather than answered with a line through nothing.
        Document d3;
        Entity p1, p2;
        p1.id = QStringLiteral("p1");
        p1.type = EntityType::Line;
        p1.a = {0.0, 0.0};
        p1.b = {40.0, 0.0};
        p2.id = QStringLiteral("p2");
        p2.type = EntityType::Line;
        p2.a = {0.0, 10.0};
        p2.b = {40.0, 30.0};   // splayed
        d3.entities().push_back(p1);
        d3.entities().push_back(p2);
        Editor e3;
        e3.setDocument(&d3);
        noSnap(e3);
        e3.setTool(ToolId::CenterLine);
        e3.onPointerPress({20.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
        e3.onPointerPress({20.0, 20.0}, Qt::LeftButton, Qt::NoModifier);
        bool any = false;
        for (const Entity& e : d3.entities())
            if (e.meta.value("centerLine").toBool()) any = true;
        check(!any, "centre-line: two that are not parallel have no middle, and none is invented");
    }

    // ---- cells joined into one, and broken apart again --------------------------------------------
    {
        Document d;
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        Editor::TableSpec s;
        s.cols = 3;
        s.colWidth = 20.0;
        s.dataRows = 3;
        s.rowHeight = 10.0;
        s.title = false;
        s.header = false;
        ed.setTableSpec(s);
        ed.setTool(ToolId::Table);
        ed.onPointerPress({0, 30}, Qt::LeftButton, Qt::NoModifier);   // (0,0)..(60,30)
        auto table = [&]() -> const Entity* {
            for (const auto& e : d.entities())
                if (e.meta.value("table").toBool()) return &e;
            return nullptr;
        };
        const QString tid = table()->id;
        ed.applyTableCellEdit(tid, 0, QStringLiteral("first"));
        ed.applyTableCellEdit(tid, 1, QStringLiteral("second"));

        // Drag across the top two cells, then join them.
        ed.setTool(ToolId::Select);
        ed.onPointerPress({10, 25}, Qt::LeftButton, Qt::NoModifier);   // cell 0
        ed.onPointerMove({30, 25});                                   // ...to cell 1
        ed.onPointerRelease({30, 25}, Qt::LeftButton);
        std::printf("  table-join: picked %d cell(s)\n", int(ed.selectedCells().size()));
        ed.tableMergeCells();

        const std::vector<TableMerge> m = tableMerges(*table());
        check(m.size() == 1, "table-join: the two cells are one block");
        if (m.size() == 1)
            check(m[0].row == 0 && m[0].col == 0 && m[0].rowSpan == 1 && m[0].colSpan == 2,
                  "table-join: covering the cells that were picked");
        // The words of the first cell that had any become the joined cell's words, and the covered cell
        // is emptied - text in a cell nobody can see or reach is how a drawing says what nobody wrote.
        const QJsonArray cells = table()->meta.value("cells").toArray();
        check(cells[0].toString() == QLatin1String("first"),
              "table-join: the joined cell keeps the words");
        check(cells[1].toString().isEmpty(), "table-join: and the covered cell is emptied");

        // A click anywhere in the block picks the block: the cell it starts at.
        ed.onKey(Qt::Key_Escape, Qt::NoModifier);
        ed.onPointerPress({30, 25}, Qt::LeftButton, Qt::NoModifier);   // over what WAS cell 1
        ed.onPointerRelease({30, 25}, Qt::LeftButton);
        check(ed.selectedCellIndex() == 0,
              "table-join: clicking anywhere in the block picks the block");

        // And it comes apart again.
        ed.tableSplitCell();
        check(tableMerges(*table()).empty(), "table-join: split gives the cells back");
        ed.onKey(Qt::Key_Escape, Qt::NoModifier);
        ed.onPointerPress({30, 25}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerRelease({30, 25}, Qt::LeftButton);
        check(ed.selectedCellIndex() == 1, "table-join: and cell 1 is its own cell once more");
    }

    // ---- the table itself, or one of its cells ----------------------------------------------------
    {
        // Every click inside a table used to take the whole table, so there was no way to say which was
        // meant - and the corner handle that resizes the table sat on top of the cells being typed into.
        // The OUTLINE is the table; anywhere inside is the cell.
        Document d;
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        Editor::TableSpec s;
        s.cols = 3;
        s.colWidth = 20.0;
        s.dataRows = 2;
        s.rowHeight = 10.0;
        s.title = false;
        s.header = false;
        ed.setTableSpec(s);
        ed.setTool(ToolId::Table);
        ed.onPointerPress({0, 20}, Qt::LeftButton, Qt::NoModifier);   // (0,0)..(60,20)
        auto table = [&]() -> const Entity* {
            for (const auto& e : d.entities())
                if (e.meta.value("table").toBool()) return &e;
            return nullptr;
        };
        const QString tid = table()->id;

        // Well inside a cell: the CELL, and the table is not picked as a whole.
        ed.setTool(ToolId::Select);
        ed.onPointerPress({30, 15}, Qt::LeftButton, Qt::NoModifier);
        check(ed.selectedCellTable() == tid && ed.selectedCellIndex() == 1,
              "table-pick: inside a cell picks the cell");

        // ON the outline: the whole table, and no cell.
        ed.onKey(Qt::Key_Escape, Qt::NoModifier);
        ed.onPointerPress({30, 20}, Qt::LeftButton, Qt::NoModifier);   // the top border
        ed.onPointerRelease({30, 20}, Qt::LeftButton);
        check(ed.selection().size() == 1 && ed.selection().front() == tid,
              "table-pick: the outline picks the table");
        check(ed.selectedCells().empty(),
              "table-pick: and picking the table leaves no cell picked");

        // The band that counts as the outline must not reach into the cells: a click a quarter of the
        // way down the top row is the cell, not the border.
        ed.onKey(Qt::Key_Escape, Qt::NoModifier);
        ed.onPointerPress({30, 17.0}, Qt::LeftButton, Qt::NoModifier);
        check(ed.selectedCellIndex() == 1,
              "table-pick: the outline band does not swallow the cells behind it");

    }

    // ---- a cell's handles can be CAUGHT where they are drawn --------------------------------------
    {
        // A TALL table, deliberately: a cell's handle sits at the middle of that cell, and the handle
        // this replaced sat at the middle of the whole table. On a short table those two are close enough
        // that the pick tolerance covers both and either would be caught - which is exactly why the first
        // version of this test passed against the bug it was written for. Ten rows put them 45 apart.
        //
        // The bug: the click cleared the cell selection FIRST, and where a cell's handles are is worked
        // out from which cell is picked. So the grab looked for handles at the corners and the middle of
        // the whole table - where they used to be, not where they had just been drawn - and on a real
        // thirty-four row bill of materials the handle simply could not be caught at all.
        Document d;
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        Editor::TableSpec s;
        s.cols = 3;
        s.colWidth = 20.0;
        s.dataRows = 10;
        s.rowHeight = 10.0;
        s.title = false;
        s.header = false;
        ed.setTableSpec(s);
        ed.setTool(ToolId::Table);
        ed.onPointerPress({0, 100}, Qt::LeftButton, Qt::NoModifier);   // (0,0)..(60,100)
        auto table = [&]() -> const Entity* {
            for (const auto& e : d.entities())
                if (e.meta.value("table").toBool()) return &e;
            return nullptr;
        };

        // Cell 1 is the top row, middle column: x 20..40, y 90..100. Its handles sit at y = 95. The
        // table's own middle is y = 50, a long way off.
        ed.setTool(ToolId::Select);
        ed.onPointerPress({30, 95}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerRelease({30, 95}, Qt::LeftButton);
        check(ed.selectedCellIndex() == 1, "table-grip: the cell is picked");
        const double before = table()->meta.value("colFrac").toArray()[0].toDouble();
        ed.onPointerPress({20, 95}, Qt::LeftButton, Qt::NoModifier);   // grab it at the CELL
        ed.onPointerMove({26, 95});
        ed.onPointerPress({26, 95}, Qt::LeftButton, Qt::NoModifier);   // place it
        const double after = table()->meta.value("colFrac").toArray()[0].toDouble();
        std::printf("  table-grip: first column %.3f -> %.3f of the table\n", before, after);
        check(after > before,
              "table-grip: the handle is caught where it is drawn, and widens the whole column");

        // ---- a column is not squeezed over its own words ----
        //
        // Dragging a boundary to the left takes width from the column on the left, and it stops where that
        // column's longest entry stops: a table that hides what is written in it is worse than a wide one.
        {
            Document dw;
            Editor ew;
            ew.setDocument(&dw);
            noSnap(ew);
            Editor::TableSpec ts;
            ts.cols = 2;
            ts.colWidth = 40.0;
            ts.dataRows = 2;
            ts.rowHeight = 10.0;
            ts.title = false;
            ts.header = false;
            ew.setTableSpec(ts);
            ew.setTool(ToolId::Table);
            ew.onPointerPress({0, 20}, Qt::LeftButton, Qt::NoModifier);   // (0,0)..(80,20)
            auto tw = [&]() -> const Entity* {
                for (const auto& e : dw.entities())
                    if (e.meta.value("table").toBool()) return &e;
                return nullptr;
            };
            const QString id = tw()->id;
            // Words in the LEFT column that need SOME of its 40, but not all of it - so there is room to
            // narrow and a point past which there is not. (A string that already overflows its column
            // cannot be narrowed at all, which is right, and would test nothing.)
            ew.applyTableCellEdit(id, 0, QStringLiteral("SHORT"));
            ew.setTool(ToolId::Select);
            // Pick the cell first. With the whole TABLE picked, every boundary handle sits on the table's
            // centre lines - so the middle column handle and the middle row handle land on the same point
            // and a click there is a coin toss between them. Picking a cell puts its handles on the cell,
            // which is both what a person does and what keeps this test measuring one thing.
            ew.onPointerPress({20, 15}, Qt::LeftButton, Qt::NoModifier);   // cell 0
            ew.onPointerRelease({20, 15}, Qt::LeftButton);
            // Haul its right-hand boundary hard left, far past what the words need. The handle sits at the
            // cell's own mid-height, y = 15.
            ew.onPointerPress({40, 15}, Qt::LeftButton, Qt::NoModifier);
            ew.onPointerMove({2, 15});
            ew.onPointerPress({2, 15}, Qt::LeftButton, Qt::NoModifier);
            const QJsonArray cw = tw()->meta.value("colFrac").toArray();
            const double leftW = cw[0].toDouble() * 80.0;
            std::printf("  table-floor: left column stopped at %.1f wide\n", leftW);
            // It goes right down to about a letter's width. It used to stop at the column's longest WORD,
            // on the grounds that a word cannot be broken - it can, the wrap breaks one that will not fit -
            // so there was no reason to stop the drag at all. What is left is only enough to keep the
            // column from vanishing.
            check(leftW > 1.0, "table-floor: the column does not vanish altogether");
            check(leftW < 15.0, "table-floor: but it narrows right down to about a letter");
        }

        // ---- a picked cell's row CAN be made taller ----
        //
        // It could not be while the two rows either side traded a fixed total between them: on a real
        // imported table every row already sits near the least its text will fit in, so there was nothing
        // to take from the neighbour and the row would not budge. Only the picked cell's row changes now,
        // and the rows past the boundary travel with it.
        {
            Document dh;
            Editor eh;
            eh.setDocument(&dh);
            noSnap(eh);
            Editor::TableSpec ts;
            ts.cols = 2;
            ts.colWidth = 30.0;
            ts.dataRows = 3;
            ts.rowHeight = 10.0;
            ts.title = false;
            ts.header = false;
            eh.setTableSpec(ts);
            eh.setTool(ToolId::Table);
            eh.onPointerPress({0, 30}, Qt::LeftButton, Qt::NoModifier);   // (0,0)..(60,30)
            auto th = [&]() -> const Entity* {
                for (const auto& e : dh.entities())
                    if (e.meta.value("table").toBool()) return &e;
                return nullptr;
            };
            eh.setTool(ToolId::Select);
            eh.onPointerPress({15, 25}, Qt::LeftButton, Qt::NoModifier);   // the top-left cell
            eh.onPointerRelease({15, 25}, Qt::LeftButton);
            check(eh.selectedCellIndex() == 0, "table-taller: the cell is picked");
            const double rowBefore = th()->meta.value("rowFrac").toArray()[0].toDouble() *
                                     th()->bounds().height();
            // Its BOTTOM edge is at y = 20, handle at the cell's mid-x = 15. Pull it down to make the row
            // taller.
            eh.onPointerPress({15, 20}, Qt::LeftButton, Qt::NoModifier);
            eh.onPointerMove({15, 14});
            eh.onPointerPress({15, 14}, Qt::LeftButton, Qt::NoModifier);
            const double rowAfter = th()->meta.value("rowFrac").toArray()[0].toDouble() *
                                    th()->bounds().height();
            std::printf("  table-taller: first row %.1f -> %.1f\n", rowBefore, rowAfter);
            check(rowAfter > rowBefore + 1.0, "table-taller: the row can be made taller");
            const QJsonArray rfh = th()->meta.value("rowFrac").toArray();
            const double Hh = th()->bounds().height();
            check(std::abs(rfh[1].toDouble() * Hh - 10.0) < 1e-6 &&
                      std::abs(rfh[2].toDouble() * Hh - 10.0) < 1e-6,
                  "table-taller: and the rows below keep their own heights");
        }

        // ---- and the table stays where it is put ----
        //
        // A boundary's position is the top of the table less the heights of the rows above it, so with the
        // top fixed the only way to MOVE boundary r is to change row r-1. Making it change the row BELOW
        // instead meant the top of the table had to travel to keep the boundary under the cursor - and a
        // drag on one cell's top edge hauled the whole table down the sheet, which is what was reported.
        {
            Document dt;
            Editor et;
            et.setDocument(&dt);
            noSnap(et);
            Editor::TableSpec ts;
            ts.cols = 2;
            ts.colWidth = 30.0;
            ts.dataRows = 3;
            ts.rowHeight = 10.0;
            ts.title = false;
            ts.header = false;
            et.setTableSpec(ts);
            et.setTool(ToolId::Table);
            et.onPointerPress({0, 30}, Qt::LeftButton, Qt::NoModifier);   // top-left at (0,30)
            auto tt = [&]() -> const Entity* {
                for (const auto& e : dt.entities())
                    if (e.meta.value("table").toBool()) return &e;
                return nullptr;
            };
            et.setTool(ToolId::Select);
            // The MIDDLE row's cell, so its top edge is a real interior boundary at y = 20.
            et.onPointerPress({15, 15}, Qt::LeftButton, Qt::NoModifier);
            et.onPointerRelease({15, 15}, Qt::LeftButton);
            const double topBefore = tt()->bounds().max.y;
            et.onPointerPress({15, 20}, Qt::LeftButton, Qt::NoModifier);   // its TOP edge
            et.onPointerMove({15, 16});                                    // pull it down
            et.onPointerPress({15, 16}, Qt::LeftButton, Qt::NoModifier);
            const double topAfter = tt()->bounds().max.y;
            std::printf("  table-anchor: top edge %.1f -> %.1f\n", topBefore, topAfter);
            check(std::abs(topAfter - topBefore) < 1e-6,
                  "table-anchor: dragging a cell's top edge does not move the table");
        }

        // ---- an EDGE cell has a handle on the table's own edge too ----
        //
        // Only the interior boundaries used to get a handle, so a cell in the first column had one on its
        // right and nothing on its left: the edge it shares with the table could not be moved, and that is
        // the edge a first column is usually widened by. Boundary 0 is that edge.
        ed.onKey(Qt::Key_Escape, Qt::NoModifier);
        ed.onPointerPress({10, 95}, Qt::LeftButton, Qt::NoModifier);   // pick cell 0, the corner cell
        ed.onPointerRelease({10, 95}, Qt::LeftButton);
        check(ed.selectedCellIndex() == 0, "table-edge: the corner cell is picked");
        const double leftBefore = table()->bounds().min.x;
        ed.onPointerPress({0, 95}, Qt::LeftButton, Qt::NoModifier);    // the table's own left edge
        ed.onPointerMove({-8, 95});
        ed.onPointerPress({-8, 95}, Qt::LeftButton, Qt::NoModifier);
        const double leftAfter = table()->bounds().min.x;
        std::printf("  table-edge: left edge %.1f -> %.1f\n", leftBefore, leftAfter);
        check(leftAfter < leftBefore - 1.0,
              "table-edge: the outer boundary of an edge cell can be caught and moved");
    }


    // ---- point the way, type how far ---------------------------------------------------------------
    {
        // This is how a drawing is actually moved, and it is the one thing a mouse cannot do: the direction is
        // easy to aim and the distance is impossible to hit. So the direction is POINTED with the cursor and the
        // distance is TYPED, and the object goes exactly that far that way.
        //
        // Both ways of moving something take it: the Move tool with its base point down, and anything taken hold
        // of on the sheet by a grip.
        {
            Document d;
            Entity l;
            l.id = QStringLiteral("l");
            l.type = EntityType::Line;
            l.a = {0.0, 0.0};
            l.b = {10.0, 0.0};
            d.entities().push_back(l);
            Editor ed;
            ed.setDocument(&d);
            noSnap(ed);
            ed.onPointerPress({5.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerRelease({5.0, 0.0}, Qt::LeftButton);
            check(ed.selection().size() == 1, "far: the line is picked");
            ed.setTool(ToolId::Move);
            ed.onPointerPress({5.0, 0.0}, Qt::LeftButton, Qt::NoModifier);   // the base point
            // Point up and to the right, at a distance that is nothing like the one that will be typed - the
            // cursor gives the DIRECTION and only the direction.
            ed.onPointerMove({8.0, 4.0});
            check(ed.movingByDistance(), "far: with the base point down, it is waiting for a distance");
            check(!ed.drawInputFieldLabels().empty() &&
                      ed.drawInputFieldLabels().front() == QStringLiteral("Distance"),
                  "far: and the field says so, rather than sitting blank");
            check(ed.acceptCommandInput(QStringLiteral("50")), "far: a typed number is taken as the distance");
            const Entity* moved = nullptr;
            for (const Entity& e : d.entities())
                if (e.id == QStringLiteral("l")) moved = &e;
            check(moved != nullptr, "far: the line is still there");
            if (moved) {
                // 3,4 is a 5 long direction, so 50 along it is exactly 30,40.
                std::printf("  far: pointed 3,4 and typed 50 - moved %+.1f,%+.1f\n", moved->a.x - 0.0,
                            moved->a.y - 0.0);
                check(std::abs(moved->a.x - 30.0) < 1e-6 && std::abs(moved->a.y - 40.0) < 1e-6,
                      "far: it went 50 along the direction pointed, not to where the cursor was");
                check(std::abs(distance(moved->a, moved->b) - 10.0) < 1e-6,
                      "far: and it is still the line it was - moved, not stretched");
            }
        }
        {
            // And a grip: the same thing, measured from where the grab began rather than from wherever the
            // cursor has got to since.
            Document d;
            Entity l;
            l.id = QStringLiteral("l");
            l.type = EntityType::Line;
            l.a = {0.0, 0.0};
            l.b = {10.0, 0.0};
            d.entities().push_back(l);
            Editor ed;
            ed.setDocument(&d);
            noSnap(ed);
            ed.onPointerPress({5.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerRelease({5.0, 0.0}, Qt::LeftButton);
            ed.onPointerPress({10.0, 0.0}, Qt::LeftButton, Qt::NoModifier);   // its right-hand end
            ed.onPointerMove({10.0, 3.0});                                    // pointed straight up
            check(ed.movingByDistance(), "far: a grabbed joint is waiting for a distance too");
            check(ed.acceptCommandInput(QStringLiteral("25")), "far: and takes one");
            const Entity* moved = nullptr;
            for (const Entity& e : d.entities())
                if (e.id == QStringLiteral("l")) moved = &e;
            if (moved) {
                std::printf("  far: a joint pointed up and typed 25 - now at %.1f,%.1f\n", moved->b.x,
                            moved->b.y);
                check(std::abs(moved->b.x - 10.0) < 1e-6 && std::abs(moved->b.y - 25.0) < 1e-6,
                      "far: 25 straight up from where the grab began, not from where the cursor was");
            }
            check(!ed.movingByDistance(), "far: and the move is finished - it is not still in hand");
        }
        {
            // Nothing being moved: a number is not a distance, and must fall through to whatever else a typed
            // number means rather than being swallowed.
            Document d;
            Editor ed;
            ed.setDocument(&d);
            noSnap(ed);
            check(!ed.movingByDistance(), "far: with nothing in hand there is no distance to give");
        }
    }


    // ---- scaled, the writing goes with it ----------------------------------------------------------
    {
        // Scaling something is asking for the SAME DRAWING at another size. A text height is a length like any
        // other, and it was being left behind: a table scaled up kept its cells' words at the size they were, so
        // the words no longer fitted the boxes they were in and spilled over the lines into the row below. Which
        // is not the same drawing bigger, it is a drawing with the wrong writing on it.
        Document d;
        // A table with its own text height, one cell given a height of its own, and a row given a fixed height.
        Entity t;
        t.id = QStringLiteral("t");
        t.type = EntityType::Rect;
        t.points = {{0.0, 0.0}, {100.0, 0.0}, {100.0, 40.0}, {0.0, 40.0}};
        t.meta.insert(QStringLiteral("table"), true);
        t.meta.insert(QStringLiteral("textSize"), 4.0);
        QJsonArray cellSizes;
        cellSizes.append(0.0);
        cellSizes.append(6.0);   // this one was set by hand
        t.meta.insert(QStringLiteral("cellTextSize"), cellSizes);
        QJsonArray rowFixed;
        rowFixed.append(10.0);
        t.meta.insert(QStringLiteral("rowFixed"), rowFixed);
        d.entities().push_back(t);
        // And a plain piece of text, a dimension and a balloon, which had the same fault.
        Entity words;
        words.id = QStringLiteral("w");
        words.type = EntityType::Text;
        words.a = {0.0, 0.0};
        words.meta.insert(QStringLiteral("text"), QStringLiteral("LABEL TABLE"));
        words.meta.insert(QStringLiteral("size"), 5.0);
        d.entities().push_back(words);
        Entity dim;
        dim.id = QStringLiteral("dm");
        dim.type = EntityType::Dimension;
        dim.a = {0.0, 0.0};
        dim.b = {50.0, 0.0};
        dim.meta.insert(QStringLiteral("textHeight"), 3.5);
        dim.meta.insert(QStringLiteral("arrowSize"), 2.0);
        d.entities().push_back(dim);

        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.selectAll();
        ed.scaleSelectionForTest(Vec2{0.0, 0.0}, 3.0);

        auto meta = [&](const QString& id, const QString& key) {
            for (const Entity& e : d.entities())
                if (e.id == id) return e.meta.value(key).toDouble();
            return 0.0;
        };
        auto arr0 = [&](const QString& id, const QString& key, int i) {
            for (const Entity& e : d.entities())
                if (e.id == id) return e.meta.value(key).toArray()[i].toDouble();
            return 0.0;
        };
        std::printf("  scaled: table text %.1f, hand-set cell %.1f, fixed row %.1f\n",
                    meta(QStringLiteral("t"), QStringLiteral("textSize")),
                    arr0(QStringLiteral("t"), QStringLiteral("cellTextSize"), 1),
                    arr0(QStringLiteral("t"), QStringLiteral("rowFixed"), 0));
        check(std::abs(meta(QStringLiteral("t"), QStringLiteral("textSize")) - 12.0) < 1e-6,
              "scaled: the table's words are three times the size, as the table is");
        check(std::abs(arr0(QStringLiteral("t"), QStringLiteral("cellTextSize"), 1) - 18.0) < 1e-6,
              "scaled: including a cell somebody had set by hand - or that one alone would stay behind");
        check(std::abs(arr0(QStringLiteral("t"), QStringLiteral("cellTextSize"), 0)) < 1e-9,
              "scaled: and a cell with no height of its own still has none - zero means 'the table's'");
        check(std::abs(arr0(QStringLiteral("t"), QStringLiteral("rowFixed"), 0) - 30.0) < 1e-6,
              "scaled: a row held at a height is held at three times it, or the words would not fit it");
        std::printf("  scaled: text %.1f, dimension text %.1f, arrow %.1f\n",
                    meta(QStringLiteral("w"), QStringLiteral("size")),
                    meta(QStringLiteral("dm"), QStringLiteral("textHeight")),
                    meta(QStringLiteral("dm"), QStringLiteral("arrowSize")));
        check(std::abs(meta(QStringLiteral("w"), QStringLiteral("size")) - 15.0) < 1e-6,
              "scaled: a piece of text is three times the size too - it had the same fault");
        check(std::abs(meta(QStringLiteral("dm"), QStringLiteral("textHeight")) - 10.5) < 1e-6 &&
                  std::abs(meta(QStringLiteral("dm"), QStringLiteral("arrowSize")) - 6.0) < 1e-6,
              "scaled: and a dimension's words and arrows with it");
    }

    // --- Connection pin: numbered lowest-free-first, and it stays armed for the next one.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); noSnap(ed);
        ed.setTool(ToolId::ConnectionPin);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({10, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({20, 0}, Qt::LeftButton, Qt::NoModifier);
        check(ed.tool() == ToolId::ConnectionPin,
              "pin: stays armed for the next one, like the X terminator does");
        auto pinNumbers = [&] {
            std::vector<int> nums;
            for (const auto& e : d.entities())
                if (e.type == EntityType::ConnectionPin) nums.push_back(e.meta.value("pinNumber").toInt());
            std::sort(nums.begin(), nums.end());
            return nums;
        };
        std::vector<int> nums = pinNumbers();
        check(nums.size() == 3 && nums[0] == 1 && nums[1] == 2 && nums[2] == 3,
              "pin: three placed in a row get 1, 2, 3");

        // Delete pin 2, then place another - it should get 2 back, not a new highest number.
        QString midId;
        for (const auto& e : d.entities())
            if (e.type == EntityType::ConnectionPin && e.meta.value("pinNumber").toInt() == 2)
                midId = e.id;
        check(!midId.isEmpty(), "pin: found the one to delete");
        ed.selectById(midId);
        ed.deleteSelected();
        ed.setTool(ToolId::ConnectionPin);
        ed.onPointerPress({30, 0}, Qt::LeftButton, Qt::NoModifier);
        nums = pinNumbers();
        check(nums.size() == 3 && nums[0] == 1 && nums[1] == 2 && nums[2] == 3,
              "pin: deleting 2 and placing another gives 2 back rather than jumping to 4");
    }




    // ---- a BLOCK of a connector symbol scales as one thing ------------------------------------------
    {
        // A connector placed from the library is a BLOCK: the symbol, the box round it and the part number
        // on it, stamped as one copy so a click picks the whole thing. Scaling it has to take all of them
        // together - the reported fault is that it does not, and the way to find out is to scale one and
        // measure every piece of it rather than to look at a picture of it.
        Document d;
        auto stamp = [](Entity& e) {
            e.meta.insert(QStringLiteral("block"), QStringLiteral("X (schematic)"));
            e.meta.insert(QStringLiteral("blockInst"), QStringLiteral("inst-1"));
            e.meta.insert(QStringLiteral("blockOriginX"), 0.0);
            e.meta.insert(QStringLiteral("blockOriginY"), 0.0);
        };
        Entity box;
        box.id = QStringLiteral("box");
        box.type = EntityType::Rect;
        box.points = {{0.0, 0.0}, {60.0, 0.0}, {60.0, 40.0}, {0.0, 40.0}};
        stamp(box);
        d.entities().push_back(box);
        Entity pn;
        pn.id = QStringLiteral("pn");
        pn.type = EntityType::Text;
        pn.a = {0.0, 44.0};
        pn.meta.insert(QStringLiteral("text"), QStringLiteral("0436450300"));
        pn.meta.insert(QStringLiteral("size"), 5.0);
        stamp(pn);
        d.entities().push_back(pn);
        Entity conn;
        conn.id = QStringLiteral("conn");
        conn.type = EntityType::Connector;
        conn.a = {10.0, 30.0};
        conn.meta.insert(QStringLiteral("pinCount"), 3);
        conn.meta.insert(QStringLiteral("pitch"), 8.0);
        conn.meta.insert(QStringLiteral("pinLength"), 12.0);
        conn.meta.insert(QStringLiteral("textSize"), 4.0);
        stamp(conn);
        d.entities().push_back(conn);

        auto extent = [&d](const QString& id) {
            for (const Entity& e : d.entities())
                if (e.id == id) return e.bounds();
            return Bounds();
        };
        const Bounds boxBefore = extent(QStringLiteral("box"));
        const Bounds pnBefore = extent(QStringLiteral("pn"));
        const Bounds connBefore = extent(QStringLiteral("conn"));

        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.selectAll();
        ed.scaleSelectionForTest(Vec2{0.0, 0.0}, 2.0);

        auto grew = [](const Bounds& before, const Bounds& after, const char* what) {
            if (!before.valid || !after.valid) return -1.0;
            const double w0 = std::max(1e-9, before.width()), w1 = after.width();
            std::printf("  block scale: %s %.1f wide -> %.1f (x%.2f)\n", what, w0, w1, w1 / w0);
            return w1 / w0;
        };
        const double boxF = grew(boxBefore, extent(QStringLiteral("box")), "box");
        const double pnF = grew(pnBefore, extent(QStringLiteral("pn")), "part number");
        const double connF = grew(connBefore, extent(QStringLiteral("conn")), "connector");
        check(std::abs(boxF - 2.0) < 0.02, "block scale: the box is twice the size");
        check(std::abs(pnF - 2.0) < 0.05, "block scale: and the part number written on it");
        check(connF > 0.0 && std::abs(connF - 2.0) < 0.05,
              "block scale: and the connector symbol inside it, by the same factor as the rest");
    }


    // ---- a leader is its words as much as its line ---------------------------------------------------
    {
        // A leader is drawn as a line to what it points at, a flat shelf at the far end, and the words
        // sitting on that shelf. Only the LINE could be picked: clicking the words - the obvious thing to
        // click, and the only part of a leader with any size to it - selected nothing, and double-clicking
        // them to change them did nothing either, because the double-click asks what was hit first.
        Document d;
        Entity ld;
        ld.id = QStringLiteral("ld");
        ld.type = EntityType::Leader;
        ld.points = {{0.0, 0.0}, {40.0, 30.0}};   // tip at the origin, knee up and to the right
        ld.meta.insert(QStringLiteral("text"), QStringLiteral("TERMINATE HERE"));
        ld.meta.insert(QStringLiteral("size"), 10.0);
        ld.meta.insert(QStringLiteral("landing"), 6.0);
        d.entities().push_back(ld);

        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        QString edited;
        QObject::connect(&ed, &Editor::editTextRequested, &ed, [&edited](const QString& id) { edited = id; });

        // ON THE SHELF: from the knee at 40,30 running 6 further right.
        ed.clearSelection();
        ed.onPointerPress({43.0, 30.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerRelease({43.0, 30.0}, Qt::LeftButton);
        std::printf("  leader: clicking the shelf selected %d object(s)\n",
                    static_cast<int>(ed.selection().size()));
        check(ed.selection().size() == 1, "leader: clicking the shelf it lands on selects it");

        // ON THE WORDS: they start where the shelf ends and run right, about six tenths of the height per
        // character - so a point a couple of characters in is on them.
        ed.clearSelection();
        ed.onPointerPress({58.0, 33.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerRelease({58.0, 33.0}, Qt::LeftButton);
        std::printf("  leader: clicking the words selected %d object(s)\n",
                    static_cast<int>(ed.selection().size()));
        check(ed.selection().size() == 1, "leader: and clicking its words selects it too");

        // AND DOUBLE-CLICKING THE WORDS OPENS THEM. The same gesture that edits any other text.
        ed.onDoubleClick({58.0, 33.0});
        std::printf("  leader: double-clicking the words asked to edit '%s'\n", qPrintable(edited));
        check(edited == QStringLiteral("ld"),
              "leader: double-clicking its words opens the text editor on it");

        // A point well clear of all of it still selects nothing.
        ed.clearSelection();
        ed.onPointerPress({200.0, 200.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerRelease({200.0, 200.0}, Qt::LeftButton);
        check(ed.selection().empty(), "leader: and empty space still selects nothing");
    }

    // ---- the house text height ----------------------------------------------------------------------
    {
        // Five is the figure a small drawing wants and half of what an A3 sheet wants. A size somebody has
        // to change on every piece of text they write is not a default, it is a nuisance.
        const Editor::TextStyle fresh;
        std::printf("  text height: %.1f\n", fresh.size);
        check(std::abs(fresh.size - 10.0) < 1e-9, "text height: ten, out of the box");
    }


    // ---- a leader's edited words are KEPT ------------------------------------------------------------
    {
        // The editor opened, the words were typed, the commit ran - and the write dropped them on the floor,
        // because it knew about a Text object and about three kinds of annotation that carry a text value,
        // and a leader was not one of them. Nothing said so, which is the worst of it: the new words stayed
        // on the screen until the drawing was next redrawn from the entity, and then they were the old ones
        // again. The same silence whether the edit was finished with Enter or by clicking the canvas, since
        // both end in the same write.
        Document d;
        Entity ld;
        ld.id = QStringLiteral("ld");
        ld.type = EntityType::Leader;
        ld.points = {{0.0, 0.0}, {30.0, 20.0}};
        ld.meta.insert(QStringLiteral("text"), QStringLiteral("Text"));
        ld.meta.insert(QStringLiteral("size"), 10.0);
        ld.meta.insert(QStringLiteral("landing"), 6.0);
        d.entities().push_back(ld);

        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        Editor::TextStyle style;
        style.size = 12.0;
        style.font = QStringLiteral("Arial");
        ed.applyTextEdit(QStringLiteral("ld"), QStringLiteral("CRIMP TO SPEC"), style);

        auto meta = [&d](const QString& key) {
            for (const Entity& e : d.entities())
                if (e.id == QStringLiteral("ld")) return e.meta.value(key);
            return QJsonValue();
        };
        std::printf("  leader save: says '%s' at %.1f in %s\n",
                    qPrintable(meta(QStringLiteral("text")).toString()),
                    meta(QStringLiteral("size")).toDouble(),
                    qPrintable(meta(QStringLiteral("font")).toString()));
        check(meta(QStringLiteral("text")).toString() == QStringLiteral("CRIMP TO SPEC"),
              "leader save: the words that were typed are the words it has");
        check(std::abs(meta(QStringLiteral("size")).toDouble() - 12.0) < 1e-9,
              "leader save: and the height set in the editor, not the one it had");
        check(meta(QStringLiteral("font")).toString() == QStringLiteral("Arial"),
              "leader save: and the font");
        // Cleared, a leader stays a leader - a bare arrow pointing at something is a real thing to draw,
        // unlike a piece of text with nothing written on it.
        ed.applyTextEdit(QStringLiteral("ld"), QString(), style);
        int leaders = 0;
        for (const Entity& e : d.entities())
            if (e.type == EntityType::Leader) ++leaders;
        check(leaders == 1, "leader save: emptied of words it is still there - a bare arrow is a drawing");
    }


    // ---- with no landing, the line runs INTO the words -----------------------------------------------
    {
        // The last thing a leader with no shelf has is its end point, and that point is where the words
        // start - or finish, coming the other way. They used to be drawn four SCREEN pixels off it and two
        // up, which is not a distance at all: it grows and shrinks with the zoom, so the arrow floated clear
        // of its own words close up and lay across them far out.
        Document d;
        Entity ld;
        ld.id = QStringLiteral("ld");
        ld.type = EntityType::Leader;
        ld.points = {{0.0, 0.0}, {30.0, 20.0}};   // coming up from the left: the words read to the right
        ld.meta.insert(QStringLiteral("text"), QStringLiteral("ABCD"));
        ld.meta.insert(QStringLiteral("size"), 10.0);
        ld.meta.insert(QStringLiteral("landing"), 0.0);
        d.entities().push_back(ld);
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);

        const Bounds words = ed.leaderWordsBox(d.entities().front());
        check(words.valid, "no landing: the words have a place");
        if (words.valid) {
            const Vec2 end{30.0, 20.0};
            std::printf("  no landing: end at %.1f,%.1f; words x %.1f..%.1f, y %.1f..%.1f\n", end.x, end.y,
                        words.min.x, words.max.x, words.min.y, words.max.y);
            // Alongside the end, on the side the leader is reading towards - a quarter of the text height
            // clear of it and no more.
            check(words.min.x > end.x && words.min.x - end.x < 10.0 * 0.3,
                  "no landing: the words start just past the end of the line, on the reading side");
            // AND CENTRED ON IT: the line meets the middle of the first letter, not its foot.
            check(std::abs((words.min.y + words.max.y) / 2.0 - end.y) < 1e-9,
                  "no landing: and centred on the line, so it runs into them");
        }
        // Coming the other way, the words read to the left and finish at the end point.
        Entity& back = d.entities().front();
        back.points = {{60.0, 0.0}, {30.0, 20.0}};
        const Bounds left = ed.leaderWordsBox(back);
        if (left.valid) {
            std::printf("  no landing: coming from the right, words x %.1f..%.1f\n", left.min.x, left.max.x);
            check(left.max.x < 30.0,
                  "no landing: from the other side they sit before the end, right-aligned on it");
        }
    }


    // ---- a typed angle is clockwise, and Space turns what is being moved ----------------------------
    {
        // "BRING IT ROUND THIRTY" means clockwise. That is how an angle is said when somebody is turning a
        // part on a drawing, and it is the opposite of the way angles are measured in the arithmetic
        // underneath, where anticlockwise is positive - so the conversion belongs at the one place a person
        // types a number.
        Document d;
        Entity line;
        line.id = QStringLiteral("l");
        line.type = EntityType::Line;
        line.a = {0.0, 0.0};
        line.b = {10.0, 0.0};   // pointing along +x
        d.entities().push_back(line);
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.selectAll();
        ed.setTool(ToolId::Rotate);
        ed.onPointerPress({0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);   // the pivot
        check(ed.acceptCommandInput(QStringLiteral("90")), "turn: a typed angle is taken as the angle");
        auto lineNow = [&d] {
            for (const Entity& e : d.entities())
                if (e.id == QStringLiteral("l")) return e;
            return Entity();
        };
        Entity after = lineNow();
        std::printf("  turn: 90 typed took (10,0) to (%.1f,%.1f)\n", after.b.x, after.b.y);
        check(std::abs(after.b.x) < 1e-6 && std::abs(after.b.y + 10.0) < 1e-6,
              "turn: 90 goes clockwise - what pointed along +x now points DOWN the sheet");

        // And a negative one the other way.
        ed.selectAll();
        ed.setTool(ToolId::Rotate);
        ed.onPointerPress({0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
        ed.acceptCommandInput(QStringLiteral("-90"));
        after = lineNow();
        std::printf("  turn: then -90 took it to (%.1f,%.1f)\n", after.b.x, after.b.y);
        check(std::abs(after.b.x - 10.0) < 1e-6 && std::abs(after.b.y) < 1e-6,
              "turn: -90 goes the other way, and puts it back");
    }

    // ---- Space, mid-move, turns the thing being carried ---------------------------------------------
    {
        // A part is nearly always picked up to be put down THE OTHER WAY ROUND - a connector facing the
        // other end of the loom, a label reading up the sheet instead of across it. Doing that as two
        // commands means dropping it, turning it, and moving it again to where it should have gone.
        Document d;
        Entity line;
        line.id = QStringLiteral("l");
        line.type = EntityType::Line;
        line.a = {0.0, 0.0};
        line.b = {10.0, 0.0};
        d.entities().push_back(line);
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.selectAll();
        ed.setTool(ToolId::Move);
        ed.onPointerPress({0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);   // held by its left-hand end
        ed.onPointerMove({50.0, 50.0});                                  // carried out to here
        check(ed.movingByDistance(), "space turn: a move is under way");
        check(ed.onKey(Qt::Key_Space, Qt::NoModifier), "space turn: Space is taken while it is");
        auto lineNow = [&d] {
            for (const Entity& e : d.entities())
                if (e.id == QStringLiteral("l")) return e;
            return Entity();
        };
        Entity after = lineNow();
        std::printf("  space turn: picked up at 0,0 and carried to 50,50, the far end is still (%.1f,%.1f)\n", after.b.x, after.b.y);
        // NOTHING REAL HAS TURNED YET. Under the Move tool the objects have not gone anywhere - what is
        // at the cursor is the preview - so Space turns the preview and the original stands untouched
        // until it is put down. Turning it here spun the real thing on the far side of the sheet and
        // left an undo step behind for a move nobody had finished.
        check(std::abs(after.a.x) < 1e-6 && std::abs(after.a.y) < 1e-6 &&
                  std::abs(after.b.x - 10.0) < 1e-6 && std::abs(after.b.y) < 1e-6,
              "space turn: the original is untouched while the move is in the air");

        // PUT IT DOWN, and the turn lands with the move: held by its left-hand end and carried to
        // (50,50), the end that was 10 to the +x side is now 10 BELOW it.
        ed.onPointerPress({50.0, 50.0}, Qt::LeftButton, Qt::NoModifier);
        after = lineNow();
        std::printf("  space turn: placed at 50,50 with the far end at (%.1f,%.1f)\n", after.b.x, after.b.y);
        check(std::abs(after.a.x - 50.0) < 1e-6 && std::abs(after.a.y - 50.0) < 1e-6,
              "space turn: the point it was held by lands where it was carried to");
        check(std::abs(after.b.x - 50.0) < 1e-6 && std::abs(after.b.y - 40.0) < 1e-6,
              "space turn: a quarter turn clockwise, applied when it was placed");
        // AND THE OTHER WAY, on a move of its own. Shift+Space is anticlockwise, and it is worth its
        // own pick-up now: the turn belongs to the move that is in the air, so once the thing is down
        // there is nothing left to turn.
        // The Move tool is one-shot, so it is armed again - otherwise the press below is a DRAG, and a
        // drag turns the objects themselves because there they are the preview.
        ed.selectAll();
        ed.setTool(ToolId::Move);
        ed.onPointerPress({50.0, 50.0}, Qt::LeftButton, Qt::NoModifier);   // held by the same end
        ed.onPointerMove({100.0, 100.0});
        check(ed.onKey(Qt::Key_Space, Qt::ShiftModifier), "space turn: Shift+Space is taken too");
        after = lineNow();
        check(std::abs(after.b.x - 50.0) < 1e-6 && std::abs(after.b.y - 40.0) < 1e-6,
              "space turn: and it too leaves the original alone until it is put down");
        ed.onPointerPress({100.0, 100.0}, Qt::LeftButton, Qt::NoModifier);
        after = lineNow();
        std::printf("  space turn: Shift+Space placed at 100,100 with the far end at (%.1f,%.1f)\n",
                    after.b.x, after.b.y);
        // The end sat 10 BELOW the point it is held by; a quarter turn anticlockwise puts it 10 to the
        // +x side of it.
        check(std::abs(after.b.x - 110.0) < 1e-6 && std::abs(after.b.y - 100.0) < 1e-6,
              "space turn: and Shift+Space turns it back anticlockwise");

        // AND THE PREVIEW SHOWS IT, which is the whole point of pressing Space before putting the
        // thing down. Checking only where the object LANDS cannot tell a preview that turns from one
        // that does not - and it did not: the turn was recorded and applied on placement while the
        // ghost at the cursor went on lying flat, so the only way to find out what Space had done was
        // to do it and see.
        {
            Document d2;
            Entity r;
            r.id = QStringLiteral("r");
            r.type = EntityType::Line;
            r.a = {0.0, 0.0};
            r.b = {10.0, 0.0};
            d2.entities().push_back(r);
            Editor ed2;
            ed2.setDocument(&d2);
            noSnap(ed2);
            ed2.selectAll();
            ed2.setTool(ToolId::Move);
            ed2.onPointerPress({0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);   // held by its left end
            ed2.onPointerMove({50.0, 50.0});
            auto preview = ed2.modifyPreview();
            check(preview.size() == 1, "space preview: one object is previewed at the cursor");
            if (preview.size() == 1) {
                std::printf("  space preview: before Space the ghost is (%.1f,%.1f)..(%.1f,%.1f)\n",
                            preview[0].a.x, preview[0].a.y, preview[0].b.x, preview[0].b.y);
                check(std::abs(preview[0].a.x - 50.0) < 1e-6 &&
                          std::abs(preview[0].b.x - 60.0) < 1e-6 &&
                          std::abs(preview[0].b.y - 50.0) < 1e-6,
                      "space preview: carried to the cursor, still lying flat");
            }
            ed2.onKey(Qt::Key_Space, Qt::NoModifier);
            preview = ed2.modifyPreview();
            if (preview.size() == 1) {
                std::printf("  space preview: after Space it is (%.1f,%.1f)..(%.1f,%.1f)\n",
                            preview[0].a.x, preview[0].a.y, preview[0].b.x, preview[0].b.y);
                // Turned a quarter clockwise about the point it is held by, then carried: the end
                // that was 10 to the +x side is now 10 BELOW the hand.
                check(std::abs(preview[0].a.x - 50.0) < 1e-6 &&
                          std::abs(preview[0].a.y - 50.0) < 1e-6 &&
                          std::abs(preview[0].b.x - 50.0) < 1e-6 &&
                          std::abs(preview[0].b.y - 40.0) < 1e-6,
                      "space preview: and the GHOST turns, not just the thing that lands");
            }
            // The original is still untouched behind it, and the two agree when it is put down: what
            // was on screen is what lands.
            const Entity ghostWas = preview.empty() ? Entity() : preview[0];
            ed2.onPointerPress({50.0, 50.0}, Qt::LeftButton, Qt::NoModifier);
            const Entity& landed = d2.entities().front();
            std::printf("  space preview: placed as (%.1f,%.1f)..(%.1f,%.1f)\n", landed.a.x,
                        landed.a.y, landed.b.x, landed.b.y);
            check(std::abs(landed.a.x - ghostWas.a.x) < 1e-6 &&
                      std::abs(landed.a.y - ghostWas.a.y) < 1e-6 &&
                      std::abs(landed.b.x - ghostWas.b.x) < 1e-6 &&
                      std::abs(landed.b.y - ghostWas.b.y) < 1e-6,
                  "space preview: what was on screen is exactly what was put down");
            // FOUR PRESSES COME BACK ROUND. The preview turning is what tells somebody they have
            // pressed it enough - without it they press until something happens, which is never.
            ed2.selectAll();
            ed2.setTool(ToolId::Move);
            ed2.onPointerPress({50.0, 50.0}, Qt::LeftButton, Qt::NoModifier);
            ed2.onPointerMove({80.0, 80.0});
            const Entity flat = ed2.modifyPreview().front();
            for (int i = 0; i < 4; ++i) ed2.onKey(Qt::Key_Space, Qt::NoModifier);
            const Entity round = ed2.modifyPreview().front();
            std::printf("  space preview: four presses -> (%.1f,%.1f)..(%.1f,%.1f)\n", round.a.x,
                        round.a.y, round.b.x, round.b.y);
            check(std::abs(round.a.x - flat.a.x) < 1e-6 && std::abs(round.a.y - flat.a.y) < 1e-6 &&
                      std::abs(round.b.x - flat.b.x) < 1e-6 && std::abs(round.b.y - flat.b.y) < 1e-6,
                  "space preview: four quarter turns is a whole one, and the ghost is back");
        }
    }

    // ---- Space turns what is being PASTED, the same as what is being moved -----------------------
    {
        // A block off the clipboard arrives the way it was drawn and is very often wanted the other
        // way round. The turn was plumbed the whole way through - the ghost is drawn with it, the
        // placement commits with it - and no key was bound to it, so nothing waiting to be placed
        // could be turned at all.
        Document d;
        Entity line;
        line.id = QStringLiteral("l");
        line.type = EntityType::Line;
        line.a = {0.0, 0.0};
        line.b = {10.0, 0.0};
        d.entities().push_back(line);
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.selectAll();
        ed.copySelection();          // base point is the selection's bottom-left: (0,0)
        ed.paste();
        check(ed.placingPaste(), "paste turn: a paste is waiting to be placed");
        check(ed.placementPreviewActive(), "paste turn: which is a placement preview");
        ed.onPointerMove({50.0, 50.0});
        check(std::abs(ed.placementRotation()) < 1e-12,
              "paste turn: it comes off the clipboard the way it was drawn");

        // SPACE TURNS IT, and only the preview: nothing has been placed, so the drawing still holds
        // the one line it started with.
        check(ed.onKey(Qt::Key_Space, Qt::NoModifier), "paste turn: Space is taken while it waits");
        std::printf("  paste turn: after Space the preview is turned %.1f deg, %d object(s) on the "
                    "sheet\n",
                    ed.placementRotation() * 180.0 / M_PI, int(d.entities().size()));
        check(std::abs(ed.placementRotation() + M_PI / 2.0) < 1e-9,
              "paste turn: a quarter turn clockwise");
        check(d.entities().size() == 1, "paste turn: and nothing has landed yet");

        // PUT IT DOWN, and the turn lands with it. Held by (0,0) and dropped at (50,50), the end that
        // was 10 to the +x side is now 10 below.
        ed.onPointerPress({50.0, 50.0}, Qt::LeftButton, Qt::NoModifier);
        check(d.entities().size() == 2, "paste turn: the click places the copy");
        if (d.entities().size() == 2) {
            const Entity& put = d.entities().back();
            std::printf("  paste turn: placed as (%.1f,%.1f)..(%.1f,%.1f)\n", put.a.x, put.a.y,
                        put.b.x, put.b.y);
            check(std::abs(put.a.x - 50.0) < 1e-6 && std::abs(put.a.y - 50.0) < 1e-6 &&
                      std::abs(put.b.x - 50.0) < 1e-6 && std::abs(put.b.y - 40.0) < 1e-6,
                  "paste turn: it lands turned the way the preview was");
        }
        // And the next paste starts square again, rather than carrying the last one's turn.
        ed.paste();
        check(std::abs(ed.placementRotation()) < 1e-12,
              "paste turn: something picked up next starts the way it was drawn");

        // SHIFT+SPACE THE OTHER WAY, and four presses come back round - the same as Move, because a
        // person who has learnt the key there should not have to learn a second one here.
        check(ed.onKey(Qt::Key_Space, Qt::ShiftModifier), "paste turn: Shift+Space is taken too");
        check(std::abs(ed.placementRotation() - M_PI / 2.0) < 1e-9,
              "paste turn: and goes anticlockwise");
        for (int i = 0; i < 3; ++i) ed.onKey(Qt::Key_Space, Qt::ShiftModifier);
        std::printf("  paste turn: four anticlockwise presses leave it at %.1f deg\n",
                    ed.placementRotation() * 180.0 / M_PI);
        check(std::abs(ed.placementRotation()) < 1e-9,
              "paste turn: four quarter turns is a whole one, and it is back where it started");
    }

    // ---- A heatshrink sleeve is caught by the lines it actually draws -----------------------------
    {
        // A sleeve stores the ROUTE it was drawn along and works its outline out from that: a body of
        // one width recovered down to a narrower face at each end. The shape on the screen is nowhere
        // in the stored points - so a click, a green zone and a snap were all answering about a line
        // down the middle that is never drawn. A zone laid across the end of a sleeve caught nothing.
        Document d;
        Entity s;
        s.id = QStringLiteral("SLV");
        s.type = EntityType::Polyline;   // a sleeve is a polyline carrying "double", not its own type
        s.points = {{0.0, 0.0}, {100.0, 0.0}};
        s.meta.insert(QStringLiteral("shrunkEnds"), true);
        s.meta.insert(QStringLiteral("double"), true);
        s.meta.insert(QStringLiteral("gap"), 20.0);          // 20 across the body, so ±10
        s.meta.insert(QStringLiteral("shrinkFaceStart"), 8.0);
        s.meta.insert(QStringLiteral("shrinkFaceEnd"), 8.0);
        s.meta.insert(QStringLiteral("shrinkTaperStart"), 15.0);
        s.meta.insert(QStringLiteral("shrinkTaperEnd"), 15.0);
        d.entities().push_back(s);

        const std::vector<Vec2> outline = shrunkSleeveOutlineOf(d.entities().front());
        std::printf("  sleeve: its outline has %d corner(s)\n", int(outline.size()));
        check(outline.size() >= 8, "sleeve: it has an outline of its own, worked out from the route");

        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);

        // ---- A CLICK ON ITS EDGE. The body rail is 10 off the route; the route itself is empty air.
        ed.clearSelection();
        ed.onPointerPress({50.0, 10.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerRelease({50.0, 10.0}, Qt::LeftButton);
        std::printf("  sleeve: a click on the top rail picked %d\n", int(ed.selection().size()));
        check(ed.selection().size() == 1, "sleeve: a click on the line it draws picks it");

        // ---- A GREEN ZONE THAT ONLY TOUCHES ITS END. Across the recovered face at the right-hand
        // end, well clear of the centreline - which is what the old answer was made of.
        // Two clicks, which is how a region is given: press-release at one corner, then the other.
        ed.clearSelection();
        ed.onPointerPress({130.0, 12.0}, Qt::LeftButton, Qt::NoModifier);   // right to left = crossing
        ed.onPointerRelease({130.0, 12.0}, Qt::LeftButton);
        ed.onPointerMove({95.0, 3.0});
        ed.onPointerPress({95.0, 3.0}, Qt::LeftButton, Qt::NoModifier);
        std::printf("  sleeve: a green zone over its end picked %d\n", int(ed.selection().size()));
        check(ed.selection().size() == 1,
              "sleeve: a crossing zone that touches any of its lines catches it");

        // ---- NEAREST SNAPS ONTO THE EDGE, not onto the route. Asked just outside the top rail.
        SnapSettings sn;
        sn.objectSnap = true;
        sn.snapNearest = true;
        sn.snapEndpoint = false;
        sn.snapMidpoint = false;
        const SnapResult r = resolveSnap(d, {50.0, 13.0}, std::nullopt, 6.0, sn);
        std::printf("  sleeve: nearest from (50,13) -> (%.2f,%.2f) hit=%d\n", r.point.x, r.point.y,
                    r.snapped ? 1 : 0);
        check(r.snapped, "sleeve: nearest point finds it");
        check(std::abs(r.point.y - 10.0) < 1e-6,
              "sleeve: and lands on the rail it draws, not on the route down the middle");
    }

    // ---- Cable over the run with heatshrink: cut square, not recovered ----------------------------
    {
        // Heatshrink RECOVERS - it pulls in onto what it grips, so its ends neck down over a taper.
        // A cable running the length of the run under it does not: it is one width from end to end and
        // stops at a face square across it. Two different shapes on the sheet, and the second could
        // not be drawn at all.
        auto drawSleeve = [](bool square) {
            Document d;
            Editor ed;
            ed.setDocument(&d);
            noSnap(ed);
            ed.setShrinkSquareEnds(square);
            ed.setTool(ToolId::HeatshrinkRoute);
            ed.onPointerPress({0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerPress({100.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
            ed.onKey(Qt::Key_Return, Qt::NoModifier);
            return d.entities().empty() ? Entity() : d.entities().front();
        };
        // How far out the outline reaches at the very ends, against how far out it reaches in the
        // middle: a recovered end is narrower than the body, a square one is not.
        auto endHalf = [](const Entity& e) {
            const std::vector<Vec2> o = shrunkSleeveOutlineOf(e);
            // The BODY is the widest the outline ever gets - a two-point sleeve has no vertices in
            // the middle of its span to sample, only its end faces and the shoulders beside them.
            double atEnd = 0.0, body = 0.0;
            for (const Vec2& p : o) {
                body = std::max(body, std::abs(p.y));
                if (p.x < 1.0) atEnd = std::max(atEnd, std::abs(p.y));
            }
            return std::make_pair(atEnd, body);
        };

        const Entity shrunk = drawSleeve(false);
        const Entity cable = drawSleeve(true);
        check(!shrunk.points.empty() && !cable.points.empty(), "square sleeve: both are drawn");
        if (shrunk.points.empty() || cable.points.empty()) return g_fail;
        const auto s = endHalf(shrunk);
        const auto c = endHalf(cable);
        std::printf("  square sleeve: recovered end %.2f of body %.2f; square end %.2f of body %.2f\n",
                    s.first, s.second, c.first, c.second);
        check(s.first < s.second - 0.1,
              "square sleeve: heatshrink necks down at its end, as it always did");
        check(std::abs(c.first - c.second) < 1e-6,
              "square sleeve: a cable is the same width at its end as in the middle");
        // AND IT IS STILL A SLEEVE. Said with the numbers the outline already understands rather than
        // a flag of its own, so the properties panel still offers both ends and either can be pulled
        // back into a taper afterwards without the object changing what it is.
        check(cable.meta.value(QStringLiteral("shrunkEnds")).toBool(),
              "square sleeve: it is still a sleeve, and still editable end by end");
        check(std::abs(cable.meta.value(QStringLiteral("shrinkTaperStart")).toDouble()) < 1e-9,
              "square sleeve: with no taper at the start");
    }

    // ---- Cable over the run with heatshrink on its ends -----------------------------------------
    {
        // A third sort, beside the whole-run sleeve and the whole-run square cable: the cable is bare
        // through the middle, each end has a sleeve over it, and the transverse line where the sleeve
        // stops is part of the drawing rather than only where a colour changes.
        Document d;
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.setShrinkRouteMode(ShrinkRouteMode::CableEnds);
        // Six across, which is what these figures were measured on: this is about the SHAPE
        // of the thing, and a default width changing under it would say nothing about that.
        ed.setShrinkWidth(6.0);
        ed.setTool(ToolId::HeatshrinkRoute);
        ed.onPointerPress({0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({100.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onKey(Qt::Key_Return, Qt::NoModifier);
        check(d.entities().size() == 1, "end sleeves: the tool draws one routed object");
        if (d.entities().empty()) return g_fail;
        const Entity& cable = d.entities().front();
        check(cable.meta.value(QStringLiteral("shrunkEnds")).toBool() &&
                  cable.meta.value(QStringLiteral("shrinkEndsOnly")).toBool(),
              "end sleeves: it is still a sleeve, with the cable between two ends");
        double coverStart = 0.0, coverEnd = 0.0;
        check(sleeveEndCoverageOf(cable, coverStart, coverEnd) && near(coverStart, 20.0) &&
                  near(coverEnd, 20.0),
              "end sleeves: both sleeve lengths are set and editable");

        const auto runs = shrunkSleeveRunsOf(cable);
        check(runs.size() == 4, "end sleeves: two cable rails plus two sleeve outlines");
        auto hasPoint = [](const std::vector<Vec2>& run, const Vec2& want) {
            for (const Vec2& p : run) if (distance(p, want) < 1e-5) return true;
            return false;
        };
        int openRails = 0, closedSleeves = 0;
        for (const auto& run : runs) {
            const bool closed = run.size() >= 4 && distance(run.front(), run.back()) < 1e-6;
            closedSleeves += closed ? 1 : 0;
            if (closed) continue;
            ++openRails;
            // The bare cable is narrower than the sleeve: at +/-2.46 on a six-wide run, and only
            // between the two twenty-long sleeves.
            check(run.size() == 2 && near(run.front().x, 20.0) && near(run.back().x, 80.0) &&
                      std::abs(std::abs(run.front().y) - 2.46) < 1e-3,
                  "end sleeves: the cable shows only between the two sleeves");
        }
        check(openRails == 2 && closedSleeves == 2,
              "end sleeves: cable in the middle, sleeve over each end");
        check(hasPoint(runs[2], {20.0, 3.0}) && hasPoint(runs[2], {20.0, -3.0}),
              "end sleeves: the start sleeve has the transverse split line");
        check(hasPoint(runs[3], {80.0, 3.0}) && hasPoint(runs[3], {80.0, -3.0}),
              "end sleeves: the finish sleeve has the transverse split line");

        // WHAT IS DRAWN IS WHAT IS PICKED AND SNAPPED: a click on the exposed cable rail picks the
        // one object, and a nearest snap lands on that rail rather than on the hidden route.
        ed.clearSelection();
        ed.onPointerPress({50.0, 2.46}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerRelease({50.0, 2.46}, Qt::LeftButton);
        check(ed.selection().size() == 1, "end sleeves: clicking the visible cable picks it");
        SnapSettings sn;
        sn.objectSnap = true;
        sn.snapNearest = true;
        sn.snapEndpoint = false;
        sn.snapMidpoint = false;
        const SnapResult nearCable = resolveSnap(d, {50.0, 4.0}, std::nullopt, 5.0, sn);
        check(nearCable.snapped && near(nearCable.point.y, 2.46, 1e-3),
              "end sleeves: nearest snap lands on the visible cable rail");
        const Bounds box = cable.bounds();
        check(box.valid && near(box.max.x, 100.0) && near(box.max.y, 3.0) &&
                  near(box.min.y, -3.0),
              "end sleeves: its box is the compound linework, not the hidden route");

        const QString dxf = dxf::toDxf(d);
        int polyCount = 0;
        const QString polyMark = QStringLiteral("0\r\nPOLYLINE\r\n");
        for (int at = dxf.indexOf(polyMark); at >= 0;
             at = dxf.indexOf(polyMark, at + 1))
            ++polyCount;
        check(polyCount == 4, "end sleeves: DXF writes the two rails and both sleeve outlines");

        // THE SPLIT LINE IS A JOINT. Dragging it changes how far that end's sleeve covers the cable;
        // the other end is left where it was.
        ed.selectById(cable.id);
        auto offLinework = [](const Vec2& at, const std::vector<std::vector<Vec2>>& lines) {
            double best = 1e18;
            for (const auto& line : lines)
                for (size_t i = 1; i < line.size(); ++i)
                    best = std::min(best, closestPointOnSegment(at, line[i - 1], line[i]).distance);
            return best;
        };
        double off = 0.0;
        for (const Vec2& joint : ed.selectionGripPoints())
            off = std::max(off, offLinework(joint, runs));
        check(ed.selectionGripPoints().size() == 14,
              "end sleeves: both ends have face, shoulder and split-line joints");
        check(off < 1e-6,
              "end sleeves: every joint is on a drawn line, none in the middle between them");
        ed.onPointerPress({20.0, 3.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({30.0, 3.0});
        ed.onPointerPress({30.0, 3.0}, Qt::LeftButton, Qt::NoModifier);
        const Entity& moved = d.entities().front();
        check(near(moved.meta.value(QStringLiteral("shrinkCoverStart")).toDouble(), 30.0) &&
                  near(moved.meta.value(QStringLiteral("shrinkCoverEnd")).toDouble(), 20.0),
              "end sleeves: the split-line joint sets that end's sleeve length");
        const auto after = shrunkSleeveRunsOf(moved);
        check(!after.empty() && near(after.front().front().x, 30.0) &&
                  near(after.front().back().x, 80.0),
              "end sleeves: the visible cable begins where the moved sleeve stopped");

        // The OUTER end is still edited the same way as a whole-run sleeve: its own face and shrunk
        // length, now on the sleeve section rather than the whole route.
        Entity& edited = d.entities().front();
        edited.meta.insert(QStringLiteral("shrinkFaceStart"), 2.0);
        edited.meta.insert(QStringLiteral("shrinkTaperStart"), 8.0);
        const auto left = shrunkSleeveEndOutlineOf(edited, 0);
        check(hasPoint(left, {0.0, 1.0}) && hasPoint(left, {8.0, 3.0}),
              "end sleeves: the outer heatshrink face and taper stay editable");

        // The new numbers are lengths, so a scale carries them with the route; and they are ordinary
        // metadata, so a save/load leaves the same four pieces of linework rather than baking a
        // picture of it into the file.
        scaleEntity(edited, {0.0, 0.0}, 2.0);
        check(near(edited.meta.value(QStringLiteral("shrinkCoverStart")).toDouble(), 60.0) &&
                  near(edited.meta.value(QStringLiteral("shrinkCoverEnd")).toDouble(), 40.0) &&
                  near(edited.meta.value(QStringLiteral("shrinkCableWidth")).toDouble(), 9.84),
              "end sleeves: scaling carries the cable and both sleeve lengths");
        check(shrunkSleeveRunsOf(edited).size() == 4,
              "end sleeves: scaling leaves the same cable-plus-two-sleeves linework");
        Document back;
        dwx::fromJson(back, dwx::toJson(d));
        check(back.entities().size() == 1 &&
                  back.entities().front().meta.value(QStringLiteral("shrinkEndsOnly")).toBool() &&
                  shrunkSleeveRunsOf(back.entities().front()).size() == 4,
              "end sleeves: a DWX round trip keeps it editable, not a baked outline");

        // ROUND A KNEE TOO: the bend's route point is still the thing being edited, but its joints
        // are worn on the two cable rails it lies between, not on the invisible centreline.
        Document bentDoc;
        Editor bentEd;
        bentEd.setDocument(&bentDoc);
        noSnap(bentEd);
        bentEd.setShrinkRouteMode(ShrinkRouteMode::CableEnds);
        bentEd.setTool(ToolId::HeatshrinkRoute);
        bentEd.onPointerPress({0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
        bentEd.onPointerPress({50.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
        bentEd.onPointerPress({100.0, 50.0}, Qt::LeftButton, Qt::NoModifier);
        bentEd.onKey(Qt::Key_Return, Qt::NoModifier);
        check(bentDoc.entities().size() == 1, "end sleeves: a bent run still draws one object");
        if (!bentDoc.entities().empty()) {
            const Entity& bent = bentDoc.entities().front();
            const auto bentRuns = shrunkSleeveRunsOf(bent);
            bentEd.selectById(bent.id);
            bool onCentre = false;
            double offBent = 0.0;
            for (const Vec2& joint : bentEd.selectionGripPoints()) {
                offBent = std::max(offBent, offLinework(joint, bentRuns));
                if (distance(joint, bent.points[1]) < 1e-6) onCentre = true;
            }
            check(!onCentre && offBent < 1e-6,
                  "end sleeves: a knee's joints sit on the cable lines, not between them");
            check(bentRuns.size() >= 2 && bentRuns[0].size() > 2 && bentRuns[1].size() > 2,
                  "end sleeves: both cable rails follow the knee");
        }
    }

    // ---- Transition heatshrink: cross the parallel lines, fit it to the nearer end ---------------
    {
        auto line = [](const QString& id, const Vec2& a, const Vec2& b) {
            Entity e;
            e.id = id;
            e.type = EntityType::Line;
            e.a = a;
            e.b = b;
            return e;
        };
        Document d;
        d.entities().push_back(line(QStringLiteral("T1"), {0.0, 4.0}, {80.0, 4.0}));
        d.entities().push_back(line(QStringLiteral("T2"), {0.0, -4.0}, {80.0, -4.0}));
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.setShrinkRouteMode(ShrinkRouteMode::Transition);
        ed.setTool(ToolId::HeatshrinkRoute);
        ed.onPointerPress({70.0, -12.0}, Qt::LeftButton, Qt::NoModifier);
        check(ed.transitionBandStarted(), "transition: the first click starts the crossing line");
        ed.onPointerMove({70.0, 12.0});
        const Entity preview = ed.transitionPreview();
        check(preview.points.size() == 2 && near(preview.points.front().x, 80.0),
              "transition: the ghost shows the sleeve the second click will place");
        ed.onPointerPress({70.0, 12.0}, Qt::LeftButton, Qt::NoModifier);
        check(d.entities().size() == 3, "transition: crossing the pair adds one sleeve");
        if (d.entities().size() == 3) {
            const Entity& s = d.entities().back();
            check(s.meta.value(QStringLiteral("shrunkEnds")).toBool() &&
                      s.meta.value(QStringLiteral("shrinkTransition")).toBool(),
                  "transition: it is still the editable sleeve object");
            check(s.points.size() == 2 && near(s.points.front().x, 80.0) &&
                      near(s.points.front().y, 0.0) && near(s.points.back().x, 104.0) &&
                      near(s.points.back().y, 0.0),
                  "transition: it is fitted to the nearer right-hand end");
            // A LITTLE WIDER THAN THE RAILS: 8 apart, plus a millimetre of standoff each side. Drawn
            // to the rails' own 8 the sleeve came out flush with them and read as a piece butted
            // between the two lines rather than a sleeve over them.
            check(near(s.meta.value(QStringLiteral("gap")).toDouble(), 10.0) &&
                      near(s.meta.value(QStringLiteral("shrinkFaceStart")).toDouble(), 10.0) &&
                      near(s.meta.value(QStringLiteral("shrinkTaperStart")).toDouble(), 0.0),
                  "transition: square across the end the two lines meet, and standing clear of them");
            // And its LENGTH is still what it is fitted over: the standoff is clearance, not size.
            check(near(s.points.back().x, 104.0),
                  "transition: standing off the rails does not lengthen the part");
            const auto runs = shrunkSleeveRunsOf(s);
            check(runs.size() == 1 && runs.front().size() >= 4 &&
                      distance(runs.front().front(), runs.front().back()) < 1e-6,
                  "transition: the derived outline is one closed sleeve");
            check(ed.tool() == ToolId::Select, "transition: placing it returns to Select");
        }

        // ---- A RUN WITH A KNEE IN IT ENDS WHERE THE RUN ENDS, not at the knee ----
        //
        // A transition goes on the END of what it is fitted to, and the end of the crossed SEGMENT
        // of a run that turns is the corner. A band drawn over the straight before a corner fitted
        // the sleeve AT the corner, lying along the leg it had crossed and pointing off past it -
        // sitting in mid-air beside the turn with nothing under it. The end wanted is the end of
        // the drawn polyline, and the angle wanted is the angle of the leg that finishes there.
        {
            // Out from the left, then down and away to the right: the band crosses the horizontal
            // stretch near the corner, so the corner is the nearer end of that SEGMENT while the
            // left-hand end is the nearer end of the RUN.
            auto bent = [](const QString& id, double off) {
                Entity e;
                e.id = id;
                e.type = EntityType::Polyline;
                e.points = {{0.0, off}, {60.0, off}, {110.0, off - 50.0}};
                e.a = e.points.front();
                e.b = e.points.back();
                return e;
            };
            Document knee;
            knee.entities().push_back(bent(QStringLiteral("K1"), 4.0));
            knee.entities().push_back(bent(QStringLiteral("K2"), -4.0));
            Editor kneeEd;
            kneeEd.setDocument(&knee);
            noSnap(kneeEd);
            kneeEd.setShrinkRouteMode(ShrinkRouteMode::Transition);
            kneeEd.setTool(ToolId::HeatshrinkRoute);
            kneeEd.onPointerPress({50.0, -12.0}, Qt::LeftButton, Qt::NoModifier);
            kneeEd.onPointerMove({50.0, 12.0});
            kneeEd.onPointerPress({50.0, 12.0}, Qt::LeftButton, Qt::NoModifier);
            check(knee.entities().size() == 3, "knee: crossing the bent pair adds one sleeve");
            if (knee.entities().size() == 3) {
                const Entity& s = knee.entities().back();
                std::printf("  knee: the sleeve runs %.1f,%.1f to %.1f,%.1f\n", s.points.front().x,
                            s.points.front().y, s.points.back().x, s.points.back().y);
                check(s.points.size() == 2 && near(s.points.front().x, 0.0) &&
                          near(s.points.front().y, 0.0),
                      "knee: it is fitted at the END of the run, not at the corner nearer the band");
                check(s.points.back().x < s.points.front().x - 1.0 &&
                          near(s.points.back().y, 0.0),
                      "knee: and lies along the leg that finishes there, pointing out of the run");
            }

            // AND THE OTHER WAY ROUND: a band over the far leg belongs to the far end, which is the
            // one on the slant - so the sleeve is on the slant too.
            Document knee2;
            knee2.entities().push_back(bent(QStringLiteral("L1"), 4.0));
            knee2.entities().push_back(bent(QStringLiteral("L2"), -4.0));
            Editor farEd;
            farEd.setDocument(&knee2);
            noSnap(farEd);
            farEd.setShrinkRouteMode(ShrinkRouteMode::Transition);
            farEd.setTool(ToolId::HeatshrinkRoute);
            farEd.onPointerPress({98.0, -50.0}, Qt::LeftButton, Qt::NoModifier);
            farEd.onPointerMove({86.0, -20.0});
            farEd.onPointerPress({86.0, -20.0}, Qt::LeftButton, Qt::NoModifier);
            if (knee2.entities().size() == 3) {
                const Entity& s = knee2.entities().back();
                const Vec2 dir = (s.points.back() - s.points.front()).normalized();
                std::printf("  knee: the far end takes it to %.1f,%.1f running %.2f,%.2f\n",
                            s.points.front().x, s.points.front().y, dir.x, dir.y);
                // At the far end, give or take: these two rails are drawn as parallel Y
                // offsets rather than true offsets of the slant, so their ends are
                // staggered along it and the face squares up between them.
                check(distance(s.points.front(), Vec2{110.0, -50.0}) < 4.0,
                      "knee: a band over the far leg fits it at the far end of the run");
                check(dir.x > 0.3 && dir.y < -0.3,
                      "knee: at the angle that leg runs at, which is the whole of what a knee changes");
            } else {
                check(false, "knee: a band over the far leg fits a sleeve at all");
            }
        }

        // EVERY PARALLEL LINE THE MARK CROSSES IS INSIDE THE SLEEVE - a fan of five, not only a
        // pair. The vertical line sharing the mark's end is unrelated linework, not another rail.
        Document many;
        for (double y : {8.0, 4.0, 0.0, -4.0, -8.0})
            many.entities().push_back(line(QStringLiteral("M%1").arg(y), {0.0, y}, {80.0, y}));
        many.entities().push_back(line(QStringLiteral("V"), {70.0, -8.0}, {70.0, -30.0}));
        Editor manyEd;
        manyEd.setDocument(&many);
        noSnap(manyEd);
        manyEd.setShrinkRouteMode(ShrinkRouteMode::Transition);
        manyEd.setTool(ToolId::HeatshrinkRoute);
        manyEd.onPointerPress({60.0, 15.0}, Qt::LeftButton, Qt::NoModifier);
        manyEd.onPointerPress({70.0, -8.0}, Qt::LeftButton, Qt::NoModifier);
        check(many.entities().size() == 7,
              "transition: every parallel line the mark crosses is covered");
        if (many.entities().size() == 7) {
            const Entity& s = many.entities().back();
            check(s.meta.value(QStringLiteral("shrinkTransition")).toBool() &&
                      near(s.points.front().x, 80.0) && near(s.points.front().y, 0.0) &&
                      near(s.points.back().x, 128.0),
                  "transition: the sleeve spans the whole fan and reaches from its common end");
            check(near(s.meta.value(QStringLiteral("gap")).toDouble(), 19.84),
                  "transition: its width is the outer rails' spread plus the normal standoff");
        }

        // THE TWO RAILS OF ONE DOUBLE RUN COUNT AS THE TWO LINES TOO - the drawing's usual way of
        // putting a cable down is a single object with two visible sides.
        Document rails;
        Entity dl;
        dl.id = QStringLiteral("D1");
        dl.type = EntityType::DoubleLine;
        dl.a = {0.0, 0.0};
        dl.b = {80.0, 0.0};
        dl.meta.insert(QStringLiteral("gap"), 8.0);
        rails.entities().push_back(dl);
        Editor railEd;
        railEd.setDocument(&rails);
        noSnap(railEd);
        railEd.setShrinkRouteMode(ShrinkRouteMode::Transition);
        railEd.setTool(ToolId::HeatshrinkRoute);
        railEd.onPointerPress({70.0, -12.0}, Qt::LeftButton, Qt::NoModifier);
        railEd.onPointerPress({70.0, 12.0}, Qt::LeftButton, Qt::NoModifier);
        check(rails.entities().size() == 2 &&
                  rails.entities().back().meta.value(QStringLiteral("shrinkTransition")).toBool() &&
                  near(rails.entities().back().points.front().x, 80.0),
              "transition: it can be fitted across the two sides of a double line");

        // UNEQUAL LENGTHS: the longest line is the datum, not whichever end happened to be nearer the
        // crossing. Here the lower rail is shorter and ends at x=60; the upper one runs to x=100, so
        // the transition face belongs at x=100.
        Document staggered;
        staggered.entities().push_back(line(QStringLiteral("L1"), {0.0, 4.0}, {100.0, 4.0}));
        staggered.entities().push_back(line(QStringLiteral("L2"), {0.0, -4.0}, {60.0, -4.0}));
        Editor longEd;
        longEd.setDocument(&staggered);
        noSnap(longEd);
        longEd.setShrinkRouteMode(ShrinkRouteMode::Transition);
        longEd.setTool(ToolId::HeatshrinkRoute);
        longEd.onPointerPress({55.0, -12.0}, Qt::LeftButton, Qt::NoModifier);
        longEd.onPointerPress({55.0, 12.0}, Qt::LeftButton, Qt::NoModifier);
        check(staggered.entities().size() == 3, "transition: unequal parallel lines are accepted");
        if (staggered.entities().size() == 3) {
            const Entity& s = staggered.entities().back();
            check(s.meta.value(QStringLiteral("transitionHost")).toString() == QStringLiteral("L1"),
                  "transition: the longest crossed line is the host");
            check(near(s.points.front().x, 100.0) && near(s.points.front().y, 0.0),
                  "transition: and its end decides where the sleeve starts");
        }

        // NOT PARALLEL IS NOT A TRANSITION: the second click leaves the crossing line armed so the
        // user can try again rather than dropping a sleeve on unrelated geometry.
        Document skew;
        skew.entities().push_back(line(QStringLiteral("S1"), {0.0, 0.0}, {100.0, 0.0}));
        skew.entities().push_back(line(QStringLiteral("S2"), {50.0, -40.0}, {50.0, 40.0}));
        Editor skewEd;
        skewEd.setDocument(&skew);
        noSnap(skewEd);
        skewEd.setShrinkRouteMode(ShrinkRouteMode::Transition);
        skewEd.setTool(ToolId::HeatshrinkRoute);
        skewEd.onPointerPress({40.0, -10.0}, Qt::LeftButton, Qt::NoModifier);
        skewEd.onPointerPress({60.0, 10.0}, Qt::LeftButton, Qt::NoModifier);
        check(skew.entities().size() == 2 && skewEd.transitionBandStarted(),
              "transition: non-parallel lines are refused without adding an object");
    }

    // ---- A sleeve's joints are on its corners, where there is something to take hold of ----------
    {
        // A sleeve is filed as a double polyline, so its joints came off the two rails half the BODY
        // width either side of the route - and a sleeve is not that shape at its ends, it necks down.
        // The handles sat out in the air beside the drawing, level with a body the end does not have.
        Document d;
        Entity s;
        s.id = QStringLiteral("SLV");
        s.type = EntityType::Polyline;
        s.points = {{0.0, 0.0}, {100.0, 0.0}};
        s.meta.insert(QStringLiteral("shrunkEnds"), true);
        s.meta.insert(QStringLiteral("double"), true);
        s.meta.insert(QStringLiteral("gap"), 20.0);            // body ±10
        s.meta.insert(QStringLiteral("shrinkFaceStart"), 8.0);  // face ±4
        s.meta.insert(QStringLiteral("shrinkFaceEnd"), 8.0);
        s.meta.insert(QStringLiteral("shrinkTaperStart"), 15.0);
        s.meta.insert(QStringLiteral("shrinkTaperEnd"), 15.0);
        d.entities().push_back(s);
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.selectById(QStringLiteral("SLV"));

        // EVERY JOINT IS ON THE OUTLINE. Not one of them out in the air beside it.
        const std::vector<Vec2> outline = shrunkSleeveOutlineOf(d.entities().front());
        double worst = 0.0;
        int onCorner = 0;
        for (const Vec2& j : ed.selectionGripPoints()) {
            double best = 1e18;
            for (size_t i = 0; i < outline.size(); ++i)
                best = std::min(best, closestPointOnSegment(j, outline[i],
                                                            outline[(i + 1) % outline.size()]).distance);
            worst = std::max(worst, best);
            for (const Vec2& c : outline)
                if (distance(j, c) < 1e-6) { ++onCorner; break; }
        }
        std::printf("  sleeve joints: %d joint(s), %d on a corner, furthest %.3f off the outline\n",
                    int(ed.selectionGripPoints().size()), onCorner, worst);
        // The four face corners and the four shoulders, which is what the eye picks out: the vertical
        // ends, and where the straight sides meet the diagonals. The one that is NOT on the outline is
        // the mid-run handle, which sits on the centreline INSIDE the shape and moves the whole sleeve
        // - that one is meant to be there.
        check(onCorner >= 8, "sleeve joints: on the end faces and on the shoulders");
        check(worst < 10.0 + 1e-6,
              "sleeve joints: and nothing is out beyond the shape it belongs to");

        // A FACE CORNER SETS THE FACE. Dragged in across the run, the end recovers further.
        auto sleeve = [&d] { return d.entities().front(); };
        const Vec2 faceCorner{100.0, 4.0};
        // A grabbed joint follows the cursor until the NEXT CLICK puts it down - a release does not
        // end it - so each drag here is press, move, press.
        ed.onPointerPress(faceCorner, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({100.0, 2.0});
        ed.onPointerPress({100.0, 2.0}, Qt::LeftButton, Qt::NoModifier);
        std::printf("  sleeve joints: pulling the face corner in -> face %.2f\n",
                    sleeve().meta.value(QStringLiteral("shrinkFaceEnd")).toDouble());
        check(std::abs(sleeve().meta.value(QStringLiteral("shrinkFaceEnd")).toDouble() - 4.0) < 1e-6,
              "sleeve joints: the face corner sets how far that end has recovered");

        // A SHOULDER SETS THE TAPER. Dragged towards the end, the recovered stretch gets shorter.
        ed.selectById(QStringLiteral("SLV"));
        const Vec2 shoulder{85.0, 10.0};
        ed.onPointerPress(shoulder, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({95.0, 10.0});
        ed.onPointerPress({95.0, 10.0}, Qt::LeftButton, Qt::NoModifier);
        std::printf("  sleeve joints: pushing the shoulder out -> taper %.2f, route (%.1f,%.1f)..(%.1f,%.1f)\n",
                    sleeve().meta.value(QStringLiteral("shrinkTaperEnd")).toDouble(),
                    sleeve().points.front().x, sleeve().points.front().y,
                    sleeve().points.back().x, sleeve().points.back().y);
        check(std::abs(sleeve().meta.value(QStringLiteral("shrinkTaperEnd")).toDouble() - 5.0) < 1e-6,
              "sleeve joints: the shoulder sets how long the recovered end is");
    }

    // ---- A part put down from the library has something to take hold of --------------------------
    {
        // A placed part is a BLOCK: it moves as one and its lines are changed in its own editor, so it
        // has no joints of its own - handing out one per line would be handing out the ability to pull
        // the part apart on the sheet. But none at all left it with nothing to aim at: it could only be
        // shoved with the mouse and hoped at.
        LibraryPart p;
        p.id = QStringLiteral("P1");
        p.kind = PartKind::Connector;
        p.mfrPn = QStringLiteral("TEST-CONN");
        p.description = QStringLiteral("A test connector");
        // TWO lines, so "the whole copy moves" is a claim with something to prove.
        auto seg = [](Vec2 a, Vec2 b) {
            Entity e;
            e.type = EntityType::Line;
            e.a = a;
            e.b = b;
            return e;
        };
        QJsonArray drawing;
        drawing.append(seg({0.0, 0.0}, {20.0, 0.0}).toJson());
        drawing.append(seg({0.0, 10.0}, {20.0, 10.0}).toJson());
        p.mechanical = drawing;

        Document d;
        // A block instance comes back with its ids cleared - a stored drawing has no business naming
        // objects in whatever document it lands in - so the document gives them one, as the
        // application does when a part is inserted.
        int nth = 0;
        for (Entity e : partBlockInstance(p, /*mechanical=*/true, Vec2{50.0, 50.0})) {
            e.id = QStringLiteral("PART-%1").arg(++nth);
            d.entities().push_back(e);
        }
        check(d.entities().size() == 2, "placed part: both of its lines are on the sheet");
        if (d.entities().size() != 2) return g_fail;
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.selectById(d.entities().front().id);
        // Picking any of it picks all of it - that is what a block is.
        check(ed.selection().size() == 2, "placed part: it is picked as one thing");

        const std::vector<Vec2> joints = ed.selectionGripPoints();
        std::printf("  placed part: %d joint(s)\n", int(joints.size()));
        // Its corners, the middles of its sides, and its centre - nine, and ONCE for the copy rather
        // than nine per line in it.
        check(joints.size() == 9, "placed part: the copy has handles of its own, drawn once");
        Bounds b;
        for (const Entity& e : d.entities()) {
            const Bounds eb = e.bounds();
            if (eb.valid) { b.expand(eb.min); b.expand(eb.max); }
        }
        bool onCorner = false;
        for (const Vec2& j : joints)
            if (distance(j, b.min) < 1e-6) onCorner = true;
        check(onCorner, "placed part: with one on the corner of what it occupies");

        // AND DRAGGING ONE MOVES THE WHOLE COPY. Moving only the line that happened to be carrying the
        // handle would pull the part apart, which is the one thing a block exists to prevent.
        const Vec2 corner = b.min;
        ed.onPointerPress(corner, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({corner.x + 30.0, corner.y + 5.0});
        ed.onPointerPress({corner.x + 30.0, corner.y + 5.0}, Qt::LeftButton, Qt::NoModifier);
        Bounds after;
        for (const Entity& e : d.entities()) {
            const Bounds eb = e.bounds();
            if (eb.valid) { after.expand(eb.min); after.expand(eb.max); }
        }
        std::printf("  placed part: dragged its corner -> (%.1f,%.1f)..(%.1f,%.1f)\n", after.min.x,
                    after.min.y, after.max.x, after.max.y);
        check(std::abs(after.min.x - (b.min.x + 30.0)) < 1e-6 &&
                  std::abs(after.min.y - (b.min.y + 5.0)) < 1e-6,
              "placed part: the handle moves the copy to where it was dragged");
        check(std::abs((after.max.x - after.min.x) - (b.max.x - b.min.x)) < 1e-6 &&
                  std::abs((after.max.y - after.min.y) - (b.max.y - b.min.y)) < 1e-6,
              "placed part: and moves all of it, so the part keeps its shape");
    }

    // ---- A wires run converges to fewer lines, and never to more than it has ----------------------
    {
        // Three lines say "a bundle"; eleven say nothing three do not, while being eleven things to
        // keep tidy. So past three wires the run is drawn converged - and converging to MORE wires
        // than the fan has cannot draw anything, so it is not offered.
        Document d;
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);

        // TWO, out of the box. That is what a run of many wires is drawn as on a released sheet:
        // "All" would be a picture of the wires rather than of the harness.
        std::printf("  converge: to begin with, converge-to %d\n", ed.twistTrunk());
        check(ed.twistTrunk() == 2,
              "converge: a bundle is drawn as two lines unless it is told otherwise");
        ed.setTwistWires(8);
        std::printf("  converge: 8 wires -> converge-to %d\n", ed.twistTrunk());
        check(ed.twistTrunk() == 2, "converge: and raising the wire count leaves that alone");

        // SOMEBODY WHO SAYS OTHERWISE IS NOT OVERRULED. Asking for two, then raising the count
        // again, leaves it at two: it has been decided.
        ed.setTwistTrunk(2);
        ed.setTwistWires(11);
        std::printf("  converge: asked for 2, then 11 wires -> converge-to %d\n", ed.twistTrunk());
        check(ed.twistTrunk() == 2, "converge: a count somebody chose is left alone");

        // NEVER MORE THAN THE FAN HAS. Asking for more draws nothing extra - there is nothing extra
        // to draw - so it is held at the number of wires rather than accepted and ignored.
        ed.setTwistTrunk(50);
        std::printf("  converge: asked for 50 of 11 -> %d\n", ed.twistTrunk());
        check(ed.twistTrunk() == 11, "converge: it cannot be set above the number of wires");

        // AND TAKING WIRES AWAY TAKES THE CONVERGENCE WITH THEM, rather than leaving a number that
        // is now bigger than the fan.
        ed.setTwistWires(4);
        std::printf("  converge: down to 4 wires -> %d\n", ed.twistTrunk());
        check(ed.twistTrunk() <= 4, "converge: lowering the wire count brings it down too");

        // The wire count itself stays inside what a bundle can be.
        ed.setTwistWires(1);
        check(ed.twistWires() == 2, "converge: two wires is the fewest a bundle can be");
    }

    // ---- The tools that act on wire runs act on a wires run too -----------------------------------
    {
        // A wires run is the object most of a loom is drawn with, and it was the one object the
        // stroke-and-band tools could not see: they test Line, DoubleLine, Wire, Spline, Arc and
        // Polyline, and a wires run is none of those. A stroke drawn squarely across a bundle of
        // eight crossed nothing and placed nothing.
        auto runDoc = [](Document& d) {
            d.entities().clear();
            Entity w;
            w.id = QStringLiteral("RUN");
            w.type = EntityType::TwistedPair;
            w.a = {0.0, 0.0};
            w.b = {200.0, 0.0};
            w.points = {{0.0, 0.0}, {200.0, 0.0}};
            w.meta.insert(QStringLiteral("twistStraight"), true);
            w.meta.insert(QStringLiteral("twistWires"), 6);
            w.meta.insert(QStringLiteral("twistWidth"), 12.0);
            w.meta.insert(QStringLiteral("twistPitch"), 8.0);
            d.entities().push_back(w);
        };
        // It really does draw strands - otherwise the checks below would be testing nothing.
        Document d;
        runDoc(d);
        const auto strands = twistedPairStrands(d.entities().front());
        std::printf("  run tools: the run draws %d strand(s)\n", int(strands.size()));
        check(strands.size() >= 2, "run tools: a wires run is drawn as several runs of wire");

        // ---- THE TRANSITION HEATSHRINK, and the break mark: both are bands drawn across runs, and
        // both are the same switch the label stroke uses - see the matching case there.
        {
            Document d2;
            runDoc(d2);
            Editor ed;
            ed.setDocument(&d2);
            noSnap(ed);
            Entity sleeve, mark;
            const bool gotSleeve = ed.transitionFromBand({100.0, -20.0}, {100.0, 20.0}, sleeve);
            const bool gotBreak = ed.breakFromBand({100.0, -20.0}, {100.0, 20.0}, mark);
            std::printf("  run tools: transition heatshrink %s, break mark %s\n",
                        gotSleeve ? "fits" : "finds nothing", gotBreak ? "fits" : "finds nothing");
            check(gotSleeve, "run tools: a transition heatshrink can be put over a wires run");
            check(gotBreak, "run tools: and a break mark across it");
        }
    }

    // ---- Tab holds the run still, and lets go of the cursor until the drawing is clicked ----------
    {
        // Tab pins the run so the ribbon can be used with it on screen. Pinned, the run stops
        // following the cursor - which is the point: the ribbon is across the window from the
        // drawing and the mouse has to get there without dragging the run along behind it. It takes
        // the cursor up again at the next click on the drawing.
        Document d;
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.setTool(ToolId::WiresRun);

        // Nothing started: there is nothing to pin, and asking does not pretend otherwise.
        ed.freezeTwistedPreview();
        check(!ed.twistedFrozen(), "wires pin: nothing to pin before the run has a start");

        ed.onPointerPress({0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({100.0, 0.0});
        ed.freezeTwistedPreview();
        check(ed.twistedFrozen(), "wires pin: Tab pins it once the run has a start");

        // THE CURSOR IS RELEASED: moving it does not take the pin off, so the run is still being
        // held while the mouse travels to the ribbon and back.
        ed.onPointerMove({400.0, 250.0});
        ed.onPointerMove({-50.0, 90.0});
        check(ed.twistedFrozen(), "wires pin: it stays pinned however far the cursor goes");

        // AND THE CLICK ON THE DRAWING TAKES IT UP AGAIN - the same click that says where the next
        // knee goes, because coming back to the drawing is the end of setting it up.
        ed.onPointerPress({200.0, 40.0}, Qt::LeftButton, Qt::NoModifier);
        check(!ed.twistedFrozen(), "wires pin: a click on the drawing unpins it");

        // Pinning twice over is not a way to lose the first one.
        ed.freezeTwistedPreview();
        const bool once = ed.twistedFrozen();
        ed.freezeTwistedPreview();
        check(once && ed.twistedFrozen(), "wires pin: asking again while pinned changes nothing");
    }

    // ---- The click that comes back from the ribbon only unpins: it puts no knee down -------------
    {
        // The pin holds the run still so the ribbon can be reached, and lets go of the cursor. The
        // cursor then goes somewhere else entirely - across the window, onto a spin box - and
        // wherever it lands on the way back is not a place anybody chose to put a knee. Taking the
        // knee as well put one wherever the eye happened to return to, every time.
        Document d;
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.setTool(ToolId::WiresRun);
        ed.onPointerPress({0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({100.0, 0.0});
        ed.freezeTwistedPreview();
        check(ed.twistedFrozen(), "wires pin: it is pinned to start with");
        // Back from the ribbon, landing nowhere in particular.
        ed.onPointerPress({63.0, 41.0}, Qt::LeftButton, Qt::NoModifier);
        check(!ed.twistedFrozen(), "wires pin: the click back on the drawing unpins it");
        // Then the knee that WAS chosen, and finish.
        ed.onPointerMove({300.0, 0.0});
        ed.onPointerPress({300.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onKey(Qt::Key_Return, Qt::NoModifier);
        const Entity* run = nullptr;
        for (const Entity& e : d.entities())
            if (e.type == EntityType::TwistedPair) run = &e;
        check(run != nullptr, "wires pin: the run was drawn");
        if (run) {
            QStringList where;
            for (const Vec2& v : run->points)
                where << QStringLiteral("%1,%2").arg(v.x, 0, 'f', 0).arg(v.y, 0, 'f', 0);
            std::printf("  wires pin: the run came out as %s\n",
                        qPrintable(where.join(QStringLiteral(" -> "))));
            check(run->points.size() == 2,
                  "wires pin: two knees - the one clicked before pinning and the one clicked after, "
                  "and none where the cursor came back");
            check(!run->points.empty() && std::abs(run->points.back().x - 300.0) < 1e-6 &&
                      std::abs(run->points.back().y) < 1e-6,
                  "wires pin: and it ends where the second knee was clicked");
        }
    }

    // ---- A finished wires run has a joint on every wire, at the ends ------------------------------
    {
        // Its stored points are the ROUTE down the middle of the bundle, and the route is never
        // drawn: the only handles were on a line nobody can see, sitting between the wires and on
        // none of them. What is drawn is the wires, and those are what a person reaches for.
        Document d;
        Entity w;
        w.id = QStringLiteral("RUN");
        w.type = EntityType::TwistedPair;
        w.a = {0.0, 0.0};
        w.b = {200.0, 0.0};
        w.points = {{0.0, 0.0}, {200.0, 0.0}};
        w.meta.insert(QStringLiteral("twistStraight"), true);
        w.meta.insert(QStringLiteral("twistWires"), 6);
        w.meta.insert(QStringLiteral("twistWidth"), 12.0);
        w.meta.insert(QStringLiteral("twistPitch"), 8.0);
        d.entities().push_back(w);
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.selectById(QStringLiteral("RUN"));

        const std::vector<Vec2> joints = ed.selectionGripPoints();
        const std::vector<std::vector<Vec2>> strands = twistedPairStrands(d.entities().front());
        const std::vector<std::pair<Vec2, Vec2>> straights =
            twistedPairStraights(d.entities().front());
        std::printf("  wire joints: %d strand(s), %d joint(s), %d straight stretch(es)\n",
                    int(strands.size()), int(joints.size()), int(straights.size()));
        // Six wires, two ends each, plus the route's own two: fourteen - and then one in the middle
        // of every straight stretch there is, which is where a line is picked up to be moved.
        check(joints.size() == 14 + straights.size(),
              "wire joints: one per wire at each end, the route's two, and one in the middle of "
              "every straight stretch");
        check(!straights.empty(),
              "wire joints: a run has straight stretches in it to put those middles on");

        // OFF THE CENTRELINE. The bundle is 12 across, so its wires are spread either side of y = 0;
        // handles all sitting at y = 0 would be the old ones on the route.
        int offAxis = 0;
        for (const Vec2& j : joints)
            if (std::abs(j.y) > 1e-6) ++offAxis;
        std::printf("  wire joints: %d of them are off the centreline\n", offAxis);
        check(offAxis >= 10, "wire joints: they sit on the wires, not on the line between them");

        // AT THE ENDS, OR AT THE MIDDLE OF A STRAIGHT, and nowhere else: a joint strung along a run
        // at no particular place is a handle nobody asked for.
        bool allPlaced = true;
        for (const Vec2& j : joints) {
            if (std::abs(j.x) < 1e-6 || std::abs(j.x - 200.0) < 1e-6) continue;
            bool isMiddle = false;
            for (const auto& seg : straights)
                if (distance(j, lerp(seg.first, seg.second, 0.5)) < 1e-6) isMiddle = true;
            if (!isMiddle) allPlaced = false;
        }
        check(allPlaced,
              "wire joints: at the two ends of the run, or the middle of a straight, and nowhere "
              "else");

        // ONE PER WIRE, not one per drawn edge. The strands come two to a wire - its two sides - and
        // a handle on each would be twelve at one end where there are six wires.
        int atStart = 0;
        for (const Vec2& j : joints)
            if (std::abs(j.x) < 1e-6) ++atStart;
        std::printf("  wire joints: %d at the start\n", atStart);
        check(atStart == 7, "wire joints: six wires and the route's own point, not twelve edges");

        // ---- AND A RUN THAT BENDS gets them at both ends too ----
        {
            // This is the case that was wrong: the ends used to be judged by projecting onto the
            // straight line between head and tail, and on a run with a corner in it that line runs
            // through empty paper. Every wire but one was reckoned to be somewhere in the middle and
            // got no joint - a six-wire harness with a single handle on it.
            Document d2;
            Entity r = d.entities().front();
            r.id = QStringLiteral("BENT");
            r.points = {{0.0, 0.0}, {200.0, 0.0}, {200.0, -150.0}};
            r.a = r.points.front();
            r.b = r.points.back();
            d2.entities().push_back(r);
            Editor ed2;
            ed2.setDocument(&d2);
            noSnap(ed2);
            ed2.selectById(QStringLiteral("BENT"));
            const std::vector<Vec2> bent = ed2.selectionGripPoints();
            int atHead = 0, atTail = 0;
            for (const Vec2& j : bent) {
                if (distance(j, Vec2{0.0, 0.0}) < 40.0) ++atHead;
                if (distance(j, Vec2{200.0, -150.0}) < 40.0) ++atTail;
            }
            std::printf("  wire joints: bent run has %d joint(s) - %d near the head, %d near the "
                        "tail\n",
                        int(bent.size()), atHead, atTail);
            check(atHead >= 6, "wire joints: every wire is handled where the bent run starts");
            check(atTail >= 6, "wire joints: and where it finishes, round the corner");
        }
    }

    // ---- The fan length and the wire width are settings now, not constants ------------------------
    {
        // Both figures the drawing already knew how to read, and neither had anywhere to be set from:
        // the fan was whatever the default said, and the wire width - which on a straight run is also
        // what settles how wide the bundle comes out - could only be got at on a placed object.
        auto runWith = [](double fan, double wire) {
            Document d;
            Editor ed;
            ed.setDocument(&d);
            noSnap(ed);
            ed.setTwistWires(6);
            ed.setTwistFan(fan);
            ed.setTwistWireWidth(wire);
            ed.setTool(ToolId::WiresRun);
            ed.onPointerPress({0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerPress({300.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
            ed.onKey(Qt::Key_Return, Qt::NoModifier);
            return d.entities().empty() ? Entity() : d.entities().front();
        };
        // How far the wires reach across at an end, and where they stop fanning: the two things the
        // settings are for. Measured off what is drawn rather than off the meta.
        auto spreadOf = [](const Entity& e) {
            double lo = 1e300, hi = -1e300;
            for (const std::vector<Vec2>& s : twistedPairStrands(e))
                for (const Vec2& p : s) {
                    if (p.x > 1.0) continue;          // at the very start of the run
                    lo = std::min(lo, p.y);
                    hi = std::max(hi, p.y);
                }
            return hi > lo ? hi - lo : 0.0;
        };

        const Entity base = runWith(0.0, 0.0);
        check(!base.points.empty(), "fan: a run is drawn with the settings left alone");
        if (base.points.empty()) return g_fail;
        // UNSET IS THE DRAWING'S OWN. Nothing is written, so a run drawn without touching either is
        // the run that was always drawn.
        check(!base.meta.contains(QStringLiteral("twistWireWidth")),
              "fan: untouched, the wire width is not written at all");

        // THE FAN LENGTH REACHES THE OBJECT.
        const Entity longFan = runWith(60.0, 0.0);
        std::printf("  fan: asked for 60 -> twistEnds %.1f\n",
                    longFan.meta.value(QStringLiteral("twistEnds")).toDouble());
        check(std::abs(longFan.meta.value(QStringLiteral("twistEnds")).toDouble() - 60.0) < 1e-9,
              "fan: the fan length is the one that was set");

        // AND THE WIRE WIDTH CHANGES WHAT IS DRAWN: six fat wires take up more room than six thin
        // ones, which is the whole reason a straight run is measured from the wire and not the
        // bundle.
        const Entity thin = runWith(0.0, 0.5);
        const Entity fat = runWith(0.0, 2.0);
        const double thinSpread = spreadOf(thin), fatSpread = spreadOf(fat);
        std::printf("  fan: wires 0.5 across span %.2f, wires 2.0 across span %.2f\n", thinSpread,
                    fatSpread);
        check(fatSpread > thinSpread + 1e-6,
              "fan: fatter wires make a wider bundle, as they do on paper");
    }

    // ---- How tightly a run turns at a knee is a figure, not a fixed share of its width ------------
    {
        // A loom does not kink at a corner, it bends - and how far it may be bent is set by what is
        // inside it and by what it is being dressed round, neither of which is the width of the line
        // on the paper. The radius used to be 1.2 times the bundle width and nothing else.
        auto bentRun = [](double knee, bool unset = false) {
            Entity w;
            w.id = QStringLiteral("BENT");
            w.type = EntityType::TwistedPair;
            w.points = {{0.0, 0.0}, {200.0, 0.0}, {200.0, -200.0}};
            w.a = w.points.front();
            w.b = w.points.back();
            w.meta.insert(QStringLiteral("twistStraight"), true);
            w.meta.insert(QStringLiteral("twistWires"), 4);
            w.meta.insert(QStringLiteral("twistWidth"), 10.0);
            w.meta.insert(QStringLiteral("twistPitch"), 8.0);
            if (!unset) w.meta.insert(QStringLiteral("twistKnee"), knee);
            return w;
        };
        // HOW ROUND THE CORNER IS, measured off what is drawn: how far the strands keep away from the
        // sharp corner the route has at (200,0). A tighter radius hugs it; a wider one swings clear.
        auto clearance = [](const Entity& e) {
            double best = 1e300;
            for (const std::vector<Vec2>& s : twistedPairStrands(e))
                for (const Vec2& p : s) best = std::min(best, distance(p, Vec2{200.0, 0.0}));
            return best;
        };

        const double loose = clearance(bentRun(60.0));
        const double tight = clearance(bentRun(5.0));
        const double byDefault = clearance(bentRun(0.0, /*unset*/ true));
        std::printf("  knee: radius 5 keeps %.2f from the corner, radius 60 keeps %.2f, auto %.2f\n",
                    tight, loose, byDefault);
        check(loose > tight + 1.0, "knee: a wider radius swings the run clear of the corner");
        // THE FIGURE ASKED FOR IS THE SMALLEST RADIUS IN THE BUNDLE, not the centreline's.
        //
        // Every wire runs alongside the centreline, so on the inside of a turn a wire bends tighter
        // than the line down the middle by however far off it sits. A radius is a build constraint -
        // the tightest the loom may be bent ANYWHERE in it - so reading it as the centreline's let
        // the inner wires bend tighter than the number somebody had just typed in to stop that.
        //
        // Measured off what is drawn, by how tightly the wires actually curve rather than by where
        // they pass: the tightest bend anywhere in the bundle is what a radius is a limit on. Three
        // points off a strand give the radius of the circle through them, so the smallest of those
        // over every strand near the corner is the tightest bend the drawing has.
        auto tightestBend = [](const Entity& e) {
            double best = 1e300;
            for (const std::vector<Vec2>& strand : twistedPairStrands(e))
                for (size_t i = 2; i < strand.size(); ++i) {
                    const Vec2 &p = strand[i - 2], &q = strand[i - 1], &t = strand[i];
                    if (distance(q, Vec2{200.0, 0.0}) > 120.0) continue;   // the corner, not the fans
                    const double a = distance(p, q), b = distance(q, t), c = distance(p, t);
                    const double area =
                        std::abs((q.x - p.x) * (t.y - p.y) - (t.x - p.x) * (q.y - p.y)) / 2.0;
                    if (area < 1e-9 || a < 1e-9 || b < 1e-9) continue;   // straight: no bend at all
                    best = std::min(best, a * b * c / (4.0 * area));
                }
            return best;
        };
        for (const double r : {20.0, 40.0, 60.0}) {
            const double bent = tightestBend(bentRun(r));
            std::printf("  knee: asked %.0f, the tightest wire in it bends on %.2f\n", r, bent);
            check(bent > r - 0.5, "knee: no wire bends tighter than the radius asked for");
        }
        // AND UNSET IS 1.2 TIMES THE BUNDLE AS IT IS DRAWN, which is not the same thing as 1.2 times
        // the cable width it used to be taken from. A straight run's width is its wires side by side
        // and has nothing to do with a cable width it does not use: nine four-millimetre wires are
        // fifty-two across and were being turned on a ten-millimetre radius, so the wires on the
        // inside of the corner were going round a radius of minus thirteen - inside out - which is
        // where the little triangles at the knee of a wide run came from. Asking for that same figure
        // LESS half the bundle comes to the same centreline, which is what pins it here.
        const double bundle = 2.0 * (kWiresRunPitch * 3.0 / 2.0) + kWiresRunWire;   // four wires
        const double asAsked = clearance(bentRun(std::max(10.0 * 1.2, bundle * 1.2) - bundle / 2.0));
        std::printf("  knee: auto %.2f against 1.2 bundles asked for %.2f\n", byDefault, asAsked);
        check(std::abs(byDefault - asAsked) < 1e-6,
              "knee: left alone, a run turns on 1.2 times the bundle it is drawn as");
        // AND NO WIRE IS BENT INSIDE OUT BY IT, which is the whole point of taking it off the bundle:
        // the innermost wire of a run left alone still turns on a radius, not on a loop.
        const double innermost = tightestBend(bentRun(0.0, /*unset*/ true));
        std::printf("  knee: left alone, the tightest wire in it bends on %.2f\n", innermost);
        check(innermost > bundle / 2.0,
              "knee: and left alone no wire in it is bent tighter than the bundle is wide");
    }

    // ---- The fan is a straight slant with a radius at each end of it -----------------------------
    {
        // A fan has two corners in it: where a wire leaves the bundle, and where it settles into the
        // lane it lands in. Both used to be rounded across the WHOLE fan - one long S with no
        // straight anywhere in it - which reads as a wire bending the entire way to its pin rather
        // than going there. What a fan looks like on paper, and in a loom, is a straight run of wire
        // with a radius at each end, and how big that radius is is a length like every other radius
        // on the sheet: asked for, or left to the drawing, or NOUGHT, which is a corner.
        auto fanned = [](double fanKnee) {
            Entity w;
            w.id = QStringLiteral("FAN");
            w.type = EntityType::TwistedPair;
            w.points = {{0.0, 0.0}, {300.0, 0.0}};
            w.a = w.points.front();
            w.b = w.points.back();
            w.meta.insert(QStringLiteral("twistStraight"), true);
            w.meta.insert(QStringLiteral("twistWires"), 6);
            w.meta.insert(QStringLiteral("twistWidth"), 12.0);
            w.meta.insert(QStringLiteral("twistPitch"), 8.0);
            w.meta.insert(QStringLiteral("twistEnds"), 60.0);
            w.meta.insert(QStringLiteral("twistLane"), 12.0);
            if (fanKnee > -0.5) w.meta.insert(QStringLiteral("twistFanKnee"), fanKnee);
            return w;
        };
        // HOW BENT THE MIDDLE OF THE SLANT IS. The outermost wire's own centre line - the midpoint
        // of its two edges, which is where the wire is - taken across the middle half of the fan and
        // measured against the straight line between its two ends. A slant is straight when that
        // comes to nothing; a fan rounded end to end bows away from it.
        auto slantBow = [](const Entity& e) {
            const std::vector<std::vector<Vec2>> s = twistedPairStrands(e);
            if (s.size() < 2) return -1.0;
            std::vector<Vec2> mid;
            for (size_t i = 0; i < s[0].size() && i < s[1].size(); ++i)
                mid.push_back(lerp(s[0][i], s[1][i], 0.5));
            // The fan runs from the tail (0.6 of the lead) to the lead; take the middle half of it.
            std::vector<Vec2> slant;
            for (const Vec2& v : mid)
                if (v.x > 36.0 + 0.25 * 24.0 && v.x < 36.0 + 0.75 * 24.0) slant.push_back(v);
            if (slant.size() < 3) return -1.0;
            double bow = 0.0;
            for (size_t i = 1; i + 1 < slant.size(); ++i)
                bow = std::max(bow,
                               closestPointOnSegment(slant[i], slant.front(), slant.back()).distance);
            return bow;
        };
        const double bowAuto = slantBow(fanned(-1.0));
        const double bowSharp = slantBow(fanned(0.0));
        const double bowWide = slantBow(fanned(40.0));
        std::printf("  fan: the middle of the slant bows %.4f left alone, %.4f at 0, %.4f at 40\n",
                    bowAuto, bowSharp, bowWide);
        check(bowSharp >= 0.0 && bowSharp < 1e-6,
              "fan: a radius of nought draws the slant dead straight, corner to corner");
        check(bowAuto >= 0.0 && bowAuto < 1e-6,
              "fan: and left alone the middle of the slant is straight too - the radius is at the "
              "ends of it, which is where a fan is drawn with one");
        check(bowWide > bowAuto + 1e-6,
              "fan: a radius too big for the fan to hold rounds the whole of it, as it must");

        // AND THE CORNER IS A CORNER when nought is asked for, where otherwise it is a bend. Measured
        // at the bundle end of the slant: how sharply the wire's own line turns there.
        auto cornerTurn = [](const Entity& e) {
            const std::vector<std::vector<Vec2>> s = twistedPairStrands(e);
            if (s.size() < 2) return 0.0;
            std::vector<Vec2> mid;
            for (size_t i = 0; i < s[0].size() && i < s[1].size(); ++i)
                mid.push_back(lerp(s[0][i], s[1][i], 0.5));
            double sharpest = 0.0;
            for (size_t i = 2; i < mid.size(); ++i) {
                if (mid[i].x > 70.0) break;
                const Vec2 d1 = mid[i - 1] - mid[i - 2], d2 = mid[i] - mid[i - 1];
                if (d1.length() < 1e-9 || d2.length() < 1e-9) continue;
                sharpest = std::max(sharpest, std::abs(angleOf(d2) - angleOf(d1)));
            }
            return sharpest * 180.0 / M_PI;
        };
        const double turnSharp = cornerTurn(fanned(0.0)), turnRound = cornerTurn(fanned(20.0));
        std::printf("  fan: the sharpest turn in it is %.1f deg at 0, %.1f deg on a 20 radius\n",
                    turnSharp, turnRound);
        check(turnSharp > turnRound + 5.0,
              "fan: nought turns the whole corner at once - a corner - and a radius spreads it");

        // AND THE WIRES STILL LAND WHERE THEY LAND. The radius rounds the corners of the fan; it does
        // not move the end of the wire, which is on a pin.
        auto endSpread = [](const Entity& e) {
            double lo = 1e300, hi = -1e300;
            for (const std::vector<Vec2>& s : twistedPairStrands(e))
                if (!s.empty()) { lo = std::min(lo, s.front().y); hi = std::max(hi, s.front().y); }
            return hi - lo;
        };
        const double spreadAuto = endSpread(fanned(-1.0)), spreadKnee = endSpread(fanned(8.0));
        std::printf("  fan: the end spreads %.2f left alone, %.2f filleted\n", spreadAuto,
                    spreadKnee);
        check(std::abs(spreadAuto - spreadKnee) < 0.05,
              "fan: rounding the corners does not move the wire ends");
    }

    // ---- The harness snaps on its wires, and trims and extends like the run it is ----------------
    {
        // A harness offered the snap system its ROUTE - the line down the middle of the bundle,
        // which is never drawn. So the cursor caught something between the wires and on none of
        // them, and the wires, the only part of a harness anybody measures from or lands anything
        // on, could not be snapped to at all.
        auto aRun = [](const QString& id, const std::vector<Vec2>& route) {
            Entity w;
            w.id = id;
            w.type = EntityType::TwistedPair;
            w.points = route;
            w.a = route.front();
            w.b = route.back();
            w.meta.insert(QStringLiteral("twistStraight"), true);
            w.meta.insert(QStringLiteral("twistWires"), 6);
            w.meta.insert(QStringLiteral("twistWidth"), 12.0);
            w.meta.insert(QStringLiteral("twistPitch"), 8.0);
            w.meta.insert(QStringLiteral("twistWireWidth"), 2.0);
            w.meta.insert(QStringLiteral("twistEnds"), 40.0);
            w.meta.insert(QStringLiteral("twistLane"), 10.0);
            return w;
        };

        // THE STRETCHES IT OFFERS ARE THE DRAWN LINES. Every one of them lies on a wire: the middle
        // of each is a point on a line somebody can see, which the middle of the route never was.
        const Entity run = aRun(QStringLiteral("SNAPRUN"), {{0.0, 0.0}, {300.0, 0.0}});
        const std::vector<std::pair<Vec2, Vec2>> straights = twistedPairStraights(run);
        const std::vector<std::vector<Vec2>> strands = twistedPairStrands(run);
        std::printf("  harness snap: %d straight stretch(es) off %d drawn line(s)\n",
                    int(straights.size()), int(strands.size()));
        check(!straights.empty(), "harness snap: a run offers the straight stretches it is drawn as");
        int offAxis = 0;
        double furthest = 0.0;
        for (const auto& seg : straights) {
            const Vec2 mid = lerp(seg.first, seg.second, 0.5);
            if (std::abs(mid.y) > 1e-6) ++offAxis;
            // How far the middle of the stretch is from the nearest drawn line.
            double best = 1e300;
            for (const std::vector<Vec2>& line : strands)
                for (size_t i = 1; i < line.size(); ++i)
                    best = std::min(best, closestPointOnSegment(mid, line[i - 1], line[i]).distance);
            furthest = std::max(furthest, best);
        }
        std::printf("  harness snap: %d of the middles are off the route, the furthest %.4f from a "
                    "drawn line\n",
                    offAxis, furthest);
        check(offAxis == int(straights.size()),
              "harness snap: not one of them is the route, which runs down y = 0 and is not drawn");
        check(furthest < 1e-6, "harness snap: every one of them is ON a line the drawing shows");
        // AND A LAID-UP BUNDLE OFFERS NONE, because a weave has no straight in it to speak of - it
        // keeps the route it always had rather than handing over a few hundred little facets.
        Entity laid = run;
        laid.id = QStringLiteral("LAID");
        laid.meta.insert(QStringLiteral("twistStraight"), false);
        check(twistedPairStraights(laid).empty(),
              "harness snap: a laid-up bundle offers none, and keeps the route it always had");

        // ---- TRIMMED, like the run it is ----
        {
            // A harness is parametric - a route and the figures the wires are laid out from - so
            // cutting it back means cutting the ROUTE back and letting the wires, the fan and the cut
            // ends be laid out again from what is left. Cutting the drawn wires instead would leave a
            // bundle of loose lines that is no longer a harness at all.
            Document d;
            d.entities().push_back(aRun(QStringLiteral("RUN"), {{0.0, 0.0}, {300.0, 0.0}}));
            Entity wall;
            wall.id = QStringLiteral("WALL");
            wall.type = EntityType::Line;
            wall.a = {200.0, -60.0};
            wall.b = {200.0, 60.0};
            d.entities().push_back(wall);
            Editor ed;
            ed.setDocument(&d);
            noSnap(ed);
            ed.setTool(ToolId::Trim);
            ed.onPointerPress({200.0, 55.0}, Qt::LeftButton, Qt::NoModifier);   // the wall is the edge
            // A fence drawn across the far half of the run: that is the piece that goes.
            ed.onPointerPress({260.0, -40.0}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerPress({260.0, 40.0}, Qt::LeftButton, Qt::NoModifier);
            // THERE IS NO SUCH THING AS HALF A HARNESS. It is a parametric object - a route, a wire
            // count, a fan at each end - and cutting the route back and letting it lay itself out
            // again picks the fan up and moves it to the cut, which is not what a cut looks like.
            // What is left is a set of cut wires, so that is what it becomes: the lines it draws.
            int harnesses = 0, lines = 0;
            double furthest = 0.0;
            for (const Entity& e : d.entities()) {
                if (e.type == EntityType::TwistedPair) ++harnesses;
                if (e.id == QStringLiteral("WALL")) continue;
                if (e.type != EntityType::Line && e.type != EntityType::Polyline) continue;
                ++lines;
                for (const Vec2& v : e.points) furthest = std::max(furthest, v.x);
                furthest = std::max({furthest, e.a.x, e.b.x});
            }
            std::printf("  harness trim: %d harness(es) and %d line(s) left, reaching to x %.1f\n",
                        harnesses, lines, furthest);
            check(harnesses == 0,
                  "harness trim: cut anywhere, it stops being a harness - there is no half of one");
            check(lines >= 12,
                  "harness trim: what is left is the lines it drew, which is what cut wires are");
            check(furthest <= 200.0 + 1e-6,
                  "harness trim: and none of them reaches past the boundary it was cut at");
        }

        // ---- AND A COVERED RUN COMES APART THE SAME WAY ----
        {
            // A cable or a length of tubing over the converged stretch is as much a part of what the
            // run draws as its wires are, so it is cut with them: what is left is the lines that
            // were there, whichever sort they came from. A body left behind whole beside cut wires
            // would be a cover over nothing.
            for (const char* sort : {"cable", "heatshrink"}) {
                Document d;
                Entity covered = aRun(QStringLiteral("RUN"), {{0.0, 0.0}, {300.0, 0.0}});
                covered.meta.insert(QStringLiteral("twistBody"), QString::fromLatin1(sort));
                d.entities().push_back(covered);
                Entity wall;
                wall.id = QStringLiteral("WALL");
                wall.type = EntityType::Line;
                wall.a = {200.0, -60.0};
                wall.b = {200.0, 60.0};
                d.entities().push_back(wall);
                Editor ed;
                ed.setDocument(&d);
                noSnap(ed);
                ed.setTool(ToolId::Trim);
                ed.onPointerPress({200.0, 55.0}, Qt::LeftButton, Qt::NoModifier);
                ed.onPointerPress({260.0, -40.0}, Qt::LeftButton, Qt::NoModifier);
                ed.onPointerPress({260.0, 40.0}, Qt::LeftButton, Qt::NoModifier);
                int harnesses = 0, pieces = 0;
                double furthest = 0.0;
                for (const Entity& e : d.entities()) {
                    if (e.type == EntityType::TwistedPair) ++harnesses;
                    if (e.id == QStringLiteral("WALL")) continue;
                    if (e.type != EntityType::Line && e.type != EntityType::Polyline) continue;
                    ++pieces;
                    for (const Vec2& v : e.points) furthest = std::max(furthest, v.x);
                    furthest = std::max({furthest, e.a.x, e.b.x});
                }
                std::printf("  covered trim: %s - %d harness(es), %d piece(s), reaching x %.1f\n",
                            sort, harnesses, pieces, furthest);
                check(harnesses == 0,
                      "covered trim: a covered run cut anywhere stops being a harness too");
                check(pieces >= 6,
                      "covered trim: and what is left is the lines it drew, cover and all");
                check(furthest <= 200.0 + 1e-6,
                      "covered trim: with none of them past the boundary - the cover is cut with "
                      "the wires, not left hanging whole beside them");
            }
        }

        // ---- AND EXTENDED ----
        {
            Document d;
            d.entities().push_back(aRun(QStringLiteral("RUN"), {{0.0, 0.0}, {200.0, 0.0}}));
            Entity wall;
            wall.id = QStringLiteral("WALL");
            wall.type = EntityType::Line;
            wall.a = {300.0, -60.0};
            wall.b = {300.0, 60.0};
            d.entities().push_back(wall);
            Editor ed;
            ed.setDocument(&d);
            noSnap(ed);
            ed.setTool(ToolId::Extend);
            ed.onPointerPress({300.0, 55.0}, Qt::LeftButton, Qt::NoModifier);   // the wall to reach
            ed.onPointerPress({190.0, -40.0}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerPress({190.0, 40.0}, Qt::LeftButton, Qt::NoModifier);
            const Entity* grown = nullptr;
            for (const Entity& e : d.entities())
                if (e.type == EntityType::TwistedPair) grown = &e;
            check(grown != nullptr, "harness extend: the run is still there");
            if (grown) {
                std::printf("  harness extend: 0..200 reaching for a wall on x = 300 now runs to "
                            "%.1f\n",
                            grown->points.back().x);
                check(std::abs(grown->points.back().x - 300.0) < 1e-6,
                      "harness extend: it reaches the boundary, like any other run");
                check(std::abs(grown->b.x - 300.0) < 1e-6,
                      "harness extend: and its stored far end goes with it");
            }
        }
    }

    // ---- The fan ends flush, and every wire is cut with a line across it -------------------------
    {
        // A fan ends with a straight stretch where every wire is parallel to the run again, so every
        // cut end lines up square across it - flush. That is how a fan is drawn, and it was always on
        // and always the same share of the fan: a drawing wanting a stated length of parallel wire to
        // land, or wanting the wires cut where they are still spreading, could say neither.
        //
        // AND A CUT END IS A LINE. Two edges and nothing else is an open channel - the eye reads them
        // as a wire because they run together, and then they simply stop.
        auto run = [](int flush, double flushLen, const QString& spread, int trunk) {
            Entity w;
            w.id = QStringLiteral("FLUSH%1-%2-%3-%4")
                       .arg(flush)
                       .arg(flushLen)
                       .arg(spread)
                       .arg(trunk);
            w.type = EntityType::TwistedPair;
            w.points = {{0.0, 0.0}, {300.0, 0.0}};
            w.a = w.points.front();
            w.b = w.points.back();
            w.meta.insert(QStringLiteral("twistStraight"), true);
            w.meta.insert(QStringLiteral("twistWires"), 6);
            w.meta.insert(QStringLiteral("twistWidth"), 12.0);
            w.meta.insert(QStringLiteral("twistPitch"), 8.0);
            w.meta.insert(QStringLiteral("twistWireWidth"), 2.0);
            w.meta.insert(QStringLiteral("twistEnds"), 60.0);
            w.meta.insert(QStringLiteral("twistLane"), 12.0);
            w.meta.insert(QStringLiteral("twistSpread"), spread);
            if (trunk > 0) w.meta.insert(QStringLiteral("twistTrunkWires"), trunk);
            if (flush == 0) w.meta.insert(QStringLiteral("twistFlush"), false);
            if (flushLen > 0.0) w.meta.insert(QStringLiteral("twistFlushLen"), flushLen);
            return w;
        };

        // ONE LINE ACROSS EACH WIRE AT EACH END IT IS CUT AT. Six wires fanning both ends is twelve.
        const Entity both = run(1, 0.0, QStringLiteral("both"), 0);
        const std::vector<std::vector<Vec2>> caps = twistedPairEndCaps(both);
        std::printf("  flush: %d line(s) across the ends of 6 wires fanning both ends\n",
                    int(caps.size()));
        check(caps.size() == 12, "flush: one line across each wire at each end it is cut at");
        // AND THEY ARE SQUARE ACROSS THE RUN, which is what flush means: the wire is parallel to the
        // run where it is cut, so the line across it is not.
        double leanest = 0.0, shortest = 1e300, longest = 0.0;
        for (const std::vector<Vec2>& c : caps) {
            if (c.size() != 2) continue;
            leanest = std::max(leanest, std::abs(c.front().x - c.back().x));
            const double len = distance(c.front(), c.back());
            shortest = std::min(shortest, len);
            longest = std::max(longest, len);
        }
        std::printf("  flush: the lines lean %.4f across the run and are %.3f to %.3f long\n",
                    leanest, shortest, longest);
        check(leanest < 1e-6, "flush: each line is square across the run, not across a slant");
        check(std::abs(shortest - 2.0) < 1e-6 && std::abs(longest - 2.0) < 1e-6,
              "flush: and exactly one wire wide, because that is what it closes");
        // And they sit AT the ends of the run, which is where the wires are cut.
        double nearest = 1e300;
        for (const std::vector<Vec2>& c : caps)
            if (c.size() == 2)
                nearest = std::min(nearest, std::min(c.front().x, 300.0 - c.front().x));
        check(nearest < 1e-6, "flush: on the ends of the run, where the wires are landed");

        // A WIRE THAT STOPS INSIDE THE BUNDLE IS NOT CUT THERE. Converged to two, four of the six
        // run into the bundle and stop - and get no line across them where they do, because they are
        // carrying on inside the lines the bundle is drawn as. Fanning one end only, each of those
        // four is cut once and the two that carry on are cut twice.
        const Entity oneEnd = run(1, 0.0, QStringLiteral("left"), 2);
        std::printf("  flush: converged to 2 and fanning one end, %d line(s)\n",
                    int(twistedPairEndCaps(oneEnd).size()));
        check(twistedPairEndCaps(oneEnd).size() == 8,
              "flush: a wire that stops in the bundle is not cut there - four cut once, two twice");

        // HOW LONG THE FLUSH STRETCH IS can be asked for. The outermost wire is level from the end
        // until the fan starts, and that is what the figure sets.
        auto levelFor = [](const Entity& e) {
            const std::vector<std::vector<Vec2>> s = twistedPairStrands(e);
            if (s.empty()) return -1.0;
            const std::vector<Vec2>& a = s.front();
            if (a.empty()) return -1.0;
            const double endY = a.front().y;
            double level = 0.0;
            for (const Vec2& v : a) {
                if (v.x > 150.0) break;
                if (std::abs(v.y - endY) < 1e-6) level = std::max(level, v.x);
            }
            return level;
        };
        const double byDefault = levelFor(both);
        const double asked = levelFor(run(1, 45.0, QStringLiteral("both"), 0));
        std::printf("  flush: level for %.1f left alone, %.1f when 45 is asked for\n", byDefault,
                    asked);
        // Read off the drawing, so it is only as fine as the places the fan is measured at - one
        // every lead/24, which is 2.5 here.
        check(std::abs(byDefault - 60.0 * kWiresRunTailShare) < 2.6,
              "flush: left alone it is the share of the fan it always was");
        check(std::abs(asked - 45.0) < 2.6, "flush: and asked for, it is the length asked for");

        // TURNED OFF, THE WIRES ARE CUT WHERE THEY ARE STILL SPREADING - so there is no level
        // stretch at all, and the line across each wire leans with it.
        const Entity loose = run(0, 0.0, QStringLiteral("both"), 0);
        double lean = 0.0;
        for (const std::vector<Vec2>& c : twistedPairEndCaps(loose))
            if (c.size() == 2) lean = std::max(lean, std::abs(c.front().x - c.back().x));
        std::printf("  flush: unticked, level for %.1f and the lines lean %.3f\n", levelFor(loose),
                    lean);
        check(levelFor(loose) < 0.6,
              "flush: unticked, the wires are cut where they are still spreading");
        check(lean > 0.1, "flush: and the line across a slanting wire leans with it");

        // AND ASKING FOR MORE THAN THE FAN HAS does not leave the wires nowhere to spread: the fan
        // keeps a stretch of its own whatever is asked for.
        const double greedy = levelFor(run(1, 500.0, QStringLiteral("both"), 0));
        std::printf("  flush: asked for 500 on a 60 fan, level for %.1f\n", greedy);
        check(greedy > 50.0 && greedy < 60.0,
              "flush: more than the fan can give leaves the fan a stretch to spread in");

        // ---- AND THE ENDS CAN BE DRAWN STRIPPED ----
        {
            // A wire is landed by taking the insulation off the last few millimetres of it and
            // crimping the bare conductor into a contact, and a harness drawing says how much comes
            // off: the strip length. THE BARE PART IS THINNER THAN THE WIRE - what is left when the
            // insulation comes off is the strands - and that is the whole of how the eye tells one
            // from the other. The edge steps in where the insulation stops.
            Entity bare = run(1, 0.0, QStringLiteral("both"), 0);
            bare.id = QStringLiteral("STRIP");
            bare.meta.insert(QStringLiteral("twistStrip"), true);
            bare.meta.insert(QStringLiteral("twistStripLen"), 7.0);
            // How wide one wire is drawn at a given x, between its two edges.
            auto wideAt = [](const Entity& e, double x) {
                const std::vector<std::vector<Vec2>> s = twistedPairStrands(e);
                if (s.size() < 2) return -1.0;
                auto yAt = [x](const std::vector<Vec2>& line) {
                    for (size_t i = 1; i < line.size(); ++i) {
                        const Vec2 p = line[i - 1], q = line[i];
                        if ((p.x - x) * (q.x - x) > 0.0) continue;
                        const double span = q.x - p.x;
                        return std::abs(span) < 1e-12 ? p.y
                                                      : p.y + (q.y - p.y) * (x - p.x) / span;
                    }
                    return 0.0;
                };
                return std::abs(yAt(s[0]) - yAt(s[1]));
            };
            const double atTip = wideAt(bare, 2.0);
            const double pastIt = wideAt(bare, 20.0);
            std::printf("  strip: the wire is %.2f across 2 in and %.2f across 20 in\n", atTip,
                        pastIt);
            check(std::abs(pastIt - 2.0) < 1e-6,
                  "strip: past the strip length the wire is the width it is");
            check(std::abs(atTip - 1.0) < 1e-6,
                  "strip: and over the stripped part it is the conductor, which is thinner");
            // TWO LINES ACROSS EACH END: the cut, which closes the CONDUCTOR, and the end of the
            // insulation, which goes right across the WIRE. The edges step in where the insulation
            // stops and those two little steps are most of that end - but not the middle of it,
            // where the bare conductor passes through, and two stubs with a gap between them is
            // neither what it looks like nor something the cursor can catch.
            double atCut = 0.0, atInsulation = 0.0;
            int acrossTheWire = 0;
            for (const std::vector<Vec2>& m : twistedPairEndCaps(bare)) {
                if (m.size() != 2) continue;
                const double len = distance(m.front(), m.back());
                const double from = std::min(std::abs(m.front().x), std::abs(m.front().x - 300.0));
                if (from < 1e-6) atCut = len;
                if (std::abs(from - 7.0) < 1e-6) {
                    atInsulation = len;
                    ++acrossTheWire;
                }
            }
            std::printf("  strip: the line across the cut is %.2f, the one at the insulation %.2f, "
                        "and there are %d of the latter\n",
                        atCut, atInsulation, acrossTheWire);
            check(std::abs(atCut - 1.0) < 1e-6,
                  "strip: the line across the cut closes the conductor, not the wire");
            check(std::abs(atInsulation - 2.0) < 1e-6,
                  "strip: and the one where the insulation ends goes right across the wire");
            check(acrossTheWire == 12,
                  "strip: one for each of the six wires at each of its two ends");
            // WHICH MEANS THE CURSOR CAN CATCH IT. Every line across an end is offered to the snap
            // system, so the split between insulation and conductor is a place to measure from.
            {
                Document snapDoc;
                snapDoc.entities().push_back(bare);
                SnapSettings only;
                only.snapEndpoint = false;
                only.snapMidpoint = true;
                only.snapNearest = false;
                only.snapCenter = false;
                only.snapGeometricCenter = false;
                only.snapNode = false;
                only.snapIntersection = false;
                only.snapPerpendicular = false;
                only.snapTangent = false;
                only.snapQuadrant = false;
                // The middle of the line where the insulation ends on the outermost wire.
                double wantY = 0.0;
                for (const std::vector<Vec2>& m : twistedPairEndCaps(bare))
                    if (m.size() == 2 && std::abs(m.front().x - 7.0) < 1e-6)
                        wantY = std::max(wantY, (m.front().y + m.back().y) / 2.0);
                const auto hit = resolveSnap(snapDoc, {7.4, wantY + 0.4}, std::nullopt, 3.0, only, {});
                std::printf("  strip: asked near the split at 7.0,%.2f and caught %s at %.2f,%.2f\n",
                            wantY, hit.snapped ? "a point" : "nothing", hit.point.x, hit.point.y);
                check(hit.snapped && std::abs(hit.point.x - 7.0) < 1e-6 &&
                          std::abs(hit.point.y - wantY) < 1e-6,
                      "strip: the cursor catches the line that splits insulation from conductor");
            }
            // SAYING NOTHING LEAVES THE WIRE ITS OWN WIDTH ALL THE WAY OUT, which is what a drawing
            // that does not call out a strip length shows.
            Entity plain = bare;
            plain.id = QStringLiteral("NOSTRIP");
            plain.meta.remove(QStringLiteral("twistStrip"));
            check(std::abs(wideAt(plain, 2.0) - 2.0) < 1e-6,
                  "strip: unasked for, the wire is its own width right to the cut");
            // AND IT NEVER REACHES PAST THE END IT IS PART OF: a stripped stretch out on the bundle
            // is a thin bundle.
            Entity greedyStrip = bare;
            greedyStrip.id = QStringLiteral("GREEDY");
            greedyStrip.meta.insert(QStringLiteral("twistStripLen"), 500.0);
            // Measured a little past the end the strip belongs to. The wire is slanting through the
            // fan there, so the gap straight up the page is a touch more than the wire is wide - what
            // says the strip stopped is that it is a wire and not a conductor.
            const double onTheBundle = wideAt(greedyStrip, 58.0);
            std::printf("  strip: asked for 500 on a 60 fan, the wire is %.2f across 58 in\n",
                        onTheBundle);
            check(onTheBundle > 1.8,
                  "strip: more than the end can spare stops at the end, not out on the bundle");
        }
    }

    // ---- The converged stretch can be a cable under heatshrink instead of loose wires ------------
    {
        // A loom between its two fans is a covered thing - a cable, or a sleeve over the bundle -
        // and drawing the two or three lines its wires make there says the wires are on show when
        // they are not. THE FANS ARE UNTOUCHED: the wires come out of it at both ends exactly as they
        // did, and what changes is only the part between them.
        auto run = [](const char* body) {
            Entity w;
            w.id = QStringLiteral("BODY%1").arg(QString::fromLatin1(body));
            w.type = EntityType::TwistedPair;
            w.points = {{0.0, 0.0}, {320.0, 0.0}};
            w.a = w.points.front();
            w.b = w.points.back();
            w.meta.insert(QStringLiteral("twistStraight"), true);
            w.meta.insert(QStringLiteral("twistWires"), 8);
            w.meta.insert(QStringLiteral("twistWidth"), 12.0);
            w.meta.insert(QStringLiteral("twistPitch"), 8.0);
            w.meta.insert(QStringLiteral("twistWireWidth"), 2.5);
            w.meta.insert(QStringLiteral("twistTrunkWires"), 3);
            w.meta.insert(QStringLiteral("twistLane"), 11.0);
            w.meta.insert(QStringLiteral("twistEnds"), 55.0);
            if (body[0])
                w.meta.insert(QStringLiteral("twistBody"), QString::fromLatin1(body));
            return w;
        };
        // The SLEEVE sort: a length of tubing over the run, shrunk onto what it grips at each end.
        // The cable sort is cut square and has a boot at each end instead - see below.
        const Entity wires = run(""), cable = run("heatshrink");
        const std::vector<std::vector<Vec2>> body = twistedPairBody(cable);
        std::printf("  cable body: %d outline(s) asked for, %d when it is not\n", int(body.size()),
                    int(twistedPairBody(wires).size()));
        check(twistedPairBody(wires).empty(),
              "cable body: a run drawn as its wires has no body, which is what it always was");

        // WHAT IS DRAWN IS WHAT IS PICKED. The wires of a run with a body stop at the fans - what
        // lies between them is the body, and a wire drawn through it would be a wire drawn through a
        // cable. The strands said so; the WEAVE, which is what the renderer actually draws, worked
        // the ranges out for itself and knew nothing about the body, so a cable was picked as a
        // cable and drawn as its bare wires - running the whole length and splaying out from the
        // boot face to their full width in the middle. Both now read the one answer.
        {
            auto crossesMiddle = [](const Entity& e) {
                double from = 1e300, to = -1e300;
                for (const Vec2& p : e.points) { from = std::min(from, p.x); to = std::max(to, p.x); }
                const double mid = (from + to) / 2.0;
                int drawn = 0;
                for (const auto& stretch : twistedPairWeave(e))
                    for (const auto& wire : stretch.wires)
                        for (size_t i = 1; i < wire.edgeA.size(); ++i)
                            if ((wire.edgeA[i - 1].x - mid) * (wire.edgeA[i].x - mid) <= 0.0) ++drawn;
                int picked = 0;
                for (const auto& s : twistedPairStrands(e))
                    for (size_t i = 1; i < s.size(); ++i)
                        if ((s[i - 1].x - mid) * (s[i].x - mid) <= 0.0) ++picked;
                return std::make_pair(drawn, picked);
            };
            const auto covered = crossesMiddle(cable);
            check(covered.first == 0 && covered.second == 0,
                  "cable body: no wire is drawn through the body, and none is picked there either");
            const auto bare = crossesMiddle(wires);
            check(bare.first > 0 && bare.second > 0,
                  "cable body: with no body the wires do run the whole way, as they always have");
        }
        check(body.size() == 1, "cable body: asked for, the converged stretch is one body");
        if (body.size() == 1) {
        const std::vector<Vec2>& shell = body.front();
        check(shell.size() > 3 && distance(shell.front(), shell.back()) < 1e-9,
              "cable body: and it is closed, because it is a body and not a run of lines");

        // A CABLE IS TWO LINES WIDE, WHATEVER IT CARRIES, and shrunk onto what it grips at each
        // end. Two of these 2.5 mm wires at a 3.75 pitch stand 6.25 apart, so the cover is 6.25
        // across - half of it 3.125 - whether there are eight wires inside it or eighty, and the
        // face is a millimetre inside that at each end.
        double widest = 0.0, atTheEnd = 0.0;
        double from = 1e300, to = 0.0;
        for (const Vec2& v : shell) {
            widest = std::max(widest, std::abs(v.y));
            from = std::min(from, v.x);
            to = std::max(to, v.x);
            if (std::abs(v.x - 55.0) < 1e-6) atTheEnd = std::max(atTheEnd, std::abs(v.y));
        }
        std::printf("  cable body: %.1f..%.1f along the run, %.3f half-width in the middle and "
                    "%.3f where it meets the fan\n",
                    from, to, widest, atTheEnd);
        check(std::abs(from - 55.0) < 1e-6 && std::abs(to - 265.0) < 1e-6,
              "cable body: it runs from the end of one fan to the start of the other, and no further");
        check(std::abs(widest - 3.125) < 1e-6,
              "cable body: as wide as two hook-up lines, and not as wide as the bundle it covers");
        check(std::abs(atTheEnd - 2.125) < 1e-6,
              "cable body: and shrunk a little inside that where it meets the fan");

        // AND NO WIRE RUNS THROUGH IT. A wire drawn through a cable is a wire drawn through a cable.
        // Counted by what CROSSES the middle, not by points landing in it: down the bundle a wire is
        // one straight step from fan to fan with nothing measured along the way.
        auto crossing = [](const Entity& e, double x) {
            int n = 0;
            for (const std::vector<Vec2>& strand : twistedPairStrands(e))
                for (size_t i = 1; i < strand.size(); ++i)
                    if ((strand[i - 1].x - x) * (strand[i].x - x) <= 0.0 &&
                        std::abs(strand[i - 1].x - strand[i].x) > 1e-9) {
                        ++n;
                        break;
                    }
            return n;
        };
        const int inTheMiddle = crossing(cable, 160.0);
        const int wiresInTheMiddle = crossing(wires, 160.0);
        std::printf("  cable body: %d wire line(s) cross the middle, against %d without it\n",
                    inTheMiddle, wiresInTheMiddle);
        check(inTheMiddle == 0, "cable body: no wire is drawn through the cable");
        check(wiresInTheMiddle > 0,
              "cable body: where it is not asked for, the wires run the whole way as before");

        // THE FANS ARE UNTOUCHED, which is the whole condition on this: every wire end is where it
        // was, so nothing landed on a pin has moved.
        auto endsOf = [](const Entity& e) {
            std::vector<double> ys;
            for (const std::vector<Vec2>& s : twistedPairStrands(e))
                if (!s.empty() && s.front().x < 1e-6) ys.push_back(s.front().y);
            std::sort(ys.begin(), ys.end());
            return ys;
        };
        const std::vector<double> wasThere = endsOf(wires), stillThere = endsOf(cable);
        bool same = wasThere.size() == stillThere.size() && !wasThere.empty();
        for (size_t i = 0; same && i < wasThere.size(); ++i)
            if (std::abs(wasThere[i] - stillThere[i]) > 1e-9) same = false;
        std::printf("  cable body: %d wire end(s) at the start, and they %s where they were\n",
                    int(stillThere.size()), same ? "are" : "are NOT");
        check(same, "cable body: the fan is untouched - every wire ends where it always did");
        }

        // ---- AND THE CABLE IS A THING WITH FIGURES ON IT ----
        {
            // How wide it is, and at each end how far across its face is and over what length it
            // comes down to that. A cover over a loom is shrunk onto a connector at one end and onto
            // a breakout at the other, and those are not the same size - so the two ends can be
            // described apart, though most of the time they are not.
            auto measured = [](const Entity& e) {
                struct Read { double widest = 0.0, atStart = 0.0, atEnd = 0.0; };
                Read r;
                const std::vector<std::vector<Vec2>> body = twistedPairBody(e);
                if (body.empty()) return r;
                double from = 1e300, to = -1e300;
                for (const Vec2& v : body.front()) {
                    from = std::min(from, v.x);
                    to = std::max(to, v.x);
                }
                for (const Vec2& v : body.front()) {
                    r.widest = std::max(r.widest, std::abs(v.y) * 2.0);
                    if (std::abs(v.x - from) < 1e-6) r.atStart = std::max(r.atStart, std::abs(v.y) * 2.0);
                    if (std::abs(v.x - to) < 1e-6) r.atEnd = std::max(r.atEnd, std::abs(v.y) * 2.0);
                }
                return r;
            };
            Entity wide = run("heatshrink");
            wide.id = QStringLiteral("WIDE");
            wide.meta.insert(QStringLiteral("twistBodyWidth"), 40.0);
            wide.meta.insert(QStringLiteral("twistBodyFace"), 20.0);
            const auto got = measured(wide);
            std::printf("  cable body: asked 40 across with a 20 face - %.2f across, %.2f at the "
                        "ends\n",
                        got.widest, got.atStart);
            check(std::abs(got.widest - 40.0) < 1e-6,
                  "cable body: it is drawn the width it is asked for");
            check(std::abs(got.atStart - 20.0) < 1e-6 && std::abs(got.atEnd - 20.0) < 1e-6,
                  "cable body: and shrunk at each end to the size asked for");

            // THE SHRUNK LENGTH IS HOW FAR IN IT TAKES TO GET THERE. Measured by where the outline
            // reaches full width: a long shoulder reaches it further in than a short one, and NOUGHT
            // is a square shoulder, which is a thing a drawing can want.
            auto reachesFullAt = [](const Entity& e) {
                const std::vector<std::vector<Vec2>> body = twistedPairBody(e);
                if (body.empty()) return -1.0;
                double widest = 0.0, from = 1e300;
                for (const Vec2& v : body.front()) {
                    widest = std::max(widest, std::abs(v.y));
                    from = std::min(from, v.x);
                }
                double at = 1e300;
                for (const Vec2& v : body.front())
                    if (std::abs(std::abs(v.y) - widest) < 1e-6) at = std::min(at, v.x);
                return at - from;
            };
            Entity shoulder = wide;
            shoulder.id = QStringLiteral("SHOULDER");
            shoulder.meta.insert(QStringLiteral("twistBodyShrink"), 18.0);
            Entity square = wide;
            square.id = QStringLiteral("SQUARE");
            square.meta.insert(QStringLiteral("twistBodyShrink"), 0.0);
            std::printf("  cable body: full width %.1f in on an 18 shrink, %.1f in on nought\n",
                        reachesFullAt(shoulder), reachesFullAt(square));
            check(std::abs(reachesFullAt(shoulder) - 18.0) < 1e-6,
                  "cable body: the shrunk length is how far in it takes to reach full width");
            check(reachesFullAt(square) < 1e-6,
                  "cable body: and nought is a square shoulder, which a drawing can ask for");

            // THE TWO ENDS CAN BE DESCRIBED APART, and are not unless they are told to be.
            Entity apart = wide;
            apart.id = QStringLiteral("APART");
            apart.meta.insert(QStringLiteral("twistBodyFaceFinish"), 32.0);
            const auto together = measured(wide);
            const auto split = measured(apart);
            std::printf("  cable body: ends %.1f and %.1f alike, %.1f and %.1f once parted\n",
                        together.atStart, together.atEnd, split.atStart, split.atEnd);
            check(std::abs(split.atStart - 20.0) < 1e-6 && std::abs(split.atEnd - 20.0) < 1e-6,
                  "cable body: a finish figure is ignored while both ends are said to be alike");
            apart.meta.insert(QStringLiteral("twistBodySidesSame"), false);
            const auto parted = measured(apart);
            std::printf("  cable body: parted, the ends are %.1f and %.1f\n", parted.atStart,
                        parted.atEnd);
            check(std::abs(parted.atStart - 20.0) < 1e-6 && std::abs(parted.atEnd - 32.0) < 1e-6,
                  "cable body: parted, the finish end shrinks to its own size and the start to its");
            // AND A FACE CANNOT FLARE WIDER THAN THE CABLE IT IS THE END OF.
            Entity flared = wide;
            flared.id = QStringLiteral("FLARED");
            flared.meta.insert(QStringLiteral("twistBodyFace"), 500.0);
            const auto held = measured(flared);
            std::printf("  cable body: asked for a 500 face on a 40 cable, got %.1f\n", held.atStart);
            check(std::abs(held.atStart - 40.0) < 1e-6,
                  "cable body: a face wider than the cable is held to the cable, not flared past it");
        }

        // ---- AND A CABLE IS CUT SQUARE, WITH A BOOT AT EACH END ----
        {
            // A cable running the length of the run does not shrink onto anything: it is one width
            // end to end and it stops at a face square across it. What takes it down to the wires is
            // the TRANSITION HEATSHRINK at each end - the boot - fitted over the end of the cable,
            // standing proud of it, and shrunk down at the end facing the fan. That is what a
            // breakout looks like, and it is the difference between this sort and the length of
            // tubing above.
            const Entity asCable = run("cable");
            const std::vector<std::vector<Vec2>> drawn = twistedPairBody(asCable);
            std::printf("  cable: %d outline(s) - a body and a boot at each end\n",
                        int(drawn.size()));
            check(drawn.size() == 3,
                  "cable: three outlines - the cable, and the boot at each end of it");
            check(twistedPairBody(run("heatshrink")).size() == 1,
                  "cable: where a length of tubing is asked for there is one, and no boot");
            if (drawn.size() == 3) {
            // THE CABLE IS SQUARE: as wide at its ends as in the middle.
            double widest = 0.0, atEnds = 0.0, from = 1e300, to = -1e300;
            for (const Vec2& v : drawn.front()) {
                widest = std::max(widest, std::abs(v.y));
                from = std::min(from, v.x);
                to = std::max(to, v.x);
            }
            for (const Vec2& v : drawn.front())
                if (std::abs(v.x - from) < 1e-6 || std::abs(v.x - to) < 1e-6)
                    atEnds = std::max(atEnds, std::abs(v.y));
            std::printf("  cable: %.3f across the middle and %.3f at the ends\n", widest * 2.0,
                        atEnds * 2.0);
            check(std::abs(widest - atEnds) < 1e-6,
                  "cable: one width end to end, cut square, not recovered onto anything");

            // AND THE BOOTS SIT ON THE ENDS OF THE CABLE, one at each, shrunk toward the fan.
            int overTheFan = 0;
            for (size_t i = 1; i < drawn.size(); ++i) {
                double lo = 1e300, hi = -1e300, atCable = 0.0, atMouth = 0.0;
                for (const Vec2& v : drawn[i]) {
                    lo = std::min(lo, v.x);
                    hi = std::max(hi, v.x);
                }
                // Which end of the boot is against the cable: the one nearer the middle of the run.
                const bool atStartFan = hi <= 160.0;
                const double cableSide = atStartFan ? hi : lo;
                const double mouthSide = atStartFan ? lo : hi;
                for (const Vec2& v : drawn[i]) {
                    if (std::abs(v.x - cableSide) < 1e-6) atCable = std::max(atCable, std::abs(v.y));
                    if (std::abs(v.x - mouthSide) < 1e-6) atMouth = std::max(atMouth, std::abs(v.y));
                }
                std::printf("  cable: a boot from %.1f to %.1f, %.2f across on the cable and %.2f "
                            "at the shrunk end\n",
                            lo, hi, atCable * 2.0, atMouth * 2.0);
                if (atCable > atMouth + 1e-6) ++overTheFan;
            }
            check(overTheFan == 2,
                  "cable: each boot stands over the cable and shrinks toward the fan - a breakout");
            }

            // AND THE BODY IS A DRAWN THING LIKE THE WIRES: it takes a joint in the middle of every
            // straight of it and the cursor catches it. Where the stretch between the fans is a
            // cable there are no wires along it at all, so the body is the only thing there - and
            // it was the one part of such a run with nothing to take hold of and nothing to catch.
            for (const char* sort : {"cable", "heatshrink"}) {
                Document d;
                Entity e = run(sort);
                e.id = QStringLiteral("BODY");
                d.entities().push_back(e);
                Editor ed;
                ed.setDocument(&d);
                noSnap(ed);
                ed.selectById(QStringLiteral("BODY"));
                const std::vector<std::pair<Vec2, Vec2>> straights = twistedPairStraights(e);
                // A straight of the body: one that runs along the middle of the run, where no wire
                // goes once the stretch is covered.
                int alongTheBody = 0;
                for (const auto& seg : straights) {
                    const Vec2 mid = lerp(seg.first, seg.second, 0.5);
                    if (mid.x > 80.0 && mid.x < 240.0) ++alongTheBody;
                }
                const std::vector<Vec2> joints = ed.selectionGripPoints();
                int onTheBody = 0;
                for (const Vec2& j : joints)
                    if (j.x > 80.0 && j.x < 240.0) ++onTheBody;
                std::printf("  body: %s - %d straight(s) along it, %d joint(s) on it\n", sort,
                            alongTheBody, onTheBody);
                check(alongTheBody >= 2,
                      "body: its two sides are straights of the run, which is what a cursor catches");
                check(onTheBody >= 2,
                      "body: and each takes a joint in its middle, like every other line here");
            }
        }
    }

    // ---- Entry faces: Shift lands a run on one, sized to it; Alt shows them; a part is held by it -
    {
        // A part's entry face: a vertical line 40 long at x = 200, marked as its connection line.
        auto sheet = [](Document& d) {
            Entity face;
            face.id = QStringLiteral("FACE");
            face.type = EntityType::Line;
            face.a = {200.0, -20.0};
            face.b = {200.0, 20.0};
            face.meta.insert(QLatin1String(kConnectionLineKey), true);
            d.entities().push_back(face);
        };
        auto last = [](const Document& d) -> const Entity* {
            return d.entities().empty() ? nullptr : &d.entities().back();
        };
        {
            Document d;
            sheet(d);
            Editor ed;
            ed.setDocument(&d);
            noSnap(ed);
            check(ed.entryFaceAt({200.5, 7.0}).has_value() && !ed.entryFaceAt({150.0, 0.0}),
                  "entry face: found under the cursor, and only there");
            ed.setAltHeld(true);
            check(ed.altHeld(), "entry face: Alt held says so");
            ed.setAltHeld(false);
        }
        // A HARNESS finished with Shift on the face: arrives at its middle, the fan over 95% of it.
        {
            Document d;
            sheet(d);
            Editor ed;
            ed.setDocument(&d);
            noSnap(ed);
            ed.setTool(ToolId::WiresRun);
            ed.setTwistWires(4);
            ed.onPointerMove({0.0, 0.0});
            ed.onPointerPress({0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerMove({200.0, 7.0});
            ed.onPointerPress({200.0, 7.0}, Qt::LeftButton, Qt::ShiftModifier);
            const Entity* run = last(d);
            check(run && run->type == EntityType::TwistedPair && !ed.isDrafting(),
                  "face fit: Shift on the face lays the harness there");
            if (run && run->type == EntityType::TwistedPair) {
                const TwistFigures f = twistedPairFigures(*run);
                const double laneB = run->meta.value(QStringLiteral("twistLaneFinish")).toDouble();
                const double spread = laneB * 3.0 + f.wireWidth;
                std::printf("  face fit: harness ends at %.1f,%.1f, finish fan %.2f of 40 (start lane %.2f)\n",
                            run->points.back().x, run->points.back().y, spread, f.lane);
                check(distance(run->points.back(), Vec2{200.0, 0.0}) < 1e-6,
                      "face fit: it arrives at the middle of the face");
                check(std::abs(spread - 38.0) < 1e-6, "face fit: its fan spans 95% of the face");
                check(std::abs(f.lane - laneB) > 1e-3,
                      "face fit: and the end it started at keeps its own spread");
            }
        }
        // STARTED on the face, at the scale set in the options.
        {
            Document d;
            sheet(d);
            Editor ed;
            ed.setDocument(&d);
            noSnap(ed);
            ed.setFaceFanScale(80.0);
            ed.setTool(ToolId::WiresRun);
            ed.setTwistWires(4);
            ed.onPointerMove({200.0, -9.0});
            ed.onPointerPress({200.0, -9.0}, Qt::LeftButton, Qt::ShiftModifier);
            ed.onPointerMove({350.0, 0.0});
            ed.onPointerPress({350.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
            ed.onKey(Qt::Key_Return, Qt::NoModifier);
            const Entity* run = last(d);
            if (run && run->type == EntityType::TwistedPair) {
                const TwistFigures f = twistedPairFigures(*run);
                check(distance(run->points.front(), Vec2{200.0, 0.0}) < 1e-6 &&
                          std::abs(f.lane * 3.0 + f.wireWidth - 32.0) < 1e-6,
                      "face fit: started on a face, the start fan spans the share set (80%)");
            } else {
                check(false, "face fit: a run started on a face is laid");
            }
        }
        // AND THE PITCH SET ON THE RIBBON AFTERWARDS IS THE ONE DRAWN. Landed on the face the fan
        // starts at 95% of it; pinned with Tab, a pitch typed into the field used to be put straight
        // back to the face's, so the field did nothing.
        {
            Document d;
            sheet(d);
            Editor ed;
            ed.setDocument(&d);
            noSnap(ed);
            ed.setTool(ToolId::Fanout);
            ed.setTwistWires(4);
            ed.onPointerMove({200.0, -9.0});
            ed.onPointerPress({200.0, -9.0}, Qt::LeftButton, Qt::ShiftModifier);
            ed.onPointerMove({350.0, 0.0});
            const double faceLane = ed.faceFitLane(false);
            const TwistFigures pf = ed.twistPreviewFigures();
            std::printf("  face pitch: landed, the field shows %.2f\n", faceLane);
            check(std::abs(faceLane * 3.0 + pf.wireWidth - 38.0) < 1e-6,
                  "face pitch: landed, the field shows the pitch that spans 95% of the face");
            ed.freezeTwistedPreview();
            ed.setTwistLane(7.0);
            check(ed.faceFitLane(false) == 0.0, "face pitch: a pitch typed after landing is the one");
            ed.setTwistLane(0.0);
            check(std::abs(ed.faceFitLane(false) - faceLane) < 1e-9,
                  "face pitch: and Auto gives it back the face's");
            ed.setTwistLane(7.0);
            ed.onPointerPress({350.0, 0.0}, Qt::LeftButton, Qt::NoModifier);   // unpins
            ed.onPointerPress({350.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
            ed.onKey(Qt::Key_Return, Qt::NoModifier);
            const Entity* run = last(d);
            if (run && run->type == EntityType::TwistedPair) {
                const TwistFigures f = twistedPairFigures(*run);
                std::printf("  face pitch: laid with lane %.2f\n", f.lane);
                check(distance(run->points.front(), Vec2{200.0, 0.0}) < 1e-6 &&
                          std::abs(f.lane - 7.0) < 1e-6,
                      "face pitch: the fanout is laid on the face at the pitch typed on the ribbon");
            } else {
                check(false, "face pitch: the fanout is laid");
            }
        }
        // A CABLE and A SLEEVE: 60% of the face across.
        for (const bool sleeve : {false, true}) {
            Document d;
            sheet(d);
            Editor ed;
            ed.setDocument(&d);
            noSnap(ed);
            ed.setTool(sleeve ? ToolId::HeatshrinkRoute : ToolId::DoublePolyline);
            ed.onPointerMove({0.0, 0.0});
            ed.onPointerPress({0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerMove({200.0, 12.0});
            ed.onPointerPress({200.0, 12.0}, Qt::LeftButton, Qt::ShiftModifier);
            const Entity* run = last(d);
            const double gap = run ? run->meta.value(QStringLiteral("gap")).toDouble() : 0.0;
            std::printf("  face fit: %s %.1f across\n", sleeve ? "sleeve" : "cable", gap);
            check(run && run->id != QStringLiteral("FACE") && std::abs(gap - 24.0) < 1e-6 &&
                      !ed.isDrafting() && !run->points.empty() &&
                      distance(run->points.back(), Vec2{200.0, 0.0}) < 1e-6,
                  sleeve ? "face fit: a sleeve landed on a face is 60% of it across, centred on it"
                         : "face fit: a cable landed on a face is 60% of it across, centred on it");
        }
        // A LIBRARY PART is held by its entry face, so the snap lands the face on a line.
        {
            Document d;
            Entity rail;
            rail.id = QStringLiteral("RAIL");
            rail.type = EntityType::Line;
            rail.a = {0.0, 100.0};
            rail.b = {400.0, 100.0};
            d.entities().push_back(rail);
            Editor ed;
            ed.setDocument(&d);
            ed.setPixelTolerance(6);
            std::vector<Entity> part;
            for (const auto& [a, b, conn] :
                 {std::tuple<Vec2, Vec2, bool>{{0, 0}, {30, 0}, false},
                  std::tuple<Vec2, Vec2, bool>{{30, 0}, {30, 20}, true},
                  std::tuple<Vec2, Vec2, bool>{{30, 20}, {0, 20}, false}}) {
                Entity l;
                l.type = EntityType::Line;
                l.a = a;
                l.b = b;
                l.meta.insert(QStringLiteral("libPart"), QStringLiteral("p1"));
                if (conn) l.meta.insert(QLatin1String(kConnectionLineKey), true);
                part.push_back(l);
            }
            ed.placeEntities(part);
            // Near the rail, off any of its points: only Nearest can put it on the line.
            ed.onPointerMove({173.0, 101.5});
            ed.onPointerPress({173.0, 101.5}, Qt::LeftButton, Qt::NoModifier);
            Vec2 faceMid{0, 0};
            for (const Entity& e : d.entities())
                if (e.meta.value(QLatin1String(kConnectionLineKey)).toBool())
                    faceMid = (e.a + e.b) * 0.5;
            std::printf("  part placed: entry face middle at %.2f,%.2f\n", faceMid.x, faceMid.y);
            check(std::abs(faceMid.y - 100.0) < 1e-6 && std::abs(faceMid.x - 173.0) < 1e-6,
                  "part placement: held by its entry face, which Nearest lands on the line");
        }
    }

    // ---- A library part's OUTPUT FACES are its entry faces on the sheet ---------------------------
    {
        // A box 30 x 20 whose right-hand side is marked in the library as the face the wires leave by.
        PartsCatalog cat;
        cat.setRemembering(false);
        QString partId;
        {
            LibraryPart& part = cat.active().add(PartKind::Connector);
            partId = part.id;
            QJsonArray drawing;
            for (const auto& [a, b] : {std::pair<Vec2, Vec2>{{0, -10}, {30, -10}},
                                       std::pair<Vec2, Vec2>{{30, -10}, {30, 10}},
                                       std::pair<Vec2, Vec2>{{30, 10}, {0, 10}},
                                       std::pair<Vec2, Vec2>{{0, 10}, {0, -10}}}) {
                Entity l;
                l.type = EntityType::Line;
                l.a = a;
                l.b = b;
                drawing.append(l.toJson());
            }
            part.mechanical = drawing;
            part.outputs.push_back({{30, -10}, {30, 10}});
        }
        const LibraryPart* part = cat.byId(partId);
        const std::vector<Entity> placed = part ? partBlockInstance(*part, true, Vec2{0, 0})
                                                : std::vector<Entity>{};
        check(!placed.empty() && placed.front().meta.contains(QLatin1String(kEntryFacesKey)),
              "output faces: a placed part carries the faces its wires enter by");

        // Placed, TURNED a quarter round and moved: the face goes with it, and Alt / Shift find it.
        Document d;
        for (Entity e : placed) {
            rotateEntity(e, Vec2{0, 0}, M_PI / 2.0);
            translateEntity(e, Vec2{100, 50});
            e.id = QStringLiteral("P%1").arg(d.entities().size());
            d.entities().push_back(e);
        }
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        const auto faces = ed.entryFaces();
        const Vec2 mid = faces.empty() ? Vec2{} : (faces.front().first + faces.front().second) * 0.5;
        std::printf("  output faces: %d on the sheet, the turned one's middle at %.1f,%.1f\n",
                    int(faces.size()), mid.x, mid.y);
        check(faces.size() == 1 && distance(mid, Vec2{100, 80}) < 1e-6,
              "output faces: one per copy, and it turned and moved with the part");

        // A CABLE landed on it with Shift: centred on the face, 60% of it across.
        ed.setTool(ToolId::DoublePolyline);
        ed.onPointerMove({100, 200});
        ed.onPointerPress({100, 200}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({104, 80.4});
        ed.onPointerPress({104, 80.4}, Qt::LeftButton, Qt::ShiftModifier);
        const Entity& cable = d.entities().back();
        check(cable.meta.value(QStringLiteral("double")).toBool() &&
                  std::abs(cable.meta.value(QStringLiteral("gap")).toDouble() - 12.0) < 1e-6 &&
                  distance(cable.points.back(), Vec2{100, 80}) < 1e-6,
              "output faces: Shift lands a cable on the middle of one, 60% of it across");

        // PUT DOWN BEFORE FACES WERE CARRIED: a copy mirrored and turned, with no faces on it, gets
        // them from the library by matching its drawing.
        std::vector<Entity> old;
        for (Entity e : placed) {
            e.meta.remove(QLatin1String(kEntryFacesKey));
            mirrorEntity(e, Vec2{0, 0}, Vec2{1, 0});
            rotateEntity(e, Vec2{0, 0}, M_PI);
            translateEntity(e, Vec2{-40, 7});
            old.push_back(e);
        }
        // what the face would be, the same way round
        Entity probe;
        probe.type = EntityType::Line;
        probe.a = {30, -10};
        probe.b = {30, 10};
        mirrorEntity(probe, Vec2{0, 0}, Vec2{1, 0});
        rotateEntity(probe, Vec2{0, 0}, M_PI);
        translateEntity(probe, Vec2{-40, 7});
        const int got = recoverEntryFaces(old, cat);
        const QJsonArray f = old.front().meta.value(QLatin1String(kEntryFacesKey)).toArray();
        const Vec2 fa = f.size() >= 4 ? Vec2{f[0].toDouble(), f[1].toDouble()} : Vec2{1e9, 1e9};
        const Vec2 fb = f.size() >= 4 ? Vec2{f[2].toDouble(), f[3].toDouble()} : Vec2{1e9, 1e9};
        check(got == 1 && distance(fa, probe.a) < 1e-6 && distance(fb, probe.b) < 1e-6,
              "output faces: a copy placed before, mirrored and turned, gets them where they belong");
    }

    // ---- A fanout is not turned round by the first Shift, only by the next ----------------------
    {
        Document d;
        Editor ed;
        ed.setDocument(&d);
        ed.setTool(ToolId::Fanout);
        const bool was = ed.fanoutAtFinish();
        ed.toggleShiftMode();
        check(ed.fanoutAtFinish() == was, "fanout: the first Shift does not turn it round");
        ed.toggleShiftMode();
        check(ed.fanoutAtFinish() != was, "fanout: the second one does");
        ed.toggleShiftMode();
        check(ed.fanoutAtFinish() == was, "fanout: and each one after it");
        ed.setTool(ToolId::Fanout);
        ed.toggleShiftMode();
        check(ed.fanoutAtFinish() == was, "fanout: a fresh fanout starts counting again");
        // Nor does a Shift-click at the start - that is how a face is landed on.
        noSnap(ed);
        ed.setTool(ToolId::Fanout);
        ed.onPointerMove({0, 0});
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::ShiftModifier);
        check(ed.fanoutAtFinish() == was, "fanout: a Shift-click to start it does not turn it");
    }

    // ---- Align: by the objects' own edges, or on a line clicked through the drawing -------------
    {
        // Three boxes at odd places, and a label-like GROUP of two lines that has to move as one:
        // lining up a group's pieces by their own edges would take it apart.
        auto box = [](const QString& id, double x, double y, double w, double h) {
            Entity e;
            e.id = id;
            e.type = EntityType::Rect;
            e.points = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
            e.a = {x, y};
            e.b = {x + w, y + h};
            return e;
        };
        auto sheet = [&](Document& d) {
            d.entities().push_back(box(QStringLiteral("A"), 10.0, 0.0, 20.0, 10.0));
            d.entities().push_back(box(QStringLiteral("B"), 50.0, 30.0, 40.0, 15.0));
            d.entities().push_back(box(QStringLiteral("C"), 25.0, 70.0, 10.0, 25.0));
            // The group: two lines, one longer than the other, stamped as one object.
            for (const auto& [id, a, b] :
                 {std::tuple<QString, Vec2, Vec2>{QStringLiteral("G1"), {60.0, 100.0}, {90.0, 100.0}},
                  std::tuple<QString, Vec2, Vec2>{QStringLiteral("G2"), {70.0, 105.0}, {80.0, 105.0}}}) {
                Entity l;
                l.id = id;
                l.type = EntityType::Line;
                l.a = a;
                l.b = b;
                stampEntityGroup(l, QStringLiteral("GROUP-1"));
                d.entities().push_back(l);
            }
        };
        auto edges = [](const Document& d, const QString& id) {
            for (const Entity& e : d.entities())
                if (e.id == id) return e.bounds();
            return Bounds();
        };
        const QStringList all{QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"),
                              QStringLiteral("G1"), QStringLiteral("G2")};
        struct Case { Editor::AlignMode mode; const char* name; };
        for (const Case& k : {Case{Editor::AlignMode::Left, "left"},
                              Case{Editor::AlignMode::Right, "right"},
                              Case{Editor::AlignMode::Top, "top"},
                              Case{Editor::AlignMode::Bottom, "bottom"}}) {
            Document d;
            sheet(d);
            Editor ed;
            ed.setDocument(&d);
            noSnap(ed);
            ed.setTool(ToolId::Select);
            for (const QString& id : all) ed.selectById(id, /*add=*/true);
            ed.alignSelection(k.mode);
            // Each OBJECT'S edge: the group's is its two lines taken together.
            auto objEdge = [&](const QString& id) {
                Bounds b = edges(d, id);
                if (id == QStringLiteral("G1")) {
                    const Bounds g2 = edges(d, QStringLiteral("G2"));
                    b.expand(g2.min);
                    b.expand(g2.max);
                }
                switch (k.mode) {
                case Editor::AlignMode::Left: return b.min.x;
                case Editor::AlignMode::Right: return b.max.x;
                case Editor::AlignMode::Top: return b.max.y;
                default: return b.min.y;
                }
            };
            const double a = objEdge(QStringLiteral("A")), b = objEdge(QStringLiteral("B")),
                         c = objEdge(QStringLiteral("C")), g = objEdge(QStringLiteral("G1"));
            std::printf("  align %s: A %.1f, B %.1f, C %.1f, group %.1f\n", k.name, a, b, c, g);
            check(std::abs(a - b) < 1e-9 && std::abs(a - c) < 1e-9 && std::abs(a - g) < 1e-9,
                  "align: every object's edge on the one line");
            const double expect = k.mode == Editor::AlignMode::Left    ? 10.0
                                  : k.mode == Editor::AlignMode::Right ? 90.0
                                  : k.mode == Editor::AlignMode::Top   ? 105.0
                                                                       : 0.0;
            check(std::abs(a - expect) < 1e-9,
                  "align: and that line is the selection's own furthest edge, which stays put");
            // The group moved as one: its two lines are still 5 apart and still offset by 10.
            const Bounds g1 = edges(d, QStringLiteral("G1")), g2 = edges(d, QStringLiteral("G2"));
            check(std::abs((g2.min.x - g1.min.x) - 10.0) < 1e-9 &&
                      std::abs((g2.min.y - g1.min.y) - 5.0) < 1e-9,
                  "align: a group is one object - its pieces keep their places in it");
            ed.undo();
            check(std::abs(edges(d, QStringLiteral("B")).min.x - 50.0) < 1e-9 &&
                      std::abs(edges(d, QStringLiteral("B")).min.y - 30.0) < 1e-9,
                  "align: and one undo puts every object back");
        }

        // BY THE CURSOR: nothing moves until the click, the click is SNAPPED, and each object's
        // middle goes on the line - a line to hang things on is where their middles hang.
        {
            Document d;
            sheet(d);
            // A point of the drawing to snap to, at an x nobody would click exactly.
            Entity post;
            post.id = QStringLiteral("POST");
            post.type = EntityType::Line;
            post.a = {213.4, -40.0};
            post.b = {213.4, -20.0};
            d.entities().push_back(post);
            Editor ed;
            ed.setDocument(&d);
            ed.setTool(ToolId::Select);
            for (const QString& id : {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")})
                ed.selectById(id, /*add=*/true);
            ed.alignSelection(Editor::AlignMode::CursorX);
            check(ed.aligningToCursor(), "align by cursor: it waits for the click that says where");
            check(std::abs(edges(d, QStringLiteral("B")).min.x - 50.0) < 1e-9,
                  "align by cursor: and nothing has moved yet");
            // Near the post's end, not on it: the snap is what puts the line exactly there.
            ed.onPointerMove({213.1, -40.2});
            ed.onPointerPress({213.1, -40.2}, Qt::LeftButton, Qt::NoModifier);
            auto mid = [&](const QString& id) {
                const Bounds b = edges(d, id);
                return (b.min.x + b.max.x) / 2.0;
            };
            std::printf("  align by cursor: middles at %.2f, %.2f, %.2f\n", mid(QStringLiteral("A")),
                        mid(QStringLiteral("B")), mid(QStringLiteral("C")));
            check(!ed.aligningToCursor(), "align by cursor: the click is taken, and it is done");
            check(std::abs(mid(QStringLiteral("A")) - 213.4) < 1e-6 &&
                      std::abs(mid(QStringLiteral("B")) - 213.4) < 1e-6 &&
                      std::abs(mid(QStringLiteral("C")) - 213.4) < 1e-6,
                  "align by cursor: every middle on the line - snapped to the point, not near it");
            // AND ESC GIVES IT UP with nothing moved.
            ed.alignSelection(Editor::AlignMode::CursorY);
            const double wasY = edges(d, QStringLiteral("B")).min.y;
            ed.onKey(Qt::Key_Escape, Qt::NoModifier);
            ed.onPointerPress({0.0, 500.0}, Qt::LeftButton, Qt::NoModifier);
            check(!ed.aligningToCursor() &&
                      std::abs(edges(d, QStringLiteral("B")).min.y - wasY) < 1e-9,
                  "align by cursor: Esc gives it up, and the next click is just a click");
        }

        // AND WITH NOTHING PICKED THERE IS NOTHING TO LINE UP - said, not silently ignored.
        {
            Document d;
            sheet(d);
            Editor ed;
            ed.setDocument(&d);
            QString said;
            QObject::connect(&ed, &Editor::message, [&said](const QString& m) { said = m; });
            ed.alignSelection(Editor::AlignMode::Left);
            check(said.contains(QStringLiteral("select")),
                  "align: with nothing picked it says to pick the objects first");
        }
    }

    // ---- Shift and pull on the end of a run carries it on ---------------------------------------
    {
        // Hold Shift, press on the end of a harness, a cable or a sleeve, and pull: the run carries
        // on from there, the way it was drawn. It used to want the run PICKED first and then taken
        // by the one point down the middle of its end - two gestures for one thought, and on a run
        // that was not already picked, pressing its end with Shift did nothing at all.
        auto harness = [](const QString& id) {
            Entity w;
            w.id = id;
            w.type = EntityType::TwistedPair;
            w.points = {{0.0, 0.0}, {100.0, 0.0}};
            w.a = w.points.front();
            w.b = w.points.back();
            w.meta.insert(QStringLiteral("twistStraight"), true);
            w.meta.insert(QStringLiteral("twistWires"), 4);
            w.meta.insert(QStringLiteral("twistWidth"), 9.0);
            w.meta.insert(QStringLiteral("twistPitch"), 18.0);
            w.meta.insert(QStringLiteral("twistEnds"), 20.0);
            w.meta.insert(QStringLiteral("twistLane"), 6.0);
            return w;
        };
        Document d;
        d.entities().push_back(harness(QStringLiteral("RUN")));
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.setTool(ToolId::Select);
        check(ed.selection().empty(), "pull: nothing is picked beforehand");
        // A WIRE TIP at the far end - what the hand goes for - not the route's own end point, which
        // is in the middle of the fan where nothing is drawn.
        const Vec2 tip{100.0, 9.0};
        ed.onPointerPress(tip, Qt::LeftButton, Qt::ShiftModifier);
        std::printf("  pull: Shift on an unpicked run's wire tip - carrying it on %d\n",
                    ed.continuingRun() ? 1 : 0);
        check(ed.continuingRun(), "pull: Shift on the visible end of a run takes it up, picked or not");
        check(ed.runBeingCarriedOn() == QStringLiteral("RUN"),
              "pull: the run itself is left out of the drawing - the ghost is the whole of it, longer");
        // PULLED and let go: that is the first new knee.
        ed.onPointerMove({100.0, -60.0});
        ed.onPointerRelease({100.0, -60.0}, Qt::LeftButton);
        ed.onPointerMove({160.0, -60.0});
        ed.onPointerPress({160.0, -60.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onKey(Qt::Key_Return, Qt::NoModifier);
        const Entity* run = nullptr;
        for (const Entity& e : d.entities())
            if (e.id == QStringLiteral("RUN")) run = &e;
        check(run != nullptr && d.entities().size() == 1,
              "pull: it is the SAME run carried on, not a new one beside it");
        check(ed.runBeingCarriedOn().isEmpty(), "pull: and once laid, the drawing has it back");
        if (run) {
            std::printf("  pull: the run now has %d point(s), ending at %.1f,%.1f\n",
                        int(run->points.size()), run->points.back().x, run->points.back().y);
            check(run->points.size() == 4, "pull: the pull and the click each put down a knee");
            const bool pulledKnee = run->points.size() >= 3 &&
                                    distance(run->points[2], Vec2{100.0, -60.0}) < 1e-6;
            check(pulledKnee, "pull: and the first of them is where the pull was let go");
            check(distance(run->points.back(), Vec2{160.0, -60.0}) < 1e-6,
                  "pull: and the run finishes where it was last clicked");
        }
        // AND SHIFT IN THE MIDDLE OF A RUN STILL MEANS SELECTION: only the end is taken up.
        Document d2;
        d2.entities().push_back(harness(QStringLiteral("MID")));
        Editor e2;
        e2.setDocument(&d2);
        noSnap(e2);
        e2.setTool(ToolId::Select);
        e2.onPointerPress({50.0, 0.0}, Qt::LeftButton, Qt::ShiftModifier);
        check(!e2.continuingRun(),
              "pull: Shift in the middle of a run still adds it to the selection, as it always did");
    }

    // ---- A pulled run takes typed lengths knee to knee ------------------------------------------
    {
        // Pulled out of its end and a length typed with the button still down: that is the pull's
        // knee, that far from the end - and letting go does not put another one where the mouse is.
        // The next length is from that knee, and so on.
        Document d;
        Entity w;
        w.id = QStringLiteral("RUN");
        w.type = EntityType::TwistedPair;
        w.points = {{0.0, 0.0}, {100.0, 0.0}};
        w.a = w.points.front();
        w.b = w.points.back();
        w.meta.insert(QStringLiteral("twistStraight"), true);
        w.meta.insert(QStringLiteral("twistWires"), 4);
        w.meta.insert(QStringLiteral("twistEnds"), 20.0);
        w.meta.insert(QStringLiteral("twistLane"), 6.0);
        d.entities().push_back(w);
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.setTool(ToolId::Select);
        ed.onPointerPress({100.0, 9.0}, Qt::LeftButton, Qt::ShiftModifier);
        check(ed.continuingRun(), "typed pull: Shift on the end takes the run up");
        ed.onPointerMove({100.0, -60.0});
        ed.acceptCommandInput(QStringLiteral("80"));
        ed.onPointerRelease({100.0, -60.0}, Qt::LeftButton);
        ed.onPointerMove({300.0, -80.0});
        ed.acceptCommandInput(QStringLiteral("120"));
        ed.onKey(Qt::Key_Return, Qt::NoModifier);
        const Entity* run = d.entities().empty() ? nullptr : &d.entities().front();
        if (run) {
            for (const Vec2& v : run->points) std::printf("  typed pull: knee %.1f,%.1f\n", v.x, v.y);
            check(run->points.size() == 4 &&
                      distance(run->points[2], Vec2{100.0, -80.0}) < 1e-6 &&
                      distance(run->points[3], Vec2{220.0, -80.0}) < 1e-6,
                  "typed pull: each length is from the knee before it - end to knee, knee to knee");
        } else {
            check(false, "typed pull: the run is still there");
        }
        // AND A FRESH FANOUT, drawn with lengths alone.
        Document d2;
        Editor e2;
        e2.setDocument(&d2);
        noSnap(e2);
        e2.setTool(ToolId::Fanout);
        e2.setTwistWires(4);
        e2.onPointerMove({0.0, 0.0});
        e2.onPointerPress({0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
        e2.onPointerMove({500.0, 0.0});
        e2.acceptCommandInput(QStringLiteral("150"));
        e2.onPointerMove({150.0, 500.0});
        e2.acceptCommandInput(QStringLiteral("90"));
        e2.onKey(Qt::Key_Return, Qt::NoModifier);
        const Entity* fan = d2.entities().empty() ? nullptr : &d2.entities().back();
        check(fan && fan->points.size() == 3 &&
                  distance(fan->points[1], Vec2{150.0, 0.0}) < 1e-6 &&
                  distance(fan->points[2], Vec2{150.0, 90.0}) < 1e-6,
              "typed fanout: start to knee, then knee to knee");
        // PINNED (Tab) and the cursor gone off somewhere else: the length goes the way the run on the
        // screen points, and puts its knee down - it used to do nothing but let go of the pin. Enter
        // then ends the run at that knee.
        Document d3;
        Editor e3;
        e3.setDocument(&d3);
        noSnap(e3);
        e3.setTool(ToolId::Fanout);
        e3.setTwistWires(4);
        e3.onPointerMove({0.0, 0.0});
        e3.onPointerPress({0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
        e3.onPointerMove({200.0, 0.0});
        e3.freezeTwistedPreview();
        e3.onPointerMove({-150.0, 80.0});
        e3.acceptCommandInput(QStringLiteral("60"));
        check(!e3.twistedFrozen(), "typed pinned: the number lets go of the pin");
        e3.onKey(Qt::Key_Return, Qt::NoModifier);
        const Entity* pinned = d3.entities().empty() ? nullptr : &d3.entities().back();
        if (pinned)
            std::printf("  typed pinned: %d point(s), ending at %.1f,%.1f\n", int(pinned->points.size()),
                        pinned->points.back().x, pinned->points.back().y);
        check(pinned && pinned->points.size() == 2 &&
                  distance(pinned->points.back(), Vec2{60.0, 0.0}) < 1e-6,
              "typed pinned: the knee goes along the pinned run, and Enter ends it there");
    }

    // ---- the gathered wires run straight, or laid up in pairs, or every other pair -------------
    {
        // Drawn as its wires, the body IS those wires, and a bundle of hook-up wire is not the only
        // thing a drawing office runs between two fans: pairs are laid up against interference, and
        // a loom that carries both is drawn with both. The fans are untouched by any of it - the
        // weave is a whole number of turns, so every wire is back on its own lane where the fan
        // meets it - which is what lets a pair be laid up without disturbing anything else.
        auto laid = [](const QString& id, const char* lay, int wires) {
            Entity w;
            w.id = id;
            w.type = EntityType::TwistedPair;
            w.points = {{0.0, 0.0}, {300.0, 0.0}};
            w.a = w.points.front();
            w.b = w.points.back();
            w.meta.insert(QStringLiteral("twistStraight"), true);
            w.meta.insert(QStringLiteral("twistWires"), wires);
            w.meta.insert(QStringLiteral("twistWidth"), 9.0);
            w.meta.insert(QStringLiteral("twistPitch"), 30.0);
            w.meta.insert(QStringLiteral("twistEnds"), 45.0);
            w.meta.insert(QStringLiteral("twistLane"), 12.0);
            if (lay) w.meta.insert(QStringLiteral("twistLay"), QString::fromUtf8(lay));
            return w;
        };
        // How many of the drawn lines move up and down BETWEEN the fans, and how far the furthest
        // of them goes: a wire running straight in its lane does neither.
        auto swinging = [](const Entity& e, double* most) {
            int moving = 0;
            *most = 0.0;
            for (const std::vector<Vec2>& s : twistedPairStrands(e)) {
                double lo = 1e300, hi = -1e300;
                for (const Vec2& v : s)
                    if (v.x > 60.0 && v.x < 240.0) {
                        lo = std::min(lo, v.y);
                        hi = std::max(hi, v.y);
                    }
                if (hi <= lo) continue;
                *most = std::max(*most, hi - lo);
                if (hi - lo > 0.5) ++moving;
            }
            return moving;
        };
        double flat = 0.0, all = 0.0, some = 0.0, odd = 0.0;
        const int nFlat = swinging(laid(QStringLiteral("LAY0"), nullptr, 6), &flat);
        const int nAll = swinging(laid(QStringLiteral("LAY1"), "pairs", 6), &all);
        const int nSome = swinging(laid(QStringLiteral("LAY2"), "mixed", 6), &some);
        const int nOdd = swinging(laid(QStringLiteral("LAY3"), "pairs", 5), &odd);
        std::printf("  lay: straight %d line(s) move (%.2f), pairs %d (%.2f), mixed %d (%.2f), "
                    "five wires %d (%.2f)\n", nFlat, flat, nAll, all, nSome, some, nOdd, odd);
        check(nFlat == 0 && flat < 1e-6,
              "lay: straight, every wire holds its lane from one fan to the other");
        check(nAll == 12, "lay: laid up in pairs, all six wires go round each other");
        check(nSome == 8, "lay: mixed, every other pair does - four wires of the six");
        check(nOdd == 8, "lay: and the odd wire of five has nobody to twist with, so it runs on");
        // AND THE FAN IS UNTOUCHED: the wires meet it exactly where they always did.
        auto fanEnds = [](const Entity& e) {
            std::vector<double> ys;
            for (const std::vector<Vec2>& s : twistedPairStrands(e))
                for (const Vec2& v : s)
                    if (v.x < 1e-6) ys.push_back(v.y);
            std::sort(ys.begin(), ys.end());
            return ys;
        };
        const std::vector<double> before = fanEnds(laid(QStringLiteral("LAY4"), nullptr, 6));
        const std::vector<double> after = fanEnds(laid(QStringLiteral("LAY5"), "pairs", 6));
        bool same = before.size() == after.size() && !before.empty();
        for (size_t i = 0; same && i < before.size(); ++i)
            same = std::abs(before[i] - after[i]) < 1e-9;
        std::printf("  lay: %d wire end(s) at the fan, %s once the pairs are laid up\n",
                    int(before.size()), same ? "where they were" : "MOVED");
        check(same, "lay: laying the pairs up moves nothing about the fan at either end");
    }

    // ---- A BENT CABLE IS STILL A CABLE, and the transition at each end of it is a part ----------
    {
        // Three things, all about the stretch between the fans: it survives a bend, the boot at
        // each end of it can be turned off, and where there is one the wires come out of its mouth
        // rather than out from underneath it.
        auto covered = [](const QString& id, const std::vector<Vec2>& route, const char* sort,
                          bool transition) {
            Entity w;
            w.id = id;
            w.type = EntityType::TwistedPair;
            w.points = route;
            w.a = route.front();
            w.b = route.back();
            w.meta.insert(QStringLiteral("twistStraight"), true);
            w.meta.insert(QStringLiteral("twistWires"), 6);
            w.meta.insert(QStringLiteral("twistWidth"), 12.0);
            w.meta.insert(QStringLiteral("twistPitch"), 8.0);
            w.meta.insert(QStringLiteral("twistWireWidth"), 2.0);
            w.meta.insert(QStringLiteral("twistEnds"), 40.0);
            w.meta.insert(QStringLiteral("twistLane"), 10.0);
            w.meta.insert(QStringLiteral("twistKnee"), 30.0);
            w.meta.insert(QStringLiteral("twistBody"), QString::fromLatin1(sort));
            if (!transition) w.meta.insert(QStringLiteral("twistBodyTransition"), false);
            return w;
        };
        // A ROUTE WITH KNEES IN IT. The cover is laid along the sampled route, whose legs are the
        // facets of a rounded knee - a fraction of a millimetre each - so a test that measured
        // those legs against the width of the bundle refused every bend there has ever been, and
        // the cable quietly turned back into hook-up wires the moment the run was bent.
        const std::vector<Vec2> bent{{0.0, 0.0}, {160.0, 0.0}, {240.0, -70.0}, {380.0, -70.0}};
        auto offTheRoute = [&bent](const Vec2& p) {
            double least = 1e300;
            for (size_t i = 1; i < bent.size(); ++i)
                least = std::min(least, distance(p, closestPointOnSegment(p, bent[i - 1], bent[i]).point));
            return least;
        };
        for (const char* sort : {"cable", "heatshrink"}) {
            const Entity round = covered(QStringLiteral("BENT"), bent, sort, true);
            const std::vector<std::vector<Vec2>> drawn = twistedPairBody(round);
            double reach = 0.0;
            for (const std::vector<Vec2>& outline : drawn)
                for (const Vec2& v : outline) reach = std::max(reach, offTheRoute(v));
            std::printf("  bent body: %s - %d outline(s), the furthest point %.2f off the route\n",
                        sort, int(drawn.size()), reach);
            check(!drawn.empty(), "bent body: a run taken round a corner keeps the cover it was given");
            check(reach < 60.0,
                  "bent body: and the cover lies along the route, not away across the sheet");
        }

        // THE TRANSITION IS A TICK. A cable has one at each end unless the drawing says otherwise;
        // a length of tubing over the whole run never has one, because it IS the transition.
        const std::vector<Vec2> flat{{0.0, 0.0}, {300.0, 0.0}};
        const Entity booted = covered(QStringLiteral("BOOT"), flat, "cable", true);
        const Entity bare = covered(QStringLiteral("BARE"), flat, "cable", false);
        std::printf("  transition: %d outline(s) with the boots, %d without\n",
                    int(twistedPairBody(booted).size()), int(twistedPairBody(bare).size()));
        check(twistedPairBody(booted).size() == 3,
              "transition: a cable takes a boot at each end of it");
        check(twistedPairBody(bare).size() == 1,
              "transition: and unticked it runs straight out into its own fan");
        check(twistedPairBody(covered(QStringLiteral("TUBE"), flat, "heatshrink", true)).size() == 1,
              "transition: a length of tubing over the run has none either way");

        // AND IT IS A SLEEVE OVER THE END OF THE CABLE. It reaches back ALONG the cable, standing
        // proud of it, and it steps straight onto the cable at the body end - that end is not
        // recovered, it is where the part begins, and the step is drawn at the cable's own width so
        // that face and the cable's are one line. THE SHRUNK END IS THE ONE FACING THE FAN, because
        // that is the end that grips what comes out of it.
        const std::vector<std::vector<Vec2>> parts = twistedPairBody(booted);
        if (parts.size() == 3) {
            double cableFrom = 1e300, cableHalf = 0.0;
            for (const Vec2& v : parts.front()) {
                cableFrom = std::min(cableFrom, v.x);
                cableHalf = std::max(cableHalf, std::abs(v.y));
            }
            // The boot at the START end: the one whose points are all before the middle of the run.
            double lo = 1e300, hi = -1e300, widest = 0.0, onTheCable = 1e300, atTheFan = 0.0;
            for (size_t i = 1; i < parts.size(); ++i) {
                double least = 1e300, most = -1e300;
                for (const Vec2& v : parts[i]) {
                    least = std::min(least, v.x);
                    most = std::max(most, v.x);
                }
                if (most > 150.0) continue;   // the finish end is the same part the other way round
                lo = least;
                hi = most;
                for (const Vec2& v : parts[i]) {
                    widest = std::max(widest, std::abs(v.y));
                    // The step onto the cable is the NARROWEST the outline gets at that end - the
                    // widest there is the sleeve itself, which stands proud of it.
                    if (std::abs(v.x - most) < 1e-6) onTheCable = std::min(onTheCable, std::abs(v.y));
                    if (std::abs(v.x - least) < 1e-6) atTheFan = std::max(atTheFan, std::abs(v.y));
                }
            }
            // How far into the run the wires at the start end are drawn - where the fan comes out.
            // Measured off the EDGES of the wires, which are offset across each one, so the last of
            // a slanting wire sits a fraction of its own width beyond the place it stops at.
            auto fanTo = [](const Entity& e) {
                double most = 0.0;
                for (const std::vector<Vec2>& strand : twistedPairStrands(e))
                    for (const Vec2& v : strand)
                        if (v.x < 150.0) most = std::max(most, v.x);
                return most;
            };
            const double wireTo = fanTo(booted), bareTo = fanTo(bare);
            std::printf("  transition: the boot runs %.2f..%.2f, %.2f across, %.2f where it grips "
                        "the %.2f cable, and the wires stop at %.2f against %.2f with no boot\n",
                        lo, hi, widest * 2.0, onTheCable * 2.0, cableHalf * 2.0, wireTo, bareTo);
            check(std::abs(lo - 40.0) < 1e-6,
                  "transition: it begins at the end of the cable, where the fan begins");
            check(hi > 40.0 + 1e-6,
                  "transition: and it reaches back ALONG the cable, which is what it is fitted to");
            check(std::abs(onTheCable - cableHalf) < 1e-6,
                  "transition: stepping onto it at that end at the width of the cable itself");
            check(widest > cableHalf + 1e-6,
                  "transition: and standing wider over the rest of itself, being fitted over it");
            check(atTheFan < widest - 1e-6 && atTheFan > 1e-6,
                  "transition: the end facing the fan is the shrunk one - that is the end that grips");
            // AND IT IS AS LONG AS A PART LIKE THIS COMES: three times across the face it grips.
            // Taken off the length of the FAN instead, as it was, the same boot grew and shrank
            // with a figure that has nothing to do with it.
            check(std::abs((hi - lo) - 3.0 * 2.0 * atTheFan) < 0.5,
                  "transition: and three times across that face long, which is how one is sized");
            check(std::abs(cableFrom - hi) < 1e-6,
                  "transition: the cable starts where the boot grips it, so that face is drawn once");
            check(std::abs(wireTo - bareTo) < 0.5,
                  "transition: and the whole fan is on show - the boot covers cable, not wires");
            check(wireTo > lo - 1.5,
                  "transition: which the wires run right up to, the fan starting at that face");
        }

        // AND THE SHRUNK LENGTH IS MEASURED FROM THAT FACE INWARD. Asked for more of it, the
        // shoulder moves toward the BODY and the face stays where the wires leave: a shrunk length
        // that moved the face would move the place the fan comes from every time it was set.
        {
            auto shoulderAt = [&](double shrink) {
                Entity e = covered(QStringLiteral("SHRUNK%1").arg(shrink), flat, "cable", true);
                e.meta.insert(QStringLiteral("twistBodyShrink"), shrink);
                double face = 0.0, shoulder = 0.0;
                for (const std::vector<Vec2>& o : twistedPairBody(e)) {
                    double least = 1e300, most = -1e300, wide = 0.0;
                    for (const Vec2& v : o) {
                        least = std::min(least, v.x);
                        most = std::max(most, v.x);
                        wide = std::max(wide, std::abs(v.y));
                    }
                    if (most > 150.0 || least > 45.0) continue;   // the start end's boot, only
                    face = least;
                    shoulder = most;
                    for (const Vec2& v : o)
                        if (std::abs(std::abs(v.y) - wide) < 1e-6) shoulder = std::min(shoulder, v.x);
                }
                return std::make_pair(face, shoulder);
            };
            const auto few = shoulderAt(4.0), lots = shoulderAt(12.0);
            std::printf("  transition: a 4 shrink has its face at %.2f and its shoulder at %.2f; a "
                        "12 shrink, %.2f and %.2f\n",
                        few.first, few.second, lots.first, lots.second);
            check(std::abs(few.first - lots.first) < 1e-9,
                  "transition: the face stays at the end of the cable whatever the shrink is set to");
            check(lots.second > few.second + 1.0,
                  "transition: and the shoulder moves toward the body as the shrink is lengthened");
        }

        // A COVER SURVIVES EVERY KNEE THERE IS. Not "nearly every": a cable that turns into a fan of
        // loose wires at some angles and some lengths is worse than one that never covered anything,
        // because it does it while you are drawing. A knee is clicked and then drawn away from, so
        // the leg past it starts at nothing and grows - and a cover that needs a long leg before it
        // appears comes and goes under the cursor. A part that does that is not a part.
        //
        // It used to be thrown away wherever a point of the outline had run too far from the route,
        // which was guarding against an unclipped mitre flying off the sheet. The mitre is clipped
        // where it is made now, so there is nothing left to guard against - and this says so at
        // every angle and every length, which is the only way to say it about a rule like that.
        {
            int tried = 0, lost = 0, worstDeg = 0;
            double worstLeg = 0.0;
            for (int deg = 5; deg <= 175; deg += 5) {
                for (double leg = 5.0; leg <= 120.0; leg += 5.0) {
                    const double t = deg * M_PI / 180.0;
                    const std::vector<Vec2> route{{0.0, 0.0},
                                                  {100.0, 0.0},
                                                  {100.0 + leg * std::cos(t), leg * std::sin(t)}};
                    for (const char* sort : {"cable", "heatshrink"}) {
                        Entity e = covered(QStringLiteral("K%1_%2_%3").arg(deg).arg(leg).arg(sort),
                                           route, sort, true);
                        ++tried;
                        if (!twistedPairBody(e).empty()) continue;
                        ++lost;
                        worstDeg = deg;
                        worstLeg = leg;
                    }
                }
            }
            std::printf("  every knee: %d route(s) tried, %d lost the cover%s\n", tried, lost,
                        lost ? QStringLiteral(" (last %1 deg, leg %2)")
                                   .arg(worstDeg)
                                   .arg(worstLeg)
                                   .toLocal8Bit()
                                   .constData()
                             : "");
            check(lost == 0,
                  "every knee: a covered run keeps its cover at every angle and every leg length");
        }

        // AND A LENGTH OF TUBING COMES TWO WAYS. ALONG the run - one sleeve, recovered at each end,
        // which is what a heatshrink has always been here - or OVER a cable: the cable is drawn and
        // the heatshrink is a piece fitted over each of its ends, the same part a cable takes.
        {
            Entity overCable = covered(QStringLiteral("OVER"), flat, "heatshrink", true);
            overCable.meta.insert(QStringLiteral("twistBodyOverCable"), true);
            const std::vector<std::vector<Vec2>> twoWays = twistedPairBody(overCable);
            double atTheEnd = 0.0, inTheMiddle = 0.0;
            if (!twoWays.empty()) {
                double least = 1e300;
                for (const Vec2& v : twoWays.front()) {
                    inTheMiddle = std::max(inTheMiddle, std::abs(v.y));
                    least = std::min(least, v.x);
                }
                for (const Vec2& v : twoWays.front())
                    if (std::abs(v.x - least) < 1e-6) atTheEnd = std::max(atTheEnd, std::abs(v.y));
            }
            std::printf("  over the cable: %d outline(s), the body %.2f across and %.2f at its "
                        "end\n", int(twoWays.size()), inTheMiddle * 2.0, atTheEnd * 2.0);
            check(twoWays.size() == 3,
                  "over the cable: a cable, and a piece of heatshrink over each end of it");
            check(std::abs(inTheMiddle - atTheEnd) < 1e-6,
                  "over the cable: the cable under it is cut square, being what the pieces grip");
            check(twistedPairBody(covered(QStringLiteral("ALONG"), flat, "heatshrink", true))
                          .size() == 1,
                  "over the cable: unticked, it is still one sleeve along the whole run");
        }

        // AND THE FAN RUNS INTO THE FACE OF THE COVER. The wires under a cover have no bundle of
        // their own to converge to - there are no lines there, the cover is what is drawn - so what
        // they run into is the face of it. Sized to something other than the bundle, the fan
        // followed: the wires stood out past the face of a narrow cable, or stopped short inside a
        // wide one with daylight between the last of the fan and the thing it goes into.
        {
            auto meets = [&](double width) {
                Entity e = covered(QStringLiteral("MEET%1").arg(width), flat, "cable", false);
                e.meta.insert(QStringLiteral("twistBodyWidth"), width);
                // How far out the outermost wire is where it meets the body, and how far out the
                // face of the body is there.
                double wires = 0.0, face = 0.0;
                for (const std::vector<Vec2>& strand : twistedPairStrands(e))
                    for (const Vec2& v : strand)
                        if (std::abs(v.x - 40.0) < 0.6) wires = std::max(wires, std::abs(v.y));
                for (const std::vector<Vec2>& o : twistedPairBody(e))
                    for (const Vec2& v : o) face = std::max(face, std::abs(v.y));
                return std::make_pair(wires, face);
            };
            const auto narrow = meets(14.0), wide = meets(40.0);
            std::printf("  fan entry: a 14 cable takes the wires to %.2f of its %.2f face; a 40 "
                        "cable, %.2f of %.2f\n",
                        narrow.first * 2.0, narrow.second * 2.0, wide.first * 2.0,
                        wide.second * 2.0);
            check(std::abs(narrow.first - narrow.second) < 0.6,
                  "fan entry: the wires meet a narrow cable at its face, not standing out past it");
            check(std::abs(wide.first - wide.second) < 0.6,
                  "fan entry: and a wide one at its face, not stopping short inside it");
        }

        // AND A GREEN ZONE LAID ACROSS THE CABLE HAS TOUCHED THE RUN. A covered run draws no wires
        // at all along its converged stretch - the cover is the only thing there - so a crossing
        // zone over the cable touched nothing that answered for the run, and the only way to catch
        // one was to reach a fan at an end of it.
        for (const char* sort : {"cable", "heatshrink"}) {
            Document d;
            d.entities().push_back(covered(QStringLiteral("RUN"), flat, sort, true));
            Editor ed;
            ed.setDocument(&d);
            noSnap(ed);
            ed.setTool(ToolId::Select);
            // Right to left is a CROSSING zone: over the middle of the cable, nowhere near a fan.
            // Swept as a rectangle is drawn - the opposite corner settles it.
            ed.onPointerPress({170.0, 20.0}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerMove({130.0, -20.0});
            ed.onPointerRelease({130.0, -20.0}, Qt::LeftButton);
            ed.onPointerPress({130.0, -20.0}, Qt::LeftButton, Qt::NoModifier);
            std::printf("  cable pick: %s - %d selected by a zone over the cable alone\n", sort,
                        int(ed.selection().size()));
            check(ed.selection().size() == 1,
                  "cable pick: a zone that touches only the cable is enough to select the run");
        }

        // AND THE STRIPPED ENDS BELONG TO THE ENDS THAT FAN. An end that does not fan has no wire
        // ends drawn at it at all - only the bundle running into whatever it runs into - so a strip
        // mark there would be a step in the edges of the bundle saying the whole loom is bare.
        Entity oneSided = covered(QStringLiteral("ONESIDED"), flat, "", true);
        oneSided.meta.remove(QStringLiteral("twistBody"));
        oneSided.meta.insert(QStringLiteral("twistStrip"), true);
        oneSided.meta.insert(QStringLiteral("twistStripLen"), 8.0);
        oneSided.meta.insert(QStringLiteral("twistSpread"), QStringLiteral("left"));
        int atStart = 0, atFinish = 0;
        for (const std::vector<Vec2>& cap : twistedPairEndCaps(oneSided)) {
            if (cap.size() != 2) continue;
            const double x = (cap.front().x + cap.back().x) / 2.0;
            if (std::abs(x - 8.0) < 1e-6) ++atStart;
            if (std::abs(x - 292.0) < 1e-6) ++atFinish;
        }
        std::printf("  strip and spread: %d mark(s) at the end that fans, %d at the end that does "
                    "not\n", atStart, atFinish);
        check(atStart == 6, "strip and spread: every wire at the end that fans is marked");
        check(atFinish == 0,
              "strip and spread: and the end that does not fan has no wire ends to strip");
    }

    // ---- A loom that cannot go there stops where it can ------------------------------------------
    {
        // The bundle has a width, and a width cannot be bent round a corner tighter than half of it
        // nor sent down a leg shorter than it is. Asked to, the drawing does not fail - it pinches:
        // the bend is clamped to whatever fits and the wires are held off their lanes so none of
        // them turns inside out, and what comes out is a knot where a corner was meant to be. That
        // is the right last resort and the wrong thing to draw on purpose.
        QString why;
        check(runRouteCarries({{0.0, 0.0}, {300.0, 0.0}}, 12.0, 0.0, 0.0, &why),
              "carries: a straight run of three hundred carries a twelve-wide bundle");
        check(runRouteCarries({{0.0, 0.0}, {200.0, 0.0}, {200.0, -200.0}}, 12.0, 0.0, 0.0, &why),
              "carries: and a right angle with two hundred either side of it");
        // A LEG SHORTER THAN THE BUNDLE IS WIDE is not a leg.
        check(!runRouteCarries({{0.0, 0.0}, {200.0, 0.0}, {204.0, 0.0}}, 12.0, 0.0, 0.0, &why),
              "carries: a four-millimetre leg does not carry a twelve-wide bundle");
        std::printf("  carries: the short leg is refused - %s\n", qPrintable(why));
        check(why.contains(QLatin1String("shorter")), "carries: and it says which it was");
        // AND A TURN TOO SHARP FOR THE BUNDLE. Doubling back is the extreme of it.
        check(!runRouteCarries({{0.0, 0.0}, {200.0, 0.0}, {40.0, 6.0}}, 12.0, 0.0, 0.0, &why),
              "carries: and a run doubling back on itself does not either");
        std::printf("  carries: the hairpin is refused - %s\n", qPrintable(why));
        // THE ONE IN test1.dwx, WHICH IS WHERE THIS CAME FROM: out 172, back 17 and down 22, then a
        // millimetre and a half further on. Four wires of the ordinary size make a bundle nine
        // across, and that route cannot carry it.
        const std::vector<Vec2> asDrawn{{-366.0, 146.0},
                                        {-193.89950611156704, 146.0},
                                        {-210.90909998587227, 124.07577689776151},
                                        {-209.6674505782298, 124.69660160158277}};
        check(!runRouteCarries(asDrawn, 9.0, 0.0, 0.0, &why),
              "carries: the route that drew the anomaly is refused, which is where this came from");
        std::printf("  carries: test1's route is refused - %s\n", qPrintable(why));
        // AND THE FAN NEEDS A STRAIGHT TO STAND ON. The wires open out over the first stretch at
        // each end, and a knee inside that stretch is a knee through the middle of a fan-out, where
        // every wire is at its furthest from the route and each is turning on a radius of its own.
        // That is the mess a fan drawn too close to a corner makes, and it looks like a mistake
        // rather than like a tight bend.
        const std::vector<Vec2> kneeSoon{{0.0, 0.0}, {30.0, 0.0}, {200.0, 0.0}, {200.0, -200.0}};
        check(runRouteCarries(kneeSoon, 9.0, 0.0, 0.0, &why),
              "carries: with no fan to fit, a knee thirty along is a knee like any other");
        check(!runRouteCarries(kneeSoon, 9.0, 55.0, 55.0, &why),
              "carries: but a fifty-five millimetre fan does not fit in a thirty millimetre leg");
        std::printf("  carries: the knee inside the fan is refused - %s\n", qPrintable(why));
        check(why.contains(QLatin1String("fan")), "carries: and it says it was the fan");
        // The far end is held to its own fan the same way.
        const std::vector<Vec2> kneeLate{{0.0, 0.0}, {200.0, 0.0}, {200.0, -200.0}, {230.0, -200.0}};
        check(!runRouteCarries(kneeLate, 9.0, 55.0, 55.0, &why),
              "carries: and the fan at the far end wants its own straight just as much");

        // ---- AND THE TOOL HOLDS THE RUN THERE ----
        {
            // The cursor carries on; the run does not follow it until it is somewhere the bundle can
            // be taken, which is the engineer changing direction.
            Document d;
            Editor ed;
            ed.setDocument(&d);
            noSnap(ed);
            ed.setTool(ToolId::WiresRun);
            ed.onPointerPress({0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerMove({200.0, 0.0});
            check(!ed.twistedHeld(), "held: a run going somewhere it can go is not held");
            ed.onPointerPress({200.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
            // Now back on itself, which the bundle cannot do.
            ed.onPointerMove({40.0, 6.0});
            std::printf("  held: doubling back, the run is %s at %.1f,%.1f\n",
                        ed.twistedHeld() ? "held" : "following", ed.twistedHeldAt().x,
                        ed.twistedHeldAt().y);
            check(ed.twistedHeld(), "held: doubling back, the run stops where it was");
            // AND IT TAKES IT UP AGAIN the moment the cursor is somewhere it can follow.
            ed.onPointerMove({200.0, -200.0});
            check(!ed.twistedHeld(), "held: and follows again once the cursor comes back round");
            // A KNEE IS NOT PUT WHERE THE RUN IS NOT: clicking while held means the place on screen.
            ed.onPointerMove({40.0, 6.0});
            const Vec2 heldAt = ed.twistedHeldAt();
            ed.onPointerPress({40.0, 6.0}, Qt::LeftButton, Qt::NoModifier);
            ed.onKey(Qt::Key_Return, Qt::NoModifier);
            const Entity* run = nullptr;
            for (const Entity& e : d.entities())
                if (e.type == EntityType::TwistedPair) run = &e;
            check(run != nullptr, "held: the run was drawn");
            if (run) {
                std::printf("  held: clicked at 40,6 while held at %.1f,%.1f - the run ends at "
                            "%.1f,%.1f\n",
                            heldAt.x, heldAt.y, run->points.back().x, run->points.back().y);
                check(distance(run->points.back(), heldAt) < 1e-6,
                      "held: a click while held puts the knee where the run is, not where the "
                      "cursor wandered to");
                check(runRouteCarries(run->points, 9.0, 0.0, 0.0, nullptr),
                      "held: so what comes out is a route the bundle can be taken round");
            }
        }
    }

    // ---- Nought is a sharp corner on the run's knee as well --------------------------------------
    {
        // Nought used to mean "nobody has said" on both radii, which left no way at all to ask for a
        // corner: every run was bent whether the drawing wanted it bent or not. Saying nothing is now
        // saying nothing - and every run drawn before this said nothing, so none of them moves.
        auto bentRun = [](double knee, bool say) {
            Entity w;
            w.id = QStringLiteral("SHARP");
            w.type = EntityType::TwistedPair;
            w.points = {{0.0, 0.0}, {200.0, 0.0}, {200.0, -200.0}};
            w.a = w.points.front();
            w.b = w.points.back();
            w.meta.insert(QStringLiteral("twistStraight"), true);
            w.meta.insert(QStringLiteral("twistWires"), 4);
            w.meta.insert(QStringLiteral("twistWidth"), 10.0);
            w.meta.insert(QStringLiteral("twistPitch"), 8.0);
            if (say) w.meta.insert(QStringLiteral("twistKnee"), knee);
            return w;
        };
        // How sharply the bundle turns at the knee: a corner turns the whole ninety degrees between
        // one step and the next, a bend spreads it over the ten steps an arc is drawn with.
        auto sharpestTurn = [](const Entity& e) {
            double sharpest = 0.0;
            for (const std::vector<Vec2>& s : twistedPairStrands(e))
                for (size_t i = 2; i < s.size(); ++i) {
                    if (distance(s[i - 1], Vec2{200.0, 0.0}) > 120.0) continue;
                    const Vec2 d1 = s[i - 1] - s[i - 2], d2 = s[i] - s[i - 1];
                    if (d1.length() < 1e-9 || d2.length() < 1e-9) continue;
                    sharpest = std::max(sharpest, std::abs(angleOf(d2) - angleOf(d1)));
                }
            return sharpest * 180.0 / M_PI;
        };
        const double said0 = sharpestTurn(bentRun(0.0, true));
        const double said30 = sharpestTurn(bentRun(30.0, true));
        const double saidNothing = sharpestTurn(bentRun(0.0, false));
        std::printf("  sharp: the knee turns %.1f deg at 0, %.1f deg at 30, %.1f deg unsaid\n", said0,
                    said30, saidNothing);
        check(said0 > 80.0, "sharp: a knee radius of nought is a corner, which is a thing a drawing "
                            "can want and could not be asked for");
        check(said30 < 30.0, "sharp: and a radius is still a bend");
        check(saidNothing < 30.0,
              "sharp: saying nothing still leaves the drawing to decide, so nothing already drawn "
              "turns into a corner");
    }

    // ---- The finish end of a run need not fan like the start end ---------------------------------
    {
        // A run leaves one thing and arrives at another: eight ways out of a header at one end and
        // two into a ring terminal at the other is one run, not two runs that meet. Nearly every run
        // IS drawn alike at both ends, so that is what it does unless told otherwise - and that is
        // what every run already on a sheet does.
        auto sided = [](bool same, double finishFan, double finishPitch) {
            Entity w;
            w.id = QStringLiteral("SIDES");
            w.type = EntityType::TwistedPair;
            w.points = {{0.0, 0.0}, {400.0, 0.0}};
            w.a = w.points.front();
            w.b = w.points.back();
            w.meta.insert(QStringLiteral("twistStraight"), true);
            w.meta.insert(QStringLiteral("twistWires"), 6);
            w.meta.insert(QStringLiteral("twistWidth"), 12.0);
            w.meta.insert(QStringLiteral("twistPitch"), 8.0);
            w.meta.insert(QStringLiteral("twistEnds"), 40.0);
            w.meta.insert(QStringLiteral("twistLane"), 10.0);
            if (!same) {
                w.meta.insert(QStringLiteral("twistSidesSame"), false);
                w.meta.insert(QStringLiteral("twistEndsFinish"), finishFan);
                w.meta.insert(QStringLiteral("twistLaneFinish"), finishPitch);
            }
            return w;
        };
        // How far apart the wires are where they land, at each end, and how far in from that end the
        // fan reaches - the two things the finish end is now allowed its own answer to.
        auto spreadAt = [](const Entity& e, bool atStart) {
            double lo = 1e300, hi = -1e300;
            for (const std::vector<Vec2>& s : twistedPairStrands(e)) {
                if (s.empty()) continue;
                const Vec2 p = atStart ? s.front() : s.back();
                lo = std::min(lo, p.y);
                hi = std::max(hi, p.y);
            }
            return hi - lo;
        };
        auto fanReach = [](const Entity& e, bool atStart) {
            // The outermost wire is level where it is landed and level again in the bundle; how far
            // from the end it stops moving is where its fan ends.
            const std::vector<std::vector<Vec2>> s = twistedPairStrands(e);
            if (s.empty()) return 0.0;
            const std::vector<Vec2>& a = s.front();
            const double endY = atStart ? a.front().y : a.back().y;
            double reach = 0.0;
            for (const Vec2& p : a) {
                // Its OWN half of the run: the other end is off level too, and it is not this fan.
                const double from = atStart ? p.x : 400.0 - p.x;
                if (from > 200.0) continue;
                if (std::abs(p.y - endY) > 1e-6) reach = std::max(reach, from);
            }
            return reach;
        };

        const Entity alike = sided(true, 0.0, 0.0);
        std::printf("  sides: alike - spreads %.2f and %.2f, fans reach %.1f and %.1f\n",
                    spreadAt(alike, true), spreadAt(alike, false), fanReach(alike, true),
                    fanReach(alike, false));
        check(std::abs(spreadAt(alike, true) - spreadAt(alike, false)) < 1e-6 &&
                  std::abs(fanReach(alike, true) - fanReach(alike, false)) < 1e-6,
              "sides: said once, the finish end fans exactly as the start end does");

        const Entity apart = sided(false, 90.0, 24.0);
        const double startSpread = spreadAt(apart, true), finishSpread = spreadAt(apart, false);
        const double startReach = fanReach(apart, true), finishReach = fanReach(apart, false);
        std::printf("  sides: apart - spreads %.2f and %.2f, fans reach %.1f and %.1f\n",
                    startSpread, finishSpread, startReach, finishReach);
        check(std::abs(startSpread - spreadAt(alike, true)) < 1e-6,
              "sides: parting them leaves the start end exactly where it was");
        check(finishSpread > startSpread + 1.0,
              "sides: and the finish end spreads to its own pitch");
        check(finishReach > startReach + 10.0,
              "sides: over its own fan length, which is the other half of a run having two ends");
    }

    // ---- A wire keeps its width where it slants through the fan -----------------------------------
    {
        // Both edges of a wire are offset from its centre along the normal to the RUN. In the fan the
        // wire is crossing the run to reach its lane, and two lines held a fixed distance apart
        // measured ACROSS the run are closer than that measured across the wire itself - by the
        // cosine of the slant. So the diagonal part of every fan came out thinner than the straight
        // parts at either end of it, and widening the pitch made it worse, because a wider pitch is a
        // steeper slant.
        auto run = [](double pitch) {
            Entity w;
            w.id = QStringLiteral("RUN");
            w.type = EntityType::TwistedPair;
            w.points = {{0.0, 0.0}, {300.0, 0.0}};
            w.a = w.points.front();
            w.b = w.points.back();
            w.meta.insert(QStringLiteral("twistStraight"), true);
            w.meta.insert(QStringLiteral("twistWires"), 6);
            w.meta.insert(QStringLiteral("twistWidth"), 12.0);
            w.meta.insert(QStringLiteral("twistPitch"), 8.0);
            w.meta.insert(QStringLiteral("twistWireWidth"), 2.0);
            w.meta.insert(QStringLiteral("twistLane"), pitch);
            return w;
        };
        // How wide one wire is drawn at a given x, measured between its two edges ACROSS THE WIRE -
        // the shortest distance from a point on one edge to the other, which is what the eye sees.
        auto widthAt = [](const Entity& e, size_t wire, double x) {
            const std::vector<std::vector<Vec2>> s = twistedPairStrands(e);
            if (2 * wire + 1 >= s.size()) return 0.0;
            const std::vector<Vec2>& a = s[2 * wire];
            const std::vector<Vec2>& b = s[2 * wire + 1];
            // The point on the first edge AT x, interpolated along the step that spans it. Taking
            // the nearest measured place instead answers about somewhere else entirely: the middle
            // of a run is one long step with nothing measured inside it, so "nearest to the middle"
            // is the end of the fan, a hundred millimetres away and on a curve.
            Vec2 on{0.0, 0.0};
            double bestDx = 1e300;
            for (size_t i = 1; i < a.size(); ++i) {
                const Vec2 p = a[i - 1], q = a[i];
                if ((p.x - x) * (q.x - x) > 0.0) continue;
                const double span = q.x - p.x;
                on = std::abs(span) < 1e-12 ? p : lerp(p, q, (x - p.x) / span);
                bestDx = 0.0;
                break;
            }
            if (bestDx > 1e299)
                for (const Vec2& p : a)
                    if (std::abs(p.x - x) < bestDx) { bestDx = std::abs(p.x - x); on = p; }
            double best = 1e300;
            for (size_t i = 1; i < b.size(); ++i)
                best = std::min(best, closestPointOnSegment(on, b[i - 1], b[i]).distance);
            return best;
        };

        const Entity wide = run(24.0);
        // The outermost wire slants hardest, so it is the one that showed the fault. In the middle of
        // the run it is straight; a quarter of the way in it is crossing the fan.
        const double straightPart = widthAt(wide, 0, 150.0);
        const double slantPart = widthAt(wide, 0, 20.0);
        std::printf("  wire width: straight %.3f, through the slant %.3f\n", straightPart, slantPart);
        check(straightPart > 1e-6, "wire width: the wire is drawn with a width at all");
        check(std::abs(slantPart - straightPart) < 0.15 * straightPart,
              "wire width: it is the same width through the slant as along the straight");

        // AND A WIDER PITCH DOES NOT THIN IT. A wider pitch is a steeper fan, which is exactly when
        // the old drawing pinched hardest.
        const double tightSlant = widthAt(run(8.0), 0, 20.0);
        std::printf("  wire width: pitch 8 %.3f, pitch 24 %.3f - the wire itself is 2.00\n",
                    tightSlant, slantPart);
        // THE WIRE IS THE WIDTH IT IS, whatever the fan is set to. Opening the pitch used to pinch
        // it: the wires had the same short stretch to travel further in, so they crossed the run at
        // seventy degrees and the widening was capped rather than drawn.
        check(std::abs(tightSlant - 2.0) < 0.2,
              "wire width: a tight fan draws the wire at the width it was given");
        check(std::abs(slantPart - 2.0) < 0.2,
              "wire width: and so does a wide one - opening the pitch does not thin them");
    }

    // ---- A fan too short for its pitch loses width, it does not grow spikes -----------------------
    {
        // Asking for a 5 mm fan and a wide pitch is asking the wires to cross the run almost square.
        // The width correction is one over the cosine of that slant, which runs away to infinity as
        // the wire approaches square - so uncapped it threw the edges out in long spikes past the
        // knee, far outside the bundle they belong to.
        auto run = [](double fan, double pitch) {
            Entity w;
            w.id = QStringLiteral("RUN");
            w.type = EntityType::TwistedPair;
            w.points = {{0.0, 0.0}, {300.0, 0.0}};
            w.a = w.points.front();
            w.b = w.points.back();
            w.meta.insert(QStringLiteral("twistStraight"), true);
            w.meta.insert(QStringLiteral("twistWires"), 8);
            w.meta.insert(QStringLiteral("twistWidth"), 12.0);
            w.meta.insert(QStringLiteral("twistPitch"), 8.0);
            w.meta.insert(QStringLiteral("twistWireWidth"), 2.0);
            w.meta.insert(QStringLiteral("twistEnds"), fan);     // an explicit, punishing fan length
            w.meta.insert(QStringLiteral("twistLane"), pitch);
            return w;
        };
        // How far the drawing reaches across, against how far the wires are meant to spread. A spike
        // shows up as a line a long way outside the fan it belongs to.
        auto reach = [](const Entity& e) {
            double most = 0.0;
            for (const std::vector<Vec2>& s : twistedPairStrands(e))
                for (const Vec2& p : s) most = std::max(most, std::abs(p.y));
            return most;
        };
        const double pitch = 20.0;
        const double wanted = pitch * (8 - 1) / 2.0 + 1.0;   // the outermost lane, plus half a wire
        const double cruel = reach(run(5.0, pitch));
        const double easy = reach(run(80.0, pitch));
        std::printf("  fan spike: wires spread to %.1f; a 5 mm fan reaches %.1f, an 80 mm fan %.1f\n",
                    wanted, cruel, easy);
        check(easy < wanted + 1.0, "fan spike: a fan with room reaches exactly as far as its wires do");
        // The punishing one may not reach appreciably further. Before the cap it ran to several times
        // the spread.
        check(cruel < wanted + 2.0,
              "fan spike: and a fan with no room stays inside the spread rather than spiking past it");
    }

    // ---- A click on a harness picks the PART, not the one line under the cursor ------------------
    {
        // A generated harness is not one thing and it is not fifty things. It is a handful of parts,
        // each drawn as several objects: a run is two rails, its conductors and the figure that
        // dimensions it. Clicking one rail used to select that rail and leave the rest of the run
        // behind, which is not what anybody means by clicking a run.
        Document d;
        auto add = [&d](const char* id, double y, const char* node, const char* seg) {
            Entity e;
            e.id = QString::fromLatin1(id);
            e.type = EntityType::Line;
            e.a = {0.0, y};
            e.b = {100.0, y};
            if (*node) e.meta.insert(QStringLiteral("mechNode"), QString::fromLatin1(node));
            if (*seg) e.meta.insert(QStringLiteral("mechSeg"), QString::fromLatin1(seg));
            d.entities().push_back(e);
        };
        add("rail1", 0.0, "", "s1");        // the run: two rails ...
        add("rail2", 2.0, "", "s1");
        add("wire1", 1.0, "N1", "s1");      // ... and conductors that also name where they start
        add("dimS1", 4.0, "", "s1");        // ... and the figure that dimensions it
        add("body", 20.0, "N1", "");        // the connector at that end
        add("dimN1", 22.0, "", "N1");       // and ITS dimension, which the builder files under seg
        add("loose", 40.0, "", "");         // something that is not part of a harness at all

        auto joined = [](const std::vector<QString>& v) {
            QStringList s;
            for (const QString& x : v) s << x;
            return s.join(QStringLiteral(", "));
        };
        auto has = [](const std::vector<QString>& v, const char* id) {
            return std::find(v.begin(), v.end(), QString::fromLatin1(id)) != v.end();
        };
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        auto clickAt = [&ed](double y) {
            ed.clearSelection();
            ed.onPointerPress({50.0, y}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerRelease({50.0, y}, Qt::LeftButton);
            // selection() hands back a std::vector; sorted, so the check reads the same whatever
            // order the picking happened to add them in.
            std::vector<QString> got = ed.selection();
            std::sort(got.begin(), got.end());
            return got;
        };

        // THE WHOLE RUN, from a click on one rail of it.
        const std::vector<QString> run = clickAt(0.0);
        std::printf("  harness part: clicking a rail selected %s\n",
                    qPrintable(joined(run)));
        check(run.size() == 4, "harness part: a click on a rail takes the whole run");
        check(has(run, "rail2") && has(run, "wire1") && has(run, "dimS1"),
              "harness part: its other rail, its conductors and its dimension come with it");

        // THE CONDUCTORS BELONG TO THE RUN and not to the connector they start at, even though they
        // name both. Dragging a run has to take its wires; leaving them with the connector would
        // strip the run bare.
        const std::vector<QString> fromWire = clickAt(1.0);
        check(fromWire == run, "harness part: clicking a conductor selects the same run");

        // THE CONNECTOR IS ITS OWN PART - and its dimension is part of it, though the builder files
        // that under the segment field. Keeping them apart would leave the figure behind whenever
        // the connector was moved.
        const std::vector<QString> conn = clickAt(20.0);
        std::printf("  harness part: clicking the connector selected %s\n",
                    qPrintable(joined(conn)));
        check(conn.size() == 2 && has(conn, "dimN1"),
              "harness part: a connector takes its own dimension and nothing of the run");

        // SOMETHING THAT IS NOT A HARNESS IS LEFT ALONE.
        const std::vector<QString> loose = clickAt(40.0);
        check(loose.size() == 1 && loose.front() == QStringLiteral("loose"),
              "harness part: an ordinary object still selects on its own");

        // AND A DELIBERATE SELECTION OF MANY IS NOT WIDENED. Select-all, a green zone, the set a
        // paste just put down - those are choices somebody made, and quietly adding to them would be
        // a different command from the one they gave.
        ed.selectAll();
        const int all = int(ed.selection().size());
        std::printf("  harness part: select-all holds %d of %d\n", all, int(d.entities().size()));
        check(all == static_cast<int>(d.entities().size()),
              "harness part: select-all selects everything, and is not grown further");
    }

    // ---- Space during a DRAG turns it under the hand, once, and leaves the ghost be ---------------
    {
        // The other way of moving something: take hold of its body and drag it, rather than the Move
        // tool. There the object itself follows the cursor, so the object is what turns - but it has
        // to turn about the CURSOR, or it swings away from the hand that is carrying it.
        Document d;
        Entity line;
        line.id = QStringLiteral("l");
        line.type = EntityType::Line;
        line.a = {0.0, 0.0};
        line.b = {10.0, 0.0};
        d.entities().push_back(line);
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.selectAll();
        ed.setTool(ToolId::Select);
        auto lineNow = [&d] {
            for (const Entity& e : d.entities())
                if (e.id == QStringLiteral("l")) return e;
            return Entity();
        };
        // Grabbed at 2.5 along it - clear of the grips at 0, 5 and 10, so this is the body and not a
        // joint - and carried out to (50,50), which puts it from (47.5,50) to (57.5,50).
        ed.onPointerPress({2.5, 0.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({50.0, 50.0});
        check(ed.movingByDistance(), "drag turn: a drag counts as a move under way");
        const Entity carried = lineNow();
        std::printf("  drag turn: carried to (%.1f,%.1f)..(%.1f,%.1f)\n", carried.a.x, carried.a.y,
                    carried.b.x, carried.b.y);
        check(ed.onKey(Qt::Key_Space, Qt::NoModifier), "drag turn: Space is taken");
        const Entity turned = lineNow();
        std::printf("  drag turn: after Space it is (%.1f,%.1f)..(%.1f,%.1f)\n", turned.a.x,
                    turned.a.y, turned.b.x, turned.b.y);
        // ABOUT THE CURSOR, so whatever is under the hand stays under it. Worked out from where the
        // drag actually left the line rather than hard-coded: a press near the middle of a line takes
        // hold of its midpoint joint, which re-centres it on the cursor, and that is not what is being
        // tested here. Turning a point clockwise about (50,50) sends (dx,dy) to (dy,-dx).
        auto turnedAboutCursor = [](Vec2 p) {
            const Vec2 d{p.x - 50.0, p.y - 50.0};
            return Vec2{50.0 + d.y, 50.0 - d.x};
        };
        const Vec2 wantA = turnedAboutCursor(carried.a);
        const Vec2 wantB = turnedAboutCursor(carried.b);
        std::printf("  drag turn: about the cursor that is (%.1f,%.1f)..(%.1f,%.1f)\n", wantA.x,
                    wantA.y, wantB.x, wantB.y);
        check(std::abs(turned.a.x - wantA.x) < 1e-6 && std::abs(turned.a.y - wantA.y) < 1e-6 &&
                  std::abs(turned.b.x - wantB.x) < 1e-6 && std::abs(turned.b.y - wantB.y) < 1e-6,
              "drag turn: it turns about the cursor, so it stays under the hand");
        // AND NOT ABOUT WHERE THE DRAG STARTED, which is what it used to do: from (2.5,0) that pivot
        // is nowhere near the hand, and the line was flung off across the sheet.
        check(std::abs(turned.a.x - 2.5) > 1.0 || std::abs(turned.a.y) > 1.0,
              "drag turn: and not about the far-off point the drag started from");
        // THE GHOST IS WHERE IT CAME FROM, and that does not change because it was turned on the way.
        bool ghostMoved = false;
        for (const Entity& g : ed.dragGhost())
            if (std::abs(g.a.x) > 1e-6 || std::abs(g.a.y) > 1e-6 || std::abs(g.b.x - 10.0) > 1e-6 ||
                std::abs(g.b.y) > 1e-6)
                ghostMoved = true;
        std::printf("  drag turn: the source ghost holds %d object(s), moved=%d\n",
                    int(ed.dragGhost().size()), ghostMoved ? 1 : 0);
        check(!ed.dragGhost().empty() && !ghostMoved,
              "drag turn: the mark showing where it came from stays put");
        // AND THE WHOLE DRAG IS ONE THING TO UNDO, however many times Space was pressed. It used to
        // push a step per press, so a drag with four turns in it took five undos to get back.
        ed.onKey(Qt::Key_Space, Qt::NoModifier);
        ed.onKey(Qt::Key_Space, Qt::NoModifier);
        ed.onPointerRelease({50.0, 50.0}, Qt::LeftButton);
        ed.undo();
        const Entity back = lineNow();
        std::printf("  drag turn: one undo puts it back to (%.1f,%.1f)..(%.1f,%.1f)\n", back.a.x,
                    back.a.y, back.b.x, back.b.y);
        check(std::abs(back.a.x) < 1e-6 && std::abs(back.a.y) < 1e-6 &&
                  std::abs(back.b.x - 10.0) < 1e-6 && std::abs(back.b.y) < 1e-6,
              "drag turn: one undo takes back the move and every turn in it");
    }

    // ---- DRAW ORDER: which of two things in the same place is in front ---------------------------
    {
        // Three lines stacked in the same place, and a fourth well away from all of them. The one away
        // is the whole point of the relative commands: it has no quarrel with the others, so nothing
        // should ever be moved past it. TOUCHING means the extents overlap - two parallel lines a
        // millimetre apart do NOT touch, which is why the above/under fixtures below cross instead.
        auto sheet = [](Document& d) {
            d.entities().clear();
            auto line = [&](const char* id, double y) {
                Entity e;
                e.id = QString::fromLatin1(id);
                e.type = EntityType::Line;
                e.a = {0.0, y};
                e.b = {100.0, y};
                d.entities().push_back(e);
            };
            line("A", 0.0);
            line("B", 1.0);
            line("C", 2.0);
            Entity far;
            far.id = QStringLiteral("FAR");
            far.type = EntityType::Line;
            far.a = {900.0, 900.0};
            far.b = {1000.0, 900.0};
            d.entities().push_back(far);
        };
        auto order = [](Document& d) {
            QStringList out;
            for (const Entity& e : d.entities()) out << e.id;
            return out.join(QLatin1Char(','));
        };

        // ---- FRONT and BACK are absolute. ----
        {
            Document d;
            sheet(d);
            Editor ed;
            ed.setDocument(&d);
            noSnap(ed);
            ed.selectById(QStringLiteral("A"));
            ed.setSelectionDrawOrder(Editor::DrawOrder::Front);
            std::printf("  draw order: front -> %s\n", qPrintable(order(d)));
            check(order(d) == QLatin1String("B,C,FAR,A"), "draw order: to the front is last of all");

            ed.clearSelection();
            ed.selectById(QStringLiteral("FAR"));
            ed.setSelectionDrawOrder(Editor::DrawOrder::Back);
            std::printf("  draw order: back  -> %s\n", qPrintable(order(d)));
            check(order(d) == QLatin1String("FAR,B,C,A"), "draw order: to the back is first of all");
        }

        // ---- A GROUP KEEPS ITS OWN ORDER when it moves as one. ----
        {
            Document d;
            sheet(d);
            Editor ed;
            ed.setDocument(&d);
            noSnap(ed);
            ed.selectById(QStringLiteral("A"));
            ed.selectById(QStringLiteral("C"), /*additive=*/true);
            check(ed.selection().size() == 2, "draw order: both lines are picked");
            ed.setSelectionDrawOrder(Editor::DrawOrder::Front);
            std::printf("  draw order: two to the front -> %s\n", qPrintable(order(d)));
            check(order(d) == QLatin1String("B,FAR,A,C"),
                  "draw order: A was under C and is still under C");
        }

        // ---- ABOVE: past what it TOUCHES, and no further. ----
        {
            // A is at the bottom, B lies across it, FAR is on top of the pile and touches neither.
            Document d;
            d.entities().clear();
            auto add = [&](const char* id, Vec2 a, Vec2 b) {
                Entity e;
                e.id = QString::fromLatin1(id);
                e.type = EntityType::Line;
                e.a = a;
                e.b = b;
                d.entities().push_back(e);
            };
            add("A", {0.0, 0.0}, {100.0, 0.0});
            add("B", {50.0, -10.0}, {50.0, 10.0});   // crosses A
            add("FAR", {900.0, 900.0}, {1000.0, 900.0});
            Editor ed;
            ed.setDocument(&d);
            noSnap(ed);
            ed.selectById(QStringLiteral("A"));
            ed.setSelectionDrawOrder(Editor::DrawOrder::AboveTouching);
            std::printf("  draw order: above -> %s\n", qPrintable(order(d)));
            // The tell: FAR is still LAST. Bringing it to the front instead would read "B,FAR,A".
            check(order(d) == QLatin1String("B,A,FAR"),
                  "draw order: above clears what it overlaps and stops there");
        }

        // ---- UNDER: below what it TOUCHES, and no further. ----
        {
            Document d;
            d.entities().clear();
            auto add = [&](const char* id, Vec2 a, Vec2 b) {
                Entity e;
                e.id = QString::fromLatin1(id);
                e.type = EntityType::Line;
                e.a = a;
                e.b = b;
                d.entities().push_back(e);
            };
            add("FAR", {900.0, 900.0}, {1000.0, 900.0});
            add("B", {50.0, -10.0}, {50.0, 10.0});   // crosses A
            add("A", {0.0, 0.0}, {100.0, 0.0});
            Editor ed;
            ed.setDocument(&d);
            noSnap(ed);
            ed.selectById(QStringLiteral("A"));
            ed.setSelectionDrawOrder(Editor::DrawOrder::UnderTouching);
            std::printf("  draw order: under -> %s\n", qPrintable(order(d)));
            // The tell: FAR is still FIRST. Sending it to the back would read "A,FAR,B".
            check(order(d) == QLatin1String("FAR,A,B"),
                  "draw order: under drops below what it overlaps and stops there");
        }

        // ---- IT IS AN EDIT, so it can be taken back. ----
        {
            Document d;
            sheet(d);
            Editor ed;
            ed.setDocument(&d);
            noSnap(ed);
            const QString was = order(d);
            ed.selectById(QStringLiteral("A"));
            ed.setSelectionDrawOrder(Editor::DrawOrder::Front);
            check(order(d) != was, "draw order: the order did change");
            ed.undo();
            std::printf("  draw order: undone -> %s (was %s)\n", qPrintable(order(d)),
                        qPrintable(was));
            check(order(d) == was, "draw order: and undo puts the order back");
        }

        // ---- NOTHING SELECTED, nothing happens. ----
        {
            Document d;
            sheet(d);
            Editor ed;
            ed.setDocument(&d);
            noSnap(ed);
            const QString was = order(d);
            ed.clearSelection();
            ed.setSelectionDrawOrder(Editor::DrawOrder::Front);
            check(order(d) == was, "draw order: with nothing picked the sheet is left alone");
        }
    }

    // ---- A BLOCK ARROW CAN BE PICKED --------------------------------------------------------------
    {
        // It keeps its two ends and lays its outline out when it is drawn, and everything that asks
        // where an object IS was reading the stored point list - which is empty - so an arrow could be
        // drawn and then never selected, windowed, zoomed to or snapped to again.
        Document d;
        d.entities().clear();
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.setTool(ToolId::VolumeArrow);
        ed.onPointerPress({0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);      // tail
        ed.onPointerPress({100.0, 0.0}, Qt::LeftButton, Qt::NoModifier);    // tip
        check(d.entities().size() == 1, "arrow: one object is drawn");
        if (d.entities().size() == 1) {
            const Entity& arrow = d.entities().front();
            check(arrow.meta.value("volumeArrow").toBool(), "arrow: and it is a block arrow");

            // THE BOX IT OCCUPIES. Without this it had none, so a window drag could not catch it.
            const Bounds b = arrow.bounds();
            std::printf("  arrow: box x %.2f..%.2f y %.2f..%.2f\n", b.min.x, b.max.x, b.min.y,
                        b.max.y);
            check(b.valid, "arrow: it has a box on the sheet");
            check(b.valid && std::abs(b.min.x) < 1e-6 && std::abs(b.max.x - 100.0) < 1e-6,
                  "arrow: the box runs tail to tip");
            // A 100 long arrow is 18 wide by default, so the shaft is 9 either side - and the barbs are
            // twice that, 18 either side. That is the tell that the box came from the OUTLINE and not
            // from the two ends, which would be a box of no height at all.
            check(b.valid && b.max.y > 17.0 && b.max.y < 19.0,
                  "arrow: and out to the barbs across it");

            // A CLICK ON IT PICKS IT - on the shaft, and inside the head, because it is a solid.
            ed.setTool(ToolId::Select);
            ed.clearSelection();
            ed.onPointerPress({50.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
            check(ed.selection().size() == 1, "arrow: clicking the shaft picks it");
            ed.clearSelection();
            ed.onPointerPress({50.0, 8.0}, Qt::LeftButton, Qt::NoModifier);
            check(ed.selection().size() == 1, "arrow: so does clicking inside it");
            ed.clearSelection();
            // AND NOT PICKED from well outside it, or the test above would pass on anything.
            ed.onPointerPress({50.0, 60.0}, Qt::LeftButton, Qt::NoModifier);
            check(ed.selection().empty(), "arrow: a click off the arrow picks nothing");
        }
    }

    // ---- the head of a block arrow is a size that can be set --------------------------------------
    {
        // Both numbers were on the object and neither could be reached, so every arrow was a third of
        // its own length in the head and a fifth across - and a row of arrows of different lengths
        // could not be made to match each other.
        const std::vector<Vec2> plain = volumeArrowOutline({0.0, 0.0}, {100.0, 0.0});
        const std::vector<Vec2> stub = volumeArrowOutline({0.0, 0.0}, {100.0, 0.0}, 0.0, 10.0);
        auto neckX = [](const std::vector<Vec2>& o) {
            // The outline runs tail, neck, barb, tip, barb, neck, tail - so the neck is the second
            // point, and where it sits along the arrow is the length of the head.
            return o.size() >= 2 ? o[1].x : -1.0;
        };
        std::printf("  arrow head: neck at %.2f by default, %.2f when set to 10\n", neckX(plain),
                    neckX(stub));
        check(std::abs(neckX(plain) - 64.0) < 1e-6, "arrow head: a third of the length by default");
        check(std::abs(neckX(stub) - 90.0) < 1e-6, "arrow head: and the set size where it is set");
        // NEVER LONGER THAN THE ARROW, however big a number is typed.
        const std::vector<Vec2> huge = volumeArrowOutline({0.0, 0.0}, {100.0, 0.0}, 0.0, 500.0);
        check(std::abs(neckX(huge)) < 1e-6, "arrow head: a head bigger than the arrow is all head");
    }

    // ---- AN AXIS ANGLE IS ITS ARC ------------------------------------------------------------------
    {
        // Drawn as centre lines and a swept arc, so that is what it occupies and what a click has to
        // land on. It was stored as a centre and a point and answered from the line between them,
        // which is a line the drawing does not have.
        Document d;
        d.entities().clear();
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.setTool(ToolId::AxisAngle);
        ed.onPointerPress({0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);      // the axis
        ed.onPointerPress({0.0, -50.0}, Qt::LeftButton, Qt::NoModifier);    // straight down: the start
        check(d.entities().size() == 1, "axis: one object is drawn");
        if (d.entities().size() == 1) {
            const Entity& ax = d.entities().front();
            check(ax.meta.value("axisAngle").toBool(), "axis: and it is an axis angle");
            check(std::abs(ax.meta.value("axisSweep").toDouble() - 180.0) < 1e-9,
                  "axis: half a turn to begin with");
            // The START is marked 0 and the far end is that plus the sweep, worked out when it is
            // drawn - so there is no second number to be left saying 180 on a 90 degree arc.
            check(std::abs(ax.meta.value("axisFrom").toDouble()) < 1e-9,
                  "axis: with its zero marked 0");
            check(!ax.meta.contains(QStringLiteral("axisTo")),
                  "axis: and no end number of its own to go stale");

            // THE ROOM IT TAKES is the reach of the centre lines, not the 50 between the two points.
            const Bounds b = ax.bounds();
            std::printf("  axis: box x %.2f..%.2f y %.2f..%.2f\n", b.min.x, b.max.x, b.min.y, b.max.y);
            // The box is whichever reaches further, the arms or the arc - the two are separate lengths
            // now, and a fresh one has short arms (10) around a 50 arc.
            check(b.valid && b.max.x >= 49.99, "axis: the box reaches out to the arc");
            check(b.valid && b.min.y <= -49.99 && b.max.y >= 49.99,
                  "axis: and across the whole of it, either side of the axis");

            // A CLICK ON THE ARC PICKS IT. A half turn starting straight down bulges to the +x side,
            // so (50, 0) is on the arc.
            ed.setTool(ToolId::Select);
            ed.clearSelection();
            ed.onPointerPress({50.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
            check(ed.selection().size() == 1, "axis: clicking the arc picks it");
            // AND NOT from inside the ring, where nothing is drawn but the centre lines - well off to
            // one side of them, so this is a genuine miss rather than a click on an axis.
            ed.clearSelection();
            ed.onPointerPress({25.0, 25.0}, Qt::LeftButton, Qt::NoModifier);
            check(ed.selection().empty(), "axis: a click in the empty middle picks nothing");
            // The part of the ring it does NOT sweep is not the object either. Taken at 135 degrees,
            // which is off both centre lines - (-50, 0) would have been ON the horizontal one, so
            // picking there is a hit on an axis and says nothing about the arc.
            ed.clearSelection();
            ed.onPointerPress({-35.36, 35.36}, Qt::LeftButton, Qt::NoModifier);
            check(ed.selection().empty(), "axis: nor is the part of the ring it does not sweep");
        }
    }

    // ---- the radius of an axis angle keeps one answer ---------------------------------------------
    {
        Document d;
        d.entities().clear();
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.setTool(ToolId::AxisAngle);
        ed.onPointerPress({0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({0.0, -50.0}, Qt::LeftButton, Qt::NoModifier);
        if (d.entities().size() == 1) {
            ed.setTool(ToolId::Select);
            ed.selectById(d.entities().front().id);
            // Typing a radius MOVES THE POINT rather than being remembered beside it, so the object
            // has one answer for where the arc is however it was set.
            ed.setGeometryValue(QStringLiteral("Radius"), 80.0);
            const Entity& ax = d.entities().front();
            std::printf("  axis: radius set to 80 -> point at (%.2f, %.2f)\n", ax.b.x, ax.b.y);
            check(std::abs(distance(ax.a, ax.b) - 80.0) < 1e-6, "axis: the radius is what was typed");
            check(std::abs(ax.b.x) < 1e-6 && ax.b.y < 0.0,
                  "axis: and the point slid out the way it already faced");
            check(!ax.meta.contains(QStringLiteral("axisRadius")),
                  "axis: with no second answer kept beside it");
        }
    }

    // ---- A GREEN ZONE TOUCHING A LINE SELECTS THE OBJECT ------------------------------------------
    {
        // Crossing selection asks an object for its drawn segments. A block arrow answered with an
        // empty list, because its outline is worked out rather than stored, and an axis angle answered
        // with the line from its centre out to the clicked point - a line the drawing does not have.
        // So a zone dragged over the arrow caught nothing, and one dragged over the arc caught nothing,
        // while one dragged through the empty middle of the arc caught the object by an invisible line.
        //
        // A crossing zone is dragged RIGHT TO LEFT (see finishRegionSelect), so each of these presses
        // the right-hand corner first.
        // A two-click rectangle: press and release on the first corner, then move to the opposite one
        // and press. Right corner first, because right-to-left is what makes it a crossing zone.
        auto crossingPick = [](Editor& ed, Vec2 rightCorner, Vec2 leftCorner) {
            ed.setTool(ToolId::Select);
            ed.clearSelection();
            ed.onPointerPress(rightCorner, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerRelease(rightCorner, Qt::LeftButton);
            ed.onPointerMove(leftCorner);
            ed.onPointerPress(leftCorner, Qt::LeftButton, Qt::NoModifier);
            return ed.selection().size();
        };

        {
            Document d;
            d.entities().clear();
            Editor ed;
            ed.setDocument(&d);
            noSnap(ed);
            ed.setTool(ToolId::VolumeArrow);
            ed.onPointerPress({0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerPress({100.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
            check(d.entities().size() == 1, "green arrow: an arrow is on the sheet");
            // A tall thin zone across the middle of the arrow: it cuts both edges of the shaft, so it
            // has touched the arrow itself and not merely its bounding box.
            const size_t got = crossingPick(ed, {60.0, 40.0}, {40.0, -40.0});
            std::printf("  green arrow: a zone across the shaft picked %d object(s)\n",
                        static_cast<int>(got));
            check(got == 1, "green arrow: a zone touching the arrow selects it");
            // AND A ZONE THAT TOUCHES NOTHING selects nothing, or the check above passes on anything.
            check(crossingPick(ed, {60.0, 140.0}, {40.0, 80.0}) == 0,
                  "green arrow: a zone clear of it selects nothing");
        }

        {
            Document d;
            d.entities().clear();
            Editor ed;
            ed.setDocument(&d);
            noSnap(ed);
            ed.setTool(ToolId::AxisAngle);
            ed.onPointerPress({0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerPress({0.0, -50.0}, Qt::LeftButton, Qt::NoModifier);
            check(d.entities().size() == 1, "green axis: an axis angle is on the sheet");
            // A zone out on the +x side, clear of both centre lines (which reach to 59) but across the
            // arc, which passes through (50, 0).
            // Both corners well clear of every line the object draws - the arc at r=50 and the centre
            // lines out to 59 - so only the zone between them can catch it. It spans the arc at 45
            // degrees, where nothing else of the object runs.
            const size_t got = crossingPick(ed, {80.0, 80.0}, {12.0, 12.0});
            std::printf("  green axis: a zone across the arc picked %d object(s)\n",
                        static_cast<int>(got));
            check(got == 1, "green axis: a zone touching the arc selects it");
            // AND A ZONE WELL OUTSIDE IT ALL selects nothing.
            check(crossingPick(ed, {300.0, 300.0}, {200.0, 200.0}) == 0,
                  "green axis: a zone clear of it selects nothing");
        }
    }

    // ---- the head size reaches the DRAWING, not only the measurements ------------------------------
    {
        // The number was read when the object was measured and ignored when it was drawn, so typing a
        // head size moved the box the arrow occupies and changed nothing anybody could see. Rendered
        // twice here, because that is the only place the fault showed.
        Document d;
        d.entities().clear();
        Entity arrow;
        arrow.id = QStringLiteral("VA");
        arrow.type = EntityType::Polygon;
        arrow.a = {10.0, 60.0};
        arrow.b = {110.0, 60.0};
        arrow.meta.insert(QStringLiteral("volumeArrow"), true);
        arrow.meta.insert(QStringLiteral("arrowHead"), 15.0);
        d.entities().push_back(arrow);

        auto shot = [&](double headMm) {
            d.entities()[0].meta.insert(QStringLiteral("arrowHead"), headMm);
            ViewTransform vt;
            vt.view.scale = 1.0;
            vt.view.x = 0.0;
            vt.view.y = 120.0;   // world y runs UP the page; without this the arrow draws off the top
            QImage img(160, 120, QImage::Format_RGB32);
            img.fill(QColor("#1e2832"));
            QPainter p(&img);
            for (const auto& e : d.entities()) EntityRenderer::draw(p, e, d, vt);
            p.end();
            return img;
        };
        const QImage small = shot(15.0);
        const QImage big = shot(70.0);
        // THE PICTURE IS NOT BLANK. Two empty images differ by nothing, so without this the test
        // passes for the one reason it must never pass for.
        int drawn = 0;
        for (int y = 0; y < small.height(); ++y)
            for (int x = 0; x < small.width(); ++x)
                if (small.pixel(x, y) != QColor("#1e2832").rgb()) ++drawn;
        std::printf("  arrow head drawn: %d pixel(s) of arrow in the picture\n", drawn);
        check(drawn > 100, "arrow head: the arrow is actually drawn");
        int differing = 0;
        for (int y = 0; y < small.height(); ++y)
            for (int x = 0; x < small.width(); ++x)
                if (small.pixel(x, y) != big.pixel(x, y)) ++differing;
        std::printf("  arrow head drawn: %d pixel(s) differ between a 15 and a 70 head\n", differing);
        check(differing > 50, "arrow head: setting it changes what is drawn");
    }

    // ---- the far label follows the sweep -----------------------------------------------------------
    {
        // It used to be a number of its own, so opening the arc to 90 left "180" written at the end of
        // it: the annotation saying one thing and the arc it labels another. The end is the start plus
        // however far the thing travels, so a stale number left on an old object must not win.
        Document d;
        d.entities().clear();
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.setTool(ToolId::AxisAngle);
        ed.onPointerPress({60.0, 60.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({60.0, 20.0}, Qt::LeftButton, Qt::NoModifier);
        check(d.entities().size() == 1, "axis label: an axis angle is on the sheet");
        if (d.entities().size() == 1) {
            check(!d.entities()[0].meta.contains(QStringLiteral("axisTo")),
                  "axis label: no end number is stored to go stale");

            auto shot = [&]() {
                ViewTransform vt;
                vt.view.scale = 1.0;
                vt.view.x = 0.0;
                vt.view.y = 160.0;   // world y runs UP the page - see the arrow test above
                QImage img(200, 160, QImage::Format_RGB32);
                img.fill(QColor("#1e2832"));
                QPainter p(&img);
                for (const auto& e : d.entities()) EntityRenderer::draw(p, e, d, vt);
                p.end();
                return img;
            };
            auto differ = [](const QImage& x, const QImage& y) {
                int n = 0;
                for (int j = 0; j < x.height(); ++j)
                    for (int i = 0; i < x.width(); ++i)
                        if (x.pixel(i, j) != y.pixel(i, j)) ++n;
                return n;
            };

            // A LEFTOVER axisTo changes nothing: the drawing no longer consults it.
            const QImage clean = shot();
            // AND THE PICTURE IS NOT BLANK, or "no difference" below means "nothing was drawn".
            int drawn = 0;
            for (int j = 0; j < clean.height(); ++j)
                for (int i = 0; i < clean.width(); ++i)
                    if (clean.pixel(i, j) != QColor("#1e2832").rgb()) ++drawn;
            std::printf("  axis label: %d pixel(s) of axis angle in the picture\n", drawn);
            check(drawn > 100, "axis label: the axis angle is actually drawn");
            d.entities()[0].meta.insert(QStringLiteral("axisTo"), 999.0);
            const QImage stale = shot();
            std::printf("  axis label: a leftover end number moves %d pixel(s)\n",
                        differ(clean, stale));
            check(differ(clean, stale) == 0, "axis label: a stale end number is ignored");
            d.entities()[0].meta.remove(QStringLiteral("axisTo"));

            // AND THE LABELS ARE REALLY BEING DRAWN FROM THE NUMBERS: moving the datum leaves the arc
            // and the axes exactly where they were, so anything that changes is the writing.
            d.entities()[0].meta.insert(QStringLiteral("axisFrom"), 45.0);
            const QImage moved = shot();
            std::printf("  axis label: marking the zero 45 instead of 0 moves %d pixel(s)\n",
                        differ(clean, moved));
            check(differ(clean, moved) > 5, "axis label: the labels come from the numbers");
        }
    }

    // ---- the zero can be turned, and the whole annotation turns with it ---------------------------
    {
        // The second click puts the zero wherever the cursor was, which is quick and rarely exactly
        // right. Turning it moves the whole annotation about its axis WITHOUT touching the radius.
        Document d;
        d.entities().clear();
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.setTool(ToolId::AxisAngle);
        ed.onPointerPress({0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({0.0, -50.0}, Qt::LeftButton, Qt::NoModifier);   // zero straight down
        if (d.entities().size() == 1) {
            // Straight down with a half turn sweeps the RIGHT half of the ring, so the point at -45
            // degrees is on the arc.
            ed.setTool(ToolId::Select);
            ed.clearSelection();
            ed.onPointerPress({35.36, -35.36}, Qt::LeftButton, Qt::NoModifier);
            check(ed.selection().size() == 1, "axis zero: the arc is where it was drawn");

            // Turned a quarter turn, the same half turn sweeps the TOP of the ring instead - so that
            // same point is no longer on it, and the radius has not moved.
            const double wasR = distance(d.entities()[0].a, d.entities()[0].b);
            d.entities()[0].meta.insert(QStringLiteral("axisZero"), 90.0);
            ed.clearSelection();
            ed.onPointerPress({35.36, -35.36}, Qt::LeftButton, Qt::NoModifier);
            check(ed.selection().empty(), "axis zero: turning it takes the arc off that point");
            ed.clearSelection();
            ed.onPointerPress({35.36, 35.36}, Qt::LeftButton, Qt::NoModifier);
            check(ed.selection().size() == 1, "axis zero: and puts it on the other side");
            check(std::abs(distance(d.entities()[0].a, d.entities()[0].b) - wasR) < 1e-9,
                  "axis zero: turning the zero leaves the radius alone");
        }
    }

    // ---- the axis angle: what its handles do, and what its linetype says --------------------------
    {
        // A fresh one, drawn straight down so the sweep covers the right-hand half of the ring.
        auto fresh = [](Document& d, Editor& ed) {
            d.entities().clear();
            ed.setDocument(&d);
            noSnap(ed);
            ed.setTool(ToolId::AxisAngle);
            ed.onPointerPress({0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerPress({0.0, -50.0}, Qt::LeftButton, Qt::NoModifier);
            ed.setTool(ToolId::Select);
        };
        // Take hold of a handle and drag it, the way the mouse does: press on it, move, release.
        auto dragTo = [](Editor& ed, Vec2 grab, Vec2 to) {
            ed.onPointerPress(grab, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerMove(to);
            ed.onPointerRelease(to, Qt::LeftButton);
        };

        // ---- IT IS BORN A CENTRE LINE, and says so on itself rather than being forced. ----
        {
            Document d;
            Editor ed;
            fresh(d, ed);
            check(d.entities().size() == 1, "axis linetype: one object");
            if (d.entities().size() == 1)
                check(d.entities()[0].meta.value("linetype").toString() ==
                          QLatin1String("CENTER"),
                      "axis linetype: a new one is a centre line, on the object");
        }

        // ---- THE START GOES WHERE IT IS DRAGGED: out for radius, round for where the sweep begins. ----
        {
            Document d;
            Editor ed;
            fresh(d, ed);
            ed.selectById(d.entities()[0].id);
            // A fresh one points straight down and sweeps half a turn, so it finishes straight up.
            const double endWas = std::atan2(d.entities()[0].b.y - d.entities()[0].a.y,
                                             d.entities()[0].b.x - d.entities()[0].a.x) +
                                  d.entities()[0].meta.value("axisSweep").toDouble() * M_PI / 180.0;
            // Grabbed at the handle (0, -50) and dragged off to one side. It used to be projected back
            // onto the direction it already had, so the only thing this drag could change was the
            // radius - and an angle whose start cannot be moved is an angle that always reads the same.
            dragTo(ed, {0.0, -50.0}, {60.0, -60.0});
            const Entity& ax = d.entities()[0];
            const double nowStart = std::atan2(ax.b.y - ax.a.y, ax.b.x - ax.a.x);
            const double nowEnd = nowStart + ax.meta.value("axisSweep").toDouble() * M_PI / 180.0;
            std::printf("  axis start: dragged to (60,-60) -> r %.2f, start %.1f deg, sweep %.1f, "
                        "cross %.1f deg\n",
                        distance(ax.a, ax.b), nowStart * 180.0 / M_PI,
                        ax.meta.value("axisSweep").toDouble(),
                        ax.meta.value("axisCross").toDouble());
            // The radius is the distance to where it was dragged - hypot(60,60) = 84.85.
            check(std::abs(distance(ax.a, ax.b) - 84.85) < 0.05,
                  "axis start: the radius follows the drag");
            check(std::abs(nowStart * 180.0 / M_PI + 45.0) < 1e-6,
                  "axis start: and so does where the sweep begins");
            // THE CROSS IS PINNED WHERE IT WAS. It is the datum the angle is measured from, and a
            // datum that follows the thing being measured tells you nothing.
            check(std::abs(ax.meta.value("axisCross").toDouble() + 90.0) < 1e-6,
                  "axis start: the cross stays where it was");
            // AND THE FAR LIMIT STAYS PUT, so the angle opens by what the start moved: 180 -> 135.
            check(std::abs(std::remainder(nowEnd - endWas, 2 * M_PI)) < 1e-6,
                  "axis start: the far limit does not move");
            check(std::abs(ax.meta.value("axisSweep").toDouble() - 135.0) < 1e-4,
                  "axis start: so the angle reads 135 instead of 180");
        }

        // ---- AND A START LEFT IN MID-AIR IS STOPPED BY A LINE OF ITS OWN. ----
        {
            // The far end has always had one where it needed it (see THE STOP LINE below). The start
            // never could: it was pinned to the cross, so it always finished on a centre line. Now it
            // can be dragged off, and an arc that BEGINS in mid-air says no more about where the
            // travel starts than one that ends there says about where it finishes.
            Document d;
            Editor ed;
            fresh(d, ed);
            // A start at -45 against a cross still lying where it was, and the axis lengthened to 60
            // for the same reason the far-end test lengthens it: at the default the stop line is a
            // stub whose every point is a few millimetres from both centre lines, so a click there
            // proves nothing. Set here rather than dragged - the drag is tested just above, and this
            // is about what the object then DRAWS.
            Entity& ax = d.entities()[0];
            ax.b = {50.0 * std::cos(-M_PI / 4.0), 50.0 * std::sin(-M_PI / 4.0)};
            ax.meta.insert(QStringLiteral("axisCross"), -90.0);
            ax.meta.insert(QStringLiteral("axisSweep"), 135.0);
            ax.meta.insert(QStringLiteral("axisSize"), 60.0);
            ed.clearSelection();
            ed.onPointerPress({28.0, -28.0}, Qt::LeftButton, Qt::NoModifier);
            std::printf("  axis start stop: a click on the start's stop line picked %d\n",
                        int(ed.selection().size()));
            check(ed.selection().size() == 1,
                  "axis start stop: the start is stopped against a line back to the centre");

            // AND WITH THE START BACK ON THE CROSS there is none, which is the check that the line
            // above was the stop line and not the arc or an axis.
            Document d2;
            Editor ed2;
            fresh(d2, ed2);
            d2.entities()[0].meta.insert(QStringLiteral("axisCross"), -90.0);
            d2.entities()[0].meta.insert(QStringLiteral("axisSize"), 60.0);
            ed2.clearSelection();
            ed2.onPointerPress({28.0, -28.0}, Qt::LeftButton, Qt::NoModifier);
            std::printf("  axis start stop: with the start on the cross it picked %d\n",
                        int(ed2.selection().size()));
            check(ed2.selection().empty(),
                  "axis start stop: a start that lies on a centre line needs no line of its own");
        }

        // ---- WHILE IT IS HELD, A TYPED NUMBER IS THE RADIUS. ----
        {
            Document d;
            Editor ed;
            fresh(d, ed);
            ed.selectById(d.entities()[0].id);
            ed.onPointerPress({0.0, -50.0}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerMove({0.0, -70.0});
            // The box asks for the radius, not for how far the handle travels - which is what every
            // other drag in the editor asks for and is no use at all here.
            const auto asked = ed.drawInputFieldLabels();
            std::printf("  axis radius typed: the box asks for %s\n",
                        asked.empty() ? "nothing" : qPrintable(asked.front()));
            check(asked.size() == 1 && asked.front() == QLatin1String("Radius"),
                  "axis radius typed: the box asks for a radius");
            check(ed.acceptCommandInput(QStringLiteral("120")),
                  "axis radius typed: and the number is taken");
            const Entity& ax = d.entities()[0];
            std::printf("  axis radius typed: 120 -> r %.2f at (%.2f, %.2f)\n",
                        distance(ax.a, ax.b), ax.b.x, ax.b.y);
            check(std::abs(distance(ax.a, ax.b) - 120.0) < 1e-6,
                  "axis radius typed: the radius is exactly what was typed");
            check(std::abs(ax.b.x) < 1e-6 && ax.b.y < 0.0,
                  "axis radius typed: along the way it already faced");
        }

        // ---- THE NUMBERS ARE DEGREES AND SAY SO. ----
        {
            Document d;
            Editor ed;
            fresh(d, ed);
            const QString from = axisAngleLabelText(d.entities()[0], true);
            const QString to = axisAngleLabelText(d.entities()[0], false);
            std::printf("  axis degrees: the two limits read \"%s\" and \"%s\"\n",
                        qPrintable(from), qPrintable(to));
            check(from == QStringLiteral("0") + QChar(0x00B0) &&
                      to == QStringLiteral("180") + QChar(0x00B0),
                  "axis degrees: both limits carry the degree sign");
            // BUT NOT WORDS SOMEBODY TYPED. A mechanism whose positions are not degrees says OPEN and
            // SHUT, and "OPEN deg" is nonsense.
            d.entities()[0].meta.insert(QStringLiteral("axisToText"), QStringLiteral("OPEN"));
            check(axisAngleLabelText(d.entities()[0], false) == QLatin1String("OPEN"),
                  "axis degrees: an override is left exactly as it was typed");
        }

        // ---- A LABEL GOES WHERE IT IS PUT, AND STAYS THERE. ----
        {
            Document d;
            Editor ed;
            fresh(d, ed);
            ed.selectById(d.entities()[0].id);
            // Grabbed where the label actually IS, asked of the same helper the renderer draws it
            // with - a hard-coded point here would only be testing my arithmetic, and would go stale
            // the next time the placing rule changes (it just did: the label steps aside now).
            const Vec2 labelAt = axisAngleLabelAnchor(d.entities()[0], true);
            std::printf("  axis text: the zero label sits at (%.2f, %.2f)\n", labelAt.x, labelAt.y);
            // AND IT IS BESIDE THE CENTRE LINE, not on it. The zero of a fresh one points straight
            // down, so the vertical centre line runs through x = 0 - and a label centred there would
            // have the dash-dot through the middle of the digits, which is what this fixes.
            check(std::abs(labelAt.x) > 1.0, "axis text: the label is beside the axis, not on it");
            dragTo(ed, labelAt, {40.0, -80.0});
            {
                const Entity& ax = d.entities()[0];
                std::printf("  axis text: the zero label moved to (%.2f, %.2f)\n",
                            ax.meta.value("fromTextX").toDouble(),
                            ax.meta.value("fromTextY").toDouble());
                check(ax.meta.contains(QStringLiteral("fromTextX")),
                      "axis text: the label remembers where it was put");
                check(std::abs(ax.meta.value("fromTextX").toDouble() - 40.0) < 1e-6 &&
                          std::abs(ax.meta.value("fromTextY").toDouble() + 80.0) < 1e-6,
                      "axis text: exactly where it was dragged to");
            }
            // OPENING OR CLOSING THE ARC LEAVES IT THERE. A label pinned to the arc would travel with
            // it, which is what somebody who just moved it clear of the geometry does not want.
            d.entities()[0].meta.insert(QStringLiteral("axisSweep"), 90.0);
            check(std::abs(d.entities()[0].meta.value("fromTextX").toDouble() - 40.0) < 1e-6,
                  "axis text: changing the angle does not move it");
            // BUT MOVING THE OBJECT TAKES IT ALONG - it is part of the object, not a note beside it.
            translateEntity(d.entities()[0], {100.0, 0.0});
            std::printf("  axis text: after moving the object it is at (%.2f, %.2f)\n",
                        d.entities()[0].meta.value("fromTextX").toDouble(),
                        d.entities()[0].meta.value("fromTextY").toDouble());
            check(std::abs(d.entities()[0].meta.value("fromTextX").toDouble() - 140.0) < 1e-6,
                  "axis text: and moving the object carries it");
        }

        // ---- TURNING IT IS THE TURN COMMAND, and the whole annotation turns together. ----
        {
            Document d;
            Editor ed;
            fresh(d, ed);
            const double wasR = distance(d.entities()[0].a, d.entities()[0].b);
            // A quarter turn anticlockwise about the axis: the zero was straight down, so it ends up
            // pointing along +x.
            rotateEntity(d.entities()[0], {0.0, 0.0}, M_PI / 2.0);
            const Entity& ax = d.entities()[0];
            std::printf("  axis turn: the zero now points %.1f deg, r %.2f\n",
                        std::atan2(ax.b.y - ax.a.y, ax.b.x - ax.a.x) * 180.0 / M_PI,
                        distance(ax.a, ax.b));
            check(std::abs(ax.b.x - 50.0) < 1e-6 && std::abs(ax.b.y) < 1e-6,
                  "axis turn: Rotate turns where the zero points");
            check(std::abs(distance(ax.a, ax.b) - wasR) < 1e-9,
                  "axis turn: and leaves the radius alone");
        }

        // ---- SNAP APPLIES while the arc is dragged: it is handed the snapped point. ----
        {
            Document d;
            Editor ed;
            d.entities().clear();
            ed.setDocument(&d);
            // Something to snap TO: a line whose end sits at a distance from the axis that no cursor
            // position in this test would give by accident.
            Entity mark;
            mark.id = QStringLiteral("SNAPME");
            mark.type = EntityType::Line;
            mark.a = {77.0, 0.0};
            mark.b = {77.0, 20.0};
            d.entities().push_back(mark);

            ed.setTool(ToolId::AxisAngle);
            ed.onPointerPress({0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerPress({0.0, -50.0}, Qt::LeftButton, Qt::NoModifier);
            ed.setTool(ToolId::Select);
            const QString axId = d.entities().back().id;
            ed.selectById(axId);

            // Endpoint snapping on, and the arc handle dragged to NEAR that end - close enough to snap
            // to it, not on it. If the snap is honoured the radius comes out at exactly 77.
            auto& sn = ed.snapSettings();
            sn.objectSnap = true;
            sn.snapEndpoint = true;
            sn.snapNearest = false;   // nearest would snap to the line itself, not to its end
            sn.gridSnap = false;
            sn.ortho = false;
            sn.polar = false;
            ed.onPointerPress({0.0, -50.0}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerMove({76.4, 0.6});
            ed.onPointerRelease({76.4, 0.6}, Qt::LeftButton);
            const Entity* ax = nullptr;
            for (const Entity& e : d.entities())
                if (e.id == axId) ax = &e;
            check(ax != nullptr, "axis snap: the axis angle is still there");
            if (ax) {
                std::printf("  axis snap: dragged near (77,0) -> r %.4f\n", distance(ax->a, ax->b));
                check(std::abs(distance(ax->a, ax->b) - 77.0) < 1e-6,
                      "axis snap: the radius lands on the snapped point exactly");
            }
        }
    }

    // ---- what a new axis angle is born with, and the line an angle stops against -------------------
    {
        auto fresh = [](Document& d, Editor& ed, double sweepDeg) {
            d.entities().clear();
            ed.setDocument(&d);
            noSnap(ed);
            ed.setTool(ToolId::AxisAngle);
            ed.onPointerPress({0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerPress({0.0, -50.0}, Qt::LeftButton, Qt::NoModifier);   // zero straight down
            if (!d.entities().empty())
                d.entities()[0].meta.insert(QStringLiteral("axisSweep"), sweepDeg);
            ed.setTool(ToolId::Select);
        };

        // ---- THE DEFAULTS. ----
        {
            Document d;
            Editor ed;
            fresh(d, ed, 180.0);
            check(d.entities().size() == 1, "axis defaults: one object");
            if (d.entities().size() == 1) {
                const Entity& ax = d.entities()[0];
                std::printf("  axis defaults: text %.1f, axis %.1f, arrows %s\n",
                            ax.meta.value("textSize").toDouble(),
                            ax.meta.value("axisSize").toDouble(),
                            qPrintable(ax.meta.value("arrowEnds").toString()));
                check(std::abs(ax.meta.value("textSize").toDouble() - 10.0) < 1e-9,
                      "axis defaults: the text is 10 high");
                // SIZED FROM THE PULL: one drag out from the centre sizes the arc and the centre
                // lines together, the arms reaching a little past the arc so the thing reads as an
                // axis with a travel about it. Pulled 50, so the arms are 59.
                check(std::abs(ax.meta.value("axisSize").toDouble() - 59.0) < 1e-9,
                      "axis defaults: the axis is sized from the pull");
                check(ax.meta.value("axisSize").toDouble() > distance(ax.a, ax.b),
                      "axis defaults: and reaches past the arc");
                check(ax.meta.value("arrowEnds").toString() == QLatin1String("both"),
                      "axis defaults: an arrow on both ends");
                check(std::abs(ax.meta.value("arrowSize").toDouble() - 7.0) < 1e-9,
                      "axis defaults: and the arrows are 7");
            }
        }

        // ---- THE STOP LINE, when the sweep does not finish on a centre line. ----
        {
            // 45 degrees from a zero pointing straight down ends at -45, which is on neither centre
            // line - so the angle needs a line of its own to stop against, or the arc simply ends in
            // mid-air and the drawing does not say where the travel finishes.
            Document d;
            Editor ed;
            fresh(d, ed, 45.0);
            // THE AXIS IS LENGTHENED FIRST, to 60. With the default 10 the stop line is a 10mm stub
            // running out at 45 degrees, whose every point is within a few millimetres of BOTH centre
            // lines - so a click there lands on an axis and says nothing about the stop line either
            // way. At 60 the line reaches (42,-42) and a point on it is nowhere near anything else.
            d.entities()[0].meta.insert(QStringLiteral("axisSize"), 60.0);
            // Two thirds of the way out: 28 from each centre line, and 10 clear of the arc at 50.
            ed.clearSelection();
            ed.onPointerPress({28.0, -28.0}, Qt::LeftButton, Qt::NoModifier);
            std::printf("  axis stop: a click on the stop line picked %d\n",
                        static_cast<int>(ed.selection().size()));
            check(ed.selection().size() == 1, "axis stop: the stop line is part of the object");

            // AND WITH A QUARTER TURN there is no stop line: the sweep finishes ON the horizontal
            // centre line, which stops it already. The same point now picks nothing, which is the
            // check that the line above was really the stop line and not something else.
            Document d2;
            Editor ed2;
            fresh(d2, ed2, 90.0);
            d2.entities()[0].meta.insert(QStringLiteral("axisSize"), 60.0);
            ed2.clearSelection();
            ed2.onPointerPress({28.0, -28.0}, Qt::LeftButton, Qt::NoModifier);
            std::printf("  axis stop: with a quarter turn the same point picked %d\n",
                        static_cast<int>(ed2.selection().size()));
            check(ed2.selection().empty(),
                  "axis stop: an angle that ends on an axis needs no stop line");
        }

        // ---- THE AXIS LENGTH IS PULLED ON ITS OWN, and the arc stays where it is. ----
        {
            Document d;
            Editor ed;
            fresh(d, ed, 180.0);
            ed.selectById(d.entities()[0].id);
            const double wasR = distance(d.entities()[0].a, d.entities()[0].b);
            // A joint at every end of the axis. The zero points straight down and a 50 pull gives arms
            // of 59, so they run to (0,-59), (0,59), (59,0) and (-59,0). This one is pushed IN to 30 -
            // which is also the check that the axis can be made shorter than the arc, the case a
            // proportional size could never draw.
            ed.onPointerPress({59.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerMove({30.0, 0.0});
            ed.onPointerRelease({30.0, 0.0}, Qt::LeftButton);
            const Entity& ax = d.entities()[0];
            std::printf("  axis size: moved the arm to 30 -> axis %.2f, arc still %.2f\n",
                        ax.meta.value("axisSize").toDouble(), distance(ax.a, ax.b));
            check(std::abs(ax.meta.value("axisSize").toDouble() - 30.0) < 1e-6,
                  "axis size: the arm end sets how long the axis is");
            check(ax.meta.value("axisSize").toDouble() < distance(ax.a, ax.b),
                  "axis size: an axis shorter than its own arc, which is now drawable");
            check(std::abs(distance(ax.a, ax.b) - wasR) < 1e-9,
                  "axis size: and the annotation does not move with it");
        }

        // ---- WHILE THE ARC END IS DRAGGED, A TYPED NUMBER IS THE SWEPT ANGLE. ----
        {
            Document d;
            Editor ed;
            fresh(d, ed, 180.0);
            ed.selectById(d.entities()[0].id);
            // The far end of a half turn from a zero pointing down is straight up, at (0, 50).
            ed.onPointerPress({0.0, 50.0}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerMove({35.0, 35.0});
            const auto asked = ed.drawInputFieldLabels();
            std::printf("  axis sweep typed: the box asks for %s\n",
                        asked.empty() ? "nothing" : qPrintable(asked.front()));
            check(asked.size() == 1 && asked.front() == QLatin1String("Angle"),
                  "axis sweep typed: the box asks for an angle");
            check(ed.acceptCommandInput(QStringLiteral("45")),
                  "axis sweep typed: and the number is taken");
            const Entity& ax = d.entities()[0];
            std::printf("  axis sweep typed: 45 -> sweep %.2f, arc r %.2f\n",
                        ax.meta.value("axisSweep").toDouble(), distance(ax.a, ax.b));
            check(std::abs(ax.meta.value("axisSweep").toDouble() - 45.0) < 1e-9,
                  "axis sweep typed: the sweep is exactly what was typed");
            check(std::abs(distance(ax.a, ax.b) - 50.0) < 1e-9,
                  "axis sweep typed: and the radius is left alone");
        }
    }

    std::printf(g_fail ? "  -> %d failed\n" : "  -> ok\n", g_fail);
    return g_fail;
}
