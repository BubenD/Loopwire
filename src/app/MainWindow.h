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

    void refreshPanels();
    void refreshLayers();
    void refreshProperties();
    void refreshEntityList();

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
    void showBomDialog();

    void newDrawing();
    void openDrawing();
    void saveDwxAs();
    void exportDxfAs();

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

    QActionGroup* toolGroup_ = nullptr;
    QHash<int, QAction*> toolActions_;
    QVector<QAction*> allToolActions_; // every tool button instance (for theme re-tint)

    // Status-bar drafting toggles (re-tinted/re-styled on theme change).
    QVector<QToolButton*> statusToggleBtns_;
    QStringList statusToggleNames_;
    QVector<QToolButton*> statusArrowBtns_;

    QTabWidget* ribbon_ = nullptr;
    QTableWidget* layersTable_ = nullptr;
    QTableWidget* propsTable_ = nullptr;
    QListWidget* entityList_ = nullptr;
    QLineEdit* commandEdit_ = nullptr;
    QLabel* coordLabel_ = nullptr;
    QLabel* cmdHintLabel_ = nullptr; // applicable commands for the active draw tool

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
