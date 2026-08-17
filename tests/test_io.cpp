#include "io/Dwx.h"
#include "io/Dxf.h"
#include "geometry/Geometry.h"   // expandBulgePolyline: what an imported arc actually draws as
#include "model/Document.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
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

    {
        QTemporaryDir temp;
        check(temp.isValid(), "io: temporary directory");
        if (temp.isValid()) {
            const Document src = makeSample();
            QString err;
            const QString dwxPath = temp.filePath(QStringLiteral("drawing.dwx"));
            check(dwx::save(src, dwxPath, &err), "dwx: atomic file save");
            QFile dwxFile(dwxPath);
            check(dwxFile.open(QIODevice::ReadOnly) && dwxFile.size() > 0,
                  "dwx: saved file is readable and non-empty");
            Document dwxLoaded;
            check(dwx::load(dwxLoaded, dwxPath, &err) &&
                      dwxLoaded.entities().size() == src.entities().size(),
                  "dwx: saved file reloads");

            const QString dxfPath = temp.filePath(QStringLiteral("drawing.dxf"));
            check(dxf::exportFile(src, dxfPath, &err), "dxf: atomic file export");
            QFile dxfFile(dxfPath);
            check(dxfFile.open(QIODevice::ReadOnly) && dxfFile.size() > 0,
                  "dxf: exported file is readable and non-empty");
            Document dxfLoaded;
            check(dxf::importFile(dxfLoaded, dxfPath, &err) &&
                      dxfLoaded.entities().size() == src.entities().size(),
                  "dxf: exported file reloads");
        }
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

    // --- A harness PRESET belongs to the drawing, so it has to survive being saved with it.
    {
        Document src;
        QJsonObject one;
        one[QStringLiteral("connectors")] = QJsonArray{QJsonObject{{"id", "c1"}}};
        one[QStringLiteral("wires")] = QJsonArray{QJsonObject{{"id", "w1"}},
                                                 QJsonObject{{"id", "w2"}}};
        QJsonObject presets;
        presets[QStringLiteral("P1 → P2")] = one;
        src.harnessPresets() = presets;

        Document dst;
        dwx::fromJson(dst, dwx::toJson(src));
        const QJsonObject back = dst.harnessPresets();
        check(back.keys() == QStringList{QStringLiteral("P1 → P2")},
              "dwx: a harness preset is saved with the drawing, under the name it was given");
        check(back.value(QStringLiteral("P1 → P2")).toObject()
                  .value(QStringLiteral("wires")).toArray().size() == 2,
              "dwx: and the whole definition comes back with it");

        // Deleting one leaves the drawing without it - a preset can be thrown away.
        QJsonObject fewer = dst.harnessPresets();
        fewer.remove(QStringLiteral("P1 → P2"));
        dst.harnessPresets() = fewer;
        Document again;
        dwx::fromJson(again, dwx::toJson(dst));
        check(again.harnessPresets().isEmpty(), "dwx: a deleted preset stays deleted");
    }

    // --- Layout sheets and viewports belong to the drawing and must survive a save/reload.
    {
        Document src;
        // The default document already carries one layout with one viewport; replace
        // it with a known two-viewport layout so the round-trip can be checked exactly.
        src.layouts().clear();
        DrawingLayout layout;
        layout.name = QStringLiteral("MyLayout");
        layout.paperWidth = 297.0;
        layout.paperHeight = 210.0;
        layout.templateName = QStringLiteral("A4");
        Entity border; border.id = "bd"; border.type = EntityType::Line; border.layer = "0";
        border.a = {0, 0}; border.b = {297, 0};
        layout.paperEntities.push_back(border);
        LayoutViewport vp1;
        vp1.id = QStringLiteral("vp1");
        vp1.x = 20.0; vp1.y = 20.0; vp1.width = 200.0; vp1.height = 150.0;
        vp1.modelCenter = {100.0, 50.0}; vp1.modelScale = 0.5;
        layout.viewports.push_back(vp1);
        LayoutViewport vp2;
        vp2.id = QStringLiteral("vp2");
        vp2.x = 240.0; vp2.y = 20.0; vp2.width = 50.0; vp2.height = 150.0;
        vp2.modelCenter = {0.0, 0.0}; vp2.modelScale = 2.0;
        layout.viewports.push_back(vp2);
        src.layouts().push_back(std::move(layout));

        Document dst;
        dwx::fromJson(dst, dwx::toJson(src));
        check(dst.layouts().size() == 1, "dwx: layout count preserved");
        if (dst.layouts().size() == 1) {
            const DrawingLayout& back = dst.layouts()[0];
            check(back.name == QStringLiteral("MyLayout"), "dwx: layout name preserved");
            check(near(back.paperWidth, 297.0) && near(back.paperHeight, 210.0),
                  "dwx: layout paper size preserved");
            check(back.templateName == QStringLiteral("A4"), "dwx: layout template name preserved");
            check(back.paperEntities.size() == 1, "dwx: layout paper entities preserved");
            check(back.viewports.size() == 2, "dwx: viewport count preserved");
            if (back.viewports.size() == 2) {
                const LayoutViewport& b1 = back.viewports[0];
                check(b1.id == QStringLiteral("vp1"), "dwx: viewport id preserved");
                check(near(b1.x, 20.0) && near(b1.y, 20.0), "dwx: viewport position preserved");
                check(near(b1.width, 200.0) && near(b1.height, 150.0),
                      "dwx: viewport size preserved");
                check(near(b1.modelCenter.x, 100.0) && near(b1.modelCenter.y, 50.0),
                      "dwx: viewport model center preserved");
                check(near(b1.modelScale, 0.5), "dwx: viewport model scale preserved");
                const LayoutViewport& b2 = back.viewports[1];
                check(near(b2.modelScale, 2.0), "dwx: second viewport model scale preserved");
            }
        }

        // A DWX with no layouts leaves the document's default layout in place
        // (the constructor creates one, and the loader only replaces when the
        // file actually carries layouts).
        src.layouts().clear();
        Document withDefault;
        // Against what a NEW drawing has rather than against a number: the point being made is that a
        // file carrying no layouts leaves the ones the constructor made alone, and how many that is is
        // a decision about new drawings (two sheets, at the time of writing) and not about loading.
        const size_t defaults = Document().layouts().size();
        dwx::fromJson(withDefault, dwx::toJson(src));
        check(withDefault.layouts().size() == defaults,
              "dwx: a drawing with no layouts keeps the layouts a new drawing has");
    }

    // --- DXF as AutoCAD writes it: formatted MTEXT, and geometry placed by blocks.
    {
        // A block holding one line and one label, placed twice - once turned and scaled - plus an
        // MTEXT with the formatting runs, the %% escapes and the split AutoCAD uses for a long note.
        const QString src = QStringLiteral(
            "0\nSECTION\n2\nBLOCKS\n"
            "0\nBLOCK\n2\nSYM\n10\n0.0\n20\n0.0\n"
            "0\nLINE\n8\n0\n10\n0.0\n20\n0.0\n11\n10.0\n21\n0.0\n"
            "0\nTEXT\n8\n0\n10\n0.0\n20\n0.0\n40\n2.0\n1\nP1\n"
            "0\nENDBLK\n"
            "0\nENDSEC\n"
            "0\nSECTION\n2\nENTITIES\n"
            "0\nINSERT\n8\n0\n2\nSYM\n10\n100.0\n20\n50.0\n"
            "0\nINSERT\n8\n0\n2\nSYM\n10\n200.0\n20\n50.0\n41\n2.0\n42\n2.0\n50\n90.0\n"
            "0\nMTEXT\n8\n0\n10\n0.0\n20\n0.0\n40\n2.5\n41\n60.0\n71\n1\n"
            "3\nFirst part of a long note that AutoCAD had to split, \n"
            "1\n{\\fArial|b0|i0|c0|p34;and the tail}: +0.5%%d, %%c8, %%p1\n"
            "0\nENDSEC\n0\nEOF\n");
        Document d;
        QString err;
        check(dxf::parse(d, src, &err), "dxf-in: the file is read");

        // The block was placed twice, so its line and its label are there twice - at the insertion
        // points, and the second one turned a right angle and drawn at twice the size.
        int lines = 0, labels = 0;
        const Entity* turned = nullptr;
        for (const auto& e : d.entities()) {
            if (e.type == EntityType::Line) {
                ++lines;
                if (near(e.a.x, 200.0) && near(e.a.y, 50.0)) turned = &e;
            }
            if (e.type == EntityType::Text && e.meta.value("text").toString() == "P1") ++labels;
        }
        check(lines == 2 && labels == 2,
              "dxf-in: a block placed by INSERT is drawn, once per placement");
        check(turned && near(turned->b.x, 200.0) && near(turned->b.y, 70.0),
              "dxf-in: with the insert's own scale and rotation (10 long, doubled, turned 90)");

        // The note: assembled from its continuation, its formatting dropped, its escapes decoded.
        const Entity* note = nullptr;
        for (const auto& e : d.entities())
            if (e.type == EntityType::Text && e.meta.value("text").toString().size() > 20) note = &e;
        check(note != nullptr, "dxf-in: the note is read");
        if (note) {
            const QString s = note->meta.value("text").toString();
            check(s.startsWith(QLatin1String("First part of a long note")),
                  "dxf-in: MTEXT split over group 3 and group 1 is put back together");
            check(s.contains(QLatin1String("and the tail")) && !s.contains(QLatin1Char('{')) &&
                      !s.contains(QLatin1String("Arial")),
                  "dxf-in: the inline formatting runs are dropped, not shown as text");
            check(s.contains(QChar(0x00B0)) && s.contains(QChar(0x2300)) &&
                      s.contains(QChar(0x00B1)),
                  "dxf-in: %%d, %%c and %%p come through as degree, diameter and plus-minus");
            check(near(note->meta.value("boxWidth").toDouble(), 60.0) &&
                      note->meta.value("justify").toString() == QLatin1String("TL"),
                  "dxf-in: and it keeps the box it wraps in and where its anchor sits");
        }
    }

    // --- The caret escapes, and where a label with no box sits about its anchor.
    {
        // ^I is a tab (a note's indents, a tolerance table's columns) and ^J a line feed. Shown
        // literally, a wire read "BLUE^I^I28 AWG".
        const QString src = QStringLiteral(
            "0\nSECTION\n2\nENTITIES\n"
            "0\nMTEXT\n8\n0\n10\n0.0\n20\n0.0\n40\n2.5\n71\n5\n"
            "1\nBLUE^I^I28 AWG^JWHITE^I28 AWG\n"
            "0\nENDSEC\n0\nEOF\n");
        Document d;
        check(dxf::parse(d, src), "dxf-in: the file is read");
        const Entity* t = nullptr;
        for (const auto& e : d.entities())
            if (e.type == EntityType::Text) t = &e;
        check(t != nullptr, "dxf-in: the label is read");
        if (t) {
            const QString s = t->meta.value("text").toString();
            check(!s.contains(QLatin1String("^I")) && !s.contains(QLatin1String("^J")),
                  "dxf-in: a caret escape is decoded, not shown");
            check(s.contains(QLatin1Char('\n')), "dxf-in: ^J breaks the line");
            check(s.startsWith(QLatin1String("BLUE")) && s.contains(QLatin1String("28 AWG")),
                  "dxf-in: and the words survive");
            // Tabs become spacing to the next four-column stop, so the columns line up: BLUE is 4
            // characters, so two tabs reach column 12.
            check(s.section(QLatin1Char('\n'), 0, 0) == QLatin1String("BLUE        28 AWG"),
                  "dxf-in: a tab pads to the next stop rather than vanishing");

            // The anchor: 5 is middle-centre, and with no box the text sits ABOUT that point.
            check(t->meta.value("justify").toString() == QLatin1String("MC"),
                  "dxf-in: the attachment point is kept");
            const Bounds b = t->bounds();
            check(b.min.y < t->a.y && b.max.y > t->a.y,
                  "dxf-in: a middle attachment straddles its anchor instead of hanging below it");
            check(b.min.x < t->a.x && b.max.x > t->a.x,
                  "dxf-in: and a centre attachment straddles it sideways");
        }
    }

    // --- The sheet the file states, a multileader's words, and text justified about its anchor.
    {
        const QString src = QStringLiteral(
            "0\nSECTION\n2\nHEADER\n"
            "9\n$LIMMIN\n10\n0.0\n20\n0.0\n"
            "9\n$LIMMAX\n10\n420.0\n20\n297.0\n"
            "9\n$EXTMIN\n10\n5.0\n20\n5.0\n"
            "9\n$EXTMAX\n10\n415.0\n20\n292.0\n"
            "0\nENDSEC\n"
            "0\nSECTION\n2\nENTITIES\n"
            // A cell's heading: centred, with a wrap width. Its anchor is the middle of the words,
            // NOT the corner of a box to justify them in.
            "0\nMTEXT\n8\n0\n10\n100.0\n20\n50.0\n40\n2.5\n41\n40.0\n71\n5\n1\nDescription\n"
            // A callout: one leader vertex, and the word it points with.
            "0\nMULTILEADER\n8\n0\n40\n1.0\n10\n60.0\n20\n80.0\n41\n2.5\n304\nLABEL\n"
            "304\nLEADER_LINE{\n10\n40.0\n20\n70.0\n303\n}\n"
            "0\nENDSEC\n0\nEOF\n");
        Document d;
        check(dxf::parse(d, src), "dxf-in: the file is read");

        // The sheet: the LIMITS, not the extents, and not the bounding box of the objects.
        check(d.sheet().valid && near(d.sheet().min.x, 0.0) && near(d.sheet().max.x, 420.0) &&
                  near(d.sheet().max.y, 297.0),
              "dxf-in: the sheet the file was drawn on is remembered, so the drawing opens on it");

        // The heading sits ABOUT its anchor: the box only says where the lines wrap.
        const Entity* head = nullptr;
        for (const auto& e : d.entities())
            if (e.type == EntityType::Text &&
                e.meta.value("text").toString() == QLatin1String("Description"))
                head = &e;
        check(head != nullptr, "dxf-in: the heading is read");
        if (head) {
            check(head->meta.value("anchorJustify").toBool(),
                  "dxf-in: imported text is justified about its anchor");
            const Bounds b = head->bounds();
            const double slack = 1.0;
            check(std::abs((b.min.x + b.max.x) / 2.0 - head->a.x) < slack,
                  "dxf-in: so a centred heading straddles its anchor rather than its column");
            check(b.max.x - b.min.x < 39.0,
                  "dxf-in: and it is as wide as its words, not as wide as the wrap width");
        }

        // The callout: ONE object, carrying both the line that points and the word it points with.
        const Entity* callout = nullptr;
        for (const auto& e : d.entities())
            if (e.type == EntityType::Leader) callout = &e;
        check(callout != nullptr, "dxf-in: a multileader is read as a leader");
        if (callout) {
            check(callout->meta.value("text").toString() == QLatin1String("LABEL"),
                  "dxf-in: a multileader's words are read, and they belong to the leader itself");
            check(callout->points.size() == 2 && near(callout->points.front().x, 40.0) &&
                      near(callout->points.back().x, 60.0),
                  "dxf-in: and the leader runs from its vertex to where the words sit");
        }
    }

    // --- A mask, and the order the drawing is drawn in.
    {
        // A wipeout hides what is under it - the sleeve round a cable label hides the braid running
        // through it. Its boundary is in IMAGE space: -0.5..+0.5 across, counted downwards, with 10/20
        // where it sits and 11/21, 12/22 the vectors along its width and height. And the order matters:
        // the file LISTS the line first, and the sort table says the mask is drawn first.
        const QString src = QStringLiteral(
            "0\nSECTION\n2\nENTITIES\n"
            "0\nLINE\n5\nA1\n8\n0\n10\n95.0\n20\n105.0\n11\n115.0\n21\n105.0\n"
            "0\nWIPEOUT\n5\nA2\n8\n0\n10\n100.0\n20\n100.0\n"
            "11\n10.0\n21\n0.0\n12\n0.0\n22\n10.0\n"
            "14\n-0.5\n24\n0.5\n14\n0.5\n24\n0.5\n14\n0.5\n24\n-0.5\n"
            "14\n-0.5\n24\n-0.5\n14\n-0.5\n24\n0.5\n"
            "0\nENDSEC\n"
            "0\nSECTION\n2\nOBJECTS\n"
            "0\nSORTENTSTABLE\n5\nB1\n100\nAcDbSortentsTable\n330\n1F\n"
            "331\nA1\n5\n20\n"
            "331\nA2\n5\n10\n"
            "0\nENDSEC\n0\nEOF\n");
        Document d;
        check(dxf::parse(d, src), "dxf-in: the file is read");

        int maskAt = -1, lineAt = -1;
        for (size_t i = 0; i < d.entities().size(); ++i) {
            if (d.entities()[i].type == EntityType::Wipeout) maskAt = int(i);
            if (d.entities()[i].type == EntityType::Line) lineAt = int(i);
        }
        check(maskAt >= 0, "dxf-in: a wipeout is read as a mask");
        check(lineAt >= 0 && maskAt < lineAt,
              "dxf-in: and the drawing is put in the order the file says it is drawn in, so a mask "
              "listed after what it hides still ends up under it");
        if (maskAt >= 0) {
            const Entity& m = d.entities()[size_t(maskAt)];
            const Bounds b = m.bounds();
            check(near(b.min.x, 100.0) && near(b.max.x, 110.0) && near(b.min.y, 100.0) &&
                      near(b.max.y, 110.0),
                  "dxf-in: the mask covers the square the file put it on, not the -0.5..0.5 of its "
                  "own picture");
            check(m.points.size() == 4,
                  "dxf-in: the repeated closing vertex is not kept as a corner of its own");
            check(m.meta.value("noFrame").toBool(),
                  "dxf-in: an imported mask is not framed - the drawing outlines it itself");
        }
    }

    // --- A mirrored placement, and the head a leader points with.
    {
        // AutoCAD records a mirrored object by turning the normal of its plane round (230 = -1), and
        // the object's own coordinates are then measured on that plane: x runs the other way. The
        // schematic's pin leads are placed like this, and read as though the normal were the usual one
        // they were drawn a sheet's width away to the left instead of against the connector.
        //
        // A leader carries no arrowhead: it names a dimension style and AutoCAD draws the head from
        // that style's arrow size, which the leader may override in its own extended data.
        const QString src = QStringLiteral(
            "0\nSECTION\n2\nTABLES\n"
            "0\nDIMSTYLE\n2\nEdi\n41\n3.0\n"
            "0\nENDSEC\n"
            "0\nSECTION\n2\nBLOCKS\n"
            "0\nBLOCK\n2\npin\n10\n0.0\n20\n0.0\n"
            "0\nLINE\n8\n0\n10\n0.0\n20\n0.0\n11\n10.0\n21\n0.0\n"
            "0\nENDBLK\n"
            "0\nENDSEC\n"
            "0\nSECTION\n2\nENTITIES\n"
            // Mirrored, and mirrored with a quarter turn on it.
            "0\nINSERT\n8\n0\n2\npin\n10\n-50.0\n20\n20.0\n230\n-1.0\n"
            "0\nINSERT\n8\n0\n2\npin\n10\n-50.0\n20\n60.0\n50\n90.0\n230\n-1.0\n"
            "0\nLEADER\n8\n0\n3\nEdi\n10\n100.0\n20\n100.0\n10\n90.0\n20\n100.0\n"
            "0\nSOLID\n8\n0\n10\n0.0\n20\n0.0\n11\n1.0\n21\n0.0\n12\n1.0\n22\n1.0\n"
            "13\n0.0\n23\n1.0\n"
            "0\nENDSEC\n0\nEOF\n");
        Document d;
        check(dxf::parse(d, src), "dxf-in: the file is read");

        bool mirrored = false, turned = false;
        for (const auto& e : d.entities()) {
            if (e.type != EntityType::Line) continue;
            // The plain mirror: the lead runs from x=50 back to x=40, on the sheet, not at x=-50.
            if (near(e.a.x, 50.0) && near(e.a.y, 20.0) && near(e.b.x, 40.0) && near(e.b.y, 20.0))
                mirrored = true;
            // And with a quarter turn: mirroring reverses the sense of the turn, so the lead runs UP
            // from where it is placed rather than down.
            if (near(e.a.x, 50.0) && near(e.a.y, 60.0) && near(e.b.x, 50.0) && near(e.b.y, 70.0))
                turned = true;
        }
        check(mirrored, "dxf-in: an object drawn on a plane facing the other way is mirrored onto the "
                        "sheet rather than placed off it");
        check(turned, "dxf-in: and a turn inside a mirrored placement turns the other way");

        const Entity* lead = nullptr;
        const Entity* solid = nullptr;
        for (const auto& e : d.entities()) {
            if (e.type == EntityType::Leader) lead = &e;
            if (e.type == EntityType::Polygon && e.points.size() == 4) solid = &e;
        }
        // The head is no longer an object of its own: a leader IS its line and its head, so the head is
        // drawn from the leader's own arrow size rather than left lying about as a separate triangle for
        // somebody to drag away from the line it belongs to.
        check(lead != nullptr, "dxf-in: a leader is read as a leader");
        if (lead) {
            check(near(lead->points[0].x, 100.0) && near(lead->points[0].y, 100.0),
                  "dxf-in: its first point is the point it picks out - the tip of the arrow");
            check(near(lead->meta.value("arrowSize").toDouble(), 3.0),
                  "dxf-in: and it carries the style's arrow size to draw its head at");
        }
        check(solid && solid->meta.value("filled").toBool(),
              "dxf-in: a SOLID is filled - it is a patch of colour, not an outline of one");
    }

    // ---- a DXF written as BYTES ---------------------------------------------------------------
    {
        // AutoCAD writes a binary DXF whenever it is asked for one: the same drawing and the same group
        // codes, written as bytes rather than as lines. Read as text it is gibberish, and a released
        // drawing that plainly exists would not open at all.
        //
        // The one thing to get right is how wide a group code is. R13 and later write it as TWO bytes;
        // R12 and earlier as one. Guess wrong and every record after the first is read at the wrong
        // offset - a drawing of a hundred thousand records comes out as a handful of nonsense.
        auto binaryDxf = [](bool wideCodes) {
            QByteArray b("AutoCAD Binary DXF\r\n\x1a\0", 22);
            auto code = [&](int c) {
                if (wideCodes) {
                    const quint16 v = static_cast<quint16>(c);
                    b.append(reinterpret_cast<const char*>(&v), 2);
                } else {
                    b.append(static_cast<char>(c));
                }
            };
            auto str = [&](const char* s) { b.append(s); b.append('\0'); };
            auto dbl = [&](double d) { b.append(reinterpret_cast<const char*>(&d), 8); };
            auto i16 = [&](qint16 v) { b.append(reinterpret_cast<const char*>(&v), 2); };
            code(0); str("SECTION");
            code(2); str("ENTITIES");
            code(0); str("LINE");
            code(8); str("WALLS");
            code(62); i16(1);
            code(10); dbl(10.0);
            code(20); dbl(20.0);
            code(11); dbl(110.0);
            code(21); dbl(70.0);
            code(0); str("ENDSEC");
            code(0); str("EOF");
            return b;
        };

        for (const bool wide : {true, false}) {
            const QByteArray bytes = binaryDxf(wide);
            check(dxf::looksBinary(bytes), "dxf-binary: it is known for what it is");
            Document d;
            QString err;
            check(dxf::parseBytes(d, bytes, &err),
                  wide ? "dxf-binary: a file with two-byte group codes reads"
                       : "dxf-binary: and so does one with one-byte codes");
            check(d.entities().size() == 1, "dxf-binary: the line in it comes through");
            if (d.entities().size() == 1) {
                const Entity& e = d.entities().front();
                check(e.type == EntityType::Line && std::abs(e.a.x - 10.0) < 1e-9 &&
                          std::abs(e.b.y - 70.0) < 1e-9,
                      "dxf-binary: with the coordinates it was written with");
                check(e.layer == QLatin1String("WALLS"), "dxf-binary: and on its own layer");
            }
        }

        // Text is still text: the same reader takes either, because which one a file is is the file's
        // business and not the user's.
        Document plain;
        const QByteArray asText = QByteArrayLiteral(
            "0\nSECTION\n2\nENTITIES\n0\nLINE\n8\n0\n10\n1.0\n20\n2.0\n11\n3.0\n21\n4.0\n"
            "0\nENDSEC\n0\nEOF\n");
        check(dxf::parseBytes(plain, asText, nullptr) && plain.entities().size() == 1,
              "dxf-binary: and a text DXF still reads through the same door");
    }

    // ---- an arc is as big as the piece that is drawn --------------------------------------------
    {
        // A drawing package writes an almost-straight segment as an arc of enormous radius sweeping a
        // thousandth of a degree - four of them in one released sheet. Taking the whole circle for those
        // made the drawing's extents 86 metres across, and everything that asks how big a drawing is got
        // the wrong answer: the fit-to-view showed a sheet the size of a full stop.
        Entity nearlyStraight;
        nearlyStraight.type = EntityType::Arc;
        nearlyStraight.center = {86868.0, 2386.0};
        nearlyStraight.radius = 86884.0;
        nearlyStraight.startAngle = 181.5461 * M_PI / 180.0;
        nearlyStraight.endAngle = 181.5465 * M_PI / 180.0;
        const Bounds b = nearlyStraight.bounds();
        check(b.valid && b.width() < 1.0 && b.height() < 1.0,
              "arc bounds: a hair of an arc is a hair wide, not the circle it belongs to");

        Entity quarter;
        quarter.type = EntityType::Arc;
        quarter.center = {0.0, 0.0};
        quarter.radius = 10.0;
        quarter.startAngle = 0.0;
        quarter.endAngle = M_PI / 2.0;
        const Bounds q = quarter.bounds();
        check(std::abs(q.min.x) < 1e-9 && std::abs(q.max.x - 10.0) < 1e-9 &&
                  std::abs(q.min.y) < 1e-9 && std::abs(q.max.y - 10.0) < 1e-9,
              "arc bounds: a quarter is the quarter, corner to corner");

        Entity whole;
        whole.type = EntityType::Arc;
        whole.center = {0.0, 0.0};
        whole.radius = 5.0;
        whole.startAngle = 0.0;
        whole.endAngle = 2.0 * M_PI;
        const Bounds w = whole.bounds();
        check(std::abs(w.min.x + 5.0) < 1e-9 && std::abs(w.max.y - 5.0) < 1e-9,
              "arc bounds: and the whole way round is still the whole circle");
    }

    // ---- the sheet a file states, and where the drawing actually is -----------------------------
    {
        // A released sheet had limits of 12 by 9 - the sheet the file was set up with, years ago - and the
        // drawing itself out at 13 to 42. Taking the limits as the sheet opened the window on an empty
        // patch of paper with the drawing off the screen, which reads exactly like a file that will not
        // open. The limits are the sheet when the drawing is ON them, and what was last saved otherwise.
        const QByteArray away = QByteArrayLiteral(
            "0\nSECTION\n2\nHEADER\n"
            "9\n$LIMMIN\n10\n0.0\n20\n0.0\n"
            "9\n$LIMMAX\n10\n12.0\n20\n9.0\n"
            "9\n$EXTMIN\n10\n13.0\n20\n20.0\n"
            "9\n$EXTMAX\n10\n42.0\n20\n47.0\n"
            "0\nENDSEC\n0\nSECTION\n2\nENTITIES\n"
            "0\nLINE\n8\n0\n10\n13.0\n20\n20.0\n11\n42.0\n21\n47.0\n"
            "0\nENDSEC\n0\nEOF\n");
        Document d;
        check(dxf::parseBytes(d, away, nullptr), "sheet: the file reads");
        const Bounds sheet = d.sheet();
        std::printf("  sheet: %.0f,%.0f .. %.0f,%.0f\n", sheet.min.x, sheet.min.y, sheet.max.x,
                    sheet.max.y);
        check(sheet.valid && sheet.min.x > 12.0,
              "sheet: limits the drawing is nowhere near are not the sheet - what was saved is");

        // And when the drawing IS on the limits, they are the sheet: a drawing keeps working geometry off
        // the sheet, and fitting to every last object of it opens the sheet small and off to one side.
        const QByteArray onIt = QByteArrayLiteral(
            "0\nSECTION\n2\nHEADER\n"
            "9\n$LIMMIN\n10\n0.0\n20\n0.0\n"
            "9\n$LIMMAX\n10\n420.0\n20\n297.0\n"
            "9\n$EXTMIN\n10\n10.0\n20\n10.0\n"
            "9\n$EXTMAX\n10\n400.0\n20\n280.0\n"
            "0\nENDSEC\n0\nSECTION\n2\nENTITIES\n"
            "0\nLINE\n8\n0\n10\n10.0\n20\n10.0\n11\n400.0\n21\n280.0\n"
            "0\nENDSEC\n0\nEOF\n");
        Document d2;
        dxf::parseBytes(d2, onIt, nullptr);
        check(d2.sheet().valid && std::abs(d2.sheet().max.x - 420.0) < 1e-9,
              "sheet: and a drawing on its own sheet keeps that sheet");
    }

    // ---- a polyline that arcs, arcs ---------------------------------------------------------------
    {
        // A vertex can say that the segment LEAVING it bows out into an arc rather than running straight
        // (group 42, the bulge). Reading the points and nothing else drew the straight line between every
        // pair of them, so on a real released sheet a round connector body written as four bulged
        // vertices came out a DIAMOND, a half-round came out a triangle, and a circle written as two
        // 180-degree arcs came out one flat line. Nothing about it looked like a failure to read the
        // file - it looked like the drawing had been made of diamonds.
        //
        // Four quarter-turns is a circle: the bulge of a 90-degree arc is tan(90/4) = 0.4142.
        const QByteArray circle = QByteArrayLiteral(
            "0\nSECTION\n2\nENTITIES\n"
            "0\nLWPOLYLINE\n8\n0\n90\n4\n70\n1\n"
            "10\n-1.0\n20\n0.0\n42\n0.41421356\n"
            "10\n0.0\n20\n-1.0\n42\n0.41421356\n"
            "10\n1.0\n20\n0.0\n42\n0.41421356\n"
            "10\n0.0\n20\n1.0\n42\n0.41421356\n"
            "0\nENDSEC\n0\nEOF\n");
        Document d;
        check(dxf::parseBytes(d, circle, nullptr) && d.entities().size() == 1,
              "bulge: the polyline reads");
        if (d.entities().size() == 1) {
            const Entity& e = d.entities().front();
            const QJsonArray arr = e.meta.value("bulges").toArray();
            check(!arr.isEmpty(), "bulge: the arcs are carried, not dropped");
            // Closed, so the first point is repeated: five points, five bulges.
            check(e.points.size() == 5 && arr.size() == 5,
                  "bulge: a closed shape's last arc has a segment to sit on");
            // Shifted along by one: DXF hangs a bulge on the vertex its segment LEAVES, the drawing
            // side indexes it by the point the segment ARRIVES at. Index 0 is nothing's arrival.
            check(std::abs(arr[0].toDouble()) < 1e-9,
                  "bulge: nothing arrives at the first point");
            bool allFour = true;
            for (int k = 1; k < arr.size(); ++k)
                if (std::abs(arr[k].toDouble() - 0.41421356) > 1e-6) allFour = false;
            check(allFour, "bulge: every one of the four quarter-turns landed on its own segment");

            // And the shape it actually draws as is a CIRCLE of radius 1 - not the diamond through the
            // same four points, whose edges would pass 0.707 from the centre.
            std::vector<double> bulges;
            for (const auto& v : arr) bulges.push_back(v.toDouble());
            const std::vector<Vec2> run = expandBulgePolyline(e.points, bulges);
            check(run.size() > 20, "bulge: it expands into a curve, not four straight edges");
            double lo = 1e9, hi = -1e9;
            for (const Vec2& q : run) {
                const double r = std::hypot(q.x, q.y);
                lo = std::min(lo, r);
                hi = std::max(hi, r);
            }
            std::printf("  bulge: %d points, %.3f..%.3f from the centre\n", int(run.size()), lo, hi);
            check(lo > 0.99 && hi < 1.01,
                  "bulge: every point of it is one radius out - it is the circle, not the diamond");
        }

        // An OPEN run keeps its arcs too, and the bulge hung on the last vertex - the one whose segment
        // does not exist - is simply never reached.
        const QByteArray open = QByteArrayLiteral(
            "0\nSECTION\n2\nENTITIES\n"
            "0\nLWPOLYLINE\n8\n0\n90\n2\n70\n0\n"
            "10\n0.0\n20\n0.0\n42\n1.0\n"
            "10\n2.0\n20\n0.0\n"
            "0\nENDSEC\n0\nEOF\n");
        Document o;
        dxf::parseBytes(o, open, nullptr);
        check(o.entities().size() == 1, "bulge: an open run reads");
        if (o.entities().size() == 1) {
            const QJsonArray arr = o.entities().front().meta.value("bulges").toArray();
            check(arr.size() == 2 && std::abs(arr[1].toDouble() - 1.0) < 1e-9,
                  "bulge: a half-turn on an open run lands on the point it arrives at");
        }

        // TWO vertices are a closed shape once arcs are in play: a circle is very often written as two
        // 180-degree arcs between a pair of opposite points, which is how the round connector bodies on a
        // real released sheet are drawn. The closing point was only ever added ABOVE two vertices - right
        // when the edges are straight, since closing a pair of points with a line only retraces it, and
        // wrong here, because it threw away the arc BACK to the start. The body drew as a half circle.
        const QByteArray twoArcs = QByteArrayLiteral(
            "0\nSECTION\n2\nENTITIES\n"
            "0\nLWPOLYLINE\n8\n0\n90\n2\n70\n1\n"
            "10\n-1.0\n20\n0.0\n42\n1.0\n"
            "10\n1.0\n20\n0.0\n42\n1.0\n"
            "0\nENDSEC\n0\nEOF\n");
        Document t;
        dxf::parseBytes(t, twoArcs, nullptr);
        check(t.entities().size() == 1, "bulge: a two-vertex closed shape reads");
        if (t.entities().size() == 1) {
            const Entity& e = t.entities().front();
            check(e.points.size() == 3, "bulge: it is closed, so the arc back to the start has a segment");
            std::vector<double> bulges;
            for (const auto& v : e.meta.value("bulges").toArray()) bulges.push_back(v.toDouble());
            const std::vector<Vec2> run = expandBulgePolyline(e.points, bulges);
            double lo = 1e9, hi = -1e9;
            for (const Vec2& q : run) {
                const double r = std::hypot(q.x, q.y);
                lo = std::min(lo, r);
                hi = std::max(hi, r);
            }
            std::printf("  bulge: two half-turns -> %d points, %.3f..%.3f out\n", int(run.size()), lo, hi);
            check(run.size() > 20 && lo > 0.99 && hi < 1.01,
                  "bulge: two half-turns make the whole circle, not half of one");
        }

        // A polyline with no bulge at all carries none: a straight run must not be handed an array of
        // zeroes for every renderer and every modify tool to walk.
        const QByteArray straight = QByteArrayLiteral(
            "0\nSECTION\n2\nENTITIES\n"
            "0\nLWPOLYLINE\n8\n0\n90\n2\n70\n0\n"
            "10\n0.0\n20\n0.0\n10\n5.0\n20\n5.0\n"
            "0\nENDSEC\n0\nEOF\n");
        Document s;
        dxf::parseBytes(s, straight, nullptr);
        check(s.entities().size() == 1 &&
                  !s.entities().front().meta.contains(QStringLiteral("bulges")),
              "bulge: a straight run is left straight, with nothing to expand");
    }

    // ---- a spline is a shape, and it was skipped -------------------------------------------------
    {
        // SPLINE was not in the list of things worth reading, so it was dropped outright. On a real sheet
        // that is not a missing line but a missing SHAPE - the ovals round the wire groups simply were not
        // there - and nothing about the drawing said anything had been left out.
        //
        // Evaluated on the way in rather than kept as control points: a DXF spline states its own knot
        // vector and may be rational, and this drawing's own spline is a uniform one, so assuming uniform
        // spacing would draw a different curve, wrong by a different amount for every spline in a file.
        //
        // Degree 3, knots 0,0,0,0,1,1,1,1 - clamped, one span - so the curve must start on the first
        // control point and end on the last, and bow toward the two in between.
        const QByteArray sp = QByteArrayLiteral(
            "0\nSECTION\n2\nENTITIES\n"
            "0\nSPLINE\n8\n0\n70\n8\n71\n3\n72\n8\n73\n4\n74\n0\n"
            "40\n0.0\n40\n0.0\n40\n0.0\n40\n0.0\n40\n1.0\n40\n1.0\n40\n1.0\n40\n1.0\n"
            "10\n0.0\n20\n0.0\n30\n0.0\n"
            "10\n0.0\n20\n10.0\n30\n0.0\n"
            "10\n10.0\n20\n10.0\n30\n0.0\n"
            "10\n10.0\n20\n0.0\n30\n0.0\n"
            "0\nENDSEC\n0\nEOF\n");
        Document d;
        check(dxf::parseBytes(d, sp, nullptr) && d.entities().size() == 1,
              "spline: it is read at all, rather than skipped");
        if (d.entities().size() == 1) {
            const Entity& e = d.entities().front();
            check(e.points.size() > 8, "spline: it arrives as a curve, finely sampled");
            check(std::abs(e.points.front().x) < 1e-6 && std::abs(e.points.front().y) < 1e-6,
                  "spline: a clamped curve starts exactly on its first control point");
            check(std::abs(e.points.back().x - 10.0) < 1e-6 &&
                      std::abs(e.points.back().y) < 1e-6,
                  "spline: and ends exactly on its last");
            // It is pulled TOWARD the middle two control points without reaching them: the highest the
            // curve gets is well above the ends and well below y = 10.
            double topY = -1e9;
            for (const Vec2& q : e.points) topY = std::max(topY, q.y);
            std::printf("  spline: %d points, rises to y = %.3f (control points reach 10)\n",
                        int(e.points.size()), topY);
            check(topY > 3.0 && topY < 9.9,
                  "spline: bowed toward the middle control points, not through them");
        }

        // A spline that says nothing usable about its knots is still drawn - read as the clamped uniform
        // curve anybody would take it for, rather than thrown away for being incomplete.
        const QByteArray noKnots = QByteArrayLiteral(
            "0\nSECTION\n2\nENTITIES\n"
            "0\nSPLINE\n8\n0\n71\n3\n73\n4\n"
            "10\n0.0\n20\n0.0\n10\n0.0\n20\n5.0\n10\n5.0\n20\n5.0\n10\n5.0\n20\n0.0\n"
            "0\nENDSEC\n0\nEOF\n");
        Document k;
        dxf::parseBytes(k, noKnots, nullptr);
        check(k.entities().size() == 1 && k.entities().front().points.size() > 8,
              "spline: one with no knot vector is still read, as the clamped curve it looks like");
    }

    // ---- a turn stated as a direction, and a point pretending to be one --------------------------
    {
        // MTEXT can stand at an angle given as a direction vector (11/21) instead of an angle (50). But
        // plenty of writers put the INSERTION POINT in 11/21 - the very same coordinates as 10/20 - and a
        // point read as a direction is the bearing of the text's own position from the origin of the
        // drawing. On the sheet this came from, that turned 92 labels by 40 to 72 degrees, each by a
        // different amount because each sat somewhere different: "P2" at (16.85, 29.87) came out at 60.58
        // degrees, which is exactly atan2(29.87, 16.85).
        const QByteArray pretend = QByteArrayLiteral(
            "0\nSECTION\n2\nENTITIES\n"
            "0\nMTEXT\n8\n0\n10\n16.847\n20\n29.870\n11\n16.847\n21\n29.870\n"
            "40\n0.2\n1\nP2\n"
            "0\nENDSEC\n0\nEOF\n");
        Document d;
        dxf::parseBytes(d, pretend, nullptr);
        check(d.entities().size() == 1, "text turn: the label reads");
        if (d.entities().size() == 1) {
            std::printf("  text turn: 11/21 = the insertion point -> %.2f deg\n",
                        d.entities().front().rotation * 180.0 / M_PI);
            check(std::abs(d.entities().front().rotation) < 1e-9,
                  "text turn: a vector that IS the insertion point states no direction at all");
        }

        // And one that really does carry a direction is still turned by it.
        const QByteArray sideways = QByteArrayLiteral(
            "0\nSECTION\n2\nENTITIES\n"
            "0\nMTEXT\n8\n0\n10\n21.977\n20\n27.199\n11\n0.0\n21\n-1.0\n"
            "40\n0.086\n1\n28V DC IN\n"
            "0\nENDSEC\n0\nEOF\n");
        Document s;
        dxf::parseBytes(s, sideways, nullptr);
        check(s.entities().size() == 1, "text turn: the turned label reads");
        if (s.entities().size() == 1) {
            const double deg = s.entities().front().rotation * 180.0 / M_PI;
            std::printf("  text turn: a real direction vector (0,-1) -> %.2f deg\n", deg);
            check(std::abs(deg + 90.0) < 1e-6,
                  "text turn: a genuine direction still stands the text on its side");
        }
    }

    // ---- a measurement read as a measurement -----------------------------------------------------
    {
        // Every dimension in a DXF carries BOTH: an anonymous block holding the geometry AutoCAD already
        // drew for it, and the points saying what is being measured. Expanding the block was faithful to
        // the marks on the paper and lost the dimension - a handful of lines, two filled triangles and a
        // piece of text, none of which knows it is a measurement, none of which re-measures when the
        // geometry moves, and every piece of which has to be deleted separately.
        //
        // The sizes matter as much as the geometry. A DIMSTYLE record only writes what it CHANGES, so on a
        // real file the style table is three bare names and the figures live in the header - or, as here,
        // in the dimension's own extended-data overrides, where 1070 names the setting by its variable
        // number and the 1040 after it carries the value.
        const QByteArray dim = QByteArrayLiteral(
            "0\nSECTION\n2\nHEADER\n"
            "9\n$DIMSCALE\n40\n1.0\n"
            "9\n$DIMASZ\n40\n0.18\n"
            "9\n$DIMTXT\n40\n0.18\n"
            "0\nENDSEC\n0\nSECTION\n2\nENTITIES\n"
            "0\nDIMENSION\n8\n0\n70\n32\n3\nEdi\n"
            "13\n10.0\n23\n5.0\n"      // first extension line origin
            "14\n20.0\n24\n5.0\n"      // second
            "10\n20.0\n20\n8.0\n"      // where the dimension line stands off
            "11\n15.0\n21\n8.0\n"      // the text midpoint
            "1\nTBD\n"                 // what it SAYS, in place of the measurement
            "1001\nACAD\n1000\nDSTYLE\n1002\n{\n1070\n140\n1040\n0.12\n1002\n}\n"
            "0\nENDSEC\n0\nEOF\n");
        Document d;
        check(dxf::parseBytes(d, dim, nullptr), "dimension: the file reads");
        check(d.entities().size() == 1, "dimension: it is ONE object, not a pile of lines");
        if (d.entities().size() == 1) {
            const Entity& e = d.entities().front();
            check(e.type == EntityType::Dimension, "dimension: read as a dimension");
            check(e.meta.value("dimType").toString() == QLatin1String("linear"),
                  "dimension: a rotated one measures along an axis");
            // Which way it was PLACED measuring, stated rather than worked out later: the geometry it
            // measures can be dragged, and a dimension that decides afterwards turns round and measures
            // something else the moment that happens.
            check(e.meta.value("dimAcross").toBool(), "dimension: it remembers measuring across x");
            check(std::abs(e.a.x - 10.0) < 1e-9 && std::abs(e.b.x - 20.0) < 1e-9,
                  "dimension: between the two points it was taken between");
            check(e.points.size() == 3 && std::abs(e.points[2].y - 8.0) < 1e-9,
                  "dimension: standing off where the drawing put its line");
            // Six of the nine on the sheet this came from read "TBD", and two more read a round number
            // quite unlike the distance on the paper. A dimension that measured for itself and ignored
            // that would quietly state something the drawing never said.
            check(e.meta.value("textOverride").toString() == QLatin1String("TBD"),
                  "dimension: it says what the drawing says, not what it measures");
            std::printf("  dimension: says %s, text %.3f, arrow %.3f\n",
                        qPrintable(e.meta.value("textOverride").toString()),
                        e.meta.value("textHeight").toDouble(),
                        e.meta.value("arrowSize").toDouble());
            check(std::abs(e.meta.value("textHeight").toDouble() - 0.12) < 1e-9,
                  "dimension: written at the size its own override states");
            check(std::abs(e.meta.value("arrowSize").toDouble() - 0.18) < 1e-9,
                  "dimension: and the arrow at the size the header states, having no override");
        }

        // A dimension taken on a SLANT is not one this drawing can represent - ours measures along one
        // axis or the other - so it must not be read as though it were. Better the baked geometry (or,
        // with no block in the file to bake from, nothing) than a measurement that states a length the
        // drawing never took.
        const QByteArray slant = QByteArrayLiteral(
            "0\nSECTION\n2\nENTITIES\n"
            "0\nDIMENSION\n8\n0\n70\n32\n50\n30.0\n"
            "13\n0.0\n23\n0.0\n14\n10.0\n24\n0.0\n10\n10.0\n20\n4.0\n"
            "0\nENDSEC\n0\nEOF\n");
        Document s;
        dxf::parseBytes(s, slant, nullptr);
        bool anyDim = false;
        for (const Entity& e : s.entities())
            if (e.type == EntityType::Dimension) anyDim = true;
        check(!anyDim, "dimension: one taken on a slant is not passed off as an axis measurement");
    }

    // ---- an arrow and its line are one object ----------------------------------------------------
    {
        // A callout arrived as a polyline plus a separate filled triangle - and a multileader as those
        // plus a separate piece of text. Three things to select, three to move, three to delete, and an
        // arrow left behind after dragging its line away is exactly what gets released by mistake.
        // Grouping them made them move together but gave them no joints, so the arrow could be picked up
        // and still not pointed anywhere. A leader is one OBJECT, held by its tip.
        const QByteArray lead = QByteArrayLiteral(
            "0\nSECTION\n2\nENTITIES\n"
            "0\nLEADER\n8\n0\n3\nEdi\n"
            "10\n1.0\n20\n1.0\n10\n5.0\n20\n5.0\n"
            "0\nENDSEC\n0\nEOF\n");
        Document d;
        dxf::parseBytes(d, lead, nullptr);
        std::printf("  leader: %d object(s)\n", int(d.entities().size()));
        check(d.entities().size() == 1, "leader: one object, not a line and a triangle");
        if (d.entities().size() == 1) {
            const Entity& e = d.entities().front();
            check(e.type == EntityType::Leader, "leader: and it is a leader");
            check(e.points.size() == 2 && near(e.points[0].x, 1.0) && near(e.points[1].x, 5.0),
                  "leader: the tip it points with comes first, then where it runs to");
            check(e.meta.contains(QStringLiteral("arrowSize")),
                  "leader: it carries the size to draw its own head at");
            // The thing the grouping could not give it: joints. One per point, the first being the tip,
            // so the arrow can actually be pointed somewhere.
            check(!e.bounds().valid == false, "leader: it covers ground, so it can be found");
        }
    }

    // ---- a table read as a table ------------------------------------------------------------------
    {
        // Everything needed is in the record: how many rows and columns, the height of each row, the width
        // of each column, and one cell record per cell carrying that cell's words. It was thrown away in
        // favour of expanding the grid AutoCAD baked - which drew the right marks and left no table. On the
        // sheet this was written for that was 197 lines and 243 separate pieces of text in place of four
        // objects: nothing whose cells could be typed into, no rows to add, no columns to widen.
        //
        // Read sequentially, because the header codes and the cell records both use 90..94 for their own
        // purposes and only their position tells them apart.
        const QByteArray tbl = QByteArrayLiteral(
            "0\nSECTION\n2\nENTITIES\n"
            "0\nACAD_TABLE\n8\n0\n2\n*T1\n"
            "10\n5.0\n20\n20.0\n11\n1.0\n21\n0.0\n"
            "91\n2\n92\n3\n"                       // 2 rows, 3 columns
            "141\n1.0\n141\n2.0\n"                 // row heights
            "142\n3.0\n142\n4.0\n142\n5.0\n"       // column widths
            "171\n1\n1\nA1\n"
            "171\n1\n1\nB1\n"
            "171\n1\n1\nC1\n"
            "171\n1\n1\nA2\n"
            "171\n1\n"                             // an empty cell
            "171\n1\n1\nC2\n"
            "0\nENDSEC\n0\nEOF\n");
        Document d;
        check(dxf::parseBytes(d, tbl, nullptr) && d.entities().size() == 1,
              "table: it is ONE object, not a grid of lines and loose texts");
        if (d.entities().size() == 1) {
            const Entity& e = d.entities().front();
            check(e.meta.value("table").toBool(), "table: read as a table");
            check(e.meta.value("rows").toInt() == 2 && e.meta.value("cols").toInt() == 3,
                  "table: with the rows and columns the file states");
            const QJsonArray cells = e.meta.value("cells").toArray();
            check(cells.size() == 6, "table: a cell for every row and column");
            check(cells[0].toString() == QLatin1String("A1") &&
                      cells[2].toString() == QLatin1String("C1") &&
                      cells[5].toString() == QLatin1String("C2"),
                  "table: the words are in the cells they belong to");
            check(cells[4].toString().isEmpty(), "table: and an empty cell stays empty");
            // The insertion point is the TOP-LEFT: columns run right from it, rows down.
            const Bounds b = e.bounds();
            std::printf("  table: %.1f x %.1f at (%.1f,%.1f)\n", b.width(), b.height(), b.min.x,
                        b.min.y);
            check(std::abs(b.width() - 12.0) < 1e-6 && std::abs(b.height() - 3.0) < 1e-6,
                  "table: as wide as its columns and as tall as its rows");
            check(std::abs(b.max.y - 20.0) < 1e-6 && std::abs(b.min.x - 5.0) < 1e-6,
                  "table: hanging down and to the right of where it is placed");
            const QJsonArray colFrac = e.meta.value("colFrac").toArray();
            check(colFrac.size() == 3 && std::abs(colFrac[0].toDouble() - 0.25) < 1e-9,
                  "table: the columns keep their own widths, not an even share");
        }

        // A cell says what is in it in one of TWO ways, and reading only the first left holes all over an
        // imported table. Most cells carry their words as text (code 1). A cell holding a plain value - a
        // quantity, a number - carries it as a VALUE instead, in a block of its own: 301 says CELL_VALUE,
        // 302 is the value, 304 closes it. Of the 204 cells in the bill of materials on the sheet this
        // came from, 166 were text and 33 were values, and the 33 missing ones were the quantities.
        const QByteArray valued = QByteArrayLiteral(
            "0\nSECTION\n2\nENTITIES\n"
            "0\nACAD_TABLE\n8\n0\n2\n*T2\n"
            "10\n0.0\n20\n10.0\n11\n1.0\n21\n0.0\n"
            "91\n1\n92\n2\n141\n1.0\n142\n2.0\n142\n2.0\n"
            "171\n1\n1\nPART\n"                                   // words, as text
            "171\n1\n301\nCELL_VALUE\n302\n4\n304\nACVALUE_END\n"  // a quantity, as a value
            "0\nENDSEC\n0\nEOF\n");
        Document v;
        dxf::parseBytes(v, valued, nullptr);
        check(v.entities().size() == 1, "table: the table reads");
        if (v.entities().size() == 1) {
            const QJsonArray cells = v.entities().front().meta.value("cells").toArray();
            std::printf("  table: cells [%s] [%s]\n",
                        cells.size() > 0 ? qPrintable(cells[0].toString()) : "?",
                        cells.size() > 1 ? qPrintable(cells[1].toString()) : "?");
            check(cells.size() == 2 && cells[0].toString() == QLatin1String("PART"),
                  "table: a cell holding words gives its words");
            check(cells.size() == 2 && cells[1].toString() == QLatin1String("4"),
                  "table: and a cell holding a value gives its value, not a blank");
        }

        // A TURNED table is not something this drawing's table can be, so it is not laid out square and
        // wrong - it keeps the baked grid (and with no block in the file to bake from, nothing).
        const QByteArray turned = QByteArrayLiteral(
            "0\nSECTION\n2\nENTITIES\n"
            "0\nACAD_TABLE\n8\n0\n2\n*T9\n"
            "10\n5.0\n20\n20.0\n11\n0.0\n21\n1.0\n"
            "91\n1\n92\n1\n141\n1.0\n142\n1.0\n171\n1\n1\nX\n"
            "0\nENDSEC\n0\nEOF\n");
        Document t;
        dxf::parseBytes(t, turned, nullptr);
        bool anyTable = false;
        for (const Entity& e : t.entities())
            if (e.meta.value("table").toBool()) anyTable = true;
        check(!anyTable, "table: a turned one is not passed off as a square one");
    }

    // ---- a fill that was thrown away --------------------------------------------------------------
    {
        // HATCH was not in the list of things worth reading, so every fill on a sheet was dropped: a
        // solid pad drawn hollow, a section left blank, and nothing to say it had happened.
        //
        // It is the fiddliest record in a DXF - 10/20 mean a dozen different things depending where in it
        // they appear (a polyline vertex, an arc's centre, an ellipse's axis, a spline's control point, a
        // seed point) - so it is read by walking the codes in order rather than asking for them by name.

        // A solid fill with a POLYLINE boundary: the common case.
        const QByteArray solid = QByteArrayLiteral(
            "0\nSECTION\n2\nENTITIES\n"
            "0\nHATCH\n8\n0\n2\nSOLID\n70\n1\n91\n1\n"
            "92\n3\n"          // external (1) + polyline (2)
            "72\n0\n73\n1\n93\n4\n"
            "10\n0.0\n20\n0.0\n10\n10.0\n20\n0.0\n10\n10.0\n20\n10.0\n10\n0.0\n20\n10.0\n"
            "97\n0\n"
            "0\nENDSEC\n0\nEOF\n");
        Document d;
        check(dxf::parseBytes(d, solid, nullptr) && d.entities().size() == 1,
              "hatch: a solid fill is read");
        if (d.entities().size() == 1) {
            const Entity& e = d.entities().front();
            check(e.type == EntityType::Hatch, "hatch: as a hatch");
            check(e.meta.value("pattern").toString() == QLatin1String("solid"),
                  "hatch: and it knows it is solid, so it is filled rather than ruled");
            const Bounds b = e.bounds();
            std::printf("  hatch: solid, %d pts, %.1f x %.1f\n", int(e.points.size()), b.width(),
                        b.height());
            check(std::abs(b.width() - 10.0) < 1e-6 && std::abs(b.height() - 10.0) < 1e-6,
                  "hatch: covering the ground its boundary encloses");
            // Its EDGE is drawn. A hatch does not normally draw one, because the edge is usually a real
            // object in the same place - but a boundary read out of a file was "derived" from geometry
            // that need not be in the file, and without an edge a round fill came out as a few loose
            // strokes floating in space: a circle with no circle.
            check(e.meta.value("drawBoundary").toBool(),
                  "hatch: an imported fill draws its own edge, having no object to borrow one from");
        }

        // A ruled fill whose boundary is given as EDGES rather than a polyline, with the pattern's angle
        // and scale.
        const QByteArray ruled = QByteArrayLiteral(
            "0\nSECTION\n2\nENTITIES\n"
            "0\nHATCH\n8\n0\n2\nANSI31\n70\n0\n52\n45.0\n41\n2.0\n91\n1\n"
            "92\n1\n93\n3\n"   // external, edge data, three edges
            "72\n1\n10\n0.0\n20\n0.0\n11\n10.0\n21\n0.0\n"
            "72\n1\n10\n10.0\n20\n0.0\n11\n10.0\n21\n10.0\n"
            "72\n1\n10\n10.0\n20\n10.0\n11\n0.0\n21\n0.0\n"
            "97\n0\n"
            "0\nENDSEC\n0\nEOF\n");
        Document r;
        dxf::parseBytes(r, ruled, nullptr);
        check(r.entities().size() == 1, "hatch: one given as edges is read too");
        if (r.entities().size() == 1) {
            const Entity& e = r.entities().front();
            check(e.type == EntityType::Hatch && e.points.size() >= 3,
                  "hatch: the edges close into a boundary");
            check(e.meta.value("pattern").toString() == QLatin1String("ansi31"),
                  "hatch: it keeps the pattern it was drawn with");
            check(std::abs(e.meta.value("hatchAngle").toDouble() - 45.0) < 1e-9 &&
                      std::abs(e.meta.value("hatchScale").toDouble() - 2.0) < 1e-9,
                  "hatch: at the angle and scale the file states");
        }

        // A boundary with an ARC in it comes out curved, not chorded.
        const QByteArray curved = QByteArrayLiteral(
            "0\nSECTION\n2\nENTITIES\n"
            "0\nHATCH\n8\n0\n2\nSOLID\n70\n1\n91\n1\n"
            "92\n1\n93\n2\n"
            "72\n2\n10\n0.0\n20\n0.0\n40\n5.0\n50\n0.0\n51\n180.0\n73\n1\n"
            "72\n1\n10\n-5.0\n20\n0.0\n11\n5.0\n21\n0.0\n"
            "97\n0\n"
            "0\nENDSEC\n0\nEOF\n");
        Document c;
        dxf::parseBytes(c, curved, nullptr);
        check(c.entities().size() == 1, "hatch: a boundary with an arc in it is read");
        if (c.entities().size() == 1) {
            const Entity& e = c.entities().front();
            std::printf("  hatch: half-round boundary, %d pts\n", int(e.points.size()));
            check(e.points.size() > 10, "hatch: the arc is followed round, not cut straight across");
            const Bounds b = e.bounds();
            check(std::abs(b.width() - 10.0) < 0.1 && std::abs(b.height() - 5.0) < 0.1,
                  "hatch: and it covers the half-circle it bounds");
        }
    }

    // ---- the text size a drawing states ----------------------------------------------------------
    {
        // A floor of 0.1 used to be imposed on every imported text height, so that text could never be
        // invisible. On a sheet drawn in millimetres-as-units it silently inflated most of the drawing:
        // a title block's text of 0.05 became 0.1 - twice the size - overflowing every cell it was laid
        // in, and because the wrap box is imported too, forcing extra wrapped lines that pushed the
        // lower rows out of the block. It read as a title block with rows missing.
        const QByteArray small = QByteArrayLiteral(
            "0\nSECTION\n2\nENTITIES\n"
            "0\nTEXT\n8\n0\n10\n1.0\n20\n1.0\n40\n0.0499\n1\nSMALL\n"
            "0\nENDSEC\n0\nEOF\n");
        Document d;
        dxf::parseBytes(d, small, nullptr);
        check(d.entities().size() == 1, "text size: the text reads");
        if (d.entities().size() == 1) {
            const double size = d.entities().front().meta.value("size").toDouble();
            std::printf("  text size: stated 0.0499, imported %.4f\n", size);
            check(std::abs(size - 0.0499) < 1e-9,
                  "text size: the height the file states is the height it gets");
        }
    }

    std::printf(g_fail ? "  -> %d failed\n" : "  -> ok\n", g_fail);
    return g_fail;
}
