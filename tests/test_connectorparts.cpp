// Connectors and the contacts that go with them.
//
// A connector is never ordered by itself: it is ordered with its contacts, and a drawing that names the
// connector without them is a drawing nobody can build from. So the contacts are tied to the part in the
// library, one of them is chosen when the connector is placed, and the bill of materials puts it on the line
// below the connector in the quantity the connector needs. That chain is what is checked here - and, at the
// end, the two markings a connector carries and whether they are drawn.
#include "harness/Bom.h"
#include "io/Dwx.h"
#include "harness/Harness.h"
#include "harness/MechanicalHarness.h"
#include "harness/PartsLibrary.h"
#include "model/Block.h"
#include "model/Document.h"
#include "render/EntityRenderer.h"

#include <QImage>
#include <QPainter>

#include <cmath>
#include <cstdio>

using namespace Loopwire;

namespace {

int connFailures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("  [FAIL] %s\n", what);
        ++connFailures;
    }
}

// A library with one connector and two contacts to choose between.
PartsLibrary shopLibrary(QString& connectorId, QString& pinId, QString& socketId) {
    PartsLibrary lib;
    LibraryPart& conn = lib.add(PartKind::Connector);
    conn.description = QStringLiteral("Circular connector, 12 way");
    conn.mfrPn = QStringLiteral("CONN-12");
    conn.params.insert(QStringLiteral("pins"), QStringLiteral("12"));
    conn.params.insert(QStringLiteral("pitch"), QStringLiteral("3.5"));
    connectorId = conn.id;

    LibraryPart& pin = lib.add(PartKind::ContactPin);
    pin.description = QStringLiteral("Contact, pin, 20 AWG");
    pin.mfrPn = QStringLiteral("PIN-20");
    pin.unit = QStringLiteral("each");
    pinId = pin.id;

    LibraryPart& socket = lib.add(PartKind::ContactPin);
    socket.description = QStringLiteral("Contact, socket, 20 AWG");
    socket.mfrPn = QStringLiteral("SKT-20");
    socket.unit = QStringLiteral("each");
    socketId = socket.id;
    return lib;
}

// How much ink a rectangle of a rendered connector has: used to see whether a marking was drawn.
int inkOf(const Entity& e, const Document& doc) {
    QImage img(400, 300, QImage::Format_RGB32);
    img.fill(Qt::white);
    ViewTransform vt;
    vt.view.scale = 1.4;
    vt.view.x = 120.0;
    vt.view.y = 150.0;
    QPainter p(&img);
    EntityRenderer::draw(p, e, doc, vt);
    p.end();
    int ink = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (qRed(img.pixel(x, y)) < 200) ++ink;
    return ink;
}

} // namespace

