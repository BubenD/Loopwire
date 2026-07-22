#pragma once

#include "app/Editor.h" // Editor::TextStyle used in a method signature
#include "model/Document.h"
#include "tools/ToolId.h"

#include <QHash>
#include <QMainWindow>
#include <QStringList>
#include <QVector>
#include <functional>
#include <memory>
#include <vector>

class QLabel;
class QAction;
class QActionGroup;
class QTabWidget;
class QTabBar;
class QTableWidget;
class QListWidget;
class QLineEdit;
class QComboBox;
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

namespace Edraw_Desktop {

class CanvasWidget;
class Editor;

// Top-level application window: ribbon, dockable panels (layers / properties /
// entity list), command bar, coordinate readout, and view/edit actions.
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Open a .dwx or .dxf by path (used by File>Open and the command line).
    void openPath(const QString& path);

protected:
    void closeEvent(QCloseEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* e) override; // inline text editor keys

private:
    void buildRibbon();
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
    void buildPanels();
    void buildStatusControls();
    void buildCommandBar();
    void buildMenus();
    void buildDocumentArea(); // tab bar + canvas

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
    QWidget* toolRow(const QList<ToolId>& tools);
    // A 2-row icon grid of the Modify "Operations" actions (Array, Explode,
    // Purge, Delete). Reused on both the Home and Modify tabs.
    QWidget* operationsRow();
    // Clipboard group: a Copy button (copies the selection) whose fall-menu holds Cut.
    QWidget* clipboardRow();
    // AutoCAD-style Insert Table dialog → sets the table spec + arms the Table tool.
    void insertTableCommand();

    void refreshPanels();
    void refreshLayers();
    void refreshProperties();
    void refreshEntityList();
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
    void arrayDialog();
    void promptFilletRadius();
    void promptChamferDistance();

    void showConnectorDialog();
    // Populate the Schematic-build "Connector build" ribbon (Identity / Pins / Body /
    // Text / Actions), mirroring the Edraw web connector builder.
    void buildConnectorBuildTab(QWidget* page, QHBoxLayout* lay);
    void showBomDialog();
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
    // Export the drawing to PDF. windowSelect=true first lets the user drag a
    // rectangular print window on the canvas; otherwise the full drawing extents.
    void exportPdf(bool windowSelect);
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
    void showOptions();          // AutoCAD-style Options dialog (E menu)
    void showAbout();            // app name / description / version (E menu)
    void loadSettings();         // restore persisted drafting/snap toggles
    void saveSettings();         // persist drafting/snap toggles on close
    void applyTheme(bool dark);  // re-theme chrome + canvas (Dark / Light)

    std::vector<std::unique_ptr<Document>> docs_; // open drawings
    QStringList docPaths_;                         // parallel file paths ("" if unsaved)
    Document* doc_ = nullptr;                      // active drawing (raw, owned by docs_)
    QTabBar* docTabs_ = nullptr;
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
    QWidget* markerGroup_ = nullptr;             // Schematic-build "Marker" size group (contextual)
    QLabel* markerLabel_ = nullptr;              // its label ("Splice ⌀" / "X size")
    QDoubleSpinBox* markerSpin_ = nullptr;       // its size spin (edits the active marker tool)
    // Schematic-build Modify / DRC groups (+ their separator rules): hidden while the
    // Splice or Termination tool is armed so only the marker preferences show.
    QWidget* schemModifyGroup_ = nullptr;
    QWidget* schemModifyRule_ = nullptr;
    QWidget* schemDrcGroup_ = nullptr;
    QWidget* schemDrcRule_ = nullptr;
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
    QTableWidget* propsTable_ = nullptr;
    QListWidget* entityList_ = nullptr;
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
