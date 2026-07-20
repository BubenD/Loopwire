#include "app/MainWindow.h"

#include "app/Editor.h"
#include "app/FontLibrary.h"
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
#include <QAbstractButton>
#include <QButtonGroup>
#include <QSpinBox>
#include <QActionGroup>
#include <QApplication>
#include <QColorDialog>
#include <QCompleter>
#include <QSet>
#include <QStandardItemModel>
#include <QGroupBox>
#include <QRadioButton>
#include <QSlider>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFrame>
#include <QJsonArray>
#include <QJsonValue>
#include <QKeyEvent>
#include <QSignalBlocker>
#include <QTextCursor>
#include <QGridLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QScrollArea>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QCloseEvent>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPdfWriter>
#include <QPageSize>
#include <QPageLayout>
#include <QMarginsF>
#include <QFile>
#include <QTextStream>
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

// A thin ruler bar above the on-canvas text editor (AutoCAD MTEXT style): a tick
// scale with a left indent marker and a right WIDTH handle (diamond) you can drag
// to resize the container. `onWidth(newWidthPx)` fires while dragging.
class InlineRuler : public QWidget {
public:
    using QWidget::QWidget;
    std::function<void(int)> onWidth;
    static constexpr int kHandle = 7; // half-size of the right diamond handle

protected:
    bool overHandle(int x) const { return std::abs(x - (width() - kHandle)) <= kHandle + 2; }

    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        p.fillRect(rect(), QColor("#d8d8d0"));
        p.setPen(QColor("#3a3d43"));
        const int h = height();
        p.drawLine(0, h - 1, width(), h - 1);
        for (int x = 4; x < width() - kHandle; x += 10) {
            const int top = ((x - 4) % 50 == 0) ? 2 : h / 2;
            p.drawLine(x, top, x, h - 1);
        }
        // Left indent marker (small filled triangle).
        QPolygon left;
        left << QPoint(2, 2) << QPoint(2, h - 2) << QPoint(7, h - 2);
        p.setBrush(QColor("#3a3d43"));
        p.drawPolygon(left);
        // Right width handle (diamond).
        const int cx = width() - kHandle, cy = h / 2;
        QPolygon dia;
        dia << QPoint(cx, cy - kHandle) << QPoint(cx + kHandle, cy) << QPoint(cx, cy + kHandle)
            << QPoint(cx - kHandle, cy);
        p.setBrush(QColor("#3d8bfd"));
        p.drawPolygon(dia);
    }

    void mouseMoveEvent(QMouseEvent* e) override {
        if (dragging_) {
            if (onWidth) onWidth(std::max(40, e->pos().x() + kHandle));
        } else {
            setCursor(overHandle(e->pos().x()) ? Qt::SizeHorCursor : Qt::ArrowCursor);
        }
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && overHandle(e->pos().x())) dragging_ = true;
    }
    void mouseReleaseEvent(QMouseEvent*) override { dragging_ = false; }

private:
    bool dragging_ = false;
};

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
    case ToolId::MText: { QFont f = p.font(); f.setPixelSize(11); f.setBold(true); p.setFont(f); p.drawText(QRect(2, 2, 20, 20), Qt::AlignLeft | Qt::AlignTop, "A"); p.drawLine(9, 8, 20, 8); p.drawLine(5, 14, 20, 14); p.drawLine(5, 19, 20, 19); break; }
    case ToolId::Dimension: p.drawLine(5, 12, 19, 12); p.drawLine(5, 8, 5, 16); p.drawLine(19, 8, 19, 16); break;
    case ToolId::Hatch: {
        p.drawRect(4, 5, 16, 14);
        // diagonal hatch lines clipped to the box
        p.save();
        p.setClipRect(4, 5, 16, 14);
        for (int o = -14; o <= 16; o += 4) p.drawLine(4 + o, 19, 4 + o + 14, 5);
        p.restore();
        break;
    }
    case ToolId::Wipeout: {
        p.drawRect(4, 5, 16, 14);
        p.drawLine(4, 19, 20, 5); // crossed-out (masked) box
        p.drawLine(4, 5, 20, 19);
        break;
    }
    case ToolId::Move: {
        // Four-headed move arrow (clear 4-way cross).
        p.drawLine(12, 4, 12, 20); p.drawLine(4, 12, 20, 12);
        p.drawLine(12, 4, 9, 7);   p.drawLine(12, 4, 15, 7);    // up
        p.drawLine(12, 20, 9, 17); p.drawLine(12, 20, 15, 17);  // down
        p.drawLine(4, 12, 7, 9);   p.drawLine(4, 12, 7, 15);    // left
        p.drawLine(20, 12, 17, 9); p.drawLine(20, 12, 17, 15);  // right
        break;
    }
    case ToolId::Rotate: {
        // Near-full circular arrow.
        QPainterPath pa; pa.arcMoveTo(5, 5, 14, 14, 70); pa.arcTo(5, 5, 14, 14, 70, 250);
        p.drawPath(pa);
        const QPointF tip(12 + 7 * std::cos(-70 * M_PI / 180), 12 + 7 * std::sin(-70 * M_PI / 180));
        p.drawLine(tip, tip + QPointF(-4, -1)); p.drawLine(tip, tip + QPointF(0, -4));
        break;
    }
    case ToolId::Mirror: {
        // Dashed mirror axis with a solid shape and its reflection.
        QPen dash = pen; dash.setStyle(Qt::DashLine); p.setPen(dash);
        p.drawLine(12, 3, 12, 21);
        p.setPen(pen);
        QPolygonF a({{4, 7}, {10, 12}, {4, 17}}); QPolygonF b({{20, 7}, {14, 12}, {20, 17}});
        p.drawPolygon(a); p.drawPolygon(b);
        break;
    }
    case ToolId::Scale: {
        // Small square growing into a larger one along a diagonal arrow.
        p.drawRect(5, 14, 5, 5);
        p.drawRect(5, 6, 13, 13);
        p.drawLine(7, 17, 16, 8);
        p.drawLine(16, 8, 12, 8); p.drawLine(16, 8, 16, 12); // arrowhead
        break;
    }
    case ToolId::Offset: {
        // Two parallel corner polylines (the offset of a shape).
        QPolygonF a({{5, 6}, {5, 19}, {19, 19}}); p.drawPolyline(a);
        QPolygonF b({{9, 6}, {9, 15}, {19, 15}}); p.drawPolyline(b);
        break;
    }
    case ToolId::Trim: {
        // A line trimmed at a boundary: a vertical cutting edge, the kept part solid
        // up to it, and the removed part beyond shown dashed (AutoCAD TRIM sense).
        p.drawLine(14, 4, 14, 20);          // boundary / cutting edge
        p.drawLine(3, 12, 14, 12);          // kept segment
        QPen dash = pen;
        dash.setStyle(Qt::DashLine);
        p.setPen(dash);
        p.drawLine(14, 12, 21, 12);         // trimmed-away segment
        p.setPen(pen);
        break;
    }
    case ToolId::Extend: {
        // Arrow extending to a boundary wall.
        p.drawLine(19, 4, 19, 20);          // wall
        p.drawLine(4, 12, 16, 12);          // shaft
        p.drawLine(16, 12, 13, 9); p.drawLine(16, 12, 13, 15); // arrowhead
        break;
    }
    case ToolId::Fillet: { QPainterPath pa; pa.moveTo(5, 19); pa.lineTo(5, 12); pa.arcTo(5, 5, 14, 14, 180, -90); pa.lineTo(19, 5); p.drawPath(pa); break; }
    case ToolId::Chamfer: { QPolygonF a({{5, 19}, {5, 10}, {10, 5}, {19, 5}}); p.drawPolyline(a); break; }
    case ToolId::Wire: { pen.setWidthF(2.4); p.setPen(pen); p.drawLine(4, 17, 20, 7); break; }
    case ToolId::Connector: p.drawRect(6, 5, 8, 14); p.drawLine(14, 9, 20, 9); p.drawLine(14, 15, 20, 15); break;
    case ToolId::Splice: p.setBrush(ink); p.drawEllipse(QPointF(12, 12), 4, 4); break;
    case ToolId::NotConnected: p.drawLine(7, 7, 17, 17); p.drawLine(17, 7, 7, 17); break;
    case ToolId::NetLabel: { QPolygonF a({{4, 8}, {15, 8}, {20, 12}, {15, 16}, {4, 16}}); p.drawPolygon(a); break; }
    case ToolId::Balloon: {
        // Numbered callout: a circle with a leader stub and a "1" inside.
        p.drawEllipse(QPointF(9, 9), 6, 6);
        p.drawLine(13, 13, 20, 20); // leader
        QFont f = p.font(); f.setPixelSize(8); f.setBold(true); p.setFont(f);
        p.drawText(QRectF(3, 3, 12, 12), Qt::AlignCenter, QStringLiteral("1"));
        break;
    }
    case ToolId::TextLeader: {
        // A leader arrow up to a short landing, with text lines (no circle).
        p.drawLine(4, 20, 12, 12);          // leader
        p.drawLine(4, 20, 4, 16); p.drawLine(4, 20, 8, 20); // arrowhead
        p.drawLine(12, 12, 20, 12);         // horizontal landing
        p.drawLine(13, 8, 20, 8);           // text lines above the landing
        p.drawLine(13, 5, 18, 5);
        break;
    }
    case ToolId::Note: {
        // Bordered note box with a few text lines.
        p.drawRect(4, 5, 16, 14);
        for (int i = 0; i < 3; ++i) p.drawLine(7, 9 + i * 3, 17, 9 + i * 3);
        break;
    }
    case ToolId::Revision: {
        // Titled box: a top strip + a small revision triangle.
        p.drawRect(4, 5, 16, 14);
        p.drawLine(4, 10, 20, 10); // title strip divider
        QPolygonF tri({{12, 12}, {16, 18}, {8, 18}}); // rev triangle
        p.drawPolyline(tri); p.drawLine(8, 18, 12, 12);
        break;
    }
    case ToolId::Table: {
        // Grid of cells.
        p.drawRect(4, 5, 16, 14);
        p.drawLine(4, 10, 20, 10); p.drawLine(4, 15, 20, 15); // rows
        p.drawLine(9, 5, 9, 19); p.drawLine(15, 5, 15, 19);   // cols
        break;
    }
    }
    p.end();
    return QIcon(pm);
}

