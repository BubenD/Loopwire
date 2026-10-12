#include "commands/UndoStack.h"

namespace Loopwire {

void UndoStack::clear() {
    undo_.clear();
    redo_.clear();
    restored_={};
}

void UndoStack::push(const std::vector<Entity>& current, const QJsonObject& context) {
    undo_.push_back({current,context});
    if (undo_.size() > kMaxDepth)
        undo_.erase(undo_.begin());
    redo_.clear();
}

std::vector<Entity> UndoStack::undo(const std::vector<Entity>& current, const QJsonObject& context) {
    if (undo_.empty()) return current;
    redo_.push_back({current,context});
    restored_=undo_.back().context;
    std::vector<Entity> prev = std::move(undo_.back().entities);
    undo_.pop_back();
    return prev;
}

std::vector<Entity> UndoStack::redo(const std::vector<Entity>& current, const QJsonObject& context) {
    if (redo_.empty()) return current;
    undo_.push_back({current,context});
    restored_=redo_.back().context;
    std::vector<Entity> next = std::move(redo_.back().entities);
    redo_.pop_back();
    return next;
}

} // namespace Loopwire
