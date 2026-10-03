#pragma once

#include "geometry/Mesh3D.h"

#include <QColor>
#include <QImage>

#include <vector>

namespace Edraw_Desktop {

// THE CAMERA OF THE 3D VIEW: turned round a point it looks at - round the upright (yaw) and up and down (pitch) - from
// so far away, with so wide a lens.
struct Camera3D {
    V3 target;
    double yawDeg = -55.0;
    double pitchDeg = 35.0;
    double distance = 500.0;
    double fovDeg = 35.0;
    // Where it stands, and its three directions: right, up, and the way it looks.
    V3 eye() const;
    void axes(V3& right, V3& up, V3& forward) const;
    // Brought round to show the whole of a box, from where it looks now, filling a picture this wide for its height.
    void fit(const V3& lo, const V3& hi, double aspect = 1.6);
};

// One mesh to draw, in its colour.
struct DrawItem3D {
    const Mesh3D* mesh = nullptr;
    QColor color;
};

// THE PICTURE: every triangle of every item, nearest in front (a depth buffer), each lit by how squarely it faces the
// light - or, as wireframe, its edges alone. Software throughout: the same picture on any machine.
void renderScene3D(QImage& image, const std::vector<DrawItem3D>& items, const Camera3D& camera, bool wireframe,
                   const QColor& background);

} // namespace Edraw_Desktop
