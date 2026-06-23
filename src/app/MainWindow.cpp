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
#include <QCompleter>
#include <QGroupBox>
#include <QRadioButton>
#include <QSlider>
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
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSettings>
#include <QSignalBlocker>
#include <QTimer>
#include <QHash>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QPaintEvent>
#include <QResizeEvent>
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
QIcon makeToolIcon(ToolId id, const QColor& ink = QColor("#d6dae1")) {
    QPixmap pm(24, 24);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(ink);
    pen.setWidthF(1.6);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    switch (id) {
    case ToolId::Select: {
        QPolygonF a({{6, 4}, {6, 18}, {10, 14}, {13, 20}, {15, 19}, {12, 13}, {17, 13}});
        p.setBrush(ink);
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
    case ToolId::Splice: p.setBrush(ink); p.drawEllipse(QPointF(12, 12), 4, 4); break;
    case ToolId::NotConnected: p.drawLine(7, 7, 17, 17); p.drawLine(17, 7, 7, 17); break;
    case ToolId::NetLabel: { QPolygonF a({{4, 8}, {15, 8}, {20, 12}, {15, 16}, {4, 16}}); p.drawPolygon(a); break; }
    }
    p.end();
    return QIcon(pm);
}

// Icons for the bottom-right drafting toggles (grid / grid-snap / polar / object
// snap), in the same flat line-art style as the tool icons.
QIcon makeStatusIcon(const QString& which, const QColor& ink = QColor("#cfd8e6")) {
    QPixmap pm(22, 22);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QColor dimC = ink;
    dimC.setAlpha(150); // secondary lines in gridsnap/osnap
    QPen pen(ink);
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
        QPen dim(dimC);
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
        p.setBrush(ink);
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
        QPen dim(dimC);
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

// AutoCAD-style object-snap marker glyphs for the osnap fall-menu (soft magenta,
// like the running-osnap markers AutoCAD draws on a dark canvas).
QIcon makeOsnapMarkerIcon(const QString& kind) {
    QPixmap pm(18, 18);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor("#e2a0c6"));
    pen.setWidthF(1.5);
    pen.setJoinStyle(Qt::MiterJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    const QPointF c(9, 9);
    if (kind == QLatin1String("endpoint")) {
        p.drawRect(QRectF(3.5, 3.5, 11, 11)); // square
    } else if (kind == QLatin1String("midpoint")) {
        p.drawPolygon(QPolygonF({{9, 3}, {15, 15}, {3, 15}})); // triangle
    } else if (kind == QLatin1String("center")) {
        p.drawEllipse(c, 6, 6); // circle
    } else if (kind == QLatin1String("intersection")) {
        p.drawLine(QPointF(3.5, 3.5), QPointF(14.5, 14.5)); // X
        p.drawLine(QPointF(14.5, 3.5), QPointF(3.5, 14.5));
    } else if (kind == QLatin1String("nearest")) {
        // Hourglass / bowtie.
        p.drawPolygon(QPolygonF({{3.5, 3.5}, {14.5, 3.5}, {3.5, 14.5}, {14.5, 14.5}}));
    }
    p.end();
    return QIcon(pm);
}

// ---- Flyout method icons (circle / arc / ellipse construction options) ----
// Drawn in a theme-neutral blue with red point markers so they read on both the
// dark and light menus.
namespace {
const QColor kGeoBlue("#5a9bf0");
const QColor kGeoDot("#e0584f");
void geoDot(QPainter& p, const QPointF& c) {
    p.setPen(Qt::NoPen);
    p.setBrush(kGeoDot);
    p.drawEllipse(c, 1.8, 1.8);
}
// 48x48 canvas drawn in 24-unit space (painter scaled 2x) so the flyout icons are
// ~200% larger and clearly visible in the menus.
QPixmap geoCanvas() {
    QPixmap pm(48, 48);
    pm.fill(Qt::transparent);
    return pm;
}
} // namespace

QIcon makeCircleMethodIcon(int mode) {
    QPixmap pm = geoCanvas();
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(2, 2);
    QPen pen(kGeoBlue); pen.setWidthF(1.4);
    p.setPen(pen); p.setBrush(Qt::NoBrush);
    const QPointF c(12, 12); const double r = 7.5;
    p.drawEllipse(c, r, r);
    switch (mode) {
    case 0: // Center, Radius
        p.drawLine(c, QPointF(c.x() + r, c.y())); geoDot(p, c); break;
    case 1: // Center, Diameter
        p.drawLine(QPointF(c.x() - r, c.y()), QPointF(c.x() + r, c.y())); geoDot(p, c); break;
    case 2: // 2-Point
        geoDot(p, QPointF(c.x() - r, c.y())); geoDot(p, QPointF(c.x() + r, c.y())); break;
    case 4:   // Tan, Tan, Radius — two tangent lines + radius
    case 5: { // Tan, Tan, Tan — three tangent lines
        QPen tp(kGeoDot); tp.setWidthF(1.0); p.setPen(tp);
        p.drawLine(QPointF(2, c.y() + r), QPointF(22, c.y() + r));            // bottom tangent
        p.drawLine(QPointF(c.x() - r, 2), QPointF(c.x() - r, 22));            // left tangent
        if (mode == 5) p.drawLine(QPointF(22, 2), QPointF(2, 22));            // third tangent
        else { p.setPen(QPen(kGeoBlue)); p.drawLine(c, QPointF(c.x() + r, c.y())); } // radius
        break;
    }
    default: // 3-Point
        geoDot(p, QPointF(c.x(), c.y() - r));
        geoDot(p, QPointF(c.x() + r * 0.87, c.y() + r * 0.5));
        geoDot(p, QPointF(c.x() - r * 0.87, c.y() + r * 0.5));
        break;
    }
    p.end();
    return QIcon(pm);
}

QIcon makeArcMethodIcon(int mode) {
    QPixmap pm = geoCanvas();
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(2, 2);
    QPen pen(kGeoBlue); pen.setWidthF(1.4);
    p.setPen(pen); p.setBrush(Qt::NoBrush);
    const QPointF c(12, 14); const double r = 8.0;
    QPainterPath path;
    path.arcMoveTo(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), 20);
    path.arcTo(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), 20, 140);
    p.drawPath(path);
    const QPointF s(c.x() + r * 0.94, c.y() - r * 0.34);
    const QPointF en(c.x() - r * 0.94, c.y() - r * 0.34);
    const QPointF md(c.x(), c.y() - r);
    switch (mode) {
    case 0: geoDot(p, s); geoDot(p, md); geoDot(p, en); break;       // 3-Point
    case 1: geoDot(p, s); geoDot(p, c); geoDot(p, en); break;         // Start,Center,End
    case 2: geoDot(p, c); geoDot(p, s); geoDot(p, en); break;         // Center,Start,End
    default: geoDot(p, s); geoDot(p, en); break;                      // Start,End,Radius
    }
    p.end();
    return QIcon(pm);
}

QIcon makeEllipseMethodIcon(int mode) {
    QPixmap pm = geoCanvas();
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(2, 2);
    QPen pen(kGeoBlue); pen.setWidthF(1.4);
    p.setPen(pen); p.setBrush(Qt::NoBrush);
    const QPointF c(12, 12); const double rx = 9, ry = 5.5;
    if (mode == 2) { // Elliptical arc: partial ellipse
        QPainterPath path;
        path.arcMoveTo(QRectF(c.x() - rx, c.y() - ry, 2 * rx, 2 * ry), 30);
        path.arcTo(QRectF(c.x() - rx, c.y() - ry, 2 * rx, 2 * ry), 30, 250);
        p.drawPath(path);
    } else {
        p.drawEllipse(c, rx, ry);
    }
    if (mode == 0) geoDot(p, c);                                   // Center
    else if (mode == 1) {                                          // Axis, End
        geoDot(p, QPointF(c.x() - rx, c.y()));
        geoDot(p, QPointF(c.x() + rx, c.y()));
    }
    p.end();
    return QIcon(pm);
}

