#include "io/Dxf.h"
#include <functional>
#include <QColor>
#include <QRegularExpression>

#include "geometry/Geometry.h"
#include "harness/Harness.h"  // a twisted bundle exports the lines it is actually drawn with
#include "model/Block.h"   // stampEntityGroup: a callout is one thing, not three
#include "model/Document.h"

#include <QFile>
#include <QHash>
#include <QJsonArray>   // a polyline's bulges travel with it as meta
#include <QSaveFile>
#include <QStringList>
#include <QTextStream>
#include <cstring>
#include <QStringDecoder>
#include <QUuid>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace Loopwire::dxf {

namespace {

QString newId() { return QUuid::createUuid().toString(QUuid::WithoutBraces).left(8); }

QString pair(int code, const QString& value) {
    return QStringLiteral("%1\n%2").arg(code).arg(value);
}
QString pair(int code, double value) {
    return QStringLiteral("%1\n%2").arg(code).arg(value, 0, 'g', 12);
}

// hex -> DXF ACI color index (matches Loopwire dxfColor()).
int colorToIndex(const QString& hex, int fallback = 7) {
    static const QHash<QString, int> map = {
        {"#ff0000", 1}, {"#ffff00", 2}, {"#00ff00", 3}, {"#00ffff", 4},
        {"#0000ff", 5}, {"#ff00ff", 6}, {"#ffffff", 7}, {"#000000", 250},
        {"#00f0ff", 4},
        {"#f5c66d", 2}, {"#8bdc65", 3}, {"#b28cff", 6}, {"#ff6b6b", 1}};
    const QString k = hex.toLower();
    return map.value(k, (fallback % 7) + 1);
}

// DXF ACI color index -> hex (matches Loopwire colorIndexToHex()).
QString indexToColor(int index, int fallback) {
    static const QHash<int, QString> map = {
        {1, "#ff0000"}, {2, "#ffff00"}, {3, "#00ff00"}, {4, "#00ffff"},
        {5, "#0000ff"}, {6, "#ff00ff"}, {7, "#ffffff"}, {8, "#808080"},
        {9, "#c0c0c0"}, {250, "#000000"}};
    static const QString defaults[] = {"#ffffff", "#00f0ff", "#f5c66d"};
    const auto it = map.find(std::abs(index));
    if (it != map.end()) return it.value();
    return defaults[fallback % 3];
}

double radToDxf(double v) {
    double deg = std::fmod(v * 180.0 / M_PI, 360.0);
    if (deg < 0) deg += 360.0;
    return deg;
}

QString resolveEntityColor(const Entity& e, const Document& doc) {
    if (e.hasExplicitColor()) return e.color;
    if (const Layer* l = doc.findLayer(e.layer)) return l->color;
    return QStringLiteral("#ffffff");
}

// The reverse of this - a colour packed into group code 420 - is deliberately absent. Nothing writes
// 420 any more: see commonDxf. colorFromTrueColor below stays, because reading it is still required.
QString colorFromTrueColor(const QString& value) {
    bool ok = false;
    const int rgb = value.toInt(&ok);
    if (!ok || rgb < 0 || rgb > 0xFFFFFF) return QString();
    return QStringLiteral("#%1").arg(rgb, 6, 16, QLatin1Char('0'));
}

// WHAT EVERY ENTITY CARRIES: its layer and its colour, and NOTHING FROM A LATER DXF VERSION.
//
// This used to add group code 420, the 24-bit true colour, so an entity kept its exact RGB rather
// than the nearest of the 255 ACI indices. Code 420 was introduced in AutoCAD 2004 (AC1018). This
// file declares itself AC1009 - R12, from 1992 - in its header, and a reader that has been told R12
// and then meets a group code that did not exist for another twelve years is entitled to reject the
// file. AutoCAD does. Every tolerant reader - Loopwire's own importer, ezdxf, the Edo converter - skips
// the unknown code and says nothing, which is why the file looked fine everywhere except the one
// program the format exists to talk to.
//
// So the writer emits index colour only, and the drawing loses exact RGB on the way out. That is the
// right trade: 62 carries the colour to within an ACI index, and the alternative - writing a genuine
// R2000+ file - means handles on every entity, a BLOCK_RECORD table and an OBJECTS section, which is
// a different piece of work and not one to do by accident.
//
// The READER still understands 420, and must: files from AutoCAD 2004 onwards carry it, and Loopwire
// imports those. This is a constraint on what Loopwire writes, not on what it accepts.
QString commonDxf(const Entity& e, const Document& doc) {
    const QString color = resolveEntityColor(e, doc);
    return pair(8, e.layer.isEmpty() ? QStringLiteral("0") : e.layer) + "\n" +
           pair(62, QString::number(colorToIndex(color)));
}

QString dxfLine(const Entity& e, const Document& doc, const Vec2& a, const Vec2& b) {
    return pair(0, "LINE") + "\n" + commonDxf(e, doc) + "\n" + pair(10, a.x) + "\n" +
           pair(20, a.y) + "\n" + pair(30, 0.0) + "\n" + pair(11, b.x) + "\n" +
           pair(21, b.y) + "\n" + pair(31, 0.0);
}

QString dxfPolyline(const Entity& e, const Document& doc,
                    const std::vector<Vec2>& pts, bool closed) {
    QStringList out;
    out << pair(0, "POLYLINE") << commonDxf(e, doc) << pair(66, QString::number(1))
        << pair(10, 0.0) << pair(20, 0.0) << pair(30, 0.0)
        << pair(70, QString::number(closed ? 1 : 0));
    for (const auto& p : pts) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y)) continue;
        out << pair(0, "VERTEX") << commonDxf(e, doc) << pair(10, p.x) << pair(20, p.y)
            << pair(30, 0.0);
    }
    out << pair(0, "SEQEND") << commonDxf(e, doc);
    return out.join("\n");
}

QString dxfText(const Entity& e, const Document& doc, const Vec2& pos,
                const QString& text, double size) {
    return pair(0, "TEXT") + "\n" + commonDxf(e, doc) + "\n" + pair(10, pos.x) + "\n" +
           pair(20, pos.y) + "\n" + pair(30, 0.0) + "\n" +
           pair(40, size > 0 ? size : 3.0) + "\n" + pair(1, text);
}

std::vector<Vec2> ellipsePoints(const Entity& e, int steps = 48) {
    std::vector<Vec2> pts;
    const double c = std::cos(e.rotation), s = std::sin(e.rotation);
    for (int i = 0; i <= steps; ++i) {
        const double t = 2.0 * M_PI * i / steps;
        const double ex = e.radius * std::cos(t);
        const double ey = e.radiusY * std::sin(t);
        pts.push_back({e.center.x + ex * c - ey * s, e.center.y + ex * s + ey * c});
    }
    return pts;
}

} // namespace

