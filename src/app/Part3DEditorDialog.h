#pragma once

#include "app/NavWheel.h"
#include "app/Sketch2DDialog.h"
#include "geometry/Environment3D.h"
#include "geometry/PartModel3D.h"
#include "harness/Harness3D.h"
#include "harness/PartsLibrary.h"

#include <QDialog>

#include <memory>
#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QToolButton;

namespace Loopwire {

class View3DCanvas;

// BUILD A PART'S 3D MODEL: its solids put together in a 3D view of their own. Simple solids added and sized, made
// from the part's drawing and pinout, each moved, turned, scaled, mirrored, copied in a row or round, coloured, given
// what it is made of, or made a hole cut out of the others; undone and done again. Saved to the part as its 3D model -
// what the 3D view of a harness shows it as - or written out as STL or OBJ.
class Part3DEditorDialog : public QDialog {
    Q_OBJECT
public:
    Part3DEditorDialog(const LibraryPart& part, const PartModel3D& start, QWidget* parent = nullptr);

    const PartModel3D& model() const { return model_; }
    // A NEW BODY of a shape, put in the middle - or, made from the part, as the part has it - and selected.
    void addBody(BodyShape shape);
    bool autoCreateFromDrawings();
    // The one selected (-1 none), and selecting one.
    int selected() const { return selected_; }
    void select(int index);
    // THE ONE SELECTED changed - as the panel's boxes do it, one change at a time, to undo.
    void duplicateSelected();
    void deleteSelected();
    void mirrorSelected(int axis);
    void turnSelected(int axis, double degrees);
    void moveSelected(const V3& by);
    void arraySelected(int count, const V3& step, int turnAxis, double turnStep);
    void centreSelected();
    void setSelectedCut(bool on);
    void undo();
    void redo();
    // A SKETCH: an outline drawn on a plane (0 top, 1 front, 2 side) - its points as clicked, in the plane's two
    // directions (a rectangle by two corners, a circle by its middle and its edge, as the bar says) - given depth.
    void startSketch(int plane);
    void sketchClick(double u, double v);
    bool finishSketch(double depth);
    void cancelSketch();
    bool sketching() const { return sketchPlane_ >= 0; }
    // THE 2D SKETCH: drawn flat on a plane, or on a face clicked (with the face tool on), in a window of its own, then
    // made a solid - new, or combined with the one selected (the one whose face it is). A sketch selected, drawn again.
    void openSketch2D(int plane);
    void setFaceSketchTool(bool on);
    bool faceSketchTool() const { return faceSketch_; }
    void editSelectedSketch();
    // WHAT A 2D SKETCH MAKES: a solid for each outline, with its holes - combined with `target` (an id) as it says. With
    // `replace` (a body's index) the first of them is that body drawn again.
    bool addSketchResult(const Sketch2DDialog::Result& result, const QString& target, int replace = -1);
    // A sketch body as the 2D sketch begins from it.
    static Sketch2DDialog::Result sketchOf(const Body3D& body);
    // COMBINED: the one selected added to another, taken out of it, or kept only where it meets it; or a solid of its
    // own again.
    bool combineSelected(BodyOp op, int target);
    void separateSelected();
    // THE WHEEL: the view's turns, tilts, rolls and zooms - or, from its middle, the arrows that move the one selected
    // a step along each axis and turn it, each where its axis runs on the screen.
    NavWheel* wheel() const { return wheel_; }
    void setWheelMoving(bool on);
    bool wheelMoving() const { return wheelMoving_; }
    void setMoveStep(double mm);
    // PINS PUT BY HAND: the next clicks on a face put pins (1) or sockets (2) there - 0 off. Numbered in turn.
    void setPinTool(int tool);
    bool placePinAt(const V3& point, const V3& normal, bool female);
    // The pins and sockets numbered 1, 2, 3... in the order they are listed.
    void renumberPins();
    // The model as shown: its items, and the note on cuts where there is one.
    const std::vector<ModelItem3D>& items() const { return items_; }
    View3DCanvas* canvas() const { return canvas_; }

signals:
    // Save to the part asked for.
    void saveRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void changed();          // the model changed: drawn again, the list and the panel with it
    void remember();         // before a change: kept, to undo
    void fillList();
    void fillPanel();
    void fromPanel();
    void rebuild(bool fit);
    void exportModel();
    void buildMoveWheel();
    void combineMenu(BodyOp op);

