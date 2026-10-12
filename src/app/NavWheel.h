#pragma once

#include <QWidget>

#include <functional>
#include <vector>

namespace Loopwire {

// THE NAVIGATION WHEEL: round and see-through, in a pane's corner - the harness wizard's brick pane, the 3D view -
// segments round a middle that names the one under the pointer. Each segment an action, drawn as its own little
// picture (no font needed), lit while it is on. The middle, clicked, can open more (the 3D view's options).
class NavWheel : public QWidget {
    Q_OBJECT
public:
    struct Item {
        QString key;     // its picture: up, right, down, left, cw, ccw, sym, tie, plus, minus, fit, lock, iso, gear
        QString label;   // said in the middle while the pointer is on it
        QString tip;
        std::function<void()> act;
        std::function<bool()> lit;   // on (a toggle), or empty
        // AN ARROW THE WAY SOMETHING RUNS ON THE SCREEN - an axis, as the view is turned - degrees counter-clockwise from
        // three o'clock; unset: as the key draws it.
        double arrowDeg = 1e9;
    };
    explicit NavWheel(QWidget* parent = nullptr);
    void setItems(std::vector<Item> items);
    // WHAT THE MIDDLE SAYS, AND DOES when clicked (at the point of the click, for a menu there) - "Move" and nothing,
    // unless set.
    void setMiddle(const QString& label, std::function<void(const QPoint& global)> act);
    int count() const { return static_cast<int>(items_.size()); }
    const std::vector<Item>& items() const { return items_; }
    double angleOf(int i) const;   // the middle of segment i, degrees counter-clockwise from three o'clock
    // The segment at a point (-1 off the ring), and the middle of a segment - for the tests to click.
    int segmentAt(const QPointF& p) const;
    QPointF segmentCentre(int i) const;
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void leaveEvent(QEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    std::vector<Item> items_;
    QString middleLabel_ = QStringLiteral("Move");
    std::function<void(const QPoint&)> middleAct_;
    int hover_ = -1;
    int pressed_ = -1;
    // A twentieth bigger than first drawn, so the middle has room for its longest word.
    static constexpr double kOuter = 58.8;
    static constexpr double kInner = 24.15;
};


} // namespace Loopwire
