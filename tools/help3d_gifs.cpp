// THE 3D VIEW'S HELP, RECORDED. Builds a small model to route in, opens the real 3D view on it, and drives it the
// way a person would - the pointer moving, buttons pressed, faces clicked, keys struck - grabbing the view as it
// goes. Over each picture: the pointer, a ring where it clicks, the key pressed, and a line saying what is being
// done. Written as the animated GIFs the help shows, into resources/help3d.
//
//   cmake --build build --config Release --target edraw_help3d_gifs
//   build\Release\edraw_help3d_gifs.exe <repo>\resources\help3d
//
// Run on the screen (not the offscreen platform) with Qt's binaries on PATH. Recorded from the program itself, the
// help cannot show the view doing something it does not do.
#include "app/View3DPane.h"
#include "io/GifWriter.h"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSlider>
#include <QTableWidget>
#include <QTextStream>
#include <QWheelEvent>

#include <cmath>
#include <cstdio>
#include <iterator>

using namespace Edraw_Desktop;

namespace {

// THE MODEL: a base plate, a device with a connector on its side, a second box with one on its front, and a rib
// between them that the cable has to go round. In millimetres.
QString writeModel(const QString& dir) {
    struct Box {
        const char* name;
        double x0, y0, z0, x1, y1, z1;
    };
    const Box boxes[] = {
        {"Base", 0, 0, -8, 600, 400, 0},
        {"Device", 40, 150, 0, 160, 270, 100},
        {"Box", 440, 120, 0, 560, 220, 70},
        {"Rib", 260, 0, 0, 280, 300, 14},
    };
    const QString path = QDir(dir).filePath(QStringLiteral("help3d_model.obj"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return {};
    QTextStream out(&f);
    int base = 0;
    for (const Box& b : boxes) {
        out << "o " << b.name << "\n";
        const double xs[2] = {b.x0, b.x1}, ys[2] = {b.y0, b.y1}, zs[2] = {b.z0, b.z1};
        for (int i = 0; i < 8; ++i) out << "v " << xs[i & 1] << ' ' << ys[(i >> 1) & 1] << ' ' << zs[(i >> 2) & 1] << "\n";
        // The six faces, each wound to look out.
        const int faces[6][4] = {{0, 2, 3, 1}, {4, 5, 7, 6}, {0, 1, 5, 4}, {2, 6, 7, 3}, {0, 4, 6, 2}, {1, 3, 7, 5}};
        for (const auto& q : faces)
            out << "f " << base + q[0] + 1 << ' ' << base + q[1] + 1 << ' ' << base + q[2] + 1 << ' ' << base + q[3] + 1 << "\n";
        base += 8;
    }
    return path;
}

class Recorder {
public:
    Recorder(View3DPane* pane, double scale) : pane_(pane), canvas_(pane->canvas()), scale_(scale) {
        cursor_ = QPointF(pane->width() * 0.5, pane->height() * 0.5);
    }

    void say(const QString& caption) {
        caption_ = caption;
        hold(6);
    }
    void hold(int frames, int delayCs = 8) {
        for (int i = 0; i < frames; ++i) frame(delayCs);
    }

    // Where a point of the model is in the pane.
    QPointF at(const V3& p) const {
        QPointF s;
        canvas_->toScreen(p, s);
        return QPointF(canvas_->mapTo(pane_, s.toPoint()));
    }
    QPointF centreOf(QWidget* w) const { return QPointF(w->mapTo(pane_, w->rect().center())); }

    // THE POINTER, moved there over `steps` pictures - telling the picture where it is on the way, as moving the
    // mouse over it does.
    void moveTo(const QPointF& to, int steps = 10) {
        const QPointF from = cursor_;
        for (int i = 1; i <= steps; ++i) {
            const double u = static_cast<double>(i) / steps;
            const double e = u * u * (3.0 - 2.0 * u);
            cursor_ = from + (to - from) * e;
            hover();
            frame(4);
        }
    }
    void clickCanvas() {
        const QPointF local = canvasPoint();
        send(QEvent::MouseButtonPress, local, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, local, Qt::LeftButton, Qt::NoButton);
        ripple_ = 6;
        hold(7);
    }
    void clickAt(const V3& p, int steps = 12) {
        moveTo(at(p), steps);
        clickCanvas();
    }
    void press(QAbstractButton* b) {
        moveTo(centreOf(b), 12);
        ripple_ = 6;
        b->click();
        hold(7);
    }
    // A DRAG over the picture, with the button held: turning it (left) or moving it (right).
    void drag(Qt::MouseButton button, const QPointF& by, int steps = 24) {
        send(QEvent::MouseButtonPress, canvasPoint(), button, button);
        const QPointF from = cursor_;
        for (int i = 1; i <= steps; ++i) {
            cursor_ = from + by * (static_cast<double>(i) / steps);
            send(QEvent::MouseMove, canvasPoint(), Qt::NoButton, button);
            frame(4);
        }
        send(QEvent::MouseButtonRelease, canvasPoint(), button, Qt::NoButton);
        hold(3);
    }
    void wheel(int notches) {
        for (int i = 0; i < std::abs(notches); ++i) {
            const QPointF local = canvasPoint();
            QWheelEvent w(local, canvas_->mapToGlobal(local), QPoint(), QPoint(0, notches > 0 ? 120 : -120), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(canvas_, &w);
            hold(3);
        }
    }
    void key(Qt::Key k, const QString& said) {
        QKeyEvent down(QEvent::KeyPress, k, Qt::NoModifier);
        QApplication::sendEvent(canvas_, &down);
        key_ = said;
        keyFor_ = 12;
        hold(12);
    }
    // A spin box's value walked to a figure, the pointer on it.
    void spinTo(QDoubleSpinBox* s, double to, int steps = 10) {
        moveTo(centreOf(s), 12);
        const double from = s->value();
        for (int i = 1; i <= steps; ++i) {
            s->setValue(from + (to - from) * i / steps);
            frame(6);
        }
        hold(8);
    }
    void sliderTo(QSlider* s, int to, int steps = 24) {
        const QRect g = s->geometry();
        auto spot = [&](int v) {
            const double u = static_cast<double>(v - s->minimum()) / std::max(1, s->maximum() - s->minimum());
            return QPointF(s->parentWidget()->mapTo(pane_, QPoint(g.left() + static_cast<int>(u * g.width()), g.center().y())));
        };
        moveTo(spot(s->value()), 12);
        const int from = s->value();
        for (int i = 1; i <= steps; ++i) {
            s->setValue(from + (to - from) * i / steps);
            cursor_ = spot(s->value());
            frame(5);
        }
        hold(6);
    }
    void choose(QComboBox* box, int index) {
        moveTo(centreOf(box), 12);
        ripple_ = 6;
        box->setCurrentIndex(index);
        emit box->activated(index);
        hold(8);
    }

    bool save(const QString& path) {
        QString err;
        const bool ok = gif::write(path, frames_, &err);
        std::printf("%s: %zu pictures, %s\n", qPrintable(path), frames_.size(), ok ? "written" : qPrintable(err));
        frames_.clear();
        return ok;
    }

private:
    QPointF canvasPoint() const { return QPointF(canvas_->mapFrom(pane_, cursor_.toPoint())); }
    void send(QEvent::Type type, const QPointF& local, Qt::MouseButton b, Qt::MouseButtons held) {
        QMouseEvent e(type, local, canvas_->mapToGlobal(local), b, held, Qt::NoModifier);
        QApplication::sendEvent(canvas_, &e);
    }
    void hover() {
        if (canvas_->rect().contains(canvasPoint().toPoint())) send(QEvent::MouseMove, canvasPoint(), Qt::NoButton, Qt::NoButton);
    }

    void frame(int delayCs) {
        QApplication::processEvents();
        QImage img = pane_->grab().toImage().convertToFormat(QImage::Format_RGB32);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        // WHAT IS BEING DONE, along the top of the picture.
        if (!caption_.isEmpty()) {
            QFont f = p.font();
            f.setPixelSize(19);
            f.setBold(true);
            p.setFont(f);
            const QRect cr = canvas_->geometry();
            const QRect band(QPoint(canvas_->mapTo(pane_, QPoint(0, 0))), QSize(cr.width(), 40));
            p.fillRect(band, QColor(10, 14, 20, 215));
            p.setPen(QColor(255, 214, 102));
            p.drawText(band.adjusted(14, 0, -14, 0), Qt::AlignVCenter | Qt::AlignLeft, caption_);
        }
        // THE KEY STRUCK, in a key's shape.
        if (keyFor_ > 0) {
            --keyFor_;
            QFont f = p.font();
            f.setPixelSize(20);
            f.setBold(true);
            p.setFont(f);
            const QRect cr(canvas_->mapTo(pane_, QPoint(0, 0)), canvas_->size());
            const QRect k(cr.right() - 190, cr.bottom() - 70, 170, 50);
            p.setBrush(QColor(240, 240, 240));
            p.setPen(QPen(QColor(60, 60, 60), 2));
            p.drawRoundedRect(k, 8, 8);
            p.setPen(QColor(20, 20, 20));
            p.drawText(k, Qt::AlignCenter, key_);
        }
        // A RING WHERE IT CLICKED, growing.
        if (ripple_ > 0) {
            const double r = 8.0 + (6 - ripple_) * 4.0;
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(255, 80, 60, 60 + ripple_ * 30), 3));
            p.drawEllipse(cursor_, r, r);
            --ripple_;
        }
        // THE POINTER.
        QPainterPath arrow;
        const QPointF o = cursor_;
        arrow.moveTo(o);
        arrow.lineTo(o + QPointF(0, 22));
        arrow.lineTo(o + QPointF(6, 16));
        arrow.lineTo(o + QPointF(10.5, 25));
        arrow.lineTo(o + QPointF(14, 23.5));
        arrow.lineTo(o + QPointF(9.5, 14.5));
        arrow.lineTo(o + QPointF(17, 14.5));
        arrow.closeSubpath();
        p.setBrush(Qt::white);
        p.setPen(QPen(Qt::black, 1.5));
        p.drawPath(arrow);
        p.end();
        frames_.push_back({img.scaled(img.size() * scale_, Qt::KeepAspectRatio, Qt::SmoothTransformation), delayCs});
    }

    View3DPane* pane_;
    View3DCanvas* canvas_;
    double scale_;
    QPointF cursor_;
    QString caption_;
    QString key_;
    int keyFor_ = 0;
    int ripple_ = 0;
    std::vector<gif::Frame> frames_;
};

// A FRESH VIEW of the model, seen at an angle, nothing routed yet.
struct Scene {
    View3DPane pane;
    explicit Scene(const QString& model) {
        pane.setEnvironmentChoice(true);
        pane.resize(1100, 700);
        pane.show();
        pane.canvas()->setUseGpu(false);
        QApplication::processEvents();
        pane.loadEnvironment(model, true);
        pane.canvas()->viewFrom(QStringLiteral("iso"));
        pane.canvas()->fit();
        QApplication::processEvents();
    }
    template <typename T> T* child(const char* name) { return pane.findChild<T*>(QLatin1String(name)); }
};

// THE CONNECTORS AND THE WAY ROUND THE RIB, in the model's millimetres.
const V3 kOnDevice{160, 210, 30};
const V3 kOnBox{500, 120, 25};
const V3 kCorners[] = {{235, 210, 0}, {235, 350, 0}, {340, 350, 0}, {390, 45, 0}};

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const QString outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath();
    QDir().mkpath(outDir);
    const QString model = writeModel(QDir::tempPath());
    if (model.isEmpty()) return 1;
    const double scale = 0.72;
    int failed = 0;

    // ---- THE VIEW AND THE PLANE ---------------------------------------------------------------------------------
    {
        Scene s(model);
        Recorder r(&s.pane, scale);
        r.moveTo(r.at({300, 200, 40}), 6);
        r.say(QStringLiteral("Left-drag turns the view"));
        r.drag(Qt::LeftButton, QPointF(150, 30));
        r.drag(Qt::LeftButton, QPointF(-150, -30));
        r.say(QStringLiteral("Right-drag moves it - the wheel zooms"));
        r.drag(Qt::RightButton, QPointF(-90, 40), 18);
        r.wheel(-2);
        r.wheel(1);
        r.press(s.child<QAbstractButton>("view3dIso"));
        r.press(s.child<QAbstractButton>("view3dFit"));
        r.wheel(-1);
        r.say(QStringLiteral("A click lands on the face under the pointer"));
        r.press(s.child<QAbstractButton>("view3dToolBranch"));
        r.clickAt({100, 210, 100});
        r.clickAt({500, 170, 70});
        r.hold(10);
        r.say(QStringLiteral("Off the model it lands on the ground plane (Z = 0)"));
        r.clickAt({660, 200, 0});
        r.clickAt({300, 460, 0});
        r.hold(12);
        r.say(QStringLiteral("Cut it open: a section, and the slider moves its plane"));
        r.choose(s.child<QComboBox>("view3dSection"), 3);
        r.sliderTo(s.child<QSlider>("view3dSectionAt"), 450);
        r.say(QStringLiteral("A click goes through what is cut away"));
        r.clickAt({100, 210, 47});
        r.hold(14);
        r.choose(s.child<QComboBox>("view3dSection"), 0);
        r.hold(10);
        failed += !r.save(QDir(outDir).filePath(QStringLiteral("plane.gif")));
    }

    // ---- ROUTE A CABLE FROM CONNECTOR TO CONNECTOR --------------------------------------------------------------
    {
        Scene s(model);
        Recorder r(&s.pane, scale);
        r.say(QStringLiteral("1  Connector: click the face each connector sits on"));
        r.press(s.child<QAbstractButton>("view3dToolConnector"));
        r.clickAt(kOnDevice);
        r.clickAt(kOnBox);
        r.hold(8);
        r.say(QStringLiteral("2  Route: click the first connector"));
        r.press(s.child<QAbstractButton>("view3dToolRoute"));
        r.clickAt(kOnDevice);
        r.say(QStringLiteral("3  Click the faces it runs over - each click a corner"));
        for (const V3& c : kCorners) r.clickAt(c, 16);
        r.say(QStringLiteral("4  Click the other connector to finish"));
        r.clickAt(kOnBox, 16);
        r.hold(8);
        r.say(QStringLiteral("Its length is in the table"));
        r.moveTo(r.centreOf(s.child<QTableWidget>("view3dRoutes")), 14);
        r.hold(30);
        failed += !r.save(QDir(outDir).filePath(QStringLiteral("route.gif")));
    }

    // ---- BEND THE CABLE -----------------------------------------------------------------------------------------
    {
        Scene s(model);
        Recorder r(&s.pane, scale);
        // The connectors are put first, as in the last recording.
        r.press(s.child<QAbstractButton>("view3dToolConnector"));
        r.clickAt(kOnDevice, 2);
        r.clickAt(kOnBox, 2);
        r.say(QStringLiteral("Each corner you click is a bend"));
        r.press(s.child<QAbstractButton>("view3dToolRoute"));
        r.clickAt(kOnDevice);
        r.clickAt(kCorners[0], 14);
        r.clickAt({235, 300, 0}, 14);
        r.say(QStringLiteral("Backspace takes the last corner back"));
        r.key(Qt::Key_Backspace, QStringLiteral("Backspace"));
        r.say(QStringLiteral("...and lay it on from there"));
        for (size_t i = 1; i < std::size(kCorners); ++i) r.clickAt(kCorners[i], 14);
        r.clickAt(kOnBox, 14);
        r.hold(10);
        r.say(QStringLiteral("Bend R: Auto is 6 x the diameter - type a radius to bend wider"));
        r.spinTo(s.child<QDoubleSpinBox>("view3dRouteBend"), 90.0);
        r.say(QStringLiteral("Or tighter"));
        r.spinTo(s.child<QDoubleSpinBox>("view3dRouteBend"), 20.0);
        r.say(QStringLiteral("Ø: the cable's diameter"));
        r.spinTo(s.child<QDoubleSpinBox>("view3dRouteDiameter"), 16.0, 1);
        r.say(QStringLiteral("Undo takes a change back"));
        r.press(s.child<QAbstractButton>("view3dRouteUndo"));
        r.hold(20);
        failed += !r.save(QDir(outDir).filePath(QStringLiteral("bend.gif")));
    }
    QFile::remove(model);
    return failed;
}
