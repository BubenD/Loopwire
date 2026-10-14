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
#include <QRegularExpression>
#include <QSet>
#include <QXmlStreamReader>

#include <algorithm>
#include <map>

namespace Loopwire {

namespace {

// ---- The sheets and their headings ------------------------------------------------------------------------------
// One list, read by the writer and the reader both: a heading written is a heading found.

const QString kInfo = QStringLiteral("Info");
const QString kConnectors = QStringLiteral("Connectors");
const QString kConnections = QStringLiteral("Connections");
const QString kGroups = QStringLiteral("Groups");
const QString kLists = QStringLiteral("Lists");

struct Column {
    QString heading;
    int width;            // in characters
    QString note;         // what goes in it, for the Instructions sheet
};

const std::vector<Column>& connectorColumns() {
    static const std::vector<Column> c{
        {QStringLiteral("Designator"), 12, QStringLiteral("The connector's name on the drawing: J1, P2, X3 ...")},
        {QStringLiteral("End"), 7, QStringLiteral("A or B - which end of the harness it is at")},
        {QStringLiteral("Description"), 30, QStringLiteral("What it is or what it plugs into")},
        {QStringLiteral("Mating part"), 24, QStringLiteral("The connector's part number or type, if known")},
        {QStringLiteral("Pins"), 7, QStringLiteral("How many pins it has (left empty: as many as are used)")},
        {QStringLiteral("Pin names"), 11, QStringLiteral("Numbers (1, 2, 3) or Letters (A, B, C)")},
        {QStringLiteral("Overall shield"), 14, QStringLiteral("Yes when one screen goes round everything at this connector")},
        {QStringLiteral("Shield drain to"), 15, QStringLiteral("Where that screen is terminated: None, Body (the shell) or Pin")},
        {QStringLiteral("Drain pin"), 10, QStringLiteral("The pin, when the drain goes to a pin")},
        {QStringLiteral("Notes"), 36, QString()},
    };
    return c;
}

const std::vector<Column>& connectionColumns() {
    static const std::vector<Column> c{
        {QStringLiteral("From"), 10, QStringLiteral("Connector at one end (empty: a flying lead)")},
        {QStringLiteral("From pin"), 9, QStringLiteral("Its pin")},
        {QStringLiteral("To"), 10, QStringLiteral("Connector at the other end (empty: a flying lead)")},
        {QStringLiteral("To pin"), 9, QStringLiteral("Its pin")},
        {QStringLiteral("Signal"), 18, QStringLiteral("What the wire carries: CAN_H, +24V, GND ...")},
        {QStringLiteral("Type"), 8, QStringLiteral("Wire or Coax")},
        {QStringLiteral("Twist group"), 12, QStringLiteral("Wires twisted together share a name here: TP1, TP2 ...")},
        {QStringLiteral("Shield group"), 12, QStringLiteral("Wires screened together share a name here: S1, S2 ...")},
        {QStringLiteral("Gauge"), 9, QStringLiteral("Cross-section in mm2 (0.5) or AWG (20 AWG); optional")},
        {QStringLiteral("Colour"), 10, QStringLiteral("Insulation colour (RD or Red); optional")},
        {QStringLiteral("Coax shield from pin"), 12, QStringLiteral("A coax: the pin its braid lands on at the From end")},
        {QStringLiteral("Coax shield to pin"), 12, QStringLiteral("A coax: the pin its braid lands on at the To end")},
        {QStringLiteral("Notes"), 36, QString()},
    };
    return c;
}

const std::vector<Column>& groupColumns() {
    static const std::vector<Column> c{
        {QStringLiteral("Group"), 10, QStringLiteral("A twist or shield group named on the Connections sheet")},
        {QStringLiteral("Kind"), 10, QStringLiteral("Twisted or Shielded")},
        {QStringLiteral("Drain connector"), 15, QStringLiteral("A shielded group: the connector its drain is taken to")},
        {QStringLiteral("Drain pin"), 10, QStringLiteral("And the pin")},
        {QStringLiteral("Notes"), 36, QString()},
    };
    return c;
}

const QStringList& infoFields() {
    static const QStringList f{QStringLiteral("Customer"),    QStringLiteral("Project"),
                               QStringLiteral("Harness name"), QStringLiteral("Harness P/N"),
                               QStringLiteral("Revision"),    QStringLiteral("Date"),
                               QStringLiteral("Contact"),     QStringLiteral("E-mail"),
                               QStringLiteral("Notes")};
    return f;
}

QString* infoField(DemandInfo& i, int at) {
    QString* f[] = {&i.customer, &i.project, &i.harnessName, &i.partNumber, &i.revision,
                    &i.date,     &i.contact, &i.email,       &i.notes};
    return at >= 0 && at < 9 ? f[at] : nullptr;
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

// The styles the cells are given, by number (see stylesXml).
enum Style { Plain = 0, Heading = 1, Body = 2, Title = 3, Note = 4, Label = 5 };

struct Cell {
    QString text;
    int style = Body;
    bool number = false;
};

struct Sheet {
    QString name;
    bool hidden = false;
    std::vector<int> widths;
    std::vector<std::vector<Cell>> rows;
    int freezeRows = 0;
    // Drop-downs: the cells they are on ("F2:F500") and what they offer - a list in quotes or a range.
    std::vector<std::pair<QString, QString>> lists;
};

QString sheetXml(const Sheet& s) {
    QString x = QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                               "<worksheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\" "
                               "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">");
    x += QStringLiteral("<sheetViews><sheetView workbookViewId=\"0\">");
    if (s.freezeRows > 0)
        x += QStringLiteral("<pane ySplit=\"%1\" topLeftCell=\"A%2\" activePane=\"bottomLeft\" state=\"frozen\"/>")
                 .arg(s.freezeRows)
                 .arg(s.freezeRows + 1);
    x += QStringLiteral("</sheetView></sheetViews>");
    if (!s.widths.empty()) {
        x += QStringLiteral("<cols>");
        for (size_t c = 0; c < s.widths.size(); ++c)
            x += QStringLiteral("<col min=\"%1\" max=\"%1\" width=\"%2\" customWidth=\"1\"/>").arg(c + 1).arg(s.widths[c]);
        x += QStringLiteral("</cols>");
    }
    x += QStringLiteral("<sheetData>");
    for (size_t r = 0; r < s.rows.size(); ++r) {
        x += QStringLiteral("<row r=\"%1\">").arg(r + 1);
        for (size_t c = 0; c < s.rows[r].size(); ++c) {
            const Cell& cell = s.rows[r][c];
            const QString ref = colName(int(c)) + QString::number(r + 1);
            if (cell.text.isEmpty()) {
                if (cell.style != Plain) x += QStringLiteral("<c r=\"%1\" s=\"%2\"/>").arg(ref).arg(cell.style);
                continue;
            }
            if (cell.number)
                x += QStringLiteral("<c r=\"%1\" s=\"%2\"><v>%3</v></c>").arg(ref).arg(cell.style).arg(cell.text);
            else
                x += QStringLiteral("<c r=\"%1\" s=\"%2\" t=\"inlineStr\"><is><t xml:space=\"preserve\">%3</t></is></c>")
                         .arg(ref)
                         .arg(cell.style)
                         .arg(xmlText(cell.text));
        }
        x += QStringLiteral("</row>");
    }
    x += QStringLiteral("</sheetData>");
    if (!s.lists.empty()) {
        x += QStringLiteral("<dataValidations count=\"%1\">").arg(s.lists.size());
        for (const auto& [range, offer] : s.lists)
            x += QStringLiteral("<dataValidation type=\"list\" allowBlank=\"1\" showErrorMessage=\"0\" sqref=\"%1\">"
                                "<formula1>%2</formula1></dataValidation>")
                     .arg(range, xmlText(offer));
        x += QStringLiteral("</dataValidations>");
    }
    x += QStringLiteral("<pageMargins left=\"0.5\" right=\"0.5\" top=\"0.6\" bottom=\"0.6\" header=\"0.3\" footer=\"0.3\"/>"
                        "<pageSetup orientation=\"landscape\"/></worksheet>");
    return x;
}

QString stylesXml() {
    return QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<styleSheet xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\">"
        "<fonts count=\"5\">"
        "<font><sz val=\"11\"/><name val=\"Calibri\"/></font>"
        "<font><b/><sz val=\"11\"/><color rgb=\"FFFFFFFF\"/><name val=\"Calibri\"/></font>"
        "<font><b/><sz val=\"16\"/><color rgb=\"FF1F3A5F\"/><name val=\"Calibri\"/></font>"
        "<font><i/><sz val=\"10\"/><color rgb=\"FF555555\"/><name val=\"Calibri\"/></font>"
        "<font><b/><sz val=\"11\"/><name val=\"Calibri\"/></font>"
        "</fonts>"
        "<fills count=\"3\">"
        "<fill><patternFill patternType=\"none\"/></fill>"
        "<fill><patternFill patternType=\"gray125\"/></fill>"
        "<fill><patternFill patternType=\"solid\"><fgColor rgb=\"FF2F5D8A\"/><bgColor indexed=\"64\"/></patternFill></fill>"
        "</fills>"
        "<borders count=\"2\">"
        "<border><left/><right/><top/><bottom/><diagonal/></border>"
        "<border><left style=\"thin\"><color rgb=\"FFB7C3D0\"/></left><right style=\"thin\"><color rgb=\"FFB7C3D0\"/></right>"
        "<top style=\"thin\"><color rgb=\"FFB7C3D0\"/></top><bottom style=\"thin\"><color rgb=\"FFB7C3D0\"/></bottom><diagonal/></border>"
        "</borders>"
        "<cellStyleXfs count=\"1\"><xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\"/></cellStyleXfs>"
        "<cellXfs count=\"6\">"
        "<xf numFmtId=\"0\" fontId=\"0\" fillId=\"0\" borderId=\"0\" xfId=\"0\"/>"
        "<xf numFmtId=\"0\" fontId=\"1\" fillId=\"2\" borderId=\"1\" xfId=\"0\" applyFont=\"1\" applyFill=\"1\" applyBorder=\"1\" applyAlignment=\"1\">"
        "<alignment vertical=\"center\" wrapText=\"1\"/></xf>"
        "<xf numFmtId=\"49\" fontId=\"0\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyNumberFormat=\"1\" applyBorder=\"1\"/>"
        "<xf numFmtId=\"0\" fontId=\"2\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\"/>"
        "<xf numFmtId=\"0\" fontId=\"3\" fillId=\"0\" borderId=\"0\" xfId=\"0\" applyFont=\"1\" applyAlignment=\"1\">"
        "<alignment vertical=\"top\" wrapText=\"1\"/></xf>"
        "<xf numFmtId=\"0\" fontId=\"4\" fillId=\"0\" borderId=\"1\" xfId=\"0\" applyFont=\"1\" applyBorder=\"1\"/>"
        "</cellXfs>"
        "<cellStyles count=\"1\"><cellStyle name=\"Normal\" xfId=\"0\" builtinId=\"0\"/></cellStyles>"
        "</styleSheet>");
}

// A table sheet: its headings, then `filled` rows, then empty bordered rows to write in.
Sheet tableSheet(const QString& name, const std::vector<Column>& cols, const std::vector<QStringList>& filled,
                 int blankRows) {
    Sheet s;
    s.name = name;
    s.freezeRows = 1;
    std::vector<Cell> head;
    for (const Column& c : cols) {
        s.widths.push_back(c.width);
        head.push_back({c.heading, Heading});
    }
    s.rows.push_back(head);
    for (const QStringList& r : filled) {
        std::vector<Cell> row;
        for (size_t c = 0; c < cols.size(); ++c) row.push_back({c < size_t(r.size()) ? r[int(c)] : QString(), Body});
        s.rows.push_back(row);
    }
    for (int k = 0; k < blankRows; ++k) s.rows.push_back(std::vector<Cell>(cols.size(), Cell{QString(), Body}));
    return s;
}

int columnOf(const std::vector<Column>& cols, const QString& heading) {
    for (size_t c = 0; c < cols.size(); ++c)
        if (cols[c].heading == heading) return int(c);
    return -1;
}

QString rangeOf(const std::vector<Column>& cols, const QString& heading, int lastRow) {
    const QString c = colName(columnOf(cols, heading));
    return c + QStringLiteral("2:") + c + QString::number(lastRow);
}

// ---- Reading the workbook ----------------------------------------------------------------------------------------

using Rows = std::vector<QStringList>;

int columnIndex(const QString& ref) {   // "BC12" -> 54
    int c = 0;
    for (const QChar ch : ref) {
        if (!ch.isLetter()) break;
        c = c * 26 + (ch.toUpper().unicode() - u'A' + 1);
    }
    return c - 1;
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

Rows sheetRows(const QByteArray& xml, const QStringList& shared) {
    Rows rows;
    QXmlStreamReader r(xml);
    int rowAt = -1;
    while (!r.atEnd()) {
        r.readNext();
        if (!r.isStartElement()) continue;
        if (r.name() == QLatin1String("row")) {
            const int n = r.attributes().value(QLatin1String("r")).toInt();
            rowAt = n > 0 ? n - 1 : rowAt + 1;
            if (int(rows.size()) <= rowAt) rows.resize(size_t(rowAt + 1));
        } else if (r.name() == QLatin1String("c") && rowAt >= 0) {
            const QString ref = r.attributes().value(QLatin1String("r")).toString();
            const QString type = r.attributes().value(QLatin1String("t")).toString();
            QStringList& row = rows[size_t(rowAt)];
            const int col = ref.isEmpty() ? int(row.size()) : columnIndex(ref);
            QString value;
            while (!r.atEnd()) {
                r.readNext();
                if (r.isEndElement() && r.name() == QLatin1String("c")) break;
                if (!r.isStartElement()) continue;
                if (r.name() == QLatin1String("v")) {
                    value = r.readElementText();
                } else if (r.name() == QLatin1String("t")) {
                    value += r.readElementText(QXmlStreamReader::IncludeChildElements);
                }
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
    return rows;
}

// The workbook's sheets by name (lower case), each its rows.
bool readWorkbook(const QByteArray& bytes, QHash<QString, Rows>& sheets, QString* error) {
    const std::vector<ZipItem> items = readZip(bytes);
    QHash<QString, QByteArray> files;
    for (const ZipItem& z : items) files.insert(z.name, z.data);
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
        const QString name = r.attributes().value(QLatin1String("name")).toString().trimmed().toLower();
        const QString file = targets.value(id);
        if (!file.isEmpty() && files.contains(file)) sheets.insert(name, sheetRows(files.value(file), shared));
    }
    if (!sheets.contains(kConnections.toLower()) || !sheets.contains(kConnectors.toLower())) {
        if (error) *error = QStringLiteral("The workbook has no Connectors and Connections sheets - is it a harness demand?");
        return false;
    }
    return true;
}

// A table's rows as heading -> value, each with the row number it was on. Rows with nothing in them are left out.
struct TableRow {
    int row = 0;
    QHash<QString, QString> v;
    QString at(const QString& heading) const { return v.value(heading.toLower()); }
};

std::vector<TableRow> tableRows(const Rows& rows) {
    std::vector<TableRow> out;
    if (rows.empty()) return out;
    const QStringList& head = rows.front();
    for (size_t r = 1; r < rows.size(); ++r) {
        TableRow t;
        t.row = int(r) + 1;
        bool any = false;
        for (int c = 0; c < head.size() && c < rows[r].size(); ++c) {
            if (rows[r][c].isEmpty()) continue;
            t.v.insert(head[c].trimmed().toLower(), rows[r][c]);
            any = true;
        }
        if (any) out.push_back(t);
    }
    return out;
}

bool yes(const QString& s) {
    const QString t = s.trimmed().toLower();
    return t == QLatin1String("yes") || t == QLatin1String("y") || t == QLatin1String("x") ||
           t == QLatin1String("true") || t == QLatin1String("1");
}

// ---- The lists the drop-downs offer -----------------------------------------------------------------------------

QStringList colourChoices() {
    QStringList out;
    for (const QString& code : wireColorCodes()) {
        if (code == QLatin1String("SHIELD") || code == QLatin1String("DRAIN") || code.startsWith(QLatin1String("SIGNAL")))
            continue;
        out << code;
    }
    return out;
}

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

// A gauge as written - 0.5, 0.5 mm2, 0,5 or 20 AWG - as the builder's mm2 figure. Empty when unknown.
QString gaugeCode(const QString& written) {
    QString w = written.trimmed().toLower();
    if (w.isEmpty()) return {};
    w.replace(QLatin1Char(','), QLatin1Char('.'));
    const bool awg = w.contains(QLatin1String("awg"));
    w.remove(QRegularExpression(QStringLiteral("[^0-9.]")));
    bool ok = false;
    const double n = w.toDouble(&ok);
    if (!ok) return {};
    for (const QString& mm2 : wireGaugeChoices()) {
        if (awg ? wireGaugeAwg(mm2).toDouble() == n : std::abs(mm2.toDouble() - n) < 1e-9) return mm2;
    }
    return awg ? QString() : QString::number(n);   // an odd cross-section is still a cross-section
}

} // namespace

// ---- The workbook, out ------------------------------------------------------------------------------------------

QByteArray harnessDemandToXlsx(const HarnessDemand& d) {
    constexpr int kLast = 500;   // how far down the drop-downs reach
    std::vector<Sheet> sheets;

    // INSTRUCTIONS: what each sheet is for, each column of it, and a worked example.
    {
        Sheet s;
        s.name = QStringLiteral("Instructions");
        s.widths = {26, 90};
        auto line = [&](const QString& a, const QString& b = QString(), int sa = Label, int sb = Note) {
            s.rows.push_back({Cell{a, a.isEmpty() ? Plain : sa}, Cell{b, b.isEmpty() ? Plain : sb}});
        };
        s.rows.push_back({Cell{QStringLiteral("Harness demand"), Title}});
        line(QString(), QStringLiteral("Fill in this workbook to describe the harness you need, then send it back. "
                                       "An overview is enough: which connector pin goes to which, and which wires "
                                       "are twisted, shielded or coax. Part numbers are welcome but not required."));
        line(QString());
        line(QStringLiteral("Info"), QStringLiteral("Who you are and which harness this is."));
        line(QStringLiteral("Connectors"), QStringLiteral("One row per connector. End A is drawn on the left, End B on "
                                                          "the right; a harness with branches puts several connectors "
                                                          "on one end."));
        for (const Column& c : connectorColumns())
            if (!c.note.isEmpty()) line(QStringLiteral("   ") + c.heading, c.note, Plain);
        line(QStringLiteral("Connections"), QStringLiteral("One row per wire. A coax is one row: its centre on the pins, "
                                                           "its braid on the two Coax shield pins."));
        for (const Column& c : connectionColumns())
            if (!c.note.isEmpty()) line(QStringLiteral("   ") + c.heading, c.note, Plain);
        line(QStringLiteral("Groups"), QStringLiteral("Optional: a shielded group's drain, and notes on any group."));
        for (const Column& c : groupColumns())
            if (!c.note.isEmpty()) line(QStringLiteral("   ") + c.heading, c.note, Plain);
        line(QString());
        line(QStringLiteral("Example"), QStringLiteral("J1 (End A, 4 pins) to P1 (End B, 4 pins), a shielded twisted "
                                                       "pair for CAN and two power wires:"));
        line(QString(), QStringLiteral("J1 pin 1 -> P1 pin 1, CAN_H, Twist group TP1, Shield group S1"));
        line(QString(), QStringLiteral("J1 pin 2 -> P1 pin 2, CAN_L, Twist group TP1, Shield group S1"));
        line(QString(), QStringLiteral("J1 pin 3 -> P1 pin 3, +24V, 1 mm2, Red"));
        line(QString(), QStringLiteral("J1 pin 4 -> P1 pin 4, GND, 1 mm2, Black"));
        line(QString(), QStringLiteral("Groups: S1, Shielded, drain to J1 pin 4"));
        sheets.push_back(s);
    }
    // INFO: field and value.
    {
        Sheet s;
        s.name = kInfo;
        s.widths = {18, 60};
        s.rows.push_back({Cell{QStringLiteral("Field"), Heading}, Cell{QStringLiteral("Value"), Heading}});
        DemandInfo info = d.info;
        for (int i = 0; i < infoFields().size(); ++i)
            s.rows.push_back({Cell{infoFields()[i], Label}, Cell{*infoField(info, i), Body}});
        sheets.push_back(s);
    }
    // CONNECTORS.
    {
        std::vector<QStringList> filled;
        for (const DemandConnector& c : d.connectors)
            filled.push_back({c.designator, c.end, c.description, c.matingPart, c.pins > 0 ? QString::number(c.pins) : QString(),
                              c.pinNames, c.overallShield ? QStringLiteral("Yes") : QStringLiteral("No"), c.drainTo,
                              c.drainPin, c.notes});
        Sheet s = tableSheet(kConnectors, connectorColumns(), filled, 30);
        const auto& cols = connectorColumns();
        s.lists = {{rangeOf(cols, QStringLiteral("End"), kLast), QStringLiteral("\"A,B\"")},
                   {rangeOf(cols, QStringLiteral("Pin names"), kLast), QStringLiteral("\"Numbers,Letters\"")},
                   {rangeOf(cols, QStringLiteral("Overall shield"), kLast), QStringLiteral("\"Yes,No\"")},
                   {rangeOf(cols, QStringLiteral("Shield drain to"), kLast), QStringLiteral("\"None,Body,Pin\"")}};
        sheets.push_back(s);
    }
    // CONNECTIONS.
    {
        std::vector<QStringList> filled;
        for (const DemandConnection& c : d.connections)
            filled.push_back({c.from, c.fromPin, c.to, c.toPin, c.signal, c.type, c.twistGroup, c.shieldGroup, c.gauge,
                              c.color, c.coaxShieldFromPin, c.coaxShieldToPin, c.notes});
        Sheet s = tableSheet(kConnections, connectionColumns(), filled, 100);
        const auto& cols = connectionColumns();
        const QString designators = QStringLiteral("Connectors!$A$2:$A$%1").arg(kLast);
        const int colours = int(colourChoices().size()), gauges = int(wireGaugeChoices().size());
        s.lists = {{rangeOf(cols, QStringLiteral("From"), kLast), designators},
                   {rangeOf(cols, QStringLiteral("To"), kLast), designators},
                   {rangeOf(cols, QStringLiteral("Type"), kLast), QStringLiteral("\"Wire,Coax\"")},
                   {rangeOf(cols, QStringLiteral("Gauge"), kLast), QStringLiteral("Lists!$B$2:$B$%1").arg(gauges + 1)},
                   {rangeOf(cols, QStringLiteral("Colour"), kLast), QStringLiteral("Lists!$A$2:$A$%1").arg(colours + 1)}};
        sheets.push_back(s);
    }
    // GROUPS.
    {
        std::vector<QStringList> filled;
        for (const DemandGroup& g : d.groups) filled.push_back({g.name, g.kind, g.drainConnector, g.drainPin, g.notes});
        Sheet s = tableSheet(kGroups, groupColumns(), filled, 20);
        const auto& cols = groupColumns();
        s.lists = {{rangeOf(cols, QStringLiteral("Kind"), kLast), QStringLiteral("\"Twisted,Shielded\"")},
                   {rangeOf(cols, QStringLiteral("Drain connector"), kLast),
                    QStringLiteral("Connectors!$A$2:$A$%1").arg(kLast)}};
        sheets.push_back(s);
    }
    // LISTS, out of sight: what the colour and gauge drop-downs offer.
    {
        Sheet s;
        s.name = kLists;
        s.hidden = true;
        s.widths = {12, 12};
        s.rows.push_back({Cell{QStringLiteral("Colour"), Heading}, Cell{QStringLiteral("Gauge mm2"), Heading}});
        const QStringList colours = colourChoices(), gauges = wireGaugeChoices();
        for (int i = 0; i < std::max(colours.size(), gauges.size()); ++i)
            s.rows.push_back({Cell{i < colours.size() ? colours[i] : QString(), Plain},
                              Cell{i < gauges.size() ? gauges[i] : QString(), Plain}});
        sheets.push_back(s);
    }

    // The package around them.
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
        "<bookViews><workbookView activeTab=\"0\"/></bookViews><sheets>");
    QString bookRels = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">");
    for (size_t i = 0; i < sheets.size(); ++i) {
        const int n = int(i) + 1;
        parts.push_back({QStringLiteral("xl/worksheets/sheet%1.xml").arg(n), sheetXml(sheets[i]).toUtf8()});
        types += QStringLiteral("<Override PartName=\"/xl/worksheets/sheet%1.xml\" "
                                "ContentType=\"application/vnd.openxmlformats-officedocument.spreadsheetml.worksheet+xml\"/>")
                     .arg(n);
        book += QStringLiteral("<sheet name=\"%1\" sheetId=\"%2\"%3 r:id=\"rId%2\"/>")
                    .arg(xmlText(sheets[i].name))
                    .arg(n)
                    .arg(sheets[i].hidden ? QStringLiteral(" state=\"hidden\"") : QString());
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
    QHash<QString, Rows> sheets;
    if (!readWorkbook(bytes, sheets, error)) return false;
    out = HarnessDemand{};
    // INFO: a field's value is beside it, wherever the row is.
    for (const QStringList& row : sheets.value(kInfo.toLower())) {
        if (row.size() < 2) continue;
        const int at = infoFields().indexOf(row[0].trimmed());
        if (QString* f = infoField(out.info, at)) *f = row[1];
    }
    for (const TableRow& r : tableRows(sheets.value(kConnectors.toLower()))) {
        DemandConnector c;
        c.designator = r.at(QStringLiteral("Designator"));
        c.end = r.at(QStringLiteral("End"));
        c.description = r.at(QStringLiteral("Description"));
        c.matingPart = r.at(QStringLiteral("Mating part"));
        c.pins = r.at(QStringLiteral("Pins")).toInt();
        c.pinNames = r.at(QStringLiteral("Pin names"));
        c.overallShield = yes(r.at(QStringLiteral("Overall shield")));
        c.drainTo = r.at(QStringLiteral("Shield drain to"));
        c.drainPin = r.at(QStringLiteral("Drain pin"));
        c.notes = r.at(QStringLiteral("Notes"));
        out.connectors.push_back(c);
    }
    for (const TableRow& r : tableRows(sheets.value(kConnections.toLower()))) {
        DemandConnection c;
        c.from = r.at(QStringLiteral("From"));
        c.fromPin = r.at(QStringLiteral("From pin"));
        c.to = r.at(QStringLiteral("To"));
        c.toPin = r.at(QStringLiteral("To pin"));
        c.signal = r.at(QStringLiteral("Signal"));
        c.type = r.at(QStringLiteral("Type"));
        c.twistGroup = r.at(QStringLiteral("Twist group"));
        c.shieldGroup = r.at(QStringLiteral("Shield group"));
        c.gauge = r.at(QStringLiteral("Gauge"));
        c.color = r.at(QStringLiteral("Colour"));
        if (c.color.isEmpty()) c.color = r.at(QStringLiteral("Color"));
        c.coaxShieldFromPin = r.at(QStringLiteral("Coax shield from pin"));
        c.coaxShieldToPin = r.at(QStringLiteral("Coax shield to pin"));
        c.notes = r.at(QStringLiteral("Notes"));
        out.connections.push_back(c);
    }
    for (const TableRow& r : tableRows(sheets.value(kGroups.toLower()))) {
        DemandGroup g;
        g.name = r.at(QStringLiteral("Group"));
        g.kind = r.at(QStringLiteral("Kind"));
        g.drainConnector = r.at(QStringLiteral("Drain connector"));
        g.drainPin = r.at(QStringLiteral("Drain pin"));
        g.notes = r.at(QStringLiteral("Notes"));
        out.groups.push_back(g);
    }
    return true;
}

// ---- A demand as a definition -----------------------------------------------------------------------------------

HarnessDef harnessDefFromDemand(const HarnessDemand& d, std::vector<DemandIssue>* issues) {
    HarnessDef def;
    auto issue = [&](const QString& sheet, int row, const QString& text) {
        if (issues) issues->push_back({sheet, row, text});
    };
    // The connectors, each on its end in the order listed. Rows are numbered as the sheet numbers them: the
    // headings are row 1, so the n-th connector listed is not necessarily on row n + 1 - an empty row is skipped
    // when read - but listed order is what is kept.
    QHash<QString, QString> idOf;   // designator, upper case -> connector id
    int orderLeft = 0, orderRight = 0;
    // The highest pin each connector's wires use, for a connector whose pin count was not given.
    QHash<QString, int> highest;
    std::vector<QString> rowId(d.connectors.size());   // the id each listed connector was given, "" when left out
    for (size_t i = 0; i < d.connectors.size(); ++i) {
        const DemandConnector& dc = d.connectors[i];
        const int row = int(i) + 2;
        const QString name = dc.designator.trimmed();
        if (name.isEmpty()) {
            issue(kConnectors, row, QStringLiteral("A connector with no designator is left out"));
            continue;
        }
        if (idOf.contains(name.toUpper())) {
            issue(kConnectors, row, QStringLiteral("%1 is listed twice; the first is kept").arg(name));
            continue;
        }
        HarnessConnector c;
        c.id = QStringLiteral("c%1").arg(def.connectors.size() + 1);
        rowId[i] = c.id;
        c.designator = name;
        c.partNumber = dc.matingPart;
        const QString end = dc.end.trimmed().toUpper();
        if (end == QLatin1String("B") || end == QLatin1String("RIGHT") || end == QLatin1String("2")) {
            c.side = HarnessSide::Right;
            c.order = orderRight++;
        } else {
            if (!end.isEmpty() && end != QLatin1String("A") && end != QLatin1String("LEFT") && end != QLatin1String("1"))
                issue(kConnectors, row, QStringLiteral("%1: End \"%2\" is not A or B - put on End A").arg(name, dc.end));
            c.side = HarnessSide::Left;
            c.order = orderLeft++;
        }
        c.annotation = dc.pinNames.trimmed().toLower().startsWith(QLatin1String("let")) ? QStringLiteral("letters")
                                                                                        : QStringLiteral("numbers");
        c.pinCount = dc.pins;
        if (c.side == HarnessSide::Left) c.shieldLeft = dc.overallShield;
        else c.shieldRight = dc.overallShield;
        idOf.insert(name.toUpper(), c.id);
        def.connectors.push_back(c);
    }
    auto connOf = [&](const QString& name) -> HarnessConnector* {
        const QString id = idOf.value(name.trimmed().toUpper());
        for (HarnessConnector& c : def.connectors)
            if (c.id == id) return &c;
        return nullptr;
    };
    // A pin as written - a number, or a letter on a lettered connector - as the connector's pin number. 0 when it
    // cannot be read; the caller says so.
    auto pinOf = [&](const HarnessConnector& c, const QString& written) {
        const QString w = written.trimmed();
        if (w.isEmpty()) return 0;
        bool ok = false;
        const int n = w.toInt(&ok);
        if (ok && n > 0 && c.annotation != QLatin1String("letters")) return n;
        const int upTo = std::max({c.pinCount, highest.value(c.id), 26});
        HarnessConnector probe = c;
        probe.pinCount = upTo;
        for (int p = 1; p <= upTo; ++p)
            if (harnessPinLabel(probe, p).compare(w, Qt::CaseInsensitive) == 0) return p;
        return ok && n > 0 ? n : 0;
    };
    // The connections. Gauges and colours written in any of the usual ways; groups by name.
    int coaxCount = 0;
    for (size_t i = 0; i < d.connections.size(); ++i) {
        const DemandConnection& dc = d.connections[i];
        const int row = int(i) + 2;
        HarnessWire w;
        w.id = QStringLiteral("w%1").arg(i + 1);
        w.label = dc.signal;
        w.twistGroup = dc.twistGroup.trimmed();
        w.shieldGroup = dc.shieldGroup.trimmed();
        auto end = [&](const QString& conn, const QString& pin, QString& outConn, int& outPin, const char* which) {
            if (conn.trimmed().isEmpty()) {
                outConn = harnessOpenEndpointKey();
                outPin = 0;
                return true;
            }
            HarnessConnector* c = connOf(conn);
            if (!c) {
                issue(kConnections, row, QStringLiteral("%1 connector \"%2\" is not on the Connectors sheet")
                                             .arg(QLatin1String(which), conn));
                return false;
            }
            outConn = c->id;
            outPin = pinOf(*c, pin);
            if (outPin <= 0)
                issue(kConnections, row, QStringLiteral("%1 pin \"%2\" of %3 cannot be read - left unlanded")
                                             .arg(QLatin1String(which), pin, c->designator));
            highest[c->id] = std::max(highest.value(c->id), outPin);
            return true;
        };
        if (!end(dc.from, dc.fromPin, w.fromConn, w.fromPin, "From") || !end(dc.to, dc.toPin, w.toConn, w.toPin, "To"))
            continue;
        if (!dc.gauge.trimmed().isEmpty()) {
            const QString g = gaugeCode(dc.gauge);
            if (g.isEmpty()) issue(kConnections, row, QStringLiteral("Gauge \"%1\" is not a size - 0.5 used").arg(dc.gauge));
            else w.gauge = g;
        }
        if (!dc.color.trimmed().isEmpty()) {
            const QString code = colourCode(dc.color);
            if (code.isEmpty()) issue(kConnections, row, QStringLiteral("Colour \"%1\" is not known - White used").arg(dc.color));
            else w.color = code;
        }
        const QString type = dc.type.trimmed().toLower();
        if (type.startsWith(QLatin1String("coax"))) {
            // A COAX IS TWO ROWS of one bundle: the signal as written, and its braid between the two shield pins.
            w.kind = QStringLiteral("coax");
            w.bundle = QStringLiteral("COAX%1").arg(++coaxCount);
            HarnessWire braid = w;
            braid.id = w.id + QStringLiteral("s");
            braid.color = QStringLiteral("SHIELD");
            braid.label.clear();
            braid.twistGroup.clear();
            if (const HarnessConnector* c = harnessConnById(def, w.fromConn))
                braid.fromPin = pinOf(*c, dc.coaxShieldFromPin);
            if (const HarnessConnector* c = harnessConnById(def, w.toConn)) braid.toPin = pinOf(*c, dc.coaxShieldToPin);
            if ((harnessConnById(def, w.fromConn) && braid.fromPin <= 0) ||
                (harnessConnById(def, w.toConn) && braid.toPin <= 0))
                issue(kConnections, row, QStringLiteral("Coax %1: say which pins its braid lands on (Coax shield from / to "
                                                        "pin)").arg(dc.signal.isEmpty() ? w.bundle : dc.signal));
            highest[braid.fromConn] = std::max(highest.value(braid.fromConn), braid.fromPin);
            highest[braid.toConn] = std::max(highest.value(braid.toConn), braid.toPin);
            def.wires.push_back(w);
            def.wires.push_back(braid);
            continue;
        }
        if (!type.isEmpty() && type != QLatin1String("wire"))
            issue(kConnections, row, QStringLiteral("Type \"%1\" is not Wire or Coax - taken as a wire").arg(dc.type));
        def.wires.push_back(w);
    }
    // A CONNECTOR WITH NO PIN COUNT has as many as its wires reach; one with fewer than they reach is told so.
    for (size_t i = 0; i < def.connectors.size(); ++i) {
        HarnessConnector& c = def.connectors[i];
        const int used = highest.value(c.id);
        if (c.pinCount <= 0) {
            c.pinCount = std::max(1, used);
        } else if (used > c.pinCount) {
            issue(kConnectors, 0, QStringLiteral("%1 has %2 pins but a wire goes to pin %3 - given %3")
                                      .arg(c.designator).arg(c.pinCount).arg(used));
            c.pinCount = used;
        }
    }
    // THE OVERALL SHIELDS' DRAINS, now that the pins are known.
    for (size_t i = 0; i < d.connectors.size(); ++i) {
        const DemandConnector& dc = d.connectors[i];
        HarnessConnector* c = connOf(dc.designator);
        if (!c || c->id != rowId[i]) continue;
        const QString to = dc.drainTo.trimmed().toLower();
        if (!dc.overallShield) {
            if (!to.isEmpty() && to != QLatin1String("none"))
                issue(kConnectors, int(i) + 2, QStringLiteral("%1 has a shield drain but no overall shield").arg(c->designator));
            continue;
        }
        if (to == QLatin1String("body")) {
            c->drainTo = QStringLiteral("body");
        } else if (to == QLatin1String("pin")) {
            const int pin = pinOf(*c, dc.drainPin);
            if (pin > 0) c->drainTo = QStringLiteral("pin:%1:%2").arg(c->id).arg(pin);
            else issue(kConnectors, int(i) + 2, QStringLiteral("%1: the drain pin \"%2\" cannot be read").arg(c->designator, dc.drainPin));
        }
    }
    harnessSyncDrains(def);
    // THE GROUPS: a shielded group's drain is a wire from the pin named to the group's screen.
    QSet<QString> twistNames, shieldNames;
    for (const HarnessWire& w : def.wires) {
        if (!w.twistGroup.isEmpty()) twistNames.insert(w.twistGroup.toUpper());
        if (!w.shieldGroup.isEmpty()) shieldNames.insert(w.shieldGroup.toUpper());
    }
    for (size_t i = 0; i < d.groups.size(); ++i) {
        const DemandGroup& g = d.groups[i];
        const int row = int(i) + 2;
        const QString name = g.name.trimmed();
        if (name.isEmpty()) continue;
        const bool shielded = g.kind.trimmed().toLower().startsWith(QLatin1String("shield"));
        if (!twistNames.contains(name.toUpper()) && !shieldNames.contains(name.toUpper())) {
            issue(kGroups, row, QStringLiteral("Group %1 is named by no connection").arg(name));
            continue;
        }
        if (g.drainConnector.trimmed().isEmpty()) continue;
        if (!shielded || !shieldNames.contains(name.toUpper())) {
            issue(kGroups, row, QStringLiteral("Group %1 has a drain but is not a shield group").arg(name));
            continue;
        }
        HarnessConnector* c = connOf(g.drainConnector);
        if (!c) {
            issue(kGroups, row, QStringLiteral("Drain connector \"%1\" is not on the Connectors sheet").arg(g.drainConnector));
            continue;
        }
        QString group = name;   // as the wires spell it
        for (const HarnessWire& w : def.wires)
            if (w.shieldGroup.compare(name, Qt::CaseInsensitive) == 0) group = w.shieldGroup;
        HarnessWire drain;
        drain.id = QStringLiteral("g%1").arg(i + 1);
        drain.fromConn = c->id;
        drain.fromPin = pinOf(*c, g.drainPin);
        drain.toConn = harnessInternalShieldKey(group);
        drain.toPin = 0;
        drain.color = QStringLiteral("DRAIN");
        if (drain.fromPin <= 0)
            issue(kGroups, row, QStringLiteral("Group %1: the drain pin \"%2\" cannot be read").arg(name, g.drainPin));
        def.wires.push_back(drain);
    }
    // AND WHAT THE BUILDER ITSELF WOULD SAY of it: a pin carrying two wires, a twist of one wire, and the rest.
    for (const HarnessIssue& h : validateHarness(def)) issue(QString(), 0, h.message);
    return def;
}

HarnessDemand harnessDemandFromDef(const HarnessDef& def) {
    HarnessDemand d;
    auto nameOf = [&](const QString& id) {
        const HarnessConnector* c = harnessConnById(def, id);
        return c ? c->designator : QString();
    };
    auto pinName = [&](const QString& id, int pin) {
        const HarnessConnector* c = harnessConnById(def, id);
        return c && pin > 0 ? harnessPinLabel(*c, pin) : QString();
    };
    // On each end in its order.
    std::vector<const HarnessConnector*> conns;
    for (const HarnessConnector& c : def.connectors) conns.push_back(&c);
    std::stable_sort(conns.begin(), conns.end(), [](const HarnessConnector* a, const HarnessConnector* b) {
        return a->side != b->side ? a->side == HarnessSide::Left : a->order < b->order;
    });
    for (const HarnessConnector* c : conns) {
        DemandConnector dc;
        dc.designator = c->designator;
        dc.end = c->side == HarnessSide::Left ? QStringLiteral("A") : QStringLiteral("B");
        dc.matingPart = c->partNumber;
        dc.pins = c->pinCount;
        dc.pinNames = c->annotation == QLatin1String("letters") ? QStringLiteral("Letters") : QStringLiteral("Numbers");
        dc.overallShield = c->side == HarnessSide::Left ? c->shieldLeft : c->shieldRight;
        if (dc.overallShield) {
            if (c->drainTo == QLatin1String("body")) {
                dc.drainTo = QStringLiteral("Body");
            } else if (c->drainTo.startsWith(QLatin1String("pin:"))) {
                dc.drainTo = QStringLiteral("Pin");
                const QStringList parts = c->drainTo.split(QLatin1Char(':'));
                if (parts.size() == 3) dc.drainPin = pinName(parts[1], parts[2].toInt());
            } else {
                dc.drainTo = QStringLiteral("None");
            }
        }
        d.connectors.push_back(dc);
    }
    // The connections: a coax's two rows as one, the drains made by the connectors left to them.
    QSet<QString> done;
    std::map<QString, QString> groupDrain;   // shield group -> "connector id|pin"
    for (const HarnessWire& w : def.wires) {
        if (harnessIsDrainRow(w)) continue;
        if (harnessIsShieldEndpoint(w.toConn) || harnessIsShieldEndpoint(w.fromConn)) {
            const bool toShield = harnessIsShieldEndpoint(w.toConn);
            const QString group = harnessShieldEndpointGroup(toShield ? w.toConn : w.fromConn);
            if (!group.isEmpty())
                groupDrain[group] = (toShield ? w.fromConn : w.toConn) + QLatin1Char('|') +
                                    QString::number(toShield ? w.fromPin : w.toPin);
            continue;
        }
        if (harnessIsCoaxKind(w.kind) && w.color.startsWith(QLatin1String("SHIELD"))) continue;   // with its signal
        if (done.contains(w.id)) continue;
        DemandConnection c;
        c.from = harnessIsOpenEndpoint(w.fromConn) ? QString() : nameOf(w.fromConn);
        c.fromPin = pinName(w.fromConn, w.fromPin);
        c.to = harnessIsOpenEndpoint(w.toConn) ? QString() : nameOf(w.toConn);
        c.toPin = pinName(w.toConn, w.toPin);
        c.signal = w.label;
        c.type = harnessIsCoaxKind(w.kind) ? QStringLiteral("Coax") : QStringLiteral("Wire");
        c.twistGroup = w.twistGroup;
        c.shieldGroup = w.shieldGroup;
        c.gauge = w.gauge;
        c.color = w.color;
        if (harnessIsCoaxKind(w.kind))
            for (const HarnessWire& o : def.wires)
                if (o.bundle == w.bundle && harnessIsCoaxKind(o.kind) && o.color.startsWith(QLatin1String("SHIELD"))) {
                    c.coaxShieldFromPin = pinName(o.fromConn, o.fromPin);
                    c.coaxShieldToPin = pinName(o.toConn, o.toPin);
                    break;
                }
        d.connections.push_back(c);
    }
    // The groups: each twisted and shielded one named, with a shield's drain.
    QStringList twisted, shielded;
    for (const HarnessWire& w : def.wires) {
        if (!w.twistGroup.isEmpty() && !twisted.contains(w.twistGroup)) twisted << w.twistGroup;
        if (!w.shieldGroup.isEmpty() && !shielded.contains(w.shieldGroup)) shielded << w.shieldGroup;
    }
    for (const QString& g : twisted) d.groups.push_back({g, QStringLiteral("Twisted"), {}, {}, {}});
    for (const QString& g : shielded) {
        DemandGroup group{g, QStringLiteral("Shielded"), {}, {}, {}};
        if (const auto it = groupDrain.find(g); it != groupDrain.end()) {
            const QStringList parts = it->second.split(QLatin1Char('|'));
            group.drainConnector = nameOf(parts.value(0));
            group.drainPin = pinName(parts.value(0), parts.value(1).toInt());
        }
        d.groups.push_back(group);
    }
    return d;
}

// ---- The demand on paper ----------------------------------------------------------------------------------------

bool writeHarnessDemandPdf(const HarnessDemand& d, const QString& path, QString* error) {
    std::vector<DemandIssue> issues;
    const HarnessDef def = harnessDefFromDemand(d, &issues);
    QPdfWriter pdf(path);
    pdf.setPageSize(QPageSize(QPageSize::A3));
    pdf.setPageOrientation(QPageLayout::Landscape);
    pdf.setPageMargins(QMarginsF(12, 12, 12, 12), QPageLayout::Millimeter);
    pdf.setResolution(150);
    pdf.setTitle(QStringLiteral("Harness demand - %1").arg(d.info.harnessName));
    QPainter p;
    if (!p.begin(&pdf)) {
        if (error) *error = QStringLiteral("cannot write %1").arg(path);
        return false;
    }
    const QRectF page(QPointF(0, 0), QSizeF(pdf.width(), pdf.height()));
    const double mm = pdf.resolution() / 25.4;
    const QColor ink(20, 30, 45), rule(150, 165, 185), head(47, 93, 138);

    // THE TITLE AND THE DETAILS, across the top.
    QFont title(QStringLiteral("Arial"));
    title.setPixelSize(int(7 * mm));
    title.setBold(true);
    p.setFont(title);
    p.setPen(ink);
    const QString heading = d.info.harnessName.isEmpty() ? QStringLiteral("Harness demand")
                                                         : QStringLiteral("Harness demand - %1").arg(d.info.harnessName);
    p.drawText(QRectF(page.left(), page.top(), page.width(), 10 * mm), Qt::AlignLeft | Qt::AlignVCenter, heading);
    QFont small(QStringLiteral("Arial"));
    small.setPixelSize(int(3.2 * mm));
    p.setFont(small);
    const QStringList details{
        QStringLiteral("Customer: %1").arg(d.info.customer), QStringLiteral("Project: %1").arg(d.info.project),
        QStringLiteral("Harness P/N: %1").arg(d.info.partNumber), QStringLiteral("Revision: %1").arg(d.info.revision),
        QStringLiteral("Date: %1").arg(d.info.date),
        QStringLiteral("Contact: %1 %2").arg(d.info.contact, d.info.email).trimmed()};
    const double detailTop = page.top() + 11 * mm;
    for (int i = 0; i < details.size(); ++i)
        p.drawText(QRectF(page.left() + (i % 3) * page.width() / 3.0, detailTop + (i / 3) * 5 * mm, page.width() / 3.0,
                          5 * mm),
                   Qt::AlignLeft | Qt::AlignVCenter, details[i]);
    double top = detailTop + 11 * mm;
    if (!d.info.notes.isEmpty()) {
        const QRectF notes(page.left(), top, page.width(), 10 * mm);
        p.drawText(notes, Qt::AlignLeft | Qt::TextWordWrap, QStringLiteral("Notes: %1").arg(d.info.notes));
        top += 10 * mm;
    }
    p.setPen(QPen(rule, 0.3 * mm));
    p.drawLine(QPointF(page.left(), top), QPointF(page.right(), top));
    top += 3 * mm;

    // THE HARNESS AS THE BUILDER DRAWS IT, in the left part of the page.
    const double tableWidth = page.width() * 0.36;
    const QRectF drawingArea(page.left(), top, page.width() - tableWidth - 6 * mm, page.bottom() - top);
    {
        HarnessBuild build = buildHarness(def);
        Document doc;
        doc.entities().clear();
        int n = 0;
        Bounds box;
        for (Entity& e : build.entities) {
            e.id = QStringLiteral("demand%1").arg(++n);
            const Bounds b = e.bounds();
            if (b.valid) {
                box.expand(b.min);
                box.expand(b.max);
            }
            doc.entities().push_back(e);
        }
        if (box.valid && box.max.x > box.min.x && box.max.y > box.min.y) {
            const double scale = std::min(drawingArea.width() / (box.max.x - box.min.x),
                                          drawingArea.height() / (box.max.y - box.min.y)) * 0.94;
            ViewTransform vt;
            vt.view.scale = scale;
            const Vec2 mid = (box.min + box.max) * 0.5;
            vt.view.x = drawingArea.center().x() - mid.x * scale;
            vt.view.y = drawingArea.center().y() + mid.y * scale;
            p.save();
            p.setClipRect(drawingArea);
            p.setRenderHint(QPainter::Antialiasing, true);
            for (const Entity& e : doc.entities()) EntityRenderer::draw(p, e, doc, vt, ink);
            p.restore();
        }
    }

    // EACH CONNECTOR'S PINOUT, down the right: pin, signal, where it goes and what it is part of.
    {
        double y = top;
        const double x = page.right() - tableWidth;
        const double rowH = 4.6 * mm;
        const double cols[] = {0.12, 0.30, 0.30, 0.28};   // pin, signal, to, group / type
        QFont bold = small;
        bold.setBold(true);
        auto cell = [&](double& cx, int c, const QString& text, bool header) {
            const QRectF r(cx, y, tableWidth * cols[c], rowH);
            if (header) p.fillRect(r, head);
            p.setPen(QPen(rule, 0.2 * mm));
            p.drawRect(r);
            p.setPen(header ? Qt::white : ink);
            p.drawText(r.adjusted(1 * mm, 0, -1 * mm, 0), Qt::AlignLeft | Qt::AlignVCenter,
                       p.fontMetrics().elidedText(text, Qt::ElideRight, int(r.width() - 2 * mm)));
            cx += r.width();
        };
        for (const HarnessConnector& c : def.connectors) {
            if (y + 3 * rowH > page.bottom()) break;
            p.setFont(bold);
            p.setPen(ink);
            QString name = c.designator;
            for (const DemandConnector& dc : d.connectors)
                if (dc.designator.compare(c.designator, Qt::CaseInsensitive) == 0 && !dc.description.isEmpty())
                    name += QStringLiteral(" - ") + dc.description;
            p.drawText(QRectF(x, y, tableWidth, rowH), Qt::AlignLeft | Qt::AlignVCenter,
                       QStringLiteral("%1   (End %2, %3 pins%4)")
                           .arg(name, c.side == HarnessSide::Left ? QStringLiteral("A") : QStringLiteral("B"))
                           .arg(c.pinCount)
                           .arg(c.partNumber.isEmpty() ? QString() : QStringLiteral(", ") + c.partNumber));
            y += rowH;
            double cx = x;
            cell(cx, 0, QStringLiteral("Pin"), true);
            cell(cx, 1, QStringLiteral("Signal"), true);
            cell(cx, 2, QStringLiteral("To"), true);
            cell(cx, 3, QStringLiteral("Group / type"), true);
            y += rowH;
            p.setFont(small);
            for (const int pin : harnessPinSequence(c)) {
                if (y + rowH > page.bottom()) break;
                QString signal, to, group;
                for (const HarnessWire& w : def.wires) {
                    const bool here = (w.fromConn == c.id && w.fromPin == pin) || (w.toConn == c.id && w.toPin == pin);
                    if (!here) continue;
                    const bool atFrom = w.fromConn == c.id && w.fromPin == pin;
                    const QString otherConn = atFrom ? w.toConn : w.fromConn;
                    const int otherPin = atFrom ? w.toPin : w.fromPin;
                    signal = harnessIsCoaxKind(w.kind) && w.color.startsWith(QLatin1String("SHIELD"))
                                 ? QStringLiteral("coax braid")
                                 : (w.color == QLatin1String("DRAIN") ? QStringLiteral("drain") : w.label);
                    if (harnessIsOpenEndpoint(otherConn)) to = QStringLiteral("flying lead");
                    else if (harnessIsShieldEndpoint(otherConn)) to = harnessShieldEndpointLabel(def, otherConn);
                    else if (const HarnessConnector* o = harnessConnById(def, otherConn))
                        to = otherPin > 0 ? QStringLiteral("%1-%2").arg(o->designator, harnessPinLabel(*o, otherPin))
                                          : o->designator;
                    QStringList g;
                    if (!w.twistGroup.isEmpty()) g << QStringLiteral("twist %1").arg(w.twistGroup);
                    if (!w.shieldGroup.isEmpty()) g << QStringLiteral("shield %1").arg(w.shieldGroup);
                    if (harnessIsCoaxKind(w.kind)) g << w.bundle;
                    group = g.join(QStringLiteral(", "));
                    break;
                }
                double rx = x;
                cell(rx, 0, harnessPinLabel(c, pin), false);
                cell(rx, 1, signal, false);
                cell(rx, 2, to, false);
                cell(rx, 3, group, false);
                y += rowH;
            }
            y += rowH * 0.6;
        }
        // WHAT COULD NOT BE TAKEN AS WRITTEN, where there is room left for it.
        if (!issues.empty() && y + 2 * rowH < page.bottom()) {
            p.setFont(bold);
            p.setPen(QColor(170, 60, 30));
            p.drawText(QRectF(x, y, tableWidth, rowH), Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("To check"));
            y += rowH;
            p.setFont(small);
            for (const DemandIssue& i : issues) {
                if (y + rowH > page.bottom()) break;
                const QString where = i.sheet.isEmpty() ? QString()
                                      : i.row > 0     ? QStringLiteral("%1 row %2: ").arg(i.sheet).arg(i.row)
                                                      : i.sheet + QStringLiteral(": ");
                p.drawText(QRectF(x, y, tableWidth, rowH), Qt::AlignLeft | Qt::AlignVCenter,
                           p.fontMetrics().elidedText(where + i.text, Qt::ElideRight, int(tableWidth)));
                y += rowH;
            }
        }
    }
    p.end();
    return true;
}

} // namespace Loopwire
