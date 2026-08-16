#pragma once

#include "app/BomDialog.h" // BomConfigOpener
#include "app/Editor.h" // Editor::TextStyle used in a method signature
#include "harness/Bom.h"
#include "harness/MechModels.h"
#include "harness/PartsLibrary.h"
#include "model/Document.h"
#include "model/PageSetup.h"
#include "model/DrawingTemplate.h"   // tplBeingDrawn_ is held by value
#include "tools/ToolId.h"

#include <QColor>
#include <QHash>
#include <QIcon>
#include <QPoint>
#include <QSize>
#include <QMainWindow>
#include <QStringList>
#include <QVector>
#include <functional>
#include <map>
#include <memory>
#include <utility>
#include <vector>

class QLabel;
class QAction;
class QActionGroup;
class QTabWidget;
class QTabBar;
class QTableWidget;
class QListWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QCheckBox;
class QLineEdit;
class QComboBox;
class QMenu;
class QDockWidget;
class QWidget;
class QCloseEvent;
class QToolButton;
class QPlainTextEdit;
class QFontComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QButtonGroup;
class QHBoxLayout;
class QDialog;
class QShortcut;

namespace Edraw_Desktop {

class CanvasWidget;
class Editor;
class HarnessBuilderDialog;
class PartsLibraryDialog;
class BomDialog;
class VoltageDropDialog;
class HarnessStepsDialog;
class MechModelsDialog;
class MechanicalBuilderDialog;
class MechanicalValidationDialog;
class BundleDiameterDialog;
class ThermalModelDialog;
class TemplateDialog;
class BlockDialog;

// Top-level application window: ribbon, dockable panels (layers / properties /
// entity list), command bar, coordinate readout, and view/edit actions.
class MainWindow : public QMainWindow, public BomConfigOpener {
    Q_OBJECT
public:
    // `onModule` is called as each startup module is reached, so the splash can name it. The
    // names, in order, are startupModules().
    using ModuleReporter = std::function<void(const QString&)>;
    explicit MainWindow(QWidget* parent = nullptr, ModuleReporter onModule = {});
    // The startup steps, in the order they run. What the splash counts and shows.
    static QStringList startupModules();
    ~MainWindow() override;

    // Open a .dwx or .dxf by path (used by File>Open and the command line).
    void openPath(const QString& path);
    // Output: the model and the sheets, in a chosen order, onto a PDF or a printer. One entry point for
    // both, because a page is a page whichever device it lands on.
    void showOutput();
    // One page: the space `space` (0 = model, 1..n = layout n) painted over the whole of the painter's
    // viewport. Used for a PDF page, a printed page and, in the tests, an image.
    void renderSpaceToPage(QPainter& painter, int space, bool monochrome);
    // The size of paper a space wants, in millimetres: a sheet is its own size, the model is its extents
    // with a small margin. Returns false if there is nothing there to put out.
    bool spacePageSizeMm(int space, double& widthMm, double& heightMm) const;
    // What a space is called - "Model", or the layout's own name.
    QString spaceName(int space) const;
    // The pages, in order, into one PDF.
    bool writeSpacePdf(const QString& path, const std::vector<int>& spaces, bool monochrome,
                       bool fitToPage);

    // The menu on a layout's tab: what is done TO a sheet rather than on it.
    void showDrawingSpaceMenu(const QPoint& at);
    QWidget* drawingSpaceStrip_ = nullptr;   // the strip the tabs sit on, so its right-clicks stay put
    void duplicateLayout(int space);
    void renameLayout(int space);
    void printLayout(int space);
    void showPageSetup(int space);
    // The library, at the top of the canvas menu: pick a part and put its drawing down. `mechanical` says
    // which of the two drawings; the search dialog is for a library too long to walk through by hand.
    void addLibraryToCanvasMenu(QMenu& menu);
    void placeLibraryPart(const QString& partId, bool mechanical);
    // The fanout - the third drawing of a part - put down for the side it was drawn from, or mirrored for
    // the other one. `mirrored` turns a fanout drawn from the front into the back of the connector.
    void placeLibraryFanout(const QString& partId, bool mirrored);
    // A library part as a line of this drawing's bill of materials, one of it. Added BY HAND, which is what
    // makes it removable again: the lines the drawing itself calls for are the drawing's to say.
    void addPartToBom(const QString& partId);
    // The menu for a part in the panel: place its drawings, list it, edit it. Built rather than shown, so
    // what is on it can be asked for.
    QMenu* buildPartsPanelMenu(const QString& partId);
    // Take a line put there by hand off the bill of materials - `row` as the panel shows it.
    void removeBomPanelRow(int row);
    void findAndPlaceLibraryPart();
    // The page setup manager, opened fresh or coming back from picking a plot window. `resumeTarget` is
    // which setup was being edited (-1 = the layout's own) and `resumeWorking` what the user had set.
    void openPageSetupManager(int space, bool resume, int resumeTarget, const PageSetup& resumeWorking);
    // The page setup a space plots by: the layout's own, or - for model space, which has no sheet and so
    // no setup - one that fits the drawing to the paper.
    PageSetup pageSetupFor(int space) const;
    // Print these spaces, in order, on the printer chosen in the print dialog.
    bool printSpaces(const std::vector<int>& spaces);

