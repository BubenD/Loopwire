#include "app/BlockDialog.h"

#include "app/DrawingPreview.h"
#include "app/Editor.h"
#include "io/AppPaths.h"
#include "model/Document.h"
#include "tools/EntityTransforms.h"

#include <QDesktopServices>
#include <QFile>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QUrl>
#include <QVBoxLayout>

namespace Loopwire {

BlockDialog::BlockDialog(Editor* editor, QWidget* parent) : QDialog(parent), editor_(editor) {
    setWindowTitle(QStringLiteral("Blocks"));
    resize(900, 600);
    buildUi();
    refresh();
}

void BlockDialog::buildUi() {
    auto* root = new QVBoxLayout(this);
    auto* split = new QSplitter(Qt::Horizontal, this);

    auto* left = new QWidget(split);
    auto* leftLay = new QVBoxLayout(left);
    leftLay->setContentsMargins(0, 0, 0, 0);
    list_ = new QListWidget(left);
    list_->setToolTip(QStringLiteral("Double-click to drop a copy on the sheet"));
    connect(list_, &QListWidget::currentRowChanged, this, [this](int) { refreshPreview(); });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem*) {
        insertSelected();
    });
    leftLay->addWidget(list_, 1);
    info_ = new QLabel(QString(), left);
    info_->setWordWrap(true);
    info_->setStyleSheet(QStringLiteral("color:#9fb4c8;"));
    leftLay->addWidget(info_);
    split->addWidget(left);

    auto* right = new QWidget(split);
    auto* rightLay = new QVBoxLayout(right);
    rightLay->setContentsMargins(0, 0, 0, 0);
    preview_ = new DrawingPreview(QStringLiteral("The block"), right);
    preview_->setEmptyText(QStringLiteral("nothing selected"));
    rightLay->addWidget(preview_, 1);
    split->addWidget(right);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    root->addWidget(split, 1);

    // ---- Making one out of what is on the sheet ----
    auto* makeBox = new QGroupBox(QStringLiteral("Make a block out of what is selected"), this);
    auto* makeLay = new QHBoxLayout(makeBox);
    nameEdit_ = new QLineEdit(makeBox);
    nameEdit_->setPlaceholderText(QStringLiteral("what to call it"));
    makeLay->addWidget(nameEdit_, 1);
    auto* createBtn = new QPushButton(QStringLiteral("Create"), makeBox);
    createBtn->setToolTip(QStringLiteral(
        "Save what is selected as a block, in the block folder. The selection stays where it is."));
    connect(createBtn, &QPushButton::clicked, this, [this] {
        createFromSelection(nameEdit_->text());
    });
    makeLay->addWidget(createBtn);
    addToBtn_ = new QPushButton(QStringLiteral("Add to the selected block"), makeBox);
    addToBtn_->setToolTip(QStringLiteral(
        "Put what is selected into the block picked on the left, and rebuild every copy of it"));
    connect(addToBtn_, &QPushButton::clicked, this, [this] {
        if (const BlockDef* b = selectedBlock()) addSelectionTo(b->name);
    });
    makeLay->addWidget(addToBtn_);
    root->addWidget(makeBox);

    summary_ = new QLabel(QString(), this);
    summary_->setWordWrap(true);
    summary_->setStyleSheet(QStringLiteral("color:#9fb4c8;"));
    root->addWidget(summary_);

    auto* foot = new QHBoxLayout;
    auto* insertBtn = new QPushButton(QStringLiteral("Insert"), this);
    insertBtn->setDefault(true);
    insertBtn->setToolTip(QStringLiteral("Then click the sheet to drop it"));
    connect(insertBtn, &QPushButton::clicked, this, [this] { insertSelected(); });
    foot->addWidget(insertBtn);
    editBtn_ = new QPushButton(QStringLiteral("Edit the block"), this);
    editBtn_->setToolTip(QStringLiteral(
        "Open it in a tab of its own, on a grey canvas. Saving rebuilds every copy on every sheet."));
    connect(editBtn_, &QPushButton::clicked, this, [this] {
        if (const BlockDef* b = selectedBlock()) emit editBlockRequested(b->name);
    });
    foot->addWidget(editBtn_);
    deleteBtn_ = new QPushButton(QStringLiteral("Delete"), this);
    connect(deleteBtn_, &QPushButton::clicked, this, [this] { removeSelected(); });
    foot->addWidget(deleteBtn_);
    auto* folderBtn = new QPushButton(QStringLiteral("Show the folder"), this);
    connect(folderBtn, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::blockDir()));
    });
    foot->addWidget(folderBtn);
    foot->addStretch(1);
    auto* closeBtn = new QPushButton(QStringLiteral("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    foot->addWidget(closeBtn);
    root->addLayout(foot);
}

void BlockDialog::refresh() {
    const QString keep = selectedBlock() ? selectedBlock()->name : QString();
    // ONLY the blocks the user made. A part in the library is not a block: it is a part, it lives in
    // the parts library, it is edited there and it is placed from there. Listing every drawing of every
    // connected library here buried the handful of blocks somebody actually made among dozens of parts
    // - and the entries could not be edited or deleted from this window anyway, since they belong to
    // the library.
    blocks_ = allBlocks(nullptr);
    list_->clear();
    for (const BlockDef& b : blocks_) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1   —   %2 object(s)").arg(b.name).arg(b.entities.size()), list_);
        item->setData(Qt::UserRole, b.name);
    }
    if (summary_)
        summary_->setText(blocks_.empty()
                              ? QStringLiteral("No blocks yet — select something and Create one. "
                                               "(Library parts live in the parts library.)")
                              : QStringLiteral("%1 block(s), saved as files.").arg(blocks_.size()));
    if (!keep.isEmpty())
        for (int i = 0; i < list_->count(); ++i)
            if (list_->item(i)->data(Qt::UserRole).toString() == keep) list_->setCurrentRow(i);
    if (list_->currentRow() < 0 && list_->count() > 0) list_->setCurrentRow(0);
    refreshPreview();
}

