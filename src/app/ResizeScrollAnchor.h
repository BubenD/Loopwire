#pragma once

#include <QList>
#include <QMouseEvent>
#include <QPointer>
#include <QScrollArea>
#include <QAbstractScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QWidget>
#include <algorithm>
#include <utility>

namespace Loopwire {

// Resizing a section must not also navigate its enclosing scrolling pages.
// Apply after layout settles, including QScrollArea's deferred range updates.
class ResizeScrollAnchor : public QObject {
public:
    static void install(QWidget* handle) {
        if (!handle || handle->property("resizeScrollAnchorInstalled").toBool()) return;
        handle->setProperty("resizeScrollAnchorInstalled", true);
        new ResizeScrollAnchor(handle);
    }

private:
    explicit ResizeScrollAnchor(QWidget* handle) : QObject(handle) { handle->installEventFilter(this); }
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick) {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() == Qt::LeftButton) {
                ++sequence_; active_ = true; positions_.clear();
                auto* container = qobject_cast<QWidget*>(watched)->parentWidget();
                // An outer splitter changes the viewports inside its panes;
                // a section grip changes content inside an ancestor viewport.
                auto areas = container ? container->findChildren<QAbstractScrollArea*>() : QList<QAbstractScrollArea*>();
                for (auto* ancestor = container; ancestor; ancestor = ancestor->parentWidget())
                    if (auto* area = qobject_cast<QAbstractScrollArea*>(ancestor); area && !areas.contains(area)) areas.append(area);
                for (auto* area : areas)
                    for (auto* bar : {area->verticalScrollBar(), area->horizontalScrollBar()})
                        positions_.append({bar, bar->value()});
                if (event->type() == QEvent::MouseButtonDblClick) restoreLater();
            }
        } else if (active_ && event->type() == QEvent::MouseMove) {
            if (static_cast<QMouseEvent*>(event)->buttons() & Qt::LeftButton) restoreLater();
        } else if (active_ && event->type() == QEvent::MouseButtonRelease) {
            if (static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
                active_ = false; restoreLater();
            }
        }
        return false; // The handle still performs its normal resize.
    }
    void restoreLater() {
        const int sequence = sequence_;
        const auto positions = positions_;
        auto restore = [this, sequence, positions] {
            if (sequence != sequence_) return;
            for (const auto& position : positions) if (auto* bar = position.first.data())
                bar->setValue(std::clamp(position.second, bar->minimum(), bar->maximum()));
        };
        QTimer::singleShot(0, this, [this, restore] {
            restore();
            QTimer::singleShot(0, this, restore);
        });
    }
    QList<std::pair<QPointer<QScrollBar>, int>> positions_;
    int sequence_ = 0;
    bool active_ = false;
};

} // namespace Loopwire
