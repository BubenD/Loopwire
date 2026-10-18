// THE 3D VIEW'S HELP, RECORDED. Builds a small model to route in, opens the real 3D view on it, and drives it the
// way a person would - the pointer moving, buttons pressed, faces clicked, keys struck - grabbing the view as it
// goes. Over each picture: the pointer, a ring where it clicks, the key pressed, and a line saying what is being
// done. Written as the animated GIFs the help shows, into resources/help3d.
//
//   cmake --build build --config Release --target loopwire_help3d_gifs
//   build\Release\loopwire_help3d_gifs.exe <repo>\resources\help3d
//
// Run on the screen (not the offscreen platform) with Qt's binaries on PATH. Recorded from the program itself, the
// help cannot show the view doing something it does not do.
#include "app/View3DPane.h"
#include "app/MainWindow.h"
#include "app/DrawingPreview.h"
#include "io/Dwx.h"
#include "app/NavWheel.h"
#include "io/GifWriter.h"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSlider>
#include <QSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QSettings>
#include <QTemporaryDir>
#include <QListWidget>
#include <QTextStream>
#include <QWheelEvent>

#include <cmath>
#include <cstdio>
#include <functional>
#include <iterator>

using namespace Loopwire;

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
    Recorder(View3DPane* pane, double scale) : pane_(pane), root_(pane->window()), canvas_(pane->canvas()), scale_(scale) {
        cursor_ = QPointF(root_->width() * 0.5, root_->height() * 0.5);
    }

    void say(const QString& caption) {
        std::printf("Demo: %s\n",qPrintable(caption));
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
        return QPointF(canvas_->mapTo(root_, s.toPoint()));
    }
    QPointF centreOf(QWidget* w) const { return QPointF(w->mapTo(root_, w->rect().center())); }

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
        if(!b)return;
        moveTo(centreOf(b), 12);
        ripple_ = 6;
        b->click();
        hold(7);
    }
    // A DRAG over the picture, with the button held: turning it (left) or moving it (right).
    void drag(Qt::MouseButton button, const QPointF& by, int steps = 24, Qt::KeyboardModifiers mods = Qt::NoModifier) {
        send(QEvent::MouseButtonPress, canvasPoint(), button, button, mods);
        const QPointF from = cursor_;
        for (int i = 1; i <= steps; ++i) {
            cursor_ = from + by * (static_cast<double>(i) / steps);
            send(QEvent::MouseMove, canvasPoint(), Qt::NoButton, button, mods);
            frame(4);
        }
        send(QEvent::MouseButtonRelease, canvasPoint(), button, Qt::NoButton, mods);
        hold(3);
    }
    // Pictures taken or not: the setting up of a recording about something else is not part of it.
    void recording(bool on) { recording_ = on; }
    // A key held while dragging, shown as one.
    void showKey(const QString& said, int frames) {
        key_ = said;
        keyFor_ = frames;
    }
    void spinTo(QSpinBox* s, int to, int steps = 10) {
        moveTo(centreOf(s), 12);
        const int from = s->value();
        for (int i = 1; i <= steps; ++i) {
            s->setValue(from + (to - from) * i / steps);
            frame(6);
        }
        hold(8);
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
            return QPointF(s->parentWidget()->mapTo(root_, QPoint(g.left() + static_cast<int>(u * g.width()), g.center().y())));
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
    // A SEGMENT OF THE WHEEL, clicked: the pointer to it, and a press and a release on it.
    void clickWheel(NavWheel* wheel, int segment) {
        const QPointF local = wheel->segmentCentre(segment);
        moveTo(QPointF(wheel->mapTo(root_, local.toPoint())), 12);
        QMouseEvent down(QEvent::MouseButtonPress, local, wheel->mapToGlobal(local), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent up(QEvent::MouseButtonRelease, local, wheel->mapToGlobal(local), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(wheel, &down);
        QApplication::sendEvent(wheel, &up);
        ripple_ = 6;
        hold(8);
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
    QPointF canvasPoint() const { return QPointF(canvas_->mapFrom(root_, cursor_.toPoint())); }
    void send(QEvent::Type type, const QPointF& local, Qt::MouseButton b, Qt::MouseButtons held,
              Qt::KeyboardModifiers mods = Qt::NoModifier) {
        QMouseEvent e(type, local, canvas_->mapToGlobal(local), b, held, mods);
        QApplication::sendEvent(canvas_, &e);
    }
    void hover() {
        if (canvas_->rect().contains(canvasPoint().toPoint())) send(QEvent::MouseMove, canvasPoint(), Qt::NoButton, Qt::NoButton);
    }

    void frame(int delayCs) {
        QApplication::processEvents();
        if (!recording_) return;
        QImage img = root_->grab().toImage().convertToFormat(QImage::Format_RGB32);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing, true);
        // Popup menus are separate native windows, so include them explicitly.
        for(auto* widget:QApplication::topLevelWidgets())if(widget->isVisible() && widget->windowType()==Qt::Popup)
            p.drawPixmap(root_->mapFromGlobal(widget->pos()),widget->grab());
        // Keep the ribbon visible in the fullscreen demonstration as context.
        if(pane_->isWindow() && pane_!=root_) {
            const QImage full=pane_->grab().toImage();
            const QRect target=img.rect().adjusted(0,150,0,-25);
            const QImage fitted=full.scaled(target.size(),Qt::KeepAspectRatio,Qt::SmoothTransformation);
            p.drawImage(QPoint(target.center().x()-fitted.width()/2,target.top()),fitted);
        }
        // WHAT IS BEING DONE, along the top of the picture.
        if (!caption_.isEmpty()) {
            QFont f = p.font();
            f.setPixelSize(19);
            f.setBold(true);
            p.setFont(f);
            const QRect cr = canvas_->geometry();
            const QRect band(QPoint(canvas_->mapTo(root_, QPoint(0, 0))), QSize(cr.width(), 40));
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
            const QRect cr(canvas_->mapTo(root_, QPoint(0, 0)), canvas_->size());
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
        // EVERY PICTURE THE SIZE OF THE FIRST: a view gone full screen is shown in the same frame.
        if (frames_.empty()) size_ = img.size() * scale_;
        frames_.push_back({img.scaled(size_, Qt::IgnoreAspectRatio, Qt::SmoothTransformation), delayCs});
    }

    View3DPane* pane_;
    QWidget* root_;
    View3DCanvas* canvas_;
    double scale_;
    QPointF cursor_;
    QString caption_;
    QString key_;
    int keyFor_ = 0;
    int ripple_ = 0;
    bool recording_ = true;
    QSize size_;
    std::vector<gif::Frame> frames_;
};

// A FRESH VIEW of the model, seen at an angle, nothing routed yet.
struct Scene {
    std::unique_ptr<MainWindow> owner;
    MainWindow& window;
    View3DPane& pane;
    static View3DPane& open(MainWindow& window) {
        std::fprintf(stderr,"Opening 3D Preview ribbon...\n");std::fflush(stderr);
        window.resize(2200,1000);window.show();QApplication::processEvents();
        for(auto* tabs:window.findChildren<QTabWidget*>())for(int i=0;i<tabs->count();++i)
            if(tabs->tabText(i)==QLatin1String("3D Preview")){tabs->setCurrentIndex(i);QApplication::processEvents();}
        auto* pane=window.findChild<View3DPane*>();if(!pane)qFatal("The 3D Preview tab did not create a view");return *pane;
    }
    explicit Scene(const QString& model) : owner(std::make_unique<MainWindow>()),window(*owner),pane(open(window)) {
        pane.setEnvironmentChoice(true);
        if(auto* shown=window.findChild<QAbstractButton*>(QStringLiteral("view3dShowPropertiesPanel"));shown && !shown->isChecked())pane.showPropertiesPanel();
        if(auto* shown=window.findChild<QAbstractButton*>(QStringLiteral("view3dShowPartsPanel"));shown && !shown->isChecked())pane.showPartsPanel();
        if(auto* pin=window.findChild<QAbstractButton*>(QStringLiteral("view3dRoutePin")))pin->setChecked(true);
        if(auto* pin=window.findChild<QAbstractButton*>(QStringLiteral("view3dPartsPin")))pin->setChecked(true);
        pane.canvas()->setUseGpu(false);
        QApplication::processEvents();
        pane.loadEnvironment(model, true);
        // Every tutorial starts shaded, independently of the Display recording.
        if(auto* wire=window.findChild<QAbstractButton*>(QStringLiteral("view3dWireframe")))wire->setChecked(false);
        pane.canvas()->setWireframe(false);
        if(auto* gravity=window.findChild<QAbstractButton*>(QStringLiteral("view3dGravity")))gravity->setChecked(false);
        if(auto* opacity=window.findChild<QSlider*>(QStringLiteral("view3dEnvOpacity")))opacity->setValue(100);
        pane.canvas()->viewFrom(QStringLiteral("iso"));
        pane.canvas()->fit();
        QApplication::processEvents();
    }
    template <typename T> T* child(const char* name) {
        QString mirrored=QLatin1String(name);mirrored.replace(QStringLiteral("view3dTool"),QStringLiteral("preview3d"));
        mirrored.replace(QStringLiteral("view3d"),QStringLiteral("preview3d"));
        mirrored.replace(QStringLiteral("RouteDiameter"),QStringLiteral("Diameter"));
        mirrored.replace(QStringLiteral("RouteWires"),QStringLiteral("Wires"));
        mirrored.replace(QStringLiteral("RouteWireD"),QStringLiteral("WireD"));
        if(auto* control=window.findChild<T*>(mirrored);control && control->isVisible())return control;
        auto* control=window.findChild<T*>(QLatin1String(name));
        if(!control)qFatal("Help control missing: %s",name);
        return control;
    }
};

// THE CONNECTORS AND THE WAY ROUND THE RIB, in the model's millimetres.
const V3 kOnDevice{160, 210, 30};
const V3 kOnBox{500, 120, 25};
const V3 kOnBoxSide{440, 175, 35};
const V3 kCorners[] = {{235, 210, 0}, {235, 350, 0}, {340, 350, 0}, {390, 45, 0}};

// Where a node of the route is drawn - what a click on it has to hit - by its label.
V3 nodePoint(const View3DPane& pane, const QString& label) {
    const Route3D& r = pane.route();
    for (const RouteNode3D& n : r.nodes)
        if (n.label == label)
            return n.kind == RouteNodeKind::Connector ? n.at + n.normal * (r.lead * 0.5) : n.at + n.normal * (r.diameter * 0.5);
    return {};
}

// A point on the first cable, `u` of the way along it.
V3 cablePoint(const View3DPane& pane, double u) {
    const Route3D& r = pane.route();
    if (r.segments.empty()) return {};
    const std::vector<V3> c = r.centreline(r.segments.front());
    double total = 0.0;
    for (size_t i = 1; i < c.size(); ++i) total += (c[i] - c[i - 1]).length();
    double want = total * u, acc = 0.0;
    for (size_t i = 1; i < c.size(); ++i) {
        const double seg = (c[i] - c[i - 1]).length();
        if (acc + seg >= want && seg > 1e-9) return c[i - 1] + (c[i] - c[i - 1]) * ((want - acc) / seg);
        acc += seg;
    }
    return c.empty() ? V3{} : c.back();
}

// THE TWO CONNECTORS AND A CABLE BETWEEN THEM, put quickly - the start of a recording about something else.
void quickRoute(Recorder& r, Scene& s, bool cable) {
    r.recording(false);
    r.press(s.child<QAbstractButton>("view3dToolConnector"));
    r.clickAt(kOnDevice, 3);
    r.clickAt(kOnBox, 3);
    if (!cable) {
        r.recording(true);
        return;
    }
    r.press(s.child<QAbstractButton>("view3dToolRoute"));
    r.clickAt(kOnDevice, 3);
    for (const V3& c : kCorners) r.clickAt(c, 3);
    r.clickAt(kOnBox, 3);
    r.recording(true);
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    QApplication app(argc, argv);
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    QCoreApplication::setOrganizationName(QStringLiteral("LoopwireHelpRecorder"));
    QCoreApplication::setApplicationName(QStringLiteral("3D ribbon demonstrations"));
    const QString outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath();
    QDir().mkpath(outDir);
    QTemporaryDir modelDirectory;
    const QString model = writeModel(modelDirectory.path());
    if (model.isEmpty()) return 1;
    const double scale = 0.52;
    int failed = 0;
    const QStringList only=argc>2 ? QString::fromLocal8Bit(argv[2]).split(',') : QStringList{};
    auto wanted=[&](std::initializer_list<const char*> names){
        if(only.isEmpty())return true;
        for(const char* name:names)if(only.contains(QLatin1String(name)))return true;
        return false;
    };

    // ---- THE VIEW AND THE PLANE ---------------------------------------------------------------------------------
    if(wanted({"plane"})) {
        Scene s(model);
        Recorder r(&s.pane, scale);
        r.moveTo(r.at({300, 200, 40}), 6);
        r.say(QStringLiteral("Hold the wheel button and drag: the view turns"));
        r.drag(Qt::MiddleButton, QPointF(150, 30));
        r.drag(Qt::MiddleButton, QPointF(-150, -30));
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
    if(wanted({"route"})) {
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
    if(wanted({"bend"})) {
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
    // ---- GETTING STARTED: THE WHOLE OF IT ONCE --------------------------------------------------------------------
    if(wanted({"start"})) {
        Scene s(model);
        Recorder r(&s.pane, scale);
        r.say(QStringLiteral("1  The model: Environment - Load... (here, the example)"));
        r.moveTo(r.centreOf(s.child<QAbstractButton>("view3dEnvLoad")), 14);
        r.hold(14);
        r.say(QStringLiteral("2  Connector: click the face each end sits on"));
        r.press(s.child<QAbstractButton>("view3dToolConnector"));
        r.clickAt(kOnDevice);
        r.clickAt(kOnBox);
        r.say(QStringLiteral("3  Route: first connector, the corners, the other connector"));
        r.press(s.child<QAbstractButton>("view3dToolRoute"));
        r.clickAt(kOnDevice);
        for (const V3& c : kCorners) r.clickAt(c, 12);
        r.clickAt(kOnBox, 12);
        r.say(QStringLiteral("4  Its length is in the table"));
        r.moveTo(r.centreOf(s.child<QTableWidget>("view3dRoutes")), 14);
        r.hold(30);
        failed += !r.save(QDir(outDir).filePath(QStringLiteral("start.gif")));
    }

    // ---- THE VIEW, ALL THE WAY ROUND ------------------------------------------------------------------------------
    if(wanted({"view"})) {
        Scene s(model);
        Recorder r(&s.pane, scale);
        r.moveTo(r.at({300, 200, 40}), 6);
        r.say(QStringLiteral("Wheel button held, drag left and right: it turns - all the way round"));
        for (int i = 0; i < 3; ++i) {
            r.drag(Qt::MiddleButton, QPointF(300, 0), 20);
            r.moveTo(r.at({300, 200, 40}) - QPointF(150, 0), 6);
        }
        r.say(QStringLiteral("Drag up and down: it tilts - over the top and underneath"));
        r.moveTo(r.at({300, 200, 40}) - QPointF(0, 150), 8);
        for (int i = 0; i < 3; ++i) {
            r.drag(Qt::MiddleButton, QPointF(0, 300), 20);
            r.moveTo(r.at({300, 200, 40}) - QPointF(0, 150), 6);
        }
        r.press(s.child<QAbstractButton>("view3dIso"));
        r.say(QStringLiteral("Shift + wheel-button drag: it rolls round the way it looks"));
        r.moveTo(r.at({300, 200, 40}) - QPointF(150, 0), 8);
        r.showKey(QStringLiteral("Shift"), 26);
        r.drag(Qt::MiddleButton, QPointF(300, 0), 24, Qt::ShiftModifier);
        r.say(QStringLiteral("Turn, Tilt and Roll: the same, in degrees"));
        r.spinTo(s.child<QSpinBox>("view3dRoll"), 0, 6);
        r.spinTo(s.child<QSpinBox>("view3dTurn"), 120, 10);
        r.spinTo(s.child<QSpinBox>("view3dTilt"), -30, 10);
        r.say(QStringLiteral("Iso, Top, Front, Side bring it back"));
        r.press(s.child<QAbstractButton>("view3dTop"));
        r.hold(6);
        r.press(s.child<QAbstractButton>("view3dIso"));
        r.hold(14);
        failed += !r.save(QDir(outDir).filePath(QStringLiteral("view.gif")));
    }

    // ---- ROUTING UP AND DOWN: THE WORK PLANE ----------------------------------------------------------------------
    if(wanted({"workplane"})) {
        Scene s(model);
        Recorder r(&s.pane, scale);
        quickRoute(r, s, false);
        r.say(QStringLiteral("Route: start at the connector"));
        r.press(s.child<QAbstractButton>("view3dToolRoute"));
        r.clickAt(kOnDevice);
        r.say(QStringLiteral("Clicks land on: a level plane - set its height"));
        r.choose(s.child<QComboBox>("view3dWorkPlane"), 3);
        r.spinTo(s.child<QDoubleSpinBox>("view3dWorkPlaneAt"), 150.0, 12);
        r.say(QStringLiteral("Each click lands at that height - in the air"));
        r.clickAt({230, 210, 150}, 14);
        r.clickAt({330, 300, 150}, 14);
        r.clickAt({470, 300, 150}, 14);
        r.say(QStringLiteral("An upright plane: clicks go up and down it"));
        r.choose(s.child<QComboBox>("view3dWorkPlane"), 2);
        r.spinTo(s.child<QDoubleSpinBox>("view3dWorkPlaneAt"), 60.0, 10);
        r.clickAt({500, 60, 120}, 14);
        r.clickAt({500, 60, 40}, 14);
        r.say(QStringLiteral("Back to the model's faces - click the connector"));
        r.choose(s.child<QComboBox>("view3dWorkPlane"), 0);
        r.clickAt(kOnBox, 14);
        r.hold(26);
        failed += !r.save(QDir(outDir).filePath(QStringLiteral("workplane.gif")));
    }

    // ---- CONNECTORS: PUTTING AND TURNING ---------------------------------------------------------------------------
    if(wanted({"connector"})) {
        Scene s(model);
        Recorder r(&s.pane, scale);
        quickRoute(r, s, true);
        // Close in on it: a connector is small beside the model.
        s.pane.canvas()->fitBox({90, 150, 0}, {300, 290, 90});
        r.say(QStringLiteral("Select the connector"));
        r.press(s.child<QAbstractButton>("view3dToolSelect"));
        r.clickAt(nodePoint(s.pane, QStringLiteral("X1")));
        r.hold(6);
        r.say(QStringLiteral("Points: along an axis - here, up"));
        r.choose(s.child<QComboBox>("view3dConnAim"), 5);
        r.hold(8);
        r.say(QStringLiteral("Turned and Tilted: point it any way at all"));
        r.spinTo(s.child<QSpinBox>("view3dConnTurn"), 45, 8);
        r.spinTo(s.child<QSpinBox>("view3dConnTilt"), 20, 8);
        r.say(QStringLiteral("Spun: round its own axis - R, a quarter turn"));
        r.moveTo(r.at(nodePoint(s.pane, QStringLiteral("X1"))) + QPointF(40, 40), 10);
        r.key(Qt::Key_R, QStringLiteral("R"));
        r.key(Qt::Key_R, QStringLiteral("R"));
        r.say(QStringLiteral("Points: out of its face - as it was put"));
        r.choose(s.child<QComboBox>("view3dConnAim"), 0);
        r.hold(20);
        failed += !r.save(QDir(outDir).filePath(QStringLiteral("connector.gif")));
    }

    // ---- BRANCH -----------------------------------------------------------------------------------------------------
    if(wanted({"branch"})) {
        Scene s(model);
        Recorder r(&s.pane, scale);
        quickRoute(r, s, true);
        r.recording(false);
        r.press(s.child<QAbstractButton>("view3dToolConnector"));
        r.clickAt(kOnBoxSide, 3);
        r.recording(true);
        r.say(QStringLiteral("Branch: click the cable where it divides"));
        r.press(s.child<QAbstractButton>("view3dToolBranch"));
        r.clickAt(cablePoint(s.pane, 0.72));
        r.hold(6);
        r.say(QStringLiteral("Route: from the branch to the next connector"));
        r.press(s.child<QAbstractButton>("view3dToolRoute"));
        r.clickAt(nodePoint(s.pane, QStringLiteral("B1")));
        r.clickAt(nodePoint(s.pane, QStringLiteral("X3")), 16);
        r.hold(6);
        r.say(QStringLiteral("Each piece has its own length"));
        r.moveTo(r.centreOf(s.child<QTableWidget>("view3dRoutes")), 14);
        r.hold(30);
        failed += !r.save(QDir(outDir).filePath(QStringLiteral("branch.gif")));
    }

    // ---- CLIP ---------------------------------------------------------------------------------------------------------
    if(wanted({"clip"})) {
        Scene s(model);
        Recorder r(&s.pane, scale);
        quickRoute(r, s, true);
        r.say(QStringLiteral("Clip: click the cable where it is held"));
        r.press(s.child<QAbstractButton>("view3dToolClip"));
        r.clickAt(cablePoint(s.pane, 0.35));
        r.clickAt(cablePoint(s.pane, 0.6), 14);
        r.hold(6);
        r.say(QStringLiteral("Each piece's length: to the clip, clip to clip"));
        r.moveTo(r.centreOf(s.child<QTableWidget>("view3dRoutes")), 14);
        r.hold(30);
        failed += !r.save(QDir(outDir).filePath(QStringLiteral("clip.gif")));
    }

    // ---- LOCK VIEW: THE WHEEL, AND MOVING A CABLE AND A CONNECTOR -----------------------------------------------
    if(wanted({"lock"})) {
        Scene s(model);
        Recorder r(&s.pane, scale);
        quickRoute(r, s, true);
        auto* wheel = s.pane.findChild<NavWheel*>(QStringLiteral("view3dWheel"));
        r.say(QStringLiteral("The wheel: each click turns, tilts or zooms the view"));
        r.clickWheel(wheel, 3);
        r.clickWheel(wheel, 3);
        r.clickWheel(wheel, 0);
        r.clickWheel(wheel, 4);
        r.say(QStringLiteral("Lock: the view held still"));
        r.clickWheel(wheel, 5);
        r.say(QStringLiteral("Drag the cable: a corner is put there, and it bends"));
        r.moveTo(r.at(cablePoint(s.pane, 0.62)), 14);
        r.drag(Qt::LeftButton, QPointF(-70, -60), 26);
        r.hold(6);
        r.say(QStringLiteral("Drag a connector: its cable comes with it"));
        r.moveTo(r.at(nodePoint(s.pane, QStringLiteral("X2"))), 14);
        r.drag(Qt::LeftButton, QPointF(40, 30), 22);
        r.hold(6);
        r.say(QStringLiteral("Undo takes a move back; the padlock unlocks"));
        r.press(s.child<QAbstractButton>("view3dRouteUndo"));
        r.clickWheel(wheel, 5);
        r.hold(16);
        failed += !r.save(QDir(outDir).filePath(QStringLiteral("lock.gif")));
    }

    // ---- FULL SCREEN ---------------------------------------------------------------------------------------------------
    if(wanted({"fullscreen"})) {
        Scene s(model);
        Recorder r(&s.pane, scale);
        quickRoute(r, s, true);
        r.say(QStringLiteral("Full screen: the view in a window of its own, filling the screen"));
        r.press(s.child<QAbstractButton>("view3dFullScreen"));
        r.hold(24);
        r.say(QStringLiteral("Every row and the panel are there - Back (or F11) returns it"));
        r.press(s.child<QAbstractButton>("view3dFullScreen"));
        r.hold(16);
        failed += !r.save(QDir(outDir).filePath(QStringLiteral("fullscreen.gif")));
    }

    // ---- MEASURING ----------------------------------------------------------------------------------------------------
    if(wanted({"measure"})) {
        Scene s(model);
        Recorder r(&s.pane, scale);
        r.say(QStringLiteral("Measure: click one end, then the other"));
        r.press(s.child<QAbstractButton>("view3dToolMeasure"));
        r.clickAt({100, 210, 100});
        r.clickAt({500, 170, 70}, 16);
        r.say(QStringLiteral("Straight, along X, Y, Z and across each plane - at the foot"));
        r.hold(26);
        r.say(QStringLiteral("Surface to surface: a face, then the face opposite"));
        r.choose(s.child<QComboBox>("view3dMeasureMode"), 1);
        r.clickAt({350, 330, 0});
        r.clickAt({120, 200, 100}, 16);
        r.say(QStringLiteral("The gap between them, square to the faces - 100 mm"));
        r.hold(30);
        failed += !r.save(QDir(outDir).filePath(QStringLiteral("measure.gif")));
    }

    // ---- MOVING AND PULLING: THE LENGTH KEPT; STRETCH ----------------------------------------------------------------
    if(wanted({"pull"})) {
        Scene s(model);
        Recorder r(&s.pane, scale);
        quickRoute(r, s, true);
        r.say(QStringLiteral("Move: drag a connector - its cable keeps its length"));
        r.press(s.child<QAbstractButton>("view3dToolMove"));
        r.moveTo(r.at(nodePoint(s.pane, QStringLiteral("X2"))), 14);
        r.drag(Qt::LeftButton, QPointF(-60, 50), 26);
        r.hold(10);
        r.say(QStringLiteral("Stretch: take hold of the cable and pull"));
        r.press(s.child<QAbstractButton>("view3dToolStretch"));
        r.moveTo(r.at(cablePoint(s.pane, 0.8)), 14);
        r.drag(Qt::LeftButton, QPointF(40, -30), 26);
        r.say(QStringLiteral("Pulled tight through its connector - the rest cut off"));
        r.hold(20);
        r.say(QStringLiteral("Undo takes each pull back"));
        r.press(s.child<QAbstractButton>("view3dRouteUndo"));
        r.hold(16);
        failed += !r.save(QDir(outDir).filePath(QStringLiteral("pull.gif")));
    }

    // ---- CONNECTORS: FORMS ------------------------------------------------------------------------------------------
    if(wanted({"connforms"})) {
        Scene s(model);
        Recorder r(&s.pane, scale);
        r.say(QStringLiteral("The arrow on Connector chooses the form - here, cylindrical"));
        s.pane.setPlaceForm(QStringLiteral("round"));
        r.press(s.child<QAbstractButton>("view3dToolConnector"));
        r.clickAt(kOnDevice);
        r.say(QStringLiteral("A D-sub shell"));
        s.pane.setPlaceForm(QStringLiteral("dsub"));
        r.clickAt(kOnBox, 16);
        r.hold(8);
        s.pane.canvas()->fitBox({90, 150, 0}, {300, 290, 90});
        r.say(QStringLiteral("Select one put already: its form on the right"));
        r.press(s.child<QAbstractButton>("view3dToolSelect"));
        r.clickAt(nodePoint(s.pane, QStringLiteral("X1")));
        r.choose(s.child<QComboBox>("view3dConnForm"), 2);
        r.hold(10);
        r.choose(s.child<QComboBox>("view3dConnForm"), 3);
        r.hold(20);
        failed += !r.save(QDir(outDir).filePath(QStringLiteral("connforms.gif")));
    }

    // ---- DISPLAY: BACKGROUND AND SHADING -----------------------------------------------------------------------------
    if(wanted({"display"})) {
        Scene s(model);
        Recorder r(&s.pane, scale);
        quickRoute(r, s, true);
        QMenu menu;
        s.pane.fillDisplayMenu(&menu);
        // Each choice, in the menu and its submenus - all but one asking for a colour.
        std::vector<QAction*> shown;
        std::function<void(QMenu*)> walk = [&](QMenu* m) {
            for (QAction* a : m->actions()) {
                if (a->menu()) walk(a->menu());
                else if (!a->isSeparator() && !a->text().contains(QLatin1String("..."))) shown.push_back(a);
            }
        };
        walk(&menu);
        for (QAction* a : shown) {
            r.say(QStringLiteral("Display: ") + QString(a->text()).remove(QLatin1Char('&')));
            a->trigger();
            r.hold(10);
        }
        if (!shown.empty()) shown.front()->trigger();
        r.hold(8);
        failed += !r.save(QDir(outDir).filePath(QStringLiteral("display.gif")));
    }

    // ---- TENSION AND GRAVITY ----------------------------------------------------------------------------------------
    if(wanted({"gravity"})) {
        Scene s(model);
        Recorder r(&s.pane, scale);
        quickRoute(r, s, false);
        // A cable through the air, from the device to the box, at a height.
        r.recording(false);
        r.press(s.child<QAbstractButton>("view3dToolRoute"));
        r.clickAt(kOnDevice, 2);
        r.choose(s.child<QComboBox>("view3dWorkPlane"), 3);
        s.child<QDoubleSpinBox>("view3dWorkPlaneAt")->setValue(120.0);
        r.clickAt({220, 210, 120}, 2);
        r.clickAt({470, 170, 120}, 2);
        r.choose(s.child<QComboBox>("view3dWorkPlane"), 0);
        r.clickAt(kOnBox, 2);
        r.press(s.child<QAbstractButton>("view3dToolSelect"));
        r.recording(true);
        r.say(QStringLiteral("Gravity: each cable hung under its weight - where it falls, in red"));
        r.press(s.child<QAbstractButton>("view3dGravity"));
        r.hold(20);
        r.say(QStringLiteral("Click the cable, then its own Tension: Loose - it sags more"));
        r.clickAt(cablePoint(s.pane, 0.5));
        r.choose(s.child<QComboBox>("view3dCableTension"), 1);
        r.hold(20);
        r.say(QStringLiteral("Taut - it hardly sags"));
        r.choose(s.child<QComboBox>("view3dCableTension"), 5);
        r.hold(20);
        r.say(QStringLiteral("On the floor a cable stays as it is laid - only what is in the air falls"));
        r.choose(s.child<QComboBox>("view3dCableTension"), 1);
        r.hold(26);
        failed += !r.save(QDir(outDir).filePath(QStringLiteral("gravity.gif")));
    }

    // Linking changes the association, never the physical 2D drawing.
    if(wanted({"link","unlink"})) {
        Scene s(model);Recorder r(&s.pane,scale);quickRoute(r,s,true);
        RouteHarness harness;harness.ok=true;harness.name=QStringLiteral("Example harness");
        harness.design.addBrick(BrickType::End,PartKind::Connector,0,0);
        s.pane.setHarnessLink([&]{return harness;},{});
        r.press(s.child<QAbstractButton>("view3dToolSelect"));r.clickAt(nodePoint(s.pane,QStringLiteral("X1")));
        auto* parts=s.window.findChild<QListWidget*>(QStringLiteral("view3dParts"));parts->setCurrentRow(0);
        r.say(QStringLiteral("Link assigns the selected harness part to this 3D connector"));
        r.press(s.child<QAbstractButton>("view3dLinkPart"));r.hold(18);
        failed+=!r.save(QDir(outDir).filePath(QStringLiteral("link.gif")));
        r.say(QStringLiteral("Unlink removes the assignment; the connector and 2D part remain"));
        r.press(s.child<QAbstractButton>("view3dUnlinkPart"));r.hold(18);
        failed+=!r.save(QDir(outDir).filePath(QStringLiteral("unlink.gif")));
    }
    if(wanted({"keepouts"})) {
        Scene s(model);Recorder r(&s.pane,scale);quickRoute(r,s,true);
        r.say(QStringLiteral("Keep-out: add a zone to mark space the harness must avoid"));
        s.pane.addKeepOutZone({200,260,0},{250,300,60},false);r.hold(20);
        r.say(QStringLiteral("Show keep-out hides the markers; the routing rules still apply"));
        r.press(s.child<QAbstractButton>("view3dKeepOutsShown"));r.hold(12);
        r.press(s.child<QAbstractButton>("view3dKeepOutsShown"));r.hold(12);
        failed+=!r.save(QDir(outDir).filePath(QStringLiteral("keepouts.gif")));
    }
    if(wanted({"pinpoints"})) {
        Scene s(model);Recorder r(&s.pane,scale);quickRoute(r,s,true);
        s.pane.clearEnvironment(false);
        r.say(QStringLiteral("Stretch menu > Stretch by pinpoints: select the cable first"));
        const QString cable=s.pane.route().segments.front().id;
        s.pane.canvas()->selectSegment(cable);s.pane.setRouteTool(RouteTool::Pinpoints);
        r.choose(s.child<QComboBox>("view3dWorkPlane"),3);
        s.child<QDoubleSpinBox>("view3dWorkPlaneAt")->setValue(120);
        r.say(QStringLiteral("Click ordered ball-and-flag points from the first end to the second"));
        r.clickAt({230,320,120});r.clickAt({390,310,120});r.hold(16);
        r.say(QStringLiteral("Enter or Stretch pulls the cable through every flag; fixed length is checked"));
        r.key(Qt::Key_Return,QStringLiteral("Enter"));r.hold(22);
        failed+=!r.save(QDir(outDir).filePath(QStringLiteral("pinpoints.gif")));
    }
    if(wanted({"labels"})) {
        Scene s(model);Recorder r(&s.pane,scale);quickRoute(r,s,true);
        Route3D route=s.pane.route();auto& segment=route.segments.front();
        RouteLabel3D label;label.id=QStringLiteral("example-label");label.brick=QStringLiteral("label");label.text=QStringLiteral("CBL-001");label.length=45;label.clearShrink=true;
        segment.labels={label};QJsonObject saved=route.toJson();saved.insert(QStringLiteral("environment"),QJsonObject{{"path",model}});
        s.pane.setRoutingStore([&]{return saved;},[&](const QJsonObject& value){saved=value;});s.pane.reloadRouting(true);
        s.pane.canvas()->selectSegment(segment.id);emit s.pane.canvas()->selectionChanged(segment.id);
        r.say(QStringLiteral("Label properties: choose Flexible or Stiff; All applies to every label"));
        r.choose(s.child<QComboBox>("view3dLabelFlex"),1);
        r.sliderTo(s.child<QSlider>("view3dLabelStiffness"),70);
        r.say(QStringLiteral("Text always visible faces the viewer; uncheck to rotate with the harness"));
        r.press(s.child<QAbstractButton>("view3dLabelTextAlwaysVisible"));r.hold(16);
        r.press(s.child<QAbstractButton>("view3dLabelTextAlwaysVisible"));r.hold(16);
        failed+=!r.save(QDir(outDir).filePath(QStringLiteral("labels.gif")));
    }
    if(wanted({"drawing","commands"})) {
        Scene s(model);Recorder r(&s.pane,scale);quickRoute(r,s,true);
        r.say(QStringLiteral("Drawing contains Save routing, System link, Add drawings and Create 2D views"));
        auto* drawing=s.window.findChild<QAbstractButton*>(QStringLiteral("preview3dFoldDrawing"));r.press(drawing);r.hold(24);
        if(auto* popup=QApplication::activePopupWidget())popup->hide();
        r.say(QStringLiteral("Save routing saves 3D positions and lengths in the DWX file"));r.hold(20);
        r.say(QStringLiteral("Create 2D views exports visible outlines in Top, Front and Side planes"));r.hold(20);
        DrawingPreview projection(QStringLiteral("Top (XY): visible outlines"),&s.window);
        projection.setEntities(s.pane.canvas()->projectedDrawing(0,false));
        projection.setGeometry(QRect(s.pane.canvas()->mapTo(&s.window,QPoint()),s.pane.canvas()->size()));
        projection.show();projection.raise();r.hold(20);projection.hide();
        failed+=!r.save(QDir(outDir).filePath(QStringLiteral("drawing.gif")));
        r.say(QStringLiteral("Delete removes a selected cable or node; Undo and Redo restore route changes"));
        s.pane.canvas()->selectSegment(s.pane.route().segments.front().id);r.key(Qt::Key_Delete,QStringLiteral("Delete"));r.hold(16);
        s.pane.undoRoute();r.say(QStringLiteral("Undo restores the deleted cable"));r.hold(16);
        s.pane.redoRoute();r.say(QStringLiteral("Redo repeats the deletion"));r.hold(16);s.pane.undoRoute();
        r.say(QStringLiteral("Attach places the selected cable onto the environment faces"));
        r.press(s.child<QAbstractButton>("view3dAttach"));r.hold(18);
        failed+=!r.save(QDir(outDir).filePath(QStringLiteral("commands.gif")));
    }

    if(wanted({"system"})) {
        Scene s(model);Recorder r(&s.pane,scale);
        Document drawing;Entity wire;wire.id=QStringLiteral("help-system-wire");wire.type=EntityType::TwistedPair;
        wire.a={0,0};wire.b={300,0};wire.points={wire.a,wire.b};wire.meta=QJsonObject{{"twistStraight",true},{"twistWires",4},{"twistWireWidth",1.0}};
        drawing.entities()={wire};Route3D initial;addDrawingToRoute(initial,drawing.entities(),false);Route3D routed=initial;
        routed.segments.front().via={{{100,80,50},{0,0,1}},{{200,80,50},{0,0,1}}};
        QJsonObject saved=routed.toJson();saved.insert(QStringLiteral("initialHarnessRoute"),initial.toJson());drawing.routing3d()=saved;
        const QString first=QDir(outDir).filePath(QStringLiteral("Harness A.dwx")),second=QDir(outDir).filePath(QStringLiteral("Harness B.dwx"));QString error;
        if(!dwx::save(drawing,first,&error))qFatal("Could not create help feed: %s",qPrintable(error));
        for(auto& node:initial.nodes)node.at.y+=150;for(auto& node:routed.nodes)node.at.y+=150;
        for(auto& segment:routed.segments)for(auto& via:segment.via)via.at.y+=150;
        for(auto& entity:drawing.entities()){entity.a.y+=150;entity.b.y+=150;for(auto& p:entity.points)p.y+=150;}
        saved=routed.toJson();saved.insert(QStringLiteral("initialHarnessRoute"),initial.toJson());drawing.routing3d()=saved;
        if(!dwx::save(drawing,second,&error))qFatal("Could not create help feed: %s",qPrintable(error));
        r.say(QStringLiteral("Drawing > System link switches from this harness to the system routing"));
        s.pane.setSystemDesignLinked(true);r.hold(18);
        r.say(QStringLiteral("Add drawings: choose Initial to load a harness as it was drawn in 2D"));
        if(!s.pane.addSystemDrawing(first,false,&error))qFatal("Help system feed: %s",qPrintable(error));s.pane.canvas()->fit();r.hold(24);
        r.say(QStringLiteral("Choose Routed to use a drawing's saved 3D route; the feed table lists both drawings"));
        if(!s.pane.addSystemDrawing(second,true,&error))qFatal("Help routed feed: %s",qPrintable(error));s.pane.canvas()->fit();r.hold(28);
        failed+=!r.save(QDir(outDir).filePath(QStringLiteral("system.gif")));
    }
    if(wanted({"panels"})) {
        Scene s(model);Recorder r(&s.pane,scale);quickRoute(r,s,true);
        r.say(QStringLiteral("Show > Parts and Properties switch the side views on and off"));
        s.pane.showPartsPanel();r.hold(15);s.pane.showPartsPanel();r.hold(15);
        s.pane.showPropertiesPanel();r.hold(15);s.pane.showPropertiesPanel();r.hold(15);
        r.say(QStringLiteral("Eye hides a part; the half circle makes it transparent"));
        s.pane.canvas()->setEnvPartOpacity(1,.25);s.pane.refreshSolids();r.hold(22);
        s.pane.canvas()->setEnvPartHidden(1,true);s.pane.refreshSolids();r.hold(22);
        r.say(QStringLiteral("Show all restores hidden parts. Pin a side view to keep it open"));
        s.pane.canvas()->showAllParts();s.pane.refreshSolids();r.hold(20);
        failed+=!r.save(QDir(outDir).filePath(QStringLiteral("panels.gif")));
    }

    // THE EXAMPLE, for the help's "Open the example": the model all of the above were recorded on.
    QFile::remove(QDir(outDir).filePath(QStringLiteral("example.obj")));
    failed += !QFile::copy(model, QDir(outDir).filePath(QStringLiteral("example.obj")));
    QFile::remove(model);
    return failed;
}
