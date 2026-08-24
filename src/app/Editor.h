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

#include <functional>
#include <QSet>
#include <QJsonArray>
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
    // The break mark: which of the two styles is placed, and how far it reaches across the run.
    QString& breakStyle() { return breakStyle_; }
    double& breakSize() { return breakSize_; }
    // What a break is placed with. The gap is the distance between its two strokes; the angle is how
    // far they lean out of square; the size is how far they reach across the run, which is worked out
    // from the run itself unless it is said here. 0 = "as the run needs".
    double& breakGap() { return breakGap_; }
    double& breakAngle() { return breakAngle_; }
    // The mark that WOULD be placed by a click, for the overlay to draw. Empty type when the tool is
    // not armed. A break is sized and angled by the run under the cursor, so seeing it before
    // committing is the difference between placing it and placing it twice.
    // The mark that the marking line currently describes, for the overlay to draw. Empty type until
    // the line has a start. `bandEnd` is where the line's second end is - the cursor, or the pinned
    // end once Tab has pinned it.
    Entity breakPreview() const;
    bool breakFrozen() const { return breakFrozen_; }
    // Is a break's marking line being drawn (its first end taken)?
    bool breakBandStarted() const { return tool_ == ToolId::LengthBreak && wireBandHasStart_; }
    Vec2 breakBandStart() const { return wireBandStart_; }
    Vec2 breakBandEnd() const { return breakFrozen_ ? breakFrozenAt_ : cursor_; }
    // Where the cursor actually IS once the snaps have had it - the point a click would use, rather than
    // the raw position of the mouse. What the status readout shows, and what a test of a snap has to ask.
    Vec2 snappedCursor() const { return cursor_; }
    // Tab: pin the preview where it is, so the numbers on the ribbon can be tried on the real thing
    // without the cursor moving it. The next click on the drawing places it and resumes.
    void freezeBreakPreview();

    // How many wires a twisted bundle is drawn with (2..12), set on the ribbon while the tool is armed.
    int& twistWires() { return twistWires_; }
    // Tab while one is being drawn PINS it where it is, so the count and the rest can be tried on the
    // real thing without the cursor moving it. The next click, or Enter, places it.
    void freezeTwistedPreview();
    bool twistedFrozen() const { return twistedFrozen_; }
    // Is a twisted pair / route / harness / harness route being drawn?
    static bool isTwistedTool(ToolId t) {
        return t == ToolId::TwistedPair || t == ToolId::TwistedRoute ||
               t == ToolId::TwistedHarness || t == ToolId::TwistedHarnessRoute;
    }

    // Ask the user for a rectangle: two clicks, with everything a click normally gets - every running
    // object snap, the snap-override menu, ortho and polar, a typed coordinate. That is the whole reason
    // this goes through the editor rather than through a rubber band of its own: a plot window is picked
    // off the drawing, and picking off a drawing means snapping to it.
    //
    // `onDone` is handed the two corners; `onCancel` runs instead if Esc is pressed. Neither is called
    // twice, and the pick is over as soon as one of them fires.
    void beginRectanglePick(std::function<void(Vec2, Vec2)> onDone,
                            std::function<void()> onCancel = {});
    bool pickingRectangle() const { return rectPick_; }

    // Rectangles that are not entities but are still there to be snapped to: on a layout, the sheet's own
    // edge and the frame of every window on it. Lining a plot window up with the paper, or with a view
    // already on the paper, is the commonest thing anyone does with one - and neither the sheet nor a
    // window is an entity, so nothing in the snap search would ever find them.
    //
    // Corners snap as endpoints, the middle of each edge as a midpoint, and anywhere along an edge as
    // nearest - each only if that snap is switched on, like any other.
    void setSnapRectangles(std::vector<Bounds> rects);
    const std::vector<Bounds>& snapRectangles() const { return snapRects_; }

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
        // TEN, the house figure. It was 5 - the figure a small drawing wants and half of what an A3 sheet
        // wants - and a size somebody has to change on every piece of text they write is not a default, it
        // is a nuisance.
        double size = 10.0;
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
    // Which side of the drawn line the SECOND line of a double run goes. Ctrl swaps it while drawing.
    bool doubleSecondBelow() const { return !dpolySwap_; }
    // The point ortho and polar tracking are measured FROM, and where the green lock line is drawn from.
    //
    // Asked in two places - by the snap, to work the angle out, and by the overlay, to draw the ray along
    // it - and it was written out twice, in neither case as this function. So when the two-click picks
    // were given an anchor, the snap began tracking and the ray still was not drawn: only one of the two
    // copies had been told about them. One answer, in one place, asked by both.
    std::optional<Vec2> trackingAnchor() const;

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
               tool_ == ToolId::Splice || tool_ == ToolId::NotConnected || movingByDistance();
    }

    // A MOVE UNDER WAY, waiting for a distance.
    //
    // This is how a drawing is actually moved: point the direction with the cursor and say how far. Every drawing
    // package works this way and it is the one thing a mouse cannot do accurately - the direction is easy to aim
    // and the distance is impossible to hit, so the direction is pointed and the number is typed.
    //
    // True while something is being dragged and there is a point to measure FROM: a grabbed grip or body, or the
    // Move / Copy tool with its base point down.
    bool movingByDistance() const;
    // Where the move is measured from, and which way it is going: the base point and the cursor. Empty when
    // nothing is being moved.
    std::optional<std::pair<Vec2, Vec2>> moveVector() const;
    // Move whatever is being dragged exactly `mm` along the direction now pointed, and finish - which is what
    // typing a number during a move means.
    bool applyMoveDistance(double mm);

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
    // Scale what is selected about a point. What the Scale tool does when its two clicks are in, asked for
    // directly - a test of "the writing scales with it" has to be able to scale something.
    void scaleSelectionForTest(const Vec2& base, double factor);
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
    // Every cell currently picked, row-major. A zone dragged over a table picks the cells it
    // covers rather than the table, so a block of them can be aligned in one go.
    const std::vector<int>& selectedCells() const { return cellSel_; }
    const QString& selectedCellTableId() const { return cellTableId_; }
    // Pick the cells of `tableId` that `region` covers. True when it found any.
    bool selectCellsInRegion(const QString& tableId, const std::vector<Vec2>& region);
    // Start a balloon carrying `text` (the next click gives it what it points at, the one after
    // that where the label sits). What a click on a bill of materials' "#" cell does.
    void beginBalloonWithText(const QString& text);
    void setCellSelection(const QString& tableId, int cell);
    void clearCellSelection();
    // Cell editing ops (act on the selected cell / its row / its column).
    // The two centre marks. A centre MARK is the cross at the middle of a round thing; a centre LINE runs
    // down the middle of the gap between two parallel runs.
    void placeCenterMark(const QString& id);
    void placeCenterLine(const QString& firstId, const QString& secondId);
    // Join the picked cells into one, and break a joined one back apart.
    void tableMergeCells();
    void tableSplitCell();
    void tableInsertRow(bool below);
    void tableDeleteRow();
    void tableInsertColumn(bool right);
    void tableDeleteColumn();
    void setCellAlignment(const QString& align); // "TL","TC",...,"BR"
    // The text height of one cell. A taller text needs a taller row and, when the words no longer
    // fit across it, a wider column - so the row and the column are grown to hold it. Every other
    // row / column keeps the size it had.
    void setTableCellTextHeight(const QString& tableId, int cell, double height);
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
    void setSelectionMeta(const QString& key, const QString& value);
    // Mark what is selected as the line the HARNESS ATTACHES TO on this part - the face of a
    // connector, the barrel of a terminal. It is drawn orange from then on, travels with the drawing
    // into the library, and is what the generator sets the part down by: the marked line goes across
    // the wires with its centre on the joint. Called again with the same objects, it takes the mark
    // off. Answers how many objects it marked, so a caller can say so.
    int markSelectionAsConnectionLine(bool on);
    // Whether everything selected carries the mark (for a toggle to show its state).
    bool selectionIsConnectionLine() const;
    // Which side of a screen its drain leaves from. Not setSelectionMeta: the leg is GEOMETRY - the
    // last point of the entity - so the side has to move it, or the drawing keeps the old leg.
    void setSelectionScreenLegDir(const QString& dir); // linetype, plot style, ...
    void setSelectionMetaNumber(const QString& key, double value);
    // A yes/no property of the selection. Written as a real boolean, so what reads it back gets a
    // boolean and not the string "true".
    void setSelectionMetaBool(const QString& key, bool value);   // gap, ...
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

    // ---- Re-ordering the pinout of connectors already on the drawing ----
    // What a re-order is asked to achieve. Crossings are always worth something; the rest say
    // whether the wires should be pulled into one tight group, and whether that group should
    // start at the top of the connector.
    struct PinoutGoal {
        bool group = false;   // the wires end up on neighbouring pins (least distance between them)
        bool fromTop = false; // ...and the group starts at pin row 1
    };
    // Re-order the pins of every selected connector so the wires between them read better,
    // moving the wires with their pin NAMES - the wiring is never changed, only where each pin
    // sits. Returns how many connectors were re-ordered (0 if the selection has nothing to work
    // on). Needs at least two selected connectors with wires running between them.
    int reorderSelectedPinout(const PinoutGoal& goal);
    // Whether that command has anything to act on, for enabling the menu entry.
    bool canReorderSelectedPinout() const;
    // Read the selected connectors and the wires between them into a definition the builder's
    // optimiser understands. False when there is nothing to work on.
    bool buildPinoutModel(std::vector<QString>& connIds, HarnessDef& def) const;
    // How many wire ends land on a point (optionally ignoring one wire's own).
    int wireEndsAt(const Vec2& at, const QString& except = QString()) const;
    // Drop a joint marker at a point (unless one is already there).
    void addSpliceAt(const Vec2& at);
    // The direction of the run under a point - a wire, a line, a double line, a polyline - as an
    // angle. Used to lay a mark square across whatever it is placed on.
    std::optional<double> runDirectionAt(const Vec2& q) const;
    // How far the runs at `q` reach across a line through it at `rot`: the width of a double line, of
    // a pair of rails, of a bundle of parallels. What a mark placed there has to span.
    double runSpanAcross(const Vec2& q, double rot) const;
    // The run a mark placed at `q` belongs to: the nearest line / double line / wire / polyline.
    QString runAt(const Vec2& q) const;
    // Slide a break mark along the run it is attached to by `delta`. False when it is attached to
    // nothing, in which case it moves like anything else.
    bool slideBreakAlongHost(Entity& e, const Vec2& delta);
    // The mark a line drawn from `a` to `b` across the runs describes: where it sits, which way it
    // lies, how far it reaches, and which run it belongs to. False when the line crossed nothing.
    bool breakFromBand(const Vec2& a, const Vec2& b, Entity& out) const;
    // The four corners of what a break mark hides, in world units: stroke 0's two ends then stroke 1's
    // (so the quad reads round its edge). Empty when the mark masks nothing.
    std::vector<Vec2> breakMaskQuad(const Entity& mark) const;
    // `segs` with every stretch hidden by a break mark on `host` removed - what selection and
    // hit-testing may see of a run.
    // Whether `p` falls in a stretch of `host` that a break mark hides.
    bool pointHiddenByBreak(const Entity& host, const Vec2& p) const;
    std::vector<std::pair<Vec2, Vec2>> unmaskedSegments(
        const Entity& host, const std::vector<std::pair<Vec2, Vec2>>& segs) const;
    // The nearest point ON THE BODY of a wire (between its ends) within `tol`, and which wire it is.
    // What lets a run be landed on another run as a T, and what says a joint belongs there.
    std::optional<Vec2> wireBodyPointNear(const Vec2& q, double tol, QString* wireId = nullptr,
                                          const QString& except = QString()) const;
    // Is this point ON something a wire can be connected to - a pin end, another wire's end or body,
    // a joint? Such a point is where the user said the wire goes, and nothing may nudge it.
    bool isConnectablePoint(const Vec2& q) const;
    // Join this wire to another whose free end has been brought onto one of its own: the two
    // become a single run with a splice at the joint. True if a join was made.
    bool mergeWireEnds(const QString& wireId);
    // Note what a shield's drain leg has been dropped on, so the X goes and the shield can
    // pull that wire; and bring that wire along after the leg has moved.
    bool attachShieldLeg(Entity& screen);
    void pullShieldLegWire(Entity& screen);
    // Slide a Screen (shield) along the wires it catches to world-x `x`, re-deriving each
    // crossing from its own wire and carrying the drain leg along — a shield is pinned to
    // its wires and never translates off them. False for a decorative copy with no wire
    // references (the caller then translates freely).
    bool repositionScreenAtX(Entity& screen, double x);
    // The wires a shield of half-height `ry` would enclose (crossings + wire ids, parallel
    // arrays), measured along its major axis through its centre.
    bool shieldCatchAt(const Entity& screen, double ry, std::vector<Vec2>& crossings,
                       std::vector<QString>& wireIds) const;
    // ---- Wire label & marking ----------------------------------------------
    // Set the selected wire(s) label, carrying the new text onto any Wire label
    // (NetLabel) entity attached to them so the drawing and the property agree.
    void setSelectionWireLabel(const QString& text);
    // Set how many colour/gauge markings run along the selected wire(s), keeping the
    // positions of the instances that already exist.
    void setSelectionWireAnnotCount(int count);
    // How many times ONE marking is repeated along each selected wire ("color" | "gauge" |
    // "label"); the places already dragged are kept and new instances spread evenly.
    void setSelectionWireAnnotCountFor(const QString& part, int count);
    // How far above the wire ONE marking sits ("color" | "gauge" | "label").
    void setSelectionWireAnnotGap(const QString& part, double gap);
    // The text height of ONE marking ("color" | "gauge" | "label").
    void setSelectionWireAnnotSize(const QString& part, double height);
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
    // When the application last put something on its OWN clipboard (ms since the epoch). A paste has
    // to choose between what was copied in here and what is on the system clipboard - a screenshot,
    // say - and the answer is simply whichever happened last.
    qint64 clipboardStamp() const { return clipStamp_; }
    bool clipboardEmpty() const { return clipboard_.empty(); }
    // Put a set of entities on the clipboard and start placing them: the parts library inserts a
    // part's drawing this way, so it is dropped by a click like anything else that is pasted.
    // `heldAt` is the point the cursor holds the set by. Given, the set hangs by THAT point - a part
    // is held by its middle, because that is where you mean to put it - and left out, by the corner of
    // its own extent, which is what a pasted set of objects has always done.
    void placeEntities(const std::vector<Entity>& ents,
                       const std::optional<Vec2>& heldAt = std::nullopt);
    // A LABEL laid across the runs it is put on, rather than dropped at a fixed size.
    //
    // It goes on with a STROKE, the way the length-break mark does: draw a line across the runs the label
    // covers, and the label is however wide those runs are. That is the whole reason it is a stroke and not
    // a click - how wide a label has to be is not the drawing office's decision, it is however wide the
    // bundle is at the point the label goes, and only the person placing it knows which runs are meant. A
    // bundle of eight passing a bundle of three is one click either way.
    //
    // It exceeds the outermost run it crosses a little, because a label flush with the edge of a bundle
    // reads as a label that missed. What it covers is masked: a label is opaque, and a label with the wires
    // drawn straight over the top of it reads as a label BEHIND the harness.
    // `exceedMm` is how far past the first and last run crossed the label goes, at each end - the model's
    // own house figure. Zero falls back to a proportion of the label's size, which is what it did before
    // there was a figure to ask for.
    void beginLabelPlacement(const std::vector<Entity>& drawing, double exceedMm = 0.0);
    bool placingLabel() const { return labelPlacing_; }
    bool labelStrokeStarted() const { return labelPlacing_ && labelStrokeHasStart_; }
    Vec2 labelStrokeStart() const { return labelStrokeStart_; }
    Vec2 labelStrokeEnd() const;
    // Tab pins the preview, so the two sizes can be tried on the real thing instead of on something
    // jumping about under the cursor. The same gesture that pins a break mark and a twisted bundle.
    void freezeLabelPreview();
    bool labelFrozen() const { return labelFrozen_; }
    // THE LABEL'S TWO SIZES, whichever stage it is at: still a preview under the cursor, or down on the
    // drawing and selected.
    //
    // WIDTH IS THE DISTANCE LEFT TO RIGHT AND HEIGHT THE DISTANCE UP AND DOWN. Not along the run and across
    // it - those are the label's own axes, and naming the fields after them was wrong twice over in opposite
    // directions, because it made the answer depend on which way the cable happens to run.
    //
    // Nobody reads a label that way. So Width is whichever of the label's two dimensions lies nearer the
    // horizontal where it sits, and Height is the other: on a bundle drawn across the sheet Width is the length
    // along the wire, and on one drawn up the sheet it is the reach across the bundle. It follows the eye
    // rather than the geometry, which is the only reading of those two words that does not change meaning with
    // the cable.
    // HOW MUCH BIGGER a detail view is drawn than the thing it is a view of. Two is the drawing-office
    // default: enough to read, and small enough to fit on the sheet beside what it came from.
    double& detailScale() { return detailScale_; }
    double detailScale() const { return detailScale_; }
    int detailStage() const { return detailStage_; }
    // THE COPY, AS REAL GEOMETRY: what a view that is NOT associative shows. The objects inside its boundary,
    // copied once, enlarged, clipped to the boundary and set down in the view as ordinary objects - so editing
    // them is ordinary editing and the parent does not move.
    void materialiseDetailCopy(const Entity& view);
    void clearDetailCopy(const QString& viewId);
    // Every detail view has what it should have: a copy if it is not associative, none if it is.
    void syncDetailCopies();
    Vec2 detailCenter() const { return detailCenter_; }
    double detailRadius() const { return detailRadius_; }
    static constexpr double kLabelWidthMm = 40.0;    // along the run, on a bundle drawn across the sheet
    static constexpr double kLabelHeightMm = 12.0;   // across the runs, on that same bundle
    bool labelLive() const { return !labelLiveIds_.empty(); }
    // Whether the label's ALONG axis - the way the run goes - is the one lying nearer the horizontal. This is
    // what decides which of its two sizes answers to Width and which to Height.
    bool labelAlongIsHorizontal() const;
    double labelWidth() const;
    double labelHeight() const;
    // The label's OWN two sizes, in its own axes rather than the sheet's: along the run it is on, and across it.
    // Width and Height above are what somebody reads off the sheet; these are what the geometry is built from,
    // and the pair a test has to ask for if it wants to know how deep the band should be.
    double labelAlongMm() const { return labelFit_.ok ? labelFit_.alongMm : labelAlongMm_; }
    double labelAcrossMm() const { return labelFit_.ok ? labelFit_.acrossMm : labelAcrossMm_; }
    // How far the label's middle sits OFF the run it is laid along. The middle of a BUNDLE is not the middle of
    // the one run the label is measured from, so the two rails of the band are at midOff +/- half the depth from
    // that run rather than symmetrically either side of it.
    double labelMidOffMm() const { return labelFit_.ok ? labelFit_.midOff : 0.0; }
    // THE LABEL'S OWN JOINTS: one at each end of it and one on each side, in that order - along the run one way,
    // along it the other, across it one way, across it the other.
    //
    // A label is a grouped object, so it has none of the joints its lines would have had, and until now the only
    // thing that could be done to it by hand was to slide it: its size had to be typed. A label is sized by eye
    // more often than by number - it is as wide as the bundle it covers and as long as it needs to be to be read -
    // and every other object on the sheet is sized by dragging a corner of it.
    //
    // Four rather than eight: a label is symmetrical about the run it sits on, so pulling one end out lengthens it
    // both ways about its middle. Dragging one corner of a label to stretch it off-centre would take it off the
    // bundle, which is the one place it has to stay.
    std::vector<Vec2> labelJoints() const;

    void setLabelWidth(double mm);
    void setLabelHeight(double mm);
    // One place both go through: `horizontal` says which way ON THE SHEET the number is meant, and it is
    // applied to whichever of the label's own axes is lying that way.
    void setLabelSize(bool horizontal, double mm);
    // A label already on the drawing, taken up so the ribbon and the properties act on it - read off the
    // label's own meta, so one placed last week resizes the same as one placed a second ago.
    bool adoptLabelFromSelection();
    // Done fitting: the label stays where it is and the ribbon goes back to normal.
    void endLabelEdit();
    void appendEntities(const std::vector<Entity>& ents);
    // Everything currently selected, copied out (the parts library captures a part's drawing from
    // the sheet this way).
    std::vector<Entity> copyOfSelection() const;
    bool isPastePlacing() const { return pastePlacing_; }
    // Flip the paste preview before placing: xAxis mirrors across a vertical line
    // through the base (flip X), else a horizontal line (flip Y). Connectors flip too.
    void togglePasteMirror(bool xAxis);
    // How many objects are on the clipboard. The clipboard is the EDITOR'S, not the drawing's, so it
    // survives switching tabs - which is what lets a selection copied inside a block be pasted outside it.
    int clipboardCount() const { return static_cast<int>(clipboard_.size()); }
    // What is being placed, to look at. A caller that wants to know WHERE the set hanging off the cursor
    // is - a test of "the fanout goes down in front of the symbol" - cannot answer that from a count.
    const std::vector<Entity>& clipboardContents() const { return clipboard_; }
    // A stock symbol just put down: where its two leads land on a net, the net is cut between them, so the
    // part is IN the circuit rather than lying across it. Returns how many nets were cut.
    int cutNetsUnderSymbols(const std::vector<QString>& placedIds);

    // While something is being MOVED - dragged by the pointer, or by one of its own joints - Space
    // turns it a quarter turn clockwise about the cursor and Shift+Space the other way. Turning a part
    // as it is carried to its place is one gesture instead of three (drop it, rotate it, move it back),
    // and it is the same key pair as when a part is held before being placed.
    bool movingSomething() const { return editing_ && !selected_.empty(); }
    void rotateMoveBy90(bool clockwise);

    // Where a command has just refused to do something, and why. Drawn on the drawing as a struck-out
    // circle: a line of words at the bottom of the window is easy to miss with the eye on the sheet.
    // Cleared by the next thing that works, by Esc, or by leaving the tool.
    bool refused() const { return refused_; }
    Vec2 refusedAt() const { return refusedAt_; }
    void clearRefusal();

    // ---- Turning what is held under the cursor -------------------------------
    // Anything waiting to be placed - a paste, a block, a part out of the library, a generated
    // harness - can be turned before it is dropped: R a quarter turn clockwise, Shift+R the other
    // way. Otherwise the only way to put a symbol in sideways is to drop it and then rotate it,
    // which is two commands and an undo when the drop lands in the wrong place.
    bool placementPreviewActive() const { return pastePlacing_ || harnessPlacing_; }
    void rotatePlacementPreview(bool clockwise);
    // How far it has been turned, in radians (a multiple of a quarter turn).
    double placementPreviewRotation() const { return placeRot_; }
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
    // Whether Align can line this up: it must be something that rides ON a wire.
    bool isAlignable(const Entity& e) const;
    // Where a column crosses a wire, as a fraction along it.
    double wireFractionAtX(const Entity& wire, double x) const;
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

    // The overlay's ink, as it must come out where it is being drawn. Every colour in the overlay - the
    // line being drawn, the ghosts, the guides, the grips, the selection - was chosen to read on the dark
    // canvas of model space, and pale blue on pale grey is exactly right there. On a LAYOUT the same ink
    // is laid on white paper, where it all but disappears: which is why a line being drawn inside a
    // window could not be seen before it was placed. Same hue, taken down to where it reads on white,
    // and made more solid.
    static QColor paperInk(const QColor& c);
    // The wires lit as the rest of the selection's bundle (read for tests / status).
    const QSet<QString>& bundleCompanions() const { return bundleCompanions_; }

    // ---- Wire markings picked on their own ----
    // A selection zone that catches a marking's text picks THAT marking, not the wire it belongs
    // to. A marking so picked can only be slid along its own wire: it names that wire, so it can
    // never be pulled off it.
    struct MarkPick {
        QString wireId;
        QString part; // "color" | "gauge" | "label"
        int index = 0; // which instance of it
        bool operator==(const MarkPick& o) const {
            return wireId == o.wireId && part == o.part && index == o.index;
        }
    };
    const std::vector<MarkPick>& markSelection() const { return markSel_; }
    void clearMarkSelection();
    // The four corners of the box a marking's text occupies, in world space.
    bool markQuad(const Entity& wire, const QString& part, int index,
                  std::vector<Vec2>& quad) const;
    // Is a marking of the current pick under `p`? (Its index in markSel_, or -1.)
    int markSelectionAt(const Vec2& p) const;
    // Slide every picked marking to the place on ITS OWN wire nearest `to`, or just the one at
    // `only` when `all` is false.
    void dragPickedMarks(const Vec2& to, bool all, int only);
    // Re-read which wires share a bundle with the selection.
    void refreshBundleCompanions();
    // Connect the refresh to selectionChanged (called once, from the constructor).
    void hookSelectionCompanions();
    // A placed block is one object: pull every piece of a picked copy into the selection. True when
    // that added anything, so the caller knows to announce the selection again.
    bool expandBlockSelection();
    // Part of a block that has been placed - and so not editable on the sheet. Static: it asks only
    // about the entity, and is called from const paths that draw the grips.
    static bool isBlockPiece(const Entity& e);

    // Pixel tolerance (screen px) for snapping/hit testing.
    void setPixelTolerance(double px) { pixelTol_ = px; }
    // How big the editor's world is being SHOWN, in pixels per world unit, when that is not the
    // document's own view: inside a window on a layout the model is shown at the WINDOW's scale, and the
    // pick and snap tolerances - a few pixels of screen turned into world - must be turned with that
    // scale. Taken from the document's view (the model's own zoom, which has nothing to do with how the
    // window shows it), the catch radius came out wrong by whatever the two zooms differed by, and grew
    // with every step of zoom inside the window: the snap reached far across the drawing and jumped.
    // Zero puts it back to the document's own view.
    void setInputScale(double pxPerWorld) { inputScale_ = pxPerWorld > 0.0 ? pxPerWorld : 0.0; }
    double inputScale() const { return inputScale_; }

