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
#include <QSet>
#include <QString>
#include <algorithm>
#include <optional>
#include <vector>

class QPainter;
class QTimer;

namespace Edraw_Desktop {

// One schematic design-rule discrepancy. `key` is a stable identity used to match
// user "bypass" (accept) decisions across re-validations; `entityId` + `at` let the
// UI jump to and highlight the offending object.
struct ValidationIssue {
    QString key;
    QString message;
    QString entityId;
    Vec2 at{0.0, 0.0};
};

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
    bool wantsValueInput() const {
        return tool_ == ToolId::Scale || tool_ == ToolId::Offset ||
               tool_ == ToolId::Splice || tool_ == ToolId::NotConnected;
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
    // Table cell selection (AutoCAD-style): a single click inside a cell selects just
    // that cell, enabling the row/column/alignment editing commands.
    QString selectedCellTable() const { return cellTableId_; }
    int selectedCellIndex() const { return cellIndex_; }
    void setCellSelection(const QString& tableId, int cell);
    void clearCellSelection();
    // Cell editing ops (act on the selected cell / its row / its column).
    void tableInsertRow(bool below);
    void tableDeleteRow();
    void tableInsertColumn(bool right);
    void tableDeleteColumn();
    void setCellAlignment(const QString& align); // "TL","TC",...,"BR"
    // Endpoints of edge `seg` of a polyline/rect/polygon AS DRAWN — clipped inward at
    // each end by any existing fillet/chamfer setback, so a highlighted "wall" matches
    // the visible (already-modified) outline instead of overshooting to a sharp vertex.
    std::pair<Vec2, Vec2> visibleWall(const Entity& e, int seg) const;
    // The id of the balloon (its circle or one of its leader lines) under `p`, else empty.
    QString balloonAt(const Vec2& p) const;
    void clearSelection();
    void deleteSelected();

    // Align routing (schematic): the next canvas click picks a reference connector
    // that stays put; every OTHER connector wired to it is shifted so its connecting
    // wires become axis-aligned (horizontal if the run is mostly horizontal, else
    // vertical) and the wires are straightened. alignRoutingFrom runs it directly.
    void beginAlignRouting();
    void alignRoutingFrom(const QString& refConnectorId);
    bool awaitingAlignRef() const { return awaitAlignRef_; }

    // For a wire endpoint: the connector pin it lands on, as "Des/pinLabel" (e.g.
    // "P3/5"), or empty if the point is not on any connector pin (i.e. "open").
    QString pinRefAt(const Vec2& p) const;

    // A unique sequential number for a new connector/wire (stored in meta["entNum"]),
    // one greater than the largest already in the document.
    int assignEntityNumber() const;
    // Schematic design-rule check: returns every problem found (empty = valid):
    //  - a connector pin not on a wire and not terminated by an X (not-connected) marker
    //  - duplicate pin labels within one connector
    //  - a connector mixing numeric and letter pin labels
    //  - a connector with no reference designator / no part number (P/N)
    //  - a wire with an open (dangling) end
    // Each issue carries a stable key + the offending object's id/location.
    std::vector<ValidationIssue> validateSchematic() const;

    // "Bypass" registry: discrepancy keys the user has reviewed and accepted. They are
    // filtered out of the active issue list and listed in the bypass table instead.
    // Session-scoped (not written to the drawing file).
    const QSet<QString>& bypassedIssues() const { return bypassed_; }
    void bypassIssue(const QString& key) { bypassed_.insert(key); }
    void restoreIssue(const QString& key) { bypassed_.remove(key); }

    // Per-rule enable flags for validateSchematic (all on by default; session-scoped).
    // `id` is the rule's key prefix: nc / dup / openwire / mixedpins / norefdes / nopn.
    bool ruleEnabled(const QString& id) const { return !disabledRules_.contains(id); }
    void setRuleEnabled(const QString& id, bool on) {
        if (on) disabledRules_.remove(id);
        else disabledRules_.insert(id);
    }

    // Briefly flash a highlight ring at a world point (used to locate a discrepancy).
    void flashHighlight(const Vec2& at);

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
    void setSelectionMetaBool(const QString& key, bool value);       // showS, ...
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
    // Mirror: after the mirror line is picked, the host offers Copy / Move / Cancel.
    void commitMirror(bool copy);           // copy=false -> mirror the originals in place
    void cancelMirror();
    // Toggle a mirror of the dragged set about the catch location (X axis = a
    // horizontal mirror line, Y axis = vertical). Text mirrors position only.
    void rightDragMirror(bool xAxis);
    void setGeometryValue(const QString& key, double value);         // Center X/Y, Width, Height, Radius
    void setColorsByLayer();  // strip explicit color so entities follow their layer (selection, or all)
    void copySelection();          // Ctrl+C: copy (base point = selection bbox min)
    void cutSelection();           // Ctrl+X: copy to clipboard + delete the originals
    void copyWithBase(bool cut);   // Ctrl+Shift+C: then pick a base point
    void paste();                  // Ctrl+V: then click to place (relative to base)
    bool isPastePlacing() const { return pastePlacing_; }
    // Flip the paste preview before placing: xAxis mirrors across a vertical line
    // through the base (flip X), else a horizontal line (flip Y). Connectors flip too.
    void togglePasteMirror(bool xAxis);
    void undo();
    void redo();

    // Advanced modify (selection-based actions invoked from the UI).
    void arraySelected(int rows, int cols, double dx, double dy);
    void explodeSelected();    // break compound objects into their drawn primitives
    void purgeUnused();        // AutoCAD PURGE: drop empty layers + stray/degenerate objects
    // Add a coupled numbered circle to balloon `id`'s batch, on the side OPPOSITE its
    // arrow/leader (the row grows away from the arrow).
    void addCoupledCircle(const QString& id);
    // Balloon multileader ops (act on the selected balloons).
    void addLeaderToSelection();     // add a leader to the selected balloon
    void removeLeaderFromSelection();// remove the last leader of the selected balloon
    void alignBalloons();            // line the selected balloons up, evenly spaced
    void collectBalloons();          // pack the selected balloons into a touching row
    // Reorder the batch containing balloon `id` into a single touching row (numeric
    // order, arrow circle at the leader end) — minimum "catch" area.
    void orderBalloonRow(const QString& id);
    // Pack the batch containing `id` into a compact triangular cluster (hex rows) —
    // the smallest bounding area (e.g. 2 on top, 1 tucked below).
    void reorderBalloonMinimize(const QString& id);
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
    // Extra wire defaults applied to newly drawn wires (set from the Wire ribbon).
    QString& wireAwg() { return wireAwg_; }
    QString& wireInsulation() { return wireInsulation_; }
    QString& wireVoltage() { return wireVoltage_; }
    QString& wireType() { return wireType_; }
    double& wireRes() { return wireRes_; }          // default resistance for new wires
    QString& wireResUnit() { return wireResUnit_; } // "m" | "ft"
    // Gap between the parallel wire lanes after a knee (align routing): "pitch" uses the
    // reference connector's pitch, "custom" uses wireGapCustom().
    QString& wireGapRule() { return wireGapRule_; }
    double& wireGapCustom() { return wireGapCustom_; }
    // Constrain wire drawing to horizontal/vertical (90°) segments only.
    bool& wireOrtho() { return wireOrtho_; }
    // Default marker sizes for newly placed splices / X (not-connected) terminations.
    double& spliceSize() { return spliceSize_; }
    double& ncSize() { return ncSize_; }
    // Screen (shield) defaults: small radius (rx), drain-leg length, and show the "S".
    double& screenRx() { return screenRx_; }
    double& screenLeg() { return screenLeg_; }
    bool& screenShowS() { return screenShowS_; }
    // Large radius: locked (auto-fit to the caught wires) unless explicitly unlocked.
    bool& screenRyLocked() { return screenRyLocked_; }
    double& screenRy() { return screenRy_; }
    // Large:small radius ratio. Read-only display that re-syncs whenever either radius
    // default is edited; typing a new ratio instead derives Large = Small * ratio
    // (unlocking Large radius). Purely a ribbon convenience for the NEXT shield placed.
    double& screenRatio() { return screenRatio_; }
    // Per-field locks. Three quantities (small radius, large radius, ratio) with only two
    // degrees of freedom (ratio == large/small); each lock disables direct editing of that
    // field and marks it as the dependent that the others drive. Ratio lock takes priority:
    // with the ratio held, editing a radius drives the OTHER radius (even a locked one).
    bool& screenRxLock() { return screenRxLock_; }        // small radius lock
    bool& screenRatioLock() { return screenRatioLock_; }  // ratio lock ("Primary")
    // How many shields to place per crossing-line drag (1-5), laid out side by side.
    int& screenAmount() { return screenAmount_; }
    // Shield outline shape: "ellipse" or "capsule" (a vertical stadium — two semicircle
    // caps joined by straight sides, built from arcs + straight lines).
    QString& screenShape() { return screenShape_; }
    // Which side of the ellipse the drain leg exits from.
    QString& screenLegDir() { return screenLegDir_; } // "top" | "bottom" | "left" | "right"
    bool screenArmed() const { return screenArm_; }
    // Freeze the in-progress screen preview (Tab) so its ribbon fields can be edited
    // without the crossing-line drag changing the caught wires; a click then places it
    // at the frozen end point regardless of where the click lands.
    void freezeScreenPreview();
    bool screenFrozen() const { return screenFrozen_; }

    // Cross-switch tool: click a wire-over-wire crossing to toggle it between a hop
    // (bridge) and a plain cross. Armed until Esc.
    void beginCrossSwitch();
    bool awaitingCrossSwitch() const { return awaitCrossSwitch_; }
    void toggleWireCrossAt(const Vec2& p); // toggle the hop at the crossing nearest p

    // Twist align: select one or more Twist entities, press Twist align, then click a
    // destination point on a wire — every selected twist slides to that x position
    // along its own wires (same constraint as the twist's move grip).
    void beginTwistAlign();
    void twistAlignTo(const Vec2& dest);
    bool awaitingTwistAlign() const { return awaitTwistAlign_; }

    // Match properties (AutoCAD MATCHPROP): click a source object, then click similar
    // objects to copy the source's style + technical properties (color, layer, weight,
    // and geometry/font/meta) onto them. Armed until Esc; the first click captures the
    // source, every later click applies to a compatible target under the cursor.
    void beginMatchProps();
    void matchPropsClick(const Vec2& p);
    bool awaitingMatchProps() const { return awaitMatchProps_; }

    // Twist / Screen: click the button, then draw a crossing line across the wires to
    // select them. Twist adds a twisted-pair/bundle symbol of the given type ("cross"
    // braid or "jog" transposition); Screen adds a shield enclosing the selected wires.
    void beginTwist(const QString& type);
    void beginScreen();
    bool awaitingWireBand() const { return !twistArmType_.isEmpty() || screenArm_; }
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
    // Set one pin's label on a connector (from the inline pin-label editor); a
    // non-empty font applies to the connector's pin font (the whole label set).
    void applyConnectorPinLabel(const QString& id, int pin, const QString& text,
                                const QString& font = QString());
    // Id of the single selected connector, or empty.
    QString selectedConnectorId() const;
    // Flip the connector placement preview: xAxis=true mirrors across the Y axis
    // (mirrorX), false mirrors across the X axis (mirrorY). Text stays upright.
    void toggleConnectorMirror(bool xAxis);
    // Arm connector placement: activate the Connector tool AND show the live preview.
    // The preview appears only after this (pressing "Place connector"), not merely when
    // the Connector tool is selected for editing settings.
    void armConnectorPlacement();
    // True while the connector placement preview is showing (armed by the button above).
    bool connectorArmed() const { return connectorArmed_; }
    // Freeze the placement preview at the current cursor (Tab, so settings can be edited
    // without the ghost following the mouse). The next canvas click re-attaches it.
    void freezeConnectorPreview();
    bool connectorFrozen() const { return connectorFrozen_; }

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
    void editTableCellRequested(const QString& id, int cell, const QString& text, Vec2 at,
                                double cellW, double cellH);
    // A table cell was selected/deselected — the host shows/hides the Table Cell tab.
    void cellSelectionChanged();
    // Connector placement was armed/disarmed (e.g. first canvas click starts the
    // preview) — the host reflects it on the "Place connector" button.
    void connectorArmedChanged();
    void markerSizeChanged(); // splice/X default size changed (ribbon field re-syncs)
    void screenArmedChanged(); // screen placement armed/disarmed/frozen (ribbon group re-syncs)
    // Double-clicked a connector pin label — the host opens the inline editor for it
    // (font = the connector's current pin font, so the picker starts on it).
    void editConnectorPinRequested(const QString& id, int pin, const QString& text, Vec2 at,
                                   double size, const QString& font);
    // The mirror line was picked — the host pops a Copy / Move / Cancel menu, then
    // calls commitMirror(copy) or cancelMirror().
    void mirrorChoiceRequested();
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
    // Cap one end (key = "cutStart" / "cutEnd") of a double entity to whatever cutter
    // (line/circle) passes through world point `at`, so its physical runs clip flush.
    void capDoubleEndAt(Entity& ent, const QString& key, const Vec2& at,
                        const std::vector<Entity>& joints);
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
    // Scale a whole selection about `base` by `sf`, keeping a harness intact: connectors
    // scale their structure parametrically, wires/other geometry scale their points, and
    // EVERY wire endpoint that sat on a scaled connector's pin end (selected or not) is
    // re-snapped to the connector's new pin end so the harness stays wired.
    void scaleSelection(const std::vector<Entity*>& sel, const Vec2& base, double sf);
    void handlePickModifyPress(const Vec2& p);
    // Apply the active corner modifier (Fillet radius or Chamfer distance) to vertex
    // `knee` of `e`; `corner` is the three-point corner and `gap` the double-line gap
    // (0 if single). Writes cornerRadii/cornerChamfers and clears the other on that knee.
    void applyCornerModAt(Entity& e, int knee, const std::vector<Vec2>& corner, double gap);
    // Balloons Ctrl+coupled into a batch share meta["chain"]; returns the OTHER
    // balloons in e's batch (empty if e has no chain). The batch moves as one unit.
    std::vector<Entity*> chainMatesOf(const Entity& e);
    void handlePlacementPress(const Vec2& p);
    // Second click of the twist/screen crossing line: gather the wires it crosses and
    // create the Twist or Screen annotation.
    void finishWireBand(const Vec2& fenceEnd);
    // Compute the shield ellipse radii for a set of caught wires (span = distance from the
    // first to the last wire, nWires = how many). Shared by finishWireBand and the live
    // preview so what you drag is exactly what gets placed. When all three lock flags are
    // set (the fully-automatic default) the large radius sits 15% beyond the wire span and
    // the small radius follows the locked ratio.
    void shieldAutoSize(double span, int nWires, double& rxOut, double& ryOut) const;
    void finishRegionSelect(bool subtract); // shift = remove from selection
    void drawGrips(QPainter& p, const Entity& e, const ViewTransform& vt) const;
    void onDragTick(); // promotes a held box-drag into a lasso (Edraw behavior)

    // Direct manipulation (Edraw-style): grab a joint (click-to-place) / apply edit.
    bool beginGripGrab(const Vec2& at, bool shift = false);
    void applyEditDrag(const Vec2& target);
    // Wires whose vertex sits exactly on one of `conn`'s pin ends are CONNECTED:
    // when the connector moves by `delta`, those wire vertices follow so the wires
    // stay attached (hold Ctrl while moving to detach). Called BEFORE the connector
    // itself is translated (the pin ends are taken from its current geometry).
    // `movingIds` are the entities already moving in the same operation (skipped,
    // so a selected wire isn't shifted twice).
    void dragAttachedWires(const Entity& conn, const Vec2& delta,
                           const std::vector<QString>& movingIds);
    // Reposition a Twist entity's per-wire crossing points to world-x `x`, clamped to
    // each referenced wire's own extent and interpolated along it. Shared by the twist's
    // constrained move grip and Twist Align.
    void repositionTwistAtX(Entity& twist, double x);
    // For the connector placement preview: if any feature of the ghost (a body corner
    // or a pin end) at `rawAnchor` lands within snap tolerance of an existing schematic
    // point (another connector's corner/pin end, a wire end, splice, or X marker),
    // return the anchor shifted so those points coincide (else none). The nearest
    // feature wins. If `catchOut` is given, it receives the connection point latched on.
    std::optional<Vec2> connectorFeatureSnap(const Vec2& rawAnchor, Vec2* catchOut = nullptr) const;
    // While the preview is FROZEN: the frozen connector's pin/corner nearest world point
    // `p` (within a grab pick radius), or none. Used to show the snap square on hover and
    // to catch that feature on click.
    std::optional<Vec2> frozenGrabFeature(const Vec2& p) const;
    bool awaitAlignRef_ = false; // next click picks the align-routing reference connector

    QSet<QString> bypassed_;        // validation discrepancy keys the user accepted
    QSet<QString> disabledRules_;   // validation rule ids the user turned off
    bool flashOn_ = false;          // a locate-highlight ring is currently shown
    Vec2 flashPt_{0.0, 0.0};        // where to draw it (world)
    QTimer* flashTimer_ = nullptr;  // clears the flash after a moment

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
    // Shift held on the first / most-recent draft click of a double line/polyline:
    // caps that run-end flush to a line it lands on (finishDraw applies the caps).
    bool drawStartShift_ = false;
    bool drawEndShift_ = false;
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

    // Mirror-decision state: the picked mirror line, pending a Copy/Move/Cancel choice.
    bool mirrorPending_ = false;
    Vec2 mirrorA_, mirrorB_;

    // Connector placement is "armed" (preview shown) only after Place connector.
    bool connectorArmed_ = false;
    // While frozen (Tab pressed to edit settings) the preview stays at connectorFrozenPos_
    // instead of following the cursor; the next canvas click re-attaches it to the cursor.
    bool connectorFrozen_ = false;
    Vec2 connectorFrozenPos_;
    // Grab handle: the offset from the ghost anchor to the point that tracks the cursor.
    // {0,0} = the top-left corner (default). While frozen, clicking one of the connector's
    // own pins/corners sets this so the ghost is then held by that feature.
    Vec2 connectorGrab_{0.0, 0.0};

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
    QString wireAwg_;
    QString wireInsulation_;
    QString wireVoltage_;
    QString wireType_ = QStringLiteral("Stranded");
    double wireRes_ = 0.0;
    QString wireResUnit_ = QStringLiteral("m");
    QString wireGapRule_ = QStringLiteral("pitch"); // "pitch" | "custom"
    double wireGapCustom_ = 15.0;
    bool wireOrtho_ = false;      // wire drawing constrained to 90° segments
    double spliceSize_ = 4.0;     // splice dot radius (world units)
    double ncSize_ = 6.0;         // X termination half-size (world units)
    double screenRx_ = 12.0;      // screen ellipse small radius
    double screenLeg_ = 15.0;     // screen drain-leg length (default 15mm)
    bool screenShowS_ = true;     // draw the "S" shield label
    bool screenRyLocked_ = true;  // large radius auto-fits the caught wires unless unlocked
    double screenRy_ = 20.0;      // large radius when unlocked
    double screenRatio_ = 20.0 / 12.0; // large:small ratio (kept in sync with the radii above)
    bool screenRatioLock_ = true;      // hold the ratio fixed while editing a radius
    bool screenRxLock_ = true;         // small-radius field locked (dependent, not editable)
    int screenAmount_ = 1;             // shields placed per drag (1-5), laid side by side
    QString screenShape_ = QStringLiteral("ellipse"); // "ellipse" | "capsule"
    QString screenLegDir_ = QStringLiteral("bottom"); // drain leg exit side
    bool screenFrozen_ = false;   // Tab pressed: preview end pinned, edit fields freely
    Vec2 screenFrozenEnd_;
    bool awaitCrossSwitch_ = false; // Cross-switch tool armed (click crossings to toggle hops)
    bool awaitTwistAlign_ = false;  // Twist Align armed (next click = destination point)
    bool awaitMatchProps_ = false;  // Match-properties armed (first click = source, then targets)
    QString matchSourceId_;         // source object id once picked (empty until the first click)
    QString twistArmType_;          // non-empty while the Twist tool is armed (its type)
    bool screenArm_ = false;        // Screen tool armed (draw a crossing line to enclose wires)
    bool wireBandHasStart_ = false; // first click of the twist/screen crossing line taken
    Vec2 wireBandStart_;
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
    QString cellTableId_;         // table whose cell is selected (empty = none)
    int cellIndex_ = -1;          // selected cell (row-major), -1 = none
    bool rectAwaitY_ = false;     // rectangle: awaiting the Y length after the X length
    double rectX_ = 0;            // rectangle: signed X length already entered

    // Clipboard with a base point (AutoCAD COPYBASE / cut / paste-to-point).
    Vec2 clipboardBase_{0, 0};    // reference point the clipboard is stored relative to
    bool awaitClipBase_ = false;  // next click sets the base point (after Ctrl+Shift+C / Ctrl+X)
    bool clipCut_ = false;        // the pending base-point op is a cut (delete originals)
    bool pastePlacing_ = false;   // paste in progress: next click drops the contents
    bool pasteMirrorX_ = false;   // X/Y flip the paste preview about the base before placing
    bool pasteMirrorY_ = false;
    // Cut originals stay visible until the paste lands; these ids are removed when
    // the contents are finally placed (so a cut object isn't gone mid-move).
    std::vector<QString> cutPending_;
};

} // namespace Edraw_Desktop
