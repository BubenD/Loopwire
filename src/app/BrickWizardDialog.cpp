#include "app/BrickWizardDialog.h"

#include "app/DrawingPreview.h"
#include "app/Editor.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace Edraw_Desktop {

namespace {

double snapHalf(double v) { return std::round(v * 2.0) / 2.0; }

double segDistance(const QPointF& p, const QPointF& a, const QPointF& b) {
    const QPointF ab = b - a;
    const double len2 = ab.x() * ab.x() + ab.y() * ab.y();
    double t = len2 > 1e-12 ? ((p - a).x() * ab.x() + (p - a).y() * ab.y()) / len2 : 0.0;
    t = std::clamp(t, 0.0, 1.0);
    const QPointF q = a + ab * t;
    return std::hypot(p.x() - q.x(), p.y() - q.y());
}

QString brickTitle(const Brick& b) {
    if (!b.name.isEmpty()) return b.name;
    return b.type == BrickType::End ? partKindName(b.kind) : brickTypeName(b.type);
}

} // namespace

// ==================================================================================================
// The pane
// ==================================================================================================

BrickCanvas::BrickCanvas(BrickDesign* design, QWidget* parent) : QWidget(parent), d_(design) {
    setAcceptDrops(true);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(420, 220);
}

QPointF BrickCanvas::toScreen(double col, double row) const {
    return origin_ + QPointF(col * stepX_, row * stepY_);
}

void BrickCanvas::toGrid(const QPointF& at, double& col, double& row) const {
    col = (at.x() - origin_.x()) / stepX_;
    row = (at.y() - origin_.y()) / stepY_;
}

QRectF BrickCanvas::brickRect(const Brick& b) const {
    const QPointF c = toScreen(b.col, b.row);
    const QSizeF sz = b.type == BrickType::End ? QSizeF(96.0, 38.0) : QSizeF(80.0, 30.0);
    return QRectF(c.x() - sz.width() / 2.0, c.y() - sz.height() / 2.0, sz.width(), sz.height());
}

QPointF BrickCanvas::portOf(const Brick& b) const {
    const QRectF r = brickRect(b);
    return QPointF(r.right(), r.center().y());
}

QString BrickCanvas::brickAt(const QPointF& at) const {
    // The topmost - the last drawn.
    for (auto it = d_->bricks.rbegin(); it != d_->bricks.rend(); ++it)
        if (brickRect(*it).adjusted(-7, 0, 7, 0).contains(at)) return it->id;
    return QString();
}

void BrickCanvas::setSelected(const QString& id) {
    sel_ = id;
    selLink_ = {};
    update();
}

void BrickCanvas::fit() {
    if (d_->bricks.empty()) {
        origin_ = QPointF(70.0, 60.0);
        update();
        return;
    }
    double c0 = 1e9, c1 = -1e9, r0 = 1e9, r1 = -1e9;
    for (const Brick& b : d_->bricks) {
        c0 = std::min(c0, b.col);
        c1 = std::max(c1, b.col);
        r0 = std::min(r0, b.row);
        r1 = std::max(r1, b.row);
    }
    const QPointF mid((c0 + c1) / 2.0 * stepX_, (r0 + r1) / 2.0 * stepY_);
    origin_ = QPointF(width() / 2.0, height() / 2.0) - mid;
    update();
}

void BrickCanvas::deleteSelected() {
    if (!sel_.isEmpty()) {
        d_->removeBrick(sel_);
        sel_.clear();
        emit selectionChanged(QString());
        emit designChanged();
    } else if (!selLink_.first.isEmpty()) {
        d_->disconnect(selLink_.first, selLink_.second);
        selLink_ = {};
        emit designChanged();
    }
    update();
}

void BrickCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(0x23, 0x2a, 0x33));
    // The grid, as dots at every step.
    p.setPen(QPen(QColor(0x3c, 0x46, 0x52), 1.6));
    double c0, r0, c1, r1;
    toGrid(QPointF(0, 0), c0, r0);
    toGrid(QPointF(width(), height()), c1, r1);
    for (int c = int(std::floor(c0)); c <= int(std::ceil(c1)); ++c)
        for (int r = int(std::floor(r0)); r <= int(std::ceil(r1)); ++r) p.drawPoint(toScreen(c, r));

    if (d_->bricks.empty()) {
        p.setPen(QColor(0x8a, 0x93, 0xa0));
        p.drawText(rect(), Qt::AlignCenter,
                   QStringLiteral("Drag end parts and specials here from the list on the left.\n"
                                  "Link them by dragging from the dot on a brick's edge to another."));
    }

    // The links, under the bricks.
    for (const BrickLink& l : d_->links) {
        const Brick* a = d_->brick(l.a);
        const Brick* b = d_->brick(l.b);
        if (!a || !b) continue;
        const bool picked = (selLink_.first == l.a && selLink_.second == l.b) ||
                            (selLink_.first == l.b && selLink_.second == l.a);
        p.setPen(QPen(picked ? QColor(0x2b, 0xd0, 0xff) : QColor(0xc9, 0xcc, 0xd1), picked ? 3.0 : 2.0));
        p.drawLine(brickRect(*a).center(), brickRect(*b).center());
    }
    // A link being dragged out.
    if (drag_ == Drag::Link) {
        if (const Brick* a = d_->brick(dragId_)) {
            QPen pen(QColor(0x7e, 0xe0, 0xa0), 2.0);
            pen.setStyle(Qt::DashLine);
            p.setPen(pen);
            p.drawLine(portOf(*a), cursor_);
        }
    }

    // The bricks.
    for (const Brick& b : d_->bricks) {
        const QRectF r = brickRect(b);
        const bool end = b.type == BrickType::End;
        const bool picked = b.id == sel_;
        QColor fill = end ? QColor(0x2f, 0x4f, 0x74) : QColor(0x5a, 0x43, 0x78);
        if (end && b.libPart.isEmpty()) fill = QColor(0x3a, 0x44, 0x52);
        p.setPen(QPen(picked ? QColor(0x2b, 0xd0, 0xff) : QColor(0x9a, 0xa7, 0xb8), picked ? 2.4 : 1.2));
        p.setBrush(fill);
        if (end) p.drawRect(r);
        else p.drawRoundedRect(r, r.height() / 2.0, r.height() / 2.0);
        p.setPen(QColor(0xe8, 0xea, 0xee));
        QFont f = font();
        f.setPointSizeF(8.5);
        f.setBold(true);
        p.setFont(f);
        const QRectF top(r.left() + 4, r.top() + 2, r.width() - 8, r.height() / 2.0);
        p.drawText(top, Qt::AlignCenter, p.fontMetrics().elidedText(brickTitle(b), Qt::ElideRight,
                                                                    int(top.width())));
        f.setBold(false);
        f.setPointSizeF(7.5);
        p.setFont(f);
        QString sub;
        if (end) sub = b.libPart.isEmpty() ? QStringLiteral("no part yet") : b.libPart;
        else sub = QStringLiteral("%1 w · %2").arg(b.run.wires).arg(b.run.lengthMm, 0, 'f', 0);
        const QRectF bottom(r.left() + 4, r.center().y(), r.width() - 8, r.height() / 2.0 - 2);
        p.setPen(QColor(0xb8, 0xc0, 0xcc));
        p.drawText(bottom, Qt::AlignCenter,
                   p.fontMetrics().elidedText(sub, Qt::ElideRight, int(bottom.width())));
        // The port a link is dragged from.
        p.setPen(QPen(QColor(0x7e, 0xe0, 0xa0), 1.2));
        p.setBrush(QColor(0x23, 0x2a, 0x33));
        p.drawEllipse(portOf(b), 4.0, 4.0);
    }
}

