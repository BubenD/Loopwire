#pragma once

#include "harness/HarnessBuilder.h"

#include <QDialog>
#include <QHash>
#include <QPointF>
#include <QRectF>

#include <functional>
#include <optional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QScrollArea;
class QStackedWidget;
class QTimer;
class QToolButton;

namespace Loopwire {

// THE WIRING DRAWN, TO WIRE BY HAND: each connector a brick holding a square for each of its pins - laid out as its
// pinout drawing has them where the library part has one, a numbered grid where it has not - and each wire a line from
// its pin, across a bar in the middle, to its other pin: the bars stacked in the order the wires are listed, which is
// the order the table and the drawing take. A free pin an empty square; one wired, filled with its wire's colour; one
// wired to a connector not shown, hatched.
class WiringCanvas : public QWidget {
    Q_OBJECT
public:
    // CONNECT: a pin clicked, then another - or dragged from one to the other. SELECT: lines clicked, or a stroke drawn
    // across them. TWIST, SCREEN, CABLE: a stroke drawn across lines makes them twisted, screened, one cable.
    enum class Tool { Connect, Select, Twist, Screen, Cable };
    WiringCanvas(HarnessDef& def, QWidget* parent = nullptr);

    // The connectors shown: in TWO mode the first on the left and the second on the right; otherwise each on the side
    // the harness has it, those of a side one under another.
    void setShown(const QStringList& connIds, bool twoMode);
    const QStringList& shown() const { return shown_; }
    void setTool(Tool tool);
    Tool tool() const { return tool_; }
    // The wires drawn here - both ends on connectors shown - in the order they are listed.
    QStringList shownWires() const;
    // The wires picked, and the pin waiting for its other end (none: empty).
    const QStringList& selection() const { return selection_; }
    void setSelection(const QStringList& ids);
    QString pendingPin() const { return pending_; }
    // WHERE THE NEXT WIRE MIGHT GO: the next free pin on each connector the last wire joined - drawn dashed; Enter takes it.
    void setGhost(const QString& fromConn, int fromPin, const QString& toConn, int toPin);
    void clearGhost();
    bool ghost(QString& fromConn, int& fromPin, QString& toConn, int& toPin) const;
    // Where things are drawn, for a click to land on (and the tests to click).
    QPointF pinCentre(const QString& conn, int pin) const;
    QPointF barCentre(const QString& wireId) const;
    // The wires whose bars a stroke from a to b crosses.
    QStringList barsAcross(const QPointF& a, const QPointF& b) const;
    // The pin under a point: "conn:pin", or "".
    QString pinAt(const QPointF& p) const;
    QString barAt(const QPointF& p) const;
    // The wires on a pin, in the order they are listed.
    QStringList wiresOn(const QString& conn, int pin) const;
    QSize sizeHint() const override;
    // Laid out again - the definition changed.
    void relayout();

signals:
    void connectAsked(const QString& fromConn, int fromPin, const QString& toConn, int toPin);
    void relandAsked(const QString& wireId, int end, const QString& conn, int pin);
    void moveAsked(const QString& wireId, int toShownIndex);
    void strokeMade(WiringCanvas::Tool tool, const QStringList& wireIds);
    void selectionChanged();
    void deleteAsked();
    void ghostAccepted();
    void said(const QString& text);
    // A PIN CAUGHT (held 0.3 s) AND LET GO on another pin of its connector: put there in the connector's pin stack.
    void pinStackMoveAsked(const QString& conn, int pin, int toPin);
    // A WIRE'S BAR CAUGHT (held 0.3 s) AND LET GO: the wire moved there in the list, its pins with it in both stacks.
    void wireWithPinsMoveAsked(const QString& wireId, int toShownIndex);
    // A PIN DRAGGED ONTO A WIRE'S BAR: a wire from the pin to a joint on that wire's run.
    void jointAsked(const QString& conn, int pin, const QString& wireId);
    void brickSelectionChanged();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    struct Brick {
        QString conn;
        QRectF rect;
        bool left = true;
        QHash<int, QRectF> pins;
    };
    struct Bar {
        QString wire;
        double y = 0.0;
        QPointF from, to;   // where its two ends meet their pins
        int fromSide = 0, toSide = 0;   // -1 the left column, 1 the right
    };
    const Brick* brickOf(const QString& conn) const;
    QPointF pinExit(const QString& conn, int pin, int& side) const;

