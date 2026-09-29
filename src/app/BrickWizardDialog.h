#pragma once

#include "harness/Bom.h"
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
class QFormLayout;
class QMenu;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QSplitter;
class QTabWidget;
class QTableWidget;
class QToolButton;
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
    // Zoomed and centred so every brick is in view - the Fit button.
    void fit();
    // Centred on the bricks at the zoom it is at - what arranging or loading a design does.
    void centreView();
    // Back to the pane's own size, keeping the middle where it is.
    void resetZoom();
    void deleteSelected();
    void moveSelection(double dCol, double dRow);
    void rotateSelection();
    // THE MAP: the whole of it small in a corner, with the part in view marked; a click on it goes there.
    void setMinimap(bool on);
    bool minimap() const { return minimap_; }
    // THE RIGHT-CLICK MENU on the pane: turning, moving, turning a fan round, duplicating, unlinking,
    // deleting what is picked - or fitting and the map, on nothing.
    void fillContextMenu(QMenu& menu);
    void duplicateSelection();
    // COPY AND PASTE of bricks - their parts, settings and the links between them - through the clipboard,
    // so a brick set up once is put down again, here or in another wizard.
    void copySelection();
    void pasteClipboard();
    // The middle of what is in view, on the grid.
    QPointF viewCentreGrid() const;
    // How far the pane is zoomed: 1 is the pane's own size.
    double zoom() const { return zoom_; }