QIcon makeSplineMethodIcon(int mode) {
    QPixmap pm = geoCanvas();
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(2, 2);
    const QPointF a(3, 17), b(9, 5), c(15, 17), d(21, 6);
    if (mode == 1) {
        // CV: dashed control frame + red control dots, smooth curve nearby.
        QPen frame(kGeoDot); frame.setStyle(Qt::DashLine); frame.setWidthF(1.0);
        p.setPen(frame);
        p.drawPolyline(QPolygonF({a, b, c, d}));
    }
    QPen pen(kGeoBlue); pen.setWidthF(1.5);
    p.setPen(pen); p.setBrush(Qt::NoBrush);
    QPainterPath path;
    path.moveTo(a);
    path.cubicTo(b, b, QPointF((b.x() + c.x()) / 2, (b.y() + c.y()) / 2));
    path.cubicTo(c, c, d);
    p.drawPath(path);
    if (mode == 0) { geoDot(p, a); geoDot(p, b); geoDot(p, c); geoDot(p, d); } // fit points on the curve
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

// Live crosshair preview for the Options dialog: paints the crosshair with the
// chosen size / pick-box / colour on a sample (canvas-like) background.
class CrosshairPreview : public QWidget {
public:
    explicit CrosshairPreview(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(160, 96);
    }
    void setParams(int pct, int box, const QColor& c) {
        pct_ = pct; box_ = box; col_ = c; update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor("#10141b")); // matches the dark model space
        const QPointF c(width() / 2.0, height() / 2.0);
        const double arm = (pct_ >= 100)
                               ? std::max(width(), height())
                               : std::min(width(), height()) * 0.5 * pct_ / 100.0;
        const double b = box_;
        QPen pen(col_);
        pen.setCosmetic(true);
        p.setPen(pen);
        p.drawLine(QPointF(std::max(0.0, c.x() - arm), c.y()), QPointF(c.x() - b, c.y()));
        p.drawLine(QPointF(c.x() + b, c.y()), QPointF(std::min<double>(width(), c.x() + arm), c.y()));
        p.drawLine(QPointF(c.x(), std::max(0.0, c.y() - arm)), QPointF(c.x(), c.y() - b));
        p.drawLine(QPointF(c.x(), c.y() + b), QPointF(c.x(), std::min<double>(height(), c.y() + arm)));
        p.drawRect(QRectF(c.x() - b, c.y() - b, 2 * b, 2 * b));
        p.setPen(QColor("#3a3f48"));
        p.drawRect(0, 0, width() - 1, height() - 1);
    }

private:
    int pct_ = 100;
    int box_ = 9;
    QColor col_{"#cdd6e4"};
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
        p.fillRect(rect(), QColor("#34373d"));
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
        // After the user finishes resizing an unpinned side, fold it away.
        resizeTimer_ = new QTimer(this);
        resizeTimer_->setSingleShot(true);
        resizeTimer_->setInterval(3000);
        QObject::connect(resizeTimer_, &QTimer::timeout, this, [this] {
            if (!pinned_)
                for (auto* d : group()) d->collapse();
        });
    }

    // Provide the custom title bar + the real content widget.
    void setManaged(QWidget* titleBar, QWidget* content) {
        titleBar_ = titleBar;
        content_ = content;
        strip_ = new VerticalStrip(title_, this);
        // Hovering any strip on this side expands the whole side group.
        strip_->onEnter = [this] {
            if (pinned_) return;
            for (auto* d : group()) d->expandIfCollapsed();
        };
        strip_->hide();
        emptyBar_ = new QWidget(this);
        emptyBar_->setFixedHeight(0);
        setTitleBarWidget(titleBar_);
        setWidget(content_);
    }

    // Register the title bar's pin button so the group can keep them in sync.
    void setPinButton(QToolButton* b) { pinBtn_ = b; }
    // The docks pinned/auto-hidden together as one side (left or right).
    void setGroup(const QVector<AutoHideDock*>& peers) { peers_ = peers; }

    QMainWindow* mainWin() const { return mw_; }

    // Re-dock to the area nearest the given global point, or stay floating.
    void redockNear(const QPoint& global) {
        if (!mw_) return;

        // 1) Dropped onto an existing docked panel: snap into that column and push
        //    the existing panel down (drop on the top half -> above it; bottom half
        //    -> below it).
        for (QDockWidget* d : mw_->findChildren<QDockWidget*>()) {
            if (d == this || d->isFloating() || !d->isVisible()) continue;
            const QRect g(d->mapToGlobal(QPoint(0, 0)), d->size());
            if (!g.contains(global)) continue;
            const Qt::DockWidgetArea da = mw_->dockWidgetArea(d);
            if (da == Qt::NoDockWidgetArea) continue;
            const bool dropTopHalf = global.y() < g.center().y();
            setFloating(false);
            mw_->addDockWidget(da, this);
            // splitDockWidget(first, second, Vertical): first on top, second below.
            if (dropTopHalf)
                mw_->splitDockWidget(this, d, Qt::Vertical);  // new panel on top
            else
                mw_->splitDockWidget(d, this, Qt::Vertical);  // new panel below
            return;
        }

        // 2) Otherwise snap to the nearest main-window edge, or stay floating.
        const QRect area(mw_->mapToGlobal(QPoint(0, 0)), mw_->size());
        constexpr int kEdge = 70; // snap margin near a main-window edge
        if (!area.adjusted(-30, -30, 30, 30).contains(global)) return; // keep floating
        Qt::DockWidgetArea dwa = Qt::NoDockWidgetArea;
        const int dl = global.x() - area.left(), dr = area.right() - global.x();
        const int dt = global.y() - area.top(), db = area.bottom() - global.y();
        const int m = std::min(std::min(dl, dr), std::min(dt, db));
        if (m > kEdge) return; // dropped in the middle -> leave floating
        if (m == dl) dwa = Qt::LeftDockWidgetArea;
        else if (m == dr) dwa = Qt::RightDockWidgetArea;
        else if (m == dt) dwa = Qt::TopDockWidgetArea;
        else dwa = Qt::BottomDockWidgetArea;
        setFloating(false);
        mw_->addDockWidget(dwa, this);
    }

    bool pinned() const { return pinned_; }

    // Toggling the pin on one panel pins/unpins the whole side group.
    void setPinned(bool v) {
        for (auto* d : group()) {
            d->applyPinned(v);
            if (d->pinBtn_ && d->pinBtn_->isChecked() != v) {
                QSignalBlocker block(d->pinBtn_);
                d->pinBtn_->setChecked(v);
                d->pinBtn_->setIcon(makePinIcon(v));
            }
        }
    }

    void expandIfCollapsed() { if (collapsed_) expand(); }

private:
    QVector<AutoHideDock*> group() {
        return peers_.isEmpty() ? QVector<AutoHideDock*>{this} : peers_;
    }
    void applyPinned(bool v) {
        pinned_ = v;
        if (pinned_) { poll_->stop(); expand(); }
        else { poll_->start(); } // onPoll collapses once the cursor is away
    }

    bool horizontalArea() {
        const Qt::DockWidgetArea a = mw_->dockWidgetArea(this);
        return a != Qt::TopDockWidgetArea && a != Qt::BottomDockWidgetArea;
    }

    // Is the cursor over ANY panel (or its strip) on this side?
    bool groupInside() {
        const QPoint c = QCursor::pos();
        for (auto* d : group()) {
            if (d->isFloating()) continue;
            if (QRect(d->mapToGlobal(QPoint(0, 0)), d->size()).contains(c)) return true;
        }
        return false;
    }

    void onPoll() {
        if (pinned_ || isFloating()) return;
        // While a resize is pending, hold the panel open; it folds when the
        // resize timer fires (3 s after the user finishes resizing).
        if (resizeTimer_->isActive()) {
            outside_.invalidate();
            return;
        }
        if (groupInside()) {
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
        suppressResize_ = true; // ignore the programmatic resize below
        setTitleBarWidget(titleBar_);
        setWidget(content_);
        content_->show();
        setMinimumSize(0, 0);
        setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        if (expandedExtent_ > 0)
            mw_->resizeDocks({this}, {expandedExtent_},
                             horizontalExtent_ ? Qt::Horizontal : Qt::Vertical);
        outside_.invalidate();
        QTimer::singleShot(150, this, [this] { suppressResize_ = false; });
    }

    void collapse() {
        if (collapsed_ || isFloating()) return;
        horizontalExtent_ = horizontalArea();
        expandedExtent_ = horizontalExtent_ ? width() : height();
        collapsed_ = true;
        suppressResize_ = true;
        resizeTimer_->stop();
        setTitleBarWidget(emptyBar_);
        setWidget(strip_);
        strip_->show();
        if (horizontalExtent_)
            setFixedWidth(24);
        else
            setFixedHeight(24);
        QTimer::singleShot(150, this, [this] { suppressResize_ = false; });
    }

protected:
    // A manual resize of an unpinned panel re-arms a 3 s countdown; once the
    // user stops dragging, the side folds away.
    void resizeEvent(QResizeEvent* e) override {
        QDockWidget::resizeEvent(e);
        if (pinned_ || collapsed_ || isFloating() || suppressResize_) return;
        if (e->oldSize().isValid() && e->size().width() != e->oldSize().width()) {
            outside_.invalidate();
            resizeTimer_->start();
        }
    }

private:

    QMainWindow* mw_;
    QString title_;
    QWidget* titleBar_ = nullptr;
    QWidget* content_ = nullptr;
    VerticalStrip* strip_ = nullptr;
    QWidget* emptyBar_ = nullptr;
    QTimer* poll_ = nullptr;
    QTimer* resizeTimer_ = nullptr;    // fires 3s after the user finishes resizing
    QElapsedTimer outside_;
    bool pinned_ = true;
    bool collapsed_ = false;
    bool horizontalExtent_ = true;
    bool suppressResize_ = false;      // ignore our own (programmatic) resizes
    int expandedExtent_ = 0;
    QToolButton* pinBtn_ = nullptr;   // the title bar's pin toggle (for group sync)
    QVector<AutoHideDock*> peers_;     // docks on the same side (pinned as a unit)
};

