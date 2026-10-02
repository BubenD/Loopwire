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

// THE MARKS ON A BRICK: whether its part is on the bill (-1 no part to put on it, 0 not on it, 1 on it), and how
// well it suits what is linked to it (-1 not checked, 0 suits, 1 something unknown or not linked yet, 2 does not
// suit) - with why.
struct BrickMark {
    int bom = -1;
    int compat = -1;
    QString why;
};

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
    // THE MARKS ON EACH BRICK: a dot for the bill and one for how well its part suits what it is linked to.
    void setMarks(const QHash<QString, BrickMark>& marks) {
        marks_ = marks;
        update();
    }
    BrickMark markOf(const QString& id) const { return marks_.value(id); }
    // Zoomed and centred so every brick is in view - the Fit button.
    void fit();
    // Centred on the bricks at the zoom it is at - what arranging or loading a design does.
    void centreView();
    // Back to the pane's own size, keeping the middle where it is.
    void resetZoom();
    void deleteSelected();
    void moveSelection(double dCol, double dRow);
    void rotateSelection();
    // ON THE DRAWING ONLY: the picked parts turned (R 45, Space 90) or mirrored (M).
    void turnOnDrawing(double deg);
    void mirrorOnDrawing();
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
    // A brick double-clicked: its part brought up on the drawing.
    void brickDoubleClicked(const QString& id);
    // "Open in library" on a brick: its part (or one of its layers' parts) to be shown in the parts library.
    void openInLibrary(const QString& partId);
    // "Update from library" on a brick: its part read again from the library as it is now.
    void updateFromLibrary(const QString& brickId);
    // Two bricks clicked while putting a dimension on: the dimension goes between them.
    void dimensionPicked(const QString& a, const QString& b);
    // A brick clicked while setting another's drawing order: `source` goes above (or under) `other`.
    void orderPicked(const QString& source, const QString& other, bool above);
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
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    bool event(QEvent* e) override;
public:
    // How far apart the drawing's rows were last drawn - what a brick moved a row is kept still on the drawing by.
    void setDrawingRowPitch(double mm) { drawnRowPitch_ = mm; }
protected:
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
    double drawnRowPitch_ = 0.0;
    QHash<QString, BrickMark> marks_;
    void keepOnDrawing(const QHash<QString, QPointF>& from);
    bool dimMode_ = false;                 // putting a dimension on: two clicks, two bricks
    QString dimFirst_;
    QString orderSource_;                  // setting a brick's drawing order: the next brick clicked is the other
    bool orderAbove_ = true;
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
    // THE DIMENSION WIZARD: every dimension of the harness in one window - which runs are dimensioned and how, those
    // put on by hand, the points they are measured from, and the side they all go.
    void openDimensionWizard();
    QDialog* dimensionWizard() const { return dimWizard_; }
    // UPDATE FROM LIBRARY: the library read again from its files, and the brick - its part's kind, its drawing and
    // faces, its contacts, its lines on the bill - brought up to what the part is there now. Returns whether the part
    // was found.
    bool updateBrickFromLibrary(const QString& brickId);

    // THE BILL. Puts one of a part on the drawing's bill of materials. A part the bill already lists
    // is not added without asking; `ask` answers the question (the window asks the user when none is
    // set - a test sets one). Returns whether a line was added.
    bool addPartToBom(const QString& partId, double qty = 1.0);
    // WHICH CONTACT, when a connector bought without its contacts goes on the bill and several are tied to it:
    // asked (a list to pick from), or answered by this - for a test. Returns the contact's id, or empty for none.
    void setContactChooser(std::function<QString(const QStringList& contactIds)> fn) { chooseContact_ = std::move(fn); }
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
    // A brick's part asked to be opened in the parts library.
    void openInLibraryRequested(const QString& partId);

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
    // THE JOINT'S PAGE: the splice made in the run it is tied to - how, with what, which wires in and out, where.
    QWidget* buildJointPage();
    void loadJoint();
    void editJoint(const std::function<void(BrickJoint&)>& fn);
    QLabel* jointRun_ = nullptr;
    QComboBox* jointMethod_ = nullptr;
    QListWidget* jointParts_ = nullptr;
    QLabel* jointPartsNote_ = nullptr;
    QLabel* jointLen_ = nullptr;
    QLineEdit* jointIn_ = nullptr;
    QLabel* jointInNames_ = nullptr;
    QSpinBox* jointOutCount_ = nullptr;
    QComboBox* jointOutPart_ = nullptr;
    QLineEdit* jointOutNames_ = nullptr;
    QDoubleSpinBox* jointAt_ = nullptr;
    QComboBox* jointFrom_ = nullptr;
    QCheckBox* jointStepped_ = nullptr;
    QDoubleSpinBox* jointStep_ = nullptr;
    QPushButton* jointShowSpan_ = nullptr;
    QString jointShow_;   // the joint whose stretch of cable the preview lights
