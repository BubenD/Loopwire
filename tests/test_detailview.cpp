// A DETAIL VIEW: the small thing on the drawing, drawn again bigger somewhere there is room for it. Two
// circles and a magnification, and it is a VIEW rather than a copy - what goes in the big circle is read out
// of the document every time it is drawn, so changing the thing changes its detail. A detail that is a copy
// goes stale the first time somebody edits what it was a copy of, and a stale detail on a released sheet is
// worse than no detail at all. Which is measured here rather than eyeballed.
#include "app/Editor.h"
#include "model/Document.h"

#include <QJsonArray>
#include <QStringList>

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

// Snaps off: a test clicks exact coordinates and means them.
void noSnap(Editor& ed) {
    auto& s = ed.snapSettings();
    s.objectSnap = false;
    s.gridSnap = false;
    s.ortho = false;
    s.polar = false;
}

} // namespace

int run_detailview_tests() {
    std::printf("detail view tests:\n");
    failures = 0;

    // ---- three clicks, because it is three decisions ----------------------------------------------
    {
        // WHAT is wanted, HOW MUCH of it, and WHERE the enlargement goes. None can be guessed from the
        // others: a circle sized to whatever happens to be under the cursor guesses wrong on a dense drawing
        // every time, and "somewhere there is room" is not something a program knows.
        Document d;
        Entity c;
        c.id = QStringLiteral("thing");
        c.type = EntityType::Circle;
        c.center = {10.0, 10.0};
        c.radius = 1.5;
        d.entities().push_back(c);
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.setTool(ToolId::DetailView);
        check(ed.detailStage() == 0, "detail: nothing decided yet");
        ed.onPointerPress({10.0, 10.0}, Qt::LeftButton, Qt::NoModifier);
        check(ed.detailStage() == 1, "detail: the first click says what is wanted");
        ed.onPointerPress({14.0, 10.0}, Qt::LeftButton, Qt::NoModifier);
        check(ed.detailStage() == 2, "detail: the second says how much of it");
        check(std::abs(ed.detailRadius() - 4.0) < 1e-6, "detail: measured off the centre");
        ed.onPointerPress({60.0, 60.0}, Qt::LeftButton, Qt::NoModifier);
        check(ed.detailStage() == 0, "detail: the third places it, and it stays armed for the next");

        const Entity* dv = nullptr;
        for (const Entity& e : d.entities())
            if (e.type == EntityType::DetailView) dv = &e;
        check(dv != nullptr, "detail: a detail view is ONE object, not two circles and a caption");
        if (dv) {
            check(std::abs(dv->radius - 4.0) < 1e-6 && distance(dv->center, Vec2{10.0, 10.0}) < 1e-6,
                  "detail: round the thing, where the thing actually is");
            check(distance(dv->b, Vec2{60.0, 60.0}) < 1e-6,
                  "detail: and drawn again where there was room");
            check(dv->meta.value(QStringLiteral("detailTag")).toString() == QStringLiteral("A"),
                  "detail: lettered A, being the first");
            const double scale = dv->meta.value(QStringLiteral("detailScale")).toDouble();
            std::printf("  detail: A at %.0f,%.0f showing r=%.1f, enlarged %.1f times\n", dv->b.x, dv->b.y,
                        dv->radius, scale);
            check(scale > 1.0, "detail: enlarged, which is the whole idea");
            // BOTH circles are the object. Answering with less than both leaves half of it out of Zoom All
            // and lets the canvas cull the half that is on screen.
            const Bounds bb = dv->bounds();
            check(bb.min.x <= 10.0 - 4.0 && bb.max.x >= 60.0 + 4.0 * scale,
                  "detail: it reaches from the thing to its enlargement - one object in two places");
        }

        // The letters run on, and a letter is taken once and kept.
        ed.onPointerPress({30.0, 10.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({33.0, 10.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({30.0, 60.0}, Qt::LeftButton, Qt::NoModifier);
        QStringList tags;
        for (const Entity& e : d.entities())
            if (e.type == EntityType::DetailView)
                tags << e.meta.value(QStringLiteral("detailTag")).toString();
        std::printf("  detail: lettered %s\n", qPrintable(tags.join(QStringLiteral(", "))));
        check(tags.size() == 2 && tags.contains(QStringLiteral("A")) &&
                  tags.contains(QStringLiteral("B")),
              "detail: the next one is B, not A again");

        // A detail of nothing is nothing: the second click on the centre is refused rather than making a
        // circle of no radius that could never be seen or picked.
        Document d2;
        Editor e2;
        e2.setDocument(&d2);
        noSnap(e2);
        e2.setTool(ToolId::DetailView);
        e2.onPointerPress({0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
        e2.onPointerPress({0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
        check(e2.detailStage() == 1, "detail: a detail of nothing is refused, and it waits for a real one");
    }

    // ---- annotative: the enlargement is a live window, not a picture -------------------------------
    {
        // A detail is drawn BECAUSE the thing is too small to work on. A view that can only be looked at
        // sends you back to the small version to do the work at the size you could not read - so the
        // enlarged circle reaches through to the real geometry, and the change shows in both at once.
        Document d;
        Entity l;
        l.id = QStringLiteral("wire");
        l.type = EntityType::Line;
        l.a = {9.0, 10.0};
        l.b = {11.0, 10.0};
        d.entities().push_back(l);
        Entity dv;
        dv.id = QStringLiteral("dv");
        dv.type = EntityType::DetailView;
        dv.center = {10.0, 10.0};
        dv.radius = 4.0;
        dv.b = {100.0, 100.0};
        dv.meta.insert(QStringLiteral("detailScale"), 4.0);
        dv.meta.insert(QStringLiteral("detailTag"), QStringLiteral("A"));
        d.entities().push_back(dv);
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);

        // NOT annotative: the big circle is a picture, and a click in it is a click where it looks.
        ed.onPointerMove({100.0, 100.0});
        std::printf("  window: not annotative, a click at 100,100 stays at %.1f,%.1f\n",
                    ed.snappedCursor().x, ed.snappedCursor().y);
        check(distance(ed.snappedCursor(), Vec2{100.0, 100.0}) < 1e-6,
              "window: an ordinary detail is a picture - the cursor is where it is");

        // Annotative: the same click is really at the place in the drawing that circle is a view of.
        for (Entity& e : d.entities())
            if (e.id == QStringLiteral("dv")) e.meta.insert(QStringLiteral("detailAnnotative"), true);
        ed.onPointerMove({104.0, 100.0});   // 4 right of the middle, at 4x = 1 in the drawing
        std::printf("  window: annotative, 4 right of the middle lands at %.2f,%.2f\n",
                    ed.snappedCursor().x, ed.snappedCursor().y);
        check(distance(ed.snappedCursor(), Vec2{11.0, 10.0}) < 1e-6,
              "window: it reaches through - 4 across the enlargement is 1 across the drawing");

        // Which means the REAL object is what gets picked in there, and so what gets moved.
        ed.onPointerPress({104.0, 100.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerRelease({104.0, 100.0}, Qt::LeftButton);
        check(ed.selection().size() == 1 && ed.selection().front() == QStringLiteral("wire"),
              "window: so clicking in the enlargement picks the REAL object, not the view");

        // And the view's own circle is NOT looked through, or it could never be taken hold of and moved off
        // whatever it ended up covering.
        ed.clearSelection();
        ed.onPointerMove({116.0, 100.0});   // on the enlarged circle's own edge (radius 16)
        check(distance(ed.snappedCursor(), Vec2{116.0, 100.0}) < 1e-6,
              "window: its own edge stays where it is, so the view can still be grabbed");
        // Its centre handle too - but only while it is selected, which is the only time there IS a handle
        // there. A dead spot in the middle of every enlargement on a view nobody has selected would be a
        // hole in the window with nothing behind it.
        ed.onPointerPress({116.0, 100.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerRelease({116.0, 100.0}, Qt::LeftButton);
        check(ed.selection().size() == 1 && ed.selection().front() == QStringLiteral("dv"),
              "window: its edge picks the view itself");
        ed.onPointerMove({100.0, 100.0});
        check(distance(ed.snappedCursor(), Vec2{100.0, 100.0}) < 1e-6,
              "window: and with it selected, its centre handle can be grabbed");

        // Placing a detail is not looking through one either: a detail put down inside another would
        // otherwise land wherever that one is a view of instead of where it was pointed.
        ed.setTool(ToolId::DetailView);
        ed.onPointerMove({104.0, 100.0});
        check(distance(ed.snappedCursor(), Vec2{104.0, 100.0}) < 1e-6,
              "window: while placing one, the cursor means the place it is");
    }


    // ---- one object in two places, and each place moves on its own ---------------------------------
    {
        // The circle round the thing and the circle it is enlarged in are in different parts of the sheet for
        // a reason. A source circle that tows its enlargement across the drawing every time it is nudged on
        // to the right bit of geometry is unusable - and that is what the generic centre handle did, because
        // it translated the whole object.
        Document d;
        Entity dv;
        dv.id = QStringLiteral("dv");
        dv.type = EntityType::DetailView;
        dv.center = {10.0, 10.0};
        dv.radius = 4.0;
        dv.b = {100.0, 100.0};
        dv.meta.insert(QStringLiteral("detailScale"), 2.0);
        d.entities().push_back(dv);
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        // Pick it by the edge of the circle round the thing, then take hold of that circle's centre handle.
        ed.onPointerPress({14.0, 10.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerRelease({14.0, 10.0}, Qt::LeftButton);
        check(ed.selection().size() == 1, "apart: the ring round the thing picks the view up");
        ed.onPointerPress({10.0, 10.0}, Qt::LeftButton, Qt::NoModifier);   // its centre handle
        ed.onPointerMove({20.0, 10.0});
        const Entity* now = nullptr;
        for (const Entity& e : d.entities())
            if (e.type == EntityType::DetailView) now = &e;
        check(now != nullptr, "apart: still there");
        if (now) {
            std::printf("  apart: the thing moved to %.1f,%.1f, the enlargement stayed at %.1f,%.1f\n",
                        now->center.x, now->center.y, now->b.x, now->b.y);
            check(distance(now->center, Vec2{20.0, 10.0}) < 1e-6,
                  "apart: moving the small circle moves which bit of the drawing it is a view of");
            check(distance(now->b, Vec2{100.0, 100.0}) < 1e-6,
                  "apart: and leaves the enlargement exactly where it was put");
        }
    }

    // ---- what it says, and where -------------------------------------------------------------------
    {
        // Two texts, because they say different things: the letter on the circle round the thing is a
        // pointer, and the heading on the enlargement is a heading. And each runs round its own circle,
        // because a letter fixed at one o'clock lands on the geometry about half the time.
        Document d;
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.setTool(ToolId::DetailView);
        ed.onPointerPress({0.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({5.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({50.0, 0.0}, Qt::LeftButton, Qt::NoModifier);
        const Entity* dv = nullptr;
        for (const Entity& e : d.entities())
            if (e.type == EntityType::DetailView) dv = &e;
        check(dv != nullptr, "says: placed");
        if (dv) {
            // It is drawn in the LAYER'S colour like everything else, and orange only when SELECTED - which
            // is where a colour of its own actually helps, on a sheet where every other circle is a hole. A
            // ring that is orange all the time is an orange ring on a released drawing, and the colours on a
            // sheet are the drawing office's to choose.
            std::printf("  says: colour %s, letter %s\n",
                        dv->color.isEmpty() ? "the layer's" : qPrintable(dv->color),
                        qPrintable(dv->meta.value(QStringLiteral("detailTag")).toString()));
            check(dv->color.isEmpty(),
                  "says: no colour forced on it - orange is the SELECTED look, not the drawn one");
            // The heading is empty to start with, which is what makes it default to DETAIL and the letter -
            // the convention - while still being anything an office wants to write instead.
            check(dv->meta.value(QStringLiteral("detailBigText")).toString().isEmpty(),
                  "says: the heading starts as the convention rather than as a copy of the letter");
        }
    }


    // ---- an ordinary detail is a PICTURE; only an annotative one passes changes both ways -----------
    {
        // The switch decides where the view is READ FROM, which is the whole difference between the two.
        // Annotative, it comes out of the document every time it is drawn, so the two circles are the same
        // thing seen twice. Ordinary, it comes from the copy taken when it was placed - so drawing something
        // new by the small circle leaves the enlargement alone, and the two stop passing anything to each
        // other, which is what an ordinary detail view IS.
        Document d;
        Entity l;
        l.id = QStringLiteral("first");
        l.type = EntityType::Line;
        l.a = {8.0, 10.0};
        l.b = {12.0, 10.0};
        d.entities().push_back(l);
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.setTool(ToolId::DetailView);
        ed.onPointerPress({10.0, 10.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({14.0, 10.0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({80.0, 10.0}, Qt::LeftButton, Qt::NoModifier);
        const Entity* dv = nullptr;
        for (const Entity& e : d.entities())
            if (e.type == EntityType::DetailView) dv = &e;
        check(dv != nullptr, "picture: placed");
        if (!dv) return failures;

        // Neither switch is on to begin with: a detail is drawn to show a SHAPE, and dimensions sized for the
        // small version become the biggest thing in the circle and cover what they were measuring. And a live
        // window is the special case, not the ordinary one.
        check(!dv->meta.value(QStringLiteral("detailAnnotative")).toBool(),
              "picture: not annotative unless it is asked for");
        check(!dv->meta.value(QStringLiteral("detailShowDims")).toBool(),
              "picture: and it does not catch the dimensions unless it is asked to");

        // It has something to be a picture OF, taken when it was placed.
        const int snapWhenPlaced = dv->meta.value(QStringLiteral("detailSnap")).toArray().size();
        std::printf("  picture: it is a picture of %d object(s)\n", snapWhenPlaced);
        check(snapWhenPlaced == 1, "picture: the one line that was inside its circle");

        // Something new drawn by the small circle does NOT get into that picture. This is the "not passed to
        // each other" the switch is for: a picture is of how the thing WAS.
        Entity later;
        later.id = QStringLiteral("later");
        later.type = EntityType::Line;
        later.a = {8.0, 11.0};
        later.b = {12.0, 11.0};
        d.entities().push_back(later);
        for (const Entity& e : d.entities())
            if (e.type == EntityType::DetailView) dv = &e;
        check(dv->meta.value(QStringLiteral("detailSnap")).toArray().size() == snapWhenPlaced,
              "picture: something drawn afterwards does not appear in it");

        // Pointing it somewhere else re-takes the picture, because a view that kept the old copy would show
        // the bit of drawing it USED to be over, which is a detail view telling a lie.
        ed.setTool(ToolId::Select);   // the tool stays armed after a placement, so put it away first
        ed.onPointerPress({14.0, 10.0}, Qt::LeftButton, Qt::NoModifier);   // pick it by its ring
        ed.onPointerRelease({14.0, 10.0}, Qt::LeftButton);
        check(ed.selection().size() == 1, "picture: its ring picks it up");
        ed.onPointerPress({10.0, 10.0}, Qt::LeftButton, Qt::NoModifier);   // its centre handle
        ed.onPointerMove({10.0, 10.5});                                    // over both lines now
        for (const Entity& e : d.entities())
            if (e.type == EntityType::DetailView) dv = &e;
        const int snapAfter = dv->meta.value(QStringLiteral("detailSnap")).toArray().size();

        std::printf("  picture: pointed at both lines, it is now a picture of %d\n", snapAfter);
        check(snapAfter == 2, "picture: moved, it is a picture of what it is over NOW");
    }

    if (failures == 0) std::printf("  -> ok\n");
    else std::printf("  -> %d failed\n", failures);
    return failures;
}
