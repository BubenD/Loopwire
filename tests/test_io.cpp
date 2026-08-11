#include "io/Dwx.h"
#include "io/Dxf.h"
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

        // The callout: the word, and the line that points with it.
        bool haveLabel = false, haveLeader = false;
        for (const auto& e : d.entities()) {
            if (e.type == EntityType::Text && e.meta.value("text").toString() == QLatin1String("LABEL"))
                haveLabel = true;
            if (e.type == EntityType::Polyline && e.points.size() == 2 &&
                near(e.points.front().x, 40.0) && near(e.points.back().x, 60.0))
                haveLeader = true;
        }
        check(haveLabel, "dxf-in: a multileader's words are read");
        check(haveLeader, "dxf-in: and the leader runs from its vertex to where the words sit");
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

        const Entity* head = nullptr;
        const Entity* solid = nullptr;
        for (const auto& e : d.entities()) {
            if (e.type != EntityType::Polygon) continue;
            if (e.points.size() == 3) head = &e;
            if (e.points.size() == 4) solid = &e;
        }
        check(head != nullptr, "dxf-in: a leader is given the arrowhead it points with");
        if (head) {
            check(head->meta.value("filled").toBool(), "dxf-in: and the head is filled, not outlined");
            check(near(head->points[0].x, 100.0) && near(head->points[0].y, 100.0),
                  "dxf-in: its tip is the point the leader picks out");
            // 3.0 long (the style's arrow size), a third of that across.
            check(near(head->points[1].x, 97.0) && near(head->points[2].x, 97.0) &&
                      near(head->points[1].y, 100.5) && near(head->points[2].y, 99.5),
                  "dxf-in: and it lies back along the leader, as long as the style's arrow size");
        }
        check(solid && solid->meta.value("filled").toBool(),
              "dxf-in: a SOLID is filled - it is a patch of colour, not an outline of one");
    }

    std::printf(g_fail ? "  -> %d failed\n" : "  -> ok\n", g_fail);
    return g_fail;
}
