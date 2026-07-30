#include "app/PartsLibraryDialog.h"

#include "app/Editor.h"
#include "model/Document.h"
#include "render/EntityRenderer.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace Edraw_Desktop {

// One of a part's two drawings, drawn the way it will look on the sheet: the stored entities,
// scaled to fit. An empty one says so, so "no symbol yet" is never mistaken for a blank one.
class PartDrawingPreview : public QWidget {
public:
    explicit PartDrawingPreview(const QString& caption, QWidget* parent = nullptr)
        : QWidget(parent), caption_(caption) {
        setMinimumSize(190, 130);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    void setDrawing(const QJsonArray& drawing) {
        ents_ = partDrawingEntities(drawing);
        update();
    }
    bool isEmpty() const { return ents_.empty(); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRect r = rect().adjusted(0, 0, -1, -1);
        p.fillRect(r, QColor("#2b2e33"));
        p.setPen(QPen(QColor("#4a4e55"), 1));
        p.drawRect(r);
        QFont f = p.font();
        f.setPixelSize(10);
        p.setFont(f);
        p.setPen(QColor("#8fa2b8"));
        p.drawText(r.adjusted(6, 4, -6, 0), Qt::AlignLeft | Qt::AlignTop, caption_);
        if (ents_.empty()) {
            p.setPen(QColor("#6f7681"));
            p.drawText(r, Qt::AlignCenter, QStringLiteral("no drawing yet"));
            return;
        }
        // Frame everything, then draw it through the same renderer the canvas uses, so the
        // preview is the drawing rather than a picture of one.
        Document doc;
        doc.entities().clear();
        Bounds b;
        for (const Entity& e : ents_) {
            doc.entities().push_back(e);
            const Bounds eb = e.bounds();
            if (eb.valid) { b.expand(eb.min); b.expand(eb.max); }
        }
        if (!b.valid) return;
        const double w = std::max(1e-6, b.max.x - b.min.x);
        const double h = std::max(1e-6, b.max.y - b.min.y);
        const QRect box = r.adjusted(10, 18, -10, -10);
        const double scale = std::min(box.width() / w, box.height() / h) * 0.9;
        ViewTransform vt;
        vt.view.scale = scale;
        vt.view.x = box.center().x() - (b.min.x + b.max.x) / 2.0 * scale;
        vt.view.y = box.center().y() + (b.min.y + b.max.y) / 2.0 * scale;
        p.save();
        p.setClipRect(box);
        EntityRenderer::setBackgroundColor(QColor("#2b2e33"));
        for (const Entity& e : doc.entities())
            EntityRenderer::draw(p, e, doc, vt, QColor("#d7dade"));
        p.restore();
    }

private:
    QString caption_;
    std::vector<Entity> ents_;
};

namespace {

// A small labelled row in the parameter form.
QWidget* rowHost(QWidget* parent, const QString& label, QWidget* field, const QString& unit) {
    auto* host = new QWidget(parent);
    auto* lay = new QHBoxLayout(host);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);
    auto* cap = new QLabel(label, host);
    cap->setFixedWidth(150);
    cap->setStyleSheet(QStringLiteral("color:#9fb4c8;"));
    lay->addWidget(cap);
    // The field takes a fixed share, so the unit after it stays on screen and the rows line up
    // however long a label is.
    field->setMaximumWidth(260);
    lay->addWidget(field, 1);
    auto* u = new QLabel(unit, host);
    u->setFixedWidth(42);
    u->setStyleSheet(QStringLiteral("color:#7f8896;"));
    lay->addWidget(u);
    lay->addStretch(1);
    return host;
}

} // namespace

PartsLibraryDialog::PartsLibraryDialog(Editor* editor, QWidget* parent)
    : QDialog(parent), editor_(editor) {
    setWindowTitle(QStringLiteral("Harness parts library"));
    resize(1180, 760);
    path_ = PartsLibrary::defaultPath();
    lib_.load(path_); // an empty library on the first run
    buildUi();
    reloadTree();
    if (!lib_.parts.empty()) selectPart(lib_.parts.front().id);
    else showPart(QString());
}

void PartsLibraryDialog::setLibraryPath(const QString& path) {
    path_ = path;
    lib_ = PartsLibrary();
    lib_.load(path_);
    reloadTree();
    showPart(lib_.parts.empty() ? QString() : lib_.parts.front().id);
}

