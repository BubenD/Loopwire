#include "app/CableBuilderDialog.h"

#include "app/DrawingPreview.h"
#include "app/UiBits.h"
#include "harness/Harness.h"
#include "harness/PartsLibrary.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

namespace Edraw_Desktop {

namespace {

// The cross-sections offered, labelled the way the drawing is set to write them, with the mm² figure
// as the value - the same list a wire is chosen from, so a cable's core and a discrete wire cannot end
// up described in different units.
void fillGauges(QComboBox* box) {
    for (const QString& mm2 : wireGaugeChoices()) box->addItem(wireGaugeLabel(mm2), mm2);
}

QString gaugeLabel(const QString& mm2) { return wireGaugeLabel(mm2); }

} // namespace

CableBuilderDialog::CableBuilderDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Cable make-up"));
    resize(940, 640);
    buildUi();
    reloadTable();
}

void CableBuilderDialog::setCableName(const QString& name) {
    name_ = name;
    refreshSummary();
}

void CableBuilderDialog::buildUi() {
    auto* root = new QVBoxLayout(this);

    // ---- The cable itself ----
    {
        auto* box = new QGroupBox(QStringLiteral("The cable"), this);
        auto* g = new QHBoxLayout(box);
        auto cap = [this](const QString& t) {
            auto* l = new QLabel(t, this);
            l->setStyleSheet(QStringLiteral("color:#9fb4c8;"));
            return l;
        };
        g->addWidget(cap(QStringLiteral("Outer diameter")));
        odSpin_ = new QDoubleSpinBox(box);
        odSpin_->setRange(0.0, 200.0);
        odSpin_->setDecimals(2);
        odSpin_->setSuffix(QStringLiteral(" mm"));
        odSpin_->setToolTip(QStringLiteral("Over the jacket — what a gland, a boot or a bundle "
                                          "calculation has to fit"));
        connect(odSpin_, &QDoubleSpinBox::valueChanged, this, [this](double) { readCableFields(); });
        g->addWidget(odSpin_);

        g->addWidget(cap(QStringLiteral("Jacket")));
        jacketBox_ = new QComboBox(box);
        jacketBox_->setEditable(true);
        jacketBox_->addItems(choicesFor(PartKind::Cable, QStringLiteral("jacket")));
        jacketBox_->setToolTip(QStringLiteral(
            "Pick one, or type a new compound - it is added to the list for every part from then on."));
        connect(jacketBox_->lineEdit(), &QLineEdit::editingFinished, this, [this] {
            rememberChoice(QStringLiteral("jacket"), jacketBox_->currentText(),
                           partFieldChoices(PartKind::Cable, QStringLiteral("jacket")));
        });
        connect(jacketBox_, &QComboBox::currentTextChanged, this,
                [this](const QString&) { readCableFields(); });
        g->addWidget(jacketBox_);

        // What colour the jacket is: how a cable is recognised in a loom, and what a drawing calls it.
        g->addWidget(cap(QStringLiteral("Jacket colour")));
        jacketColourBox_ = makeColorBox(box, spec_.jacketColour, true);
        connect(jacketColourBox_, &QComboBox::currentIndexChanged, this, [this](int) {
            if (syncing_) return;
            colorBoxCommit(jacketColourBox_, this);
            readCableFields();
        });
        g->addWidget(jacketColourBox_);

        // Which colour standard the conductors are coloured to. A cable is coloured to a standard and
        // the far end expects to find it, so this is a property of the cable - and changing it colours
        // the make-up, which is the whole point of choosing one.
        g->addWidget(cap(QStringLiteral("Colours to")));
        standardBox_ = new QComboBox(box);
        for (CableColourStandard s : allCableColourStandards())
            standardBox_->addItem(cableColourStandardName(s), cableColourStandardKey(s));
        {
            QStringList tip;
            tip << QStringLiteral("Which standard the conductor colours follow:");
            for (CableColourStandard s : allCableColourStandards())
                tip << QStringLiteral("  %1 - %2")
                           .arg(cableColourStandardName(s), cableColourStandardNote(s));
            tip << QStringLiteral("Changing it re-colours the make-up; a colour set by hand is left "
                                 "alone.");
            standardBox_->setToolTip(tip.join(QLatin1Char('\n')));
        }
        connect(standardBox_, &QComboBox::currentIndexChanged, this, [this](int) {
            if (syncing_) return;
            spec_.colourStandard =
                cableColourStandardFromKey(standardBox_->currentData().toString());
            applyCableColourStandard(spec_, true);   // chosen deliberately: colour the lot to it
            reloadTable();
        });
        g->addWidget(standardBox_);

        g->addWidget(cap(QStringLiteral("Conductors")));
        materialBox_ = new QComboBox(box);
        materialBox_->setEditable(true);
        materialBox_->addItems(choicesFor(PartKind::Cable, QStringLiteral("material")));
        materialBox_->setToolTip(QStringLiteral(
            "Pick one, or type a new material - it is added to the list for every part from then on."));
        connect(materialBox_->lineEdit(), &QLineEdit::editingFinished, this, [this] {
            rememberChoice(QStringLiteral("material"), materialBox_->currentText(),
                           partFieldChoices(PartKind::Cable, QStringLiteral("material")));
        });
        connect(materialBox_, &QComboBox::currentTextChanged, this,
                [this](const QString&) { readCableFields(); });
        g->addWidget(materialBox_);
        g->addStretch(1);
        root->addWidget(box);
    }

    // ---- The overall screen ----
    {
        auto* box = new QGroupBox(QStringLiteral("Overall screen"), this);
        auto* g = new QHBoxLayout(box);
        // Each screen with its own coverage BESIDE it: "Foil [100 %]  Braid [85 %]". A foil is a wrap
        // and covers about all of it; a braid is woven and covers 65-95%. Read as two boxes and then
        // two numbers, there is nothing to say which number belongs to which screen.
        overallFoil_ = new QCheckBox(QStringLiteral("Foil"), box);
        overallFoil_->setToolTip(QStringLiteral(
            "A foil screen around the whole make-up. Everything inside it is drawn inside one screen "
            "in the harness, and its drain has to land somewhere."));
        connect(overallFoil_, &QCheckBox::toggled, this, [this](bool) { readCableFields(); });
        g->addWidget(overallFoil_);
        foilCoverageSpin_ = new QSpinBox(box);
        foilCoverageSpin_->setRange(0, 100);
        foilCoverageSpin_->setSuffix(QStringLiteral(" %"));
        foilCoverageSpin_->setToolTip(QStringLiteral(
            "How much of the circumference the FOIL covers - a wrap is usually 100%. 0 if the "
            "datasheet does not say."));
        connect(foilCoverageSpin_, &QSpinBox::valueChanged, this,
                [this](int) { readCableFields(); });
        g->addWidget(foilCoverageSpin_);
        overallBraid_ = new QCheckBox(QStringLiteral("Braid"), box);
        connect(overallBraid_, &QCheckBox::toggled, this, [this](bool) { readCableFields(); });
        g->addWidget(overallBraid_);
        coverageSpin_ = new QSpinBox(box);
        coverageSpin_->setRange(0, 100);
        coverageSpin_->setSuffix(QStringLiteral(" %"));
        coverageSpin_->setToolTip(QStringLiteral(
            "How much of the circumference the BRAID covers - woven, so 65-95% is usual. 0 if the "
            "datasheet does not say."));
        connect(coverageSpin_, &QSpinBox::valueChanged, this, [this](int) { readCableFields(); });
        g->addWidget(coverageSpin_);
        auto* drainCap = new QLabel(QStringLiteral("Drain wire"), box);
        drainCap->setStyleSheet(QStringLiteral("color:#9fb4c8;"));
        g->addWidget(drainCap);
        drainBox_ = new QComboBox(box);
        drainBox_->addItem(QStringLiteral("none"), QString());
        fillGauges(drainBox_);
        drainBox_->setToolTip(QStringLiteral(
            "A drain wire is a conductor: it comes out of the cable, it lands on a pin, and the "
            "harness gets a row for it."));
        connect(drainBox_, &QComboBox::currentIndexChanged, this,
                [this](int) { readCableFields(); });
        g->addWidget(drainBox_);
        g->addStretch(1);
        root->addWidget(box);
    }

    // ---- The shorthand, and the button that reads it ----
    // A cable arrives described in one line - in an e-mail, on a drawing, in a quotation. Typing that
    // line and pressing a button beats filling in the table row by row, and the same line written back
    // out of the make-up is what the part's Structure field says the cable is.
    {
        auto* box = new QGroupBox(QStringLiteral("Structure, in one line"), this);
        auto* v = new QVBoxLayout(box);
        auto* row = new QHBoxLayout;
        structureEdit_ = new QLineEdit(box);
        structureEdit_->setPlaceholderText(
            QStringLiteral("3x24AWG + FTP 2x2x28AWG + coax, foil, braid, black"));
        structureEdit_->setToolTip(QStringLiteral(
            "How a cable is spoken about:\n"
            "  3x24AWG                three singles of 24 AWG\n"
            "  2x2x28AWG              two pairs of 28 AWG\n"
            "  FTP / STP / SFTP       screened per pair: foil, braid, or both\n"
            "  coax 0.22              a coax\n"
            "  4x0.5mm2               in mm² instead\n"
            "  foil 100%, braid 85%   the overall screens and how much they cover\n"
            "  drain 24AWG            a drain wire\n"
            "  black                  the jacket colour\n"
            "Terms are separated by + or by a comma, in any order."));
        connect(structureEdit_, &QLineEdit::returnPressed, this,
                [this] { generateFromStructureText(); });
        row->addWidget(structureEdit_, 1);
        auto* gen = new QPushButton(QStringLiteral("Generate structure"), box);
        gen->setToolTip(QStringLiteral(
            "Read the line and build the make-up from it. What it could not make sense of is said "
            "rather than quietly ignored."));
        connect(gen, &QPushButton::clicked, this, [this] { generateFromStructureText(); });
        row->addWidget(gen);
        v->addLayout(row);
        structureNote_ = new QLabel(QString(), box);
        structureNote_->setWordWrap(true);
        structureNote_->setStyleSheet(QStringLiteral("color:#e6b45a;"));
        structureNote_->hide();
        v->addWidget(structureNote_);
        root->addWidget(box);
    }

    // ---- The make-up ----
    {
        auto* box = new QGroupBox(QStringLiteral("What is inside the jacket"), this);
        auto* v = new QVBoxLayout(box);
        table_ = new QTableWidget(box);
        table_->setColumnCount(8);
        table_->setHorizontalHeaderLabels({QStringLiteral("What"), QStringLiteral("How many"),
                                           QStringLiteral("Cross-section"), QStringLiteral("Twisted"),
                                           QStringLiteral("Foil"), QStringLiteral("Braid"),
                                           QStringLiteral("Colours"), QStringLiteral("")});
        table_->verticalHeader()->setVisible(false);
        table_->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Stretch);
        table_->setColumnWidth(0, 150);
        table_->setColumnWidth(1, 80);
        table_->setColumnWidth(2, 110);
        table_->setColumnWidth(3, 66);
        table_->setColumnWidth(4, 50);
        table_->setColumnWidth(5, 56);
        table_->setColumnWidth(7, 34);
        v->addWidget(table_, 1);

        auto* row = new QHBoxLayout;
        for (CableElementKind kind : allCableElementKinds()) {
            auto* add = makeAddButton(box, cableElementKindName(kind),
                                      QStringLiteral("Add %1 to the make-up")
                                          .arg(cableElementKindName(kind).toLower()));
            connect(add, &QPushButton::clicked, this, [this, kind] {
                // A group is twisted unless it is said otherwise: that is what a pair IS.
                const bool twisted = kind == CableElementKind::Pair ||
                                     kind == CableElementKind::Triple ||
                                     kind == CableElementKind::Quad;
                addElement(kind, 1, QStringLiteral("0.5"), twisted, false, false, {});
            });
            row->addWidget(add);
        }
        row->addStretch(1);
        v->addLayout(row);
        root->addWidget(box, 1);
    }

    // ---- What it comes to, and what it looks like ----
    {
        auto* row = new QHBoxLayout;
        summary_ = new QLabel(QString(), this);
        summary_->setWordWrap(true);
        summary_->setStyleSheet(QStringLiteral("color:#8fb0d6;"));
        row->addWidget(summary_, 1);
        schematic_ = new DrawingPreview(QStringLiteral("Schematic"), this);
        schematic_->setMinimumSize(300, 210);
        schematic_->setEmptyText(QStringLiteral("nothing inside it yet"));
        schematic_->setToolTip(QStringLiteral(
            "What goes on a wiring diagram: a line per conductor, a twist sign at both ends of every "
            "twisted group, a screen and its drain at both ends of every screened one, and a coax "
            "drawn as a coax."));
        row->addWidget(schematic_);
        preview_ = new DrawingPreview(QStringLiteral("Cross-section"), this);
        preview_->setMinimumSize(210, 210);
        preview_->setEmptyText(QStringLiteral("nothing inside it yet"));
        row->addWidget(preview_);
        root->addLayout(row);
    }

    auto* foot = new QHBoxLayout;
    foot->addStretch(1);
    auto* ok = new QPushButton(QStringLiteral("Done"), this);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    foot->addWidget(ok);
    auto* cancel = new QPushButton(QStringLiteral("Cancel"), this);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    foot->addWidget(cancel);
    root->addLayout(foot);
}

