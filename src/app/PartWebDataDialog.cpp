#include "app/PartWebDataDialog.h"
#include <QPlainTextEdit>
#include <QGuiApplication>
#include <QFile>
#include <QClipboard>
#include "io/PdfText.h"
#include "harness/Datasheet.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace Edraw_Desktop {

PartWebDataDialog::PartWebDataDialog(const LibraryPart& part, const QString& url, QWidget* parent)
    : QDialog(parent), part_(part), url_(url) {
    setWindowTitle(QStringLiteral("Read the data off the page"));
    setModal(true);
    resize(680, 460);

    auto* outer = new QVBoxLayout(this);
    status_ = new QLabel(QStringLiteral("Reading %1 …").arg(url_), this);
    status_->setWordWrap(true);
    outer->addWidget(status_);

    list_ = new QTreeWidget(this);
    list_->setColumnCount(4);
    list_->setHeaderLabels({QStringLiteral("Field"), QStringLiteral("Read off the page"),
                            QStringLiteral("The part says"), QStringLiteral("Where it was read")});
    list_->setRootIsDecorated(false);
    list_->setAlternatingRowColors(true);
    list_->header()->setStretchLastSection(true);
    outer->addWidget(list_, 1);

    auto* note = new QLabel(
        QStringLiteral("Only the ticked lines are written into the part. A page can say \"for use with 22 "
                       "AWG\" on a part that is not 22 AWG, so each reading is shown with the words it came "
                       "from — check it before you keep it. A field the part already fills in is left "
                       "unticked."),
        this);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color:#8fa2b8;"));
    outer->addWidget(note);

    auto* buttons = new QDialogButtonBox(this);
    applyBtn_ = buttons->addButton(QStringLiteral("Fill the ticked fields"), QDialogButtonBox::AcceptRole);
    applyBtn_->setEnabled(false);
    // The two ways in when a site refuses to be read by a program - which the distributors do, all of them.
    pasteBtn_ = buttons->addButton(QStringLiteral("Paste the page text..."), QDialogButtonBox::ActionRole);
    pasteBtn_->setToolTip(QStringLiteral(
        "Open the page in the browser, select it all, copy — then paste it here. A page a person can read is "
        "a page this can read; it is only the fetching that gets refused."));
    sheetBtn_ = buttons->addButton(QStringLiteral("Read the datasheet"), QDialogButtonBox::ActionRole);
    sheetBtn_->setToolTip(QStringLiteral(
        "Read the PDF datasheet instead: the document the numbers actually come from. The one attached to "
        "this part, or one linked on the page."));
    buttons->addButton(QStringLiteral("Cancel"), QDialogButtonBox::RejectRole);
    outer->addWidget(buttons);
    connect(pasteBtn_, &QPushButton::clicked, this, [this] { pastePageText(); });
    connect(sheetBtn_, &QPushButton::clicked, this, [this] { readDatasheet(); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // Started after the dialog is up, so it is plain that something is happening.
    QTimer::singleShot(0, this, [this] { startFetch(); });
}

void PartWebDataDialog::report(const QString& what) {
    if (status_) status_->setText(what);
}

void PartWebDataDialog::startFetch() {
    const QUrl url(url_);
    if (!url.isValid() || url.scheme().isEmpty() ||
        (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https"))) {
        report(QStringLiteral("That is not a web address this can read: %1\nA link must start with http:// "
                              "or https://.")
                   .arg(url_));
        return;
    }
    auto* net = new QNetworkAccessManager(this);
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    // Said plainly: this is a drawing program reading a page a person pointed it at.
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Edraw/1.0 (parts library data reader)"));
    request.setTransferTimeout(15000);
    QNetworkReply* reply = net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const QString html = QString::fromUtf8(body);
        // What actually happened, said plainly. "Nothing could be read" reads as "this part has no data",
        // when what happened was that the site refused to answer a program at all - and the way round it is
        // a different way in, not a different part.
        QString refused;
        if (reply->error() != QNetworkReply::NoError || pageWasRefused(html, status, &refused)) {
            const QString what = !refused.isEmpty()
                                     ? refused
                                     : QStringLiteral("The site answered: %1").arg(reply->errorString());
            report(QStringLiteral("%1 could not be read.\n\n%2\n\nThe way in: press Paste the page text, "
                                  "open the page in your browser (the Open button beside the link), select "
                                  "it all and copy it — or press Read the datasheet, which is the document "
                                  "the numbers come from anyway.")
                       .arg(url_, what));
            return;
        }
        if (body.isEmpty()) {
            report(QStringLiteral("The page came back empty: %1").arg(url_));
            return;
        }
        showFinds(html);
    });
}

void PartWebDataDialog::showFindsFromText(const QString& text, const QString& source) {
    finds_ = partFieldsFromText(text, part_, source);
    if (finds_.empty()) {
        report(QStringLiteral("Nothing technical could be read out of %1.\n\nWhat this reads is the words a "
                              "catalogue uses - a gauge, a way count, a pitch, a temperature range, written "
                              "as a table row or in a sentence. Text that has none of those in it has "
                              "nothing to give.")
                   .arg(source));
        return;
    }
    report(QStringLiteral("%1 thing%2 read out of %3.")
               .arg(finds_.size())
               .arg(finds_.size() == 1 ? QString() : QStringLiteral("s"), source));
    fillList();
}

void PartWebDataDialog::showFinds(const QString& html) {
    finds_ = partFieldsFromPage(html, part_);
    const QStringList sheets = datasheetLinks(html, QUrl(url_));
    datasheet_ = sheets.isEmpty() ? QString() : sheets.front();

    if (finds_.empty()) {
        report(QStringLiteral("Nothing could be read off %1.\nA page that keeps its data in a picture or "
                              "behind a script has nothing for a reader to find — the datasheet is the way "
                              "in for those.")
                   .arg(url_));
        return;
    }
    report(QStringLiteral("%1 thing%2 read off %3.%4")
               .arg(finds_.size())
               .arg(finds_.size() == 1 ? QString() : QStringLiteral("s"), url_)
               .arg(datasheet_.isEmpty()
                        ? QString()
                        : QStringLiteral("\nA datasheet is linked there too: %1").arg(datasheet_)));

    fillList();
}

void PartWebDataDialog::fillList() {
    if (!list_) return;
    list_->clear();
    for (const WebFieldFind& f : finds_) {
        auto* item = new QTreeWidgetItem(list_);
        item->setText(0, f.label);
        item->setText(1, f.value);
        item->setText(2, f.key == QStringLiteral("__description")
                             ? part_.description
                             : part_.params.value(f.key));
        item->setText(3, f.evidence);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        // A field the part already fills in starts unticked: what somebody typed is worth more than what a
        // page was read to say, until they decide otherwise.
        item->setCheckState(0, f.exists ? Qt::Unchecked : Qt::Checked);
        item->setData(0, Qt::UserRole, f.key);
    }
    if (datasheet_.isEmpty() == false) {
        auto* item = new QTreeWidgetItem(list_);
        item->setText(0, QStringLiteral("Datasheet link"));
        item->setText(1, datasheet_);
        item->setText(3, QStringLiteral("a link on the page"));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, Qt::Checked);
        item->setData(0, Qt::UserRole, QStringLiteral("__datasheet"));
    }
    for (int c = 0; c < 3; ++c) list_->resizeColumnToContents(c);
    if (applyBtn_) applyBtn_->setEnabled(true);
}

