// Pictures on a sheet: a PNG, JPEG, BMP (anything Qt can read) placed as an ordinary drawing object.
//
// The bytes are carried INSIDE the entity, base64 in its meta, rather than as a path to a file. A
// template with a company logo on it has to keep working when it is copied to another machine, sent to
// a colleague, or opened after the original file has moved - and a screenshot pasted from the clipboard
// never had a file to point at in the first place. The cost is a bigger .dwx/.etp; the alternative is a
// drawing whose logo silently disappears, which is worse.
#pragma once

#include "model/Entity.h"

#include <QByteArray>
#include <QImage>
#include <QSize>
#include <QString>

namespace Loopwire {

// Every meta key a picture uses, in one place.
namespace PictureKeys {
inline constexpr auto kData = "image";         // base64 of the file's bytes
inline constexpr auto kFormat = "imageFormat"; // "png", "jpg", ... (what the bytes are)
inline constexpr auto kName = "imageName";     // what to call it in the properties panel
inline constexpr auto kKey = "imageKey";       // content hash: the render cache's key
inline constexpr auto kPixW = "imagePixW";     // its natural size in pixels, so it can be reset
inline constexpr auto kPixH = "imagePixH";
inline constexpr auto kOpacity = "opacity";    // 0..100, for a watermark behind the drawing
inline constexpr auto kLockAspect = "lockAspect";
} // namespace PictureKeys

// A picture entity from file bytes. `format` is the image format ("png"); `name` is what to call it.
// The bytes are kept as they came, so a JPEG stays a JPEG rather than being re-encoded.
// Returns an entity of type Unknown if the bytes are not an image Qt can read.
Entity makePictureEntity(const QByteArray& bytes, const QString& format, const QString& name,
                         double maxSideMm = 250.0);

// A picture entity from an image already in memory (a clipboard screenshot). Encoded as PNG, which is
// lossless - a screenshot re-encoded as JPEG would come out blurred around the text on it.
Entity makePictureEntity(const QImage& image, const QString& name, double maxSideMm = 250.0);

// Read a picture from a file. Empty entity (type Unknown) if it cannot be read.
Entity pictureEntityFromFile(const QString& path, double maxSideMm = 250.0);

// The size on the sheet a picture of this pixel size gets by default: its natural size at 96 dpi,
// scaled down to fit `maxSideMm` if that would be bigger than a sheet.
QSizeF pictureDefaultSizeMm(const QSize& pixels, double maxSideMm);

// The four corners for a picture of size `mm` with its lower-left at the origin.
std::vector<Vec2> pictureCorners(double wMm, double hMm);

// The bytes back out of a picture entity, ready for QPixmap::loadFromData.
QByteArray pictureBytes(const Entity& e);

// Is this entity a picture with something in it?
bool isPicture(const Entity& e);

// The file extensions the application will open as a picture, as a file-dialog filter.
QString pictureFileFilter();

} // namespace Loopwire
