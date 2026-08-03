#include "app/PartViewDialog.h"

#include "app/DrawingPreview.h"
#include "harness/CableSpec.h"
#include "harness/Datasheet.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

namespace Edraw_Desktop {

PartViewDialog::PartViewDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Part"));
    resize(880, 520);
    auto* root = new QVBoxLayout(this);

    title_ = new QLabel(QString(), this);
    title_->setWordWrap(true);
    title_->setStyleSheet(QStringLiteral("color:#e6e9ef; font-size:13px; font-weight:600;"));
    root->addWidget(title_);

    auto* row = new QHBoxLayout;
    {
        auto* box = new QGroupBox(QStringLiteral("Schematic symbol"), this);
        auto* v = new QVBoxLayout(box);
        schematic_ = new DrawingPreview(QStringLiteral("Schematic"), box);
        schematic_->setEmptyText(QStringLiteral("no schematic symbol"));
        schematic_->setMinimumSize(300, 260);
        v->addWidget(schematic_);
        row->addWidget(box, 1);
    }
    {
        auto* box = new QGroupBox(QStringLiteral("Mechanical drawing"), this);
        auto* v = new QVBoxLayout(box);
        mechanical_ = new DrawingPreview(QStringLiteral("Mechanical"), box);
        mechanical_->setEmptyText(QStringLiteral("no mechanical drawing"));
        mechanical_->setMinimumSize(300, 260);
        v->addWidget(mechanical_);
        row->addWidget(box, 1);
    }
    root->addLayout(row, 1);

    data_ = new QLabel(QString(), this);
    data_->setWordWrap(true);
    data_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    data_->setStyleSheet(QStringLiteral("color:#9fb4c8;"));
    root->addWidget(data_);

    auto* foot = new QHBoxLayout;
    foot->addStretch(1);
    auto* close = new QPushButton(QStringLiteral("Close"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    foot->addWidget(close);
    root->addLayout(foot);
}

void PartViewDialog::setPart(const LibraryPart& part, Show which) {
    setWindowTitle(QStringLiteral("%1 — %2").arg(partKindName(part.kind), part.title()));
    QStringList head;
    head << part.title();
    if (!part.mfrName.isEmpty()) head << part.mfrName;
    title_->setText(head.join(QStringLiteral("   ·   ")));

    // The symbol a schematic uses is the part's own when it has one and the standard one for its kind
    // when it has not, which is what will actually be drawn - so that is what is shown.
    if (schematic_) schematic_->setDrawing(partSchematicDrawing(part));
    if (mechanical_) mechanical_->setDrawing(part.mechanical);

    // What the library knows: the numbers, then the parameters that are filled in, then the files.
    QStringList lines;
    QStringList ids;
    if (!part.mfrPn.isEmpty()) ids << QStringLiteral("P/N %1").arg(part.mfrPn);
    if (!part.internalPn.isEmpty()) ids << QStringLiteral("internal %1").arg(part.internalPn);
    if (!part.unit.isEmpty()) ids << QStringLiteral("bought by the %1").arg(part.unit);
    if (!ids.isEmpty()) lines << ids.join(QStringLiteral("   ·   "));
    if (!part.description.isEmpty() && part.description != part.title())
        lines << part.description;
    // A cable says what it is made of, which is the whole point of asking about a cable.
    if (part.kind == PartKind::Cable) {
        const CableSpec spec = cableSpecOf(part);
        if (!spec.isEmpty())
            lines << QStringLiteral("%1 — %2 conductor(s)%3")
                         .arg(spec.summary())
                         .arg(spec.conductorCount())
                         .arg(spec.screened() ? QStringLiteral(", screened") : QString());
    }
    QStringList params;
    for (const PartField& f : partFields(part.kind)) {
        // A Range field is two values; everything else is one.
        if (f.type == PartField::Type::Range) {
            const QString lo = part.value(f.key + QStringLiteral("Min"));
            const QString hi = part.value(f.key + QStringLiteral("Max"));
            if (lo.isEmpty() && hi.isEmpty()) continue;
            params << QStringLiteral("%1 %2–%3 %4").arg(f.label, lo, hi, f.unit).trimmed();
            continue;
        }
        const QString v = part.value(f.key).trimmed();
        if (v.isEmpty() || v == QLatin1String("0")) continue;
        // The make-up is JSON and is already said in words above; a yes/no reads as its label.
        if (f.key == QLatin1String("structure") && part.kind == PartKind::Cable) continue;
        if (f.type == PartField::Type::Bool) {
            if (v == QLatin1String("1")) params << f.label;
            continue;
        }
        params << QStringLiteral("%1 %2 %3").arg(f.label, v, f.unit).trimmed();
    }
    if (!params.isEmpty()) lines << params.join(QStringLiteral("   ·   "));
    if (part.hasDatasheet()) {
        QStringList sheets;
        for (const QString& s : part.datasheets)
            sheets << DatasheetStore::originalName(s) +
                          (DatasheetStore::exists(s) ? QString()
                                                     : QStringLiteral(" (missing)"));
        lines << QStringLiteral("Datasheets: %1").arg(sheets.join(QStringLiteral(", ")));
    }
    data_->setText(lines.join(QLatin1Char('\n')));

    // Which drawing was asked about gets the eye: it is fitted, the other left as it is.
    if (which == Show::Mechanical && mechanical_) mechanical_->fitToView();
    else if (which == Show::Schematic && schematic_) schematic_->fitToView();
}

void PartViewDialog::showPart(const LibraryPart& part, Show which, QWidget* parent) {
    // One window, re-used: a run of right-clicks should not bury the screen.
    static QPointer<PartViewDialog> dlg;
    if (!dlg) dlg = new PartViewDialog(parent);
    dlg->setPart(part, which);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

bool PartViewDialog::showPartById(const QString& partId, Show which, QWidget* parent) {
    if (partId.trimmed().isEmpty()) return false;
    PartsCatalog catalog;
    catalog.openRemembered();
    const LibraryPart* found = catalog.byId(partId);
    if (!found) return false;
    showPart(*found, which, parent);
    return true;
}

} // namespace Edraw_Desktop