int run_connectorparts_tests() {
    std::printf("connector parts tests:\n");

    QString connectorId, pinId, socketId;
    PartsLibrary lib = shopLibrary(connectorId, pinId, socketId);
    LibraryPart* conn = lib.byId(connectorId);
    check(conn != nullptr, "library: the connector is there");
    if (!conn) {
        std::printf("  -> %d failed\n", ++connFailures);
        return connFailures;
    }

    // ---- tying contacts on ------------------------------------------------------------------------
    check(conn->contactIds().isEmpty(), "tie: a connector starts with none tied");
    conn->setContactIds({pinId, socketId});
    check(conn->contactIds().size() == 2 && conn->contactIds().front() == pinId,
          "tie: more than one can be tied, and the order is the order they were tied in");
    conn->setContactIds({pinId, socketId, pinId, QString(), QStringLiteral("  ")});
    check(conn->contactIds().size() == 2,
          "tie: the same contact twice is once, and blanks are not contacts");
    check(conn->wayCount() == 12, "tie: the connector needs one contact per way, and says so");

    // The tie travels with the library, or it is not a property of the part at all.
    {
        PartsLibrary back;
        back.fromJson(lib.toJson());
        const LibraryPart* again = back.byId(connectorId);
        check(again && again->contactIds().size() == 2,
              "tie: the contacts tied to it are written to the file and read back");
    }

    // ---- placing it with a contact, and the bill of materials -------------------------------------
    {
        // A connector's mechanical drawing has to exist for it to be placed as a block, so it gets one.
        std::vector<Entity> body;
        Entity line;
        line.type = EntityType::Line;
        line.layer = QStringLiteral("0");
        line.a = Vec2{0.0, 0.0};
        line.b = Vec2{20.0, 0.0};
        body.push_back(line);
        conn->mechanical = partDrawingFromEntities(body);

        Document doc;
        std::vector<Entity> placed = partBlockInstance(*conn, /*mechanical=*/true, Vec2{0.0, 0.0});
        check(!placed.empty(), "place: the part goes down as a block");
        for (Entity& e : placed) e.meta.insert(QLatin1String(kContactPartKey), pinId);
        for (const Entity& e : placed) doc.entities().push_back(e);

        const std::vector<BomRow> rows = bomFromDrawing(doc, lib);
        std::printf("  placed with a contact: %zu rows\n", rows.size());
        check(rows.size() == 2, "bom: the connector is one line and its contact the next");
        if (rows.size() == 2) {
            check(rows[0].mfrPn == QStringLiteral("CONN-12") && std::abs(rows[0].qty - 1.0) < 1e-9,
                  "bom: the connector, one of it");
            check(rows[1].mfrPn == QStringLiteral("PIN-20"),
                  "bom: the contact chosen when it was placed, on the line BELOW the connector");
            check(std::abs(rows[1].qty - 12.0) < 1e-9,
                  "bom: twelve of them - one per way, which is how a connector is ordered");
            check(rows[1].ref == rows[0].ref,
                  "bom: against the same connector, so it is plain what the contacts are for");
        }

        // Two of the same connector on the sheet are two lines, not one - and two sets of contacts.
        std::vector<Entity> second = partBlockInstance(*conn, true, Vec2{100.0, 0.0});
        for (Entity& e : second) e.meta.insert(QLatin1String(kContactPartKey), socketId);
        for (const Entity& e : second) doc.entities().push_back(e);
        const std::vector<BomRow> two = bomFromDrawing(doc, lib);
        check(two.size() == 4,
              "bom: a second connector placed is a second pair of lines - each block instance counted "
              "once, however many lines its drawing is made of");
        bool sawSocket = false;
        for (const BomRow& r : two)
            if (r.mfrPn == QStringLiteral("SKT-20") && std::abs(r.qty - 12.0) < 1e-9) sawSocket = true;
        check(sawSocket, "bom: and the second one's own contact, which was a different choice");
    }

    // ---- placed with no contact -------------------------------------------------------------------
    {
        Document doc;
        for (const Entity& e : partBlockInstance(*conn, true, Vec2{0.0, 0.0}))
            doc.entities().push_back(e);
        const std::vector<BomRow> rows = bomFromDrawing(doc, lib);
        check(rows.size() == 1,
              "bom: placed on its own it is one line - the choice not to fit a contact is honoured");
    }

    // ---- a connector OBJECT placed with a contact --------------------------------------------------
    {
        Document doc;
        Entity e;
        e.type = EntityType::Connector;
        e.layer = QStringLiteral("0");
        e.a = Vec2{0.0, 0.0};
        ConnectorSpec spec = defaultConnectorSpec(9);
        spec.designator = QStringLiteral("X7");
        writeConnectorSpec(e.meta, spec);
        e.meta.insert(QStringLiteral("libPart"), connectorId);
        e.meta.insert(QLatin1String(kContactPartKey), socketId);
        doc.entities().push_back(e);
        const std::vector<BomRow> rows = bomFromDrawing(doc, lib);
        check(rows.size() == 2, "bom: a connector object with a contact is two lines too");
        if (rows.size() == 2) {
            check(rows[0].ref == QStringLiteral("X7"), "bom: under its designator");
            check(rows[1].mfrPn == QStringLiteral("SKT-20") && std::abs(rows[1].qty - 12.0) < 1e-9,
                  "bom: with a contact per way of the part it names");
        }
    }

    // ---- the schematic symbol, from the part's own data --------------------------------------------
    {
        const QJsonArray made = connectorSymbolFromData(*conn);
        check(!made.isEmpty(), "symbol: a connector's symbol can be built from its data");
        const std::vector<Entity> ents = partDrawingEntities(made);
        check(ents.size() == 1 && ents.front().type == EntityType::Connector,
              "symbol: and it comes out as a real connector object, not as loose lines - so it can be "
              "wired and edited after it is placed");
        if (!ents.empty()) {
            const ConnectorSpec spec = connectorSpecFromMeta(ents.front().meta);
            check(spec.pinCount == 12, "symbol: of the part's own way count");
            check(spec.partNumber == QStringLiteral("CONN-12"), "symbol: lettered with its part number");
            check(spec.designator.isEmpty(),
                  "symbol: and with no designator - that belongs to where it is used, not to the part");
            check(std::abs(spec.pitch - 3.5) < 1e-9,
                  "symbol: at the pitch the data gives, so it reads against the datasheet");
        }
        LibraryPart wire;
        wire.kind = PartKind::Wire;
        check(connectorSymbolFromData(wire).isEmpty(),
              "symbol: nothing is invented for a part that is not a connector");
    }

    // ---- the two markings, and whether they are drawn ---------------------------------------------
    {
        Document doc;
        Entity e;
        e.type = EntityType::Connector;
        e.layer = QStringLiteral("0");
        e.a = Vec2{0.0, 0.0};
        ConnectorSpec spec = defaultConnectorSpec(4);
        spec.designator = QStringLiteral("X12");
        spec.partNumber = QStringLiteral("CONN-12");
        writeConnectorSpec(e.meta, spec);
        check(connectorSpecFromMeta(e.meta).showDesignator &&
                  connectorSpecFromMeta(e.meta).showPartNumber,
              "eye: both markings are drawn until told otherwise");

        const int bothShown = inkOf(e, doc);
        spec.showDesignator = false;
        writeConnectorSpec(e.meta, spec);
        const int noDesignator = inkOf(e, doc);
        spec.showPartNumber = false;
        writeConnectorSpec(e.meta, spec);
        const int neither = inkOf(e, doc);
        std::printf("  ink: both %d, without the designator %d, without either %d\n", bothShown,
                    noDesignator, neither);
        check(noDesignator < bothShown, "eye: hiding the designator takes it off the drawing");
        check(neither < noDesignator, "eye: and hiding the part number takes that off too");
        check(connectorSpecFromMeta(e.meta).designator == QStringLiteral("X12"),
              "eye: the text itself is kept - the eye hides it, it does not delete it");
        // And the flags survive the file.
        Document round;
        round.entities().push_back(e);
        Document back;
        dwx::fromJson(back, dwx::toJson(round));
        check(!back.entities().empty() &&
                  !connectorSpecFromMeta(back.entities().front().meta).showDesignator,
              "eye: what is hidden stays hidden when the drawing is saved and opened again");
    }

    if (connFailures == 0) std::printf("  -> ok\n");
    else std::printf("  -> %d failed\n", connFailures);
    return connFailures;
}
