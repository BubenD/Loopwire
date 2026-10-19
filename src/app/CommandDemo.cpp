#include "app/CommandDemo.h"

#include "app/Editor.h"
#include "model/Document.h"
#include "model/Entity.h"
#include "render/CanvasWidget.h"

#include <QApplication>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QTimer>
#include <QUuid>

#include <algorithm>
#include <cmath>

namespace Loopwire {

namespace {

// WHAT A DEMONSTRATION STARTS FROM, put on its drawing.
QString freshId() { return QUuid::createUuid().toString(QUuid::WithoutBraces).left(8); }

QString addLine(Document& d, const Vec2& a, const Vec2& b) {
    Entity e;
    e.id = freshId();
    e.type = EntityType::Line;
    e.a = a;
    e.b = b;
    d.entities().push_back(e);
    return e.id;
}

QString addRect(Document& d, const Vec2& lo, const Vec2& hi) {
    Entity e;
    e.id = freshId();
    e.type = EntityType::Rect;
    e.points = {lo, {hi.x, lo.y}, hi, {lo.x, hi.y}};
    d.entities().push_back(e);
    return e.id;
}

QString addCircle(Document& d, const Vec2& c, double r) {
    Entity e;
    e.id = freshId();
    e.type = EntityType::Circle;
    e.center = c;
    e.radius = r;
    d.entities().push_back(e);
    return e.id;
}

DemoStep move(const Vec2& at, const char* say) { return {DemoStep::Move, at, 0, QString::fromUtf8(say)}; }
DemoStep click(const Vec2& at, const char* say) { return {DemoStep::Click, at, 0, QString::fromUtf8(say)}; }
DemoStep key(int k, const char* say) { return {DemoStep::Key, {}, k, QString::fromUtf8(say)}; }

// A RECTANGLE TO WORK ON, picked out first: the modify commands work on what is selected.
std::function<void(Document&, Editor&)> selectedBox() {
    return [](Document& d, Editor& ed) {
        const QString id = addRect(d, {40, 25}, {90, 60});
        ed.selectById(id);
    };
}

// EVERY DEMONSTRATION, in the order the command list has them. Places are in a drawing 200 across and 100 up.
const std::vector<CommandDemoScript>& scripts() {
    static const std::vector<CommandDemoScript> all = [] {
        std::vector<CommandDemoScript> s;
        auto add = [&](const char* command, const char* title, ToolId tool, std::vector<DemoStep> steps,
                       std::function<void(Document&, Editor&)> setup = {}) {
            s.push_back({QString::fromLatin1(command), QString::fromUtf8(title), std::move(setup), tool,
                         std::move(steps)});
        };
        // ---- Draw
        add("line", "Line: each click places a line, and the next goes on from its end; Space or Esc puts the tool down.",
            ToolId::Line,
            {move({25, 20}, "Line: the first point"), click({25, 20}, "Click: where it starts"),
             move({80, 80}, "The line follows the pointer"), click({80, 80}, "Click: a line placed - the next starts here"),
             move({130, 25}, "And on"), click({130, 25}, "Click: another line placed"),
             key(Qt::Key_Space, "Space: the lines stay, and the tool is put down")});
        add("pline", "Polyline: one object through every point; [A]rc, [L]ine, [W]idth, [C]lose, [U]ndo as it is drawn.",
            ToolId::Polyline,
            {move({25, 20}, "Polyline: the first point"), click({25, 20}, "Click: where it starts"),
             move({80, 80}, "To the next corner"), click({80, 80}, "Click: a corner"),
             move({130, 25}, "And the next"), click({130, 25}, "Click: another corner"),
             move({175, 70}, "The last point"), click({175, 70}, "Click: the last point"),
             key(Qt::Key_Space, "Space: the polyline is kept as drawn, and the tool put down")});
        add("dline", "Double line: two parallel lines through every point clicked; started on another object, both "
                     "start flush on it. Space or Esc keeps what is drawn.",
            ToolId::DoublePolyline,
            {move({30, 25}, "Double line: the first point"), click({30, 25}, "Click: the start"),
             move({100, 75}, "To a corner"), click({100, 75}, "Click: a corner"),
             move({170, 25}, "The last point"), click({170, 25}, "Click: the end"),
             key(Qt::Key_Escape, "Esc: kept as drawn, and the tool put down")});
        add("rectang", "Rectangle: click two opposite corners.", ToolId::Rect,
            {move({45, 20}, "Rectangle: one corner"), click({45, 20}, "Click: the first corner"),
             move({155, 80}, "The rectangle follows the pointer"), click({155, 80}, "Click: the opposite corner")});
        add("polygon", "Polygon: click its centre, then how far out its corners are.", ToolId::Polygon,
            {move({100, 50}, "Polygon: the centre"), click({100, 50}, "Click: the centre"),
             move({140, 50}, "Out to a corner - the size and the turn"), click({140, 50}, "Click: a corner")});
        add("circle", "Circle: click its centre, then a point on it.", ToolId::Circle,
            {move({100, 50}, "Circle: the centre"), click({100, 50}, "Click: the centre"),
             move({135, 50}, "Out to the size wanted"), click({135, 50}, "Click: the radius")});
        add("arc", "Arc: click where it starts, a point it passes through, and where it ends.", ToolId::Arc,
            {move({40, 30}, "Arc: where it starts"), click({40, 30}, "Click: the start"),
             move({100, 80}, "A point on the way round"), click({100, 80}, "Click: a point it passes through"),
             move({160, 30}, "Where it ends"), click({160, 30}, "Click: the end")});
        add("ellipse", "Ellipse: click the centre, the end of one axis, then how wide the other is.", ToolId::Ellipse,
            {move({100, 50}, "Ellipse: the centre"), click({100, 50}, "Click: the centre"),
             move({160, 50}, "To the end of the long axis"), click({160, 50}, "Click: one axis"),
             move({100, 72}, "Out to the other"), click({100, 72}, "Click: the other axis")});
        add("spline", "Spline: a smooth curve through every point clicked; Enter finishes it.", ToolId::Spline,
            {move({25, 30}, "Spline: the first point"), click({25, 30}, "Click: where it starts"),
             move({70, 80}, "The curve bends through each point"), click({70, 80}, "Click: a point"),
             move({120, 20}, "And the next"), click({120, 20}, "Click: a point"),
             move({175, 70}, "The last point"), click({175, 70}, "Click: the end"),
             key(Qt::Key_Return, "Enter: finish the curve")});
        add("dim", "Dimension: click the two points to measure, then where the dimension line goes.",
            ToolId::Dimension,
            {move({40, 30}, "Dimension: the first point to measure from"), click({40, 30}, "Click: one end"),
             move({160, 30}, "The other point"), click({160, 30}, "Click: the other end"),
             move({100, 62}, "Up to where the dimension line sits"), click({100, 62}, "Click: place it")},
            [](Document& d, Editor&) { addLine(d, {40, 30}, {160, 30}); });
        // ---- Annotation
        add("balloon", "Balloon: click what it points at, then where the balloon sits.", ToolId::Balloon,
            {move({65, 42}, "Balloon: the thing it calls out"), click({65, 42}, "Click: the arrow's tip"),
             move({125, 78}, "Out to where the number reads clearly"), click({125, 78}, "Click: the balloon")},
            [](Document& d, Editor&) { addRect(d, {40, 25}, {90, 60}); });
        add("leader", "Leader: click what it points at, then where the words go.", ToolId::TextLeader,
            {move({65, 42}, "Leader: the thing it points at"), click({65, 42}, "Click: the arrow's tip"),
             move({125, 78}, "Out to where the words go"), click({125, 78}, "Click: the words")},
            [](Document& d, Editor&) { addRect(d, {40, 25}, {90, 60}); });
        // ---- Harness
        add("wire", "Wire: click from one point to the next; Enter or Esc ends it.", ToolId::Wire,
            {move({30, 50}, "Wire: where it starts"), click({30, 50}, "Click: the start"),
             move({100, 50}, "Along"), click({100, 50}, "Click: a bend"),
             move({100, 80}, "Turn the corner"), click({100, 80}, "Click: the end"),
             key(Qt::Key_Return, "Enter: the wire is done")});
        add("joint", "Joint: click where wires are joined.", ToolId::Splice,
            {move({100, 50}, "Joint: where the wires meet"), click({100, 50}, "Click: the joint")},
            [](Document& d, Editor&) {
                addLine(d, {30, 50}, {170, 50});
                addLine(d, {100, 20}, {100, 50});
            });
        add("notconnected", "Not connected: click the end that goes nowhere.", ToolId::NotConnected,
            {move({150, 50}, "Not connected: the end that is left open"), click({150, 50}, "Click: mark it")},
            [](Document& d, Editor&) { addLine(d, {50, 50}, {150, 50}); });
        add("twistedpair", "Twisted pair: click where the pair starts and where it ends.", ToolId::TwistedPair,
            {move({30, 50}, "Twisted pair: where it starts"), click({30, 50}, "Click: the start"),
             move({170, 50}, "The pair follows the pointer"), click({170, 50}, "Click: the end")});
        add("twistedbundle", "Twisted bundle: click where the bundle starts and where it ends.", ToolId::TwistedHarness,
            {move({30, 50}, "Twisted bundle: where it starts"), click({30, 50}, "Click: the start"),
             move({170, 50}, "The bundle follows the pointer"), click({170, 50}, "Click: the end")});
        // ---- Modify
        add("move", "Move: with the objects selected, click a point on them, then where that point goes.",
            ToolId::Move,
            {move({40, 25}, "Move: the box is selected; its corner is the point to move by"),
             click({40, 25}, "Click: the base point"), move({110, 55}, "The box follows the pointer"),
             click({110, 55}, "Click: where the corner goes")},
            selectedBox());
        add("copy", "Copy: Ctrl+C takes the selection; Ctrl+V and a click puts a copy down.", ToolId::Select,
            {move({65, 42}, "The box is selected"), key(Qt::Key_C | Qt::CTRL, "Ctrl+C: copy it"),
             key(Qt::Key_V | Qt::CTRL, "Ctrl+V: paste - the copy is carried by the pointer"),
             move({140, 60}, "Take it where it goes"), click({140, 60}, "Click: put it down")},
            selectedBox());
        add("rotate", "Rotate: with the objects selected, click the pivot, then the angle they turn to (or type it).",
            ToolId::Rotate,
            {move({65, 42}, "Rotate: the box is selected; its middle is the pivot"),
             click({65, 42}, "Click: the pivot"), move({93, 70}, "Round to the angle wanted - the box turns with it"),
             click({93, 70}, "Click: the angle")},
            selectedBox());
        add("mirror", "Mirror: with the objects selected, click two points on the mirror line, then keep the "
                      "original or not.",
            ToolId::Mirror,
            {move({110, 15}, "Mirror: the box is selected; the mirror line starts here"),
             click({110, 15}, "Click: one end of the mirror line"), move({110, 85}, "The mirror image follows"),
             click({110, 85}, "Click: the other end - asked, keep the original: a mirrored copy")},
            selectedBox());
        add("scale", "Scale: with the objects selected, click a base point, then how big (or type a factor).",
            ToolId::Scale,
            {move({40, 25}, "Scale: the box is selected; scale it from its corner"),
             click({40, 25}, "Click: the base point"), move({140, 25}, "Out to the new size - here twice its width"),
             click({140, 25}, "Click: twice as big")},
            selectedBox());
        add("offset", "Offset: click two points for the distance (or type it), then the line, then the side.",
            ToolId::Offset,
            {move({30, 70}, "Offset: first the distance - one point"), click({30, 70}, "Click: one point"),
             move({30, 90}, "And another, that far away"), click({30, 90}, "Click: the distance is set"),
             move({100, 40}, "The line to copy alongside"), click({100, 40}, "Click: the line"),
             move({100, 60}, "To the side the copy goes"), click({100, 60}, "Click: the copy is made"),
             key(Qt::Key_Escape, "Esc: done")},
            [](Document& d, Editor&) { addLine(d, {30, 40}, {170, 40}); });
        add("trim", "Trim: click the line to trim to, then draw a short line across each part to cut away.",
            ToolId::Trim,
            {move({110, 75}, "Trim: first the line to trim to"), click({110, 75}, "Click: the cutting line"),
             move({145, 62}, "Then across the part to cut away"), click({145, 62}, "Click: start a line across it"),
             move({145, 38}, "Across it"), click({145, 38}, "Click: that part is cut away"),
             key(Qt::Key_Escape, "Esc: done")},
            [](Document& d, Editor&) {
                addLine(d, {30, 50}, {170, 50});
                addLine(d, {110, 15}, {110, 85});
            });
        add("extend", "Extend: click the line to extend to, then draw a short line across each line to lengthen.",
            ToolId::Extend,
            {move({150, 75}, "Extend: first the line to extend to"), click({150, 75}, "Click: the boundary"),
             move({70, 62}, "Then across the line that is short"), click({70, 62}, "Click: start a line across it"),
             move({70, 38}, "Across it"), click({70, 38}, "Click: it runs on to the boundary"),
             key(Qt::Key_Escape, "Esc: done")},
            [](Document& d, Editor&) {
                addLine(d, {30, 50}, {80, 50});
                addLine(d, {150, 15}, {150, 85});
            });
        add("fillet", "Fillet: click two lines; they are joined by an arc.", ToolId::Fillet,
            {move({100, 30}, "Fillet: the first line"), click({100, 30}, "Click: the first line"),
             move({150, 60}, "The second line"), click({150, 60}, "Click: the second line")},
            [](Document& d, Editor&) {
                addLine(d, {40, 30}, {150, 30});
                addLine(d, {150, 30}, {150, 85});
            });
        add("chamfer", "Chamfer: click two lines; the corner between them is cut off.", ToolId::Chamfer,
            {move({100, 30}, "Chamfer: the first line"), click({100, 30}, "Click: the first line"),
             move({150, 60}, "The second line"), click({150, 60}, "Click: the second line")},
            [](Document& d, Editor&) {
                addLine(d, {40, 30}, {150, 30});
                addLine(d, {150, 30}, {150, 85});
            });
        add("erase", "Erase: select what goes, then Delete.", ToolId::Select,
            {move({65, 25}, "The box is selected"), key(Qt::Key_Delete, "Delete: it is gone")},
            selectedBox());
        add("centermark", "Centre mark: click a circle; its centre is marked.", ToolId::CenterMark,
            {move({135, 50}, "Centre mark: the circle"), click({135, 50}, "Click: the circle")},
            [](Document& d, Editor&) { addCircle(d, {100, 50}, 35); });
        add("hatch", "Hatch: click inside a closed shape; it is filled with the hatch pattern.", ToolId::Hatch,
            {move({100, 50}, "Hatch: inside the shape to fill"), click({100, 50}, "Click: it is hatched")},
            [](Document& d, Editor&) { addRect(d, {40, 25}, {160, 75}); });
        add("lengthbreak", "Length break: draw a short line across a run; the break mark follows what it crosses.",
            ToolId::LengthBreak,
            {move({100, 70}, "Length break: above the run"), click({100, 70}, "Click: start the line across"),
             move({100, 30}, "Across the run"), click({100, 30}, "Click: the run is broken there")},
            [](Document& d, Editor&) {
                Entity run;
                run.id = freshId();
                run.type = EntityType::TwistedPair;
                run.points = {{30, 50}, {170, 50}};
                run.a = run.points.front();
                run.b = run.points.back();
                d.entities().push_back(run);
            });
        return s;
    }();
    return all;
}

// THE COMMAND'S OTHER NAMES, from the command list: the demonstration is asked for by whichever was typed.
const std::vector<QStringList>& aliases() {
    static const std::vector<QStringList> a = {
        {"line", "l"}, {"pline", "lp", "pl", "polyline"}, {"dline", "d", "dl", "ml"},
        {"dpline", "dp", "dpl", "mline"}, {"rectang", "rec", "rectangle", "rect"}, {"polygon", "pol"},
        {"circle", "c"}, {"arc", "a"}, {"ellipse", "el"}, {"spline", "spl"}, {"dim", "dimlinear"},
        {"balloon", "bl"}, {"leader", "ld", "mleader"}, {"wire", "w"}, {"joint", "splice"},
        {"notconnected", "nc"}, {"twistedpair", "tp", "pair"}, {"twistedbundle", "tbundle", "twb"},
        {"move", "m"}, {"copy", "co", "cp"}, {"rotate", "ro"}, {"mirror", "mi"}, {"scale", "sc"},
        {"offset", "o"}, {"trim", "tr"}, {"extend", "ex"}, {"fillet", "fi", "f"}, {"chamfer", "ch", "cha"},
        {"erase", "e", "delete", "del"}, {"centermark", "cm", "centremark"}, {"hatch", "h"},
        {"lengthbreak", "lb", "break"},
    };
    return a;
}

} // namespace

QStringList commandDemoNames() {
    QStringList out;
    for (const CommandDemoScript& s : scripts()) out << s.command;
    return out;
}

bool commandDemoFor(const QString& command, CommandDemoScript& out) {
    QString name = command.trimmed().toLower();
    for (const QStringList& names : aliases())
        if (names.contains(name)) {
            name = names.front();
            break;
        }
    for (const CommandDemoScript& s : scripts())
        if (s.command == name) {
            out = s;
            return true;
        }
    return false;
}

// ---- What is drawn over the canvas -------------------------------------------------------------------------

class CommandDemo::Overlay : public QWidget {
public:
    explicit Overlay(QWidget* parent) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
    }
    QPointF pointer{-100, -100};
    bool showPointer = false;
    double ring = 0.0;     // 0 none, else how far the click's ring has spread (0..1)
    QPointF ringAt;
    QString keyText;       // the key just struck
    QString say;           // what is being done

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        if (ring > 0.0) {
            QColor c(255, 190, 60);
            c.setAlphaF(std::clamp(1.0 - ring, 0.0, 1.0));
            p.setPen(QPen(c, 2.5));
            p.setBrush(Qt::NoBrush);
            const double r = 6.0 + 16.0 * ring;
            p.drawEllipse(ringAt, r, r);
        }
        if (showPointer) {
            // THE POINTER, as the system draws one: an arrow with its tip on the place.
            QPainterPath arrow;
            const QPointF t = pointer;
            arrow.moveTo(t);
            arrow.lineTo(t + QPointF(0, 17));
            arrow.lineTo(t + QPointF(4.5, 13));
            arrow.lineTo(t + QPointF(7.5, 20));
            arrow.lineTo(t + QPointF(10, 19));
            arrow.lineTo(t + QPointF(7, 12));
            arrow.lineTo(t + QPointF(12, 12));
            arrow.closeSubpath();
            p.setPen(QPen(Qt::black, 1.2));
            p.setBrush(Qt::white);
            p.drawPath(arrow);
        }
        QFont f = font();
        f.setPointSizeF(f.pointSizeF() + 0.5);
        p.setFont(f);
        const QFontMetrics fm(f);
        if (!keyText.isEmpty()) {
            // THE KEY, as a key cap at the top right.
            const int w = fm.horizontalAdvance(keyText) + 18;
            const QRectF cap(width() - w - 10, 10, w, fm.height() + 10);
            p.setPen(QPen(QColor(90, 96, 104), 1.2));
            p.setBrush(QColor(245, 245, 247));
            p.drawRoundedRect(cap, 5, 5);
            p.setPen(QColor(25, 25, 28));
            p.drawText(cap, Qt::AlignCenter, keyText);
        }
        if (!say.isEmpty()) {
            const QRectF band(0, height() - fm.height() - 14, width(), fm.height() + 14);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0, 0, 0, 170));
            p.drawRect(band);
            p.setPen(QColor(240, 240, 240));
            p.drawText(band.adjusted(10, 0, -10, 0), Qt::AlignVCenter | Qt::AlignLeft, say);
        }
    }
};

