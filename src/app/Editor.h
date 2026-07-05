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
    // Modify tools that accept a TYPED numeric value (e.g. Scale factor) even though
    // they are not draw tools — lets a digit pop the flying input field at the cursor.
    bool wantsValueInput() const { return tool_ == ToolId::Scale || tool_ == ToolId::Offset; }

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
    // Table (#2): the spec for the NEXT table placed (AutoCAD Insert Table), and cell
    // text editing. A table has an optional Title row (merged) + Header row + data rows.
    struct TableSpec {
        int cols = 5;
        double colWidth = 40.0;
        int dataRows = 1;
        double rowHeight = 10.0;
        bool title = true;
        bool header = true;
    };
    void setTableSpec(const TableSpec& s) { tableSpec_ = s; }
    // Cell (row-major index) at world point `p` in table `e`; -1 if outside. Sets the
    // cell's top-left world corner and its world width/height.
    int tableCellAt(const Entity& e, const Vec2& p, Vec2& cellTopLeft, double& cw, double& ch) const;
    void applyTableCellEdit(const QString& id, int cell, const QString& text);
    // Endpoints of edge `seg` of a polyline/rect/polygon AS DRAWN — clipped inward at
    // each end by any existing fillet/chamfer setback, so a highlighted "wall" matches
    // the visible (already-modified) outline instead of overshooting to a sharp vertex.
    std::pair<Vec2, Vec2> visibleWall(const Entity& e, int seg) const;
    void clearSelection();
    void deleteSelected();

    // Current draw color applied to newly created entities (empty => ByLayer).
    void setCurrentColor(const QString& hex) { currentColor_ = hex; }
    const QString& currentColor() const { return currentColor_; }
    // Default lineweight for newly created entities (0 => ByLayer).
    void setCurrentWeight(double w) { currentWeight_ = w; }
    double currentWeight() const { return currentWeight_; }

    // Apply a common property to all selected entities.
    void setSelectionLayer(const QString& layer);
    void setSelectionColor(const QString& hex); // empty => ByLayer
    void setSelectionWeight(double weight);      // 0 => ByLayer
    void setSelectionMeta(const QString& key, const QString& value); // linetype, plot style, ...
    void setSelectionMetaNumber(const QString& key, double value);   // gap, ...
    void setSelectionTextMeta(const QString& key, const QJsonValue& value); // text font/size/bold/...
    void setSelectionHatchMeta(const QString& key, const QJsonValue& value); // hatch pattern/angle/...
    void setSelectionDimMeta(const QString& key, const QJsonValue& value); // dimension props
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
    // Toggle a mirror of the dragged set about the catch location (X axis = a
    // horizontal mirror line, Y axis = vertical). Text mirrors position only.
    void rightDragMirror(bool xAxis);
    void setGeometryValue(const QString& key, double value);         // Center X/Y, Width, Height, Radius
    void setColorsByLayer();  // strip explicit color so entities follow their layer (selection, or all)
    void copySelection();          // Ctrl+C: copy (base point = selection bbox min)
    void cutSelection();           // Ctrl+X: copy to clipboard + delete the originals
    void copyWithBase(bool cut);   // Ctrl+Shift+C: then pick a base point
    void paste();                  // Ctrl+V: then click to place (relative to base)
    void undo();
    void redo();

    // Advanced modify (selection-based actions invoked from the UI).
    void arraySelected(int rows, int cols, double dx, double dy);
    void explodeSelected();    // break compound objects into their drawn primitives
    void purgeUnused();        // AutoCAD PURGE: drop empty layers + stray/degenerate objects
    // Balloon multileader ops (act on the selected balloons).
    void addLeaderToSelection();     // add a leader to the selected balloon
    void removeLeaderFromSelection();// remove the last leader of the selected balloon
    void alignBalloons();            // line the selected balloons up, evenly spaced
    void collectBalloons();          // pack the selected balloons into a touching row
    void repeatLastTool();
    ToolId lastTool() const { return lastTool_; }     // re-invoke the last used tool (Space / Enter when idle)
    void setFilletRadius(double r) { filletRadius_ = r; }
    void setChamferDistance(double d) { chamferDist_ = d; }
    // Pending polyline fillet radius for the next corner(s) drawn (0 = sharp).
    void setCornerRadius(double r) { cornerRadius_ = r; }
    double cornerRadius() const { return cornerRadius_; }
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
    // Dimension type (AutoCAD-style flyout).
    enum class DimMode { Linear, Aligned, Angular, ArcLength, Radius, Diameter, Ordinate, Jogged };
    void setDimMode(DimMode m) { dimMode_ = m; }
    DimMode dimMode() const { return dimMode_; }

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
    // Double-clicked a table cell — the host opens the inline editor for that cell.
    void editTableCellRequested(const QString& id, int cell, const QString& text, Vec2 at);
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

    // Hatch "Pick Points": the smallest closed entity that encloses `seed`, or
    // nullptr if none surrounds it.
    const Entity* enclosingEntity(const Vec2& seed) const;
    // Hatch "Pick Points" fallback: the smallest closed loop of edges (formed by
    // ANY combination of separate visible objects) that encloses `seed`. Returns
    // the loop polygon, or nullopt if the seed isn't inside a closed region.
    std::optional<std::vector<Vec2>> enclosingRegionLoop(const Vec2& seed) const;
    // The bounded region to hatch at `seed`: the SMALLER of the smallest enclosing
    // closed object and the cell traced from all crossing objects (so a line cutting
    // through a rectangle fills only the sub-cell, not the whole rectangle). Exactly
    // one of {ent, loop} is set on success; both empty if nothing encloses the seed.
    struct HatchRegion {
        const Entity* ent = nullptr;            // a single closed object won
        std::optional<std::vector<Vec2>> loop;  // a traced multi-object cell won
    };
    // `allowTrace` runs the O(n^2) multi-object boundary tracer; pass false for the
    // live cursor preview on big drawings to keep it responsive (the click still
    // traces). The single-object path (enclosingEntity) is always cheap.
    HatchRegion pickHatchRegion(const Vec2& seed, bool allowTrace = true) const;
    // Create a Hatch/Wipeout from a source boundary entity (circle shapes stay a
    // true circle when "multiple joints" is off; everything else is a polygon).
    void commitHatchFromSource(const Entity& src);
    // Create a Hatch/Wipeout from a boundary polygon; stays in the tool.
    void commitHatchBoundary(const std::vector<Vec2>& pts);
    // Create a circle-boundary Hatch (fills the circle; 4 joints resize it).
    void commitHatchCircle(const Vec2& center, double radius);
    // Finish a hatch/wipeout entity: stamp color + pattern meta, add it, set it as
    // the live-edit target, and notify. (geometry already filled by the caller)
    void addHatchEntity(Entity e);

    // Tools where click/window selection of objects is allowed (Select, plus the
    // Hatch/Wipeout tools so you can still select while hatching).
    bool selectionTool() const {
        return tool_ == ToolId::Select || tool_ == ToolId::Hatch || tool_ == ToolId::Wipeout;
    }
    void handleSelectPress(const Vec2& p, const Vec2& raw, Qt::KeyboardModifiers mods);
    // Trim every line a fence/cut line crosses back to the reference cutting edge.
    void applyTrimFence(const Vec2& a, const Vec2& b);
    // Extend every line a fence line crosses to the reference boundary object.
    void applyExtendFence(const Vec2& a, const Vec2& b);
    // Record the exact cut boundary (line or circle) at each end of a trimmed/
    // extended DOUBLE entity so the renderer clips both physical runs flush to it.
    void recordCutMeta(Entity& ent, const QString& selfId, const std::vector<Entity>& joints);
    bool tryHatchAt(const Vec2& p); // attempt a hatch/wipeout fill; false if nothing to fill
    void handleDimensionPress(const Vec2& p); // multi-mode dimension placement

    void handleDrawPress(const Vec2& p);
    void handleTangentPick(const Vec2& p); // Circle Tan-Tan-Radius/Tan-Tan-Tan: pick objects
    void buildTangentCircle();             // solve + place once enough objects are picked
    void handleModifyPress(const Vec2& p);
    // Offset tool (AutoCAD OFFSET): set a distance (two clicks or a typed value), then
    // repeatedly pick an object and click the side to drop an offset copy. Esc exits.
    void handleOffsetPress(const Vec2& p);
    // The selection transformed by the in-progress modify (Move/Rotate/Mirror/
    // Scale/Offset) using the collected points + the live cursor — drawn as a ghost
    // preview until the command finishes.
    std::vector<Entity> modifyPreview() const;
    // Reference length for a 2-click scale: the selection's bounding-box diagonal,
    // so dragging out to ~that distance is ~1x.
    double selectionRefLength(const std::vector<Entity*>& sel) const;
    void handlePickModifyPress(const Vec2& p);
    // Apply the active corner modifier (Fillet radius or Chamfer distance) to vertex
    // `knee` of `e`; `corner` is the three-point corner and `gap` the double-line gap
    // (0 if single). Writes cornerRadii/cornerChamfers and clears the other on that knee.
    void applyCornerModAt(Entity& e, int knee, const std::vector<Vec2>& corner, double gap);
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
    // Per-corner fillet radius while drafting a (double) polyline. Parallels
    // draftPts_; entry i is the radius of the corner at vertex i (0 = sharp). A
    // fillet radius set with "f" applies to each corner completed from then on
    // (the next joint and onward), never retroactively. [0] / last are endpoints.
    std::vector<double> draftCornerRadii_;
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
    bool rdMirrorX_ = false; // mirror the dragged set across the X axis (horizontal line)
    bool rdMirrorY_ = false; // ... across the Y axis (vertical line) — about the catch point
    Vec2 rightDragStart_;
    Vec2 rightDragCur_;
    std::vector<Entity> rightDragGhost_; // originals snapshot, drawn transformed as a preview
    Entity rightDragResult(const Entity& src) const; // mirror(s) about start + translate to cursor

    // Modify command state: collected reference points.
    std::vector<Vec2> modifyPts_;

    // Offset tool state.
    double offsetDist_ = 0.0;             // the offset distance once set
    bool offsetHasDist_ = false;          // distance defined (by 2 clicks or typed)?
    std::vector<Vec2> offsetDistPts_;     // the 2-click distance definition, transient
    QString offsetSourceId_;              // object picked to offset (empty = awaiting pick)

    // Pick-modify state (trim/extend/fillet/chamfer).
    std::vector<QString> pickIds_;
    std::vector<Vec2> pickPts_;
    double filletRadius_ = 5.0;
    double chamferDist_ = 5.0;
    // Trim with a reference cutting edge + a drawn fence (AutoCAD fence trim): the
    // objects selected when Trim starts are the cutting edges; the user then drags a
    // cut line and every object it crosses is trimmed back to the reference.
    std::vector<QString> trimRefIds_;
    bool trimFenceActive_ = false;
    Vec2 trimFenceStart_;

    // Harness defaults.
    ConnectorSpec connectorSpec_ = defaultConnectorSpec(4);
    QString wireColor_ = "WH";
    QString wireGauge_ = "0.5";
    QString wireNet_;
    double doubleLineGap_ = 10.0;
    // Double line/polyline "ls" (line-start): the next click picks a reference line
    // that both physical runs are extended to start from. dpolyLsArm_ awaits that
    // pick; lsRefA_/lsRefB_ hold the picked line until the entity is committed.
    bool dpolyLsArm_ = false;
    bool dpolyHasLsRef_ = false;
    Vec2 lsRefA_;
    Vec2 lsRefB_;
    bool showAngles_ = true; // show the live length/angle readout while drawing
    QString currentColor_;   // color for new entities (empty => ByLayer)
    double currentWeight_ = 0.0; // lineweight for new entities (0 => ByLayer)
    DimMode dimMode_ = DimMode::Linear;
    // For radius/diameter/arc-length/jogged: the circle/arc being measured, captured
    // on the first pick so the second click only places the text/dimension line.
    bool dimHasRef_ = false;
    Vec2 dimRefCenter_;
    double dimRefRadius_ = 0.0;
    double dimRefStart_ = 0.0, dimRefEnd_ = 0.0;
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
    TableSpec tableSpec_;         // spec for the next Table placed (Insert Table dialog)
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