void PartsLibraryDialog::buildUi() {
    auto* root = new QVBoxLayout(this);
    auto* split = new QSplitter(Qt::Horizontal, this);
    root->addWidget(split, 1);

    // ---- Left: the parts, by kind ----
    auto* left = new QWidget(split);
    auto* lv = new QVBoxLayout(left);
    lv->setContentsMargins(0, 0, 0, 0);
    filter_ = new QLineEdit(left);
    filter_->setPlaceholderText(QStringLiteral("Filter by description, P/N or manufacturer"));
    connect(filter_, &QLineEdit::textChanged, this, [this] { reloadTree(); });
    lv->addWidget(filter_);
    tree_ = new QTreeWidget(left);
    tree_->setHeaderLabels({QStringLiteral("Part"), QStringLiteral("Manufacturer")});
    tree_->setColumnWidth(0, 250);
    tree_->setRootIsDecorated(true);
    connect(tree_, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* it, QTreeWidgetItem*) {
                if (syncing_ || !it) return;
                const QString id = it->data(0, Qt::UserRole).toString();
                if (!id.isEmpty()) showPart(id);
            });
    lv->addWidget(tree_, 1);

    auto* addRow = new QWidget(left);
    auto* ar = new QHBoxLayout(addRow);
    ar->setContentsMargins(0, 0, 0, 0);
    auto* newKind = new QComboBox(addRow);
    for (PartKind k : allPartKinds()) newKind->addItem(partKindName(k), partKindKey(k));
    ar->addWidget(newKind, 1);
    auto* addBtn = new QPushButton(QStringLiteral("+ New part"), addRow);
    connect(addBtn, &QPushButton::clicked, this, [this, newKind] {
        LibraryPart* p = addPart(partKindFromKey(newKind->currentData().toString()));
        if (p) selectPart(p->id);
    });
    ar->addWidget(addBtn);
    auto* dupBtn = new QPushButton(QStringLiteral("Duplicate"), addRow);
    connect(dupBtn, &QPushButton::clicked, this, [this] { duplicateCurrentPart(); });
    ar->addWidget(dupBtn);
    auto* delBtn = new QPushButton(QStringLiteral("Delete"), addRow);
    connect(delBtn, &QPushButton::clicked, this, [this] { deleteCurrentPart(); });
    ar->addWidget(delBtn);
    lv->addWidget(addRow);
    split->addWidget(left);

    // ---- Right: the part ----
    auto* right = new QWidget(split);
    auto* rv = new QVBoxLayout(right);
    rv->setContentsMargins(0, 0, 0, 0);

    auto* head = new QGroupBox(QStringLiteral("Part"), right);
    auto* hg = new QGridLayout(head);
    hg->setContentsMargins(8, 8, 8, 8);
    hg->setHorizontalSpacing(8);
    int hr = 0;
    auto headRow = [&](const QString& label, QWidget* field) {
        auto* cap = new QLabel(label, head);
        cap->setStyleSheet(QStringLiteral("color:#9fb4c8;"));
        hg->addWidget(cap, hr, 0);
        hg->addWidget(field, hr, 1);
        ++hr;
    };
    kindBox_ = new QComboBox(head);
    for (PartKind k : allPartKinds()) kindBox_->addItem(partKindName(k), partKindKey(k));
    connect(kindBox_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (syncing_) return;
        if (LibraryPart* p = current()) {
            p->kind = partKindFromKey(kindBox_->currentData().toString());
            // A different kind is issued differently: the unit follows unless it was set by hand.
            if (p->unit.isEmpty()) p->unit = defaultUnitForKind(p->kind);
            rebuildParamForm(); // a different kind asks for different parameters
            touched();
        }
    });
    headRow(QStringLiteral("Type"), kindBox_);
    descEdit_ = new QLineEdit(head);
    descEdit_->setPlaceholderText(QStringLiteral("what the part is"));
    connect(descEdit_, &QLineEdit::textChanged, this, [this](const QString& t) {
        if (syncing_) return;
        if (LibraryPart* p = current()) { p->description = t; touched(); }
    });
    headRow(QStringLiteral("Description"), descEdit_);
    mfrEdit_ = new QLineEdit(head);
    connect(mfrEdit_, &QLineEdit::textChanged, this, [this](const QString& t) {
        if (syncing_) return;
        if (LibraryPart* p = current()) { p->mfrName = t; touched(); }
    });
    headRow(QStringLiteral("Mfr name"), mfrEdit_);
    pnEdit_ = new QLineEdit(head);
    connect(pnEdit_, &QLineEdit::textChanged, this, [this](const QString& t) {
        if (syncing_) return;
        if (LibraryPart* p = current()) { p->mfrPn = t; touched(); }
    });
    headRow(QStringLiteral("Mfr P/N"), pnEdit_);
    internalEdit_ = new QLineEdit(head);
    internalEdit_->setPlaceholderText(QStringLiteral("the house part number, if there is one"));
    connect(internalEdit_, &QLineEdit::textChanged, this, [this](const QString& t) {
        if (syncing_) return;
        if (LibraryPart* p = current()) { p->internalPn = t; touched(); }
    });
    headRow(QStringLiteral("Internal P/N"), internalEdit_);
    // What the part is issued in. A bill of materials totals quantities by it, so it decides what
    // can be added to what: wire by the metre, a connector each, heatshrink by the millimetre.
    unitBox_ = new QComboBox(head);
    unitBox_->setEditable(true);
    for (const PartUnit& u : partUnits())
        unitBox_->addItem(QStringLiteral("%1 — %2").arg(u.code, u.label), u.code);
    unitBox_->setToolTip(QStringLiteral(
        "The unit this part is bought and counted in. The bill of materials adds up quantities in "
        "it, and can only add units of the same kind (mm to m, never m to each)."));
    connect(unitBox_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (syncing_ || i < 0) return;
        if (LibraryPart* p = current()) { p->unit = unitBox_->itemData(i).toString(); touched(); }
    });
    headRow(QStringLiteral("Unit"), unitBox_);
    rv->addWidget(head);

    // The technical parameters of this kind, in a scroller: some kinds have many.
    auto* paramBox = new QGroupBox(QStringLiteral("Technical parameters"), right);
    auto* pbLay = new QVBoxLayout(paramBox);
    pbLay->setContentsMargins(8, 8, 8, 8);
    auto* scroll = new QScrollArea(paramBox);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    paramHost_ = new QWidget(scroll);
    paramLay_ = new QVBoxLayout(paramHost_);
    paramLay_->setContentsMargins(0, 0, 0, 0);
    paramLay_->setSpacing(4);
    scroll->setWidget(paramHost_);
    pbLay->addWidget(scroll);
    rv->addWidget(paramBox, 1);

    // The two drawings.
    auto* draw = new QGroupBox(QStringLiteral("Drawings"), right);
    auto* dg = new QGridLayout(draw);
    dg->setContentsMargins(8, 8, 8, 8);
    struct Side {
        const char* caption;
        bool mechanical;
    };
    const Side sides[] = {{"Schematic symbol", false}, {"Mechanical drawing", true}};
    for (int i = 0; i < 2; ++i) {
        auto* prev = new PartDrawingPreview(QString::fromLatin1(sides[i].caption), draw);
        if (sides[i].mechanical) mechPreview_ = prev;
        else schemPreview_ = prev;
        dg->addWidget(prev, 0, i);
        auto* btns = new QWidget(draw);
        auto* bl = new QHBoxLayout(btns);
        bl->setContentsMargins(0, 0, 0, 0);
        const bool mech = sides[i].mechanical;
        auto* cap = new QPushButton(QStringLiteral("Capture selection"), btns);
        cap->setToolTip(QStringLiteral(
            "Take what is selected on the sheet as this part's %1 drawing")
                            .arg(mech ? QStringLiteral("mechanical") : QStringLiteral("schematic")));
        connect(cap, &QPushButton::clicked, this, [this, mech] { captureFromSelection(mech); });
        bl->addWidget(cap);
        auto* ins = new QPushButton(QStringLiteral("Insert"), btns);
        ins->setToolTip(QStringLiteral("Put this drawing on the sheet — then click where it goes"));
        connect(ins, &QPushButton::clicked, this, [this, mech] { insertIntoDrawing(mech); });
        bl->addWidget(ins);
        auto* clr = new QPushButton(QStringLiteral("Clear"), btns);
        connect(clr, &QPushButton::clicked, this, [this, mech] { clearDrawing(mech); });
        bl->addWidget(clr);
        dg->addWidget(btns, 1, i);
    }
    rv->addWidget(draw);
    split->addWidget(right);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 2);

    auto* foot = new QHBoxLayout;
    summary_ = new QLabel(QString(), this);
    summary_->setStyleSheet(QStringLiteral("color:#8fb0d6;"));
    foot->addWidget(summary_, 1);
    auto* openBtn = new QPushButton(QStringLiteral("Open…"), this);
    openBtn->setToolTip(QStringLiteral("Open another part library (.plb)"));
    connect(openBtn, &QPushButton::clicked, this, [this] {
        const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Open part library"),
                                                          QFileInfo(path_).absolutePath(),
                                                          PartsLibrary::fileFilter());
        if (file.isEmpty()) return;
        saveLibrary(); // the one open now is kept before the other is loaded
        setLibraryPath(file);
    });
    foot->addWidget(openBtn);
    auto* saveAsBtn = new QPushButton(QStringLiteral("Save as…"), this);
    connect(saveAsBtn, &QPushButton::clicked, this, [this] {
        QString file = QFileDialog::getSaveFileName(this, QStringLiteral("Save part library"),
                                                    path_, PartsLibrary::fileFilter());
        if (file.isEmpty()) return;
        if (!file.endsWith(QStringLiteral(".plb"), Qt::CaseInsensitive))
            file += QStringLiteral(".plb");
        path_ = file;
        if (saveLibrary()) reloadTree(); // the footer names the file it is kept in
    });
    foot->addWidget(saveAsBtn);
    auto* saveBtn = new QPushButton(QStringLiteral("Save library"), this);
    connect(saveBtn, &QPushButton::clicked, this, [this] {
        if (saveLibrary())
            summary_->setText(QStringLiteral("Saved %1 part(s) to %2")
                                  .arg(lib_.parts.size())
                                  .arg(path_));
        else
            summary_->setText(QStringLiteral("Could not write %1").arg(path_));
    });
    foot->addWidget(saveBtn);
    auto* closeBtn = new QPushButton(QStringLiteral("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, [this] {
        saveLibrary(); // the library is a file, not a document: closing keeps it
        accept();
    });
    foot->addWidget(closeBtn);
    root->addLayout(foot);
}