// Monochrome glyphs for the Modify "Operations" buttons (Array, Explode, Purge,
// Delete, Fillet radius) — same flat line-art style as the tool icons so they sit
// alongside them in the ribbon.
QIcon makeOpIcon(const QString& op, const QColor& ink = QColor("#d6dae1")) {
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
    if (op == QLatin1String("array")) {
        // 2 rows x 3 columns of small squares (rectangular array).
        for (int r = 0; r < 2; ++r)
            for (int c = 0; c < 3; ++c)
                p.drawRect(4 + c * 6, 6 + r * 7, 4, 4);
    } else if (op == QLatin1String("explode")) {
        // Small central box with four diagonal arrows flying outward.
        p.drawRect(10, 10, 4, 4);
        auto arrow = [&](QPointF tip, QPointF a, QPointF b) {
            p.drawLine(QPointF(12, 12), tip);
            p.drawLine(tip, a);
            p.drawLine(tip, b);
        };
        arrow({4, 4}, {4, 8}, {8, 4});
        arrow({20, 4}, {16, 4}, {20, 8});
        arrow({4, 20}, {4, 16}, {8, 20});
        arrow({20, 20}, {16, 20}, {20, 16});
    } else if (op == QLatin1String("purge")) {
        // Broom sweeping away unused items.
        p.drawLine(19, 4, 11, 12);          // handle
        QPolygonF head({{8, 11}, {14, 11}, {16, 19}, {6, 19}});
        p.drawPolyline(head);
        p.drawLine(8, 14, 14, 14);          // bristle band
        for (int x = 7; x <= 15; x += 2) p.drawLine(x, 15, x - 1, 19); // bristles
    } else if (op == QLatin1String("delete")) {
        // Trash can: lid + body + ribs.
        p.drawLine(4, 7, 20, 7);            // lid line
        p.drawRect(9, 4, 6, 3);             // handle
        QPolygonF body({{6, 7}, {18, 7}, {17, 20}, {7, 20}});
        p.drawPolygon(body);
        p.drawLine(10, 10, 10, 17);
        p.drawLine(12, 10, 12, 17);
        p.drawLine(14, 10, 14, 17);
    } else if (op == QLatin1String("filletr")) {
        // Rounded corner with an "R" label.
        QPainterPath pa;
        pa.moveTo(5, 20);
        pa.lineTo(5, 12);
        pa.arcTo(5, 5, 14, 14, 180, -90);
        pa.lineTo(20, 5);
        p.drawPath(pa);
        QFont f = p.font();
        f.setPixelSize(9);
        f.setBold(true);
        p.setFont(f);
        p.drawText(QRect(11, 11, 11, 11), Qt::AlignCenter, QStringLiteral("R"));
    } else if (op == QLatin1String("copy")) {
        // Two overlapping sheets (classic copy-to-clipboard glyph).
        p.drawRect(8, 8, 11, 13);  // front sheet
        p.drawRect(5, 4, 11, 13);  // back sheet (offset up-left)
    } else if (op == QLatin1String("cut")) {
        // Scissors: two blades (open V) with pivot and finger loops.
        p.drawLine(6, 5, 15, 14);
        p.drawLine(6, 19, 15, 10);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(5, 5.5), 2.2, 2.2);  // top loop
        p.drawEllipse(QPointF(5, 18.5), 2.2, 2.2); // bottom loop
        p.drawLine(15, 14, 20, 18);                // blade tips extending right
        p.drawLine(15, 10, 20, 6);
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
// A small preview of a leader arrowhead (shaft + head) for the Arrowhead fall-menu.
QIcon makeArrowheadIcon(const QString& type) {
    QPixmap pm(56, 16);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QColor ink("#d6dae1");
    QPen pen(ink);
    pen.setWidthF(1.4);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    const QPointF tail(4, 8), tip(44, 8);
    const double len = 10;
    p.drawLine(tail, QPointF(tip.x() - (type == QLatin1String("dot") ? len * 0.3 : 0), 8)); // shaft
    if (type == QLatin1String("none")) {
        // shaft only
    } else if (type == QLatin1String("dot")) {
        p.setBrush(ink); p.setPen(Qt::NoPen);
        p.drawEllipse(QPointF(tip.x() - len * 0.3, 8), len * 0.3, len * 0.3);
    } else if (type == QLatin1String("tick")) {
        p.drawLine(QPointF(tip.x() - len * 0.5, 8 + len * 0.5), QPointF(tip.x() + len * 0.5, 8 - len * 0.5));
    } else if (type == QLatin1String("open")) {
        p.setBrush(Qt::NoBrush);
        p.drawLine(tip, QPointF(tip.x() - len, 8 - len * 0.4));
        p.drawLine(tip, QPointF(tip.x() - len, 8 + len * 0.4));
    } else if (type == QLatin1String("blank")) {
        p.setBrush(Qt::NoBrush);
        QPolygonF tri;
        tri << tip << QPointF(tip.x() - len, 8 - len * 0.36) << QPointF(tip.x() - len, 8 + len * 0.36);
        p.drawPolygon(tri);
    } else { // closed filled
        p.setBrush(ink); p.setPen(Qt::NoPen);
        QPolygonF tri;
        tri << tip << QPointF(tip.x() - len, 8 - len * 0.36) << QPointF(tip.x() - len, 8 + len * 0.36);
        p.drawPolygon(tri);
    }
    p.end();
    return QIcon(pm);
}

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

// Icon per dimension mode (Linear/Aligned/Angular/ArcLength/Radius/Diameter/
// Ordinate/Jogged), matching Editor::DimMode order.
QIcon makeDimMethodIcon(int mode) {
    QPixmap pm = geoCanvas();
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(2, 2);
    QPen pen(kGeoBlue);
    pen.setWidthF(1.4);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    switch (mode) {
    case 0: // Linear: extension lines + horizontal dimension line
        p.drawLine(4, 19, 4, 9);
        p.drawLine(20, 19, 20, 9);
        p.drawLine(4, 10, 20, 10);
        break;
    case 1: // Aligned: diagonal dimension line
        p.drawLine(4, 20, 8, 16);
        p.drawLine(16, 8, 20, 4);
        p.drawLine(6, 18, 18, 6);
        break;
    case 2: { // Angular: two rays + arc
        p.drawLine(12, 20, 4, 6);
        p.drawLine(12, 20, 20, 6);
        QPainterPath pa;
        pa.moveTo(8, 14);
        pa.arcTo(4, 12, 16, 16, 130, -80);
        p.drawPath(pa);
        break;
    }
    case 3: { // Arc Length: arc with end ticks
        QPainterPath pa;
        pa.moveTo(5, 18);
        pa.arcTo(5, 5, 16, 16, 200, -140);
        p.drawPath(pa);
        break;
    }
    case 4: // Radius: circle + radius line
        p.drawEllipse(QPointF(12, 12), 8, 8);
        p.drawLine(12, 12, 20, 12);
        break;
    case 5: // Diameter: circle + line across
        p.drawEllipse(QPointF(12, 12), 8, 8);
        p.drawLine(4, 12, 20, 12);
        break;
    case 6: // Ordinate: feature dot + leader
        p.drawLine(12, 19, 12, 4);
        geoDot(p, QPointF(12, 19));
        break;
    case 7: { // Jogged: circle + jagged radius
        p.drawEllipse(QPointF(12, 12), 7, 7);
        QPolygonF z;
        z << QPointF(12, 12) << QPointF(15, 10) << QPointF(16, 14) << QPointF(21, 12);
        p.drawPolyline(z);
        break;
    }
    default:
        break;
    }
    p.end();
    return QIcon(pm);
}

// The Hatch pattern catalogue (display name + meta key), shared by the ribbon
// swatch row and the expanded pattern picker.
QList<QPair<QString, QString>> hatchPatternList() {
    return {
        {QStringLiteral("Solid"), QStringLiteral("solid")},
        {QStringLiteral("ANSI31"), QStringLiteral("ansi31")},
        {QStringLiteral("ANSI32"), QStringLiteral("ansi32")},
        {QStringLiteral("Lines"), QStringLiteral("lines")},
        {QStringLiteral("Grid"), QStringLiteral("grid")},
        {QStringLiteral("Cross"), QStringLiteral("cross")},
        {QStringLiteral("Dots"), QStringLiteral("dots")},
        {QStringLiteral("Brick"), QStringLiteral("brick")},
        {QStringLiteral("Honeycomb"), QStringLiteral("honeycomb")},
        {QStringLiteral("Braid"), QStringLiteral("braid")},
        {QStringLiteral("Cable 3D"), QStringLiteral("cable")},
        {QStringLiteral("Weave"), QStringLiteral("weave")},
        // ---- the rest appear in the expanded picker ----
        {QStringLiteral("Angle"), QStringLiteral("angle")},
        {QStringLiteral("Checker"), QStringLiteral("checker")},
        {QStringLiteral("Herringbone"), QStringLiteral("herringbone")},
        {QStringLiteral("Scales"), QStringLiteral("scales")},
        {QStringLiteral("Zigzag"), QStringLiteral("zigzag")},
    };
}

// A pattern swatch for the Hatch tab, rendered with the SAME pattern code as the
// canvas (EntityRenderer), so the icon always matches what gets drawn.
QIcon makeHatchSwatch(const QString& key, int W = 52, int H = 34) {
    QPixmap pm(W, H);
    pm.fill(QColor("#2a2d33"));
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    Edraw_Desktop::Document doc; // default layers; the override color drives the ink
    Edraw_Desktop::Entity e;
    e.type = Edraw_Desktop::EntityType::Hatch;
    e.layer = QStringLiteral("0");
    e.points = {{2, 2}, {W - 2.0, 2}, {W - 2.0, H - 2.0}, {2, H - 2.0}};
    e.meta.insert(QStringLiteral("pattern"), key);
    // Scale the pattern to the swatch so it shows a few clear repeats (not a fine
    // grid) regardless of icon size.
    e.meta.insert(QStringLiteral("hatchScale"), H / 22.0);
    // Identity transform, but flip Y back into the icon (toScreen negates Y).
    Edraw_Desktop::ViewTransform vt;
    vt.view.scale = 1.0;
    vt.view.x = 0.0;
    vt.view.y = H;
    Edraw_Desktop::EntityRenderer::draw(p, e, doc, vt, QColor("#e3e7ee"));

    QPen border(QColor("#7d8590"));
    border.setWidthF(1.0);
    p.setPen(border);
    p.setBrush(Qt::NoBrush);
    p.drawRect(QRectF(0.5, 0.5, W - 1.0, H - 1.0));
    p.end();
    return QIcon(pm);
}

// A filled color square for the hatch color picker; ByLayer/ByBlock get a marker.
QIcon makeColorSwatchIcon(const QColor& c, bool special) {
    QPixmap pm(18, 18);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (special) {
        p.setPen(QPen(QColor("#9aa3ad"), 1.0));
        p.setBrush(QColor("#3a3d43"));
        p.drawRect(QRectF(1.5, 1.5, 15, 15));
        p.setPen(QPen(QColor("#c9ccd1"), 1.0));
        p.drawLine(QPointF(3, 14), QPointF(15, 4)); // diagonal = "by ..."
    } else {
        p.setPen(QPen(QColor("#1a1c20"), 1.0));
        p.setBrush(c);
        p.drawRect(QRectF(1.5, 1.5, 15, 15));
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
    // Right-button drag dropped: AutoCAD-style Move / Copy / Cancel menu.
    // Tab while placing a connector pauses placement and focuses the ribbon preferences.
    connect(canvas_, &CanvasWidget::connectorEditRequested, this, [this] {
        if (connectorTab_ && ribbon_ && ribbon_->indexOf(connectorTab_) >= 0) {
            ribbon_->setCurrentWidget(connectorTab_);
            if (auto* le = connectorTab_->findChild<QLineEdit*>()) le->setFocus(Qt::TabFocusReason);
            statusBar()->showMessage(
                QStringLiteral("Editing connector preferences — click the drawing to place"));
        }
    });
    connect(canvas_, &CanvasWidget::rightDragDropped, this, [this](const QPoint& gp) {
        // Keys: M = Move, C = Copy, A = Cancel (the underlined mnemonics); Esc also
        // cancels. (Type the underlined letter while the menu is open.)
        QMenu menu(this);
        QAction* mv = menu.addAction(QStringLiteral("&Move Here"));
        QAction* cp = menu.addAction(QStringLiteral("&Copy Here"));
        menu.addSeparator();
        menu.addAction(QStringLiteral("C&ancel"));
        QAction* chosen = menu.exec(gp);
        if (chosen == mv) editor_->commitRightDrag(false);
        else if (chosen == cp) editor_->commitRightDrag(true);
        else editor_->cancelRightDrag();
    });
    connect(editor_, &Editor::message, this, [this](const QString& m) {
        statusBar()->showMessage(m);
        // Option toggles (arc/fillet/…) emit messages — keep the hint in sync.
        if (cmdHintLabel_) cmdHintLabel_->setText(editor_->drawCommandHint());
    });
    connect(editor_, &Editor::changed, this, [this] {
        canvas_->update();
        refreshPanels();
        updateActiveTabText(); // reflect the dirty marker
    });
    connect(editor_, &Editor::selectionChanged, this, [this] {
        refreshPanels();
        updateHatchContext(); // selecting a hatch opens the Hatch Creation tab to edit it
        updateConnectorContext(); // selecting a connector opens the Connector build tab
    });
    // Keep the "Place connector" button pressed-state in sync when placement is armed
    // by a canvas click (not just the button).
    connect(editor_, &Editor::connectorArmedChanged, this, [this] {
        if (connectorPlaceBtn_) connectorPlaceBtn_->setChecked(editor_->connectorArmed());
    });
    // A hatch was created (or the creation session ended): re-point the tab so the
    // controls keep editing the just-placed hatch until you click out / Close.
    connect(editor_, &Editor::hatchTargetChanged, this, [this] { updateHatchContext(); });
    // Clicking a table cell opens the contextual "Table Cell" tab (row/col + align ops).
    connect(editor_, &Editor::cellSelectionChanged, this, [this] { updateTableCellContext(); });
    // Mirror line picked: offer Copy / Move / Cancel (AutoCAD "erase source? No/Yes").
    connect(editor_, &Editor::mirrorChoiceRequested, this, [this] {
        // Keys: C = Copy, M = Move, A = Cancel (underlined mnemonics); Esc cancels.
        QMenu menu(this);
        QAction* cp = menu.addAction(QStringLiteral("&Copy (keep original)"));
        QAction* mv = menu.addAction(QStringLiteral("&Move (mirror in place)"));
        menu.addSeparator();
        menu.addAction(QStringLiteral("C&ancel"));
        QAction* chosen = menu.exec(QCursor::pos());
        if (chosen == cp) editor_->commitMirror(true);
        else if (chosen == mv) editor_->commitMirror(false);
        else editor_->cancelMirror();
    });
    connect(editor_, &Editor::toolChanged, this, [this](ToolId t) {
        if (QAction* a = toolActions_.value(static_cast<int>(t), nullptr)) a->setChecked(true);
        if (cmdHintLabel_) cmdHintLabel_->setText(editor_->drawCommandHint()); // applicable-commands help
        // AutoCAD-style contextual ribbon: the Hatch tool (or a selected hatch)
        // opens the "Hatch Creation" tab.
        updateHatchContext();
        updateConnectorContext(); // the Connector tool opens the "Connector build" tab
        if (connectorPlaceBtn_) // "Place connector" is pressed only while placement is armed
            connectorPlaceBtn_->setChecked(editor_->connectorArmed());
        canvas_->setFocus(); // so typing immediately opens the dynamic-input field
        canvas_->update();   // repaint now (e.g. to clear a connector preview on tool change)
        if (t == ToolId::Connector)
            statusBar()->showMessage(QStringLiteral(
                "Connector: edit the settings, then press Place connector to position it"));
    });
    connect(editor_, &Editor::textPromptRequested, this, [this](const QString&) {
        // Begin on-canvas in-place editing at the pending insertion box.
        Vec2 tl;
        double w = 0, h = 0;
        if (!editor_->pendingTextBox(tl, w, h)) return;
        Editor::TextStyle style;
        style.boxWidth = w;
        style.boxHeight = h;
        beginInlineText(QString(), QString(), style, tl);
    });
    connect(editor_, &Editor::editTextRequested, this, [this](const QString& id) {
        if (!doc_) return;
        const Entity* found = nullptr;
        for (const auto& e : doc_->entities())
            if (e.id == id) { found = &e; break; }
        if (!found) return;
        Editor::TextStyle style;
        style.size = found->meta.value(QStringLiteral("size")).toDouble(5.0);
        style.font = found->meta.value(QStringLiteral("font")).toString();
        style.bold = found->meta.value(QStringLiteral("bold")).toBool();
        style.italic = found->meta.value(QStringLiteral("italic")).toBool();
        style.underline = found->meta.value(QStringLiteral("underline")).toBool();
        style.strike = found->meta.value(QStringLiteral("strike")).toBool();
        style.justify = found->meta.value(QStringLiteral("justify")).toString();
        if (style.justify.isEmpty()) style.justify = QStringLiteral("TL");
        style.lineSpacing = found->meta.value(QStringLiteral("lineSpacing")).toDouble(1.0);
        style.list = found->meta.value(QStringLiteral("list")).toString();
        if (style.list.isEmpty()) style.list = QStringLiteral("none");
        style.boxWidth = found->meta.value(QStringLiteral("boxWidth")).toDouble(0.0);
        style.boxHeight = found->meta.value(QStringLiteral("boxHeight")).toDouble(0.0);
        beginInlineText(id, found->meta.value(QStringLiteral("text")).toString(), style, found->a);
        // A balloon number is a single value: plain Enter (or clicking away) commits it.
        inlineSingleLine_ = found->meta.value(QStringLiteral("balloon")).toBool();
    });
    // Double-clicked a table cell: open the inline editor over that cell, writing back
    // to the cell (not a text object) on commit.
    connect(editor_, &Editor::editTableCellRequested, this,
            [this](const QString& id, int cell, const QString& text, Vec2 at, double cw, double ch) {
                Editor::TextStyle style;
                style.size = 4.0;
                style.boxWidth = cw;  // size the editor to the cell so text stays inside it
                style.boxHeight = ch;
                beginInlineText(id, text, style, at);
                inlineCell_ = cell; // mark this as a table-cell edit (after beginInlineText resets it)
                inlineSingleLine_ = true; // Enter commits a table cell too
                inlineRuler_->hide();     // a cell editor sits exactly on the cell, no width ruler
                layoutInlineText();       // relayout now that it's flagged a cell
            });
    // Double-clicked a connector pin label: edit that pin's number/letter inline.
    connect(editor_, &Editor::editConnectorPinRequested, this,
            [this](const QString& id, int pin, const QString& text, Vec2 at, double size,
                   const QString& font) {
                Editor::TextStyle style;
                style.size = std::max(2.0, size);
                style.font = font; // start the font picker on the connector's pin font
                beginInlineText(QString(), text, style, at);
                inlineConnectorId_ = id;
                inlineConnectorPin_ = pin; // set after beginInlineText resets it
                inlineSingleLine_ = true;  // Enter commits the pin label
                inlineRuler_->hide();
                layoutInlineText();
            });
    connect(canvas_, &CanvasWidget::inlineCommit, this, [this] { commitInlineText(true); });
    // Keep the on-canvas text editor glued to its world position when panning/zooming.
    connect(canvas_, &CanvasWidget::viewChanged, this, [this] {
        if (inlineEdit_ && inlineEdit_->isVisible()) layoutInlineText();
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

    // Command autocomplete: typing the first letters suggests every matching
    // command/alias and previews what it does ("tr — Trim to a boundary"). Accepting
    // a suggestion inserts the bare word. Shared by the bottom command bar and the
    // floating dynamic-input box.
    {
        // One entry per name/alias, sorted by word; label = "word — description".
        QList<QPair<QString, QString>> entries; // (word, label)
        QSet<QString> seen;
        for (const auto& c : commands_)
            for (const auto& n : c.names)
                if (!seen.contains(n)) {
                    seen.insert(n);
                    entries.append({n, n + QStringLiteral("  —  ") + c.desc});
                }
        std::sort(entries.begin(), entries.end(),
                  [](const auto& a, const auto& b) { return a.first.compare(b.first, Qt::CaseInsensitive) < 0; });

        QStringList words, labels;
        auto* model = new QStandardItemModel(this);
        for (const auto& e : entries) {
            words << e.first;
            labels << e.second;
            auto* it = new QStandardItem();
            it->setData(e.second, Qt::DisplayRole); // preview text in the popup
            it->setData(e.first, Qt::EditRole);     // inserted on accept
            model->appendRow(it);
        }
        auto* comp = new QCompleter(model, this);
        model->setParent(comp);
        comp->setCaseSensitivity(Qt::CaseInsensitive);
        comp->setFilterMode(Qt::MatchStartsWith);
        comp->setCompletionMode(QCompleter::PopupCompletion);
        if (commandEdit_) {
            commandEdit_->setCompleter(comp);
            comp->popup()->installEventFilter(this); // Enter submits typed text (nearest cmd)
        }
        canvas_->setCommandWords(words, labels);
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
    lbl->setStyleSheet(QStringLiteral("color:#b8bdc6; font-size:11px; background:transparent;"));
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
        b->setIconSize(QSize(28, 28)); // larger ribbon buttons
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus); // keep keyboard focus on the canvas
        // Show the tool's name on hover. Set it on the button too (not only the
        // action) so the popup-menu variants below still get a tooltip.
        b->setToolTip(toolLabel(id));

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
        } else if (id == ToolId::Dimension) {
            // AutoCAD-style dimension flyout.
            auto* menu = new QMenu(b);
            auto add = [&](const QString& label, Editor::DimMode m) {
                menu->addAction(makeDimMethodIcon(static_cast<int>(m)), label, this, [this, act, m] {
                    editor_->setDimMode(m);
                    editor_->setTool(ToolId::Dimension);
                    act->setIcon(makeDimMethodIcon(static_cast<int>(m)));
                });
            };
            add(QStringLiteral("Linear"), Editor::DimMode::Linear);
            add(QStringLiteral("Aligned"), Editor::DimMode::Aligned);
            add(QStringLiteral("Angular"), Editor::DimMode::Angular);
            menu->addSeparator();
            add(QStringLiteral("Arc Length"), Editor::DimMode::ArcLength);
            add(QStringLiteral("Radius"), Editor::DimMode::Radius);
            add(QStringLiteral("Diameter"), Editor::DimMode::Diameter);
            menu->addSeparator();
            add(QStringLiteral("Ordinate"), Editor::DimMode::Ordinate);
            add(QStringLiteral("Jogged"), Editor::DimMode::Jogged);
            act->setIcon(makeDimMethodIcon(0)); // default = Linear
            b->setMenu(menu);
            b->setPopupMode(QToolButton::MenuButtonPopup);
        }
        // Hatch options live in the contextual "Hatch Creation" tab that opens when
        // the tool is active (AutoCAD-style), so the ribbon button just starts it.

        grid->addWidget(b, i / perRow, i % perRow);
        ++i;
    }
    return w;
}

// A 2-row icon grid of the Modify "Operations" actions, built as icon buttons in
// the same style as toolRow so they read consistently in the ribbon. Reused on
// both the Home and Modify tabs. Buttons are tracked in opButtons_ for theme
// re-tinting.
QWidget* MainWindow::operationsRow() {
    struct Op { const char* key; QString tip; std::function<void()> fn; };
    const QList<Op> ops = {
        {"array", QStringLiteral("Array"), [this] { arrayDialog(); }},
        {"explode", QStringLiteral("Explode"), [this] { editor_->explodeSelected(); }},
        {"purge", QStringLiteral("Purge (remove empty layers / stray objects)"),
         [this] { editor_->purgeUnused(); }},
        {"delete", QStringLiteral("Delete"), [this] { editor_->deleteSelected(); }},
    };
    auto* w = new QWidget(ribbon_);
    auto* grid = new QGridLayout(w);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(1);
    const int perRow = (ops.size() + 1) / 2;
    int i = 0;
    for (const Op& op : ops) {
        auto* b = new QToolButton(w);
        const QString key = QString::fromLatin1(op.key);
        b->setIcon(makeOpIcon(key));
        b->setIconSize(QSize(28, 28));
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setToolTip(op.tip);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        connect(b, &QToolButton::clicked, this, op.fn);
        grid->addWidget(b, i / perRow, i % perRow);
        opButtons_.append({key, b});
        ++i;
    }
    return w;
}

void MainWindow::insertTableCommand() {
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Insert Table"));
    auto* form = new QFormLayout(&dlg);
    auto* colsSp = new QSpinBox(&dlg);
    colsSp->setRange(1, 200);
    colsSp->setValue(5);
    auto* colWSp = new QDoubleSpinBox(&dlg);
    colWSp->setRange(1, 100000);
    colWSp->setDecimals(2);
    colWSp->setValue(40.0);
    auto* rowsSp = new QSpinBox(&dlg);
    rowsSp->setRange(1, 500);
    rowsSp->setValue(1);
    auto* rowHSp = new QDoubleSpinBox(&dlg);
    rowHSp->setRange(1, 100000);
    rowHSp->setDecimals(2);
    rowHSp->setValue(10.0);
    auto* titleCb = new QCheckBox(QStringLiteral("Title row"), &dlg);
    titleCb->setChecked(true);
    auto* headerCb = new QCheckBox(QStringLiteral("Header row"), &dlg);
    headerCb->setChecked(true);
    form->addRow(QStringLiteral("Columns:"), colsSp);
    form->addRow(QStringLiteral("Column width:"), colWSp);
    form->addRow(QStringLiteral("Data rows:"), rowsSp);
    form->addRow(QStringLiteral("Row height:"), rowHSp);
    form->addRow(titleCb);
    form->addRow(headerCb);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    Editor::TableSpec s;
    s.cols = colsSp->value();
    s.colWidth = colWSp->value();
    s.dataRows = rowsSp->value();
    s.rowHeight = rowHSp->value();
    s.title = titleCb->isChecked();
    s.header = headerCb->isChecked();
    editor_->setTableSpec(s);
    editor_->setTool(ToolId::Table);
    statusBar()->showMessage(QStringLiteral("Insert Table — click the insertion point"));
}

QWidget* MainWindow::clipboardRow() {
    auto* w = new QWidget(ribbon_);
    auto* lay = new QVBoxLayout(w);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(1);
    lay->setAlignment(Qt::AlignTop);

    // A split button whose PRIMARY action is whichever was chosen last (Copy or Cut),
    // AutoCAD-flyout style: clicking runs it; the fall-menu switches it. Choosing Cut
    // from the menu makes the ribbon button become Cut (and vice-versa).
    auto* btn = new QToolButton(w);
    btn->setIconSize(QSize(28, 28));
    btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    btn->setAutoRaise(true);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setPopupMode(QToolButton::MenuButtonPopup); // click = current action; arrow = menu

    auto* copyAct = new QAction(makeOpIcon(QStringLiteral("copy")), QStringLiteral("Copy"), this);
    copyAct->setToolTip(QStringLiteral("Copy selection to clipboard"));
    connect(copyAct, &QAction::triggered, this, [this] { editor_->copySelection(); });
    auto* cutAct = new QAction(makeOpIcon(QStringLiteral("cut")), QStringLiteral("Cut"), this);
    cutAct->setToolTip(QStringLiteral("Cut selection to clipboard"));
    connect(cutAct, &QAction::triggered, this, [this] { editor_->cutSelection(); });
    // Triggering either action (from the button OR the menu) also makes it primary.
    connect(copyAct, &QAction::triggered, this, [btn, copyAct] { btn->setDefaultAction(copyAct); });
    connect(cutAct, &QAction::triggered, this, [btn, cutAct] { btn->setDefaultAction(cutAct); });

    auto* menu = new QMenu(btn);
    menu->addAction(copyAct);
    menu->addAction(cutAct);
    btn->setMenu(menu);
    btn->setDefaultAction(copyAct); // starts as Copy

    lay->addWidget(btn);
    lay->addStretch(1);
    return w;
}

void MainWindow::buildRibbon() {
    toolGroup_ = new QActionGroup(this);
    toolGroup_->setExclusive(true);

    ribbon_ = new QTabWidget(this);
    ribbon_->setDocumentMode(true);
    ribbon_->setFixedHeight(150); // taller: larger icons + room for the 4-row hatch fields
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
        "QLabel{background-color:transparent;color:#c8ccd2;}"
        "QToolButton{background-color:transparent;border:1px solid transparent;border-radius:4px;padding:3px 6px;}"
        "QToolButton:hover{background-color:#484c53;border-color:#5a5f68;}"
        "QToolButton:checked{background-color:#294e74;border-color:#3d8bfd;}"
        "QToolButton:pressed{background-color:#22384f;}"
        "QFrame{color:#2a2c31;}"
        // Combos + spin fields: lighter field, bright text, clear border so they
        // read against the charcoal ribbon in dark mode.
        "QComboBox{background-color:#565b64;color:#f2f4f7;border:1px solid #7b818b;border-radius:3px;padding:1px 6px;min-height:18px;}"
        "QComboBox:hover{border-color:#5aa0ff;}"
        // A distinct, button-like drop-down box with a clear down-triangle.
        "QComboBox::drop-down{subcontrol-origin:padding;subcontrol-position:right center;"
        "background-color:#6a6f78;border-left:1px solid #7b818b;border-top-right-radius:3px;"
        "border-bottom-right-radius:3px;width:20px;}"
        "QComboBox::drop-down:hover{background-color:#7c828c;}"
        "QComboBox::down-arrow{image:none;width:0;height:0;margin-right:2px;"
        "border-left:5px solid transparent;border-right:5px solid transparent;"
        "border-top:7px solid #eef1f5;}"
        "QComboBox QAbstractItemView{background-color:#2f3237;color:#f2f4f7;selection-background-color:#3d8bfd;selection-color:#ffffff;}"
        "QAbstractSpinBox{background-color:#565b64;color:#f2f4f7;border:1px solid #7b818b;border-radius:3px;padding:1px 4px;min-height:18px;}"
        "QAbstractSpinBox:hover{border-color:#5aa0ff;}"
        "QAbstractSpinBox::up-button,QAbstractSpinBox::down-button{width:14px;background-color:#6a6f78;}"
        "QAbstractSpinBox::up-arrow{image:none;width:7px;height:7px;border-left:3px solid transparent;border-right:3px solid transparent;border-bottom:5px solid #e8eaed;}"
        "QAbstractSpinBox::down-arrow{image:none;width:7px;height:7px;border-left:3px solid transparent;border-right:3px solid transparent;border-top:5px solid #e8eaed;}"));

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
                          ToolId::Ellipse, ToolId::Arc, ToolId::Spline, ToolId::Polygon,
                          ToolId::Hatch, ToolId::Wipeout}));
        {
            // Modify group holds the modify tools AND the Copy/Cut clipboard button.
            auto* modW = new QWidget(page);
            auto* mh = new QHBoxLayout(modW);
            mh->setContentsMargins(0, 0, 0, 0);
            mh->setSpacing(2);
            mh->addWidget(toolRow({ToolId::Move, ToolId::Rotate, ToolId::Mirror, ToolId::Scale,
                                   ToolId::Offset, ToolId::Trim, ToolId::Extend, ToolId::Fillet,
                                   ToolId::Chamfer}));
            mh->addWidget(clipboardRow());
            addGroup(lay, QStringLiteral("Modify"), modW);
        }
        addGroup(lay, QStringLiteral("Operations"), operationsRow());
        {
            // Annotation group: the annotation tools + a Table button (Insert Table).
            auto* annoW = new QWidget(page);
            auto* ah = new QHBoxLayout(annoW);
            ah->setContentsMargins(0, 0, 0, 0);
            ah->setSpacing(2);
            ah->addWidget(toolRow({ToolId::Text, ToolId::Dimension, ToolId::Balloon,
                                   ToolId::Note, ToolId::Revision}));
            auto* tb = new QToolButton(annoW);
            tb->setIcon(makeToolIcon(ToolId::Table));
            tb->setIconSize(QSize(28, 28));
            tb->setToolButtonStyle(Qt::ToolButtonIconOnly);
            tb->setAutoRaise(true);
            tb->setFocusPolicy(Qt::NoFocus);
            tb->setToolTip(QStringLiteral("Insert Table"));
            connect(tb, &QToolButton::clicked, this, [this] { insertTableCommand(); });
            ah->addWidget(tb);
            addGroup(lay, QStringLiteral("Annotation"), annoW);
        }

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
        // Bright, full-width labels so "Layer"/"Color" read clearly in dark mode.
        auto propLabel = [&](const QString& t) {
            auto* l = new QLabel(t, propsW);
            l->setStyleSheet(QStringLiteral("color:#e6e9ef; background:transparent;"));
            l->setMinimumWidth(38);
            return l;
        };
        // Lineweight dropdown (ByLayer + standard weights) for new entities / selection.
        auto* lwCombo = new QComboBox(propsW);
        lwCombo->setMinimumWidth(120);
        lwCombo->addItems({"ByLayer", "0.00", "0.13", "0.25", "0.35", "0.50", "0.70",
                           "1.00", "1.40", "2.00"});
        pg->addWidget(propLabel(QStringLiteral("Layer")), 0, 0);
        pg->addWidget(layerCombo_, 0, 1);
        pg->addWidget(propLabel(QStringLiteral("Color")), 1, 0);
        pg->addWidget(colorCombo_, 1, 1);
        pg->addWidget(propLabel(QStringLiteral("Weight")), 2, 0);
        pg->addWidget(lwCombo, 2, 1);
        connect(lwCombo, &QComboBox::activated, this, [this, lwCombo](int) {
            const QString t = lwCombo->currentText();
            const double w = (t == QLatin1String("ByLayer")) ? 0.0 : t.toDouble();
            editor_->setCurrentWeight(w);   // applies to newly drawn entities
            editor_->setSelectionWeight(w); // and to any current selection
        });
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
        {
            // Edit group: the pick-modify tools plus the Copy/Cut clipboard button.
            auto* editW = new QWidget(page);
            auto* eh = new QHBoxLayout(editW);
            eh->setContentsMargins(0, 0, 0, 0);
            eh->setSpacing(2);
            eh->addWidget(
                toolRow({ToolId::Trim, ToolId::Extend, ToolId::Fillet, ToolId::Chamfer}));
            eh->addWidget(clipboardRow());
            addGroup(lay, QStringLiteral("Edit"), editW);
        }
        addGroup(lay, QStringLiteral("Operations"), operationsRow());
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
        addGroup(lay, QStringLiteral("Annotate"), toolRow({ToolId::Dimension, ToolId::Text, ToolId::MText, ToolId::NetLabel}));
        {
            // Callouts + a Table button that asks rows/cols before you drag the grid.
            auto* calloutW = new QWidget(page);
            auto* ch = new QHBoxLayout(calloutW);
            ch->setContentsMargins(0, 0, 0, 0);
            ch->setSpacing(2);
            // Leader = a split-button: click places a TEXT leader (no circle); the
            // fall-menu offers a Balloon leader plus the multileader operations.
            auto* balloonBtn = new QToolButton(calloutW);
            balloonBtn->setIcon(makeToolIcon(ToolId::TextLeader));
            balloonBtn->setIconSize(QSize(28, 28));
            balloonBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
            balloonBtn->setAutoRaise(true);
            balloonBtn->setFocusPolicy(Qt::NoFocus);
            balloonBtn->setToolTip(QStringLiteral("Leader (text) — click the tip then the label"));
            balloonBtn->setPopupMode(QToolButton::MenuButtonPopup);
            connect(balloonBtn, &QToolButton::clicked, this,
                    [this] { editor_->setTool(ToolId::TextLeader); });
            auto* lm = new QMenu(balloonBtn);
            {
                QAction* a = lm->addAction(makeToolIcon(ToolId::TextLeader),
                                           QStringLiteral("Leader (text only)"));
                connect(a, &QAction::triggered, this, [this] { editor_->setTool(ToolId::TextLeader); });
                a = lm->addAction(makeToolIcon(ToolId::Balloon), QStringLiteral("Balloon"));
                connect(a, &QAction::triggered, this, [this] { editor_->setTool(ToolId::Balloon); });
                lm->addSeparator();
                a = lm->addAction(QStringLiteral("Add Leader"));
                connect(a, &QAction::triggered, this, [this] { editor_->addLeaderToSelection(); });
                a = lm->addAction(QStringLiteral("Remove Leader"));
                connect(a, &QAction::triggered, this,
                        [this] { editor_->removeLeaderFromSelection(); });
                lm->addSeparator();
                a = lm->addAction(QStringLiteral("Align"));
                connect(a, &QAction::triggered, this, [this] { editor_->alignBalloons(); });
                a = lm->addAction(QStringLiteral("Collect"));
                connect(a, &QAction::triggered, this, [this] { editor_->collectBalloons(); });
            }
            balloonBtn->setMenu(lm);
            ch->addWidget(balloonBtn);
            ch->addWidget(toolRow({ToolId::Note, ToolId::Revision}));
            auto* tableBtn = new QToolButton(calloutW);
            tableBtn->setIcon(makeToolIcon(ToolId::Table));
            tableBtn->setIconSize(QSize(28, 28));
            tableBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
            tableBtn->setAutoRaise(true);
            tableBtn->setFocusPolicy(Qt::NoFocus);
            tableBtn->setToolTip(QStringLiteral("Table — set rows/columns, then drag its size"));
            connect(tableBtn, &QToolButton::clicked, this, [this] { insertTableCommand(); });
            ch->addWidget(tableBtn);
            addGroup(lay, QStringLiteral("Callouts"), calloutW);
        }
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

    // ---- SCHEMATIC BUILD: placement tools; the "Connector build" groups live in a
    //      contextual tab that opens only when a connector is being placed / edited. ----
    {
        auto [page, lay] = makeTabPage();
        addGroup(lay, QStringLiteral("Place"),
                 toolRow({ToolId::Connector, ToolId::Wire, ToolId::NetLabel,
                          ToolId::NotConnected}));
        // Modify: straighten the wiring around a reference connector.
        auto* modW = new QWidget(page);
        auto* modG = new QGridLayout(modW);
        modG->setContentsMargins(0, 0, 0, 0);
        modG->setSpacing(2);
        auto* alignBtn = new QToolButton(modW);
        alignBtn->setText(QStringLiteral("Align\nrouting"));
        alignBtn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        alignBtn->setIconSize(QSize(26, 26));
        alignBtn->setMinimumWidth(66);
        // Icon: two pin stubs on the left joined by a right-angle (0°/90°) route to a
        // pin stub on the right — the "make wires orthogonal" glyph.
        {
            QPixmap pm(28, 28);
            pm.fill(Qt::transparent);
            QPainter ip(&pm);
            ip.setRenderHint(QPainter::Antialiasing, true);
            QPen pen(QColor("#cfe3ff"));
            pen.setWidthF(1.8);
            ip.setPen(pen);
            ip.drawLine(3, 7, 12, 7);    // top pin exits right
            ip.drawLine(12, 7, 12, 21);  // vertical run (90°)
            ip.drawLine(12, 21, 25, 21); // into the lower-right pin
            ip.drawLine(3, 14, 8, 14);   // a second (straight) pin stub
            ip.drawLine(8, 14, 8, 21);
            ip.end();
            alignBtn->setIcon(QIcon(pm));
        }
        alignBtn->setToolTip(QStringLiteral(
            "Click a reference connector; connectors wired to it shift so their wires "
            "become horizontal/vertical"));
        connect(alignBtn, &QToolButton::clicked, this, [this] {
            editor_->beginAlignRouting();
            canvas_->setFocus(Qt::OtherFocusReason);
        });
        modG->addWidget(alignBtn, 0, 0);
        // Validate button: schematic design-rule check.
        auto* valBtn = new QToolButton(modW);
        valBtn->setText(QStringLiteral("Validate"));
        valBtn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        valBtn->setIconSize(QSize(26, 26));
        valBtn->setMinimumWidth(66);
        valBtn->setToolTip(QStringLiteral(
            "Check the schematic: every connector pin wired or X-terminated, no duplicate "
            "pin labels, no open wires"));
        {
            QPixmap pm(28, 28);
            pm.fill(Qt::transparent);
            QPainter ip(&pm);
            ip.setRenderHint(QPainter::Antialiasing, true);
            QPen pen(QColor("#7ee0a0"));
            pen.setWidthF(2.4);
            pen.setCapStyle(Qt::RoundCap);
            pen.setJoinStyle(Qt::RoundJoin);
            ip.setPen(pen);
            ip.drawLine(6, 15, 12, 21); // check mark
            ip.drawLine(12, 21, 23, 7);
            ip.end();
            valBtn->setIcon(QIcon(pm));
        }
        connect(valBtn, &QToolButton::clicked, this, [this] { showValidation(); });
        modG->addWidget(valBtn, 0, 1);
        addGroup(lay, QStringLiteral("Modify"), modW);
        lay->addStretch(1);
        ribbon_->addTab(page, QStringLiteral("Schematic build"));
    }
    // (Parametric and Output ribbon tabs were removed — file New/Open/Save and the
    //  DXF/PDF/CSV exports live on the E (application) menu and the command line.)

    QToolBar* top = addToolBar(QStringLiteral("Ribbon"));
    top->setMovable(false);
    top->setFloatable(false);
    top->addWidget(ribbon_);
    buildHatchRibbonTab(); // built once, added to the ribbon only while hatching
    buildTableCellRibbonTab(); // built once, added only while a table cell is selected
    buildConnectorRibbonTab(); // built once, added only while placing/editing a connector
    // Drafting toggles + grid size now live in the bottom-right status bar
    // (see buildStatusControls()).
}

