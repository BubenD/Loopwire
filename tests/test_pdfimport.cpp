#include "io/PdfImport.h"
#include <QTabBar>
#include <QSet>
#include <QFile>
#include <QDir>
#include <QApplication>
#include "harness/PartsLibrary.h"
#include "model/Block.h"
#include "app/Editor.h"
#include "tools/ToolId.h"
#include "app/MainWindow.h"
#include <QProgressBar>
#include <QToolButton>
#include <QElapsedTimer>
#include <QMenu>
#include <QFileInfo>
#include "io/Dwx.h"
#include <QImage>
#include <QPainter>
#include "render/EntityRenderer.h"
#include "tools/EntityTransforms.h"
#include <cmath>
#include "model/Document.h"

#include <cstdio>

using namespace Edraw_Desktop;

namespace {
int g_fail = 0;
void check(bool ok, const char* what) {
    if (!ok) { std::printf("  [FAIL] %s\n", what); ++g_fail; }
}
} // namespace

int run_pdfimport_tests() {
    g_fail = 0;
    std::printf("PDF import tests\n");
    const QByteArray pdf =
        "%PDF-1.4\n"
        "1 0 obj << /Length 35 >>\nstream\n"
        "1 0 0 RG 0 0 m 100 0 l 100 50 l S\n"
        "endstream\nendobj\n"
        "trailer << >>\n%%EOF\n";
    Document doc;
    QString error;
    check(pdf::parse(pdf, doc, &error), "pdf: vector paths convert through DXF");
    check(doc.entities().size() == 1, "pdf: path becomes a drawing entity");
    if (doc.entities().size() == 1) {
        check(doc.entities()[0].type == EntityType::Polyline,
              "pdf: line path is preserved");
        check(doc.entities()[0].points.back().y == 50.0, "pdf: coordinates are preserved");
        // Color is checked below after the DXF conversion.
        check(doc.entities()[0].color == QLatin1String("#ff0000"),
              "pdf: stroke colour survives the DXF conversion");
    }

    // ---- a PDF comes in as a BLOCK, and one that can be opened and edited ------------------------
    {
        // Loose, a PDF is hundreds of little lines: impossible to move as a unit, impossible to pick
        // without catching the drawing under it, impossible to change all at once. As a block it is one
        // object, insertable again, and openable on its own tab where the lines can be worked on.
        const QString stem = QStringLiteral("edraw-pdfblock-test");
        const QString file = QDir::tempPath() + QStringLiteral("/") + stem + QStringLiteral(".pdf");
        QFile::remove(file);
        {
            QFile f(file);
            check(f.open(QIODevice::WriteOnly), "block: a PDF to import is written");
            f.write(pdf);
        }
        // Any block of that name left by an earlier run would make this one a name test instead.
        QFile::remove(BlockDef::pathFor(stem));

        MainWindow win;
        QString said;
        const bool ok = win.importPdfFile(file, &said);
        std::printf("  pdf block: %s\n", qPrintable(said));
        check(ok, "block: the PDF imports");
        // AND IT LEAVES NOTHING BEHIND: no progress bar at the bottom, and the copy not left picked -
        // a big PDF's tens of thousands of picked lines were minutes of work for every panel.
        check(!win.findChild<QProgressBar*>(QStringLiteral("importProgress")),
              "block: the progress bar is gone once the import is done");
        check(win.findChild<Editor*>() && win.findChild<Editor*>()->selection().empty(),
              "block: the imported copy is not left picked");
        check(said.contains(QStringLiteral("2 line(s)")),
              "block: a three-point path comes in as its two lines, not as one object");

        PartsCatalog catalog;
        catalog.openRemembered();
        BlockDef made;
        for (const BlockDef& b : allBlocks(&catalog))
            if (b.name == stem) made = b;
        check(!made.name.isEmpty(), "block: the import is a block, named after the file");
        check(made.entities.size() == 2,
              "block: the block holds the lines separately - a path is not one object inside it");
        check(made.description.contains(stem), "block: and it says where it came from");

        // ONE OBJECT on the sheet, not a heap of lines: a block reference that draws the block out of
        // its file. As the block's own entities, a big PDF was tens of thousands of objects and picking
        // one of them stopped the window for minutes.
        Document* doc = win.findChild<Editor*>() ? win.findChild<Editor*>()->document() : nullptr;
        check(doc != nullptr, "block: there is a drawing to have imported into");
        const Entity* ref = nullptr;
        if (doc) {
            int refs = 0, loose = 0;
            for (const Entity& e : doc->entities()) {
                if (e.type == EntityType::BlockRef && e.meta.value(QLatin1String(kBlockRefKey)).toString() == stem) {
                    ++refs;
                    ref = &e;
                }
                if (e.meta.value(QStringLiteral("block")).toString() == stem) ++loose;
            }
            std::printf("  pdf block: %d reference(s), %d loose copy entities\n", refs, loose);
            check(refs == 1 && loose == 0, "block: ONE reference to it on the sheet, however many lines it is");
            const auto content = blockRefContent(stem);
            check(content && content->entities.size() == 2, "block: and the reference draws the block's lines");
            // The reference is round what it draws: its box is the block's box.
            if (ref) {
                const Bounds rb = ref->bounds();
                const Bounds lb = blockLocalBounds(made.entities);
                check(rb.valid && std::abs(rb.min.x - lb.min.x) < 1e-6 && std::abs(rb.max.y - lb.max.y) < 1e-6,
                      "block: the reference's box is the block's");
                check(explodeBlockRef(*ref).size() == 2, "block: and it breaks back into its two lines");
                // Painted without trouble.
                QImage img(200, 200, QImage::Format_ARGB32);
                img.fill(Qt::white);
                QPainter painter(&img);
                ViewTransform vt;
                vt.view.scale = 1.0;
                vt.view.x = 100.0;
                vt.view.y = 100.0;
                EntityRenderer::draw(painter, *ref, *doc, vt, QColor());
                painter.end();
                int inked = 0;
                for (int y = 0; y < img.height(); ++y)
                    for (int x = 0; x < img.width(); ++x)
                        if (img.pixelColor(x, y) != QColor(Qt::white)) ++inked;
                check(inked > 0, "block: the reference draws its lines");
            }
        }

        // Openable for editing: the block editor puts it on a tab of its own with its lines in it.
        win.openBlockEditor(stem);
        QApplication::processEvents();
        bool openForEditing = false;
        for (QTabBar* bar : win.findChildren<QTabBar*>())
            for (int i = 0; i < bar->count(); ++i)
                if (bar->tabText(i).contains(stem)) openForEditing = true;
        check(openForEditing, "block: and it opens on a tab of its own to be edited");
        // EVERY LINE ITS OWN ID inside the block, and picking one picks that one. They all used to be
        // called "", so a click on one line selected the whole drawing - and a big one stopped the window.
        if (Editor* inside = win.findChild<Editor*>(); inside && inside->document()) {
            QSet<QString> ids;
            for (const Entity& e : inside->document()->entities()) ids.insert(e.id);
            check(!ids.contains(QString()) && ids.size() == int(inside->document()->entities().size()),
                  "block: every line in the block editor has an id of its own");
            const Entity& first = inside->document()->entities().front();
            inside->setTool(ToolId::Select);
            const Vec2 mid = (first.a + first.b) * 0.5;
            inside->onPointerPress(mid, Qt::LeftButton, Qt::NoModifier);
            inside->onPointerRelease(mid, Qt::LeftButton);
            check(inside->selection().size() == 1 && inside->selection().front() == first.id,
                  "block: and a click on one line picks that line alone");
            inside->clearSelection();
        }
        win.cancelBlockEdit();
        QApplication::processEvents();

        // HOVERED, THE BLOCK IS LIT GREY: over one of its lines it is the block the cursor is on; off in
        // empty space, it is not.
        if (doc && ref) {
            Editor* ed2 = win.findChild<Editor*>();
            if (ed2) {
                ed2->setTool(ToolId::Select);
                Vec2 onLine{0.0, 0.0};
                if (const auto content = blockRefContent(stem); content && !content->entities.empty()) {
                    const Entity& first = content->entities.front();
                    onLine = first.points.empty() ? first.a : first.points.front();
                }
                ed2->onPointerMove(onLine);
                check(ed2->hoveredBlock() == ref->id, "block: hovered, the reference is the one lit");
                ed2->onPointerMove(Vec2{5000.0, 5000.0});
                check(ed2->hoveredBlock().isEmpty(), "block: and moved off it, nothing is lit");
            }
        }

        // THE HOME TAB'S EDIT BLOCK opens the picked block, the same as a double-click.
        if (doc && ref) {
            Editor* ed1 = win.findChild<Editor*>();
            auto* editBtn = win.findChild<QToolButton*>(QStringLiteral("editBlockButton"));
            check(editBtn != nullptr, "block: an Edit block button on the Home tab");
            if (ed1 && editBtn) {
                ed1->selectById(ref->id);
                editBtn->click();
                QApplication::processEvents();
                Editor* now = win.findChild<Editor*>();
                check(now && now->document() && now->document()->name().contains(stem),
                      "block: and it opens the picked block for editing");
                win.cancelBlockEdit();
                QApplication::processEvents();
            }
        }

        // DOUBLE-CLICK it on the sheet and the block opens: the lines are separate objects there, every
        // drawing tool works on them, and what is copied inside can be pasted outside. Which is the whole
        // point of importing a PDF as a block rather than as a picture.
        if (doc) {
            // A point ON the geometry, not the middle of its bounding box: the middle of the box around an
            // L-shaped path is not on the path, and a double-click there hits nothing at all.
            Vec2 onIt{0.0, 0.0};
            if (const auto content = blockRefContent(stem); content && !content->entities.empty()) {
                const Entity& first = content->entities.front();
                onIt = first.points.empty() ? first.a : first.points.front();
            }
            // A click on its lines picks it.
            {
                Editor* ed0 = win.findChild<Editor*>();
                if (ed0) {
                    ed0->setTool(ToolId::Select);
                    ed0->onPointerPress(onIt, Qt::LeftButton, Qt::NoModifier);
                    ed0->onPointerRelease(onIt, Qt::LeftButton);
                }
                check(ed0 && ed0->selection().size() == 1 && ref && ed0->selection().front() == ref->id,
                      "block: a click on its lines picks the reference, and only it");
                if (ed0) ed0->clearSelection();
            }
            Editor* ed = win.findChild<Editor*>();
            check(ed != nullptr, "block: the editor is there to double-click with");
            if (ed) {
                ed->setTool(ToolId::Select);
                ed->onDoubleClick(onIt);
                QApplication::processEvents();
                bool editing = false;
                for (QTabBar* bar : win.findChildren<QTabBar*>())
                    for (int i = 0; i < bar->count(); ++i)
                        if (bar->tabText(i).contains(stem)) editing = true;
                check(editing, "block: double-clicking a copy of it opens the block to be edited");

                // Inside the block, and known to be: the tab is the block's own, so what is measured next
                // is the block's contents and not the drawing's.
                Editor* inBlock = win.findChild<Editor*>();
                check(inBlock && inBlock->document() &&
                          inBlock->document()->name().contains(stem),
                      "block: and the drawing being edited IS the block");
                ed = inBlock;
                // Inside: the lines are their own objects, and they can be taken to the clipboard.
                const int inside = static_cast<int>(ed->document()->entities().size());
                std::printf("  pdf block: %d separate object(s) inside the block\n", inside);
                check(inside == 2,
                      "block: and inside it every line of the PDF is its own object to work on");
                ed->selectAll();
                ed->copySelection();
                check(ed->clipboardCount() == inside,
                      "block: all of them go to the clipboard together");

                // Out again, and paste: what was inside the block is now loose in the drawing, which is
                // what "copy out of the block" has to mean.
                win.cancelBlockEdit();
                QApplication::processEvents();
                Editor* back = win.findChild<Editor*>();
                const int beforePaste = static_cast<int>(back->document()->entities().size());
                back->paste();
                back->onPointerPress(Vec2{500.0, 500.0}, Qt::LeftButton, Qt::NoModifier);
                QApplication::processEvents();
                const int afterPaste = static_cast<int>(back->document()->entities().size());
                std::printf("  pdf block: pasted outside, %d -> %d object(s)\n", beforePaste, afterPaste);
                check(afterPaste == beforePaste + inside,
                      "block: and pasting outside the block puts them in the drawing");
            }
        }

        // A second import of the same file does not overwrite the first block - and every copy of it
        // already on a sheet.
        QString again;
        win.importPdfFile(file, &again);
        bool secondOne = false;
        PartsCatalog catalog2;
        catalog2.openRemembered();
        for (const BlockDef& b : allBlocks(&catalog2))
            if (b.name == stem + QStringLiteral(" (2)")) secondOne = true;
        check(secondOne, "block: importing it again makes a second block rather than replacing the first");

        QFile::remove(file);
        QFile::remove(BlockDef::pathFor(stem));
        QFile::remove(BlockDef::pathFor(stem + QStringLiteral(" (2)")));
        QString dxf = file;
        dxf.chop(4);
        QFile::remove(dxf + QStringLiteral(".dxf"));
    }

    // HATCH PICK POINTS WAITS FOR THE CURSOR TO STOP: nothing is shown while it moves, and the region under
    // it a fifth of a second after it stops.
    {
        Document hd;
        hd.entities().clear();
        Entity box;
        box.id = QStringLiteral("hatch-box");
        box.type = EntityType::Rect;
        box.points = {{0, 0}, {100, 0}, {100, 60}, {0, 60}};
        hd.entities().push_back(box);
        Editor he;
        he.setDocument(&hd);
        he.setTool(ToolId::Hatch);
        he.hatchStyle().boundary = Editor::HatchBoundary::PickPoints;
        he.onPointerMove(Vec2{50, 30});
        check(!he.hatchPreviewShown(), "hatch: nothing shown while the cursor is still moving");
        QElapsedTimer wait;
        wait.start();
        while (!he.hatchPreviewShown() && wait.elapsed() < 1500) QApplication::processEvents(QEventLoop::AllEvents, 20);
        std::printf("  hatch preview after %lld ms\n", static_cast<long long>(wait.elapsed()));
        check(he.hatchPreviewShown() && wait.elapsed() >= 150, "hatch: and the region shows once it has stopped");
        he.onPointerMove(Vec2{51, 30});
        check(!he.hatchPreviewShown(), "hatch: moving again takes it away until the cursor stops again");
    }

    // A BLOCK REFERENCE KEEPS ITS PLACE when the block changes: turned a quarter, doubled and moved, then
    // the block grows - the block's origin is still where it was put, and one unit is still two, upright.
    {
        BlockDef def;
        def.name = QStringLiteral("refit-test");
        Entity l;
        l.type = EntityType::Line;
        l.a = {0, 0};
        l.b = {10, 5};
        def.entities.append(l.toJson());
        Entity ref = blockRefEntity(def, Vec2{0, 0});
        rotateEntity(ref, Vec2{0, 0}, M_PI / 2.0);
        scaleEntity(ref, Vec2{0, 0}, 2.0);
        translateEntity(ref, Vec2{100, 50});
        Bounds grown;
        grown.expand(Vec2{-4, -4});
        grown.expand(Vec2{20, 8});
        refitBlockRef(ref, grown);
        Vec2 o, ux, uy;
        const bool framed = blockRefFrame(ref, o, ux, uy);
        check(framed && std::abs(o.x - 100) < 1e-6 && std::abs(o.y - 50) < 1e-6 && std::abs(ux.x) < 1e-6 &&
                  std::abs(ux.y - 2.0) < 1e-6 && std::abs(uy.x + 2.0) < 1e-6,
              "blockref: refitted to a bigger block, it keeps where it is, how it is turned and how big");
    }

    // THE LAST FIFTEEN DRAWINGS: one opened goes to the top of File > Open recent, the list stops at
    // fifteen, and the menu offers each of them.
    {
        MainWindow::recentDrawings();   // touch the settings before clearing
        const QString file = QDir::temp().filePath(QStringLiteral("edraw-recent-test.dwx"));
        {
            Document saved;
            Entity l;
            l.id = QStringLiteral("r1");
            l.type = EntityType::Line;
            l.a = {0, 0};
            l.b = {10, 0};
            saved.entities().push_back(l);
            dwx::save(saved, file);
        }
        MainWindow win;
        win.clearRecentDrawings();
        win.openPath(file);
        const QStringList after = MainWindow::recentDrawings();
        check(!after.isEmpty() && QFileInfo(after.front()) == QFileInfo(file),
              "recent: a drawing opened goes to the top of the list");
        for (int i = 0; i < 20; ++i)
            win.rememberRecentDrawing(QDir::temp().filePath(QStringLiteral("edraw-recent-%1.dwx").arg(i)));
        win.rememberRecentDrawing(file);   // again: back to the top, not listed twice
        const QStringList list = MainWindow::recentDrawings();
        int times = 0;
        for (const QString& p : list) times += QFileInfo(p) == QFileInfo(file) ? 1 : 0;
        std::printf("  recent: %lld listed\n", static_cast<long long>(list.size()));
        check(list.size() == MainWindow::kRecentDrawings && times == 1 && QFileInfo(list.front()) == QFileInfo(file),
              "recent: fifteen at most, newest first, each once");
        auto* menu = win.findChild<QMenu*>(QStringLiteral("recentMenu"));
        bool offered = false, missingDisabled = false;
        if (menu)
            for (QAction* a : menu->actions()) {
                if (QFileInfo(a->data().toString()) == QFileInfo(file)) offered = a->isEnabled();
                if (a->data().toString().contains(QStringLiteral("edraw-recent-1"))) missingDisabled = !a->isEnabled();
            }
        check(menu && offered && missingDisabled,
              "recent: the menu offers each - and one no longer on disk is shown but cannot be picked");
        win.clearRecentDrawings();
        QFile::remove(file);
    }

    Document unchanged;
    Entity existing;
    existing.type = EntityType::Line;
    existing.a = {1, 2};
    existing.b = {3, 4};
    unchanged.entities().push_back(existing);
    check(!pdf::parse(QByteArrayLiteral("not a pdf"), unchanged, &error),
          "pdf: invalid input is rejected");
    check(unchanged.entities().size() == 1, "pdf: failed import leaves the drawing unchanged");

    // A real PDF has bytes in it that mean nothing to a content stream: the '>' that ends a hex
    // string, a stray ')', the '{' and '}' of a PostScript function, a NUL padding a stream. The
    // reader has to pass over them.
    //
    // It did not: a byte that is a delimiter but is not one the reader handles left it making no
    // progress - the run of non-delimiters before it is empty, so the read position never moved - and
    // it appended an empty token for ever until the process ran out of memory. A 23 kB file took the
    // application down with a fail-fast rather than being read. Every one of those bytes is in here, so
    // a reader that cannot get past one of them hangs this test instead of a person's drawing.
    {
        QByteArray awkward =
            "%PDF-1.4\n"
            "1 0 obj << /Length 90 >>\nstream\n"
            "{ 1 exch } 0 0 m 10 0 l S ) > } { <414243> 20 20 m 30 30 l S\n"
            "endstream\nendobj\n"
            "trailer << >>\n%%EOF\n";
        awkward.append('\0');            // and a NUL, which is a delimiter too
        awkward.append("\n%%EOF\n");
        Document odd;
        QString err;
        pdf::parse(awkward, odd, &err);  // it may read it or refuse it; it must not spin
        check(true, "pdf: a stray delimiter is passed over rather than read for ever");
        // What it could read is still read: the two paths in there.
        check(odd.entities().size() >= 1,
              "pdf: and the geometry either side of one is still picked up");
    }

    std::printf(g_fail ? "  -> %d failed\n" : "  -> ok\n", g_fail);
    return g_fail;
}
