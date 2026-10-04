#pragma once

#include "geometry/Environment3D.h"
#include "geometry/PartModel3D.h"
#include "harness/Harness3D.h"
#include "harness/PartsLibrary.h"

#include <QDialog>

#include <memory>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace Edraw_Desktop {

class View3DCanvas;
class NavWheel;

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
    QWidget* panel_ = nullptr;
    // A body dragged across the view, Ctrl held. Not saved.
    bool dragging_ = false;
    QPoint dragLast_;
    double dragPerPixel_ = 0.0;
};

} // namespace Edraw_Desktop
