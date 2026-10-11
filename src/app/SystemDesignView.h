#pragma once

#include "app/SystemCanvas.h"
#include "system/SystemDesign.h"

#include <QImage>
#include <QJsonObject>
#include <QSet>
#include <QWidget>

#include <functional>
#include <vector>

class QComboBox;
class QDialog;
class QDoubleSpinBox;
class QPlainTextEdit;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSlider;
class QSpinBox;
class QToolButton;
class QStackedWidget;
class QTableWidget;
class QTabWidget;

class QMenu;

namespace Edraw_Desktop {

// WHAT THE PDF IS MADE OF: which part of the diagram goes out - every sheet a page, the whole diagram, what the window
// shows, or the units selected - the paper ("" as the sheet, else "A4" to "A0", "Letter", "Tabloid") and its way round,
// a margin, and black on white or in colour.
struct SysPdfOptions {
    enum What { Sheets, Whole, Window, Selected };
    What what = Sheets;
    QString paper;
    bool portrait = false;
    double marginMm = 0.0;
    bool blackWhite = false;
};


// THE SYSTEM DESIGN: the system diagram (SystemCanvas) in the middle; on the right what is selected - a unit's name,
// part number, revision and ports, or a cable's name, part number, revision, length, wires and the drawing it is
// linked to with that drawing's number and revision; under it the cable list and what the check found. Kept in the
// drawing (its store), every change one step to undo.
class SystemDesignView : public QWidget {
    Q_OBJECT
public:
    explicit SystemDesignView(QWidget* parent = nullptr);

    // WHERE THE DESIGN IS KEPT: read from, and written to (the drawing's systemDesign).
    void setStore(std::function<QJsonObject()> load, std::function<void(const QJsonObject&)> save);
    void reload();
    SystemDesign& design() { return design_; }
    SystemCanvas* canvas() const { return canvas_; }

