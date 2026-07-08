#include "app/Editor.h"
#include "harness/Harness.h"
#include "model/Document.h"

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
        ed.onPointerPress({0, 0}, Qt::LeftButton, Qt::NoModifier);
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

    std::printf(g_fail ? "  -> %d failed\n" : "  -> ok\n", g_fail);
    return g_fail;
}
