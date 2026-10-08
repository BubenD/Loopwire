#pragma once

#include "system/SystemDesign.h"

#include <QPointF>
#include <QWidget>

#include <vector>

namespace Edraw_Desktop {

// WHAT A CLICK ON THE SYSTEM DIAGRAM DOES: select (and drag a unit, a note, or a port along its unit's sides; a unit's
// corner or edge to size it), put a unit, put a port on a unit's side, draw a cable from one port to another (a unit
// touched where it has no port given one there), put a note.
enum class SysTool { Select, Unit, Port, Cable, Note };

// THE SYSTEM DIAGRAM DRAWN: units as boxes with their name, part number and revision and a mark for what they are (a
// mechanical part, a PCBA), their ports on their sides - each a connector through the wall, its inside always drawn and
// its outside once a cable is plugged into it from outside - the cables between ports drawn square, each with its name,
// part number and revision, and a dot saying how its drawing stands (green: the revision called for; red: another
// revision, or not there; amber: none linked); notes. Zoomed with the wheel, moved with the right or middle button.
// Keys: U a unit, P a port, H a harness (a cable), N a note, Esc select.
class SystemCanvas : public QWidget {
    Q_OBJECT
public:
    explicit SystemCanvas(QWidget* parent = nullptr);
    void setDesign(SystemDesign* design);
    void setTool(SysTool tool);
    SysTool tool() const { return tool_; }
    // What the next unit put is: "mech" or "pcba".
    void setNewUnitKind(const QString& kind) { newKind_ = kind; }
    QString newUnitKind() const { return newKind_; }

    QString selectedUnit() const { return selUnit_; }
    QString selectedPort() const { return selPort_; }
    QString selectedCable() const { return selCable_; }
    QString selectedNote() const { return selNote_; }
    void select(const QString& unit, const QString& port, const QString& cable);
    void selectNote(const QString& note);
    void deleteSelected();
    // The whole diagram in view.
    void fit();

    // WHERE THINGS ARE, for the tests to click: a port's point on the screen (on its unit's side), a unit's middle, its
    // bottom-right corner, a cable's middle, a note's middle.
    QPointF portOnScreen(const QString& unit, const QString& port) const;
    QPointF unitOnScreen(const QString& unit) const;
    QPointF unitCornerOnScreen(const QString& unit) const;
    QPointF cableOnScreen(const QString& cable) const;
    QPointF noteOnScreen(const QString& note) const;
    QPointF toScreen(const QPointF& world) const;
    QPointF toWorld(const QPointF& screen) const;

signals:
    void aboutToChange();   // before an edit - to undo
    void changed();         // after it
    void selectionChanged();
    void toolChanged(SysTool tool);
    void noteEditRequested(const QString& note);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;

private:
    QPointF portPoint(const SysUnit& u, const SysPort& p) const;   // on its side, in the diagram's own units
    QPointF portOut(const SysPort& p) const;                        // the way out of its side
    double portHalf() const;                                        // half a port's square, in the diagram's units
    std::vector<QPointF> cablePath(const SysCable& c) const;
    QString unitAt(const QPointF& world) const;
    bool portAt(const QPointF& world, QString& unit, QString& port) const;
    QString cableAt(const QPointF& world) const;
    QString noteAt(const QPointF& world) const;
    // THE NEAREST SIDE of a unit to a point, and how far along it.
    static void nearestSide(const SysUnit& u, const QPointF& w, QString& side, double& at);
    // A unit's corner or edge under the pointer, to size it: 1 its right edge, 2 its bottom, 3 its bottom-right corner.
    int sizeHandleAt(const QPointF& world, QString& unit) const;
    // Whether a unit lies inside another (a PCBA in its housing).
    bool inside(const QString& inner, const QString& outer) const;

    SystemDesign* design_ = nullptr;
    SysTool tool_ = SysTool::Select;
    QString newKind_ = QStringLiteral("mech");
    QString selUnit_, selPort_, selCable_, selNote_;
    double scale_ = 1.0;
    QPointF offset_{40.0, 40.0};
    // A drag under way: a unit, a note or a port moved, a unit sized, a cable drawn from a port, the view moved.
    enum class Drag { None, Unit, Note, Port, Size, Cable, Pan };
    Drag drag_ = Drag::None;
    int sizeHow_ = 0;
    QPointF pressWorld_, lastScreen_, grabOffset_;
    QString fromUnit_, fromPort_;
    // A CABLE STARTED ON A UNIT WITH NO PORT THERE: the port it is to have, put when the cable is.
    bool fromNew_ = false;
    QString fromSide_;
    double fromAt_ = 0.5;
    QPointF rubber_;
    bool moved_ = false;
    bool moveAnnounced_ = false;
};

} // namespace Edraw_Desktop