    HarnessDef& def_;
    QStringList shown_;
    bool twoMode_ = true;
    Tool tool_ = Tool::Connect;
    std::vector<Brick> bricks_;
    std::vector<Bar> bars_;
    double laneLeft_ = 0.0, laneRight_ = 0.0, barTop_ = 0.0;
    QSize content_{600, 400};
    QStringList selection_;
    QString pending_;
    bool ghostOn_ = false;
    QString ghostFrom_, ghostTo_;
    int ghostFromPin_ = 0, ghostToPin_ = 0;
    // A press and what it is taking hold of (not saved): a pin, a bar, or a stroke.
    QPointF pressAt_, now_;
    QString pressPin_, pressBar_;
    bool pressing_ = false, moved_ = false, ctrl_ = false;
    // A PIN (or a bar) HELD STILL 0.3 S IS CAUGHT, to be put elsewhere - its pin stack, or the list with its pins (not saved).
    QTimer* holdTimer_ = nullptr;
    bool carrying_ = false;
    bool carryingBar_ = false;
    // The connectors picked - clicked on their brick, Ctrl for more - for what is done to their pin stacks (not saved).
    QStringList selectedBricks_;
    QString pressBrick_;

public:
    bool carrying() const { return carrying_; }
    const QStringList& selectedBricks() const { return selectedBricks_; }
    void setSelectedBricks(const QStringList& ids);
    // The connector whose brick is under a point (not on a pin), or "".
    QString brickAt(const QPointF& p) const;
};

// THE WIRING WINDOW: two connectors - or several - wired by clicking and dragging pins, the lines stacked in order and
// moved by dragging, twisted, screened and made one cable by a stroke across them; the lines picked given their part,
// colour, gauge and label in the panel beside. Every change goes straight into the harness and through `changed` - the
// table, the drawing and undo with it.
class WiringDialog : public QDialog {
    Q_OBJECT
public:
    WiringDialog(HarnessDef& def, std::function<void()> changed, QWidget* parent = nullptr);
    // How a new screen's drain is asked for (a connector, or none); undo and redo, the wizard's own.
    void setAskDrain(std::function<QString()> ask) { askDrain_ = std::move(ask); }
    void setUndo(std::function<void()> undo, std::function<void()> redo);
    // THE HARNESS CHANGED somewhere else: drawn again, the panel filled again.
    void refresh();

    // TWO CONNECTORS, or SEVERAL (those named; none named: all).
    void showTwo(const QString& left, const QString& right);
    void showSeveral(const QStringList& ids = QStringList());
    bool twoMode() const;
    WiringCanvas* canvas() const { return canvas_; }

