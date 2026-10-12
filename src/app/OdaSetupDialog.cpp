#include "app/OdaSetupDialog.h"

#include "io/OdaConverter.h"

#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>

namespace Loopwire {

namespace {

// One button in the list: a title, the sentence under it, and what it does. Written as a helper
// because four near-identical twelve-line blocks is how the four drift apart.
QPushButton* addChoice(QVBoxLayout* col, QWidget* parent, const QString& title,
                       const QString& detail, bool primary) {
    auto* btn = new QPushButton(title, parent);
    btn->setMinimumHeight(34);
    btn->setCursor(Qt::PointingHandCursor);
    if (primary) btn->setDefault(true);
    col->addWidget(btn);

    auto* note = new QLabel(detail, parent);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color:#9aa7b8;font-size:11px;margin-left:2px;"));
    col->addWidget(note);
    col->addSpacing(10);
    return btn;
}

} // namespace

OdaSetupDialog::OdaSetupDialog(OdaConverter* manager, QWidget* parent, Purpose purpose)
    : QDialog(parent), manager_(manager), purpose_(purpose) {
    const bool importing = purpose_ == Purpose::ImportDwg;
    setWindowTitle(importing ? QStringLiteral("Native DWG Import Setup")
                             : QStringLiteral("Native DWG Export Setup"));
    setModal(true);
    setMinimumWidth(520);

    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(22, 20, 22, 18);
    col->setSpacing(0);

    auto* head = new QLabel(QStringLiteral("The ODA File Converter was not found"), this);
    head->setStyleSheet(QStringLiteral("font-size:16px;font-weight:600;"));
    col->addWidget(head);
    col->addSpacing(6);

    // WHY, in the user's terms. "Not found" on its own reads as a fault in Loopwire, and the user's
    // next move depends on understanding that this is a free tool they install once.
    auto* why = new QLabel(
        importing
            ? QStringLiteral(
                  "Loopwire reads DWG using the <b>ODA File Converter</b>, a free utility from the Open "
                  "Design Alliance. Loopwire cannot include it, because its licence is granted to you by "
                  "ODA when you download it &mdash; so it is installed once, by you, and Loopwire finds "
                  "it from then on.<br><br>"
                  "If you only need this one drawing, whoever sent it can usually save it as DXF, "
                  "which Loopwire opens on its own.")
            : QStringLiteral(
                  "Loopwire writes DWG using the <b>ODA File Converter</b>, a free utility from the Open "
                  "Design Alliance. Loopwire cannot include it, because its licence is granted to you by "
                  "ODA when you download it &mdash; so it is installed once, by you, and Loopwire finds "
                  "it from then on.<br><br>"
                  "You do not need it to keep working: DXF is read by every CAD program, including "
                  "AutoCAD."),
        this);
    why->setWordWrap(true);
    why->setTextFormat(Qt::RichText);
    col->addWidget(why);
    col->addSpacing(16);

    auto* download = addChoice(
        col, this, QStringLiteral("Download ODA Converter (Free)"),
        QStringLiteral("Opens opendesign.com in your browser. Registration is free. Install it, then "
                       "come back and choose Re-detect."),
        /*primary=*/true);

    auto* redetectBtn = addChoice(
        col, this, QStringLiteral("Re-detect Installation"),
        QStringLiteral("Look again, for when you have just installed it. Your drawing and this "
                       "export are kept."),
        false);

    auto* browseBtn = addChoice(
        col, this, QStringLiteral("Browse Executable..."),
        QStringLiteral("Point Loopwire at the converter yourself, if it is installed somewhere "
                       "unusual. Remembered for next time."),
        false);

    // Only when exporting. See the note on Purpose: offering to export a DXF to somebody who is
    // trying to read a DWG answers a question they did not ask.
    QPushButton* dxfBtn = nullptr;
    if (!importing) {
        dxfBtn = addChoice(
            col, this, QStringLiteral("Export as DXF Instead"),
            QStringLiteral("Save the drawing as DXF now, with no extra software. This is the option "
                           "to take if you just need the drawing out of Loopwire."),
            false);
    }

    // The line that reports what just happened - a re-detect that failed, a file that was not the
    // converter. Empty until there is something to say, so the dialog does not open with a blank
    // row of space where an error will later go.
    status_ = new QLabel(this);
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::RichText);
    status_->setVisible(false);
    col->addWidget(status_);