void CableBuilderDialog::setSpec(const CableSpec& spec) {
    spec_ = spec;
    syncing_ = true;
    if (odSpin_) odSpin_->setValue(spec_.odMm);
    if (jacketBox_) jacketBox_->setCurrentText(spec_.jacket);
    if (materialBox_) materialBox_->setCurrentText(spec_.conductor);
    if (overallFoil_) overallFoil_->setChecked(spec_.overallFoil);
    if (overallBraid_) overallBraid_->setChecked(spec_.overallBraid);
    if (foilCoverageSpin_) foilCoverageSpin_->setValue(spec_.foilCoverage);
    if (coverageSpin_) coverageSpin_->setValue(spec_.braidCoverage);
    if (jacketColourBox_) {
        const int at = jacketColourBox_->findData(spec_.jacketColour);
        if (at >= 0) jacketColourBox_->setCurrentIndex(at);
    }
    if (drainBox_) {
        const int at = drainBox_->findData(spec_.drainGauge);
        drainBox_->setCurrentIndex(at >= 0 ? at : 0);
    }
    if (standardBox_) {
        const int at = standardBox_->findData(cableColourStandardKey(spec_.colourStandard));
        if (at >= 0) standardBox_->setCurrentIndex(at);
    }
    syncing_ = false;
    reloadTable();
}

