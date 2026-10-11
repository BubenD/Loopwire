#include "app/SystemDesignView.h"

#include "app/DrawingFinderDialog.h"
#include "app/UiBits.h"
#include "system/SystemImport.h"
#include "app/RuleExplainer.h"
#include "system/SystemExport.h"

#include <QComboBox>
#include <QCheckBox>
#include <QJsonArray>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QInputDialog>
#include <QPlainTextEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTableWidget>
#include <QSettings>
#include <QTextStream>
#include <QVBoxLayout>
#include <QColorDialog>
#include <QFrame>
#include <QGroupBox>
#include <QPainter>
#include <QPalette>
#include <QPainterPath>
#include <QSlider>
#include <QToolButton>
#include <QApplication>
#include <QBuffer>
#include <QClipboard>
#include <QImage>
#include <QJsonDocument>
#include <QMenu>
#include <QMimeData>
#include "system/SystemRouting.h"
#include "model/DrawingTemplate.h"
#include "render/EntityRenderer.h"
#include "model/Entity.h"
#include <QPdfWriter>
#include <QPageSize>

#include <algorithm>

namespace Edraw_Desktop {

namespace {

// AN EYE, AS THE MECHANICAL WIZARD'S: open - shown on the diagram; struck through - not. The same drawing and colours,
// one for each state of the button.
QIcon fieldEyeIcon() {
    QIcon icon;
    icon.addPixmap(eyeIcon(true).pixmap(20, 20), QIcon::Normal, QIcon::On);
    icon.addPixmap(eyeIcon(false).pixmap(20, 20), QIcon::Normal, QIcon::Off);
    return icon;
}

const char* const kShown[4] = {"name", "pn", "rev", "desc"};

QLineEdit* field(QWidget* parent, const char* name, const QString& tip) {
    auto* e = new QLineEdit(parent);
    e->setObjectName(QLatin1String(name));
    e->setToolTip(tip);
    return e;
}

} // namespace

SystemDesignView::SystemDesignView(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("systemDesign"));
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    auto* outer = new QSplitter(Qt::Vertical, this);
    outer->setObjectName(QStringLiteral("systemResultsSplit"));
    auto* split = new QSplitter(Qt::Horizontal, outer);
    canvas_ = new SystemCanvas(split);
    canvas_->setDesign(&design_);
    // ON THE RIGHT: WHAT IS SELECTED.
    panel_ = new QStackedWidget(split);
    panel_->setObjectName(QStringLiteral("systemPanel"));
    panel_->setMinimumWidth(360);
    nothing_ = new QLabel(QStringLiteral("<b>System design</b><br><br>Put units with <i>Unit</i>, ports on their sides with "
                                         "<i>Port</i>, and draw cables from port to port with <i>Cable</i> (or drag from a "
                                         "port). Click a unit or a cable to see and change what is its own here."),
                          panel_);
    nothing_->setWordWrap(true);
    nothing_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    nothing_->setMargin(8);
    {
        // NOTHING SELECTED: the design's own - and its sheet: the template round it, and what its title block says.
        auto* page = new QWidget(panel_);
        auto* v = new QVBoxLayout(page);
        v->setContentsMargins(0, 0, 0, 0);
        nothing_->setParent(page);
        v->addWidget(nothing_);
        auto* sheetBox = new QGroupBox(QStringLiteral("Sheet (template)"), page);
        sheetBox->setObjectName(QStringLiteral("systemSheetBox"));
        auto* sv = new QVBoxLayout(sheetBox);
        sheetName_ = new QLabel(sheetBox);
        sheetName_->setObjectName(QStringLiteral("systemSheetName"));
        sheetName_->setWordWrap(true);
        sv->addWidget(sheetName_);
        auto* buttons = new QHBoxLayout;
        auto* choose = new QPushButton(QStringLiteral("Choose template..."), sheetBox);
        choose->setObjectName(QStringLiteral("systemSheetChoose"));
        connect(choose, &QPushButton::clicked, this, [this] { applyTemplate(); });
        auto* drop = new QPushButton(QStringLiteral("Remove"), sheetBox);
        drop->setObjectName(QStringLiteral("systemSheetRemove"));
        connect(drop, &QPushButton::clicked, this, [this] { removeSheet(); });
        buttons->addWidget(choose);
        buttons->addWidget(drop);
        buttons->addStretch(1);
        sv->addLayout(buttons);
        sheetFields_ = new QWidget(sheetBox);
        sheetFields_->setObjectName(QStringLiteral("systemSheetFields"));
        new QFormLayout(sheetFields_);
        sv->addWidget(sheetFields_);
        v->addWidget(sheetBox);
        v->addStretch(1);
        panel_->addWidget(page);
    }
    // A UNIT.
    {
        auto* page = new QWidget(panel_);
        auto* v = new QVBoxLayout(page);
        auto* form = new QFormLayout;
        unitName_ = field(page, "systemUnitName", QStringLiteral("The unit's name on the diagram"));
        unitPn_ = field(page, "systemUnitPn", QStringLiteral("Its part number"));
        unitRev_ = field(page, "systemUnitRev", QStringLiteral("Its revision"));
        unitDesc_ = field(page, "systemUnitDesc", QStringLiteral("What it is"));
        unitKind_ = new QComboBox(page);
        unitKind_->setObjectName(QStringLiteral("systemUnitKind"));
        unitKind_->addItem(QStringLiteral("Mechanical part"), QStringLiteral("mech"));
        unitKind_->addItem(QStringLiteral("PCBA"), QStringLiteral("pcba"));
        unitKind_->addItem(QStringLiteral("System module"),QStringLiteral("module"));
        unitKind_->addItem(QStringLiteral("Device"), QStringLiteral("device"));
        unitKind_->setToolTip(QStringLiteral("What the unit is - a mechanical part or a printed circuit board assembly; its mark "
                                             "is in its corner on the diagram"));
        connect(unitKind_, &QComboBox::currentIndexChanged, this, [this](int i) {
            SysUnit* u = design_.unit(canvas_->selectedUnit());
            if (filling_ || !u || i < 0 || u->kind == unitKind_->itemData(i).toString()) return;
            aboutToChange();
            u->kind = unitKind_->itemData(i).toString();if(u->kind==QLatin1String("module"))u->opacity=.15;
            commit();
        });
        form->addRow(QStringLiteral("Kind"), unitKind_);
        // EACH FIELD WITH ITS EYE: shown on the unit on the diagram, or not.
        auto withEye = [this, page](QLineEdit* edit, int which, bool cable) {
            auto* row = new QWidget(page);
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            h->setSpacing(2);
            h->addWidget(edit, 1);
            auto* eye = new QToolButton(row);
            eye->setObjectName(edit->objectName() + QStringLiteral("Eye"));
            eye->setCheckable(true);
            eye->setChecked(true);
            eye->setAutoRaise(true);
            eye->setIcon(fieldEyeIcon());
            eye->setIconSize(QSize(18, 18));
            eye->setToolTip(QStringLiteral("Shown on the diagram - click to hide it there (it is kept all the same)"));
            h->addWidget(eye);
            (cable ? cableEyes_ : unitEyes_)[which] = eye;
            connect(eye, &QToolButton::toggled, this, [this, which, cable](bool on) {
                if (filling_) return;
                QStringList* hidden = nullptr;
                if (cable) {
                    if (SysCable* c = design_.cable(canvas_->selectedCable())) hidden = &c->hidden;
                } else if (SysUnit* u = design_.unit(canvas_->selectedUnit())) {
                    hidden = &u->hidden;
                }
                const QString key = QString::fromLatin1(kShown[which]);
                if (!hidden || hidden->contains(key) == !on) return;
                aboutToChange();
                if (on) hidden->removeAll(key);
                else hidden->append(key);
                commit();
            });
            return row;
        };
        form->addRow(QStringLiteral("Name"), withEye(unitName_, 0, false));
        form->addRow(QStringLiteral("P/N"), withEye(unitPn_, 1, false));
        form->addRow(QStringLiteral("Rev"), withEye(unitRev_, 2, false));
        form->addRow(QStringLiteral("Description"), withEye(unitDesc_, 3, false));
        // ITS COLOUR, and how solid it is drawn.
        auto* look = new QWidget(page);
        auto* lh = new QHBoxLayout(look);
        lh->setContentsMargins(0, 0, 0, 0);
        unitColour_ = new QToolButton(look);
        unitColour_->setObjectName(QStringLiteral("systemUnitColour"));
        unitColour_->setText(QStringLiteral("Colour..."));
        unitColour_->setToolTip(QStringLiteral("The unit's colour on the diagram"));
        connect(unitColour_, &QToolButton::clicked, this, [this] {
            const SysUnit* u = design_.unit(canvas_->selectedUnit());
            if (!u) return;
            const QColor c = QColorDialog::getColor(u->color.isValid() ? u->color : QColor(232, 238, 246), this, QStringLiteral("Unit colour"));
            if (c.isValid()) setUnitColour(c);
        });
        auto* plain = new QToolButton(look);
        plain->setObjectName(QStringLiteral("systemUnitColourDefault"));
        plain->setText(QStringLiteral("As its kind"));
        plain->setToolTip(QStringLiteral("Back to the colour of its kind - grey-blue for a mechanical part, green for a PCBA"));
        connect(plain, &QToolButton::clicked, this, [this] { setUnitColour(QColor()); });
        unitOpacity_ = new QSlider(Qt::Horizontal, look);
        unitOpacity_->setObjectName(QStringLiteral("systemUnitOpacity"));
        unitOpacity_->setRange(0,100);
        unitOpacity_->setToolTip(QStringLiteral("How solid the unit is filled - less, and what is behind it shows through"));
        connect(unitOpacity_, &QSlider::valueChanged, this, [this](int v) {
            if (!filling_) setUnitOpacity(v / 100.0);
        });
        lh->addWidget(unitColour_);
        lh->addWidget(plain);
        lh->addWidget(unitOpacity_, 1);
        form->addRow(QStringLiteral("Look"), look);
        // ITS PICTURE: seen here, and in the unit between its mark and its name - its eye saying whether, its slider how big.
        auto* pictureBox = new QWidget(page);
        auto* pv = new QVBoxLayout(pictureBox);
        pv->setContentsMargins(0, 0, 0, 0);
        pv->setSpacing(3);
        unitPicturePreview_ = new QLabel(pictureBox);
        unitPicturePreview_->setObjectName(QStringLiteral("systemUnitPicturePreview"));
        unitPicturePreview_->setAlignment(Qt::AlignCenter);
        unitPicturePreview_->setMinimumHeight(90);
        unitPicturePreview_->setStyleSheet(QStringLiteral("QLabel{border:1px solid #4a5260;border-radius:3px;color:#8a93a0;}"));
        pv->addWidget(unitPicturePreview_);
        auto* pictureRow = new QWidget(pictureBox);
        pv->addWidget(pictureRow);
        auto* ph = new QHBoxLayout(pictureRow);
        ph->setContentsMargins(0, 0, 0, 0);
        unitPictureEye_ = new QToolButton(pictureRow);
        unitPictureEye_->setObjectName(QStringLiteral("systemUnitPictureEye"));
        unitPictureEye_->setCheckable(true);
        unitPictureEye_->setChecked(true);
        unitPictureEye_->setAutoRaise(true);
        unitPictureEye_->setIcon(fieldEyeIcon());
        unitPictureEye_->setIconSize(QSize(18, 18));
        unitPictureEye_->setToolTip(QStringLiteral("The picture shown in the unit on the diagram - click to hide it there (it is kept)"));
        connect(unitPictureEye_, &QToolButton::toggled, this, [this](bool on) {
            if (!filling_) setUnitPictureShown(canvas_->selectedUnit(), on);
        });
        ph->addWidget(unitPictureEye_);
        auto* putPicture = new QToolButton(pictureRow);
        putPicture->setObjectName(QStringLiteral("systemUnitPicture"));
        putPicture->setText(QStringLiteral("Insert picture..."));
        putPicture->setToolTip(QStringLiteral("Put a picture (PNG, JPG, BMP...) in the unit - shown in it above its name, kept in the design"));
        connect(putPicture, &QToolButton::clicked, this, [this] { setUnitPicture(canvas_->selectedUnit()); });
        auto* dropPicture = new QToolButton(pictureRow);
        dropPicture->setObjectName(QStringLiteral("systemUnitPictureRemove"));
        dropPicture->setText(QStringLiteral("Remove"));
        connect(dropPicture, &QToolButton::clicked, this, [this] { removeUnitPicture(canvas_->selectedUnit()); });
        auto* keepPicture = new QToolButton(pictureRow);
        keepPicture->setObjectName(QStringLiteral("systemUnitPictureSave"));
        keepPicture->setText(QStringLiteral("Save..."));
        keepPicture->setToolTip(QStringLiteral("Save the unit's picture to a file"));
        connect(keepPicture, &QToolButton::clicked, this, [this] { saveUnitPicture(canvas_->selectedUnit()); });
        ph->addWidget(putPicture);
        ph->addWidget(keepPicture);
        ph->addWidget(dropPicture);
        ph->addStretch(1);
        unitPictureSize_ = new QSlider(Qt::Horizontal, pictureBox);
        unitPictureSize_->setObjectName(QStringLiteral("systemUnitPictureSize"));
        unitPictureSize_->setRange(10, 100);
        unitPictureSize_->setToolTip(QStringLiteral("How big the picture is in the unit - as much of the room as there is, or less"));
        connect(unitPictureSize_, &QSlider::valueChanged, this, [this](int v) {
            if (!filling_) setUnitPictureScale(canvas_->selectedUnit(), v / 100.0);
        });
        pv->addWidget(unitPictureSize_);
        form->addRow(QStringLiteral("Picture"), pictureBox);
        v->addLayout(form);
        v->addWidget(new QLabel(QStringLiteral("<b>Ports</b> - connector and contact part numbers"), page));
        ports_ = new QTableWidget(0, 5, page);
        ports_->setObjectName(QStringLiteral("systemPorts"));
        ports_->setHorizontalHeaderLabels({QStringLiteral("Designator"), QStringLiteral("Side"), QStringLiteral("Connector P/N"),
                                           QStringLiteral("Contact P/N"), QStringLiteral("Description")});
        ports_->setToolTip(QStringLiteral("One connector P/N for the port, the part number of its contacts and what the port is for. "
                                          "Drag the designator on the diagram to move its label independently."));
        ports_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        ports_->horizontalHeader()->setStretchLastSection(true);
        ports_->verticalHeader()->setVisible(false);
        v->addWidget(ports_, 1);
        auto* buttons = new QHBoxLayout;
        auto* add = new QPushButton(QStringLiteral("Add port"), page);
        auto* remove = new QPushButton(QStringLiteral("Remove port"), page);
        buttons->addWidget(add);
        buttons->addWidget(remove);
        v->addLayout(buttons);
        auto applyUnit = [this] {
            SysUnit* u = design_.unit(canvas_->selectedUnit());
            if (filling_ || !u) return;
            if (u->name == unitName_->text() && u->pn == unitPn_->text() && u->rev == unitRev_->text() && u->description == unitDesc_->text())
                return;
            aboutToChange();
            u->name = unitName_->text();
            u->pn = unitPn_->text();
            u->rev = unitRev_->text();
            u->description = unitDesc_->text();
            commit();
        };
        for (QLineEdit* e : {unitName_, unitPn_, unitRev_, unitDesc_}) connect(e, &QLineEdit::editingFinished, this, applyUnit);
        connect(ports_, &QTableWidget::cellChanged, this, [this](int row, int col) {
            SysUnit* u = design_.unit(canvas_->selectedUnit());
            if (filling_ || !u || row < 0 || row >= static_cast<int>(u->ports.size())) return;
            if (col == 1) return;   // the side is a box of its own
            aboutToChange();
            SysPort& p = u->ports[static_cast<size_t>(row)];
            const QString text = ports_->item(row, col) ? ports_->item(row, col)->text() : QString();
            if (col == 0) p.name = text;
            else if (col == 2) p.connector = text;
            else if (col == 3) p.contactPn = text;
            else if (col == 4) p.description = text;
            commit();
        });
        connect(add, &QPushButton::clicked, this, [this] {
            if (!design_.unit(canvas_->selectedUnit())) return;
            aboutToChange();
            design_.addPort(canvas_->selectedUnit(), QStringLiteral("right"), 0.5);
            commit();
        });
        connect(remove, &QPushButton::clicked, this, [this] {
            SysUnit* u = design_.unit(canvas_->selectedUnit());
            const int row = ports_->currentRow();
            if (!u || row < 0 || row >= static_cast<int>(u->ports.size())) return;
            aboutToChange();
            design_.removePort(u->id, u->ports[static_cast<size_t>(row)].id);
            commit();
        });
        panel_->addWidget(page);
    }
    // A CABLE.
    {
        auto* page = new QWidget(panel_);
        auto* v = new QVBoxLayout(page);
        auto* form = new QFormLayout;
        cableName_ = field(page, "systemCableName", QStringLiteral("The cable's name on the diagram (W1, W2...)"));
        cablePn_ = field(page, "systemCablePn", QStringLiteral("Its part number - the drawing linked should be this number"));
        cableRev_ = field(page, "systemCableRev", QStringLiteral("The revision the system calls for - the drawing linked should be it"));
        cableDesc_ = field(page, "systemCableDesc", QStringLiteral("What it is"));
        cableLength_ = new QDoubleSpinBox(page);
        cableLength_->setObjectName(QStringLiteral("systemCableLength"));
        cableLength_->setRange(0.0, 1000000.0);
        cableLength_->setDecimals(0);
        cableLength_->setSuffix(QStringLiteral(" mm"));
        cableLength_->setKeyboardTracking(false);
        cableWires_ = new QSpinBox(page);
        cableWires_->setObjectName(QStringLiteral("systemCableWires"));
        cableWires_->setRange(0, 9999);
        cableWires_->setKeyboardTracking(false);
        cableEnds_ = new QLabel(page);
        cableEnds_->setWordWrap(true);
        // EACH FIELD WITH ITS EYE: shown on the cable's label, or not.
        auto withEye = [this, page](QLineEdit* edit, int which) {
            auto* row = new QWidget(page);
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(0, 0, 0, 0);
            h->setSpacing(2);
            h->addWidget(edit, 1);
            auto* eye = new QToolButton(row);
            eye->setObjectName(edit->objectName() + QStringLiteral("Eye"));
            eye->setCheckable(true);
            eye->setAutoRaise(true);
            eye->setIcon(fieldEyeIcon());
            eye->setIconSize(QSize(18, 18));
            eye->setToolTip(QStringLiteral("Shown on the cable's label - click to hide it there, or show it"));
            h->addWidget(eye);
            cableEyes_[which] = eye;
            connect(eye, &QToolButton::toggled, this, [this, which](bool on) {
                SysCable* c = design_.cable(canvas_->selectedCable());
                const QString key = QString::fromLatin1(kShown[which]);
                if (filling_ || !c || c->hidden.contains(key) == !on) return;
                aboutToChange();
                if (on) c->hidden.removeAll(key);
                else c->hidden.append(key);
                commit();
            });
            return row;
        };
        form->addRow(QStringLiteral("Name"), withEye(cableName_, 0));
        form->addRow(QStringLiteral("P/N"), withEye(cablePn_, 1));
        form->addRow(QStringLiteral("Rev"), withEye(cableRev_, 2));
        form->addRow(QStringLiteral("Description"), withEye(cableDesc_, 3));
        form->addRow(QStringLiteral("Length"), cableLength_);
        form->addRow(QStringLiteral("Wires"), cableWires_);
        form->addRow(QStringLiteral("From - to"), cableEnds_);
        v->addLayout(form);
        v->addWidget(new QLabel(QStringLiteral("<b>Its drawing</b>"), page));
        cableDrawing_ = field(page, "systemCableDrawing", QStringLiteral("The drawing that defines this cable"));
        cableDrawing_->setReadOnly(true);
        v->addWidget(cableDrawing_);
        cableExpectedTitle_=field(page,"systemExpectedDrawingTitle",QStringLiteral("The title required for the linked drawing"));
        auto* expected=new QFormLayout;expected->addRow(QStringLiteral("Expected title"),cableExpectedTitle_);v->addLayout(expected);
        connect(cableExpectedTitle_,&QLineEdit::editingFinished,this,[this]{
            SysCable* c=design_.cable(canvas_->selectedCable());if(filling_||!c||c->expectedTitle==cableExpectedTitle_->text())return;
            aboutToChange();c->expectedTitle=cableExpectedTitle_->text().trimmed();commit();
        });
        auto* buttons = new QHBoxLayout;
        auto* link = new QPushButton(QStringLiteral("Link..."), page);
        link->setToolTip(QStringLiteral("Link a drawing file (DWX, DXF, DWG) to this cable"));
        auto* find = new QPushButton(QStringLiteral("Find..."), page);
        find->setToolTip(QStringLiteral("Find the drawing among the files - by its name or its drawing number"));
        auto* open = new QPushButton(QStringLiteral("Open"), page);
        auto* unlink = new QPushButton(QStringLiteral("Unlink"), page);
        for (QPushButton* b : {link, find, open, unlink}) buttons->addWidget(b);
        v->addLayout(buttons);
        connect(link, &QPushButton::clicked, this, [this] { linkDrawingFromFile(); });
        connect(find, &QPushButton::clicked, this, [this] { findDrawing(); });
        connect(open, &QPushButton::clicked, this, [this] { openDrawing(); });
        connect(unlink, &QPushButton::clicked, this, [this] { linkDrawing(canvas_->selectedCable(), QString()); });
        cableRead_ = new QLabel(page);
        cableRead_->setWordWrap(true);
        v->addWidget(cableRead_);
        cableStatus_ = new QLabel(page);
        cableStatus_->setObjectName(QStringLiteral("systemCableStatus"));
        cableStatus_->setWordWrap(true);
        v->addWidget(cableStatus_);
        v->addStretch(1);
        auto applyCable = [this] {
            SysCable* c = design_.cable(canvas_->selectedCable());
            if (filling_ || !c) return;
            if (c->name == cableName_->text() && c->pn == cablePn_->text() && c->rev == cableRev_->text() &&
                c->description == cableDesc_->text() && c->lengthMm == cableLength_->value() && c->wires == cableWires_->value())
                return;
            aboutToChange();
            c->name = cableName_->text();
            c->pn = cablePn_->text();
            c->rev = cableRev_->text();
            c->description = cableDesc_->text();
            c->lengthMm = cableLength_->value();
            c->wires = cableWires_->value();
            commit();
        };
        for (QLineEdit* e : {cableName_, cablePn_, cableRev_, cableDesc_}) connect(e, &QLineEdit::editingFinished, this, applyCable);
        connect(cableLength_, &QDoubleSpinBox::valueChanged, this, applyCable);
        connect(cableWires_, &QSpinBox::valueChanged, this, applyCable);
        panel_->addWidget(page);
    }
    // A NOTE.
    {
        auto* page = new QWidget(panel_);
        auto* v = new QVBoxLayout(page);
        v->addWidget(new QLabel(QStringLiteral("<b>Note</b> - its text"), page));
        noteText_ = new QPlainTextEdit(page);
        noteText_->setObjectName(QStringLiteral("systemNoteText"));
        noteText_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
        // THE TEXT ABOVE, ITS PROPERTIES BELOW, and a white line between them to drag: more room for either.
        auto* noteSplit = new QSplitter(Qt::Vertical, page);
        noteSplit->setObjectName(QStringLiteral("systemNoteSplit"));
        noteSplit->setChildrenCollapsible(false);
        noteSplit->setHandleWidth(5);
        noteSplit->setStyleSheet(QStringLiteral("QSplitter#systemNoteSplit::handle{background:#ffffff;margin:1px 0px;}"
                                                "QSplitter#systemNoteSplit::handle:hover{background:#9fd0ff;}"));
        noteSplit->addWidget(noteText_);
        auto* noteProperties = new QWidget(noteSplit);
        noteProperties->setObjectName(QStringLiteral("systemNoteProperties"));
        noteSplit->addWidget(noteProperties);
        noteSplit->setStretchFactor(0, 1);
        noteSplit->setStretchFactor(1, 0);
        v->addWidget(noteSplit, 1);
        auto* noteForm = new QFormLayout(noteProperties);
        noteForm->setContentsMargins(0, 6, 0, 0);
        noteWidth_ = new QDoubleSpinBox(page);
        noteHeight_ = new QDoubleSpinBox(page);
        noteWidth_->setObjectName(QStringLiteral("systemNoteWidth"));
        noteHeight_->setObjectName(QStringLiteral("systemNoteHeight"));
        noteWidth_->setRange(60, 10000); noteHeight_->setRange(30, 10000);
        for (auto* spin : {noteWidth_, noteHeight_}) { spin->setDecimals(0); spin->setSingleStep(10); }
        noteForm->addRow(QStringLiteral("Width"), noteWidth_);
        noteForm->addRow(QStringLiteral("Height"), noteHeight_);
        noteColour_ = new QToolButton(page);
        noteColour_->setObjectName(QStringLiteral("systemNoteColour"));
        noteColour_->setText(QStringLiteral("Choose colour..."));
        auto* noteLook = new QWidget(page);
        auto* noteLookRow = new QHBoxLayout(noteLook); noteLookRow->setContentsMargins(0, 0, 0, 0);
        noteLookRow->addWidget(noteColour_);
        auto* resetColour = new QToolButton(noteLook); resetColour->setText(QStringLiteral("Reset"));
        resetColour->setObjectName(QStringLiteral("systemNoteColourReset")); noteLookRow->addWidget(resetColour);
        connect(resetColour, &QToolButton::clicked, this, [this] {
            SysNote* n = design_.note(canvas_->selectedNote());
            if (!n || !n->color.isValid()) return;
            noteTyping_.clear(); aboutToChange(); n->color = QColor(); commit();
        });
        noteForm->addRow(QStringLiteral("Colour"), noteLook);
        noteLink_ = new QComboBox(page);
        noteLink_->setObjectName(QStringLiteral("systemNoteLink"));
        noteLink_->setToolTip(QStringLiteral("Attach a leader to a unit, cable or port; it follows that item when it moves."));
        // THE LINK LINE'S EYE, as the fields' own: the leader shown on the diagram, or hidden with the link kept.
        auto* linkRow = new QWidget(noteProperties);
        auto* linkLayout = new QHBoxLayout(linkRow); linkLayout->setContentsMargins(0, 0, 0, 0); linkLayout->setSpacing(2);
        linkLayout->addWidget(noteLink_, 1);
        noteLinkEye_ = new QToolButton(linkRow);
        noteLinkEye_->setObjectName(QStringLiteral("systemNoteLinkEye"));
        noteLinkEye_->setCheckable(true);
        noteLinkEye_->setChecked(true);
        noteLinkEye_->setAutoRaise(true);
        noteLinkEye_->setIcon(fieldEyeIcon());
        noteLinkEye_->setIconSize(QSize(18, 18));
        noteLinkEye_->setToolTip(QStringLiteral("The link line shown on the diagram - click to hide it (the note stays linked)"));
        linkLayout->addWidget(noteLinkEye_);
        noteForm->addRow(QStringLiteral("Link to"), linkRow);
        connect(noteLinkEye_, &QToolButton::toggled, this, [this](bool on) {
            SysNote* n = design_.note(canvas_->selectedNote());
            if (filling_ || !n || n->showLink == on) return;
            noteTyping_.clear(); aboutToChange(); n->showLink = on; commit();
        });
        auto applyNoteSize = [this] {
            SysNote* n = design_.note(canvas_->selectedNote());
            if (filling_ || !n || (n->w == noteWidth_->value() && n->h == noteHeight_->value())) return;
            noteTyping_.clear(); aboutToChange(); n->w = noteWidth_->value(); n->h = noteHeight_->value(); commit();
        };
        connect(noteWidth_, &QDoubleSpinBox::valueChanged, this, applyNoteSize);
        connect(noteHeight_, &QDoubleSpinBox::valueChanged, this, applyNoteSize);
        connect(noteColour_, &QToolButton::clicked, this, [this] {
            const SysNote* n = design_.note(canvas_->selectedNote());
            if (!n) return;
            const QString id = n->id;
            const QColor colour = QColorDialog::getColor(n->color.isValid() ? n->color : QColor(255, 247, 190), this, QStringLiteral("Note colour"));
            if (!colour.isValid()) return;
            SysNote* changedNote = design_.note(id);
            if (!changedNote) return;
            noteTyping_.clear(); aboutToChange(); changedNote->color = colour; commit();
        });
        connect(noteLink_, &QComboBox::activated, this, [this](int index) {
            SysNote* n = design_.note(canvas_->selectedNote());
            if (filling_ || !n) return;
            const QStringList ids = noteLink_->itemData(index).toStringList();
            noteTyping_.clear(); aboutToChange();
            n->unit = ids.value(0); n->port = ids.value(1); n->cable = ids.value(2); commit();
        });
        connect(noteText_, &QPlainTextEdit::textChanged, this, [this] {
            SysNote* n = design_.note(canvas_->selectedNote());
            if (filling_ || !n || n->text == noteText_->toPlainText()) return;
            // ONE STEP TO UNDO for what is typed into one note at a time.
            if (noteTyping_ != n->id) {
                aboutToChange();
                noteTyping_ = n->id;
            }
            n->text = noteText_->toPlainText();
            if (save_) save_(design_.toJson());
            canvas_->update();
            emit changed();
        });
        panel_->addWidget(page);
    }
    connect(canvas_, &SystemCanvas::deleteRequested, this, [this] { deleteSelected(); });
    connect(canvas_, &SystemCanvas::contextMenuRequested, this, [this](const QPoint& global, const QPointF& world) {
        QMenu* menu = contextMenu(world);
        menu->exec(global);
        menu->deleteLater();
    });
    connect(canvas_, &SystemCanvas::editRequested, this, [this](const QString& what) {
        if (what == QLatin1String("copy")) copySelection();
        else if (what == QLatin1String("cut")) cutSelection();
        else if (what == QLatin1String("paste")) paste();
    });
    connect(canvas_, &SystemCanvas::noteEditRequested, this, [this] {
        noteText_->setFocus();
        noteText_->selectAll();
    });
    split->addWidget(canvas_);
    split->addWidget(panel_);
    split->setStretchFactor(0, 4);
    split->setStretchFactor(1, 1);
    // UNDER IT: THE CABLE LIST, AND WHAT THE CHECK FOUND.
    tabs_ = new QTabWidget(outer);
    tabs_->setObjectName(QStringLiteral("systemTabs"));
    cables_ = new QTableWidget(0, 9, tabs_);
    cables_->setObjectName(QStringLiteral("systemCables"));
    cables_->setHorizontalHeaderLabels({QStringLiteral("Cable"), QStringLiteral("P/N"), QStringLiteral("Rev"), QStringLiteral("From"),
                                        QStringLiteral("To"), QStringLiteral("Length"), QStringLiteral("Drawing"), QStringLiteral("Drawing rev"),
                                        QStringLiteral("Status")});
    cables_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    cables_->setSelectionBehavior(QAbstractItemView::SelectRows);
    cables_->verticalHeader()->setVisible(false);
    cables_->horizontalHeader()->setStretchLastSection(true);
    connect(cables_, &QTableWidget::itemSelectionChanged, this, [this] {
        if (filling_) return;
        const QList<QTableWidgetItem*> sel = cables_->selectedItems();
        if (!sel.isEmpty()) canvas_->select(QString(), QString(), cables_->item(sel.first()->row(), 0)->data(Qt::UserRole).toString());
    });
    tabs_->addTab(cables_, QStringLiteral("Cables"));
    auto* checkPage = new QWidget(tabs_);
    auto* checkLayout = new QVBoxLayout(checkPage); checkLayout->setContentsMargins(4, 4, 4, 4);
    auto* checkHead = new QHBoxLayout;
    issueSummary_ = new QLabel(checkPage); issueSummary_->setObjectName(QStringLiteral("systemDrcSummary")); issueSummary_->setWordWrap(true); checkHead->addWidget(issueSummary_, 1);
    auto* checkAgain = new QPushButton(QStringLiteral("Check again"), checkPage);
    checkAgain->setObjectName(QStringLiteral("systemCheckAgain")); checkHead->addWidget(checkAgain);
    connect(checkAgain, &QPushButton::clicked, this, [this] { checkDesign(lastScope_); });
    auto* ruleButton = new QPushButton(QStringLiteral("Rules..."), checkPage);
    ruleButton->setObjectName(QStringLiteral("systemDrcRules")); checkHead->addWidget(ruleButton);
    connect(ruleButton, &QPushButton::clicked, this, [this] { askRules(); });
    auto* bypassButton = new QPushButton(QStringLiteral("Bypass table"), checkPage);
    bypassButton->setObjectName(QStringLiteral("systemDrcBypass")); checkHead->addWidget(bypassButton);
    connect(bypassButton, &QPushButton::clicked, this, [this] { showBypass(); });
    checkLayout->addLayout(checkHead);
    found_ = new QTableWidget(0, 4, checkPage);
    found_->setObjectName(QStringLiteral("systemIssues"));
    found_->setHorizontalHeaderLabels({QStringLiteral("Severity"), QStringLiteral("Rule"), QStringLiteral("Issue"), QString()});
    found_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    found_->setSelectionBehavior(QAbstractItemView::SelectRows);
    found_->verticalHeader()->hide(); found_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    found_->setWordWrap(true);
    found_->setToolTip(QStringLiteral("Click a finding to centre its unit, port, cable or note and flash its location."));
    // A TOPIC'S HEADING clicked: its findings folded away, or opened again. A finding: its part found on the diagram.
    connect(found_, &QTableWidget::cellClicked, this, [this](int row, int) {
        if (row < 0 || row >= static_cast<int>(rowIssue_.size())) return;
        if (rowIssue_[static_cast<size_t>(row)] >= 0) { locateIssue(rowIssue_[static_cast<size_t>(row)]); return; }
        const QString topic = found_->item(row, 0) ? found_->item(row, 0)->data(Qt::UserRole + 3).toString() : QString();
        if (!foldedTopics_.remove(topic)) foldedTopics_.insert(topic);
        fillIssues();
    });
    connect(found_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        if (row >= 0 && row < static_cast<int>(rowIssue_.size()) && rowIssue_[static_cast<size_t>(row)] >= 0) locateIssue(rowIssue_[static_cast<size_t>(row)]);
    });
    checkLayout->addWidget(found_, 1);
    tabs_->addTab(checkPage, QStringLiteral("Check"));
    connections_ = new QTableWidget(0, 6, tabs_);
    connections_->setObjectName(QStringLiteral("systemConnections"));
    connections_->setHorizontalHeaderLabels({QStringLiteral("Cable"), QStringLiteral("Line name"), QStringLiteral("From unit / port / contact"),
                                            QStringLiteral("To unit / port / contact"), QStringLiteral("Drawing"), QStringLiteral("Connection / issue")});
    connections_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connections_->setSelectionBehavior(QAbstractItemView::SelectRows);
    connections_->verticalHeader()->hide(); connections_->horizontalHeader()->setStretchLastSection(true);
    connect(connections_, &QTableWidget::cellClicked, this, [this](int row, int) {
        const QTableWidgetItem* it = connections_->item(row, 0);
        if (it) { SysIssue issue; issue.cable = it->data(Qt::UserRole).toString(); canvas_->locateIssue(issue); }
    });
    connect(connections_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        const QTableWidgetItem* it = connections_->item(row, 0);
        const SysCable* c = it ? design_.cable(it->data(Qt::UserRole).toString()) : nullptr;
        if (c && !c->drawing.isEmpty()) emit openRequested(c->drawing);
    });
    tabs_->addTab(connections_, QStringLiteral("Connections"));
    outer->addWidget(split);
    outer->addWidget(tabs_);
    outer->setStretchFactor(0, 4);
    outer->setStretchFactor(1, 1);
    lay->addWidget(outer);

    connect(canvas_, &SystemCanvas::aboutToChange, this, [this] { aboutToChange(); });
    connect(canvas_, &SystemCanvas::changed, this, [this] { commit(); });
    connect(canvas_, &SystemCanvas::selectionChanged, this, [this] { syncPanel(); });
    refresh();
}