public:
    QString jointShown() const { return jointShow_; }
private:
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
    QHash<QString, double> lastPartX_;     // and where each part was along the harness
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
    QComboBox* dimSideTotal_ = nullptr;     // each run dimension above or below the run
    QComboBox* dimSideFans_ = nullptr;
    QComboBox* dimSideBetween_ = nullptr;
    QSpinBox* pairs_ = nullptr;          // a twisted pair's number of pairs
    // THE DRAWING AS LAST BUILT, without the grab rings - which are shown only for the part picked or pointed at.
    std::vector<Entity> lastEnts_;
    QString hoverPart_;
    void showGrabRings();
    double lastRowPitch() const;
    QString bridgeValue() const;
    // A PART DRAGGED ON THE DRAWING (the preview): its run's length follows, the pane stays as it is.
    QString partDrag_;
    // A RUN BEING SHORTENED by Ctrl + dragging its end on the drawing: the run, the way that end points, its length
    // when the drag began and where the drag began.
    QString runDrag_;
    Vec2 runDragDir_{1.0, 0.0};
    double runDragLen0_ = 0.0;
    Vec2 runDragFrom_;
    Vec2 partDragFrom_;
    BrickDesign partDragDesign_;
    BrickBuild partDragBuilt_;
    QString previewPartAt(const QPointF& at) const;
    double handleMm_ = 2.0;   // the grab point's size on the drawing just shown
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
    // THE PICKED LAYER'S PART NUMBER, from the parts list - or copied from the cables next along.
    QComboBox* layerPart_ = nullptr;
    void loadLayerPart();
public:
    // The layers' part numbers of the specials next to this one - through a part (ignored) or a junction -
    // copied onto it where it has the same layer. Returns how many layers were given a part.
    int layerPartsFromNeighbours(const QString& special);
private:
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
    // CLEAR: every brick and link off the pane - the name and what is saved stay, Ctrl+Z brings it back.
    void clearAll();
    void undo();
    void redo();
    bool canUndo() const { return historyAt_ > 0; }
    bool canRedo() const { return historyAt_ + 1 < static_cast<int>(history_.size()); }
