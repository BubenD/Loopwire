#pragma once

#include "geometry/PartModel3D.h"

#include <QDialog>
#include <QPointF>

#include <optional>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QSpinBox;
class QTableWidget;
class QToolButton;

namespace Loopwire {

class SketchCanvas2D;

// A PART'S SKETCH, DRAWN FLAT: closed outlines on a plane of the model - the top, the front, the side, or a face of it
// clicked - drawn as lines, rectangles, circles, polygons and slots on a grid, snapped to their own corners and to where
// the plane cuts the model (drawn under them, with the model seen through the plane). Corners rounded, outlines
// mirrored, points typed or dragged. An outline inside another is a hole through it. Then given depth (one way or both),
// or turned round one of the plane's axes - a new solid, or added to, cut out of, or kept only where it meets another.
class Sketch2DDialog : public QDialog {
    Q_OBJECT
public:
    enum Tool { Select, Lines, Rectangle, Circle, Polygon, Slot };
    // WHAT IS MADE OF IT, and where: the plane (0 top, 1 front, 2 side - a face's plane is the side's, turned and put
    // where the face is), the outlines in the plane's two directions, and how they are given depth.
    struct Result {
        int plane = 0;
        V3 at{0, 0, 0};
        V3 turnDeg{0, 0, 0};
        std::vector<SketchLoop> loops;
        bool revolve = false;
        double depth = 5.0;
        bool both = false;
        double angle = 360.0;
        int axis = 0;
        BodyOp op = BodyOp::None;
        bool cutAll = false;   // taken out of every solid
    };

    // The model as it is (to draw under the sketch), and the solid it is combined with, if any, by its name.
    Sketch2DDialog(const std::vector<ModelItem3D>& model, const QString& targetName, QWidget* parent = nullptr);

    // ON A PLANE through the model's origin, so far along the way it is given depth; or on a face, at a point of it.
    void setPlane(int plane, double at);
    void setFace(const V3& point, const V3& normal);
    // Begun from a sketch already made: its plane, outlines and how it was given depth.
    void setStart(const Result& start);
    Result result() const;

    // DRAWING, as the pointer does it: the tool, a click at a point of the plane (snapped as the canvas snaps), lines
    // closed into an outline.
    void setTool(Tool tool);
    Tool tool() const { return tool_; }
    void clickAt(double u, double v);
    bool closeLoop();
    void cancelDrawing();
    const std::vector<SketchLoop>& loops() const { return loops_; }
    void setLoops(const std::vector<SketchLoop>& loops);
    // THE ONE SELECTED, and its corner: rounded, the outline deleted, mirrored across an axis (0 the plane's first,
    // 1 its second), one of its points moved.
    void selectLoop(int loop, int corner = -1);
    int selectedLoop() const { return selLoop_; }
    bool roundCorner(int loop, int corner, double radius);
    void deleteLoop(int loop);
    void mirrorLoop(int loop, int axis);
    void movePoint(int loop, int corner, double u, double v);
    void undo();
    // WHERE THE PLANE CUTS THE MODEL - its edges, as segments in the plane - and the model seen through it.
    const std::vector<std::pair<QPointF, QPointF>>& section() const { return section_; }
    SketchCanvas2D* canvas() const { return canvas_; }
    // A point snapped: to a corner, or an end of the section, within so far (in the plane's units), or to the grid.
    QPointF snapped(const QPointF& p, double reach) const;

private:
    friend class SketchCanvas2D;
    void remember();
    void refresh();         // the model under the sketch, for the plane now
    void changed();         // the outlines changed: the list, the points, the picture
    void fillPoints();
    void addLoop(SketchLoop loop);
    SketchLoop shapeFrom(const QPointF& a, const QPointF& b) const;
    void finishChanged();

    std::vector<ModelItem3D> model_;
    QString targetName_;
    int plane_ = 0;
    double at_ = 0.0;
    bool onFace_ = false;
    V3 facePoint_, faceTurn_;
    std::vector<SketchLoop> loops_;
    std::vector<std::vector<SketchLoop>> undo_;
    Tool tool_ = Lines;
    // Being drawn (not saved): the corners of lines so far; a two-click shape's first click; a slot's two middles.
    SketchLoop drawing_;
    std::optional<QPointF> first_;
    int selLoop_ = -1, selCorner_ = -1;
    std::vector<std::pair<QPointF, QPointF>> section_;
    std::vector<std::array<QPointF, 3>> seen_;
    bool filling_ = false;

    SketchCanvas2D* canvas_ = nullptr;
    QComboBox* planeBox_ = nullptr;
    QDoubleSpinBox* atBox_ = nullptr;
    QToolButton* toolButtons_[6] = {};
    QComboBox* gridBox_ = nullptr;
    QCheckBox* snapBox_ = nullptr;
    QSpinBox* sidesBox_ = nullptr;
    QDoubleSpinBox* slotWidth_ = nullptr;
    QDoubleSpinBox* typedU_ = nullptr;
    QDoubleSpinBox* typedV_ = nullptr;
    QDoubleSpinBox* radius_ = nullptr;
    QTableWidget* points_ = nullptr;
    QLabel* said_ = nullptr;
    QComboBox* finish_ = nullptr;
    QDoubleSpinBox* depth_ = nullptr;
    QCheckBox* both_ = nullptr;
    QDoubleSpinBox* angle_ = nullptr;
    QComboBox* axis_ = nullptr;
    QComboBox* combine_ = nullptr;
    QLabel* depthLabel_ = nullptr;
    QLabel* angleLabel_ = nullptr;
    QLabel* axisLabel_ = nullptr;
};

} // namespace Loopwire
