#pragma once

#include "harness/HarnessBuilder.h"

#include <QDialog>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QMap>
#include <QPointer>
#include <QString>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QHBoxLayout;
class QKeyEvent;
class QLabel;
class QListWidget;
class QMenu;
class QToolButton;
class QPushButton;
class QSpinBox;
class QSplitter;
class QStackedWidget;
class QTableWidget;
class QTextBrowser;
class QVBoxLayout;
class QHideEvent;
class QResizeEvent;

namespace Loopwire {

class Editor;
class WizardFlowLayout;
struct LibraryPart;
// Drawn previews used inside the dialog (defined in the .cpp).
class ConnectorPreview;
class WiringDialog;
class HarnessWiringPreview;

// The Harness Builder: describe a whole harness once — connectors, their order and side,
// the pinout, every pin-to-pin wire, which wires are shielded or twisted, and where it all
// sits — then Generate draws the schematic in one step.
//
// The dialog is a thin editor over a HarnessDef; all the geometry, validation and crossing
// work lives in harness/HarnessBuilder so it can be tested without a UI. Every control
// writes into def_ and asks for a re-sync, so the tables, the block map and the pin map can
// never drift apart from the definition they show.
// THE HARNESS AS THE BUILDER'S PREVIEW SHOWS IT: what Generate draws, with each wire named as its twists and screens
// name it, so a twisted wire leaves the gap its twist is drawn in. Screens and their drains left out unless asked.
std::vector<Entity> harnessPreviewEntities(const HarnessDef& def, bool showShields);
struct PartsLibrary;
enum class PartKind;
const PartsLibrary& harnessBuilderLibrary();
void applyHarnessWirePart(HarnessWire& wire, const LibraryPart& part, int conductor = 0);
// For the tests: the parts the builder offers, given rather than read from the libraries on this machine.
void useHarnessBuilderLibraryForTest(const PartsLibrary& lib);

class HarnessBuilderDialog : public QDialog {
    Q_OBJECT
public:
    explicit HarnessBuilderDialog(Editor* editor, QWidget* parent = nullptr);

    // Place a CABLE from the library: one click gives the harness every conductor the cable has, with
    // its colours, cross-sections, twisted groups and screens already set. `libPartId` empty asks
    // which cable. Returns how many conductors were added.
    int addCableFromLibrary(const QString& libPartId = QString());
    // ADDED IN ONE CLICK, from the Add menu: two wires twisted, or screened (and twisted), or a coax - between the
    // connectors the last wire used, on the next free pins. The ids of the wires added.
    enum class AddWhat { Wire, TwistedPair, ScreenedPair, ScreenedTwistedPair, Coax };
    QStringList addConnection(AddWhat what);
    // THE WIRES SELECTED in the table (or, none selected, the one being edited) made into one thing - twisted, screened,
    // one cable, one coax - or taken back out of it. A screen's drain goes to `drainConn` (its next free pin), where one
    // is named. The name given, or "" (and why, on the summary line).
    enum class GroupAs { Twisted, Screened, TwistedScreened, Cable, Coax, Ungroup, UngroupTwist, UngroupScreen, UngroupCable };
    QString groupSelectedWires(GroupAs how, const QString& drainConn = QString());
    // The wires the table has selected - or the one being edited, where none is.
    QStringList selectedOrCurrentWireIds() const;
    // A COPY of the wire selected (its whole cable, where it is in one), its ends left open; its two ends swapped.
    void duplicateSelectedWire();
    void swapSelectedWireEnds();
    // THE WIRING WINDOW: the connectors drawn with their pins, wired by hand - opened on the two the wire being edited joins.
    WiringDialog* openWiringWindow();

    // What the builder currently describes. Read-only: everything that changes a harness goes
    // through the dialog's own controls, so the definition and the tables can never disagree.
    const HarnessDef& definition() const { return def_; }
    // A HARNESS DEMAND READ IN (see HarnessDemand): the builder takes its definition whole, as a loaded pattern is
    // taken, and says where it came from.
    void loadDemand(const HarnessDef& def, const QString& from);

    // UNDO AND REDO inside the builder: every change to the harness is one step (typing in one field, a step for the
    // burst). Ctrl+Z and Ctrl+Y here too. Whether there is one to take back, or to do again.
    void undo();
    void redo();
    bool canUndo() const { return !undoStack_.isEmpty(); }
    bool canRedo() const { return !redoStack_.isEmpty(); }

