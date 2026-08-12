// Using the library: the fanout as a third drawing of a part, the one contact a connector is used with, and
// the library at the top of the canvas menu.
//
// The fanout is a third drawing because it is a third thing - the mechanical view is the connector's body,
// the schematic is its circuit, and the fanout is what the harness DOES at it - and it is drawn for one side
// of the connector and placed mirrored for the other. That mirroring is the part worth measuring: a fanout
// put down for the wrong side is a harness built the wrong way round.
#include "app/MainWindow.h"
#include "harness/PartsLibrary.h"
#include "model/Block.h"
#include "model/Document.h"

#include <QMenu>

#include <cmath>
#include <cstdio>

using namespace Edraw_Desktop;

namespace {

int useFailures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("  [FAIL] %s\n", what);
        ++useFailures;
    }
}

bool near(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }

} // namespace

int run_libraryuse_tests() {
    std::printf("library use tests:\n");

    // ---- the fanout: a drawing of its own, with a side ---------------------------------------------
    // Every part added FIRST, and then reached by id. lib.add() returns a reference into a vector, and a
    // later add can move that vector: holding on to the reference across one is holding a dangling
    // reference, which is exactly the mistake this test made on its first run.
    PartsLibrary lib;
    const QString connId = lib.add(PartKind::Connector).id;
    const QString pinId = lib.add(PartKind::ContactPin).id;
    const QString socketId = lib.add(PartKind::ContactPin).id;
    lib.byId(pinId)->mfrPn = QStringLiteral("PIN-20");
    lib.byId(socketId)->mfrPn = QStringLiteral("SKT-20");
    LibraryPart& conn = *lib.byId(connId);
    conn.description = QStringLiteral("Connector, 9 way");
    conn.mfrPn = QStringLiteral("CONN-9");
    conn.params.insert(QStringLiteral("pins"), QStringLiteral("9"));

    check(!conn.hasFanout(), "fanout: a part starts without one");
    check(conn.fanoutSide() == QStringLiteral("front"),
          "fanout: and is taken to be drawn from the front until it says otherwise");
    conn.setFanoutSide(QStringLiteral("back"));
    check(conn.fanoutSide() == QStringLiteral("back"), "fanout: the side it was drawn from is kept");
    conn.setFanoutSide(QStringLiteral("nonsense"));
    check(conn.fanoutSide() == QStringLiteral("front"),
          "fanout: and anything that is not a side reads as the front, rather than as itself");

    // A fanout: three wires leaving upward from the origin, so mirroring is visible in the numbers.
    std::vector<Entity> wires;
    for (int i = 0; i < 3; ++i) {
        Entity e;
        e.type = EntityType::Line;
        e.layer = QStringLiteral("0");
        e.a = Vec2{0.0, 0.0};
        e.b = Vec2{40.0, 10.0 + 10.0 * i};
        wires.push_back(e);
    }
    conn.fanout = partDrawingFromEntities(wires);
    check(conn.hasFanout(), "fanout: captured, the part has one");

    // It travels with the library, or it is not part of the part.
    {
        PartsLibrary back;
        back.fromJson(lib.toJson());
        const LibraryPart* again = back.byId(connId);
        check(again && again->hasFanout() && again->fanout.size() == conn.fanout.size(),
              "fanout: written to the library file and read back");
        check(again && again->fanoutSide() == QStringLiteral("front"),
              "fanout: and so is the side it was drawn from");
    }

    // ---- placed as drawn, and mirrored for the other side -----------------------------------------
    {
        const std::vector<Entity> asDrawn = partDrawingBlockInstance(
            conn, conn.fanout, QStringLiteral("fanout"), Vec2{100.0, 50.0}, /*mirrorY=*/false);
        check(asDrawn.size() == 3, "place: the fanout goes down as its three wires");
        bool up = !asDrawn.empty();
        for (const Entity& e : asDrawn)
            if (e.b.y <= e.a.y) up = false;
        check(up, "place: as drawn, the wires leave upward");
        bool stamped = !asDrawn.empty();
        for (const Entity& e : asDrawn) {
            if (e.meta.value(QStringLiteral("libPart")).toString() != conn.id) stamped = false;
            if (!e.meta.value(QStringLiteral("block")).toString().contains(QStringLiteral("fanout")))
                stamped = false;
        }
        check(stamped,
              "place: every line of it says which part it is and which block - so the bill of materials "
              "counts it and editing the part rebuilds it");

        const std::vector<Entity> mirrored = partDrawingBlockInstance(
            conn, conn.fanout, QStringLiteral("fanout"), Vec2{100.0, 50.0}, /*mirrorY=*/true);
        check(mirrored.size() == 3, "mirror: the same three wires");
        bool down = !mirrored.empty();
        for (const Entity& e : mirrored)
            if (e.b.y >= e.a.y) down = false;
        check(down,
              "mirror: they leave downward instead - which is the same fanout seen from the other side of "
              "the connector");
        // Mirrored about the X axis THROUGH WHERE IT WAS PLACED, so it stays where it was put.
        if (!mirrored.empty() && !asDrawn.empty()) {
            check(near(mirrored.front().a.x, asDrawn.front().a.x) &&
                      near(mirrored.front().a.y, asDrawn.front().a.y),
                  "mirror: about the point it was placed at, so it does not move off the spot");
            check(near(mirrored.front().b.x, asDrawn.front().b.x),
                  "mirror: across the wires only - the length along them is unchanged");
        }
        bool named = !mirrored.empty();
        for (const Entity& e : mirrored)
            if (!e.meta.value(QStringLiteral("block")).toString().contains(QStringLiteral("mirrored")))
                named = false;
        check(named,
              "mirror: and it is its own block - a mirrored fanout is not the same block as the one it was "
              "mirrored from, or rebuilding one would flip the other");
    }

    // ---- one contact, for both drawings ------------------------------------------------------------
    {
        LibraryPart* c = lib.byId(connId);
        check(c && c->contactId().isEmpty(), "contact: none to start with");
        c->setContactId(pinId);
        check(c->contactId() == pinId, "contact: the one it is used with is kept");
        check(c->contactIds().size() == 1,
              "contact: and it is ONE - a connector is built with one kind of contact in it, and the "
              "choice belongs to the connector rather than to the moment it is placed");
        c->setContactId(socketId);
        check(c->contactId() == socketId && c->contactIds().size() == 1,
              "contact: choosing another replaces it rather than adding to it");
        c->setContactId(QString());
        check(c->contactId().isEmpty() && c->contactIds().isEmpty(),
              "contact: and it can be taken off again");
    }

    // ---- the tie, from either end ------------------------------------------------------------------
    {
        // One tie, two ends. Tied from the connector, the contact says what it goes in; tied from the
        // contact, the connector says what goes in it. Anything else means opening a contact on its own and
        // being told nothing.
        PartsLibrary two;
        const QString cId = two.add(PartKind::Connector).id;
        const QString pId = two.add(PartKind::ContactPin).id;
        two.byId(cId)->mfrPn = QStringLiteral("CONN-9");
        two.byId(pId)->mfrPn = QStringLiteral("PIN-20");

        tieContactToConnector(two, cId, pId);
        check(two.byId(cId)->contactId() == pId, "tie: the connector says which contact goes in it");
        check(two.byId(pId)->connectorId() == cId,
              "tie: and the CONTACT says which connector it goes in - the same tie, written at both ends");
        check(tiedContactsOf(two, cId) == QStringList{pId} &&
                  tiedConnectorsOf(two, pId) == QStringList{cId},
              "tie: and it reads the same way round from either end");

        // Tied from the contact's end instead: the same call, the other way about.
        const QString c2 = two.add(PartKind::Connector).id;
        two.byId(c2)->mfrPn = QStringLiteral("CONN-37");
        tieContactToConnector(two, c2, pId);
        check(two.byId(c2)->contactId() == pId,
              "tie: a second connector can use the same contact");
        check(two.byId(pId)->connectorIds().size() == 2,
              "tie: and the contact knows both of them - a contact goes in as many connectors as it fits");

        // Untying at one end unties at both.
        tieContactToConnector(two, cId, QString());
        check(two.byId(cId)->contactId().isEmpty(), "untie: the connector forgets the contact");
        check(!two.byId(pId)->connectorIds().contains(cId),
              "untie: and the contact forgets the connector - a tie off at one end is off at both");
        check(two.byId(pId)->connectorIds().contains(c2),
              "untie: the other connector's tie is untouched");

        // A tie written before both ends were kept - the connector's side only - still shows on the contact.
        PartsLibrary legacy;
        const QString lc = legacy.add(PartKind::Connector).id;
        const QString lp = legacy.add(PartKind::ContactPin).id;
        legacy.byId(lc)->setContactId(lp);   // as an older version wrote it
        check(tiedConnectorsOf(legacy, lp) == QStringList{lc},
              "tie: worked out from both ends, so a tie written on one part only still shows on the other");
    }

    // ---- the library at the top of the canvas menu -------------------------------------------------
    {
        MainWindow win;
        QMenu menu;
        win.addLibraryToCanvasMenu(menu);
        const QList<QAction*> actions = menu.actions();
        check(!actions.isEmpty(), "menu: something was added");
        if (!actions.isEmpty()) {
            check(actions.first()->menu() != nullptr &&
                      actions.first()->text() == QStringLiteral("Library"),
                  "menu: the library is the FIRST thing on the canvas menu - putting a part on the sheet is "
                  "the commonest thing done from it");
            QMenu* root = actions.first()->menu();
            QStringList entries;
            for (QAction* a : root->actions())
                if (!a->isSeparator()) entries << a->text();
            std::printf("  library menu: %s\n", entries.join(QStringLiteral(" / ")).toUtf8().constData());
            check(entries.filter(QStringLiteral("Find a part")).size() == 1 ||
                      entries.filter(QStringLiteral("nothing in the library")).size() == 1,
                  "menu: a search for a library too long to walk through - or a plain word that there is "
                  "nothing drawn to place");
            check(entries.filter(QStringLiteral("Open the parts library")).size() == 1,
                  "menu: and the way through to the library itself");
            check(actions.size() > 1 && actions.at(1)->isSeparator(),
                  "menu: with a rule under it, so what follows reads as something else");
        }
    }

    if (useFailures == 0) std::printf("  -> ok\n");
    else std::printf("  -> %d failed\n", useFailures);
    return useFailures;
}
