// Pictures on the sheet: how big one arrives, that its bytes survive a save and a load, that it is
// picked by clicking anywhere on it, and that dragging a corner keeps its proportions.
#include "app/Editor.h"
#include "model/Document.h"
#include "model/Entity.h"
#include "model/Picture.h"

#include <QBuffer>
#include <QImage>
#include <QPainter>
#include <cmath>
#include <cstdio>

using namespace Edraw_Desktop;

namespace {
int g_fail = 0;
void check(bool cond, const char* what) {
    if (!cond) { std::printf("  [FAIL] %s\n", what); ++g_fail; }
}
bool near(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }

void noSnap(Editor& ed) {
    auto& s = ed.snapSettings();
    s.objectSnap = false;
    s.gridSnap = false;
    s.ortho = false;
    s.polar = false;
}

// A picture with something in it, so the reader reports a real size: a red square and a blue one.
QImage madeUpImage(int w, int h) {
    QImage img(w, h, QImage::Format_ARGB32);
    img.fill(Qt::white);
    QPainter p(&img);
    p.fillRect(0, 0, w / 2, h, QColor(200, 40, 40));
    p.fillRect(w / 2, 0, w - w / 2, h, QColor(40, 60, 200));
    return img;
}

QByteArray pngBytes(int w, int h) {
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    madeUpImage(w, h).save(&buf, "PNG");
    return bytes;
}
} // namespace

