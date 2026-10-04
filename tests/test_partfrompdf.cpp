#include "app/Editor.h"
#include "app/PartFromPdfDialog.h"
#include "app/PartsLibraryDialog.h"
#include "harness/Harness.h"
#include "harness/PartFromPdf.h"
#include "harness/PartsLibrary.h"
#include "model/Document.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QPushButton>

#include <cmath>
#include <cstdio>

using namespace Edraw_Desktop;

namespace {
int g_fail = 0;
void check(bool ok, const char* what) {
    if (!ok) { std::printf("  [FAIL] %s\n", what); ++g_fail; }
}

Entity text(const QString& s, double x, double y, double size = 8.0) {
    Entity e;
    e.type = EntityType::Text;
    e.a = {x, y};
    e.meta.insert(QStringLiteral("text"), s);
    e.meta.insert(QStringLiteral("size"), size);
    return e;
}

Entity line(double x0, double y0, double x1, double y1) {
    Entity e;
    e.type = EntityType::Line;
    e.a = {x0, y0};
    e.b = {x1, y1};
    e.color = QStringLiteral("#000000");
    return e;
}

QString said(const std::vector<PdfPin>& pins) {
    QStringList out;
    for (const PdfPin& p : pins) out << pinLabelFor(p);
    return out.join(QStringLiteral(" | "));
}

// A one-page PDF: a 100 x 50 pt box (the part's outline) low on the page, and a pin table of three rows
// with a heading above it, the pins in one column and their signals in the next.
QByteArray samplePdf() {
    const QByteArray content =
        "100 100 m 200 100 l 200 150 l 100 150 l h S "
        "BT /F1 9 Tf 100 700 Td (Pin) Tj ET BT /F1 9 Tf 140 700 Td (Signal) Tj ET "
        "BT /F1 9 Tf 100 685 Td (1) Tj ET BT /F1 9 Tf 140 685 Td (VBAT) Tj ET "
        "BT /F1 9 Tf 100 670 Td (2) Tj ET BT /F1 9 Tf 140 670 Td (GND) Tj ET "
        "BT /F1 9 Tf 100 655 Td (3) Tj ET BT /F1 9 Tf 140 655 Td (CAN_H) Tj ET "
        "BT /F1 9 Tf 300 400 Td (25) Tj ET";
    auto obj = [](int n, const QByteArray& dict, const QByteArray& stream = QByteArray()) {
        QByteArray o = QByteArray::number(n) + " 0 obj\n" + dict + "\n";
        if (!stream.isNull()) o += "stream\n" + stream + "\nendstream\n";
        return o + "endobj\n";
    };
    QByteArray bytes = "%PDF-1.5\n";
    bytes += obj(1, "<</Type/Catalog/Pages 2 0 R>>");
    bytes += obj(2, "<</Type/Pages/Kids[3 0 R]/Count 1>>");
    bytes += obj(3, "<</Type/Page/Parent 2 0 R/MediaBox[0 0 612 792]/Contents 4 0 R"
                    "/Resources<</Font<</F1 5 0 R>>>>>>");
    bytes += obj(4, "<</Length " + QByteArray::number(content.size()) + ">>", content);
    bytes += obj(5, "<</Type/Font/Subtype/Type1/BaseFont/Helvetica>>");
    bytes += "trailer <</Root 1 0 R>>\n%%EOF\n";
    return bytes;
}
} // namespace