QString toDxf(const Document& docIn) {
    // A BLOCK REFERENCE goes out as the lines it draws: R12 has blocks, but a reader of the file
    // wants the drawing, and a reference to a block file on this machine means nothing on another.
    const bool hasRefs = std::any_of(docIn.entities().begin(), docIn.entities().end(),
                                     [](const Entity& e) { return e.type == EntityType::BlockRef; });
    Document exploded;
    if (hasRefs) {
        exploded = docIn;
        std::vector<Entity> flat;
        for (const Entity& e : docIn.entities()) {
            if (e.type != EntityType::BlockRef) { flat.push_back(e); continue; }
            for (Entity& x : explodeBlockRef(e)) flat.push_back(std::move(x));
        }
        exploded.entities() = std::move(flat);
    }
    const Document& doc = hasRefs ? exploded : docIn;
    QStringList L;
    // ONLY VARIABLES THAT EXIST IN R12.
    //
    // $INSBASE does. $INSUNITS does NOT - it arrived with AutoCAD 2000 - and it was briefly added
    // here to record whether the drawing was in millimetres or inches. That is the same mistake as
    // group code 420 in commonDxf, in the header instead of the entities: a file that announces
    // itself as R12 and then uses something from eight years later. The unit is carried on the
    // dimension text instead, which is where a reader of a 1992 format will look for it.
    L << pair(0, "SECTION") << pair(2, "HEADER")
      << pair(9, "$ACADVER") << pair(1, "AC1009")
      << pair(9, "$INSBASE") << pair(10, 0.0) << pair(20, 0.0) << pair(30, 0.0)
      << pair(0, "ENDSEC");
    L << pair(0, "SECTION") << pair(2, "TABLES");

    // THE TABLES AutoCAD EXPECTS TO FIND, in the order R12 puts them.
    //
    // Loopwire used to write LTYPE and LAYER and stop there, which is enough for a reader that only
    // wants the geometry and not enough for AutoCAD. Three are added here because entities in this
    // file refer to them, directly or by omission:
    //
    //   VPORT   *ACTIVE is the drawing's own view. A DXF with no VPORT table has no model space
    //           configuration to open into.
    //   STYLE   every TEXT record here omits code 7, which means "the STANDARD style" - so STANDARD
    //           has to be a style that exists. A TEXT naming a style that was never defined is a
    //           dangling reference, and the entity it belongs to is the whole label.
    //   APPID   ACAD, the application id. Conventionally present in every R12 file.
    L << pair(0, "TABLE") << pair(2, "VPORT") << pair(70, QString::number(1));
    L << pair(0, "VPORT") << pair(2, "*ACTIVE") << pair(70, QString::number(0))
      << pair(10, 0.0) << pair(20, 0.0)          // lower-left of the viewport, in paper terms
      << pair(11, 1.0) << pair(21, 1.0)          // upper-right
      << pair(12, 0.0) << pair(22, 0.0)          // view centre
      << pair(13, 0.0) << pair(23, 0.0)          // snap base
      << pair(14, 10.0) << pair(24, 10.0)        // snap spacing
      << pair(15, 10.0) << pair(25, 10.0)        // grid spacing
      << pair(16, 0.0) << pair(26, 0.0) << pair(36, 1.0)   // view direction: straight down +Z
      << pair(17, 0.0) << pair(27, 0.0) << pair(37, 0.0)   // view target
      << pair(40, 297.0)                         // view height: an A4 sheet's worth
      << pair(41, 1.414) << pair(42, 50.0) << pair(43, 0.0) << pair(44, 0.0)
      << pair(50, 0.0) << pair(51, 0.0)
      << pair(71, QString::number(0)) << pair(72, QString::number(100))
      << pair(73, QString::number(1)) << pair(74, QString::number(3))
      << pair(75, QString::number(0)) << pair(76, QString::number(0))
      << pair(77, QString::number(0)) << pair(78, QString::number(0));
    L << pair(0, "ENDTAB");

    L << pair(0, "TABLE") << pair(2, "LTYPE") << pair(70, QString::number(1));
    L << pair(0, "LTYPE") << pair(2, "CONTINUOUS") << pair(70, QString::number(0))
      << pair(3, "Solid line") << pair(72, QString::number(65))
      << pair(73, QString::number(0)) << pair(40, 0.0);
    L << pair(0, "ENDTAB");
    L << pair(0, "TABLE") << pair(2, "LAYER")
      << pair(70, QString::number(doc.layers().size()));
    int li = 0;
    for (const auto& layer : doc.layers()) {
        const int ci = colorToIndex(layer.color, li);
        L << pair(0, "LAYER") << pair(2, layer.name)
          << pair(70, QString::number(layer.visible ? 0 : 1))
          << pair(62, QString::number(layer.visible ? ci : -ci))
          << pair(6, "CONTINUOUS");
        ++li;
    }
    L << pair(0, "ENDTAB");

    // STANDARD, the style every TEXT in this file leans on by not naming one.
    L << pair(0, "TABLE") << pair(2, "STYLE") << pair(70, QString::number(1));
    L << pair(0, "STYLE") << pair(2, "STANDARD") << pair(70, QString::number(0))
      << pair(40, 0.0)                    // height 0 = "ask each TEXT for its own", which they give
      << pair(41, 1.0)                    // width factor
      << pair(50, 0.0)                    // oblique angle
      << pair(71, QString::number(0))     // not backwards, not upside down
      << pair(42, 2.5)                    // last height used
      << pair(3, "txt") << pair(4, QString());
    L << pair(0, "ENDTAB");

    L << pair(0, "TABLE") << pair(2, "APPID") << pair(70, QString::number(1));
    L << pair(0, "APPID") << pair(2, "ACAD") << pair(70, QString::number(0));
    L << pair(0, "ENDTAB");

    L << pair(0, "ENDSEC");

    // AN EMPTY BLOCKS SECTION, WHICH IS NOT THE SAME AS NO BLOCKS SECTION.
    //
    // AutoCAD expects the section to be there in a DXF it is asked to open; it was absent entirely.
    // Empty is correct rather than merely sufficient: this writer never emits INSERT - blocks and
    // callouts are stamped down as their constituent entities on the way out - so there is genuinely
    // nothing for it to hold, and inventing *Model_Space and *Paper_Space definitions that no entity
    // refers to would be adding fiction to make the file look more conventional.
    L << pair(0, "SECTION") << pair(2, "BLOCKS") << pair(0, "ENDSEC");

    L << pair(0, "SECTION") << pair(2, "ENTITIES");

    for (const auto& e : doc.entities()) {
        switch (e.type) {
        case EntityType::Line:
        case EntityType::Dimension:
            L << dxfLine(e, doc, e.a, e.b);
            if (e.type == EntityType::Dimension) {
                const Vec2 mid = lerp(e.a, e.b, 0.5);
                const QString label = QStringLiteral("%1 %2")
                                          .arg(distance(e.a, e.b), 0, 'f', 2)
                                          .arg(doc.unit());
                L << dxfText(e, doc, mid, label, 3.5);
            }
            break;
        case EntityType::DoubleLine: {
            const double gap = e.meta.value("gap").toDouble(10.0);
            const Vec2 off = (e.b - e.a).normalized().perp() * (gap / 2.0);
            L << dxfLine(e, doc, e.a + off, e.b + off);
            L << dxfLine(e, doc, e.a - off, e.b - off);
            break;
        }
        case EntityType::Polygon:
            L << dxfPolyline(e, doc, e.points, true);
            break;
        case EntityType::Wire:
        case EntityType::Spline:
            L << dxfPolyline(e, doc, e.points, false);
            break;
        case EntityType::TwistedPair:
            // Export what is drawn: every wire's two edges, fan-out and weave included, and the line
            // across each wire where it is cut. The object itself is parametric (route + count +
            // spread), which DXF has no record type for.
            for (const auto& strand : twistedPairStrands(e))
                L << dxfPolyline(e, doc, strand, false);
            for (const auto& cap : twistedPairEndCaps(e))
                L << dxfPolyline(e, doc, cap, false);
            for (const auto& outline : twistedPairBody(e))
                L << dxfPolyline(e, doc, outline, false);
            break;
        case EntityType::Polyline: {
            const double cr = e.meta.value("cornerRadius").toDouble(0.0);
            if (e.meta.value("shrunkEnds").toBool()) {
                // Export what is drawn: a whole sleeve as its outline, and the cable/end-sleeve sort
                // as its two cable rails plus each sleeve outline and split face.
                for (const auto& run : shrunkSleeveRunsOf(e)) {
                    if (run.size() < 2) continue;
                    const bool closed = run.size() >= 4 &&
                                        distance(run.front(), run.back()) < 1e-7;
                    L << dxfPolyline(e, doc,
                                     closed ? std::vector<Vec2>(run.begin(), run.end() - 1) : run,
                                     closed);
                }
            } else if (e.meta.value("double").toBool()) {
                const double gap = e.meta.value("gap").toDouble(10.0);
                // Round centerline at cr+gap/2 so both runs fillet (inner radius = cr).
                const std::vector<Vec2> center =
                    cr > 0.0 ? roundedPolyline(e.points, cr + gap / 2.0) : e.points;
                L << dxfPolyline(e, doc, offsetPolyline(center, gap / 2.0), false);
                L << dxfPolyline(e, doc, offsetPolyline(center, -gap / 2.0), false);
            } else {
                L << dxfPolyline(e, doc, cr > 0.0 ? roundedPolyline(e.points, cr) : e.points, false);
            }
            break;
        }
        case EntityType::Rect:
        case EntityType::Hatch:
        case EntityType::Wipeout:
        // A picture exports as the rectangle it occupies: a DXF image is a reference to a file
        // beside the drawing, and an embedded one has nowhere to go. The frame at least keeps the
        // layout right rather than silently dropping part of the sheet.
        case EntityType::Image:
            L << dxfPolyline(e, doc, e.points, true);
            break;
        case EntityType::Circle:
            L << pair(0, "CIRCLE") << commonDxf(e, doc) << pair(10, e.center.x)
              << pair(20, e.center.y) << pair(40, e.radius);
            break;
        case EntityType::Arc: {
            double start = std::isfinite(e.startAngle) ? e.startAngle : 0.0;
            double end = std::isfinite(e.endAngle) ? e.endAngle : start;
            const double sweep = shortAngleDelta(start, end);
            if (sweep < 0) { const double s2 = start + sweep; end = start; start = s2; }
            else end = start + sweep;
            L << pair(0, "ARC") << commonDxf(e, doc) << pair(10, e.center.x)
              << pair(20, e.center.y) << pair(40, e.radius)
              << pair(50, radToDxf(start)) << pair(51, radToDxf(end));
            break;
        }
        case EntityType::Ellipse:
            L << dxfPolyline(e, doc, ellipsePoints(e), false);
            break;
        case EntityType::Text:
            L << dxfText(e, doc, e.a, e.meta.value("text").toString(),
                         e.meta.value("size").toDouble(3.0));
            break;
        default:
            break;
        }
    }
    L << pair(0, "ENDSEC");

    L << pair(0, "EOF");

    // LINE ENDINGS, NORMALISED IN ONE PLACE AT THE END. THIS IS NOT TIDYING.
    //
    // A DXF is a list of records, each of which is a code line and a value line. Every line in the
    // file has to end the same way. This used to produce a file where they did not: pair() joined its
    // code to its value with "\n", and the list of pairs was joined with "\r\n", so every record came
    // out as
    //
    //     0<LF>SECTION<CR><LF>
    //
    // - the code line ending LF and the value line ending CRLF, alternating, all the way down.
    // Tolerant readers normalise that and never mention it, which is why it survived: Loopwire's own
    // importer, ezdxf and the Edo converter all read those files without complaint. AutoCAD reads
    // lines strictly and would not open them at all.
    //
    // Doing it here rather than in pair() is deliberate. Ten helpers in this file build fragments
    // with "\n" in them, and fixing pair() alone would have left the ones that concatenate their own
    // records still mixing the two. Normalising the finished document is the only version of this fix
    // that a new helper cannot silently undo: whatever anything upstream emits, what leaves this
    // function has CRLF endings and nothing else.
    QString out = L.join(QLatin1String("\n"));
    out.replace(QLatin1String("\r\n"), QLatin1String("\n"));   // collapse anything already paired
    out.replace(QLatin1Char('\r'), QLatin1Char('\n'));         // and any lone CR
    out.replace(QLatin1Char('\n'), QLatin1String("\r\n"));
    return out + QLatin1String("\r\n");
}

namespace {

struct Pair { QString code; QString value; };

QString findVal(const std::vector<Pair>& pairs, const QString& code,
                const QString& fallback = QString()) {
    for (const auto& p : pairs)
        if (p.code == code) return p.value;
    return fallback;
}
double findNum(const std::vector<Pair>& pairs, const QString& code, double fallback = 0.0) {
    bool ok = false;
    const double v = findVal(pairs, code, QString::number(fallback)).toDouble(&ok);
    return ok ? v : fallback;
}

std::vector<Pair> collectGroup(const std::vector<Pair>& pairs, int start) {
    std::vector<Pair> out;
    for (int i = start; i < static_cast<int>(pairs.size()); ++i) {
        if (pairs[i].code == "0") break;
        out.push_back(pairs[i]);
    }
    return out;
}

// A tab is written for alignment - a note's indents, the columns of a tolerance table. The drawing
// carries real tab STOPS in drawing units, which this application's text does not; a stop every four
// characters keeps the columns lined up and is a great deal closer than the literal "^I" that a tab
// used to be shown as.
QString expandTabs(const QString& in) {
    if (!in.contains(QLatin1Char('\t'))) return in;
    constexpr int kStop = 4;
    QString out;
    int col = 0;
    for (const QChar c : in) {
        if (c == QLatin1Char('\n')) {
            out += c;
            col = 0;
        } else if (c == QLatin1Char('\t')) {
            const int pad = kStop - (col % kStop);
            out += QString(pad, QLatin1Char(' '));
            col += pad;
        } else {
            out += c;
            ++col;
        }
    }
    return out;
}

// ---- Text as AutoCAD writes it ---------------------------------------------------------------
//
// A TEXT or MTEXT string is not plain text. It carries the old %% escapes (%%d for a degree sign,
// %%c for a diameter, %%p for plus/minus), \U+ escapes for anything else, and - in MTEXT - inline
// formatting runs: a font is declared as \fArial|b0|i0|c0|p34;, a height as \H2.5x;, a colour as
// \C3;, a paragraph break as \P, and runs are grouped in braces. Read literally, a label reads
// "{\fArial|b0|i0|c0|p34;+0.5%%d}" instead of "+0.5°", which is what the whole drawing looked like.
// MTEXT longer than 250 characters is also SPLIT: the leading parts arrive as repeated group 3 and
// only the tail as group 1, so a note read from group 1 alone lost everything but its last line.
QString decodeDxfText(const QString& in, bool mtext) {
    QString s = in;
    // The %% escapes first: they are the same in TEXT and MTEXT.
    s.replace(QLatin1String("%%d"), QString(QChar(0x00B0)));   // degree
    s.replace(QLatin1String("%%c"), QString(QChar(0x2300)));   // diameter
    s.replace(QLatin1String("%%p"), QString(QChar(0x00B1)));   // plus/minus
    s.replace(QLatin1String("%%%"), QStringLiteral("%"));
    // %%nnn: a character by its code.
    {
        static const QRegularExpression re(QStringLiteral("%%(\\d{1,3})"));
        QRegularExpressionMatch m;
        while ((m = re.match(s)).hasMatch())
            s.replace(m.capturedStart(), m.capturedLength(),
                      QString(QChar(m.captured(1).toInt())));
    }
    // \U+XXXX: a character by its code point.
    {
        static const QRegularExpression re(QStringLiteral("\\\\U\\+([0-9A-Fa-f]{4})"));
        QRegularExpressionMatch m;
        while ((m = re.match(s)).hasMatch())
            s.replace(m.capturedStart(), m.capturedLength(),
                      QString(QChar(m.captured(1).toUShort(nullptr, 16))));
    }
    // The caret escapes: ^I is a TAB and ^J a line feed. Shown literally - which is what happened -
    // a note reads "1.^IElectrical properties" and a wire reads "BLUE^I^I28 AWG".
    s.replace(QLatin1String("^J"), QLatin1String("\n"));
    s.replace(QLatin1String("^I"), QLatin1String("\t"));
    if (!mtext) return expandTabs(s);

    // The inline runs. Everything here is formatting, not content: it is dropped, leaving the words.
    QString out;
    out.reserve(s.size());
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (c == QLatin1Char('\\') && i + 1 < s.size()) {
            const QChar k = s.at(i + 1);
            // A paragraph break is real content: it is where the line ends.
            if (k == QLatin1Char('P')) { out += QLatin1Char('\n'); ++i; continue; }
            if (k == QLatin1Char('~')) { out += QLatin1Char(' '); ++i; continue; }
            // An escaped brace or backslash stands for itself.
            if (k == QLatin1Char('\\') || k == QLatin1Char('{') || k == QLatin1Char('}')) {
                out += k;
                ++i;
                continue;
            }
            // A stacked run - \S over^under; - is written as over/under, which is what it means.
            if (k == QLatin1Char('S')) {
                int j = i + 2;
                QString run;
                while (j < s.size() && s.at(j) != QLatin1Char(';')) run += s.at(j++);
                run.replace(QLatin1Char('^'), QLatin1Char('/'));
                run.replace(QLatin1Char('#'), QLatin1Char('/'));
                out += run;
                i = j;
                continue;
            }
            // Everything else with a value runs to its semicolon: \f font, \H height, \C colour,
            // \W width, \Q slant, \T tracking, \A alignment, \p paragraph settings.
            if (QStringLiteral("fFHhCcWwQqTtAap").contains(k)) {
                int j = i + 2;
                while (j < s.size() && s.at(j) != QLatin1Char(';')) ++j;
                i = j;
                continue;
            }
            // The switches that carry no value: under/over/strike-through on and off.
            if (QStringLiteral("LlOoKkXN").contains(k)) { ++i; continue; }
            ++i;                                     // an escape this build does not know
            continue;
        }
        if (c == QLatin1Char('{') || c == QLatin1Char('}')) continue;   // run grouping
        out += c;
    }
    return expandTabs(out);
}

