#pragma once

#include "harness/BrickDrc.h"
#include "harness/MechanicalHarness.h"
#include "harness/PartsLibrary.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QTextBrowser;

namespace Loopwire {

class Editor;

// Mechanical compatibility: whether the parts fitted to this harness will physically go on it.
//
// Every check is against the BUNDLE the conductors actually make, so it follows the drawing rather
// than a figure someone typed once. Three kinds of finding, kept apart because they mean different
// things: something will not fit (wrong), a figure is missing (unfinished), or something is worth
// knowing (a note). A missing figure is never filled in with a guess - a fit check on an invented
// entry size is exactly the sort of number that gets a harness built wrong.
class MechanicalValidationDialog : public QDialog {
    Q_OBJECT
public:
    explicit MechanicalValidationDialog(Editor* editor, QWidget* parent = nullptr);

    // Check the definition read off the drawing, or one handed in from the builder.
    void setDefinition(const MechHarnessDef& def);
    void readFromDrawing();
    void revalidate();
    const std::vector<MechIssue>& issues() const { return issues_; }
    int countOf(MechIssueKind kind) const;

    // THE HARNESS DRC: every harness the wizard made - the one open in it as it is now, saved or not, and every one
    // saved in the drawing - checked by the logical and compatibility rules turned on, less what is bypassed.
    void setOpenHarness(const BrickDesign* design, const QString& name);
    const std::vector<BrickIssue>& harnessIssues() const { return harnessIssues_; }   // all found
    std::vector<BrickIssue> shownHarnessIssues() const;                                // rules on, not bypassed
    // Its rules (which are checked) and its bypass table - the DRC's own, not the schematic's.
    void showRules();
    void showBypass();
    // A rule's severity now: as the user set it (2 fail, 1 warning, 0 pass), or as it comes.
    int severityOf(const QString& rule) const;

signals:
    // A finding double-clicked: its part found on the drawing - where it was found, or by its harness and brick.
    void locateRequested(const QString& harness, const QString& brickId, double x, double y, bool located);
    void drawingFieldLocateRequested(const QString& entityId,const QString& layout,double x,double y);

private:
    void buildUi();
    void refreshTable();
    void refreshExplanation();
    void checkHarnesses();
    void refreshHarnessTable();
    int bomItemOf(const QString& partId) const;

    Editor* editor_ = nullptr;
    PartsCatalog catalog_;
    MechHarnessDef def_;
    std::vector<MechIssue> issues_;

    QLabel* summary_ = nullptr;
    QTableWidget* table_ = nullptr;
    QCheckBox* showNotes_ = nullptr;
    QTextBrowser* detail_ = nullptr;

    const BrickDesign* openDesign_ = nullptr;
    QString openName_;
    PartsLibrary merged_;
    std::vector<BrickIssue> harnessIssues_;
    std::vector<BrickIssue> harnessRows_;   // as shown, row by row
    QLabel* harnessSummary_ = nullptr;
    QTableWidget* harnessTable_ = nullptr;
    QDialog* rulesDlg_ = nullptr;
    QDialog* bypassDlg_ = nullptr;
};

} // namespace Loopwire
