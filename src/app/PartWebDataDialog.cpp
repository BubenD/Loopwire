#include "app/PartWebDataDialog.h"

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
    buttons->addButton(QStringLiteral("Cancel"), QDialogButtonBox::RejectRole);
    outer->addWidget(buttons);
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
        if (reply->error() != QNetworkReply::NoError) {
            report(QStringLiteral("The page could not be read: %1\n%2")
                       .arg(url_, reply->errorString()));
            return;
        }
        const QByteArray body = reply->readAll();
        if (body.isEmpty()) {
            report(QStringLiteral("The page came back empty: %1").arg(url_));
            return;
        }
        showFinds(QString::fromUtf8(body));
    });
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
