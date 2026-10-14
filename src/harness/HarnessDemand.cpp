#include "harness/HarnessDemand.h"

#include "harness/Harness.h"
#include "model/Document.h"
#include "render/EntityRenderer.h"
#include "system/SystemImport.h"

#include <QFont>
#include <QHash>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QPoint>
#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>

#include <algorithm>
#include <map>

namespace Loopwire {

namespace {

const QString kInfo = QStringLiteral("Info");
const QString kPinout = QStringLiteral("Pinout");

// ---- The table's columns ----------------------------------------------------------------------------------------
// The columns before the signals and the one after them, by heading - written in this order, found by heading when
// read (spaces and line breaks aside, any case), so a table laid out a little differently still reads.

enum Fixed { CableName, Function, NumConnectors, Length, ConnectorType, ConnectorImg, MaxPins, EmptyPins, ConnectorName, FixedCount };

struct ColumnHeading {
    QString text;
    int width;
};

const ColumnHeading kFixed[FixedCount] = {
    {QStringLiteral("Cable Name"), 13},      {QStringLiteral("Function"), 22},
    {QStringLiteral("Number of connectors"), 11}, {QStringLiteral("Length (mm)"), 8},
    {QStringLiteral("Connector type"), 30},  {QStringLiteral("Connector IMG"), 14},
    {QStringLiteral("Max Number of pins"), 8}, {QStringLiteral("Empty Pins"), 7},
    {QStringLiteral("Connector Name"), 12}};
const QString kMating = QStringLiteral("Mating Connector");

// A heading as compared: letters and digits only, lower case. "Max\nNumber\nof pins" is "maxnumberofpins".
QString key(const QString& s) {
    QString k;
    for (const QChar c : s)
        if (c.isLetterOrNumber()) k += c.toLower();
    return k;
}

// A group label as written - "Twist", "shield + twist", "Shiled" (as typed in a hurry) - as one of Twist, Shield,
// Shield+Twist or Coax. Empty when it is none of them.
QString groupLabel(const QString& written) {
    const QString k = key(written);
    if (k.isEmpty()) return {};
    const bool shield = k.contains(QLatin1String("shi")) || k.contains(QLatin1String("scr"));
    const bool twist = k.contains(QLatin1String("twist"));
    if (k.contains(QLatin1String("coax"))) return QStringLiteral("Coax");
    if (shield && twist) return QStringLiteral("Shield+Twist");
    if (shield) return QStringLiteral("Shield");
    if (twist) return QStringLiteral("Twist");
    return {};
}

const QStringList& infoFields() {
    static const QStringList f{QStringLiteral("Customer"), QStringLiteral("Project"),  QStringLiteral("Harness P/N"),
                               QStringLiteral("Revision"), QStringLiteral("Date"),     QStringLiteral("Contact"),
                               QStringLiteral("E-mail"),   QStringLiteral("Notes")};
    return f;
}

QString* infoField(DemandInfo& i, int at) {
    QString* f[] = {&i.customer, &i.project, &i.partNumber, &i.revision, &i.date, &i.contact, &i.email, &i.notes};
    return at >= 0 && at < 8 ? f[at] : nullptr;
}

// ---- Writing the workbook ----------------------------------------------------------------------------------------

QString xmlText(const QString& s) {
    QString out = s.toHtmlEscaped();
    out.replace(QLatin1Char('\n'), QStringLiteral("&#10;"));
    return out;
}

QString colName(int c) {   // 0 -> A, 25 -> Z, 26 -> AA
    QString s;
    for (++c; c > 0; c = (c - 1) / 26) s.prepend(QChar(u'A' + (c - 1) % 26));
    return s;
}

QString cellRef(int row, int col) { return colName(col) + QString::number(row + 1); }   // both 0-based

// The styles the cells are given, by number (see stylesXml).
enum Style {
    Plain = 0, Heading = 1, Body = 2, Title = 3, Note = 4, Label = 5, SignalHead = 6, GroupHead = 7,
    GroupHeadRed = 8, BodyLeft = 9, FunctionText = 10, Value = 11
};

struct Sheet {
    QString name;
    std::vector<int> widths;
    std::map<std::pair<int, int>, std::pair<QString, int>> cells;   // (row, col) -> text, style
    std::map<int, double> heights;
    std::vector<QString> merges;
    std::vector<std::pair<QString, QString>> lists;   // drop-downs: cells, what they offer
    void put(int r, int c, const QString& text, int style) { cells[{r, c}] = {text, style}; }
};

QString sheetXml(const Sheet& s) {
    QString x = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                               "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
                               "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
                               "<sheetViews><sheetView workbookViewId=\"0\"/></sheetViews>");
    if (!s.widths.empty()) {
        x += QStringLiteral("<cols>");
        for (size_t c = 0; c < s.widths.size(); ++c)
            x += QStringLiteral("<col min=\"%1\" max=\"%1\" width=\"%2\" customWidth=\"1\"/>").arg(c + 1).arg(s.widths[c]);
        x += QStringLiteral("</cols>");
    }
    x += QStringLiteral("<sheetData>");
    int row = -1;
    for (const auto& [rc, cell] : s.cells) {
        if (rc.first != row) {
            if (row >= 0) x += QStringLiteral("</row>");
            row = rc.first;
            x += QStringLiteral("<row r=\"%1\"").arg(row + 1);
            if (const auto h = s.heights.find(row); h != s.heights.end())
                x += QStringLiteral(" ht=\"%1\" customHeight=\"1\"").arg(h->second);
            x += QLatin1Char('>');
        }
        const QString ref = cellRef(rc.first, rc.second);
        if (cell.first.isEmpty())
            x += QStringLiteral("<c r=\"%1\" s=\"%2\"/>").arg(ref).arg(cell.second);
        else
            x += QStringLiteral("<c r=\"%1\" s=\"%2\" t=\"inlineStr\"><is><t xml:space=\"preserve\">%3</t></is></c>")
                     .arg(ref)
                     .arg(cell.second)
                     .arg(xmlText(cell.first));
    }
    if (row >= 0) x += QStringLiteral("</row>");
    x += QStringLiteral("</sheetData>");
    if (!s.merges.empty()) {
        x += QStringLiteral("<mergeCells count=\"%1\">").arg(s.merges.size());
        for (const QString& m : s.merges) x += QStringLiteral("<mergeCell ref=\"%1\"/>").arg(m);
        x += QStringLiteral("</mergeCells>");
    }
    if (!s.lists.empty()) {
        x += QStringLiteral("<dataValidations count=\"%1\">").arg(s.lists.size());
        for (const auto& [range, offer] : s.lists)
            x += QStringLiteral("<dataValidation type=\"list\" allowBlank=\"1\" showErrorMessage=\"0\" sqref=\"%1\">"
                                "<formula1>%2</formula1></dataValidation>")
                     .arg(range, xmlText(offer));
        x += QStringLiteral("</dataValidations>");
    }
    x += QStringLiteral("<pageMargins left=\"0.4\" right=\"0.4\" top=\"0.5\" bottom=\"0.5\" header=\"0.3\" footer=\"0.3\"/>"
                        "<pageSetup orientation=\"landscape\"/></worksheet>");
    return x;
}

QString stylesXml() {
    auto xf = [](int font, int fill, int border, const char* horizontal, bool text = false) {
        return QStringLiteral("<xf numFmtId=\"%1\" fontId=\"%2\" fillId=\"%3\" borderId=\"%4\" xfId=\"0\" applyNumberFormat=\"1\" "
                              "applyFont=\"1\" applyFill=\"1\" applyBorder=\"1\" applyAlignment=\"1\">"
                              "<alignment horizontal=\"%5\" vertical=\"center\" wrapText=\"1\"/></xf>")
            .arg(text ? 49 : 0)
            .arg(font)
            .arg(fill)
            .arg(border)
            .arg(QLatin1String(horizontal));
    };
    QString x = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        "<fonts count=\"5\">"
        "<font><sz val=\"10\"/><name val=\"Calibri\"/></font>"
        "<font><b/><sz val=\"10\"/><name val=\"Calibri\"/></font>"
        "<font><b/><sz val=\"16\"/><color rgb=\"FF1F3A5F\"/><name val=\"Calibri\"/></font>"
        "<font><i/><sz val=\"10\"/><color rgb=\"FF555555\"/><name val=\"Calibri\"/></font>"
        "<font><b/><sz val=\"10\"/><color rgb=\"FFFF0000\"/><name val=\"Calibri\"/></font>"
        "</fonts>"
        "<fills count=\"5\">"
        "<fill><patternFill patternType=\"none\"/></fill>"
        "<fill><patternFill patternType=\"gray125\"/></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFC6EFCE\"/><bgColor indexed=\"64\"/></patternFill></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FFDDEBF7\"/><bgColor indexed=\"64\"/></patternFill></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FF9BC2E6\"/><bgColor indexed=\"64\"/></patternFill></fill>"
        "</fills>"
        "<borders count=\"2\"><border><left/><right/><top/><bottom/><diagonal/></border>"
        "<border><left style=\"thin\"><color rgb=\"FF000000\"/></left><right style=\"thin\"><color rgb=\"FF000000\"/></right>"
        "<top style=\"thin\"><color rgb=\"FF000000\"/></top><bottom style=\"thin\"><color rgb=\"FF000000\"/></bottom>"
        "<diagonal/></border></borders>"
        "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
        "<cellXfs count=\"12\">"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>");
    x += xf(1, 2, 1, "center");          // Heading
    x += xf(0, 0, 1, "center", true);    // Body
    x += QStringLiteral("<xf numFmtId=\"0\" fontId=\"2\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\"/>");   // Title
    x += QStringLiteral("<xf numFmtId=\"0\" fontId=\"3\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\" applyAlignment=\"1\">"
                        "<alignment vertical=\"top\" wrapText=\"1\"/></xf>");   // Note
    x += xf(1, 0, 1, "left");            // Label
    x += xf(1, 3, 1, "center", true);    // SignalHead
    x += xf(1, 4, 1, "center");          // GroupHead
    x += xf(4, 4, 1, "center");          // GroupHeadRed
    x += xf(0, 0, 1, "left", true);      // BodyLeft
    x += xf(4, 0, 1, "center");          // FunctionText
    x += xf(0, 0, 1, "left", true);      // Value
    x += QStringLiteral("</cellXfs><cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>"
                        "</styleSheet>");
    return x;
}

// What a cell says - see below.
QString colourCode(const QString& written);
QString firstLine(const QString& s);
QStringList cellParts(const QString& cell);

// ONE CABLE'S BLOCK, from `top` (0-based): the group row, the headings, a row per connector. The rows it took.
int writeCableBlock(Sheet& s, int top, const DemandCable& cable, int blankSignals, int blankConnectors) {
    const int nSignals = std::max<int>(int(cable.signalColumns.size()), blankSignals);
    const int nConnectors = std::max<int>(int(cable.connectors.size()), blankConnectors);
    const int head = top + 1, first = top + 2, last = first + nConnectors - 1;
    const int sig0 = FixedCount, mating = FixedCount + nSignals;
    for (int c = 0; c < FixedCount; ++c) s.put(head, c, kFixed[c].text, Heading);
    s.put(head, mating, kMating, Heading);
    s.heights[head] = 40;
    for (int k = 0; k < nSignals; ++k)
        s.put(head, sig0 + k, k < int(cable.signalColumns.size()) ? cable.signalColumns[size_t(k)].name : QString(), SignalHead);
    // THE GROUP ROW: each run of one group over its columns, merged; the rest left to be filled in.
    for (int k = 0; k < nSignals; ++k) {
        const DemandSignal* sig = k < int(cable.signalColumns.size()) ? &cable.signalColumns[size_t(k)] : nullptr;
        if (!sig || sig->group.isEmpty()) {
            s.put(top, sig0 + k, QString(), Plain);
            continue;
        }
        int end = k;
        while (end + 1 < int(cable.signalColumns.size()) && cable.signalColumns[size_t(end + 1)].groupId == sig->groupId &&
               cable.signalColumns[size_t(end + 1)].group == sig->group)
            ++end;
        const bool red = sig->group != QLatin1String("Twist");
        for (int c = k; c <= end; ++c) s.put(top, sig0 + c, c == k ? sig->group : QString(), red ? GroupHeadRed : GroupHead);
        if (end > k) s.merges.push_back(cellRef(top, sig0 + k) + QLatin1Char(':') + cellRef(top, sig0 + end));
        k = end;
    }
    s.lists.push_back({cellRef(top, sig0) + QLatin1Char(':') + cellRef(top, mating - 1),
                       QStringLiteral("\"Twist,Shield,Shield+Twist,Coax\"")});
    // THE CONNECTORS, the cable's own cells merged down beside them.
    for (int r = 0; r < nConnectors; ++r) {
        const DemandConnector* dc = r < int(cable.connectors.size()) ? &cable.connectors[size_t(r)] : nullptr;
        const int row = first + r;
        s.put(row, CableName, r == 0 ? cable.name : QString(), Heading);
        s.put(row, Function, r == 0 ? cable.function : QString(), FunctionText);
        s.put(row, NumConnectors, r == 0 && !cable.connectors.empty() ? QString::number(cable.connectors.size()) : QString(),
              Body);
        s.put(row, Length, dc ? dc->length : QString(), Body);
        s.put(row, ConnectorType, dc ? dc->type : QString(), Body);
        s.put(row, ConnectorImg, dc ? dc->image : QString(), Body);
        s.put(row, MaxPins, dc && dc->maxPins > 0 ? QString::number(dc->maxPins) : QString(), Body);
        QString empty;
        if (dc && dc->maxPins > 0) {
            int used = 0;
            for (const QString& cell : dc->cells) {
                if (cell.trimmed().isEmpty() || key(cell) == QLatin1String("na")) continue;
                // A lead is one; a cell of pins is as many as it names.
                used += colourCode(firstLine(cell)).isEmpty() ? std::max<int>(1, int(cellParts(cell).size())) : 1;
            }
            empty = QString::number(std::max(0, dc->maxPins - used));
        }
        s.put(row, EmptyPins, empty, Body);
        s.put(row, ConnectorName, dc ? dc->name : QString(), Body);
        for (int k = 0; k < nSignals; ++k)
            s.put(row, sig0 + k, dc && k < dc->cells.size() ? dc->cells[k] : QString(), Body);
        s.put(row, mating, dc ? dc->mating : QString(), BodyLeft);
        s.heights[row] = 30;
    }
    if (nConnectors > 1)
        for (const int c : {int(CableName), int(Function), int(NumConnectors)})
            s.merges.push_back(cellRef(first, c) + QLatin1Char(':') + cellRef(last, c));
    return last - top + 1;
}

// ---- Reading the workbook ----------------------------------------------------------------------------------------

struct ReadSheet {
    std::vector<QStringList> rows;
    std::vector<std::pair<QPoint, QPoint>> merges;   // (col, row) top-left and bottom-right, 0-based
    QString at(int r, int c) const {
        return r >= 0 && r < int(rows.size()) && c >= 0 && c < rows[size_t(r)].size() ? rows[size_t(r)][c] : QString();
    }
};

int columnIndex(const QString& ref) {   // "BC12" -> 54
    int c = 0;
    for (const QChar ch : ref) {
        if (!ch.isLetter()) break;
        c = c * 26 + (ch.toUpper().unicode() - u'A' + 1);
    }
    return c - 1;
}

QPoint refPoint(const QString& ref) {   // "B3" -> (1, 2)
    int i = 0;
    while (i < ref.size() && ref[i].isLetter()) ++i;
    return {columnIndex(ref), ref.mid(i).toInt() - 1};
}

QStringList sharedStrings(const QByteArray& xml) {
    QStringList out;
    QXmlStreamReader r(xml);
    QString cur;
    bool inSi = false;
    while (!r.atEnd()) {
        r.readNext();
        if (r.isStartElement() && r.name() == QLatin1String("si")) {
            inSi = true;
            cur.clear();
        } else if (r.isEndElement() && r.name() == QLatin1String("si")) {
            inSi = false;
            out << cur;
        } else if (inSi && r.isStartElement() && r.name() == QLatin1String("t")) {
            cur += r.readElementText(QXmlStreamReader::IncludeChildElements);
        } else if (inSi && r.isStartElement() && r.name() == QLatin1String("rPh")) {
            r.skipCurrentElement();   // phonetic hints are not the text
        }
    }
    return out;
}

ReadSheet readSheet(const QByteArray& xml, const QStringList& shared) {
    ReadSheet s;
    QXmlStreamReader r(xml);
    int rowAt = -1;
    while (!r.atEnd()) {
        r.readNext();
        if (!r.isStartElement()) continue;
        if (r.name() == QLatin1String("row")) {
            const int n = r.attributes().value(QLatin1String("r")).toInt();
            rowAt = n > 0 ? n - 1 : rowAt + 1;
            if (int(s.rows.size()) <= rowAt) s.rows.resize(size_t(rowAt + 1));
        } else if (r.name() == QLatin1String("mergeCell")) {
            const QStringList ends = r.attributes().value(QLatin1String("ref")).toString().split(QLatin1Char(':'));
            if (ends.size() == 2) s.merges.push_back({refPoint(ends[0]), refPoint(ends[1])});
        } else if (r.name() == QLatin1String("c") && rowAt >= 0) {
            const QString ref = r.attributes().value(QLatin1String("r")).toString();
            const QString type = r.attributes().value(QLatin1String("t")).toString();
            QStringList& row = s.rows[size_t(rowAt)];
            const int col = ref.isEmpty() ? int(row.size()) : columnIndex(ref);
            QString value;
            while (!r.atEnd()) {
                r.readNext();
                if (r.isEndElement() && r.name() == QLatin1String("c")) break;
                if (!r.isStartElement()) continue;
                if (r.name() == QLatin1String("v")) value = r.readElementText();
                else if (r.name() == QLatin1String("t")) value += r.readElementText(QXmlStreamReader::IncludeChildElements);
            }
            if (type == QLatin1String("s")) {
                const int i = value.toInt();
                value = i >= 0 && i < shared.size() ? shared[i] : QString();
            } else if (type == QLatin1String("b")) {
                value = value == QLatin1String("1") ? QStringLiteral("Yes") : QStringLiteral("No");
            } else if (type.isEmpty() || type == QLatin1String("n")) {
                // A number as Excel keeps it: 2 is "2", not "2.0000000001".
                bool ok = false;
                const double d = value.toDouble(&ok);
                if (ok) value = QString::number(d, 'g', 12);
            }
            if (col >= 0) {
                while (row.size() <= col) row << QString();
                row[col] = value.trimmed();
            }
        }
    }
    return s;
}

// The workbook's sheets in order, each with its name.
bool readWorkbook(const QByteArray& bytes, std::vector<std::pair<QString, ReadSheet>>& sheets, QString* error) {
    QHash<QString, QByteArray> files;
    for (const ZipItem& z : readZip(bytes)) files.insert(z.name, z.data);
    if (!files.contains(QStringLiteral("xl/workbook.xml"))) {
        if (error) *error = QStringLiteral("This is not an Excel workbook (.xlsx).");
        return false;
    }
    const QStringList shared = sharedStrings(files.value(QStringLiteral("xl/sharedStrings.xml")));
    QHash<QString, QString> targets;   // relationship id -> file
    {
        QXmlStreamReader r(files.value(QStringLiteral("xl/_rels/workbook.xml.rels")));
        while (!r.atEnd()) {
            r.readNext();
            if (r.isStartElement() && r.name() == QLatin1String("Relationship")) {
                QString target = r.attributes().value(QLatin1String("Target")).toString();
                if (target.startsWith(QLatin1Char('/'))) target = target.mid(1);
                else target = QStringLiteral("xl/") + target;
                targets.insert(r.attributes().value(QLatin1String("Id")).toString(), target);
            }
        }
    }
    QXmlStreamReader r(files.value(QStringLiteral("xl/workbook.xml")));
    while (!r.atEnd()) {
        r.readNext();
        if (!r.isStartElement() || r.name() != QLatin1String("sheet")) continue;
        QString id;
        for (const QXmlStreamAttribute& a : r.attributes())
            if (a.name() == QLatin1String("id")) id = a.value().toString();   // r:id, whatever its prefix
        const QString file = targets.value(id);
        if (!file.isEmpty() && files.contains(file))
            sheets.push_back({r.attributes().value(QLatin1String("name")).toString().trimmed(),
                              readSheet(files.value(file), shared)});
    }
    return true;
}

// A merged area's cells all take the value of its top-left cell - which is the only one a merge keeps.
void spreadMerges(ReadSheet& s) {
    for (const auto& [tl, br] : s.merges) {
        const QString v = s.at(tl.y(), tl.x());
        for (int r = tl.y(); r <= br.y(); ++r)
            for (int c = tl.x(); c <= br.x(); ++c) {
                if (r == tl.y() && c == tl.x()) continue;
                if (int(s.rows.size()) <= r) s.rows.resize(size_t(r + 1));
                QStringList& row = s.rows[size_t(r)];
                while (row.size() <= c) row << QString();
                if (row[c].isEmpty()) row[c] = v;
            }
    }
}

// Which merged area of row `r` column `c` is in, as its first column; -1 when none.
int mergeStart(const ReadSheet& s, int r, int c) {
    for (const auto& [tl, br] : s.merges)
        if (r >= tl.y() && r <= br.y() && c >= tl.x() && c <= br.x()) return tl.x();
    return -1;
}

// Every block on a sheet: found by its heading row (a "Cable Name" heading), read down to the first row that is
// empty, a group row or another block's headings.
void readBlocks(const ReadSheet& raw, std::vector<DemandCable>& out) {
    ReadSheet s = raw;
    spreadMerges(s);
    for (int h = 0; h < int(s.rows.size()); ++h) {
        const QStringList& head = s.rows[size_t(h)];
        int col[FixedCount];
        std::fill(std::begin(col), std::end(col), -1);
        int mating = -1;
        for (int c = 0; c < head.size(); ++c) {
            const QString k = key(head[c]);
            if (k.isEmpty()) continue;
            if (k == QLatin1String("cablename")) col[CableName] = c;
            else if (k == QLatin1String("function")) col[Function] = c;
            else if (k.startsWith(QLatin1String("numberofconn"))) col[NumConnectors] = c;
            else if (k.startsWith(QLatin1String("length"))) col[Length] = c;
            else if (k == QLatin1String("connectortype")) col[ConnectorType] = c;
            else if (k.startsWith(QLatin1String("connectorim"))) col[ConnectorImg] = c;
            else if (k.startsWith(QLatin1String("max"))) col[MaxPins] = c;
            else if (k.startsWith(QLatin1String("empty"))) col[EmptyPins] = c;
            else if (k == QLatin1String("connectorname")) col[ConnectorName] = c;
            else if (k.startsWith(QLatin1String("mating"))) mating = c;
        }
        if (col[CableName] < 0 || col[ConnectorName] < 0) continue;
        // The signals: the columns after the connector's name, up to the mating connector (or the last heading).
        const int sigFrom = col[ConnectorName] + 1;
        int sigTo = mating >= 0 ? mating - 1 : int(head.size()) - 1;
        while (sigTo >= sigFrom && s.at(h, sigTo).isEmpty()) --sigTo;
        DemandCable cable;
        // THE GROUP ROW, above: a merged label is one group over its columns; labels written cell by cell are one
        // group to a run of the same label - a twist or a coax two columns at a time, since they stand side by side.
        int groupNo = 0, runLen = 0;
        for (int c = sigFrom; c <= sigTo; ++c) {
            DemandSignal sig;
            sig.name = s.at(h, c);
            sig.group = groupLabel(s.at(h - 1, c));
            if (!sig.group.isEmpty()) {
                const int m = mergeStart(raw, h - 1, c);
                const bool sameRun = !cable.signalColumns.empty() && cable.signalColumns.back().group == sig.group;
                const bool inMerge = m >= 0 && m < c;
                const bool pairFull = (sig.group == QLatin1String("Twist") || sig.group == QLatin1String("Coax")) && runLen >= 2;
                const bool continues = sameRun && (inMerge || (m < 0 && mergeStart(raw, h - 1, c - 1) < 0 && !pairFull));
                if (!continues) {
                    ++groupNo;
                    runLen = 0;
                }
                sig.groupId = groupNo;
                ++runLen;
            } else {
                runLen = 0;
            }
            cable.signalColumns.push_back(sig);
        }
        // THE CONNECTORS, a row each, down to an empty row, a group row or another block's headings.
        for (int r = h + 1; r < int(s.rows.size()); ++r) {
            bool any = false, onlyLabels = true, isHeading = false;
            for (int c = 0; c < s.rows[size_t(r)].size(); ++c) {
                const QString v = s.at(r, c);
                if (key(v) == QLatin1String("cablename")) isHeading = true;
                if (v.isEmpty() || c == col[CableName] || c == col[Function] || c == col[NumConnectors]) continue;
                any = true;
                if (groupLabel(v).isEmpty()) onlyLabels = false;
            }
            if (!any || onlyLabels || isHeading) break;
            if (cable.name.isEmpty()) cable.name = s.at(r, col[CableName]);
            if (cable.function.isEmpty()) cable.function = s.at(r, col[Function]);
            DemandConnector dc;
            dc.sheetRow = r + 1;
            dc.name = s.at(r, col[ConnectorName]);
            dc.type = s.at(r, col[ConnectorType]);
            dc.image = s.at(r, col[ConnectorImg]);
            dc.maxPins = s.at(r, col[MaxPins]).toInt();
            dc.length = s.at(r, col[Length]);
            dc.mating = s.at(r, mating);
            for (int c = sigFrom; c <= sigTo; ++c) dc.cells << s.at(r, c);
            cable.connectors.push_back(dc);
        }
        if (!cable.connectors.empty()) out.push_back(cable);
    }
}

// ---- What a cell says -------------------------------------------------------------------------------------------

// A colour as written - its code (RD) or its name (Red), any case - as the builder's code. Empty when unknown.
QString colourCode(const QString& written) {
    const QString w = written.trimmed();
    if (w.isEmpty()) return {};
    for (const QString& code : wireColorCodes()) {
        if (code.compare(w, Qt::CaseInsensitive) == 0) return code;
        if (wireColorName(code).compare(w, Qt::CaseInsensitive) == 0) return code;
    }
    return {};
}

// The first line of a cell: "43-J2\nCOMM" is 43-J2, "RED\n"U"" is RED.
QString firstLine(const QString& s) { return s.section(QLatin1Char('\n'), 0, 0).trimmed(); }

QStringList cellParts(const QString& cell) {
    return firstLine(cell).split(QRegularExpression(QStringLiteral("[,;/ ]+")), Qt::SkipEmptyParts);
}

} // namespace