void BrickCanvas::mousePressEvent(QMouseEvent* e) {
    setFocus(Qt::MouseFocusReason);
    pressAt_ = cursor_ = e->position();
    if (e->button() == Qt::MiddleButton) {
        drag_ = Drag::Pan;
        panFrom_ = origin_;
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    // A PORT UNDER THE POINTER WINS over any brick's body: it is the small target, aimed at on purpose.
    QString hit;
    for (const Brick& b : d_->bricks) {
        const QPointF port = portOf(b);
        if (std::hypot(e->position().x() - port.x(), e->position().y() - port.y()) <= 7.0) hit = b.id;
    }
    if (hit.isEmpty()) hit = brickAt(e->position());
    if (!hit.isEmpty()) {
        const Brick* b = d_->brick(hit);
        const QPointF port = portOf(*b);
        const bool onPort = std::hypot(e->position().x() - port.x(), e->position().y() - port.y()) <= 7.0;
        dragId_ = hit;
        drag_ = (onPort || (e->modifiers() & Qt::ControlModifier)) ? Drag::Link : Drag::Move;
        startCol_ = b->col;
        startRow_ = b->row;
        if (sel_ != hit) {
            setSelected(hit);
            emit selectionChanged(hit);
        }
        update();
        return;
    }
    // A link, then nothing.
    for (const BrickLink& l : d_->links) {
        const Brick* a = d_->brick(l.a);
        const Brick* b = d_->brick(l.b);
        if (!a || !b) continue;
        if (segDistance(e->position(), brickRect(*a).center(), brickRect(*b).center()) <= 5.0) {
            sel_.clear();
            selLink_ = {l.a, l.b};
            emit selectionChanged(QString());
            emit message(QStringLiteral("Link picked - Delete removes it"));
            update();
            return;
        }
    }
    const bool had = !sel_.isEmpty() || !selLink_.first.isEmpty();
    sel_.clear();
    selLink_ = {};
    drag_ = Drag::Pan;
    panFrom_ = origin_;
    if (had) emit selectionChanged(QString());
    update();
}

void BrickCanvas::mouseMoveEvent(QMouseEvent* e) {
    cursor_ = e->position();
    if (drag_ == Drag::Pan) {
        origin_ = panFrom_ + (e->position() - pressAt_);
        update();
    } else if (drag_ == Drag::Move) {
        if (Brick* b = d_->brick(dragId_)) {
            const double col = snapHalf(startCol_ + (e->position().x() - pressAt_.x()) / stepX_);
            const double row = snapHalf(startRow_ + (e->position().y() - pressAt_.y()) / stepY_);
            if (col != b->col || row != b->row) {
                b->col = col;
                b->row = row;
                emit designChanged();
            }
        }
        update();
    } else if (drag_ == Drag::Link) {
        update();
    }
}

void BrickCanvas::mouseReleaseEvent(QMouseEvent* e) {
    if (drag_ == Drag::Link) {
        const QString target = brickAt(e->position());
        if (!target.isEmpty() && target != dragId_) {
            QString why;
            if (d_->connect(dragId_, target, &why)) {
                emit message(QStringLiteral("Linked"));
                emit designChanged();
            } else {
                emit message(why);
            }
        }
    }
    drag_ = Drag::None;
    dragId_.clear();
    update();
}

void BrickCanvas::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
        deleteSelected();
        return;
    }
    Brick* b = d_->brick(sel_);
    if (b) {
        double dc = 0.0, dr = 0.0;
        if (e->key() == Qt::Key_Left) dc = -1.0;
        else if (e->key() == Qt::Key_Right) dc = 1.0;
        else if (e->key() == Qt::Key_Up) dr = -1.0;
        else if (e->key() == Qt::Key_Down) dr = 1.0;
        if (dc != 0.0 || dr != 0.0) {
            const double step = (e->modifiers() & Qt::ShiftModifier) ? 0.5 : 1.0;
            b->col += dc * step;
            b->row += dr * step;
            emit designChanged();
            update();
            return;
        }
    }
    QWidget::keyPressEvent(e);
}

void BrickCanvas::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasFormat(QString::fromLatin1(kBrickMime))) e->acceptProposedAction();
}

void BrickCanvas::dragMoveEvent(QDragMoveEvent* e) {
    if (e->mimeData()->hasFormat(QString::fromLatin1(kBrickMime))) e->acceptProposedAction();
}

void BrickCanvas::dropEvent(QDropEvent* e) {
    const QString what = QString::fromUtf8(e->mimeData()->data(QString::fromLatin1(kBrickMime)));
    const QStringList parts = what.split(QLatin1Char('|'));
    if (parts.size() != 2) return;
    double col, row;
    toGrid(e->position(), col, row);
    const QString id = d_->addBrick(brickTypeFromKey(parts[0]), partKindFromKey(parts[1]),
                                    std::round(col), snapHalf(row));
    e->acceptProposedAction();
    setSelected(id);
    emit selectionChanged(id);
    emit designChanged();
}

// ==================================================================================================
// The palette
// ==================================================================================================