void CableBuilderDialog::readCableFields() {
    if (syncing_) return;
    if (odSpin_) spec_.odMm = odSpin_->value();
    if (jacketBox_) spec_.jacket = jacketBox_->currentText();
    if (materialBox_) spec_.conductor = materialBox_->currentText();
    if (overallFoil_) spec_.overallFoil = overallFoil_->isChecked();
    if (overallBraid_) spec_.overallBraid = overallBraid_->isChecked();
    if (foilCoverageSpin_) spec_.foilCoverage = foilCoverageSpin_->value();
    if (coverageSpin_) spec_.braidCoverage = coverageSpin_->value();
    if (jacketColourBox_) spec_.jacketColour = colorBoxCode(jacketColourBox_);
    if (drainBox_) spec_.drainGauge = drainBox_->currentData().toString();
    refreshSummary();
}

void CableBuilderDialog::addElement(CableElementKind kind, int count, const QString& gauge,
                                    bool twisted, bool foil, bool braid,
                                    const QStringList& colours) {
    CableElement e;
    e.kind = kind;
    e.count = std::max(1, count);
    e.gauge = gauge;
    e.twisted = twisted;
    e.foil = foil;
    e.braid = braid;
    e.colours = colours;
    spec_.elements.push_back(e);
    // Coloured straight away, to the cable's standard. The drawing has always used those colours
    // whether the table said so or not - boxes reading "none" beside a schematic showing white and
    // green was the table telling a different story from the picture next to it.
    applyCableColourStandard(spec_, false);
    reloadTable();
}