private:
    QToolButton* dimTool_ = nullptr;              // putting a dimension on by hand
    QComboBox* dimSide_ = nullptr;                // (the part's own Dimensions side, on its panel)
    QComboBox* partDimSide_ = nullptr;            // the picked part's dimensions: above it or below it
    std::function<QString(const QStringList&)> chooseContact_;
    QCheckBox* tieTurn_ = nullptr;   // the bricks turn with the drawing
    QComboBox* dimAllSide_ = nullptr;   // every dimension above the objects, or below
    QDialog* dimWizard_ = nullptr;
    QTableWidget* dimRuns_ = nullptr;
    QTableWidget* dimHand_ = nullptr;
    QComboBox* dimWizFrom_ = nullptr;
    QComboBox* dimWizTo_ = nullptr;
    QComboBox* dimWizSide_ = nullptr;
    bool dimWizQueued_ = false;
    void refreshDimensionWizard();
    void setAllDimsSide(bool below);
    // THE MARKS ON THE BRICKS: on the bill or not, and whether the part suits what is linked to it.
    void refreshBrickMarks();
    BrickMark compatibilityOf(const Brick& b) const;
    QToolButton* bridgeMany_ = nullptr;   // a bridge wire taking several of the cable's wires
    // THE LIVE BILL: how many bricks use each part, last time the bill was brought into step with them.
    QHash<QString, int> liveCounts_;
    // AND BY LENGTH: how many millimetres of each run's layers' parts (a cable, a braid, a heatshrink - its wires,
    // so many times over) the runs take, each at its LONGEST branch.
    QHash<QString, double> liveLengths_;
    bool liveLenInit_ = false;
    QHash<QString, double> partLengths() const;
    void syncLiveLengths();
    QComboBox* bridgeAt_ = nullptr;   // which branch of its cable a bridge wire comes out of
    bool partDragOwn_ = false;        // Shift held: the part dragged gets a length of its own
    // What the drawing made of the runs, for the bill (see refreshPreview).
    QHash<QString, double> extraBranchLengths_;
    QHash<QString, int> extraTransitions_;
    QHash<QString, double> extraTransitionMm_;   // and how long they come to
    QHash<QString, int> extraLayerCount_;        // the layers over them, by "<special>|<part>"
    QHash<QString, double> extraLayerMm_;
    QHash<QString, int> extraFanEnds_;
    // THE BRANCHES AND WIRE ENDS section of a run's panel.
    QWidget* branchesBox_ = nullptr;
    QComboBox* transitionPart_ = nullptr;
    QComboBox* branchCover_ = nullptr;
    QComboBox* branchShrink_ = nullptr;
    QTableWidget* branches_ = nullptr;
    QCheckBox* fanShrink_ = nullptr;
    QDoubleSpinBox* fanShrinkLen_ = nullptr;
    QComboBox* fanShrinkPart_ = nullptr;
    QDoubleSpinBox* transLenStart_ = nullptr;   // each transition's own length, 0 Auto
    QDoubleSpinBox* transLenFinish_ = nullptr;
    QToolButton* transLenStartAuto_ = nullptr;  // "A": as long as the drawing makes it, the field showing that
    QToolButton* transLenFinishAuto_ = nullptr;
    QComboBox* transInsideEnds_ = nullptr;      // which transition is inside the backshell
    QComboBox* transLayerPick_ = nullptr;       // a heatshrink to put on over the transition
    QListWidget* transLayers_ = nullptr;        // the layers over it, inside out
    QListWidget* fanShrinkParts_ = nullptr;     // the parts whose wire ends get heatshrink
    QFormLayout* transitionForm_ = nullptr;     // the run's transition heatshrink box
    QCheckBox* fanShrinkInside_ = nullptr;      // the wire-end heatshrink inside the backshell: on the bill, not drawn
    QCheckBox* transitionInside_ = nullptr;     // the transition heatshrink likewise
    QCheckBox* transStretch_ = nullptr;         // stretched over the end part
    QDoubleSpinBox* transStretchLen_ = nullptr; // and how far
    QToolButton* transStretchAuto_ = nullptr;   // "A": a tenth of the part's length
    bool rightSized_ = false;                   // the right's panel and bill set to 3:1 once shown
    bool matchSetting_ = false;                 // the scale is being set by Match, not typed
    // Match lit blue while the part's size is the one Match gave it.
    void showMatched();
    // A DIMENSION PICKED ON THE PREVIEW, and one being dragged in or out: its key, where the drag began, the way it
    // moves (square to what it measures) and how far off its place it was then.
    QString dimSel_;
    QString dimDrag_;
    Vec2 dimDragFrom_;
    Vec2 dimDragN_{0.0, 1.0};
    double dimDragShift0_ = 0.0;
    QString previewDimAt(const Vec2& world) const;
public:
    QString selectedDimension() const { return dimSel_; }
private:
    void loadBranches();
    bool liveInit_ = false;
    QHash<QString, int> partCounts() const;
    void syncLiveBom();
    // A connector just put on the bill: its contacts with it, as the connector needs them (one per way).
    void addContactsFor(const LibraryPart& connector);
    // WHAT GOES WITH IT: a connector bought without its contacts needs contacts on the bill - said, with the
    // contacts tied to it offered.
    QLabel* compat_ = nullptr;
    QPushButton* compatAdd_ = nullptr;
    void checkCompatibility(const Brick* b);
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
