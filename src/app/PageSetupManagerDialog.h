#pragma once

#include "model/Document.h"
#include "model/PageSetup.h"

#include <QDialog>

class QCheckBox;
class QLabel;
class QListWidget;
class QPushButton;

namespace Edraw_Desktop {

// The Page Setup Manager: the setups a drawing has, and which one the layout showing is using.
//
// Two kinds are on the list. The layout's OWN setup, written *Layout1* the way a plot dialog writes it -
// it belongs to that sheet and goes nowhere else. And NAMED setups, which live on the drawing beside the
// layouts, so a paper, a scale and a set of plot options can be settled once and then set on every sheet
// of a pack. Set current copies a named setup onto the layout; Modify edits whichever is picked; New makes
// a named one from what is picked; Import reads the setups out of another drawing.
//
// It edits the document directly, because that is where a page setup lives: it is saved with the drawing,
// so a sheet that plots correctly today plots the same for whoever opens the file next.
class PageSetupManagerDialog : public QDialog {
    Q_OBJECT
public:
    // `layoutIndex` is the layout being worked on (0-based into doc.layouts()).
    PageSetupManagerDialog(Document& doc, int layoutIndex, QWidget* parent = nullptr);

    // Whether anything was changed, so the caller knows to mark the drawing dirty and repaint.
    bool changed() const { return changed_; }

private:
    void rebuild();
    void updateDetails();
    // The setup the row stands for: the layout's own, or a named one. Null if the row is odd.
    PageSetup* setupAt(int row);

    void setCurrent();
    void makeNew();
    void modify();
    void importFrom();

    Document& doc_;
    int layoutIndex_ = 0;
    bool changed_ = false;

    QListWidget* list_ = nullptr;
    QLabel* current_ = nullptr;
    QLabel* details_ = nullptr;
    QPushButton* setCurrentBtn_ = nullptr;
    QCheckBox* showOnNew_ = nullptr;
};

} // namespace Edraw_Desktop