    auto* bb = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    col->addSpacing(4);
    col->addWidget(bb);

    connect(download,   &QPushButton::clicked, this, &OdaSetupDialog::openDownloadPage);
    connect(redetectBtn,&QPushButton::clicked, this, &OdaSetupDialog::redetect);
    connect(browseBtn,  &QPushButton::clicked, this, &OdaSetupDialog::browse);
    if (dxfBtn) connect(dxfBtn, &QPushButton::clicked, this, &OdaSetupDialog::useDxfInstead);
}

void OdaSetupDialog::openDownloadPage() {
    const QUrl url(OdaConverter::downloadPageUrl());
    if (!QDesktopServices::openUrl(url)) {
        // A browser that would not open leaves the user with nothing to act on unless the address
        // itself is put in front of them, selectable.
        status_->setText(QStringLiteral(
            "<span style='color:#e0a852;'>Could not open a browser. The address is:<br>"
            "<b>%1</b></span>").arg(url.toString()));
        status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        status_->setVisible(true);
        return;
    }
    status_->setText(QStringLiteral(
        "Opened <b>opendesign.com</b> in your browser. Once the converter is installed, choose "
        "<b>Re-detect Installation</b>."));
    status_->setVisible(true);
}

void OdaSetupDialog::redetect() {
    if (manager_->redetect()) {
        showFound();
        return;
    }
    // Where it looked, so "still not found" is actionable. A user who installed it somewhere else
    // can see that from this list and reach for Browse.
    QString where;
    for (const QString& p : OdaConverter::searchedLocations())
        where += QStringLiteral("<br>&nbsp;&nbsp;%1").arg(p.toHtmlEscaped());
    status_->setText(QStringLiteral(
        "<span style='color:#e0a852;'>Still not found.</span> Loopwire looked in:%1<br><br>"
        "If it is installed somewhere else, use <b>Browse Executable...</b>").arg(where));
    status_->setVisible(true);
}

void OdaSetupDialog::browse() {
#ifdef Q_OS_WIN
    const QString filter = QStringLiteral("ODA File Converter (ODAFileConverter.exe);;"
                                          "Programs (*.exe);;All files (*)");
#else
    const QString filter = QStringLiteral("ODA File Converter (ODAFileConverter);;All files (*)");
#endif
    const QString picked = QFileDialog::getOpenFileName(
        this, QStringLiteral("Select ODAFileConverter"), QString(), filter);
    if (picked.isEmpty()) return;

    if (!OdaConverter::looksLikeConverter(picked)) {
        // Named, not guessed at: the user picked a file and is owed a reason it was refused.
        status_->setText(QStringLiteral(
            "<span style='color:#e0a852;'>That is not the ODA File Converter.</span><br>"
            "Loopwire needs the file named <b>%1</b>, which the installer puts in its own folder.<br>"
            "You chose: %2")
                             .arg(QString::fromLatin1(
#ifdef Q_OS_WIN
                                      "ODAFileConverter.exe"
#else
                                      "ODAFileConverter"
#endif
                                      ),
                                  QFileInfo(picked).fileName().toHtmlEscaped()));
        status_->setVisible(true);
        return;
    }

    OdaConverter::savePath(picked);
    if (manager_->redetect()) {
        showFound();
        return;
    }
    // Passed the name check and still did not take: the file went away between the two, or is not
    // executable by this user.
    OdaConverter::forgetPath();
    status_->setText(QStringLiteral(
        "<span style='color:#e0a852;'>That file could not be used.</span> Check that it is the "
        "converter itself and that you have permission to run it."));
    status_->setVisible(true);
}

void OdaSetupDialog::showFound() {
    const auto loc = manager_->location();
    result_ = Result::Detected;
    // Accepted straight away rather than showing a "found it!" state with an extra button to press.
    // The user asked to export a DWG; the obstacle is gone; carry on with what they asked for.
    Q_UNUSED(loc);
    accept();
}

void OdaSetupDialog::useDxfInstead() {
    result_ = Result::FallbackDxf;
    accept();
}

OdaSetupDialog::Result OdaSetupDialog::run(OdaConverter* manager, QWidget* parent,
                                          Purpose purpose) {
    OdaSetupDialog dlg(manager, parent, purpose);
    if (dlg.exec() != QDialog::Accepted) return Result::Cancelled;
    return dlg.result();
}

} // namespace Loopwire
