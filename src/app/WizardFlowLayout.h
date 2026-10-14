#pragma once
#include <QLayout>
#include <QWidget>
#include <QWidgetItem>
#include <algorithm>

namespace Loopwire {
// Toolbars wrap instead of forcing the wizard wider than its screen.
class WizardFlowLayout : public QLayout {
public:
    explicit WizardFlowLayout(QWidget* parent = nullptr) : QLayout(parent) { setContentsMargins(0, 0, 0, 0); setSpacing(6); }
    ~WizardFlowLayout() override { while (auto* item = takeAt(0)) delete item; }
    void addItem(QLayoutItem* item) override { items_.append(item); }
    void addWidget(QWidget* widget, int = 0) { QLayout::addWidget(widget); }
    void addStretch(int = 0) { addItem(new QSpacerItem(6, 1)); }
    void addSpacing(int width) { addItem(new QSpacerItem(width, 1)); }
    int count() const override { return items_.size(); }
    QLayoutItem* itemAt(int i) const override { return items_.value(i); }
    QLayoutItem* takeAt(int i) override { return i >= 0 && i < items_.size() ? items_.takeAt(i) : nullptr; }
    Qt::Orientations expandingDirections() const override { return {}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return place(QRect(0, 0, width, 0), false); }
    QSize sizeHint() const override { return minimumSize(); }
    QSize minimumSize() const override {
        QSize size;
        for (auto* item : items_) size = size.expandedTo(item->minimumSize());
        return size;
    }
    void setGeometry(const QRect& rect) override { QLayout::setGeometry(rect); place(rect, true); }
private:
    int place(const QRect& rect, bool apply) const {
        int x = rect.x(), y = rect.y(), height = 0;
        for (auto* item : items_) {
            if (item->isEmpty()) continue;
            const QSize size = item->sizeHint();
            if (x > rect.x() && x + size.width() > rect.right() + 1) {
                x = rect.x(); y += height + spacing(); height = 0;
            }
            if (apply) item->setGeometry(QRect(QPoint(x, y), size));
            x += size.width() + spacing(); height = std::max(height, size.height());
        }
        return y + height - rect.y();
    }
    QList<QLayoutItem*> items_;
};
}