    LibraryPart part_;
    PartModel3D model_;
    std::vector<PartModel3D> undo_, redo_;
    int selected_ = -1;
    bool filling_ = false;
    std::vector<ModelItem3D> items_;
    Harness3DScene scene_;
    std::vector<const Mesh3D*> pickMeshes_;
    std::vector<int> pickBody_;   // the body each picked mesh is
    RayIndex3D pickIndex_;
    View3DCanvas* canvas_ = nullptr;
    NavWheel* wheel_ = nullptr;
    QListWidget* list_ = nullptr;
    QLabel* said_ = nullptr;
    QLineEdit* name_ = nullptr;
    QLabel* shapeSaid_ = nullptr;
    QLabel* sizeLabel_[3] = {};
    QDoubleSpinBox* size_[3] = {};
    QDoubleSpinBox* at_[3] = {};
    QDoubleSpinBox* turn_[3] = {};
    QDoubleSpinBox* scale_[3] = {};
    QPushButton* color_ = nullptr;
    QComboBox* material_ = nullptr;
    QCheckBox* cut_ = nullptr;
    QCheckBox* shown_ = nullptr;
    QDoubleSpinBox* fillet_ = nullptr;
    QDoubleSpinBox* chamfer_ = nullptr;
    QLabel* filletLabel_ = nullptr;
    QLabel* chamferLabel_ = nullptr;
    QCheckBox* female_ = nullptr;
    QSpinBox* number_ = nullptr;
    QLabel* numberLabel_ = nullptr;
    // A SKETCH: both ways, the axis a revolved one turns round, drawn again. COMBINED: how, and with which.
    QCheckBox* both_ = nullptr;
    QComboBox* axis_ = nullptr;
    QLabel* axisLabel_ = nullptr;
    QPushButton* editSketch_ = nullptr;
    QComboBox* op_ = nullptr;
    QComboBox* target_ = nullptr;
    QLabel* targetLabel_ = nullptr;
    // The wheel's two rings (not saved): the view's, and the arrows that move the one selected; each step.
    std::vector<NavWheel::Item> viewWheel_;
    bool wheelMoving_ = false;
    double wheelTurn_ = 15.0;
    QDoubleSpinBox* moveStep_ = nullptr;
    // Sketch on the face clicked (not saved), and its button.
    bool faceSketch_ = false;
    QToolButton* faceSketchButton_ = nullptr;
    // The sketch being drawn (not saved): its plane (-1 none), where the plane is, its points, a rectangle's or a
    // circle's first click; the bar it is drawn with.
    int sketchPlane_ = -1;
    double sketchAt_ = 0.0;
    std::vector<std::pair<double, double>> sketch_;
    std::optional<std::pair<double, double>> sketchFirst_;
    QWidget* sketchBar_ = nullptr;
    QComboBox* sketchPlaneBox_ = nullptr;
    QDoubleSpinBox* sketchAtBox_ = nullptr;
    QComboBox* sketchShape_ = nullptr;
    QDoubleSpinBox* sketchDepth_ = nullptr;
    // Pins put by hand: the tool (0 off, 1 pins, 2 sockets) and its two buttons.
    int pinTool_ = 0;
    QToolButton* pinButtons_[2] = {};
    QWidget* panel_ = nullptr;
    // A body dragged across the view, Ctrl held. Not saved.
    bool dragging_ = false;
    QPoint dragLast_;
    double dragPerPixel_ = 0.0;
};

} // namespace Loopwire
