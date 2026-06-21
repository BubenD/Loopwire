#include "commands/UndoStack.h"

namespace Edraw_Desktop {

void UndoStack::clear() {
    undo_.clear();
    redo_.clear();
}

void UndoStack::push(const std::vector<Entity>& current) {
    undo_.push_back(current);
    if (undo_.size() > kMaxDepth)
        undo_.erase(undo_.begin());
    redo_.clear();
}

std::vector<Entity> UndoStack::undo(const std::vector<Entity>& current) {
    if (undo_.empty()) return current;
    redo_.push_back(current);
    std::vector<Entity> prev = std::move(undo_.back());
    undo_.pop_back();
    return prev;
}

std::vector<Entity> UndoStack::redo(const std::vector<Entity>& current) {
    if (redo_.empty()) return current;
    undo_.push_back(current);
    std::vector<Entity> next = std::move(redo_.back());
    redo_.pop_back();
    return next;
}

} // namespace Edraw_Desktop