    // Blocks, as operations the host can be asked to perform - each is also reachable by name from
    // the command line. Opening one for editing puts it in a tab of its own on a grey canvas.
    void openBlockEditor(const QString& name);
    void showBlockDialog();
    void saveBlockBeingEdited();
    void cancelBlockEdit();
    // Drawing the template itself: it opens on a tab of its own, tinted so it can never be mistaken
    // for a drawing, and is finished with Save template / Save as / Cancel.
    void openTemplateEditor(const QString& name);
    void saveTemplateBeingEdited(bool saveAs);
    void cancelTemplateEdit();
    // Pictures: a PNG/JPEG/BMP from a file, or whatever is on the clipboard - a screenshot, a copied
    // image file - dropped onto the sheet and then resized by its corners or in Properties.
    // A PDF into this drawing, as an editable block. Split from the menu command so the work can be asked
    // for with a path - by a test, or by anything else that already knows which file.
    bool importPdfFile(const QString& path, QString* said = nullptr);
    void insertPictureFromFile();
    bool insertPictureFromClipboard(bool quiet);
    void pasteFromClipboard();   // a picture if that is what is on the clipboard, else the drawing kind
    // The panels' arrangement and, above all, their size: kept between sessions, so a side pulled wider is
    // that wide the next time the program is started. Saved on the way out, read back while building.
    void savePanelLayout();
    void restorePanelLayout();

protected:
    void closeEvent(QCloseEvent* e) override;
    void showEvent(QShowEvent* e) override;   // the panels take their saved widths once there is a layout
    bool eventFilter(QObject* obj, QEvent* e) override;    // Esc in the PROPERTIES panel: back to Select, with the keyboard on the drawing again.
    void leavePropertiesPanel();
 // inline text editor keys

private:
    void buildRibbon();
    // One tab per file, in src/app/ribbon/. They are members, so each still reaches every widget and
    // helper it needs; splitting them up only stops MainWindow.cpp being where everything lands.
    void buildHomeTab();
    void buildModifyTab();
    void buildInsertTab();
    void buildAnnotationTab();
    // The View tab: which panels are on the screen and which side each is on.
    void buildViewTab();
    void setPanelsVisible(bool on);
    void applyPanelSizes();
    bool panelSizesApplied_ = false;
    // The panel a View-tab button stands for, by name: the ribbon is built before the panels, so the buttons
    // are wired by name and resolved when they are pressed.
    QDockWidget* panelByKey(const QString& key) const;
    void syncViewPanelButtons();
    void restyleViewPanelIcons();   // the View tab's panel glyphs, drawn again in the current scheme
    QIcon schemeIcon() const;       // the sun/moon glyph for the scheme button, in the scheme now set
    QToolButton* schemeButton_ = nullptr;
    // The two panels the View tab adds: the parts of the library, and the blocks, as lists to place from.
    QDockWidget* makePartsPanel();
    QDockWidget* makeBlocksPanel();
    void refreshPartsPanel();
    // Which of the connected libraries the parts panel draws from. Remembered by path, so connecting
    // another library does not disturb what is in view.
    void fillPartsLibraryMenu(QMenu* menu);
    bool hasSchematicDrawing(const QString& partId) const;   // its own symbol, or the standard one
    void showPartsPanelMenu(const QPoint& at);   // right-click on a part: place it, list it, edit it
    void showBomPanelMenu(const QPoint& at);     // right-click on a line: take it off, if it is yours
    QStringList partsPanelHidden_;
    QLabel* partsPanelCount_ = nullptr;
    // The two rules the panel puts within reach, both of them the drawing's own (they are kept in its bill
    // of materials, and the bill-of-materials window shows the same two switches).
    QCheckBox* bomOnPlaceBox_ = nullptr;
    QCheckBox* bomUnifyBox_ = nullptr;
    void syncBomRuleBoxes();                       // the boxes say what the drawing is set to
    void setBomRule(bool onPlace, bool value);     // and setting one is setting the drawing's own rule
    void refreshBlocksPanel();
    void buildSchematicBuildTab();
    void buildHarnessMechanicalTab();
    // Shared by every tab: a page with its group layout, a titled group with a rule after it, and
    // the match-properties button that two tabs both carry.
    std::pair<QWidget*, QHBoxLayout*> makeTabPage();
    void addGroup(QHBoxLayout* lay, const QString& title, QWidget* content);
    // The same, handing back the group and its divider so a contextual tab can fold them away.
    QWidget* addGroupKeep(QHBoxLayout* lay, const QString& title, QWidget* content);
    QToolButton* makeMatchBtn(QWidget* parent, bool iconOnly);
    // Undo and Redo as ribbon buttons. Ctrl+Z and Ctrl+Y have always worked; a button is what makes
    // them findable, and it can say whether there is anything left to undo.
    // Undo and Redo. `compact` makes the small icon-only pair that sits in the ribbon's top right
    // corner - where a hand reaches for them, and out of the way of the tools.
    QWidget* undoRedoRow(QWidget* parent, bool compact = false);
    // The stock electronic parts, as one button with a menu of them: the mechanical drawings on the
    // mechanical tab, the symbols on the schematic tab. Choosing one hands it to the cursor, so it is
    // dropped by a click - and turned with R first, like anything else being placed.
    QToolButton* makePartSymbolButton(QWidget* parent, bool schematic);
    // The stock symbols as a small icon each, in two rows: a symbol is picked by its shape, so the shapes
    // are on the ribbon rather than behind a menu of names.
    QWidget* makePartSymbolRow(QWidget* parent, bool schematic);
    QVector<QPair<int, QToolButton*>> partSymbolButtons_;   // drawn icons: re-drawn per scheme
    // Show/hide the Schematic-build ribbon groups by context: while a twist is armed only
    // the Twist group shows; while a Splice/X marker tool is armed the Modify/DRC groups hide.
    void updateSchematicRibbonGroups();
    void buildHatchRibbonTab();        // build the contextual "Hatch Creation" tab (hidden)
    void showHatchRibbon(bool on);     // add/select or remove the contextual tab
    void syncHatchRibbon();            // reflect the current HatchStyle in the tab controls
    void updateHatchContext();         // open/close + target the tab based on tool/selection
    void applyHatchControl(const QString& key, const QJsonValue& value); // -> selected hatch or default
    void choosePattern(const QString& key); // pick a hatch pattern (edit target or new-hatch default)
    void showHatchPatternDialog();          // expanded pattern picker (gallery + name list)
    void buildTableCellRibbonTab();    // build the contextual "Table Cell" tab (hidden)
    void showTableCellRibbon(bool on); // add/select or remove the contextual tab
    void updateTableCellContext();     // open/close based on the table-cell selection
    void buildConnectorRibbonTab();    // build the contextual "Connector build" tab (hidden)
    void showConnectorRibbon(bool on); // add/select or remove the contextual tab
    void updateConnectorContext();     // open/close: Connector tool active OR a connector selected
    void buildWireRibbonTab();         // build the contextual "Wire" tab (hidden)
    void showWireRibbon(bool on);      // add/select or remove the contextual Wire tab
    void buildArrayRibbonTab();        // build the contextual "Array Creation" tab (hidden)
    // The contextual "Template" tab: the sheet, the frame, the revision table and the save buttons,
    // shown only while a template is being drawn.
    void buildTemplateRibbonTab();
    void showTemplateRibbon(bool on);
    void syncTemplateRibbon();         // reflect the template being drawn in the tab's fields
    void applyTemplateRibbon();        // and the other way, when one of them is changed
    void showHarnessRibbonTabs(bool on);   // the harness tabs are put away while drawing a template
    // A bigger sheet gets a BIGGER ruler, not more of it: the division is rescaled so the same number
    // of zones spans the new sheet.
    void keepTemplateZoneCount(const TemplateSheet& from, const TemplateSheet& to);
    // Exactly one ribbon page visible: the one whose tab is selected. See the definition for why this
    // has to be enforced rather than left to the tab widget.
    void normaliseRibbonPages();
    void updateModelRibbonTabs();
    // The contextual "Capture" tab: shown while the library window has stepped aside so objects can be
    // selected for a part's drawing. Enter finishes it; Esc drops it.
    void buildCaptureRibbonTab();
    void showCaptureRibbon(bool on, const QString& what = QString(), bool mechanical = false);
    // Who the capture is FOR. The ribbon's Take / Cancel - and Enter / Esc - belong to whichever window
    // armed it: the parts library capturing a part's drawing, or the mechanical models capturing a model.
    // Wired to the library alone, the same ribbon could not serve the second one at all.
    std::function<void()> captureTake_;
    std::function<void()> captureDrop_;
    void armCapture(const QString& what, const QString& which, std::function<void()> take,
                    std::function<void()> drop);
    void disarmCapture();
    // Stamp the frame with ids and remember it as generated, so every line of it is an ordinary
    // object that can be picked, moved and reshaped - and an edited one is kept, not regenerated.
    void adoptGeneratedFrame(std::vector<Entity>& ents, bool markAll);
    void showArrayRibbon(bool on);     // add/select or remove the contextual Array tab
    void updateArrayContext();         // open/sync/close it with the array session
    void beginArrayCommand(ArrayKind kind); // arm an array of that kind on the selection
    void buildPanels();
    void buildStatusControls();
    void buildCommandBar();
    void buildMenus();
    void buildDocumentArea(); // tab bar + canvas
    void activateDrawingSpace(int index, bool fitToSpace = true);
    // Close a layout. Model is not one and has no x on it.
    void closeDrawingSpace(int index);
    // The scale the layout showing is plotted at: what one millimetre of paper is worth in the model.
    // It is the layout's own - a viewport property, kept with the drawing - not a setting of the window.
    void setDrawingSpaceScale(double paperPerModel);
    // Cut a window onto the drawing in the sheet showing.
    void addLayoutWindow();
    // Stepped into a window on a layout, or back out: which document the editor works in follows.
    void enteredLayoutViewport(bool inside);

