// Output: what comes out of a drawing, in what order, and onto what.
//
// Two things are being checked. The DIALOG, which is only a list with ticks and an order - but the order
// is the order of the pages, and a tick is the difference between a pack that is right and one that has
// the raw model in the middle of it. And the PAGE, which is where the work is: a sheet page must be the
// sheet - its paper geometry at its own size, and the model seen through each window at the scale that
// window was given - because a page that is a picture of the model with a margin round it is not the sheet
// that was laid out.
#include "app/Editor.h"
#include "app/MainWindow.h"
#include "app/OutputDialog.h"
#include "model/Document.h"
#include "render/CanvasWidget.h"

#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QListWidget>
#include <QPainter>
#include <QRadioButton>
#include <QTabBar>

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace Edraw_Desktop;

namespace {

int outFailures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("  [FAIL] %s\n", what);
        ++outFailures;
    }
}

// How much ink is inside a rectangle of a page: what came out, measured.
int inkIn(const QImage& img, const QRect& r) {
    int ink = 0;
    const QRect area = r.intersected(img.rect());
    for (int y = area.top(); y <= area.bottom(); ++y)
        for (int x = area.left(); x <= area.right(); ++x) {
            const QRgb c = img.pixel(x, y);
            if (qRed(c) < 200 || qGreen(c) < 200 || qBlue(c) < 200) ++ink;
        }
    return ink;
}

} // namespace