    void setTool(SysTool tool);
    // WHAT IS SELECTED DELETED - a unit with cables or units in it, or a port with cables, only once asked (confirm).
    // False where nothing was.
    bool deleteSelected();
    // THE UNIT SELECTED'S COLOUR (none: as units of its kind are) and how solid its fill is (0 to 1).
    void setUnitColour(const QColor& colour);
    void setUnitOpacity(double opacity);
    void undo();
    void redo();
    bool canUndo() const { return !history_.empty(); }
    bool canRedo() const { return !future_.empty(); }
    // PART NUMBERS for the cables with none. How many.
    int numberCables();
    // HOW CABLES ARE NUMBERED: the prefix and how many digits after it (CBL-001: "CBL-", 3) - set, one step to undo; and
    // asked for in a dialog.
    void setNumbering(const QString& prefix, int digits);
    void askNumbering();
    // THE CABLE SELECTED linked to a drawing (empty: unlinked) - its number and revision read off it; `ask`: where the
    // drawing calls the cable or its connectors otherwise, asked whether to take the drawing's names.
    bool linkDrawing(const QString& cable, const QString& path, bool ask = false);
    // HOW IT ASKS: yes or no to a question (a message box, unless the tests say otherwise).
    void setConfirm(std::function<bool(const QString& question)> confirm) { confirm_ = std::move(confirm); }
    void linkDrawingFromFile();   // chosen in a file dialog
    void findDrawing();           // found with Find drawings
    void openDrawing();           // the selected cable's drawing, opened (openRequested)
    // EVERY LINKED DRAWING READ AGAIN, and the design checked - what is found listed. How many errors.
    // THE CHECKS, each of its own part: the linked drawings' revisions only (read again first); the units' connections
    // only - their ports and the line names through them; everything.
    enum class CheckScope { All, Revisions, Connections };
    int checkRevisions();
    int checkDesign(CheckScope scope = CheckScope::All);
    int checkSchematics();
    void askRules();
    bool locateIssue(int index);   // by its place in issues()
    void setRuleSeverity(const QString& rule, int level);
    bool bypassIssue(int row);
    bool restoreIssue(const QString& key);
    void showBypass();
    void setCanvasColour(const QColor& colour);
    void askCanvasColour();
    // THE DESIGNATORS' COLOUR: one for all, or none for Auto (each against what is behind it).
    void setDesignatorColour(const QColor& colour);
    void askDesignatorColour();
    // THE CABLE LIST to a CSV file (asked where, when none is given).
    bool exportCableList(const QString& path = QString());
    // THE DESIGN AS A FILE OF ITS OWN (.edsys), and read back from one (one step to undo); THE DIAGRAM FOR DRAW.IO and
    // FOR VISIO. Asked where, when no path is given.
    bool saveDesignAs(const QString& path = QString());
    bool openDesign(const QString& path = QString());
    // THE FILE IT WAS LAST SAVED TO OR OPENED FROM AS A DESIGN OF ITS OWN (.wetsys); empty: none. Not saved.
    QString designPath() const { return designPath_; }
    // A DIAGRAM FROM DRAW.IO OR VISIO, in place of this one (one step to undo); asked which, when not given.
    bool importDesign(const QString& path = QString());
    // COPY, CUT AND PASTE what is selected - units with what is inside them, the harnesses among them, notes - by the
    // clipboard. Pasted where asked (the place right-clicked), or a little down and right of where it was.
    bool copySelection();
    bool cutSelection();
    bool canPaste() const;
    bool paste(const QPointF& at = QPointF(), bool atPoint = false);
    // FEWER CROSSINGS: the ports of the selected harnesses (of those at the selected units; of all, where nothing is
    // selected) put in order along their sides. How many crossings went.
    int minimizeCrossings();
    // A UNIT'S PICTURE, from a file (no more than 400 across, kept in the design) - asked for where none is given; taken out.
    bool setUnitPicture(const QString& unit, const QString& path = QString());
    void removeUnitPicture(const QString& unit);
    // ITS PICTURE drawn in the unit or not, and how much of the room in it it takes (0.1 to 1); saved to a file.
    void setUnitPictureShown(const QString& unit, bool shown);
    void setUnitPictureScale(const QString& unit, double scale);
    void setUnitPictureAlign(const QString& unit, const QString& align);
    bool saveUnitPicture(const QString& unit, const QString& path = QString());
    // WHAT A UNIT OR HARNESS SHOWS of itself on the diagram ("name", "pn", "rev", "desc"), as its eyes say.
    void setShown(const QString& id, const QString& what, bool shown);
    // The row of the findings table a finding is on (-1: folded away, or none).
    int rowOfIssue(int issue) const;
    // THE RIGHT-CLICK MENU of the diagram: copy, cut, paste, delete, minimize crossings, the unit's picture.
    QMenu* contextMenu(const QPointF& world);
    bool exportDrawio(const QString& path = QString());
    bool exportVisio(const QString& path = QString());
    // THE DIAGRAM OUT AS A PICTURE (PNG) or as a PDF - its sheet with it, where it has one.
    bool exportPng(const QString& path = QString());
    // THE PDF: with no file given, the PDF window - what goes out, the paper, its way round, margins, colour, a preview.
    // Given a file: every sheet a page (or the whole diagram where there is none), as the sheet's paper.
    bool exportPdf(const QString& path = QString());
    int pdfPageCount(const SysPdfOptions& o) const;
    QImage pdfPage(const SysPdfOptions& o, int page, int longSide) const;
    bool writePdf(const SysPdfOptions& o, const QString& path) const;
    // A TEMPLATE'S SHEET round the diagram: an Edraw drawing template (.etp) - its frame and title block - fitted round what
    // is drawn, its title filled in; chosen from those installed (or a file) where none is given. Its fields; taken away.
    // ANOTHER: a sheet more, beside the last - a page more in the PDF. The rest work on the sheet chosen in the panel.
    bool applyTemplate(const QString& path = QString(), bool another = false);
    bool askSheetFields();
    void setSheetField(const QString& key, const QString& value);
    // WHERE A FIELD OF THE SHEET CHOSEN TAKES ITS TEXT FROM: an id of sysFieldSources(), "" for what is typed.
    void setSheetFieldSource(const QString& key, const QString& source);
    QStringList sheetFieldKeys() const;
    void removeSheet();
    // THE SHEET THE PANEL WORKS ON: 0 the first.
    int currentSheet() const { return sheetAt_; }
    void setCurrentSheet(int i);
    // THE REVISIONS: one added after the last (its letter or number the next, dated today), one taken away, one written.
    void addRevision();
    void removeRevision(int row);
    void setRevision(int row, const SysRevision& r);
    // WHERE THE TEXT SITS in the revision table's cells on the sheets: "TL" to "BR", "" as the template - for one column of
    // the seven (rev, change note, ECO, drawn, checked, approved, date), or every one for -1.
    void setRevisionAlign(int column, const QString& code);
    // A TITLE BLOCK FIELD'S NAME, said plainly ("scale": "Drawing scale"), and what it is for.
    static QString sheetFieldName(const QString& key);
    static QString sheetFieldTip(const QString& key);
    // THE TEMPLATES THERE ARE TO CHOOSE FROM: those installed, by name, each with its file - and the plain sheets built in
    // ("builtin:A3").
    QList<QPair<QString, QString>> templateChoices() const;
    // AUTO-ALIGN the selected units and harnesses (all, where nothing is selected): small offsets tidied. How many moved.
    int autoAlign();
    // What the check found last.
    const std::vector<SysIssue>& issues() const { return issues_; }

signals:
    void openRequested(const QString& path);
    void changed();

private:
    void aboutToChange();
    void commit();
    void refresh();         // the panel, the cable list, the picture
    void syncPanel();
    void fillCables();
    void fillIssues();
    void fillConnections();
    void fillBypass();
    QString status(const SysCable& c) const;

