#include "app/MainWindow.h"

#include "app/Editor.h"
#include "harness/Harness.h"
#include "io/Dwx.h"
#include "io/Dxf.h"
#include "render/CanvasWidget.h"

#include <QAction>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QMessageBox>
#include <QSpinBox>
#include <QActionGroup>
#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QStatusBar>
#include <QCloseEvent>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSignalBlocker>
#include <QTimer>
#include <QHash>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QPaintEvent>
#include <QCursor>
#include <QElapsedTimer>

#include <QtMath>
#include <algorithm>
#include <cmath>
#include <functional>

namespace Edraw_Desktop {

namespace {

// Draw a simple monochrome glyph for a tool so the ribbon reads like Edraw's
// icon grid without external assets.
QIcon makeToolIcon(ToolId id) {
    QPixmap pm(24, 24);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor("#cfd8e6"));
    pen.setWidthF(1.6);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    switch (id) {
    case ToolId::Select: {
        QPolygonF a({{6, 4}, {6, 18}, {10, 14}, {13, 20}, {15, 19}, {12, 13}, {17, 13}});
        p.setBrush(QColor("#cfd8e6"));
        p.drawPolygon(a);
        break;
    }
    case ToolId::Line: p.drawLine(5, 18, 19, 6); break;
    case ToolId::DoubleLine: p.drawLine(4, 16, 17, 5); p.drawLine(7, 19, 20, 8); break;
    case ToolId::Polyline: { QPolygonF a({{4, 16}, {9, 9}, {14, 14}, {20, 6}}); p.drawPolyline(a); break; }
    case ToolId::DoublePolyline: {
        QPolygonF a({{4, 14}, {9, 7}, {14, 12}, {20, 4}});
        QPolygonF b({{4, 19}, {9, 12}, {14, 17}, {20, 9}});
        p.drawPolyline(a); p.drawPolyline(b); break;
    }
    case ToolId::Rect: p.drawRect(5, 7, 14, 10); break;
    case ToolId::Polygon: {
        QPolygonF h; for (int i = 0; i < 6; ++i) { double an = M_PI / 6 + i * M_PI / 3; h << QPointF(12 + 8 * std::cos(an), 12 + 8 * std::sin(an)); } p.drawPolygon(h); break;
    }
    case ToolId::Circle: p.drawEllipse(QPointF(12, 12), 7, 7); break;
    case ToolId::Arc: { QPainterPath pa; pa.moveTo(5, 17); pa.arcTo(5, 5, 16, 16, 180, -120); p.drawPath(pa); break; }
    case ToolId::Ellipse: p.drawEllipse(QPointF(12, 12), 8, 5); break;
    case ToolId::Spline: { QPainterPath pa; pa.moveTo(4, 16); pa.cubicTo(8, 2, 16, 22, 20, 8); p.drawPath(pa); break; }
    case ToolId::Text: { QFont f = p.font(); f.setPixelSize(16); f.setBold(true); p.setFont(f); p.drawText(pm.rect(), Qt::AlignCenter, "A"); break; }
    case ToolId::Dimension: p.drawLine(5, 12, 19, 12); p.drawLine(5, 8, 5, 16); p.drawLine(19, 8, 19, 16); break;
    case ToolId::Move: p.drawLine(12, 4, 12, 20); p.drawLine(4, 12, 20, 12); p.drawLine(12, 4, 9, 7); p.drawLine(12, 4, 15, 7); p.drawLine(4, 12, 7, 9); p.drawLine(4, 12, 7, 15); break;
    case ToolId::Rotate: { QPainterPath pa; pa.moveTo(18, 8); pa.arcTo(5, 5, 14, 14, 40, 280); p.drawPath(pa); p.drawLine(18, 8, 15, 7); p.drawLine(18, 8, 19, 11); break; }
    case ToolId::Mirror: p.drawLine(12, 4, 12, 20); { QPolygonF a({{4, 8}, {9, 12}, {4, 16}}); QPolygonF b({{20, 8}, {15, 12}, {20, 16}}); p.drawPolygon(a); p.drawPolygon(b); } break;
    case ToolId::Scale: p.drawRect(5, 11, 8, 8); { QPainterPath pa; } p.drawLine(11, 13, 19, 5); p.drawLine(19, 5, 14, 5); p.drawLine(19, 5, 19, 10); break;
    case ToolId::Offset: p.drawRect(5, 8, 7, 10); p.drawRect(14, 6, 5, 14); break;
    case ToolId::Trim: p.drawLine(4, 18, 20, 6); p.drawLine(11, 7, 17, 17); break;
    case ToolId::Extend: p.drawLine(4, 16, 13, 9); p.drawLine(18, 5, 18, 19); p.drawLine(13, 9, 11, 8); p.drawLine(13, 9, 12, 11); break;
    case ToolId::Fillet: { QPainterPath pa; pa.moveTo(5, 19); pa.lineTo(5, 12); pa.arcTo(5, 5, 14, 14, 180, -90); pa.lineTo(19, 5); p.drawPath(pa); break; }
    case ToolId::Chamfer: { QPolygonF a({{5, 19}, {5, 10}, {10, 5}, {19, 5}}); p.drawPolyline(a); break; }
    case ToolId::Wire: { pen.setWidthF(2.4); p.setPen(pen); p.drawLine(4, 17, 20, 7); break; }
    case ToolId::Connector: p.drawRect(6, 5, 8, 14); p.drawLine(14, 9, 20, 9); p.drawLine(14, 15, 20, 15); break;
    case ToolId::Splice: p.setBrush(QColor("#cfd8e6")); p.drawEllipse(QPointF(12, 12), 4, 4); break;
    case ToolId::NotConnected: p.drawLine(7, 7, 17, 17); p.drawLine(17, 7, 7, 17); break;
    case ToolId::NetLabel: { QPolygonF a({{4, 8}, {15, 8}, {20, 12}, {15, 16}, {4, 16}}); p.drawPolygon(a); break; }
    }
    p.end();
    return QIcon(pm);
}

// Icons for the bottom-right drafting toggles (grid / grid-snap / polar / object
// snap), in the same flat line-art style as the tool icons.
QIcon makeStatusIcon(const QString& which) {
    QPixmap pm(22, 22);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor("#cfd8e6"));
    pen.setWidthF(1.4);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    if (which == QLatin1String("grid")) {
        // A 3x3 lattice of cells.
        for (int i = 0; i <= 3; ++i) {
            double t = 4 + i * 4.5;
            p.drawLine(QPointF(4, t), QPointF(17.5, t));
            p.drawLine(QPointF(t, 4), QPointF(t, 17.5));
        }
    } else if (which == QLatin1String("gridsnap")) {
        // Grid with a cursor snapped onto a node.
        QPen dim(QColor("#7d8aa0"));
        dim.setWidthF(1.1);
        p.setPen(dim);
        for (int i = 0; i <= 2; ++i) {
            double t = 4 + i * 5.5;
            p.drawLine(QPointF(4, t), QPointF(15, t));
            p.drawLine(QPointF(t, 4), QPointF(t, 15));
        }
        p.setPen(pen);
        // Cursor arrow landing on the centre node (9.5, 9.5).
        QPolygonF a({{11, 11}, {20, 13.5}, {15.5, 15.5}, {13.5, 20}});
        p.setBrush(QColor("#cfd8e6"));
        p.drawPolygon(a);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(9.5, 9.5), 1.6, 1.6);
    } else if (which == QLatin1String("polar")) {
        // Angle/protractor: a baseline + a ray with a sweep arc.
        p.drawLine(QPointF(4, 18), QPointF(18, 18));
        p.drawLine(QPointF(4, 18), QPointF(17, 7));
        QPainterPath pa;
        pa.arcMoveTo(QRectF(-2, 8, 20, 20), 0);
        pa.arcTo(QRectF(-2, 8, 20, 20), 0, 38);
        p.drawPath(pa);
    } else if (which == QLatin1String("osnap")) {
        // Object snap: a snap square marker over an intersection of two lines.
        QPen dim(QColor("#7d8aa0"));
        dim.setWidthF(1.1);
        p.setPen(dim);
        p.drawLine(QPointF(3, 16), QPointF(19, 6));
        p.drawLine(QPointF(4, 5), QPointF(18, 18));
        p.setPen(pen);
        p.drawRect(QRectF(7.5, 7.5, 7, 7)); // AutoCAD-style snap box
    } else if (which == QLatin1String("ortho")) {
        // Right-angle (L-shape).
        QPolygonF a({{6, 4}, {6, 18}, {19, 18}});
        p.drawPolyline(a);
    } else if (which == QLatin1String("angles")) {
        // Angle wedge.
        p.drawLine(QPointF(4, 18), QPointF(19, 18));
        p.drawLine(QPointF(4, 18), QPointF(18, 6));
    }
    p.end();
    return QIcon(pm);
}

// A 3x3 dot-matrix "grip" — the only place a panel can be grabbed to move/detach.
QPixmap makeGripPixmap() {
    constexpr int cols = 3, rows = 3, pitch = 4;
    QPixmap pm(cols * pitch, rows * pitch);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#8b97a8"));
    for (int c = 0; c < cols; ++c)
        for (int r = 0; r < rows; ++r)
            p.drawEllipse(QPointF(c * pitch + pitch / 2.0, r * pitch + pitch / 2.0),
                          1.0, 1.0);
    p.end();
    return pm;
}

// Push-pin icon: upright/filled when pinned, tilted/outline when auto-hide.
QIcon makePinIcon(bool pinned) {
    QPixmap pm(18, 18);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.translate(9, 9);
    if (!pinned) p.rotate(45); // tilted = "released"
    QPen pen(QColor("#cfd8e6"));
    pen.setWidthF(1.4);
    p.setPen(pen);
    p.setBrush(pinned ? QColor("#ffd166") : QColor(0, 0, 0, 0));
    // Pin head (rounded top) + needle pointing down.
    p.drawRoundedRect(QRectF(-4, -7, 8, 7), 2, 2);
    p.drawLine(QPointF(0, 0), QPointF(0, 7));
    p.end();
    return QIcon(pm);
}

// ---- AutoCAD-style preview icons for the PROPERTIES combos ----