void PartsLibraryDialog::reloadTree() {
    if (!tree_) return;
    const QString keep = currentId_;
    syncing_ = true;
    tree_->clear();
    const QString needle = filter_ ? filter_->text().trimmed() : QString();
    for (PartKind k : allPartKinds()) {
        const std::vector<const LibraryPart*> parts = lib_.ofKind(k);
        std::vector<const LibraryPart*> shown;
        for (const LibraryPart* p : parts) {
            if (needle.isEmpty() ||
                p->description.contains(needle, Qt::CaseInsensitive) ||
                p->mfrPn.contains(needle, Qt::CaseInsensitive) ||
                p->mfrName.contains(needle, Qt::CaseInsensitive))
                shown.push_back(p);
        }
        if (shown.empty()) continue; // a kind with nothing in it is not listed
        auto* head = new QTreeWidgetItem(tree_);
        head->setText(0, QStringLiteral("%1  (%2)").arg(partKindName(k)).arg(shown.size()));
        head->setFirstColumnSpanned(false);
        QFont f = head->font(0);
        f.setBold(true);
        head->setFont(0, f);
        for (const LibraryPart* p : shown) {
            auto* it = new QTreeWidgetItem(head);
            it->setText(0, p->title());
            it->setText(1, p->mfrName);
            it->setData(0, Qt::UserRole, p->id);
            // What the part already has drawn, so the list says which are done.
            QStringList have;
            if (p->hasSchematic()) have << QStringLiteral("schem");
            if (p->hasMechanical()) have << QStringLiteral("mech");
            if (!have.isEmpty()) it->setToolTip(0, QStringLiteral("has: ") + have.join(", "));
        }
        head->setExpanded(true);
    }
    syncing_ = false;
    if (!keep.isEmpty()) selectPart(keep);
    if (summary_)
        summary_->setText(QStringLiteral("%1 part(s) in %2").arg(lib_.parts.size()).arg(path_));
}