    SystemDesign design_;
    std::function<QJsonObject()> load_;
    std::function<void(const QJsonObject&)> save_;
    std::vector<QJsonObject> history_;
    std::vector<QJsonObject> future_;   // what Undo took back, for Redo
    std::vector<SysIssue> issues_;
    std::vector<SysIssue> checkedFindings_;
    QDialog* bypassDialog_ = nullptr;
    QTableWidget* bypassTable_ = nullptr;
    QLabel* bypassSummary_ = nullptr;
    std::function<bool(const QString&)> confirm_;
    bool filling_ = false;

    SystemCanvas* canvas_ = nullptr;
    QStackedWidget* panel_ = nullptr;
    QLabel* nothing_ = nullptr;
    // A unit.
    QLineEdit* unitName_ = nullptr;
    QLineEdit* unitPn_ = nullptr;
    QLineEdit* unitRev_ = nullptr;
    QLineEdit* unitDesc_ = nullptr;
    QComboBox* unitKind_ = nullptr;
    QToolButton* unitColour_ = nullptr;
    QSlider* unitOpacity_ = nullptr;
    QString opacityDragging_;   // one step to undo for a slide of it
    // THE EYES: whether each of a unit's (a cable's) name, P/N, rev and description is shown on the diagram.
    QToolButton* unitEyes_[4] = {};
    QToolButton* cableEyes_[4] = {};
    QTableWidget* ports_ = nullptr;
    // A cable.
    QLineEdit* cableName_ = nullptr;
    QLineEdit* cablePn_ = nullptr;
    QLineEdit* cableRev_ = nullptr;
    QLineEdit* cableDesc_ = nullptr;
    QDoubleSpinBox* cableLength_ = nullptr;
    QSpinBox* cableWires_ = nullptr;
    QLabel* cableEnds_ = nullptr;
    QLineEdit* cableDrawing_ = nullptr;
    QLineEdit* cableExpectedTitle_ = nullptr;
    QLabel* cableRead_ = nullptr;
    QLabel* cableStatus_ = nullptr;
    // A note: its text - one step to undo however much of it is typed at once.
    QPlainTextEdit* noteText_ = nullptr;
    QString noteTyping_;
    QDoubleSpinBox* noteWidth_ = nullptr;
    QDoubleSpinBox* noteHeight_ = nullptr;
    QToolButton* noteColour_ = nullptr;
    QComboBox* noteLink_ = nullptr;
    QToolButton* noteLinkEye_ = nullptr;
    std::vector<SysConnection> connectionRows_;
    // Under the picture.
    QTabWidget* tabs_ = nullptr;
    QTableWidget* cables_ = nullptr;
    QTableWidget* found_ = nullptr;
    QLabel* issueSummary_ = nullptr;
    bool checked_ = false;
    QJsonObject lastPiece_;   // what was copied last - the clipboard's, kept here too. Not saved.
    std::vector<int> rowIssue_;     // each findings row's finding (-1: a topic's heading). Not saved.
    QSet<QString> foldedTopics_;    // topics folded away in the findings table. Not saved.
    QLabel* unitPicturePreview_ = nullptr;
    QToolButton* unitPictureEye_ = nullptr;
    QSlider* unitPictureSize_ = nullptr;
    QComboBox* unitPictureAlign_ = nullptr;
    // THE SHEET, on the panel when nothing is selected: its template and its fields.
    QLabel* sheetName_ = nullptr;
    QWidget* sheetFields_ = nullptr;
    QComboBox* sheetPick_ = nullptr;
    QTableWidget* revisions_ = nullptr;
    QComboBox* revisionAlign_ = nullptr;
    int sheetAt_ = 0;   // the sheet the panel works on. Not saved.
    QString designPath_;
    SysSheet* chosenSheet();
    const SysSheet* chosenSheet() const;
    void fillSheetPanel();
    QStringList selectedIds(int what) const;   // 0 units, 1 harnesses, 2 notes
    CheckScope lastScope_ = CheckScope::All;   // what Check again runs: the check last run. Not saved.
    QTableWidget* connections_ = nullptr;
};

} // namespace Edraw_Desktop