// ---- The workbook, out ------------------------------------------------------------------------------------------

QByteArray harnessDemandToXlsx(const HarnessDemand& d) {
    std::vector<Sheet> sheets;
    // INSTRUCTIONS.
    {
        Sheet s;
        s.name = QStringLiteral("Instructions");
        s.widths = {24, 100};
        int r = 0;
        auto line = [&](const QString& a, const QString& b = QString()) {
            if (!a.isEmpty()) s.put(r, 0, a, Label);
            if (!b.isEmpty()) s.put(r, 1, b, Note);
            ++r;
        };
        s.put(r++, 0, QStringLiteral("Harness demand"), Title);
        line(QString(), QStringLiteral("Describe each cable on the Pinout sheet and send the workbook back. An overview is "
                                       "enough; part numbers are welcome but not required."));
        line(QString());
        line(QStringLiteral("Info"), QStringLiteral("Who you are and which project this is."));
        line(QStringLiteral("Pinout"), QStringLiteral("One block per cable: a row for each connector, a column for each "
                                                      "signal. For more cables, copy the block below the last one, with "
                                                      "an empty row between them."));
        line(QStringLiteral("   Rows"), QStringLiteral("The first connector is the cable's main end; the others are the "
                                                       "ends it goes to."));
        line(QStringLiteral("   Signal columns"), QStringLiteral("Write the signal's name in the heading (CAN_H, +24V, "
                                                                 "GND ...). Under it, on each connector's row, write the "
                                                                 "pin it lands on there. Several pins: 21,25."));
        line(QStringLiteral("   Leads"), QStringLiteral("A part with flying leads instead of pins (a motor): write the "
                                                        "lead's colour (White, Blue ...) in place of a pin."));
        line(QStringLiteral("   Group row"), QStringLiteral("Above the signal headings: Twist over the two columns of a "
                                                            "twisted pair, Shield over the columns screened together, "
                                                            "Shield+Twist for both, Coax over a coax's signal and its "
                                                            "braid. Merge the cells over a group, or write the label in "
                                                            "each of them."));
        line(QStringLiteral("   Max Number of pins"), QStringLiteral("How many pins the connector has; N/A for leads."));
        line(QStringLiteral("   Mating Connector"), QStringLiteral("What it plugs into, if known."));
        sheets.push_back(s);
    }
    // INFO.
    {
        Sheet s;
        s.name = kInfo;
        s.widths = {18, 60};
        s.put(0, 0, QStringLiteral("Field"), Heading);
        s.put(0, 1, QStringLiteral("Value"), Heading);
        DemandInfo info = d.info;
        for (int i = 0; i < infoFields().size(); ++i) {
            s.put(i + 1, 0, infoFields()[i], Label);
            s.put(i + 1, 1, *infoField(info, i), Value);
        }
        sheets.push_back(s);
    }
    // THE PINOUT: the cables one under another, or one empty block to fill in.
    {
        Sheet s;
        s.name = kPinout;
        int widest = d.cables.empty() ? 24 : 0;
        for (const DemandCable& c : d.cables) widest = std::max<int>(widest, int(c.signalColumns.size()));
        for (const ColumnHeading& h : kFixed) s.widths.push_back(h.width);
        for (int k = 0; k < widest; ++k) s.widths.push_back(9);
        s.widths.push_back(24);
        int top = 0;
        if (d.cables.empty()) {
            writeCableBlock(s, top, DemandCable{}, 24, 6);
        } else {
            for (const DemandCable& c : d.cables) top += writeCableBlock(s, top, c, widest, 0) + 1;
        }
        sheets.push_back(s);
    }

    std::vector<ZipItem> parts;
    QString types = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>"
        "<Override PartName=\"/xl/workbook.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.sheet.main+xml\"/>"
        "<Override PartName=\"/xl/styles.xml\" "
        "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.styles+xml\"/>");
    QString book = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<workbook xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
        "<bookViews><workbookView activeTab=\"2\"/></bookViews><sheets>");
    QString bookRels = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
    for (size_t i = 0; i < sheets.size(); ++i) {
        const int n = int(i) + 1;
        parts.push_back({QStringLiteral("xl/worksheets/sheet%1.xml").arg(n), sheetXml(sheets[i]).toUtf8()});
        types += QStringLiteral("<Override PartName=\"/xl/worksheets/sheet%1.xml\" "
                                "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>")
                     .arg(n);
        book += QStringLiteral("<sheet name=\"%1\" sheetId=\"%2\" r:id=\"rId%2\"/>").arg(xmlText(sheets[i].name)).arg(n);
        bookRels += QStringLiteral("<Relationship Id=\"rId%1\" "
                                   "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/worksheet\" "
                                   "Target=\"worksheets/sheet%1.xml\"/>")
                        .arg(n);
    }
    types += QStringLiteral("</Types>");
    book += QStringLiteral("</sheets></workbook>");
    bookRels += QStringLiteral("<Relationship Id=\"rId%1\" "
                               "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" "
                               "Target=\"styles.xml\"/></Relationships>")
                    .arg(sheets.size() + 1);
    const QString rels = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
        "<Relationship Id=\"rId1\" "
        "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
        "Target=\"xl/workbook.xml\"/></Relationships>");
    parts.insert(parts.begin(), {{QStringLiteral("[Content_Types].xml"), types.toUtf8()},
                                 {QStringLiteral("_rels/.rels"), rels.toUtf8()},
                                 {QStringLiteral("xl/workbook.xml"), book.toUtf8()},
                                 {QStringLiteral("xl/_rels/workbook.xml.rels"), bookRels.toUtf8()},
                                 {QStringLiteral("xl/styles.xml"), stylesXml().toUtf8()}});
    return writeZip(parts);
}

