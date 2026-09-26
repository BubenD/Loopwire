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

        // One copy of it on the sheet - one object, not a heap of lines: every entity of it carries the
        // block's name and the one instance key that makes it a copy.
        Document* doc = win.findChild<Editor*>() ? win.findChild<Editor*>()->document() : nullptr;
        check(doc != nullptr, "block: there is a drawing to have imported into");
        if (doc) {
            QSet<QString> instances;
            int stamped = 0;
            for (const Entity& e : doc->entities())
                if (e.meta.value(QStringLiteral("block")).toString() == stem) {
                    ++stamped;
                    instances.insert(e.meta.value(QStringLiteral("blockInst")).toString());
                }
            std::printf("  pdf block: %d entities on the sheet, %lld instance(s)\n", stamped,
                        static_cast<long long>(instances.size()));
            check(stamped >= 1, "block: a copy of it is on the sheet");
            check(instances.size() == 1, "block: and it is ONE copy, however many lines it is drawn with");
        }

        // Openable for editing: the block editor puts it on a tab of its own with its lines in it.
        win.openBlockEditor(stem);
        QApplication::processEvents();
        bool openForEditing = false;
        for (QTabBar* bar : win.findChildren<QTabBar*>())
            for (int i = 0; i < bar->count(); ++i)
                if (bar->tabText(i).contains(stem)) openForEditing = true;
        check(openForEditing, "block: and it opens on a tab of its own to be edited");
        win.cancelBlockEdit();
        QApplication::processEvents();

        // DOUBLE-CLICK it on the sheet and the block opens: the lines are separate objects there, every
        // drawing tool works on them, and what is copied inside can be pasted outside. Which is the whole
        // point of importing a PDF as a block rather than as a picture.
        if (doc) {
            // A point ON the geometry, not the middle of its bounding box: the middle of the box around an
            // L-shaped path is not on the path, and a double-click there hits nothing at all.
            Vec2 onIt{0.0, 0.0};
            for (const Entity& e : doc->entities())
                if (e.meta.value(QStringLiteral("block")).toString() == stem) {
                    if (!e.points.empty()) onIt = e.points.front();
                    else onIt = e.a;
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
