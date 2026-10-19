#include "app/TemplateDialog.h"

#include "app/UiBits.h"
#include "app/DrawingPreview.h"
#include "app/Editor.h"
#include "harness/Harness.h"
#include "harness/PartsLibrary.h"   // partDrawingEntities
#include "io/AppPaths.h"
#include "model/Document.h"
#include "model/DrawingTemplate.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDate>

#include <algorithm>
#include <QDesktopServices>
#include <QFileDialog>
#include <QFileInfo>
#include <QSettings>
#include <QFile>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QScrollArea>
#include <QScreen>
#include <QGuiApplication>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace Loopwire {
namespace {
constexpr int kLayName = 0, kLayColour = 1, kLayWeight = 2, kLayVisible = 3, kLayLocked = 4;

QLabel* caption(QWidget* parent, const QString& text) {
    auto* l = new QLabel(text, parent);
    l->setStyleSheet(QStringLiteral("color:#9fb4c8;"));
    return l;
}
} // namespace

TemplateDialog::TemplateDialog(Editor* editor, QWidget* parent) : QDialog(parent), editor_(editor) {
    setWindowTitle(QStringLiteral("Drawing template"));
    setSizeGripEnabled(true);
    setWindowFlags(windowFlags() | Qt::WindowMaximizeButtonHint);
    setMinimumSize(640, 420);
    resize(1080, 780);
    // A new template is a bordered A3 sheet with a title block, not an empty form.
    tpl_.name = QStringLiteral("House A3");
    tpl_.layers = Document::defaultLayers();
    tpl_.drawn.date = QDate::currentDate().toString(Qt::ISODate);
    tpl_.content = defaultSheetContent(tpl_);
    buildUi();
    writeToControls();
    refreshInstalled();
    refreshPreview();
    if(auto* screen=QGuiApplication::primaryScreen())resize(size().boundedTo(screen->availableGeometry().size()*0.9));
}