// A color swatch (filled square) for the Color dropdown.
QIcon colorSwatchIcon(const QColor& c) {
    QPixmap pm(18, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setPen(QColor("#6b7280"));
    p.setBrush(c);
    p.drawRect(0, 0, 14, 13);
    p.end();
    return QIcon(pm);
}

// A multi-colour swatch for the "Select Color..." entry.
QIcon selectColorIcon() {
    QPixmap pm(18, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setPen(Qt::NoPen);
    const QColor quad[4] = {QColor("#ff0000"), QColor("#00ff00"),
                            QColor("#0000ff"), QColor("#ffff00")};
    p.setBrush(quad[0]); p.drawRect(0, 0, 7, 6);
    p.setBrush(quad[1]); p.drawRect(7, 0, 7, 6);
    p.setBrush(quad[2]); p.drawRect(0, 6, 7, 7);
    p.setBrush(quad[3]); p.drawRect(7, 6, 7, 7);
    p.setPen(QColor("#6b7280")); p.setBrush(Qt::NoBrush); p.drawRect(0, 0, 14, 13);
    p.end();
    return QIcon(pm);
}

// A horizontal sample of a linetype's dash pattern.
QIcon linetypeIcon(const QString& name) {
    QPixmap pm(72, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor("#dfe6f0"));
    pen.setWidthF(1.3);
    pen.setCapStyle(Qt::FlatCap);
    const QString n = name.toUpper();
    if (n == "CENTER") pen.setDashPattern({12, 3, 2, 3});
    else if (n == "CENTERX2") pen.setDashPattern({20, 4, 3, 4});
    else if (n == "DOT2" || n == "DOTTED") pen.setDashPattern({1, 3});
    else if (n == "HIDDEN" || n == "DASHED") pen.setDashPattern({5, 3});
    else if (n == "PHANTOM") pen.setDashPattern({14, 3, 3, 3, 3, 3});
    else if (n == "DASHDOT") pen.setDashPattern({8, 3, 1, 3});
    // Continuous / ByLayer / ByBlock -> solid line
    p.setPen(pen);
    p.drawLine(2, 7, 70, 7);
    p.end();
    return QIcon(pm);
}

// A horizontal line whose thickness reflects a lineweight (in mm).
QIcon lineweightIcon(double mm, bool isRef) {
    QPixmap pm(72, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    double w = isRef ? 1.0 : std::max(1.0, mm * 4.0);
    if (w > 9.0) w = 9.0;
    QPen pen(QColor("#dfe6f0"));
    pen.setWidthF(w);
    pen.setCapStyle(Qt::FlatCap);
    p.setPen(pen);
    p.drawLine(2, 7, 70, 7);
    p.end();
    return QIcon(pm);
}

// One entry of an icon combo: shown text, preview icon, and the canonical value
// passed to the apply callback.
struct ComboEntry {
    QString text;
    QIcon icon;
    QString data;
};

// A thin vertical strip shown when a panel is auto-hidden (collapsed): it shows
// the panel name rotated, and hovering it re-expands the panel.
class VerticalStrip : public QWidget {
public:
    VerticalStrip(const QString& title, QWidget* parent = nullptr)
        : QWidget(parent), title_(title) {
        setFixedWidth(24);
        setCursor(Qt::PointingHandCursor);
        setToolTip(QStringLiteral("Hover to expand"));
    }

    std::function<void()> onEnter; // invoked when the cursor touches the strip

protected:
    void enterEvent(QEnterEvent* e) override {
        if (onEnter) onEnter();
        QWidget::enterEvent(e);
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor("#222a36"));
        p.setPen(QColor("#cfd8e6"));
        // A few grip dots near the top.
        p.setBrush(QColor("#8b97a8"));
        p.setPen(Qt::NoPen);
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 2; ++c)
                p.drawEllipse(QPointF(8 + c * 5, 8 + r * 4), 1.0, 1.0);
        // Rotated title running up the strip.
        p.setPen(QColor("#cfd8e6"));
        p.save();
        p.translate(width() / 2.0 + 5, height() - 10);
        p.rotate(-90);
        QFont f = p.font();
        f.setPixelSize(11);
        f.setBold(true);
        p.setFont(f);
        p.drawText(0, 0, title_);
        p.restore();
    }

private:
    QString title_;
};

// A dock that can auto-hide: when "pinned" it stays open; when unpinned it
// collapses to a VerticalStrip whenever the cursor isn't over it, expanding on
// hover and re-collapsing 2 s after the cursor leaves.
class AutoHideDock : public QDockWidget {
public:
    AutoHideDock(const QString& title, QMainWindow* mw)
        : QDockWidget(title, mw), mw_(mw), title_(title) {
        poll_ = new QTimer(this);
        poll_->setInterval(200);
        QObject::connect(poll_, &QTimer::timeout, this, [this] { onPoll(); });
    }

    // Provide the custom title bar + the real content widget.
    void setManaged(QWidget* titleBar, QWidget* content) {
        titleBar_ = titleBar;
        content_ = content;
        strip_ = new VerticalStrip(title_, this);
        strip_->onEnter = [this] { if (!pinned_ && collapsed_) expand(); };
        strip_->hide();
        emptyBar_ = new QWidget(this);
        emptyBar_->setFixedHeight(0);
        setTitleBarWidget(titleBar_);
        setWidget(content_);
    }

    bool pinned() const { return pinned_; }
    void setPinned(bool v) {
        pinned_ = v;
        if (pinned_) {
            poll_->stop();
            expand();
        } else {
            poll_->start(); // onPoll collapses once the cursor is away
        }
    }

private:
    bool horizontalArea() {
        const Qt::DockWidgetArea a = mw_->dockWidgetArea(this);
        return a != Qt::TopDockWidgetArea && a != Qt::BottomDockWidgetArea;
    }

    void onPoll() {
        if (pinned_ || isFloating()) return;
        const QRect g(mapToGlobal(QPoint(0, 0)), size());
        if (g.contains(QCursor::pos())) {
            outside_.invalidate();
            if (collapsed_) expand();
        } else if (!collapsed_) {
            if (!outside_.isValid())
                outside_.start();
            else if (outside_.elapsed() >= 2000)
                collapse();
        }
    }

    void expand() {
        if (!collapsed_) return;
        collapsed_ = false;
        setTitleBarWidget(titleBar_);
        setWidget(content_);
        content_->show();
        setMinimumSize(0, 0);
        setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        if (expandedExtent_ > 0)
            mw_->resizeDocks({this}, {expandedExtent_},
                             horizontalExtent_ ? Qt::Horizontal : Qt::Vertical);
        outside_.invalidate();
    }

    void collapse() {
        if (collapsed_ || isFloating()) return;
        horizontalExtent_ = horizontalArea();
        expandedExtent_ = horizontalExtent_ ? width() : height();
        collapsed_ = true;
        setTitleBarWidget(emptyBar_);
        setWidget(strip_);
        strip_->show();
        if (horizontalExtent_)
            setFixedWidth(24);
        else
            setFixedHeight(24);
    }

    QMainWindow* mw_;
    QString title_;
    QWidget* titleBar_ = nullptr;
    QWidget* content_ = nullptr;
    VerticalStrip* strip_ = nullptr;
    QWidget* emptyBar_ = nullptr;
    QTimer* poll_ = nullptr;
    QElapsedTimer outside_;
    bool pinned_ = true;
    bool collapsed_ = false;
    bool horizontalExtent_ = true;
    int expandedExtent_ = 0;
};

// Custom panel title bar: a dots grip (the only drag handle), the title, a
// pin/auto-hide toggle, and a close button.
class DockTitleBar : public QWidget {
public:
    DockTitleBar(AutoHideDock* dock, const QString& title)
        : QWidget(dock), dock_(dock) {
        setStyleSheet(QStringLiteral("background:#222a36;"));
        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(5, 2, 5, 2);
        lay->setSpacing(6);

        grip_ = new QLabel(this);
        grip_->setPixmap(makeGripPixmap());
        grip_->setCursor(Qt::SizeAllCursor);
        grip_->setToolTip(QStringLiteral("Drag to move / detach"));
        lay->addWidget(grip_);

        auto* lbl = new QLabel(title, this);
        lbl->setStyleSheet(QStringLiteral("font-weight:600;color:#cfd8e6;"));
        lay->addWidget(lbl, 1);

        auto* pin = new QToolButton(this);
        pin->setCheckable(true);
        pin->setChecked(true);
        pin->setAutoRaise(true);
        pin->setFocusPolicy(Qt::NoFocus);
        pin->setIcon(makePinIcon(true));
        pin->setToolTip(QStringLiteral("Pinned — click to auto-hide"));
        QObject::connect(pin, &QToolButton::toggled, dock, [pin, dock](bool on) {
            pin->setIcon(makePinIcon(on));
            pin->setToolTip(on ? QStringLiteral("Pinned — click to auto-hide")
                               : QStringLiteral("Auto-hide — click to pin"));
            dock->setPinned(on);
        });
        lay->addWidget(pin);

        auto* closeBtn = new QToolButton(this);
        closeBtn->setText(QString(QChar(0x2715))); // ✕
        closeBtn->setAutoRaise(true);
        closeBtn->setFocusPolicy(Qt::NoFocus);
        closeBtn->setToolTip(QStringLiteral("Close"));
        QObject::connect(closeBtn, &QToolButton::clicked, dock, &QDockWidget::close);
        lay->addWidget(closeBtn);
    }

protected:
    // Only the grip starts the (native, snapping) dock move; presses there are
    // ignored so they bubble up to the QDockWidget, while the rest of the bar
    // consumes presses so the panel can't be dragged from anywhere else.
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && grip_->geometry().contains(e->pos())) {
            e->ignore();
            return;
        }
        e->accept();
    }
    void mouseMoveEvent(QMouseEvent* e) override { e->accept(); }
    void mouseDoubleClickEvent(QMouseEvent* e) override {
        if (grip_->geometry().contains(e->pos())) {
            e->ignore();
            return;
        }
        e->accept(); // block double-click-to-float off the title text
    }

private:
    AutoHideDock* dock_;
    QLabel* grip_;
};

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(QStringLiteral("Edraw"));
    resize(1340, 860);

    canvas_ = new CanvasWidget(this);
    editor_ = new Editor(this);
    canvas_->setEditor(editor_);
    buildDocumentArea(); // tab bar + canvas as the central widget

    coordLabel_ = new QLabel(QStringLiteral("0.000, 0.000"), this);
    statusBar()->addPermanentWidget(coordLabel_);
    statusBar()->showMessage(QStringLiteral("Ready"));

    connect(canvas_, &CanvasWidget::cursorWorldMoved, this, [this](Vec2 w) {
        coordLabel_->setText(QStringLiteral("%1, %2").arg(w.x, 0, 'f', 3).arg(w.y, 0, 'f', 3));
    });
    connect(editor_, &Editor::message, this, [this](const QString& m) { statusBar()->showMessage(m); });
    connect(editor_, &Editor::changed, this, [this] {
        canvas_->update();
        refreshPanels();
        updateActiveTabText(); // reflect the dirty marker
    });
    connect(editor_, &Editor::selectionChanged, this, [this] { refreshPanels(); });
    connect(editor_, &Editor::toolChanged, this, [this](ToolId t) {
        if (QAction* a = toolActions_.value(static_cast<int>(t), nullptr)) a->setChecked(true);
        canvas_->setFocus(); // so typing immediately opens the dynamic-input field
    });
    connect(editor_, &Editor::textPromptRequested, this, [this](const QString& prompt) {
        bool ok = false;
        const QString text = QInputDialog::getText(this, QStringLiteral("Edraw"), prompt,
                                                   QLineEdit::Normal, QString(), &ok);
        editor_->provideText(ok ? text : QString());
    });

    buildRibbon();
    buildPanels();
    buildStatusControls();
    buildCommandBar();
    buildMenus();
    buildCommands();

    newDrawing(); // start with a single empty drawing (no demo, no last session)

    QMetaObject::invokeMethod(this, [this] { canvas_->fitView(); canvas_->setFocus(); },
                              Qt::QueuedConnection);
}