// Where a group 1 / group 3 text lives: MTEXT longer than 250 characters arrives as any number of
// group 3 continuations followed by the final group 1.
QString gatherText(const std::vector<Pair>& g) {
    QString s;
    for (const auto& p : g)
        if (p.code == QLatin1String("3")) s += p.value;
    for (const auto& p : g)
        if (p.code == QLatin1String("1")) s += p.value;
    return s;
}

// A block's placement: scale, turn, then move. Composed for nested inserts.
struct Xf {
    double sx = 1.0, sy = 1.0, rot = 0.0;   // rot in radians
    Vec2 t{0.0, 0.0};

    Vec2 apply(const Vec2& p) const {
        const double x = p.x * sx, y = p.y * sy;
        const double c = std::cos(rot), s = std::sin(rot);
        return {t.x + x * c - y * s, t.y + x * s + y * c};
    }
    // A length, scaled. A non-uniform insert cannot keep a circle a circle, so the mean is used -
    // the honest approximation for the one case (sx != sy) where no single radius is right.
    double len(double v) const { return v * (std::abs(sx) + std::abs(sy)) * 0.5; }
    double angle(double a) const {
        // A mirrored insert (one negative scale) reverses the sense of an angle.
        return (sx * sy < 0.0) ? -a + rot : a + rot;
    }
    // A polyline's bulge is a SIGNED measure of how far a segment arcs, and the sign says which way it
    // bows. Reflecting the shape bows every arc the other way, for the same reason it reverses an angle,
    // so a mirrored connector body came out inside-out unless the sign goes round with it.
    double bulge(double b) const { return (sx * sy < 0.0) ? -b : b; }
    Xf then(const Xf& parent) const {
        Xf out;
        out.sx = sx * parent.sx;
        out.sy = sy * parent.sy;
        // A MIRRORED parent turns the sense of what it holds round: reflecting and then turning is
        // turning the other way and then reflecting. Added as a plain sum, a mirrored placement put
        // everything inside it on the wrong side of the turn.
        out.rot = parent.rot + ((parent.sx * parent.sy < 0.0) ? -rot : rot);
        out.t = parent.apply(t);
        return out;
    }
};

} // namespace

// ---- The binary form of a DXF ------------------------------------------------------------------
//
// The same drawing and the same group codes, written as bytes: a sentinel, then record after record of
// [code][value], where the code is one byte (or 255 followed by two bytes) and what follows depends on
// which code it is - a NUL-terminated string, an eight-byte double, a two- or four-byte integer.
//
// Rather than a second parser for it, the bytes are turned back into the CODE / VALUE lines the text
// parser already reads. One parser understands DXF; this only understands how it was written down.