// ---- The workbook, in -------------------------------------------------------------------------------------------

bool harnessDemandFromXlsx(const QByteArray& bytes, HarnessDemand& out, QString* error) {
    std::vector<std::pair<QString, ReadSheet>> sheets;
    if (!readWorkbook(bytes, sheets, error)) return false;
    out = HarnessDemand{};
    bool anyHeading = false;
    for (const auto& [name, sheet] : sheets) {
        if (name.compare(kInfo, Qt::CaseInsensitive) == 0) {
            for (const QStringList& row : sheet.rows) {
                if (row.size() < 2) continue;
                const int at = infoFields().indexOf(row[0].trimmed());
                if (QString* f = infoField(out.info, at)) *f = row[1];
            }
            continue;
        }
        for (const QStringList& row : sheet.rows)
            for (const QString& v : row)
                if (key(v) == QLatin1String("cablename")) anyHeading = true;
        readBlocks(sheet, out.cables);
    }
    if (!anyHeading) {
        if (error) *error = QStringLiteral("No pinout table was found - a demand has a heading row with Cable Name, "
                                           "Connector Name and the signals.");
        return false;
    }
    return true;
}

// ---- A cable as a definition ------------------------------------------------------------------------------------

HarnessDef harnessDefFromDemand(const DemandCable& cable, std::vector<DemandIssue>* issues) {
    HarnessDef def;
    const QString where = cable.name.isEmpty() ? QStringLiteral("Cable") : cable.name;
    auto issue = [&](int row, const QString& text) {
        if (issues) issues->push_back({where, row, text});
    };
    // WHAT EACH CELL SAYS: pins (numbers, or letters on a lettered connector), or a lead's colour.
    struct Place {
        int row = 0;                // which connector
        std::vector<int> pins;      // the pins there
        bool isLead = false;        // or a flying lead,
        QString lead;               // of this colour (a code)
    };
    const int nRows = int(cable.connectors.size());
    const int nSignals = int(cable.signalColumns.size());
    // A ROW IS LETTERED when every pin written on it is a single letter (a lead's colour aside).
    std::vector<bool> lettered(size_t(nRows), false);
    for (int r = 0; r < nRows; ++r) {
        bool any = false, letters = true;
        for (const QString& cell : cable.connectors[size_t(r)].cells) {
            if (!colourCode(firstLine(cell)).isEmpty() || key(cell) == QLatin1String("na") || firstLine(cell) == QLatin1String("-"))
                continue;
            for (const QString& part : cellParts(cell)) {
                any = true;
                if (!(part.size() == 1 && part[0].isLetter())) letters = false;
            }
        }
        lettered[size_t(r)] = any && letters;
    }
    std::vector<std::vector<Place>> placesOf = std::vector<std::vector<Place>>(size_t(nSignals));
    std::vector<int> highest(size_t(nRows), 0);
    std::vector<bool> hasPins(size_t(nRows), false);
    for (int k = 0; k < nSignals; ++k)
        for (int r = 0; r < nRows; ++r) {
            const DemandConnector& dc = cable.connectors[size_t(r)];
            const QString cell = k < dc.cells.size() ? firstLine(dc.cells[k]) : QString();
            if (cell.isEmpty() || key(cell) == QLatin1String("na") || cell == QLatin1String("-")) continue;
            Place p;
            p.row = r;
            const QString colour = colourCode(cell);
            if (!colour.isEmpty() && !lettered[size_t(r)]) {
                p.isLead = true;
                p.lead = colour;
            } else {
                for (const QString& part : cellParts(cell)) {
                    bool ok = false;
                    int pin = part.toInt(&ok);
                    if (!ok && part.size() == 1 && part[0].isLetter()) pin = part[0].toUpper().unicode() - u'A' + 1;
                    if (pin <= 0) {
                        issue(dc.sheetRow, QStringLiteral("%1, %2: \"%3\" is not a pin - left out")
                                               .arg(firstLine(dc.name), cable.signalColumns[size_t(k)].name, part));
                        continue;
                    }
                    p.pins.push_back(pin);
                    highest[size_t(r)] = std::max(highest[size_t(r)], pin);
                }
                if (p.pins.empty()) continue;
                hasPins[size_t(r)] = true;
            }
            placesOf[size_t(k)].push_back(p);
        }
    // THE CONNECTORS: the rows with pins. A part only of leads (a motor) is not a connector - its wires are flying
    // leads, each the colour written.
    std::vector<QString> idOf = std::vector<QString>(size_t(nRows));
    int right = 0;
    for (int r = 0; r < nRows; ++r) {
        if (!hasPins[size_t(r)]) continue;
        const DemandConnector& dc = cable.connectors[size_t(r)];
        HarnessConnector c;
        c.id = QStringLiteral("c%1").arg(def.connectors.size() + 1);
        c.designator = firstLine(dc.name).isEmpty() ? QStringLiteral("X%1").arg(r + 1) : firstLine(dc.name);
        c.partNumber = firstLine(dc.type);
        c.annotation = lettered[size_t(r)] ? QStringLiteral("letters") : QStringLiteral("numbers");
        c.pinCount = std::max(dc.maxPins, highest[size_t(r)]);
        if (dc.maxPins > 0 && highest[size_t(r)] > dc.maxPins)
            issue(dc.sheetRow, QStringLiteral("%1 has %2 pins but a signal lands on pin %3")
                                   .arg(c.designator)
                                   .arg(dc.maxPins)
                                   .arg(highest[size_t(r)]));
        // The first connector is the cable's main end; the rest are where it goes.
        if (def.connectors.empty()) {
            c.side = HarnessSide::Left;
            c.order = 0;
        } else {
            c.side = HarnessSide::Right;
            c.order = right++;
        }
        idOf[size_t(r)] = c.id;
        def.connectors.push_back(c);
    }
    // THE GROUPS, named: a twist, a shield, both, a coax.
    std::map<int, QString> groupName;
    int twists = 0, shields = 0, coaxes = 0;
    for (const DemandSignal& s : cable.signalColumns) {
        if (s.group.isEmpty() || groupName.count(s.groupId)) continue;
        if (s.group == QLatin1String("Twist")) groupName[s.groupId] = QStringLiteral("TP%1").arg(++twists);
        else if (s.group == QLatin1String("Shield")) groupName[s.groupId] = QStringLiteral("S%1").arg(++shields);
        else if (s.group == QLatin1String("Shield+Twist")) groupName[s.groupId] = QStringLiteral("STP%1").arg(++twists);
        else groupName[s.groupId] = QStringLiteral("COAX%1").arg(++coaxes);
    }
    std::map<int, int> coaxColumns;   // coax group -> its columns met so far
    // THE WIRES: from the signal's first place to each other one - two pins to two pins in their order, one pin to
    // several in turn.
    int wireNo = 0;
    for (int k = 0; k < nSignals; ++k) {
        const DemandSignal& sig = cable.signalColumns[size_t(k)];
        const std::vector<Place>& places = placesOf[size_t(k)];
        if (places.empty()) continue;
        const QString group = sig.group.isEmpty() ? QString() : groupName[sig.groupId];
        const bool braid = sig.group == QLatin1String("Coax") && coaxColumns[sig.groupId]++ >= 1;
        auto make = [&](const Place& a, int ai, const Place* b, int bi) {
            HarnessWire w;
            w.id = QStringLiteral("w%1").arg(++wireNo);
            w.label = sig.name;
            if (sig.group == QLatin1String("Twist") || sig.group == QLatin1String("Shield+Twist")) w.twistGroup = group;
            if (sig.group == QLatin1String("Shield") || sig.group == QLatin1String("Shield+Twist")) w.shieldGroup = group;
            if (sig.group == QLatin1String("Coax")) {
                w.kind = QStringLiteral("coax");
                w.bundle = group;
                if (braid) {
                    w.color = QStringLiteral("SHIELD");
                    w.label.clear();
                }
            }
            auto end = [&](const Place& p, int i, QString& conn, int& pin) {
                if (p.isLead) {
                    conn = harnessOpenEndpointKey();
                    pin = 0;
                    if (!braid) w.color = p.lead;
                } else {
                    conn = idOf[size_t(p.row)];
                    pin = p.pins[size_t(i) % p.pins.size()];
                }
            };
            end(a, ai, w.fromConn, w.fromPin);
            if (b) {
                end(*b, bi, w.toConn, w.toPin);
            } else {
                w.toConn = harnessOpenEndpointKey();
                w.toPin = 0;
            }
            def.wires.push_back(w);
        };
        const Place& first = places.front();
        if (places.size() == 1) {
            if (!first.isLead)
                issue(cable.connectors[size_t(first.row)].sheetRow,
                      QStringLiteral("%1 lands only on %2 - drawn as a flying lead")
                          .arg(sig.name, firstLine(cable.connectors[size_t(first.row)].name)));
            const int n = std::max<int>(1, int(first.pins.size()));
            for (int i = 0; i < n; ++i) make(first, i, nullptr, 0);
            continue;
        }
        for (size_t o = 1; o < places.size(); ++o) {
            const Place& other = places[o];
            const int n = std::max(std::max<int>(1, int(first.pins.size())), std::max<int>(1, int(other.pins.size())));
            for (int i = 0; i < n; ++i) make(first, i, &other, i);
        }
    }
    for (const auto& [id, count] : coaxColumns)
        if (count != 2)
            issue(0, QStringLiteral("Coax %1 has %2 column(s): a coax is its signal and its braid").arg(groupName[id]).arg(count));
    // AND WHAT THE BUILDER ITSELF WOULD SAY of it: a pin carrying two wires, a twist of one wire, and the rest.
    for (const HarnessIssue& h : validateHarness(def)) issue(0, h.message);
    return def;
}

