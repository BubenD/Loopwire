#pragma once

#include "model/Entity.h"
#include <QFont>
#include <QTextLayout>
#include <cmath>

namespace Loopwire {

// Text belongs to the opaque label, whose fit excludes any clear sleeve around it.
inline void layoutLabelCaption(Entity& e) {
    const QString source = e.meta.value(QStringLiteral("labelCaption")).toString();
    const double w = std::max(0.01, e.meta.value(QStringLiteral("labelAlong")).toDouble() * 0.9);
    const double h = std::max(0.01, e.meta.value(QStringLiteral("labelAcross")).toDouble() * 0.85);
    QFont font;
    font.setPixelSize(1000);
    auto linesAt = [&](double size, QStringList& lines) {
        double used = 0.0;
        for (const QString& para : source.split(QLatin1Char('\n'))) {
            QTextLayout layout(para.isEmpty() ? QStringLiteral(" ") : para, font);
            QTextOption option;
            option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
            layout.setTextOption(option);
            layout.beginLayout();
            for (;;) {
                QTextLine line = layout.createLine();
                if (!line.isValid()) break;
                line.setLineWidth(w * 1000.0 / size);
                lines << para.mid(line.textStart(), line.textLength());
                used += line.height() * size / 1000.0;
            }
            layout.endLayout();
        }
        return used;
    };
    // An explicit height is a target; reduce it only when the text cannot fit the label.
    const double requested = e.meta.value(QStringLiteral("labelTextHeight")).toDouble();
    double lo = 0.001, hi = requested > 0.0 ? requested : 2.5;
    for (int i = 0; i < 22; ++i) {
        QStringList lines;
        const double mid = (lo + hi) * 0.5;
        if (linesAt(mid, lines) <= h) lo = mid; else hi = mid;
    }
    QStringList lines;
    linesAt(lo, lines);
    const Vec2 mid{e.meta.value(QStringLiteral("labelMidX")).toDouble(),
                   e.meta.value(QStringLiteral("labelMidY")).toDouble()};
    Vec2 dir{e.meta.value(QStringLiteral("labelDirX")).toDouble(1.0),
             e.meta.value(QStringLiteral("labelDirY")).toDouble()};
    if (dir.lengthSq() < 1e-12) dir = {1, 0};
    dir = dir.normalized();
    if (dir.x < 0 || (std::abs(dir.x) < 1e-9 && dir.y < 0)) dir = dir * -1.0;
    e.type = EntityType::Text;
    e.a = mid - dir * (w * 0.5) + dir.perp() * (h * 0.5);
    e.rotation = std::atan2(dir.y, dir.x);
    e.meta.insert(QStringLiteral("text"), lines.join(QLatin1Char('\n')));
    e.meta.insert(QStringLiteral("size"), lo);
    e.meta.insert(QStringLiteral("height"), lo);
    e.meta.insert(QStringLiteral("boxWidth"), w);
    e.meta.insert(QStringLiteral("boxHeight"), h);
    e.meta.insert(QStringLiteral("justify"), QStringLiteral("MC"));
    e.meta.insert(QStringLiteral("labelCaptionEntity"), true);
}

} // namespace Loopwire