// The contextual "Hatch Creation" ribbon tab (AutoCAD-style): Boundaries | Pattern
// | Properties | Options | Close. Built once and parented to the ribbon, but only
// inserted as a visible tab while the Hatch tool is active (showHatchRibbon).
void MainWindow::buildHatchRibbonTab() {
    hatchTab_ = new QWidget(ribbon_);
    auto* lay = new QHBoxLayout(hatchTab_);
    lay->setContentsMargins(6, 4, 6, 2);
    lay->setSpacing(0);
    lay->setAlignment(Qt::AlignLeft);
    auto addGroup = [&](const QString& title, QWidget* content) {
        lay->addWidget(ribbonGroup(title, content));
        auto* rule = new QFrame(hatchTab_);
        rule->setFrameShape(QFrame::VLine);
        rule->setStyleSheet(QStringLiteral("color:#b3afa6;"));
        lay->addWidget(rule);
    };
    auto bigButton = [&](QWidget* parent, const QIcon& icon, const QString& text) {
        auto* b = new QToolButton(parent);
        b->setIcon(icon);
        b->setText(text);
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setIconSize(QSize(28, 28));
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    };

    // ---- Boundaries: Pick Points / Select ----
    {
        auto* w = new QWidget(hatchTab_);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0);
        g->setSpacing(1);
        // Checkable + exclusive so the active boundary mode is clearly highlighted.
        auto* bmGroup = new QButtonGroup(this);
        bmGroup->setExclusive(true);
        hatchPickBtn_ = bigButton(w, makeToolIcon(ToolId::Hatch), QStringLiteral("Pick\nPoints"));
        hatchPickBtn_->setCheckable(true);
        bmGroup->addButton(hatchPickBtn_);
        connect(hatchPickBtn_, &QToolButton::clicked, this, [this] {
            editor_->hatchStyle().boundary = Editor::HatchBoundary::PickPoints;
            editor_->setTool(ToolId::Hatch);
            syncHatchRibbon();
        });
        hatchSelectBtn_ = bigButton(w, makeToolIcon(ToolId::Select), QStringLiteral("Select"));
        hatchSelectBtn_->setCheckable(true);
        bmGroup->addButton(hatchSelectBtn_);
        connect(hatchSelectBtn_, &QToolButton::clicked, this, [this] {
            editor_->hatchStyle().boundary = Editor::HatchBoundary::SelectObject;
            editor_->setTool(ToolId::Hatch);
            syncHatchRibbon();
        });
        g->addWidget(hatchPickBtn_, 0, 0);
        g->addWidget(hatchSelectBtn_, 0, 1);
        addGroup(QStringLiteral("Boundaries"), w);
    }

    // ---- Pattern swatches (exclusive) — all patterns, horizontally scrollable ----
    {
        auto* w = new QWidget(hatchTab_); // inner widget that scrolls
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0);
        g->setSpacing(1);
        hatchPatternGroup_ = new QButtonGroup(this);
        hatchPatternGroup_->setExclusive(true);
        const QList<QPair<QString, QString>> pats = hatchPatternList();
        const int perRow = (pats.size() + 1) / 2; // lay all patterns out in two rows
        for (int i = 0; i < pats.size(); ++i) {
            const QString key = pats[i].second;
            auto* b = bigButton(w, makeHatchSwatch(key, 60, 38), QString()); // swatch only
            b->setToolButtonStyle(Qt::ToolButtonIconOnly); // big swatch, name in tooltip
            b->setToolTip(pats[i].first);
            b->setIconSize(QSize(60, 38));                 // larger so the pattern reads clearly
            b->setCheckable(true);
            b->setProperty("hatchKey", key);
            if (editor_->hatchStyle().pattern == key) b->setChecked(true);
            hatchPatternGroup_->addButton(b);
            connect(b, &QToolButton::clicked, this, [this, key] { choosePattern(key); });
            g->addWidget(b, i / perRow, i % perRow);
        }
        // Scroll the swatch grid horizontally so every pattern is reachable inline.
        auto* scroll = new QScrollArea(hatchTab_);
        scroll->setWidget(w);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setMaximumWidth(440);
        scroll->setStyleSheet(QStringLiteral("QScrollArea{background:transparent;}"));

        // Holder: the selected pattern's NAME on top, then the scroll + expand row.
        auto* holder = new QWidget(hatchTab_);
        auto* vbox = new QVBoxLayout(holder);
        vbox->setContentsMargins(0, 0, 0, 0);
        vbox->setSpacing(1);
        hatchPatternLabel_ = new QLabel(holder);
        hatchPatternLabel_->setStyleSheet(
            QStringLiteral("color:#eef0f3; font-weight:bold; background:transparent;"));
        vbox->addWidget(hatchPatternLabel_);

        auto* rowW = new QWidget(holder);
        auto* hl = new QHBoxLayout(rowW);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(2);
        hl->addWidget(scroll, 1);
        auto* more = new QToolButton(rowW);
        more->setText(QString::fromUtf8("\xE2\x8B\xAE")); // vertical ellipsis
        more->setToolTip(QStringLiteral("More patterns…"));
        more->setAutoRaise(true);
        more->setFocusPolicy(Qt::NoFocus);
        more->setMinimumHeight(64);
        connect(more, &QToolButton::clicked, this, [this] { showHatchPatternDialog(); });
        hl->addWidget(more);
        vbox->addWidget(rowW, 1);
        addGroup(QStringLiteral("Pattern"), holder);
    }

    // ---- Properties: Angle / Scale / Transparency ----
    {
        auto* w = new QWidget(hatchTab_);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0);
        g->setSpacing(2);
        // Wrap a spin box with explicit, clearly-marked + / - stepper buttons.
        const QString stepQss = QStringLiteral(
            "QToolButton{background:#6a6f78;color:#f4f6f9;border:1px solid #868c96;"
            "border-radius:2px;font:bold 11px;padding:0;margin:0;}"
            "QToolButton:hover{background:#7c828c;}"
            "QToolButton:pressed{background:#3d8bfd;}");
        auto wrapSpin = [&](QAbstractSpinBox* spin) -> QWidget* {
            spin->setButtonSymbols(QAbstractSpinBox::NoButtons); // we draw +/- ourselves
            auto* box = new QWidget(w);
            auto* row = new QHBoxLayout(box);
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(2);
            auto* stack = new QVBoxLayout();
            stack->setContentsMargins(0, 0, 0, 0);
            stack->setSpacing(1);
            auto* plus = new QToolButton(box);
            plus->setText(QStringLiteral("+"));
            plus->setAutoRepeat(true);
            plus->setFixedSize(20, 12);
            plus->setStyleSheet(stepQss);
            plus->setFocusPolicy(Qt::NoFocus);
            auto* minus = new QToolButton(box);
            minus->setText(QString::fromUtf8("\xE2\x88\x92")); // "−"
            minus->setAutoRepeat(true);
            minus->setFixedSize(20, 12);
            minus->setStyleSheet(stepQss);
            minus->setFocusPolicy(Qt::NoFocus);
            connect(plus, &QToolButton::clicked, this, [spin] { spin->stepUp(); });
            connect(minus, &QToolButton::clicked, this, [spin] { spin->stepDown(); });
            stack->addWidget(plus);
            stack->addWidget(minus);
            row->addWidget(spin, 1);
            row->addLayout(stack);
            return box;
        };
        hatchAngleSpin_ = new QDoubleSpinBox(w);
        hatchAngleSpin_->setRange(0, 180);
        hatchAngleSpin_->setSingleStep(5);
        hatchAngleSpin_->setDecimals(0);
        hatchAngleSpin_->setValue(editor_->hatchStyle().angle);
        connect(hatchAngleSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this](double v) { applyHatchControl(QStringLiteral("hatchAngle"), v); });
        hatchScaleSpin_ = new QDoubleSpinBox(w);
        hatchScaleSpin_->setRange(0.1, 50);
        hatchScaleSpin_->setSingleStep(0.25);
        hatchScaleSpin_->setDecimals(2);
        hatchScaleSpin_->setValue(editor_->hatchStyle().scale);
        connect(hatchScaleSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
                [this](double v) { applyHatchControl(QStringLiteral("hatchScale"), v); });
        hatchTranspSpin_ = new QSpinBox(w);
        hatchTranspSpin_->setRange(0, 95);
        hatchTranspSpin_->setSingleStep(5);
        hatchTranspSpin_->setValue(editor_->hatchStyle().transparency);
        connect(hatchTranspSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
                [this](int v) { applyHatchControl(QStringLiteral("transparency"), v); });
        // Color: ByLayer / ByBlock + standard colors, each with a swatch example.
        hatchColorCombo_ = new QComboBox(w);
        hatchColorCombo_->setMinimumWidth(140); // show the swatch + full label
        hatchColorCombo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        hatchColorCombo_->addItem(makeColorSwatchIcon(QColor(), true), QStringLiteral("ByLayer"),
                                  QString());
        hatchColorCombo_->addItem(makeColorSwatchIcon(QColor(), true), QStringLiteral("ByBlock"),
                                  QStringLiteral("byblock"));
        const QList<QPair<QString, QString>> cols = {
            {QStringLiteral("Red"), QStringLiteral("#ff0000")},
            {QStringLiteral("Yellow"), QStringLiteral("#ffff00")},
            {QStringLiteral("Green"), QStringLiteral("#00ff00")},
            {QStringLiteral("Cyan"), QStringLiteral("#00ffff")},
            {QStringLiteral("Blue"), QStringLiteral("#3d8bfd")},
            {QStringLiteral("Magenta"), QStringLiteral("#ff00ff")},
            {QStringLiteral("White"), QStringLiteral("#ffffff")},
            {QStringLiteral("Gray"), QStringLiteral("#808080")},
        };
        for (const auto& c : cols)
            hatchColorCombo_->addItem(makeColorSwatchIcon(QColor(c.second), false), c.first, c.second);
        connect(hatchColorCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this](int idx) {
                    if (idx < 0) return;
                    applyHatchControl(QStringLiteral("color"), hatchColorCombo_->itemData(idx).toString());
                });

        auto propLabel = [&](const QString& t) {
            auto* l = new QLabel(t, w);
            l->setStyleSheet(QStringLiteral("color:#e6e9ef; background:transparent;"));
            return l;
        };
        g->addWidget(propLabel(QStringLiteral("Angle")), 0, 0);
        g->addWidget(wrapSpin(hatchAngleSpin_), 0, 1);
        g->addWidget(propLabel(QStringLiteral("Scale")), 1, 0);
        g->addWidget(wrapSpin(hatchScaleSpin_), 1, 1);
        g->addWidget(propLabel(QStringLiteral("Transp.")), 2, 0);
        g->addWidget(wrapSpin(hatchTranspSpin_), 2, 1);
        g->addWidget(propLabel(QStringLiteral("Color")), 3, 0);
        g->addWidget(hatchColorCombo_, 3, 1);
        addGroup(QStringLiteral("Properties"), w);
    }

    // ---- Options: Associative + Multiple joints ----
    {
        auto* w = new QWidget(hatchTab_);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0);
        g->setSpacing(1);
        hatchAssocBtn_ = bigButton(w, makeToolIcon(ToolId::Offset), QStringLiteral("Associative"));
        hatchAssocBtn_->setCheckable(true);
        hatchAssocBtn_->setChecked(editor_->hatchStyle().associative);
        connect(hatchAssocBtn_, &QToolButton::toggled, this,
                [this](bool on) { applyHatchControl(QStringLiteral("associative"), on); });
        // Many joints: ON keeps dense boundary joints on curves (current); OFF
        // reduces them (a circle -> 4 joints). Applies to newly created hatches.
        hatchJointsBtn_ = bigButton(w, makeToolIcon(ToolId::Spline), QStringLiteral("Multiple\njoints"));
        hatchJointsBtn_->setCheckable(true);
        hatchJointsBtn_->setChecked(editor_->hatchStyle().manyJoints);
        hatchJointsBtn_->setToolTip(QStringLiteral(
            "On: many joints on curved boundaries (current).  Off: few joints "
            "(circle = 4). Applies to newly created hatches."));
        connect(hatchJointsBtn_, &QToolButton::toggled, this,
                [this](bool on) { editor_->hatchStyle().manyJoints = on; });
        // Both option buttons share one size so they line up evenly.
        const QSize optSize(92, 80);
        hatchAssocBtn_->setFixedSize(optSize);
        hatchJointsBtn_->setFixedSize(optSize);
        g->addWidget(hatchAssocBtn_, 0, 0);
        g->addWidget(hatchJointsBtn_, 0, 1);
        addGroup(QStringLiteral("Options"), w);
    }

    // ---- Close Hatch Creation ----
    {
        auto* w = new QWidget(hatchTab_);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0);
        g->setSpacing(1);
        auto* close = bigButton(w, makeToolIcon(ToolId::NotConnected), QStringLiteral("Close Hatch\nCreation"));
        connect(close, &QToolButton::clicked, this, [this] {
            // Finish the hatch command: leave the tool, drop the live-edit target,
            // close the contextual tab and return to the canvas.
            editor_->clearSelection();
            editor_->setTool(ToolId::Select); // -> toolChanged -> updateHatchContext hides the tab
            hatchEditId_.clear();
            showHatchRibbon(false);
            canvas_->setFocus();
        });
        g->addWidget(close, 0, 0);
        addGroup(QStringLiteral("Close"), w);
    }

    lay->addStretch(1);
    hatchTab_->hide();
}

