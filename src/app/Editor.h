#pragma once

#include "commands/UndoStack.h"
#include "geometry/Snap.h"
#include "geometry/Vec2.h"
#include "harness/Harness.h"
#include "model/Document.h"
#include "render/EntityRenderer.h"
#include "tools/ToolId.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <optional>
#include <vector>

class QPainter;
class QTimer;

namespace Edraw_Desktop {

// The interaction brain: owns the active tool, snapping, selection, undo stack,
// and in-progress geometry. The CanvasWidget forwards input here and asks it to
// paint overlays (previews, selection highlights, snap markers). Mirrors the
// role of Edraw's global `state` plus its tool/command logic.
class Editor : public QObject {
    Q_OBJECT
public:
    explicit Editor(QObject* parent = nullptr);

    void setDocument(Document* doc);
    Document* document() const { return doc_; }

    ToolId tool() const { return tool_; }
    void setTool(ToolId t);

    SnapSettings& snapSettings() { return snap_; }
    const SnapSettings& snapSettings() const { return snap_; }

    const std::vector<QString>& selection() const { return selected_; }
    const SnapResult& currentSnap() const { return snapResult_; }

    // Input from the canvas (all in world coordinates).
    void onPointerMove(const Vec2& raw);
    void onPointerPress(const Vec2& raw, Qt::MouseButton button,
                        Qt::KeyboardModifiers mods);
    void onPointerRelease(const Vec2& raw, Qt::MouseButton button);
    void onDoubleClick(const Vec2& raw);
    bool onKey(int key, Qt::KeyboardModifiers mods);

    // Command-bar / dialog text completion (e.g. typed numeric value, text).
    void provideText(const QString& text);

    // Direct distance entry while a line/polyline/etc. is being drawn: a bare
    // number places the next point at that distance along the cursor direction;
    // "x,y" is absolute and "@dx,dy" is relative. Returns true if consumed.
    bool applyDrawInput(const QString& text);
    bool isDrafting() const { return drafting_; }

    // Command-line input typed during drawing (AutoCAD-style). Handles the `g`
    // gap option for double line/polyline and direct distance entry. Returns
    // true if it consumed the text; false means treat it as a command word.
    bool acceptCommandInput(const QString& text);
    // True while typed characters should be routed to the command line.
    bool wantsTextInput() const {
        return drafting_ || isDrawTool(tool_) || gapInputMode_ || filletInputMode_;
    }

    // Edit actions.
    void selectAll();
    void selectById(const QString& id, bool additive = false);
    void clearSelection();
    void deleteSelected();

    // Apply a common property to all selected entities.
    void setSelectionLayer(const QString& layer);
    void setSelectionColor(const QString& hex); // empty => ByLayer
    void setSelectionWeight(double weight);      // 0 => ByLayer
    void setSelectionMeta(const QString& key, const QString& value); // linetype, plot style, ...
    void setGeometryValue(const QString& key, double value);         // Center X/Y, Width, Height, Radius
    void setColorsByLayer();  // strip explicit color so entities follow their layer (selection, or all)
    void copySelection();
    void paste();
    void undo();
    void redo();

    // Advanced modify (selection-based actions invoked from the UI).
    void arraySelected(int rows, int cols, double dx, double dy);
    void explodeSelected();
    void purgeSelected();      // separate objects into single primitives
    void repeatLastTool();     // re-invoke the last used tool (Space / Enter when idle)
    void setFilletRadius(double r) { filletRadius_ = r; }
    void setChamferDistance(double d) { chamferDist_ = d; }
    double filletRadius() const { return filletRadius_; }
    double chamferDistance() const { return chamferDist_; }

    // Harness defaults / editing.
    ConnectorSpec& connectorSpec() { return connectorSpec_; }
    QString& wireColor() { return wireColor_; }
    QString& wireGauge() { return wireGauge_; }
    QString& wireNet() { return wireNet_; }
    double& doubleLineGap() { return doubleLineGap_; } // spacing for the double-line tool
    void setShowAngles(bool v) { showAngles_ = v; emit overlayChanged(); }
    bool showAngles() const { return showAngles_; }

    // Apply an edited connector spec to the currently selected connector.
    void applyConnectorSpecToSelection(const ConnectorSpec& spec);
    // Id of the single selected connector, or empty.
    QString selectedConnectorId() const;

    // Bill of materials row (connector or wire).
    struct BomRow {
        QString kind;     // "Connector" | "Wire"
        QString ref;      // designator / net
        QString part;     // part number / color
        QString detail;   // pin count / gauge
        QString qty;      // count / length
    };
    std::vector<BomRow> buildBom() const;

    bool canUndo() const { return undo_.canUndo(); }
    bool canRedo() const { return undo_.canRedo(); }