    // Paper space. A layout is drawn on with the same tools as the model, and the way that is had without
    // a second set of tools is to give the sheet a document of its own: the editor is pointed at it and
    // draws, selects, moves, snaps, dimensions and undoes on the sheet exactly as it does on the drawing,
    // because as far as it knows it IS a drawing. The canvas keeps the drawing itself in hand separately
    // (setDrawingDocument) for the layouts and for what the windows look at.
    Document* paperSpaceFor(Document* drawing, int layoutIndex);
    // Write what is on the sheet back into the layout it belongs to. Called on the way out of a layout
    // and before the drawing is saved, so what was drawn on the paper is in the file.
    void flushPaperSpace();
    // The document the tools and the selection are working in: the drawing in model space, the sheet's
    // paper space on a layout.
    Document* editDoc() const;
    // Drop the paper spaces of a drawing that is being closed.
    void forgetPaperSpaces(Document* drawing);
    void syncDrawingSpaceTabs();
    void addDrawingLayout();
    void applyLayoutTemplate();

    // Multi-document management.
    Document* addDocument(std::unique_ptr<Document> doc, const QString& path);
    void activateIndex(int index);   // make tab `index` the active drawing
    void closeDocument(int index);
    bool maybeSaveDoc(int index);    // prompt to save if dirty; false => cancel
    bool saveDocument(int index, bool forceDialog = false);
    void updateActiveTabText();
    void updateWindowTitleForActive();
    int activeIndex() const;