    // A CONNECTOR FROM THE LIBRARY: its P/N, pin count and part - and its contact, where the library ties one to it (or
    // `contact` given) - put on the side with fewer connectors. Its id.
    QString addConnectorFromLibrary(const LibraryPart& part, const LibraryPart* contact = nullptr);
    // THE PICTURE OR THE PIN MAP under the steps' checks: either can be looked at at any step.
    bool showingPinMap() const;
    void showPinMap(bool on);

    // CTRL+HOVER ON THE PIN MAP: the pin under the pointer, and every pin wired to it on the other connectors, lit. The pins
    // lit now, each "connector id:pin". Not saved.
    QStringList litPins() const { return litPins_; }
    void lightPins(const QString& connId, int pin);   // an empty id: none lit

    // THE STEPS, one on screen at a time: 0 Connectors (with their placement), 1 Wires, 2 Shields, 3 Layout. Which is
    // open; open another; how many a step's checks find wrong.
    int currentStep() const;
    void setStep(int step);
    int stepIssueCount(int step) const;
    // The step a check's finding belongs to - where it is put right.
    static int stepOfIssue(const HarnessIssue& issue);

signals:
    // "See it in the library": the host owns the library window, so it is asked to open it at the part.
    void openLibraryAt(const QString& libPartId);

protected:
    void keyPressEvent(QKeyEvent* e) override;
    // The libraries are read again every time this window is opened, so a part added since it was
    // last used is there to be picked.
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private:
    void initializeWizardUi();
    void fitWizardColumns();
    void saveWizardUi();
    void setCompactColumns(bool compact);
    void selectWire(const QString& id);
    void applyPinHighlights(const QStringList& pins);
    void highlightWires(const QStringList& wires);
    void refreshWireDetails();
    void refreshLiveBom();
    void openLiveBom();
    // The BOM in its own window ("window") or attached to the wizard ("left", "right", "bottom"). Remembered.
    void placeLiveBom(const QString& where);
    void keepDeleteColumnLast();
    static constexpr int kDeleteColumn = 12;      // the row's delete button, in both tables
    static constexpr int kDeleteWidth = 26;
    static constexpr int kDetailFieldWidth = 200; // a field of the connection details, whatever the window's width
    QComboBox* wizardPartChoice(QWidget* parent, const std::vector<PartKind>& kinds, const QString& current);
    QWidget* wireDetails_ = nullptr;
    QSplitter* wireSplit_ = nullptr;
    QDialog* bomDialog_ = nullptr;
    QTableWidget* bomTable_ = nullptr;
    QWidget* bomPanel_ = nullptr;     // the BOM, wherever it is put
    QSplitter* bomSplit_ = nullptr;   // the wizard's work and an attached BOM
    QString bomDock_ = QStringLiteral("window");   // where the BOM goes. Not saved with the harness.
    bool compactColumns_ = true, wizardUiReady_ = false, wizardUiBuilt_ = false;
    QString selectedWireId_;
    QStringList pinnedPins_;
    // --- Sections, in the order they appear ---
    QWidget* buildConnectorsSection();
    QWidget* buildBlockMapSection();
    QWidget* buildPinMapSection();
    QWidget* buildWiresSection();
    QWidget* buildLayoutSection();
    QWidget* buildIssuesSection();

    // --- Re-sync the UI from def_ (the definition is the single source of truth) ---
    void reloadAll();
    void reloadConnectors();
    void reloadBlockMap();
    void reloadPinMap();
    // Redraw the wiring preview from the definition without rebuilding the pin map: a layout
    // parameter changes the picture but not the tables.
    void refreshPreview();
    void reloadWires();
    // Build the editable cells of one wire onto table row `row`. Split out of reloadWires
    // so the grouped layout can interleave group headers between the wire rows.
    void buildWireRow(int row, const HarnessWire& w, const QStringList& connChoices);
    // Insert a spanning, clickable header for a group of wires; returns nothing but hides
    // its members when the group is folded.
    void buildGroupHeader(int row, const QString& key, const QString& title, int memberCount);
    // The rows of one bundle / screen / twisted pair carry ONE cell between them, merged down the
    // column and marked with what the block is.
    void mergeGroupCells();
    // The Group column's cell for these rows: what they are part of, said, and a click to rename it or take it apart.
    QWidget* wireGroupCell(const QStringList& ids, const QString& caption, const QString& tint);
    // The part menu for a row, and the wiring that lets a right-click ON A CELL reach it.
    void showWireRowMenu(const QString& wireId, const QPoint& globalPos);
    void armRowMenu(int row, const QString& wireId);
    void armCellMenu(QWidget* cell, const QString& wireId);
    void reloadLayout();
    // Give each section the height its contents need, so nothing has to be dragged into view.
    void showIssues(const std::vector<HarnessIssue>& issues, bool sayWhenClean);