// Custom panel title bar: a dots grip (the only drag handle), the title, a
// pin/auto-hide toggle, and a close button.
class DockTitleBar : public QWidget {
public:
    DockTitleBar(AutoHideDock* dock, const QString& title)
        : QWidget(dock), dock_(dock) {
        setStyleSheet(QStringLiteral("background:#34373d;"));
        auto* lay = new QHBoxLayout(this);
        lay->setContentsMargins(5, 2, 5, 2);
        lay->setSpacing(6);

        grip_ = new QLabel(this);
        grip_->setPixmap(makeGripPixmap());
        grip_->setToolTip(QStringLiteral("Drag to move / detach"));
        // Transparent to mouse so the press lands on the title bar (which owns the
        // drag), giving a reliable implicit grab for move/release.
        grip_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
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
        pin->setToolTip(QStringLiteral("Pin / auto-hide this side"));
        dock->setPinButton(pin);
        QObject::connect(pin, &QToolButton::toggled, dock, [pin, dock](bool on) {
            pin->setIcon(makePinIcon(on));
            pin->setToolTip(on ? QStringLiteral("Pinned — click to auto-hide this side")
                               : QStringLiteral("Auto-hide — click to pin this side"));
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
    // The grip is the only drag handle. Dragging is handled explicitly (Qt's
    // native dock drag does not work reliably through a custom title bar): a drag
    // floats the panel and moves it; dropping near a main-window edge re-docks it.
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && grip_->geometry().contains(e->pos())) {
            dragging_ = true;
            pressGlobal_ = e->globalPosition().toPoint();
        }
        e->accept(); // consume so the panel never drags from anywhere but the grip
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (dragging_ && (e->buttons() & Qt::LeftButton)) {
            const QPoint g = e->globalPosition().toPoint();
            if (!dock_->isFloating()) {
                if ((g - pressGlobal_).manhattanLength() < QApplication::startDragDistance()) {
                    e->accept();
                    return;
                }
                dock_->setFloating(true); // detach once the drag really starts
            }
            // Keep the grip roughly under the cursor (fixed offset is predictable
            // across the dock->float transition).
            dock_->move(g - QPoint(14, 12));
            e->accept();
            return;
        }
        // Show the move cursor while hovering the grip.
        setCursor(grip_->geometry().contains(e->pos()) ? Qt::SizeAllCursor : Qt::ArrowCursor);
        e->accept();
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (dragging_) {
            dragging_ = false;
            if (dock_->isFloating())
                dock_->redockNear(e->globalPosition().toPoint());
        }
        e->accept();
    }
    void mouseDoubleClickEvent(QMouseEvent* e) override {
        // Double-click the grip toggles floating (like AutoCAD palettes).
        if (grip_->geometry().contains(e->pos()))
            dock_->setFloating(!dock_->isFloating());
        e->accept();
    }

private:
    AutoHideDock* dock_;
    QLabel* grip_;
    bool dragging_ = false;
    QPoint pressGlobal_;
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
    connect(canvas_, &CanvasWidget::contextMenuRequested, this,
            [this](const QPoint& gp) { showCanvasMenu(gp); });
    connect(editor_, &Editor::message, this, [this](const QString& m) { statusBar()->showMessage(m); });
    connect(editor_, &Editor::changed, this, [this] {
        canvas_->update();
        refreshPanels();
        updateActiveTabText(); // reflect the dirty marker
    });
    connect(editor_, &Editor::selectionChanged, this, [this] { refreshPanels(); });
    connect(editor_, &Editor::toolChanged, this, [this](ToolId t) {
        if (QAction* a = toolActions_.value(static_cast<int>(t), nullptr)) a->setChecked(true);
        if (cmdHintLabel_) cmdHintLabel_->setText(editor_->drawCommandHint()); // applicable-commands help
        canvas_->setFocus(); // so typing immediately opens the dynamic-input field
    });
    connect(editor_, &Editor::textPromptRequested, this, [this](const QString& prompt) {
        bool ok = false;
        const QString text = QInputDialog::getText(this, QStringLiteral("Edraw"), prompt,
                                                   QLineEdit::Normal, QString(), &ok);
        editor_->provideText(ok ? text : QString());
    });

    // Restore saved drafting/snap preferences before the status toggles are built
    // so their initial checked state reflects the last session.
    loadSettings();

    buildRibbon();
    buildPanels();
    buildStatusControls();
    buildCommandBar();
    buildMenus();
    buildCommands();

    // Command autocomplete: suggest/filter commands starting with the typed letters
    // (in both the bottom command bar and the floating dynamic-input box).
    {
        QStringList words;
        for (const auto& c : commands_)
            for (const auto& n : c.names) words << n;
        words.removeDuplicates();
        words.sort(Qt::CaseInsensitive);
        auto* comp = new QCompleter(words, this);
        comp->setCaseSensitivity(Qt::CaseInsensitive);
        comp->setFilterMode(Qt::MatchStartsWith);
        comp->setCompletionMode(QCompleter::PopupCompletion);
        if (commandEdit_) commandEdit_->setCompleter(comp);
        canvas_->setCommandWords(words);
    }

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
    a->setData(static_cast<int>(id)); // for theme re-tinting
    toolGroup_->addAction(a);
    toolActions_.insert(static_cast<int>(id), a);
    allToolActions_.append(a);        // every instance (duplicates across tabs too)
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
    lbl->setStyleSheet(QStringLiteral("color:#55524c; font-size:10px; background:transparent;"));
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
        QAction* act = makeToolAction(toolLabel(id), id);
        b->setDefaultAction(act);
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setIconSize(QSize(24, 24)); // ~10% larger ribbon buttons
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus); // keep keyboard focus on the canvas

        // Circle / Arc / Ellipse get AutoCAD-style flyouts of construction methods,
        // each option shown with a (large) visual icon. Choosing an option also makes
        // it the button's primary icon for the rest of the session.
        if (id == ToolId::Circle) {
            auto* menu = new QMenu(b);
            auto add = [&](const QString& label, Editor::CircleMode m) {
                menu->addAction(makeCircleMethodIcon(static_cast<int>(m)), label, this, [this, act, m] {
                    editor_->setCircleMode(m);
                    editor_->setTool(ToolId::Circle);
                    act->setIcon(makeCircleMethodIcon(static_cast<int>(m))); // primary icon
                });
            };
            add(QStringLiteral("Center, Radius"), Editor::CircleMode::CenterRadius);
            add(QStringLiteral("Center, Diameter"), Editor::CircleMode::CenterDiameter);
            add(QStringLiteral("2-Point"), Editor::CircleMode::TwoPoint);
            add(QStringLiteral("3-Point"), Editor::CircleMode::ThreePoint);
            menu->addSeparator();
            add(QStringLiteral("Tan, Tan, Radius"), Editor::CircleMode::TanTanRadius);
            add(QStringLiteral("Tan, Tan, Tan"), Editor::CircleMode::TanTanTan);
            act->setIcon(makeCircleMethodIcon(0)); // default = first fall-menu method
            b->setMenu(menu);
            b->setPopupMode(QToolButton::MenuButtonPopup);
        } else if (id == ToolId::Arc) {
            auto* menu = new QMenu(b);
            auto add = [&](const QString& label, Editor::ArcMode m) {
                menu->addAction(makeArcMethodIcon(static_cast<int>(m)), label, this, [this, act, m] {
                    editor_->setArcMode(m);
                    editor_->setTool(ToolId::Arc);
                    act->setIcon(makeArcMethodIcon(static_cast<int>(m)));
                });
            };
            add(QStringLiteral("3-Point"), Editor::ArcMode::ThreePoint);
            add(QStringLiteral("Start, Center, End"), Editor::ArcMode::StartCenterEnd);
            add(QStringLiteral("Center, Start, End"), Editor::ArcMode::CenterStartEnd);
            add(QStringLiteral("Start, End, Radius"), Editor::ArcMode::StartEndRadius);
            act->setIcon(makeArcMethodIcon(0)); // default = first fall-menu method
            b->setMenu(menu);
            b->setPopupMode(QToolButton::MenuButtonPopup);
        } else if (id == ToolId::Ellipse) {
            auto* menu = new QMenu(b);
            auto add = [&](const QString& label, Editor::EllipseMode m) {
                menu->addAction(makeEllipseMethodIcon(static_cast<int>(m)), label, this, [this, act, m] {
                    editor_->setEllipseMode(m);
                    editor_->setTool(ToolId::Ellipse);
                    act->setIcon(makeEllipseMethodIcon(static_cast<int>(m)));
                });
            };
            add(QStringLiteral("Center"), Editor::EllipseMode::Center);
            add(QStringLiteral("Axis, End"), Editor::EllipseMode::AxisEnd);
            add(QStringLiteral("Elliptical Arc"), Editor::EllipseMode::EllipticalArc);
            act->setIcon(makeEllipseMethodIcon(0)); // default = first fall-menu method
            b->setMenu(menu);
            b->setPopupMode(QToolButton::MenuButtonPopup);
        } else if (id == ToolId::Spline) {
            auto* menu = new QMenu(b);
            auto add = [&](const QString& label, Editor::SplineMode m) {
                menu->addAction(makeSplineMethodIcon(static_cast<int>(m)), label, this, [this, act, m] {
                    editor_->setSplineMode(m);
                    editor_->setTool(ToolId::Spline);
                    act->setIcon(makeSplineMethodIcon(static_cast<int>(m)));
                });
            };
            add(QStringLiteral("Spline Fit"), Editor::SplineMode::Fit);
            add(QStringLiteral("Spline CV (Control Vertices)"), Editor::SplineMode::CV);
            act->setIcon(makeSplineMethodIcon(0)); // default = Fit
            b->setMenu(menu);
            b->setPopupMode(QToolButton::MenuButtonPopup);
        }

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
    ribbon_->setFixedHeight(114); // taller to fit the ~10% larger buttons
    // AutoCAD-style charcoal-gray ribbon, scoped to the ribbon and its children.
    // Use background-color (not the `background` shorthand) so it reliably wins
    // over the application stylesheet's QWidget{background-color:...} rule.
    ribbon_->setStyleSheet(QStringLiteral(
        "QTabWidget::pane{background-color:#3a3d43;border:none;border-bottom:1px solid #2a2c31;}"
        "QWidget{background-color:#3a3d43;color:#c9ccd1;}"
        "QTabBar{background-color:#2f3237;}"
        "QTabBar::tab{background-color:transparent;color:#c2c5ca;padding:5px 14px;border:none;}"
        "QTabBar::tab:hover{color:#ffffff;}"
        "QTabBar::tab:selected{color:#ffffff;background-color:#3a3d43;border-bottom:2px solid #3d8bfd;}"
        "QLabel{background-color:transparent;color:#9da3ac;}"
        "QToolButton{background-color:transparent;border:1px solid transparent;border-radius:4px;padding:3px 6px;}"
        "QToolButton:hover{background-color:#484c53;border-color:#5a5f68;}"
        "QToolButton:checked{background-color:#294e74;border-color:#3d8bfd;}"
        "QToolButton:pressed{background-color:#22384f;}"
        "QFrame{color:#2a2c31;}"
        "QComboBox{background-color:#45484f;color:#e0e2e6;border:1px solid #585c64;border-radius:2px;padding:1px 4px;}"
        "QComboBox:hover{border-color:#3d8bfd;}"
        "QComboBox QAbstractItemView{background-color:#3a3d43;color:#e0e2e6;selection-background-color:#294e74;selection-color:#ffffff;}"));

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
        rule->setStyleSheet(QStringLiteral("color:#b3afa6;"));
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
            const QString hex = map.value(colorCombo_->currentText(), QString());
            editor_->setCurrentColor(hex);     // applies to newly drawn entities
            editor_->setSelectionColor(hex);   // and recolors any current selection
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

