#pragma once

#include "geometry/Vec2.h"
#include "model/Document.h"
#include "model/Entity.h"

#include <QColor>
#include <QPointF>

class QPainter;

namespace Edraw_Desktop {

// World <-> screen mapping, matching Edraw's worldToScreen/screenToWorld:
//   screen.x =  world.x * scale + view.x
//   screen.y = -world.y * scale + view.y
struct ViewTransform {
    ViewState view;

    QPointF toScreen(const Vec2& w) const {
        return QPointF(w.x * view.scale + view.x, -w.y * view.scale + view.y);
    }
    Vec2 toWorld(const QPointF& s) const {
        return {(s.x() - view.x) / view.scale, -(s.y() - view.y) / view.scale};
    }
    double scale() const { return view.scale; }
};

// Stateless entity painter. Draws a single entity in screen space using the
// supplied transform; resolves color/weight against the document's layers.
class EntityRenderer {
public:
    // If `overrideColor` is valid, the entity is stroked in that color (used to
    // highlight the current selection).
    static void draw(QPainter& p, const Entity& e, const Document& doc,
                     const ViewTransform& vt,
                     const QColor& overrideColor = QColor());

    // Resolve the effective stroke color for an entity (explicit or ByLayer).
    static QColor resolveColor(const Entity& e, const Document& doc);

    // Number of decimals shown on dimension labels (Options dialog).
    static void setDimensionDecimals(int d);
    static int dimensionDecimals();

    // Canvas background color, so a Wipeout fills with an opaque mask that matches
    // the model-space background (kept in sync by the CanvasWidget).
    static void setBackgroundColor(const QColor& c);
    static QColor backgroundColor();
};

} // namespace Edraw_Desktop
