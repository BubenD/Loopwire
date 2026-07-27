#pragma once

#include "harness/HarnessBuilder.h"

#include <QDialog>
#include <QJsonObject>
#include <QSet>
#include <QString>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QHBoxLayout;
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
    void reloadWires();
    // Build the editable cells of one wire onto table row `row`. Split out of reloadWires
    // so the grouped layout can interleave group headers between the wire rows.
    void buildWireRow(int row, const HarnessWire& w, const QStringList& connChoices);
    // Insert a spanning, clickable header for a group of wires; returns nothing but hides
    // its members when the group is folded.
    void buildGroupHeader(int row, const QString& key, const QString& title, int memberCount);
    void reloadLayout();
    void showIssues(const std::vector<HarnessIssue>& issues, bool sayWhenClean);

    // --- Actions ---
    void addConnector(HarnessSide side);
    void removeConnector(int row);
    void addWire();
    void removeWire(int row);
    void autoFillPins();     // give every wire the next free pin at each end
    void applyBlockOrder();  // read the dragged block lists back into `order` / `leg`
    void applyPinOrder();    // read the dragged pin lists back into each pinOrder
    void runValidate();
    void runMinimizeCrossings();
    // Targeted pinout optimisation: gather the shielded groups (shield=true) or the
    // twisted groups onto neighbouring pins.
    void runOptimizeBy(bool shield);
    void generate();
    void saveDefinition();
    void loadDefinition();
    // The definition as a document, and back: the file format and the in-memory model in one
    // place, so Save and Load cannot drift apart.
    QJsonObject definitionToJson() const;
    void applyDefinitionJson(const QJsonObject& root);

    // Helpers.
    QString uniqueConnectorId() const;
    QString uniqueWireId() const;
    HarnessConnector* connById(const QString& id);
    QStringList connectorChoices() const;   // "P1 (left)" style entries, in table order
    static QStringList gaugeChoices();
    static QStringList colorChoices();
    // Guard: while a reload is writing the widgets, their signals must not be read back
    // as user edits.
    bool syncing_ = false;

    Editor* editor_ = nullptr;
    HarnessDef def_;
    int connSeq_ = 0;
    int wireSeq_ = 0;

    QTableWidget* connTable_ = nullptr;
    QListWidget* leftBlocks_ = nullptr;
    QListWidget* rightBlocks_ = nullptr;
    QWidget* pinMapHost_ = nullptr;
    // The whole harness drawn small beside the pin tables.
    HarnessWiringPreview* wiringPreview_ = nullptr;
    QHBoxLayout* pinMapLayout_ = nullptr;
    QTableWidget* wireTable_ = nullptr;
    // How the wire rows are grouped, and which groups are folded. A long wire list is far
    // easier to work through by shield, by twisted group or by the connector pair it joins,
    // with the groups you are not touching collapsed out of the way. The fold state is kept
    // across reloads (every field edit rebuilds the table) so editing never springs a
    // collapsed group back open.
    enum class WireGrouping { None, Shield, Twist, Connectors };
    WireGrouping grouping_ = WireGrouping::None;
    QSet<QString> collapsedGroups_;
    QComboBox* groupByBox_ = nullptr;
    QComboBox* allGaugeBox_ = nullptr;
    QTextBrowser* issueView_ = nullptr;
    QLabel* summaryLabel_ = nullptr;

    QDoubleSpinBox* leftXSpin_ = nullptr;
    QDoubleSpinBox* leftYSpin_ = nullptr;
    QDoubleSpinBox* rightXSpin_ = nullptr;
    QDoubleSpinBox* gapSpin_ = nullptr;
    QDoubleSpinBox* legGapSpin_ = nullptr;
    QSpinBox* twistCountSpin_ = nullptr;
    QSpinBox* twistAmountSpin_ = nullptr;
    QSpinBox* shieldAmountSpin_ = nullptr;
    QSpinBox* wireShieldAmountSpin_ = nullptr;
    QDoubleSpinBox* shieldLegSpin_ = nullptr;
    QDoubleSpinBox* pinPitchSpin_ = nullptr;
    QComboBox* twistTypeBox_ = nullptr;
    QComboBox* shieldTypeBox_ = nullptr;
    QComboBox* wireColorModeBox_ = nullptr;
    QCheckBox* bridgeBox_ = nullptr;
    QCheckBox* terminateBox_ = nullptr;
};

} // namespace Edraw_Desktop
