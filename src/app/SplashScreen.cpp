#include "app/SplashScreen.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>

#include <algorithm>

namespace Edraw_Desktop {
namespace {

constexpr int kW = 640;
constexpr int kH = 380;

// Brand colours, taken from the logo.
const QColor kCard(247, 249, 251);
const QColor kInk(24, 38, 56);
const QColor kRed(178, 32, 46);
const QColor kTeal(31, 122, 140);
const QColor kLine(214, 222, 230);

// Where a logo picture may be. Beside the executable for an installed copy; up one and into
// resources/ for a build tree (build/Edraw.exe -> ../resources); and the compiled-in copy last,
// so dropping a file next to the exe overrides it without a rebuild.
QPixmap findLogo() {
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QStringList paths = {exeDir + QStringLiteral("/edraw-logo.png"),
                              exeDir + QStringLiteral("/resources/edraw-logo.png"),
                              exeDir + QStringLiteral("/../resources/edraw-logo.png"),
                              QStringLiteral(":/edraw-logo.png")};
    for (const QString& p : paths) {
        if (!p.startsWith(QLatin1Char(':')) && !QFileInfo::exists(p)) continue;
        QPixmap pm(p);
        if (!pm.isNull()) return pm;
    }
    return {};
}

} // namespace

SplashScreen::SplashScreen(const QStringList& modules, QWidget* parent)
    : QWidget(parent, Qt::SplashScreen | Qt::FramelessWindowHint), modules_(modules) {
    // The application stylesheet paints every QWidget; the card is drawn here instead, so the
    // window itself has to stay out of the way for the rounded corners to read.
    setAttribute(Qt::WA_TranslucentBackground);
    setStyleSheet(QStringLiteral("background:transparent;"));
    resize(kW, kH);
    logo_ = findLogo();
    if (QScreen* sc = QApplication::primaryScreen())
        move(sc->geometry().center() - QPoint(kW / 2, kH / 2));
    shown_.start();
}

void SplashScreen::step(const QString& module) {
    if (!current_.isEmpty()) ++done_;
    current_ = module;
    repaint(); // not update(): the caller blocks straight after this
    QCoreApplication::processEvents();
}

void SplashScreen::finish() {
    if (!current_.isEmpty()) ++done_;
    current_ = QStringLiteral("Ready");
    repaint();
    // Long enough to be read, and no longer: whatever the load already took counts towards it.
    while (shown_.elapsed() < 500) {
        QCoreApplication::processEvents();
    }
    close();
}

void SplashScreen::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // The card.
    QPainterPath card;
    card.addRoundedRect(QRectF(0, 0, width(), height()), 14, 14);
    p.setClipPath(card);
    p.fillPath(card, kCard);

    // A faint drafting grid, so the plate reads as a drawing sheet.
    p.setPen(QPen(kLine, 1));
    for (int x = 20; x < width(); x += 20) p.drawLine(x, 0, x, height());
    for (int y = 20; y < height(); y += 20) p.drawLine(0, y, width(), y);

    const int pad = 34;
    const QRect logoBox(pad, 28, width() - pad * 2, 214);
    if (!logo_.isNull()) {
        const QPixmap scaled = logo_.scaled(logoBox.size(), Qt::KeepAspectRatio,
                                            Qt::SmoothTransformation);
        p.drawPixmap(logoBox.center().x() - scaled.width() / 2,
                     logoBox.center().y() - scaled.height() / 2, scaled);
    } else {
        // No picture: the wordmark, drawn.
        QFont f = font();
        f.setPixelSize(58);
        f.setBold(true);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 2);
        p.setFont(f);
        const QFontMetrics fm(f);
        const QString a = QStringLiteral("E"), b = QStringLiteral("DRAW");
        const int total = fm.horizontalAdvance(a + b);
        int x = logoBox.center().x() - total / 2;
        const int y = logoBox.center().y() + 6;
        p.setPen(kRed);
        p.drawText(x, y, a);
        x += fm.horizontalAdvance(a);
        p.setPen(kInk);
        p.drawText(x, y, b);

        f.setPixelSize(15);
        f.setBold(false);
        f.setLetterSpacing(QFont::AbsoluteSpacing, 3);
        p.setFont(f);
        p.setPen(kInk);
        p.drawText(QRect(logoBox.left(), y + 12, logoBox.width(), 24), Qt::AlignHCenter,
                   QStringLiteral("HARNESS & CABLE CAD SUITE"));
    }

    // The module line: what is loading right now, and how far through the list it is.
    QFont f = font();
    f.setPixelSize(13);
    p.setFont(f);
    p.setPen(kInk);
    const int barY = height() - 62;
    p.drawText(QRect(pad, barY - 26, width() - pad * 2, 20), Qt::AlignLeft | Qt::AlignVCenter,
               current_ == QLatin1String("Ready")
                   ? QStringLiteral("Ready")
                   : QStringLiteral("Loading %1…").arg(current_));
    const int total = std::max(1, static_cast<int>(modules_.size()));
    p.setPen(QColor(120, 132, 146));
    p.drawText(QRect(pad, barY - 26, width() - pad * 2, 20), Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("%1 / %2").arg(std::min(done_, total)).arg(total));

    // The bar.
    const QRectF track(pad, barY, width() - pad * 2, 4);
    p.setPen(Qt::NoPen);
    p.setBrush(kLine);
    p.drawRoundedRect(track, 2, 2);
    const double frac = std::min(1.0, static_cast<double>(done_) / total);
    QLinearGradient g(track.topLeft(), track.topRight());
    g.setColorAt(0.0, kTeal);
    g.setColorAt(1.0, kRed);
    p.setBrush(g);
    p.drawRoundedRect(QRectF(track.left(), track.top(), track.width() * frac, track.height()), 2,
                      2);

    f.setPixelSize(11);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 2);
    p.setFont(f);
    p.setPen(QColor(140, 150, 162));
    p.drawText(QRect(pad, height() - 40, width() - pad * 2, 24), Qt::AlignHCenter,
               QStringLiteral("PRECISE  |  CONNECTED"));
}

} // namespace Edraw_Desktop
