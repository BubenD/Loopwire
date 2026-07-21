#include "app/Editor.h"
#include "geometry/Snap.h"
#include "harness/Harness.h"
#include "geometry/Geometry.h"
#include "model/Document.h"
#include "tools/EntityTransforms.h"

#include <QJsonArray>
#include <cmath>
#include <cstdio>

using namespace Edraw_Desktop;

namespace {
int g_fail = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("  [FAIL] %s\n", what); ++g_fail; }
}
bool near(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }
} // namespace

int run_harness_tests() {
    g_fail = 0;
    std::printf("harness tests:\n");

    // Wire color codes resolve to hex; unknown -> fallback.
    check(wireColorHex("RD") == "#CC2222", "wire color RD");
    check(wireColorHex("GNYE") == "#99CC00", "wire color GNYE");
    check(wireColorHex("#123456") == "#123456", "wire color passthrough hex");
    check(near(wireGaugeWeight("2.5"), 3.0), "gauge weight 2.5");

    // Connector geometry: pin count, exit direction.
    {
        ConnectorSpec s = defaultConnectorSpec(6);
        s.rectWidth = 40;
        ConnectorGeometry g = connectorGeometry({0, 0}, s);
        check(g.pinTip.size() == 6 && g.pinEnd.size() == 6, "connector: 6 pins");
        // Default exit is to the right: pin end x > tip x.
        check(g.pinEnd[0].x > g.pinTip[0].x, "connector: pins exit right");
        s.mirrorX = true;
        ConnectorGeometry gm = connectorGeometry({0, 0}, s);
        check(gm.pinEnd[0].x < gm.pinTip[0].x, "connector: mirrored exit left");
    }

    // Meta round-trip.
    {
        ConnectorSpec s = defaultConnectorSpec(3);
        s.designator = "X7";
        s.partNumber = "PN-3";
        QJsonObject meta;
        writeConnectorSpec(meta, s);
        ConnectorSpec back = connectorSpecFromMeta(meta);
        check(back.designator == "X7" && back.partNumber == "PN-3" && back.pinCount == 3,
              "connector: meta round-trip");
    }

    // Pin label annotation (numbers/letters) + order (top/bottom).
    {
        ConnectorSpec s = defaultConnectorSpec(4);
        check(s.pins.size() == 4 && s.pins[0] == "1" && s.pins[3] == "4",
              "connector: default labels are numbers 1..N top-down");
        s.annotation = "letters"; regenPinLabels(s);
        check(s.pins[0] == "A" && s.pins[3] == "D", "connector: letters annotation A..D");
        s.annotation = "numbers"; s.pinOrder = "bottom"; regenPinLabels(s);
        check(s.pins[0] == "4" && s.pins[3] == "1", "connector: bottom order reverses numbering");
    }

    // mirrorY flips the pin vertical order (reflect across the X axis).
    {
        ConnectorSpec s = defaultConnectorSpec(4);
        s.pitch = 10;
        ConnectorGeometry up = connectorGeometry({0, 0}, s);
        s.mirrorY = true;
        ConnectorGeometry dn = connectorGeometry({0, 0}, s);
        // Pin 0 and pin 3 swap Y positions when mirrored.
        check(near(dn.pinTip[0].y, up.pinTip[3].y) && near(dn.pinTip[3].y, up.pinTip[0].y),
              "connector: mirrorY flips pin order vertically");
    }

    // Movable text offsets shift the label positions (relative to the connector).
    {
        ConnectorSpec s = defaultConnectorSpec(2);
        s.designator = "J9"; s.partNumber = "PN9";
        ConnectorGeometry base = connectorGeometry({0, 0}, s);
        s.desTextDX = 5; s.desTextDY = 3;
        s.pnTextDX = -4; s.pnTextDY = 2;
        s.pinTextDX = 1; s.pinTextDY = -1;
        ConnectorGeometry off = connectorGeometry({0, 0}, s);
        check(near(off.designatorPos.x, base.designatorPos.x + 5) &&
                  near(off.designatorPos.y, base.designatorPos.y + 3),
              "connector: designator text offset applied");
        check(near(off.partPos.x, base.partPos.x - 4) && near(off.partPos.y, base.partPos.y + 2),
              "connector: part-number text offset applied");
        check(near(off.pinLabelPos[0].x, base.pinLabelPos[0].x + 1) &&
                  near(off.pinLabelPos[0].y, base.pinLabelPos[0].y - 1),
              "connector: pin-label text offset applied to every pin");
        // Round-trips through meta.
        QJsonObject meta; writeConnectorSpec(meta, s);
        ConnectorSpec b = connectorSpecFromMeta(meta);
        check(near(b.desTextDX, 5) && near(b.pnTextDY, 2) && near(b.pinTextDX, 1),
              "connector: text offsets round-trip through meta");
    }

    // Body mode: dynamic auto-fits (width 40); custom honours W/H.
    {
        ConnectorSpec s = defaultConnectorSpec(4);
        s.rectMode = "dynamic"; s.rectWidth = 999; // width ignored in dynamic
        ConnectorGeometry gd = connectorGeometry({0, 0}, s);
        check(near(gd.effectiveWidth, 40.0), "connector: dynamic body is 40 wide");
        s.rectMode = "custom"; s.rectWidth = 60; s.rectHeight = 120;
        ConnectorGeometry gc = connectorGeometry({0, 0}, s);
        check(near(gc.effectiveWidth, 60.0) && near(gc.effectiveHeight, 120.0),
              "connector: custom body honours W/H");
    }

    // Dynamic body enlarges to keep the pin text in proportion: wider body + pin
    // spacing at least the pin text size.
    {
        ConnectorSpec s = defaultConnectorSpec(4);
        s.rectMode = "dynamic"; s.pitch = 20; s.pinSize = 50; s.textSize = 50;
        ConnectorGeometry g = connectorGeometry({0, 0}, s);
        check(g.effectiveWidth > 100.0, "connector: dynamic body widens for large pin text");
        const double sp = std::abs(g.pinTip[0].y - g.pinTip[1].y);
        check(sp >= 50.0 - 1e-6, "connector: dynamic pin spacing grows to fit large text");
    }

    // New build options survive the meta round-trip.
    {
        ConnectorSpec s = defaultConnectorSpec(5);
        s.annotation = "letters"; s.pinOrder = "bottom"; s.rectMode = "custom";
        s.associative = false; s.pinSize = 7; s.desSize = 12; s.pnSize = 9;
        s.pinFont = "Arial";
        QJsonObject meta; writeConnectorSpec(meta, s);
        ConnectorSpec b = connectorSpecFromMeta(meta);
        check(b.annotation == "letters" && b.pinOrder == "bottom" && b.rectMode == "custom" &&
                  !b.associative && near(b.pinSize, 7) && near(b.desSize, 12) &&
                  near(b.pnSize, 9) && b.pinFont == "Arial",
              "connector: build options round-trip through meta");
    }

    // Editor places connector + wire, BOM reflects them.
    {
        Document doc;
        doc.entities().clear();
        Editor ed;
        ed.setDocument(&doc);
        ed.snapSettings().objectSnap = false;
        ed.connectorSpec().designator = "J1";
        ed.connectorSpec().partNumber = "ACME-9";
        ed.setTool(ToolId::Connector);
        ed.armConnectorPlacement(); // in Connector mode the 1st click starts the preview;
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier); // armed -> this click places
        check(doc.entities().size() == 1 &&
                  doc.entities()[0].type == EntityType::Connector,
              "editor: connector placed");

        ed.wireColor() = "BU";
        ed.wireGauge() = "1.0";
        ed.wireNet() = "CAN_H";
        ed.setTool(ToolId::Wire);
        ed.onPointerPress({10, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({30, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onKey(Qt::Key_Return, Qt::NoModifier);
        bool hasWire = false;
        for (const auto& e : doc.entities())
            if (e.type == EntityType::Wire) hasWire = true;
        check(hasWire, "editor: wire created");

        const auto bom = ed.buildBom();
        check(bom.size() == 2, "bom: 2 rows");
        bool conn = false, wire = false;
        for (const auto& r : bom) {
            if (r.kind == "Connector" && r.ref == "J1") conn = true;
            if (r.kind == "Wire" && r.ref == "CAN_H") wire = true;
        }
        check(conn && wire, "bom: connector + wire rows");
    }

    // Non-associative connector explodes to primitives on placement.
    {
        Document doc; doc.entities().clear();
        Editor ed; ed.setDocument(&doc);
        ed.snapSettings().objectSnap = false;
        ed.connectorSpec() = defaultConnectorSpec(3);
        ed.connectorSpec().associative = false;
        ed.setTool(ToolId::Connector);
        ed.armConnectorPlacement(); // in Connector mode the 1st click starts the preview;
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier); // armed -> this click places
        bool anyConnector = false, anyLine = false, anyText = false;
        for (const auto& e : doc.entities()) {
            if (e.type == EntityType::Connector) anyConnector = true;
            if (e.type == EntityType::Line) anyLine = true;
            if (e.type == EntityType::Text) anyText = true;
        }
        check(!anyConnector && anyLine && anyText && doc.entities().size() > 3,
              "editor: non-associative connector placed as loose primitives");
    }

    // A crossing window that merely touches the connector body selects it.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        ed.connectorSpec() = defaultConnectorSpec(5);
        ed.setTool(ToolId::Connector);
        ed.armConnectorPlacement();
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier); // resume from freeze? no -> places
        // armConnectorPlacement doesn't freeze; the first click places at (0,0).
        const Entity* conn = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Connector) conn = &e;
        check(conn, "conn-select: connector placed");
        const ConnectorGeometry g = connectorGeometry(conn->a, connectorSpecFromMeta(conn->meta));
        // Crossing box (right->left) overlapping the lower-right of the body only.
        const Vec2 mid{(g.rectMin.x + g.rectMax.x) / 2.0, (g.rectMin.y + g.rectMax.y) / 2.0};
        ed.setTool(ToolId::Select);
        ed.onPointerPress({g.rectMax.x + 5, mid.y}, Qt::LeftButton, Qt::NoModifier); // 1st corner (right)
        ed.onPointerMove({mid.x, g.rectMin.y - 5});
        ed.onPointerPress({mid.x, g.rectMin.y - 5}, Qt::LeftButton, Qt::NoModifier);  // 2nd corner (left) -> crossing
        check(ed.selection().size() == 1 && ed.selection().front() == conn->id,
              "conn-select: a crossing window touching the body selects the connector");
    }

    // Object snap finds an associative connector's pin ends + rectangle corners.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d);
        ed.snapSettings().objectSnap = false; // deterministic placement
        ed.connectorSpec() = defaultConnectorSpec(4);
        ed.setTool(ToolId::Connector);
        ed.armConnectorPlacement(); // arm so the click places (1st click otherwise starts preview)
        ed.onPointerPress({20, 20}, Qt::LeftButton, Qt::NoModifier);
        const Entity* conn = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Connector) conn = &e;
        check(conn, "conn-snap: connector placed");
        const ConnectorGeometry g = connectorGeometry(conn->a, connectorSpecFromMeta(conn->meta));
        SnapSettings s; // objectSnap + endpoint on by default
        const Vec2 pe = g.pinEnd[0];
        auto rp = resolveSnap(d, {pe.x + 1, pe.y + 1}, std::nullopt, 5.0, s, {});
        check(rp.snapped && near(rp.point.x, pe.x) && near(rp.point.y, pe.y),
              "conn-snap: snaps to a pin end");
        const Vec2 corner = g.rectMin; // left corner (pin-free side) — unambiguous
        auto rc = resolveSnap(d, {corner.x - 1, corner.y - 1}, std::nullopt, 5.0, s, {});
        check(rc.snapped && near(rc.point.x, corner.x) && near(rc.point.y, corner.y),
              "conn-snap: snaps to a rectangle corner");
    }

    // The placement preview arms only after "Place connector", not when the tool is picked.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d);
        ed.setTool(ToolId::Connector);
        check(!ed.connectorArmed(), "conn-arm: picking the Connector tool does NOT show the preview");
        ed.armConnectorPlacement();
        check(ed.connectorArmed() && ed.tool() == ToolId::Connector,
              "conn-arm: Place connector arms the preview");
        ed.setTool(ToolId::Select);
        check(!ed.connectorArmed(), "conn-arm: leaving the tool clears the preview");
    }

    // Tab freezes the armed preview; a canvas click re-attaches it (doesn't place).
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        ed.connectorSpec() = defaultConnectorSpec(3);
        ed.armConnectorPlacement();
        ed.onPointerMove({50, 50}); // preview follows the cursor
        ed.freezeConnectorPreview();
        check(ed.connectorFrozen(), "conn-freeze: Tab freezes the preview");
        // A click while frozen re-attaches to the cursor instead of placing.
        ed.onPointerPress({80, 40}, Qt::LeftButton, Qt::NoModifier);
        bool anyConn = false;
        for (const auto& e : d.entities()) if (e.type == EntityType::Connector) anyConn = true;
        check(!ed.connectorFrozen() && !anyConn && ed.connectorArmed(),
              "conn-freeze: click resumes the preview (no connector placed yet)");
        // The next click places it.
        ed.onPointerPress({80, 40}, Qt::LeftButton, Qt::NoModifier);
        for (const auto& e : d.entities()) if (e.type == EntityType::Connector) anyConn = true;
        check(anyConn, "conn-freeze: a click after resuming places the connector");
    }

    // The connector's empty interior is not pickable; the border is.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        ed.connectorSpec() = defaultConnectorSpec(4);
        ed.setTool(ToolId::Connector);
        ed.armConnectorPlacement(); // in Connector mode the 1st click starts the preview;
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier); // armed -> this click places
        const Entity* c = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Connector) c = &e;
        check(c, "conn-interior: connector placed");
        const ConnectorGeometry g = connectorGeometry(c->a, connectorSpecFromMeta(c->meta));
        const QString id = c->id;
        ed.setTool(ToolId::Select);
        const Vec2 mid{(g.rectMin.x + g.rectMax.x) / 2.0, (g.rectMin.y + g.rectMax.y) / 2.0};
        ed.onPointerPress(mid, Qt::LeftButton, Qt::NoModifier); // empty inside -> region, no pick
        check(ed.selection().empty(),
              "conn-interior: clicking the empty inside does not select the connector");
        ed.onKey(Qt::Key_Escape, Qt::NoModifier);
        const Vec2 topMid{(g.rectMin.x + g.rectMax.x) / 2.0, g.rectMax.y}; // on the top edge
        ed.onPointerPress(topMid, Qt::LeftButton, Qt::NoModifier);
        check(ed.selection().size() == 1 && ed.selection().front() == id,
              "conn-interior: clicking the border selects the connector");
    }

    // Editing a pin label writes it back to the connector's meta.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        ed.connectorSpec() = defaultConnectorSpec(3);
        ed.setTool(ToolId::Connector);
        ed.armConnectorPlacement(); // in Connector mode the 1st click starts the preview;
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier); // armed -> this click places
        QString id;
        for (const auto& e : d.entities()) if (e.type == EntityType::Connector) id = e.id;
        ed.applyConnectorPinLabel(id, 1, QStringLiteral("K9"), QStringLiteral("Consolas"));
        const Entity* c = nullptr;
        for (const auto& e : d.entities()) if (e.id == id) c = &e;
        check(c && c->meta.value("pins").toArray()[1].toString() == QLatin1String("K9"),
              "conn-pin: applyConnectorPinLabel sets the pin's text");
        check(c && c->meta.value("pinFont").toString() == QLatin1String("Consolas"),
              "conn-pin: a chosen font from the library applies to the pin text");
    }

    // A wire on a connector pin follows the connector when it moves; a wire off the
    // pin (disconnected) does not.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        ed.connectorSpec() = defaultConnectorSpec(4);
        ed.setTool(ToolId::Connector);
        ed.armConnectorPlacement();
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        const Entity* conn = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Connector) conn = &e;
        const QString cid = conn->id;
        const ConnectorGeometry g = connectorGeometry(conn->a, connectorSpecFromMeta(conn->meta));
        const Vec2 pe = g.pinEnd[0];
        Entity w; w.id = "w1"; w.type = EntityType::Wire; w.points = {pe, {pe.x + 50, pe.y}};
        d.entities().push_back(w);
        Entity fw; fw.id = "w2"; fw.type = EntityType::Wire; fw.points = {{300, 300}, {360, 300}};
        d.entities().push_back(fw);
        // Move the connector by (0,-30) via the Move tool.
        ed.setTool(ToolId::Select);
        ed.selectById(cid);
        ed.setTool(ToolId::Move);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({0, -30}, Qt::LeftButton, Qt::NoModifier);
        auto wire = [&](const char* id) -> const Entity* {
            for (const auto& e : d.entities()) if (e.id == id) return &e;
            return nullptr;
        };
        check(wire("w1") && near(wire("w1")->points[0].x, pe.x) &&
                  near(wire("w1")->points[0].y, pe.y - 30),
              "wire-follow: a wire on a pin follows the connector move");
        check(wire("w2") && near(wire("w2")->points[0].x, 300),
              "wire-follow: an unconnected wire does not move");
        // Disconnect: move w1's start off the pin, then move the connector again — the
        // wire no longer follows (positional connection is broken).
        const Vec2 movedPin{pe.x, pe.y - 30};
        ed.setTool(ToolId::Select);
        ed.selectById("w1");
        ed.setTool(ToolId::Move);
        ed.onPointerPress({movedPin.x, movedPin.y}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({movedPin.x + 20, movedPin.y}, Qt::LeftButton, Qt::NoModifier); // pull off pin
        const double afterPullX = wire("w1")->points[0].x;
        ed.setTool(ToolId::Select);
        ed.selectById(cid);
        ed.setTool(ToolId::Move);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({0, -10}, Qt::LeftButton, Qt::NoModifier); // move connector again
        check(near(wire("w1")->points[0].x, afterPullX),
              "wire-disconnect: a wire pulled off the pin no longer follows the connector");
    }

    // Mirroring a connector across a vertical line flips its pin direction (mirrorX)
    // and reflects the body; across a horizontal line flips the pin order (mirrorY).
    {
        Entity c; c.type = EntityType::Connector; c.a = {0, 0};
        ConnectorSpec s = defaultConnectorSpec(4); s.mirrorX = false; s.mirrorY = false;
        writeConnectorSpec(c.meta, s);
        const ConnectorGeometry g0 = connectorGeometry(c.a, connectorSpecFromMeta(c.meta));
        Entity v = c;
        mirrorEntity(v, {100, 0}, {100, 10}); // vertical mirror line at x=100
        const ConnectorSpec sv = connectorSpecFromMeta(v.meta);
        const ConnectorGeometry gv = connectorGeometry(v.a, sv);
        check(sv.mirrorX && !sv.mirrorY, "mirror-conn: vertical mirror flips mirrorX only");
        check(near(gv.rectMin.x, 200 - g0.rectMax.x) && near(gv.rectMax.x, 200 - g0.rectMin.x),
              "mirror-conn: body reflects across the vertical line");
        Entity h = c;
        mirrorEntity(h, {0, 50}, {10, 50}); // horizontal mirror line at y=50
        const ConnectorSpec sh = connectorSpecFromMeta(h.meta);
        check(!sh.mirrorX && sh.mirrorY, "mirror-conn: horizontal mirror flips mirrorY only");
    }

    // Copying a connector then X-mirroring the paste preview flips the placed copy.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        ed.connectorSpec() = defaultConnectorSpec(4);
        ed.setTool(ToolId::Connector);
        ed.armConnectorPlacement();
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        QString cid;
        for (const auto& e : d.entities()) if (e.type == EntityType::Connector) cid = e.id;
        ed.setTool(ToolId::Select);
        ed.selectById(cid);
        ed.copySelection();
        ed.paste();
        ed.togglePasteMirror(true); // X -> flip across a vertical line through the base
        ed.onPointerPress({200, 0}, Qt::LeftButton, Qt::NoModifier); // drop the mirrored copy
        int conns = 0; const Entity* pasted = nullptr;
        for (const auto& e : d.entities())
            if (e.type == EntityType::Connector) { ++conns; if (e.id != cid) pasted = &e; }
        check(conns == 2 && pasted, "paste-mirror: a second connector was pasted");
        check(pasted && connectorSpecFromMeta(pasted->meta).mirrorX,
              "paste-mirror: X mirror flips the pasted connector's pin direction");
    }

    // Connector preview snaps by its own corner onto another connector's corner.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); // object snap on by default
        ed.connectorSpec() = defaultConnectorSpec(4);
        ed.setTool(ToolId::Connector);
        ed.armConnectorPlacement();
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier); // connector A at (0,0)
        QString aid;
        for (const auto& e : d.entities()) if (e.type == EntityType::Connector) aid = e.id;
        // Arm a second connector; move its top-left corner near A's top-left corner (0,0)
        // (within the 10-unit tolerance) so the feature snap pulls the anchor onto (0,0).
        ed.armConnectorPlacement();
        ed.onPointerPress({4, 4}, Qt::LeftButton, Qt::NoModifier); // snapped -> anchor (0,0)
        const Entity* b = nullptr;
        for (const auto& e : d.entities())
            if (e.type == EntityType::Connector && e.id != aid) b = &e;
        check(b && near(b->a.x, 0.0) && near(b->a.y, 0.0),
              "conn-snap-place: the ghost corner snaps onto another connector's corner");
    }

    // Connector preview auto-catches its nearest pin end onto a wire endpoint, so the
    // connector can be dropped straight onto an existing harness connection.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); // object snap on by default
        ed.connectorSpec() = defaultConnectorSpec(4);
        const Vec2 T{140.0, -18.0}; // a wire end to catch onto
        Entity w; w.id = "w1"; w.type = EntityType::Wire; w.points = {{300, -18}, T};
        d.entities().push_back(w);
        // Where pin end 0 sits relative to the anchor, so we can aim the click.
        const Vec2 off0 = connectorGeometry({0, 0}, ed.connectorSpec()).pinEnd[0];
        ed.setTool(ToolId::Connector); ed.armConnectorPlacement();
        // Click so pin end 0 lands ~2.8 units from T (inside snap tolerance).
        ed.onPointerPress({T.x - off0.x + 2, T.y - off0.y + 2}, Qt::LeftButton, Qt::NoModifier);
        const Entity* c = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Connector) c = &e;
        const ConnectorGeometry gc =
            connectorGeometry(c->a, connectorSpecFromMeta(c->meta));
        check(c && near(gc.pinEnd[0].x, T.x) && near(gc.pinEnd[0].y, T.y),
              "conn-snap-place: a pin end auto-catches onto a wire end");
    }

    // Connector preview can also catch onto a wire's INTERIOR joint (a mid vertex/knee),
    // not just its endpoints.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); // object snap on by default
        const Vec2 J{100.0, 50.0}; // interior joint; endpoints are far away
        Entity w; w.id = "wj"; w.type = EntityType::Wire; w.points = {{0, 0}, J, {300, 0}};
        d.entities().push_back(w);
        ed.connectorSpec() = defaultConnectorSpec(4);
        ed.setTool(ToolId::Connector); ed.armConnectorPlacement();
        ed.onPointerPress({J.x + 2, J.y + 2}, Qt::LeftButton, Qt::NoModifier); // near the joint
        const Entity* c = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Connector) c = &e;
        check(c && near(c->a.x, J.x) && near(c->a.y, J.y),
              "conn-snap-place: the ghost corner catches a wire's interior joint");
    }

    // Freeze the preview, grab it by a specific pin end, then place: the grabbed pin
    // lands exactly at the placement click (the connector is held by that point).
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d);
        ed.snapSettings().objectSnap = false; ed.snapSettings().gridSnap = false;
        ed.connectorSpec() = defaultConnectorSpec(4);
        ed.setTool(ToolId::Connector); ed.armConnectorPlacement();
        ed.onPointerMove({0, 0});             // ghost anchor at origin
        ed.freezeConnectorPreview();          // Tab: freeze at (0,0)
        const ConnectorGeometry fg = connectorGeometry({0, 0}, ed.connectorSpec());
        const Vec2 pin0 = fg.pinEnd[0];
        ed.onPointerPress(pin0, Qt::LeftButton, Qt::NoModifier); // grab by pin 0 (unfreezes)
        const Vec2 C{250, -40};
        ed.onPointerPress(C, Qt::LeftButton, Qt::NoModifier);    // place, held by pin 0
        const Entity* c = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Connector) c = &e;
        const ConnectorGeometry gc =
            connectorGeometry(c->a, connectorSpecFromMeta(c->meta));
        check(c && near(gc.pinEnd[0].x, C.x) && near(gc.pinEnd[0].y, C.y),
              "freeze-grab: the grabbed pin lands at the placement point");
    }

    // Align routing helper: place two facing connectors (A fixed, B mirrored so its
    // pins face A) with B `dy` below, wire pin 0<->0, then align from A.
    auto alignCase = [](double bx, double by, bool knee) {
        struct R { double wpts; double way0; double way1; double bay; bool built; };
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        ed.connectorSpec() = defaultConnectorSpec(4); ed.connectorSpec().mirrorX = false;
        ed.setTool(ToolId::Connector); ed.armConnectorPlacement();
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);           // A (pins right)
        ed.connectorSpec().mirrorX = true;                                   // B faces A
        ed.setTool(ToolId::Connector); ed.armConnectorPlacement();
        ed.onPointerPress({bx, by}, Qt::LeftButton, Qt::NoModifier);
        QString aid, bid;
        for (const auto& e : d.entities())
            if (e.type == EntityType::Connector) { if (aid.isEmpty()) aid = e.id; else bid = e.id; }
        const Entity* ea = nullptr; const Entity* eb = nullptr;
        for (const auto& e : d.entities()) { if (e.id == aid) ea = &e; if (e.id == bid) eb = &e; }
        const ConnectorGeometry ga = connectorGeometry(ea->a, connectorSpecFromMeta(ea->meta));
        const ConnectorGeometry gb = connectorGeometry(eb->a, connectorSpecFromMeta(eb->meta));
        Entity w; w.id = "aw"; w.type = EntityType::Wire;
        if (knee) // a wire with a bend (knee) between the pins
            w.points = {ga.pinEnd[0], {(ga.pinEnd[0].x + gb.pinEnd[0].x) / 2, ga.pinEnd[0].y},
                        gb.pinEnd[0]};
        else
            w.points = {ga.pinEnd[0], gb.pinEnd[0]}; // straight
        d.entities().push_back(w);
        ed.alignRoutingFrom(aid);
        const Entity* ww = nullptr; const Entity* bb = nullptr;
        for (const auto& e : d.entities()) { if (e.id == "aw") ww = &e; if (e.id == bid) bb = &e; }
        return R{ww ? double(ww->points.size()) : 0.0,
                 ww && ww->points.size() ? ww->points.front().y : 0.0,
                 ww && ww->points.size() ? ww->points.back().y : 0.0,
                 bb ? bb->a.y : -999, ww && bb};
    };
    {
        // Straight wire (no knees), any offset -> straighten: B shifts into line, wire
        // becomes 2 pts horizontal.
        auto r = alignCase(600, -400, false);
        check(r.built && r.wpts == 2 && near(r.way0, r.way1),
              "align-routing: a straight wire is straightened (0 deg)");
        check(near(r.bay, 0.0),
              "align-routing: the connector moves into line when the wires have no knees");
    }
    {
        // A wire that already has a knee -> keep the connector, re-route it at 90 deg.
        auto r = alignCase(600, -400, true);
        check(r.built && r.wpts == 4, "align-routing: a knee'd wire re-routes with 90 deg bends");
        check(near(r.bay, -400.0), "align-routing: the connector stays put for knee'd wires");
    }

    // Scaling a connector resizes its parametric geometry (not just its position).
    {
        Entity c; c.type = EntityType::Connector; c.a = {10, 0};
        ConnectorSpec s = defaultConnectorSpec(4);
        s.rectMode = "custom"; s.rectWidth = 40; s.rectHeight = 84;
        writeConnectorSpec(c.meta, s);
        scaleEntity(c, {0, 0}, 2.0);
        const ConnectorSpec s2 = connectorSpecFromMeta(c.meta);
        check(near(s2.pitch, s.pitch * 2) && near(s2.pinLength, s.pinLength * 2) &&
                  near(s2.rectWidth, 80) && near(s2.rectHeight, 168),
              "scale-conn: body/pitch/pin length scale with the factor");
        check(near(s2.pinSize, s.pinSize * 2) && near(c.a.x, 20.0),
              "scale-conn: text scales and the anchor scales about the base");
    }

    // Scaling a connector keeps a (non-selected) attached wire wired to its pin.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d);
        ed.snapSettings().objectSnap = false; ed.snapSettings().gridSnap = false;
        ed.connectorSpec() = defaultConnectorSpec(4);
        ed.setTool(ToolId::Connector); ed.armConnectorPlacement();
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier); // A at the origin
        QString aid; const Entity* ea = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Connector) { aid = e.id; ea = &e; }
        const ConnectorGeometry ga = connectorGeometry(ea->a, connectorSpecFromMeta(ea->meta));
        const Vec2 pin0 = ga.pinEnd[0];
        Entity w; w.id = "sw"; w.type = EntityType::Wire; w.points = {pin0, {pin0.x + 300, pin0.y}};
        d.entities().push_back(w);
        // Select ONLY the connector and scale it x2 about the origin (its anchor).
        ed.setTool(ToolId::Select); ed.selectById(aid);
        ed.setTool(ToolId::Scale);
        const double ref = distance(ga.rectMin, ga.rectMax);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({2 * ref, 0}, Qt::LeftButton, Qt::NoModifier); // factor 2
        const Entity* ea2 = nullptr; const Entity* ww = nullptr;
        for (const auto& e : d.entities()) { if (e.id == aid) ea2 = &e; if (e.id == "sw") ww = &e; }
        const ConnectorGeometry ga2 = connectorGeometry(ea2->a, connectorSpecFromMeta(ea2->meta));
        check(ww && near(ww->points[0].x, ga2.pinEnd[0].x) && near(ww->points[0].y, ga2.pinEnd[0].y),
              "scale-wire: the attached wire end follows the scaled connector's pin");
        check(ww && near(ww->points[1].x, pin0.x + 300),
              "scale-wire: the wire's open end is left where it was");
    }

    // Scaling a WHOLE harness (both connectors + the wire between them, all selected)
    // scales every part and keeps BOTH wire ends wired to their pins.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d);
        ed.snapSettings().objectSnap = false; ed.snapSettings().gridSnap = false;
        // Connector A at origin.
        ed.connectorSpec() = defaultConnectorSpec(4);
        ed.setTool(ToolId::Connector); ed.armConnectorPlacement();
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        // Connector B further right.
        ed.connectorSpec() = defaultConnectorSpec(4);
        ed.setTool(ToolId::Connector); ed.armConnectorPlacement();
        ed.onPointerPress({400, 0}, Qt::LeftButton, Qt::NoModifier);
        QString aid, bid; const Entity* ea = nullptr; const Entity* eb = nullptr;
        for (const auto& e : d.entities())
            if (e.type == EntityType::Connector) { if (aid.isEmpty()) { aid = e.id; ea = &e; } else { bid = e.id; eb = &e; } }
        const Vec2 pinA = connectorGeometry(ea->a, connectorSpecFromMeta(ea->meta)).pinEnd[0];
        const Vec2 pinB = connectorGeometry(eb->a, connectorSpecFromMeta(eb->meta)).pinEnd[0];
        Entity w; w.id = "hw"; w.type = EntityType::Wire; w.points = {pinA, {pinA.x, pinB.y}, pinB};
        d.entities().push_back(w);
        // Select the ENTIRE harness and scale x2 about the origin.
        ed.setTool(ToolId::Select);
        ed.selectById(aid); ed.selectById(bid, true); ed.selectById("hw", true);
        ed.setTool(ToolId::Scale);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.acceptCommandInput("2"); // typed scale factor about the picked base
        const Entity* a2 = nullptr; const Entity* b2 = nullptr; const Entity* w2 = nullptr;
        for (const auto& e : d.entities()) { if (e.id == aid) a2 = &e; if (e.id == bid) b2 = &e; if (e.id == "hw") w2 = &e; }
        const Vec2 pinA2 = connectorGeometry(a2->a, connectorSpecFromMeta(a2->meta)).pinEnd[0];
        const Vec2 pinB2 = connectorGeometry(b2->a, connectorSpecFromMeta(b2->meta)).pinEnd[0];
        check(a2 && near(a2->meta.value("pitch").toDouble(), 30.0),
              "scale-harness: connector structure scales (pitch x2)");
        check(w2 && near(w2->points.front().x, pinA2.x) && near(w2->points.front().y, pinA2.y),
              "scale-harness: near wire end stays wired to connector A");
        check(w2 && near(w2->points.back().x, pinB2.x) && near(w2->points.back().y, pinB2.y),
              "scale-harness: far wire end stays wired to connector B");
    }

    // Editing a mirrored connector's pin count keeps it mirrored (ribbon seeds the spec
    // from the selected connector, so mirrorX/mirrorY aren't reset to the default).
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        ed.connectorSpec() = defaultConnectorSpec(4); ed.connectorSpec().mirrorX = true;
        ed.setTool(ToolId::Connector); ed.armConnectorPlacement();
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        QString cid; for (const auto& e : d.entities()) if (e.type == EntityType::Connector) cid = e.id;
        ed.setTool(ToolId::Select); ed.selectById(cid);
        const Entity* c0 = nullptr; for (const auto& e : d.entities()) if (e.id == cid) c0 = &e;
        ConnectorSpec s = connectorSpecFromMeta(c0->meta); // seed from the selected connector
        s.pinCount = 6; regenPinLabels(s);
        ed.applyConnectorSpecToSelection(s);
        const Entity* c1 = nullptr; for (const auto& e : d.entities()) if (e.id == cid) c1 = &e;
        const ConnectorSpec s1 = connectorSpecFromMeta(c1->meta);
        check(s1.mirrorX && s1.pinCount == 6,
              "conn-editpins: adding pins keeps the connector mirrored (not flipped)");
    }

    // Wire grips: an open straight wire has a middle joint that moves the whole wire.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        Entity w; w.id = "ow"; w.type = EntityType::Wire; w.points = {{0, 0}, {100, 0}};
        d.entities().push_back(w);
        ed.setTool(ToolId::Select); ed.selectById("ow");
        ed.onPointerPress({50, 0}, Qt::LeftButton, Qt::NoModifier);  // grab middle joint
        ed.onPointerPress({50, 20}, Qt::LeftButton, Qt::NoModifier); // place -> move wire
        const Entity* ww = nullptr; for (const auto& e : d.entities()) if (e.id == "ow") ww = &e;
        check(ww && near(ww->points[0].y, 20) && near(ww->points[1].y, 20),
              "wire-grip: an open wire's middle joint moves the whole wire");
    }
    // A bent wire's between-knee joint moves that run and keeps the ends in place.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        Entity w; w.id = "bw"; w.type = EntityType::Wire;
        w.points = {{0, 0}, {50, 0}, {50, -100}, {100, -100}}; // end, knee, knee, end
        d.entities().push_back(w);
        ed.setTool(ToolId::Select); ed.selectById("bw");
        ed.onPointerPress({50, -50}, Qt::LeftButton, Qt::NoModifier);  // grab middle-run joint
        ed.onPointerPress({70, -50}, Qt::LeftButton, Qt::NoModifier);  // drag the run +20 in x
        const Entity* ww = nullptr; for (const auto& e : d.entities()) if (e.id == "bw") ww = &e;
        check(ww && near(ww->points[1].x, 70) && near(ww->points[2].x, 70) &&
                  near(ww->points[0].x, 0) && near(ww->points[3].x, 100),
              "wire-grip: a between-knee joint moves the run, ends stay put");
    }
    // A straight wire connected to a pin has NO middle move joint.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        ed.connectorSpec() = defaultConnectorSpec(4);
        ed.setTool(ToolId::Connector); ed.armConnectorPlacement();
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        const Entity* c = nullptr; for (const auto& e : d.entities()) if (e.type == EntityType::Connector) c = &e;
        const ConnectorGeometry g = connectorGeometry(c->a, connectorSpecFromMeta(c->meta));
        const Vec2 pe = g.pinEnd[0];
        Entity w; w.id = "cw"; w.type = EntityType::Wire; w.points = {pe, {pe.x + 100, pe.y}};
        d.entities().push_back(w);
        ed.setTool(ToolId::Select); ed.selectById("cw");
        const Vec2 mid{pe.x + 50, pe.y};
        ed.onPointerPress(mid, Qt::LeftButton, Qt::NoModifier);           // no grip here
        ed.onPointerPress({mid.x, mid.y + 40}, Qt::LeftButton, Qt::NoModifier);
        const Entity* ww = nullptr; for (const auto& e : d.entities()) if (e.id == "cw") ww = &e;
        check(ww && near(ww->points[0].x, pe.x) && near(ww->points[0].y, pe.y),
              "wire-grip: a pin-connected wire has no middle joint (not moved)");
    }

    // Align routing at 90 deg picks a lane order with no crossings for a 1:1 mapping.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        ed.connectorSpec() = defaultConnectorSpec(4); ed.connectorSpec().mirrorX = false;
        ed.setTool(ToolId::Connector); ed.armConnectorPlacement();
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);          // A (pins right)
        ed.connectorSpec().mirrorX = true;
        ed.setTool(ToolId::Connector); ed.armConnectorPlacement();
        ed.onPointerPress({600, -400}, Qt::LeftButton, Qt::NoModifier);     // B facing, far below
        QString aid, bid;
        for (const auto& e : d.entities())
            if (e.type == EntityType::Connector) { if (aid.isEmpty()) aid = e.id; else bid = e.id; }
        const Entity* ea = nullptr; const Entity* eb = nullptr;
        for (const auto& e : d.entities()) { if (e.id == aid) ea = &e; if (e.id == bid) eb = &e; }
        const ConnectorGeometry ga = connectorGeometry(ea->a, connectorSpecFromMeta(ea->meta));
        const ConnectorGeometry gb = connectorGeometry(eb->a, connectorSpecFromMeta(eb->meta));
        for (int i = 0; i < 4; ++i) {
            Entity w; w.id = QStringLiteral("cw%1").arg(i); w.type = EntityType::Wire;
            // Pin i -> pin i (monotonic), with a knee so align re-routes at 90° (rather
            // than straightening) and the crossing-minimiser picks a clean lane order.
            w.points = {ga.pinEnd[i], {(ga.pinEnd[i].x + gb.pinEnd[i].x) / 2, ga.pinEnd[i].y},
                        gb.pinEnd[i]};
            d.entities().push_back(w);
        }
        ed.alignRoutingFrom(aid);
        std::vector<const Entity*> ws;
        for (const auto& e : d.entities()) if (e.type == EntityType::Wire) ws.push_back(&e);
        int cross = 0;
        for (size_t a = 0; a < ws.size(); ++a)
            for (size_t b = a + 1; b < ws.size(); ++b)
                for (size_t i = 1; i < ws[a]->points.size(); ++i)
                    for (size_t j = 1; j < ws[b]->points.size(); ++j)
                        if (segmentSegmentIntersection(ws[a]->points[i - 1], ws[a]->points[i],
                                                       ws[b]->points[j - 1], ws[b]->points[j]))
                            ++cross;
        check(ws.size() == 4 && cross == 0,
              "align-routing: monotonic mapping re-routes at 90 deg with no crossings");
    }

    // Placed connectors and wires each get a unique entity number.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        ed.connectorSpec() = defaultConnectorSpec(2);
        ed.setTool(ToolId::Connector); ed.armConnectorPlacement();
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.setTool(ToolId::Connector); ed.armConnectorPlacement();
        ed.onPointerPress({200, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.setTool(ToolId::Wire);
        ed.onPointerPress({0, -80}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({100, -80}, Qt::LeftButton, Qt::NoModifier);
        ed.onKey(Qt::Key_Return, Qt::NoModifier);
        std::vector<int> nums;
        for (const auto& e : d.entities())
            if (e.type == EntityType::Connector || e.type == EntityType::Wire)
                nums.push_back(e.meta.value("entNum").toInt(-1));
        bool allUnique = nums.size() == 3;
        for (size_t i = 0; i < nums.size() && allUnique; ++i) {
            if (nums[i] <= 0) allUnique = false;
            for (size_t j = i + 1; j < nums.size(); ++j) if (nums[i] == nums[j]) allUnique = false;
        }
        check(allUnique, "entnum: connectors and wires get unique nonzero numbers");
    }

    // Validation flags an unconnected pin, a duplicate pin label, and an open wire.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        ed.connectorSpec() = defaultConnectorSpec(3);
        ed.setTool(ToolId::Connector); ed.armConnectorPlacement();
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        QString cid; const Entity* c = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Connector) { cid = e.id; c = &e; }
        const ConnectorGeometry g = connectorGeometry(c->a, connectorSpecFromMeta(c->meta));
        // Duplicate a pin label + make the labels a numeric/letter mix (no refdes/PN either).
        ed.applyConnectorPinLabel(cid, 1, QStringLiteral("1")); // pin 2 label == pin 1 label
        ed.applyConnectorPinLabel(cid, 2, QStringLiteral("A")); // letter among numbers -> mixed
        // Wire from pin 0 to a dangling point (open end) — leaves pins 1,2 unconnected.
        Entity w; w.id = "vw"; w.type = EntityType::Wire;
        w.points = {g.pinEnd[0], {g.pinEnd[0].x + 200, g.pinEnd[0].y - 100}};
        d.entities().push_back(w);
        const auto issues = ed.validateSchematic();
        auto has = [&](const char* frag) {
            for (const auto& s : issues)
                if (s.message.contains(QLatin1String(frag))) return true;
            return false;
        };
        check(has("open"), "validate: reports the wire's open end");
        check(has("share the label"), "validate: reports the duplicate pin label");
        check(has("not connected"), "validate: reports the unconnected pins");
        check(has("mixes numeric and letter"), "validate: reports mixed numeric/letter pin labels");
        check(has("no reference designator"), "validate: reports a missing refdes");
        check(has("no part number"), "validate: reports a missing P/N");
        // Each issue carries a stable key + the offending entity id for jump/highlight.
        check(!issues.empty() && !issues.front().key.isEmpty() &&
                  !issues.front().entityId.isEmpty(),
              "validate: issues carry a key + entity id");
        // Bypass registry: accepting a discrepancy records its key; restore removes it.
        const QString k = issues.front().key;
        ed.bypassIssue(k);
        check(ed.bypassedIssues().contains(k), "validate: bypass records the discrepancy key");
        ed.restoreIssue(k);
        check(!ed.bypassedIssues().contains(k), "validate: restore clears the bypass");
        // Turning a rule off suppresses only that rule's discrepancies.
        ed.setRuleEnabled(QStringLiteral("nc"), false);
        bool anyNc = false, anyDup = false;
        for (const auto& s : ed.validateSchematic()) {
            if (s.key.startsWith(QLatin1String("nc:"))) anyNc = true;
            if (s.key.startsWith(QLatin1String("dup:"))) anyDup = true;
        }
        check(!anyNc, "validate: a disabled rule (nc) produces no discrepancies");
        check(anyDup, "validate: other rules still run when one is off");
        ed.setRuleEnabled(QStringLiteral("nc"), true); // restore for the rest of the test
        // A clean schematic: refdes + P/N set, single numeric pin X-terminated, no wires.
        Document d2; d2.entities().clear();
        Editor ed2; ed2.setDocument(&d2); ed2.snapSettings().objectSnap = false;
        ed2.connectorSpec() = defaultConnectorSpec(1);
        ed2.connectorSpec().designator = QStringLiteral("P1");
        ed2.connectorSpec().partNumber = QStringLiteral("PN-0001");
        ed2.setTool(ToolId::Connector); ed2.armConnectorPlacement();
        ed2.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        const Entity* c2 = nullptr;
        for (const auto& e : d2.entities()) if (e.type == EntityType::Connector) c2 = &e;
        const ConnectorGeometry g2 = connectorGeometry(c2->a, connectorSpecFromMeta(c2->meta));
        Entity nc; nc.type = EntityType::NotConnected; nc.a = g2.pinEnd[0]; // X on the only pin
        d2.entities().push_back(nc);
        check(ed2.validateSchematic().empty(),
              "validate: a fully X-terminated connector with no wires is valid");
    }

    // Wire endpoint reference: "Des/pinLabel" when on a connector pin, else empty ("open").
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        ed.connectorSpec() = defaultConnectorSpec(4);
        ed.connectorSpec().designator = QStringLiteral("P3");
        ed.setTool(ToolId::Connector); ed.armConnectorPlacement();
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        const Entity* c = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Connector) c = &e;
        const ConnectorGeometry g = connectorGeometry(c->a, connectorSpecFromMeta(c->meta));
        check(ed.pinRefAt(g.pinEnd[0]) == QLatin1String("P3/1"),
              "wire-endref: a point on pin 0's end resolves to P3/1");
        check(ed.pinRefAt({9999, 9999}).isEmpty(),
              "wire-endref: a point off any pin is open (empty)");
    }

    // Wire session: a wire function keeps the Wire tool active; the first Esc cancels the
    // in-progress function but stays in Wire, and a second Esc exits to Select.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d);
        ed.setTool(ToolId::Wire);
        ed.beginTwist(QStringLiteral("cross"));
        check(ed.tool() == ToolId::Wire, "wire-session: a wire function keeps the Wire tool");
        ed.onKey(Qt::Key_Escape, Qt::NoModifier);
        check(ed.tool() == ToolId::Wire, "wire-session: first Esc cancels the function, stays in Wire");
        ed.onKey(Qt::Key_Escape, Qt::NoModifier);
        check(ed.tool() == ToolId::Select, "wire-session: second Esc exits to Select");
    }

    // Splice / X size: the typed/ribbon size is written onto the placed marker's meta.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        ed.spliceSize() = 8.0;
        ed.setTool(ToolId::Splice);
        ed.onPointerPress({10, 10}, Qt::LeftButton, Qt::NoModifier);
        const Entity* sp = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Splice) sp = &e;
        check(sp && near(sp->meta.value("size").toDouble(), 8.0),
              "marker-size: a placed splice carries the splice size");
        ed.ncSize() = 12.0;
        ed.setTool(ToolId::NotConnected);
        ed.onPointerPress({20, 20}, Qt::LeftButton, Qt::NoModifier);
        const Entity* nc = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::NotConnected) nc = &e;
        check(nc && near(nc->meta.value("ncSize").toDouble(), 12.0),
              "marker-size: a placed X carries the X size");
        check(ed.wantsValueInput(), "marker-size: X tool accepts a typed value (flying field)");
    }

    // Twist: arm, then a crossing line across two wires creates a Twist annotation.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        Entity w1; w1.id = "w1"; w1.type = EntityType::Wire; w1.points = {{0, 0}, {200, 0}};
        Entity w2; w2.id = "w2"; w2.type = EntityType::Wire; w2.points = {{0, 30}, {200, 30}};
        d.entities().push_back(w1);
        d.entities().push_back(w2);
        ed.beginTwist(QStringLiteral("cross"));
        ed.onPointerPress({100, -10}, Qt::LeftButton, Qt::NoModifier); // start of crossing line
        ed.onPointerPress({100, 40}, Qt::LeftButton, Qt::NoModifier);  // across both wires
        const Entity* tw = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Twist) tw = &e;
        check(tw && tw->points.size() == 2 &&
                  tw->meta.value("twistType").toString() == QLatin1String("cross"),
              "twist: a crossing line over two wires makes a Twist over both");
    }

    // Screen: a crossing line over wires creates a Screen annotation enclosing them.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        Entity w1; w1.id = "w1"; w1.type = EntityType::Wire; w1.points = {{0, 0}, {200, 0}};
        Entity w2; w2.id = "w2"; w2.type = EntityType::Wire; w2.points = {{0, 30}, {200, 30}};
        d.entities().push_back(w1);
        d.entities().push_back(w2);
        ed.beginScreen();
        ed.onPointerPress({100, -10}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({100, 40}, Qt::LeftButton, Qt::NoModifier);
        const Entity* sc = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Screen) sc = &e;
        // 2 caught-wire joints + 1 drain endpoint; catch count = 2.
        check(sc && sc->meta.value("catch").toInt() == 2 && sc->points.size() == 3 &&
                  sc->meta.value("wires").toArray().size() == 2,
              "screen: a crossing line over two wires shields both (ellipse + drain)");
        // Screen radii/showS are editable via setSelectionMetaNumber/Bool (Properties).
        ed.setTool(ToolId::Select);
        ed.selectById(sc->id);
        ed.setSelectionMetaNumber(QStringLiteral("ry"), 30.0);
        ed.setSelectionMetaNumber(QStringLiteral("rx"), 8.0);
        ed.setSelectionMetaBool(QStringLiteral("showS"), false);
        const Entity* sc2 = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Screen) sc2 = &e;
        check(sc2 && near(sc2->meta.value("ry").toDouble(), 30.0) &&
                  near(sc2->meta.value("rx").toDouble(), 8.0) &&
                  sc2->meta.value("showS").toBool(true) == false,
              "screen: large/small radius and showS are editable");
    }

    // Cross switch: toggling a wire crossing adds a hop to one wire, toggling again removes it.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d);
        Entity h; h.id = "h"; h.type = EntityType::Wire; h.points = {{0, 0}, {100, 0}};   // horizontal
        Entity v; v.id = "v"; v.type = EntityType::Wire; v.points = {{50, -50}, {50, 50}}; // vertical
        d.entities().push_back(h);
        d.entities().push_back(v);
        auto hopCount = [&] {
            int n = 0;
            for (const auto& e : d.entities())
                n += e.meta.value("hops").toArray().size();
            return n;
        };
        ed.toggleWireCrossAt({50, 0}); // click the crossing
        check(hopCount() == 1, "cross-switch: first click adds a bridge hop at the crossing");
        // The horizontal wire is the one that hops (it is the more horizontal segment).
        const Entity* hw = nullptr;
        for (const auto& e : d.entities()) if (e.id == "h") hw = &e;
        check(hw && hw->meta.value("hops").toArray().size() == 1,
              "cross-switch: the horizontal wire carries the hop");
        ed.toggleWireCrossAt({50, 0}); // click again
        check(hopCount() == 0, "cross-switch: second click removes the hop (plain cross)");
    }

    // Per-pin wire gauge + ampere round-trip through the connector meta.
    {
        ConnectorSpec s = defaultConnectorSpec(3);
        s.pinGauge = {QStringLiteral("0.5"), QStringLiteral("0.75"), QStringLiteral("1.0")};
        s.pinAmp = {QStringLiteral("5"), QStringLiteral("7.5"), QStringLiteral("10")};
        QJsonObject meta;
        writeConnectorSpec(meta, s);
        const ConnectorSpec r = connectorSpecFromMeta(meta);
        check(r.pinGauge.size() == 3 && r.pinGauge[1] == QLatin1String("0.75"),
              "pin-table: per-pin gauge round-trips through meta");
        check(r.pinAmp.size() == 3 && r.pinAmp[2] == QLatin1String("10"),
              "pin-table: per-pin ampere round-trips through meta");
        // A spec with no gauge/amp arrays pads to pinCount (empty strings).
        QJsonObject bare;
        writeConnectorSpec(bare, defaultConnectorSpec(4));
        bare.remove("pinGauge");
        bare.remove("pinAmp");
        const ConnectorSpec p = connectorSpecFromMeta(bare);
        check(p.pinGauge.size() == 4 && p.pinAmp.size() == 4,
              "pin-table: missing gauge/amp arrays pad to pin count");
    }

    // Dimension length formatting: mm, inch, and dual-unit parentheses.
    {
        EntityRenderer::setDimensionDecimals(2);
        EntityRenderer::setLengthUnitInch(false);
        EntityRenderer::setDualUnits(false);
        check(EntityRenderer::formatLength(25.4) == QLatin1String("25.40"),
              "units: metric length shows the mm value");
        EntityRenderer::setLengthUnitInch(true);
        check(EntityRenderer::formatLength(25.4) == QString::fromUtf8("1.00\""),
              "units: inch length divides by 25.4");
        EntityRenderer::setDualUnits(true);
        check(EntityRenderer::formatLength(25.4).contains(QLatin1String("25.40")),
              "units: dual units append the other unit in parentheses");
        EntityRenderer::setLengthUnitInch(false); // restore defaults
        EntityRenderer::setDualUnits(false);
    }

    // The X (NotConnected) tool keeps placing until Esc — it does not revert to Select.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d);
        ed.snapSettings().objectSnap = false; ed.snapSettings().gridSnap = false;
        ed.setTool(ToolId::NotConnected);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({50, 0}, Qt::LeftButton, Qt::NoModifier);
        int xs = 0;
        for (const auto& e : d.entities()) if (e.type == EntityType::NotConnected) ++xs;
        check(xs == 2, "x-repeat: two X markers placed in a row");
        check(ed.tool() == ToolId::NotConnected, "x-repeat: tool stays active after placing");
        ed.onKey(Qt::Key_Escape, Qt::NoModifier);
        check(ed.tool() == ToolId::Select, "x-repeat: Esc ends the X tool");
    }

    std::printf(g_fail ? "  -> %d failed\n" : "  -> ok\n", g_fail);
    return g_fail;
}
