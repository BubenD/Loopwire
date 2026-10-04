#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

#include <vector>

namespace Edraw_Desktop::gif {

// ONE PICTURE OF AN ANIMATION, and how long it stays up, in hundredths of a second.
struct Frame {
    QImage image;
    int delayCs = 10;
};

// AN ANIMATED GIF, looping, from pictures all the same size. Qt reads GIFs but does not write them, and an
// animation is what shows how a thing is done with the mouse better than any number of words.
//
// One palette for the whole of it, made from the colours the pictures use most - a screen recording has few - and
// each picture after the first written as only what changed since the one before, so a cursor moving over a
// still view costs a few hundred bytes rather than the whole view again. Pictures that change nothing are not
// written twice: the one before stays up the longer.
QByteArray encode(const std::vector<Frame>& frames);
bool write(const QString& path, const std::vector<Frame>& frames, QString* error = nullptr);

} // namespace Edraw_Desktop::gif
