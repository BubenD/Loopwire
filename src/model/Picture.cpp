#include "model/Picture.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>

#include <algorithm>
#include <cmath>

namespace Loopwire {

namespace {
// 96 dpi is what a screen pixel is worth, so a screenshot arrives on the sheet at the size it looked
// on screen rather than at some arbitrary scale.
constexpr double kMmPerPixel = 25.4 / 96.0;
} // namespace

QSizeF pictureDefaultSizeMm(const QSize& pixels, double maxSideMm) {
    const double pw = std::max(1, pixels.width());
    const double ph = std::max(1, pixels.height());
    double w = pw * kMmPerPixel;
    double h = ph * kMmPerPixel;
    // A big screenshot is 3000 px across - a metre and a half of paper. Bring it down to something a
    // sheet can hold, keeping its proportions, and let the corner grip take it from there.
    const double longest = std::max(w, h);
    if (maxSideMm > 0.0 && longest > maxSideMm) {
        const double f = maxSideMm / longest;
        w *= f;
        h *= f;
    }
    return QSizeF(w, h);
}

std::vector<Vec2> pictureCorners(double wMm, double hMm) {
    // The same winding as a Rect: lower-left, lower-right, upper-right, upper-left. Every generic
    // points-based operation (move, mirror, corner grips, snapping) then treats it as a rectangle.
    return {{0.0, 0.0}, {wMm, 0.0}, {wMm, hMm}, {0.0, hMm}};
}

Entity makePictureEntity(const QByteArray& bytes, const QString& format, const QString& name,
                         double maxSideMm) {
    Entity e;
    if (bytes.isEmpty()) return e;
    QBuffer buf;
    buf.setData(bytes);
    if (!buf.open(QIODevice::ReadOnly)) return e;
    QImageReader reader(&buf);
    const QSize pix = reader.size();
    if (!pix.isValid() || pix.isEmpty()) return e;   // not something Qt can read as an image

    const QSizeF mm = pictureDefaultSizeMm(pix, maxSideMm);
    e.type = EntityType::Image;
    e.points = pictureCorners(mm.width(), mm.height());
    e.meta.insert(QLatin1String(PictureKeys::kData), QString::fromLatin1(bytes.toBase64()));
    e.meta.insert(QLatin1String(PictureKeys::kFormat),
                  format.isEmpty() ? QString::fromLatin1(reader.format()).toLower() : format.toLower());
    e.meta.insert(QLatin1String(PictureKeys::kName), name);
    // A content hash, so the renderer decodes each distinct picture once however many copies of it are
    // on the sheet.
    e.meta.insert(QLatin1String(PictureKeys::kKey),
                  QString::fromLatin1(
                      QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex()));
    e.meta.insert(QLatin1String(PictureKeys::kPixW), pix.width());
    e.meta.insert(QLatin1String(PictureKeys::kPixH), pix.height());
    e.meta.insert(QLatin1String(PictureKeys::kOpacity), 100);
    e.meta.insert(QLatin1String(PictureKeys::kLockAspect), true);
    return e;
}

Entity makePictureEntity(const QImage& image, const QString& name, double maxSideMm) {
    if (image.isNull()) return Entity{};
    QByteArray bytes;
    QBuffer buf(&bytes);
    if (!buf.open(QIODevice::WriteOnly)) return Entity{};
    if (!image.save(&buf, "PNG")) return Entity{};
    buf.close();
    return makePictureEntity(bytes, QStringLiteral("png"), name, maxSideMm);
}

Entity pictureEntityFromFile(const QString& path, double maxSideMm) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return Entity{};
    const QByteArray bytes = f.readAll();
    f.close();
    const QFileInfo fi(path);
    return makePictureEntity(bytes, fi.suffix(), fi.fileName(), maxSideMm);
}

QByteArray pictureBytes(const Entity& e) {
    return QByteArray::fromBase64(
        e.meta.value(QLatin1String(PictureKeys::kData)).toString().toLatin1());
}

bool isPicture(const Entity& e) {
    return e.type == EntityType::Image &&
           !e.meta.value(QLatin1String(PictureKeys::kData)).toString().isEmpty();
}

QString pictureFileFilter() {
    // Whatever this build of Qt can actually read, so the filter never offers a format that then
    // fails to open - plus the common ones spelled out first, which is what people look for.
    QStringList exts;
    for (const QByteArray& f : QImageReader::supportedImageFormats())
        exts << QStringLiteral("*.%1").arg(QString::fromLatin1(f).toLower());
    exts.removeDuplicates();
    return QStringLiteral("Pictures (%1);;All files (*)").arg(exts.join(QLatin1Char(' ')));
}

} // namespace Loopwire