void TemplateDialog::buildUi() {
    auto* root = new QVBoxLayout(this);
    auto* split = new QSplitter(Qt::Horizontal, this);

    // ---- What it is ----
    // Every section sits in a splitter, so any of them can be dragged bigger at the expense of the
    // others. A section squeezed small CLIPS what is in it rather than pushing the window out - which
    // is why each one has a small minimum and its own scrolling where it needs it.
    auto* leftSplit = new QSplitter(Qt::Vertical, split);
    leftSplit->setChildrenCollapsible(true);
    leftSplit->setHandleWidth(6);
    auto* left = leftSplit;
    auto* leftLay = new QVBoxLayout;   // sections are added to the splitter, not to a column
    (void)leftLay;
    auto addSection = [leftSplit](QWidget* w, int stretch) {
        w->setMinimumHeight(0);
        leftSplit->addWidget(w);
        leftSplit->setStretchFactor(leftSplit->count() - 1, stretch);
    };

    auto* idBox = new QGroupBox(QStringLiteral("The template"), left);
    auto* idGrid = new QGridLayout(idBox);
    int r = 0;
    nameEdit_ = new QLineEdit(idBox);
    nameEdit_->setToolTip(QStringLiteral("What it is called, and the file name it is saved under"));
    idGrid->addWidget(caption(idBox, QStringLiteral("Name")), r, 0);
    idGrid->addWidget(nameEdit_, r++, 1, 1, 3);
    authorEdit_ = new QLineEdit(idBox);
    revEdit_ = new QLineEdit(idBox);
    idGrid->addWidget(caption(idBox, QStringLiteral("Author")), r, 0);
    idGrid->addWidget(authorEdit_, r, 1);
    idGrid->addWidget(caption(idBox, QStringLiteral("Revision")), r, 2);
    idGrid->addWidget(revEdit_, r++, 3);
    descEdit_ = new QPlainTextEdit(idBox);
    descEdit_->setMaximumHeight(56);
    descEdit_->setPlaceholderText(QStringLiteral("What it is for, and who should use it"));
    idGrid->addWidget(caption(idBox, QStringLiteral("Description")), r, 0, Qt::AlignTop);
    idGrid->addWidget(descEdit_, r++, 1, 1, 3);
    addSection(idBox, 2);

    // ---- Who is responsible for it ----
    // A name and a date apiece. Free text on the date, because the date against an approval is blank
    // until it is approved - a date control with no empty state would force one that is not true.
    auto* sigBox = new QGroupBox(QStringLiteral("Signatures, as they go in the title block"), left);
    auto* sigGrid = new QGridLayout(sigBox);
    int sr = 0;
    sigGrid->addWidget(caption(sigBox, QStringLiteral("Name")), sr, 1);
    sigGrid->addWidget(caption(sigBox, QStringLiteral("Date")), sr++, 2);
    auto addSignature = [&](const QString& label, SignatureRow& row) {
        row.name = new QLineEdit(sigBox);
        row.name->setPlaceholderText(QStringLiteral("who"));
        row.date = new QLineEdit(sigBox);
        row.date->setPlaceholderText(QStringLiteral("yyyy-mm-dd, or leave it blank"));
        for (QLineEdit* e : {row.name, row.date})
            connect(e, &QLineEdit::textEdited, this, [this](const QString&) {
                if (syncing_) return;
                readFromControls();
                // The title block carries them, so it is redrawn as they are typed - but only when
                // the content IS the generated sheet, never over a border someone drew themselves.
                refreshPreview();
            });
        auto* today = new QPushButton(QStringLiteral("Today"), sigBox);
        today->setToolTip(QStringLiteral("Put today's date in"));
        QLineEdit* dateField = row.date;
        connect(today, &QPushButton::clicked, this, [this, dateField] {
            dateField->setText(QDate::currentDate().toString(Qt::ISODate));
            readFromControls();
            refreshPreview();
        });
        sigGrid->addWidget(caption(sigBox, label), sr, 0);
        sigGrid->addWidget(row.name, sr, 1);
        sigGrid->addWidget(row.date, sr, 2);
        sigGrid->addWidget(today, sr++, 3);
    };
    addSignature(QStringLiteral("Drawn"), drawnRow_);
    addSignature(QStringLiteral("Checked"), checkedRow_);
    addSignature(QStringLiteral("Approved"), approvedRow_);
    addSection(sigBox, 2);

    // ---- The sheet ----
    auto* sheetBox = new QGroupBox(QStringLiteral("The sheet"), left);
    auto* sGrid = new QGridLayout(sheetBox);
    r = 0;
    sheetBox_ = new QComboBox(sheetBox);
    for (const TemplateSheet& s : templateSheets())
        sheetBox_->addItem(QStringLiteral("%1  (%2 × %3 mm)")
                               .arg(s.name)
                               .arg(s.widthMm, 0, 'f', 0)
                               .arg(s.heightMm, 0, 'f', 0),
                           s.name);
    sheetBox_->addItem(QStringLiteral("Custom"), QStringLiteral(""));
    connect(sheetBox_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (syncing_) return;
        const QString name = sheetBox_->currentData().toString();
        if (const TemplateSheet* s = templateSheetByName(name)) {
            syncing_ = true;
            widthSpin_->setValue(s->widthMm);
            heightSpin_->setValue(s->heightMm);
            syncing_ = false;
        }
        readFromControls();
        refreshPreview();
    });
    sGrid->addWidget(caption(sheetBox, QStringLiteral("Size")), r, 0);
    sGrid->addWidget(sheetBox_, r++, 1, 1, 3);
    auto spin = [&](double lo, double hi, int dp) {
        auto* s = new QDoubleSpinBox(sheetBox);
        s->setRange(lo, hi);
        s->setDecimals(dp);
        connect(s, &QDoubleSpinBox::valueChanged, this, [this](double) {
            if (syncing_) return;
            readFromControls();
            refreshPreview();
        });
        return s;
    };
    widthSpin_ = spin(10.0, 5000.0, 1);
    widthSpin_->setSuffix(QStringLiteral(" mm"));
    heightSpin_ = spin(10.0, 5000.0, 1);
    heightSpin_->setSuffix(QStringLiteral(" mm"));
    sGrid->addWidget(caption(sheetBox, QStringLiteral("Width")), r, 0);
    sGrid->addWidget(widthSpin_, r, 1);
    sGrid->addWidget(caption(sheetBox, QStringLiteral("Height")), r, 2);
    sGrid->addWidget(heightSpin_, r++, 3);
    // The grid reference frame: numbers along the top and bottom, letters down the sides.
    zonesChk_ = new QCheckBox(QStringLiteral("Zone frame (A1, B3 …)"), sheetBox);
    zonesChk_->setToolTip(QStringLiteral(
        "A grid reference round the border, so anything on the sheet can be called out by the zone it "
        "sits in. Divisions are equal, so the size below is what is aimed for - the count is whatever "
        "comes nearest on this sheet."));
    connect(zonesChk_, &QCheckBox::toggled, this, [this](bool on) {
        if (zoneSizeSpin_) zoneSizeSpin_->setEnabled(on);
        if (zoneBandSpin_) zoneBandSpin_->setEnabled(on);
        if (lettersTopChk_) lettersTopChk_->setEnabled(on);
        if (syncing_) return;
        readFromControls();
        refreshPreview();
    });
    zoneSizeSpin_ = spin(10.0, 200.0, 1);
    zoneSizeSpin_->setSuffix(QStringLiteral(" mm"));
    zoneSizeSpin_->setToolTip(QStringLiteral("The division aimed for; 50 mm is the usual figure"));
    zoneBandSpin_ = spin(3.0, 30.0, 1);
    zoneBandSpin_->setSuffix(QStringLiteral(" mm"));
    zoneBandSpin_->setToolTip(QStringLiteral(
        "The band the labels sit in, between the sheet edge and the border"));
    lettersTopChk_ = new QCheckBox(QStringLiteral("A at the top"), sheetBox);
    lettersTopChk_->setToolTip(QStringLiteral(
        "Letters running down from the top, as ISO 5457 has them. Off starts A at the bottom."));
    connect(lettersTopChk_, &QCheckBox::toggled, this, [this](bool) {
        if (syncing_) return;
        readFromControls();
        refreshPreview();
    });
    sGrid->addWidget(zonesChk_, r, 0, 1, 2);
    sGrid->addWidget(lettersTopChk_, r++, 2, 1, 2);
    sGrid->addWidget(caption(sheetBox, QStringLiteral("Zone size")), r, 0);
    sGrid->addWidget(zoneSizeSpin_, r, 1);
    sGrid->addWidget(caption(sheetBox, QStringLiteral("Label band")), r, 2);
    sGrid->addWidget(zoneBandSpin_, r++, 3);
    zoneCount_ = new QLabel(QString(), sheetBox);
    zoneCount_->setStyleSheet(QStringLiteral("color:#8fb0d6;"));
    sGrid->addWidget(zoneCount_, r++, 0, 1, 4);

    landscapeChk_ = new QCheckBox(QStringLiteral("Landscape"), sheetBox);
    connect(landscapeChk_, &QCheckBox::toggled, this, [this](bool) {
        if (syncing_) return;
        readFromControls();
        refreshPreview();
    });
    sGrid->addWidget(landscapeChk_, r++, 1);
    addSection(sheetBox, 3);

    // ---- The revision table ----
    auto* revBox = new QGroupBox(QStringLiteral("Revision table, top-right corner"), left);
    auto* revGrid = new QGridLayout(revBox);
    int rr = 0;
    revChk_ = new QCheckBox(QStringLiteral("Show it"), revBox);
    revRowsSpin_ = new QSpinBox(revBox);
    revRowsSpin_->setRange(0, 40);
    revRowsSpin_->setToolTip(QStringLiteral("Blank rows to start with, under the heading row"));
    revRowHSpin_ = new QDoubleSpinBox(revBox);
    revRowHSpin_->setRange(3.0, 20.0);
    revRowHSpin_->setDecimals(1);
    revRowHSpin_->setSuffix(QStringLiteral(" mm"));
    revGrid->addWidget(revChk_, rr, 0);
    revGrid->addWidget(caption(revBox, QStringLiteral("Rows")), rr, 1);
    revGrid->addWidget(revRowsSpin_, rr, 2);
    revGrid->addWidget(caption(revBox, QStringLiteral("Row height")), rr, 3);
    revGrid->addWidget(revRowHSpin_, rr++, 4);
    // A column apiece: shown or not, and how wide when it is. Hiding one takes its width with it, so
    // the table is only ever as wide as the columns actually on it.
    static const char* kRevNames[7] = {"Rev", "Change note", "ECO", "Drawn", "Checked", "Approved",
                                      "Date"};
    revGrid->addWidget(caption(revBox, QStringLiteral("Columns")), rr, 0);
    for (int i = 0; i < 7; ++i) {
        revCols_[i].show = new QCheckBox(QString::fromLatin1(kRevNames[i]), revBox);
        revCols_[i].width = new QDoubleSpinBox(revBox);
        revCols_[i].width->setRange(6.0, 120.0);
        revCols_[i].width->setDecimals(1);
        revCols_[i].width->setSuffix(QStringLiteral(" mm"));
        const int row = rr + i / 2;
        const int col = 1 + (i % 2) * 2;
        revGrid->addWidget(revCols_[i].show, row, col);
        revGrid->addWidget(revCols_[i].width, row, col + 1);
    }
    rr += 4;
    revSummary_ = new QLabel(QString(), revBox);
    revSummary_->setStyleSheet(QStringLiteral("color:#8fb0d6;"));
    revGrid->addWidget(revSummary_, rr++, 0, 1, 5);
    for (int i = 0; i < 7; ++i) {
        connect(revCols_[i].show, &QCheckBox::toggled, this, [this, i](bool on) {
            if (revCols_[i].width) revCols_[i].width->setEnabled(on);
            if (syncing_) return;
            readFromControls();
            refreshPreview();
        });
        connect(revCols_[i].width, &QDoubleSpinBox::valueChanged, this, [this](double) {
            if (syncing_) return;
            readFromControls();
            refreshPreview();
        });
    }
    connect(revChk_, &QCheckBox::toggled, this, [this](bool on) {
        if (revRowsSpin_) revRowsSpin_->setEnabled(on);
        if (revRowHSpin_) revRowHSpin_->setEnabled(on);
        for (int i = 0; i < 7; ++i) {
            if (revCols_[i].show) revCols_[i].show->setEnabled(on);
            if (revCols_[i].width)
                revCols_[i].width->setEnabled(on && revCols_[i].show->isChecked());
        }
        if (syncing_) return;
        readFromControls();
        refreshPreview();
    });
    connect(revRowsSpin_, &QSpinBox::valueChanged, this, [this](int) {
        if (syncing_) return;
        readFromControls();
        refreshPreview();
    });
    connect(revRowHSpin_, &QDoubleSpinBox::valueChanged, this, [this](double) {
        if (syncing_) return;
        readFromControls();
        refreshPreview();
    });
    addSection(revBox, 3);

    // ---- The boxes each drawing fills in ----
    auto* fieldBox = new QGroupBox(QStringLiteral("Boxes each drawing fills in"), left);
    auto* fieldLay = new QVBoxLayout(fieldBox);
    fields_ = new QTableWidget(fieldBox);
    fields_->setColumnCount(4);
    fields_->setHorizontalHeaderLabels({QStringLiteral("Key"), QStringLiteral("Printed as"),
                                        QStringLiteral("Starts at"), QStringLiteral("Text")});
    // The caption is the long column, not the text height - stretching the last one put all the room
    // in the narrowest field and squeezed the rest to nothing.
    fields_->horizontalHeader()->setStretchLastSection(false);
    fields_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    fields_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    fields_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    fields_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    fields_->verticalHeader()->setVisible(false);
    // Room for several boxes at once, and it grows with the section rather than being pinned short.
    fields_->setMinimumHeight(150);
    fields_->setToolTip(QStringLiteral(
        "A box the template puts on the sheet for the DRAWING to fill in - the title, the number, the "
        "sheet count. Double-click one on the sheet to type into it."));
    connect(fields_, &QTableWidget::cellChanged, this, [this](int, int) {
        if (syncing_) return;
        readFromControls();
        refreshPreview();
    });
    fieldLay->addWidget(fields_);
    auto* fieldBtns = new QHBoxLayout;
    auto* addField = makeAddButton(fieldBox, QStringLiteral("Box"));
    connect(addField, &QPushButton::clicked, this, [this] {
        TemplateField f;
        f.key = QStringLiteral("field%1").arg(fields_->rowCount() + 1);
        f.label = QStringLiteral("NEW BOX");
        f.textHeight = 3.5;
        tpl_.fields.push_back(f);
        writeToControls();
        refreshPreview();
    });
    fieldBtns->addWidget(addField);
    auto* rmField = new QPushButton(QStringLiteral("Remove"), fieldBox);
    connect(rmField, &QPushButton::clicked, this, [this] {
        const int row = fields_->currentRow();
        if (row < 0 || row >= static_cast<int>(tpl_.fields.size())) return;
        if (tpl_.fields[static_cast<size_t>(row)].builtIn) {
            if (summary_)
                summary_->setText(QStringLiteral("The title block places that one; it stays."));
            return;
        }
        tpl_.fields.erase(tpl_.fields.begin() + row);
        writeToControls();
        refreshPreview();
    });
    fieldBtns->addWidget(rmField);
    fieldBtns->addStretch(1);
    fieldLay->addLayout(fieldBtns);
    addSection(fieldBox, 3);

    // ---- The layers ----
    auto* layBox = new QGroupBox(QStringLiteral("The layers a drawing starts with"), left);
    auto* layLay = new QVBoxLayout(layBox);
    layers_ = new QTableWidget(layBox);
    layers_->setColumnCount(5);
    layers_->setHorizontalHeaderLabels({QStringLiteral("Name"), QStringLiteral("Colour"),
                                        QStringLiteral("Weight"), QStringLiteral("Visible"),
                                        QStringLiteral("Locked")});
    layers_->horizontalHeader()->setStretchLastSection(true);
    layers_->verticalHeader()->setVisible(false);
    layers_->setMinimumHeight(120);   // grows with its section instead of being pinned short
    connect(layers_, &QTableWidget::cellChanged, this, [this](int, int) {
        if (syncing_) return;
        layersFromTable();
        refreshPreview();
    });
    connect(layers_, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) {
        if (col != kLayColour) return;
        QTableWidgetItem* it = layers_->item(row, kLayColour);
        if (!it) return;
        const QColor picked = QColorDialog::getColor(QColor(it->text()), this,
                                                     QStringLiteral("Layer colour"));
        if (!picked.isValid()) return;
        it->setText(picked.name());
        it->setBackground(picked);
    });
    layLay->addWidget(layers_);
    auto* layBtns = new QHBoxLayout;
    auto* addLayer = makeAddButton(layBox, QStringLiteral("Layer"));
    connect(addLayer, &QPushButton::clicked, this, [this] {
        Layer l;
        l.name = QStringLiteral("layer %1").arg(layers_->rowCount() + 1);
        tpl_.layers.push_back(l);
        refreshLayerTable();
    });
    layBtns->addWidget(addLayer);
    auto* rmLayer = new QPushButton(QStringLiteral("Remove"), layBox);
    connect(rmLayer, &QPushButton::clicked, this, [this] {
        const int row = layers_->currentRow();
        if (row < 0 || row >= static_cast<int>(tpl_.layers.size())) return;
        // Layer 0 is where anything with no layer of its own lands: it cannot be removed.
        if (tpl_.layers[static_cast<size_t>(row)].name == QLatin1String("0")) {
            if (summary_)
                summary_->setText(QStringLiteral("Layer 0 is where anything without a layer of its "
                                                "own goes; it stays."));
            return;
        }
        tpl_.layers.erase(tpl_.layers.begin() + row);
        refreshLayerTable();
    });
    layBtns->addWidget(rmLayer);
    layBtns->addWidget(caption(layBox, QStringLiteral("Active")));
    activeLayerBox_ = new QComboBox(layBox);
    connect(activeLayerBox_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!syncing_) readFromControls();
    });
    layBtns->addWidget(activeLayerBox_, 1);
    layLay->addLayout(layBtns);
    addSection(layBox, 3);

    // ---- The defaults ----
    auto* defBox = new QGroupBox(QStringLiteral("What a drawing is worked in"), left);
    auto* dGrid = new QGridLayout(defBox);
    r = 0;
    unitBox_ = new QComboBox(defBox);
    unitBox_->addItems({QStringLiteral("mm"), QStringLiteral("cm"), QStringLiteral("m"),
                        QStringLiteral("in")});
    connect(unitBox_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (!syncing_) readFromControls();
    });
    auto dspin = [&](double lo, double hi, int dp, const QString& suffix) {
        auto* s = new QDoubleSpinBox(defBox);
        s->setRange(lo, hi);
        s->setDecimals(dp);
        s->setSuffix(suffix);
        connect(s, &QDoubleSpinBox::valueChanged, this, [this](double) {
            if (!syncing_) readFromControls();
        });
        return s;
    };
    textSpin_ = dspin(0.1, 200.0, 1, QString());
    dimTextSpin_ = dspin(0.1, 200.0, 1, QString());
    arrowSpin_ = dspin(0.1, 50.0, 1, QString());
    gridSpin_ = dspin(0.1, 500.0, 1, QString());
    decimalsSpin_ = new QSpinBox(defBox);
    decimalsSpin_->setRange(0, 6);
    connect(decimalsSpin_, &QSpinBox::valueChanged, this, [this](int) {
        if (!syncing_) readFromControls();
    });
    dGrid->addWidget(caption(defBox, QStringLiteral("Units")), r, 0);
    dGrid->addWidget(unitBox_, r, 1);
    dGrid->addWidget(caption(defBox, QStringLiteral("Text height")), r, 2);
    dGrid->addWidget(textSpin_, r++, 3);
    dGrid->addWidget(caption(defBox, QStringLiteral("Dim text")), r, 0);
    dGrid->addWidget(dimTextSpin_, r, 1);
    dGrid->addWidget(caption(defBox, QStringLiteral("Dim arrow")), r, 2);
    dGrid->addWidget(arrowSpin_, r++, 3);
    dGrid->addWidget(caption(defBox, QStringLiteral("Dim decimals")), r, 0);
    dGrid->addWidget(decimalsSpin_, r, 1);
    dGrid->addWidget(caption(defBox, QStringLiteral("Grid")), r, 2);
    dGrid->addWidget(gridSpin_, r++, 3);
    gridChk_ = new QCheckBox(QStringLiteral("Grid on"), defBox);
    snapChk_ = new QCheckBox(QStringLiteral("Snap on"), defBox);
    awgChk_ = new QCheckBox(QStringLiteral("Cross-sections in AWG"), defBox);
    awgChk_->setToolTip(QStringLiteral(
        "A house decision, so it belongs in the template rather than in each drawing"));
    for (QCheckBox* c : {gridChk_, snapChk_, awgChk_})
        connect(c, &QCheckBox::toggled, this, [this](bool) {
            if (!syncing_) readFromControls();
        });
    dGrid->addWidget(gridChk_, r, 0);
    dGrid->addWidget(snapChk_, r, 1);
    dGrid->addWidget(awgChk_, r++, 2, 1, 2);
    addSection(defBox, 3);
    leftSplit->setMinimumWidth(455);
    leftSplit->setMinimumHeight(std::max(1000,leftSplit->minimumSizeHint().height()));
    auto* settingsScroll=new QScrollArea(split);
    settingsScroll->setObjectName(QStringLiteral("templateSettingsScroll"));
    settingsScroll->setWidgetResizable(true);settingsScroll->setFrameShape(QFrame::NoFrame);
    settingsScroll->setMinimumWidth(220);settingsScroll->setWidget(leftSplit);
    split->addWidget(settingsScroll);

    // ---- The sheet as it will look, and the templates already saved ----
    auto* right = new QWidget(split);
    auto* rightLay = new QVBoxLayout(right);
    rightLay->setContentsMargins(0, 0, 0, 0);
    preview_ = new DrawingPreview(QStringLiteral("The sheet a drawing starts on"), right);
    preview_->setEmptyText(QStringLiteral("nothing on the sheet yet"));
    rightLay->addWidget(preview_, 2);

    auto* contentBtns = new QGridLayout;
    auto* captureBtn = new QPushButton(QStringLiteral("Capture the sheet"), right);
    captureBtn->setToolTip(QStringLiteral(
        "Take everything drawn on the sheet as this template's content - draw the border and the "
        "title block with the ordinary tools, then capture it here"));
    connect(captureBtn, &QPushButton::clicked, this, [this] { captureFromDrawing(); });
    contentBtns->addWidget(captureBtn,0,0);
    auto* resetBtn = new QPushButton(QStringLiteral("Plain bordered sheet"), right);
    resetBtn->setToolTip(QStringLiteral("Replace the content with a border and a title block for "
                                        "the sheet size above"));
    connect(resetBtn, &QPushButton::clicked, this, [this] { resetContent(); });
    contentBtns->addWidget(resetBtn,0,1);
    auto* drawBtn = new QPushButton(QStringLiteral("Draw it on the sheet"), right);
    drawBtn->setToolTip(QStringLiteral(
        "Open the template on a tab of its own, on a tinted canvas, and lay the sheet out with the "
        "ordinary drawing tools. Save template puts it back."));
    connect(drawBtn, &QPushButton::clicked, this, [this] {
        readFromControls();
        layersFromTable();
        emit drawOnSheetRequested(tpl_.name);
    });
    contentBtns->addWidget(drawBtn,1,0);
    auto* applyBtn = new QPushButton(QStringLiteral("Apply to this drawing"), right);
    applyBtn->setToolTip(QStringLiteral(
        "Put the layers, the defaults and the sheet into the drawing that is open. What is already "
        "drawn is kept."));
    connect(applyBtn, &QPushButton::clicked, this, [this] { applyToDrawing(); });
    contentBtns->addWidget(applyBtn,1,1);
    rightLay->addLayout(contentBtns);

    auto* instBox = new QGroupBox(QStringLiteral("Templates"), right);
    auto* instLay = new QVBoxLayout(instBox);
    installed_ = new QListWidget(instBox);
    installed_->setMinimumHeight(150);
    installed_->setToolTip(QStringLiteral(
        "Every template in the template folder. Double-click one to DRAW it - it opens on a tab of its "
        "own, on a grey canvas, with the template ribbon and the save buttons."));
    // Double-click DRAWS it, which is what a template is for. Open (below) just loads its settings
    // into this window.
    connect(installed_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (!item) return;
        const QString path = item->data(Qt::UserRole).toString();
        if (path.isEmpty()) return;
        if (loadFrom(path)) emit drawOnSheetRequested(tpl_.name);
    });
    instLay->addWidget(installed_, 1);
    auto* instBtns = new QHBoxLayout;
    auto* openBtn = new QPushButton(QStringLiteral("Open"), instBox);
    connect(openBtn, &QPushButton::clicked, this, [this] {
        if (QListWidgetItem* item = installed_->currentItem())
            loadFrom(item->data(Qt::UserRole).toString());
    });
    instBtns->addWidget(openBtn);
    auto* defaultBtn = new QPushButton(QStringLiteral("Set as default"), instBox);
    defaultBtn->setToolTip(QStringLiteral(
        "The template a NEW drawing starts from. One of them is the house sheet; this is how the "
        "application knows which."));
    connect(defaultBtn, &QPushButton::clicked, this, [this] {
        QListWidgetItem* item = installed_ ? installed_->currentItem() : nullptr;
        const QString path = item ? item->data(Qt::UserRole).toString() : QString();
        if (path.isEmpty()) {
            if (summary_) summary_->setText(QStringLiteral("Pick a template in the list first."));
            return;
        }
        setDefaultTemplatePath(path);
        refreshInstalled();
        if (summary_)
            summary_->setText(QStringLiteral("%1 is the template a new drawing starts from.")
                                  .arg(QFileInfo(path).completeBaseName()));
    });
    instBtns->addWidget(defaultBtn);
    auto* clearDefaultBtn = new QPushButton(QStringLiteral("No default"), instBox);
    clearDefaultBtn->setToolTip(QStringLiteral("A new drawing starts on an empty sheet again"));
    connect(clearDefaultBtn, &QPushButton::clicked, this, [this] {
        setDefaultTemplatePath(QString());
        refreshInstalled();
        if (summary_) summary_->setText(QStringLiteral("A new drawing starts on an empty sheet."));
    });
    instBtns->addWidget(clearDefaultBtn);
    auto* folderBtn = new QPushButton(QStringLiteral("Show the folder"), instBox);
    connect(folderBtn, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(AppPaths::templateDir()));
    });
    instBtns->addWidget(folderBtn);
    instBtns->addStretch(1);
    instLay->addLayout(instBtns);
    rightLay->addWidget(instBox, 1);
    split->addWidget(right);
    split->setStretchFactor(0, 4);
    split->setStretchFactor(1, 3);
    auto* bodyScroll=new QScrollArea(this);bodyScroll->setObjectName(QStringLiteral("templateBodyScroll"));
    bodyScroll->setWidgetResizable(true);bodyScroll->setFrameShape(QFrame::NoFrame);
    bodyScroll->setWidget(split);root->addWidget(bodyScroll,1);

    summary_ = new QLabel(QString(), this);
    summary_->setWordWrap(true);
    summary_->setStyleSheet(QStringLiteral("color:#9fb4c8;"));
    root->addWidget(summary_);

    auto* foot = new QHBoxLayout;
    foot->addStretch(1);
    auto* saveBtn = new QPushButton(QStringLiteral("Save"), this);
    saveBtn->setDefault(true);
    saveBtn->setToolTip(QStringLiteral("Into the template folder, under the name above"));
    connect(saveBtn, &QPushButton::clicked, this, [this] { saveToFolder(); });
    foot->addWidget(saveBtn);
    auto* saveAsBtn = new QPushButton(QStringLiteral("Save as…"), this);
    connect(saveAsBtn, &QPushButton::clicked, this, [this] { saveAs(); });
    foot->addWidget(saveAsBtn);
    auto* openFileBtn = new QPushButton(QStringLiteral("Open…"), this);
    connect(openFileBtn, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open a template"),
                                                          AppPaths::templateDir(),
                                                          DrawingTemplate::fileFilter());
        if (!path.isEmpty()) loadFrom(path);
    });
    foot->addWidget(openFileBtn);
    auto* closeBtn = new QPushButton(QStringLiteral("Close"), this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    foot->addWidget(closeBtn);
    root->addLayout(foot);
}

