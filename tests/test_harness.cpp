#include "app/Editor.h"
#include "geometry/Snap.h"
#include "harness/Harness.h"
#include "harness/HarnessBuilder.h"
#include "harness/Bom.h"
#include "model/Block.h"
#include "harness/MechanicalHarness.h"
#include <QSettings>
#include "harness/PartsLibrary.h"
#include "geometry/Geometry.h"
#include "model/Document.h"
#include "tools/EntityTransforms.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QSet>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

using namespace Loopwire;

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

    // Placement defaults scale the actual dynamic geometry, not just its controls.
    {
        Editor editor;
        const ConnectorSpec small = editor.connectorSpec();
        const ConnectorGeometry original = connectorGeometry({0, 0}, defaultConnectorSpec(4));
        const ConnectorGeometry placed = connectorGeometry({0, 0}, small);
        check(near(placed.effectiveWidth, original.effectiveWidth * 0.4) &&
                  near(placed.effectiveHeight, original.effectiveHeight * 0.4),
              "connector: placement body defaults to 40 percent");
        for (size_t i = 0; i < placed.pinEnd.size(); ++i)
            check(near(placed.pinEnd[i].x, original.pinEnd[i].x * 0.4) &&
                      near(placed.pinEnd[i].y, original.pinEnd[i].y * 0.4),
                  "connector: placement pin geometry keeps its proportions");
        QJsonObject meta;
        writeConnectorSpec(meta, small);
        const ConnectorSpec restored = connectorSpecFromMeta(meta);
        check(near(restored.rectWidth, small.rectWidth) && near(restored.pinSize, small.pinSize),
              "connector: smaller placement size survives saving");
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
        check(wireAnnotColorText(w) == QLatin1String("Red"),
              "wire marking: the colour reads by its full name unless told otherwise");
        w.meta.insert("colorNameMode", QStringLiteral("code"));
        check(wireAnnotColorText(w) == QLatin1String("RD"),
              "wire marking: asking for the code gives the 2 letters");
        w.meta.remove(QStringLiteral("colorNameMode"));
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
            // One overall screen per shielded connector end is the default now: a second copy
            // of the same screen further along the run said nothing extra.
            check(overall == 1, "harness: one overall shield symbol per shielded end");
            check(internal == 2, "harness: the wire shield is deployed with its own amount 2");
            // ...and asking for more still gives more.
            {
                HarnessDef more = d;
                more.layout.shieldAmount = 3;
                int n = 0;
                for (const auto& e : buildHarness(more).entities)
                    if (e.type == EntityType::Screen && e.meta.contains("overallShield")) ++n;
                check(n == 3, "harness: the overall shield amount is still honoured");
            }
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
            // Drawing ON from the wire's own free tip is one conductor carrying on, not two
            // wires meeting, so there is no joint to mark: one wire and nothing else.
            int wires = 0, splices = 0;
            for (const auto& e : d.entities()) {
                if (e.type == EntityType::Wire) ++wires;
                if (e.type == EntityType::Splice) ++splices;
            }
            check(wires == 1, "wire extend: no second wire is created");
            check(splices == 0, "wire extend: carrying a wire on is not a joint");
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
            int wires2 = 0;
            for (const auto& e : d.entities())
                if (e.type == EntityType::Wire) ++wires2;
            // The leg drawn on to the end runs at an angle, and wires are drawn with square corners: it gets a knee
            // before it lands, so the one wire has four points where it had three.
            check(wires2 == 1 && d.entities().front().points.size() == 4,
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
            int wires3 = 0;
            for (const auto& e : d.entities())
                if (e.type == EntityType::Wire) ++wires3;
            check(wires3 == 1, "wire extend: the front end joins too");
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

    // ---- Cross-section notation follows the general option ----
    {
        setWireGaugeNotationAwg(false);
        check(wireGaugeLabel(QStringLiteral("0.5")) == QString::fromUtf8("0.5 mm\xC2\xB2"),
              "gauge notation: mm2 by default");
        setWireGaugeNotationAwg(true);
        check(wireGaugeLabel(QStringLiteral("0.5")) == QLatin1String("20 AWG"),
              "gauge notation: the same wire reads as AWG when the option is set");
        check(wireGaugeAwg(QStringLiteral("2.5")) == QLatin1String("14"),
              "gauge notation: the standard pairing is used");
        check(wireGaugeAwg(QStringLiteral("0.55")) == QLatin1String("20"),
              "gauge notation: an off-standard figure takes the nearest size");

        // A wire's marking reads as ONE line, and follows the notation.
        Entity w;
        w.type = EntityType::Wire;
        w.points = {{0, 0}, {100, 0}};
        w.meta.insert("wireColor", QStringLiteral("RD"));
        w.meta.insert("wireGauge", QStringLiteral("0.5"));
        w.meta.insert("showColorName", true);
        w.meta.insert("showGaugeName", true);
        w.meta.insert("colorNameMode", QStringLiteral("code")); // the 2-letter form, to keep short
        check(wireAnnotText(w) == QStringLiteral("RD  20 AWG"),
              "marking: the colour and the cross-section are one line of text");
        setWireGaugeNotationAwg(false);
        check(wireAnnotText(w) == QString::fromUtf8("RD  0.5 mm\xC2\xB2"),
              "marking: switching notation re-labels it without touching the wire");
        w.meta.insert("showGaugeName", false);
        check(wireAnnotText(w) == QStringLiteral("RD"),
              "marking: one part alone is the whole line");
        w.meta.insert("showColorName", false);
        check(wireAnnotText(w).isEmpty(), "marking: nothing shown, nothing drawn");
        check(near(wireTextGap(w), 0.0), "marking: no gap over the line unless asked for");
        w.meta.insert("textGap", 4.0);
        check(near(wireTextGap(w), 4.0), "marking: the text gap is read off the wire");
    }

    // ---- The generated drawing's rules: wire terminations, splices, text gap ----
    {
        auto twoConn = []() {
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
            return d;
        };
        // The text gap reaches the drawn wires.
        HarnessDef gap = twoConn();
        gap.layout.textGap = 5.0;
        int gapped = 0;
        for (const auto& e : buildHarness(gap).entities)
            if (e.type == EntityType::Wire && near(e.meta.value("textGap").toDouble(), 5.0))
                ++gapped;
        check(gapped == 2, "rules: the text gap is carried onto every wire");
        HarnessDef noGap = twoConn();
        for (const auto& e : buildHarness(noGap).entities)
            if (e.type == EntityType::Wire)
                check(!e.meta.contains("textGap"), "rules: no gap set, nothing stored");

        // A wire naming a pin that is not there lands on nothing: with the rule on, the drawing
        // marks that end.
        HarnessDef dangling = twoConn();
        dangling.wires[0].toPin = 99; // out of range: routed to nowhere
        auto crossCount = [](const HarnessBuild& b, EntityType t) {
            int n = 0;
            for (const auto& e : b.entities)
                if (e.type == t) ++n;
            return n;
        };
        dangling.layout.terminateUnconnectedWires = false;
        const int without = crossCount(buildHarness(dangling), EntityType::NotConnected);
        dangling.layout.terminateUnconnectedWires = true;
        const int with = crossCount(buildHarness(dangling), EntityType::NotConnected);
        check(with >= without, "rules: terminating unconnected wires only ever adds marks");

        // Splices mark where wires MEET. Two wires on one PIN is not a joint - it is a wiring
        // fault, which the validator reports; dotting it would say the drawing was fine.
        HarnessDef onePin = twoConn();
        onePin.wires[1].fromPin = 1; // both wires leave the same pin
        onePin.layout.spliceAtJoins = true;
        check(crossCount(buildHarness(onePin), EntityType::Splice) == 0,
              "rules: two wires on one pin are not spliced");
        bool pinFault = false;
        for (const auto& iss : validateHarness(onePin))
            if (iss.key.startsWith(QLatin1String("pinbusy"))) pinFault = true;
        check(pinFault, "rules: ...the validator reports it instead");

        // A drain brought onto a screen's leg IS a joint, and gets its dot.
        HarnessDef drained = twoConn();
        drained.connectors[0].shieldLeft = true;
        drained.layout.shieldAmount = 1;
        {
            HarnessWire dr;
            dr.id = "dr";
            dr.fromConn = "p1";
            dr.fromPin = 4;
            dr.toConn = harnessOverallShieldKey(QStringLiteral("p1"), HarnessSide::Left);
            drained.wires.push_back(dr);
        }
        drained.layout.spliceAtJoins = true;
        const int spliced = crossCount(buildHarness(drained), EntityType::Splice);
        check(spliced >= 1, "rules: a wire meeting a screen's leg is spliced");
        drained.layout.spliceAtJoins = false;
        check(crossCount(buildHarness(drained), EntityType::Splice) == 0,
              "rules: with the rule off, no joint is drawn");
        HarnessDef joined = drained;
        joined.layout.spliceAtJoins = true;
        check(HarnessLayout().spliceAtJoins,
              "rules: a join is spliced unless told otherwise");
        // An overall screen is drawn ONCE unless more are asked for.
        check(HarnessLayout().shieldAmount == 1,
              "rules: one overall shield symbol unless told otherwise");
        // ...and one symbol really is what gets drawn.
        {
            HarnessDef one;
            HarnessConnector a1;
            a1.id = "p1"; a1.side = HarnessSide::Left; a1.designator = "P1"; a1.pinCount = 4;
            a1.shieldLeft = true;
            HarnessConnector b1;
            b1.id = "p2"; b1.side = HarnessSide::Right; b1.designator = "P2"; b1.pinCount = 4;
            one.connectors = {a1, b1};
            for (int i = 1; i <= 2; ++i) {
                HarnessWire w;
                w.id = QStringLiteral("w%1").arg(i);
                w.fromConn = "p1"; w.fromPin = i; w.toConn = "p2"; w.toPin = i;
                one.wires.push_back(w);
            }
            int overall = 0;
            for (const auto& e : buildHarness(one).entities)
                if (e.type == EntityType::Screen &&
                    e.meta.value("overallShield").toString() == QLatin1String("P1"))
                    ++overall;
            check(overall == 1, "rules: exactly one overall screen is drawn by default");
        }

        // The joint dot is drawn at the size the Layout gives it.
        check(near(HarnessLayout().spliceSize, 3.0), "rules: a joint dot is 3 by default");
        HarnessDef sized = joined; // the drain-onto-a-leg case, which really has a joint
        sized.layout.spliceSize = 7.5;
        int sizedDots = 0;
        for (const auto& e : buildHarness(sized).entities)
            if (e.type == EntityType::Splice) {
                ++sizedDots;
                check(near(e.meta.value("size").toDouble(), 3.75),
                      "rules: connection dots use half the layout scale");
            }
        check(sizedDots > 0, "rules: the sized case really produced a joint");
        check(!HarnessLayout().terminateUnconnectedWires,
              "rules: unconnected wire ends are not marked unless asked for");
    }

    // ---- Align lines up everything that rides on a wire ----
    {
        Document d;
        d.entities().clear();
        // Two horizontal wires, each carrying a colour marking, plus a twist across them and
        // a shield catching them.
        for (int i = 0; i < 2; ++i) {
            Entity w;
            w.id = QStringLiteral("w%1").arg(i + 1);
            w.type = EntityType::Wire;
            w.points = {{0, -20.0 * i}, {300, -20.0 * i}};
            w.meta.insert("wireColor", QStringLiteral("RD"));
            w.meta.insert("wireGauge", QStringLiteral("0.5"));
            w.meta.insert("showColorName", true);
            w.meta.insert("annCount", 1);
            QJsonArray ts;
            ts.append(0.2); // both markings start well left of the column
            w.meta.insert("annT", ts);
            d.entities().push_back(w);
        }
        Editor ed;
        ed.setDocument(&d);
        ed.selectById(QStringLiteral("w1"));
        ed.selectById(QStringLiteral("w2"), /*additive=*/true);
        ed.beginTwistAlign();
        ed.onPointerPress({210, 0}, Qt::LeftButton, Qt::NoModifier);
        for (const auto& e : d.entities()) {
            if (e.type != EntityType::Wire) continue;
            const std::vector<double> ts = wireAnnotFractions(e, 1);
            check(!ts.empty() && near(ts[0], 0.7, 1e-3),
                  "align: each wire's marking lands on the clicked column");
        }

        // A wire label attached to a wire follows the same column.
        Entity lab;
        lab.id = "lab1";
        lab.type = EntityType::NetLabel;
        lab.a = {30, 0};
        lab.meta.insert("wireId", QStringLiteral("w1"));
        lab.meta.insert("annT", 0.1);
        lab.meta.insert("text", QStringLiteral("PWR"));
        d.entities().push_back(lab);
        ed.selectById(QStringLiteral("lab1"));
        ed.beginTwistAlign();
        ed.onPointerPress({150, 0}, Qt::LeftButton, Qt::NoModifier);
        for (const auto& e : d.entities())
            if (e.id == QLatin1String("lab1"))
                check(near(e.meta.value("annT").toDouble(), 0.5, 1e-3),
                      "align: an attached wire label slides to the column too");

        // Something that rides on nothing cannot be aligned, and Align says so rather than
        // moving it.
        Entity rect;
        rect.id = "r1";
        rect.type = EntityType::Rect;
        rect.points = {{0, 0}, {10, 10}};
        d.entities().push_back(rect);
        ed.selectById(QStringLiteral("r1"));
        ed.beginTwistAlign();
        ed.onPointerPress({150, 0}, Qt::LeftButton, Qt::NoModifier);
        for (const auto& e : d.entities())
            if (e.id == QLatin1String("r1"))
                check(near(e.points.front().x, 0.0),
                      "align: an object that rides on no wire is left where it is");
    }

    // ---- The drain: a wire of its own, joined to the leg it terminates ----
    {
        HarnessDef d;
        HarnessConnector a1;
        a1.id = "p1"; a1.side = HarnessSide::Left; a1.designator = "P1"; a1.pinCount = 4;
        a1.shieldLeft = true;
        a1.bodyBondLeft = true;
        HarnessConnector b1;
        b1.id = "p2"; b1.side = HarnessSide::Right; b1.designator = "P2"; b1.pinCount = 4;
        d.connectors = {a1, b1};
        for (int i = 1; i <= 2; ++i) {
            HarnessWire w;
            w.id = QStringLiteral("w%1").arg(i);
            w.fromConn = "p1"; w.fromPin = i; w.toConn = "p2"; w.toPin = i;
            w.label = QStringLiteral("L%1").arg(i);
            d.wires.push_back(w);
        }
        d.layout.shieldAmount = 1;
        d.layout.wireColorMode = QStringLiteral("actual");

        // The definition's drain spec reaches the drawn drain.
        HarnessBuild out = buildHarness(d);
        const Entity* drain = nullptr;
        for (const auto& e : out.entities)
            if (e.type == EntityType::Wire && e.meta.contains("shieldBond")) drain = &e;
        check(drain != nullptr, "drain: it is drawn as a wire");
        check(drain && drain->meta.value("wireColor").toString() == QLatin1String("DRAIN"),
              "drain: it is given the Drain colour, not a setting of its own");
        check(drain && drain->hasExplicitColor(),
              "drain: in Actual mode it is painted its own colour, like any wire");
        check(wireColorName(QStringLiteral("DRAIN")) == QLatin1String("Drain") &&
                  wireColorCodes().contains(QStringLiteral("DRAIN")),
              "drain: Drain is offered in the wire colour list");

        // Stamping the harness into a document joins the screen to its drain, which is what
        // takes the X off the leg and lets the shield pull the drain when it is moved.
        Document doc;
        doc.entities().clear();
        Editor ed;
        ed.setDocument(&doc);
        ed.insertHarness(d);
        const Entity* screen = nullptr;
        const Entity* placed = nullptr;
        int drains = 0;
        for (const auto& e : doc.entities()) {
            if (e.type == EntityType::Screen &&
                e.meta.value("overallShield").toString() == QLatin1String("P1"))
                screen = &e;
            if (e.type == EntityType::Wire && e.meta.contains("shieldBond")) {
                placed = &e;
                ++drains;
            }
        }
        check(drains == 1, "drain: exactly one is placed");
        check(screen && placed &&
                  screen->meta.value("legWireId").toString() == placed->id,
              "drain: the screen is joined to it by id once both are in the document");
        check(screen && !screen->meta.value("notConnected").toBool(),
              "drain: the leg is terminated on it, so no X is drawn");
        check(screen && screen->meta.contains("legWireEnd"),
              "drain: which end of the drain is on the leg is recorded, so it can be pulled");

        // And the drain must not have stolen a shield's wire references while ids were stamped.
        check(screen && screen->meta.value("wires").toArray().size() == 2,
              "drain: the screen still refers to the two wires it catches");
        for (const auto& v : screen->meta.value("wires").toArray()) {
            const QString wid = v.toString();
            bool isCaughtWire = false;
            for (const auto& e : doc.entities())
                if (e.id == wid && e.type == EntityType::Wire && !e.meta.contains("shieldBond"))
                    isCaughtWire = true;
            check(isCaughtWire, "drain: a shield reference points at a conductor, not the drain");
        }
    }

    // ---- Two wires meeting become one, with a splice at the joint ----
    {
        Document d;
        d.entities().clear();
        auto wire = [](const char* id, Vec2 a, Vec2 b) {
            Entity w;
            w.id = QString::fromLatin1(id);
            w.type = EntityType::Wire;
            w.points = {a, b};
            w.meta.insert("wireColor", QStringLiteral("RD"));
            return w;
        };
        d.entities().push_back(wire("w1", {0, 0}, {100, 0}));
        d.entities().push_back(wire("w2", {100, 0}, {100, -80})); // its front meets w1's back
        Editor ed;
        ed.setDocument(&d);
        check(ed.mergeWireEnds(QStringLiteral("w1")), "wire join: the meeting ends are joined");
        int wires = 0, splices = 0;
        const Entity* kept = nullptr;
        for (const auto& e : d.entities()) {
            if (e.type == EntityType::Wire) { ++wires; kept = &e; }
            if (e.type == EntityType::Splice) ++splices;
        }
        check(wires == 1, "wire join: one wire is left, not two");
        check(splices == 1, "wire join: the joint is spliced");
        check(kept && kept->points.size() == 3,
              "wire join: the shared point is kept once, not duplicated");
        check(kept && near(kept->points.front().x, 0.0) && near(kept->points.back().y, -80.0),
              "wire join: the run reads continuously from one far end to the other");
        for (const auto& e : d.entities())
            if (e.type == EntityType::Splice)
                check(near(e.a.x, 100.0) && near(e.a.y, 0.0),
                      "wire join: the splice sits where they met");

        // Joining a second time has nothing to join.
        check(!ed.mergeWireEnds(kept->id), "wire join: nothing left to join");

        // A tip that TERMINATES is a proper end: two conductors meeting on a pin stay two.
        Document t;
        t.entities().clear();
        ConnectorSpec spec = defaultConnectorSpec(2);
        Entity conn;
        conn.id = "c1";
        conn.type = EntityType::Connector;
        conn.a = {0, 0};
        writeConnectorSpec(conn.meta, spec);
        const ConnectorGeometry cg = connectorGeometry(conn.a, spec);
        t.entities().push_back(conn);
        const Vec2 pin = cg.pinEnd.front();
        t.entities().push_back(wire("t1", pin, {pin.x + 100, pin.y}));
        t.entities().push_back(wire("t2", pin, {pin.x, pin.y - 90}));
        Editor te;
        te.setDocument(&t);
        check(!te.mergeWireEnds(QStringLiteral("t1")),
              "wire join: two wires meeting on a pin are left as two conductors");
    }

    // ---- The palette and the cross-section range ----
    {
        // Black was sharing a hex with grey, which is why it read as grey.
        check(wireColorHex(QStringLiteral("BK")) == QLatin1String("#000000"),
              "palette: black is black");
        check(wireColorHex(QStringLiteral("BK")) != wireColorHex(QStringLiteral("GY")),
              "palette: black and grey are no longer the same colour");
        // A coax has no colour: it has a signal and a shield.
        check(wireColorName(QStringLiteral("SIGNAL")) == QLatin1String("Signal") &&
                  wireColorName(QStringLiteral("SHIELD")) == QLatin1String("Shield"),
              "palette: Signal and Shield are named");
        for (const QString& code : {QStringLiteral("SIGNAL"), QStringLiteral("SHIELD"),
                                    QStringLiteral("DRAIN")})
            check(wireColorCodes().contains(code),
                  "palette: the conductor kinds are offered alongside the colours");

        // Cross-sections: 4 AWG to 32 AWG, thin end first, and nothing outside that.
        const QStringList sizes = wireGaugeChoices();
        check(!sizes.isEmpty(), "gauge range: there are sizes to choose from");
        check(sizes.front() == QLatin1String("0.03") && sizes.back() == QLatin1String("25.0"),
              "gauge range: it runs from 32 AWG to 4 AWG");
        for (const QString& mm2 : sizes) {
            const int awg = wireGaugeAwg(mm2).toInt();
            check(awg >= 4 && awg <= 32, "gauge range: every size is between 4 and 32 AWG");
        }
        setWireGaugeNotationAwg(true);
        check(wireGaugeLabel(QStringLiteral("0.03")) == QLatin1String("32 AWG") &&
                  wireGaugeLabel(QStringLiteral("25.0")) == QLatin1String("4 AWG"),
              "gauge range: the ends of the range read as 32 and 4 AWG");
        setWireGaugeNotationAwg(false);
        // Ascending by area, so the list reads in order.
        for (int i = 1; i < sizes.size(); ++i)
            check(sizes[i].toDouble() > sizes[i - 1].toDouble(),
                  "gauge range: the sizes are in order");
    }

    // ---- Text height, colour-name length, and the screen's clearance ----
    {
        HarnessDef d;
        HarnessConnector p1;
        p1.id = "p1"; p1.side = HarnessSide::Left; p1.designator = "P1"; p1.pinCount = 4;
        p1.pitch = 24.0; // a wide pitch, so a proportional clearance is obvious
        p1.shieldLeft = true;
        HarnessConnector p2;
        p2.id = "p2"; p2.side = HarnessSide::Right; p2.designator = "P2"; p2.pinCount = 4;
        p2.pitch = 24.0;
        d.connectors = {p1, p2};
        for (int i = 1; i <= 2; ++i) {
            HarnessWire w;
            w.id = QStringLiteral("w%1").arg(i);
            w.fromConn = "p1"; w.fromPin = i; w.toConn = "p2"; w.toPin = i;
            w.showColor = true;
            d.wires.push_back(w);
        }
        d.layout.shieldAmount = 1;
        d.layout.pinPitch = 24.0;
        d.layout.textHeight = 6.0;
        d.layout.colorNameMode = QStringLiteral("long");

        const HarnessBuild out = buildHarness(d);
        int wires = 0;
        for (const auto& e : out.entities)
            if (e.type == EntityType::Wire) {
                ++wires;
                check(near(e.meta.value("annSize").toDouble(), 6.0),
                      "text: the height from the layout reaches the wire");
                check(e.meta.value("colorNameMode").toString() == QLatin1String("full"),
                      "text: Long spells the colour out");
            }
        check(wires > 0, "text: the wires were generated");
        check(near(HarnessLayout().textHeight, 3.5) &&
                  HarnessLayout().colorNameMode == QLatin1String("long"),
              "text: 3.5 high and full colour names unless told otherwise");
        // And the marking really reads as the full name.
        for (const auto& e : out.entities)
            if (e.type == EntityType::Wire && e.meta.value("showColorName").toBool()) {
                check(wireAnnotColorText(e) == wireColorName(
                                                  e.meta.value("wireColor").toString()),
                      "text: the wire's marking is the spelled-out colour");
                break;
            }

        // The screen clears the outermost wire by about a quarter of the pin pitch, so the
        // clearance follows the harness instead of being a fixed number of units.
        for (const auto& e : out.entities) {
            if (e.type != EntityType::Screen) continue;
            const int k = e.meta.value("catch").toInt(1);
            if (k < 2) continue;
            double lo = e.points.front().y, hi = e.points.front().y;
            for (int i = 0; i < k; ++i) {
                lo = std::min(lo, e.points[i].y);
                hi = std::max(hi, e.points[i].y);
            }
            const double clear = e.meta.value("ry").toDouble() - (hi - lo) / 2.0;
            std::printf("    (shield clearance at pitch 24: %.2f)\n", clear);
            check(clear > 24.0 / 4.0 - 0.01 && clear < 24.0,
                  "shield: the outline clears the last wire by about a quarter of the pitch");
        }
    }

    // ---- An OPEN end: a flying lead that runs out and stops ----
    {
        HarnessDef d;
        HarnessConnector p1;
        p1.id = "p1"; p1.side = HarnessSide::Left; p1.designator = "P1"; p1.pinCount = 4;
        HarnessConnector p2;
        p2.id = "p2"; p2.side = HarnessSide::Right; p2.designator = "P2"; p2.pinCount = 4;
        d.connectors = {p1, p2};
        HarnessWire joined;
        joined.id = "w1";
        joined.fromConn = "p1"; joined.fromPin = 1; joined.toConn = "p2"; joined.toPin = 1;
        d.wires.push_back(joined);
        HarnessWire lead;
        lead.id = "lead";
        lead.fromConn = "p1";
        lead.fromPin = 2;
        lead.toConn = harnessOpenEndpointKey();
        lead.color = QStringLiteral("RD");
        d.wires.push_back(lead);

        check(harnessIsOpenEndpoint(harnessOpenEndpointKey()),
              "open end: the key is recognised");
        check(!harnessIsOpenEndpoint(QStringLiteral("p1")) &&
                  !harnessIsShieldEndpoint(harnessOpenEndpointKey()),
              "open end: it is not mistaken for a connector or a screen");
        check(validateHarness(d).empty(), "open end: a flying lead validates");

        const HarnessBuild out = buildHarness(d);
        const Entity* drawn = nullptr;
        for (const auto& e : out.entities)
            if (e.type == EntityType::Wire && e.meta.value("openEnd").toBool()) drawn = &e;
        check(drawn != nullptr, "open end: the lead is drawn");
        check(drawn && drawn->points.size() == 2, "open end: it runs straight out");
        check(drawn && drawn->meta.value("wireColor").toString() == QLatin1String("RD"),
              "open end: it keeps its own colour");
        // It leaves the pin and heads INTO the harness, not backwards through the connector.
        if (drawn)
            check(drawn->points.back().x > drawn->points.front().x,
                  "open end: a left-hand lead runs to the right, away from its connector");
        // And it is marked as reaching nothing.
        bool marked = false;
        if (drawn)
            for (const auto& e : out.entities)
                if (e.type == EntityType::NotConnected &&
                    near(e.a.x, drawn->points.back().x) && near(e.a.y, drawn->points.back().y))
                    marked = true;
        check(marked, "open end: the loose end carries the not-connected mark");

        // Open at BOTH ends joins nothing, and the validator says so.
        HarnessDef both = d;
        both.wires[1].fromConn = harnessOpenEndpointKey();
        bool reported = false;
        for (const auto& iss : validateHarness(both))
            if (iss.key.startsWith(QLatin1String("bothopen"))) reported = true;
        check(reported, "open end: open at both ends is reported");
        buildHarness(both); // and draws nothing for it rather than crashing

        // The pin at the other end is still checked.
        HarnessDef badPin = d;
        badPin.wires[1].fromPin = 99;
        bool pinReported = false;
        for (const auto& iss : validateHarness(badPin))
            if (iss.key.startsWith(QLatin1String("badpin"))) pinReported = true;
        check(pinReported, "open end: the connector end of a lead is still checked");
    }

    // ---- The cross-section nudges the drawn width; it does not take it over ----
    {
        // The renderer starts from the layer's weight and shifts it by the cross-section, capped
        // at both ends: a 10 AWG conductor must still look like a wire, and a net wire on a
        // weight-1 layer must still be thin. The weights below are what that shift is read from,
        // so they have to rise across the whole offered range and every size needs its own.
        check(wireGaugeWeight(QStringLiteral("0.5")) < wireGaugeWeight(QStringLiteral("2.5")),
              "gauge width: a bigger cross-section weighs more");
        check(wireGaugeWeight(QStringLiteral("2.5")) < wireGaugeWeight(QStringLiteral("25.0")),
              "gauge width: ...all the way up the range");
        double last = -1.0;
        bool rising = true, allKnown = true;
        for (const QString& mm2 : wireGaugeChoices()) {
            const double w = wireGaugeWeight(mm2);
            if (near(w, 2.0) && mm2 != QLatin1String("1.0")) allKnown = false;
            if (w < last) rising = false;
            last = w;
        }
        check(rising, "gauge width: the offered sizes get heavier in order");
        check(allKnown, "gauge width: every offered size has a weight of its own");
        check(wireGaugeChoices().size() >= 15, "gauge width: the 4-32 AWG range is offered");
        check(wireGaugeChoices().first() == QLatin1String("0.03") &&
                  wireGaugeChoices().last() == QLatin1String("25.0"),
              "gauge width: from 32 AWG up to 4 AWG");
        // The shift the renderer applies is bounded: across the whole range it stays within
        // about two pixels of the layer's own weight, which is what keeps a thick wire sane.
        const double spread = 0.28 * (wireGaugeWeight(QStringLiteral("25.0")) -
                                      wireGaugeWeight(QStringLiteral("0.03")));
        check(spread > 0.5 && spread < 3.0,
              "gauge width: the whole range spans a couple of pixels, not a bar");
    }

    // ---- A wire's own label slides along it ----
    {
        Entity w;
        w.id = "w1";
        w.type = EntityType::Wire;
        w.points = {{0, 0}, {200, 0}};
        w.meta.insert("wireLabel", QStringLiteral("PWR"));
        check(near(wireLabelFraction(w), 0.5), "wire label: mid-span until it is moved");
        setWireLabelFraction(w, 0.25);
        check(near(wireLabelFraction(w), 0.25), "wire label: the position is kept on the wire");
        setWireLabelFraction(w, 3.0);
        check(near(wireLabelFraction(w), 1.0), "wire label: it cannot be pushed off the end");
        setWireLabelFraction(w, -1.0);
        check(near(wireLabelFraction(w), 0.0), "wire label: ...or off the start");

        // Dragging its grip is what moves it: the wire is selected, the grip grabbed at where
        // the label sits, and dropped further along.
        Document d;
        d.entities().clear();
        setWireLabelFraction(w, 0.5);
        w.meta.insert("showWireLabel", true);
        d.entities().push_back(w);
        Editor ed;
        ed.setDocument(&d);
        ed.snapSettings().objectSnap = false;
        ed.snapSettings().gridSnap = false;
        ed.snapSettings().ortho = false;
        ed.selectById(QStringLiteral("w1"));
        ed.onPointerPress({100, 0}, Qt::LeftButton, Qt::NoModifier); // grab the label's grip
        ed.onPointerPress({160, 0}, Qt::LeftButton, Qt::NoModifier); // place it further along
        for (const auto& e : d.entities())
            if (e.id == QLatin1String("w1"))
                check(wireLabelFraction(e) > 0.6,
                      "wire label: dragging its grip slides it along the run");
    }

    // ---- Coax: a signal and its braid, as two rows of one bundle ----
    {
        // A coax is a cable of two conductors: one row is the SIGNAL, the other the SHIELD, both
        // in the same bundle. The signal runs pin to pin; the braid is drawn as a screen symbol on
        // the signal near each end with the shield row's own conductor brought into it.
        auto coaxCase = [](bool withShield) {
            HarnessDef d;
            HarnessConnector p1;
            p1.id = "p1"; p1.side = HarnessSide::Left; p1.designator = "P1"; p1.pinCount = 6;
            HarnessConnector p2;
            p2.id = "p2"; p2.side = HarnessSide::Right; p2.designator = "P2"; p2.pinCount = 6;
            d.connectors = {p1, p2};
            HarnessWire sig;
            sig.id = "sig";
            sig.fromConn = "p1"; sig.fromPin = 1; sig.toConn = "p2"; sig.toPin = 1;
            sig.kind = QStringLiteral("coax");
            sig.bundle = QStringLiteral("COAX1");
            sig.color = QStringLiteral("SIGNAL");
            d.wires.push_back(sig);
            if (withShield) {
                HarnessWire sh;
                sh.id = "shd";
                sh.fromConn = "p1"; sh.fromPin = 2; sh.toConn = "p2"; sh.toPin = 2;
                sh.kind = QStringLiteral("coax");
                sh.bundle = QStringLiteral("COAX1");
                sh.color = QStringLiteral("SHIELD");
                d.wires.push_back(sh);
            }
            return d;
        };

        const HarnessDef whole = coaxCase(true);
        check(validateHarness(whole).empty(), "coax: a signal and a shield row validate");
        const HarnessBuild out = buildHarness(whole);

        // The signal is one run, pin to pin.
        const Entity* signal = nullptr;
        int braids = 0, shieldStubs = 0;
        for (const auto& e : out.entities) {
            if (e.type == EntityType::Screen && !e.meta.value("coaxScreenOf").toString().isEmpty())
                ++braids;
            if (e.type != EntityType::Wire) continue;
            if (e.meta.value("wireKind").toString() == QLatin1String("coax")) signal = &e;
            if (e.meta.value("wireKind").toString() == QLatin1String("coaxShield")) ++shieldStubs;
        }
        check(signal != nullptr, "coax: the signal is drawn as a wire");
        check(braids == 2, "coax: a braid symbol sits on the signal near each end");
        check(shieldStubs == 2, "coax: the shield reaches each braid from its own pin");

        // The braid really is ON the signal, and the stub really starts at the shield's pin.
        for (const auto& e : out.entities) {
            if (e.type != EntityType::Screen || e.meta.value("coaxScreenOf").toString().isEmpty())
                continue;
            const Vec2 at = e.points.front();
            bool onSignal = false;
            for (size_t i = 1; signal && i < signal->points.size(); ++i) {
                const Vec2 a = signal->points[i - 1], b = signal->points[i];
                if (at.x < std::min(a.x, b.x) - 1e-6 || at.x > std::max(a.x, b.x) + 1e-6) continue;
                const double t = (std::abs(b.x - a.x) < 1e-9) ? 0.0 : (at.x - a.x) / (b.x - a.x);
                if (std::abs(a.y + (b.y - a.y) * t - at.y) < 1e-6) onSignal = true;
            }
            check(onSignal, "coax: the braid is placed where the signal runs");
            check(e.meta.value("catch").toInt() == 1, "coax: it catches the signal");
            check(!e.meta.value("notConnected").toBool(),
                  "coax: the braid is terminated by the shield, so it carries no X");
        }
        // A shield stub runs from a pin to a braid, at right angles.
        for (const auto& e : out.entities) {
            if (e.type != EntityType::Wire ||
                e.meta.value("wireKind").toString() != QLatin1String("coaxShield"))
                continue;
            check(e.points.size() >= 2, "coax: the shield stub has a run");
            for (size_t i = 1; i < e.points.size(); ++i)
                check(near(e.points[i].x, e.points[i - 1].x) ||
                          near(e.points[i].y, e.points[i - 1].y),
                      "coax: every segment of the shield stub is level or upright");
            check(e.meta.value("wireColor").toString().startsWith(QLatin1String("SHIELD")),
                  "coax: the stub is marked as the shield");
        }
        // The shield row is NOT also routed pin to pin - that would draw the braid twice.
        int plainRuns = 0;
        for (const auto& e : out.entities)
            if (e.type == EntityType::Wire &&
                e.meta.value("wireKind").toString().isEmpty())
                ++plainRuns;
        check(plainRuns == 0, "coax: neither row is drawn as an ordinary wire");

        // Half a coax does not pass validation, and nothing is drawn for it.
        const HarnessDef half = coaxCase(false);
        bool reported = false;
        for (const auto& iss : validateHarness(half))
            if (iss.key.startsWith(QLatin1String("coaxhalf"))) reported = true;
        check(reported, "coax: a bundle with only a signal is reported");
        int halfBraids = 0;
        for (const auto& e : buildHarness(half).entities)
            if (e.type == EntityType::Screen && !e.meta.value("coaxScreenOf").toString().isEmpty())
                ++halfBraids;
        check(halfBraids == 0, "coax: ...and no braid is invented for it");
        // A shield-only bundle is reported the same way.
        HarnessDef shieldOnly = coaxCase(true);
        shieldOnly.wires.erase(shieldOnly.wires.begin()); // drop the signal
        bool reported2 = false;
        for (const auto& iss : validateHarness(shieldOnly))
            if (iss.key.startsWith(QLatin1String("coaxhalf"))) reported2 = true;
        check(reported2, "coax: a bundle with only a shield is reported too");
        // A coax row in no bundle cannot be paired at all.
        HarnessDef loose = coaxCase(true);
        loose.wires[0].bundle.clear();
        loose.wires[1].bundle.clear();
        bool reported3 = false;
        for (const auto& iss : validateHarness(loose))
            if (iss.key.startsWith(QLatin1String("coaxnobundle"))) reported3 = true;
        check(reported3, "coax: a coax row in no bundle is reported");

        // A plain wire row is untouched by any of this.
        HarnessDef plain = coaxCase(true);
        for (auto& w : plain.wires) {
            w.kind = QStringLiteral("wire");
            w.color = QStringLiteral("WH");
            w.bundle.clear();
        }
        int braids2 = 0, wires2 = 0;
        for (const auto& e : buildHarness(plain).entities) {
            if (e.type == EntityType::Screen) ++braids2;
            if (e.type == EntityType::Wire) ++wires2;
        }
        check(braids2 == 0 && wires2 == 2, "coax: plain rows are two plain wires");
        check(HarnessWire().kind == QLatin1String("wire") && HarnessWire().bundle.isEmpty(),
              "coax: a new row is a plain, unbundled wire");
    }

    // ---- A coax cannot join one connector's body to another's ----
    {
        // The reported drawing: P1 pins 8/9 to P3 pins 1/2 as a coax. Its screen used to be bonded
        // to the connector SHELL at each end, and being one conductor that joined P1's body to
        // P3's body. The braid now terminates on the shield row's own PINS, so no conductor
        // reaches a connector body at all and the fault cannot be drawn.
        HarnessDef d;
        HarnessConnector p1;
        p1.id = "p1"; p1.side = HarnessSide::Left; p1.designator = "P1"; p1.pinCount = 15;
        HarnessConnector p3;
        p3.id = "p3"; p3.side = HarnessSide::Right; p3.designator = "P3"; p3.pinCount = 8;
        d.connectors = {p1, p3};
        const char* colors[] = {"SIGNAL", "SHIELD"};
        for (int i = 0; i < 2; ++i) {
            HarnessWire w;
            w.id = QStringLiteral("cx%1").arg(i + 1);
            w.fromConn = "p1"; w.fromPin = 8 + i; w.toConn = "p3"; w.toPin = 1 + i;
            w.kind = QStringLiteral("coax");
            w.bundle = QStringLiteral("COAX1");
            w.color = QString::fromLatin1(colors[i]);
            d.wires.push_back(w);
        }
        check(harnessCoaxIndex(d, d.wires[0]) == 1 && harnessCoaxIndex(d, d.wires[1]) == 1,
              "coax bodies: both rows of COAX1 are coax 1");
        check(validateHarness(d).empty(), "coax bodies: the pair validates");

        // Nothing a coax draws may touch a connector's body rectangle.
        const HarnessBuild out = buildHarness(d);
        int touching = 0;
        for (const auto& c : out.entities) {
            if (c.type != EntityType::Connector) continue;
            const ConnectorGeometry g = connectorGeometry(c.a, connectorSpecFromMeta(c.meta));
            const double lo = std::min(g.rectMin.x, g.rectMax.x);
            const double hi = std::max(g.rectMin.x, g.rectMax.x);
            const double top = std::max(g.rectMin.y, g.rectMax.y);
            const double bot = std::min(g.rectMin.y, g.rectMax.y);
            for (const auto& e : out.entities) {
                if (e.type != EntityType::Wire) continue;
                const QString kind = e.meta.value("wireKind").toString();
                if (kind != QLatin1String("coax") && kind != QLatin1String("coaxShield")) continue;
                for (const Vec2& pt : e.points)
                    if (pt.x > lo + 1e-6 && pt.x < hi - 1e-6 && pt.y > bot + 1e-6 &&
                        pt.y < top - 1e-6) {
                        ++touching;
                        break;
                    }
            }
        }
        std::printf("    (coax bodies: coax conductors reaching inside a body: %d)\n",
                    touching);
        check(touching == 0, "coax bodies: no coax conductor reaches inside a connector body");
    }

    // ---- A wire whose end IS a shield: the drain, joined to the leg with a splice ----
    {
        auto base = []() {
            HarnessDef d;
            HarnessConnector p1;
            p1.id = "p1"; p1.side = HarnessSide::Left; p1.designator = "P1"; p1.pinCount = 6;
            p1.shieldLeft = true; // an overall screen at the left end
            HarnessConnector p2;
            p2.id = "p2"; p2.side = HarnessSide::Right; p2.designator = "P2"; p2.pinCount = 6;
            d.connectors = {p1, p2};
            for (int i = 1; i <= 2; ++i) {
                HarnessWire w;
                w.id = QStringLiteral("w%1").arg(i);
                w.fromConn = "p1"; w.fromPin = i; w.toConn = "p2"; w.toPin = i;
                w.shieldGroup = QStringLiteral("S1"); // ...and an internal one around them
                d.wires.push_back(w);
            }
            d.layout.shieldAmount = 1;
            d.layout.wireShieldAmount = 1;
            return d;
        };

        // The keys, and what the harness offers.
        HarnessDef d = base();
        const QString overallKey = harnessOverallShieldKey(QStringLiteral("p1"), HarnessSide::Left);
        const QString internalKey = harnessInternalShieldKey(QStringLiteral("S1"));
        check(harnessIsShieldEndpoint(overallKey) && harnessIsShieldEndpoint(internalKey),
              "shield end: a shield key is told apart from a connector id");
        check(!harnessIsShieldEndpoint(QStringLiteral("p1")),
              "shield end: a connector id is not mistaken for one");
        const QStringList offered = harnessShieldEndpoints(d);
        check(offered.contains(overallKey) && offered.contains(internalKey),
              "shield end: the screens the harness has are offered");
        check(offered.size() == 2, "shield end: and only those - no screens it has not got");
        check(harnessShieldEndpointLabel(d, overallKey) == QLatin1String("P1 shield (left)"),
              "shield end: an overall screen reads as its connector and end");
        check(harnessShieldEndpointLabel(d, internalKey) ==
                  QLatin1String("Shield S1 (internal)"),
              "shield end: an internal screen reads as its group");
        {
            QString conn;
            HarnessSide side = HarnessSide::Right;
            check(harnessShieldEndpointConn(overallKey, conn, side) && conn == QLatin1String("p1") &&
                      side == HarnessSide::Left,
                  "shield end: the key round-trips to its connector and side");
            check(harnessShieldEndpointGroup(internalKey) == QLatin1String("S1"),
                  "shield end: ...and an internal key to its group");
        }

        // A drain wire from a spare pin to the overall screen.
        HarnessWire drain;
        drain.id = "dr1";
        drain.fromConn = "p1";
        drain.fromPin = 6;
        drain.toConn = overallKey;
        drain.color = QStringLiteral("BK");
        drain.gauge = QStringLiteral("0.5");
        d.wires.push_back(drain);
        check(validateHarness(d).empty(), "shield end: a drain to a screen validates");

        const HarnessBuild out = buildHarness(d);
        const Entity* drawn = nullptr;
        for (const auto& e : out.entities)
            if (e.type == EntityType::Wire &&
                e.meta.value("shieldLeg").toString() == overallKey)
                drawn = &e;
        check(drawn != nullptr, "shield end: the drain is drawn as a wire");
        check(drawn && drawn->meta.value("drainWire").toBool(),
              "shield end: it is marked as a drain");
        // It runs from the pin to the screen's leg, and the leg has a splice on it.
        Vec2 leg{0, 0};
        bool haveLeg = false;
        for (const auto& e : out.entities)
            if (e.type == EntityType::Screen &&
                e.meta.value("overallShield").toString() == QLatin1String("P1")) {
                leg = e.points.back();
                haveLeg = true;
                check(e.meta.contains("notConnected") && !e.meta.value("notConnected").toBool(),
                      "shield end: the leg is terminated, so no X is drawn on it");
            }
        check(haveLeg, "shield end: the screen was generated");
        check(drawn && haveLeg &&
                  (near(drawn->points.back().x, leg.x) && near(drawn->points.back().y, leg.y)),
              "shield end: the drain lands on the screen's leg");
        int splicesOnLeg = 0;
        for (const auto& e : out.entities)
            if (e.type == EntityType::Splice && near(e.a.x, leg.x) && near(e.a.y, leg.y))
                ++splicesOnLeg;
        check(splicesOnLeg == 1, "shield end: the joint on the leg is a splice");
        // Right-angled, like every other wire.
        check(drawn && drawn->points.size() >= 2, "shield end: the drain has a run");
        if (drawn)
            for (size_t i = 1; i < drawn->points.size(); ++i)
                check(near(drawn->points[i].x, drawn->points[i - 1].x) ||
                          near(drawn->points[i].y, drawn->points[i - 1].y),
                      "shield end: every segment of the drain is level or upright");

        // An internal screen can be drained the same way.
        HarnessDef inner = base();
        HarnessWire d2;
        d2.id = "dr2";
        d2.fromConn = internalKey; // the shield end can be either way round
        d2.toConn = "p2";
        d2.toPin = 6;
        inner.wires.push_back(d2);
        check(validateHarness(inner).empty(), "shield end: a drain from an internal screen validates");
        bool drewInner = false;
        for (const auto& e : buildHarness(inner).entities)
            if (e.type == EntityType::Wire && e.meta.value("shieldLeg").toString() == internalKey)
                drewInner = true;
        check(drewInner, "shield end: an internal screen's drain is drawn too");

        // A drain pointed at a screen the harness does not have is a fault, not a crash.
        HarnessDef bad = base();
        HarnessWire d3;
        d3.id = "dr3";
        d3.fromConn = "p1";
        d3.fromPin = 5;
        d3.toConn = harnessInternalShieldKey(QStringLiteral("NOPE"));
        bad.wires.push_back(d3);
        bool reported = false;
        for (const auto& iss : validateHarness(bad))
            if (iss.key.startsWith(QLatin1String("badshield"))) reported = true;
        check(reported, "shield end: a drain to a screen that is not there is reported");
        buildHarness(bad); // must not crash, and simply draws nothing for that wire

        // A drain has no pin at its shield end, so the pin is not checked there - and a pin
        // that IS out of range still is.
        HarnessDef badPin = base();
        HarnessWire d4;
        d4.id = "dr4";
        d4.fromConn = "p1";
        d4.fromPin = 99;
        d4.toConn = overallKey;
        badPin.wires.push_back(d4);
        bool pinReported = false;
        for (const auto& iss : validateHarness(badPin))
            if (iss.key.startsWith(QLatin1String("badpin"))) pinReported = true;
        check(pinReported, "shield end: the pin end of a drain is still checked");
    }

    // ---- A junction where a bundle comes together: spliced, and one wire carries on ----
    {
        // The reported case: two wires routed up to the same point, and a single wire drawn on
        // from there. The meeting is a splice, and the wire past it stays a wire of its own -
        // it must not be swallowed into one of the two that arrived.
        Document d;
        d.entities().clear();
        auto wire = [](const char* id, std::vector<Vec2> pts, const char* color) {
            Entity w;
            w.id = QString::fromLatin1(id);
            w.type = EntityType::Wire;
            w.points = std::move(pts);
            w.meta.insert("wireColor", QString::fromLatin1(color));
            return w;
        };
        const Vec2 join{600, 0};
        d.entities().push_back(wire("rd", {{0, -40}, {400, -40}, {600, 0}}, "RD"));
        d.entities().push_back(wire("bu", {{0, -80}, {500, -80}, {600, 0}}, "BU"));
        Editor ed;
        ed.setDocument(&d);
        ed.snapSettings().objectSnap = false;
        ed.snapSettings().gridSnap = false;
        ed.snapSettings().ortho = false;
        ed.snapSettings().polar = false;

        check(ed.wireEndsAt(join) == 2, "junction: both arriving wires are counted");
        check(ed.wireEndsAt(join, QStringLiteral("rd")) == 1,
              "junction: a wire does not count its own end");

        // Draw the continuation from the junction.
        ed.setTool(ToolId::Wire);
        ed.onPointerPress(join, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress({900, 0}, Qt::LeftButton, Qt::NoModifier);
        ed.onKey(Qt::Key_Return, Qt::NoModifier);

        int wires = 0, splices = 0;
        const Entity* carriesOn = nullptr;
        for (const auto& e : d.entities()) {
            if (e.type == EntityType::Wire) {
                ++wires;
                if (e.points.back().x > 800.0) carriesOn = &e;
            }
            if (e.type == EntityType::Splice) ++splices;
        }
        check(wires == 3, "junction: the wire past it is its own conductor, not a continuation");
        check(splices == 1, "junction: a splice is generated where they meet");
        for (const auto& e : d.entities())
            if (e.type == EntityType::Splice)
                check(near(e.a.x, 600.0) && near(e.a.y, 0.0),
                      "junction: the splice sits on the meeting point");
        check(carriesOn && carriesOn->points.size() == 2 &&
                  near(carriesOn->points.front().x, 600.0),
              "junction: the single wire runs from the splice onward");
        // The two that arrived are untouched - neither grew through the junction.
        for (const auto& e : d.entities())
            if (e.id == QLatin1String("rd") || e.id == QLatin1String("bu"))
                check(e.points.size() == 3 && near(e.points.back().x, 600.0),
                      "junction: an arriving wire still ends at the junction");

        // Drawing a THIRD arrival at the same point splices once, not again.
        ed.setTool(ToolId::Wire);
        ed.onPointerPress({0, -120}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerPress(join, Qt::LeftButton, Qt::NoModifier);
        ed.onKey(Qt::Key_Return, Qt::NoModifier);
        int splices2 = 0;
        for (const auto& e : d.entities())
            if (e.type == EntityType::Splice) ++splices2;
        check(splices2 == 1, "junction: the joint is marked once however many wires arrive");

        // Two wires meeting with nothing else there is still a plain continuation: one run.
        Document two;
        two.entities().clear();
        two.entities().push_back(wire("a", {{0, 0}, {100, 0}}, "RD"));
        two.entities().push_back(wire("b", {{100, 0}, {100, -60}}, "RD"));
        Editor te;
        te.setDocument(&two);
        check(te.mergeWireEnds(QStringLiteral("a")),
              "junction: two ends alone still join into one run");
        int twoWires = 0;
        for (const auto& e : two.entities())
            if (e.type == EntityType::Wire) ++twoWires;
        check(twoWires == 1, "junction: ...and there is one wire left");
    }

    // ---- A shield drain joined to a wire: no X, and the wire is pulled along ----
    {
        Document d;
        d.entities().clear();
        // Two horizontal wires for the shield to catch, and a drain wire hanging below it.
        for (int i = 0; i < 2; ++i) {
            Entity w;
            w.id = QStringLiteral("c%1").arg(i + 1);
            w.type = EntityType::Wire;
            w.points = {{0, -20.0 * i}, {400, -20.0 * i}};
            d.entities().push_back(w);
        }
        Entity sc;
        sc.id = "s1";
        sc.type = EntityType::Screen;
        sc.points = {{100, 0}, {100, -20}, {100, -70}}; // two caught wires + the drain end
        sc.meta.insert("catch", 2);
        QJsonArray refs;
        refs.append(QStringLiteral("c1"));
        refs.append(QStringLiteral("c2"));
        sc.meta.insert("wires", refs);
        sc.meta.insert("rx", 12.0);
        sc.meta.insert("ry", 22.0);
        sc.meta.insert("rot", 1.5707963267948966);
        d.entities().push_back(sc);
        Entity drain;
        drain.id = "dw";
        drain.type = EntityType::Wire;
        drain.points = {{100, -70}, {100, -140}}; // its front sits on the drain end
        d.entities().push_back(drain);

        Editor ed;
        ed.setDocument(&d);
        Entity* screen = nullptr;
        for (auto& e : d.entities())
            if (e.id == QLatin1String("s1")) screen = &e;
        check(screen != nullptr, "shield leg: the screen was found");
        check(ed.attachShieldLeg(*screen), "shield leg: dropping it on a wire end connects it");
        check(screen->meta.value("legWireId").toString() == QLatin1String("dw"),
              "shield leg: the wire it was joined to is remembered");
        check(screen->meta.contains("legWireEnd"),
              "shield leg: which end of that wire is remembered, so it can be pulled");
        check(!screen->meta.value("notConnected").toBool(),
              "shield leg: a connected leg is no longer marked not-connected (no X)");

        // Slide the shield along the harness with Align: the drain moves, and the wire joined
        // to it is pulled with it - the wire changes length rather than coming adrift.
        ed.selectById(QStringLiteral("s1"));
        ed.beginTwistAlign();
        ed.onPointerPress({260, 0}, Qt::LeftButton, Qt::NoModifier);
        const Entity* movedScreen = nullptr;
        const Entity* movedWire = nullptr;
        for (const auto& e : d.entities()) {
            if (e.id == QLatin1String("s1")) movedScreen = &e;
            if (e.id == QLatin1String("dw")) movedWire = &e;
        }
        check(movedScreen && near(movedScreen->points.front().x, 260.0),
              "shield leg: the shield slid to the column");
        check(movedScreen && movedWire &&
                  near(movedWire->points.front().x, movedScreen->points.back().x) &&
                  near(movedWire->points.front().y, movedScreen->points.back().y),
              "shield leg: the wire end stayed on the drain");
        check(movedWire && near(movedWire->points.back().x, 100.0) &&
                  near(movedWire->points.back().y, -140.0),
              "shield leg: its far end stayed put, so the wire stretched");

        // A leg dropped in free space is not connected to anything.
        Entity* s2 = nullptr;
        for (auto& e : d.entities())
            if (e.id == QLatin1String("s1")) s2 = &e;
        s2->points.back() = {2000, 2000};
        check(!ed.attachShieldLeg(*s2), "shield leg: nothing under it, nothing connected");
        check(!s2->meta.contains("legWireId"),
              "shield leg: the old connection is dropped when it is moved away");
    }

    // ---- A splice sits on a wires MEETING point, and nowhere else ----
    {
        // The reported case: a harness with shields, twists and a bonded screen. Every dot the
        // generator draws has to be a point where wires actually meet - not a connector pin, and
        // not the tip of something that reaches nothing.
        HarnessDef d;
        HarnessConnector p1;
        p1.id = "p1"; p1.side = HarnessSide::Left; p1.designator = "P1"; p1.pinCount = 16;
        p1.shieldLeft = true;
        // NOT bonded to the body here: a bonded leg is already terminated, so the drain brought
        // onto it carries no dot (that case has a test of its own). This one is about where the
        // dots that ARE drawn end up.
        HarnessConnector p2;
        p2.id = "p2"; p2.side = HarnessSide::Right; p2.designator = "P2"; p2.pinCount = 4;
        p2.shieldRight = true;
        d.connectors = {p1, p2};
        struct W { int from; int to; const char* twist; const char* shield; };
        const W spec[] = {{7, 1, "T1", ""}, {1, 2, "T1", ""}, {2, 3, "", "S1"}, {3, 4, "", "S1"}};
        int n = 0;
        for (const W& sp : spec) {
            HarnessWire w;
            w.id = QStringLiteral("w%1").arg(++n);
            w.fromConn = "p1";
            w.fromPin = sp.from;
            w.toConn = "p2";
            w.toPin = sp.to;
            w.twistGroup = QString::fromLatin1(sp.twist);
            w.shieldGroup = QString::fromLatin1(sp.shield);
            d.wires.push_back(w);
        }
        // A drain from a spare pin onto the left screen's leg: a real joint.
        HarnessWire drain;
        drain.id = "dr";
        drain.fromConn = "p1";
        drain.fromPin = 16;
        drain.toConn = harnessOverallShieldKey(QStringLiteral("p1"), HarnessSide::Left);
        d.wires.push_back(drain);
        d.layout.shieldAmount = 1;
        d.layout.wireShieldAmount = 1;
        d.layout.spliceAtJoins = true;

        const HarnessBuild out = buildHarness(d);
        // Every pin end, and every point where two conductors really meet.
        std::vector<Vec2> pins;
        for (const auto& e : out.entities)
            if (e.type == EntityType::Connector) {
                const ConnectorGeometry g =
                    connectorGeometry(e.a, connectorSpecFromMeta(e.meta));
                for (const Vec2& pe : g.pinEnd) pins.push_back(pe);
            }
        auto same = [](const Vec2& a, const Vec2& b) {
            return std::abs(a.x - b.x) < 1e-6 && std::abs(a.y - b.y) < 1e-6;
        };
        int dots = 0, onPin = 0, atNothing = 0;
        for (const auto& sp : out.entities) {
            if (sp.type != EntityType::Splice) continue;
            ++dots;
            for (const Vec2& pe : pins)
                if (same(pe, sp.a)) ++onPin;
            // Something has to MEET there: two conductor ends, or an end on another's run.
            int ends = 0;
            bool onRun = false;
            for (const auto& w : out.entities) {
                if (w.type == EntityType::Screen && !w.points.empty()) {
                    const int k = std::min(w.meta.value("catch").toInt(
                                               static_cast<int>(w.points.size())),
                                           static_cast<int>(w.points.size()));
                    if (static_cast<int>(w.points.size()) > k && same(w.points.back(), sp.a))
                        ++ends;
                    continue;
                }
                if (w.type != EntityType::Wire || w.points.size() < 2) continue;
                if (same(w.points.front(), sp.a) || same(w.points.back(), sp.a)) ++ends;
                for (size_t i = 1; i < w.points.size(); ++i) {
                    const Vec2 a2 = w.points[i - 1], b2 = w.points[i];
                    const double len = distance(a2, b2);
                    if (len < 1e-9) continue;
                    const double cross = std::abs((b2.x - a2.x) * (sp.a.y - a2.y) -
                                                 (b2.y - a2.y) * (sp.a.x - a2.x));
                    if (cross / len > 1e-6) continue;
                    const double t = ((sp.a.x - a2.x) * (b2.x - a2.x) +
                                      (sp.a.y - a2.y) * (b2.y - a2.y)) / (len * len);
                    if (t > 1e-6 && t < 1.0 - 1e-6) onRun = true;
                }
            }
            if (ends < 2 && !(ends >= 1 && onRun)) ++atNothing;
        }
        std::printf("    (splices: %d drawn, %d on a pin, %d with nothing meeting)\n", dots,
                    onPin, atNothing);
        check(dots >= 1, "splice: the drain onto the screen's leg is dotted");
        check(onPin == 0, "splice: never on a connector pin");
        check(atNothing == 0, "splice: every dot is a point where conductors meet");

        // Switching the rule off leaves the drawing without any dots at all.
        HarnessDef off = d;
        off.layout.spliceAtJoins = false;
        int none = 0;
        for (const auto& e : buildHarness(off).entities)
            if (e.type == EntityType::Splice) ++none;
        check(none == 0, "splice: the rule switches every dot off");
    }

    // ---- Re-ordering the pinout of connectors already on the drawing ----
    {
        // Two connectors wired straight across but with the pins deliberately tangled, plus
        // spare pins between the wired ones so there is something to group.
        // P1 pin 1 -> P2 pin 4, P1 3 -> P2 2, P1 5 -> P2 1: crossings AND gaps.
        struct Wiring { int from; int to; };
        auto makeDoc = [](Document& d, std::vector<QString>& ids,
                          const std::vector<Wiring>& wiring) {
            d.entities().clear();
            ConnectorSpec sa = defaultConnectorSpec(6);
            sa.designator = QStringLiteral("P1");
            Entity ca;
            ca.id = "c1";
            ca.type = EntityType::Connector;
            ca.a = {0, 0};
            writeConnectorSpec(ca.meta, sa);
            ConnectorSpec sb = defaultConnectorSpec(6);
            sb.designator = QStringLiteral("P2");
            sb.mirrorX = true;
            Entity cb;
            cb.id = "c2";
            cb.type = EntityType::Connector;
            cb.a = {600, 0};
            writeConnectorSpec(cb.meta, sb);
            d.entities().push_back(ca);
            d.entities().push_back(cb);
            const ConnectorGeometry ga = connectorGeometry(ca.a, sa);
            const ConnectorGeometry gb = connectorGeometry(cb.a, sb);
            int n = 0;
            for (const Wiring& w : wiring) {
                Entity wire;
                wire.id = QStringLiteral("w%1").arg(++n);
                wire.type = EntityType::Wire;
                wire.layer = QStringLiteral("Wires");
                wire.points = {ga.pinEnd[w.from - 1],
                               {(ga.pinEnd[w.from - 1].x + gb.pinEnd[w.to - 1].x) / 2.0,
                                ga.pinEnd[w.from - 1].y},
                               {(ga.pinEnd[w.from - 1].x + gb.pinEnd[w.to - 1].x) / 2.0,
                                gb.pinEnd[w.to - 1].y},
                               gb.pinEnd[w.to - 1]};
                d.entities().push_back(wire);
            }
            ids = {QStringLiteral("c1"), QStringLiteral("c2")};
        };
        // How tangled / how spread the drawing is, read back off the entities.
        auto measure = [](Document& d, Editor& ed, int& crossings, int& gaps) {
            std::vector<QString> ids;
            HarnessDef def;
            ed.selectById(QStringLiteral("c1"));
            ed.selectById(QStringLiteral("c2"), /*additive=*/true);
            const bool ok = ed.buildPinoutModel(ids, def);
            crossings = ok ? harnessCrossings(def) : -1;
            gaps = ok ? harnessWireSpanPenalty(def) : -1;
            return ok;
        };

        const std::vector<Wiring> tangled = {{1, 4}, {3, 2}, {5, 1}};

        // Nothing to work on: one connector selected.
        {
            Document d;
            std::vector<QString> ids;
            makeDoc(d, ids, tangled);
            Editor ed;
            ed.setDocument(&d);
            ed.selectById(QStringLiteral("c1"));
            check(!ed.canReorderSelectedPinout(),
                  "reorder: one connector alone is nothing to re-order");
            ed.selectById(QStringLiteral("c2"), /*additive=*/true);
            check(ed.canReorderSelectedPinout(),
                  "reorder: two connectors with wires between them can be re-ordered");
        }

        // (a) Crossings only.
        {
            Document d;
            std::vector<QString> ids;
            makeDoc(d, ids, tangled);
            Editor ed;
            ed.setDocument(&d);
            int c0 = 0, g0 = 0;
            measure(d, ed, c0, g0);
            Editor::PinoutGoal goal; // neither grouped nor from the top
            const int moved = ed.reorderSelectedPinout(goal);
            int c1 = 0, g1 = 0;
            measure(d, ed, c1, g1);
            std::printf("    (reorder: crossings %d -> %d, gaps %d -> %d, %d connector(s))\n",
                        c0, c1, g0, g1, moved);
            check(c0 > 0, "reorder: the case really starts tangled");
            check(c1 == 0, "reorder: minimize crossings untangles it");
            // The WIRING is untouched: every wire still joins the same pin NAMES.
            int wires = 0;
            for (const auto& e : d.entities())
                if (e.type == EntityType::Wire) ++wires;
            check(wires == 3, "reorder: no wire is added or lost");
        }

        // (b) Grouped: the wires end up on neighbouring pins.
        {
            Document d;
            std::vector<QString> ids;
            makeDoc(d, ids, tangled);
            Editor ed;
            ed.setDocument(&d);
            int c0 = 0, g0 = 0;
            measure(d, ed, c0, g0);
            Editor::PinoutGoal goal;
            goal.group = true;
            ed.reorderSelectedPinout(goal);
            int c1 = 0, g1 = 0;
            measure(d, ed, c1, g1);
            std::printf("    (reorder grouped: gaps %d -> %d, crossings %d -> %d)\n", g0, g1, c0,
                        c1);
            check(g0 > 0, "reorder: the case really starts with the wires spread out");
            check(g1 == 0, "reorder: minimize wire pitch puts them on neighbouring pins");
        }

        // (c) Grouped AND starting at the top.
        {
            Document d;
            std::vector<QString> ids;
            makeDoc(d, ids, {{2, 4}, {4, 2}, {6, 1}});
            Editor ed;
            ed.setDocument(&d);
            ed.selectById(QStringLiteral("c1"));
            ed.selectById(QStringLiteral("c2"), /*additive=*/true);
            Editor::PinoutGoal goal;
            goal.group = true;
            goal.fromTop = true;
            ed.reorderSelectedPinout(goal);
            std::vector<QString> ids2;
            HarnessDef def;
            ed.selectById(QStringLiteral("c1"));
            ed.selectById(QStringLiteral("c2"), /*additive=*/true);
            check(ed.buildPinoutModel(ids2, def), "reorder: the model still reads back");
            check(harnessWireSpanPenalty(def) == 0,
                  "reorder: from the top still groups the wires");
            check(harnessPinOrderPenalty(def) == 0,
                  "reorder: ...and no spare pin is left above them");
        }

        // (d) Crossings, starting at the top.
        {
            Document d;
            std::vector<QString> ids;
            makeDoc(d, ids, {{2, 4}, {4, 2}, {6, 1}});
            Editor ed;
            ed.setDocument(&d);
            ed.selectById(QStringLiteral("c1"));
            ed.selectById(QStringLiteral("c2"), /*additive=*/true);
            Editor::PinoutGoal goal;
            goal.fromTop = true;
            ed.reorderSelectedPinout(goal);
            std::vector<QString> ids2;
            HarnessDef def;
            ed.selectById(QStringLiteral("c1"));
            ed.selectById(QStringLiteral("c2"), /*additive=*/true);
            check(ed.buildPinoutModel(ids2, def), "reorder: the model reads back");
            check(harnessCrossings(def) == 0, "reorder: crossings from the top untangles it");
            check(harnessPinOrderPenalty(def) == 0,
                  "reorder: ...with the wired pins at the top");
        }

        // The grouping metric itself: gaps inside the band the wires occupy.
        {
            HarnessDef d;
            HarnessConnector a1;
            a1.id = "p1"; a1.side = HarnessSide::Left; a1.pinCount = 6;
            HarnessConnector b1;
            b1.id = "p2"; b1.side = HarnessSide::Right; b1.pinCount = 6;
            d.connectors = {a1, b1};
            auto wire = [&d](const char* id, int from, int to) {
                HarnessWire w;
                w.id = QString::fromLatin1(id);
                w.fromConn = "p1"; w.fromPin = from; w.toConn = "p2"; w.toPin = to;
                d.wires.push_back(w);
            };
            wire("a", 1, 1);
            wire("b", 2, 2);
            check(harnessWireSpanPenalty(d) == 0, "span: neighbouring pins have no gap");
            d.wires.clear();
            wire("a", 1, 1);
            wire("b", 4, 2);
            check(harnessWireSpanPenalty(d) == 2 + 0,
                  "span: two spare pins between them on the left count");
        }
    }

    // ---- A shield bonded to its body is not also spliced to a wire ----
    {
        // The bond IS the joint: a drain brought to the same leg is joining a conductor that
        // already ends there, so a dot on top of it claims two joints where there is one.
        auto shieldCase = [](bool bondToBody) {
            HarnessDef d;
            HarnessConnector p1;
            p1.id = "p1"; p1.side = HarnessSide::Left; p1.designator = "P1"; p1.pinCount = 6;
            p1.shieldLeft = true;
            p1.bodyBondLeft = bondToBody;
            HarnessConnector p2;
            p2.id = "p2"; p2.side = HarnessSide::Right; p2.designator = "P2"; p2.pinCount = 6;
            d.connectors = {p1, p2};
            for (int i = 1; i <= 2; ++i) {
                HarnessWire w;
                w.id = QStringLiteral("w%1").arg(i);
                w.fromConn = "p1"; w.fromPin = i; w.toConn = "p2"; w.toPin = i;
                d.wires.push_back(w);
            }
            HarnessWire dr;
            dr.id = "dr";
            dr.fromConn = "p1";
            dr.fromPin = 6;
            dr.toConn = harnessOverallShieldKey(QStringLiteral("p1"), HarnessSide::Left);
            d.wires.push_back(dr);
            d.layout.shieldAmount = 1;
            d.layout.spliceAtJoins = true;
            return d;
        };
        auto splices = [](const HarnessBuild& b) {
            int n = 0;
            for (const auto& e : b.entities)
                if (e.type == EntityType::Splice) ++n;
            return n;
        };
        const int bonded = splices(buildHarness(shieldCase(true)));
        const int loose = splices(buildHarness(shieldCase(false)));
        std::printf("    (shield splices: bonded to body=%d, not bonded=%d)\n", bonded,
                    loose);
        check(loose >= 1, "shield splice: with no bond, the drain onto the leg is spliced");
        check(bonded == 0,
              "shield splice: a leg already bonded to the body carries no splice as well");
    }

    // ---- The three markings read one per row, and a click on one picks the wire ----
    {
        Document d;
        d.entities().clear();
        Entity w;
        w.id = "w1";
        w.type = EntityType::Wire;
        w.layer = QStringLiteral("Wires");
        w.points = {{0, 0}, {300, 0}};
        w.meta.insert("wireColor", QStringLiteral("RD"));
        w.meta.insert("wireGauge", QStringLiteral("0.5"));
        w.meta.insert("showColorName", true);
        w.meta.insert("showGaugeName", true);
        w.meta.insert("wireLabel", QStringLiteral("PWR"));
        w.meta.insert("showWireLabel", true);
        w.meta.insert("annCount", 1);
        w.meta.insert("annSize", 4.0);
        d.entities().push_back(w);
        Editor ed;
        ed.setDocument(&d);
        ed.snapSettings().objectSnap = false;
        ed.snapSettings().gridSnap = false;
        ed.snapSettings().ortho = false;

        // Each marking sits at its OWN height above the run, so a click on any of them finds the
        // wire - that is what puts its colour, gauge and label in Properties. The heights are the
        // ones the drawing actually uses, so they are read back rather than assumed.
        for (const QString& part : wireAnnotParts()) {
            const double h = wireAnnotGapFor(d.entities().front(), part);
            ed.clearSelection();
            ed.onPointerPress({150, h + 1.0}, Qt::LeftButton, Qt::NoModifier);
            check(ed.selection().size() == 1 && ed.selection().front() == QLatin1String("w1"),
                  "markings: a click on a marking picks the wire");
        }
        // Well clear of the highest of them, nothing is picked.
        {
            double top = 0.0;
            for (const QString& part : wireAnnotParts())
                top = std::max(top, wireAnnotGapFor(d.entities().front(), part));
            ed.clearSelection();
            ed.onPointerPress({150, top + 30.0}, Qt::LeftButton, Qt::NoModifier);
            check(ed.selection().empty(), "markings: a click clear of them picks nothing");
        }

        // Equal gaps mean equal heights: nothing is stacked relative to anything else.
        {
            Entity& we = d.entities().front();
            for (const QString& part : wireAnnotParts()) setWireAnnotGapFor(we, part, 7.0);
            for (const QString& part : wireAnnotParts())
                check(near(wireAnnotGapFor(we, part), 7.0),
                      "markings: a height set is the height used, the same for each of them");
            // ...and the defaults are 10 apart, label highest.
            for (const QString& part : wireAnnotParts()) we.meta.remove(
                QStringLiteral("annGap") + QString(part).replace(0, 1, part.at(0).toUpper()));
            const double g = wireAnnotGapFor(we, QStringLiteral("gauge"));
            const double c = wireAnnotGapFor(we, QStringLiteral("color"));
            const double l = wireAnnotGapFor(we, QStringLiteral("label"));
            std::printf("    (marking heights: gauge %.0f, colour %.0f, label %.0f)\n",
                        g, c, l);
            check(near(g, 2.0) && near(c, 2.0) && near(l, 2.0),
                  "markings: all three sit 2 above the wire by default");
            // They are kept apart ALONG the wire instead: 10 between each, so a pin pitch of 15
            // has room for them. Label first, then the colour, then the cross-section.
            const double len = polylineLength(we.points); // this wire's own length
            auto at = [&we, len](const char* part) {
                const std::vector<WireAnnot> a = wireAnnotsFor(we, QString::fromLatin1(part));
                return a.empty() ? 0.0 : a.front().t * len;
            };
            const double xl = at("label"), xc = at("color"), xg = at("gauge");
            std::printf("    (marking places along the wire: label %.0f, colour %.0f, gauge %.0f)\n", xl, xc, xg);
            // 10 of CLEAR space between them: the distance between two centres is 10 plus half
            // of each one's text, so none of them is ever drawn over another.
            auto widthOf = [&we](const char* part) {
                const QString p = QString::fromLatin1(part);
                return 0.62 * wireAnnotSizeFor(we, p) * wireAnnotPartText(we, p).size();
            };
            check(near(xc - xl, kAnnotGroupGap + (widthOf("label") + widthOf("color")) / 2.0) &&
                      near(xg - xc, kAnnotGroupGap + (widthOf("color") + widthOf("gauge")) / 2.0),
                  "markings: 10 of clear space between them, so none is drawn over another");
            check(xl < xc && xc < xg, "markings: label first along the wire, then colour, then AWG");
        }
    }

    // ---- Selecting a bundled wire lights the rest of its bundle ----
    {
        Document d;
        d.entities().clear();
        auto wire = [&d](const char* id, double y, const char* bundle) {
            Entity w;
            w.id = QString::fromLatin1(id);
            w.type = EntityType::Wire;
            w.layer = QStringLiteral("Wires");
            w.points = {{0, y}, {300, y}};
            if (bundle && *bundle) w.meta.insert("bundle", QString::fromLatin1(bundle));
            d.entities().push_back(w);
        };
        wire("a", 0, "CABLE1");
        wire("b", -20, "CABLE1");
        wire("c", -40, "CABLE2");
        wire("d", -60, "");
        Editor ed;
        ed.setDocument(&d);
        ed.selectById(QStringLiteral("a"));
        const QSet<QString> lit = ed.bundleCompanions();
        check(lit.size() == 1 && lit.contains(QStringLiteral("b")),
              "bundle: the other wire of the cable is lit");
        check(!lit.contains(QStringLiteral("a")),
              "bundle: the selected wire itself is drawn as selected, not as a companion");
        check(!lit.contains(QStringLiteral("c")) && !lit.contains(QStringLiteral("d")),
              "bundle: another cable and a loose wire are not lit");
        // Lit is not selected: an edit still reaches only what was clicked.
        check(ed.selection().size() == 1,
              "bundle: lighting the cable does not select it");
        // A wire in no bundle lights nothing.
        ed.selectById(QStringLiteral("d"));
        check(ed.bundleCompanions().isEmpty(), "bundle: a loose wire lights nothing");
        // Clearing the selection puts the lights out.
        ed.clearSelection();
        check(ed.bundleCompanions().isEmpty(), "bundle: nothing selected, nothing lit");
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
        // A leg only reaches out to the set length when something terminates it - a drain wire
        // run to the screen, or a bond to the connector body. Standing on its own it is a short
        // stub with an X on the end, because there is nothing out there to reach.
        auto legCase = [&base](double len, bool drain) {
            HarnessDef d = base();
            d.connectors[0].shieldLeft = true;
            d.layout.shieldLegLength = len;
            if (drain) {
                HarnessWire dr;
                dr.id = "dr";
                dr.fromConn = "p1";
                dr.fromPin = 4;
                dr.toConn = harnessOverallShieldKey(QStringLiteral("p1"), HarnessSide::Left);
                d.wires.push_back(dr);
            }
            return d;
        };
        const double stub10 = legOf(buildHarness(legCase(10.0, false)));
        const double stub40 = legOf(buildHarness(legCase(40.0, false)));
        std::printf("    (shield leg: unconnected %.2f / %.2f, drained %.2f)\n", stub10,
                    stub40, legOf(buildHarness(legCase(10.0, true))));
        check(near(stub10, 5.0), "layout: an unconnected shield leg is a 5-long stub");
        check(near(stub40, 5.0),
              "layout: the length setting does not stretch a leg that reaches nothing");
        const double leg10 = legOf(buildHarness(legCase(10.0, true)));
        check(leg10 >= 10.0 - 1e-6, "layout: a terminated shield leg reaches the set length");
        check(near(legOf(buildHarness(legCase(40.0, true))) - leg10, 30.0),
              "layout: raising the setting lengthens the terminated leg by exactly that much");
        check(near(HarnessLayout().shieldLegLength, 20.0),
              "layout: a shield leg is 20 long unless told otherwise");

        // ---- Every screen keeps the same proportion, whatever band it encloses ----
        {
            // The large radius follows the wires the screen has to cover, so the small one is
            // taken from it: a screen around eight wires is the same shape as one around two.
            auto ratios = [](int wires) {
                HarnessDef d;
                HarnessConnector a;
                a.id = "p1"; a.side = HarnessSide::Left; a.designator = "P1";
                a.pinCount = wires + 1;
                a.shieldLeft = true;
                HarnessConnector b2;
                b2.id = "p2"; b2.side = HarnessSide::Right; b2.designator = "P2";
                b2.pinCount = wires + 1;
                d.connectors = {a, b2};
                for (int i = 1; i <= wires; ++i) {
                    HarnessWire w;
                    w.id = QStringLiteral("w%1").arg(i);
                    w.fromConn = "p1"; w.fromPin = i; w.toConn = "p2"; w.toPin = i;
                    w.shieldGroup = QStringLiteral("S1");
                    d.wires.push_back(w);
                }
                d.layout.shieldAmount = 1;
                d.layout.wireShieldAmount = 1;
                std::vector<double> out;
                for (const auto& e : buildHarness(d).entities)
                    if (e.type == EntityType::Screen && e.meta.contains(QLatin1String("ry")))
                        out.push_back(e.meta.value("ry").toDouble() /
                                      e.meta.value("rx").toDouble());
                return out;
            };
            int checked = 0;
            for (int wires : {2, 8}) {
                const std::vector<double> rs = ratios(wires);
                check(!rs.empty(), "shield ratio: the screens are there to measure");
                for (double r : rs) {
                    std::printf("    (shield ratio at %d wires: %.3f)\n", wires, r);
                    check(near(r, 3.0), "shield ratio: large / small is 3");
                    ++checked;
                }
            }
            check(checked >= 2, "shield ratio: both the small and the large band were measured");
        }

        // ---- A twist symbol is never drawn under a screen ----
        {
            // A braid drawn over a twist hides the crossings the twist is there to show, so the
            // sweep that places the twists skips any x a screen already occupies.
            HarnessDef d;
            HarnessConnector a;
            a.id = "p1"; a.side = HarnessSide::Left; a.designator = "P1"; a.pinCount = 4;
            a.shieldLeft = true;
            HarnessConnector b2;
            b2.id = "p2"; b2.side = HarnessSide::Right; b2.designator = "P2"; b2.pinCount = 4;
            b2.shieldRight = true;
            d.connectors = {a, b2};
            for (int i = 1; i <= 2; ++i) {
                HarnessWire w;
                w.id = QStringLiteral("w%1").arg(i);
                w.fromConn = "p1"; w.fromPin = i; w.toConn = "p2"; w.toPin = i;
                w.twistGroup = QStringLiteral("T1");
                w.shieldGroup = QStringLiteral("S1");
                d.wires.push_back(w);
            }
            d.layout.shieldAmount = 1;
            d.layout.wireShieldAmount = 2;
            d.layout.twistCount = 4;
            const HarnessBuild out = buildHarness(d);
            std::vector<std::pair<double, double>> screens; // centre x, half width
            for (const auto& e : out.entities) {
                if (e.type != EntityType::Screen || e.points.empty()) continue;
                const int k = std::min(e.meta.value("catch").toInt(
                                           static_cast<int>(e.points.size())),
                                       static_cast<int>(e.points.size()));
                double cx = 0.0;
                for (int i = 0; i < k; ++i) cx += e.points[i].x;
                if (k > 0) cx /= k;
                screens.push_back({cx, e.meta.value("rx").toDouble(12.0)});
            }
            int twists = 0, clashes = 0;
            for (const auto& e : out.entities) {
                if (e.type != EntityType::Twist) continue;
                ++twists;
                for (const auto& sc : screens)
                    if (std::abs(sc.first - e.a.x) < sc.second) ++clashes;
            }
            std::printf("    (twists: %d drawn over %d screen(s), %d overlaid)\n",
                        twists, int(screens.size()), clashes);
            check(twists >= 1 && !screens.empty(),
                  "twist/screen: the drawing really has both to compare");
            check(clashes == 0, "twist/screen: no twist symbol sits inside a screen");
        }

        // ---- The three markings are three separate markings ----
    {
        // Each is switched on by itself, repeated as many times as IT says, and each instance
        // sits where it was dragged to. They also read on their own rows, evenly spaced.
        Document d;
        d.entities().clear();
        Entity w;
        w.id = "w1";
        w.type = EntityType::Wire;
        w.layer = QStringLiteral("Wires");
        w.points = {{0, 0}, {400, 0}};
        w.meta.insert("wireColor", QStringLiteral("WH"));
        w.meta.insert("wireGauge", QStringLiteral("0.5"));
        w.meta.insert("wireLabel", QStringLiteral("PWR"));
        w.meta.insert("showColorName", true);
        w.meta.insert("showGaugeName", true);
        w.meta.insert("showWireLabel", true);
        w.meta.insert("annCountColor", 3);
        w.meta.insert("annCountGauge", 2);
        w.meta.insert("annCountLabel", 1);
        d.entities().push_back(w);
        const Entity& e = d.entities().front();
        std::printf("    (markings: colour %d, gauge %d, label %d)\n",
                    int(wireAnnotsFor(e, QStringLiteral("color")).size()),
                    int(wireAnnotsFor(e, QStringLiteral("gauge")).size()),
                    int(wireAnnotsFor(e, QStringLiteral("label")).size()));
        check(wireAnnotsFor(e, QStringLiteral("color")).size() == 3,
              "markings: the colour is drawn its own number of times");
        check(wireAnnotsFor(e, QStringLiteral("gauge")).size() == 2,
              "markings: the cross-section is drawn its own number of times");
        check(wireAnnotsFor(e, QStringLiteral("label")).size() == 1,
              "markings: the label is drawn its own number of times");
        // Switching one off leaves the others alone.
        {
            Entity off = e;
            off.meta.insert("showColorName", false);
            check(wireAnnotsFor(off, QStringLiteral("color")).empty() &&
                      wireAnnotsFor(off, QStringLiteral("gauge")).size() == 2,
                  "markings: hiding one does not touch the others");
        }
        // A drawing made before they were separated keeps the amount it had.
        {
            Entity old = e;
            old.meta.remove(QStringLiteral("annCountColor"));
            old.meta.remove(QStringLiteral("annCountGauge"));
            old.meta.remove(QStringLiteral("annCountLabel"));
            old.meta.insert("annCount", 4);
            check(wireAnnotsFor(old, QStringLiteral("color")).size() == 4 &&
                      wireAnnotsFor(old, QStringLiteral("gauge")).size() == 4,
                  "markings: the old shared amount still drives colour and cross-section");
            check(wireAnnotsFor(old, QStringLiteral("label")).size() == 1,
                  "markings: and the label was drawn once");
        }
        // Setting one amount keeps the places already dragged and spreads the new ones.
        {
            Editor ed;
            ed.setDocument(&d);
            ed.selectById(QStringLiteral("w1"));
            std::vector<double> ts = {0.2, 0.4, 0.6};
            setWireAnnotFractionsFor(d.entities().front(), QStringLiteral("color"), ts);
            ed.setSelectionWireAnnotCountFor(QStringLiteral("color"), 4);
            const std::vector<double> after =
                wireAnnotFractionsFor(d.entities().front(), QStringLiteral("color"), 4);
            check(after.size() == 4 && near(after[0], 0.2) && near(after[1], 0.4) &&
                      near(after[2], 0.6),
                  "markings: raising the amount keeps the instances already placed");
            // ...and the OTHER markings' amounts did not change with it.
            check(wireAnnotCountFor(d.entities().front(), QStringLiteral("gauge")) == 2,
                  "markings: one amount is one marking's own");
        }
    }

    // ---- The parts library ----
    {
        // Every kind names itself, keeps a stable key in the file, and says which technical
        // parameters it has - the library's form is built from that list, so a kind is complete
        // when its fields are.
        int kinds = 0, withFields = 0;
        for (PartKind k : allPartKinds()) {
            ++kinds;
            check(!partKindName(k).isEmpty(), "library: every kind has a name");
            check(partKindFromKey(partKindKey(k)) == k,
                  "library: a kind survives a trip through its file key");
            if (!partFields(k).empty()) ++withFields;
        }
        std::printf("    (library: %d kinds, %d with parameters)\n", kinds, withFields);
        check(kinds >= 11, "library: the kinds asked for are all there");
        check(withFields == kinds, "library: every kind has parameters of its own");

        // ---- The components a harness carries in line, and their standard symbols ----
        {
            const PartKind components[] = {PartKind::Resistor, PartKind::Capacitor,
                                           PartKind::Inductor, PartKind::FerriteBead};
            for (PartKind k : components) {
                const QJsonArray sym = builtinSchematicSymbol(k);
                const std::vector<Entity> ents = partDrawingEntities(sym);
                std::printf("    (symbol %-16s %2lld entity/ies)\n",
                            qPrintable(partKindName(k)),
                            static_cast<long long>(ents.size()));
                check(!ents.empty(), "symbols: every component kind has a symbol drawn for it");
                // It straddles the origin and is the size of a schematic symbol, not of a sheet:
                // dropped on a drawing beside a connector it needs no scaling.
                Bounds b;
                for (const Entity& e : ents) {
                    const Bounds eb = e.bounds();
                    if (eb.valid) { b.expand(eb.min); b.expand(eb.max); }
                }
                check(b.valid && b.min.x < 0.0 && b.max.x > 0.0,
                      "symbols: drawn about the origin, so it lands where it is clicked");
                check(b.valid && (b.max.x - b.min.x) > 20.0 && (b.max.x - b.min.x) < 80.0,
                      "symbols: about as long as a connector's pins");
                // A part of that kind shows the standard symbol without one being captured...
                LibraryPart part;
                part.kind = k;
                check(!part.hasSchematic(), "symbols: a new part has nothing captured");
                check(!partSchematicDrawing(part).isEmpty(),
                      "symbols: and still has a symbol to show and to insert");
                // ...and a captured one takes precedence over it.
                Entity own;
                own.type = EntityType::Circle;
                own.center = Vec2{0.0, 0.0};
                own.radius = 5.0;
                part.schematic = partDrawingFromEntities({own});
                check(partDrawingEntities(partSchematicDrawing(part)).size() == 1,
                      "symbols: a symbol drawn for the part itself wins");
            }
            // A resistor is a rectangle with a lead each side; a capacitor is two plates with a gap
            // between them, which is the difference that has to survive.
            const std::vector<Entity> res =
                partDrawingEntities(builtinSchematicSymbol(PartKind::Resistor));
            int rects = 0;
            for (const Entity& e : res)
                if (e.type == EntityType::Rect) ++rects;
            check(rects == 1, "symbols: the resistor is a rectangle on the line");
            const std::vector<Entity> coil =
                partDrawingEntities(builtinSchematicSymbol(PartKind::Inductor));
            int arcs = 0;
            for (const Entity& e : coil)
                if (e.type == EntityType::Arc) ++arcs;
            check(arcs == 4, "symbols: the coil is a row of humps");
            // A bead is that coil with a core over it, so it has the humps AND more besides.
            const std::vector<Entity> bead =
                partDrawingEntities(builtinSchematicSymbol(PartKind::FerriteBead));
            check(bead.size() > coil.size(),
                  "symbols: the bead is the coil with a core drawn over it");
            // Anything that is not in the circuit has no symbol at all, rather than an empty box.
            check(builtinSchematicSymbol(PartKind::HeatshrinkBoot).isEmpty(),
                  "symbols: a part that is not in the circuit has none");

            // Their descriptions read the way a catalogue writes them.
            LibraryPart r;
            r.kind = PartKind::Resistor;
            r.params[QStringLiteral("value")] = QStringLiteral("100");
            r.params[QStringLiteral("tolerance")] = QStringLiteral("1");
            r.params[QStringLiteral("power")] = QStringLiteral("0.25");
            std::printf("    (described: %s)\n", qPrintable(describePart(r)));
            check(describePart(r).contains(QLatin1String("100 ohm")) &&
                      describePart(r).contains(QLatin1String("0.25 W")),
                  "symbols: a resistor is described by its value first");
            LibraryPart fb;
            fb.kind = PartKind::FerriteBead;
            fb.params[QStringLiteral("impedance")] = QStringLiteral("600");
            fb.params[QStringLiteral("atFreq")] = QStringLiteral("100");
            std::printf("    (described: %s)\n", qPrintable(describePart(fb)));
            check(describePart(fb).contains(QLatin1String("600 ohm")) &&
                      describePart(fb).contains(QLatin1String("at 100 MHz")),
                  "symbols: a bead's impedance is written with the frequency it is measured at");
        }

        // The parameters asked for, kind by kind.
        auto has = [](PartKind k, const char* key) {
            for (const PartField& f : partFields(k))
                if (f.key == QLatin1String(key)) return true;
            return false;
        };
        auto isRange = [](PartKind k, const char* key) {
            for (const PartField& f : partFields(k))
                if (f.key == QLatin1String(key)) return f.type == PartField::Type::Range;
            return false;
        };
        check(has(PartKind::Wire, "gauge") && has(PartKind::Wire, "material") &&
                  has(PartKind::Wire, "insulation"),
              "library: a wire carries its own technical parameters");
        // WHAT ITS PINS ARE is asked once, in "Its own pins" - a row per size, with the conductor it takes
        // and which pins are that size. It was asked here as well, in three fields that could only say less:
        // one tick, one unit and one gauge range for a whole housing that may carry two sizes at once.
        check(has(PartKind::Connector, "shellSize") && has(PartKind::Connector, "material"),
              "library: a connector says its shell size and what it is made of");
        check(!has(PartKind::Connector, "withPins") && !has(PartKind::Connector, "pinGauge") &&
                  !has(PartKind::Connector, "pinGaugeMode"),
              "library: and not the three fields that duplicated its own pin rows");
        check(isRange(PartKind::ContactPin, "gauge") && has(PartKind::ContactPin, "material"),
              "library: a contact pin is a gauge range and a material");
        check(has(PartKind::Ferrule, "size") && isRange(PartKind::Ferrule, "stud"),
              "library: a ferrule is a size and a stud range");
        check(has(PartKind::Crimp, "type") && isRange(PartKind::Crimp, "stud"),
              "library: a crimp names its type and the studs it fits");
        check(has(PartKind::Splice, "type") && isRange(PartKind::Splice, "insertion"),
              "library: a splice is crimp or solder, over an insertion range");
        check(has(PartKind::Heatshrink, "material") && isRange(PartKind::Heatshrink, "entry") &&
                  has(PartKind::Heatshrink, "adhesive"),
              "library: heatshrink is a material, an entry range and whether it is lined");
        check(has(PartKind::HeatshrinkBoot, "shellSize") &&
                  isRange(PartKind::HeatshrinkBoot, "connEntry") &&
                  isRange(PartKind::HeatshrinkBoot, "cableEntry"),
              "library: a boot has a shell size and both entry ranges");
        check(has(PartKind::Backshell, "shellSize") && isRange(PartKind::Backshell, "connEntry") &&
                  isRange(PartKind::Backshell, "cableEntry"),
              "library: a backshell has a shell size and both entry ranges");
        check(isRange(PartKind::YSplit, "main") && has(PartKind::YSplit, "legs") &&
                  isRange(PartKind::YSplit, "leg1") && isRange(PartKind::YSplit, "leg3"),
              "library: a Y-split has a main range, a leg count and a range per leg");
        check(has(PartKind::ShieldBraid, "size"),
              "library: braid is bought by a size written by hand");

        // A library is a file: parts go in, the file is written, and it comes back the same.
        PartsLibrary lib;
        LibraryPart& w = lib.add(PartKind::Wire);
        w.description = QStringLiteral("22 AWG PTFE white");
        w.mfrName = QStringLiteral("Acme");
        w.mfrPn = QStringLiteral("A-22-PTFE-WH");
        w.params.insert(QStringLiteral("gauge"), QStringLiteral("22"));
        LibraryPart& b = lib.add(PartKind::Backshell);
        b.description = QStringLiteral("90 degree, size 11");
        b.mfrPn = QStringLiteral("BS-11-90");
        b.params.insert(QStringLiteral("connEntryMin"), QStringLiteral("6"));
        b.params.insert(QStringLiteral("connEntryMax"), QStringLiteral("9"));
        // A part's two drawings are entities, so a symbol drawn once can be dropped anywhere.
        std::vector<Entity> sym;
        Entity line;
        line.type = EntityType::Line;
        line.a = {0, 0};
        line.b = {10, 0};
        sym.push_back(line);
        lib.parts.front().schematic = partDrawingFromEntities(sym);

        const QString path = QDir::tempPath() + QStringLiteral("/edraw-test-library.json");
        QFile::remove(path);
        check(lib.save(path), "library: it writes its file");
        PartsLibrary back;
        check(back.load(path), "library: and reads it again");
        std::printf("    (library file: %d part(s) written, %d read)\n",
                    int(lib.parts.size()), int(back.parts.size()));
        check(back.parts.size() == lib.parts.size(), "library: every part survives the trip");
        const LibraryPart* rw = back.byId(lib.parts.front().id);
        check(rw && rw->kind == PartKind::Wire && rw->mfrPn == QLatin1String("A-22-PTFE-WH") &&
                  rw->value(QStringLiteral("gauge")) == QLatin1String("22"),
              "library: a part comes back with its kind, its P/N and its parameters");
        check(rw && rw->hasSchematic() && !rw->hasMechanical(),
              "library: and with the drawings it had, and only those");
        const std::vector<Entity> backSym = partDrawingEntities(rw->schematic);
        check(backSym.size() == 1 && backSym.front().type == EntityType::Line &&
                  near(backSym.front().b.x, 10.0),
              "library: a stored drawing comes back as the entities it was");
        check(backSym.front().id.isEmpty(),
              "library: with no id of its own, so the sheet it lands on names it");

        // Duplicating gives a part of its own, and deleting takes only that one. (The part's
        // values are taken by VALUE first: adding to the library may move its parts in memory.)
        const size_t was = back.parts.size();
        const QString srcId = lib.parts.front().id;
        const QString srcPn = rw->mfrPn;
        LibraryPart* copy = back.duplicate(srcId);
        check(copy && copy->id != srcId && copy->mfrPn == srcPn,
              "library: a duplicate is the same part under a new id");
        check(back.parts.size() == was + 1, "library: and is added to the library");
        back.remove(copy->id);
        check(back.parts.size() == was && back.byId(lib.parts.front().id),
              "library: deleting one leaves the rest alone");
        check(back.ofKind(PartKind::Backshell).size() == 1,
              "library: the parts of one kind can be listed on their own");
        QFile::remove(path);
    }

    // ---- Units, and a bill of materials that adds them up properly ----
    {
        // Every part is issued in something, and only quantities of the SAME dimension can be
        // added together: 300 mm of wire and 1.2 m of wire are 1.5 m; metres and pieces are not.
        check(partUnitDimension(QStringLiteral("mm")) == QLatin1String("length") &&
                  partUnitDimension(QStringLiteral("each")) == QLatin1String("count"),
              "units: a unit knows what kind of thing it measures");
        check(partUnitsCompatible(QStringLiteral("mm"), QStringLiteral("m")) &&
                  !partUnitsCompatible(QStringLiteral("m"), QStringLiteral("each")),
              "units: lengths add to lengths, never to pieces");
        double got = 0.0;
        check(convertPartUnit(1500.0, QStringLiteral("mm"), QStringLiteral("m"), got) &&
                  near(got, 1.5),
              "units: mm to m");
        check(convertPartUnit(1.0, QStringLiteral("ft"), QStringLiteral("mm"), got) &&
                  near(got, 304.8),
              "units: a foot is 304.8 mm");
        got = -1.0;
        check(!convertPartUnit(1.0, QStringLiteral("m"), QStringLiteral("each"), got) &&
                  near(got, -1.0),
              "units: what cannot be converted is left alone");
        check(defaultUnitForKind(PartKind::Wire) == QLatin1String("m") &&
                  defaultUnitForKind(PartKind::Connector) == QLatin1String("each") &&
                  defaultUnitForKind(PartKind::Heatshrink) == QLatin1String("mm"),
              "units: a kind starts in the unit it is bought in");

        // The seven columns, and headings a drawing may rename.
        check(bomColumnCount() == 7, "bom: seven columns");
        BomTable t;
        check(t.header(0) == QLatin1String("#") && t.header(6) == QLatin1String("Units"),
              "bom: the default headings");
        t.setHeader(2, QStringLiteral("Our P/N"));
        check(t.header(2) == QLatin1String("Our P/N") && t.header(1) == QLatin1String("Description"),
              "bom: a renamed heading sticks, and only that one");

        // Totalling: the same part in two units becomes one line, in the first row's unit.
        std::vector<BomRow> rows;
        BomRow a1;
        a1.description = QStringLiteral("22 AWG white");
        a1.mfrPn = QStringLiteral("W-22-WH");
        a1.qty = 1.2;
        a1.unit = QStringLiteral("m");
        a1.ref = QStringLiteral("W1");
        BomRow a2 = a1;
        a2.qty = 300.0;
        a2.unit = QStringLiteral("mm");
        a2.ref = QStringLiteral("W2");
        BomRow b1;
        b1.description = QStringLiteral("Backshell size 11");
        b1.mfrPn = QStringLiteral("BS-11");
        b1.qty = 2.0;
        b1.unit = QStringLiteral("each");
        rows = {a1, a2, b1};
        std::vector<BomRow> one = bomUnify(rows);
        std::printf("    (bom: %d row(s) in, %d line(s) out, first %.2f %s)\n",
                    int(rows.size()), int(one.size()), one.empty() ? 0.0 : one.front().qty,
                    one.empty() ? "-" : qPrintable(one.front().unit));
        check(one.size() == 2, "bom: the same part is one line");
        check(near(one.front().qty, 1.5) && one.front().unit == QLatin1String("m"),
              "bom: totalled in the unit of the first row of that part");
        check(one.front().ref.contains(QLatin1String("W1")) &&
                  one.front().ref.contains(QLatin1String("W2")),
              "bom: and it still says where it is used");

        // IS THIS ALREADY ON THE BILL? The very test the totalling uses, asked on its own - so a window
        // about to add a line and the totalling that would merge it can never answer differently.
        {
            check(bomSameThing(a1, a2), "same thing: the same P/N in two units is one part");
            check(!bomSameThing(a1, b1), "same thing: two different parts are two parts");
            BomRow byId = a1;
            byId.partId = QStringLiteral("lib-1");
            BomRow sameIdOtherPn = byId;
            sameIdOtherPn.mfrPn = QStringLiteral("SOMETHING-ELSE");
            check(bomSameThing(byId, sameIdOtherPn),
                  "same thing: the library part it IS beats the P/N written beside it");
            BomRow otherId = byId;
            otherId.partId = QStringLiteral("lib-2");
            check(!bomSameThing(byId, otherId), "same thing: two library parts are two parts");
            // A line that says nothing about what it is is the same thing as nothing, itself included:
            // an empty line is a place to type in, not a part the next empty line is one more of.
            BomRow blankA, blankB;
            blankA.rowId = QStringLiteral("r1");
            blankB.rowId = QStringLiteral("r2");
            check(!bomSameThing(blankA, blankB) && !bomSameThing(blankA, blankA),
                  "same thing: a blank line is the same thing as nothing");

            // And the answer for a whole set at once: one entry per row about to be added, saying which
            // line of the bill already names it.
            const std::vector<BomRow> bill = {b1, a1};
            const std::vector<int> found = bomAlreadyListed(bill, {a2, blankA, b1});
            std::printf("    (already listed: %d, %d, %d)\n", found.size() > 0 ? found[0] : -9,
                        found.size() > 1 ? found[1] : -9, found.size() > 2 ? found[2] : -9);
            check(found.size() == 3, "already listed: one answer per row about to be added");
            check(found.size() == 3 && found[0] == 1,
                  "already listed: and it points at the line that names it");
            check(found.size() == 3 && found[1] == -1, "already listed: a blank line is on no bill");
            check(found.size() == 3 && found[2] == 0, "already listed: the first line is found too");
        }
        // A row that names nothing is only itself: two blank rows are two lines, not one line of
        // qty 2. (Pressing "+ Row" twice used to read as adding 1 to the first row's quantity.)
        {
            std::vector<BomRow> blanks;
            for (int i = 0; i < 3; ++i) {
                BomRow blank;
                blank.rowId = bomNextRowId(blanks);
                blank.qty = 1.0;
                blank.unit = QStringLiteral("each");
                blanks.push_back(blank);
            }
            const std::vector<BomRow> kept = bomUnify(blanks);
            std::printf("    (bom blanks: %d row(s) in, %d line(s) out, first qty %.0f)\n",
                        int(blanks.size()), int(kept.size()),
                        kept.empty() ? 0.0 : kept.front().qty);
            check(kept.size() == 3, "bom: a blank row is a row of its own");
            check(!kept.empty() && near(kept.front().qty, 1.0),
                  "bom: and adding one does not raise another's quantity");
            // Once two of them say they are the same thing, they DO total.
            blanks[0].description = QStringLiteral("Ty-wrap");
            blanks[1].description = QStringLiteral("Ty-wrap");
            check(bomUnify(blanks).size() == 2,
                  "bom: rows that name the same part still come out as one line");
        }

        // A part in units that cannot be added keeps its own line rather than being forced.
        BomRow odd = a1;
        odd.qty = 3.0;
        odd.unit = QStringLiteral("each");
        std::vector<BomRow> mixed = bomUnify({a1, odd});
        check(mixed.size() == 2, "bom: quantities that cannot be added stay separate lines");

        // ---- The configuration: what the list works in, and which columns reach the sheet ----
        {
            BomConfig cfg;
            // MILLIMETRES, because that is what the drawing is in. A bill in metres of a harness drawn in
            // millimetres asks everybody reading it to divide by a thousand in their head, and the one place
            // a rounding creeps in is the place nobody checks.
            check(cfg.lengthUnit == QLatin1String("mm") && cfg.unify && cfg.includeDrawing,
                  "bom config: lengths in the drawing's own unit, totalled, with the drawing included");
            for (int i = 0; i < bomColumnCount(); ++i)
                check(cfg.columnShown(i), "bom config: every column reaches the sheet unless told");
            cfg.setColumnShown(2, false);
            check(!cfg.columnShown(2) && cfg.columnShown(1) && cfg.columnShown(3),
                  "bom config: switching one column off leaves the rest on");

            // A length is converted into the working unit, given its spare, and rounded up.
            BomRow wire;
            wire.rowId = QStringLiteral("r1");
            wire.description = QStringLiteral("22 AWG");
            wire.qty = 1500.0;
            wire.unit = QStringLiteral("mm");
            BomRow conn;
            conn.rowId = QStringLiteral("r2");
            conn.description = QStringLiteral("Connector");
            conn.qty = 2.0;
            conn.unit = QStringLiteral("each");
            cfg.lengthUnit = QStringLiteral("m");
            std::vector<BomRow> done = bomApplyConfig({wire, conn}, cfg);
            std::printf("    (bom config: 1500 mm -> %.2f %s; 2 each -> %.2f %s)\n",
                        done[0].qty, qPrintable(done[0].unit), done[1].qty,
                        qPrintable(done[1].unit));
            check(near(done[0].qty, 1.5) && done[0].unit == QLatin1String("m"),
                  "bom config: a length is listed in the working unit");
            check(near(done[1].qty, 2.0) && done[1].unit == QLatin1String("each"),
                  "bom config: and a count is left alone");
            cfg.sparePercent = 10.0;
            done = bomApplyConfig({wire, conn}, cfg);
            check(near(done[0].qty, 1.65) && near(done[1].qty, 2.0),
                  "bom config: spare is added to lengths only");
            cfg.roundUpLengths = true;
            done = bomApplyConfig({wire, conn}, cfg);
            check(near(done[0].qty, 2.0) && near(done[1].qty, 2.0),
                  "bom config: rounding up applies to lengths only");
            // Two decimals or none, as asked.
            BomRow odd;
            odd.qty = 1.234;
            check(odd.qtyText(2) == QLatin1String("1.23") && odd.qtyText(0) == QLatin1String("1"),
                  "bom config: a quantity shows the decimals asked for");
            BomRow whole;
            whole.qty = 3.0;
            check(whole.qtyText(2) == QLatin1String("3"),
                  "bom config: a whole figure shows none");

            // And the configuration is kept with the drawing.
            BomTable t2;
            t2.config = cfg;
            BomTable back2;
            back2.fromJson(t2.toJson());
            check(back2.config.lengthUnit == cfg.lengthUnit &&
                      near(back2.config.sparePercent, 10.0) && back2.config.roundUpLengths &&
                      !back2.config.columnShown(2),
                  "bom config: it survives a trip through the drawing's file");
        }

        // What the drawing calls for, with the library filling in the numbers.
        PartsLibrary lib;
        LibraryPart& wp = lib.add(PartKind::Wire);
        wp.description = QStringLiteral("22 AWG PTFE white");
        wp.mfrPn = QStringLiteral("A-22");
        wp.internalPn = QStringLiteral("INT-1");
        wp.unit = QStringLiteral("m");
        const QString wireId = wp.id;
        Document d;
        d.entities().clear();
        Entity w;
        w.id = "w1";
        w.type = EntityType::Wire;
        w.layer = QStringLiteral("Wires");
        w.points = {{0, 0}, {2000, 0}}; // 2000 mm
        w.meta.insert("libPart", wireId);
        d.entities().push_back(w);
        Entity plain;
        plain.id = "w2";
        plain.type = EntityType::Wire;
        plain.layer = QStringLiteral("Wires");
        plain.points = {{0, 50}, {500, 50}};
        plain.meta.insert("wireColor", QStringLiteral("RD"));
        plain.meta.insert("wireGauge", QStringLiteral("0.5"));
        d.entities().push_back(plain);
        const std::vector<BomRow> drawn = bomFromDrawing(d, lib);
        std::printf("    (bom from drawing: %d row(s))\n", int(drawn.size()));
        check(drawn.size() == 2, "bom: a row per wire in the drawing");
        const BomRow* fromLib = nullptr;
        for (const BomRow& r : drawn)
            if (r.partId == wireId) fromLib = &r;
        check(fromLib && fromLib->mfrPn == QLatin1String("A-22") &&
                  fromLib->internalPn == QLatin1String("INT-1"),
              "bom: a wire made of a library part is ordered by that part's numbers");
        check(fromLib && fromLib->unit == QLatin1String("m") && near(fromLib->qty, 2.0),
              "bom: and its length is given in the unit the part is bought in");
        for (const BomRow& r : drawn)
            if (r.partId.isEmpty())
                check(r.unit == QLatin1String("mm") && near(r.qty, 500.0),
                      "bom: a wire that names no part is measured in the drawing's own unit");

        // A library part becomes a row of its own.
        const BomRow made = bomRowFromPart(wp, 3.0);
        check(made.partId == wireId && made.unit == QLatin1String("m") && near(made.qty, 3.0) &&
                  made.description == wp.description,
              "bom: a part added from the library carries its description, numbers and unit");

        // The table is a file: the typed rows and the headings survive, the drawing's do not (they
        // are read back off the drawing).
        BomTable store;
        store.setHeader(1, QStringLiteral("What it is"));
        store.rows.push_back(made);
        BomRow drawnRow = made;
        drawnRow.fromDrawing = true;
        store.rows.push_back(drawnRow);
        BomTable back;
        back.fromJson(store.toJson());
        std::printf("    (bom file: %d row(s) stored of %d)\n", int(back.rows.size()),
                    int(store.rows.size()));
        check(back.rows.size() == 1 && !back.rows.front().fromDrawing,
              "bom: only the typed rows are stored");
        check(back.header(1) == QLatin1String("What it is"),
              "bom: and the headings the drawing was given");
    }

    // ---- The library a first run starts from: real parts, 14 to 30 AWG ----
    {
        const PartsLibrary seed = seedLibrary();
        const std::vector<const LibraryPart*> allWires = seed.ofKind(PartKind::Wire);
        // THE ORIGINAL ALPHA HOOK-UP SET, which is what every figure below belongs to: the 300 V
        // 1550-series wire the seed started life with, in 14 through 30 AWG.
        //
        // The seed carries two other shelves now, and neither answers to these figures. The Raychem
        // Spec 55 shelf is 600 V crosslinked ETFE with its own part numbers and resistances. And
        // Alpha's own 2626 and 3079 are 600 V, bend on five times their diameter rather than ten,
        // and are 26 and 14 AWG only - they are Alpha Wire, so a test that picks its parts by the
        // maker's name picks them up, and every figure here is wrong for them. Both have a shelf
        // test of their own; this one is about the set it has always been about.
        std::vector<const LibraryPart*> wires;
        for (const LibraryPart* p : allWires)
            if (p->mfrName == QLatin1String("Alpha Wire") &&
                !p->mfrPn.startsWith(QLatin1String("2626 ")) &&
                !p->mfrPn.startsWith(QLatin1String("3079 ")))
                wires.push_back(p);
        std::printf("    (seed library: %d part(s), %d of them wire, %d of those Alpha)\n",
                    int(seed.parts.size()), int(allWires.size()), int(wires.size()));
        check(wires.size() >= 10, "seed: ten wires to start from");
        // And the Spec 55 shelf is on it too, which is what the count above no longer includes.
        int spec55 = 0;
        for (const LibraryPart* p : allWires)
            if (p->mfrPn.startsWith(QLatin1String("55A0111-"))) ++spec55;
        check(spec55 == 36,
              "seed: and the Spec 55 wire shelf beside it, twelve sizes in three colours");
        // And Alpha's own 2626 and 3079, in every colour each is made in - eight and fifteen.
        int alpha = 0;
        for (const LibraryPart* p : allWires)
            if (p->mfrPn.startsWith(QLatin1String("2626 ")) ||
                p->mfrPn.startsWith(QLatin1String("3079 ")))
                ++alpha;
        check(alpha == 23, "seed: and the Alpha hook-up shelf, 2626 and 3079 in all their colours");
        // Every gauge from 14 to 30, and every one of them a real Alpha Wire part number with the
        // figures its data sheet gives.
        QSet<int> gauges;
        int named = 0, withStrands = 0;
        for (const LibraryPart* p : wires) {
            gauges.insert(p->value(QStringLiteral("gauge")).toInt());
            if (p->mfrName == QLatin1String("Alpha Wire") && !p->mfrPn.isEmpty()) ++named;
            if (!p->value(QStringLiteral("strands")).isEmpty()) ++withStrands;
            check(p->unit == QLatin1String("m"), "seed: wire is bought by the metre");
            check(p->value(QStringLiteral("voltage")) == QLatin1String("300"),
                  "seed: and carries its voltage rating");
        }
        for (int awg : {14, 16, 18, 20, 22, 24, 26, 28, 30})
            check(gauges.contains(awg),
                  "seed: every gauge from 14 to 30 AWG is there");
        check(named == static_cast<int>(wires.size()),
              "seed: every wire names its manufacturer and part number");
        check(withStrands == static_cast<int>(wires.size()),
              "seed: and its stranding");
        // Resistance is kept per METRE (a data sheet gives it per 1000 ft), and every wire says how
        // tightly it may be bent.
        for (const LibraryPart* p : wires) {
            const double ohmPerM = p->value(QStringLiteral("resistance")).toDouble();
            const double bend = p->value(QStringLiteral("bendRadius")).toDouble();
            const double od = p->value(QStringLiteral("od")).toDouble();
            check(ohmPerM > 0.0 && ohmPerM < 1.0, "seed: resistance is per metre, not per 1000 ft");
            check(near(bend, od * 10.0), "seed: the bend radius is ten times the diameter");
        }
        // The 16 AWG figure is the one on the data sheet: 4.4 ohm/1000 ft is 0.0144 ohm/m.
        for (const LibraryPart* p : wires)
            if (p->value(QStringLiteral("gauge")) == QLatin1String("16"))
                check(std::abs(p->value(QStringLiteral("resistance")).toDouble() - 0.0144) < 0.0002,
                      "seed: 4.4 ohm/1000ft reads as 0.0144 ohm/m");

        // The connectors, ferrules and ring terminals that came with it.
        const int conns = static_cast<int>(seed.ofKind(PartKind::Connector).size());
        const int ferrules = static_cast<int>(seed.ofKind(PartKind::Ferrule).size());
        const int rings = static_cast<int>(seed.ofKind(PartKind::Crimp).size());
        std::printf("    (seed: %d connector(s), %d ferrule(s), %d ring terminal(s))\n",
                    conns, ferrules, rings);
        check(conns >= 20, "seed: ten Molex and ten Amphenol connectors");
        check(ferrules >= 10, "seed: ten ferrules");
        check(rings >= 10, "seed: ten ring terminals");
        int molex = 0, amphenol = 0;
        QSet<QString> shells, ferruleSizes, studs;
        for (const LibraryPart* p : seed.ofKind(PartKind::Connector)) {
            if (p->mfrName == QLatin1String("Molex")) ++molex;
            if (p->mfrName.startsWith(QLatin1String("Amphenol"))) {
                ++amphenol;
                shells.insert(p->value(QStringLiteral("shellSize")));
                check(p->value(QStringLiteral("spec")).contains(QLatin1String("38999")),
                      "seed: an Amphenol part says which standard it is built to");
            }
            check(!p->mfrPn.isEmpty(), "seed: every connector has a part number");
        }
        for (const LibraryPart* p : seed.ofKind(PartKind::Ferrule))
            ferruleSizes.insert(p->value(QStringLiteral("size")));
        for (const LibraryPart* p : seed.ofKind(PartKind::Crimp))
            studs.insert(p->value(QStringLiteral("size")));
        check(molex >= 10 && amphenol >= 10, "seed: ten of each make");
        check(shells.size() >= 5, "seed: the military connectors cover several shell sizes");
        check(ferruleSizes.size() >= 8, "seed: the ferrules are a spread of sizes");
        check(studs.size() >= 6, "seed: the ring terminals are a spread of studs");

        // ---- A description written from the technical data ----
        {
            // What the part IS, from the parameters that are filled in - and nothing that is not.
            LibraryPart w;
            w.kind = PartKind::Wire;
            w.params.insert(QStringLiteral("gauge"), QStringLiteral("22"));
            w.params.insert(QStringLiteral("insulation"), QStringLiteral("PTFE"));
            w.params.insert(QStringLiteral("material"), QStringLiteral("Silver-plated copper"));
            w.params.insert(QStringLiteral("voltage"), QStringLiteral("600"));
            const QString made = describePart(w);
            std::printf("    (described: %s)\n", qPrintable(made));
            check(made.startsWith(QLatin1String("Hook-up wire")),
                  "describe: it leads with what kind of thing it is");
            check(made.contains(QLatin1String("22 AWG")) && made.contains(QLatin1String("PTFE")) &&
                      made.contains(QLatin1String("600 V")),
                  "describe: every parameter that is filled in is named");
            check(!made.contains(QLatin1String("Colour")) && !made.contains(QLatin1String(", ,")),
                  "describe: and nothing that is blank");
            // A range reads as a range, and a yes/no is named only when it is yes.
            LibraryPart c;
            c.kind = PartKind::Crimp;
            c.params.insert(QStringLiteral("type"), QStringLiteral("Ring"));
            c.params.insert(QStringLiteral("gaugeMin"), QStringLiteral("16"));
            c.params.insert(QStringLiteral("gaugeMax"), QStringLiteral("14"));
            c.params.insert(QStringLiteral("insulated"), QStringLiteral("Yes"));
            const QString rc = describePart(c);
            std::printf("    (described: %s)\n", qPrintable(rc));
            check(rc.contains(QLatin1String("16-14 AWG")), "describe: a range reads as a range");
            check(rc.contains(QLatin1String("insulated")),
                  "describe: a yes/no is named when it is yes");
            LibraryPart plain = c;
            plain.params.insert(QStringLiteral("insulated"), QStringLiteral("No"));
            check(!describePart(plain).contains(QLatin1String("insulated")),
                  "describe: and left out when it is no");
            // A part with nothing filled in still says what it is.
            LibraryPart bare;
            bare.kind = PartKind::Backshell;
            check(describePart(bare) == QLatin1String("Backshell"),
                  "describe: a part with no data still names its kind");
        }
    }

    // ---- A marking can be picked on its own, and only slid along its wire ----
    {
        // A zone over the text picks THAT marking - not the wire - and pulling it changes only how
        // far along the run it sits. With several picked, Shift pulls them all, each along its own
        // wire, so they line up under the cursor.
        Document d;
        d.entities().clear();
        auto wire = [&d](const char* id, double y, const char* label) {
            Entity w;
            w.id = QString::fromLatin1(id);
            w.type = EntityType::Wire;
            w.layer = QStringLiteral("Wires");
            w.points = {{0, y}, {400, y}};
            w.meta.insert("wireLabel", QString::fromLatin1(label));
            w.meta.insert("showWireLabel", true);
            w.meta.insert("annSizeLabel", 4.0);
            d.entities().push_back(w);
        };
        wire("w1", 0, "AAA");
        wire("w2", -40, "BBB");
        Editor ed;
        ed.setDocument(&d);
        ed.snapSettings().objectSnap = false;
        ed.snapSettings().gridSnap = false;
        ed.snapSettings().ortho = false;

        // Where each label is, and the box it occupies.
        auto labelAt = [&d](const char* id) {
            for (const auto& e : d.entities())
                if (e.id == QLatin1String(id)) {
                    const std::vector<WireAnnot> a2 =
                        wireAnnotsFor(e, QStringLiteral("label"));
                    return a2.empty() ? Vec2{0, 0} : a2.front().pos;
                }
            return Vec2{0, 0};
        };
        const Vec2 p1 = labelAt("w1");
        // A window zone (left to right) around w1's label alone, starting well clear of the wire
        // so the first click is on empty space rather than on the wire itself.
        ed.onPointerPress({p1.x - 30, p1.y + 25}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerRelease({p1.x - 30, p1.y + 25}, Qt::LeftButton);
        ed.onPointerMove({p1.x + 30, p1.y + 1.2});
        ed.onPointerPress({p1.x + 30, p1.y + 1.2}, Qt::LeftButton, Qt::NoModifier);
        std::printf("    (mark pick: %d marking(s), %d entity/ies selected)\n",
                    int(ed.markSelection().size()), int(ed.selection().size()));
        check(ed.markSelection().size() == 1,
              "mark pick: a zone over the text picks that marking");
        check(ed.selection().empty(), "mark pick: and does not select the wire it belongs to");
        check(ed.markSelection().front().part == QLatin1String("label"),
              "mark pick: the marking picked is the one the zone was over");

        // Pull it: only the place along the wire changes. The grab is on the text's box, which
        // sits its gap above the run.
        const Vec2 before = labelAt("w1");
        const Vec2 grabAt{before.x, before.y + 4.0};
        ed.onPointerPress(grabAt, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({350, before.y + 25});
        ed.onPointerRelease({350, before.y + 25}, Qt::LeftButton);
        const Vec2 after = labelAt("w1");
        std::printf("    (mark drag: (%.0f,%.0f) -> (%.0f,%.0f))\n", before.x, before.y,
                    after.x, after.y);
        check(std::abs(after.x - 350.0) < 20.0, "mark drag: it follows the cursor along the wire");
        check(near(after.y, before.y), "mark drag: and never leaves the wire it names");

        // Both labels picked, Shift-pulled: each lands on its own wire under the cursor.
        ed.clearSelection();
        // A window zone (left to right) holding both texts but neither whole wire, so the two
        // markings are picked and the wires are left alone.
        ed.onPointerPress({150, 20}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerRelease({150, 20}, Qt::LeftButton);
        ed.onPointerMove({390, -45});
        ed.onPointerPress({390, -45}, Qt::LeftButton, Qt::NoModifier);
        check(ed.markSelection().size() == 2, "mark pick: a zone over both picks both");
        const Vec2 g = labelAt("w1");
        ed.onPointerPress({g.x, g.y + 4.0}, Qt::LeftButton, Qt::ShiftModifier);
        ed.onPointerMove({120, 0});
        ed.onPointerRelease({120, 0}, Qt::LeftButton);
        const Vec2 a1 = labelAt("w1"), a2 = labelAt("w2");
        std::printf("    (shift pull: w1 at %.0f, w2 at %.0f)\n", a1.x, a2.x);
        check(std::abs(a1.x - a2.x) < 1.0,
              "shift pull: every picked marking lines up under the cursor");
        check(near(a1.y, 0.0) && near(a2.y, -40.0),
              "shift pull: each one stays on the wire it belongs to");
    }

    // ---- A vertical run: which side the text sits, and which way it reads ----
    {
        Entity w;
        w.type = EntityType::Wire;
        w.layer = QStringLiteral("Wires");
        w.points = {{100, 0}, {100, 300}}; // straight up
        w.meta.insert("wireLabel", QStringLiteral("PWR"));
        w.meta.insert("showWireLabel", true);
        check(wireAnnotVerticalSide(w) == QLatin1String("left") &&
                  wireAnnotVerticalText(w) == QLatin1String("up"),
              "vertical text: left and reading up unless told otherwise");
        auto dirOf = [](const Entity& e) {
            const std::vector<WireAnnot> a2 = wireAnnotsFor(e, QStringLiteral("label"));
            return a2.empty() ? Vec2{0, 0} : a2.front().dir;
        };
        const Vec2 up = dirOf(w);
        check(up.y > 0.0, "vertical text: reading up runs with the wire");
        w.meta.insert("annVertText", QStringLiteral("down"));
        check(dirOf(w).y < 0.0, "vertical text: asking for down turns it round");
        // The side is which way the text is offset from the run.
        const std::vector<WireAnnot> leftSide = wireAnnotsFor(w, QStringLiteral("label"));
        w.meta.insert("annVertSide", QStringLiteral("right"));
        const std::vector<WireAnnot> rightSide = wireAnnotsFor(w, QStringLiteral("label"));
        check(!leftSide.empty() && !rightSide.empty() && leftSide.front().flip != rightSide.front().flip,
              "vertical text: the side asked for is the side it sits on");
    }

    // ---- Each marking is drawn in its own text height ----
    {
        Entity w;
        w.type = EntityType::Wire;
        w.points = {{0, 0}, {200, 0}};
        w.meta.insert("wireColor", QStringLiteral("WH"));
        w.meta.insert("wireGauge", QStringLiteral("0.5"));
        w.meta.insert("wireLabel", QStringLiteral("PWR"));
        w.meta.insert("showColorName", true);
        w.meta.insert("showGaugeName", true);
        check(near(wireAnnotSizeFor(w, QStringLiteral("label")), 3.0) &&
                  near(wireAnnotSizeFor(w, QStringLiteral("color")), 3.0),
              "marking height: 3 unless the drawing says otherwise");
        // The wire's own marking size still speaks for the markings that have none.
        w.meta.insert("annSize", 5.0);
        check(near(wireAnnotSizeFor(w, QStringLiteral("gauge")), 5.0),
              "marking height: the wire's marking size stands in");
        // ...and one marking's height is its own.
        setWireAnnotSizeFor(w, QStringLiteral("label"), 8.0);
        check(near(wireAnnotSizeFor(w, QStringLiteral("label")), 8.0) &&
                  near(wireAnnotSizeFor(w, QStringLiteral("color")), 5.0),
              "marking height: set on one marking, it belongs to that marking");
    }

    // ---- A twist braids the wires it was given, even among look-alikes ----
    {
        // The reported cuts: the generated runs were matched back to the definition's rows by
        // what they look like (label, colour, gauge), so with several identical white wires - and
        // a cable cut into sections and pigtails - a twist could end up naming a wire it has
        // nothing to do with. It then interrupted THAT wire's run, which read as a wire ending in
        // mid air. Every twist must name exactly the entities its own rows were drawn as.
        HarnessDef d;
        HarnessConnector p1;
        p1.id = "c1"; p1.side = HarnessSide::Left; p1.designator = "P1"; p1.pinCount = 9;
        HarnessConnector p2;
        p2.id = "c2"; p2.side = HarnessSide::Right; p2.designator = "P2"; p2.pinCount = 9;
        d.connectors = {p1, p2};
        // Six wires that look identical, two of them a twisted pair, plus a spliced coax.
        for (int i = 1; i <= 6; ++i) {
            HarnessWire w;
            w.id = QStringLiteral("w%1").arg(i);
            w.fromConn = "c1"; w.fromPin = i; w.toConn = "c2"; w.toPin = i;
            if (i == 5 || i == 6) w.twistGroup = QStringLiteral("T1");
            d.wires.push_back(w);
        }
        HarnessWire sig;
        sig.id = "w8"; sig.kind = "coax-splice"; sig.bundle = "COAX1"; sig.color = "SIGNAL1";
        sig.fromConn = "c1"; sig.fromPin = 8; sig.toConn = "c2"; sig.toPin = 8;
        HarnessWire shl;
        shl.id = "w9"; shl.kind = "coax-splice"; shl.bundle = "COAX1"; shl.color = "SHIELD1";
        shl.fromConn = "c1"; shl.fromPin = 9; shl.toConn = "c2"; shl.toPin = 9;
        d.wires.push_back(sig);
        d.wires.push_back(shl);

        Document doc;
        doc.entities().clear();
        Editor ed;
        ed.setDocument(&doc);
        ed.insertHarness(d);

        // What each twist points at, and which definition row that entity was drawn from.
        int twists = 0, wrong = 0, named = 0;
        for (const auto& t : doc.entities()) {
            if (t.type != EntityType::Twist) continue;
            ++twists;
            for (const auto& v : t.meta.value(QStringLiteral("wires")).toArray()) {
                ++named;
                bool found = false;
                for (const auto& w : doc.entities()) {
                    if (w.id != v.toString()) continue;
                    found = true;
                    const QString from = w.meta.value(QStringLiteral("defWire")).toString();
                    if (from != QLatin1String("w5") && from != QLatin1String("w6")) ++wrong;
                }
                if (!found) ++wrong; // named an entity that is not in the drawing
            }
        }
        std::printf("    (twist refs: %d twist(s), %d wire(s) named, %d wrong)\n",
                    twists, named, wrong);
        check(twists >= 1, "twist refs: the pair really was braided");
        check(named == twists * 2, "twist refs: each twist names both wires of its pair");
        check(wrong == 0, "twist refs: a twist only ever names the wires of its own group");

        // And nothing else claims the coax pigtails: they are not any definition row.
        int pigtails = 0;
        for (const auto& w : doc.entities())
            if (w.type == EntityType::Wire &&
                !w.meta.contains(QLatin1String("defWire")) &&
                !w.meta.contains(QLatin1String("shieldLeg")) &&
                !w.meta.contains(QLatin1String("shieldBond")))
                ++pigtails;
        std::printf("    (unnamed runs: %d)\n", pigtails);
        check(pigtails >= 4, "twist refs: the cable's pigtails belong to no definition row");
    }

    // ---- A twist is never drawn over a wire's corner ----
        {
            // A twist takes over a stretch of straight wire. Placed across a corner, the leg that
            // turns there was drawn as a line running out of the braid to nothing - the reported
            // "unconnected vertical wire".
            HarnessDef d;
            HarnessConnector a;
            a.id = "p1"; a.side = HarnessSide::Left; a.designator = "P1"; a.pinCount = 6;
            HarnessConnector b2;
            b2.id = "p2"; b2.side = HarnessSide::Right; b2.designator = "P2"; b2.pinCount = 6;
            b2.leg = QStringLiteral("2"); // dropped below, so both wires turn a corner mid-span
            d.connectors = {a, b2};
            for (int i = 1; i <= 2; ++i) {
                HarnessWire w;
                w.id = QStringLiteral("w%1").arg(i);
                w.fromConn = "p1"; w.fromPin = i; w.toConn = "p2"; w.toPin = i;
                w.twistGroup = QStringLiteral("T1");
                d.wires.push_back(w);
            }
            d.layout.twistCount = 2;
            d.layout.twistAmount = 2;
            const HarnessBuild out = buildHarness(d);
            int twists = 0, overCorner = 0;
            for (const auto& t : out.entities) {
                if (t.type != EntityType::Twist) continue;
                ++twists;
                double band = 0.0, cx = 0.0;
                for (const Vec2& c1 : t.points)
                    for (const Vec2& c2 : t.points) band = std::max(band, std::abs(c1.y - c2.y));
                for (const Vec2& c1 : t.points) cx += c1.x;
                if (!t.points.empty()) cx /= t.points.size();
                const double half =
                    twistBandWidth(t.meta.value("repeats").toInt(2), band,
                                   t.meta.value("flatLen").toDouble(6.0)) / 2.0;
                for (const auto& w : out.entities) {
                    if (w.type != EntityType::Wire || w.points.size() < 3) continue;
                    for (size_t i = 1; i + 1 < w.points.size(); ++i)
                        if (std::abs(w.points[i].x - cx) < half) ++overCorner;
                }
            }
            std::printf("    (twist/corner: %d twist(s), %d over a corner)\n", twists,
                        overCorner);
            check(twists >= 1, "twist/corner: the pair really was braided");
            check(overCorner == 0, "twist/corner: no twist symbol covers a wire's corner");
        }

        // ---- A generated screen is not lettered ----
        {
            HarnessDef d;
            HarnessConnector a;
            a.id = "p1"; a.side = HarnessSide::Left; a.designator = "P1"; a.pinCount = 4;
            a.shieldLeft = true;
            HarnessConnector b2;
            b2.id = "p2"; b2.side = HarnessSide::Right; b2.designator = "P2"; b2.pinCount = 4;
            d.connectors = {a, b2};
            for (int i = 1; i <= 2; ++i) {
                HarnessWire w;
                w.id = QStringLiteral("w%1").arg(i);
                w.fromConn = "p1"; w.fromPin = i; w.toConn = "p2"; w.toPin = i;
                w.shieldGroup = QStringLiteral("S1");
                d.wires.push_back(w);
            }
            d.layout.shieldAmount = 1;
            int screens = 0, lettered = 0;
            for (const auto& e : buildHarness(d).entities) {
                if (e.type != EntityType::Screen) continue;
                ++screens;
                if (e.meta.value("showS").toBool(false)) ++lettered;
            }
            check(screens >= 2, "screen letter: the screens are there to check");
            check(lettered == 0, "screen letter: a generated screen carries no S");
        }

        // ---- A coax joins the harness through one splice with two joints ----
        {
            // The shield conductor lands on the BRAID, not on the signal it screens; and where
            // the cable meets the harness both conductors are spliced, drawn as a single part.
            HarnessDef d;
            HarnessConnector p1;
            p1.id = "p1"; p1.side = HarnessSide::Left; p1.designator = "P1"; p1.pinCount = 4;
            HarnessConnector p2;
            p2.id = "p2"; p2.side = HarnessSide::Right; p2.designator = "P2"; p2.pinCount = 4;
            d.connectors = {p1, p2};
            HarnessWire sig;
            sig.id = "sig"; sig.kind = "coax-splice"; sig.bundle = "COAX1";
            sig.color = "SIGNAL1";
            sig.fromConn = "p1"; sig.fromPin = 1; sig.toConn = "p2"; sig.toPin = 1;
            HarnessWire shl;
            shl.id = "shl"; shl.kind = "coax-splice"; shl.bundle = "COAX1";
            shl.color = "SHIELD1";
            shl.fromConn = "p1"; shl.fromPin = 3; shl.toConn = "p2"; shl.toPin = 3;
            d.wires = {sig, shl};
            d.layout.spliceAtJoins = true;
            const HarnessBuild out = buildHarness(d);

            // The braid, and where the shield conductor ends.
            int braids = 0, ontoSignal = 0, ontoBraid = 0, pairs = 0, plainDots = 0;
            for (const auto& sc : out.entities) {
                if (sc.type != EntityType::Screen ||
                    !sc.meta.contains(QLatin1String("coaxScreenOf")))
                    continue;
                ++braids;
                const Vec2 c = sc.points.front();
                const double ry = sc.meta.value("ry").toDouble();
                for (const auto& w : out.entities) {
                    if (w.type != EntityType::Wire ||
                        w.meta.value("wireKind").toString() != QLatin1String("coaxShield"))
                        continue;
                    const Vec2 tip = w.points.back();
                    if (std::abs(tip.x - c.x) > 1e-6) continue;
                    if (std::abs(tip.y - c.y) < 1e-6) ++ontoSignal;
                    else if (near(std::abs(tip.y - c.y), ry)) ++ontoBraid;
                }
            }
            for (const auto& sp : out.entities) {
                if (sp.type != EntityType::Splice) continue;
                if (sp.meta.value("pair").toBool(false)) ++pairs;
                else ++plainDots;
            }
            std::printf("    (coax: %d braid(s), shield tip on signal %d / on braid %d,"
                        " splices %d paired + %d plain)\n",
                        braids, ontoSignal, ontoBraid, pairs, plainDots);
            check(braids == 2, "coax: a braid at each end of the cable");
            check(ontoSignal == 0, "coax: the shield never runs into the signal centreline");
            check(ontoBraid == braids, "coax: every shield conductor ends on the braid's outline");
            check(pairs == 2, "coax: each end carries one two-joint splice");
            check(plainDots == 0, "coax: and no loose dot repeating the same joint");
            // The two joints sit on the two conductors, at the same x.
            for (const auto& sp : out.entities) {
                if (sp.type != EntityType::Splice || !sp.meta.value("pair").toBool(false)) continue;
                check(near(sp.a.x, sp.b.x), "coax splice: both joints sit at the same x");
                check(std::abs(sp.a.y - sp.b.y) > 1e-6,
                      "coax splice: the joints are on the two different conductors");
                // The meta KEY keeps the old name so drawings written before the rename still read;
                // the word drawn on the sheet is what the app calls the thing.
                check(sp.meta.value("spliceText").toString() == QLatin1String("JOINT"),
                      "coax joint: the part is labelled");
            }
            // Past each joint the conductors are wires of their own: the cable is what runs
            // between the two splices, and the pigtails reach the pins.
            {
                int reachPin = 0, cableEnds = 0;
                std::vector<Vec2> joints;
                for (const auto& sp : out.entities)
                    if (sp.type == EntityType::Splice && sp.meta.value("pair").toBool(false)) {
                        joints.push_back(sp.a);
                        joints.push_back(sp.b);
                    }
                auto atJoint = [&joints](const Vec2& q) {
                    for (const Vec2& j : joints)
                        if (std::abs(j.x - q.x) < 1e-6 && std::abs(j.y - q.y) < 1e-6) return true;
                    return false;
                };
                // Every connector pin, so a pigtail can be told from the cable's own sections
                // (which run from a joint to the braid, not to a pin).
                std::vector<Vec2> pins;
                for (const auto& e : out.entities)
                    if (e.type == EntityType::Connector) {
                        const ConnectorGeometry g =
                            connectorGeometry(e.a, connectorSpecFromMeta(e.meta));
                        for (const Vec2& pe : g.pinEnd) pins.push_back(pe);
                    }
                auto atPin = [&pins](const Vec2& q) {
                    for (const Vec2& pe : pins)
                        if (std::abs(pe.x - q.x) < 1e-6 && std::abs(pe.y - q.y) < 1e-6) return true;
                    return false;
                };
                for (const auto& w : out.entities) {
                    if (w.type != EntityType::Wire || w.points.size() < 2) continue;
                    const bool a = atJoint(w.points.front()), b2 = atJoint(w.points.back());
                    if (a && b2) ++cableEnds; // the cable's own signal section
                    else if ((a && atPin(w.points.back())) || (b2 && atPin(w.points.front())))
                        ++reachPin; // a pigtail: the joint at one end, a pin at the other
                }
                std::printf("    (coax cut: %d cable section(s), %d pigtail(s))\n",
                            cableEnds, reachPin);
                check(cableEnds == 1, "coax cut: one cable section runs between the two joints");
                // Two conductors x two ends: four pigtails.
                check(reachPin == 4, "coax cut: every conductor is a separate wire past the joint");
                // And no conductor still runs straight from a pin into the cable.
                for (const auto& w : out.entities) {
                    if (w.type != EntityType::Wire || w.points.size() < 2) continue;
                    if (w.meta.value("bundle").toString() != QLatin1String("COAX1")) continue;
                    check(!atPin(w.points.front()) && !atPin(w.points.back()),
                          "coax cut: the cable itself no longer reaches a pin");
                }
                for (const auto& w : out.entities) {
                    if (w.type != EntityType::Wire || w.points.size() < 2) continue;
                    if (atJoint(w.points.front()) && atJoint(w.points.back()))
                        check(w.meta.contains(QLatin1String("bundle")),
                              "coax cut: the cable section is still part of the bundle");
                }
            }

            // A plain coax has no transition at all: it runs pin to pin as one cable.
            HarnessDef plain = d;
            for (auto& w : plain.wires) w.kind = QStringLiteral("coax");
            int none = 0, sections = 0;
            for (const auto& e : buildHarness(plain).entities) {
                if (e.type == EntityType::Splice) ++none;
                if (e.type == EntityType::Wire &&
                    e.meta.value("bundle").toString() == QLatin1String("COAX1"))
                    ++sections;
            }
            std::printf("    (plain coax: %d splice(s), %d bundled run(s))\n", none,
                        sections);
            check(none == 0, "coax splice: only a coax-splice is spliced to the harness");
            check(sections >= 1, "coax: a plain coax still draws its cable");
        }

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
                if (e.type == EntityType::Wire &&
                    e.meta.value("shieldBond").toString() == QLatin1String("P1"))
                    bond = &e;
            check(bond != nullptr, "shield bond: a drain wire to the body was drawn");
            if (bond) {
                check(bond->meta.value("drainWire").toBool(),
                      "shield bond: the drain is marked as one");
                check(bond->meta.value("wireColor").toString() == QLatin1String("DRAIN") &&
                          bond->meta.value("wireGauge").toString() == QLatin1String("0.5"),
                      "shield bond: the drain carries the Drain colour and a cross-section");
            }
            // A screen terminated on its drain is not marked not-connected, so no X is drawn.
            for (const auto& e : out.entities)
                if (e.type == EntityType::Screen &&
                    e.meta.value("overallShield").toString() == QLatin1String("P1"))
                    check(e.meta.contains("notConnected") &&
                              !e.meta.value("notConnected").toBool(),
                          "shield bond: the leg is terminated, so the X goes");
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
            check(HarnessLayout().wireColorMode == QLatin1String("bylayer"),
                  "wire colour: By layer unless told otherwise");
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

    // ---- Connection table: rows straight from the definition, shields as nested SPANS
    //      (an overall one and, inside it, a named subset), twist as a merged run.
    {
        HarnessDef d;
        HarnessConnector p1;
        p1.id = "p1"; p1.side = HarnessSide::Left; p1.designator = "P1"; p1.pinCount = 4;
        p1.shieldLeft = true; // wants an OVERALL shield: everything touching p1
        HarnessConnector p2;
        p2.id = "p2"; p2.side = HarnessSide::Right; p2.designator = "P2"; p2.pinCount = 4;
        d.connectors = {p1, p2};
        for (int i = 1; i <= 4; ++i) {
            HarnessWire w;
            w.id = QStringLiteral("w%1").arg(i);
            w.fromConn = "p1"; w.fromPin = i;
            w.toConn = "p2"; w.toPin = i;
            w.color = QStringLiteral("RED");
            w.gauge = QStringLiteral("20AWG");
            d.wires.push_back(w);
        }
        // w1/w2 share an INTERNAL shield group, nested inside p1's overall one.
        d.wires[0].shieldGroup = d.wires[1].shieldGroup = QStringLiteral("G1");
        // w3/w4 are twisted together.
        d.wires[2].twistGroup = d.wires[3].twistGroup = QStringLiteral("T1");

        const ConnectionTable ct = harnessConnectionTable(d);
        check(ct.rows.size() == 4, "conn-table: one row per wire");
        // Grouped by shield/twist so each group's rows are adjacent: w1,w2 (shieldGroup) then
        // w3,w4 (twistGroup) - the wiring itself (pin 1-1, 2-2, ...) is untouched by this.
        if (ct.rows.size() == 4) {
            check(ct.rows[0].wireId == "w1" && ct.rows[1].wireId == "w2" &&
                      ct.rows[2].wireId == "w3" && ct.rows[3].wireId == "w4",
                  "conn-table: rows are grouped so a shield/twist group is contiguous");
            check(ct.rows[0].from == QLatin1String("P1-1") && ct.rows[0].to == QLatin1String("P2-1"),
                  "conn-table: from/to read the designator and the harness's own pin label");
            check(ct.rows[0].color == QLatin1String("RED") && ct.rows[0].gauge == QLatin1String("20AWG"),
                  "conn-table: colour and gauge come straight off the wire");
        }
        const ConnectionTableSpan *overall = nullptr, *internal = nullptr;
        for (const auto& sp : ct.shieldSpans) (sp.level == 0 ? overall : internal) = &sp;
        check(overall != nullptr && internal != nullptr,
              "conn-table: both an overall and a nested internal shield span are found");
        if (overall)
            check(overall->firstRow == 0 && overall->lastRow == 3 &&
                      overall->label == QLatin1String("SHIELD"),
                  "conn-table: the overall shield covers every wire touching p1 - all four rows");
        if (internal)
            check(internal->firstRow == 0 && internal->lastRow == 1 &&
                      internal->label == QLatin1String("G1"),
                  "conn-table: the internal shield covers just its own named subset, nested inside");
        check(ct.twistSpans.size() == 1, "conn-table: one twist span");
        if (ct.twistSpans.size() == 1) {
            check(ct.twistSpans[0].firstRow == 2 && ct.twistSpans[0].lastRow == 3,
                  "conn-table: the twist span covers exactly its two wires");
            check(ct.twistSpans[0].memberCount == 2, "conn-table: and knows there are 2 of them");
        }

        // A wire with an open end - nothing to print for that side - is left out.
        HarnessWire open;
        open.id = "wo"; open.fromConn = "p1"; open.fromPin = 1;
        open.toConn = harnessOpenEndpointKey(); open.toPin = 0;
        HarnessDef d2 = d;
        d2.wires.push_back(open);
        check(harnessConnectionTable(d2).rows.size() == 4,
              "conn-table: a wire with an open end is not listed");

        const std::vector<Entity> ents = harnessConnectionTableEntities(d);
        const Entity* table = nullptr;
        int lines = 0, texts = 0;
        for (const auto& e : ents) {
            if (e.meta.value("connectionTable").toBool()) table = &e;
            if (e.meta.value("connectionTableBracket").toBool()) {
                if (e.type == EntityType::Line) ++lines;
                if (e.type == EntityType::Text) ++texts;
            }
        }
        check(table != nullptr, "conn-table entities: a table entity is built");
        check(lines == 6 && texts == 2,
              "conn-table entities: 3 lines + 1 label per bracket, 2 brackets (overall + internal)");
        if (table) {
            check(table->meta.value("rows").toInt() == 5 && table->meta.value("cols").toInt() == 6,
                  "conn-table entities: header + 4 rows, and Shield is not a column any more");
            const std::vector<TableMerge> merges = tableMerges(*table);
            check(merges.size() == 1 && merges[0].row == 3 && merges[0].col == 5 &&
                      merges[0].rowSpan == 2,
                  "conn-table entities: the twist group's two rows are merged into one cell");
            const QJsonArray cells = table->meta.value("cells").toArray();
            const QJsonArray kinds = table->meta.value("cellIconKind").toArray();
            const int cols = table->meta.value("cols").toInt();
            check(cells[3 * cols + 5].toString() == QLatin1String("2") &&
                      kinds[3 * cols + 5].toString() == QLatin1String("twist"),
                  "conn-table entities: the merge's anchor cell shows the count and the icon");
            check(cells[4 * cols + 5].toString().isEmpty() && kinds[4 * cols + 5].toString().isEmpty(),
                  "conn-table entities: the cell it merges into carries nothing of its own");
        }
        std::vector<double> labelXs;
        for (const auto& e : ents)
            if (e.type == EntityType::Text) labelXs.push_back(e.a.x);
        check(labelXs.size() == 2 && !near(labelXs[0], labelXs[1]),
              "conn-table entities: the overall and internal brackets sit in two different lanes");
    }


    // ---- a marked face is what the part is SET DOWN BY ----------------------------------------------
    {
        // A face the conductors leave by IS where the harness meets the part - that is what the mark means.
        // Marking one used to describe the part and change nothing about where it landed: the part was still
        // centred on the joint, which on a connector whose body is long on one side of its face and short on
        // the other puts half of it through the bundle.
        //
        // Driven through the generator, not through the placing code's arithmetic copied into a test: what is
        // being checked is where the part ENDS UP.
        auto placedAt = [](const LibraryPart& part, double dirRad) {
            MechDrawOptions opt;
            return placedPartDrawingForTest(part, Vec2{100.0, 50.0}, dirRad, opt);
        };
        // A connector: a body 20 long, and the face the wires leave by across its left-hand end.
        LibraryPart faced;
        faced.id = QStringLiteral("P-F");
        faced.kind = PartKind::Connector;
        Entity body;
        body.type = EntityType::Rect;
        body.points = {{0.0, -6.0}, {20.0, -6.0}, {20.0, 6.0}, {0.0, 6.0}};
        faced.mechanical = partDrawingFromEntities({body});
        faced.outputs.push_back({{0.0, -5.0}, {0.0, 5.0}});

        const std::vector<Entity> down = placedAt(faced, 0.0);
        check(!down.empty(), "face placement: the part is drawn");
        if (!down.empty()) {
            Bounds b;
            for (const Entity& e : down) b.expand(e.bounds());
            std::printf("  face placement: body x %.1f..%.1f, y %.1f..%.1f (joint at 100,50)\n", b.min.x,
                        b.max.x, b.min.y, b.max.y);
            // The face's centre lands ON the joint, and the body reaches away along the run - not straddling
            // it, which is what centring did.
            check(std::abs(b.min.x - 100.0) < 1e-6 && std::abs(b.max.x - 120.0) < 1e-6,
                  "face placement: the face is on the joint and the body reaches away from it");
            check(std::abs((b.min.y + b.max.y) / 2.0 - 50.0) < 1e-6,
                  "face placement: and it sits across the run, centred on it");
        }
        // Told nothing, it is centred on the joint as before - a guess, but the same guess it always was.
        LibraryPart bare = faced;
        bare.outputs.clear();
        const std::vector<Entity> centred = placedAt(bare, 0.0);
        if (!centred.empty()) {
            Bounds b;
            for (const Entity& e : centred) b.expand(e.bounds());
            std::printf("  face placement: unmarked, body x %.1f..%.1f\n", b.min.x, b.max.x);
            check(std::abs((b.min.x + b.max.x) / 2.0 - 100.0) < 1e-6,
                  "face placement: a part told nothing is still centred on the joint");
        }
        // And the kinds it is offered for: wires go into a connector and a ferrule; a length of tape has no
        // face to mark and no business being asked about one.
        check(partKindTakesWires(PartKind::Connector) && partKindTakesWires(PartKind::Ferrule) &&
                  partKindTakesWires(PartKind::Crimp) && partKindTakesWires(PartKind::Backshell),
              "face placement: the kinds wires go into can be told where they leave");
        check(!partKindTakesWires(PartKind::Tape) && !partKindTakesWires(PartKind::CableTie) &&
                  !partKindTakesWires(PartKind::Wire),
              "face placement: and the kinds they do not are not asked");
    }


    // ---- a connector that comes with its pins fitted -------------------------------------------------
    {
        // Most connectors are ordered as a housing and its contacts separately, and the contact is a part of
        // its own tied to the housing. Some come with the pins ALREADY IN THEM - a moulded header, an
        // overmoulded lead, a power connector with its terminals fitted - and for those there is no contact
        // to order, so the tie is not a question with an answer. What matters instead is what those pins ARE,
        // and a housing can carry two sizes at once: two heavy ways and six signal ways, which is the whole
        // reason for buying one.
        LibraryPart p;
        p.id = QStringLiteral("P-PINS");
        p.kind = PartKind::Connector;
        p.params[QStringLiteral("ways")] = QStringLiteral("8");
        check(!partIncludesPins(p), "pins: a connector is ordered with separate contacts unless it says so");
        setPartIncludesPins(p, true);
        check(partIncludesPins(p), "pins: and says so when it comes with them");

        std::vector<PartPinType> types;
        PartPinType heavy;
        heavy.size = QStringLiteral("16");
        heavy.unit = QStringLiteral("AWG");
        heavy.gauge = QStringLiteral("14");
        heavy.amps = QStringLiteral("13");
        heavy.pins = QStringLiteral("1-2");
        types.push_back(heavy);
        PartPinType signal;
        signal.size = QStringLiteral("20");
        signal.unit = QStringLiteral("AWG");
        signal.gauge = QStringLiteral("22");
        signal.amps = QStringLiteral("5");
        signal.pins = QStringLiteral("3-8");
        types.push_back(signal);
        setPartPinTypes(p, types);

        // KEPT, and read back as written: the order they were entered in is the order they are read, because
        // a datasheet lists the heavy ways first and a form that re-sorts them is a form that argues.
        const std::vector<PartPinType> back = partPinTypes(p);
        std::printf("  pins: %lld type(s) kept, first %s %s %s for pins %s\n",
                    static_cast<long long>(back.size()),
                    back.empty() ? "" : qPrintable(back.front().size),
                    back.empty() ? "" : qPrintable(back.front().gauge),
                    back.empty() ? "" : qPrintable(back.front().unit),
                    back.empty() ? "" : qPrintable(back.front().pins));
        check(back.size() == 2, "pins: both types are kept");
        if (back.size() == 2) {
            check(back[0].size == QStringLiteral("16") && back[0].gauge == QStringLiteral("14") &&
                      back[0].unit == QStringLiteral("AWG") && back[0].amps == QStringLiteral("13"),
                  "pins: a row is a size, the conductor it takes, in which unit, and what it carries");
            check(back[1].pins == QStringLiteral("3-8"), "pins: and which pins are that type");
        }

        // WHICH PINS ARE WHICH, worked out for the whole connector.
        const QStringList gauges = partPinGauges(p);
        std::printf("  pins: pin 1 takes %s, pin 8 takes %s\n",
                    gauges.value(0).isEmpty() ? "(nothing said)" : qPrintable(gauges.value(0)),
                    gauges.value(7).isEmpty() ? "(nothing said)" : qPrintable(gauges.value(7)));
        check(gauges.size() == 8, "pins: one answer per way");
        check(gauges.value(0) == QStringLiteral("14 AWG") &&
                  gauges.value(1) == QStringLiteral("14 AWG"),
              "pins: the two heavy ways take the heavy conductor");
        check(gauges.value(2) == QStringLiteral("22 AWG") &&
                  gauges.value(7) == QStringLiteral("22 AWG"),
              "pins: and the six signal ways the small one");

        // A pin no type claims is left blank rather than guessed at.
        std::vector<PartPinType> partial;
        partial.push_back(heavy);   // pins 1-2 only
        LibraryPart q = p;
        setPartPinTypes(q, partial);
        const QStringList some = partPinGauges(q);
        check(some.value(0) == QStringLiteral("14 AWG") && some.value(4).isEmpty(),
              "pins: a pin no type claims is left blank, not filled in with a guess");

        // AND THE SCHEMATIC LABELS EACH PIN WITH ITS OWN SIZE. A symbol that says one gauge for a housing
        // with two in it is a symbol somebody wires the wrong way from.
        const QJsonArray sym = connectorSymbolFromData(p);
        check(!sym.isEmpty(), "pins: the symbol is still built from the part's data");
        QStringList onSymbol;
        for (const Entity& e : partDrawingEntities(sym))
            if (e.type == EntityType::Connector)
                for (const auto& v : e.meta.value(QStringLiteral("pinGauge")).toArray())
                    onSymbol << v.toString();
        std::printf("  pins: the symbol says %s\n", qPrintable(onSymbol.join(QStringLiteral(" / "))));
        check(onSymbol.size() == 8 && onSymbol.value(0) == QStringLiteral("14 AWG") &&
                  onSymbol.value(7) == QStringLiteral("22 AWG"),
              "pins: each pin on the symbol carries the conductor its own size takes");
    }


    // ---- the pins the connector comes with reach its description, its table and its symbol ----------
    {
        // ONE PLACE SAYS WHAT THE PINS ARE. The technical parameters used to ask as well - a tick, one unit
        // for the whole connector and one gauge range for the whole connector - which could only ever say
        // less than the rows can, on a housing whose whole point may be that it carries two sizes at once.
        // Two places to say one thing is two places to disagree.
        LibraryPart p;
        p.id = QStringLiteral("P-DESC");
        p.kind = PartKind::Connector;
        p.mfrPn = QStringLiteral("X-8W");
        p.params[QStringLiteral("ways")] = QStringLiteral("8");
        p.params[QStringLiteral("pitch")] = QStringLiteral("3");
        QStringList asked;
        for (const PartField& f : partFields(PartKind::Connector)) asked << f.label;
        std::printf("  own pins: a connector is asked for %s\n", qPrintable(asked.join(QStringLiteral(", "))));
        check(!asked.contains(QStringLiteral("Includes pins")) &&
                  !asked.contains(QStringLiteral("Pin gauge in")) &&
                  !asked.contains(QStringLiteral("Pin gauge range")),
              "own pins: the three fields that duplicated the rows are gone");
        check(asked.contains(QStringLiteral("Contact material")) &&
                  !asked.contains(QStringLiteral("Plating")),
              "own pins: and plating is called what it is - the contact's material");

        // A RANGE IS A REAL ANSWER, and it is kept in one form however it was typed: a size 20 contact takes
        // anything from 24 to 20 AWG and a datasheet states it that way.
        setPartIncludesPins(p, true);
        std::vector<PartPinType> types;
        PartPinType heavy;
        heavy.size = QStringLiteral("16");
        heavy.unit = QStringLiteral("AWG");
        heavy.gauge = QStringLiteral("20 - 16");   // typed loosely
        heavy.amps = QStringLiteral("13");
        heavy.pins = QStringLiteral("1-2");
        types.push_back(heavy);
        PartPinType signal;
        signal.size = QStringLiteral("20");
        signal.unit = QStringLiteral("AWG");
        signal.gauge = QStringLiteral("24 to 20");   // and the other way
        signal.amps = QStringLiteral("5");
        signal.pins = QStringLiteral("3-8");
        types.push_back(signal);
        setPartPinTypes(p, types);
        const std::vector<PartPinType> back = partPinTypes(p);
        std::printf("  own pins: ranges kept as %s and %s\n",
                    back.empty() ? "" : qPrintable(back[0].gauge),
                    back.size() < 2 ? "" : qPrintable(back[1].gauge));
        check(back.size() == 2 && back[0].gauge == QStringLiteral("20-16") &&
                  back[1].gauge == QStringLiteral("24-20"),
              "own pins: a range is kept, and the same range typed two ways reads one way");

        // THE DESCRIPTION SAYS WHAT THEY ARE. A connector bought with its pins in it is not described by its
        // way count alone - what those pins take is half of what somebody orders it for.
        const QString said = describePart(p);
        std::printf("  own pins: description - %s\n", qPrintable(said));
        check(said.contains(QStringLiteral("pins fitted")), "own pins: the description says they are fitted");
        check(said.contains(QStringLiteral("2 x 20-16 AWG")) &&
                  said.contains(QStringLiteral("6 x 24-20 AWG")),
              "own pins: and how many of each, and what each takes");
        check(said.contains(QStringLiteral("13 A")), "own pins: with the rating where there is one");

        // AND THE PIN TABLE OF THE SYMBOL FOLLOWS THE SAME ROWS - gauge and rating both, so a table filled
        // in by hand from a datasheet the library already holds is work nobody does twice.
        QStringList gauges, amps;
        for (const Entity& e : partDrawingEntities(connectorSymbolFromData(p)))
            if (e.type == EntityType::Connector) {
                for (const auto& v : e.meta.value(QStringLiteral("pinGauge")).toArray())
                    gauges << v.toString();
                for (const auto& v : e.meta.value(QStringLiteral("pinAmp")).toArray())
                    amps << v.toString();
            }
        std::printf("  own pins: pin 1 - %s / %s A; pin 8 - %s / %s A\n", qPrintable(gauges.value(0)),
                    qPrintable(amps.value(0)), qPrintable(gauges.value(7)), qPrintable(amps.value(7)));
        check(gauges.value(0) == QStringLiteral("20-16 AWG") &&
                  gauges.value(7) == QStringLiteral("24-20 AWG"),
              "own pins: the table's gauge column comes from the rows");
        check(amps.value(0) == QStringLiteral("13") && amps.value(7) == QStringLiteral("5"),
              "own pins: and so does its rating column");

        // A LIST THAT CAN BE ADDED TO CAN BE TAKEN FROM. Otherwise a misspelling typed once is offered for
        // ever - and only what was added can be forgotten: the built-in materials are the program's.
        const QStringList builtIn = partFieldChoices(PartKind::Connector, QStringLiteral("plating"));
        // Started from nothing, so a leftover from an earlier run cannot make this pass or fail for reasons
        // of its own: the list is kept between sessions, which is the whole point of it.
        forgetChoice(QStringLiteral("plating"), QStringLiteral("Tim"));
        rememberChoice(QStringLiteral("plating"), QStringLiteral("Tim"), builtIn);   // a slip for "Tin"
        check(addedChoices(QStringLiteral("plating")).contains(QStringLiteral("Tim")),
              "own pins: a typed material is remembered and offered again");
        forgetChoice(QStringLiteral("plating"), QStringLiteral("Tim"));
        check(!addedChoices(QStringLiteral("plating")).contains(QStringLiteral("Tim")),
              "own pins: and can be taken off the list again");
        if (!builtIn.isEmpty()) {
            forgetChoice(QStringLiteral("plating"), builtIn.front());
            check(partFieldChoices(PartKind::Connector, QStringLiteral("plating")).contains(builtIn.front()),
                  "own pins: a built-in one stays - it is the program's, not the office's");
        }
    }


    // ---- the schematic carries the count -------------------------------------------------------------
    {
        // A bill of materials is what is BUILT, and what is built is what the circuit says: four connectors
        // on the schematic are four connectors, whether the assembly sheet happens to show two of them or
        // six. It used to count whichever view had the most copies - a rule that gives a different answer
        // depending on which sheet somebody was working on last, and that orders two of a connector shown
        // twice mechanically (a front view and a section).
        PartsLibrary lib;
        LibraryPart part;
        part.id = QStringLiteral("P-CNT");
        part.kind = PartKind::Connector;
        part.description = QStringLiteral("Counted connector");
        part.mfrPn = QStringLiteral("CNT-1");
        part.params[QStringLiteral("ways")] = QStringLiteral("2");
        lib.parts.push_back(part);

        Document d;
        auto place = [&d](const QString& view, const QString& inst) {
            Entity e;
            e.id = QStringLiteral("e%1").arg(inst);
            e.type = EntityType::Rect;
            e.points = {{0.0, 0.0}, {5.0, 0.0}, {5.0, 5.0}, {0.0, 5.0}};
            e.meta.insert(QStringLiteral("libPart"), QStringLiteral("P-CNT"));
            e.meta.insert(QStringLiteral("blockInst"), inst);
            e.meta.insert(QLatin1String(kLibPartViewKey), view);
            d.entities().push_back(e);
        };
        // Shown twice in the metal - a front view and a section - and ONCE on the circuit. One is built.
        place(QStringLiteral("mechanical"), QStringLiteral("m1"));
        place(QStringLiteral("mechanical"), QStringLiteral("m2"));
        place(QStringLiteral("schematic"), QStringLiteral("s1"));
        std::vector<BomRow> rows = bomUnify(bomFromDrawing(d, lib));
        double qty = 0.0;
        for (const BomRow& r : rows)
            if (r.mfrPn == QStringLiteral("CNT-1")) qty += r.qty;
        std::printf("  bom count: 2 mechanical + 1 schematic = %.0f on the bill\n", qty);
        check(std::abs(qty - 1.0) < 1e-9,
              "bom count: the schematic says one, so one is ordered - not the two the metal shows");

        // A SECOND SCHEMATIC COPY IS A SECOND CONNECTOR. That is the whole point of counting from the
        // circuit: two symbols is two of them to build.
        place(QStringLiteral("schematic"), QStringLiteral("s2"));
        rows = bomUnify(bomFromDrawing(d, lib));
        qty = 0.0;
        for (const BomRow& r : rows)
            if (r.mfrPn == QStringLiteral("CNT-1")) qty += r.qty;
        std::printf("  bom count: a second symbol makes it %.0f\n", qty);
        check(std::abs(qty - 2.0) < 1e-9, "bom count: the same part again on the schematic counts again");

        // And with NO schematic copy at all, the view there is carries it - a mechanical-only drawing is
        // still a drawing, and counting nothing would be worse than counting what it has.
        Document mechOnly;
        {
            Document& d2 = mechOnly;
            Entity e;
            e.id = QStringLiteral("only");
            e.type = EntityType::Rect;
            e.points = {{0.0, 0.0}, {5.0, 0.0}, {5.0, 5.0}, {0.0, 5.0}};
            e.meta.insert(QStringLiteral("libPart"), QStringLiteral("P-CNT"));
            e.meta.insert(QStringLiteral("blockInst"), QStringLiteral("only1"));
            e.meta.insert(QLatin1String(kLibPartViewKey), QStringLiteral("mechanical"));
            d2.entities().push_back(e);
        }
        double onlyQty = 0.0;
        for (const BomRow& r : bomUnify(bomFromDrawing(mechOnly, lib)))
            if (r.mfrPn == QStringLiteral("CNT-1")) onlyQty += r.qty;
        std::printf("  bom count: mechanical only counts %.0f\n", onlyQty);
        check(std::abs(onlyQty - 1.0) < 1e-9,
              "bom count: with nothing on a circuit sheet, what is drawn is what is counted");
    }


    // ---- a length said in another unit is the same length --------------------------------------------
    {
        // 2 m IS 2000 mm. Changing the unit on a line used to relabel it and leave the figure alone, so a
        // line that said "2 m" became "2 mm" - the program saying something untrue about how much to buy, in
        // the one place somebody orders from.
        struct Case { double qty; const char* from; const char* to; double want; };
        const Case cases[] = {{2.0, "m", "mm", 2000.0},
                              {2000.0, "mm", "m", 2.0},
                              {1.0, "m", "cm", 100.0},
                              {1.0, "ft", "mm", 304.8},
                              {12.0, "in", "ft", 1.0},
                              {1.0, "yd", "in", 36.0}};
        for (const Case& c : cases) {
            const double got = convertQuantity(c.qty, QString::fromLatin1(c.from),
                                               QString::fromLatin1(c.to));
            std::printf("  units: %g %s = %g %s\n", c.qty, c.from, got, c.to);
            check(std::abs(got - c.want) < 1e-6, "units: a length said another way is the same length");
        }
        // A COUNT IS NOT A LENGTH. A hundred grommets are not any number of metres, so the figure stands and
        // only the word changes - guessing here would be worse than leaving it alone.
        check(std::abs(convertQuantity(100.0, QStringLiteral("each"), QStringLiteral("m")) - 100.0) < 1e-9,
              "units: pieces are not converted into metres - there is no answer to convert to");
        check(std::abs(convertQuantity(5.0, QStringLiteral("m"), QStringLiteral("each")) - 5.0) < 1e-9,
              "units: nor the other way about");
        check(std::abs(lengthUnitInMm(QStringLiteral("each"))) < 1e-12,
              "units: and a count says so - no length in it at all");
    }

    // ---- A TWISTED PAIR OR BUNDLE SNAPS ON EVERY LINE IT DRAWS ----------------------------------------
    // A dimension, or anything else that snaps, can land on a wire's edge where it curves, on the place two
    // wires cross, and on the corners of the lines - not only on the straight stretches.
    {
        Document d;
        Entity bundle;
        bundle.type = EntityType::TwistedPair;
        bundle.id = QStringLiteral("snap-bundle");
        bundle.meta.insert(QStringLiteral("twistLook"), QStringLiteral("woven"));   // the woven drawing, crossings and all
        bundle.points = {{0.0, 0.0}, {120.0, 0.0}};
        bundle.a = bundle.points.front();
        bundle.b = bundle.points.back();
        bundle.meta.insert(QStringLiteral("twistWires"), 4);
        d.entities().push_back(bundle);
        const RunSnap* snap = twistedPairSnap(d.entities().front());
        check(snap && snap->lines.size() > 100, "run snap: a laid-up bundle offers every facet of its wires");
        auto onDrawn = [&](const Vec2& p) {
            double best = 1e9;
            for (const auto& seg : snap->lines)
                best = std::min(best, closestPointOnSegment(p, seg.first, seg.second).distance);
            return best;
        };
        if (snap) {
            // NEAREST ON A CURVE: a facet in the middle of the run, the cursor a little off it.
            const auto& seg = snap->lines[snap->lines.size() / 3];
            const Vec2 mid = lerp(seg.first, seg.second, 0.5);
            SnapSettings nearest;
            nearest.objectSnap = true;
            nearest.snapEndpoint = false;
            nearest.snapMidpoint = false;
            nearest.snapIntersection = false;
            nearest.snapNearest = true;
            const SnapResult r = resolveSnap(d, mid + Vec2{0.05, 0.05}, std::nullopt, 0.5, nearest, {});
            std::printf("  run snap: nearest by (%.2f,%.2f) -> (%.3f,%.3f), %.4f off a drawn line\n", mid.x, mid.y,
                        r.point.x, r.point.y, onDrawn(r.point));
            check(r.snapped && r.kind == SnapKind::Nearest && onDrawn(r.point) < 1e-6,
                  "run snap: nearest lands on a wire's edge where it curves");

            // WHERE TWO WIRES CROSS, in the middle of the run.
            Vec2 crossing;
            bool found = false;
            for (size_t i = 0; i < snap->lines.size() && !found; ++i) {
                if (std::abs(snap->lines[i].first.x - 60.0) > 10.0) continue;
                for (size_t j = i + 2; j < snap->lines.size() && !found; ++j)
                    if (auto x = segmentSegmentIntersection(snap->lines[i].first, snap->lines[i].second,
                                                            snap->lines[j].first, snap->lines[j].second)) {
                        crossing = *x;
                        found = true;
                    }
            }
            check(found, "run snap: the bundle's wires cross on the drawing");
            SnapSettings cross;
            cross.objectSnap = true;
            cross.snapEndpoint = false;
            cross.snapMidpoint = false;
            cross.snapIntersection = true;
            cross.snapNearest = false;
            const SnapResult x = resolveSnap(d, crossing + Vec2{0.03, -0.03}, std::nullopt, 0.3, cross, {});
            std::printf("  run snap: crossing at (%.3f,%.3f) -> (%.3f,%.3f) kind %d\n", crossing.x, crossing.y,
                        x.point.x, x.point.y, int(x.kind));
            check(found && x.snapped && x.kind == SnapKind::Intersection && distance(x.point, crossing) < 0.05,
                  "run snap: a crossing of two wires is caught");

            // NO MIDPOINT IN THE MIDDLE OF A CURVE: a facet's middle is no place on the drawing.
            SnapSettings mids;
            mids.objectSnap = true;
            mids.snapEndpoint = false;
            mids.snapMidpoint = true;
            mids.snapIntersection = false;
            mids.snapNearest = false;
            const SnapResult m = resolveSnap(d, mid, std::nullopt, 0.2, mids, {});
            check(!(m.snapped && m.kind == SnapKind::Midpoint), "run snap: no midpoint on a facet of a curve");
        }

        // A FAN'S CORNERS. A straight pair that fans at its ends turns a corner where each wire leaves the
        // bundle, and that corner is caught as an end of a line is.
        Document f;
        Entity pair = bundle;
        pair.id = QStringLiteral("snap-fan");
        pair.meta.insert(QStringLiteral("twistWires"), 4);
        pair.meta.insert(QStringLiteral("twistStraight"), true);
        f.entities().push_back(pair);
        const RunSnap* fan = twistedPairSnap(f.entities().front());
        const auto strands = twistedPairStrands(f.entities().front());
        size_t ends = 0;
        for (const auto& s : strands) ends += s.empty() ? 0 : 2;
        std::printf("  run snap: a fanned pair offers %d point(s), %d of them wire ends\n",
                    fan ? int(fan->points.size()) : 0, int(ends));
        check(fan && fan->points.size() > ends, "run snap: a fanned pair offers its corners as well as its ends");
        if (fan) {
            Vec2 corner;
            bool inside = false;
            for (const Vec2& p : fan->points)
                if (p.x > 1.0 && p.x < 119.0) {
                    corner = p;
                    inside = true;
                    break;
                }
            check(inside, "run snap: a corner away from the ends");
            SnapSettings endSnap;
            endSnap.objectSnap = true;
            endSnap.snapEndpoint = true;
            endSnap.snapMidpoint = false;
            endSnap.snapIntersection = false;
            endSnap.snapNearest = false;
            const SnapResult c = resolveSnap(f, corner + Vec2{0.05, 0.05}, std::nullopt, 0.3, endSnap, {});
            check(inside && c.snapped && c.kind == SnapKind::Endpoint && distance(c.point, corner) < 1e-6,
                  "run snap: the corner is caught");
        }
    }

    // ---- A LAID-UP BUNDLE SEEN FROM THE SIDE -----------------------------------------------------------------
    // Its near strands, one beside the next, all lying the same way; nothing of the weave over that stretch.
    {
        Entity run;
        run.type = EntityType::TwistedPair;
        run.id = QStringLiteral("side-bundle");
        run.points = {{0.0, 0.0}, {160.0, 0.0}};
        run.a = run.points.front();
        run.b = run.points.back();
        run.meta.insert(QStringLiteral("twistWires"), 4);
        // A bundle is drawn seen from the side; woven only when it says so.
        check(!twistedPairSideView(run).empty(), "side view: a bundle that says nothing is drawn seen from the side");
        Entity woven = run;
        woven.meta.insert(QStringLiteral("twistLook"), QStringLiteral("woven"));
        check(twistedPairSideView(woven).empty(), "side view: one that says it is woven is woven");
        const auto all = twistedPairSideView(run);
        std::vector<SideStrand> pieces, wiresIn;
        for (const SideStrand& piece : all) (piece.wire ? wiresIn : pieces).push_back(piece);
        bool inside = true, closed = true;
        double lo = 1e9, hi = -1e9;
        int spanning = 0;
        for (const SideStrand& piece : pieces) {
            closed = closed && piece.lines.size() == 1 && piece.lines.front() == piece.fill;
            double plo = 1e9, phi = -1e9;
            for (const Vec2& p : piece.fill) {
                lo = std::min(lo, p.y);
                hi = std::max(hi, p.y);
                plo = std::min(plo, p.y);
                phi = std::max(phi, p.y);
                inside = inside && p.x > -1e-6 && p.x < 160.0 + 1e-6;
            }
            // A band reaches right across the bundle, its ends on the bundle's two edges.
            if (plo < -2.0 && phi > 2.0) ++spanning;
        }
        std::printf("  side view: %d band(s), %d right across, across %.2f..%.2f\n", int(pieces.size()), spanning, lo, hi);
        check(pieces.size() >= 8, "side view: the laid-up stretch is a row of bands");
        check(closed, "side view: each band is drawn whole - its outline is the edge of what it fills");
        check(inside, "side view: and none runs past the ends of the run");
        check(spanning >= 0.8 * pieces.size(), "side view: the bands reach right across the bundle");
        check(hi > 0.5 && lo < -0.5 && std::abs(hi + lo) < 0.5, "side view: from edge to edge");
        // THE WIRES GO INTO THE ROPE: each of the four at each end, from the end of the stretch in under a band,
        // its end hidden - a wire's last point lies inside one of the bands.
        auto insideBand = [&](const Vec2& p) {
            for (const SideStrand& band : pieces) {
                bool in = false;
                const auto& poly = band.fill;
                for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
                    if ((poly[i].y > p.y) != (poly[j].y > p.y) &&
                        p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x)
                        in = !in;
                if (in) return true;
            }
            return false;
        };
        int hidden = 0;
        for (const SideStrand& w : wiresIn) {
            if (w.lines.size() != 2 || w.lines[0].size() < 2) continue;
            // The end inside the rope is the one further from the run's nearer end.
            const Vec2 a = (w.lines[0].front() + w.lines[1].front()) * 0.5;
            const Vec2 b = (w.lines[0].back() + w.lines[1].back()) * 0.5;
            const Vec2 deep = std::min(a.x, 160.0 - a.x) > std::min(b.x, 160.0 - b.x) ? a : b;
            if (insideBand(deep)) ++hidden;
        }
        std::printf("  side view: %d wire(s) into the rope, %d ending under a band\n", int(wiresIn.size()), hidden);
        check(wiresIn.size() == 8 && hidden == 8, "side view: every wire goes into the rope at each end, under a band");
    }

    // ---- A TWISTED PAIR IS WOVEN; A BUNDLE IS ITS BANDS, AS A PAIR'S WOULD BE -------------------------------
    {
        auto runOf = [](int wires) {
            Entity run;
            run.type = EntityType::TwistedPair;
            run.id = QStringLiteral("look-%1").arg(wires);
            run.points = {{0.0, 0.0}, {160.0, 0.0}};
            run.a = run.points.front();
            run.b = run.points.back();
            run.meta.insert(QStringLiteral("twistWires"), wires);
            return run;
        };
        check(twistedPairSideView(runOf(2)).empty(), "pair look: a twisted pair is drawn woven, as it always was");
        Entity asked = runOf(2);
        asked.meta.insert(QStringLiteral("twistLook"), QStringLiteral("side"));
        const auto pairBands = twistedPairSideView(asked);
        check(!pairBands.empty(), "pair look: and seen from the side when it asks to be");
        // A bundle's bands are a pair's: the same, however many wires it carries.
        auto bandsOf = [](const std::vector<SideStrand>& all) {
            std::vector<SideStrand> bands;
            for (const SideStrand& piece : all)
                if (!piece.wire) bands.push_back(piece);
            return bands;
        };
        const auto four = bandsOf(twistedPairSideView(runOf(4)));
        const auto eight = bandsOf(twistedPairSideView(runOf(8)));
        std::printf("  bundle look: %d band(s) for four wires, %d for eight, %d for a pair seen from the side\n",
                    int(four.size()), int(eight.size()), int(pairBands.size()));
        check(!four.empty() && four.size() == eight.size(),
              "bundle look: drawn with a pair's bands, however many wires it has");
    }

    // ---- A BUNDLE'S WIRES COME OUT OF IT SIDE BY SIDE -------------------------------------------------------
    // Seen from the side, each wire leaves the bundle in the order it fans out in: none crosses another on its way.
    {
        Entity run;
        run.type = EntityType::TwistedPair;
        run.id = QStringLiteral("out-of-the-bundle");
        run.points = {{0.0, 0.0}, {160.0, 0.0}};
        run.a = run.points.front();
        run.b = run.points.back();
        run.meta.insert(QStringLiteral("twistWires"), 5);
        const auto edges = twistedPairStrands(run);
        int crossings = 0;
        for (size_t a = 0; a < edges.size(); ++a)
            for (size_t b = a + 1; b < edges.size(); ++b) {
                if (a / 2 == b / 2) continue;   // the two edges of one wire
                for (size_t i = 1; i < edges[a].size(); ++i)
                    for (size_t j = 1; j < edges[b].size(); ++j)
                        if (segmentSegmentIntersection(edges[a][i - 1], edges[a][i], edges[b][j - 1], edges[b][j]))
                            ++crossings;
            }
        std::printf("  bundle ends: %d wire(s), %d crossing(s) among them\n", int(edges.size() / 2), crossings);
        check(edges.size() == 10 && crossings == 0, "bundle ends: the wires come out of the bundle side by side, none crossing");
    }

    // ---- A SHEET OF BUNDLES DOES NOT SLOW THE CURSOR -------------------------------------------------------
    // More runs than the measurement cache once held, and a reach wide enough to take in thousands of the short
    // pieces bundles are drawn with: snapping stays a matter of milliseconds.
    {
        Document d;
        for (int i = 0; i < 150; ++i) {
            Entity t;
            t.id = QStringLiteral("many-%1").arg(i);
            t.type = EntityType::TwistedPair;
            t.points = {{0.0, i * 12.0}, {200.0, i * 12.0}};
            t.a = t.points.front();
            t.b = t.points.back();
            t.meta.insert(QStringLiteral("twistWires"), (i % 2) ? 2 : 4);
            d.entities().push_back(t);
        }
        SnapSettings sn;
        resolveSnap(d, {100.0, 100.0}, std::nullopt, 25.0, sn, {});   // every run measured once
        QElapsedTimer clock;
        clock.start();
        for (int k = 0; k < 10; ++k) resolveSnap(d, {100.0 + k, 100.0 + k}, std::nullopt, 25.0, sn, {});
        const double each = clock.elapsed() / 10.0;
        std::printf("  many bundles: %.1f ms a snap\n", each);
        check(each < 60.0, "many bundles: snapping over a sheet of them stays quick");
    }

    std::printf(g_fail ? "  -> %d failed\n" : "  -> ok\n", g_fail);
    return g_fail;
}
