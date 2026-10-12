// Copies have to point at each other, not at the originals.
//
// The report was of "something strange with the polylines" after a move or a copy: long lines straight across
// the sheet. This is where they came from. A shield is DRAWN ACROSS the wires it names, a twist band clips the
// wires it names, a label sits on the wire it names - all of them ids in meta. Copy the harness and every copy
// still names the originals: the copied shield spans wires on the other side of the drawing, and what is
// drawn is a line from the copy to them.
#include "app/Editor.h"
#include "model/Document.h"
#include "model/EntityRefs.h"
#include "render/EntityRenderer.h"
#include "tools/EntityTransforms.h"

#include <QJsonArray>

#include <cmath>
#include <cstdio>

using namespace Loopwire;

namespace {

int refFailures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("  [FAIL] %s\n", what);
        ++refFailures;
    }
}

// A little harness: two wires, a shield across both, and a label on the first.
void buildHarness(Document& doc, double y) {
    Entity w1;
    w1.type = EntityType::Wire;
    w1.id = QStringLiteral("w1_%1").arg(y);
    w1.layer = QStringLiteral("0");
    w1.points = {Vec2{0.0, y}, Vec2{100.0, y}};
    Entity w2 = w1;
    w2.id = QStringLiteral("w2_%1").arg(y);
    w2.points = {Vec2{0.0, y + 10.0}, Vec2{100.0, y + 10.0}};

    Entity shield;
    shield.type = EntityType::Screen;
    shield.id = QStringLiteral("s_%1").arg(y);
    shield.layer = QStringLiteral("0");
    shield.points = {Vec2{50.0, y}, Vec2{50.0, y + 10.0}};
    QJsonArray wires;
    wires.append(w1.id);
    wires.append(w2.id);
    shield.meta.insert(QStringLiteral("wires"), wires);

    Entity label;
    label.type = EntityType::NetLabel;
    label.id = QStringLiteral("l_%1").arg(y);
    label.layer = QStringLiteral("0");
    label.a = Vec2{20.0, y};
    label.meta.insert(QStringLiteral("wireId"), w1.id);
    label.meta.insert(QStringLiteral("text"), QStringLiteral("A1"));

    doc.entities().push_back(w1);
    doc.entities().push_back(w2);
    doc.entities().push_back(shield);
    doc.entities().push_back(label);
}

} // namespace