// ---- Controls <-> template ----------------------------------------------------------------------

void TemplateDialog::readFromControls() {
    if (nameEdit_) tpl_.name = nameEdit_->text();
    if (descEdit_) tpl_.description = descEdit_->toPlainText();
    if (authorEdit_) tpl_.author = authorEdit_->text();
    if (revEdit_) tpl_.revision = revEdit_->text();
    auto readSig = [](const SignatureRow& row, TemplateSignature& into) {
        if (row.name) into.name = row.name->text();
        if (row.date) into.date = row.date->text();
    };
    readSig(drawnRow_, tpl_.drawn);
    readSig(checkedRow_, tpl_.checked);
    readSig(approvedRow_, tpl_.approved);
    if (sheetBox_) {
        const QString picked = sheetBox_->currentData().toString();
        tpl_.sheet.name = picked.isEmpty() ? QStringLiteral("Custom") : picked;
    }
    if (widthSpin_) tpl_.sheet.widthMm = widthSpin_->value();
    if (heightSpin_) tpl_.sheet.heightMm = heightSpin_->value();
    if (landscapeChk_) tpl_.sheet.landscape = landscapeChk_->isChecked();
    if (zonesChk_) tpl_.zones.enabled = zonesChk_->isChecked();
    if (zoneSizeSpin_) tpl_.zones.sizeMm = zoneSizeSpin_->value();
    if (zoneBandSpin_) tpl_.zones.bandMm = zoneBandSpin_->value();
    if (lettersTopChk_) tpl_.zones.lettersFromTop = lettersTopChk_->isChecked();
    if (revChk_) tpl_.revisions.enabled = revChk_->isChecked();
    if (revRowsSpin_) tpl_.revisions.rows = revRowsSpin_->value();
    if (revRowHSpin_) tpl_.revisions.rowHeightMm = revRowHSpin_->value();
    {
        bool* flags[7] = {&tpl_.revisions.showRev,      &tpl_.revisions.showNote,
                          &tpl_.revisions.showEco,      &tpl_.revisions.showDrawn,
                          &tpl_.revisions.showChecked,  &tpl_.revisions.showApproved,
                          &tpl_.revisions.showDate};
        double* widths[7] = {&tpl_.revisions.wRev,     &tpl_.revisions.wNote,
                             &tpl_.revisions.wEco,     &tpl_.revisions.wDrawn,
                             &tpl_.revisions.wChecked, &tpl_.revisions.wApproved,
                             &tpl_.revisions.wDate};
        for (int i = 0; i < 7; ++i) {
            if (revCols_[i].show) *flags[i] = revCols_[i].show->isChecked();
            if (revCols_[i].width) *widths[i] = revCols_[i].width->value();
        }
    }
    if (fields_) {
        std::vector<TemplateField> out;
        for (int i = 0; i < fields_->rowCount(); ++i) {
            TemplateField f;
            auto cell = [this, i](int c) {
                const QTableWidgetItem* it = fields_->item(i, c);
                return it ? it->text().trimmed() : QString();
            };
            f.key = cell(0);
            f.label = cell(1);
            f.value = cell(2);
            f.textHeight = cell(3).toDouble();
            if (f.textHeight <= 0.0) f.textHeight = 3.5;
            if (i < static_cast<int>(tpl_.fields.size()))
                f.builtIn = tpl_.fields[static_cast<size_t>(i)].builtIn;
            if (!f.key.isEmpty()) out.push_back(f);
        }
        tpl_.fields = out;
    }
    if (activeLayerBox_) tpl_.activeLayer = activeLayerBox_->currentText();
    if (unitBox_) tpl_.unit = unitBox_->currentText();
    if (textSpin_) tpl_.textHeight = textSpin_->value();
    if (dimTextSpin_) tpl_.dimTextHeight = dimTextSpin_->value();
    if (arrowSpin_) tpl_.dimArrowSize = arrowSpin_->value();
    if (decimalsSpin_) tpl_.dimDecimals = decimalsSpin_->value();
    if (gridSpin_) tpl_.gridSize = gridSpin_->value();
    if (gridChk_) tpl_.gridOn = gridChk_->isChecked();
    if (snapChk_) tpl_.snapOn = snapChk_->isChecked();
    if (awgChk_) tpl_.gaugeAwg = awgChk_->isChecked();
}

