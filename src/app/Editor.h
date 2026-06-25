#pragma once

#include "commands/UndoStack.h"
#include "geometry/Snap.h"
#include "geometry/Vec2.h"
#include "harness/Harness.h"
#include "model/Document.h"
#include "render/EntityRenderer.h"
#include "tools/ToolId.h"

#include <QElapsedTimer>
#include <QJsonValue>
#include <QObject>
#include <QString>
#include <algorithm>
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
    // Style passed to/from the text editor (font, size, paragraph formatting).
    struct TextStyle {
        double size = 5.0;
        QString font;
        bool bold = false;
        bool italic = false;
        bool underline = false;
        bool strike = false;
        QString justify = QStringLiteral("TL"); // TL..BR (vert T/M/B + horiz L/C/R)
        double lineSpacing = 1.0;
        QString list = QStringLiteral("none"); // none / bullet / number
        double boxWidth = 0.0;  // container wrap width (0 = none)
        double boxHeight = 0.0; // container height (0 = auto)
    };
    // Place a styled text/mtext entity at the pending click position; `text` may
    // contain newlines for multiline. Empty `text` cancels.
    void commitText(const QString& text, const TextStyle& s);
    // Update an existing text entity's content/style (used by double-click edit).
    void applyTextEdit(const QString& id, const QString& text, const TextStyle& s);
    // Ask the host to open the rich text editor for entity `id` (Properties "Edit...").
    void emitEditText(const QString& id) { emit editTextRequested(id); }
    // The pending text insertion box (from the click / MText box drag) in world
    // coords, for positioning the on-canvas editor. Returns false if none pending.
    bool pendingTextBox(Vec2& topLeft, double& w, double& h) const;

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

    // Dynamic-input reference fields (AutoCAD-style). For the current draw step
    // these are the typed references — line/segment: Length + Angle; circle:
    // Radius or Diameter; ellipse: X radius + Y radius. Tab cycles the active
    // field; a committed value feeds the next point. Empty when not applicable.
    std::vector<QString> drawInputFieldLabels() const;
    QString currentDrawFieldLabel() const;
    int drawInputFieldIndex() const { return drawFieldIndex_; }
    bool hasDrawInputFields() const { return !drawInputFieldLabels().empty(); }
    // One-line help of the applicable commands/options for the active draw tool,
    // shown in the bottom command bar while drawing (empty for Select/modify).
    QString drawCommandHint() const;
    // True if the typed letter is a valid option for the active draw tool (so the
    // flying input field should open for it). Lets the canvas ignore irrelevant
    // letters mid-draw — e.g. "f" during a Line does nothing instead of popping a
    // command box. Digits/coords are always accepted by the caller.
    bool isDrawOptionChar(QChar ch) const;
    void cycleDrawInputField();              // Tab: move to the next reference field
    void lockDrawField(const QString& text); // store the typed value in the active field
    bool commitDrawFields();                 // build the point from the fields and place it

    // Edit actions.
    void selectAll();
    void selectById(const QString& id, bool additive = false);
    void clearSelection();
    void deleteSelected();

    // Current draw color applied to newly created entities (empty => ByLayer).
    void setCurrentColor(const QString& hex) { currentColor_ = hex; }
    const QString& currentColor() const { return currentColor_; }

    // Apply a common property to all selected entities.
    void setSelectionLayer(const QString& layer);
    void setSelectionColor(const QString& hex); // empty => ByLayer
    void setSelectionWeight(double weight);      // 0 => ByLayer
    void setSelectionMeta(const QString& key, const QString& value); // linetype, plot style, ...
    void setSelectionTextMeta(const QString& key, const QJsonValue& value); // text font/size/bold/...
    void setSelectionHatchMeta(const QString& key, const QJsonValue& value); // hatch pattern/angle/...
    void setHatchMeta(const QString& id, const QString& key, const QJsonValue& value); // by id
    void setEntityColor(const QString& id, const QString& hex);  // explicit hex, empty = ByLayer
    // Id of the most recently created hatch in the active Hatch session (for live
    // edit while creating), or empty.
    const QString& lastHatchId() const { return lastHatchId_; }

    // Right-button drag (AutoCAD-style): press-drag-release on a selection (or the
    // object under the cursor) moves a ghost; the drop offers Move / Copy / Cancel.
    bool beginRightDrag(const Vec2& world); // false if there's nothing to drag
    void updateRightDrag(const Vec2& world);
    void commitRightDrag(bool copy);        // copy=false -> move the originals
    void cancelRightDrag();
    bool isRightDragging() const { return rightDrag_; }
    void setGeometryValue(const QString& key, double value);         // Center X/Y, Width, Height, Radius
    void setColorsByLayer();  // strip explicit color so entities follow their layer (selection, or all)
    void copySelection();          // Ctrl+C: copy (base point = selection bbox min)
    void copyWithBase(bool cut);   // Ctrl+Shift+C / Ctrl+X: then pick a base point
    void paste();                  // Ctrl+V: then click to place (relative to base)
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

    // Circle / arc / ellipse construction methods (AutoCAD-style flyouts).
    enum class CircleMode { CenterRadius, CenterDiameter, TwoPoint, ThreePoint,
                            TanTanRadius, TanTanTan };
    enum class ArcMode { ThreePoint, StartCenterEnd, CenterStartEnd, StartEndRadius };
    enum class EllipseMode { Center, AxisEnd, EllipticalArc };
    // Spline construction: Fit passes through the points; CV (control vertices)
    // is shaped by them (a B-spline).
    enum class SplineMode { Fit, CV };
    void setCircleMode(CircleMode m) { circleMode_ = m; }
    void setArcMode(ArcMode m) { arcMode_ = m; }
    void setEllipseMode(EllipseMode m) { ellipseMode_ = m; }
    void setSplineMode(SplineMode m) { splineMode_ = m; }
    // Flip the arc being drawn to the opposite (mirrored) sweep — the "mi" option
    // while the Arc tool is active (also offered in the canvas right-click menu).
    void toggleArcMirror();
    bool isArcMirrored() const { return arcMirror_; }
    CircleMode circleMode() const { return circleMode_; }
    ArcMode arcMode() const { return arcMode_; }
    EllipseMode ellipseMode() const { return ellipseMode_; }
    SplineMode splineMode() const { return splineMode_; }

    // Number of sides for the polygon tool (typed before placing the center).
    void setPolygonSides(int n) { polygonSides_ = std::clamp(n, 3, 64); }
    int polygonSides() const { return polygonSides_; }

    // Hatch / Wipeout tool options (AutoCAD "Hatch Creation"-style). The boundary
    // is either picked as a closed polygon (PickPoints) or taken from a clicked
    // closed object (SelectObject).
    enum class HatchBoundary { PickPoints, SelectObject };
    struct HatchStyle {
        QString pattern = QStringLiteral("ansi31"); // solid/ansi31/.../brick/braid/...
        double angle = 0.0;     // pattern rotation (degrees)
        double scale = 1.0;     // pattern spacing multiplier
        int transparency = 0;   // 0..95 (%)
        QString color;          // explicit hex, or empty = ByLayer
        bool associative = true;
        bool manyJoints = true; // many boundary joints on curves (off: circle->4, arc->3)
        HatchBoundary boundary = HatchBoundary::PickPoints;
    };
    HatchStyle& hatchStyle() { return hatchStyle_; }
    const HatchStyle& hatchStyle() const { return hatchStyle_; }

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
    // Double-clicked an existing text object — the host opens the editor for it.
    void editTextRequested(const QString& id);
    // Ask the canvas to pop the flying input field at the cursor, pre-labelled,
    // for an inline value (e.g. fillet radius / gap) — so the user can type it
    // immediately and keep drawing.
    void dynamicPrompt(const QString& placeholder);
    // The hatch live-edit target changed (a hatch was created, or the session
    // ended) — the host re-points the contextual Hatch tab.
    void hatchTargetChanged();

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

    // Hatch "Pick Points": the smallest closed boundary (existing closed entity)
    // that encloses `seed`, sampled to a polygon — empty if none surrounds it.
    std::vector<Vec2> pickEnclosingBoundary(const Vec2& seed) const;
    // Create a Hatch/Wipeout from a boundary polygon and stay in the tool so more
    // regions can be filled (Esc / Close exits).
    void commitHatchBoundary(const std::vector<Vec2>& pts);

    void handleDrawPress(const Vec2& p);
    void handleTangentPick(const Vec2& p); // Circle Tan-Tan-Radius/Tan-Tan-Tan: pick objects
    void buildTangentCircle();             // solve + place once enough objects are picked
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
    // Polyline Arc-segment mode (AutoCAD PLINE "A"/"L"): while on, each new vertex
    // adds an arc tangent to the previous segment. draftBulges_ parallels draftPts_
    // (bulge of the segment ENDING at that index; [0] unused).
    bool polyArcMode_ = false;
    std::vector<double> draftBulges_;
    QString pendingTextPos_; // serialized point while awaiting text input
    // Dynamic-input reference fields for the active draw step (Tab cycles them).
    int drawFieldIndex_ = 0;
    std::vector<std::optional<double>> drawFieldVals_;

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
    QString editEntityId_;    // primary entity whose grip is being dragged
    int editRole_ = 0;        // GripRole (see Editor.cpp)
    int editIndex_ = 0;       // vertex/corner index for the grip
    Vec2 editLast_;           // last drag position (for move deltas)
    Vec2 editAnchor_;         // fixed reference for ortho/polar while grip-dragging
    bool editMoved_ = false;  // has the drag actually moved yet (for undo push)
    // All joints being dragged together: a grabbed point-joint moves every
    // coincident joint of the selected entities (shared joints stay shared).
    struct EditGrip { QString id; int role; int index; Vec2 offset{0, 0}; };
    std::vector<EditGrip> editGrips_;
    // Snapshot of the selected entities at their ORIGINAL position when a move /
    // joint drag begins: drawn as a faint ghost so the source stays visible until
    // the new placement click commits.
    std::vector<Entity> editGhost_;

    // Right-button drag state (move/copy via a drop menu).
    bool rightDrag_ = false;
    Vec2 rightDragStart_;
    Vec2 rightDragCur_;
    std::vector<Entity> rightDragGhost_; // originals snapshot, drawn translated as a preview

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
    // Double line/polyline justification: false = clicked path is the centerline;
    // true ("ls" / line-start) = the clicked path is one edge, the parallel run is
    // offset to one side.
    bool dpolySideJustify_ = false;
    bool showAngles_ = true; // show the live length/angle readout while drawing
    QString currentColor_;   // color for new entities (empty => ByLayer)
    CircleMode circleMode_ = CircleMode::CenterRadius;
    ArcMode arcMode_ = ArcMode::ThreePoint;
    EllipseMode ellipseMode_ = EllipseMode::Center;
    SplineMode splineMode_ = SplineMode::Fit;
    bool arcMirror_ = false; // arc being drawn is flipped to the opposite sweep
    // Tangent-circle construction (Tan-Tan-Radius / Tan-Tan-Tan): the objects the
    // new circle must touch, the click points (to pick which solution), and the
    // pending-radius state for TTR.
    std::vector<QString> tanPickIds_;
    std::vector<Vec2> tanPickPts_;
    bool tanAwaitRadius_ = false;
    double tanRadius_ = 50.0;
    HatchStyle hatchStyle_;       // current Hatch/Wipeout options
    QString lastHatchId_;         // most recent hatch created in this session (live edit)
    int polygonSides_ = 5;        // default side count (prompted on tool select)
    bool rectAwaitY_ = false;     // rectangle: awaiting the Y length after the X length
    double rectX_ = 0;            // rectangle: signed X length already entered

    // Clipboard with a base point (AutoCAD COPYBASE / cut / paste-to-point).
    Vec2 clipboardBase_{0, 0};    // reference point the clipboard is stored relative to
    bool awaitClipBase_ = false;  // next click sets the base point (after Ctrl+Shift+C / Ctrl+X)
    bool clipCut_ = false;        // the pending base-point op is a cut (delete originals)
    bool pastePlacing_ = false;   // paste in progress: next click drops the contents
    // Cut originals stay visible until the paste lands; these ids are removed when
    // the contents are finally placed (so a cut object isn't gone mid-move).
    std::vector<QString> cutPending_;
};

} // namespace Edraw_Desktop