    // Build an icon toggle (AutoCAD status-bar style). Returns the button; the
    // caller groups it into a field via addField().
    auto makeToggle = [&](const QString& icon, const QString& tip, bool initial,
                          auto setter) -> QToolButton* {
        auto* b = new QToolButton(this);
        b->setIcon(makeStatusIcon(icon));
        b->setIconSize(QSize(22, 22)); // ~10% larger status buttons
        b->setToolTip(tip);
        b->setCheckable(true);
        b->setChecked(initial);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setStyleSheet(toggleQss);
        connect(b, &QToolButton::toggled, this, setter);
        statusToggleBtns_.append(b);
        statusToggleNames_.append(icon); // icon name, for theme re-tinting
        return b;
    };

    // A separator line between fields (long) or inside a field (short).
    auto sepLine = [this](int height, const QString& color) {
        auto* f = new QFrame(this);
        f->setFrameShape(QFrame::VLine);
        f->setFrameShadow(QFrame::Plain);
        f->setFixedHeight(height);
        f->setStyleSheet(QStringLiteral("color:%1;").arg(color));
        return f;
    };

    // A status "field" is a single permanent widget, so the status bar's built-in
    // divider falls only BETWEEN fields (not between a toggle and its fall-menu).
    // The toggle and its arrow are joined inside the field by a short inner line.
    auto addField = [&](QToolButton* toggle, QToolButton* arrow) {
        auto* field = new QWidget(this);
        auto* hl = new QHBoxLayout(field);
        hl->setContentsMargins(3, 0, 3, 0);
        hl->setSpacing(2);
        hl->addWidget(toggle);
        if (arrow) {
            hl->addWidget(sepLine(12, QStringLiteral("#5a5f68"))); // short: button | fall-menu
            hl->addWidget(arrow);
        }
        statusBar()->addPermanentWidget(field);
    };

    // Grid size field.
    auto* gridLbl = new QLabel(QStringLiteral("Grid"), this);
    gridLbl->setStyleSheet(QStringLiteral("font-size:11px;color:#9aa7b8;"));
    auto* gridSpin = new QDoubleSpinBox(this);
    gridSpin->setRange(0.001, 100000.0);
    gridSpin->setDecimals(2);
    gridSpin->setValue(canvas_->gridSize());
    gridSpin->setMaximumWidth(74);
    gridSpin->setButtonSymbols(QAbstractSpinBox::NoButtons); // input-only, no +/- spin
    gridSpin->setFocusPolicy(Qt::ClickFocus);
    gridSpin->setStyleSheet(QStringLiteral("QDoubleSpinBox{font-size:11px;}"));
    connect(gridSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) {
                canvas_->setGridSize(v);
                editor_->snapSettings().gridSize = v;
            });
    statusBar()->addPermanentWidget(gridLbl);
    statusBar()->addPermanentWidget(gridSpin);

    // Build a "fall menu" arrow button for a field (does not add it to the bar;
    // addField joins it to its toggle).
    auto makeArrow = [&](QMenu* menu, const QString& tip) -> QToolButton* {
        auto* a = new QToolButton(this);
        a->setText(QString(QChar(0x25BE))); // ▾
        a->setAutoRaise(true);
        a->setFocusPolicy(Qt::NoFocus);
        a->setToolTip(tip);
        a->setStyleSheet(QStringLiteral(
            "QToolButton{padding:0 2px;border:none;font-size:9px;color:#aab0b8;}"
            "QToolButton:hover{background:#3d4148;}"));
        connect(a, &QToolButton::clicked, this, [a, menu] {
            menu->popup(a->mapToGlobal(QPoint(0, -menu->sizeHint().height())));
        });
        statusArrowBtns_.append(a);
        return a;
    };

    addField(makeToggle(QStringLiteral("grid"), QStringLiteral("Show grid"),
                        canvas_->gridVisible(),
                        [this](bool v) { canvas_->setGridVisible(v); }),
             nullptr);
    addField(makeToggle(QStringLiteral("gridsnap"), QStringLiteral("Snap to grid"),
                        editor_->snapSettings().gridSnap,
                        [this](bool v) { editor_->snapSettings().gridSnap = v; }),
             nullptr);
    addField(makeToggle(QStringLiteral("ortho"), QStringLiteral("Ortho mode"),
                        editor_->snapSettings().ortho,
                        [this](bool v) { editor_->snapSettings().ortho = v; }),
             nullptr);

    // Polar tracking + a "fall menu" of common angle increments (joined arrow).
    {
        auto* toggle = makeToggle(QStringLiteral("polar"), QStringLiteral("Polar tracking"),
                                  editor_->snapSettings().polar,
                                  [this](bool v) { editor_->snapSettings().polar = v; });
        auto* menu = new QMenu(this);
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
        addField(toggle, makeArrow(menu, QStringLiteral("Polar angle increments")));
    }

    // Object snap + a "fall menu" to pick which snap types are active.
    {
        auto* toggle = makeToggle(QStringLiteral("osnap"), QStringLiteral("Object snap"),
                                  editor_->snapSettings().objectSnap,
                                  [this](bool v) { editor_->snapSettings().objectSnap = v; });
        auto* menu = new QMenu(this);
        auto addSnap = [&](const QString& label, const QString& kind, bool SnapSettings::*field) {
            auto* act = menu->addAction(makeOsnapMarkerIcon(kind), label);
            act->setCheckable(true);
            act->setChecked(editor_->snapSettings().*field);
            connect(act, &QAction::toggled, this, [this, field](bool on) {
                editor_->snapSettings().*field = on;
            });
        };
        addSnap(QStringLiteral("Endpoint"), QStringLiteral("endpoint"), &SnapSettings::snapEndpoint);
        addSnap(QStringLiteral("Midpoint"), QStringLiteral("midpoint"), &SnapSettings::snapMidpoint);
        addSnap(QStringLiteral("Center"), QStringLiteral("center"), &SnapSettings::snapCenter);
        addSnap(QStringLiteral("Intersection"), QStringLiteral("intersection"), &SnapSettings::snapIntersection);
        addSnap(QStringLiteral("Nearest"), QStringLiteral("nearest"), &SnapSettings::snapNearest);
        menu->addSeparator();
        connect(menu->addAction(QStringLiteral("Object Snap Settings...")), &QAction::triggered,
                this, [this] { showOptions(); });
        addField(toggle, makeArrow(menu, QStringLiteral("Object snap types")));
    }

    addField(makeToggle(QStringLiteral("angles"), QStringLiteral("Show drawing angles"),
                        editor_->showAngles(),
                        [this](bool v) { editor_->setShowAngles(v); }),
             nullptr);
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
    layersTable_->verticalHeader()->setVisible(false);
    // Only Name stretches; Status / On / Lock / Color keep fixed widths so they
    // don't grow with the panel width.
    {
        auto* hh = layersTable_->horizontalHeader();
        hh->setStretchLastSection(false);
        hh->setSectionResizeMode(0, QHeaderView::Fixed);   // Status
        hh->setSectionResizeMode(1, QHeaderView::Stretch);  // Name
        hh->setSectionResizeMode(2, QHeaderView::Fixed);   // On
        hh->setSectionResizeMode(3, QHeaderView::Fixed);   // Lock
        hh->setSectionResizeMode(4, QHeaderView::Fixed);   // Color
    }
    layersTable_->setColumnWidth(0, 50);
    layersTable_->setColumnWidth(2, 42);
    layersTable_->setColumnWidth(3, 46);
    layersTable_->setColumnWidth(4, 58);
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

    // Pin / auto-hide acts on a whole side: the left column (Properties + Layers)
    // and the right column (Entities) each pin and fold as one unit.
    const QVector<AutoHideDock*> leftGroup{propsDock, layersDock};
    propsDock->setGroup(leftGroup);
    layersDock->setGroup(leftGroup);
    const QVector<AutoHideDock*> rightGroup{listDock};
    listDock->setGroup(rightGroup);
}