void PartWebDataDialog::pastePageText() {
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Paste the page"));
    dlg.resize(620, 420);
    auto* v = new QVBoxLayout(&dlg);
    v->addWidget(new QLabel(
        QStringLiteral("Open the part's page in your browser, select it all (Ctrl+A), copy it (Ctrl+C), and "
                       "paste it here (Ctrl+V). The table of technical data is what this reads."),
        &dlg));
    auto* box = new QPlainTextEdit(&dlg);
    box->setPlaceholderText(QStringLiteral("paste the page here"));
    box->setPlainText(QGuiApplication::clipboard() ? QGuiApplication::clipboard()->text() : QString());
    v->addWidget(box, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    v->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    const QString text = box->toPlainText();
    if (text.trimmed().isEmpty()) {
        report(QStringLiteral("Nothing was pasted."));
        return;
    }
    showFindsFromText(text, QStringLiteral("the pasted page"));
}

void PartWebDataDialog::readDatasheet() {
    // The one attached to the part first: it is already on this machine, it needs no network at all, and it
    // is the document the numbers come from.
    QString path;
    if (part_.hasDatasheet()) path = DatasheetStore::pathOf(part_.datasheet());
    if (!path.isEmpty() && QFile::exists(path)) {
        QString how;
        const QString text = PdfText::extractFromFile(path, &how);
        if (text.trimmed().isEmpty()) {
            report(QStringLiteral("The datasheet attached to this part could not be read: %1").arg(how));
            return;
        }
        showFindsFromText(text, QStringLiteral("the attached datasheet"));
        return;
    }
    // Else a PDF: the one found on the page, or the link itself if it points at one.
    QString pdfUrl = datasheet_;
    if (pdfUrl.isEmpty() && url_.contains(QStringLiteral(".pdf"), Qt::CaseInsensitive)) pdfUrl = url_;
    if (pdfUrl.isEmpty()) {
        report(QStringLiteral("There is no datasheet to read: none attached to this part, and none found on "
                              "the page. Attach the PDF to the part (Attach… beside Datasheets) and press "
                              "this again."));
        return;
    }
    report(QStringLiteral("Reading the datasheet: %1 …").arg(pdfUrl));
    auto* net = new QNetworkAccessManager(this);
    const QUrl asked(pdfUrl);
    QNetworkRequest request(asked);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Edraw/1.0 (parts library data reader)"));
    request.setTransferTimeout(30000);
    QNetworkReply* reply = net->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, pdfUrl] {
        reply->deleteLater();
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError || body.isEmpty()) {
            report(QStringLiteral("The datasheet could not be fetched: %1\n%2\n\nDownload it in your "
                                  "browser and attach it to the part, then press Read the datasheet again.")
                       .arg(pdfUrl, reply->errorString()));
            return;
        }
        QString how;
        const QString text = PdfText::extract(body, &how);
        if (text.trimmed().isEmpty()) {
            report(QStringLiteral("That datasheet gave no text: %1\n\nA scanned datasheet is a picture of "
                                  "text, and reading it would need character recognition.")
                       .arg(how));
            return;
        }
        showFindsFromText(text, QStringLiteral("the datasheet"));
    });
}

std::vector<WebFieldFind> PartWebDataDialog::chosen() const {
    std::vector<WebFieldFind> out;
    if (!list_) return out;
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
        const QTreeWidgetItem* item = list_->topLevelItem(i);
        if (item->checkState(0) != Qt::Checked) continue;
        const QString key = item->data(0, Qt::UserRole).toString();
        if (key == QStringLiteral("__datasheet")) continue;   // handled by datasheetLink()
        for (const WebFieldFind& f : finds_)
            if (f.key == key) out.push_back(f);
    }
    return out;
}

QString PartWebDataDialog::datasheetLink() const {
    if (!list_ || datasheet_.isEmpty()) return QString();
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
        const QTreeWidgetItem* item = list_->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString() == QStringLiteral("__datasheet") &&
            item->checkState(0) == Qt::Checked)
            return datasheet_;
    }
    return QString();
}

} // namespace Edraw_Desktop
