#include "app/MechanicalValidationDialog.h"

#include "app/Editor.h"
#include "app/RuleExplainer.h"
#include "harness/Bom.h"
#include "model/Document.h"
#include "model/DrawingDetails.h"

#include <QDialogButtonBox>
#include <QHash>
#include <QSet>
#include <QJsonObject>
#include <QScrollArea>

#include <QCheckBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTextBrowser>
#include <QToolButton>
#include <QVBoxLayout>

namespace Loopwire {

MechanicalValidationDialog::MechanicalValidationDialog(Editor* editor, QWidget* parent)
    : QDialog(parent), editor_(editor) {
    setWindowTitle(QStringLiteral("DRC - harness design rules and compatibility"));
    setObjectName(QStringLiteral("harnessDrc"));
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
    auto* rulesBtn = new QPushButton(QStringLiteral("Rules…"), this);
    rulesBtn->setObjectName(QStringLiteral("harnessDrcRules"));
    rulesBtn->setToolTip(QStringLiteral("Choose which harness design rules are checked"));
    connect(rulesBtn, &QPushButton::clicked, this, [this] { showRules(); });
    head->addWidget(rulesBtn);
    auto* bypassBtn = new QPushButton(QStringLiteral("Bypass table"), this);
    bypassBtn->setObjectName(QStringLiteral("harnessDrcBypass"));
    bypassBtn->setToolTip(QStringLiteral("The findings you have bypassed (accepted)"));
    connect(bypassBtn, &QPushButton::clicked, this, [this] { showBypass(); });
    head->addWidget(bypassBtn);
    root->addLayout(head);

    // THE HARNESS WIZARD'S HARNESSES: the logical design rules and compatibility, finding by finding.
    harnessSummary_ = new QLabel(QString(), this);
    harnessSummary_->setWordWrap(true);
    root->addWidget(harnessSummary_);
    harnessTable_ = new QTableWidget(this);
    harnessTable_->setObjectName(QStringLiteral("harnessDrcTable"));
    harnessTable_->setColumnCount(5);
    harnessTable_->setHorizontalHeaderLabels({QStringLiteral("What"), QStringLiteral("Rule"), QStringLiteral("Harness"),
                                              QStringLiteral("Finding"), QString()});
    harnessTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    harnessTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    harnessTable_->verticalHeader()->setVisible(false);
    harnessTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    harnessTable_->setToolTip(QStringLiteral("Double-click a finding to go to its part on the drawing"));
    // DOUBLE-CLICKED: to the part on the drawing.
    connect(harnessTable_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row < 0 || row >= static_cast<int>(harnessRows_.size())) return;
        const BrickIssue& i = harnessRows_[static_cast<size_t>(row)];
        if(i.rule==QLatin1String("drawing-fields")){
            QString layout;const Document* owner=editor_?editor_->drawingOwner():nullptr;
            if(owner)for(const DrawingFieldIssue& field:checkDrawingDetails(*owner))if(field.key==i.key){layout=field.layout;break;}
            emit drawingFieldLocateRequested(i.brickId,layout,i.atX,i.atY);
        } else emit locateRequested(i.harness, i.brickId, i.atX, i.atY, i.located);
    });
    root->addWidget(harnessTable_, 3);

    auto* fitLabel = new QLabel(QStringLiteral("On the drawing: what is fitted against the bundle it goes on"), this);
    fitLabel->setStyleSheet(QStringLiteral("color:#8fb0d6;font-weight:600;"));
    root->addWidget(fitLabel);
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
    if (!editor_ || !editor_->drawingOwner()) return;
    def_ = mechFromDrawing(*editor_->drawingOwner());
    revalidate();
}

void MechanicalValidationDialog::revalidate() {
    issues_ = validateMechanicalHarness(def_, &catalog_);
    refreshTable();
    refreshExplanation();
    checkHarnesses();
    refreshHarnessTable();
    bool passed=shownHarnessIssues().empty() && countOf(MechIssueKind::Violation)==0 && countOf(MechIssueKind::Gap)==0;
    const bool checked=!def_.segments.empty() || (openDesign_&&!openDesign_->bricks.empty()) ||
        (editor_&&editor_->drawingOwner()&&!editor_->drawingOwner()->wizards().value(QStringLiteral("harnesses")).toObject().isEmpty());
    if(editor_&&editor_->drawingOwner()){
        editor_->drawingOwner()->recordValidation(QStringLiteral("mechanical"),passed&&checked);
        emit editor_->validationStatusChanged();
    }
}