void MainWindow::syncHatchRibbon() {
    if (!hatchTab_) return;
    // Read from the selected hatch being edited, else from the new-hatch defaults.
    QString pattern = editor_->hatchStyle().pattern;
    double angle = editor_->hatchStyle().angle;
    double scale = editor_->hatchStyle().scale;
    int transp = editor_->hatchStyle().transparency;
    bool assoc = editor_->hatchStyle().associative;
    QString color = editor_->hatchStyle().color;
    if (!hatchEditId_.isEmpty() && doc_) {
        for (const auto& e : doc_->entities())
            if (e.id == hatchEditId_ && e.type == EntityType::Hatch) {
                pattern = e.meta.value(QStringLiteral("pattern")).toString(pattern);
                angle = e.meta.value(QStringLiteral("hatchAngle")).toDouble(angle);
                scale = e.meta.value(QStringLiteral("hatchScale")).toDouble(scale);
                transp = e.meta.value(QStringLiteral("transparency")).toInt(transp);
                assoc = e.meta.value(QStringLiteral("associative")).toBool(assoc);
                color = e.color;
                break;
            }
    }
    if (hatchPatternGroup_)
        for (QAbstractButton* b : hatchPatternGroup_->buttons())
            if (b->property("hatchKey").toString() == pattern) {
                QSignalBlocker blk(b);
                b->setChecked(true);
            }
    if (hatchPatternLabel_) {
        QString name = pattern;
        for (const auto& pp : hatchPatternList())
            if (pp.second == pattern) { name = pp.first; break; }
        hatchPatternLabel_->setText(QStringLiteral("Pattern:  %1").arg(name));
    }
    if (hatchAngleSpin_) { QSignalBlocker b(hatchAngleSpin_); hatchAngleSpin_->setValue(angle); }
    if (hatchScaleSpin_) { QSignalBlocker b(hatchScaleSpin_); hatchScaleSpin_->setValue(scale); }
    if (hatchTranspSpin_) { QSignalBlocker b(hatchTranspSpin_); hatchTranspSpin_->setValue(transp); }
    if (hatchAssocBtn_) { QSignalBlocker b(hatchAssocBtn_); hatchAssocBtn_->setChecked(assoc); }
    if (hatchJointsBtn_) {
        QSignalBlocker b(hatchJointsBtn_);
        hatchJointsBtn_->setChecked(editor_->hatchStyle().manyJoints);
    }
    // Reflect the active boundary mode (Pick Points vs Select).
    const bool sel = editor_->hatchStyle().boundary == Editor::HatchBoundary::SelectObject;
    if (hatchPickBtn_) { QSignalBlocker b(hatchPickBtn_); hatchPickBtn_->setChecked(!sel); }
    if (hatchSelectBtn_) { QSignalBlocker b(hatchSelectBtn_); hatchSelectBtn_->setChecked(sel); }
    if (hatchColorCombo_) {
        QSignalBlocker b(hatchColorCombo_);
        int idx = hatchColorCombo_->findData(color);
        hatchColorCombo_->setCurrentIndex(idx >= 0 ? idx : 0); // default ByLayer
    }
}

// Apply a control change to the new-hatch defaults AND, if a hatch is being edited
// (selected, or just created in the session), to that hatch in place; then repaint.
void MainWindow::applyHatchControl(const QString& key, const QJsonValue& value) {
    auto& hs = editor_->hatchStyle();
    if (key == QLatin1String("pattern")) hs.pattern = value.toString();
    else if (key == QLatin1String("hatchAngle")) hs.angle = value.toDouble();
    else if (key == QLatin1String("hatchScale")) hs.scale = value.toDouble();
    else if (key == QLatin1String("transparency")) hs.transparency = value.toInt();
    else if (key == QLatin1String("associative")) hs.associative = value.toBool();
    else if (key == QLatin1String("color")) hs.color = value.toString();

    if (!hatchEditId_.isEmpty()) {
        if (key == QLatin1String("color"))
            editor_->setEntityColor(hatchEditId_, value.toString());
        else
            editor_->setHatchMeta(hatchEditId_, key, value);
    }
    canvas_->update();
}

// Pick a pattern: change the hatch being edited in place, or set the new-hatch
// default and start the Hatch tool. Keeps the ribbon swatches in sync.
void MainWindow::choosePattern(const QString& key) {
    if (!hatchEditId_.isEmpty()) {
        applyHatchControl(QStringLiteral("pattern"), key);
    } else {
        editor_->hatchStyle().pattern = key;
        if (editor_->tool() != ToolId::Hatch) editor_->setTool(ToolId::Hatch);
        canvas_->update();
    }
    syncHatchRibbon(); // re-check the matching inline swatch
}

// Expanded pattern picker: a big swatch gallery plus a side list of pattern names
// (a "fall menu") — both synced. Double-click or OK applies.
void MainWindow::showHatchPatternDialog() {
    const auto pats = hatchPatternList();
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Hatch Pattern"));
    dlg.resize(560, 380);
    auto* outer = new QVBoxLayout(&dlg);
    auto* row = new QHBoxLayout();
    outer->addLayout(row, 1);

    // Side "fall menu" of pattern names.
    auto* names = new QListWidget(&dlg);
    names->setFixedWidth(150);
    for (const auto& pp : pats) names->addItem(pp.first);

    // Gallery of large swatches.
    auto* gallery = new QListWidget(&dlg);
    gallery->setViewMode(QListView::IconMode);
    gallery->setIconSize(QSize(110, 70));
    gallery->setGridSize(QSize(130, 104));
    gallery->setResizeMode(QListView::Adjust);
    gallery->setMovement(QListView::Static);
    gallery->setSpacing(6);
    gallery->setWordWrap(true);
    for (const auto& pp : pats) {
        auto* it = new QListWidgetItem(makeHatchSwatch(pp.second, 110, 70), pp.first, gallery);
        it->setData(Qt::UserRole, pp.second);
        it->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    }
    row->addWidget(names);
    row->addWidget(gallery, 1);

    // Pre-select the current pattern in both.
    int cur = 0;
    for (int i = 0; i < pats.size(); ++i)
        if (pats[i].second == editor_->hatchStyle().pattern) { cur = i; break; }
    if (!hatchEditId_.isEmpty() && doc_)
        for (const auto& e : doc_->entities())
            if (e.id == hatchEditId_)
                for (int i = 0; i < pats.size(); ++i)
                    if (pats[i].second == e.meta.value(QStringLiteral("pattern")).toString())
                        cur = i;
    names->setCurrentRow(cur);
    gallery->setCurrentRow(cur);

    // Keep the two views in lock-step.
    bool guard = false;
    connect(names, &QListWidget::currentRowChanged, &dlg, [&](int r) {
        if (guard || r < 0) return;
        guard = true; gallery->setCurrentRow(r); guard = false;
    });
    connect(gallery, &QListWidget::currentRowChanged, &dlg, [&](int r) {
        if (guard || r < 0) return;
        guard = true; names->setCurrentRow(r); guard = false;
    });
    // Double-click a swatch or a name applies and closes.
    connect(gallery, &QListWidget::itemDoubleClicked, &dlg, [&](QListWidgetItem*) { dlg.accept(); });
    connect(names, &QListWidget::itemDoubleClicked, &dlg, [&](QListWidgetItem*) { dlg.accept(); });

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    outer->addWidget(bb);

    if (dlg.exec() == QDialog::Accepted) {
        const int r = gallery->currentRow();
        if (r >= 0 && r < pats.size()) choosePattern(pats[r].second);
    }
}

// Open/close the contextual tab and decide its edit target: the Hatch tool edits
// the new-hatch defaults; a single selected hatch (in Select) edits that hatch.
void MainWindow::updateHatchContext() {
    const bool creating = editor_->tool() == ToolId::Hatch;
    QString selHatch;
    if (!creating && doc_ && editor_->selection().size() == 1) {
        const QString id = editor_->selection().front();
        for (const auto& e : doc_->entities())
            if (e.id == id && e.type == EntityType::Hatch) { selHatch = id; break; }
    }
    if (creating) {
        // Live-edit the most recently created hatch in this session (if any), so
        // pattern/angle/etc keep previewing on it until you click out or Close.
        hatchEditId_ = editor_->lastHatchId();
        showHatchRibbon(true);
    } else if (!selHatch.isEmpty()) {
        hatchEditId_ = selHatch;
        showHatchRibbon(true);
    } else {
        hatchEditId_.clear();
        showHatchRibbon(false);
    }
}

void MainWindow::showHatchRibbon(bool on) {
    if (!hatchTab_ || !ribbon_) return;
    const int idx = ribbon_->indexOf(hatchTab_);
    if (on) {
        syncHatchRibbon();
        if (idx < 0) {
            const int at = ribbon_->addTab(hatchTab_, hatchEditId_.isEmpty()
                                                           ? QStringLiteral("Hatch Creation")
                                                           : QStringLiteral("Hatch Editor"));
            ribbon_->setCurrentIndex(at);
        } else {
            ribbon_->setTabText(idx, hatchEditId_.isEmpty() ? QStringLiteral("Hatch Creation")
                                                            : QStringLiteral("Hatch Editor"));
            ribbon_->setCurrentIndex(idx);
        }
        hatchTab_->show();
    } else if (idx >= 0) {
        ribbon_->removeTab(idx); // detaches but does not delete hatchTab_
        ribbon_->setCurrentIndex(0); // back to Home
    }
}

// The contextual "Table Cell" ribbon tab (AutoCAD-style): Rows | Columns | Cell
// Styles | Close. Built once and parented to the ribbon, inserted as a visible tab
// only while a single table cell is selected (showTableCellRibbon).
void MainWindow::buildTableCellRibbonTab() {
    tableCellTab_ = new QWidget(ribbon_);
    auto* lay = new QHBoxLayout(tableCellTab_);
    lay->setContentsMargins(6, 4, 6, 2);
    lay->setSpacing(0);
    lay->setAlignment(Qt::AlignLeft);
    auto addGroup = [&](const QString& title, QWidget* content) {
        lay->addWidget(ribbonGroup(title, content));
        auto* rule = new QFrame(tableCellTab_);
        rule->setFrameShape(QFrame::VLine);
        rule->setStyleSheet(QStringLiteral("color:#b3afa6;"));
        lay->addWidget(rule);
    };
    auto bigButton = [&](QWidget* parent, const QString& text) {
        auto* b = new QToolButton(parent);
        b->setText(text);
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setMinimumWidth(52);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        return b;
    };

    // ---- Rows: Insert Above / Insert Below / Delete Row ----
    {
        auto* w = new QWidget(tableCellTab_);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0);
        g->setSpacing(1);
        auto* above = bigButton(w, QStringLiteral("Insert\nAbove"));
        auto* below = bigButton(w, QStringLiteral("Insert\nBelow"));
        auto* del = bigButton(w, QStringLiteral("Delete\nRow"));
        connect(above, &QToolButton::clicked, this, [this] { editor_->tableInsertRow(false); });
        connect(below, &QToolButton::clicked, this, [this] { editor_->tableInsertRow(true); });
        connect(del, &QToolButton::clicked, this, [this] { editor_->tableDeleteRow(); });
        g->addWidget(above, 0, 0);
        g->addWidget(below, 0, 1);
        g->addWidget(del, 0, 2);
        addGroup(QStringLiteral("Rows"), w);
    }

    // ---- Columns: Insert Left / Insert Right / Delete Column ----
    {
        auto* w = new QWidget(tableCellTab_);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0);
        g->setSpacing(1);
        auto* left = bigButton(w, QStringLiteral("Insert\nLeft"));
        auto* right = bigButton(w, QStringLiteral("Insert\nRight"));
        auto* del = bigButton(w, QStringLiteral("Delete\nColumn"));
        connect(left, &QToolButton::clicked, this, [this] { editor_->tableInsertColumn(false); });
        connect(right, &QToolButton::clicked, this, [this] { editor_->tableInsertColumn(true); });
        connect(del, &QToolButton::clicked, this, [this] { editor_->tableDeleteColumn(); });
        g->addWidget(left, 0, 0);
        g->addWidget(right, 0, 1);
        g->addWidget(del, 0, 2);
        addGroup(QStringLiteral("Columns"), w);
    }

    // ---- Cell Styles: 9-way alignment dropdown ----
    {
        auto* w = new QWidget(tableCellTab_);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0);
        g->setSpacing(1);
        auto* align = bigButton(w, QStringLiteral("Cell\nAlignment"));
        align->setPopupMode(QToolButton::InstantPopup);
        auto* menu = new QMenu(align);
        const std::pair<const char*, const char*> items[] = {
            {"Top Left", "TL"},    {"Top Center", "TC"},    {"Top Right", "TR"},
            {"Middle Left", "ML"}, {"Middle Center", "MC"}, {"Middle Right", "MR"},
            {"Bottom Left", "BL"}, {"Bottom Center", "BC"}, {"Bottom Right", "BR"}};
        for (const auto& it : items) {
            const QString code = QString::fromLatin1(it.second);
            menu->addAction(QString::fromLatin1(it.first), this,
                            [this, code] { editor_->setCellAlignment(code); });
        }
        align->setMenu(menu);
        g->addWidget(align, 0, 0);
        addGroup(QStringLiteral("Cell Styles"), w);
    }

    // ---- Data: export the table to CSV ----
    {
        auto* w = new QWidget(tableCellTab_);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0);
        g->setSpacing(1);
        auto* csv = bigButton(w, QStringLiteral("Export\nCSV"));
        connect(csv, &QToolButton::clicked, this, [this] { exportTableCsv(); });
        g->addWidget(csv, 0, 0);
        addGroup(QStringLiteral("Data"), w);
    }

    // ---- Close: dismiss the cell selection ----
    {
        auto* w = new QWidget(tableCellTab_);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0);
        g->setSpacing(1);
        auto* close = bigButton(w, QStringLiteral("Close"));
        connect(close, &QToolButton::clicked, this, [this] { editor_->clearSelection(); });
        g->addWidget(close, 0, 0);
        addGroup(QStringLiteral("Close"), w);
    }

    lay->addStretch(1);
    tableCellTab_->hide();
}

void MainWindow::updateTableCellContext() {
    showTableCellRibbon(!editor_->selectedCellTable().isEmpty() &&
                        editor_->selectedCellIndex() >= 0);
}