void MainWindow::buildDocumentArea() {
    auto* container = new QWidget(this);
    auto* v = new QVBoxLayout(container);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);

    auto* topRow = new QWidget(container);
    auto* h = new QHBoxLayout(topRow);
    h->setContentsMargins(2, 2, 2, 0);
    h->setSpacing(2);
    docTabs_ = new QTabBar(topRow);
    docTabs_->setTabsClosable(true);
    docTabs_->setMovable(true);
    docTabs_->setExpanding(false);
    docTabs_->setDocumentMode(true);
    auto* plus = new QToolButton(topRow);
    plus->setText(QStringLiteral("+"));
    plus->setToolTip(QStringLiteral("New drawing"));
    plus->setAutoRaise(true);
    h->addWidget(docTabs_);
    h->addWidget(plus);
    h->addStretch(1);

    v->addWidget(topRow);
    v->addWidget(canvas_, 1);
    setCentralWidget(container);

    connect(docTabs_, &QTabBar::currentChanged, this, [this](int i) { activateIndex(i); });
    connect(docTabs_, &QTabBar::tabCloseRequested, this, [this](int i) { closeDocument(i); });
    connect(plus, &QToolButton::clicked, this, [this] { newDrawing(); });
}

MainWindow::~MainWindow() = default;

QAction* MainWindow::makeToolAction(const QString& text, ToolId id) {
    QAction* a = new QAction(makeToolIcon(id), text, this);
    a->setCheckable(true);
    a->setToolTip(text);
    toolGroup_->addAction(a);
    toolActions_.insert(static_cast<int>(id), a);
    connect(a, &QAction::triggered, this, [this, id] { editor_->setTool(id); });
    return a;
}

// Wrap ribbon content with a centered group label underneath (Edraw style).
QWidget* MainWindow::ribbonGroup(const QString& title, QWidget* content) {
    auto* g = new QWidget(ribbon_);
    auto* v = new QVBoxLayout(g);
    v->setContentsMargins(6, 2, 6, 2);
    v->setSpacing(1);
    v->addWidget(content, 1);
    auto* lbl = new QLabel(title, g);
    lbl->setAlignment(Qt::AlignHCenter);
    lbl->setStyleSheet(QStringLiteral("color:#7f8ca0; font-size:10px;"));
    v->addWidget(lbl);
    return g;
}

// A compact 2-row grid of icon tool buttons for the given tools.
QWidget* MainWindow::toolRow(const QList<ToolId>& tools) {
    auto* w = new QWidget(ribbon_);
    auto* grid = new QGridLayout(w);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(1);
    const int perRow = (tools.size() + 1) / 2;
    int i = 0;
    for (ToolId id : tools) {
        auto* b = new QToolButton(w);
        b->setDefaultAction(makeToolAction(toolLabel(id), id));
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setIconSize(QSize(22, 22));
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus); // keep keyboard focus on the canvas
        grid->addWidget(b, i / perRow, i % perRow);
        ++i;
    }
    return w;
}

void MainWindow::buildRibbon() {
    toolGroup_ = new QActionGroup(this);
    toolGroup_->setExclusive(true);

    ribbon_ = new QTabWidget(this);
    ribbon_->setDocumentMode(true);
    ribbon_->setFixedHeight(104);

    // A ribbon tab page holds horizontal groups separated by vertical rules.
    auto makeTabPage = [&]() {
        auto* page = new QWidget(ribbon_);
        auto* lay = new QHBoxLayout(page);
        lay->setContentsMargins(6, 4, 6, 2);
        lay->setSpacing(0);
        lay->setAlignment(Qt::AlignLeft);
        return std::pair<QWidget*, QHBoxLayout*>{page, lay};
    };
    auto addGroup = [&](QHBoxLayout* lay, const QString& title, QWidget* content) {
        lay->addWidget(ribbonGroup(title, content));
        auto* rule = new QFrame(lay->parentWidget());
        rule->setFrameShape(QFrame::VLine);
        rule->setStyleSheet(QStringLiteral("color:#222b38;"));
        lay->addWidget(rule);
    };

    // ---- HOME : Draw | Modify | Properties | AI ----
    {
        auto [page, lay] = makeTabPage();
        QAction* sel = makeToolAction(QStringLiteral("Select"), ToolId::Select);
        sel->setChecked(true);
        addGroup(lay, QStringLiteral("Draw"),
                 toolRow({ToolId::Line, ToolId::DoubleLine, ToolId::Polyline,
                          ToolId::DoublePolyline, ToolId::Rect, ToolId::Circle,
                          ToolId::Ellipse, ToolId::Arc, ToolId::Spline, ToolId::Text,
                          ToolId::Polygon}));
        addGroup(lay, QStringLiteral("Modify"),
                 toolRow({ToolId::Move, ToolId::Rotate, ToolId::Mirror, ToolId::Scale,
                          ToolId::Offset, ToolId::Trim, ToolId::Extend, ToolId::Fillet,
                          ToolId::Chamfer}));

        // Properties group: Layer + Color dropdowns.
        auto* propsW = new QWidget(page);
        auto* pg = new QGridLayout(propsW);
        pg->setContentsMargins(0, 0, 0, 0);
        pg->setSpacing(2);
        layerCombo_ = new QComboBox(propsW);
        layerCombo_->setMinimumWidth(120);
        colorCombo_ = new QComboBox(propsW);
        colorCombo_->setMinimumWidth(120);
        colorCombo_->addItems({"ByLayer", "White", "Red", "Yellow", "Green", "Cyan", "Blue", "Magenta"});
        pg->addWidget(new QLabel(QStringLiteral("Layer"), propsW), 0, 0);
        pg->addWidget(layerCombo_, 0, 1);
        pg->addWidget(new QLabel(QStringLiteral("Color"), propsW), 1, 0);
        pg->addWidget(colorCombo_, 1, 1);
        connect(layerCombo_, &QComboBox::activated, this, [this](int) {
            const QString name = layerCombo_->currentText();
            doc_->setActiveLayer(name);
            editor_->setSelectionLayer(name);
        });
        connect(colorCombo_, &QComboBox::activated, this, [this](int) {
            static const QHash<QString, QString> map = {
                {"ByLayer", ""}, {"White", "#ffffff"}, {"Red", "#ff0000"},
                {"Yellow", "#ffff00"}, {"Green", "#00ff00"}, {"Cyan", "#00ffff"},
                {"Blue", "#0000ff"}, {"Magenta", "#ff00ff"}};
            editor_->setSelectionColor(map.value(colorCombo_->currentText(), QString()));
        });
        addGroup(lay, QStringLiteral("Properties"), propsW);

        // AI group (Phase 7 — currently informational buttons).
        auto* aiW = new QWidget(page);
        auto* ag = new QGridLayout(aiW);
        ag->setContentsMargins(0, 0, 0, 0);
        ag->setSpacing(2);
        auto aiBtn = [&](const QString& t) {
            auto* b = new QToolButton(aiW);
            b->setText(t);
            b->setMinimumWidth(64);
            connect(b, &QToolButton::clicked, this, [this, t] {
                statusBar()->showMessage(QStringLiteral("%1 — coming in the AI phase").arg(t));
            });
            return b;
        };
        ag->addWidget(aiBtn(QStringLiteral("AI draw")), 0, 0);
        ag->addWidget(aiBtn(QStringLiteral("AI lib")), 0, 1);
        ag->addWidget(aiBtn(QStringLiteral("Codex")), 1, 0, 1, 2);
        lay->addWidget(ribbonGroup(QStringLiteral("AI"), aiW));
        lay->addStretch(1);
        ribbon_->addTab(page, QStringLiteral("Home"));
    }

    // ---- MODIFY ----
    {
        auto [page, lay] = makeTabPage();
        addGroup(lay, QStringLiteral("Transform"),
                 toolRow({ToolId::Move, ToolId::Rotate, ToolId::Mirror, ToolId::Scale,
                          ToolId::Offset}));
        addGroup(lay, QStringLiteral("Edit"),
                 toolRow({ToolId::Trim, ToolId::Extend, ToolId::Fillet, ToolId::Chamfer}));
        auto* opsW = new QWidget(page);
        auto* og = new QGridLayout(opsW);
        og->setContentsMargins(0, 0, 0, 0);
        og->setSpacing(2);
        auto opBtn = [&](const QString& t, std::function<void()> fn, int r, int c) {
            auto* b = new QToolButton(opsW);
            b->setText(t);
            b->setMinimumWidth(60);
            connect(b, &QToolButton::clicked, this, fn);
            og->addWidget(b, r, c);
        };
        opBtn(QStringLiteral("Array"), [this] { arrayDialog(); }, 0, 0);
        opBtn(QStringLiteral("Explode"), [this] { editor_->explodeSelected(); }, 0, 1);
        opBtn(QStringLiteral("Purge"), [this] { editor_->purgeSelected(); }, 0, 2);
        opBtn(QStringLiteral("Delete"), [this] { editor_->deleteSelected(); }, 1, 0);
        opBtn(QStringLiteral("Fillet R"), [this] { promptFilletRadius(); }, 1, 1);
        lay->addWidget(ribbonGroup(QStringLiteral("Operations"), opsW));
        lay->addStretch(1);
        ribbon_->addTab(page, QStringLiteral("Modify"));
    }

    // ---- INSERT ----
    {
        auto [page, lay] = makeTabPage();
        addGroup(lay, QStringLiteral("Annotation"), toolRow({ToolId::Text, ToolId::Dimension}));
        lay->addStretch(1);
        ribbon_->addTab(page, QStringLiteral("Insert"));
    }

    // ---- ANNOTATION ----
    {
        auto [page, lay] = makeTabPage();
        addGroup(lay, QStringLiteral("Annotate"), toolRow({ToolId::Dimension, ToolId::Text, ToolId::NetLabel}));
        lay->addStretch(1);
        ribbon_->addTab(page, QStringLiteral("Annotation"));
    }

    // ---- HARNESS BUILD ----
    {
        auto [page, lay] = makeTabPage();
        addGroup(lay, QStringLiteral("Place"),
                 toolRow({ToolId::Wire, ToolId::Connector, ToolId::Splice,
                          ToolId::NotConnected, ToolId::NetLabel}));

        auto* wireW = new QWidget(page);
        auto* wg = new QGridLayout(wireW);
        wg->setContentsMargins(0, 0, 0, 0);
        wg->setSpacing(2);
        auto* colorBox = new QComboBox(wireW);
        colorBox->addItems(wireColorCodes());
        colorBox->setCurrentText(editor_->wireColor());
        connect(colorBox, &QComboBox::currentTextChanged, this,
                [this](const QString& c) { editor_->wireColor() = c; });
        auto* gaugeBox = new QComboBox(wireW);
        gaugeBox->addItems({"0.25", "0.35", "0.5", "0.75", "1.0", "1.5", "2.5", "4.0", "6.0"});
        gaugeBox->setCurrentText(editor_->wireGauge());
        connect(gaugeBox, &QComboBox::currentTextChanged, this,
                [this](const QString& g) { editor_->wireGauge() = g; });
        auto* netEdit = new QLineEdit(wireW);
        netEdit->setMaximumWidth(110);
        connect(netEdit, &QLineEdit::textChanged, this,
                [this](const QString& n) { editor_->wireNet() = n; });
        wg->addWidget(new QLabel(QStringLiteral("Color"), wireW), 0, 0);
        wg->addWidget(colorBox, 0, 1);
        wg->addWidget(new QLabel(QStringLiteral("Gauge"), wireW), 1, 0);
        wg->addWidget(gaugeBox, 1, 1);
        wg->addWidget(new QLabel(QStringLiteral("Net"), wireW), 2, 0);
        wg->addWidget(netEdit, 2, 1);
        addGroup(lay, QStringLiteral("Wire"), wireW);

        auto* toolsW = new QWidget(page);
        auto* tg = new QGridLayout(toolsW);
        tg->setContentsMargins(0, 0, 0, 0);
        tg->setSpacing(2);
        auto* cfgBtn = new QToolButton(toolsW);
        cfgBtn->setText(QStringLiteral("Connector..."));
        connect(cfgBtn, &QToolButton::clicked, this, [this] { showConnectorDialog(); });
        auto* bomBtn = new QToolButton(toolsW);
        bomBtn->setText(QStringLiteral("BOM"));
        connect(bomBtn, &QToolButton::clicked, this, [this] { showBomDialog(); });
        tg->addWidget(cfgBtn, 0, 0);
        tg->addWidget(bomBtn, 1, 0);
        lay->addWidget(ribbonGroup(QStringLiteral("Tools"), toolsW));
        lay->addStretch(1);
        ribbon_->addTab(page, QStringLiteral("Harness build"));
    }

    // ---- SCHEMATIC BUILD / PARAMETRIC (placeholders for now) ----
    {
        auto [page, lay] = makeTabPage();
        addGroup(lay, QStringLiteral("Symbols"), toolRow({ToolId::Connector, ToolId::Wire}));
        lay->addStretch(1);
        ribbon_->addTab(page, QStringLiteral("Schematic build"));
    }
    {
        auto [page, lay] = makeTabPage();
        auto* lbl = new QLabel(QStringLiteral("Parametric constraints — coming soon"), page);
        lay->addWidget(lbl);
        lay->addStretch(1);
        ribbon_->addTab(page, QStringLiteral("Parametric"));
    }

    // ---- OUTPUT : file actions ----
    {
        auto [page, lay] = makeTabPage();
        auto* fileW = new QWidget(page);
        auto* fg = new QGridLayout(fileW);
        fg->setContentsMargins(0, 0, 0, 0);
        fg->setSpacing(2);
        auto fb = [&](const QString& t, std::function<void()> fn, int r, int c) {
            auto* b = new QToolButton(fileW);
            b->setText(t);
            b->setMinimumWidth(74);
            connect(b, &QToolButton::clicked, this, fn);
            fg->addWidget(b, r, c);
        };
        fb(QStringLiteral("New"), [this] { newDrawing(); }, 0, 0);
        fb(QStringLiteral("Open"), [this] { openDrawing(); }, 0, 1);
        fb(QStringLiteral("Save DWX"), [this] { saveDwxAs(); }, 1, 0);
        fb(QStringLiteral("Export DXF"), [this] { exportDxfAs(); }, 1, 1);
        addGroup(lay, QStringLiteral("File"), fileW);
        lay->addStretch(1);
        ribbon_->addTab(page, QStringLiteral("Output"));
    }

    QToolBar* top = addToolBar(QStringLiteral("Ribbon"));
    top->setMovable(false);
    top->setFloatable(false);
    top->addWidget(ribbon_);
    // Drafting toggles + grid size now live in the bottom-right status bar
    // (see buildStatusControls()).
}

