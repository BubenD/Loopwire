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

namespace Loopwire {

// ---- MODIFY ----
void MainWindow::buildModifyTab() {
    auto [page, lay] = makeTabPage();
    {
        // Move / Rotate / Mirror / Scale / Offset - and Array, which is the same idea repeated.
        // All six in the same two rows: Array standing full height beside two rows of tools read as
        // something other than one of them.
        addGroup(lay, QStringLiteral("Transform"),
                 toolRow({ToolId::Move, ToolId::Rotate, ToolId::Mirror, ToolId::Scale, ToolId::Offset},
                         {makeArrayButton(page)}));
    }
    {
        // Edit group: the pick-modify tools plus the Copy/Cut clipboard button.
        // The pick-modify tools and the clipboard button, in the same two rows.
        addGroup(lay, QStringLiteral("Edit"),
                 toolRow({ToolId::Trim, ToolId::Extend, ToolId::Fillet, ToolId::Chamfer},
                         {clipboardRow(true)}));
    }
    addGroup(lay, QStringLiteral("Operations"), operationsRow());
    lay->addStretch(1);
    ribbon_->addTab(page, QStringLiteral("Modify"));
}

} // namespace Loopwire
