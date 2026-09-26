#include "app/MainWindow.h"
#include <QProgressBar>
#include <QItemSelectionModel>
#include <QScopeGuard>

#include "app/Chrome.h"
#include "app/OutputDialog.h"
#include "app/MechWizardDialog.h"
#include "app/BrickWizardDialog.h"
#include "app/OdaSetupDialog.h"
#include "io/OdaConverter.h"
#include "app/PageSetupManagerDialog.h"

#include "app/ribbon/RibbonIcons.h"

#include "app/HarnessBuilderDialog.h"
#include "app/BomDialog.h"
#include "harness/Bom.h"
#include "harness/PartsLibrary.h"
#include "app/PartsLibraryDialog.h"
#include "app/PartViewDialog.h"
#include "app/BlockDialog.h"
#include "app/BundleDiameterDialog.h"
#include "app/HarnessStepsDialog.h"
#include "app/MechModelsDialog.h"
#include "app/MechanicalBuilderDialog.h"
#include "app/MechanicalValidationDialog.h"
#include "app/TemplateDialog.h"
#include "app/ScientificCalculatorDialog.h"
#include "model/Block.h"
#include "model/Picture.h"
#include "app/ThermalModelDialog.h"
#include "app/VoltageDropDialog.h"

#include "app/Editor.h"
#include "app/UiBits.h"
#include "app/FontLibrary.h"
#include "harness/Harness.h"
#include "io/Dwx.h"
#include "tools/EntityTransforms.h"
#include "io/Dxf.h"
#include "io/PdfImport.h"
#include "render/CanvasWidget.h"

#include <QAction>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QClipboard>
#include <QGuiApplication>
#include <QDrag>
#include <QMimeData>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QMessageBox>
#include <QAbstractButton>
#include <QButtonGroup>
#include <QSpinBox>
#include <QActionGroup>
#include <QAbstractItemDelegate>
#include <QCoreApplication>
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
#include <QTreeWidget>
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
#include <QScrollBar>
#include <QCloseEvent>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolBar>
#include <QToolButton>
#include <QUuid>
#include <QWidgetAction>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QIcon>
#include <QPainter>
#include <QPointer>
#include <QPainterPath>
#include <QPixmap>
#include <QPdfWriter>
#include <QPrinter>
#include <QPrintDialog>
#include <QPageSize>
#include <QPageLayout>
#include <QMarginsF>
#include <QFile>
#include <QTextStream>
#include <QSettings>
#include <QTemporaryDir>
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
#include <cstdio>
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
    } else if (op == QLatin1String("arraypolar")) {
        // Small squares ringed around a centre mark (polar array).
        p.drawLine(10, 12, 14, 12);
        p.drawLine(12, 10, 12, 14);
        for (int k = 0; k < 6; ++k) {
            const double an = k * M_PI / 3.0;
            p.drawRect(QRectF(12 + 8 * std::cos(an) - 2.0, 12 + 8 * std::sin(an) - 2.0, 4, 4));
        }
    } else if (op == QLatin1String("arraypath")) {
        // Squares marching along a curve (path / tracking array).
        QPainterPath curve;
        curve.moveTo(3, 19);
        curve.cubicTo(8, 3, 16, 21, 21, 5);
        p.drawPath(curve);
        for (double t : {0.08, 0.5, 0.92}) {
            const QPointF at = curve.pointAtPercent(t);
            p.drawRect(QRectF(at.x() - 2.0, at.y() - 2.0, 4, 4));
        }
    } else if (op == QLatin1String("qselect")) {
        // A filter funnel with a cursor arrow: pick objects by their properties.
        QPolygonF funnel({{4, 4}, {17, 4}, {12, 12}, {12, 19}, {9, 16}, {9, 12}});
        p.drawPolygon(funnel);
        p.drawLine(6, 8, 15, 8); // the sieve line across it
        QPolygonF cur({{15, 13}, {15, 22}, {17, 20}, {19, 23}, {21, 22}, {19, 19}, {22, 19}});
        p.setBrush(ink);
        p.drawPolygon(cur);
        p.setBrush(Qt::NoBrush);
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
    } else if (op.startsWith(QLatin1String("align"))) {
        // THREE BARS OF DIFFERENT LENGTHS AND THE LINE THEY ARE SQUARED UP TO - which way round
        // says which edge. The plain button is the left edge, the commonest of them.
        const QString how = op.mid(5);   // "", "left", "right", "top", "bottom", "x", "y"
        QPen guide(ink);
        guide.setWidthF(1.2);
        guide.setStyle(how == QLatin1String("x") || how == QLatin1String("y") ? Qt::DashLine
                                                                             : Qt::SolidLine);
        const bool across = how == QLatin1String("top") || how == QLatin1String("bottom") ||
                            how == QLatin1String("y");
        const int lengths[3] = {12, 7, 10};
        p.setBrush(ink);
        for (int i = 0; i < 3; ++i) {
            const int len = lengths[i];
            const int band = 5 + i * 6;
            QRectF bar;
            if (!across) {
                const bool fromRight = how == QLatin1String("right");
                const bool centred = how == QLatin1String("x");
                const double x0 = centred ? 12.0 - len / 2.0 : fromRight ? 19.0 - len : 5.0;
                bar = QRectF(x0, band, len, 3.0);
            } else {
                const bool fromBottom = how == QLatin1String("bottom");
                const bool centred = how == QLatin1String("y");
                const double y0 = centred ? 12.0 - len / 2.0 : fromBottom ? 19.0 - len : 5.0;
                bar = QRectF(band, y0, 3.0, len);
            }
            p.setPen(Qt::NoPen);
            p.drawRect(bar);
        }
        p.setBrush(Qt::NoBrush);
        p.setPen(guide);
        if (!across) {
            const double x = how == QLatin1String("right") ? 20.0
                             : how == QLatin1String("x")   ? 12.0
                                                           : 4.0;
            p.drawLine(QPointF(x, 2.0), QPointF(x, 22.0));
        } else {
            const double y = how == QLatin1String("bottom") ? 20.0
                             : how == QLatin1String("y")    ? 12.0
                                                            : 4.0;
            p.drawLine(QPointF(2.0, y), QPointF(22.0, y));
        }
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
    p.setBrush(Chrome::grip());
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
    // The SAME table the drawing uses. This little sample is the only thing anybody has to go on when
    // choosing a linetype, so it must not be able to disagree with the line that comes out - which it
    // could while the patterns were written down twice.
    if (const QVector<qreal> dashes = linetypeDashes(name, 1.0, pen.widthF()); !dashes.isEmpty())
        pen.setDashPattern(dashes);
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

// HOW NARROW A SIDE PANEL MAY BE PULLED. Narrow enough to get it out of the way of the drawing without
// closing it, and wide enough that what is left is still a panel and not a sliver.
constexpr int kNarrowestPanel = 150;

// "You may be squeezed." A caption keeps its own idea of how wide it needs to be, and inside a panel
// that idea becomes a floor under the whole side. This lets the widget be given less than it asked for -
// the text is then clipped, so the full wording goes on the tooltip where it can still be read.
void letSqueeze(QWidget* w) {
    if (!w) return;
    if (w->toolTip().isEmpty()) {
        if (auto* b = qobject_cast<QAbstractButton*>(w)) w->setToolTip(b->text());
        else if (auto* l = qobject_cast<QLabel*>(w)) w->setToolTip(l->text());
    }
    w->setMinimumWidth(0);
    w->setSizePolicy(QSizePolicy::Ignored, w->sizePolicy().verticalPolicy());
}

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
        p.fillRect(rect(), Chrome::strip());
        // A few grip dots near the top.
        p.setBrush(Chrome::grip());
        p.setPen(Qt::NoPen);
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 2; ++c)
                p.drawEllipse(QPointF(8 + c * 5, 8 + r * 4), 1.0, 1.0);
        // Rotated title running up the strip. Its colour is the one thing a light scheme cannot get wrong:
        // the panel's name is all a folded side shows of itself.
        p.setPen(Chrome::stripInk());
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

// The inner edge of a fly-out: pull it to make the panel wider. A fly-out lies OVER the drawing rather
// than being part of the window's layout, so it has no splitter of its own - this is its splitter.
class FlyGrip : public QWidget {
public:
    explicit FlyGrip(QWidget* parent, bool vertical) : QWidget(parent), vertical_(vertical) {
        setObjectName(QStringLiteral("panelSideEdge"));
        setCursor(vertical ? Qt::SplitHCursor : Qt::SplitVCursor);
        setToolTip(QStringLiteral("Drag to set how wide this side opens — all of its panels together"));
    }
    std::function<void(QPoint)> onDrag;   // how far the grip has been pulled since the last call
    bool beingDragged() const { return dragging_; }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), Chrome::edge());
        p.setPen(Qt::NoPen);
        p.setBrush(Chrome::grip());
        const QPointF c(width() / 2.0, height() / 2.0);
        for (int i = -1; i <= 1; ++i)
            p.drawEllipse(vertical_ ? QPointF(c.x(), c.y() + i * 5) : QPointF(c.x() + i * 5, c.y()), 1.0,
                          1.0);
    }
    void mousePressEvent(QMouseEvent* e) override {
        last_ = e->globalPosition().toPoint();
        dragging_ = true;
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (!dragging_) return;
        const QPoint now = e->globalPosition().toPoint();
        if (onDrag) onDrag(now - last_);
        last_ = now;
    }
    void mouseReleaseEvent(QMouseEvent*) override { dragging_ = false; }

private:
    bool vertical_;      // the grip runs up and down: it sets a WIDTH
    QPoint last_;
    bool dragging_ = false;
};

// A dock that can auto-hide. Pinning is per SIDE, not per panel: every panel docked
// on the same side (the left column, the right column, ...) pins, folds and expands
// as one unit, and the left and right sides are completely independent of each other.
// A side that is unpinned folds away AT ONCE and completely, leaving only a
// VerticalStrip per panel; hovering a strip brings the whole side back until the
// cursor leaves. The side groups are worked out from where the panels actually are, so
// dragging a panel from one side to the other re-forms both groups.
class AutoHideDock : public QDockWidget {
public:
    AutoHideDock(const QString& title, QMainWindow* mw)
        : QDockWidget(title, mw), mw_(mw), title_(title) {
        // A name of its own, because that is how a panel is found again in the saved arrangement:
        // QMainWindow::saveState()/restoreState() key everything by objectName, and a panel without one
        // is silently left out.
        setObjectName(QStringLiteral("panel_") + title);
        allDocks_.push_back(this);
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
        // Landing on a different side means joining that side's group: the panel takes
        // on that side's pin state, and the single pin moves to whichever panel is now
        // on top. Both sides are re-checked because one of them just lost a member.
        QObject::connect(this, &QDockWidget::dockLocationChanged, this,
                         [this](Qt::DockWidgetArea) { adoptArea(); });
        QObject::connect(this, &QDockWidget::topLevelChanged, this,
                         [this](bool) { adoptArea(); });
    }

    // Provide the custom title bar + the real content widget.
    void setManaged(QWidget* titleBar, QWidget* content) {
        titleBar_ = titleBar;
        content_ = content;
        // A PANEL HAS TO BE DRAGGABLE NARROW.
        //
        // A dock is only as small as what is in it, and what is in these is captions: "Open the bill of
        // materials" on a button is 334 pixels of minimum all by itself, so the right-hand side could not
        // be pulled in past about 340 however hard it was dragged. The widest caption in a panel should
        // not decide how much of the window that panel is entitled to.
        //
        // So the panel is given a floor of its own, well under any caption, and the captions inside are
        // told they may be squeezed (see letSqueeze). Set here rather than at each panel, because it is
        // true of every one of them and a panel added later would otherwise quietly pin its side again.
        content_->setMinimumWidth(kNarrowestPanel);
        strip_ = new VerticalStrip(title_, this);
        // Hovering any strip on this side expands the whole side group.
        strip_->onEnter = [this] {
            if (pinned_ || awaitExit_) return;
            for (auto* d : group()) d->expandIfCollapsed();
        };
        strip_->hide();
        emptyBar_ = new QWidget(this);
        emptyBar_->setFixedHeight(0);
        setTitleBarWidget(titleBar_);
        setWidget(content_);
        // Rolled up the last time the program was used, rolled up now.
        sectionFolded_ = QSettings().value(QStringLiteral("ui/foldedPanels")).toStringList().contains(objectName());
        applySectionFold();
    }

    // ---- a panel rolled up to its title -----------------------------------------------------------------
    //
    // A SIDE HOLDS SEVERAL PANELS, and not all of them are wanted open at once: Properties is read all day,
    // the bill of materials now and then, the calculator for a sum. The arrow on a panel's title bar rolls
    // it up to that bar alone, and the panels beside it on the side take the room it gave up; the arrow
    // again opens it. Remembered between sessions, like the side's width and pin.
    bool sectionFolded() const { return sectionFolded_; }
    void setFoldButton(QToolButton* b) { foldBtn_ = b; applySectionFold(); }
    void setSectionFolded(bool folded) {
        if (sectionFolded_ == folded) return;
        // How tall it was - and every panel beside it - so opening it again gives the side back as it
        // stood, rather than this one the least the others can spare.
        if (folded && !collapsed_ && !isFloating() && height() > 60 && mw_) {
            sectionHeight_ = height();
            sideDocks_.clear();
            sideHeights_.clear();
            for (AutoHideDock* d : allDocks_)
                if (d && d->mw_ == mw_ && !d->isFloating() && d->isVisible() && d->areaKey() == areaKey()) {
                    sideDocks_ << QPointer<QDockWidget>(d);
                    sideHeights_ << d->height();
                }
        }
        sectionFolded_ = folded;
        QSettings st;
        QStringList rolled = st.value(QStringLiteral("ui/foldedPanels")).toStringList();
        rolled.removeAll(objectName());
        if (folded) rolled << objectName();
        st.setValue(QStringLiteral("ui/foldedPanels"), rolled);
        applySectionFold();
        if (!folded && sectionHeight_ > 60 && mw_ && !collapsed_ && !isFloating()) {
            const int want = sectionHeight_;
            QTimer::singleShot(0, this, [this, want] {
                if (!mw_ || sectionFolded_ || isFloating()) return;
                QList<QDockWidget*> docks;
                QList<int> sizes;
                for (int i = 0; i < sideDocks_.size(); ++i)
                    if (sideDocks_[i] && !sideDocks_[i]->isFloating() && sideDocks_[i]->isVisible()) {
                        docks << sideDocks_[i].data();
                        sizes << sideHeights_[i];
                    }
                if (!docks.contains(this)) {
                    docks = {this};
                    sizes = {want};
                }
                mw_->resizeDocks(docks, sizes, Qt::Vertical);
            });
        }
    }
    // What the fold means to the panel as it stands. Folded away as a strip on an unpinned side it
    // means nothing yet - the strip is the strip - and it is put right when the panel opens out again.
    void applySectionFold() {
        if (foldBtn_) {
            foldBtn_->setText(sectionFolded_ ? QString(QChar(0x25B8)) : QString(QChar(0x25BE)));   // > or v
            foldBtn_->setToolTip(sectionFolded_ ? QStringLiteral("Rolled up - click to open this panel")
                                                : QStringLiteral("Roll this panel up to its title"));
        }
        if (collapsed_ || !content_ || !titleBar_ || content_->parentWidget() != this) return;
        content_->setVisible(!sectionFolded_);
        if (sectionFolded_)
            setMaximumHeight(titleBar_->sizeHint().height() + 2);
        else
            setMaximumHeight(QWIDGETSIZE_MAX);
    }

    // Register the title bar's pin button, so the side can keep it in sync and show
    // it on the top panel only.
    void setPinButton(QToolButton* b) { pinBtn_ = b; }

    // Where a drag would land: decided by WHERE THE POINTER IS, not by what the panel's rectangle
    // happens to overlap. Over a docked panel, it stacks into that panel's column (above it in its
    // upper half, below in its lower); over the edge band of a side, it goes on that side; anywhere
    // else it stays floating.
    //
    // It used to be the panel's whole rectangle, the largest overlap winning - generous, and wrong
    // for a tall panel: the calculator let go of in the middle of the drawing reached down into the
    // bottom band and was docked along the bottom of the window, which nobody had aimed at.
    struct DropTarget {
        Qt::DockWidgetArea area = Qt::NoDockWidgetArea;
        QDockWidget* onto = nullptr; // existing panel to stack with (null = the bare side)
        bool above = false;          // stack above `onto`, else below it
        QRect indicator;             // where it lands, in global coordinates
    };
    DropTarget dropTargetAt(const QPoint& at) const {
        DropTarget best;
        if (!mw_) return best;
        const QRect win(mw_->mapToGlobal(QPoint(0, 0)), mw_->size());
        if (!win.contains(at)) return best; // outside the window: stay floating

        // 1) Over a docked panel: into its column, above or below it.
        for (QDockWidget* d : mw_->findChildren<QDockWidget*>()) {
            if (d == this || d->isFloating() || !d->isVisible()) continue;
            const Qt::DockWidgetArea da = mw_->dockWidgetArea(d);
            if (da == Qt::NoDockWidgetArea) continue;
            const QRect r(d->mapToGlobal(QPoint(0, 0)), d->size());
            if (!r.contains(at)) continue;
            best.area = da;
            best.onto = d;
            best.above = at.y() < r.center().y();
            best.indicator = r;
            return best;
        }

        // 2) Over the edge band of a side - the left and right first, as those are the side menus
        //    and what a panel is nearly always being taken to.
        const int bandW = std::max(90, win.width() / 8);
        const int bandH = std::max(50, win.height() / 10);
        const QRect left(win.left(), win.top(), bandW, win.height());
        const QRect right(win.right() - bandW + 1, win.top(), bandW, win.height());
        const QRect top(win.left(), win.top(), win.width(), bandH);
        const QRect bottom(win.left(), win.bottom() - bandH + 1, win.width(), bandH);
        if (left.contains(at)) best = {Qt::LeftDockWidgetArea, nullptr, false, left};
        else if (right.contains(at)) best = {Qt::RightDockWidgetArea, nullptr, false, right};
        else if (top.contains(at)) best = {Qt::TopDockWidgetArea, nullptr, false, top};
        else if (bottom.contains(at)) best = {Qt::BottomDockWidgetArea, nullptr, false, bottom};
        return best;
    }

    // Attach the panel wherever it is currently touching (nothing = stay floating).
    void redockHere(const QPoint& at) {
        if (!mw_) return;
        hideDropHint();
        const DropTarget t = dropTargetAt(at);
        if (t.area == Qt::NoDockWidgetArea) return;
        // A SIDE SOMETHING IS PUT ON IS OPENED. Dropped onto a folded side, the panel folded with it
        // - a strip as thin as the others, when it had just been put there to be used. Putting a
        // panel on a side is asking to see it there, so the side is pinned open with it.
        //
        // AND THE SIDE KEEPS ITS WIDTH. Opening a side and adding a panel to it both lay the side out
        // afresh, and the passes in between squeeze every panel on it to its narrowest - which each of
        // them took for the user dragging the side narrow, and wrote down: the side came back 150 wide.
        // So the width it had is taken now and given back once the window has settled.
        const int side = static_cast<int>(t.area);
        const int keep = areaExtent_.value(mw_).value(side, 0);
        settingExtents_ = true;
        if (!areaPinnedState(side)) {
            areaPinned_[side] = true;
            for (AutoHideDock* d : allDocks_) {
                if (!d || d == this || d->mw_ != mw_ || d->isFloating() || d->areaKey() != side) continue;
                d->applyPinned(true);
                d->syncPinButton(true);
            }
        }
        setFloating(false);
        mw_->addDockWidget(t.area, this);
        // splitDockWidget(first, second, Vertical): first on top, second below.
        if (t.onto) {
            if (t.above) mw_->splitDockWidget(this, t.onto, Qt::Vertical);
            else mw_->splitDockWidget(t.onto, this, Qt::Vertical);
        }
        if (collapsed_) expand();
        QPointer<AutoHideDock> self(this);
        QTimer::singleShot(250, this, [self, side, keep] {
            if (!self || !self->mw_) return;
            if (keep > kSmallestExtent) self->areaExtent_[self->mw_].insert(side, keep);
            for (AutoHideDock* d : allDocks_)
                if (d && d->mw_ == self->mw_ && !d->isFloating() && d->areaKey() == side) {
                    if (keep > kSmallestExtent) d->expandedExtent_ = keep;
                    d->applyRememberedExtent();
                }
            settingExtents_ = true;   // what applying it shakes loose is not the user either
            QTimer::singleShot(300, [] { settingExtents_ = false; });
            refreshPinButtons();
        });
    }

    // FOLDED, A PANEL IS A STRIP with no title bar - fine against the side of the window, and useless
    // on its own: taken off the side like that it floated as a sliver that could be neither moved nor
    // opened. So it is opened out first, whatever takes it off the side.
    void unfoldToFloat() {
        if (collapsed_) expand();
    }
    // FLOATING, AT A SIZE THAT HOLDS IT - the size it has on the side, not the strip's. A folded panel
    // taken off the side kept the strip's size and came out as a sliver too small for its own title
    // bar. Only a panel that small is touched: one floated at a size somebody chose keeps it.
    void fitFloating() {
        if (!isFloating() || (width() >= kNarrowestPanel && height() >= 120)) return;
        const int w = std::max(rememberedExtent() > kSmallestExtent ? rememberedExtent() : 0,
                               std::max(kNarrowestPanel * 2, content_ ? content_->sizeHint().width() : 0));
        const int h = std::max(240, (content_ ? content_->sizeHint().height() : 0) +
                                        (titleBar_ ? titleBar_->sizeHint().height() : 0));
        resize(w, h);
    }

    // Translucent highlight of where the drag will land, so it is obvious that the
    // panel is already touching a side and will jump there when released.
    void showDropHint(const QPoint& at) {
        if (!mw_) return;
        const DropTarget t = dropTargetAt(at);
        if (t.area == Qt::NoDockWidgetArea) { hideDropHint(); return; }
        if (!hint_) {
            hint_ = new QWidget(mw_);
            hint_->setAttribute(Qt::WA_TransparentForMouseEvents);
            hint_->setStyleSheet(QStringLiteral(
                "background:rgba(90,150,230,60);border:2px solid #7fb2ff;"));
        }
        hint_->setGeometry(
            QRect(mw_->mapFromGlobal(t.indicator.topLeft()), t.indicator.size()));
        hint_->show();
        hint_->raise();
    }
    void hideDropHint() {
        if (hint_) hint_->hide();
    }

    // Pin / unpin THIS SIDE. The other sides are left exactly as they are — the left
    // and right columns are independent.
    void setPinned(bool v) {
        // THE SIDE'S WIDTH, TAKEN ONCE, BEFORE ANY OF IT FOLDS. The panels fold one after another, and
        // each one folded narrows the column the rest are measured in: the second panel on a side was
        // measured squeezed to its narrowest and wrote that down, so every side of two or more panels
        // came back 150 wide the next time it was opened.
        if (!v && !collapsed_ && !isFloating() && isVisible()) {
            const int px = horizontalArea() ? width() : height();
            if (px > kSmallestExtent) setRememberedExtent(px, horizontalArea());
        }
        settingExtents_ = true;
        areaPinned_[areaKey()] = v;
        for (auto* d : group()) {
            d->applyPinned(v);
            d->syncPinButton(v);
        }
        QTimer::singleShot(300, [] { settingExtents_ = false; });
        refreshPinButtons();
    }

    void expandIfCollapsed() { if (collapsed_) showFlyout(); }

    // ---- the fly-out ----------------------------------------------------------------------------------
    //
    // A folded side comes back as a fly-out: the panel laid OVER the drawing, hanging off its own strip.
    // It used to come back by making the dock wide again, and a dock is part of the window's layout - so
    // opening a panel shoved the drawing sideways, and closing it shoved it back. Nothing was gained by
    // that: a panel that is only open while the cursor is on it is a thing held up in front of the
    // drawing, not a thing the drawing has to make room for. It also fixes the width: a fly-out is ours
    // to size, where a dock's width is settled between it, the panels above and below it, and the canvas
    // - which is why a side pulled wider used to come back narrow.
    bool flyoutOpen() const { return flyout_ && flyout_->isVisible(); }

    // A fly-out is held over the drawing, so it needs a ground of its own and an edge to be seen against
    // whatever is under it - in whichever scheme.
    void restyleFlyout() {
        if (!flyout_) return;
        flyout_->setStyleSheet(QStringLiteral("QWidget#panelFlyout{background:%1;border:1px solid %2;}")
                                   .arg(Chrome::flyout().name(), Chrome::flyoutBorder().name()));
    }

    // The scheme has changed: every panel repaints its strip, re-grounds its fly-out and re-colours its
    // edge. The strips and edges paint themselves, so they only need telling to.
    static void restyleAll() {
        for (AutoHideDock* d : allDocks_) {
            if (!d) continue;
            d->restyleFlyout();
            if (d->strip_) d->strip_->update();
        }
        for (auto win = sideGrip_.cbegin(); win != sideGrip_.cend(); ++win)
            for (auto side = win->cbegin(); side != win->cend(); ++side)
                if (*side) (*side)->update();
    }

    void showFlyout() {
        if (pinned_ || isFloating() || !collapsed_ || !mw_ || !titleBar_ || !content_) return;
        if (!flyout_) {
            flyout_ = new QWidget(mw_);
            flyout_->setObjectName(QStringLiteral("panelFlyout"));
            flyout_->setAutoFillBackground(true);
            restyleFlyout();
            body_ = new QWidget(flyout_);
            auto* v = new QVBoxLayout(body_);
            v->setContentsMargins(0, 0, 0, 0);
            v->setSpacing(0);
        }
        if (auto* v = qobject_cast<QVBoxLayout*>(body_->layout())) {
            if (titleBar_->parentWidget() != body_) v->addWidget(titleBar_);
            if (content_->parentWidget() != body_) v->addWidget(content_, 1);
        }
        titleBar_->show();
        content_->show();
        flyout_->show();
        flyout_->raise();
        layoutFlyout();
        outside_.invalidate();
    }

    void hideFlyout() {
        if (!flyout_ || onTheWayOut(mw_)) return;
        const int side = areaKey();
        flyout_->hide();
        // The pieces go back to the dock, out of sight, so pinning the side can put them straight back in.
        //
        // ONLY THE PIECES THAT ARE IN THE FLY-OUT. This runs on every hide of the panel - and floating
        // one hides it for a moment - so it used to reach into a panel that had long since been opened
        // out, take its title bar and its content away and hide them: the panel floated as its bare
        // frame, locked at the size of a title bar with nothing in it.
        if (titleBar_ && titleBar_->parentWidget() == body_) { titleBar_->setParent(this); titleBar_->hide(); }
        if (content_ && content_->parentWidget() == body_) { content_->setParent(this); content_->hide(); }
        layoutSide(mw_, side);   // the side's own edge goes with the last panel on it
    }

    // Lay out every fly-out on a side, and the ONE edge that sets how wide they all open. One edge, because
    // the width belongs to the side: an edge per panel says the panels can be set to different widths, and
    // they cannot - they share the column the moment the side is pinned again.
    void layoutFlyout() { layoutSide(mw_, areaKey()); }

    static void layoutSide(QMainWindow* mw, int side) {
        if (!mw || onTheWayOut(mw) || side == static_cast<int>(Qt::NoDockWidgetArea)) return;
        const auto area = static_cast<Qt::DockWidgetArea>(side);
        const bool vertical = area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea;
        const int grip = 8;
        QRect together;
        std::vector<AutoHideDock*> open;
        for (AutoHideDock* d : allDocks_) {
            if (!d || d->mw_ != mw || d->isFloating() || d->areaKey() != side || !d->flyoutOpen()) continue;
            d->placeFlyout(area, grip);
            open.push_back(d);
        }
        // ONE ABOVE THE OTHER, NOT ON TOP OF EACH OTHER. The whole side flies out together, each panel
        // level with its strip - and a fly-out is at least 200 tall where a strip may be 120, so each one
        // reached down over the next and hid its title bar: the panel underneath could be neither read
        // nor taken hold of. Each now ends where the next one along the side begins.
        std::sort(open.begin(), open.end(), [vertical](AutoHideDock* a, AutoHideDock* b) {
            return vertical ? a->flyout_->y() < b->flyout_->y() : a->flyout_->x() < b->flyout_->x();
        });
        for (size_t i = 0; i + 1 < open.size(); ++i) {
            QRect r = open[i]->flyout_->geometry();
            const QRect next = open[i + 1]->flyout_->geometry();
            if (vertical && r.bottom() >= next.top()) r.setBottom(next.top() - 1);
            if (!vertical && r.right() >= next.left()) r.setRight(next.left() - 1);
            open[i]->flyout_->setGeometry(r);
            open[i]->body_->setGeometry(0, 0, r.width(), r.height());
        }
        for (AutoHideDock* d : open) {
            const QRect r = d->flyout_->geometry();
            together = together.isNull() ? r : together.united(r);
        }
        QPointer<FlyGrip>& edge = sideGrip_[mw][side];
        if (together.isNull()) {                 // nothing open on this side any more
            if (edge) edge->hide();
            return;
        }
        if (!edge) {
            edge = new FlyGrip(mw, vertical);
            edge->onDrag = [mw, side](QPoint by) {
                // Whatever the edge is pulled to is the SIDE'S width, so every panel on it follows.
                for (AutoHideDock* d : allDocks_) {
                    if (!d || d->mw_ != mw || d->areaKey() != side) continue;
                    int px = d->rememberedExtent();
                    switch (static_cast<Qt::DockWidgetArea>(side)) {
                        case Qt::RightDockWidgetArea: px -= by.x(); break;
                        case Qt::LeftDockWidgetArea: px += by.x(); break;
                        case Qt::BottomDockWidgetArea: px -= by.y(); break;
                        default: px += by.y(); break;
                    }
                    d->setRememberedExtent(px, d->horizontalArea());
                    break;   // one width a side: telling one panel tells the side
                }
                layoutSide(mw, side);
            };
        }
        edge->setCursor(vertical ? Qt::SplitHCursor : Qt::SplitVCursor);
        // The edge runs the whole length of the side's fly-outs, on the face turned to the drawing.
        if (vertical)
            edge->setGeometry(area == Qt::RightDockWidgetArea ? together.left() - grip : together.right() + 1,
                              together.top(), grip, together.height());
        else
            edge->setGeometry(together.left(),
                              area == Qt::BottomDockWidgetArea ? together.top() - grip
                                                               : together.bottom() + 1,
                              together.width(), grip);
        edge->show();
        edge->raise();
    }

    // Where one fly-out hangs: off the inner edge of its own strip, as tall as the strip, never past the
    // far side of the window, and leaving room for the side's edge.
    QRect placeFlyout(Qt::DockWidgetArea area, int grip) {
        const QPoint at = mapTo(mw_, QPoint(0, 0));
        QRect r;
        if (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea) {
            const int want = std::max(180, rememberedExtent() > kSmallestExtent ? rememberedExtent() : 300);
            const int room = area == Qt::LeftDockWidgetArea ? mw_->width() - at.x() - width() - 8
                                                            : at.x() - 8;
            const int w = std::max(120, std::min(want, room)) - grip;
            const int h = std::min(std::max(200, height()), mw_->height() - at.y());
            r = QRect(area == Qt::LeftDockWidgetArea ? at.x() + width() : at.x() - w, at.y(), w, h);
        } else {
            const int want = std::max(140, rememberedExtent() > kSmallestExtent ? rememberedExtent() : 240);
            const int room = area == Qt::TopDockWidgetArea ? mw_->height() - at.y() - height() - 8
                                                           : at.y() - 8;
            const int h = std::max(100, std::min(want, room)) - grip;
            r = QRect(at.x(), area == Qt::TopDockWidgetArea ? at.y() + height() : at.y() - h, width(), h);
        }
        flyout_->setGeometry(r);
        body_->setGeometry(0, 0, r.width(), r.height());
        return r;
    }

    // ---- how wide the panel is, and staying that wide ------------------------------------------------
    //
    // A side pulled wider is meant to STAY that wide: folded away and hovered open again, pinned and
    // unpinned, and after the program has been shut and started again. So the width is not read off the
    // panel when it is wanted - it is remembered the moment the panel is resized by hand, kept through
    // folding, and written to the settings on the way out.
    // The width belongs to the SIDE, not to the panel. Every panel on the right opens at the right side's
    // width - they share the column when they are pinned, so they have to agree when they fly out too;
    // panels on one side opening at three different widths is not an arrangement, it is a mess. The
    // panel's own number is only the fallback, and what gets written down for it.
    int rememberedExtent() {
        const int side = areaExtent_.value(mw_).value(areaKey(), 0);
        return side > kSmallestExtent ? side : expandedExtent_;
    }
    // What to write down on the way out: the width the user chose, or - for a panel never touched this
    // session - the width it actually has, so what comes back is what the screen looked like.
    int extentForSaving() {
        if (rememberedExtent() > kSmallestExtent) return rememberedExtent();
        if (collapsed_ || isFloating() || isHidden()) return 0;
        const int px = horizontalExtent_ ? width() : height();
        return px > kSmallestExtent ? px : 0;
    }
    bool extentIsWidth() const { return horizontalExtent_; }
    bool folded() const { return collapsed_; }
    void setRememberedExtent(int px, bool asWidth) {
        if (px <= kSmallestExtent) return;   // nonsense, or a panel that was never laid out
        expandedExtent_ = px;
        horizontalExtent_ = asWidth;
        const int side = areaKey();
        if (side != static_cast<int>(Qt::NoDockWidgetArea)) areaExtent_[mw_].insert(side, px);
    }
    // Give the panel the width it is remembered as, now. Wanted once the window is on screen: resizing a
    // dock before the layout exists does nothing.
    void applyRememberedExtent() {
        if (collapsed_ || isFloating() || !mw_ || rememberedExtent() <= kSmallestExtent) return;
        if (isHidden()) return;   // a panel nobody can see has no width worth insisting on
        const int want = rememberedExtent();
        suppressResize_ = true;
        settingExtents_ = true;
        // A width ASKED for (resizeDocks) can be quietly dropped when the window's layout has not settled
        // yet, which is what made a saved width look as though it had never been saved. A width the panel
        // will not go below cannot be dropped - so the width is set as a floor, the layout is made to
        // honour it, and the floor is taken away again once it has.
        if (horizontalExtent_) setMinimumWidth(want);
        else setMinimumHeight(want);
        mw_->resizeDocks({this}, {want}, horizontalExtent_ ? Qt::Horizontal : Qt::Vertical);
        // Made to happen NOW rather than whenever the window next lays itself out: the floor has to be
        // honoured before it is taken away again, and leaving that to two timers is a race - which is
        // exactly why the width came back right some of the time and not the rest.
        if (QLayout* l = mw_->layout()) l->activate();
        setMinimumSize(0, 0);
        // The window may lay itself out again a pass or two later, and settle the panel somewhere of its
        // own choosing. That is not the user speaking either, so it is ignored for a moment longer.
        QTimer::singleShot(150, this, [this] {
            suppressResize_ = false;
            settingExtents_ = false;
        });
    }

    // Which side the panel is on, and whose window it belongs to: both wanted from outside, to work out
    // one width a side and to leave another window's panels alone.
    int sideKey() { return areaKey(); }
    QMainWindow* owner() const { return mw_; }

    static const QVector<AutoHideDock*>& all() { return allDocks_; }

    // The window is going. Said by MainWindow's own destructor, which runs BEFORE the panels are taken
    // apart - and that order is the whole point. A panel hidden as its window comes down still gets a hide
    // event, and answering one means asking the window which side the panel is on - of a window whose
    // layout has already gone. Nothing is worth doing for a window on its way out.
    static void windowGoing(QMainWindow* mw) {
        going_.insert(mw);
        sideGrip_.remove(mw);
        areaExtent_.remove(mw);
    }
    static bool onTheWayOut(QMainWindow* mw) { return mw && going_.contains(mw); }

    // Which sides are pinned, as something that can be written down and read back: one "area:0|1" a side.
    static QStringList pinnedSnapshot() {
        QStringList out;
        for (auto it = areaPinned_.cbegin(); it != areaPinned_.cend(); ++it)
            out.append(QStringLiteral("%1:%2").arg(it.key()).arg(it.value() ? 1 : 0));
        return out;
    }
    static void restorePinned(const QStringList& saved) {
        for (const QString& one : saved) {
            const QStringList bits = one.split(QLatin1Char(':'));
            if (bits.size() != 2) continue;
            areaPinned_.insert(bits.at(0).toInt(), bits.at(1).toInt() != 0);
        }
        for (AutoHideDock* d : allDocks_) {
            if (!d || d->isFloating() || !d->isVisible()) continue;
            const bool v = areaPinnedState(d->areaKey());
            d->applyPinned(v);
            d->syncPinButton(v);
        }
        refreshPinButtons();
    }

    // One pin per side, on the panel at the top of it: a side made of several panels
    // (Properties + Layers, say) is pinned as a whole, so it needs a single control.
    // Floating palettes keep their own.
    static void refreshPinButtons() {
        QHash<int, AutoHideDock*> topOfSide;
        for (AutoHideDock* d : allDocks_) {
            if (!d || !d->pinBtn_) continue;
            // A floating palette carries its own pin. One that is not on screen is not part of any
            // side's reckoning - and must not be given a pin on the strength of being invisible.
            if (d->isFloating()) {
                d->pinBtn_->setVisible(true);
                continue;
            }
            if (!d->isVisible()) {
                d->pinBtn_->setVisible(false);
                continue;
            }
            const int key = d->areaKey();
            if (key == static_cast<int>(Qt::NoDockWidgetArea)) {
                d->pinBtn_->setVisible(true);
                continue;
            }
            AutoHideDock* cur = topOfSide.value(key, nullptr);
            if (!cur) {
                topOfSide.insert(key, d);
                continue;
            }
            // "Top" is up the column on the left/right sides, leftmost across the
            // top/bottom ones.
            const bool vertical = key == static_cast<int>(Qt::LeftDockWidgetArea) ||
                                  key == static_cast<int>(Qt::RightDockWidgetArea);
            const QPoint a = d->mapToGlobal(QPoint(0, 0));
            const QPoint b = cur->mapToGlobal(QPoint(0, 0));
            if (vertical ? a.y() < b.y() : a.x() < b.x()) topOfSide.insert(key, d);
        }
        for (AutoHideDock* d : allDocks_) {
            if (!d || !d->pinBtn_ || d->isFloating() || !d->isVisible()) continue;
            const int key = d->areaKey();
            if (key == static_cast<int>(Qt::NoDockWidgetArea)) continue;
            d->pinBtn_->setVisible(topOfSide.value(key, nullptr) == d);
        }
    }

private:
    int areaKey() {
        if (!mw_ || isFloating()) return static_cast<int>(Qt::NoDockWidgetArea);
        return static_cast<int>(mw_->dockWidgetArea(this));
    }
    // Sides start out pinned; the state is remembered per side, so a panel dragged
    // onto a folded side folds with it.
    static bool areaPinnedState(int key) { return areaPinned_.value(key, true); }

    // Every panel currently docked on the same side as this one — the unit that pins,
    // folds and expands together. Derived from the live layout rather than fixed at
    // start-up, so moving a panel between sides re-forms the groups.
    QVector<AutoHideDock*> group() {
        QVector<AutoHideDock*> out;
        const int key = areaKey();
        if (key != static_cast<int>(Qt::NoDockWidgetArea))
            for (AutoHideDock* d : allDocks_)
                if (d && !d->isFloating() && d->isVisible() && d->areaKey() == key)
                    out.push_back(d);
        if (out.isEmpty()) out.push_back(this);
        return out;
    }

    void syncPinButton(bool v) {
        if (!pinBtn_ || pinBtn_->isChecked() == v) return;
        QSignalBlocker block(pinBtn_);
        pinBtn_->setChecked(v);
        pinBtn_->setIcon(makePinIcon(v));
    }

    // Joining a side (or being torn off it): adopt that side's pin state.
    void adoptArea() {
        if (!titleBar_ || !strip_) return; // still being built
        if (isFloating()) {
            unfoldToFloat();   // and never a strip
            QTimer::singleShot(0, this, [this] { fitFloating(); });
            pinned_ = true; // a floating palette is always shown in full
            poll_->stop();
            resizeTimer_->stop();
            awaitExit_ = false;
            syncPinButton(true);
        } else {
            const bool v = areaPinnedState(areaKey());
            applyPinned(v);
            syncPinButton(v);
        }
        // The panel's place inside its new side is not settled yet, so decide which
        // one carries the pin once the layout has caught up.
        QTimer::singleShot(0, this, [] { refreshPinButtons(); });
    }

    void applyPinned(bool v) {
        pinned_ = v;
        resizeTimer_->stop();
        if (pinned_) {
            poll_->stop();
            awaitExit_ = false;
            expand();
        } else {
            // Unpinning folds the side away at once and completely — not whenever the
            // cursor happens to wander off. The cursor is still on the panel (the pin
            // button is part of it), so hover-expand is held back until it leaves once,
            // otherwise the side would spring straight back open.
            awaitExit_ = true;
            collapse();
            poll_->start();
        }
    }

    bool horizontalArea() {
        const Qt::DockWidgetArea a = mw_->dockWidgetArea(this);
        return a != Qt::TopDockWidgetArea && a != Qt::BottomDockWidgetArea;
    }

    // Is the cursor on this side - a strip, a fly-out, or the edge that sets the width? With `slack`, is it
    // still WITHIN REACH of one: the cursor has to get properly clear of a side before it folds away.
    //
    // Reaching for the edge to pull the side wider takes the cursor off the panel, and off it by definition -
    // the edge is beyond the panel, over the drawing. A side that folded the moment the cursor left the
    // panel folded before the edge could be caught, every time. A little slack costs nothing: the panel is
    // still there while the hand is still at it.
    bool groupInside(int slack = 0) {
        const QPoint c = QCursor::pos();
        auto within = [&c, slack](QWidget* w) {
            if (!w || !w->isVisible()) return false;
            QRect r(w->mapToGlobal(QPoint(0, 0)), w->size());
            r.adjust(-slack, -slack, slack, slack);
            return r.contains(c);
        };
        for (auto* d : group()) {
            if (d->isFloating()) continue;
            if (within(d)) return true;
            if (d->flyoutOpen() && within(d->flyout_)) return true;
        }
        // The side's own edge - and whether it is being pulled at this moment, since a drag can take the
        // cursor well past any slack and a side that folded away mid-drag would be absurd.
        if (mw_) {
            const QPointer<FlyGrip> edge = sideGrip_.value(mw_).value(areaKey());
            if (edge && (edge->beingDragged() || within(edge))) return true;
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
        // Two questions, not one: ON the side (which opens it - a panel that flew out because the cursor
        // came within a hand's breadth of the edge would be in the way all day), and WITHIN REACH of it
        // (which keeps it open).
        const bool onIt = groupInside();
        const bool nearIt = onIt || groupInside(kFoldSlack);
        // Straight after an unpin the cursor is still on the panel; wait for it to
        // leave before hover-expand becomes live again.
        if (awaitExit_) {
            if (nearIt) {
                outside_.invalidate();
                return;
            }
            awaitExit_ = false;
        }
        if (onIt) {
            outside_.invalidate();
            if (collapsed_) showFlyout();
            else layoutFlyout();   // the window may have been moved or resized under it
        } else if (flyoutOpen()) {
            if (nearIt) {
                outside_.invalidate();   // the hand is still at it: the edge, or just off the panel
            } else if (!outside_.isValid())
                outside_.start();
            else if (outside_.elapsed() >= 500)
                hideFlyout();
        } else if (!collapsed_) {
            // Unpinned and somehow still full width (a panel just dragged onto a folded side): fold it.
            if (!outside_.isValid())
                outside_.start();
            else if (outside_.elapsed() >= 500)
                collapse();
        }
        if (flyoutOpen()) layoutFlyout();
    }

    void expand() {
        if (!collapsed_ || !titleBar_) return;
        hideFlyout();   // the panel is going back into the window; it is not held over the drawing as well
        collapsed_ = false;
        suppressResize_ = true; // ignore the programmatic resize below
        setTitleBarWidget(titleBar_);
        setWidget(content_);
        titleBar_->show();   // it was hidden while it lived in the fly-out
        content_->show();
        setMinimumSize(0, 0);
        setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        applyRememberedExtent();   // the width it was left at, made to stick
        applySectionFold();        // and rolled up, if it was
        outside_.invalidate();
        QTimer::singleShot(150, this, [this] { suppressResize_ = false; });
    }

    void collapse() {
        if (collapsed_ || isFloating() || !strip_) return;
        hideFlyout();
        horizontalExtent_ = horizontalArea();
        // Only a size the panel has really been given counts. Folding a panel that has not been laid out
        // yet - which is what happens when a side starts unpinned - used to overwrite the width the user
        // had chosen with whatever the panel measured before it was ever on screen.
        const int px = horizontalExtent_ ? width() : height();
        // Not while a side is being folded or laid out as a whole (settingExtents_): its width was
        // taken before any of it moved, and what the panels measure part-way through is not it.
        if (px > kSmallestExtent && !settingExtents_) setRememberedExtent(px, horizontalExtent_);
        collapsed_ = true;
        suppressResize_ = true;
        resizeTimer_->stop();
        setTitleBarWidget(emptyBar_);
        setWidget(strip_);
        strip_->show();
        setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);   // a rolled-up panel's strip is still a strip
        if (horizontalExtent_)
            setFixedWidth(24);
        else
            setFixedHeight(24);
        QTimer::singleShot(150, this, [this] { suppressResize_ = false; });
    }

protected:
    // Which panel carries the side's ONE pin is decided from where the panels actually sit, so it can
    // only be decided once they are on screen. It used to be settled during construction, when every
    // panel was still invisible: the code took the "not visible" branch for all of them, left every
    // pin showing and never came back - which is how a side made of two panels ended up with two pins.
    void showEvent(QShowEvent* e) override {
        QDockWidget::showEvent(e);
        if (onTheWayOut(mw_)) return;
        QTimer::singleShot(0, this, [] { refreshPinButtons(); });
    }
    void hideEvent(QHideEvent* e) override {
        QDockWidget::hideEvent(e);
        if (onTheWayOut(mw_)) return;
        hideFlyout();   // a panel closed by its x takes its fly-out with it
        // A panel closing hands the pin to whatever is now at the top of that side.
        QTimer::singleShot(0, this, [] { refreshPinButtons(); });
    }
    void moveEvent(QMoveEvent* e) override {
        QDockWidget::moveEvent(e);
        // Re-ordering a side changes which panel is topmost. Deferred and coalesced: a drag emits
        // these by the dozen.
        if (!pinRefresh_) {
            pinRefresh_ = new QTimer(this);
            pinRefresh_->setSingleShot(true);
            pinRefresh_->setInterval(60);
            QObject::connect(pinRefresh_, &QTimer::timeout, this, [] { refreshPinButtons(); });
        }
        pinRefresh_->start();
    }

    // A manual resize of an unpinned panel re-arms a 3 s countdown; once the
    // user stops dragging, the side folds away.
    void resizeEvent(QResizeEvent* e) override {
        QDockWidget::resizeEvent(e);
        if (collapsed_ || isFloating() || suppressResize_) return;
        // Whatever the panel is dragged to is what it is now remembered as - pinned or not. This is the one
        // place the size is taken from the user, so folding, pinning and restarting all give back the same
        // number rather than each measuring the panel at a different moment.
        // Only a panel that is ON THE SCREEN can say anything about its width. One not yet shown, or
        // hidden, is resized by the layout for its own reasons - that is how a panel the user had never
        // opened came to hold a width and, since the panels on a side share one, to impose it on the side.
        // settingExtents_ is the other half of it: while a saved width is being handed out, the rest of the
        // side is resized along with it, and none of that is the user speaking.
        if (e->oldSize().isValid() && isVisible() && !settingExtents_) {
            const bool asWidth = horizontalArea();
            const int px = asWidth ? width() : height();
            if (px > kSmallestExtent) setRememberedExtent(px, asWidth);
        }
        if (pinned_) return;
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
    QWidget* flyout_ = nullptr;        // the panel held over the drawing while an unpinned side is open
    QWidget* body_ = nullptr;          // the title bar + the panel, inside the fly-out
    QTimer* poll_ = nullptr;
    QTimer* resizeTimer_ = nullptr;    // fires 3s after the user finishes resizing
    QTimer* pinRefresh_ = nullptr;     // coalesces "which panel holds the pin" during a drag
    QElapsedTimer outside_;
    bool pinned_ = true;
    bool collapsed_ = false;
    bool sectionFolded_ = false;       // rolled up to its title bar (the arrow on it)
    int sectionHeight_ = 0;            // how tall it was before it was rolled up
    QList<QPointer<QDockWidget>> sideDocks_;   // and the side as it stood then
    QList<int> sideHeights_;
    QToolButton* foldBtn_ = nullptr;
    bool horizontalExtent_ = true;
    bool suppressResize_ = false;      // ignore our own (programmatic) resizes
    bool awaitExit_ = false;           // just unpinned: hold hover-expand until the cursor leaves
    int expandedExtent_ = 0;
    // Below this a number is not a panel width anybody chose - it is a strip (24 px), or a panel measured
    // before it was ever laid out.
    static constexpr int kSmallestExtent = 60;
    // How far the cursor has to get from a folded side before it folds away again. Enough to reach the edge
    // that sets the width - which is over the drawing, past the panel - and to overshoot it a little.
    static constexpr int kFoldSlack = 30;
    QToolButton* pinBtn_ = nullptr;    // the title bar's pin toggle (synced across the side)
    QWidget* hint_ = nullptr;          // drop highlight shown while dragging
    // Window -> side -> how wide that side opens. Per window, because a program with two windows open must
    // not have one window's panels deciding how wide the other's are.
    static QHash<QMainWindow*, QHash<int, int>> areaExtent_;
    // Window -> side -> the ONE edge that sets how wide that side's fly-outs open.
    // Guarded pointers: the edge belongs to the window, so it dies with it - and a later window can be
    // handed the very same address by the allocator, which must not hand it a dead edge with it.
    static QHash<QMainWindow*, QHash<int, QPointer<FlyGrip>>> sideGrip_;
    static QSet<QMainWindow*> going_;         // windows being taken apart: leave their panels alone
    static bool settingExtents_;              // a saved width is being handed out; ignore the fall-out
    static QVector<AutoHideDock*> allDocks_;  // every auto-hide dock (to find side groups)
    static QHash<int, bool> areaPinned_;      // Qt::DockWidgetArea -> pinned, per side

public:
    ~AutoHideDock() override {
        allDocks_.removeAll(this);
        // The last panel of a window gone: forget that window's side widths and its edges. Another window
        // can be given the same address later, and would otherwise inherit them.
        bool anyLeft = false;
        for (AutoHideDock* d : allDocks_)
            if (d && d->mw_ == mw_) anyLeft = true;
        if (!anyLeft) {
            areaExtent_.remove(mw_);
            sideGrip_.remove(mw_);
            going_.remove(mw_);   // the address can be handed to another window; it starts clean
        }
    }
};

QHash<QMainWindow*, QHash<int, int>> AutoHideDock::areaExtent_;
QHash<QMainWindow*, QHash<int, QPointer<FlyGrip>>> AutoHideDock::sideGrip_;
QSet<QMainWindow*> AutoHideDock::going_;
bool AutoHideDock::settingExtents_ = false;
QVector<AutoHideDock*> AutoHideDock::allDocks_;
QHash<int, bool> AutoHideDock::areaPinned_;

// Custom panel title bar: a dots grip (the only drag handle), the title, a
// pin/auto-hide toggle, and a close button.
class DockTitleBar : public QWidget {
public:
    DockTitleBar(AutoHideDock* dock, const QString& title)
        : QWidget(dock), dock_(dock) {
        allBars_.push_back(this);
        restyle();
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

        // The arrow that rolls the panel up to this bar, and opens it again.
        auto* fold = new QToolButton(this);
        fold->setAutoRaise(true);
        fold->setFocusPolicy(Qt::NoFocus);
        fold->setFixedWidth(18);
        fold->setStyleSheet(QStringLiteral("QToolButton{border:none;background:transparent;}"));
        QObject::connect(fold, &QToolButton::clicked, dock,
                         [dock] { dock->setSectionFolded(!dock->sectionFolded()); });
        lay->addWidget(fold);
        dock->setFoldButton(fold);

        lbl_ = new QLabel(title, this);
        // THE NAME MUST NOT DECIDE HOW NARROW THE PANEL MAY BE PULLED.
        //
        // A title bar is as wide as its title, and the dock is at least as wide as its title bar - so
        // "BILL OF MATERIALS" put a 330-pixel floor under the whole right-hand side, and no amount of
        // dragging would bring it in past that. The side widened happily and would not narrow, which is
        // what "cannot change the width" looks like from the outside.
        //
        // So the name may be squeezed, and is elided when it is - the full name is on the tooltip, and on
        // the strip the panel folds down to, so nothing is lost by clipping it.
        lbl_->setToolTip(title);
        letSqueeze(lbl_);
        lay->addWidget(lbl_, 1);
        restyle();   // the label exists now, so it gets its colour too

        auto* pin = new QToolButton(this);
        pin->setCheckable(true);
        pin->setChecked(true);
        pin->setAutoRaise(true);
        pin->setFocusPolicy(Qt::NoFocus);
        pin->setIcon(makePinIcon(true));
        pin->setToolTip(QStringLiteral(
            "Pinned — click to fold this whole side away (this side only; the other side "
            "keeps its own pin)"));
        dock->setPinButton(pin);
        QObject::connect(pin, &QToolButton::toggled, dock, [pin, dock](bool on) {
            pin->setIcon(makePinIcon(on));
            pin->setToolTip(on ? QStringLiteral("Pinned — click to fold this whole side away")
                               : QStringLiteral("Folded away — click to pin this side open"));
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
    // THE WHOLE BAR IS THE HANDLE, the dots and the name alike - only the pin and the close button,
    // which take their own clicks, are not. Dragging is handled explicitly (Qt's native dock drag
    // does not work reliably through a custom title bar): a drag floats the panel and moves it, and
    // letting go over a side of the window docks it there.
    //
    // It used to be the dots alone. Taken by the rest of the bar a floating panel still went where
    // it was pulled, but by a path that never asked where it was over - so it could be moved about
    // and never put back into the left or right menu, and only the dots could do that.
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) {
            // THE WHOLE DRAG IS THIS BAR'S, by an explicit grab. Pulled off its side, the panel
            // becomes a window of its own under the button - and the press's own implicit hold
            // stayed with the main window, so the release went to the drawing: the panel was left
            // floating where it was, the side lit up as though it were about to land, and nothing
            // landed. Grabbed, every move and the release come here, whichever window it is in.
            grabMouse();
            dragging_ = true;
            pressGlobal_ = e->globalPosition().toPoint();
            lastGlobal_ = pressGlobal_;
            grabOffset_ = mapTo(dock_, e->pos());   // docked or floating, a place on the panel
        }
        e->accept(); // consumed: the drag is this bar's, start to finish
    }
    void mouseMoveEvent(QMouseEvent* e) override {
        if (dragging_) lastGlobal_ = e->globalPosition().toPoint();
        // The button came up somewhere this bar never heard about: finish the drag as a release
        // would have, rather than leaving the side lit and the panel hanging.
        if (dragging_ && !(e->buttons() & Qt::LeftButton)) {
            endDrag();
            e->accept();
            return;
        }
        if (dragging_ && (e->buttons() & Qt::LeftButton)) {
            const QPoint g = e->globalPosition().toPoint();
            if ((g - pressGlobal_).manhattanLength() < QApplication::startDragDistance() &&
                !moved_) {
                e->accept();
                return;
            }
            moved_ = true;
            if (!dock_->isFloating()) {
                // Taken from a folded side's fly-out, the panel is a strip underneath: opened out
                // first, or it floats as one.
                dock_->unfoldToFloat();
                dock_->setFloating(true); // detach once the drag really starts
                dock_->fitFloating();
                grabMouse();              // and keep the drag through the panel becoming a window
                // Where it was taken hold of stays under the cursor - within the bar's height, so a
                // panel much shorter floating than docked is not left hanging off the pointer.
                grabOffset_.setY(std::clamp(grabOffset_.y(), 4, std::max(4, height() - 4)));
                grabOffset_.setX(std::clamp(grabOffset_.x(), 4, std::max(4, dock_->width() - 4)));
            }
            dock_->move(g - grabOffset_);
            // Highlight the side the panel is now touching — releasing lands it there.
            dock_->showDropHint(g);
            e->accept();
            return;
        }
        // The move cursor anywhere on the bar: all of it is the handle.
        setCursor(Qt::SizeAllCursor);
        e->accept();
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        if (dragging_) lastGlobal_ = e->globalPosition().toPoint();
        endDrag();
        e->accept();
    }
    void endDrag() {
        if (QWidget::mouseGrabber() == this) releaseMouse();
        if (!dragging_) return;
        dragging_ = false;
        dock_->hideDropHint();
        // Attach wherever the pointer is - over a side or a docked panel. A click that never became
        // a drag leaves a floating panel where it is.
        if (moved_ && dock_->isFloating()) dock_->redockHere(lastGlobal_);
        moved_ = false;
    }
    void mouseDoubleClickEvent(QMouseEvent* e) override {
        // Double-click the bar toggles floating (like AutoCAD palettes).
        if (!dock_->isFloating()) dock_->unfoldToFloat();
        dock_->setFloating(!dock_->isFloating());
        dock_->fitFloating();
        e->accept();
    }

public:
    // Every title bar there is. A plain QWidget subclass in a .cpp has no Q_OBJECT and so cannot be found
    // with findChildren, and a list of them is wanted anyway: the scheme changing means telling all of them.
    static const QVector<DockTitleBar*>& all() { return allBars_; }
    static void restyleAll() {
        for (DockTitleBar* b : allBars_)
            if (b) b->restyle();
    }

    // Its ground and its lettering, in the current scheme. Both, because a title bar that changed its
    // background and kept its light lettering would be a white bar with white writing on it.
    void restyle() {
        setStyleSheet(QStringLiteral("background:%1;").arg(Chrome::bar().name()));
        if (grip_) grip_->setPixmap(makeGripPixmap());   // drawn, so it has to be drawn again
        if (lbl_)
            lbl_->setStyleSheet(
                QStringLiteral("font-weight:600;color:%1;background:transparent;").arg(Chrome::barInk().name()));
    }

private:
    AutoHideDock* dock_;
    QLabel* lbl_ = nullptr;
    QLabel* grip_ = nullptr;   // both start empty: restyle() runs before either is built
    bool dragging_ = false;
    bool moved_ = false;       // the press has become a drag
    QPoint pressGlobal_;
    QPoint grabOffset_;        // where on the panel it was taken hold of
    QPoint lastGlobal_;        // where the pointer last was in the drag: where it lands
    static QVector<DockTitleBar*> allBars_;

public:
    ~DockTitleBar() override { allBars_.removeAll(this); }
};

QVector<DockTitleBar*> DockTitleBar::allBars_;

Bounds mechModelBounds(const Document& doc) {
    Bounds out;
    for (const Entity& e : doc.entities()) {
        if (e.layer == QLatin1String(kMechModelGuideLayer)) continue;
        const Bounds b = e.bounds();
        if (b.valid) {
            out.expand(b.min);
            out.expand(b.max);
        }
    }
    return out;
}

void addMechModelGuides(Document& doc, const MechModel& model) {
    doc.entities().erase(
        std::remove_if(doc.entities().begin(), doc.entities().end(), [](const Entity& e) {
            return e.layer == QLatin1String(kMechModelGuideLayer);
        }),
        doc.entities().end());
    const Bounds b = mechModelBounds(doc);
    // HOW BIG A MARKER IS, decided ONCE for the part and then left alone.
    //
    // It used to be a proportion of the drawing's extent as it stood at that moment, and the guides are rebuilt
    // every time anything on the canvas changes - so moving a primitive changed the extent, which changed the
    // marker size, and the marks swelled and shrank as the part was drawn. A mark is chrome: it says where
    // something is, and a mark that changes size when you move a line reads as though the mark itself had been
    // edited.
    //
    // So it is measured from the model AS CAPTURED where there is one - which does not move while the canvas is
    // being worked on - and otherwise from the extent the first time it is asked, and kept.
    // QUANTISED, to a 1-2-5 series. A mark still has to suit the size of the part - three millimetres of ring on a
    // part two metres tall is not a mark, it is a speck - so it cannot simply be a constant. But it must not follow
    // the drawing continuously either, and it was: the guides are rebuilt every time anything on the canvas
    // changes, so moving one line changed the extent, which changed the size, and the marks swelled and shrank as
    // the part was drawn.
    //
    // Snapped to 1, 2, 5, 10, 20, 50 ... a line moved about, or the part growing by a quarter, stays in its bucket
    // and the marks do not move at all. A part drawn ten times bigger goes up two buckets, which is the one time
    // the size ought to change.
    const double markerRadius = [&] {
        Bounds stable;
        for (const Entity& e : partDrawingEntities(model.drawing)) stable.expand(e.bounds());
        const Bounds& from = stable.valid ? stable : b;
        const double want = from.valid ? std::max(2.0, from.height() * 0.08) : 3.0;
        const double decade = std::pow(10.0, std::floor(std::log10(want)));
        for (const double step : {1.0, 2.0, 5.0})
            if (want <= step * decade * 1.0000001) return step * decade;
        return 10.0 * decade;
    }();

    // THE MARKS, from the one place that decides what a marked-up model looks like - so what is seen here
    // while it is being marked up is what the manager's preview shows when it is being chosen. The arrow
    // reaches across a third of the part, which on a part the size of a real one is long enough to read the
    // direction off and short enough not to leave the drawing.
    for (Entity& mark :
         mechModelMarkEntities(model, markerRadius, std::max(6.0 * markerRadius,
                                                            b.valid ? b.width() * 0.35 : 10.0)))
        doc.entities().push_back(std::move(mark));
    // The object the app scales by, boxed in cyan so it can be SEEN which one it is. A mark that cannot be
    // seen is a mark nobody trusts: the whole point of picking the label inside a sleeve is knowing you
    // picked the label and not the sleeve, and the two are drawn one inside the other.
    for (const Entity& e : doc.entities()) {
        if (e.layer == QLatin1String(kMechModelGuideLayer)) continue;
        if (!e.meta.value(QStringLiteral("modelScaleRef")).toBool()) continue;
        const Bounds rb = e.bounds();
        if (!rb.valid) continue;
        const double pad = std::max(0.6, std::max(rb.width(), rb.height()) * 0.05);
        Entity box;
        box.id = QStringLiteral("scaleref");
        box.type = EntityType::Polyline;
        box.layer = QLatin1String(kMechModelGuideLayer);
        box.color = QStringLiteral("#2bd0ff");
        box.points = {{rb.min.x - pad, rb.min.y - pad},
                      {rb.max.x + pad, rb.min.y - pad},
                      {rb.max.x + pad, rb.max.y + pad},
                      {rb.min.x - pad, rb.max.y + pad},
                      {rb.min.x - pad, rb.min.y - pad}};
        box.meta.insert(QStringLiteral("lineType"), QStringLiteral("dashed"));
        doc.entities().push_back(box);
        break;
    }
    if (!mechModelStretches(model.kind)) return;
    const double length = model.drawnLengthMm > 0.0 ? model.drawnLengthMm
                                                    : mechModelDrawnLength(model);
    if (length <= 0.0) return;
    const double half = b.valid ? std::max(4.0, b.height()) : 8.0;
    const double x0 = b.valid ? b.min.x : 0.0;
    for (double at : {model.headMm, length - model.tailMm}) {
        if (at <= 0.0 || at >= length) continue;
        Entity guide;
        guide.id = QStringLiteral("guide%1").arg(at, 0, 'f', 2);
        guide.type = EntityType::Line;
        guide.layer = QLatin1String(kMechModelGuideLayer);
        guide.a = {x0 + at, -half};
        guide.b = {x0 + at, half};
        guide.color = QStringLiteral("#e6b45a");
        guide.meta.insert(QStringLiteral("lineType"), QStringLiteral("dashed"));
        doc.entities().push_back(guide);
    }
}

void loadMechModelDocument(Document& doc, const MechModel& model, bool schematic = false) {
    doc.entities().clear();
    // A PART IS NOT A SHEET. Fit looks at the drawing's stated sheet first, which is right for a drawing - what a
    // sheet is FOR is being the thing you look at - and wrong here: this window holds one part a few millimetres
    // across, and fitting to a sheet round it opened the window on a page of empty grey with the part a speck in
    // the middle of it. Left unstated, Fit fits what is actually drawn, which is the part.
    doc.sheet() = Bounds();
    // The model comes in as ONE object. It used to arrive as its own loose primitives, which meant the
    // thing the session is about could not be picked up and moved as the part it is: every line of it had
    // to be swept up first. Grouped, a click anywhere takes the whole model - and a double-click opens it
    // up again when it is the lines themselves that need changing.
    for (const Entity& e : partDrawingEntities(schematic ? model.schematicDrawing : model.drawing)) {
        doc.entities().push_back(e);
        Entity& put = doc.entities().back();
        // partDrawingEntities clears the id on the way out - a stored drawing has no business naming
        // objects in whatever document it lands in, and the document is meant to give it one. This did
        // not, so every line of a model being edited was called "", and everything that works by id -
        // which is the selection, the joints, Delete and undo - resolved all of them to the first.
        if (put.id.isEmpty()) put.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
        stampEntityGroup(put, QLatin1String(kMechModelGroup));
    }
    if (!doc.findLayer(QLatin1String(kMechModelGuideLayer)))
        doc.layers().push_back({QLatin1String(kMechModelGuideLayer), "#e6b45a", true, true, 0.25});
    doc.setActiveLayer(QStringLiteral("0"));
    // A schematic symbol has no head/tail/stretch to guide - those guides are a mechanical-drawing
    // concept (see addMechModelGuides), and drawing them over a schematic glyph would just be wrong.
    if (!schematic) addMechModelGuides(doc, model);
}

// The same three marks, for a library part's mechanical drawing - see placementMarkEntities().
// Deliberately not the scale-reference box or the stretch guides addMechModelGuides also draws:
// neither concept applies to a library part (it is not scaled by an object marked inside it, and
// it does not stretch to a length the harness decides - it is placed at the size it was drawn).
void addLibraryPartGuides(Document& doc, const LibraryPart& part) {
    doc.entities().erase(
        std::remove_if(doc.entities().begin(), doc.entities().end(), [](const Entity& e) {
            return e.layer == QLatin1String(kMechModelGuideLayer);
        }),
        doc.entities().end());
    const Bounds b = mechModelBounds(doc);
    const double markerRadius = [&] {
        const double want = b.valid ? std::max(2.0, b.height() * 0.08) : 3.0;
        const double decade = std::pow(10.0, std::floor(std::log10(want)));
        for (const double step : {1.0, 2.0, 5.0})
            if (want <= step * decade * 1.0000001) return step * decade;
        return 10.0 * decade;
    }();
    for (Entity& mark :
         placementMarkEntities(part.hasConnect, part.connectPoint, part.connectAngleRad,
                               part.hasNearPoint, part.nearPoint, part.hasFarPoint, part.farPoint,
                               markerRadius,
                               std::max(6.0 * markerRadius, b.valid ? b.width() * 0.35 : 10.0)))
        doc.entities().push_back(std::move(mark));
    // AND THE FACES THE WIRES LEAVE BY, the same bars and numbers a default model shows. A connector out of
    // a catalogue has faces exactly as the house model of one does.
    for (Entity& mark : outputFaceMarkEntities(part.outputs, markerRadius))
        doc.entities().push_back(std::move(mark));
}

void loadLibraryPartDocument(Document& doc, const LibraryPart& part, LibraryPartDrawing kind) {
    doc.entities().clear();
    doc.sheet() = Bounds();
    // A CONNECTOR MADE FROM DATA IS STILL A DRAWING TO EDIT. A part whose schematic has never been
    // captured has one all the same - built from its own data, its way count at its pitch, numbered - and
    // that is what the library shows in its window and what it puts on a sheet. Asked to edit it, the
    // session opened EMPTY, because it looked only at what had been captured: the drawing on the screen and
    // the drawing being edited were two different things, and the way to get the shown one into the editor
    // was to place it on a sheet and capture it back.
    //
    // partSchematicDrawing() is the same answer the window and the sheet get, so all three agree.
    QJsonArray generated;
    if (kind == LibraryPartDrawing::Schematic) {
        generated = part.schematic;
        // A CONNECTOR'S SYMBOL COMES FROM ITS OWN DATA when nothing has been captured: its way count at its
        // pitch, numbered - which is what the library window shows and what goes on a sheet. It is built the
        // same way the library's own "From data" button builds it, so the three agree.
        if (generated.isEmpty() && part.kind == PartKind::Connector)
            generated = connectorSymbolFromData(part);
        // And for the kinds that have a standard symbol of their own - a resistor, a capacitor - that.
        if (generated.isEmpty()) generated = partSchematicDrawing(part);
    }
    const QJsonArray& drawing = kind == LibraryPartDrawing::Mechanical  ? part.mechanical
                                : kind == LibraryPartDrawing::Schematic ? generated
                                : kind == LibraryPartDrawing::Fanout    ? part.fanout
                                                                        : part.fanoutOpposite;
    for (const Entity& e : partDrawingEntities(drawing)) {
        doc.entities().push_back(e);
        Entity& put = doc.entities().back();
        if (put.id.isEmpty()) put.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
        // AS PRIMITIVES, NOT AS ONE OBJECT. On a sheet a part is one thing - a click picks the whole
        // connector, and that is right, because there it is a part being used. Here it is being DRAWN, and
        // every line of it has to be movable on its own: a session where the first click selects all
        // seventy-nine lines of a connector body is a session in which nothing can be corrected. The
        // grouping is put back when the drawing is captured, because that is where it belongs.
        //
        // Nothing is lost: the drawing is stored as its lines either way, and the part's own copies on any
        // sheet are rebuilt from it as one object as before.
        (void)0;
    }
    doc.setActiveLayer(QStringLiteral("0"));
    // Only the mechanical drawing carries connect/near/far points - a schematic symbol and a fanout
    // are not placed by running a direction along a run.
    if (kind == LibraryPartDrawing::Mechanical) {
        if (!doc.findLayer(QLatin1String(kMechModelGuideLayer)))
            doc.layers().push_back({QLatin1String(kMechModelGuideLayer), "#e6b45a", true, true, 0.25});
        addLibraryPartGuides(doc, part);
    }
}

} // namespace

QStringList MainWindow::startupModules() {
    // One entry per note() below, in order. The splash sizes its bar from this list, so a step
    // added there belongs here too.
    return {QStringLiteral("Drawing canvas"),   QStringLiteral("Document tabs"),
            QStringLiteral("Preferences"),      QStringLiteral("Ribbon"),
            QStringLiteral("Panels"),           QStringLiteral("Status controls"),
            QStringLiteral("Command bar"),      QStringLiteral("Menus"),
            QStringLiteral("Commands"),         QStringLiteral("Parts library"),
            QStringLiteral("Command autocomplete")};
}

MainWindow::MainWindow(QWidget* parent, ModuleReporter onModule) : QMainWindow(parent) {
    // Name each step as it is reached. Nothing is timed or faked: the splash moves on when the
    // module before it is actually built.
    auto note = [&onModule](const QString& module) {
        Q_ASSERT(startupModules().contains(module));
        if (onModule) onModule(module);
    };
    // The title carries WHEN this executable was built. Which build is running is otherwise invisible,
    // and an application left open across a rebuild looks exactly like one whose fix did not work - a
    // confusion worth one line of code to end. The file's own timestamp is used rather than __DATE__,
    // because that only changes when this file itself is recompiled.
    const QDateTime built =
        QFileInfo(QCoreApplication::applicationFilePath()).lastModified();
    setWindowTitle(built.isValid()
                       ? QStringLiteral("Edraw — build %1").arg(built.toString(
                             QStringLiteral("yyyy-MM-dd HH:mm")))
                       : QStringLiteral("Edraw"));
    resize(1340, 860);

    note(QStringLiteral("Drawing canvas"));
    canvas_ = new CanvasWidget(this);
    editor_ = new Editor(this);
    canvas_->setEditor(editor_);
    note(QStringLiteral("Document tabs"));
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
    // Something is now on the cursor waiting for its click. It is the sheet that answers Esc, Space
    // and X/Y, so placement takes keyboard focus away from whichever ribbon button or dialog started it.
    connect(editor_, &Editor::pastePlacementStarted, this, [this] {
        if (!canvas_) return;
        // The library window hides itself after handing the part over. Put the focus on the canvas on
        // the next event turn, after that hide has settled, or Esc can still belong to a gone window.
        QTimer::singleShot(0, canvas_, [this] {
            if (!canvas_ || !editor_ || !editor_->placingPaste()) return;
            if (QWidget* window = canvas_->window()) {
                window->raise();
                window->activateWindow();
            }
            canvas_->setFocus(Qt::OtherFocusReason);
        });
    });
    connect(editor_, &Editor::changed, this, [this] {
        canvas_->update();
        refreshPanels();
        updateActiveTabText(); // reflect the dirty marker
        // A connection/near/far marker just moved or was deleted: the Select tool's own Move and Delete
        // now reach these (Editor::hitTest exempts them, on purpose, from the guide layer's lock), so
        // this is where a drag or a Delete on one of them is heard. The marker ON THE SHEET is read back
        // into the model it belongs to - not the other way round - because the entity is what the user
        // just acted on; a marker that is gone is a point that is no longer marked, and one that moved
        // is marked wherever it was dropped.
        if (doc_ && mechModelEdits_.contains(doc_)) {
            MechModel& model = mechModelSessionModels_[doc_];
            bool sawNear = false, sawFar = false, sawConnect = false;
            for (const Entity& e : doc_->entities()) {
                const QString tag = e.meta.value(QStringLiteral("modelAnchor")).toString();
                if (tag == QLatin1String("near")) {
                    sawNear = true;
                    model.hasNearPoint = true;
                    model.nearPoint = e.center;
                } else if (tag == QLatin1String("far")) {
                    sawFar = true;
                    model.hasFarPoint = true;
                    model.farPoint = e.center;
                } else if (tag == QLatin1String("connect")) {
                    sawConnect = true;
                    model.hasConnect = true;
                    model.connectPoint = e.center;   // the angle is set only by re-picking Connection point
                }
            }
            if (!sawNear) model.hasNearPoint = false;
            if (!sawFar) model.hasFarPoint = false;
            if (!sawConnect) model.hasConnect = false;
            // Redrawn rather than left as they are: a marker dropped onto another one has to restagger
            // to the two-ring size right away, and a deleted marker's ring has to actually disappear.
            addMechModelGuides(*doc_, model);
        } else if (doc_ && libraryPartEdits_.contains(doc_) &&
                   libraryPartEditKind_.value(doc_) == LibraryPartDrawing::Mechanical) {
            LibraryPart& part = libraryPartSessionModels_[doc_];
            bool sawNear = false, sawFar = false, sawConnect = false;
            for (const Entity& e : doc_->entities()) {
                const QString tag = e.meta.value(QStringLiteral("modelAnchor")).toString();
                if (tag == QLatin1String("near")) {
                    sawNear = true;
                    part.hasNearPoint = true;
                    part.nearPoint = e.center;
                } else if (tag == QLatin1String("far")) {
                    sawFar = true;
                    part.hasFarPoint = true;
                    part.farPoint = e.center;
                } else if (tag == QLatin1String("connect")) {
                    sawConnect = true;
                    part.hasConnect = true;
                    part.connectPoint = e.center;
                }
            }
            if (!sawNear) part.hasNearPoint = false;
            if (!sawFar) part.hasFarPoint = false;
            if (!sawConnect) part.hasConnect = false;
            addLibraryPartGuides(*doc_, part);
        }
    });
    connect(editor_, &Editor::modelAnchorPicked, this, [this](Vec2 point, bool near) {
        if (!doc_) return;
        if (mechModelEdits_.contains(doc_)) {
            MechModel& model = mechModelSessionModels_[doc_];
            if (near) {
                model.hasNearPoint = true;
                model.nearPoint = point;
            } else {
                model.hasFarPoint = true;
                model.farPoint = point;
            }
            addMechModelGuides(*doc_, model);
        } else if (libraryPartEdits_.contains(doc_) &&
                   libraryPartEditKind_.value(doc_) == LibraryPartDrawing::Mechanical) {
            LibraryPart& part = libraryPartSessionModels_[doc_];
            if (near) {
                part.hasNearPoint = true;
                part.nearPoint = point;
            } else {
                part.hasFarPoint = true;
                part.farPoint = point;
            }
            addLibraryPartGuides(*doc_, part);
        } else {
            return;
        }
        doc_->setDirty(true);
        canvas_->invalidateScene();
        statusBar()->showMessage(near ? QStringLiteral("Near point marked in orange")
                                      : QStringLiteral("Far point marked in green"),
                                 3000);
    });
    // One object marked as the one the app scales by - and only one, so marking another moves the mark
    // rather than adding a second. Two references would be two answers to a question with one answer.
    connect(editor_, &Editor::modelScaleRefPicked, this, [this](const QString& id) {
        if (!doc_ || !mechModelEdits_.contains(doc_)) return;
        QString what;
        for (Entity& e : doc_->entities()) {
            if (e.layer == QLatin1String(kMechModelGuideLayer)) continue;
            if (e.id == id) {
                e.meta.insert(QStringLiteral("modelScaleRef"), true);
                const Bounds b = e.bounds();
                if (b.valid)
                    what = tr("%1 long by %2 across").arg(b.width(), 0, 'f', 1).arg(b.height(), 0, 'f', 1);
            } else {
                e.meta.remove(QStringLiteral("modelScaleRef"));
            }
        }
        doc_->setDirty(true);
        canvas_->invalidateScene();
        canvas_->update();
        statusBar()->showMessage(
            tr("Scaled by this object — %1. Everything else is carried along by the same scale.").arg(what),
            6000);
    });
    connect(editor_, &Editor::modelOutputPicked, this, [this](Vec2 a, Vec2 b) {
        // A LIBRARY PART HAS FACES TOO. Anything wires go into - a connector, a ferrule, a ring - has one or
        // more faces they come out of, and the generator hands the branches out in the order they were
        // marked. A default model has been able to say so all along; a part out of the catalogue, which is
        // the same part with a part number on it, could only be told where it connects.
        const bool libraryMechSession = doc_ && libraryPartEdits_.contains(doc_) &&
                                        libraryPartEditKind_.value(doc_) == LibraryPartDrawing::Mechanical;
        if (libraryMechSession) {
            LibraryPart& part = libraryPartSessionModels_[doc_];
            part.outputs.push_back({a, b});
            addLibraryPartGuides(*doc_, part);
            doc_->setDirty(true);
            canvas_->invalidateScene();
            statusBar()->showMessage(
                tr("Output face %1 marked — the wires spread along it and stay inside it. Click another "
                   "for the next branch, or Esc to finish")
                    .arg(part.outputs.size()),
                6000);
            return;
        }
        if (!doc_ || !mechModelEdits_.contains(doc_)) return;
        MechModel& model = mechModelSessionModels_[doc_];
        model.outputs.push_back({a, b});
        addMechModelGuides(*doc_, model);
        doc_->setDirty(true);
        canvas_->invalidateScene();
        statusBar()->showMessage(
            tr("Output face %1 marked — the wires spread along it and stay inside it. Click another "
               "for the next branch, or Esc to finish")
                .arg(model.outputs.size()),
            6000);
    });
    connect(editor_, &Editor::modelConnectPicked, this,
            [this](Vec2 point, double angleRad) { markMechModelConnectAt(point, angleRad); });
    connect(editor_, &Editor::selectionChanged, this, [this] {
        refreshPanels();
        updateHatchContext(); // selecting a hatch opens the Hatch Creation tab to edit it
        updateConnectorContext(); // selecting a connector opens the Connector build tab
        updateDetailContext();    // and selecting a detail view opens its own
        updateModelRibbonTabs();
    });
    // Keep the "Place connector" button pressed-state in sync when placement is armed
    // by a canvas click (not just the button).
    connect(editor_, &Editor::connectorArmedChanged, this, [this] {
        if (connectorPlaceBtn_) connectorPlaceBtn_->setChecked(editor_->connectorArmed());
    });
    // A hatch was created (or the creation session ended): re-point the tab so the
    // controls keep editing the just-placed hatch until you click out / Close.
    connect(editor_, &Editor::hatchTargetChanged, this, [this] {
        updateHatchContext();
        updateModelRibbonTabs();
    });
    // A label armed, placed, resized or let go of: its own group comes up, the rest of the tab goes away,
    // and the two fields track the size the stroke measured.
    connect(editor_, &Editor::labelChanged, this, [this] { syncLabelOptions(); });
    // Clicking a table cell opens the contextual "Table Cell" tab (row/col + align ops).
    connect(editor_, &Editor::cellSelectionChanged, this, [this] {
        updateTableCellContext();
        updateModelRibbonTabs();
    });
    // An array session opening/closing (or its centre/path landing) drives the
    // contextual "Array Creation" tab and keeps its fields in step with the preview.
    connect(editor_, &Editor::arrayChanged, this, [this] {
        updateArrayContext();
        updateModelRibbonTabs();
    });
    // Match properties latches its ribbon button(s) while it waits for clicks.
    connect(editor_, &Editor::matchPropsChanged, this, [this] {
        const bool armed = editor_->awaitingMatchProps();
        for (QToolButton* b : matchBtns_) {
            QSignalBlocker block(b);
            b->setChecked(armed);
        }
    });
    // Quick Select asks for its filter dialog once it has a sample object.
    connect(editor_, &Editor::quickSelectRequested, this,
            [this](const QString& sampleId) { showQuickSelectDialog(sampleId); });
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
        updateDetailContext();    // and the Detail view tool opens its own, and keeps it while armed
        updateModelRibbonTabs();
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
    // Double-clicking a copy of a block opens the block itself.
    connect(editor_, &Editor::editBlockRequested, this,
            [this](const QString& name) { openBlockEditor(name); });
    // Double-clicking the sheet frame opens the template it came from.
    connect(editor_, &Editor::editTemplateRequested, this,
            [this](const QString& name) { openTemplateEditor(name); });
    // Ctrl+V: the window decides, because the system clipboard may hold a newer picture.
    connect(editor_, &Editor::pasteRequested, this, [this] { pasteFromClipboard(); });
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
        // Where the editor opens: over the words themselves. A leader's words sit at the far end of it -
        // the end away from what it points at - and `a` is not a point on a leader at all, so editing one
        // opened the box at the origin of the drawing.
        Vec2 at = found->a;
        if (found->type == EntityType::Leader && !found->points.empty()) {
            at = found->points.back();
            if (style.size <= 0.0) style.size = 2.5;
        }
        beginInlineText(id, found->meta.value(QStringLiteral("text")).toString(), style, at);
        // A balloon number is a single value: plain Enter (or clicking away) commits it. So is a
        // callout's label - it is a label, not a paragraph.
        inlineSingleLine_ = found->meta.value(QStringLiteral("balloon")).toBool() ||
                            found->type == EntityType::Leader;
    });
    // Double-clicked a table cell: open the inline editor over that cell, writing back
    // to the cell (not a text object) on commit.
    connect(editor_, &Editor::editTableCellRequested, this,
            [this](const QString& id, int cell, const QString& text, Vec2 at, double cw, double ch) {
                Editor::TextStyle style;
                // The height the cell is ALREADY written at. This was a flat 4.0, and committing writes
                // the editor's height back onto the cell - so every edit set the text to 4 units high
                // whatever it had been. On an imported drawing whose table is written at 0.12 that is
                // thirty times over: typing in one cell blew its row apart. The height a cell has is the
                // height it keeps, unless the editor's own toolbar is used to change it.
                style.size = 4.0;
                if (doc_) {
                    for (const Entity& t : doc_->entities()) {
                        if (t.id != id) continue;
                        const QJsonArray own = t.meta.value(QStringLiteral("cellTextSize")).toArray();
                        const double mine = cell < own.size() ? own[cell].toDouble() : 0.0;
                        style.size = mine > 0.0
                                         ? mine
                                         : t.meta.value(QStringLiteral("textSize")).toDouble(4.0);
                        break;
                    }
                }
                if (!(style.size > 0.0)) style.size = 4.0;
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
    note(QStringLiteral("Preferences"));
    loadSettings();

    note(QStringLiteral("Ribbon"));
    buildRibbon();
    note(QStringLiteral("Panels"));
    buildPanels();
    note(QStringLiteral("Status controls"));
    buildStatusControls();
    note(QStringLiteral("Command bar"));
    buildCommandBar();
    note(QStringLiteral("Menus"));
    buildMenus();
    note(QStringLiteral("Commands"));
    buildCommands();
    // Watch the system clipboard, so a paste knows whether the picture on it arrived before or after
    // whatever was last copied inside the application.
    if (QClipboard* cb = QGuiApplication::clipboard())
        connect(cb, &QClipboard::dataChanged, this, [this] {
            const QMimeData* md = QGuiApplication::clipboard()->mimeData();
            if (md && (md->hasImage() || md->hasUrls()))
                sysClipImageAt_ = QDateTime::currentMSecsSinceEpoch();
        });
    note(QStringLiteral("Parts library"));
    {
        // Open the connected libraries once at start-up: the default one is created and filled with
        // real parts on a first run, so the bill of materials and the harness builder find a shelf
        // that is already stocked - without waiting for the library window to be opened.
        PartsCatalog catalog;
        catalog.openRemembered();
    }

    note(QStringLiteral("Command autocomplete"));

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

    // The panels as they were left last time: which side each is on, which are open, whether a side is
    // folded away, and how wide it is. Last of the building, so everything it arranges exists.
    restorePanelLayout();

    newDrawing(); // start with a single empty drawing (no demo, no last session)

    // THE GROUND CHOSEN LAST TIME, read before the theme is applied - applyTheme builds the stylesheets
    // that ask for it. A setting that is absent leaves the built-in colour in force, which is what a
    // fresh install and a reset both look like.
    {
        const QSettings st;
        const QString d = st.value(QStringLiteral("ui/chromeBackDark")).toString();
        const QString l = st.value(QStringLiteral("ui/chromeBackLight")).toString();
        if (!d.isEmpty() && QColor::isValidColorName(d)) Chrome::setChosenBack(true, QColor(d));
        if (!l.isEmpty() && QColor::isValidColorName(l)) Chrome::setChosenBack(false, QColor(l));
        // And how much light was on the canvas. Set before the theme, which is what puts the ground
        // colour on: the brightness rides on whatever colour that turns out to be.
        canvas_->setCanvasBrightness(st.value(QStringLiteral("ui/canvasBrightness"), 0).toInt());
    }

    // The scheme last chosen. AFTER the first drawing exists: colouring the chrome ends by filling the
    // panels again - a properties row colours itself as it is made - and there is nothing to fill them from
    // until there is a drawing.
    applyTheme(darkTheme_);

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

    auto* spaceRow = new QWidget(canvas_);
    spaceRow->setFixedHeight(32);
    auto* spaceLayout = new QHBoxLayout(spaceRow);
    spaceLayout->setContentsMargins(0, 0, 0, 0);
    spaceLayout->setSpacing(0);
    auto* spaceMenu = new QToolButton(spaceRow);
    spaceMenu->setText(QStringLiteral("≡"));
    spaceMenu->setToolTip(QStringLiteral("Drawing spaces"));
    spaceMenu->setAutoRaise(true);
    spaceMenu->setFixedWidth(38);
    spaceMenu->setFixedHeight(32);
    drawingSpaceTabs_ = new QTabBar(spaceRow);
    drawingSpaceTabs_->setDocumentMode(true);
    drawingSpaceTabs_->setExpanding(false);
    drawingSpaceTabs_->setFixedHeight(32);
    // No scroll arrows. The strip is as wide as the canvas and the tabs are 384 px of it, so there is
    // nothing to scroll - but Qt draws the arrows anyway, at the right-hand end, ON TOP of the last
    // tab's close button. Which is why the second layout could not be closed: the x was there, and the
    // arrow was over it. With them off the strip simply shows the tabs it has.
    drawingSpaceTabs_->setUsesScrollButtons(false);
    drawingSpaceTabs_->addTab(QStringLiteral("Model"));
    drawingSpaceTabs_->setCurrentIndex(0);
    // Coloured by restyleDrawingSpaceStrip(), which knows which scheme is set.
    auto* addSpace = new QToolButton(spaceRow);
    addSpace->setText(QStringLiteral("+"));
    addSpace->setToolTip(QStringLiteral("Add layout"));
    addSpace->setAutoRaise(true);
    addSpace->setFixedWidth(38);
    addSpace->setFixedHeight(32);
    spaceStripBtns_.append(addSpace);
    spaceStripBtns_.append(spaceMenu);
    auto* drawingSpaceMenu = new QMenu(spaceMenu);
    drawingSpaceMenu->addAction(QStringLiteral("Add viewport"), this,
                                [this] { canvas_->addLayoutViewport(); });
    drawingSpaceMenu->addAction(QStringLiteral("Apply layout template..."), this,
                                [this] { applyLayoutTemplate(); });
    spaceMenu->setMenu(drawingSpaceMenu);
    spaceMenu->setPopupMode(QToolButton::InstantPopup);
    // The scale the sheet is plotted at, in the right-hand corner where a drawing office looks for it.
    // It is the LAYOUT'S scale - the viewport's - so it is kept with the drawing and is what the sheet
    // shows and prints at. 1:2 means one millimetre of paper is worth two of the harness.
    spaceScaleBox_ = new QComboBox(spaceRow);
    spaceScaleBox_->setToolTip(QStringLiteral(
        "The scale this layout is plotted at: what one millimetre of paper is worth in the model. "
        "Fit sizes the drawing to the viewport instead."));
    spaceScaleBox_->setFixedHeight(24);
    spaceScaleBox_->setMinimumWidth(96);
    struct Ratio { const char* label; double paperPerModel; };
    // Reducing scales first, as a drawing office lists them, then the enlarging ones. The figure is
    // paper per model: 1:2 is a half, 2:1 is two.
    const Ratio ratios[] = {
        {"Fit", 0.0},      {"1:1", 1.0},        {"1:2", 1.0 / 2.0},   {"1:4", 1.0 / 4.0},
        {"1:5", 1.0 / 5.0}, {"1:8", 1.0 / 8.0}, {"1:10", 1.0 / 10.0}, {"1:16", 1.0 / 16.0},
        {"1:20", 1.0 / 20.0}, {"1:30", 1.0 / 30.0}, {"1:40", 1.0 / 40.0}, {"1:50", 1.0 / 50.0},
        {"1:100", 1.0 / 100.0}, {"2:1", 2.0}, {"4:1", 4.0}, {"8:1", 8.0}, {"10:1", 10.0},
        {"100:1", 100.0}};
    for (const Ratio& r : ratios)
        spaceScaleBox_->addItem(QString::fromLatin1(r.label), r.paperPerModel);
    spaceScaleBox_->setCurrentIndex(1);   // 1:1

    connect(spaceScaleBox_, &QComboBox::currentIndexChanged, this, [this](int i) {
        if (!spaceScaleBox_ || i < 0) return;
        setDrawingSpaceScale(spaceScaleBox_->itemData(i).toDouble());
    });

    // A window onto the model, put on the paper. A layout is a sheet with windows cut in it: this is what
    // cuts one. Only on a layout - there is nothing to cut a window in on the model itself.
    addViewportBtn_ = new QToolButton(spaceRow);
    addViewportBtn_->setText(QStringLiteral("+ window"));
    addViewportBtn_->setToolTip(QStringLiteral(
        "Put a window onto the drawing on this sheet. Drag its corners to size it, drag inside it to "
        "move what it looks at, and set what it is plotted at with the scale beside this."));
    addViewportBtn_->setAutoRaise(true);
    addViewportBtn_->setFixedHeight(24);
    addViewportBtn_->setStyleSheet(QStringLiteral(
        "QToolButton{color:#d8dee8;background:#252c36;border:1px solid #3d4653;border-radius:3px;"
        "padding:2px 10px;}"
        "QToolButton:hover{background:#364252;}"
        "QToolButton:disabled{color:#6f7681;border-color:#2e353f;}"));
    connect(addViewportBtn_, &QToolButton::clicked, this, [this] { addLayoutWindow(); });

    // The scale wants a word beside it. On its own, 98 px of dark grey at the very edge of the canvas
    // reads as part of the frame rather than as something to press - which is how it came to be reported
    // as not opening at all.
    spaceScaleLabel_ = new QLabel(QStringLiteral("Scale"), spaceRow);
    spaceScaleLabel_->setStyleSheet(QStringLiteral("color:#8fa2b8;padding:0 6px;"));

    spaceLayout->addWidget(spaceMenu);
    spaceLayout->addWidget(drawingSpaceTabs_);
    spaceLayout->addWidget(addSpace);
    spaceLayout->addStretch(1);
    spaceLayout->addWidget(addViewportBtn_);
    spaceLayout->addSpacing(14);
    spaceLayout->addWidget(spaceScaleLabel_);
    spaceLayout->addWidget(spaceScaleBox_);
    spaceLayout->addSpacing(10);

    v->addWidget(topRow);
    v->addWidget(canvas_, 1);
    setCentralWidget(container);
    canvas_->setBottomOverlay(spaceRow);

    connect(docTabs_, &QTabBar::currentChanged, this, [this](int i) { activateIndex(i); });
    connect(docTabs_, &QTabBar::tabCloseRequested, this, [this](int i) { closeDocument(i); });
    connect(plus, &QToolButton::clicked, this, [this] { newDrawing(); });
    connect(drawingSpaceTabs_, &QTabBar::currentChanged, this,
            [this](int i) { activateDrawingSpace(i); });
    connect(addSpace, &QToolButton::clicked, this, [this] { addDrawingLayout(); });
    connect(drawingSpaceTabs_, &QTabBar::tabCloseRequested, this,
            [this](int i) { closeDrawingSpace(i); });
    // A right-click on a tab: what is done TO a sheet - copied, renamed, plotted, set up - as against what
    // is drawn on it. On the tab, because that is the thing being talked about.
    //
    // Taken through an event filter rather than the context-menu signal, because a QTabBar IGNORES a
    // right-button press: the press then travels up the parents - the strip, and the canvas the strip is
    // laid over - and the canvas answers it with the drawing's own menu. Which is what was happening: a
    // right-click on a layout tab offered Copy, Cut, Quick Select. The press is caught here and stops
    // here, on both the tabs and the strip they sit on.
    drawingSpaceTabs_->setContextMenuPolicy(Qt::PreventContextMenu);
    drawingSpaceTabs_->installEventFilter(this);
    drawingSpaceStrip_ = spaceRow;
    restyleDrawingSpaceStrip();   // the strip, its tabs, its + and its scale box, in the scheme now set
    spaceRow->setContextMenuPolicy(Qt::PreventContextMenu);
    spaceRow->installEventFilter(this);
    if (canvas_)
        connect(canvas_, &CanvasWidget::viewportEntered, this,
                [this](bool inside) { enteredLayoutViewport(inside); });
}

void MainWindow::syncDrawingSpaceTabs() {
    if (!drawingSpaceTabs_) return;
    const int current = drawingSpaceTabs_->currentIndex();
    const QSignalBlocker blocker(drawingSpaceTabs_);
    while (drawingSpaceTabs_->count() > 0) drawingSpaceTabs_->removeTab(0);
    drawingSpaceTabs_->addTab(QStringLiteral("Model"));
    if (doc_)
        for (const DrawingLayout& layout : doc_->layouts()) drawingSpaceTabs_->addTab(layout.name);
    // An x on the layouts, so a sheet can be thrown away where it was made. Not on Model: there is one
    // model space and the drawing is in it, so its button is taken away rather than offered and refused.
    // Settled here because the tabs are rebuilt from the document every time it changes.
    drawingSpaceTabs_->setTabsClosable(true);
    drawingSpaceTabs_->setTabButton(0, QTabBar::RightSide, nullptr);
    drawingSpaceTabs_->setTabButton(0, QTabBar::LeftSide, nullptr);
    drawingSpaceTabs_->setCurrentIndex(std::clamp(current, 0, drawingSpaceTabs_->count() - 1));
}

void MainWindow::addDrawingLayout() {
    if (!doc_) return;
    DrawingLayout layout;
    layout.name = QStringLiteral("Layout%1").arg(doc_->layouts().size() + 1);
    LayoutViewport viewport;
    viewport.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    layout.viewports.push_back(viewport);
    doc_->layouts().push_back(std::move(layout));
    doc_->setDirty(true);
    syncDrawingSpaceTabs();
    drawingSpaceTabs_->setCurrentIndex(static_cast<int>(doc_->layouts().size()));
}

void MainWindow::closeDrawingSpace(int index) {
    if (!doc_ || !drawingSpaceTabs_) return;
    if (index <= 0) return;                       // Model: not a layout, nothing to close
    const int at = index - 1;                     // the layouts start after it
    auto& layouts = doc_->layouts();
    if (at < 0 || at >= static_cast<int>(layouts.size())) return;
    if (layouts.size() <= 1) {
        statusBar()->showMessage(
            tr("That is the last layout — a drawing keeps one sheet to plot."), 4000);
        return;
    }
    const QString name = layouts[static_cast<size_t>(at)].name;
    // What was on that sheet goes with it: its paper objects, its viewports, its scale. Asked first,
    // because a sheet that has been laid out is work, and an x is easy to hit.
    if (!layouts[static_cast<size_t>(at)].paperEntities.empty() ||
        !layouts[static_cast<size_t>(at)].templateName.isEmpty()) {
        const auto answer = QMessageBox::question(
            this, tr("Close layout"),
            tr("%1 has a sheet laid out on it. Close it and lose that?").arg(name),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) return;
    }
    layouts.erase(layouts.begin() + at);
    doc_->setDirty(true);
    // Whichever space is showing after it goes: the one before, so the eye stays where it was.
    const int show = std::clamp(index - 1, 0, static_cast<int>(layouts.size()));
    syncDrawingSpaceTabs();
    drawingSpaceTabs_->setCurrentIndex(show);
    activateDrawingSpace(show);
    statusBar()->showMessage(tr("%1 closed").arg(name), 3000);
}

void MainWindow::setDrawingSpaceScale(double paperPerModel) {
    if (!doc_ || !canvas_) return;
    // MODEL SPACE: the box is the scale the drawing will be plotted at, and what it sets is how big the
    // annotation is DRAWN. A drawing to be plotted at 1:10 needs its dimension text ten times the height in
    // the model for it to come off the paper the size it says - so the factor is the other way up from the
    // ratio, and Fit (which means nothing here) is one. The harness is not touched: only what is written
    // about it changes size.
    if (!canvas_->layoutMode()) {
        const int space = drawingSpaceTabs_ ? drawingSpaceTabs_->currentIndex() : 0;
        spaceScale_[space] = paperPerModel > 1e-9 ? paperPerModel : 1.0;
        const double annot = paperPerModel > 1e-9 ? 1.0 / paperPerModel : 1.0;
        EntityRenderer::setAnnotationScale(annot);
        canvas_->invalidateScene();
        statusBar()->showMessage(
            annot > 1.0001 || annot < 0.9999
                ? tr("Dimensions drawn %1× size, for a drawing plotted at %2")
                      .arg(annot, 0, 'g', 3)
                      .arg(spaceScaleBox_ ? spaceScaleBox_->currentText() : QString())
                : tr("Dimensions drawn at size, for a drawing plotted at 1:1"),
            4000);
        return;
    }
    const int at = canvas_->layoutIndex();
    auto& layouts = doc_->layouts();
    if (at < 0 || at >= static_cast<int>(layouts.size())) return;
    DrawingLayout& layout = layouts[static_cast<size_t>(at)];
    if (layout.viewports.empty()) {
        LayoutViewport viewport;
        viewport.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
        layout.viewports.push_back(viewport);
    }
    // Every viewport on the sheet: what the corner says is what the sheet is plotted at.
    for (LayoutViewport& viewport : layout.viewports) viewport.modelScale = std::max(0.0, paperPerModel);
    doc_->setDirty(true);
    canvas_->invalidateScene();
    statusBar()->showMessage(
        paperPerModel > 0.0
            ? tr("%1 plots at %2").arg(layout.name,
                                       spaceScaleBox_ ? spaceScaleBox_->currentText() : QString())
            : tr("%1 fits the drawing to its viewport").arg(layout.name),
        3000);
}

void MainWindow::enteredLayoutViewport(bool inside) {
    if (!editor_ || !canvas_) return;
    if (inside) {
        // Inside the window the harness is what is being drawn on, at its own size: the window only
        // decides how much of it is seen and how big it is shown. So the editor is pointed at the
        // drawing, and the canvas hands it that window's coordinates.
        editor_->setDocument(doc_);
        statusBar()->showMessage(
            tr("Inside the window — drawing, selecting and moving here is the model itself. Middle-drag "
               "or the wheel moves what the window looks at. Esc, or a click on the paper, steps out."),
            8000);
    } else {
        Document* paper = paperSpaceFor(doc_, canvas_->layoutIndex());
        editor_->setDocument(paper ? paper : doc_);
        statusBar()->showMessage(tr("Back on the paper."), 3000);
    }
    refreshProperties();
}

void MainWindow::showDrawingSpaceMenu(const QPoint& at) {
    if (!drawingSpaceTabs_ || !doc_) return;
    const int space = drawingSpaceTabs_->tabAt(at);
    if (space < 0) return;
    // Popped rather than exec'd, so this returns at once: the menu owns itself and goes when it closes.
    auto* menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    const QString name = spaceName(space);
    menu->addAction(name)->setEnabled(false);
    menu->addSeparator();
    if (space == 0) {
        // Model space: there is one of it, it has no name to change and no paper to set up. What is left
        // is putting it out, which is the same Output the sheets use, opened on the model.
        menu->addAction(tr("Plot..."), this, [this] { printLayout(0); });
        menu->addAction(tr("Publish... (every space, in plot order)"), this, [this] { showOutput(); });
        menu->popup(drawingSpaceTabs_->mapToGlobal(at));
        return;
    }
    menu->addAction(tr("Duplicate"), this, [this, space] { duplicateLayout(space); });
    menu->addAction(tr("Rename..."), this, [this, space] { renameLayout(space); });
    menu->addSeparator();
    menu->addAction(tr("Plot..."), this, [this, space] { printLayout(space); });
    menu->addAction(tr("Page setup..."), this, [this, space] { showPageSetup(space); });
    menu->addSeparator();
    QAction* close = menu->addAction(tr("Delete"), this, [this, space] { closeDrawingSpace(space); });
    close->setEnabled(doc_->layouts().size() > 1);
    menu->popup(drawingSpaceTabs_->mapToGlobal(at));
}

void MainWindow::duplicateLayout(int space) {
    if (!doc_ || space <= 0) return;
    flushPaperSpace();   // the sheet being copied is copied as it stands, not as it was last saved
    auto& layouts = doc_->layouts();
    const int at = space - 1;
    if (at < 0 || at >= static_cast<int>(layouts.size())) return;
    DrawingLayout copy = layouts[static_cast<size_t>(at)];   // paper, geometry, windows, page setup
    // A name that is not taken: "Layout2 (2)", then "(3)", the way a copy is named everywhere else.
    const QString base = copy.name;
    int suffix = 2;
    auto taken = [&](const QString& candidate) {
        for (const DrawingLayout& l : layouts)
            if (l.name.compare(candidate, Qt::CaseInsensitive) == 0) return true;
        return false;
    };
    QString name = QStringLiteral("%1 (%2)").arg(base).arg(suffix);
    while (taken(name)) name = QStringLiteral("%1 (%2)").arg(base).arg(++suffix);
    copy.name = name;
    // The windows get fresh ids: two windows with the same id are one window as far as anything that
    // looks them up is concerned.
    for (LayoutViewport& viewport : copy.viewports)
        viewport.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    layouts.insert(layouts.begin() + at + 1, std::move(copy));
    doc_->setDirty(true);
    syncDrawingSpaceTabs();
    drawingSpaceTabs_->setCurrentIndex(space + 1);
    statusBar()->showMessage(tr("%1 copied to %2").arg(base, name), 4000);
}

void MainWindow::renameLayout(int space) {
    if (!doc_ || space <= 0) return;
    auto& layouts = doc_->layouts();
    const int at = space - 1;
    if (at < 0 || at >= static_cast<int>(layouts.size())) return;
    bool ok = false;
    const QString was = layouts[static_cast<size_t>(at)].name;
    const QString name = QInputDialog::getText(this, tr("Rename layout"), tr("Name:"),
                                               QLineEdit::Normal, was, &ok);
    if (!ok || name.trimmed().isEmpty() || name.trimmed() == was) return;
    for (int i = 0; i < static_cast<int>(layouts.size()); ++i)
        if (i != at && layouts[static_cast<size_t>(i)].name.compare(name.trimmed(),
                                                                    Qt::CaseInsensitive) == 0) {
            QMessageBox::information(this, tr("Rename layout"),
                                     tr("There is already a layout called %1.").arg(name.trimmed()));
            return;
        }
    layouts[static_cast<size_t>(at)].name = name.trimmed();
    doc_->setDirty(true);
    syncDrawingSpaceTabs();
    statusBar()->showMessage(tr("%1 is now %2").arg(was, name.trimmed()), 4000);
}

void MainWindow::printLayout(int space) {
    if (!doc_) return;
    flushPaperSpace();
    double w = 0.0, h = 0.0;
    if (!spacePageSizeMm(space, w, h)) {
        QMessageBox::information(this, tr("Print"),
                                 tr("There is nothing on %1 — nothing to print.").arg(spaceName(space)));
        return;
    }
    // One sheet, by its own page setup: the paper it says, the scale it says, the ink it says. A printer
    // if it names one, a PDF if it does not - which is the choice a page setup exists to record.
    const PageSetup setup = pageSetupFor(space);
    if (!setup.device.isEmpty()) {
        printSpaces({space});
        return;
    }
    QString path = QFileDialog::getSaveFileName(
        this, tr("Print %1 to PDF").arg(spaceName(space)),
        QStringLiteral("%1.pdf").arg(spaceName(space)), QStringLiteral("PDF (*.pdf)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) path += QStringLiteral(".pdf");
    if (writeSpacePdf(path, {space}, setup.monochrome, setup.fitToPaper))
        statusBar()->showMessage(tr("%1 written to %2").arg(spaceName(space),
                                                            QFileInfo(path).fileName()), 6000);
}

void MainWindow::addLibraryToCanvasMenu(QMenu& menu) {
    if (!editor_) return;
    PartsCatalog catalog;
    catalog.openRemembered();
    const PartsLibrary& lib = catalog.active();
    auto* root = menu.addMenu(tr("Library"));
    root->setToolTip(tr("Put a part from the library on the sheet"));
    // Only the parts that have a drawing to place. A part with neither is a line on a bill of materials and
    // nothing to put down, and offering it here would be offering nothing.
    int offered = 0;
    for (PartKind kind : allPartKinds()) {
        std::vector<const LibraryPart*> ofKind;
        for (const LibraryPart& part : lib.parts) {
            if (part.kind != kind) continue;
            if (!part.hasMechanical() && partSchematicDrawing(part).isEmpty()) continue;
            ofKind.push_back(&part);
        }
        if (ofKind.empty()) continue;
        auto* kindMenu = root->addMenu(partKindName(kind));
        for (const LibraryPart* part : ofKind) {
            auto* partMenu = kindMenu->addMenu(part->title());
            const QString id = part->id;
            if (part->hasMechanical())
                partMenu->addAction(tr("Mechanical drawing"), this,
                                    [this, id] { placeLibraryPart(id, true); });
            if (!partSchematicDrawing(*part).isEmpty())
                partMenu->addAction(tr("Schematic symbol"), this,
                                    [this, id] { placeLibraryPart(id, false); });
            ++offered;
        }
    }
    if (offered == 0) {
        root->addAction(tr("(nothing in the library has a drawing yet)"))->setEnabled(false);
    } else {
        root->addSeparator();
        root->addAction(tr("Find a part... (search the library)"), this,
                        [this] { findAndPlaceLibraryPart(); });
    }
    root->addSeparator();
    root->addAction(tr("Open the parts library..."), this, [this] { showPartsLibrary(); });
    menu.addSeparator();
}

// What the parts panel's switch says. Read from the box when there is one and from the setting when there
// is not - a part can be placed from the canvas menu before the panel has ever been opened.
bool MainWindow::placesFanoutWithSchematic() const {
    if (fanoutWithSchematicBox_) return fanoutWithSchematicBox_->isChecked();
    return QSettings().value(QStringLiteral("ui/placeFanoutWithSchematic"), false).toBool();
}

void MainWindow::placeLibraryPart(const QString& partId, bool mechanical) {
    if (!editor_) return;
    PartsCatalog catalog;
    catalog.openRemembered();
    const LibraryPart* part = catalog.byId(partId);
    if (!part) return;
    std::vector<Entity> block = partBlockInstance(*part, mechanical, Vec2{0.0, 0.0});
    if (block.empty()) {
        statusBar()->showMessage(tr("%1 has no %2 drawing to place")
                                     .arg(part->title(), mechanical ? tr("mechanical")
                                                                    : tr("schematic")),
                                 4000);
        return;
    }
    // THE FANOUT WITH IT, when that is the habit and the part has one. Beside the symbol - in front of it,
    // where the wires come in, next to its designator - and handed down as ONE placement, so the two land
    // together and stay together instead of being lined up by hand every time.
    if (!mechanical && placesFanoutWithSchematic() && !part->fanout.isEmpty()) {
        std::vector<Entity> fan = partDrawingBlockInstance(*part, part->fanout,
                                                           QStringLiteral("fanout"), Vec2{0.0, 0.0});
        if (!fan.empty()) {
            Bounds sym, fanB;
            for (const Entity& e : block) sym.expand(e.bounds());
            for (const Entity& e : fan) fanB.expand(e.bounds());
            if (sym.valid && fanB.valid) {
                // Its right-hand edge a small gap in front of the symbol's left-hand edge, and the two
                // centred on one another across the run. The gap is taken from the symbol, so it looks the
                // same on a symbol drawn at any size.
                const double gap = std::max(2.0, sym.width() * 0.12);
                const Vec2 shift{sym.min.x - gap - fanB.max.x,
                                 (sym.min.y + sym.max.y) / 2.0 - (fanB.min.y + fanB.max.y) / 2.0};
                for (Entity& e : fan) translateEntity(e, shift);
                for (Entity& e : fan) block.push_back(std::move(e));
            }
        }
    }
    // The contact the connector is used with goes down with it, as it does from the library window: it is a
    // property of the part, so it does not matter which way the part was reached.
    const QString contactId = part->contactId();
    if (!contactId.isEmpty())
        for (Entity& e : block) e.meta.insert(QLatin1String(kContactPartKey), contactId);
    editor_->placeEntities(std::move(block));
    statusBar()->showMessage(tr("Click the sheet to place %1").arg(part->title()), 6000);
    if (canvas_) canvas_->setFocus();
}

void MainWindow::placeLibraryFanout(const QString& partId, bool mirrored) {
    if (!editor_) return;
    PartsCatalog catalog;
    catalog.openRemembered();
    const LibraryPart* part = catalog.byId(partId);
    if (!part) return;
    // Drawn for the part, or worked out from what the part says: one wire per way, out of the back of the
    // housing and into the bundle. Being sent to the library to draw one is not placing a fanout, and a
    // connector's fanout is exactly the thing its own data can say.
    // WHICH SIDE, and the drawing that side has of its own. `fanout` is the front of the connector and
    // `fanoutOpposite` the back - two drawings, each captured on its own in the library, because the back is
    // not the front held up to a mirror. The part says which side it is normally placed from (fanoutSide),
    // and `mirrored` here means "the other one" rather than a transform: asking for the other side of a part
    // whose back is drawn gets that drawing, not a guess at it.
    const bool wantBack = (part->fanoutSide() == QStringLiteral("back")) != mirrored;
    QJsonArray drawing = wantBack ? part->fanoutOpposite : part->fanout;
    bool useMirrorTransform = false;
    if (drawing.isEmpty()) {
        // Nothing drawn for that side: the other side's drawing, mirrored, which is what this always did
        // for a part with one fanout and no second one. A guess, and said to be one below.
        const QJsonArray other = wantBack ? part->fanout : part->fanoutOpposite;
        if (!other.isEmpty()) {
            drawing = other;
            useMirrorTransform = true;
        }
    }
    bool generated = false;
    if (drawing.isEmpty()) {
        drawing = fanoutFromData(*part);
        generated = true;
        useMirrorTransform = false;
    }
    if (drawing.isEmpty()) {
        statusBar()->showMessage(
            tr("%1 has no fanout, and none can be worked out from it - a fanout needs a way count")
                .arg(part->title()),
            5000);
        return;
    }
    std::vector<Entity> block = partDrawingBlockInstance(*part, drawing, QStringLiteral("fanout"),
                                                         Vec2{0.0, 0.0}, useMirrorTransform);
    const QString contactId = part->contactId();
    if (!contactId.isEmpty())
        for (Entity& e : block) e.meta.insert(QLatin1String(kContactPartKey), contactId);
    editor_->placeEntities(std::move(block));
    // Which side is on the sheet is the whole point of having two, so it is said - and said to be a mirror
    // of the other one where that is all it is.
    const QString shows = wantBack ? tr("back") : tr("front");
    statusBar()->showMessage(
        generated
            ? tr("Click the sheet to place a fanout worked out from %1 - %2 way(s) out of the back of the "
                 "housing. Capture your own in the library to replace it")
                  .arg(part->title())
                  .arg(part->wayCount())
            : tr("Click the sheet to place the fanout of %1 — the %2 of the connector")
                  .arg(part->title(), shows),
        6000);
    if (canvas_) canvas_->setFocus();
}

void MainWindow::openBomWindowForTest() { showBomDialog(); }

void MainWindow::addPartToBom(const QString& partId) {
    Document* doc = editDoc();
    if (!doc) return;
    PartsCatalog catalog;
    catalog.openRemembered();
    const LibraryPart* part = catalog.byId(partId);
    if (!part) return;
    BomTable table;
    table.fromJson(doc->bom());
    BomRow row = bomRowFromPart(*part, 1.0);
    row.rowId = bomNextRowId(table.rows);
    row.fromDrawing = false;   // put there by hand, and so removable by hand
    table.rows.push_back(row);
    doc->bom() = table.toJson();
    doc->setDirty(true);
    refreshBomPanel();
    // AND THE WINDOW, IF IT IS OPEN. It shows the same bill: a line added from the panel that did not appear
    // there left two views of one list disagreeing, and no way to tell which was right.
    if (bomDlg_) bomDlg_->reload();
    statusBar()->showMessage(tr("%1 added to the bill of materials — set the quantity in its window")
                                 .arg(part->title()),
                             6000);
}

void MainWindow::findAndPlaceLibraryPart() {
    PartsCatalog catalog;
    catalog.openRemembered();
    const PartsLibrary lib = catalog.merged();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Find a part to place"));
    dlg.resize(560, 460);
    auto* v = new QVBoxLayout(&dlg);
    auto* search = new QLineEdit(&dlg);
    search->setPlaceholderText(tr("part number, description, manufacturer…"));
    v->addWidget(search);
    auto* kindBox = new QComboBox(&dlg);
    kindBox->addItem(tr("Every kind"), -1);
    for (PartKind k : allPartKinds()) kindBox->addItem(partKindName(k), static_cast<int>(k));
    v->addWidget(kindBox);
    auto* list = new QListWidget(&dlg);
    v->addWidget(list, 1);
    auto* buttons = new QDialogButtonBox(&dlg);
    auto* mechBtn = buttons->addButton(tr("Place mechanical"), QDialogButtonBox::AcceptRole);
    auto* schemBtn = buttons->addButton(tr("Place schematic"), QDialogButtonBox::AcceptRole);
    buttons->addButton(tr("Cancel"), QDialogButtonBox::RejectRole);
    v->addWidget(buttons);

    // The list, filtered by what has been typed and by the kind chosen: a library of a thousand parts is
    // searched, not scrolled.
    auto refill = [&] {
        list->clear();
        const QString needle = search->text().trimmed();
        const int wantKind = kindBox->currentData().toInt();
        for (const LibraryPart& part : lib.parts) {
            if (wantKind >= 0 && static_cast<int>(part.kind) != wantKind) continue;
            const bool drawn = part.hasMechanical() || !partSchematicDrawing(part).isEmpty();
            if (!drawn) continue;
            if (!needle.isEmpty()) {
                const QString hay = QStringLiteral("%1 %2 %3 %4")
                                        .arg(part.mfrPn, part.internalPn, part.description, part.mfrName);
                if (!hay.contains(needle, Qt::CaseInsensitive)) continue;
            }
            auto* item = new QListWidgetItem(
                QStringLiteral("%1 — %2").arg(partKindName(part.kind), part.title()), list);
            item->setData(Qt::UserRole, part.id);
            item->setData(Qt::UserRole + 1, part.hasMechanical());
        }
        if (list->count() > 0) list->setCurrentRow(0);
    };
    QObject::connect(search, &QLineEdit::textChanged, &dlg, refill);
    QObject::connect(kindBox, &QComboBox::currentIndexChanged, &dlg, refill);
    QObject::connect(list, &QListWidget::currentItemChanged, &dlg, [&](QListWidgetItem* item) {
        // A part with no mechanical drawing cannot be placed as one: the button says so rather than
        // failing when it is pressed.
        mechBtn->setEnabled(item && item->data(Qt::UserRole + 1).toBool());
    });
    bool mechanical = true;
    QObject::connect(mechBtn, &QPushButton::clicked, &dlg, [&] { mechanical = true; });
    QObject::connect(schemBtn, &QPushButton::clicked, &dlg, [&] { mechanical = false; });
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dlg, &QDialog::accept);
    refill();
    if (dlg.exec() != QDialog::Accepted || !list->currentItem()) return;
    placeLibraryPart(list->currentItem()->data(Qt::UserRole).toString(), mechanical);
}

void MainWindow::showPageSetup(int space) {
    openPageSetupManager(space, /*resume=*/false, -1, PageSetup());
}

void MainWindow::openPageSetupManager(int space, bool resume, int resumeTarget,
                                      const PageSetup& resumeWorking) {
    if (!doc_ || space <= 0) return;
    PageSetupManagerDialog dlg(*doc_, space - 1, this);
    if (resume) dlg.resumeEditing(resumeTarget, resumeWorking);
    const int result = dlg.exec();
    if (dlg.changed()) {
        doc_->setDirty(true);
        if (canvas_) canvas_->invalidateScene();
        statusBar()->showMessage(tr("Page setup changed for %1").arg(spaceName(space)), 4000);
    }
    if (result != PageSetupManagerDialog::PickWindowResult) return;

    // The plot window is picked ON the sheet, so the sheet is what is shown, and the picking goes through
    // the editor: every running snap applies, the snap-override menu works, ortho and polar apply, and a
    // coordinate can be typed - because a plot window is lined up with the drawing and lining up means
    // snapping to it. Both dialogs had to close first: a modal dialog blocks the canvas however well
    // hidden it is. They come back when the second corner is picked, or at once if Esc is pressed.
    const int target = dlg.pickTarget();
    const PageSetup working = dlg.pickWorking();
    if (drawingSpaceTabs_ && drawingSpaceTabs_->currentIndex() != space)
        drawingSpaceTabs_->setCurrentIndex(space);
    if (!editor_) return;
    statusBar()->showMessage(
        tr("Pick the two corners of the plot window on the sheet — the snaps are running; Esc to give "
           "up"));
    if (canvas_) canvas_->setFocus();
    editor_->beginRectanglePick(
        [this, space, target, working](Vec2 a, Vec2 b) {
            PageSetup picked = working;
            picked.windowX1Mm = std::min(a.x, b.x);
            picked.windowY1Mm = std::min(a.y, b.y);
            picked.windowX2Mm = std::max(a.x, b.x);
            picked.windowY2Mm = std::max(a.y, b.y);
            picked.area = PlotArea::Window;
            statusBar()->showMessage(tr("Plot window %1 × %2 mm")
                                         .arg(picked.windowX2Mm - picked.windowX1Mm, 0, 'f', 1)
                                         .arg(picked.windowY2Mm - picked.windowY1Mm, 0, 'f', 1),
                                     4000);
            openPageSetupManager(space, true, target, picked);
        },
        [this, space, target, working] {
            statusBar()->showMessage(tr("The plot window was left as it was"), 3000);
            openPageSetupManager(space, true, target, working);
        });
}

void MainWindow::addLayoutWindow() {
    if (!canvas_ || !canvas_->layoutMode()) {
        statusBar()->showMessage(
            tr("Open a layout first — a window onto the drawing is cut in a sheet."), 4000);
        return;
    }
    canvas_->addLayoutViewport();
    statusBar()->showMessage(
        tr("A window onto the drawing added — drag its corners to size it, drag inside it to move what "
           "it looks at, and set what it plots at with the scale in the corner."),
        6000);
}

void MainWindow::applyLayoutTemplate() {
    if (!doc_ || !canvas_->layoutMode()) return;
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Apply layout template"), QString(), DrawingTemplate::fileFilter());
    if (path.isEmpty()) return;
    DrawingTemplate drawingTemplate;
    if (!drawingTemplate.load(path)) {
        QMessageBox::warning(this, QStringLiteral("Layout template"),
                             QStringLiteral("Could not open the selected template."));
        return;
    }
    const int index = canvas_->layoutIndex();
    if (index < 0 || index >= static_cast<int>(doc_->layouts().size())) return;
    DrawingLayout& layout = doc_->layouts()[static_cast<size_t>(index)];
    layout.paperWidth = drawingTemplate.sheet.drawnWidthMm();
    layout.paperHeight = drawingTemplate.sheet.drawnHeightMm();
    layout.templateName = drawingTemplate.name;
    layout.paperEntities.clear();
    for (const QJsonValue& value : drawingTemplate.content)
        layout.paperEntities.push_back(Entity::fromJson(value.toObject()));
    doc_->setDirty(true);
    canvas_->invalidateScene();
    canvas_->fitView();
}

Document* MainWindow::editDoc() const {
    // Whatever the editor is pointed at: the drawing in model space, the sheet on a layout, and the
    // drawing again inside a window on a layout - where what is drawn is the harness, seen through it.
    if (editor_ && editor_->document()) return editor_->document();
    return canvas_ && canvas_->document() ? canvas_->document() : doc_;
}

Document* MainWindow::paperSpaceFor(Document* drawing, int layoutIndex) {
    if (!drawing || layoutIndex < 0 ||
        layoutIndex >= static_cast<int>(drawing->layouts().size()))
        return nullptr;
    const std::pair<Document*, int> key{drawing, layoutIndex};
    auto found = paperDocs_.find(key);
    if (found == paperDocs_.end()) {
        auto paper = std::make_unique<Document>();
        const DrawingLayout& layout = drawing->layouts()[static_cast<size_t>(layoutIndex)];
        paper->entities() = layout.paperEntities;
        found = paperDocs_.emplace(key, std::move(paper)).first;
    }
    // The drawing's layers, every time the sheet is opened: a line on the sheet then resolves its colour,
    // its width and its linetype the way a line in the model does, and a layer added or recoloured in the
    // drawing is there on the sheet too. The layer list is the drawing's - it is not per sheet.
    found->second->layers() = drawing->layers();
    found->second->setActiveLayer(drawing->activeLayer());
    return found->second.get();
}

void MainWindow::flushPaperSpace() {
    if (!paperLoaded_.first || paperLoaded_.second < 0) return;
    auto found = paperDocs_.find(paperLoaded_);
    if (found == paperDocs_.end()) return;
    Document* drawing = paperLoaded_.first;
    const int at = paperLoaded_.second;
    if (at >= static_cast<int>(drawing->layouts().size())) return;
    DrawingLayout& layout = drawing->layouts()[static_cast<size_t>(at)];
    Document* paper = found->second.get();
    layout.paperEntities = paper->entities();
    if (paper->dirty()) {
        drawing->setDirty(true);   // the sheet is part of the drawing; it is saved with it
        paper->setDirty(false);
    }
}

void MainWindow::forgetPaperSpaces(Document* drawing) {
    for (auto it = paperDocs_.begin(); it != paperDocs_.end();)
        it = it->first.first == drawing ? paperDocs_.erase(it) : std::next(it);
    if (paperLoaded_.first == drawing) paperLoaded_ = {nullptr, -1};
}

void MainWindow::activateDrawingSpace(int index, bool fitToSpace) {
    if (!canvas_) return;
    // On the way out of a sheet, what was drawn on it goes back into the layout.
    flushPaperSpace();
    // What the space being left had is kept, so coming back to it finds it as it was.
    if (lastDrawingSpace_ >= 0) spaceGrid_[lastDrawingSpace_] = canvas_->gridVisible();
    lastDrawingSpace_ = index;
    const bool layout = index > 0;
    canvas_->setLayoutMode(layout);
    if (layout) canvas_->setLayoutIndex(index - 1);
    // Which document the tools work in. On a layout that is the sheet's paper space; the drawing itself
    // stays in the canvas's other hand, for the layouts and for what the windows look at.
    Document* paper = layout ? paperSpaceFor(doc_, index - 1) : nullptr;
    if (paper) {
        paperLoaded_ = {doc_, index - 1};
        canvas_->setDrawingDocument(doc_);
        canvas_->setDocument(paper);
        if (editor_) editor_->setDocument(paper);
    } else {
        paperLoaded_ = {nullptr, -1};
        canvas_->setDrawingDocument(nullptr);
        canvas_->setDocument(doc_);
        if (editor_) editor_->setDocument(doc_);
    }
    if (layout) {
        canvas_->setBackgroundColor(QColor("#747a83"));
    } else {
        const bool editingBlock = doc_ && blockEdits_.contains(doc_);
        const bool editingTemplate = doc_ && templateEdits_.contains(doc_);
        const bool editingMechModel = doc_ && mechModelEdits_.contains(doc_);
        const bool editingLibraryPart = doc_ && libraryPartEdits_.contains(doc_);
        canvas_->setBackgroundColor(editingBlock       ? QColor("#6d7278")
                                    : editingTemplate  ? QColor("#7c8189")
                                    : editingMechModel  ? QColor("#3a3d42")
                                    : editingLibraryPart ? QColor("#42464c")
                                                        : normalCanvasColor_);
    }
    // The annotation scale of the space being entered: model space has its own (the scale the drawing will be
    // plotted at), and a layout has none - a sheet's annotation is at size on the paper, and it is the window
    // that is scaled.
    EntityRenderer::setAnnotationScale(layout ? 1.0
                                              : (spaceScale_.value(index, 1.0) > 1e-9
                                                     ? 1.0 / spaceScale_.value(index, 1.0)
                                                     : 1.0));
    // The grid this space was left with. A layout starts without one - a sheet is laid out over a title
    // block rather than over squares - and model space with the one it has always had.
    canvas_->setGridVisible(spaceGrid_.value(index, !layout));
    {
        // The button in the status bar shows the state of the space showing, not of the one left.
        const int gridBtn = statusToggleNames_.indexOf(QStringLiteral("grid"));
        if (gridBtn >= 0 && gridBtn < statusToggleBtns_.size() && statusToggleBtns_[gridBtn]) {
            const QSignalBlocker block(statusToggleBtns_[gridBtn]);
            statusToggleBtns_[gridBtn]->setChecked(canvas_->gridVisible());
        }
    }
    // And the scale of the layout showing. Model space has no plot scale - it is the harness, at size -
    // so the box is put out of reach there rather than showing a figure that means nothing.
    if (spaceScaleBox_) {
        double paperPerModel = 1.0;
        if (layout && doc_) {
            const int at = index - 1;
            const auto& layouts = doc_->layouts();
            if (at >= 0 && at < static_cast<int>(layouts.size()) &&
                !layouts[static_cast<size_t>(at)].viewports.empty())
                paperPerModel = layouts[static_cast<size_t>(at)].viewports.front().modelScale;
        }
        // In model space the box is the ANNOTATION scale: the scale the drawing will be plotted at, so that
        // the dimensions are drawn big enough to come off the paper the size they say. The harness itself is
        // never touched by it. On a layout it is the plot scale, as before.
        if (!layout) paperPerModel = spaceScale_.value(index, 1.0);
        const QSignalBlocker block(spaceScaleBox_);
        const int found = spaceScaleBox_->findData(paperPerModel);
        spaceScaleBox_->setCurrentIndex(found >= 0 ? found : (paperPerModel > 0.0 ? 1 : 0));
        spaceScaleBox_->setEnabled(true);
        spaceScaleBox_->setToolTip(
            layout ? tr("The scale this layout is plotted at: what one millimetre of paper is worth in "
                        "the model. Fit sizes the drawing to its window instead.")
                   : tr("The scale this drawing will be plotted at. The dimensions are drawn to suit, so "
                        "they come off the paper the size they say — the harness itself is unchanged."));
    }
    if (spaceScaleLabel_) spaceScaleLabel_->setEnabled(true);
    if (addViewportBtn_) {
        // Not there at all in model space, rather than there and greyed: a window onto the drawing is cut in
        // a SHEET, and there is nothing in the model for the button to mean.
        addViewportBtn_->setVisible(layout);
        addViewportBtn_->setEnabled(layout);
        addViewportBtn_->setToolTip(tr("Put a window onto the drawing on this sheet"));
    }
    // Fitted when a space is stepped into, not when a drawing is opened into the space already showing:
    // a drawing opens at the view it was saved with, and refitting it would throw that away.
    if (fitToSpace) canvas_->fitView();
    canvas_->setFocus();
}

MainWindow::~MainWindow() {
    // Said before anything of this window is taken apart: the panels have work they do on being hidden, and
    // none of it can be done once the window they hang in is half gone.
    AutoHideDock::windowGoing(this);
}

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
    // Named rather than styled here: the ribbon's own sheet colours it, so it follows the scheme. Styled on
    // the label itself, it would not - an inline sheet beats an ancestor's, so the group names stayed pale
    // grey on a light ribbon and could not be read at all.
    lbl->setObjectName(QStringLiteral("ribbonGroupTitle"));
    lbl->setStyleSheet(QStringLiteral("font-size:11px; background:transparent;"));
    v->addWidget(lbl);
    return g;
}

// A compact 2-row grid of icon tool buttons for the given tools.
QWidget* MainWindow::toolRow(const QList<ToolId>& tools, const QList<QWidget*>& alsoInTheGroup) {
    auto* w = new QWidget(ribbon_);
    auto* grid = new QGridLayout(w);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(1);
    // Two rows, and everything in the group goes in them - the tools, and whatever else belongs beside them.
    // Left out of the grid, an extra button stood full height next to two rows of tools, which is what made
    // the Modify and Annotation groups look thrown together rather than ordered.
    const int cells = static_cast<int>(tools.size()) + static_cast<int>(alsoInTheGroup.size());
    const int perRow = std::max(1, (cells + 1) / 2);
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

        b->setFixedSize(kRibbonCell);
        grid->addWidget(b, i / perRow, i % perRow);
        ++i;
    }
    for (QWidget* extra : alsoInTheGroup) {
        if (!extra) continue;
        extra->setParent(w);
        // The same square as a plain tool. Array, Copy and Table are split buttons - a click and a fall
        // menu - and a split button asks for more room than an icon does, so each of them made its own
        // column wider than the rest and the two rows stopped lining up. Given the same square as
        // everything else, with the arrow inside it (see the ribbon's menu-button rule), a group is a grid.
        if (auto* btn = qobject_cast<QToolButton*>(extra)) btn->setFixedSize(kRibbonCell);
        grid->addWidget(extra, i / perRow, i % perRow, Qt::AlignLeft | Qt::AlignVCenter);
        ++i;
    }
    return w;
}

// A 2-row icon grid of the Modify "Operations" actions, built as icon buttons in
// the same style as toolRow so they read consistently in the ribbon. Reused on
// both the Home and Modify tabs. Buttons are tracked in opButtons_ for theme
// re-tinting.
QToolButton* MainWindow::makeArrayButton(QWidget* parent) {
    auto* b = new QToolButton(parent);
    b->setIcon(makeOpIcon(QStringLiteral("array")));
    b->setIconSize(QSize(28, 28));
    b->setToolButtonStyle(Qt::ToolButtonIconOnly);
    b->setToolTip(QStringLiteral(
        "Array — rectangular, polar or path (use the arrow to choose the type)"));
    b->setAutoRaise(true);
    b->setFocusPolicy(Qt::NoFocus);
    connect(b, &QToolButton::clicked, this, [this] { beginArrayCommand(editor_->lastArrayKind()); });
    // AutoCAD-style flyout of the three array laws. The chosen one becomes the button's icon and what a
    // plain click repeats - so the icon on the ribbon always says which array you get.
    struct Kind { const char* icon; QString label; ArrayKind kind; };
    const QList<Kind> kinds = {
        {"array", QStringLiteral("Rectangular array (linear)"), ArrayKind::Rect},
        {"arraypolar", QStringLiteral("Polar array"), ArrayKind::Polar},
        {"arraypath", QStringLiteral("Path array (tracking)"), ArrayKind::Path},
    };
    auto* menu = new QMenu(b);
    for (const Kind& k : kinds) {
        const QString iconKey = QString::fromLatin1(k.icon);
        menu->addAction(makeOpIcon(iconKey), k.label, this, [this, b, iconKey, k] {
            b->setIcon(makeOpIcon(iconKey));
            for (auto& entry : opButtons_)
                if (entry.second == b) entry.first = iconKey;
            beginArrayCommand(k.kind);
        });
    }
    b->setMenu(menu);
    b->setPopupMode(QToolButton::MenuButtonPopup);
    opButtons_.append({QStringLiteral("array"), b});   // so the theme re-tints it
    return b;
}

QWidget* MainWindow::operationsRow() {
    struct Op { const char* key; QString tip; std::function<void()> fn; };
    // Array used to be here. It is a way of copying something about the drawing, which is what the
    // modify tools do - so it now sits with them, and this group is what is left: the commands that act
    // on a selection without drawing anything.
    const QList<Op> ops = {
        {"qselect",
         QStringLiteral("Quick Select — build a selection from an object's properties.\n"
                        "Either order works: select an object then click this, or click this, "
                        "pick objects on the drawing and press Enter."),
         [this] {
             editor_->beginQuickSelect();
             canvas_->setFocus(Qt::OtherFocusReason);
         }},
        {"explode", QStringLiteral("Explode"), [this] { editor_->explodeSelected(); }},
        {"purge", QStringLiteral("Purge (remove empty layers / stray objects)"),
         [this] { editor_->purgeUnused(); }},
        {"delete", QStringLiteral("Delete"), [this] { editor_->deleteSelected(); }},
    };
    auto* w = new QWidget(ribbon_);
    auto* grid = new QGridLayout(w);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(1);
    const int perRow = (ops.size() + 2) / 2;   // one more cell than the ops: Align, below
    int i = 0;
    // ALIGN: the objects picked brought into line - by their own edges, or on a line clicked through
    // the drawing. A click on the button offers the ways of doing it and only those; each acts on
    // what is picked, and the two cursor ones then wait for the click that says where.
    {
        auto* b = new QToolButton(w);
        b->setIcon(makeOpIcon(QStringLiteral("align")));
        b->setIconSize(QSize(28, 28));
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setToolTip(QStringLiteral(
            "Align — pick the objects, then choose how to line them up: by their left or right "
            "edges, their tops or bottoms, or on a point you click (snapped)."));
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setFixedSize(kRibbonCell);
        b->setObjectName(QStringLiteral("alignButton"));
        struct Way { const char* icon; QString label; Editor::AlignMode mode; };
        const QList<Way> ways = {
            {"alignleft", QStringLiteral("By object X-left"), Editor::AlignMode::Left},
            {"alignright", QStringLiteral("By object X-right"), Editor::AlignMode::Right},
            {"aligntop", QStringLiteral("By object top"), Editor::AlignMode::Top},
            {"alignbottom", QStringLiteral("By object bottom"), Editor::AlignMode::Bottom},
            {"alignx", QStringLiteral("By X-cursor"), Editor::AlignMode::CursorX},
            {"aligny", QStringLiteral("By Y-cursor"), Editor::AlignMode::CursorY},
        };
        auto* menu = new QMenu(b);
        for (const Way& way : ways) {
            const Editor::AlignMode mode = way.mode;
            menu->addAction(makeOpIcon(QString::fromLatin1(way.icon)), way.label, this, [this, mode] {
                editor_->alignSelection(mode);
                canvas_->setFocus(Qt::OtherFocusReason);   // the click that says where goes there
                canvas_->update();
            });
        }
        b->setMenu(menu);
        b->setPopupMode(QToolButton::InstantPopup);
        grid->addWidget(b, i / perRow, i % perRow);
        opButtons_.append({QStringLiteral("align"), b});   // so the theme re-tints it
        ++i;
    }
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
        b->setFixedSize(kRibbonCell);   // the same square as the tools beside them, on the same tab
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

QWidget* MainWindow::clipboardRow(bool iconOnly) {

    // A split button whose PRIMARY action is whichever was chosen last (Copy or Cut),
    // AutoCAD-flyout style: clicking runs it; the fall-menu switches it. Choosing Cut
    // from the menu makes the ribbon button become Cut (and vice-versa).
    auto* btn = new QToolButton(ribbon_);
    btn->setIconSize(QSize(28, 28));
    // Icon-only leans on the tooltip for the name (which is what the icon buttons beside
    // it do); the fall-menu still spells both actions out.
    btn->setToolButtonStyle(iconOnly ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
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

    // The BUTTON, not a widget holding it. Wrapped in a box with a stretch under it, Copy sat at the top of
    // a cell as tall as two rows - the cell was the right size and the button was not in the right part of
    // it, which looks exactly like a button in the wrong place.
    return btn;
}

// A ribbon tab page holds horizontal groups separated by vertical rules. Members rather than
// lambdas in buildRibbon(), because each tab is built in a file of its own and has to reach them.
std::pair<QWidget*, QHBoxLayout*> MainWindow::makeTabPage() {
    auto* page = new QWidget(ribbon_);
    auto* lay = new QHBoxLayout(page);
    lay->setContentsMargins(6, 4, 6, 2);
    lay->setSpacing(0);
    lay->setAlignment(Qt::AlignLeft);
    return {page, lay};
}

// Exactly one ribbon page visible: the one whose tab is selected.
//
// A QTabWidget shows and hides its pages itself. Calling show() on a page that is NOT the current one
// makes that page paint over the whole ribbon - the tab bar included - which is how the template's
// groups ended up on top of a drawing's ribbon with no tabs to be seen. A page that has been DETACHED
// (a contextual tab not currently offered) has to be hidden too: removing its tab leaves it a child of
// the ribbon, and a child that was never hidden is shown the moment its parent is.
void MainWindow::normaliseRibbonPages() {
    if (!ribbon_) return;
    QWidget* cur = ribbon_->currentWidget();
    for (int i = 0; i < ribbon_->count(); ++i) {
        QWidget* page = ribbon_->widget(i);
        if (page && page != cur && page->isVisible()) page->hide();
    }
    // EVERY contextual page has to be in this list. They are children of the ribbon but not tabs of it, so
    // one left out stays visible and floats over whichever page IS showing - its groups drawn on top of that
    // page's, with that page's group titles underneath them. Which is what the detail-view tab did the moment
    // it was added, and is why a new contextual tab is not finished until its name is here.
    for (QWidget* page : {templateTab_, hatchTab_, tableCellTab_, arrayTab_, connectorTab_, wireTab_,
                          captureTab_, detailTab_})
        if (page && ribbon_->indexOf(page) < 0 && page->isVisible()) page->hide();
    if (cur) cur->show();
}

// Undo and Redo, drawn as the arrows they are. The keys have always done this; the buttons are for
// the hand that is already on the mouse - and for anyone who does not know the keys exist.
// The stock symbols as what they ARE: a small icon each, in two rows, like the tools beside them. They used
// to be one button with a fall menu, which hides the very thing being chosen - a symbol is picked by its
// shape, and a menu of names makes the eye read words to find a picture it already knows.
QWidget* MainWindow::makePartSymbolRow(QWidget* parent, bool schematic) {
    auto* w = new QWidget(parent);
    auto* grid = new QGridLayout(w);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(1);
    const auto list = schematic ? schematicPartSymbols() : mechanicalPartSymbols();
    const int perRow = std::max(1, (static_cast<int>(list.size()) + 1) / 2);
    int i = 0;
    for (const PartSymbol s : list) {
        auto* b = new QToolButton(w);
        b->setIcon(makePartSymbolIcon(s));
        b->setIconSize(QSize(28, 28));
        b->setToolButtonStyle(Qt::ToolButtonIconOnly);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setFixedSize(kRibbonCell);   // the same square as every other icon on the ribbon
        b->setToolTip(QStringLiteral("%1 — %2").arg(partSymbolName(s), partSymbolTip(s)));
        connect(b, &QToolButton::clicked, this, [this, s] {
            // Handed to the cursor exactly like a pasted block: it ghosts under the crosshair, R turns it a
            // quarter turn at a time, and a click puts it down. Held by its middle: a resistor is put where
            // its body goes, not where its corner does.
            editor_->placeEntities(partSymbolEntities(s, Vec2{0.0, 0.0}), Vec2{0.0, 0.0});
            statusBar()->showMessage(
                QStringLiteral("%1 - click to place it, R turns it, Esc cancels").arg(partSymbolName(s)),
                4000);
        });
        partSymbolButtons_.append({static_cast<int>(s), b});   // so the scheme can re-draw the icon
        grid->addWidget(b, i / perRow, i % perRow);
        ++i;
    }
    return w;
}

QToolButton* MainWindow::makePartSymbolButton(QWidget* parent, bool schematic) {
    auto* btn = new QToolButton(parent);
    btn->setText(schematic ? QStringLiteral("Symbols") : QStringLiteral("Parts"));
    btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    btn->setIconSize(QSize(26, 26));
    btn->setAutoRaise(true);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setPopupMode(QToolButton::InstantPopup);
    btn->setToolTip(schematic
                        ? QStringLiteral("The stock symbols: resistor, capacitor, coil, ferrite. Pick "
                                         "one and it follows the cursor - R turns it, a click drops it.")
                        : QStringLiteral("The stock parts, drawn at size: resistors, capacitors, coils "
                                         "and ferrite rings. Pick one and it follows the cursor - R "
                                         "turns it, a click drops it."));
    auto* menu = new QMenu(btn);
    btn->setMenu(menu);
    const auto list = schematic ? schematicPartSymbols() : mechanicalPartSymbols();
    btn->setIcon(makePartSymbolIcon(list.front()));
    for (const PartSymbol s : list) {
        QAction* act = menu->addAction(makePartSymbolIcon(s), partSymbolName(s));
        act->setToolTip(partSymbolTip(s));
        connect(act, &QAction::triggered, this, [this, s, btn] {
            // Handed to the cursor exactly like a pasted block: it ghosts under the crosshair, R turns
            // it a quarter turn at a time, and a click puts it down.
            // Held by its middle: a resistor is put where its body goes, not where its corner does.
            editor_->placeEntities(partSymbolEntities(s, Vec2{0.0, 0.0}), Vec2{0.0, 0.0});
            btn->setIcon(makePartSymbolIcon(s));   // the button wears whatever was last placed
            statusBar()->showMessage(
                QStringLiteral("%1 - click to place it, R turns it, Esc cancels")
                    .arg(partSymbolName(s)),
                4000);
        });
    }
    {
        // The menus are long enough to want their descriptions.
        menu->setToolTipsVisible(true);
    }
    return btn;
}

// Undo and Redo, drawn: a curved arrow going back or forward. The ink is passed in, because on a light
// ribbon a pale grey arrow is not a faint arrow - it is no arrow at all.
static QIcon undoArrowIcon(bool redo, bool compact, const QColor& ink) {
    QPixmap pm(28, 28);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(ink);
    // Small, the arrow is drawn BOLDER and BIGGER in its box rather than being the large one shrunk: at 18
    // pixels a 2.2 wide stroke lands on less than two and reads as grey, and a five-pixel head disappears.
    pen.setWidthF(compact ? 3.4 : 2.2);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    const double head = compact ? 9.0 : 5.0;
    const double x0 = compact ? 3.0 : 6.0, x1 = compact ? 25.0 : 22.0;
    const double yTip = compact ? 11.0 : 12.0, yTail = compact ? 21.0 : 19.0;
    QPainterPath path;
    if (redo) {
        path.moveTo(x0, yTail);
        path.cubicTo(x0 + 3, 8, x1 - 3, 6, x1, yTip);
    } else {
        path.moveTo(x1, yTail);
        path.cubicTo(x1 - 3, 8, x0 + 3, 6, x0, yTip);
    }
    p.drawPath(path);
    p.setBrush(ink);
    p.setPen(Qt::NoPen);
    QPolygonF tip;
    if (redo) tip << QPointF(x1, yTip - 1) << QPointF(x1 - head, yTip - 1) << QPointF(x1, yTip - 1 + head);
    else tip << QPointF(x0, yTip - 1) << QPointF(x0 + head, yTip - 1) << QPointF(x0, yTip - 1 + head);
    p.drawPolygon(tip);
    return QIcon(pm);
}

QWidget* MainWindow::undoRedoRow(QWidget* parent, bool compact) {
    auto* host = new QWidget(parent);
    auto* lay = new QHBoxLayout(host);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(2);
    auto arrow = [compact](bool redo) { return undoArrowIcon(redo, compact, Chrome::ink()); };
    auto button = [&](bool redo) {
        auto* b = new QToolButton(host);
        b->setIcon(arrow(redo));
        b->setIconSize(compact ? QSize(18, 18) : QSize(26, 26));
        b->setToolButtonStyle(compact ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextUnderIcon);
        if (!compact) b->setText(redo ? QStringLiteral("Redo") : QStringLiteral("Undo"));
        b->setToolTip(redo ? QStringLiteral("Redo the last undone change (Ctrl+Y)")
                           : QStringLiteral("Undo the last change (Ctrl+Z)"));
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        if (compact) b->setFixedSize(24, 22);
        else b->setMinimumWidth(52);
        // What it is, kept on the button: the icons are drawn, so a change of scheme means drawing them
        // again, and the drawing needs to know which arrow and which size.
        b->setProperty("redo", redo);
        b->setProperty("compact", compact);
        undoRedoBtns_.append(b);
        connect(b, &QToolButton::clicked, this, [this, redo] {
            if (redo) editor_->redo();
            else editor_->undo();
            canvas_->setFocus(Qt::OtherFocusReason);   // the keys keep working afterwards
        });
        // Greyed when there is nothing to do, so the button says what the drawing's state is.
        auto sync = [this, b, redo] { b->setEnabled(redo ? editor_->canRedo() : editor_->canUndo()); };
        sync();
        connect(editor_, &Editor::changed, this, sync);
        connect(editor_, &Editor::selectionChanged, this, sync);
        return b;
    };
    if (compact) {
        // On the tab-bar row, which is its own darker colour. It has to be painted here: the corner of
        // a tab widget is not the tab bar and not the pane, so left to itself it comes out in the
        // application's light palette - a white patch in the corner of a charcoal ribbon.
        lay->setContentsMargins(4, 4, 8, 4);
        host->setMinimumHeight(30);   // fills the tab row: the corner is painted by the style otherwise
        ribbonCorner_ = host;
        restyleRibbonCorner();
    }
    lay->addWidget(button(false));
    lay->addWidget(button(true));
    return host;
}

void MainWindow::addGroup(QHBoxLayout* lay, const QString& title, QWidget* content) {
    addGroupKeep(lay, title, content);
}

QWidget* MainWindow::addGroupKeep(QHBoxLayout* lay, const QString& title, QWidget* content) {
    // The group and its divider in one holder, so folding a group away takes its rule with it and
    // leaves no double line behind.
    auto* holder = new QWidget(lay->parentWidget());
    auto* hl = new QHBoxLayout(holder);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(0);
    hl->addWidget(ribbonGroup(title, content));
    auto* rule = new QFrame(holder);
    rule->setFrameShape(QFrame::VLine);
    rule->setObjectName(QStringLiteral("ribbonGroupRule"));
    hl->addWidget(rule);
    lay->addWidget(holder);
    return holder;
}

// Match properties (paintbrush): pick a source object, then click similar-type targets to copy its
// style/geometry/technical properties. Shared by the Home and Schematic-build Modify groups.
// iconOnly => match the plain tool-row icons (Home); otherwise a labelled text-under-icon button.
QToolButton* MainWindow::makeMatchBtn(QWidget* parent, bool iconOnly) {
        auto* b = new QToolButton(parent);
        if (iconOnly) {
            b->setToolButtonStyle(Qt::ToolButtonIconOnly);
            b->setIconSize(QSize(28, 28)); // same as toolRow() icons
            b->setFixedSize(kRibbonCell);  // and the same square, so a row of icons stays a row of icons
            b->setAutoRaise(true);
            b->setFocusPolicy(Qt::NoFocus);
        } else {
            b->setText(QStringLiteral("Match"));
            b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            b->setIconSize(QSize(26, 26));
            b->setMinimumWidth(66);
        }
        b->setToolTip(QStringLiteral(
            "Match properties — either order works:\n"
            "  select an object, click this, then click the objects to paint, or\n"
            "  click this, click the source object, then click the objects to paint.\n"
            "Copies layer / colour / lineweight always, and geometry, font and technical "
            "properties between objects of the same type (Esc to end)"));
        // Latches while the command is armed, so it is clear it is waiting for clicks.
        b->setCheckable(true);
        matchBtns_.append(b);
        {
            QPixmap pm(28, 28);
            pm.fill(Qt::transparent);
            QPainter ip(&pm);
            ip.setRenderHint(QPainter::Antialiasing, true);
            // A paintbrush: handle + ferrule + bristles (the AutoCAD MATCHPROP motif).
            QPen pen(QColor("#cfe3ff"));
            pen.setWidthF(2.0);
            pen.setCapStyle(Qt::RoundCap);
            pen.setJoinStyle(Qt::RoundJoin);
            ip.setPen(pen);
            ip.drawLine(21, 5, 12, 14);            // handle
            ip.setBrush(QColor("#8fb7ff"));
            ip.drawRoundedRect(QRectF(8, 12, 8, 6), 1.5, 1.5); // ferrule
            ip.setBrush(QColor("#ffd27f"));
            QPolygonF bristles;
            bristles << QPointF(8, 17) << QPointF(16, 17) << QPointF(13, 24) << QPointF(6, 22);
            ip.setPen(QPen(QColor("#ffd27f"), 1.0));
            ip.drawPolygon(bristles);            // bristle tuft
            ip.end();
            b->setIcon(QIcon(pm));
        }
        connect(b, &QToolButton::clicked, this, [this, b] {
            // Clicking it while armed ends the command (a toggle, like the pin buttons).
            if (!b->isChecked()) {
                editor_->setTool(ToolId::Select); // clears match mode + un-latches
                return;
            }
            editor_->beginMatchProps();
            canvas_->setFocus(Qt::OtherFocusReason);
        });
        return b;
}

// The ribbon's own stylesheet, in whichever scheme is set. One function for it, called when the ribbon is
// built and again when the scheme changes: written out twice, the two would drift, and the ribbon is where
// the words are - the tab names, the group names, the fields. Text colour is the whole of the difference
// between a light ribbon that can be read and one that cannot.
QString MainWindow::ribbonStyleSheet() const {
    const bool dark = darkTheme_;
    const QHash<QString, QString> c =
        dark ? QHash<QString, QString>{
                   {"BG", Chrome::back().name()}, {"LINE", "#2a2c31"}, {"RULE", "#b3afa6"},
                   {"TX", Chrome::backInk().name()}, {"TXHI", "#ffffff"}, {"LBL", "#c8ccd2"},
                   {"TABBAR", "#2f3237"},    {"HOVER", "#484c53"},  {"HOVERBORD", "#5a5f68"},
                   {"SEL", "#294e74"},       {"PRESS", "#22384f"},  {"FIELD", "#565b64"},
                   {"FIELDTX", "#f2f4f7"},   {"FIELDBORD", "#7b818b"}, {"FOCUS", "#5aa0ff"},
                   {"DROP", "#6a6f78"},      {"DROPHOVER", "#7c828c"}}
             : QHash<QString, QString>{
                   // Light: the lettering goes to near-black, not to a paler grey. A group's name and a
                   // tab's name are read at a glance or not at all.
                   {"BG", Chrome::back().name()}, {"LINE", "#c2c2c8"}, {"RULE", "#b8b8be"},
                   {"TX", Chrome::backInk().name()}, {"TXHI", "#000000"}, {"LBL", "#4c525b"},
                   {"TABBAR", "#dcdce2"},    {"HOVER", "#d0e3ff"},  {"HOVERBORD", "#9cc4f0"},
                   {"SEL", "#cfe3ff"},       {"PRESS", "#bcd9fb"},  {"FIELD", "#ffffff"},
                   {"FIELDTX", "#1e2126"},   {"FIELDBORD", "#a8aeb8"}, {"FOCUS", "#3d8bfd"},
                   {"DROP", "#e2e4e8"},      {"DROPHOVER", "#d0d4da"}};
    QString ss = QStringLiteral(
        "QTabWidget::pane{background-color:{BG};border:none;border-bottom:1px solid {LINE};}"
        "QWidget{background-color:{BG};color:{TX};}"
        "QTabBar{background-color:{TABBAR};}"
        "QTabBar::tab{background-color:transparent;color:{TX};padding:5px 14px;border:none;}"
        "QTabBar::tab:hover{color:{TXHI};}"
        "QTabBar::tab:selected{color:{TXHI};background-color:{BG};border-bottom:2px solid #3d8bfd;}"
        "QLabel{background-color:transparent;color:{LBL};}"
        "QToolButton{background-color:transparent;border:1px solid transparent;border-radius:4px;padding:3px 6px;}"
        "QToolButton:hover{background-color:{HOVER};border-color:{HOVERBORD};}"
        "QToolButton:checked{background-color:{SEL};border-color:#3d8bfd;}"
        "QToolButton:pressed{background-color:{PRESS};}"
        // A split button's arrow: a narrow strip inside the button's own square rather than something
        // added to its width. Styling the strip at all takes the style's arrow away, so it is given as
        // the same chevron image the combo boxes use.
        "QToolButton::menu-button{width:11px;border:none;background:transparent;}"
        "QToolButton::menu-arrow{image:url(:/caret-down.svg);width:9px;height:9px;}"
        "QFrame{color:{LINE};}"
        // A group's NAME under its buttons, and the rule between one group and the next.
        // Named where they are built rather than styled there, so they follow the scheme:
        // an inline stylesheet beats an ancestor's, and the names stayed pale grey on a
        // light ribbon - which is to say unreadable.
        "QLabel#ribbonGroupTitle{background-color:transparent;color:{LBL};}"
        "QFrame#ribbonGroupRule{color:{RULE};}"
        // Combos + spin fields: lighter field, bright text, clear border so they
        // read against the charcoal ribbon in dark mode.
        "QComboBox{background-color:{FIELD};color:{FIELDTX};border:1px solid {FIELDBORD};border-radius:3px;padding:1px 6px;min-height:18px;}"
        "QComboBox:hover{border-color:{FOCUS};}"
        // Line edits (and the editable-combo line edit) need bright text too, else they
        // render near-black on the ribbon.
        "QLineEdit{background-color:{FIELD};color:{FIELDTX};border:1px solid {FIELDBORD};border-radius:3px;padding:1px 4px;min-height:18px;}"
        "QComboBox QLineEdit{background-color:transparent;border:none;color:{FIELDTX};}"
        // A distinct, button-like drop-down box with a clear down-triangle (a non-zero
        // arrow box is required for the CSS border-triangle to render).
        "QComboBox::drop-down{subcontrol-origin:padding;subcontrol-position:right center;"
        "background-color:{DROP};border-left:1px solid {FIELDBORD};border-top-right-radius:3px;"
        "border-bottom-right-radius:3px;width:18px;}"
        "QComboBox::drop-down:hover{background-color:{DROPHOVER};}"
        "QComboBox::down-arrow{image:url(:/caret-down.svg);width:12px;height:12px;}"
        "QComboBox QAbstractItemView{background-color:{TABBAR};color:{FIELDTX};selection-background-color:#3d8bfd;selection-color:{TXHI};}"
        "QAbstractSpinBox{background-color:{FIELD};color:{FIELDTX};border:1px solid {FIELDBORD};border-radius:3px;padding:1px 4px;min-height:18px;}"
        "QAbstractSpinBox:hover{border-color:{FOCUS};}"
        "QAbstractSpinBox::up-button,QAbstractSpinBox::down-button{width:14px;background-color:{DROP};}"
        // The arrows have to be IMAGES. Styling ::up-button at all stops Qt drawing the style's own
        // arrow, and its stylesheet engine does not do the CSS border-triangle trick - so what was
        // here before ("image:none" plus borders) left two blank blocks that turned blue on hover,
        // which is exactly what a spin box must not look like.
        "QAbstractSpinBox::up-arrow{image:url(:/caret-up.svg);width:9px;height:9px;}"
        "QAbstractSpinBox::down-arrow{image:url(:/caret-down.svg);width:9px;height:9px;}"
        "QAbstractSpinBox::up-arrow:disabled,QAbstractSpinBox::down-arrow:disabled{opacity:80;}");
    for (auto it = c.cbegin(); it != c.cend(); ++it)
        ss.replace(QStringLiteral("{%1}").arg(it.key()), it.value());
    // A checked tool must read as checked in both: a pale blue wash with dark ink, not white on white.
    ss += dark ? QStringLiteral("QToolButton:checked{color:#ffffff;}")
               : QStringLiteral("QToolButton:checked{color:#10243d;}");
    return ss;
}

void MainWindow::buildRibbon() {
    toolGroup_ = new QActionGroup(this);
    toolGroup_->setExclusive(true);

    ribbon_ = new QTabWidget(this);
    ribbon_->setDocumentMode(true);
    ribbon_->setFixedHeight(150); // taller: larger icons + room for the 4-row hatch fields
    // The ribbon's colours, scoped to the ribbon and its children. background-color rather than the
    // `background` shorthand, so it reliably wins over the application sheet's QWidget rule.
    ribbon_->setStyleSheet(ribbonStyleSheet());

    // One tab per file, in src/app/ribbon/ - the ribbon was the biggest thing in this file and the
    // place every new feature landed.
    // Undo and Redo live in the ribbon's top right corner: small, always there whichever tab is
    // open, and out of the way of the tools. They used to be a group of their own at the front of
    // Home and of Schematic build - two large buttons taking the best place on the tab, and absent
    // from every other tab.
    //
    // AND THE WAY OUT OF A MODEL SESSION GOES THERE TOO, for the same reason. Capture and Cancel were on the
    // Home tab, in the session's own group - and a symbol is drawn with Modify and Schematic build as much as
    // with Home, so the moment you crossed to another tab to draw the two buttons that FINISH what you are
    // doing went out of sight. A session you can leave only by going back to the tab you started on is a
    // session with a way in and no way out.
    //
    // Beside the tab bar, where they are visible from every tab and cannot be scrolled away from.
    {
        auto* corner = new QWidget(ribbon_);
        auto* cl = new QHBoxLayout(corner);
        cl->setContentsMargins(0, 0, 0, 0);
        cl->setSpacing(2);
        modelSessionCorner_ = new QWidget(corner);
        auto* sl = new QHBoxLayout(modelSessionCorner_);
        sl->setContentsMargins(0, 0, 6, 0);
        sl->setSpacing(2);
        // A tick and a cross, drawn: on a strip this size there is room for a picture and a word, and the
        // picture is what the eye finds. Drawn rather than lettered so they are the same at any font.
        auto mark = [](bool take) {
            QPixmap pm(18, 18);
            pm.fill(Qt::transparent);
            QPainter p(&pm);
            p.setRenderHint(QPainter::Antialiasing, true);
            QPen pen(take ? QColor("#8fd6a2") : QColor("#e0a0a0"));
            pen.setWidthF(2.2);
            p.setPen(pen);
            if (take) {
                p.drawLine(QPointF(3.5, 10.0), QPointF(7.0, 13.5));
                p.drawLine(QPointF(7.0, 13.5), QPointF(14.5, 4.5));
            } else {
                p.drawLine(QPointF(4.5, 4.5), QPointF(13.5, 13.5));
                p.drawLine(QPointF(13.5, 4.5), QPointF(4.5, 13.5));
            }
            return QIcon(pm);
        };
        auto sessionBtn = [&](const QString& text, const QIcon& icon, const QString& tip) {
            auto* b = new QToolButton(modelSessionCorner_);
            b->setText(text);
            b->setIcon(icon);
            b->setToolTip(tip);
            b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
            b->setIconSize(QSize(16, 16));
            b->setAutoRaise(true);
            b->setFocusPolicy(Qt::NoFocus);
            sl->addWidget(b);
            return b;
        };
        cornerCaptureBtn_ =
            sessionBtn(tr("Capture"), mark(true),
                       tr("Take what is drawn here as this model and close the session"));
        connect(cornerCaptureBtn_, &QToolButton::clicked, this,
                [this] { saveMechModelBeingEdited(); });
        cornerCancelBtn_ = sessionBtn(tr("Cancel"), mark(false), tr("Leave the model as it was"));
        connect(cornerCancelBtn_, &QToolButton::clicked, this, [this] { cancelMechModelEdit(); });
        modelSessionCorner_->hide();   // only while a model or a library part is being drawn
        cl->addWidget(modelSessionCorner_);
        cl->addWidget(undoRedoRow(corner, /*compact=*/true));
        ribbon_->setCornerWidget(corner, Qt::TopRightCorner);
    }

    buildHomeTab();
    buildModifyTab();
    buildInsertTab();
    buildAnnotationTab();
    buildSchematicBuildTab();
    buildHarnessMechanicalTab();
    // The View tab is a list of the panels there are. The panels themselves do not exist yet - buildPanels()
    // runs after this - so its buttons are wired by name and switched on later by syncViewPanelButtons().
    buildViewTab();

    // (Parametric and Output ribbon tabs were removed — file New/Open/Save and the
    //  DXF/PDF/CSV exports live on the E (application) menu and the command line.)

    QToolBar* top = addToolBar(QStringLiteral("Ribbon"));
    top->setObjectName(QStringLiteral("ribbonBar"));   // named, so the saved arrangement knows it
    top->setMovable(false);
    top->setFloatable(false);
    top->addWidget(ribbon_);
    buildHatchRibbonTab(); // built once, added to the ribbon only while hatching
    buildDetailRibbonTab(); // likewise: only while a detail view is selected
    buildTableCellRibbonTab(); // built once, added only while a table cell is selected
    buildConnectorRibbonTab(); // built once, added only while placing/editing a connector
    buildWireRibbonTab();      // built once, added only while the Wire tool is active
    buildArrayRibbonTab();     // built once, added only while an array is being laid out
    buildTemplateRibbonTab();  // built once, added only while a template is being drawn
    // Drafting toggles + grid size now live in the bottom-right status bar
    // (see buildStatusControls()).
}

// Context-driven visibility of the Schematic-build ribbon groups:
//   • a twist is armed  -> show ONLY the Twist group (the twist menu takes over the ribbon)
//   • a Splice/X marker -> show the Marker group, hide Modify / DRC
//   • otherwise         -> show Place / Modify / DRC, hide Marker and Twist
// The break mark's fields: shown while its tool is armed, and the third one named for the style being
// placed - a straight stroke has a length, an S has a reach across the run.
// While the break tool is armed the Harness-mechanical tab shows the break group alone: the other
// groups place bundles, dimension them and call parts out, none of which is what the next click will
// do. Leaving the tab as it was made the mark's own fields one small thing among many.
void MainWindow::updateModelRibbonTabs() {
    if (!ribbon_) return;
    const bool libraryPartEdit = doc_ && libraryPartEdits_.contains(doc_);
    const bool modelEdit = (doc_ && mechModelEdits_.contains(doc_)) || libraryPartEdit;
    const bool schematicModel = (doc_ && mechModelEditSchematic_.value(doc_, false)) ||
                                 (libraryPartEdit && libraryPartEditKind_.value(doc_) == LibraryPartDrawing::Schematic);
    int home = -1;
    for (int i = 0; i < ribbon_->count(); ++i) {
        if (ribbon_->tabText(i) == QStringLiteral("Home")) home = i;
    }
    // What a model session puts away, and what it must not.
    //
    // A default model is one part drawn on its own, so laying out a whole harness has nothing to act
    // on - Harness mechanical goes away for EITHER kind of model session. Schematic build is the
    // opposite: it is where a schematic symbol is actually drawn (its Connector/Wire/Splice tools and
    // the Connection pin tool), so it stays for a SCHEMATIC session and only goes for a mechanical
    // one. Everything else stays. View went too, and that was wrong - zooming, panning and the layers
    // are exactly what somebody drawing a part needs, and a session that takes them away is a session
    // you cannot draw in. Import stays for the same reason: a part is often traced from a supplier's
    // DXF, and that is the one thing you most want to bring in here.
    // A DEFAULT MODEL'S SCHEMATIC SYMBOL IS DRAWN WITH THREE TABS AND NOTHING ELSE: Home to draw with,
    // Modify to change what is drawn, Schematic build to put the schematic's own pieces down. A symbol is a
    // small glyph - a dozen lines and a few pins - and every other tab is about a SHEET: dimensioning and
    // calling out a drawing, importing a supplier's DXF, laying a harness out, setting a title block. None of
    // it applies to a glyph, and the house rule for this application is that while a function is being used
    // the ribbon shows that function and not the whole program.
    //
    // Contextual tabs are left alone. They are not there unless their own function is armed, and while
    // drawing a symbol you may well hatch a shape or array a row of pins - taking away the tab that has just
    // appeared to serve the thing you are doing is not tidiness, it is a trap.
    const bool schematicDefaultModel =
        doc_ && mechModelEditSchematic_.value(doc_, false) && !libraryPartEdit;
    auto contextual = [this](QWidget* page) {
        for (QWidget* p : {templateTab_, hatchTab_, tableCellTab_, arrayTab_, connectorTab_, wireTab_,
                           captureTab_, detailTab_})
            if (p && p == page) return true;
        return false;
    };
    auto putAway = [&](int i) {
        const QString title = ribbon_->tabText(i);
        if (schematicDefaultModel) {
            if (contextual(ribbon_->widget(i))) return false;
            return !(title == QStringLiteral("Home") || title == QStringLiteral("Modify") ||
                     title == QStringLiteral("Schematic build"));
        }
        if (title == QStringLiteral("Harness mechanical")) return true;
        if (title == QStringLiteral("Schematic build")) return !schematicModel;
        return false;
    };
    if (modelEdit && home >= 0 && putAway(ribbon_->currentIndex())) ribbon_->setCurrentIndex(home);
    for (int i = 0; i < ribbon_->count(); ++i)
        ribbon_->tabBar()->setTabVisible(i, !modelEdit || !putAway(i));
    // AND THE STRIP IS LAID OUT AGAIN. A tab put away gives its space back - the tab bar works that out for
    // itself - but the strip on the screen is whatever was last painted, and a session opening left the
    // holes where the hidden tabs had been until something else happened to force a repaint. Asked for
    // here, so the tabs that are showing are drawn next to one another straight away.
    ribbon_->tabBar()->updateGeometry();
    ribbon_->tabBar()->update();
    normaliseRibbonPages();
}

// A FIELD WHOSE LOWEST VALUE IS "AUTO", with the little A that puts it back there.
//
// Auto lives at the bottom of the range, which is fine for reading and hopeless for setting: once a
// number has been typed in, getting back to Auto means holding the down arrow to the end of the
// range or knowing that a number below the minimum lands there. The A does it in one click, and it
// lights up while the field is on Auto, so the field says which it is without being read.
QWidget* MainWindow::autoBeside(QAbstractSpinBox* field, std::function<void()> onAuto) {
    if (!field) return nullptr;
    auto* cell = new QWidget(field->parentWidget());
    cell->setObjectName(QStringLiteral("autoFieldCell"));
    auto* row = new QHBoxLayout(cell);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(2);
    field->setParent(cell);
    row->addWidget(field);
    auto* mark = new QToolButton(cell);
    mark->setText(QStringLiteral("A"));
    mark->setCheckable(true);
    mark->setFocusPolicy(Qt::NoFocus);
    // IT IS A LETTER, AND IT HAS TO SAY SO. A tool button shows its icon and not its text unless it
    // is told otherwise - that is Qt's default - and this one has no icon at all, so what it shows
    // is whatever the style makes of a button with neither.
    mark->setToolButtonStyle(Qt::ToolButtonTextOnly);
    // AS SMALL AS A MARK ON A FIELD, because that is what it is: it sits beside the arrows and
    // belongs to the field, and at the size of a button it read as another control on the ribbon
    // and took width from fields that have none to spare.
    mark->setFixedSize(13, 13);
    mark->setToolTip(QStringLiteral("Auto - leave this one to the drawing"));
    mark->setStyleSheet(QStringLiteral(
        "QToolButton{color:#9fb3c8;background:#2a3644;border:1px solid #3a4756;border-radius:2px;"
        "font-size:8px;font-weight:bold;padding:0px;margin:0px;}"
        "QToolButton:hover{background:#35435a;color:#e6e9ef;}"
        "QToolButton:checked{background:#3d6ea5;border-color:#5b93cf;color:#ffffff;}"));
    row->addWidget(mark, 0, Qt::AlignVCenter);
    // WHAT CLICKING IT DOES, and how it knows to be lit.
    //
    // A field that is DRIVEN - one whose caller told us what "leave it to the drawing" means for it -
    // shows the figure the drawing worked out while it is on Auto, and its A is lit or dark by
    // whoever is showing it. That is the whole point: a field saying "Auto" is standing for a number
    // nobody can read, and a field showing the number it stands for is the same field doing its job.
    // Clicking the A puts it back to the drawing's own; typing over the figure takes it off Auto,
    // and the A goes dark, because a typed number is a number somebody meant.
    //
    // A field that is NOT driven still works the old way: Auto is the bottom of its range, the A
    // takes it there, and it lights when it is there.
    if (onAuto) {
        connect(mark, &QToolButton::clicked, field, [onAuto] { onAuto(); });
        // AND IT GOES DARK THE MOMENT SOMEBODY TYPES, not at the next time the ribbon is synced.
        // Anything that reaches here is a person changing the field: every time the field is set
        // from the settings it is set with its signals blocked, exactly so that this can tell the
        // difference between a figure being shown and a figure being chosen.
        auto darken = [mark] {
            QSignalBlocker block(mark);
            mark->setChecked(false);
        };
        if (auto* d = qobject_cast<QDoubleSpinBox*>(field))
            connect(d, &QDoubleSpinBox::valueChanged, mark, [darken](double) { darken(); });
        else if (auto* i = qobject_cast<QSpinBox*>(field))
            connect(i, &QSpinBox::valueChanged, mark, [darken](int) { darken(); });
        return cell;
    }
    auto atBottom = [field] {
        if (auto* d = qobject_cast<QDoubleSpinBox*>(field))
            return d->value() <= d->minimum() + 1e-9;
        if (auto* i = qobject_cast<QSpinBox*>(field)) return i->value() <= i->minimum();
        return false;
    };
    auto show = [mark, atBottom] {
        QSignalBlocker block(mark);
        mark->setChecked(atBottom());
    };
    if (auto* d = qobject_cast<QDoubleSpinBox*>(field))
        connect(d, &QDoubleSpinBox::valueChanged, mark, [show](double) { show(); });
    else if (auto* i = qobject_cast<QSpinBox*>(field))
        connect(i, &QSpinBox::valueChanged, mark, [show](int) { show(); });
    connect(mark, &QToolButton::clicked, field, [field, show] {
        // Clicking it always means Auto, so clicking it again while it is on Auto changes nothing
        // rather than turning Auto off into some number nobody chose.
        if (auto* d = qobject_cast<QDoubleSpinBox*>(field)) d->setValue(d->minimum());
        else if (auto* i = qobject_cast<QSpinBox*>(field)) i->setValue(i->minimum());
        show();
    });
    show();
    return cell;
}

QToolButton* MainWindow::autoMarkOf(QWidget* field) {
    QWidget* cell = autoFieldCell(field);
    if (!cell || cell == field) return nullptr;
    for (QToolButton* b : cell->findChildren<QToolButton*>())
        if (b->text() == QStringLiteral("A")) return b;
    return nullptr;
}

void MainWindow::showAutoField(QDoubleSpinBox* field, QLabel* label, bool shown, bool isAuto,
                               double value) {
    if (!field) return;
    if (label) label->setVisible(shown);
    if (QWidget* cell = autoFieldCell(field)) cell->setVisible(shown);
    if (!shown) return;
    QSignalBlocker block(field);
    // ONLY WHILE IT IS THE DRAWING'S OWN FIGURE. Lit, the A means "you work it out", and what stands
    // in the field is the drawing's answer - so it is written here every time that answer changes.
    // Dark, the number is one somebody typed, and it is theirs: the drawing may not be able to use
    // it in full - a fan longer than the run it is on, a radius tighter than the bundle can turn -
    // and it makes what it can of it, but the figure that was ASKED FOR does not quietly become the
    // figure that was possible. Typing 40 and watching the field say 12 reads as the field refusing
    // to take what you typed.
    if (isAuto) field->setValue(value);
    if (QToolButton* mark = autoMarkOf(field)) {
        QSignalBlocker lit(mark);
        mark->setChecked(isAuto);
    }
}

// The thing to show and hide for such a field: the field and its A together, not the field alone -
// hiding the field on its own leaves the A behind with nothing beside it.
QWidget* MainWindow::autoFieldCell(QWidget* field) {
    if (field && field->parentWidget() &&
        field->parentWidget()->objectName() == QLatin1String("autoFieldCell"))
        return field->parentWidget();
    return field;
}

void MainWindow::updateMechanicalRibbonGroups() {
    if (!editor_) return;
    // The rule for this application: while a function is being used, the ribbon shows THAT FUNCTION'S
    // settings and nothing else. The tools that cannot be used during it are noise, and hunting for the
    // two fields that matter among forty buttons is what makes a drawing slow to make.
    const bool libraryPartEdit = doc_ && libraryPartEdits_.contains(doc_);
    const bool modelEdit = (doc_ && mechModelEdits_.contains(doc_)) || libraryPartEdit;
    // A label being STROKED ON takes the tab outright: its own two fields, and nothing else, because
    // nothing else on the tab applies while one is going down. It gives the tab back with the click that
    // places it - the fields are for before the click, which is what pinning with Tab is for, and a group
    // that stays up afterwards leaves the tab taken over by a function that has finished. A placed label's
    // sizes live in the properties.
    const bool labelling = editor_->placingLabel();
    if (labelFitGroup_) labelFitGroup_->setVisible(labelling && !modelEdit);
    if (labelling && !modelEdit) {
        for (QWidget* g : mechGroups_)
            if (g) g->setVisible(false);
        if (mechModelSessionGroup_) mechModelSessionGroup_->setVisible(false);
        return;
    }
    if (modelEdit) {
        for (QWidget* g : mechModelHiddenGroups_)
            if (g) g->setVisible(false);
        // Mark near/far/connect/output/scale-reference: placing a part BY a direction along a run,
        // which is a mechanical-drawing idea a schematic symbol has no use for (see
        // openMechModelEditor's schematic branch, which never marks any of these to begin with). A
        // library part's session keeps the group up regardless of which drawing it is - it is the only
        // place Save/Cancel for that session live, and every kind of library-part drawing needs them.
        // A SCHEMATIC DRAWING HAS NO CONNECTION POINT, whether it belongs to a default model or to a library
        // part. The marks - where it connects and which way it runs from there, the two ends that must not
        // stretch, the faces the conductors leave by - all describe placing a part BY a direction along a run.
        // A symbol is a fixed glyph on a circuit: there is nothing on it for any of them to point at, and a
        // library part's session was offering all five of them anyway.
        const bool schematicDrawing =
            (doc_ && mechModelEditSchematic_.value(doc_, false)) ||
            (libraryPartEdit && libraryPartEditKind_.value(doc_) == LibraryPartDrawing::Schematic);
        const bool schematicDefaultModel = doc_ && mechModelEditSchematic_.value(doc_, false) && !libraryPartEdit;
        (void)schematicDefaultModel;
        // BUT CAPTURE AND CANCEL STAY. The whole group used to go for a schematic session, which took the two
        // buttons that END the session away with the five that mark up a mechanical drawing - so a symbol
        // could be drawn and then not put anywhere: the only ways out were the window's close box and the
        // keyboard. Taking a part of the drawing away is not the same as taking away the way to finish.
        //
        // The marks themselves do go, and should: a connection point, two ends and the faces the conductors
        // leave by describe placing a part BY a direction along a run, and a schematic symbol is not placed
        // that way. It is a fixed glyph, and there is nothing on it for any of them to point at.
        // THE WAY OUT IS IN THE CORNER NOW, visible from every tab, so the group's own pair stands down:
        // two Captures on the screen at once is two answers to "which one finishes this".
        if (captureMechModelBtn_) captureMechModelBtn_->setVisible(false);
        if (cancelMechModelBtn_) cancelMechModelBtn_->setVisible(false);
        for (QToolButton* b : {markConnectMechModelBtn_, markNearMechModelBtn_, markFarMechModelBtn_,
                               markOutputMechModelBtn_, markScaleRefMechModelBtn_,
                               showMechModelMarksBtn_})
            if (b) b->setVisible(!schematicDrawing);
        // Which leaves the group with the MARKS in it - and a schematic drawing has none, so on a schematic
        // session there is nothing left for the group to hold and it goes.
        if (mechModelSessionGroup_) mechModelSessionGroup_->setVisible(!schematicDrawing);
        return;
    }
    // AUTO ROUTE IS A FUNCTION LIKE THE REST OF THEM: armed, it has the ribbon to itself - its own two
    // fields, and none of the tools that cannot be used until it is finished.
    const bool armed = editor_->tool() == ToolId::LengthBreak ||
                       Editor::isTwistedTool(editor_->tool()) ||
                       editor_->tool() == ToolId::AutoRoute;
    for (QWidget* g : mechGroups_)
        if (g && g != mechBreakGroup_) g->setVisible(!armed);
    // Special is the one group an armed tool does NOT put away - the tool being used is on it. But the
    // label branch above puts away everything, Special included, so it is turned back on here by name:
    // without this, fitting one label hid the button that places the next one.
    if (mechBreakGroup_) mechBreakGroup_->setVisible(true);
    // AND THE OTHER ICONS ON IT STAND DOWN while one of them is in use. The rule for this tab is that
    // a function in progress shows its own settings and nothing else - every other group obeys it, and
    // this was the one that did not: the two fields that matter appeared beside the whole row of tools
    // that cannot be used until this one is finished.
    //
    // INCLUDING THE ONE BEING USED. Its icon was kept at first, on the grounds that it says which
    // function the fields belong to - but it does not say that: it is the same icon whether the tool
    // is armed or not, and the fields beside it name themselves. What it did do was take a cell's
    // width from settings that have none to spare.
    for (QWidget* cell : mechSpecialCells_) {
        if (!cell) continue;
        // AND THE CELL CARRYING THE SETTINGS STAYS, whichever tool they belong to. The wires fields
        // live inside the Wires-harness button's cell but serve all three wires tools, so hiding
        // that cell while the straight run was armed took the fields away with it - the group came
        // out empty and invisible, which is the opposite of what this is for.
        const bool carriesSettings =
            (twistOptionsGroup_ && twistOptionsGroup_->isVisible() &&
             cell->isAncestorOf(twistOptionsGroup_)) ||
            (breakOptionsGroup_ && breakOptionsGroup_->isVisible() &&
             cell->isAncestorOf(breakOptionsGroup_)) ||
            // Asked of the group's OWN flag, not isVisible(): the cell around it may be hidden from
            // the last function, and isVisible() then says no for a group that is about to be shown.
            (autoRouteOptionsGroup_ && !autoRouteOptionsGroup_->isHidden() &&
             cell->isAncestorOf(autoRouteOptionsGroup_));
        cell->setVisible(!armed || carriesSettings);
        // The cell that carries the settings has to stay, but its BUTTON does not: that is the one
        // icon that was still on the ribbon beside the fields, kept alive by the cell around it.
        //
        // EXCEPT THE LITTLE A ON A FIELD, which is part of the field and not one of the icons this
        // is putting away. It is a tool button because that is what a two-letter button is made of,
        // and sweeping up every tool button in the cell swept up all eight of them - so on the
        // ribbon, where the fields are only ever seen with a tool armed, the A never appeared at
        // all. It is told apart by the cell it sits in; see autoBeside.
        for (QToolButton* b : cell->findChildren<QToolButton*>()) {
            if (b->parentWidget() &&
                b->parentWidget()->objectName() == QLatin1String("autoFieldCell"))
                continue;
            b->setVisible(!armed);
        }
    }
    if (mechModelSessionGroup_) mechModelSessionGroup_->setVisible(false);
}

// The label's two fields, seeded from what the stroke measured. Blocked while they are set, or the field
// being turned would write its own rounded value back into the label as a change of its own.
void MainWindow::syncLabelOptions() {
    if (!editor_) return;
    // Live at BOTH stages: a preview under the cursor and a label down on the drawing. Setting a label's
    // size only after it is placed means placing it at the wrong size first, which is why Tab pins the
    // preview - the two fields are meant to be used on it before the click, not only after.
    const bool usable = editor_->placingLabel();
    if (usable && labelWidthSpin_ && labelHeightSpin_) {
        QSignalBlocker bw(labelWidthSpin_);
        QSignalBlocker bh(labelHeightSpin_);
        labelWidthSpin_->setValue(editor_->labelWidth());
        labelHeightSpin_->setValue(editor_->labelHeight());
    }
    if (labelWidthSpin_) labelWidthSpin_->setEnabled(usable);
    if (labelHeightSpin_) labelHeightSpin_->setEnabled(usable);
    updateMechanicalRibbonGroups();
    if (labelFitGroup_) {
        // The Harness-mechanical tab is where the label button lives, so that is where its fields are; a
        // label placed from anywhere else still wants them, so the tab is brought forward.
        if (usable && ribbon_) {
            for (int i = 0; i < ribbon_->count(); ++i)
                if (ribbon_->tabText(i) == QStringLiteral("Harness mechanical") &&
                    ribbon_->tabBar()->isTabVisible(i))
                    ribbon_->setCurrentIndex(i);
        }
    }
}

void MainWindow::syncBreakOptions() {
    if (!editor_) return;
    // AUTO ROUTE's two settings, while it is armed, each showing the figure Auto comes to.
    if (autoRouteOptionsGroup_) {
        const bool routing = editor_->tool() == ToolId::AutoRoute;
        autoRouteOptionsGroup_->setVisible(routing);
        if (routing) {
            showAutoField(autoRouteFilletSpin_, nullptr, true, editor_->autoRouteFillet() < 0.0,
                          editor_->autoRouteFilletShown());
            showAutoField(autoRouteShiftSpin_, nullptr, true, editor_->autoRouteShift() < 0.0,
                          editor_->autoRouteShiftShown());
        }
    }
    // Each function on this tab shows its own settings and only while it is armed.
    if (twistOptionsGroup_) {
        // A FANOUT IS A STRAIGHT RUN - the fan of one, drawn on its own - so every field a straight
        // run offers is a field it offers. Left out of this list it was armed with an empty ribbon.
        const bool straightRun =
            editor_->tool() == ToolId::WiresRun || editor_->tool() == ToolId::Fanout;
        const bool twisting = editor_->tool() == ToolId::TwistedHarness ||
                              editor_->tool() == ToolId::TwistedHarnessRoute || straightRun;
        twistOptionsGroup_->setVisible(twisting);
        if (twisting && twistWiresSpin_) {
            QSignalBlocker b(twistWiresSpin_);
            twistWiresSpin_->setValue(editor_->twistWires());
        }
        // WHICH END FANS IS ASKED ONCE. Left / right / both names the ends as sides of the sheet;
        // Single end names them in draw order and has its own box for it. Showing both at once would
        // be two controls for one setting, and the left/right one cannot even say "start" - it used
        // to fall back to showing Both, which is the one answer that was certainly wrong.
        // A FANOUT IS THE FAN AND NOTHING ELSE. It is drawn as hook-up wires always - there is no
        // run to put a cable or a sleeve on - so the Body box goes away and what is left is every
        // fan setting and only those. Which end it fans at is the gesture, not a list: Shift says
        // whether the drawing starts at the spread wires or at the bundle.
        const bool fanout = editor_->tool() == ToolId::Fanout;
        if (twistBodyBox_) twistBodyBox_->setVisible(!fanout);

        // WHICH END FANS, asked once: the Spread list says it, and every one of its answers but
        // Both leaves the other end as body.
        const bool singleEnd = editor_->twistSingleEnd();
        if (twisting && twistSpreadCombo_) {
            QSignalBlocker b(twistSpreadCombo_);
            const int at = twistSpreadCombo_->findData(editor_->twistSpread());
            twistSpreadCombo_->setCurrentIndex(at >= 0 ? at : twistSpreadCombo_->count() - 1);
        }
        if (twistSpreadCombo_) twistSpreadCombo_->setVisible(twisting && !fanout);
        if (twistSpreadLabel_) twistSpreadLabel_->setVisible(twisting && !fanout);
        // WHAT A RUN DRAWN WITH THESE SETTINGS WOULD ACTUALLY COME OUT WITH, so a field left to the
        // drawing can show the figure the drawing worked out instead of the word "Auto". That is the
        // whole of what the little A says now: lit, this field is the drawing's own and the number
        // beside it is what the drawing made of it; dark, the number is one somebody typed.
        const TwistFigures fig = editor_->twistPreviewFigures();
        // WHICH FIELDS THIS SORT OF RUN CAN EVER SHOW, as against which it is showing now.
        //
        // A field hidden by a TICK keeps its room: ticking Flush ends off must not take the strip in
        // by the width of a field and shuffle everything to the right of it, because a ribbon is a
        // place on the screen you learn. A field the sort of run or body has no use for AT ALL gives
        // its room up, so a box is only as big as what can be in it - hook-up wires have no cover to
        // size and no boot to measure, and eight fields' worth of empty frame said they had.
        auto room = [](QWidget* w, bool keep) {
            if (!w) return;
            QSizePolicy sp = w->sizePolicy();
            if (sp.retainSizeWhenHidden() == keep) return;
            sp.setRetainSizeWhenHidden(keep);
            w->setSizePolicy(sp);
        };
        auto fieldRoom = [&room](QWidget* field, QLabel* label, bool keep) {
            room(autoFieldCell(field), keep);
            room(label, keep);
        };
        // The fan-out box belongs to a run that fans, and every field in it either stands for the
        // whole of that box or comes and goes with a tick inside it.
        fieldRoom(twistLaneSpin_, twistLaneLabel_, straightRun);
        fieldRoom(twistFanSpin_, twistFanLabel_, true);
        fieldRoom(twistFanKneeSpin_, twistFanKneeLabel_, straightRun);
        fieldRoom(twistFlushSpin_, twistFlushLabel_, straightRun);
        fieldRoom(twistStripSpin_, twistStripLabel_, straightRun);
        fieldRoom(twistLaneBSpin_, twistLaneBLabel_, straightRun);
        fieldRoom(twistFanBSpin_, twistFanBLabel_, straightRun);
        fieldRoom(twistFanKneeBSpin_, twistFanKneeBLabel_, straightRun);
        room(twistFlushCheck_, straightRun);
        room(twistStripCheck_, straightRun);
        // A FANOUT HAS ONE END. "Both sides same" is an answer about the other one, and there is
        // no other one - the tick would be a setting that cannot do anything.
        room(twistSidesCheck_, straightRun && !fanout);
        if (twistSidesCheck_) twistSidesCheck_->setVisible(straightRun && !fanout);
        // ONE END FANS: the tick belongs to the straight run, and the three under it come and go
        // with the tick - a run that fans at both ends has no body end to say anything about.
        // The wires' own pitch in the body is a HOOK-UP WIRES setting: under a cover the wires are
        // not drawn, so how far apart they run is not a thing you can see.
        const bool wiresBody = editor_->twistBodyKind() == RunBody::Wires;
        fieldRoom(twistBodyPitchSpin_, twistBodyPitchLabel_, straightRun && wiresBody);
        showAutoField(twistBodyPitchSpin_, twistBodyPitchLabel_, straightRun && wiresBody,
                      editor_->twistBodyPitch() <= 1e-9,
                      editor_->twistBodyPitch() > 1e-9 ? editor_->twistBodyPitch() : fig.packed);
        const bool single = straightRun && singleEnd;
        // WHAT THE BODY END CAN BE ASKED ABOUT DEPENDS ON WHAT THE BODY IS. A transition is a part
        // fitted over the end of a CABLE; a sleeve laid along the run is the cover itself and what
        // there is to say about its end is whether it has recovered onto what comes out. Neither
        // question means anything about bare wires, which have no end to finish.
        const RunBody bodyKind = editor_->twistBodyKind();
        const bool cableEnd = single && bodyKind == RunBody::Cable;
        const bool shrinkEnd = single && bodyKind == RunBody::Heatshrink;
        room(twistEndBootCombo_, cableEnd);
        room(twistEndBootFaceCombo_, shrinkEnd);

        if (twistEndBootCombo_) {
            twistEndBootCombo_->setVisible(cableEnd);
            QSignalBlocker b(twistEndBootCombo_);
            twistEndBootCombo_->setCurrentIndex(editor_->twistEndBoot() ? 1 : 0);
        }
        if (twistEndBootFaceCombo_) {
            twistEndBootFaceCombo_->setVisible(shrinkEnd);
            QSignalBlocker b(twistEndBootFaceCombo_);
            twistEndBootFaceCombo_->setCurrentIndex(editor_->twistEndBootRecovered() ? 0 : 1);
        }
        room(twistSpreadCombo_, straightRun);
        // The spacing is the straight run's own: a laid-up bundle spreads its wires across its width,
        // so the row would be a setting that does nothing while its tool is armed.
        // LANDED ON AN ENTRY FACE, the fan's spread is the face's until a pitch is typed here - so
        // that is the figure shown, lit as worked out, and a number typed over it is the one drawn.
        const double faceLane = editor_->faceFitLane(false);
        showAutoField(twistLaneSpin_, twistLaneLabel_, straightRun,
                      faceLane > 0.0 || editor_->twistLane() <= 1e-9,
                      faceLane > 0.0                ? faceLane
                      : editor_->twistLane() > 1e-9 ? editor_->twistLane()
                                                    : fig.lane);
        // The fan is on every wires tool; the wire width is the straight run's, because a laid-up
        // bundle takes its thickness from the bundle and not from one wire in it. The knee radius
        // belongs to the ROUTED tools: a run drawn between two points has no knee to turn at.
        const bool routed = editor_->tool() == ToolId::TwistedHarnessRoute ||
                            editor_->tool() == ToolId::WiresRun;
        showAutoField(twistKneeSpin_, twistKneeLabel_, routed, editor_->twistKnee() < 0.0,
                      editor_->twistKnee() >= 0.0 ? editor_->twistKnee() : fig.kneeRadius);
        showAutoField(twistFanSpin_, twistFanLabel_, true, editor_->twistFan() <= 1e-9,
                      editor_->twistFan() > 1e-9 ? editor_->twistFan() : fig.fanLen);
        // The fan's own corner radius is the straight run's, for the same reason the wire width is:
        // a laid-up bundle has no fan corners to round, its wires cross the whole way.
        showAutoField(twistFanKneeSpin_, twistFanKneeLabel_, straightRun,
                      editor_->twistFanKnee() < 0.0,
                      editor_->twistFanKnee() >= 0.0 ? editor_->twistFanKnee() : fig.fanKnee);
        // THE FINISH END'S OWN THREE, which are there only when it has any: ticked, the finish end
        // repeats the start and three fields showing the same figures twice would be three more
        // things to keep in step and nothing gained.
        // The flush end is the straight run's: a laid-up bundle does not fan, so it has no cut ends
        // to line up. The length only means something while there is one.
        if (twistFlushCheck_) {
            twistFlushCheck_->setVisible(straightRun);
            if (straightRun) {
                QSignalBlocker b(twistFlushCheck_);
                twistFlushCheck_->setChecked(editor_->twistFlush());
            }
        }
        showAutoField(twistFlushSpin_, twistFlushLabel_, straightRun && editor_->twistFlush(),
                      editor_->twistFlushLen() <= 1e-9,
                      editor_->twistFlushLen() > 1e-9 ? editor_->twistFlushLen() : fig.flushLen);
        // The stripped end is the straight run's, like the flush one: a laid-up bundle has no wire
        // ends of its own on show to strip.
        if (twistStripCheck_) {
            twistStripCheck_->setVisible(straightRun);
            if (straightRun) {
                QSignalBlocker b(twistStripCheck_);
                twistStripCheck_->setChecked(editor_->twistStrip());
            }
        }
        showAutoField(twistStripSpin_, twistStripLabel_, straightRun && editor_->twistStrip(),
                      editor_->twistStripLen() <= 1e-9,
                      editor_->twistStripLen() > 1e-9 ? editor_->twistStripLen() : fig.stripLen);
        if (twistBodyCombo_) {
            twistBodyCombo_->setVisible(straightRun);
            if (straightRun) {
                QSignalBlocker b(twistBodyCombo_);
                const int at =
                    twistBodyCombo_->findData(static_cast<int>(editor_->twistBodyKind()));
                twistBodyCombo_->setCurrentIndex(at >= 0 ? at : 0);
            }
        }
        // AND EACH SORT OF BODY BRINGS ITS OWN FIGURES, and only its own. A cable is cut square and
        // has a boot at each end; a length of tubing over the run is shrunk onto what it grips and
        // has none. Showing both sets at once would be offering half of them about nothing.
        const RunBody kind = editor_->twistBodyKind();
        const bool covered = straightRun && kind != RunBody::Wires;
        const bool asCable = straightRun && kind == RunBody::Cable;
        const bool asSleeve = straightRun && kind == RunBody::Heatshrink;
        const bool endsApart = covered && !editor_->twistBodySidesSame();
        fieldRoom(twistBodyWidthSpin_, twistBodyWidthLabel_, covered);
        fieldRoom(twistBodyFaceSpin_, twistBodyFaceLabel_, covered);
        fieldRoom(twistBodyShrinkSpin_, twistBodyShrinkLabel_, covered);
        fieldRoom(twistBodyFaceBSpin_, twistBodyFaceBLabel_, covered);
        fieldRoom(twistBodyShrinkBSpin_, twistBodyShrinkBLabel_, covered);
        fieldRoom(twistBootSpin_, twistBootLabel_, covered);
        fieldRoom(twistBootBSpin_, twistBootBLabel_, covered);
        room(twistBodySidesCheck_, covered);
        room(twistTransitionCombo_, asCable);
        room(twistTransitionLabel_, asCable);
        room(twistLayCombo_, straightRun && kind == RunBody::Wires);
        room(twistLayLabel_, straightRun && kind == RunBody::Wires);
        room(twistBodyOverCableCheck_, asSleeve);
        if (twistBodySidesCheck_) {
            twistBodySidesCheck_->setVisible(covered);
            if (covered) {
                QSignalBlocker b(twistBodySidesCheck_);
                twistBodySidesCheck_->setChecked(editor_->twistBodySidesSame());
            }
        }
        showAutoField(twistBodyWidthSpin_, twistBodyWidthLabel_, covered,
                      editor_->twistBodyWidth() <= 1e-9,
                      editor_->twistBodyWidth() > 1e-9 ? editor_->twistBodyWidth() : fig.bodyWidth);
        // THE TRANSITION IS A CABLE THING, and WHICH ENDS wear one is the question: a loom is not
        // symmetrical, and an end going into a connector wants the boot while one running into a
        // splice does not.
        const RunEnds transitionEnds = editor_->twistTransitionEnds();
        if (twistTransitionCombo_ && twistTransitionLabel_) {
            twistTransitionCombo_->setVisible(asCable);
            twistTransitionLabel_->setVisible(asCable);
            if (asCable) {
                QSignalBlocker b(twistTransitionCombo_);
                const int at = twistTransitionCombo_->findData(static_cast<int>(transitionEnds));
                twistTransitionCombo_->setCurrentIndex(at >= 0 ? at : 3);
            }
        }
        // AND HOW THE GATHERED WIRES RUN, which is a hook-up-wires thing and of no other: under a
        // cable or a sleeve there is nothing of the wires to see, laid up or not.
        const bool loose = straightRun && kind == RunBody::Wires;
        if (twistLayCombo_ && twistLayLabel_) {
            twistLayCombo_->setVisible(loose);
            twistLayLabel_->setVisible(loose);
            if (loose) {
                QSignalBlocker b(twistLayCombo_);
                const int at = twistLayCombo_->findData(static_cast<int>(editor_->twistLay()));
                twistLayCombo_->setCurrentIndex(at >= 0 ? at : 0);
            }
        }
        if (twistBodyOverCableCheck_) {
            twistBodyOverCableCheck_->setVisible(asSleeve);
            if (asSleeve) {
                QSignalBlocker b(twistBodyOverCableCheck_);
                twistBodyOverCableCheck_->setChecked(editor_->twistBodyOverCable());
            }
        }
        // WHICH END HAS ANYTHING TO SAY. A sleeve laid along the run is recovered at both of its
        // ends by the nature of the thing; a cable has whatever its transition gives it, which may
        // be one end, the other, both or neither. An end with no boot on it has no boot length and
        // no recovered face to set, so its figures stand down rather than sitting there describing
        // a part that is not on the drawing.
        const bool alongSleeve = asSleeve && !editor_->twistBodyOverCable();
        const bool bootStart = (asCable && runEndsHas(transitionEnds, false)) ||
                               (asSleeve && editor_->twistBodyOverCable());
        const bool bootFinish = (asCable && runEndsHas(transitionEnds, true)) ||
                                (asSleeve && editor_->twistBodyOverCable());
        const bool recoveredStart = alongSleeve || bootStart;
        const bool recoveredFinish = alongSleeve || bootFinish;
        // Told apart, each end answers for itself; told alike, the START fields answer for both -
        // so they show while EITHER end has something, and the finish ones never show at all.
        const bool startShown = endsApart ? recoveredStart : (recoveredStart || recoveredFinish);
        const bool bootShown = endsApart ? bootStart : (bootStart || bootFinish);
        showAutoField(twistBodyFaceSpin_, twistBodyFaceLabel_, startShown,
                      editor_->twistBodyFace() <= 1e-9,
                      editor_->twistBodyFace() > 1e-9 ? editor_->twistBodyFace() : fig.bodyFace);
        showAutoField(twistBodyShrinkSpin_, twistBodyShrinkLabel_, startShown,
                      editor_->twistBodyShrink() < 0.0,
                      editor_->twistBodyShrink() >= 0.0 ? editor_->twistBodyShrink()
                                                        : fig.bodyShrink);
        showAutoField(twistBodyFaceBSpin_, twistBodyFaceBLabel_, recoveredFinish && endsApart,
                      editor_->twistBodyFaceFinish() <= 1e-9,
                      editor_->twistBodyFaceFinish() > 1e-9 ? editor_->twistBodyFaceFinish()
                                                            : fig.bodyFace);
        showAutoField(twistBodyShrinkBSpin_, twistBodyShrinkBLabel_, recoveredFinish && endsApart,
                      editor_->twistBodyShrinkFinish() < 0.0,
                      editor_->twistBodyShrinkFinish() >= 0.0 ? editor_->twistBodyShrinkFinish()
                                                              : fig.bodyShrink);
        showAutoField(twistBootSpin_, twistBootLabel_, bootShown, editor_->twistBoot() <= 1e-9,
                      editor_->twistBoot() > 1e-9 ? editor_->twistBoot() : fig.boot);
        showAutoField(twistBootBSpin_, twistBootBLabel_, bootFinish && endsApart,
                      editor_->twistBootFinish() <= 1e-9,
                      editor_->twistBootFinish() > 1e-9 ? editor_->twistBootFinish() : fig.boot);
        if (twistSidesCheck_) {
            // ...and not on a fanout, which has one end. Set here as well as above, because this is
            // the later of the two and the one that decides.
            twistSidesCheck_->setVisible(straightRun && !fanout);
            if (straightRun) {
                QSignalBlocker b(twistSidesCheck_);
                twistSidesCheck_->setChecked(editor_->twistSidesSame());
            }
        }
        const bool apart = straightRun && !editor_->twistSidesSame();
        // The finish end's three, shown the same way: its own figure where it has one, and the
        // start's - which is what it falls back to - where it has not.
        const double faceLaneB = editor_->faceFitLane(true);
        showAutoField(twistLaneBSpin_, twistLaneBLabel_, apart,
                      faceLaneB > 0.0 || editor_->twistLaneFinish() <= 1e-9,
                      faceLaneB > 0.0                     ? faceLaneB
                      : editor_->twistLaneFinish() > 1e-9 ? editor_->twistLaneFinish()
                                                          : fig.lane);
        showAutoField(twistFanBSpin_, twistFanBLabel_, apart, editor_->twistFanFinish() <= 1e-9,
                      editor_->twistFanFinish() > 1e-9 ? editor_->twistFanFinish() : fig.fanLen);
        showAutoField(twistFanKneeBSpin_, twistFanKneeBLabel_, apart,
                      editor_->twistFanKneeFinish() < 0.0,
                      editor_->twistFanKneeFinish() >= 0.0 ? editor_->twistFanKneeFinish()
                                                           : fig.fanKnee);
        showAutoField(twistWireSpin_, twistWireLabel_, straightRun,
                      editor_->twistWireWidth() <= 1e-9,
                      editor_->twistWireWidth() > 1e-9 ? editor_->twistWireWidth()
                                                       : fig.wireWidth);
        // HOW MANY WIRES CARRY ON is a figure of the hook-up-wires body and of no other: it says how
        // many lines the stretch between the fans is drawn as, and a stretch covered by a cable or
        // by a length of tubing draws none of them at all. Offered against a cover it was a setting
        // that changed nothing on the sheet. The same `loose` the lay menu is shown by.
        fieldRoom(twistTrunkSpin_, twistTrunkLabel_, loose);
        if (twistTrunkSpin_ && twistTrunkLabel_) {
            autoFieldCell(twistTrunkSpin_)->setVisible(loose);
            twistTrunkLabel_->setVisible(loose);
            if (loose) {
                QSignalBlocker b(twistTrunkSpin_);
                // NEVER MORE THAN THE FAN HAS. A number above the wire count draws nothing extra -
                // there is nothing extra to draw - so it is not offered rather than accepted and
                // quietly ignored, which reads as a setting being disregarded.
                twistTrunkSpin_->setMaximum(std::max(2, editor_->twistWires()));
                twistTrunkSpin_->setValue(editor_->twistTrunk());
            }
        }
    }
    if (!breakOptionsGroup_) return;
    const bool armed = editor_->tool() == ToolId::LengthBreak;
    breakOptionsGroup_->setVisible(armed);
    updateMechanicalRibbonGroups();
    if (!armed) return;
    const bool curved = editor_->breakStyle() == QLatin1String("curved");
    if (breakSizeLabel_)
        breakSizeLabel_->setText(curved ? QStringLiteral("Size") : QStringLiteral("Lines length"));
    // EACH FIELD SHOWS WHAT IS GOING TO BE DRAWN. Left to the drawing, that is the figure the drawing
    // has worked out - off the mark being made where a line has been drawn across something, and off
    // the same sums with nothing crossed yet where it has not - with the A lit to say so. Typed into,
    // it is the figure that was typed and the A goes dark. The word "auto" said neither.
    const Editor::BreakFigures bf = editor_->breakPreviewFigures();
    showAutoField(breakGapSpin_, breakGapLabel_, true, editor_->breakGap() <= 1e-6,
                  editor_->breakGap() > 1e-6 ? editor_->breakGap() : bf.gap);
    showAutoField(breakSizeSpin_, breakSizeLabel_, true, editor_->breakSize() <= 1e-6,
                  editor_->breakSize() > 1e-6 ? editor_->breakSize() : bf.size);
    showAutoField(breakAngleSpin_, breakAngleLabel_, true, editor_->breakAngle() < 0.0,
                  editor_->breakAngle() >= 0.0 ? editor_->breakAngle() : bf.angleDeg);
}

void MainWindow::updateSchematicRibbonGroups() {
    if (!editor_) return;
    // Drawing a default model's SYMBOL: this tab is where its pieces come from, and of its groups only Place
    // and Modify are about the glyph. The design-rule check reads a whole schematic - what is joined to what,
    // what is left floating - and a symbol on its own is nothing but floating ends: it would report a fault
    // per pin, every one of them wrong.
    const bool schematicDefaultModel = doc_ && mechModelEditSchematic_.value(doc_, false) &&
                                       !libraryPartEdits_.contains(doc_);
    const bool twistArmed = !editor_->twistArmType().isEmpty();
    const ToolId t = editor_->tool();
    const bool markerArmed = (t == ToolId::Splice || t == ToolId::NotConnected);
    // Twist takes over the whole ribbon while armed.
    if (schemTwistGroup_) schemTwistGroup_->setVisible(twistArmed);
    if (schemTwistRule_) schemTwistRule_->setVisible(twistArmed);
    if (schemPlaceGroup_) schemPlaceGroup_->setVisible(!twistArmed);
    if (schemPlaceRule_) schemPlaceRule_->setVisible(!twistArmed);
    if (markerGroup_) markerGroup_->setVisible(markerArmed && !twistArmed);
    const bool showModDrc = !twistArmed && !markerArmed;
    if (schemModifyGroup_) schemModifyGroup_->setVisible(showModDrc);
    if (schemModifyRule_) schemModifyRule_->setVisible(showModDrc);
    if (schemDrcGroup_) schemDrcGroup_->setVisible(showModDrc && !schematicDefaultModel);
    if (schemDrcRule_) schemDrcRule_->setVisible(showModDrc && !schematicDefaultModel);
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
        rule->setObjectName(QStringLiteral("ribbonGroupRule"));
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
        for (const auto& e : editDoc()->entities())
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
        for (const auto& e : editDoc()->entities())
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
        for (const auto& e : editDoc()->entities())
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


// ---- the detail view's own ribbon tab -----------------------------------------------------------------
// Clicking a detail view brings up everything about it in one place. Built once and parented to the ribbon,
// inserted as a visible tab only while a detail view is selected or the tool is armed - the same way the
// hatch and table-cell tabs come and go, because a tab that is always there for a thing that is usually not
// selected is a tab in the way.
void MainWindow::buildDetailRibbonTab() {
    detailTab_ = new QWidget(ribbon_);
    auto* lay = new QHBoxLayout(detailTab_);
    // TIGHT. Eleven fields over four groups is a tab you read across rather than glance at, and the space between
    // them was doing nothing but making it wider.
    lay->setContentsMargins(4, 2, 4, 1);
    lay->setSpacing(0);
    lay->setAlignment(Qt::AlignLeft);
    auto addGroup = [&](const QString& title, QWidget* content) {
        lay->addWidget(ribbonGroup(title, content));
        auto* rule = new QFrame(detailTab_);
        rule->setFrameShape(QFrame::VLine);
        rule->setObjectName(QStringLiteral("ribbonGroupRule"));
        lay->addWidget(rule);
    };
    auto write = [this](const QString& key, const QJsonValue& v) {
        if (detailSyncing_ || detailEditId_.isEmpty() || !doc_) return;
        for (Entity& e : editDoc()->entities()) {
            if (e.id != detailEditId_) continue;
            e.meta.insert(key, v);
            editDoc()->setDirty(true);
            // Made associative or not, or shown more or less: what a view that is NOT associative shows is a
            // COPY made of real objects, so it is built, rebuilt or taken away to match.
            editor_->syncDetailCopies();
            canvas_->invalidateScene();
            canvas_->update();
            refreshPanels();
            return;
        }
    };
    auto spin = [&](QWidget* parent, QGridLayout* g, int row, const QString& caption, double lo, double hi,
                    double step, const QString& tip, QDoubleSpinBox** out) {
        auto* l = new QLabel(caption, parent);
        l->setStyleSheet(QStringLiteral("color:#e6e9ef;background:transparent;"));
        l->setToolTip(tip);
        auto* s = new QDoubleSpinBox(parent);
        s->setRange(lo, hi);
        s->setSingleStep(step);
        s->setDecimals(1);
        s->setMinimumWidth(64);
        s->setKeyboardTracking(false);
        s->setToolTip(tip);
        g->addWidget(l, row, 0);
        g->addWidget(s, row, 1);
        *out = s;
        return s;
    };

    // WHAT IS WRITTEN, and on which circle. Two texts, because they say different things: the one on the
    // circle round the thing is a pointer and the one on the enlargement is a heading. "DETAIL A" beside A is
    // the convention, so the heading defaults to that and can be anything an office writes instead.
    {
        auto* w = new QWidget(detailTab_);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(3, 0, 3, 0);
        g->setSpacing(1);
        auto row = [&](int r, const QString& caption, const QString& tip, QLineEdit** out) {
            auto* l = new QLabel(caption, w);
            l->setStyleSheet(QStringLiteral("color:#e6e9ef;background:transparent;"));
            l->setToolTip(tip);
            auto* le = new QLineEdit(w);
            le->setMinimumWidth(96);
            le->setToolTip(tip);
            g->addWidget(l, r, 0);
            g->addWidget(le, r, 1);
            *out = le;
        };
        row(0, tr("Identifier"),
            tr("The letter that ties the two circles together — A, B, C. Both labels are built from it, and "
               "the next detail view on the sheet takes the next free letter."),
            &detailLetter_);
        row(1, tr("Source label"),
            tr("The label beside the boundary on the parent view. Both labels read \"Detail A\" unless "
               "something else is typed: a bare letter on a sheet has to be worked out — it could be a note, "
               "a zone, a revision — and the two reading the same ties them together at a glance."),
            &detailSmallText_);
        row(2, tr("View label"),
            tr("The label on the detail view itself. Left empty it reads \"Detail\" and the identifier, "
               "which is the convention."),
            &detailBigText_);
        connect(detailSmallText_, &QLineEdit::textEdited, this, [write](const QString& t) {
            write(QStringLiteral("detailTagText"), t.trimmed());
        });
        connect(detailBigText_, &QLineEdit::textEdited, this, [write](const QString& t) {
            write(QStringLiteral("detailBigText"), t.trimmed());
        });
        connect(detailLetter_, &QLineEdit::textEdited, this, [write](const QString& t) {
            write(QStringLiteral("detailTag"), t.trimmed().toUpper());
        });
        // HOW IT IS WRITTEN. A drawing office's details have to read like the rest of its sheet, and a
        // caption at whatever height the program felt like is the one thing that says they do not.
        {
            auto* l = new QLabel(tr("Text height"), w);
            l->setStyleSheet(QStringLiteral("color:#e6e9ef;background:transparent;"));
            detailTextSizeSpin_ = new QDoubleSpinBox(w);
            detailTextSizeSpin_->setRange(0.1, 500.0);
            detailTextSizeSpin_->setSingleStep(0.5);
            detailTextSizeSpin_->setDecimals(1);
            detailTextSizeSpin_->setKeyboardTracking(false);
            detailTextSizeSpin_->setToolTip(
                tr("How tall the words are, in drawing units. The scale line under the heading is drawn a "
                   "little smaller than this, as it is on a drawn sheet."));
            g->addWidget(l, 3, 0);
            g->addWidget(detailTextSizeSpin_, 3, 1);
            connect(detailTextSizeSpin_, &QDoubleSpinBox::valueChanged, this,
                    [write](double v) { write(QStringLiteral("detailTextSize"), std::max(0.1, v)); });
            auto* fl = new QLabel(tr("Font"), w);
            fl->setStyleSheet(QStringLiteral("color:#e6e9ef;background:transparent;"));
            detailFontCombo_ = new QComboBox(w);
            detailFontCombo_->setEditable(true);
            detailFontCombo_->addItem(tr("Default"));
            detailFontCombo_->addItems(QFontDatabase::families());
            detailFontCombo_->setMinimumWidth(104);
            detailFontCombo_->setToolTip(tr("The face the words are set in. Default follows the drawing."));
            g->addWidget(fl, 4, 0);
            g->addWidget(detailFontCombo_, 4, 1);
            connect(detailFontCombo_, &QComboBox::currentTextChanged, this, [write](const QString& f) {
                write(QStringLiteral("detailFont"), f == QStringLiteral("Default") ? QString() : f);
            });
        }
        addGroup(tr("Identification"), w);
    }

    // HOW MUCH, and HOW BIG.
    {
        auto* w = new QWidget(detailTab_);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(3, 0, 3, 0);
        g->setSpacing(1);
        spin(w, g, 0, tr("Boundary radius"), 0.1, 100000.0, 1.0,
             tr("The radius of the detail boundary on the parent view — how much of the drawing this view "
                "is of."),
             &detailShowsSpin_);
        spin(w, g, 1, tr("Scale factor"), 0.1, 200.0, 0.5,
             tr("How many times the detail view is enlarged. Two is the usual: enough to read, and small "
                "enough to fit on the sheet beside the view it came from."),
             &detailScaleSpin_);
        connect(detailShowsSpin_, &QDoubleSpinBox::valueChanged, this, [this](double v) {
            if (detailSyncing_ || detailEditId_.isEmpty() || !doc_) return;
            for (Entity& e : editDoc()->entities())
                if (e.id == detailEditId_) {
                    e.radius = std::max(1e-6, v);
                    editor_->clearDetailCopy(e.id);   // it shows more, or less: the copy is made again
                    editor_->materialiseDetailCopy(e);
                    editDoc()->setDirty(true);
                    canvas_->invalidateScene();
                    canvas_->update();
                    refreshPanels();
                    return;
                }
        });
        connect(detailScaleSpin_, &QDoubleSpinBox::valueChanged, this,
                [write](double v) { write(QStringLiteral("detailScale"), std::max(0.05, v)); });
        addGroup(tr("Scale"), w);
    }

    // WHERE THE WORDS SIT. A letter fixed at one o'clock lands on the geometry about half the time, and a
    // sheet with several details on it cannot be arranged at all if none of the letters can be moved off
    // whatever they are sitting on. So each text runs round its own circle, at its own distance from it.
    {
        auto* w = new QWidget(detailTab_);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(3, 0, 3, 0);
        g->setSpacing(1);
        spin(w, g, 0, tr("Source label angle"), -360.0, 360.0, 15.0,
             tr("Where the source label sits round the boundary, in degrees: 0 is to the right, 90 straight "
                "up. Move it clear of the geometry it landed on."),
             &detailTagAngleSpin_);
        spin(w, g, 1, tr("Source label offset"), 0.0, 1000.0, 0.5,
             tr("How far the source label stands off the boundary."), &detailTagGapSpin_);
        spin(w, g, 2, tr("View label angle"), -360.0, 360.0, 15.0,
             tr("Where the view label sits round the detail view, in degrees. Directly below (-90) is the "
                "convention."),
             &detailBigAngleSpin_);
        spin(w, g, 3, tr("View label offset"), 0.0, 1000.0, 0.5,
             tr("How far the view label stands off the detail view."), &detailBigGapSpin_);
        connect(detailTagAngleSpin_, &QDoubleSpinBox::valueChanged, this,
                [write](double v) { write(QStringLiteral("detailTagAngle"), v); });
        connect(detailTagGapSpin_, &QDoubleSpinBox::valueChanged, this,
                [write](double v) { write(QStringLiteral("detailTagGap"), std::max(0.0, v)); });
        connect(detailBigAngleSpin_, &QDoubleSpinBox::valueChanged, this,
                [write](double v) { write(QStringLiteral("detailBigAngle"), v); });
        connect(detailBigGapSpin_, &QDoubleSpinBox::valueChanged, this,
                [write](double v) { write(QStringLiteral("detailBigGap"), std::max(0.0, v)); });
        addGroup(tr("Label placement"), w);
    }

    // THE THREE SWITCHES.
    {
        auto* w = new QWidget(detailTab_);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(3, 0, 3, 0);
        g->setSpacing(1);
        auto box = [&](int row, const QString& caption, const QString& tip, QCheckBox** out) {
            auto* c = new QCheckBox(caption, w);
            c->setStyleSheet(QStringLiteral("color:#e6e9ef;background:transparent;"));
            c->setToolTip(tip);
            c->setFocusPolicy(Qt::NoFocus);
            g->addWidget(c, row, 0);
            *out = c;
        };
        box(0, tr("Show view boundary"),
            tr("The circle drawn round the detail view. It is the convention and it says where the view "
               "stops — but a detail in a clear part of the sheet reads perfectly well without one, and on "
               "a busy sheet it is one more circle to be mistaken for a hole."),
            &detailBoundaryBox_);
        box(1, tr("Associative"),
            tr("Keeps the detail view tied to the parent geometry, so a change to either shows in both. "
               "Cleared, the view is a static copy taken when it was placed — but it can still be drawn and "
               "edited in either way, and a static view is brought up to date by whatever is drawn through "
               "it. A detail view is made BECAUSE the geometry is too small to work on."),
            &detailAnnotativeBox_);
        box(2, tr("Include annotations"),
            tr("Whether the DIMENSIONS inside the boundary are shown in the view, enlarged with everything "
               "else. A detail view is usually made to show a SHAPE, and a dimension sized for the parent "
               "view becomes the largest thing in the detail and covers what it was measuring — but a view "
               "made BECAUSE the dimensions would not fit at parent scale needs them. Labels and text come "
               "with the view either way: the marking on a thing is part of what the thing is."),
            &detailDimsBox_);
        connect(detailBoundaryBox_, &QCheckBox::toggled, this,
                [write](bool on) { write(QStringLiteral("detailBoundary"), on); });
        connect(detailAnnotativeBox_, &QCheckBox::toggled, this,
                [write](bool on) { write(QStringLiteral("detailAnnotative"), on); });
        connect(detailDimsBox_, &QCheckBox::toggled, this,
                [write](bool on) { write(QStringLiteral("detailShowDims"), on); });
        addGroup(tr("Display"), w);
    }
    lay->addStretch(1);
    // Out of sight until a detail view is selected. It is a child of the ribbon but not a tab of it, and a
    // contextual page left showing floats over whichever page IS showing.
    detailTab_->hide();
}

// Which detail view the tab is pointed at: the one selected, or none. Unlike the hatch tab there is nothing
// to live-edit while the tool is merely armed - a detail view does not exist until its third click - so the
// tab comes up when one is selected and goes when it is not.
void MainWindow::updateDetailContext() {
    QString sel;
    if (doc_ && editor_->selection().size() == 1) {
        const QString id = editor_->selection().front();
        for (const auto& e : editDoc()->entities())
            if (e.id == id && e.type == EntityType::DetailView) { sel = id; break; }
    }
    // ARMING THE TOOL brings the tab up too, and it STAYS while the tool is armed. Pressing the button and
    // getting nothing until after the third click means finding out how the settings read only once the view
    // is already down - and the tab going away again the instant the tool is used would be worse, because
    // placing several details is the ordinary case and the settings are the same for all of them.
    //
    // With nothing selected the tab is pointed at the LAST detail placed, so the settings act on the one just
    // put down; before there is one, the fields simply have nothing to write to.
    const bool arming = editor_->tool() == ToolId::DetailView;
    if (sel.isEmpty() && arming && doc_)
        for (const auto& e : editDoc()->entities())
            if (e.type == EntityType::DetailView) sel = e.id;   // the last one in the drawing
    detailEditId_ = sel;
    showDetailRibbon(!sel.isEmpty() || arming);
}

void MainWindow::showDetailRibbon(bool on) {
    if (!detailTab_ || !ribbon_) return;
    const int idx = ribbon_->indexOf(detailTab_);
    if (on) {
        syncDetailRibbon();
        if (idx < 0) {
            const int at = ribbon_->addTab(detailTab_, tr("Detail view"));
            ribbon_->setCurrentIndex(at);
        } else {
            ribbon_->setCurrentIndex(idx);
        }
        detailTab_->show();
    } else if (idx >= 0) {
        ribbon_->removeTab(idx);   // detaches but does not delete detailTab_
        ribbon_->setCurrentIndex(0);
    }
}

// The fields, from the detail view they are pointed at. Guarded, or writing them back would read them
// straight out again and fight whoever is typing.
void MainWindow::syncDetailRibbon() {
    // Armed but nothing placed yet: the fields are there, showing what a new one will look like, and dead
    // until there is something to change. A row of live-looking fields that write to nothing is worse.
    const bool haveOne = !detailEditId_.isEmpty();
    for (QWidget* w : {static_cast<QWidget*>(detailSmallText_), static_cast<QWidget*>(detailBigText_),
                       static_cast<QWidget*>(detailScaleSpin_), static_cast<QWidget*>(detailShowsSpin_),
                       static_cast<QWidget*>(detailTagAngleSpin_),
                       static_cast<QWidget*>(detailTagGapSpin_),
                       static_cast<QWidget*>(detailBigAngleSpin_),
                       static_cast<QWidget*>(detailBigGapSpin_),
                       static_cast<QWidget*>(detailBoundaryBox_),
                       static_cast<QWidget*>(detailAnnotativeBox_),
                       static_cast<QWidget*>(detailDimsBox_), static_cast<QWidget*>(detailFontCombo_),
                       static_cast<QWidget*>(detailTextSizeSpin_),
                       static_cast<QWidget*>(detailLetter_)})
        if (w) w->setEnabled(haveOne);
    if (!haveOne || !doc_) return;
    const Entity* dv = nullptr;
    for (const auto& e : editDoc()->entities())
        if (e.id == detailEditId_ && e.type == EntityType::DetailView) { dv = &e; break; }
    if (!dv) return;
    detailSyncing_ = true;
    const double capH =
        std::max(1e-9, dv->meta.value(QStringLiteral("detailTextSize")).toDouble(5.0));
    const QString letter = dv->meta.value(QStringLiteral("detailTag")).toString(QStringLiteral("A"));
    const QString both = QStringLiteral("Detail %1").arg(letter);
    if (detailSmallText_)
        detailSmallText_->setText(dv->meta.value(QStringLiteral("detailTagText")).toString(both));
    if (detailBigText_)
        detailBigText_->setText(dv->meta.value(QStringLiteral("detailBigText")).toString(both));
    if (detailLetter_) detailLetter_->setText(letter);
    if (detailTextSizeSpin_) detailTextSizeSpin_->setValue(capH);
    if (detailFontCombo_) {
        const QString f = dv->meta.value(QStringLiteral("detailFont")).toString();
        detailFontCombo_->setCurrentText(f.isEmpty() ? tr("Default") : f);
    }
    if (detailShowsSpin_) detailShowsSpin_->setValue(dv->radius);
    if (detailScaleSpin_)
        detailScaleSpin_->setValue(dv->meta.value(QStringLiteral("detailScale")).toDouble(2.0));
    if (detailTagAngleSpin_)
        detailTagAngleSpin_->setValue(dv->meta.value(QStringLiteral("detailTagAngle")).toDouble(90.0));
    if (detailTagGapSpin_)
        detailTagGapSpin_->setValue(
            dv->meta.value(QStringLiteral("detailTagGap")).toDouble(capH * 0.6));
    if (detailBigAngleSpin_)
        detailBigAngleSpin_->setValue(dv->meta.value(QStringLiteral("detailBigAngle")).toDouble(-90.0));
    if (detailBigGapSpin_)
        detailBigGapSpin_->setValue(
            dv->meta.value(QStringLiteral("detailBigGap")).toDouble(capH * 1.1));
    if (detailBoundaryBox_)
        detailBoundaryBox_->setChecked(dv->meta.value(QStringLiteral("detailBoundary")).toBool(true));
    if (detailAnnotativeBox_)
        detailAnnotativeBox_->setChecked(
            dv->meta.value(QStringLiteral("detailAnnotative")).toBool(false));
    if (detailDimsBox_)
        detailDimsBox_->setChecked(dv->meta.value(QStringLiteral("detailShowDims")).toBool(false));
    detailSyncing_ = false;
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
        rule->setObjectName(QStringLiteral("ribbonGroupRule"));
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

    // ---- Cells: Join / Split ----
    {
        auto* w = new QWidget(tableCellTab_);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0);
        g->setSpacing(1);
        auto* join = bigButton(w, QStringLiteral("Join\nCells"));
        auto* split = bigButton(w, QStringLiteral("Split\nCell"));
        join->setToolTip(QStringLiteral(
            "Make the picked cells into one cell. Drag across the cells first; the words of the first "
            "one that has any become the words of the joined cell."));
        split->setToolTip(QStringLiteral("Break a joined cell back into the cells it was made of."));
        connect(join, &QToolButton::clicked, this, [this] { editor_->tableMergeCells(); });
        connect(split, &QToolButton::clicked, this, [this] { editor_->tableSplitCell(); });
        g->addWidget(join, 0, 0);
        g->addWidget(split, 0, 1);
        addGroup(QStringLiteral("Cells"), w);
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

// The contextual "Array Creation" ribbon tab (AutoCAD-style): Type | Columns/Items |
// Rows | Properties | Close. Built once and parented to the ribbon, inserted as a
// visible tab only while an array session is live (showArrayRibbon). Groups carry the
// kinds they belong to, so switching Type swaps the parameters in place.
void MainWindow::buildArrayRibbonTab() {
    arrayTab_ = new QWidget(ribbon_);
    auto* lay = new QHBoxLayout(arrayTab_);
    lay->setContentsMargins(6, 4, 6, 2);
    lay->setSpacing(0);
    lay->setAlignment(Qt::AlignLeft);
    arrayGroups_.clear();

    // A group plus its separator rule, remembered with the kinds it applies to.
    auto addGroup = [&](const QString& title, QWidget* content, bool rect, bool polar, bool path) {
        auto* group = ribbonGroup(title, content);
        lay->addWidget(group);
        auto* rule = new QFrame(arrayTab_);
        rule->setFrameShape(QFrame::VLine);
        rule->setObjectName(QStringLiteral("ribbonGroupRule"));
        lay->addWidget(rule);
        arrayGroups_.append({group, rule, rect, polar, path});
    };
    auto label = [&](QWidget* parent, const QString& t) {
        auto* l = new QLabel(t, parent);
        l->setStyleSheet(QStringLiteral("color:#e6e9ef;background:transparent;"));
        return l;
    };
    // Value fields commit on Enter / focus-out / arrows (not per keystroke), so a
    // half-typed number never re-previews the array.
    auto spin = [&](QWidget* parent, double lo, double hi, int dec) {
        auto* s = new QDoubleSpinBox(parent);
        s->setRange(lo, hi);
        s->setDecimals(dec);
        s->setKeyboardTracking(false);
        s->setButtonSymbols(QAbstractSpinBox::NoButtons);
        s->setMinimumWidth(62);
        return s;
    };
    auto countSpin = [&](QWidget* parent) {
        auto* s = new QSpinBox(parent);
        s->setRange(1, 1000);
        s->setKeyboardTracking(false);
        s->setButtonSymbols(QAbstractSpinBox::PlusMinus);
        s->setMinimumWidth(56);
        return s;
    };
    auto bigButton = [&](QWidget* parent, const QString& text) {
        auto* b = new QToolButton(parent);
        b->setText(text);
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setIconSize(QSize(26, 26));
        b->setMinimumWidth(64);
        b->setAutoRaise(true);
        return b;
    };
    // Every control edits editor_->arraySpec() then asks for a repaint. The guard stops
    // syncArrayRibbon_ (which writes the widgets) from being taken for a user edit.
    auto apply = [this](const std::function<void(ArraySpec&)>& fn) {
        if (arraySyncing_ || !editor_->arrayActive()) return;
        fn(editor_->arraySpec());
        editor_->arrayParamsChanged();
        if (syncArrayRibbon_) syncArrayRibbon_(); // derived fields (Total / Between) re-read
    };

    // ---- Type: the three array laws ----
    auto* typeRect = static_cast<QToolButton*>(nullptr);
    auto* typePolar = static_cast<QToolButton*>(nullptr);
    auto* typePath = static_cast<QToolButton*>(nullptr);
    {
        auto* w = new QWidget(arrayTab_);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0);
        g->setSpacing(1);
        auto typeBtn = [&](const QString& text, const QString& icon, const QString& tip,
                           ArrayKind kind) {
            auto* b = bigButton(w, text);
            b->setIcon(makeOpIcon(icon));
            b->setCheckable(true);
            b->setToolTip(tip);
            connect(b, &QToolButton::clicked, this, [this, kind] {
                if (arraySyncing_) return;
                editor_->setArrayKind(kind); // re-seeds that kind's defaults, re-previews
                canvas_->setFocus(Qt::OtherFocusReason);
            });
            return b;
        };
        typeRect = typeBtn(QStringLiteral("Rect"), QStringLiteral("array"),
                           QStringLiteral("Rectangular (linear) array: columns x rows"),
                           ArrayKind::Rect);
        typePolar = typeBtn(QStringLiteral("Polar"), QStringLiteral("arraypolar"),
                            QStringLiteral("Polar array: items swept around a centre point"),
                            ArrayKind::Polar);
        typePath = typeBtn(QStringLiteral("Path"), QStringLiteral("arraypath"),
                           QStringLiteral("Path array: items track along a picked object"),
                           ArrayKind::Path);
        g->addWidget(typeRect, 0, 0);
        g->addWidget(typePolar, 0, 1);
        g->addWidget(typePath, 0, 2);
        addGroup(QStringLiteral("Type"), w, true, true, true);
    }

    // ---- Columns (rectangular): count + spacing, with the derived total ----
    auto* colCount = static_cast<QSpinBox*>(nullptr);
    auto* colBetween = static_cast<QDoubleSpinBox*>(nullptr);
    auto* colTotal = static_cast<QDoubleSpinBox*>(nullptr);
    {
        auto* w = new QWidget(arrayTab_);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0);
        g->setHorizontalSpacing(4);
        g->setVerticalSpacing(3);
        colCount = countSpin(w);
        colCount->setToolTip(QStringLiteral("Number of columns"));
        colBetween = spin(w, -1e6, 1e6, 2);
        colBetween->setToolTip(QStringLiteral("Distance between columns (negative goes left)"));
        colTotal = spin(w, -1e6, 1e6, 2);
        colTotal->setToolTip(QStringLiteral("Distance from the first to the last column"));
        connect(colCount, &QSpinBox::valueChanged, this,
                [apply](int v) { apply([v](ArraySpec& s) { s.count = v; }); });
        connect(colBetween, &QDoubleSpinBox::valueChanged, this,
                [apply](double v) { apply([v](ArraySpec& s) { s.colSpacing = v; }); });
        // Total spans first-to-last, so it divides by the gaps, not the items.
        connect(colTotal, &QDoubleSpinBox::valueChanged, this, [apply](double v) {
            apply([v](ArraySpec& s) {
                if (s.count > 1) s.colSpacing = v / (s.count - 1);
            });
        });
        g->addWidget(label(w, QStringLiteral("Columns")), 0, 0);
        g->addWidget(colCount, 0, 1);
        g->addWidget(label(w, QStringLiteral("Between")), 1, 0);
        g->addWidget(colBetween, 1, 1);
        g->addWidget(label(w, QStringLiteral("Total")), 2, 0);
        g->addWidget(colTotal, 2, 1);
        addGroup(QStringLiteral("Columns"), w, true, false, false);
    }

    // ---- Items (polar): count + angles ----
    auto* polCount = static_cast<QSpinBox*>(nullptr);
    auto* polBetween = static_cast<QDoubleSpinBox*>(nullptr);
    auto* polFill = static_cast<QDoubleSpinBox*>(nullptr);
    {
        auto* w = new QWidget(arrayTab_);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0);
        g->setHorizontalSpacing(4);
        g->setVerticalSpacing(3);
        polCount = countSpin(w);
        polCount->setToolTip(QStringLiteral("Number of items around the centre"));
        polBetween = spin(w, -360.0, 360.0, 2);
        polBetween->setToolTip(QStringLiteral("Angle between two neighbouring items"));
        polFill = spin(w, -360.0, 360.0, 2);
        polFill->setToolTip(QStringLiteral("Total angle the items are swept through "
                                           "(360 = all the way round)"));
        connect(polCount, &QSpinBox::valueChanged, this,
                [apply](int v) { apply([v](ArraySpec& s) { s.count = v; }); });
        // Typing an angle BETWEEN items sets the fill angle that produces it. A full
        // circle divides by the item count (no item doubled at 0/360) and a partial
        // sweep spans it end to end, so the fill needed differs — and past 360 the
        // sweep simply saturates at a full circle.
        connect(polBetween, &QDoubleSpinBox::valueChanged, this, [apply](double v) {
            apply([v](ArraySpec& s) {
                const double span = v * std::max(0, s.count - 1);
                s.fillAngle = (std::abs(span) >= 360.0) ? (span < 0 ? -360.0 : 360.0) : span;
            });
        });
        connect(polFill, &QDoubleSpinBox::valueChanged, this,
                [apply](double v) { apply([v](ArraySpec& s) { s.fillAngle = v; }); });
        g->addWidget(label(w, QStringLiteral("Items")), 0, 0);
        g->addWidget(polCount, 0, 1);
        g->addWidget(label(w, QStringLiteral("Between°")), 1, 0);
        g->addWidget(polBetween, 1, 1);
        g->addWidget(label(w, QStringLiteral("Fill°")), 2, 0);
        g->addWidget(polFill, 2, 1);
        addGroup(QStringLiteral("Items"), w, false, true, false);
    }

    // ---- Items (path): method + count + spacing ----
    auto* pathMethod = static_cast<QComboBox*>(nullptr);
    auto* pathCount = static_cast<QSpinBox*>(nullptr);
    auto* pathBetween = static_cast<QDoubleSpinBox*>(nullptr);
    auto* pathTotal = static_cast<QDoubleSpinBox*>(nullptr);
    {
        auto* w = new QWidget(arrayTab_);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0);
        g->setHorizontalSpacing(4);
        g->setVerticalSpacing(3);
        pathMethod = new QComboBox(w);
        pathMethod->addItems({QStringLiteral("Divide"), QStringLiteral("Measure")});
        pathMethod->setMinimumWidth(78);
        pathMethod->setToolTip(QStringLiteral(
            "Divide: spread the items evenly over the whole path.\n"
            "Measure: step along the path by a fixed distance."));
        pathCount = countSpin(w);
        pathCount->setToolTip(QStringLiteral("Number of items along the path"));
        pathBetween = spin(w, -1e6, 1e6, 2);
        pathBetween->setToolTip(QStringLiteral("Distance along the path between two items. In "
                                              "Divide mode this shows what the path length "
                                              "gives; typing a value switches to Measure."));
        pathTotal = spin(w, -1e6, 1e6, 2);
        pathTotal->setToolTip(QStringLiteral("Distance along the path from the first to the "
                                             "last item"));
        connect(pathMethod, &QComboBox::currentIndexChanged, this, [apply](int idx) {
            apply([idx](ArraySpec& s) { s.divide = (idx == 0); });
        });
        connect(pathCount, &QSpinBox::valueChanged, this,
                [apply](int v) { apply([v](ArraySpec& s) { s.count = v; }); });
        // Setting a distance is the Measure method by definition, so it switches over.
        connect(pathBetween, &QDoubleSpinBox::valueChanged, this, [apply](double v) {
            apply([v](ArraySpec& s) {
                s.pathSpacing = v;
                s.divide = false;
            });
        });
        connect(pathTotal, &QDoubleSpinBox::valueChanged, this, [apply](double v) {
            apply([v](ArraySpec& s) {
                if (s.count > 1) {
                    s.pathSpacing = v / (s.count - 1);
                    s.divide = false;
                }
            });
        });
        g->addWidget(label(w, QStringLiteral("Method")), 0, 0);
        g->addWidget(pathMethod, 0, 1);
        g->addWidget(label(w, QStringLiteral("Items")), 1, 0);
        g->addWidget(pathCount, 1, 1);
        g->addWidget(label(w, QStringLiteral("Between")), 2, 0);
        g->addWidget(pathBetween, 2, 1);
        g->addWidget(label(w, QStringLiteral("Total")), 3, 0);
        g->addWidget(pathTotal, 3, 1);
        addGroup(QStringLiteral("Items"), w, false, false, true);
    }

    // ---- Rows: shared by all three kinds (rectangular grid rows, polar rings,
    //      rows running alongside a path) ----
    auto* rowCount = static_cast<QSpinBox*>(nullptr);
    auto* rowBetween = static_cast<QDoubleSpinBox*>(nullptr);
    auto* rowTotal = static_cast<QDoubleSpinBox*>(nullptr);
    auto* rowLabel = static_cast<QLabel*>(nullptr);
    {
        auto* w = new QWidget(arrayTab_);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0);
        g->setHorizontalSpacing(4);
        g->setVerticalSpacing(3);
        rowCount = countSpin(w);
        rowCount->setToolTip(QStringLiteral("Number of rows"));
        rowBetween = spin(w, -1e6, 1e6, 2);
        rowTotal = spin(w, -1e6, 1e6, 2);
        rowTotal->setToolTip(QStringLiteral("Distance from the first to the last row"));
        connect(rowCount, &QSpinBox::valueChanged, this,
                [apply](int v) { apply([v](ArraySpec& s) { s.rows = v; }); });
        connect(rowBetween, &QDoubleSpinBox::valueChanged, this,
                [apply](double v) { apply([v](ArraySpec& s) { s.rowSpacing = v; }); });
        connect(rowTotal, &QDoubleSpinBox::valueChanged, this, [apply](double v) {
            apply([v](ArraySpec& s) {
                if (s.rows > 1) s.rowSpacing = v / (s.rows - 1);
            });
        });
        rowLabel = label(w, QStringLiteral("Between"));
        g->addWidget(label(w, QStringLiteral("Rows")), 0, 0);
        g->addWidget(rowCount, 0, 1);
        g->addWidget(rowLabel, 1, 0);
        g->addWidget(rowBetween, 1, 1);
        g->addWidget(label(w, QStringLiteral("Total")), 2, 0);
        g->addWidget(rowTotal, 2, 1);
        addGroup(QStringLiteral("Rows"), w, true, true, true);
    }

    // ---- Properties (rectangular): the angle both grid axes are turned by ----
    auto* axisAngle = static_cast<QDoubleSpinBox*>(nullptr);
    {
        auto* w = new QWidget(arrayTab_);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0);
        g->setHorizontalSpacing(4);
        g->setVerticalSpacing(3);
        axisAngle = spin(w, -360.0, 360.0, 2);
        axisAngle->setToolTip(QStringLiteral("Angle of the column axis (the row axis stays "
                                             "perpendicular to it)"));
        connect(axisAngle, &QDoubleSpinBox::valueChanged, this,
                [apply](double v) { apply([v](ArraySpec& s) { s.axisAngle = v; }); });
        g->addWidget(label(w, QStringLiteral("Axis°")), 0, 0);
        g->addWidget(axisAngle, 0, 1);
        addGroup(QStringLiteral("Properties"), w, true, false, false);
    }

    // ---- Properties (polar): rotate items + sweep direction ----
    auto* rotateItems = static_cast<QCheckBox*>(nullptr);
    auto* direction = static_cast<QComboBox*>(nullptr);
    {
        auto* w = new QWidget(arrayTab_);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0);
        g->setHorizontalSpacing(4);
        g->setVerticalSpacing(3);
        rotateItems = new QCheckBox(QStringLiteral("Rotate items"), w);
        rotateItems->setStyleSheet(QStringLiteral("color:#e6e9ef;background:transparent;"));
        rotateItems->setToolTip(QStringLiteral("On: each item turns with its place on the ring. "
                                               "Off: every item keeps the original's angle."));
        direction = new QComboBox(w);
        direction->addItems({QStringLiteral("CCW"), QStringLiteral("CW")});
        direction->setMinimumWidth(70);
        direction->setToolTip(QStringLiteral("Direction the items are swept in"));
        connect(rotateItems, &QCheckBox::toggled, this,
                [apply](bool on) { apply([on](ArraySpec& s) { s.rotateItems = on; }); });
        connect(direction, &QComboBox::currentIndexChanged, this, [apply](int idx) {
            apply([idx](ArraySpec& s) { s.ccw = (idx == 0); });
        });
        g->addWidget(rotateItems, 0, 0, 1, 2);
        g->addWidget(label(w, QStringLiteral("Direction")), 1, 0);
        g->addWidget(direction, 1, 1);
        addGroup(QStringLiteral("Properties"), w, false, true, false);
    }

    // ---- Properties (path): align to the tangent + where the run starts ----
    auto* alignPath = static_cast<QCheckBox*>(nullptr);
    auto* startOffset = static_cast<QDoubleSpinBox*>(nullptr);
    {
        auto* w = new QWidget(arrayTab_);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0);
        g->setHorizontalSpacing(4);
        g->setVerticalSpacing(3);
        alignPath = new QCheckBox(QStringLiteral("Align items"), w);
        alignPath->setStyleSheet(QStringLiteral("color:#e6e9ef;background:transparent;"));
        alignPath->setToolTip(QStringLiteral("On: each item turns to follow the path. "
                                             "Off: every item keeps the original's angle."));
        startOffset = spin(w, -1e6, 1e6, 2);
        startOffset->setToolTip(QStringLiteral("Distance along the path before the first item"));
        connect(alignPath, &QCheckBox::toggled, this,
                [apply](bool on) { apply([on](ArraySpec& s) { s.alignToPath = on; }); });
        connect(startOffset, &QDoubleSpinBox::valueChanged, this,
                [apply](double v) { apply([v](ArraySpec& s) { s.startOffset = v; }); });
        g->addWidget(alignPath, 0, 0, 1, 2);
        g->addWidget(label(w, QStringLiteral("Start at")), 1, 0);
        g->addWidget(startOffset, 1, 1);
        addGroup(QStringLiteral("Properties"), w, false, false, true);
    }

    // ---- Close: the item count, re-picking the centre/path, place or cancel ----
    auto* itemsLabel = static_cast<QLabel*>(nullptr);
    auto* repickBtn = static_cast<QToolButton*>(nullptr);
    {
        auto* w = new QWidget(arrayTab_);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0);
        g->setSpacing(2);
        itemsLabel = label(w, QString());
        repickBtn = bigButton(w, QStringLiteral("Pick\ncentre"));
        repickBtn->setToolTip(QStringLiteral("Pick the reference again on the canvas"));
        connect(repickBtn, &QToolButton::clicked, this, [this] {
            editor_->arrayRepick();
            canvas_->setFocus(Qt::OtherFocusReason);
        });
        auto* placeBtn = bigButton(w, QStringLiteral("Close\narray"));
        placeBtn->setToolTip(QStringLiteral("Place the array (same as pressing Enter)"));
        connect(placeBtn, &QToolButton::clicked, this, [this] {
            editor_->commitArray();
            canvas_->setFocus(Qt::OtherFocusReason);
        });
        auto* cancelBtn = bigButton(w, QStringLiteral("Cancel"));
        cancelBtn->setToolTip(QStringLiteral("Discard the array (same as pressing Esc)"));
        connect(cancelBtn, &QToolButton::clicked, this, [this] {
            editor_->cancelArray();
            canvas_->setFocus(Qt::OtherFocusReason);
        });
        g->addWidget(itemsLabel, 0, 0, 1, 3);
        g->addWidget(repickBtn, 1, 0);
        g->addWidget(placeBtn, 1, 1);
        g->addWidget(cancelBtn, 1, 2);
        addGroup(QStringLiteral("Close"), w, true, true, true);
    }

    // Reload every control from the live spec and show only the groups that belong to
    // the current kind. Called whenever the session changes (kind switch, a pick
    // landing, a value edit deriving another field).
    syncArrayRibbon_ = [this, typeRect, typePolar, typePath, colCount, colBetween, colTotal,
                        polCount, polBetween, polFill, pathMethod, pathCount, pathBetween,
                        pathTotal, rowCount, rowBetween, rowTotal, rowLabel, axisAngle,
                        rotateItems, direction, alignPath, startOffset, itemsLabel, repickBtn] {
        if (!editor_->arrayActive()) return;
        const ArraySpec& s = editor_->arraySpec();
        arraySyncing_ = true;
        for (const ArrayGroupVis& gv : arrayGroups_) {
            const bool show = s.kind == ArrayKind::Rect    ? gv.rect
                              : s.kind == ArrayKind::Polar ? gv.polar
                                                           : gv.path;
            if (gv.group) gv.group->setVisible(show);
            if (gv.rule) gv.rule->setVisible(show);
        }
        typeRect->setChecked(s.kind == ArrayKind::Rect);
        typePolar->setChecked(s.kind == ArrayKind::Polar);
        typePath->setChecked(s.kind == ArrayKind::Path);

        colCount->setValue(s.count);
        colBetween->setValue(s.colSpacing);
        colTotal->setValue(s.colSpacing * std::max(0, s.count - 1));

        polCount->setValue(s.count);
        // Between is derived: a full sweep divides by the item count, a partial one by
        // the gaps between them — exactly how the items are laid out.
        const bool fullCircle = std::abs(s.fillAngle) >= 360.0 - 1e-9;
        const int divisor = fullCircle ? std::max(1, s.count) : std::max(1, s.count - 1);
        polBetween->setValue(s.fillAngle / divisor);
        polFill->setValue(s.fillAngle);

        pathMethod->setCurrentIndex(s.divide ? 0 : 1);
        pathCount->setValue(s.count);
        // In Divide mode the spacing is derived from the path length — show the value
        // actually being used, not the stored Measure spacing. Typing over it switches
        // the method to Measure, so the fields stay live either way.
        const double step = s.divide && editor_->arrayItemStep() > 0.0 ? editor_->arrayItemStep()
                                                                      : s.pathSpacing;
        pathBetween->setValue(step);
        pathTotal->setValue(step * std::max(0, s.count - 1));

        rowCount->setValue(s.rows);
        rowBetween->setValue(s.rowSpacing);
        rowTotal->setValue(s.rowSpacing * std::max(0, s.rows - 1));
        // What "between rows" means depends on the kind.
        rowLabel->setText(s.kind == ArrayKind::Polar ? QStringLiteral("Radial")
                                                     : QStringLiteral("Between"));
        rowBetween->setToolTip(s.kind == ArrayKind::Polar
                                   ? QStringLiteral("Radial distance between rings")
                                   : s.kind == ArrayKind::Path
                                         ? QStringLiteral("Distance between rows, measured "
                                                          "across the path")
                                         : QStringLiteral("Distance between rows"));

        axisAngle->setValue(s.axisAngle);
        rotateItems->setChecked(s.rotateItems);
        direction->setCurrentIndex(s.ccw ? 0 : 1);
        alignPath->setChecked(s.alignToPath);
        startOffset->setValue(s.startOffset);

        // Live feedback on what will actually be placed, plus what is still needed.
        if (editor_->arrayAwaitingPath())
            itemsLabel->setText(QStringLiteral("Click the path on the drawing"));
        else if (editor_->arrayAwaitingCenter())
            itemsLabel->setText(QStringLiteral("Click the centre (%1 items)")
                                    .arg(editor_->arrayItemCount()));
        else
            itemsLabel->setText(QStringLiteral("%1 items").arg(editor_->arrayItemCount()));
        repickBtn->setText(s.kind == ArrayKind::Path ? QStringLiteral("Pick\npath")
                                                     : QStringLiteral("Pick\ncentre"));
        repickBtn->setEnabled(s.kind != ArrayKind::Rect); // nothing to pick for a grid
        arraySyncing_ = false;
    };

    lay->addStretch(1);
    arrayTab_->hide();
}

void MainWindow::updateArrayContext() {
    if (editor_->arrayActive() && syncArrayRibbon_) syncArrayRibbon_();
    showArrayRibbon(editor_->arrayActive());
}

void MainWindow::showArrayRibbon(bool on) {
    if (!arrayTab_ || !ribbon_) return;
    const int idx = ribbon_->indexOf(arrayTab_);
    if (on) {
        if (idx < 0) {
            const int at = ribbon_->addTab(arrayTab_, QStringLiteral("Array Creation"));
            ribbon_->setCurrentIndex(at);
        } else {
            ribbon_->setCurrentIndex(idx);
        }
        arrayTab_->show();
    } else if (idx >= 0) {
        ribbon_->removeTab(idx); // detaches but does not delete arrayTab_
        ribbon_->setCurrentIndex(0); // back to Home, where Array was launched from
    }
}

void MainWindow::beginArrayCommand(ArrayKind kind) {
    editor_->beginArray(kind);
    canvas_->setFocus(Qt::OtherFocusReason);
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
                        [this](bool v) {
                            // The space showing, and only it: pressed on a layout it turns the grid on
                            // over that sheet and leaves model space as it was.
                            canvas_->setGridVisible(v);
                            if (drawingSpaceTabs_)
                                spaceGrid_[drawingSpaceTabs_->currentIndex()] = v;
                        }),
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
    propsDock_ = propsDock;
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
    // A little air between the value fields and the scroll bar: a field that runs right up to the
    // bar looks clipped, and its drop-down arrow ends up under the user's thumb. Padding on the
    // frame shrinks the cells' area and leaves the bar where it is.
    propsTable_->setStyleSheet(QStringLiteral("QTableWidget { padding-right: 5px; }"));
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
    // Esc in a PROPERTIES field hands the keyboard back to the drawing and puts the editor in Select.
    // A value is committed with Enter; Esc means "I am done here", and being left with the focus in the
    // panel - and whatever tool was armed still armed - is not what it means. Two ways in, because Qt
    // routes the key differently depending on whether a field is open: the delegate says so when Esc
    // cancels an open field, and the filter below catches it when the panel merely has the focus.
    propsTable_->installEventFilter(this);
    if (QAbstractItemDelegate* del = propsTable_->itemDelegate())
        connect(del, &QAbstractItemDelegate::closeEditor, this,
                [this](QWidget*, QAbstractItemDelegate::EndEditHint hint) {
                    if (hint == QAbstractItemDelegate::RevertModelCache) leavePropertiesPanel();
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
    layersDock_ = layersDock;
    configDock(layersDock);
    auto* layersWidget = new QWidget(layersDock);
    auto* lv = new QVBoxLayout(layersWidget);
    lv->setContentsMargins(4, 4, 4, 4);
    lv->setSpacing(4);

    auto* curLabel = new QLabel(QStringLiteral("Current layer: 0"), layersWidget);
    curLabel->setObjectName(QStringLiteral("currentLayerLabel"));
    letSqueeze(curLabel);
    lv->addWidget(curLabel);

    auto* nameRow = new QHBoxLayout();
    auto* nameEdit = new QLineEdit(layersWidget);
    nameEdit->setPlaceholderText(QStringLiteral("Layer name"));
    auto* addLayer = makeAddButton(layersWidget, QStringLiteral("Add"),
                                  QStringLiteral("Add a layer with the name typed beside"));
    auto* delLayer = makeRemoveButton(layersWidget, QStringLiteral("Delete"),
                                     QStringLiteral("Delete the selected layer"), true);
    auto* lockLayer = new QPushButton(QStringLiteral("Lock"), layersWidget);
    nameRow->addWidget(nameEdit, 1);
    nameRow->addWidget(addLayer);
    nameRow->addWidget(delLayer);
    nameRow->addWidget(lockLayer);
    lv->addLayout(nameRow);

    layersTable_ = new QTableWidget(layersWidget);
    layersTable_->setColumnCount(6);
    // Lineweight is a layer's property and belongs with the rest of them: without it here a drawing
    // that came in with heavy layers - an older file, or a DXF - could not be made to draw thin.
    layersTable_->setHorizontalHeaderLabels({"Status", "Name", "On", "Lock", "Color", "Weight"});
    layersTable_->verticalHeader()->setVisible(false);
    // Only Name stretches; the rest keep fixed widths so they don't grow with the panel width.
    {
        auto* hh = layersTable_->horizontalHeader();
        hh->setStretchLastSection(false);
        hh->setSectionResizeMode(0, QHeaderView::Fixed);   // Status
        hh->setSectionResizeMode(1, QHeaderView::Stretch);  // Name
        hh->setSectionResizeMode(2, QHeaderView::Fixed);   // On
        hh->setSectionResizeMode(3, QHeaderView::Fixed);   // Lock
        hh->setSectionResizeMode(4, QHeaderView::Fixed);   // Color
        hh->setSectionResizeMode(5, QHeaderView::Fixed);   // Weight
    }
    layersTable_->setColumnWidth(0, 50);
    layersTable_->setColumnWidth(2, 42);
    layersTable_->setColumnWidth(3, 46);
    layersTable_->setColumnWidth(4, 58);
    layersTable_->setColumnWidth(5, 66);
    lv->addWidget(layersTable_);
    layersDock->setManaged(new DockTitleBar(layersDock, QStringLiteral("LAYERS")), layersWidget);
    addDockWidget(Qt::RightDockWidgetArea, layersDock);

    connect(addLayer, &QPushButton::clicked, this, [this, nameEdit] {
        auto& ls = doc_->layers();
        QString name = nameEdit->text().trimmed();
        if (name.isEmpty()) name = QStringLiteral("Layer%1").arg(ls.size());
        ls.push_back({name, "#ffffff", true, false, 0.25});   // a hairline, like every other new layer
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
        canvas_->invalidateScene();   // the drawing itself changed, not just the overlay
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

    // ===== BILL OF MATERIALS (right, top) =====
    // What the drawing is made of, beside the drawing. It used to be only behind a button, which meant
    // the one list that says whether a harness can be BUILT was the one thing never on screen while it
    // was drawn. Read-only here - the panel shows it, the window edits it - because a bill of materials
    // is edited a row at a time with the parts library open beside it.
    auto* bomDock = new AutoHideDock(QStringLiteral("BILL OF MATERIALS"), this);
    bomDock_ = bomDock;
    configDock(bomDock);
    auto* bomWidget = new QWidget(bomDock);
    auto* bv = new QVBoxLayout(bomWidget);
    bv->setContentsMargins(4, 4, 4, 4);
    bv->setSpacing(4);
    bomPanel_ = new QTableWidget(bomWidget);
    bomPanel_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    bomPanel_->setSelectionBehavior(QAbstractItemView::SelectRows);
    bomPanel_->verticalHeader()->setVisible(false);
    bomPanel_->setShowGrid(false);
    bomPanel_->setAlternatingRowColors(true);
    // Right-click a line: take it off, if it is a line somebody put there.
    bomPanel_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(bomPanel_, &QTableWidget::customContextMenuRequested, this,
            [this](const QPoint& at) { showBomPanelMenu(at); });
    bv->addWidget(bomPanel_);
    {
        auto* open = new QPushButton(QStringLiteral("Open the bill of materials"), bomWidget);
        letSqueeze(open);
        connect(open, &QPushButton::clicked, this, [this] { showBomDialog(); });
        bv->addWidget(open);
    }
    bomDock->setManaged(new DockTitleBar(bomDock, QStringLiteral("BILL OF MATERIALS")), bomWidget);
    addDockWidget(Qt::RightDockWidgetArea, bomDock);
    // The bill of materials above the layers, in the right column.
    splitDockWidget(bomDock, layersDock, Qt::Vertical);
    refreshBomPanel();

    // The list of every entity used to live here. It said what a click on the drawing says better, it
    // was as long as the drawing, and it was in the way of the two things that are worth having on
    // screen while drawing: what the drawing is made OF, and which layer it is on.

    // The parts of the library and the blocks, as PANELS rather than only as windows: what is on the shelf
    // is wanted while drawing, and crossing the screen to a dialog to put a part down - and closing it
    // again - is a lot of work for a thing done twenty times an hour.
    partsDock_ = makePartsPanel();
    blocksDock_ = makeBlocksPanel();
    calculatorDock_ = makeCalculatorPanel();

    // Pin / auto-hide acts on a whole side, and each side is independent: the left column (Properties)
    // and the right column (the bill of materials above the layers) each carry ONE pin - on the panel at
    // the top of the side - and folding one leaves the other alone. The sides themselves are worked out
    // from the live layout, so dragging a panel across re-forms both.
    AutoHideDock::refreshPinButtons();
    // The View tab's buttons were built before these panels existed: now they can be wired to them.
    syncViewPanelButtons();
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
    bar->setObjectName(QStringLiteral("commandBar"));
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
    // IMPORT, alongside Export, and meaning the opposite of Open.
    //
    // Open replaces the drawing on screen; Import adds another drawing to it. Both are wanted and
    // they were not distinguishable before - PDF was the only thing that could be brought in, and it
    // sat on the top level as a single entry with no sibling to be told apart from.
    auto* importMenu = fileMenu->addMenu(QStringLiteral("&Import"));
    importMenu->addAction(QStringLiteral("&DWG..."), this,
                          [this] { importDrawingFile(QFileDialog::getOpenFileName(
                                       this, QStringLiteral("Import DWG into this drawing"),
                                       QString(), QStringLiteral("DWG (*.dwg)"))); });
    importMenu->addAction(QStringLiteral("D&XF..."), this,
                          [this] { importDrawingFile(QFileDialog::getOpenFileName(
                                       this, QStringLiteral("Import DXF into this drawing"),
                                       QString(), QStringLiteral("DXF (*.dxf)"))); });
    importMenu->addSeparator();
    importMenu->addAction(QStringLiteral("&PDF..."), this, [this] { importPdf(); });
    fileMenu->addAction(QStringLiteral("&Save DWX..."), QKeySequence::Save, this, [this] { saveDwxAs(); });
    // AND EVERY OPEN DRAWING AT ONCE. On the menu as well as on the strip: a command that exists only
    // as an unlabelled icon is a command half the people using it will never find, and this is also
    // where its keyboard shortcut is declared.
    fileMenu->addAction(QStringLiteral("Save a&ll drawings"),
                        QKeySequence(QStringLiteral("Ctrl+Shift+S")), this,
                        [this] { saveAllDocuments(); });
    // THE FOUR EXPORTS, UNDER ONE SUBMENU.
    //
    // They were four entries in a row on a menu that also holds New, Open, Import, Save, Publish,
    // Templates, Options and About - and half of it read "Export ...". A submenu says once what all
    // four have in common and leaves the File menu to be a list of things that differ.
    //
    // The formats are named as formats: DXF, DWG, PDF. "DWG (native)" said what the file is not
    // rather than what it is - there is no other DWG here to distinguish it from - so it is DWG.
    auto* exportMenu = fileMenu->addMenu(QStringLiteral("&Export"));
    exportMenu->addAction(QStringLiteral("&DXF..."), this, [this] { exportDxfAs(); });
    exportMenu->addAction(QStringLiteral("D&WG..."), this, [this] { exportDwgAs(); });
    exportMenu->addSeparator();
    exportMenu->addAction(QStringLiteral("&PDF..."), this, [this] { exportPdf(false); });
    exportMenu->addAction(QStringLiteral("PDF (&window)..."), this, [this] { exportPdf(true); });
    fileMenu->addSeparator();
    // Output: the whole drawing, or the part of it that is wanted, in the order it is wanted, to paper or
    // to a PDF. The two Export PDF entries above stay for what they are - one quick page of what is
    // showing; this is the one to reach for when a pack of sheets is to go out.
    fileMenu->addAction(QStringLiteral("&Publish... (plot the model and the layouts)"),
                        QKeySequence::Print, this, [this] { showOutput(); });
    fileMenu->addSeparator();
    // What a new drawing starts as: the sheet, its border, the layers and the defaults.
    fileMenu->addAction(QStringLiteral("Drawing &templates..."), this,
                        [this] { showTemplateDialog(); });
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("&Options..."), this, [this] { showOptions(); });
    // About carries the Qt/LGPL notice, and the button on it is the only way through to the licence
    // texts. That is deliberate and it is enough: what the LGPL asks for is a prominent notice (§4(a)),
    // Qt's copyright among the ones the program displays plus a reference to where the licence copies
    // are (§4(c)), and that the texts ACCOMPANY the program (§4(b)) - which the files beside the
    // executable do whether or not a menu points at them. A second menu entry was not required, so
    // there is not one.
    // About has moved to the Help menu, which is where a person looks for it - see below. The LGPL
    // notice it carries is unaffected by which menu points at it.
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

    // ---- THE QUICK-ACCESS STRIP: New, Open, Save, Save all -------------------------------------
    //
    // The four things done most often, on the row above the tabs, one click from anywhere.
    //
    // They are all on the file menu behind the logo and they all have keyboard shortcuts, and that
    // was the argument for not having them: everything is already reachable. But "reachable" is not
    // the same as "to hand" - a save is two clicks and a read of a menu, and it is done twenty times
    // an hour. The strip is where every drawing program puts them and it is where the hand goes.
    //
    // ON THE MENU-BAR ROW rather than in the ribbon, and beside the logo rather than opposite it,
    // because that row is already the chrome above the tabs and the logo is already the file button:
    // the file commands belong next to the thing they came out of. Nothing is taken off the menu -
    // this is a short cut to it, not a replacement for it, and a menu with a hole in it where the
    // common commands used to be is worse than a menu nobody opens.
    auto* quick = new QWidget(this);
    auto* ql = new QHBoxLayout(quick);
    ql->setContentsMargins(0, 0, 0, 0);
    ql->setSpacing(0);
    ql->addWidget(logo);
    {
        // Small, flat and iconic. Text would double the width of the row for words that four
        // twenty-year-old glyphs already say, and the tooltip carries the name and the shortcut -
        // which is also how somebody LEARNS the shortcut.
        const QString style = QStringLiteral(
            "QToolButton{background:transparent;border:none;padding:2px;margin:3px 1px;"
            "border-radius:4px;}"
            "QToolButton:hover{background:rgba(255,255,255,28);}"
            "QToolButton:pressed{background:rgba(255,255,255,50);}"
            "QToolButton:disabled{background:transparent;}");
        auto add = [&](const QIcon& icon, const QString& name, const QKeySequence& key,
                       auto&& doIt) {
            auto* b = new QToolButton(quick);
            b->setIcon(icon);
            b->setIconSize(QSize(18, 18));
            b->setAutoRaise(true);
            b->setFocusPolicy(Qt::NoFocus);   // the canvas keeps the keyboard
            // THE SHORTCUT IN THE TOOLTIP, where it teaches: "Save (Ctrl+S)" on the button somebody
            // is already reaching for is how the reaching stops.
            b->setToolTip(key.isEmpty()
                              ? name
                              : QStringLiteral("%1  (%2)").arg(
                                    name, key.toString(QKeySequence::NativeText)));
            b->setStyleSheet(style);
            connect(b, &QToolButton::clicked, this, doIt);
            ql->addWidget(b);
            return b;
        };
        add(newFileIcon(), tr("New drawing"), QKeySequence::New, [this] { newDrawing(); });
        add(openFileIcon(), tr("Open a drawing"), QKeySequence::Open, [this] { openDrawing(); });
        add(saveFileIcon(), tr("Save this drawing"), QKeySequence::Save,
            [this] { if (doc_) saveDocument(activeIndex(), /*forceDialog=*/false); });
        quickSaveAllBtn_ = add(saveAllFilesIcon(), tr("Save every open drawing"),
                               QKeySequence(QStringLiteral("Ctrl+Shift+S")),
                               [this] { saveAllDocuments(); });
    }
    ql->addSpacing(6);
    menuBar()->setCornerWidget(quick, Qt::TopLeftCorner);
    refreshQuickAccess();



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
    // "Color: ByLayer" used to sit here. It is a property command, not an edit one - it belongs with
    // the colour picker on the ribbon, which is where somebody setting a colour already is - and one
    // lonely entry under a separator made the menu look like it had been left half-finished. Still
    // reachable by typing BYLAYER, which is where the command list says it is.

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    // No bare "F" shortcut: a single-letter accelerator fires before the canvas
    // sees the key, so pressing "f" while drawing would zoom-to-fit (jump to
    // centre). Fit stays available here, via the command line (z / fit / extents),
    // and the canvas right-click "Drawing center". Ctrl+Shift+F is a safe hotkey.
    viewMenu->addAction(QStringLiteral("&Fit drawing"),
                        QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F), this,
                        [this] { canvas_->fitView(); });

    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    helpMenu->addAction(QStringLiteral("&Commands and keys..."), QKeySequence::HelpContents, this,
                        [this] { showHelp(); });
    helpMenu->addSeparator();
    // Where a person looks for it. It used to be under File, between the licences and Exit, which is
    // nowhere anybody looks for what version they are running.
    helpMenu->addAction(QStringLiteral("&About Edraw..."), this, [this] { showAbout(); });
}

// ---- Theme (Dark / Light) --------------------------------------------------

// The corner of the ribbon's tab row, where Undo and Redo sit. It has to be painted: the corner of a tab
// widget is neither the tab bar nor the pane, so left alone it comes out in the application's own colour -
// a patch of the wrong shade in the corner of the ribbon, whichever scheme is set.
void MainWindow::restyleRibbonCorner() {
    if (!ribbonCorner_) return;
    ribbonCorner_->setStyleSheet(QStringLiteral("QWidget{background-color:%1;}")
                                     .arg(Chrome::dark() ? "#2f3237" : "#dcdce2"));
}

// The strip under the drawing: Model and the sheets, the + that adds one, and the scale in the corner. It
// is the one piece of chrome that touches the drawing, and it was written in one colour - so in the light
// scheme a light row carried a dark chip for the sheet in hand, and a dark scale box beside it.
void MainWindow::restyleDrawingSpaceStrip() {
    const bool dark = Chrome::dark();
    const QString bg = dark ? "#252c36" : "#dcdce2";
    const QString tx = dark ? "#aeb9c9" : "#33373f";
    const QString onTx = dark ? "#ffffff" : "#10243d";
    const QString on = dark ? "#4b596b" : "#ffffff";
    const QString hover = dark ? "#364252" : "#eaecef";
    const QString bord = dark ? "#3d4653" : "#a8aeb8";
    const QString off = dark ? "#6f7681" : "#8b929c";
    if (drawingSpaceStrip_)
        drawingSpaceStrip_->setStyleSheet(QStringLiteral("QWidget{background-color:%1;}").arg(bg));
    if (drawingSpaceTabs_)
        drawingSpaceTabs_->setStyleSheet(
            QStringLiteral("QTabBar{background:%1;}"
                           "QTabBar::tab{background:%1;color:%2;padding:7px 18px;border:none;}"
                           "QTabBar::tab:selected{background:%3;color:%4;font-weight:600;}"
                           "QTabBar::tab:hover{background:%5;color:%4;}")
                .arg(bg, tx, on, onTx, hover));
    for (QToolButton* b : spaceStripBtns_)
        if (b)
            b->setStyleSheet(QStringLiteral("QToolButton{color:%1;font-size:20px;background:%2;border:none;}"
                                            "QToolButton:hover{background:%3;}")
                                 .arg(tx, bg, hover));
    if (spaceScaleBox_)
        spaceScaleBox_->setStyleSheet(
            QStringLiteral("QComboBox{background:%1;color:%2;border:1px solid %3;border-radius:3px;"
                           "padding:2px 10px;}"
                           "QComboBox:hover{background:%4;}"
                           "QComboBox:disabled{color:%5;border-color:%3;}"
                           "QComboBox QAbstractItemView{background:%1;color:%2;"
                           "selection-background-color:%6;}")
                .arg(dark ? bg : QString("#ffffff"), tx, bord, hover, off, dark ? on : QString("#cfe3ff")));
}

void MainWindow::applyTheme(bool dark) {
    darkTheme_ = dark;
    Chrome::setDark(dark);   // said first: everything below asks Chrome what colour to be

    // Palette tokens for the application stylesheet (panels / tables / inputs).
    // The status bar + bottom command toolbar stay dark in both schemes (their
    // icons are light), matching AutoCAD's persistent dark status strip.
    QHash<QString, QString> t;
    if (dark) {
        t = {{"WIN", Chrome::back().name()}, {"TEXT", Chrome::backInk().name()},
             {"PANEL", Chrome::back().name()},
             {"ALT", Chrome::backAlt().name()}, {"HEADER", Chrome::backHeader().name()},
             {"LINE", "#4a4e55"},
             {"SEL", "#294e74"},   {"SELTX", "#ffffff"},  {"INPUT", "#45484f"},
             {"INBORD", "#585c64"},{"BTN", "#484c53"},    {"HOVER", "#525760"},
             {"SCROLL", "#2d2f34"},{"HANDLE", "#5a5f68"}, {"TABBAR", "#2f3237"},
             {"STBG", "#2d2f34"},  {"STTX", "#aab0b8"}};
    } else {
        t = {{"WIN", Chrome::back().name()}, {"TEXT", Chrome::backInk().name()},
             {"PANEL", Chrome::back().name()},
             {"ALT", Chrome::backAlt().name()}, {"HEADER", Chrome::backHeader().name()},
             {"LINE", "#c2c2c8"},
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
        // A TREE IS STYLED LIKE THE LISTS IT IS ONE OF.
        //
        // This rule named QTableWidget and QListWidget only, so a QTreeWidget fell through to the
        // platform's own colours: no visible selection band and lettering in whatever the system
        // picked, which on this ground came out purple on dark blue. Nothing was wrong with the
        // selection - it simply could not be seen, which is the same thing to whoever is using it.
        //
        // Named here rather than styled on the widget, because the answer has to change with the
        // scheme like every other colour in the window.
        "QTableWidget,QListWidget,QTreeWidget,QTreeView{background-color:{PANEL};"
        "alternate-background-color:{ALT};color:{TEXT};gridline-color:{LINE};"
        "selection-background-color:{SEL};selection-color:{SELTX};border:1px solid {LINE};}"
        // AND THE SELECTED ROW CARRIES THE BAND ITSELF. A tree draws its rows through item delegates,
        // and selection-background-color alone leaves the branch column - the indent and the arrow -
        // unpainted, so a selected row reads as a band with a gap bitten out of its left end.
        "QTreeView::item:selected,QTreeWidget::item:selected{background-color:{SEL};color:{SELTX};}"
        "QTreeView::item:hover,QTreeWidget::item:hover{background-color:{HOVER};}"
        "QTreeView::branch:selected,QTreeWidget::branch:selected{background-color:{SEL};}"
        "QHeaderView::section{background-color:{HEADER};color:{TEXT};padding:3px 6px;border:none;"
        "border-right:1px solid {LINE};border-bottom:1px solid {LINE};}"
        "QComboBox,QLineEdit,QSpinBox,QDoubleSpinBox{background-color:{INPUT};color:{TEXT};"
        "border:1px solid {INBORD};border-radius:3px;padding:2px 4px;}"
        "QAbstractSpinBox::up-button{subcontrol-origin:border;subcontrol-position:top right;"
        "width:18px;border-left:1px solid {INBORD};}"
        "QAbstractSpinBox::down-button{subcontrol-origin:border;subcontrol-position:bottom right;"
        "width:18px;border-left:1px solid {INBORD};}"
        "QAbstractSpinBox::up-button:hover,QAbstractSpinBox::down-button:hover{background:#3d8bfd;}"
        // Styling the buttons takes the style's own arrows away, so they are put back as images.
        "QAbstractSpinBox::up-arrow{image:url(:/caret-up.svg);width:9px;height:9px;}"
        "QAbstractSpinBox::down-arrow{image:url(:/caret-down.svg);width:9px;height:9px;}"
        "QComboBox QAbstractItemView{background:{PANEL};color:{TEXT};selection-background-color:{SEL};}"
        // A POPUP MENU. This sheet replaces the one loaded from the resources, and that one had these
        // rules while this one did not - so every right-click menu in the application came up with no
        // background at all, the drawing showing through the words. A menu has to be opaque.
        "QMenu{background-color:{PANEL};color:{TEXT};border:1px solid {LINE};}"
        "QMenu::item{padding:4px 22px;}"
        "QMenu::item:selected{background-color:{SEL};color:{SELTX};}"
        "QMenu::item:disabled{color:#8b929c;}"
        "QMenu::separator{height:1px;background:{LINE};margin:3px 6px;}"
        // Tooltips. Left to the platform they come out black on pale yellow in some themes and
        // black on dark in others - unreadable next to this window's own colours either way.
        "QToolTip{background-color:{PANEL};color:{TEXT};border:1px solid {LINE};padding:3px;}"
        "QPushButton{background-color:{BTN};color:{TEXT};border:1px solid {INBORD};"
        "border-radius:3px;padding:3px 10px;}"
        "QPushButton:hover{background-color:{HOVER};}"
        "QPushButton:pressed{background-color:{HEADER};}"
        // Everything below was in the stylesheet loaded from the resources and NOT here, and this sheet
        // replaces that one - so choosing a scheme used to take these away: tool buttons outside the ribbon
        // lost their hover and their pressed state, the combo boxes lost their drop-down box and its
        // chevron, and a panel's own title bar lost its ground.
        "QToolButton{background:transparent;color:{TEXT};border:1px solid transparent;border-radius:4px;"
        "padding:3px 6px;}"
        "QToolButton:hover{background:{HOVER};border-color:{INBORD};}"
        "QToolButton:checked{background:{SEL};border-color:#3d8bfd;}"
        "QToolButton:pressed{background:{HEADER};}"
        "QToolBar::separator{background:{LINE};width:1px;margin:4px 4px;}"
        "QDockWidget{color:{TEXT};titlebar-close-icon:none;titlebar-normal-icon:none;}"
        "QDockWidget::title{background:{HEADER};padding:5px 8px;border-bottom:1px solid {LINE};"
        "font-weight:bold;color:{TEXT};}"
        "QListWidget::item:selected{background:{SEL};color:{SELTX};}"
        "QTabBar::tab:hover{color:{TEXT};}"
        "QComboBox:hover,QLineEdit:focus{border-color:#3d8bfd;}"
        "QComboBox::drop-down{subcontrol-origin:padding;subcontrol-position:right center;"
        "background-color:{BTN};border-left:1px solid {INBORD};border-top-right-radius:3px;"
        "border-bottom-right-radius:3px;width:18px;}"
        "QComboBox::drop-down:hover{background-color:{HOVER};}"
        "QComboBox::down-arrow{image:url(:/caret-down.svg);width:12px;height:12px;}"
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

    // The ribbon, in the scheme now set - tabs, group names, fields and all.
    if (ribbon_) ribbon_->setStyleSheet(ribbonStyleSheet());
    // The parts library is kept alive between openings, so its own lettering and signs - which are its
    // own colours, not the application stylesheet's - have to be told the scheme changed.
    if (libraryDlg_) libraryDlg_->restyle();

    // Ribbon tool icons: light on a dark ribbon, dark on a light ribbon. Retint
    // EVERY tool button instance (a ToolId can appear on several tabs, so the
    // ToolId-keyed map alone would miss the duplicates).
    // Light on a dark ribbon, dark on a light one - the ordinary tool icons stay as they were. Recolouring
    // the lot blue was the wrong reading of "the icons are grey": the complaint was about the FALL-MENU
    // buttons showing a generic grey shape instead of the method they are set to, which is put right below.
    // A tool icon is white here because the ribbon is dark, and that is all it needs to be.
    const QColor ink = dark ? QColor("#d6dae1") : QColor("#33373f");
    for (QAction* a : allToolActions_)
        a->setIcon(makeToolIcon(static_cast<ToolId>(a->data().toInt()), ink));
    // AND PUT THE METHOD ICONS BACK.
    //
    // A tool with a fall-menu wears the method it is set to - Circle wearing "centre, radius", Arc wearing
    // "three point" - and this pass had just overwritten every one of them with the plain tool icon. It runs
    // when a scheme is applied, which includes startup, so the method icons were wiped before they were ever
    // seen: the flyout buttons showed the generic shape, in grey, and only started showing a method once one
    // was picked from the menu by hand.
    if (editor_) {
        auto wear = [this](ToolId id, const QIcon& ic) {
            if (QAction* a = toolActions_.value(static_cast<int>(id), nullptr)) a->setIcon(ic);
            for (QAction* a : allToolActions_)
                if (static_cast<ToolId>(a->data().toInt()) == id) a->setIcon(ic);
        };
        wear(ToolId::Circle, makeCircleMethodIcon(static_cast<int>(editor_->circleMode())));
        wear(ToolId::Arc, makeArcMethodIcon(static_cast<int>(editor_->arcMode())));
        wear(ToolId::Ellipse, makeEllipseMethodIcon(static_cast<int>(editor_->ellipseMode())));
        wear(ToolId::Spline, makeSplineMethodIcon(static_cast<int>(editor_->splineMode())));
        wear(ToolId::Dimension, makeDimMethodIcon(static_cast<int>(editor_->dimMode())));
        wear(ToolId::HeatshrinkRoute,
             makeToolIcon(ToolId::HeatshrinkRoute, ink,
                          static_cast<int>(editor_->shrinkRouteMode())));
    }
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

    // The panels: title bars, the strips a folded side leaves, the fly-outs and their edges. Each carries a
    // stylesheet or a paintEvent of its own, so each has to be told - a panel whose ground turned white and
    // whose lettering stayed pale would be a white bar with white writing on it.
    DockTitleBar::restyleAll();
    AutoHideDock::restyleAll();
    restyleViewPanelIcons();   // the View tab's icons are drawn, so they are drawn again
    restyleRibbonCorner();
    restyleDrawingSpaceStrip();
    // The stock symbols are drawn as well - a pale grey resistor on a light ribbon is not a faint symbol,
    // it is no symbol at all.
    for (const auto& sb : partSymbolButtons_)
        if (sb.second) sb.second->setIcon(makePartSymbolIcon(static_cast<PartSymbol>(sb.first), ink));
    // Undo and Redo are drawn too.
    for (QToolButton* b : undoRedoBtns_)
        if (b)
            b->setIcon(undoArrowIcon(b->property("redo").toBool(), b->property("compact").toBool(),
                                     Chrome::ink()));
    // The panels are filled with rows the code colours itself - a properties section header, a layer's
    // swatch - and those were coloured when the row was made. Filling them again makes them the scheme's.
    if (!docs_.empty()) refreshPanels();
    // AND THE TWO PANELS refreshPanels DOES NOT COVER.
    //
    // The parts library and the blocks list colour their own rows as they are filled - a part with no
    // drawing of a kind is dimmed, a heading is subInk - so a row keeps whatever ink was in force when
    // it was made. Nothing refilled them on a change of scheme, so a library listed while the office was
    // in the light scheme kept near-black lettering when it went dark, on a near-black panel.
    //
    // Here rather than inside refreshPanels: those four are refilled on every change to the drawing, and
    // these two read the parts catalogue off disk, which is not work to do on every edit.
    refreshPartsPanel();
    refreshBlocksPanel();

    // The hint beside the command line, and the panel headers that are not the ribbon's.
    if (cmdHintLabel_)
        cmdHintLabel_->setStyleSheet(QStringLiteral("color:%1;padding:0 10px;font-weight:600;")
                                         .arg(dark ? "#8fb0d6" : "#1f4d80"));

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
    sizeSpin->setToolTip(QStringLiteral("Text height"));
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
        // Its own cursor: a child of the canvas inherits the blank one the canvas uses for its
        // crosshair, and a text box you cannot see the pointer in is hard to click into.
        inlineEdit_->setCursor(Qt::IBeamCursor);
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
        inlineBar_->setCursor(Qt::ArrowCursor);
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

        else if (inlineCell_ >= 0) {
            editor_->applyTableCellEdit(inlineEditId_, inlineCell_, text);
            // The height typed into the editor is the cell's own text height, and a taller text
            // needs a taller row (and a wider column, if the words no longer fit across it).
            editor_->setTableCellTextHeight(inlineEditId_, inlineCell_, inlineStyle_.size);
        }
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

void MainWindow::leavePropertiesPanel() {
    // Esc means Esc: whatever the drawing would do with it. That cancels any command in progress, drops
    // the selection and goes back to Select - not merely "close the field and leave the object picked",
    // which leaves the state half way and the next click acting on something still selected.
    if (editor_) editor_->onKey(Qt::Key_Escape, Qt::NoModifier);
    if (canvas_) canvas_->setFocus(Qt::OtherFocusReason);
}

bool MainWindow::eventFilter(QObject* obj, QEvent* e) {
    // A PROPERTIES FIELD THAT BELONGS TO ONE END OF A SLEEVE has that end coloured on the drawing while
    // the keyboard is in it - see Editor::setSleeveEndHighlight. Cleared on the way out whatever the
    // field was, so a stale colour cannot outlive the panel being rebuilt under it.
    if (e->type() == QEvent::FocusIn) {
        const auto it = sleeveEndFields_.constFind(obj);
        if (it != sleeveEndFields_.constEnd()) editor_->setSleeveEndHighlight(it.value());
    } else if (e->type() == QEvent::FocusOut && sleeveEndFields_.contains(obj)) {
        editor_->setSleeveEndHighlight(-1);
    }
    // A SECTION OF THE SHELF DROPPED IN A NEW PLACE. The tree does the moving; what has to happen after it is
    // writing the order down and redrawing from it, so the tree and the setting cannot drift apart. Queued,
    // because at this moment the move has not happened yet - the order is read once it has.
    if (partsPanelTree_ && obj == partsPanelTree_->viewport() && partsPanelDragEvent(e)) return true;
    // The strip along the bottom of the canvas. A right press on a tab opens that sheet's own menu; a
    // right press anywhere else on the strip does nothing at all. Either way it stops here rather than
    // falling through to the canvas, which would answer with the menu for the drawing.
    if ((obj == drawingSpaceTabs_ || obj == drawingSpaceStrip_) &&
        (e->type() == QEvent::MouseButtonPress || e->type() == QEvent::MouseButtonRelease ||
         e->type() == QEvent::ContextMenu)) {
        if (e->type() == QEvent::ContextMenu) return true;
        auto* me = static_cast<QMouseEvent*>(e);
        if (me->button() != Qt::RightButton) return false;
        if (e->type() == QEvent::MouseButtonPress && obj == drawingSpaceTabs_ && drawingSpaceTabs_)
            showDrawingSpaceMenu(me->pos());
        return true;
    }
    if (obj == propsTable_ && e->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) {
        leavePropertiesPanel();
        return true;
    }

    if (obj == inlineEdit_ && e->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(e);
        if (ke->key() == Qt::Key_Escape) { commitInlineText(false); return true; }
        // SHIFT+ENTER IS A NEW ROW, everywhere and always. It is the convention in every box that takes
        // more than one line of words, and without it a callout that wants two lines could not be typed at
        // all: a leader's words are a single-value edit, so plain Enter finishes them - and Shift+Enter
        // finished them too, because nothing looked at the Shift.
        if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) &&
            (ke->modifiers() & Qt::ShiftModifier) && !(ke->modifiers() & Qt::ControlModifier))
            return false;   // the editor puts the line break in
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
    QMenu menu(this);
    buildCanvasMenu(menu, canvas_->worldFromGlobal(globalPos));
    menu.exec(globalPos);
}

// WHAT IS ON THE MENU, apart from putting it on the screen. Split so that "right-clicking a placed part
// offers to open it in the library" can be checked by reading the menu rather than by popping a modal one up
// in front of a test and waiting for somebody to click it.
void MainWindow::buildCanvasMenu(QMenu& menu, const Vec2& menuWorld) {
    if (!editor_) return;

    // A balloon under the cursor — or a selected balloon — enables the leader commands
    // alongside the general ones (right-click on the canvas shows general + leader).
    const Vec2 world = menuWorld;
    QString balloonId = editor_->balloonAt(world);
    if (!balloonId.isEmpty()) {
        editor_->selectById(balloonId); // clicking a balloon selects it/its batch
    } else if (doc_) {
        for (const auto& id : editor_->selection()) {
            for (const auto& e : editDoc()->entities())
                if (e.id == id && e.meta.value("balloon").toBool()) { balloonId = id; break; }
            if (!balloonId.isEmpty()) break;
        }
    }

    // The library first. Putting a part on the sheet is the commonest thing done from this menu by a long
    // way, and it used to mean crossing the screen to the library window and coming back. The parts of the
    // library being worked in are here, by kind, with a search for a library too long to walk through.
    addLibraryToCanvasMenu(menu);
    const bool hasSel = !editor_->selection().empty();

    // On a sheet, the windows come first. A window is added here, or with "+ window" in the corner of the
    // strip, or by typing LAYOUTWINDOW - three ways, because it was not found the first time.
    if (canvas_->layoutMode()) {
        menu.addAction(QStringLiteral("Add a window onto the drawing"), this,
                       [this] { addLayoutWindow(); });
        const int picked = canvas_->selectedLayoutViewport();
        const int inside = canvas_->activeLayoutViewport();
        // The window UNDER THE CURSOR, whether or not it has been picked out first: right-clicking a window
        // and being offered nothing about it is the sort of thing that makes a person hunt for a menu.
        // Where the menu was asked for, in the canvas's own pixels - worked back from the world point the
        // menu was built about, because the builder is given that rather than a position on the screen (it
        // has to be answerable without a screen at all).
        const int under = canvas_->layoutViewportAt(canvas_->screenFromWorld(menuWorld).toPoint());
        if (under >= 0) {
            menu.addAction(tr("Delete this window"), this,
                           [this, under] { canvas_->removeLayoutViewport(under); });
            if (under != inside)
                menu.addAction(tr("Step into this window"), this,
                               [this, under] { canvas_->enterLayoutViewport(under); });
        }
        if (inside >= 0) {
            menu.addAction(QStringLiteral("Step out onto the paper"), this,
                           [this] { canvas_->leaveLayoutViewport(); });
        } else if (picked >= 0 && picked != under) {
            menu.addAction(QStringLiteral("Step into this window"), this,
                           [this, picked] { canvas_->enterLayoutViewport(picked); });
        }
        if (picked >= 0 && picked != under)
            menu.addAction(QStringLiteral("Take the picked window off the sheet"), this,
                           [this, picked] { canvas_->removeLayoutViewport(picked); });
        menu.addSeparator();
    }

    // Anything drawn FROM a library part carries its id, so the sheet can answer the same question the
    // builder's tables answer: what is that thing? A cable's wires, a connector, a braid - the entry is
    // named after what it actually is, and it comes first because it is what a right-click on a drawn
    // object is usually asking.
    {
        QString libPart;
        QString what;
        auto lookAt = [&](const Entity& e) {
            const QString id = e.meta.value(QStringLiteral("libPart")).toString();
            if (id.isEmpty() || !libPart.isEmpty()) return;
            libPart = id;
            what = e.meta.value(QStringLiteral("bundle")).toString();
        };
        if (doc_) {
            // Whatever is selected first - a right-click on the sheet acts on the selection - and then
            // a wire under the cursor, which is the one thing worth picking up without selecting it.
            for (const auto& id : editor_->selection())
                for (const auto& e : editDoc()->entities())
                    if (e.id == id) lookAt(e);
            if (libPart.isEmpty()) {
                const QString wire = editor_->wireAt(world);
                for (const auto& e : editDoc()->entities())
                    if (e.id == wire) lookAt(e);
            }
        }
        if (!libPart.isEmpty()) {
            PartsCatalog catalog;
            catalog.openRemembered();
            if (const LibraryPart* part = catalog.byId(libPart)) {
                const LibraryPart copyOfPart = *part;   // the catalog goes out of scope; the part must not
                const QString noun = part->kind == PartKind::Cable
                                         ? QStringLiteral("cable")
                                         : partKindName(part->kind).toLower();
                menu.addAction(QStringLiteral("%1 — %2").arg(partKindName(part->kind), part->title()))
                    ->setEnabled(false);
                menu.addAction(QStringLiteral("See the %1's schematic").arg(noun), this,
                               [this, copyOfPart] {
                                   PartViewDialog::showPart(copyOfPart,
                                                            PartViewDialog::Show::Schematic, this);
                               });
                menu.addAction(QStringLiteral("See the %1's mechanical drawing").arg(noun), this,
                               [this, copyOfPart] {
                                   PartViewDialog::showPart(copyOfPart,
                                                            PartViewDialog::Show::Mechanical, this);
                               });
                menu.addAction(QStringLiteral("See what it is"), this, [this, copyOfPart] {
                    PartViewDialog::showPart(copyOfPart, PartViewDialog::Show::Data, this);
                });
                // OPEN, not "see": this is the way to the library now that a double-click on a placed part
                // does nothing, so it has to read as the door it is.
                menu.addAction(QStringLiteral("Open the %1 in the parts library").arg(noun), this,
                               [this, libPart] { showPartsLibraryAt(libPart); });
                menu.addSeparator();
            }
        }
    }

    QAction* copy = menu.addAction(QStringLiteral("Copy"));
    copy->setEnabled(hasSel);
    connect(copy, &QAction::triggered, this, [this] { editor_->copySelection(); });

    QAction* cut = menu.addAction(QStringLiteral("Cut"));
    cut->setEnabled(hasSel);
    connect(cut, &QAction::triggered, this, [this] { editor_->copyWithBase(true); });

    // Quick Select / Match properties, where AutoCAD puts them (right-click menu).
    QAction* qsel = menu.addAction(QStringLiteral("Quick Select..."));
    connect(qsel, &QAction::triggered, this, [this] { editor_->beginQuickSelect(); });

    QAction* matchProp = menu.addAction(QStringLiteral("Match properties"));
    matchProp->setEnabled(hasSel);
    matchProp->setToolTip(QStringLiteral("Paint this object's properties onto others"));
    connect(matchProp, &QAction::triggered, this, [this] {
        editor_->beginMatchProps();
        canvas_->setFocus(Qt::OtherFocusReason);
    });

    // DRAW ORDER. Two things in the same place are drawn in the order the sheet holds them, and until now
    // that order was whatever order they were drawn in - a hatch put over a bundle covered it, and there
    // was nothing to be done about it. Front and Back are absolute; ABOVE and UNDER move the selection
    // just clear of the objects it actually overlaps, and leave the rest of the sheet alone.
    {
        QMenu* order = menu.addMenu(QStringLiteral("Draw Order"));
        order->setEnabled(hasSel);
        using DrawOrder = Editor::DrawOrder;
        auto add = [&](const QString& text, DrawOrder where) {
            order->addAction(text, this, [this, where] {
                editor_->setSelectionDrawOrder(where);
                canvas_->update();
            });
        };
        add(QStringLiteral("Bring to Front"), DrawOrder::Front);
        add(QStringLiteral("Send to Back"), DrawOrder::Back);
        order->addSeparator();
        add(QStringLiteral("Bring Above Objects"), DrawOrder::AboveTouching);
        add(QStringLiteral("Send Under Objects"), DrawOrder::UnderTouching);
    }

    // A table under the cursor (or selected): its contents as a CSV file. Offered only when there
    // is a table to export, so the menu says nothing that would do nothing.
    {
        const Entity* table = nullptr;
        if (doc_) {
            for (const auto& e : editDoc()->entities())
                if (e.meta.value("table").toBool()) {
                    const Bounds tb = e.bounds();
                    if (tb.valid && world.x >= tb.min.x && world.x <= tb.max.x &&
                        world.y >= tb.min.y && world.y <= tb.max.y)
                        table = &e;
                }
            if (!table)
                for (const auto& id : editor_->selection())
                    for (const auto& e : editDoc()->entities())
                        if (e.id == id && e.meta.value("table").toBool()) table = &e;
        }
        if (table) {
            menu.addSeparator();
            const QString tableId = table->id;
            const bool isBom = table->meta.value("bomTable").toBool();
            QAction* csv = menu.addAction(isBom
                                              ? QStringLiteral("Export bill of materials to CSV...")
                                              : QStringLiteral("Export table to CSV..."));
            connect(csv, &QAction::triggered, this, [this, tableId] {
                // The export works on the selection, so the table under the cursor becomes it.
                editor_->selectById(tableId);
                exportTableCsv();
            });
        }
    }

    // Re-ordering the pinout of the connectors in the selection. Offered only when there is
    // something to work on - two or more selected connectors with wires running between them -
    // because on any other selection every one of these would do nothing.
    if (editor_->canReorderSelectedPinout()) {
        menu.addSeparator();
        auto* pm = menu.addMenu(QStringLiteral("Re-order pinout"));
        struct Cmd { const char* text; const char* tip; bool group; bool fromTop; };
        const Cmd cmds[] = {
            {"Minimize wire crossings",
             "Re-order the pins so as few wires cross as the wiring allows", false, false},
            {"Minimize wire pitch",
             "Re-order the pins so the wires sit on neighbouring pins, as one tight group",
             true, false},
            {"Minimize wire pitch, from the top",
             "As above, and the group starts at the connector's first pin", true, true},
            {"Minimize wire crossings, from the top",
             "Fewest crossings, with the wired pins starting at the connector's first pin",
             false, true},
        };
        for (const Cmd& c : cmds) {
            QAction* act = pm->addAction(QString::fromLatin1(c.text));
            act->setToolTip(QString::fromLatin1(c.tip));
            const bool group = c.group, fromTop = c.fromTop;
            connect(act, &QAction::triggered, this, [this, group, fromTop] {
                Editor::PinoutGoal goal;
                goal.group = group;
                goal.fromTop = fromTop;
                editor_->reorderSelectedPinout(goal);
            });
        }
    }

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
    for (const auto& e : editDoc()->entities()) {
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
    // Wider than it was. The Qt/LGPL notice below is a paragraph rather than a line, and at 380 it wrapped
    // into a column so narrow the dialog became taller than it was wide.
    dlg.setFixedWidth(520);
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

    // THE NOTICE THE LGPL ASKS FOR, where the licence asks for it: with every copy of the program,
    // plainly, not buried in a file somebody has to know to open. Edraw links Qt dynamically, so the
    // toolkit is used under the LGPL v3. This label is the whole of Edraw's §4 notice, and every clause
    // of it is here for a reason:
    //
    //   §4(a) prominent notice that the Library is used and that this License covers it
    //         -> "uses the Qt toolkit ... under the GNU Lesser General Public License v3.0"
    //   §4(c) applies BECAUSE this dialog displays a copyright notice during execution ("(c) ETS").
    //         Where it applies it asks for two further things: the Library's own copyright notice
    //         among the ones shown, and a reference directing the user to the copies of the GNU GPL
    //         and the LGPL.
    //         -> "Copyright (c) The Qt Company Ltd. and other contributors"
    //         -> "See Third-party licences below ... also in the licenses folder"
    //
    // The version is read from Qt itself rather than typed, because a notice that names the wrong
    // version is not a notice. Do not trim this text without reading §4 of licenses/LGPL-3.0.txt.
    auto* qtNotice = new QLabel(
        QStringLiteral("This program uses the Qt toolkit version %1 — Copyright © The Qt Company Ltd. "
                       "and other contributors — under the GNU Lesser General Public License v3.0. "
                       "See “Third-party licences” below for the LGPL and GPL texts and for how to "
                       "obtain Qt's source code; the same documents are supplied in the “licenses” "
                       "folder beside this program.")
            .arg(QStringLiteral(QT_VERSION_STR)),
        &dlg);
    qtNotice->setWordWrap(true);
    qtNotice->setStyleSheet(QStringLiteral("color:#9aa7b8;font-size:11px;"));

    col->addWidget(name);
    col->addWidget(ver);
    col->addSpacing(6);
    col->addWidget(desc);
    col->addSpacing(10);
    col->addWidget(qtNotice);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
    // Not an "extra" button in the Ok/Cancel sense - the licences have to be reachable, so the button
    // that reaches them sits on the same row as Ok rather than behind a menu somewhere.
    auto* licBtn = bb->addButton(QStringLiteral("Third-party licences..."),
                                 QDialogButtonBox::ActionRole);
    connect(licBtn, &QPushButton::clicked, &dlg, [this, &dlg] {
        dlg.accept();          // About is modal; close it so the licences are not trapped behind it
        showLicenses();
    });
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    col->addSpacing(8);
    col->addWidget(bb);
    row->addLayout(col, 1);

    dlg.exec();
}

// ---- Third-party licences --------------------------------------------------
//
// WHAT THIS IS FOR. Edraw ships Qt's libraries beside its own executable, which makes Edraw a combined
// work under the LGPL v3. Three things follow, and this dialog is where two of them are met: the notice
// that Qt is used under the LGPL (also on the About dialog), and the supply of the licence texts
// themselves. The third - the route to Qt's source code - is written into the notices document.
//
// The documents come out of the compiled-in resources, not off the disk, so the notice cannot be lost by
// a copy that missed a subdirectory. The same files are also written into the install folder, as plain
// text, because that is where anyone auditing the product will look.
void MainWindow::showLicenses(const QString& document) {
    struct Doc { QString title; QString path; };
    // Notices first: it is the document that says what is used and where its source can be had. The two
    // licence texts follow, in the order the LGPL itself puts them - the LGPL is a set of additional
    // permissions on top of the GPL, so both are supplied and both are here.
    const QList<Doc> docs = {
        {QStringLiteral("Third-party notices"), QStringLiteral(":/licenses/THIRD-PARTY-NOTICES.txt")},
        {QStringLiteral("GNU LGPL v3"),         QStringLiteral(":/licenses/LGPL-3.0.txt")},
        {QStringLiteral("GNU GPL v3"),          QStringLiteral(":/licenses/GPL-3.0.txt")},
    };

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Third-party licences"));
    dlg.resize(820, 620);
    auto* outer = new QVBoxLayout(&dlg);

    auto* intro = new QLabel(
        QStringLiteral("Edraw is proprietary software. It incorporates and distributes third-party "
                       "software, whose notices and licences are reproduced below. The same documents "
                       "are supplied as text files beside the program."),
        &dlg);
    intro->setWordWrap(true);
    outer->addWidget(intro);

    auto* tabs = new QTabWidget(&dlg);
    int openOn = 0;
    for (int i = 0; i < docs.size(); ++i) {
        const Doc& d = docs.at(i);
        auto* view = new QPlainTextEdit(tabs);
        view->setReadOnly(true);
        view->setLineWrapMode(QPlainTextEdit::NoWrap);   // the texts are pre-wrapped; re-wrapping them
                                                        // breaks the licences' own layout
        view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));

        QFile f(d.path);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            view->setPlainText(QString::fromUtf8(f.readAll()));
        } else {
            // A build that dropped the resource is a build that does not give the notice. Say so
            // loudly rather than showing an empty box that looks like there is nothing to declare.
            view->setPlainText(
                QStringLiteral("This copy of Edraw is incomplete: the licence document\n\n    %1\n\n"
                               "is missing from the program. Please report this. The text is also "
                               "supplied as a file in the \"licenses\" folder beside Edraw.exe, and "
                               "the licences can be read at https://www.gnu.org/licenses/.")
                    .arg(d.path));
        }
        tabs->addTab(view, d.title);
        if (!document.isEmpty() && document == d.path) openOn = i;
    }
    tabs->setCurrentIndex(openOn);
    outer->addWidget(tabs, 1);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    outer->addWidget(bb);

    dlg.exec();
}

// ---- Options dialog --------------------------------------------------------

void MainWindow::showOptions(const QString& page) {
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

    // ---- THE GROUND THE RIBBON AND THE PANELS ARE DRAWN ON, per scheme ----
    //
    // Two buttons and not one, because a ground chosen for the dark scheme means nothing in the light
    // one: they are two settings that happen to share a purpose, not one setting with a brightness
    // knob. Each shows the colour it currently holds, so the pair can be read without opening either.
    //
    // Reset is offered beside them rather than as a colour somebody has to find again: "the built-in
    // one" is a real answer and there is no shade of grey that means it.
    QColor backDark = Chrome::chosenBack(true);
    QColor backLight = Chrome::chosenBack(false);
    bool backChanged = false;
    // WHAT THE WINDOW WAS WEARING WHEN OPTIONS OPENED, so a previewed colour can be taken back off it
    // again. Apply below puts a ground on the ribbon without writing it down anywhere; Cancel has to
    // undo that, or "Cancel" leaves the change on screen and only a restart gets rid of it.
    const QColor origBackDark = backDark;
    const QColor origBackLight = backLight;
    const bool origDark = darkTheme_;
    bool backPreviewed = false;

    const auto backRow = [&](const QString& label, QColor* held, bool forDark) {
        auto* row = new QWidget(disp);
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        auto* pick = new QPushButton(row);
        pick->setMinimumWidth(120);
        auto* reset = new QPushButton(QStringLiteral("Built-in"), row);
        rl->addWidget(pick, 1);
        rl->addWidget(reset);
        const auto show = [pick, held, forDark] {
            const QColor c = held->isValid() ? *held : Chrome::chosenBack(forDark);
            if (c.isValid()) {
                pick->setText(c.name());
                // The button wears the colour, so the choice can be seen without a preview pane.
                pick->setStyleSheet(QStringLiteral("background:%1;color:%2;")
                                        .arg(c.name(),
                                             c.lightness() < 128 ? QStringLiteral("#f0f0f0")
                                                                 : QStringLiteral("#1e2126")));
            } else {
                pick->setText(QStringLiteral("Built-in colour..."));
                pick->setStyleSheet(QString());
            }
        };
        show();
        connect(pick, &QPushButton::clicked, &dlg, [&dlg, held, label, show, &backChanged] {
            const QColor c = QColorDialog::getColor(
                held->isValid() ? *held : QColor("#3a3d43"), &dlg, label);
            if (!c.isValid()) return;
            *held = c;
            backChanged = true;
            show();
        });
        connect(reset, &QPushButton::clicked, &dlg, [held, show, &backChanged] {
            *held = QColor();
            backChanged = true;
            show();
        });
        df->addRow(label, row);
    };
    backRow(QStringLiteral("Ribbon and panel colour (dark)"), &backDark, true);
    backRow(QStringLiteral("Ribbon and panel colour (light)"), &backLight, false);

    // SEEING IT ON THE RIBBON BEFORE COMMITTING TO IT.
    //
    // A ground is chosen against the chrome it will be drawn on, and until now the only way to see it
    // there was to press OK, look, and reopen Options if it was wrong - so the choice was really being
    // made from a 120-pixel swatch. This puts it on the window at once with the dialog still open, so
    // the next pick is judged against the real thing.
    //
    // The scheme goes with it, because the two grounds are PER SCHEME: the dark one is invisible while
    // the light scheme is showing, so previewing it without switching would show nothing at all.
    //
    // It is a preview and not a commit - nothing is written to the settings here, and Cancel puts the
    // window back to what it was wearing when Options opened.
    auto* applyBack = new QPushButton(QStringLiteral("Apply now"), disp);
    applyBack->setToolTip(QStringLiteral(
        "Put these colours on the ribbon and panels now, without closing Options.\n"
        "Nothing is saved until OK; Cancel puts back what was there before."));
    connect(applyBack, &QPushButton::clicked, &dlg,
            [this, &backDark, &backLight, scheme, &backPreviewed] {
                Chrome::setChosenBack(true, backDark);
                Chrome::setChosenBack(false, backLight);
                backPreviewed = true;
                // Set first, applied second: applyTheme is what builds the stylesheets that read the
                // chosen ground, which is the order the OK path uses for the same reason.
                applyTheme(scheme->currentIndex() == 0);
            });
    df->addRow(QString(), applyBack);

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

    // HOW MUCH LIGHT IS ON THAT GROUND, which is not the same question as what colour it is. A
    // drawing office in the sun and one at night want the same colour scheme and a different amount
    // of light on it, and picking a fresh grey out of a colour wheel every time is not how anybody
    // wants to answer that. The grid follows it, so the squares keep their footing either way.
    auto* bright = new QSlider(Qt::Horizontal, disp);
    bright->setRange(-60, 60);
    bright->setValue(canvas_->canvasBrightness());
    bright->setToolTip(QStringLiteral(
        "Lighter or darker than the background colour itself. 0 is the colour as it is set."));
    df->addRow(QStringLiteral("Canvas brightness"), sliderRow(bright, QStringLiteral("%")));
    // Shown on the canvas as it is dragged, so it is judged on the drawing and not on a number.
    const int brightWas = canvas_->canvasBrightness();
    connect(bright, &QSlider::valueChanged, canvas_,
            [this](int v) { canvas_->setCanvasBrightness(v); });

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

    // ===== Drawing =====
    auto* drw = new QWidget(tabs);
    auto* dwf = new QFormLayout(drw);
    auto* fanScale = new QDoubleSpinBox(drw);
    fanScale->setObjectName(QStringLiteral("optFaceFanScale"));
    fanScale->setRange(1.0, 100.0);
    fanScale->setDecimals(0);
    fanScale->setSuffix(QStringLiteral(" %"));
    fanScale->setValue(editor_->faceFanScale());
    fanScale->setToolTip(QStringLiteral(
        "Shift+click on a part's entry face while drawing a harness spreads its fan over this "
        "much of the face, centred on it."));
    dwf->addRow(QStringLiteral("Auto scale fan"), fanScale);
    auto* cableScale = new QDoubleSpinBox(drw);
    cableScale->setObjectName(QStringLiteral("optFaceCableScale"));
    cableScale->setRange(1.0, 100.0);
    cableScale->setDecimals(0);
    cableScale->setSuffix(QStringLiteral(" %"));
    cableScale->setValue(editor_->faceCableScale());
    cableScale->setToolTip(QStringLiteral(
        "Shift+click on a part's entry face while drawing a cable or heatshrink makes it this "
        "wide a share of the face, centred on it."));
    dwf->addRow(QStringLiteral("Cable/heatshrink scale"), cableScale);
    tabs->addTab(drw, QStringLiteral("Drawing"));

    // ===== Units =====
    auto* units = new QWidget(tabs);
    auto* ufm = new QFormLayout(units);
    auto* lenUnit = new QComboBox(units);
    lenUnit->addItems({QStringLiteral("Millimeters (metric)"), QStringLiteral("Inches")});
    lenUnit->setCurrentIndex(EntityRenderer::lengthUnitInch() ? 1 : 0);
    ufm->addRow(QStringLiteral("Working length unit"), lenUnit);
    auto* dualUnits = new QCheckBox(QStringLiteral("Show both units (other in parentheses)"), units);
    dualUnits->setChecked(EntityRenderer::dualUnits());
    ufm->addRow(QString(), dualUnits);
    auto* xsec = new QComboBox(units);
    xsec->addItems({QString::fromUtf8("mm\xC2\xB2 (metric)"), QStringLiteral("AWG")});
    xsec->setCurrentIndex(crossSectionAwg_ ? 1 : 0);
    ufm->addRow(QStringLiteral("Wire cross-section"), xsec);
    tabs->addTab(units, QStringLiteral("Units"));

    // ===== BOM: how this drawing's bill of materials is worked out and what reaches the sheet =====
    // The settings belong to the DRAWING (a sheet keeps the units and columns it was set up with),
    // so this page edits the open drawing's own configuration.
    auto* bom = new QWidget(tabs);
    auto* bf = new QFormLayout(bom);
    BomTable bomTable;
    if (doc_) bomTable.fromJson(doc_->bom());
    BomConfig& bc = bomTable.config;
    auto* bomUnit = new QComboBox(bom);
    for (const PartUnit& u : partUnits())
        if (u.dim == QLatin1String("length"))
            bomUnit->addItem(QStringLiteral("%1 — %2").arg(u.code, u.label), u.code);
    bomUnit->setCurrentIndex(std::max(0, bomUnit->findData(bc.lengthUnit)));
    bomUnit->setToolTip(QStringLiteral(
        "The unit every LENGTH is listed and totalled in. The drawing is in millimeters; a harness "
        "is usually bought by the meter. A row counted in pieces is not affected."));
    bf->addRow(QStringLiteral("Lengths in"), bomUnit);
    auto* bomSpare = new QDoubleSpinBox(bom);
    bomSpare->setRange(0.0, 500.0);
    bomSpare->setDecimals(1);
    bomSpare->setSuffix(QStringLiteral(" %"));
    bomSpare->setValue(bc.sparePercent);
    bomSpare->setToolTip(QStringLiteral("Added to every length; pieces are not affected"));
    bf->addRow(QStringLiteral("Spare on lengths"), bomSpare);
    auto* bomDec = new QSpinBox(bom);
    bomDec->setRange(0, 4);
    bomDec->setValue(bc.qtyDecimals);
    bf->addRow(QStringLiteral("Quantity decimals"), bomDec);
    auto* bomRound = new QCheckBox(QStringLiteral("Round lengths up to the next whole unit"), bom);
    bomRound->setChecked(bc.roundUpLengths);
    bf->addRow(QString(), bomRound);
    auto* bomUnify = new QCheckBox(QStringLiteral("Total the same part into one line"), bom);
    bomUnify->setChecked(bc.unify);
    bf->addRow(QString(), bomUnify);
    auto* bomDrawing = new QCheckBox(QStringLiteral("Include what the drawing calls for"), bom);
    bomDrawing->setChecked(bc.includeDrawing);
    bf->addRow(QString(), bomDrawing);
    // Which columns the table placed on the drawing carries. The labels are the drawing's own, so
    // a renamed column is ticked by the name it was given.
    QList<QCheckBox*> bomCols;
    {
        auto* colsW = new QWidget(bom);
        auto* colsL = new QGridLayout(colsW);
        colsL->setContentsMargins(0, 0, 0, 0);
        for (int i = 0; i < bomColumnCount(); ++i) {
            auto* cb = new QCheckBox(bomTable.header(i), colsW);
            cb->setChecked(bc.columnShown(i));
            bomCols << cb;
            colsL->addWidget(cb, i / 3, i % 3);
        }
        // The ticks govern this window and the side panel as well as the placed table now, so the label no
        // longer says only "on the drawing" - which was true while they reached the sheet alone.
        bf->addRow(QStringLiteral("Columns shown"), colsW);
    }
    tabs->addTab(bom, QStringLiteral("BOM"));
    if (!page.isEmpty())
        for (int i = 0; i < tabs->count(); ++i)
            if (tabs->tabText(i).compare(page, Qt::CaseInsensitive) == 0) tabs->setCurrentIndex(i);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    outer->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() != QDialog::Accepted) {
        // A PREVIEW THAT WAS NOT KEPT. Apply put a ground on the window without writing it anywhere,
        // so Cancel has to take it off again and put back what Options opened on - both the colours and
        // the scheme, since Apply switches that too. Without this, Cancel leaves the change on screen
        // and nothing but a restart undoes it, which makes Apply a trap rather than a preview.
        if (backPreviewed) {
            Chrome::setChosenBack(true, origBackDark);
            Chrome::setChosenBack(false, origBackLight);
            applyTheme(origDark);
        }
        canvas_->setCanvasBrightness(brightWas);   // the light went on as it was dragged
        return;
    }

    // The drawing's bill-of-materials configuration, as set on the BOM page.
    if (doc_) {
        bc.lengthUnit = bomUnit->currentData().toString();
        bc.sparePercent = bomSpare->value();
        bc.qtyDecimals = bomDec->value();
        bc.roundUpLengths = bomRound->isChecked();
        bc.unify = bomUnify->isChecked();
        bc.includeDrawing = bomDrawing->isChecked();
        for (int i = 0; i < bomCols.size(); ++i) bc.setColumnShown(i, bomCols[i]->isChecked());
        doc_->bom() = bomTable.toJson();
        doc_->setDirty(true);
        if (bomDlg_) bomDlg_->reload(); // the list is showing: it follows at once
    }

    // THE CHOSEN GROUND IS SET BEFORE THE THEME IS APPLIED, because applyTheme is what builds the
    // stylesheets that read it. Setting it afterwards would leave the window on its old colour until
    // something else happened to rebuild them.
    if (backChanged) {
        Chrome::setChosenBack(true, backDark);
        Chrome::setChosenBack(false, backLight);
        QSettings st;
        // An invalid colour is stored as nothing at all, so "built-in" survives a restart as the
        // absence of a setting rather than as some particular grey that happens to match today's.
        if (backDark.isValid()) st.setValue(QStringLiteral("ui/chromeBackDark"), backDark.name());
        else st.remove(QStringLiteral("ui/chromeBackDark"));
        if (backLight.isValid()) st.setValue(QStringLiteral("ui/chromeBackLight"), backLight.name());
        else st.remove(QStringLiteral("ui/chromeBackLight"));
    }

    // Apply everything. The theme sets canvas colors; explicit picks override.
    applyTheme(scheme->currentIndex() == 0);
    canvas_->setCrosshairSize(crossSize->value());
    if (crossChanged) canvas_->setCrosshairColor(crossCol);
    if (bgChanged) canvas_->setBackgroundColor(bgCol); // auto-derives grid colours
    canvas_->setCanvasBrightness(bright->value());
    {
        QSettings st;
        st.setValue(QStringLiteral("ui/canvasBrightness"), bright->value());
    }
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
    editor_->setFaceFanScale(fanScale->value());
    editor_->setFaceCableScale(cableScale->value());
    EntityRenderer::setDimensionDecimals(dimDec->value());
    EntityRenderer::setLengthUnitInch(lenUnit->currentIndex() == 1);
    EntityRenderer::setDualUnits(dualUnits->isChecked());
    crossSectionAwg_ = (xsec->currentIndex() == 1);
    // Every cross-section shown anywhere follows this: the wire markings, the harness
    // builder's gauge list and the bill of materials.
    setWireGaugeNotationAwg(crossSectionAwg_);
    saveSettings();
    canvas_->update();
}

// ---- Panel refresh ---------------------------------------------------------

void MainWindow::refreshPanels() {
    refreshLayers();
    refreshProperties();
    refreshEntityList();
    refreshBomPanel();
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
            // A layer going off or on changes what the drawing IS, so the cached scene has to be
            // rebuilt. A plain update() repainted the cache exactly as it was, which is why unchecking
            // a layer appeared to do nothing until the view was panned or something was edited.
            canvas_->invalidateScene();
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

    // The lineweight every object on the layer is drawn with, from the standard list. 0.25 is a normal
    // line - one pixel, the way a frame or a table should look; the heavier ones are for something meant
    // to stand out. Changing it redraws the sheet at once.
    auto weightCell = [this](int row, double weight) {
        auto* cb = new QComboBox(layersTable_);
        static const double kWeights[] = {0.13, 0.25, 0.35, 0.50, 0.70, 1.00, 1.40, 2.00};
        int at = -1;
        for (double w : kWeights) {
            cb->addItem(QStringLiteral("%1").arg(w, 0, 'f', 2), w);
            if (std::abs(w - weight) < 1e-9) at = cb->count() - 1;
        }
        if (at < 0) {   // a weight from a file that is not one of the standard ones
            cb->insertItem(0, QStringLiteral("%1").arg(weight, 0, 'f', 2), weight);
            at = 0;
        }
        cb->setCurrentIndex(at);
        cb->setToolTip(QStringLiteral("How thick everything on this layer is drawn"));
        connect(cb, &QComboBox::activated, this, [this, cb, row] {
            if (refreshing_) return;
            auto& ll = doc_->layers();
            if (row < 0 || row >= static_cast<int>(ll.size())) return;
            ll[static_cast<size_t>(row)].weight = cb->currentData().toDouble();
            doc_->setDirty(true);
            canvas_->invalidateScene();
        });
        return cb;
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
        layersTable_->setCellWidget(r, 5, weightCell(r, ls[r].weight));
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

int MainWindow::focusedPropsRow() const {
    if (!propsTable_) return -1;
    QWidget* f = QApplication::focusWidget();
    if (!f) return -1;
    for (int r = 0; r < propsTable_->rowCount(); ++r) {
        QWidget* w = propsTable_->cellWidget(r, 1);
        if (w && (w == f || w->isAncestorOf(f))) return r;
    }
    return -1;
}

void MainWindow::refocusPropsRow(int row) {
    if (row < 0 || !propsTable_ || row >= propsTable_->rowCount()) return;
    QWidget* w = propsTable_->cellWidget(row, 1);
    if (!w) return;
    w->setFocus(Qt::MouseFocusReason);
    if (auto* le = qobject_cast<QLineEdit*>(w)) le->selectAll();
    else if (auto* sp = qobject_cast<QDoubleSpinBox*>(w)) sp->selectAll();
    else if (auto* cb = qobject_cast<QComboBox*>(w))
        if (cb->isEditable() && cb->lineEdit()) cb->lineEdit()->selectAll();
}

void MainWindow::deferredPropApply(std::function<void()> apply) {
    // Deferred so the field's own signal isn't destroyed mid-apply; after the panel
    // rebuilds, restore focus to whatever field the user has moved into (if any) so a
    // single click on another field lands there ready to edit — and keep the panel
    // scrolled where it was (don't jump to the top).
    QTimer::singleShot(0, this, [this, apply = std::move(apply)] {
        const int row = focusedPropsRow(); // the field the user just clicked into
        const int scroll = propsTable_ && propsTable_->verticalScrollBar()
                               ? propsTable_->verticalScrollBar()->value()
                               : 0;
        applyingPropEdit_ = true; // this rebuild is intentional — don't skip it
        apply();                  // mutate + rebuild the Properties panel
        applyingPropEdit_ = false;
        if (propsTable_ && propsTable_->verticalScrollBar())
            propsTable_->verticalScrollBar()->setValue(scroll);
        refocusPropsRow(row);
    });
}

void MainWindow::refreshProperties() {
    if (!propsTable_) return;
    // Don't rebuild while the user is typing in one of our text fields: an external
    // changed()/selectionChanged() would destroy the field mid-edit (making it jitter).
    // Only a live text/number editor blocks — a focused button (e.g. Pin table) doesn't.
    // The field's own commit re-enters here with applyingPropEdit_ set, so it still applies.
    if (!applyingPropEdit_) {
        QWidget* f = QApplication::focusWidget();
        if (f && propsTable_->isAncestorOf(f) &&
            (qobject_cast<QLineEdit*>(f) || qobject_cast<QDoubleSpinBox*>(f)))
            return;
    }
    refreshing_ = true;
    // Suppress intermediate repaints while the table is torn down and rebuilt row by row
    // (each insertRow/setCellWidget would otherwise paint), which is what made the fields
    // flicker/jitter when changed() fires repeatedly (e.g. dragging a selected object).
    propsTable_->setUpdatesEnabled(false);
    const auto& sel = editor_->selection();
    // Every field in the table is about to be destroyed, so nothing may be left pointing at one.
    sleeveEndFields_.clear();
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
    // THE NAME CELL OF A ROW, with the whole label kept as its tooltip.
    //
    // The key column is 110 pixels wide and a good many labels are longer than that, so what is on
    // screen is the label with its tail cut off - which is fine for "Distance between..." and useless
    // for two rows that differ only in the part that was cut. Widening the column would take the space
    // off the value fields, which are already narrow, so the full text is one hover away instead.
    // `tip` says something the label cannot fit at all; without one the tooltip is the label itself.
    auto putKey = [&](int r, const QString& k, const QString& tip = QString()) {
        auto* key = new QTableWidgetItem(k);
        key->setFlags(Qt::ItemIsEnabled);
        key->setToolTip(tip.isEmpty() ? k : tip);
        propsTable_->setItem(r, 0, key);
    };
    // A FIELD THAT BELONGS TO ONE END OF A SLEEVE. While the keyboard is in it, that end is coloured on
    // the drawing; on the way out the colour goes. Done with a filter rather than a signal because a
    // spin box has no focus signal, and the whole panel is torn down and rebuilt often enough that the
    // map is cleared and refilled on every pass - a pointer left in it would outlive its widget.
    auto watchSleeveEnd = [this](QWidget* w, int end) {
        if (end < 0) return;
        sleeveEndFields_.insert(w, end);
        w->installEventFilter(this);
    };
    auto addRow = [&](const QString& k, const QString& v) {
        const int r = propsTable_->rowCount();
        propsTable_->insertRow(r);
        putKey(r, k);
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
        putKey(r, k);
        auto* cb = new QComboBox(propsTable_);
        cb->setEditable(editable);
        cb->addItems(items);
        if (cb->findText(current) < 0) cb->insertItem(0, current);
        cb->setCurrentText(current);
        connect(cb, &QComboBox::currentTextChanged, this,
                [this, apply, current](const QString& t) {
                    if (refreshing_ || applyingPropEdit_) return;
                    if (t == current) return; // back to what it was: nothing to apply
                    deferredPropApply([apply, t] { apply(t); });
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
        putKey(r, k);
        auto* cb = new QComboBox(propsTable_);
        cb->setIconSize(QSize(72, 14));
        int idx = -1;
        for (int i = 0; i < items.size(); ++i) {
            cb->addItem(items[i].icon, items[i].text, items[i].data);
            if (items[i].data == currentData) idx = i;
        }
        // A value not among the entries — including the "#Value" marker of a
        // multi-selection that disagrees — is shown as its own leading entry, so the row
        // never silently displays the first item instead.
        if (idx < 0) {
            cb->insertItem(0, QIcon(), currentData, currentData);
            idx = 0;
        }
        cb->setCurrentIndex(idx);
        connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
                [this, cb, apply](int i) {
                    if (refreshing_) return;
                    const QString d = cb->itemData(i).toString();
                    deferredPropApply([apply, d] { apply(d); });
                });
        propsTable_->setCellWidget(r, 1, cb);
    };
    // A numeric value cell that applies on edit (deferred).
    // True when a field's value is still exactly what it was built with - nothing to apply.
    // (Comparing values, not "was the widget touched": a value typed back to what it already was
    // is not a change either, and a rebuilt field must never look like an edit.)
    auto unchanged = [](double a, double b) { return std::abs(a - b) < 1e-9; };
    // `tip` is the hover text for the name cell (the label itself, when empty). `sleeveEnd` marks the
    // field as belonging to one end of a heatshrink sleeve: while the keyboard is in it, that end is
    // coloured on the drawing - see watchSleeveEnd.
    auto addNum = [&](const QString& k, double value, std::function<void(double)> apply,
                      const QString& tip = QString(), int sleeveEnd = -1) {
        const int r = propsTable_->rowCount();
        propsTable_->insertRow(r);
        putKey(r, k, tip);
        // A number that VARIES across a multi-selection arrives as NaN: there is no single
        // number to show, so the row shows the "#Value" marker in a plain field. Typing a
        // number over it applies that number to every selected object.
        if (std::isnan(value)) {
            auto* le = new QLineEdit(propsTable_);
            le->setText(QStringLiteral("#Value"));
            const QString built = le->text();
            connect(le, &QLineEdit::editingFinished, this, [this, le, apply, built] {
                if (refreshing_ || applyingPropEdit_) return;
                if (le->text() == built) return; // untouched "#Value": nothing was typed
                bool ok = false;
                const double v = le->text().trimmed().toDouble(&ok);
                if (ok) deferredPropApply([apply, v] { apply(v); });
            });
            watchSleeveEnd(le, sleeveEnd);
            propsTable_->setCellWidget(r, 1, le);
            return;
        }
        auto* sp = new QDoubleSpinBox(propsTable_);
        sp->setRange(-1e9, 1e9);
        sp->setDecimals(2);
        sp->setKeyboardTracking(false); // don't reformat/emit while typing (avoids jitter)
        sp->setValue(value);
        sp->setButtonSymbols(QAbstractSpinBox::NoButtons);
        connect(sp, &QDoubleSpinBox::editingFinished, this,
                [this, sp, apply, value, unchanged] {
                    if (refreshing_ || applyingPropEdit_) return;
                    const double v = sp->value();
                    // Untouched: this is a rebuilt field losing focus, not an edit. Applying
                    // here would push the pre-edit value back over the one just typed.
                    if (unchanged(v, value)) return;
                    deferredPropApply([apply, v] { apply(v); });
                });
        watchSleeveEnd(sp, sleeveEnd);
        propsTable_->setCellWidget(r, 1, sp);
    };
    // A whole-number cell: an amount of something (how many times a marking is repeated, how
    // many pins, how many twists). No decimals, because a count has none.
    auto addInt = [&](const QString& k, int value, std::function<void(int)> apply) {
        const int r = propsTable_->rowCount();
        propsTable_->insertRow(r);
        putKey(r, k);
        auto* sp = new QSpinBox(propsTable_);
        sp->setRange(1, 999);
        sp->setKeyboardTracking(false); // don't reformat / emit while typing
        sp->setValue(value);
        sp->setButtonSymbols(QAbstractSpinBox::NoButtons);
        connect(sp, &QSpinBox::editingFinished, this, [this, sp, apply, value] {
            if (refreshing_ || applyingPropEdit_) return;
            const int v = sp->value();
            if (v == value) return; // a rebuilt field losing focus, not an edit
            deferredPropApply([apply, v] { apply(v); });
        });
        propsTable_->setCellWidget(r, 1, sp);
    };
    // An editable text cell that applies on Enter / focus-out (deferred). `placeholder`
    // (if given) is shown greyed/semi-transparent while the field is empty.
    auto addText = [&](const QString& k, const QString& value,
                       std::function<void(const QString&)> apply,
                       const QString& placeholder = QString()) {
        const int r = propsTable_->rowCount();
        propsTable_->insertRow(r);
        putKey(r, k);
        auto* le = new QLineEdit(propsTable_);
        le->setText(value);
        if (!placeholder.isEmpty()) le->setPlaceholderText(placeholder);
        connect(le, &QLineEdit::editingFinished, this, [this, le, apply, value] {
            if (refreshing_ || applyingPropEdit_) return;
            const QString t = le->text();
            if (t == value) return; // a rebuilt field losing focus, not an edit
            deferredPropApply([apply, t] { apply(t); });
        });
        propsTable_->setCellWidget(r, 1, le);
    };
    // The eye that shows / hides a marking on the drawing. Drawn rather than typed, so it reads
    // the same whatever fonts are installed: an open eye when the marking is drawn, a struck-out
    // one when it is not.
    auto eyeIcon = [](bool shown) {
        QPixmap pm(18, 18);
        pm.fill(Qt::transparent);
        QPainter pp(&pm);
        pp.setRenderHint(QPainter::Antialiasing, true);
        const QColor ink = shown ? QColor("#d7dade") : QColor("#7c828a");
        pp.setPen(QPen(ink, 1.4));
        pp.setBrush(Qt::NoBrush);
        QPainterPath eye; // the lid: two arcs meeting at the corners
        eye.moveTo(2.5, 9);
        eye.quadTo(9, 2.5, 15.5, 9);
        eye.quadTo(9, 15.5, 2.5, 9);
        pp.drawPath(eye);
        if (shown) {
            pp.setBrush(ink);
            pp.drawEllipse(QPointF(9, 9), 2.4, 2.4);
        } else {
            pp.drawLine(QPointF(3, 15), QPointF(15, 3)); // struck out: not drawn
        }
        return QIcon(pm);
    };
    // A value row with its amount and its eye on the end of it. `field` is the editor for the
    // value itself; `amount` (when > 0) is how many times that marking is repeated along the wire.
    auto addWithEye = [this, eyeIcon, putKey](const QString& k, QWidget* field, bool shown,
                                      std::function<void(bool)> applyShown, int amount = -1,
                                      std::function<void(int)> applyAmount = {}) {
        const int r = propsTable_->rowCount();
        propsTable_->insertRow(r);
        putKey(r, k);
        auto* host = new QWidget(propsTable_);
        auto* lay = new QHBoxLayout(host);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(2);
        field->setParent(host);
        lay->addWidget(field, 1);
        if (amount > 0 && applyAmount) {
            auto* amt = new QSpinBox(host);
            amt->setRange(1, 20);
            amt->setValue(amount);
            amt->setKeyboardTracking(false);
            amt->setButtonSymbols(QAbstractSpinBox::NoButtons);
            amt->setFixedWidth(34);
            amt->setAlignment(Qt::AlignCenter);
            amt->setToolTip(QStringLiteral("How many times this is repeated along the wire"));
            connect(amt, &QSpinBox::editingFinished, this, [this, amt, applyAmount, amount] {
                if (refreshing_ || applyingPropEdit_) return;
                const int v = amt->value();
                if (v == amount) return; // a rebuilt field losing focus, not an edit
                deferredPropApply([applyAmount, v] { applyAmount(v); });
            });
            lay->addWidget(amt, 0);
        }
        auto* eye = new QToolButton(host);
        eye->setCheckable(true);
        eye->setChecked(shown);
        eye->setIcon(eyeIcon(shown));
        eye->setIconSize(QSize(16, 16));
        eye->setAutoRaise(true);
        eye->setToolTip(QStringLiteral("Show this on the wire in the drawing"));
        eye->setFocusPolicy(Qt::NoFocus);
        connect(eye, &QToolButton::clicked, this, [this, eye, eyeIcon, applyShown](bool on) {
            if (refreshing_ || applyingPropEdit_) return;
            eye->setIcon(eyeIcon(on));
            deferredPropApply([applyShown, on] { applyShown(on); });
        });
        lay->addWidget(eye, 0);
        propsTable_->setCellWidget(r, 1, host);
    };
    // A text value plus its eye.
    auto addTextEye = [this, addWithEye](const QString& k, const QString& value,
                                        std::function<void(const QString&)> apply, bool shown,
                                        std::function<void(bool)> applyShown, int amount,
                                        std::function<void(int)> applyAmount,
                                        const QString& placeholder = QString()) {
        auto* le = new QLineEdit(propsTable_);
        le->setText(value);
        if (!placeholder.isEmpty()) le->setPlaceholderText(placeholder);
        connect(le, &QLineEdit::editingFinished, this, [this, le, apply, value] {
            if (refreshing_ || applyingPropEdit_) return;
            const QString t = le->text();
            if (t == value) return; // a rebuilt field losing focus, not an edit
            deferredPropApply([apply, t] { apply(t); });
        });
        addWithEye(k, le, shown, applyShown, amount, applyAmount);
    };
    // A choice plus its eye.
    auto addComboEye = [this, addWithEye](const QString& k, const QStringList& items,
                                         const QString& current,
                                         std::function<void(const QString&)> apply, bool shown,
                                         std::function<void(bool)> applyShown, int amount,
                                         std::function<void(int)> applyAmount) {
        auto* box = new QComboBox(propsTable_);
        box->addItems(items);
        if (!current.isEmpty() && box->findText(current) < 0) box->addItem(current);
        box->setCurrentText(current);
        connect(box, &QComboBox::currentTextChanged, this,
                [this, apply, current](const QString& t) {
                    if (refreshing_ || applyingPropEdit_) return;
                    if (t == current) return; // a rebuilt field, not a choice
                    deferredPropApply([apply, t] { apply(t); });
                });
        addWithEye(k, box, shown, applyShown, amount, applyAmount);
    };

    // A row whose value cell is an action button (opens a sub-editor, etc.).
    auto addButton = [&](const QString& k, const QString& text, std::function<void()> onClick) {
        const int r = propsTable_->rowCount();
        propsTable_->insertRow(r);
        putKey(r, k);
        auto* btn = new QPushButton(text, propsTable_);
        connect(btn, &QPushButton::clicked, this, [onClick] { onClick(); });
        propsTable_->setCellWidget(r, 1, btn);
    };

    // The selected objects, and whether they are all the same kind. SEVERAL objects of the
    // same type show that type's full property panel, built from the first of them: every
    // row where they agree shows the shared value, and every row where they disagree shows
    // the "#Value" marker rather than pretending one object's value speaks for all. A
    // mixed-type selection has no common panel, so only the General summary shows.
    // Found through a table of the drawing, not a walk of it per picked object: eighty thousand picked
    // lines walked for one at a time was a minute and a half before the panel appeared.
    std::vector<const Entity*> selEnts;
    selEnts.reserve(sel.size());
    if (sel.size() == 1) {
        for (const auto& ent : editDoc()->entities())
            if (ent.id == sel.front()) { selEnts.push_back(&ent); break; }
    } else if (!sel.empty()) {
        QHash<QString, const Entity*> byId;
        byId.reserve(static_cast<qsizetype>(editDoc()->entities().size()));
        for (const auto& ent : editDoc()->entities())
            if (!byId.contains(ent.id)) byId.insert(ent.id, &ent);   // the first, as the walk found
        for (const auto& id : sel)
            if (const Entity* found = byId.value(id, nullptr)) selEnts.push_back(found);
    }
    bool sameType = !selEnts.empty();
    for (const Entity* se : selEnts)
        if (se->type != selEnts.front()->type) { sameType = false; break; }
    const Entity* e = sameType ? selEnts.front() : nullptr;
    const bool multi = sameType && selEnts.size() > 1;
    // COLOUR, layer, linetype, lineweight: every object has them, whatever kind of object it is. So the
    // General section is built for any selection - `gen` stands for it - while the sections below stay behind
    // `e`, which is only set when the selection is all of one kind. Picking a line and a circle together and
    // being told "2 objects (mixed)" and nothing else meant the commonest thing anybody wants to do to a
    // mixed selection - make it all red - could not be done from the panel at all.
    const Entity* gen = selEnts.empty() ? nullptr : selEnts.front();
    const bool several = selEnts.size() > 1;

    // Reading one property ACROSS the selection. With a single object it is simply that
    // object's value; with several it is the shared value, or "varies" when they differ.
    // Each row renders that as "#Value" — numbers carry it as NaN, since a number field has
    // no spare value that could mean "several different ones".
    struct SelValue {
        QJsonValue v;
        bool varies = false;
        QString toString(const QString& def = QString()) const {
            return varies ? QStringLiteral("#Value") : v.toString(def);
        }
        double toDouble(double def = 0.0) const {
            return varies ? std::numeric_limits<double>::quiet_NaN() : v.toDouble(def);
        }
        int toInt(int def = 0) const { return varies ? 0 : v.toInt(def); }
        bool toBool(bool def = false) const { return v.toBool(def); }
        QJsonArray toArray() const { return v.toArray(); }
        bool contains() const { return !v.isUndefined(); }
        bool isVaries() const { return varies; }
    };
    auto mv = [&selEnts, several](const QString& key) {
        SelValue out;
        if (selEnts.empty()) return out;
        out.v = selEnts.front()->meta.value(key);
        if (several)
            for (const Entity* se : selEnts)
                if (se->meta.value(key) != out.v) { out.varies = true; break; }
        return out;
    };
    // The same for values that live on the entity itself (or are derived from its
    // geometry) rather than in its meta bag: `sv` for text, `nv` for numbers.
    auto sv = [&selEnts, several](const std::function<QString(const Entity&)>& read) -> QString {
        if (selEnts.empty()) return QString();
        const QString out = read(*selEnts.front());
        if (several)
            for (const Entity* se : selEnts)
                if (read(*se) != out) return QStringLiteral("#Value");
        return out;
    };
    auto nv = [&selEnts, several](const std::function<double(const Entity&)>& read) -> double {
        if (selEnts.empty()) return 0.0;
        const double out = read(*selEnts.front());
        if (several)
            for (const Entity* se : selEnts)
                if (std::abs(read(*se) - out) > 1e-9)
                    return std::numeric_limits<double>::quiet_NaN();
        return out;
    };
    // A boolean property shown as Yes/No — or "#Value" when the selection disagrees.
    auto yesNo = [](const SelValue& val, bool def = false) -> QString {
        return val.isVaries() ? QStringLiteral("#Value")
                              : (val.toBool(def) ? QStringLiteral("Yes") : QStringLiteral("No"));
    };

    addHeader(QStringLiteral("General"));
    addRow(QStringLiteral("Type"),
           sel.empty() ? QStringLiteral("No selection")
                       : (multi ? QStringLiteral("%1 x %2")
                                      .arg(sel.size())
                                      .arg(entityTypeDisplayName(e->type))
                                : (e ? entityTypeDisplayName(e->type)
                                     : QStringLiteral("%1 objects (mixed)").arg(sel.size()))));
    if (gen) {
        QStringList layerNames;
        for (const auto& l : doc_->layers()) layerNames << l.name;
        static const QHash<QString, QString> hexToColor = {
            {"#ffffff", "White"}, {"#ff0000", "Red"}, {"#ffff00", "Yellow"},
            {"#00ff00", "Green"}, {"#00ffff", "Cyan"}, {"#0000ff", "Blue"},
            {"#ff00ff", "Magenta"}};

        // --- Color (swatch preview + Select Color...) ---
        const QString curColor = sv([](const Entity& x) { // hexToColor is static: no capture
            return x.hasExplicitColor() ? hexToColor.value(x.color.toLower(), x.color)
                                        : QStringLiteral("ByLayer");
        });
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
        if (gen->hasExplicitColor() && !hexToColor.contains(gen->color.toLower()))
            colorItems.push_front({gen->color, colorSwatchIcon(QColor(gen->color)), gen->color});
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

        addCombo(QStringLiteral("Layer"), layerNames,
                 sv([](const Entity& x) { return x.layer; }), false,
                 [this](const QString& t) { editor_->setSelectionLayer(t); });

        // --- Linetype (dash-pattern preview) ---
        const QStringList ltNames = {"ByLayer",    "ByBlock", "CENTER", "CENTERX2",
                                     "Continuous", "DOT2",    "HIDDEN", "PHANTOM"};
        const QString curLt = mv("linetype").toString("ByLayer");
        QVector<ComboEntry> ltItems;
        for (const auto& n : ltNames) ltItems.push_back({n, linetypeIcon(n), n});
        if (!ltNames.contains(curLt))
            ltItems.push_front({curLt, linetypeIcon(curLt), curLt});
        addIconCombo(QStringLiteral("Linetype"), ltItems, curLt, [this](const QString& d) {
            editor_->setSelectionMeta(QStringLiteral("linetype"), d);
        });

        addCombo(QStringLiteral("Linetype scale"), {"0.25", "0.5", "1.0", "2.0", "5.0"},
                 mv("ltscale").toString("1.0"), true,
                 [this](const QString& t) { editor_->setSelectionMeta("ltscale", t); });
        addCombo(QStringLiteral("Plot style"), {"ByColor", "Normal", "Screening"},
                 mv("plotStyle").toString("ByColor"), false,
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
        const QString curLw = sv([](const Entity& x) {
            return x.weight > 0 ? QString::number(x.weight, 'f', 2) : QStringLiteral("ByLayer");
        });
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
        // Change one field of every selected connector's spec, in place. Editing a field
        // with several connectors selected changes just that field on each of them.
        auto edit = [this](std::function<void(ConnectorSpec&)> mut) {
            editor_->editSelectedConnectorSpecs(mut);
            if (canvas_) canvas_->update();
        };
        const ConnectorSpec cs = connectorSpecFromMeta(e->meta);
        // Every spec field is stored in meta under its own name, so variance across a
        // multi-selection is read straight from there: csText/csNum hand back the shared
        // value or the "#Value" marker (NaN for numbers), and csPick does the same for the
        // fields shown through a friendly label.
        auto csText = [&mv, multi](const char* key, const QString& own) {
            return multi ? mv(QString::fromLatin1(key)).toString(own) : own;
        };
        auto csNum = [&mv, multi](const char* key, double own) {
            return multi ? mv(QString::fromLatin1(key)).toDouble(own) : own;
        };
        auto csPick = [&mv, multi](const char* key, const QString& label) {
            return (multi && mv(QString::fromLatin1(key)).isVaries()) ? QStringLiteral("#Value")
                                                                     : label;
        };
        const QStringList fonts = FontLibrary::families();
        // With no designator, show the validation fallback name (#<entNum>) as a
        // semi-transparent placeholder so it matches what Validate reports.
        const QString refdesHint = QStringLiteral("#%1").arg(mv("entNum").toInt());
        // Each with an eye on the end of the row: whether it is DRAWN on the sheet. The same connector
        // wants its designator on a schematic and often not on the mechanical view beside it, and a
        // crowded corner is tidied by turning one off rather than by dragging it somewhere it does not
        // belong. The text stays either way - the eye hides it, it does not delete it.
        addTextEye(QStringLiteral("Designator"), csText("designator", cs.designator),
                   [edit](const QString& t) { edit([t](ConnectorSpec& s) { s.designator = t; }); },
                   cs.showDesignator,
                   [edit](bool on) { edit([on](ConnectorSpec& s) { s.showDesignator = on; }); }, -1, {},
                   refdesHint);
        addTextEye(QStringLiteral("Part number"), csText("partNumber", cs.partNumber),
                   [edit](const QString& t) { edit([t](ConnectorSpec& s) { s.partNumber = t; }); },
                   cs.showPartNumber,
                   [edit](bool on) { edit([on](ConnectorSpec& s) { s.showPartNumber = on; }); }, -1, {});
        // Per-pin wire cross-section (AWG/mm²) + ampere editor. It edits one connector's
        // own pin list, so with several selected there is no single table to open.
        if (multi)
            addRow(QStringLiteral("Pin wiring"), QStringLiteral("select one connector"));
        else
            addButton(QStringLiteral("Pin wiring"), QStringLiteral("Pin table…"),
                      [this] { showPinTable(editor_->selectedConnectorId()); });
        addInt(QStringLiteral("Pins"), static_cast<int>(csNum("pinCount", cs.pinCount)),
               [edit](int v) {
                   edit([v](ConnectorSpec& s) { s.pinCount = std::clamp(v, 1, 128); });
               });
        {
            // "Mixed" names the pins A1..A10, B1..B10 - a letter per block of ten.
            auto annName = [](const QString& mode) {
                if (mode == QLatin1String("letters")) return QStringLiteral("Letters");
                if (mode == QLatin1String("mixed")) return QStringLiteral("Mixed");
                return QStringLiteral("Numbers");
            };
            addCombo(QStringLiteral("Labels"),
                     {QStringLiteral("Numbers"), QStringLiteral("Letters"),
                      QStringLiteral("Mixed")},
                     csPick("annotation", annName(cs.annotation)), false,
                     [edit](const QString& t) {
                         edit([t](ConnectorSpec& s) {
                             s.annotation = t == QLatin1String("Letters")
                                                ? QStringLiteral("letters")
                                                : (t == QLatin1String("Mixed")
                                                       ? QStringLiteral("mixed")
                                                       : QStringLiteral("numbers"));
                         });
                     });
        }
        addCombo(QStringLiteral("Order"), {QStringLiteral("Top"), QStringLiteral("Bottom")},
                 csPick("pinOrder", cs.pinOrder == QLatin1String("bottom")
                                        ? QStringLiteral("Bottom")
                                        : QStringLiteral("Top")),
                 false, [edit](const QString& t) {
                     edit([t](ConnectorSpec& s) {
                         s.pinOrder = t == QLatin1String("Bottom") ? QStringLiteral("bottom")
                                                                  : QStringLiteral("top");
                     });
                 });
        addNum(QStringLiteral("Pitch"), csNum("pitch", cs.pitch),
               [edit](double v) { edit([v](ConnectorSpec& s) { s.pitch = std::max(1.0, v); }); });
        addNum(QStringLiteral("Pin length"), csNum("pinLength", cs.pinLength),
               [edit](double v) { edit([v](ConnectorSpec& s) { s.pinLength = std::max(1.0, v); }); });
        addCombo(QStringLiteral("Body"), {QStringLiteral("Dynamic"), QStringLiteral("Custom")},
                 csPick("rectMode", cs.rectMode == QLatin1String("custom")
                                        ? QStringLiteral("Custom")
                                        : QStringLiteral("Dynamic")),
                 false, [edit](const QString& t) {
                     edit([t](ConnectorSpec& s) {
                         s.rectMode = t == QLatin1String("Custom") ? QStringLiteral("custom")
                                                                  : QStringLiteral("dynamic");
                     });
                 });
        addNum(QStringLiteral("Width"), csNum("rectWidth", cs.rectWidth), [edit](double v) {
            edit([v](ConnectorSpec& s) { s.rectMode = QStringLiteral("custom"); s.rectWidth = std::max(1.0, v); });
        });
        addNum(QStringLiteral("Height"), csNum("rectHeight", cs.rectHeight), [edit](double v) {
            edit([v](ConnectorSpec& s) { s.rectMode = QStringLiteral("custom"); s.rectHeight = std::max(1.0, v); });
        });
        addCombo(QStringLiteral("Associative"), {QStringLiteral("Yes"), QStringLiteral("No")},
                 cs.associative ? QStringLiteral("Yes") : QStringLiteral("No"), false,
                 [edit](const QString& t) {
                     edit([t](ConnectorSpec& s) { s.associative = (t == QLatin1String("Yes")); });
                 });
        addCombo(QStringLiteral("Pin font"), fonts, csText("pinFont", cs.pinFont), false,
                 [edit](const QString& t) { edit([t](ConnectorSpec& s) { s.pinFont = t; }); });
        addNum(QStringLiteral("Pin size"), csNum("pinSize", cs.pinSize),
               [edit](double v) { edit([v](ConnectorSpec& s) { s.pinSize = std::max(1.0, v); }); });
        addCombo(QStringLiteral("Des. font"), fonts, csText("desFont", cs.desFont), false,
                 [edit](const QString& t) { edit([t](ConnectorSpec& s) { s.desFont = t; }); });
        addNum(QStringLiteral("Des. size"), csNum("desSize", cs.desSize),
               [edit](double v) { edit([v](ConnectorSpec& s) { s.desSize = std::max(1.0, v); }); });
        addCombo(QStringLiteral("P/N font"), fonts, csText("pnFont", cs.pnFont), false,
                 [edit](const QString& t) { edit([t](ConnectorSpec& s) { s.pnFont = t; }); });
        addNum(QStringLiteral("P/N size"), csNum("pnSize", cs.pnSize),
               [edit](double v) { edit([v](ConnectorSpec& s) { s.pnSize = std::max(1.0, v); }); });
    }

    // A selected harness run is parametric: the route is its points, and these values are what the
    // wires are laid out from. Changing either re-lays the same object rather than asking for it to be
    // drawn again.
    if (e && e->type == EntityType::TwistedPair) {
        addHeader(QStringLiteral("Harness run"));
        // WHAT THE RUN IN FRONT OF YOU IS ACTUALLY DRAWN WITH. A figure nobody set is not nought - it
        // is whatever the drawing made of it, and nought is what the panel showed: a fan pitch of 0
        // on a run whose wires are plainly 4.5 apart, and the same for the wire pitch, the fan
        // length, the wire width and the rest. Reading a value out of the drawing is the whole of
        // what this panel is for, so each field falls back to the figure the drawing worked out.
        const TwistFigures drawn = twistedPairFigures(*e);
        addNum(QStringLiteral("Wires"), mv(QStringLiteral("twistWires")).toDouble(2.0),
               [this](double v) {
                   editor_->setSelectionMetaNumber(
                       QStringLiteral("twistWires"),
                       std::clamp(static_cast<int>(std::lround(v)), 2, kTwistedMaxWires));
               });
        // WHICH END FANS, in every way the drawing can say it: as a side of the sheet, in the order
        // the run was drawn, or both ends. The ribbon offers start and finish, so this has to as
        // well - a run that fans at one end was shown here as "Both", which is the one answer that
        // is certainly wrong.
        const QString spread = twistedPairSpread(*e);
        const QString shown = spread == QLatin1String("left")     ? QStringLiteral("Left")
                              : spread == QLatin1String("right")  ? QStringLiteral("Right")
                              : spread == QLatin1String("start")  ? QStringLiteral("Start end")
                              : spread == QLatin1String("finish") ? QStringLiteral("Finish end")
                                                                  : QStringLiteral("Both");
        const QString current = mv(QStringLiteral("twistSpread")).isVaries()
                                    ? QStringLiteral("#Value")
                                    : shown;
        addCombo(QStringLiteral("Spread wires"),
                 {QStringLiteral("Left"), QStringLiteral("Right"), QStringLiteral("Start end"),
                  QStringLiteral("Finish end"), QStringLiteral("Both")},
                 current, false, [this](const QString& t) {
                     const QString said = t == QLatin1String("Start end") ? QStringLiteral("start")
                                          : t == QLatin1String("Finish end")
                                              ? QStringLiteral("finish")
                                              : t.toLower();
                     editor_->setSelectionMeta(QStringLiteral("twistSpread"), said);
                 });
        // WHICH LAY, and it can be changed here. The two are one object - the same route, the same
        // count, the same ends - so a run drawn straight that turns out to be twisted after all is a
        // value on it rather than a thing to delete and draw again.
        const bool straight = e->meta.value(QStringLiteral("twistStraight")).toBool();
        // BUT ONLY WHILE THE RUN IS ITS WIRES. Under a cable or a sleeve there are no wires on the
        // sheet to lay up or to converge: what is drawn between the fans is the cover, and both
        // questions are about lines that are not there. Asked anyway, they offered a setting that
        // changed nothing you could see.
        const RunBody bodyKind = runBodyOf(*e);
        const bool bareWires = bodyKind == RunBody::Wires;
        if (bareWires) {
            addCombo(QStringLiteral("Lay"),
                     {QStringLiteral("Straight"), QStringLiteral("Laid up")},
                     mv(QStringLiteral("twistStraight")).isVaries()
                         ? QStringLiteral("#Value")
                         : (straight ? QStringLiteral("Straight") : QStringLiteral("Laid up")),
                     false, [this](const QString& t) {
                         editor_->setSelectionMetaBool(QStringLiteral("twistStraight"),
                                                       t == QLatin1String("Straight"));
                     });
        }
        // And the two a straight run lays itself out from: how far apart the spread wires sit, and
        // how many of them carry on through the bundle.
        if (straight) {
            // PITCH, which is what this drawing calls how far apart a repeated thing sits, every
            // other place it says it.
            addNum(QStringLiteral("Fan pitch"),
                   mv(QStringLiteral("twistLane")).toDouble(drawn.lane), [this](double v) {
                       editor_->setSelectionMetaNumber(QStringLiteral("twistLane"),
                                                       std::max(0.0, v));
                   });
            if (bareWires) {
                addNum(QStringLiteral("Converge to"),
                       mv(QStringLiteral("twistTrunkWires")).toDouble(drawn.trunk),
                       [this](double v) {
                           editor_->setSelectionMetaNumber(
                               QStringLiteral("twistTrunkWires"),
                               std::clamp(static_cast<int>(std::lround(v)), 1, kTwistedMaxWires));
                       });
                // And how far apart they run where the bundle is gathered - a hook-up wires figure,
                // for the same reason: under a cover there are no wires on the sheet to space.
                addNum(QStringLiteral("Wire pitch"),
                       mv(QStringLiteral("twistBodyPitch")).toDouble(drawn.packed),
                       [this](double v) {
                           editor_->setSelectionMetaNumber(QStringLiteral("twistBodyPitch"),
                                                           std::max(0.0, v));
                       });
            }
        }
        // WHAT FINISHES A BODY END, which is there only when one end is body and the other fans.
        // The same two the ribbon asks, and asked of the same sorts: a transition is a part fitted
        // over the end of a CABLE, and recovered-or-square is what there is to say about the end of
        // a SLEEVE.
        const bool oneEnd = spread == QLatin1String("start") || spread == QLatin1String("finish") ||
                            spread == QLatin1String("left") || spread == QLatin1String("right");
        if (oneEnd && bodyKind == RunBody::Cable) {
            addCombo(QStringLiteral("Body end"),
                     {QStringLiteral("No transition"), QStringLiteral("With transition")},
                     mv(QStringLiteral("twistEndBoot")).isVaries()
                         ? QStringLiteral("#Value")
                         : (e->meta.value(QStringLiteral("twistEndBoot")).toBool()
                                ? QStringLiteral("With transition")
                                : QStringLiteral("No transition")),
                     false, [this](const QString& t) {
                         editor_->setSelectionMetaBool(QStringLiteral("twistEndBoot"),
                                                       t.startsWith(QLatin1String("With")));
                     });
        }
        if (oneEnd && bodyKind == RunBody::Heatshrink) {
            addCombo(QStringLiteral("Body end"),
                     {QStringLiteral("Recovered"), QStringLiteral("Not recovered")},
                     mv(QStringLiteral("twistEndBootRecovered")).isVaries()
                         ? QStringLiteral("#Value")
                         : (e->meta.value(QStringLiteral("twistEndBootRecovered")).toBool(true)
                                ? QStringLiteral("Recovered")
                                : QStringLiteral("Not recovered")),
                     false, [this](const QString& t) {
                         editor_->setSelectionMetaBool(QStringLiteral("twistEndBootRecovered"),
                                                       t == QLatin1String("Recovered"));
                     });
        }
        // AND EVERYTHING THE RIBBON OFFERS WHILE ONE IS BEING DRAWN, because the ribbon fields are
        // the settings the NEXT run is drawn with and these are the values on the run in front of
        // you. Every one of them was settable up to the moment the run was placed and not one
        // afterwards, so getting a fan length wrong meant deleting the run and drawing it again.
        //
        // Each shows where it means something, the same as on the ribbon: the fan belongs to both
        // lays, the wire and the fan's corners are the straight run's - a laid-up bundle takes its
        // thickness from the cable and its wires cross the whole way - and the knee radius belongs
        // to a routed run, because a run between two points has no knee to turn at.
        // A RADIUS BELOW NOUGHT IS THE DRAWING'S OWN and nought is a sharp corner, which is the same
        // thing the ribbon's Auto says - a drawing that wants a knee shown as a corner can say so,
        // and one that says nothing still gets the bend it always got.
        const QString autoTip = QStringLiteral(
            "-1 leaves it to the drawing; 0 is a sharp corner; anything above is the radius.");
        addNum(QStringLiteral("Fan length"), mv(QStringLiteral("twistEnds")).toDouble(drawn.fanLen),
               [this](double v) {
                   editor_->setSelectionMetaNumber(QStringLiteral("twistEnds"), std::max(0.0, v));
               });
        if (straight) {
            addNum(QStringLiteral("Wire width"),
                   mv(QStringLiteral("twistWireWidth")).toDouble(drawn.wireWidth),
                   [this](double v) {
                       editor_->setSelectionMetaNumber(QStringLiteral("twistWireWidth"),
                                                       std::max(0.0, v));
                   });
            addNum(QStringLiteral("Fan fillet"), mv(QStringLiteral("twistFanKnee")).toDouble(drawn.fanKnee),
                   [this](double v) {
                       editor_->setSelectionMetaNumber(QStringLiteral("twistFanKnee"),
                                                       std::max(-1.0, v));
                   }, autoTip);
            // THE FLUSH END, and how long it is: the straight stretch where every wire is parallel
            // to the run again, so every cut end lines up square across the fan.
            const bool flushing = !e->meta.contains(QStringLiteral("twistFlush")) ||
                                  e->meta.value(QStringLiteral("twistFlush")).toBool();
            addCombo(QStringLiteral("Flush ends"),
                     {QStringLiteral("Yes"), QStringLiteral("No")},
                     mv(QStringLiteral("twistFlush")).isVaries()
                         ? QStringLiteral("#Value")
                         : (flushing ? QStringLiteral("Yes") : QStringLiteral("No")),
                     false, [this](const QString& t) {
                         editor_->setSelectionMetaBool(QStringLiteral("twistFlush"),
                                                       t == QLatin1String("Yes"));
                     });
            if (flushing)
                addNum(QStringLiteral("Flush length"),
                       mv(QStringLiteral("twistFlushLen")).toDouble(drawn.flushLen),
                       [this](double v) {
                           editor_->setSelectionMetaNumber(QStringLiteral("twistFlushLen"),
                                                           std::max(0.0, v));
                       });
            // WHETHER THE CONVERGED STRETCH IS A CABLE under heatshrink, or the lines its wires make.
            const RunBody kind = runBodyOf(*e);
            const bool covered = kind != RunBody::Wires;
            const QString kindName = kind == RunBody::Cable        ? QStringLiteral("Cable")
                                     : kind == RunBody::Heatshrink ? QStringLiteral("Hshrink")
                                                                   : QStringLiteral("Hook-up wires");
            addCombo(QStringLiteral("Between the fans"),
                     {QStringLiteral("Hook-up wires"), QStringLiteral("Cable"),
                      QStringLiteral("Hshrink")},
                     mv(QStringLiteral("twistBody")).isVaries() ? QStringLiteral("#Value") : kindName,
                     false, [this](const QString& t) {
                         editor_->setSelectionMeta(
                             QStringLiteral("twistBody"),
                             t == QLatin1String("Cable")     ? QStringLiteral("cable")
                             : t == QLatin1String("Hshrink") ? QStringLiteral("heatshrink")
                                                             : QString());
                     });
            // AND WHAT THAT CABLE IS DRAWN TO, while there is one: how wide, and at each end how far
            // across its face is and over what length it comes down to that.
            if (covered) {
                addNum(QStringLiteral("Body width"),
                       mv(QStringLiteral("twistBodyWidth")).toDouble(drawn.bodyWidth),
                       [this](double v) {
                           editor_->setSelectionMetaNumber(QStringLiteral("twistBodyWidth"),
                                                           std::max(0.0, v));
                       });
                // A cable is cut square and has a boot at each end; a sleeve is shrunk onto what it
                // grips and has none. Each sort is offered its own figures and not the other's.
                if (kind == RunBody::Cable)
                    addNum(QStringLiteral("Hshrink length"),
                           mv(QStringLiteral("twistBoot")).toDouble(drawn.boot),
                           [this](double v) {
                               editor_->setSelectionMetaNumber(QStringLiteral("twistBoot"),
                                                               std::max(0.0, v));
                           });
                if (kind == RunBody::Heatshrink) {
                addNum(QStringLiteral("Entry size"), mv(QStringLiteral("twistBodyFace")).toDouble(drawn.bodyFace),
                       [this](double v) {
                           editor_->setSelectionMetaNumber(QStringLiteral("twistBodyFace"),
                                                           std::max(0.0, v));
                       });
                addNum(QStringLiteral("Hshrink recover length"),
                       mv(QStringLiteral("twistBodyShrink")).toDouble(drawn.bodyShrink),
                       [this](double v) {
                           editor_->setSelectionMetaNumber(QStringLiteral("twistBodyShrink"),
                                                           std::max(-1.0, v));
                       }, autoTip);
                }
                const bool bothEnds =
                    !e->meta.contains(QStringLiteral("twistBodySidesSame")) ||
                    e->meta.value(QStringLiteral("twistBodySidesSame")).toBool();
                addCombo(QStringLiteral("Cable both sides"),
                         {QStringLiteral("Alike"), QStringLiteral("Each its own")},
                         mv(QStringLiteral("twistBodySidesSame")).isVaries()
                             ? QStringLiteral("#Value")
                             : (bothEnds ? QStringLiteral("Alike")
                                         : QStringLiteral("Each its own")),
                         false, [this](const QString& t) {
                             editor_->setSelectionMetaBool(QStringLiteral("twistBodySidesSame"),
                                                           t == QLatin1String("Alike"));
                         });
                if (!bothEnds && kind == RunBody::Cable)
                    addNum(QStringLiteral("Finish boot"),
                           mv(QStringLiteral("twistBootFinish")).toDouble(0.0), [this](double v) {
                               editor_->setSelectionMetaNumber(QStringLiteral("twistBootFinish"),
                                                               std::max(0.0, v));
                           });
                if (!bothEnds && kind == RunBody::Heatshrink) {
                    addNum(QStringLiteral("Finish size"),
                           mv(QStringLiteral("twistBodyFaceFinish")).toDouble(0.0),
                           [this](double v) {
                               editor_->setSelectionMetaNumber(
                                   QStringLiteral("twistBodyFaceFinish"), std::max(0.0, v));
                           });
                    addNum(QStringLiteral("Finish shrink"),
                           mv(QStringLiteral("twistBodyShrinkFinish")).toDouble(-1.0),
                           [this](double v) {
                               editor_->setSelectionMetaNumber(
                                   QStringLiteral("twistBodyShrinkFinish"), std::max(-1.0, v));
                           }, autoTip);
                }
            }
            // AND WHETHER THE ENDS ARE STRIPPED, and by how much: the insulation off the last of
            // each wire, back to bare conductor, which is how a wire is landed in a contact.
            const bool stripping = e->meta.value(QStringLiteral("twistStrip")).toBool();
            addCombo(QStringLiteral("Stripped ends"),
                     {QStringLiteral("Yes"), QStringLiteral("No")},
                     mv(QStringLiteral("twistStrip")).isVaries()
                         ? QStringLiteral("#Value")
                         : (stripping ? QStringLiteral("Yes") : QStringLiteral("No")),
                     false, [this](const QString& t) {
                         editor_->setSelectionMetaBool(QStringLiteral("twistStrip"),
                                                       t == QLatin1String("Yes"));
                     });
            if (stripping)
                addNum(QStringLiteral("Strip length"),
                       mv(QStringLiteral("twistStripLen")).toDouble(0.0), [this](double v) {
                           editor_->setSelectionMetaNumber(QStringLiteral("twistStripLen"),
                                                           std::max(0.0, v));
                       });
        }
        if (e->points.size() > 2)
            addNum(QStringLiteral("Knee radius"), mv(QStringLiteral("twistKnee")).toDouble(-1.0),
                   [this](double v) {
                       editor_->setSelectionMetaNumber(QStringLiteral("twistKnee"),
                                                       std::max(-1.0, v));
                   }, autoTip);
        // AND THE FINISH END, when it is not simply repeating the start. The run leaves one thing
        // and arrives at another; the tick says whether that shows in how it fans.
        //
        // ONLY WHERE THERE ARE TWO ENDS THAT FAN. A run that fans at one end - a fanout, or any run
        // whose other end is body - has no second fan to be alike or unlike, and the three fields
        // under this one describe a fan that is not there.
        if (straight && !oneEnd) {
            const bool same = !e->meta.contains(QStringLiteral("twistSidesSame")) ||
                              e->meta.value(QStringLiteral("twistSidesSame")).toBool();
            addCombo(QStringLiteral("Both sides"),
                     {QStringLiteral("Alike"), QStringLiteral("Each its own")},
                     mv(QStringLiteral("twistSidesSame")).isVaries()
                         ? QStringLiteral("#Value")
                         : (same ? QStringLiteral("Alike") : QStringLiteral("Each its own")),
                     false, [this](const QString& t) {
                         editor_->setSelectionMetaBool(QStringLiteral("twistSidesSame"),
                                                       t == QLatin1String("Alike"));
                     });
            if (!same) {
                addNum(QStringLiteral("Finish pitch"),
                       mv(QStringLiteral("twistLaneFinish")).toDouble(drawn.lane), [this](double v) {
                           editor_->setSelectionMetaNumber(QStringLiteral("twistLaneFinish"),
                                                           std::max(0.0, v));
                       });
                addNum(QStringLiteral("Finish fan"),
                       mv(QStringLiteral("twistEndsFinish")).toDouble(drawn.fanLen), [this](double v) {
                           editor_->setSelectionMetaNumber(QStringLiteral("twistEndsFinish"),
                                                           std::max(0.0, v));
                       });
                addNum(QStringLiteral("Finish knee"),
                       mv(QStringLiteral("twistFanKneeFinish")).toDouble(drawn.fanKnee), [this](double v) {
                           editor_->setSelectionMetaNumber(QStringLiteral("twistFanKneeFinish"),
                                                           std::max(-1.0, v));
                       }, autoTip);
            }
        }
    }

    // A selected wire shows its harness attributes (number, gauge, colour, insulation,
    // conductor type/strand) plus its endpoints' connections.
    if (e && e->type == EntityType::Wire) {
        addHeader(QStringLiteral("Wire"));
        addRow(QStringLiteral("Number"), sv([](const Entity& x) {
                   return QStringLiteral("#%1").arg(x.meta.value(QStringLiteral("entNum")).toInt());
               }));
        // From / To: the connector pin each end lands on; a splice it joins; "open" for
        // an end deliberately terminated with an X marker; "unconnected" for an end that
        // touches nothing at all.
        addRow(QStringLiteral("From"),
               sv([this](const Entity& x) { return editor_->wireEndRef(x, true); }));
        addRow(QStringLiteral("To"),
               sv([this](const Entity& x) { return editor_->wireEndRef(x, false); }));
        // Shielded / twisted: read back from the Screen / Twist annotations covering
        // this wire (they are what actually make it shielded or twisted, so these are
        // reported, not edited here).
        addRow(QStringLiteral("Shielded"), sv([this](const Entity& x) {
                   return editor_->wireIsShielded(x.id) ? QStringLiteral("Yes")
                                                        : QStringLiteral("No");
               }));
        addRow(QStringLiteral("Twisted"), sv([this](const Entity& x) {
                   return editor_->wireIsTwisted(x.id) ? QStringLiteral("Yes")
                                                       : QStringLiteral("No");
               }));
        // Wire label: free text identifying the wire. Placing a Wire label with the
        // ribbon tool onto this wire writes the same field, and editing it here updates
        // any label already attached.
        // Each marking: the value, how many times it repeats, and the eye that shows or hides
        // it - followed by how far above the wire it sits. All three are independent markings.
        addTextEye(QStringLiteral("Wire label"), mv(QStringLiteral("wireLabel")).toString(),
                   [this](const QString& t) { editor_->setSelectionWireLabel(t); },
                   e->meta.value(QStringLiteral("showWireLabel")).toBool(true),
                   [this](bool on) {
                       editor_->setSelectionMetaBool(QStringLiteral("showWireLabel"), on);
                   },
                   std::max(1, wireAnnotCountFor(*e, QStringLiteral("label"))),
                   [this](int n) {
                       editor_->setSelectionWireAnnotCountFor(QStringLiteral("label"), n);
                   },
                   QStringLiteral("e.g. W12 / PWR-A"));
        addNum(QStringLiteral("Wire label height"),
               wireAnnotSizeFor(*e, QStringLiteral("label")), [this](double v) {
                   editor_->setSelectionWireAnnotSize(QStringLiteral("label"), v);
               });
        addNum(QStringLiteral("Wire label gap"),
               wireAnnotGapFor(*e, QStringLiteral("label")), [this](double v) {
                   editor_->setSelectionWireAnnotGap(QStringLiteral("label"), v);
               });
        // AWG cross-section, 4 (thick) … 32 (thin).
        QStringList awgList;
        for (int g = 4; g <= 32; g += 2) awgList << QStringLiteral("%1 AWG").arg(g);
        const QString curAwg = mv(QStringLiteral("awg")).toString();
        addComboEye(QStringLiteral("AWG"), awgList, curAwg,
                    [this](const QString& t) {
                        editor_->setSelectionMeta(QStringLiteral("awg"), t);
                    },
                    wireShowsGaugeName(*e), [this](bool on) {
                        editor_->setSelectionMetaBool(QStringLiteral("showGaugeName"), on);
                    },
                    std::max(1, wireAnnotCountFor(*e, QStringLiteral("gauge"))),
                    [this](int n) {
                        editor_->setSelectionWireAnnotCountFor(QStringLiteral("gauge"), n);
                    });
        addNum(QStringLiteral("AWG height"), wireAnnotSizeFor(*e, QStringLiteral("gauge")),
               [this](double v) {
                   editor_->setSelectionWireAnnotSize(QStringLiteral("gauge"), v);
               });
        addNum(QStringLiteral("AWG gap"), wireAnnotGapFor(*e, QStringLiteral("gauge")),
               [this](double v) {
                   editor_->setSelectionWireAnnotGap(QStringLiteral("gauge"), v);
               });
        // Wire colour code (WH, BK, …).
        addComboEye(QStringLiteral("Wire color"), wireColorCodes(),
                    mv(QStringLiteral("wireColor")).toString(QStringLiteral("WH")),
                    [this](const QString& t) {
                        editor_->setSelectionMeta(QStringLiteral("wireColor"), t);
                        if (canvas_) canvas_->update();
                    },
                    wireShowsColorName(*e), [this](bool on) {
                        editor_->setSelectionMetaBool(QStringLiteral("showColorName"), on);
                    },
                    std::max(1, wireAnnotCountFor(*e, QStringLiteral("color"))),
                    [this](int n) {
                        editor_->setSelectionWireAnnotCountFor(QStringLiteral("color"), n);
                    });
        addNum(QStringLiteral("Wire color height"), wireAnnotSizeFor(*e, QStringLiteral("color")),
               [this](double v) {
                   editor_->setSelectionWireAnnotSize(QStringLiteral("color"), v);
               });
        addNum(QStringLiteral("Wire color gap"), wireAnnotGapFor(*e, QStringLiteral("color")),
               [this](double v) {
                   editor_->setSelectionWireAnnotGap(QStringLiteral("color"), v);
               });
        // On a vertical run: which side of the wire the markings sit, and which way they read.
        // Neither follows from the geometry, so both are asked for rather than guessed.
        addCombo(QStringLiteral("Vertical side"),
                 {QStringLiteral("Left"), QStringLiteral("Right")},
                 wireAnnotVerticalSide(*e) == QLatin1String("right") ? QStringLiteral("Right")
                                                                     : QStringLiteral("Left"),
                 false, [this](const QString& t) {
                     editor_->setSelectionMeta(QStringLiteral("annVertSide"),
                                               t == QLatin1String("Right")
                                                   ? QStringLiteral("right")
                                                   : QStringLiteral("left"));
                 });
        addCombo(QStringLiteral("Vertical text"), {QStringLiteral("Up"), QStringLiteral("Down")},
                 wireAnnotVerticalText(*e) == QLatin1String("down") ? QStringLiteral("Down")
                                                                    : QStringLiteral("Up"),
                 false, [this](const QString& t) {
                     editor_->setSelectionMeta(QStringLiteral("annVertText"),
                                               t == QLatin1String("Down") ? QStringLiteral("down")
                                                                          : QStringLiteral("up"));
                 });
        // What the conductor is made of. Copper unless the drawing says otherwise; the list is
        // editable, because a harness may call for a metal that is not on it.
        addCombo(QStringLiteral("Wire material"),
                 {QStringLiteral("Copper"), QStringLiteral("Tinned copper"),
                  QStringLiteral("Silver-plated copper"), QStringLiteral("Nickel-plated copper"),
                  QStringLiteral("Copper-clad aluminium"), QStringLiteral("Aluminium"),
                  QStringLiteral("Copper alloy"), QStringLiteral("Steel")},
                 mv(QStringLiteral("wireMaterial")).toString(QStringLiteral("Copper")), true,
                 [this](const QString& t) {
                     editor_->setSelectionMeta(QStringLiteral("wireMaterial"), t);
                 });
        // Insulation type (editable — user may enter any material).
        addCombo(QStringLiteral("Insulation"),
                 {"PVC", "XLPE", "PTFE", "FEP", "Silicone", "Rubber", "Polyethylene"},
                 mv(QStringLiteral("insulation")).toString(), true, [this](const QString& t) {
                     editor_->setSelectionMeta(QStringLiteral("insulation"), t);
                 });
        // Insulation voltage rating.
        addCombo(QStringLiteral("Voltage"), {"30V", "60V", "150V", "300V", "600V", "1000V"},
                 mv(QStringLiteral("insulationVoltage")).toString(), true,
                 [this](const QString& t) {
                     editor_->setSelectionMeta(QStringLiteral("insulationVoltage"), t);
                 });
        // Conductor type: solid / stranded.
        addCombo(QStringLiteral("Type"), {QStringLiteral("Solid"), QStringLiteral("Stranded")},
                 mv(QStringLiteral("conductorType")).toString(QStringLiteral("Stranded")),
                 false, [this](const QString& t) {
                     editor_->setSelectionMeta(QStringLiteral("conductorType"), t);
                 });
        // Strand definition, e.g. "7/0.20" (strands / strand-diameter).
        addText(QStringLiteral("Strands"), mv(QStringLiteral("strandDef")).toString(),
                [this](const QString& t) {
                    editor_->setSelectionMeta(QStringLiteral("strandDef"), t);
                },
                QStringLiteral("e.g. 7/0.20"));
        // Outer diameter of the finished (insulated) wire, in the working length unit.
        addNum(QStringLiteral("OD (outer dia.)"), mv(QStringLiteral("od")).toDouble(),
               [this](double v) {
                   editor_->setSelectionMetaNumber(QStringLiteral("od"), std::max(0.0, v));
               });
        // Conductor resistance with a per-metre / per-foot unit; switching the unit
        // converts the value (1 ft = 0.3048 m, so Ω/ft = Ω/m × 0.3048).
        const SelValue rUnitVal = mv(QStringLiteral("resUnit"));
        const QString rUnit = rUnitVal.isVaries() ? QStringLiteral("m")
                                                  : rUnitVal.toString(QStringLiteral("m"));
        addCombo(QStringLiteral("R unit"),
                 {QString::fromUtf8("Ω/m"), QString::fromUtf8("Ω/ft")},
                 rUnitVal.isVaries()
                     ? QStringLiteral("#Value")
                     : (rUnit == QLatin1String("ft") ? QString::fromUtf8("Ω/ft")
                                                     : QString::fromUtf8("Ω/m")),
                 false, [this](const QString& t) {
                     // Each wire's OWN value is converted from its OWN current unit, so
                     // switching the unit with several wires selected converts them all
                     // correctly instead of stamping one number over the rest.
                     if (t == QLatin1String("#Value")) return;
                     editor_->convertSelectionResistanceUnit(t.contains(QLatin1String("ft")));
                 });
        addNum(QString::fromUtf8("Resistance (Ω/%1)").arg(rUnit == QLatin1String("ft")
                                                              ? QStringLiteral("ft")
                                                              : QStringLiteral("m")),
               mv(QStringLiteral("resistance")).toDouble(), [this](double v) {
                   editor_->setSelectionMetaNumber(QStringLiteral("resistance"), v);
               });
        // Marking on the drawing. Whether each part is shown is the eye on its own field, up in
        // the Wire section; what is left here is how the marking is written and how often it is
        // repeated along the run.
        addHeader(QStringLiteral("Wire marking"));
        addCombo(QStringLiteral("Colour as"),
                 {QStringLiteral("Full name"), QStringLiteral("2 letters")},
                 mv(QStringLiteral("colorNameMode")).toString() == QLatin1String("code")
                     ? QStringLiteral("2 letters")
                     : QStringLiteral("Full name"),
                 false, [this](const QString& t) {
                     editor_->setSelectionMeta(QStringLiteral("colorNameMode"),
                                               t.startsWith(QLatin1String("Full"))
                                                   ? QStringLiteral("full")
                                                   : QStringLiteral("code"));
                 });
    }

    // A selected Wire label: its text, and which wire it rides. Editing the text here
    // also updates the wire's own "Wire label" property — an attached label and the wire
    // property are the same thing.
    if (e && e->type == EntityType::NetLabel) {
        addHeader(QStringLiteral("Wire label"));
        const QString wid = mv(QStringLiteral("wireId")).toString();
        const QString labId = e->id;
        addText(QStringLiteral("Text"), mv(QStringLiteral("text")).toString(),
                [this, labId](const QString& t) { editor_->setWireLabelText(labId, t); },
                QStringLiteral("wire label"));
        QString onWire = QStringLiteral("not attached");
        if (!wid.isEmpty() && doc_)
            for (const auto& w : editDoc()->entities())
                if (w.id == wid && w.type == EntityType::Wire) {
                    onWire = QStringLiteral("wire #%1")
                                 .arg(w.meta.value(QStringLiteral("entNum")).toInt());
                    break;
                }
        addRow(QStringLiteral("Attached to"), onWire);
        addNum(QStringLiteral("Text size"), mv(QStringLiteral("textSize")).toDouble(4.0),
               [this](double v) {
                   editor_->setSelectionMetaNumber(QStringLiteral("textSize"),
                                                   std::clamp(v, 1.0, 50.0));
               });
    }

    // A selected BREAK MARK: everything about how it is drawn, in the panel as well as on the ribbon.
    // The ribbon's fields say what the NEXT one will look like; these say what THIS one looks like,
    // which is what is wanted once it is on the drawing.
    if (e && e->type == EntityType::LengthBreak) {
        addHeader(QStringLiteral("Break"));
        const bool curved =
            mv(QStringLiteral("breakStyle")).toString() == QLatin1String("curved");
        addCombo(QStringLiteral("Style"),
                 {QStringLiteral("Straight strokes"), QStringLiteral("S-curves")},
                 curved ? QStringLiteral("S-curves") : QStringLiteral("Straight strokes"), false,
                 [this](const QString& t) {
                     editor_->setSelectionMeta(QStringLiteral("breakStyle"),
                                               t == QLatin1String("S-curves")
                                                   ? QStringLiteral("curved")
                                                   : QStringLiteral("straight"));
                 });
        addNum(QStringLiteral("Distance between"), mv(QStringLiteral("gap")).toDouble(0.0),
               [this](double v) {
                   editor_->setSelectionMetaNumber(QStringLiteral("gap"),
                                                   std::clamp(v, 0.5, 500.0));
               });
        addNum(curved ? QStringLiteral("Size") : QStringLiteral("Lines length"),
               mv(QStringLiteral("size")).toDouble(6.0), [this](double v) {
                   editor_->setSelectionMetaNumber(QStringLiteral("size"),
                                                   std::clamp(v, 0.5, 2000.0));
               });
        // Blank means "as the style is drawn"; a number leans the strokes by that many degrees.
        const double ang = mv(QStringLiteral("angle")).toDouble(-1.0);
        addNum(QStringLiteral("Angle"), ang < 0.0 ? 0.0 : ang, [this](double v) {
            editor_->setSelectionMetaNumber(QStringLiteral("angle"), std::clamp(v, -75.0, 75.0));
        });
        addCombo(QStringLiteral("Interrupts the run"),
                 {QStringLiteral("Yes"), QStringLiteral("No")},
                 mv(QStringLiteral("mask")).toBool(true) ? QStringLiteral("Yes")
                                                        : QStringLiteral("No"),
                 false, [this](const QString& t) {
                     editor_->setSelectionMetaBool(QStringLiteral("mask"),
                                                   t == QLatin1String("Yes"));
                 });
        addCombo(QStringLiteral("Joints when selected"),
                 {QStringLiteral("Yes"), QStringLiteral("No")},
                 mv(QStringLiteral("endJoints")).toBool(true) ? QStringLiteral("Yes")
                                                             : QStringLiteral("No"),
                 false, [this](const QString& t) {
                     editor_->setSelectionMetaBool(QStringLiteral("endJoints"),
                                                   t == QLatin1String("Yes"));
                 });
        // What it is attached to, and where along it: read-only, since it is set by placing and by
        // dragging the mark along its run.
        const QString host = mv(QStringLiteral("host")).toString();
        QString onRun = QStringLiteral("nothing");
        if (!host.isEmpty() && doc_)
            for (const auto& r : editDoc()->entities())
                if (r.id == host)
                    onRun = QStringLiteral("%1 at %2%")
                                .arg(entityTypeToString(r.type))
                                .arg(mv(QStringLiteral("annT")).toDouble(0.5) * 100.0, 0, 'f', 0);
        addRow(QStringLiteral("On the run"), onRun);
    }

    // A selected Shield: edit its ellipse radii and whether the "S" shows.
    if (e && e->type == EntityType::Screen) {
        addHeader(QStringLiteral("Shield"));
        addNum(QStringLiteral("Small radius"), mv(QStringLiteral("rx")).toDouble(12.0),
               [this](double v) {
                   editor_->resizeSelectedShield(QStringLiteral("rx"), std::max(1.0, v));
               });
        // Current large radius: explicit override, else auto-fit to the caught wires.
        double ry = mv(QStringLiteral("ry")).toDouble(0.0);
        if (ry <= 0.0) {
            const int kc = std::min(mv(QStringLiteral("catch"))
                                        .toInt(static_cast<int>(e->points.size())),
                                    static_cast<int>(e->points.size()));
            double yMin = 1e18, yMax = -1e18;
            for (int i = 0; i < kc; ++i) {
                yMin = std::min(yMin, e->points[i].y);
                yMax = std::max(yMax, e->points[i].y);
            }
            ry = (yMax - yMin) / 2.0 + 6.0;
        }
        addNum(QStringLiteral("Large radius"), ry, [this](double v) {
            editor_->resizeSelectedShield(QStringLiteral("ry"), std::max(1.0, v));
        });
        // The proportion between them, as the ribbon shows it: setting the ratio moves the LARGE
        // radius, leaving the small one (the width across the wires) where the user put it.
        {
            const double rx = mv(QStringLiteral("rx")).toDouble(12.0);
            addNum(QStringLiteral("Ratio (L/S)"), rx > 1e-6 ? ry / rx : 0.0,
                   [this, rx](double v) {
                       editor_->resizeSelectedShield(QStringLiteral("ry"),
                                                     std::max(1.0, rx * std::max(0.01, v)));
                   });
        }
        // The outline: a plain ellipse, or a capsule (a stadium) for a long band of wires.
        // "Shield type", not "Type": the General section already has a Type row (the kind of
        // object), and two rows with one name in a flat list cannot be told apart. Same wording
        // as the builder's Layout field.
        addCombo(QStringLiteral("Shield type"),
                 {QStringLiteral("Ellipse"), QStringLiteral("Capsule")},
                 mv(QStringLiteral("shape")).toString() == QLatin1String("capsule")
                     ? QStringLiteral("Capsule")
                     : QStringLiteral("Ellipse"),
                 false, [this](const QString& t) {
                     editor_->setSelectionMeta(QStringLiteral("shape"),
                                               t == QLatin1String("Capsule")
                                                   ? QStringLiteral("capsule")
                                                   : QStringLiteral("ellipse"));
                 });
        // Which side of the outline the drain leaves from.
        {
            const QString legDir =
                mv(QStringLiteral("legDir")).toString(QStringLiteral("bottom"));
            auto legName = [](const QString& d) {
                if (d == QLatin1String("top")) return QStringLiteral("Top");
                if (d == QLatin1String("left")) return QStringLiteral("Left");
                if (d == QLatin1String("right")) return QStringLiteral("Right");
                return QStringLiteral("Bottom");
            };
            addCombo(QStringLiteral("Leg side"),
                     {QStringLiteral("Bottom"), QStringLiteral("Top"), QStringLiteral("Left"),
                      QStringLiteral("Right")},
                     legName(legDir), false, [this](const QString& t) {
                         // Moves the leg as well as recording the side; see setSelectionScreenLegDir.
                         editor_->setSelectionScreenLegDir(t.toLower());
                     });
        }
        // The outline's line: solid, or dashed with a scale for the dash.
        addCombo(QStringLiteral("Line"), {QStringLiteral("Solid"), QStringLiteral("Dashed")},
                 mv(QStringLiteral("lineType")).toString() == QLatin1String("dashed")
                     ? QStringLiteral("Dashed")
                     : QStringLiteral("Solid"),
                 false, [this](const QString& t) {
                     editor_->setSelectionMeta(QStringLiteral("lineType"),
                                               t == QLatin1String("Dashed")
                                                   ? QStringLiteral("dashed")
                                                   : QStringLiteral("solid"));
                 });
        addNum(QStringLiteral("LT scale"), mv(QStringLiteral("dashScale")).toDouble(1.0),
               [this](double v) {
                   editor_->setSelectionMetaNumber(QStringLiteral("dashScale"),
                                                   std::max(0.1, v));
               });
        addCombo(QStringLiteral("Show S"), {QStringLiteral("Yes"), QStringLiteral("No")},
                 yesNo(mv(QStringLiteral("showS")), false),
                 false, [this](const QString& t) {
                     editor_->setSelectionMetaBool(QStringLiteral("showS"),
                                                   t == QLatin1String("Yes"));
                 });
        addNum(QStringLiteral("S size"), mv(QStringLiteral("sSize")).toDouble(4.0),
               [this](double v) {
                   editor_->setSelectionMetaNumber(QStringLiteral("sSize"), std::max(1.0, v));
               });
        // Drain-leg length: how far the leg reaches from the outline edge. Measured from the
        // geometry (the leg end IS the shield's last point) rather than a stored number, so
        // it always agrees with what is drawn, including after the leg was dragged.
        addNum(QStringLiteral("Leg length"), editor_->selectedShieldLegLength(),
               [this](double v) { editor_->setSelectedShieldLegLength(std::max(0.0, v)); });
        // A leg that actually lands on a wire IS connected, so the X is not offered at all —
        // the drawing says what the geometry says.
        const bool legWired = editor_->shieldLegIsWired(*e);
        if (legWired) {
            addRow(QStringLiteral("Not Connected"), QStringLiteral("no — leg is wired"));
        } else {
            addCombo(QStringLiteral("Not Connected"), {QStringLiteral("Yes"), QStringLiteral("No")},
                     yesNo(mv(QStringLiteral("notConnected")), false),
                     false, [this](const QString& t) {
                         editor_->setSelectionMetaBool(QStringLiteral("notConnected"),
                                                       t == QLatin1String("Yes"));
                     });
            addNum(QStringLiteral("X size"), mv(QStringLiteral("ncSize")).toDouble(3.0),
                   [this](double v) {
                       editor_->setSelectionMetaNumber(QStringLiteral("ncSize"), std::max(0.5, v));
                   });
        }
    }

    // A selected twist: edit the parameters for its type.
    if (e && e->type == EntityType::Twist) {
        const QString ty = mv(QStringLiteral("twistType")).toString();
        if (ty == QLatin1String("crossed")) {
            addHeader(QStringLiteral("Twist (Type 1)"));
            addNum(QStringLiteral("Diagonal angle"),
                   mv(QStringLiteral("diagAngle")).toDouble(15.0), [this](double v) {
                       editor_->setSelectionMetaNumber(QStringLiteral("diagAngle"),
                                                        std::clamp(v, 0.0, 89.0));
                   });
            addNum(QStringLiteral("H line length"),
                   mv(QStringLiteral("capLen")).toDouble(8.0), [this](double v) {
                       editor_->setSelectionMetaNumber(QStringLiteral("capLen"), std::max(0.0, v));
                   });
        } else if (ty == QLatin1String("type3")) {
            addHeader(QStringLiteral("Twist (Type 3)"));
            addInt(QStringLiteral("Twists"), std::max(1, mv(QStringLiteral("repeats")).toInt(2)),
                   [this](int v) {
                       editor_->setSelectionMetaNumber(QStringLiteral("repeats"),
                                                       std::max(1, v));
                   });
        } else { // Type 2 (jog) and any legacy braid
            addHeader(QStringLiteral("Twist (Type 2)"));
            addNum(QStringLiteral("H line length"),
                   mv(QStringLiteral("flatLen")).toDouble(6.0), [this](double v) {
                       editor_->setSelectionMetaNumber(QStringLiteral("flatLen"), std::max(0.0, v));
                   });
            addInt(QStringLiteral("Twists"), std::max(1, mv(QStringLiteral("repeats")).toInt(2)),
                   [this](int v) {
                       editor_->setSelectionMetaNumber(QStringLiteral("repeats"),
                                                       std::max(1, v));
                   });
        }
    }

    // A selected Splice / X termination: edit its marker size.
    if (e && e->type == EntityType::Splice) {
        addHeader(QStringLiteral("Joint"));
        addNum(QStringLiteral("Size"), mv(QStringLiteral("size")).toDouble(4.0),
               [this](double v) {
                   editor_->setSelectionMetaNumber(QStringLiteral("size"), std::max(0.5, v));
               });
    }
    if (e && e->type == EntityType::NotConnected) {
        addHeader(QStringLiteral("Termination"));
        addNum(QStringLiteral("Size"), mv(QStringLiteral("ncSize")).toDouble(6.0),
               [this](double v) {
                   editor_->setSelectionMetaNumber(QStringLiteral("ncSize"), std::max(0.5, v));
               });
    }

    addHeader(QStringLiteral("Geometry"));
    if (e) {
        // Read through nv so a multi-selection whose objects sit at different places (or
        // are different sizes) reports "#Value" instead of the first one's numbers.
        addNum(QStringLiteral("Center X"), nv([](const Entity& x) {
                   const Bounds bb = x.bounds();
                   return bb.valid ? bb.center().x : 0.0;
               }),
               [this](double v) { editor_->setGeometryValue("Center X", v); });
        addNum(QStringLiteral("Center Y"), nv([](const Entity& x) {
                   const Bounds bb = x.bounds();
                   return bb.valid ? bb.center().y : 0.0;
               }),
               [this](double v) { editor_->setGeometryValue("Center Y", v); });
        // A LABEL answers Width and Height with ITS OWN two sizes, not with the box it happens to occupy.
        //
        // The box is the wrong answer twice over: on a bundle drawn vertically it reports the label's length
        // as its height, and a label under a clear sleeve is measured out to the sleeve rather than to the
        // label. Setting either of them by the box would stretch the label out of square with the wires. So
        // these rows ARE the label's width and height - along the run and across it - and they take, which is
        // what was asked for and what the two greyed-looking zeroes were failing to do: with the whole label
        // selected the box varies from piece to piece, so every row read "#Value" and showed nothing.
        const bool isLabel = mv(QStringLiteral("labelAlong")).contains();
        addNum(QStringLiteral("Width"),
               isLabel ? mv(QStringLiteral("labelAlong")).toDouble()
                       : nv([](const Entity& x) {
                             const Bounds bb = x.bounds();
                             return bb.valid ? bb.width() : 0.0;
                         }),
               [this, isLabel](double v) {
                   if (isLabel) {
                       if (editor_->adoptLabelFromSelection()) editor_->setLabelWidth(v);
                   } else {
                       editor_->setGeometryValue("Width", v);
                   }
               });
        addNum(QStringLiteral("Height"),
               isLabel ? mv(QStringLiteral("labelAcross")).toDouble()
                       : nv([](const Entity& x) {
                             const Bounds bb = x.bounds();
                             return bb.valid ? bb.height() : 0.0;
                         }),
               [this, isLabel](double v) {
                   if (isLabel) {
                       if (editor_->adoptLabelFromSelection()) editor_->setLabelHeight(v);
                   } else {
                       editor_->setGeometryValue("Height", v);
                   }
               });
        if (e->type == EntityType::Circle || e->type == EntityType::Arc)
            addNum(QStringLiteral("Radius"), nv([](const Entity& x) { return x.radius; }),
                   [this](double v) { editor_->setGeometryValue("Radius", v); });
        // A DETAIL VIEW: everything about it, in the panel as well as on its own ribbon tab. The panel is
        // where somebody goes to change one thing on one object; the tab is where somebody works through
        // several. Neither is a substitute for the other, so both.
        if (e->type == EntityType::DetailView) {
            addNum(QStringLiteral("Boundary radius"), nv([](const Entity& x) { return x.radius; }),
                   [this](double v) { editor_->setGeometryValue("Radius", v); });
            addNum(QStringLiteral("Scale factor"), mv(QStringLiteral("detailScale")).toDouble(2.0),
                   [this](double v) {
                       editor_->setSelectionMetaNumber(QStringLiteral("detailScale"),
                                                       std::max(0.01, v));
                   });
            // TWO TEXTS, because they say different things: the one on the circle round the thing is a
            // pointer, and the one on the enlargement is a heading. Tied together by convention rather than
            // by code - "DETAIL A" is what an office writes beside A - so the heading defaults to that and
            // can be anything instead.
            const QString letter = mv(QStringLiteral("detailTag")).toString(QStringLiteral("A"));
            const QString both = QStringLiteral("Detail %1").arg(letter);
            addText(QStringLiteral("Identifier"), letter, [this](const QString& t) {
                editor_->setSelectionMeta(QStringLiteral("detailTag"), t.trimmed().toUpper());
            });
            addText(QStringLiteral("Source label"),
                    mv(QStringLiteral("detailTagText")).toString(both), [this](const QString& t) {
                        editor_->setSelectionMeta(QStringLiteral("detailTagText"), t.trimmed());
                    });
            addText(QStringLiteral("View label"),
                    mv(QStringLiteral("detailBigText")).toString(both), [this](const QString& t) {
                        editor_->setSelectionMeta(QStringLiteral("detailBigText"), t.trimmed());
                    });
            addNum(QStringLiteral("Text height"), mv(QStringLiteral("detailTextSize")).toDouble(5.0),
                   [this](double v) {
                       editor_->setSelectionMetaNumber(QStringLiteral("detailTextSize"),
                                                       std::max(0.1, v));
                   });
            const QString dFont = mv(QStringLiteral("detailFont")).toString();
            addCombo(QStringLiteral("Font"), QStringList{QStringLiteral("Default")}
                                                 + QFontDatabase::families(),
                     dFont.isEmpty() ? QStringLiteral("Default") : dFont, true,
                     [this](const QString& f) {
                         editor_->setSelectionMeta(QStringLiteral("detailFont"),
                                                   f == QLatin1String("Default") ? QString() : f);
                     });
            // WHERE THE WORDS SIT ROUND EACH CIRCLE. A letter fixed at one o'clock lands on the geometry
            // about half the time, and a sheet with several details on it cannot be arranged at all if none
            // of the letters can be moved off what they are sitting on.
            addNum(QStringLiteral("Source label angle"),
                   mv(QStringLiteral("detailTagAngle")).toDouble(90.0), [this](double v) {
                       editor_->setSelectionMetaNumber(QStringLiteral("detailTagAngle"), v);
                   });
            addNum(QStringLiteral("Source label offset"),
                   mv(QStringLiteral("detailTagGap")).toDouble(3.0), [this](double v) {
                       editor_->setSelectionMetaNumber(QStringLiteral("detailTagGap"),
                                                       std::max(0.0, v));
                   });
            addNum(QStringLiteral("View label angle"),
                   mv(QStringLiteral("detailBigAngle")).toDouble(-90.0), [this](double v) {
                       editor_->setSelectionMetaNumber(QStringLiteral("detailBigAngle"), v);
                   });
            addNum(QStringLiteral("View label offset"),
                   mv(QStringLiteral("detailBigGap")).toDouble(5.5), [this](double v) {
                       editor_->setSelectionMetaNumber(QStringLiteral("detailBigGap"),
                                                       std::max(0.0, v));
                   });
            // The ring round the enlargement. It is the convention and it says where the view stops, but a
            // detail drawn into a clear part of the sheet reads perfectly well without one, and on a busy
            // sheet it is one more circle to be mistaken for a hole.
            addCombo(QStringLiteral("Show view boundary"), {"Yes", "No"},
                     yesNo(mv(QStringLiteral("detailBoundary")), true), false,
                     [this](const QString& v) {
                         editor_->setSelectionMetaBool(QStringLiteral("detailBoundary"),
                                                       v == QLatin1String("Yes"));
                     });
            // ANNOTATIVE: the enlargement is a live WINDOW on the drawing rather than a picture of it.
            // Ticked, the cursor reaches through it - pick something in the big circle and it is the real
            // object that is picked, drag it and the real object moves, and it changes in both circles at
            // once. Which is the point: a detail is drawn BECAUSE the thing is too small to work on, and a
            // view you can only look at sends you back to the small version to do the work at the size you
            // could not read.
            addCombo(QStringLiteral("Associative"), {"No", "Yes"},
                     yesNo(mv(QStringLiteral("detailAnnotative")), false), false,
                     [this](const QString& v) {
                         editor_->setSelectionMetaBool(QStringLiteral("detailAnnotative"),
                                                       v == QLatin1String("Yes"));
                     });
            // The dimensions and text in the source circle, enlarged with everything else - or left out. A
            // detail is usually drawn to show a SHAPE, and dimensions sized for the small version become the
            // biggest thing in the circle and cover what they were measuring. But a detail drawn BECAUSE the
            // dimensions would not fit at the small size needs them. Both are ordinary, so it is a switch.
            addCombo(QStringLiteral("Include annotations"), {"No", "Yes"},
                     yesNo(mv(QStringLiteral("detailShowDims")), false), false,
                     [this](const QString& v) {
                         editor_->setSelectionMetaBool(QStringLiteral("detailShowDims"),
                                                       v == QLatin1String("Yes"));
                     });
        }
        // Double line / double polyline: the parallel spacing between the two runs.
        const bool isDoubleLine = e->type == EntityType::DoubleLine;
        const bool isDoublePoly =
            e->type == EntityType::Polyline && mv(QStringLiteral("double")).toBool();
        // A SLEEVE IS A DOUBLE POLYLINE UNDERNEATH - that is how it gets joints on both rails - but what
        // it is drawn from is not "the spacing between two runs". It is how far apart its two sides run,
        // and then, per end, how long the shrunk part is and how big the face it recovers to - and those
        // are what a shop reads off a sleeve, so the panel says them. One row called "Gap" on a necked
        // outline names none of them.
        const bool isSleeve = isDoublePoly && mv(QStringLiteral("shrunkEnds")).toBool();
        if (isSleeve) {
            addHeader(QStringLiteral("Heatshrink"));
            const double across = mv(QStringLiteral("gap")).toDouble(6.0);
            const bool endsOnly = mv(QStringLiteral("shrinkEndsOnly")).toBool();
            addNum(QStringLiteral("Line spacing"), across, [this](double v) {
                editor_->setSelectionSleeveAcross(std::max(0.2, v));
            });
            if (endsOnly) {
                addNum(QStringLiteral("Cable width"),
                       mv(QStringLiteral("shrinkCableWidth")).toDouble(across * 0.82),
                       [this, across](double v) {
                           editor_->setSelectionMetaNumber(QStringLiteral("shrinkCableWidth"),
                                                           std::clamp(v, 0.1, across));
                       });
            }
            // ONE SECTION PER END, rather than four rows saying which end they belong to.
            //
            // The key column is 110 pixels wide and "Shrunk length (start)" is half as long again, so
            // all four rows arrived reading "Shrunk length ..." and "Entry face ..." - the words telling
            // them apart were the ones cut off. Widening the column would take that space off the value
            // fields, which are already narrow, so the end goes in the BAND ABOVE instead, where it is
            // said once and the rows underneath can be short enough to fit.
            for (int end = 0; end < 2; ++end) {
                addHeader(end == 0 ? QStringLiteral("Start end") : QStringLiteral("Finish end"));
                const QString side = end == 0 ? QStringLiteral("Start") : QStringLiteral("End");
                // Unset reads as the DEFAULT rather than as a zero, so the row opens showing the sleeve
                // that is on the sheet - a zero would say the end is square when it is not, and redraw
                // it the moment the field was touched. Typing over it fixes the number for good. A
                // selection whose sleeves disagree reads "#Value", which mv() carries through as NaN.
                auto row = [&](const QString& label, const QString& key, double def, double floor,
                               const QString& tip) {
                    addNum(label, mv(key).toDouble(def),
                           [this, key, floor](double v) {
                               editor_->setSelectionMetaNumber(key, std::max(floor, v));
                           },
                           tip, end);
                };
                if (endsOnly) {
                    // THE SLEEVE'S OWN LENGTH along the cable: where its split line sits, measured
                    // inward from this end. The face and taper below still edit the recovered outside
                    // end exactly as they do on a whole-run sleeve.
                    row(QStringLiteral("Sleeve length"),
                        QStringLiteral("shrinkCover") + side, 20.0, 0.0,
                        QStringLiteral("How far heatshrink covers the cable from the %1 end.")
                            .arg(end == 0 ? QStringLiteral("start") : QStringLiteral("finish")));
                }
                // THE SHRUNK LENGTH: how far in from this end the sleeve takes to reach full width, so
                // the number measures the shrunk part itself, running up to the straight lines of the
                // body. Zero is allowed and means a square shoulder - what the end of a length of
                // tubing recovered onto something its own size looks like.
                row(QStringLiteral("Shrunk length"), QStringLiteral("shrinkTaper") + side, 4.0, 0.0,
                    QStringLiteral("Shrunk length at the %1 end: from the end of the sleeve to where "
                                   "its straight lines begin. 0 = a square shoulder.")
                        .arg(end == 0 ? QStringLiteral("start") : QStringLiteral("finish")));
                // THE ENTRY FACE: the line across the very end, which is the size the sleeve has
                // recovered down to. Shown and typed as the WHOLE line - what is measured on the sheet.
                row(QStringLiteral("Entry face"), QStringLiteral("shrinkFace") + side, across * 0.82,
                    0.1,
                    QStringLiteral("Entry face at the %1 end: the length of the line across the very "
                                   "end of the sleeve.")
                        .arg(end == 0 ? QStringLiteral("start") : QStringLiteral("finish")));
            }
        } else if (isDoubleLine || isDoublePoly) {
            addNum(QStringLiteral("Gap"), mv(QStringLiteral("gap")).toDouble(10.0),
                   [this](double v) {
                       editor_->setSelectionMetaNumber(QStringLiteral("gap"), std::max(0.1, v));
                   });
        }
        // Balloon callout: arrowhead type/size, leader landing length + the number.
        if (mv(QStringLiteral("balloon")).toBool()) {
            // Arrowhead type (fall menu). Canonical values map to friendly labels.
            static const QVector<QPair<QString, QString>> arrowTypes = {
                {"Closed filled", "closed"}, {"Closed blank", "blank"}, {"Dot", "dot"},
                {"Architectural tick", "tick"}, {"Oblique", "tick"}, {"Open", "open"},
                {"None", "none"}};
            const QString curAh = mv(QStringLiteral("arrowhead")).toString("closed");
            QVector<ComboEntry> ahItems;
            for (const auto& a : arrowTypes)
                ahItems.push_back({a.first, makeArrowheadIcon(a.second), a.second});
            addIconCombo(QStringLiteral("Arrowhead"), ahItems, curAh, [this](const QString& d) {
                editor_->setSelectionMeta(QStringLiteral("arrowhead"), d);
            });
            addNum(QStringLiteral("Arrowhead size"),
                   mv(QStringLiteral("arrowSize")).toDouble(3.3), [this](double v) {
                       editor_->setSelectionMetaNumber(QStringLiteral("arrowSize"), std::max(0.1, v));
                   });
            addNum(QStringLiteral("Landing distance"),
                   mv(QStringLiteral("landing")).toDouble(8.0), [this](double v) {
                       editor_->setSelectionMetaNumber(QStringLiteral("landing"), std::max(0.0, v));
                   });
            addCombo(QStringLiteral("Number"), {}, mv(QStringLiteral("text")).toString(),
                     true, [this](const QString& t) {
                         editor_->setSelectionMeta(QStringLiteral("text"), t);
                     });
        }
    } else if (mv(QStringLiteral("labelAlong")).contains()) {
        // A LABEL IS MANY KINDS OF OBJECT - a rectangle, some lines, and the mask under it - so a selected
        // label is a MIXED selection, and the mixed branch below is four dead rows reading zero. Which is
        // exactly what "cannot modify the width and height" was: the rows were there, they were the
        // no-single-object placeholders, and nothing could be typed into them.
        //
        // A label has its own two sizes whatever it is made of, so it answers with them: along the run and
        // across it. Not the bounding box - on a bundle drawn vertically the box reports the length as the
        // height, and a label bent round a knee has no box worth the name.
        addNum(QStringLiteral("Width"), mv(QStringLiteral("labelAlong")).toDouble(), [this](double v) {
            if (editor_->adoptLabelFromSelection()) editor_->setLabelWidth(v);
        });
        addNum(QStringLiteral("Height"), mv(QStringLiteral("labelAcross")).toDouble(), [this](double v) {
            if (editor_->adoptLabelFromSelection()) editor_->setLabelHeight(v);
        });
    } else {
        addRow(QStringLiteral("Center X"), QStringLiteral("0"));
        addRow(QStringLiteral("Center Y"), QStringLiteral("0"));
        addRow(QStringLiteral("Width"), QStringLiteral("0"));
        addRow(QStringLiteral("Height"), QStringLiteral("0"));
    }

    // --- Picture section: what it is, how big it was, and how big it is now. ---
    // Width and Height are up in Geometry, where every object's are, so a picture can be sized to an
    // exact number of millimetres instead of only by eye with the corner grip.
    if (e && e->type == EntityType::Image) {
        addHeader(QStringLiteral("Picture"));
        addRow(QStringLiteral("File"),
               mv(QLatin1String(PictureKeys::kName)).toString(QStringLiteral("(pasted)")));
        const double pixW = mv(QLatin1String(PictureKeys::kPixW)).toDouble(0.0);
        const double pixH = mv(QLatin1String(PictureKeys::kPixH)).toDouble(0.0);
        if (pixW > 0.0 && pixH > 0.0)
            addRow(QStringLiteral("Pixels"), QStringLiteral("%1 × %2")
                                                 .arg(pixW, 0, 'f', 0)
                                                 .arg(pixH, 0, 'f', 0));
        addCombo(QStringLiteral("Keep proportions"), {QStringLiteral("Yes"), QStringLiteral("No")},
                 yesNo(mv(QLatin1String(PictureKeys::kLockAspect)), true), false,
                 [this](const QString& t) {
                     editor_->setSelectionMetaBool(QLatin1String(PictureKeys::kLockAspect),
                                                   t == QLatin1String("Yes"));
                 });
        addNum(QStringLiteral("Opacity %"), mv(QLatin1String(PictureKeys::kOpacity)).toDouble(100.0),
               [this](double v) {
                   editor_->setSelectionMetaNumber(QLatin1String(PictureKeys::kOpacity),
                                                   std::clamp(v, 0.0, 100.0));
               });
        // Back to the size it came at - 96 dpi, the size it looked on screen - after it has been
        // dragged about. Width then Height, because each axis is scaled on its own.
        if (pixW > 0.0 && pixH > 0.0) {
            addButton(QStringLiteral("Natural size"), QStringLiteral("Reset"), [this, pixW, pixH] {
                const QSizeF mm = pictureDefaultSizeMm(
                    QSize(static_cast<int>(pixW), static_cast<int>(pixH)), 250.0);
                editor_->setGeometryValue(QStringLiteral("Width"), mm.width());
                editor_->setGeometryValue(QStringLiteral("Height"), mm.height());
            });
        }
        // A different picture in the same frame: the sheet's layout is kept, only what fills it
        // changes. Useful for a template whose logo has been redrawn.
        // The width it has now, taken while the entity is definitely alive: the panel is rebuilt
        // whenever the selection changes, so a pointer captured here could be stale by the time the
        // file dialog closes.
        const Bounds picBounds = e->bounds();
        const double picW = picBounds.valid ? picBounds.width() : 0.0;
        addButton(QStringLiteral("Replace"), QStringLiteral("Choose a file…"), [this, picW] {
            const QString path = QFileDialog::getOpenFileName(
                this, QStringLiteral("Replace the picture"), QString(), pictureFileFilter());
            if (path.isEmpty()) return;
            const Entity fresh = pictureEntityFromFile(path);
            if (fresh.type != EntityType::Image) {
                statusBar()->showMessage(QStringLiteral("%1 is not a picture this build can read")
                                             .arg(QFileInfo(path).fileName()),
                                         6000);
                return;
            }
            for (const char* key : {PictureKeys::kData, PictureKeys::kFormat, PictureKeys::kName,
                                    PictureKeys::kKey})
                editor_->setSelectionMeta(QLatin1String(key),
                                          fresh.meta.value(QLatin1String(key)).toString());
            const double w = fresh.meta.value(QLatin1String(PictureKeys::kPixW)).toDouble();
            const double h = fresh.meta.value(QLatin1String(PictureKeys::kPixH)).toDouble();
            editor_->setSelectionMetaNumber(QLatin1String(PictureKeys::kPixW), w);
            editor_->setSelectionMetaNumber(QLatin1String(PictureKeys::kPixH), h);
            // Keep the width it had on the sheet and take the new picture's proportions, so the
            // replacement is not squashed into the old one's shape.
            if (w > 0.0 && h > 0.0 && picW > 1e-9)
                editor_->setGeometryValue(QStringLiteral("Height"), picW * h / w);
            refreshProperties();
        });
    }

    // --- Block arrow: how wide the shaft is, and how big the head is. ---
    if (e && e->meta.value(QStringLiteral("volumeArrow")).toBool()) {
        addHeader(QStringLiteral("Block arrow"));
        // Both are real millimetres, and both are optional: left at zero each follows the arrow's own
        // length, which is what a freshly drawn arrow does. Set them and a row of arrows drawn at
        // different lengths can be given shafts and heads that MATCH - which is the whole reason to
        // want the numbers, and there was no way to reach them at all.
        const double len = distance(e->a, e->b);
        addNum(QStringLiteral("Shaft width"), mv(QStringLiteral("arrowWidth")).toDouble(len * 0.18),
               [this](double v) {
                   editor_->setSelectionMetaNumber(QStringLiteral("arrowWidth"), std::max(0.0, v));
               });
        addNum(QStringLiteral("Head size"), mv(QStringLiteral("arrowHead")).toDouble(len * 0.36),
               [this](double v) {
                   editor_->setSelectionMetaNumber(QStringLiteral("arrowHead"), std::max(0.0, v));
               });
    }

    // --- Axis and the angle swept about it. ---
    if (e && e->meta.value(QStringLiteral("axisAngle")).toBool()) {
        addHeader(QStringLiteral("Axis angle"));
        // The sweep is the object's whole point, so it is the first row. Negative sweeps the other way
        // round, which is how a mechanism that opens anticlockwise is drawn.
        addNum(QStringLiteral("Swept angle"), mv(QStringLiteral("axisSweep")).toDouble(180.0),
               [this](double v) {
                   editor_->setSelectionMetaNumber(QStringLiteral("axisSweep"),
                                                   std::clamp(v, -359.9, 359.9));
               },
               QStringLiteral("How far the thing travels. The label at the far end follows this."));
        // WHERE THE ZERO SITS. The second click puts it wherever the cursor was, which is quick but
        // rarely exactly right; this turns the whole annotation about its axis without touching the
        // radius, so the zero can be put on the datum the mechanism actually measures from.
        addNum(QStringLiteral("Turn the zero by"), mv(QStringLiteral("axisZero")).toDouble(0.0),
               [this](double v) { editor_->setSelectionMetaNumber(QStringLiteral("axisZero"), v); },
               QStringLiteral("Degrees to turn the whole annotation about the axis."));
        // WHAT THE START IS CALLED. The far end is this plus the sweep - see the renderer - so the two
        // labels cannot disagree with the arc they are labelling.
        addNum(QStringLiteral("Marked at the zero"), mv(QStringLiteral("axisFrom")).toDouble(0.0),
               [this](double v) { editor_->setSelectionMetaNumber(QStringLiteral("axisFrom"), v); },
               QStringLiteral("The number written at the start. The far end is this plus the sweep."));
        // How far out the arc runs, measured from the axis. It is the distance to the second click and
        // stays that - the point itself is moved out along its own direction - so the object keeps ONE
        // answer for where the arc is, whether it was set here or by dragging the handle.
        addNum(QStringLiteral("Annotation radius"), distance(e->a, e->b),
               [this](double v) {
                   editor_->setGeometryValue(QStringLiteral("Radius"), std::max(0.1, v));
               },
               QStringLiteral("How far the arc runs out from the axis."));
        // HOW LONG THE CENTRE LINES ARE, which is nothing to do with how far out the arc runs: the two
        // were one number, so a long axis with a tight arc could not be drawn.
        addNum(QStringLiteral("Axis size"), axisAngleArm(*e),
               [this](double v) {
                   editor_->setSelectionMetaNumber(QStringLiteral("axisSize"), std::max(0.1, v));
               },
               QStringLiteral("How far the centre lines reach from the axis."));
        // WHICH ENDS CARRY AN ARROW, and what kind. A travel that runs one way is drawn with one; a
        // travel either side of a datum with two; a range that is simply marked out with none.
        addCombo(QStringLiteral("Arrow ends"),
                 {QStringLiteral("end"), QStringLiteral("start"), QStringLiteral("both"),
                  QStringLiteral("none")},
                 mv(QStringLiteral("arrowEnds")).toString().isEmpty()
                     ? QStringLiteral("end")
                     : mv(QStringLiteral("arrowEnds")).toString(),
                 false, [this](const QString& v) {
                     editor_->setSelectionMeta(QStringLiteral("arrowEnds"), v);
                 });
        addCombo(QStringLiteral("Arrow type"),
                 {QStringLiteral("closed"), QStringLiteral("open"), QStringLiteral("dot")},
                 mv(QStringLiteral("arrowType")).toString().isEmpty()
                     ? QStringLiteral("closed")
                     : mv(QStringLiteral("arrowType")).toString(),
                 false, [this](const QString& v) {
                     editor_->setSelectionMeta(QStringLiteral("arrowType"), v);
                 });
        addNum(QStringLiteral("Arrow size"), mv(QStringLiteral("arrowSize")).toDouble(7.0),
               [this](double v) {
                   editor_->setSelectionMetaNumber(QStringLiteral("arrowSize"), std::max(0.0, v));
               });
        addNum(QStringLiteral("Text height"), mv(QStringLiteral("textSize")).toDouble(10.0),
               [this](double v) {
                   editor_->setSelectionMetaNumber(QStringLiteral("textSize"), std::max(0.1, v));
               });
    }

    // --- Leader section: the words it carries, and the sizes it is drawn at. ---
    if (e && e->type == EntityType::Leader) {
        const QString id = e->id;
        addHeader(QStringLiteral("Leader"));
        // A callout's label, changed here or by double-clicking the callout itself. Both were missing:
        // the only way to correct a label was to delete the whole callout and draw another.
        {
            const QString content = mv(QStringLiteral("text")).toString();
            const int r = propsTable_->rowCount();
            propsTable_->insertRow(r);
            auto* key = new QTableWidgetItem(QStringLiteral("Label"));
            key->setFlags(Qt::ItemIsEnabled);
            propsTable_->setItem(r, 0, key);
            auto* btn = new QPushButton(propsTable_);
            QString oneLine = content;
            oneLine.replace(QLatin1Char('\n'), QLatin1Char(' '));
            btn->setText(oneLine.isEmpty() ? QStringLiteral("Edit...") : oneLine.left(40));
            connect(btn, &QPushButton::clicked, this, [this, id] { editor_->emitEditText(id); });
            propsTable_->setCellWidget(r, 1, btn);
        }
        addNum(QStringLiteral("Text height"), mv(QStringLiteral("size")).toDouble(2.5),
               [this](double v) { editor_->setSelectionMetaNumber(QStringLiteral("size"), v); });
        addNum(QStringLiteral("Arrow size"), mv(QStringLiteral("arrowSize")).toDouble(3.3),
               [this](double v) { editor_->setSelectionMetaNumber(QStringLiteral("arrowSize"), v); });
        // The flat run the words sit on. Zero for a bare arrow with the words straight off its end.
        addNum(QStringLiteral("Landing"), mv(QStringLiteral("landing")).toDouble(0.0),
               [this](double v) {
                   editor_->setSelectionMetaNumber(QStringLiteral("landing"), std::max(0.0, v));
               });
    }

    // --- Text section: editable content, font, height, style, justification. ---
    if (e && e->type == EntityType::Text) {
        const QString id = e->id;
        addHeader(QStringLiteral("Text"));

        // Contents — single-line preview; an "Edit..." button opens the full editor.
        const QString content = mv(QStringLiteral("text")).toString();
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
        const QString curFont = mv(QStringLiteral("font")).toString();
        addCombo(QStringLiteral("Font"), fonts, curFont.isEmpty() ? QStringLiteral("Default") : curFont,
                 true, [this](const QString& f) {
                     editor_->setSelectionTextMeta(QStringLiteral("font"),
                                                   f == QLatin1String("Default") ? QJsonValue(QString())
                                                                                 : QJsonValue(f));
                 });

        // Height.
        addNum(QStringLiteral("Height"), mv(QStringLiteral("size")).toDouble(5.0),
               [this](double v) { editor_->setSelectionTextMeta(QStringLiteral("size"), v); });

        // Bold / Italic / Justify dropdowns.
        addCombo(QStringLiteral("Bold"), {QStringLiteral("No"), QStringLiteral("Yes")},
                 yesNo(mv(QStringLiteral("bold"))),
                 false, [this](const QString& v) {
                     editor_->setSelectionTextMeta(QStringLiteral("bold"), v == QLatin1String("Yes"));
                 });
        addCombo(QStringLiteral("Italic"), {QStringLiteral("No"), QStringLiteral("Yes")},
                 yesNo(mv(QStringLiteral("italic"))),
                 false, [this](const QString& v) {
                     editor_->setSelectionTextMeta(QStringLiteral("italic"), v == QLatin1String("Yes"));
                 });
        addCombo(QStringLiteral("Underline"), {QStringLiteral("No"), QStringLiteral("Yes")},
                 yesNo(mv(QStringLiteral("underline"))),
                 false, [this](const QString& v) {
                     editor_->setSelectionTextMeta(QStringLiteral("underline"), v == QLatin1String("Yes"));
                 });
        addCombo(QStringLiteral("Strikethrough"), {QStringLiteral("No"), QStringLiteral("Yes")},
                 yesNo(mv(QStringLiteral("strike"))),
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
        QString curJust = mv(QStringLiteral("justify")).toString();
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
        const QString curList = mv(QStringLiteral("list")).toString();
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
               mv(QStringLiteral("lineSpacing")).toDouble(1.0),
               [this](double v) {
                   editor_->setSelectionTextMeta(QStringLiteral("lineSpacing"),
                                                 v > 0 ? v : 1.0);
               });
        // Container wrap width (0 = no wrapping / single column). Also draggable
        // via the right-edge grip on the object.
        addNum(QStringLiteral("Box width"), mv(QStringLiteral("boxWidth")).toDouble(0.0),
               [this](double v) {
                   editor_->setSelectionTextMeta(QStringLiteral("boxWidth"), std::max(0.0, v));
               });
        addNum(QStringLiteral("Box height"), mv(QStringLiteral("boxHeight")).toDouble(0.0),
               [this](double v) {
                   editor_->setSelectionTextMeta(QStringLiteral("boxHeight"), std::max(0.0, v));
               });
    }

    // ---- Dimension properties (AutoCAD-style) ----
    if (e && e->type == EntityType::Dimension) {
        const int dec = e->meta.contains(QStringLiteral("decimals"))
                            ? mv(QStringLiteral("decimals")).toInt()
                            : 2;
        addHeader(QStringLiteral("Lines & Arrows"));
        addNum(QStringLiteral("Arrow size"), mv(QStringLiteral("arrowSize")).toDouble(3.3),
               [this](double v) {
                   editor_->setSelectionDimMeta(QStringLiteral("arrowSize"), std::max(0.1, v));
               });
        addNum(QStringLiteral("Ext line offset"),
               mv(QStringLiteral("extOffset")).toDouble(2.0), [this](double v) {
                   editor_->setSelectionDimMeta(QStringLiteral("extOffset"), std::max(0.0, v));
               });
        // Which axis a linear dimension measures along. Dragging it about cannot change this - a height
        // that turned into a length while it was being moved clear of the drawing was a bug, not a
        // feature - so here is where it is changed on purpose.
        if (mv(QStringLiteral("dimType")).toString(QStringLiteral("linear")) ==
            QLatin1String("linear")) {
            addCombo(QStringLiteral("Direction"), {"Horizontal", "Vertical"},
                     dimensionAcrossX(*e) ? QStringLiteral("Horizontal")
                                          : QStringLiteral("Vertical"),
                     false, [this](const QString& t) {
                         editor_->setSelectionDimMeta(QStringLiteral("dimAcross"),
                                                      t == QLatin1String("Horizontal"));
                     });
        }

        addHeader(QStringLiteral("Text"));
        addNum(QStringLiteral("Text height"), mv(QStringLiteral("textHeight")).toDouble(5.0),
               [this](double v) {
                   editor_->setSelectionDimMeta(QStringLiteral("textHeight"), std::max(0.1, v));
               });
        // What the dimension says instead of what it measures. Empty means the measurement, and
        // emptying the field again brings it back. "<>" stands in for the measured value, so "<> REF"
        // reads as the number followed by REF.
        addText(QStringLiteral("Text override"),
                mv(QStringLiteral("textOverride")).toString(),
                [this](const QString& t) {
                    editor_->setSelectionDimMeta(QStringLiteral("textOverride"), t);
                },
                QStringLiteral("the measurement — or <> inside your own text"));

        addHeader(QStringLiteral("Primary Units"));
        addCombo(QStringLiteral("Precision"),
                 {"0", "1", "2", "3", "4", "5", "6"}, QString::number(dec), false,
                 [this](const QString& t) {
                     editor_->setSelectionDimMeta(QStringLiteral("decimals"), t.toInt());
                 });
        // Read-only measured value, like AutoCAD's "Measurement".
        double measured = 0.0;
        const QString ty = mv(QStringLiteral("dimType")).toString(QStringLiteral("linear"));
        if (ty == QLatin1String("radius") || ty == QLatin1String("jogged")) measured = e->radius;
        else if (ty == QLatin1String("diameter")) measured = e->radius * 2.0;
        else measured = std::hypot(e->b.x - e->a.x, e->b.y - e->a.y);
        addRow(QStringLiteral("Measurement"), QString::number(measured, 'f', dec));
    }
    propsTable_->setUpdatesEnabled(true); // one coalesced repaint of the finished table
    refreshing_ = false;
}

void MainWindow::refreshBomPanel() {
    if (!bomPanel_ || !doc_) return;
    // The same rows the bill-of-materials window shows: what the drawing calls for, plus the rows added
    // by hand, in the working unit, totalled if that is what the drawing asks for.
    BomTable table;
    table.fromJson(doc_->bom());
    std::vector<BomRow> rows;
    if (table.config.includeDrawing) {
        if (!bomLibLoaded_) {
            bomCatalog_.openRemembered();
            bomLib_ = bomCatalog_.merged();
            bomLibLoaded_ = true;
        }
        rows = bomFromDrawing(*doc_, bomLib_);
    }
    for (const BomRow& r : table.rows)
        if (!r.fromDrawing) rows.push_back(r);
    rows = bomApplyConfig(rows, table.config);
    if (table.config.unify) rows = bomUnify(rows);

    // Five of the seven columns: what it is, the part number to order it by, WHO MAKES IT, how many,
    // and of what. The two left out - the line number and the internal P/N - are for the window and for
    // the table placed on the sheet. (1 = description, 3 = manufacturer's P/N, 4 = manufacturer,
    // 5 = quantity, 6 = units.)
    bomPanel_->clear();
    // A LINE NUMBER down the left, which is what a balloon on the drawing calls a part out by.
    //
    // It counts POSITIONS, not lines: it is 1, 2, 3 down the panel whatever order the lines are in, so
    // moving a line moves it to a numbered place rather than carrying its old number along with it. That is
    // what makes the column worth having - a drawing's balloons read against a settled order, and a list
    // whose numbering shuffles when it is re-sorted cannot be read against anything.
    bomPanel_->setColumnCount(6);
    bomPanel_->setHorizontalHeaderLabels({QStringLiteral("#"), table.header(1), table.header(3),
                                          table.header(4), table.header(5), table.header(6)});
    // AND ANY OF THEM THE CONFIGURATION HAS SWITCHED OFF IS NOT HERE EITHER. This panel and the bill's own
    // window are two views of one list; a column left out of the sheet has no business in either. The
    // description stays whatever the ticks say - it is what every line is read by, and it carries what the
    // right-click menu needs to know about the row (see below).
    {
        const BomConfig& cfg = table.config;
        const bool pn = cfg.columnShown(3) || cfg.columnShown(2);
        const bool shown[6] = {cfg.columnShown(0), true, pn, cfg.columnShown(4), cfg.columnShown(5),
                               cfg.columnShown(6)};
        for (int c = 0; c < 6; ++c) bomPanel_->setColumnHidden(c, !shown[c]);
    }
    bomPanel_->setRowCount(static_cast<int>(rows.size()));
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const BomRow& r = rows[static_cast<size_t>(i)];
        auto cell = [&](int col, const QString& text) {
            auto* it = new QTableWidgetItem(text);
            it->setToolTip(text);
            bomPanel_->setItem(i, col, it);
        };
        {
            auto* num = new QTableWidgetItem(QString::number(i + 1));
            num->setTextAlignment(Qt::AlignCenter);
            num->setToolTip(QStringLiteral("Line %1 — the number a balloon calls this part out by")
                                .arg(i + 1));
            bomPanel_->setItem(i, 0, num);
        }
        cell(1, r.description.isEmpty() ? r.ref : r.description);
        // Where the line came from, and what it names: what the right-click menu needs to know whether the
        // line can be taken off, and which of the drawing's own rows to take off with it. Carried on the
        // DESCRIPTION cell, as before - the number cell is a position, and holds nothing about the part.
        if (QTableWidgetItem* it = bomPanel_->item(i, 1)) {
            it->setData(Qt::UserRole + 1, !r.fromDrawing);
            it->setData(Qt::UserRole + 2, r.partId);
            it->setData(Qt::UserRole + 3, r.mfrPn);
        }
        cell(2, r.mfrPn.isEmpty() ? r.internalPn : r.mfrPn);
        cell(3, r.mfrName);
        cell(4, r.qtyText(table.config.qtyDecimals));
        cell(5, r.unit);
    }
    bomPanel_->horizontalHeader()->setStretchLastSection(false);
    // The number is as narrow as it needs to be, the description takes what is left over, and the rest are
    // as wide as they need to be.
    bomPanel_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    bomPanel_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    for (int c = 2; c < 6; ++c)
        bomPanel_->horizontalHeader()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
}

void MainWindow::refreshEntityList() {
    if (!entityList_) return;
    // A SET to look the selection up in, not a walk down it for every object: with a PDF's worth of
    // lines on the sheet and all of them picked, walking the list for each was tens of thousands
    // squared - minutes of nothing happening.
    entityList_->setUpdatesEnabled(false);
    const QSignalBlocker block(entityList_);
    entityList_->clear();
    const auto& selList = editor_->selection();
    const QSet<QString> sel(selList.begin(), selList.end());
    // AND THE PICKED ROWS MARKED IN ONE GO, as runs. Marking them one row at a time has the list merge
    // each into everything marked before it - eighty thousand picked lines was a minute and a half.
    QItemSelection picked;
    int runFrom = -1, row = 0;
    QAbstractItemModel* model = entityList_->model();
    auto endRun = [&](int upTo) {
        if (runFrom >= 0) picked.select(model->index(runFrom, 0), model->index(upTo, 0));
        runFrom = -1;
    };
    for (const auto& e : editDoc()->entities()) {
        auto* item = new QListWidgetItem(
            QStringLiteral("%1  [%2]").arg(entityTypeToString(e.type), e.id));
        item->setData(Qt::UserRole, e.id);
        entityList_->addItem(item);
        if (sel.contains(e.id)) {
            if (runFrom < 0) runFrom = row;
        } else {
            endRun(row - 1);
        }
        ++row;
    }
    endRun(row - 1);
    if (!picked.isEmpty()) entityList_->selectionModel()->select(picked, QItemSelectionModel::Select);
    entityList_->setUpdatesEnabled(true);
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
        // Both words find it: the app says "joint", and anyone who learned it as a splice still
        // types that.
        {{"joint", "splice"}, "Harness", "Joint marker", tool(ToolId::Splice)},
        {{"notconnected", "nc"}, "Harness", "Not-connected marker", tool(ToolId::NotConnected)},
        {{"wirelabel", "wl", "netlabel", "nl"}, "Harness", "Wire label",
         tool(ToolId::NetLabel)},
        {{"harness", "hb", "shb"}, "Harness",
         "Schematic harness builder: define the harness and generate it",
         [this] { showHarnessBuilder(); }},
        {{"bom"}, "Harness", "Bill of materials", [this] { showBomDialog(); }},
        {{"block", "blocks", "b"}, "Draw", "Blocks: create, insert and edit",
         [this] { showBlockDialog(); }},
        {{"makeblock", "mkblock"}, "Draw", "Make a block out of what is selected",
         [this] { createBlockFromSelection(); }},
        {{"saveblock"}, "Draw", "Save the block being edited and rebuild every copy",
         [this] { saveBlockBeingEdited(); }},
        {{"cancelblock"}, "Draw", "Leave the block being edited as it was",
         [this] { cancelBlockEdit(); }},
        {{"drawtemplate", "edittemplate"}, "File",
         "Draw the template: open it on a tab of its own and lay the sheet out",
         [this] { openTemplateEditor(QString()); }},
        {{"savetemplate"}, "File", "Save the template being drawn",
         [this] { saveTemplateBeingEdited(false); }},
        {{"template", "tpl", "sheetset"}, "File",
         "Drawing template: the sheet, the layers and the defaults a drawing starts with",
         [this] { showTemplateDialog(); }},
        {{"bundlediameter", "bundle", "bd"}, "Harness",
         "Bundle diameter: the calculation, with the formula and what it assumes",
         [this] { showBundleDiameter(); }},
        {{"thermal", "temp", "heat"}, "Harness",
         "Lumped thermal model: how hot the bundle gets, and what it could carry",
         [this] { showThermalModel(); }},
        {{"mechvalidate", "mdrc"}, "Harness",
         "Mechanical compatibility: whether what is fitted will go on",
         [this] { showMechanicalValidation(); }},
        {{"mechanical", "mhb", "mech"}, "Harness",
         "Mechanical builder: the harness as a physical object, generated to scale",
         [this] { showMechanicalBuilder(); }},
        {{"publish", "output", "plot"}, "Output",
         "Plot the model and the layouts, in order, to a printer or a PDF",
         [this] { showOutput(); }},
        {{"layoutwindow", "viewport", "addwindow"}, "Layout",
         "Put a window onto the drawing on this sheet", [this] { addLayoutWindow(); }},
        {{"mechmodels", "models", "defaults"}, "Harness",
         "Default mechanical models: how this office draws a wire, a cable, heatshrink, a ferrule",
         [this] { showMechModels(); }},
        {{"connline", "markconnection", "attach"}, "Harness",
         "Mark the selection as this part's connection line (where the harness attaches)",
         [this] { markConnectionLine(); }},
        {{"buildwizard", "wizard", "harnesswizard"}, "Harness",
         "Mechanical harness wizard: end parts and the runs between them as bricks, previewed live",
         [this] { showMechWizard(); }},
        {{"stepwizard", "oldwizard"}, "Harness",
         "Step wizard: place one thing at a time, in any order, with the neighbours checked",
         [this] { showStepWizard(); }},
        {{"buildsteps", "guided", "harnesssteps"}, "Harness",
         "Harness build steps (the older fixed eleven-step wizard)",
         [this] { showHarnessSteps(); }},
        {{"voltagedrop", "vdrop", "vd"}, "Harness",
         "Voltage drop: what arrives at the load, wire by wire",
         [this] { showVoltageDropDialog(); }},
        {{"calculator", "calc", "scientific"}, "Tools",
         "Scientific calculator", [this] { showCalculator(); }},
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
        // ARRAY repeats the last type chosen (like the ribbon button); the three
        // explicit names arm that type directly, as in AutoCAD.
        {{"array", "ar"}, "Modify", "Array the selection (last type used)",
         [this] { beginArrayCommand(editor_->lastArrayKind()); }},
        {{"arrayrect", "arrect", "arr"}, "Modify", "Rectangular (linear) array",
         [this] { beginArrayCommand(ArrayKind::Rect); }},
        {{"arraypolar", "arpolar", "arpol"}, "Modify", "Polar array around a centre point",
         [this] { beginArrayCommand(ArrayKind::Polar); }},
        {{"arraypath", "arpath", "artrack"}, "Modify", "Path array tracking along an object",
         [this] { beginArrayCommand(ArrayKind::Path); }},
        {{"matchprop", "ma", "match"}, "Modify",
         "Match properties: paint one object's properties onto others",
         [this] { editor_->beginMatchProps(); }},
        {{"qselect", "qs"}, "Edit", "Quick Select: select objects by property",
         [this] { editor_->beginQuickSelect(); }},
        {{"explode", "x"}, "Modify", "Explode into primitives", [this] { editor_->explodeSelected(); }},
        {{"purge", "pu"}, "Modify", "Purge unused layers / stray objects", [this] { editor_->purgeUnused(); }},
        {{"erase", "e", "delete", "del"}, "Modify", "Delete the selection", [this] { editor_->deleteSelected(); }},
        // Edit
        {{"select", "sel"}, "Edit", "Select tool", tool(ToolId::Select)},
        {{"selectall", "all"}, "Edit", "Select everything", [this] { editor_->selectAll(); }},
        {{"copy", "co", "cp"}, "Edit", "Copy selection to clipboard", [this] { editor_->copySelection(); }},
        {{"cut", "cu"}, "Edit", "Cut selection to clipboard", [this] { editor_->cutSelection(); }},
        {{"paste", "pa"}, "Edit", "Paste from clipboard - a picture if that is what is on it",
         [this] { pasteFromClipboard(); }},
        {{"picture", "image", "img"}, "Insert", "Insert a picture (PNG, JPEG, BMP…) from a file",
         [this] { insertPictureFromFile(); }},
        {{"pastepicture", "pasteimage"}, "Insert", "Place the picture on the clipboard",
         [this] { insertPictureFromClipboard(false); }},
        {{"pdfimport", "pdfin", "importpdf"}, "Insert", "Import vector paths from a PDF",
         [this] { importPdf(); }},
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
        {{"export", "dwgout"}, "File", "Export DWG", [this] { exportDwgAs(); }},
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

namespace {
// EVERY KEY THE DRAWING ANSWERS TO, gathered in one place.
//
// A typed command is discoverable - type the first letters and it is offered. A key combination is
// not discoverable at all: Shift+Space turns the thing you are carrying and nothing on the screen
// will ever tell you so. So the keys that are written nowhere else are written here.
//
// Read off the handlers rather than remembered. Editor::onKey and CanvasWidget::keyPressEvent are
// between them the whole of it, plus the menu accelerators - and the canvas gets first refusal, which
// is why the same key does different things in different states. Each state is a line of its own:
// one row saying "Space" would be a lie three times over.
//
// The menu accelerators are asked of QKeySequence instead of being typed out, so this says whatever
// the platform actually binds rather than assuming Ctrl.
QString nativeKeys(QKeySequence::StandardKey k) {
    const QKeySequence seq(k);
    // Empty means the platform binds nothing to it - Quit has no accelerator on Windows - and the
    // row is dropped rather than printed with a dash. A reference that lists keys which do nothing
    // is worse than one that is short.
    return seq.isEmpty() ? QString() : seq.toString(QKeySequence::NativeText);
}

struct KeyRow {
    QString keys;
    QString what;
    bool header = false;
};

std::vector<KeyRow> keyboardReference() {
    auto head = [](const char* t) { return KeyRow{QString::fromLatin1(t), QString(), true}; };
    auto row = [](const QString& k, const char* t) {
        return KeyRow{k, QString::fromLatin1(t), false};
    };
    auto lit = [&](const char* k, const char* t) { return row(QString::fromLatin1(k), t); };

    std::vector<KeyRow> out;
    out.push_back(head("Getting out of things"));
    out.push_back(lit("Esc", "Cancel what is going on and drop the selection. A dragged joint goes "
                             "back; a part being placed is let go of; a harness returns to the "
                             "builder"));
    out.push_back(lit("Enter", "Finish the object being drawn. Places a pinned preview, closes an "
                               "array, ends a Quick Select pick"));
    out.push_back(lit("Space", "With nothing going on: run the last tool again"));

    out.push_back(head("Carrying something about"));
    out.push_back(lit("Space", "Turn what is being moved a quarter turn clockwise. Only the "
                               "preview turns; the object turns when it is put down"));
    out.push_back(lit("Shift+Space", "The same, anticlockwise"));
    out.push_back(lit("Space  /  R", "Turn what is waiting to be placed - a paste, a block, a part "
                                   "out of the library, a generated harness"));
    out.push_back(lit("Shift+Space  /  Shift+R", "The same, anticlockwise"));
    out.push_back(lit("X  /  Y", "Mirror what is being placed or right-drag-copied: X flips it "
                                 "left to right, Y top to bottom"));

    out.push_back(head("While drawing"));
    out.push_back(lit("Tab", "Pin the preview so the ribbon's numbers can be tried on the real "
                             "thing - twisted bundle, length break, label, connector, screen, twist"));
    out.push_back(lit("Tab", "Where a tool asks for more than one number, move between them "
                             "(Length and Angle, X radius and Y radius)"));
    out.push_back(lit("Ctrl", "Drawing an arc: flip it to the sweep the other way round"));
    out.push_back(lit("Ctrl", "Drawing a double line or double polyline: swap which side of the "
                              "line you draw the second line falls"));
    out.push_back(lit("Shift+1 to Shift+9", "Fanning wires out of a connector: add or drop that one "
                                       "pin's wire"));
    out.push_back(lit("Shift", "Fanning wires out of a connector: route one from every pin"));

    out.push_back(head("Typing while drawing"));
    out.push_back(lit("a number", "How far along the direction the cursor is pointing"));
    out.push_back(lit("x,y", "An absolute point"));
    out.push_back(lit("@dx,dy", "A point relative to the last one"));
    out.push_back(lit("g  /  f", "Set the double-line gap / the polyline fillet radius"));
    out.push_back(lit("a letter", "With nothing going on, starts a command at the cursor"));

    out.push_back(head("The usual editing"));
    out.push_back(row(nativeKeys(QKeySequence::SelectAll), "Select everything"));
    out.push_back(row(nativeKeys(QKeySequence::Copy), "Copy"));
    out.push_back(lit("Ctrl+Shift+C", "Copy, then pick the point it is held by"));
    out.push_back(row(nativeKeys(QKeySequence::Cut), "Cut"));
    out.push_back(row(nativeKeys(QKeySequence::Paste), "Paste - then click to put it down"));
    out.push_back(row(nativeKeys(QKeySequence::Undo), "Undo"));
    out.push_back(row(nativeKeys(QKeySequence::Redo), "Redo"));
    out.push_back(row(nativeKeys(QKeySequence::Delete) + QStringLiteral("  /  Backspace"),
                      "Delete what is selected"));

    out.push_back(head("Files, and looking at them"));
    out.push_back(row(nativeKeys(QKeySequence::New), "New drawing"));
    out.push_back(row(nativeKeys(QKeySequence::Open), "Open a drawing"));
    out.push_back(row(nativeKeys(QKeySequence::Save), "Save"));
    out.push_back(lit("Ctrl+Shift+S", "Save every open drawing"));
    out.push_back(row(nativeKeys(QKeySequence::Print), "Print and export"));
    out.push_back(lit("Ctrl+Shift+F", "Fit the whole drawing on screen"));
    out.push_back(row(nativeKeys(QKeySequence::HelpContents), "This window"));
    out.push_back(row(nativeKeys(QKeySequence::Quit), "Leave Edraw"));
    return out;
}
} // namespace

bool MainWindow::hasCommandNamed(const QString& name) const {
    for (const Command& c : commands_)
        for (const QString& n : c.names)
            if (n.compare(name, Qt::CaseInsensitive) == 0) return true;
    return false;
}

void MainWindow::showHelp() {
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Commands and keys"));
    dlg.resize(680, 680);
    auto* lay = new QVBoxLayout(&dlg);

    // TWO LISTS, because they are two different things and mixing them helps nobody: what you can
    // TYPE, and what you can PRESS. A typed command is discoverable already - the box offers it as
    // soon as the first letters are in - so the keys are the half that actually needed writing down.
    auto* tabs = new QTabWidget(&dlg);
    const QColor headBg = darkTheme_ ? QColor("#313438") : QColor("#d2d2d8");

    auto makeTable = [&](const QString& left, const QString& right, int leftWidth) {
        auto* t = new QTableWidget(&dlg);
        t->setColumnCount(2);
        t->setHorizontalHeaderLabels({left, right});
        t->horizontalHeader()->setStretchLastSection(true);
        t->verticalHeader()->setVisible(false);
        t->setEditTriggers(QAbstractItemView::NoEditTriggers);
        t->setSelectionMode(QAbstractItemView::NoSelection);
        t->setColumnWidth(0, leftWidth);
        t->setWordWrap(true);
        return t;
    };
    auto addTo = [&](QTableWidget* t, const QString& a, const QString& b, bool header) {
        const int r = t->rowCount();
        t->insertRow(r);
        auto* c0 = new QTableWidgetItem(a);
        auto* c1 = new QTableWidgetItem(b);
        if (header) {
            QFont f = c0->font();
            f.setBold(true);
            c0->setFont(f);
            c0->setBackground(headBg);
            c1->setBackground(headBg);
        } else {
            // The key itself reads as a key: fixed pitch, so Ctrl+Shift+S lines up under Ctrl+S.
            QFont f = c0->font();
            f.setFamily(QStringLiteral("Consolas"));
            c0->setFont(f);
        }
        c0->setTextAlignment(Qt::AlignLeft | Qt::AlignTop);
        c1->setTextAlignment(Qt::AlignLeft | Qt::AlignTop);
        t->setItem(r, 0, c0);
        t->setItem(r, 1, c1);
    };

    // ---- What you can type ----
    auto* typed = makeTable(QStringLiteral("Command (aliases)"), QStringLiteral("Description"), 200);
    const QStringList order = {"Draw", "Modify", "Harness", "Edit", "View", "File", "Help"};
    for (const QString& cat : order) {
        bool first = true;
        for (const auto& c : commands_) {
            if (c.category != cat) continue;
            if (first) { addTo(typed, cat, QString(), true); first = false; }
            QString name = c.names.first().toUpper();
            if (c.names.size() > 1) {
                QStringList al;
                for (int i = 1; i < c.names.size(); ++i) al << c.names[i].toUpper();
                name += QStringLiteral("  (%1)").arg(al.join(", "));
            }
            addTo(typed, name, c.desc, false);
        }
    }
    tabs->addTab(typed, QStringLiteral("Commands"));

    // ---- What you can press ----
    auto* keys = makeTable(QStringLiteral("Keys"), QStringLiteral("What it does"), 190);
    for (const KeyRow& k : keyboardReference())
        if (k.header || !k.keys.isEmpty()) addTo(keys, k.keys, k.what, k.header);
    tabs->addTab(keys, QStringLiteral("Keyboard"));

    // SIZED ONCE THE TABLE HAS ITS REAL WIDTH, and again whenever a tab is shown. Called while the
    // dialog is still being built it measures a wrapped column against a width the table has not got
    // yet, and every row comes out several times too tall - one line of text in a box half the window
    // deep. A hidden tab has no width to measure at all, so the second one is sized when it is first
    // turned to.
    auto fit = [typed, keys] { typed->resizeRowsToContents(); keys->resizeRowsToContents(); };
    connect(tabs, &QTabWidget::currentChanged, &dlg, [fit](int) { fit(); });
    QTimer::singleShot(0, &dlg, fit);

    lay->addWidget(tabs);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Close, &dlg);
    connect(box, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    connect(box, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    lay->addWidget(box);
    dlg.exec();
}

// Quick Select (AutoCAD QSELECT): pick objects by property. The property list is the
// one that applies to the sampled object's type — a circle offers Radius / Diameter /
// Area, a wire its gauge / colour / net — plus the properties every object has. The
// value dropdown is filled with the values actually present in the drawing, starting on
// the sampled object's own, so the common case (find everything like this one) is one
// click away.
void MainWindow::showQuickSelectDialog(const QString& sampleId) {
    if (!doc_) return;
    const std::optional<Entity> picked = editor_->entityCopy(sampleId);

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Quick Select"));
    auto* form = new QFormLayout(&dlg);

    const bool haveSelection = !editor_->selection().empty();
    auto* applyTo = new QComboBox(&dlg);
    applyTo->addItem(QStringLiteral("Entire drawing"));
    if (haveSelection) applyTo->addItem(QStringLiteral("Current selection"));
    applyTo->setToolTip(QStringLiteral("Search the whole drawing, or only what is selected now"));

    // Object type: every type present in the drawing, opened on the sampled object's.
    auto pretty = [](const QString& t) {
        // The stored key is what a drawing is written with; what is shown is what the thing is called.
        // "splice" is the key an earlier version wrote for a JOINT and is kept for compatibility.
        if (t == QLatin1String("splice")) return QStringLiteral("Joint");
        return t.isEmpty() ? t : t.left(1).toUpper() + t.mid(1);
    };
    auto* typeCombo = new QComboBox(&dlg);
    typeCombo->addItem(QStringLiteral("Any type"), QString());
    for (const QString& t : editor_->presentEntityTypes()) typeCombo->addItem(pretty(t), t);
    if (picked) {
        const int at = typeCombo->findData(entityTypeToString(picked->type));
        if (at >= 0) typeCombo->setCurrentIndex(at);
    }

    auto* propList = new QListWidget(&dlg);
    propList->setMinimumHeight(160);
    auto* opCombo = new QComboBox(&dlg);
    auto* valueCombo = new QComboBox(&dlg);
    valueCombo->setEditable(true); // any value can be typed, not just ones in the drawing
    valueCombo->setMinimumWidth(180);
    auto* howCombo = new QComboBox(&dlg);
    howCombo->addItem(QStringLiteral("Include in new selection set"));
    howCombo->addItem(QStringLiteral("Exclude from new selection set"));
    auto* appendCb = new QCheckBox(QStringLiteral("Append to current selection set"), &dlg);
    auto* countLbl = new QLabel(&dlg);
    countLbl->setStyleSheet(QStringLiteral("color:#8fb0d6;"));

    form->addRow(QStringLiteral("Apply to:"), applyTo);
    form->addRow(QStringLiteral("Object type:"), typeCombo);
    form->addRow(QStringLiteral("Properties:"), propList);
    form->addRow(QStringLiteral("Operator:"), opCombo);
    form->addRow(QStringLiteral("Value:"), valueCombo);
    form->addRow(QStringLiteral("How to apply:"), howCombo);
    form->addRow(appendCb);
    form->addRow(countLbl);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    // The object the property list is derived from: the picked one while its type is
    // chosen, otherwise the first object of the chosen type in the drawing.
    std::vector<QuickSelectProp> props;
    auto sampleForType = [this, &picked](const QString& type) -> std::optional<Entity> {
        if (type.isEmpty()) return picked;
        if (picked && entityTypeToString(picked->type) == type) return picked;
        for (const auto& e : editDoc()->entities())
            if (entityTypeToString(e.type) == type) return e;
        return std::nullopt;
    };
    auto currentQuery = [&]() {
        QuickSelectQuery q;
        q.wholeDrawing = applyTo->currentIndex() == 0;
        q.type = typeCombo->currentData().toString();
        const int row = propList->currentRow();
        if (row >= 0 && row < static_cast<int>(props.size())) q.key = props[row].key;
        q.op = static_cast<QsOp>(opCombo->currentData().toInt());
        q.value = valueCombo->currentText().trimmed();
        q.exclude = howCombo->currentIndex() == 1;
        q.append = appendCb->isChecked();
        return q;
    };
    // Live count of what the filter would select, so the filter can be judged before
    // committing to it.
    auto updateCount = [&] {
        const auto ids = quickSelectMatchIds(editDoc()->entities(), currentQuery(),
                                             editor_->selection());
        countLbl->setText(QStringLiteral("%1 object(s) match").arg(ids.size()));
    };
    // Operators offered follow the property: numbers can be compared, text can be
    // searched. "Select All" ignores the value and matches on object type alone.
    auto rebuildOps = [&](bool numeric) {
        const QString keep = opCombo->currentText();
        QSignalBlocker block(opCombo);
        opCombo->clear();
        opCombo->addItem(QStringLiteral("="), static_cast<int>(QsOp::Equal));
        opCombo->addItem(QStringLiteral("<>"), static_cast<int>(QsOp::NotEqual));
        if (numeric) {
            opCombo->addItem(QStringLiteral(">"), static_cast<int>(QsOp::Greater));
            opCombo->addItem(QStringLiteral("<"), static_cast<int>(QsOp::Less));
            opCombo->addItem(QStringLiteral(">="), static_cast<int>(QsOp::GreaterEqual));
            opCombo->addItem(QStringLiteral("<="), static_cast<int>(QsOp::LessEqual));
        } else {
            opCombo->addItem(QStringLiteral("contains"), static_cast<int>(QsOp::Contains));
        }
        opCombo->addItem(QStringLiteral("Select All"), static_cast<int>(QsOp::All));
        const int at = opCombo->findText(keep);
        opCombo->setCurrentIndex(at >= 0 ? at : 0);
    };
    auto syncProperty = [&] {
        const int row = propList->currentRow();
        if (row < 0 || row >= static_cast<int>(props.size())) return;
        const QuickSelectProp& p = props[row];
        rebuildOps(p.numeric);
        QSignalBlocker block(valueCombo);
        valueCombo->clear();
        for (const QString& v : editor_->quickSelectValueChoices(p.key)) valueCombo->addItem(v);
        if (const auto s = sampleForType(typeCombo->currentData().toString()))
            valueCombo->setCurrentText(quickSelectValue(*s, p.key)); // start on this object's own
        valueCombo->setEnabled(opCombo->currentData().toInt() != static_cast<int>(QsOp::All));
        updateCount();
    };
    auto rebuildProps = [&] {
        const auto s = sampleForType(typeCombo->currentData().toString());
        props = s ? quickSelectProps(*s) : quickSelectGeneralProps();
        QSignalBlocker block(propList);
        propList->clear();
        for (const QuickSelectProp& p : props) propList->addItem(p.label);
        if (propList->count()) propList->setCurrentRow(0);
        syncProperty();
    };

    connect(typeCombo, &QComboBox::currentIndexChanged, &dlg, [&](int) { rebuildProps(); });
    connect(propList, &QListWidget::currentRowChanged, &dlg, [&](int) { syncProperty(); });
    connect(opCombo, &QComboBox::currentIndexChanged, &dlg, [&](int) {
        valueCombo->setEnabled(opCombo->currentData().toInt() != static_cast<int>(QsOp::All));
        updateCount();
    });
    connect(valueCombo, &QComboBox::currentTextChanged, &dlg, [&](const QString&) { updateCount(); });
    connect(applyTo, &QComboBox::currentIndexChanged, &dlg, [&](int) { updateCount(); });
    connect(howCombo, &QComboBox::currentIndexChanged, &dlg, [&](int) { updateCount(); });
    connect(appendCb, &QCheckBox::toggled, &dlg, [&](bool) { updateCount(); });

    rebuildProps();
    if (dlg.exec() != QDialog::Accepted) return;
    editor_->applyQuickSelect(currentQuery());
    canvas_->setFocus(Qt::OtherFocusReason);
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
    lblCombo->addItem(QStringLiteral("Mixed"), QStringLiteral("mixed")); // A1..A10, B1..B10

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
    // The Wire tab opens while the Wire tool is active.
    showWireRibbon(editor_->tool() == ToolId::Wire);
}

void MainWindow::buildWireRibbonTab() {
    if (insulationMaterials_.isEmpty())
        insulationMaterials_ = {"PVC",      "XLPE",   "PTFE",        "FEP",
                                "Silicone", "Rubber", "Polyethylene"};
    wireTab_ = new QWidget(ribbon_);
    // Bright label/checkbox text (the ribbon's QLabel style doesn't cascade to these
    // inline labels, so they'd otherwise render dark and be unreadable).
    wireTab_->setStyleSheet(QStringLiteral("QLabel,QCheckBox{color:#e6e9ef;background:transparent;}"));
    auto* lay = new QHBoxLayout(wireTab_);
    lay->setContentsMargins(6, 4, 6, 2);
    lay->setSpacing(0);
    lay->setAlignment(Qt::AlignLeft);

    // Groups hidden while a shield is being placed (only the Screen panel shows then).
    std::vector<QWidget*> otherWireGroups;

    // ---- Place: wire + junction markers ----
    // (Screen/shield lives on the Schematic-build tab's Place group, not here.)
    auto* placeGroup = ribbonGroup(
        QStringLiteral("Place"),
        toolRow({ToolId::Wire, ToolId::Splice, ToolId::NotConnected, ToolId::NetLabel}));
    otherWireGroups.push_back(placeGroup);
    lay->addWidget(placeGroup);

    // ---- Wire: colour / AWG / net ----
    auto* wireW = new QWidget(wireTab_);
    auto* wg = new QGridLayout(wireW);
    wg->setContentsMargins(0, 0, 0, 0);
    wg->setSpacing(2);
    auto* colorBox = new QComboBox(wireW);
    colorBox->setEditable(true); // type a custom code / #hex to add it to the list
    colorBox->addItems(wireColorCodes());
    colorBox->addItems(customWireColors_);
    colorBox->setCurrentText(editor_->wireColor());
    connect(colorBox, &QComboBox::currentTextChanged, this, [this, colorBox](const QString& c) {
        editor_->wireColor() = c;
        if (!c.isEmpty() && colorBox->findText(c) < 0 && !wireColorCodes().contains(c)) {
            customWireColors_ << c; // remember custom colours across sessions
            saveSettings();
        }
    });
    auto* awgBox = new QComboBox(wireW);
    awgBox->setEditable(true);
    for (int g = 4; g <= 32; g += 2) awgBox->addItem(QStringLiteral("%1 AWG").arg(g));
    awgBox->setCurrentText(editor_->wireAwg());
    connect(awgBox, &QComboBox::currentTextChanged, this,
            [this](const QString& t) { editor_->wireAwg() = t; });
    auto* netEdit = new QLineEdit(wireW);
    netEdit->setMaximumWidth(110);
    netEdit->setText(editor_->wireNet());
    connect(netEdit, &QLineEdit::textChanged, this,
            [this](const QString& n) { editor_->wireNet() = n; });
    wg->addWidget(new QLabel(QStringLiteral("Color"), wireW), 0, 0);
    wg->addWidget(colorBox, 0, 1, 1, 2);
    wg->addWidget(new QLabel(QStringLiteral("AWG"), wireW), 1, 0);
    wg->addWidget(awgBox, 1, 1, 1, 2);
    wg->addWidget(new QLabel(QStringLiteral("Label"), wireW), 2, 0);
    wg->addWidget(netEdit, 2, 1, 1, 2);
    auto* wireGroup = ribbonGroup(QStringLiteral("Wire"), wireW);
    otherWireGroups.push_back(wireGroup);
    lay->addWidget(wireGroup);

    // ---- Insulation: material (add/delete) / voltage / conductor type ----
    auto* insW = new QWidget(wireTab_);
    auto* ig = new QGridLayout(insW);
    ig->setContentsMargins(0, 0, 0, 0);
    ig->setSpacing(2);
    auto* matBox = new QComboBox(insW);
    matBox->setEditable(true);
    matBox->addItems(insulationMaterials_);
    matBox->setCurrentText(editor_->wireInsulation());
    connect(matBox, &QComboBox::currentTextChanged, this,
            [this](const QString& t) { editor_->wireInsulation() = t; });
    auto* addMat = new QToolButton(insW);
    addMat->setText(QStringLiteral("Add"));
    addMat->setToolTip(QStringLiteral("Add a new insulation material to the menu (kept)"));
    connect(addMat, &QToolButton::clicked, this, [this, matBox] {
        bool ok = false;
        const QString m = QInputDialog::getText(this, QStringLiteral("Add material"),
                                                QStringLiteral("Insulation material:"),
                                                QLineEdit::Normal, QString(), &ok)
                              .trimmed();
        if (ok && !m.isEmpty() && !insulationMaterials_.contains(m)) {
            insulationMaterials_ << m;
            matBox->addItem(m);
            matBox->setCurrentText(m);
            saveSettings();
        }
    });
    auto* delMat = new QToolButton(insW);
    delMat->setText(QStringLiteral("Del"));
    delMat->setToolTip(QStringLiteral("Remove the shown material from the menu"));
    connect(delMat, &QToolButton::clicked, this, [this, matBox] {
        const QString m = matBox->currentText();
        const int idx = matBox->findText(m);
        if (idx >= 0) matBox->removeItem(idx);
        insulationMaterials_.removeAll(m);
        saveSettings();
    });
    auto* voltBox = new QComboBox(insW);
    voltBox->setEditable(true);
    voltBox->addItems({"30V", "60V", "150V", "300V", "600V", "1000V"});
    voltBox->setCurrentText(editor_->wireVoltage());
    connect(voltBox, &QComboBox::currentTextChanged, this,
            [this](const QString& t) { editor_->wireVoltage() = t; });
    auto* typeBox = new QComboBox(insW);
    typeBox->addItems({QStringLiteral("Solid"), QStringLiteral("Stranded")});
    typeBox->setCurrentText(editor_->wireType());
    connect(typeBox, &QComboBox::currentTextChanged, this,
            [this](const QString& t) { editor_->wireType() = t; });
    ig->addWidget(new QLabel(QStringLiteral("Material"), insW), 0, 0);
    ig->addWidget(matBox, 0, 1);
    ig->addWidget(addMat, 0, 2);
    ig->addWidget(delMat, 0, 3);
    ig->addWidget(new QLabel(QStringLiteral("Voltage"), insW), 1, 0);
    ig->addWidget(voltBox, 1, 1, 1, 3);
    ig->addWidget(new QLabel(QStringLiteral("Type"), insW), 2, 0);
    ig->addWidget(typeBox, 2, 1, 1, 3);
    auto* insGroup = ribbonGroup(QStringLiteral("Insulation"), insW);
    otherWireGroups.push_back(insGroup);
    lay->addWidget(insGroup);

    // ---- Resistance: value + per-m / per-ft unit (switching converts the value) ----
    auto* resW = new QWidget(wireTab_);
    auto* rg = new QGridLayout(resW);
    rg->setContentsMargins(0, 0, 0, 0);
    rg->setSpacing(2);
    auto* resSpin = new QDoubleSpinBox(resW);
    resSpin->setRange(0.0, 1e6);
    resSpin->setDecimals(4);
    resSpin->setValue(editor_->wireRes());
    connect(resSpin, &QDoubleSpinBox::editingFinished, this,
            [this, resSpin] { editor_->wireRes() = resSpin->value(); });
    auto* resUnitBox = new QComboBox(resW);
    resUnitBox->addItems({QString::fromUtf8("Ω/m"), QString::fromUtf8("Ω/ft")});
    resUnitBox->setCurrentIndex(editor_->wireResUnit() == QLatin1String("ft") ? 1 : 0);
    connect(resUnitBox, &QComboBox::currentTextChanged, this,
            [this, resSpin](const QString& t) {
                const bool toFt = t.contains(QLatin1String("ft"));
                const bool curFt = editor_->wireResUnit() == QLatin1String("ft");
                if (curFt != toFt && resSpin->value() != 0.0) {
                    const double v = toFt ? resSpin->value() * 0.3048 : resSpin->value() / 0.3048;
                    QSignalBlocker b(resSpin);
                    resSpin->setValue(v);
                    editor_->wireRes() = v;
                }
                editor_->wireResUnit() = toFt ? QStringLiteral("ft") : QStringLiteral("m");
            });
    rg->addWidget(new QLabel(QString::fromUtf8("Resistance"), resW), 0, 0, 1, 2);
    rg->addWidget(resSpin, 1, 0);
    rg->addWidget(resUnitBox, 1, 1);
    auto* resGroup = ribbonGroup(QStringLiteral("Resistance"), resW);
    otherWireGroups.push_back(resGroup);
    lay->addWidget(resGroup);

    // Cross switch and Twist type live in the Modify ribbon group, not here — the Wire
    // ribbon stays focused on drawing/insulation/routing.

    // ---- Shield fields: appear only WHILE placing a shield (armed via the Shield
    // button). Two side-by-side blocks, each 3 rows tall (a ribbon panel is only ~3 rows
    // high, so content must spread wide, not tall):
    //   Left  (bordered box): Small radius / Large radius / Ratio — each label + field +
    //                         a "Lock" checkbox.
    //   Right: Leg side / Leg length / Amount + Show S.
    auto* scW = new QWidget(wireTab_);
    auto* scOuter = new QHBoxLayout(scW);
    scOuter->setContentsMargins(4, 2, 4, 2);
    scOuter->setSpacing(12);
    auto scLabel = [](QWidget* parent, const QString& text) {
        auto* l = new QLabel(text, parent);
        l->setMinimumWidth(78);
        return l;
    };

    // ---- Left: the radii + ratio, wrapped in a single-line bounding box ----
    auto* radiiFrame = new QFrame(scW);
    radiiFrame->setObjectName(QStringLiteral("shieldRadii"));
    radiiFrame->setStyleSheet(QStringLiteral(
        "QFrame#shieldRadii{border:1px solid #6a7078;border-radius:3px;}"));
    auto* radiiG = new QGridLayout(radiiFrame);
    radiiG->setContentsMargins(6, 4, 6, 4);
    radiiG->setHorizontalSpacing(8);
    radiiG->setVerticalSpacing(6);

    auto* scRx = new QDoubleSpinBox(radiiFrame);
    scRx->setRange(1.0, 500.0);
    scRx->setDecimals(1);
    scRx->setMinimumWidth(70);
    scRx->setButtonSymbols(QAbstractSpinBox::NoButtons);
    auto* scRy = new QDoubleSpinBox(radiiFrame);
    scRy->setRange(1.0, 500.0);
    scRy->setDecimals(1);
    scRy->setMinimumWidth(70);
    scRy->setButtonSymbols(QAbstractSpinBox::NoButtons);
    auto* scRatio = new QDoubleSpinBox(radiiFrame);
    scRatio->setRange(0.2, 20.0);
    scRatio->setDecimals(2);
    scRatio->setSingleStep(0.1);
    scRatio->setMinimumWidth(70);
    scRatio->setButtonSymbols(QAbstractSpinBox::NoButtons);
    scRatio->setToolTip(QStringLiteral("Large ÷ Small radius"));
    // Two "Lock" checkboxes: one locks BOTH radii together, one locks the ratio. With the
    // ratio locked and the radii unlocked, editing one radius drives the other to keep the
    // ratio; a checked lock disables the field(s) it covers.
    auto* scRadiiLock = new QCheckBox(QStringLiteral("Lock"), radiiFrame);
    auto* scRatioLock = new QCheckBox(QStringLiteral("Lock"), radiiFrame);
    scRadiiLock->setToolTip(QStringLiteral("Lock the small AND large radius together"));
    scRatioLock->setToolTip(QStringLiteral(
        "Lock the ratio: with the radii unlocked, changing one radius adjusts the other to "
        "keep Large ÷ Small fixed"));

    // Push the current Editor state into every field (values + enabled + checkboxes).
    auto refreshShieldRadii = [this, scRx, scRy, scRatio, scRadiiLock, scRatioLock] {
        QSignalBlocker b1(scRx), b2(scRy), b3(scRatio), b4(scRadiiLock), b5(scRatioLock);
        scRx->setValue(editor_->screenRx());
        scRy->setValue(editor_->screenRy());
        scRatio->setValue(editor_->screenRatio());
        scRadiiLock->setChecked(editor_->screenRxLock()); // rx/ry locks move together
        scRatioLock->setChecked(editor_->screenRatioLock());
        // A locked field can't be edited directly.
        scRx->setEnabled(!editor_->screenRxLock());
        scRy->setEnabled(!editor_->screenRyLocked());
        scRatio->setEnabled(!editor_->screenRatioLock());
        if (canvas_) canvas_->update(); // repaint so the shield preview reflects the change
    };

    connect(scRx, &QDoubleSpinBox::editingFinished, this, [this, refreshShieldRadii, scRx] {
        editor_->screenRx() = scRx->value();
        if (editor_->screenRatioLock()) // ratio held: drive the other radius
            editor_->screenRy() = std::max(1.0, scRx->value() * editor_->screenRatio());
        else if (scRx->value() > 1e-6) // free: the ratio absorbs the change
            editor_->screenRatio() = editor_->screenRy() / scRx->value();
        refreshShieldRadii();
    });
    connect(scRy, &QDoubleSpinBox::editingFinished, this, [this, refreshShieldRadii, scRy] {
        editor_->screenRy() = scRy->value();
        if (editor_->screenRatioLock()) {
            const double ratio = editor_->screenRatio();
            editor_->screenRx() =
                std::max(1.0, ratio > 1e-6 ? scRy->value() / ratio : editor_->screenRx());
        } else if (editor_->screenRx() > 1e-6) {
            editor_->screenRatio() = scRy->value() / editor_->screenRx();
        }
        refreshShieldRadii();
    });
    connect(scRatio, &QDoubleSpinBox::editingFinished, this, [this, refreshShieldRadii, scRatio] {
        editor_->screenRatio() = scRatio->value();
        // Keep the locked radius fixed and drive the unlocked one; default to driving Large.
        if (editor_->screenRyLocked() && !editor_->screenRxLock())
            editor_->screenRx() = std::max(
                1.0, scRatio->value() > 1e-6 ? editor_->screenRy() / scRatio->value()
                                             : editor_->screenRx());
        else
            editor_->screenRy() = std::max(1.0, editor_->screenRx() * scRatio->value());
        refreshShieldRadii();
    });
    connect(scRadiiLock, &QCheckBox::toggled, this, [this, refreshShieldRadii](bool on) {
        editor_->screenRxLock() = on;      // both radius locks move together
        editor_->screenRyLocked() = on;
        refreshShieldRadii();
    });
    connect(scRatioLock, &QCheckBox::toggled, this,
            [this, refreshShieldRadii](bool on) { editor_->screenRatioLock() = on; refreshShieldRadii(); });

    radiiG->addWidget(scLabel(radiiFrame, QStringLiteral("Small radius")), 0, 0);
    radiiG->addWidget(scRx, 0, 1);
    // One "Lock" for both radii, on the small-radius row spanning both radius rows.
    radiiG->addWidget(scRadiiLock, 0, 2, 2, 1, Qt::AlignVCenter);
    radiiG->addWidget(scLabel(radiiFrame, QStringLiteral("Large radius")), 1, 0);
    radiiG->addWidget(scRy, 1, 1);
    radiiG->addWidget(scLabel(radiiFrame, QStringLiteral("Ratio (L÷S)")), 2, 0);
    radiiG->addWidget(scRatio, 2, 1);
    radiiG->addWidget(scRatioLock, 2, 2);
    scOuter->addWidget(radiiFrame);

    // ---- Right: leg + amount + display ----
    auto* rightW = new QWidget(scW);
    auto* rightG = new QGridLayout(rightW);
    rightG->setContentsMargins(0, 0, 0, 0);
    rightG->setHorizontalSpacing(8);
    rightG->setVerticalSpacing(6);
    auto* scLegDir = new QComboBox(rightW);
    scLegDir->setMinimumWidth(90);
    scLegDir->addItems({QStringLiteral("Bottom"), QStringLiteral("Top"),
                        QStringLiteral("Left"), QStringLiteral("Right")});
    connect(scLegDir, &QComboBox::currentTextChanged, this, [this](const QString& t) {
        editor_->screenLegDir() = t.toLower();
        if (canvas_) canvas_->update();
    });
    auto* scLeg = new QDoubleSpinBox(rightW);
    scLeg->setRange(0.0, 5000.0);
    scLeg->setDecimals(1);
    scLeg->setMinimumWidth(70);
    scLeg->setButtonSymbols(QAbstractSpinBox::NoButtons);
    connect(scLeg, &QDoubleSpinBox::editingFinished, this, [this, scLeg] {
        editor_->screenLeg() = scLeg->value();
        if (canvas_) canvas_->update();
    });
    // Amount: how many shields to place per drag (1-5), laid side by side.
    auto* scAmount = new QComboBox(rightW);
    for (int n = 1; n <= 5; ++n) scAmount->addItem(QString::number(n));
    scAmount->setMinimumWidth(52);
    scAmount->setToolTip(QStringLiteral("Place this many shields at once, side by side"));
    connect(scAmount, &QComboBox::currentTextChanged, this, [this](const QString& t) {
        editor_->screenAmount() = std::max(1, t.toInt());
        if (canvas_) canvas_->update();
    });
    auto* scShowS = new QCheckBox(QStringLiteral("Show S"), rightW);
    connect(scShowS, &QCheckBox::toggled, this, [this](bool on) {
        editor_->screenShowS() = on;
        if (canvas_) canvas_->update();
    });
    // "Not Connected": marks the drain leg's end with an "X" (deliberately unterminated).
    // Sticky default for the next shield placed, like "Show S"; unchecked by default.
    auto* scNotConnected = new QCheckBox(QStringLiteral("Not Connected"), rightW);
    connect(scNotConnected, &QCheckBox::toggled, this, [this](bool on) {
        editor_->screenNotConnected() = on;
        if (canvas_) canvas_->update();
    });
    // Size of that "X" (half-size, world units). Only meaningful with the box ticked, so
    // the field enables/disables with it.
    auto* scNcSize = new QDoubleSpinBox(rightW);
    scNcSize->setRange(0.5, 100.0);
    scNcSize->setDecimals(1);
    scNcSize->setMinimumWidth(60);
    scNcSize->setButtonSymbols(QAbstractSpinBox::NoButtons);
    scNcSize->setToolTip(QStringLiteral("Size of the \"X\" terminating the drain leg"));
    connect(scNcSize, &QDoubleSpinBox::editingFinished, this, [this, scNcSize] {
        editor_->screenNcSize() = scNcSize->value();
        if (canvas_) canvas_->update();
    });
    auto* scNcSizeLabel = scLabel(rightW, QStringLiteral("X size"));
    scNcSize->setEnabled(editor_->screenNotConnected());
    connect(scNotConnected, &QCheckBox::toggled, scNcSize, &QWidget::setEnabled);
    // "S" label text size.
    auto* scSSize = new QDoubleSpinBox(rightW);
    scSSize->setRange(1.0, 100.0);
    scSSize->setDecimals(1);
    scSSize->setMinimumWidth(60);
    scSSize->setButtonSymbols(QAbstractSpinBox::NoButtons);
    scSSize->setToolTip(QStringLiteral("Size of the \"S\" shield label"));
    connect(scSSize, &QDoubleSpinBox::editingFinished, this, [this, scSSize] {
        editor_->screenSSize() = scSSize->value();
        if (canvas_) canvas_->update();
    });
    // Shield outline shape: ellipse or a capsule ("Semi circle" — two semicircle caps
    // joined by straight sides).
    auto* scShape = new QComboBox(rightW);
    scShape->addItems({QStringLiteral("Ellipse"), QStringLiteral("Semi circle")});
    scShape->setMinimumWidth(96);
    scShape->setToolTip(QStringLiteral("Shield outline shape"));
    connect(scShape, &QComboBox::currentTextChanged, this, [this](const QString& t) {
        editor_->screenShape() =
            t.startsWith(QLatin1String("Semi")) ? QStringLiteral("capsule") : QStringLiteral("ellipse");
        if (canvas_) canvas_->update();
    });
    rightG->addWidget(scLabel(rightW, QStringLiteral("Leg side")), 0, 0);
    rightG->addWidget(scLegDir, 0, 1);
    rightG->addWidget(scLabel(rightW, QStringLiteral("Type")), 0, 2);
    rightG->addWidget(scShape, 0, 3);
    rightG->addWidget(scLabel(rightW, QStringLiteral("Leg length")), 1, 0);
    rightG->addWidget(scLeg, 1, 1);
    rightG->addWidget(scLabel(rightW, QStringLiteral("S size")), 1, 2);
    rightG->addWidget(scSSize, 1, 3);
    rightG->addWidget(scLabel(rightW, QStringLiteral("Amount")), 2, 0);
    rightG->addWidget(scAmount, 2, 1);
    rightG->addWidget(scShowS, 2, 2, 1, 2);
    rightG->addWidget(scNotConnected, 3, 0, 1, 2);
    rightG->addWidget(scNcSizeLabel, 3, 2);
    rightG->addWidget(scNcSize, 3, 3);
    scOuter->addWidget(rightW);

    // ---- Style: line type / dash scale / colour ----
    auto* styleW = new QWidget(scW);
    auto* styleG = new QGridLayout(styleW);
    styleG->setContentsMargins(0, 0, 0, 0);
    styleG->setHorizontalSpacing(8);
    styleG->setVerticalSpacing(6);
    auto* scLineType = new QComboBox(styleW);
    scLineType->addItems({QStringLiteral("Solid"), QStringLiteral("Dashed")});
    scLineType->setMinimumWidth(84);
    connect(scLineType, &QComboBox::currentTextChanged, this, [this](const QString& t) {
        editor_->screenLineType() =
            (t == QLatin1String("Dashed")) ? QStringLiteral("dashed") : QStringLiteral("solid");
        if (canvas_) canvas_->update();
    });
    auto* scDash = new QDoubleSpinBox(styleW);
    scDash->setRange(0.1, 20.0);
    scDash->setDecimals(1);
    scDash->setSingleStep(0.5);
    scDash->setMinimumWidth(60);
    scDash->setButtonSymbols(QAbstractSpinBox::NoButtons);
    connect(scDash, &QDoubleSpinBox::editingFinished, this, [this, scDash] {
        editor_->screenDashScale() = scDash->value();
        if (canvas_) canvas_->update();
    });
    // Colour: a swatch button with a By-Layer / standard-colours / Custom menu.
    auto swatch = [](const QColor& c) {
        QPixmap pm(16, 16);
        pm.fill(c.isValid() ? c : QColor(110, 116, 124));
        QPainter ip(&pm);
        ip.setPen(QColor(30, 34, 40));
        ip.drawRect(0, 0, 15, 15);
        ip.end();
        return QIcon(pm);
    };
    auto* scColorBtn = new QToolButton(styleW);
    scColorBtn->setPopupMode(QToolButton::InstantPopup);
    scColorBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    scColorBtn->setMinimumWidth(96);
    auto* colorMenu = new QMenu(scColorBtn);
    auto setShieldColor = [this, scColorBtn, swatch](const QString& hex, const QString& label) {
        editor_->screenColor() = hex; // "" = ByLayer
        scColorBtn->setText(label);
        scColorBtn->setIcon(swatch(hex.isEmpty() ? QColor() : QColor(hex)));
        if (canvas_) canvas_->update();
    };
    colorMenu->addAction(QStringLiteral("By Layer"), this,
                         [setShieldColor] { setShieldColor(QString(), QStringLiteral("By Layer")); });
    const std::pair<const char*, const char*> stdCols[] = {
        {"Red", "#ff0000"},  {"Yellow", "#ffff00"}, {"Green", "#00c000"}, {"Cyan", "#00c8c8"},
        {"Blue", "#4060ff"}, {"Magenta", "#ff00ff"}, {"White", "#ffffff"}};
    for (const auto& c : stdCols) {
        const QString hex = QString::fromLatin1(c.second), name = QString::fromLatin1(c.first);
        colorMenu->addAction(swatch(QColor(hex)), name, this,
                             [setShieldColor, hex, name] { setShieldColor(hex, name); });
    }
    colorMenu->addSeparator();
    colorMenu->addAction(QStringLiteral("Custom…"), this, [this, setShieldColor] {
        const QColor c = QColorDialog::getColor(QColor(editor_->screenColor().isEmpty()
                                                           ? QStringLiteral("#ffffff")
                                                           : editor_->screenColor()),
                                                this, QStringLiteral("Shield colour"));
        if (c.isValid()) setShieldColor(c.name(), QStringLiteral("Custom"));
    });
    scColorBtn->setMenu(colorMenu);
    setShieldColor(QString(), QStringLiteral("By Layer")); // initial = ByLayer
    styleG->addWidget(scLabel(styleW, QStringLiteral("Line")), 0, 0);
    styleG->addWidget(scLineType, 0, 1);
    styleG->addWidget(scLabel(styleW, QStringLiteral("LT scale")), 1, 0);
    styleG->addWidget(scDash, 1, 1);
    styleG->addWidget(scLabel(styleW, QStringLiteral("Color")), 2, 0);
    styleG->addWidget(scColorBtn, 2, 1);
    scOuter->addWidget(styleW);

    auto* scGroup = ribbonGroup(QStringLiteral("Shield"), scW);
    scGroup->setVisible(false); // shown only while a shield is being placed
    lay->addWidget(scGroup);

    // ---- Routing: gap between the parallel wire lanes after a knee ----
    auto* routeW = new QWidget(wireTab_);
    auto* rgl = new QGridLayout(routeW);
    rgl->setContentsMargins(0, 0, 0, 0);
    rgl->setSpacing(2);
    auto* gapRule = new QComboBox(routeW);
    gapRule->addItems({QStringLiteral("Connector pitch"), QStringLiteral("Custom")});
    gapRule->setCurrentIndex(editor_->wireGapRule() == QLatin1String("custom") ? 1 : 0);
    gapRule->setToolTip(QStringLiteral(
        "Pitch rule: a wire's knee sits one pitch from its neighbour wire's knee — the "
        "defined/connected connector pitch (Connector pitch) or a Custom value"));
    auto* gapVal = new QDoubleSpinBox(routeW);
    gapVal->setRange(0.5, 1000.0);
    gapVal->setDecimals(2);
    gapVal->setValue(editor_->wireGapCustom());
    gapVal->setButtonSymbols(QAbstractSpinBox::NoButtons); // plain field, no +/- steppers
    gapVal->setMinimumWidth(70);
    // The custom pitch field only makes sense when "Custom" is chosen, so it sits to
    // the SIDE of the fall-menu and is enabled only then.
    gapVal->setEnabled(editor_->wireGapRule() == QLatin1String("custom"));
    connect(gapRule, &QComboBox::currentTextChanged, this, [this, gapVal](const QString& t) {
        const bool custom = t == QLatin1String("Custom");
        editor_->wireGapRule() = custom ? QStringLiteral("custom") : QStringLiteral("pitch");
        gapVal->setEnabled(custom);
    });
    connect(gapVal, &QDoubleSpinBox::editingFinished, this,
            [this, gapVal] { editor_->wireGapCustom() = gapVal->value(); });
    auto* ortho90 = new QCheckBox(QStringLiteral("90° only"), routeW);
    ortho90->setChecked(editor_->wireOrtho());
    ortho90->setToolTip(QStringLiteral("Constrain wire drawing to horizontal / vertical segments"));
    connect(ortho90, &QCheckBox::toggled, this, [this](bool on) { editor_->wireOrtho() = on; });
    // The pitch rule ALSO governs live routing: the wire being drawn is held one pitch
    // clear of the runs already on the drawing, so parallel runs can never end up on top
    // of each other. It uses the very same rule above (connector pitch / custom), which is
    // why there is no separate distance here — a wire pulled off a connector lines up with
    // the pins it came from. Applies to a single wire too, not only a fan-out.
    auto* clearOn = new QCheckBox(QStringLiteral("Keep clearance while routing"), routeW);
    clearOn->setChecked(editor_->wireClearanceOn());
    clearOn->setToolTip(QStringLiteral(
        "While routing, hold one pitch clear of neighbouring wire runs — the same Pitch "
        "rule as above (the connector's pin pitch, or the custom value). Applies to a "
        "single wire as well as a fan-out."));
    connect(clearOn, &QCheckBox::toggled, this,
            [this](bool on) { editor_->wireClearanceOn() = on; });
    rgl->setHorizontalSpacing(8);
    rgl->addWidget(ortho90, 0, 0, 1, 3);
    rgl->addWidget(new QLabel(QStringLiteral("Pitch rule"), routeW), 1, 0);
    // Fall-menu + its value on the SAME row, value to the side of the dropdown.
    rgl->addWidget(gapRule, 1, 1);
    rgl->addWidget(gapVal, 1, 2);
    rgl->addWidget(clearOn, 2, 0, 1, 3);
    auto* routeGroup = ribbonGroup(QStringLiteral("Routing"), routeW);
    otherWireGroups.push_back(routeGroup);

    // ---- Marking: what is written along the wire itself ----
    // The colour and the cross-section are switched on independently and repeated
    // `Count` times along the run. These are the defaults for wires drawn next, and they
    // are applied to any wires currently selected as well.
    auto* markW = new QWidget(wireTab_);
    auto* mkg = new QGridLayout(markW);
    mkg->setContentsMargins(0, 0, 0, 0);
    mkg->setSpacing(2);
    mkg->setHorizontalSpacing(8);
    auto* showCol = new QCheckBox(QStringLiteral("Colour"), markW);
    showCol->setChecked(editor_->wireShowColorName());
    showCol->setToolTip(QStringLiteral("Write the wire colour along the wire"));
    auto* colMode = new QComboBox(markW);
    colMode->addItems({QStringLiteral("2 letters"), QStringLiteral("Full name")});
    colMode->setCurrentIndex(editor_->wireColorNameMode() == QLatin1String("full") ? 1 : 0);
    colMode->setToolTip(QStringLiteral("Show the colour as its 2-letter code (WH) or its "
                                      "full name (White)"));
    colMode->setEnabled(editor_->wireShowColorName());
    auto* showGauge = new QCheckBox(QStringLiteral("Gauge"), markW);
    showGauge->setChecked(editor_->wireShowGaugeName());
    showGauge->setToolTip(QStringLiteral("Write the cross-section along the wire"));
    auto* markCount = new QSpinBox(markW);
    markCount->setRange(1, 20);
    markCount->setValue(editor_->wireAnnotCountDefault());
    markCount->setButtonSymbols(QAbstractSpinBox::PlusMinus);
    markCount->setMinimumWidth(56);
    markCount->setToolTip(QStringLiteral("How many times the marking repeats along the wire "
                                        "(each one can then be dragged along it)"));
    auto* markSize = new QDoubleSpinBox(markW);
    markSize->setRange(1.0, 50.0);
    markSize->setDecimals(2);
    markSize->setValue(editor_->wireAnnotSize());
    markSize->setButtonSymbols(QAbstractSpinBox::NoButtons);
    markSize->setMinimumWidth(60);
    markSize->setToolTip(QStringLiteral("Marking text height (world units)"));
    // Every control writes the default AND paints the current selection, so the group
    // works both before drawing and on wires already on the sheet.
    auto pushMarking = [this] { editor_->applyWireMarkingToSelection(); };
    connect(showCol, &QCheckBox::toggled, this, [this, colMode, pushMarking](bool on) {
        editor_->wireShowColorName() = on;
        colMode->setEnabled(on);
        pushMarking();
    });
    connect(colMode, &QComboBox::currentTextChanged, this,
            [this, pushMarking](const QString& t) {
                editor_->wireColorNameMode() = t.startsWith(QLatin1String("Full"))
                                                   ? QStringLiteral("full")
                                                   : QStringLiteral("code");
                pushMarking();
            });
    connect(showGauge, &QCheckBox::toggled, this, [this, pushMarking](bool on) {
        editor_->wireShowGaugeName() = on;
        pushMarking();
    });
    connect(markCount, &QSpinBox::valueChanged, this, [this, pushMarking](int v) {
        editor_->wireAnnotCountDefault() = v;
        pushMarking();
    });
    connect(markSize, &QDoubleSpinBox::editingFinished, this,
            [this, markSize, pushMarking] {
                editor_->wireAnnotSize() = markSize->value();
                pushMarking();
            });
    mkg->addWidget(showCol, 0, 0);
    mkg->addWidget(colMode, 0, 1, 1, 2);
    mkg->addWidget(showGauge, 1, 0);
    mkg->addWidget(new QLabel(QStringLiteral("Count"), markW), 1, 1);
    mkg->addWidget(markCount, 1, 2);
    mkg->addWidget(new QLabel(QStringLiteral("Size"), markW), 2, 1);
    mkg->addWidget(markSize, 2, 2);
    auto* markGroup = ribbonGroup(QStringLiteral("Marking"), markW);
    otherWireGroups.push_back(markGroup);
    // Logical order: Place | Routing (drawing geometry) | Marking | Wire | Insulation |
    // Resistance. Routing/Marking are created last but inserted right after Place so the
    // geometry/behaviour groups lead and the electrical-property groups follow.
    lay->insertWidget(1, routeGroup);
    lay->insertWidget(2, markGroup);

    // Show/hide the Screen panel + (re)load its fields from the Editor's current
    // defaults whenever placement arms, is cancelled, is placed, or is frozen (Tab).
    // While armed, every OTHER Wire-ribbon group is hidden so only Screen shows.
    auto syncScreenGroup = [this, scGroup, scShowS, scSSize, scNotConnected, scNcSize,
                           refreshShieldRadii, scLegDir, scLeg, scAmount, scShape, scLineType,
                           scDash, otherWireGroups] {
        const bool armed = editor_->screenArmed();
        scGroup->setVisible(armed);
        for (QWidget* g : otherWireGroups) g->setVisible(!armed);
        if (!armed) return;
        refreshShieldRadii(); // radii + ratio values, enable states, lock checkboxes
        QSignalBlocker b1(scShowS), b2(scLegDir), b3(scLeg), b4(scAmount), b5(scShape),
            b6(scLineType), b7(scDash), b8(scSSize), b9(scNotConnected), b10(scNcSize);
        scShowS->setChecked(editor_->screenShowS());
        scSSize->setValue(editor_->screenSSize());
        scNotConnected->setChecked(editor_->screenNotConnected());
        scNcSize->setValue(editor_->screenNcSize());
        scNcSize->setEnabled(editor_->screenNotConnected());
        const QString dir = editor_->screenLegDir();
        scLegDir->setCurrentText(dir.isEmpty() ? QStringLiteral("Bottom")
                                               : dir.left(1).toUpper() + dir.mid(1));
        scLeg->setValue(editor_->screenLeg());
        scAmount->setCurrentText(QString::number(std::max(1, editor_->screenAmount())));
        scShape->setCurrentText(editor_->screenShape() == QLatin1String("capsule")
                                    ? QStringLiteral("Semi circle")
                                    : QStringLiteral("Ellipse"));
        scLineType->setCurrentText(editor_->screenLineType() == QLatin1String("dashed")
                                       ? QStringLiteral("Dashed")
                                       : QStringLiteral("Solid"));
        scDash->setValue(editor_->screenDashScale());
    };
    connect(editor_, &Editor::screenArmedChanged, this, [syncScreenGroup] { syncScreenGroup(); });

    lay->addStretch(1);
    wireTab_->hide();
}

void MainWindow::showWireRibbon(bool on) {
    if (!wireTab_ || !ribbon_) return;
    const int idx = ribbon_->indexOf(wireTab_);
    if (on) {
        if (idx < 0) {
            const int at = ribbon_->addTab(wireTab_, QStringLiteral("Wire"));
            ribbon_->setCurrentIndex(at);
        } else {
            ribbon_->setCurrentIndex(idx);
        }
        wireTab_->show();
    } else if (idx >= 0) {
        ribbon_->removeTab(idx); // detaches, does not delete
        // Return to the Schematic build tab (where wiring/shield was launched from).
        int sb = 0;
        for (int i = 0; i < ribbon_->count(); ++i)
            if (ribbon_->tabText(i) == QStringLiteral("Schematic build")) { sb = i; break; }
        ribbon_->setCurrentIndex(sb);
    }
}

QToolButton* MainWindow::makePartsLibraryButton(QWidget* parent) {
    auto* btn = new QToolButton(parent);
    btn->setText(QStringLiteral("Parts\nlibrary"));
    btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    btn->setIconSize(QSize(30, 30));
    btn->setMinimumWidth(96);
    btn->setAutoRaise(true);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setToolTip(QStringLiteral(
        "The raw materials a harness is built from - wires, connectors, contacts, crimps, "
        "heatshrink, backshells, braid - each with its manufacturer, part number and technical "
        "parameters, and with a schematic symbol and a mechanical drawing of its own"));
    {
        // A drawer of parts: a box with three compartments and a handle.
        QPixmap pm(32, 32);
        pm.fill(Qt::transparent);
        QPainter ip(&pm);
        ip.setRenderHint(QPainter::Antialiasing, true);
        ip.setPen(QPen(QColor("#8fb7ff"), 1.7));
        ip.drawRect(4, 9, 24, 17);
        ip.drawLine(4, 15, 28, 15);
        ip.drawLine(12, 15, 12, 26);
        ip.drawLine(20, 15, 20, 26);
        ip.setPen(QPen(QColor("#7ee0a0"), 1.5));
        ip.drawLine(11, 6, 21, 6);
        ip.drawLine(16, 6, 16, 9);
        ip.end();
        btn->setIcon(QIcon(pm));
    }
    connect(btn, &QToolButton::clicked, this, [this] { showPartsLibrary(); });
    return btn;
}

QToolButton* MainWindow::makeVoltageDropButton(QWidget* parent) {
    auto* btn = new QToolButton(parent);
    btn->setText(QStringLiteral("Voltage\ndrop"));
    btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    btn->setIconSize(QSize(26, 26));
    btn->setMinimumWidth(84);
    btn->setAutoRaise(true);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setToolTip(QStringLiteral(
        "What arrives at the load: the drop along the wires, across the contacts and through the "
        "joints, broken down segment by segment"));
    {
        // A supply level falling along a run: the full line, then the sag, then what is left of it.
        QPixmap pm(28, 28);
        pm.fill(Qt::transparent);
        QPainter ip(&pm);
        ip.setRenderHint(QPainter::Antialiasing, true);
        ip.setPen(QPen(QColor("#8fb7ff"), 1.6));
        ip.drawLine(4, 8, 24, 8);                     // the supply
        ip.setPen(QPen(QColor("#e6b45a"), 1.8));
        ip.drawLine(4, 10, 24, 20);                   // the sag along the harness
        ip.setPen(QPen(QColor("#7ee0a0"), 1.4));
        ip.drawLine(4, 10, 4, 8);
        ip.drawLine(24, 20, 24, 8);                   // what the drop cost, at each end
        ip.setBrush(QColor("#e6b45a"));
        ip.drawEllipse(QPointF(24, 20), 2.0, 2.0);
        ip.end();
        btn->setIcon(QIcon(pm));
    }
    connect(btn, &QToolButton::clicked, this, [this] { showVoltageDropDialog(); });
    return btn;
}

QToolButton* MainWindow::makeCalculatorButton(QWidget* parent) {
    auto* btn = new QToolButton(parent);
    btn->setText(QStringLiteral("Scientific\ncalculator"));
    btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    btn->setIconSize(QSize(26, 26));
    btn->setMinimumWidth(84);
    btn->setAutoRaise(true);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setToolTip(QStringLiteral(
        "A scientific calculator - arithmetic, powers, roots, logs and trigonometry. It opens as a "
        "side panel and can be moved, docked or floated like the other panels"));
    {
        // The calculator as a little body, a display and two rows of keys.
        QPixmap pm(28, 28);
        pm.fill(Qt::transparent);
        QPainter ip(&pm);
        ip.setRenderHint(QPainter::Antialiasing, true);
        ip.setPen(QPen(QColor("#8fb7ff"), 1.5));
        ip.setBrush(Qt::NoBrush);
        ip.drawRoundedRect(6, 3, 16, 22, 2, 2);
        ip.drawRect(8, 6, 12, 4);
        ip.setPen(QPen(QColor("#7ee0a0"), 1.2));
        for (int y = 14; y <= 21; y += 4)
            for (int x = 9; x <= 19; x += 5) ip.drawPoint(x, y);
        ip.end();
        btn->setIcon(QIcon(pm));
    }
    connect(btn, &QToolButton::clicked, this, [this] { showCalculator(); });
    return btn;
}

void MainWindow::showPartsLibrary() {
    // Modeless and kept alive: a part is described while the sheet stays usable, which is what
    // lets a symbol be drawn, captured, and dropped back without closing anything.
    if (!libraryDlg_) {
        libraryDlg_ = new PartsLibraryDialog(
            editor_, this, [this](QString partId, LibraryPartDrawing kind) {
                openLibraryPartEditor(partId, kind);
            });
        // Capturing a drawing for a part: the library window steps aside, and the ribbon takes over
        // saying what to do - so the sheet is clear to work on and it is still obvious what is going
        // on and how to finish.
        connect(libraryDlg_, &PartsLibraryDialog::captureArmed, this,
                [this](bool mechanical, const QString& what) {
                    armCapture(QStringLiteral("%1 — the %2 drawing")
                                   .arg(what, mechanical ? QStringLiteral("mechanical")
                                                         : QStringLiteral("schematic")),
                               mechanical ? QStringLiteral("mechanical") : QStringLiteral("schematic"),
                               [this] { if (libraryDlg_) libraryDlg_->finishCaptureFromSheet(); },
                               [this] { if (libraryDlg_) libraryDlg_->cancelCaptureFromSheet(); });
                });
        connect(libraryDlg_, &PartsLibraryDialog::captureFinished, this, [this] { disarmCapture(); });
        // "Tell this part where its wires leave": the part's mechanical drawing, opened with the
        // output-face tool already in hand. Arming it here rather than making the user find the button is
        // the whole point of asking for it from the library - they have already said what they want to do.
        connect(libraryDlg_, &PartsLibraryDialog::markOutputFacesRequested, this,
                [this](const QString& partId) {
                    openLibraryPartEditor(partId, LibraryPartDrawing::Mechanical);
                    armMechModelOutput();
                });
    }
    libraryDlg_->show();
    libraryDlg_->raise();
    libraryDlg_->activateWindow();
}

PartsLibraryDialog* MainWindow::partsLibraryDialogForTest() {
    showPartsLibrary();
    if (libraryDlg_) libraryDlg_->hide();
    return libraryDlg_;
}

// Arming a capture: the window that asked steps aside, the ribbon says what to do and how to finish, and
// Enter / Esc are given to whoever armed it. One routine for it, because there are two windows that capture
// and the second one could not have been added without either copying this or generalising it.
void MainWindow::armCapture(const QString& what, const QString& which, std::function<void()> take,
                            std::function<void()> drop) {
    captureTake_ = std::move(take);
    captureDrop_ = std::move(drop);
    showCaptureRibbon(true, what);
    statusBar()->showMessage(
        tr("Select what the %1 is, then press Enter (Esc to drop it)").arg(which), 0);
}

void MainWindow::disarmCapture() {
    captureTake_ = nullptr;
    captureDrop_ = nullptr;
    showCaptureRibbon(false);
    statusBar()->clearMessage();
}

void MainWindow::showPartsLibraryAt(const QString& libPartId) {
    showPartsLibrary();
    if (libraryDlg_ && !libPartId.isEmpty()) libraryDlg_->selectPart(libPartId);
}

void MainWindow::showHarnessBuilder() {
    // Modeless and kept alive: the drawing stays usable while a harness is described, and a
    // part-built definition survives Close.
    if (!harnessDlg_) {
        harnessDlg_ = new HarnessBuilderDialog(editor_, this);
        // "See it in the library": the builder does not own the library window, so it asks.
        connect(harnessDlg_, &HarnessBuilderDialog::openLibraryAt, this,
                [this](const QString& id) { showPartsLibraryAt(id); });
    }
    harnessDlg_->show();
    harnessDlg_->raise();
    harnessDlg_->activateWindow();
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

void MainWindow::locateIssue(const ValidationIssue& issue) {
    if (!issue.entityId.isEmpty()) editor_->selectById(issue.entityId);
    canvas_->centerOn(issue.at, 2.5); // jump the view to the object
    editor_->flashHighlight(issue.at);
    canvas_->setFocus(Qt::OtherFocusReason);
}

// A small red "no-entry"-style icon for the bypass table button/window.
static QIcon makeBypassIcon() {
    QPixmap pm(28, 28);
    pm.fill(Qt::transparent);
    QPainter ip(&pm);
    ip.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(QColor("#ffb038"));
    pen.setWidthF(2.4);
    ip.setPen(pen);
    ip.setBrush(Qt::NoBrush);
    ip.drawEllipse(QPointF(14, 14), 9.0, 9.0);
    ip.drawLine(8, 8, 20, 20); // slash across = "bypassed"
    ip.end();
    return QIcon(pm);
}

void MainWindow::showValidation(bool revalidate) {
    const std::vector<ValidationIssue> all = editor_->validateSchematic();
    QSet<QString> presentKeys; // discrepancies still detected in the drawing
    for (const auto& is : all) presentKeys.insert(is.key);
    const auto& bypassed = editor_->bypassedIssues();

    // Tracked set: on a fresh open it's the current (non-bypassed) discrepancies; on
    // Revalidate we KEEP the earlier set and merely add any newly-appeared ones, so a
    // discrepancy the user has since fixed stays listed (and gets a green tick).
    if (!revalidate) {
        validationSnapshot_.clear();
        for (const auto& is : all)
            if (!bypassed.contains(is.key)) validationSnapshot_.push_back(is);
    } else {
        QSet<QString> known;
        for (const auto& s : validationSnapshot_) known.insert(s.key);
        for (const auto& is : all)
            if (!bypassed.contains(is.key) && !known.contains(is.key))
                validationSnapshot_.push_back(is);
    }
    // Display = tracked issues the user hasn't bypassed. Fixed ones are still shown.
    std::vector<ValidationIssue> display;
    for (const auto& is : validationSnapshot_)
        if (!bypassed.contains(is.key)) display.push_back(is);
    int open = 0, fixed = 0;
    for (const auto& is : display) (presentKeys.contains(is.key) ? open : fixed)++;

    // Build the window ONCE; Revalidate / Bypass / Rules just repopulate it in place
    // (no second window pops up).
    if (!validationDlg_) {
        auto* dlg = new QDialog(this);
        validationDlg_ = dlg;
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        connect(dlg, &QObject::destroyed, this, [this] {
            validationDlg_ = nullptr;
            validationTable_ = nullptr;
            validationHeader_ = nullptr;
        });
        dlg->setWindowTitle(QStringLiteral("Schematic validation"));
        dlg->setModal(false);
        dlg->resize(600, 400);
        auto* lay = new QVBoxLayout(dlg);

        validationHeader_ = new QLabel(dlg);
        validationHeader_->setWordWrap(true);
        lay->addWidget(validationHeader_);

        validationTable_ = new QTableWidget(dlg);
        validationTable_->setColumnCount(2);
        validationTable_->setHorizontalHeaderLabels({QStringLiteral("Discrepancy"), QString()});
        validationTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        validationTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
        validationTable_->setColumnWidth(1, 92);
        validationTable_->verticalHeader()->setVisible(false);
        validationTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
        validationTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        // Click a row → jump to + highlight the offending object (reads current rows).
        connect(validationTable_, &QTableWidget::cellClicked, this, [this](int row, int) {
            if (row >= 0 && row < static_cast<int>(validationRows_.size()))
                locateIssue(validationRows_[row]);
        });
        lay->addWidget(validationTable_);

        auto* btnRow = new QHBoxLayout();
        auto* revalBtn = new QPushButton(QStringLiteral("Revalidate"), dlg);
        revalBtn->setToolTip(
            QStringLiteral("Re-run the check; issues you have fixed get a green tick"));
        connect(revalBtn, &QPushButton::clicked, this, [this] { showValidation(true); });
        btnRow->addWidget(revalBtn);
        auto* rulesBtn = new QPushButton(QStringLiteral("Rules…"), dlg);
        rulesBtn->setToolTip(QStringLiteral("Choose which design-rule checks are active"));
        connect(rulesBtn, &QPushButton::clicked, this, [this] { showValidationRules(); });
        btnRow->addWidget(rulesBtn);
        auto* bypassBtn = new QPushButton(makeBypassIcon(), QStringLiteral("Bypass table"), dlg);
        bypassBtn->setToolTip(QStringLiteral("Show discrepancies you have bypassed (accepted)"));
        connect(bypassBtn, &QPushButton::clicked, this, [this] { showBypassTable(); });
        btnRow->addWidget(bypassBtn);
        btnRow->addStretch(1);
        auto* closeBtn = new QPushButton(QStringLiteral("Close"), dlg);
        connect(closeBtn, &QPushButton::clicked, dlg, &QDialog::close);
        btnRow->addWidget(closeBtn);
        lay->addLayout(btnRow);
    }

    // Repopulate the (existing) window's header + table in place.
    validationHeader_->setText(
        display.empty()
            ? QStringLiteral("Schematic is valid — no active discrepancies.")
            : QStringLiteral("%1 open, %2 fixed — click a row to jump to it; Bypass to accept; "
                             "Revalidate to re-check.")
                  .arg(open)
                  .arg(fixed));
    validationRows_ = display;
    QTableWidget* table = validationTable_;
    table->setRowCount(0); // clears old rows + their Bypass buttons
    table->setRowCount(static_cast<int>(display.size()));
    for (int r = 0; r < static_cast<int>(display.size()); ++r) {
        const bool isFixed = !presentKeys.contains(display[r].key);
        auto* item = new QTableWidgetItem(
            isFixed ? QStringLiteral("✔  %1").arg(display[r].message) : display[r].message);
        if (isFixed) {
            item->setForeground(QColor("#3ec46d")); // green tick = resolved
            QFont f = item->font();
            f.setStrikeOut(true);
            item->setFont(f);
        }
        table->setItem(r, 0, item);
        if (!isFixed) { // a still-open issue gets a Bypass button
            auto* by = new QPushButton(QStringLiteral("Bypass"), table);
            const QString key = display[r].key;
            connect(by, &QPushButton::clicked, this, [this, key] {
                editor_->bypassIssue(key);
                showValidation(true); // in-place refresh; no new window
            });
            table->setCellWidget(r, 1, by);
        }
    }

    validationDlg_->show();
    validationDlg_->raise();
}

void MainWindow::showBypassTable() {
    const std::vector<ValidationIssue> all = editor_->validateSchematic();
    // Bypassed AND still present in the drawing (a fixed one simply drops off the list).
    std::vector<ValidationIssue> bypassed;
    for (const auto& is : all)
        if (editor_->bypassedIssues().contains(is.key)) bypassed.push_back(is);

    if (bypassDlg_) { bypassDlg_->close(); bypassDlg_ = nullptr; }
    auto* dlg = new QDialog(this);
    bypassDlg_ = dlg;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &QObject::destroyed, this, [this] { bypassDlg_ = nullptr; });
    dlg->setWindowTitle(QStringLiteral("Bypassed discrepancies"));
    dlg->setModal(false);
    dlg->resize(600, 360);
    auto* lay = new QVBoxLayout(dlg);

    auto* header = new QLabel(
        bypassed.empty()
            ? QStringLiteral("No bypassed discrepancies.")
            : QStringLiteral("%1 bypassed discrepancy(ies) — click a row to jump to it; "
                             "press Restore to re-flag it.")
                  .arg(bypassed.size()),
        dlg);
    header->setWordWrap(true);
    lay->addWidget(header);

    auto* table = new QTableWidget(dlg);
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels({QStringLiteral("Bypassed discrepancy"), QString()});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    table->setColumnWidth(1, 92);
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setRowCount(static_cast<int>(bypassed.size()));
    for (int r = 0; r < static_cast<int>(bypassed.size()); ++r) {
        table->setItem(r, 0, new QTableWidgetItem(bypassed[r].message));
        auto* rest = new QPushButton(QStringLiteral("Restore"), table);
        const QString key = bypassed[r].key;
        connect(rest, &QPushButton::clicked, this, [this, key] {
            editor_->restoreIssue(key);
            showBypassTable();               // drop it from this list
            if (validationDlg_) showValidation(true); // and re-flag it in the active list
        });
        table->setCellWidget(r, 1, rest);
    }
    connect(table, &QTableWidget::cellClicked, this,
            [this, bypassed](int row, int) {
                if (row >= 0 && row < static_cast<int>(bypassed.size()))
                    locateIssue(bypassed[row]);
            });
    lay->addWidget(table);

    auto* box = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    connect(box, &QDialogButtonBox::rejected, dlg, &QDialog::close);
    lay->addWidget(box);

    dlg->show();
    dlg->raise();
}

// The set of design-rule checks (id must match the discrepancy key prefix in
// Editor::validateSchematic) plus a human label for the chooser.
static const std::vector<std::pair<QString, QString>>& validationRuleDefs() {
    static const std::vector<std::pair<QString, QString>> defs = {
        {QStringLiteral("nc"), QStringLiteral("Pin not connected (no wire and no X marker)")},
        {QStringLiteral("openwire"), QStringLiteral("Wire with an unconnected end (an "
                                                   "X-terminated end counts as open)")},
        {QStringLiteral("dup"), QStringLiteral("Duplicate pin labels in a connector")},
        {QStringLiteral("mixedpins"), QStringLiteral("Connector mixes numeric and letter pins")},
        {QStringLiteral("norefdes"), QStringLiteral("Connector has no reference designator")},
        {QStringLiteral("nopn"), QStringLiteral("Connector has no part number (P/N)")},
        {QStringLiteral("shieldpair"),
         QStringLiteral("Wire shielded by a single shield (needs at least 2, or none)")},
        {QStringLiteral("twistpair"),
         QStringLiteral("Wire with a single twist sign (needs at least 2, or none)")},
        {QStringLiteral("shielddrain"),
         QStringLiteral("Shield drain not connected to a wire or terminated")},
    };
    return defs;
}

void MainWindow::showValidationRules() {
    if (rulesDlg_) { rulesDlg_->close(); rulesDlg_ = nullptr; }
    auto* dlg = new QDialog(this);
    rulesDlg_ = dlg;
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    connect(dlg, &QObject::destroyed, this, [this] { rulesDlg_ = nullptr; });
    dlg->setWindowTitle(QStringLiteral("Validation rules"));
    dlg->setModal(false);
    dlg->resize(420, 260);
    auto* lay = new QVBoxLayout(dlg);
    lay->addWidget(new QLabel(QStringLiteral("Tick the checks to run when validating:"), dlg));

    for (const auto& [id, label] : validationRuleDefs()) {
        auto* cb = new QCheckBox(label, dlg);
        cb->setChecked(editor_->ruleEnabled(id));
        const QString rid = id;
        connect(cb, &QCheckBox::toggled, this, [this, rid](bool on) {
            editor_->setRuleEnabled(rid, on);
            // Re-run so the open validation window reflects the new rule set.
            if (validationDlg_) showValidation(false);
        });
        lay->addWidget(cb);
    }
    lay->addStretch(1);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    connect(box, &QDialogButtonBox::rejected, dlg, &QDialog::close);
    lay->addWidget(box);

    dlg->show();
    dlg->raise();
}

// THE PIN TABLE'S ANSWER, BACK INTO THE LIBRARY. Apart from the window that asks for it, because a modal
// dialog cannot be driven by a test and this is the part worth checking: what ends up in the library file.
void MainWindow::writePinTableBackToLibrary(const QString& connectorId) {
    if (connectorId.isEmpty() || !doc_) return;
    ConnectorSpec s;
    QString libId;
    bool found = false;
    for (const auto& en : doc_->entities())
        if (en.id == connectorId && en.type == EntityType::Connector) {
            s = connectorSpecFromMeta(en.meta);
            libId = en.meta.value(QStringLiteral("libPart")).toString();
            found = true;
            break;
        }
    if (!found) return;
// AND BACK INTO THE LIBRARY, when this connector is a copy of a catalogue part that comes with its pins.
//
// The figures went one way only: the library filled the table's empty cells, and a correction made on the
// drawing stayed on that drawing - so the next connector placed from the same part came down with the old
// ones, and the two disagreed with nothing to say which was right. A datasheet figure corrected once
// should be corrected everywhere it is kept.
//
// Written as ROWS, the way the library holds them: the pins that share a cross-section and a rating are
// one type, and which pins those are is the list beside it. A row that already described that pair keeps
// its own name for the size, because "size 1" is somebody's word for it and not ours to replace.
if (libId.isEmpty()) return;
    {
    PartsCatalog catalog;
    catalog.openRemembered();
    // The catalogue hands out a part to READ; the library it lives in hands out one to write to - which
        // is the same object, reached the way anything that writes has to reach it.
        PartsLibrary* owner = catalog.byPath(catalog.pathOf(libId));
        LibraryPart* part = owner ? owner->byId(libId) : nullptr;
        if (part && partIncludesPins(*part)) {
        const std::vector<PartPinType> had = partPinTypes(*part);
        std::vector<PartPinType> now;
        for (int i = 0; i < s.pinCount; ++i) {
            QString gauge = i < s.pinGauge.size() ? s.pinGauge[i].trimmed() : QString();
            const QString amps = i < s.pinAmp.size() ? s.pinAmp[i].trimmed() : QString();
            if (gauge.isEmpty() && amps.isEmpty()) continue;
            // The unit is written on the figure in the table ("20 AWG"); the library keeps the two apart.
            QString unit = crossSectionAwg_ ? QStringLiteral("AWG") : QStringLiteral("mm2");
            if (gauge.endsWith(QStringLiteral("AWG"), Qt::CaseInsensitive)) {
                gauge.chop(3);
                gauge = gauge.trimmed();
                unit = QStringLiteral("AWG");
            } else if (gauge.endsWith(QStringLiteral("mm2"), Qt::CaseInsensitive)) {
                gauge.chop(3);
                gauge = gauge.trimmed();
                unit = QStringLiteral("mm2");
            }
            PartPinType* into = nullptr;
            for (PartPinType& t : now)
                if (t.gauge == gauge && t.unit == unit && t.amps == amps) into = &t;
            if (!into) {
                PartPinType fresh;
                fresh.gauge = gauge;
                fresh.unit = unit;
                fresh.amps = amps;
                for (const PartPinType& was : had)
                    if (was.gauge == gauge && was.unit == unit && was.amps == amps)
                        fresh.size = was.size;   // its old name for this pair
                if (fresh.size.isEmpty())
                    fresh.size = gauge.isEmpty() ? QStringLiteral("size %1").arg(now.size() + 1) : gauge;
                now.push_back(fresh);
                into = &now.back();
            }
            into->pins += (into->pins.isEmpty() ? QString() : QStringLiteral(",")) +
                          QString::number(i + 1);
        }
        if (!now.empty()) {
            setPartPinTypes(*part, now);
            catalog.saveAll();
            if (libraryDlg_) libraryDlg_->reloadTreeForTest();
            statusBar()->showMessage(tr("%1 pin type(s) written back to %2 in the library")
                                         .arg(now.size())
                                         .arg(part->title()),
                                     5000);
        }
    }
}
}

void MainWindow::showPinTable(const QString& connectorId) {
    if (connectorId.isEmpty() || !doc_) return;
    ConnectorSpec s;
    bool found = false;
    QString libId;   // which catalogue part this connector is a copy of, if any
    for (const auto& en : doc_->entities())
        if (en.id == connectorId && en.type == EntityType::Connector) {
            s = connectorSpecFromMeta(en.meta);
            libId = en.meta.value(QStringLiteral("libPart")).toString();
            found = true;
            break;
        }
    if (!found) return;

    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Pin wiring — %1")
                           .arg(s.designator.isEmpty() ? QStringLiteral("connector") : s.designator));
    dlg.resize(480, 440);
    auto* lay = new QVBoxLayout(&dlg);
    lay->addWidget(new QLabel(
        QStringLiteral("Define each pin's wire cross-section and current rating:"), &dlg));
    auto* table = new QTableWidget(&dlg);
    const QString gaugeHdr = crossSectionAwg_ ? QStringLiteral("AWG") : QString::fromUtf8("mm\xC2\xB2");
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels(
        // "Ampere (A)" names a unit twice and the QUANTITY not at all: what goes in the column is what the
        // pin is rated to carry, which is a rate, and the (A) says in what.
        {QStringLiteral("Pin"), QStringLiteral("Label"), gaugeHdr, QStringLiteral("Current rate (A)")});
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->setColumnWidth(2, 120);
    table->verticalHeader()->setVisible(false);
    table->setRowCount(s.pinCount);
    // Fall-menu choices for the gauge column: single values plus common min–max ranges.
    // The combo is editable so any other single value or range can be typed.
    QStringList gaugeItems;
    gaugeItems << QString(); // blank = unset
    if (crossSectionAwg_) {
        for (int g = 4; g <= 32; g += 2) gaugeItems << QStringLiteral("%1 AWG").arg(g);
        gaugeItems << QStringLiteral("4-8 AWG") << QStringLiteral("10-14 AWG")
                   << QStringLiteral("16-20 AWG") << QStringLiteral("20-24 AWG")
                   << QStringLiteral("22-26 AWG") << QStringLiteral("24-28 AWG")
                   << QStringLiteral("28-32 AWG");
    } else {
        for (const char* v : {"0.13", "0.25", "0.34", "0.5", "0.75", "1.0", "1.5", "2.5", "4.0",
                              "6.0", "10.0", "16.0", "25.0"})
            gaugeItems << QString::fromLatin1(v);
        gaugeItems << QStringLiteral("0.25-0.5") << QStringLiteral("0.5-1.0")
                   << QStringLiteral("0.75-1.5") << QStringLiteral("1.0-2.5");
    }
    // WHAT THE LIBRARY ALREADY KNOWS, in the cells that are empty.
    //
    // A connector that comes with its pins has them described in the library - a row per size, with the
    // conductor it takes and what it carries - and a symbol BUILT from that data is labelled with them. A
    // symbol CAPTURED off the sheet is not, and neither is one built before those rows were filled in: the
    // pin table opened blank over a part whose datasheet the library was holding all along, and the figures
    // were typed in again by hand, differently.
    //
    // Only the empty cells. What somebody typed on the drawing is about THIS connector on THIS sheet and
    // outranks the catalogue - a pin fitted with a heavier contact than the housing usually takes is a real
    // thing, and overwriting it would be the program arguing with the person.
    QStringList fromLibGauge, fromLibAmp;
    if (!libId.isEmpty()) {
        PartsCatalog catalog;
        catalog.openRemembered();
        if (const LibraryPart* part = catalog.byId(libId); part && partIncludesPins(*part)) {
            fromLibGauge = partPinGauges(*part);
            fromLibAmp = partPinAmps(*part);
        }
    }
    std::vector<QComboBox*> gaugeCombos(s.pinCount, nullptr);
    for (int i = 0; i < s.pinCount; ++i) {
        auto* pn = new QTableWidgetItem(QString::number(i + 1));
        pn->setFlags(Qt::ItemIsEnabled); // pin index is read-only
        table->setItem(i, 0, pn);
        table->setItem(i, 1, new QTableWidgetItem(i < s.pins.size() ? s.pins[i] : QString()));
        auto* gc = new QComboBox(table);
        gc->setEditable(true); // single value from the list, or a typed min–max range
        gc->setToolTip(QStringLiteral("Pick a gauge, or type a range like 20-24"));
        gc->addItems(gaugeItems);
        QString cur = i < s.pinGauge.size() ? s.pinGauge[i] : QString();
        if (cur.trimmed().isEmpty() && i < fromLibGauge.size()) cur = fromLibGauge[i];
        if (gc->findText(cur) < 0) gc->addItem(cur);
        gc->setCurrentText(cur);
        table->setCellWidget(i, 2, gc);
        gaugeCombos[i] = gc;
        QString amp = i < s.pinAmp.size() ? s.pinAmp[i] : QString();
        if (amp.trimmed().isEmpty() && i < fromLibAmp.size()) amp = fromLibAmp[i];
        table->setItem(i, 3, new QTableWidgetItem(amp));
    }
    lay->addWidget(table);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(bb);
    if (dlg.exec() != QDialog::Accepted) return;

    // Read the table back into the spec (labels + per-pin gauge + ampere) and apply.
    s.pins.clear();
    s.pinGauge.clear();
    s.pinAmp.clear();
    for (int i = 0; i < s.pinCount; ++i) {
        s.pins << (table->item(i, 1) ? table->item(i, 1)->text() : QString());
        s.pinGauge << (gaugeCombos[i] ? gaugeCombos[i]->currentText() : QString());
        s.pinAmp << (table->item(i, 3) ? table->item(i, 3)->text() : QString());
    }
    editor_->applyConnectorSpecToSelection(s);
    refreshProperties();
    if (canvas_) canvas_->update();
    writePinTableBackToLibrary(connectorId);
}

void MainWindow::showBomDialog() {
    // Modeless and kept alive: rows are added from the library and typed in while the drawing
    // stays usable, and the table is placed on the sheet by a click.
    if (!bomDlg_) {
        bomDlg_ = new BomDialog(editor_, this);
        bomDlg_->setConfigOpener(this); // its Configuration button opens Options on the BOM page
        // The panel shows the same bill, so it follows every write the window makes.
        connect(bomDlg_, &BomDialog::bomChanged, this, [this] { refreshBomPanel(); });
    }
    bomDlg_->reload();
    bomDlg_->markOpened();   // what Cancel in that window will put back
    bomDlg_->show();
    bomDlg_->raise();
    bomDlg_->activateWindow();
}

QToolButton* MainWindow::makeHarnessStepsButton(QWidget* parent) {
    auto* btn = new QToolButton(parent);
    btn->setText(QStringLiteral("Harness build\nwizard"));
    btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    btn->setIconSize(QSize(26, 26));
    btn->setToolTip(QStringLiteral(
        "Lay a harness out as bricks - end parts and the runs between them - with the real drawing "
        "previewed as you go"));
    {
        // Three numbered steps with an arrow through them: a sequence, not a form.
        QPixmap pm(28, 28);
        pm.fill(Qt::transparent);
        QPainter ip(&pm);
        ip.setRenderHint(QPainter::Antialiasing, true);
        ip.setPen(QPen(QColor("#8fb7ff"), 1.5));
        for (int i = 0; i < 3; ++i) ip.drawRect(3, 3 + i * 8, 8, 6);
        ip.setPen(QPen(QColor("#7ee0a0"), 1.6));
        ip.drawLine(14, 6, 24, 6);
        ip.drawLine(14, 14, 24, 14);
        ip.drawLine(14, 22, 24, 22);
        ip.setPen(QPen(QColor("#e6b45a"), 1.6));
        ip.drawLine(12, 6, 12, 22);           // the thread the steps hang on
        ip.end();
        btn->setIcon(QIcon(pm));
    }
    // THE REBUILT WIZARD is what this button opens. The old fixed-script one is still reachable from
    // the command palette while the new one is being proven on real harnesses - see MechWizardDialog
    // for why the two are different tools rather than two versions of one.
    connect(btn, &QToolButton::clicked, this, [this] { showMechWizard(); });
    return btn;
}

QToolButton* MainWindow::makeMechanicalBuilderButton(QWidget* parent) {
    auto* btn = new QToolButton(parent);
    btn->setText(QStringLiteral("Mechanical\nbuilder"));
    btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    btn->setIconSize(QSize(26, 26));
    btn->setMinimumWidth(84);
    btn->setAutoRaise(true);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setToolTip(QStringLiteral(
        "The harness as a physical object: a trunk that breaks out to each connector, every run drawn "
        "at the bundle its own conductors make, dimensioned and called out"));
    {
        // A trunk breaking out to three ends, drawn as the bundle it is: a body, not a line.
        QPixmap pm(28, 28);
        pm.fill(Qt::transparent);
        QPainter ip(&pm);
        ip.setRenderHint(QPainter::Antialiasing, true);
        ip.setPen(QPen(QColor("#8fb7ff"), 1.5));
        ip.drawLine(3, 12, 15, 12);           // the trunk, as two edges
        ip.drawLine(3, 17, 15, 17);
        ip.setPen(QPen(QColor("#7ee0a0"), 1.4));
        ip.drawLine(15, 13, 25, 6);           // and the branches off it
        ip.drawLine(15, 16, 25, 9);
        ip.drawLine(15, 14, 25, 22);
        ip.drawLine(15, 17, 25, 25);
        ip.setPen(QPen(QColor("#e6b45a"), 1.4));
        ip.drawRect(1, 11, 3, 7);             // the connector at the trunk end
        ip.end();
        btn->setIcon(QIcon(pm));
    }
    connect(btn, &QToolButton::clicked, this, [this] { showMechanicalBuilder(); });
    return btn;
}

QToolButton* MainWindow::makeMechanicalValidationButton(QWidget* parent) {
    auto* btn = new QToolButton(parent);
    btn->setText(QStringLiteral("Mechanical\nchecks"));
    btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    btn->setIconSize(QSize(26, 26));
    btn->setMinimumWidth(84);
    btn->setAutoRaise(true);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setToolTip(QStringLiteral(
        "Whether what is fitted will physically go on: every boot, backshell, sleeve and gland "
        "against the bundle its own conductors make"));
    {
        // A bundle going into something, with a tick beside it.
        QPixmap pm(28, 28);
        pm.fill(Qt::transparent);
        QPainter ip(&pm);
        ip.setRenderHint(QPainter::Antialiasing, true);
        ip.setPen(QPen(QColor("#8fb7ff"), 1.5));
        ip.drawLine(2, 11, 14, 11);
        ip.drawLine(2, 16, 14, 16);
        ip.setPen(QPen(QColor("#e6b45a"), 1.6));
        ip.drawRect(14, 8, 8, 12);            // the part it goes into
        ip.setPen(QPen(QColor("#7ee0a0"), 2.0));
        ip.drawLine(18, 24, 21, 27);          // and the tick
        ip.drawLine(21, 27, 27, 19);
        ip.end();
        btn->setIcon(QIcon(pm));
    }
    connect(btn, &QToolButton::clicked, this, [this] { showMechanicalValidation(); });
    return btn;
}

QToolButton* MainWindow::makeBundleDiameterButton(QWidget* parent) {
    auto* btn = new QToolButton(parent);
    btn->setText(QStringLiteral("Bundle\ndiameter"));
    btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    btn->setIconSize(QSize(26, 26));
    btn->setMinimumWidth(84);
    btn->setAutoRaise(true);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setToolTip(QStringLiteral(
        "D = √(Σ D²) × k_packing, shown as a calculation: the conductors, the working line by line, "
        "and what the figure does and does not allow for"));
    {
        // A ring of conductors inside the circle they make.
        QPixmap pm(28, 28);
        pm.fill(Qt::transparent);
        QPainter ip(&pm);
        ip.setRenderHint(QPainter::Antialiasing, true);
        ip.setPen(QPen(QColor("#8fb7ff"), 1.5));
        ip.drawEllipse(QPointF(14, 14), 11.5, 11.5);
        ip.setPen(QPen(QColor("#7ee0a0"), 1.2));
        const double r = 5.2;
        for (int i = 0; i < 6; ++i) {
            const double a = i * 3.14159265 / 3.0;
            ip.drawEllipse(QPointF(14 + r * std::cos(a), 14 + r * std::sin(a)), 3.0, 3.0);
        }
        ip.end();
        btn->setIcon(QIcon(pm));
    }
    connect(btn, &QToolButton::clicked, this, [this] { showBundleDiameter(); });
    return btn;
}

QToolButton* MainWindow::makeThermalButton(QWidget* parent) {
    auto* btn = new QToolButton(parent);
    btn->setText(QStringLiteral("Thermal\nmodel"));
    btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    btn->setIconSize(QSize(26, 26));
    btn->setMinimumWidth(84);
    btn->setAutoRaise(true);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setToolTip(QStringLiteral(
        "How hot the bundle gets: the heat the current makes, the resistances it has to escape "
        "through, and the loop between them settling - with what it could carry instead"));
    {
        // A bundle with heat coming off it, and a thermometer beside it.
        QPixmap pm(28, 28);
        pm.fill(Qt::transparent);
        QPainter ip(&pm);
        ip.setRenderHint(QPainter::Antialiasing, true);
        ip.setPen(QPen(QColor("#8fb7ff"), 1.5));
        ip.drawEllipse(QPointF(10, 16), 7.0, 7.0);        // the bundle end-on
        ip.setPen(QPen(QColor("#e06c6c"), 1.4));
        for (int i = 0; i < 3; ++i) {                     // heat coming off it
            const int x = 4 + i * 5;
            ip.drawLine(x, 6, x + 2, 3);
            ip.drawLine(x + 2, 3, x, 1);
        }
        ip.setPen(QPen(QColor("#e6b45a"), 1.6));
        ip.drawLine(23, 6, 23, 20);                       // the thermometer
        ip.setBrush(QColor("#e06c6c"));
        ip.drawEllipse(QPointF(23, 22), 3.0, 3.0);
        ip.end();
        btn->setIcon(QIcon(pm));
    }
    connect(btn, &QToolButton::clicked, this, [this] { showThermalModel(); });
    return btn;
}

void MainWindow::showMechanicalValidation() {
    // Modeless and kept alive: a gauge is changed on the drawing, Check again is pressed, and the
    // answers follow - which is the whole point of checking against the bundle rather than a figure.
    if (!mechCheckDlg_) mechCheckDlg_ = new MechanicalValidationDialog(editor_, this);
    mechCheckDlg_->readFromDrawing();
    mechCheckDlg_->show();
    mechCheckDlg_->raise();
    mechCheckDlg_->activateWindow();
}

QToolButton* MainWindow::makeBlockButton(QWidget* parent, BlockAction action) {
    struct Look { const char* text; const char* tip; };
    const Look look =
        action == BlockAction::Create
            ? Look{"Create\nblock", "Make a block out of what is selected, and save it in the block "
                                   "folder"}
        : action == BlockAction::Insert
            ? Look{"Insert\nblock", "Drop a copy of a block on the sheet"}
        : action == BlockAction::Manage
            ? Look{"Blocks", "Every block there is: the ones saved as files, and every drawing in "
                             "the parts library"}
        : action == BlockAction::Edit
            ? Look{"Edit\nblock", "Open the selected block on a tab of its own, to change its lines"}
        : action == BlockAction::SaveEdit
            ? Look{"Save\nblock", "Only while a block is open for editing"}
            : Look{"Cancel\nblock", "Leave the block as it was and close it"};
    auto* btn = new QToolButton(parent);
    btn->setText(QString::fromLatin1(look.text));
    btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    btn->setIconSize(QSize(26, 26));
    btn->setMinimumWidth(76);
    btn->setAutoRaise(true);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setToolTip(QString::fromLatin1(look.tip));
    {
        QPixmap pm(28, 28);
        pm.fill(Qt::transparent);
        QPainter ip(&pm);
        ip.setRenderHint(QPainter::Antialiasing, true);
        // A block: a piece of drawing with a base point on it. Create shows it being made, Insert an
        // arrow bringing it in, Manage a shelf of them, Save a tick.
        ip.setPen(QPen(QColor("#8fb7ff"), 1.5));
        if (action == BlockAction::Manage) {
            for (int i = 0; i < 3; ++i) ip.drawRect(3, 3 + i * 8, 22, 6);
        } else {
            ip.drawRect(5, 8, 14, 12);
            ip.setPen(QPen(QColor("#7ee0a0"), 1.4));
            ip.drawLine(8, 14, 16, 14);
            ip.drawLine(12, 11, 12, 17);
            ip.setBrush(QColor("#e6b45a"));
            ip.setPen(QPen(QColor("#e6b45a"), 1.0));
            ip.drawEllipse(QPointF(5, 20), 2.0, 2.0);   // the base point
            if (action == BlockAction::Edit) {
                // A pencil across the corner.
                ip.setPen(QPen(QColor("#e6b45a"), 2.2));
                ip.drawLine(17, 25, 26, 16);
                ip.setPen(QPen(QColor("#e8e8e8"), 1.2));
                ip.drawLine(15, 27, 17, 25);
            } else if (action == BlockAction::Insert) {
                ip.setPen(QPen(QColor("#8fb7ff"), 1.6));
                ip.drawLine(21, 6, 26, 11);
                ip.drawLine(26, 11, 21, 11);
                ip.drawLine(26, 11, 26, 6);
            } else if (action == BlockAction::SaveEdit) {
                ip.setPen(QPen(QColor("#7ee0a0"), 2.0));
                ip.drawLine(18, 23, 21, 26);
                ip.drawLine(21, 26, 27, 18);
            } else if (action == BlockAction::CancelEdit) {
                ip.setPen(QPen(QColor("#e06c6c"), 2.0));
                ip.drawLine(19, 19, 27, 27);
                ip.drawLine(27, 19, 19, 27);
            }
        }
        ip.end();
        btn->setIcon(QIcon(pm));
    }
    switch (action) {
        case BlockAction::Create:
            connect(btn, &QToolButton::clicked, this, [this] { createBlockFromSelection(); });
            break;
        case BlockAction::Insert:
        case BlockAction::Manage:
            connect(btn, &QToolButton::clicked, this, [this] { showBlockDialog(); });
            break;
        case BlockAction::Edit:
            connect(btn, &QToolButton::clicked, this, [this] { editSelectedBlock(); });
            break;
        case BlockAction::SaveEdit:
            btn->setEnabled(false);
            connect(btn, &QToolButton::clicked, this, [this] { saveBlockBeingEdited(); });
            break;
        case BlockAction::CancelEdit:
            btn->setEnabled(false);
            connect(btn, &QToolButton::clicked, this, [this] { cancelBlockEdit(); });
            break;
    }
    return btn;
}

void MainWindow::editSelectedBlock() {
    // The block that is picked on the sheet - a reference to one, or a piece of a placed copy - opened
    // on its own tab, the same as double-clicking it.
    if (!editor_ || !doc_) return;
    QString name;
    const auto& sel = editor_->selection();
    const QSet<QString> picked(sel.begin(), sel.end());
    for (const Entity& e : doc_->entities()) {
        if (!picked.contains(e.id)) continue;
        name = e.type == EntityType::BlockRef ? e.meta.value(QLatin1String(kBlockRefKey)).toString()
                                              : entityBlockName(e);
        if (!name.isEmpty()) break;
    }
    if (name.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("Select a block on the sheet first, then Edit block opens it"),
                                 5000);
        return;
    }
    openBlockEditor(name);
}

void MainWindow::showBlockDialog() {
    if (!blockDlg_) {
        blockDlg_ = new BlockDialog(editor_, this);
        connect(blockDlg_, &BlockDialog::editBlockRequested, this,
                [this](const QString& name) { openBlockEditor(name); });
    }
    blockDlg_->refresh();
    blockDlg_->show();
    blockDlg_->raise();
    blockDlg_->activateWindow();
}

void MainWindow::createBlockFromSelection() {
    // Straight to the point when something is selected; otherwise the window explains what is needed.
    if (editor_ && editor_->copyOfSelection().empty()) {
        statusBar()->showMessage(
            QStringLiteral("Select what the block is made of first, then Create block"), 4000);
        showBlockDialog();
        return;
    }
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, QStringLiteral("Create block"), QStringLiteral("Name:"),
                              QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    showBlockDialog();
    if (blockDlg_ && blockDlg_->createFromSelection(name)) {
        statusBar()->showMessage(QStringLiteral("Block %1 saved").arg(name), 4000);
    }
}

void MainWindow::openBlockEditor(const QString& name) {
    // Already open? Go to it rather than opening it twice.
    for (int i = 0; i < static_cast<int>(docs_.size()); ++i)
        if (blockEdits_.value(docs_[i].get()) == name) {
            docTabs_->setCurrentIndex(i);
            activateIndex(i);
            return;
        }
    // Find it: a file block, or one of the library's drawings.
    // Its own file first: reading every block there is to find one was every imported sheet in the
    // folder parsed on each double-click - seconds of nothing, several times over.
    BlockDef found;
    if (!found.load(BlockDef::pathFor(name)) || found.name != name) {
        found = BlockDef();
        PartsCatalog catalog;
        catalog.openRemembered();
        for (const BlockDef& b : allBlocks(&catalog))
            if (b.name == name) found = b;
    }
    if (found.name.isEmpty()) {
        statusBar()->showMessage(QStringLiteral("No block called %1").arg(name), 4000);
        return;
    }
    if (found.fromLibrary()) {
        // The drawing belongs to the part, so editing it here would be lost the next time the library was
        // read - and it used to OPEN THE LIBRARY at that point, which is a whole window arriving over the
        // drawing because of one stray double-click. A double-click on a placed part is nearly always a
        // mis-click while selecting or moving it, and the answer to a mis-click is nothing at all.
        //
        // Opening it in the library is still a thing to want, and it is on the part's own right-click menu -
        // asked for, rather than sprung. Said here so the way to get there is known and the double-click
        // does not merely look broken.
        statusBar()->showMessage(
            QStringLiteral("%1 is a parts-library drawing — right-click it to open it in the parts library")
                .arg(name),
            6000);
        return;
    }

    // The Blocks window gets out of the way for the duration: the block is edited on the sheet, and
    // the window comes back when the edit ends with Save block or Cancel block.
    if (blockDlg_ && blockDlg_->isVisible()) {
        blockDlgWasOpen_ = true;
        blockDlg_->hide();
    }
    auto doc = std::make_unique<Document>(QStringLiteral("Block: %1").arg(name));
    doc->entities().clear();
    // Each line its own id: the library's drawings come without them, and eighty thousand lines all
    // called the same thing were one object to the selection - a click on any picked them all.
    int next = 0;
    for (Entity& e : partDrawingEntities(found.entities)) {
        e.id = QStringLiteral("blk%1").arg(++next);
        doc->entities().push_back(std::move(e));
    }
    Document* raw = doc.get();
    blockEdits_.insert(raw, name);
    addDocument(std::move(doc), QString());
    for (int i = 0; i < static_cast<int>(docs_.size()); ++i)
        if (docs_[i].get() == raw) {
            docTabs_->setCurrentIndex(i);
            activateIndex(i);
            break;
        }
    // Looking at what is in it: a block is drawn about its own origin, and an imported sheet sits up and
    // to the right of that - off the screen of a fresh tab, which then looks empty.
    if (canvas_) canvas_->fitView();
    statusBar()->showMessage(
        QStringLiteral("Editing the block %1 — Save block puts it back and rebuilds every copy")
            .arg(name),
        6000);
}

void MainWindow::saveBlockBeingEdited() {
    const QString name = blockEdits_.value(doc_);
    if (name.isEmpty()) return;
    // Whatever is on this tab IS the block now. The base point stays where it was - the entities are
    // already in the block's own coordinates, because that is how they were opened.
    BlockDef block;
    block.name = name;
    for (const Entity& e : doc_->entities()) {
        Entity copy = e;
        copy.meta.remove(QStringLiteral("block"));
        copy.meta.remove(QStringLiteral("blockInst"));
        copy.meta.remove(QStringLiteral("blockOriginX"));
        copy.meta.remove(QStringLiteral("blockOriginY"));
        QJsonObject o = copy.toJson();
        o.remove(QStringLiteral("id"));
        block.entities.append(o);
    }
    if (!block.save(BlockDef::pathFor(name))) {
        QMessageBox::warning(this, QStringLiteral("Blocks"),
                             QStringLiteral("Could not save the block %1").arg(name));
        return;
    }
    // And every copy of it, on every drawing that is open, is rebuilt from what was just saved.
    // A REFERENCE draws out of the file, so it has only to forget what it read and take the new box.
    forgetBlockRefContent(name);
    const Bounds local = blockLocalBounds(block.entities);
    int rebuilt = 0, sheets = 0;
    for (auto& d : docs_) {
        if (blockEdits_.contains(d.get())) continue;   // not the block's own tab
        int n = updateBlockInstances(*d, block);
        for (Entity& e : d->entities())
            if (e.type == EntityType::BlockRef && e.meta.value(QLatin1String(kBlockRefKey)).toString() == name) {
                refitBlockRef(e, local);
                ++n;
            }
        if (n > 0) d->setDirty(true);
        if (n > 0) {
            ++sheets;
            rebuilt += n;
        }
    }
    doc_->setDirty(false);
    updateActiveTabText();
    if (canvas_) canvas_->update();
    refreshPanels();
    if (blockDlg_) blockDlg_->refresh();
    statusBar()->showMessage(
        rebuilt > 0 ? QStringLiteral("Block %1 saved — %2 cop%3 rebuilt on %4 sheet(s)")
                          .arg(name)
                          .arg(rebuilt)
                          .arg(rebuilt == 1 ? QStringLiteral("y") : QStringLiteral("ies"))
                          .arg(sheets)
                    : QStringLiteral("Block %1 saved").arg(name),
        6000);
    endBlockEdit();
}

void MainWindow::cancelBlockEdit() {
    if (blockEdits_.value(doc_).isEmpty()) return;
    // Thrown away deliberately, so closing the tab does not ask whether to save it as a drawing.
    doc_->setDirty(false);
    statusBar()->showMessage(QStringLiteral("Block left as it was"), 4000);
    endBlockEdit();
}

void MainWindow::endBlockEdit() {
    // Close the block's own tab, whichever way the edit ended, and give the Blocks window back.
    for (int i = 0; i < static_cast<int>(docs_.size()); ++i) {
        if (!blockEdits_.contains(docs_[i].get()) || docs_[i].get() != doc_) continue;
        docs_[i]->setDirty(false);   // its content has been dealt with either way
        closeDocument(i);
        break;
    }
    if (blockDlgWasOpen_ && blockDlg_) {
        blockDlg_->refresh();
        blockDlg_->show();
        blockDlg_->raise();
    }
    blockDlgWasOpen_ = false;
}

QToolButton* MainWindow::makeTemplateSessionButton(QWidget* parent, int which) {
    // 0 = Save template, 1 = Save as, 2 = Cancel.
    auto* btn = new QToolButton(parent);
    btn->setText(which == 0 ? QStringLiteral("Save\ntemplate")
                 : which == 1 ? QStringLiteral("Save\nas…")
                              : QStringLiteral("Cancel\ntemplate"));
    btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    btn->setIconSize(QSize(26, 26));
    btn->setMinimumWidth(76);
    btn->setAutoRaise(true);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setToolTip(which == 2 ? QStringLiteral("Leave the template as it was and close it")
                              : QStringLiteral("Only while a template is open for drawing"));
    {
        // A sheet with a border, and what is being done to it.
        QPixmap pm(28, 28);
        pm.fill(Qt::transparent);
        QPainter ip(&pm);
        ip.setRenderHint(QPainter::Antialiasing, true);
        ip.setPen(QPen(QColor("#8fb7ff"), 1.5));
        ip.drawRect(4, 3, 16, 20);
        ip.setPen(QPen(QColor("#7b828c"), 1.0));
        ip.drawRect(7, 6, 10, 14);
        ip.setBrush(QColor("#8fb7ff"));
        ip.setPen(Qt::NoPen);
        ip.drawRect(12, 17, 5, 3);          // the title block
        if (which == 2) {
            ip.setPen(QPen(QColor("#e06c6c"), 2.0));
            ip.drawLine(19, 19, 26, 26);
            ip.drawLine(26, 19, 19, 26);
        } else {
            ip.setPen(QPen(QColor("#7ee0a0"), 2.0));
            ip.drawLine(18, 22, 21, 25);
            ip.drawLine(21, 25, 27, 17);
            if (which == 1) {
                ip.setPen(QPen(QColor("#e6b45a"), 1.4));
                ip.drawLine(22, 3, 26, 3);   // the extra stroke that says "as..."
                ip.drawLine(24, 1, 24, 5);
            }
        }
        ip.end();
        btn->setIcon(QIcon(pm));
    }
    btn->setEnabled(false);
    tplSessionBtns_.push_back(btn);   // so every set of them follows the session
    if (which == 2)
        connect(btn, &QToolButton::clicked, this, [this] { cancelTemplateEdit(); });
    else
        connect(btn, &QToolButton::clicked, this,
                [this, which] { saveTemplateBeingEdited(which == 1); });
    return btn;
}

void MainWindow::openTemplateEditor(const QString& name) {
    // Already open? Go to it.
    for (int i = 0; i < static_cast<int>(docs_.size()); ++i)
        if (templateEdits_.value(docs_[i].get()) == name && !name.isEmpty()) {
            docTabs_->setCurrentIndex(i);
            activateIndex(i);
            return;
        }
    // Find it: whichever template of that name is in the template folder, else whatever the window
    // currently holds (a template being made that has not been saved yet).
    DrawingTemplate tpl;
    QString path;
    for (const QString& p : DrawingTemplate::installed()) {
        DrawingTemplate t;
        if (t.load(p) && t.name == name) {
            tpl = t;
            path = p;
            break;
        }
    }
    if (path.isEmpty()) {
        if (!templateDlg_) templateDlg_ = new TemplateDialog(editor_, this);
        tpl = templateDlg_->templateData();
        if (!name.isEmpty()) tpl.name = name;
    }
    // A template with nothing in it yet gets a generated frame to start from. One loaded from a file
    // is taken exactly as it is - including whatever was drawn on it by hand.
    if (tpl.content.isEmpty()) tpl.content = defaultSheetContent(tpl);

    if (templateDlg_ && templateDlg_->isVisible()) {
        templateDlgWasOpen_ = true;
        templateDlg_->hide();
    }
    auto doc = std::make_unique<Document>(
        QStringLiteral("Template: %1").arg(tpl.name.isEmpty() ? QStringLiteral("new") : tpl.name));
    doc->entities().clear();
    // Every object of the sheet is a full object here: its own id, its own grips, free to be moved,
    // stretched, trimmed or deleted. That is what designing a template means - the frame is a starting
    // point, not a locked group as it is once placed on a drawing.
    std::vector<Entity> ents = partDrawingEntities(tpl.content);
    Document* raw = doc.get();
    templateEdits_.insert(raw, tpl.name);
    templateEditPaths_.insert(raw, path);
    tplBeingDrawn_ = tpl;
    // What is generated says so itself (defaultSheetContent marks it), so nothing has to be assumed
    // here: a hand-drawn line is never mistaken for the generator's and regenerated away.
    adoptGeneratedFrame(ents, false);
    for (const Entity& e : ents) raw->entities().push_back(e);
    addDocument(std::move(doc), QString());
    for (int i = 0; i < static_cast<int>(docs_.size()); ++i)
        if (docs_[i].get() == raw) {
            docTabs_->setCurrentIndex(i);
            activateIndex(i);
            break;
        }
    statusBar()->showMessage(
        QStringLiteral("Drawing the template %1 — draw the sheet, then Save template")
            .arg(tpl.name),
        6000);
}

void MainWindow::saveTemplateBeingEdited(bool saveAs) {
    if (!templateEdits_.contains(doc_)) return;
    const QString name = templateEdits_.value(doc_);
    QString path = templateEditPaths_.value(doc_);

    // Start from what the template already says (its sheet, zones, signatures) and take the SHEET as
    // its new content: the drawing on this tab is the template now.
    // The template as the ribbon has it: its sheet, its frame, its revision table. Those are what the
    // tab has been changing, so they are what gets written.
    DrawingTemplate tpl = tplBeingDrawn_;
    if (tpl.name.isEmpty() && !name.isEmpty()) tpl.name = name;
    if (!name.isEmpty()) tpl.name = name;
    const DrawingTemplate taken = templateFromDocument(*doc_, tpl.name);
    tpl.content = taken.content;
    tpl.layers = taken.layers;
    tpl.activeLayer = taken.activeLayer;

    if (saveAs || path.isEmpty()) {
        QString suggested = path.isEmpty() ? DrawingTemplate::pathFor(tpl.name) : path;
        QString chosen = QFileDialog::getSaveFileName(this, QStringLiteral("Save the template"),
                                                      suggested, DrawingTemplate::fileFilter());
        if (chosen.isEmpty()) return;
        const QString suffix = QStringLiteral(".") + DrawingTemplate::extension();
        if (!chosen.endsWith(suffix, Qt::CaseInsensitive)) chosen += suffix;
        path = chosen;
        tpl.name = QFileInfo(path).completeBaseName();
    }
    if (!tpl.save(path)) {
        QMessageBox::warning(this, QStringLiteral("Drawing template"),
                             QStringLiteral("Could not write %1").arg(path));
        return;
    }
    if (templateDlg_) {
        templateDlg_->setTemplateData(tpl);
        templateDlg_->refreshInstalled();
    }
    statusBar()->showMessage(QStringLiteral("Template saved to %1").arg(path), 6000);
    doc_->setDirty(false);
    endTemplateEdit();
}

void MainWindow::cancelTemplateEdit() {
    if (!templateEdits_.contains(doc_)) return;
    doc_->setDirty(false);   // thrown away deliberately
    statusBar()->showMessage(QStringLiteral("Template left as it was"), 4000);
    endTemplateEdit();
}

void MainWindow::endTemplateEdit() {
    for (int i = 0; i < static_cast<int>(docs_.size()); ++i) {
        if (!templateEdits_.contains(docs_[i].get()) || docs_[i].get() != doc_) continue;
        docs_[i]->setDirty(false);
        closeDocument(i);
        break;
    }
    if (templateDlgWasOpen_ && templateDlg_) {
        templateDlg_->refreshInstalled();
        templateDlg_->show();
        templateDlg_->raise();
    }
    templateDlgWasOpen_ = false;
}

void MainWindow::showTemplateDialog() {
    // Modeless and kept alive: a border is drawn on the sheet, captured here, saved, and applied -
    // without the window being reopened between each step.
    if (!templateDlg_) {
        templateDlg_ = new TemplateDialog(editor_, this);
        connect(templateDlg_, &TemplateDialog::drawOnSheetRequested, this,
                [this](const QString& name) { openTemplateEditor(name); });
    }
    templateDlg_->refreshInstalled();
    templateDlg_->show();
    templateDlg_->raise();
    templateDlg_->activateWindow();
}

void MainWindow::showBundleDiameter() {
    if (!bundleDlg_) bundleDlg_ = new BundleDiameterDialog(editor_, this);
    bundleDlg_->show();
    bundleDlg_->raise();
    bundleDlg_->activateWindow();
}

void MainWindow::showThermalModel() {
    if (!thermalDlg_) thermalDlg_ = new ThermalModelDialog(editor_, this);
    thermalDlg_->show();
    thermalDlg_->raise();
    thermalDlg_->activateWindow();
}

void MainWindow::showCalculator() {
    if (!calculatorDock_) return;
    calculatorDock_->setVisible(true);
    calculatorDock_->raise();
    if (auto* calc = calculatorDock_->findChild<ScientificCalculatorDialog*>())
        calc->focusExpression();
}

void MainWindow::showMechanicalBuilder() {
    // Modeless and kept alive: the schematic is edited, read in again, and the mechanical drawing
    // regenerated, without the definition being retyped each time.
    if (!mechDlg_) mechDlg_ = new MechanicalBuilderDialog(editor_, this);
    mechDlg_->show();
    mechDlg_->raise();
    mechDlg_->activateWindow();
}

void MainWindow::showMechModels() {
    if (!modelsDlg_) {
        modelsDlg_ = new MechModelsDialog(
            editor_, this, [this](MechModelKind kind, QString name, bool schematic) {
                openMechModelEditor(kind, name, schematic);
            });
        // The same stepping-aside as the parts library: the window goes, the ribbon says what to do, and
        // Enter takes the selection.
        connect(modelsDlg_, &MechModelsDialog::captureArmed, this, [this](const QString& name) {
            armCapture(tr("%1 — the model's drawing").arg(name), tr("model's drawing"),
                       [this] { if (modelsDlg_) modelsDlg_->finishCaptureFromSheet(); },
                       [this] { if (modelsDlg_) modelsDlg_->cancelCaptureFromSheet(); });
        });
        connect(modelsDlg_, &MechModelsDialog::captureFinished, this, [this] { disarmCapture(); });
    }
    modelsDlg_->show();
    modelsDlg_->raise();
    modelsDlg_->activateWindow();
}

void MainWindow::openMechModelEditor(MechModelKind kind) {
    // Which model of the kind "the model" means, resolved to a name here so everything below works in
    // names only - a kind holds as many models as an office has drawn.
    MechModelSet set;
    set.load();
    const MechModel* def = set.of(kind);
    openMechModelEditor(kind, def ? def->name : QString());
}

void MainWindow::openMechModelEditor(MechModelKind kind, const QString& name, bool schematic) {
    // Already open? The same MODEL AND the same drawing of it, not merely the same kind - the
    // mechanical and schematic sessions for one model are two different tabs, each with its own
    // drawing to edit, so matching on the model alone would hand back the wrong one.
    for (int i = 0; i < static_cast<int>(docs_.size()); ++i) {
        Document* d = docs_[i].get();
        if (!mechModelEdits_.contains(d) || mechModelEdits_.value(d) != kind) continue;
        if (mechModelSessionModels_.value(d).name != name) continue;
        if (mechModelEditSchematic_.value(d, false) != schematic) continue;
        docTabs_->setCurrentIndex(i);
        activateIndex(i);
        return;
    }

    MechModelSet set;
    set.load();
    MechModel model;
    model.kind = kind;
    model.name = name;   // a model that has nothing drawn yet is still captured under its own name
    if (const MechModel* existing = set.of(kind, name)) model = *existing;

    // WHERE IT CONNECTS AND WHICH WAY IT RUNS, marked before anybody asks. A model is placed BY those marks -
    // the drawing is turned so the marked direction runs along the run and set down so the marked point lands on
    // it - so a model without them cannot be placed at all, and an office that does not know that draws a part,
    // captures it, and finds the wizard puts it down by the corner of its own extent.
    //
    // So they start marked, at the reading almost every part wants: the connection at the left-hand end of the
    // drawing running along +x, and the two ends at the two ends. Marked means VISIBLE, which is the point - they
    // are there on the canvas to be dragged to where they really belong, rather than absent until somebody knows
    // to go and look for the buttons.
    //
    // None of this applies to a SCHEMATIC symbol: it is not placed by running a direction along a run, it
    // has no two ends to keep, and it is checked against model.drawing, which a schematic session never
    // touches.
    if (!schematic && !model.isEmpty()) {
        Bounds ext;
        for (const Entity& e : partDrawingEntities(model.drawing)) ext.expand(e.bounds());
        if (ext.valid) {
            const double midY = (ext.min.y + ext.max.y) / 2.0;
            if (!model.hasConnect) {
                model.hasConnect = true;
                model.connectPoint = {ext.min.x, midY};
                model.connectAngleRad = 0.0;
            }
            if (!model.hasNearPoint) {
                model.hasNearPoint = true;
                model.nearPoint = {ext.min.x, midY};
            }
            if (!model.hasFarPoint) {
                model.hasFarPoint = true;
                model.farPoint = {ext.max.x, midY};
            }
        }
    }

    const QString suffix = schematic ? QStringLiteral(" — schematic") : QString();
    auto doc = std::make_unique<Document>(
        (name.isEmpty() ? QStringLiteral("Model - %1").arg(mechModelName(kind))
                        : QStringLiteral("Model - %1 (%2)").arg(mechModelName(kind), name)) +
        suffix);
    loadMechModelDocument(*doc, model, schematic);
    doc->setDirty(false);
    Document* raw = doc.get();
    mechModelEdits_.insert(raw, kind);
    mechModelSessionModels_.insert(raw, model);
    mechModelEditSchematic_.insert(raw, schematic);
    addDocument(std::move(doc), QString());
    // Where it was left, if it has been open before; otherwise fitted to the part - which is what it now fits,
    // the part having no sheet round it to fit instead.
    const QString viewKey = mechModelViewKey(kind, name, schematic);
    if (mechModelViews_.contains(viewKey)) {
        raw->view() = mechModelViews_.value(viewKey);
    } else {
        // FITTED TO THE DRAWING, and fitted AGAIN once the window has settled. Fit measures against the canvas's
        // size, and at this moment the canvas has whatever size it had before this document arrived - the panels
        // and the tab bar have not been laid out for it yet - so fitting only here fits to a size that is about to
        // change, and the part comes up at the wrong zoom. The second one lands after the layout, on the size the
        // canvas really has.
        canvas_->fitView();
        QTimer::singleShot(0, this, [this, raw] {
            if (doc_ != raw || !canvas_) return;   // moved on since: leave whatever is showing alone
            canvas_->fitView();
            canvas_->invalidateScene();
            canvas_->update();
        });
    }
    canvas_->invalidateScene();
    if (modelsDlg_) modelsDlg_->hide();
    statusBar()->showMessage(
        QStringLiteral("Editing %1 in the main canvas — use Place and Modify, then Capture to default model")
            .arg(mechModelName(kind)),
        6000);
}

void MainWindow::openLibraryPartEditor(const QString& partId, LibraryPartDrawing kind) {
    // Reached into the library dialog's OWN catalog, not a fresh independent one - that dialog stays
    // open (hidden, not closed) for the life of the session, the same as the models manager does for
    // a default-model session, so its in-memory copy is never allowed to go stale against a session
    // writing straight into the same catalog behind its back.
    if (!libraryDlg_) return;
    LibraryPart* stored = libraryDlg_->partForSession(partId);
    if (!stored) return;

    // Already open? The same part AND the same drawing of it.
    for (int i = 0; i < static_cast<int>(docs_.size()); ++i) {
        Document* d = docs_[i].get();
        if (libraryPartEdits_.value(d) != partId) continue;
        if (libraryPartEditKind_.value(d) != kind) continue;
        docTabs_->setCurrentIndex(i);
        activateIndex(i);
        return;
    }

    LibraryPart part = *stored;
    const QString drawingKindName = kind == LibraryPartDrawing::Mechanical  ? QStringLiteral("mechanical")
                                    : kind == LibraryPartDrawing::Schematic ? QStringLiteral("schematic")
                                    : kind == LibraryPartDrawing::Fanout    ? QStringLiteral("fanout front")
                                                                            : QStringLiteral("fanout back");
    // WHERE IT CONNECTS AND WHICH WAY IT RUNS, marked before anybody asks - the same reasoning
    // openMechModelEditor uses for a default model. Only the mechanical drawing has anywhere to mark:
    // a schematic symbol and a fanout are not placed by running a direction along a run.
    if (kind == LibraryPartDrawing::Mechanical && !part.mechanical.isEmpty()) {
        Bounds ext;
        for (const Entity& e : partDrawingEntities(part.mechanical)) ext.expand(e.bounds());
        if (ext.valid) {
            const double midY = (ext.min.y + ext.max.y) / 2.0;
            if (!part.hasConnect) {
                part.hasConnect = true;
                part.connectPoint = {ext.min.x, midY};
                part.connectAngleRad = 0.0;
            }
            if (!part.hasNearPoint) {
                part.hasNearPoint = true;
                part.nearPoint = {ext.min.x, midY};
            }
            if (!part.hasFarPoint) {
                part.hasFarPoint = true;
                part.farPoint = {ext.max.x, midY};
            }
        }
    }

    auto doc = std::make_unique<Document>(
        QStringLiteral("Part - %1 (%2)").arg(part.title(), drawingKindName));
    loadLibraryPartDocument(*doc, part, kind);
    doc->setDirty(false);
    Document* raw = doc.get();
    libraryPartEdits_.insert(raw, partId);
    libraryPartEditKind_.insert(raw, kind);
    libraryPartSessionModels_.insert(raw, part);
    addDocument(std::move(doc), QString());
    canvas_->fitView();
    QTimer::singleShot(0, this, [this, raw] {
        if (doc_ != raw || !canvas_) return;
        canvas_->fitView();
        canvas_->invalidateScene();
        canvas_->update();
    });
    canvas_->invalidateScene();
    statusBar()->showMessage(
        QStringLiteral("Editing %1's %2 drawing — use Place and Modify, then Capture to save it back")
            .arg(part.title(), drawingKindName),
        6000);
}

// The connection point and the direction the part runs from it: two clicks on the model itself, which is
// where they can be seen. The wizard places a part by them - the drawing is turned so that direction runs
// along the run, and set down so that point lands on it.
void MainWindow::armMechModelConnect() {
    const bool libraryMechSession = doc_ && libraryPartEdits_.contains(doc_) &&
                                     libraryPartEditKind_.value(doc_) == LibraryPartDrawing::Mechanical;
    if (!doc_ || !(mechModelEdits_.contains(doc_) || libraryMechSession)) return;
    editor_->setTool(ToolId::ModelConnectPoint);
    canvas_->setFocus();
    statusBar()->showMessage(
        tr("Click where the part connects — then click along the direction it runs from there"), 6000);
}

// A default model put on the sheet by hand, at the size it was drawn. The office's own drawing of the
// thing, rather than a shape made up here - which is the whole point of keeping default models.
void MainWindow::placeMechModel(MechModelKind kind) {
    MechModelSet set;
    set.load();
    const MechModel* found = set.of(kind);
    // The office's own drawing when there is one, and the built-in otherwise - a button that places nothing
    // until somebody has drawn a part is a button nobody can use. Capturing a drawing of your own replaces
    // this for good; it is a starting point, not a house style.
    MechModel model = found && !found->isEmpty() ? *found : builtInMechModel(kind);
    if (model.isEmpty()) {
        statusBar()->showMessage(
            tr("No %1 has been drawn yet — draw one in Default models and it will be placed here")
                .arg(mechModelName(kind).toLower()),
            6000);
        showMechModels();
        return;
    }
    const std::vector<Entity> ents =
        mechModelEntities(model, Vec2{0.0, 0.0}, 0.0, mechModelDrawnLength(model));
    if (ents.empty()) {
        statusBar()->showMessage(tr("That model has nothing drawn in it"), 4000);
        return;
    }
    // A LABEL is laid on the runs rather than dropped at a fixed size: how wide it has to be is however
    // wide the bundle is where it goes, which is not something to be typed in beforehand. Everything else
    // is placed as drawn, held by its connection point - the point somebody means to put it on.
    if (kind == MechModelKind::Label || kind == MechModelKind::LabelUnderHeatshrink) {
        editor_->beginLabelPlacement(partDrawingEntities(model.drawing), model.exceedMm);
        canvas_->setFocus();
        return;
    }
    std::optional<Vec2> heldAt;
    if (model.hasConnect) heldAt = model.connectPoint;
    editor_->placeEntities(ents, heldAt);
    canvas_->setFocus();
    statusBar()->showMessage(tr("%1 — click to place it").arg(mechModelName(kind)), 4000);
}

// A small picture of a kind, drawn as the thing rather than as a symbol for it.
QIcon MainWindow::makeMechModelIcon(MechModelKind kind) {
    QPixmap pm(28, 28);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor("#cfe3ff"), 1.6));
    p.setBrush(Qt::NoBrush);

    // HEATSHRINK HAS AN ICON OF ITS OWN.
    //
    // Every kind used to get the LABEL drawing - a run with a rectangle wrapped round it - so the
    // heatshrink button on the ribbon was the label button with a different tooltip. Two tools that do
    // different things wearing one picture is a tool nobody can find.
    //
    // Drawn as the thing itself: the run, and a sleeve over it pulled in at both ends. That shape is
    // exactly what tells recovered heatshrink apart from a rigid sleeve on the drawing, so it is what
    // tells the button apart on the ribbon.
    if (kind == MechModelKind::Heatshrink || kind == MechModelKind::TransitionHeatshrink ||
        kind == MechModelKind::BranchSplitHeatshrink) {
        p.drawLine(2, 14, 26, 14);
        const double bodyHalf = 6.0, endHalf = 4.0, x0 = 6.0, x1 = 22.0, taper = 3.0;
        QPolygonF sleeve;
        sleeve << QPointF(x0, 14 - endHalf) << QPointF(x0 + taper, 14 - bodyHalf)
               << QPointF(x1 - taper, 14 - bodyHalf) << QPointF(x1, 14 - endHalf)
               << QPointF(x1, 14 + endHalf) << QPointF(x1 - taper, 14 + bodyHalf)
               << QPointF(x0 + taper, 14 + bodyHalf) << QPointF(x0, 14 + endHalf);
        p.drawPolygon(sleeve);
        return QIcon(pm);
    }

    // The run it sits on, and the label wrapped round it.
    p.drawLine(2, 14, 26, 14);
    p.drawRect(QRectF(8, 7, 12, 14));
    if (kind == MechModelKind::LabelUnderHeatshrink) {
        // The clear shrink over the top: a second, wider outline, dashed to read as transparent.
        QPen dash(QColor("#8fd3ff"), 1.2);
        dash.setStyle(Qt::DashLine);
        p.setPen(dash);
        p.drawRect(QRectF(5, 4, 18, 20));
    }
    return QIcon(pm);
}

// WHICH OBJECT THE APP SCALES THE MODEL BY, marked on the object itself rather than kept beside the model
// as an id or an index. A drawing is captured, moved, re-captured and re-ordered; a mark that travels IN the
// object survives all of that, and a number kept next to the model does not.
// ---- the marks: shown or put away, and taken off one at a time -----------------------------------------
// The connection point, marked. One place, so the two clicks on the canvas and anything else with a reason to ask
// put it in the same way.
void MainWindow::markMechModelConnectAt(const Vec2& at, double angleRad) {
    if (!doc_) return;
    if (mechModelEdits_.contains(doc_)) {
        MechModel& model = mechModelSessionModels_[doc_];
        model.hasConnect = true;
        model.connectPoint = at;
        model.connectAngleRad = angleRad;
        addMechModelGuides(*doc_, model);
    } else if (libraryPartEdits_.contains(doc_) &&
               libraryPartEditKind_.value(doc_) == LibraryPartDrawing::Mechanical) {
        LibraryPart& part = libraryPartSessionModels_[doc_];
        part.hasConnect = true;
        part.connectPoint = at;
        part.connectAngleRad = angleRad;
        addLibraryPartGuides(*doc_, part);
    } else {
        return;
    }
    doc_->setDirty(true);
    canvas_->invalidateScene();
    statusBar()->showMessage(
        tr("Connection point marked, running %1° from it — the wizard places the part by these")
            .arg(angleRad * 180.0 / M_PI, 0, 'f', 1),
        5000);
}

bool MainWindow::mechModelMarksVisible() const {
    if (!doc_) return true;
    if (const Layer* l = editDoc()->findLayer(QLatin1String(kMechModelGuideLayer))) return l->visible;
    return true;
}

// The marks are chrome over the part. While the part itself is being drawn they are in the way of seeing it, so
// they go - and the button shows which state it is in, because a toggle that does not say is a button you press
// twice to find out.
void MainWindow::toggleMechModelMarks() {
    if (!doc_) return;
    const bool on = mechModelMarksVisible();
    for (Layer& l : editDoc()->layers())
        if (l.name == QLatin1String(kMechModelGuideLayer)) l.visible = !on;
    if (showMechModelMarksBtn_) {
        QSignalBlocker block(showMechModelMarksBtn_);
        showMechModelMarksBtn_->setChecked(!on);
    }
    canvas_->invalidateScene();
    canvas_->update();
    statusBar()->showMessage(on ? tr("Marks hidden — the part on its own")
                                : tr("Marks shown"),
                            3000);
}

void MainWindow::clearMechModelConnect() {
    if (!doc_) return;
    const bool libraryMechSession = libraryPartEdits_.contains(doc_) &&
                                     libraryPartEditKind_.value(doc_) == LibraryPartDrawing::Mechanical;
    if (!mechModelEdits_.contains(doc_) && !libraryMechSession) return;
    if (libraryMechSession) {
        LibraryPart& part = libraryPartSessionModels_[doc_];
        if (!part.hasConnect) {
            statusBar()->showMessage(tr("No connection point marked on this model"), 3000);
            return;
        }
        part.hasConnect = false;
        addLibraryPartGuides(*doc_, part);
    } else {
        MechModel& model = mechModelSessionModels_[doc_];
        if (!model.hasConnect) {
            statusBar()->showMessage(tr("No connection point marked on this model"), 3000);
            return;
        }
        model.hasConnect = false;
        addMechModelGuides(*doc_, model);
    }
    doc_->setDirty(true);
    canvas_->invalidateScene();
    canvas_->update();
    statusBar()->showMessage(tr("Connection point taken off — mark it again where it belongs"), 4000);
}

void MainWindow::clearMechModelAnchor(bool near) {
    if (!doc_) return;
    const bool libraryMechSession = libraryPartEdits_.contains(doc_) &&
                                     libraryPartEditKind_.value(doc_) == LibraryPartDrawing::Mechanical;
    if (!mechModelEdits_.contains(doc_) && !libraryMechSession) return;
    if (libraryMechSession) {
        LibraryPart& part = libraryPartSessionModels_[doc_];
        bool& had = near ? part.hasNearPoint : part.hasFarPoint;
        if (!had) {
            statusBar()->showMessage(near ? tr("No near point marked on this model")
                                         : tr("No far point marked on this model"),
                                     3000);
            return;
        }
        had = false;
        addLibraryPartGuides(*doc_, part);
    } else {
        MechModel& model = mechModelSessionModels_[doc_];
        bool& had = near ? model.hasNearPoint : model.hasFarPoint;
        if (!had) {
            statusBar()->showMessage(near ? tr("No near point marked on this model")
                                         : tr("No far point marked on this model"),
                                     3000);
            return;
        }
        had = false;
        addMechModelGuides(*doc_, model);
    }
    doc_->setDirty(true);
    canvas_->invalidateScene();
    canvas_->update();
    statusBar()->showMessage(near ? tr("Near point taken off") : tr("Far point taken off"), 3000);
}

void MainWindow::clearMechModelScaleRef() {
    if (!doc_ || !mechModelEdits_.contains(doc_)) return;
    int had = 0;
    for (Entity& e : doc_->entities()) {
        if (!e.meta.contains(QStringLiteral("modelScaleRef"))) continue;
        e.meta.remove(QStringLiteral("modelScaleRef"));
        ++had;
    }
    if (had == 0) {
        statusBar()->showMessage(tr("Nothing is marked as what this model is scaled by"), 3000);
        return;
    }
    addMechModelGuides(*doc_, mechModelSessionModels_[doc_]);
    doc_->setDirty(true);
    canvas_->invalidateScene();
    canvas_->update();
    statusBar()->showMessage(tr("Taken off — the model is scaled by the whole drawing again"), 4000);
}

void MainWindow::armMechModelScaleRef() {
    if (!doc_ || !mechModelEdits_.contains(doc_)) return;
    editor_->setTool(ToolId::ModelScaleRef);
    canvas_->setFocus();
    statusBar()->showMessage(
        tr("Click the object the model is scaled by - the label inside a sleeve, not the sleeve"), 6000);
}

void MainWindow::armMechModelOutput() {
    const bool libraryMechSession = doc_ && libraryPartEdits_.contains(doc_) &&
                                    libraryPartEditKind_.value(doc_) == LibraryPartDrawing::Mechanical;
    if (!doc_ || !(mechModelEdits_.contains(doc_) || libraryMechSession)) return;
    editor_->setTool(ToolId::ModelOutputLine);
    canvas_->setFocus();
    statusBar()->showMessage(
        tr("Click across the face the wires come out of - one line per branch; Esc to finish"), 6000);
}

void MainWindow::clearMechModelOutputs() {
    const bool libraryMechSession = doc_ && libraryPartEdits_.contains(doc_) &&
                                    libraryPartEditKind_.value(doc_) == LibraryPartDrawing::Mechanical;
    if (!doc_ || !(mechModelEdits_.contains(doc_) || libraryMechSession)) return;
    if (libraryMechSession) {
        LibraryPart& part = libraryPartSessionModels_[doc_];
        if (part.outputs.empty()) {
            statusBar()->showMessage(tr("No output faces are marked on this part"), 3000);
            return;
        }
        part.outputs.clear();
        addLibraryPartGuides(*doc_, part);
        doc_->setDirty(true);
        canvas_->invalidateScene();
        statusBar()->showMessage(tr("Output faces cleared — mark them again from the first branch"), 4000);
        return;
    }
    MechModel& model = mechModelSessionModels_[doc_];
    if (model.outputs.empty()) {
        statusBar()->showMessage(tr("No output faces are marked on this model"), 3000);
        return;
    }
    model.outputs.clear();
    addMechModelGuides(*doc_, model);
    doc_->setDirty(true);
    canvas_->invalidateScene();
    statusBar()->showMessage(tr("Output faces cleared — mark them again from the first branch"), 4000);
}

void MainWindow::armMechModelAnchor(bool near) {
    const bool libraryMechSession = doc_ && libraryPartEdits_.contains(doc_) &&
                                     libraryPartEditKind_.value(doc_) == LibraryPartDrawing::Mechanical;
    if (!doc_ || !(mechModelEdits_.contains(doc_) || libraryMechSession)) return;
    editor_->setTool(near ? ToolId::ModelNearPoint : ToolId::ModelFarPoint);
    canvas_->setFocus();
    statusBar()->showMessage(near ? QStringLiteral("Click the near point — it will be orange")
                                  : QStringLiteral("Click the far point — it will be green"),
                             4000);
}

bool MainWindow::saveMechModelDocument(Document* doc) {
    if (!doc || !mechModelEdits_.contains(doc)) return false;
    const MechModelKind kind = mechModelEdits_.value(doc);
    std::vector<Entity> drawing;
    drawing.reserve(doc->entities().size());
    for (const Entity& e : doc->entities())
        if (e.layer != QLatin1String(kMechModelGuideLayer)) {
            drawing.push_back(e);
            // The grouping is how the model is HANDLED on the canvas, not part of what it is. Saved with
            // it, every copy the generator ever placed would carry the editing session's own stamp.
            clearEntityBlockStamp(drawing.back());
        }

    const Bounds bounds = mechModelBounds(*doc);
    if (drawing.empty() || !bounds.valid) {
        statusBar()->showMessage(QStringLiteral("The model is empty — nothing was saved"), 4000);
        return false;
    }

    MechModelSet set;
    set.load();
    MechModel model = mechModelSessionModels_.value(doc);
    model.kind = kind;
    const bool schematic = mechModelEditSchematic_.value(doc, false);
    if (schematic) {
        // A schematic symbol is not placed by running a direction along it - it is a fixed little
        // glyph - so there is nothing here to shift a connection/near/far point for, and nothing
        // to measure a length or a diameter from. Centre it and leave the rest of the model alone.
        const Vec2 shift{-(bounds.min.x + bounds.max.x) / 2.0, -(bounds.min.y + bounds.max.y) / 2.0};
        for (Entity& e : drawing) translateEntity(e, shift);
        model.schematicDrawing = partDrawingFromEntities(drawing);
    } else {
        // A model is kept running along +x from the origin and centred across it, so the drawing is
        // moved there. Everything MARKED on it has to travel with it: a connection point left behind
        // in the coordinates the drawing used to be at places the part by a point that is no longer
        // anywhere near it - the near and far points were being carried and it was not.
        const Vec2 shift{-bounds.min.x, -(bounds.min.y + bounds.max.y) / 2.0};
        for (Entity& e : drawing) translateEntity(e, shift);
        if (model.hasNearPoint) model.nearPoint += shift;
        if (model.hasFarPoint) model.farPoint += shift;
        if (model.hasConnect) model.connectPoint += shift;
        // The output faces travel with the drawing too. Left behind, a part would be told to put its
        // wires out through a line that is no longer anywhere near it - the same mistake the
        // connection point made.
        for (MechModel::OutputLine& o : model.outputs) {
            o.a += shift;
            o.b += shift;
        }
        model.drawing = partDrawingFromEntities(drawing);
        model.drawnLengthMm = bounds.width();
        // Scaling across is a yes-or-no the manager holds, where zero means leave it alone. Measuring
        // it here unconditionally switched it ON for every model that was ever edited on the canvas,
        // so it is re-measured only for the models that were already being scaled.
        if (model.drawnAcrossMm > 0.0) model.drawnAcrossMm = bounds.height();
    }
    // And the model actually goes back INTO the set. Without this the whole capture was built up and
    // then dropped on the floor: save() writes out what load() read, so a model drawn on the canvas was
    // reported saved, the session closed, and the file was left exactly as it was found.
    set.set(model);
    if (!set.save()) {
        QMessageBox::warning(this, QStringLiteral("Save model failed"),
                             QStringLiteral("The model could not be saved."));
        return false;
    }
    doc->setDirty(false);
    if (modelsDlg_) modelsDlg_->setModels(set);
    // Measured, not whatever the scale-across setting happens to hold: the message says how big the
    // thing that was just saved is, which is the one number worth reading back.
    statusBar()->showMessage(
        QStringLiteral("Saved %1%2 — %3 object(s), %4 mm long, %5 mm across")
            .arg(model.name.isEmpty() ? mechModelName(kind)
                                      : QStringLiteral("%1 (%2)").arg(mechModelName(kind), model.name))
            .arg(schematic ? QStringLiteral(" (schematic)") : QString())
            .arg(drawing.size())
            .arg(bounds.width(), 0, 'f', 1)
            .arg(bounds.height(), 0, 'f', 1),
        5000);
    return true;
}

bool MainWindow::saveLibraryPartDocument(Document* doc) {
    if (!doc || !libraryPartEdits_.contains(doc) || !libraryDlg_) return false;
    const QString partId = libraryPartEdits_.value(doc);
    const LibraryPartDrawing kind = libraryPartEditKind_.value(doc);
    LibraryPart* stored = libraryDlg_->partForSession(partId);
    if (!stored) return false;

    std::vector<Entity> drawing;
    drawing.reserve(doc->entities().size());
    for (const Entity& e : doc->entities())
        if (e.layer != QLatin1String(kMechModelGuideLayer)) {
            drawing.push_back(e);
            clearEntityBlockStamp(drawing.back());
        }
    const Bounds bounds = mechModelBounds(*doc);
    if (drawing.empty() || !bounds.valid) {
        statusBar()->showMessage(QStringLiteral("The drawing is empty — nothing was saved"), 4000);
        return false;
    }

    LibraryPart part = libraryPartSessionModels_.value(doc);
    if (kind == LibraryPartDrawing::Mechanical) {
        // A connector's mechanical drawing is placed by its connect point the same way a default
        // model is - centred across, run from the left-hand end - so the same marks travel with it
        // the same way (see saveMechModelDocument's own comment on this).
        const Vec2 shift{-bounds.min.x, -(bounds.min.y + bounds.max.y) / 2.0};
        for (Entity& e : drawing) translateEntity(e, shift);
        if (part.hasNearPoint) part.nearPoint += shift;
        if (part.hasFarPoint) part.farPoint += shift;
        if (part.hasConnect) part.connectPoint += shift;
        // AND THE FACES THE CONDUCTORS LEAVE BY travel with the drawing like every other mark. They were
        // left behind twice over: not shifted with it, and then not copied into the part at all - so a face
        // marked on the canvas was gone the moment the session closed, and the library still said "no output
        // face marked". A mark that is taken and then dropped is worse than one that cannot be made.
        for (MechOutputFace& f : part.outputs) {
            f.a += shift;
            f.b += shift;
        }
        part.mechanical = partDrawingFromEntities(drawing);
    } else {
        // A schematic symbol or a fanout is a fixed little glyph, not a run to be placed along a
        // direction - centre it and leave it at that.
        const Vec2 shift{-(bounds.min.x + bounds.max.x) / 2.0, -(bounds.min.y + bounds.max.y) / 2.0};
        for (Entity& e : drawing) translateEntity(e, shift);
        const QJsonArray built = partDrawingFromEntities(drawing);
        if (kind == LibraryPartDrawing::Schematic) part.schematic = built;
        else if (kind == LibraryPartDrawing::Fanout) part.fanout = built;
        else part.fanoutOpposite = built;
    }

    // Written straight into the SAME catalog the library dialog is showing - not a fresh one loaded
    // and saved independently - so a field edited in the dialog while this session was open (the
    // description, a parameter) is never clobbered by writing back an older snapshot of the rest of
    // the part. Only the one drawing this session owns, plus its marks, are copied across - the other
    // three drawings are left exactly as `stored` already has them, not overwritten with this
    // session's own snapshot of them from when it was opened.
    if (kind == LibraryPartDrawing::Mechanical) stored->mechanical = part.mechanical;
    else if (kind == LibraryPartDrawing::Schematic) stored->schematic = part.schematic;
    else if (kind == LibraryPartDrawing::Fanout) stored->fanout = part.fanout;
    else stored->fanoutOpposite = part.fanoutOpposite;
    if (kind == LibraryPartDrawing::Mechanical) {
        stored->hasConnect = part.hasConnect;
        stored->connectPoint = part.connectPoint;
        stored->connectAngleRad = part.connectAngleRad;
        stored->hasNearPoint = part.hasNearPoint;
        stored->nearPoint = part.nearPoint;
        stored->hasFarPoint = part.hasFarPoint;
        stored->farPoint = part.farPoint;
        stored->outputs = part.outputs;
    }
    if (!libraryDlg_->saveLibrary()) {
        QMessageBox::warning(this, QStringLiteral("Save part failed"),
                             QStringLiteral("The library part could not be saved."));
        return false;
    }
    doc->setDirty(false);
    statusBar()->showMessage(
        QStringLiteral("Saved %1 — %2 object(s), %3 mm long, %4 mm across")
            .arg(stored->title())
            .arg(drawing.size())
            .arg(bounds.width(), 0, 'f', 1)
            .arg(bounds.height(), 0, 'f', 1),
        5000);
    return true;
}

// One key per sub-drawing, so a boot at 45 and a boot at 90 keep their own view - and the
// mechanical and schematic drawings of the same model keep theirs apart too.
QString MainWindow::mechModelViewKey(MechModelKind kind, const QString& name, bool schematic) {
    return mechModelKey(kind) + QLatin1Char('/') + name +
           (schematic ? QStringLiteral("/schematic") : QString());
}

// How this model is being looked at, written down before the window closes. Drawing a part is not one sitting: it
// is opened, zoomed in on the end that matters, closed to go and check something, and opened again - and a window
// that fits the whole part every time is one that makes you find that place again every time.
void MainWindow::rememberMechModelView(Document* doc) {
    if (!doc || !mechModelEdits_.contains(doc)) return;
    const MechModel& m = mechModelSessionModels_[doc];
    mechModelViews_.insert(
        mechModelViewKey(mechModelEdits_.value(doc), m.name, mechModelEditSchematic_.value(doc, false)),
        doc->view());
}

void MainWindow::saveMechModelBeingEdited() {
    if (!doc_) return;
    if (libraryPartEdits_.contains(doc_)) {
        const QString partId = libraryPartEdits_.value(doc_);
        if (saveLibraryPartDocument(doc_)) {
            closeDocument(activeIndex());
            showPartsLibraryAt(partId);
        }
        return;
    }
    if (!mechModelEdits_.contains(doc_)) return;
    rememberMechModelView(doc_);
    if (saveMechModelDocument(doc_)) {
        closeDocument(activeIndex());
        showMechModels();
    }
}

void MainWindow::cancelMechModelEdit() {
    if (!doc_) return;
    if (libraryPartEdits_.contains(doc_)) {
        const QString partId = libraryPartEdits_.value(doc_);
        doc_->setDirty(false);
        closeDocument(activeIndex());
        showPartsLibraryAt(partId);
        return;
    }
    if (!mechModelEdits_.contains(doc_)) return;
    // The view is kept even when the drawing is not: how you were looking at it is not part of what was changed,
    // and leaving a session to go and check something should not cost the place you were looking at.
    rememberMechModelView(doc_);
    doc_->setDirty(false);
    closeDocument(activeIndex());
    showMechModels();
}

QToolButton* MainWindow::makeMechModelsButton(QWidget* parent) {
    auto* btn = new QToolButton(parent);
    btn->setText(QStringLiteral("Default\nmodels"));
    btn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    btn->setIconSize(QSize(26, 26));
    btn->setToolTip(QStringLiteral(
        "How this office draws the standard pieces — a wire, a cable, heatshrink, a ferrule, a ring, a "
        "boot, a Y-split. Drawn once here and used by every harness drawing generated."));
    {
        // A length with an end on it, and the arrows that say the middle is what stretches.
        QPixmap pm(28, 28);
        pm.fill(Qt::transparent);
        QPainter ip(&pm);
        ip.setRenderHint(QPainter::Antialiasing, true);
        ip.setPen(QPen(QColor("#8fb7ff"), 1.6));
        ip.drawLine(4, 10, 24, 10);
        ip.drawLine(4, 16, 24, 16);
        ip.setPen(QPen(QColor("#e6b45a"), 1.6));
        ip.drawLine(4, 7, 4, 19);              // the end that does not stretch
        ip.drawLine(24, 7, 24, 19);
        ip.setPen(QPen(QColor("#7ee0a0"), 1.4));
        ip.drawLine(9, 13, 19, 13);            // and the middle that does
        ip.drawLine(9, 13, 12, 11);
        ip.drawLine(9, 13, 12, 15);
        ip.drawLine(19, 13, 16, 11);
        ip.drawLine(19, 13, 16, 15);
        ip.end();
        btn->setIcon(QIcon(pm));
    }
    connect(btn, &QToolButton::clicked, this, [this] { showMechModels(); });
    return btn;
}

void MainWindow::markConnectionLine() {
    if (!editor_) return;
    // A toggle: marked objects are unmarked, unmarked ones are marked, so the one command both puts
    // the mark on and takes it off - which is how it will be used, a line at a time, while a part's
    // drawing is being made.
    const bool already = editor_->selectionIsConnectionLine();
    const int n = editor_->markSelectionAsConnectionLine(!already);
    if (n == 0) {
        statusBar()->showMessage(
            tr("Select the line the harness attaches to on this part first."), 4000);
        return;
    }
    statusBar()->showMessage(already
                                 ? tr("%1 object(s) are no longer the connection line").arg(n)
                                 : tr("%1 object(s) marked as the connection line — a part placed by "
                                      "the builder is set down across the wires by it").arg(n),
                             5000);
}

void MainWindow::showMechWizard() {
    // THE BRICK WIZARD: end parts and the runs between them as bricks on a pane, the real drawing
    // previewed under it. Modeless and kept alive, like the others: a harness is described over
    // several sittings, and closing the window to look at the sheet must not throw the answers away.
    if (!brickWizard_) {
        brickWizard_ = new BrickWizardDialog(editor_, this);
        // It writes the drawing's bill; the panel and the bill's own window show that same bill.
        connect(brickWizard_, &BrickWizardDialog::bomChanged, this, [this] {
            refreshBomPanel();
            if (bomDlg_) bomDlg_->reload();
        });
    }
    brickWizard_->show();
    brickWizard_->raise();
    brickWizard_->activateWindow();
}

void MainWindow::showStepWizard() {
    if (!mechWizard_) mechWizard_ = new MechWizardDialog(editor_, this);
    mechWizard_->show();
    mechWizard_->raise();
    mechWizard_->activateWindow();
}

void MainWindow::showHarnessSteps() {
    // Modeless and kept alive, like the builder it shares its model with: a harness is described over
    // several sittings, and closing the window to look at the sheet must not throw the answers away.
    if (!stepsDlg_) {
        stepsDlg_ = new HarnessStepsDialog(editor_, this);
        // Its "add the parts to the bill of materials" tick writes the drawing's bill. The panel and
        // the bill's own window show that same bill, so they follow it.
        connect(stepsDlg_, &HarnessStepsDialog::bomChanged, this, [this] {
            refreshBomPanel();
            if (bomDlg_) bomDlg_->reload();
        });
    }
    stepsDlg_->show();
    stepsDlg_->raise();
    stepsDlg_->activateWindow();
}

void MainWindow::showVoltageDropDialog() {
    // Modeless and kept alive: the drawing is edited - a gauge changed, a splice moved - and the
    // same window re-reads it on the next Calculate rather than being reopened.
    if (!voltageDropDlg_) voltageDropDlg_ = new VoltageDropDialog(editor_, this);
    voltageDropDlg_->reloadFromDocument();
    voltageDropDlg_->run();   // open on an answer, not on an empty table
    voltageDropDlg_->show();
    voltageDropDlg_->raise();
    voltageDropDlg_->activateWindow();
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
    flushPaperSpace();   // the sheet being left belongs to the drawing being left
    doc_ = docs_[index].get();
    canvas_->setDocument(doc_);
    editor_->setDocument(doc_); // resets selection/undo for this drawing
    // PARTS PUT DOWN BEFORE THEIR ENTRY FACES WERE CARRIED get them now, from the library - so Alt
    // shows them and Shift lands a run on them on an old drawing as on a new one.
    {
        bool missing = false;
        for (const Entity& e : doc_->entities())
            if (e.meta.contains(QStringLiteral("libPart")) &&
                !e.meta.contains(QLatin1String(kEntryFacesKey))) {
                missing = true;
                break;
            }
        if (missing) {
            PartsCatalog catalog;
            catalog.openRemembered();
            recoverEntryFaces(doc_->entities(), catalog);
        }
    }
    syncDrawingSpaceTabs();
    // A block being edited is not a drawing: the canvas goes grey, so it is never in doubt which
    // one is which. Switching back to a drawing puts the ordinary background back.
    const bool editingBlock = blockEdits_.contains(doc_);
    const bool editingTemplate = templateEdits_.contains(doc_);
    const bool editingMechModel = mechModelEdits_.contains(doc_);
    const bool editingLibraryPart = libraryPartEdits_.contains(doc_);
    // A library part's MECHANICAL drawing gets the same near/far/connect marks a default model does;
    // its schematic and fanout drawings get the grey canvas, and Save/Cancel, but none of these marks.
    const bool editingLibraryPartMechanical =
        editingLibraryPart && libraryPartEditKind_.value(doc_) == LibraryPartDrawing::Mechanical;
    const bool anyModelEdit = editingMechModel || editingLibraryPart;
    const bool anchorableModelEdit = editingMechModel || editingLibraryPartMechanical;
    // Each kind of edit gets its own colour, so which one is open is never in doubt: grey for a
    // block, a blue slate for a template, the ordinary dark for a drawing.
    // Both editors get a grey canvas - a lighter one for a template, so it reads as a sheet being
    // laid out rather than as a drawing. Either way it is never mistaken for the drawing itself.
    // Which space the drawing being opened shows. Standing on a sheet and opening a drawing that has
    // sheets of its own shows ITS sheet - the same one by number if it goes that far, otherwise its
    // first - because the work in hand is laying out sheets, and dropping to model space interrupts it.
    // A drawing with no sheets shows its model; there is nothing else it could show.
    int space = drawingSpaceTabs_ ? drawingSpaceTabs_->currentIndex() : 0;
    const int sheets = static_cast<int>(doc_->layouts().size());
    if (space > 0) space = sheets > 0 ? std::min(space, sheets) : 0;
    if (drawingSpaceTabs_ && drawingSpaceTabs_->currentIndex() != space) {
        const QSignalBlocker block(drawingSpaceTabs_);
        drawingSpaceTabs_->setCurrentIndex(space);
    }
    // The space itself does the rest - the layout mode, the paper space the editor works in, the grid
    // and the plot scale - without refitting the view the drawing was saved with.
    activateDrawingSpace(space, /*fitToSpace=*/false);
    // The Template group is only on the ribbon while a template is open - buttons that mean nothing
    // to a drawing are not shown to it at all, rather than shown greyed out.
    if (tplHomeGroup_) tplHomeGroup_->setVisible(editingTemplate);
    if (mechModelSessionGroup_) mechModelSessionGroup_->setVisible(anyModelEdit);
    // The way out, beside the tab bar: up for as long as there is a session to leave, whichever tab is open.
    if (modelSessionCorner_) modelSessionCorner_->setVisible(anyModelEdit);
    if (cornerCaptureBtn_) cornerCaptureBtn_->setEnabled(anyModelEdit);
    if (cornerCancelBtn_) cornerCancelBtn_->setEnabled(anyModelEdit);
    const bool anchorableMechModel =
        anchorableModelEdit && !(editingMechModel && mechModelStretches(mechModelEdits_.value(doc_)));
    if (captureMechModelBtn_) captureMechModelBtn_->setEnabled(anyModelEdit);
    if (markConnectMechModelBtn_) markConnectMechModelBtn_->setEnabled(anchorableModelEdit);
    if (markNearMechModelBtn_) markNearMechModelBtn_->setEnabled(anchorableMechModel);
    if (markFarMechModelBtn_) markFarMechModelBtn_->setEnabled(anchorableMechModel);
    // WHERE THE WIRES LEAVE is not a default-model-only idea. It was reasoned that a library part's
    // connector has its own pins for that - true of a connector that was built from its data, and no help
    // at all for a connector traced off a supplier's drawing, which is most of them: a body and an outline
    // with nothing in it that says which edge the conductors come out of. A part IS placed by that face
    // (see placedPartDrawing), so it has to be possible to mark one, and the button was greyed.
    //
    // WHAT IT IS SCALED BY stays default-model-only, and that reasoning does hold: a model is stretched and
    // scaled to fit the bundle it is put on, and a part out of a catalogue is the size it is.
    if (markOutputMechModelBtn_) markOutputMechModelBtn_->setEnabled(anchorableModelEdit);
    if (markScaleRefMechModelBtn_) markScaleRefMechModelBtn_->setEnabled(editingMechModel);
    if (showMechModelMarksBtn_) showMechModelMarksBtn_->setEnabled(anchorableModelEdit);
    if (cancelMechModelBtn_) cancelMechModelBtn_->setEnabled(anyModelEdit);
    for (QToolButton* b : tplSessionBtns_)
        if (b) b->setEnabled(editingTemplate);
    // The template ribbon comes and goes with the session, the way the hatch and table-cell tabs do,
    // and the harness tabs go the other way: away while a sheet is being laid out.
    showHarnessRibbonTabs(!editingTemplate);
    showTemplateRibbon(editingTemplate);
    updateMechanicalRibbonGroups();
    updateSchematicRibbonGroups();   // the design-rule check goes while a symbol is being drawn
    updateModelRibbonTabs();
    if (editingTemplate) syncTemplateRibbon();
    if (saveBlockBtn_) {
        saveBlockBtn_->setVisible(editingBlock);
        saveBlockBtn_->setEnabled(editingBlock);
        saveBlockBtn_->setToolTip(
            QStringLiteral("Save the block %1, rebuild every copy of it, and close it")
                .arg(blockEdits_.value(doc_)));
    }
    if (cancelBlockBtn_) {
        cancelBlockBtn_->setVisible(editingBlock);
        cancelBlockBtn_->setEnabled(editingBlock);
    }
    normaliseRibbonPages();   // never two pages at once, whatever came and went above
    refreshPanels();
    updateWindowTitleForActive();
}

// The three drawings a part can have, named once so the columns, their tooltips and the menu all say the
// same words.
static const char* const kPartDrawingNames[3] = {"mechanical", "schematic", "fanout"};

QDockWidget* MainWindow::makePartsPanel() {
    auto* dock = new AutoHideDock(QStringLiteral("PARTS"), this);
    // Movable, floatable, closable - the same terms as the panels that were here before, so a panel behaves
    // the same whichever of them it is.
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                      QDockWidget::DockWidgetClosable);
    auto* body = new QWidget(dock);
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(4);

    // The filter, and beside it which libraries the list is drawn from. A shop connects more than one
    // library - the house library, a customer's, a project's - and the panel is a shelf: which shelves are
    // in view is the first thing to be able to say about it.
    auto* head = new QWidget(body);
    auto* hh = new QHBoxLayout(head);
    hh->setContentsMargins(0, 0, 0, 0);
    hh->setSpacing(4);
    partsPanelFilter_ = new QLineEdit(head);
    partsPanelFilter_->setPlaceholderText(QStringLiteral("filter by P/N, description or manufacturer"));
    connect(partsPanelFilter_, &QLineEdit::textChanged, this, [this] { refreshPartsPanel(); });
    hh->addWidget(partsPanelFilter_, 1);
    // BY TYPE. A real library is mostly heat-shrink sizes and marker sleeves - a hundred and twenty rows of
    // which two are the connectors somebody is looking for - so the shelf is grouped by what a part IS, and
    // one kind can be asked for on its own. The kinds with nothing in them are not offered: a menu of
    // twenty-six kinds where a library holds six is a menu you read rather than use.
    partsPanelKindBox_ = new QComboBox(head);
    partsPanelKindBox_->setToolTip(QStringLiteral(
        "Which kind of part the shelf shows. Grouped by kind either way - this narrows it to one."));
    partsPanelKindBox_->setMinimumContentsLength(10);
    partsPanelKindBox_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    connect(partsPanelKindBox_, &QComboBox::currentIndexChanged, this, [this](int) {
        if (partsPanelSyncing_) return;
        refreshPartsPanel();
    });
    hh->addWidget(partsPanelKindBox_);
    auto* libsBtn = new QToolButton(head);
    libsBtn->setText(QStringLiteral("Libraries"));
    libsBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    libsBtn->setPopupMode(QToolButton::InstantPopup);
    libsBtn->setToolTip(QStringLiteral("Which of the connected libraries this list is drawn from"));
    auto* libsMenu = new QMenu(libsBtn);
    // Filled when it is opened, not when it is built: a library connected since is on the list.
    connect(libsMenu, &QMenu::aboutToShow, this, [this, libsMenu] { fillPartsLibraryMenu(libsMenu); });
    libsBtn->setMenu(libsMenu);
    hh->addWidget(libsBtn);
    v->addWidget(head);

    partsPanelTree_ = new QTreeWidget(body);
    partsPanelTree_->setColumnCount(4);
    partsPanelTree_->setHeaderLabels({tr("Part"), tr("M"), tr("S"), tr("F")});
    // The three columns are what a part IS TO THE DRAWING, so they are named where they are read.
    partsPanelTree_->headerItem()->setToolTip(1, tr("Mechanical drawing"));
    partsPanelTree_->headerItem()->setToolTip(2, tr("Schematic symbol"));
    partsPanelTree_->headerItem()->setToolTip(3, tr("Fanout drawing"));
    partsPanelTree_->setRootIsDecorated(true);   // a heading per kind, which opens and closes
    // THE SECTIONS CAN BE PUT IN THE ORDER SOMEBODY WORKS IN. The shelf is grouped by kind, and which kind
    // matters most is the shop's business, not the program's: a connector shop wants connectors at the top
    // and the heat-shrink sizes further down; a panel shop the other way about. Drag a heading, and the
    // order is remembered between sessions.
    //
    // Only the HEADINGS move. A part belongs to its kind - dragging one out of its section would say it was
    // something it is not - so a part row is neither dragged nor dropped on.
    //
    // THE DRAG IS THE PANEL'S OWN (partsPanelDragEvent), not the tree's. The tree's InternalMove drags
    // what is SELECTED, and a heading is not selectable - so pressing one and pulling did nothing - and
    // where it would have dropped it it could as easily have put one section INSIDE another. Here a
    // heading is taken up by pulling it, a line is drawn in the gap between two sections where it will
    // land, and letting go puts it there.
    partsPanelTree_->setDragDropMode(QAbstractItemView::NoDragDrop);
    partsPanelTree_->viewport()->setAcceptDrops(true);
    partsPanelTree_->viewport()->installEventFilter(this);
    partsDropGuide_ = new QWidget(partsPanelTree_->viewport());
    partsDropGuide_->setAttribute(Qt::WA_TransparentForMouseEvents);
    partsDropGuide_->setAttribute(Qt::WA_StyledBackground);
    partsDropGuide_->setStyleSheet(QStringLiteral("background:#3fa9f5;border-radius:1px;"));
    partsDropGuide_->hide();
    partsPanelTree_->setUniformRowHeights(true);
    partsPanelTree_->setAllColumnsShowFocus(true);
    // The part takes what is left, and the three tick columns stay narrow. Left to itself a header stretches
    // its LAST section, which gave the fanout column half the panel and the part's name none of it.
    partsPanelTree_->header()->setStretchLastSection(false);
    partsPanelTree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int c = 1; c <= 3; ++c) {
        partsPanelTree_->header()->setSectionResizeMode(c, QHeaderView::Fixed);
        partsPanelTree_->header()->resizeSection(c, 22);
    }
    partsPanelTree_->setToolTip(QStringLiteral(
        "Every part of every library in view, and which drawings it has: M mechanical, S schematic, "
        "F fanout. Double-click to put one on the sheet; a part with no drawing at all opens in the "
        "library, where one can be made for it."));
    // WHAT A DOUBLE-CLICK PUTS DOWN is said once, here, rather than guessed at per part. On the NAME it
    // used to place "the mechanical drawing if it has one", which is right for an assembly sheet and wrong
    // all day on a schematic - and a part with three drawings and one gesture had no way of being asked.
    // The columns still say which drawing (double-click the tick under S and the symbol comes down); this is
    // for the double-click on the part itself, and it can name more than one.
    //
    // Remembered between sessions: it is a habit that follows the kind of sheet somebody works on.
    // Double-click places it. One gesture, no dialog: which is the whole reason for the panel.
    // A HEADING ROLLED UP OR OPENED is remembered, so the shelf comes back the way it was left.
    connect(partsPanelTree_, &QTreeWidget::itemCollapsed, this,
            [this](QTreeWidgetItem* it) { rememberPartsPanelHeading(it, true); });
    connect(partsPanelTree_, &QTreeWidget::itemExpanded, this,
            [this](QTreeWidgetItem* it) { rememberPartsPanelHeading(it, false); });
    connect(partsPanelTree_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int column) {
                if (!item) return;
                const QString id = item->data(0, Qt::UserRole).toString();
                if (id.isEmpty()) return;   // a library's name, not a part
                const bool hasMech = item->data(0, Qt::UserRole + 1).toBool();
                const bool hasAny = item->data(0, Qt::UserRole + 2).toBool();
                const bool hasFan = item->data(0, Qt::UserRole + 3).toBool();
                // The COLUMN says which drawing: double-click the tick under S and the schematic symbol is
                // what comes down. The columns are how the row says what it has, so they are also how it is
                // asked for - a part with three drawings and one gesture had no way of saying which.
                if (column == 1 || column == 2 || column == 3) {
                    if (column == 3) {
                        // The fanout, drawn for the part or worked out from its ways - placeLibraryFanout
                        // knows which, and says so either way. The tick still means one was DRAWN.
                        Q_UNUSED(hasFan);
                        placeLibraryFanout(id, false);
                        return;
                    }
                    if (column == 2 && !hasSchematicDrawing(id)) {
                        statusBar()->showMessage(tr("%1 has no schematic symbol").arg(item->text(0)), 4000);
                        return;
                    }
                    if (column == 1 && !hasMech) {
                        statusBar()->showMessage(tr("%1 has no mechanical drawing").arg(item->text(0)),
                                                 4000);
                        return;
                    }
                    Q_UNUSED(hasAny);
                    placeLibraryPart(id, column == 1);
                    return;
                }
                if (!hasAny) {
                    // Nothing to put down yet. Rather than a shrug, the part opens where one is made.
                    statusBar()->showMessage(
                        tr("%1 has no drawing yet — opening it in the library").arg(item->text(0)), 6000);
                    showPartsLibraryAt(id);
                    return;
                }
                // On the name: whatever the ticks under the shelf say - one drawing, two, or all of them.
                placeFromPartsPanel(id, hasMech);
            });
    // Right-click: what can be done with the part under the cursor. The commands are the same ones the
    // library window has - place it, add it to the bill of materials, edit it - and a menu on the part is
    // where they are looked for when the part is what is being pointed at.
    partsPanelTree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(partsPanelTree_, &QTreeWidget::customContextMenuRequested, this,
            [this](const QPoint& at) { showPartsPanelMenu(at); });
    v->addWidget(partsPanelTree_, 1);
    partsPanelCount_ = new QLabel(body);
    partsPanelCount_->setToolTip(QStringLiteral("What is in view, out of what is connected"));
    v->addWidget(partsPanelCount_);
    {
        auto* what = new QWidget(body);
        auto* wl = new QHBoxLayout(what);
        wl->setContentsMargins(0, 0, 0, 0);
        wl->setSpacing(6);
        wl->addWidget(new QLabel(tr("Double-click places:"), what));
        struct Which { const char* text; const char* key; const char* tip; };
        const Which whichOnes[] = {
            {"mechanical", "mech", "The part as it is in the metal"},
            {"schematic", "schem", "Its symbol, for a circuit sheet"},
            // FRONT AND BACK, not "mirrored". Which side of the connector it is drawn from is what a fanout
            // IS - the same drawing seen from the face or from the wires - and "mirrored" says how the second
            // one was made rather than what it shows. Somebody placing one is choosing a side.
            {"fanout front", "fan", "How the wires break out of it, seen from the face"},
            {"fanout back", "fanOpp", "The same fanout seen from the wires"}};
        QSettings s;
        for (const Which& w : whichOnes) {
            auto* box = new QCheckBox(tr(w.text), what);
            box->setToolTip(tr(w.tip));
            const QString key = QStringLiteral("ui/partsPanelPlace/") + QLatin1String(w.key);
            // Mechanical to begin with, which is what it did before there was a choice.
            box->setChecked(s.value(key, QLatin1String(w.key) == QLatin1String("mech")).toBool());
            connect(box, &QCheckBox::toggled, this, [key](bool on) {
                QSettings st;
                st.setValue(key, on);
                st.sync();
            });
            partsPanelPlaceBoxes_.push_back(box);
            wl->addWidget(box);
        }
        wl->addStretch(1);
        v->addWidget(what);
    }

    // The two rules about placing a part and counting it. They belong here rather than only in the
    // bill-of-materials window, because this is where parts are put down - and they ARE that window's own
    // two switches, kept with the drawing, not a second set of settings that could disagree with them.
    bomOnPlaceBox_ = new QCheckBox(tr("A placed part goes on the bill of materials"), body);
    bomOnPlaceBox_->setToolTip(tr(
        "Every part put on the sheet is counted on the bill of materials, with its part number and its "
        "unit. Off, the bill lists only what was added to it by hand.\nKept with the drawing - the "
        "bill-of-materials window shows the same switch."));
    connect(bomOnPlaceBox_, &QCheckBox::toggled, this, [this](bool on) { setBomRule(true, on); });
    v->addWidget(bomOnPlaceBox_);
    bomUnifyBox_ = new QCheckBox(tr("The same part twice is one line, counted twice"), body);
    bomUnifyBox_->setToolTip(tr(
        "Two of the same part number are one line with a quantity of two, rather than two lines of one. "
        "Quantities in units that cannot be added - metres and pieces - are never merged.\nKept with the "
        "drawing - the bill-of-materials window shows the same switch."));
    connect(bomUnifyBox_, &QCheckBox::toggled, this, [this](bool on) { setBomRule(false, on); });
    v->addWidget(bomUnifyBox_);
    // A SCHEMATIC CONNECTOR AND ITS FANOUT, PLACED TOGETHER. The fanout is the picture of how the wires
    // break out of that connector, and on a schematic sheet it belongs beside the symbol it belongs to -
    // put down separately it has to be lined up by hand every time, and a fanout lined up by eye against
    // the wrong connector is a drawing that lies.
    //
    // A toggle, because it is not always wanted: a sheet of forty connectors would be a sheet of forty
    // fanouts. Remembered between sessions - it is a habit, not a decision to be made per part.
    fanoutWithSchematicBox_ = new QCheckBox(tr("A schematic connector is placed with its fanout"), body);
    fanoutWithSchematicBox_->setToolTip(tr(
        "The part's fanout - how the wires break out of it - goes down beside the schematic symbol, in "
        "front of it and next to its designator, as one placement: the two land together and stay "
        "together.\nOnly for a part that HAS a fanout drawn; anything else is placed as before."));
    {
        QSettings s;
        fanoutWithSchematicBox_->setChecked(
            s.value(QStringLiteral("ui/placeFanoutWithSchematic"), false).toBool());
    }
    connect(fanoutWithSchematicBox_, &QCheckBox::toggled, this, [](bool on) {
        QSettings().setValue(QStringLiteral("ui/placeFanoutWithSchematic"), on);
    });
    v->addWidget(fanoutWithSchematicBox_);
    {
        auto* open = new QPushButton(QStringLiteral("Open the parts library"), body);
        letSqueeze(open);
        connect(open, &QPushButton::clicked, this, [this] { showPartsLibrary(); });
        v->addWidget(open);
    }
    dock->setManaged(new DockTitleBar(dock, QStringLiteral("PARTS")), body);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
    dock->hide();   // asked for from the View tab, rather than filling the screen unbidden
    refreshPartsPanel();
    return dock;
}

// The two boxes say what the DRAWING is set to, because that is where the rules are kept: a sheet keeps the
// way its bill of materials is counted, so opening another drawing shows that drawing's rules.
void MainWindow::syncBomRuleBoxes() {
    const Document* doc = editDoc();
    if (!doc) return;
    BomTable table;
    table.fromJson(doc->bom());
    if (bomOnPlaceBox_) {
        const QSignalBlocker block(bomOnPlaceBox_);
        bomOnPlaceBox_->setChecked(table.config.includeDrawing);
    }
    if (bomUnifyBox_) {
        const QSignalBlocker block(bomUnifyBox_);
        bomUnifyBox_->setChecked(table.config.unify);
    }
}

void MainWindow::setBomRule(bool onPlace, bool value) {
    Document* doc = editDoc();
    if (!doc) return;
    BomTable table;
    table.fromJson(doc->bom());
    if (onPlace) {
        if (table.config.includeDrawing == value) return;
        table.config.includeDrawing = value;
    } else {
        if (table.config.unify == value) return;
        table.config.unify = value;
    }
    doc->bom() = table.toJson();
    doc->setDirty(true);
    refreshBomPanel();
    statusBar()->showMessage(onPlace ? (value ? tr("Parts placed on the sheet are counted on the bill of "
                                                  "materials")
                                              : tr("The bill of materials now lists only what was added to "
                                                   "it by hand"))
                                     : (value ? tr("The same part twice is one line, counted twice")
                                              : tr("Each of the same part is a line of its own")),
                             6000);
}

// The menu on a part in the panel. Only what CAN be done is offered: a part with no fanout is not given a
// "Place the fanout" that would answer with a message in the status bar.
// Has this part a schematic symbol to place - its own, or the standard one for its kind? Asked by name so
// the panel's double-click can answer for the column it was made on.
bool MainWindow::hasSchematicDrawing(const QString& partId) const {
    PartsCatalog catalog;
    catalog.openRemembered();
    const LibraryPart* part = catalog.byId(partId);
    return part && !partSchematicDrawing(*part).isEmpty();
}

void MainWindow::showPartsPanelMenu(const QPoint& at) {
    if (!partsPanelTree_) return;
    QTreeWidgetItem* item = partsPanelTree_->itemAt(at);
    if (!item) return;
    const QString id = item->data(0, Qt::UserRole).toString();
    if (id.isEmpty()) return;   // a library's heading, not a part
    if (QMenu* menu = buildPartsPanelMenu(id)) {
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(partsPanelTree_->viewport()->mapToGlobal(at));
    }
}

// Built rather than shown, so what is on it can be asked for: by the panel, which pops it up, and by a test,
// which reads it.
QMenu* MainWindow::buildPartsPanelMenu(const QString& id) {
    PartsCatalog catalog;
    catalog.openRemembered();
    const LibraryPart* part = catalog.byId(id);
    if (!part) return nullptr;

    auto* menuPtr = new QMenu(this);
    QMenu& menu = *menuPtr;
    const bool mech = part->hasMechanical();
    const bool schem = !partSchematicDrawing(*part).isEmpty();
    const bool fan = part->hasFanout();
    if (mech)
        menu.addAction(tr("Place the mechanical drawing"), this, [this, id] { placeLibraryPart(id, true); });
    if (schem)
        menu.addAction(tr("Place the schematic symbol"), this, [this, id] { placeLibraryPart(id, false); });
    if (fan) {
        // Both sides, named for what each one shows rather than for what was done to it, with the part's own
        // side first - it is the one a plain "fanout" means.
        const bool frontFirst = part->fanoutSide() != QStringLiteral("back");
        menu.addAction(tr("Place the fanout (%1 of the connector)").arg(frontFirst ? tr("front") : tr("back")),
                       this, [this, id] { placeLibraryFanout(id, false); });
        menu.addAction(
            tr("Place the fanout (%1 of the connector)").arg(frontFirst ? tr("back") : tr("front")),
            this, [this, id] { placeLibraryFanout(id, true); });
    } else if (canHaveFanout(*part)) {
        // None drawn - so one is worked out from the part's own data and placed. Being sent to the library
        // to draw one is not placing a fanout; the generated one can be drawn over, or replaced by capturing
        // a real one in the library.
        menu.addAction(tr("Place the fanout — %1 way(s), from its own data").arg(part->wayCount()), this,
                       [this, id] { placeLibraryFanout(id, false); });
        menu.addAction(tr("Place the fanout mirrored — %1 way(s), from its own data").arg(part->wayCount()),
                       this, [this, id] { placeLibraryFanout(id, true); });
    }
    if (!mech && !schem && !fan) {
        QAction* none = menu.addAction(tr("No drawing to place yet"));
        none->setEnabled(false);
    }
    menu.addSeparator();
    menu.addAction(tr("Add to the bill of materials"), this, [this, id] { addPartToBom(id); });
    menu.addAction(tr("Edit in the library"), this, [this, id] { showPartsLibraryAt(id); });
    return menuPtr;
}

// The menu on a line of the bill of materials. A line put there BY HAND can be taken off; a line the drawing
// itself calls for cannot, and saying why is more use than a greyed-out command: the way to remove it is to
// take the thing off the drawing, or to stop counting the drawing at all.
void MainWindow::showBomPanelMenu(const QPoint& at) {
    if (!bomPanel_ || !editDoc()) return;
    QTableWidgetItem* cell = bomPanel_->itemAt(at);
    const int row = cell ? cell->row() : -1;
    QMenu menu(this);
    if (row >= 0) {
        // Column 1, the DESCRIPTION: column 0 is the line number now, and a position knows nothing about
        // the part it happens to be numbering.
        QTableWidgetItem* first = bomPanel_->item(row, 1);
        const bool byHand = first && first->data(Qt::UserRole + 1).toBool();
        const QString what = first ? first->text() : QString();
        if (byHand) {
            menu.addAction(tr("Remove \"%1\" from the bill of materials").arg(what), this,
                           [this, row] { removeBomPanelRow(row); });
        } else {
            QAction* why = menu.addAction(tr("\"%1\" is what the drawing calls for").arg(what));
            why->setEnabled(false);
            menu.addAction(tr("Stop counting the drawing (the window's own switch)"), this,
                           [this] { showBomDialog(); });
        }
        menu.addSeparator();
    }
    menu.addAction(tr("Open the bill of materials"), this, [this] { showBomDialog(); });
    menu.exec(bomPanel_->viewport()->mapToGlobal(at));   // a menu on a table row: modal is what is wanted
}

// Take a hand-added line off. The panel shows the lines TOTALLED, so one line on screen can be several rows
// in the drawing's own table: every hand-added row that names the same thing goes, and the count is said, or
// a part added twice would take two goes to remove and look as though the first had not worked.
void MainWindow::removeBomPanelRow(int row) {
    Document* doc = editDoc();
    if (!doc || !bomPanel_) return;
    QTableWidgetItem* first = bomPanel_->item(row, 1);   // the description, not the line number
    if (!first) return;
    const QString partId = first->data(Qt::UserRole + 2).toString();
    const QString mfrPn = first->data(Qt::UserRole + 3).toString();
    const QString description = first->text();
    BomTable table;
    table.fromJson(doc->bom());
    // The same test the totalling uses: the library id if there is one, else the manufacturer's P/N, else
    // what it is called.
    auto sameThing = [&](const BomRow& r) {
        if (!partId.isEmpty()) return r.partId == partId;
        if (!mfrPn.isEmpty()) return r.mfrPn == mfrPn;
        return r.description == description;
    };
    std::vector<BomRow> kept;
    int gone = 0;
    for (const BomRow& r : table.rows) {
        if (!r.fromDrawing && sameThing(r)) { ++gone; continue; }
        kept.push_back(r);
    }
    if (gone == 0) return;
    table.rows = kept;
    doc->bom() = table.toJson();
    doc->setDirty(true);
    refreshBomPanel();
    statusBar()->showMessage(gone == 1 ? tr("%1 removed from the bill of materials").arg(description)
                                       : tr("%1 — %2 lines removed from the bill of materials")
                                             .arg(description)
                                             .arg(gone),
                             6000);
}

// The libraries that are connected, each with a tick: on the list or not. A library is remembered as hidden
// by its own path, so connecting another does not disturb what is in view.
void MainWindow::fillPartsLibraryMenu(QMenu* menu) {
    if (!menu) return;
    menu->clear();
    PartsCatalog catalog;
    catalog.openRemembered();
    const QStringList paths = catalog.paths();
    if (paths.isEmpty()) {
        QAction* none = menu->addAction(QStringLiteral("No library is connected"));
        none->setEnabled(false);
        return;
    }
    for (const QString& path : paths) {
        const PartsLibrary* lib = catalog.byPath(path);
        const int count = lib ? static_cast<int>(lib->parts.size()) : 0;
        QAction* a = menu->addAction(
            QStringLiteral("%1  (%2)").arg(QFileInfo(path).completeBaseName()).arg(count));
        a->setCheckable(true);
        a->setChecked(!partsPanelHidden_.contains(path));
        a->setToolTip(path);
        connect(a, &QAction::toggled, this, [this, path](bool on) {
            if (on) partsPanelHidden_.removeAll(path);
            else if (!partsPanelHidden_.contains(path)) partsPanelHidden_.append(path);
            refreshPartsPanel();
        });
    }
    if (paths.size() > 1) {
        menu->addSeparator();
        menu->addAction(QStringLiteral("All of them"), this, [this] {
            partsPanelHidden_.clear();
            refreshPartsPanel();
        });
    }
}

// WHAT A DOUBLE-CLICK ON A PART'S NAME PUTS DOWN: whatever the ticks under the shelf say. One drawing, two,
// or all of them - and they go down together as one set, side by side, the way the library's own Place does.
//
// It used to place "the mechanical drawing if it has one", which is right on an assembly sheet and wrong all
// day on a schematic; and a part with three drawings had no way of being asked for any but the one.
void MainWindow::placeFromPartsPanel(const QString& partId, bool hasMech) {
    if (!editor_) return;
    auto ticked = [this](int at) {
        return at < static_cast<int>(partsPanelPlaceBoxes_.size()) && partsPanelPlaceBoxes_[at] &&
               partsPanelPlaceBoxes_[at]->isChecked();
    };
    // Nothing ticked is not a request to place nothing - it is a switch somebody has emptied. The old
    // behaviour stands in for it rather than a shrug.
    if (!ticked(0) && !ticked(1) && !ticked(2) && !ticked(3)) {
        placeLibraryPart(partId, hasMech);
        return;
    }
    PartsCatalog catalog;
    catalog.openRemembered();
    const LibraryPart* part = catalog.byId(partId);
    if (!part) return;
    std::vector<Entity> all;
    QStringList placed, missing;
    double at = 0.0;
    auto addOne = [&](std::vector<Entity> one, const QString& name) {
        if (one.empty()) {
            missing << name;
            return;
        }
        Bounds b;
        for (const Entity& e : one) b.expand(e.bounds());
        if (b.valid) {
            const Vec2 shift{at - b.min.x, -(b.min.y + b.max.y) / 2.0};
            for (Entity& e : one) translateEntity(e, shift);
            at += b.width() + std::max(2.0, b.width() * 0.1);
        }
        for (Entity& e : one) all.push_back(std::move(e));
        placed << name;
    };
    if (ticked(0)) addOne(partBlockInstance(*part, /*mechanical=*/true, Vec2{0.0, 0.0}), tr("mechanical"));
    if (ticked(1)) addOne(partBlockInstance(*part, /*mechanical=*/false, Vec2{0.0, 0.0}), tr("schematic"));
    if (ticked(2)) {
        // Either side may be the one that was drawn, so each falls back to a mirror of the other: a part
        // whose back is the drawn side still puts a front down when a front is asked for.
        const QJsonArray front = part->fanout.isEmpty() ? part->fanoutOpposite : part->fanout;
        addOne(front.isEmpty() ? std::vector<Entity>{}
                               : partDrawingBlockInstance(*part, front, QStringLiteral("fanout"),
                                                          Vec2{0.0, 0.0}, part->fanout.isEmpty()),
               tr("fanout front"));
    }
    if (ticked(3)) {
        const QJsonArray other = part->fanoutOpposite.isEmpty() ? part->fanout : part->fanoutOpposite;
        addOne(other.isEmpty() ? std::vector<Entity>{}
                               : partDrawingBlockInstance(*part, other,
                                                          QStringLiteral("fanout (opposite)"),
                                                          Vec2{0.0, 0.0},
                                                          part->fanoutOpposite.isEmpty()),
               tr("fanout back"));
    }
    if (all.empty()) {
        statusBar()->showMessage(
            tr("%1 has no %2 drawing").arg(part->title(), missing.join(tr(" or "))), 5000);
        return;
    }
    const QString contactId = part->contactId();
    if (!contactId.isEmpty())
        for (Entity& e : all) e.meta.insert(QLatin1String(kContactPartKey), contactId);
    editor_->placeEntities(all);
    statusBar()->showMessage(
        missing.isEmpty()
            ? tr("Click the sheet to place %1 — %2").arg(part->title(), placed.join(tr(" + ")))
            : tr("Click the sheet to place %1 — %2  (nothing drawn for %3)")
                  .arg(part->title(), placed.join(tr(" + ")), missing.join(tr(", "))),
        6000);
}

// THE ORDER THE SHELF'S SECTIONS ARE IN, as somebody has arranged them. Kept as a list of kinds; any kind
// not in it (a new one, or one never dragged) follows in the program's own order, so the list never has to be
// complete and a new kind is never lost.
std::vector<PartKind> MainWindow::partsPanelKindOrder() const {
    QSettings s;
    const QStringList said = s.value(QStringLiteral("ui/partsPanelKindOrder")).toStringList();
    std::vector<PartKind> out;
    QSet<int> have;
    for (const QString& one : said) {
        bool ok = false;
        const int k = one.toInt(&ok);
        if (!ok || have.contains(k)) continue;
        for (PartKind kind : allPartKinds())
            if (static_cast<int>(kind) == k) {
                out.push_back(kind);
                have.insert(k);
            }
    }
    for (PartKind kind : allPartKinds())
        if (!have.contains(static_cast<int>(kind))) out.push_back(kind);
    return out;
}

// The order as the tree now shows it, written down. Taken from the tree AFTER the drop rather than worked out
// from the drop itself: what the eye sees is the answer, and there is only one place it can be read from.
void MainWindow::rememberPartsPanelKindOrder() {
    if (!partsPanelTree_) return;
    QStringList said;
    QSet<int> seen;
    std::function<void(QTreeWidgetItem*)> walk = [&](QTreeWidgetItem* it) {
        const QVariant kind = it->data(0, Qt::UserRole + 4);
        if (kind.isValid() && !seen.contains(kind.toInt())) {
            said << QString::number(kind.toInt());
            seen.insert(kind.toInt());
        }
        for (int i = 0; i < it->childCount(); ++i) walk(it->child(i));
    };
    for (int i = 0; i < partsPanelTree_->topLevelItemCount(); ++i)
        walk(partsPanelTree_->topLevelItem(i));
    if (said.isEmpty()) return;
    QSettings s;
    s.setValue(QStringLiteral("ui/partsPanelKindOrder"), said);
    s.sync();
    refreshPartsPanel();   // redrawn from the order, so the tree and the setting cannot disagree
}

void MainWindow::movePartsPanelKind(int kind, int beforeKind, bool after) {
    if (kind == beforeKind) return;
    std::vector<PartKind> order = partsPanelKindOrder();
    auto at = [&](int k) {
        return std::find_if(order.begin(), order.end(),
                            [k](PartKind p) { return static_cast<int>(p) == k; });
    };
    const auto moving = at(kind);
    if (moving == order.end()) return;
    const PartKind k = *moving;
    order.erase(moving);
    auto where = at(beforeKind);
    if (where != order.end() && after) ++where;
    order.insert(where, k);
    QStringList said;
    for (PartKind p : order) said << QString::number(static_cast<int>(p));
    QSettings s;
    s.setValue(QStringLiteral("ui/partsPanelKindOrder"), said);
    s.sync();
    refreshPartsPanel();
}

// THE SHELF'S SECTIONS, DRAGGED BY THEIR HEADINGS. Pressed on a heading and pulled, the heading is taken
// up; over the shelf, the gap it would go into - above or below the section under the cursor, whichever
// half of that section it is over - is marked with a line across the panel; let go, and it goes there.
// A part row counts as its section, so the whole of a section is somewhere to aim at, not only its title.
bool MainWindow::partsPanelDragEvent(QEvent* e) {
    static const QString kMime = QStringLiteral("application/x-edraw-partkind");
    QTreeWidget* tree = partsPanelTree_;
    auto kindOf = [](QTreeWidgetItem* it) {
        const QVariant v = it ? it->data(0, Qt::UserRole + 4) : QVariant();
        return v.isValid() ? v.toInt() : -1;
    };
    auto hideGuide = [this] {
        if (partsDropGuide_) partsDropGuide_->hide();
        partsDropKind_ = -1;
    };
    switch (e->type()) {
    case QEvent::MouseButtonPress: {
        auto* me = static_cast<QMouseEvent*>(e);
        partsDragKind_ = -1;
        if (me->button() == Qt::LeftButton) {
            partsDragKind_ = kindOf(tree->itemAt(me->position().toPoint()));
            partsDragPress_ = me->position().toPoint();
        }
        return false;   // the tree still opens and closes the heading as it always did
    }
    case QEvent::MouseMove: {
        auto* me = static_cast<QMouseEvent*>(e);
        if (partsDragKind_ < 0 || !(me->buttons() & Qt::LeftButton)) return false;
        if ((me->position().toPoint() - partsDragPress_).manhattanLength() <
            QApplication::startDragDistance())
            return false;
        const int kind = partsDragKind_;
        partsDragKind_ = -1;
        auto* drag = new QDrag(tree->viewport());
        auto* mime = new QMimeData;
        mime->setData(kMime, QByteArray::number(kind));
        drag->setMimeData(mime);
        if (QTreeWidgetItem* head = tree->itemAt(partsDragPress_)) {
            // What is being carried: the heading itself, as it looks on the shelf.
            const QRect r = tree->visualItemRect(head);
            drag->setPixmap(
                tree->viewport()->grab(QRect(0, r.top(), tree->viewport()->width(), r.height())));
            drag->setHotSpot(QPoint(partsDragPress_.x(), partsDragPress_.y() - r.top()));
        }
        drag->exec(Qt::MoveAction);
        hideGuide();
        return true;
    }
    case QEvent::MouseButtonRelease:
        partsDragKind_ = -1;
        return false;
    case QEvent::DragEnter:
    case QEvent::DragMove: {
        auto* de = static_cast<QDropEvent*>(e);
        if (!de->mimeData()->hasFormat(kMime)) return false;
        const QPoint pos = de->position().toPoint();
        QTreeWidgetItem* it = tree->itemAt(pos);
        // A part row aims at its section.
        if (it && kindOf(it) < 0 && kindOf(it->parent()) >= 0) it = it->parent();
        if (kindOf(it) < 0) {
            hideGuide();
            de->acceptProposedAction();   // still over the shelf: let go here and nothing moves
            return true;
        }
        // The section's whole height: its heading and, opened, every row under it.
        QRect block = tree->visualItemRect(it);
        if (it->isExpanded() && it->childCount() > 0)
            block = block.united(tree->visualItemRect(it->child(it->childCount() - 1)));
        partsDropAfter_ = pos.y() > block.center().y();
        partsDropKind_ = kindOf(it);
        const int y = partsDropAfter_ ? block.bottom() + 1 : block.top();
        partsDropGuide_->setGeometry(2, std::max(0, y - 1), tree->viewport()->width() - 4, 3);
        partsDropGuide_->show();
        partsDropGuide_->raise();
        de->acceptProposedAction();
        return true;
    }
    case QEvent::DragLeave:
        hideGuide();
        return false;
    case QEvent::Drop: {
        auto* de = static_cast<QDropEvent*>(e);
        if (!de->mimeData()->hasFormat(kMime)) return false;
        const int kind = de->mimeData()->data(kMime).toInt();
        const int target = partsDropKind_;
        const bool after = partsDropAfter_;
        hideGuide();
        de->acceptProposedAction();
        // Queued: the shelf is rebuilt by the move, and the tree is still in the middle of this drop.
        if (target >= 0 && target != kind)
            QTimer::singleShot(0, this, [this, kind, target, after] {
                movePartsPanelKind(kind, target, after);
            });
        return true;
    }
    default:
        return false;
    }
}

QString MainWindow::partsPanelHeadingKey(const QTreeWidgetItem* heading) {
    if (!heading) return QString();
    const QVariant kind = heading->data(0, Qt::UserRole + 4);
    const QVariant lib = heading->data(0, Qt::UserRole + 5);
    if (kind.isValid()) {
        const QString under =
            heading->parent() ? heading->parent()->data(0, Qt::UserRole + 5).toString() : QString();
        return under + QStringLiteral("#") + QString::number(kind.toInt());
    }
    if (lib.isValid()) return QStringLiteral("lib:") + lib.toString();
    return QString();   // a part row: not a heading
}

bool MainWindow::partsPanelHeadingCollapsed(const QString& key) const {
    return QSettings().value(QStringLiteral("ui/partsPanelCollapsed")).toStringList().contains(key);
}

void MainWindow::rememberPartsPanelHeading(QTreeWidgetItem* heading, bool collapsed) {
    if (partsPanelFilling_) return;
    const QString key = partsPanelHeadingKey(heading);
    if (key.isEmpty()) return;
    QSettings s;
    QStringList rolled = s.value(QStringLiteral("ui/partsPanelCollapsed")).toStringList();
    rolled.removeAll(key);
    if (collapsed) rolled << key;
    s.setValue(QStringLiteral("ui/partsPanelCollapsed"), rolled);
}

void MainWindow::refreshPartsPanel() {
    if (!partsPanelTree_) return;
    // Rebuilding opens every heading as it is made; none of that is somebody rolling one up or down.
    struct Filling {
        bool& flag;
        explicit Filling(bool& f) : flag(f) { flag = true; }
        ~Filling() { flag = false; }
    } filling(partsPanelFilling_);
    const QStringList rolledUp =
        QSettings().value(QStringLiteral("ui/partsPanelCollapsed")).toStringList();
    partsPanelTree_->clear();
    syncBomRuleBoxes();
    PartsCatalog catalog;
    catalog.openRemembered();
    const QString needle = partsPanelFilter_ ? partsPanelFilter_->text().trimmed() : QString();

    // EVERY part, not only the ones with a drawing. The panel used to skip a part that had neither a
    // mechanical drawing nor a symbol, which is most of a real library - a heat-shrink size, a contact, a
    // length of wire - so a shelf of a hundred parts showed three, and looked broken rather than filtered.
    // A part with no drawing is shown greyed and opens in the library, where one can be made for it.
    auto matches = [&needle](const LibraryPart& part) {
        if (needle.isEmpty()) return true;
        const QString hay = QStringLiteral("%1 %2 %3 %4 %5")
                                .arg(part.mfrPn, part.internalPn, part.description, part.mfrName,
                                     partKindName(part.kind));
        return hay.contains(needle, Qt::CaseInsensitive);
    };

    QStringList shownFrom = catalog.paths();
    for (const QString& hidden : partsPanelHidden_) shownFrom.removeAll(hidden);
    int shown = 0, connected = 0;

    // WHICH KINDS THERE ARE TO SHOW, and which one is being asked for. A real library is mostly heat-shrink
    // sizes and marker sleeves - a hundred and twenty rows of which two are the connectors somebody wants -
    // so the shelf is grouped by what a part IS, and one kind can be asked for on its own. Only the kinds
    // with something in them are offered: a menu of twenty-six where the library holds six is a menu you
    // read rather than use.
    QSet<PartKind> kindsPresent;
    for (const QString& path : catalog.paths()) {
        const PartsLibrary* lib = catalog.byPath(path);
        if (!lib || !shownFrom.contains(path)) continue;
        for (const LibraryPart& part : lib->parts) kindsPresent.insert(part.kind);
    }
    if (catalog.paths().isEmpty())
        for (const LibraryPart& part : catalog.merged().parts) kindsPresent.insert(part.kind);
    int wantKind = -1;   // -1 = every kind
    if (partsPanelKindBox_) {
        const QVariant was = partsPanelKindBox_->currentData();
        partsPanelSyncing_ = true;
        partsPanelKindBox_->clear();
        partsPanelKindBox_->addItem(tr("All kinds"), -1);
        std::vector<QPair<QString, int>> offer;
        for (PartKind k : allPartKinds())
            if (kindsPresent.contains(k))
                offer.push_back({partKindName(k), static_cast<int>(k)});
        std::sort(offer.begin(), offer.end(),
                  [](const QPair<QString, int>& a, const QPair<QString, int>& b) {
                      return a.first.localeAwareCompare(b.first) < 0;
                  });
        for (const auto& one : offer) partsPanelKindBox_->addItem(one.first, one.second);
        const int at = was.isValid() ? partsPanelKindBox_->findData(was) : 0;
        partsPanelKindBox_->setCurrentIndex(at >= 0 ? at : 0);
        partsPanelSyncing_ = false;
        wantKind = partsPanelKindBox_->currentData().toInt();
    }
    auto wanted = [wantKind](const LibraryPart& part) {
        return wantKind < 0 || static_cast<int>(part.kind) == wantKind;
    };
    const bool severalInView = shownFrom.size() > 1;

    auto addPart = [this, &shown](const LibraryPart& part, QTreeWidgetItem* under) {
        const bool mech = part.hasMechanical();
        const bool schem = !partSchematicDrawing(part).isEmpty();
        const bool fan = part.hasFanout();
        auto* item = under ? new QTreeWidgetItem(under) : new QTreeWidgetItem(partsPanelTree_);
        item->setText(0, QStringLiteral("%1 — %2").arg(partKindName(part.kind), part.title()));
        item->setData(0, Qt::UserRole, part.id);
        item->setData(0, Qt::UserRole + 1, mech);           // the mechanical drawing where there is one
        item->setData(0, Qt::UserRole + 2, mech || schem);   // is there anything to put down at all
        item->setData(0, Qt::UserRole + 3, fan);
        // A part belongs to its kind: it is picked and placed, never dragged out of its section.
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        // A column each, ticked: what the part IS, read down the columns rather than opened part by part.
        // A tick and nothing - not a tick and a cross - because the eye counts ticks in a column and stops
        // at the gaps.
        const bool has[3] = {mech, schem, fan};
        for (int c = 0; c < 3; ++c) {
            item->setText(c + 1, has[c] ? QString(QChar(0x2713)) : QString());
            item->setTextAlignment(c + 1, Qt::AlignCenter);
            item->setForeground(c + 1, has[c] ? Chrome::ink() : Chrome::subInk());
            QString cellTip = has[c] ? tr("Has a %1 drawing - double-click to place it")
                                           .arg(kPartDrawingNames[c])
                                     : tr("No %1 drawing").arg(kPartDrawingNames[c]);
            if (c == 2 && !fan && canHaveFanout(part))
                cellTip = tr("No fanout drawn - double-click to place one worked out from its %1 way(s)")
                              .arg(part.wayCount());
            item->setToolTip(c + 1, cellTip);
        }
        QStringList drawings;
        if (mech) drawings << tr("mechanical");
        if (schem) drawings << tr("schematic");
        if (fan) {
            // WHICH SIDES ARE DRAWN, not just "it has a fanout": a part may have the front, the back or
            // both, and which of them exists is the thing worth knowing before placing one.
            QStringList sides;
            if (!part.fanout.isEmpty()) sides << tr("front");
            if (!part.fanoutOpposite.isEmpty()) sides << tr("back");
            drawings << tr("fanout (%1)").arg(sides.join(tr(" and ")));
        }
        if (!drawings.isEmpty())
            item->setToolTip(0,
                             tr("%1\nDrawings: %2\nDouble-click to place the %3 — right-click for the rest")
                                 .arg(part.title(), drawings.join(QStringLiteral(", ")),
                                      mech ? tr("mechanical drawing") : tr("schematic symbol")));
        else {
            item->setToolTip(0, tr("%1\nNo drawing of any kind yet — double-click to open it in the library")
                                    .arg(part.title()));
            item->setForeground(0, Chrome::subInk());
        }
        ++shown;
    };

    // The parts of one shelf, under a heading per KIND: "Connector", "Heatshrink", each with its own
    // parts beneath it and collapsible, so the two connectors in a library of a hundred and twenty are
    // found by opening one heading rather than by scrolling past the sleeves.
    auto addByKind = [this, &addPart, &wanted, &matches, &rolledUp](const std::vector<LibraryPart>& parts,
                                                                   QTreeWidgetItem* under) {
        for (PartKind k : partsPanelKindOrder()) {
            std::vector<const LibraryPart*> ofKind;
            for (const LibraryPart& part : parts)
                if (part.kind == k && wanted(part) && matches(part)) ofKind.push_back(&part);
            if (ofKind.empty()) continue;
            QTreeWidgetItem* head = under ? new QTreeWidgetItem(under)
                                          : new QTreeWidgetItem(partsPanelTree_);
            // Which kind this heading is (so the order can be read back off the tree), and draggable -
            // headings are what moves; the parts under them go with their own kind.
            head->setData(0, Qt::UserRole + 4, static_cast<int>(k));
            head->setFlags(Qt::ItemIsEnabled | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
            head->setText(0, QStringLiteral("%1  (%2)").arg(partKindName(k)).arg(ofKind.size()));
            QFont f = head->font(0);
            f.setBold(true);
            head->setFont(0, f);
            head->setForeground(0, Chrome::subInk());
            head->setFirstColumnSpanned(false);
            for (const LibraryPart* part : ofKind) addPart(*part, head);
            head->setExpanded(!rolledUp.contains(partsPanelHeadingKey(head)));
        }
    };

    if (catalog.paths().isEmpty()) {
        // Nothing connected by name: whatever the catalogue has, as one shelf.
        const PartsLibrary lib = catalog.merged();
        connected = static_cast<int>(lib.parts.size());
        addByKind(lib.parts, nullptr);
    } else {
        for (const QString& path : catalog.paths()) {
            const PartsLibrary* lib = catalog.byPath(path);
            if (!lib) continue;
            connected += static_cast<int>(lib->parts.size());
            if (!shownFrom.contains(path)) continue;
            QTreeWidgetItem* head = nullptr;
            if (severalInView) {
                // Which shelf this is, with its parts under it. It is a heading, not a part: it carries no
                // part id, and everything that acts on a part asks for one.
                head = new QTreeWidgetItem(partsPanelTree_);
                head->setData(0, Qt::UserRole + 5, path);   // which library, for remembering it rolled up
                head->setText(0, QFileInfo(path).completeBaseName().toUpper());
                QFont f = head->font(0);
                f.setBold(true);
                head->setFont(0, f);
                head->setForeground(0, Chrome::subInk());
                head->setFirstColumnSpanned(false);
                head->setExpanded(!rolledUp.contains(partsPanelHeadingKey(head)));
            }
            addByKind(lib->parts, head);
        }
        partsPanelTree_->setRootIsDecorated(true);   // the kind headings open and close
    }
    if (partsPanelCount_)
        partsPanelCount_->setText(
            shownFrom.size() == catalog.paths().size()
                ? tr("%1 of %2 parts").arg(shown).arg(connected)
                : tr("%1 of %2 parts — %3 of %4 libraries")
                      .arg(shown)
                      .arg(connected)
                      .arg(shownFrom.size())
                      .arg(catalog.paths().size()));
}

QDockWidget* MainWindow::makeBlocksPanel() {
    auto* dock = new AutoHideDock(QStringLiteral("BLOCKS"), this);
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                      QDockWidget::DockWidgetClosable);
    auto* body = new QWidget(dock);
    auto* v = new QVBoxLayout(body);
    v->setContentsMargins(4, 4, 4, 4);
    v->setSpacing(4);
    blocksPanelList_ = new QListWidget(body);
    blocksPanelList_->setToolTip(
        QStringLiteral("Double-click to put a block on the sheet, then click where it goes."));
    connect(blocksPanelList_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (!item || !editor_) return;
        const QString name = item->data(Qt::UserRole).toString();
        PartsCatalog catalog;
        catalog.openRemembered();
        for (const BlockDef& block : allBlocks(&catalog)) {
            if (block.name != name) continue;
            editor_->placeEntities(blockInstanceEntities(block, Vec2{0.0, 0.0}, QString()));
            statusBar()->showMessage(tr("Click the sheet to place %1").arg(block.name), 6000);
            if (canvas_) canvas_->setFocus();
            break;
        }
    });
    v->addWidget(blocksPanelList_, 1);
    {
        auto* open = new QPushButton(QStringLiteral("Open the blocks window"), body);
        letSqueeze(open);
        connect(open, &QPushButton::clicked, this, [this] { showBlockDialog(); });
        v->addWidget(open);
    }
    dock->setManaged(new DockTitleBar(dock, QStringLiteral("BLOCKS")), body);
    addDockWidget(Qt::LeftDockWidgetArea, dock);
    dock->hide();
    refreshBlocksPanel();
    return dock;
}

QDockWidget* MainWindow::makeCalculatorPanel() {
    auto* dock = new AutoHideDock(QStringLiteral("CALCULATOR"), this);
    dock->setAllowedAreas(Qt::AllDockWidgetAreas);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                      QDockWidget::DockWidgetClosable);
    auto* calc = new ScientificCalculatorDialog(dock);
    dock->setManaged(new DockTitleBar(dock, QStringLiteral("CALCULATOR")), calc);
    // Off to begin with: it is a tool to reach for, not another panel taking room from a drawing
    // that may not need it. The Tools button and the View tab bring it back.
    addDockWidget(Qt::RightDockWidgetArea, dock);
    if (layersDock_) splitDockWidget(layersDock_, dock, Qt::Vertical);
    dock->hide();
    return dock;
}

void MainWindow::refreshBlocksPanel() {
    if (!blocksPanelList_) return;
    blocksPanelList_->clear();
    PartsCatalog catalog;
    catalog.openRemembered();
    for (const BlockDef& block : allBlocks(&catalog)) {
        auto* item = new QListWidgetItem(block.name, blocksPanelList_);
        item->setData(Qt::UserRole, block.name);
        if (!block.description.isEmpty()) item->setToolTip(block.description);
    }
}

void MainWindow::updateActiveTabText() {
    // The strip shows the same fact the tab's asterisk does - whether there is unsaved work - so it
    // is brought up to date wherever that is.
    refreshQuickAccess();
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
    Document* doc = docs_[index].get();
    if (!doc->dirty()) return true;
    const bool modelEdit = mechModelEdits_.contains(doc);
    const bool libraryPartEdit = libraryPartEdits_.contains(doc);
    const QString name = modelEdit ? mechModelName(mechModelEdits_.value(doc))
                          : libraryPartEdit
                              ? (libraryDlg_ ? [&] {
                                    LibraryPart* p = libraryDlg_->partForSession(libraryPartEdits_.value(doc));
                                    return p ? p->title() : libraryPartEdits_.value(doc);
                                }()
                                              : libraryPartEdits_.value(doc))
                              : (docPaths_[index].isEmpty() ? doc->name()
                                                            : QFileInfo(docPaths_[index]).fileName());
    const auto choice = QMessageBox::warning(
        this, (modelEdit || libraryPartEdit) ? QStringLiteral("Save model changes?") : QStringLiteral("Save changes?"),
        (modelEdit || libraryPartEdit)
            ? QStringLiteral("The %1 model has unsaved changes. Save before closing?").arg(name)
            : QStringLiteral("\"%1\" has unsaved changes. Save before closing?").arg(name),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (choice == QMessageBox::Cancel) return false;
    if (choice == QMessageBox::Discard) return true;
    if (modelEdit) return saveMechModelDocument(doc);
    if (libraryPartEdit) return saveLibraryPartDocument(doc);
    return saveDocument(index);
}

bool MainWindow::saveDocument(int index, bool forceDialog) {
    if (index < 0 || index >= static_cast<int>(docs_.size())) return false;
    flushPaperSpace();   // what is on the sheet being drawn on belongs in the file
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

int MainWindow::saveAllDocuments() {
    int saved = 0, alreadyClean = 0;
    for (int i = 0; i < static_cast<int>(docs_.size()); ++i) {
        if (!docs_[static_cast<size_t>(i)]) continue;
        // ONLY WHAT HAS CHANGED. Rewriting a file nobody has touched changes its timestamp, which is
        // one of the things a drawing office reads to answer "what moved since the release?" - so a
        // clean drawing is left exactly as it is, and counted so that can be said out loud.
        if (!docs_[static_cast<size_t>(i)]->dirty()) {
            ++alreadyClean;
            continue;
        }
        // CANCEL ON THE NAME DIALOGUE STOPS THE WHOLE THING. A drawing that has never been saved is
        // asked for a name like any other; answering Cancel to that and then being shown the next
        // file dialogue is the program arguing. There is no reading of Cancel that means "ask me
        // again about a different drawing".
        if (!saveDocument(i, /*forceDialog=*/false)) break;
        ++saved;
    }
    refreshQuickAccess();
    // Said plainly, the empty case included: a button that appears to do nothing is a button
    // somebody presses again.
    if (saved == 0)
        statusBar()->showMessage(alreadyClean > 0
                                     ? tr("Nothing to save — every open drawing is already saved")
                                     : tr("Nothing to save"),
                                 4000);
    else
        statusBar()->showMessage(tr("Saved %1 drawing(s)").arg(saved), 4000);
    return saved;
}

int MainWindow::dirtyDocumentCountForTest() const {
    int dirty = 0;
    for (const auto& d : docs_)
        if (d && d->dirty()) ++dirty;
    return dirty;
}

void MainWindow::refreshQuickAccess() {
    if (!quickSaveAllBtn_) return;
    // Live only while there is something to write. Greyed, it says "everything is saved" - which is
    // the question somebody glances at that corner of the window to answer.
    int dirty = 0;
    for (const auto& d : docs_)
        if (d && d->dirty()) ++dirty;
    quickSaveAllBtn_->setEnabled(dirty > 0);
    quickSaveAllBtn_->setToolTip(
        dirty == 0 ? tr("Save every open drawing  (Ctrl+Shift+S) — all of them are saved")
                   : tr("Save every open drawing  (Ctrl+Shift+S) — %1 unsaved").arg(dirty));
}

void MainWindow::closeDocument(int index) {
    if (index < 0 || index >= static_cast<int>(docs_.size())) return;
    if (!maybeSaveDoc(index)) return; // user cancelled
    {
        QSignalBlocker block(docTabs_);
        // Forget any block-edit session on this document BEFORE it is destroyed: a stale pointer
        // left in the map would match the next document allocated at the same address.
        blockEdits_.remove(docs_[index].get());
        templateEdits_.remove(docs_[index].get());
        templateEditPaths_.remove(docs_[index].get());
        // How the model was being looked at, before the window that was looking at it goes. Closed by the tab's X
        // rather than by Capture or Cancel, this is the only place that hears about it.
        rememberMechModelView(docs_[index].get());
        mechModelEdits_.remove(docs_[index].get());
        mechModelSessionModels_.remove(docs_[index].get());
        mechModelEditSchematic_.remove(docs_[index].get());
        libraryPartEdits_.remove(docs_[index].get());
        libraryPartEditKind_.remove(docs_[index].get());
        libraryPartSessionModels_.remove(docs_[index].get());
        // And its paper spaces, for the same reason: the next document allocated at that address must
        // not inherit the sheets of this one.
        forgetPaperSpaces(docs_[index].get());
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
    savePanelLayout();
    e->accept();
}

void MainWindow::showEvent(QShowEvent* e) {
    QMainWindow::showEvent(e);
    // The saved widths can only be given to the panels once there is a layout to give them to: resizing a
    // dock before the window is on screen does nothing at all. Once, on the way up.
    if (!panelSizesApplied_) {
        panelSizesApplied_ = true;
        // Twice: a dock resized before the window's layout has been through a pass of its own can have the
        // size taken back off it again, so it is given once as soon as possible and once when the window
        // has certainly settled.
        QTimer::singleShot(0, this, [this] { applyPanelSizes(); });
        QTimer::singleShot(120, this, [this] { applyPanelSizes(); });
    }
}

// ---- The panels between sessions -------------------------------------------
//
// A side is pulled to the width the work needs - a bill of materials with part numbers in it wants more
// room than the layers list - and that width is a decision, not an accident. It survives folding an
// unpinned side away and hovering it open again, and it survives shutting the program: what comes back is
// the arrangement that was left. The panels ARE the tool; setting them out again every morning is not work.

void MainWindow::savePanelLayout() {
    QSettings s;
    // Which panel is on which side, in what order, and which of them are open at all. Keyed by the
    // objectName each panel was given.
    s.setValue(QStringLiteral("panels/state"), saveState());
    // The widths are kept separately rather than left to the saved state, because a folded panel is 24 px
    // wide and that is what the state would remember - the strip, not the panel.
    for (AutoHideDock* d : AutoHideDock::all()) {
        if (!d || d->owner() != this) continue;
        const QString key = QStringLiteral("panels/") + d->objectName() + QStringLiteral("/");
        if (const int extent = d->extentForSaving(); extent > 0) {
            s.setValue(key + QStringLiteral("extent"), extent);
            s.setValue(key + QStringLiteral("extentIsWidth"), d->extentIsWidth());
        }
        s.setValue(key + QStringLiteral("shown"), !d->isHidden());
    }
    s.setValue(QStringLiteral("panels/pinned"), AutoHideDock::pinnedSnapshot());
}

void MainWindow::restorePanelLayout() {
    QSettings s;
    // The arrangement first: it decides which side each panel is on, and a width belongs to a SIDE - so
    // there is no telling which width is a panel's until it is standing where it was left.
    const QByteArray state = s.value(QStringLiteral("panels/state")).toByteArray();
    if (!state.isEmpty()) restoreState(state);
    // Then the widths, one to a side: the widest of what the panels standing on it remember. Panels on one
    // side opening at three different widths is not an arrangement, and whichever was read last winning is
    // not a rule anybody could predict.
    QHash<int, int> bySide;
    QHash<int, bool> asWidth;
    for (AutoHideDock* d : AutoHideDock::all()) {
        if (!d || d->owner() != this) continue;
        const QString key = QStringLiteral("panels/") + d->objectName() + QStringLiteral("/");
        const int extent = s.value(key + QStringLiteral("extent"), 0).toInt();
        if (extent <= 0) continue;
        const int side = d->sideKey();
        bySide.insert(side, std::max(bySide.value(side, 0), extent));
        asWidth.insert(side, s.value(key + QStringLiteral("extentIsWidth"), true).toBool());
    }
    for (AutoHideDock* d : AutoHideDock::all()) {
        if (!d || d->owner() != this) continue;
        const int side = d->sideKey();
        if (bySide.contains(side))
            d->setRememberedExtent(bySide.value(side), asWidth.value(side, true));
    }
    // restoreState() settles what is open and what is not, but only for panels it knows about - a panel
    // added since the arrangement was saved is not in it, so each is told again.
    for (AutoHideDock* d : AutoHideDock::all()) {
        if (!d || d->owner() != this) continue;
        const QString key = QStringLiteral("panels/") + d->objectName() + QStringLiteral("/shown");
        if (s.contains(key)) d->setVisible(s.value(key).toBool());
    }
    // Then which sides are folded away. A side left unpinned comes back unpinned, as a strip - and opens
    // to the width it was left at, because that is what was read back above.
    AutoHideDock::restorePinned(s.value(QStringLiteral("panels/pinned")).toStringList());
    AutoHideDock::refreshPinButtons();
    syncViewPanelButtons();   // the View tab says what is actually on the screen
}

void MainWindow::applyPanelSizes() {
    // ONE width a side. The panels on a side share their width, so handing each its own number in turn
    // means the last one heard wins - and it was a panel the user had never even opened that won, which is
    // why a side pulled wider came back narrow. The widest number remembered on a side is the one given.
    QHash<int, AutoHideDock*> perSide;
    for (AutoHideDock* d : AutoHideDock::all()) {
        if (!d || d->owner() != this || d->isHidden() || d->isFloating()) continue;
        if (d->rememberedExtent() <= 0) continue;
        AutoHideDock* cur = perSide.value(d->sideKey(), nullptr);
        if (!cur || d->rememberedExtent() > cur->rememberedExtent()) perSide.insert(d->sideKey(), d);
    }
    for (AutoHideDock* d : perSide) d->applyRememberedExtent();
    AutoHideDock::refreshPinButtons();
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
    editor_->setFaceFanScale(s.value(QStringLiteral("draw/faceFanScale"), editor_->faceFanScale()).toDouble());
    editor_->setFaceCableScale(
        s.value(QStringLiteral("draw/faceCableScale"), editor_->faceCableScale()).toDouble());
    // Measurement / cross-section units.
    EntityRenderer::setLengthUnitInch(s.value(QStringLiteral("units/inch"), false).toBool());
    EntityRenderer::setDualUnits(s.value(QStringLiteral("units/dual"), false).toBool());
    crossSectionAwg_ = s.value(QStringLiteral("units/awg"), false).toBool();
    setWireGaugeNotationAwg(crossSectionAwg_); // restored before anything is drawn
    // Which scheme the chrome is in. Read here and applied at the end of building, once there is a ribbon
    // and there are panels to colour.
    darkTheme_ = s.value(QStringLiteral("ui/darkChrome"), true).toBool();
    Chrome::setDark(darkTheme_);
    partsPanelHidden_ = s.value(QStringLiteral("panels/partsHidden")).toStringList();
    customWireColors_ = s.value(QStringLiteral("wire/customColors")).toStringList();
    insulationMaterials_ = s.value(QStringLiteral("wire/insulationMaterials")).toStringList();
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
    s.setValue(QStringLiteral("draw/faceFanScale"), editor_->faceFanScale());
    s.setValue(QStringLiteral("draw/faceCableScale"), editor_->faceCableScale());
    s.setValue(QStringLiteral("units/inch"), EntityRenderer::lengthUnitInch());
    s.setValue(QStringLiteral("units/dual"), EntityRenderer::dualUnits());
    s.setValue(QStringLiteral("units/awg"), crossSectionAwg_);
    s.setValue(QStringLiteral("wire/customColors"), customWireColors_);
    s.setValue(QStringLiteral("wire/insulationMaterials"), insulationMaterials_);
    s.setValue(QStringLiteral("ui/darkChrome"), darkTheme_);
    s.setValue(QStringLiteral("panels/partsHidden"), partsPanelHidden_);
}

// ---- File actions ----------------------------------------------------------

void MainWindow::newDrawing() {
    Document* doc = addDocument(std::make_unique<Document>(
                                    QStringLiteral("Drawing%1").arg(docs_.size() + 1)),
                                QString());
    // A new drawing starts from the default template, when one has been set: that is what makes it the
    // house sheet rather than just a file in a folder.
    const QString def = TemplateDialog::defaultTemplatePath();
    if (doc && !def.isEmpty()) {
        DrawingTemplate tpl;
        if (tpl.load(def)) {
            applyTemplate(tpl, *doc);
            setWireGaugeNotationAwg(tpl.gaugeAwg);
            doc->setDirty(false);   // a fresh sheet, not unsaved work
            refreshPanels();
            if (canvas_) canvas_->update();
            statusBar()->showMessage(
                QStringLiteral("New drawing from the template %1").arg(tpl.name), 4000);
            return;
        }
    }
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

void MainWindow::importPdf() {
    if (!doc_) return;
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Import PDF into drawing"), QString(), QStringLiteral("PDF (*.pdf)"));
    if (path.isEmpty()) return;
    importPdfFile(path);
}

bool MainWindow::importPdfFile(const QString& path, QString* said) {
    if (!doc_) return false;
    // HOW FAR IT HAS GOT, at the bottom of the window. A big PDF is tens of thousands of lines and
    // takes a while to read; without a sign of it the window looks like it has stopped.
    auto* bar = new QProgressBar(this);
    bar->setObjectName(QStringLiteral("importProgress"));
    bar->setRange(0, 100);
    bar->setMaximumWidth(260);
    bar->setTextVisible(true);
    auto* stage = new QLabel(this);
    stage->setObjectName(QStringLiteral("importStage"));
    statusBar()->addPermanentWidget(stage);
    statusBar()->addPermanentWidget(bar);
    const auto done = qScopeGuard([this, bar, stage] {
        // Gone the moment the import is over, whichever way it ends.
        statusBar()->removeWidget(bar);
        statusBar()->removeWidget(stage);
        delete bar;
        delete stage;
    });
    auto step = [bar, stage](int pct, const QString& what) {
        bar->setValue(std::clamp(pct, 0, 100));
        stage->setText(what);
        // Drawn now, but nothing typed or clicked is acted on while the import is under way.
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    };
    step(0, tr("Importing %1").arg(QFileInfo(path).fileName()));
    Document imported;
    QString error;
    // The reading is the first 60% of the bar; laying it out as a block is the rest.
    const pdf::Progress reading = [&step](int pct, const QString& what) { step(pct * 60 / 100, what); };
    if (!pdf::importFile(imported, path, &error, reading)) {
        if (said) *said = error;
        else QMessageBox::warning(this, QStringLiteral("PDF import failed"), error);
        return false;
    }
    if (imported.entities().empty()) {
        const QString nothing = QStringLiteral("Nothing was found to import: that PDF has no vector "
                                               "geometry in it (a scan is a picture, not lines).");
        if (said) *said = nothing;
        else QMessageBox::information(this, QStringLiteral("Import PDF"), nothing);
        return false;
    }

    // Laid on the origin: what comes out of a PDF carries whatever coordinates the page had - a
    // corner at 400, 600 on one file and at 0, 842 on the next - and dropping that into a drawing
    // puts it anywhere but where it is wanted. The bottom-left corner of what was imported goes on
    // 0,0, so it lands somewhere known and can be moved from there.
    Bounds ext;
    for (const Entity& e : imported.entities()) {
        const Bounds b = e.bounds();
        if (b.valid) {
            ext.expand(b.min);
            ext.expand(b.max);
        }
    }
    // Every path a LINE at a time. A PDF draws a whole outline - a title block's frame, a border, a table -
    // as one path, and one path is one object: inside the block it could not be worked on, only replaced. A
    // segment each is what makes an imported PDF editable, which is the whole reason for importing it as a
    // block rather than as a picture. Nothing about how it LOOKS changes: a polyline and its segments draw
    // the same lines.
    step(62, tr("Splitting %1 path(s) into lines").arg(imported.entities().size()));
    std::vector<Entity> placed;
    placed.reserve(imported.entities().size() * 2);
    int fromPaths = 0;
    for (Entity e : imported.entities()) {
        if (ext.valid) translateEntity(e, Vec2{-ext.min.x, -ext.min.y});
        if (e.type == EntityType::Polyline && e.points.size() >= 2) {
            ++fromPaths;
            for (size_t i = 0; i + 1 < e.points.size(); ++i) {
                Entity seg = e;
                seg.id.clear();
                seg.type = EntityType::Line;
                seg.a = e.points[i];
                seg.b = e.points[i + 1];
                seg.points.clear();
                placed.push_back(std::move(seg));
            }
            continue;
        }
        placed.push_back(e);
    }

    // And converted to DXF beside the PDF, as the ask was: the drawing it becomes is a DXF, and having
    // it as a file means it can be opened, sent on, or imported again without the PDF being read a
    // second time. The import itself uses what was parsed rather than reading the DXF back, so nothing
    // is lost in the round trip.
    QString dxfPath = path;
    if (dxfPath.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive))
        dxfPath.chop(4);
    dxfPath += QStringLiteral(".dxf");
    step(68, tr("Writing %1").arg(QFileInfo(dxfPath).fileName()));
    Document asDxf;
    asDxf.entities().clear();
    for (const Entity& e : placed) asDxf.entities().push_back(e);
    QString dxfError;
    const bool wroteDxf = dxf::exportFile(asDxf, dxfPath, &dxfError);

    // As a BLOCK, and one that can be edited. A PDF comes in as hundreds or thousands of little lines -
    // a title block alone is a few hundred - and as loose entities they are impossible to move as a unit,
    // impossible to pick without catching the drawing under them, and impossible to change all at once.
    // As one block it is one object on the sheet, insertable again as often as it is wanted, and openable
    // on its own tab (Blocks, or right-click it) where the lines can be worked on and saved back to every
    // copy at once.
    step(76, tr("Making the block of %1 line(s)").arg(placed.size()));
    const QString wanted = blockNameForImport(QFileInfo(path).completeBaseName());
    BlockDef def = blockFromEntities(wanted, placed, Vec2{0.0, 0.0});
    def.description = QStringLiteral("Imported from %1").arg(QFileInfo(path).fileName());
    step(84, tr("Saving the block"));
    const bool savedBlock = def.save(BlockDef::pathFor(wanted));
    step(90, tr("Putting it on the drawing"));
    if (savedBlock) {
        // ONE OBJECT on the origin - where the entities themselves would have gone: a reference that
        // draws the block out of its file. Laid down as the block's own entities it was tens of
        // thousands of objects, and picking any one of them had every panel work through the whole set
        // to find the rest of the copy - the window stopped for minutes. Not left picked either.
        editor_->appendEntities({blockRefEntity(def, Vec2{0.0, 0.0})}, false);
        if (blockDlg_) blockDlg_->refresh();
        refreshBlocksPanel();
    } else {
        // The block folder could not be written. Rather than lose the import, the lines go down as they
        // are and the message says which of the two happened.
        editor_->appendEntities(placed, false);
    }
    step(97, tr("Drawing it"));
    canvas_->fitView();
    QStringList told;
    told << (savedBlock ? tr("Imported %1 line(s) from %2 as the block \"%3\", one copy on 0,0 — "
                             "double-click it to edit them")
                              .arg(placed.size())
                              .arg(QFileInfo(path).fileName())
                              .arg(wanted)
                        : tr("Imported %1 object(s) from %2 onto 0,0 (a block could not be saved, so the "
                             "lines went down as they are)")
                              .arg(placed.size())
                              .arg(QFileInfo(path).fileName()));
    told << (wroteDxf ? tr("also written as %1").arg(QFileInfo(dxfPath).fileName())
                      : tr("the DXF could not be written: %1").arg(dxfError));
    const QString message = told.join(QStringLiteral(" — "));
    statusBar()->showMessage(message, 10000);
    if (said) *said = message;
    return true;
}

// A name for the block a PDF becomes: the file's own, and if that name is taken, the next number after it.
// Overwriting a block somebody already has - and every copy of it already on a sheet - because a second PDF
// happened to have the same file name is not an import, it is a loss.
QString MainWindow::blockNameForImport(const QString& base) const {
    QString stem = base.trimmed();
    if (stem.isEmpty()) stem = QStringLiteral("PDF");
    PartsCatalog catalog;
    catalog.openRemembered();
    QSet<QString> taken;
    for (const BlockDef& b : allBlocks(&catalog)) taken.insert(b.name);
    if (!taken.contains(stem)) return stem;
    for (int n = 2; n < 1000; ++n) {
        const QString tryThis = QStringLiteral("%1 (%2)").arg(stem).arg(n);
        if (!taken.contains(tryThis)) return tryThis;
    }
    return stem;
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

// IMPORT ANOTHER DRAWING INTO THIS ONE.
//
// Not the same gesture as Open, and the difference is the whole point: Open replaces what is on the
// screen, Import adds to it. A harness sheet being built from a customer's outline wants the second.
//
// DWG cannot be read directly - it is Autodesk's binary format - so it goes through the ODA converter
// to DXF first and then follows exactly the same path as a DXF. One merge, not two.
void MainWindow::importDrawing() {
    if (!doc_) return;
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Import drawing into this one"), QString(),
        QStringLiteral("Drawings (*.dwg *.dxf);;DWG (*.dwg);;DXF (*.dxf);;All files (*)"));
    if (path.isEmpty()) return;
    importDrawingFile(path);
}

void MainWindow::importDrawingFile(const QString& path) {
    // Empty is what a cancelled file dialog gives back, and the menu entries hand their result
    // straight here rather than checking first. Cancelling is not an error and says nothing.
    if (!doc_ || path.isEmpty()) return;

    if (!path.endsWith(QStringLiteral(".dwg"), Qt::CaseInsensitive)) {
        mergeDxfIntoDrawing(path, QFileInfo(path).fileName());
        return;
    }

    // ---- A DWG, which means the converter ---------------------------------------------------------
    if (!odaExport_) {
        odaExport_ = new OdaConverter(this);
        connect(odaExport_, &OdaConverter::progress, this,
                [this](const QString& what) { statusBar()->showMessage(what); });
    }
    if (odaExport_->busy()) {
        QMessageBox::information(this, QStringLiteral("Import"),
                                 QStringLiteral("A conversion is already running."));
        return;
    }
    if (!odaExport_->available()) {
        // The same setup dialog as the export, because it is the same missing tool and the same four
        // ways forward. Told which errand it is on, so it does not offer to export a DXF to somebody
        // who is trying to read one.
        if (OdaSetupDialog::run(odaExport_, this, OdaSetupDialog::Purpose::ImportDwg)
            != OdaSetupDialog::Result::Detected) {
            return;
        }
    }

    // The converted DXF lands in a temporary directory that lives exactly as long as the merge. It is
    // an intermediate, not a file the user asked for, and leaving DXFs beside their DWGs after every
    // import would be litter they never chose.
    auto* scratch = new QTemporaryDir();
    if (!scratch->isValid()) {
        delete scratch;
        QMessageBox::warning(this, QStringLiteral("Import DWG"),
                             QStringLiteral("A temporary folder could not be made for the conversion."));
        return;
    }
    const QString asDxf = scratch->path() + QStringLiteral("/imported.dxf");
    const QString shownAs = QFileInfo(path).fileName();

    // A one-shot connection: this handler belongs to THIS import and must not still be listening when
    // the next export finishes.
    auto* link = new QMetaObject::Connection;
    *link = connect(odaExport_, &OdaConverter::finished, this,
                    [this, scratch, asDxf, shownAs, link](bool ok, const QString& error) {
                        disconnect(*link);
                        delete link;
                        QApplication::restoreOverrideCursor();
                        if (!ok) {
                            statusBar()->clearMessage();
                            QMessageBox::warning(this, QStringLiteral("Import DWG"), error);
                        } else {
                            mergeDxfIntoDrawing(asDxf, shownAs);
                        }
                        delete scratch;   // takes the converted DXF with it
                    });

    QString err;
    QApplication::setOverrideCursor(Qt::BusyCursor);
    if (!odaExport_->importDwgAsDxf(path, asDxf, &err)) {
        QApplication::restoreOverrideCursor();
        disconnect(*link);
        delete link;
        delete scratch;
        statusBar()->clearMessage();
        QMessageBox::warning(this, QStringLiteral("Import DWG"), err);
    }
}

bool MainWindow::mergeDxfIntoDrawing(const QString& dxfPath, const QString& shownAs, QString* said) {
    if (!doc_) return false;

    // Into a document of its OWN first. dxf::parse REPLACES the entities and layers of whatever it is
    // given - it is written for opening a file, not adding to one - so reading straight into doc_
    // would not import the drawing, it would delete it.
    Document incoming;
    QString error;
    if (!dxf::importFile(incoming, dxfPath, &error)) {
        const QString why = error.isEmpty()
                                ? QStringLiteral("%1 could not be read.").arg(shownAs)
                                : QStringLiteral("%1 could not be read:\n%2").arg(shownAs, error);
        if (said) *said = why;
        else QMessageBox::warning(this, QStringLiteral("Import failed"), why);
        return false;
    }
    if (incoming.entities().empty()) {
        const QString nothing = QStringLiteral("There was nothing to import: %1 has no drawable "
                                               "geometry in it.").arg(shownAs);
        if (said) *said = nothing;
        else QMessageBox::information(this, QStringLiteral("Import"), nothing);
        return false;
    }

    // THE LAYERS COME TOO, or the entities land on names this drawing has never heard of and the
    // layers panel cannot show, switch off or colour them. Only the ones that are missing: a layer
    // this drawing already has keeps ITS settings, because the drawing being imported into is the one
    // whose conventions win.
    int addedLayers = 0;
    for (const Layer& incomingLayer : incoming.layers()) {
        bool have = false;
        for (const Layer& mine : doc_->layers())
            if (mine.name == incomingLayer.name) { have = true; break; }
        if (!have) {
            doc_->layers().push_back(incomingLayer);
            ++addedLayers;
        }
    }

    // At the coordinates the file gives. A DXF's numbers mean something - millimetres on a sheet -
    // and moving the drawing to the origin "so it can be found" would throw that away, which for a
    // customer outline that has to line up with what is already here is the one thing not to do.
    // appendEntities pushes an undo step and selects what arrived, so an import that landed somewhere
    // unhelpful is one Ctrl+Z, or one drag, from being right.
    const int count = static_cast<int>(incoming.entities().size());
    // NOT selected. An imported sheet is commonly a thousand objects or more, and leaving all of them
    // picked means the user's next drag moves the whole import and their next click throws the
    // selection away - neither of which they asked for. It is one undo step, which is the handle that
    // actually matters if the import was wrong.
    editor_->appendEntities(incoming.entities(), /*select=*/false);
    refreshLayers();
    refreshEntityList();
    canvas_->update();

    QString message = tr("Imported %1 object(s) from %2").arg(count).arg(shownAs);
    if (addedLayers > 0) message += tr(", with %1 new layer(s)").arg(addedLayers);
    message += tr(" — Ctrl+Z to undo");
    statusBar()->showMessage(message, 10000);
    if (said) *said = message;
    return true;
}

// EXPORT TO NATIVE DWG.
//
// The drawing goes out as DXF to a temporary folder, the ODA File Converter turns that into a DWG,
// and the DWG is moved to where the user asked for it. All of the interesting behaviour is in what
// happens when the converter is not installed - which is every machine until somebody installs it,
// and is not a fault of the user's. See OdaSetupDialog.
void MainWindow::exportDwgAs() {
    if (!doc_) return;

    // Made once and kept, so a path the user browsed to in the setup dialog is still known the next
    // time they export, and so a running conversion is not orphaned by the manager going out of
    // scope while its process is still working.
    if (!odaExport_) {
        odaExport_ = new OdaConverter(this);
        connect(odaExport_, &OdaConverter::progress, this,
                [this](const QString& what) { statusBar()->showMessage(what); });
        connect(odaExport_, &OdaConverter::finished, this,
                [this](bool ok, const QString& error) {
                    QApplication::restoreOverrideCursor();
                    if (!ok) {
                        statusBar()->clearMessage();
                        QMessageBox::warning(this, QStringLiteral("Export DWG"), error);
                        return;
                    }
                    statusBar()->showMessage(
                        QStringLiteral("Exported %1").arg(QFileInfo(odaExportTarget_).fileName()),
                        4000);
                });
    }

    if (odaExport_->busy()) {
        QMessageBox::information(this, QStringLiteral("Export DWG"),
                                 QStringLiteral("A DWG export is already running."));
        return;
    }

    // The converter is looked for now rather than at start-up: a user who installs it while Edraw is
    // open should not have to restart to be able to use it.
    if (!odaExport_->available()) {
        switch (OdaSetupDialog::run(odaExport_, this)) {
        case OdaSetupDialog::Result::Cancelled:
            return;
        case OdaSetupDialog::Result::FallbackDxf:
            // Their drawing, out of Edraw, now. Straight to the DXF export they would otherwise have
            // had to find on the menu themselves.
            exportDxfAs();
            return;
        case OdaSetupDialog::Result::Detected:
            break;   // found - carry on with the DWG the user actually asked for
        }
    }

    QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Export DWG"), QString(), QStringLiteral("DWG (*.dwg)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(".dwg", Qt::CaseInsensitive)) path += QStringLiteral(".dwg");
    odaExportTarget_ = path;

    QString err;
    QApplication::setOverrideCursor(Qt::BusyCursor);
    if (!odaExport_->exportToDwg(*doc_, path, &err)) {
        QApplication::restoreOverrideCursor();
        statusBar()->clearMessage();
        QMessageBox::warning(this, QStringLiteral("Export DWG"), err);
    }
    // Otherwise the conversion is running; finished() clears the cursor and reports.
}

void MainWindow::exportPdf(bool windowSelect) {
    if (!doc_) return;
    if (windowSelect) {
        statusBar()->showMessage(
            QStringLiteral("Drag a window on the drawing to print to PDF (Esc to cancel)"));
        canvas_->beginWindowCapture([this](Vec2 a, Vec2 b) { exportPdfRegion(a, b); });
        return;
    }
    // On a layout, the page IS the layout: the sheet, at its own size, exactly as the Layout tab shows
    // it. Exporting the extents of the model instead gave a PDF of the geometry with a margin round
    // it - which is a picture of the drawing rather than the sheet that was laid out.
    if (drawingSpaceTabs_ && drawingSpaceTabs_->currentIndex() > 0) {
        Bounds sheet = doc_->sheet();
        if (!sheet.valid) {
            sheet.expand({0.0, 0.0});
            sheet.expand({420.0, 297.0});   // A3 landscape, the same default the canvas draws
        }
        exportPdfRegion(sheet.min, sheet.max);
        return;
    }

    // In model space: the bounds of every visible entity (plus a small margin).
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

PageSetup MainWindow::pageSetupFor(int space) const {
    if (doc_ && space > 0) {
        const auto& layouts = doc_->layouts();
        const int at = space - 1;
        if (at >= 0 && at < static_cast<int>(layouts.size()))
            return layouts[static_cast<size_t>(at)].pageSetup;
    }
    // Model space has no sheet, so it has no paper of its own to plot on: the drawing is fitted to the
    // page, which is what Export PDF has always done with it.
    PageSetup model;
    model.area = PlotArea::Extents;
    model.fitToPaper = true;
    return model;
}

bool MainWindow::spacePageSizeMm(int space, double& widthMm, double& heightMm) const {
    if (!doc_) return false;
    if (space > 0) {
        // The PAGE the sheet is plotted on, which is what the sheet's page setup says. A new layout is set
        // up on full-bleed paper of its own size, so by default the page is the sheet - but a sheet set up
        // for plain A4 comes out A4, and that is the setting's whole purpose.
        const auto& layouts = doc_->layouts();
        const int at = space - 1;
        if (at < 0 || at >= static_cast<int>(layouts.size())) return false;
        const PageSetup setup = layouts[static_cast<size_t>(at)].pageSetup;
        widthMm = std::max(10.0, setup.pageWidthMm());
        heightMm = std::max(10.0, setup.pageHeightMm());
        return true;
    }
    // Model space: what is drawn, with a margin round it, as Export PDF has always taken it.
    Bounds ext;
    for (const auto& e : doc_->entities()) {
        if (const Layer* l = doc_->findLayer(e.layer); l && !l->visible) continue;
        const Bounds b = e.bounds();
        if (b.valid) { ext.expand(b.min); ext.expand(b.max); }
    }
    if (!ext.valid) return false;
    widthMm = ext.width() * 1.1 + 2.0;
    heightMm = ext.height() * 1.1 + 2.0;
    return true;
}

void MainWindow::renderSpaceToPage(QPainter& painter, int space, bool monochrome) {
    if (!doc_) return;
    const QRectF page = painter.viewport();
    const QColor oldBackground = EntityRenderer::backgroundColor();
    const bool oldMonochrome = EntityRenderer::monochrome();
    const bool oldWhitePaper = EntityRenderer::whitePaper();
    const bool oldWeights = EntityRenderer::plotLineweights();
    EntityRenderer::setBackgroundColor(Qt::white);
    EntityRenderer::setMonochrome(monochrome);
    EntityRenderer::setWhitePaper(true);
    EntityRenderer::setPlotLineweights(pageSetupFor(space).plotLineweights);
    painter.fillRect(page, Qt::white);

    Bounds model;
    for (const Entity& e : doc_->entities()) model.expand(e.bounds());

    if (space > 0 && space - 1 < static_cast<int>(doc_->layouts().size())) {
        // A SHEET, laid on the paper its page setup names. The paper space geometry goes on at the plot
        // scale, and each window shows the model at the scale that window was given - the same arithmetic
        // the canvas uses, so what comes out is what was laid out. The frames themselves are not printed:
        // a window is a hole to see the drawing through, not a rectangle in the drawing.
        const DrawingLayout& layout = doc_->layouts()[static_cast<size_t>(space - 1)];
        const PageSetup setup = layout.pageSetup;
        const double sheetW = std::max(10.0, layout.paperWidth);
        const double sheetH = std::max(10.0, layout.paperHeight);
        // How many device units a millimetre of paper is, and the printable rectangle inside the margin.
        // A full-bleed size has no margin, so the printable area IS the page and the sheet comes out at
        // true size; a plain size keeps its margin unprinted, as an office printer needs.
        const double pxPerMm = std::min(page.width() / std::max(1.0, setup.pageWidthMm()),
                                        page.height() / std::max(1.0, setup.pageHeightMm()));
        const double margin = setup.marginMm() * pxPerMm;
        const QRectF printable = page.adjusted(margin, margin, -margin, -margin);
        if (setup.upsideDown) {
            // Plotted the other way up: the whole page turned about its middle, which is what the setting
            // means on a plotter that feeds the paper the other way.
            painter.translate(page.center());
            painter.rotate(180.0);
            painter.translate(-page.center());
        }
        // WHAT of the sheet goes on the page. The whole of it, edge to edge; or only as much as has
        // something on it; or the window that was set; or whatever the canvas is showing at this moment,
        // for a print of the thing being looked at.
        double partX = 0.0, partY = 0.0, partW = sheetW, partH = sheetH;
        if (setup.area == PlotArea::Window && setup.windowValid()) {
            partX = std::min(setup.windowX1Mm, setup.windowX2Mm);
            partY = std::min(setup.windowY1Mm, setup.windowY2Mm);
            partW = std::abs(setup.windowX2Mm - setup.windowX1Mm);
            partH = std::abs(setup.windowY2Mm - setup.windowY1Mm);
        } else if (setup.area == PlotArea::Extents) {
            Bounds on;
            for (const Entity& e : layout.paperEntities) on.expand(e.bounds());
            for (const LayoutViewport& v : layout.viewports) {
                on.expand(Vec2{v.x, v.y});
                on.expand(Vec2{v.x + v.width, v.y + v.height});
            }
            if (on.valid && on.width() > 1e-6 && on.height() > 1e-6) {
                partX = on.min.x;
                partY = on.min.y;
                partW = on.width();
                partH = on.height();
            }
        } else if (setup.area == PlotArea::Display && canvas_ && canvas_->layoutMode() &&
                   canvas_->layoutIndex() == space - 1) {
            // What is on screen: the canvas's own view of this sheet, corner to corner.
            const ViewTransform view = canvas_->transform();
            const Vec2 a = view.toWorld(QPointF(0, 0));
            const Vec2 b = view.toWorld(QPointF(canvas_->width(), canvas_->height()));
            const double x1 = std::min(a.x, b.x), x2 = std::max(a.x, b.x);
            const double y1 = std::min(a.y, b.y), y2 = std::max(a.y, b.y);
            if (x2 - x1 > 1e-6 && y2 - y1 > 1e-6) {
                partX = x1;
                partY = y1;
                partW = x2 - x1;
                partH = y2 - y1;
            }
        }
        const double scale = setup.fitToPaper
                                 ? std::min(printable.width() / partW, printable.height() / partH)
                                 : setup.scale * pxPerMm;
        // Where the middle of what is being printed lands: the middle of the printable area, or the offset
        // from its lower-left corner (paper y measured up, as everywhere else).
        const QPointF centre =
            setup.centred
                ? printable.center()
                : QPointF(printable.left() + setup.offsetXMm * pxPerMm + partW * scale / 2.0,
                          printable.bottom() - setup.offsetYMm * pxPerMm - partH * scale / 2.0);
        ViewTransform paper;
        paper.view.scale = scale;
        paper.view.x = centre.x() - (partX + partW / 2.0) * scale;
        paper.view.y = centre.y() + (partY + partH / 2.0) * scale;
        // The margin is not printed in - by anything. A sheet plotted at true size on paper too small for
        // it is clipped at the printable edge, which is exactly what would come out of the machine.
        painter.setClipRect(printable, Qt::IntersectClip);
        for (const LayoutViewport& viewport : layout.viewports) {
            const QRectF rect = CanvasWidget::layoutViewportRect(viewport, paper);
            const ViewTransform view =
                CanvasWidget::layoutViewportView(viewport, rect, paper, model);
            painter.save();
            painter.setClipRect(rect, Qt::IntersectClip);
            for (const Entity& e : doc_->entities()) {
                if (const Layer* l = doc_->findLayer(e.layer); l && !l->visible) continue;
                EntityRenderer::draw(painter, e, *doc_, view);
            }
            painter.restore();
        }
        // What is ON the sheet goes on last, over the windows - as on the screen, and for the same
        // reason: a note or a border is meant to be read across the view it points at.
        const std::vector<Entity>& onPaper =
            paperLoaded_.first == doc_ && paperLoaded_.second == space - 1
                ? paperDocs_[paperLoaded_]->entities()
                : layout.paperEntities;
        painter.save();
        for (const Entity& e : onPaper) EntityRenderer::draw(painter, e, *doc_, paper);
        painter.restore();
    } else {
        // MODEL SPACE: the drawing, fitted to the page.
        double wMm = 0.0, hMm = 0.0;
        if (spacePageSizeMm(0, wMm, hMm) && model.valid) {
            const double scale = std::min(page.width() / wMm, page.height() / hMm);
            ViewTransform vt;
            vt.view.scale = scale;
            vt.view.x = page.center().x() - model.center().x * scale;
            vt.view.y = page.center().y() + model.center().y * scale;
            painter.setClipRect(page);
            for (const Entity& e : doc_->entities()) {
                if (const Layer* l = doc_->findLayer(e.layer); l && !l->visible) continue;
                EntityRenderer::draw(painter, e, *doc_, vt);
            }
        }
    }
    EntityRenderer::setPlotLineweights(oldWeights);
    EntityRenderer::setWhitePaper(oldWhitePaper);
    EntityRenderer::setMonochrome(oldMonochrome);
    EntityRenderer::setBackgroundColor(oldBackground);
}

void MainWindow::showOutput() {
    if (!doc_) return;
    flushPaperSpace();   // what has just been drawn on the sheet goes out with it
    OutputDialog dlg(*doc_, drawingSpaceTabs_ ? drawingSpaceTabs_->currentIndex() : 0, this);
    if (dlg.exec() != QDialog::Accepted) return;
    const std::vector<int> spaces = dlg.spaces();
    if (spaces.empty()) {
        statusBar()->showMessage(tr("Nothing was ticked, so nothing came out."), 4000);
        return;
    }
    // Anything that has nothing on it is dropped here rather than coming out as a blank page.
    std::vector<int> pages;
    QStringList empty;
    for (const int space : spaces) {
        double w = 0.0, h = 0.0;
        if (spacePageSizeMm(space, w, h)) pages.push_back(space);
        else empty << spaceName(space);
    }
    if (pages.empty()) {
        QMessageBox::information(this, tr("Output"),
                                 tr("There is nothing on %1 — nothing to put out.")
                                     .arg(empty.join(QStringLiteral(", "))));
        return;
    }

    const bool monochrome = dlg.monochrome();
    const bool fit = dlg.fitToPage();

    if (dlg.toPrinter()) {
        printSpaces(pages);
        return;
    }

    if (dlg.oneFilePerSheet()) {
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("A folder for the sheets"), QString());
        if (dir.isEmpty()) return;
        int written = 0;
        for (const int space : pages) {
            const QString path =
                QDir(dir).filePath(QStringLiteral("%1.pdf").arg(spaceName(space)));
            if (writeSpacePdf(path, {space}, monochrome, fit)) ++written;
        }
        statusBar()->showMessage(tr("%1 file(s) written to %2").arg(written).arg(dir), 6000);
        return;
    }

    QString path = QFileDialog::getSaveFileName(this, tr("Output PDF"), QString(),
                                                QStringLiteral("PDF (*.pdf)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(QStringLiteral(".pdf"), Qt::CaseInsensitive)) path += QStringLiteral(".pdf");
    if (writeSpacePdf(path, pages, monochrome, fit))
        statusBar()->showMessage(
            tr("%1 page(s) written to %2").arg(pages.size()).arg(QFileInfo(path).fileName()), 6000);
}

QString MainWindow::spaceName(int space) const {
    if (space <= 0) return tr("Model");
    const auto& layouts = doc_ ? doc_->layouts() : std::vector<DrawingLayout>();
    const int at = space - 1;
    if (at >= 0 && at < static_cast<int>(layouts.size()))
        return layouts[static_cast<size_t>(at)].name;
    return tr("Layout%1").arg(space);
}

bool MainWindow::printSpaces(const std::vector<int>& spaces) {
    if (!doc_ || spaces.empty()) return false;
    QPrinter printer(QPrinter::HighResolution);
    // The printer the first page's setup names, if it names one and the machine is still there: a page
    // setup that says which plotter it is for should not have to be repeated in the print dialog.
    const PageSetup first = pageSetupFor(spaces.front());
    if (!first.device.isEmpty()) printer.setPrinterName(first.device);
    // The paper the setup asks for, and which way round - upright paper plus an orientation, for the same
    // reason as the PDF above. The print dialog opens on it rather than on whatever it was left on.
    {
        double w = 0.0, h = 0.0;
        if (spacePageSizeMm(spaces.front(), w, h)) {
            const QSizeF upright(std::min(w, h), std::max(w, h));
            printer.setPageSize(QPageSize(upright, QPageSize::Millimeter, spaceName(spaces.front()),
                                          QPageSize::FuzzyMatch));
            printer.setPageOrientation(w >= h ? QPageLayout::Landscape : QPageLayout::Portrait);
        }
    }
    QPrintDialog print(&printer, this);
    print.setWindowTitle(tr("Print %1 page(s)").arg(spaces.size()));
    if (print.exec() != QDialog::Accepted) return false;
    QPainter painter(&printer);
    if (!painter.isActive()) {
        QMessageBox::warning(this, tr("Print failed"), tr("Could not start the print job."));
        return false;
    }
    for (size_t i = 0; i < spaces.size(); ++i) {
        if (i > 0 && !printer.newPage()) break;
        // Each page by its own setup: the ink it says, and the paper it says as far as the printer's own
        // paper allows. The page is laid out to fill what the printer gives; the setup's scale and offset
        // are honoured inside it by renderSpaceToPage.
        const PageSetup setup = pageSetupFor(spaces[i]);
        renderSpaceToPage(painter, spaces[i], setup.monochrome);
    }
    painter.end();
    statusBar()->showMessage(tr("Printed %1 page(s)").arg(spaces.size()), 5000);
    return true;
}

bool MainWindow::writeSpacePdf(const QString& path, const std::vector<int>& spaces, bool monochrome,
                               bool fitToPage) {
    if (!doc_ || spaces.empty()) return false;
    QPdfWriter pdf(path);
    pdf.setResolution(300);
    QPainter painter;
    for (size_t i = 0; i < spaces.size(); ++i) {
        double w = 0.0, h = 0.0;
        if (!spacePageSizeMm(spaces[i], w, h)) continue;
        // Each page is the size of the thing on it: a 420 x 297 sheet is an A3 page, and a dimension
        // read off the print with a rule is right. Fitting to a fixed page is offered, not assumed.
        // The page, and which way round it goes - and these two must not both do the swapping.
        //
        // A QPageSize is a piece of paper, and it is given UPRIGHT: short edge across, long edge up. The
        // orientation then turns it. Handed a size that was already turned (420 x 297 for a landscape A3)
        // AND told Landscape, Qt turned it again and wrote a portrait page: a landscape sheet came out of
        // the PDF standing up. So the paper goes in upright and the orientation does the turning, once.
        const bool landscape = w >= h;
        const QSizeF upright(std::min(w, h), std::max(w, h));
        const QPageSize size = fitToPage
                                   ? QPageSize(QPageSize::A4)
                                   : QPageSize(upright, QPageSize::Millimeter, spaceName(spaces[i]),
                                               QPageSize::ExactMatch);
        if (i == 0) {
            pdf.setPageSize(size);
            pdf.setPageOrientation(landscape ? QPageLayout::Landscape : QPageLayout::Portrait);
            pdf.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Millimeter);
            painter.begin(&pdf);
            if (!painter.isActive()) {
                QMessageBox::warning(this, tr("Output failed"),
                                     tr("Could not open %1 for writing.").arg(path));
                return false;
            }
        } else {
            pdf.setPageSize(size);
            pdf.setPageOrientation(landscape ? QPageLayout::Landscape : QPageLayout::Portrait);
            if (!pdf.newPage()) break;
        }
        renderSpaceToPage(painter, spaces[i], monochrome);
    }
    if (painter.isActive()) painter.end();
    return true;
}

bool MainWindow::choosePdfLineMode(bool& monochrome) {
    QMessageBox box(QMessageBox::Question, QStringLiteral("Export PDF"),
                    QStringLiteral("Choose how drawing lines should be exported."),
                    QMessageBox::NoButton, this);
    box.setInformativeText(QStringLiteral("The PDF page will always have a white background."));
    auto* black = box.addButton(QStringLiteral("Black lines"), QMessageBox::AcceptRole);
    auto* color = box.addButton(QStringLiteral("Color lines"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(black);
    box.exec();
    if (box.clickedButton() == black) {
        monochrome = true;
        return true;
    }
    if (box.clickedButton() == color) {
        monochrome = false;
        return true;
    }
    return false;
}

void MainWindow::exportPdfRegion(const Vec2& a, const Vec2& b) {
    if (!doc_) return;
    const double wMinX = std::min(a.x, b.x), wMaxX = std::max(a.x, b.x);
    const double wMinY = std::min(a.y, b.y), wMaxY = std::max(a.y, b.y);
    const double worldW = wMaxX - wMinX, worldH = wMaxY - wMinY;
    if (worldW < 1e-9 || worldH < 1e-9) return;

    bool monochrome = true;
    if (!choosePdfLineMode(monochrome)) return;

    QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export PDF"), QString(),
                                                QStringLiteral("PDF (*.pdf)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(".pdf", Qt::CaseInsensitive)) path += ".pdf";

    QPdfWriter pdf(path);
    pdf.setResolution(300);
    // The nearest standard sheet to what is being exported, rather than A4 whatever it is: an A3
    // layout printed onto A4 is the same drawing at 71%, and a dimension read off it with a rule is
    // wrong. The long and short edges are compared against the A series, in millimetres.
    struct Paper { QPageSize::PageSizeId id; double shortMm, longMm; };
    const Paper papers[] = {{QPageSize::A5, 148.0, 210.0},
                            {QPageSize::A4, 210.0, 297.0},
                            {QPageSize::A3, 297.0, 420.0},
                            {QPageSize::A2, 420.0, 594.0},
                            {QPageSize::A1, 594.0, 841.0},
                            {QPageSize::A0, 841.0, 1189.0}};
    const double shortEdge = std::min(worldW, worldH), longEdge = std::max(worldW, worldH);
    QPageSize::PageSizeId paper = QPageSize::A4;
    double bestFit = 1e300;
    for (const Paper& q : papers) {
        // Big enough for it, and no bigger than it need be.
        if (q.shortMm + 0.5 < shortEdge || q.longMm + 0.5 < longEdge) continue;
        const double waste = q.shortMm * q.longMm - shortEdge * longEdge;
        if (waste < bestFit) {
            bestFit = waste;
            paper = q.id;
        }
    }
    pdf.setPageSize(QPageSize(paper));
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

    const QColor oldBackground = EntityRenderer::backgroundColor();
    const bool oldMonochrome = EntityRenderer::monochrome();
    const bool oldWhitePaper = EntityRenderer::whitePaper();
    EntityRenderer::setBackgroundColor(Qt::white);
    EntityRenderer::setMonochrome(monochrome);
    EntityRenderer::setWhitePaper(true);
    p.fillRect(0, 0, pw, ph, Qt::white);
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
    EntityRenderer::setWhitePaper(oldWhitePaper);
    EntityRenderer::setMonochrome(oldMonochrome);
    EntityRenderer::setBackgroundColor(oldBackground);
    statusBar()->showMessage(QStringLiteral("Exported %1").arg(QFileInfo(path).fileName()));
}

void MainWindow::exportTableCsv() {
    if (!doc_) return;
    // Target: the table of the selected cell, else a single selected table entity.
    const Entity* table = nullptr;
    if (!editor_->selectedCellTable().isEmpty()) {
        for (const auto& e : editDoc()->entities())
            if (e.id == editor_->selectedCellTable()) { table = &e; break; }
    }
    if (!table && editor_->selection().size() == 1) {
        for (const auto& e : editDoc()->entities())
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
