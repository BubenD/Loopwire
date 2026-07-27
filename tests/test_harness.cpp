#include "app/Editor.h"
#include "geometry/Snap.h"
#include "harness/Harness.h"
#include "harness/HarnessBuilder.h"
#include "geometry/Geometry.h"
#include "model/Document.h"
#include "tools/EntityTransforms.h"

#include <QJsonArray>
#include <algorithm>
#include <array>
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
        // The pin "cup" glyph scales with the connector (cupScale accumulates the factor).
        check(near(c.meta.value("cupScale").toDouble(1.0), 2.0),
              "scale-conn: the pin cup scales with the connector");
        scaleEntity(c, {0, 0}, 1.5); // scaling again compounds
        check(near(c.meta.value("cupScale").toDouble(1.0), 3.0),
              "scale-conn: repeated scaling compounds the cup factor");
    }

    // Shift-drag a wire from a connector pin: it fans out to EVERY pin of the connector
    // at once. Each pin gets a wire with the same relative route (offset by pin spacing).
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d);
        ed.snapSettings().objectSnap = false; ed.snapSettings().gridSnap = false;
        ed.connectorSpec() = defaultConnectorSpec(4);
        ed.setTool(ToolId::Connector); ed.armConnectorPlacement();
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        const Entity* conn = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Connector) conn = &e;
        const ConnectorGeometry g = connectorGeometry(conn->a, connectorSpecFromMeta(conn->meta));
        const Vec2 pin0 = g.pinEnd[0];
        // Wire tool: Shift-click on pin 0, then click the route end, then Enter to finish.
        ed.setTool(ToolId::Wire);
        ed.onPointerPress(pin0, Qt::LeftButton, Qt::ShiftModifier);      // fan-out armed
        ed.onPointerPress({pin0.x + 200, pin0.y}, Qt::LeftButton, Qt::NoModifier);
        ed.onKey(Qt::Key_Return, Qt::NoModifier);                       // finish the wire
        int wires = 0;
        for (const auto& e : d.entities()) if (e.type == EntityType::Wire) ++wires;
        check(wires == 4, "wire-fanout: one wire is created per connector pin");
        // Each wire starts exactly on its pin end.
        int onPin = 0;
        for (const auto& e : d.entities()) {
            if (e.type != EntityType::Wire) continue;
            for (const Vec2& pe : g.pinEnd)
                if (near(e.points.front().x, pe.x) && near(e.points.front().y, pe.y)) ++onPin;
        }
        check(onPin == 4, "wire-fanout: every fanned wire starts on a distinct pin end");
    }

    // Shift-fanout from a MIDDLE pin must include the pins ABOVE it too (pin 3 -> pins 1..4),
    // each wire starting exactly on its own pin.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d);
        ed.snapSettings().objectSnap = false; ed.snapSettings().gridSnap = false;
        ed.connectorSpec() = defaultConnectorSpec(4);
        ed.setTool(ToolId::Connector); ed.armConnectorPlacement();
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        const Entity* conn = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Connector) conn = &e;
        const ConnectorGeometry g = connectorGeometry(conn->a, connectorSpecFromMeta(conn->meta));
        ed.setTool(ToolId::Wire);
        ed.onPointerPress(g.pinEnd[2], Qt::LeftButton, Qt::ShiftModifier); // start on pin 3
        ed.onPointerPress({g.pinEnd[2].x + 200, g.pinEnd[2].y}, Qt::LeftButton, Qt::NoModifier);
        ed.onKey(Qt::Key_Return, Qt::NoModifier);
        int onPin = 0;
        for (const auto& e : d.entities()) {
            if (e.type != EntityType::Wire) continue;
            for (const Vec2& pe : g.pinEnd)
                if (near(e.points.front().x, pe.x) && near(e.points.front().y, pe.y)) ++onPin;
        }
        check(onPin == 4, "wire-fanout-mid: starting on pin 3 fans all pins incl. 1 and 2");
    }

    // Wiring from a pin, Shift+<pin number> toggles that ONE pin in/out of the fan. Start
    // on pin 1, toggle pin 3 and pin 4 in => wires on pins 1,3,4 (pin 2 skipped); toggling
    // pin 3 again removes it.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d);
        ed.snapSettings().objectSnap = false; ed.snapSettings().gridSnap = false;
        ed.connectorSpec() = defaultConnectorSpec(4);
        ed.setTool(ToolId::Connector); ed.armConnectorPlacement();
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        const Entity* conn = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Connector) conn = &e;
        const ConnectorGeometry g = connectorGeometry(conn->a, connectorSpecFromMeta(conn->meta));
        ed.setTool(ToolId::Wire);
        ed.onPointerPress(g.pinEnd[0], Qt::LeftButton, Qt::NoModifier); // start on pin 1
        check(ed.wireFanArmed(), "wire-fan-toggle: fan is armed after starting on a pin");
        ed.toggleWireFanPin(3); // add wire 3
        ed.toggleWireFanPin(4); // add wire 4
        ed.toggleWireFanPin(2); // add wire 2
        ed.toggleWireFanPin(2); // ...then remove wire 2 again
        ed.onPointerPress({g.pinEnd[0].x + 200, g.pinEnd[0].y}, Qt::LeftButton, Qt::NoModifier);
        ed.onKey(Qt::Key_Return, Qt::NoModifier);
        std::vector<double> ys;
        for (const auto& e : d.entities())
            if (e.type == EntityType::Wire) ys.push_back(e.points.front().y);
        check(ys.size() == 3, "wire-fan-toggle: pins 1,3,4 wired (pin 2 toggled back off)");
        auto wiredAt = [&](const Vec2& pe) {
            for (double y : ys) if (near(y, pe.y)) return true;
            return false;
        };
        check(wiredAt(g.pinEnd[0]) && wiredAt(g.pinEnd[2]) && wiredAt(g.pinEnd[3]) &&
                  !wiredAt(g.pinEnd[1]),
              "wire-fan-toggle: exactly pins 1,3,4 have wires");
    }

    // Termination: Shift + click a connector pin X-terminates every UNCONNECTED pin at
    // once — pins already wired (or already terminated) are left alone.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d);
        ed.snapSettings().objectSnap = false; ed.snapSettings().gridSnap = false;
        ed.connectorSpec() = defaultConnectorSpec(4);
        ed.setTool(ToolId::Connector); ed.armConnectorPlacement();
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        const Entity* conn = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Connector) conn = &e;
        const ConnectorGeometry g = connectorGeometry(conn->a, connectorSpecFromMeta(conn->meta));
        // Wire pin 1 (index 1) so it counts as connected; the other 3 stay open.
        Entity w; w.id = "w"; w.type = EntityType::Wire;
        w.points = {g.pinEnd[1], {g.pinEnd[1].x + 100, g.pinEnd[1].y}};
        d.entities().push_back(w);
        ed.setTool(ToolId::NotConnected);
        ed.onPointerPress(g.pinEnd[0], Qt::LeftButton, Qt::ShiftModifier); // Shift+click a pin
        int xs = 0;
        for (const auto& e : d.entities()) if (e.type == EntityType::NotConnected) ++xs;
        check(xs == 3, "term-fanout: the 3 unconnected pins are terminated, the wired one is not");
        // Each X sits on a distinct unconnected pin end (not the wired pin 1).
        bool onWired = false;
        for (const auto& e : d.entities())
            if (e.type == EntityType::NotConnected && near(e.a.x, g.pinEnd[1].x) &&
                near(e.a.y, g.pinEnd[1].y))
                onWired = true;
        check(!onWired, "term-fanout: the already-wired pin is left un-terminated");
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
        check(has("unconnected end"), "validate: reports the wire's unconnected end");
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

    // Twist arms without opening the Wire tool/ribbon (it uses Select); its crossing-line
    // pick still works, and Esc cancels it, staying in Select.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d);
        ed.setTool(ToolId::Wire);
        ed.beginTwist(QStringLiteral("cross"));
        check(ed.tool() == ToolId::Select, "twist-no-wire: Twist arms on Select, not Wire");
        check(ed.awaitingWireBand(), "twist-no-wire: the twist band pick is armed");
        ed.onKey(Qt::Key_Escape, Qt::NoModifier);
        check(ed.tool() == ToolId::Select && !ed.awaitingWireBand(),
              "twist-no-wire: Esc cancels the twist, stays in Select");
    }

    // Esc while actively drawing a wire commits what was already placed (>=2 confirmed
    // points) instead of discarding the whole draft — only the pending next segment is
    // cancelled — then returns to the Select tool.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d);
        ed.snapSettings().objectSnap = false; ed.snapSettings().gridSnap = false;
        ed.setTool(ToolId::Wire);
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({50, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({50, 50}, Qt::LeftButton, Qt::NoModifier); // 3 confirmed points
        ed.onKey(Qt::Key_Escape, Qt::NoModifier); // cancel the pending next point, keep the rest
        int wires = 0; const Entity* w = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Wire) { ++wires; w = &e; }
        check(wires == 1, "wire-esc-commit: Esc commits the wire with the points already placed");
        check(w && w->points.size() == 3, "wire-esc-commit: exactly the 3 confirmed points are kept");
        check(ed.tool() == ToolId::Select,
              "wire-esc-commit: Esc returns to the Select tool");
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
        check(tw && tw->meta.value("wires").toArray().size() == 2,
              "twist: records which wire each crossing point belongs to");

        // Constrained move: the twist's grip only slides it ALONG its wires (world x);
        // the per-wire crossing points stay ON their wires (y re-interpolated). Driven
        // through the real grip-grab flow (click grip, click destination), like the
        // wire-grip tests above.
        const QString twId = tw->id;
        ed.setTool(ToolId::Select);
        ed.selectById(twId);
        const Vec2 gripAt = lerp(tw->points[0], tw->points[1], 0.5); // twist's Move grip
        ed.onPointerPress(gripAt, Qt::LeftButton, Qt::NoModifier);   // grab
        ed.onPointerPress({150, 999}, Qt::LeftButton, Qt::NoModifier); // place (y ignored)
        const Entity* tw2 = nullptr;
        for (const auto& e : d.entities()) if (e.id == twId) tw2 = &e;
        check(tw2 && near(tw2->points[0].x, 150.0) && near(tw2->points[0].y, 0.0) &&
                  near(tw2->points[1].x, 150.0) && near(tw2->points[1].y, 30.0),
              "twist-move: dragging the grip slides both crossing points along their wires");
        // Clamped to the wire's own extent — can't slide past its endpoints.
        ed.selectById(twId);
        const Vec2 gripAt2 = lerp(tw2->points[0], tw2->points[1], 0.5);
        ed.onPointerPress(gripAt2, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({9999, 0}, Qt::LeftButton, Qt::NoModifier);
        const Entity* tw3 = nullptr;
        for (const auto& e : d.entities()) if (e.id == twId) tw3 = &e;
        check(tw3 && near(tw3->points[0].x, 200.0),
              "twist-move: clamped to the wire's own x-extent");
    }

    // Twist align: select twist marks, then click a destination — they all slide there.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        Entity w1; w1.id = "w1"; w1.type = EntityType::Wire; w1.points = {{0, 0}, {200, 0}};
        Entity w2; w2.id = "w2"; w2.type = EntityType::Wire; w2.points = {{0, 30}, {200, 30}};
        d.entities().push_back(w1);
        d.entities().push_back(w2);
        ed.beginTwist(QStringLiteral("cross"));
        ed.onPointerPress({50, -10}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({50, 40}, Qt::LeftButton, Qt::NoModifier);
        QString twId;
        for (const auto& e : d.entities()) if (e.type == EntityType::Twist) twId = e.id;
        ed.setTool(ToolId::Select);
        ed.selectById(twId);
        ed.beginTwistAlign();
        check(ed.awaitingTwistAlign(), "twist-align: arms after a twist is selected");
        ed.onPointerPress({160, 5}, Qt::LeftButton, Qt::NoModifier); // destination on a wire
        const Entity* tw = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Twist) tw = &e;
        check(tw && near(tw->points[0].x, 160.0) && near(tw->points[1].x, 160.0),
              "twist-align: the selected twist slides to the destination x");
        check(!ed.awaitingTwistAlign(), "twist-align: disarms after aligning");
    }

    // Twist structure fix: the wire itself is what's clipped through the twist band (no
    // separate masking rectangle) — verified via EntityRenderer::formatLength-adjacent
    // API isn't applicable here, so we check the underlying data model stays intact:
    // the wire's OWN points are untouched by adding a twist (only the Twist stores the
    // crossing points) — i.e. the wire's structure is preserved, not overwritten.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        Entity w1; w1.id = "w1"; w1.type = EntityType::Wire; w1.points = {{0, 0}, {200, 0}};
        Entity w2; w2.id = "w2"; w2.type = EntityType::Wire; w2.points = {{0, 30}, {200, 30}};
        d.entities().push_back(w1);
        d.entities().push_back(w2);
        ed.beginTwist(QStringLiteral("cross"));
        ed.onPointerPress({100, -10}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({100, 40}, Qt::LeftButton, Qt::NoModifier);
        const Entity* w1After = nullptr;
        for (const auto& e : d.entities()) if (e.id == "w1") w1After = &e;
        check(w1After && w1After->points.size() == 2 &&
                  near(w1After->points[0].x, 0.0) && near(w1After->points[1].x, 200.0),
              "twist-structure: the wire's own points are unchanged by adding a twist");
    }

    // A crossing box that only touches the RIGHT part of a Twist's rendered band (not
    // its narrow raw crossing points) still selects it.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        Entity w1; w1.id = "w1"; w1.type = EntityType::Wire; w1.points = {{0, 0}, {200, 0}};
        Entity w2; w2.id = "w2"; w2.type = EntityType::Wire; w2.points = {{0, 30}, {200, 30}};
        d.entities().push_back(w1);
        d.entities().push_back(w2);
        ed.beginTwist(QStringLiteral("cross"));
        ed.onPointerPress({100, -10}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({100, 40}, Qt::LeftButton, Qt::NoModifier);
        QString twId;
        for (const auto& e : d.entities()) if (e.type == EntityType::Twist) twId = e.id;
        check(!twId.isEmpty(), "twist-touch-select: the twist was placed");
        // Box spans x:[110,125] (inside the rendered band x0≈79..x1≈121 but NOT reaching
        // the raw crossing points at x=100), and y far from both wires (0/30) so the
        // corner clicks land on EMPTY space. A two-click rectangle needs press-RELEASE
        // (Deciding -> RectPending) - move - press (commits), matching test_editor.cpp's
        // regionDrag helper; without the release the 2nd press just starts a fresh drag.
        ed.setTool(ToolId::Select);
        ed.onPointerPress({125, 100}, Qt::LeftButton, Qt::NoModifier);   // 1st corner (right)
        ed.onPointerRelease({125, 100}, Qt::LeftButton);                // -> RectPending
        ed.onPointerMove({110, -50});
        ed.onPointerPress({110, -50}, Qt::LeftButton, Qt::NoModifier); // 2nd corner (left) -> crossing
        auto has = [&](const QString& id) {
            const auto& sel = ed.selection();
            return std::find(sel.begin(), sel.end(), id) != sel.end();
        };
        check(has(twId), "twist-touch-select: a crossing box touching only the glyph's edge selects it");
    }

    // Screen leg termination is a snappable joint (a new wire's endpoint can catch it).
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        Entity w1; w1.id = "w1"; w1.type = EntityType::Wire; w1.points = {{0, 0}, {200, 0}};
        Entity w2; w2.id = "w2"; w2.type = EntityType::Wire; w2.points = {{0, 30}, {200, 30}};
        d.entities().push_back(w1);
        d.entities().push_back(w2);
        ed.screenLegDir() = QStringLiteral("bottom");
        ed.beginScreen();
        ed.onPointerPress({100, -10}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({100, 40}, Qt::LeftButton, Qt::NoModifier);
        const Entity* sc = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Screen) sc = &e;
        check(sc && !sc->points.empty(), "screen-leg-snap: the screen was placed");
        const Vec2 drain = sc->points.back();
        // Default (auto): ry = span/2 + 30% of the wire gap = 15 + 0.3*30 = 24, cyMid=15,
        // bottom edge = 15-24 = -9, drain sits 15 (leg) below it at y = -9-15 = -24.
        check(near(drain.y, -24.0), "shield-default-leg: the default leg length is 15");
        SnapSettings s; // objectSnap + endpoint on by default
        auto r = resolveSnap(d, {drain.x + 1, drain.y + 1}, std::nullopt, 5.0, s, {});
        check(r.snapped && near(r.point.x, drain.x) && near(r.point.y, drain.y),
              "screen-leg-snap: a nearby point snaps onto the drain-leg termination");
        // Placing a shield returns to the Select tool (region-select), not Wire.
        check(ed.tool() == ToolId::Select, "shield-return-select: back to Select after placing");
    }

    // Shield leg "left" exits to the LEFT (negative x), "right" to the right — for a
    // vertical shield over level wires.
    {
        auto legDrainX = [](const QString& dir) {
            Document d; d.entities().clear();
            Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
            Entity w1; w1.id = "w1"; w1.type = EntityType::Wire; w1.points = {{0, 0}, {200, 0}};
            Entity w2; w2.id = "w2"; w2.type = EntityType::Wire; w2.points = {{0, 30}, {200, 30}};
            d.entities().push_back(w1);
            d.entities().push_back(w2);
            ed.screenLegDir() = dir;
            ed.beginScreen();
            ed.onPointerPress({100, -10}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerPress({100, 40}, Qt::LeftButton, Qt::NoModifier);
            for (const auto& e : d.entities())
                if (e.type == EntityType::Screen) return e.points.back().x;
            return 0.0;
        };
        check(legDrainX(QStringLiteral("left")) < 100.0,
              "shield-leg-lr: the left leg exits to the left of centre");
        check(legDrainX(QStringLiteral("right")) > 100.0,
              "shield-leg-lr: the right leg exits to the right of centre");
    }

    // Shield "amount": placing with amount=3 drops 3 shields in one drag, side by side.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        Entity w1; w1.id = "w1"; w1.type = EntityType::Wire; w1.points = {{0, 0}, {200, 0}};
        Entity w2; w2.id = "w2"; w2.type = EntityType::Wire; w2.points = {{0, 30}, {200, 30}};
        d.entities().push_back(w1);
        d.entities().push_back(w2);
        ed.screenAmount() = 3;
        ed.beginScreen();
        ed.onPointerPress({100, -10}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({100, 40}, Qt::LeftButton, Qt::NoModifier);
        std::vector<const Entity*> shields;
        for (const auto& e : d.entities()) if (e.type == EntityType::Screen) shields.push_back(&e);
        check(shields.size() == 3, "shield-amount: three shields placed for amount=3");
        // Only the first carries the caught-wire references; the copies are decorative.
        int withWires = 0;
        for (const Entity* s : shields)
            if (!s->meta.value("wires").toArray().isEmpty()) ++withWires;
        check(withWires == 1, "shield-amount: only the first shield references the wires");
        // The copies are offset side by side (distinct centre x's; direction follows the
        // minor axis, which is horizontal for these level wires).
        check(std::abs(shields[0]->points.front().x - shields[2]->points.front().x) > 1.0,
              "shield-amount: the copies are offset side by side");
    }

    // Shield covers ALL caught wires even when they're caught out of spatial order (the
    // size/centre come from the outermost wires, not the first/last entity).
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        // Wires added in scrambled y-order (0, 90, 30, 60) — extent is 0..90.
        const double ys[] = {0, 90, 30, 60};
        int i = 0;
        for (double y : ys) {
            Entity w; w.id = QStringLiteral("w%1").arg(i++); w.type = EntityType::Wire;
            w.points = {{0, y}, {200, y}};
            d.entities().push_back(w);
        }
        ed.beginScreen();
        ed.onPointerPress({100, -20}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({100, 110}, Qt::LeftButton, Qt::NoModifier);
        const Entity* sc = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Screen) sc = &e;
        check(sc && sc->meta.value("catch").toInt() == 4, "shield-covers-all: caught all 4 wires");
        // The ellipse (centre = midpoint of extent = y=45, semi-axis ry) must enclose the
        // outermost joints at y=0 and y=90 — i.e. ry >= 45.
        const double ry = sc->meta.value("ry").toDouble();
        check(ry >= 45.0, "shield-covers-all: ry spans the full extent of the caught wires");
    }

    // Shield auto-perpendicular: a SLANTED crossing line over level (0°) wires still makes
    // a shield perpendicular to them (rot = 90°), crossing all wires at 90°.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        Entity w1; w1.id = "w1"; w1.type = EntityType::Wire; w1.points = {{0, 0}, {200, 0}};
        Entity w2; w2.id = "w2"; w2.type = EntityType::Wire; w2.points = {{0, 60}, {200, 60}};
        d.entities().push_back(w1);
        d.entities().push_back(w2);
        ed.beginScreen();
        ed.onPointerPress({80, 100}, Qt::LeftButton, Qt::NoModifier);   // deliberately slanted
        ed.onPointerPress({120, -40}, Qt::LeftButton, Qt::NoModifier);  // crossing line
        const Entity* sc = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Screen) sc = &e;
        check(sc && sc->meta.value("catch").toInt() == 2, "shield-perp: caught both level wires");
        check(sc && near(sc->meta.value("rot").toDouble(), 1.5707963267948966), // pi/2
              "shield-perp: the shield is perpendicular (90°) to the level wires");
    }

    // Unlocking the radii makes the shield use the TYPED radii (so editing the large radius
    // shows in the preview / placed shield) instead of auto-fitting the wires.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        Entity w1; w1.id = "w1"; w1.type = EntityType::Wire; w1.points = {{0, 0}, {200, 0}};
        Entity w2; w2.id = "w2"; w2.type = EntityType::Wire; w2.points = {{0, 30}, {200, 30}};
        d.entities().push_back(w1);
        d.entities().push_back(w2);
        ed.screenRxLock() = false; ed.screenRyLocked() = false; // unlock the radii = manual
        ed.screenRx() = 20.0;
        ed.screenRy() = 50.0;
        ed.beginScreen();
        ed.onPointerPress({100, -10}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({100, 40}, Qt::LeftButton, Qt::NoModifier);
        const Entity* sc = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Screen) sc = &e;
        check(sc && near(sc->meta.value("ry").toDouble(), 50.0) &&
                  near(sc->meta.value("rx").toDouble(), 20.0),
              "shield-manual: unlocked radii use the typed values (not auto-fit)");
    }

    // Shield "S" mark: off by default, with an editable size stored on the shield.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        Entity w1; w1.id = "w1"; w1.type = EntityType::Wire; w1.points = {{0, 0}, {200, 0}};
        Entity w2; w2.id = "w2"; w2.type = EntityType::Wire; w2.points = {{0, 30}, {200, 30}};
        d.entities().push_back(w1);
        d.entities().push_back(w2);
        check(!ed.screenShowS(), "shield-s: the S mark is off by default");
        ed.screenSSize() = 7.0;
        ed.beginScreen();
        ed.onPointerPress({100, -10}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({100, 40}, Qt::LeftButton, Qt::NoModifier);
        const Entity* sc = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Screen) sc = &e;
        check(sc && sc->meta.value("showS").toBool() == false,
              "shield-s: placed shield defaults to no S mark");
        check(sc && near(sc->meta.value("sSize").toDouble(), 7.0),
              "shield-s: the S label size is stored on the shield");
    }

    // Shield "Not Connected" checkbox: off by default; when checked, the placed shield's
    // meta records it (and it stays checked for the NEXT shield placed, sticky default).
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        Entity w1; w1.id = "w1"; w1.type = EntityType::Wire; w1.points = {{0, 0}, {200, 0}};
        Entity w2; w2.id = "w2"; w2.type = EntityType::Wire; w2.points = {{0, 30}, {200, 30}};
        d.entities().push_back(w1);
        d.entities().push_back(w2);
        check(!ed.screenNotConnected(), "shield-notconnected: unchecked by default");
        // Placing more shields reallocates the entity vector, so track them by ID and read
        // meta through a lookup rather than holding Entity* across placements.
        std::vector<QString> scIds;
        auto placeShield = [&](double x) {
            ed.beginScreen();
            ed.onPointerPress({x, -10}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerPress({x, 40}, Qt::LeftButton, Qt::NoModifier);
            for (const auto& e : d.entities())
                if (e.type == EntityType::Screen &&
                    std::find(scIds.begin(), scIds.end(), e.id) == scIds.end())
                    scIds.push_back(e.id);
            return scIds.back();
        };
        auto metaOf = [&](const QString& id, const char* key) {
            for (const auto& e : d.entities())
                if (e.id == id) return e.meta.value(QLatin1String(key));
            return QJsonValue();
        };

        const QString id1 = placeShield(100);
        check(metaOf(id1, "notConnected").toBool(true) == false,
              "shield-notconnected: unchecked default leaves the leg without the X flag");
        ed.screenNotConnected() = true;
        const QString id2 = placeShield(150);
        check(metaOf(id2, "notConnected").toBool(false) == true,
              "shield-notconnected: checking it marks the placed shield's leg as not connected");
        // The X is a real world-sized symbol (not a fixed-pixel tick), default half-size 3.
        check(near(ed.screenNcSize(), 3.0), "shield-notconnected: the X defaults to size 3");
        check(near(metaOf(id2, "ncSize").toDouble(), 3.0),
              "shield-notconnected: the leg-end X is world-sized at half-size 3");
        // Ribbon: the X size is a sticky default for the next shield placed.
        ed.screenNcSize() = 8.0;
        const QString id3 = placeShield(40);
        check(near(metaOf(id3, "ncSize").toDouble(), 8.0),
              "shield-notconnected: the ribbon X size applies to the next shield");
        // Properties: the X size is editable per shield, and only that shield changes.
        ed.setTool(ToolId::Select);
        ed.clearSelection();
        ed.selectById(id3);
        ed.setSelectionMetaNumber(QStringLiteral("ncSize"), 4.5);
        check(near(metaOf(id3, "ncSize").toDouble(), 4.5),
              "shield-notconnected: the X size is editable from Properties");
        check(near(metaOf(id2, "ncSize").toDouble(), 3.0),
              "shield-notconnected: editing one shield's X size leaves the others alone");
        // Removable again from Properties (the left panel drives setSelectionMetaBool).
        ed.clearSelection();
        ed.selectById(id2);
        ed.setSelectionMetaBool(QStringLiteral("notConnected"), false);
        check(metaOf(id2, "notConnected").toBool(true) == false,
              "shield-notconnected: the X can be removed again from Properties");
    }

    // A shield is pinned to the wires it catches: a move slides it ALONG them (each
    // crossing re-derived from its own wire) and the drain leg rides along.
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
        Entity* sc = nullptr;
        for (auto& e : d.entities()) if (e.type == EntityType::Screen) sc = &e;
        check(sc != nullptr, "shield-slide: a shield was placed");
        const Vec2 drain0 = sc->points.back();
        // Slide 40 to the right along the (horizontal) wires.
        const bool ok = ed.repositionScreenAtX(*sc, 140.0);
        check(ok, "shield-slide: the shield reports it slid along its wires");
        check(near(sc->points[0].x, 140.0) && near(sc->points[1].x, 140.0),
              "shield-slide: every caught crossing moved to the new x");
        check(near(sc->points[0].y, 0.0) && near(sc->points[1].y, 30.0),
              "shield-slide: each crossing stays ON its own wire");
        check(near(sc->points.back().x, drain0.x + 40.0) && near(sc->points.back().y, drain0.y),
              "shield-slide: the drain leg rides along with the shield");
        // It can never leave the wires: a huge x clamps to the wire's own extent.
        ed.repositionScreenAtX(*sc, 10000.0);
        check(near(sc->points[0].x, 200.0), "shield-slide: clamped to the wire's extent");
    }

    // Large-radius grips exist, and a resize only sticks when it reaches a different set
    // of wires; otherwise the shield snaps back to its previous size.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        Entity w1; w1.id = "w1"; w1.type = EntityType::Wire; w1.points = {{0, 0}, {200, 0}};
        Entity w2; w2.id = "w2"; w2.type = EntityType::Wire; w2.points = {{0, 30}, {200, 30}};
        Entity w3; w3.id = "w3"; w3.type = EntityType::Wire; w3.points = {{0, 90}, {200, 90}};
        d.entities().push_back(w1);
        d.entities().push_back(w2);
        d.entities().push_back(w3);
        ed.beginScreen();
        ed.onPointerPress({100, -10}, Qt::LeftButton, Qt::NoModifier); // catches w1 + w2 only
        ed.onPointerPress({100, 40}, Qt::LeftButton, Qt::NoModifier);
        Entity* sc = nullptr;
        for (auto& e : d.entities()) if (e.type == EntityType::Screen) sc = &e;
        check(sc && sc->meta.value("catch").toInt() == 2, "shield-ry: placed over 2 wires");
        const QString scId = sc->id;
        const double ry0 = sc->meta.value("ry").toDouble();
        // Centre sits at y=15, so reaching w3 (y=90) needs a half-height over 75.
        std::vector<Vec2> xs; std::vector<QString> ids;
        ed.shieldCatchAt(*sc, 80.0, xs, ids);
        check(ids.size() == 3, "shield-ry: a taller shield reaches the third wire");
        ed.shieldCatchAt(*sc, ry0 + 2.0, xs, ids);
        check(ids.size() == 2, "shield-ry: a slightly taller one still reaches only two");

        // Drive the REAL grip drag. Centre is (100,15) and the major axis is vertical, so
        // the large-radius joints sit at (100, 15 +/- ry0).
        auto findSc = [&]() -> Entity* {
            for (auto& e : d.entities()) if (e.type == EntityType::Screen) return &e;
            return nullptr;
        };
        auto dragRyTo = [&](double y) {
            ed.setTool(ToolId::Select);
            ed.clearSelection();
            ed.selectById(scId);
            ed.onPointerPress({100, 15 + ry0}, Qt::LeftButton, Qt::NoModifier); // grab the tip
            ed.onPointerMove({100, y});
            ed.onPointerPress({100, y}, Qt::LeftButton, Qt::NoModifier);        // place it
        };
        // (a) A drag that stops short of w3 catches nothing new -> snaps back.
        dragRyTo(15 + ry0 + 4.0);
        Entity* after = findSc();
        check(after && after->meta.value("catch").toInt() == 2 &&
                  near(after->meta.value("ry").toDouble(), ry0),
              "shield-ry: a drag reaching no other wire returns to the previous size");
        // (b) A drag past w3 (y=90) catches it -> the resize sticks and w3 joins the set.
        dragRyTo(95.0);
        after = findSc();
        bool hasW3 = false;
        for (const auto v : after->meta.value("wires").toArray())
            if (v.toString() == QLatin1String("w3")) hasW3 = true;
        check(after && after->meta.value("catch").toInt() == 3 && hasW3,
              "shield-ry: dragging past another wire catches it");
        check(after && after->meta.value("ry").toDouble() > ry0,
              "shield-ry: the shield kept the larger size that caught the wire");
        // The drain leg is still the last point, beyond the (new) outline.
        check(after && static_cast<int>(after->points.size()) ==
                           after->meta.value("catch").toInt() + 1,
              "shield-ry: the drain leg survives the resize");
    }

    // Shield shape: choosing "capsule" is recorded on the placed shield's meta.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        Entity w1; w1.id = "w1"; w1.type = EntityType::Wire; w1.points = {{0, 0}, {200, 0}};
        Entity w2; w2.id = "w2"; w2.type = EntityType::Wire; w2.points = {{0, 30}, {200, 30}};
        d.entities().push_back(w1);
        d.entities().push_back(w2);
        ed.screenShape() = QStringLiteral("capsule");
        ed.beginScreen();
        ed.onPointerPress({100, -10}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({100, 40}, Qt::LeftButton, Qt::NoModifier);
        const Entity* sc = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Screen) sc = &e;
        check(sc && sc->meta.value("shape").toString() == "capsule",
              "shield-shape: the capsule shape is stored on the placed shield");
    }

    // Angled shield: wires that slope give a shield whose major axis runs first->last wire.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        // Two sloping wires so their crossing points aren't vertically aligned.
        Entity w1; w1.id = "w1"; w1.type = EntityType::Wire; w1.points = {{0, 0}, {200, 40}};
        Entity w2; w2.id = "w2"; w2.type = EntityType::Wire; w2.points = {{0, 40}, {200, 80}};
        d.entities().push_back(w1);
        d.entities().push_back(w2);
        ed.beginScreen();
        ed.onPointerPress({80, 200}, Qt::LeftButton, Qt::NoModifier);   // crossing line...
        ed.onPointerPress({120, -50}, Qt::LeftButton, Qt::NoModifier);  // ...across both wires
        const Entity* sc = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Screen) sc = &e;
        check(sc && sc->meta.value("catch").toInt() == 2, "shield-angled: caught both sloping wires");
        // The two crossing joints differ in BOTH x and y (the shield is tilted, not vertical).
        const double dx = std::abs(sc->points[0].x - sc->points[1].x);
        check(sc && dx > 1.0, "shield-angled: the crossing joints are offset in x (tilted)");
    }

    // Shield line style: dashed + colour are stored on the placed shield.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        Entity w1; w1.id = "w1"; w1.type = EntityType::Wire; w1.points = {{0, 0}, {200, 0}};
        Entity w2; w2.id = "w2"; w2.type = EntityType::Wire; w2.points = {{0, 30}, {200, 30}};
        d.entities().push_back(w1);
        d.entities().push_back(w2);
        ed.screenLineType() = QStringLiteral("dashed");
        ed.screenDashScale() = 2.0;
        ed.screenColor() = QStringLiteral("#ff0000");
        ed.beginScreen();
        ed.onPointerPress({100, -10}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({100, 40}, Qt::LeftButton, Qt::NoModifier);
        const Entity* sc = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Screen) sc = &e;
        check(sc && sc->meta.value("lineType").toString() == "dashed" &&
                  near(sc->meta.value("dashScale").toDouble(), 2.0) && sc->color == "#ff0000",
              "shield-style: dashed line, dash scale and colour are stored");
    }

    // Validation: a SINGLE shield covering several wires is reported as ONE row listing
    // all its pins; two shields on the same wires clear the flag.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d);
        Entity w1; w1.id = "w1"; w1.type = EntityType::Wire; w1.points = {{0, 0}, {200, 0}};
        w1.meta.insert("entNum", 7);
        Entity w2; w2.id = "w2"; w2.type = EntityType::Wire; w2.points = {{0, 30}, {200, 30}};
        w2.meta.insert("entNum", 8);
        d.entities().push_back(w1);
        d.entities().push_back(w2);
        QJsonArray wref; wref.append(QStringLiteral("w1")); wref.append(QStringLiteral("w2"));
        Entity s1; s1.id = "s1"; s1.type = EntityType::Screen; s1.points = {{50, 0}, {50, 30}, {50, -20}};
        s1.meta.insert("catch", 2); s1.meta.insert("wires", wref);
        d.entities().push_back(s1);
        auto countKey = [&](const char* k) {
            int n = 0;
            for (const auto& is : ed.validateSchematic())
                if (is.key.startsWith(QLatin1String(k))) ++n;
            return n;
        };
        auto msgFor = [&](const char* k) {
            for (const auto& is : ed.validateSchematic())
                if (is.key.startsWith(QLatin1String(k))) return is.message;
            return QString();
        };
        check(countKey("shieldpair") == 1,
              "validate: a single shield over 2 wires is ONE row, not one per wire");
        check(msgFor("shieldpair").contains("#7") && msgFor("shieldpair").contains("#8"),
              "validate: the single-shield row lists all the pins it covers");
        // A second shield on the same wires clears the flag.
        Entity s2; s2.id = "s2"; s2.type = EntityType::Screen; s2.points = {{150, 0}, {150, 30}, {150, -20}};
        s2.meta.insert("catch", 2); s2.meta.insert("wires", wref);
        d.entities().push_back(s2);
        check(countKey("shieldpair") == 0, "validate: two shields on the wires clear the flag");
        // One twist sign over both wires -> one flagged row.
        Entity t1; t1.id = "t1"; t1.type = EntityType::Twist; t1.points = {{80, 0}, {80, 30}};
        t1.meta.insert("wires", wref);
        d.entities().push_back(t1);
        check(countKey("twistpair") == 1, "validate: a single twist sign is one flagged row");
    }

    // Validation: a shield whose drain is neither on a wire nor terminated is flagged;
    // dropping an X (termination) on the drain clears it.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d);
        Entity w; w.id = "w1"; w.type = EntityType::Wire; w.points = {{0, 0}, {200, 0}};
        d.entities().push_back(w);
        Entity s; s.id = "s1"; s.type = EntityType::Screen;
        s.points = {{50, 0}, {50, -40}}; // one joint + a free drain at (50,-40)
        s.meta.insert("catch", 1);
        s.meta.insert("entNum", 5);
        d.entities().push_back(s);
        auto has = [&](const char* k) {
            for (const auto& is : ed.validateSchematic())
                if (is.key.startsWith(QLatin1String(k))) return true;
            return false;
        };
        check(has("shielddrain"), "validate: a floating shield drain is flagged");
        Entity x; x.id = "x1"; x.type = EntityType::NotConnected; x.a = {50, -40};
        d.entities().push_back(x);
        check(!has("shielddrain"), "validate: terminating the drain clears the flag");
    }

    // Validation: a shield terminated by its OWN "Not Connected" X counts as terminated.
    // The X is drawn by the shield itself (no separate NotConnected entity), so the
    // coincidence tests can't see it and the rule has to read the flag.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d);
        Entity w; w.id = "w1"; w.type = EntityType::Wire; w.points = {{0, 0}, {200, 0}};
        d.entities().push_back(w);
        Entity s; s.id = "s1"; s.type = EntityType::Screen;
        s.points = {{50, 0}, {50, -40}};
        s.meta.insert("catch", 1);
        s.meta.insert("entNum", 5);
        d.entities().push_back(s);
        auto has = [&](const char* k) {
            for (const auto& is : ed.validateSchematic())
                if (is.key.startsWith(QLatin1String(k))) return true;
            return false;
        };
        check(has("shielddrain"), "validate: an unterminated shield drain is still flagged");
        for (auto& e : d.entities())
            if (e.type == EntityType::Screen) e.meta.insert("notConnected", true);
        check(!has("shielddrain"),
              "validate: a shield's own Not-Connected X marks its drain as terminated");
        // Turning it back off restores the flag (the X really is what cleared it).
        for (auto& e : d.entities())
            if (e.type == EntityType::Screen) e.meta.insert("notConnected", false);
        check(has("shielddrain"), "validate: clearing the X flags the drain again");
    }

    // Validation, wire ends: a bare end is UNCONNECTED; an X-terminated end is
    // deliberately open and is not reported; connecting to a shield drain also counts.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d);
        Entity w1; w1.id = "w1"; w1.type = EntityType::Wire; w1.points = {{0, 0}, {100, 0}};
        w1.meta.insert("entNum", 1);
        d.entities().push_back(w1);
        auto openEnds = [&] {
            int n = 0;
            for (const auto& is : ed.validateSchematic())
                if (is.key.startsWith(QLatin1String("openwire"))) ++n;
            return n;
        };
        auto anyMsg = [&] {
            for (const auto& is : ed.validateSchematic())
                if (is.key.startsWith(QLatin1String("openwire"))) return is.message;
            return QString();
        };
        check(openEnds() == 2, "validate-ends: both bare ends of a lone wire are reported");
        check(anyMsg().contains(QLatin1String("unconnected")) &&
                  !anyMsg().contains(QLatin1String("open (")),
              "validate-ends: a bare end is described as UNCONNECTED, not \"open\"");
        // X-terminate one end: deliberately open, so it drops out of the report.
        Entity x; x.id = "x1"; x.type = EntityType::NotConnected; x.a = {0, 0};
        d.entities().push_back(x);
        check(openEnds() == 1, "validate-ends: an X-terminated end is open, so not reported");
        // Run the other end onto a shield's drain: that is a real connection.
        Entity s; s.id = "s1"; s.type = EntityType::Screen;
        s.points = {{160, 40}, {100, 0}}; // one caught joint + drain exactly at w1's end
        s.meta.insert("catch", 1);
        d.entities().push_back(s);
        check(openEnds() == 0, "validate-ends: an end landing on a shield drain is connected");
    }

    // Bundle twist: type "bundle" is stored (a twist sign suitable for 2+ wires).
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        for (int i = 0; i < 3; ++i) {
            Entity w; w.id = QStringLiteral("w%1").arg(i); w.type = EntityType::Wire;
            w.points = {{0, i * 20.0}, {200, i * 20.0}};
            d.entities().push_back(w);
        }
        ed.beginTwist(QStringLiteral("bundle"));
        ed.onPointerPress({100, -10}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({100, 50}, Qt::LeftButton, Qt::NoModifier);
        const Entity* tw = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Twist) tw = &e;
        check(tw && tw->meta.value("twistType").toString() == "bundle" &&
                  tw->meta.value("wires").toArray().size() == 3,
              "twist-bundle: a bundle twist over 3 wires is placed");
    }

    // "Crossed" twist type + Remove-twist mode.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        Entity w1; w1.id = "w1"; w1.type = EntityType::Wire; w1.points = {{0, 0}, {200, 0}};
        Entity w2; w2.id = "w2"; w2.type = EntityType::Wire; w2.points = {{0, 30}, {200, 30}};
        d.entities().push_back(w1);
        d.entities().push_back(w2);
        ed.twistDiagAngle() = 20.0;
        ed.twistCapLen() = 12.0;
        ed.beginTwist(QStringLiteral("crossed"));
        ed.onPointerPress({100, -10}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({100, 40}, Qt::LeftButton, Qt::NoModifier);
        const Entity* tw = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Twist) tw = &e;
        check(tw && tw->meta.value("twistType").toString() == "crossed",
              "twist-crossed: a crossed twist is placed");
        check(tw && near(tw->meta.value("diagAngle").toDouble(), 20.0) &&
                  near(tw->meta.value("capLen").toDouble(), 12.0),
              "twist-crossed: diagonal angle and line length are stored");
    }

    // Crossed leaves wires continuous, so only the FIRST of several copies references the
    // wires (the rest are decorative) — unlike jog/type3 where every copy interrupts them.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        Entity w1; w1.id = "w1"; w1.type = EntityType::Wire; w1.points = {{0, 0}, {400, 0}};
        Entity w2; w2.id = "w2"; w2.type = EntityType::Wire; w2.points = {{0, 30}, {400, 30}};
        d.entities().push_back(w1);
        d.entities().push_back(w2);
        ed.twistAmount() = 3;
        ed.beginTwist(QStringLiteral("crossed"));
        ed.onPointerPress({100, -10}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({100, 40}, Qt::LeftButton, Qt::NoModifier);
        int crossedWithWires = 0;
        for (const auto& e : d.entities())
            if (e.type == EntityType::Twist && e.meta.value("wires").toArray().size() == 2)
                ++crossedWithWires;
        check(crossedWithWires == 1, "twist-crossed: only the first copy references the wires");
    }

    // Type 2 (jog): repeats + flat length stored, and Amount places several side by side.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        Entity w1; w1.id = "w1"; w1.type = EntityType::Wire; w1.points = {{0, 0}, {400, 0}};
        w1.meta.insert("wireGauge", QStringLiteral("18"));
        Entity w2; w2.id = "w2"; w2.type = EntityType::Wire; w2.points = {{0, 30}, {400, 30}};
        d.entities().push_back(w1);
        d.entities().push_back(w2);
        ed.twistRepeats() = 4;
        ed.twistFlatLen() = 10.0;
        ed.twistAmount() = 3;
        ed.beginTwist(QStringLiteral("jog"));
        check(ed.lastTwistType() == "jog", "twist-default: the chosen type becomes the default");
        ed.onPointerPress({100, -10}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({100, 40}, Qt::LeftButton, Qt::NoModifier);
        int twists = 0; const Entity* first = nullptr;
        for (const auto& e : d.entities())
            if (e.type == EntityType::Twist) { ++twists; if (!first) first = &e; }
        check(twists == 3, "twist-jog: Amount=3 places three twist marks");
        // The braid is stamped with the crossed wire's gauge so it draws at wire thickness.
        check(first && first->meta.value("wireGauge").toString() == "18",
              "twist-jog: marks carry the crossed wire's gauge (drawn at wire thickness)");
        check(first && first->meta.value("repeats").toInt() == 4 &&
                  near(first->meta.value("flatLen").toDouble(), 10.0),
              "twist-jog: repeats and flat length are stored");
        // Every jog copy must reference the wires it crosses, so the wire stops/restarts at
        // each braid's ends (not just the first). Each mark carries both wire ids.
        int jogWithWires = 0;
        for (const auto& e : d.entities())
            if (e.type == EntityType::Twist && e.meta.value("wires").toArray().size() == 2)
                ++jogWithWires;
        check(jogWithWires == 3, "twist-jog: every copy references its crossed wires");
        // Type 3 stores repeats but no flat length.
        Document d2; d2.entities().clear();
        Editor e2; e2.setDocument(&d2); e2.snapSettings().objectSnap = false;
        Entity a; a.id = "a"; a.type = EntityType::Wire; a.points = {{0, 0}, {200, 0}};
        Entity b; b.id = "b"; b.type = EntityType::Wire; b.points = {{0, 30}, {200, 30}};
        d2.entities().push_back(a); d2.entities().push_back(b);
        e2.twistRepeats() = 3;
        e2.beginTwist(QStringLiteral("type3"));
        e2.onPointerPress({100, -10}, Qt::LeftButton, Qt::NoModifier);
        e2.onPointerPress({100, 40}, Qt::LeftButton, Qt::NoModifier);
        const Entity* t3 = nullptr;
        for (const auto& e : d2.entities()) if (e.type == EntityType::Twist) t3 = &e;
        check(t3 && t3->meta.value("repeats").toInt() == 3 && !t3->meta.contains("flatLen"),
              "twist-type3: repeats stored, no flat length");
    }

    // Remove-twist: clicking the twist sign deletes it.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        Entity w1; w1.id = "w1"; w1.type = EntityType::Wire; w1.points = {{0, 0}, {200, 0}};
        Entity w2; w2.id = "w2"; w2.type = EntityType::Wire; w2.points = {{0, 30}, {200, 30}};
        d.entities().push_back(w1);
        d.entities().push_back(w2);
        ed.twistAmount() = 1;
        ed.beginTwist(QStringLiteral("crossed"));
        ed.onPointerPress({100, -10}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({100, 40}, Qt::LeftButton, Qt::NoModifier);
        const Entity* tw = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Twist) tw = &e;
        check(tw != nullptr, "twist-remove: a twist was placed to remove");
        ed.beginTwistRemove();
        check(ed.awaitingTwistRemove(), "twist-remove: armed after beginTwistRemove");
        ed.onPointerPress(tw->points.front(), Qt::LeftButton, Qt::NoModifier); // click the twist
        int twists = 0;
        for (const auto& e : d.entities()) if (e.type == EntityType::Twist) ++twists;
        check(twists == 0, "twist-remove: clicking the twist sign deletes it");
    }

    // A green crossing region that TOUCHES the crossed twist's line selects it.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        Entity w1; w1.id = "w1"; w1.type = EntityType::Wire; w1.points = {{0, 0}, {200, 0}};
        Entity w2; w2.id = "w2"; w2.type = EntityType::Wire; w2.points = {{0, 30}, {200, 30}};
        d.entities().push_back(w1);
        d.entities().push_back(w2);
        ed.beginTwist(QStringLiteral("crossed"));
        ed.onPointerPress({100, -10}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({100, 40}, Qt::LeftButton, Qt::NoModifier);
        QString twId;
        for (const auto& e : d.entities()) if (e.type == EntityType::Twist) twId = e.id;
        check(!twId.isEmpty(), "twist-touch-crossed: the crossed twist was placed");
        // Region box from a far-away empty point to a corner past the diagonal (crosses the
        // diagonal ~ (100,15)); the crossing must select the twist.
        ed.setTool(ToolId::Select);
        ed.onPointerPress({300, 45}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerRelease({300, 45}, Qt::LeftButton);
        ed.onPointerMove({95, 10});
        ed.onPointerPress({95, 10}, Qt::LeftButton, Qt::NoModifier);
        const auto& sel = ed.selection();
        check(std::find(sel.begin(), sel.end(), twId) != sel.end(),
              "twist-touch-crossed: touching the twist line selects it");
    }

    // A green region drawn TIGHTLY over just the twist selects the twist but NOT the wires
    // running under it (the wires' only contact with the region is inside the twist's band).
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        Entity w1; w1.id = "w1"; w1.type = EntityType::Wire; w1.points = {{0, 0}, {200, 0}};
        Entity w2; w2.id = "w2"; w2.type = EntityType::Wire; w2.points = {{0, 30}, {200, 30}};
        d.entities().push_back(w1);
        d.entities().push_back(w2);
        ed.beginTwist(QStringLiteral("crossed"));
        ed.onPointerPress({100, -10}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({100, 40}, Qt::LeftButton, Qt::NoModifier);
        QString twId;
        for (const auto& e : d.entities()) if (e.type == EntityType::Twist) twId = e.id;
        ed.setTool(ToolId::Select);
        // Tight crossing box around the twist only (x 90..110), inside the twist's band.
        // The two corners must clear the wires by more than the 10-unit pick tolerance,
        // or the clicks body-select the wires instead of starting a region.
        ed.onPointerPress({110, 45}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerRelease({110, 45}, Qt::LeftButton);
        ed.onPointerMove({90, -15});
        ed.onPointerPress({90, -15}, Qt::LeftButton, Qt::NoModifier);
        const auto& sel = ed.selection();
        const bool hasTwist = std::find(sel.begin(), sel.end(), twId) != sel.end();
        const bool hasW1 = std::find(sel.begin(), sel.end(), QString("w1")) != sel.end();
        const bool hasW2 = std::find(sel.begin(), sel.end(), QString("w2")) != sel.end();
        check(hasTwist && !hasW1 && !hasW2,
              "twist-region-only: a tight region over the twist selects it, not the wires");
    }

    // Twist Tab-freeze: the crossing end pins, then Enter places at the frozen point; Esc
    // while armed cancels the whole twist function and returns to Select.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        Entity w1; w1.id = "w1"; w1.type = EntityType::Wire; w1.points = {{0, 0}, {200, 0}};
        Entity w2; w2.id = "w2"; w2.type = EntityType::Wire; w2.points = {{0, 30}, {200, 30}};
        d.entities().push_back(w1);
        d.entities().push_back(w2);
        ed.beginTwist(QStringLiteral("crossed"));
        ed.onPointerPress({100, -10}, Qt::LeftButton, Qt::NoModifier); // first end
        ed.onPointerMove({100, 40});                                   // drag across
        ed.freezeTwistPreview();
        check(ed.twistFrozen(), "twist-freeze: Tab pins the crossing-line end");
        // A click while frozen must NOT place the twist — it releases the freeze and goes
        // back to placing (the click becomes the crossing line's new first end).
        ed.onPointerPress({140, -10}, Qt::LeftButton, Qt::NoModifier);
        int afterClick = 0;
        for (const auto& e : d.entities()) if (e.type == EntityType::Twist) ++afterClick;
        check(afterClick == 0 && !ed.twistFrozen() && !ed.twistArmType().isEmpty(),
              "twist-freeze: a click releases the freeze instead of placing the twist");
        // Re-freeze from the restarted line, then Enter places it.
        ed.onPointerMove({140, 40});
        ed.freezeTwistPreview();
        check(ed.twistFrozen(), "twist-freeze: the restarted line can be frozen again");
        ed.onKey(Qt::Key_Return, Qt::NoModifier); // Enter places at the frozen end
        int placed = 0;
        for (const auto& e : d.entities()) if (e.type == EntityType::Twist) ++placed;
        check(placed == 1 && !ed.twistFrozen(), "twist-freeze: Enter places the frozen twist");
        // Esc while armed cancels and returns to Select (no twist added).
        ed.beginTwist(QStringLiteral("crossed"));
        ed.onPointerPress({50, -10}, Qt::LeftButton, Qt::NoModifier);
        ed.onKey(Qt::Key_Escape, Qt::NoModifier);
        int after = 0;
        for (const auto& e : d.entities()) if (e.type == EntityType::Twist) ++after;
        check(after == 1 && ed.twistArmType().isEmpty(),
              "twist-esc: Esc cancels the armed twist (nothing placed, back to Select)");
    }

    // Frozen shield + Enter places it.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        Entity w1; w1.id = "w1"; w1.type = EntityType::Wire; w1.points = {{0, 0}, {200, 0}};
        Entity w2; w2.id = "w2"; w2.type = EntityType::Wire; w2.points = {{0, 30}, {200, 30}};
        d.entities().push_back(w1);
        d.entities().push_back(w2);
        ed.beginScreen();
        ed.onPointerPress({100, -10}, Qt::LeftButton, Qt::NoModifier); // first crossing point
        ed.onPointerMove({100, 40});                                   // hover across the wires
        ed.freezeScreenPreview();                                      // Tab
        ed.onKey(Qt::Key_Return, Qt::NoModifier);                      // Enter places it
        int shields = 0;
        for (const auto& e : d.entities()) if (e.type == EntityType::Screen) ++shields;
        check(shields == 1, "shield-freeze-enter: Enter places the frozen shield");
    }

    // Scaling a shield resizes its ellipse (rx/ry meta), not just its joint positions —
    // so a shield scales with the wires/harness when they are scaled together.
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
        Entity* sc = nullptr;
        for (auto& e : d.entities()) if (e.type == EntityType::Screen) sc = &e;
        const double rx0 = sc->meta.value("rx").toDouble();
        const double ry0 = sc->meta.value("ry").toDouble();
        // Drain distance from the shield centre (= ry + leg) before scaling.
        const Vec2 c0{(sc->points.front().x + sc->points[1].x) / 2.0,
                      (sc->points.front().y + sc->points[1].y) / 2.0};
        const double legDist0 = distance(sc->points.back(), c0);
        scaleEntity(*sc, {100, 15}, 2.0);
        check(near(sc->meta.value("rx").toDouble(), rx0 * 2) &&
                  near(sc->meta.value("ry").toDouble(), ry0 * 2),
              "shield-scale: scaling the shield doubles its ellipse radii");
        const Vec2 c1{(sc->points.front().x + sc->points[1].x) / 2.0,
                      (sc->points.front().y + sc->points[1].y) / 2.0};
        const double legDist1 = distance(sc->points.back(), c1);
        check(near(legDist1, legDist0 * 2),
              "shield-scale: the drain leg scales with the shield");
    }

    // Fitting to the crossed wires UNIFORMLY scales the shield: it grows to span the
    // wires but keeps its aspect (ry/rx == ratio) — the structure is unchanged, not
    // re-proportioned by the wire count.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        for (int i = 0; i < 4; ++i) {
            Entity w; w.id = QStringLiteral("w%1").arg(i); w.type = EntityType::Wire;
            w.points = {{0, i * 10.0}, {200, i * 10.0}};
            d.entities().push_back(w);
        }
        ed.beginScreen();
        ed.onPointerPress({100, -10}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({100, 40}, Qt::LeftButton, Qt::NoModifier);
        const Entity* sc = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Screen) sc = &e;
        check(sc && sc->meta.value("catch").toInt() == 4, "shield-uniform: caught all 4 wires");
        // ry = span/2 + 30% of the wire gap = 15 + 0.3*10 = 18; rx follows the ratio.
        const double rx = sc->meta.value("rx").toDouble();
        const double ry = sc->meta.value("ry").toDouble();
        check(near(ry, 18.0) && near(ry / rx, ed.screenRatio()),
              "shield-uniform: fit scales uniformly, keeping ry/rx == ratio");
    }

    // Shield joints sit ON the ellipse boundary (not at the raw, possibly-centred wire
    // crossing point) — grabbing the joint at its projected position works, while the
    // old raw centre-line position no longer has a grip there.
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
        QString scId; const Entity* sc = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Screen) { scId = e.id; sc = &e; }
        check(sc, "shield-joint-ellipse: the shield was placed");
        const double rx = sc->meta.value("rx").toDouble();
        // Each wire now shows TWO joints, on both ellipse edges at its height: pin 0
        // (y=0, on the centre line) has joints at (cx-rx, 0) and (cx+rx, 0). Grabbing
        // either one drags that wire's connection point; here we grab the LEFT one.
        ed.setTool(ToolId::Select);
        ed.selectById(scId);
        ed.onPointerPress({100.0 - rx, 0.0}, Qt::LeftButton, Qt::NoModifier); // grab left joint
        ed.onPointerPress({100.0 - rx, 60.0}, Qt::LeftButton, Qt::NoModifier); // drag it
        const Entity* sc2 = nullptr;
        for (const auto& e : d.entities()) if (e.id == scId) sc2 = &e;
        check(sc2 && near(sc2->points[0].y, 60.0),
              "shield-joint-ellipse: grabbing the left ellipse-edge joint moves the wire");
    }

    // Touch-select must reach the ACTUAL ellipse/leg, not the loose bounding box: a box
    // only over a bbox corner (outside the true ellipse curve and away from the leg)
    // must NOT select the shield.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        Entity w1; w1.id = "w1"; w1.type = EntityType::Wire; w1.points = {{0, 0}, {200, 0}};
        Entity w2; w2.id = "w2"; w2.type = EntityType::Wire; w2.points = {{0, 30}, {200, 30}};
        d.entities().push_back(w1);
        d.entities().push_back(w2);
        ed.screenLegDir() = QStringLiteral("bottom"); // leg goes down, away from this box
        // Unlock rx + ratio so the large radius auto-fits (ry=21, rx=12) — the compact
        // geometry this bbox-corner test was written against (the all-locked default is
        // deliberately larger, which would put this corner inside the ellipse).
        ed.screenRxLock() = false;
        ed.screenRatioLock() = false;
        ed.beginScreen();
        ed.onPointerPress({100, -10}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({100, 40}, Qt::LeftButton, Qt::NoModifier);
        QString scId;
        for (const auto& e : d.entities()) if (e.type == EntityType::Screen) scId = e.id;
        check(!scId.isEmpty(), "shield-touch-precise: the shield was placed");
        // Start the drag from a point FAR from anything (so the first click can't
        // accidentally direct-hit the shield/wires and skip the region-select path —
        // once RectPending, the 2nd click's position is used purely as the opposite
        // corner and is never hit-tested). The resulting box's near corner sits right
        // at the shield's bbox top-right corner (~112,36) — inside the loose bounding
        // box but outside the true ellipse curve (checked: the curve only reaches
        // y≈30.6 at x=108, well under this box's y0=33).
        ed.setTool(ToolId::Select);
        ed.onPointerPress({300, 45}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerRelease({300, 45}, Qt::LeftButton);
        ed.onPointerMove({112, 36});   // bbox top-right corner, outside the ellipse curve
        ed.onPointerPress({112, 36}, Qt::LeftButton, Qt::NoModifier);
        const auto& sel = ed.selection();
        check(std::find(sel.begin(), sel.end(), scId) == sel.end(),
              "shield-touch-precise: a box over only the bbox corner does NOT select the shield");
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

    // Screen freeze (Tab): pins the crossing-line end so a click anywhere commits at
    // the frozen point, and the leg direction picks which side the drain exits.
    {
        Document d; d.entities().clear();
        Editor ed; ed.setDocument(&d); ed.snapSettings().objectSnap = false;
        Entity w1; w1.id = "w1"; w1.type = EntityType::Wire; w1.points = {{0, 0}, {200, 0}};
        Entity w2; w2.id = "w2"; w2.type = EntityType::Wire; w2.points = {{0, 30}, {200, 30}};
        d.entities().push_back(w1);
        d.entities().push_back(w2);
        ed.screenLegDir() = QStringLiteral("top");
        ed.beginScreen();
        check(ed.screenArmed(), "screen-freeze: beginScreen arms placement");
        ed.onPointerPress({100, -10}, Qt::LeftButton, Qt::NoModifier); // start of crossing line
        ed.onPointerMove({100, 40});   // drag toward the intended end
        ed.freezeScreenPreview();      // Tab: pin the end at (100,40)
        check(ed.screenFrozen(), "screen-freeze: Tab freezes the preview");
        ed.onPointerMove({500, 500});  // move far away — must NOT change the frozen end
        ed.onPointerPress({-999, -999}, Qt::LeftButton, Qt::NoModifier); // click anywhere commits
        const Entity* sc = nullptr;
        for (const auto& e : d.entities()) if (e.type == EntityType::Screen) sc = &e;
        check(sc && sc->meta.value("wires").toArray().size() == 2,
              "screen-freeze: click anywhere (not just at the frozen point) commits it");
        check(sc && sc->meta.value("legDir").toString() == QLatin1String("top"),
              "screen-freeze: the leg direction is stored on the placed screen");
        check(!ed.screenArmed(), "screen-freeze: placing disarms the screen tool");
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

    // ---- Wire status: shielded / twisted, and what each end is connected to ----
    {
        Document d;
        d.entities().clear();
        Entity w;
        w.id = "w1";
        w.type = EntityType::Wire;
        w.points = {{0, 0}, {100, 0}};
        d.entities().push_back(w);
        Editor ed;
        ed.setDocument(&d);
        ed.snapSettings().objectSnap = false;
        ed.snapSettings().gridSnap = false;

        check(!ed.wireIsShielded("w1") && !ed.wireIsTwisted("w1"),
              "wire status: a bare wire is neither shielded nor twisted");
        const Entity* wire = nullptr;
        for (const auto& e : d.entities())
            if (e.id == QLatin1String("w1")) wire = &e;
        check(wire && ed.wireEndRef(*wire, true) == QLatin1String("unconnected") &&
                  ed.wireEndRef(*wire, false) == QLatin1String("unconnected"),
              "wire status: an end touching nothing reads 'unconnected'");

        // An X (not-connected) marker on the far end makes it deliberately open.
        Entity x;
        x.id = "x1";
        x.type = EntityType::NotConnected;
        x.a = {100, 0};
        d.entities().push_back(x);
        // A splice on the near end joins it to other wires.
        Entity sp;
        sp.id = "s1";
        sp.type = EntityType::Splice;
        sp.a = {0, 0};
        d.entities().push_back(sp);
        for (const auto& e : d.entities())
            if (e.id == QLatin1String("w1")) wire = &e;
        check(wire && ed.wireEndRef(*wire, false) == QLatin1String("open"),
              "wire status: an X-terminated end reads 'open'");
        check(wire && ed.wireEndRef(*wire, true) == QLatin1String("splice"),
              "wire status: a spliced end reads 'splice'");

        // A shield / twist records the wires it covers, which is what the status reads.
        Entity screen;
        screen.id = "sc1";
        screen.type = EntityType::Screen;
        QJsonArray covered;
        covered.append(QStringLiteral("w1"));
        screen.meta.insert("wires", covered);
        d.entities().push_back(screen);
        Entity tw;
        tw.id = "tw1";
        tw.type = EntityType::Twist;
        tw.meta.insert("wires", covered);
        d.entities().push_back(tw);
        check(ed.wireIsShielded("w1"), "wire status: a shield covering it reports shielded");
        check(ed.wireIsTwisted("w1"), "wire status: a twist covering it reports twisted");
        check(!ed.wireIsShielded("other"), "wire status: an uncovered wire is not shielded");
    }

    // ---- Wire markings: colour / gauge text along the run ----
    {
        Entity w;
        w.type = EntityType::Wire;
        w.points = {{0, 0}, {100, 0}};
        w.meta.insert("wireColor", QStringLiteral("RD"));
        w.meta.insert("wireGauge", QStringLiteral("0.5"));

        check(wireAnnots(w).empty(), "wire marking: nothing is drawn until switched on");
        w.meta.insert("showColorName", true);
        check(wireAnnotColorText(w) == QLatin1String("RD"),
              "wire marking: the colour defaults to its 2-letter code");
        w.meta.insert("colorNameMode", QStringLiteral("full"));
        check(wireAnnotColorText(w) == QLatin1String("Red"),
              "wire marking: full mode spells the colour out");
        check(wireAnnotGaugeText(w).isEmpty(),
              "wire marking: the gauge stays off independently of the colour");
        w.meta.insert("showGaugeName", true);
        check(wireAnnotGaugeText(w).startsWith(QLatin1String("0.5")),
              "wire marking: the gauge switches on on its own");

        // One instance sits mid-run; raising the count spreads them evenly.
        auto marks = wireAnnots(w);
        check(marks.size() == 1 && near(marks[0].pos.x, 50.0) && near(marks[0].t, 0.5),
              "wire marking: a single marking sits at the middle of the run");
        w.meta.insert("annCount", 3);
        marks = wireAnnots(w);
        check(marks.size() == 3 && near(marks[0].pos.x, 100.0 / 6.0) &&
                  near(marks[2].pos.x, 500.0 / 6.0),
              "wire marking: several markings spread evenly along the run");

        // The text reads along the wire: a vertical run turns the direction with it.
        Entity v;
        v.type = EntityType::Wire;
        v.points = {{0, 0}, {0, 80}};
        v.meta.insert("wireColor", QStringLiteral("BK"));
        v.meta.insert("showColorName", true);
        const auto vm = wireAnnots(v);
        check(vm.size() == 1 && near(vm[0].dir.x, 0.0) && near(vm[0].dir.y, 1.0),
              "wire marking: a vertical wire's marking runs with the wire");
        // A run heading left/down is flipped so the text never reads upside down.
        Entity back;
        back.type = EntityType::Wire;
        back.points = {{100, 0}, {0, 0}};
        back.meta.insert("wireColor", QStringLiteral("BK"));
        back.meta.insert("showColorName", true);
        const auto bm = wireAnnots(back);
        check(bm.size() == 1 && near(bm[0].dir.x, 1.0),
              "wire marking: a right-to-left run still reads left-to-right");

        // Positions are kept per instance, so dragging one and then adding more keeps it.
        std::vector<double> ts = {0.1, 0.9, 0.5};
        setWireAnnotFractions(w, ts);
        const auto kept = wireAnnotFractions(w, 4);
        check(kept.size() == 4 && near(kept[0], 0.1) && near(kept[1], 0.9),
              "wire marking: dragged positions survive a change of count");
        check(near(wireFractionNearest(w, {25, 5}), 0.25),
              "wire marking: the nearest fraction projects onto the run");
    }

    // ---- Wire label: placing one attaches it to the wire and fills the property ----
    {
        Document d;
        d.entities().clear();
        Entity w;
        w.id = "w1";
        w.type = EntityType::Wire;
        w.points = {{0, 0}, {100, 0}};
        w.meta.insert("entNum", 7);
        d.entities().push_back(w);
        Editor ed;
        ed.setDocument(&d);
        ed.snapSettings().objectSnap = false;
        ed.snapSettings().gridSnap = false;
        ed.snapSettings().ortho = false;
        ed.snapSettings().polar = false;
        ed.wireNet() = QStringLiteral("PWR-A");
        ed.setTool(ToolId::NetLabel);
        ed.onPointerPress({30, 0}, Qt::LeftButton, Qt::NoModifier); // click ON the wire

        const Entity* lab = nullptr;
        const Entity* wire = nullptr;
        for (const auto& e : d.entities()) {
            if (e.type == EntityType::NetLabel) lab = &e;
            if (e.id == QLatin1String("w1")) wire = &e;
        }
        check(lab && lab->meta.value("wireId").toString() == QLatin1String("w1"),
              "wire label: placing it on a wire attaches it to that wire");
        check(lab && near(lab->meta.value("annT").toDouble(), 0.3),
              "wire label: it remembers where along the wire it sits");
        check(wire && wire->meta.value("wireLabel").toString() == QLatin1String("PWR-A"),
              "wire label: the wire's own label property is filled in");
        check(lab && lab->meta.value("text").toString() == QLatin1String("PWR-A"),
              "wire label: the label shows that text");

        // Editing the label's text writes back to the wire.
        const QString labId = lab->id;
        ed.setWireLabelText(labId, QStringLiteral("W12"));
        for (const auto& e : d.entities())
            if (e.id == QLatin1String("w1")) wire = &e;
        check(wire && wire->meta.value("wireLabel").toString() == QLatin1String("W12"),
              "wire label: editing the label updates the wire property");
        // ...and editing the wire property updates the label.
        ed.selectById("w1");
        ed.setSelectionWireLabel(QStringLiteral("GND-3"));
        for (const auto& e : d.entities())
            if (e.type == EntityType::NetLabel) lab = &e;
        check(lab && lab->meta.value("text").toString() == QLatin1String("GND-3"),
              "wire label: editing the wire property updates the attached label");

        // A label placed away from any wire stays free.
        ed.setTool(ToolId::NetLabel);
        ed.onPointerPress({30, 500}, Qt::LeftButton, Qt::NoModifier);
        int attached = 0, free = 0;
        for (const auto& e : d.entities())
            if (e.type == EntityType::NetLabel) {
                if (e.meta.value("wireId").toString().isEmpty()) ++free;
                else ++attached;
            }
        check(attached == 1 && free == 1,
              "wire label: one placed off any wire is left unattached");
    }

    // ---- Fan-out: a turn comes out pitched, not stacked ----
    {
        // Route: out to the right, then up. Pins are spaced 10 apart vertically, so the
        // lateral offset is across the first (horizontal) run.
        const std::vector<Vec2> route = {{0, 0}, {50, 0}, {50, 60}};
        const std::vector<Vec2> anchorRoute = Editor::fanRoute(route, {0, 0});
        check(anchorRoute.size() == 3 && near(anchorRoute[0].x, 0.0) && near(anchorRoute[0].y, 0.0),
              "fan-out: the anchor wire follows the drafted route unchanged");
        const std::vector<Vec2> lane = Editor::fanRoute(route, {0, -10});
        check(lane.size() == 3, "fan-out: a fanned wire has the same vertex count");
        check(near(lane[0].x, 0.0) && near(lane[0].y, -10.0),
              "fan-out: it starts exactly on its own pin");
        // The horizontal run keeps the pin pitch...
        check(near(lane[1].y, -10.0), "fan-out: the first run stays one pitch clear");
        // ...and so does the VERTICAL run after the turn: the corner moves along by the
        // pitch, so the run after it is a pitch clear of the anchor's (x = 60, not 50).
        // A plain translation would have left both at x = 50, one on top of the other.
        check(near(lane[1].x, 60.0) && near(lane[2].x, 60.0),
              "fan-out: the turn is pitched, so the run after it does not overlap");
        // The wire on the OUTSIDE of the bend turns later, the one on the inside turns
        // earlier — which is what keeps the lanes from crossing each other.
        const std::vector<Vec2> inner = Editor::fanRoute(route, {0, 10});
        check(inner.size() == 3 && near(inner[1].y, 10.0) && near(inner[1].x, 40.0),
              "fan-out: the wire inside the bend turns one pitch earlier");
        // Every lane stays exactly one pitch from the next, before and after the turn.
        check(near(std::abs(lane[1].x - anchorRoute[1].x), 10.0) &&
                  near(std::abs(anchorRoute[1].x - inner[1].x), 10.0),
              "fan-out: neighbouring lanes stay one pitch apart through the turn");
    }

    // ---- Routing clearance: a run is held clear of its neighbours ----
    {
        Document d;
        d.entities().clear();
        Entity w;
        w.id = "w1";
        w.type = EntityType::Wire;
        w.points = {{0, 0}, {100, 0}}; // an existing horizontal run at y = 0
        d.entities().push_back(w);
        Editor ed;
        ed.setDocument(&d);
        ed.wireClearanceOn() = true;
        // The clearance comes from the Pitch rule; pin it to 10 with a custom value here.
        ed.wireGapRule() = QStringLiteral("custom");
        ed.wireGapCustom() = 10.0;

        // A parallel run drawn too close is pushed out to the clearance.
        const Vec2 pushed = ed.applyWireClearance({0, 3}, {80, 3});
        check(near(pushed.x, 80.0) && near(pushed.y, 10.0),
              "clearance: a parallel run too close is pushed out to the clearance");
        // Below the neighbour it is pushed the other way.
        const Vec2 down = ed.applyWireClearance({0, -2}, {80, -2});
        check(near(down.y, -10.0), "clearance: it is pushed to the side it came from");
        // Already clear: left exactly where it was.
        const Vec2 clear = ed.applyWireClearance({0, 40}, {80, 40});
        check(near(clear.y, 40.0), "clearance: a run already clear is not moved");
        // Perpendicular runs are not neighbours — crossing is fine.
        const Vec2 cross = ed.applyWireClearance({50, -30}, {50, 30});
        check(near(cross.x, 50.0) && near(cross.y, 30.0),
              "clearance: a run crossing at 90 degrees is left alone");
        // No lengthwise overlap => not a neighbour either.
        const Vec2 beyond = ed.applyWireClearance({200, 2}, {300, 2});
        check(near(beyond.y, 2.0), "clearance: a run that does not overlap is left alone");
        // Switched off, nothing moves.
        ed.wireClearanceOn() = false;
        check(near(ed.applyWireClearance({0, 3}, {80, 3}).y, 3.0),
              "clearance: the rule can be switched off");
    }

    // ---- Bundle pull: Shift drags every selected wire, release drags only the held one ----
    {
        Document d;
        d.entities().clear();
        // Three parallel wires, each out to the right then up (a fan-out shape): the
        // knees are index 1, the vertical run is segment 1.
        for (int i = 0; i < 3; ++i) {
            Entity w;
            w.id = QStringLiteral("w%1").arg(i);
            w.type = EntityType::Wire;
            const double y = -10.0 * i;
            w.points = {{0, y}, {50 + 10.0 * i, y}, {50 + 10.0 * i, 80}};
            d.entities().push_back(w);
        }
        Editor ed;
        ed.setDocument(&d);
        ed.snapSettings().objectSnap = false;
        ed.snapSettings().gridSnap = false;
        ed.snapSettings().ortho = false;
        ed.snapSettings().polar = false;
        ed.selectById("w0");
        ed.selectById("w1", /*additive=*/true);
        ed.selectById("w2", /*additive=*/true);

        auto knee = [&d](const char* id) {
            for (const auto& e : d.entities())
                if (e.id == QLatin1String(id)) return e.points[1];
            return Vec2{0, 0};
        };
        const Vec2 knee0Start = knee("w0");
        const Vec2 knee1Start = knee("w1");

        // Grab w0's knee and drag it. With Shift NOT held, only w0 moves.
        ed.onPointerPress(knee0Start, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({knee0Start.x + 15.0, knee0Start.y});
        check(!near(knee("w0").x, knee0Start.x),
              "bundle pull: dragging the knee moves the held wire");
        check(near(knee("w1").x, knee1Start.x) && near(knee("w1").y, knee1Start.y),
              "bundle pull: without Shift the other selected wires stay put");

        // Now the Shift branch: the peers move by the held wire's TOTAL travel since the
        // drag began, which is what keeps the bundle's pitch and shape.
        const Vec2 heldNow = knee("w0");
        const Vec2 travel = heldNow - knee0Start;
        ed.pullSelectedWireBundle(/*shiftHeld=*/true);
        check(near(knee("w1").x, knee1Start.x + travel.x) &&
                  near(knee("w1").y, knee1Start.y + travel.y),
              "bundle pull: with Shift the other selected wires follow by the same amount");
        check(near(knee("w0").x, heldNow.x),
              "bundle pull: the held wire is not moved twice");
        // The lanes are still a pitch apart — the bundle kept its shape.
        check(near(knee("w1").x - knee("w0").x, knee1Start.x - knee0Start.x),
              "bundle pull: the bundle keeps its pitch");

        // Repeated steps stay absolute rather than accumulating twice over.
        ed.pullSelectedWireBundle(/*shiftHeld=*/true);
        check(near(knee("w1").x, knee1Start.x + travel.x),
              "bundle pull: following is absolute, not applied twice");

        // Releasing Shift returns every wire EXCEPT the held one to where it started.
        ed.pullSelectedWireBundle(/*shiftHeld=*/false);
        check(near(knee("w1").x, knee1Start.x) && near(knee("w1").y, knee1Start.y),
              "bundle pull: releasing Shift returns the other wires to their initial place");
        check(near(knee("w0").x, heldNow.x) && near(knee("w0").y, heldNow.y),
              "bundle pull: the held wire keeps its own movement on release");
        // Pressing Shift again picks the bundle back up.
        ed.pullSelectedWireBundle(/*shiftHeld=*/true);
        check(near(knee("w1").x, knee1Start.x + travel.x),
              "bundle pull: Shift can be pressed again mid-drag to re-join the bundle");
    }

    // ---- Moving a connector previews its wires as well ----
    {
        Document d;
        d.entities().clear();
        ConnectorSpec spec = defaultConnectorSpec(2);
        spec.pitch = 20.0;
        Entity conn;
        conn.id = "c1";
        conn.type = EntityType::Connector;
        conn.a = {0, 0};
        writeConnectorSpec(conn.meta, spec);
        d.entities().push_back(conn);
        const ConnectorGeometry g = connectorGeometry(conn.a, spec);
        // A wire off pin 1, and one that touches nothing.
        Entity attached;
        attached.id = "wa";
        attached.type = EntityType::Wire;
        attached.points = {g.pinEnd[0], {g.pinEnd[0].x + 120, g.pinEnd[0].y}, {g.pinEnd[0].x + 120, -90}};
        d.entities().push_back(attached);
        Entity loose;
        loose.id = "wl";
        loose.type = EntityType::Wire;
        loose.points = {{0, -300}, {100, -300}};
        d.entities().push_back(loose);

        Editor ed;
        ed.setDocument(&d);
        ed.snapSettings().objectSnap = false;
        ed.snapSettings().gridSnap = false;
        ed.snapSettings().ortho = false;
        ed.snapSettings().polar = false;
        ed.selectById("c1");
        // Grab the connector by one of its corner move handles.
        ed.onPointerPress(g.rectMin, Qt::LeftButton, Qt::NoModifier);
        const auto& ghost = ed.dragGhost();
        bool ghostHasConn = false, ghostHasAttached = false, ghostHasLoose = false;
        for (const Entity& e : ghost) {
            if (e.id == QLatin1String("c1")) ghostHasConn = true;
            if (e.id == QLatin1String("wa")) ghostHasAttached = true;
            if (e.id == QLatin1String("wl")) ghostHasLoose = true;
        }
        check(ghostHasConn, "connector preview: the connector itself is ghosted");
        check(ghostHasAttached,
              "connector preview: its attached wires are ghosted too, so their move previews");
        check(!ghostHasLoose, "connector preview: unrelated wires are not ghosted");

        // Dragging really does move the attached wire live (and leaves the loose one).
        ed.onPointerMove({g.rectMin.x + 40, g.rectMin.y + 25});
        const Entity* wa = nullptr;
        const Entity* wl = nullptr;
        for (const auto& e : d.entities()) {
            if (e.id == QLatin1String("wa")) wa = &e;
            if (e.id == QLatin1String("wl")) wl = &e;
        }
        check(wa && !near(wa->points[0].x, g.pinEnd[0].x),
              "connector preview: the attached wire follows during the drag");
        check(wa && near(wa->points[0].y, wa->points[1].y),
              "connector preview: and keeps its right angle while previewing");
        check(wl && near(wl->points[0].y, -300.0),
              "connector preview: the unrelated wire is untouched");
    }

    // ---- Routing clearance follows the PITCH RULE, not a separate distance ----
    {
        Document d;
        d.entities().clear();
        // A connector with a 22-unit pin pitch, and an existing wire to keep clear of.
        ConnectorSpec spec = defaultConnectorSpec(4);
        spec.pitch = 22.0;
        spec.pinLength = 30.0;
        Entity conn;
        conn.id = "c1";
        conn.type = EntityType::Connector;
        conn.a = {0, 0};
        writeConnectorSpec(conn.meta, spec);
        d.entities().push_back(conn);
        const ConnectorGeometry g = connectorGeometry(conn.a, spec);

        Editor ed;
        ed.setDocument(&d);
        ed.snapSettings().objectSnap = false;
        ed.snapSettings().gridSnap = false;
        ed.snapSettings().ortho = false;
        ed.snapSettings().polar = false;
        ed.wireClearanceOn() = true;
        ed.wireGapRule() = QStringLiteral("pitch");

        // Before any wire is pulled, the clearance falls back to the connector default.
        ed.connectorSpec().pitch = 15.0;
        check(near(ed.routingClearance(), 15.0),
              "routing clearance: with no run started it uses the connector default pitch");

        // Pulling a wire off pin 1 adopts THAT connector's pitch as the clearance.
        ed.setTool(ToolId::Wire);
        ed.onPointerPress(g.pinEnd[0], Qt::LeftButton, Qt::NoModifier);
        check(near(ed.routingClearance(), 22.0),
              "routing clearance: a run pulled off a connector uses its pin pitch");

        // ...and the run is held a full pitch off a neighbouring run, not some other gap.
        Entity w;
        w.id = "w1";
        w.type = EntityType::Wire;
        w.points = {{g.pinEnd[0].x, 0}, {g.pinEnd[0].x + 200, 0}};
        d.entities().push_back(w);
        const Vec2 pushed = ed.applyWireClearance({g.pinEnd[0].x, 4}, {g.pinEnd[0].x + 150, 4});
        check(near(pushed.y, 22.0),
              "routing clearance: the neighbouring run is cleared by one pitch");

        // Custom overrides it with an explicit distance.
        ed.wireGapRule() = QStringLiteral("custom");
        ed.wireGapCustom() = 8.0;
        check(near(ed.routingClearance(), 8.0),
              "routing clearance: Custom uses the entered distance instead");
    }

    // ---- Dragging a connector keeps its wires' 90 degree knees ----
    {
        // A wire leaving a pin horizontally, turning down, then right again.
        Entity w;
        w.type = EntityType::Wire;
        w.points = {{0, 0}, {100, 0}, {100, -80}, {200, -80}};

        // Move the connector right and up: the horizontal run absorbs the x, the knee
        // shifts in y so that run stays horizontal, and everything past the knee stays put.
        const int knee = dragWireEndKeepingRightAngle(w, 0, {30, 15});
        check(knee == 1, "connector drag: the knee beside the moved end is the one adjusted");
        check(near(w.points[0].x, 30.0) && near(w.points[0].y, 15.0),
              "connector drag: the attached end follows the pin");
        check(near(w.points[1].y, w.points[0].y),
              "connector drag: the run off the pin stays horizontal");
        check(near(w.points[1].x, 100.0),
              "connector drag: the move along the run is absorbed by its length");
        check(near(w.points[2].x, 100.0) && near(w.points[2].y, -80.0) &&
                  near(w.points[3].x, 200.0) && near(w.points[3].y, -80.0),
              "connector drag: the rest of the wire is left where it was");
        // The next run is still vertical (its length simply changed).
        check(near(w.points[1].x, w.points[2].x),
              "connector drag: the following run stays vertical");

        // A wire leaving a pin VERTICALLY is squared the other way round.
        Entity v;
        v.type = EntityType::Wire;
        v.points = {{0, 0}, {0, -60}, {90, -60}};
        const int vknee = dragWireEndKeepingRightAngle(v, 0, {12, -7});
        check(vknee == 1 && near(v.points[1].x, v.points[0].x) && near(v.points[1].y, -60.0),
              "connector drag: a vertical run off the pin stays vertical");

        // A diagonal run has no right angle to hold, so only the end moves.
        Entity diag;
        diag.type = EntityType::Wire;
        diag.points = {{0, 0}, {50, 50}, {120, 50}};
        const int dknee = dragWireEndKeepingRightAngle(diag, 0, {10, 10});
        check(dknee == -1 && near(diag.points[1].x, 50.0) && near(diag.points[1].y, 50.0),
              "connector drag: a diagonal run is left alone");

        // The far end works the same way.
        Entity fromEnd;
        fromEnd.type = EntityType::Wire;
        fromEnd.points = {{0, 0}, {100, 0}, {100, -80}, {200, -80}};
        const int fknee = dragWireEndKeepingRightAngle(fromEnd, 3, {25, 10});
        check(fknee == 2 && near(fromEnd.points[2].y, fromEnd.points[3].y) &&
                  near(fromEnd.points[2].x, 100.0),
              "connector drag: the far end squares its own knee");

        // A straight two-point wire would go DIAGONAL when its end moves across the run, so
        // it is given a knee instead of being left as a slope.
        Entity straight;
        straight.type = EntityType::Wire;
        straight.points = {{0, 0}, {100, 0}};
        const int newKnee = dragWireEndKeepingRightAngle(straight, 0, {10, 20});
        check(newKnee == 1 && straight.points.size() == 4,
              "connector drag: a straight run gains a knee rather than going diagonal");
        check(near(straight.points[0].x, 10.0) && near(straight.points[0].y, 20.0),
              "connector drag: the moved end followed the pin");
        check(near(straight.points[3].x, 100.0) && near(straight.points[3].y, 0.0),
              "connector drag: the far end stayed put");
        for (size_t i = 1; i < straight.points.size(); ++i) {
            const Vec2 p = straight.points[i - 1], q = straight.points[i];
            check(std::abs(p.x - q.x) < 1e-6 || std::abs(p.y - q.y) < 1e-6,
                  "connector drag: the new route is all 0/90");
        }
        // Leaving the pin horizontally before means leaving it horizontally after.
        check(near(straight.points[0].y, straight.points[1].y),
              "connector drag: the run still leaves the pin the way it did");
        // A move ALONG the run keeps it straight — no pointless knee.
        Entity along;
        along.type = EntityType::Wire;
        along.points = {{0, 0}, {100, 0}};
        check(dragWireEndKeepingRightAngle(along, 0, {10, 0}) == -1 && along.points.size() == 2,
              "connector drag: a move along the run leaves it straight");
        // The far end of a straight run is squared the same way.
        Entity farEnd;
        farEnd.type = EntityType::Wire;
        farEnd.points = {{0, 0}, {100, 0}};
        check(dragWireEndKeepingRightAngle(farEnd, 1, {10, -30}) == 2 &&
                  farEnd.points.size() == 4 && near(farEnd.points[0].y, 0.0) &&
                  near(farEnd.points[3].y, -30.0),
              "connector drag: the far end of a straight run gains a knee too");
        // A vertical run breaks the other way (V-H-V).
        Entity vert;
        vert.type = EntityType::Wire;
        vert.points = {{0, 0}, {0, 100}};
        check(dragWireEndKeepingRightAngle(vert, 0, {25, 5}) == 1 && vert.points.size() == 4 &&
                  near(vert.points[0].x, vert.points[1].x),
              "connector drag: a vertical run still leaves the pin vertically");
    }

    // ---- Editing a property with SEVERAL objects selected acts on each of them ----
    {
        Document d;
        d.entities().clear();
        // Two connectors with different identities but the same pitch.
        for (int i = 0; i < 2; ++i) {
            ConnectorSpec s = defaultConnectorSpec(4);
            s.pitch = 20.0;
            s.designator = QStringLiteral("X%1").arg(i + 1);
            s.partNumber = QStringLiteral("PN-%1").arg(i + 1);
            Entity c;
            c.id = QStringLiteral("c%1").arg(i);
            c.type = EntityType::Connector;
            c.a = {i * 300.0, 0};
            writeConnectorSpec(c.meta, s);
            d.entities().push_back(c);
        }
        Editor ed;
        ed.setDocument(&d);
        ed.selectById("c0");
        ed.selectById("c1", /*additive=*/true);

        // Changing one field changes it on BOTH, and leaves their identities alone — the
        // panel must not stamp the first connector's whole spec over the rest.
        ed.editSelectedConnectorSpecs([](ConnectorSpec& s) { s.pitch = 33.0; });
        auto specOf = [&d](const char* id) {
            for (const auto& e : d.entities())
                if (e.id == QLatin1String(id)) return connectorSpecFromMeta(e.meta);
            return ConnectorSpec{};
        };
        check(near(specOf("c0").pitch, 33.0) && near(specOf("c1").pitch, 33.0),
              "multi edit: the edited field is applied to every selected connector");
        check(specOf("c0").designator == QLatin1String("X1") &&
                  specOf("c1").designator == QLatin1String("X2"),
              "multi edit: each connector keeps its own designator");
        check(specOf("c0").partNumber == QLatin1String("PN-1") &&
                  specOf("c1").partNumber == QLatin1String("PN-2"),
              "multi edit: each connector keeps its own part number");
    }

    // ---- Switching the resistance unit converts each wire's own value ----
    {
        Document d;
        d.entities().clear();
        Entity a;
        a.id = "wa";
        a.type = EntityType::Wire;
        a.points = {{0, 0}, {10, 0}};
        a.meta.insert("resistance", 10.0);
        a.meta.insert("resUnit", QStringLiteral("m"));
        Entity b;
        b.id = "wb";
        b.type = EntityType::Wire;
        b.points = {{0, 20}, {10, 20}};
        b.meta.insert("resistance", 20.0);
        b.meta.insert("resUnit", QStringLiteral("m"));
        d.entities().push_back(a);
        d.entities().push_back(b);
        Editor ed;
        ed.setDocument(&d);
        ed.selectById("wa");
        ed.selectById("wb", /*additive=*/true);
        ed.convertSelectionResistanceUnit(/*toFeet=*/true);
        auto res = [&d](const char* id) {
            for (const auto& e : d.entities())
                if (e.id == QLatin1String(id)) return e.meta.value("resistance").toDouble();
            return -1.0;
        };
        auto unit = [&d](const char* id) {
            for (const auto& e : d.entities())
                if (e.id == QLatin1String(id)) return e.meta.value("resUnit").toString();
            return QString();
        };
        check(near(res("wa"), 10.0 * 0.3048) && near(res("wb"), 20.0 * 0.3048),
              "multi edit: each wire's own resistance is converted, not overwritten");
        check(unit("wa") == QLatin1String("ft") && unit("wb") == QLatin1String("ft"),
              "multi edit: both wires carry the new unit");
        // Converting to the unit already in force leaves the values alone.
        ed.convertSelectionResistanceUnit(/*toFeet=*/true);
        check(near(res("wa"), 10.0 * 0.3048),
              "multi edit: re-selecting the same unit does not convert twice");
    }

    // ---- Harness definition: generation, validation, crossing minimisation ----
    {
        auto makeDef = []() {
            HarnessDef d;
            HarnessConnector p1;
            p1.id = "p1";
            p1.side = HarnessSide::Left;
            p1.designator = "P1";
            p1.partNumber = "1234";
            p1.pinCount = 4;
            p1.order = 0;
            HarnessConnector p2;
            p2.id = "p2";
            p2.side = HarnessSide::Right;
            p2.designator = "P2";
            p2.pinCount = 4;
            p2.order = 0;
            d.connectors = {p1, p2};
            for (int i = 1; i <= 4; ++i) {
                HarnessWire w;
                w.id = QStringLiteral("w%1").arg(i);
                w.fromConn = "p1";
                w.fromPin = i;
                w.toConn = "p2";
                w.toPin = i;
                d.wires.push_back(w);
            }
            return d;
        };

        // Pin sequence + labels: a partial or stale order still covers every pin once.
        {
            HarnessConnector c;
            c.pinCount = 4;
            c.pinOrder = {3, 1, 9, 3}; // out of range and repeated entries are dropped
            const auto seq = harnessPinSequence(c);
            check(seq.size() == 4 && seq[0] == 3 && seq[1] == 1,
                  "harness: the pin order is honoured and completed");
            check(std::find(seq.begin(), seq.end(), 2) != seq.end() &&
                      std::find(seq.begin(), seq.end(), 4) != seq.end(),
                  "harness: pins left out of the order are appended");
            c.annotation = QStringLiteral("letters");
            check(harnessPinLabel(c, 1) == QLatin1String("A") &&
                      harnessPinLabel(c, 2) == QLatin1String("B"),
                  "harness: letter annotation names the pins A, B, ...");
        }

        // Straight-through wiring draws a connector per side plus one wire each.
        {
            const HarnessDef d = makeDef();
            const HarnessBuild b = buildHarness(d);
            int conns = 0, wires = 0;
            for (const auto& e : b.entities) {
                if (e.type == EntityType::Connector) ++conns;
                if (e.type == EntityType::Wire) ++wires;
            }
            check(conns == 2 && wires == 4, "harness: two connectors and four wires drawn");
            check(b.crossings == 0, "harness: pin 1-1, 2-2 ... does not cross");
            check(b.issues.empty(), "harness: a straight-through harness validates clean");
            // Wires start exactly on a pin end of their connector.
            const Entity* conn = nullptr;
            for (const auto& e : b.entities)
                if (e.type == EntityType::Connector) { conn = &e; break; }
            const ConnectorGeometry g = connectorGeometry(conn->a, connectorSpecFromMeta(conn->meta));
            bool landed = false;
            for (const auto& e : b.entities)
                if (e.type == EntityType::Wire)
                    for (const Vec2& pe : g.pinEnd)
                        if (distance(e.points.front(), pe) < 1e-6) landed = true;
            check(landed, "harness: wires start on the connector pin ends");
        }

        // Crossed wiring is detected, and minimising re-orders the pins to undo it.
        {
            HarnessDef d = makeDef();
            d.wires[0].toPin = 4; // 1->4
            d.wires[3].toPin = 1; // 4->1
            check(harnessCrossings(d) > 0, "harness: reversed pins cross");
            const int before = harnessCrossings(d);
            const int after = minimizeHarnessCrossings(d);
            check(after < before, "harness: minimising reduces the crossings");
            check(after == harnessCrossings(d),
                  "harness: the reported count matches the re-ordered definition");
            // The connectors themselves were not moved, only their pin order.
            check(d.connectors[0].side == HarnessSide::Left && d.connectors[0].order == 0,
                  "harness: minimising leaves the connector placement alone");
        }

        // Validation (a): a repeated pin name is reported.
        {
            HarnessDef d = makeDef();
            d.connectors[0].pinOrder = {1, 1, 2, 3}; // pin 1 named twice
            const auto issues = validateHarness(d);
            bool found = false;
            for (const auto& i : issues)
                if (i.key.startsWith(QLatin1String("duporder"))) found = true;
            check(found, "harness validate: a repeated pin in the order is reported");
        }
        // Two connectors sharing a designator, and two wires on one pin.
        {
            HarnessDef d = makeDef();
            d.connectors[1].designator = "P1";
            d.wires[1].fromPin = 1; // pin 1 of P1 now carries two wires
            const auto issues = validateHarness(d);
            bool dupDes = false, busy = false;
            for (const auto& i : issues) {
                if (i.key.startsWith(QLatin1String("dupdes"))) dupDes = true;
                if (i.key.startsWith(QLatin1String("pinbusy"))) busy = true;
            }
            check(dupDes, "harness validate: a duplicated designator is reported");
            check(busy, "harness validate: two wires on one pin is reported");
        }
        // Validation (b): a shield broken by an unshielded wire between its wires.
        {
            HarnessDef d = makeDef();
            d.wires[0].shieldGroup = "S1"; // rows 0 and 2 shielded...
            d.wires[2].shieldGroup = "S1";
            const auto issues = validateHarness(d);
            bool split = false;
            for (const auto& i : issues)
                if (i.key.startsWith(QLatin1String("shieldsplit"))) split = true;
            check(split, "harness validate: an unshielded wire inside a shield is reported");
            // Neighbouring wires are fine.
            HarnessDef ok = makeDef();
            ok.wires[0].shieldGroup = "S1";
            ok.wires[1].shieldGroup = "S1";
            bool anySplit = false;
            for (const auto& i : validateHarness(ok))
                if (i.key.startsWith(QLatin1String("shieldsplit"))) anySplit = true;
            check(!anySplit, "harness validate: a shield over neighbouring wires is accepted");
        }
        // Validation (c): the same rule for a twisted group, plus a lone twisted wire.
        {
            HarnessDef d = makeDef();
            d.wires[0].twistGroup = "T1";
            d.wires[2].twistGroup = "T1";
            bool split = false, alone = false;
            for (const auto& i : validateHarness(d)) {
                if (i.key.startsWith(QLatin1String("twistsplit"))) split = true;
                if (i.key.startsWith(QLatin1String("twistalone"))) alone = true;
            }
            check(split, "harness validate: a wire between a twisted pair is reported");
            check(!alone, "harness validate: a twisted pair of two is not 'alone'");
            HarnessDef one = makeDef();
            one.wires[0].twistGroup = "T1";
            bool lone = false;
            for (const auto& i : validateHarness(one))
                if (i.key.startsWith(QLatin1String("twistalone"))) lone = true;
            check(lone, "harness validate: a twisted group of one wire is reported");
        }

        // Shields and twists are deployed with the configured amount (2), and the shield
        // sits beside the connector that asked for it.
        {
            HarnessDef d = makeDef();
            d.wires[0].shieldGroup = "S1";
            d.wires[1].shieldGroup = "S1";
            d.wires[0].twistGroup = "T1";
            d.wires[1].twistGroup = "T1";
            d.connectors[1].shieldRight = true; // screened at the RIGHT end of the harness
            const HarnessBuild b = buildHarness(d);
            int overall = 0, internal = 0, twists = 0;
            double shieldX = 0.0;
            for (const auto& e : b.entities) {
                if (e.type == EntityType::Screen) {
                    if (e.meta.contains("overallShield")) { ++overall; shieldX = e.points.front().x; }
                    else ++internal;
                }
                if (e.type == EntityType::Twist) ++twists;
            }
            check(overall == 2, "harness: the overall shield is deployed with amount 2");
            check(internal == 2, "harness: the wire shield is deployed with its own amount 2");
            check(twists == 2, "harness: the twist is deployed with amount 2");
            // The right end means past the middle of the run.
            check(shieldX > (d.layout.leftX + d.layout.rightX) / 2.0,
                  "harness: the shield is placed at the end that asked for it");
            // The twist carries the configured number of crossings.
            for (const auto& e : b.entities)
                if (e.type == EntityType::Twist)
                    check(e.meta.value("repeats").toInt() == d.layout.twistCount,
                          "harness: the twist carries the configured twist count");
        }

        // Bridged crossings mark hops; switching it off leaves plain crosses.
        {
            HarnessDef d = makeDef();
            d.wires[0].toPin = 4;
            d.wires[3].toPin = 1;
            d.layout.bridgeCrossings = true;
            int hops = 0;
            for (const auto& e : buildHarness(d).entities)
                if (e.type == EntityType::Wire) hops += e.meta.value("hops").toArray().size();
            check(hops > 0, "harness: crossings are bridged when asked for");
            d.layout.bridgeCrossings = false;
            int plain = 0;
            for (const auto& e : buildHarness(d).entities)
                if (e.type == EntityType::Wire) plain += e.meta.value("hops").toArray().size();
            check(plain == 0, "harness: no bridges when the option is off");
        }

        // Unconnected pins are X-terminated on request.
        {
            HarnessDef d = makeDef();
            d.connectors[0].pinCount = 6; // two pins left over
            d.layout.terminateUnconnected = true;
            int xs = 0;
            for (const auto& e : buildHarness(d).entities)
                if (e.type == EntityType::NotConnected) ++xs;
            check(xs == 2, "harness: every unwired pin gets an X marker");
            d.layout.terminateUnconnected = false;
            int none = 0;
            for (const auto& e : buildHarness(d).entities)
                if (e.type == EntityType::NotConnected) ++none;
            check(none == 0, "harness: no markers when termination is off");
        }

        // Placement order and Y-split legs: order stacks a side, legs are kept apart.
        {
            HarnessDef d = makeDef();
            HarnessConnector p3;
            p3.id = "p3";
            p3.side = HarnessSide::Right;
            p3.designator = "P3";
            p3.pinCount = 4;
            p3.order = 1;
            p3.leg = "B"; // its own leg of a Y-split
            d.connectors[1].leg = "A";
            d.connectors.push_back(p3);
            const HarnessBuild b = buildHarness(d);
            std::vector<double> rightY;
            for (const auto& e : b.entities)
                if (e.type == EntityType::Connector &&
                    connectorSpecFromMeta(e.meta).designator.startsWith(QLatin1String("P")) &&
                    e.a.x > (d.layout.leftX + d.layout.rightX) / 2.0)
                    rightY.push_back(e.a.y);
            check(rightY.size() == 2, "harness: both right-hand connectors are placed");
            check(std::abs(rightY[0] - rightY[1]) >
                      d.layout.connectorGap + d.layout.legGap,
                  "harness: separate Y-split legs are stacked further apart than one gap");
        }
    }

    // ---- Generating a harness into the document ----
    {
        HarnessDef d;
        HarnessConnector p1;
        p1.id = "p1"; p1.side = HarnessSide::Left; p1.designator = "P1"; p1.pinCount = 2;
        HarnessConnector p2;
        p2.id = "p2"; p2.side = HarnessSide::Right; p2.designator = "P2"; p2.pinCount = 2;
        d.connectors = {p1, p2};
        for (int i = 1; i <= 2; ++i) {
            HarnessWire w;
            w.id = QStringLiteral("dw%1").arg(i);
            w.fromConn = "p1"; w.fromPin = i; w.toConn = "p2"; w.toPin = i;
            w.color = (i == 1) ? QStringLiteral("RD") : QStringLiteral("BK");
            w.shieldGroup = "S1";
            w.twistGroup = "T1";
            d.wires.push_back(w);
        }
        Document doc;
        doc.entities().clear();
        Editor ed;
        ed.setDocument(&doc);
        std::vector<HarnessIssue> issues;
        const int n = ed.insertHarness(d, &issues);
        check(n > 0 && static_cast<size_t>(n) == doc.entities().size(),
              "harness insert: every generated object is added");
        check(issues.empty(), "harness insert: this definition validates clean");
        check(ed.selection().size() == doc.entities().size(),
              "harness insert: the generated harness is left selected");
        // Every object carries a real id, and the shield/twist name wires that EXIST.
        QSet<QString> wireIds;
        for (const auto& e : doc.entities()) {
            check(!e.id.isEmpty(), "harness insert: every object has an id");
            if (e.type == EntityType::Wire) wireIds.insert(e.id);
        }
        int checkedRefs = 0;
        for (const auto& e : doc.entities()) {
            if (e.type != EntityType::Screen && e.type != EntityType::Twist) continue;
            for (const auto& v : e.meta.value("wires").toArray()) {
                check(wireIds.contains(v.toString()),
                      "harness insert: shield/twist references point at real wires");
                ++checkedRefs;
            }
        }
        check(checkedRefs > 0, "harness insert: the annotations do reference their wires");
        // Those references are what make the wire report itself shielded and twisted.
        QString aWire = *wireIds.begin();
        check(ed.wireIsShielded(aWire) && ed.wireIsTwisted(aWire),
              "harness insert: the generated wires read back as shielded and twisted");
        // Undo takes the whole harness away in one step.
        ed.undo();
        check(doc.entities().empty(), "harness insert: one undo removes the whole harness");
    }

    // ---- Pinout optimisation: bundling counts, not just crossings ----
    {
        // P1 pins 1..6 straight across to P2. Put the shielded pair on rows 0 and 2 and the
        // twisted pair on rows 3 and 5, so each bundle has a foreign wire through the middle
        // of it — exactly the split the generated drawing showed.
        auto split = []() {
            HarnessDef d;
            HarnessConnector a;
            a.id = "p1"; a.side = HarnessSide::Left; a.designator = "P1"; a.pinCount = 6;
            HarnessConnector b;
            b.id = "p2"; b.side = HarnessSide::Right; b.designator = "P2"; b.pinCount = 6;
            d.connectors = {a, b};
            for (int i = 1; i <= 6; ++i) {
                HarnessWire w;
                w.id = QStringLiteral("w%1").arg(i);
                w.fromConn = "p1"; w.fromPin = i; w.toConn = "p2"; w.toPin = i;
                d.wires.push_back(w);
            }
            d.wires[0].shieldGroup = "S1";
            d.wires[2].shieldGroup = "S1";
            d.wires[3].twistGroup = "T1";
            d.wires[5].twistGroup = "T1";
            return d;
        };

        // The break counts see the problem, on both connectors (2 each).
        {
            const HarnessDef d = split();
            check(harnessBundleBreaks(d, /*shield=*/true) == 2,
                  "optimise: a wire through a shield counts as a break on each connector");
            check(harnessBundleBreaks(d, /*shield=*/false) == 2,
                  "optimise: a wire through a twisted pair counts too");
            check(harnessCrossings(d) == 0, "optimise: this start has no crossings");
        }

        // Minimise crossings now also pulls the bundles together — the whole point of the
        // fix, since a crossing-free layout that splits a shield cannot be drawn.
        {
            HarnessDef d = split();
            minimizeHarnessCrossings(d);
            check(harnessBundleBreaks(d, true) == 0,
                  "optimise: minimise crossings closes the shield band");
            check(harnessBundleBreaks(d, false) == 0,
                  "optimise: minimise crossings closes the twisted band");
            check(harnessCrossings(d) == 0,
                  "optimise: and it does not introduce crossings to do it");
            // Only the pin order moved: the wiring is untouched.
            check(d.wires[0].fromPin == 1 && d.wires[2].fromPin == 3,
                  "optimise: the wire list is left exactly as it was");
            check(d.connectors[0].pinOrder.size() == 6,
                  "optimise: the pinout order is what changed");
            // The shielded wires really are neighbours in the new pin order.
            const std::vector<int> seq = harnessPinSequence(d.connectors[0]);
            const auto rowOf = [&seq](int pin) {
                return int(std::find(seq.begin(), seq.end(), pin) - seq.begin());
            };
            check(std::abs(rowOf(1) - rowOf(3)) == 1,
                  "optimise: the two shielded wires end up on adjacent pins");
        }

        // The targeted buttons: shield first, twist first.
        {
            HarnessDef d = split();
            check(optimizeHarnessForShields(d) == 0,
                  "optimise by shield: every shielded group ends up adjacent");
            HarnessDef t = split();
            check(optimizeHarnessForTwists(t) == 0,
                  "optimise by twist: every twisted group ends up adjacent");
        }

        // A layout that must trade a crossing for a closed bundle takes the trade: the
        // bundle weight dominates.
        {
            HarnessDef d = split();
            d.wires[0].toPin = 3; // force a crossing into the shielded pair's path
            d.wires[2].toPin = 1;
            minimizeHarnessCrossings(d);
            check(harnessBundleBreaks(d, true) == 0,
                  "optimise: closing the shield wins over shaving a crossing");
        }

        // Cost weighting is what encodes that priority.
        {
            const HarnessDef d = split();
            HarnessOptimizeWeights w;
            check(harnessLayoutCost(d, w) == w.shieldBreak * 2 + w.twistBreak * 2,
                  "optimise: the cost is the weighted sum of the breaks and crossings");
        }
    }

    // ---- Overall shield (per connector) vs internal shield (per wire group) ----
    {
        auto base = []() {
            HarnessDef d;
            HarnessConnector a;
            a.id = "p1"; a.side = HarnessSide::Left; a.designator = "P1"; a.pinCount = 4;
            HarnessConnector b;
            b.id = "p2"; b.side = HarnessSide::Right; b.designator = "P2"; b.pinCount = 4;
            d.connectors = {a, b};
            for (int i = 1; i <= 2; ++i) {
                HarnessWire w;
                w.id = QStringLiteral("w%1").arg(i);
                w.fromConn = "p1"; w.fromPin = i; w.toConn = "p2"; w.toPin = i;
                d.wires.push_back(w);
            }
            d.layout.shieldAmount = 1;
            d.layout.wireShieldAmount = 1;
            return d;
        };
        auto countScreens = [](const HarnessBuild& b, const char* metaKey) {
            int n = 0;
            for (const auto& e : b.entities)
                if (e.type == EntityType::Screen && e.meta.contains(QLatin1String(metaKey))) ++n;
            return n;
        };
        auto firstX = [](const HarnessBuild& b, const char* metaKey) {
            for (const auto& e : b.entities)
                if (e.type == EntityType::Screen && e.meta.contains(QLatin1String(metaKey)))
                    return e.points.front().x;
            return 0.0;
        };

        // An OVERALL shield is asked for on the connector and wraps everything reaching it —
        // no wire needs a shield group for it to appear.
        HarnessDef ov = base();
        ov.connectors[0].shieldLeft = true;
        const HarnessBuild ovb = buildHarness(ov);
        check(countScreens(ovb, "overallShield") == 1,
              "overall shield: a connector's own tick draws it, with no wire groups at all");
        check(countScreens(ovb, "shieldGroup") == 0,
              "overall shield: no internal screen appears unless a wire asks for one");
        const double mid = (ov.layout.leftX + ov.layout.rightX) / 2.0;
        check(firstX(ovb, "overallShield") > ov.layout.leftX &&
                  firstX(ovb, "overallShield") < mid,
              "overall shield: it sits inboard of the connector that asked for it");

        // A LEFT connector's right-end flag is meaningless and draws nothing (and vice versa),
        // which is why the dialog only offers each connector its own side.
        HarnessDef wrong = base();
        wrong.connectors[0].shieldRight = true; // a left connector, right-end flag
        check(countScreens(buildHarness(wrong), "overallShield") == 0,
              "overall shield: the wrong side's flag is ignored");
        HarnessDef rightEnd = base();
        rightEnd.connectors[1].shieldRight = true; // the right connector's own flag
        check(firstX(buildHarness(rightEnd), "overallShield") > mid,
              "overall shield: the right connector's shield sits inboard of the right end");

        // Both ends ticked: a screen at each.
        HarnessDef both = base();
        both.connectors[0].shieldLeft = true;
        both.connectors[1].shieldRight = true;
        check(countScreens(buildHarness(both), "overallShield") == 2,
              "overall shield: screened at both ends gives one screen per connector");

        // An INTERNAL shield comes from the wire rows and nests further along the run.
        HarnessDef internal = base();
        internal.wires[0].shieldGroup = "S1";
        internal.wires[1].shieldGroup = "S1";
        internal.connectors[0].shieldLeft = true;
        const HarnessBuild ib = buildHarness(internal);
        check(countScreens(ib, "shieldGroup") == 1, "internal shield: the wire group draws one");
        check(countScreens(ib, "overallShield") == 1,
              "internal shield: the overall one is still drawn as well");
        check(firstX(ib, "shieldGroup") > firstX(ib, "overallShield"),
              "internal shield: it sits further along the run, inside the overall one");

        // The two amounts are set separately in Layout.
        HarnessDef amounts = base();
        amounts.connectors[0].shieldLeft = true;
        amounts.wires[0].shieldGroup = "S1";
        amounts.wires[1].shieldGroup = "S1";
        amounts.layout.shieldAmount = 3;
        amounts.layout.wireShieldAmount = 2;
        const HarnessBuild ab = buildHarness(amounts);
        check(countScreens(ab, "overallShield") == 3,
              "shield amounts: the overall count comes from its own Layout field");
        check(countScreens(ab, "shieldGroup") == 2,
              "shield amounts: the wire shield count comes from its own Layout field");

        // Every connector marked "shield to body" is bonded to ITS OWN body.
        HarnessDef bonds = base();
        bonds.connectors[0].shieldLeft = true;
        bonds.connectors[0].bodyBondLeft = true;
        bonds.connectors[1].shieldRight = true;
        bonds.connectors[1].bodyBondRight = true;
        QStringList bondKeys;
        for (const auto& e : buildHarness(bonds).entities)
            if (e.meta.contains("shieldBond")) bondKeys << e.meta.value("shieldBond").toString();
        bondKeys.sort();
        check(bondKeys.size() == 2,
              "shield to body: each marked connector gets its own bond");
        check(bondKeys.contains(QStringLiteral("P1")) && bondKeys.contains(QStringLiteral("P2")),
              "shield to body: the bonds name the connector each belongs to");
        // Not marked: no bond.
        HarnessDef noBond = base();
        noBond.connectors[0].shieldLeft = true;
        int none = 0;
        for (const auto& e : buildHarness(noBond).entities)
            if (e.meta.contains("shieldBond")) ++none;
        check(none == 0, "shield to body: nothing drawn unless the box is ticked");

        // Validation: an overall shield needs wires arriving, and a bond needs a shield.
        HarnessDef noWires = base();
        noWires.wires.clear();
        noWires.connectors[0].shieldLeft = true;
        bool flagged = false;
        for (const auto& i : validateHarness(noWires))
            if (i.key.startsWith(QLatin1String("shieldnowire"))) flagged = true;
        check(flagged, "validate: an overall shield with no wire reaching it is reported");
        HarnessDef bondOnly = base();
        bondOnly.connectors[0].bodyBondLeft = true; // bond without the shield
        bool bondFlagged = false;
        for (const auto& i : validateHarness(bondOnly))
            if (i.key.startsWith(QLatin1String("bondnoshield"))) bondFlagged = true;
        check(bondFlagged, "validate: a body bond with no overall shield is reported");
        HarnessDef fine = base();
        fine.connectors[0].shieldLeft = true;
        fine.connectors[0].bodyBondLeft = true;
        bool complained = false;
        for (const auto& i : validateHarness(fine))
            if (i.key.startsWith(QLatin1String("shieldnowire")) ||
                i.key.startsWith(QLatin1String("bondnoshield")))
                complained = true;
        check(!complained, "validate: a shield with wires and a matching bond is accepted");
    }

    // ---- A re-ordered pinout must SHOW on the drawn connector ----
    {
        HarnessDef d;
        HarnessConnector a;
        a.id = "p1"; a.side = HarnessSide::Left; a.designator = "P1"; a.pinCount = 4;
        a.pinOrder = {4, 3, 2, 1}; // reversed
        HarnessConnector b;
        b.id = "p2"; b.side = HarnessSide::Right; b.designator = "P2"; b.pinCount = 4;
        d.connectors = {a, b};
        HarnessWire w;
        w.id = "w1"; w.fromConn = "p1"; w.fromPin = 1; w.toConn = "p2"; w.toPin = 1;
        d.wires.push_back(w);
        const HarnessBuild built = buildHarness(d);
        const Entity* conn = nullptr;
        for (const auto& e : built.entities)
            if (e.type == EntityType::Connector &&
                connectorSpecFromMeta(e.meta).designator == QLatin1String("P1"))
                conn = &e;
        check(conn != nullptr, "pinout draw: the connector is generated");
        const ConnectorSpec s = connectorSpecFromMeta(conn->meta);
        check(s.pins.size() == 4 && s.pins[0] == QLatin1String("4") &&
                  s.pins[3] == QLatin1String("1"),
              "pinout draw: the drawn pin labels follow the re-ordered pinout");
        // ...and the wire on pin 1 attaches at the row pin 1 now occupies (the bottom).
        const ConnectorGeometry g = connectorGeometry(conn->a, s);
        const Entity* wire = nullptr;
        for (const auto& e : built.entities)
            if (e.type == EntityType::Wire) wire = &e;
        check(wire && distance(wire->points.front(), g.pinEnd[3]) < 1e-6,
              "pinout draw: the wire attaches at its pin's new row");
    }

    // ---- Crossings across DIFFERENT destination connectors are seen and fixed ----
    {
        // One left connector fanning to two right connectors, wired the wrong way round:
        // P1's top pins go to the LOWER connector and its bottom pins to the UPPER one, so
        // the bundle crosses itself in the middle. Nothing here shares a connector PAIR, so
        // the old per-pair test scored it 0 and the optimiser had nothing to do.
        HarnessDef d;
        HarnessConnector p1;
        p1.id = "p1"; p1.side = HarnessSide::Left; p1.designator = "P1"; p1.pinCount = 4;
        HarnessConnector up;
        up.id = "up"; up.side = HarnessSide::Right; up.designator = "P3"; up.pinCount = 2;
        up.order = 0;
        HarnessConnector down;
        down.id = "dn"; down.side = HarnessSide::Right; down.designator = "P2"; down.pinCount = 2;
        down.order = 1;
        d.connectors = {p1, up, down};
        auto wire = [](const char* id, int fromPin, const char* to, int toPin) {
            HarnessWire w;
            w.id = QString::fromLatin1(id);
            w.fromConn = "p1";
            w.fromPin = fromPin;
            w.toConn = QString::fromLatin1(to);
            w.toPin = toPin;
            return w;
        };
        d.wires = {wire("a", 1, "dn", 1), wire("b", 2, "dn", 2), wire("c", 3, "up", 1),
                   wire("d", 4, "up", 2)};
        const int before = harnessCrossings(d);
        check(before > 0,
              "crossings: wires to different connectors on the same side are counted");
        minimizeHarnessCrossings(d);
        check(harnessCrossings(d) < before,
              "crossings: re-ordering the pinout untangles a multi-destination fan-out");
        check(harnessCrossings(d) == 0, "crossings: this one untangles completely");
        // Only the pin order moved.
        check(d.wires[0].fromPin == 1 && d.wires[3].fromPin == 4,
              "crossings: the wiring is untouched by the optimiser");
    }

    // ---- Knee lanes are spaced by the connector pitch ----
    {
        HarnessDef d;
        HarnessConnector a;
        a.id = "p1"; a.side = HarnessSide::Left; a.designator = "P1"; a.pinCount = 4;
        a.pitch = 40.0; // a wide pitch, so a fixed small lane step would be obvious
        HarnessConnector b;
        b.id = "p2"; b.side = HarnessSide::Right; b.designator = "P2"; b.pinCount = 4;
        b.pitch = 40.0;
        d.connectors = {a, b};
        // Fully reverse the pins so EVERY wire needs a knee (a wire whose two pins sit at
        // the same height runs straight across and has none).
        for (int i = 1; i <= 4; ++i) {
            HarnessWire w;
            w.id = QStringLiteral("w%1").arg(i);
            w.fromConn = "p1"; w.fromPin = i; w.toConn = "p2"; w.toPin = 5 - i;
            d.wires.push_back(w);
        }
        std::vector<double> kneeX;
        for (const auto& e : buildHarness(d).entities)
            if (e.type == EntityType::Wire && e.points.size() == 4) kneeX.push_back(e.points[1].x);
        std::sort(kneeX.begin(), kneeX.end());
        check(kneeX.size() == 4, "knee pitch: every staggered wire has a knee");
        check(near(kneeX[1] - kneeX[0], 40.0) && near(kneeX[3] - kneeX[2], 40.0),
              "knee pitch: neighbouring knees are one connector pitch apart");
        // A narrower connector pitch gives correspondingly narrower lanes.
        d.connectors[0].pitch = 12.0;
        d.connectors[1].pitch = 12.0;
        std::vector<double> tightX;
        for (const auto& e : buildHarness(d).entities)
            if (e.type == EntityType::Wire && e.points.size() == 4) tightX.push_back(e.points[1].x);
        std::sort(tightX.begin(), tightX.end());
        check(tightX.size() == 4 && near(tightX[1] - tightX[0], 12.0),
              "knee pitch: the lane spacing follows the pitch");
    }

    // ---- Shield drain leg: length property, and no X when the leg is wired ----
    {
        // A shield catching two horizontal wires, with its drain leg hanging below.
        Document d;
        d.entities().clear();
        Entity w1;
        w1.id = "w1"; w1.type = EntityType::Wire; w1.points = {{0, 100}, {200, 100}};
        Entity w2;
        w2.id = "w2"; w2.type = EntityType::Wire; w2.points = {{0, 80}, {200, 80}};
        d.entities().push_back(w1);
        d.entities().push_back(w2);
        Entity sc;
        sc.id = "sc";
        sc.type = EntityType::Screen;
        sc.points = {{100, 100}, {100, 80}, {100, 20}}; // 2 crossings + the drain end
        sc.meta.insert("catch", 2);
        sc.meta.insert("rx", 10.0);
        sc.meta.insert("ry", 20.0);
        sc.meta.insert("rot", 1.5707963267948966); // vertical major axis
        sc.meta.insert("legDir", QStringLiteral("bottom"));
        d.entities().push_back(sc);

        Editor ed;
        ed.setDocument(&d);
        ed.selectById("sc");
        // The leg length is measured from where the leg leaves the outline, not from the
        // centre: centre y = 90, ry = 20, so the bottom edge is y = 70 and the leg reaches
        // to y = 20 — 50 long.
        check(near(ed.selectedShieldLegLength(), 50.0),
              "shield leg: the length is measured from the outline edge");
        ed.setSelectedShieldLegLength(30.0);
        const Entity* after = nullptr;
        for (const auto& e : d.entities())
            if (e.id == QLatin1String("sc")) after = &e;
        check(after && near(after->points.back().y, 40.0),
              "shield leg: setting the length slides the leg end along its own direction");
        check(near(ed.selectedShieldLegLength(), 30.0),
              "shield leg: the new length reads back");
        // Setting it does not swing the leg to another side.
        check(after && near(after->points.back().x, 100.0),
              "shield leg: the leg keeps its direction");

        // Nothing on the leg end: it is not wired.
        check(!ed.shieldLegIsWired(*after), "shield leg: a leg in free space is not wired");
        // A wire ENDING on the leg end counts as wired.
        Entity drainWire;
        drainWire.id = "dw";
        drainWire.type = EntityType::Wire;
        drainWire.points = {{100, 40}, {180, 40}};
        d.entities().push_back(drainWire);
        for (const auto& e : d.entities())
            if (e.id == QLatin1String("sc")) after = &e;
        check(ed.shieldLegIsWired(*after), "shield leg: a wire on the leg end is wired");
        // A wire PASSING THROUGH the leg end counts too (a leg dropped onto a run).
        auto& ents = d.entities();
        ents.erase(std::remove_if(ents.begin(), ents.end(),
                                  [](const Entity& x) { return x.id == QLatin1String("dw"); }),
                   ents.end());
        Entity through;
        through.id = "tw";
        through.type = EntityType::Wire;
        through.points = {{20, 40}, {180, 40}}; // runs across the leg end
        ents.push_back(through);
        for (const auto& e : d.entities())
            if (e.id == QLatin1String("sc")) after = &e;
        check(ed.shieldLegIsWired(*after),
              "shield leg: a run passing through the leg end is wired too");
        // A wire that misses the leg end does not count.
        for (auto& e : ents)
            if (e.id == QLatin1String("tw")) e.points = {{20, 55}, {180, 55}};
        for (const auto& e : d.entities())
            if (e.id == QLatin1String("sc")) after = &e;
        check(!ed.shieldLegIsWired(*after), "shield leg: a wire that misses it does not count");
    }

    // ---- Every generated wire run must be axis-aligned (0 / 90 only) ----
    {
        // The shape that showed the problem: one 6-pin connector on the left feeding an
        // 8-pin and a 4-pin connector stacked on the right.
        HarnessDef d;
        HarnessConnector p1;
        p1.id = "p1"; p1.side = HarnessSide::Left; p1.designator = "P1"; p1.pinCount = 6;
        HarnessConnector p3;
        p3.id = "p3"; p3.side = HarnessSide::Right; p3.designator = "P3"; p3.pinCount = 8;
        p3.order = 0;
        HarnessConnector p2;
        p2.id = "p2"; p2.side = HarnessSide::Right; p2.designator = "P2"; p2.pinCount = 4;
        p2.order = 1;
        d.connectors = {p1, p3, p2};
        HarnessWire a;
        a.id = "a"; a.fromConn = "p1"; a.fromPin = 1; a.toConn = "p3"; a.toPin = 1;
        HarnessWire b;
        b.id = "b"; b.fromConn = "p1"; b.fromPin = 2; b.toConn = "p2"; b.toPin = 1;
        d.wires = {a, b};

        int checkedSegs = 0;
        for (const auto& e : buildHarness(d).entities) {
            if (e.type != EntityType::Wire) continue;
            for (size_t i = 1; i < e.points.size(); ++i) {
                const Vec2 p = e.points[i - 1], q = e.points[i];
                const bool horiz = std::abs(p.y - q.y) < 1e-6;
                const bool vert = std::abs(p.x - q.x) < 1e-6;
                check(horiz || vert,
                      "generated route: every wire segment is horizontal or vertical");
                ++checkedSegs;
            }
        }
        check(checkedSegs > 0, "generated route: there are segments to check");
    }

    // ---- Drawing onto an existing wire's free end continues it: ONE wire ----
    {
        auto docWithWire = [](Document& d) {
            d.entities().clear();
            Entity w;
            w.id = "w1";
            w.type = EntityType::Wire;
            w.points = {{0, 0}, {100, 0}};
            w.meta.insert("wireColor", QStringLiteral("RD"));
            w.meta.insert("wireGauge", QStringLiteral("0.75"));
            w.meta.insert("entNum", 7);
            d.entities().push_back(w);
        };
        auto arm = [](Editor& ed) {
            ed.snapSettings().objectSnap = false;
            ed.snapSettings().gridSnap = false;
            ed.snapSettings().ortho = false;
            ed.snapSettings().polar = false;
            ed.setTool(ToolId::Wire);
        };

        // Draw on from the free end: one wire, its properties kept.
        {
            Document d;
            docWithWire(d);
            Editor ed;
            ed.setDocument(&d);
            arm(ed);
            ed.onPointerPress({100, 0}, Qt::LeftButton, Qt::NoModifier); // on the free end
            ed.onPointerPress({100, -60}, Qt::LeftButton, Qt::NoModifier);
            ed.onKey(Qt::Key_Return, Qt::NoModifier);
            check(d.entities().size() == 1, "wire extend: no second wire is created");
            const Entity& w = d.entities().front();
            check(w.points.size() == 3 && near(w.points.back().y, -60.0),
                  "wire extend: the run is appended to the existing wire");
            check(w.id == QLatin1String("w1") && w.meta.value("entNum").toInt() == 7,
                  "wire extend: it stays the same wire, with its own number");
            check(w.meta.value("wireColor").toString() == QLatin1String("RD") &&
                      w.meta.value("wireGauge").toString() == QLatin1String("0.75"),
                  "wire extend: the original colour and gauge are kept");
        }

        // Drawn TOWARD the end instead of away from it: still one continuous wire.
        {
            Document d;
            docWithWire(d);
            Editor ed;
            ed.setDocument(&d);
            arm(ed);
            ed.onPointerPress({160, -60}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerPress({100, 0}, Qt::LeftButton, Qt::NoModifier); // ends on the tip
            ed.onKey(Qt::Key_Return, Qt::NoModifier);
            check(d.entities().size() == 1 && d.entities().front().points.size() == 3,
                  "wire extend: drawing toward the end joins it too");
            const Entity& w = d.entities().front();
            check(near(w.points.front().x, 0.0) && near(w.points.back().x, 160.0),
                  "wire extend: the joined run reads continuously end to end");
        }

        // The FRONT end can be continued as well, and the wire still reads in order.
        {
            Document d;
            docWithWire(d);
            Editor ed;
            ed.setDocument(&d);
            arm(ed);
            ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier); // the other free end
            ed.onPointerPress({-70, 0}, Qt::LeftButton, Qt::NoModifier);
            ed.onKey(Qt::Key_Return, Qt::NoModifier);
            check(d.entities().size() == 1, "wire extend: the front end joins too");
            const Entity& w = d.entities().front();
            check(w.points.size() == 3 && near(w.points.front().x, -70.0) &&
                      near(w.points.back().x, 100.0),
                  "wire extend: prepending keeps the run in order");
        }

        // An end on a connector PIN is a termination: a wire drawn from there is its own
        // conductor, never a continuation.
        {
            Document d;
            d.entities().clear();
            ConnectorSpec spec = defaultConnectorSpec(2);
            Entity conn;
            conn.id = "c1";
            conn.type = EntityType::Connector;
            conn.a = {0, 0};
            writeConnectorSpec(conn.meta, spec);
            d.entities().push_back(conn);
            const ConnectorGeometry g = connectorGeometry(conn.a, spec);
            Entity w;
            w.id = "w1";
            w.type = EntityType::Wire;
            w.points = {{g.pinEnd[0].x + 120, g.pinEnd[0].y}, g.pinEnd[0]}; // ends ON the pin
            d.entities().push_back(w);
            Editor ed;
            ed.setDocument(&d);
            arm(ed);
            ed.onPointerPress(g.pinEnd[0], Qt::LeftButton, Qt::NoModifier);
            ed.onPointerPress({g.pinEnd[0].x, g.pinEnd[0].y - 80}, Qt::LeftButton, Qt::NoModifier);
            ed.onKey(Qt::Key_Return, Qt::NoModifier);
            int wires = 0;
            for (const auto& e : d.entities())
                if (e.type == EntityType::Wire) ++wires;
            check(wires == 2, "wire extend: a pin end is a termination, so a new wire is made");
        }

        // A splice is an explicit junction: joining through it would fuse two conductors.
        {
            Document d;
            docWithWire(d);
            Entity sp;
            sp.id = "s1";
            sp.type = EntityType::Splice;
            sp.a = {100, 0};
            d.entities().push_back(sp);
            Editor ed;
            ed.setDocument(&d);
            arm(ed);
            ed.onPointerPress({100, 0}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerPress({100, -60}, Qt::LeftButton, Qt::NoModifier);
            ed.onKey(Qt::Key_Return, Qt::NoModifier);
            int wires = 0;
            for (const auto& e : d.entities())
                if (e.type == EntityType::Wire) ++wires;
            check(wires == 2, "wire extend: a spliced end is not joined through");
        }

        // A wire drawn nowhere near an existing end is simply its own wire.
        {
            Document d;
            docWithWire(d);
            Editor ed;
            ed.setDocument(&d);
            arm(ed);
            ed.onPointerPress({0, -300}, Qt::LeftButton, Qt::NoModifier);
            ed.onPointerPress({100, -300}, Qt::LeftButton, Qt::NoModifier);
            ed.onKey(Qt::Key_Return, Qt::NoModifier);
            int wires = 0;
            for (const auto& e : d.entities())
                if (e.type == EntityType::Wire) ++wires;
            check(wires == 2, "wire extend: an unrelated wire is left as its own object");
        }
    }

    // ---- Populated pins first, spares after ----
    {
        // 6-pin left connector feeding an 8-pin right one on pins 1, 3 and 6, so the wired
        // pins start scattered among the spares.
        HarnessDef d;
        HarnessConnector p1;
        p1.id = "p1"; p1.side = HarnessSide::Left; p1.designator = "P1"; p1.pinCount = 6;
        HarnessConnector p3;
        p3.id = "p3"; p3.side = HarnessSide::Right; p3.designator = "P3"; p3.pinCount = 8;
        d.connectors = {p1, p3};
        const int toPins[3] = {1, 3, 6};
        for (int i = 0; i < 3; ++i) {
            HarnessWire w;
            w.id = QStringLiteral("w%1").arg(i);
            w.fromConn = "p1"; w.fromPin = i + 1; w.toConn = "p3"; w.toPin = toPins[i];
            d.wires.push_back(w);
        }
        check(harnessPinOrderPenalty(d) > 0,
              "pin order: spare pins sitting above wired ones are penalised");
        minimizeHarnessCrossings(d);
        check(harnessPinOrderPenalty(d) == 0,
              "pin order: optimising puts every wired pin above the spares");
        // Read it off the drawn connector: the wired pins form a solid block at the top.
        const HarnessConnector* p3o = harnessConnById(d, "p3");
        const std::vector<int> seq = harnessPinSequence(*p3o);
        QSet<int> wired = {1, 3, 6};
        int firstSpare = -1;
        for (int i = 0; i < static_cast<int>(seq.size()); ++i) {
            if (!wired.contains(seq[i]) && firstSpare < 0) firstSpare = i;
            if (wired.contains(seq[i]) && firstSpare >= 0)
                check(false, "pin order: no wired pin appears below a spare");
        }
        check(firstSpare == 3, "pin order: the three wired pins occupy the top three rows");
        // A connector with nothing wired to it is left alone.
        HarnessDef bare;
        bare.connectors = {p1};
        check(harnessPinOrderPenalty(bare) == 0,
              "pin order: an unwired connector carries no penalty");
    }

    // ---- Knee lanes nest, so no vertical cuts across another wire's horizontal ----
    {
        // Three wires leaving one connector and fanning down to a lower one: the wire from
        // the highest row reaches furthest, so it must turn LAST (outermost lane).
        HarnessDef d;
        HarnessConnector p1;
        p1.id = "p1"; p1.side = HarnessSide::Left; p1.designator = "P1"; p1.pinCount = 3;
        HarnessConnector p2;
        p2.id = "p2"; p2.side = HarnessSide::Right; p2.designator = "P2"; p2.pinCount = 3;
        p2.order = 1; // stacked below, so every wire has to drop
        HarnessConnector top;
        top.id = "p3"; top.side = HarnessSide::Right; top.designator = "P3"; top.pinCount = 1;
        top.order = 0;
        d.connectors = {p1, top, p2};
        for (int i = 1; i <= 3; ++i) {
            HarnessWire w;
            w.id = QStringLiteral("w%1").arg(i);
            w.fromConn = "p1"; w.fromPin = i; w.toConn = "p2"; w.toPin = i;
            d.wires.push_back(w);
        }
        // Collect each knee's x against the row it left from.
        std::vector<std::pair<double, double>> byRow; // (source y, knee x)
        for (const auto& e : buildHarness(d).entities)
            if (e.type == EntityType::Wire && e.points.size() == 4)
                byRow.push_back({e.points.front().y, e.points[1].x});
        check(byRow.size() == 3, "knee order: all three wires have knees");
        // Highest source row (largest y) must have the largest knee x.
        std::sort(byRow.begin(), byRow.end(),
                  [](const auto& a, const auto& b) { return a.first > b.first; });
        check(byRow[0].second > byRow[1].second && byRow[1].second > byRow[2].second,
              "knee order: the wire reaching furthest turns last, so the lanes nest");
        // And the knees are all distinct — no two wires share a lane.
        check(!near(byRow[0].second, byRow[1].second) && !near(byRow[1].second, byRow[2].second),
              "knee order: every wire gets its own lane");
        // Nesting means no vertical run crosses another wire's horizontal run.
        const HarnessBuild built = buildHarness(d);
        std::vector<Entity> ws;
        for (const auto& e : built.entities)
            if (e.type == EntityType::Wire) ws.push_back(e);
        int crossings = 0;
        for (size_t i = 0; i < ws.size(); ++i)
            for (size_t j = 0; j < ws.size(); ++j) {
                if (i == j) continue;
                for (size_t si = 1; si < ws[i].points.size(); ++si)
                    for (size_t sj = 1; sj < ws[j].points.size(); ++sj)
                        if (segmentSegmentIntersection(ws[i].points[si - 1], ws[i].points[si],
                                                       ws[j].points[sj - 1], ws[j].points[sj]))
                            ++crossings;
            }
        check(crossings == 0, "knee order: the routed bundle has no crossings at all");
    }

    // ---- Knee lanes: one fan, and as few crossings as the wiring allows ----
    {
        // The lane order itself, on rows alone. Two partly overlapping wires must be ordered
        // so neither vertical is cut: the one whose source row sits inside the other's reach
        // turns first.
        {
            const std::vector<int> lanes = harnessKneeLanes({{2, 5}, {4, 8}});
            check(lanes.size() == 2 && lanes[1] < lanes[0],
                  "knee lanes: of two overlapping wires, the lower one turns first");
        }
        {
            // Disjoint wires cannot cross whatever the order, but they still get their own
            // lanes so no two turns land on the same line.
            const std::vector<int> lanes = harnessKneeLanes({{0, 3}, {10, 14}, {20, 26}});
            check(lanes.size() == 3 && lanes[0] != lanes[1] && lanes[1] != lanes[2] &&
                      lanes[0] != lanes[2],
                  "knee lanes: every wire gets a lane of its own");
        }
        {
            // A wire spanning both of another's rows separates the rails: one crossing is
            // unavoidable, and the order must not make it two.
            const std::vector<int> lanes = harnessKneeLanes({{3, 4}, {1, 8}});
            check(lanes.size() == 2, "knee lanes: a nested pair still gets lanes");
        }

        // The reported picture: one 16-pin connector on the left feeding three connectors
        // stacked on the right. The lanes must form ONE fan - a common start line, an even
        // gap - and the routed result must not be worse than the wiring forces it to be.
        HarnessDef d;
        HarnessConnector p1;
        p1.id = "p1"; p1.side = HarnessSide::Left; p1.designator = "P1"; p1.pinCount = 16;
        d.connectors.push_back(p1);
        struct C { const char* id; const char* des; int pins; int order; };
        for (const C& c : {C{"p2", "P2", 4, 0}, C{"p3", "P3", 4, 1}, C{"p4", "P4", 4, 2}}) {
            HarnessConnector r;
            r.id = QString::fromLatin1(c.id);
            r.side = HarnessSide::Right;
            r.designator = QString::fromLatin1(c.des);
            r.pinCount = c.pins;
            r.order = c.order;
            d.connectors.push_back(r);
        }
        struct W { int from; const char* to; int toPin; };
        const W spec[] = {{7, "p2", 1}, {1, "p2", 4}, {2, "p2", 2}, {3, "p2", 3},
                          {5, "p3", 1}, {4, "p3", 4}, {6, "p4", 4}, {8, "p3", 3}};
        int n = 0;
        for (const W& sp : spec) {
            HarnessWire w;
            w.id = QStringLiteral("w%1").arg(++n);
            w.fromConn = "p1";
            w.fromPin = sp.from;
            w.toConn = QString::fromLatin1(sp.to);
            w.toPin = sp.toPin;
            d.wires.push_back(w);
        }
        const HarnessBuild built = buildHarness(d);
        std::vector<double> kneeX;
        for (const auto& e : built.entities)
            if (e.type == EntityType::Wire && e.points.size() == 4) kneeX.push_back(e.points[1].x);
        // Wires whose two pins happen to sit at the same height run straight across and have
        // no knee at all, so only the staggered ones are counted.
        check(kneeX.size() >= 5, "knee fan: the staggered wires turn");
        std::sort(kneeX.begin(), kneeX.end());
        bool even = true, distinct = true;
        for (size_t i = 1; i < kneeX.size(); ++i) {
            if (!near(kneeX[i] - kneeX[i - 1], kneeX[1] - kneeX[0])) even = false;
            if (near(kneeX[i], kneeX[i - 1])) distinct = false;
        }
        check(distinct, "knee fan: no two wires turn on the same line");
        check(even, "knee fan: the turns are evenly spaced from one start");
        check(near(kneeX[1] - kneeX[0], 15.0),
              "knee fan: the gap between turns is the connector pitch");

        // How many crossings the routing actually has, against the best ANY order of the same
        // turn lines could manage - worked out by trying every one of them. This is the real
        // question: not "is it zero" (a wire spanning another's rows has to cross it) but "did
        // the app pick the clearest order available".
        std::vector<std::vector<Vec2>> straight;   // wires with no knee: fixed, but still in the way
        std::vector<std::array<double, 4>> kneed;  // ax, ay, bx, by
        for (const auto& e : built.entities) {
            if (e.type != EntityType::Wire) continue;
            if (e.points.size() == 2) straight.push_back(e.points);
            else if (e.points.size() == 4)
                kneed.push_back({e.points.front().x, e.points.front().y, e.points.back().x,
                                 e.points.back().y});
        }
        std::vector<double> laneXs = kneeX; // the lines the generator used, in order
        auto countFor = [&](const std::vector<int>& lane) {
            std::vector<std::vector<Vec2>> ws = straight;
            for (size_t i2 = 0; i2 < kneed.size(); ++i2) {
                const double mx = laneXs[lane[i2]];
                ws.push_back({{kneed[i2][0], kneed[i2][1]},
                              {mx, kneed[i2][1]},
                              {mx, kneed[i2][3]},
                              {kneed[i2][2], kneed[i2][3]}});
            }
            int n2 = 0;
            for (size_t a2 = 0; a2 < ws.size(); ++a2)
                for (size_t b2 = a2 + 1; b2 < ws.size(); ++b2)
                    for (size_t sa = 1; sa < ws[a2].size(); ++sa)
                        for (size_t sb = 1; sb < ws[b2].size(); ++sb)
                            if (segmentSegmentIntersection(ws[a2][sa - 1], ws[a2][sa],
                                                           ws[b2][sb - 1], ws[b2][sb]))
                                ++n2;
            return n2;
        };
        // The order the generator chose, read back off the knee positions.
        std::vector<int> mine(kneed.size(), 0);
        for (size_t i2 = 0; i2 < kneed.size(); ++i2) {
            // kneed[] came out of the entity list in build order; find its lane by its x.
            double mx = 0.0;
            int seen = 0;
            for (const auto& e : built.entities)
                if (e.type == EntityType::Wire && e.points.size() == 4) {
                    if (seen == static_cast<int>(i2)) { mx = e.points[1].x; break; }
                    ++seen;
                }
            mine[i2] = static_cast<int>(std::lower_bound(laneXs.begin(), laneXs.end(), mx - 1e-6) -
                                        laneXs.begin());
        }
        const int gotFan = countFor(mine);
        std::vector<int> perm(kneed.size());
        for (size_t i2 = 0; i2 < perm.size(); ++i2) perm[i2] = static_cast<int>(i2);
        int best = gotFan;
        do {
            best = std::min(best, countFor(perm));
        } while (std::next_permutation(perm.begin(), perm.end()));
        std::printf("    (pin-map case: %d crossing(s) drawn, %d is the best any order can do)\n",
                    gotFan, best);
        check(gotFan == best,
              "knee fan: the turns are in the clearest order the wiring allows");
    }

    // ---- Every twist of a group braids the SAME wires ----
    {
        HarnessDef d;
        HarnessConnector p1;
        p1.id = "p1"; p1.side = HarnessSide::Left; p1.designator = "P1"; p1.pinCount = 4;
        HarnessConnector p2;
        p2.id = "p2"; p2.side = HarnessSide::Right; p2.designator = "P2"; p2.pinCount = 4;
        d.connectors = {p1, p2};
        for (int i = 1; i <= 4; ++i) {
            HarnessWire w;
            w.id = QStringLiteral("w%1").arg(i);
            w.fromConn = "p1"; w.fromPin = i; w.toConn = "p2"; w.toPin = i;
            d.wires.push_back(w);
        }
        // One twisted pair on the first two wires.
        d.wires[0].twistGroup = "T1";
        d.wires[1].twistGroup = "T1";
        d.layout.twistAmount = 2;
        std::vector<QStringList> perTwist;
        for (const auto& e : buildHarness(d).entities) {
            if (e.type != EntityType::Twist) continue;
            QStringList refs;
            for (const auto& v : e.meta.value("wires").toArray()) refs << v.toString();
            refs.sort();
            perTwist.push_back(refs);
            check(e.points.size() == 2, "twist pairing: each symbol spans exactly the pair");
        }
        check(perTwist.size() == 2, "twist pairing: both symbols were placed");
        check(perTwist[0] == perTwist[1],
              "twist pairing: the second symbol names the same wires as the first");
        check(perTwist[0].size() == 2, "twist pairing: each names both wires of the pair");
        // Both symbols really do belong to the pair — no symbol left unreferenced.
        for (const QStringList& refs : perTwist)
            check(!refs.isEmpty(), "twist pairing: no symbol is left as a bare decoration");
    }

    // ---- The reported case: wires to one destination must group away from the other ----
    {
        // P1(6) feeds P3(8) on pins 4,5,6 and P2(4) on pins 2,1,3. Whatever order the pins
        // end up in, the two destinations must not interleave — otherwise the wires going up
        // to P3 cross the ones going down to P2.
        HarnessDef d;
        HarnessConnector p1;
        p1.id = "p1"; p1.side = HarnessSide::Left; p1.designator = "P1"; p1.pinCount = 6;
        HarnessConnector p3;
        p3.id = "p3"; p3.side = HarnessSide::Right; p3.designator = "P3"; p3.pinCount = 8;
        p3.order = 0;
        HarnessConnector p2;
        p2.id = "p2"; p2.side = HarnessSide::Right; p2.designator = "P2"; p2.pinCount = 4;
        p2.order = 1;
        d.connectors = {p1, p3, p2};
        struct Spec { const char* id; int from; const char* to; int toPin; };
        const Spec specs[6] = {{"w4", 4, "p3", 1}, {"w2", 2, "p2", 3}, {"w1", 1, "p2", 2},
                               {"w5", 5, "p3", 6}, {"w6", 6, "p3", 3}, {"w3", 3, "p2", 1}};
        for (const Spec& sp : specs) {
            HarnessWire w;
            w.id = QString::fromLatin1(sp.id);
            w.fromConn = "p1";
            w.fromPin = sp.from;
            w.toConn = QString::fromLatin1(sp.to);
            w.toPin = sp.toPin;
            d.wires.push_back(w);
        }
        const int before = harnessCrossings(d);
        check(before > 0, "reported case: the starting wiring does cross");
        minimizeHarnessCrossings(d);
        const int after = harnessCrossings(d);
        std::printf("    (reported case: crossings %d -> %d)\n", before, after);
        check(after == 0, "reported case: the pinout can be untangled completely");
        // The P3-bound pins (4,5,6) and the P2-bound pins (2,1,3) each form a solid block.
        const HarnessConnector* p1o = harnessConnById(d, "p1");
        const std::vector<int> seq = harnessPinSequence(*p1o);
        QSet<int> toP3 = {4, 5, 6};
        int changes = 0;
        for (int i = 1; i < static_cast<int>(seq.size()); ++i)
            if (toP3.contains(seq[i]) != toP3.contains(seq[i - 1])) ++changes;
        check(changes == 1,
              "reported case: the two destinations end up in one block each, not interleaved");
    }

    // ---- The reported case WITH bundling: a twisted pair must be able to slide as a unit ----
    {
        // Exactly the builder's screenshot: P1(6) -> P3(8) and P2(4), every wire in one
        // shield, plus two twisted pairs (P1 1&2, P1 5&6). The optimum keeps both pairs
        // together AND has no crossings, but reaching it means sliding a whole pair past its
        // neighbours — something a swap of two single pins cannot do without breaking the pair
        // apart first, which the bundle weights forbid.
        auto build = []() {
            HarnessDef d;
            HarnessConnector p1;
            p1.id = "p1"; p1.side = HarnessSide::Left; p1.designator = "P1"; p1.pinCount = 6;
            HarnessConnector p3;
            p3.id = "p3"; p3.side = HarnessSide::Right; p3.designator = "P3"; p3.pinCount = 8;
            p3.order = 0;
            HarnessConnector p2;
            p2.id = "p2"; p2.side = HarnessSide::Right; p2.designator = "P2"; p2.pinCount = 4;
            p2.order = 1;
            d.connectors = {p1, p3, p2};
            struct S { const char* id; int from; const char* to; int toPin; const char* twist; };
            const S specs[6] = {{"w1", 1, "p2", 2, "T1"}, {"w2", 2, "p2", 3, "T1"},
                                {"w3", 3, "p2", 1, ""},   {"w4", 4, "p3", 1, ""},
                                {"w5", 5, "p3", 6, "T2"}, {"w6", 6, "p3", 3, "T2"}};
            for (const S& sp : specs) {
                HarnessWire w;
                w.id = QString::fromLatin1(sp.id);
                w.fromConn = "p1";
                w.fromPin = sp.from;
                w.toConn = QString::fromLatin1(sp.to);
                w.toPin = sp.toPin;
                w.shieldGroup = QStringLiteral("S1"); // every wire screened together
                w.twistGroup = QString::fromLatin1(sp.twist);
                d.wires.push_back(w);
            }
            return d;
        };
        HarnessDef d = build();
        minimizeHarnessCrossings(d);
        const int cross = harnessCrossings(d);
        std::printf("    (bundled case: crossings -> %d, twist breaks -> %d)\n", cross,
                    harnessBundleBreaks(d, false));
        check(cross == 0, "bundled case: the pinout is untangled even with the bundles held");
        check(harnessBundleBreaks(d, /*shield=*/false) == 0,
              "bundled case: both twisted pairs stay on neighbouring pins");
        check(harnessBundleBreaks(d, /*shield=*/true) == 0,
              "bundled case: the shield still covers a solid band");
        // Each pair really is adjacent on P1.
        const HarnessConnector* p1o = harnessConnById(d, "p1");
        const std::vector<int> seq = harnessPinSequence(*p1o);
        auto rowOf = [&seq](int pin) {
            return int(std::find(seq.begin(), seq.end(), pin) - seq.begin());
        };
        check(std::abs(rowOf(1) - rowOf(2)) == 1, "bundled case: pins 1 and 2 stay together");
        check(std::abs(rowOf(5) - rowOf(6)) == 1, "bundled case: pins 5 and 6 stay together");
        // Starting from the stuck order the user saw, it still reaches the optimum.
        HarnessDef stuck = build();
        for (auto& c : stuck.connectors)
            if (c.id == QLatin1String("p1")) c.pinOrder = {4, 3, 1, 2, 6, 5};
        minimizeHarnessCrossings(stuck);
        check(harnessCrossings(stuck) == 0,
              "bundled case: it escapes the order that pairwise swaps got stuck on");
    }

    // ---- New Layout controls: twist type, shield leg length, pin pitch ----
    {
        auto base = []() {
            HarnessDef d;
            HarnessConnector a;
            a.id = "p1"; a.side = HarnessSide::Left; a.designator = "P1"; a.pinCount = 4;
            HarnessConnector b;
            b.id = "p2"; b.side = HarnessSide::Right; b.designator = "P2"; b.pinCount = 4;
            d.connectors = {a, b};
            for (int i = 1; i <= 2; ++i) {
                HarnessWire w;
                w.id = QStringLiteral("w%1").arg(i);
                w.fromConn = "p1"; w.fromPin = i; w.toConn = "p2"; w.toPin = i;
                w.twistGroup = "T1";
                w.shieldGroup = "S1";
                d.wires.push_back(w);
            }
            d.layout.twistAmount = 1;
            d.layout.wireShieldAmount = 1;
            d.layout.shieldAmount = 1;
            return d;
        };

        // Twist type drives the symbol the generated twists use.
        HarnessDef braid = base();
        braid.layout.twistType = QStringLiteral("jog");
        for (const auto& e : buildHarness(braid).entities)
            if (e.type == EntityType::Twist) {
                check(e.meta.value("twistType").toString() == QLatin1String("jog"),
                      "layout: the twist type is carried onto the symbol");
                check(e.meta.contains("flatLen"),
                      "layout: a braid twist carries the parameters its type needs");
            }
        HarnessDef crossed = base();
        crossed.layout.twistType = QStringLiteral("crossed");
        for (const auto& e : buildHarness(crossed).entities)
            if (e.type == EntityType::Twist) {
                check(e.meta.value("twistType").toString() == QLatin1String("crossed"),
                      "layout: choosing Crossed generates a Type 1 twist");
                check(e.meta.contains("diagAngle") && e.meta.contains("capLen"),
                      "layout: a crossed twist carries its diagonal / cap parameters instead");
            }

        // Shield leg length: how far the drain reaches out of the screen's edge. An OVERALL
        // screen's leg is also carried clear of the connector body, so the length is a
        // MINIMUM there — but changing the setting still moves the end by exactly that much.
        auto legOf = [](const HarnessBuild& b) {
            for (const auto& e : b.entities)
                if (e.type == EntityType::Screen && e.points.size() >= 2) {
                    const int k = e.meta.value("catch").toInt(1);
                    double yMin = e.points.front().y, yMax = e.points.front().y;
                    for (int i = 0; i < k && i < static_cast<int>(e.points.size()); ++i) {
                        yMin = std::min(yMin, e.points[i].y);
                        yMax = std::max(yMax, e.points[i].y);
                    }
                    const double mid = (yMin + yMax) / 2.0;
                    const double ry = e.meta.value("ry").toDouble();
                    const Vec2 drain = e.points.back();
                    const double edge = (drain.y > mid) ? mid + ry : mid - ry;
                    return std::abs(drain.y - edge);
                }
            return -1.0;
        };
        HarnessDef shortLeg = base();
        shortLeg.connectors[0].shieldLeft = true;
        shortLeg.layout.shieldLegLength = 10.0;
        const double leg10 = legOf(buildHarness(shortLeg));
        check(leg10 >= 10.0 - 1e-6, "layout: the shield leg reaches at least the set length");
        HarnessDef longLeg = base();
        longLeg.connectors[0].shieldLeft = true;
        longLeg.layout.shieldLegLength = 40.0;
        check(near(legOf(buildHarness(longLeg)) - leg10, 30.0),
              "layout: raising the setting lengthens the leg by exactly that much");
        check(near(HarnessLayout().shieldLegLength, 20.0),
              "layout: a shield leg is 20 long unless told otherwise");

        // ---- Shield appearance and the body bond ----
        // A generated screen is drawn SOLID, and its outline follows the Shield type field.
        HarnessDef look = base();
        look.connectors[0].shieldLeft = true;
        look.layout.shieldType = QStringLiteral("capsule");
        int screens = 0;
        for (const auto& e : buildHarness(look).entities)
            if (e.type == EntityType::Screen) {
                ++screens;
                check(e.meta.value("lineType").toString() == QLatin1String("solid"),
                      "shield: a generated screen is drawn with a solid outline");
                check(e.meta.value("shape").toString() == QLatin1String("capsule"),
                      "shield: the Shield type field picks the outline");
            }
        check(screens > 0, "shield: the definition really produced a screen");
        HarnessDef ell = base();
        ell.connectors[0].shieldLeft = true;
        for (const auto& e : buildHarness(ell).entities)
            if (e.type == EntityType::Screen)
                check(e.meta.value("shape").toString() == QLatin1String("ellipse"),
                      "shield: an ellipse is the default outline");

        // The drain of an overall screen leaves PAST the connector body, and the bond back to
        // the body runs entirely outside it — so neither can cut across the other pins.
        {
            HarnessDef bonded;
            HarnessConnector a;
            a.id = "p1"; a.side = HarnessSide::Left; a.designator = "P1"; a.pinCount = 8;
            a.shieldLeft = true;
            a.bodyBondLeft = true;
            HarnessConnector b;
            b.id = "p2"; b.side = HarnessSide::Right; b.designator = "P2"; b.pinCount = 8;
            bonded.connectors = {a, b};
            for (int i = 1; i <= 3; ++i) { // only the top pins are wired: the rest are spare
                HarnessWire w;
                w.id = QStringLiteral("w%1").arg(i);
                w.fromConn = "p1"; w.fromPin = i; w.toConn = "p2"; w.toPin = i;
                bonded.wires.push_back(w);
            }
            bonded.layout.shieldAmount = 1;
            const HarnessBuild out = buildHarness(bonded);
            // The connector body, to judge the drain and the bond against.
            double bodyLo = 0.0, bodyHi = 0.0, bodyEdge = 0.0;
            bool haveBody = false;
            for (const auto& e : out.entities)
                if (e.type == EntityType::Connector &&
                    e.meta.value("designator").toString() == QLatin1String("P1")) {
                    const ConnectorGeometry g =
                        connectorGeometry(e.a, connectorSpecFromMeta(e.meta));
                    bodyLo = std::min(g.rectMin.y, g.rectMax.y);
                    bodyHi = std::max(g.rectMin.y, g.rectMax.y);
                    bodyEdge = std::max(g.rectMin.x, g.rectMax.x);
                    haveBody = true;
                }
            check(haveBody, "shield bond: the left connector was found");
            Vec2 drain{0.0, 0.0};
            bool haveDrain = false;
            for (const auto& e : out.entities)
                if (e.type == EntityType::Screen &&
                    e.meta.value("overallShield").toString() == QLatin1String("P1")) {
                    drain = e.points.back();
                    haveDrain = true;
                }
            check(haveDrain, "shield bond: the overall screen was generated");
            check(haveDrain && (drain.y > bodyHi + 1e-9 || drain.y < bodyLo - 1e-9),
                  "shield bond: the drain leaves clear of the connector body, not across it");
            const Entity* bond = nullptr;
            for (const auto& e : out.entities)
                if (e.type == EntityType::Polyline &&
                    e.meta.value("shieldBond").toString() == QLatin1String("P1"))
                    bond = &e;
            check(bond != nullptr, "shield bond: a bond to the body was drawn");
            if (bond) {
                check(bond->points.size() >= 2 && bond->points.back().x == drain.x &&
                          near(bond->points.back().y, drain.y),
                      "shield bond: it ends on the drain");
                check(near(bond->points.front().x, bodyEdge) &&
                          (near(bond->points.front().y, bodyLo) ||
                           near(bond->points.front().y, bodyHi)),
                      "shield bond: it starts on a body corner joint");
                // Every point of the bond is outside the body's rows, so it cannot cross a pin.
                bool clear = true;
                for (const Vec2& pt : bond->points)
                    if (pt.x > bodyEdge + 1e-9 && pt.y < bodyHi - 1e-9 && pt.y > bodyLo + 1e-9)
                        clear = false;
                check(clear, "shield bond: it never runs back across the pin rows");
            }
        }

        // ---- What is printed on a wire ----
        {
            HarnessDef marked = base();
            marked.wires[0].showColor = true;
            marked.wires[0].showGauge = true;
            marked.wires[0].label = QStringLiteral("PWR-1");
            marked.wires[1].showLabel = false;
            marked.wires[1].label = QStringLiteral("GND-1");
            int seen = 0;
            for (const auto& e : buildHarness(marked).entities) {
                if (e.type != EntityType::Wire) continue;
                if (e.meta.value("wireLabel").toString() == QLatin1String("PWR-1")) {
                    ++seen;
                    check(e.meta.value("showColorName").toBool(),
                          "markings: the colour tick reaches the wire");
                    check(e.meta.value("showGaugeName").toBool(),
                          "markings: the gauge tick reaches the wire");
                    check(e.meta.value("showWireLabel").toBool(),
                          "markings: a labelled wire shows its label by default");
                }
                if (e.meta.value("wireLabel").toString() == QLatin1String("GND-1")) {
                    ++seen;
                    check(!e.meta.value("showWireLabel").toBool(),
                          "markings: unticking the label hides it");
                    check(!e.meta.value("showColorName").toBool(),
                          "markings: an unticked colour is not printed");
                }
            }
            check(seen == 2, "markings: both labelled wires were generated");
            check(!HarnessWire().showColor && !HarnessWire().showGauge &&
                      HarnessWire().showLabel,
                  "markings: a new wire prints its label and nothing else");
        }

        // ---- Mixed pin annotation: a letter per block of ten, then the position ----
        {
            HarnessConnector c;
            c.pinCount = 24;
            c.annotation = QStringLiteral("mixed");
            check(harnessPinLabel(c, 1) == QLatin1String("A1"), "mixed: pin 1 is A1");
            check(harnessPinLabel(c, 10) == QLatin1String("A10"), "mixed: pin 10 is A10");
            check(harnessPinLabel(c, 11) == QLatin1String("B1"), "mixed: pin 11 starts block B");
            check(harnessPinLabel(c, 24) == QLatin1String("C4"), "mixed: pin 24 is C4");
            c.annotation = QStringLiteral("letters");
            check(harnessPinLabel(c, 3) == QLatin1String("C"), "mixed: letters are unaffected");
            c.annotation = QStringLiteral("numbers");
            check(harnessPinLabel(c, 3) == QLatin1String("3"), "mixed: numbers are unaffected");
            // A generated connector really carries the mixed labels onto its pins.
            HarnessDef md = base();
            for (auto& cc : md.connectors) cc.annotation = QStringLiteral("mixed");
            bool sawMixed = false;
            for (const auto& e : buildHarness(md).entities)
                if (e.type == EntityType::Connector) {
                    const ConnectorSpec sp = connectorSpecFromMeta(e.meta);
                    if (!sp.pins.isEmpty() && sp.pins.first() == QLatin1String("A1"))
                        sawMixed = true;
                }
            check(sawMixed, "mixed: the drawn connector's pins are named A1, A2, ...");
        }

        // ---- How a generated wire is coloured ----
        {
            HarnessDef actual = base();
            actual.wires[0].color = QStringLiteral("RD");
            actual.layout.wireColorMode = QStringLiteral("actual");
            bool sawOwn = false;
            for (const auto& e : buildHarness(actual).entities)
                if (e.type == EntityType::Wire &&
                    e.meta.value("wireColor").toString() == QLatin1String("RD")) {
                    sawOwn = e.hasExplicitColor() &&
                             e.color == wireColorHex(QStringLiteral("RD"));
                    check(!e.meta.contains("wireColorMode"),
                          "wire colour: nothing forces it by layer");
                }
            check(sawOwn, "wire colour: Actual paints the wire's own colour property");

            HarnessDef byLayer = base();
            byLayer.layout.wireColorMode = QStringLiteral("bylayer");
            int wires = 0;
            for (const auto& e : buildHarness(byLayer).entities)
                if (e.type == EntityType::Wire) {
                    ++wires;
                    check(!e.hasExplicitColor(),
                          "wire colour: By layer leaves the colour property alone");
                    check(e.meta.value("wireColorMode").toString() == QLatin1String("bylayer"),
                          "wire colour: By layer is recorded on the wire");
                    check(!e.meta.value("wireColor").toString().isEmpty(),
                          "wire colour: the code is still carried as information");
                }
            check(wires > 0, "wire colour: the by-layer case really produced wires");
            check(HarnessLayout().wireColorMode == QLatin1String("actual"),
                  "wire colour: Actual unless told otherwise");
        }

        // ---- Custom wire colours ----
        {
            registerCustomWireColor(QStringLiteral("LIME"), QStringLiteral("Lime"),
                                    QStringLiteral("#a4e400"));
            check(wireColorHex(QStringLiteral("LIME")) == QLatin1String("#a4e400"),
                  "custom colour: it resolves to the hex it was given");
            check(wireColorName(QStringLiteral("LIME")) == QLatin1String("Lime"),
                  "custom colour: it keeps its name");
            check(wireColorCodes().contains(QStringLiteral("LIME")),
                  "custom colour: it is offered alongside the standard codes");
            // Re-registering updates rather than duplicating, and a standard code is never
            // shadowed.
            registerCustomWireColor(QStringLiteral("LIME"), QStringLiteral("Lime green"),
                                    QStringLiteral("#b0f000"));
            check(wireColorHex(QStringLiteral("LIME")) == QLatin1String("#b0f000") &&
                      customWireColorCodes().count(QStringLiteral("LIME")) == 1,
                  "custom colour: re-adding one updates it in place");
            registerCustomWireColor(QStringLiteral("RD"), QStringLiteral("Not red"),
                                    QStringLiteral("#000000"));
            check(wireColorName(QStringLiteral("RD")) == QLatin1String("Red"),
                  "custom colour: a standard code cannot be overwritten");
        }

        // Pin pitch: the connectors are drawn at the pitch they carry, which the builder's
        // Layout field writes through to all of them.
        HarnessDef pitched = base();
        for (auto& c : pitched.connectors) c.pitch = 25.0;
        pitched.layout.pinPitch = 25.0;
        std::vector<double> pinYs;
        for (const auto& e : buildHarness(pitched).entities)
            if (e.type == EntityType::Connector) {
                const ConnectorGeometry g =
                    connectorGeometry(e.a, connectorSpecFromMeta(e.meta));
                if (g.pinEnd.size() >= 2)
                    pinYs.push_back(std::abs(g.pinEnd[0].y - g.pinEnd[1].y));
            }
        check(!pinYs.empty() && near(pinYs.front(), 25.0),
              "layout: the pin pitch is what the connector is drawn at");
    }

    std::printf(g_fail ? "  -> %d failed\n" : "  -> ok\n", g_fail);
    return g_fail;
}
