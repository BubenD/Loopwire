#pragma once

#include "harness/PartsLibrary.h"

#include <QDialog>
#include <QHash>
#include <QString>

class QComboBox;
class QLabel;
class QLineEdit;
class QGroupBox;
class QListWidget;
class QPushButton;
class QSplitter;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;
class QWidget;

namespace Edraw_Desktop {

class Editor;
class DrawingPreview;

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
    const PartsCatalog& catalog() const { return catalog_; }
    // Every part of every connected library, to read from.
    PartsLibrary library() const { return catalog_.merged(); }

    // Testing / driving: the part currently shown, and the actions the buttons run.
    QString currentPartId() const { return currentId_; }
    void selectPart(const QString& id);
    LibraryPart* addPart(PartKind kind);
    void deleteCurrentPart();
    void duplicateCurrentPart();
    void captureFromSelection(bool mechanical); // the sheet's selection becomes this drawing
    // Capturing when nothing is selected yet: the window gets out of the way so the sheet can be
    // worked on, and comes back when the selection is taken (or the capture is dropped).
    void beginCaptureFromSheet(bool mechanical);
    void finishCaptureFromSheet();
    void cancelCaptureFromSheet();
    bool capturing() const { return !capturingPart_.isEmpty(); }
    void insertIntoDrawing(bool mechanical);
    // The contacts tied to the connector showing: which ones, and the row that edits them.
    void refreshContactRow();
    // Build the connector's schematic symbol from its own data (the way count above all).
    void generateConnectorSymbol();
    // The part's page: open it in a browser, or read what can be read off it into the fields.
    void openPartLink();
    void fetchDataFromLink();

    // Place it with a contact fitted (empty for none): what the fall menu on Insert chooses.
    // Place the fanout - as drawn, or mirrored about the X axis for the other side of the connector.
    void insertFanout(bool mirrored);
    // Take what is selected on the sheet as this part's fanout.
    void beginCaptureFanout();
    // Which pin contact this connector is used with: one, for both drawings.
    void refreshContactChoice();    // this drawing goes onto the sheet
    // Onto the sheet AND selected, to be worked on: lines added or moved, and the one the harness
    // attaches to marked as such (Harness ▸ connection line, or the `connline` command). Pressing
    // Capture selection afterwards puts the edited drawing back on this part.
    void editDrawing(bool mechanical);
    void clearDrawing(bool mechanical);
    bool saveLibrary();
    // Write the current part's description from its technical parameters.
    void describeFromData();
    // The datasheet: attached into the datasheet folder, opened, or taken off. Taking it off deletes
    // the file only when no other part points at it - and says which parts those are.
    void attachDatasheet();
    void openDatasheet();
    void removeDatasheet();
    void refreshDatasheetRow();
    // Which file is picked in the list, or the part's first when none is.
    QString selectedDatasheet() const;
    void showDatasheetInFolder();   // open Explorer with the file selected
    void pullFromDatasheet();       // read the technical fields out of the attached file(s) again
    // A cable's make-up, in the builder made for it. What comes back is written into the part, and
    // the figures it implies - the conductor count, whether it is screened - are written with it.
    void editCableStructure();

signals:
    // The window has stepped aside so objects can be selected on the sheet. The host puts up the
    // ribbon and the hint that say how to finish, and calls finishCaptureFromSheet() when told to.
    void captureArmed(bool mechanical, const QString& partTitle);
    void captureFinished();

private:
    void buildUi();
    void reloadTree();
    void refreshLibraryChooser();
    // Every library there is: the connected ones and the rest of what is in the library folder, with
    // which is which said plainly and any of them connectable or disconnectable from here.
    void refreshLibraryList();
    // The S / M signs on one row of the parts list. Used when the list is built and again whenever a
    // drawing is captured or cleared, so the row keeps up with the part.
    void setPartSigns(QTreeWidgetItem* item, const LibraryPart& part);
    void disconnectSelectedLibrary();
    void showPart(const QString& id);
    void rebuildParamForm();
    LibraryPart* current();
    void touched(); // a field changed: keep the list and the file in step

    Editor* editor_ = nullptr;
    // Every connected library, and which one is being worked on. A part is edited in the library it
    // came from; a new one is added to the library chosen in the chooser.
    PartsCatalog catalog_;
    QString path_;
    QString currentId_;
    bool syncing_ = false;
    // A capture waiting on a selection: which part it is for, and which of its two drawings. Empty
    // when no capture is in hand.
    QString capturingPart_;
    bool capturingMechanical_ = false;

    QComboBox* libraryBox_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    QTreeWidget* libraryList_ = nullptr;
    QLabel* libraryTotals_ = nullptr;
    QLineEdit* filter_ = nullptr;
    QComboBox* kindBox_ = nullptr;
    QLineEdit* descEdit_ = nullptr;
    QLineEdit* mfrEdit_ = nullptr;
    QLineEdit* pnEdit_ = nullptr;
    QLineEdit* internalEdit_ = nullptr;
    QComboBox* unitBox_ = nullptr;
    QLabel* datasheetLabel_ = nullptr;
    // The files attached to this part, one row each. A part may have several - the datasheet, an
    // outline drawing, a certificate - and each can be opened, shown in its folder or taken off.
    QListWidget* datasheetList_ = nullptr;
    QPushButton* attachBtn_ = nullptr;
    QPushButton* openDsBtn_ = nullptr;
    QPushButton* removeDsBtn_ = nullptr;
    QWidget* paramHost_ = nullptr;
    QVBoxLayout* paramLay_ = nullptr;
    QLabel* summary_ = nullptr;
    // The connector's contacts: the list, and the buttons that tie one on and take one off.
    QWidget* contactGroup_ = nullptr;
    QGroupBox* contactGroupBox_ = nullptr;   // the same group, to retitle it for whichever end is shown
    QPushButton* genSymbolBtn_ = nullptr;
    QLineEdit* linkEdit_ = nullptr;
    DrawingPreview* schemPreview_ = nullptr;
    DrawingPreview* mechPreview_ = nullptr;
    // The fanout: a third drawing of the part, and which side of the connector it was drawn from.
    DrawingPreview* fanoutPreview_ = nullptr;
    QComboBox* fanoutSideBox_ = nullptr;
    QWidget* fanoutSideRow_ = nullptr;
    QComboBox* contactBox_ = nullptr;
    QHash<QString, QWidget*> paramWidgets_; // field key (or key+"Min"/"Max") -> editor
};

} // namespace Edraw_Desktop
