#pragma once

#include "system/SystemDesign.h"

#include <QPointF>
#include <QElapsedTimer>
#include <QWidget>
#include <QSet>
#include <QHash>

class QTimer;

#include <vector>

namespace Edraw_Desktop {

// WHAT A CLICK ON THE SYSTEM DIAGRAM DOES: select (and drag a unit, a note, or a port along its unit's sides; a unit's
// corner or edge to size it), put a unit, put a port on a unit's side, draw a cable from one port to another (a unit
// touched where it has no port given one there), put a note.
enum class SysTool { Select, Unit, Port, Cable, Note, Link };
enum class SysSelectionMode { Direction, Window, Crossing };

// THE SYSTEM DIAGRAM DRAWN: units as boxes with their name, part number and revision and a mark for what they are (a
// mechanical part, a PCBA), their ports on their sides - each a connector through the wall, its inside always drawn and
// its outside once a cable is plugged into it from outside - the cables between ports drawn square, each with its name,
// part number and revision, and a dot saying how its drawing stands (green: the revision called for; red: another
// revision, or not there; amber: none linked); notes. Zoomed with the wheel, moved with the right or middle button.
// Keys: U a unit, P a port, H a harness (a cable), N a note, Esc select, Space the last thing done again (a tool, or
// Delete). A unit, port, cable or note put, it is back to Select - unless Shift is held, to put another.
class SystemCanvas : public QWidget {
    Q_OBJECT
public:
    explicit SystemCanvas(QWidget* parent = nullptr);
    void setDesign(SystemDesign* design);
    void setTool(SysTool tool);
    SysTool tool() const { return tool_; }
    void setSelectionMode(SysSelectionMode mode) { selectionMode_ = mode; }
    SysSelectionMode selectionMode() const { return selectionMode_; }
    // What the next unit put is: "mech" or "pcba".
    void setNewUnitKind(const QString& kind) { newKind_ = kind; }
    QString newUnitKind() const { return newKind_; }

    QString selectedUnit() const { return selUnit_; }
    QString selectedPort() const { return selPort_; }
    QString selectedCable() const { return selCable_; }
    QString selectedNote() const { return selNote_; }
    void select(const QString& unit, const QString& port, const QString& cable);
    void selectNote(const QString& note);
    bool startNoteLink();
    const QSet<QString>& selectedUnits() const { return zoneUnits_; }
    const QSet<QString>& selectedCables() const { return zoneCables_; }
    const QSet<QString>& selectedNotes() const { return zoneNotes_; }
    int selectionCount() const;
    // The check played over the diagram: the rules it ran (none: all of them), under a title of its own.
    void animateCheck(const std::vector<SysIssue>& issues, const QStringList& rules = {}, const QString& title = {});
    // The rules the last check reviewed, by id - what its card lists.
    QStringList checkedRules() const;
    void clearCheck();
    bool locateIssue(const SysIssue& issue);
    QPointF portLabelOnScreen(const QString& unit, const QString& port) const;
    // What is selected taken away - as it is, no questions (the view asks first: deleteRequested).
    void deleteSelected(bool removeContents = true);
    // Delete was the last thing done - Space does it again.
    void noteDeleteDone() { lastDelete_ = true; }
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
    void deleteRequested();
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
    bool portLabelAt(const QPointF& world, QString& unit, QString& port) const;
    void selectZone(const QRectF& zone, bool crossing);
    bool issueOn(const QString& unit, const QString& cable) const;
    // THE NEAREST SIDE of a unit to a point, and how far along it.
    static void nearestSide(const SysUnit& u, const QPointF& w, QString& side, double& at);
    // A unit's wall or corner under the pointer, to size it by: 1 its right, 2 its bottom, 4 its left, 8 its top - a
    // corner two of them.
    int sizeHandleAt(const QPointF& world, QString& unit) const;
    // A UNIT DROPPED: in the unit round its middle now (or none), and kept inside that one.
    void settleUnit(const QString& unit);
    // A CABLE STARTED BY A CLICK: it follows the pointer until a click on another port or unit ends it (Esc gives it up).
    bool pending_ = false;
    // The cable being drawn ended where the pointer is: on a port, or on another unit (a port put there); anywhere else,
    // given up.
    void finishCable(const QMouseEvent* e);
    // After a unit, port, cable or note is put: back to Select, unless Shift is held to put another.
    void placed(const QMouseEvent* e);
    // Whether a unit lies inside another (a PCBA in its housing).
    bool inside(const QString& inner, const QString& outer) const;

    SystemDesign* design_ = nullptr;
    SysTool tool_ = SysTool::Select;
    QString newKind_ = QStringLiteral("mech");
    // THE LAST THING DONE, for Space: a tool put down (Unit, Port, Cable, Note), or Delete.
    SysTool lastTool_ = SysTool::Select;
    bool lastDelete_ = false;
    QString selUnit_, selPort_, selCable_, selNote_;
    QString linkNote_;
    double scale_ = 1.0;
    QPointF offset_{40.0, 40.0};
    // A drag under way: a unit, a note or a port moved, a unit sized, a cable drawn from a port, the view moved.
    enum class Drag { None, Unit, Note, Port, Size, NoteSize, PortLabel, Zone, Group, CableMove, Cable, Pan };
    Drag drag_ = Drag::None;
    int sizeHow_ = 0;
    QRectF sizeFrom_;
    QPointF pressWorld_, lastScreen_, grabOffset_;
    QString fromUnit_, fromPort_;
    // A CABLE STARTED ON A UNIT WITH NO PORT THERE: the port it is to have, put when the cable is.
    bool fromNew_ = false;
    QString fromSide_;
    double fromAt_ = 0.5;
    QPointF rubber_;
    bool moved_ = false;
    bool moveAnnounced_ = false;
    QSet<QString> zoneUnits_, zoneCables_, zoneNotes_;
    QPointF zoneEnd_, groupDelta_;
    SysSelectionMode selectionMode_ = SysSelectionMode::Direction;
    bool zoneCrossing() const;
    QTimer* checkTimer_ = nullptr;
    std::vector<SysIssue> checkIssues_;
    std::vector<int> checkRules_;   // the rules the card reviews, by place in the list of all
    QString checkTitle_;
    int checkRuleCount() const { return static_cast<int>(checkRules_.size()); }
    int checkStep_ = -1;
    double checkPhase_ = 0.0;
    QElapsedTimer checkClock_;
    bool checkShown_ = false;
    QRectF checkPanelRect() const;
    QTimer* locateTimer_ = nullptr;
    QElapsedTimer locateClock_;
    QPointF locatePoint_;
    bool locateOn_ = false;
    QPointF cableDragA_,cableDragB_;
    mutable QByteArray routesKey_;
    mutable QHash<QString,std::vector<QPointF>> routes_;
};

} // namespace Edraw_Desktop