int BlockDialog::blockCount() const { return static_cast<int>(blocks_.size()); }

const BlockDef* BlockDialog::selectedBlock() const {
    if (!list_ || list_->currentRow() < 0) return nullptr;
    const QString name = list_->item(list_->currentRow())->data(Qt::UserRole).toString();
    for (const BlockDef& b : blocks_)
        if (b.name == name) return &b;
    return nullptr;
}

void BlockDialog::refreshPreview() {
    const BlockDef* b = selectedBlock();
    if (preview_) preview_->setEntities(b ? partDrawingEntities(b->entities) : std::vector<Entity>{});
    // A library block is the part's own drawing: it is changed in the parts library, not here.
    if (editBtn_) editBtn_->setEnabled(b && !b->fromLibrary());
    if (deleteBtn_) deleteBtn_->setEnabled(b && !b->fromLibrary());
    if (addToBtn_) addToBtn_->setEnabled(b && !b->fromLibrary());
    if (!info_) return;
    if (!b) {
        info_->setText(QString());
        return;
    }
    QStringList bits;
    if (b->fromLibrary())
        bits << QStringLiteral("From the parts library — edit it in the library, where it belongs.");
    if (!b->description.isEmpty()) bits << b->description;
    if (!b->created.isEmpty()) bits << QStringLiteral("saved %1").arg(b->created);
    if (editor_ && editor_->document()) {
        const int copies = blockInstancesIn(*editor_->document(), b->name).size();
        bits << (copies == 0 ? QStringLiteral("no copies on this sheet")
                             : QStringLiteral("%1 cop%2 on this sheet")
                                   .arg(copies)
                                   .arg(copies == 1 ? QStringLiteral("y") : QStringLiteral("ies")));
    }
    info_->setText(bits.join(QStringLiteral("  •  ")));
}

bool BlockDialog::createFromSelection(const QString& name) {
    if (!editor_) return false;
    const QString use = name.trimmed();
    if (use.isEmpty()) {
        if (summary_) summary_->setText(QStringLiteral("Give the block a name first."));
        return false;
    }
    const std::vector<Entity> sel = editor_->copyOfSelection();
    if (sel.empty()) {
        if (summary_)
            summary_->setText(QStringLiteral("Nothing is selected. Pick what the block is made of "
                                            "on the sheet, then Create."));
        return false;
    }
    // The base point is the selection's bottom-left, so a dropped block sits under the cursor
    // rather than off at some remembered offset.
    Bounds b;
    for (const Entity& e : sel) {
        const Bounds eb = e.bounds();
        if (eb.valid) { b.expand(eb.min); b.expand(eb.max); }
    }
    const Vec2 base = b.valid ? b.min : Vec2{0.0, 0.0};

    const QString path = BlockDef::pathFor(use);
    if (QFile::exists(path) &&
        QMessageBox::question(this, QStringLiteral("Blocks"),
                             QStringLiteral("%1 already exists. Replace it? Every copy of it on "
                                            "every open sheet will be rebuilt.")
                                 .arg(use),
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No) !=
            QMessageBox::Yes)
        return false;

    const BlockDef block = blockFromEntities(use, sel, base);
    if (!block.save(path)) {
        QMessageBox::warning(this, QStringLiteral("Blocks"),
                             QStringLiteral("Could not write %1").arg(path));
        return false;
    }
    refresh();
    if (summary_)
        summary_->setText(QStringLiteral("Saved %1 — %2 object(s). Insert it from the list.")
                              .arg(use)
                              .arg(block.entities.size()));
    return true;
}