void MainWindow::showTableCellRibbon(bool on) {
    if (!tableCellTab_ || !ribbon_) return;
    const int idx = ribbon_->indexOf(tableCellTab_);
    if (on) {
        if (idx < 0) {
            const int at = ribbon_->addTab(tableCellTab_, QStringLiteral("Table Cell"));
            ribbon_->setCurrentIndex(at);
        } else {
            ribbon_->setCurrentIndex(idx);
        }
        tableCellTab_->show();
    } else if (idx >= 0) {
        ribbon_->removeTab(idx); // detaches but does not delete tableCellTab_
        ribbon_->setCurrentIndex(0); // back to Home
    }
}

void MainWindow::buildStatusControls() {
    // "Repeat last command" button: re-activates the last draw/modify function (the
    // app returns to Select after each object; this button — like Space/Enter — starts
    // the last tool again). Its label tracks whatever that tool is.
    repeatBtn_ = new QToolButton(this);
    repeatBtn_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    repeatBtn_->setText(QStringLiteral("↻ Repeat"));
    repeatBtn_->setToolTip(QStringLiteral("Repeat the last command (also Enter / Space)"));
    repeatBtn_->setAutoRaise(true);
    repeatBtn_->setFocusPolicy(Qt::NoFocus);
    repeatBtn_->setStyleSheet(
        QStringLiteral("QToolButton{padding:2px 8px;border:none;color:#c9ccd1;}"
                       "QToolButton:hover{background:#243245;}"));
    connect(repeatBtn_, &QToolButton::clicked, this, [this] { editor_->repeatLastTool(); });
    connect(editor_, &Editor::toolChanged, this, [this](ToolId) {
        const ToolId lt = editor_->lastTool();
        repeatBtn_->setText(lt == ToolId::Select ? QStringLiteral("↻ Repeat")
                                                 : QStringLiteral("↻ %1").arg(toolLabel(lt)));
        repeatBtn_->setEnabled(lt != ToolId::Select);
    });
    repeatBtn_->setEnabled(false);
    statusBar()->addPermanentWidget(repeatBtn_);

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
        // Each snap type is a QCheckBox embedded in the menu (a QWidgetAction), so a
        // click toggles it WITHOUT closing the menu — flip several quickly.
        auto addSnap = [&](const QString& label, const QString& kind, bool SnapSettings::*field) {
            auto* row = new QWidget(menu);
            auto* h = new QHBoxLayout(row);
            h->setContentsMargins(8, 3, 12, 3);
            h->setSpacing(6);
            auto* icon = new QLabel(row);
            icon->setPixmap(makeOsnapMarkerIcon(kind).pixmap(16, 16));
            auto* cb = new QCheckBox(label, row);
            cb->setChecked(editor_->snapSettings().*field);
            cb->setStyleSheet(QStringLiteral("QCheckBox{color:#c9ccd1;} QLabel{background:transparent;}"));
            connect(cb, &QCheckBox::toggled, this,
                    [this, field](bool on) { editor_->snapSettings().*field = on; });
            h->addWidget(icon);
            h->addWidget(cb, 1);
            auto* wa = new QWidgetAction(menu);
            wa->setDefaultWidget(row);
            menu->addAction(wa);
        };
        addSnap(QStringLiteral("Endpoint"), QStringLiteral("endpoint"), &SnapSettings::snapEndpoint);
        addSnap(QStringLiteral("Midpoint"), QStringLiteral("midpoint"), &SnapSettings::snapMidpoint);
        addSnap(QStringLiteral("Center"), QStringLiteral("center"), &SnapSettings::snapCenter);
        addSnap(QStringLiteral("Geometric Center"), QStringLiteral("center"),
                &SnapSettings::snapGeometricCenter);
        addSnap(QStringLiteral("Node"), QStringLiteral("endpoint"), &SnapSettings::snapNode);
        addSnap(QStringLiteral("Quadrant"), QStringLiteral("center"), &SnapSettings::snapQuadrant);
        addSnap(QStringLiteral("Intersection"), QStringLiteral("intersection"), &SnapSettings::snapIntersection);
        addSnap(QStringLiteral("Insertion"), QStringLiteral("endpoint"), &SnapSettings::snapInsertion);
        addSnap(QStringLiteral("Perpendicular"), QStringLiteral("nearest"),
                &SnapSettings::snapPerpendicular);
        addSnap(QStringLiteral("Tangent"), QStringLiteral("center"), &SnapSettings::snapTangent);
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
    fileMenu->addAction(QStringLiteral("Export &PDF..."), this, [this] { exportPdf(false); });
    fileMenu->addAction(QStringLiteral("Export PDF (&window)..."), this, [this] { exportPdf(true); });
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
    // No bare "F" shortcut: a single-letter accelerator fires before the canvas
    // sees the key, so pressing "f" while drawing would zoom-to-fit (jump to
    // centre). Fit stays available here, via the command line (z / fit / extents),
    // and the canvas right-click "Drawing center". Ctrl+Shift+F is a safe hotkey.
    viewMenu->addAction(QStringLiteral("&Fit drawing"),
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F), this,
                        [this] { canvas_->fitView(); });

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
    // Modify "Operations" icon buttons follow the same ink as the tool icons.
    for (const auto& ob : opButtons_)
        ob.second->setIcon(makeOpIcon(ob.first, ink));

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

bool MainWindow::showTextDialog(bool multiline, QString& text, Editor::TextStyle& style) {
    QDialog dlg(this);
    dlg.setWindowTitle(multiline ? QStringLiteral("Multiline text") : QStringLiteral("Text"));
    dlg.setMinimumWidth(460);
    auto* lay = new QVBoxLayout(&dlg);

    // Font / size / style row.
    auto* row = new QHBoxLayout();
    auto* fontCombo = new QFontComboBox(&dlg);
    FontLibrary::applyTo(fontCombo);
    if (!style.font.isEmpty()) fontCombo->setCurrentFont(QFont(style.font));
    auto* sizeSpin = new QDoubleSpinBox(&dlg);
    sizeSpin->setRange(0.1, 1000.0);
    sizeSpin->setDecimals(2);
    sizeSpin->setValue(style.size > 0 ? style.size : 5.0);
    sizeSpin->setPrefix(QStringLiteral("H "));
    auto* boldBtn = new QToolButton(&dlg);
    boldBtn->setText(QStringLiteral("B"));
    boldBtn->setCheckable(true);
    boldBtn->setChecked(style.bold);
    { QFont bf = boldBtn->font(); bf.setBold(true); boldBtn->setFont(bf); }
    auto* italicBtn = new QToolButton(&dlg);
    italicBtn->setText(QStringLiteral("I"));
    italicBtn->setCheckable(true);
    italicBtn->setChecked(style.italic);
    { QFont itf = italicBtn->font(); itf.setItalic(true); italicBtn->setFont(itf); }
    row->addWidget(fontCombo, 1);
    row->addWidget(sizeSpin);
    row->addWidget(boldBtn);
    row->addWidget(italicBtn);
    lay->addLayout(row);

    // Paragraph formatting row: Justification, Bullets/Numbering, Line spacing.
    auto* fmtRow = new QHBoxLayout();
    auto* justCombo = new QComboBox(&dlg);
    const QVector<QPair<QString, QString>> justs = {
        {"Top Left", "TL"},     {"Top Center", "TC"},     {"Top Right", "TR"},
        {"Middle Left", "ML"},  {"Middle Center", "MC"},  {"Middle Right", "MR"},
        {"Bottom Left", "BL"},  {"Bottom Center", "BC"},  {"Bottom Right", "BR"}};
    for (const auto& j : justs) justCombo->addItem(j.first, j.second);
    justCombo->setCurrentIndex(std::max(0, justCombo->findData(style.justify)));
    auto* listCombo = new QComboBox(&dlg);
    listCombo->addItem(QStringLiteral("No list"), QStringLiteral("none"));
    listCombo->addItem(QStringLiteral("Bullets"), QStringLiteral("bullet"));
    listCombo->addItem(QStringLiteral("Numbered"), QStringLiteral("number"));
    listCombo->setCurrentIndex(std::max(0, listCombo->findData(style.list)));
    auto* spacingSpin = new QDoubleSpinBox(&dlg);
    spacingSpin->setRange(0.5, 4.0);
    spacingSpin->setSingleStep(0.25);
    spacingSpin->setDecimals(2);
    spacingSpin->setValue(style.lineSpacing > 0 ? style.lineSpacing : 1.0);
    spacingSpin->setPrefix(QStringLiteral("Line × "));
    fmtRow->addWidget(new QLabel(QStringLiteral("Justify:"), &dlg));
    fmtRow->addWidget(justCombo, 1);
    fmtRow->addWidget(listCombo);
    fmtRow->addWidget(spacingSpin);
    lay->addLayout(fmtRow);

    // Text body (multiline or single line).
    auto* edit = new QPlainTextEdit(&dlg);
    edit->setPlainText(text);
    if (!multiline) edit->setFixedHeight(40);
    else edit->setMinimumHeight(120);
    lay->addWidget(edit);

    // Case conversion buttons.
    auto* caseRow = new QHBoxLayout();
    auto* upBtn = new QPushButton(QStringLiteral("UPPERCASE"), &dlg);
    auto* loBtn = new QPushButton(QStringLiteral("lowercase"), &dlg);
    auto* titleBtn = new QPushButton(QStringLiteral("Title Case"), &dlg);
    connect(upBtn, &QPushButton::clicked, &dlg,
            [edit] { edit->setPlainText(edit->toPlainText().toUpper()); });
    connect(loBtn, &QPushButton::clicked, &dlg,
            [edit] { edit->setPlainText(edit->toPlainText().toLower()); });
    connect(titleBtn, &QPushButton::clicked, &dlg, [edit] {
        const QStringList words = edit->toPlainText().toLower().split(QLatin1Char(' '));
        QStringList out;
        for (QString w : words) {
            if (!w.isEmpty()) w[0] = w[0].toUpper();
            out << w;
        }
        edit->setPlainText(out.join(QLatin1Char(' ')));
    });
    caseRow->addWidget(upBtn);
    caseRow->addWidget(loBtn);
    caseRow->addWidget(titleBtn);
    caseRow->addStretch();
    lay->addLayout(caseRow);

    // A live font preview applied to the editor itself.
    auto applyPreview = [edit, fontCombo, boldBtn, italicBtn] {
        QFont pf = fontCombo->currentFont();
        pf.setBold(boldBtn->isChecked());
        pf.setItalic(italicBtn->isChecked());
        pf.setPointSize(12);
        edit->setFont(pf);
    };
    applyPreview();
    connect(fontCombo, &QFontComboBox::currentFontChanged, &dlg, [applyPreview] { applyPreview(); });
    connect(boldBtn, &QToolButton::toggled, &dlg, [applyPreview] { applyPreview(); });
    connect(italicBtn, &QToolButton::toggled, &dlg, [applyPreview] { applyPreview(); });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(buttons);

    edit->setFocus();
    if (dlg.exec() != QDialog::Accepted) return false;
    text = edit->toPlainText();
    if (!multiline) text.replace(QLatin1Char('\n'), QLatin1Char(' ')); // single line stays one line
    style.size = sizeSpin->value();
    style.font = fontCombo->currentFont().family();
    style.bold = boldBtn->isChecked();
    style.italic = italicBtn->isChecked();
    style.justify = justCombo->currentData().toString();
    style.list = listCombo->currentData().toString();
    style.lineSpacing = spacingSpin->value();
    return true;
}

void MainWindow::beginInlineText(const QString& id, const QString& text,
                                 const Editor::TextStyle& style, Vec2 topLeft) {
    inlineEditId_ = id;
    inlineCell_ = -1;           // plain text edit unless a table-cell edit sets it afterwards
    inlineConnectorPin_ = -1;   // ... or a connector pin-label edit sets it afterwards
    inlineConnectorId_.clear();
    inlineSingleLine_ = false;  // multi-line unless a single-value edit sets it afterwards
    inlineStyle_ = style;
    inlineTopLeft_ = topLeft;

    if (!inlineEdit_) {
        inlineEdit_ = new QPlainTextEdit(canvas_);
        inlineEdit_->setStyleSheet(QStringLiteral(
            "QPlainTextEdit{background:rgba(18,28,42,215);color:#eaf2ff;border:1px solid #3d8bfd;"
            // A bright, high-contrast highlight so selected text is clearly visible.
            "selection-background-color:#3d8bfd;selection-color:#ffffff;}"));
        // Show the normal arrow cursor over the editor instead of the I-beam
        // "cross" text cursor while editing on-canvas text.
        inlineEdit_->setCursor(Qt::ArrowCursor);
        inlineEdit_->viewport()->setCursor(Qt::ArrowCursor);
        inlineEdit_->installEventFilter(this);
        auto* ruler = new InlineRuler(canvas_);
        // Dragging the ruler's right handle resizes the container width.
        ruler->onWidth = [this](int wpx) {
            const double scale = canvas_->viewScale();
            inlineStyle_.boxWidth = std::max(1.0, wpx / (scale > 1e-9 ? scale : 1.0));
            layoutInlineText();
            inlineEdit_->setFocus();
        };
        inlineRuler_ = ruler;

        inlineBar_ = new QFrame(canvas_);
        inlineBar_->setStyleSheet(QStringLiteral(
            "QFrame{background:#3a3d43;border:1px solid #555;border-radius:3px;}"));
        auto* bl = new QHBoxLayout(inlineBar_);
        bl->setContentsMargins(4, 2, 4, 2);
        bl->setSpacing(4);
        inlineFont_ = new QFontComboBox(inlineBar_);
        inlineFont_->setMaximumWidth(150);
        inlineFont_->setEditable(false); // a font picker, not a text field
        FontLibrary::applyTo(inlineFont_);
        // Build a numeric field with its own visible, stacked +/- buttons ("+" on
        // top, "-" below). Used for both the text height and the line spacing.
        const QString stepQss = QStringLiteral(
            "QToolButton{background:#50545b;color:#f4f4f4;border:1px solid #6a6e76;"
            "border-radius:2px;padding:0;margin:0;font:bold 12px;}"
            "QToolButton:hover{background:#5e636b;}"
            "QToolButton:pressed{background:#3d8bfd;}");
        auto buildStepper = [&](QDoubleSpinBox* spin, QToolButton*& plus,
                                QToolButton*& minus) -> QWidget* {
            spin->setButtonSymbols(QAbstractSpinBox::NoButtons); // we draw our own
            QWidget* box = new QWidget(inlineBar_);
            auto* row = new QHBoxLayout(box);
            row->setContentsMargins(0, 0, 0, 0);
            row->setSpacing(2);
            auto* stack = new QVBoxLayout();
            stack->setContentsMargins(0, 0, 0, 0);
            stack->setSpacing(1);
            plus = new QToolButton(box);
            plus->setText(QStringLiteral("+"));
            plus->setAutoRepeat(true);
            plus->setFixedSize(22, 14);
            plus->setStyleSheet(stepQss);
            minus = new QToolButton(box);
            minus->setText(QString::fromUtf8("\xE2\x88\x92")); // "−"
            minus->setAutoRepeat(true);
            minus->setFixedSize(22, 14);
            minus->setStyleSheet(stepQss);
            stack->addWidget(plus);
            stack->addWidget(minus);
            row->addWidget(spin);
            row->addLayout(stack);
            return box;
        };

        QToolButton *sizePlus = nullptr, *sizeMinus = nullptr;
        QToolButton *spacePlus = nullptr, *spaceMinus = nullptr;

        inlineSize_ = new QDoubleSpinBox(inlineBar_);
        inlineSize_->setRange(0.1, 1000.0);
        inlineSize_->setDecimals(2);
        inlineSize_->setSingleStep(1.0);
        inlineSize_->setPrefix(QStringLiteral("H "));
        inlineSize_->setToolTip(QStringLiteral("Text height"));
        QWidget* sizeBox = buildStepper(inlineSize_, sizePlus, sizeMinus);
        inlineBold_ = new QToolButton(inlineBar_);
        inlineBold_->setText(QStringLiteral("B"));
        inlineBold_->setCheckable(true);
        { QFont bf = inlineBold_->font(); bf.setBold(true); inlineBold_->setFont(bf); }
        inlineItalic_ = new QToolButton(inlineBar_);
        inlineItalic_->setText(QStringLiteral("I"));
        inlineItalic_->setCheckable(true);
        { QFont itf = inlineItalic_->font(); itf.setItalic(true); inlineItalic_->setFont(itf); }
        inlineUnderline_ = new QToolButton(inlineBar_);
        inlineUnderline_->setText(QStringLiteral("U"));
        inlineUnderline_->setCheckable(true);
        { QFont uf = inlineUnderline_->font(); uf.setUnderline(true); inlineUnderline_->setFont(uf); }
        inlineStrike_ = new QToolButton(inlineBar_);
        inlineStrike_->setText(QStringLiteral("S"));
        inlineStrike_->setCheckable(true);
        { QFont sf = inlineStrike_->font(); sf.setStrikeOut(true); inlineStrike_->setFont(sf); }
        inlineJust_ = new QComboBox(inlineBar_);
        for (const char* code : {"TL", "TC", "TR", "ML", "MC", "MR", "BL", "BC", "BR"})
            inlineJust_->addItem(QString::fromLatin1(code), QString::fromLatin1(code));
        inlineList_ = new QComboBox(inlineBar_);
        inlineList_->addItem(QStringLiteral("No list"), QStringLiteral("none"));
        inlineList_->addItem(QStringLiteral("Bullets"), QStringLiteral("bullet"));
        inlineList_->addItem(QStringLiteral("Numbered"), QStringLiteral("number"));
        inlineSpacing_ = new QDoubleSpinBox(inlineBar_);
        inlineSpacing_->setRange(0.5, 4.0);
        inlineSpacing_->setSingleStep(0.25);
        inlineSpacing_->setPrefix(QString::fromUtf8("\xC3\x97 ")); // "× " (line-spacing multiplier)
        inlineSpacing_->setToolTip(QStringLiteral("Line spacing (× the text height)"));
        QWidget* spaceBox = buildStepper(inlineSpacing_, spacePlus, spaceMinus);
        auto* okBtn = new QToolButton(inlineBar_);
        okBtn->setText(QString::fromUtf8("\xE2\x9C\x93")); // checkmark
        auto* cancelBtn = new QToolButton(inlineBar_);
        cancelBtn->setText(QString::fromUtf8("\xE2\x9C\x95")); // x
        for (QWidget* w : {static_cast<QWidget*>(inlineFont_), static_cast<QWidget*>(sizeBox),
                           static_cast<QWidget*>(inlineBold_), static_cast<QWidget*>(inlineItalic_),
                           static_cast<QWidget*>(inlineUnderline_), static_cast<QWidget*>(inlineStrike_),
                           static_cast<QWidget*>(inlineJust_), static_cast<QWidget*>(inlineList_),
                           static_cast<QWidget*>(spaceBox), static_cast<QWidget*>(okBtn),
                           static_cast<QWidget*>(cancelBtn)})
            bl->addWidget(w);

        auto sync = [this] {
            inlineStyle_.font = inlineFont_->currentFont().family();
            inlineStyle_.size = inlineSize_->value();
            inlineStyle_.bold = inlineBold_->isChecked();
            inlineStyle_.italic = inlineItalic_->isChecked();
            inlineStyle_.underline = inlineUnderline_->isChecked();
            inlineStyle_.strike = inlineStrike_->isChecked();
            inlineStyle_.justify = inlineJust_->currentData().toString();
            inlineStyle_.list = inlineList_->currentData().toString();
            inlineStyle_.lineSpacing = inlineSpacing_->value();
            QFont f = inlineFont_->currentFont();
            f.setBold(inlineStyle_.bold);
            f.setItalic(inlineStyle_.italic);
            f.setUnderline(inlineStyle_.underline);
            f.setStrikeOut(inlineStyle_.strike);
            // Keep the on-canvas edit text comfortably readable regardless of zoom.
            f.setPixelSize(std::max(18, static_cast<int>(inlineStyle_.size * canvas_->viewScale())));
            inlineEdit_->setFont(f);
            inlineEdit_->setFocus();
        };
        connect(inlineFont_, &QFontComboBox::currentFontChanged, this, [sync] { sync(); });
        connect(inlineSize_, &QDoubleSpinBox::editingFinished, this, [sync] { sync(); });
        connect(sizePlus, &QToolButton::clicked, this, [this, sync] { inlineSize_->stepUp(); sync(); });
        connect(sizeMinus, &QToolButton::clicked, this, [this, sync] { inlineSize_->stepDown(); sync(); });
        connect(inlineBold_, &QToolButton::toggled, this, [sync] { sync(); });
        connect(inlineItalic_, &QToolButton::toggled, this, [sync] { sync(); });
        connect(inlineUnderline_, &QToolButton::toggled, this, [sync] { sync(); });
        connect(inlineStrike_, &QToolButton::toggled, this, [sync] { sync(); });
        connect(inlineJust_, &QComboBox::currentTextChanged, this, [sync] { sync(); });
        connect(inlineList_, &QComboBox::currentTextChanged, this, [sync] { sync(); });
        connect(inlineSpacing_, &QDoubleSpinBox::editingFinished, this, [sync] { sync(); });
        connect(spacePlus, &QToolButton::clicked, this, [this, sync] { inlineSpacing_->stepUp(); sync(); });
        connect(spaceMinus, &QToolButton::clicked, this, [this, sync] { inlineSpacing_->stepDown(); sync(); });
        connect(okBtn, &QToolButton::clicked, this, [this] { commitInlineText(true); });
        connect(cancelBtn, &QToolButton::clicked, this, [this] { commitInlineText(false); });
    }

    {
        const QSignalBlocker b1(inlineFont_), b2(inlineSize_), b3(inlineBold_), b4(inlineItalic_),
            b5(inlineJust_), b6(inlineList_), b7(inlineSpacing_), b8(inlineUnderline_),
            b9(inlineStrike_);
        if (!style.font.isEmpty()) inlineFont_->setCurrentFont(QFont(style.font));
        inlineSize_->setValue(style.size > 0 ? style.size : 5.0);
        inlineBold_->setChecked(style.bold);
        inlineItalic_->setChecked(style.italic);
        inlineUnderline_->setChecked(style.underline);
        inlineStrike_->setChecked(style.strike);
        inlineJust_->setCurrentIndex(std::max(
            0, inlineJust_->findData(style.justify.isEmpty() ? QStringLiteral("TL") : style.justify)));
        inlineList_->setCurrentIndex(std::max(
            0, inlineList_->findData(style.list.isEmpty() ? QStringLiteral("none") : style.list)));
        inlineSpacing_->setValue(style.lineSpacing > 0 ? style.lineSpacing : 1.0);
    }
    inlineEdit_->setPlainText(text);
    {
        QFont f = inlineFont_->currentFont();
        f.setBold(style.bold);
        f.setItalic(style.italic);
        f.setUnderline(style.underline);
        f.setStrikeOut(style.strike);
        f.setPixelSize(std::max(18, static_cast<int>((style.size > 0 ? style.size : 5.0) *
                                                     canvas_->viewScale())));
        inlineEdit_->setFont(f);
    }
    layoutInlineText();
    inlineRuler_->show();
    inlineBar_->show();
    inlineEdit_->show();
    inlineBar_->raise();
    inlineEdit_->setFocus();
    // Pre-select existing content (shown highlighted) so it's clear what will change and
    // typing overwrites it; a fresh insert just parks the cursor at the end.
    if (!text.isEmpty()) inlineEdit_->selectAll();
    else inlineEdit_->moveCursor(QTextCursor::End);
    canvas_->setInlineEditing(true);
    canvas_->raiseCrosshairOverlay(); // keep the crosshair above the editor toolbar
}