void SystemDesignView::setStore(std::function<QJsonObject()> load, std::function<void(const QJsonObject&)> save) {
    load_ = std::move(load);
    save_ = std::move(save);
    reload();
}

void SystemDesignView::reload() {
    design_ = SystemDesign::fromJson(load_ ? load_() : QJsonObject());
    history_.clear();
    future_.clear();
    issues_.clear();
    connectionRows_.clear(); fillConnections();
    canvas_->setDesign(&design_);
    refresh();
    canvas_->fit();
}

void SystemDesignView::setTool(SysTool tool) { canvas_->setTool(tool); }

void SystemDesignView::aboutToChange() {
    noteTyping_.clear();
    history_.push_back(design_.toJson());
    if (history_.size() > 100) history_.erase(history_.begin());
    // Something new done: what Undo took back cannot be put back over it.
    future_.clear();
}

void SystemDesignView::commit() {
    if (save_) save_(design_.empty() ? QJsonObject() : design_.toJson());
    refresh();
    emit changed();
}

void SystemDesignView::redo() {
    if (future_.empty()) return;
    const QJsonObject again = future_.back();
    future_.pop_back();
    history_.push_back(design_.toJson());
    const QString unit = canvas_->selectedUnit(), cable = canvas_->selectedCable();
    noteTyping_.clear();
    design_ = SystemDesign::fromJson(again);
    canvas_->setDesign(&design_);
    canvas_->select(design_.unit(unit) ? unit : QString(), QString(), design_.cable(cable) ? cable : QString());
    if (save_) save_(design_.empty() ? QJsonObject() : design_.toJson());
    refresh();
    emit changed();
}