void TemplateDialog::writeToControls() {
    syncing_ = true;
    if (nameEdit_) nameEdit_->setText(tpl_.name);
    if (descEdit_) descEdit_->setPlainText(tpl_.description);
    if (authorEdit_) authorEdit_->setText(tpl_.author);
    if (revEdit_) revEdit_->setText(tpl_.revision);
    auto writeSig = [](const SignatureRow& row, const TemplateSignature& from) {
        if (row.name) row.name->setText(from.name);
        if (row.date) row.date->setText(from.date);
    };
    writeSig(drawnRow_, tpl_.drawn);
    writeSig(checkedRow_, tpl_.checked);
    writeSig(approvedRow_, tpl_.approved);
    if (sheetBox_) {
        const int at = sheetBox_->findData(tpl_.sheet.name);
        sheetBox_->setCurrentIndex(at >= 0 ? at : sheetBox_->count() - 1);
    }
    if (widthSpin_) widthSpin_->setValue(tpl_.sheet.widthMm);
    if (heightSpin_) heightSpin_->setValue(tpl_.sheet.heightMm);
    if (landscapeChk_) landscapeChk_->setChecked(tpl_.sheet.landscape);
    if (zonesChk_) zonesChk_->setChecked(tpl_.zones.enabled);
    if (zoneSizeSpin_) {
        zoneSizeSpin_->setValue(tpl_.zones.sizeMm);
        zoneSizeSpin_->setEnabled(tpl_.zones.enabled);
    }
    if (zoneBandSpin_) {
        zoneBandSpin_->setValue(tpl_.zones.bandMm);
        zoneBandSpin_->setEnabled(tpl_.zones.enabled);
    }
    if (lettersTopChk_) {
        lettersTopChk_->setChecked(tpl_.zones.lettersFromTop);
        lettersTopChk_->setEnabled(tpl_.zones.enabled);
    }
    if (revChk_) revChk_->setChecked(tpl_.revisions.enabled);
    if (revRowsSpin_) {
        revRowsSpin_->setValue(tpl_.revisions.rows);
        revRowsSpin_->setEnabled(tpl_.revisions.enabled);
    }
    if (revRowHSpin_) {
        revRowHSpin_->setValue(tpl_.revisions.rowHeightMm);
        revRowHSpin_->setEnabled(tpl_.revisions.enabled);
    }
    {
        const bool flags[7] = {tpl_.revisions.showRev,     tpl_.revisions.showNote,
                               tpl_.revisions.showEco,     tpl_.revisions.showDrawn,
                               tpl_.revisions.showChecked, tpl_.revisions.showApproved,
                               tpl_.revisions.showDate};
        const double widths[7] = {tpl_.revisions.wRev,     tpl_.revisions.wNote,
                                  tpl_.revisions.wEco,     tpl_.revisions.wDrawn,
                                  tpl_.revisions.wChecked, tpl_.revisions.wApproved,
                                  tpl_.revisions.wDate};
        for (int i = 0; i < 7; ++i) {
            if (revCols_[i].show) {
                revCols_[i].show->setChecked(flags[i]);
                revCols_[i].show->setEnabled(tpl_.revisions.enabled);
            }
            if (revCols_[i].width) {
                revCols_[i].width->setValue(widths[i]);
                revCols_[i].width->setEnabled(tpl_.revisions.enabled && flags[i]);
            }
        }
    }
    if (fields_) {
        if (tpl_.fields.empty()) tpl_.fields = DrawingTemplate::defaultFields();
        fields_->setRowCount(static_cast<int>(tpl_.fields.size()));
        for (int i = 0; i < static_cast<int>(tpl_.fields.size()); ++i) {
            const TemplateField& f = tpl_.fields[static_cast<size_t>(i)];
            fields_->setItem(i, 0, new QTableWidgetItem(f.key));
            fields_->setItem(i, 1, new QTableWidgetItem(f.label));
            fields_->setItem(i, 2, new QTableWidgetItem(f.value));
            fields_->setItem(i, 3, new QTableWidgetItem(QString::number(f.textHeight, 'f', 1)));
            // The key of a box the title block places is what ties it to its cell, so it is fixed.
            if (f.builtIn) {
                QTableWidgetItem* k = fields_->item(i, 0);
                k->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
                k->setToolTip(QStringLiteral("The title block places this one, so its key is fixed"));
            }
        }
        fields_->resizeColumnsToContents();
    }
    if (unitBox_) unitBox_->setCurrentText(tpl_.unit);
    if (textSpin_) textSpin_->setValue(tpl_.textHeight);
    if (dimTextSpin_) dimTextSpin_->setValue(tpl_.dimTextHeight);
    if (arrowSpin_) arrowSpin_->setValue(tpl_.dimArrowSize);
    if (decimalsSpin_) decimalsSpin_->setValue(tpl_.dimDecimals);
    if (gridSpin_) gridSpin_->setValue(tpl_.gridSize);
    if (gridChk_) gridChk_->setChecked(tpl_.gridOn);
    if (snapChk_) snapChk_->setChecked(tpl_.snapOn);
    if (awgChk_) awgChk_->setChecked(tpl_.gaugeAwg);
    syncing_ = false;
    refreshLayerTable();
}