void PartsLibraryDialog::selectPart(const QString& id) {
    if (!tree_) return;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* head = tree_->topLevelItem(i);
        for (int j = 0; j < head->childCount(); ++j)
            if (head->child(j)->data(0, Qt::UserRole).toString() == id) {
                syncing_ = true;
                tree_->setCurrentItem(head->child(j));
                syncing_ = false;
                showPart(id);
                return;
            }
    }
    showPart(id); // not in the list (filtered out): still show it
}

LibraryPart* PartsLibraryDialog::current() { return lib_.byId(currentId_); }

void PartsLibraryDialog::showPart(const QString& id) {
    currentId_ = id;
    const LibraryPart* p = lib_.byId(id);
    syncing_ = true;
    const bool have = p != nullptr;
    if (kindBox_) {
        kindBox_->setEnabled(have);
        if (have) kindBox_->setCurrentIndex(std::max(0, kindBox_->findData(partKindKey(p->kind))));
    }
    if (descEdit_) {
        descEdit_->setEnabled(have);
        descEdit_->setText(have ? p->description : QString());
    }
    if (mfrEdit_) {
        mfrEdit_->setEnabled(have);
        mfrEdit_->setText(have ? p->mfrName : QString());
    }
    if (pnEdit_) {
        pnEdit_->setEnabled(have);
        pnEdit_->setText(have ? p->mfrPn : QString());
    }
    if (internalEdit_) {
        internalEdit_->setEnabled(have);
        internalEdit_->setText(have ? p->internalPn : QString());
    }
    if (unitBox_) {
        unitBox_->setEnabled(have);
        const QString unit = have ? (p->unit.isEmpty() ? defaultUnitForKind(p->kind) : p->unit)
                                  : QString();
        const int at = unitBox_->findData(unit);
        if (at >= 0) unitBox_->setCurrentIndex(at);
        else if (have) unitBox_->setCurrentText(unit);
    }
    if (schemPreview_) schemPreview_->setDrawing(have ? p->schematic : QJsonArray());
    if (mechPreview_) mechPreview_->setDrawing(have ? p->mechanical : QJsonArray());
    syncing_ = false;
    rebuildParamForm();
}