void MainWindow::layoutInlineText() {
    if (!inlineEdit_ || !canvas_) return;
    const double scale = canvas_->viewScale();
    const QPointF tl = canvas_->mapToScreen(inlineTopLeft_);
    // A table-cell edit fits snugly to the cell (small floors, no width ruler); a
    // plain text edit uses comfortable default minimums.
    const bool cell = inlineCell_ >= 0;
    const int minW = cell ? 24 : 140;
    const int minH = cell ? 14 : 44;
    int w = std::max(minW, inlineStyle_.boxWidth > 0
                               ? static_cast<int>(inlineStyle_.boxWidth * scale)
                               : (cell ? minW : 240));
    int h = std::max(minH, inlineStyle_.boxHeight > 0
                               ? static_cast<int>(inlineStyle_.boxHeight * scale)
                               : (cell ? minH : 64));
    const int rulerH = cell ? 0 : 14;
    const int barH = std::max(28, inlineBar_->sizeHint().height());
    const int x = static_cast<int>(tl.x());
    const int y = static_cast<int>(tl.y());
    inlineRuler_->setGeometry(x, y, w, rulerH);
    inlineEdit_->setGeometry(x, y + rulerH, w, h);
    const int barW = std::max(w, inlineBar_->sizeHint().width());
    inlineBar_->setGeometry(x, std::max(0, y - barH - 2), barW, barH);
    canvas_->raiseCrosshairOverlay(); // stay above the editor after any relayout
}

void MainWindow::commitInlineText(bool apply) {
    if (!inlineEdit_ || !inlineEdit_->isVisible()) return;
    const QString text = inlineEdit_->toPlainText();
    canvas_->setInlineEditing(false);
    inlineEdit_->hide();
    inlineBar_->hide();
    inlineRuler_->hide();
    if (apply) {
        if (inlineConnectorPin_ >= 0)
            editor_->applyConnectorPinLabel(inlineConnectorId_, inlineConnectorPin_, text,
                                            inlineStyle_.font); // font library -> pin font

        else if (inlineCell_ >= 0) editor_->applyTableCellEdit(inlineEditId_, inlineCell_, text);
        else if (inlineEditId_.isEmpty()) editor_->commitText(text, inlineStyle_);
        else editor_->applyTextEdit(inlineEditId_, text, inlineStyle_);
    } else if (inlineEditId_.isEmpty() && inlineCell_ < 0 && inlineConnectorPin_ < 0) {
        editor_->commitText(QString(), inlineStyle_); // cancel: clears the pending point/tool
    }
    inlineCell_ = -1;
    inlineConnectorPin_ = -1;
    inlineConnectorId_.clear();
    canvas_->setFocus();
}

bool MainWindow::eventFilter(QObject* obj, QEvent* e) {
    if (obj == inlineEdit_ && e->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(e);
        if (ke->key() == Qt::Key_Escape) { commitInlineText(false); return true; }
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) &&
            ((ke->modifiers() & Qt::ControlModifier) || inlineSingleLine_)) {
            // Ctrl+Enter always commits; plain Enter commits a single-value edit
            // (balloon number / table cell).
            commitInlineText(true);
            return true;
        }
    }
    // Enter while the command autocomplete popup is open submits the TYPED text
    // (so it runs the nearest command in one keystroke) instead of just accepting
    // the highlighted suggestion and waiting for a second Enter.
    if (commandEdit_ && commandEdit_->completer() &&
        obj == commandEdit_->completer()->popup() && e->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(e);
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            const QString t = commandEdit_->text().trimmed();
            commandEdit_->completer()->popup()->hide();
            commandEdit_->clear();
            submitCommand(t);
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, e);
}