void TemplateDialog::refreshLayerTable() {
    if (!layers_) return;
    syncing_ = true;
    layers_->setRowCount(static_cast<int>(tpl_.layers.size()));
    for (int i = 0; i < static_cast<int>(tpl_.layers.size()); ++i) {
        const Layer& l = tpl_.layers[static_cast<size_t>(i)];
        layers_->setItem(i, kLayName, new QTableWidgetItem(l.name));
        auto* colour = new QTableWidgetItem(l.color);
        colour->setBackground(QColor(l.color));
        colour->setToolTip(QStringLiteral("Double-click to pick a colour"));
        layers_->setItem(i, kLayColour, colour);
        layers_->setItem(i, kLayWeight, new QTableWidgetItem(QString::number(l.weight, 'f', 2)));
        for (int col : {kLayVisible, kLayLocked}) {
            auto* it = new QTableWidgetItem;
            it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
            it->setCheckState((col == kLayVisible ? l.visible : l.locked) ? Qt::Checked
                                                                        : Qt::Unchecked);
            layers_->setItem(i, col, it);
        }
    }
    layers_->resizeColumnsToContents();
    if (activeLayerBox_) {
        const QString keep = tpl_.activeLayer;
        activeLayerBox_->clear();
        for (const Layer& l : tpl_.layers) activeLayerBox_->addItem(l.name);
        const int at = activeLayerBox_->findText(keep);
        activeLayerBox_->setCurrentIndex(at >= 0 ? at : 0);
    }
    syncing_ = false;
}

