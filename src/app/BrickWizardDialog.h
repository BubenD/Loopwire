#pragma once

#include "harness/BrickHarness.h"
#include "harness/PartsLibrary.h"

#include <QDialog>
#include <QHash>
#include <QListWidget>
#include <QPointF>
#include <QWidget>

#include <functional>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QTreeWidget;

namespace Edraw_Desktop {

class Document;
class DrawingPreview;
class Editor;

// What a palette drag carries: "<brick type key>|<part kind key>".
inline constexpr char kBrickMime[] = "application/x-edraw-brick";

// ==================================================================================================
// THE LOW-DETAIL PANE: the bricks of a harness on a coarse grid, each with a port on its left and its
// right, and the links between ports as lines.
//
// A brick is dropped here from the palette. A click picks it, Shift+click adds it to what is picked,
// and a drag on the empty pane picks with a zone - left to right (blue) takes what is wholly inside,
// right to left (green) whatever it touches. What is picked is dragged, moved with the arrow keys and
// turned with R, together. A LINK is made by dragging from a port onto another brick - or with Ctrl
// held, from anywhere on the brick - and lands on the port of that brick nearest where it is let go.
// ==================================================================================================
class BrickCanvas : public QWidget {
    Q_OBJECT
public:
    explicit BrickCanvas(BrickDesign* design, QWidget* parent = nullptr);

    // The brick picked last - the one the panel shows - and everything picked.
    QString selected() const { return sel_.isEmpty() ? QString() : sel_.back(); }
    QStringList selection() const { return sel_; }
    void setSelected(const QString& id);
    void setSelection(const QStringList& ids);
    std::pair<QString, QString> selectedLink() const { return selLink_; }

    QPointF toScreen(double col, double row) const;
    void toGrid(const QPointF& at, double& col, double& row) const;
    QString brickAt(const QPointF& at) const;
    QRectF brickRect(const Brick& b) const;
    QPointF portPoint(const Brick& b, int port) const;
    // The port under a point, as its brick and side, or an empty id.
    std::pair<QString, int> portAt(const QPointF& at) const;
    void fit();
    void deleteSelected();
    void moveSelection(double dCol, double dRow);
    void rotateSelection();

signals:
    void selectionChanged(const QString& current);
    void linkPicked(const QString& a, const QString& b);
    void designChanged();
    void message(const QString& text);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    BrickDesign* d_;
    QStringList sel_;
    std::pair<QString, QString> selLink_;
    QPointF origin_{80.0, 70.0};
    double stepX_ = 116.0;
    double stepY_ = 62.0;
    enum class Drag { None, Move, Link, Pan, Zone };
    Drag drag_ = Drag::None;
    QString dragId_;
    int dragPort_ = kPortRight;
    QPointF pressAt_;
    QPointF cursor_;
    QHash<QString, QPointF> startAt_;   // where each picked brick was when the drag began
    QPointF panFrom_;
    bool zoneAdd_ = false;
    bool shiftAlone_ = false;   // Shift pressed and let go with nothing clicked in between
};

// The bricks to choose from: the end parts, the joints, then the specials. Dragged onto the pane;
// double-click puts one in the next free place.
class BrickPalette : public QListWidget {
    Q_OBJECT
public:
    explicit BrickPalette(QWidget* parent = nullptr);

protected:
    QStringList mimeTypes() const override;
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;
};

// ==================================================================================================
// THE MECHANICAL HARNESS WIZARD: the bricks and the parts list on the left, the low-detail pane above
// the preview of the real drawing, and what the picked brick is on the right.
// ==================================================================================================
class BrickWizardDialog : public QDialog {
    Q_OBJECT
public:
    explicit BrickWizardDialog(Editor* editor, QWidget* parent = nullptr);

    BrickDesign& design() { return design_; }
    BrickCanvas* canvas() const { return canvas_; }
    DrawingPreview* preview() const { return preview_; }
    const PartsCatalog& catalog() const { return catalog_; }

    QString addFromPalette(const QString& what);
    void selectBrick(const QString& id);
    void moveSelected(double dCol, double dRow);
    void rotateSelected();
    void arrange();
    std::vector<Entity> drawing() const;
    void place();

    // THE BILL. Puts one of a part on the drawing's bill of materials. A part the bill already lists
    // is not added without asking; `ask` answers the question (the window asks the user when none is
    // set - a test sets one). Returns whether a line was added.
    bool addPartToBom(const QString& partId, double qty = 1.0);
    // The line of the bill a part is on, 1-based, or 0.
    int bomItemOf(const QString& partId) const;
    void setAsk(std::function<bool(const QString& question)> ask) { ask_ = std::move(ask); }

    // THE WIZARDS KEPT IN THE DRAWING, by name: saved with it, opened again, deleted.
    bool saveToDrawing(const QString& name);
    bool loadFromDrawing(const QString& name);
    bool deleteFromDrawing(const QString& name);
    QStringList savedNames() const;
    QString currentName() const { return currentName_; }

    // The nets on the schematic wires landing on the pins of the connectors so designated.
    QStringList schematicNets(const QStringList& designators) const;

signals:
    // The bill was changed from here, so the window showing it can catch up.
    void bomChanged();

protected:
    void keyPressEvent(QKeyEvent* e) override;
    void showEvent(QShowEvent* e) override;

private:
    void buildUi();
    void refreshBom();
    void refreshSaved();
    void loadNets();
    QWidget* buildEndPage();
    QWidget* buildRunPage();
    QWidget* buildLinkPage();
    void loadPanel();
    void loadLabels();
    void refreshPreview();
    void designChanged();
    void writeEnd();
    void writeRun();
    void fillPartsList();
    Document* document() const;

    Editor* editor_;
    PartsCatalog catalog_;
    BrickDesign design_;
    std::function<bool(const QString&)> ask_;
    QString currentName_;
    QComboBox* saved_ = nullptr;
    QLineEdit* harnessName_ = nullptr;
    QTableWidget* bom_ = nullptr;
    QTableWidget* nets_ = nullptr;

    BrickPalette* palette_ = nullptr;
    QTreeWidget* parts_ = nullptr;
    QLineEdit* partsFilter_ = nullptr;
    BrickCanvas* canvas_ = nullptr;
    DrawingPreview* preview_ = nullptr;
    QLabel* status_ = nullptr;
    QComboBox* dimFrom_ = nullptr;
    QComboBox* dimTo_ = nullptr;

    QStackedWidget* panel_ = nullptr;
    QLabel* noneText_ = nullptr;
    // an end
    QLabel* endKind_ = nullptr;
    QComboBox* part_ = nullptr;
    QLineEdit* name_ = nullptr;
    QCheckBox* balloon_ = nullptr;
    QLabel* bomState_ = nullptr;
    QPushButton* bomAdd_ = nullptr;
    QTableWidget* params_ = nullptr;
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
    QDoubleSpinBox* bodyScale_ = nullptr;
    QDoubleSpinBox* bodyWidth_ = nullptr;
    QListWidget* stack_ = nullptr;
    QComboBox* transitions_ = nullptr;
    QCheckBox* dimTotal_ = nullptr;
    QCheckBox* dimFans_ = nullptr;
    QCheckBox* dimBetween_ = nullptr;
    QLineEdit* dimTextTotal_ = nullptr;
    QLineEdit* dimTextFans_ = nullptr;
    QLineEdit* dimTextBetween_ = nullptr;
    QTableWidget* labels_ = nullptr;
    // a link
    QLabel* linkText_ = nullptr;
    QSpinBox* linkWires_ = nullptr;
};

} // namespace Edraw_Desktop