void MainWindow::buildStatusControls() {
    const QString toggleQss =
        QStringLiteral("QToolButton{padding:2px;border:none;}"
                       "QToolButton:hover{background:#243245;}"
                       "QToolButton:checked{background:#1f3a5f;}"
                       "QToolButton::menu-button{width:12px;border:none;}");

    // A small icon toggle (AutoCAD status-bar style). Returns the button so a
    // dropdown ("fall menu") can be attached.
    auto addToggle = [&](const QString& icon, const QString& tip, bool initial,
                         auto setter) -> QToolButton* {
        auto* b = new QToolButton(this);
        b->setIcon(makeStatusIcon(icon));
        b->setIconSize(QSize(20, 20));
        b->setToolTip(tip);
        b->setCheckable(true);
        b->setChecked(initial);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setStyleSheet(toggleQss);
        connect(b, &QToolButton::toggled, this, setter);
        statusBar()->addPermanentWidget(b);
        return b;
    };

    // Grid size field.
    auto* gridLbl = new QLabel(QStringLiteral("Grid"), this);
    gridLbl->setStyleSheet(QStringLiteral("font-size:11px;color:#9aa7b8;"));
    auto* gridSpin = new QDoubleSpinBox(this);
    gridSpin->setRange(0.001, 100000.0);
    gridSpin->setDecimals(3);
    gridSpin->setValue(canvas_->gridSize());
    gridSpin->setMaximumWidth(74);
    gridSpin->setFocusPolicy(Qt::ClickFocus);
    gridSpin->setStyleSheet(QStringLiteral("QDoubleSpinBox{font-size:11px;}"));
    connect(gridSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) {
                canvas_->setGridSize(v);
                editor_->snapSettings().gridSize = v;
            });
    statusBar()->addPermanentWidget(gridLbl);
    statusBar()->addPermanentWidget(gridSpin);

    // Show grid.
    addToggle(QStringLiteral("grid"), QStringLiteral("Show grid"),
              canvas_->gridVisible(),
              [this](bool v) { canvas_->setGridVisible(v); });
    // Snap to grid.
    addToggle(QStringLiteral("gridsnap"), QStringLiteral("Snap to grid"),
              editor_->snapSettings().gridSnap,
              [this](bool v) { editor_->snapSettings().gridSnap = v; });
    addToggle(QStringLiteral("ortho"), QStringLiteral("Ortho mode"),
              editor_->snapSettings().ortho,
              [this](bool v) { editor_->snapSettings().ortho = v; });

    // Polar tracking + a "fall menu" of common angle increments.
    auto* polar = addToggle(QStringLiteral("polar"),
                            QStringLiteral("Polar tracking"),
                            editor_->snapSettings().polar,
                            [this](bool v) { editor_->snapSettings().polar = v; });
    {
        auto* menu = new QMenu(polar);
        auto* group = new QActionGroup(menu);
        group->setExclusive(true);
        for (double deg : {5.0, 10.0, 15.0, 22.5, 30.0, 45.0, 90.0}) {
            auto* act = menu->addAction(QStringLiteral("%1°").arg(deg, 0, 'g'));
            act->setCheckable(true);
            act->setChecked(qFuzzyCompare(editor_->snapSettings().polarStepDeg, deg));
            group->addAction(act);
            connect(act, &QAction::triggered, this, [this, deg]() {
                editor_->snapSettings().polarStepDeg = deg;
                editor_->snapSettings().polar = true;
                statusBar()->showMessage(
                    QStringLiteral("Polar tracking: %1°").arg(deg, 0, 'g'), 2000);
            });
        }
        polar->setMenu(menu);
        polar->setPopupMode(QToolButton::MenuButtonPopup);
    }

    // Object snap + a "fall menu" to pick which snap types are active.
    auto* osnap = addToggle(QStringLiteral("osnap"),
                            QStringLiteral("Object snap"),
                            editor_->snapSettings().objectSnap,
                            [this](bool v) { editor_->snapSettings().objectSnap = v; });
    {
        auto* menu = new QMenu(osnap);
        auto addSnap = [&](const QString& label, bool SnapSettings::*field) {
            auto* act = menu->addAction(label);
            act->setCheckable(true);
            act->setChecked(editor_->snapSettings().*field);
            connect(act, &QAction::toggled, this, [this, field](bool on) {
                editor_->snapSettings().*field = on;
            });
        };
        addSnap(QStringLiteral("Endpoint"), &SnapSettings::snapEndpoint);
        addSnap(QStringLiteral("Midpoint"), &SnapSettings::snapMidpoint);
        addSnap(QStringLiteral("Center"), &SnapSettings::snapCenter);
        addSnap(QStringLiteral("Intersection"), &SnapSettings::snapIntersection);
        addSnap(QStringLiteral("Nearest"), &SnapSettings::snapNearest);
        osnap->setMenu(menu);
        osnap->setPopupMode(QToolButton::MenuButtonPopup);
    }

    addToggle(QStringLiteral("angles"), QStringLiteral("Show drawing angles"),
              editor_->showAngles(),
              [this](bool v) { editor_->setShowAngles(v); });
}