bool SystemDesignView::deleteSelected() {
    const QString unit = canvas_->selectedUnit(), port = canvas_->selectedPort();
    const bool any = canvas_->selectionCount() > 0;
    if (!any) return false;
    // CONNECTED: a unit with cables (its own, or those of units inside it) or units in it, or a port with cables - asked
    // first.
    QString question;
    if (canvas_->selectionCount() > 1) {
        bool connected = false;
        for (const QString& id : canvas_->selectedUnits()) {
            connected = connected || !design_.childrenOf(id).empty();
            for (const SysCable& c : design_.cables)
                connected = connected || c.aUnit == id || c.bUnit == id || design_.isWithin(c.aUnit, id) || design_.isWithin(c.bUnit, id);
        }
        if (connected) question = QStringLiteral("Delete %1 selected items with the units and cables connected to them?").arg(canvas_->selectionCount());
    }
    if (const SysUnit* u = design_.unit(unit)) {
        if (!port.isEmpty()) {
            const size_t n = design_.cablesAt(unit, port).size();
            const SysPort* p = u->port(port);
            if (n > 0)
                question = QStringLiteral("%1.%2 has %3 cable(s) plugged in - delete the port and its cable(s)?")
                               .arg(u->name, p ? p->name : port)
                               .arg(n);
        } else {
            size_t cables = 0, inside = 0;
            for (const SysCable& c : design_.cables)
                if (c.aUnit == unit || c.bUnit == unit || design_.isWithin(c.aUnit, unit) || design_.isWithin(c.bUnit, unit)) ++cables;
            for (const SysUnit& o : design_.units) inside += design_.isWithin(o.id, unit) ? 1 : 0;
            if (cables > 0 || inside > 0) {
                QStringList what;
                if (cables > 0) what << QStringLiteral("%1 cable(s)").arg(cables);
                if (inside > 0) what << QStringLiteral("%1 unit(s) inside it").arg(inside);
                question = QStringLiteral("%1 is connected - %2. Delete it with them?").arg(u->name, what.join(QStringLiteral(" and ")));
            }
        }
    }
    bool hasContents = false;
    for (const QString& id : canvas_->selectedUnits())
        hasContents = hasContents || !design_.childrenOf(id).empty();
    bool removeContents = true;
    if (hasContents && !confirm_) {
        const bool multiple = canvas_->selectedUnits().size() > 1;
        QMessageBox dialog(QMessageBox::Question, QStringLiteral("Delete parent unit"),
                           multiple ? QStringLiteral("What should be deleted with the selected parent units?")
                                    : QStringLiteral("What should be deleted with this parent unit?"),
                           QMessageBox::NoButton, this);
        dialog.setObjectName(QStringLiteral("systemDeleteUnitDialog"));
        dialog.setInformativeText(QStringLiteral(
            "Only selected unit keeps the units inside, their positions and their remaining cables. "
            "They move into the enclosing parent, or onto the main canvas.\n\n"
            "Unit and all inside also deletes every contained unit. "
            "In either case, cables attached directly to a deleted unit are removed."));
        auto* keep = dialog.addButton(multiple ? QStringLiteral("Only selected units")
                                              : QStringLiteral("Only selected unit"), QMessageBox::AcceptRole);
        keep->setObjectName(QStringLiteral("deleteUnitKeepContents"));
        auto* all = dialog.addButton(QStringLiteral("Unit and all inside"), QMessageBox::DestructiveRole);
        all->setObjectName(QStringLiteral("deleteUnitWithContents"));
        auto* cancel = dialog.addButton(QMessageBox::Cancel);
        cancel->setObjectName(QStringLiteral("cancelDeleteUnit"));
        dialog.setDefaultButton(keep);
        dialog.setEscapeButton(cancel);
        dialog.exec();
        if (dialog.clickedButton() != keep && dialog.clickedButton() != all) return false;
        removeContents = dialog.clickedButton() == all;
    } else if (!question.isEmpty()) {
        const bool yes = confirm_ ? confirm_(question)
                                  : QMessageBox::question(this, QStringLiteral("Delete"), question, QMessageBox::Yes | QMessageBox::No,
                                                          QMessageBox::No) == QMessageBox::Yes;
        if (!yes) return false;
    }
    canvas_->deleteSelected(removeContents);
    canvas_->noteDeleteDone();
    return true;
}