// ---- The demonstration -------------------------------------------------------------------------------------

CommandDemo::CommandDemo(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("commandDemo"));
    setMinimumSize(360, 220);
    doc_ = std::make_unique<Document>(QStringLiteral("demonstration"));
    editor_ = new Editor(this);
    editor_->setDocument(doc_.get());
    canvas_ = new CanvasWidget(this);
    canvas_->setDocument(doc_.get());
    canvas_->setEditor(editor_);
    canvas_->setGridVisible(false);
    // NO AXES AND NO DRAWING DETAILS PANEL: the panel sits over the top of the drawing, and a click the
    // demonstration made there went to the panel instead of the command.
    canvas_->setPlainCanvas(true);
    canvas_->installEventFilter(this);
    // MIRROR ASKS WHETHER TO KEEP THE ORIGINAL, and the main window puts the question. Here there is nobody
    // to ask: the demonstration keeps it, so the mirrored copy is seen beside what it mirrors.
    connect(editor_, &Editor::mirrorChoiceRequested, this, [this] { editor_->commitMirror(true); });
    overlay_ = new Overlay(this);
    overlay_->raise();
    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
    connect(timer_, &QTimer::timeout, this, [this] { next(); });
}

CommandDemo::~CommandDemo() {
    timer_->stop();
    // The canvas and the editor go before the drawing they work on, not after it with the other children.
    delete overlay_;
    delete canvas_;
    delete editor_;
}

