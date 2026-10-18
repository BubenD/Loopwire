#pragma once

#include <vector>

#include "harness/MechModels.h"
#include "harness/PartsLibrary.h"
#include "harness/PartFromPdf.h"
#include "geometry/PartModel3D.h"
#include "harness/Harness3D.h"

#include <QDialog>
#include <QHash>
#include <QString>

#include <functional>

class QToolButton;
class QComboBox;
class QLabel;
class QLineEdit;
class QGroupBox;
class QTableWidget;
class QDoubleSpinBox;
class QListWidget;
class QAction;
class QCheckBox;
class QPushButton;
class QSplitter;
class QTreeWidget;
class QTreeWidgetItem;
class QVBoxLayout;
class QWidget;

namespace Loopwire {

class Editor;
class DrawingPreview;
class View3DCanvas;

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
    // onEdit is handed the part id and which of its drawings to open - the main canvas session
    // (grey canvas, and connection points for the mechanical one) that replaces the old
    // connection-line popup. Optional so a dialog built without a host still works (tests).
    explicit PartsLibraryDialog(Editor* editor, QWidget* parent = nullptr,
                                std::function<void(QString, LibraryPartDrawing)> onEdit = {});

    // THE WINDOW REOPENS THE SIZE IT WAS LEFT. Three drawings side by side with a row of buttons under
    // each is a wide window, and the size that suits one machine's screen and one person's habits is not
    // a thing to work out afresh every time - having resized it once so nothing is clipped, somebody
    // should not have to do it again on the next opening.
    void closeEvent(QCloseEvent* e) override;
    // SHIFT + CLICK in the parts list adds that part to the ones selected; CTRL + CLICK takes it off them.
    bool eventFilter(QObject* watched, QEvent* event) override;

    // Holds every captioned button at the width its words need and sets the window's minimum to what the
    // layouts then ask for (never more than the screen). Returns that need, unclamped.
    QSize holdCaptions();

    // Where the library is read from / written to (the default path unless told otherwise).
    void setLibraryPath(const QString& path);
    // THE WINDOW FOLLOWS A CHANGE OF SCHEME.
    //
    // It is kept alive between openings, so it cannot simply be built in whatever scheme is in force -
    // one opened in the dark scheme and left open keeps dark-scheme lettering when the office switches
    // to light, which is pale grey on white. MainWindow calls this from applyTheme.
    void restyle();
    QString libraryPath() const { return path_; }
    const PartsCatalog& catalog() const { return catalog_; }
    // Every part of every connected library, to read from.
    PartsLibrary library() const { return catalog_.merged(); }
    // The part, mutably, in whichever connected library actually holds it - not a copy, and not
    // necessarily the part currently shown. What a canvas editing session reads its starting
    // drawing from and writes the finished one back into.
    LibraryPart* partForSession(const QString& id);

    // Testing / driving: the part currently shown, and the actions the buttons run.
    QString currentPartId() const { return currentId_; }
    void selectPart(const QString& id);
    // GO TO A PART: its group opened if folded, the part picked and scrolled to, its form shown - what Open does
    // from a connector to its contact and back.
    void openPart(const QString& id);
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
    // Take one of a default model's drawings as one of this part's. `fromSchematic` is which of the MODEL's
    // drawings is taken, `intoMechanical` which of the PART's it lands in - they are separate because either
    // pairing can be the right one. False when there is nothing to take, which leaves the part alone.
    bool takeDrawingFromDefaultModel(const MechModelSet& models, MechModelKind kind,
                                     const QString& modelName, bool fromSchematic, bool intoMechanical);
    // The contacts tied to the connector showing: which ones, and the row that edits them.
    void refreshContactRow();
    // Build the connector's schematic symbol from its own data (the way count above all).
    void generateConnectorSymbol();
    // DRAW THE PART FROM A PDF: its mechanical drawing from a view boxed on the page, its pinout from the
    // pin table. The PDF is the datasheet picked (or the first one attached), or one chosen when there is
    // none; `path` gives it outright.
    void drawFromPdf(const QString& path = QString());
    // BUILD ITS 3D MODEL in a window of its own; and the model built, kept as the part's 3D model.
    void buildModel3D(bool automatic = false);
    bool saveBuiltModel3D(const PartModel3D& model);
    // What that takes, put into the part showing: the drawing as its mechanical drawing, the pinout as its
    // pin count and labels, and a connector's symbol built again from them. `ask` asks before replacing a
    // drawing the part already has.
    void applyFromPdf(const QJsonArray& drawing, const std::vector<PdfPin>& pinout, bool ask = true);
    // The part's page: open it in a browser, or read what can be read off it into the fields.
    void openPartLink();