signals:
    void changed();              // geometry/document mutated -> repaint + panels
    void overlayChanged();       // interactive preview/cursor moved -> repaint only (no panel rebuild)
    void selectionChanged();
    // A selection zone was closed: the polygon it enclosed, in the coordinates of whatever is being
    // worked in, and which kind it was - the green CROSSING zone, where touching is enough, or the blue
    // window zone, which takes only what is wholly inside it. Shift makes it take away instead.
    // The canvas listens for this so that a zone drawn on a sheet also picks out the windows on it:
    // a window onto the drawing is not an entity, so nothing in the loop below would ever find one.
    void regionSelected(const std::vector<Vec2>& region, bool crossing, bool subtract);
    void toolChanged(ToolId t);
    void message(const QString& text);
    // Ctrl+V. The host decides what a paste means: the system clipboard may hold a picture that is
    // newer than anything copied in here. Not connected => the editor pastes its own clipboard.
    void pasteRequested();
    // Tool needs free-text/numeric input (prompt shown by the host UI).
    void textPromptRequested(const QString& prompt);
    // Double-clicked an existing text object — the host opens the editor for it.
    void editTextRequested(const QString& id);
    // Double-clicked a copy of a block — the host opens the block editor for it.
    void editBlockRequested(const QString& name);
    // Double-clicked the sheet frame — the host opens the template editor for it.
    void editTemplateRequested(const QString& name);
    void modelAnchorPicked(Vec2 point, bool near);
    // Where the part connects, and which way it runs from there. Two clicks: the point, then anywhere along
    // the direction - so the polar tracking and the object snaps give the angle, rather than a number typed
    // into a box that nobody can check against the drawing.
    void modelConnectPicked(Vec2 point, double angleRad);
    // A face the wires leave the model by, marked across it: the wires are spread along this line and may
    // not run off either end of it.
    void modelOutputPicked(Vec2 a, Vec2 b);
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
    // An object on a model's editing canvas picked as the one the app scales the model by.
    void modelScaleRefPicked(const QString& id);
    // A label was armed, placed, resized or let go of - the host shows the label's own ribbon group while
    // one is being fitted, and puts the rest of the tab away while it is, because hunting for the two
    // fields that matter among forty buttons is what makes a drawing slow to make.
    void labelChanged();

