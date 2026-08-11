// Page setup: the paper a sheet plots on, and everything that follows from it.
//
// The one that matters is FULL BLEED. A plain paper size keeps a margin the printer cannot reach, so a
// 420 x 297 sheet plotted on plain A3 must shrink to fit inside it; a full-bleed size has no margin, and
// the sheet comes out at true size. Both are offered, and the difference has to be real in what comes out
// of the plot - not just a word on a list - which is what the last block here measures.
#include "app/MainWindow.h"
#include "app/PageSetupDialog.h"
#include "app/PageSetupManagerDialog.h"
#include "io/Dwx.h"
#include "model/Document.h"
#include "model/PageSetup.h"
#include "render/CanvasWidget.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QMenu>
#include <QMouseEvent>
#include <QDir>
#include <QImage>
#include <QListWidget>
#include <QPainter>
#include <QPushButton>
#include <QTabBar>

#include <cmath>
#include <cstdio>

using namespace Edraw_Desktop;

namespace {

int setupFailures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("  [FAIL] %s\n", what);
        ++setupFailures;
    }
}

bool near(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }

// Ink in a strip of a page: used to see whether a plot reaches the paper's edge or stops at a margin.
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

// A sheet with a border drawn hard against its own edges, so where the border comes out on the page says
// exactly how much of the paper was used.
void borderTheSheet(DrawingLayout& layout) {
    layout.paperEntities.clear();
    const Vec2 corners[5] = {{0.0, 0.0},
                             {layout.paperWidth, 0.0},
                             {layout.paperWidth, layout.paperHeight},
                             {0.0, layout.paperHeight},
                             {0.0, 0.0}};
    for (int i = 0; i < 4; ++i) {
        Entity e;
        e.type = EntityType::Line;
        e.id = QStringLiteral("edge%1").arg(i);
        e.layer = QStringLiteral("0");
        e.a = corners[i];
        e.b = corners[i + 1];
        layout.paperEntities.push_back(e);
    }
}

} // namespace

