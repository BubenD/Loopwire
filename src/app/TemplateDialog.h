#pragma once

#include "model/DrawingTemplate.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;

namespace Edraw_Desktop {

class DrawingPreview;
class Editor;

// The template module: what a new drawing starts as, edited and saved as a file that can be handed
// to someone else.
//
// A template holds the sheet and its border, the layers, and the defaults a drawing is worked in.
// The border and title block are not described in a form - they are DRAWN, with the ordinary tools,
// and captured from the sheet. That way a house title block is made once and every drawing starts
// with it.
//
// Templates are files in the template folder, saved as .etp.
class TemplateDialog : public QDialog {
    Q_OBJECT
public:
    explicit TemplateDialog(Editor* editor, QWidget* parent = nullptr);

    // Files.
    bool saveToFolder();                       // <app data>/template/<name>.etp
    bool saveAs();
    bool loadFrom(const QString& path);
    void refreshInstalled();
    // The template a NEW drawing starts from, remembered between sessions. Empty means an empty sheet.
    static QString defaultTemplatePath();
    static void setDefaultTemplatePath(const QString& path);
    // Put this template into the drawing that is open.
    void applyToDrawing();
    // Take the sheet as the template's content.
    void captureFromDrawing();
    // Replace the content with a plain bordered sheet and title block.
    void resetContent();

    const DrawingTemplate& templateData() const { return tpl_; }

signals:
    // The host opens the template on a tab of its own, on a tinted canvas, so the sheet can be drawn
    // with the ordinary tools and finished with Save template.
    void drawOnSheetRequested(const QString& name);

public:
    void setTemplateData(const DrawingTemplate& tpl);

private:
    void buildUi();
    void readFromControls();
    void writeToControls();
    void refreshPreview();
    void refreshLayerTable();
    void layersFromTable();

    Editor* editor_ = nullptr;
    DrawingTemplate tpl_;
    QString path_;          // where it came from / last went, so Save is not Save As
    bool syncing_ = false;
    // True while the sheet is the one this window generates - and so may be regenerated as the
    // fields change. False once a sheet has been captured or opened, because that one is the user's.
    bool generatedContent_ = true;

    QLineEdit* nameEdit_ = nullptr;
    QPlainTextEdit* descEdit_ = nullptr;
    QLineEdit* authorEdit_ = nullptr;
    QLineEdit* revEdit_ = nullptr;
    // Who drew it, who checked it, who approved it - and the date against each. Three rows of a name
    // and a date, which go into the title block.
    struct SignatureRow {
        QLineEdit* name = nullptr;
        QLineEdit* date = nullptr;
    };
    SignatureRow drawnRow_;
    SignatureRow checkedRow_;
    SignatureRow approvedRow_;

    QComboBox* sheetBox_ = nullptr;
    QDoubleSpinBox* widthSpin_ = nullptr;
    QDoubleSpinBox* heightSpin_ = nullptr;
    QCheckBox* landscapeChk_ = nullptr;
    // The grid reference frame.
    QCheckBox* zonesChk_ = nullptr;
    QDoubleSpinBox* zoneSizeSpin_ = nullptr;
    QDoubleSpinBox* zoneBandSpin_ = nullptr;
    QCheckBox* lettersTopChk_ = nullptr;
    QLabel* zoneCount_ = nullptr;
    // The revision table: whether it is there, how many rows, and which columns are shown. Each column
    // is a checkbox with the width beside it, because hiding a column has to take its width with it.
    QCheckBox* revChk_ = nullptr;
    QSpinBox* revRowsSpin_ = nullptr;
    QDoubleSpinBox* revRowHSpin_ = nullptr;
    struct RevColumn {
        QCheckBox* show = nullptr;
        QDoubleSpinBox* width = nullptr;
    };
    RevColumn revCols_[7];
    QLabel* revSummary_ = nullptr;
    // The boxes each drawing fills in.
    QTableWidget* fields_ = nullptr;

    QTableWidget* layers_ = nullptr;
    QComboBox* activeLayerBox_ = nullptr;

    QComboBox* unitBox_ = nullptr;
    QDoubleSpinBox* textSpin_ = nullptr;
    QDoubleSpinBox* dimTextSpin_ = nullptr;
    QDoubleSpinBox* arrowSpin_ = nullptr;
    QSpinBox* decimalsSpin_ = nullptr;
    QDoubleSpinBox* gridSpin_ = nullptr;
    QCheckBox* gridChk_ = nullptr;
    QCheckBox* snapChk_ = nullptr;
    QCheckBox* awgChk_ = nullptr;

    QListWidget* installed_ = nullptr;
    DrawingPreview* preview_ = nullptr;
    QLabel* summary_ = nullptr;
};

} // namespace Edraw_Desktop