int run_picture_tests() {
    g_fail = 0;
    std::printf("Picture tests\n");

    // --- The size a picture arrives at: its own pixels, at 96 dpi.
    {
        // 96 px is an inch at 96 dpi, so 96 x 48 px is 25.4 x 12.7 mm.
        const Entity pic = makePictureEntity(pngBytes(96, 48), QStringLiteral("png"),
                                            QStringLiteral("two-squares.png"));
        check(pic.type == EntityType::Image, "picture: the entity is an Image");
        check(pic.points.size() == 4, "picture: four corners, like a rectangle");
        const Bounds b = pic.bounds();
        check(b.valid && near(b.width(), 25.4, 1e-3) && near(b.height(), 12.7, 1e-3),
              "picture: 96 x 48 px arrives 25.4 x 12.7 mm (96 dpi)");
        check(pic.meta.value("imagePixW").toInt() == 96 && pic.meta.value("imagePixH").toInt() == 48,
              "picture: its natural pixel size is remembered, so it can be reset");
        check(!pic.meta.value("imageKey").toString().isEmpty(),
              "picture: a content hash for the render cache");
        check(pic.meta.value("lockAspect").toBool(), "picture: proportions locked to begin with");
        check(pictureBytes(pic) == pngBytes(96, 48), "picture: the bytes come back out unchanged");
        check(isPicture(pic), "picture: recognised as a picture");
    }

    // --- A screenshot the size of a wall is brought down to something a sheet can hold.
    {
        const Entity pic = makePictureEntity(pngBytes(1600, 800), QStringLiteral("png"),
                                             QStringLiteral("big.png"), 250.0);
        const Bounds b = pic.bounds();
        check(near(b.width(), 250.0, 1e-6), "picture: the long side is capped at the given maximum");
        check(near(b.width() / b.height(), 2.0, 1e-6),
              "picture: capping it keeps its proportions");
    }

    // --- Nonsense in is nothing out, not a broken object on the sheet.
    {
        const Entity pic = makePictureEntity(QByteArray("not an image at all"),
                                             QStringLiteral("png"), QStringLiteral("junk"));
        check(pic.type != EntityType::Image, "picture: bytes that are not an image are refused");
    }

    // --- Save and load: the picture survives, bytes and all.
    {
        const Entity pic = makePictureEntity(pngBytes(64, 64), QStringLiteral("png"),
                                            QStringLiteral("logo.png"));
        const Entity back = Entity::fromJson(pic.toJson());
        check(back.type == EntityType::Image, "picture: the type round-trips through JSON");
        check(pictureBytes(back) == pictureBytes(pic),
              "picture: the bytes round-trip, so a drawing keeps its logo");
        check(entityTypeToString(EntityType::Image) == QLatin1String("image") &&
                  entityTypeFromString(QStringLiteral("image")) == EntityType::Image,
              "picture: the type name maps both ways");
    }

    // --- Clicking anywhere on a picture picks it: what you see of it is its whole area.
    {
        Document d;
        d.entities().clear();
        Entity pic = makePictureEntity(pngBytes(96, 96), QStringLiteral("png"),
                                       QStringLiteral("p.png"));
        pic.id = QStringLiteral("pic");
        d.entities().push_back(pic);
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.setPixelTolerance(2);
        ed.setTool(ToolId::Select);
        const Bounds b = pic.bounds();
        ed.onPointerPress({b.center().x, b.center().y}, Qt::LeftButton, Qt::NoModifier);
        check(ed.selection().size() == 1 && ed.selection().front() == QLatin1String("pic"),
              "picture: a click in the middle of it selects it");
    }

    // --- Dragging a corner resizes it, and keeps its proportions while the lock is on.
    {
        Document d;
        d.entities().clear();
        Entity pic = makePictureEntity(pngBytes(200, 100), QStringLiteral("png"),
                                       QStringLiteral("p.png"));
        pic.id = QStringLiteral("pic");
        d.entities().push_back(pic);
        const Bounds start = pic.bounds();
        check(near(start.width() / start.height(), 2.0, 1e-6), "picture: 2:1 to start with");

        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.setPixelTolerance(2);
        ed.setTool(ToolId::Select);
        ed.selectById(QStringLiteral("pic"));
        // Grab the upper-right corner and pull it out and up, out of proportion.
        ed.onPointerPress({start.max.x, start.max.y}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({start.max.x + 40.0, start.max.y + 40.0});
        ed.onPointerRelease({start.max.x + 40.0, start.max.y + 40.0}, Qt::LeftButton);
        const Bounds after = d.entities()[0].bounds();
        check(after.width() > start.width() + 1e-6, "picture: the corner grip resizes it");
        check(near(after.width() / after.height(), 2.0, 1e-6),
              "picture: the lock keeps 2:1 however the corner is dragged");
        // The corner that was NOT dragged stays put, so it grows where it was told to.
        check(near(after.min.x, start.min.x, 1e-6) && near(after.min.y, start.min.y, 1e-6),
              "picture: the opposite corner stays where it was");
    }

    // --- With the lock off it stretches freely, which is what the lock being off means.
    {
        Document d;
        d.entities().clear();
        Entity pic = makePictureEntity(pngBytes(200, 100), QStringLiteral("png"),
                                       QStringLiteral("p.png"));
        pic.id = QStringLiteral("pic");
        pic.meta.insert(QStringLiteral("lockAspect"), false);
        d.entities().push_back(pic);
        const Bounds start = pic.bounds();
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.setPixelTolerance(2);
        ed.setTool(ToolId::Select);
        ed.selectById(QStringLiteral("pic"));
        ed.onPointerPress({start.max.x, start.max.y}, Qt::LeftButton, Qt::NoModifier);
        ed.onPointerMove({start.max.x, start.max.y + 60.0});
        ed.onPointerRelease({start.max.x, start.max.y + 60.0}, Qt::LeftButton);
        const Bounds after = d.entities()[0].bounds();
        check(near(after.width(), start.width(), 1e-6) &&
                  near(after.height(), start.height() + 60.0, 1e-6),
              "picture: with the lock off, one corner drag stretches one way only");
    }

    // --- Moving one: an ordinary object, moved like any other.
    {
        Document d;
        d.entities().clear();
        Entity pic = makePictureEntity(pngBytes(96, 96), QStringLiteral("png"),
                                       QStringLiteral("p.png"));
        pic.id = QStringLiteral("pic");
        d.entities().push_back(pic);
        Editor ed;
        ed.setDocument(&d);
        noSnap(ed);
        ed.setTool(ToolId::Select);
        ed.selectById(QStringLiteral("pic"));
        ed.setGeometryValue(QStringLiteral("Center X"), 500.0);
        ed.setGeometryValue(QStringLiteral("Width"), 50.0);
        const Bounds after = d.entities()[0].bounds();
        check(near(after.center().x, 500.0, 1e-6),
              "picture: Properties moves it (Center X), like anything else");
        check(near(after.width(), 50.0, 1e-6),
              "picture: Properties sizes it to an exact number of millimetres");
    }

    if (g_fail == 0) std::printf("  -> ok\n");
    return g_fail;
}