void MainWindow::showCanvasMenu(const QPoint& globalPos) {
    if (!editor_) return;

    // A balloon under the cursor — or a selected balloon — enables the leader commands
    // alongside the general ones (right-click on the canvas shows general + leader).
    const Vec2 world = canvas_->worldFromGlobal(globalPos);
    QString balloonId = editor_->balloonAt(world);
    if (!balloonId.isEmpty()) {
        editor_->selectById(balloonId); // clicking a balloon selects it/its batch
    } else if (doc_) {
        for (const auto& id : editor_->selection()) {
            for (const auto& e : doc_->entities())
                if (e.id == id && e.meta.value("balloon").toBool()) { balloonId = id; break; }
            if (!balloonId.isEmpty()) break;
        }
    }

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

    // Leader (multileader) commands, when a balloon is under the cursor / selected.
    if (!balloonId.isEmpty()) {
        menu.addSeparator();
        auto* lm = menu.addMenu(QStringLiteral("Leader"));
        connect(lm->addAction(QStringLiteral("Add leader")), &QAction::triggered, this,
                [this, balloonId] { editor_->addCoupledCircle(balloonId); });
        connect(lm->addAction(QStringLiteral("Delete leader")), &QAction::triggered, this,
                [this] { editor_->removeLeaderFromSelection(); });
        connect(lm->addAction(QStringLiteral("Order leaders in a row")), &QAction::triggered, this,
                [this, balloonId] { editor_->orderBalloonRow(balloonId); });
        connect(lm->addAction(QStringLiteral("Reorder to minimize")), &QAction::triggered, this,
                [this, balloonId] { editor_->reorderBalloonMinimize(balloonId); });
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
    auto* snGeo = new QCheckBox(QStringLiteral("Geometric Center"), draft); snGeo->setChecked(s.snapGeometricCenter);
    auto* snNode = new QCheckBox(QStringLiteral("Node"), draft); snNode->setChecked(s.snapNode);
    auto* snQuad = new QCheckBox(QStringLiteral("Quadrant"), draft); snQuad->setChecked(s.snapQuadrant);
    auto* snInt = new QCheckBox(QStringLiteral("Intersection"), draft); snInt->setChecked(s.snapIntersection);
    auto* snIns = new QCheckBox(QStringLiteral("Insertion"), draft); snIns->setChecked(s.snapInsertion);
    auto* snPerp = new QCheckBox(QStringLiteral("Perpendicular"), draft); snPerp->setChecked(s.snapPerpendicular);
    auto* snTan = new QCheckBox(QStringLiteral("Tangent"), draft); snTan->setChecked(s.snapTangent);
    auto* snNear = new QCheckBox(QStringLiteral("Nearest"), draft); snNear->setChecked(s.snapNearest);
    gf->addRow(QStringLiteral("Snap types"), snEnd);
    gf->addRow(QString(), snMid);
    gf->addRow(QString(), snCen);
    gf->addRow(QString(), snGeo);
    gf->addRow(QString(), snNode);
    gf->addRow(QString(), snQuad);
    gf->addRow(QString(), snInt);
    gf->addRow(QString(), snIns);
    gf->addRow(QString(), snPerp);
    gf->addRow(QString(), snTan);
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
    s.snapGeometricCenter = snGeo->isChecked();
    s.snapNode = snNode->isChecked();
    s.snapQuadrant = snQuad->isChecked();
    s.snapIntersection = snInt->isChecked();
    s.snapInsertion = snIns->isChecked();
    s.snapPerpendicular = snPerp->isChecked();
    s.snapTangent = snTan->isChecked();
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
    // An editable text cell that applies on Enter / focus-out (deferred).
    auto addText = [&](const QString& k, const QString& value,
                       std::function<void(const QString&)> apply) {
        const int r = propsTable_->rowCount();
        propsTable_->insertRow(r);
        auto* key = new QTableWidgetItem(k);
        key->setFlags(Qt::ItemIsEnabled);
        propsTable_->setItem(r, 0, key);
        auto* le = new QLineEdit(propsTable_);
        le->setText(value);
        connect(le, &QLineEdit::editingFinished, this, [this, le, apply] {
            if (refreshing_) return;
            const QString t = le->text();
            QTimer::singleShot(0, this, [apply, t] { apply(t); });
        });
        propsTable_->setCellWidget(r, 1, le);
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

    // A selected connector shows its full editable spec here (so it can be changed
    // without opening the Connector build ribbon).
    if (e && e->type == EntityType::Connector) {
        addHeader(QStringLiteral("Connector"));
        // Change one field of the selected connector's spec and apply it in place.
        auto edit = [this](std::function<void(ConnectorSpec&)> mut) {
            const QString id = editor_->selectedConnectorId();
            if (id.isEmpty()) return;
            ConnectorSpec s;
            for (const auto& en : doc_->entities())
                if (en.id == id) { s = connectorSpecFromMeta(en.meta); break; }
            mut(s);
            s.textSize = std::max({s.pinSize, s.desSize, s.pnSize});
            regenPinLabels(s);
            editor_->applyConnectorSpecToSelection(s);
            if (canvas_) canvas_->update();
        };
        const ConnectorSpec cs = connectorSpecFromMeta(e->meta);
        const QStringList fonts = FontLibrary::families();
        addText(QStringLiteral("Designator"), cs.designator,
                [edit](const QString& t) { edit([t](ConnectorSpec& s) { s.designator = t; }); });
        addText(QStringLiteral("Part number"), cs.partNumber,
                [edit](const QString& t) { edit([t](ConnectorSpec& s) { s.partNumber = t; }); });
        addNum(QStringLiteral("Pins"), cs.pinCount, [edit](double v) {
            edit([v](ConnectorSpec& s) { s.pinCount = std::clamp(int(v + 0.5), 1, 128); });
        });
        addCombo(QStringLiteral("Labels"), {QStringLiteral("Numbers"), QStringLiteral("Letters")},
                 cs.annotation == QLatin1String("letters") ? QStringLiteral("Letters")
                                                           : QStringLiteral("Numbers"),
                 false, [edit](const QString& t) {
                     edit([t](ConnectorSpec& s) {
                         s.annotation = t == QLatin1String("Letters") ? QStringLiteral("letters")
                                                                     : QStringLiteral("numbers");
                     });
                 });
        addCombo(QStringLiteral("Order"), {QStringLiteral("Top"), QStringLiteral("Bottom")},
                 cs.pinOrder == QLatin1String("bottom") ? QStringLiteral("Bottom")
                                                       : QStringLiteral("Top"),
                 false, [edit](const QString& t) {
                     edit([t](ConnectorSpec& s) {
                         s.pinOrder = t == QLatin1String("Bottom") ? QStringLiteral("bottom")
                                                                  : QStringLiteral("top");
                     });
                 });
        addNum(QStringLiteral("Pitch"), cs.pitch,
               [edit](double v) { edit([v](ConnectorSpec& s) { s.pitch = std::max(1.0, v); }); });
        addNum(QStringLiteral("Pin length"), cs.pinLength,
               [edit](double v) { edit([v](ConnectorSpec& s) { s.pinLength = std::max(1.0, v); }); });
        addCombo(QStringLiteral("Body"), {QStringLiteral("Dynamic"), QStringLiteral("Custom")},
                 cs.rectMode == QLatin1String("custom") ? QStringLiteral("Custom")
                                                       : QStringLiteral("Dynamic"),
                 false, [edit](const QString& t) {
                     edit([t](ConnectorSpec& s) {
                         s.rectMode = t == QLatin1String("Custom") ? QStringLiteral("custom")
                                                                  : QStringLiteral("dynamic");
                     });
                 });
        addNum(QStringLiteral("Width"), cs.rectWidth, [edit](double v) {
            edit([v](ConnectorSpec& s) { s.rectMode = QStringLiteral("custom"); s.rectWidth = std::max(1.0, v); });
        });
        addNum(QStringLiteral("Height"), cs.rectHeight, [edit](double v) {
            edit([v](ConnectorSpec& s) { s.rectMode = QStringLiteral("custom"); s.rectHeight = std::max(1.0, v); });
        });
        addCombo(QStringLiteral("Associative"), {QStringLiteral("Yes"), QStringLiteral("No")},
                 cs.associative ? QStringLiteral("Yes") : QStringLiteral("No"), false,
                 [edit](const QString& t) {
                     edit([t](ConnectorSpec& s) { s.associative = (t == QLatin1String("Yes")); });
                 });
        addCombo(QStringLiteral("Pin font"), fonts, cs.pinFont, false,
                 [edit](const QString& t) { edit([t](ConnectorSpec& s) { s.pinFont = t; }); });
        addNum(QStringLiteral("Pin size"), cs.pinSize,
               [edit](double v) { edit([v](ConnectorSpec& s) { s.pinSize = std::max(1.0, v); }); });
        addCombo(QStringLiteral("Des. font"), fonts, cs.desFont, false,
                 [edit](const QString& t) { edit([t](ConnectorSpec& s) { s.desFont = t; }); });
        addNum(QStringLiteral("Des. size"), cs.desSize,
               [edit](double v) { edit([v](ConnectorSpec& s) { s.desSize = std::max(1.0, v); }); });
        addCombo(QStringLiteral("P/N font"), fonts, cs.pnFont, false,
                 [edit](const QString& t) { edit([t](ConnectorSpec& s) { s.pnFont = t; }); });
        addNum(QStringLiteral("P/N size"), cs.pnSize,
               [edit](double v) { edit([v](ConnectorSpec& s) { s.pnSize = std::max(1.0, v); }); });
    }

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
        // Double line / double polyline: the parallel spacing between the two runs.
        const bool isDoubleLine = e->type == EntityType::DoubleLine;
        const bool isDoublePoly =
            e->type == EntityType::Polyline && e->meta.value(QStringLiteral("double")).toBool();
        if (isDoubleLine || isDoublePoly)
            addNum(QStringLiteral("Gap"), e->meta.value(QStringLiteral("gap")).toDouble(10.0),
                   [this](double v) {
                       editor_->setSelectionMetaNumber(QStringLiteral("gap"), std::max(0.1, v));
                   });
        // Balloon callout: arrowhead type/size, leader landing length + the number.
        if (e->meta.value(QStringLiteral("balloon")).toBool()) {
            // Arrowhead type (fall menu). Canonical values map to friendly labels.
            static const QVector<QPair<QString, QString>> arrowTypes = {
                {"Closed filled", "closed"}, {"Closed blank", "blank"}, {"Dot", "dot"},
                {"Architectural tick", "tick"}, {"Oblique", "tick"}, {"Open", "open"},
                {"None", "none"}};
            const QString curAh = e->meta.value(QStringLiteral("arrowhead")).toString("closed");
            QVector<ComboEntry> ahItems;
            for (const auto& a : arrowTypes)
                ahItems.push_back({a.first, makeArrowheadIcon(a.second), a.second});
            addIconCombo(QStringLiteral("Arrowhead"), ahItems, curAh, [this](const QString& d) {
                editor_->setSelectionMeta(QStringLiteral("arrowhead"), d);
            });
            addNum(QStringLiteral("Arrowhead size"),
                   e->meta.value(QStringLiteral("arrowSize")).toDouble(3.3), [this](double v) {
                       editor_->setSelectionMetaNumber(QStringLiteral("arrowSize"), std::max(0.1, v));
                   });
            addNum(QStringLiteral("Landing distance"),
                   e->meta.value(QStringLiteral("landing")).toDouble(8.0), [this](double v) {
                       editor_->setSelectionMetaNumber(QStringLiteral("landing"), std::max(0.0, v));
                   });
            addCombo(QStringLiteral("Number"), {}, e->meta.value(QStringLiteral("text")).toString(),
                     true, [this](const QString& t) {
                         editor_->setSelectionMeta(QStringLiteral("text"), t);
                     });
        }
    } else {
        addRow(QStringLiteral("Center X"), QStringLiteral("0"));
        addRow(QStringLiteral("Center Y"), QStringLiteral("0"));
        addRow(QStringLiteral("Width"), QStringLiteral("0"));
        addRow(QStringLiteral("Height"), QStringLiteral("0"));
    }

    // --- Text section: editable content, font, height, style, justification. ---
    if (e && e->type == EntityType::Text) {
        const QString id = e->id;
        addHeader(QStringLiteral("Text"));

        // Contents — single-line preview; an "Edit..." button opens the full editor.
        const QString content = e->meta.value(QStringLiteral("text")).toString();
        {
            const int r = propsTable_->rowCount();
            propsTable_->insertRow(r);
            auto* key = new QTableWidgetItem(QStringLiteral("Contents"));
            key->setFlags(Qt::ItemIsEnabled);
            propsTable_->setItem(r, 0, key);
            auto* btn = new QPushButton(propsTable_);
            QString oneLine = content;
            oneLine.replace(QLatin1Char('\n'), QLatin1Char(' '));
            btn->setText(oneLine.isEmpty() ? QStringLiteral("Edit...") : oneLine.left(40));
            connect(btn, &QPushButton::clicked, this, [this, id] {
                editor_->emitEditText(id); // reuse the rich text dialog flow
            });
            propsTable_->setCellWidget(r, 1, btn);
        }

        // Font — all system families.
        QStringList fonts = QFontDatabase::families();
        const QString curFont = e->meta.value(QStringLiteral("font")).toString();
        addCombo(QStringLiteral("Font"), fonts, curFont.isEmpty() ? QStringLiteral("Default") : curFont,
                 true, [this](const QString& f) {
                     editor_->setSelectionTextMeta(QStringLiteral("font"),
                                                   f == QLatin1String("Default") ? QJsonValue(QString())
                                                                                 : QJsonValue(f));
                 });

        // Height.
        addNum(QStringLiteral("Height"), e->meta.value(QStringLiteral("size")).toDouble(5.0),
               [this](double v) { editor_->setSelectionTextMeta(QStringLiteral("size"), v); });

        // Bold / Italic / Justify dropdowns.
        addCombo(QStringLiteral("Bold"), {QStringLiteral("No"), QStringLiteral("Yes")},
                 e->meta.value(QStringLiteral("bold")).toBool() ? QStringLiteral("Yes")
                                                                : QStringLiteral("No"),
                 false, [this](const QString& v) {
                     editor_->setSelectionTextMeta(QStringLiteral("bold"), v == QLatin1String("Yes"));
                 });
        addCombo(QStringLiteral("Italic"), {QStringLiteral("No"), QStringLiteral("Yes")},
                 e->meta.value(QStringLiteral("italic")).toBool() ? QStringLiteral("Yes")
                                                                  : QStringLiteral("No"),
                 false, [this](const QString& v) {
                     editor_->setSelectionTextMeta(QStringLiteral("italic"), v == QLatin1String("Yes"));
                 });
        addCombo(QStringLiteral("Underline"), {QStringLiteral("No"), QStringLiteral("Yes")},
                 e->meta.value(QStringLiteral("underline")).toBool() ? QStringLiteral("Yes")
                                                                     : QStringLiteral("No"),
                 false, [this](const QString& v) {
                     editor_->setSelectionTextMeta(QStringLiteral("underline"), v == QLatin1String("Yes"));
                 });
        addCombo(QStringLiteral("Strikethrough"), {QStringLiteral("No"), QStringLiteral("Yes")},
                 e->meta.value(QStringLiteral("strike")).toBool() ? QStringLiteral("Yes")
                                                                  : QStringLiteral("No"),
                 false, [this](const QString& v) {
                     editor_->setSelectionTextMeta(QStringLiteral("strike"), v == QLatin1String("Yes"));
                 });
        // Justification: 9-point (vertical T/M/B + horizontal L/C/R).
        static const QVector<QPair<QString, QString>> justNames = {
            {"Top Left", "TL"},    {"Top Center", "TC"},    {"Top Right", "TR"},
            {"Middle Left", "ML"}, {"Middle Center", "MC"}, {"Middle Right", "MR"},
            {"Bottom Left", "BL"}, {"Bottom Center", "BC"}, {"Bottom Right", "BR"}};
        QStringList justLabels;
        for (const auto& j : justNames) justLabels << j.first;
        QString curJust = e->meta.value(QStringLiteral("justify")).toString();
        if (curJust.isEmpty()) curJust = QStringLiteral("TL");
        QString curJustName = QStringLiteral("Top Left");
        for (const auto& j : justNames) if (j.second == curJust) curJustName = j.first;
        addCombo(QStringLiteral("Justify"), justLabels, curJustName, false,
                 [this](const QString& name) {
                     for (const auto& j : justNames)
                         if (j.first == name) {
                             editor_->setSelectionTextMeta(QStringLiteral("justify"), j.second);
                             return;
                         }
                 });
        // Bullets / numbering.
        const QString curList = e->meta.value(QStringLiteral("list")).toString();
        addCombo(QStringLiteral("List"),
                 {QStringLiteral("No list"), QStringLiteral("Bullets"), QStringLiteral("Numbered")},
                 curList == QLatin1String("bullet")   ? QStringLiteral("Bullets")
                 : curList == QLatin1String("number") ? QStringLiteral("Numbered")
                                                      : QStringLiteral("No list"),
                 false, [this](const QString& v) {
                     editor_->setSelectionTextMeta(
                         QStringLiteral("list"),
                         v == QLatin1String("Bullets")    ? QStringLiteral("bullet")
                         : v == QLatin1String("Numbered") ? QStringLiteral("number")
                                                          : QStringLiteral("none"));
                 });
        // Line spacing factor.
        addNum(QStringLiteral("Line spacing"),
               e->meta.value(QStringLiteral("lineSpacing")).toDouble(1.0),
               [this](double v) {
                   editor_->setSelectionTextMeta(QStringLiteral("lineSpacing"),
                                                 v > 0 ? v : 1.0);
               });
        // Container wrap width (0 = no wrapping / single column). Also draggable
        // via the right-edge grip on the object.
        addNum(QStringLiteral("Box width"), e->meta.value(QStringLiteral("boxWidth")).toDouble(0.0),
               [this](double v) {
                   editor_->setSelectionTextMeta(QStringLiteral("boxWidth"), std::max(0.0, v));
               });
        addNum(QStringLiteral("Box height"), e->meta.value(QStringLiteral("boxHeight")).toDouble(0.0),
               [this](double v) {
                   editor_->setSelectionTextMeta(QStringLiteral("boxHeight"), std::max(0.0, v));
               });
    }

    // ---- Dimension properties (AutoCAD-style) ----
    if (e && e->type == EntityType::Dimension) {
        const int dec = e->meta.contains(QStringLiteral("decimals"))
                            ? e->meta.value(QStringLiteral("decimals")).toInt()
                            : 2;
        addHeader(QStringLiteral("Lines & Arrows"));
        addNum(QStringLiteral("Arrow size"), e->meta.value(QStringLiteral("arrowSize")).toDouble(3.3),
               [this](double v) {
                   editor_->setSelectionDimMeta(QStringLiteral("arrowSize"), std::max(0.1, v));
               });
        addNum(QStringLiteral("Ext line offset"),
               e->meta.value(QStringLiteral("extOffset")).toDouble(2.0), [this](double v) {
                   editor_->setSelectionDimMeta(QStringLiteral("extOffset"), std::max(0.0, v));
               });

        addHeader(QStringLiteral("Text"));
        addNum(QStringLiteral("Text height"), e->meta.value(QStringLiteral("textHeight")).toDouble(5.0),
               [this](double v) {
                   editor_->setSelectionDimMeta(QStringLiteral("textHeight"), std::max(0.1, v));
               });

        addHeader(QStringLiteral("Primary Units"));
        addCombo(QStringLiteral("Precision"),
                 {"0", "1", "2", "3", "4", "5", "6"}, QString::number(dec), false,
                 [this](const QString& t) {
                     editor_->setSelectionDimMeta(QStringLiteral("decimals"), t.toInt());
                 });
        // Read-only measured value, like AutoCAD's "Measurement".
        double measured = 0.0;
        const QString ty = e->meta.value(QStringLiteral("dimType")).toString(QStringLiteral("linear"));
        if (ty == QLatin1String("radius") || ty == QLatin1String("jogged")) measured = e->radius;
        else if (ty == QLatin1String("diameter")) measured = e->radius * 2.0;
        else measured = std::hypot(e->b.x - e->a.x, e->b.y - e->a.y);
        addRow(QStringLiteral("Measurement"), QString::number(measured, 'f', dec));
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
        {{"pline", "lp", "pl", "polyline"}, "Draw", "Polyline", tool(ToolId::Polyline)},
        {{"dline", "d", "dl", "ml"}, "Draw", "Double line", tool(ToolId::DoubleLine)},
        {{"dpline", "dp", "dpl", "mline"}, "Draw", "Double polyline", tool(ToolId::DoublePolyline)},
        {{"rectang", "rec", "rectangle", "rect"}, "Draw", "Rectangle", tool(ToolId::Rect)},
        {{"polygon", "pol"}, "Draw", "Polygon", tool(ToolId::Polygon)},
        {{"circle", "c"}, "Draw", "Circle", tool(ToolId::Circle)},
        {{"arc", "a"}, "Draw", "Arc", tool(ToolId::Arc)},
        {{"ellipse", "el"}, "Draw", "Ellipse", tool(ToolId::Ellipse)},
        {{"spline", "spl"}, "Draw", "Spline", tool(ToolId::Spline)},
        {{"text", "t", "dt"}, "Draw", "Single-line text", tool(ToolId::Text)},
        {{"dim", "dimlinear"}, "Draw", "Linear dimension", tool(ToolId::Dimension)},
        {{"balloon", "bl"}, "Annotation", "Numbered callout balloon", tool(ToolId::Balloon)},
        {{"leader", "ld", "mleader"}, "Annotation", "Text leader", tool(ToolId::TextLeader)},
        {{"note", "nt"}, "Annotation", "Note box", tool(ToolId::Note)},
        {{"revision", "rev"}, "Annotation", "Revision box", tool(ToolId::Revision)},
        {{"table", "tb"}, "Annotation", "Insert an editable table", [this] { insertTableCommand(); }},
        // Harness
        {{"wire", "w"}, "Harness", "Wire", tool(ToolId::Wire)},
        {{"connector", "con"}, "Harness", "Connector", tool(ToolId::Connector)},
        {{"splice"}, "Harness", "Splice marker", tool(ToolId::Splice)},
        {{"notconnected", "nc"}, "Harness", "Not-connected marker", tool(ToolId::NotConnected)},
        {{"netlabel", "nl"}, "Harness", "Net label", tool(ToolId::NetLabel)},
        {{"bom"}, "Harness", "Bill of materials", [this] { showBomDialog(); }},
        {{"alignrouting", "align", "ar2"}, "Harness",
         "Align routing to a reference connector", [this] { editor_->beginAlignRouting(); }},
        {{"validate", "drc", "check"}, "Harness",
         "Validate the schematic (pins/labels/wires)", [this] { showValidation(); }},
        // Modify
        {{"move", "m"}, "Modify", "Move (base point, target)", tool(ToolId::Move)},
        {{"rotate", "ro"}, "Modify", "Rotate (pivot, ref, target)", tool(ToolId::Rotate)},
        {{"mirror", "mi"}, "Modify", "Mirror across an axis", tool(ToolId::Mirror)},
        {{"scale", "sc"}, "Modify", "Scale (base, ref, new)", tool(ToolId::Scale)},
        {{"offset", "o"}, "Modify", "Offset", tool(ToolId::Offset)},
        {{"trim", "tr"}, "Modify", "Trim to a boundary", tool(ToolId::Trim)},
        {{"extend", "ex"}, "Modify", "Extend to a boundary", tool(ToolId::Extend)},
        {{"fillet", "fi", "f"}, "Modify", "Fillet two lines (set radius: r <value>)", tool(ToolId::Fillet)},
        {{"chamfer", "ch", "cha"}, "Modify", "Chamfer two lines", tool(ToolId::Chamfer)},
        {{"array", "ar"}, "Modify", "Rectangular array", [this] { arrayDialog(); }},
        {{"explode", "x"}, "Modify", "Explode into primitives", [this] { editor_->explodeSelected(); }},
        {{"purge", "pu"}, "Modify", "Purge unused layers / stray objects", [this] { editor_->purgeUnused(); }},
        {{"erase", "e", "delete", "del"}, "Modify", "Delete the selection", [this] { editor_->deleteSelected(); }},
        // Edit
        {{"select", "sel"}, "Edit", "Select tool", tool(ToolId::Select)},
        {{"selectall", "all"}, "Edit", "Select everything", [this] { editor_->selectAll(); }},
        {{"copy", "co", "cp"}, "Edit", "Copy selection to clipboard", [this] { editor_->copySelection(); }},
        {{"cut", "cu"}, "Edit", "Cut selection to clipboard", [this] { editor_->cutSelection(); }},
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
        {{"pdf", "pdfout", "print"}, "File", "Export the drawing to PDF", [this] { exportPdf(false); }},
        {{"pdfwindow", "pdfw"}, "File", "Export a dragged window to PDF", [this] { exportPdf(true); }},
        {{"csv", "csvout"}, "File", "Export the selected table to CSV", [this] { exportTableCsv(); }},
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

    // Exact command/alias match runs directly.
    for (const auto& c : commands_) {
        if (c.names.contains(cmd)) { c.run(); return; }
    }

    // Otherwise take the NEAREST available command: the alphabetically-first
    // name/alias that starts with what was typed (i.e. the entry highlighted at the
    // top of the autocomplete list). So "tri" -> trim, "cir" -> circle, "mo" -> move.
    const Command* best = nullptr;
    QString bestWord, bestCanon;
    for (const auto& c : commands_) {
        for (const auto& n : c.names) {
            if (!n.startsWith(cmd, Qt::CaseInsensitive)) continue;
            if (bestWord.isEmpty() || n.compare(bestWord, Qt::CaseInsensitive) < 0) {
                bestWord = n;
                bestCanon = c.names.isEmpty() ? n : c.names.front();
                best = &c;
            }
        }
    }
    if (best) {
        statusBar()->showMessage(QStringLiteral("%1 → %2").arg(cmd, bestCanon));
        best->run();
        return;
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

void MainWindow::buildConnectorBuildTab(QWidget* page, QHBoxLayout* lay) {
    const ConnectorSpec init = editor_->connectorSpec();
    auto addGroup = [&](const QString& title, QWidget* content) {
        lay->addWidget(ribbonGroup(title, content));
    };
    auto label = [&](QWidget* parent, const QString& t) {
        auto* l = new QLabel(t, parent);
        l->setStyleSheet(QStringLiteral("color:#c9ccd1;background:transparent;"));
        return l;
    };

    // ---- Identity: Designator / Part number ----
    auto* desEdit = new QLineEdit(init.designator, page);
    desEdit->setMaximumWidth(90);
    desEdit->setPlaceholderText(QStringLiteral("J1"));
    auto* pnEdit = new QLineEdit(init.partNumber, page);
    pnEdit->setMaximumWidth(90);
    pnEdit->setPlaceholderText(QStringLiteral("P/N"));
    {
        auto* w = new QWidget(page);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0); g->setSpacing(2);
        g->addWidget(label(w, QStringLiteral("Des.")), 0, 0);
        g->addWidget(desEdit, 0, 1);
        g->addWidget(label(w, QStringLiteral("P/N")), 1, 0);
        g->addWidget(pnEdit, 1, 1);
        g->setColumnStretch(2, 1); // keep label + field left-adjacent (no gap)
        addGroup(QStringLiteral("Identity"), w);
    }

    // ---- Pins: Qty / Label style / Order / Pitch / Length ----
    auto* qtySpin = new QSpinBox(page);
    qtySpin->setRange(1, 128); qtySpin->setValue(init.pinCount); qtySpin->setMaximumWidth(60);
    auto* lblCombo = new QComboBox(page);
    lblCombo->addItem(QStringLiteral("Numbers"), QStringLiteral("numbers"));
    lblCombo->addItem(QStringLiteral("Letters"), QStringLiteral("letters"));
    lblCombo->setCurrentIndex(std::max(0, lblCombo->findData(init.annotation)));
    auto* ordCombo = new QComboBox(page);
    ordCombo->addItem(QStringLiteral("Top"), QStringLiteral("top"));
    ordCombo->addItem(QStringLiteral("Bottom"), QStringLiteral("bottom"));
    ordCombo->setCurrentIndex(std::max(0, ordCombo->findData(init.pinOrder)));
    auto* pitchSpin = new QDoubleSpinBox(page);
    pitchSpin->setRange(1, 500); pitchSpin->setValue(init.pitch); pitchSpin->setMaximumWidth(70);
    auto* lenSpin = new QDoubleSpinBox(page);
    lenSpin->setRange(1, 1000); lenSpin->setValue(init.pinLength); lenSpin->setMaximumWidth(70);
    {
        auto* w = new QWidget(page);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0); g->setSpacing(2);
        g->addWidget(label(w, QStringLiteral("Qty")), 0, 0);   g->addWidget(qtySpin, 0, 1);
        g->addWidget(label(w, QStringLiteral("Lbl")), 0, 2);   g->addWidget(lblCombo, 0, 3);
        g->addWidget(label(w, QStringLiteral("Ord")), 1, 2);   g->addWidget(ordCombo, 1, 3);
        g->addWidget(label(w, QStringLiteral("Pitch")), 0, 4); g->addWidget(pitchSpin, 0, 5);
        g->addWidget(label(w, QStringLiteral("Len")), 1, 4);   g->addWidget(lenSpin, 1, 5);
        addGroup(QStringLiteral("Pins"), w);
    }

    // ---- Body: Rect mode / W / H / Associative ----
    auto* rectCombo = new QComboBox(page);
    rectCombo->addItem(QStringLiteral("Dynamic"), QStringLiteral("dynamic"));
    rectCombo->addItem(QStringLiteral("Custom"), QStringLiteral("custom"));
    rectCombo->setCurrentIndex(std::max(0, rectCombo->findData(init.rectMode)));
    auto* wSpin = new QDoubleSpinBox(page);
    wSpin->setRange(1, 2000); wSpin->setValue(init.rectWidth); wSpin->setMaximumWidth(70);
    auto* hSpin = new QDoubleSpinBox(page);
    hSpin->setRange(1, 5000); hSpin->setValue(init.rectHeight); hSpin->setMaximumWidth(70);
    auto* assoc = new QCheckBox(QStringLiteral("Associative"), page);
    assoc->setChecked(init.associative);
    assoc->setStyleSheet(QStringLiteral("color:#c9ccd1;background:transparent;"));
    {
        auto* w = new QWidget(page);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0); g->setSpacing(2);
        g->addWidget(label(w, QStringLiteral("Rect")), 0, 0); g->addWidget(rectCombo, 0, 1);
        g->addWidget(label(w, QStringLiteral("W")), 1, 0);    g->addWidget(wSpin, 1, 1);
        g->addWidget(label(w, QStringLiteral("H")), 1, 2);    g->addWidget(hSpin, 1, 3);
        g->addWidget(assoc, 0, 2, 1, 2);
        addGroup(QStringLiteral("Body"), w);
    }

    // ---- Text: per-role font + size (pin / designator / part number) ----
    auto* pinFont = new QFontComboBox(page); pinFont->setMaximumWidth(120);
    FontLibrary::applyTo(pinFont);
    pinFont->setCurrentFont(QFont(init.pinFont));
    auto* pinSize = new QDoubleSpinBox(page); pinSize->setRange(1, 200);
    pinSize->setValue(init.pinSize); pinSize->setMaximumWidth(56);
    auto* desFont = new QFontComboBox(page); desFont->setMaximumWidth(120);
    FontLibrary::applyTo(desFont);
    desFont->setCurrentFont(QFont(init.desFont));
    auto* desSize = new QDoubleSpinBox(page); desSize->setRange(1, 200);
    desSize->setValue(init.desSize); desSize->setMaximumWidth(56);
    auto* pnFont = new QFontComboBox(page); pnFont->setMaximumWidth(120);
    FontLibrary::applyTo(pnFont);
    pnFont->setCurrentFont(QFont(init.pnFont));
    auto* pnSize = new QDoubleSpinBox(page); pnSize->setRange(1, 200);
    pnSize->setValue(init.pnSize); pnSize->setMaximumWidth(56);
    {
        auto* w = new QWidget(page);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0); g->setSpacing(2);
        g->addWidget(label(w, QStringLiteral("Pin")), 0, 0);  g->addWidget(pinFont, 0, 1); g->addWidget(pinSize, 0, 2);
        g->addWidget(label(w, QStringLiteral("Des.")), 1, 0); g->addWidget(desFont, 1, 1); g->addWidget(desSize, 1, 2);
        g->addWidget(label(w, QStringLiteral("P/N")), 2, 0);  g->addWidget(pnFont, 2, 1); g->addWidget(pnSize, 2, 2);
        addGroup(QStringLiteral("Text"), w);
    }

    // Gather every widget into a spec and store it as the placement default; when a
    // connector is selected, live-apply so edits show immediately.
    auto apply = [=, this] {
        if (connectorSyncing_) return; // ignore programmatic widget updates from sync
        // Seed from the SELECTED connector (if any) so fields the ribbon doesn't expose
        // — mirrorX / mirrorY especially — are preserved when editing it (e.g. changing
        // the pin count must not flip a mirrored connector back to the default).
        ConnectorSpec s = editor_->connectorSpec();
        const QString selId = editor_->selectedConnectorId();
        if (!selId.isEmpty() && doc_)
            for (const auto& e : doc_->entities())
                if (e.id == selId) { s = connectorSpecFromMeta(e.meta); break; }
        s.designator = desEdit->text();
        s.partNumber = pnEdit->text();
        s.pinCount = qtySpin->value();
        s.annotation = lblCombo->currentData().toString();
        s.pinOrder = ordCombo->currentData().toString();
        s.pitch = pitchSpin->value();
        s.pinLength = lenSpin->value();
        s.rectMode = rectCombo->currentData().toString();
        s.rectWidth = wSpin->value();
        s.rectHeight = hSpin->value();
        s.associative = assoc->isChecked();
        s.pinFont = pinFont->currentFont().family();
        s.pinSize = pinSize->value();
        s.desFont = desFont->currentFont().family();
        s.desSize = desSize->value();
        s.pnFont = pnFont->currentFont().family();
        s.pnSize = pnSize->value();
        s.textSize = std::max({s.pinSize, s.desSize, s.pnSize});
        regenPinLabels(s);
        const bool custom = s.rectMode == QLatin1String("custom");
        wSpin->setEnabled(custom);
        hSpin->setEnabled(custom);
        // Dynamic body: show the auto-computed W/H in the (disabled) fields so the
        // ribbon reflects the real size as pitch / text size / pin count change.
        if (!custom) {
            const ConnectorGeometry g = connectorGeometry({0, 0}, s);
            connectorSyncing_ = true; // don't let the setValue re-enter apply()
            wSpin->setValue(g.effectiveWidth);
            hSpin->setValue(g.effectiveHeight);
            connectorSyncing_ = false;
        }
        // If a connector is selected, edit it in place; otherwise set the placement
        // default. Either way repaint so a selected connector or the placement preview
        // ghost reflects the change live.
        if (!editor_->selectedConnectorId().isEmpty())
            editor_->applyConnectorSpecToSelection(s);
        else
            editor_->connectorSpec() = s;
        if (canvas_) canvas_->update(); // canvas_ may not exist yet at first sync
    };
    for (QLineEdit* le : {desEdit, pnEdit})
        connect(le, &QLineEdit::textChanged, this, [apply] { apply(); });
    connect(qtySpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [apply] { apply(); });
    for (QComboBox* cb : {lblCombo, ordCombo, rectCombo})
        connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [apply] { apply(); });
    for (QDoubleSpinBox* sp : {pitchSpin, lenSpin, wSpin, hSpin, pinSize, desSize, pnSize})
        connect(sp, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [apply] { apply(); });
    for (QFontComboBox* fc : {pinFont, desFont, pnFont})
        connect(fc, &QFontComboBox::currentFontChanged, this, [apply] { apply(); });
    connect(assoc, &QCheckBox::toggled, this, [apply] { apply(); });

    // Plain editable number fields — no +/- spin buttons (type the value directly).
    for (QAbstractSpinBox* sp : std::initializer_list<QAbstractSpinBox*>{
             qtySpin, pitchSpin, lenSpin, wSpin, hSpin, pinSize, desSize, pnSize})
        sp->setButtonSymbols(QAbstractSpinBox::NoButtons);

    // Reload every widget from the current context (the selected connector, else the
    // placement default) without triggering apply() — used when the tab opens.
    syncConnectorRibbon_ = [=, this] {
        ConnectorSpec s = editor_->connectorSpec();
        const QString selId = editor_->selectedConnectorId();
        if (!selId.isEmpty() && doc_)
            for (const auto& e : doc_->entities())
                if (e.id == selId) { s = connectorSpecFromMeta(e.meta); break; }
        connectorSyncing_ = true;
        desEdit->setText(s.designator);
        pnEdit->setText(s.partNumber);
        qtySpin->setValue(s.pinCount);
        lblCombo->setCurrentIndex(std::max(0, lblCombo->findData(s.annotation)));
        ordCombo->setCurrentIndex(std::max(0, ordCombo->findData(s.pinOrder)));
        pitchSpin->setValue(s.pitch);
        lenSpin->setValue(s.pinLength);
        rectCombo->setCurrentIndex(std::max(0, rectCombo->findData(s.rectMode)));
        wSpin->setValue(s.rectWidth);
        hSpin->setValue(s.rectHeight);
        assoc->setChecked(s.associative);
        pinFont->setCurrentFont(QFont(s.pinFont)); pinSize->setValue(s.pinSize);
        desFont->setCurrentFont(QFont(s.desFont)); desSize->setValue(s.desSize);
        pnFont->setCurrentFont(QFont(s.pnFont)); pnSize->setValue(s.pnSize);
        const bool custom = s.rectMode == QLatin1String("custom");
        wSpin->setEnabled(custom);
        hSpin->setEnabled(custom);
        connectorSyncing_ = false;
    };

    // ---- Actions: Auto scale / Place connector ----
    {
        auto* w = new QWidget(page);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0); g->setSpacing(2);
        auto* autoBtn = new QToolButton(w);
        autoBtn->setText(QStringLiteral("Auto scale"));
        autoBtn->setMinimumWidth(96);
        autoBtn->setToolTip(QStringLiteral("Scale the connector so it fills ~10% of the drawing view"));
        connect(autoBtn, &QToolButton::clicked, this, [this] {
            if (!editor_->connectorArmed()) {
                statusBar()->showMessage(QStringLiteral("Place the connector to start preview before auto scaling"));
                return;
            }
            // Uniformly scale the connector so its larger dimension is ~10% of the
            // smaller viewport dimension (in the current zoom).
            ConnectorSpec s = editor_->connectorSpec();
            if (!s.associative) {
                statusBar()->showMessage(QStringLiteral("Auto scale requires an associative connector"));
                return;
            }
            const ConnectorGeometry g = connectorGeometry({0, 0}, s);
            const double curMax = std::max(g.effectiveWidth, g.effectiveHeight);
            const double vs = canvas_->viewScale();
            const double screenMin = std::min(canvas_->width(), canvas_->height());
            if (curMax < 1e-6 || vs < 1e-9 || screenMin < 1.0) return;
            const double f = (0.10 * screenMin / vs) / curMax;
            for (double* d : {&s.pitch, &s.pinLength, &s.textSize,
                              &s.pinSize, &s.desSize, &s.pnSize})
                *d *= f;
            // Keep the SHAPE: bake the current effective body into custom W/H scaled
            // by the same factor, so the whole geometry (body + pins + text) scales
            // uniformly. (Dynamic mode would keep the body width fixed at 40 and
            // re-derive the height, leaving the geometry visually unchanged.)
            s.rectMode = QStringLiteral("custom");
            s.rectWidth = g.effectiveWidth * f;
            s.rectHeight = g.effectiveHeight * f;
            editor_->connectorSpec() = s;
            if (syncConnectorRibbon_) syncConnectorRibbon_();
            canvas_->update();
            statusBar()->showMessage(QStringLiteral("Connector scaled to ~10%% of the view"));
        });
        auto* resetBtn = new QToolButton(w);
        resetBtn->setText(QStringLiteral("Reset size"));
        resetBtn->setMinimumWidth(96);
        resetBtn->setToolTip(QStringLiteral("Reset the connector's size (pitch, pin length, "
                                            "body, text) to the defaults"));
        connect(resetBtn, &QToolButton::clicked, this, [this] {
            ConnectorSpec s = editor_->connectorSpec();
            const ConnectorSpec d = defaultConnectorSpec(std::max(1, s.pinCount));
            s.pitch = d.pitch; s.pinLength = d.pinLength;
            s.rectMode = d.rectMode; s.rectWidth = d.rectWidth; s.rectHeight = d.rectHeight;
            s.textSize = d.textSize; s.pinSize = d.pinSize; s.desSize = d.desSize; s.pnSize = d.pnSize;
            if (!editor_->selectedConnectorId().isEmpty())
                editor_->applyConnectorSpecToSelection(s);
            else
                editor_->connectorSpec() = s;
            if (syncConnectorRibbon_) syncConnectorRibbon_();
            canvas_->update();
            statusBar()->showMessage(QStringLiteral("Connector size reset to default"));
        });
        auto* placeBtn = new QToolButton(w);
        connectorPlaceBtn_ = placeBtn;
        placeBtn->setText(QStringLiteral("Place connector"));
        placeBtn->setMinimumWidth(110);
        placeBtn->setCheckable(true); // shows a pressed state only while placement is armed
        // Neutral when idle; the prominent blue appears only while placement is armed.
        placeBtn->setStyleSheet(QStringLiteral(
            "QToolButton{background:#3a3d43;color:#eaf2ff;border:1px solid #565a61;"
            "border-radius:3px;padding:4px 8px;}QToolButton:hover{background:#45484f;}"
            "QToolButton:checked{background:#1b64ff;border:1px solid #cfe3ff;}"));
        connect(placeBtn, &QToolButton::clicked, this, [this, apply] {
            apply();
            editor_->armConnectorPlacement(); // arm placement -> the preview appears now
            connectorPlaceBtn_->setChecked(true); // stay pressed while armed
            // Move focus to the canvas so the ghost tracks and Tab/X/Y work immediately.
            canvas_->setFocus(Qt::OtherFocusReason);
            canvas_->update();
            statusBar()->showMessage(QStringLiteral(
                "Connector preview follows the cursor — X/Y flip, Tab to edit, click to place"));
        });
        g->addWidget(autoBtn, 0, 0);
        g->addWidget(resetBtn, 0, 1);
        g->addWidget(placeBtn, 1, 0, 1, 2);
        addGroup(QStringLiteral("Actions"), w);
    }
    apply(); // sync W/H enabled state + store the initial spec
}