void TemplateDialog::layersFromTable() {
    std::vector<Layer> out;
    for (int i = 0; i < layers_->rowCount(); ++i) {
        Layer l;
        auto text = [this, i](int col) {
            const QTableWidgetItem* it = layers_->item(i, col);
            return it ? it->text().trimmed() : QString();
        };
        l.name = text(kLayName);
        if (l.name.isEmpty()) l.name = QStringLiteral("layer %1").arg(i + 1);
        l.color = text(kLayColour).isEmpty() ? QStringLiteral("#ffffff") : text(kLayColour);
        l.weight = text(kLayWeight).toDouble();
        if (l.weight <= 0.0) l.weight = 2.0;
        const QTableWidgetItem* vis = layers_->item(i, kLayVisible);
        const QTableWidgetItem* lock = layers_->item(i, kLayLocked);
        l.visible = !vis || vis->checkState() == Qt::Checked;
        l.locked = lock && lock->checkState() == Qt::Checked;
        out.push_back(l);
    }
    tpl_.layers = out;
    readFromControls();
}

void TemplateDialog::refreshPreview() {
    if (revSummary_) {
        const QStringList heads = tpl_.revisions.headings();
        revSummary_->setText(
            !tpl_.revisions.enabled
                ? QStringLiteral("no revision table")
                : heads.isEmpty()
                      ? QStringLiteral("every column is switched off, so nothing is drawn")
                      : QStringLiteral("%1  —  %2 × %3 mm")
                            .arg(heads.join(QStringLiteral(" | ")))
                            .arg(tpl_.revisions.totalWidthMm(), 0, 'f', 0)
                            .arg(tpl_.revisions.totalHeightMm(), 0, 'f', 0));
    }
    if (zoneCount_) {
        if (!tpl_.zones.enabled) {
            zoneCount_->setText(QStringLiteral("no zone frame"));
        } else {
            const int cols = templateZoneColumns(tpl_.sheet, tpl_.zones);
            const int rows = templateZoneRows(tpl_.sheet, tpl_.zones);
            const double band = std::clamp(tpl_.zones.bandMm, 3.0,
                                           std::min(tpl_.sheet.drawnWidthMm(),
                                                    tpl_.sheet.drawnHeightMm()) * 0.1);
            zoneCount_->setText(
                QStringLiteral("%1 × %2 zones, %3 to %4 — divisions of %5 × %6 mm")
                    .arg(cols)
                    .arg(rows)
                    .arg(templateZoneRowLabel(0) + templateZoneColumnLabel(0))
                    .arg(templateZoneRowLabel(rows - 1) + templateZoneColumnLabel(cols - 1))
                    .arg((tpl_.sheet.drawnWidthMm() - 2 * band) / cols, 0, 'f', 1)
                    .arg((tpl_.sheet.drawnHeightMm() - 2 * band) / rows, 0, 'f', 1));
        }
    }
    // While the sheet is the generated one, it is regenerated so a name or a date typed above shows
    // up in the title block at once. A sheet captured from a drawing is left exactly as captured -
    // regenerating over someone's own border would throw their work away.
    if (generatedContent_) tpl_.content = defaultSheetContent(tpl_);
    if (!preview_) return;
    preview_->setEntities(partDrawingEntities(tpl_.content));
}