    // Place it with a contact fitted (empty for none): what the fall menu on Insert chooses.
    // Place the fanout - as drawn, or mirrored about the X axis for the other side of the connector.
    void insertFanout(bool mirrored);
    // Take what is selected on the sheet as this part's fanout - the opposite side's when
    // `opposite` is set.
    void beginCaptureFanout(bool opposite = false);
    // Which pin contact this connector is used with: one, for both drawings.
    void refreshContactChoice();    // this drawing goes onto the sheet
    // Opens that drawing in its own canvas session (grey canvas; the mechanical one also gets real
    // connect/near/far points) via onEdit_ - the host (MainWindow) owns the session itself, the way
    // MechModelsDialog's onEdit_ works for a default model.
    void editDrawing(LibraryPartDrawing kind);
    void clearDrawing(bool mechanical);
    bool saveLibrary();
    // Re-read the connected libraries off disk and redraw. Wanted when something OUTSIDE this window has
    // written to them - a pin table on the drawing correcting the figures it took from here.
    void reloadTreeForTest();
    // Which side of the connector the fanout pane is working on, and that pane brought into step with it.
    // Front and back are two drawings, each captured and edited on its own; the side showing is the part's.
    bool showingBackFanout() const;
    void showFanoutSide();
    // Write the current part's description from its technical parameters.
    void describeFromData();
    // The datasheet: attached into the datasheet folder, opened, or taken off. Taking it off deletes
    // the file only when no other part points at it - and says which parts those are.
    void attachDatasheet();
    void openDatasheet();
    void removeDatasheet();
    void refreshDatasheetRow();
    // ITS 3D MODEL (.stp, .stl, .obj): attached into the same folder, shown on the harness in the 3D view - a box where
    // a part has none.
    void attachModel3D();
    void removeModel3D();
    void refreshModel3DRow();
    // THE LIVE VIEW of its 3D model under the row, read again when the model is another or was saved since.
    void refreshModelView();
    // THE SIZE a mechanical drawing drawn to proportion is put at - by its entry face (see partSizeFactor).
    void refreshSizeRow();
    // Which file is picked in the list, or the part's first when none is.
    QString selectedDatasheet() const;
    void showDatasheetInFolder();   // open Explorer with the file selected
    void pullFromDatasheet();       // read the technical fields out of the attached file(s) again
    // A cable's make-up, in the builder made for it. What comes back is written into the part, and
    // the figures it implies - the conductor count, whether it is screened - are written with it.
    void editCableStructure();

signals:
    // "Tell this part where its wires leave": the faces are marked on the drawing with two clicks each, so
    // the host opens the part's mechanical drawing with that tool in hand. Emitted rather than done here
    // because this window does not own the session - the same reasoning as onEdit_.
    void markOutputFacesRequested(const QString& partId);
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
    // A LIBRARY SWITCHED OFF in the list: its parts left out of the parts list. Remembered between sessions.
    bool libraryHidden(const QString& file) const;
    void setLibraryHidden(const QString& file, bool hidden);
public:
    // A NEW, EMPTY LIBRARY written to `file`, connected and made the one worked in. False when it is there already or
    // cannot be written.
    bool createLibrary(const QString& file);
private:
    void showPart(const QString& id);
    void rebuildParamForm();
    LibraryPart* current();
    void touched(); // a field changed: keep the list and the file in step
    // Hand entities to the cursor and step aside until they land or Esc puts the ghost down.
    void beginPlacement(std::vector<Entity> entities);