void MainWindow::buildConnectorRibbonTab() {
    connectorTab_ = new QWidget(ribbon_);
    auto* lay = new QHBoxLayout(connectorTab_);
    lay->setContentsMargins(6, 4, 6, 2);
    lay->setSpacing(0);
    lay->setAlignment(Qt::AlignLeft);
    buildConnectorBuildTab(connectorTab_, lay);
    lay->addStretch(1); // absorb the extra width so groups pack left (no stretched gaps)
    connectorTab_->hide();
}

void MainWindow::showConnectorRibbon(bool on) {
    if (!connectorTab_ || !ribbon_) return;
    const int idx = ribbon_->indexOf(connectorTab_);
    if (on) {
        if (syncConnectorRibbon_) syncConnectorRibbon_(); // reflect the selected/default spec
        if (idx < 0) {
            const int at = ribbon_->addTab(connectorTab_, QStringLiteral("Connector build"));
            ribbon_->setCurrentIndex(at);
        } else {
            ribbon_->setCurrentIndex(idx);
        }
        connectorTab_->show();
    } else if (idx >= 0) {
        ribbon_->removeTab(idx); // detaches but does not delete connectorTab_
        // Return to the Schematic build tab (where the connector was opened from), not Home.
        int sb = 0;
        for (int i = 0; i < ribbon_->count(); ++i)
            if (ribbon_->tabText(i) == QStringLiteral("Schematic build")) { sb = i; break; }
        ribbon_->setCurrentIndex(sb);
    }
}

void MainWindow::updateConnectorContext() {
    // Show the connector build ribbon only while PLACING a connector (Connector tool).
    // A selected connector is edited in the side Properties panel instead of the ribbon.
    showConnectorRibbon(editor_->tool() == ToolId::Connector);
}

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

void MainWindow::showValidation() {
    const std::vector<QString> issues = editor_->validateSchematic();
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("Schematic validation"));
    if (issues.empty()) {
        box.setIcon(QMessageBox::Information);
        box.setText(QStringLiteral("Schematic is valid — no issues found."));
    } else {
        box.setIcon(QMessageBox::Warning);
        box.setText(QStringLiteral("%1 issue(s) found:").arg(issues.size()));
        QStringList lines;
        for (const QString& s : issues) lines << QStringLiteral("•  ") + s;
        box.setInformativeText(lines.join('\n'));
    }
    box.exec();
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
    sn.snapGeometricCenter = s.value(QStringLiteral("draft/snapGeomCenter"), sn.snapGeometricCenter).toBool();
    sn.snapNode = s.value(QStringLiteral("draft/snapNode"), sn.snapNode).toBool();
    sn.snapQuadrant = s.value(QStringLiteral("draft/snapQuadrant"), sn.snapQuadrant).toBool();
    sn.snapIntersection = s.value(QStringLiteral("draft/snapIntersection"), sn.snapIntersection).toBool();
    sn.snapInsertion = s.value(QStringLiteral("draft/snapInsertion"), sn.snapInsertion).toBool();
    sn.snapPerpendicular = s.value(QStringLiteral("draft/snapPerpendicular"), sn.snapPerpendicular).toBool();
    sn.snapTangent = s.value(QStringLiteral("draft/snapTangent"), sn.snapTangent).toBool();
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
    s.setValue(QStringLiteral("draft/snapGeomCenter"), sn.snapGeometricCenter);
    s.setValue(QStringLiteral("draft/snapNode"), sn.snapNode);
    s.setValue(QStringLiteral("draft/snapQuadrant"), sn.snapQuadrant);
    s.setValue(QStringLiteral("draft/snapIntersection"), sn.snapIntersection);
    s.setValue(QStringLiteral("draft/snapInsertion"), sn.snapInsertion);
    s.setValue(QStringLiteral("draft/snapPerpendicular"), sn.snapPerpendicular);
    s.setValue(QStringLiteral("draft/snapTangent"), sn.snapTangent);
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

void MainWindow::exportPdf(bool windowSelect) {
    if (!doc_) return;
    if (windowSelect) {
        statusBar()->showMessage(
            QStringLiteral("Drag a window on the drawing to print to PDF (Esc to cancel)"));
        canvas_->beginWindowCapture([this](Vec2 a, Vec2 b) { exportPdfRegion(a, b); });
        return;
    }
    // Whole drawing: the bounds of every visible entity (plus a small margin).
    Bounds ext;
    for (const auto& e : doc_->entities()) {
        if (const Layer* l = doc_->findLayer(e.layer); l && !l->visible) continue;
        const Bounds b = e.bounds();
        if (b.valid) { ext.expand(b.min); ext.expand(b.max); }
    }
    if (!ext.valid) {
        QMessageBox::information(this, QStringLiteral("Export PDF"),
                                 QStringLiteral("The drawing is empty — nothing to export."));
        return;
    }
    const double mx = std::max(1.0, ext.width() * 0.05), my = std::max(1.0, ext.height() * 0.05);
    exportPdfRegion({ext.min.x - mx, ext.min.y - my}, {ext.max.x + mx, ext.max.y + my});
}

void MainWindow::exportPdfRegion(const Vec2& a, const Vec2& b) {
    if (!doc_) return;
    const double wMinX = std::min(a.x, b.x), wMaxX = std::max(a.x, b.x);
    const double wMinY = std::min(a.y, b.y), wMaxY = std::max(a.y, b.y);
    const double worldW = wMaxX - wMinX, worldH = wMaxY - wMinY;
    if (worldW < 1e-9 || worldH < 1e-9) return;

    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export PDF"), QString(),
                                                QStringLiteral("PDF (*.pdf)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(".pdf", Qt::CaseInsensitive)) path += ".pdf";

    QPdfWriter pdf(path);
    pdf.setResolution(300);
    // Landscape or portrait page to best fit the window's aspect ratio.
    pdf.setPageSize(QPageSize(QPageSize::A4));
    pdf.setPageOrientation(worldW >= worldH ? QPageLayout::Landscape
                                            : QPageLayout::Portrait);
    pdf.setPageMargins(QMarginsF(10, 10, 10, 10), QPageLayout::Millimeter);

    QPainter p(&pdf);
    if (!p.isActive()) {
        QMessageBox::warning(this, QStringLiteral("Export failed"),
                             QStringLiteral("Could not open the PDF for writing."));
        return;
    }
    p.setRenderHint(QPainter::Antialiasing, true);
    const int pw = p.viewport().width(), ph = p.viewport().height();

    // Fit the world window into the page, preserving aspect ratio and centring it.
    const double scale = std::min(pw / worldW, ph / worldH);
    ViewTransform vt;
    vt.view.scale = scale;
    vt.view.x = pw / 2.0 - (wMinX + wMaxX) / 2.0 * scale;
    vt.view.y = ph / 2.0 + (wMinY + wMaxY) / 2.0 * scale; // y is flipped (screen y-down)

    // WYSIWYG: paint the drawing exactly as shown on the canvas (dark background), so
    // white/ByLayer geometry stays visible and colours match the screen.
    const QColor bg = EntityRenderer::backgroundColor();
    p.fillRect(0, 0, pw, ph, bg);
    p.setClipRect(0, 0, pw, ph);
    for (const auto& e : doc_->entities()) {
        if (const Layer* l = doc_->findLayer(e.layer); l && !l->visible) continue;
        const Bounds eb = e.bounds();
        if (eb.valid && (eb.max.x < wMinX || eb.min.x > wMaxX || eb.max.y < wMinY ||
                         eb.min.y > wMaxY))
            continue; // outside the print window
        EntityRenderer::draw(p, e, *doc_, vt);
    }
    p.end();
    statusBar()->showMessage(QStringLiteral("Exported %1").arg(QFileInfo(path).fileName()));
}

void MainWindow::exportTableCsv() {
    if (!doc_) return;
    // Target: the table of the selected cell, else a single selected table entity.
    const Entity* table = nullptr;
    if (!editor_->selectedCellTable().isEmpty()) {
        for (const auto& e : doc_->entities())
            if (e.id == editor_->selectedCellTable()) { table = &e; break; }
    }
    if (!table && editor_->selection().size() == 1) {
        for (const auto& e : doc_->entities())
            if (e.id == editor_->selection().front() && e.meta.value("table").toBool()) {
                table = &e;
                break;
            }
    }
    if (!table) {
        QMessageBox::information(
            this, QStringLiteral("Export CSV"),
            QStringLiteral("Select a table (or a table cell) first, then export to CSV."));
        return;
    }
    const int rows = table->meta.value("rows").toInt();
    const int cols = table->meta.value("cols").toInt();
    if (rows <= 0 || cols <= 0) return;
    const QJsonArray cells = table->meta.value("cells").toArray();

    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export table to CSV"),
                                                QString(), QStringLiteral("CSV (*.csv)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(".csv", Qt::CaseInsensitive)) path += ".csv";
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, QStringLiteral("Export failed"),
                             QStringLiteral("Could not open the file for writing."));
        return;
    }
    // Escape a field per RFC 4180: wrap in quotes if it has a comma/quote/newline.
    auto esc = [](QString v) {
        if (v.contains('"')) v.replace('"', QStringLiteral("\"\""));
        if (v.contains(',') || v.contains('"') || v.contains('\n') || v.contains('\r'))
            return QStringLiteral("\"%1\"").arg(v);
        return v;
    };
    QTextStream out(&f);
    for (int r = 0; r < rows; ++r) {
        QStringList row;
        for (int c = 0; c < cols; ++c) {
            const int idx = r * cols + c;
            row << esc(idx < cells.size() ? cells[idx].toString() : QString());
        }
        out << row.join(',') << "\r\n";
    }
    f.close();
    statusBar()->showMessage(QStringLiteral("Exported %1").arg(QFileInfo(path).fileName()));
}

} // namespace Edraw_Desktop
