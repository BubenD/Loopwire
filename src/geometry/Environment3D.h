#pragma once

#include "geometry/Mesh3D.h"
#include <functional>

#include <QColor>
#include <QString>

#include <vector>

namespace Edraw_Desktop {

// ONE SOLID OF THE SPACE A HARNESS IS ROUTED IN: its name in the model and its colour, if the model gives them.
struct EnvPart3D {
    QString name;
    QColor color;
    Mesh3D mesh;
};

// THE SPACE THE HARNESS IS ROUTED IN: a chassis, a panel or an enclosure, loaded from its CAD model - a STEP assembly
// solid by solid, or one STL or OBJ mesh. In the model's own coordinates, in millimetres.
struct Environment3D {
    QString path;
    std::vector<EnvPart3D> parts;
    // What was read and how long it took, or why nothing was.
    QString how;
    bool empty() const;
    size_t triangles() const;
    bool bounds(V3& lo, V3& hi) const;
};

// HOW FAR A READING HAS GOT, 0..100, and what it is doing: for a progress bar. Called from the thread that reads.
using Load3DProgress = std::function<void(int percent, const QString& stage)>;

// Read from a file by its extension. A STEP file through Open CASCADE where this build has it - every surface, the
// assembly's solids with their names and colours - and through Edraw's own reader where it has not.
Environment3D loadEnvironment3D(const QString& path, const Load3DProgress& progress = {});
QString environment3DFileFilter();
// Whether STEP files are read whole, through Open CASCADE.
bool fullStepReader();

// ---- Picking --------------------------------------------------------------------------------------

// WHERE A RAY MEETS A MODEL: the point, the face's normal turned to face back along the ray, which part, and how far.
struct Pick3D {
    bool hit = false;
    V3 point;
    V3 normal;
    int part = -1;
    double distance = 0.0;
};

// THE TRIANGLES OF SEVERAL MESHES IN A TREE OF BOXES, so a ray finds the first one it meets without trying them all.
class RayIndex3D {
public:
    void build(const std::vector<const Mesh3D*>& meshes);
    void clear();
    bool empty() const { return tris_.empty(); }
    // The nearest triangle the ray from `origin` along `dir` meets in front of it - passing those `skip` says to (by
    // their part and where it would be met): a solid hidden, what a section cuts away.
    using Skip3D = std::function<bool(int part, const V3& point)>;
    Pick3D cast(const V3& origin, const V3& dir, const Skip3D& skip = {}) const;

private:
    struct Tri {
        V3 a, b, c;
        int part;
    };
    struct Node {
        V3 lo, hi;
        int first = 0, count = 0;  // a leaf: its triangles; otherwise its children are left and left + 1
        int left = -1;
    };
    std::vector<Tri> tris_;
    std::vector<Node> nodes_;
};

} // namespace Edraw_Desktop
