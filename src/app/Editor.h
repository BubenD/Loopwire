#pragma once

#include "commands/UndoStack.h"
#include "geometry/Snap.h"
#include "geometry/Vec2.h"
#include "harness/Harness.h"
#include "harness/HarnessBuilder.h" // HarnessDef used by insertHarness
#include "model/Document.h"
#include "render/EntityRenderer.h"
#include "tools/ModifyOps.h" // ArraySpec / ArrayKind used by the array session
#include "tools/QuickSelect.h" // QuickSelectQuery used by the Quick Select command
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

    // ---- Wire status (shown in the wire's properties) -----------------------
    // A Screen (shield) or Twist annotation records the wires it covers in its "wires"
    // meta, so a wire's shielded / twisted status is read back from them.
    bool wireIsShielded(const QString& wireId) const;
    bool wireIsTwisted(const QString& wireId) const;
    // What one END of a wire is connected to:
    //   "P1/3"        on that connector pin
    //   "splice"      on a splice dot (joined to other wires)
    //   "open"        deliberately terminated by an X (not-connected) marker
    //   "unconnected" landing on nothing at all
    QString wireEndRef(const Entity& wire, bool firstEnd) const;

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
    // Resize a selected shield's radius ("rx" or "ry") AND scale its drain leg by the same
    // factor (so the leg grows/shrinks with the shield when edited in Properties, matching
    // the Scale tool). Only the leg on the axis that changed is scaled.
    void resizeSelectedShield(const QString& key, double value);
    // The drain-leg length of the selected shield: how far the leg reaches from the outline
    // edge to its end. Reading it measures the geometry (the leg IS the last point), and
    // setting it slides that end along the leg's own direction.
    double selectedShieldLegLength() const;
    void setSelectedShieldLegLength(double length);
    // Is the selected shield's drain leg landing on a wire? A leg that is genuinely wired
    // must not also carry a "not connected" X.
    bool shieldLegIsWired(const Entity& shield) const;
    // Slide a Screen (shield) along the wires it catches to world-x `x`, re-deriving each
    // crossing from its own wire and carrying the drain leg along — a shield is pinned to
    // its wires and never translates off them. False for a decorative copy with no wire
    // references (the caller then translates freely).
    bool repositionScreenAtX(Entity& screen, double x);
    // The wires a shield of half-height `ry` would enclose (crossings + wire ids, parallel
    // arrays), measured along its major axis through its centre.
    bool shieldCatchAt(const Entity& screen, double ry, std::vector<Vec2>& crossings,
                       std::vector<QString>& wireIds) const;
    void setSelectionMetaBool(const QString& key, bool value);       // showS, ...
    // ---- Wire label & marking ----------------------------------------------
    // Set the selected wire(s) label, carrying the new text onto any Wire label
    // (NetLabel) entity attached to them so the drawing and the property agree.
    void setSelectionWireLabel(const QString& text);
    // Set how many colour/gauge markings run along the selected wire(s), keeping the
    // positions of the instances that already exist.
    void setSelectionWireAnnotCount(int count);
    // Re-point an attached Wire label's text, writing it back to its wire as well.
    void setWireLabelText(const QString& labelId, const QString& text);
    // Switch the selected wires' resistance unit between Ω/m and Ω/ft, converting EACH
    // wire's own value from its own current unit (1 ft = 0.3048 m).
    void convertSelectionResistanceUnit(bool toFeet);
    // The wire under `p` within pick tolerance (nearest wins), or empty.
    QString wireAt(const Vec2& p) const;
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

    // ---- Array (AutoCAD ARRAY: rectangular / polar / path) -----------------
    // beginArray arms an array session on the current selection: the items are
    // previewed live as ghosts and every parameter stays editable in the contextual
    // "Array Creation" ribbon until Enter / Close Array commits (Esc cancels), so
    // nothing is written to the drawing until the layout looks right.
    //   Rectangular - previews immediately from the seeded spacings.
    //   Polar       - the centre follows the cursor until a click sets it.
    //   Path        - the next click picks the object the items track along.
    void beginArray(ArrayKind kind);
    bool arrayActive() const { return arrayActive_; }
    // Switch kind mid-session (the ribbon's Type buttons), keeping the selection.
    void setArrayKind(ArrayKind kind);
    ArraySpec& arraySpec() { return arraySpec_; }
    const ArraySpec& arraySpec() const { return arraySpec_; }
    // Called after the ribbon edits arraySpec() so the preview repaints.
    void arrayParamsChanged();
    void commitArray();
    void cancelArray();
    // Drop the polar centre / the picked path so the next canvas click supplies it
    // again (the ribbon's "Pick centre" / "Pick path" button).
    void arrayRepick();
    // A polar array with no centre yet / a path array with no path yet: the next
    // canvas click supplies it, and there is nothing to preview meanwhile.
    bool arrayAwaitingCenter() const;
    bool arrayAwaitingPath() const;
    bool arrayReady() const { return arrayActive_ && !arrayAwaitingPath(); }
    // How many items the current spec actually produces (a path array drops the
    // items that would fall off the end of an open path).
    int arrayItemCount() const;
    // Length of the picked path, and the distance actually stepped between items
    // along it — so the ribbon can show what "Divide" derives. Both 0 unless a path
    // array has its path.
    double arrayPathLength() const;
    double arrayItemStep() const;
    ArrayKind lastArrayKind() const { return lastArrayKind_; } // default for the next Array click

    // Advanced modify (selection-based actions invoked from the UI).
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
    // Pitch rule (align routing): a wire's knee is spaced one pitch from its neighbour
    // wire's knee — "pitch" uses the reference connector's pitch, "custom" uses
    // wireGapCustom().
    QString& wireGapRule() { return wireGapRule_; }
    double& wireGapCustom() { return wireGapCustom_; }
    // The same pitch rule applied while ROUTING: a wire being drawn keeps one PITCH of
    // clearance from the wire runs already on the drawing, so parallel runs can never
    // land on top of each other. Applies to a single wire too, not just a fan-out.
    bool& wireClearanceOn() { return wireClearanceOn_; }
    // The clearance actually used, decided by the Pitch rule: the pitch of the connector
    // the run was started from ("Connector pitch"), or wireGapCustom() ("Custom").
    double routingClearance() const;
    // Marking defaults stamped onto newly drawn wires (Wire ribbon "Marking" group):
    // whether the colour and the cross-section are written along the wire, whether the
    // colour is the 2-letter code or the full name, how many times they repeat, and the
    // text size. Each is independent of the others.
    bool& wireShowColorName() { return wireShowColorName_; }
    bool& wireShowGaugeName() { return wireShowGaugeName_; }
    QString& wireColorNameMode() { return wireColorNameMode_; } // "code" | "full"
    int& wireAnnotCountDefault() { return wireAnnotCount_; }
    double& wireAnnotSize() { return wireAnnotSize_; }
    // Apply the current marking defaults to the selected wire(s) too, so the ribbon acts
    // on what is selected as well as on what is drawn next (as the other ribbon
    // property controls do).
    void applyWireMarkingToSelection();
    // Push the next wire vertex `p` (coming from `prev`) out of any parallel run that is
    // closer than the clearance, moving only across the run so an ortho route stays ortho.
    Vec2 applyWireClearance(const Vec2& prev, const Vec2& p) const;
    // The route one fanned wire follows when several pins are wired at once: the drafted
    // route shifted sideways into its own lane (offset parallel, corners mitred) so the
    // pin pitch is carried THROUGH every turn instead of every wire turning at the same
    // point and overlapping after it. Shared by the live preview and the commit, so what
    // is previewed is what gets placed.
    static std::vector<Vec2> fanRoute(const std::vector<Vec2>& route, const Vec2& pinOffset);
    // Constrain wire drawing to horizontal/vertical (90°) segments only.
    bool& wireOrtho() { return wireOrtho_; }
    // Default marker sizes for newly placed splices / X (not-connected) terminations.
    double& spliceSize() { return spliceSize_; }
    double& ncSize() { return ncSize_; }
    // Screen (shield) defaults: small radius (rx), drain-leg length, and show the "S".
    double& screenRx() { return screenRx_; }
    double& screenLeg() { return screenLeg_; }
    bool& screenShowS() { return screenShowS_; }
    double& screenSSize() { return screenSSize_; } // "S" label text size
    // "Not Connected": marks the drain leg as deliberately unterminated, drawing an "X"
    // at its end regardless of whether a wire happens to land there. Sticky default for
    // the next shield placed (off by default), like screenShowS_.
    bool& screenNotConnected() { return screenNotConnected_; }
    // Half-size of that "X" (world units), for the NEXT shield placed. Per-shield the
    // value lives in meta["ncSize"] and is editable from Properties.
    double& screenNcSize() { return screenNcSize_; }
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
    // Shield outline style: colour (empty = ByLayer, else explicit hex), line type
    // ("solid"|"dashed") and the dash pattern scale.
    QString& screenColor() { return screenColor_; }
    QString& screenLineType() { return screenLineType_; }
    double& screenDashScale() { return screenDashScale_; }
    // Which side of the ellipse the drain leg exits from.
    QString& screenLegDir() { return screenLegDir_; } // "top" | "bottom" | "left" | "right"
    bool screenArmed() const { return screenArm_; }
    // Wire fan-out: true while drafting a wire that started on a connector pin (so a number
    // key can toggle individual pins). Shift+<pin number> toggles that one pin's wire in/out
    // of the fan (the anchor pin — the one being drawn — is always kept); Shift at the first
    // click adds all pins.
    bool wireFanArmed() const {
        return drafting_ && tool_ == ToolId::Wire && wireFanAnchor_ >= 0 && !wireFanPins_.empty();
    }
    void toggleWireFanPin(int pinNumber); // pinNumber is 1-based (the pin's displayed number)
    void setWireFanAll();                 // add every pin of the connector to the fan
    // Freeze the in-progress screen preview (Tab) so its ribbon fields can be edited
    // without the crossing-line drag changing the caught wires; a click then places it
    // at the frozen end point regardless of where the click lands.
    void freezeScreenPreview();
    bool screenFrozen() const { return screenFrozen_; }
    // Same for a twist: Tab mid-drag pins the crossing-line end so the ribbon twist
    // parameters can be tuned. Unlike the shield, only ENTER places a frozen twist — a
    // canvas click releases the freeze and goes back to placing the crossing line, so a
    // stray click while editing the fields can never commit a twist by accident.
    void freezeTwistPreview();
    bool twistFrozen() const { return twistFrozen_; }

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

    // Pull a whole selected wire bundle. During a wire run/knee drag, holding Shift moves
    // every OTHER selected wire's matching run/knee by the held wire's TOTAL travel since
    // the drag began — so a fan-out keeps its pitch and shape — and releasing Shift puts
    // them all straight back where they started, leaving only the held wire moving. Both
    // directions come out of the one absolute rule, so the release lands exactly.
    // `shiftHeld` is passed in (read live at the call site) because Shift cannot START a
    // grab — that is selection — so it only ever acts as a mid-drag modifier.
    void pullSelectedWireBundle(bool shiftHeld);

    // Match properties (AutoCAD MATCHPROP): the source object's style + technical
    // properties (color, layer, weight, and geometry/font/meta) are painted onto every
    // object clicked afterwards. Armed until Esc. Either order works: with ONE object
    // already selected that becomes the source straight away, otherwise the first click
    // captures it.
    void beginMatchProps();
    void matchPropsClick(const Vec2& p);
    bool awaitingMatchProps() const { return awaitMatchProps_; }
    // The source object once captured (empty while still waiting for it).
    const QString& matchSource() const { return matchSourceId_; }

    // ---- Quick Select (AutoCAD QSELECT) ------------------------------------
    // Build a selection from a property filter. Either order works, matching the way
    // the other pick commands here behave: with objects already selected the host opens
    // the dialog at once, otherwise Quick Select stays armed so objects can be picked
    // on the canvas and Enter opens it. The sample object seeds which properties are
    // offered and their starting values.
    void beginQuickSelect();
    bool awaitingQuickSelect() const { return awaitQuickSelect_; }
    void cancelQuickSelect();
    // Ask the host for the dialog now (Enter while armed, or a ready selection).
    void openQuickSelect();
    // Apply a filter and select what it matches. Returns how many objects ended up
    // selected. `q.wholeDrawing` false filters the CURRENT selection instead.
    int applyQuickSelect(const QuickSelectQuery& q);
    // Every distinct value of `key` present in the drawing (for the value dropdown).
    std::vector<QString> quickSelectValueChoices(const QString& key) const;
    // The entity types actually present in the drawing, as type names.
    std::vector<QString> presentEntityTypes() const;
    // A copy of the entity with this id (for building the dialog), or nullopt.
    std::optional<Entity> entityCopy(const QString& id) const;

    // Twist / Screen: click the button, then draw a crossing line across the wires to
    // select them. Twist adds a twisted-pair/bundle symbol of the given type ("cross"
    // braid or "jog" transposition); Screen adds a shield enclosing the selected wires.
    void beginTwist(const QString& type);
    void beginScreen();
    bool awaitingWireBand() const { return !twistArmType_.isEmpty() || screenArm_; }
    QString twistArmType() const { return twistArmType_; } // "" when no twist armed
    QString lastTwistType() const { return lastTwistType_; } // last chosen type (default next time)
    // Remove-twist mode: click a twist sign to delete it (stays armed until Esc).
    void beginTwistRemove();
    bool awaitingTwistRemove() const { return awaitTwistRemove_; }
    // Twist parameters: Type 1 (crossed) uses diagonal angle + horizontal cap length;
    // Type 2 (jog) uses flat length + repeats; Type 3 uses repeats; all use amount.
    double& twistDiagAngle() { return twistDiagAngle_; }
    double& twistCapLen() { return twistCapLen_; }
    int& twistRepeats() { return twistRepeats_; }
    double& twistFlatLen() { return twistFlatLen_; }
    int& twistAmount() { return twistAmount_; }
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
    // Change ONE aspect of every selected connector's own spec. Used by the Properties
    // panel so editing a field with several connectors selected changes just that field on
    // each of them, instead of stamping the first connector's whole spec (and so its
    // designator / part number / pin labels) onto all the others.
    void editSelectedConnectorSpecs(const std::function<void(ConnectorSpec&)>& mut);
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

    // Draw a whole harness definition into the document in one undoable step (the Harness
    // builder's Generate): the connectors, the routed wires, the shields, the twist symbols
    // and the unconnected-pin markers. Wire ids are stamped here and the shield/twist
    // "wires" references are re-pointed at them, so the placed annotations really do own
    // the wires they cover. Returns how many objects were added; the result is left
    // selected. `issuesOut`, if given, receives whatever the definition failed to validate.
    int insertHarness(const HarnessDef& def, std::vector<HarnessIssue>* issuesOut = nullptr);

    // ---- Harness placement ---------------------------------------------------
    // Build the harness and hand it to the cursor instead of dropping it at its designed
    // coordinates: the whole thing ghosts under the crosshair, a click places it, and Esc
    // abandons it. The builder uses this so Generate becomes "now put it where you want it".
    void beginHarnessPlacement(const HarnessDef& def);
    bool harnessPlacing() const { return harnessPlacing_; }
    void cancelHarnessPlacement();
    // Drop the ghosted harness so the corner it is held by lands at `at`.
    void placeHarnessAt(const Vec2& at);

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

    // The faint "where it came from" snapshot drawn during a move / joint drag: the
    // dragged objects plus the wires attached to any dragged connector.
    const std::vector<Entity>& dragGhost() const { return editGhost_; }

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
    void twistArmChanged();    // twist type armed/cleared (ribbon shows crossed params when crossed)
    // An array session started/ended, changed kind, or had its centre/path picked —
    // the host opens, syncs or closes the contextual "Array Creation" tab.
    void arrayChanged();
    // Match-properties was armed or ended — the ribbon's Match button latches with it.
    void matchPropsChanged();
    // Quick Select wants its dialog, seeded from `sampleId` (empty = no sample: the
    // filter starts on the general properties every object has).
    void quickSelectRequested(const QString& sampleId);
    // A harness placement finished: `placed` is true if it landed, false if Esc abandoned
    // it. The Harness Builder hides itself while placing and comes back on this.
    void harnessPlacementFinished(bool placed);
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
    // Is `wireId` named in the "wires" list of any entity of that type (Screen/Twist)?
    bool wireCoveredBy(const QString& wireId, EntityType type) const;
    // Give a freshly generated harness its entity ids and re-point the shield/twist wire
    // references at them (the generator names wires by their DEFINITION id).
    void stampHarnessIds(std::vector<Entity>& ents, const HarnessDef& def);
    // Array session helpers: the live placements for the current spec (empty while a
    // pick is outstanding), the polar centre in force (the cursor until one is picked),
    // and the click handler that takes the centre / path pick.
    std::vector<ArrayPlacement> arrayPreview() const;
    Vec2 arrayCenterInUse() const;
    void handleArrayPress(const Vec2& p);
    // Seed spacings / counts from the selection's size when a kind is armed, so the
    // first preview is already sensible (AutoCAD-style clear-of-each-other defaults).
    void seedArrayDefaults();
    // Apply the active corner modifier (Fillet radius or Chamfer distance) to vertex
    // `knee` of `e`; `corner` is the three-point corner and `gap` the double-line gap
    // (0 if single). Writes cornerRadii/cornerChamfers and clears the other on that knee.
    void applyCornerModAt(Entity& e, int knee, const std::vector<Vec2>& corner, double gap);
    // Distance between consecutive twist marks placed in one drag (Amount > 1): one mark's
    // own width for the armed type + a clear gap, so marks never touch. `band` = crossed-wire
    // height. Used by both placement (finishWireBand) and the drag preview (drawOverlay).
    double twistMarkSpacing(double band) const;
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
    void shieldAutoSize(double span, double pinGap, double& rxOut, double& ryOut) const;
    // If p sits on a connector pin end, fill pinEndsOut with that connector's ordered pin
    // ends and anchorOut with the index of the matched pin; returns true.
    // `pitchOut`, when given, receives that connector's pin pitch — the routing clearance
    // for a run pulled off it.
    bool connectorPinsAt(const Vec2& p, std::vector<Vec2>& pinEndsOut, int& anchorOut,
                         double* pitchOut = nullptr) const;
    void rebuildWireFanOffsets(); // wireFanSel_ -> wireArrayOffsets_ (relative to the anchor pin)
    // A wire drawn from (or onto) the FREE end of an existing wire CONTINUES that wire: the
    // drafted run is appended to it so the result is one whole wire, keeping the original's
    // number, colour and gauge — it is the same conductor carried further, not a second one.
    // An end sitting on a connector pin or a splice is a real termination and is never
    // joined onto. Returns true if the run was absorbed (so nothing new is created).
    bool extendExistingWire(const std::vector<Vec2>& pts);
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
    // Settle a large-radius grip drag: adopt the newly enclosed wire set, or restore the
    // pre-drag shield from editGhost_ when the drag reached no different set of wires.
    void commitShieldResize(const QString& id);
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

    // Array session state (ARRAYRECT / ARRAYPOLAR / ARRAYPATH). arraySrc_ is the
    // snapshot of the selected objects being arrayed; arrayBase_ their bounding-box
    // centre (the base point every placement is measured from). While the session is
    // live nothing is written to the document — the items are ghost-previewed only.
    bool arrayActive_ = false;
    ArraySpec arraySpec_;
    ArrayKind lastArrayKind_ = ArrayKind::Rect; // kind the Array button repeats
    std::vector<Entity> arraySrc_;
    std::vector<QString> arraySrcIds_;
    Vec2 arrayBase_{0.0, 0.0};
    bool arrayHasCenter_ = false;   // polar: a centre has been clicked (else the cursor)
    QString arrayPathId_;           // path: the picked object (empty = awaiting the pick)
    std::vector<Vec2> arrayPathPts_;// its drawn centreline
    bool arrayPathClosed_ = false;

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
    bool wireClearanceOn_ = true;  // hold a clearance from neighbouring wires while routing
    // Pitch of the connector the run being drawn started from (0 = it didn't start on
    // one). The Pitch rule's "Connector pitch" clearance comes from this, so a run pulled
    // off a connector keeps exactly one pin pitch from its neighbours.
    double wireRoutePitch_ = 0.0;
    bool wireShowColorName_ = false; // write the colour along new wires
    bool wireShowGaugeName_ = false; // write the cross-section along new wires
    QString wireColorNameMode_ = QStringLiteral("code"); // "code" (WH) | "full" (White)
    int wireAnnotCount_ = 1;         // how many times the marking repeats along the wire
    double wireAnnotSize_ = 3.5;     // marking text height (world units)
    bool wireOrtho_ = false;      // wire drawing constrained to 90° segments
    // Wiring from a connector pin: the same route can fan out to several pins at once.
    // Shift at the first click fans to the last pin; pressing a number key sets the count.
    // wireArrayOffsets_ holds each fanned wire's offset from the anchor pin (anchor=={0,0});
    // wireFanPins_ / wireFanAnchor_ remember the connector so the count can be re-picked live.
    bool wireArray_ = false;
    std::vector<Vec2> wireArrayOffsets_;
    std::vector<Vec2> wireFanPins_;   // all pin ends of the connector the wire started on
    int wireFanAnchor_ = -1;          // index (in wireFanPins_) of the pin the wire started on
    std::vector<int> wireFanSel_;     // selected pin indices in the fan (always incl. the anchor)
    double spliceSize_ = 4.0;     // splice dot radius (world units)
    double ncSize_ = 6.0;         // X termination half-size (world units)
    double screenRx_ = 10.0;      // screen ellipse small radius (default 10)
    double screenLeg_ = 15.0;     // screen drain-leg length (default 15mm)
    bool screenShowS_ = false;    // draw the "S" shield label (off by default)
    bool screenNotConnected_ = false; // "X" the drain leg's end (off by default)
    double screenNcSize_ = 3.0;       // that "X"'s half-size (world units)
    double screenSSize_ = 4.0;    // "S" label text size (world units)
    bool screenRyLocked_ = true;  // large radius auto-fits the caught wires unless unlocked
    double screenRy_ = 30.0;      // large radius when unlocked (default 30)
    double screenRatio_ = 30.0 / 10.0; // large:small ratio (kept in sync with the radii above)
    bool screenRatioLock_ = true;      // hold the ratio fixed while editing a radius
    bool screenRxLock_ = true;         // small-radius field locked (dependent, not editable)
    int screenAmount_ = 1;             // shields placed per drag (1-5), laid side by side
    QString screenShape_ = QStringLiteral("ellipse"); // "ellipse" | "capsule"
    QString screenColor_;                             // outline colour ("" = ByLayer)
    QString screenLineType_ = QStringLiteral("solid"); // "solid" | "dashed"
    double screenDashScale_ = 1.0;                    // dash pattern scale
    QString screenLegDir_ = QStringLiteral("bottom"); // drain leg exit side
    bool screenFrozen_ = false;   // Tab pressed: preview end pinned, edit fields freely
    Vec2 screenFrozenEnd_;
    bool awaitCrossSwitch_ = false; // Cross-switch tool armed (click crossings to toggle hops)
    bool awaitTwistAlign_ = false;  // Twist Align armed (next click = destination point)
    bool awaitTwistRemove_ = false; // Remove-twist armed (click a twist sign to delete it)
    bool awaitMatchProps_ = false;  // Match-properties armed (first click = source, then targets)
    QString matchSourceId_;         // source object id once picked (empty until the first click)
    int matchCount_ = 0;            // objects painted so far in this match session
    bool awaitQuickSelect_ = false; // Quick Select armed: pick objects, Enter opens the dialog
    QString twistArmType_;          // non-empty while the Twist tool is armed (its type)
    QString lastTwistType_ = QStringLiteral("crossed"); // last chosen type (the default next time)
    double twistDiagAngle_ = 15.0;  // Type 1 (crossed): diagonal angle (deg from perpendicular)
    double twistCapLen_ = 8.0;      // Type 1 (crossed): horizontal cap length (world units)
    int twistRepeats_ = 2;          // Type 2/3: number of crossings ("how many twists")
    double twistFlatLen_ = 6.0;     // Type 2: horizontal flat length between crossings
    int twistAmount_ = 1;           // twists placed per crossing-line drag (side by side)
    bool screenArm_ = false;        // Screen tool armed (draw a crossing line to enclose wires)
    bool twistFrozen_ = false;      // Tab while placing a twist: preview end pinned, edit fields
    Vec2 twistFrozenEnd_;
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

    // Harness placement: the generated harness ghosts under the cursor until a click drops
    // it (Esc abandons it and hands control back to the builder). harnessBase_ is the point
    // the ghost is held by, so the whole harness moves as one rigid piece.
    bool harnessPlacing_ = false;
    std::vector<Entity> harnessGhost_;
    Vec2 harnessBase_{0.0, 0.0};
};

} // namespace Edraw_Desktop
