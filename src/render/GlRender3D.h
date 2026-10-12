#pragma once

#include "render/Render3D.h"

#include <memory>

namespace Loopwire {

// THE SAME PICTURE AS renderScene3D, DRAWN BY THE GRAPHICS CARD - for a CAD model of a million triangles, which the
// software renderer cannot turn smoothly. Drawn off screen into a picture, so the 3D view paints it as it paints the
// software one. Each mesh is sent to the card once and kept there while it is drawn. Where the machine has no usable
// OpenGL (or under the tests' offscreen platform) it says so, and the software renderer is used instead.
class GlRenderer3D {
public:
    GlRenderer3D();
    ~GlRenderer3D();
    GlRenderer3D(const GlRenderer3D&) = delete;
    GlRenderer3D& operator=(const GlRenderer3D&) = delete;

    // Whether it can draw at all - tried once, the first time it is asked.
    bool available();
    // Why it cannot, when it cannot.
    QString why() const;
    // Drawn into `image` at its size; false when it could not be, the image then untouched.
    bool render(QImage& image, const std::vector<DrawItem3D>& items, const Camera3D& camera, bool wireframe,
                const QColor& background, const Section3D& section = Section3D());

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace Loopwire
