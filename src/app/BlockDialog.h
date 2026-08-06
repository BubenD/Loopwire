#pragma once

#include "harness/PartsLibrary.h"
#include "model/Block.h"

#include <QDialog>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace Edraw_Desktop {

class DrawingPreview;
class Editor;

// The blocks there are, and what can be done with them.
//
// Two sorts sit in one list: the blocks saved as files in the block folder, and every drawing in the
// connected part libraries - because a library part IS a block, and having to copy one into a block
// first would be busywork.
//
// A block from the library cannot be edited here: it belongs to the part, and the parts library is
// where it is changed. Saying so is better than letting an edit be made and then lost.
class BlockDialog : public QDialog {
    Q_OBJECT
public:
    explicit BlockDialog(Editor* editor, QWidget* parent = nullptr);

    void refresh();
    // Make a block out of what is selected on the sheet. The base point is the selection's
    // bottom-left, so the block lands under the cursor.
    bool createFromSelection(const QString& name);
    // Add what is selected to a block that already exists.
    bool addSelectionTo(const QString& name);
    void insertSelected();
    void removeSelected();
    const BlockDef* selectedBlock() const;
    int blockCount() const;

signals:
    // The host opens the block editor (a tab of its own, on a grey canvas).
    void editBlockRequested(const QString& name);

private:
    void buildUi();
    void refreshPreview();

    Editor* editor_ = nullptr;
    std::vector<BlockDef> blocks_;

    QListWidget* list_ = nullptr;
    QLineEdit* nameEdit_ = nullptr;
    QLabel* info_ = nullptr;
    QLabel* summary_ = nullptr;
    DrawingPreview* preview_ = nullptr;
    QPushButton* editBtn_ = nullptr;
    QPushButton* deleteBtn_ = nullptr;
    QPushButton* addToBtn_ = nullptr;
};

} // namespace Edraw_Desktop