BrickPalette::BrickPalette(QWidget* parent) : QListWidget(parent) {
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);
    setSelectionMode(QAbstractItemView::SingleSelection);
    auto header = [this](const QString& text) {
        auto* it = new QListWidgetItem(text, this);
        QFont f = it->font();
        f.setBold(true);
        it->setFont(f);
        it->setFlags(Qt::ItemIsEnabled);
    };
    auto brick = [this](const QString& text, const QString& data) {
        auto* it = new QListWidgetItem(QStringLiteral("   ") + text, this);
        it->setData(Qt::UserRole, data);
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
    };
    header(QStringLiteral("End parts"));
    for (PartKind k : brickEndKinds())
        brick(partKindName(k), brickTypeKey(BrickType::End) + QLatin1Char('|') + partKindKey(k));
    header(QStringLiteral("Specials"));
    for (BrickType t : {BrickType::Fanout, BrickType::Run, BrickType::TwistedPair,
                        BrickType::TwistedBundle})
        brick(brickTypeName(t), brickTypeKey(t) + QLatin1Char('|') + partKindKey(PartKind::Wire));
}

QStringList BrickPalette::mimeTypes() const { return {QString::fromLatin1(kBrickMime)}; }

QMimeData* BrickPalette::mimeData(const QList<QListWidgetItem*>& items) const {
    auto* m = new QMimeData;
    if (!items.isEmpty())
        m->setData(QString::fromLatin1(kBrickMime), items.front()->data(Qt::UserRole).toString().toUtf8());
    return m;
}

// ==================================================================================================
// The wizard
// ==================================================================================================

BrickWizardDialog::BrickWizardDialog(Editor* editor, QWidget* parent)
    : QDialog(parent), editor_(editor) {
    setWindowTitle(QStringLiteral("Mechanical harness wizard"));
    resize(1320, 820);
    catalog_.openRemembered();
    buildUi();
    loadPanel();
    refreshPreview();
}

