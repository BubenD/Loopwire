#pragma once

#include "harness/MechanicalHarness.h"
#include "harness/PartsLibrary.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QTextBrowser;

namespace Edraw_Desktop {

class Editor;
class MechanicalBuilderDialog;

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

private:
    void buildUi();
    void refreshTable();
    void refreshExplanation();

    Editor* editor_ = nullptr;
    PartsCatalog catalog_;
    MechHarnessDef def_;
    std::vector<MechIssue> issues_;

    QLabel* summary_ = nullptr;
    QTableWidget* table_ = nullptr;
    QCheckBox* showNotes_ = nullptr;
    QTextBrowser* detail_ = nullptr;
};

} // namespace Edraw_Desktop
