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
#include <cmath>

namespace Loopwire {
namespace {

constexpr int kW = 760;
constexpr int kH = 440;
constexpr int kShadow = 20;   // the room round the card for its shadow

// Brand colours, taken from the logo: its dark lettering, and the wires it is made of.
const QColor kCard(255, 255, 255);
const QColor kInk(24, 30, 42);
const QColor kSoft(122, 132, 148);
const QColor kLine(226, 231, 238);
const QColor kWires[] = {QColor(222, 38, 44), QColor(24, 96, 214), QColor(247, 196, 22), QColor(32, 158, 86)};
const QColor kDot(22, 98, 232);   // the blue dot over the i

// Where a logo picture may be. Beside the executable for an installed copy; up one and into
// resources/ for a build tree (build/Loopwire.exe -> ../resources); and the compiled-in copy last,
// so dropping a file next to the exe overrides it without a rebuild.
QPixmap findLogo() {
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QStringList paths = {exeDir + QStringLiteral("/loopwire-logo.png"),
                              exeDir + QStringLiteral("/resources/loopwire-logo.png"),
                              exeDir + QStringLiteral("/../resources/loopwire-logo.png"),
                              QStringLiteral(":/loopwire-logo.png")};
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

    // THE CARD, floating: a soft shadow of stacked translucent rounded rectangles, then a white plate.
    const QRectF plate(kShadow, kShadow - 6, width() - 2.0 * kShadow, height() - 2.0 * kShadow);
    p.setPen(Qt::NoPen);
    for (int i = 0; i < kShadow; ++i) {
        p.setBrush(QColor(20, 30, 50, 3));
        p.drawRoundedRect(plate.adjusted(-i * 0.8, -i * 0.8 + 8, i * 0.8, i * 0.8 + 8), 18 + i, 18 + i);
    }
    QPainterPath card;
    card.addRoundedRect(plate, 18, 18);
    p.setClipPath(card);
    p.fillPath(card, kCard);
    // A band of the wires' colours along the top edge - the logo's wires, laid side by side.
    const double bandW = plate.width() / 4.0;
    for (int i = 0; i < 4; ++i) p.fillRect(QRectF(plate.left() + i * bandW, plate.top(), bandW + 1, 5), kWires[i]);

    const int pad = 44;
    const QRectF inner = plate.adjusted(pad, 0, -pad, 0);
    // THE LOGO, as large as the card allows, in the upper part.
    const QRectF logoBox(inner.left(), plate.top() + 34, inner.width(), 236);
    if (!logo_.isNull()) {
        const QPixmap scaled = logo_.scaled(logoBox.size().toSize(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        p.drawPixmap(QPointF(logoBox.center().x() - scaled.width() / 2.0, logoBox.center().y() - scaled.height() / 2.0), scaled);
    } else {
        // No picture: the wordmark, drawn.
        QFont wf = font();
        wf.setPixelSize(86);
        wf.setBold(true);
        p.setFont(wf);
        p.setPen(kInk);
        p.drawText(logoBox, Qt::AlignCenter, QStringLiteral("Loopwire"));
    }
    QFont f = font();
    f.setPixelSize(12);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 3.2);
    p.setFont(f);
    p.setPen(kSoft);
    p.drawText(QRectF(inner.left(), logoBox.bottom() + 2, inner.width(), 20), Qt::AlignHCenter | Qt::AlignVCenter,
               QStringLiteral("HARNESS  &  CABLE  DESIGN"));

    // WHAT IS LOADING right now, and how far through the list it is.
    f = font();
    f.setPixelSize(13);
    p.setFont(f);
    const double barY = plate.bottom() - 52;
    const bool ready = current_ == QLatin1String("Ready");
    p.setPen(kInk);
    p.drawText(QRectF(inner.left(), barY - 28, inner.width(), 20), Qt::AlignLeft | Qt::AlignVCenter,
               ready ? QStringLiteral("Ready") : QStringLiteral("Loading %1…").arg(current_));
    const int total = std::max(1, static_cast<int>(modules_.size()));
    p.setPen(kSoft);
    p.drawText(QRectF(inner.left(), barY - 28, inner.width(), 20), Qt::AlignRight | Qt::AlignVCenter,
               QStringLiteral("%1 / %2").arg(std::min(done_, total)).arg(total));

    // THE PROGRESS, a cable being pulled: a grey track, and over it four strands - the logo's red, blue, yellow and
    // green - as far as it has got, ending in the logo's blue dot.
    const QRectF track(inner.left(), barY, inner.width(), 8);
    p.setPen(Qt::NoPen);
    p.setBrush(kLine);
    p.drawRoundedRect(track, 4, 4);
    const double frac = std::min(1.0, static_cast<double>(done_) / total);
    const double filled = track.width() * frac;
    if (filled > 0.5) {
        p.save();
        QPainterPath cable;
        cable.addRoundedRect(QRectF(track.left(), track.top(), filled, track.height()), 4, 4);
        p.setClipPath(cable);
        const double strand = track.height() / 4.0;
        for (int i = 0; i < 4; ++i) p.fillRect(QRectF(track.left(), track.top() + i * strand, filled, strand + 0.5), kWires[i]);
        // A gleam along the top, as on the logo's cable.
        p.fillRect(QRectF(track.left(), track.top(), filled, 1.5), QColor(255, 255, 255, 70));
        p.restore();
    }
    const double pulse = 0.5 + 0.5 * std::sin(shown_.elapsed() / 160.0);
    const double dotR = ready ? 6.0 : 5.0 + pulse;
    p.setBrush(QColor(kDot.red(), kDot.green(), kDot.blue(), 60));
    p.drawEllipse(QPointF(track.left() + filled, track.center().y()), dotR + 4.0, dotR + 4.0);
    p.setBrush(kDot);
    p.drawEllipse(QPointF(track.left() + filled, track.center().y()), dotR, dotR);

    f.setPixelSize(11);
    f.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
    p.setFont(f);
    p.setPen(QColor(150, 158, 172));
    p.drawText(QRectF(inner.left(), plate.bottom() - 30, inner.width(), 18), Qt::AlignHCenter | Qt::AlignVCenter,
               QStringLiteral("Version %1   |   Whatson").arg(QCoreApplication::applicationVersion()));
}

} // namespace Loopwire
