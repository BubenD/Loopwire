#pragma once

#include "harness/MechModels.h"
#include "model/Document.h"

#include <QDialog>
#include <QJsonArray>

class QLabel;
class QToolButton;

namespace Loopwire {

class CanvasWidget;
class Editor;

// A canvas of its own, to draw a default model on.
//
// A model could be drawn on the sheet and captured from it, and that works, but it is the wrong place:
// the sheet is a drawing of a harness, and what is being drawn here is a piece of the vocabulary the
// harness drawings are written in. Mixing the two means finding somewhere clear on a drawing to work,
// remembering to delete it afterwards, and capturing the right objects.
//
// So the model gets a canvas of its own - the same canvas the application draws on, with the same
// tools, over a scratch drawing that holds nothing else. Lines, polylines, rectangles, circles and arcs
// are enough to draw any of these pieces, and they are on the strip above it; everything else the
// canvas can do - snapping, the grid, zoom, undo - is there because it is the same canvas.
//
// The two ends that must not stretch are shown while drawing, as vertical lines at the head and tail
// distances, so a boot's taper or a wire's flush end can be drawn UP TO the mark rather than measured
// afterwards.
class MechModelCanvasDialog : public QDialog {
    Q_OBJECT
public:
    // Opens on `model` - its drawing to be changed, or empty to draw one from nothing.
    MechModelCanvasDialog(const MechModel& model, QWidget* parent = nullptr);
    ~MechModelCanvasDialog() override;

    // The drawing as it stands on the canvas, in the model's own coordinates: shifted so it starts at
    // x = 0 and is centred across, which is how a model is stored and what makes the stretch figures
    // mean the same thing every time.
    QJsonArray drawing() const;
    // How long and how wide what is drawn measures, for the window that owns the figures.
    double drawnLengthMm() const;
    double drawnAcrossMm() const;

    // Driving it: which tool the strip has armed.
    void useTool(int toolId);

private:
    void buildUi();
    void refreshGuides();
    void refreshMeasure();

    MechModel model_;
    Document doc_;
    Editor* editor_ = nullptr;
    CanvasWidget* canvas_ = nullptr;
    QLabel* measure_ = nullptr;
    QLabel* hint_ = nullptr;
};

} // namespace Loopwire
