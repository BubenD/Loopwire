#include "app/MechanicalValidationDialog.h"

#include "app/Editor.h"
#include "model/Document.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace Edraw_Desktop {

MechanicalValidationDialog::MechanicalValidationDialog(Editor* editor, QWidget* parent)
    : QDialog(parent), editor_(editor) {
    setWindowTitle(QStringLiteral("Mechanical compatibility"));
    resize(1000, 680);
    catalog_.openRemembered();
    buildUi();
    readFromDrawing();
}

void MechanicalValidationDialog::buildUi() {
    auto* root = new QVBoxLayout(this);

    auto* head = new QHBoxLayout;
    auto* readBtn = new QPushButton(QStringLiteral("Read from the drawing"), this);
    readBtn->setToolTip(QStringLiteral(
        "Take the harness off the sheet - a branch to each connector, the conductors routed into "
        "them - and check what is fitted to it"));
    connect(readBtn, &QPushButton::clicked, this, [this] { readFromDrawing(); });
    head->addWidget(readBtn);
    auto* againBtn = new QPushButton(QStringLiteral("Check again"), this);
    againBtn->setToolTip(QStringLiteral(
        "Re-run the checks after a gauge or a part has been changed - the answers follow the drawing"));
    connect(againBtn, &QPushButton::clicked, this, [this] { revalidate(); });
    head->addWidget(againBtn);
    showNotes_ = new QCheckBox(QStringLiteral("Show notes as well"), this);
    showNotes_->setChecked(true);
    connect(showNotes_, &QCheckBox::toggled, this, [this](bool) { refreshTable(); });
    head->addWidget(showNotes_);
    head->addStretch(1);
    root->addLayout(head);

    summary_ = new QLabel(QString(), this);
    summary_->setWordWrap(true);
    summary_->setStyleSheet(QStringLiteral("color:#8fb0d6;font-weight:600;font-size:14px;"));
    root->addWidget(summary_);

    table_ = new QTableWidget(this);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels({QStringLiteral("What"), QStringLiteral("Where"),
                                       QStringLiteral("Figure"), QStringLiteral("What it says")});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    root->addWidget(table_, 2);

    detail_ = new QTextBrowser(this);
    root->addWidget(detail_, 1);

    auto* foot = new QHBoxLayout;
    foot->addStretch(1);
    auto* closeBtn = new QPushButton(QStringLiteral("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    foot->addWidget(closeBtn);
    root->addLayout(foot);
}

void MechanicalValidationDialog::setDefinition(const MechHarnessDef& def) {
    def_ = def;
    revalidate();
}

void MechanicalValidationDialog::readFromDrawing() {
    if (!editor_ || !editor_->document()) return;
    def_ = mechFromDrawing(*editor_->document());
    revalidate();
}

void MechanicalValidationDialog::revalidate() {
    issues_ = validateMechanicalHarness(def_, &catalog_);
    refreshTable();
    refreshExplanation();
}

int MechanicalValidationDialog::countOf(MechIssueKind kind) const {
    int n = 0;
    for (const MechIssue& i : issues_)
        if (i.kind == kind) ++n;
    return n;
}

void MechanicalValidationDialog::refreshTable() {
    const int violations = countOf(MechIssueKind::Violation);
    const int gaps = countOf(MechIssueKind::Gap);
    const int notes = countOf(MechIssueKind::Warning);
    if (summary_) {
        if (def_.segments.empty())
            summary_->setText(QStringLiteral(
                "Nothing to check: draw the harness, or read a definition in from the drawing."));
        else if (violations == 0 && gaps == 0)
            summary_->setText(QStringLiteral(
                "%1 run(s) checked — everything fitted will go on, and nothing is missing.")
                                  .arg(def_.segments.size()));
        else
            summary_->setText(QStringLiteral(
                "%1 will not fit, %2 figure(s) missing, %3 note(s), over %4 run(s).")
                                  .arg(violations)
                                  .arg(gaps)
                                  .arg(notes)
                                  .arg(def_.segments.size()));
        summary_->setStyleSheet(violations > 0
                                    ? QStringLiteral("color:#e06c6c;font-weight:600;font-size:14px;")
                                : gaps > 0
                                    ? QStringLiteral("color:#e6b45a;font-weight:600;font-size:14px;")
                                    : QStringLiteral("color:#7ee0a0;font-weight:600;font-size:14px;"));
    }
    if (!table_) return;

    // Worst first: what will not fit, then what is not known, then what is merely worth saying.
    std::vector<const MechIssue*> shown;
    for (MechIssueKind kind :
         {MechIssueKind::Violation, MechIssueKind::Gap, MechIssueKind::Warning}) {
        if (kind == MechIssueKind::Warning && showNotes_ && !showNotes_->isChecked()) continue;
        for (const MechIssue& i : issues_)
            if (i.kind == kind) shown.push_back(&i);
    }

    table_->setRowCount(static_cast<int>(shown.size()));
    for (int r = 0; r < static_cast<int>(shown.size()); ++r) {
        const MechIssue& i = *shown[static_cast<size_t>(r)];
        const QString what = i.kind == MechIssueKind::Violation ? QStringLiteral("will not fit")
                             : i.kind == MechIssueKind::Gap     ? QStringLiteral("missing")
                                                                : QStringLiteral("note");
        const QColor colour = i.kind == MechIssueKind::Violation ? QColor("#e06c6c")
                              : i.kind == MechIssueKind::Gap     ? QColor("#e6b45a")
                                                                 : QColor("#9fb4c8");
        const QString cells[4] = {what, i.objectName, i.property, i.message};
        for (int c = 0; c < 4; ++c) {
            auto* item = new QTableWidgetItem(cells[c]);
            item->setForeground(colour);
            table_->setItem(r, c, item);
        }
    }
    table_->resizeColumnsToContents();
}

void MechanicalValidationDialog::refreshExplanation() {
    if (!detail_) return;
    QString html = QStringLiteral(
        "<style>body{color:#d7dade;} h4{color:#8fb0d6;margin-bottom:4px;} p{color:#c3c9d1;} "
        "li{color:#c3c9d1;} th{color:#8fb0d6;}</style>");

    // The bundle each run makes, because every fit check is against that figure and nothing else.
    if (!def_.segments.empty()) {
        html += QStringLiteral(
            "<h4>What each run is, and what has to fit over it</h4>"
            "<table cellpadding='4'><tr><th align='left'>Run</th><th align='right'>Conductors</th>"
            "<th align='right'>Bundle Ø</th><th align='left'>Fitted</th></tr>");
        for (const MechSegment& s : def_.segments) {
            const MechNode* to = def_.node(s.toNode);
            QStringList fitted;
            for (const MechFitting& f : s.coverings)
                fitted << (f.name.isEmpty() ? partKindName(f.kind) : f.name);
            if (to)
                for (const MechFitting& f : to->fittings)
                    fitted << (f.name.isEmpty() ? partKindName(f.kind) : f.name);
            html += QStringLiteral("<tr><td>%1</td><td align='right'>%2</td>"
                                   "<td align='right'>%3 mm</td><td>%4</td></tr>")
                        .arg((to ? to->name : s.id).toHtmlEscaped())
                        .arg(s.wireIds.size())
                        .arg(segmentBundleDiameterMm(def_, s), 0, 'f', 2)
                        .arg(fitted.isEmpty() ? QStringLiteral("—")
                                              : fitted.join(QStringLiteral(", ")).toHtmlEscaped());
        }
        html += QStringLiteral("</table>");
    }

    html += QStringLiteral("<h4>What is checked</h4><ul>");
    html += QStringLiteral(
        "<li><b>Bundle fit</b> — the bundle each run makes against the entry the part takes. A "
        "backshell, a boot and a gland are entered from the cable side; a sleeve, a braid and a "
        "conduit are sized by the bundle itself.</li>");
    html += QStringLiteral(
        "<li><b>Recovery</b> — heatshrink has to be able to go over the bundle AND to shrink onto "
        "it. Too small a bundle is as much a failure as too big a one, which is why the message for "
        "shrinkable parts says \"recovers to\" rather than \"needs at least\".</li>");
    html += QStringLiteral(
        "<li><b>The figures themselves</b> — a run with no length, a conductor with neither a "
        "cross-section nor a diameter, a part that does not state what it fits over, a part that is "
        "not in any connected library.</li>");
    html += QStringLiteral(
        "<li><b>The topology</b> — a run that does not join two points, a branch hanging off a run "
        "that leads nowhere, a run routing a conductor that is not in the list.</li>");
    html += QStringLiteral("</ul>");

    html += QStringLiteral("<h4>Why the three kinds are kept apart</h4>");
    html += QStringLiteral(
        "<p><b>Will not fit</b> is wrong: the parts as chosen cannot be assembled. <b>Missing</b> is "
        "unfinished: the check could not be made at all, and it is reported rather than assumed, "
        "because a fit check on a guessed entry size is exactly the sort of figure that gets a "
        "harness built wrong. A <b>note</b> is neither — worth knowing, nothing to do.</p>");
    html += QStringLiteral(
        "<p>Every diameter here is worked out from the conductors routed through that run, so "
        "changing a gauge changes what fits. Check again after any change.</p>");
    html += QStringLiteral(
        "<p><b>Not checked here:</b> a screen or a braid adds to the bundle before a boot goes over "
        "it; bend radius; how much a bundle flattens under a tie; and whether a part is right for "
        "the environment. Those are still yours to judge.</p>");
    detail_->setHtml(html);
}

} // namespace Edraw_Desktop