void BrickWizardDialog::buildUi() {
    auto* root = new QVBoxLayout(this);
    auto* split = new QSplitter(Qt::Horizontal, this);
    root->addWidget(split, 1);

    // THE BRICKS TO CHOOSE FROM.
    auto* left = new QWidget(split);
    auto* ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->addWidget(new QLabel(QStringLiteral("Bricks — drag onto the pane"), left));
    palette_ = new BrickPalette(left);
    palette_->setObjectName(QStringLiteral("brickPalette"));
    ll->addWidget(palette_, 1);
    connect(palette_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        const QString what = it->data(Qt::UserRole).toString();
        if (!what.isEmpty()) addFromPalette(what);
    });

    // THE LOW-DETAIL PANE ABOVE THE PREVIEW.
    auto* middle = new QSplitter(Qt::Vertical, split);
    canvas_ = new BrickCanvas(&design_, middle);
    canvas_->setObjectName(QStringLiteral("brickCanvas"));
    preview_ = new DrawingPreview(QStringLiteral("Preview"), middle);
    preview_->setEmptyText(QStringLiteral("The drawing appears here as the bricks are linked"));
    middle->setStretchFactor(0, 1);
    middle->setStretchFactor(1, 1);
    connect(canvas_, &BrickCanvas::selectionChanged, this, [this](const QString&) {
        loadPanel();
        refreshPreview();
    });
    connect(canvas_, &BrickCanvas::designChanged, this, &BrickWizardDialog::designChanged);
    connect(canvas_, &BrickCanvas::message, this, [this](const QString& t) { status_->setText(t); });

    // WHAT THE PICKED BRICK IS.
    auto* right = new QWidget(split);
    auto* rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    panel_ = new QStackedWidget(right);
    rl->addWidget(panel_, 1);

    auto* none = new QLabel(QStringLiteral("Pick a brick to set what it is."), panel_);
    none->setWordWrap(true);
    none->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    panel_->addWidget(none);

    auto* endPage = new QWidget(panel_);
    auto* ef = new QFormLayout(endPage);
    endKind_ = new QLabel(endPage);
    ef->addRow(QStringLiteral("End part"), endKind_);
    part_ = new QComboBox(endPage);
    part_->setObjectName(QStringLiteral("brickPart"));
    ef->addRow(QStringLiteral("Library part"), part_);
    name_ = new QLineEdit(endPage);
    name_->setObjectName(QStringLiteral("brickName"));
    name_->setPlaceholderText(QStringLiteral("X1"));
    ef->addRow(QStringLiteral("Designator"), name_);
    balloon_ = new QCheckBox(QStringLiteral("Balloon"), endPage);
    balloon_->setObjectName(QStringLiteral("brickBalloon"));
    ef->addRow(QString(), balloon_);
    panel_->addWidget(endPage);
    connect(part_, &QComboBox::currentIndexChanged, this, [this](int) { writeEnd(); });
    connect(name_, &QLineEdit::textEdited, this, [this](const QString&) { writeEnd(); });
    connect(balloon_, &QCheckBox::toggled, this, [this](bool) { writeEnd(); });

    auto* runPage = new QWidget(panel_);
    auto* rf = new QFormLayout(runPage);
    runKind_ = new QLabel(runPage);
    rf->addRow(QStringLiteral("Special"), runKind_);
    auto spin = [runPage](const char* name, double lo, double hi, double step, const QString& suffix,
                          const QString& special = QString()) {
        auto* s = new QDoubleSpinBox(runPage);
        s->setObjectName(QLatin1String(name));
        s->setRange(lo, hi);
        s->setSingleStep(step);
        s->setDecimals(1);
        s->setSuffix(suffix);
        if (!special.isEmpty()) s->setSpecialValueText(special);
        return s;
    };
    wires_ = new QSpinBox(runPage);
    wires_->setObjectName(QStringLiteral("brickWires"));
    wires_->setRange(2, kTwistedMaxWires);
    rf->addRow(QStringLiteral("Wires"), wires_);
    length_ = spin("brickLength", 10.0, 100000.0, 10.0, QStringLiteral(" mm"));
    rf->addRow(QStringLiteral("Length"), length_);
    wireWidth_ = spin("brickWireWidth", 0.2, 20.0, 0.1, QStringLiteral(" mm"));
    rf->addRow(QStringLiteral("Wire width"), wireWidth_);
    fanStart_ = spin("brickFanStart", -1.0, 1000.0, 1.0, QStringLiteral(" mm"), QStringLiteral("Auto"));
    rf->addRow(QStringLiteral("Fan length, start"), fanStart_);
    fanFinish_ = spin("brickFanFinish", -1.0, 1000.0, 1.0, QStringLiteral(" mm"), QStringLiteral("Auto"));
    rf->addRow(QStringLiteral("Fan length, finish"), fanFinish_);
    knee_ = spin("brickKnee", -1.0, 1000.0, 1.0, QStringLiteral(" mm"), QStringLiteral("Auto"));
    rf->addRow(QStringLiteral("Bend radius, body"), knee_);
    fanKnee_ = spin("brickFanKnee", -1.0, 1000.0, 0.5, QStringLiteral(" mm"), QStringLiteral("Auto"));
    rf->addRow(QStringLiteral("Bend radius, fans"), fanKnee_);
    fanScale_ = spin("brickFanScale", 5.0, 100.0, 5.0, QStringLiteral(" %"));
    rf->addRow(QStringLiteral("Fan across the face"), fanScale_);
    body_ = new QComboBox(runPage);
    body_->setObjectName(QStringLiteral("brickBody"));
    body_->addItems({QStringLiteral("Wires"), QStringLiteral("Cable"), QStringLiteral("Heatshrink")});
    rf->addRow(QStringLiteral("Over the run"), body_);
    bodyScale_ = spin("brickBodyScale", 5.0, 100.0, 5.0, QStringLiteral(" %"));
    rf->addRow(QStringLiteral("Body across the face"), bodyScale_);
    bodyWidth_ = spin("brickBodyWidth", 0.0, 1000.0, 0.5, QStringLiteral(" mm"), QStringLiteral("From the face"));
    rf->addRow(QStringLiteral("Body width"), bodyWidth_);
    panel_->addWidget(runPage);
    for (QDoubleSpinBox* s : {length_, wireWidth_, fanStart_, fanFinish_, knee_, fanKnee_, fanScale_,
                              bodyScale_, bodyWidth_})
        connect(s, &QDoubleSpinBox::valueChanged, this, [this](double) { writeRun(); });
    connect(wires_, &QSpinBox::valueChanged, this, [this](int) { writeRun(); });
    connect(body_, &QComboBox::currentIndexChanged, this, [this](int) { writeRun(); });

    // MOVING THE PICKED BRICK, and the harness as a whole.
    auto* moves = new QHBoxLayout;
    auto moveBtn = [&](const char* name, const QString& text, double dc, double dr) {
        auto* b = new QPushButton(text, right);
        b->setObjectName(QLatin1String(name));
        b->setFixedWidth(34);
        connect(b, &QPushButton::clicked, this, [this, dc, dr] { moveSelected(dc, dr); });
        moves->addWidget(b);
    };
    moveBtn("brickLeft", QStringLiteral("←"), -1.0, 0.0);
    moveBtn("brickRight", QStringLiteral("→"), 1.0, 0.0);
    moveBtn("brickUp", QStringLiteral("↑"), 0.0, -1.0);
    moveBtn("brickDown", QStringLiteral("↓"), 0.0, 1.0);
    moves->addStretch(1);
    auto* del = new QPushButton(QStringLiteral("Delete"), right);
    del->setObjectName(QStringLiteral("brickDelete"));
    connect(del, &QPushButton::clicked, this, [this] { canvas_->deleteSelected(); });
    moves->addWidget(del);
    rl->addLayout(moves);

    split->addWidget(left);
    split->addWidget(middle);
    split->addWidget(right);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setStretchFactor(2, 0);
    split->setSizes({200, 820, 300});

    // THE BOTTOM ROW.
    auto* bottom = new QHBoxLayout;
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("brickStatus"));
    bottom->addWidget(status_, 1);
    auto* arrangeBtn = new QPushButton(QStringLiteral("Arrange"), this);
    arrangeBtn->setObjectName(QStringLiteral("brickArrange"));
    arrangeBtn->setToolTip(QStringLiteral(
        "Lay the end parts out symmetrically: the most connected on the axis, the rest either side"));
    connect(arrangeBtn, &QPushButton::clicked, this, [this] { arrange(); });
    bottom->addWidget(arrangeBtn);
    auto* placeBtn = new QPushButton(QStringLiteral("Place the drawing"), this);
    placeBtn->setObjectName(QStringLiteral("brickPlace"));
    connect(placeBtn, &QPushButton::clicked, this, [this] { place(); });
    bottom->addWidget(placeBtn);
    auto* close = new QPushButton(QStringLiteral("Close"), this);
    connect(close, &QPushButton::clicked, this, &QDialog::hide);
    bottom->addWidget(close);
    root->addLayout(bottom);
}

