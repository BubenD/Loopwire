// The Insert ribbon tab. Split out of MainWindow.cpp so each tab is one file: the ribbon was the
// largest thing in that file and the place every new feature landed. Still MainWindow members, so
// nothing about what they build or reach has changed.
#include "app/MainWindow.h"

#include "app/BlockDialog.h"
#include "app/ribbon/RibbonIcons.h"
#include "app/Editor.h"
#include "harness/Harness.h"
#include "render/CanvasWidget.h"
#include "tools/QuickSelect.h"

#include <QActionGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

#include <cmath>
#include <functional>

namespace Edraw_Desktop {

// ---- INSERT ----
void MainWindow::buildInsertTab() {
    auto [page, lay] = makeTabPage();
    addGroup(lay, QStringLiteral("Annotation"),
             toolRow({ToolId::Text, ToolId::Dimension, ToolId::DetailView}));

    // ---- Pictures ----
    // A logo, a photograph of the loom, a screenshot of a connector drawing: on the sheet as an
    // ordinary object, moved and sized like anything else. Two ways in, because both are how it is
    // done in practice - a file on disk, or whatever is on the clipboard right now.
    {
        auto* w = new QWidget(page);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0);
        g->setSpacing(2);
        // A framed picture: a horizon, a sun and a hill, which is what a picture icon looks like.
        auto pictureIcon = [](bool clipboard) {
            QPixmap pm(28, 28);
            pm.fill(Qt::transparent);
            QPainter ip(&pm);
            ip.setRenderHint(QPainter::Antialiasing, true);
            const QRect frame = clipboard ? QRect(3, 6, 18, 18) : QRect(3, 5, 22, 18);
            ip.setPen(QPen(QColor("#8fb7ff"), 1.5));
            ip.setBrush(QColor(40, 52, 68));
            ip.drawRect(frame);
            ip.setPen(Qt::NoPen);
            ip.setBrush(QColor("#e0c169"));
            ip.drawEllipse(QPointF(frame.left() + 5.5, frame.top() + 5.0), 2.2, 2.2);
            QPainterPath hill;
            hill.moveTo(frame.left() + 1.0, frame.bottom() - 1.0);
            hill.lineTo(frame.left() + frame.width() * 0.42, frame.top() + frame.height() * 0.45);
            hill.lineTo(frame.left() + frame.width() * 0.68, frame.bottom() - 1.0);
            hill.closeSubpath();
            ip.setBrush(QColor("#6fae7a"));
            ip.drawPath(hill);
            if (clipboard) {
                // A clipboard behind it, so the two buttons are told apart at a glance.
                ip.setPen(QPen(QColor("#c9d3e2"), 1.5));
                ip.setBrush(Qt::NoBrush);
                ip.drawRect(9, 2, 16, 20);
                ip.drawLine(14, 2, 20, 2);
            }
            return QIcon(pm);
        };
        auto button = [&](const QString& text, const QString& tip, const QIcon& icon,
                          std::function<void()> onClick) {
            auto* b = new QToolButton(w);
            b->setText(text);
            b->setIcon(icon);
            b->setToolTip(tip);
            b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
            b->setIconSize(QSize(26, 26));
            b->setMinimumWidth(76);
            b->setAutoRaise(true);
            b->setFocusPolicy(Qt::NoFocus);
            connect(b, &QToolButton::clicked, this, [onClick] { onClick(); });
            return b;
        };
        g->addWidget(button(QStringLiteral("Picture"),
                            QStringLiteral("Insert a picture from a file — PNG, JPEG, BMP and every "
                                           "other format this build can read. Click to place it, then "
                                           "drag a corner to size it."),
                            pictureIcon(false), [this] { insertPictureFromFile(); }),
                     0, 0);
        g->addWidget(button(QStringLiteral("Paste\npicture"),
                            QStringLiteral("Place whatever picture is on the clipboard — a screenshot, "
                                           "an image copied from another application, or an image file "
                                           "copied in the file manager. Ctrl+V does the same."),
                            pictureIcon(true), [this] { insertPictureFromClipboard(false); }),
                     0, 1);
        addGroup(lay, QStringLiteral("Picture"), w);
    }
    {
        auto* w = new QWidget(page);
        auto* g = new QGridLayout(w);
        g->setContentsMargins(0, 0, 0, 0);
        g->setSpacing(2);
        auto* pdf = new QToolButton(w);
        pdf->setText(QStringLiteral("PDF import"));
        pdf->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        pdf->setIconSize(QSize(28, 28));
        pdf->setAutoRaise(true);
        pdf->setFocusPolicy(Qt::NoFocus);
        QPixmap pm(28, 28);
        pm.fill(Qt::transparent);
        QPainter ip(&pm);
        ip.setRenderHint(QPainter::Antialiasing, true);
        ip.setPen(QPen(QColor("#e6e9ef"), 1.6));
        ip.drawRect(5, 3, 18, 22);
        ip.drawLine(15, 3, 23, 11);
        ip.drawLine(15, 3, 15, 11);
        ip.drawLine(15, 11, 23, 11);
        ip.setPen(QPen(QColor("#e66b6b"), 1.8));
        ip.drawLine(8, 17, 20, 17);
        ip.drawLine(8, 21, 20, 21);
        ip.end();
        pdf->setIcon(QIcon(pm));
        pdf->setToolTip(QStringLiteral("Import vector paths from a PDF through DXF into the current drawing"));
        connect(pdf, &QToolButton::clicked, this, [this] { importPdf(); });
        g->addWidget(pdf, 0, 0);
        addGroup(lay, QStringLiteral("Import"), w);
    }
    lay->addStretch(1);
    // Import, not Insert: what the tab is for is bringing a drawing, a picture or a PDF IN from
    // somewhere else, and "insert" says nothing about where a thing comes from.
    ribbon_->addTab(page, QStringLiteral("Import"));
}

} // namespace Edraw_Desktop