DemandCable harnessDemandFromDef(const HarnessDef& def, const QString& name) {
    DemandCable cable;
    cable.name = name;
    // The connectors: the left end first, then the right, each in its order.
    std::vector<const HarnessConnector*> conns;
    for (const HarnessConnector& c : def.connectors) conns.push_back(&c);
    std::stable_sort(conns.begin(), conns.end(), [](const HarnessConnector* a, const HarnessConnector* b) {
        return a->side != b->side ? a->side == HarnessSide::Left : a->order < b->order;
    });
    QHash<QString, int> rowOf;
    for (const HarnessConnector* c : conns) {
        rowOf.insert(c->id, int(cable.connectors.size()));
        DemandConnector dc;
        dc.name = c->designator;
        dc.type = c->partNumber;
        dc.maxPins = c->pinCount;
        cable.connectors.push_back(dc);
    }
    // The signals, a column a wire: the members of a group side by side, a coax's signal before its braid.
    std::vector<const HarnessWire*> wires;
    for (const HarnessWire& w : def.wires)
        if (!harnessIsDrainRow(w) && !harnessIsShieldEndpoint(w.fromConn) && !harnessIsShieldEndpoint(w.toConn))
            wires.push_back(&w);
    auto groupKey = [](const HarnessWire* w) {
        if (harnessIsCoaxKind(w->kind)) return QStringLiteral("C:") + w->bundle;
        if (!w->twistGroup.isEmpty()) return QStringLiteral("T:") + w->twistGroup + QLatin1Char('/') + w->shieldGroup;
        if (!w->shieldGroup.isEmpty()) return QStringLiteral("S:") + w->shieldGroup;
        return QString();
    };
    auto isBraid = [](const HarnessWire* w) {
        return harnessIsCoaxKind(w->kind) && w->color.startsWith(QLatin1String("SHIELD"));
    };
    std::vector<const HarnessWire*> ordered;
    QSet<const HarnessWire*> taken;
    for (const HarnessWire* w : wires) {
        if (taken.contains(w)) continue;
        const QString g = groupKey(w);
        if (g.isEmpty()) {
            ordered.push_back(w);
            taken.insert(w);
            continue;
        }
        for (const bool braids : {false, true})
            for (const HarnessWire* o : wires)
                if (!taken.contains(o) && groupKey(o) == g && isBraid(o) == braids) {
                    ordered.push_back(o);
                    taken.insert(o);
                }
    }
    QHash<QString, int> groupIds;
    for (const HarnessWire* w : ordered) {
        DemandSignal sig;
        sig.name = !w->label.isEmpty() ? w->label : isBraid(w) ? QStringLiteral("SHIELD") : w->id;
        const QString g = groupKey(w);
        if (!g.isEmpty()) {
            sig.group = g.startsWith(QLatin1String("C:")) ? QStringLiteral("Coax")
                        : g.startsWith(QLatin1String("S:")) ? QStringLiteral("Shield")
                        : w->shieldGroup.isEmpty()          ? QStringLiteral("Twist")
                                                            : QStringLiteral("Shield+Twist");
            if (!groupIds.contains(g)) groupIds.insert(g, int(groupIds.size()) + 1);
            sig.groupId = groupIds.value(g);
        }
        const int k = int(cable.signalColumns.size());
        cable.signalColumns.push_back(sig);
        for (DemandConnector& dc : cable.connectors) dc.cells << QString();
        auto mark = [&](const QString& conn, int pin) {
            if (!rowOf.contains(conn) || pin <= 0) return;
            const HarnessConnector* c = harnessConnById(def, conn);
            QString& cell = cable.connectors[size_t(rowOf.value(conn))].cells[k];
            const QString p = c ? harnessPinLabel(*c, pin) : QString::number(pin);
            cell = cell.isEmpty() ? p : cell + QLatin1Char(',') + p;
        };
        mark(w->fromConn, w->fromPin);
        mark(w->toConn, w->toPin);
        // A flying lead's end is its colour, on a row of its own when it has no connector to sit on.
        if (harnessIsOpenEndpoint(w->toConn) || harnessIsOpenEndpoint(w->fromConn)) {
            int leads = -1;
            for (size_t r = 0; r < cable.connectors.size(); ++r)
                if (cable.connectors[r].name == QLatin1String("Leads")) leads = int(r);
            if (leads < 0) {
                DemandConnector dc;
                dc.name = QStringLiteral("Leads");
                for (int i = 0; i <= k; ++i) dc.cells << QString();
                cable.connectors.push_back(dc);
                leads = int(cable.connectors.size()) - 1;
            }
            cable.connectors[size_t(leads)].cells[k] = wireColorName(w->color);
        }
    }
    return cable;
}

