#pragma once

#include "geometry/Mesh3D.h"

#include <QColor>
#include <QImage>

#include <algorithm>
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
    // THE RAY THROUGH A PIXEL of a picture so big: from the eye, through that point of the picture.
    void rayAt(double px, double py, int w, int h, V3& origin, V3& dir) const;
    // How far it sees: its near plane, a little way in front of the eye.
    double nearPlane() const { return std::max(0.01, distance * 0.001); }
};

// A SECTION: everything beyond a plane cut away, so the inside of a model shows. A point p is cut away where
// dot(p, normal) > offset.
struct Section3D {
    bool on = false;
    V3 normal{1.0, 0.0, 0.0};
    double offset = 0.0;
    bool cuts(const V3& p) const { return on && dot3(p, normal) > offset; }
};

// One mesh to draw, in its colour; see-through below an alpha of one, and cut by the section or not.
struct DrawItem3D {
    const Mesh3D* mesh = nullptr;
    QColor color;
    double alpha = 1.0;
    bool sectioned = false;
};

// THE PICTURE: every triangle of every item, nearest in front (a depth buffer), each lit by how squarely it faces the
// light - or, as wireframe, its edges alone. See-through items over the rest. Software throughout: the same picture
// on any machine.
void renderScene3D(QImage& image, const std::vector<DrawItem3D>& items, const Camera3D& camera, bool wireframe,
                   const QColor& background, const Section3D& section = Section3D());

} // namespace Edraw_Desktop
