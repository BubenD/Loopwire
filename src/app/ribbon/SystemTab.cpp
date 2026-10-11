// THE SYSTEM DESIGN TAB: the system diagram - units, their ports and the cables between them, each cable with its part
// number and revision and the drawing that defines it - in the middle of the window while the tab is open, and every
// other tab puts the drawing back. Its buttons: what a click does (Select, Unit, Port, Cable, Note) and Delete - Undo
// and Redo are the window's, in its top right corner; the
// cables' part numbers, the drawing each is linked to, Find drawings; the revision check and the design check; the
// cable list out to a file.
#include "app/MainWindow.h"
#include "app/Chrome.h"
#include "app/DrawingFinderDialog.h"
#include "app/Icons3D.h"
#include "app/SystemDesignView.h"
#include "model/Document.h"
#include "render/CanvasWidget.h"

#include <QActionGroup>
#include <QButtonGroup>
#include <QDockWidget>
#include <QMenu>
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
            {SysTool::Select, "systemSelect", "Select", "select",
             "Select a unit, a port, a cable or a note - drag a unit or a note to move it, a port to slide it along its unit's "
             "sides, a unit's corner or edge to size it (Esc)"},
            {SysTool::Unit, "systemUnit", "Unit", "unit",
             "Put a unit of the system where you click (U) - the arrow: a mechanical part or a PCBA"},
            {SysTool::Port, "systemPort", "Port", "connector", "Put a port on the side of the unit you click, where you click (P)"},
            {SysTool::Cable, "systemCable", "Harness", "route",
             "Draw a harness (H): press on a port and let go on another; start or end on a unit where it has no port, and a port "
             "is put there. Press on a harness and let go on a port to spread it to that port too - a branch"},
            {SysTool::Note, "systemNote", "Note", "help", "Put a note where you click (N) - its text on the right"},
        };
        auto* toolGroup = tools;
        for (const auto& t : list) {
            QToolButton* b = big(w, row, t.name, QString::fromLatin1(t.text), QString::fromLatin1(t.icon), QString::fromLatin1(t.tip),
                                 [this, tool = t.tool] {
                                     if (systemView_) systemView_->setTool(tool);
                                 });
            b->setCheckable(true);
            tools->addButton(b, static_cast<int>(t.tool));
            if (t.tool == SysTool::Select) b->setChecked(true);
            if (t.tool == SysTool::Select) {
                b->setPopupMode(QToolButton::MenuButtonPopup);
                b->setToolTip(QStringLiteral("Select and drag objects. Drag an empty area to select a zone: blue encloses objects, green crosses them. The arrow chooses the zone mode."));
                auto* menu = new QMenu(b);
                auto* modes = new QActionGroup(menu);
                const std::pair<const char*, SysSelectionMode> choices[] = {
                    {"By drag direction (blue / green)", SysSelectionMode::Direction},
                    {"Blue window: fully enclosed", SysSelectionMode::Window},
                    {"Green crossing: touched objects", SysSelectionMode::Crossing}
                };
                for (const auto& choice : choices) {
                    auto* action = menu->addAction(QString::fromLatin1(choice.first), this, [this, mode = choice.second] {
                        systemView_->setTool(SysTool::Select); systemView_->canvas()->setSelectionMode(mode);
                    });
                    action->setCheckable(true); action->setChecked(choice.second == SysSelectionMode::Direction); modes->addAction(action);
                }
                b->setMenu(menu);
            }
            // UNIT'S ARROW: what the units put next are.
            if (t.tool == SysTool::Unit) {
                b->setPopupMode(QToolButton::MenuButtonPopup);
                auto* kinds = new QMenu(b);
                kinds->setObjectName(QStringLiteral("systemUnitKinds"));
                auto* pick = new QActionGroup(kinds);
                for (const auto& [text, kind] : {std::pair<const char*, const char*>{"Mechanical part", "mech"}, {"PCBA", "pcba"}, {"System module", "module"}, {"Device", "device"}}) {
                    QAction* a = kinds->addAction(QString::fromLatin1(text), this, [this, b, k = QString::fromLatin1(kind)] {
                        if (!systemView_) return;
                        systemView_->canvas()->setNewUnitKind(k);
                        systemView_->setTool(SysTool::Unit);
                        b->setChecked(true);
                    });
                    a->setIcon(icon3D(QLatin1String(kind)==QLatin1String("module")?QStringLiteral("module"):QStringLiteral("unit"),Chrome::ink()));
                    a->setCheckable(true);
                    a->setChecked(QLatin1String(kind) == QLatin1String("mech"));
                    pick->addAction(a);
                }
                b->setMenu(kinds);
            }
        }
        // THE TOOL TAKEN BY ITS KEY in the diagram: its button pressed in here too.
        connect(systemView_->canvas(), &SystemCanvas::toolChanged, this, [toolGroup](SysTool tool) {
            if (QAbstractButton* b = toolGroup->button(static_cast<int>(tool))) b->setChecked(true);
        });
        big(w,row,"systemLinkNote",QStringLiteral("Link note (L)"),QStringLiteral("link"),QStringLiteral("Select a note, then L and click its unit, cable or port"),
            [this]{if(systemView_&&!systemView_->canvas()->startNoteLink())statusBar()->showMessage(QStringLiteral("Select a note to link first"),4000);});
        big(w, row, "systemDelete", QStringLiteral("Delete"), QStringLiteral("delete"),
            QStringLiteral("Delete the unit, port, cable or note selected (Delete) - a unit or port with cables, or a unit with "
                           "units in it, only once asked; Space does it again"),
            [this] { if (systemView_) systemView_->deleteSelected(); });
        big(w, row, "systemFit", QStringLiteral("Fit"), QStringLiteral("fit"), QStringLiteral("The whole diagram in view"),
            [this] { if (systemView_) systemView_->canvas()->fit(); });
    });
    group(QStringLiteral("View"), [&](QWidget* w, QHBoxLayout* row) {
        auto* colour=big(w,row,"systemCanvasColour",QStringLiteral("Canvas colour"),QStringLiteral("unit"),
                         QStringLiteral("Choose the project canvas background; the arrow offers dark grey and light presets"),
                         [this]{if(systemView_) systemView_->askCanvasColour();});
        colour->setPopupMode(QToolButton::MenuButtonPopup); auto* menu=new QMenu(colour);
        menu->addAction(QStringLiteral("Dark grey"),this,[this]{if(systemView_)systemView_->setCanvasColour(QColor(48,52,58));});
        menu->addAction(QStringLiteral("Light"),this,[this]{if(systemView_)systemView_->setCanvasColour(QColor(250,251,252));});
        menu->addAction(QStringLiteral("Custom colour..."),this,[this]{if(systemView_)systemView_->askCanvasColour();}); colour->setMenu(menu);
        // THE DESIGNATORS' COLOUR: Auto (ticked) - each dark or light against what it is on - or one chosen for all.
        auto* names = big(w, row, "systemDesignatorColour", QStringLiteral("Designators"), QStringLiteral("designator"),
                          QStringLiteral("The colour of unit, port and cable designators - Auto keeps each readable against what "
                                         "is behind it; the arrow: Auto or a colour of your own"),
                          [this] { if (systemView_) systemView_->askDesignatorColour(); });
        names->setPopupMode(QToolButton::MenuButtonPopup);
        auto* namesMenu = new QMenu(names);
        auto* autoInk = namesMenu->addAction(QStringLiteral("Auto colour"));
        autoInk->setObjectName(QStringLiteral("systemDesignatorAuto"));
        autoInk->setCheckable(true);
        connect(autoInk, &QAction::triggered, this, [this, autoInk](bool on) {
            if (!systemView_) return;
            if (on) systemView_->setDesignatorColour(QColor());
            else systemView_->askDesignatorColour();
            autoInk->setChecked(!systemView_->design().designatorColor.isValid());
        });
        namesMenu->addAction(QStringLiteral("Choose colour..."), this, [this] { if (systemView_) systemView_->askDesignatorColour(); });
        connect(namesMenu, &QMenu::aboutToShow, this, [this, autoInk] {
            autoInk->setChecked(!systemView_ || !systemView_->design().designatorColor.isValid());
        });
        names->setMenu(namesMenu);
        // THE SHEET ROUND THE DIAGRAM: a drawing template's frame and title block.
        auto* sheet = big(w, row, "systemTemplate", QStringLiteral("Template"), QStringLiteral("display"),
                          QStringLiteral("Put a drawing template's sheet - its frame and title block - round the diagram; the arrow: "
                                         "its fields, or take it away"),
                          [this] { if (systemView_) systemView_->applyTemplate(); });
        sheet->setPopupMode(QToolButton::MenuButtonPopup);
        auto* sheetMenu = new QMenu(sheet);
        sheetMenu->addAction(QStringLiteral("Choose template..."), this, [this] { if (systemView_) systemView_->applyTemplate(); });
        sheetMenu->addAction(QStringLiteral("Sheet fields..."), this, [this] { if (systemView_) systemView_->askSheetFields(); });
        sheetMenu->addAction(QStringLiteral("Remove the sheet"), this, [this] { if (systemView_) systemView_->removeSheet(); });
        sheet->setMenu(sheetMenu);
    });
    // THE CABLES: their part numbers, and the drawings that define them.
    group(QStringLiteral("Cables"), [&](QWidget* w, QHBoxLayout* row) {
        QToolButton* number = big(w, row, "systemNumber", QStringLiteral("Number P/Ns"), QStringLiteral("renumber"),
                                  QStringLiteral("Give each cable with no part number the next one (CBL-001, CBL-002...) - the arrow: "
                                                 "the prefix and how many digits"),
                                  [this] {
                                      if (systemView_) systemView_->numberCables();
                                  });
        number->setPopupMode(QToolButton::MenuButtonPopup);
        auto* numbering = new QMenu(number);
        numbering->addAction(QStringLiteral("Prefix and digits..."), this, [this] { if (systemView_) systemView_->askNumbering(); });
        number->setMenu(numbering);
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
                           "calls for, and check its drawing fields - nothing else"),
            [this] {
                if (!systemView_) return;
                const int errors = systemView_->checkRevisions();
                statusBar()->showMessage(errors == 0 ? QStringLiteral("System design: every linked drawing is the revision called for")
                                                     : QStringLiteral("System design: %1 error(s) - see Check under the diagram").arg(errors),
                                         8000);
            });
        auto* check = big(w, row, "systemCheck", QStringLiteral("Check design"), QStringLiteral("measure"),
            QStringLiteral("Ports with nothing plugged in or more than one cable, cables with no part number, the same part number on "
                           "cables that differ, no drawing linked"),
            [this] { if (systemView_) systemView_->checkDesign(); });
        check->setPopupMode(QToolButton::MenuButtonPopup);
        auto* rules = new QMenu(check);
        rules->addAction(QStringLiteral("Design rules..."), this, [this] { if (systemView_) systemView_->askRules(); });
        check->setMenu(rules);
        big(w,row,"systemBypass",QStringLiteral("Bypass table"),QStringLiteral("check"),QStringLiteral("Review and restore accepted project findings"),
            [this]{if(systemView_)systemView_->showBypass();});
        big(w, row, "systemCheckConnections", QStringLiteral("Connections"), QStringLiteral("check"),
            QStringLiteral("Check named connections in every linked schematic and compare line names at shared unit ports and contacts"),
            [this] { if (systemView_) systemView_->checkSchematics(); });
    });
    // THE DESIGN AS A FILE OF ITS OWN - saved, opened - and the diagram out for draw.io and Visio.
    group(QStringLiteral("File"), [&](QWidget* w, QHBoxLayout* row) {
        big(w, row, "systemSave", QStringLiteral("Save as"), QStringLiteral("export"),
            QStringLiteral("Save the system design as a file of its own (.wetsys, Edraw's own) - it is kept in the drawing too"),
            [this] { if (systemView_) systemView_->saveDesignAs(); });
        big(w, row, "systemImport", QStringLiteral("Import"), QStringLiteral("load"),
            QStringLiteral("A system diagram from draw.io (.drawio) or Visio (.vsdx) - its boxes as units, the small squares on "
                           "their sides as ports, its lines as cables, its notes - in place of this one (Undo takes it back)"),
            [this] { if (systemView_) systemView_->importDesign(); });
        big(w, row, "systemOpenDesign", QStringLiteral("Open"), QStringLiteral("load"),
            QStringLiteral("Open a system design saved as a file of its own (.wetsys) - in place of this one (Undo takes it back)"),
            [this] { if (systemView_) systemView_->openDesign(); });
    });
    group(QStringLiteral("Export"), [&](QWidget* w, QHBoxLayout* row) {
        big(w, row, "systemExport", QStringLiteral("Cable list"), QStringLiteral("export"),
            QStringLiteral("The cable list - each cable, its part number, revision, ends, length and drawing - to a CSV file"),
            [this] { if (systemView_) systemView_->exportCableList(); });
        big(w, row, "systemExportDrawio", QStringLiteral("draw.io"), QStringLiteral("export"),
            QStringLiteral("The diagram for draw.io (diagrams.net) - units, ports, cables with their labels, and notes - a .drawio file"),
            [this] { if (systemView_) systemView_->exportDrawio(); });
        big(w, row, "systemExportVisio", QStringLiteral("Visio"), QStringLiteral("export"),
            QStringLiteral("The diagram for Microsoft Visio (2013 and on) - units, ports, cables with their labels, and notes - a .vsdx file"),
            [this] { if (systemView_) systemView_->exportVisio(); });
        big(w, row, "systemExportPdf", QStringLiteral("PDF"), QStringLiteral("export"),
            QStringLiteral("The diagram as a PDF - on a page the size of its template's sheet, or A3"),
            [this] { if (systemView_) systemView_->exportPdf(); });
        big(w, row, "systemExportPng", QStringLiteral("PNG"), QStringLiteral("export"),
            QStringLiteral("The diagram as a picture (PNG)"),
            [this] { if (systemView_) systemView_->exportPng(); });
    });
    lay->addStretch(1);
    ribbon_->addTab(page, QStringLiteral("System design"));
}

void MainWindow::showSystemDesign(bool on) {
    if (!systemView_ || !centralStack_ || on == systemOn_) return;
    systemOn_ = on;
    if (on) {
        // THE DRAWING'S PANELS PUT AWAY - properties, layers, the parts list... the system diagram has its own on the
        // right - and given back as they were when the tab is left.
        systemDocksHidden_.clear();
        for (QDockWidget* d : findChildren<QDockWidget*>(Qt::FindDirectChildrenOnly))
            if (d->isVisible() && !d->isFloating()) {
                systemDocksHidden_.append(d);
                d->hide();
            }
        centralStack_->setCurrentWidget(systemView_);
        systemView_->canvas()->setFocus();
    } else {
        if (centralStack_->currentWidget() == systemView_) centralStack_->setCurrentIndex(0);
        for (const QPointer<QDockWidget>& d : systemDocksHidden_)
            if (d) d->show();
        systemDocksHidden_.clear();
        canvas_->setFocus();
    }
}

} // namespace Edraw_Desktop
