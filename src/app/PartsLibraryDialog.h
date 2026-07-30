#pragma once

#include "harness/PartsLibrary.h"

#include <QDialog>
#include <QHash>
#include <QString>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSplitter;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;
class QWidget;

namespace Edraw_Desktop {

class Editor;
class PartDrawingPreview;

// The harness parts library: the raw materials a harness is built from, kept in one file and shown
// by kind. A part carries what a purchase order needs (description, manufacturer, its part number)
// plus the technical parameters ITS kind calls for - the form is built from partFields(), so a kind
// gains a parameter in one place.
//
// Each part can also hold two drawings of itself: a schematic symbol and a mechanical drawing.
// Both are taken from the sheet ("Capture selection") and can be dropped back onto one ("Insert"),
// so a part is drawn once and re-used, and the library shows what it will look like.
class PartsLibraryDialog : public QDialog {
    Q_OBJECT
public:
    explicit PartsLibraryDialog(Editor* editor, QWidget* parent = nullptr);

    // Where the library is read from / written to (the default path unless told otherwise).
    void setLibraryPath(const QString& path);
    QString libraryPath() const { return path_; }
    const PartsLibrary& library() const { return lib_; }

    // Testing / driving: the part currently shown, and the actions the buttons run.
    QString currentPartId() const { return currentId_; }
    void selectPart(const QString& id);
    LibraryPart* addPart(PartKind kind);
    void deleteCurrentPart();
    void duplicateCurrentPart();
    void captureFromSelection(bool mechanical); // the sheet's selection becomes this drawing
    void insertIntoDrawing(bool mechanical);    // this drawing goes onto the sheet
    void clearDrawing(bool mechanical);
    bool saveLibrary();

private:
    void buildUi();
    void reloadTree();
    void showPart(const QString& id);
    void rebuildParamForm();
    LibraryPart* current();
    void touched(); // a field changed: keep the list and the file in step

    Editor* editor_ = nullptr;
    PartsLibrary lib_;
    QString path_;
    QString currentId_;
    bool syncing_ = false;

    QTreeWidget* tree_ = nullptr;
    QLineEdit* filter_ = nullptr;
    QComboBox* kindBox_ = nullptr;
    QLineEdit* descEdit_ = nullptr;
    QLineEdit* mfrEdit_ = nullptr;
    QLineEdit* pnEdit_ = nullptr;
    QLineEdit* internalEdit_ = nullptr;
    QComboBox* unitBox_ = nullptr;
    QWidget* paramHost_ = nullptr;
    QVBoxLayout* paramLay_ = nullptr;
    QLabel* summary_ = nullptr;
    PartDrawingPreview* schemPreview_ = nullptr;
    PartDrawingPreview* mechPreview_ = nullptr;
    QHash<QString, QWidget*> paramWidgets_; // field key (or key+"Min"/"Max") -> editor
};

} // namespace Edraw_Desktop