private:
    double worldTolerance() const;
    QString uid() const;
    void pushUndo();
    void addEntity(Entity e);
    void finishDraw();         // commit multi-point tool
    void cancelDraw();
    // The topmost object within reach of `world`, ignoring any whose id is in `skip` - which is how the
    // second pick of a two-object command finds the OTHER object where both meet at a joint.
    // Refuse, out loud and on the drawing.
    void refuseAt(const Vec2& where, const QString& why);
    std::optional<QString> hitTest(const Vec2& world,
                                   const std::vector<QString>& skip = {}) const;
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
    // Trim ONE physical line of a double run: the fence crossed that line and not the centreline, so
    // that line alone comes back to the reference and the other keeps its length. True when it did it.
    // One pass of a trim: what to cut, and what bounds it. Run twice - the selection as the boundary,
    // then the selection as the thing to cut - so the command works whichever way round they were picked.
    int trimPass(const Vec2& a, const Vec2& b, const std::vector<QString>& onlyIds,
                 const std::vector<Entity>& boundaries, QString* whyOut);
    // Trim the piece UNDER THE POINTER, which is how trimming is asked for first: click the bit that
    // should be gone. The fence stays - it is how several objects come off at once - but a click on the
    // piece did nothing at all unless the two clicks happened to straddle exactly the right things.
    bool trimPickAt(const Vec2& pick, const std::vector<Entity>& boundaries, QString* why,
                    QString* what = nullptr,
                    const QString* onlyId = nullptr);
    // The shared tail of both: this line, the piece of it the pick is in, back to the boundary.
    bool trimRailPiece(Entity& ent, const std::vector<Vec2>& rail, int side, double alongPick,
                       const std::vector<Entity>& refs, QString* why);
    bool trimOneRailOfDouble(Entity& ent, const Vec2& fa, const Vec2& fb,
                             const std::vector<Entity>& refs, QString* why = nullptr);
    // Record a cut for ONE physical line at an end, KEEPING whatever cut the other line has there. Each
    // end has two slots, one per line, so the two lines of a run can come back to different places -
    // with a single slot the second line to be trimmed simply replaced the first.
    bool capDoubleRailAt(Entity& ent, const QString& key, const Vec2& at,
                         const std::vector<Entity>& refs, int side);
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
    // Whether this object is a table with one of its cells picked, and which cell. What is drawn and
    // what can be grabbed both ask, so they cannot disagree.
    bool pickedCellOf(const Entity& e, int& row, int& col) const;
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
    // The other wires of the bundle(s) the selection belongs to: lit for information, never
    // selected, so an edit only ever reaches the wire that was clicked.
    QSet<QString> bundleCompanions_;
    // Markings picked on their own (see MarkPick), and the drag of one of them.
    std::vector<MarkPick> markSel_;
    bool markGrab_ = false;
    int markGrabAt_ = -1;
    bool markGrabAll_ = false; // Shift: the whole pick follows the cursor, wire by wire
    bool markMoved_ = false;
    std::vector<Entity> clipboard_;

    // Cursor + snap.
    Vec2 cursor_;
    SnapResult snapResult_;
    QString breakStyle_ = QStringLiteral("straight"); // "straight" | "curved"
    double breakSize_ = 0.0;                          // 0 = as wide as the run needs
    double breakGap_ = 0.0;                           // 0 = a proportion of the reach
    double breakAngle_ = -1.0;                        // <0 = the shape's own lean (30° / upright)
    bool breakFrozen_ = false;                        // Tab pinned the preview
    int twistWires_ = 4;             // what a twisted harness fans out to, until the ribbon says otherwise
    bool twistedFrozen_ = false;     // Tab pinned a twisted bundle being drawn
    Vec2 twistedFrozenAt_{0.0, 0.0}; // ... at this end point
    Vec2 breakFrozenAt_{0.0, 0.0};
    // The sheet and its windows, as things to snap to. Empty in model space.
    std::vector<Bounds> snapRects_;
    // The best of them for a point, or nothing within tolerance.
    SnapResult snapToRectangles(const Vec2& raw, double tol) const;

    // A rectangle being picked for something outside the editor (the plot window).
    void endRectanglePick();
    bool rectPick_ = false;
    bool rectPickHaveFirst_ = false;
    Vec2 rectPickFirst_;
    std::function<void(Vec2, Vec2)> rectPickDone_;
    std::function<void()> rectPickCancel_;

    double pixelTol_ = 10.0;
    // The first click of the connection-point pick, waiting for the second that gives the direction.
    bool haveConnectFirst_ = false;
    // The first end of an output line, while waiting for the second.
    bool haveOutputFirst_ = false;
    Vec2 outputFirst_;
    // A label being laid on the runs: the drawing as it was captured, the stroke being drawn across the
    // runs it goes on, and what it looks like for that stroke - worked out on every move, because what it
    // has to cover changes as the stroke is drawn.
    bool labelPlacing_ = false;
    bool labelStrokeHasStart_ = false;
    bool labelFrozen_ = false;      // the stroke pinned with Tab, so the ribbon can be used on it
    Vec2 labelStrokeStart_;
    Vec2 labelFrozenAt_;
    std::vector<Entity> labelProto_;
    std::vector<Entity> labelPreview_;
    // WHAT THE LABEL COVERS, as the label's own four corners rather than as a box round them. A bundle that
    // runs at an angle gets a label that runs at that angle, and a mask taken as the bounding box of it
    // stood square to the sheet with the label lying across it corner to corner - covering ground the label
    // is nowhere near and, at 45 degrees, half again as much of it.
    std::vector<Vec2> labelCovers_;
    // The two sizes as they stand, kept across a session so the next label goes on at the size the last one
    // was set to - a drawing office's labels are all one size, and typing it once should be enough.
    double labelAlongMm_ = kLabelWidthMm;     // along the run
    double labelAcrossMm_ = kLabelHeightMm;   // across the runs
    // Either size said by hand: the stroke stops deciding that one. Until then the label keeps its SHAPE
    // and changes its SIZE - the wires say how far across it has to reach and the length follows by the same
    // factor, so a label on a pair of wires is a small label and not a big one squashed.
    bool labelAcrossManual_ = false;
    bool labelAlongManual_ = false;
    // How far past the outermost run crossed it goes, at each end: the model's house figure, or 0 for a
    // proportion of the label's own size.
    double labelExceedMm_ = 0.0;
    // HOW A LABEL SITS: where its middle is, which way the runs it is on go, and the size it takes. Kept
    // apart from the drawing so that a width typed on the ribbon, or a slide along the runs, is a change to
    // three numbers and a redraw rather than a re-measure of the sheet.
    struct LabelFit {
        bool ok = false;
        Vec2 mid;
        Vec2 runDir{1.0, 0.0};      // the way the runs it is on run, at the middle of the label
        Vec2 acrossDir{0.0, 1.0};   // square to them
        double alongMm = 0.0;       // along the runs  - "Width"
        double acrossMm = 0.0;      // across the runs - "Height", what has to cover the bundle
        // THE RUN IT IS ON: the whole of it, how far along it the label sits, and how far off it.
        //
        // A label is COUPLED to its run, and the path is kept whether the run bends or not - because sliding a
        // label along a straight stretch that turns further on has to take it round the turn rather than carry
        // it on into thin air.
        //
        // `bent` is whether the label REACHES a bend and so has to be laid round one: every point placed at a
        // distance along the run and an offset across it, the way words are set on a curve.
        //
        // `midOff` is how far the label's middle sits OFF the run. The middle of the BUNDLE is not the middle
        // of the one wire the run happens to be, and a label centred on that wire instead has half of itself
        // hanging off the bundle - which is what a label on a bundle of several wires looked like.
        std::vector<Vec2> path;
        double midS = 0.0;
        double midOff = 0.0;
        bool bent = false;
    };
    LabelFit labelFit_;
    // Measured off the stroke a..b: which runs it crosses, how far they spread, and the size that covers
    // them. Does not touch any state, so the preview and the commit can ask the same question.
    // What the label is measured BY: the object marked as the model's scale reference, or the whole drawing
    // when nothing is marked. A label under clear heatshrink is a label with a sleeve over it, and it is the
    // label that has to match the bundle.
    Bounds labelReferenceBounds() const;
    // THE WHOLE RUN through one object, as a path: itself, and whatever joins on at either end.
    //
    // A filleted corner is not one object. It is a line, an arc, and another line, each trimmed to meet the
    // next - so a label laid along "the run it crossed" would follow one straight line, walk off the end of it
    // and hang in the air where the fillet turns away. Chained, the three read as the one run they are on the
    // sheet, and the label hugs the fillet.
    //
    // Arcs and polyline bulges are flattened, because a label is laid by arc length and a path of points is
    // what that needs.
    std::vector<Vec2> runPathThrough(const QString& hostId) const;

    LabelFit labelFitFor(const Vec2& a, const Vec2& b) const;
    // The captured drawing laid out to that fit, and the ground it covers.
    std::vector<Entity> labelEntities(const LabelFit& fit, std::vector<Vec2>& covers) const;
    // The layout itself. labelEntities calls it, CHECKS what came back, and falls back to a straight label if
    // anything the bent one is made of crosses itself - so a fold can never reach the drawing whatever the cable
    // is doing, including whatever case has not been thought of yet.
    std::vector<Entity> labelEntitiesRaw(const LabelFit& fit, std::vector<Vec2>& covers) const;
    // The four corners of the ground a label covers, turned with it.
    static std::vector<Vec2> labelMaskQuad(const LabelFit& fit);
    void commitLabel();
    // The fit written on to every piece of the label, so it travels with the label in the file: a label
    // that has to be re-measured off the sheet before it can be resized is a label that cannot be resized
    // once the drawing has been closed and opened.
    static void stampLabelFit(Entity& e, const QString& group, const LabelFit& fit,
                              const QJsonArray& proto = QJsonArray());
    // Lay a placed label out again from the drawing it keeps, at the fit it now has. What a resize and a slide
    // both come down to on a bent one: reading a point's place on the run back off its geometry is ambiguous on
    // the inside of a bend, and doing it on every change compounded the error until the label crumpled.
    bool relayPlacedLabel();
    // The label being fitted: its objects, its mask and its group key, so a size typed on the ribbon or a
    // slide along the runs acts on THAT label rather than making another one.
    std::vector<QString> labelLiveIds_;
    QString labelMaskId_;
    QString labelGroup_;
    // Resize a label already on the drawing: its geometry is scaled about its own middle along its own axes,
    // rather than laid out again from the captured drawing, which may be long gone.
    bool scalePlacedLabel(double alongMm, double acrossMm);
    void relayLabelPreview();
    // Which of them is being dragged, or -1. Held rather than derived, because the drag has to go on meaning the
    // same joint after the cursor has moved away from where it started.
    int labelJoint_ = -1;
    // Clicking away ends the fitting; clicking the label again does not. `emptying` is the case where the
    // selection is being dropped outright, which always ends it.
    void dropLabelEditIfDeselected(bool emptying = false);
    // A drag on the live label slides it ALONG the runs it crosses. True when it took the drag: a label
    // sits on a bundle, and one nudged off the bundle by a careless drag is a drawing sent back.
    bool slideLiveLabel(const Vec2& delta);
    // A DETAIL VIEW being placed: the middle of what is wanted, then how much of it, then where the
    // enlargement goes. Three clicks, because that is three separate decisions and none can be guessed from
    // the others.
    double detailScale_ = 2.0;
    // Whether the cursor is CURRENTLY reaching through an annotative detail's window. Held rather than
    // re-asked mid-drag: a drag that started on an object seen through the window has to go on being read in
    // that object's own space, and the cursor leaving the circle part-way must not throw it into the other
    // one. Settled on every move that is not a drag, which is every move before a drag begins.
    bool detailThrough_ = false;
    int detailStage_ = 0;          // 0 = waiting for the centre, 1 = for the radius, 2 = for the placement
    Vec2 detailCenter_;
    double detailRadius_ = 0.0;
    // AN ANNOTATIVE DETAIL VIEW IS A LIVE WINDOW ON THE DRAWING, not a picture of it. A point inside the
    // enlarged circle is mapped back to the place in the drawing it is a view OF, so the cursor works on the
    // real geometry: pick something in the big circle and it is the real object that is picked, drag it there
    // and the real object moves, and because the detail is drawn from the document every time, the change
    // shows in both circles at once.
    //
    // Returns the point unchanged when it is not inside any detail view's enlargement, which is the ordinary
    // case. Every enlargement can be worked in - a detail view is drawn BECAUSE the thing is too small to work
    // on - and what annotative decides is whether the view is read live or is a picture told to look again.
    Vec2 throughDetailWindow(const Vec2& p) const;
    // Which detail view the cursor last reached through, if any. A view that is a PICTURE rather than a live
    // window has to be told to look again once something has been drawn through it, or the drawing would change
    // and the enlargement of it would not.
    QString detailWindowId(const Vec2& p) const;
    QString detailThroughId_;
    // A detail view whose copy is of somewhere else now, because it was moved or resized. Made again when the
    // drag finishes rather than on every step of it.
    QString detailCopyStale_;
    // The next free letter for a detail on this drawing. An office letters its details in the order they
    // were made and never renumbers them, so a letter is taken once and kept: the next one is the lowest
    // that is not in use, which after deleting B gives B back rather than jumping to D.
    QString nextDetailTag() const;
    // The next free connection-pin number: the lowest one not already on the drawing, so
    // deleting pin 2 and placing another gives 2 back rather than jumping to a new highest one.
    int nextConnectionPinNumber() const;
    // The first of the two runs a centre line goes between, while waiting for the second.
    QString centerLineFirst_;
    Vec2 connectFirst_;
    double inputScale_ = 0.0;   // 0 = the document's own view; otherwise px per world unit in use

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
    bool editMoved_ = false;
    // WHERE A DRAG STARTED, so a typed distance has something to measure from. editLast_ follows the cursor, so by
    // the time a number is typed it is no longer where the drag began - and "move it 40" means 40 from where it
    // was, not 40 from wherever it has got to.
    Vec2 editDragFrom_;
    bool editHasFrom_ = false;  // has the drag actually moved yet (for undo push)
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
    // Ctrl while drawing a double run swaps which side the second line goes.
    bool dpolySwap_ = false;
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
    std::vector<int> cellSel_;    // every picked cell of cellTableId_ (cellIndex_ is the first)
    // Dragging across cells: the cell the drag started on, and whether one is in progress. The
    // block between that cell and the one under the cursor is what is picked.
    int cellDragFrom_ = -1;
    bool cellDragging_ = false;
    // A balloon started from somewhere that already knows its number (a BOM line): the text the
    // next balloon takes, instead of counting up from the ones in the drawing.
    QString balloonTextNext_;
    bool rectAwaitY_ = false;     // rectangle: awaiting the Y length after the X length
    double rectX_ = 0;            // rectangle: signed X length already entered

    // Clipboard with a base point (AutoCAD COPYBASE / cut / paste-to-point).
    Vec2 clipboardBase_{0, 0};    // reference point the clipboard is stored relative to
    qint64 clipStamp_ = 0;        // when the internal clipboard was last filled
    bool awaitClipBase_ = false;  // next click sets the base point (after Ctrl+Shift+C / Ctrl+X)
    bool clipCut_ = false;        // the pending base-point op is a cut (delete originals)
    bool pastePlacing_ = false;   // paste in progress: next click drops the contents
    // A quarter turn at a time, applied to whatever is held under the cursor about the point it is
    // held by. Cleared whenever something new is picked up and when it is dropped or abandoned.
    double placeRot_ = 0.0;
    bool refused_ = false;         // a command said no, and the drawing shows where
    Vec2 refusedAt_{0.0, 0.0};
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