void MechanicalValidationDialog::setOpenHarness(const BrickDesign* design, const QString& name) {
    openDesign_ = design;
    openName_ = name.trimmed().isEmpty() ? QStringLiteral("(open in the wizard)") : name.trimmed();
}

int MechanicalValidationDialog::bomItemOf(const QString& partId) const {
    const Document* doc = editor_ ? editor_->drawingOwner() : nullptr;
    const LibraryPart* p = merged_.byId(partId);
    if (!doc || !p) return 0;
    return bomItemLookup(*doc, merged_)(partId, p->kind, p->description);
}

void MechanicalValidationDialog::checkHarnesses() {
    harnessIssues_.clear();
    merged_ = catalog_.merged();
    const auto bom = [this](const QString& id) { return bomItemOf(id); };
    auto check = [&](const BrickDesign& from, const QString& name) {
        BrickDesign d = from;
        d.catalog = &catalog_;
        for (BrickIssue& i : brickDrc(d, name, merged_, bom)) harnessIssues_.push_back(std::move(i));
    };
    QStringList done;
    if (openDesign_ && !openDesign_->bricks.empty()) {
        check(*openDesign_, openName_);
        done << openName_;
    }
    const Document* doc = editor_ ? editor_->drawingOwner() : nullptr;
    if (!doc) return;
    if(const Document* owner=editor_->drawingOwner())for(const DrawingFieldIssue& field:checkDrawingDetails(*owner))
        harnessIssues_.push_back({field.key,QStringLiteral("drawing-fields"),QStringLiteral("Drawing details"),field.entityId,field.message,2,true,field.at.x,field.at.y});
    const QJsonObject saved = doc->wizards().value(QStringLiteral("harnesses")).toObject();
    for (auto it = saved.begin(); it != saved.end(); ++it)
        if (!done.contains(it.key())) check(BrickDesign::fromJson(it.value().toObject()), it.key());
    // AND AS PLACED ON THE SHEET: a part moved off its wires there.
    for (BrickIssue& i : drawingConnectivity(doc->entities())) harnessIssues_.push_back(std::move(i));
    // AND EVERY SLEEVE DRAWN ON IT against the part linked to it - by a balloon to its bill line, or its own.
    for (BrickIssue& i : drawingSleeveParts(*doc, merged_)) harnessIssues_.push_back(std::move(i));
}

int MechanicalValidationDialog::severityOf(const QString& rule) const {
    int asItComes = 2;
    for (const BrickDrcRule& r : brickDrcRules())
        if (r.id == rule) asItComes = r.severity;
    return editor_ ? editor_->drcSeverity(rule, asItComes) : asItComes;
}

std::vector<BrickIssue> MechanicalValidationDialog::shownHarnessIssues() const {
    std::vector<BrickIssue> out;
    for (const BrickIssue& i : harnessIssues_) {
        // AT ITS RULE'S SEVERITY: a fail, a warning - or, set to pass, not shown at all.
        const int severity = severityOf(i.rule);
        if (severity == 0 || (editor_ && editor_->drcBypassed().contains(i.key))) continue;
        out.push_back(i);
        out.back().level = severity;
    }
    // Worst first.
    std::stable_sort(out.begin(), out.end(), [](const BrickIssue& a, const BrickIssue& b) { return a.level > b.level; });
    return out;
}