void MainWindow::buildPanels() {
    // Panels can float free or nest/snap to any edge (left/right/top/bottom),
    // and tab together — like AutoCAD palettes.
    setDockNestingEnabled(true);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowNestedDocks |
                   QMainWindow::AllowTabbedDocks);
    setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);

    // Each dock: draggable out to a floating window, dockable to any side, closable.
    auto configDock = [](QDockWidget* d) {
        d->setAllowedAreas(Qt::AllDockWidgetAreas);
        d->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                       QDockWidget::DockWidgetClosable);
    };

    // ===== PROPERTIES (left, top) =====
    auto* propsDock = new AutoHideDock(QStringLiteral("PROPERTIES"), this);
    configDock(propsDock);
    auto* propsWidget = new QWidget(propsDock);
    auto* pv = new QVBoxLayout(propsWidget);
    pv->setContentsMargins(4, 4, 4, 4);
    pv->setSpacing(4);
    propsTable_ = new QTableWidget(propsWidget);
    propsTable_->setColumnCount(2);
    propsTable_->horizontalHeader()->setVisible(false);
    propsTable_->verticalHeader()->setVisible(false);
    propsTable_->setShowGrid(false);
    propsTable_->setColumnWidth(0, 110);
    propsTable_->horizontalHeader()->setStretchLastSection(true);
    propsTable_->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::SelectedClicked);
    pv->addWidget(propsTable_);
    propsDock->setManaged(new DockTitleBar(propsDock, QStringLiteral("PROPERTIES")), propsWidget);
    addDockWidget(Qt::LeftDockWidgetArea, propsDock);

    // Edit Layer / Color / Lineweight from the property grid.
    connect(propsTable_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (refreshing_ || item->column() != 1) return;
        const QString key = propsTable_->item(item->row(), 0) ? propsTable_->item(item->row(), 0)->text() : QString();
        if (key == "Lineweight") editor_->setSelectionWeight(item->text().toDouble());
    });
    connect(propsTable_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        QTableWidgetItem* key = propsTable_->item(row, 0);
        if (!key) return;
        if (key->text() == "Color") {
            const QColor c = QColorDialog::getColor(Qt::white, this, QStringLiteral("Entity color"));
            if (c.isValid()) editor_->setSelectionColor(c.name());
        }
    });

    // ===== LAYERS (left, below properties) =====
    auto* layersDock = new AutoHideDock(QStringLiteral("LAYERS"), this);
    configDock(layersDock);
    auto* layersWidget = new QWidget(layersDock);
    auto* lv = new QVBoxLayout(layersWidget);
    lv->setContentsMargins(4, 4, 4, 4);
    lv->setSpacing(4);

    auto* curLabel = new QLabel(QStringLiteral("Current layer: 0"), layersWidget);
    curLabel->setObjectName(QStringLiteral("currentLayerLabel"));
    lv->addWidget(curLabel);

    auto* nameRow = new QHBoxLayout();
    auto* nameEdit = new QLineEdit(layersWidget);
    nameEdit->setPlaceholderText(QStringLiteral("Layer name"));
    auto* addLayer = new QPushButton(QStringLiteral("Add"), layersWidget);
    auto* delLayer = new QPushButton(QStringLiteral("Delete"), layersWidget);
    auto* lockLayer = new QPushButton(QStringLiteral("Lock"), layersWidget);
    nameRow->addWidget(nameEdit, 1);
    nameRow->addWidget(addLayer);
    nameRow->addWidget(delLayer);
    nameRow->addWidget(lockLayer);
    lv->addLayout(nameRow);

    layersTable_ = new QTableWidget(layersWidget);
    layersTable_->setColumnCount(5);
    layersTable_->setHorizontalHeaderLabels({"Status", "Name", "On", "Lock", "Color"});
    layersTable_->horizontalHeader()->setStretchLastSection(true);
    layersTable_->verticalHeader()->setVisible(false);
    layersTable_->setColumnWidth(0, 48);
    layersTable_->setColumnWidth(2, 36);
    layersTable_->setColumnWidth(3, 40);
    lv->addWidget(layersTable_);
    layersDock->setManaged(new DockTitleBar(layersDock, QStringLiteral("LAYERS")), layersWidget);
    addDockWidget(Qt::LeftDockWidgetArea, layersDock);

    // Stack Properties above Layers in the left column (Edraw layout).
    splitDockWidget(propsDock, layersDock, Qt::Vertical);

    connect(addLayer, &QPushButton::clicked, this, [this, nameEdit] {
        auto& ls = doc_->layers();
        QString name = nameEdit->text().trimmed();
        if (name.isEmpty()) name = QStringLiteral("Layer%1").arg(ls.size());
        ls.push_back({name, "#ffffff", true, false, 2.0});
        nameEdit->clear();
        doc_->setDirty(true);
        refreshLayers();
    });
    connect(delLayer, &QPushButton::clicked, this, [this] {
        const int row = layersTable_->currentRow();
        auto& ls = doc_->layers();
        if (row >= 0 && row < static_cast<int>(ls.size()) && ls.size() > 1) {
            ls.erase(ls.begin() + row);
            doc_->setDirty(true);
            refreshLayers();
            canvas_->update();
        }
    });
    connect(lockLayer, &QPushButton::clicked, this, [this] {
        const int row = layersTable_->currentRow();
        auto& ls = doc_->layers();
        if (row >= 0 && row < static_cast<int>(ls.size())) {
            ls[row].locked = !ls[row].locked;
            doc_->setDirty(true);
            refreshLayers();
        }
    });
    connect(layersTable_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (refreshing_) return;
        auto& ls = doc_->layers();
        const int r = item->row();
        if (r < 0 || r >= static_cast<int>(ls.size())) return;
        if (item->column() == 1) ls[r].name = item->text();
        else if (item->column() == 2) ls[r].visible = item->checkState() == Qt::Checked;
        else if (item->column() == 3) ls[r].locked = item->checkState() == Qt::Checked;
        doc_->setDirty(true);
        canvas_->update();
    });
    connect(layersTable_, &QTableWidget::cellClicked, this, [this](int row, int) {
        auto& ls = doc_->layers();
        if (row >= 0 && row < static_cast<int>(ls.size())) {
            doc_->setActiveLayer(ls[row].name);
            refreshLayers();
        }
    });
    connect(layersTable_, &QTableWidget::cellDoubleClicked, this, [this](int row, int col) {
        if (col != 4) return;
        auto& ls = doc_->layers();
        if (row < 0 || row >= static_cast<int>(ls.size())) return;
        const QColor chosen = QColorDialog::getColor(QColor(ls[row].color), this,
                                                     QStringLiteral("Layer color"));
        if (chosen.isValid()) {
            ls[row].color = chosen.name();
            doc_->setDirty(true);
            refreshLayers();
            canvas_->update();
        }
    });

    // ===== ENTITIES (right) =====
    auto* listDock = new AutoHideDock(QStringLiteral("Entities"), this);
    configDock(listDock);
    entityList_ = new QListWidget(listDock);
    listDock->setManaged(new DockTitleBar(listDock, QStringLiteral("Entities")), entityList_);
    addDockWidget(Qt::RightDockWidgetArea, listDock);

    connect(entityList_, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        const QString id = item->data(Qt::UserRole).toString();
        const bool additive = QApplication::keyboardModifiers() & Qt::ControlModifier;
        editor_->selectById(id, additive);
    });
}

void MainWindow::buildCommandBar() {
    auto* bar = new QToolBar(QStringLiteral("Command"), this);
    bar->setMovable(false);
    auto* label = new QLabel(QStringLiteral("  Command: "), bar);
    commandEdit_ = new QLineEdit(bar);
    commandEdit_->setPlaceholderText(QStringLiteral("command (line, rect, circle...) — or, while drawing, a distance / x,y / @dx,dy + Enter"));
    bar->addWidget(label);
    bar->addWidget(commandEdit_);
    addToolBar(Qt::BottomToolBarArea, bar);

    connect(commandEdit_, &QLineEdit::returnPressed, this, [this] {
        const QString t = commandEdit_->text().trimmed();
        commandEdit_->clear();
        submitCommand(t);
        if (editor_->wantsTextInput()) commandEdit_->setFocus(); // keep chaining input
        else canvas_->setFocus();
    });

    // The floating dynamic-input field (next to the cursor) submits the same way.
    connect(canvas_, &CanvasWidget::commandSubmitted, this,
            [this](const QString& t) { submitCommand(t.trimmed()); });
}

void MainWindow::submitCommand(const QString& t) {
    if (t.isEmpty()) return;
    // While drawing, a value / "g" / "f" / coordinate is handled live; anything
    // else is treated as a command word.
    if (!editor_->acceptCommandInput(t)) runCommand(t);
}

void MainWindow::buildMenus() {
    // The File menu is presented as the red "E" application-logo button in the
    // top-left corner of the menu bar (Edraw style).
    auto* fileMenu = new QMenu(this);
    fileMenu->addAction(QStringLiteral("&New drawing"), QKeySequence::New, this, [this] { newDrawing(); });
    fileMenu->addAction(QStringLiteral("&Open DWX/DXF..."), QKeySequence::Open, this, [this] { openDrawing(); });
    fileMenu->addAction(QStringLiteral("&Save DWX..."), QKeySequence::Save, this, [this] { saveDwxAs(); });
    fileMenu->addAction(QStringLiteral("&Export DXF..."), this, [this] { exportDxfAs(); });
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("E&xit"), QKeySequence::Quit, qApp, &QApplication::quit);

    auto* logo = new QToolButton(this);
    logo->setText(QStringLiteral("E"));
    logo->setToolTip(QStringLiteral("Edraw — file menu"));
    logo->setMenu(fileMenu);
    logo->setPopupMode(QToolButton::InstantPopup);
    logo->setAutoRaise(false);
    logo->setStyleSheet(QStringLiteral(
        "QToolButton{background:transparent;color:#c0392b;font-weight:bold;font-size:14px;"
        "border:2px solid #c0392b;border-radius:4px;padding:0px 8px;margin:3px 6px;}"
        "QToolButton:hover{background:rgba(192,57,43,40);}"
        "QToolButton::menu-indicator{image:none;width:0;}"));
    menuBar()->setCornerWidget(logo, Qt::TopLeftCorner);

    QMenu* editMenu = menuBar()->addMenu(QStringLiteral("&Edit"));
    editMenu->addAction(QStringLiteral("&Undo"), QKeySequence::Undo, this, [this] { editor_->undo(); });
    editMenu->addAction(QStringLiteral("&Redo"), QKeySequence::Redo, this, [this] { editor_->redo(); });
    editMenu->addSeparator();
    editMenu->addAction(QStringLiteral("Select &all"), QKeySequence::SelectAll, this, [this] { editor_->selectAll(); });
    editMenu->addAction(QStringLiteral("&Delete"), QKeySequence::Delete, this, [this] { editor_->deleteSelected(); });
    editMenu->addSeparator();
    editMenu->addAction(QStringLiteral("Color: By&Layer"), this, [this] { editor_->setColorsByLayer(); });

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    viewMenu->addAction(QStringLiteral("&Fit drawing"), QKeySequence(Qt::Key_F), this, [this] { canvas_->fitView(); });

    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    helpMenu->addAction(QStringLiteral("&Commands..."), QKeySequence::HelpContents, this,
                        [this] { showHelp(); });
}

// ---- Panel refresh ---------------------------------------------------------

void MainWindow::refreshPanels() {
    refreshLayers();
    refreshProperties();
    refreshEntityList();
}

void MainWindow::refreshLayers() {
    if (!layersTable_) return;
    refreshing_ = true;
    const auto& ls = doc_->layers();
    const QString active = doc_->activeLayer();
    layersTable_->setRowCount(static_cast<int>(ls.size()));
    for (int r = 0; r < static_cast<int>(ls.size()); ++r) {
        auto* status = new QTableWidgetItem(ls[r].name == active ? QStringLiteral("●") : QString());
        status->setFlags(Qt::ItemIsEnabled);
        status->setTextAlignment(Qt::AlignCenter);
        status->setForeground(QColor("#5fb763"));
        layersTable_->setItem(r, 0, status);
        layersTable_->setItem(r, 1, new QTableWidgetItem(ls[r].name));
        auto* vis = new QTableWidgetItem();
        vis->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        vis->setCheckState(ls[r].visible ? Qt::Checked : Qt::Unchecked);
        layersTable_->setItem(r, 2, vis);
        auto* lock = new QTableWidgetItem();
        lock->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        lock->setCheckState(ls[r].locked ? Qt::Checked : Qt::Unchecked);
        layersTable_->setItem(r, 3, lock);
        auto* color = new QTableWidgetItem();
        color->setBackground(QColor(ls[r].color));
        color->setFlags(Qt::ItemIsEnabled);
        layersTable_->setItem(r, 4, color);
    }
    refreshing_ = false;

    // Current-layer label.
    if (auto* lbl = findChild<QLabel*>(QStringLiteral("currentLayerLabel")))
        lbl->setText(QStringLiteral("Current layer: %1").arg(active));

    // Ribbon Layer combo.
    if (layerCombo_) {
        const QSignalBlocker block(layerCombo_);
        layerCombo_->clear();
        for (const auto& l : ls) layerCombo_->addItem(l.color.isEmpty() ? l.name : l.name);
        layerCombo_->setCurrentText(active);
    }
}