signals:
    void selectionChanged(const QString& current);
    void linkPicked(const QString& a, const QString& b);
    // Bricks moved on the pane, with where each was before the move - so a part moved can say a new length.
    void bricksMoved(const QHash<QString, QPointF>& from);
    // Two bricks clicked while putting a dimension on: the dimension goes between them.
    void dimensionPicked(const QString& a, const QString& b);
    // A link made on the pane - so a part linked to a special can be put at the special's length.
    void linked(const QString& a, const QString& b);
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
    void resizeEvent(QResizeEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    QRectF minimapRect() const;
    // ZOOM, on the wheel, about the cursor. The pane is laid out in its own units and drawn scaled; a
    // pointer position is turned into those units with L(), and the part of them in view is viewSize().
    double zoom_ = 1.0;
    QPointF L(const QPointF& screen) const { return screen / zoom_; }
    QSizeF viewSize() const { return QSizeF(width(), height()) / zoom_; }
    QToolButton* fitButton_ = nullptr;
    bool minimap_ = false;
    QToolButton* mapButton_ = nullptr;
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
    QHash<QString, BrickFollow> follow_;   // what goes with the picked bricks, and on which axes
    // A BRICK MOVED ON THE PANE STAYS WHERE IT IS ON THE DRAWING: how far up or down each part was drawn when the
    // move began, and how far apart the rows are drawn - so its move on the pane is taken back off the drawing.
    QHash<QString, double> startDy_;
    double rowPitchMm_ = 0.0;
    void keepOnDrawing(const QHash<QString, QPointF>& from);
    bool dimMode_ = false;                 // putting a dimension on: two clicks, two bricks
    QString dimFirst_;
public:
    void setDimensionMode(bool on);
    bool dimensionMode() const { return dimMode_; }
    bool dragging() const { return drag_ != Drag::None; }
private:
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

    // Put the picked end part as this library part - from the parts list - and on the bill.
    bool assignPart(const QString& partId);

    // WHERE THE BILL IS: 0 a tab on the left, 1 under the left, 2 under the preview, 3 above the
    // pane, 4 under the right.
    void placeBom(int where);
    // Take a line of the bill, as the table shows it, off the drawing's bill. A line the drawing
    // itself calls for cannot be - it is there because the part is on the drawing.
    bool removeFromBom(int shownRow);
    // A LINE OF THE BILL MOVED: the one shown at `from` put in the gap before shown line `gap` (the number of
    // lines, for the end). The item numbers - and every balloon, which is numbered by them - follow.
    bool moveBomRow(int from, int gap);
    int bomPlace() const { return bomAt_; }

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
    void showActuals();
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
    int bomAt_ = -1;
    QWidget* bomBox_ = nullptr;
    // Dragging a line of the bill to a new place, the white line showing where it will go.
    QWidget* bomDropLine_ = nullptr;
    int bomDragFrom_ = -1;
    bool bomDragging_ = false;
    QPoint bomPressAt_;
    int bomGapAt(const QPoint& at) const;
    std::vector<BomRow> bomShown_;
    QDoubleSpinBox* trunk_ = nullptr;
    QTabWidget* leftTabs_ = nullptr;
    QSplitter* leftSplit_ = nullptr;
    QSplitter* middle_ = nullptr;
    QSplitter* rightSplit_ = nullptr;
    QComboBox* bomAtCombo_ = nullptr;
    // THE AUTO BUTTONS: beside each figure the drawing works out when left alone - "A", lit while
    // it is left alone, the figure shown then being what it came to.
    QHash<QDoubleSpinBox*, QToolButton*> autoOf_;
    QHash<QString, BrickActual> actual_;
    QCheckBox* dimLinear_ = nullptr;
    QCheckBox* dimTrace_ = nullptr;
    QCheckBox* dimSingle_ = nullptr;

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
    // How big the part is drawn, and "A" - worked out so the wires come in with the fewest bends.
    QDoubleSpinBox* partScale_ = nullptr;
    QToolButton* partScaleAuto_ = nullptr;
    QHash<QString, double> drawnScales_;   // what each end came to on the last drawing
    QHash<QString, BrickFace> lastFaces_;  // every entry face on the last drawing
    // SCALE TO ANOTHER PART: pressed, the next brick clicked is the one this part is sized to - its entry
    // face made as long as that brick's nearest one.
    QToolButton* partScaleMatch_ = nullptr;
    QString matchSource_;
public:
    // Size `id`'s part so its entry face nearest `other` is as long as `other`'s nearest entry face.
    bool scalePartToMatch(const QString& id, const QString& other);
private:
    bool eventFilter(QObject* o, QEvent* e) override;
    QLabel* bomState_ = nullptr;
    QPushButton* bomAdd_ = nullptr;
    QTableWidget* params_ = nullptr;
    // a special
    QLabel* runKind_ = nullptr;
    QSpinBox* wires_ = nullptr;
    QDoubleSpinBox* length_ = nullptr;
    QDoubleSpinBox* wireWidth_ = nullptr;
    // BRIDGE WIRES, on a bridge wire only: which wire of the cable it is joined to it takes - picked from that
    // cable's wires (by net, where they have one) or its braided shield, or typed ("1-3,5").
    QComboBox* bridge_ = nullptr;
    QFormLayout* runForm_ = nullptr;
    QString bridgeValue() const;
    // A PART DRAGGED ON THE DRAWING (the preview): its run's length follows, the pane stays as it is.
    QString partDrag_;
    Vec2 partDragFrom_;
    BrickDesign partDragDesign_;
    BrickBuild partDragBuilt_;
    QString previewPartAt(const QPointF& at) const;
    // THE DRAWING ENLARGED to the whole window, the pane small beside it, and a button to go back.
    QSplitter* mainSplit_ = nullptr;
    QToolButton* enlargeBtn_ = nullptr;
    QList<int> savedMainSizes_, savedMiddleSizes_;
    void placeEnlargeButton();
public:
    void setDrawingEnlarged(bool on);
    bool drawingEnlarged() const;
private:
    QLabel* bridgeFrom_ = nullptr;       // and which special that is
    QDoubleSpinBox* fanStart_ = nullptr;
    QDoubleSpinBox* fanFinish_ = nullptr;
    // WHETHER THERE IS A FAN at each end at all: off, the wires go in as the bundle (fan length nought).
    QToolButton* fanStartOn_ = nullptr;
    QToolButton* fanFinishOn_ = nullptr;
    QDoubleSpinBox* knee_ = nullptr;
    QDoubleSpinBox* fanKnee_ = nullptr;
    QDoubleSpinBox* fanScale_ = nullptr;
    QDoubleSpinBox* bodyScale_ = nullptr;
    QDoubleSpinBox* bodyWidth_ = nullptr;
    QListWidget* stack_ = nullptr;
    QComboBox* transitions_ = nullptr;
    QDoubleSpinBox* transitionWidth_ = nullptr;   // never below the cable's width
    QToolButton* showDesignator_ = nullptr;       // the eye on the Designator line: written on the drawing or not
    // UNDO AND REDO: the design as it was after each change, and where in that list it is now.
    std::vector<QByteArray> history_;
    int historyAt_ = -1;
    bool restoring_ = false;
    void recordHistory();
    void restoreHistory(int at);
public:
    void undo();
    void redo();
    bool canUndo() const { return historyAt_ > 0; }
    bool canRedo() const { return historyAt_ + 1 < static_cast<int>(history_.size()); }
private:
    QToolButton* dimTool_ = nullptr;              // putting a dimension on by hand
    QComboBox* dimSide_ = nullptr;                // above the parts or below them
    QDoubleSpinBox* designatorHeight_ = nullptr;  // how tall its letters are
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
