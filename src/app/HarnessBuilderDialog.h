#pragma once

#include "harness/HarnessBuilder.h"

#include <QDialog>
#include <QJsonObject>
#include <QList>
#include <QSet>
#include <QString>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QHBoxLayout;
class QKeyEvent;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QSplitter;
class QTableWidget;
class QTextBrowser;
class QVBoxLayout;

namespace Edraw_Desktop {

class Editor;
// Drawn previews used inside the dialog (defined in the .cpp).
class ConnectorPreview;
class HarnessWiringPreview;

// The Harness Builder: describe a whole harness once — connectors, their order and side,
// the pinout, every pin-to-pin wire, which wires are shielded or twisted, and where it all
// sits — then Generate draws the schematic in one step.
//
// The dialog is a thin editor over a HarnessDef; all the geometry, validation and crossing
// work lives in harness/HarnessBuilder so it can be tested without a UI. Every control
// writes into def_ and asks for a re-sync, so the tables, the block map and the pin map can
// never drift apart from the definition they show.
class HarnessBuilderDialog : public QDialog {
    Q_OBJECT
public:
    explicit HarnessBuilderDialog(Editor* editor, QWidget* parent = nullptr);

    // Place a CABLE from the library: one click gives the harness every conductor the cable has, with
    // its colours, cross-sections, twisted groups and screens already set. `libPartId` empty asks
    // which cable. Returns how many conductors were added.
    int addCableFromLibrary(const QString& libPartId = QString());

    // What the builder currently describes. Read-only: everything that changes a harness goes
    // through the dialog's own controls, so the definition and the tables can never disagree.
    const HarnessDef& definition() const { return def_; }

signals:
    // "See it in the library": the host owns the library window, so it is asked to open it at the part.
    void openLibraryAt(const QString& libPartId);

protected:
    void keyPressEvent(QKeyEvent* e) override;
    // The libraries are read again every time this window is opened, so a part added since it was
    // last used is there to be picked.
    void showEvent(QShowEvent* e) override;

private:
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
    // The part menu for a row, and the wiring that lets a right-click ON A CELL reach it.
    void showWireRowMenu(const QString& wireId, const QPoint& globalPos);
    void armRowMenu(int row, const QString& wireId);
    void armCellMenu(QWidget* cell, const QString& wireId);
    void reloadLayout();
    // Give each section the height its contents need, so nothing has to be dragged into view.
    void spreadSections();
    void showIssues(const std::vector<HarnessIssue>& issues, bool sayWhenClean);

    // --- Actions ---
    void addConnector(HarnessSide side);
    void removeConnector(int row);
    // Right-clicking anything that names a library part: look at it, or open the library at it. The
    // same menu wherever a part is named, because the question is the same.
    void showPartMenu(const QString& libPartId, const QPoint& at, const QString& what);
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
    // Unify is a two-step: a button arms a pick, Enter applies it, Esc abandons it. Rows are
    // only selectable while one is pending, so nothing can be picked by accident.
    void armUnify(const QString& kind); // "cable" | "coax" | "coax-splice" | "loose"
    // Turns a click during a pick into a row toggle (see setRowsPickable).
    bool eventFilter(QObject* watched, QEvent* ev) override;
    // While a pick is armed the rows' cell widgets let the mouse through, so one click picks a row.
    void setRowsPickable(bool pickable);
    void applyUnify();
    void cancelUnify();
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
    QStringList connectorChoices() const;
    // Every endpoint a wire end can name - connectors and shields - as (key, label) pairs.
    QList<QPair<QString, QString>> endpointChoices() const;
    static QStringList gaugeChoices();
    static QStringList colorChoices();
    // Guard: while a reload is writing the widgets, their signals must not be read back
    // as user edits.
    bool syncing_ = false;

    Editor* editor_ = nullptr;
    HarnessDef def_;
    int connSeq_ = 0;
    int wireSeq_ = 0;

    // The six section frames, in the order they appear, for spreadSections().
    QList<QWidget*> sections_;
    QTableWidget* connTable_ = nullptr;
    QListWidget* leftBlocks_ = nullptr;
    QListWidget* rightBlocks_ = nullptr;
    QWidget* pinMapHost_ = nullptr;
    // The whole harness drawn small beside the pin tables.
    HarnessWiringPreview* wiringPreview_ = nullptr;
    QHBoxLayout* pinMapLayout_ = nullptr;
    QTableWidget* wireTable_ = nullptr;
    // Outlines the picked rows (their cells are widgets, so the selection colour never shows).
    class PickOverlay* pickOverlay_ = nullptr;
    // How the wire rows are grouped, and which groups are folded. A long wire list is far
    // easier to work through by shield, by twisted group or by the connector pair it joins,
    // with the groups you are not touching collapsed out of the way. The fold state is kept
    // across reloads (every field edit rebuilds the table) so editing never springs a
    // collapsed group back open.
    enum class WireGrouping { None, Shield, Twist, Connectors };
    // Grouped by the connector pair by default: that is how a harness is read - everything
    // running from P1 to P2, then P1 to P3 - and the groups follow the placement order.
    WireGrouping grouping_ = WireGrouping::Connectors;
    QSet<QString> collapsedGroups_;
    // The kind of unify waiting for a pick ("" when none is).
    QString pendingUnify_;
    // True while an Add wire is waiting for a group header to be clicked.
    bool pendingAddGroup_ = false;
    QList<QPushButton*> unifyButtons_;
    QPushButton* finishUnifyBtn_ = nullptr;
    QComboBox* groupByBox_ = nullptr;
    QComboBox* allGaugeBox_ = nullptr;
    // All / Clr / AWG / Lbl for the whole Show-on-wire column.
    QList<QCheckBox*> showColumnBoxes_;
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

} // namespace Edraw_Desktop