QString BrickWizardDialog::addFromPalette(const QString& what) {
    const QStringList parts = what.split(QLatin1Char('|'));
    if (parts.size() != 2) return QString();
    // The next free place: to the right of everything there is.
    double col = 0.0;
    for (const Brick& b : design_.bricks) col = std::max(col, b.col + 1.0);
    const QString id = design_.addBrick(brickTypeFromKey(parts[0]), partKindFromKey(parts[1]),
                                        design_.bricks.empty() ? 0.0 : col, 0.0);
    selectBrick(id);
    designChanged();
    return id;
}

void BrickWizardDialog::selectBrick(const QString& id) {
    canvas_->setSelected(id);
    loadPanel();
    refreshPreview();
}

void BrickWizardDialog::moveSelected(double dCol, double dRow) {
    Brick* b = design_.brick(canvas_->selected());
    if (!b) {
        status_->setText(QStringLiteral("Pick a brick to move"));
        return;
    }
    b->col += dCol;
    b->row += dRow;
    canvas_->update();
    designChanged();
}

void BrickWizardDialog::arrange() {
    arrangeBricks(design_);
    canvas_->fit();
    designChanged();
    status_->setText(QStringLiteral("Arranged symmetrically"));
}

std::vector<Entity> BrickWizardDialog::drawing() const {
    return buildBrickHarness(design_, &catalog_).entities;
}

void BrickWizardDialog::designChanged() {
    canvas_->update();
    loadPanel();
    refreshPreview();
}