void CableBuilderDialog::removeElement(int row) {
    if (row < 0 || row >= static_cast<int>(spec_.elements.size())) return;
    spec_.elements.erase(spec_.elements.begin() + row);
    reloadTable();
}

void CableBuilderDialog::reloadTable() {
    if (!table_) return;
    syncing_ = true;
    table_->setRowCount(static_cast<int>(spec_.elements.size()));
    for (int r = 0; r < static_cast<int>(spec_.elements.size()); ++r) {
        const CableElement& e = spec_.elements[static_cast<size_t>(r)];

        auto* what = new QComboBox(table_);
        for (CableElementKind k : allCableElementKinds())
            what->addItem(cableElementKindName(k), cableElementKindKey(k));
        what->setCurrentIndex(what->findData(cableElementKindKey(e.kind)));
        connect(what, &QComboBox::currentIndexChanged, this, [this, r, what](int) {
            if (syncing_) return;
            spec_.elements[static_cast<size_t>(r)].kind =
                cableElementKindFromKey(what->currentData().toString());
            reloadTable();
        });
        table_->setCellWidget(r, 0, what);

        auto* count = new QSpinBox(table_);
        count->setRange(1, 200);
        count->setValue(std::max(1, e.count));
        connect(count, &QSpinBox::valueChanged, this, [this, r](int v) {
            if (syncing_) return;
            spec_.elements[static_cast<size_t>(r)].count = std::max(1, v);
            refreshSummary();
        });
        table_->setCellWidget(r, 1, count);

        auto* gauge = new QComboBox(table_);
        fillGauges(gauge);
        const int at = gauge->findData(e.gauge);
        if (at >= 0) gauge->setCurrentIndex(at);
        else gauge->setCurrentText(gaugeLabel(e.gauge));
        gauge->setToolTip(QStringLiteral(
            "The conductors' cross-section. Written in whichever notation the drawing is set to - "
            "AWG or mm² - and stored as the mm² figure either way, so it never has to be converted "
            "by hand."));
        connect(gauge, &QComboBox::currentIndexChanged, this, [this, r, gauge](int) {
            if (syncing_) return;
            spec_.elements[static_cast<size_t>(r)].gauge = gauge->currentData().toString();
            spec_.elements[static_cast<size_t>(r)].enteredAwg = wireGaugeNotationAwg();
            refreshSummary();
        });
        table_->setCellWidget(r, 2, gauge);

        auto checkCell = [this, r](bool on, const char* which, bool enabled) {
            auto* host = new QWidget(table_);
            auto* hl = new QHBoxLayout(host);
            hl->setContentsMargins(0, 0, 0, 0);
            hl->setAlignment(Qt::AlignCenter);
            auto* cb = new QCheckBox(host);
            cb->setChecked(on);
            cb->setEnabled(enabled);
            const QString key = QString::fromLatin1(which);
            connect(cb, &QCheckBox::toggled, this, [this, r, key](bool v) {
                if (syncing_) return;
                CableElement& el = spec_.elements[static_cast<size_t>(r)];
                if (key == QLatin1String("twisted")) el.twisted = v;
                else if (key == QLatin1String("foil")) el.foil = v;
                else el.braid = v;
                refreshSummary();
            });
            hl->addWidget(cb);
            return host;
        };
        // A single conductor cannot be twisted with itself, and a coax is not twisted either - so
        // there is no box at all for them. A box that is there but can never be ticked is a question
        // nobody asked, and it invites the reader to wonder what would happen if they could.
        const bool canTwist = e.kind == CableElementKind::Pair ||
                              e.kind == CableElementKind::Triple ||
                              e.kind == CableElementKind::Quad;
        if (canTwist) table_->setCellWidget(r, 3, checkCell(e.twisted, "twisted", true));
        else table_->removeCellWidget(r, 3);
        // A coax carries its own screen; a screen around it as well would be a second one.
        const bool canScreen = e.kind != CableElementKind::Coax;
        table_->setCellWidget(r, 4, checkCell(e.foil, "foil", canScreen));
        table_->setCellWidget(r, 5, checkCell(e.braid, "braid", canScreen));

        // A colour box PER CONDUCTOR, each with its circle: a twisted pair has two conductors and so
        // two colours, and which is which matters - the first is one end of the pair and the second is
        // the other. A single line of "WH/BU" says the same thing but has to be typed and cannot be
        // picked by eye.
        {
            auto* host = new QWidget(table_);
            auto* hl = new QHBoxLayout(host);
            hl->setContentsMargins(0, 0, 0, 0);
            hl->setSpacing(2);
            const int per = cableConductorsPerElement(e.kind);
            const QStringList usual = defaultCableColours(e.kind, r);
            for (int c = 0; c < per; ++c) {
                // A coax's second conductor IS its screen: a braid has no colour to choose.
                if (e.kind == CableElementKind::Coax && c == 1) {
                    auto* screen = new QLabel(QStringLiteral("screen"), host);
                    screen->setStyleSheet(QStringLiteral("color:#c8b273;"));
                    hl->addWidget(screen);
                    continue;
                }
                const QString have = c < e.colours.size() ? e.colours.at(c) : QString();
                auto* pick = makeColorBox(host, have, true);
                pick->setToolTip(per > 1
                                     ? QStringLiteral("Conductor %1 of %2. Left as none, the usual "
                                                      "colours are used: %3")
                                           .arg(c + 1)
                                           .arg(per)
                                           .arg(usual.join(QLatin1Char('/')))
                                     : QStringLiteral("Left as none, the usual order is followed"));
                connect(pick, &QComboBox::currentIndexChanged, this, [this, r, c, pick](int) {
                    if (syncing_) return;
                    colorBoxCommit(pick, this);
                    CableElement& el = spec_.elements[static_cast<size_t>(r)];
                    const int want = cableConductorsPerElement(el.kind);
                    while (el.colours.size() < want) el.colours << QString();
                    el.colours[c] = colorBoxCode(pick);
                    // All of them left as none means "use the usual ones", which is not the same as
                    // every conductor being deliberately colourless.
                    bool any = false;
                    for (const QString& s : el.colours)
                        if (!s.isEmpty()) any = true;
                    if (!any) el.colours.clear();
                    refreshSummary();
                });
                hl->addWidget(pick);
            }
            hl->addStretch(1);
            table_->setCellWidget(r, 6, host);
        }
        auto* del = makeRemoveButton(table_, QString(),
                                     QStringLiteral("Take this out of the make-up"), true);
        connect(del, &QPushButton::clicked, this, [this, r] { removeElement(r); });
        table_->setCellWidget(r, 7, del);
    }
    syncing_ = false;
    refreshSummary();
}

