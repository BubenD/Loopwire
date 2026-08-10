#pragma once

#include "model/Entity.h"
#include "render/EntityRenderer.h"

#include <QJsonArray>
#include <QPointF>
#include <QWidget>
#include <vector>

namespace Edraw_Desktop {

// A set of entities drawn the way they will look on the sheet: framed to fit, through the same
// renderer the canvas uses, so the preview IS the drawing rather than a picture of one. An empty
// one says so, so "nothing yet" is never mistaken for a blank drawing.
//
// The wheel zooms about the cursor and a drag pans, because a sheet shown whole is too small to check
// anything on - a title block or a zone label needs to be looked at closely. Double-click puts it back
// to the whole sheet.
//
// Shared: the parts library shows a part's two drawings with it, the mechanical builder shows the
// harness it is about to generate, and the template window shows the sheet.
class DrawingPreview : public QWidget {
    Q_OBJECT
public:
    explicit DrawingPreview(const QString& caption, QWidget* parent = nullptr);

    void setDrawing(const QJsonArray& drawing);
    void setEntities(const std::vector<Entity>& ents);
    void setCaption(const QString& caption);
    bool isEmpty() const { return ents_.empty(); }
    // What is shown when there is nothing to show ("no drawing yet").
    void setEmptyText(const QString& text);

    // Back to the whole drawing, filling the widget.
    void fitToView();
    double zoom() const { return zoom_; }

    // Picking one object out of what is shown. Used to say which line of a part's drawing the harness
    // attaches to: with this on, a click takes the nearest object under it and nothing else - one line,
    // never two, because a part has one place the harness meets it.
    void setPickable(bool on);
    bool isPickable() const { return pickable_; }
    // Which object is picked, by its place in the drawing (-1 for none), and setting it from outside.
    int pickedIndex() const { return picked_; }
    void setPickedIndex(int index);
    // The objects, so a caller can write the pick back onto the part.
    const std::vector<Entity>& entities() const { return ents_; }

signals:
    void pickedChanged(int index);

protected:
    void paintEvent(QPaintEvent*) override;
    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;

private:
    QString caption_;
    QString emptyText_;
    std::vector<Entity> ents_;

    // Zoom about the fitted view, and a pan in pixels on top of it. 1 is the whole drawing.
    double zoom_ = 1.0;
    QPointF pan_{0.0, 0.0};
    bool dragging_ = false;
    QPointF dragFrom_;

    bool pickable_ = false;
    int picked_ = -1;
    // The mapping the last paint used, so a click can be turned back into a point on the drawing
    // rather than the transform being worked out twice from two different places.
    ViewTransform lastVt_;
    bool lastVtValid_ = false;
};

} // namespace Edraw_Desktop
