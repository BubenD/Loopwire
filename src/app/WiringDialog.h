#pragma once

#include "harness/HarnessBuilder.h"

#include <QDialog>
#include <QHash>
#include <QPointF>
#include <QPointer>
#include <QRectF>
#include <QSet>

#include <functional>
#include <optional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
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
class DrawingPreview;

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
    void terminatePinAsked(const QString& conn, int pin);
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
    // A WIRE'S BAR RIGHT-CLICKED: a joint to be added on it - the pin its new wire goes to asked for.
    void jointMenuAsked(const QString& wireId, const QPoint& globalAt);
    // The tool changed from here (Esc out of twisting, shielding, a cable: back to Select).
    void toolPicked(WiringCanvas::Tool tool);
    void brickSelectionChanged();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

public:
    // ZOOMED with the wheel: everything drawn and clicked at this scale (1 = as laid out).
    void setConnectorSpacing(double spacing);
    double connectorSpacing() const { return connectorSpacing_; }
    double zoom() const { return zoom_; }
    void setZoom(double z);
    // THE ZOOM THAT FITS everything drawn into the width and height there is.
    double fitZoom(const QSize& room) const;
    // THE WIRES IN THEIR COLOURS, or all white - a picture of what goes where, without the colours.
    void setWireColours(bool on);
    bool wireColours() const { return colours_; }
    // A CABLE SHOWN AS ONE HARNESS: its wires drawn into one trunk across the lane, fanned out only to their pins - this
    // picture only, the drawing as it was (not saved).
    void setTrunk(const QString& cable, bool on);
    bool isTrunk(const QString& cable) const { return trunks_.contains(cable); }
    // A SCREEN'S RING OR A PAIR'S TWIST under a point: the wires of that group, or none - and those a stroke crosses.
    QStringList groupMarkAt(const QPointF& p) const;
    QStringList groupMarksAcross(const QPointF& a, const QPointF& b) const;
    // A WIRE JOINED TO ANOTHER'S RUN under a point - its line from its pin to the dot - or "".
    QString jointWireAt(const QPointF& p) const;
    QString jointEndpointAt(const QPointF& p) const;
    // The way a joined wire is drawn: from its pin to the dot on the wire it joins.
    std::vector<QPointF> jointPath(const HarnessWire& w) const;
    std::vector<QPointF> wirePath(const QString& wireId) const;

private:
    // Where each screen's ring and each pair's twist is drawn, by group: from the bars of its wires.
    QMultiHash<QString, QRectF> markRects(bool screens) const;
    QList<int> screenEnds(const QString& group) const;
    // A wire's colour as drawn here: its own, or white while colours are off (and no stripe).
    QColor shownBase(const QString& code) const;
    QColor shownStripe(const QString& code) const;
    double zoom_ = 1.0;
    double connectorSpacing_ = 460.0;
    bool colours_ = true;
    QSet<QString> trunks_;   // the cables shown as one harness (not saved)
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
        int lane = 0;   // between two connectors on one side: its own lane beside that side, at laneX
        double laneX = 0.0;
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
    HarnessPaths jointPaths_;
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
    // A LAYOUT FLAG CHANGED here (Terminate unconnected): the drawing only, not every table - what the wizard does for it.
    void setLayoutChanged(std::function<void()> f) { layoutChanged_ = std::move(f); }
    // THE HARNESS CHANGED somewhere else: drawn again, the panel filled again.
    void refresh();
    // The small drawing of the harness drawn again, a moment from now.
    void redrawPreview();

    // TWO CONNECTORS, or SEVERAL (those named; none named: all).
    void showTwo(const QString& left, const QString& right);
    void showSeveral(const QStringList& ids = QStringList());
    bool twoMode() const;
    WiringCanvas* canvas() const { return canvas_; }
    // A SHIELD AS A LAYER OF A CABLE: over every wire of it, a part of its own (its P/N given in the panel) - its name.
    QString addCableShield(const QString& cable);
    // A shield that is a cable's layer - over all that cable's wires and no others: the cable, else empty.
    QString cableOfShield(const QString& shield) const;

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
    // A JOINT ADDED ON A WIRE: the pin its new wire goes to picked from every connector's pins (`pinKey` "conn:pin" given
    // straight, for the tests; empty, asked for).
    QString addJointOnWire(const QString& wireId, const QString& pinKey = QString());
    QString splitAtJoint(const QString& wireId);
    bool promoteGroup(const QString& kind, const QString& name);
    // THE PIN STACKS OF THE CONNECTORS PICKED (none picked: every one shown): the wired pins at the top, the free ones below -
    // or, `wiredFirst` false, the other way round. How many were reordered.
    int orderStacks(bool wiredFirst);
    // A SCREEN'S DRAIN AT ONE END (0 its left, 1 its right): how - "" not drained, "drain" its own drain wire, "hook" a
    // hook-up wire spliced onto it (its part `wirePart`, from the library) - and where to: "pin:<connector>:<pin>" any
    // connector's pin (a ferrule, a terminal, ...), "shield:<connector>" a connector's own screen, "screen:<group>" another.
    bool setScreenDrain(const QString& group, int end, const QString& how, const QString& target = QString(),
                        const QString& wirePart = QString());
    QString screenDrain(const QString& group, int end, QString* target = nullptr, QString* wirePart = nullptr) const;
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
    std::function<void()> layoutChanged_;
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
    // The bricks' size said, the wire colours switch, the small drawing under the panel and its timer (not saved).
    QLabel* zoomSaid_ = nullptr;
    QToolButton* colours_ = nullptr;
    DrawingPreview* preview_ = nullptr;
    QTimer* previewTimer_ = nullptr;
    QPointer<DrawingPreview> bigPreview_;   // the drawing enlarged, while its window is open
    std::function<void(DrawingPreview*)> markDrag_;
public:
    // THE DRAWING ENLARGED: a window of its own, kept drawn as the wiring changes - its picture.
    DrawingPreview* enlargePreview();
private:
    QLabel* panelTitle_ = nullptr;
    QLabel* cma_ = nullptr;
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
    QPushButton* addShield_ = nullptr;
    QPushButton* trunk_ = nullptr;
    QGroupBox* screenBox_ = nullptr;
    QComboBox* screenPart_ = nullptr;
    QComboBox* screenEnd_[2] = {};
    QComboBox* screenPin_[2] = {};
    QComboBox* screenWire_[2] = {};   // the hook-up wire's part, from the library
    QToolButton* terminate_ = nullptr;
    bool filling_ = false;
    void fillGroups();
    void markGroups();
    void fillScreenBox();
    QString screenOfSelection() const;
    QString screenEndConn(const QString& group, int end) const;
};

} // namespace Loopwire