void MainWindow::refreshProperties() {
    if (!propsTable_) return;
    refreshing_ = true;
    const auto& sel = editor_->selection();
    propsTable_->setRowCount(0);

    auto addHeader = [&](const QString& title) {
        const int r = propsTable_->rowCount();
        propsTable_->insertRow(r);
        auto* h = new QTableWidgetItem(title);
        QFont f = h->font();
        f.setBold(true);
        h->setFont(f);
        h->setFlags(Qt::ItemIsEnabled);
        h->setBackground(QColor("#161d29"));
        propsTable_->setItem(r, 0, h);
        auto* blank = new QTableWidgetItem();
        blank->setFlags(Qt::ItemIsEnabled);
        blank->setBackground(QColor("#161d29"));
        propsTable_->setItem(r, 1, blank);
    };
    auto addRow = [&](const QString& k, const QString& v) {
        const int r = propsTable_->rowCount();
        propsTable_->insertRow(r);
        auto* key = new QTableWidgetItem(k);
        key->setFlags(Qt::ItemIsEnabled);
        propsTable_->setItem(r, 0, key);
        auto* val = new QTableWidgetItem(v);
        val->setFlags(Qt::ItemIsEnabled);
        propsTable_->setItem(r, 1, val);
    };
    // A dropdown (fall menu) value cell. `apply` is deferred so the combo is not
    // destroyed inside its own signal when the panel rebuilds.
    auto addCombo = [&](const QString& k, const QStringList& items, const QString& current,
                        bool editable, std::function<void(const QString&)> apply) {
        const int r = propsTable_->rowCount();
        propsTable_->insertRow(r);
        auto* key = new QTableWidgetItem(k);
        key->setFlags(Qt::ItemIsEnabled);
        propsTable_->setItem(r, 0, key);
        auto* cb = new QComboBox(propsTable_);
        cb->setEditable(editable);
        cb->addItems(items);
        if (cb->findText(current) < 0) cb->insertItem(0, current);
        cb->setCurrentText(current);
        connect(cb, &QComboBox::currentTextChanged, this, [this, apply](const QString& t) {
            if (refreshing_) return;
            QTimer::singleShot(0, this, [apply, t] { apply(t); });
        });
        propsTable_->setCellWidget(r, 1, cb);
    };
    // A dropdown whose items carry an AutoCAD-style preview icon. `apply` gets
    // the entry's canonical data value (not the display text). Deferred apply.
    auto addIconCombo = [&](const QString& k, const QVector<ComboEntry>& items,
                            const QString& currentData,
                            std::function<void(const QString&)> apply) {
        const int r = propsTable_->rowCount();
        propsTable_->insertRow(r);
        auto* key = new QTableWidgetItem(k);
        key->setFlags(Qt::ItemIsEnabled);
        propsTable_->setItem(r, 0, key);
        auto* cb = new QComboBox(propsTable_);
        cb->setIconSize(QSize(72, 14));
        int idx = 0;
        for (int i = 0; i < items.size(); ++i) {
            cb->addItem(items[i].icon, items[i].text, items[i].data);
            if (items[i].data == currentData) idx = i;
        }
        cb->setCurrentIndex(idx);
        connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, cb, apply](int i) {
                    if (refreshing_) return;
                    const QString d = cb->itemData(i).toString();
                    QTimer::singleShot(0, this, [apply, d] { apply(d); });
                });
        propsTable_->setCellWidget(r, 1, cb);
    };
    // A numeric value cell that applies on edit (deferred).
    auto addNum = [&](const QString& k, double value, std::function<void(double)> apply) {
        const int r = propsTable_->rowCount();
        propsTable_->insertRow(r);
        auto* key = new QTableWidgetItem(k);
        key->setFlags(Qt::ItemIsEnabled);
        propsTable_->setItem(r, 0, key);
        auto* sp = new QDoubleSpinBox(propsTable_);
        sp->setRange(-1e9, 1e9);
        sp->setDecimals(3);
        sp->setValue(value);
        sp->setButtonSymbols(QAbstractSpinBox::NoButtons);
        connect(sp, &QDoubleSpinBox::editingFinished, this, [this, sp, apply] {
            if (refreshing_) return;
            const double v = sp->value();
            QTimer::singleShot(0, this, [apply, v] { apply(v); });
        });
        propsTable_->setCellWidget(r, 1, sp);
    };

    const Entity* e = nullptr;
    if (sel.size() == 1)
        for (const auto& ent : doc_->entities())
            if (ent.id == sel.front()) { e = &ent; break; }

    addHeader(QStringLiteral("General"));
    addRow(QStringLiteral("Type"), e ? entityTypeToString(e->type)
                                     : (sel.empty() ? QStringLiteral("No selection")
                                                    : QStringLiteral("%1 objects").arg(sel.size())));
    if (e) {
        QStringList layerNames;
        for (const auto& l : doc_->layers()) layerNames << l.name;
        static const QHash<QString, QString> hexToColor = {
            {"#ffffff", "White"}, {"#ff0000", "Red"}, {"#ffff00", "Yellow"},
            {"#00ff00", "Green"}, {"#00ffff", "Cyan"}, {"#0000ff", "Blue"},
            {"#ff00ff", "Magenta"}};

        // --- Color (swatch preview + Select Color...) ---
        const QString curColor = e->hasExplicitColor()
                                     ? hexToColor.value(e->color.toLower(), e->color)
                                     : QStringLiteral("ByLayer");
        QVector<ComboEntry> colorItems;
        auto addColor = [&](const QString& name, const QColor& c) {
            colorItems.push_back({name, colorSwatchIcon(c), name});
        };
        addColor(QStringLiteral("ByLayer"), QColor("#ffffff"));
        addColor(QStringLiteral("ByBlock"), QColor("#ffffff"));
        addColor(QStringLiteral("Red"), QColor("#ff0000"));
        addColor(QStringLiteral("Yellow"), QColor("#ffff00"));
        addColor(QStringLiteral("Green"), QColor("#00ff00"));
        addColor(QStringLiteral("Cyan"), QColor("#00ffff"));
        addColor(QStringLiteral("Blue"), QColor("#0000ff"));
        addColor(QStringLiteral("Magenta"), QColor("#ff00ff"));
        addColor(QStringLiteral("White"), QColor("#ffffff"));
        colorItems.push_back({QStringLiteral("Select Color..."), selectColorIcon(),
                              QStringLiteral("__pick__")});
        // A custom (non-named) explicit colour: show it as the current entry.
        if (e->hasExplicitColor() && !hexToColor.contains(e->color.toLower()))
            colorItems.push_front({e->color, colorSwatchIcon(QColor(e->color)), e->color});
        addIconCombo(QStringLiteral("Color"), colorItems, curColor, [this](const QString& d) {
            if (d == QLatin1String("__pick__")) {
                const QColor c = QColorDialog::getColor(Qt::white, this,
                                                        QStringLiteral("Select color"));
                if (c.isValid()) editor_->setSelectionColor(c.name());
                else refreshProperties(); // user cancelled -> revert the combo
                return;
            }
            static const QHash<QString, QString> m = {
                {"ByLayer", ""}, {"ByBlock", ""}, {"White", "#ffffff"}, {"Red", "#ff0000"},
                {"Yellow", "#ffff00"}, {"Green", "#00ff00"}, {"Cyan", "#00ffff"},
                {"Blue", "#0000ff"}, {"Magenta", "#ff00ff"}};
            editor_->setSelectionColor(m.contains(d) ? m.value(d) : d);
        });

        addCombo(QStringLiteral("Layer"), layerNames, e->layer, false,
                 [this](const QString& t) { editor_->setSelectionLayer(t); });

        // --- Linetype (dash-pattern preview) ---
        const QStringList ltNames = {"ByLayer",    "ByBlock", "CENTER", "CENTERX2",
                                     "Continuous", "DOT2",    "HIDDEN", "PHANTOM"};
        const QString curLt = e->meta.value("linetype").toString("ByLayer");
        QVector<ComboEntry> ltItems;
        for (const auto& n : ltNames) ltItems.push_back({n, linetypeIcon(n), n});
        if (!ltNames.contains(curLt))
            ltItems.push_front({curLt, linetypeIcon(curLt), curLt});
        addIconCombo(QStringLiteral("Linetype"), ltItems, curLt, [this](const QString& d) {
            editor_->setSelectionMeta(QStringLiteral("linetype"), d);
        });

        addCombo(QStringLiteral("Linetype scale"), {"0.25", "0.5", "1.0", "2.0", "5.0"},
                 e->meta.value("ltscale").toString("1.0"), true,
                 [this](const QString& t) { editor_->setSelectionMeta("ltscale", t); });
        addCombo(QStringLiteral("Plot style"), {"ByColor", "Normal", "Screening"},
                 e->meta.value("plotStyle").toString("ByColor"), false,
                 [this](const QString& t) { editor_->setSelectionMeta("plotStyle", t); });

        // --- Lineweight (thickness preview, in mm) ---
        const QVector<QPair<QString, double>> weights = {
            {"0.00 mm", 0.00}, {"0.05 mm", 0.05}, {"0.09 mm", 0.09}, {"0.13 mm", 0.13},
            {"0.15 mm", 0.15}, {"0.18 mm", 0.18}, {"0.20 mm", 0.20}, {"0.25 mm", 0.25},
            {"0.30 mm", 0.30}, {"0.35 mm", 0.35}, {"0.40 mm", 0.40}, {"0.50 mm", 0.50},
            {"0.53 mm", 0.53}, {"0.60 mm", 0.60}, {"0.70 mm", 0.70}, {"0.80 mm", 0.80},
            {"0.90 mm", 0.90}, {"1.00 mm", 1.00}, {"1.06 mm", 1.06}, {"1.20 mm", 1.20},
            {"1.40 mm", 1.40}, {"1.58 mm", 1.58}, {"2.00 mm", 2.00}, {"2.11 mm", 2.11}};
        QVector<ComboEntry> lwItems;
        lwItems.push_back(
            {QStringLiteral("ByLayer"), lineweightIcon(0.0, true), QStringLiteral("ByLayer")});
        for (const auto& w : weights)
            lwItems.push_back(
                {w.first, lineweightIcon(w.second, false), QString::number(w.second, 'f', 2)});
        const QString curLw =
            e->weight > 0 ? QString::number(e->weight, 'f', 2) : QStringLiteral("ByLayer");
        addIconCombo(QStringLiteral("Lineweight"), lwItems, curLw, [this](const QString& d) {
            editor_->setSelectionWeight(d == QLatin1String("ByLayer") ? 0.0 : d.toDouble());
        });
    } else {
        addRow(QStringLiteral("Color"), QStringLiteral("ByLayer"));
        addRow(QStringLiteral("Layer"), doc_->activeLayer());
        addRow(QStringLiteral("Linetype"), QStringLiteral("ByLayer"));
        addRow(QStringLiteral("Linetype scale"), QStringLiteral("1.0000"));
        addRow(QStringLiteral("Plot style"), QStringLiteral("ByColor"));
        addRow(QStringLiteral("Lineweight"), QStringLiteral("ByLayer"));
    }
    addRow(QStringLiteral("Transparency"), QStringLiteral("0"));
    addRow(QStringLiteral("Thickness"), QStringLiteral("0.0000"));

    addHeader(QStringLiteral("Geometry"));
    if (e) {
        const Bounds b = e->bounds();
        const Vec2 c = b.valid ? b.center() : Vec2{0, 0};
        addNum(QStringLiteral("Center X"), c.x,
               [this](double v) { editor_->setGeometryValue("Center X", v); });
        addNum(QStringLiteral("Center Y"), c.y,
               [this](double v) { editor_->setGeometryValue("Center Y", v); });
        addNum(QStringLiteral("Width"), b.valid ? b.width() : 0.0,
               [this](double v) { editor_->setGeometryValue("Width", v); });
        addNum(QStringLiteral("Height"), b.valid ? b.height() : 0.0,
               [this](double v) { editor_->setGeometryValue("Height", v); });
        if (e->type == EntityType::Circle || e->type == EntityType::Arc)
            addNum(QStringLiteral("Radius"), e->radius,
                   [this](double v) { editor_->setGeometryValue("Radius", v); });
    } else {
        addRow(QStringLiteral("Center X"), QStringLiteral("0"));
        addRow(QStringLiteral("Center Y"), QStringLiteral("0"));
        addRow(QStringLiteral("Width"), QStringLiteral("0"));
        addRow(QStringLiteral("Height"), QStringLiteral("0"));
    }
    refreshing_ = false;
}

