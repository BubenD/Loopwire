#pragma once

#include <QPlainTextEdit>
#include <QKeyEvent>
#include <functional>

namespace Loopwire {
class LabelCaptionEdit : public QPlainTextEdit {
public:
    using QPlainTextEdit::QPlainTextEdit;
    std::function<void(const QString&)> commit;
protected:
    void keyPressEvent(QKeyEvent* event) override {
        if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
            !(event->modifiers() & Qt::ShiftModifier)) {
            if (commit) commit(toPlainText());
            event->accept();
            return;
        }
        QPlainTextEdit::keyPressEvent(event);
    }
    void focusOutEvent(QFocusEvent* event) override {
        QPlainTextEdit::focusOutEvent(event);
        if (commit) commit(toPlainText());
    }
};
} // namespace Loopwire