void MechanicalValidationDialog::refreshHarnessTable() {
    if (!harnessTable_) return;
    const std::vector<BrickIssue> shown = shownHarnessIssues();
    int wrong = 0, check = 0, bypassed = 0;
    QSet<QString> harnesses;
    for (const BrickIssue& i : harnessIssues_) {
        harnesses.insert(i.harness);
        if (editor_ && editor_->drcBypassed().contains(i.key)) ++bypassed;
    }
    for (const BrickIssue& i : shown) (i.level >= 2 ? wrong : check)++;
    if (harnessSummary_) {
        harnessSummary_->setText(
            harnessIssues_.empty() && (!openDesign_ || openDesign_->bricks.empty()) &&
                    !(editor_ && editor_->drawingOwner() &&
                      !editor_->drawingOwner()->wizards().value(QStringLiteral("harnesses")).toObject().isEmpty())
                ? QStringLiteral("Harness wizard: no harness to check yet - build one in the Harness build wizard.")
            : shown.empty()
                ? QStringLiteral("Harness wizard: every rule passes%1.")
                      .arg(bypassed ? QStringLiteral(" (%1 bypassed)").arg(bypassed) : QString())
                : QStringLiteral("Harness wizard: %1 fail, %2 warning%3 - double-click to go to it, Bypass to accept one.")
                      .arg(wrong)
                      .arg(check)
                      .arg(bypassed ? QStringLiteral(", %1 bypassed").arg(bypassed) : QString()));
        harnessSummary_->setStyleSheet(wrong > 0   ? QStringLiteral("color:#e06c6c;font-weight:600;font-size:14px;")
                                       : check > 0 ? QStringLiteral("color:#e6b45a;font-weight:600;font-size:14px;")
                                                   : QStringLiteral("color:#7ee0a0;font-weight:600;font-size:14px;"));
    }
    QHash<QString, QString> labelOf;
    for (const BrickDrcRule& r : brickDrcRules()) labelOf.insert(r.id, r.label);
    harnessRows_ = shown;
    harnessTable_->setRowCount(0);
    harnessTable_->setRowCount(static_cast<int>(shown.size()));
    for (int row = 0; row < static_cast<int>(shown.size()); ++row) {
        const BrickIssue& i = shown[static_cast<size_t>(row)];
        const QColor colour = i.level >= 2 ? QColor("#e06c6c") : i.level == 1 ? QColor("#e6b45a") : QColor("#9fb4c8");
        const QString cells[4] = {i.level >= 2 ? QStringLiteral("fail") : i.level == 1 ? QStringLiteral("warning")
                                                                                        : QStringLiteral("note"),
                                  labelOf.value(i.rule, i.rule), i.harness, i.message};
        for (int c = 0; c < 4; ++c) {
            auto* item = new QTableWidgetItem(cells[c]);
            item->setForeground(colour);
            if (c == 1) item->setToolTip(labelOf.value(i.rule, i.rule));
            harnessTable_->setItem(row, c, item);
        }
        auto* by = new QPushButton(QStringLiteral("Bypass"), harnessTable_);
        by->setObjectName(QStringLiteral("harnessDrcBypassRow"));
        const QString key = i.key;
        connect(by, &QPushButton::clicked, this, [this, key] {
            if (editor_) editor_->drcBypass(key);
            refreshHarnessTable();
            if (bypassDlg_) showBypass();
        });
        harnessTable_->setCellWidget(row, 4, by);
    }
    harnessTable_->resizeColumnToContents(0);
    harnessTable_->resizeColumnToContents(1);
    harnessTable_->resizeColumnToContents(2);
}