    // --- Actions ---
    void addConnector(HarnessSide side);
    void removeConnector(int row);
    // Right-clicking anything that names a library part: look at it, or open the library at it. The
    // same menu wherever a part is named, because the question is the same.
    void showPartMenu(const QString& libPartId, const QPoint& at, const QString& what);
    // The questions about a part, put in a menu: its drawings, what it is, the library it is in.
    void fillPartMenu(QMenu& menu, const LibraryPart& part);
    void updatePartFromLibrary(const QString& libPartId);
    QMap<int, QByteArray> checkedSteps_;
    void addWire();
    // The lowest pin of a connector no wire has taken yet - what a new row, or each conductor of a
    // placed cable, lands on.
    int nextFreePin(const HarnessConnector& c) const;
    // Add a wire belonging to the group `key` names - a grouped list has to know where a new row
    // goes; set from a group header click while an Add is waiting.
    void addWireToGroup(const QString& key);
    void removeWire(int row);
    void autoFillPins();     // give every wire the next free pin at each end
    void applyBlockOrder();  // read the dragged block lists back into `order` / `leg`
    void applyPinOrder();    // read the dragged pin lists back into each pinOrder
    void runValidate();
    void runMinimizeCrossings();
    // Targeted pinout optimisation: gather the shielded groups (shield=true) or the
    // twisted groups onto neighbouring pins.
    void runOptimizeBy(bool shield);
    // The pin optimisers that are switched on, run together in one pass (see optimizeHarnessCombined).
    void runCombinedOptimise(bool crossings, bool shields, bool twists);
    bool eventFilter(QObject* watched, QEvent* ev) override;
    void generate();
    void saveDefinition();
    void loadDefinition();
    // The definition as a document, and back: the file format and the in-memory model in one
    // place, so Save and Load cannot drift apart.
    QJsonObject definitionToJson() const;
    void applyDefinitionJson(const QJsonObject& root);
    // Patterns kept in the DRAWING rather than in a file: saved after a generate, chosen to be
    // loaded again, deleted when they are no longer wanted.
    void showPresetMenu(QWidget* under);
    // The preset a generate wrote, so the menu can point at what was just kept.
    QString lastPresetName_;
    void savePresetToDrawing();
    void loadPresetFromDrawing(const QString& name);
    void deletePresetFromDrawing(const QString& name);

    // Helpers.
    QString uniqueConnectorId() const;
    QString uniqueWireId() const;
    HarnessConnector* connById(const QString& id);
    // The wire ids of the rows currently picked in the wire table.
    QStringList pickedWireIds() const;
    // Where a new screen's drain goes, asked: a connector (its next free pin), or "" for no drain wire yet.
    QString askDrainConnector();
    QStringList connectorChoices() const;
    // Every endpoint a wire end can name - connectors and shields - as (key, label) pairs.
    QList<QPair<QString, QString>> endpointChoices() const;
    static QStringList gaugeChoices();
    // A gauge as the list shows it ("20 AWG" or "0.5 mm2") as the mm2 figure kept, and the other way.
    static QString gaugeValueOf(const QString& label);
    static QString gaugeLabelOf(const QString& mm2);
    static QStringList colorChoices();
    // Guard: while a reload is writing the widgets, their signals must not be read back
    // as user edits.
    bool syncing_ = false;

