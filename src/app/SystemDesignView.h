#pragma once

#include "app/SystemCanvas.h"
#include "system/SystemDesign.h"

#include <QJsonObject>
#include <QWidget>

#include <functional>
#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QPlainTextEdit;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QTableWidget;
class QTabWidget;

namespace Edraw_Desktop {

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
    int checkRevisions();
    int checkDesign();
    // THE CABLE LIST to a CSV file (asked where, when none is given).
    bool exportCableList(const QString& path = QString());
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
    QString status(const SysCable& c) const;

    SystemDesign design_;
    std::function<QJsonObject()> load_;
    std::function<void(const QJsonObject&)> save_;
    std::vector<QJsonObject> history_;
    std::vector<QJsonObject> future_;   // what Undo took back, for Redo
    std::vector<SysIssue> issues_;
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
    QLabel* cableRead_ = nullptr;
    QLabel* cableStatus_ = nullptr;
    // A note: its text - one step to undo however much of it is typed at once.
    QPlainTextEdit* noteText_ = nullptr;
    QString noteTyping_;
    // Under the picture.
    QTabWidget* tabs_ = nullptr;
    QTableWidget* cables_ = nullptr;
    QListWidget* found_ = nullptr;
};

} // namespace Edraw_Desktop