bool CommandDemo::eventFilter(QObject* watched, QEvent* event) {
    // THE HAND IS KEPT OUT: what the user does over the demonstration does not reach its canvas, only what the
    // demonstration itself sends. Its events are sent straight to the canvas and are never spontaneous.
    if (watched == canvas_ && event->spontaneous()) {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove:
        case QEvent::Wheel:
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
        case QEvent::ContextMenu:
            return true;
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void CommandDemo::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    canvas_->setGeometry(rect());
    overlay_->setGeometry(rect());
    fitDrawing();
}

// THE DEMONSTRATION'S DRAWING, 200 across and 100 up, in the middle of the widget with room round it.
void CommandDemo::fitDrawing() {
    if (!doc_) return;
    const double w = std::max(1, width()), h = std::max(1, height() - 30);
    const double scale = std::min(w / 230.0, h / 125.0);
    ViewState& view = doc_->view();
    view.scale = scale;
    view.x = (w - 200.0 * scale) / 2.0;
    view.y = (h + 100.0 * scale) / 2.0;
    canvas_->syncEditorScale();
    canvas_->invalidateScene();
    canvas_->update();
}

QPointF CommandDemo::screenAt(const Vec2& world) const {
    ViewTransform vt;
    vt.view = doc_->view();
    return vt.toScreen(world);
}

void CommandDemo::sendMouse(int type, const QPointF& at, Qt::MouseButton button, Qt::MouseButtons held) {
    QMouseEvent e(static_cast<QEvent::Type>(type), at, canvas_->mapToGlobal(at), button, held, Qt::NoModifier);
    QApplication::sendEvent(canvas_, &e);
}

void CommandDemo::sendKey(int key) {
    const Qt::KeyboardModifiers mods = Qt::KeyboardModifiers(key & Qt::KeyboardModifierMask);
    const int bare = key & ~Qt::KeyboardModifierMask;
    QKeyEvent down(QEvent::KeyPress, bare, mods);
    QApplication::sendEvent(canvas_, &down);
    QKeyEvent up(QEvent::KeyRelease, bare, mods);
    QApplication::sendEvent(canvas_, &up);
}

void CommandDemo::build(const CommandDemoScript& script) {
    ticks_.clear();
    at_ = 0;
    // A clean start each time: an empty drawing, nothing selected, Select the tool.
    editor_->setTool(ToolId::Select);
    editor_->clearSelection();
    doc_->entities().clear();
    if (script.setup) script.setup(*doc_, *editor_);
    startCount_ = static_cast<int>(doc_->entities().size());
    fitDrawing();
    emit editor_->changed();
    // Started at the middle, off to the left, where a hand would come in from.
    auto pointerAt = std::make_shared<QPointF>(screenAt({10, 50}));
    const ToolId tool = script.tool;
    const QString title = script.title;
    ticks_.push_back({0, [this, tool, title, pointerAt] {
                          overlay_->say = title;
                          overlay_->keyText.clear();
                          overlay_->ring = 0.0;
                          overlay_->pointer = *pointerAt;
                          overlay_->showPointer = true;
                          if (tool != ToolId::Select) editor_->setTool(tool);
                          overlay_->update();
                      }});
    ticks_.push_back({1200, {}});
    for (const DemoStep& step : script.steps) {
        const QString say = step.say;
        switch (step.kind) {
        case DemoStep::Move: {
            const Vec2 to = step.at;
            constexpr int kSteps = 18;
            for (int i = 1; i <= kSteps; ++i)
                ticks_.push_back({i == 1 ? 150 : 30, [this, to, i, say, pointerAt] {
                                      const QPointF target = screenAt(to);
                                      const double t = double(i) / kSteps;
                                      const double ease = t * t * (3.0 - 2.0 * t);
                                      const QPointF here = *pointerAt + (target - *pointerAt) * ease;
                                      if (i == kSteps) *pointerAt = target;
                                      overlay_->say = say;
                                      overlay_->keyText.clear();
                                      overlay_->pointer = here;
                                      sendMouse(QEvent::MouseMove, here, Qt::NoButton, Qt::NoButton);
                                      overlay_->update();
                                  }});
            ticks_.push_back({500, {}});
            break;
        }
        case DemoStep::Click: {
            const Vec2 to = step.at;
            ticks_.push_back({150, [this, to, say, pointerAt] {
                                  const QPointF here = screenAt(to);
                                  *pointerAt = here;
                                  overlay_->say = say;
                                  overlay_->pointer = here;
                                  overlay_->ringAt = here;
                                  overlay_->ring = 0.05;
                                  sendMouse(QEvent::MouseMove, here, Qt::NoButton, Qt::NoButton);
                                  sendMouse(QEvent::MouseButtonPress, here, Qt::LeftButton, Qt::LeftButton);
                                  sendMouse(QEvent::MouseButtonRelease, here, Qt::LeftButton, Qt::NoButton);
                                  overlay_->update();
                              }});
            for (int i = 1; i <= 8; ++i)
                ticks_.push_back({40, [this, i] {
                                      overlay_->ring = i < 8 ? 0.05 + i * 0.12 : 0.0;
                                      overlay_->update();
                                  }});
            ticks_.push_back({600, {}});
            break;
        }
        case DemoStep::Key: {
            const int k = step.key;
            ticks_.push_back({200, [this, k, say] {
                                  const int bare = k & ~Qt::KeyboardModifierMask;
                                  QString text = QKeySequence(bare).toString(QKeySequence::NativeText);
                                  if (bare == Qt::Key_Return) text = QStringLiteral("Enter");
                                  if (k & Qt::CTRL) text = QStringLiteral("Ctrl+") + text;
                                  overlay_->keyText = text;
                                  overlay_->say = say;
                                  sendKey(k);
                                  overlay_->update();
                              }});
            ticks_.push_back({1100, [this] {
                                  overlay_->keyText.clear();
                                  overlay_->update();
                              }});
            break;
        }
        }
    }
    // THE RESULT, held long enough to look at.
    ticks_.push_back({300, [this] {
                          overlay_->say = QStringLiteral("Done. (Replay shows it again.)");
                          overlay_->keyText.clear();
                          overlay_->showPointer = false;
                          // Esc puts away whatever the command still has open - the box a number is typed in.
                          sendKey(Qt::Key_Escape);
                          editor_->setTool(ToolId::Select);
                          overlay_->update();
                      }});
}

bool CommandDemo::play(const QString& command) {
    stop();
    CommandDemoScript script;
    if (!commandDemoFor(command, script)) {
        command_.clear();
        editor_->setTool(ToolId::Select);
        doc_->entities().clear();
        startCount_ = 0;
        emit editor_->changed();
        overlay_->say.clear();
        overlay_->keyText.clear();
        overlay_->showPointer = false;
        overlay_->update();
        return false;
    }
    command_ = script.command;
    build(script);
    timer_->start(0);
    return true;
}

void CommandDemo::replay() {
    if (!command_.isEmpty()) play(command_);
}

void CommandDemo::stop() {
    timer_->stop();
    ticks_.clear();
    at_ = 0;
}

bool CommandDemo::playing() const { return at_ < ticks_.size(); }

void CommandDemo::next() {
    while (at_ < ticks_.size()) {
        const Tick& t = ticks_[at_++];
        if (t.act) t.act();
        if (at_ < ticks_.size() && ticks_[at_].waitMs > 0) {
            timer_->start(ticks_[at_].waitMs);
            return;
        }
    }
    emit finished();
}

void CommandDemo::runToEnd() {
    timer_->stop();
    while (at_ < ticks_.size()) {
        const Tick& t = ticks_[at_++];
        if (t.act) t.act();
    }
    emit finished();
}

} // namespace Loopwire