void MainWindow::buildCommandBar() {
    auto* bar = new QToolBar(QStringLiteral("Command"), this);
    bar->setMovable(false);
    // Applicable-commands help for the active draw tool (left of the command box).
    cmdHintLabel_ = new QLabel(bar);
    cmdHintLabel_->setStyleSheet(QStringLiteral("color:#8fb0d6;padding:0 10px;font-weight:600;"));
    auto* label = new QLabel(QStringLiteral("  Command: "), bar);
    commandEdit_ = new QLineEdit(bar);
    commandEdit_->setPlaceholderText(QStringLiteral("command (line, rect, circle...) — or, while drawing, a distance / x,y / @dx,dy + Enter"));
    bar->addWidget(cmdHintLabel_);
    bar->addWidget(label);
    bar->addWidget(commandEdit_);
    addToolBar(Qt::BottomToolBarArea, bar);

    connect(commandEdit_, &QLineEdit::returnPressed, this, [this] {
        const QString t = commandEdit_->text().trimmed();
        commandEdit_->clear();
        submitCommand(t);
        // Always hand focus back to the canvas so the next keystroke opens the
        // FLYING input field at the cursor (AutoCAD dynamic input) rather than
        // staying in the bottom command bar — typing during a draw must not jump
        // the input down to the command line.
        canvas_->setFocus();
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
    fileMenu->addAction(QStringLiteral("&Options..."), this, [this] { showOptions(); });
    fileMenu->addAction(QStringLiteral("&About Edraw..."), this, [this] { showAbout(); });
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("E&xit"), QKeySequence::Quit, qApp, &QApplication::quit);

    auto* logo = new QToolButton(this);
    logo->setIcon(QIcon(QStringLiteral(":/edraw.ico"))); // the real Edraw logo
    logo->setIconSize(QSize(26, 26));
    logo->setToolTip(QStringLiteral("Edraw — file menu"));
    logo->setMenu(fileMenu);
    logo->setPopupMode(QToolButton::InstantPopup);
    logo->setAutoRaise(true);
    logo->setStyleSheet(QStringLiteral(
        "QToolButton{background:transparent;border:none;padding:1px 6px;margin:3px 6px;}"
        "QToolButton:hover{background:rgba(255,255,255,28);border-radius:4px;}"
        "QToolButton::menu-indicator{image:none;width:0;}"));
    menuBar()->setCornerWidget(logo, Qt::TopLeftCorner);

    // Dark-gray menu bar + menus (AutoCAD-style chrome), applied directly so it
    // does not depend on the qss resource.
    menuBar()->setStyleSheet(QStringLiteral(
        "QMenuBar{background-color:#2d2f34;color:#c9ccd1;}"
        "QMenuBar::item{background-color:transparent;padding:4px 9px;}"
        "QMenuBar::item:selected{background-color:#3d4148;color:#ffffff;}"
        "QMenu{background-color:#33363c;color:#c9ccd1;border:1px solid #4a4e55;}"
        "QMenu::item{padding:4px 22px;}"
        "QMenu::item:selected{background-color:#294e74;color:#ffffff;}"
        "QMenu::separator{height:1px;background-color:#4a4e55;margin:3px 6px;}"));

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

// ---- Theme (Dark / Light) --------------------------------------------------

void MainWindow::applyTheme(bool dark) {
    darkTheme_ = dark;

    // Palette tokens for the application stylesheet (panels / tables / inputs).
    // The status bar + bottom command toolbar stay dark in both schemes (their
    // icons are light), matching AutoCAD's persistent dark status strip.
    QHash<QString, QString> t;
    if (dark) {
        t = {{"WIN", "#3a3d43"},   {"TEXT", "#c9ccd1"},   {"PANEL", "#3a3d43"},
             {"ALT", "#41454c"},   {"HEADER", "#313438"}, {"LINE", "#4a4e55"},
             {"SEL", "#294e74"},   {"SELTX", "#ffffff"},  {"INPUT", "#45484f"},
             {"INBORD", "#585c64"},{"BTN", "#484c53"},    {"HOVER", "#525760"},
             {"SCROLL", "#2d2f34"},{"HANDLE", "#5a5f68"}, {"TABBAR", "#2f3237"},
             {"STBG", "#2d2f34"},  {"STTX", "#aab0b8"}};
    } else {
        t = {{"WIN", "#e9e9ed"},   {"TEXT", "#1e1e1e"},   {"PANEL", "#f5f5f7"},
             {"ALT", "#ececf0"},   {"HEADER", "#dcdce0"}, {"LINE", "#c2c2c8"},
             {"SEL", "#cfe3ff"},   {"SELTX", "#000000"},  {"INPUT", "#ffffff"},
             {"INBORD", "#aeaeb6"},{"BTN", "#e2e2e6"},    {"HOVER", "#d4d4da"},
             {"SCROLL", "#dedee2"},{"HANDLE", "#b4b4ba"}, {"TABBAR", "#dcdce0"},
             {"STBG", "#dcdce0"},  {"STTX", "#33373f"}};
    }
    QString app = QStringLiteral(
        "QMainWindow,QWidget{background-color:{WIN};color:{TEXT};font-size:12px;}"
        "QStatusBar{background-color:{STBG};color:{STTX};border-top:1px solid {LINE};}"
        "QToolBar{background-color:{STBG};border:none;spacing:2px;}"
        "QTabWidget::pane{background-color:{WIN};border:none;border-bottom:1px solid {LINE};}"
        "QTabBar{background:{TABBAR};}"
        "QTabBar::tab{background:transparent;color:{TEXT};padding:5px 14px;border:none;}"
        "QTabBar::tab:selected{color:{TEXT};border-bottom:2px solid #3d8bfd;}"
        "QTableWidget,QListWidget{background-color:{PANEL};alternate-background-color:{ALT};"
        "color:{TEXT};gridline-color:{LINE};selection-background-color:{SEL};"
        "selection-color:{SELTX};border:1px solid {LINE};}"
        "QHeaderView::section{background-color:{HEADER};color:{TEXT};padding:3px 6px;border:none;"
        "border-right:1px solid {LINE};border-bottom:1px solid {LINE};}"
        "QComboBox,QLineEdit,QSpinBox,QDoubleSpinBox{background-color:{INPUT};color:{TEXT};"
        "border:1px solid {INBORD};border-radius:3px;padding:2px 4px;}"
        "QAbstractSpinBox::up-button{subcontrol-origin:border;subcontrol-position:top right;"
        "width:18px;border-left:1px solid {INBORD};}"
        "QAbstractSpinBox::down-button{subcontrol-origin:border;subcontrol-position:bottom right;"
        "width:18px;border-left:1px solid {INBORD};}"
        "QAbstractSpinBox::up-button:hover,QAbstractSpinBox::down-button:hover{background:#3d8bfd;}"
        "QComboBox QAbstractItemView{background:{PANEL};color:{TEXT};selection-background-color:{SEL};}"
        "QPushButton{background-color:{BTN};color:{TEXT};border:1px solid {INBORD};"
        "border-radius:3px;padding:3px 10px;}"
        "QPushButton:hover{background-color:{HOVER};}"
        "QGroupBox{border:1px solid {LINE};border-radius:4px;margin-top:8px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:8px;padding:0 3px;color:{TEXT};}"
        "QCheckBox,QRadioButton,QLabel{background:transparent;color:{TEXT};}"
        "QScrollBar:vertical{background:{SCROLL};width:10px;}"
        "QScrollBar::handle:vertical{background:{HANDLE};border-radius:5px;min-height:20px;}"
        "QScrollBar:horizontal{background:{SCROLL};height:10px;}"
        "QScrollBar::handle:horizontal{background:{HANDLE};border-radius:5px;min-width:20px;}"
        "QScrollBar::add-line,QScrollBar::sub-line{width:0;height:0;}");
    for (auto it = t.cbegin(); it != t.cend(); ++it)
        app.replace(QStringLiteral("{%1}").arg(it.key()), it.value());
    qApp->setStyleSheet(app);

    // Menu bar + menus.
    const QString menuBg = dark ? "#2d2f34" : "#e4e4e8";
    const QString menuTx = dark ? "#c9ccd1" : "#1e1e1e";
    const QString menuPop = dark ? "#33363c" : "#f3f3f5";
    const QString menuBord = dark ? "#4a4e55" : "#b8b8be";
    menuBar()->setStyleSheet(
        QStringLiteral("QMenuBar{background-color:%1;color:%2;}"
                       "QMenuBar::item{background:transparent;padding:4px 9px;}"
                       "QMenuBar::item:selected{background-color:#cfe3ff;color:#000;}"
                       "QMenu{background-color:%3;color:%2;border:1px solid %4;}"
                       "QMenu::item{padding:4px 22px;}"
                       "QMenu::item:selected{background-color:#cfe3ff;color:#000;}"
                       "QMenu::separator{height:1px;background:%4;margin:3px 6px;}")
            .arg(menuBg, menuTx, menuPop, menuBord));

    // Ribbon.
    const QString rbBg = dark ? "#3a3d43" : "#e4e4e8";
    const QString rbTab = dark ? "#2f3237" : "#d6d6da";
    const QString rbTx = dark ? "#c9ccd1" : "#1e1e1e";
    const QString rbLabel = dark ? "#9da3ac" : "#55555c";
    const QString rbLine = dark ? "#2a2c31" : "#c2c2c8";
    const QString rbHover = dark ? "#484c53" : "#d0e3ff";
    const QString rbCombo = dark ? "#45484f" : "#ffffff";
    if (ribbon_)
        ribbon_->setStyleSheet(
            QStringLiteral(
                "QTabWidget::pane{background-color:%1;border:none;border-bottom:1px solid %5;}"
                "QWidget{background-color:%1;color:%3;}"
                "QTabBar{background-color:%2;}"
                "QTabBar::tab{background-color:transparent;color:%3;padding:5px 14px;border:none;}"
                "QTabBar::tab:selected{color:%3;background-color:%1;border-bottom:2px solid #3d8bfd;}"
                "QLabel{background-color:transparent;color:%4;}"
                "QToolButton{background-color:transparent;border:1px solid transparent;"
                "border-radius:4px;padding:3px 6px;}"
                "QToolButton:hover{background-color:%6;border-color:#9cc4f0;}"
                "QToolButton:checked{background-color:#294e74;border-color:#3d8bfd;}"
                "QFrame{color:%5;}"
                "QComboBox{background-color:%7;color:%3;border:1px solid %5;border-radius:2px;}"
                "QComboBox QAbstractItemView{background-color:%7;color:%3;}")
                .arg(rbBg, rbTab, rbTx, rbLabel, rbLine, rbHover, rbCombo));

    // Ribbon tool icons: light on a dark ribbon, dark on a light ribbon. Retint
    // EVERY tool button instance (a ToolId can appear on several tabs, so the
    // ToolId-keyed map alone would miss the duplicates).
    const QColor ink = dark ? QColor("#d6dae1") : QColor("#33373f");
    for (QAction* a : allToolActions_)
        a->setIcon(makeToolIcon(static_cast<ToolId>(a->data().toInt()), ink));

    // Status-bar drafting toggles: re-tint the icons and re-style hover/checked so
    // they read on the (now theme-coloured) status bar.
    const QColor stInk = dark ? QColor("#cfd8e6") : QColor("#33373f");
    for (int i = 0; i < statusToggleBtns_.size(); ++i)
        statusToggleBtns_[i]->setIcon(makeStatusIcon(statusToggleNames_[i], stInk));
    const QString tgl =
        dark ? QStringLiteral("QToolButton{padding:2px;border:none;}"
                              "QToolButton:hover{background:#3d4148;}"
                              "QToolButton:checked{background:#294e74;}"
                              "QToolButton::menu-button{width:12px;border:none;}")
             : QStringLiteral("QToolButton{padding:2px;border:none;color:#33373f;}"
                              "QToolButton:hover{background:#c8dcff;}"
                              "QToolButton:checked{background:#bcd9fb;}"
                              "QToolButton::menu-button{width:12px;border:none;}");
    for (QToolButton* b : statusToggleBtns_) b->setStyleSheet(tgl);
    const QString arrowSS =
        dark ? QStringLiteral("QToolButton{padding:0 2px;border:none;font-size:9px;color:#aab0b8;}"
                              "QToolButton:hover{background:#3d4148;}")
             : QStringLiteral("QToolButton{padding:0 2px;border:none;font-size:9px;color:#44474e;}"
                              "QToolButton:hover{background:#c8dcff;}");
    for (QToolButton* a : statusArrowBtns_) a->setStyleSheet(arrowSS);

    // The drawing canvas (model space) intentionally stays dark in both schemes,
    // like AutoCAD's model space; only the surrounding chrome changes.
}

// ---- About dialog ----------------------------------------------------------

void MainWindow::showCanvasMenu(const QPoint& globalPos) {
    if (!editor_) return;
    QMenu menu(this);
    const bool hasSel = !editor_->selection().empty();

    QAction* copy = menu.addAction(QStringLiteral("Copy"));
    copy->setEnabled(hasSel);
    connect(copy, &QAction::triggered, this, [this] { editor_->copySelection(); });

    QAction* cut = menu.addAction(QStringLiteral("Cut"));
    cut->setEnabled(hasSel);
    connect(cut, &QAction::triggered, this, [this] { editor_->copyWithBase(true); });

    QAction* cancel = menu.addAction(QStringLiteral("Cancel"));
    connect(cancel, &QAction::triggered, this,
            [this] { editor_->onKey(Qt::Key_Escape, Qt::NoModifier); });

    // Mirror appears only while an arc is being drawn (flip its sweep).
    if (editor_->isDrafting() && editor_->tool() == ToolId::Arc) {
        QAction* mirror = menu.addAction(QStringLiteral("Mirror arc"));
        mirror->setCheckable(true);
        mirror->setChecked(editor_->isArcMirrored());
        connect(mirror, &QAction::triggered, this, [this] { editor_->toggleArcMirror(); });
    }

    menu.addSeparator();
    connect(menu.addAction(QStringLiteral("Find...")), &QAction::triggered, this,
            [this] { findText(); });
    connect(menu.addAction(QStringLiteral("Drawing center (zoom to fit)")), &QAction::triggered,
            this, [this] { canvas_->fitView(); canvas_->setFocus(); });

    menu.exec(globalPos);
}

void MainWindow::findText() {
    if (!editor_ || !doc_) return;
    bool ok = false;
    const QString q = QInputDialog::getText(this, QStringLiteral("Find"),
                                            QStringLiteral("Find text:"), QLineEdit::Normal,
                                            QString(), &ok);
    if (!ok || q.isEmpty()) return;
    // Select every Text entity whose content contains the query (case-insensitive).
    editor_->clearSelection();
    int count = 0;
    for (const auto& e : doc_->entities()) {
        if (e.type != EntityType::Text) continue;
        if (e.meta.value(QStringLiteral("text")).toString().contains(q, Qt::CaseInsensitive)) {
            editor_->selectById(e.id, /*additive=*/true);
            ++count;
        }
    }
    statusBar()->showMessage(count ? QStringLiteral("Found %1 matching text object(s)").arg(count)
                                   : QStringLiteral("No text matching \"%1\"").arg(q),
                             3000);
    canvas_->update();
    canvas_->setFocus();
}

void MainWindow::showAbout() {
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("About Edraw"));
    dlg.setFixedWidth(380);
    auto* row = new QHBoxLayout(&dlg);
    row->setContentsMargins(20, 18, 20, 16);
    row->setSpacing(16);

    // Logo: the red square + white "E".
    auto* logo = new QLabel(&dlg);
    logo->setPixmap(QIcon(QStringLiteral(":/edraw.ico")).pixmap(64, 64));
    logo->setAlignment(Qt::AlignTop);
    row->addWidget(logo);

    auto* col = new QVBoxLayout();
    col->setSpacing(4);
    auto* name = new QLabel(QStringLiteral("Edraw"), &dlg);
    name->setStyleSheet(QStringLiteral("font-size:22px;font-weight:700;"));
    auto* ver = new QLabel(QStringLiteral("Version %1").arg(QApplication::applicationVersion()),
                           &dlg);
    ver->setStyleSheet(QStringLiteral("color:#9aa7b8;"));
    auto* desc = new QLabel(
        QStringLiteral("CAD workspace for designing 2D wire-harness drawings.\n\n© ETS"),
        &dlg);
    desc->setWordWrap(true);
    col->addWidget(name);
    col->addWidget(ver);
    col->addSpacing(6);
    col->addWidget(desc);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    col->addSpacing(8);
    col->addWidget(bb);
    row->addLayout(col, 1);

    dlg.exec();
}

// ---- Options dialog --------------------------------------------------------

void MainWindow::showOptions() {
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Options"));
    dlg.resize(560, 520);
    auto* outer = new QVBoxLayout(&dlg);
    auto* tabs = new QTabWidget(&dlg);
    outer->addWidget(tabs, 1);

    SnapSettings& s = editor_->snapSettings();

    // ===== Display =====
    auto* disp = new QWidget(tabs);
    auto* df = new QFormLayout(disp);

    // Wrap a slider with tick marks and a live numeric readout that updates as
    // the handle moves over each step.
    auto sliderRow = [disp](QSlider* sl, const QString& suffix) -> QWidget* {
        const int span = sl->maximum() - sl->minimum();
        sl->setTickPosition(QSlider::TicksBelow);
        sl->setTickInterval(span >= 10 ? span / 10 : 1);
        sl->setSingleStep(1);
        sl->setPageStep(span >= 10 ? span / 10 : 1);
        auto* w = new QWidget(disp);
        auto* hl = new QHBoxLayout(w);
        hl->setContentsMargins(0, 0, 0, 0);
        auto* val = new QLabel(QString::number(sl->value()) + suffix, w);
        val->setMinimumWidth(46);
        val->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        hl->addWidget(sl, 1);
        hl->addWidget(val);
        QObject::connect(sl, &QSlider::valueChanged, val,
                         [val, suffix](int v) { val->setText(QString::number(v) + suffix); });
        return w;
    };

    auto* scheme = new QComboBox(disp);
    scheme->addItems({QStringLiteral("Dark"), QStringLiteral("Light")});
    scheme->setCurrentIndex(darkTheme_ ? 0 : 1);
    df->addRow(QStringLiteral("Color scheme"), scheme);

    auto* crossSize = new QSlider(Qt::Horizontal, disp);
    crossSize->setRange(5, 100);
    crossSize->setValue(canvas_->crosshairSize());
    df->addRow(QStringLiteral("Crosshair size (%)"), sliderRow(crossSize, QStringLiteral("%")));

    auto* pickBox = new QSlider(Qt::Horizontal, disp);
    pickBox->setRange(2, 20);
    pickBox->setValue(canvas_->pickboxSize());
    df->addRow(QStringLiteral("Crosshair box size"), sliderRow(pickBox, QStringLiteral(" px")));

    QColor crossCol = canvas_->crosshairColor();
    bool crossChanged = false;

    // Live preview of the crosshair as size / box / colour are adjusted.
    auto* preview = new CrosshairPreview(disp);
    df->addRow(QStringLiteral("Preview"), preview);
    auto refreshPrev = [preview, crossSize, pickBox, &crossCol]() {
        preview->setParams(crossSize->value(), pickBox->value(), crossCol);
    };
    refreshPrev();
    connect(crossSize, &QSlider::valueChanged, preview, [refreshPrev](int) { refreshPrev(); });
    connect(pickBox, &QSlider::valueChanged, preview, [refreshPrev](int) { refreshPrev(); });

    auto* crossColBtn = new QPushButton(QStringLiteral("Crosshair color..."), disp);
    df->addRow(QStringLiteral("Crosshair color"), crossColBtn);
    connect(crossColBtn, &QPushButton::clicked, &dlg, [&dlg, &crossCol, &crossChanged, refreshPrev] {
        const QColor c = QColorDialog::getColor(crossCol, &dlg, QStringLiteral("Crosshair color"));
        if (c.isValid()) { crossCol = c; crossChanged = true; refreshPrev(); }
    });

    QColor bgCol = canvas_->backgroundColor();
    bool bgChanged = false;
    auto* bgBtn = new QPushButton(QStringLiteral("Background color..."), disp);
    df->addRow(QStringLiteral("Canvas background"), bgBtn);
    connect(bgBtn, &QPushButton::clicked, &dlg, [&dlg, &bgCol, &bgChanged] {
        const QColor c = QColorDialog::getColor(bgCol, &dlg, QStringLiteral("Canvas background"));
        if (c.isValid()) { bgCol = c; bgChanged = true; }
    });

    // Grid + secondary-grid colours, each adjustable independently.
    QColor gridCol = canvas_->gridMinorColor();
    QColor gridCol2 = canvas_->gridMajorColor();
    bool gridChanged = false;
    auto* gridBtn = new QPushButton(QStringLiteral("Grid color..."), disp);
    df->addRow(QStringLiteral("Grid color"), gridBtn);
    connect(gridBtn, &QPushButton::clicked, &dlg, [&dlg, &gridCol, &gridChanged] {
        const QColor c = QColorDialog::getColor(gridCol, &dlg, QStringLiteral("Grid color"));
        if (c.isValid()) { gridCol = c; gridChanged = true; }
    });
    auto* grid2Btn = new QPushButton(QStringLiteral("Secondary grid color..."), disp);
    df->addRow(QStringLiteral("Secondary grid color"), grid2Btn);
    connect(grid2Btn, &QPushButton::clicked, &dlg, [&dlg, &gridCol2, &gridChanged] {
        const QColor c = QColorDialog::getColor(gridCol2, &dlg, QStringLiteral("Secondary grid color"));
        if (c.isValid()) { gridCol2 = c; gridChanged = true; }
    });

    auto* showGrid = new QCheckBox(QStringLiteral("Show grid"), disp);
    showGrid->setChecked(canvas_->gridVisible());
    df->addRow(QString(), showGrid);

    auto* gridStep = new QDoubleSpinBox(disp);
    gridStep->setRange(0.01, 100000.0);
    gridStep->setDecimals(2);
    gridStep->setValue(canvas_->gridSize());
    gridStep->setButtonSymbols(QAbstractSpinBox::PlusMinus); // clear +/- (both work)
    df->addRow(QStringLiteral("Grid size"), gridStep);
    tabs->addTab(disp, QStringLiteral("Display"));

    // ===== Drafting (snapping) =====
    auto* draft = new QWidget(tabs);
    auto* gf = new QFormLayout(draft);
    auto* snapGrid = new QCheckBox(QStringLiteral("Snap to grid"), draft);
    snapGrid->setChecked(s.gridSnap);
    gf->addRow(QString(), snapGrid);
    auto* ortho = new QCheckBox(QStringLiteral("Ortho mode"), draft);
    ortho->setChecked(s.ortho);
    gf->addRow(QString(), ortho);
    auto* polar = new QCheckBox(QStringLiteral("Polar tracking"), draft);
    polar->setChecked(s.polar);
    gf->addRow(QString(), polar);
    auto* polarStep = new QComboBox(draft);
    polarStep->addItems({"5", "10", "15", "22.5", "30", "45", "90"});
    polarStep->setCurrentText(QString::number(s.polarStepDeg, 'g'));
    gf->addRow(QStringLiteral("Polar angle (deg)"), polarStep);
    auto* osnap = new QCheckBox(QStringLiteral("Object snap"), draft);
    osnap->setChecked(s.objectSnap);
    gf->addRow(QString(), osnap);
    auto* snEnd = new QCheckBox(QStringLiteral("Endpoint"), draft); snEnd->setChecked(s.snapEndpoint);
    auto* snMid = new QCheckBox(QStringLiteral("Midpoint"), draft); snMid->setChecked(s.snapMidpoint);
    auto* snCen = new QCheckBox(QStringLiteral("Center"), draft); snCen->setChecked(s.snapCenter);
    auto* snInt = new QCheckBox(QStringLiteral("Intersection"), draft); snInt->setChecked(s.snapIntersection);
    auto* snNear = new QCheckBox(QStringLiteral("Nearest"), draft); snNear->setChecked(s.snapNearest);
    gf->addRow(QStringLiteral("Snap types"), snEnd);
    gf->addRow(QString(), snMid);
    gf->addRow(QString(), snCen);
    gf->addRow(QString(), snInt);
    gf->addRow(QString(), snNear);
    tabs->addTab(draft, QStringLiteral("Drafting"));

    // ===== Selection / Drawing =====
    auto* sel = new QWidget(tabs);
    auto* sfm = new QFormLayout(sel);
    auto* angles = new QCheckBox(QStringLiteral("Show drawing angles"), sel);
    angles->setChecked(editor_->showAngles());
    sfm->addRow(QString(), angles);
    auto* dimDec = new QSpinBox(sel);
    dimDec->setRange(0, 6);
    dimDec->setValue(2);
    dimDec->setButtonSymbols(QAbstractSpinBox::PlusMinus); // clear +/- (both work)
    sfm->addRow(QStringLiteral("Dimension decimals"), dimDec);
    tabs->addTab(sel, QStringLiteral("Selection"));

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    outer->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) return;

    // Apply everything. The theme sets canvas colors; explicit picks override.
    applyTheme(scheme->currentIndex() == 0);
    canvas_->setCrosshairSize(crossSize->value());
    if (crossChanged) canvas_->setCrosshairColor(crossCol);
    if (bgChanged) canvas_->setBackgroundColor(bgCol); // auto-derives grid colours
    if (gridChanged) canvas_->setGridColors(gridCol, gridCol2); // explicit override
    canvas_->setGridVisible(showGrid->isChecked());
    canvas_->setGridSize(gridStep->value());
    s.gridSize = gridStep->value();
    canvas_->setPickboxSize(pickBox->value());
    s.gridSnap = snapGrid->isChecked();
    s.ortho = ortho->isChecked();
    s.polar = polar->isChecked();
    s.polarStepDeg = polarStep->currentText().toDouble();
    s.objectSnap = osnap->isChecked();
    s.snapEndpoint = snEnd->isChecked();
    s.snapMidpoint = snMid->isChecked();
    s.snapCenter = snCen->isChecked();
    s.snapIntersection = snInt->isChecked();
    s.snapNearest = snNear->isChecked();
    editor_->setShowAngles(angles->isChecked());
    EntityRenderer::setDimensionDecimals(dimDec->value());
    canvas_->update();
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
    // A checkbox centered in its cell.
    auto centeredCheck = [this](bool checked, std::function<void(bool)> onToggle) {
        auto* w = new QWidget(layersTable_);
        auto* hl = new QHBoxLayout(w);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setAlignment(Qt::AlignCenter);
        auto* cb = new QCheckBox(w);
        cb->setChecked(checked);
        connect(cb, &QCheckBox::toggled, this, [this, onToggle](bool v) {
            if (refreshing_) return;
            onToggle(v);
            doc_->setDirty(true);
            canvas_->update();
        });
        hl->addWidget(cb);
        return w;
    };
    // A small color swatch centered in its cell; click to change the layer color.
    auto colorCell = [this](int row, const QColor& c) {
        auto* w = new QWidget(layersTable_);
        auto* hl = new QHBoxLayout(w);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setAlignment(Qt::AlignCenter);
        auto* sw = new QToolButton(w);
        sw->setFixedSize(20, 14);
        sw->setCursor(Qt::PointingHandCursor);
        sw->setStyleSheet(QStringLiteral("QToolButton{background:%1;border:1px solid #6b7280;}")
                              .arg(c.name()));
        connect(sw, &QToolButton::clicked, this, [this, row] {
            auto& ll = doc_->layers();
            if (row < 0 || row >= static_cast<int>(ll.size())) return;
            const QColor chosen = QColorDialog::getColor(QColor(ll[row].color), this,
                                                         QStringLiteral("Layer color"));
            if (chosen.isValid()) {
                ll[row].color = chosen.name();
                doc_->setDirty(true);
                refreshLayers();
                canvas_->update();
            }
        });
        hl->addWidget(sw);
        return w;
    };

    layersTable_->setRowCount(static_cast<int>(ls.size()));
    for (int r = 0; r < static_cast<int>(ls.size()); ++r) {
        auto* status = new QTableWidgetItem(ls[r].name == active ? QStringLiteral("●") : QString());
        status->setFlags(Qt::ItemIsEnabled);
        status->setTextAlignment(Qt::AlignCenter);
        status->setForeground(QColor("#5fb763"));
        layersTable_->setItem(r, 0, status);
        layersTable_->setItem(r, 1, new QTableWidgetItem(ls[r].name));
        layersTable_->setCellWidget(
            r, 2, centeredCheck(ls[r].visible, [this, r](bool v) { doc_->layers()[r].visible = v; }));
        layersTable_->setCellWidget(
            r, 3, centeredCheck(ls[r].locked, [this, r](bool v) { doc_->layers()[r].locked = v; }));
        layersTable_->setCellWidget(r, 4, colorCell(r, QColor(ls[r].color)));
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

    // Section-header band, themed so the title stays legible in light + dark.
    const QColor hbg = darkTheme_ ? QColor("#313438") : QColor("#d2d2d8");
    const QColor htx = darkTheme_ ? QColor("#cdd6e4") : QColor("#1e1e1e");
    auto addHeader = [&](const QString& title) {
        const int r = propsTable_->rowCount();
        propsTable_->insertRow(r);
        auto* h = new QTableWidgetItem(title);
        QFont f = h->font();
        f.setBold(true);
        h->setFont(f);
        h->setFlags(Qt::ItemIsEnabled);
        h->setBackground(hbg);
        h->setForeground(htx);
        propsTable_->setItem(r, 0, h);
        auto* blank = new QTableWidgetItem();
        blank->setFlags(Qt::ItemIsEnabled);
        blank->setBackground(hbg);
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
        sp->setDecimals(2);
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
            const QColor hb = darkTheme_ ? QColor("#313438") : QColor("#d2d2d8");
            c0->setBackground(hb);
            c1->setBackground(hb);
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
    pins->setButtonSymbols(QAbstractSpinBox::PlusMinus);
    auto* pitch = new QDoubleSpinBox(&dlg);
    pitch->setRange(1, 200);
    pitch->setValue(spec.pitch);
    pitch->setButtonSymbols(QAbstractSpinBox::PlusMinus);
    auto* pinLen = new QDoubleSpinBox(&dlg);
    pinLen->setRange(1, 500);
    pinLen->setValue(spec.pinLength);
    pinLen->setButtonSymbols(QAbstractSpinBox::PlusMinus);
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
    saveSettings(); // persist the drafting/snap toggles for next launch
    e->accept();
}

// ---- Persistent drafting preferences ---------------------------------------

void MainWindow::loadSettings() {
    QSettings s;
    auto& sn = editor_->snapSettings();
    sn.gridSnap = s.value(QStringLiteral("draft/gridSnap"), sn.gridSnap).toBool();
    sn.ortho = s.value(QStringLiteral("draft/ortho"), sn.ortho).toBool();
    sn.polar = s.value(QStringLiteral("draft/polar"), sn.polar).toBool();
    sn.polarStepDeg = s.value(QStringLiteral("draft/polarStep"), sn.polarStepDeg).toDouble();
    sn.objectSnap = s.value(QStringLiteral("draft/objectSnap"), sn.objectSnap).toBool();
    sn.snapEndpoint = s.value(QStringLiteral("draft/snapEndpoint"), sn.snapEndpoint).toBool();
    sn.snapMidpoint = s.value(QStringLiteral("draft/snapMidpoint"), sn.snapMidpoint).toBool();
    sn.snapCenter = s.value(QStringLiteral("draft/snapCenter"), sn.snapCenter).toBool();
    sn.snapIntersection = s.value(QStringLiteral("draft/snapIntersection"), sn.snapIntersection).toBool();
    sn.snapNearest = s.value(QStringLiteral("draft/snapNearest"), sn.snapNearest).toBool();
    sn.gridSize = s.value(QStringLiteral("draft/gridSize"), sn.gridSize).toDouble();
    canvas_->setGridSize(sn.gridSize);
    canvas_->setGridVisible(s.value(QStringLiteral("draft/gridVisible"), canvas_->gridVisible()).toBool());
    editor_->setShowAngles(s.value(QStringLiteral("draft/showAngles"), editor_->showAngles()).toBool());
}

void MainWindow::saveSettings() {
    if (!editor_) return;
    QSettings s;
    const auto& sn = editor_->snapSettings();
    s.setValue(QStringLiteral("draft/gridSnap"), sn.gridSnap);
    s.setValue(QStringLiteral("draft/ortho"), sn.ortho);
    s.setValue(QStringLiteral("draft/polar"), sn.polar);
    s.setValue(QStringLiteral("draft/polarStep"), sn.polarStepDeg);
    s.setValue(QStringLiteral("draft/objectSnap"), sn.objectSnap);
    s.setValue(QStringLiteral("draft/snapEndpoint"), sn.snapEndpoint);
    s.setValue(QStringLiteral("draft/snapMidpoint"), sn.snapMidpoint);
    s.setValue(QStringLiteral("draft/snapCenter"), sn.snapCenter);
    s.setValue(QStringLiteral("draft/snapIntersection"), sn.snapIntersection);
    s.setValue(QStringLiteral("draft/snapNearest"), sn.snapNearest);
    s.setValue(QStringLiteral("draft/gridSize"), canvas_->gridSize());
    s.setValue(QStringLiteral("draft/gridVisible"), canvas_->gridVisible());
    s.setValue(QStringLiteral("draft/showAngles"), editor_->showAngles());
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