    Editor* editor_ = nullptr;
    HarnessDef def_;
    // THE STEP SHELL: the steps down the left, each step's page, Back and Next, the Shields step's table.
    QListWidget* stepList_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QPushButton* backBtn_ = nullptr;
    QPushButton* nextBtn_ = nullptr;
    bool previewBottom_ = false;              // the drawing under the step rather than beside it (remembered)
    QWidget* shieldCards_ = nullptr;          // the Shields step: a card a connector
    QStackedWidget* viewStack_ = nullptr;     // the drawing, or the pin map
    QSplitter* mainSplit_ = nullptr;          // the step's page, and the drawing/pin map beside it - or under it
    QPushButton* pinMapBtn_ = nullptr;
    QLabel* viewCaption_ = nullptr;
    void applyStepLayout(int step);
    QStringList litPins_;
    void calmSpinBoxes();
    std::vector<HarnessIssue> liveIssues_;   // the checks, run again at every change. Not saved.
    QWidget* buildShieldsSection();
    QWidget* buildPreviewColumn();
    void reloadShields();
    void updateSteps();
    // UNDO: the harness as it was before each change, as the file keeps it; and what was taken back. Not saved.
    QList<QByteArray> undoStack_, redoStack_;
    QByteArray lastSnapshot_;
    qint64 lastChangeMs_ = 0;
    QWidget* lastTypedIn_ = nullptr;   // the field the last change was typed into - only compared, never used
    QPushButton* undoBtn_ = nullptr;
    QPushButton* redoBtn_ = nullptr;
    void noteChange();
    void restoreSnapshot(const QByteArray& snapshot);
    int connSeq_ = 0;
    int wireSeq_ = 0;

    QTableWidget* connTable_ = nullptr;
    QListWidget* leftBlocks_ = nullptr;
    QListWidget* rightBlocks_ = nullptr;
    QWidget* pinMapHost_ = nullptr;
    // The whole harness drawn small beside the pin tables.
    HarnessWiringPreview* wiringPreview_ = nullptr;
    WizardFlowLayout* pinMapLayout_ = nullptr;
    QTableWidget* wireTable_ = nullptr;
    // Outlines the picked rows (their cells are widgets, so the selection colour never shows).
    class PickOverlay* pickOverlay_ = nullptr;
    // How the wire rows are grouped, and which groups are folded. A long wire list is far
    // easier to work through by shield, by twisted group or by the connector pair it joins,
    // with the groups you are not touching collapsed out of the way. The fold state is kept
    // across reloads (every field edit rebuilds the table) so editing never springs a
    // collapsed group back open.
    enum class WireGrouping { None, Shield, Twist, Connectors, Assembly };
    // Grouped by the connector pair by default: that is how a harness is read - everything
    // running from P1 to P2, then P1 to P3 - and the groups follow the placement order.
    WireGrouping grouping_ = WireGrouping::Connectors;
    QSet<QString> collapsedGroups_;
    QSet<QString> seenAssemblyGroups_;
    // True while an Add wire is waiting for a group header to be clicked.
    bool pendingAddGroup_ = false;
    QComboBox* groupByBox_ = nullptr;
    // The wiring window, once opened (not saved).
    QPointer<WiringDialog> wiringWindow_;
    QDoubleSpinBox* textGapSpin_ = nullptr;
    QDoubleSpinBox* spliceSizeSpin_ = nullptr;
    QDoubleSpinBox* textHeightSpin_ = nullptr;
    QCheckBox* terminateWiresBox_ = nullptr;
    QCheckBox* spliceBox_ = nullptr;
    QCheckBox* shieldPreviewBox_ = nullptr;
    QTextBrowser* issueView_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    // What Generate produces: the schematic drawing (as it always has), or a wiring list built
    // straight from this definition instead.
    QComboBox* genModeBox_ = nullptr;

    QDoubleSpinBox* leftXSpin_ = nullptr;
    QDoubleSpinBox* leftYSpin_ = nullptr;
    QDoubleSpinBox* rightXSpin_ = nullptr;
    QDoubleSpinBox* gapSpin_ = nullptr;
    QDoubleSpinBox* legGapSpin_ = nullptr;
    QSpinBox* twistAmountSpin_ = nullptr;
    QSpinBox* shieldAmountSpin_ = nullptr;
    QSpinBox* wireShieldAmountSpin_ = nullptr;
    QDoubleSpinBox* shieldLegSpin_ = nullptr;
    QDoubleSpinBox* leadLenSpin_ = nullptr;
    QDoubleSpinBox* pinPitchSpin_ = nullptr;
    QComboBox* twistTypeBox_ = nullptr;
    QComboBox* shieldTypeBox_ = nullptr;
    QComboBox* wireColorModeBox_ = nullptr;
    QComboBox* colorNameModeBox_ = nullptr;
    QCheckBox* bridgeBox_ = nullptr;
    QCheckBox* terminateBox_ = nullptr;
};

} // namespace Loopwire