    QAction* makeToolAction(const QString& text, ToolId id);
    QWidget* ribbonGroup(const QString& title, QWidget* content);
    // The tools as buttons, packed into TWO ROWS - and anything else that belongs in the same group packed
    // with them, so a group is two tidy rows rather than one tall one with odds and ends beside it.
    QWidget* toolRow(const QList<ToolId>& tools, const QList<QWidget*>& alsoInTheGroup = {});
    // One square for every icon button on the ribbon: 28 px of icon, room for a split button's arrow beside
    // it, and the same height whichever kind of button it is. Sizes left to Qt come out of each button's own
    // contents, and a group of mixed buttons then has no grid in it at all.
    static constexpr QSize kRibbonCell{46, 36};
    // A 2-row icon grid of the Modify "Operations" actions (Array, Explode,
    // Purge, Delete). Reused on both the Home and Modify tabs.
    QWidget* operationsRow();
    // The Array button, with its flyout of the three array laws. It sits with the MODIFY tools - an
    // array is a way of copying something about the drawing, which is what those tools do - rather than
    // among Operations, where it was the only thing that draws anything.
    QToolButton* makeArrayButton(QWidget* parent);
    // Clipboard group: a Copy button (copies the selection) whose fall-menu holds Cut.
    // iconOnly drops the "Copy"/"Cut" caption so the button matches the plain icon
    // buttons it sits with (Home); the name then shows on hover instead.
    QWidget* clipboardRow(bool iconOnly = false);
    // AutoCAD-style Insert Table dialog → sets the table spec + arms the Table tool.
    void insertTableCommand();

    void refreshPanels();
    void refreshLayers();
    void refreshProperties();
    void refreshEntityList();
    void refreshBomPanel();
    // A Properties-panel field edit rebuilds the whole panel, which would destroy the
    // field the user just clicked into. Run the apply deferred, then re-focus the field
    // now at the same row — so a single click moves editing straight to the new field.
    void deferredPropApply(std::function<void()> apply);
    int focusedPropsRow() const;      // row of the propsTable_ cell widget that has focus (-1 none)
    void refocusPropsRow(int row);    // focus (and select) the value widget at that row

    void runCommand(const QString& text);
    void submitCommand(const QString& text); // shared by command bar + dynamic input

    // AutoCAD-style command registry (drives both command input and the help popup).
    struct Command {
        QStringList names;   // first is the canonical name; rest are aliases
        QString category;    // Draw / Modify / Harness / Edit / View / File
        QString desc;
        std::function<void()> run;
    };
    void buildCommands();
    void showHelp();
    QVector<Command> commands_;
    // Quick Select (AutoCAD QSELECT): the filter dialog, seeded from `sampleId` so the
    // property list is the one that applies to that object.
    void showQuickSelectDialog(const QString& sampleId);
    void promptFilletRadius();
    void promptChamferDistance();

