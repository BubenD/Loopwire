#include "system/FileText.h"

#include "io/PdfText.h"
#include "io/PdfImport.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>

namespace Edraw_Desktop {

namespace {

// WHAT IS NOT WORDS in a drawing: names of layers, line types and fonts, ids, kinds.
const QSet<QString>& notWords() {
    static const QSet<QString> keys{QStringLiteral("id"),       QStringLiteral("uid"),       QStringLiteral("layer"),
                                    QStringLiteral("lineType"), QStringLiteral("linetype"),  QStringLiteral("color"),
                                    QStringLiteral("colour"),   QStringLiteral("font"),      QStringLiteral("style"),
                                    QStringLiteral("type"),     QStringLiteral("kind"),      QStringLiteral("unit"),
                                    QStringLiteral("version"),  QStringLiteral("format"),    QStringLiteral("app"),
                                    QStringLiteral("image"),    QStringLiteral("imageData"), QStringLiteral("data"),
                                    QStringLiteral("cellAlign")};
    return keys;
}

void walk(const QJsonValue& v, const QString& key, std::vector<TextPiece>& out) {
    if (v.isString()) {
        const QString s = v.toString();
        // A picture held as text, or an id: not words.
        if (!s.trimmed().isEmpty() && s.size() <= 4000 && !notWords().contains(key)) out.push_back({QStringLiteral("Drawing"), s});
    } else if (v.isArray()) {
        for (const QJsonValue& e : v.toArray()) walk(e, key, out);
    } else if (v.isObject()) {
        const QJsonObject o = v.toObject();
        for (auto it = o.begin(); it != o.end(); ++it) walk(it.value(), it.key(), out);
    }
}

QString decode(const QByteArray& raw) {
    // UTF-8 where it is, else the Windows code page a CSV from a spreadsheet is likely in.
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString text = utf8.decode(raw);
    if (!utf8.hasError()) return text;
    QStringDecoder latin(QStringDecoder::Latin1);
    return latin.decode(raw);
}

QStringList splitCsvLine(const QString& line, QChar sep) {
    QStringList cells;
    QString cell;
    bool quoted = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (quoted) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
                    cell += c;
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                cell += c;
            }
        } else if (c == QLatin1Char('"')) {
            quoted = true;
        } else if (c == sep) {
            cells << cell;
            cell.clear();
        } else {
            cell += c;
        }
    }
    cells << cell;
    return cells;
}

} // namespace

std::vector<TextPiece> fileText(const QString& path, QString* why) {
    std::vector<TextPiece> out;
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("dwg")) {
        if (why) *why = QStringLiteral("a DWG holds its text compressed - search its PDF or DXF");
        return out;
    }
    if (suffix == QLatin1String("pdf")) {
        QFile file(path);if(!file.open(QIODevice::ReadOnly)){if(why)*why=file.errorString();return out;}
        const auto pages=pdf::readTextPages(file.readAll(),why);
        for(int page=0;page<static_cast<int>(pages.size());++page)if(!pages[static_cast<size_t>(page)].text.trimmed().isEmpty())
            out.push_back({pages[static_cast<size_t>(page)].size.isEmpty()?QStringLiteral("PDF"):QStringLiteral("Page %1").arg(page+1),pages[static_cast<size_t>(page)].text});
        return out;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (why) *why = QStringLiteral("it could not be opened");
        return out;
    }
    const QByteArray raw = f.readAll();
    if (suffix == QLatin1String("dwx")) {
        // EVERY WORD OF THE DRAWING: its texts, notes, labels, table cells, attributes, title block - in the model, the
        // sheets and the blocks alike.
        const QJsonDocument doc = QJsonDocument::fromJson(raw);
        if (!doc.isObject()) {
            if (why) *why = QStringLiteral("it could not be read as a drawing");
            return out;
        }
        walk(doc.object(), QString(), out);
        return out;
    }
    if (suffix == QLatin1String("dxf")) {
        // THE TEXT STRINGS: the values after group codes 1 and 3 (text), 2 (names), 1000 (extended data). A binary DXF
        // is not read.
        if (raw.startsWith("AutoCAD Binary DXF")) {
            if (why) *why = QStringLiteral("a binary DXF is not read");
            return out;
        }
        const QStringList lines = decode(raw).split(QLatin1Char('\n'));
        for (int i = 0; i + 1 < lines.size(); i += 2) {
            const int code = lines.at(i).trimmed().toInt();
            if (code == 1 || code == 2 || code == 3 || code == 1000) {
                const QString value = lines.at(i + 1).trimmed();
                if (!value.isEmpty()) out.push_back({QStringLiteral("Line %1").arg(i + 2), value});
            }
        }
        return out;
    }
    if (suffix == QLatin1String("csv")) {
        // EACH CELL, by its row and its column's heading - the separator the first line uses most.
        const QStringList lines = decode(raw).split(QRegularExpression(QStringLiteral("\r?\n")));
        if (lines.isEmpty()) return out;
        const QString first = lines.first();
        QChar sep = QLatin1Char(',');
        int most = first.count(QLatin1Char(','));
        for (const QChar c : {QLatin1Char(';'), QLatin1Char('\t'), QLatin1Char('|')})
            if (first.count(c) > most) {
                most = first.count(c);
                sep = c;
            }
        const QStringList header = splitCsvLine(first, sep);
        for (int r = 0; r < lines.size(); ++r) {
            const QStringList cells = splitCsvLine(lines.at(r), sep);
            for (int c = 0; c < cells.size(); ++c) {
                if (cells.at(c).trimmed().isEmpty()) continue;
                const QString column = r > 0 && c < header.size() && !header.at(c).trimmed().isEmpty() ? header.at(c).trimmed()
                                                                                                        : QStringLiteral("Col %1").arg(c + 1);
                out.push_back({QStringLiteral("Row %1 - %2").arg(r + 1).arg(column), cells.at(c)});
            }
        }
        return out;
    }
    if (why) *why = QStringLiteral("not a kind of file that is searched");
    return out;
}

} // namespace Edraw_Desktop
