#pragma once

#include "harness/PartDataFromWeb.h"

#include <QDialog>

class QLabel;
class QTreeWidget;
class QPushButton;

namespace Edraw_Desktop {

// Fetching a part's page and showing what could be read off it.
//
// Every find is shown with the words it was read from, and has to be TICKED before it is written into the
// part. That is not caution for its own sake: a catalogue page says "for use with 22 AWG wire" on a part
// that is not 22 AWG, calls a pitch a grid, and lists the temperature range of the family rather than of the
// size. A reading is a suggestion, and it is shown as one - beside what the part already says, so replacing
// a value is a decision and not an accident.
//
// The download is plain HTTP(S) of the one page. Nothing else is fetched, nothing is sent, and a page that
// hides its table behind a script yields nothing - which is reported as nothing found rather than as
// success.
class PartWebDataDialog : public QDialog {
    Q_OBJECT
public:
    PartWebDataDialog(const LibraryPart& part, const QString& url, QWidget* parent = nullptr);

    // The finds that were ticked, ready for applyWebFinds().
    std::vector<WebFieldFind> chosen() const;
    // A datasheet found on the page, if one was and the user asked to keep the link.
    QString datasheetLink() const;

private:
    void startFetch();
    void showFinds(const QString& html);
    void report(const QString& what);

    LibraryPart part_;
    QString url_;
    QString datasheet_;
    std::vector<WebFieldFind> finds_;

    QLabel* status_ = nullptr;
    QTreeWidget* list_ = nullptr;
    QPushButton* applyBtn_ = nullptr;
};

} // namespace Edraw_Desktop
