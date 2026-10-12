// Getting a picture onto the sheet: from a file, or from the clipboard.
//
// Three ways in, because all three are how people actually do it: Insert > Picture for a file on disk,
// Ctrl+V for a screenshot just taken, and Ctrl+V again for an image file copied in Explorer (which puts
// a file URL on the clipboard, not an image). All three end in the same place - a picture entity
// following the cursor until it is clicked into position, exactly like a pasted object or a block.
#include "app/MainWindow.h"

#include "app/Editor.h"
#include "model/Document.h"
#include "model/Picture.h"

#include <QClipboard>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QStatusBar>
#include <QUrl>

namespace Loopwire {

void MainWindow::insertPictureFromFile() {
    if (!doc_) return;
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Insert a picture"),
                                                      QString(), pictureFileFilter());
    if (path.isEmpty()) return;
    Entity pic = pictureEntityFromFile(path);
    if (pic.type != EntityType::Image) {
        statusBar()->showMessage(
            QStringLiteral("%1 is not a picture this build can read").arg(QFileInfo(path).fileName()),
            6000);
        return;
    }
    editor_->placeEntities({pic});   // follows the cursor; click to put it down
    statusBar()->showMessage(
        QStringLiteral("%1 — click to place it, then drag a corner to size it")
            .arg(QFileInfo(path).fileName()),
        6000);
}

bool MainWindow::insertPictureFromClipboard(bool quiet) {
    if (!doc_) return false;
    const QClipboard* cb = QGuiApplication::clipboard();
    const QMimeData* md = cb ? cb->mimeData() : nullptr;
    if (!md) return false;

    Entity pic;
    QString what;
    if (md->hasImage()) {
        // A screenshot, or an image copied out of another application.
        const QImage img = qvariant_cast<QImage>(md->imageData());
        pic = makePictureEntity(img, QStringLiteral("Pasted picture"));
        what = QStringLiteral("the picture on the clipboard");
    } else if (md->hasUrls()) {
        // An image FILE copied in the file manager: the clipboard holds its path, not its pixels.
        for (const QUrl& u : md->urls()) {
            if (!u.isLocalFile()) continue;
            Entity fromFile = pictureEntityFromFile(u.toLocalFile());
            if (fromFile.type != EntityType::Image) continue;
            pic = fromFile;
            what = QFileInfo(u.toLocalFile()).fileName();
            break;
        }
    }
    if (pic.type != EntityType::Image) {
        if (!quiet)
            statusBar()->showMessage(QStringLiteral("There is no picture on the clipboard"), 5000);
        return false;
    }
    editor_->placeEntities({pic});
    statusBar()->showMessage(
        QStringLiteral("%1 — click to place it, then drag a corner to size it").arg(what), 6000);
    return true;
}

void MainWindow::pasteFromClipboard() {
    // Whichever was filled more recently wins: copy two lines then Ctrl+V and you get the lines; take
    // a screenshot then Ctrl+V and you get the screenshot. Comparing the two beats asking, and beats
    // a fixed preference that is wrong half the time.
    const QClipboard* cb = QGuiApplication::clipboard();
    const QMimeData* md = cb ? cb->mimeData() : nullptr;
    const bool sysHasPicture = md && (md->hasImage() || md->hasUrls());
    if (sysHasPicture && (editor_->clipboardEmpty() || sysClipImageAt_ >= editor_->clipboardStamp())) {
        if (insertPictureFromClipboard(true)) return;
    }
    editor_->paste();
}

} // namespace Loopwire
