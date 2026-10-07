#pragma once

#include <QDialog>
#include <QHash>
#include <QPointer>
#include <QStringList>

#include <atomic>
#include <memory>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QThread;

namespace Edraw_Desktop {

class DrawingPreview;
class OdaConverter;

// FIND DRAWINGS: the files under a folder (and its folders) a part number is in - DWX and DXF drawings (every text,
// note, table cell and attribute in them), PDFs (their text) and CSV lists (every cell) - or whose name has it; DWG
// drawings by their name, and beside a PDF or DXF of the same name that has it. Found as a person means a part number
// (on its own, or inside a longer one where asked; separators - . / _ optional where asked; several at once with ';'),
// each file listed with how often and the words round it, and the one chosen shown beside the list with every place it
// is found. Searched in the background, stopped at any time; opened from here, or (asked for one) chosen - to link a
// cable to it.
class DrawingFinderDialog : public QDialog {
    Q_OBJECT
public:
    explicit DrawingFinderDialog(QWidget* parent = nullptr, bool choosing = false);
    ~DrawingFinderDialog() override;

    void setFolder(const QString& folder);
    void setText(const QString& text);
    // How a part number is matched: inside a longer one too; upper and lower case alike or not; separators optional.
    void setMatching(bool partial, bool caseSensitive, bool ignoreSeparators);
    // Which kinds of file are searched: "dwx", "dxf", "dwg", "pdf", "csv".
    void setKinds(const QStringList& kinds);
    // THE SEARCH, from the folder, for the text; `wait` - until it is done (the tests).
    void search(bool wait = false);
    void stop();
    bool searching() const;
    int resultCount() const;
    QString resultPath(int row) const;
    int resultHits(int row) const;
    // The drawings beside a result, of the same name ("DWG", "DXF").
    QStringList resultDrawings(int row) const;
    // What the preview shows: so many objects of the drawing (a PDF's, read in the background - `wait` for it).
    int previewCount(bool wait = false);
    void showResult(int row);
    QString chosen() const { return chosen_; }

    // A DRAWING CHOSEN - the dialog in choosing mode; empty where none was.
    static QString choose(QWidget* parent, const QString& text = QString());

signals:
    // A drawing to open, asked for with Open.
    void openRequested(const QString& path);

private:
    struct Hit {
        QString where;
        QString before, match, after;
        bool exact = true;
    };
    struct Found {
        QString path;
        QString drawingNo;
        QString revision;
        QString title;
        qint64 modified = 0;
        QList<Hit> hits;
        QString why;                     // why its insides could not be searched
        QHash<QString, QString> beside;  // "DWG" / "DXF" of the same name
        bool byName = false;
    };
    void addFound(const Found& f);
    void finished();
    void preview(const QString& path);
    void open(const QString& path);

    bool choosing_ = false;
    QLineEdit* folder_ = nullptr;
    QLineEdit* text_ = nullptr;
    QCheckBox* sub_ = nullptr;
    QCheckBox* dwx_ = nullptr;
    QCheckBox* dxf_ = nullptr;
    QCheckBox* dwg_ = nullptr;
    QCheckBox* pdf_ = nullptr;
    QCheckBox* csv_ = nullptr;
    QCheckBox* inside_ = nullptr;
    QCheckBox* partial_ = nullptr;
    QCheckBox* case_ = nullptr;
    QCheckBox* seps_ = nullptr;
    QPushButton* go_ = nullptr;
    QPushButton* openDwg_ = nullptr;
    QPushButton* openPdf_ = nullptr;
    QPushButton* openDxf_ = nullptr;
    QPushButton* openDwx_ = nullptr;
    // The file chosen, or the one of a kind ("PDF", "DXF", "DWX", "DWG") of the same name beside it; empty: none.
    QString ofKind(const QString& path, const QString& kind) const;
    int previewToken_ = 0;
    QTableWidget* table_ = nullptr;
    DrawingPreview* preview_ = nullptr;
    QLabel* said_ = nullptr;
    QLabel* info_ = nullptr;
    QThread* worker_ = nullptr;
    std::shared_ptr<std::atomic<bool>> halt_;
    int scanned_ = 0;
    int unreadable_ = 0;
    QHash<QString, Found> found_;
    QString chosen_;
    QPointer<OdaConverter> oda_;
};

} // namespace Edraw_Desktop