// ---- The demand on paper ----------------------------------------------------------------------------------------

bool writeHarnessDemandPdf(const HarnessDemand& d, const QString& path, QString* error) {
    QPdfWriter pdf(path);
    pdf.setPageSize(QPageSize(QPageSize::A3));
    pdf.setPageOrientation(QPageLayout::Landscape);
    pdf.setPageMargins(QMarginsF(12, 12, 12, 12), QPageLayout::Millimeter);
    pdf.setResolution(150);
    pdf.setTitle(QStringLiteral("Harness demand - %1").arg(d.info.project));
    QPainter p;
    if (!p.begin(&pdf)) {
        if (error) *error = QStringLiteral("cannot write %1").arg(path);
        return false;
    }
    const QRectF page(QPointF(0, 0), QSizeF(pdf.width(), pdf.height()));
    const double mm = pdf.resolution() / 25.4;
    const QColor ink(20, 30, 45), rule(60, 60, 60), green(198, 239, 206), blue(155, 194, 230), light(221, 235, 247);
    QFont title(QStringLiteral("Arial"));
    title.setPixelSize(int(7 * mm));
    title.setBold(true);
    QFont small(QStringLiteral("Arial"));
    small.setPixelSize(int(2.8 * mm));
    QFont bold = small;
    bold.setBold(true);
    std::vector<DemandCable> cables = d.cables;
    if (cables.empty()) cables.push_back(DemandCable{});
    for (size_t pageNo = 0; pageNo < cables.size(); ++pageNo) {
        const DemandCable& cable = cables[pageNo];
        if (pageNo > 0) pdf.newPage();
        std::vector<DemandIssue> issues;
        const HarnessDef def = harnessDefFromDemand(cable, &issues);
        // THE TITLE AND THE DETAILS.
        p.setFont(title);
        p.setPen(ink);
        QString heading = cable.name.isEmpty() ? QStringLiteral("Harness demand") : cable.name;
        if (!cable.function.isEmpty()) heading += QStringLiteral(" - ") + cable.function;
        p.drawText(QRectF(page.left(), page.top(), page.width(), 10 * mm), Qt::AlignLeft | Qt::AlignVCenter, heading);
        p.setFont(small);
        const QStringList details{QStringLiteral("Customer: %1").arg(d.info.customer),
                                  QStringLiteral("Project: %1").arg(d.info.project),
                                  QStringLiteral("Harness P/N: %1").arg(d.info.partNumber),
                                  QStringLiteral("Revision: %1").arg(d.info.revision),
                                  QStringLiteral("Date: %1").arg(d.info.date),
                                  QStringLiteral("Contact: %1 %2").arg(d.info.contact, d.info.email).trimmed()};
        const double detailTop = page.top() + 11 * mm;
        for (int i = 0; i < details.size(); ++i)
            p.drawText(QRectF(page.left() + (i % 3) * page.width() / 3.0, detailTop + (i / 3) * 4.5 * mm,
                              page.width() / 3.0, 4.5 * mm),
                       Qt::AlignLeft | Qt::AlignVCenter, details[i]);
        double top = detailTop + 10 * mm;
        p.setPen(QPen(rule, 0.3 * mm));
        p.drawLine(QPointF(page.left(), top), QPointF(page.right(), top));
        top += 2 * mm;

        // THE PINOUT TABLE at the bottom, as the customer wrote it.
        const int nSig = int(cable.signalColumns.size());
        const double rowH = 8 * mm;   // two lines of a cell
        const int issueLines = issues.empty() ? 0 : std::min<int>(6, int(issues.size()) + 1);
        const double tableH = rowH * (2 + std::max<int>(1, int(cable.connectors.size())));
        const double tableTop = page.bottom() - tableH - rowH * issueLines;
        {
            const double fixedW[] = {28 * mm, 50 * mm, 14 * mm, 14 * mm};   // name, type, pins, length
            const double fixed = fixedW[0] + fixedW[1] + fixedW[2] + fixedW[3];
            const double sigW = nSig > 0 ? std::min(22 * mm, (page.width() - fixed - 30 * mm) / nSig) : 0.0;
            const double matingW = std::max(20 * mm, page.width() - fixed - sigW * nSig);
            auto box = [&](const QRectF& r, const QString& text, const QColor& fill, bool strong, bool red = false) {
                if (fill.isValid()) p.fillRect(r, fill);
                p.setPen(QPen(rule, 0.2 * mm));
                p.drawRect(r);
                p.setFont(strong ? bold : small);
                p.setPen(red ? QColor(220, 0, 0) : ink);
                p.drawText(r.adjusted(0.6 * mm, 0, -0.6 * mm, 0), Qt::AlignCenter | Qt::TextWordWrap, text);
            };
            double y = tableTop;
            double x = page.left() + fixed;
            for (int k = 0; k < nSig; ++k) {   // the group row
                const DemandSignal& s = cable.signalColumns[size_t(k)];
                if (s.group.isEmpty()) {
                    x += sigW;
                    continue;
                }
                int end = k;
                while (end + 1 < nSig && cable.signalColumns[size_t(end + 1)].groupId == s.groupId) ++end;
                box(QRectF(x, y, sigW * (end - k + 1), rowH), s.group, blue, true, s.group != QLatin1String("Twist"));
                x += sigW * (end - k + 1);
                k = end;
            }
            y += rowH;
            x = page.left();
            const QStringList fixedHeads{QStringLiteral("Connector Name"), QStringLiteral("Connector type"),
                                         QStringLiteral("Max pins"), QStringLiteral("Length")};
            for (int c = 0; c < 4; ++c) {
                box(QRectF(x, y, fixedW[c], rowH), fixedHeads[c], green, true);
                x += fixedW[c];
            }
            for (const DemandSignal& s : cable.signalColumns) {
                box(QRectF(x, y, sigW, rowH), s.name, light, true);
                x += sigW;
            }
            box(QRectF(x, y, matingW, rowH), kMating, green, true);
            y += rowH;
            for (const DemandConnector& dc : cable.connectors) {
                x = page.left();
                const QStringList fixedCells{dc.name.simplified(), dc.type.simplified(),
                                             dc.maxPins > 0 ? QString::number(dc.maxPins) : QStringLiteral("N/A"), dc.length};
                for (int c = 0; c < 4; ++c) {
                    box(QRectF(x, y, fixedW[c], rowH), fixedCells[c], QColor(), c == 0);
                    x += fixedW[c];
                }
                for (int k = 0; k < nSig; ++k) {
                    box(QRectF(x, y, sigW, rowH), k < dc.cells.size() ? dc.cells[k].simplified() : QString(), QColor(), false);
                    x += sigW;
                }
                box(QRectF(x, y, matingW, rowH), dc.mating.simplified(), QColor(), false);
                y += rowH;
            }
            // What could not be taken as written.
            if (!issues.empty()) {
                p.setFont(bold);
                p.setPen(QColor(170, 60, 30));
                p.drawText(QRectF(page.left(), y, page.width(), rowH), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("To check:"));
                y += rowH;
                p.setFont(small);
                for (size_t i = 0; i < issues.size() && i < 5; ++i) {
                    const QString at = issues[i].row > 0 ? QStringLiteral("row %1: ").arg(issues[i].row) : QString();
                    p.drawText(QRectF(page.left(), y, page.width(), rowH), Qt::AlignLeft | Qt::AlignVCenter, at + issues[i].text);
                    y += rowH;
                }
            }
        }

        // THE HARNESS AS THE BUILDER DRAWS IT, between the details and the table.
        const QRectF drawingArea(page.left(), top, page.width(), tableTop - top - 4 * mm);
        HarnessBuild build = buildHarness(def);
        Document doc;
        doc.entities().clear();
        int n = 0;
        Bounds bounds;
        for (Entity& e : build.entities) {
            e.id = QStringLiteral("demand%1").arg(++n);
            const Bounds b = e.bounds();
            if (b.valid) {
                bounds.expand(b.min);
                bounds.expand(b.max);
            }
            doc.entities().push_back(e);
        }
        if (bounds.valid && bounds.max.x > bounds.min.x && bounds.max.y > bounds.min.y && drawingArea.height() > 10 * mm) {
            const double scale = std::min(drawingArea.width() / (bounds.max.x - bounds.min.x),
                                          drawingArea.height() / (bounds.max.y - bounds.min.y)) * 0.94;
            ViewTransform vt;
            vt.view.scale = scale;
            const Vec2 mid = (bounds.min + bounds.max) * 0.5;
            vt.view.x = drawingArea.center().x() - mid.x * scale;
            vt.view.y = drawingArea.center().y() + mid.y * scale;
            p.save();
            p.setClipRect(drawingArea);
            p.setRenderHint(QPainter::Antialiasing, true);
            for (const Entity& e : doc.entities()) EntityRenderer::draw(p, e, doc, vt, ink);
            p.restore();
        }
    }
    p.end();
    return true;
}

} // namespace Loopwire