void SystemDesignView::setUnitColour(const QColor& colour) {
    SysUnit* u = design_.unit(canvas_->selectedUnit());
    if (!u || u->color == colour) return;
    aboutToChange();
    u->color = colour;
    commit();
}

void SystemDesignView::setUnitOpacity(double opacity) {
    SysUnit* u = design_.unit(canvas_->selectedUnit());
    opacity = std::clamp(opacity,0.0,1.0);
    if (!u || std::abs(u->opacity - opacity) < 1e-9) return;
    // ONE STEP TO UNDO for a slide of it.
    if (opacityDragging_ != u->id) {
        aboutToChange();
        opacityDragging_ = u->id;
    }
    u->opacity = opacity;
    if (save_) save_(design_.toJson());
    canvas_->update();
    emit changed();
}

void SystemDesignView::setNumbering(const QString& prefix, int digits) {
    if (prefix == design_.pnPrefix && digits == design_.pnDigits) return;
    aboutToChange();
    design_.pnPrefix = prefix;
    design_.pnDigits = std::clamp(digits, 1, 9);
    commit();
}

void SystemDesignView::askNumbering() {
    QDialog dlg(this);
    dlg.setObjectName(QStringLiteral("systemNumberingDialog"));
    dlg.setWindowTitle(QStringLiteral("Number P/Ns"));
    auto* form = new QFormLayout(&dlg);
    auto* prefix = new QLineEdit(design_.pnPrefix, &dlg);
    prefix->setObjectName(QStringLiteral("systemPnPrefix"));
    auto* digits = new QSpinBox(&dlg);
    digits->setObjectName(QStringLiteral("systemPnDigits"));
    digits->setRange(1, 9);
    digits->setValue(design_.pnDigits);
    auto* example = new QLabel(&dlg);
    auto show = [prefix, digits, example] {
        example->setText(QStringLiteral("The first: %1%2").arg(prefix->text()).arg(1, digits->value(), 10, QLatin1Char('0')));
    };
    connect(prefix, &QLineEdit::textChanged, &dlg, show);
    connect(digits, &QSpinBox::valueChanged, &dlg, show);
    show();
    form->addRow(QStringLiteral("Prefix"), prefix);
    form->addRow(QStringLiteral("Digits"), digits);
    form->addRow(example);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);
    if (dlg.exec() == QDialog::Accepted) setNumbering(prefix->text(), digits->value());
}

void SystemDesignView::undo() {
    if (history_.empty()) return;
    const QJsonObject back = history_.back();
    history_.pop_back();
    future_.push_back(design_.toJson());
    noteTyping_.clear();
    const QString unit = canvas_->selectedUnit(), cable = canvas_->selectedCable();
    design_ = SystemDesign::fromJson(back);
    canvas_->setDesign(&design_);
    canvas_->select(design_.unit(unit) ? unit : QString(), QString(), design_.cable(cable) ? cable : QString());
    if (save_) save_(design_.empty() ? QJsonObject() : design_.toJson());
    refresh();
    emit changed();
}

void SystemDesignView::refresh() {
    checked_ = false; checkedFindings_.clear();
    issues_.clear(); connectionRows_.clear(); fillConnections(); canvas_->clearCheck();
    syncPanel();
    fillCables();
    fillIssues();
    canvas_->update(); fillBypass();
}

QString SystemDesignView::status(const SysCable& c) const {
    switch (SystemDesign::linkState(c)) {
    case SysLinkState::None: return QStringLiteral("no drawing linked");
    case SysLinkState::Missing: return QStringLiteral("its drawing is not there");
    case SysLinkState::Mismatch:
        return QStringLiteral("rev %1 called for - the drawing is rev %2").arg(c.rev, c.drawingRev.isEmpty() ? QStringLiteral("(none)") : c.drawingRev);
    case SysLinkState::Match: break;
    }
    return QStringLiteral("ok - the drawing is rev %1").arg(c.drawingRev.isEmpty() ? QStringLiteral("(none)") : c.drawingRev);
}

