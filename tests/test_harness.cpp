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

    // Align routing helper: place two facing connectors (A fixed, B mirrored so its
    // pins face A) with B `dy` below, wire pin 0<->0, then align from A.
    auto alignCase = [](double bx, double by) {
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
        Entity w; w.id = "aw"; w.type = EntityType::Wire; w.points = {ga.pinEnd[0], gb.pinEnd[0]};
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
        // Facing + small offset -> straighten: B shifts into line, wire is 2 pts horizontal.
        auto r = alignCase(300, -30);
        check(r.built && r.wpts == 2 && near(r.way0, r.way1),
              "align-routing: small offset straightens the wire (0 deg)");
        check(near(r.bay, 0.0), "align-routing: the connector shifts into line when straightening");
    }
    {
        // Facing + large offset -> keep the connector, re-route the wire at 90 deg.
        auto r = alignCase(400, -400);
        check(r.built && r.wpts == 4, "align-routing: large offset re-routes the wire with 90 deg bends");
        check(near(r.bay, -400.0), "align-routing: the connector stays put when re-routing at 90 deg");
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
            w.points = {ga.pinEnd[i], gb.pinEnd[i]}; // pin i -> pin i (monotonic)
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

    std::printf(g_fail ? "  -> %d failed\n" : "  -> ok\n", g_fail);
    return g_fail;
}