void PartsLibraryDialog::rebuildParamForm() {
    if (!paramLay_) return;
    paramWidgets_.clear();
    // Clear the form: the parameters belong to the KIND, so they are rebuilt whenever it changes.
    while (QLayoutItem* item = paramLay_->takeAt(0)) {
        if (QWidget* w = item->widget()) w->deleteLater();
        delete item;
    }
    LibraryPart* p = current();
    if (!p) return;
    syncing_ = true;
    for (const PartField& f : partFields(p->kind)) {
        QWidget* field = nullptr;
        switch (f.type) {
            case PartField::Type::Text:
            case PartField::Type::Number: {
                auto* le = new QLineEdit(paramHost_);
                le->setText(p->value(f.key));
                const QString key = f.key;
                connect(le, &QLineEdit::textChanged, this, [this, key](const QString& t) {
                    if (syncing_) return;
                    if (LibraryPart* cp = current()) { cp->params[key] = t; touched(); }
                });
                paramWidgets_.insert(f.key, le);
                field = le;
                break;
            }
            case PartField::Type::Bool: {
                auto* cb = new QCheckBox(QStringLiteral("yes"), paramHost_);
                cb->setChecked(p->value(f.key) == QLatin1String("1"));
                const QString key = f.key;
                connect(cb, &QCheckBox::toggled, this, [this, key](bool on) {
                    if (syncing_) return;
                    if (LibraryPart* cp = current()) {
                        cp->params[key] = on ? QStringLiteral("1") : QStringLiteral("0");
                        touched();
                    }
                });
                paramWidgets_.insert(f.key, cb);
                field = cb;
                break;
            }
            case PartField::Type::Choice: {
                auto* box = new QComboBox(paramHost_);
                box->setEditable(true); // a part may call for something not on the list
                box->addItems(f.choices);
                box->setCurrentText(p->value(f.key));
                const QString key = f.key;
                connect(box, &QComboBox::currentTextChanged, this, [this, key](const QString& t) {
                    if (syncing_) return;
                    if (LibraryPart* cp = current()) { cp->params[key] = t; touched(); }
                });
                paramWidgets_.insert(f.key, box);
                field = box;
                break;
            }
            case PartField::Type::Range: {
                // Two halves of one parameter: a crimp fits a RANGE of studs, a boot a range of
                // diameters. Stored as "<key>Min" / "<key>Max".
                auto* host = new QWidget(paramHost_);
                auto* hl = new QHBoxLayout(host);
                hl->setContentsMargins(0, 0, 0, 0);
                hl->setSpacing(4);
                for (int half = 0; half < 2; ++half) {
                    const QString key = f.key + (half == 0 ? QStringLiteral("Min")
                                                           : QStringLiteral("Max"));
                    auto* le = new QLineEdit(host);
                    le->setPlaceholderText(half == 0 ? QStringLiteral("min")
                                                     : QStringLiteral("max"));
                    le->setText(p->value(key));
                    connect(le, &QLineEdit::textChanged, this, [this, key](const QString& t) {
                        if (syncing_) return;
                        if (LibraryPart* cp = current()) { cp->params[key] = t; touched(); }
                    });
                    paramWidgets_.insert(key, le);
                    if (half == 1) {
                        auto* dash = new QLabel(QStringLiteral("to"), host);
                        dash->setStyleSheet(QStringLiteral("color:#7f8896;"));
                        hl->addWidget(dash);
                    }
                    hl->addWidget(le, 1);
                }
                field = host;
                break;
            }
        }
        if (field) paramLay_->addWidget(rowHost(paramHost_, f.label, field, f.unit));
    }
    paramLay_->addStretch(1);
    syncing_ = false;
}

