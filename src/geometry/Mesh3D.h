#pragma once

#include <QString>

#include <array>
#include <functional>
#include <cmath>
#include <vector>

namespace Edraw_Desktop {

// A POINT OR A DIRECTION IN SPACE. The harness lies in the XY plane of the drawing, Z standing up out of the sheet.
struct V3 {
    double x = 0.0, y = 0.0, z = 0.0;
    V3() = default;
    V3(double x_, double y_, double z_) : x(x_), y(y_), z(z_) {}
    V3 operator+(const V3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    V3 operator-(const V3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    V3 operator*(double k) const { return {x * k, y * k, z * k}; }
    V3 operator/(double k) const { return {x / k, y / k, z / k}; }
    double length() const { return std::sqrt(x * x + y * y + z * z); }
    V3 normalized() const {
        const double l = length();
        return l > 1e-12 ? V3{x / l, y / l, z / l} : V3{0.0, 0.0, 0.0};
    }
};
inline double dot3(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross3(const V3& a, const V3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }

// A TRIANGLE MESH: corners, and triangles by three corners each.
struct Mesh3D {
    std::vector<V3> v;
    std::vector<std::array<int, 3>> f;
    bool empty() const { return f.empty(); }
    // Its box, lo and hi corners; false when it has nothing in it.
    bool bounds(V3& lo, V3& hi) const;
    // Another mesh added to this one.
    void append(const Mesh3D& o);
    int addVertex(const V3& p) {
        v.push_back(p);
        return static_cast<int>(v.size()) - 1;
    }
    void addTri(int a, int b, int c) { f.push_back({a, b, c}); }
};

// A box from one corner to the other.
Mesh3D boxMesh(const V3& lo, const V3& hi);
// A PROFILE GIVEN DEPTH: a closed outline in X and Y (either way round, not crossing itself), drawn up Z from 0 to
// `depth` - its two ends filled.
Mesh3D prismMesh(const std::vector<std::pair<double, double>>& profile, double depth);
// A TUBE along a path - a round section of so many sides swept along it, closed at its ends.
Mesh3D tubeMesh(const std::vector<V3>& path, double radius, int sides = 12, bool caps = true);
// A TUBE WHOSE RADIUS CHANGES along it - by how far along the path (its length so far) each point is.
Mesh3D tubeMeshVar(const std::vector<V3>& path, const std::function<double(double along)>& radius, int sides = 12,
                   bool caps = true);

// ---- Reading a 3D model ------------------------------------------------------------------------
//
// STL (text or binary) and OBJ whole. STEP by its boundary: every face whose surface is a plane, a cylinder or a cone
// is tessellated from the edges that bound it (lines, circles and arcs, splines by their control points); a face on
// any other surface is filled in from its boundary. `how` says what was read, or why nothing was.
Mesh3D loadStl(const QString& path, QString* how = nullptr);
Mesh3D loadObj(const QString& path, QString* how = nullptr);
Mesh3D loadStep(const QString& path, QString* how = nullptr);
Mesh3D loadStepText(const QByteArray& text, QString* how = nullptr);
// By the file's extension: .stl, .obj, .stp / .step.
Mesh3D loadMesh3D(const QString& path, QString* how = nullptr);
// The model files that can be attached to a part, for a file dialog.
QString mesh3DFileFilter();

} // namespace Edraw_Desktop