void MainWindow::refreshEntityList() {
    if (!entityList_) return;
    entityList_->clear();
    const auto& sel = editor_->selection();
    for (const auto& e : doc_->entities()) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1  [%2]").arg(entityTypeToString(e.type), e.id));
        item->setData(Qt::UserRole, e.id);
        if (std::find(sel.begin(), sel.end(), e.id) != sel.end())
            item->setSelected(true);
        entityList_->addItem(item);
    }
}

// ---- Commands & dialogs ----------------------------------------------------

void MainWindow::buildCommands() {
    auto tool = [this](ToolId id) { return [this, id] { editor_->setTool(id); }; };
    commands_ = {
        // Draw
        {{"line", "l"}, "Draw", "Line", tool(ToolId::Line)},
        {{"pline", "pl", "polyline"}, "Draw", "Polyline", tool(ToolId::Polyline)},
        {{"dline", "dl", "ml"}, "Draw", "Double line", tool(ToolId::DoubleLine)},
        {{"dpline", "dpl", "mline"}, "Draw", "Double polyline", tool(ToolId::DoublePolyline)},
        {{"rectang", "rec", "rectangle", "rect"}, "Draw", "Rectangle", tool(ToolId::Rect)},
        {{"polygon", "pol"}, "Draw", "Polygon", tool(ToolId::Polygon)},
        {{"circle", "c"}, "Draw", "Circle", tool(ToolId::Circle)},
        {{"arc", "a"}, "Draw", "Arc", tool(ToolId::Arc)},
        {{"ellipse", "el"}, "Draw", "Ellipse", tool(ToolId::Ellipse)},
        {{"spline", "spl"}, "Draw", "Spline", tool(ToolId::Spline)},
        {{"text", "t", "dt"}, "Draw", "Single-line text", tool(ToolId::Text)},
        {{"dim", "dimlinear"}, "Draw", "Linear dimension", tool(ToolId::Dimension)},
        // Harness
        {{"wire", "w"}, "Harness", "Wire", tool(ToolId::Wire)},
        {{"connector", "con"}, "Harness", "Connector", tool(ToolId::Connector)},
        {{"splice"}, "Harness", "Splice marker", tool(ToolId::Splice)},
        {{"notconnected", "nc"}, "Harness", "Not-connected marker", tool(ToolId::NotConnected)},
        {{"netlabel", "nl"}, "Harness", "Net label", tool(ToolId::NetLabel)},
        {{"bom"}, "Harness", "Bill of materials", [this] { showBomDialog(); }},
        // Modify
        {{"move", "m"}, "Modify", "Move (base point, target)", tool(ToolId::Move)},
        {{"rotate", "ro"}, "Modify", "Rotate (pivot, ref, target)", tool(ToolId::Rotate)},
        {{"mirror", "mi"}, "Modify", "Mirror across an axis", tool(ToolId::Mirror)},
        {{"scale", "sc"}, "Modify", "Scale (base, ref, new)", tool(ToolId::Scale)},
        {{"offset", "o"}, "Modify", "Offset", tool(ToolId::Offset)},
        {{"trim", "tr"}, "Modify", "Trim to a boundary", tool(ToolId::Trim)},
        {{"extend", "ex"}, "Modify", "Extend to a boundary", tool(ToolId::Extend)},
        {{"fillet", "f"}, "Modify", "Fillet two lines (set radius: r <value>)", tool(ToolId::Fillet)},
        {{"chamfer", "cha"}, "Modify", "Chamfer two lines", tool(ToolId::Chamfer)},
        {{"array", "ar"}, "Modify", "Rectangular array", [this] { arrayDialog(); }},
        {{"explode", "x"}, "Modify", "Explode into segments", [this] { editor_->explodeSelected(); }},
        {{"purge", "pu"}, "Modify", "Separate into single primitives", [this] { editor_->purgeSelected(); }},
        {{"erase", "e", "delete", "del"}, "Modify", "Delete the selection", [this] { editor_->deleteSelected(); }},
        // Edit
        {{"select", "sel"}, "Edit", "Select tool", tool(ToolId::Select)},
        {{"selectall", "all"}, "Edit", "Select everything", [this] { editor_->selectAll(); }},
        {{"copy", "co", "cp"}, "Edit", "Copy selection to clipboard", [this] { editor_->copySelection(); }},
        {{"paste", "pa"}, "Edit", "Paste from clipboard", [this] { editor_->paste(); }},
        {{"undo", "u"}, "Edit", "Undo", [this] { editor_->undo(); }},
        {{"redo"}, "Edit", "Redo", [this] { editor_->redo(); }},
        {{"bylayer"}, "Edit", "Set selection color to ByLayer", [this] { editor_->setColorsByLayer(); }},
        // View
        {{"zoom", "z", "fit", "extents", "ze"}, "View", "Zoom to fit the drawing", [this] { canvas_->fitView(); }},
        // File
        {{"new"}, "File", "New drawing tab", [this] { newDrawing(); }},
        {{"open"}, "File", "Open a .dwx / .dxf", [this] { openDrawing(); }},
        {{"save", "qsave"}, "File", "Save DWX", [this] { saveDwxAs(); }},
        {{"export", "dxfout"}, "File", "Export DXF", [this] { exportDxfAs(); }},
        // Help
        {{"help", "?"}, "Help", "Show this command list", [this] { showHelp(); }},
    };
}

void MainWindow::runCommand(const QString& text) {
    const QStringList parts = text.split(' ', Qt::SkipEmptyParts);
    if (parts.isEmpty()) return;
    const QString cmd = parts.front().toLower();

    // Numeric option: "r <value>" sets the fillet radius.
    if (cmd == "r" && parts.size() >= 2) {
        editor_->setFilletRadius(parts[1].toDouble());
        statusBar()->showMessage(QStringLiteral("Fillet radius = %1").arg(parts[1]));
        return;
    }

    for (const auto& c : commands_) {
        if (c.names.contains(cmd)) { c.run(); return; }
    }
    statusBar()->showMessage(
        QStringLiteral("Unknown command: %1  (type 'help' for the list)").arg(cmd));
}

void MainWindow::showHelp() {
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Commands"));
    dlg.resize(560, 640);
    auto* lay = new QVBoxLayout(&dlg);

    auto* table = new QTableWidget(&dlg);
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels({"Command (aliases)", "Description"});
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setColumnWidth(0, 200);

    // Group rows by category, in a fixed display order.
    const QStringList order = {"Draw", "Modify", "Harness", "Edit", "View", "File", "Help"};
    auto addRow = [&](const QString& a, const QString& b, bool header) {
        const int r = table->rowCount();
        table->insertRow(r);
        auto* c0 = new QTableWidgetItem(a);
        auto* c1 = new QTableWidgetItem(b);
        if (header) {
            QFont f = c0->font(); f.setBold(true); c0->setFont(f);
            c0->setBackground(QColor("#161d29"));
            c1->setBackground(QColor("#161d29"));
        }
        table->setItem(r, 0, c0);
        table->setItem(r, 1, c1);
    };
    for (const QString& cat : order) {
        bool first = true;
        for (const auto& c : commands_) {
            if (c.category != cat) continue;
            if (first) { addRow(cat, QString(), true); first = false; }
            QString name = c.names.first().toUpper();
            if (c.names.size() > 1) {
                QStringList al;
                for (int i = 1; i < c.names.size(); ++i) al << c.names[i].toUpper();
                name += QStringLiteral("  (%1)").arg(al.join(", "));
            }
            addRow(name, c.desc, false);
        }
    }
    // Note the live-input options.
    addRow(QStringLiteral("(while drawing)"), QString(), true);
    addRow(QStringLiteral("<number>"), QStringLiteral("Distance along the cursor direction"), false);
    addRow(QStringLiteral("x,y  /  @dx,dy"), QStringLiteral("Absolute / relative point"), false);
    addRow(QStringLiteral("g  /  f"), QStringLiteral("Set double-line gap / polyline fillet radius"), false);
    addRow(QStringLiteral("Space / Enter"), QStringLiteral("Repeat last tool (Esc cancels)"), false);

    lay->addWidget(table);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    lay->addWidget(box);
    dlg.exec();
}

void MainWindow::arrayDialog() {
    bool ok = false;
    const int rows = QInputDialog::getInt(this, QStringLiteral("Array"), QStringLiteral("Rows:"), 2, 1, 200, 1, &ok);
    if (!ok) return;
    const int cols = QInputDialog::getInt(this, QStringLiteral("Array"), QStringLiteral("Columns:"), 2, 1, 200, 1, &ok);
    if (!ok) return;
    const double dy = QInputDialog::getDouble(this, QStringLiteral("Array"), QStringLiteral("Row spacing:"), 20, -1e6, 1e6, 3, &ok);
    if (!ok) return;
    const double dx = QInputDialog::getDouble(this, QStringLiteral("Array"), QStringLiteral("Column spacing:"), 20, -1e6, 1e6, 3, &ok);
    if (!ok) return;
    editor_->arraySelected(rows, cols, dx, dy);
}

