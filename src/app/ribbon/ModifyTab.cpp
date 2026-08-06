// The Modify ribbon tab. Split out of MainWindow.cpp so each tab is one file: the ribbon was the
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

namespace Edraw_Desktop {

// ---- MODIFY ----
void MainWindow::buildModifyTab() {
    auto [page, lay] = makeTabPage();
    {
        // Move / Rotate / Mirror / Scale / Offset - and Array, which is the same idea repeated.
        auto* tw = new QWidget(page);
        auto* th = new QHBoxLayout(tw);
        th->setContentsMargins(0, 0, 0, 0);
        th->setSpacing(2);
        th->addWidget(toolRow({ToolId::Move, ToolId::Rotate, ToolId::Mirror, ToolId::Scale,
                               ToolId::Offset}));
        th->addWidget(makeArrayButton(tw));
        addGroup(lay, QStringLiteral("Transform"), tw);
    }
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

} // namespace Edraw_Desktop