    // WHAT THE POINTER DOES, done straight: a wire from pin to pin (its id); an end moved to another pin; a wire moved in
    // the list to before the shown wire at `toShownIndex`; the wires made twisted, screened or one cable.
    QString connectPins(const QString& fromConn, int fromPin, const QString& toConn, int toPin);
    bool relandEnd(const QString& wireId, int end, const QString& conn, int pin);
    bool moveWire(const QString& wireId, int toShownIndex);
    QString groupWires(WiringCanvas::Tool tool, const QStringList& ids, const QString& drainConn = QString());
    // EVERY FREE PIN of the left connector wired to the next free pin of the right, in turn. How many wires it made.
    int autoOneToOne();
    bool acceptGhost();
    // A LIBRARY CABLE placed: its conductors from the left connector's free pins to the right's, in turn - its pairs
    // twisted and its screens screened as the cable has them. How many conductors.
    int placeCable(const QString& libPartId);
    void deleteSelected();
    // THE PANEL: the lines picked given a part, a colour (base and stripe), a gauge (mm2), a label, a cut length.
    void setSelectedPart(const QString& libPartId);
    void setSelectedColor(const QString& code);
    void setSelectedGauge(const QString& mm2);
    void setSelectedLabel(const QString& label);
    void setSelectedCutLength(double mm);
    // THE PIN STACKS OPTIMISED, as the round buttons ask: no lines crossing (the second connector's pins, and the wires'
    // bars, in the order of the first's), the wires screened together on pins side by side, those twisted together too.
    // Only the order the pins are drawn in changes - every wire stays on its pins. How many connectors were reordered.
    int optimizePinStack(bool tell = true);
    void setOptimize(bool avoidCrossing, bool byShield, bool byTwist);
    bool optimizeOn() const;
    // A PIN PUT ELSEWHERE IN ITS CONNECTOR'S PIN STACK: where `toPin` is, the pins between moved up or down one.
    bool movePinInStack(const QString& conn, int pin, int toPin);
    // A WIRE MOVED IN THE LIST WITH ITS PINS: to before the shown wire at `toShownIndex`, and on each connector its pin put
    // where that wire's pin is in the stack.
    bool moveWireWithPins(const QString& wireId, int toShownIndex);
    // A WIRE FROM A PIN TO A JOINT ON ANOTHER WIRE'S RUN (its id).
    QString connectToWire(const QString& conn, int pin, const QString& wireId);
    // THE PIN STACKS OF THE CONNECTORS PICKED (none picked: every one shown): the wired pins at the top, the free ones below -
    // or, `wiredFirst` false, the other way round. How many were reordered.
    int orderStacks(bool wiredFirst);
    // A SCREEN'S DRAIN AT ONE END (0 its start, 1 its finish): "" not drained, "pin" a drain wire to `pin`, "hook" a hook-up
    // wire spliced on, to `pin`, "conn" the connector's own screen, "screen:<group>" another screen.
    bool setScreenDrain(const QString& group, int end, const QString& how, int pin = 0);
    QString screenDrain(const QString& group, int end, int* pin = nullptr) const;
    // ITS OWN PART: a library part's id, or a P/N typed.
    void setScreenPart(const QString& group, const QString& libPartId, const QString& typedPn = QString());
    // What is wrong, one a line, in the order of the wires it is about.
    QStringList problems() const;

private:
    void changed();
    void fillPanel();
    void fillConnectorChoices();
    void applyToSelection(const std::function<void(HarnessWire&)>& edit);
    int nextFreePinAfter(const QString& conn, int after) const;
    void sayProblems();

    HarnessDef& def_;
    std::function<void()> changed_;
    std::function<QString()> askDrain_;
    std::function<void()> undo_, redo_;
    WiringCanvas* canvas_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QComboBox* mode_ = nullptr;
    QStackedWidget* pickers_ = nullptr;
    QComboBox* left_ = nullptr;
    QComboBox* right_ = nullptr;
    QListWidget* several_ = nullptr;
    QToolButton* tools_[5] = {};
    QLabel* said_ = nullptr;
    QLabel* problems_ = nullptr;
    QWidget* panel_ = nullptr;
    QLabel* panelTitle_ = nullptr;
    QComboBox* part_ = nullptr;
    QComboBox* base_ = nullptr;
    QComboBox* stripe_ = nullptr;
    QComboBox* gauge_ = nullptr;
    QLineEdit* label_ = nullptr;
    QDoubleSpinBox* cut_ = nullptr;
    QCheckBox* print_[3] = {};
    QToolButton* optCross_ = nullptr;
    QToolButton* optShield_ = nullptr;
    QToolButton* optTwist_ = nullptr;
    QListWidget* groups_ = nullptr;
    QGroupBox* screenBox_ = nullptr;
    QComboBox* screenPart_ = nullptr;
    QComboBox* screenEnd_[2] = {};
    QComboBox* screenPin_[2] = {};
    bool filling_ = false;
    void fillGroups();
    void markGroups();
    void fillScreenBox();
    QString screenOfSelection() const;
    QString screenEndConn(const QString& group, int end) const;
};

} // namespace Loopwire