    // The Harness Builder (modeless, one instance): describe a harness, then Generate draws
    // it. Kept alive between openings so a part-built definition is not lost on Close.
    void showHarnessBuilder();
    // The parts library: the raw materials a harness is built from, with a schematic symbol and a
    // mechanical drawing per part. Modeless and kept alive, like the builder.
    void showPartsLibrary();
    // The library, opened with a part already picked: what "see it in the library" asks for.
    void showPartsLibraryAt(const QString& libPartId);
    // BomConfigOpener: the bill-of-materials dialog's Configuration button opens Options on its
    // own page, so every setting lives in one place.
    void openBomConfiguration() override { showOptions(QStringLiteral("BOM")); }
    // The ribbon button that opens the library. Both harness tabs carry it, so it is built once.
    QToolButton* makePartsLibraryButton(QWidget* parent);
    // Likewise the voltage-drop button: the question gets asked from the schematic side and checked
    // again on the mechanical side, so both tabs carry it.
    QToolButton* makeVoltageDropButton(QWidget* parent);
    HarnessBuilderDialog* harnessDlg_ = nullptr;
    PartsLibraryDialog* libraryDlg_ = nullptr;
    QWidget* captureTab_ = nullptr;
    QLabel* captureWhat_ = nullptr;
    // Enter takes the selection, Esc drops the capture - live ONLY while one is waiting, so neither
    // key is taken away from the drawing tools the rest of the time.
    QShortcut* captureTakeSc_ = nullptr;
    QShortcut* captureDropSc_ = nullptr;
    BomDialog* bomDlg_ = nullptr;
    VoltageDropDialog* voltageDropDlg_ = nullptr;
    MechanicalBuilderDialog* mechDlg_ = nullptr;
    HarnessStepsDialog* stepsDlg_ = nullptr;
    MechModelsDialog* modelsDlg_ = nullptr;
    // Which open documents are template-edit sessions, and the template each is drawing. A template
    // edit is a document like a block edit is: its own tab, its own undo, its own canvas colour.
    QHash<Document*, QString> templateEdits_;
    QHash<Document*, QString> templateEditPaths_;
    // Every Save template / Save as / Cancel button there is. They live in one place - the Template
    // group on Home - and that whole group is only THERE while a template is open, so a drawing's
    // ribbon is not carrying three dead buttons about a template nobody is editing.
    QVector<QToolButton*> tplSessionBtns_;
    QWidget* tplHomeGroup_ = nullptr;   // the Home "Template" group, shown only during a session
    QWidget* templateTab_ = nullptr;
    // Tabs taken out of the ribbon while a template is being drawn, with where each came from so it
    // goes back exactly where it was. A sheet is not a harness: the harness tabs mean nothing here.
    struct StashedTab {
        QWidget* page = nullptr;
        QString title;
        int at = 0;
    };
    QVector<StashedTab> stashedTabs_;
    // The generated frame as it was last generated, by id, so an object the user has since CHANGED is
    // recognised and kept instead of being regenerated over. Free design has to survive a parameter
    // change; anything untouched is still the generator's to replace.
    QHash<QString, QJsonObject> tplGeneratedAsMade_;
    // When an image last arrived on the SYSTEM clipboard. A paste takes whichever is newer, that or
    // the application's own clipboard, so Ctrl+V after a screenshot pastes the screenshot and Ctrl+V
    // after copying two lines pastes the lines.
    qint64 sysClipImageAt_ = 0;
    QComboBox* tplSheetBox_ = nullptr;
    QCheckBox* tplZonesChk_ = nullptr;
    QDoubleSpinBox* tplZoneSizeSpin_ = nullptr;
    QCheckBox* tplRevChk_ = nullptr;
    QSpinBox* tplRevRowsSpin_ = nullptr;
    QLabel* tplRibbonInfo_ = nullptr;
    bool tplRibbonSyncing_ = false;
    // The template being drawn, so the ribbon can change it and the save can write it.
    DrawingTemplate tplBeingDrawn_;
    bool templateDlgWasOpen_ = false;
    MechanicalValidationDialog* mechCheckDlg_ = nullptr;
    BundleDiameterDialog* bundleDlg_ = nullptr;
    ThermalModelDialog* thermalDlg_ = nullptr;
    TemplateDialog* templateDlg_ = nullptr;
    BlockDialog* blockDlg_ = nullptr;
    // Which open documents are block-edit sessions, and which block each one is editing. A block
    // being edited is a document of its own - that is what gives it its own undo, its own tab and a
    // canvas of its own colour, without a bespoke editing mode to get wrong.
    QHash<Document*, QString> blockEdits_;
    QColor normalCanvasColor_{"#1e2832"};
    QToolButton* saveBlockBtn_ = nullptr;
    QToolButton* cancelBlockBtn_ = nullptr;
    // Whether the Blocks window was open when a block edit started, so it can be put back.
    bool blockDlgWasOpen_ = false;
    QHash<Document*, MechModelKind> mechModelEdits_;
    QHash<Document*, MechModel> mechModelSessionModels_;
    QWidget* mechModelSessionGroup_ = nullptr;
    QToolButton* captureMechModelBtn_ = nullptr;
    QToolButton* markNearMechModelBtn_ = nullptr;
    QToolButton* markFarMechModelBtn_ = nullptr;
    QToolButton* cancelMechModelBtn_ = nullptr;

    void showConnectorDialog();
    // Populate the Schematic-build "Connector build" ribbon (Identity / Pins / Body /
    // Text / Actions), mirroring the Edraw web connector builder.
    void buildConnectorBuildTab(QWidget* page, QHBoxLayout* lay);
    void showBomDialog();
    void showVoltageDropDialog();
    void showMechanicalBuilder();
    // The same harness, asked for one step at a time - the way it is built.
    void showHarnessSteps();
    // The default mechanical models: the house style, drawn once.
    void showMechModels();
    // Mark (or unmark) the selection as the part's connection line.
    void markConnectionLine();
    void showMechanicalValidation();
    // The Tools group: the calculations, on both harness tabs because they are asked on one side and
    // checked on the other.
    void showBundleDiameter();
    void showThermalModel();
    // The template module: what a new drawing starts as.
    void showTemplateDialog();
    void openMechModelEditor(MechModelKind kind);
    void armMechModelAnchor(bool near);
    void armMechModelConnect();          // the connection point, and the direction the part runs from it
    QToolButton* markConnectMechModelBtn_ = nullptr;
    void saveMechModelBeingEdited();
    void cancelMechModelEdit();
    bool saveMechModelDocument(Document* doc);