namespace {

const char kBinarySentinel[] = "AutoCAD Binary DXF\r\n\x1a\0";
constexpr int kBinarySentinelLen = 22;

// What sort of value follows a group code, from the DXF reference's own table.
enum class BinType { Str, Dbl, I16, I32, I64, I8, Chunk };

BinType binTypeOf(int code) {
    if (code >= 0 && code <= 9) return BinType::Str;
    if (code >= 10 && code <= 59) return BinType::Dbl;
    if (code >= 60 && code <= 79) return BinType::I16;
    if (code >= 90 && code <= 99) return BinType::I32;
    if (code == 100 || code == 101 || code == 102 || code == 105) return BinType::Str;
    if (code >= 110 && code <= 149) return BinType::Dbl;
    if (code >= 160 && code <= 169) return BinType::I64;
    if (code >= 170 && code <= 179) return BinType::I16;
    if (code >= 210 && code <= 239) return BinType::Dbl;
    if (code >= 270 && code <= 289) return BinType::I16;
    if (code >= 290 && code <= 299) return BinType::I8;
    if (code >= 300 && code <= 309) return BinType::Str;
    if (code >= 310 && code <= 319) return BinType::Chunk;
    if (code >= 320 && code <= 369) return BinType::Str;
    if (code >= 370 && code <= 389) return BinType::I16;
    if (code >= 390 && code <= 399) return BinType::Str;
    if (code >= 400 && code <= 409) return BinType::I16;
    if (code >= 410 && code <= 419) return BinType::Str;
    if (code >= 420 && code <= 429) return BinType::I32;
    if (code >= 430 && code <= 439) return BinType::Str;
    if (code >= 440 && code <= 449) return BinType::I32;
    if (code >= 450 && code <= 459) return BinType::I32;
    if (code >= 460 && code <= 469) return BinType::Dbl;
    if (code >= 470 && code <= 481) return BinType::Str;
    if (code == 999) return BinType::Str;
    if (code >= 1000 && code <= 1009) return BinType::Str;
    if (code >= 1010 && code <= 1059) return BinType::Dbl;
    if (code >= 1060 && code <= 1070) return BinType::I16;
    if (code == 1071) return BinType::I32;
    return BinType::Str;
}

// A DXF's own text is written in whatever code page the file says ($DWGCODEPAGE, usually ANSI_1252). Read
// as UTF-8 where that is what it is, and as Latin-1 otherwise - which is what ANSI_1252 mostly is, and
// never throws away a byte.
QString textFromBytes(const QByteArray& bytes) {
    auto toUtf8 = QStringDecoder(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    QString out = toUtf8(bytes);
    if (!toUtf8.hasError()) return out;
    return QString::fromLatin1(bytes);
}

} // namespace

bool looksBinary(const QByteArray& bytes) {
    return bytes.size() >= kBinarySentinelLen &&
           std::memcmp(bytes.constData(), kBinarySentinel, kBinarySentinelLen) == 0;
}

// The binary form turned back into CODE / VALUE lines. Empty (with `error` said) when the bytes run out in
// the middle of a record: a truncated file is a truncated file, and saying so beats drawing half of it.
static QString binaryDxfAsText(const QByteArray& bytes, QString* error) {
    QString out;
    out.reserve(bytes.size() * 2);
    qsizetype i = kBinarySentinelLen;
    const qsizetype n = bytes.size();
    auto need = [&](qsizetype count) { return i + count <= n; };
    // How wide a group code is. R13 and later write it as TWO bytes, R12 and earlier as one - and which it
    // is can be read off the first record, because a DXF always begins with code 0, "SECTION". Guessing
    // wrong is not a small error: every record after the first is then read at the wrong offset, and a
    // drawing of half a million records comes out as a handful of nonsense.
    const bool wideCodes = n >= kBinarySentinelLen + 9 &&
                           bytes.at(kBinarySentinelLen) == '\0' &&
                           bytes.at(kBinarySentinelLen + 1) == '\0' &&
                           bytes.mid(kBinarySentinelLen + 2, 7) == QByteArrayLiteral("SECTION");
    while (i < n) {
        int code = 0;
        if (wideCodes) {
            if (!need(2)) break;
            quint16 raw = 0;
            std::memcpy(&raw, bytes.constData() + i, 2);
            code = static_cast<int>(raw);
            i += 2;
        } else {
            code = static_cast<unsigned char>(bytes.at(i++));
        }
        QString value;
        switch (binTypeOf(code)) {
        case BinType::Str: {
            const qsizetype start = i;
            while (i < n && bytes.at(i) != '\0') ++i;
            value = textFromBytes(bytes.mid(start, i - start));
            if (i < n) ++i;   // the NUL
            break;
        }
        case BinType::Dbl: {
            if (!need(8)) { i = n; break; }
            double d = 0.0;
            std::memcpy(&d, bytes.constData() + i, 8);
            i += 8;
            // As many figures as a double has, so a coordinate comes back as the coordinate it was.
            value = QString::number(d, 'g', 17);
            break;
        }
        case BinType::I16: {
            if (!need(2)) { i = n; break; }
            qint16 v = 0;
            std::memcpy(&v, bytes.constData() + i, 2);
            i += 2;
            value = QString::number(v);
            break;
        }
        case BinType::I32: {
            if (!need(4)) { i = n; break; }
            qint32 v = 0;
            std::memcpy(&v, bytes.constData() + i, 4);
            i += 4;
            value = QString::number(v);
            break;
        }
        case BinType::I64: {
            if (!need(8)) { i = n; break; }
            qint64 v = 0;
            std::memcpy(&v, bytes.constData() + i, 8);
            i += 8;
            value = QString::number(v);
            break;
        }
        case BinType::I8: {
            if (!need(1)) { i = n; break; }
            value = QString::number(static_cast<int>(static_cast<unsigned char>(bytes.at(i))));
            i += 1;
            break;
        }
        case BinType::Chunk: {
            if (!need(1)) { i = n; break; }
            const int len = static_cast<unsigned char>(bytes.at(i++));
            if (!need(len)) { i = n; break; }
            value = QString::fromLatin1(bytes.mid(i, len).toHex().toUpper());
            i += len;
            break;
        }
        }
        out += QString::number(code);
        out += QLatin1Char('\n');
        out += value;
        out += QLatin1Char('\n');
        if (code == 0 && value == QLatin1String("EOF")) break;
    }
    if (out.isEmpty() && error)
        *error = QStringLiteral("This binary DXF has no readable records in it.");
    return out;
}

bool parseBytes(Document& doc, const QByteArray& bytes, QString* error) {
    if (!looksBinary(bytes)) return parse(doc, textFromBytes(bytes), error);
    const QString asText = binaryDxfAsText(bytes, error);
    if (asText.isEmpty()) return false;
    return parse(doc, asText, error);
}

bool parse(Document& doc, const QString& text, QString*) {
    QString clean = text;
    clean.replace("\r", "");
    const QStringList raw = clean.split('\n');
    std::vector<Pair> pairs;
    for (int i = 0; i + 1 < raw.size(); i += 2)
        pairs.push_back({raw[i].trimmed(), raw[i + 1].trimmed()});
    const int n = static_cast<int>(pairs.size());

    QHash<QString, Layer> layers;
    auto ensureLayer = [&](const QString& nameIn, int colorIndex) {
        const QString name = nameIn.isEmpty() ? QStringLiteral("0") : nameIn;
        if (!layers.contains(name)) {
            Layer l;
            l.name = name;
            l.color = indexToColor(colorIndex, layers.size());
            layers.insert(name, l);
        }
    };
    for (int i = 0; i < n; ++i) {
        if (pairs[i].code == "0" && pairs[i].value == "LAYER") {
            const auto g = collectGroup(pairs, i + 1);
            ensureLayer(findVal(g, "2", "0"), std::abs(findVal(g, "62", "0").toInt()));
        }
    }

    // ---- The file's sections ------------------------------------------------------------------
    //
    // Every entity used to be read wherever it appeared, which meant the BLOCKS section - the
    // DEFINITIONS of the symbols, drawn about their own origins - was dropped into the drawing as
    // though it were geometry, while the INSERTs that actually place those symbols were ignored. A
    // frame block therefore appeared in a corner of the sheet on its own, and everything really
    // placed by an insert (the braided cable, the logos, the connector symbols) was missing.
    int entFrom = -1, entTo = -1;
    struct BlockDef { int from = -1, to = -1; Vec2 base{0.0, 0.0}; };
    QHash<QString, BlockDef> blocks;
    for (int i = 0; i < n; ++i) {
        if (pairs[i].code != "0" || pairs[i].value != "SECTION") continue;
        const auto head = collectGroup(pairs, i + 1);
        const QString name = findVal(head, "2");
        int end = i + 1;
        while (end < n && !(pairs[end].code == "0" && pairs[end].value == "ENDSEC")) ++end;
        if (name == QLatin1String("ENTITIES")) {
            entFrom = i + 1;
            entTo = end;
        } else if (name == QLatin1String("BLOCKS")) {
            for (int j = i + 1; j < end; ++j) {
                if (pairs[j].code != "0" || pairs[j].value != "BLOCK") continue;
                const auto bh = collectGroup(pairs, j + 1);
                BlockDef def;
                def.base = {findNum(bh, "10"), findNum(bh, "20")};
                int k = j + 1;
                while (k < end && pairs[k].code != "0") ++k;      // past the header
                def.from = k;
                while (k < end && !(pairs[k].code == "0" && pairs[k].value == "ENDBLK")) ++k;
                def.to = k;
                blocks.insert(findVal(bh, "2"), def);
                j = k;
            }
        }
        i = end;
    }
    if (entFrom < 0) { entFrom = 0; entTo = n; }   // a file with no sections: read it all

    // The sheet: $LIMMIN/$LIMMAX is the drawing area the file was set up with (an A3, here), and
    // $EXTMIN/$EXTMAX is what was in it when it was last saved. The limits are preferred - they are
    // the sheet itself - and either is better than the extents of every object in the file, which on a
    // real drawing include working geometry left off the sheet.
    {
        auto headerPoint = [&](const QString& name, Vec2& out) {
            for (int i = 0; i + 3 < n; ++i) {
                if (pairs[i].code != "9" || pairs[i].value != name) continue;
                double x = 0.0, y = 0.0;
                bool haveX = false, haveY = false;
                for (int j = i + 1; j < n && j < i + 8; ++j) {
                    if (pairs[j].code == "10") { x = pairs[j].value.toDouble(); haveX = true; }
                    else if (pairs[j].code == "20") { y = pairs[j].value.toDouble(); haveY = true; }
                    else if (pairs[j].code == "9") break;
                }
                if (haveX && haveY) { out = {x, y}; return true; }
                return false;
            }
            return false;
        };
        Vec2 lo, hi;
        Bounds sheet;
        if (headerPoint(QStringLiteral("$LIMMIN"), lo) && headerPoint(QStringLiteral("$LIMMAX"), hi) &&
            hi.x - lo.x > 1e-6 && hi.y - lo.y > 1e-6) {
            sheet.expand(lo);
            sheet.expand(hi);
        }
        // THE LIMITS ONLY IF THE DRAWING IS ON THEM. A released sheet had limits of 12 by 9 - the sheet the
        // file was set up with years ago - and the drawing itself out at 13 to 42 by 20 to 47, nowhere near
        // them. Fitting the view to the limits then opened the file on an empty patch of paper: the drawing
        // was there, and the window was looking somewhere else, which reads exactly like a file that will
        // not open. So the limits are used when the drawing is inside them, and what was last saved
        // ($EXTMIN/$EXTMAX) otherwise.
        Bounds extents;
        Vec2 elo, ehi;
        if (headerPoint(QStringLiteral("$EXTMIN"), elo) && headerPoint(QStringLiteral("$EXTMAX"), ehi) &&
            ehi.x - elo.x > 1e-6 && ehi.y - elo.y > 1e-6) {
            extents.expand(elo);
            extents.expand(ehi);
        }
        const bool limitsHoldTheDrawing =
            sheet.valid && extents.valid && extents.min.x >= sheet.min.x - 1e-6 &&
            extents.min.y >= sheet.min.y - 1e-6 && extents.max.x <= sheet.max.x + 1e-6 &&
            extents.max.y <= sheet.max.y + 1e-6;
        if (sheet.valid && (!extents.valid || limitsHoldTheDrawing)) doc.sheet() = sheet;
        else if (extents.valid) doc.sheet() = extents;
    }

    // The order the drawing is drawn in. A file's objects are not necessarily drawn in the order they
    // are listed: AutoCAD keeps a sort key per object in a SORTENTSTABLE, and that - not the listing -
    // decides which of two overlapping objects is on top. In this drawing the mask over the cable label
    // is LISTED before the braid it hides and SORTED after it, so read in file order the braid was
    // painted over the mask and ran straight through the label. An object the table says nothing about
    // sorts by its own handle, which is what AutoCAD does with one.
    QHash<QString, quint64> drawKey;
    for (int i = 0; i < n; ++i) {
        if (pairs[i].code != "0" || pairs[i].value != QLatin1String("SORTENTSTABLE")) continue;
        QString ent;
        for (int j = i + 1; j < n && pairs[j].code != "0"; ++j) {
            if (pairs[j].code == "331") {
                ent = pairs[j].value.trimmed().toUpper();
            } else if (pairs[j].code == "5" && !ent.isEmpty()) {
                bool ok = false;
                const quint64 k = pairs[j].value.trimmed().toULongLong(&ok, 16);
                if (ok) drawKey.insert(ent, k);
                ent.clear();
            }
        }
    }

    std::vector<Entity> entities;
    // Where each of the drawing's own objects starts in the list above, and what it sorts by: the
    // objects a block places keep the order the block lists them in, which is the order they are drawn.
    struct Slice { quint64 key = 0; int start = 0; };
    std::vector<Slice> slices;
    // One reader, used for the drawing itself and for the inside of every block it places.
    // Arrowhead sizes. An arrowhead is not stored: a leader says which dimension style it is drawn to
    // and AutoCAD draws the head from that style's arrow size (DIMASZ, code 41, 2.5mm when the style is
    // silent), scaled by the style's overall scale. A callout drawn as a multileader takes its size from
    // its own style instead, which is named by handle. Read as nothing, every balloon's leader was a
    // bare line resting against the wire it picks out.
    // How an office writes its dimensions: the sizes are held by the STYLE, not by each dimension, and
    // they matter more than they look. The drawing's own defaults are millimetres on a sheet - text 5
    // units high - and a schematic drawn in millimetres-as-units is 29 units across altogether, so a
    // dimension taking the default would write a number a sixth of the width of the whole drawing.
    struct DimStyle {
        double arrow = 2.5;      // 41, DIMASZ
        double textH = 2.5;      // 140, DIMTXT
        double extOff = 0.625;   // 42, DIMEXO
    };
    QHash<QString, DimStyle> dimStyles;
    QHash<QString, double> mleaderArrow;
    // Where a drawing actually states them. A DIMSTYLE record only writes the variables that differ from
    // the drawing's own, so on a real file the style table can be three bare names and every size lives
    // in the HEADER - which is exactly what the sheet this was written for does: $DIMTXT and $DIMASZ of
    // 0.18, and a style table stating nothing at all.
    DimStyle headerDim;
    {
        auto headerNum = [&](const QString& name, double& out) {
            for (int i = 0; i + 2 < n; ++i) {
                if (pairs[i].code != "9" || pairs[i].value != name) continue;
                for (int j = i + 1; j < n && j < i + 6; ++j) {
                    if (pairs[j].code == "9") break;
                    if (pairs[j].code == "40" || pairs[j].code == "140" ||
                        pairs[j].code == "70" || pairs[j].code == "1") {
                        out = pairs[j].value.toDouble();
                        return true;
                    }
                }
                return false;
            }
            return false;
        };
        double scale = 1.0;
        headerNum(QStringLiteral("$DIMSCALE"), scale);
        if (!(scale > 1e-9)) scale = 1.0;
        headerNum(QStringLiteral("$DIMASZ"), headerDim.arrow);
        headerNum(QStringLiteral("$DIMTXT"), headerDim.textH);
        headerNum(QStringLiteral("$DIMEXO"), headerDim.extOff);
        headerDim.arrow *= scale;
        headerDim.textH *= scale;
        headerDim.extOff *= scale;
    }
    for (int i = 0; i < n; ++i) {
        if (pairs[i].code != "0") continue;
        if (pairs[i].value == QLatin1String("DIMSTYLE")) {
            const auto g = collectGroup(pairs, i + 1);
            // Everything a style states is at the style's own overall scale (40, DIMSCALE). A style only
            // writes what it CHANGES, so what it says nothing about keeps the drawing's own figure -
            // reading an absent code as a default of 2.5 put millimetre-on-paper sizes onto a drawing
            // whose units are millimetres, and wrote a number a sixth of the width of the sheet.
            const double raw = findNum(g, "40", 0.0);
            const double scale = raw > 1e-9 ? raw : 1.0;
            DimStyle st = headerDim;
            if (!findVal(g, "41").isEmpty()) st.arrow = findNum(g, "41") * scale;
            if (!findVal(g, "140").isEmpty()) st.textH = findNum(g, "140") * scale;
            if (!findVal(g, "42").isEmpty()) st.extOff = findNum(g, "42") * scale;
            dimStyles.insert(findVal(g, "2"), st);
        } else if (pairs[i].value == QLatin1String("MLEADERSTYLE")) {
            const auto g = collectGroup(pairs, i + 1);
            mleaderArrow.insert(findVal(g, "5").trimmed().toUpper(), findNum(g, "42", 2.5));
        }
    }
    // A DIMENSION as the measurement it is, rather than the pile of lines AutoCAD baked for it.
    //
    // Every dimension in a DXF carries BOTH: an anonymous block holding the geometry already drawn for
    // it, and the points that say what is being measured. Expanding the block was faithful to the marks
    // on the paper and lost the dimension itself - what arrived was a handful of lines, two filled
    // triangles and a piece of text, none of which knows it is a measurement, none of which re-measures
    // when the geometry moves, and every piece of which has to be selected and deleted separately.
    //
    // Read from the defining points instead and it comes in as a dimension: it can be picked up by its
    // joints, moved, restyled, and it says what it measures. Anything whose kind cannot be represented
    // falls back to expanding the block, so a file never loses marks it had.
    auto readDimension = [&dimStyles, &headerDim](Entity& e, const std::vector<Pair>& g,
                                                  const Xf& xf) -> bool {
        const int flags = findVal(g, "70", "0").toInt();
        const int kind = flags & 7;
        const Vec2 defPt = xf.apply({findNum(g, "10"), findNum(g, "20")});
        const Vec2 p1 = xf.apply({findNum(g, "13"), findNum(g, "23")});
        const Vec2 p2 = xf.apply({findNum(g, "14"), findNum(g, "24")});
        const Vec2 ctr = xf.apply({findNum(g, "15"), findNum(g, "25")});
        const Vec2 txtAt = xf.apply({findNum(g, "11"), findNum(g, "21")});

        switch (kind) {
        case 0: {
            // Rotated linear: measured along a stated direction. Ours measures along one axis or the
            // other, so a dimension taken on a slant is not one of ours and keeps its baked geometry.
            const double rot = findNum(g, "50", 0.0) * M_PI / 180.0;
            const double c = std::abs(std::cos(rot)), s = std::abs(std::sin(rot));
            if (std::min(c, s) > 1e-6) return false;
            e.meta.insert(QStringLiteral("dimType"), QStringLiteral("linear"));
            // Which way it was PLACED measuring, stated rather than guessed at later: the geometry it
            // measures can be dragged, and a dimension that works its own direction out afterwards
            // turns round and measures something else the moment that happens.
            e.meta.insert(QStringLiteral("dimAcross"), c >= s);
            e.a = p1;
            e.b = p2;
            e.points = {p1, p2, defPt};
            break;
        }
        case 1:   // aligned: measured between the two points themselves
            e.meta.insert(QStringLiteral("dimType"), QStringLiteral("aligned"));
            e.a = p1;
            e.b = p2;
            e.points = {p1, p2, defPt};
            break;
        case 4:   // radius: the centre, and a point out on the ring
            e.meta.insert(QStringLiteral("dimType"), QStringLiteral("radius"));
            e.center = ctr;
            e.radius = distance(ctr, defPt);
            e.b = txtAt;
            break;
        case 3:   // diameter: two points across the circle, so the centre lies between them
            e.meta.insert(QStringLiteral("dimType"), QStringLiteral("diameter"));
            e.center = lerp(ctr, defPt, 0.5);
            e.radius = distance(ctr, defPt) * 0.5;
            e.b = txtAt;
            break;
        case 5:   // angular by three points: the corner it is taken about, and the two rays
            e.meta.insert(QStringLiteral("dimType"), QStringLiteral("angular"));
            e.points = {ctr, p1, p2};
            break;
        case 6:   // ordinate: where the feature is, and where its leader ends
            e.meta.insert(QStringLiteral("dimType"), QStringLiteral("ordinate"));
            e.a = p1;
            e.b = defPt;
            break;
        default:
            // 2 is angular taken between two LINES, which needs the lines; anything else is unknown.
            return false;
        }
        e.type = EntityType::Dimension;

        // WHAT IT SAYS, when the drawing says it rather than leaving it to the measurement. This is not a
        // nicety: on the sheet it was written for, six of the nine dimensions read "TBD" and two more read
        // a round number quite unlike the distance actually on the paper. A dimension that measured for
        // itself and ignored this would quietly state something the drawing never said.
        const QString says = findVal(g, "1");
        if (!says.isEmpty() && says != QLatin1String("<>"))
            e.meta.insert(QStringLiteral("textOverride"), says);

        // Written at the size the DRAWING says, not at this program's own default, which is millimetres
        // on a sheet and swamps a drawing whose units are millimetres.
        //
        // Three places say it, each overriding the one before: the header, the named style, and the
        // dimension's own overrides. The last of those is how AutoCAD records "this one dimension is
        // different", and it is kept in extended data rather than in a group code of its own - 1070
        // names the setting by its variable number and the 1040 after it carries the value. On the sheet
        // this was written for, that is the ONLY place the real sizes appear: 41 (the arrow) at 0.15 and
        // 140 (the text) at 0.12, against a header saying 0.18 for both.
        DimStyle st = dimStyles.contains(findVal(g, "3")) ? dimStyles.value(findVal(g, "3"))
                                                        : headerDim;
        for (size_t k = 0; k + 1 < g.size(); ++k) {
            if (g[k].code != QLatin1String("1070") || g[k + 1].code != QLatin1String("1040")) continue;
            const QString which = g[k].value.trimmed();
            const double v = g[k + 1].value.toDouble();
            if (which == QLatin1String("41")) st.arrow = v;
            else if (which == QLatin1String("140")) st.textH = v;
            else if (which == QLatin1String("42")) st.extOff = v;
        }
        e.meta.insert(QStringLiteral("textHeight"), xf.len(st.textH));
        e.meta.insert(QStringLiteral("arrowSize"), xf.len(st.arrow));
        e.meta.insert(QStringLiteral("extOffset"), xf.len(st.extOff));
        return true;
    };

    // A TABLE as a table, rather than as the grid of lines and separate texts AutoCAD baked for it.
    //
    // Everything needed is in the record: how many rows and columns, the height of each row and the width
    // of each column, and then one cell record per cell carrying that cell's words. It was being thrown
    // away in favour of expanding the generated block, which drew the right marks and left no table -
    // nothing whose cells could be typed into, no rows to add, no columns to widen, just a hundred and
    // ninety-seven lines and two hundred and forty-three separate pieces of text.
    //
    // Read sequentially: the header codes come first and the cell records follow, and both use 90..94 for
    // their own purposes, so position is what tells them apart.
    auto readTable = [](Entity& e, const std::vector<Pair>& g, const Xf& xf) -> bool {
        const int rows = findVal(g, "91", "0").toInt();
        const int cols = findVal(g, "92", "0").toInt();
        if (rows < 1 || cols < 1 || rows * cols > 20000) return false;
        // Turned tables are not something this drawing's table can be, so one of those keeps the baked
        // grid rather than being laid out square and wrong.
        const Vec2 dir{findNum(g, "11", 1.0), findNum(g, "21", 0.0)};
        if (std::hypot(dir.x, dir.y) > 1e-12 && std::abs(dir.y) > 1e-6 * std::abs(dir.x)) return false;

        std::vector<double> rowH, colW;
        std::vector<QString> cells;
        std::vector<QString> aligns;
        std::vector<TableMerge> merges;
        double textH = 0.0;
        {
            size_t i = 0;
            // The header: row heights (141) and column widths (142), up to the first cell record.
            for (; i < g.size() && g[i].code != QLatin1String("171"); ++i) {
                if (g[i].code == QLatin1String("141")) rowH.push_back(g[i].value.toDouble());
                else if (g[i].code == QLatin1String("142")) colW.push_back(g[i].value.toDouble());
                else if (g[i].code == QLatin1String("140") && textH <= 0.0)
                    textH = g[i].value.toDouble();
            }
            // Then one record per cell, each led by 171. A cell's words are its code 1; a cell with none
            // is an empty cell, which is most of a title block.
            while (i < g.size()) {
                if (g[i].code != QLatin1String("171")) { ++i; continue; }
                ++i;
                // A cell says what is in it in one of two ways, and reading only the first left holes
                // all over an imported table. Most carry their words as TEXT (code 1). But a cell
                // holding a plain value - a quantity, a number in a balloon column - carries it as a
                // VALUE instead, in a little block of its own: 301 says CELL_VALUE, 302 is the value,
                // 304 closes it. Of the 204 cells in the bill of materials on the sheet this was
                // written for, 166 are text and 33 are values; the missing 33 were the quantities.
                QString said, valued;
                int alignAt = 0, colSpan = 1, rowSpan = 1;
                for (; i < g.size() && g[i].code != QLatin1String("171"); ++i) {
                    if (g[i].code == QLatin1String("1") && said.isEmpty()) said = g[i].value;
                    else if (g[i].code == QLatin1String("302") && valued.isEmpty())
                        valued = g[i].value;
                    else if (g[i].code == QLatin1String("170") && alignAt == 0)
                        alignAt = g[i].value.toInt();
                    else if (g[i].code == QLatin1String("175")) colSpan = g[i].value.toInt();
                    else if (g[i].code == QLatin1String("176")) rowSpan = g[i].value.toInt();
                }
                const int at = static_cast<int>(cells.size());
                cells.push_back(decodeDxfText(said.isEmpty() ? valued : said, true));
                // WHERE IN THE CELL the words sit. 1..9 run top-left, top-centre, top-right, middle-left,
                // ... bottom-right, which is the same order this drawing's own codes run in. Read as
                // nothing, every cell of an imported table came out top-left: a bill of materials whose
                // quantities and headings should be centred read as a ragged left-hand column.
                static const char* const kWhere[9] = {"TL", "TC", "TR", "ML", "MC",
                                                      "MR", "BL", "BC", "BR"};
                aligns.push_back(alignAt >= 1 && alignAt <= 9 ? QLatin1String(kWhere[alignAt - 1])
                                                             : QString());
                // A cell that SPANS several columns or rows is a merged block, and its title row is the
                // one everybody notices: "Bom List" is one cell six columns wide, centred over the lot.
                // Read as nothing, it came out in the first column with five empty cells beside it.
                if (colSpan > 1 || rowSpan > 1) {
                    TableMerge m;
                    m.row = cols > 0 ? at / cols : 0;
                    m.col = cols > 0 ? at % cols : 0;
                    m.colSpan = std::max(1, colSpan);
                    m.rowSpan = std::max(1, rowSpan);
                    merges.push_back(m);
                }
            }
        }
        if (static_cast<int>(rowH.size()) < rows || static_cast<int>(colW.size()) < cols) return false;
        rowH.resize(rows);
        colW.resize(cols);
        double W = 0.0, H = 0.0;
        for (double w : colW) W += w;
        for (double h : rowH) H += h;
        if (!(W > 1e-9) || !(H > 1e-9)) return false;

        // Where it sits: the insertion point is the TOP-LEFT, columns run right from it and rows down.
        const Vec2 at = xf.apply({findNum(g, "10"), findNum(g, "20")});
        const double w = xf.len(W), h = xf.len(H);
        const double minX = at.x, maxX = at.x + w, maxY = at.y, minY = at.y - h;
        e.type = EntityType::Rect;
        e.points = {{minX, minY}, {maxX, minY}, {maxX, maxY}, {minX, maxY}};
        e.a = {minX, maxY};

        QJsonArray colFrac, rowFrac, cellArr;
        for (double cw : colW) colFrac.append(cw / W);
        for (double rh : rowH) rowFrac.append(rh / H);
        cells.resize(static_cast<size_t>(rows) * cols);   // pad short, drop long
        aligns.resize(static_cast<size_t>(rows) * cols);
        for (const QString& s : cells) cellArr.append(s);
        QJsonArray alignArr;
        bool anyAlign = false;
        for (const QString& s : aligns) {
            alignArr.append(s);
            if (!s.isEmpty()) anyAlign = true;
        }
        if (anyAlign) e.meta.insert(QStringLiteral("cellAlign"), alignArr);
        setTableMerges(e, merges);
        e.meta.insert(QStringLiteral("table"), true);
        e.meta.insert(QStringLiteral("rows"), rows);
        e.meta.insert(QStringLiteral("cols"), cols);
        e.meta.insert(QStringLiteral("colFrac"), colFrac);
        e.meta.insert(QStringLiteral("rowFrac"), rowFrac);
        e.meta.insert(QStringLiteral("cells"), cellArr);
        // The size the words are written at: what the file says, or something that fits the row.
        const double fit = xf.len(H / std::max(1, rows)) * 0.55;
        e.meta.insert(QStringLiteral("textSize"), textH > 1e-9 ? xf.len(textH) : fit);
        return true;
    };

    // The filled triangle a leader points with: its tip AT the point picked out, lying back along the
    // leader. AutoCAD's proportions - as long as the arrow size, a third of that across.
    auto arrowHead = [](const Vec2& tip, const Vec2& from, double size) {
        std::vector<Vec2> tri;
        const double dx = tip.x - from.x, dy = tip.y - from.y;
        const double len = std::hypot(dx, dy);
        if (len < 1e-9 || size < 1e-9) return tri;
        const Vec2 dir{dx / len, dy / len};
        const Vec2 back{tip.x - dir.x * size, tip.y - dir.y * size};
        const double half = size / 6.0;
        tri.push_back(tip);
        tri.push_back({back.x - dir.y * half, back.y + dir.x * half});
        tri.push_back({back.x + dir.y * half, back.y - dir.x * half});
        return tri;
    };

    // The plane an object is drawn on. A 2D object states the normal of its plane (210/220/230, the
    // drawing's Z axis unless it says otherwise) and its own coordinates are measured in THAT plane's
    // axes. A normal pointing the other way is how AutoCAD records a mirrored object, and it turns x
    // round: the schematic's pin leads, mirrored to face the other way, are placed at x = -117 and were
    // drawn out in the field to the left of the sheet instead of against the connector. Only the objects
    // whose points are stated on that plane are turned - a LINE, an ELLIPSE, an MTEXT and a LEADER give
    // theirs in the drawing's own coordinates.
    auto onPlaneOf = [](const QString& type, const std::vector<Pair>& g, const Xf& parent) {
        static const QStringList kOnPlane = {"INSERT", "CIRCLE", "ARC",  "TEXT",
                                            "SOLID",  "SHAPE",  "POLYLINE", "LWPOLYLINE"};
        Xf flip;
        if (kOnPlane.contains(type) && findNum(g, "230", 1.0) < 0.0) flip.sx = -1.0;
        return flip.then(parent);
    };

    std::function<void(int, int, const Xf&, int)> readRange =
        [&](int from, int to, const Xf& parentXf, int depth) {
            for (int i = from; i < to; ++i) {
                if (pairs[i].code != "0") continue;
                const QString type = pairs[i].value;

                if (depth == 0) {
                    // The start of one of the drawing's own objects, and its place in the drawing order.
                    const QString h = findVal(collectGroup(pairs, i + 1), "5").trimmed().toUpper();
                    bool ok = false;
                    const quint64 own = h.toULongLong(&ok, 16);
                    slices.push_back({drawKey.value(h, ok ? own : 0), int(entities.size())});
                }

                if (type == "POLYLINE") {
                    // Legacy polyline: gather following VERTEX groups until SEQEND.
                    const auto head = collectGroup(pairs, i + 1);
                    const Xf xf = onPlaneOf(type, head, parentXf);
                    const QString layer = findVal(head, "8", "0");
                    ensureLayer(layer, 0);
                    Entity e;
                    e.id = newId();
                    e.type = EntityType::Polyline;
                    e.layer = layer;
                    const int eci = findVal(head, "62", "256").toInt();
                    const QString trueColorValue = colorFromTrueColor(findVal(head, "420"));
                    e.color = !trueColorValue.isEmpty()
                                  ? trueColorValue
                                  : ((eci != 256 && eci != 0)
                                         ? indexToColor(std::abs(eci), 0)
                                         : QString());
                    int j = i + 1;
                    std::vector<double> leaving;   // the bulge of the segment starting at each vertex
                    for (; j < to; ++j) {
                        if (pairs[j].code == "0" && pairs[j].value == "VERTEX") {
                            const auto v = collectGroup(pairs, j + 1);
                            e.points.push_back(xf.apply({findNum(v, "10"), findNum(v, "20")}));
                            leaving.push_back(xf.bulge(findNum(v, "42", 0.0)));
                        } else if (pairs[j].code == "0" && pairs[j].value == "SEQEND") {
                            break;
                        } else if (pairs[j].code == "0" && pairs[j].value != "VERTEX") {
                            break;
                        }
                    }
                    // A closed legacy polyline says so on its head (70 bit 1), the same as an LWPOLYLINE,
                    // and two vertices are a closed shape once one of the edges is an arc.
                    const bool anyArc = std::any_of(leaving.begin(), leaving.end(),
                                                    [](double b) { return std::abs(b) > 1e-9; });
                    if ((findVal(head, "70", "0").toInt() & 1) &&
                        (e.points.size() > 2 || (e.points.size() == 2 && anyArc)))
                        e.points.push_back(e.points.front());
                    if (anyArc) {
                        QJsonArray arr;   // shifted along by one, as above
                        for (size_t k = 0; k < e.points.size(); ++k)
                            arr.append(k == 0 || k - 1 >= leaving.size() ? 0.0 : leaving[k - 1]);
                        e.meta.insert("bulges", arr);
                    }
                    if (e.points.size() > 1) entities.push_back(std::move(e));
                    i = j;
                    continue;
                }

                static const QStringList kKnown = {
                    "LINE",   "LWPOLYLINE", "CIRCLE",  "ARC",    "TEXT",      "MTEXT",
                    "INSERT", "ELLIPSE",    "SOLID",   "LEADER", "DIMENSION", "ACAD_TABLE",
                    "MULTILEADER", "WIPEOUT", "SPLINE", "HATCH"};
                if (!kKnown.contains(type)) continue;

                const auto g = collectGroup(pairs, i + 1);
                const Xf xf = onPlaneOf(type, g, parentXf);
                const QString layer = findVal(g, "8", "0");
                ensureLayer(layer, 0);
                const int eci = findVal(g, "62", "256").toInt();
                const QString trueColorValue = colorFromTrueColor(findVal(g, "420"));
                const QString color = !trueColorValue.isEmpty()
                                          ? trueColorValue
                                          : ((eci != 256 && eci != 0)
                                                 ? indexToColor(std::abs(eci), 0)
                                                 : QString());
                Entity e;
                e.id = newId();
                e.layer = layer;
                e.color = color;

                if (type == "INSERT" || type == "DIMENSION" || type == "ACAD_TABLE") {
                    // A dimension is READ as a dimension when it can be, from the points that define
                    // what it measures, rather than expanded from the geometry baked for it. Tried
                    // first, and it falls through to the block below when the kind is not one of ours,
                    // so a file never loses marks it had.
                    if (type == "DIMENSION") {
                        Entity dim = e;
                        if (readDimension(dim, g, xf)) {
                            entities.push_back(std::move(dim));
                            continue;
                        }
                    }
                    // A table likewise: read as a table when it can be, from the rows, columns and cells
                    // the record states, and only expanded from its baked grid when it cannot.
                    if (type == "ACAD_TABLE") {
                        Entity tbl = e;
                        if (readTable(tbl, g, xf)) {
                            entities.push_back(std::move(tbl));
                            continue;
                        }
                    }
                    // All three are drawn by a BLOCK. An insert places one, with its own scale and
                    // turn; a dimension and a table each name the block that holds the geometry
                    // AutoCAD generated for them, which is already in the drawing's coordinates.
                    if (depth > 8) continue;                    // a block that places itself
                    const QString name = findVal(g, "2");
                    const auto it = blocks.constFind(name);
                    if (it == blocks.constEnd() || it->from < 0) continue;
                    if (type == "ACAD_TABLE") {
                        // A table is a block reference as well, but it states its turn as a
                        // DIRECTION VECTOR rather than an angle, and its 41 is a column width, not a
                        // scale. Expanded with the identity transform - as a dimension rightly is,
                        // since a dimension's block is already in the drawing's coordinates - both
                        // tables landed on top of each other at the origin instead of on the sheet.
                        Xf local;
                        local.t = {findNum(g, "10"), findNum(g, "20")};
                        const Vec2 dir{findNum(g, "11", 1.0), findNum(g, "21", 0.0)};
                        if (std::hypot(dir.x, dir.y) > 1e-12) local.rot = std::atan2(dir.y, dir.x);
                        Xf shift;
                        shift.t = {-it->base.x, -it->base.y};
                        readRange(it->from, it->to, shift.then(local).then(xf), depth + 1);
                        continue;
                    }
                    if (type == "INSERT") {
                        Xf local;
                        local.sx = findNum(g, "41", 1.0);
                        local.sy = findNum(g, "42", 1.0);
                        if (std::abs(local.sx) < 1e-12) local.sx = 1.0;
                        if (std::abs(local.sy) < 1e-12) local.sy = 1.0;
                        local.rot = findNum(g, "50") * M_PI / 180.0;
                        const Vec2 at{findNum(g, "10"), findNum(g, "20")};
                        // The block's own base point is its origin, so the definition is placed
                        // relative to it rather than to the file's origin.
                        Xf shift;
                        shift.t = {-it->base.x, -it->base.y};
                        // A row/column array of the same block: rare, and cheap to honour.
                        const int cols = std::max(1, findVal(g, "70", "1").toInt());
                        const int rows = std::max(1, findVal(g, "71", "1").toInt());
                        const double cs = findNum(g, "44"), rs = findNum(g, "45");
                        for (int c = 0; c < cols; ++c)
                            for (int r = 0; r < rows; ++r) {
                                Xf place = local;
                                place.t = {at.x + c * cs, at.y + r * rs};
                                readRange(it->from, it->to, shift.then(place).then(xf), depth + 1);
                            }
                    } else {
                        readRange(it->from, it->to, xf, depth + 1);
                    }
                    continue;
                }

                if (type == "MULTILEADER") {
                    // A multileader keeps its words AND the line that points with them inside one
                    // record: 304 carries the text (and the LEADER_LINE{ / } markers that bracket a
                    // leader's vertices), and the 10/20 pairs that follow a LEADER_LINE{ marker are
                    // that line's points. Read as nothing, a callout lost its word - a "LABEL" arrow
                    // pointing at a cable with no label on it.
                    QString label;
                    double height = 0.0;
                    bool inLine = false;
                    std::vector<Vec2> verts;
                    Vec2 textAt{0.0, 0.0};
                    bool haveText = false;
                    Vec2 cur{0.0, 0.0};
                    bool halfPoint = false;
                    for (const auto& gp : g) {
                        if (gp.code == "300" || gp.code == "301" || gp.code == "302" ||
                            gp.code == "303" || gp.code == "304" || gp.code == "305") {
                            const QString v = gp.value.trimmed();
                            if (v.endsWith(QLatin1Char('{'))) {
                                inLine = v.startsWith(QLatin1String("LEADER_LINE"));
                                continue;
                            }
                            if (v == QLatin1String("}")) { inLine = false; continue; }
                            if (gp.code == "304" && label.isEmpty()) label = v;
                            continue;
                        }
                        if (gp.code == "41" && height <= 0.0) height = gp.value.toDouble();
                        if (gp.code == "10") {
                            cur = {gp.value.toDouble(), 0.0};
                            halfPoint = true;
                        } else if (gp.code == "20" && halfPoint) {
                            cur.y = gp.value.toDouble();
                            halfPoint = false;
                            if (inLine) verts.push_back(xf.apply(cur));
                            else if (!haveText) { textAt = xf.apply(cur); haveText = true; }
                        }
                    }
                    // A leader is stored as the points it turns at; where it MEETS the words is the
                    // content's own base point, so a leader given as a single point is a line from
                    // there to the text and not nothing at all.
                    if (verts.size() == 1 && haveText) verts.push_back(textAt);
                    // A callout is ONE thing: the line, the head on it, and the words it carries. It used
                    // to arrive as three separate objects - a polyline, a filled triangle and a text - so
                    // moving a callout meant finding and dragging three items, and leaving one behind was
                    // easy. It is a leader, which is a single object that can be taken hold of by its tip.
                    if (verts.size() > 1) {
                        // A multileader names its style by handle - the LAST 340 in the record; the 340
                        // before it is the style its words are set in.
                        QString styleHandle;
                        for (const auto& gp : g)
                            if (gp.code == QLatin1String("340")) styleHandle = gp.value.trimmed().toUpper();
                        Entity lead = e;
                        lead.type = EntityType::Leader;
                        lead.points = verts;
                        lead.meta.insert(QStringLiteral("arrowSize"),
                                         xf.len(mleaderArrow.value(styleHandle, 2.5)));
                        if (!label.isEmpty()) {
                            lead.meta.insert(QStringLiteral("text"), decodeDxfText(label, true));
                            lead.meta.insert(QStringLiteral("size"),
                                             std::max(1e-9, xf.len(height > 0.0 ? height : 2.5)));
                        }
                        entities.push_back(std::move(lead));
                    } else if (!label.isEmpty() && haveText) {
                        // Words with no line to point with are just words.
                        Entity txt = e;
                        txt.type = EntityType::Text;
                        txt.a = textAt;
                        txt.meta.insert("text", decodeDxfText(label, true));
                        txt.meta.insert("size",
                                        std::max(1e-9, xf.len(height > 0.0 ? height : 2.5)));
                        txt.meta.insert("justify", QStringLiteral("ML"));
                        entities.push_back(std::move(txt));
                    }
                    continue;
                }

                if (type == "WIPEOUT") {
                    // A wipeout is a mask: a shape painted in the drawing's background so that what
                    // lies under it is hidden. The cable label's sleeve is one - in AutoCAD the braid
                    // stops at the sleeve and the label reads clean, and read as nothing the braid ran
                    // straight through the words.
                    //
                    // Its boundary is not given in the drawing's coordinates. 10/20 is where the mask
                    // sits, 11/21 and 12/22 are the vectors down one pixel of its width and height, and
                    // the 14/24 vertices are in IMAGE space: -0.5..+0.5 across, and counted DOWNWARDS.
                    const Vec2 ins = {findNum(g, "10"), findNum(g, "20")};
                    const Vec2 u = {findNum(g, "11", 1.0), findNum(g, "21", 0.0)};
                    const Vec2 v = {findNum(g, "12", 0.0), findNum(g, "22", 1.0)};
                    e.type = EntityType::Wipeout;
                    Vec2 cur{0.0, 0.0};
                    bool half = false;
                    for (const auto& gp : g) {
                        if (gp.code == "14") {
                            cur = {gp.value.toDouble(), 0.0};
                            half = true;
                        } else if (gp.code == "24" && half) {
                            cur.y = gp.value.toDouble();
                            half = false;
                            e.points.push_back(xf.apply({ins.x + u.x * (cur.x + 0.5) + v.x * (0.5 - cur.y),
                                                         ins.y + u.y * (cur.x + 0.5) + v.y * (0.5 - cur.y)}));
                        }
                    }
                    // The boundary closes on itself, and AutoCAD repeats its first vertex to say so.
                    if (e.points.size() > 3 && std::hypot(e.points.front().x - e.points.back().x,
                                                          e.points.front().y - e.points.back().y) < 1e-9)
                        e.points.pop_back();
                    if (e.points.size() < 3) continue;
                    // A mask drawn in a real drawing has no outline of its own - the sleeve round the
                    // label is a separate polyline, and framing the mask as well drew the rectangle
                    // twice. The frame is still drawn when the mask is selected, so it can be found.
                    e.meta.insert("noFrame", true);
                    entities.push_back(std::move(e));
                    continue;
                }

                if (type == "LINE") {
                    e.type = EntityType::Line;
                    e.a = xf.apply({findNum(g, "10"), findNum(g, "20")});
                    e.b = xf.apply({findNum(g, "11"), findNum(g, "21")});
                    entities.push_back(std::move(e));
                } else if (type == "LWPOLYLINE" || type == "LEADER") {
                    // A leader is a run of points with an arrow on the end: the run is the part
                    // that carries meaning here.
                    e.type = EntityType::Polyline;
                    Vec2 cur;
                    bool have = false;
                    // A vertex may say that the segment LEAVING it is an arc rather than a straight
                    // (group 42, the bulge). Read as points alone, every arc became the straight line
                    // between its ends: a round connector body drawn as four bulged vertices came out a
                    // DIAMOND, a half-round as a triangle, and a circle written as two 180-degree arcs as
                    // a single flat line. Kept per vertex here and re-indexed below.
                    std::vector<double> leaving;   // DXF: the bulge of the segment starting at this vertex
                    double curBulge = 0.0;
                    for (const auto& p : g) {
                        if (p.code == "10") {
                            if (have) {
                                e.points.push_back(xf.apply(cur));
                                leaving.push_back(xf.bulge(curBulge));
                            }
                            cur = {p.value.toDouble(), 0};
                            curBulge = 0.0;
                            have = true;
                        } else if (p.code == "20" && have) {
                            cur.y = p.value.toDouble();
                        } else if (p.code == "42" && have) {
                            curBulge = p.value.toDouble();
                        }
                    }
                    if (have) {
                        e.points.push_back(xf.apply(cur));
                        leaving.push_back(xf.bulge(curBulge));
                    }
                    // A CLOSED polyline (flag 1) is a loop, and its last edge - back to where it
                    // started - is drawn like any other. Ignoring the flag left every such shape open:
                    // a connector body missing the side the pins sit against, the sleeve round a cable
                    // label missing an end, the box round the notes missing its edge.
                    const bool anyArc = std::any_of(leaving.begin(), leaving.end(),
                                                    [](double b) { return std::abs(b) > 1e-9; });
                    // TWO vertices can be a closed shape, once arcs are in play: a circle is often
                    // written as two 180-degree arcs between a pair of opposite points, and that is how
                    // the round connector bodies on a real sheet are drawn. The closing point was only
                    // added above two vertices - right when the edges are straight, since closing a pair
                    // of points with a line just retraces it, and wrong here: it threw away the arc BACK
                    // to the start, leaving a full circle drawn as a half one.
                    if (type == "LWPOLYLINE" && (findVal(g, "70", "0").toInt() & 1) &&
                        (e.points.size() > 2 || (e.points.size() == 2 && anyArc)))
                        e.points.push_back(e.points.front());
                    // DXF hangs a bulge on the vertex its segment LEAVES; the drawing side indexes a
                    // bulge by the point its segment ARRIVES at. So the run is shifted along by one, and
                    // a closed shape's last bulge - the arc back to where it started - lands exactly on
                    // the duplicated first point appended above.
                    if (anyArc) {
                        QJsonArray arr;
                        for (size_t k = 0; k < e.points.size(); ++k)
                            arr.append(k == 0 || k - 1 >= leaving.size() ? 0.0 : leaving[k - 1]);
                        e.meta.insert("bulges", arr);
                    }
                    // A leader points with an arrowhead at its FIRST vertex - the point it picks out.
                    // Its size comes from the leader's dimension style, which the leader may override in
                    // its own extended data: 1070 names the setting (41 is the arrow size) and the 1040
                    // after it carries the value.
                    if (type == "LEADER" && e.points.size() > 1) {
                        double size = dimStyles.value(findVal(g, "3")).arrow;
                        for (size_t k = 0; k + 1 < g.size(); ++k)
                            if (g[k].code == QLatin1String("1070") &&
                                g[k].value.trimmed() == QLatin1String("41") &&
                                g[k + 1].code == QLatin1String("1040"))
                                size = g[k + 1].value.toDouble();
                        // ONE object: a leader. It used to be the line plus a separate filled triangle -
                        // two things to select, two to move, two to delete, and an arrow left behind
                        // after dragging its line away is exactly what gets released by mistake.
                        e.type = EntityType::Leader;
                        e.meta.insert(QStringLiteral("arrowSize"), xf.len(size));
                    }
                    if (e.points.size() > 1) entities.push_back(std::move(e));
                } else if (type == "HATCH") {
                    // A fill was dropped outright. On a released sheet that is a solid pad drawn hollow,
                    // a section left blank - and, as with the splines, nothing to say it had happened.
                    //
                    // A HATCH is the fiddliest record in a DXF: 10/20 mean a dozen different things
                    // depending where in it they appear - a polyline vertex, an arc's centre, an
                    // ellipse's axis, a spline's control point, a seed point - so this WALKS the codes in
                    // order rather than asking for them by name, which is what every other branch here
                    // can safely do.
                    const QString patName = findVal(g, "2", "SOLID");
                    const bool solid = findVal(g, "70", "0").toInt() != 0 ||
                                       patName.compare(QLatin1String("SOLID"),
                                                       Qt::CaseInsensitive) == 0;
                    std::vector<std::vector<Vec2>> loops;
                    for (size_t i = 0; i < g.size();) {
                        if (g[i].code != QLatin1String("92")) { ++i; continue; }
                        const int pathFlags = g[i].value.toInt();
                        ++i;
                        std::vector<Vec2> loop;
                        auto atLoopEnd = [&] {
                            return i >= g.size() || g[i].code == QLatin1String("92") ||
                                   g[i].code == QLatin1String("97");
                        };
                        if (pathFlags & 2) {
                            // A POLYLINE boundary: says whether it carries bulges (72), whether it is
                            // closed (73), how many vertices (93), and then the vertices themselves.
                            bool hasBulge = false, closed = false;
                            while (!atLoopEnd() && g[i].code != QLatin1String("10")) {
                                if (g[i].code == QLatin1String("72")) hasBulge = g[i].value.toInt() != 0;
                                else if (g[i].code == QLatin1String("73")) closed = g[i].value.toInt() != 0;
                                ++i;
                            }
                            std::vector<Vec2> verts;
                            std::vector<double> leaving;
                            Vec2 cur{0.0, 0.0};
                            bool have = false;
                            double b = 0.0;
                            while (!atLoopEnd()) {
                                if (g[i].code == QLatin1String("10")) {
                                    if (have) { verts.push_back(cur); leaving.push_back(b); }
                                    cur = {g[i].value.toDouble(), 0.0};
                                    b = 0.0;
                                    have = true;
                                } else if (g[i].code == QLatin1String("20") && have) {
                                    cur.y = g[i].value.toDouble();
                                } else if (g[i].code == QLatin1String("42") && have && hasBulge) {
                                    b = g[i].value.toDouble();
                                }
                                ++i;
                            }
                            if (have) { verts.push_back(cur); leaving.push_back(b); }
                            if (closed && verts.size() > 1) {
                                verts.push_back(verts.front());
                                leaving.push_back(0.0);
                            }
                            // The same shift by one the polylines need: a bulge belongs to the segment
                            // its vertex LEAVES, and expandBulgePolyline indexes by the point it reaches.
                            std::vector<double> bulges(verts.size(), 0.0);
                            for (size_t k = 1; k < verts.size() && k - 1 < leaving.size(); ++k)
                                bulges[k] = leaving[k - 1];
                            loop = expandBulgePolyline(verts, bulges);
                        } else {
                            // EDGE data: how many edges (93), then each one led by its kind (72).
                            while (!atLoopEnd() && g[i].code != QLatin1String("72")) ++i;
                            while (!atLoopEnd()) {
                                if (g[i].code != QLatin1String("72")) { ++i; continue; }
                                const int edge = g[i].value.toInt();
                                ++i;
                                // Gather this edge's numbers until the next edge or the end of the loop.
                                std::vector<Pair> ed;
                                while (!atLoopEnd() && g[i].code != QLatin1String("72")) {
                                    ed.push_back(g[i]);
                                    ++i;
                                }
                                auto num = [&ed](const char* c, double dflt = 0.0) {
                                    for (const auto& pr : ed)
                                        if (pr.code == QLatin1String(c)) return pr.value.toDouble();
                                    return dflt;
                                };
                                auto add = [&loop](const Vec2& q) {
                                    if (loop.empty() || distance(loop.back(), q) > 1e-12)
                                        loop.push_back(q);
                                };
                                if (edge == 1) {                       // a straight edge
                                    add({num("10"), num("20")});
                                    add({num("11"), num("21")});
                                } else if (edge == 2) {                // a circular arc
                                    const Vec2 c{num("10"), num("20")};
                                    const double r = num("40");
                                    double a0 = num("50") * M_PI / 180.0;
                                    double a1 = num("51") * M_PI / 180.0;
                                    const bool ccw = num("73", 1.0) != 0.0;
                                    double sweep = a1 - a0;
                                    if (ccw) { while (sweep <= 0) sweep += 2.0 * M_PI; }
                                    else { while (sweep >= 0) sweep -= 2.0 * M_PI; }
                                    const int steps = std::max(6, int(std::abs(sweep) / (M_PI / 24.0)));
                                    for (int s = 0; s <= steps; ++s) {
                                        const double a = a0 + sweep * (double(s) / steps);
                                        add({c.x + r * std::cos(a), c.y + r * std::sin(a)});
                                    }
                                } else if (edge == 3) {                // an elliptical arc
                                    const Vec2 c{num("10"), num("20")};
                                    const Vec2 maj{num("11"), num("21")};
                                    const double rx = std::hypot(maj.x, maj.y);
                                    const double ry = rx * num("40", 1.0);
                                    const double rot = std::atan2(maj.y, maj.x);
                                    double t0 = num("50") * M_PI / 180.0;
                                    double t1 = num("51") * M_PI / 180.0;
                                    double sweep = t1 - t0;
                                    while (sweep <= 0) sweep += 2.0 * M_PI;
                                    const int steps = std::max(8, int(sweep / (M_PI / 24.0)));
                                    for (int s = 0; s <= steps; ++s)
                                        add(ellipsePointAt(t0 + sweep * (double(s) / steps), c, rx, ry,
                                                           rot));
                                } else if (edge == 4) {                // a spline edge
                                    std::vector<Vec2> ctrl;
                                    std::vector<double> knots;
                                    Vec2 cp{0.0, 0.0};
                                    bool haveCp = false;
                                    for (const auto& pr : ed) {
                                        if (pr.code == QLatin1String("10")) {
                                            if (haveCp) ctrl.push_back(cp);
                                            cp = {pr.value.toDouble(), 0.0};
                                            haveCp = true;
                                        } else if (pr.code == QLatin1String("20") && haveCp) {
                                            cp.y = pr.value.toDouble();
                                        } else if (pr.code == QLatin1String("40")) {
                                            knots.push_back(pr.value.toDouble());
                                        }
                                    }
                                    if (haveCp) ctrl.push_back(cp);
                                    const int deg = int(num("94", 3.0));
                                    for (const Vec2& q : nurbsCurvePoints(ctrl, knots, deg)) add(q);
                                }
                            }
                        }
                        if (loop.size() >= 3) loops.push_back(std::move(loop));
                    }
                    if (loops.empty()) continue;
                    // Our fill is ONE boundary and has no holes, so the outside of the shape is what is
                    // filled: the loop enclosing the most ground. A ring imported this way comes out
                    // filled through the middle, which is worth knowing and still nearer the drawing
                    // than the nothing that used to be there.
                    size_t best = 0;
                    double bestArea = -1.0;
                    for (size_t k = 0; k < loops.size(); ++k) {
                        double a2 = 0.0;
                        const auto& L = loops[k];
                        for (size_t j = 0, m = L.size(); j < m; ++j) {
                            const Vec2& u = L[j];
                            const Vec2& v = L[(j + 1) % m];
                            a2 += u.x * v.y - v.x * u.y;
                        }
                        if (std::abs(a2) > bestArea) { bestArea = std::abs(a2); best = k; }
                    }
                    e.type = EntityType::Hatch;
                    for (const Vec2& q : loops[best]) e.points.push_back(xf.apply(q));
                    e.meta.insert(QStringLiteral("pattern"),
                                  solid ? QStringLiteral("solid") : patName.toLower());
                    // Draw its edge. A hatch's boundary here is often "derived" from objects that need
                    // not be in the file at all, and without an edge a round ruled fill reads as a few
                    // loose strokes floating in space rather than as a filled circle.
                    e.meta.insert(QStringLiteral("drawBoundary"), true);
                    if (!solid) {
                        e.meta.insert(QStringLiteral("hatchAngle"), findNum(g, "52", 0.0));
                        const double sc = findNum(g, "41", 1.0);
                        e.meta.insert(QStringLiteral("hatchScale"), sc > 1e-9 ? sc : 1.0);
                    }
                    if (e.points.size() >= 3) entities.push_back(std::move(e));
                } else if (type == "SPLINE") {
                    // A spline was skipped outright, which on a real sheet is not a missing line but a
                    // missing SHAPE: the ovals round the wire groups simply were not there, and nothing
                    // about the drawing said anything had been left out.
                    //
                    // It is evaluated here rather than handed on as control points, because a DXF spline
                    // states its own knot vector and may be rational, and this drawing's own spline is a
                    // uniform one - assuming uniform spacing would draw a different curve, wrong by a
                    // different amount for every spline in the file. So it comes in as the curve it
                    // describes: sampled finely, and as a polyline, which is what it now is.
                    std::vector<Vec2> ctrl;
                    std::vector<Vec2> fit;
                    std::vector<double> knots, weights;
                    {
                        Vec2 c{0, 0}, ft{0, 0};
                        bool haveC = false, haveF = false;
                        for (const auto& pr : g) {
                            if (pr.code == "10") {
                                if (haveC) ctrl.push_back(c);
                                c = {pr.value.toDouble(), 0.0};
                                haveC = true;
                            } else if (pr.code == "20" && haveC) {
                                c.y = pr.value.toDouble();
                            } else if (pr.code == "11") {
                                if (haveF) fit.push_back(ft);
                                ft = {pr.value.toDouble(), 0.0};
                                haveF = true;
                            } else if (pr.code == "21" && haveF) {
                                ft.y = pr.value.toDouble();
                            } else if (pr.code == "40") {
                                knots.push_back(pr.value.toDouble());
                            } else if (pr.code == "41") {
                                weights.push_back(pr.value.toDouble());
                            }
                        }
                        if (haveC) ctrl.push_back(c);
                        if (haveF) fit.push_back(ft);
                    }
                    const int degree = std::max(1, findVal(g, "71", "3").toInt());
                    const bool closed = (findVal(g, "70", "0").toInt() & 1) != 0;
                    std::vector<Vec2> curve;
                    if (ctrl.size() >= 2) {
                        std::vector<Vec2> use = ctrl;
                        // A closed spline runs back into itself: the first `degree` points repeat at the
                        // end, which is what makes the join continuous rather than a corner.
                        if (closed)
                            for (int k = 0; k < degree && k < static_cast<int>(ctrl.size()); ++k)
                                use.push_back(ctrl[k]);
                        curve = nurbsCurvePoints(use, closed ? std::vector<double>{} : knots, degree,
                                                 weights.size() == use.size() ? weights
                                                                             : std::vector<double>{});
                    } else if (fit.size() >= 2) {
                        curve = splinePoints(fit);   // only fit points given: pass through them
                    }
                    if (curve.size() < 2) continue;
                    e.type = EntityType::Polyline;
                    for (const Vec2& q : curve) e.points.push_back(xf.apply(q));
                    if (closed && e.points.size() > 2) e.points.push_back(e.points.front());
                    entities.push_back(std::move(e));
                } else if (type == "CIRCLE") {
                    e.type = EntityType::Circle;
                    e.center = xf.apply({findNum(g, "10"), findNum(g, "20")});
                    e.radius = std::max(1e-6, xf.len(findNum(g, "40", 1)));
                    entities.push_back(std::move(e));
                } else if (type == "ARC") {
                    e.type = EntityType::Arc;
                    e.center = xf.apply({findNum(g, "10"), findNum(g, "20")});
                    e.radius = std::max(1e-6, xf.len(findNum(g, "40", 1)));
                    double a0 = findNum(g, "50") * M_PI / 180.0;
                    double a1 = findNum(g, "51") * M_PI / 180.0;
                    a0 = xf.angle(a0);
                    a1 = xf.angle(a1);
                    if (xf.sx * xf.sy < 0.0) std::swap(a0, a1);   // mirrored: the sweep reverses
                    e.startAngle = a0;
                    e.endAngle = a1;
                    entities.push_back(std::move(e));
                } else if (type == "ELLIPSE") {
                    // Centre, the vector to one end of the major axis, and the minor/major ratio.
                    e.type = EntityType::Ellipse;
                    e.center = xf.apply({findNum(g, "10"), findNum(g, "20")});
                    const Vec2 major{findNum(g, "11"), findNum(g, "21")};
                    const double a = std::hypot(major.x, major.y);
                    const double ratio = findNum(g, "40", 1.0);
                    e.radius = std::max(1e-6, xf.len(a));
                    e.radiusY = std::max(1e-6, xf.len(a * ratio));
                    e.rotation = xf.angle(std::atan2(major.y, major.x));
                    e.startAngle = findNum(g, "41", 0.0);
                    e.endAngle = findNum(g, "42", 2.0 * M_PI);
                    entities.push_back(std::move(e));
                } else if (type == "SOLID") {
                    // Four corners (the last two are given in the other order).
                    e.type = EntityType::Polygon;
                    const Vec2 p1 = xf.apply({findNum(g, "10"), findNum(g, "20")});
                    const Vec2 p2 = xf.apply({findNum(g, "11"), findNum(g, "21")});
                    const Vec2 p3 = xf.apply({findNum(g, "12"), findNum(g, "22")});
                    const Vec2 p4 = xf.apply({findNum(g, "13"), findNum(g, "23")});
                    e.points = {p1, p2, p4, p3};
                    // A SOLID is solid: a patch of colour, which is what the arrowheads on this
                    // drawing's dimensions are. Outlined, they read as hollow triangles.
                    e.meta.insert("filled", true);
                    entities.push_back(std::move(e));
                } else { // TEXT / MTEXT
                    e.type = EntityType::Text;
                    const bool mtext = type == QLatin1String("MTEXT");
                    e.a = xf.apply({findNum(g, "10"), findNum(g, "20")});
                    const QString body = decodeDxfText(gatherText(g), mtext);
                    if (body.trimmed().isEmpty()) continue;
                    e.meta.insert("text", body);
                    // The height the file states, whatever it is. A floor of 0.1 used to be imposed here
                    // "so text is never invisible", and on a drawing set up in millimetres-as-units it
                    // silently inflated most of the sheet: text of 0.05 became 0.1 - twice the size -
                    // which overflows every cell it was laid inside and, because the wrap box is imported
                    // too, forces extra wrapped lines that push a title block's rows out of it. A drawing
                    // states its own text size; how small that looks on screen is what the zoom is for.
                    e.meta.insert("size", std::max(1e-9, xf.len(findNum(g, "40", 8))));
                    e.rotation = xf.angle(findNum(g, "50") * M_PI / 180.0);
                    if (mtext) {
                        // The box it wraps in, and where the insertion point sits in it: 1..9 run
                        // top-left, top-centre, top-right, middle-left, ... bottom-right.
                        const double w = xf.len(findNum(g, "41", 0.0));
                        if (w > 1e-6) e.meta.insert("boxWidth", w);
                        const double h = xf.len(findNum(g, "46", 0.0));
                        if (h > 1e-6) e.meta.insert("boxHeight", h);
                        // A turn can be stated as a direction vector (11/21) rather than an angle.
                        //
                        // But plenty of writers put the INSERTION POINT there instead - the very same
                        // coordinates as 10/20 - and a point read as though it were a direction is the
                        // bearing of the text's own position from the origin of the drawing. On the sheet
                        // that showed this up, that turned 92 labels by between 40 and 72 degrees each,
                        // every one of them by a different amount, because each sat somewhere different:
                        // "P2" at (16.85, 29.87) came out at 60.58 degrees, which is exactly
                        // atan2(29.87, 16.85). A vector that IS the insertion point states nothing, so it
                        // is not read as a direction. The labels that really are turned - a genuine
                        // (0, -1) for the column written on its side - are unaffected.
                        if (findVal(g, "50").isEmpty()) {
                            const Vec2 dir{findNum(g, "11", 0.0), findNum(g, "21", 0.0)};
                            const Vec2 ins{findNum(g, "10", 0.0), findNum(g, "20", 0.0)};
                            const bool isTheInsertionPoint =
                                std::hypot(dir.x - ins.x, dir.y - ins.y) < 1e-9;
                            if (!isTheInsertionPoint && std::hypot(dir.x, dir.y) > 1e-12)
                                e.rotation = xf.angle(std::atan2(dir.y, dir.x));
                        }
                        static const char* const kJust[9] = {"TL", "TC", "TR", "ML", "MC",
                                                             "MR", "BL", "BC", "BR"};
                        const int at = std::clamp(findVal(g, "71", "1").toInt(), 1, 9);
                        e.meta.insert("justify", QLatin1String(kJust[at - 1]));
                        // In a DXF the insertion point IS the point the attachment names - the middle
                        // of the block for a centred one - and the width is only where the lines WRAP.
                        // This application's own multiline text works the other way about: the point is
                        // the box's top-left and the text is justified inside the box. Both are wanted,
                        // so the imported text says which it is. Without this, a table's cell text was
                        // justified inside its column instead of about its own centre, and every
                        // centred heading sat off to one side of its cell.
                        e.meta.insert("anchorJustify", true);
                    } else {
                        // TEXT: a justification other than left/baseline measures from the SECOND
                        // alignment point, which is where the text actually sits.
                        const int hj = findVal(g, "72", "0").toInt();
                        const int vj = findVal(g, "73", "0").toInt();
                        if (hj != 0 || vj != 0) {
                            const Vec2 alt{findNum(g, "11"), findNum(g, "21")};
                            if (std::abs(alt.x) > 1e-12 || std::abs(alt.y) > 1e-12)
                                e.a = xf.apply(alt);
                            static const char* const kH[5] = {"L", "C", "R", "L", "C"};
                            static const char* const kV[4] = {"B", "B", "M", "T"};
                            e.meta.insert("justify",
                                          QString(QLatin1String(kV[std::clamp(vj, 0, 3)])) +
                                              QLatin1String(kH[std::clamp(hj, 0, 4)]));
                        }
                    }
                    entities.push_back(std::move(e));
                }
            }
        };
    readRange(entFrom, entTo, Xf{}, 0);

    // Into the order the file says the drawing is drawn in.
    if (!drawKey.isEmpty() && slices.size() > 1) {
        std::vector<int> order(slices.size());
        for (size_t k = 0; k < order.size(); ++k) order[k] = int(k);
        std::stable_sort(order.begin(), order.end(),
                         [&](int a, int b) { return slices[a].key < slices[b].key; });
        std::vector<Entity> sorted;
        sorted.reserve(entities.size());
        for (int k : order) {
            const int from = slices[size_t(k)].start;
            const int to =
                k + 1 < int(slices.size()) ? slices[size_t(k) + 1].start : int(entities.size());
            for (int m = from; m < to; ++m) sorted.push_back(std::move(entities[size_t(m)]));
        }
        entities = std::move(sorted);
    }

    std::vector<Layer> layerList;
    for (const auto& l : layers) layerList.push_back(l);
    if (layerList.empty()) layerList = Document::defaultLayers();
    doc.layers() = std::move(layerList);
    doc.entities() = std::move(entities);
    doc.setActiveLayer(doc.layers().front().name);
    doc.setDirty(false);
    return true;
}

bool exportFile(const Document& doc, const QString& path, QString* error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    const QByteArray payload = toDxf(doc).toUtf8();
    if (file.write(payload) != payload.size()) {
        if (error) *error = file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error) *error = file.errorString();
        return false;
    }
    return true;
}

bool importFile(Document& doc, const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    // Text or binary: both are DXF, and which one it is is the file's business rather than the user's.
    return parseBytes(doc, file.readAll(), error);
}

} // namespace Loopwire::dxf
