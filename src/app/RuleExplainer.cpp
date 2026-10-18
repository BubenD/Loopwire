#include "app/RuleExplainer.h"

#include <QApplication>
#include <QCheckBox>
#include <QSettings>
#include <QEvent>
#include <QFontMetricsF>
#include <QHash>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QTimer>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>

#include <cmath>
#include <functional>

namespace Loopwire {

namespace {
constexpr int kWidth = 320;
constexpr int kSceneH = 150;
constexpr int kLoopMs = 3200;   // the fault for half of it, the fix for the other

const QColor kInk(215, 226, 238);
const QColor kPart(45, 69, 96);
const QColor kWire(201, 207, 214);
const QColor kBad(224, 90, 90);
const QColor kGood(76, 208, 122);
const QColor kSleeve(230, 180, 90);
const QColor kDim(143, 176, 214);

// What each hovered widget explains.
struct Said {
    QString rule, title, text;
    QPointer<QWidget> outside;   // the window it is shown outside of, if any
};
QHash<QObject*, Said>& saidOf() {
    static QHash<QObject*, Said> said;
    return said;
}

// THE PICTURE OF ONE RULE: drawn on a 320 x 150 sheet, the fault or the fix, `pulse` 0..1 for the marker's beat.
using Scene = std::function<void(QPainter&, bool fixed, double pulse)>;

void text(QPainter& p, const QPointF& at, const QString& s, const QColor& c, double px = 11.0, bool bold = false,
          Qt::Alignment align = Qt::AlignCenter) {
    QFont f = p.font();
    f.setPixelSize(static_cast<int>(px));
    f.setBold(bold);
    p.setFont(f);
    p.setPen(c);
    const QRectF r(at.x() - 150.0, at.y() - 10.0, 300.0, 20.0);
    p.drawText(align == Qt::AlignCenter ? r
               : align == Qt::AlignLeft ? QRectF(at.x(), at.y() - 10.0, 300.0, 20.0)
                                        : QRectF(at.x() - 300.0, at.y() - 10.0, 300.0, 20.0),
               static_cast<int>(align | Qt::AlignVCenter), s);
}
void part(QPainter& p, const QRectF& r, const QString& name, const QColor& edge = kInk) {
    p.setPen(QPen(edge, 1.6));
    p.setBrush(kPart);
    p.drawRoundedRect(r, 3, 3);
    text(p, r.center(), name, edge, 11, true);
}
void wire(QPainter& p, const QPointF& a, const QPointF& b, const QColor& c = kWire, double w = 1.6) {
    p.setPen(QPen(c, w, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(a, b);
}
void cable(QPainter& p, double x0, double x1, double y, double h, const QColor& c = kWire) {
    p.setPen(QPen(c, 1.6));
    p.setBrush(QColor(60, 72, 88));
    p.drawRect(QRectF(x0, y - h / 2.0, x1 - x0, h));
}
void bad(QPainter& p, const QPointF& c, double pulse, double r = 14.0) {
    QColor ring = kBad;
    ring.setAlphaF(0.35 + 0.65 * pulse);
    p.setPen(QPen(ring, 2.4));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(c, r + 4.0 * pulse, r + 4.0 * pulse);
}
void good(QPainter& p, const QPointF& c) {
    p.setPen(QPen(kGood, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.drawLine(c + QPointF(-7, 0), c + QPointF(-2, 5));
    p.drawLine(c + QPointF(-2, 5), c + QPointF(8, -6));
}
void circle(QPainter& p, const QPointF& c, double r, const QColor& edge, const QColor& fill = Qt::transparent) {
    p.setPen(QPen(edge, 1.8));
    p.setBrush(fill);
    p.drawEllipse(c, r, r);
}
void sleeve(QPainter& p, const QRectF& r, const QColor& c = kSleeve) {
    p.setPen(QPen(c, 1.8));
    QColor fill = c;
    fill.setAlpha(60);
    p.setBrush(fill);
    p.drawRect(r);
}
void dim(QPainter& p, double x0, double x1, double y, const QString& said, const QColor& c = kDim) {
    p.setPen(QPen(c, 1.2));
    p.drawLine(QPointF(x0, y), QPointF(x1, y));
    p.drawLine(QPointF(x0, y - 5), QPointF(x0, y + 5));
    p.drawLine(QPointF(x1, y - 5), QPointF(x1, y + 5));
    text(p, QPointF((x0 + x1) / 2.0, y - 10.0), said, c, 11, true);
}
// A connector's face with so many cavities, and how many are filled.
void face(QPainter& p, const QRectF& r, int cavities) {
    part(p, r, QString());
    for (int i = 0; i < cavities; ++i)
        circle(p, QPointF(r.left() + 12.0, r.top() + 12.0 + i * (r.height() - 24.0) / std::max(1, cavities - 1)), 4.0, kInk);
}

const QHash<QString, Scene>& scenes() {
    static const QHash<QString, Scene> all = [] {
        QHash<QString, Scene> s;
        // ---- the harness DRC: logical --------------------------------------------------------------------
        s.insert(QStringLiteral("drawn-open"), [](QPainter& p, bool fixed, double pulse) {
            part(p, {15, 45, 50, 40}, QStringLiteral("X1"));
            cable(p, 65, 225, 65, 12);
            for (double y : {57.0, 65.0, 73.0}) wire(p, {225, 65}, {250, y});
            const double drop = fixed ? 0.0 : 55.0;
            part(p, {250, 45 + drop, 50, 40}, QStringLiteral("X2"), fixed ? kInk : kBad);
            if (fixed) good(p, {275, 35});
            else bad(p, {275, 120}, pulse, 26);
        });
        s.insert(QStringLiteral("unlinked"), [](QPainter& p, bool fixed, double pulse) {
            part(p, {20, 40, 50, 40}, QStringLiteral("X1"));
            cable(p, 70, 230, 60, 12);
            part(p, {230, 40, 50, 40}, QStringLiteral("X2"));
            part(p, {125, 105, 50, 30}, QStringLiteral("X3"), fixed ? kInk : kBad);
            if (fixed) {
                wire(p, {150, 66}, {150, 105});
                good(p, {195, 120});
            } else {
                bad(p, {150, 120}, pulse, 26);
            }
        });
        s.insert(QStringLiteral("openside"), [](QPainter& p, bool fixed, double pulse) {
            part(p, {20, 50, 50, 40}, QStringLiteral("X1"));
            wire(p, {70, 70}, {130, 70});
            part(p, {130, 55, 40, 30}, QStringLiteral("SP1"));
            if (fixed) {
                wire(p, {170, 70}, {230, 70});
                part(p, {230, 50, 50, 40}, QStringLiteral("X2"));
                good(p, {200, 50});
            } else {
                bad(p, {180, 70}, pulse, 12);
            }
        });
        s.insert(QStringLiteral("nopart"), [](QPainter& p, bool fixed, double pulse) {
            part(p, {105, 30, 110, 60}, QStringLiteral("X1"));
            text(p, {160, 110}, fixed ? QStringLiteral("P/N  D38999/26WB35PN") : QStringLiteral("P/N  —"), fixed ? kGood : kBad, 12, true);
            if (fixed) good(p, {265, 110});
            else bad(p, {178, 110}, pulse, 12);
        });
        s.insert(QStringLiteral("notinlib"), [](QPainter& p, bool fixed, double pulse) {
            part(p, {105, 25, 110, 55}, QStringLiteral("X1"));
            text(p, {160, 98}, fixed ? QStringLiteral("P/N  D38999/26WB35PN") : QStringLiteral("P/N  D38999/26WX99"),
                 fixed ? kGood : kBad, 12, true);
            text(p, {160, 122}, fixed ? QStringLiteral("found in the parts library") : QStringLiteral("not in any parts library"),
                 fixed ? kGood : kBad, 11);
            if (fixed) good(p, {270, 98});
            else bad(p, {160, 98}, pulse, 16);
        });
        s.insert(QStringLiteral("nodesig"), [](QPainter& p, bool fixed, double pulse) {
            part(p, {60, 45, 70, 50}, QStringLiteral("X1"));
            part(p, {190, 45, 70, 50}, fixed ? QStringLiteral("X2") : QStringLiteral("?"), fixed ? kInk : kBad);
            if (fixed) good(p, {280, 45});
            else bad(p, {225, 70}, pulse, 30);
        });
        s.insert(QStringLiteral("dupdesig"), [](QPainter& p, bool fixed, double pulse) {
            part(p, {60, 45, 70, 50}, QStringLiteral("X1"), fixed ? kInk : kBad);
            part(p, {190, 45, 70, 50}, fixed ? QStringLiteral("X2") : QStringLiteral("X1"), fixed ? kInk : kBad);
            if (fixed) good(p, {280, 45});
            else {
                bad(p, {95, 70}, pulse, 30);
                bad(p, {225, 70}, pulse, 30);
            }
        });
        s.insert(QStringLiteral("openrun"), [](QPainter& p, bool fixed, double pulse) {
            part(p, {15, 50, 50, 40}, QStringLiteral("X1"));
            cable(p, 65, fixed ? 240 : 250, 70, 12);
            if (fixed) {
                part(p, {240, 50, 50, 40}, QStringLiteral("X2"));
                good(p, {265, 40});
            } else {
                bad(p, {252, 70}, pulse, 12);
            }
        });
        s.insert(QStringLiteral("nolength"), [](QPainter& p, bool fixed, double pulse) {
            part(p, {15, 60, 50, 40}, QStringLiteral("X1"));
            cable(p, 65, 255, 80, 12);
            part(p, {255, 60, 50, 40}, QStringLiteral("X2"));
            dim(p, 65, 255, 35, fixed ? QStringLiteral("150 mm") : QStringLiteral("0 mm"), fixed ? kGood : kBad);
            if (!fixed) bad(p, {160, 25}, pulse, 14);
        });
        s.insert(QStringLiteral("shortrun"), [](QPainter& p, bool fixed, double pulse) {
            const double x0 = 40, x1 = fixed ? 280 : 200;
            part(p, {x0 - 30, 60, 30, 40}, QString());
            part(p, {x1, 60, 30, 40}, QString());
            const double fan = 70;
            // The fans from each end, and the body between - none left where they overlap.
            for (double y : {70.0, 80.0, 90.0}) {
                wire(p, {x0, y}, {x0 + fan, 80});
                wire(p, {x1, y}, {x1 - fan, 80});
            }
            if (x1 - fan > x0 + fan) cable(p, x0 + fan, x1 - fan, 80, 10);
            dim(p, x0, x1, 35, fixed ? QStringLiteral("240 mm - room for both fans") : QStringLiteral("160 mm < 2 x 70 mm of fans"),
                fixed ? kGood : kBad);
            if (!fixed) bad(p, {(x0 + x1) / 2.0, 80}, pulse, 18);
        });
        s.insert(QStringLiteral("wirecount"), [](QPainter& p, bool fixed, double pulse) {
            cable(p, 15, 120, 75, 22);
            text(p, {68, 45}, QStringLiteral("cable: 4 wires"), kDim, 11, true);
            const int given = fixed ? 4 : 6;
            for (int i = 0; i < given; ++i) {
                const double y = 50 + i * 10;
                wire(p, {120, 75}, {230, y}, i >= 4 ? kBad : kWire);
            }
            part(p, {230, 40, 60, 80}, QStringLiteral("X1"));
            text(p, {175, 130}, fixed ? QStringLiteral("4 given of 4") : QStringLiteral("6 given of 4"), fixed ? kGood : kBad, 12, true);
            if (!fixed) bad(p, {205, 105}, pulse, 14);
        });
        s.insert(QStringLiteral("nolayerpn"), [](QPainter& p, bool fixed, double pulse) {
            const QPointF c(80, 75);
            circle(p, c, 50, kSleeve);
            circle(p, c, 40, kInk);
            circle(p, c, 32, kWire, QColor(60, 72, 88));
            for (int i = 0; i < 6; ++i) circle(p, c + QPointF(16 * std::cos(i * 1.047), 16 * std::sin(i * 1.047)), 7, kInk);
            text(p, {150, 40}, QStringLiteral("wires   M22759/16-22"), kInk, 11, false, Qt::AlignLeft);
            text(p, {150, 65}, QStringLiteral("cable   CB-4-22"), kInk, 11, false, Qt::AlignLeft);
            text(p, {150, 90}, fixed ? QStringLiteral("sleeve  DR-25-3/8") : QStringLiteral("sleeve  —  (no P/N)"), fixed ? kGood : kBad, 11,
                 true, Qt::AlignLeft);
            if (!fixed) bad(p, {130, 75}, pulse, 10);
        });
        s.insert(QStringLiteral("bridge"), [](QPainter& p, bool fixed, double pulse) {
            part(p, {10, 40, 45, 40}, QStringLiteral("X1"));
            cable(p, 55, 265, 60, 14);
            part(p, {265, 40, 45, 40}, QStringLiteral("X2"));
            text(p, {160, 38}, QStringLiteral("cable: 4 wires"), kDim, 11, true);
            wire(p, {120, 66}, {150, 115});
            wire(p, {150, 115}, {200, 115});
            part(p, {200, 105, 40, 20}, QStringLiteral("F1"));
            text(p, {100, 115}, fixed ? QStringLiteral("wire 3") : QStringLiteral("wire 7"), fixed ? kGood : kBad, 12, true);
            if (!fixed) bad(p, {100, 115}, pulse, 16);
        });
        s.insert(QStringLiteral("branch"), [](QPainter& p, bool fixed, double pulse) {
            part(p, {260, 50, 45, 40}, QStringLiteral("X1"));
            cable(p, 150, 260, 70, 14);
            const double gap = fixed ? 0 : 30;
            for (double y : {45.0, 95.0}) wire(p, {150 - gap, 70}, {50, y});
            part(p, {10, 30, 40, 30}, QStringLiteral("X2"));
            part(p, {10, 80, 40, 30}, QStringLiteral("X3"));
            if (fixed) good(p, {150, 45});
            else bad(p, {135, 70}, pulse, 16);
        });
        s.insert(QStringLiteral("joint"), [](QPainter& p, bool fixed, double pulse) {
            part(p, {10, 80, 45, 40}, QStringLiteral("X1"));
            cable(p, 55, 265, 100, 12);
            part(p, {265, 80, 45, 40}, QStringLiteral("X2"));
            const QRectF j(140, 25, 34, 34);
            p.setPen(QPen(kSleeve, 1.8));
            p.setBrush(QColor(122, 85, 34));
            p.drawEllipse(j);
            text(p, j.center(), QStringLiteral("J1"), kInk, 11, true);
            if (fixed) {
                QPen tie(kSleeve, 1.6, Qt::DashLine);
                p.setPen(tie);
                p.drawLine(j.center() + QPointF(0, 17), {157, 94});
                good(p, {195, 40});
            } else {
                bad(p, j.center(), pulse, 22);
            }
        });
        s.insert(QStringLiteral("transpn"), [](QPainter& p, bool fixed, double pulse) {
            part(p, {10, 45, 50, 50}, QStringLiteral("X1"));
            for (double y : {60.0, 70.0, 80.0}) wire(p, {60, y}, {95, 70});
            sleeve(p, {95, 58, 60, 24});
            cable(p, 155, 300, 70, 16);
            text(p, {125, 110}, fixed ? QStringLiteral("DR-25-1/2") : QStringLiteral("P/N ?"), fixed ? kGood : kBad, 12, true);
            if (!fixed) bad(p, {125, 70}, pulse, 20);
        });
        s.insert(QStringLiteral("fanshrinkpn"), [](QPainter& p, bool fixed, double pulse) {
            cable(p, 10, 110, 70, 16);
            for (int i = 0; i < 4; ++i) {
                const double y = 40 + i * 20;
                wire(p, {110, 70}, {240, y});
                sleeve(p, {205, y - 4, 30, 8});
            }
            part(p, {240, 30, 50, 80}, QStringLiteral("X1"));
            text(p, {220, 130}, fixed ? QStringLiteral("RNF-100-1/16") : QStringLiteral("P/N ?"), fixed ? kGood : kBad, 12, true);
            if (!fixed) bad(p, {220, 70}, pulse, 22);
        });
        s.insert(QStringLiteral("notonbom"), [](QPainter& p, bool fixed, double pulse) {
            part(p, {20, 45, 80, 50}, QStringLiteral("X1"), fixed ? kInk : kBad);
            p.setPen(QPen(kDim, 1.2));
            p.setBrush(Qt::NoBrush);
            const QRectF bom(150, 25, 150, fixed ? 100 : 80);
            p.drawRect(bom);
            text(p, {225, 37}, QStringLiteral("BOM"), kDim, 11, true);
            text(p, {225, 57}, QStringLiteral("1   CB-4-22"), kInk, 11);
            text(p, {225, 77}, QStringLiteral("2   DR-25-1/2"), kInk, 11);
            if (fixed) {
                text(p, {225, 97}, QStringLiteral("3   D38999/26WB35PN"), kGood, 11, true);
                good(p, {120, 45});
            } else {
                bad(p, {60, 70}, pulse, 30);
            }
        });
        // ---- the harness DRC: compatibility ----------------------------------------------------------------
        s.insert(QStringLiteral("compat-stack"), [](QPainter& p, bool fixed, double pulse) {
            const QPointF c(110, 75);
            circle(p, c, 34, kWire, QColor(60, 72, 88));
            circle(p, c, fixed ? 42 : 26, fixed ? kGood : kBad);
            text(p, {240, 55}, QStringLiteral("cable  Ø 13.0"), kInk, 12, false);
            text(p, {240, 80}, fixed ? QStringLiteral("sleeve opens to 16.0") : QStringLiteral("sleeve opens to 10.0"), fixed ? kGood : kBad, 12, true);
            if (!fixed) bad(p, c, pulse, 30);
        });
        s.insert(QStringLiteral("compat-entry"), [](QPainter& p, bool fixed, double pulse) {
            // A backshell from the side: its entry, and the run coming to it.
            part(p, {200, 25, 90, 35}, QString());
            part(p, {200, 90, 90, 35}, QString());
            text(p, {245, 75}, QStringLiteral("entry 6-10"), kDim, 10, true);
            const double h = fixed ? 20 : 40;
            cable(p, 20, fixed ? 230 : 196, 75, h, fixed ? kWire : kBad);
            text(p, {100, 40}, fixed ? QStringLiteral("bundle Ø 8") : QStringLiteral("bundle Ø 14"), fixed ? kGood : kBad, 12, true);
            if (!fixed) bad(p, {198, 75}, pulse, 20);
        });
        s.insert(QStringLiteral("compat-entrysize"), [](QPainter& p, bool fixed, double pulse) {
            for (double y : {62.0, 70.0, 78.0}) wire(p, {20, y}, {90, 70});
            const double sh = fixed ? 30 : 16;
            cable(p, 150, 300, 70, 22);
            sleeve(p, {90, 70 - sh / 2.0, 90, sh}, fixed ? kSleeve : kBad);
            text(p, {160, 30}, QStringLiteral("cable body Ø 9.5"), kInk, 11, false);
            text(p, {160, 118}, fixed ? QStringLiteral("transition entry 4.0 - 12.7") : QStringLiteral("transition entry 2.4 - 6.4"),
                 fixed ? kGood : kBad, 12, true);
            if (!fixed) bad(p, {165, 70}, pulse, 18);
        });
        s.insert(QStringLiteral("compat-gauge"), [](QPainter& p, bool fixed, double pulse) {
            wire(p, {20, 75}, {170, 75}, kWire, 5.0);
            sleeve(p, {170, 62, 80, 26}, kInk);
            text(p, {90, 50}, QStringLiteral("wire 16 AWG"), kInk, 12, true);
            text(p, {210, 112}, fixed ? QStringLiteral("ferrule 14-18 AWG") : QStringLiteral("ferrule 22-26 AWG"), fixed ? kGood : kBad, 12, true);
            if (!fixed) bad(p, {172, 75}, pulse, 16);
        });
        s.insert(QStringLiteral("compat-ways"), [](QPainter& p, bool fixed, double pulse) {
            const int cav = fixed ? 6 : 4;
            face(p, {210, 25, 60, 100}, cav);
            for (int i = 0; i < 6; ++i) {
                const double y = 37 + i * 15.2;
                const bool orphan = !fixed && i >= 4;
                wire(p, {30, 75}, {orphan ? 195 : 222.0, orphan ? y : 37 + i * (76.0 / std::max(1, cav - 1))}, orphan ? kBad : kWire);
            }
            text(p, {90, 130}, fixed ? QStringLiteral("6 wires, 6 cavities") : QStringLiteral("6 wires, 4 cavities"), fixed ? kGood : kBad, 12, true);
            if (!fixed) bad(p, {195, 105}, pulse, 16);
        });
        s.insert(QStringLiteral("compat-solid"), [](QPainter& p, bool fixed, double pulse) {
            sleeve(p, {120, 35, 120, 80}, kInk);
            text(p, {180, 128}, QStringLiteral("crimp contact"), kDim, 11, true);
            const QPointF c(180, 75);
            if (fixed) {
                for (int i = 0; i < 7; ++i)
                    circle(p, i == 0 ? c : c + QPointF(14 * std::cos(i * 1.047), 14 * std::sin(i * 1.047)), 6, kGood, kGood.darker(160));
                text(p, {60, 75}, QStringLiteral("stranded"), kGood, 12, true);
            } else {
                circle(p, c, 20, kBad, kBad.darker(180));
                text(p, {60, 75}, QStringLiteral("solid core"), kBad, 12, true);
                bad(p, c, pulse, 26);
            }
        });
        s.insert(QStringLiteral("compat-contacts"), [](QPainter& p, bool fixed, double pulse) {
            face(p, {30, 25, 60, 100}, 4);
            if (fixed)
                for (int i = 0; i < 4; ++i) wire(p, {42, 37 + i * 25.3}, {70, 37 + i * 25.3}, kGood, 3.0);
            p.setPen(QPen(kDim, 1.2));
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(140, 30, 160, fixed ? 80 : 60));
            text(p, {220, 42}, QStringLiteral("BOM"), kDim, 11, true);
            text(p, {220, 62}, QStringLiteral("1   D38999/26WB35PN"), kInk, 11);
            if (fixed) {
                text(p, {220, 82}, QStringLiteral("2   M39029/58-360  x4"), kGood, 11, true);
            } else {
                text(p, {220, 82}, QStringLiteral("contacts?"), kBad, 11, true);
                bad(p, {60, 75}, pulse, 34);
            }
        });
        s.insert(QStringLiteral("compat-joint"), [](QPainter& p, bool fixed, double pulse) {
            const QPointF c(110, 75);
            circle(p, c, fixed ? 26 : 38, fixed ? kWire : kBad, QColor(60, 72, 88));
            for (int i = 0; i < 3; ++i) circle(p, c + QPointF(10 * std::cos(i * 2.09), 10 * std::sin(i * 2.09)), 8, kSleeve);
            circle(p, c, 32, kInk);
            text(p, {235, 55}, QStringLiteral("sleeve takes 12.0"), kInk, 12);
            text(p, {235, 80}, fixed ? QStringLiteral("splice bundle 9.8") : QStringLiteral("splice bundle 14.2"), fixed ? kGood : kBad, 12, true);
            if (!fixed) bad(p, c, pulse, 34);
        });
        s.insert(QStringLiteral("compat-data"), [](QPainter& p, bool fixed, double pulse) {
            p.setPen(QPen(kDim, 1.2));
            p.setBrush(QColor(35, 45, 58));
            p.drawRect(QRectF(60, 25, 200, 100));
            text(p, {160, 40}, QStringLiteral("DR-25-1/2   heatshrink"), kInk, 12, true);
            text(p, {80, 68}, QStringLiteral("entry size"), kDim, 11, false, Qt::AlignLeft);
            text(p, {80, 92}, QStringLiteral("wall"), kDim, 11, false, Qt::AlignLeft);
            text(p, {170, 68}, fixed ? QStringLiteral("3.2 - 12.7 mm") : QStringLiteral("—"), fixed ? kGood : kBad, 11, true, Qt::AlignLeft);
            text(p, {170, 92}, QStringLiteral("0.50 mm"), kInk, 11, false, Qt::AlignLeft);
            if (!fixed) bad(p, {180, 68}, pulse, 12);
        });
        // ---- the schematic validation ----------------------------------------------------------------------
        auto pins = [](QPainter& p, const QStringList& names, const QList<int>& wired, int open, double pulse, bool fixed) {
            part(p, {40, 25, 70, 100}, QStringLiteral("J1"));
            for (int i = 0; i < names.size(); ++i) {
                const double y = 45 + i * 30;
                text(p, {95, y}, names[i], kInk, 11, true);
                if (wired.contains(i)) wire(p, {110, y}, {260, y});
                else if (i == open && fixed) {
                    p.setPen(QPen(kInk, 2.0));
                    p.drawLine(QPointF(118, y - 6), QPointF(130, y + 6));
                    p.drawLine(QPointF(118, y + 6), QPointF(130, y - 6));
                } else if (i == open) {
                    bad(p, {118, y}, pulse, 10);
                }
            }
        };
        s.insert(QStringLiteral("nc"), [pins](QPainter& p, bool fixed, double pulse) {
            pins(p, {QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")}, {0, 2}, 1, pulse, fixed);
            text(p, {200, 75}, fixed ? QStringLiteral("not connected: marked X") : QStringLiteral("pin 2: no wire, no X"), fixed ? kGood : kBad, 11, true);
        });
        s.insert(QStringLiteral("openwire"), [](QPainter& p, bool fixed, double pulse) {
            part(p, {30, 45, 60, 60}, QStringLiteral("J1"));
            wire(p, {90, 75}, {fixed ? 230.0 : 190.0, 75});
            if (fixed) part(p, {230, 45, 60, 60}, QStringLiteral("J2"));
            else bad(p, {190, 75}, pulse, 12);
        });
        s.insert(QStringLiteral("dup"), [pins](QPainter& p, bool fixed, double pulse) {
            pins(p, {QStringLiteral("1"), QStringLiteral("2"), fixed ? QStringLiteral("3") : QStringLiteral("2")}, {0, 1, 2}, -1, pulse, fixed);
            if (!fixed) bad(p, {95, 105}, pulse, 12);
        });
        s.insert(QStringLiteral("mixedpins"), [pins](QPainter& p, bool fixed, double pulse) {
            pins(p, {QStringLiteral("1"), QStringLiteral("2"), fixed ? QStringLiteral("3") : QStringLiteral("A")}, {0, 1, 2}, -1, pulse, fixed);
            if (!fixed) bad(p, {95, 105}, pulse, 12);
        });
        s.insert(QStringLiteral("norefdes"), [](QPainter& p, bool fixed, double pulse) {
            part(p, {110, 35, 100, 70}, fixed ? QStringLiteral("J1") : QStringLiteral("?"), fixed ? kInk : kBad);
            if (!fixed) bad(p, {160, 70}, pulse, 34);
            else good(p, {230, 40});
        });
        s.insert(QStringLiteral("nopn"), [](QPainter& p, bool fixed, double pulse) {
            part(p, {105, 30, 110, 60}, QStringLiteral("J1"));
            text(p, {160, 110}, fixed ? QStringLiteral("P/N  MS27467T15F35P") : QStringLiteral("P/N  —"), fixed ? kGood : kBad, 12, true);
            if (!fixed) bad(p, {178, 110}, pulse, 12);
        });
        s.insert(QStringLiteral("shieldpair"), [](QPainter& p, bool fixed, double pulse) {
            wire(p, {20, 60}, {300, 60});
            wire(p, {20, 90}, {300, 90});
            p.setPen(QPen(fixed ? kGood : kBad, 1.8));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(fixed ? QRectF(150, 45, 22, 60) : QRectF(150, 50, 22, 20));
            if (!fixed) bad(p, {161, 60}, pulse, 16);
        });
        s.insert(QStringLiteral("twistpair"), [](QPainter& p, bool fixed, double pulse) {
            wire(p, {20, 60}, {300, 60});
            wire(p, {20, 90}, {300, 90});
            p.setPen(QPen(fixed ? kGood : kBad, 2.0));
            if (fixed) {
                p.drawLine(QPointF(150, 52), QPointF(170, 98));
                p.drawLine(QPointF(170, 52), QPointF(150, 98));
            } else {
                p.drawLine(QPointF(150, 52), QPointF(170, 68));
                bad(p, {160, 60}, pulse, 16);
            }
        });
        s.insert(QStringLiteral("shielddrain"), [](QPainter& p, bool fixed, double pulse) {
            part(p, {230, 30, 60, 90}, QStringLiteral("J1"));
            wire(p, {20, 55}, {230, 55});
            wire(p, {20, 80}, {230, 80});
            p.setPen(QPen(kInk, 1.8));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(QRectF(110, 42, 22, 52));
            wire(p, {121, 94}, {fixed ? 230.0 : 160.0, fixed ? 105.0 : 115.0}, fixed ? kGood : kBad);
            if (!fixed) bad(p, {160, 115}, pulse, 12);
        });
        // THE SYSTEM DESIGN RULES: use the harness DRC's fault/fix animation with system-specific pictures.
        s.insert(QStringLiteral("DESIGNATOR"), [](QPainter& p, bool fixed, double pulse) {
            part(p, {30, 40, 100, 60}, QStringLiteral("U1"));
            part(p, {190, 40, 100, 60}, fixed ? QStringLiteral("U2") : QStringLiteral("U1"), fixed ? kInk : kBad);
            text(p, {160, 125}, fixed ? QStringLiteral("Unique unit / port names") : QStringLiteral("Duplicate designators"), fixed ? kGood : kBad);
            if (fixed) good(p, {240, 30}); else bad(p, {240, 70}, pulse, 35);
        });
        s.insert(QStringLiteral("PORT-CONNECTION"), [](QPainter& p, bool fixed, double pulse) {
            part(p, {15, 50, 75, 50}, QStringLiteral("U1"));
            circle(p, {90, 75}, 5, fixed ? kGood : kBad);
            part(p, {230, 30, 75, 40}, QStringLiteral("U2")); wire(p, {90, 75}, {230, 50});
            if (!fixed) { part(p, {230, 90, 75, 40}, QStringLiteral("U3")); wire(p, {90, 75}, {230, 110}, kBad); bad(p, {90, 75}, pulse); }
            else good(p, {110, 55});
            text(p, {155, 137}, fixed ? QStringLiteral("One cable per port side") : QStringLiteral("Two cables on one port side"), fixed ? kGood : kBad);
        });
        s.insert(QStringLiteral("PART-NUMBER"), s.value(QStringLiteral("nopart")));
        // A SLEEVE DRAWN ON THE SHEET checked against its parts: the pictures of the rules they are the drawn kin of.
        s.insert(QStringLiteral("sleeve-nopart"), s.value(QStringLiteral("nopart")));
        s.insert(QStringLiteral("sleeve-partkind"), s.value(QStringLiteral("notinlib")));
        s.insert(QStringLiteral("sleeve-endshrink"), s.value(QStringLiteral("fanshrinkpn")));
        s.insert(QStringLiteral("sleeve-unchecked"), s.value(QStringLiteral("compat-data")));
        s.insert(QStringLiteral("sleeve-size"), s.value(QStringLiteral("compat-entrysize")));
        s.insert(QStringLiteral("sleeve-notonbom"), s.value(QStringLiteral("notonbom")));
        s.insert(QStringLiteral("DRAWING-LINK"), [](QPainter& p, bool fixed, double pulse) {
            part(p, {20, 25, 100, 55}, QStringLiteral("W1"));
            text(p, {70, 98}, QStringLiteral("rev A called for"), kInk);
            part(p, {185, 25, 115, 55}, QStringLiteral("Drawing"));
            text(p, {240, 98}, fixed ? QStringLiteral("CBL-001 rev A") : QStringLiteral("CBL-001 rev B"), fixed ? kGood : kBad);
            wire(p, {120, 52}, {185, 52});
            if (fixed) good(p, {240, 125}); else bad(p, {240, 98}, pulse, 22);
        });
        s.insert(QStringLiteral("NOTE-LINK"), [](QPainter& p, bool fixed, double pulse) {
            part(p, {20, 80, 125, 45}, QStringLiteral("Note"));
            wire(p, {145, 102}, {230, 55}, fixed ? kWire : kBad);
            if (fixed) { part(p, {230, 30, 75, 50}, QStringLiteral("U1")); good(p, {260, 100}); }
            else { circle(p, {230, 55}, 4, kBad); bad(p, {230, 55}, pulse, 22); text(p, {240, 100}, QStringLiteral("Removed unit"), kBad); }
        });
        s.insert(QStringLiteral("SCHEMATIC-NAME"), [](QPainter& p, bool fixed, double pulse) {
            part(p, {15, 45, 55, 50}, QStringLiteral("U1")); part(p, {250, 45, 55, 50}, QStringLiteral("U2"));
            wire(p, {70, 70}, {160, 70}); wire(p, {160, 70}, {250, 70}, fixed ? kGood : kBad);
            text(p, {115, 50}, QStringLiteral("SUPPLY"), kInk, 10);
            text(p, {205, 50}, fixed ? QStringLiteral("SUPPLY") : QStringLiteral("RETURN"), fixed ? kGood : kBad, 10);
            if (fixed) good(p, {160, 108}); else bad(p, {160, 70}, pulse, 18);
            text(p, {160, 135}, QStringLiteral("Same contact - same line name"), fixed ? kGood : kBad, 10);
        });
        s.insert(QStringLiteral("drawing-fields"),[](QPainter& p,bool fixed,double pulse){
            part(p,{20,35,125,70},QStringLiteral("Title block"));part(p,{175,35,125,70},QStringLiteral("Drawing field"));
            text(p,{82,125},QStringLiteral("CBL-001 / A"),kInk,11);
            text(p,{238,125},fixed?QStringLiteral("CBL-001 / A"):QStringLiteral("CBL-002 / B"),fixed?kGood:kBad,11);
            if(fixed)good(p,{235,20});else bad(p,{238,125},pulse,18);
        });
        s.insert(QStringLiteral("DRAWING-FIELDS"),s.value(QStringLiteral("drawing-fields")));
        s.insert(QStringLiteral("ROUTING"),[](QPainter& p,bool fixed,double pulse){
            part(p,{15,60,50,40},QStringLiteral("U1"));part(p,{125,50,70,60},QStringLiteral("Unit"));part(p,{255,60,50,40},QStringLiteral("U2"));
            if(fixed){wire(p,{65,80},{85,80});wire(p,{85,80},{85,25});wire(p,{85,25},{235,25});wire(p,{235,25},{235,80});wire(p,{235,80},{255,80});good(p,{160,130});}
            else{wire(p,{65,80},{255,80},kBad);bad(p,{160,80},pulse,25);}
        });
        return s;
    }();
    return all;
}
} // namespace

RuleExplainer* RuleExplainer::instance() {
    static QPointer<RuleExplainer> one;
    if (!one) one = new RuleExplainer();
    return one;
}

bool RuleExplainer::hasScene(const QString& rule) { return scenes().contains(rule); }

void RuleExplainer::paintSeverity(QToolButton* button, int level) {
    const char* colour = level >= 2 ? "#e05a5a" : level == 1 ? "#e6b84a" : "#4cd07a";
    button->setStyleSheet(QStringLiteral("QToolButton{background:%1;border:1px solid #1a2028;border-radius:2px;}"
                                         "QToolButton:hover{border:1px solid #ffffff;}").arg(QLatin1String(colour)));
    button->setProperty("severity", level);
    button->setToolTip(level >= 2 ? QStringLiteral("Error - click for warning")
                       : level == 1 ? QStringLiteral("Warning - click for not checked")
                                    : QStringLiteral("Not checked - click for error"));
    button->setAccessibleName(button->toolTip());
}

void RuleExplainer::addSeverityLegend(QVBoxLayout* layout, QWidget* parent) {
    auto* legend = new QWidget(parent); legend->setObjectName(QStringLiteral("drcTrafficLegend"));
    auto* row = new QHBoxLayout(legend); row->setContentsMargins(0, 0, 0, 0);
    for (int level : {2, 1, 0}) {
        auto* square = new QToolButton(legend); square->setFixedSize(14, 14); square->setEnabled(false);
        paintSeverity(square, level); row->addWidget(square);
        row->addWidget(new QLabel(level == 2 ? QStringLiteral("Error") : level == 1 ? QStringLiteral("Warning") : QStringLiteral("Not checked"), legend));
        row->addSpacing(8);
    }
    row->addStretch(1); layout->addWidget(legend);
}

RuleExplainer::RuleExplainer() : QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint) {
    setObjectName(QStringLiteral("ruleExplainer"));
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFixedWidth(kWidth);
    timer_ = new QTimer(this);
    timer_->setInterval(40);
    connect(timer_, &QTimer::timeout, this, [this] {
        t_ = std::fmod(t_ + 40.0 / kLoopMs, 1.0);
        update();
    });
}

void RuleExplainer::attach(QWidget* w, const QString& rule, const QString& title, const QString& text) {
    attachOutside(w, nullptr, rule, title, text);
}

void RuleExplainer::attachOutside(QWidget* w, QWidget* window, const QString& rule, const QString& title, const QString& text) {
    if (!w) return;
    saidOf().insert(w, {rule, title, text, window});
    w->installEventFilter(instance());
    QObject::connect(w, &QObject::destroyed, instance(), [w] { saidOf().remove(w); });
}

bool RuleExplainer::helpShown() {
    return QSettings().value(QStringLiteral("drc/showAnimationHelp"), true).toBool();
}

void RuleExplainer::setHelpShown(bool shown) {
    QSettings().setValue(QStringLiteral("drc/showAnimationHelp"), shown);
    if (!shown && instance()->isVisible()) {
        instance()->hide();
        instance()->timer_->stop();
    }
}

QCheckBox* RuleExplainer::addHelpToggle(QVBoxLayout* layout, QWidget* parent) {
    auto* box = new QCheckBox(QStringLiteral("Show animation help"), parent);
    box->setObjectName(QStringLiteral("drcShowAnimationHelp"));
    box->setToolTip(QStringLiteral("Hovering a rule shows an animated picture of the fault it catches and its fix"));
    box->setChecked(helpShown());
    QObject::connect(box, &QCheckBox::toggled, box, [](bool on) { setHelpShown(on); });
    layout->addWidget(box);
    return box;
}

bool RuleExplainer::eventFilter(QObject* o, QEvent* e) {
    if (e->type() == QEvent::Enter && saidOf().contains(o)) {
        if (!helpShown()) return false;
        const Said& s = saidOf().value(o);
        showFor(qobject_cast<QWidget*>(o), s.rule, s.title, s.text, s.outside);
    } else if ((e->type() == QEvent::Leave || e->type() == QEvent::Hide) && saidOf().contains(o)) {
        hide();
        timer_->stop();
    }
    return false;
}

void RuleExplainer::showFor(QWidget* anchor, const QString& rule, const QString& title, const QString& text, QWidget* outside) {
    rule_ = rule;
    title_ = title;
    text_ = text;
    t_ = 0.0;
    // Tall enough for the picture and the words under it.
    QFont f = font();
    f.setPixelSize(12);
    const QRectF words = QFontMetricsF(f).boundingRect(QRectF(0, 0, kWidth - 20, 400), Qt::TextWordWrap, text_);
    setFixedHeight(kSceneH + 34 + static_cast<int>(std::ceil(words.height())) + 10);
    if (anchor && outside) {
        // OUTSIDE THE WINDOW: past its right edge, level with what is hovered - its left where the screen ends.
        const QRect frame = outside->window()->frameGeometry();
        QPoint at(frame.right() + 10, anchor->mapToGlobal(QPoint(0, 0)).y());
        if (const QScreen* sc = outside->screen()) {
            const QRect area = sc->availableGeometry();
            if (at.x() + width() > area.right()) at.setX(frame.left() - width() - 10);
            at.setY(std::clamp(at.y(), area.top(), std::max(area.top(), area.bottom() - height())));
        }
        move(at);
    } else if (anchor) {
        // Beside what is hovered, kept on its screen.
        QPoint at = anchor->mapToGlobal(QPoint(anchor->width() + 12, 0));
        if (const QScreen* sc = anchor->screen()) {
            const QRect area = sc->availableGeometry();
            if (at.x() + width() > area.right()) at.setX(anchor->mapToGlobal(QPoint(0, 0)).x() - width() - 12);
            at.setY(std::clamp(at.y(), area.top(), std::max(area.top(), area.bottom() - height())));
        }
        move(at);
    }
    show();
    raise();
    timer_->start();
    update();
}

void RuleExplainer::setPhase(double t) {
    t_ = std::clamp(t, 0.0, 0.999);
    update();
}

void RuleExplainer::drawScene(QPainter& p, const QRectF& area, bool fixed, double pulse) const {
    p.save();
    p.translate(area.topLeft());
    p.scale(area.width() / kWidth, area.height() / kSceneH);
    const auto it = scenes().constFind(rule_);
    if (it != scenes().cend()) {
        it.value()(p, fixed, pulse);
    } else {
        // A rule with no picture of its own: its name, faulted then passing.
        text(p, {160, 70}, title_, fixed ? kGood : kBad, 14, true);
        if (!fixed) bad(p, {160, 70}, pulse, 40);
    }
    p.restore();
}

void RuleExplainer::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.fillRect(rect(), QColor(24, 31, 40));
    p.setPen(QPen(QColor(90, 110, 135), 1.0));
    p.setBrush(Qt::NoBrush);
    p.drawRect(rect().adjusted(0, 0, -1, -1));
    // The fault for the first half of the loop, the fix for the second - and which it is, said in the corner.
    const bool fixed = t_ >= 0.5;
    const double half = fixed ? (t_ - 0.5) * 2.0 : t_ * 2.0;
    const double pulse = 0.5 + 0.5 * std::sin(half * 4.0 * M_PI);
    drawScene(p, QRectF(0, 0, kWidth, kSceneH), fixed, pulse);
    text(p, {kWidth - 10.0, 12}, fixed ? QStringLiteral("✔ correct") : QStringLiteral("✖ fault"), fixed ? kGood : kBad, 11, true,
         Qt::AlignRight);
    // Its name and what it means.
    QFont f = font();
    f.setPixelSize(13);
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(143, 176, 214));
    p.drawText(QRectF(10, kSceneH + 4, kWidth - 20, 24), Qt::AlignLeft | Qt::AlignVCenter, title_);
    f.setPixelSize(12);
    f.setBold(false);
    p.setFont(f);
    p.setPen(QColor(205, 212, 220));
    p.drawText(QRectF(10, kSceneH + 30, kWidth - 20, height() - kSceneH - 34), Qt::TextWordWrap, text_);
}

} // namespace Loopwire
