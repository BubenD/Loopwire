#pragma once

#include "harness/MechModels.h"

#include <QDialog>

#include <functional>

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QFormLayout;
class QDoubleSpinBox;
class QGroupBox;
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
    // onEdit is handed the kind AND which model of it is showing: a kind holds several, and editing the
    // right-angled backshell has to open that one rather than whichever the kind happens to default to.
    explicit MechModelsDialog(Editor* editor, QWidget* parent = nullptr,
                              std::function<void(MechModelKind, QString)> onEdit = {});

    // The models as they stand, and which one is showing.
    const MechModelSet& models() const { return set_; }
    void setModels(const MechModelSet& set);
    MechModelKind currentKind() const;
    // Which MODEL of that kind is showing: a kind can have several - a straight backshell and a right-angled
    // one - and the list has a row per model rather than a row per kind.
    QString currentName() const;
    bool selectKind(MechModelKind kind);
    // Driving it from a test: change a figure, ask whether there is anything unsaved, and save. The three things a
    // test of "nothing is written until Save" has to be able to do.
    void setHeadForTest(double mm);
    bool hasUnsavedForTest() const { return changed_; }
    bool saveForTest() { return saveNow(); }
    // Managing them: another model of this kind, this one gone, renamed, or made the one the kind uses.
    void addModel(const QString& name);
    void removeCurrentModel();
    void renameCurrentModel(const QString& to);
    void makeCurrentDefault();

    // Driving it: take what is selected on the sheet as this model's drawing.
    void captureFromSheet();
    // Armed capture: the window steps aside, the objects are picked on the sheet, and Enter takes them.
    // Taking the whole sheet was the only way before, so a model could not be drawn beside anything else -
    // not even beside the thing it goes on, which is exactly how it wants to be drawn.
    void finishCaptureFromSheet();
    void cancelCaptureFromSheet();
    void captureSelection();   // what is selected, as this model's drawing
    // Open it in the main document canvas, where the full Place and Modify ribbon is available.
    void editOnCanvas();

signals:
    // The sheet is wanted for picking: the window has hidden itself, and the ribbon says what to do and
    // how to finish - the same as the parts library does when it captures a part's drawing.
    void captureArmed(const QString& modelName);
    void captureFinished();

protected:
    // Asked about on the way out when the file does not match what is on the screen. Losing an afternoon of house
    // style to a stray click on Close is not a thing that should be possible.
    void closeEvent(QCloseEvent* e) override;

private:
    bool capturing_ = false;   // waiting for a selection on the sheet
    void buildUi();
    void refreshList();
    void refreshDetails();
    void refreshPreview();
    void applyFigures();          // the fields written back into the model showing
    MechModel* current();

    Editor* editor_ = nullptr;
    std::function<void(MechModelKind, QString)> onEdit_;
    MechModelSet set_;
    std::vector<MechModelKind> kinds_;
    // One entry per ROW of the list: which kind, and which model of it ("" = the kind's first).
    struct Row { MechModelKind kind; QString name; };
    std::vector<Row> rows_;
    bool syncing_ = false;

    QListWidget* list_ = nullptr;
    QLabel* heading_ = nullptr;
    QLabel* hint_ = nullptr;
    QLabel* summary_ = nullptr;
    // ONE drawing window, and it shows the model: as drawn, or at the length it is being tried at. Two of
    // them side by side halved the size of both and showed the same drawing twice for every model that is
    // not stretched at all - which is most of them.
    DrawingPreview* view_ = nullptr;

    QComboBox* insulation_ = nullptr;
    bool pendingInsulated_ = true;
    // What the CAPTURE measured off the drawing - its length and its diameter - said rather than typed.
    // They were two fields with a magic zero in each ("0 = measure it", "0 = do not scale across"), which is
    // two ways of saying a thing the drawing already knows.
    QLabel* measured_ = nullptr;
    QCheckBox* scaleAcross_ = nullptr;
    // The marks drawn over the preview: what the model is placed BY. Asked here so "has this one been marked up,
    // and where" can be answered by looking rather than by opening it for editing to find out.
    QCheckBox* showMarks_ = nullptr;
    // NOTHING IS WRITTEN UNTIL SAVE IS PRESSED. Every change used to go straight to the file - a digit typed into a
    // length wrote the whole set to disk - so there was no way to try something and think better of it. These are
    // the drawings every sheet the office releases is built from, and editing them should be as safe as editing
    // anything else.
    QPushButton* saveBtn_ = nullptr;
    bool changed_ = false;
    void markChanged();
    bool saveNow();
    // Where it connects and which way it runs: marked on the drawing, said here.
    QLabel* placement_ = nullptr;
    QGroupBox* figures_ = nullptr;
    QGroupBox* tryBox_ = nullptr;
    QFormLayout* figuresForm_ = nullptr;
    QFormLayout* tryForm_ = nullptr;
    QDoubleSpinBox* gap_ = nullptr;       // cable and wire: the piece of the run they do not cover
    // The labels: how far past the first and last run crossed the label goes, at each end.
    QDoubleSpinBox* exceed_ = nullptr;
    QDoubleSpinBox* head_ = nullptr;
    QDoubleSpinBox* tail_ = nullptr;
    QSpinBox* wires_ = nullptr;
    QDoubleSpinBox* tryLen_ = nullptr;
    QSpinBox* tryWires_ = nullptr;
    QPushButton* captureBtn_ = nullptr;
    QPushButton* removeBtn_ = nullptr;
    QPushButton* renameBtn_ = nullptr;
    QPushButton* defaultBtn_ = nullptr;
};

} // namespace Edraw_Desktop
