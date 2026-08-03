#pragma once

#include "analysis/VoltageDrop.h"

#include <QDialog>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QTableWidget;

namespace Edraw_Desktop {

class Editor;

// Voltage drop across the harness that is drawn: pick where the supply goes in, say what the load
// draws, and read what arrives - with the drop broken down wire by wire, contact by contact.
//
// The window is modeless and re-reads the drawing every time it is run, so it follows edits rather
// than holding a stale copy. Anything the drawing does not state (a wire with no gauge, a pin with
// no contact size) is listed as a gap with a box to type the figure into, so the calculation can be
// finished without leaving the panel.
class VoltageDropDialog : public QDialog {
    Q_OBJECT
public:
    explicit VoltageDropDialog(Editor* editor, QWidget* parent = nullptr);

    // Re-read the drawing and refill the pickers (called when the window is shown again).
    void reloadFromDocument();
    void run();
    const VdReport& report() const { return report_; }
    // Testing / driving: choose the ends by node id.
    bool setSource(const QString& nodeId);
    bool setLoad(const QString& nodeId);
    void setSourceVoltage(double v);
    void setLoadCurrent(double a);
    bool exportCsv(const QString& path) const;

private:
    void buildUi();
    void refreshResults();
    void refreshOverrideTable();
    VdRequest buildRequest() const;

    Editor* editor_ = nullptr;
    VdGraph graph_;
    VdReport report_;
    bool syncing_ = false;

    QComboBox* sourceBox_ = nullptr;
    QComboBox* loadBox_ = nullptr;
    QComboBox* returnBox_ = nullptr;
    QComboBox* loadReturnBox_ = nullptr;   // the pin the load's own current leaves by
    QComboBox* returnKindBox_ = nullptr;
    QComboBox* caseBox_ = nullptr;
    QDoubleSpinBox* voltsSpin_ = nullptr;
    QDoubleSpinBox* ampsSpin_ = nullptr;
    QDoubleSpinBox* limitSpin_ = nullptr;
    QDoubleSpinBox* ambientSpin_ = nullptr;
    QDoubleSpinBox* riseSpin_ = nullptr;
    QTableWidget* rows_ = nullptr;
    QTableWidget* overrides_ = nullptr;   // per-interface ohms, and the gaps to fill in
    QLabel* summary_ = nullptr;
    QLabel* totals_ = nullptr;
    QPushButton* highlightBtn_ = nullptr;

    // What the user has typed for a contact or a joint, by node id.
    QHash<QString, double> contactOverrides_;
    QHash<QString, double> jointOverrides_;
};

} // namespace Edraw_Desktop
