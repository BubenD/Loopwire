#pragma once

#include "model/Entity.h"

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
};

} // namespace Edraw_Desktop
