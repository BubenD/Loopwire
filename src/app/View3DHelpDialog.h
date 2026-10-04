#pragma once

#include <QDialog>

class QLabel;
class QListWidget;
class QMovie;
class QToolButton;

namespace Edraw_Desktop {

// HOW TO WORK IN THE 3D VIEW, SHOWN: each topic a short recording of the view itself being used - the mouse
// moving, the buttons pressed, what comes of it - and beside it the same steps written out. Recorded from the
// program by tools/help3d_gifs.cpp, so what is shown is what the view does.
class View3DHelpDialog : public QDialog {
    Q_OBJECT
public:
    explicit View3DHelpDialog(QWidget* parent = nullptr);

    int topicCount() const;
    void showTopic(int index);
    int currentTopic() const;
    // The recording showing, for the tests.
    QMovie* movie() const { return movie_; }

    // The recordings, in the program's resources: one for each topic that has one.
    static QStringList recordings();

signals:
    // The example model asked for, to try the steps on.
    void exampleRequested();

private:
    QListWidget* topics_ = nullptr;
    QLabel* picture_ = nullptr;
    QLabel* steps_ = nullptr;
    QMovie* movie_ = nullptr;
    QToolButton* pause_ = nullptr;
    QToolButton* again_ = nullptr;
};

} // namespace Edraw_Desktop
