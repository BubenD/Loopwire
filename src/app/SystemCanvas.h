#pragma once

#include "system/SystemDesign.h"

#include <QPointF>
#include <QWidget>

#include <vector>

namespace Edraw_Desktop {

// WHAT A CLICK ON THE SYSTEM DIAGRAM DOES: select (and drag a unit, or draw a cable from a port), put a unit, put a port
// on a unit's side, draw a cable from one port to another.
enum class SysTool { Select, Unit, Port, Cable };

// THE SYSTEM DIAGRAM DRAWN: units as boxes with their name, part number and revision, their ports on their sides, the
// cables between ports drawn square - each with its name, part number and revision, and a dot saying how its drawing
// stands (green: the revision called for; red: another revision, or not there; amber: none linked). Zoomed with the
// wheel, moved with the right or middle button.
class SystemCanvas : public QWidget {
    Q_OBJECT
public:
    explicit SystemCanvas(QWidget* parent = nullptr);
    void setDesign(SystemDesign* design);
    void setTool(SysTool tool);
    SysTool tool() const { return tool_; }

    QString selectedUnit() const { return selUnit_; }
    QString selectedPort() const { return selPort_; }
    QString selectedCable() const { return selCable_; }
    void select(const QString& unit, const QString& port, const QString& cable);
    void deleteSelected();
    // The whole diagram in view.
    void fit();

    // WHERE THINGS ARE, for the tests to click: a port's point on the screen, a unit's middle, a cable's middle.
    QPointF portOnScreen(const QString& unit, const QString& port) const;
    QPointF unitOnScreen(const QString& unit) const;
    QPointF cableOnScreen(const QString& cable) const;
    QPointF toScreen(const QPointF& world) const;
    QPointF toWorld(const QPointF& screen) const;

signals:
    void aboutToChange();   // before an edit - to undo
    void changed();         // after it
    void selectionChanged();

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    QPointF portPoint(const SysUnit& u, const SysPort& p) const;   // in the diagram's own units
    QPointF portOut(const SysPort& p) const;                        // the way out of its side
    std::vector<QPointF> cablePath(const SysCable& c) const;
    QString unitAt(const QPointF& world) const;
    bool portAt(const QPointF& world, QString& unit, QString& port) const;
    QString cableAt(const QPointF& world) const;

    SystemDesign* design_ = nullptr;
    SysTool tool_ = SysTool::Select;
    QString selUnit_, selPort_, selCable_;
    double scale_ = 1.0;
    QPointF offset_{40.0, 40.0};
    // A drag under way: a unit moved, a cable drawn from a port, the view moved.
    enum class Drag { None, Unit, Cable, Pan };
    Drag drag_ = Drag::None;
    QPointF pressWorld_, lastScreen_, grabOffset_;
    QString fromUnit_, fromPort_;
    QPointF rubber_;
    bool moved_ = false;
    bool moveAnnounced_ = false;
};

} // namespace Edraw_Desktop