    Editor* editor_ = nullptr;
    std::function<void(QString, LibraryPartDrawing)> onEdit_;
    // Every connected library, and which one is being worked on. A part is edited in the library it
    // came from; a new one is added to the library chosen in the chooser.
    PartsCatalog catalog_;
    QString path_;
    QString currentId_;
    bool syncing_ = false;
    bool treeBuilt_ = false;   // the parts list has been filled once: after that it keeps its folds
    // A capture waiting on a selection: which part it is for, and which of its two drawings. Empty
    // when no capture is in hand.
    QString capturingPart_;
    bool capturingMechanical_ = false;

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
    QLabel* model3dLabel_ = nullptr;
    QPushButton* model3dAttach_ = nullptr;
    QPushButton* model3dRemove_ = nullptr;
    View3DCanvas* modelView_ = nullptr;
    Harness3DScene modelScene_;   // what modelView_ shows
    QString modelShown_;          // the file (and its time) it was read from
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
    QPushButton* outputFacesBtn_ = nullptr;
    // One Place button, and a tick against each drawing of the part it puts down. See placeTicked().
    QToolButton* placeBtn_ = nullptr;
    QComboBox* sizeModeBox_ = nullptr;       // as drawn / entry face known / from the part's figures / Auto
    QDoubleSpinBox* sizeKnownBox_ = nullptr; // the entry face, when it is known
    QLabel* sizeInfo_ = nullptr;             // what that comes to
    // A connector that comes with its pins fitted: the tick, the contact menu it puts away, and the rows
    // that describe the pins instead. See rebuildPinTypes().
    QCheckBox* includePinsBox_ = nullptr;
    QWidget* contactRowHost_ = nullptr;
    QWidget* pinTypesHost_ = nullptr;
    QVBoxLayout* pinTypesLay_ = nullptr;
    QPushButton* addPinTypeBtn_ = nullptr;
    void rebuildPinTypes();
    // A connector that comes WITH ITS WIRES: the tick, and a row per pin saying what its wire is.
    QGroupBox* wiresGroup_ = nullptr;
    QCheckBox* wiredBox_ = nullptr;
    QWidget* wiresHost_ = nullptr;
    QTableWidget* wiresTable_ = nullptr;
    QPushButton* wiresSameBtn_ = nullptr;
    QComboBox* wiresCodeBox_ = nullptr;   // colour every wire by a colour code
    // How a wired connector's wires are shown: on the mechanical drawing and on the schematic.
    QCheckBox* wiredMechBox_ = nullptr;
    QComboBox* wiredSideBox_ = nullptr;
    QDoubleSpinBox* wiredStraightBox_ = nullptr;
    QDoubleSpinBox* wiredPitchBox_ = nullptr;
    QDoubleSpinBox* wiredLengthBox_ = nullptr;
    QCheckBox* wiredSchemBox_ = nullptr;
    QCheckBox* wiredLabelColorBox_ = nullptr;
    QCheckBox* wiredLabelSizeBox_ = nullptr;
    QDoubleSpinBox* wiredSchemLeadBox_ = nullptr;
    QWidget* wiredMechParams_ = nullptr;    // shown only while the mechanical tick is on
    QWidget* wiredSchemParams_ = nullptr;   // and these only while the schematic one is
    void refreshWiredPreviews();
    // N/A on a drawing pane: that drawing does not apply to the part, and the pane stands down.
    QPushButton* naBtn_[3] = {nullptr, nullptr, nullptr};   // mechanical, schematic, fanout
    void applyNotApplicable();
public:
    // The picked cells of the wire table to the clipboard, and the clipboard into them (Ctrl+C / Ctrl+V).
    void copyWireCells();
    void pasteWireCells();
private:
    void rebuildWires();
    void writeWires();
    std::vector<QAction*> placeWhat_;
    void placeTicked();
    // The OTHER side's fanout - its own drawing, not a live mirror of fanoutPreview_'s (see fanoutOpposite).
    DrawingPreview* fanoutOppositePreview_ = nullptr;
    // The eye over the three drawings: shows or hides the connection point - the line the harness attaches
    // to, drawn orange - so a part can be looked at with nothing over it. See applyShowConnection().
    QToolButton* showConnBtn_ = nullptr;
    void applyShowConnection();
    // The part's mechanical drawing with its marks drawn over it - where it connects and which way it runs
    // from there, its two ends, the faces the conductors leave by. In the colours the editing canvas uses,
    // and only when the eye says to show them.
    std::vector<Entity> markedMechanicalDrawing(const LibraryPart& part);
    // Take this drawing from a default mechanical model - the house style for the pieces a harness is made
    // of. Opens the model list with each model's drawings beside it; `mechanical` says which of the PART's
    // drawings the taken one lands in, which is the button that was pressed to get here.
    void importFromDefaultModel(bool mechanical);
    QComboBox* fanoutSideBox_ = nullptr;
    QWidget* fanoutSideRow_ = nullptr;
    QComboBox* contactBox_ = nullptr;
    QListWidget* contactList_ = nullptr;   // every contact tied to the connector - there may be several
    QHash<QString, QWidget*> paramWidgets_; // field key (or key+"Min"/"Max") -> editor
};

} // namespace Loopwire