int run_partfrompdf_tests() {
    g_fail = 0;
    std::printf("Part from PDF tests\n");

    // ---- THE PIN TABLE, from placed text ---------------------------------------------------------------
    {
        // A table of four pins under its heading, carried on in a second column (5, 6) to its right; a
        // dimension and a stray "3" elsewhere on the page that belong to no table.
        std::vector<Entity> ents = {
            text(QStringLiteral("Pin"), 100, 700),  text(QStringLiteral("Function"), 130, 700),
            text(QStringLiteral("1"), 100, 685),    text(QStringLiteral("+12V"), 130, 685),
            text(QStringLiteral("2"), 100, 670),    text(QStringLiteral("GND"), 130, 670),
            text(QStringLiteral("3"), 100, 655),    text(QStringLiteral("CAN_H"), 130, 655),
            text(QStringLiteral("4"), 100, 640),    text(QStringLiteral("CAN_L"), 130, 640),
            text(QStringLiteral("5"), 250, 685),    text(QStringLiteral("LIN"), 280, 685),
            text(QStringLiteral("6"), 250, 670),    text(QStringLiteral("WAKE"), 280, 670),
            text(QStringLiteral("12.5"), 400, 300), text(QStringLiteral("3"), 450, 200),
        };
        const std::vector<PdfPin> got = pinoutFromText(ents);
        std::printf("  pinout: %s\n", qPrintable(said(got)));
        check(got.size() == 6, "pinout: every pin of the table, both its columns");
        check(said(got) == QStringLiteral("1 +12V | 2 GND | 3 CAN_H | 4 CAN_L | 5 LIN | 6 WAKE"),
              "pinout: each pin with the signal beside it, in order");

        // Only what is in a box, when one is given: the second column left out.
        Bounds box;
        box.expand(Vec2{90, 630});
        box.expand(Vec2{200, 710});
        check(pinoutFromText(ents, &box).size() == 4, "pinout: read from a box, only the pins inside it");

        // A pin and its signal set in one run of text, and pins in letters.
        const std::vector<Entity> runs = {text(QStringLiteral("Pin 1 Shield"), 50, 500),
                                          text(QStringLiteral("Pin 2 Data+"), 50, 488),
                                          text(QStringLiteral("Pin 3 Data-"), 50, 476)};
        check(said(pinoutFromText(runs)) == QStringLiteral("1 Shield | 2 Data+ | 3 Data-"),
              "pinout: a pin and its signal in one run of text are split where the pin ends");
        const std::vector<Entity> letters = {text(QStringLiteral("A"), 10, 300), text(QStringLiteral("PWR"), 30, 300),
                                             text(QStringLiteral("B"), 10, 288), text(QStringLiteral("RTN"), 30, 288),
                                             text(QStringLiteral("C"), 10, 276), text(QStringLiteral("SIG"), 30, 276)};
        check(said(pinoutFromText(letters)) == QStringLiteral("A PWR | B RTN | C SIG"), "pinout: pins in letters");
        // Numbers with nothing to count from one - a dimension list - are no pinout.
        check(pinoutFromText({text(QStringLiteral("7"), 0, 100), text(QStringLiteral("8"), 0, 88)}).empty(),
              "pinout: a column that does not count from the first pin is not a pin table");
    }

    // ---- THE VIEW IN THE BOX, at the width it is given --------------------------------------------------
    {
        const std::vector<Entity> page = {line(100, 100, 200, 100), line(200, 100, 200, 150),
                                          line(500, 500, 600, 500), text(QStringLiteral("100"), 150, 120)};
        Bounds box;
        box.expand(Vec2{90, 90});
        box.expand(Vec2{210, 160});
        const std::vector<Entity> in = entitiesInBox(page, box, false);
        check(in.size() == 2, "box: what lies wholly inside it, and not its text unless asked");
        check(entitiesInBox(page, box, true).size() == 3, "box: and the text too, when asked");
        const std::vector<Entity> drawn = partDrawingEntities(partDrawingFromPdf(in, 50.0));
        Bounds b;
        for (const Entity& e : drawn) b.expand(e.bounds());
        check(b.valid && std::abs(b.width() - 50.0) < 1e-6 && std::abs(b.height() - 25.0) < 1e-6,
              "drawing: scaled to the width given, in proportion");
        check(b.valid && std::abs(b.center().x) < 1e-6 && std::abs(b.center().y) < 1e-6,
              "drawing: centred on the origin");
        bool layerColour = true;
        for (const Entity& e : drawn) layerColour = layerColour && e.color.isEmpty();
        check(layerColour, "drawing: in the layer's colour, not the datasheet's black");
        check(std::abs(pdfPointsToMm(72.0) - 25.4) < 1e-9, "drawing: a PDF's inch is 25.4 mm");
    }

    // ---- THE PINOUT ON THE PART, and the symbol built from it -------------------------------------------
    {
        LibraryPart part;
        part.id = QStringLiteral("P-PDF");
        part.kind = PartKind::Connector;
        setPartPinout(part, {{QStringLiteral("1"), QStringLiteral("VBAT")},
                             {QStringLiteral("2"), QStringLiteral("GND")},
                             {QStringLiteral("3"), QString()}});
        check(part.wayCount() == 3, "part: the pinout gives the part its pin count");
        check(said(partPinout(part)) == QStringLiteral("1 VBAT | 2 GND | 3"), "part: the pinout is kept, and read back");
        const QJsonArray sym = connectorSymbolFromData(part);
        const std::vector<Entity> ents = partDrawingEntities(sym);
        const ConnectorSpec spec = ents.empty() ? ConnectorSpec{} : connectorSpecFromMeta(ents.front().meta);
        std::printf("  symbol pins: %s\n", qPrintable(spec.pins.join(QStringLiteral(" | "))));
        check(spec.pins == QStringList({QStringLiteral("1 VBAT"), QStringLiteral("2 GND"), QStringLiteral("3")}),
              "symbol: each pin labelled with its signal beside its number");
        setPartPinout(part, {});
        check(part.value(QStringLiteral("pinout")).isEmpty(), "part: an empty pinout takes the pinout off");
    }

    // ---- THE DIALOG, on a real PDF ------------------------------------------------------------------
    const QString pdfPath = QDir(QDir::tempPath()).filePath(QStringLiteral("edraw_part_from_pdf.pdf"));
    {
        QFile f(pdfPath);
        check(f.open(QIODevice::WriteOnly) && f.write(samplePdf()) > 0, "dialog: a datasheet PDF written");
    }
    {
        PartFromPdfDialog dlg(pdfPath, QStringLiteral("TEST-1"));
        dlg.show();
        QApplication::processEvents();
        check(dlg.loaded() && dlg.pageCount() == 1, "dialog: the PDF read, one page");
        std::printf("  dialog pinout: %s\n", qPrintable(said(dlg.pinout())));
        check(said(dlg.pinout()) == QStringLiteral("1 VBAT | 2 GND | 3 CAN_H") && dlg.takePinout(),
              "dialog: the pin table found by itself, and ticked to be taken");
        check(!dlg.takeDrawing(), "dialog: no drawing to take before a box is drawn");
        Bounds box;
        box.expand(Vec2{90, 90});
        box.expand(Vec2{210, 160});
        dlg.setBox(box);
        Bounds b;
        for (const Entity& e : partDrawingEntities(dlg.drawing())) b.expand(e.bounds());
        std::printf("  dialog drawing: %.2f x %.2f mm\n", b.width(), b.height());
        check(dlg.takeDrawing() && b.valid && std::abs(b.width() - pdfPointsToMm(100.0)) < 0.05,
              "dialog: the boxed outline taken, at the size the page prints it");
        dlg.setWidthMm(40.0);
        b = Bounds{};
        for (const Entity& e : partDrawingEntities(dlg.drawing())) b.expand(e.bounds());
        check(b.valid && std::abs(b.width() - 40.0) < 1e-6 && std::abs(b.height() - 20.0) < 1e-6,
              "dialog: at the width typed, in proportion");
    }
    {
        PartFromPdfDialog bad(QDir(QDir::tempPath()).filePath(QStringLiteral("edraw_no_such.pdf")),
                              QStringLiteral("NONE"));
        check(!bad.loaded() && !bad.error().isEmpty(), "dialog: a PDF that cannot be read says why");
    }

    // ---- INTO THE LIBRARY ---------------------------------------------------------------------------
    {
        const QString libFile = QDir(QDir::tempPath()).filePath(QStringLiteral("edraw_part_from_pdf_lib.json"));
        QFile::remove(libFile);
        {
            PartsLibrary lib;
            LibraryPart part;
            part.id = QStringLiteral("P-FROMPDF");
            part.kind = PartKind::Connector;
            part.mfrPn = QStringLiteral("TEST-1");
            lib.parts.push_back(part);
            check(lib.save(libFile), "library: a connector with nothing drawn written");
        }
        Document doc;
        Editor ed;
        ed.setDocument(&doc);
        PartsLibraryDialog dlg(&ed);
        dlg.setLibraryPath(libFile);
        dlg.selectPart(QStringLiteral("P-FROMPDF"));
        dlg.resize(1600, 900);
        dlg.show();
        QApplication::processEvents();
        check(dlg.findChild<QPushButton*>(QStringLiteral("partFromPdf")) != nullptr,
              "library: a From PDF button on the mechanical drawing");
        const std::vector<Entity> outline = {line(0, 0, 100, 0), line(100, 0, 100, 50)};
        dlg.applyFromPdf(partDrawingFromPdf(outline, 30.0),
                         {{QStringLiteral("1"), QStringLiteral("VBAT")}, {QStringLiteral("2"), QStringLiteral("GND")}},
                         false);
        const LibraryPart* p = dlg.catalog().byId(QStringLiteral("P-FROMPDF"));
        check(p && p->hasMechanical(), "library: the drawing taken as the part's mechanical drawing");
        check(p && p->wayCount() == 2 && partPinout(*p).size() == 2, "library: the pinout taken, and the pin count");
        QStringList labels;
        if (p && p->hasSchematic()) {
            const std::vector<Entity> ents = partDrawingEntities(p->schematic);
            if (!ents.empty()) labels = connectorSpecFromMeta(ents.front().meta).pins;
        }
        check(labels == QStringList({QStringLiteral("1 VBAT"), QStringLiteral("2 GND")}),
              "library: the connector's symbol built with its pins labelled");
        QFile::remove(libFile);
    }
    QFile::remove(pdfPath);

    std::printf(g_fail ? "  -> %d failed\n" : "  -> ok\n", g_fail);
    return g_fail;
}