void TemplateDialog::setTemplateData(const DrawingTemplate& tpl) {
    tpl_ = tpl;
    writeToControls();
    refreshPreview();
}

// ---- Content -----------------------------------------------------------------------------------

void TemplateDialog::resetContent() {
    readFromControls();
    tpl_.content = defaultSheetContent(tpl_);
    generatedContent_ = true;
    refreshPreview();
    if (summary_)
        summary_->setText(QStringLiteral("A %1 sheet with a border and a title block.")
                              .arg(tpl_.sheet.name));
}

void TemplateDialog::captureFromDrawing() {
    if (!editor_ || !editor_->document()) return;
    const Document& doc = *editor_->document();
    if (doc.entities().empty()) {
        if (summary_)
            summary_->setText(QStringLiteral(
                "Nothing on the sheet to capture. Draw the border and the title block first."));
        return;
    }
    readFromControls();
    const DrawingTemplate taken = templateFromDocument(doc, tpl_.name);
    tpl_.content = taken.content;
    generatedContent_ = false;   // it is theirs now, not ours to redraw
    tpl_.layers = taken.layers;
    tpl_.activeLayer = taken.activeLayer;
    tpl_.unit = taken.unit;
    writeToControls();
    refreshPreview();
    if (summary_)
        summary_->setText(QStringLiteral("Captured %1 object(s) and %2 layer(s) from the sheet.")
                              .arg(tpl_.content.size())
                              .arg(tpl_.layers.size()));
}

