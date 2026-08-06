#pragma once

#include "harness/MechanicalHarness.h"
#include "harness/PartsLibrary.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace Edraw_Desktop {

class DrawingPreview;
class Editor;

// The mechanical builder: the harness as a physical object - a trunk that branches at breakouts,
// with a connector or a cut end at every tip - described here and generated to scale.
//
// What separates it from drawing the thing by hand is that the SIZE is not typed in. Each run is
// drawn at the bundle its own conductors make, so changing a gauge changes the drawing, and a boot
// that no longer goes over the bundle is reported rather than drawn as if it fitted. The preview is
// the real renderer on the real entities, so what is shown is what will be placed.
class MechanicalBuilderDialog : public QDialog {
    Q_OBJECT
public:
    explicit MechanicalBuilderDialog(Editor* editor, QWidget* parent = nullptr);

    // Read a first definition off the schematic that is drawn: connectors become branches and the
    // wires are routed into them.
    void readFromDrawing();
    // Generate and hand to the editor to be placed by a click.
    void generate();
    // Testing / driving.
    const MechHarnessDef& definition() const { return def_; }
    void setDefinition(const MechHarnessDef& def);
    QString selectedNodeId() const { return selected_; }
    bool selectNode(const QString& nodeId);
    void addBranch(MechNodeKind kind);
    void removeSelected();
    // The things every connector end has one of or none of - a backshell, a boot, braid over the bare
    // wires, heatshrink over that - as a tick rather than as a trip to the library. Ticking one adds a
    // fitting of that kind with nothing but its kind stated, so it is drawn to what it goes over;
    // choosing a real part for it afterwards is the same list of fittings either way.
    bool hasEndFitting(PartKind kind) const;
    void setEndFitting(PartKind kind, bool on);

private:
    void buildUi();
    void refreshTree();
    void refreshDetails();
    void refreshPreview();
    void refreshIssues();
    // The bill of materials of the harness, with the balloon number of every line - so the number on
    // the drawing and the row it points at are read side by side rather than taken on trust.
    void refreshBom();
    void refreshAll();
    // The node being edited, and the run arriving at it.
    MechNode* currentNode();
    MechSegment* currentSegment();
    // The run AT the selected point, which at the root is the single run leaving it rather than none -
    // so a connector at the start of a trunk has a cable, a jacket and a stripped length like any
    // other. `atToEnd` says which of the run's two ends this point is.
    MechSegment* currentRun(bool* atToEnd = nullptr);
    void readOptionsFromControls();
    void writeOptionsToControls();
    void refreshFittingList();
    void addFitting();
    void removeFitting();
    // Where a fitting of this kind goes at the selected point: at the END - a backshell, a boot, and a
    // braid or heatshrink when there are bare wires there to cover - or ALONG the run.
    bool fittingGoesAtEnd(PartKind kind) const;
    // The library parts that could be fitted here, with the ones that will not fit marked.
    void refreshFittingChoices();

    Editor* editor_ = nullptr;
    // Its own view of the connected libraries, the way every other window that reads parts has one.
    PartsCatalog catalog_;
    MechHarnessDef def_;
    QString selected_;
    bool syncing_ = false;

    QLineEdit* nameEdit_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    DrawingPreview* preview_ = nullptr;

    // The selected node and its run.
    QLineEdit* nodeName_ = nullptr;
    QComboBox* nodeKind_ = nullptr;
    QComboBox* connPart_ = nullptr;
    QDoubleSpinBox* lengthSpin_ = nullptr;
    QDoubleSpinBox* angleSpin_ = nullptr;
    QLabel* bundleLabel_ = nullptr;
    // The run as a CABLE: which cable it is, what the jacket measures over, and how far back the
    // jacket is cut at this end so the conductors can reach the contacts.
    QComboBox* cablePart_ = nullptr;
    QDoubleSpinBox* jacketSpin_ = nullptr;
    QDoubleSpinBox* openSpin_ = nullptr;       // wires open behind the connector
    QDoubleSpinBox* stripSpin_ = nullptr;      // at the selected end
    QDoubleSpinBox* stripFarSpin_ = nullptr;   // and at the far end of the same run
    // What is fitted at this end, as ticks.
    QCheckBox* backshellChk_ = nullptr;
    QCheckBox* bootChk_ = nullptr;
    QCheckBox* braidChk_ = nullptr;
    QCheckBox* shrinkChk_ = nullptr;
    QListWidget* wireList_ = nullptr;
    QListWidget* fittingList_ = nullptr;
    QComboBox* fittingChoice_ = nullptr;
    QCheckBox* onlyFits_ = nullptr;
    QPushButton* addFittingBtn_ = nullptr;

    // Options.
    QDoubleSpinBox* scaleSpin_ = nullptr;
    QDoubleSpinBox* factorSpin_ = nullptr;
    QDoubleSpinBox* exagSpin_ = nullptr;
    QDoubleSpinBox* textSpin_ = nullptr;
    QCheckBox* dimsChk_ = nullptr;
    QCheckBox* diaChk_ = nullptr;
    QCheckBox* countChk_ = nullptr;
    QCheckBox* balloonChk_ = nullptr;
    QCheckBox* bodiesChk_ = nullptr;
    QCheckBox* spliceChk_ = nullptr;

    QTableWidget* bomTable_ = nullptr;
    QCheckBox* notesChk_ = nullptr;
    QListWidget* issues_ = nullptr;
    QLabel* summary_ = nullptr;
};

} // namespace Edraw_Desktop