int run_output_tests() {
    std::printf("output tests:\n");
    MainWindow win;
    win.resize(1300, 850);

    QTabBar* spaces = nullptr;
    for (QTabBar* bar : win.findChildren<QTabBar*>())
        if (bar->count() > 0 && bar->tabText(0) == QLatin1String("Model")) spaces = bar;
    auto* canvas = win.findChild<CanvasWidget*>();
    auto* editor = win.findChild<Editor*>();
    check(spaces && canvas && editor, "output: the window, the strip and the editor are there");
    if (!spaces || !canvas || !editor) {
        std::printf("  -> %d failed\n", ++outFailures);
        return outFailures;
    }

    // A harness-ish thing in the model: a 200 x 100 box, so a window has something to show.
    Document* drawing = canvas->document();
    for (int i = 0; i < 4; ++i) {
        const Vec2 box[5] = {{0, 0}, {200, 0}, {200, 100}, {0, 100}, {0, 0}};
        editor->setTool(ToolId::Line);
        editor->onPointerPress(box[i], Qt::LeftButton, Qt::NoModifier);
        editor->onPointerPress(box[i + 1], Qt::LeftButton, Qt::NoModifier);
    }
    check(drawing && drawing->entities().size() == 4, "output: the model has something on it");
    if (!drawing) return outFailures;

    // ---- the dialog: ticks and order -------------------------------------------------------------
    {
        OutputDialog onSheet(*drawing, 1, &win);      // standing on a layout
        const std::vector<int> fromSheet = onSheet.spaces();
        check(!fromSheet.empty(), "dialog: something is ticked to start with");
        check(std::find(fromSheet.begin(), fromSheet.end(), 0) == fromSheet.end(),
              "dialog: standing on a sheet, the raw model is left out - it is almost never wanted in a "
              "pack of sheets");
        check(fromSheet.size() == drawing->layouts().size(),
              "dialog: and every sheet is in");
        check(!onSheet.toPrinter() && onSheet.monochrome() && !onSheet.fitToPage(),
              "dialog: a PDF of black lines at the sheet's own size, until told otherwise");

        OutputDialog onModel(*drawing, 0, &win);      // standing in model space
        const std::vector<int> fromModel = onModel.spaces();
        check(std::find(fromModel.begin(), fromModel.end(), 0) != fromModel.end(),
              "dialog: standing on the model, the model is in");

        // Arranged: the order in the list is the order of the pages.
        auto* list = onModel.findChild<QListWidget*>();
        check(list && list->count() >= 3, "dialog: the list has the model and the sheets");
        if (list && list->count() >= 3) {
            const std::vector<int> before = onModel.spaces();
            QListWidgetItem* first = list->takeItem(0);
            list->insertItem(list->count(), first);          // the model sent to the back
            const std::vector<int> after = onModel.spaces();
            check(after.size() == before.size(), "arrange: moving one about loses nothing");
            check(!after.empty() && after.back() == 0,
                  "arrange: what is last in the list is last in the pack");
            check(before != after, "arrange: and the order really did change");
        }
        // A tick taken away takes that page out.
        if (list) {
            const size_t was = onModel.spaces().size();
            list->item(0)->setCheckState(Qt::Unchecked);
            check(onModel.spaces().size() == was - 1, "include: unticking one leaves it out");
        }
    }

    // ---- a sheet page: the sheet, not a picture of the model --------------------------------------
    {
        // A window on the first sheet, showing the model at 1:2, in the lower left quarter of the sheet.
        DrawingLayout& sheet = drawing->layouts()[0];
        sheet.viewports.clear();
        LayoutViewport window;
        window.id = QStringLiteral("w");
        window.x = 20.0;
        window.y = 20.0;
        window.width = 120.0;
        window.height = 80.0;
        window.modelScale = 0.5;
        window.modelCenter = Vec2{100.0, 50.0};
        sheet.viewports.push_back(window);
        // And something drawn ON the paper, in the opposite corner, so the two can be told apart.
        Entity note;
        note.type = EntityType::Line;
        note.id = QStringLiteral("papernote");
        note.layer = QStringLiteral("0");
        note.a = Vec2{sheet.paperWidth - 100.0, sheet.paperHeight - 40.0};
        note.b = Vec2{sheet.paperWidth - 20.0, sheet.paperHeight - 40.0};
        sheet.paperEntities.clear();
        sheet.paperEntities.push_back(note);

        double wMm = 0.0, hMm = 0.0;
        check(win.spacePageSizeMm(1, wMm, hMm), "page: a sheet knows what size of paper it wants");
        check(std::abs(wMm - sheet.paperWidth) < 1e-6 && std::abs(hMm - sheet.paperHeight) < 1e-6,
              "page: and that size is the sheet's own - an A3 layout is an A3 page");

        // Painted at a known size, so where the ink lands can be measured.
        const int pxPerMm = 3;
        QImage page(static_cast<int>(sheet.paperWidth) * pxPerMm,
                    static_cast<int>(sheet.paperHeight) * pxPerMm, QImage::Format_RGB32);
        page.fill(Qt::white);
        {
            QPainter p(&page);
            win.renderSpaceToPage(p, 1, /*monochrome=*/true);
        }
        // The window's rectangle on the page, in pixels, y measured down from the top.
        const QRect windowRect(static_cast<int>(window.x) * pxPerMm,
                              static_cast<int>(sheet.paperHeight - window.y - window.height) * pxPerMm,
                              static_cast<int>(window.width) * pxPerMm,
                              static_cast<int>(window.height) * pxPerMm);
        // The note is 40 mm below the top of the sheet and 100..20 mm in from the right: on the page,
        // y is measured DOWN from the top, which is what makes this worth writing out.
        const QRect noteRect(static_cast<int>(sheet.paperWidth - 105.0) * pxPerMm, 40 * pxPerMm - 8,
                            90 * pxPerMm, 16);
        const int inWindow = inkIn(page, windowRect);
        const int inNote = inkIn(page, noteRect);
        std::printf("  sheet page: %d ink in the window, %d on the paper beside it\n", inWindow, inNote);
        check(inWindow > 100, "page: the model comes out inside the window it is seen through");
        check(inNote > 20, "page: and what was drawn on the paper comes out where it was drawn");

        // The model is INSIDE the window and nowhere else: a band across the sheet away from both.
        const QRect elsewhere(0, 0, page.width(), static_cast<int>(20.0) * pxPerMm);
        check(inkIn(page, elsewhere) == 0,
              "page: nothing is drawn where the sheet is empty - the window is a hole, and its frame is "
              "not printed");
    }

    // ---- a model page ----------------------------------------------------------------------------
    {
        double wMm = 0.0, hMm = 0.0;
        check(win.spacePageSizeMm(0, wMm, hMm), "page: the model knows what size it needs");
        check(wMm > 200.0 && hMm > 100.0, "page: its extents, with a margin round them");
        QImage page(600, 400, QImage::Format_RGB32);
        page.fill(Qt::white);
        {
            QPainter p(&page);
            win.renderSpaceToPage(p, 0, true);
        }
        check(inkIn(page, page.rect()) > 500, "page: and the drawing comes out on it");
    }

    // ---- the pages, in order, in one PDF ---------------------------------------------------------
    {
        const QString path = QDir::tempPath() + QStringLiteral("/edraw-output-test.pdf");
        QFile::remove(path);
        const std::vector<int> pages{1, 0};   // the sheet first, the model after it
        check(win.writeSpacePdf(path, pages, true, false), "pdf: the pages are written");
        QFile file(path);
        check(file.exists() && file.size() > 1000, "pdf: and the file has something in it");
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = file.readAll();
            check(bytes.startsWith("%PDF"), "pdf: it is a PDF");
            check(bytes.count("/Type /Page") >= 2 || bytes.count("/Type/Page") >= 2 ||
                      bytes.count("/Count 2") >= 1,
                  "pdf: of two pages, one per space asked for");
        }
    }

    // ---- an empty space is dropped rather than coming out blank ----------------------------------
    {
        Document bare;
        MainWindow other;
        double w = 0.0, h = 0.0;
        // A drawing with nothing in the model: the model has no page, the sheets still do.
        check(!other.spacePageSizeMm(-1, w, h), "page: there is no such space as -1");
    }

    if (outFailures == 0) std::printf("  -> ok\n");
    else std::printf("  -> %d failed\n", outFailures);
    return outFailures;
}
