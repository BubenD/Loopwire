#pragma once

#include "harness/BrickHarness.h"
#include "harness/PartsLibrary.h"

#include <QDialog>
#include <QListWidget>
#include <QPointF>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;

namespace Edraw_Desktop {

class DrawingPreview;
class Editor;

// What a palette drag carries: "<brick type key>|<part kind key>".
inline constexpr char kBrickMime[] = "application/x-edraw-brick";

// ==================================================================================================
// THE LOW-DETAIL PANE: the bricks of a harness on a coarse grid, the links between them as lines.
//
// A brick is dropped here from the palette, dragged to move it (half a grid step at a time), and
// LINKED by dragging from the port on its edge - or with Ctrl held - onto another brick. A link is
// picked by clicking on it. Delete removes what is picked; the arrow keys move the picked brick.
// ==================================================================================================
class BrickCanvas : public QWidget {
    Q_OBJECT
public:
    explicit BrickCanvas(BrickDesign* design, QWidget* parent = nullptr);

    QString selected() const { return sel_; }
    void setSelected(const QString& id);
    // The picked link, as its two ends, or two empty strings.
    std::pair<QString, QString> selectedLink() const { return selLink_; }

    // Where a grid place is on the pane, and back.
    QPointF toScreen(double col, double row) const;
    void toGrid(const QPointF& at, double& col, double& row) const;
    QString brickAt(const QPointF& at) const;
    QRectF brickRect(const Brick& b) const;
    // The port a link is dragged from: the middle of the brick's right-hand edge.
    QPointF portOf(const Brick& b) const;
    // Everything in view, in the middle of the pane.
    void fit();
    // Delete what is picked: a brick (and its links), or a link.
    void deleteSelected();

signals:
    void selectionChanged(const QString& id);
    void designChanged();
    void message(const QString& text);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    BrickDesign* d_;
    QString sel_;
    std::pair<QString, QString> selLink_;
    QPointF origin_{70.0, 60.0};
    double stepX_ = 116.0;
    double stepY_ = 58.0;
    enum class Drag { None, Move, Link, Pan };
    Drag drag_ = Drag::None;
    QString dragId_;
    QPointF pressAt_;
    QPointF cursor_;
    double startCol_ = 0.0;
    double startRow_ = 0.0;
    QPointF panFrom_;
};

// The bricks to choose from: the end parts, then the specials. Dragged onto the pane; double-click
// puts one in the next free place.
class BrickPalette : public QListWidget {
    Q_OBJECT
public:
    explicit BrickPalette(QWidget* parent = nullptr);

protected:
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;
};

// ==================================================================================================
// THE MECHANICAL HARNESS WIZARD: bricks on the left, the low-detail pane above the preview of the
// real drawing, and what the picked brick is on the right.
// ==================================================================================================
class BrickWizardDialog : public QDialog {
    Q_OBJECT
public:
    explicit BrickWizardDialog(Editor* editor, QWidget* parent = nullptr);

    BrickDesign& design() { return design_; }
    BrickCanvas* canvas() const { return canvas_; }
    DrawingPreview* preview() const { return preview_; }

    // Add a brick of a palette item's "<type>|<kind>" at the next free place, pick it.
    QString addFromPalette(const QString& what);
    void selectBrick(const QString& id);
    // Move the picked brick by grid steps.
    void moveSelected(double dCol, double dRow);
    void arrange();
    // The drawing as it stands.
    std::vector<Entity> drawing() const;
    // Put it on the sheet: the window steps aside until it is down.
    void place();

private:
    void buildUi();
    void loadPanel();
    void refreshPreview();
    void designChanged();
    void writeEnd();
    void writeRun();

    Editor* editor_;
    PartsCatalog catalog_;
    BrickDesign design_;

    BrickPalette* palette_ = nullptr;
    BrickCanvas* canvas_ = nullptr;
    DrawingPreview* preview_ = nullptr;
    QLabel* status_ = nullptr;

    QStackedWidget* panel_ = nullptr;
    // an end
    QLabel* endKind_ = nullptr;
    QComboBox* part_ = nullptr;
    QLineEdit* name_ = nullptr;
    QCheckBox* balloon_ = nullptr;
    // a special
    QLabel* runKind_ = nullptr;
    QSpinBox* wires_ = nullptr;
    QDoubleSpinBox* length_ = nullptr;
    QDoubleSpinBox* wireWidth_ = nullptr;
    QDoubleSpinBox* fanStart_ = nullptr;
    QDoubleSpinBox* fanFinish_ = nullptr;
    QDoubleSpinBox* knee_ = nullptr;
    QDoubleSpinBox* fanKnee_ = nullptr;
    QDoubleSpinBox* fanScale_ = nullptr;
    QComboBox* body_ = nullptr;
    QDoubleSpinBox* bodyScale_ = nullptr;
    QDoubleSpinBox* bodyWidth_ = nullptr;
};

} // namespace Edraw_Desktop
