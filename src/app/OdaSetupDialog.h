#pragma once

#include <QDialog>
#include <QString>

// At global scope, before the namespace opens. Written inline as `class QLabel* status_` inside
// Edraw_Desktop it would declare a NEW type, Edraw_Desktop::QLabel, rather than naming Qt's - which
// compiles as a member declaration and then fails on every use of it.
class QLabel;

namespace Edraw_Desktop {

class OdaConverter;

// "Native DWG Export Setup" - what the user sees when they ask for a DWG and the converter Edraw
// needs is not on the machine.
//
// THE POINT OF THIS DIALOG IS THAT IT IS NOT A DEAD END.
//
// Edraw cannot ship the ODA File Converter (its licence is granted by ODA to the person who
// downloads it, so redistributing it is not ours to do) and will not fetch it quietly in the
// background (that would accept ODA's terms on the user's behalf). Both of those are the right call,
// and both mean a user can reach "Export DWG" and find the machine unable to do it - through no
// mistake of their own, on a fresh install, every time.
//
// So this dialog has to leave them somewhere better than where they started, whichever way they go:
//
//   Download      - opens ODA's page in the browser. They accept ODA's terms, from ODA, as they
//                   should. Edraw is not a party to it.
//   Re-detect     - for after they have installed it, without losing the drawing or the export they
//                   were part-way through.
//   Browse        - for the install that is somewhere unusual, remembered afterwards so it is asked
//                   once rather than every time.
//   Export as DXF - the way out that needs nothing at all. Every CAD program reads DXF, so a user
//                   who wants their drawing more than they want the DWG format gets it now, rather
//                   than being sent away to install software before Edraw will help them.
//
// Cancel is deliberately NOT one of the four: it exists, on the title bar and on Esc, but it is not
// offered as a button. The buttons are the ways forward.
class OdaSetupDialog : public QDialog {
    Q_OBJECT

public:
    // WHICH ERRAND THE USER IS ON. The missing tool is the same and the ways forward are nearly the
    // same, but the fourth one is not: "Export as DXF instead" is a real answer for somebody saving a
    // drawing and a nonsense one for somebody trying to READ a DWG, who would be told to export the
    // file they cannot open. So the import case offers three ways forward and not four.
    enum class Purpose { ExportDwg, ImportDwg };

    enum class Result {
        Cancelled,     // Esc, or the window closed: do nothing
        Detected,      // the converter is now available - carry on with the DWG export
        FallbackDxf,   // the user chose DXF instead - export that
    };

    // `manager` is used and not owned; re-detection updates it in place, so the caller's manager is
    // the one that ends up knowing where the converter is.
    explicit OdaSetupDialog(OdaConverter* manager, QWidget* parent = nullptr,
                            Purpose purpose = Purpose::ExportDwg);

    Result result() const { return result_; }

    // Runs the dialog and gives the answer. The whole interaction in one call, because every caller
    // wants exactly this and none of them wants to map exec()'s int onto Result themselves.
    static Result run(OdaConverter* manager, QWidget* parent,
                      Purpose purpose = Purpose::ExportDwg);

private:
    void openDownloadPage();
    void redetect();
    void browse();
    void useDxfInstead();
    void showFound();

    OdaConverter* manager_ = nullptr;
    Purpose purpose_ = Purpose::ExportDwg;
    Result result_ = Result::Cancelled;
    QLabel* status_ = nullptr;
};

} // namespace Edraw_Desktop
