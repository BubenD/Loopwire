#pragma once

#include "model/Entity.h"
#include <QJsonObject>

#include <vector>

namespace Loopwire {

// Snapshot-based undo/redo of the entity list, mirroring Loopwire's
// pushUndo()/undoAction() model: capture the whole entity vector before a
// mutation and restore it on undo. Simple and robust for drawing-sized data.
class UndoStack {
public:
    void clear();

    // Record the current state as an undo point. Call before mutating.
    void push(const std::vector<Entity>& current, const QJsonObject& context = {});
    const QJsonObject& restoredContext() const { return restored_; }

    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }

    // Returns the state to restore, given the current state to bank on the
    // opposite stack. Caller assigns the returned vector to the document.
    std::vector<Entity> undo(const std::vector<Entity>& current, const QJsonObject& context = {});
    std::vector<Entity> redo(const std::vector<Entity>& current, const QJsonObject& context = {});

    int undoDepth() const { return static_cast<int>(undo_.size()); }

private:
    struct Snapshot { std::vector<Entity> entities; QJsonObject context; };
    std::vector<Snapshot> undo_, redo_;
    QJsonObject restored_;
    static constexpr size_t kMaxDepth = 200;
};

} // namespace Loopwire