QString CableBuilderDialog::summaryText() const {
    const int cores = spec_.conductorCount();
    return QStringLiteral("%1%2 — %3 conductor(s)%4")
        .arg(name_.isEmpty() ? QString() : name_ + QStringLiteral(": "), spec_.summary())
        .arg(cores)
        .arg(spec_.screened() ? QStringLiteral(", screened") : QString());
}

void CableBuilderDialog::setStructureText(const QString& text) {
    if (!structureEdit_) return;
    const bool was = syncing_;
    syncing_ = true;
    structureEdit_->setText(text);
    syncing_ = was;
}

QString CableBuilderDialog::structureText() const {
    return structureEdit_ ? structureEdit_->text() : QString();
}

// Read the line, build the make-up from it, and say what could not be made sense of.
void CableBuilderDialog::generateFromStructureText() {
    if (!structureEdit_) return;
    const QString text = structureEdit_->text().trimmed();
    if (text.isEmpty()) {
        if (structureNote_) {
            structureNote_->setText(QStringLiteral(
                "Type what the cable is - the placeholder shows the shape of it."));
            structureNote_->show();
        }
        return;
    }
    QStringList notes;
    CableSpec made = cableSpecFromText(text, &notes);
    if (made.elements.empty()) {
        if (structureNote_) {
            structureNote_->setText(
                QStringLiteral("Nothing in that line reads as a conductor. %1")
                    .arg(notes.join(QStringLiteral("; "))));
            structureNote_->show();
        }
        return;
    }
    // What the line does not mention is left as it was: an outer diameter, a jacket compound and a
    // conductor material are not part of the shorthand, and throwing them away would be a surprise.
    if (made.odMm <= 0.0) made.odMm = spec_.odMm;
    if (made.jacket.isEmpty()) made.jacket = spec_.jacket;
    if (made.conductor.isEmpty()) made.conductor = spec_.conductor;
    if (made.jacketColour.isEmpty()) made.jacketColour = spec_.jacketColour;
    // A make-up read from the line is coloured to the standard as well, for the same reason.
    applyCableColourStandard(made, false);
    setSpec(made);
    if (structureNote_) {
        if (notes.isEmpty()) structureNote_->hide();
        else {
            structureNote_->setText(notes.join(QStringLiteral("; ")));
            structureNote_->show();
        }
    }
}

void CableBuilderDialog::refreshSummary() {
    if (summary_) summary_->setText(summaryText());
    if (preview_) preview_->setEntities(cableCrossSection(spec_, spec_.odMm));
    if (schematic_) schematic_->setEntities(cableSchematic(spec_));
    // The line and the table say the same thing, always: whichever was changed, the other follows.
    setStructureText(cableSpecToText(spec_));
}

} // namespace Edraw_Desktop