int run_pagesetup_tests() {
    std::printf("page setup tests:\n");

    // ---- the paper list ---------------------------------------------------------------------------
    {
        const std::vector<PaperSize>& all = paperSizes();
        check(all.size() > 40, "paper: every size a drawing office asks for is on the list");
        int bleed = 0, plain = 0;
        bool a3 = false, a3Bleed = false, ansiE = false, ansiEBleed = false;
        for (const PaperSize& size : all) {
            (size.marginMm <= 0.0 ? bleed : plain)++;
            if (size.name == QStringLiteral("ISO A3 (420.00 x 297.00 MM)")) a3 = true;
            if (size.name == QStringLiteral("ISO full bleed A3 (420.00 x 297.00 MM)")) a3Bleed = true;
            if (size.name.startsWith(QStringLiteral("ANSI E"))) ansiE = true;
            if (size.name.startsWith(QStringLiteral("ANSI full bleed E"))) ansiEBleed = true;
        }
        check(a3 && a3Bleed, "paper: A3 is there both ways - plain, and full bleed");
        check(ansiE && ansiEBleed, "paper: and so is ANSI E, so an imperial office is served too");
        check(bleed == plain, "paper: every size has its full-bleed twin, and no twin is missing");
        for (const PaperSize& size : all) {
            if (size.marginMm <= 0.0) continue;
            check(size.marginMm > 0.5, "paper: a plain size keeps a real margin");
        }
        check(paperSizeByName(QStringLiteral("ISO full bleed A3 (420.00 x 297.00 MM)")).marginMm == 0.0,
              "paper: full bleed means no margin at all - the printable area IS the page");
        check(paperSizeByName(QStringLiteral("nothing of the sort")).widthMm > 0.0,
              "paper: an unknown name falls back to something plottable rather than nothing");
        check(paperSizeFor(420.0, 297.0, true)
                  .contains(QStringLiteral("full bleed A3")),
              "paper: a 420 x 297 sheet asks for full-bleed A3, which holds it at true size");
        // On plain paper the same sheet needs a bigger size, because A3's printable area is only
        // 410 x 287. Which bigger one is the smallest that holds it - B3, as it happens - is the
        // arithmetic's business; what matters is that it holds it and that it is not A3.
        const PaperSize& plainPick = paperSizeByName(paperSizeFor(420.0, 297.0, false));
        const double holdW = std::max(plainPick.widthMm, plainPick.heightMm) - 2.0 * plainPick.marginMm;
        const double holdH = std::min(plainPick.widthMm, plainPick.heightMm) - 2.0 * plainPick.marginMm;
        std::printf("  a 420 x 297 sheet on plain paper: %s\n", plainPick.name.toUtf8().constData());
        check(holdW >= 420.0 - 0.5 && holdH >= 297.0 - 0.5,
              "paper: the plain size chosen can actually print the whole sheet");
        check(!plainPick.name.contains(QStringLiteral(" A3 ")),
              "paper: and it is not A3 - its margin will not hold a 420 x 297 sheet, which is the whole "
              "reason full bleed is offered");
    }

    // ---- what a setup works out ------------------------------------------------------------------
    {
        PageSetup setup;
        setup.paper = QStringLiteral("ISO A4 (297.00 x 210.00 MM)");
        setup.landscape = true;
        check(near(setup.pageWidthMm(), 297.0) && near(setup.pageHeightMm(), 210.0),
              "setup: landscape puts the long edge across");
        setup.landscape = false;
        check(near(setup.pageWidthMm(), 210.0) && near(setup.pageHeightMm(), 297.0),
              "setup: and portrait puts it up");
        check(near(setup.printableWidthMm(), 200.0) && near(setup.printableHeightMm(), 287.0),
              "setup: the printable area is the page less its margin, both sides");
        setup.paper = QStringLiteral("ISO full bleed A4 (297.00 x 210.00 MM)");
        check(near(setup.printableWidthMm(), setup.pageWidthMm()),
              "setup: full bleed prints on all of it");
        setup.fitToPaper = false;
        setup.scale = 0.5;
        check(setup.scaleLabel() == QStringLiteral("1:2"), "setup: a scale says what it is");
        setup.fitToPaper = true;
        check(setup.scaleLabel() == QStringLiteral("Fit to paper"), "setup: and so does fitting");
    }

    // ---- saved with the drawing ------------------------------------------------------------------
    {
        Document doc;
        check(!doc.layouts().empty(), "file: a drawing has a sheet to set up");
        DrawingLayout& sheet = doc.layouts()[0];
        sheet.pageSetup.paper = QStringLiteral("ISO A2 (594.00 x 420.00 MM)");
        sheet.pageSetup.landscape = false;
        sheet.pageSetup.scale = 0.25;
        sheet.pageSetup.fitToPaper = false;
        sheet.pageSetup.centred = false;
        sheet.pageSetup.offsetXMm = 12.5;
        sheet.pageSetup.offsetYMm = -3.5;
        sheet.pageSetup.monochrome = false;
        sheet.pageSetup.plotLineweights = false;
        sheet.pageSetup.device = QStringLiteral("A plotter in the corner");
        PageSetup named;
        named.name = QStringLiteral("Check print");
        named.paper = QStringLiteral("ISO full bleed A1 (841.00 x 594.00 MM)");
        named.fitToPaper = true;
        doc.pageSetups().push_back(named);

        const QString path = QDir::tempPath() + QStringLiteral("/edraw-pagesetup.dwx");
        QString err;
        check(dwx::save(doc, path, &err), "file: the drawing saves");
        Document back;
        check(dwx::load(back, path, &err), "file: and loads");
        if (!back.layouts().empty()) {
            const PageSetup& got = back.layouts()[0].pageSetup;
            check(got.paper == sheet.pageSetup.paper && got.landscape == false &&
                      near(got.scale, 0.25) && got.centred == false && near(got.offsetXMm, 12.5) &&
                      near(got.offsetYMm, -3.5) && got.monochrome == false &&
                      got.plotLineweights == false && got.device == sheet.pageSetup.device,
                  "file: the sheet's page setup comes back exactly as it was set - a sheet that plots "
                  "right today plots right for whoever opens the file next");
        }
        check(back.pageSetups().size() == 1 &&
                  back.pageSetups()[0].name == QStringLiteral("Check print") &&
                  back.pageSetups()[0].fitToPaper,
              "file: and the drawing's named setups come back too");

        // A drawing written before page setups existed: it plotted at its sheet's own size, so that is
        // what it must come back as - nothing about it changes by being written down.
        QJsonObject old = dwx::toJson(doc);
        QJsonArray layouts = old.value(QStringLiteral("layouts")).toArray();
        QJsonObject first = layouts[0].toObject();
        first.remove(QStringLiteral("pageSetup"));
        first[QStringLiteral("paperWidth")] = 594.0;
        first[QStringLiteral("paperHeight")] = 420.0;
        layouts[0] = first;
        old[QStringLiteral("layouts")] = layouts;
        Document older;
        dwx::fromJson(older, old);
        check(!older.layouts().empty() &&
                  older.layouts()[0].pageSetup.paper.contains(QStringLiteral("full bleed")) &&
                  near(older.layouts()[0].pageSetup.pageWidthMm(), 594.0) &&
                  near(older.layouts()[0].pageSetup.scale, 1.0),
              "file: an older drawing gets its own sheet size, full bleed, at 1:1 - which is how it was "
              "being plotted all along");
    }

    // ---- the dialogs -----------------------------------------------------------------------------
    {
        PageSetup setup;
        setup.paper = QStringLiteral("ISO A2 (594.00 x 420.00 MM)");
        setup.landscape = false;
        setup.scale = 0.1;
        setup.monochrome = false;
        PageSetupDialog dlg(setup, QStringLiteral("Layout1"));
        const PageSetup out = dlg.setup();
        check(out.paper == setup.paper && out.landscape == false && near(out.scale, 0.1) &&
                  out.monochrome == false,
              "dialog: what it opens on is what it gives back when nothing is touched");
        // The paper list holds each size ONCE - the plain one - and a tick turns whichever is chosen
        // into its full-bleed twin. Every size listed twice over is where the two get confused.
        int plainCount = 0;
        for (const PaperSize& size : paperSizes())
            if (size.marginMm > 0.0) ++plainCount;
        QComboBox* papers = nullptr;
        for (QComboBox* box : dlg.findChildren<QComboBox*>())
            if (box->count() == plainCount) papers = box;
        check(papers != nullptr, "dialog: every size is on the paper list, once each");
        QCheckBox* bleed = nullptr;
        for (QCheckBox* box : dlg.findChildren<QCheckBox*>())
            if (box->text().contains(QStringLiteral("full bleed"))) bleed = box;
        check(bleed != nullptr, "dialog: and a tick for borderless, full bleed to the edge");
        if (papers && bleed) {
            const int a3 = papers->findData(QStringLiteral("ISO A3 (420.00 x 297.00 MM)"));
            check(a3 >= 0, "dialog: A3 can be picked");
            papers->setCurrentIndex(a3);
            bleed->setChecked(false);
            check(dlg.setup().paper == QStringLiteral("ISO A3 (420.00 x 297.00 MM)") &&
                      dlg.setup().marginMm() > 0.0,
                  "dialog: unticked, it is the plain size with its margin");
            bleed->setChecked(true);
            check(dlg.setup().paper.contains(QStringLiteral("full bleed A3")) &&
                      dlg.setup().marginMm() == 0.0,
                  "dialog: ticked, it is the same size printed to its edges");
        }
        // The four things that can go on a page are all offered.
        QComboBox* areas = nullptr;
        for (QComboBox* box : dlg.findChildren<QComboBox*>())
            if (box->count() == 4 && box->itemData(0).toInt() == static_cast<int>(PlotArea::Layout))
                areas = box;
        check(areas != nullptr,
              "dialog: the whole sheet, everything on it, what is on screen, or a window of it");
        if (areas) {
            areas->setCurrentIndex(3);
            check(dlg.setup().area == PlotArea::Window, "dialog: and choosing one is what it says");
        }
        // The page preview is there, and draws something: the paper, its margin and what lands on it.
        QWidget* preview = nullptr;
        for (QWidget* w : dlg.findChildren<QWidget*>())
            if (w->minimumWidth() == 240 && w->minimumHeight() == 200) preview = w;
        check(preview != nullptr, "dialog: the page is shown beside the choices");
        if (preview) {
            preview->resize(260, 220);
            const QPixmap shot = preview->grab();
            QImage img = shot.toImage();
            int ink = 0;
            for (int y = 0; y < img.height(); ++y)
                for (int x = 0; x < img.width(); ++x)
                    if (qRed(img.pixel(x, y)) < 200) ++ink;
            check(ink > 100, "dialog: and it really draws the page, not an empty panel");
        }
    }
    {
        // The manager: the layout's own setup, plus the drawing's named ones, and Set current copies a
        // named one onto the sheet.
        Document doc;
        PageSetup named;
        named.name = QStringLiteral("A1 check print");
        named.paper = QStringLiteral("ISO full bleed A1 (841.00 x 594.00 MM)");
        named.monochrome = false;
        doc.pageSetups().push_back(named);
        PageSetupManagerDialog manager(doc, 0);
        auto* list = manager.findChild<QListWidget*>();
        check(list && list->count() == 2, "manager: the sheet's own setup and the drawing's named one");
        if (list && list->count() == 2) {
            check(list->item(0)->text().startsWith(QStringLiteral("*")),
                  "manager: the sheet's own is written in stars, as a plot dialog writes it");
            check(list->item(1)->text() == named.name, "manager: the named one by its name");
            QPushButton* setCurrent = nullptr;
            for (QPushButton* b : manager.findChildren<QPushButton*>())
                if (b->text() == QStringLiteral("Set current")) setCurrent = b;
            check(setCurrent != nullptr, "manager: the button that puts a setup on this sheet is there");
            if (setCurrent) {
                check(!setCurrent->isEnabled(),
                      "manager: with the sheet's own setup picked there is nothing to set");
                list->setCurrentRow(1);
                check(setCurrent->isEnabled(), "manager: with a named one picked, there is");
                setCurrent->click();
                check(doc.layouts()[0].pageSetup.paper == named.paper &&
                          doc.layouts()[0].pageSetup.monochrome == false,
                      "manager: it puts the named setup's paper and options on the sheet");
                check(manager.changed(), "manager: and says the drawing was changed");
            }
        }
    }

    // ---- a plot honours the setup: full bleed reaches the edge, a margin does not -----------------
    {
        MainWindow win;
        auto* canvas = win.findChild<CanvasWidget*>();
        QTabBar* spaces = nullptr;
        for (QTabBar* bar : win.findChildren<QTabBar*>())
            if (bar->count() > 0 && bar->tabText(0) == QLatin1String("Model")) spaces = bar;
        check(canvas && spaces, "plot: the window is up");
        if (!canvas || !spaces) {
            std::printf("  -> %d failed\n", ++setupFailures);
            return setupFailures;
        }
        // A right-click on a layout tab must open THAT SHEET's menu. It used to open the drawing's -
        // Copy, Cut, Quick Select - because a QTabBar ignores a right press and it travelled up to the
        // canvas the strip is laid over. The press is caught and stops there now.
        win.resize(1300, 850);
        win.show();
        QApplication::processEvents();
        spaces->setCurrentIndex(1);
        const QRect tab = spaces->tabRect(1);
        const QPointF onTab(tab.center());
        QMouseEvent rightPress(QEvent::MouseButtonPress, onTab, spaces->mapToGlobal(onTab),
                               Qt::RightButton, Qt::RightButton, Qt::NoModifier);
        const bool taken = QApplication::sendEvent(spaces, &rightPress) && rightPress.isAccepted();
        check(taken, "tabs: the right press on a tab is taken by the strip, not passed to the canvas");
        QMenu* opened = nullptr;
        for (QMenu* menu : win.findChildren<QMenu*>())
            if (menu->isVisible()) opened = menu;
        check(opened != nullptr, "tabs: and a menu opens");
        if (opened) {
            QStringList items;
            for (QAction* a : opened->actions())
                if (!a->isSeparator()) items << a->text();
            std::printf("  tab menu: %s\n", items.join(QStringLiteral(" / ")).toUtf8().constData());
            check(items.filter(QStringLiteral("Duplicate")).size() == 1 &&
                      items.filter(QStringLiteral("Rename")).size() == 1 &&
                      items.filter(QStringLiteral("Page setup")).size() == 1 &&
                      items.filter(QStringLiteral("Plot")).size() == 1,
                  "tabs: it is the sheet's menu - duplicate, rename, print, page setup");
            check(items.filter(QStringLiteral("Quick Select")).isEmpty() &&
                      items.filter(QStringLiteral("Copy")).isEmpty(),
                  "tabs: and not the drawing's, which is what used to come up");
            opened->close();
        }

        // Back to the model first. While a sheet is open, what is ON it is the live paper space - the
        // document the editor is drawing in - and a plot of that sheet takes it from there. This block
        // sets the sheet up through the layout itself, as a drawing loaded from a file would be, so the
        // canvas is stood off it.
        spaces->setCurrentIndex(0);
        QApplication::processEvents();
        // The DRAWING, by name: on a sheet, what is being drawn in is that sheet's paper space, which has
        // no layouts of its own.
        Document* drawing = canvas->drawingDocument();
        check(drawing && !drawing->layouts().empty(), "plot: the drawing has a sheet to plot");
        if (!drawing || drawing->layouts().empty()) {
            std::printf("  -> %d failed\n", ++setupFailures);
            return setupFailures;
        }
        DrawingLayout& sheet = drawing->layouts()[0];
        sheet.paperWidth = 420.0;
        sheet.paperHeight = 297.0;
        borderTheSheet(sheet);

        auto plot = [&](const QString& paper) {
            sheet.pageSetup.paper = paper;
            sheet.pageSetup.landscape = true;
            sheet.pageSetup.fitToPaper = false;
            sheet.pageSetup.scale = 1.0;
            sheet.pageSetup.centred = true;
            double w = 0.0, h = 0.0;
            win.spacePageSizeMm(1, w, h);
            QImage page(static_cast<int>(w) * 2, static_cast<int>(h) * 2, QImage::Format_RGB32);
            page.fill(Qt::white);
            QPainter p(&page);
            win.renderSpaceToPage(p, 1, true);
            p.end();
            return page;
        };

        // Full bleed: the page IS the sheet, so the sheet's border comes out on the page's own edge.
        const QImage bleed = plot(QStringLiteral("ISO full bleed A3 (420.00 x 297.00 MM)"));
        const QRect edgeBand(0, 0, bleed.width(), 4);
        check(inkIn(bleed, edgeBand) > 50,
              "plot: on full-bleed paper the sheet's border comes out at the paper's very edge - true "
              "size, which is the point of the size");

        // Plain A3, still at 1:1: the sheet is bigger than the printable area, so its border falls in
        // the margin and is CLIPPED - which is what would come out of the machine. Nothing is printed in
        // the margin by anything.
        const QImage plain = plot(QStringLiteral("ISO A3 (420.00 x 297.00 MM)"));
        const int marginPx = static_cast<int>(5.0 * 2) - 1;   // 5 mm of margin, at 2 px per mm
        check(inkIn(plain, QRect(0, 0, plain.width(), marginPx)) == 0,
              "plot: on plain paper nothing is printed in the margin - a printer cannot, so neither does "
              "the page");
        check(inkIn(plain, plain.rect()) < inkIn(bleed, bleed.rect()),
              "plot: so a sheet plotted at true size on paper too small for it loses its edges, rather "
              "than being quietly shrunk to fit");

        // Fit to paper on the same plain A3: now the whole border comes out, inside the margin.
        sheet.pageSetup.paper = QStringLiteral("ISO A3 (420.00 x 297.00 MM)");
        sheet.pageSetup.fitToPaper = true;
        QImage fitted(840, 594, QImage::Format_RGB32);
        fitted.fill(Qt::white);
        {
            QPainter p(&fitted);
            win.renderSpaceToPage(p, 1, true);
        }
        check(inkIn(fitted, QRect(0, 0, fitted.width(), marginPx)) == 0,
              "plot: fitted, the margin is still clean");
        check(inkIn(fitted, QRect(0, marginPx, fitted.width(), 6)) > 20,
              "plot: and the sheet's own border comes out just inside it - fitting is how a sheet is made "
              "to go on paper that will not hold it");
        sheet.pageSetup.fitToPaper = false;

        // The page a sheet asks for is the page its setup names, not the sheet's own size.
        sheet.pageSetup.paper = QStringLiteral("ISO A4 (297.00 x 210.00 MM)");
        double w = 0.0, h = 0.0;
        check(win.spacePageSizeMm(1, w, h) && near(w, 297.0) && near(h, 210.0),
              "plot: a sheet set up for A4 plots on A4, whatever size the sheet itself is");

        // ---- what goes on the page: a window of the sheet, and nothing else ----------------------
        {
            // Two marks on the sheet: one across the middle of its lower-left quarter, one up near the
            // top. A window over the lower-left quarter must print the first and not the second - that
            // is the whole of what "a window of it" means.
            sheet.paperEntities.clear();
            Entity low;
            low.type = EntityType::Line;
            low.id = QStringLiteral("low");
            low.layer = QStringLiteral("0");
            low.a = Vec2{10.0, 74.0};
            low.b = Vec2{200.0, 74.0};
            Entity high = low;
            high.id = QStringLiteral("high");
            high.a = Vec2{10.0, 250.0};
            high.b = Vec2{400.0, 250.0};
            sheet.paperEntities.push_back(low);
            sheet.paperEntities.push_back(high);
            sheet.pageSetup.paper = QStringLiteral("ISO full bleed A3 (420.00 x 297.00 MM)");
            sheet.pageSetup.landscape = true;
            sheet.pageSetup.fitToPaper = true;
            sheet.pageSetup.centred = true;

            auto pageOf = [&]() {
                QImage img(840, 594, QImage::Format_RGB32);
                img.fill(Qt::white);
                QPainter p(&img);
                win.renderSpaceToPage(p, 1, true);
                p.end();
                return img;
            };
            const QRect topBand(0, 0, 840, 120);
            const QRect middleBand(0, 250, 840, 90);

            sheet.pageSetup.area = PlotArea::Layout;
            const QImage whole = pageOf();
            check(inkIn(whole, topBand) > 20,
                  "area: the whole sheet puts the mark near its top on the page");

            sheet.pageSetup.area = PlotArea::Window;
            sheet.pageSetup.windowX1Mm = 0.0;
            sheet.pageSetup.windowY1Mm = 0.0;
            sheet.pageSetup.windowX2Mm = 210.0;
            sheet.pageSetup.windowY2Mm = 148.5;
            check(sheet.pageSetup.windowValid(), "area: the window is a real rectangle");
            const QImage windowed = pageOf();
            check(inkIn(windowed, middleBand) > 20,
                  "area: a window of the sheet prints what is inside it, blown up to the paper");
            check(inkIn(windowed, topBand) == 0,
                  "area: and nothing that is outside it - which is what choosing a window is for");

            // Display, with the canvas standing on the model rather than on this sheet: there is no
            // screen view of it to take, so the whole sheet is printed rather than nothing at all.
            sheet.pageSetup.area = PlotArea::Display;
            const QImage displayed = pageOf();
            check(inkIn(displayed, topBand) > 20,
                  "area: what is on screen falls back to the whole sheet when the sheet is not the one "
                  "being looked at");
            sheet.pageSetup.area = PlotArea::Layout;
            sheet.pageSetup.fitToPaper = false;
            borderTheSheet(sheet);
        }

        // ---- duplicate ---------------------------------------------------------------------------
        const size_t was = drawing->layouts().size();
        const QString firstName = drawing->layouts()[0].name;
        win.duplicateLayout(1);
        check(drawing->layouts().size() == was + 1, "duplicate: there is one more sheet");
        if (drawing->layouts().size() == was + 1) {
            const DrawingLayout& copy = drawing->layouts()[1];
            check(copy.name != firstName && copy.name.contains(firstName),
                  "duplicate: named after the one it came from, and not the same name");
            check(copy.paperEntities.size() == drawing->layouts()[0].paperEntities.size(),
                  "duplicate: with what was drawn on it");
            check(copy.pageSetup.paper == drawing->layouts()[0].pageSetup.paper,
                  "duplicate: and set up the same way - that is most of why one is copied");
            bool freshIds = true;
            for (const LayoutViewport& a : drawing->layouts()[0].viewports)
                for (const LayoutViewport& b : copy.viewports)
                    if (a.id == b.id) freshIds = false;
            check(freshIds,
                  "duplicate: its windows are new windows - two with one id are one window to anything "
                  "that looks them up");
            check(spaces->count() == static_cast<int>(drawing->layouts().size()) + 1,
                  "duplicate: and the strip shows it");
        }
    }

    if (setupFailures == 0) std::printf("  -> ok\n");
    else std::printf("  -> %d failed\n", setupFailures);
    return setupFailures;
}
