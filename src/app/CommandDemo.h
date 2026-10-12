#pragma once

#include "geometry/Vec2.h"
#include "tools/ToolId.h"

#include <QPointF>
#include <QString>
#include <QStringList>
#include <QWidget>

#include <functional>
#include <memory>
#include <vector>

class QTimer;

namespace Loopwire {

class CanvasWidget;
class Document;
class Editor;

// ONE THING DONE IN A COMMAND'S DEMONSTRATION: the pointer taken to a place, a click there, a key struck. Each
// says in a line what it is for, and that line is shown while it is done.
struct DemoStep {
    enum Kind { Move, Click, Key };
    Kind kind = Move;
    Vec2 at;               // where, in the demonstration's drawing (0..200 across, 0..100 up)
    int key = 0;           // the key, for a Key step
    QString say;           // what is being done
};

// A COMMAND'S DEMONSTRATION: what is on the drawing before it starts (and selected), the tool it starts, and
// the steps that work it.
struct CommandDemoScript {
    QString command;                                   // the typed name, as the command list has it
    QString title;                                     // what it is, said plainly
    std::function<void(Document&, Editor&)> setup;     // what is there to start with
    ToolId tool = ToolId::Select;
    std::vector<DemoStep> steps;
};

// The commands that have a demonstration, in the order the command list has them.
QStringList commandDemoNames();
// That command's demonstration, by any of its names. False when it has none.
bool commandDemoFor(const QString& command, CommandDemoScript& out);

// A COMMAND, SHOWN: a small drawing of its own, with the real canvas and the real editor on it, and the command
// worked on it the way a person works it - the pointer moving, the clicks, the keys - through the same mouse and
// key events a hand makes. So what is shown is what the command does: its snaps, its previews and its result are
// the program's own, and a demonstration cannot show a command doing something it does not do. The pointer, a
// ring where it clicks, the key struck and a line saying what is being done are drawn over it.
//
// Only shows: the hand is kept out of it, so nothing on it can be changed except by the demonstration.
class CommandDemo : public QWidget {
    Q_OBJECT
public:
    explicit CommandDemo(QWidget* parent = nullptr);
    ~CommandDemo() override;

    // Show this command worked from the start. False (and the drawing left empty) when it has no demonstration.
    bool play(const QString& command);
    // The same one again, from the start.
    void replay();
    void stop();
    bool playing() const;
    QString command() const { return command_; }

    // Every step done at once, without waiting between them - for the tests, which check that each
    // demonstration really does what it says.
    void runToEnd();
    const Document* document() const { return doc_.get(); }
    // How many objects the drawing had when the demonstration started.
    int startCount() const { return startCount_; }

    // Where the drawing is on the widget, for a demonstration's place.
    QPointF screenAt(const Vec2& world) const;

signals:
    void finished();

protected:
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    class Overlay;
    struct Tick {
        int waitMs = 0;
        std::function<void()> act;
    };
    void build(const CommandDemoScript& script);
    void fitDrawing();
    void sendMouse(int type, const QPointF& at, Qt::MouseButton button, Qt::MouseButtons held);
    void sendKey(int key);
    void next();

    std::unique_ptr<Document> doc_;
    Editor* editor_ = nullptr;
    CanvasWidget* canvas_ = nullptr;
    Overlay* overlay_ = nullptr;
    QTimer* timer_ = nullptr;
    std::vector<Tick> ticks_;
    size_t at_ = 0;
    QString command_;
    int startCount_ = 0;
    bool driving_ = false;   // an event of the demonstration's own, let through to the canvas
};

} // namespace Loopwire
