#include "io/Dwx.h"
#include "io/Dxf.h"
#include "model/Document.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <cstdio>

using namespace Edraw_Desktop;

namespace {
int g_fail = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("  [FAIL] %s\n", what); ++g_fail; }
}
bool near(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }

Document makeSample() {
    Document d;
    d.entities().clear();
    Entity line; line.id = "ln"; line.type = EntityType::Line; line.layer = "0";
    line.a = {0, 0}; line.b = {50, 25};
    d.entities().push_back(line);
    Entity circle; circle.id = "ci"; circle.type = EntityType::Circle; circle.layer = "Construction";
    circle.center = {30, 30}; circle.radius = 12;
    d.entities().push_back(circle);
    Entity arc; arc.id = "ar"; arc.type = EntityType::Arc; arc.layer = "0";
    arc.center = {0, 0}; arc.radius = 20; arc.startAngle = 0; arc.endAngle = 1.5;
    d.entities().push_back(arc);
    Entity poly; poly.id = "pl"; poly.type = EntityType::Polyline; poly.layer = "0";
    poly.points = {{0, 0}, {10, 0}, {10, 10}};
    d.entities().push_back(poly);
    Entity txt; txt.id = "tx"; txt.type = EntityType::Text; txt.layer = "0";
    txt.a = {5, 5}; txt.meta.insert("text", "Hi"); txt.meta.insert("size", 4.0);
    d.entities().push_back(txt);
    return d;
}
} // namespace

int run_io_tests() {
    g_fail = 0;
    std::printf("io tests:\n");

    // --- DWX round-trip preserves everything.
    {
        Document src = makeSample();
        const QJsonObject json = dwx::toJson(src);
        Document dst;
        dwx::fromJson(dst, json);
        check(dst.entities().size() == src.entities().size(), "dwx: entity count");

        const Entity& circle = dst.entities()[1];
        check(circle.type == EntityType::Circle && near(circle.radius, 12.0), "dwx: circle radius");
        const Entity& arc = dst.entities()[2];
        check(arc.type == EntityType::Arc && near(arc.startAngle, 0.0) && near(arc.endAngle, 1.5),
              "dwx: arc angles (start/end)");
        const Entity& txt = dst.entities()[4];
        check(txt.type == EntityType::Text && txt.meta.value("text").toString() == "Hi" &&
                  near(txt.a.x, 5.0),
              "dwx: text position+content");
    }

    // --- DXF export then re-import yields a readable drawing.
    {
        Document src = makeSample();
        const QString text = dxf::toDxf(src);
        check(text.contains("CIRCLE") && text.contains("ARC") && text.contains("TEXT") &&
                  text.contains("LINE"),
              "dxf: contains entity records");
        Document dst;
        dxf::parse(dst, text);
        // line + circle + arc + polyline(3pts) + text = 5 entities round-trip.
        check(dst.entities().size() == 5, "dxf: 5 entities parsed");
        bool hasCircle = false, hasArc = false, hasText = false;
        for (const auto& e : dst.entities()) {
            if (e.type == EntityType::Circle && near(e.radius, 12.0)) hasCircle = true;
            if (e.type == EntityType::Arc) hasArc = true;
            if (e.type == EntityType::Text && e.meta.value("text").toString() == "Hi") hasText = true;
        }
        check(hasCircle, "dxf: circle re-imported");
        check(hasArc, "dxf: arc re-imported");
        check(hasText, "dxf: text re-imported");
    }

    // --- Parse a genuine Edraw-schema DWX (hand-written, not via our writer).
    {
        const char* raw = R"JSON({
          "format":"DWX","version":1,"app":"Edraw","unit":"mm",
          "layers":[{"name":"0","color":"#ffffff","visible":true,"locked":false,"weight":2}],
          "entities":[
            {"type":"text","layer":"0","position":{"x":12,"y":34},"text":"HARNESS","size":6},
            {"type":"arc","layer":"0","center":{"x":0,"y":0},"radius":15,"start":0,"end":2.0},
            {"type":"ellipse","layer":"0","center":{"x":5,"y":5},"rx":20,"ry":8,"rotation":0.5},
            {"type":"splice","layer":"0","position":{"x":7,"y":9},"size":5}
          ]})JSON";
        const QJsonDocument jd = QJsonDocument::fromJson(raw);
        Document d;
        dwx::fromJson(d, jd.object());
        check(d.entities().size() == 4, "edraw-dwx: entity count");
        const Entity& t = d.entities()[0];
        check(t.type == EntityType::Text && near(t.a.x, 12.0) && near(t.a.y, 34.0) &&
                  t.meta.value("text").toString() == "HARNESS",
              "edraw-dwx: text position read from 'position'");
        const Entity& a = d.entities()[1];
        check(a.type == EntityType::Arc && near(a.endAngle, 2.0), "edraw-dwx: arc end");
        const Entity& el = d.entities()[2];
        check(el.type == EntityType::Ellipse && near(el.radius, 20.0) && near(el.radiusY, 8.0),
              "edraw-dwx: ellipse rx/ry");
        const Entity& sp = d.entities()[3];
        check(sp.type == EntityType::Splice && near(sp.a.x, 7.0), "edraw-dwx: splice position");
    }

    std::printf(g_fail ? "  -> %d failed\n" : "  -> ok\n", g_fail);
    return g_fail;
}
