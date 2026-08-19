// A DETAIL VIEW: the small thing on the drawing, drawn again bigger somewhere there is room for it. Two
// circles and a magnification, and it is a VIEW rather than a copy - what goes in the big circle is read out
// of the document every time it is drawn, so changing the thing changes its detail. A detail that is a copy
// goes stale the first time somebody edits what it was a copy of, and a stale detail on a released sheet is
// worse than no detail at all. Which is measured here rather than eyeballed.
#include "app/Editor.h"
#include "model/Document.h"

#include "render/EntityRenderer.h"

#include <QImage>
#include <QJsonArray>
#include <QPainter>
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
        check(ed.detailStage() == 0, "detail: the third places it");
        // AND THE CURSOR GOES BACK TO SELECT, with the view it just placed picked up. A detail view is a thing
        // you place and then arrange - move it, size it, letter it - and still armed, the first click meant to
        // take hold of it starts another one instead.
        check(ed.tool() == ToolId::Select, "detail: and the cursor goes back to select");
        check(ed.selection().size() == 1, "detail: with the view it placed picked up, ready to be arranged");

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
        ed.setTool(ToolId::DetailView);
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

        // AN ASSOCIATIVE view is a WINDOW on the parent geometry, and the cursor reaches through it: that is
        // what ties the two together, and it is the whole reason to make one - a detail view is drawn BECAUSE
        // the geometry is too small to work on. The other kind is a COPY of real objects instead, edited
        // directly - see the copy test - which is why reaching through that one would be wrong.
        for (Entity& e : d.entities())
            if (e.id == QStringLiteral("dv")) e.meta.insert(QStringLiteral("detailAnnotative"), true);
        ed.onPointerMove({104.0, 100.0});   // 4 right of the middle, at 4x = 1 in the drawing
        std::printf("  window: 4 right of the middle lands at %.2f,%.2f\n", ed.snappedCursor().x,
                    ed.snappedCursor().y);
        check(distance(ed.snappedCursor(), Vec2{11.0, 10.0}) < 1e-6,
              "window: it reaches through - 4 across the enlargement is 1 across the drawing");
        ed.onPointerMove({96.0, 100.0});    // and 4 the other way is 1 back down the drawing
        check(distance(ed.snappedCursor(), Vec2{9.0, 10.0}) < 1e-6,
              "window: both ways, by the same mapping");
        ed.onPointerMove({104.0, 100.0});

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


    // ---- the copy holds EVERYTHING the boundary was over --------------------------------------------
    {
        // What an object DRAWS and what it STORES are not the same thing, and the copy was made from what was
        // stored. A double run stores a centreline and a gap and draws two rails either side of it - so copying
        // the centreline copied a line that is nowhere on the sheet and missed both lines that are. An arc stores
        // a centre and a radius and draws neither, so it was skipped for having no points at all. Which is why
        // the enlargement came out with pieces of the drawing missing.
        Document d;
        // A double run with a real arc fillet in it: a centreline that is drawn as two rails, plus a curve stored
        // as three numbers. Between them they cover both ways the copy was going wrong.
        Entity run;
        run.id = QStringLiteral("run");
        run.type = EntityType::DoubleLine;
        run.a = {0.0, 0.0};
        run.b = {40.0, 0.0};
        run.meta.insert(QStringLiteral("gap"), 4.0);
        d.entities().push_back(run);
        Entity arc;
        arc.id = QStringLiteral("bend");
        arc.type = EntityType::Arc;
        arc.center = {40.0, -10.0};
        arc.radius = 10.0;
        arc.startAngle = M_PI / 2.0;
        arc.endAngle = M_PI;
        d.entities().push_back(arc);
        Entity dv;
        dv.id = QStringLiteral("dv");
        dv.type = EntityType::DetailView;
        dv.center = {36.0, 0.0};
        dv.radius = 10.0;
        dv.b = {36.0, 80.0};
        dv.meta.insert(QStringLiteral("detailScale"), 3.0);
        d.entities().push_back(dv);
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.syncDetailCopies();

        int fromRun = 0, fromArc = 0;
        Bounds copied;
        for (const Entity& e : d.entities()) {
            if (e.meta.value(QStringLiteral("detailCopyOf")).toString() != QStringLiteral("dv")) continue;
            const Bounds b = e.bounds();
            if (b.valid) { copied.expand(b.min); copied.expand(b.max); }
            // The two rails sit either side of where the centreline went; the arc's pieces curve away from it.
            if (std::abs(b.center().x - 36.0 * 0.0) >= 0.0) ++fromRun;
        }
        std::printf("  all: %d piece(s) copied, spanning %.1f by %.1f\n", fromRun,
                    copied.valid ? copied.width() : 0.0, copied.valid ? copied.height() : 0.0);
        check(fromRun >= 3, "all: the two rails of the double run AND the arc are all in it");
        // Both rails: the copy has to reach either side of where the centreline was, by the gap times the scale.
        check(copied.valid && copied.height() > 4.0 * 3.0 * 0.9,
              "all: reaching either side of the centreline - both rails, not the line between them");
        // And the arc: it curves away downward, which nothing straight in this drawing does.
        bool curved = false;
        for (const Entity& e : d.entities()) {
            if (e.meta.value(QStringLiteral("detailCopyOf")).toString() != QStringLiteral("dv")) continue;
            const Vec2 dir = (e.b - e.a);
            if (std::abs(dir.x) > 1e-9 && std::abs(dir.y) > 1e-9) curved = true;
        }
        check(curved, "all: including the fillet, which stores no points at all and drew none in the copy");
        // Nothing outside the boundary comes with it: the run is 40 long and the boundary sees 20 of it.
        check(copied.valid && copied.width() <= 2.0 * 10.0 * 3.0 + 1.0,
              "all: and nothing from outside the boundary - it is cut at the circle");
    }

    // ---- a view that is not associative is a COPY, and the copy is its own --------------------------
    {
        // The switch decides what the enlargement IS. Associative, it is a window on the parent geometry: the
        // cursor reaches through it, and a change to either shows in both. Not associative, it is a COPY - made
        // of real objects, sitting in the view - so it can be drawn on, trimmed, moved and deleted with the tools
        // that already exist, and the parent does not move when it is.
        //
        // As a rendered picture it behaved like the associative kind in the hand: reaching through it edited the
        // parent, which is exactly what associative means and what "not associative but behaves like associative"
        // was.
        Document d;
        Entity l;
        l.id = QStringLiteral("thing");
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
        ed.detailScale() = 6.0;   // big enough on screen that its middle is clear of the pick tolerance
        ed.onPointerPress({80.0, 10.0}, Qt::LeftButton, Qt::NoModifier);
        const Entity* dv = nullptr;
        for (const Entity& e : d.entities())
            if (e.type == EntityType::DetailView) dv = &e;
        check(dv != nullptr, "copy: placed");
        if (!dv) return failures;
        const QString viewId = dv->id;
        check(!dv->meta.value(QStringLiteral("detailAnnotative")).toBool(),
              "copy: not associative unless it is asked for");

        auto copies = [&]() {
            std::vector<const Entity*> out;
            for (const Entity& e : d.entities())
                if (e.meta.value(QStringLiteral("detailCopyOf")).toString() == viewId) out.push_back(&e);
            return out;
        };
        auto pieces = copies();
        std::printf("  copy: %d real object(s) put in the view\n", int(pieces.size()));
        check(!pieces.empty(), "copy: the enlargement is made of real objects, not painted");
        // Enlarged, and put where the view is - not left on top of the parent.
        if (!pieces.empty()) {
            const Bounds cb = pieces.front()->bounds();
            const double scale = dv->meta.value(QStringLiteral("detailScale")).toDouble(2.0);
            std::printf("  copy: %.1f long where the parent was 4.0, centred near %.1f,%.1f\n", cb.width(),
                        cb.center().x, cb.center().y);
            check(std::abs(cb.width() - 4.0 * scale) < 0.5, "copy: enlarged by the scale factor");
            check(distance(cb.center(), Vec2{80.0, 10.0}) < 1.0, "copy: and set down in the view");
        }
        // THE COPY IS ITS OWN. Moving it does not move the parent, which is the whole difference.
        const Vec2 parentWas = l.a;
        for (Entity& e : d.entities())
            if (e.meta.value(QStringLiteral("detailCopyOf")).toString() == viewId) {
                e.a = e.a + Vec2{0.0, 5.0};
                e.b = e.b + Vec2{0.0, 5.0};
            }
        for (const Entity& e : d.entities())
            if (e.id == QStringLiteral("thing"))
                check(distance(e.a, parentWas) < 1e-9,
                      "copy: editing the copy leaves the parent exactly where it was");

        // And the cursor does NOT reach through it - there is nothing to reach through, the content is right
        // there. Reaching through is what made the two kinds behave alike.
        ed.clearSelection();
        ed.onPointerMove({80.0, 10.0});
        check(distance(ed.snappedCursor(), Vec2{80.0, 10.0}) < 1e-6,
              "copy: the cursor stays in the view - the objects there are the ones it acts on");

        // Made associative, the copy goes: the view draws itself, and a copy would be the same thing twice.
        for (Entity& e : d.entities())
            if (e.id == viewId) e.meta.insert(QStringLiteral("detailAnnotative"), true);
        ed.syncDetailCopies();
        std::printf("  copy: made associative, %d copied object(s) left\n", int(copies().size()));
        check(copies().empty(), "copy: made associative, the copy is taken away");
        // ...and now the cursor DOES reach through, because that is what associative is.
        ed.onPointerMove({92.0, 10.0});   // 12 right of the middle, at 6x = 2 in the drawing
        check(distance(ed.snappedCursor(), Vec2{12.0, 10.0}) < 1e-6,
              "copy: associative, it is a window again and the cursor reaches through it");

        // Made ordinary again, the copy comes back - of how the parent is NOW.
        for (Entity& e : d.entities())
            if (e.id == viewId) e.meta.insert(QStringLiteral("detailAnnotative"), false);
        ed.syncDetailCopies();
        check(!copies().empty(), "copy: made ordinary again, it is a copy again");
    }

    // ---- what an ordinary detail view actually DRAWS in its big circle ------------------------------
    {
        // The copy being taken is not the same thing as the copy being SHOWN. So this one paints the view and
        // counts the ink: an enlargement that draws nothing is an empty circle, and an empty circle is not "not
        // annotative", it is broken.
        Document d;
        Entity l;
        l.id = QStringLiteral("thing");
        l.type = EntityType::Line;
        l.a = {8.0, 10.0};
        l.b = {12.0, 10.0};
        d.entities().push_back(l);
        Entity dv;
        dv.id = QStringLiteral("dv");
        dv.type = EntityType::DetailView;
        dv.center = {10.0, 10.0};
        dv.radius = 4.0;
        dv.b = {60.0, 10.0};
        dv.meta.insert(QStringLiteral("detailScale"), 4.0);
        dv.meta.insert(QStringLiteral("detailTag"), QStringLiteral("A"));
        d.entities().push_back(dv);
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.syncDetailCopies();

        // Paint the sheet, then count the ink strictly INSIDE the enlargement - clear of its own ring and clear
        // of the caption under it, so what is counted can only be the view of the thing.
        EntityRenderer::setBackgroundColor(QColor(0, 0, 0));
        auto inkInside = [&](const Entity& view) {
            QImage img(420, 220, QImage::Format_RGB32);
            img.fill(Qt::black);
            ViewTransform vt;
            vt.view.scale = 2.0;
            vt.view.x = 20.0;
            vt.view.y = 120.0;
            {
                QPainter p(&img);
                for (const Entity& e : d.entities()) EntityRenderer::draw(p, e, d, vt, QColor());
            }
            const double scale = view.meta.value(QStringLiteral("detailScale")).toDouble(2.0);
            const QPointF c = vt.toScreen(view.b);
            const double rr = view.radius * scale * vt.scale() * 0.8;   // inside the ring, clear of it
            int lit = 0;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x) {
                    const double dx = x - c.x(), dy = y - c.y();
                    if (dx * dx + dy * dy > rr * rr) continue;
                    if (qGray(img.pixel(x, y)) > 40) ++lit;
                }
            return lit;
        };
        const Entity* view = nullptr;
        for (const Entity& e : d.entities())
            if (e.type == EntityType::DetailView) view = &e;
        check(view != nullptr, "draws: the view is there");
        // An ASSOCIATIVE view draws its own enlargement, out of the document, every time it is drawn. This
        // paints the sheet and counts the ink strictly inside it, because "it is tied to the parent" and "it
        // shows the parent" are not the same claim - a view that draws nothing is an empty circle.
        for (Entity& e : d.entities())
            if (e.id == QStringLiteral("dv")) e.meta.insert(QStringLiteral("detailAnnotative"), true);
        for (const Entity& e : d.entities())
            if (e.type == EntityType::DetailView) view = &e;
        if (view) {
            const int live = inkInside(*view);
            std::printf("  draws: associative, %d lit pixel(s) inside the enlargement\n", live);
            check(live > 0, "draws: an associative view draws the parent geometry inside it");
        }
        // The other kind draws nothing of its own there: what is in it is a copy made of real objects, drawn by
        // the same pass that draws everything else. Which is exactly what makes it editable.
        for (Entity& e : d.entities())
            if (e.id == QStringLiteral("dv")) e.meta.insert(QStringLiteral("detailAnnotative"), false);
        for (const Entity& e : d.entities())
            if (e.type == EntityType::DetailView) view = &e;
        // Asked with the copy taken away, so what is counted can only be what the VIEW itself paints.
        if (view) ed.clearDetailCopy(view->id);
        for (const Entity& e : d.entities())
            if (e.type == EntityType::DetailView) view = &e;
        if (view) {
            const int painted = inkInside(*view);
            std::printf("  draws: not associative, %d lit pixel(s) of its own\n", painted);
            check(painted == 0, "draws: a view that is not associative paints nothing - its content is real");
        }
        ed.syncDetailCopies();
        for (const Entity& e : d.entities())
            if (e.type == EntityType::DetailView) view = &e;
        if (view) {
            const int withCopy = inkInside(*view);
            std::printf("  draws: and with its copy made, %d lit pixel(s)\n", withCopy);
            check(withCopy > 0, "draws: and with the copy in it, there is something to see");
        }
    }


    if (failures == 0) std::printf("  -> ok\n");
    else std::printf("  -> %d failed\n", failures);
    return failures;
}