    // Blocks.
    enum class BlockAction { Create, Insert, Manage, SaveEdit, CancelEdit };
    QToolButton* makeBlockButton(QWidget* parent, BlockAction action);
    void createBlockFromSelection();
    // Close the block's tab and give the Blocks window back, however the edit ended.
    void endBlockEdit();
    void endTemplateEdit();
    QToolButton* makeTemplateSessionButton(QWidget* parent, int which);
    QToolButton* makeMechanicalValidationButton(QWidget* parent);
    QToolButton* makeBundleDiameterButton(QWidget* parent);
    QToolButton* makeThermalButton(QWidget* parent);
    // The ribbon button that opens it. The mechanical tab carries it; so does the schematic one,
    // because that is where the harness it reads from is drawn.
    QToolButton* makeHarnessStepsButton(QWidget* parent);
    QToolButton* makeMechModelsButton(QWidget* parent);
    QToolButton* makeMechanicalBuilderButton(QWidget* parent);
    // Run the schematic design-rule check and (re)open the validation window. On a
    // fresh open (revalidate=false) the current discrepancies become the tracked set;
    // on Revalidate (true) that set is kept so issues now fixed can be shown ticked.
    void showValidation(bool revalidate = false);
    void showBypassTable();    // list discrepancies the user has bypassed (accepted)
    void showValidationRules(); // choose which design-rule checks are active
    // Per-pin AWG/mm² + ampere editor for the selected connector (opened from Properties).
    void showPinTable(const QString& connectorId);
    // Center + flash-highlight the object carrying a discrepancy (and select it).
    void locateIssue(const ValidationIssue& issue);

    void newDrawing();
    void openDrawing();
    void saveDwxAs();
    void exportDxfAs();
    void importPdf();
    // The block name a PDF import takes: the file's own name, or the next number after it when that name
    // is already a block.
    QString blockNameForImport(const QString& base) const;
    // Export the drawing to PDF. windowSelect=true first lets the user drag a
    // rectangular print window on the canvas; otherwise the full drawing extents.
    void exportPdf(bool windowSelect);
    // The export asks whether strokes should retain their drawing colours or be
    // converted to black; both modes always use an opaque white page.
    bool choosePdfLineMode(bool& monochrome);
    void exportPdfRegion(const Vec2& a, const Vec2& b); // render a world rect to a PDF page
    void exportTableCsv(); // export the selected table's cells to a .csv file

    // Rich text editor dialog (font / size / bold / italic / case; justification,
    // line spacing, bullets/numbering; multiline for MText). Returns false if
    // cancelled; otherwise fills text + style in/out.
    bool showTextDialog(bool multiline, QString& text, Editor::TextStyle& style);
    // On-canvas in-place text editor: a text box + floating options bar + ruler,
    // overlaid on the canvas at the insertion box. Commit applies to the entity.
    void beginInlineText(const QString& id, const QString& text, const Editor::TextStyle& style,
                         Vec2 topLeft);
    void commitInlineText(bool apply);
    void layoutInlineText(); // reposition overlay (e.g. after the bar resizes)
    void showCanvasMenu(const QPoint& globalPos); // right-click context menu on the canvas
    void findText();             // Find: locate & select text entities by content
    // AutoCAD-style Options dialog (E menu). `page` names the tab to open on ("BOM" for the bill
    // of materials configurator), so a dialog elsewhere can send the user straight to it.
    void showOptions(const QString& page = QString());
    void showAbout();            // app name / description / version (E menu)
    // Light-on-dark tooltips, so a hint is readable against this window.
    void loadSettings();         // restore persisted drafting/snap toggles
    void saveSettings();         // persist drafting/snap toggles on close
    void applyTheme(bool dark);  // re-theme chrome + canvas (Dark / Light)
    // The ribbon's own sheet, in the scheme now set. One function, called when the ribbon is built and again
    // when the scheme changes - written out twice, the two would drift apart.
    QString ribbonStyleSheet() const;
    void restyleRibbonCorner();        // the tab row's corner, where Undo and Redo sit
    void restyleDrawingSpaceStrip();   // Model / the sheets / + / the scale, under the drawing
    QWidget* ribbonCorner_ = nullptr;
    QVector<QToolButton*> undoRedoBtns_;    // drawn icons, so they are drawn again per scheme
    QVector<QToolButton*> spaceStripBtns_;  // the + and its fall-menu twin on the space strip

    std::vector<std::unique_ptr<Document>> docs_; // open drawings
    // One paper space per (drawing, layout): kept for as long as the drawing is open, so leaving a sheet
    // and coming back finds the sheet as it was, view and all.
    std::map<std::pair<Document*, int>, std::unique_ptr<Document>> paperDocs_;
    std::pair<Document*, int> paperLoaded_{nullptr, -1}; // which one the editor is in, so it can be saved

