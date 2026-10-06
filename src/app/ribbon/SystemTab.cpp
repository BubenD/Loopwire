// THE SYSTEM DESIGN TAB: the system diagram - units, their ports and the cables between them, each cable with its part
// number and revision and the drawing that defines it - in the middle of the window while the tab is open, and every
// other tab puts the drawing back. Its buttons: what a click does (Select, Unit, Port, Cable), Delete and Undo; the
// cables' part numbers, the drawing each is linked to, Find drawings; the revision check and the design check; the
// cable list out to a file.
#include "app/MainWindow.h"
#include "app/Chrome.h"
#include "app/DrawingFinderDialog.h"
#include "app/Icons3D.h"
#include "app/SystemDesignView.h"
#include "model/Document.h"
#include "render/CanvasWidget.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace Edraw_Desktop {

void MainWindow::buildSystemTab() {
    auto [page, lay] = makeTabPage();
    systemTabPage_ = page;
    // THE VIEW ITSELF, in the middle of the window beside the drawing and the 3D preview.
    systemView_ = new SystemDesignView(centralStack_);
    centralStack_->addWidget(systemView_);
    systemView_->setStore([this] { return doc_ ? doc_->systemDesign() : QJsonObject(); },
                          [this](const QJsonObject& o) {
                              if (!doc_ || doc_->systemDesign() == o) return;
                              doc_->systemDesign() = o;
                              doc_->setDirty(true);
                          });
    connect(systemView_, &SystemDesignView::openRequested, this, [this](const QString& path) { openPath(path); });

    auto big = [this](QWidget* parent, QHBoxLayout* row, const char* name, const QString& text, const QString& icon, const QString& tip,
                      auto fn) {
        auto* b = new QToolButton(parent);
        b->setObjectName(QLatin1String(name));
        b->setText(text);
        b->setToolTip(tip);
        b->setIcon(icon3D(icon, Chrome::ink()));
        b->setIconSize(QSize(26, 26));
        b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        b->setAutoRaise(true);
        b->setFocusPolicy(Qt::NoFocus);
        connect(b, &QToolButton::clicked, this, fn);
        row->addWidget(b);
        preview3dIcons_.append({icon, b});
        return b;
    };
    auto group = [&](const QString& title, auto fill) {
        auto* w = new QWidget(page);
        auto* row = new QHBoxLayout(w);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(1);
        fill(w, row);
        addGroup(lay, title, w);
    };
    // WHAT A CLICK DOES.
    group(QStringLiteral("Draw"), [&](QWidget* w, QHBoxLayout* row) {
        auto* tools = new QButtonGroup(w);
        tools->setExclusive(true);
        const struct {
            SysTool tool;
            const char* name;
            const char* text;
            const char* icon;
            const char* tip;
        } list[] = {
            {SysTool::Select, "systemSelect", "Select", "select", "Select a unit, a port or a cable - drag a unit to move it, drag from a port to draw a cable"},
            {SysTool::Unit, "systemUnit", "Unit", "unit", "Put a unit of the system where you click"},
            {SysTool::Port, "systemPort", "Port", "connector", "Put a port on the side of the unit you click, where you click"},
            {SysTool::Cable, "systemCable", "Cable", "route", "Draw a cable: press on one port and let go on another"},
        };
        for (const auto& t : list) {
            QToolButton* b = big(w, row, t.name, QString::fromLatin1(t.text), QString::fromLatin1(t.icon), QString::fromLatin1(t.tip),
                                 [this, tool = t.tool] {
                                     if (systemView_) systemView_->setTool(tool);
                                 });
            b->setCheckable(true);
            tools->addButton(b);
            if (t.tool == SysTool::Select) b->setChecked(true);
        }
        big(w, row, "systemDelete", QStringLiteral("Delete"), QStringLiteral("delete"),
            QStringLiteral("Delete the unit, port or cable selected (a unit or port takes its cables with it)"),
            [this] { if (systemView_) systemView_->canvas()->deleteSelected(); });
        big(w, row, "systemUndo", QStringLiteral("Undo"), QStringLiteral("undo"), QStringLiteral("Take back the last change to the diagram"),
            [this] { if (systemView_) systemView_->undo(); });
        big(w, row, "systemFit", QStringLiteral("Fit"), QStringLiteral("fit"), QStringLiteral("The whole diagram in view"),
            [this] { if (systemView_) systemView_->canvas()->fit(); });
    });
    // THE CABLES: their part numbers, and the drawings that define them.
    group(QStringLiteral("Cables"), [&](QWidget* w, QHBoxLayout* row) {
        big(w, row, "systemNumber", QStringLiteral("Number P/Ns"), QStringLiteral("renumber"),
            QStringLiteral("Give each cable with no part number the next one (CBL-001, CBL-002...)"), [this] {
                if (systemView_) systemView_->numberCables();
            });
        big(w, row, "systemLink", QStringLiteral("Link drawing"), QStringLiteral("link"),
            QStringLiteral("Link a drawing file to the cable selected - its drawing number and revision are read off it"),
            [this] { if (systemView_) systemView_->linkDrawingFromFile(); });
        big(w, row, "systemFind", QStringLiteral("Find drawings"), QStringLiteral("search"),
            QStringLiteral("Search folders for DWX, DXF and DWG drawings - by name, drawing number or title - with a preview; a cable "
                           "selected: link the one found"),
            [this] { if (systemView_) systemView_->findDrawing(); });
        big(w, row, "systemOpen", QStringLiteral("Open drawing"), QStringLiteral("load"),
            QStringLiteral("Open the drawing linked to the cable selected"), [this] { if (systemView_) systemView_->openDrawing(); });
    });
    // THE CHECKS.
    group(QStringLiteral("Check"), [&](QWidget* w, QHBoxLayout* row) {
        big(w, row, "systemCheckRevisions", QStringLiteral("Check revisions"), QStringLiteral("check"),
            QStringLiteral("Read every linked drawing again and say where its revision (or drawing number) is not the one the cable "
                           "calls for - and check the whole design"),
            [this] {
                if (!systemView_) return;
                const int errors = systemView_->checkRevisions();
                statusBar()->showMessage(errors == 0 ? QStringLiteral("System design: every linked drawing is the revision called for")
                                                     : QStringLiteral("System design: %1 error(s) - see Check under the diagram").arg(errors),
                                         8000);
            });
        big(w, row, "systemCheck", QStringLiteral("Check design"), QStringLiteral("measure"),
            QStringLiteral("Ports with nothing plugged in or more than one cable, cables with no part number, the same part number on "
                           "cables that differ, no drawing linked"),
            [this] { if (systemView_) systemView_->checkDesign(); });
    });
    group(QStringLiteral("Export"), [&](QWidget* w, QHBoxLayout* row) {
        big(w, row, "systemExport", QStringLiteral("Cable list"), QStringLiteral("export"),
            QStringLiteral("The cable list - each cable, its part number, revision, ends, length and drawing - to a CSV file"),
            [this] { if (systemView_) systemView_->exportCableList(); });
    });
    lay->addStretch(1);
    ribbon_->addTab(page, QStringLiteral("System design"));
}

void MainWindow::showSystemDesign(bool on) {
    if (!systemView_ || !centralStack_) return;
    if (on) {
        centralStack_->setCurrentWidget(systemView_);
        systemView_->canvas()->setFocus();
    } else if (centralStack_->currentWidget() == systemView_) {
        centralStack_->setCurrentIndex(0);
        canvas_->setFocus();
    }
}

} // namespace Edraw_Desktop
