#pragma once

#include <QDialog>
#include <QPointer>

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

// FIND DRAWINGS: the DWX, DXF and DWG files under a folder (and its folders), found by their name - or by the drawing
// number, title and revision written in them - listed with those, and the one chosen shown in a preview beside the list.
// Searched in the background, stopped at any time. Opened from here, or (asked for one) chosen - to link a cable to it.
class DrawingFinderDialog : public QDialog {
    Q_OBJECT
public:
    explicit DrawingFinderDialog(QWidget* parent = nullptr, bool choosing = false);
    ~DrawingFinderDialog() override;

    void setFolder(const QString& folder);
    void setText(const QString& text);
    // THE SEARCH, from the folder, for the text; `wait` - until it is done (the tests).
    void search(bool wait = false);
    void stop();
    bool searching() const;
    int resultCount() const;
    QString resultPath(int row) const;
    void showResult(int row);
    QString chosen() const { return chosen_; }

    // A DRAWING CHOSEN - the dialog in choosing mode; empty where none was.
    static QString choose(QWidget* parent, const QString& text = QString());

signals:
    // A drawing to open, asked for with Open.
    void openRequested(const QString& path);

private:
    struct Found {
        QString path;
        QString drawingNo;
        QString revision;
        QString title;
        qint64 modified = 0;
    };
    void addFound(const Found& f);
    void finished();
    void preview(const QString& path);

    bool choosing_ = false;
    QLineEdit* folder_ = nullptr;
    QLineEdit* text_ = nullptr;
    QCheckBox* sub_ = nullptr;
    QCheckBox* dwx_ = nullptr;
    QCheckBox* dxf_ = nullptr;
    QCheckBox* dwg_ = nullptr;
    QCheckBox* inside_ = nullptr;
    QPushButton* go_ = nullptr;
    QTableWidget* table_ = nullptr;
    DrawingPreview* preview_ = nullptr;
    QLabel* said_ = nullptr;
    QLabel* info_ = nullptr;
    QThread* worker_ = nullptr;
    std::shared_ptr<std::atomic<bool>> halt_;
    int scanned_ = 0;
    QString chosen_;
    QPointer<OdaConverter> oda_;
};

} // namespace Edraw_Desktop