    // True while a region (rectangle or lasso) selection is being defined.
    bool hasSelectionBox() const { return regionMode_ != RegionMode::None; }

    // Overlay painting (after entities are drawn).
    void drawOverlay(QPainter& p, const ViewTransform& vt) const;

    // Pixel tolerance (screen px) for snapping/hit testing.
    void setPixelTolerance(double px) { pixelTol_ = px; }

signals:
    void changed();              // geometry/document mutated -> repaint + panels
    void overlayChanged();       // interactive preview/cursor moved -> repaint only (no panel rebuild)
    void selectionChanged();
    void toolChanged(ToolId t);
    void message(const QString& text);
    // Tool needs free-text/numeric input (prompt shown by the host UI).
    void textPromptRequested(const QString& prompt);

private:
    double worldTolerance() const;
    QString uid() const;
    void pushUndo();
    void addEntity(Entity e);
    void finishDraw();         // commit multi-point tool
    void cancelDraw();
    std::optional<QString> hitTest(const Vec2& world) const;
    std::vector<Entity*> selectedEntities();
    Entity* entityById(const QString& id);

    void handleDrawPress(const Vec2& p);
    void handleModifyPress(const Vec2& p);
    void handlePickModifyPress(const Vec2& p);
    void handlePlacementPress(const Vec2& p);
    void finishRegionSelect(bool subtract); // shift = remove from selection
    void drawGrips(QPainter& p, const Entity& e, const ViewTransform& vt) const;
    void onDragTick(); // promotes a held box-drag into a lasso (Edraw behavior)

    // Direct manipulation (Edraw-style): grab a joint (click-to-place) / apply edit.
    bool beginGripGrab(const Vec2& at);
    void applyEditDrag(const Vec2& target);

    Document* doc_ = nullptr;
    ToolId tool_ = ToolId::Select;
    ToolId lastTool_ = ToolId::Select; // last non-Select tool, for repeat
    SnapSettings snap_;
    UndoStack undo_;

    std::vector<QString> selected_;
    std::vector<Entity> clipboard_;

    // Cursor + snap.
    Vec2 cursor_;
    SnapResult snapResult_;
    double pixelTol_ = 10.0;

    // In-progress draw state.
    std::vector<Vec2> draftPts_;
    bool drafting_ = false;
    bool gapInputMode_ = false;    // command line is awaiting a gap value ("g")
    bool filletInputMode_ = false; // command line is awaiting a fillet radius ("f")
    double cornerRadius_ = 0.0;    // current polyline fillet radius (0 = sharp corners)
    QString pendingTextPos_; // serialized point while awaiting text input

    // Region-selection state machine (Edraw model):
    //   first click on empty space -> RectPending (rubber-band rectangle),
    //   second click -> commit; holding the first click ~0.2s -> Lasso.
    enum class RegionMode { None, Deciding, RectPending, Lasso };
    RegionMode regionMode_ = RegionMode::None;
    bool pressed_ = false;        // is the left button currently held
    Vec2 anchor_;                 // first/fixed corner of the region
    Qt::KeyboardModifiers pressMods_ = Qt::NoModifier;
    std::vector<Vec2> dragPath_;  // freeform lasso path
    bool lassoWindowMode_ = true; // window vs crossing, fixed at lasso conversion
    QTimer* dragTimer_ = nullptr; // ticks during the hold for countdown + lasso promote
    QElapsedTimer pressTimer_;    // time since pointer press (for the 0.2s hold)

    // Direct-edit drag state (move whole entity, or move a grip/joint).
    bool editing_ = false;
    bool gripGrab_ = false;   // a joint is grabbed (follows cursor until clicked to place / Esc)
    int editMode_ = 0;        // 0 = move selection, 1 = grip edit
    QString editEntityId_;    // entity whose grip is being dragged
    int editRole_ = 0;        // GripRole (see Editor.cpp)
    int editIndex_ = 0;       // vertex/corner index for the grip
    Vec2 editLast_;           // last drag position (for move deltas)
    Vec2 editAnchor_;         // fixed reference for ortho/polar while grip-dragging
    bool editMoved_ = false;  // has the drag actually moved yet (for undo push)

    // Modify command state: collected reference points.
    std::vector<Vec2> modifyPts_;

    // Pick-modify state (trim/extend/fillet/chamfer).
    std::vector<QString> pickIds_;
    std::vector<Vec2> pickPts_;
    double filletRadius_ = 5.0;
    double chamferDist_ = 5.0;

    // Harness defaults.
    ConnectorSpec connectorSpec_ = defaultConnectorSpec(4);
    QString wireColor_ = "WH";
    QString wireGauge_ = "0.5";
    QString wireNet_;
    double doubleLineGap_ = 10.0;
    bool showAngles_ = true; // show the live length/angle readout while drawing
};

} // namespace Edraw_Desktop