void SystemDesignView::syncPanel() {
    filling_ = true;
    const SysUnit* u = design_.unit(canvas_->selectedUnit());
    const SysCable* c = design_.cable(canvas_->selectedCable());
    const SysNote* n = design_.note(canvas_->selectedNote());
    if (!n || n->id != noteTyping_) noteTyping_.clear();
    if (n) {
        panel_->setCurrentIndex(3);
        if (noteText_->toPlainText() != n->text) noteText_->setPlainText(n->text);
        noteWidth_->setValue(n->w); noteHeight_->setValue(n->h);
        noteColour_->setStyleSheet(QStringLiteral("background-color:%1;color:%2;").arg(n->color.isValid() ? n->color.name() : QStringLiteral("#fff7be"),
                                                 n->color.isValid() && n->color.lightness() < 120 ? QStringLiteral("white") : QStringLiteral("black")));
        noteLink_->clear();
        noteLink_->addItem(QStringLiteral("None"), QStringList{});
        const QStringList current{n->unit, n->port, n->cable};
        int selected = n->unit.isEmpty() && n->cable.isEmpty() ? 0 : -1;
        auto addTarget = [&](const QString& label, const QStringList& ids) {
            noteLink_->addItem(label, ids);
            if (ids == current) selected = noteLink_->count() - 1;
        };
        for (const SysUnit& target : design_.units) {
            addTarget(QStringLiteral("Unit: %1").arg(target.name), {target.id, {}, {}});
            for (const SysPort& p : target.ports) addTarget(QStringLiteral("Port: %1.%2").arg(target.name, p.name), {target.id, p.id, {}});
        }
        for (const SysCable& target : design_.cables) addTarget(QStringLiteral("Cable: %1").arg(target.name), {{}, {}, target.id});
        if (selected < 0) { addTarget(QStringLiteral("Missing linked item"), current); selected = noteLink_->count() - 1; }
        noteLink_->setCurrentIndex(selected);
        noteLinkEye_->setChecked(n->showLink);
        noteLinkEye_->setEnabled(!n->unit.isEmpty() || !n->cable.isEmpty());
    } else if (c) {
        panel_->setCurrentIndex(2);
        cableName_->setText(c->name);
        cablePn_->setText(c->pn);
        cableRev_->setText(c->rev);
        cableDesc_->setText(c->description);
        cableLength_->setValue(c->lengthMm);
        cableWires_->setValue(c->wires);
        for (int i = 0; i < 4; ++i)
            if (cableEyes_[i]) cableEyes_[i]->setChecked(c->shows(QString::fromLatin1(kShown[i])));
        const SysUnit* a = design_.unit(c->aUnit);
        const SysUnit* b = design_.unit(c->bUnit);
        const SysPort* pa = a ? a->port(c->aPort) : nullptr;
        const SysPort* pb = b ? b->port(c->bPort) : nullptr;
        cableEnds_->setText(QStringLiteral("%1.%2 - %3.%4").arg(a ? a->name : QString(), pa ? pa->name : QString(), b ? b->name : QString(),
                                                                pb ? pb->name : QString()));
        cableDrawing_->setText(c->drawing.isEmpty() ? QString() : QDir::toNativeSeparators(c->drawing));
        cableExpectedTitle_->setText(c->expectedTitle);
        cableRead_->setText(c->drawing.isEmpty() ? QString()
                                                 : QStringLiteral("Read off it: drawing No. %1, rev %2")
                                                       .arg(c->drawingNo.isEmpty() ? QStringLiteral("(none)") : c->drawingNo,
                                                            c->drawingRev.isEmpty() ? QStringLiteral("(none)") : c->drawingRev));
        const SysLinkState st = SystemDesign::linkState(*c);
        cableStatus_->setText(QStringLiteral("<b>%1</b>").arg(status(*c).toHtmlEscaped()));
        cableStatus_->setStyleSheet(st == SysLinkState::Match ? QStringLiteral("color:#2f8a4a;")
                                    : st == SysLinkState::None ? QStringLiteral("color:#b07a10;")
                                                               : QStringLiteral("color:#c0392b;"));
    } else if (u) {
        panel_->setCurrentIndex(1);
        unitName_->setText(u->name);
        unitPn_->setText(u->pn);
        unitRev_->setText(u->rev);
        unitDesc_->setText(u->description);
        unitKind_->setCurrentIndex(std::max(0,unitKind_->findData(u->kind)));
        for (int i = 0; i < 4; ++i)
            if (unitEyes_[i]) unitEyes_[i]->setChecked(u->shows(QString::fromLatin1(kShown[i])));
        unitOpacity_->setValue(static_cast<int>(std::lround(u->opacity * 100.0)));
        // ITS PICTURE in the panel, and what its eye and slider say.
        {
            QImage picture;
            if (!u->picture.isEmpty()) picture.loadFromData(u->picture);
            if (picture.isNull()) {
                unitPicturePreview_->setPixmap(QPixmap());
                unitPicturePreview_->setText(QStringLiteral("No picture - Insert picture..."));
            } else {
                unitPicturePreview_->setText(QString());
                unitPicturePreview_->setPixmap(QPixmap::fromImage(picture.scaled(220, 120, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
            }
            unitPictureEye_->setChecked(u->pictureShown);
            unitPictureEye_->setEnabled(!picture.isNull());
            unitPictureSize_->setValue(static_cast<int>(std::lround(u->pictureScale * 100.0)));
            unitPictureSize_->setEnabled(!picture.isNull());
        }
        if (opacityDragging_ != u->id) opacityDragging_.clear();
        ports_->setRowCount(static_cast<int>(u->ports.size()));
        for (int i = 0; i < static_cast<int>(u->ports.size()); ++i) {
            const SysPort& p = u->ports[static_cast<size_t>(i)];
            ports_->setItem(i, 0, new QTableWidgetItem(p.name));
            auto* side = new QComboBox(ports_);
            side->addItems({QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("top"), QStringLiteral("bottom")});
            side->setCurrentText(p.side);
            const QString unitId = u->id, portId = p.id;
            connect(side, &QComboBox::currentTextChanged, this, [this, unitId, portId](const QString& s) {
                SysUnit* uu = design_.unit(unitId);
                SysPort* pp = uu ? uu->port(portId) : nullptr;
                if (filling_ || !pp) return;
                aboutToChange();
                pp->side = s;
                commit();
            });
            ports_->setCellWidget(i, 1, side);
            ports_->setItem(i, 2, new QTableWidgetItem(p.connector));
            ports_->setItem(i, 3, new QTableWidgetItem(p.contactPn));
            ports_->setItem(i, 4, new QTableWidgetItem(p.description));
        }
    } else {
        panel_->setCurrentIndex(0);
        fillSheetPanel();
        nothing_->setText(canvas_->selectionCount() > 1 ? QStringLiteral("<b>%1 items selected</b><br><br>Drag a selected unit or note to move the selection. Delete removes the selected items.").arg(canvas_->selectionCount())
                            : QStringLiteral("<b>System design</b><br><br>Draw units, ports, cables and notes. Drag an empty area left to right for blue window selection, or right to left for green crossing selection."));
    }
    filling_ = false;
}

void SystemDesignView::fillCables() {
    filling_ = true;
    cables_->setRowCount(0);
    for (const SysCable& c : design_.cables) {
        const int row = cables_->rowCount();
        cables_->insertRow(row);
        const SysUnit* a = design_.unit(c.aUnit);
        const SysUnit* b = design_.unit(c.bUnit);
        const SysPort* pa = a ? a->port(c.aPort) : nullptr;
        const SysPort* pb = b ? b->port(c.bPort) : nullptr;
        auto* name = new QTableWidgetItem(c.name);
        name->setData(Qt::UserRole, c.id);
        cables_->setItem(row, 0, name);
        cables_->setItem(row, 1, new QTableWidgetItem(c.pn));
        cables_->setItem(row, 2, new QTableWidgetItem(c.rev));
        cables_->setItem(row, 3, new QTableWidgetItem(QStringLiteral("%1.%2").arg(a ? a->name : QString(), pa ? pa->name : QString())));
        cables_->setItem(row, 4, new QTableWidgetItem(QStringLiteral("%1.%2").arg(b ? b->name : QString(), pb ? pb->name : QString())));
        cables_->setItem(row, 5, new QTableWidgetItem(c.lengthMm > 0.0 ? QStringLiteral("%1 mm").arg(c.lengthMm, 0, 'f', 0) : QString()));
        cables_->setItem(row, 6, new QTableWidgetItem(c.drawing.isEmpty() ? QString() : QFileInfo(c.drawing).fileName()));
        cables_->setItem(row, 7, new QTableWidgetItem(c.drawingRev));
        auto* st = new QTableWidgetItem(status(c));
        const SysLinkState s = SystemDesign::linkState(c);
        st->setForeground(s == SysLinkState::Match ? QColor(47, 138, 74) : s == SysLinkState::None ? QColor(176, 122, 16) : QColor(192, 57, 43));
        cables_->setItem(row, 8, st);
        if (c.id == canvas_->selectedCable()) cables_->selectRow(row);
    }
    filling_ = false;
}

int SystemDesignView::rowOfIssue(int issue) const {
    for (size_t row = 0; row < rowIssue_.size(); ++row)
        if (rowIssue_[row] == issue) return static_cast<int>(row);
    return -1;
}

bool SystemDesignView::locateIssue(int index) {
    if (index < 0 || index >= static_cast<int>(issues_.size())) return false;
    if (const int row = rowOfIssue(index); row >= 0) { found_->selectRow(row); found_->scrollToItem(found_->item(row, 0)); }
    return canvas_->locateIssue(issues_[static_cast<size_t>(index)]);
}

namespace {
// THE TOPICS OF THE FINDINGS, in the order the check reviews them, and what each is called.
const char* const kTopics[][2] = {{"DESIGNATOR", "Designators"}, {"PORT-CONNECTION", "Port connections"}, {"PART-NUMBER", "Part numbers"},
                                  {"DRAWING-LINK", "Drawing links and revisions"}, {"DRAWING-FIELDS", "Drawing details fields"},
                                  {"ROUTING", "Cable routing clearance"}, {"NOTE-LINK", "Note links"}, {"SCHEMATIC-NAME", "Schematic line names"}};
int topicOrder(const QString& rule) {
    for (int i = 0; i < 8; ++i)
        if (rule == QLatin1String(kTopics[i][0])) return i;
    return 8;
}
QString topicTitle(const QString& rule) {
    const int i = topicOrder(rule);
    return i < 8 ? QString::fromLatin1(kTopics[i][1]) : rule;
}
}

void SystemDesignView::fillIssues() {
    int errors = 0, warnings = 0, notes = 0;
    for (const SysIssue& i : issues_) { if (i.level == 2) ++errors; else if (i.level == 1) ++warnings; else ++notes; }
    // NAMED BY THE CHECK RUN, and what it covered: a revision or connection check lists its own findings here too.
    const QString which = lastScope_ == CheckScope::Revisions ? QStringLiteral("Revision check (drawing links and revisions, drawing fields): ")
                        : lastScope_ == CheckScope::Connections ? QStringLiteral("Connection check (port connections, schematic line names): ")
                                                                : QStringLiteral("Design check: ");
    issueSummary_->setText(!checked_ ? QStringLiteral("Run Check design, Check revisions or Connections to list the findings.")
                           : issues_.empty() ? which + (design_.bypassed.empty() ? QStringLiteral("every rule checked passes.") : QStringLiteral("no active findings (%1 bypassed records).").arg(design_.bypassed.size()))
                           : which + QStringLiteral("%1 error(s), %2 warning(s), %3 note(s), %4 bypassed - click a finding to go to its part.").arg(errors).arg(warnings).arg(notes).arg(design_.bypassed.size()));
    // UNDER THEIR TOPICS: a heading for each rule with findings - how many, of which sort - then its findings, errors first.
    // A heading clicked folds its findings away.
    found_->clearSpans();
    found_->setRowCount(0);
    rowIssue_.clear();
    QString topic;
    for (int index = 0; index < static_cast<int>(issues_.size()); ++index) {
        const SysIssue& i = issues_[static_cast<size_t>(index)];
        if (i.rule != topic || rowIssue_.empty()) {
            topic = i.rule;
            int count = 0, bad = 0, warn = 0;
            for (const SysIssue& o : issues_)
                if (o.rule == topic) { ++count; bad += o.level == 2 ? 1 : 0; warn += o.level == 1 ? 1 : 0; }
            const int row = found_->rowCount(); found_->insertRow(row);
            const bool folded = foldedTopics_.contains(topic);
            QStringList sorts;
            if (bad) sorts << QStringLiteral("%1 error(s)").arg(bad);
            if (warn) sorts << QStringLiteral("%1 warning(s)").arg(warn);
            if (count - bad - warn) sorts << QStringLiteral("%1 note(s)").arg(count - bad - warn);
            auto* head = new QTableWidgetItem(QStringLiteral("%1  %2 - %3 finding(s): %4").arg(folded ? QStringLiteral("\u25B8") : QStringLiteral("\u25BE"),
                                                                                             topicTitle(topic)).arg(count).arg(sorts.join(QStringLiteral(", "))));
            QFont bold = head->font(); bold.setBold(true); head->setFont(bold);
            head->setForeground(bad ? QColor("#e05a5a") : warn ? QColor("#e6b84a") : found_->palette().color(QPalette::Text));
            head->setBackground(found_->palette().color(QPalette::AlternateBase));
            head->setData(Qt::UserRole + 3, topic);
            head->setToolTip(folded ? QStringLiteral("Click to show these findings") : QStringLiteral("Click to fold these findings away"));
            found_->setItem(row, 0, head);
            found_->setSpan(row, 0, 1, 4);
            rowIssue_.push_back(-1);
        }
        if (foldedTopics_.contains(topic)) continue;
        const int row = found_->rowCount(); found_->insertRow(row);
        const QStringList values{i.level == 2 ? QStringLiteral("Error") : i.level == 1 ? QStringLiteral("Warning") : QStringLiteral("Note"), i.rule, i.what};
        for (int col = 0; col < values.size(); ++col) {
            auto* it = new QTableWidgetItem(values[col]);
            it->setForeground(i.level == 2 ? QColor("#e05a5a") : i.level == 1 ? QColor("#e6b84a") : found_->palette().color(QPalette::Text));
            it->setData(Qt::UserRole, i.unit); it->setData(Qt::UserRole + 1, i.cable); it->setData(Qt::UserRole + 2, i.port);
            it->setToolTip(i.what);
            found_->setItem(row, col, it);
        }
        auto* accept = new QPushButton(QStringLiteral("Bypass"), found_);
        accept->setObjectName(QStringLiteral("systemBypassRow"));
        const QString key = i.key();
        connect(accept, &QPushButton::clicked, this, [this, key] {
            for (int k=0; k<static_cast<int>(issues_.size()); ++k) if (issues_[static_cast<size_t>(k)].key()==key) { bypassIssue(k); break; }
        });
        found_->setCellWidget(row, 3, accept);
        rowIssue_.push_back(index);
    }
    found_->setColumnWidth(3, 85);
    found_->resizeColumnToContents(0); found_->resizeColumnToContents(1); found_->resizeRowsToContents();
    tabs_->setTabText(1, issues_.empty() ? QStringLiteral("Check") : QStringLiteral("Check (%1)").arg(issues_.size()));
}

void SystemDesignView::fillConnections() {
    connections_->setRowCount(0);
    for (const SysConnection& r : connectionRows_) {
        const SysCable* c = design_.cable(r.cable);
        const int row = connections_->rowCount(); connections_->insertRow(row);
        const QStringList values{c ? c->name : r.cable, r.line, r.from, r.to, c ? QFileInfo(c->drawing).fileName() : QString(), r.status};
        for (int col = 0; col < values.size(); ++col) {
            auto* it = new QTableWidgetItem(values[col]); it->setData(Qt::UserRole, r.cable);
            it->setToolTip(col == 4 && c ? c->drawing : values[col]);
            if (col == 5) it->setForeground(r.level == 2 ? QColor("#e05a5a") : r.level == 1 ? QColor("#e6b84a") : QColor("#4cd07a"));
            connections_->setItem(row, col, it);
        }
    }
    connections_->resizeColumnsToContents();
}

int SystemDesignView::numberCables() {
    aboutToChange();
    const int n = design_.numberCables();
    if (n == 0) history_.pop_back();
    else commit();
    return n;
}

bool SystemDesignView::linkDrawing(const QString& cable, const QString& path, bool ask) {
    SysCable* c = design_.cable(cable);
    if (!c) return false;
    aboutToChange();
    c->drawing = path;
    c->drawingNo.clear();
    c->drawingRev.clear();c->drawingTitle.clear();
    if (!path.isEmpty()) {
        const DrawingInfo info = readDrawingInfo(path);
        c->drawingNo = info.drawingNo;
        c->drawingRev = info.revision; c->drawingTitle=info.title;
        if(c->expectedTitle.isEmpty())c->expectedTitle=info.title;
        // A cable with no part number or revision of its own takes the drawing's.
        if (c->pn.isEmpty()) c->pn = info.drawingNo;
        if (c->rev.isEmpty()) c->rev = info.revision;
        // THE DRAWING NAMES THEM OTHERWISE: the cable, or the connectors at its ends - asked whether to take its names.
        const std::vector<DesignatorChange> changes = design_.designatorChanges(cable, info);
        if (ask && !changes.empty()) {
            QStringList lines;
            for (const DesignatorChange& ch : changes)
                lines << QStringLiteral("  %1 %2 -> %3").arg(ch.cable ? QStringLiteral("cable") : QStringLiteral("connector"), ch.from, ch.to);
            const QString question = QStringLiteral("The drawing %1 names them otherwise:\n\n%2\n\nTake the drawing's designators on the diagram?")
                                         .arg(QFileInfo(path).fileName(), lines.join(QLatin1Char('\n')));
            const bool yes = confirm_ ? confirm_(question)
                                      : QMessageBox::question(this, QStringLiteral("Designators"), question, QMessageBox::Yes | QMessageBox::No,
                                                              QMessageBox::Yes) == QMessageBox::Yes;
            if (yes) design_.applyDesignatorChanges(cable, changes);
        }
    }
    commit();
    return true;
}

void SystemDesignView::linkDrawingFromFile() {
    if (!design_.cable(canvas_->selectedCable())) return;
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Link a drawing"), QString(),
                                                      QStringLiteral("Drawings (*.dwx *.dxf *.dwg);;All files (*)"));
    if (!path.isEmpty()) linkDrawing(canvas_->selectedCable(), path, true);
}

void SystemDesignView::findDrawing() {
    const SysCable* c = design_.cable(canvas_->selectedCable());
    if (!c) {
        // Nothing to link: the finder opens to look, and to open.
        DrawingFinderDialog dlg(this);
        connect(&dlg, &DrawingFinderDialog::openRequested, this, [this](const QString& path) { emit openRequested(path); });
        dlg.exec();
        return;
    }
    const QString path = DrawingFinderDialog::choose(this, c->pn);
    if (!path.isEmpty()) linkDrawing(c->id, path, true);
}

void SystemDesignView::openDrawing() {
    const SysCable* c = design_.cable(canvas_->selectedCable());
    if (c && !c->drawing.isEmpty()) emit openRequested(c->drawing);
}

int SystemDesignView::checkRevisions() {
    aboutToChange();
    design_.refreshDrawings();
    commit();
    return checkDesign(CheckScope::Revisions);
}

int SystemDesignView::checkDesign(CheckScope scope) {
    // EACH CHECK ITS OWN PART: the revisions the linked drawings and their fields, the connections the ports and the line
    // names through them - the rest left out; everything, for Check design.
    const QStringList revisions{QStringLiteral("DRAWING-LINK"), QStringLiteral("DRAWING-FIELDS")};
    const QStringList connections{QStringLiteral("PORT-CONNECTION"), QStringLiteral("SCHEMATIC-NAME")};
    auto inScope = [&](const QString& rule) {
        return scope == CheckScope::All || (scope == CheckScope::Revisions ? revisions.contains(rule) : connections.contains(rule));
    };
    lastScope_ = scope;
    issues_ = design_.check();
    issues_.erase(std::remove_if(issues_.begin(), issues_.end(), [&](const SysIssue& i) { return design_.ruleSeverity(i.rule)==0 || !inScope(i.rule); }), issues_.end());
    connectionRows_.clear();
    if(inScope(QStringLiteral("DRAWING-FIELDS"))&&design_.ruleSeverity(QStringLiteral("DRAWING-FIELDS"))>0)for(const SysCable& cable:design_.cables){
        if(cable.drawing.isEmpty())continue;const DrawingInfo info=readDrawingInfo(cable.drawing);
        for(const QString& message:info.fieldIssues)issues_.push_back({2,QStringLiteral("%1: %2").arg(cable.name,message),{},cable.id,QStringLiteral("DRAWING-FIELDS"),{}});
        if(info.found&&!cable.expectedTitle.isEmpty()&&info.title!=cable.expectedTitle)
            issues_.push_back({2,QStringLiteral("%1: drawing title '%2', expected '%3'").arg(cable.name,info.title,cable.expectedTitle),{},cable.id,QStringLiteral("DRAWING-FIELDS"),{}});
    }
    if (inScope(QStringLiteral("SCHEMATIC-NAME")) && design_.ruleSeverity(QStringLiteral("SCHEMATIC-NAME"))>0) {
        connectionRows_ = design_.checkConnections();
        for (const SysConnection& r : connectionRows_) if (r.level > 0)
            issues_.push_back({r.level, r.line.isEmpty() ? r.status : r.line + QStringLiteral(": ") + r.status, {}, r.cable, QStringLiteral("SCHEMATIC-NAME"), {}});
    }
    for (SysIssue& i : issues_) if (design_.ruleLevels.contains(i.rule)) i.level = design_.ruleSeverity(i.rule);
    checkedFindings_ = issues_;
    issues_.erase(std::remove_if(issues_.begin(), issues_.end(), [this](const SysIssue& i) {
        return std::any_of(design_.bypassed.begin(), design_.bypassed.end(), [&](const SysIssue& accepted) { return accepted.key()==i.key(); });
    }), issues_.end());
    checked_ = true;
    fillBypass();
    // BY TOPIC, in the order the rules are reviewed; within each, errors first.
    std::stable_sort(issues_.begin(), issues_.end(), [](const SysIssue& a, const SysIssue& b) {
        const int ta = topicOrder(a.rule), tb = topicOrder(b.rule);
        return ta != tb ? ta < tb : a.level > b.level;
    });
    fillConnections();
    canvas_->animateCheck(issues_, scope == CheckScope::All ? QStringList{} : scope == CheckScope::Revisions ? revisions : connections,
                          scope == CheckScope::All ? QString{} : scope == CheckScope::Revisions ? QStringLiteral("Revision check")
                                                                                                 : QStringLiteral("Connection check"));
    fillIssues();
    fillCables();
    syncPanel();
    tabs_->setCurrentIndex(1);
    if (auto* split = findChild<QSplitter*>(QStringLiteral("systemResultsSplit"))) {
        const int total = split->height();
        const int desired = std::min(std::max(220, 80 + std::min(8, static_cast<int>(issues_.size())) * 28), std::max(100, total/2));
        split->setSizes({std::max(200, total-desired), desired});
    }
    int errors = 0;
    for (const SysIssue& i : issues_) errors += i.level == 2 ? 1 : 0;
    return errors;
}

int SystemDesignView::checkSchematics() {
    // THE FINDINGS LISTED on the Check tab, as for any check; the full table of connections on the tab beside it.
    const int errors = checkDesign(CheckScope::Connections);
    tabs_->setTabText(2, connectionRows_.empty() ? QStringLiteral("Connections") : QStringLiteral("Connections (%1)").arg(connectionRows_.size()));
    return errors;
}

void SystemDesignView::askRules() {
    QDialog dialog(this); dialog.setWindowTitle(QStringLiteral("System design rules"));
    dialog.setMinimumWidth(620);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(18, 16, 18, 14);
    layout->setSpacing(10);
    dialog.setObjectName(QStringLiteral("systemDrcRulesDialog"));
    auto* explanation = new QLabel(QStringLiteral("Click a traffic light to cycle error, warning and not checked. Hover over a rule for its animated fault and fix. Changes apply immediately."), &dialog);
    explanation->setWordWrap(true); layout->addWidget(explanation);
    // THE LEGEND IN A BOX OF ITS OWN, a line under it, then the rules - each with what it checks written under its name.
    auto* legendBox = new QGroupBox(QStringLiteral("Legend"), &dialog);
    legendBox->setObjectName(QStringLiteral("systemDrcLegend"));
    auto* legendLayout = new QVBoxLayout(legendBox);
    legendLayout->setContentsMargins(10, 6, 10, 6);
    RuleExplainer::addSeverityLegend(legendLayout, legendBox);
    layout->addWidget(legendBox);
    RuleExplainer::addHelpToggle(layout, &dialog);
    auto* rule = new QFrame(&dialog);
    rule->setFrameShape(QFrame::HLine);
    rule->setFrameShadow(QFrame::Sunken);
    layout->addWidget(rule);
    auto* rulesBox = new QGroupBox(QStringLiteral("Rules"), &dialog);
    rulesBox->setObjectName(QStringLiteral("systemDrcRulesBox"));
    auto* rulesLayout = new QVBoxLayout(rulesBox);
    rulesLayout->setContentsMargins(10, 8, 10, 8);
    rulesLayout->setSpacing(8);
    layout->addWidget(rulesBox);
    struct Rule { const char* first; const char* second; const char* description; };
    const Rule rules[] = {
        {"DESIGNATOR", "Unique designators", "Give every unit and cable a unique name, and every port a unique name on its unit. Empty and duplicated designators make connections ambiguous."},
        {"PORT-CONNECTION", "Port connections", "Each port side takes one cable. Unused ports are reported; multiple cables on the same side of a connector are errors."},
        {"PART-NUMBER", "Part numbers", "Units, ports, contacts and cables need their part numbers. Cables sharing a part number must have matching definitions, revisions and drawings."},
        {"ROUTING","Cable routing clearance","Connected cable routes must avoid unit bodies. Ports move to a nearby clear face; routes remain attached while bends are moved."},
        {"DRAWING-FIELDS","Drawing details fields","Every P/N, revision and title field in a linked drawing must agree with Drawing details. The drawing title must match the project expectation."},
        {"DRAWING-LINK", "Drawing links and revisions", "Link each cable to its drawing. The file must be readable, and its drawing number and revision should match the cable definition."},
        {"NOTE-LINK", "Note links", "A note leader must point to an existing unit, port or cable. Relink the note when its referenced item has been removed."},
        {"SCHEMATIC-NAME", "Schematic line names", "Named electrical paths must join the expected ports. Every segment and shared unit/port/contact must retain the same line name across linked schematics."}
    };
    for (const auto& rule : rules) {
        auto* row = new QWidget(rulesBox); auto* rowLayout = new QHBoxLayout(row); rowLayout->setContentsMargins(0, 4, 0, 4);
        rowLayout->setSpacing(10);
        auto* box = new QToolButton(row); box->setFixedSize(18, 18); box->setCursor(Qt::PointingHandCursor);
        const QString id = QString::fromLatin1(rule.first);
        RuleExplainer::paintSeverity(box, design_.ruleSeverity(id, id==QLatin1String("NOTE-LINK") ? 1 : 2));
        connect(box, &QToolButton::clicked, this, [this, box, id] {
            const int level=box->property("severity").toInt(); const int next=level>=2 ? 1 : level==1 ? 0 : 2;
            setRuleSeverity(id,next); RuleExplainer::paintSeverity(box,next);
        });
        auto* title = new QLabel(QStringLiteral("<b>%1</b>").arg(QString::fromLatin1(rule.second)), row);
        title->setObjectName(QStringLiteral("systemDrcRuleName_") + QString::fromLatin1(rule.first));
        // THE ANIMATION OUTSIDE THE WINDOW, at its right: the rules stay in sight while it plays.
        for (QWidget* hovered : {static_cast<QWidget*>(row), static_cast<QWidget*>(title), static_cast<QWidget*>(box)})
            RuleExplainer::attachOutside(hovered, &dialog, QString::fromLatin1(rule.first), QString::fromLatin1(rule.second),
                                         QString::fromLatin1(rule.description));
        auto* words = new QWidget(row);
        auto* wordsLayout = new QVBoxLayout(words);
        wordsLayout->setContentsMargins(0, 0, 0, 0);
        wordsLayout->setSpacing(2);
        wordsLayout->addWidget(title);
        auto* what = new QLabel(QString::fromLatin1(rule.description), words);
        what->setWordWrap(true);
        what->setStyleSheet(QStringLiteral("color:#8a93a0;"));
        wordsLayout->addWidget(what);
        rowLayout->addWidget(box, 0, Qt::AlignTop); rowLayout->addWidget(words, 1);
        box->setObjectName(QStringLiteral("systemDrcRule_") + QString::fromLatin1(rule.first));
        rulesLayout->addWidget(row);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    layout->addWidget(buttons); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}

void SystemDesignView::setRuleSeverity(const QString& rule, int level) {
    level=std::clamp(level,0,2);
    if (design_.ruleLevels.contains(rule) && design_.ruleSeverity(rule)==level) return;
    aboutToChange(); design_.ruleLevels[rule]=level; design_.disabledRules.removeAll(rule);
    if (level==0) design_.disabledRules << rule;
    commit(); checkDesign();
}

bool SystemDesignView::bypassIssue(int row) {
    if (row<0 || row>=static_cast<int>(issues_.size())) return false;
    const SysIssue accepted=issues_[static_cast<size_t>(row)];
    for (const SysIssue& i : design_.bypassed) if (i.key()==accepted.key()) return false;
    aboutToChange(); design_.bypassed.push_back(accepted); commit(); checkDesign(); return true;
}

bool SystemDesignView::restoreIssue(const QString& key) {
    const auto found=std::find_if(design_.bypassed.begin(),design_.bypassed.end(),[&](const SysIssue& i){return i.key()==key;});
    if (found==design_.bypassed.end()) return false;
    aboutToChange(); design_.bypassed.erase(found); commit(); checkDesign(); return true;
}

void SystemDesignView::showBypass() {
    if (!bypassDialog_) {
        bypassDialog_=new QDialog(this); bypassDialog_->setObjectName(QStringLiteral("systemDrcBypassDialog"));
        bypassDialog_->setWindowTitle(QStringLiteral("Project DRC - bypass table")); bypassDialog_->resize(820,400);
        bypassDialog_->setAttribute(Qt::WA_DeleteOnClose);
        connect(bypassDialog_,&QObject::destroyed,this,[this]{bypassDialog_=nullptr;bypassTable_=nullptr;bypassSummary_=nullptr;});
        auto* layout=new QVBoxLayout(bypassDialog_);
        bypassSummary_=new QLabel(bypassDialog_); bypassSummary_->setWordWrap(true); layout->addWidget(bypassSummary_);
        bypassTable_=new QTableWidget(0,4,bypassDialog_); bypassTable_->setObjectName(QStringLiteral("systemDrcBypassTable"));
        bypassTable_->setHorizontalHeaderLabels({QStringLiteral("Rule"),QStringLiteral("Accepted finding"),QStringLiteral("Status"),QString()});
        bypassTable_->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch); bypassTable_->verticalHeader()->hide();
        bypassTable_->setSelectionBehavior(QAbstractItemView::SelectRows); bypassTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        connect(bypassTable_,&QTableWidget::cellClicked,this,[this](int row,int){
            if(row>=0 && row<static_cast<int>(design_.bypassed.size())) canvas_->locateIssue(design_.bypassed[static_cast<size_t>(row)]);
        });
        layout->addWidget(bypassTable_);
        auto* close=new QDialogButtonBox(QDialogButtonBox::Close,bypassDialog_); layout->addWidget(close);
        connect(close,&QDialogButtonBox::rejected,bypassDialog_,&QDialog::close);
    }
    fillBypass(); bypassDialog_->show(); bypassDialog_->raise();
}

void SystemDesignView::fillBypass() {
    if (!bypassTable_) return;
    bypassSummary_->setText(QStringLiteral("%1 bypassed finding(s). Click a row to locate it; Restore flags it again. Bypasses are saved with the project.").arg(design_.bypassed.size()));
    bypassTable_->setRowCount(0);
    for (const SysIssue& i : design_.bypassed) {
        const int row=bypassTable_->rowCount(); bypassTable_->insertRow(row);
        const bool present=std::any_of(checkedFindings_.begin(),checkedFindings_.end(),[&](const SysIssue& finding){return finding.key()==i.key();});
        bypassTable_->setItem(row,0,new QTableWidgetItem(i.rule)); bypassTable_->setItem(row,1,new QTableWidgetItem(i.what));
        bypassTable_->setItem(row,2,new QTableWidgetItem(design_.ruleSeverity(i.rule)==0 ? QStringLiteral("Rule not checked")
                                                   : !checked_ ? QStringLiteral("Not checked yet") : present ? QStringLiteral("Still present") : QStringLiteral("Not currently reported")));
        auto* restore=new QPushButton(QStringLiteral("Restore"),bypassTable_); restore->setObjectName(QStringLiteral("systemRestoreBypassRow"));
        const QString key=i.key(); connect(restore,&QPushButton::clicked,this,[this,key]{restoreIssue(key);});
        bypassTable_->setCellWidget(row,3,restore);
    }
    bypassTable_->resizeColumnToContents(0); bypassTable_->resizeColumnToContents(2); bypassTable_->resizeColumnToContents(3);
    bypassTable_->resizeRowsToContents();
}

void SystemDesignView::setCanvasColour(const QColor& colour) {
    if(!colour.isValid() || colour==design_.background) return;
    aboutToChange(); design_.background=colour; design_.background.setAlpha(255); commit();
}

void SystemDesignView::setDesignatorColour(const QColor& colour) {
    QColor set = colour;
    if (set.isValid()) set.setAlpha(255);
    if (set == design_.designatorColor || (!set.isValid() && !design_.designatorColor.isValid())) return;
    aboutToChange(); design_.designatorColor = set; commit();
}

void SystemDesignView::askDesignatorColour() {
    const QColor start = design_.designatorColor.isValid() ? design_.designatorColor : QColor(25, 30, 36);
    const QColor colour = QColorDialog::getColor(start, this, QStringLiteral("Designator colour"));
    if (colour.isValid()) setDesignatorColour(colour);
}

QStringList SystemDesignView::selectedIds(int what) const {
    QStringList ids;
    if (what == 0) {
        ids = canvas_->selectedUnits().values();
        if (!canvas_->selectedUnit().isEmpty() && canvas_->selectedPort().isEmpty() && !ids.contains(canvas_->selectedUnit()))
            ids << canvas_->selectedUnit();
    } else if (what == 1) {
        ids = canvas_->selectedCables().values();
        if (!canvas_->selectedCable().isEmpty() && !ids.contains(canvas_->selectedCable())) ids << canvas_->selectedCable();
    } else {
        ids = canvas_->selectedNotes().values();
        if (!canvas_->selectedNote().isEmpty() && !ids.contains(canvas_->selectedNote())) ids << canvas_->selectedNote();
    }
    return ids;
}

namespace {
const char kPieceMime[] = "application/x-edraw-system-piece";
}

bool SystemDesignView::copySelection() {
    const QJsonObject piece = design_.copyPiece(selectedIds(0), selectedIds(1), selectedIds(2));
    const SystemDesign check = SystemDesign::fromJson(piece);
    if (check.units.empty() && check.notes.empty()) return false;
    lastPiece_ = piece;
    auto* mime = new QMimeData;
    mime->setData(QString::fromLatin1(kPieceMime), QJsonDocument(piece).toJson(QJsonDocument::Compact));
    mime->setText(QStringLiteral("Edraw system design: %1 unit(s), %2 harness(es), %3 note(s)")
                      .arg(check.units.size()).arg(check.cables.size()).arg(check.notes.size()));
    QApplication::clipboard()->setMimeData(mime);
    return true;
}

bool SystemDesignView::cutSelection() {
    if (!copySelection()) return false;
    deleteSelected();
    return true;
}

bool SystemDesignView::canPaste() const {
    const QMimeData* mime = QApplication::clipboard()->mimeData();
    return (mime && mime->hasFormat(QString::fromLatin1(kPieceMime))) || !lastPiece_.isEmpty();
}

bool SystemDesignView::paste(const QPointF& at, bool atPoint) {
    QJsonObject piece = lastPiece_;
    if (const QMimeData* mime = QApplication::clipboard()->mimeData(); mime && mime->hasFormat(QString::fromLatin1(kPieceMime)))
        piece = QJsonDocument::fromJson(mime->data(QString::fromLatin1(kPieceMime))).object();
    const SystemDesign incoming = SystemDesign::fromJson(piece);
    if (incoming.units.empty() && incoming.notes.empty()) return false;
    // WHERE IT GOES: its top left corner at the place right-clicked, on the grid - or a little down and right.
    QRectF box;
    for (const SysUnit& u : incoming.units) box = box.united(u.rect());
    for (const SysNote& n : incoming.notes) box = box.united(QRectF(n.x, n.y, n.w, n.h));
    QPointF offset(40.0, 40.0);
    if (atPoint) offset = QPointF(std::round((at.x() - box.left()) / 10.0) * 10.0, std::round((at.y() - box.top()) / 10.0) * 10.0);
    aboutToChange();
    const QStringList added = design_.pastePiece(piece, offset);
    commit();
    for (const QString& id : added)
        if (design_.unit(id)) { canvas_->select(id, QString(), QString()); return true; }
    for (const QString& id : added)
        if (design_.note(id)) { canvas_->selectNote(id); break; }
    return !added.isEmpty();
}

int SystemDesignView::minimizeCrossings() {
    QStringList cables = selectedIds(1);
    // AT THE SELECTED UNITS: the harnesses plugged into them.
    if (cables.isEmpty())
        for (const QString& unit : selectedIds(0))
            for (const SysCable& c : design_.cables) {
                bool at = c.aUnit == unit || c.bUnit == unit;
                for (const SysBranch& b : c.branches) at = at || b.unit == unit;
                if (at && !cables.contains(c.id)) cables << c.id;
            }
    SystemDesign tried = design_;
    double shorter = 0.0;
    const int gone = minimizeSystemCrossings(tried, cables, &shorter);
    if (gone <= 0 && shorter <= 0.5) return 0;
    aboutToChange();
    design_ = tried;
    commit();
    return gone;
}

bool SystemDesignView::setUnitPicture(const QString& unit, const QString& given) {
    if (!design_.unit(unit)) return false;
    const QString path = given.isEmpty() ? QFileDialog::getOpenFileName(this, QStringLiteral("A picture for the unit"), QString(),
                                                                        QStringLiteral("Pictures (*.png *.jpg *.jpeg *.bmp *.gif *.svg)"))
                                         : given;
    if (path.isEmpty()) return false;
    QImage image(path);
    if (image.isNull()) return false;
    // NO MORE THAN 400 ACROSS: enough for a unit on the diagram, and the design file stays small.
    if (image.width() > 400 || image.height() > 400) image = image.scaled(400, 400, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    aboutToChange();
    design_.unit(unit)->picture = bytes;
    commit();
    return true;
}

void SystemDesignView::removeUnitPicture(const QString& unit) {
    SysUnit* u = design_.unit(unit);
    if (!u || u->picture.isEmpty()) return;
    aboutToChange();
    design_.unit(unit)->picture.clear();
    commit();
}

void SystemDesignView::setUnitPictureShown(const QString& unit, bool shown) {
    SysUnit* u = design_.unit(unit);
    if (!u || u->pictureShown == shown) return;
    aboutToChange();
    design_.unit(unit)->pictureShown = shown;
    commit();
}

void SystemDesignView::setUnitPictureScale(const QString& unit, double scale) {
    SysUnit* u = design_.unit(unit);
    scale = std::clamp(scale, 0.1, 1.0);
    if (!u || std::abs(u->pictureScale - scale) < 1e-9) return;
    // ONE STEP TO UNDO for a slide of it.
    if (opacityDragging_ != QStringLiteral("picture:") + unit) {
        aboutToChange();
        opacityDragging_ = QStringLiteral("picture:") + unit;
    }
    design_.unit(unit)->pictureScale = scale;
    if (save_) save_(design_.toJson());
    canvas_->update();
    emit changed();
}

bool SystemDesignView::saveUnitPicture(const QString& unit, const QString& given) {
    const SysUnit* u = design_.unit(unit);
    if (!u || u->picture.isEmpty()) return false;
    const QString path = given.isEmpty() ? QFileDialog::getSaveFileName(this, QStringLiteral("Save the unit's picture"), u->name + QStringLiteral(".png"),
                                                                        QStringLiteral("PNG pictures (*.png)"))
                                         : given;
    if (path.isEmpty()) return false;
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(u->picture) == u->picture.size();
}

void SystemDesignView::setShown(const QString& id, const QString& what, bool shown) {
    QStringList* hidden = nullptr;
    if (SysUnit* u = design_.unit(id)) hidden = &u->hidden;
    else if (SysCable* c = design_.cable(id)) hidden = &c->hidden;
    if (!hidden || hidden->contains(what) != shown) return;
    aboutToChange();
    hidden = design_.unit(id) ? &design_.unit(id)->hidden : &design_.cable(id)->hidden;
    if (shown) hidden->removeAll(what); else hidden->append(what);
    commit();
}

namespace {
// EVERYTHING DRAWN, in the diagram's units: units, notes, harnesses and the sheet.
QRectF drawnBox(const SystemDesign& d) {
    QRectF box;
    for (const SysUnit& u : d.units) box = box.united(u.rect());
    for (const SysNote& n : d.notes) box = box.united(QRectF(n.x, n.y, n.w, n.h));
    if (!d.sheet.empty()) box = box.united(d.sheet.rect());
    return box.isEmpty() ? QRectF(0, 0, 800, 560) : box;
}
}

bool SystemDesignView::exportPng(const QString& given) {
    const QString path = given.isEmpty() ? QFileDialog::getSaveFileName(this, QStringLiteral("The diagram as a picture"),
                                                                        (design_.name.isEmpty() ? QStringLiteral("system") : design_.name) + QStringLiteral(".png"),
                                                                        QStringLiteral("PNG pictures (*.png)"))
                                         : given;
    if (path.isEmpty()) return false;
    // TWO PIXELS TO THE DIAGRAM'S UNIT - no more than 8000 across - drawn by a canvas of its own: nothing selected on it.
    const QRectF box = drawnBox(design_).adjusted(-60, -60, 60, 60);
    const double k = std::min(2.0, 8000.0 / std::max(box.width(), box.height()));
    const QSize size(std::max(200, static_cast<int>(box.width() * k)), std::max(150, static_cast<int>(box.height() * k)));
    SystemCanvas shot;
    shot.setDesign(&design_);
    shot.setPlain(true);
    shot.resize(size);
    shot.fit();
    QImage image(size, QImage::Format_ARGB32);
    image.fill(design_.background);
    QPainter painter(&image);
    shot.render(&painter);
    painter.end();
    return image.save(path, "PNG");
}

bool SystemDesignView::exportPdf(const QString& given) {
    const QString path = given.isEmpty() ? QFileDialog::getSaveFileName(this, QStringLiteral("The diagram as a PDF"),
                                                                        (design_.name.isEmpty() ? QStringLiteral("system") : design_.name) + QStringLiteral(".pdf"),
                                                                        QStringLiteral("PDF (*.pdf)"))
                                         : given;
    if (path.isEmpty()) return false;
    // A PAGE THE SHEET'S SIZE, where it has one; A3 across, where not. Drawn as lines and text, not as a picture.
    QPdfWriter writer(path);
    writer.setTitle(design_.name.isEmpty() ? QStringLiteral("System design") : design_.name);
    writer.setCreator(QStringLiteral("Edraw"));
    if (!design_.sheet.empty())
        writer.setPageSize(QPageSize(QSizeF(design_.sheet.widthMm, design_.sheet.heightMm), QPageSize::Millimeter, QString(), QPageSize::ExactMatch));
    else
        writer.setPageSize(QPageSize(QSizeF(420.0, 297.0), QPageSize::Millimeter, QStringLiteral("A3"), QPageSize::ExactMatch));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    writer.setResolution(96);
    // ON A CLEAN WHITE PAGE, not the dark canvas: the designators on Auto take dark ink, a white line of the sheet black.
    SystemDesign paper = design_;
    paper.background = Qt::white;
    SystemCanvas shot;
    shot.setDesign(&paper);
    shot.setPlain(true);
    const bool whiteWas = EntityRenderer::whitePaper();
    EntityRenderer::setWhitePaper(true);
    shot.resize(writer.width(), writer.height());
    shot.fit();
    QPainter painter;
    if (!painter.begin(&writer)) { EntityRenderer::setWhitePaper(whiteWas); return false; }
    shot.render(&painter);
    painter.end();
    EntityRenderer::setWhitePaper(whiteWas);
    return QFileInfo(path).size() > 0;
}

QList<QPair<QString, QString>> SystemDesignView::templateChoices() const {
    QList<QPair<QString, QString>> choices;
    for (const QString& file : DrawingTemplate::installed()) choices.append({QFileInfo(file).completeBaseName(), file});
    for (const TemplateSheet& s : templateSheets())
        choices.append({QStringLiteral("%1 sheet (built in)").arg(s.name), QStringLiteral("builtin:") + s.name});
    return choices;
}

bool SystemDesignView::applyTemplate(const QString& given) {
    QString path = given;
    if (path.isEmpty()) {
        // THE TEMPLATES INSTALLED, THE PLAIN SHEETS BUILT IN, or one from a file.
        const QList<QPair<QString, QString>> choices = templateChoices();
        QStringList names;
        for (const auto& c : choices) names << c.first;
        const QString other = QStringLiteral("Another file...");
        names << other;
        const int start = std::max(0, static_cast<int>(names.indexOf(QStringLiteral("A3 sheet (built in)"))));
        bool ok = false;
        const QString chosen = QInputDialog::getItem(this, QStringLiteral("Template"), QStringLiteral("The sheet round the diagram:"), names,
                                                     DrawingTemplate::installed().isEmpty() ? start : 0, false, &ok);
        if (!ok || chosen.isEmpty()) return false;
        if (chosen == other) path = QFileDialog::getOpenFileName(this, QStringLiteral("A template"), QString(), DrawingTemplate::fileFilter());
        for (const auto& c : choices)
            if (c.first == chosen) path = c.second;
        if (path.isEmpty()) return false;
    }
    DrawingTemplate tpl;
    if (path.startsWith(QLatin1String("builtin:"))) {
        // A PLAIN SHEET: the frame, zones, revision table and title block Edraw draws for one of that size.
        const TemplateSheet* size = templateSheetByName(path.mid(8));
        if (!size) return false;
        tpl.name = size->name;
        tpl.sheet = *size;
    } else if (!tpl.load(path)) {
        if (!confirm_) QMessageBox::warning(this, QStringLiteral("Template"), QStringLiteral("%1 could not be read as an Edraw template.").arg(QFileInfo(path).fileName()));
        return false;
    }
    SysSheet sheet;
    sheet.name = tpl.name.isEmpty() ? QFileInfo(path).completeBaseName() : tpl.name;
    for (const Layer& layer : tpl.layers) sheet.layers.append(layer.toJson());
    sheet.content = tpl.hasContent() ? tpl.content : defaultSheetContent(tpl);
    sheet.widthMm = tpl.sheet.drawnWidthMm();
    sheet.heightMm = tpl.sheet.drawnHeightMm();
    // ROUND WHAT IS DRAWN: in the open middle of the sheet, left of its tables - the revisions at the top right, the title
    // block at the bottom right - taking three fifths of it each way at most, in steps of a quarter.
    QRectF box;
    for (const SysUnit& u : design_.units) box = box.united(u.rect());
    for (const SysNote& n : design_.notes) box = box.united(QRectF(n.x, n.y, n.w, n.h));
    if (box.isEmpty()) box = QRectF(0, 0, 600, 300);
    sheet.scale = std::max(0.5, std::ceil(std::max(box.width() / (sheet.widthMm * 0.6), box.height() / (sheet.heightMm * 0.6)) * 4.0) / 4.0);
    sheet.x = box.center().x() - sheet.widthMm * sheet.scale * 0.36;
    sheet.y = box.center().y() - sheet.heightMm * sheet.scale * 0.46;
    sheet.fields = design_.sheet.fields;
    if (!sheet.fields.contains(QStringLiteral("title")))
        sheet.fields[QStringLiteral("title")] = design_.name.isEmpty() ? QStringLiteral("System design") : design_.name;
    aboutToChange();
    design_.sheet = sheet;
    commit();
    canvas_->fit();
    return true;
}

QStringList SystemDesignView::sheetFieldKeys() const {
    QStringList keys;
    for (const QJsonValue v : design_.sheet.content) {
        const QString key = entityFieldKey(Entity::fromJson(v.toObject()));
        if (!key.isEmpty() && !keys.contains(key)) keys << key;
    }
    return keys;
}

void SystemDesignView::setSheetField(const QString& key, const QString& value) {
    if (design_.sheet.empty() || design_.sheet.fields.value(key).toString() == value) return;
    aboutToChange();
    design_.sheet.fields[key] = value;
    commit();
}

bool SystemDesignView::askSheetFields() {
    if (design_.sheet.empty()) return false;
    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Sheet fields"));
    dialog.setObjectName(QStringLiteral("systemSheetFields"));
    auto* form = new QFormLayout(&dialog);
    QHash<QString, QLineEdit*> edits;
    for (const QString& key : sheetFieldKeys()) {
        auto* edit = new QLineEdit(design_.sheet.fields.value(key).toString(), &dialog);
        edit->setObjectName(QStringLiteral("systemSheetField_") + key);
        form->addRow(key, edit);
        edits.insert(key, edit);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return false;
    QJsonObject fields = design_.sheet.fields;
    for (auto it = edits.begin(); it != edits.end(); ++it) fields[it.key()] = it.value()->text();
    if (fields == design_.sheet.fields) return false;
    aboutToChange();
    design_.sheet.fields = fields;
    commit();
    return true;
}

void SystemDesignView::fillSheetPanel() {
    if (!sheetName_ || !sheetFields_) return;
    auto* form = qobject_cast<QFormLayout*>(sheetFields_->layout());
    while (form && form->rowCount() > 0) form->removeRow(0);
    if (design_.sheet.empty()) {
        sheetName_->setText(QStringLiteral("No sheet round the diagram. Choose a template - one of yours, or a plain A4 to A0 sheet - "
                                           "for its frame and title block; it is in the PDF and the picture too."));
        return;
    }
    sheetName_->setText(QStringLiteral("<b>%1</b> - %2 x %3 mm").arg(design_.sheet.name).arg(design_.sheet.widthMm).arg(design_.sheet.heightMm));
    // ITS FIELDS, each as its title block has it, written in here.
    for (const QString& key : sheetFieldKeys()) {
        auto* edit = new QLineEdit(design_.sheet.fields.value(key).toString(), sheetFields_);
        edit->setObjectName(QStringLiteral("systemSheetField_") + key);
        connect(edit, &QLineEdit::editingFinished, this, [this, edit, key] { setSheetField(key, edit->text()); });
        form->addRow(key, edit);
    }
}

int SystemDesignView::autoAlign() {
    SystemDesign tried = design_;
    const int moved = autoAlignSystem(tried, selectedIds(0), selectedIds(1));
    if (moved <= 0) return 0;
    aboutToChange();
    design_ = tried;
    commit();
    return moved;
}

void SystemDesignView::removeSheet() {
    if (design_.sheet.empty()) return;
    aboutToChange();
    design_.sheet = SysSheet();
    commit();
}

QMenu* SystemDesignView::contextMenu(const QPointF& world) {
    auto* menu = new QMenu(this);
    menu->setObjectName(QStringLiteral("systemContextMenu"));
    const bool chosen = !selectedIds(0).isEmpty() || !selectedIds(1).isEmpty() || !selectedIds(2).isEmpty() ||
                        !canvas_->selectedPort().isEmpty();
    const bool copyable = !selectedIds(0).isEmpty() || !selectedIds(2).isEmpty();
    QAction* copy = menu->addAction(QStringLiteral("Copy"), this, [this] { copySelection(); });
    copy->setObjectName(QStringLiteral("systemMenuCopy"));
    copy->setShortcut(QKeySequence::Copy);
    copy->setEnabled(copyable);
    QAction* cut = menu->addAction(QStringLiteral("Cut"), this, [this] { cutSelection(); });
    cut->setObjectName(QStringLiteral("systemMenuCut"));
    cut->setShortcut(QKeySequence::Cut);
    cut->setEnabled(copyable);
    QAction* put = menu->addAction(QStringLiteral("Paste here"), this, [this, world] { paste(world, true); });
    put->setObjectName(QStringLiteral("systemMenuPaste"));
    put->setShortcut(QKeySequence::Paste);
    put->setEnabled(canPaste());
    QAction* remove = menu->addAction(QStringLiteral("Delete"), this, [this] { deleteSelected(); });
    remove->setObjectName(QStringLiteral("systemMenuDelete"));
    remove->setShortcut(QKeySequence::Delete);
    remove->setEnabled(chosen);
    menu->addSeparator();
    const bool someHarness = !selectedIds(1).isEmpty() || !selectedIds(0).isEmpty();
    QAction* untangle = menu->addAction(someHarness ? QStringLiteral("Minimize crossings of the selected harnesses")
                                                    : QStringLiteral("Minimize crossings"),
                                        this, [this] { minimizeCrossings(); });
    untangle->setObjectName(QStringLiteral("systemMenuMinimize"));
    untangle->setToolTip(QStringLiteral("Reorder the ports at the harnesses' ends along their sides so the harnesses cross less"));
    untangle->setEnabled(!design_.cables.empty());
    QAction* tidy = menu->addAction(someHarness ? QStringLiteral("Auto-align the selection") : QStringLiteral("Auto-align"), this, [this] { autoAlign(); });
    tidy->setObjectName(QStringLiteral("systemMenuAutoAlign"));
    tidy->setToolTip(QStringLiteral("Tidy small offsets: units on the grid, harnesses that are almost straight made straight, their bends lined up"));
    // A HARNESS SPREAD TO ONE MORE PORT: the next click on a port ends its branch there.
    if (const SysCable* c = design_.cable(canvas_->selectedCable()); c && selectedIds(1).size() == 1) {
        const QString id = c->id;
        QAction* spread = menu->addAction(QStringLiteral("Spread to another port (branch)..."), this, [this, id] { canvas_->startBranch(id); });
        spread->setObjectName(QStringLiteral("systemMenuBranch"));
        spread->setToolTip(QStringLiteral("Then click the port (or the unit) the harness goes on to"));
    }
    // SHOWN ON THE DIAGRAM OR NOT: what the unit or harness clicked shows of itself, a note's link line, a unit's picture.
    auto eyes = [&](const QString& id, const QStringList& hidden) {
        menu->addSeparator();
        menu->addSection(QStringLiteral("Show on the diagram"));
        const char* const what[4][2] = {{"name", "Name"}, {"pn", "P/N"}, {"rev", "Revision"}, {"desc", "Description"}};
        for (const auto& w : what) {
            const QString key = QString::fromLatin1(w[0]);
            QAction* eye = menu->addAction(QString::fromLatin1(w[1]));
            eye->setObjectName(QStringLiteral("systemMenuShow_") + key);
            eye->setCheckable(true);
            eye->setChecked(!hidden.contains(key));
            connect(eye, &QAction::toggled, this, [this, id, key](bool on) { setShown(id, key, on); });
        }
    };
    if (const SysUnit* u = design_.unit(canvas_->selectedUnit()); u && canvas_->selectedPort().isEmpty() && selectedIds(0).size() == 1)
        eyes(u->id, u->hidden);
    else if (const SysCable* c = design_.cable(canvas_->selectedCable()); c && selectedIds(1).size() == 1)
        eyes(c->id, c->hidden);
    else if (const SysNote* n = design_.note(canvas_->selectedNote()); n && (!n->unit.isEmpty() || !n->cable.isEmpty())) {
        menu->addSeparator();
        menu->addSection(QStringLiteral("Show on the diagram"));
        QAction* link = menu->addAction(QStringLiteral("Link line"));
        link->setObjectName(QStringLiteral("systemMenuShow_link"));
        link->setCheckable(true);
        link->setChecked(n->showLink);
        const QString id = n->id;
        connect(link, &QAction::toggled, this, [this, id](bool on) {
            SysNote* note = design_.note(id);
            if (!note || note->showLink == on) return;
            aboutToChange(); note->showLink = on; commit();
        });
    }
    if (const SysUnit* u = design_.unit(canvas_->selectedUnit()); u && canvas_->selectedPort().isEmpty()) {
        menu->addSeparator();
        const QString id = u->id;
        QAction* picture = menu->addAction(QStringLiteral("Insert picture..."), this, [this, id] { setUnitPicture(id); });
        picture->setObjectName(QStringLiteral("systemMenuPicture"));
        if (!u->picture.isEmpty()) {
            QAction* shown = menu->addAction(QStringLiteral("Show picture"));
            shown->setObjectName(QStringLiteral("systemMenuShow_picture"));
            shown->setCheckable(true);
            shown->setChecked(u->pictureShown);
            connect(shown, &QAction::toggled, this, [this, id](bool on) { setUnitPictureShown(id, on); });
            QAction* drop = menu->addAction(QStringLiteral("Remove picture"), this, [this, id] { removeUnitPicture(id); });
            drop->setObjectName(QStringLiteral("systemMenuPictureRemove"));
        }
    }
    return menu;
}

void SystemDesignView::askCanvasColour() {
    const QColor colour=QColorDialog::getColor(design_.background,this,QStringLiteral("Project canvas colour"));
    if(colour.isValid()) setCanvasColour(colour);
}

namespace {

// WHERE TO, asked when not given: the place last used for these files, kept.
QString askPath(QWidget* parent, const QString& given, bool save, const QString& title, const QString& name, const QString& filter) {
    if (!given.isEmpty()) return given;
    QSettings settings;
    const QString dir = settings.value(QStringLiteral("system/fileDir")).toString();
    const QString start = dir.isEmpty() ? name : QDir(dir).filePath(name);
    const QString path = save ? QFileDialog::getSaveFileName(parent, title, start, filter) : QFileDialog::getOpenFileName(parent, title, dir, filter);
    if (!path.isEmpty()) settings.setValue(QStringLiteral("system/fileDir"), QFileInfo(path).absolutePath());
    return path;
}

QString baseName(const SystemDesign& d) { return d.name.isEmpty() ? QStringLiteral("system") : d.name; }

} // namespace

bool SystemDesignView::saveDesignAs(const QString& given) {
    const QString path = askPath(this, given, true, QStringLiteral("Save the system design"), baseName(design_) + QStringLiteral(".wetsys"),
                                 QStringLiteral("Edraw system design (*.wetsys)"));
    if (path.isEmpty()) return false;
    QString error;
    if (!saveSystemFile(design_, path, &error)) {
        if (given.isEmpty()) QMessageBox::warning(this, QStringLiteral("System design"), error);
        return false;
    }
    return true;
}

bool SystemDesignView::openDesign(const QString& given) {
    const QString path = askPath(this, given, false, QStringLiteral("Open a system design"), QString(),
                                 QStringLiteral("Edraw system design (*.wetsys *.edsys)"));
    if (path.isEmpty()) return false;
    SystemDesign read;
    QString error;
    if (!loadSystemFile(path, read, &error)) {
        if (given.isEmpty()) QMessageBox::warning(this, QStringLiteral("System design"), error);
        return false;
    }
    aboutToChange();
    design_ = read;
    canvas_->setDesign(&design_);
    commit();
    canvas_->fit();
    return true;
}

bool SystemDesignView::importDesign(const QString& given) {
    const QString path = askPath(this, given, false, QStringLiteral("Import a diagram"), QString(),
                                 QStringLiteral("Diagrams (*.drawio *.dio *.xml *.vsdx);;draw.io (*.drawio *.dio *.xml);;Visio (*.vsdx)"));
    if (path.isEmpty()) return false;
    SystemDesign read;
    QString error;
    if (!importSystemDiagram(path, read, &error)) {
        if (given.isEmpty()) QMessageBox::warning(this, QStringLiteral("Import"), error);
        return false;
    }
    aboutToChange();
    design_ = read;
    canvas_->setDesign(&design_);
    commit();
    canvas_->fit();
    return true;
}

bool SystemDesignView::exportDrawio(const QString& given) {
    const QString path = askPath(this, given, true, QStringLiteral("Export the diagram for draw.io"), baseName(design_) + QStringLiteral(".drawio"),
                                 QStringLiteral("draw.io diagram (*.drawio)"));
    if (path.isEmpty()) return false;
    QString error;
    const bool ok = exportSystemDrawio(design_, path, &error);
    if (!ok && given.isEmpty()) QMessageBox::warning(this, QStringLiteral("System design"), error);
    return ok;
}

bool SystemDesignView::exportVisio(const QString& given) {
    const QString path = askPath(this, given, true, QStringLiteral("Export the diagram for Visio"), baseName(design_) + QStringLiteral(".vsdx"),
                                 QStringLiteral("Visio drawing (*.vsdx)"));
    if (path.isEmpty()) return false;
    QString error;
    const bool ok = exportSystemVsdx(design_, path, &error);
    if (!ok && given.isEmpty()) QMessageBox::warning(this, QStringLiteral("System design"), error);
    return ok;
}

bool SystemDesignView::exportCableList(const QString& given) {
    QString path = given;
    if (path.isEmpty())
        path = QFileDialog::getSaveFileName(this, QStringLiteral("The cable list"), QStringLiteral("cables.csv"), QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty()) return false;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    QTextStream(&f) << design_.cableListCsv();
    return true;
}

} // namespace Edraw_Desktop
