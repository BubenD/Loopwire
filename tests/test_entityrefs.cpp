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

#include <QJsonArray>

#include <cmath>
#include <cstdio>

using namespace Edraw_Desktop;

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

    if (refFailures == 0) std::printf("  -> ok\n");
    else std::printf("  -> %d failed\n", refFailures);
    return refFailures;
}