void MechanicalValidationDialog::showRules() {
    if (rulesDlg_) rulesDlg_->close();
    auto* dlg = new QDialog(this);
    rulesDlg_ = dlg;
    dlg->setObjectName(QStringLiteral("harnessDrcRulesDialog"));
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &QObject::destroyed, this, [this] { rulesDlg_ = nullptr; });
    dlg->setWindowTitle(QStringLiteral("Harness DRC rules"));
    dlg->resize(560, 700);
    auto* lay = new QVBoxLayout(dlg);
    lay->addWidget(new QLabel(QStringLiteral("Each rule's severity - click its square to change it. The harness DRC's own "
                                             "rules, apart from the schematic's:"),
                              dlg));
    // THE SQUARE OF A SEVERITY: red a fail, yellow a warning, green a pass (not checked).
    auto paint = &RuleExplainer::paintSeverity;
    // THE LEGEND.
    {
        auto* legend = new QHBoxLayout;
        for (int level : {2, 1, 0}) {
            auto* sq = new QToolButton(dlg);
            sq->setFixedSize(14, 14);
            sq->setEnabled(false);
            paint(sq, level);
            legend->addWidget(sq);
            legend->addWidget(new QLabel(level >= 2 ? QStringLiteral("Fail (error)")
                                         : level == 1 ? QStringLiteral("Warning")
                                                      : QStringLiteral("Pass (not checked)"),
                                         dlg));
            legend->addSpacing(14);
        }
        legend->addStretch(1);
        lay->addLayout(legend);
        RuleExplainer::addHelpToggle(lay, dlg);
    }
    for (const QString group : {QStringLiteral("Logical"), QStringLiteral("Compatibility")}) {
        auto* box = new QGroupBox(group == QLatin1String("Logical") ? QStringLiteral("Logical DRC")
                                                                    : QStringLiteral("Compatibility validation"),
                                  dlg);
        auto* bl = new QVBoxLayout(box);
        bl->setSpacing(3);
        for (const BrickDrcRule& r : brickDrcRules()) {
            if (r.group != group) continue;
            auto* row = new QHBoxLayout;
            auto* sq = new QToolButton(box);
            sq->setObjectName(QStringLiteral("harnessDrcRule_") + r.id);
            sq->setFixedSize(14, 14);
            sq->setCursor(Qt::PointingHandCursor);
            paint(sq, severityOf(r.id));
            // PRESSED: fail, then warning, then pass, then fail again.
            const QString id = r.id;
            connect(sq, &QToolButton::clicked, this, [this, sq, id, paint] {
                const int now = sq->property("severity").toInt();
                const int next = now >= 2 ? 1 : now == 1 ? 0 : 2;
                if (editor_) editor_->setDrcSeverity(id, next);
                paint(sq, next);
                refreshHarnessTable();
            });
            row->addWidget(sq);
            auto* name = new QLabel(r.label, box);
            name->setObjectName(QStringLiteral("harnessDrcRuleName_") + r.id);
            // HOVERED: the fault and the fix, animated, with what the rule means.
            RuleExplainer::attach(name, r.id, r.label, r.description);
            row->addWidget(name, 1);
            bl->addLayout(row);
        }
        lay->addWidget(box);
    }
    lay->addStretch(1);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    connect(box, &QDialogButtonBox::rejected, dlg, &QDialog::close);
    lay->addWidget(box);
    dlg->show();
    dlg->raise();
}

void MechanicalValidationDialog::showBypass() {
    if (bypassDlg_) bypassDlg_->close();
    std::vector<BrickIssue> by;
    for (const BrickIssue& i : harnessIssues_)
        if (editor_ && editor_->drcBypassed().contains(i.key)) by.push_back(i);
    auto* dlg = new QDialog(this);
    bypassDlg_ = dlg;
    dlg->setObjectName(QStringLiteral("harnessDrcBypassDialog"));
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &QObject::destroyed, this, [this] { bypassDlg_ = nullptr; });
    dlg->setWindowTitle(QStringLiteral("Harness DRC - bypassed findings"));
    dlg->resize(640, 360);
    auto* lay = new QVBoxLayout(dlg);
    lay->addWidget(new QLabel(by.empty() ? QStringLiteral("No bypassed findings.")
                                         : QStringLiteral("%1 bypassed finding(s) - Restore to flag one again.").arg(by.size()),
                              dlg));
    auto* table = new QTableWidget(static_cast<int>(by.size()), 3, dlg);
    table->setObjectName(QStringLiteral("harnessDrcBypassTable"));
    table->setHorizontalHeaderLabels({QStringLiteral("Harness"), QStringLiteral("Bypassed finding"), QString()});
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    for (int r = 0; r < static_cast<int>(by.size()); ++r) {
        table->setItem(r, 0, new QTableWidgetItem(by[static_cast<size_t>(r)].harness));
        table->setItem(r, 1, new QTableWidgetItem(by[static_cast<size_t>(r)].message));
        auto* rest = new QPushButton(QStringLiteral("Restore"), table);
        const QString key = by[static_cast<size_t>(r)].key;
        connect(rest, &QPushButton::clicked, this, [this, key] {
            if (editor_) editor_->drcRestore(key);
            refreshHarnessTable();
            showBypass();
        });
        table->setCellWidget(r, 2, rest);
    }
    lay->addWidget(table);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    connect(box, &QDialogButtonBox::rejected, dlg, &QDialog::close);
    lay->addWidget(box);
    dlg->show();
    dlg->raise();
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

} // namespace Loopwire