    QStringList docPaths_;                         // parallel file paths ("" if unsaved)
    Document* doc_ = nullptr;                      // active drawing (raw, owned by docs_)
    QTabBar* docTabs_ = nullptr;
    QTabBar* drawingSpaceTabs_ = nullptr;
    QComboBox* spaceScaleBox_ = nullptr;
    QLabel* spaceScaleLabel_ = nullptr;
    QToolButton* addViewportBtn_ = nullptr;
    // Whether the grid is on, per drawing space, remembered as it is switched away from: a grid wanted
    // over a sheet being laid out is not the grid wanted over the harness itself, and the button that
    // turns it on belongs to the space it was pressed in. (The plot scale is not here - it belongs to
    // the layout and is kept with the drawing.)
    QHash<int, bool> spaceGrid_;
    // The scale each space was left at, by tab index. On a layout that is the plot scale (which is kept on the
    // drawing as well, on the window); in model space it is the scale the drawing will be plotted at, which
    // decides how big the dimensions are drawn.
    QHash<int, double> spaceScale_;
    int lastDrawingSpace_ = -1;   // which space is showing, so what it had is kept on the way out
    Editor* editor_ = nullptr;
    CanvasWidget* canvas_ = nullptr;
    QDialog* validationDlg_ = nullptr; // modeless "Schematic validation" window (if open)
    QDialog* bypassDlg_ = nullptr;     // modeless "Bypassed discrepancies" window (if open)
    QDialog* rulesDlg_ = nullptr;      // modeless "Validation rules" chooser (if open)
    std::vector<ValidationIssue> validationSnapshot_; // issues tracked in the open window
    QTableWidget* validationTable_ = nullptr;  // the open validation window's table (in-place refresh)
    QLabel* validationHeader_ = nullptr;       // its header label
    std::vector<ValidationIssue> validationRows_; // rows currently shown (for click → locate)
    bool crossSectionAwg_ = false;     // wire cross-section notation: false = mm², true = AWG
    bool applyingPropEdit_ = false;    // a Properties field edit is being applied (allow rebuild)

    QActionGroup* toolGroup_ = nullptr;
    QHash<int, QAction*> toolActions_;
    QVector<QAction*> allToolActions_; // every tool button instance (for theme re-tint)
    // Modify "Operations" icon buttons (key + button) for theme re-tinting.
    QVector<QPair<QString, QToolButton*>> opButtons_;
    // Every "Match properties" button instance (Home + Schematic build): they latch
    // together while the command is armed, so it is obvious it is waiting for clicks.
    QVector<QToolButton*> matchBtns_;

    // Status-bar drafting toggles (re-tinted/re-styled on theme change).
    QVector<QToolButton*> statusToggleBtns_;
    QStringList statusToggleNames_;
    QVector<QToolButton*> statusArrowBtns_;