int run_entityrefs_tests() {
    std::printf("entity reference tests:\n");

    // ---- the re-map itself -------------------------------------------------------------------------
    {
        Document doc;
        buildHarness(doc, 0.0);
        // The four of them copied, as a right-drag copy or a paste makes them.
        std::vector<Entity> copies;
        QHash<QString, QString> oldToNew;
        for (const Entity& e : doc.entities()) {
            Entity c = e;
            c.id = e.id + QStringLiteral("_copy");
            oldToNew.insert(e.id, c.id);
            copies.push_back(c);
        }
        remapEntityReferences(copies, oldToNew);

        bool shieldFixed = false, labelFixed = false;
        for (const Entity& c : copies) {
            if (c.type == EntityType::Screen) {
                const QJsonArray wires = c.meta.value(QStringLiteral("wires")).toArray();
                shieldFixed = wires.size() == 2 &&
                              wires[0].toString().endsWith(QStringLiteral("_copy")) &&
                              wires[1].toString().endsWith(QStringLiteral("_copy"));
            }
            if (c.type == EntityType::NetLabel)
                labelFixed =
                    c.meta.value(QStringLiteral("wireId")).toString().endsWith(QStringLiteral("_copy"));
        }
        check(shieldFixed,
              "remap: the copied shield catches the COPIED wires - naming the originals is what drew a line "
              "straight across the sheet from the copy to them");
        check(labelFixed, "remap: and the copied label sits on the copied wire");
    }

    // ---- what is left behind is dropped, not pointed at ---------------------------------------------
    {
        Document doc;
        buildHarness(doc, 0.0);
        // Only the shield and the label copied: the wires stay where they are.
        std::vector<Entity> copies;
        QHash<QString, QString> oldToNew;
        for (const Entity& e : doc.entities()) {
            if (e.type != EntityType::Screen && e.type != EntityType::NetLabel) continue;
            Entity c = e;
            c.id = e.id + QStringLiteral("_copy");
            oldToNew.insert(e.id, c.id);
            copies.push_back(c);
        }
        remapEntityReferences(copies, oldToNew);
        bool shieldEmptied = false, labelEmptied = false;
        for (const Entity& c : copies) {
            if (c.type == EntityType::Screen)
                shieldEmptied = !c.meta.contains(QStringLiteral("wires"));
            if (c.type == EntityType::NetLabel)
                labelEmptied = !c.meta.contains(QStringLiteral("wireId"));
        }
        check(shieldEmptied,
              "remap: a shield copied without its wires catches nothing - dropped rather than left pointing "
              "at somebody else's wires, because a shield that catches nothing draws nothing, which is "
              "honest and obvious");
        check(labelEmptied, "remap: and so does a label copied off its wire");
    }

    // ---- a block instance key is per copy, not shared -----------------------------------------------
    {
        std::vector<Entity> copies;
        for (int i = 0; i < 3; ++i) {
            Entity e;
            e.type = EntityType::Line;
            e.id = QStringLiteral("e%1").arg(i);
            e.meta.insert(QStringLiteral("blockInst"), QStringLiteral("theOriginal"));
            e.meta.insert(QStringLiteral("block"), QStringLiteral("A part (mechanical)"));
            copies.push_back(e);
        }
        remapEntityReferences(copies, {});
        const QString key = copies.front().meta.value(QStringLiteral("blockInst")).toString();
        check(!key.isEmpty() && key != QStringLiteral("theOriginal"),
              "block: a copy of a placed block is a NEW instance - two instances sharing one key are one "
              "instance to anything that counts or rebuilds them");
        bool oneKey = true;
        for (const Entity& c : copies)
            if (c.meta.value(QStringLiteral("blockInst")).toString() != key) oneKey = false;
        check(oneKey,
              "block: and it is ONE new instance for the whole copy, not one per line of its drawing");
    }

    // ---- through the editor: a right-drag copy of a harness -----------------------------------------
    {
        Document doc;
        buildHarness(doc, 0.0);
        Editor editor;
        editor.setDocument(&doc);
        editor.setTool(ToolId::Select);
        editor.selectAll();
        check(editor.selection().size() == 4, "editor: the harness is selected");
        // Picked up at one end of the wires and dropped 500 below.
        check(editor.beginRightDrag(Vec2{0.0, 0.0}), "editor: it can be dragged");
        editor.updateRightDrag(Vec2{0.0, -500.0});
        editor.commitRightDrag(/*copy=*/true);
        check(doc.entities().size() == 8, "editor: the copy is on the sheet");

        // The copied shield must catch the copied wires, and the copied label sit on the copied wire.
        const Entity* copiedShield = nullptr;
        for (const Entity& e : doc.entities())
            if (e.type == EntityType::Screen && !e.points.empty() && e.points.front().y < -100.0)
                copiedShield = &e;
        check(copiedShield != nullptr, "editor: the copied shield is there");
        if (copiedShield) {
            const QJsonArray wires = copiedShield->meta.value(QStringLiteral("wires")).toArray();
            bool allBelow = wires.size() == 2;
            for (const QJsonValue& v : wires) {
                const QString id = v.toString();
                const Entity* w = nullptr;
                for (const Entity& e : doc.entities())
                    if (e.id == id) w = &e;
                if (!w || w->points.empty() || w->points.front().y > -100.0) allBelow = false;
            }
            check(allBelow,
                  "editor: after a right-drag COPY the new shield catches the new wires - which is the bug "
                  "that drew long lines across the drawing");
            // And nothing on the sheet reaches from one harness to the other.
            Bounds all;
            for (const Entity& e : doc.entities()) {
                const Bounds b = drawnBounds(e);
                if (!b.valid) continue;
                check(b.height() < 200.0,
                      "editor: no object on the sheet reaches from the copy to the original - that reach IS "
                      "the stray line");
                all.expand(b.min);
                all.expand(b.max);
            }
            std::printf("  everything drawn spans %.1f in y (the two harnesses are 500 apart)\n",
                        all.valid ? all.height() : 0.0);
        }
    }

    // ---- every trim cap slot follows the object ----------------------------------------------------
    {
        // Each end of a double run can be cut TWICE - once per line of it - so there are four slots:
        // cutStart, cutStart2, cutEnd, cutEnd2. Only the first of each pair was being carried when the object
        // moved, and the second stayed at the coordinates it was cut at: the cap was then drawn from where the
        // run is now to where it used to be, straight across the sheet. That is the second ghost line, and it
        // was not a reference at all - it was geometry left behind.
        Entity run;
        run.type = EntityType::Polyline;
        run.id = QStringLiteral("run");
        run.layer = QStringLiteral("0");
        run.points = {Vec2{0.0, 0.0}, Vec2{100.0, 0.0}};
        run.meta.insert(QStringLiteral("double"), true);
        run.meta.insert(QStringLiteral("gap"), 4.0);
        struct Slot { const char* base; double ax, ay, bx, by; };
        const Slot capSlots[] = {   // not `slots`: Qt makes that word a macro
                              {"cutStart", 0.0, -10.0, 0.0, 10.0},
                              {"cutStart2", 1.0, -10.0, 1.0, 10.0},
                              {"cutEnd", 100.0, -10.0, 100.0, 10.0},
                              {"cutEnd2", 99.0, -10.0, 99.0, 10.0}};
        for (const Slot& s : capSlots) {
            const QString base = QString::fromLatin1(s.base);
            run.meta.insert(base, true);
            run.meta.insert(base + QStringLiteral("Ax"), s.ax);
            run.meta.insert(base + QStringLiteral("Ay"), s.ay);
            run.meta.insert(base + QStringLiteral("Bx"), s.bx);
            run.meta.insert(base + QStringLiteral("By"), s.by);
        }

        Entity moved = run;
        translateEntity(moved, Vec2{0.0, -500.0});
        bool allMoved = true;
        for (const Slot& s : capSlots) {
            const QString base = QString::fromLatin1(s.base);
            const double ay = moved.meta.value(base + QStringLiteral("Ay")).toDouble();
            const double by = moved.meta.value(base + QStringLiteral("By")).toDouble();
            if (std::abs(ay - (s.ay - 500.0)) > 1e-9 || std::abs(by - (s.by - 500.0)) > 1e-9)
                allMoved = false;
        }
        check(allMoved,
              "caps: all four trim cap slots travel with the object - the second slot of each end was the one "
              "left behind, and a cap left behind is a line from the object to where it used to be");

        // And what is DRAWN stays where the object is, which is the point of all of it.
        const Bounds capDrawn = drawnBounds(moved);
        check(capDrawn.valid && capDrawn.height() < 60.0,
              "caps: so what is drawn is the run and its caps, not a line reaching back 500 to where it was");
        std::printf("  a run with four cut slots, moved 500: drawn %.0f x %.0f\n",
                    capDrawn.valid ? capDrawn.width() : 0.0, capDrawn.valid ? capDrawn.height() : 0.0);

        // A scale carries them too, by the factor it scaled by.
        Entity scaled = run;
        scaleEntity(scaled, Vec2{0.0, 0.0}, 2.0);
        check(std::abs(scaled.meta.value(QStringLiteral("cutEnd2Ax")).toDouble() - 198.0) < 1e-9,
              "caps: and a scale takes them with it, by the factor it scaled by");
    }

    if (refFailures == 0) std::printf("  -> ok\n");
    else std::printf("  -> %d failed\n", refFailures);
    return refFailures;
}
