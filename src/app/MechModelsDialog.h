#pragma once

#include "harness/MechModels.h"

#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;

namespace Edraw_Desktop {

class DrawingPreview;
class Editor;

// The default mechanical models: the house style of a drawing office, drawn once.
//
// A part in the library is a thing you buy and its drawing is its own. These are the other half - the
// pieces that are a KIND of thing rather than a part number: a length of wire, a cable, heatshrink over
// a transition, the wires between a connector and the cable they come out of, a ring terminal, a
// ferrule, a boot, a Y-split. Every drawing an office releases draws those the same way, and this is
// where that way is decided.
//
// Each is drawn on the sheet with the ordinary tools and captured here, which is what makes this a
// drawing tool rather than a form: whatever can be drawn can be a model.
//
// Length is not part of a model. A wire is drawn at whatever length is convenient, and the harness says
// how long the wire actually is; so a model carries the two ends that must NOT stretch - the flush end
// of a wire, the crimp of a ferrule, the taper of a boot - and everything between them makes up the
// difference. The window shows it stretched to a length you can type, because a model that only looks
// right at the length it was drawn at is no use.
class MechModelsDialog : public QDialog {
    Q_OBJECT
public:
    explicit MechModelsDialog(Editor* editor, QWidget* parent = nullptr);

    // The models as they stand, and which one is showing.
    const MechModelSet& models() const { return set_; }
    void setModels(const MechModelSet& set);
    MechModelKind currentKind() const;
    bool selectKind(MechModelKind kind);

    // Driving it: take what is selected on the sheet as this model's drawing, put the model on the
    // sheet to be worked on, and clear it.
    void captureFromSheet();
    void insertIntoDrawing();
    void clearModel();
    // Draw it on a canvas of its own: the same canvas the application draws on, over a scratch drawing
    // that holds nothing else, with the ends that must not stretch marked on it while you work.
    void editOnCanvas();

private:
    void buildUi();
    void refreshList();
    void refreshDetails();
    void refreshPreview();
    void applyFigures();          // the fields written back into the model showing
    MechModel* current();

    Editor* editor_ = nullptr;
    MechModelSet set_;
    std::vector<MechModelKind> kinds_;
    bool syncing_ = false;

    QListWidget* list_ = nullptr;
    QLabel* heading_ = nullptr;
    QLabel* hint_ = nullptr;
    QLabel* summary_ = nullptr;
    DrawingPreview* asDrawn_ = nullptr;
    DrawingPreview* stretched_ = nullptr;

    QDoubleSpinBox* drawnLen_ = nullptr;
    QDoubleSpinBox* head_ = nullptr;
    QDoubleSpinBox* tail_ = nullptr;
    QDoubleSpinBox* across_ = nullptr;
    QSpinBox* wires_ = nullptr;
    QDoubleSpinBox* tryLen_ = nullptr;
    QSpinBox* tryWires_ = nullptr;
    QPushButton* captureBtn_ = nullptr;
};

} // namespace Edraw_Desktop