    QTabWidget* ribbon_ = nullptr;
    // Contextual "Hatch Creation" ribbon tab (shown only while hatching).
    QWidget* hatchTab_ = nullptr;
    QWidget* tableCellTab_ = nullptr;
    QWidget* connectorTab_ = nullptr;
    QWidget* wireTab_ = nullptr;                 // contextual "Wire" ribbon (Wire tool active)
    // Contextual "Array Creation" ribbon (shown while an array is being laid out). Each
    // group is remembered with its separator rule and the array kinds it belongs to, so
    // switching Type swaps the visible parameters. syncArrayRibbon_ reloads every control
    // from the live spec; arraySyncing_ guards against that being read as a user edit.
    QWidget* arrayTab_ = nullptr;
    struct ArrayGroupVis {
        QWidget* group = nullptr;
        QWidget* rule = nullptr;
        bool rect = false;
        bool polar = false;
        bool path = false;
    };
    QVector<ArrayGroupVis> arrayGroups_;
    std::function<void()> syncArrayRibbon_;
    bool arraySyncing_ = false;
    QWidget* markerGroup_ = nullptr;             // Schematic-build "Marker" size group (contextual)
    // The break mark's own fields, shown while its tool is armed: what a break looks like is decided
    // as it is placed, so the numbers belong beside the button rather than in a dialog.
    QWidget* breakOptionsGroup_ = nullptr;
    // The twisted bundle's own setting: how many wires it fans out to. Shown only while that tool is
    // armed, like everything else on this tab (see updateMechanicalRibbonGroups).
    QWidget* twistOptionsGroup_ = nullptr;
    QSpinBox* twistWiresSpin_ = nullptr;
    // The Harness-mechanical groups, so placing a break can take the tab over the way the twist tool
    // takes over Schematic build: while it is armed the tab shows the mark's own numbers and nothing
    // else, because nothing else applies until it is placed.
    QVector<QWidget*> mechGroups_;
    QVector<QWidget*> mechModelHiddenGroups_;
    QWidget* mechBreakGroup_ = nullptr;
    void updateMechanicalRibbonGroups();
    QDoubleSpinBox* breakGapSpin_ = nullptr;
    QDoubleSpinBox* breakSizeSpin_ = nullptr;
    QDoubleSpinBox* breakAngleSpin_ = nullptr;
    QLabel* breakSizeLabel_ = nullptr;
    void syncBreakOptions();
    QLabel* markerLabel_ = nullptr;              // its label ("Joint ⌀" / "X size")
    QDoubleSpinBox* markerSpin_ = nullptr;       // its size spin (edits the active marker tool)
    // Schematic-build Modify / DRC groups (+ their separator rules): hidden while the
    // Splice or Termination tool is armed so only the marker preferences show.
    QWidget* schemModifyGroup_ = nullptr;
    QWidget* schemModifyRule_ = nullptr;
    QWidget* schemDrcGroup_ = nullptr;
    QWidget* schemDrcRule_ = nullptr;
    // Schematic-build "Place" group (+ rule) and the contextual "Twist" parameter group
    // (+ rule): while a twist is armed the ribbon shows ONLY the Twist group; otherwise the
    // Twist group is hidden and the Place / Modify / DRC groups show.
    QWidget* schemPlaceGroup_ = nullptr;
    QWidget* schemPlaceRule_ = nullptr;
    QWidget* schemTwistGroup_ = nullptr;
    QWidget* schemTwistRule_ = nullptr;
    QStringList customWireColors_;               // user-added wire colours (persisted)
    QStringList insulationMaterials_;            // insulation material menu (persisted, editable)
    QToolButton* connectorPlaceBtn_ = nullptr;  // "Place connector" — checked while armed
    std::function<void()> syncConnectorRibbon_; // reload the connector tab from context
    bool connectorSyncing_ = false;             // guard: skip apply() while syncing widgets
    QButtonGroup* hatchPatternGroup_ = nullptr; // exclusive pattern swatch buttons
    QDoubleSpinBox* hatchAngleSpin_ = nullptr;
    QDoubleSpinBox* hatchScaleSpin_ = nullptr;
    QSpinBox* hatchTranspSpin_ = nullptr;
    QToolButton* hatchAssocBtn_ = nullptr;
    QToolButton* hatchJointsBtn_ = nullptr; // many-joints toggle
    QToolButton* hatchPickBtn_ = nullptr;   // boundary: Pick Points
    QToolButton* hatchSelectBtn_ = nullptr; // boundary: Select object
    QComboBox* hatchColorCombo_ = nullptr;
    QLabel* hatchPatternLabel_ = nullptr; // shows the selected pattern's name
    QString hatchEditId_; // id of the selected hatch being edited (empty = new-hatch defaults)
    QTableWidget* layersTable_ = nullptr;
    // The panels, kept so the View tab can show, hide and move them. They were locals: a panel closed by its
    // own x could then only be brought back by knowing that dragging a floating one to an edge re-docks it.
    QDockWidget* propsDock_ = nullptr;
    QDockWidget* layersDock_ = nullptr;
    QDockWidget* bomDock_ = nullptr;
    QDockWidget* partsDock_ = nullptr;
    QDockWidget* blocksDock_ = nullptr;
    QList<QPair<QString, QToolButton*>> viewPanelButtons_;
    // The shelf, as a table: the part, and a column each for the mechanical drawing, the schematic symbol
    // and the fanout. A tree rather than a list because several libraries in view are shown under their own
    // names, and because the three drawings want columns of their own to be read down.
    QTreeWidget* partsPanelTree_ = nullptr;
    QLineEdit* partsPanelFilter_ = nullptr;
    QListWidget* blocksPanelList_ = nullptr;

    QTableWidget* propsTable_ = nullptr;
    QListWidget* entityList_ = nullptr;   // no longer built: a click on the drawing says it better
    // The bill of materials shown beside the drawing (read-only; the window edits it), and the library
    // it needs to name the parts a drawing only carries the ids of. Opened once, on first use.
    QTableWidget* bomPanel_ = nullptr;
    PartsCatalog bomCatalog_;
    PartsLibrary bomLib_;
    bool bomLibLoaded_ = false;
    QLineEdit* commandEdit_ = nullptr;
    QLabel* coordLabel_ = nullptr;
    QLabel* cmdHintLabel_ = nullptr; // applicable commands for the active draw tool
    QToolButton* repeatBtn_ = nullptr; // status-bar "repeat last command" button
    int inlineCell_ = -1;              // table cell being inline-edited (-1 = not a cell)
    int inlineConnectorPin_ = -1;      // connector pin label being inline-edited (-1 = none)
    QString inlineConnectorId_;        // the connector whose pin label is being edited
    bool inlineSingleLine_ = false;    // single-value edit (balloon number/cell): Enter commits

    // On-canvas text editor overlay (children of the canvas).
    QPlainTextEdit* inlineEdit_ = nullptr;
    QWidget* inlineBar_ = nullptr;
    QWidget* inlineRuler_ = nullptr;
    QString inlineEditId_;          // empty => placing a new text
    Editor::TextStyle inlineStyle_; // live style (incl. box) edited by the bar/ruler
    Vec2 inlineTopLeft_;
    QFontComboBox* inlineFont_ = nullptr;
    QDoubleSpinBox* inlineSize_ = nullptr;
    QToolButton* inlineBold_ = nullptr;
    QToolButton* inlineItalic_ = nullptr;
    QToolButton* inlineUnderline_ = nullptr;
    QToolButton* inlineStrike_ = nullptr;
    QComboBox* inlineJust_ = nullptr;
    QComboBox* inlineList_ = nullptr;
    QDoubleSpinBox* inlineSpacing_ = nullptr;
    QComboBox* layerCombo_ = nullptr;
    QComboBox* colorCombo_ = nullptr;

    bool refreshing_ = false; // guard against re-entrant table edits
    bool darkTheme_ = true;   // current color scheme
};

} // namespace Edraw_Desktop
