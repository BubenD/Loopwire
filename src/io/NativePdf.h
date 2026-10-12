#pragma once
#include <QImage>
#include <QSizeF>
#include <QString>

namespace Loopwire {
struct PdfRaster {
    QImage image;
    QSizeF pageSize;
    int page = 0, count = 0;
    QString error;
};
// A PDF PAGE AS A VIEWER SHOWS IT, including embedded images and fonts. Called on a worker thread.
PdfRaster renderNativePdf(const QString& path, int page, int pixelWidth);
}