void PartsLibraryDialog::touched() {
    // The list shows the description and P/N, so it follows an edit; the file is written on Save
    // and on Close, not on every keystroke.
    if (syncing_) return;
    if (!tree_) return;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* head = tree_->topLevelItem(i);
        for (int j = 0; j < head->childCount(); ++j)
            if (head->child(j)->data(0, Qt::UserRole).toString() == currentId_) {
                if (const LibraryPart* p = lib_.byId(currentId_)) {
                    head->child(j)->setText(0, p->title());
                    head->child(j)->setText(1, p->mfrName);
                }
                return;
            }
    }
}

LibraryPart* PartsLibraryDialog::addPart(PartKind kind) {
    LibraryPart& p = lib_.add(kind);
    const QString id = p.id;
    reloadTree();
    selectPart(id);
    if (summary_)
        summary_->setText(QStringLiteral("Added a %1 — fill in its description and P/N")
                              .arg(partKindName(kind).toLower()));
    return lib_.byId(id);
}

void PartsLibraryDialog::deleteCurrentPart() {
    const LibraryPart* p = lib_.byId(currentId_);
    if (!p) return;
    const QString title = p->title();
    lib_.remove(currentId_);
    currentId_.clear();
    reloadTree();
    showPart(lib_.parts.empty() ? QString() : lib_.parts.front().id);
    if (summary_) summary_->setText(QStringLiteral("Deleted %1").arg(title));
}

void PartsLibraryDialog::duplicateCurrentPart() {
    if (LibraryPart* copy = lib_.duplicate(currentId_)) {
        const QString id = copy->id;
        reloadTree();
        selectPart(id);
        if (summary_) summary_->setText(QStringLiteral("Duplicated"));
    }
}

void PartsLibraryDialog::captureFromSelection(bool mechanical) {
    LibraryPart* p = current();
    if (!p) return;
    if (!editor_) return;
    const std::vector<Entity> sel = editor_->copyOfSelection();
    if (sel.empty()) {
        if (summary_)
            summary_->setText(QStringLiteral(
                "Select the objects on the sheet first — then capture them as the drawing"));
        return;
    }
    const QJsonArray drawing = partDrawingFromEntities(sel);
    if (mechanical) {
        p->mechanical = drawing;
        if (mechPreview_) mechPreview_->setDrawing(drawing);
    } else {
        p->schematic = drawing;
        if (schemPreview_) schemPreview_->setDrawing(drawing);
    }
    if (summary_)
        summary_->setText(QStringLiteral("Captured %1 object(s) as the %2 drawing")
                              .arg(sel.size())
                              .arg(mechanical ? QStringLiteral("mechanical")
                                              : QStringLiteral("schematic")));
}

void PartsLibraryDialog::insertIntoDrawing(bool mechanical) {
    LibraryPart* p = current();
    if (!p || !editor_) return;
    const QJsonArray& drawing = mechanical ? p->mechanical : p->schematic;
    if (drawing.isEmpty()) {
        if (summary_)
            summary_->setText(QStringLiteral("Nothing captured for that side yet"));
        return;
    }
    editor_->placeEntities(partDrawingEntities(drawing));
    if (summary_)
        summary_->setText(QStringLiteral("Click the sheet to place %1").arg(p->title()));
    // The click that places it happens on the canvas, so the library gets out of the way.
    hide();
    saveLibrary();
}

void PartsLibraryDialog::clearDrawing(bool mechanical) {
    LibraryPart* p = current();
    if (!p) return;
    if (mechanical) {
        p->mechanical = QJsonArray();
        if (mechPreview_) mechPreview_->setDrawing(QJsonArray());
    } else {
        p->schematic = QJsonArray();
        if (schemPreview_) schemPreview_->setDrawing(QJsonArray());
    }
    if (summary_) summary_->setText(QStringLiteral("Drawing cleared"));
}

bool PartsLibraryDialog::saveLibrary() { return lib_.save(path_); }

} // namespace Edraw_Desktop