void BrickWizardDialog::loadPanel() {
    const Brick* b = design_.brick(canvas_->selected());
    if (!b) {
        panel_->setCurrentIndex(0);
        return;
    }
    if (b->type == BrickType::End) {
        panel_->setCurrentIndex(1);
        const QSignalBlocker b1(part_), b2(name_), b3(balloon_);
        endKind_->setText(partKindName(b->kind));
        part_->clear();
        part_->addItem(QStringLiteral("(none yet)"), QString());
        for (const LibraryPart* p : catalog_.ofKind(b->kind))
            part_->addItem(p->title(), p->id);
        const int at = part_->findData(b->libPart);
        part_->setCurrentIndex(at >= 0 ? at : 0);
        if (name_->text() != b->name) name_->setText(b->name);
        balloon_->setChecked(b->balloon);
        return;
    }
    panel_->setCurrentIndex(2);
    const QSignalBlocker s1(wires_), s2(length_), s3(wireWidth_), s4(fanStart_), s5(fanFinish_),
        s6(knee_), s7(fanKnee_), s8(fanScale_), s9(body_), s10(bodyScale_), s11(bodyWidth_);
    const BrickRunSpec& r = b->run;
    runKind_->setText(brickTypeName(b->type));
    wires_->setValue(r.wires);
    wires_->setEnabled(b->type != BrickType::TwistedPair);
    length_->setValue(r.lengthMm);
    wireWidth_->setValue(r.wireWidthMm);
    fanStart_->setValue(r.fanStartMm);
    fanFinish_->setValue(r.fanFinishMm);
    knee_->setValue(r.kneeRadiusMm);
    fanKnee_->setValue(r.fanKneeMm);
    fanScale_->setValue(r.fanScalePct);
    body_->setCurrentIndex(r.body == RunBody::Cable ? 1 : r.body == RunBody::Heatshrink ? 2 : 0);
    body_->setEnabled(b->type == BrickType::Run);
    bodyScale_->setValue(r.bodyScalePct);
    bodyWidth_->setValue(r.bodyWidthMm);
}

void BrickWizardDialog::writeEnd() {
    Brick* b = design_.brick(canvas_->selected());
    if (!b || b->type != BrickType::End) return;
    b->libPart = part_->currentData().toString();
    b->name = name_->text();
    b->balloon = balloon_->isChecked();
    canvas_->update();
    refreshPreview();
}

void BrickWizardDialog::writeRun() {
    Brick* b = design_.brick(canvas_->selected());
    if (!b || !brickIsSpecial(b->type)) return;
    BrickRunSpec& r = b->run;
    r.wires = b->type == BrickType::TwistedPair ? 2 : wires_->value();
    r.lengthMm = length_->value();
    r.wireWidthMm = wireWidth_->value();
    r.fanStartMm = fanStart_->value();
    r.fanFinishMm = fanFinish_->value();
    r.kneeRadiusMm = knee_->value();
    r.fanKneeMm = fanKnee_->value();
    r.fanScalePct = fanScale_->value();
    r.body = body_->currentIndex() == 1   ? RunBody::Cable
             : body_->currentIndex() == 2 ? RunBody::Heatshrink
                                          : RunBody::Wires;
    r.bodyScalePct = bodyScale_->value();
    r.bodyWidthMm = bodyWidth_->value();
    canvas_->update();
    refreshPreview();
}

void BrickWizardDialog::refreshPreview() {
    std::vector<Entity> ents = drawing();
    const QString sel = canvas_->selected();
    for (Entity& e : ents)
        if (!sel.isEmpty() && e.meta.value(QStringLiteral("brickId")).toString() == sel)
            e.meta.insert(QLatin1String(kPreviewSelectedKey), true);
    preview_->setEntities(ents);
}

void BrickWizardDialog::place() {
    std::vector<Entity> ents = drawing();
    if (ents.empty() || !editor_) {
        status_->setText(QStringLiteral("Nothing to place yet"));
        return;
    }
    for (Entity& e : ents) e.meta.remove(QStringLiteral("brickId"));
    editor_->placeEntities(ents);
    status_->setText(QStringLiteral("%1 object(s) ready — click on the sheet to put them down")
                         .arg(ents.size()));
    // The window steps aside while the drawing is put down, and comes back once it is - or once the
    // placement is abandoned with Esc.
    hide();
    auto* placing = new QObject(this);
    const auto back = [this, placing] {
        if (editor_ && editor_->placingPaste()) return;
        placing->deleteLater();
        show();
        raise();
        activateWindow();
    };
    connect(editor_, &Editor::changed, placing, back);
    connect(editor_, &Editor::overlayChanged, placing, back);
}

} // namespace Edraw_Desktop