void MainWindow::promptFilletRadius() {
    bool ok = false;
    const double r = QInputDialog::getDouble(this, QStringLiteral("Fillet"),
                                             QStringLiteral("Radius:"), editor_->filletRadius(),
                                             0, 1e6, 3, &ok);
    if (ok) editor_->setFilletRadius(r);
}

void MainWindow::promptChamferDistance() {
    bool ok = false;
    const double d = QInputDialog::getDouble(this, QStringLiteral("Chamfer"),
                                             QStringLiteral("Distance:"), editor_->chamferDistance(),
                                             0, 1e6, 3, &ok);
    if (ok) editor_->setChamferDistance(d);
}

// ---- Harness dialogs -------------------------------------------------------

void MainWindow::showConnectorDialog() {
    ConnectorSpec spec = editor_->connectorSpec();
    const QString selId = editor_->selectedConnectorId();
    if (!selId.isEmpty()) {
        for (const auto& e : doc_->entities())
            if (e.id == selId) { spec = connectorSpecFromMeta(e.meta); break; }
    }

    QDialog dlg(this);
    dlg.setWindowTitle(selId.isEmpty() ? QStringLiteral("Connector defaults")
                                       : QStringLiteral("Edit connector"));
    auto* form = new QFormLayout(&dlg);
    auto* designator = new QLineEdit(spec.designator, &dlg);
    auto* part = new QLineEdit(spec.partNumber, &dlg);
    auto* pins = new QSpinBox(&dlg);
    pins->setRange(1, 128);
    pins->setValue(spec.pinCount);
    auto* pitch = new QDoubleSpinBox(&dlg);
    pitch->setRange(1, 200);
    pitch->setValue(spec.pitch);
    auto* pinLen = new QDoubleSpinBox(&dlg);
    pinLen->setRange(1, 500);
    pinLen->setValue(spec.pinLength);
    auto* mirror = new QCheckBox(QStringLiteral("Pins exit left"), &dlg);
    mirror->setChecked(spec.mirrorX);
    form->addRow(QStringLiteral("Designator"), designator);
    form->addRow(QStringLiteral("Part number"), part);
    form->addRow(QStringLiteral("Pin count"), pins);
    form->addRow(QStringLiteral("Pin pitch"), pitch);
    form->addRow(QStringLiteral("Pin length"), pinLen);
    form->addRow(QString(), mirror);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(box);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    spec.designator = designator->text();
    spec.partNumber = part->text();
    const int newCount = pins->value();
    spec.pitch = pitch->value();
    spec.pinLength = pinLen->value();
    spec.mirrorX = mirror->isChecked();
    // Re-number pins to match the new count (keep existing labels where possible).
    while (spec.pins.size() < newCount) spec.pins << QString::number(spec.pins.size() + 1);
    spec.pins = spec.pins.mid(0, newCount);
    spec.pinCount = newCount;

    editor_->applyConnectorSpecToSelection(spec);
    refreshPanels();
    statusBar()->showMessage(selId.isEmpty() ? QStringLiteral("Connector defaults updated")
                                             : QStringLiteral("Connector updated"));
}

void MainWindow::showBomDialog() {
    const auto rows = editor_->buildBom();
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Bill of materials"));
    dlg.resize(620, 420);
    auto* lay = new QVBoxLayout(&dlg);
    auto* table = new QTableWidget(&dlg);
    table->setColumnCount(5);
    table->setHorizontalHeaderLabels({"Kind", "Ref", "Part / Color", "Detail", "Qty / Len"});
    table->horizontalHeader()->setStretchLastSection(true);
    table->setRowCount(static_cast<int>(rows.size()));
    for (int r = 0; r < static_cast<int>(rows.size()); ++r) {
        table->setItem(r, 0, new QTableWidgetItem(rows[r].kind));
        table->setItem(r, 1, new QTableWidgetItem(rows[r].ref));
        table->setItem(r, 2, new QTableWidgetItem(rows[r].part));
        table->setItem(r, 3, new QTableWidgetItem(rows[r].detail));
        table->setItem(r, 4, new QTableWidgetItem(rows[r].qty));
    }
    lay->addWidget(table);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    lay->addWidget(box);
    dlg.exec();
}

// ---- Documents (tabs) ------------------------------------------------------

int MainWindow::activeIndex() const { return docTabs_ ? docTabs_->currentIndex() : -1; }

Document* MainWindow::addDocument(std::unique_ptr<Document> doc, const QString& path) {
    Document* raw = doc.get();
    docs_.push_back(std::move(doc));
    docPaths_.push_back(path);
    const QString base = path.isEmpty() ? raw->name() : QFileInfo(path).fileName();
    const int idx = docTabs_->addTab(base);
    docTabs_->setCurrentIndex(idx); // fires currentChanged -> activateIndex
    if (activeIndex() != idx) activateIndex(idx); // first tab: index already 0
    return raw;
}

void MainWindow::activateIndex(int index) {
    if (index < 0 || index >= static_cast<int>(docs_.size())) return;
    doc_ = docs_[index].get();
    canvas_->setDocument(doc_);
    editor_->setDocument(doc_); // resets selection/undo for this drawing
    refreshPanels();
    updateWindowTitleForActive();
}

void MainWindow::updateActiveTabText() {
    const int i = activeIndex();
    if (i < 0 || i >= static_cast<int>(docs_.size())) return;
    const QString base = docPaths_[i].isEmpty() ? docs_[i]->name()
                                                : QFileInfo(docPaths_[i]).fileName();
    docTabs_->setTabText(i, base + (docs_[i]->dirty() ? QStringLiteral(" *") : QString()));
    updateWindowTitleForActive();
}

void MainWindow::updateWindowTitleForActive() {
    const int i = activeIndex();
    if (i < 0 || i >= static_cast<int>(docs_.size())) { setWindowTitle(QStringLiteral("Edraw")); return; }
    const QString base = docPaths_[i].isEmpty() ? docs_[i]->name()
                                                : QFileInfo(docPaths_[i]).fileName();
    setWindowTitle(QStringLiteral("Edraw - %1%2")
                       .arg(base, docs_[i]->dirty() ? QStringLiteral(" *") : QString()));
}

bool MainWindow::maybeSaveDoc(int index) {
    if (index < 0 || index >= static_cast<int>(docs_.size())) return true;
    if (!docs_[index]->dirty()) return true;
    const QString name = docPaths_[index].isEmpty() ? docs_[index]->name()
                                                     : QFileInfo(docPaths_[index]).fileName();
    const auto choice = QMessageBox::warning(
        this, QStringLiteral("Save changes?"),
        QStringLiteral("\"%1\" has unsaved changes. Save before closing?").arg(name),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (choice == QMessageBox::Cancel) return false;
    if (choice == QMessageBox::Discard) return true;
    return saveDocument(index); // Save: only proceed if the save actually happened
}

bool MainWindow::saveDocument(int index, bool forceDialog) {
    if (index < 0 || index >= static_cast<int>(docs_.size())) return false;
    QString path = docPaths_[index];
    if (path.isEmpty() || forceDialog) {
        path = QFileDialog::getSaveFileName(this, QStringLiteral("Save DWX"),
                                            path, QStringLiteral("DWX (*.dwx)"));
        if (path.isEmpty()) return false;
        if (!path.endsWith(".dwx", Qt::CaseInsensitive)) path += ".dwx";
    }
    QString err;
    if (!dwx::save(*docs_[index], path, &err)) {
        QMessageBox::warning(this, QStringLiteral("Save failed"), err);
        return false;
    }
    docPaths_[index] = path;
    docs_[index]->setDirty(false);
    updateActiveTabText();
    statusBar()->showMessage(QStringLiteral("Saved %1").arg(QFileInfo(path).fileName()));
    return true;
}

void MainWindow::closeDocument(int index) {
    if (index < 0 || index >= static_cast<int>(docs_.size())) return;
    if (!maybeSaveDoc(index)) return; // user cancelled
    {
        QSignalBlocker block(docTabs_);
        docTabs_->removeTab(index);
        docs_.erase(docs_.begin() + index);
        docPaths_.erase(docPaths_.begin() + index);
    }
    if (docs_.empty()) { newDrawing(); return; }
    const int ni = std::min(index, static_cast<int>(docs_.size()) - 1);
    docTabs_->setCurrentIndex(ni);
    activateIndex(ni);
}

void MainWindow::closeEvent(QCloseEvent* e) {
    for (int i = 0; i < static_cast<int>(docs_.size()); ++i) {
        if (docs_[i]->dirty()) {
            docTabs_->setCurrentIndex(i);
            if (!maybeSaveDoc(i)) { e->ignore(); return; }
        }
    }
    e->accept();
}

// ---- File actions ----------------------------------------------------------

void MainWindow::newDrawing() {
    addDocument(std::make_unique<Document>(
                    QStringLiteral("Drawing%1").arg(docs_.size() + 1)),
                QString());
    statusBar()->showMessage(QStringLiteral("New drawing"));
}

void MainWindow::openDrawing() {
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Open drawing"), QString(),
        QStringLiteral("Drawings (*.dwx *.dxf);;DWX (*.dwx);;DXF (*.dxf);;All files (*)"));
    if (path.isEmpty()) return;
    openPath(path);
}

void MainWindow::openPath(const QString& path) {
    auto fresh = std::make_unique<Document>();
    fresh->entities().clear();
    QString err;
    const bool isDxf = path.endsWith(".dxf", Qt::CaseInsensitive);
    const bool ok = isDxf ? dxf::importFile(*fresh, path, &err)
                          : dwx::load(*fresh, path, &err);
    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("Open failed"), err);
        return;
    }
    const int n = static_cast<int>(fresh->entities().size());
    addDocument(std::move(fresh), path); // opens in a new tab
    canvas_->fitView();
    statusBar()->showMessage(
        QStringLiteral("Opened %1 (%2 entities)").arg(QFileInfo(path).fileName()).arg(n));
}

void MainWindow::saveDwxAs() {
    // Save the active drawing in place if it has a path, otherwise prompt.
    if (doc_) saveDocument(activeIndex(), /*forceDialog=*/false);
}

void MainWindow::exportDxfAs() {
    if (!doc_) return;
    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export DXF"), QString(), QStringLiteral("DXF (*.dxf)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(".dxf", Qt::CaseInsensitive)) path += ".dxf";
    QString err;
    if (!dxf::exportFile(*doc_, path, &err)) {
        QMessageBox::warning(this, QStringLiteral("Export failed"), err);
        return;
    }
    statusBar()->showMessage(QStringLiteral("Exported %1").arg(QFileInfo(path).fileName()));
}

} // namespace Edraw_Desktop
