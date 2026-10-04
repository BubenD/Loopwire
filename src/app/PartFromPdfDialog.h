#pragma once

#include "geometry/Geometry.h"
#include "harness/PartFromPdf.h"
#include "model/Entity.h"

#include <QDialog>
#include <QJsonArray>

#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QTableWidget;

namespace Edraw_Desktop {

class DrawingPreview;

// DRAW A PART FROM ITS DATASHEET. The PDF is read as the drawing it is - lines where the page draws them,
// text where the page sets it - and shown a page at a time. A box drawn round one view of the part takes
// that view as the part's mechanical drawing, at the width the datasheet gives it; the pinout is read from
// the pin table, found by itself or read from a box drawn round it, and can be put right in the table
// before it is taken.
class PartFromPdfDialog : public QDialog {
    Q_OBJECT
public:
    PartFromPdfDialog(const QString& pdfPath, const QString& partTitle, QWidget* parent = nullptr);

    // Whether the PDF could be read at all, and why not.
    bool loaded() const { return loaded_; }
    QString error() const { return error_; }

    // What was asked for. The drawing at its width, centred on the origin; the pinout as the table says.
    bool takeDrawing() const;
    bool takePinout() const;
    QJsonArray drawing() const;
    std::vector<PdfPin> pinout() const;

    // Driving it as a click would, for the tests.
    int pageCount() const { return static_cast<int>(pages_.size()); }
    void showPage(int index);
    void setBox(const Bounds& box);
    void setWidthMm(double mm);
    void readPinsFromBox();

private:
    void boxChanged();
    void refreshResult();
    void fillPins(const std::vector<PdfPin>& pins);
    std::vector<Entity> picked() const;

    bool loaded_ = false;
    QString error_;
    std::vector<Entity> all_;           // every object of every page
    std::vector<Bounds> pages_;         // where each page lies
    std::vector<Entity> pageEnts_;      // the page showing
    bool widthTyped_ = false;           // the width was typed in, and is not to follow the box

    QComboBox* page_ = nullptr;
    DrawingPreview* view_ = nullptr;
    DrawingPreview* result_ = nullptr;
    QDoubleSpinBox* width_ = nullptr;
    QCheckBox* withText_ = nullptr;
    QLabel* boxSaid_ = nullptr;
    QLabel* pinsSaid_ = nullptr;
    QTableWidget* pins_ = nullptr;
    QCheckBox* takeDrawing_ = nullptr;
    QCheckBox* takePinout_ = nullptr;
};

} // namespace Edraw_Desktop
