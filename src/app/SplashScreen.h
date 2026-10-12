#pragma once

#include <QElapsedTimer>
#include <QPixmap>
#include <QStringList>
#include <QWidget>

namespace Loopwire {

// The window shown while the application builds itself: the Loopwire logo, and the module
// currently being loaded. The steps are real - MainWindow reports each one as it finishes -
// so the line under the logo says what the wait is actually spent on.
//
// The logo is a picture file, looked for beside the executable (and in the source tree while
// developing) as "loopwire-logo.png". With no file there, the wordmark is drawn instead, so the
// splash never depends on an asset being present.
class SplashScreen : public QWidget {
    Q_OBJECT
public:
    explicit SplashScreen(const QStringList& modules, QWidget* parent = nullptr);

    // Name the step that is running now and repaint at once - the caller is about to block.
    void step(const QString& module);
    // Close, having stayed up long enough to be read.
    void finish();

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    QStringList modules_;
    QString current_;
    int done_ = 0;
    QPixmap logo_;
    QElapsedTimer shown_;
};

} // namespace Loopwire