void TemplateDialog::applyToDrawing() {
    if (!editor_ || !editor_->document()) return;
    readFromControls();
    applyTemplate(tpl_, *editor_->document());
    // The gauge notation is an application-wide setting rather than a document one.
    setWireGaugeNotationAwg(tpl_.gaugeAwg);
    editor_->clearSelection();
    if (summary_)
        summary_->setText(QStringLiteral(
            "Applied to the drawing: %1 layer(s) and %2 object(s). What was already drawn is still "
            "there.")
                              .arg(tpl_.layers.size())
                              .arg(tpl_.content.size()));
}

// ---- The template a new drawing starts from ------------------------------------------------------

QString TemplateDialog::defaultTemplatePath() {
    QSettings st;
    const QString path = st.value(QStringLiteral("edraw/template/default")).toString();
    // A default that has been deleted is no default at all.
    return (path.isEmpty() || !QFile::exists(path)) ? QString() : path;
}

void TemplateDialog::setDefaultTemplatePath(const QString& path) {
    QSettings st;
    if (path.isEmpty()) st.remove(QStringLiteral("edraw/template/default"));
    else st.setValue(QStringLiteral("edraw/template/default"), path);
}

// ---- Files -------------------------------------------------------------------------------------

bool TemplateDialog::saveToFolder() {
    readFromControls();
    layersFromTable();
    const QString path = DrawingTemplate::pathFor(tpl_.name);
    if (!tpl_.save(path)) {
        QMessageBox::warning(this, QStringLiteral("Drawing template"),
                             QStringLiteral("Could not write %1").arg(path));
        return false;
    }
    path_ = path;
    refreshInstalled();
    if (summary_) summary_->setText(QStringLiteral("Saved %1").arg(path));
    return true;
}

bool TemplateDialog::saveAs() {
    readFromControls();
    layersFromTable();
    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save the template"),
                                                path_.isEmpty()
                                                    ? DrawingTemplate::pathFor(tpl_.name)
                                                    : path_,
                                                DrawingTemplate::fileFilter());
    if (path.isEmpty()) return false;
    const QString suffix = QStringLiteral(".") + DrawingTemplate::extension();
    if (!path.endsWith(suffix, Qt::CaseInsensitive)) path += suffix;
    if (!tpl_.save(path)) {
        QMessageBox::warning(this, QStringLiteral("Drawing template"),
                             QStringLiteral("Could not write %1").arg(path));
        return false;
    }
    path_ = path;
    refreshInstalled();
    if (summary_) summary_->setText(QStringLiteral("Saved %1").arg(path));
    return true;
}

bool TemplateDialog::loadFrom(const QString& path) {
    DrawingTemplate loaded;
    if (!loaded.load(path)) {
        if (summary_)
            summary_->setText(QStringLiteral("%1 is not an Loopwire template.")
                                  .arg(QFileInfo(path).fileName()));
        return false;
    }
    tpl_ = loaded;
    path_ = path;
    generatedContent_ = false;   // what was saved is what it is
    writeToControls();
    refreshPreview();
    if (summary_)
        summary_->setText(QStringLiteral("Opened %1 — %2 object(s), %3 layer(s)%4")
                              .arg(QFileInfo(path).fileName())
                              .arg(tpl_.content.size())
                              .arg(tpl_.layers.size())
                              .arg(tpl_.created.isEmpty()
                                       ? QString()
                                       : QStringLiteral(", saved %1").arg(tpl_.created)));
    return true;
}

void TemplateDialog::refreshInstalled() {
    if (!installed_) return;
    installed_->clear();
    const QString def = defaultTemplatePath();
    for (const QString& path : DrawingTemplate::installed()) {
        DrawingTemplate t;
        const bool ok = t.load(path);
        const bool isDefault = !def.isEmpty() && QFileInfo(def) == QFileInfo(path);
        auto* item = new QListWidgetItem(
            ok ? QStringLiteral("%1%2   —   %3 object(s)%4")
                     .arg(isDefault ? QStringLiteral("★  ") : QString())
                     .arg(t.name.isEmpty() ? QFileInfo(path).completeBaseName() : t.name)
                     .arg(t.content.size())
                     .arg(t.sheet.name.isEmpty() ? QString()
                                                 : QStringLiteral(", %1").arg(t.sheet.name))
               : QStringLiteral("%1   (not a template)").arg(QFileInfo(path).fileName()),
            installed_);
        item->setData(Qt::UserRole, path);
        if (!ok) item->setForeground(QColor("#e6b45a"));
        else if (isDefault) {
            item->setForeground(QColor("#7ee0a0"));
            item->setToolTip(QStringLiteral("A new drawing starts from this one"));
        }
    }
    if (installed_->count() == 0) {
        auto* item = new QListWidgetItem(QStringLiteral("none saved yet"), installed_);
        item->setFlags(Qt::NoItemFlags);
    }
}

} // namespace Loopwire
