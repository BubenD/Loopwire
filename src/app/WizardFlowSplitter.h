#pragma once
#include <QSplitter>
#include <QSplitterHandle>
#include <QMouseEvent>
#include <QScrollArea>
#include <QStackedWidget>
#include <QLayout>
#include <QApplication>
#include <QPointer>
#include <QTableWidget>
#include <QHeaderView>
#include <QScrollBar>
#include <algorithm>

namespace Loopwire {
// Vertical section dividers move the following panes, rather than borrowing
// their height. The enclosing scroll area accommodates the resulting column.
class WizardFlowSplitter : public QSplitter {
public:
    using QSplitter::QSplitter;
    static int readableMinimum(QWidget* widget) {
        const int specified = widget->property("sectionGripMinimum").toInt();
        if (specified > 0) return specified;
        if (auto* table = qobject_cast<QTableWidget*>(widget)) {
            const int row = table->rowCount() ? table->rowHeight(0) : table->verticalHeader()->defaultSectionSize();
            return table->horizontalHeader()->height() + 2 * row + 2 * table->frameWidth() +
                (table->horizontalScrollBar()->isVisible() ? table->horizontalScrollBar()->height() : 0) + 4;
        }
        if (auto* flow = dynamic_cast<WizardFlowSplitter*>(widget); flow && flow->orientation() == Qt::Vertical && flow->count()) {
            int height = flow->handleWidth() * (flow->count() - 1);
            for (int i = 0; i < flow->count(); ++i) height += i + 1 == flow->count() ? readableMinimum(flow->widget(i)) : flow->widget(i)->height();
            return height;
        }
        if (auto* pages = qobject_cast<QStackedWidget*>(widget); pages && pages->currentWidget()) {
            auto* page = pages->currentWidget(); auto* area = page->findChild<QScrollArea*>();
            if (area && area->widget() && page->layout()) {
                int height = area->widget()->minimumSizeHint().height();
                for (int i = 0; i < page->layout()->count(); ++i)
                    if (auto* item = page->layout()->itemAt(i)->widget(); item && item != area)
                        height += item->hasHeightForWidth() ? item->heightForWidth(page->width()) : item->sizeHint().height();
                return height + page->layout()->spacing() * (page->layout()->count() - 1) +
                    page->layout()->contentsMargins().top() + page->layout()->contentsMargins().bottom();
            }
        }
        return std::max(60, widget->fontMetrics().height() * 3 + 12);
    }
    bool flowLocked() const { return locked_; }
    void freezePanes() {
        if (orientation() != Qt::Vertical || locked_) return;
        heights_ = sizes(); locked_ = true;
        minimums_.clear(); maximums_.clear();
        for (int i = 0; i < count(); ++i) { minimums_ << widget(i)->minimumHeight(); maximums_ << widget(i)->maximumHeight(); }
        applyHeights();
    }
    void releasePanes() {
        setProperty("flowUserResized", false);
        if (!locked_) return;
        locked_ = false;
        for (int i = 0; i < count(); ++i) { widget(i)->setProperty("clipHeightManual", false); widget(i)->setMinimumHeight(minimums_.value(i, 0)); widget(i)->setMaximumHeight(maximums_.value(i, QWIDGETSIZE_MAX)); }
        setMinimumHeight(0); setMaximumHeight(QWIDGETSIZE_MAX);
    }
    void restoreFlowHeights(const QList<int>& heights) {
        if (orientation() != Qt::Vertical || heights.size() != count()) return;
        freezePanes(); heights_ = heights; applyHeights();
    }
    QList<int> flowHeights() const { return locked_ ? heights_ : QList<int>(); }
    static void fitNaturalColumn(QSplitter* splitter, QStackedWidget* pages) {
        auto* flow = static_cast<WizardFlowSplitter*>(splitter);
        if (flow->orientation() != Qt::Vertical || !pages->currentWidget()) return;
        auto* page = pages->currentWidget();
        auto* area = page->findChild<QScrollArea*>();
        if (!area || !area->widget() || !page->layout()) return;
        auto* content = area->widget();
        const int contentHeight = content->layout() ? content->layout()->totalSizeHint().height() : content->sizeHint().height();
        int height = contentHeight + page->layout()->contentsMargins().top() + page->layout()->contentsMargins().bottom();
        for (int i = 0; i < page->layout()->count(); ++i) {
            auto* child = page->layout()->itemAt(i)->widget();
            if (child && child != area) height += child->hasHeightForWidth() ? child->heightForWidth(page->width()) : child->sizeHint().height();
        }
        height += page->layout()->spacing() * std::max(0, page->layout()->count() - 1);
        auto sizes = flow->sizes();
        if (sizes.size() != 2) return;
        sizes[0] = std::max(sizes[0], height);
        if (auto* preview = qobject_cast<QScrollArea*>(flow->widget(1)); preview && preview->widget())
            sizes[1] = std::max(sizes[1], preview->widget()->minimumSizeHint().height());
        flow->restoreFlowHeights(sizes);
    }
    int growPane(int index, int delta) {
        freezePanes();
        if (!locked_ || index < 0 || index >= heights_.size()) return 0;
        const int before = heights_[index];
        heights_[index] = std::max(readableMinimum(widget(index)), heights_[index] + delta); applyHeights();
        emit splitterMoved(heights_[index], index + 1);
        return heights_[index] - before;
    }
    static void freezeAncestors(QWidget* section) {
        for (auto* p = section->parentWidget(); p; p = p->parentWidget())
            if (auto* split = dynamic_cast<WizardFlowSplitter*>(p)) { split->setProperty("flowUserResized", true); split->freezePanes(); }
    }
    static void growAncestors(QWidget* section, int delta) {
        for (auto* p = section->parentWidget(); p; p = p->parentWidget())
            if (auto* split = dynamic_cast<WizardFlowSplitter*>(p); split && split->orientation() == Qt::Vertical) {
                for (int i = 0; i < split->count(); ++i)
                    if (split->widget(i)->isAncestorOf(section)) { split->growPane(i, delta); break; }
            }
    }
protected:
    QSplitterHandle* createHandle() override {
        class Handle : public QSplitterHandle {
        public:
            explicit Handle(WizardFlowSplitter* split) : QSplitterHandle(split->orientation(), split), split_(split) {}
        protected:
            void mousePressEvent(QMouseEvent* event) override {
                if (orientation() != Qt::Vertical) { QSplitterHandle::mousePressEvent(event); return; }
                if (event->button() != Qt::LeftButton) return;
                index_ = -1;
                for (int i = 1; i < split_->count(); ++i) if (split_->handle(i) == this) index_ = i - 1;
                // The editor contains several sections. Its bottom divider
                // belongs to the last section, never to the whole editor.
                if (index_ >= 0) if (auto* pages = qobject_cast<QStackedWidget*>(split_->widget(index_)); pages && pages->currentWidget()) {
                    QWidget* last = nullptr;
                    for (auto* frame : pages->currentWidget()->findChildren<QWidget*>())
                        if (frame->property("wizardSectionFrame").toBool() && frame->isVisibleTo(pages->currentWidget()) &&
                            (!last || frame->mapTo(pages, QPoint()).y() > last->mapTo(pages, QPoint()).y())) last = frame;
                    if (last) delegate_ = last->findChild<QWidget*>(QStringLiteral("sectionResizeGrip"), Qt::FindDirectChildrenOnly);
                }
                if (delegate_) { forward(event); return; }
                split_->setProperty("flowUserResized", true);
                split_->freezePanes(); WizardFlowSplitter::freezeAncestors(split_);
                from_ = event->globalPosition().y(); applied_ = 0;
                start_ = index_ >= 0 ? split_->widget(index_)->height() : 0;
                event->accept();
            }
            void mouseMoveEvent(QMouseEvent* event) override {
                if (orientation() != Qt::Vertical) { QSplitterHandle::mouseMoveEvent(event); return; }
                if (delegate_) { forward(event); return; }
                if (index_ < 0 || !(event->buttons() & Qt::LeftButton)) return;
                const int delta = std::max(WizardFlowSplitter::readableMinimum(split_->widget(index_)) - start_, int(event->globalPosition().y() - from_));
                const int actual = split_->growPane(index_, delta - applied_);
                WizardFlowSplitter::growAncestors(split_, actual);
                applied_ += actual; event->accept();
            }
            void mouseReleaseEvent(QMouseEvent* event) override {
                if (orientation() != Qt::Vertical) { QSplitterHandle::mouseReleaseEvent(event); return; }
                if (delegate_) { forward(event); delegate_.clear(); }
                index_ = -1; event->accept();
            }
        private:
            void forward(QMouseEvent* event) {
                QMouseEvent copy(event->type(), QPointF(delegate_->mapFromGlobal(event->globalPosition().toPoint())), event->globalPosition(),
                                 event->button(), event->buttons(), event->modifiers());
                QApplication::sendEvent(delegate_, &copy); event->accept();
            }
            QPointer<QWidget> delegate_;
            WizardFlowSplitter* split_;
            int index_ = -1, start_ = 0, applied_ = 0;
            double from_ = 0;
        };
        return new Handle(this);
    }
private:
    void applyHeights() {
        int height = handleWidth() * std::max(0, count() - 1);
        for (int i = 0; i < count(); ++i) { heights_[i] = std::max(readableMinimum(widget(i)), heights_[i]); widget(i)->setProperty("clipHeightManual", true); widget(i)->setFixedHeight(heights_[i]); height += heights_[i]; }
        setFixedHeight(height); setSizes(heights_); updateGeometry();
    }
    QList<int> heights_;
    QList<int> minimums_, maximums_;
    bool locked_ = false;
};
}