bool BlockDialog::addSelectionTo(const QString& name) {
    if (!editor_ || !editor_->document()) return false;
    const std::vector<Entity> sel = editor_->copyOfSelection();
    if (sel.empty()) {
        if (summary_) summary_->setText(QStringLiteral("Nothing is selected to add."));
        return false;
    }
    BlockDef block;
    for (const BlockDef& b : blocks_)
        if (b.name == name) block = b;
    if (block.name.isEmpty() || block.fromLibrary()) return false;

    // What is added keeps its position relative to the block's own origin, which is where the
    // copies of it sit on the sheet.
    for (const Entity& e : sel) {
        Entity moved = e;
        // A copy of THIS block already carries its origin; anything else is taken as drawn.
        const Vec2 origin = entityBlockName(e) == name ? entityBlockOrigin(e) : Vec2{0.0, 0.0};
        translateEntity(moved, Vec2{-origin.x, -origin.y});
        moved.meta.remove(QStringLiteral("block"));
        moved.meta.remove(QStringLiteral("blockInst"));
        moved.meta.remove(QStringLiteral("blockOriginX"));
        moved.meta.remove(QStringLiteral("blockOriginY"));
        QJsonObject o = moved.toJson();
        o.remove(QStringLiteral("id"));
        block.entities.append(o);
    }
    if (!block.save(BlockDef::pathFor(block.name))) return false;
    const int rebuilt = updateBlockInstances(*editor_->document(), block);
    editor_->clearSelection();
    refresh();
    if (summary_)
        summary_->setText(QStringLiteral("Added %1 object(s) to %2; %3 cop%4 rebuilt.")
                              .arg(sel.size())
                              .arg(block.name)
                              .arg(rebuilt)
                              .arg(rebuilt == 1 ? QStringLiteral("y") : QStringLiteral("ies")));
    return true;
}

void BlockDialog::insertSelected() {
    const BlockDef* b = selectedBlock();
    if (!b || !editor_) return;
    if (b->isEmpty()) {
        if (summary_) summary_->setText(QStringLiteral("There is nothing in that block to insert."));
        return;
    }
    // Dropped by a click, like anything else that is inserted. The copy is stamped so that
    // double-clicking it later opens the block rather than one of its pieces.
    editor_->placeEntities(blockInstanceEntities(*b, Vec2{0.0, 0.0}, QString()));
    if (summary_)
        summary_->setText(QStringLiteral("Click the sheet to drop %1.").arg(b->name));
    hide();
}

void BlockDialog::removeSelected() {
    const BlockDef* b = selectedBlock();
    if (!b || b->fromLibrary()) return;
    const QString name = b->name;
    int copies = 0;
    if (editor_ && editor_->document())
        copies = blockInstancesIn(*editor_->document(), name).size();
    const QString warn =
        copies > 0 ? QStringLiteral("Delete the block %1?\n\nThere %2 %3 cop%4 on this sheet. They "
                                    "stay where they are as ordinary drawing - deleting the block "
                                    "does not remove them.")
                         .arg(name)
                         .arg(copies == 1 ? QStringLiteral("is") : QStringLiteral("are"))
                         .arg(copies)
                         .arg(copies == 1 ? QStringLiteral("y") : QStringLiteral("ies"))
                   : QStringLiteral("Delete the block %1?").arg(name);
    if (QMessageBox::question(this, QStringLiteral("Blocks"), warn,
                             QMessageBox::Yes | QMessageBox::No, QMessageBox::No) !=
        QMessageBox::Yes)
        return;
    QFile::remove(BlockDef::pathFor(name));
    refresh();
    if (summary_) summary_->setText(QStringLiteral("Deleted %1.").arg(name));
}

} // namespace Loopwire
