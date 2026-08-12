#include "harness/PartDataFromWeb.h"

#include <QRegularExpression>

#include <cmath>

namespace Edraw_Desktop {

namespace {

// One thing worth reading off a page: which parameter it fills, and how it is written when it is there.
struct Pattern {
    const char* key;
    const char* label;
    const char* rx;        // the first capture is the value
    const char* suffix;    // appended to the value ("" for none) - units a field expects but a page omits
};

QString firstMatch(const QString& text, const QString& pattern, QString& evidence, int capture = 1) {
    const QRegularExpression rx(pattern, QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = rx.match(text);
    if (!m.hasMatch()) return QString();
    // The sentence it came from, so the reading can be judged rather than taken on trust: forty characters
    // either side of the match, tidied.
    const int at = m.capturedStart(0);
    const int from = std::max(0, at - 40);
    const int len = std::min(text.size() - from, m.capturedLength(0) + 80);
    evidence = text.mid(from, len).simplified();
    return m.captured(capture).trimmed();
}

// The patterns worth trying for each kind. Written as a catalogue writes them, and deliberately narrow: a
// pattern that matches loosely finds something on every page, and something wrong is worse than nothing.
std::vector<Pattern> patternsFor(PartKind kind) {
    switch (kind) {
    case PartKind::Wire:
        return {{"gauge", "Gauge", R"((\d{1,2})\s*(?:AWG|awg)\b)", ""},
                {"strands", "Stranding", R"(\b(\d{1,3}\s*/\s*\d{2})\b)", ""},
                {"od", "Outside diameter", R"((?:outside diameter|outer diameter|O\.?D\.?|nominal diameter)[^0-9]{0,20}([0-9]+\.?[0-9]*)\s*mm)", ""},
                {"voltage", "Voltage rating", R"(([0-9]{2,4})\s*V(?:olts)?\s*(?:RMS|rms|DC|dc)?\s*(?:rating|rated|max)?)", ""},
                {"temp", "Temperature range", R"((-\s?\d{2,3}\s*(?:°|deg(?:rees)?)?\s*C?\s*(?:to|\.\.\.|–|-)\s*\+?\s?\d{2,3}\s*(?:°|deg(?:rees)?)?\s*C))", ""},
                {"insulation", "Insulation", R"(\b(PTFE|ETFE|FEP|PFA|XLPE|PVC|Silicone|Polyolefin|Tefzel)\b)", ""},
                {"spec", "Specification", R"(\b((?:MIL-|SAE-|AMS-|EN\s?|BS\s?|UL\s?)[A-Z0-9\-/]{3,20})\b)", ""}};
    case PartKind::Heatshrink:
    case PartKind::HeatshrinkBoot:
        return {{"ratio", "Shrink ratio", R"(\b([2-6]\s*:\s*1)\b)", ""},
                {"entryMax", "Supplied inside diameter", R"((?:expanded|as supplied|internal diameter, expanded)[^0-9]{0,24}([0-9]+\.?[0-9]*)\s*mm)", ""},
                {"entryMin", "Recovered inside diameter", R"((?:recovered|after recovery)[^0-9]{0,24}([0-9]+\.?[0-9]*)\s*mm)", ""},
                {"wall", "Recovered wall", R"((?:recovered wall|wall thickness)[^0-9]{0,24}([0-9]+\.?[0-9]*)\s*mm)", ""},
                {"shrinkTemp", "Minimum shrink temperature", R"((?:minimum shrink|full recovery|shrink temperature)[^0-9]{0,24}([0-9]{2,3})\s*(?:°|deg)?\s*C)", ""},
                {"temp", "Temperature range", R"((-\s?\d{2,3}\s*(?:°|deg(?:rees)?)?\s*C?\s*(?:to|\.\.\.|–|-)\s*\+?\s?\d{2,3}\s*(?:°|deg(?:rees)?)?\s*C))", ""},
                {"material", "Material", R"(\b(Polyolefin|Elastomer|Fluoropolymer|PVDF|PTFE|Viton|Neoprene)\b)", ""},
                {"spec", "Specification", R"(\b((?:MIL-|SAE-|AMS-|UL\s?)[A-Z0-9\-/]{3,20})\b)", ""}};
    case PartKind::Connector:
        return {{"pins", "Pins", R"((\d{1,3})\s*(?:-|\s)?(?:way|ways|position|positions|contact|contacts|pin|pins|circuit|circuits)\b)", ""},
                {"pitch", "Pitch", R"((?:pitch|spacing|grid)[^0-9]{0,16}([0-9]+\.?[0-9]*)\s*mm)", ""},
                {"gender", "Gender", R"(\b(plug|receptacle|socket|male|female)\b)", ""},
                {"shellSize", "Shell size", R"((?:shell size)[^0-9A-Za-z]{0,10}([0-9]{1,2}[A-Z]?)\b)", ""},
                {"plating", "Plating", R"(\b(gold|tin|silver|nickel)[- ]?plat)", ""},
                {"temp", "Temperature range", R"((-\s?\d{2,3}\s*(?:°|deg(?:rees)?)?\s*C?\s*(?:to|\.\.\.|–|-)\s*\+?\s?\d{2,3}\s*(?:°|deg(?:rees)?)?\s*C))", ""},
                {"spec", "Specification", R"(\b((?:MIL-DTL-|MIL-C-|EN\s?|AS\s?)[A-Z0-9\-/]{3,20})\b)", ""}};
    case PartKind::ContactPin:
    case PartKind::Crimp:
        return {{"gaugeMin", "Smallest gauge", R"((\d{1,2})\s*(?:-|to|–)\s*\d{1,2}\s*AWG)", ""},
                {"gaugeMax", "Largest gauge", R"(\d{1,2}\s*(?:-|to|–)\s*(\d{1,2})\s*AWG)", ""},
                // What the contact will carry. A datasheet writes it as "current rating 7.5 A" or as amps
                // against the contact size; the number is taken, because the field is in amps and says so.
                {"current", "Current rating",
                 R"((?:current rating|rated current|current)[^0-9]{0,16}([0-9]+\.?[0-9]*)\s*(?:A\b|amp))",
                 ""},
                {"plating", "Plating", R"(\b(gold|tin|silver|nickel)[- ]?plat)", ""},
                {"spec", "Specification", R"(\b((?:MIL-|SAE-|AMS-)[A-Z0-9\-/]{3,20})\b)", ""}};
    case PartKind::Cable:
        return {{"cores", "Cores", R"((\d{1,3})\s*(?:core|cores|conductor|conductors|pair|pairs)\b)", ""},
                {"od", "Outside diameter", R"((?:outside diameter|outer diameter|O\.?D\.?)[^0-9]{0,20}([0-9]+\.?[0-9]*)\s*mm)", ""},
                {"gauge", "Gauge", R"((\d{1,2})\s*(?:AWG|awg)\b)", ""},
                {"jacket", "Jacket", R"(\b(PTFE|ETFE|FEP|PVC|Polyurethane|PUR|Silicone|XLPE)\b)", ""},
                {"temp", "Temperature range", R"((-\s?\d{2,3}\s*(?:°|deg(?:rees)?)?\s*C?\s*(?:to|\.\.\.|–|-)\s*\+?\s?\d{2,3}\s*(?:°|deg(?:rees)?)?\s*C))", ""},
                {"spec", "Specification", R"(\b((?:MIL-|SAE-|AMS-|EN\s?)[A-Z0-9\-/]{3,20})\b)", ""}};
    default:
        // Everything else gets the three that are worth having off any page.
        return {{"temp", "Temperature range", R"((-\s?\d{2,3}\s*(?:°|deg(?:rees)?)?\s*C?\s*(?:to|\.\.\.|–|-)\s*\+?\s?\d{2,3}\s*(?:°|deg(?:rees)?)?\s*C))", ""},
                {"material", "Material", R"(\b(Polyolefin|Elastomer|Fluoropolymer|PTFE|PVC|Nylon|Brass|Stainless steel|Aluminium|Aluminum)\b)", ""},
                {"spec", "Specification", R"(\b((?:MIL-|SAE-|AMS-|EN\s?|UL\s?)[A-Z0-9\-/]{3,20})\b)", ""}};
    }
}

// What a catalogue CALLS a thing, and which of our parameters it fills. This is where most of the reading
// happens on a real page: a distributor's product table is a list of "Attribute | Value" rows, and a
// datasheet is a list of "Label: value" lines. The patterns above find things written in a sentence; this
// finds them written in a table, which is how they are usually written.
struct LabelMap {
    const char* label;   // as the catalogue writes it, lower case, matched as a whole word
    const char* key;     // the parameter it fills
    const char* what;    // what to call it on the list
    bool numberOnly;     // take the first number out of the value (a size), rather than the words
};

const std::vector<LabelMap>& labelMap() {
    static const std::vector<LabelMap> map = {
        {"number of positions", "pins", "Pins", true},
        {"number of contacts", "pins", "Pins", true},
        {"number of ways", "pins", "Pins", true},
        {"positions", "pins", "Pins", true},
        {"contacts", "pins", "Pins", true},
        {"pitch", "pitch", "Pitch", true},
        {"contact pitch", "pitch", "Pitch", true},
        {"gender", "gender", "Gender", false},
        {"connector type", "type", "Type", false},
        {"shell size", "shellSize", "Shell size", false},
        {"contact plating", "plating", "Plating", false},
        {"plating", "plating", "Plating", false},
        {"contact material", "material", "Material", false},
        {"material", "material", "Material", false},
        {"insulation material", "insulation", "Insulation", false},
        {"insulation", "insulation", "Insulation", false},
        {"jacket material", "jacket", "Jacket", false},
        {"jacket", "jacket", "Jacket", false},
        {"wire gauge", "gauge", "Gauge", true},
        {"conductor size", "gauge", "Gauge", true},
        {"awg", "gauge", "Gauge", true},
        {"stranding", "strands", "Stranding", false},
        {"number of conductors", "cores", "Cores", true},
        {"conductors", "cores", "Cores", true},
        {"outside diameter", "od", "Outside diameter", true},
        {"outer diameter", "od", "Outside diameter", true},
        {"cable diameter", "od", "Outside diameter", true},
        {"voltage rating", "voltage", "Voltage rating", true},
        {"voltage", "voltage", "Voltage rating", true},
        {"current rating", "current", "Current rating", true},
        {"operating temperature", "temp", "Temperature range", false},
        {"temperature range", "temp", "Temperature range", false},
        {"shrink ratio", "ratio", "Shrink ratio", false},
        {"expanded diameter", "entryMax", "Supplied inside diameter", true},
        {"recovered diameter", "entryMin", "Recovered inside diameter", true},
        {"wall thickness", "wall", "Recovered wall", true},
        {"recovered wall", "wall", "Recovered wall", true},
        {"colour", "colour", "Colour", false},
        {"color", "colour", "Colour", false},
        {"standards", "spec", "Specification", false},
        {"specification", "spec", "Specification", false},
        {"approvals", "spec", "Specification", false},
    };
    return map;
}

// The first number in a value, with its decimals: "2.54 mm" -> "2.54", "19 way" -> "19".
QString firstNumber(const QString& value) {
    const QRegularExpression rx(QStringLiteral("([-+]?[0-9]+(?:\\.[0-9]+)?)"));
    const QRegularExpressionMatch m = rx.match(value);
    return m.hasMatch() ? m.captured(1) : QString();
}

// Every "label ... value" pair the text holds, as a catalogue table or a datasheet line. The separator is a
// colon, a tab, or two or more spaces - which is what a table flattened into text looks like.
std::vector<WebFieldFind> harvestLabelled(const QString& text, const LibraryPart& part) {
    std::vector<WebFieldFind> out;
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("[\\r\\n]")),
                                         Qt::SkipEmptyParts);
    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines.at(i).simplified();
        if (line.size() < 3 || line.size() > 200) continue;
        for (const LabelMap& m : labelMap()) {
            const QString label = QString::fromLatin1(m.label);
            if (!line.startsWith(label, Qt::CaseInsensitive)) continue;
            QString value = line.mid(label.size()).trimmed();
            // What separates the two on a flattened table row.
            while (!value.isEmpty() &&
                   (value.startsWith(QLatin1Char(':')) || value.startsWith(QLatin1Char('|')) ||
                    value.startsWith(QLatin1Char('=')) || value.startsWith(QLatin1Char('-')) ||
                    value.startsWith(QLatin1Char('\t'))))
                value = value.mid(1).trimmed();
            // A TABLE row flattens to the label on one line and its value on the next - which is what a
            // product page's technical table becomes once the tags are out. So when the label's own line has
            // nothing after it, the value is the next line that has something on it.
            if (value.isEmpty())
                for (int j = i + 1; j < lines.size() && j <= i + 2; ++j) {
                    const QString next = lines.at(j).simplified();
                    if (next.isEmpty()) continue;
                    if (next.size() <= 40) value = next;
                    break;
                }
            if (value.isEmpty()) continue;
            if (m.numberOnly) {
                value = firstNumber(value);
            } else {
                // The value, and not the rest of the sentence it was written in: "Shrink ratio 3:1. As
                // supplied 12.0 mm" gives 3:1, and "Shell size 15, pitch 2.54 mm" gives 15. A value is a
                // word or two; anything past the first comma or full stop is the next fact along.
                const int cut = [&] {
                    int at = value.size();
                    for (const QChar ch : {QLatin1Char(','), QLatin1Char(';'), QLatin1Char('.')}) {
                        const int found = value.indexOf(ch);
                        // A decimal point is part of the number, not the end of the value.
                        if (found > 0 && ch == QLatin1Char('.') && found + 1 < value.size() &&
                            value.at(found + 1).isDigit())
                            continue;
                        if (found > 0 && found < at) at = found;
                    }
                    return at;
                }();
                value = value.left(cut).trimmed();
            }
            if (value.isEmpty() || value.size() > 40) continue;
            // One find per key: the first time a label is met is the one nearest the top of the page, which
            // is the part's own table rather than a list of related products further down.
            bool already = false;
            for (const WebFieldFind& f : out)
                if (f.key == QString::fromLatin1(m.key)) already = true;
            if (already) break;
            WebFieldFind f;
            f.key = QString::fromLatin1(m.key);
            f.label = QString::fromLatin1(m.what);
            f.value = value;
            f.evidence = line;
            f.exists = !part.params.value(f.key).isEmpty();
            out.push_back(f);
            break;
        }
    }
    return out;
}

} // namespace

bool pageWasRefused(const QString& html, int httpStatus, QString* why) {
    const QString text = htmlToText(html);
    auto say = [&](const QString& what) {
        if (why) *why = what;
        return true;
    };
    if (httpStatus >= 400)
        return say(QStringLiteral("The site answered %1 — it refused the request. Distributor sites serve "
                                  "that to anything that is not a browser.")
                       .arg(httpStatus));
    for (const char* mark : {"Access to this page has been denied", "Access Denied",
                             "unusual traffic", "are you a robot", "captcha", "cf-browser-verification",
                             "Powered and protected by", "Checking your browser",
                             "enable JavaScript to continue"})
        if (text.contains(QString::fromLatin1(mark), Qt::CaseInsensitive) ||
            html.contains(QString::fromLatin1(mark), Qt::CaseInsensitive))
            return say(QStringLiteral("The site served a bot check rather than the page (\"%1\"). It will "
                                      "not be read by a program.")
                           .arg(QString::fromLatin1(mark)));
    // A page of almost no text at all is a shim, whatever it says: a product page is thousands of characters.
    if (text.size() < 400)
        return say(QStringLiteral("The site sent %1 characters of text — a holding page, not the product "
                                  "page. It will not be read by a program.")
                       .arg(text.size()));
    return false;
}

std::vector<WebFieldFind> partFieldsFromText(const QString& text, const LibraryPart& part,
                                             const QString& source) {
    std::vector<WebFieldFind> finds = harvestLabelled(text, part);
    // And the patterns, for what is written in a sentence rather than in a table. A key already found in the
    // table wins: a table row is about this part, a sentence may be about anything on the page.
    LibraryPart asIf = part;
    for (const WebFieldFind& f : finds)
        if (f.key != QStringLiteral("__description")) asIf.params.insert(f.key, f.value);
    for (const WebFieldFind& f : partFieldsFromPage(QStringLiteral("<body>%1</body>").arg(text), asIf)) {
        if (f.key == QStringLiteral("__description")) continue;
        bool already = false;
        for (const WebFieldFind& had : finds)
            if (had.key == f.key) already = true;
        if (!already) {
            WebFieldFind copy = f;
            copy.exists = !part.params.value(f.key).isEmpty();
            finds.push_back(copy);
        }
    }
    if (!source.isEmpty())
        for (WebFieldFind& f : finds)
            f.evidence = QStringLiteral("%1: %2").arg(source, f.evidence);
    return finds;
}

QString htmlToText(const QString& html) {
    QString out = html;
    // Script and style hold no data a reader would see, and they are full of words that look like data.
    out.remove(QRegularExpression(QStringLiteral("<script[^>]*>.*?</script>"),
                                  QRegularExpression::DotMatchesEverythingOption |
                                      QRegularExpression::CaseInsensitiveOption));
    out.remove(QRegularExpression(QStringLiteral("<style[^>]*>.*?</style>"),
                                  QRegularExpression::DotMatchesEverythingOption |
                                      QRegularExpression::CaseInsensitiveOption));
    // A table cell boundary is a space, not nothing: "22</td><td>AWG" is two words.
    out.replace(QRegularExpression(QStringLiteral("<(br|/tr|/td|/th|/p|/li|/div|/h[1-6])[^>]*>"),
                                  QRegularExpression::CaseInsensitiveOption),
                QStringLiteral(" \n"));
    out.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
    // The entities a datasheet page actually uses.
    out.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    out.replace(QStringLiteral("&deg;"), QStringLiteral("°"));
    out.replace(QStringLiteral("&plusmn;"), QStringLiteral("±"));
    out.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    out.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    out.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    out.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    out.replace(QStringLiteral("&#176;"), QStringLiteral("°"));
    out.replace(QRegularExpression(QStringLiteral("[ \\t\\x{00A0}]+")), QStringLiteral(" "));
    return out.trimmed();
}

QString pageTitle(const QString& html) {
    QString evidence;
    QString title = firstMatch(html, QStringLiteral("<title[^>]*>(.*?)</title>"), evidence);
    title = htmlToText(title).simplified();
    // Sites hang their own name on the end of every title. What is wanted is the part, not the shop.
    for (const QString& sep : {QStringLiteral(" | "), QStringLiteral(" - "), QStringLiteral(" – ")}) {
        const int at = title.indexOf(sep);
        if (at > 12) title = title.left(at).trimmed();
    }
    return title;
}

QStringList datasheetLinks(const QString& html, const QUrl& base) {
    QStringList out;
    const QRegularExpression rx(QStringLiteral("href\\s*=\\s*[\"']([^\"']+)[\"']([^<]*)"),
                               QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator it = rx.globalMatch(html);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString href = m.captured(1);
        const QString after = m.captured(2);
        const bool pdf = href.contains(QStringLiteral(".pdf"), Qt::CaseInsensitive);
        const bool says = href.contains(QStringLiteral("datasheet"), Qt::CaseInsensitive) ||
                          after.contains(QStringLiteral("datasheet"), Qt::CaseInsensitive) ||
                          after.contains(QStringLiteral("data sheet"), Qt::CaseInsensitive);
        if (!pdf && !says) continue;
        const QUrl absolute = base.isValid() ? base.resolved(QUrl(href)) : QUrl(href);
        const QString text = absolute.toString();
        if (!text.isEmpty() && !out.contains(text)) out << text;
    }
    return out;
}

std::vector<WebFieldFind> partFieldsFromPage(const QString& html, const LibraryPart& part) {
    const QString text = htmlToText(html);
    // The product table first - that is where a distributor's page keeps the data - then the patterns for
    // what is written in a sentence.
    std::vector<WebFieldFind> finds = harvestLabelled(text, part);
    for (const Pattern& p : patternsFor(part.kind)) {
        bool already = false;
        for (const WebFieldFind& f : finds)
            if (f.key == QString::fromLatin1(p.key)) already = true;
        if (already) continue;   // the table said it; a sentence does not get to overrule the table
        QString evidence;
        QString value = firstMatch(text, QString::fromLatin1(p.rx), evidence);
        if (value.isEmpty()) continue;
        value = value.simplified();
        if (*p.suffix) value += QString::fromLatin1(p.suffix);
        WebFieldFind f;
        f.key = QString::fromLatin1(p.key);
        f.label = QString::fromLatin1(p.label);
        f.value = value;
        f.evidence = evidence;
        f.exists = !part.params.value(f.key).isEmpty();
        finds.push_back(f);
    }
    // What the page calls the part, offered as the description: the one thing on a catalogue page that is
    // always about the part itself.
    const QString title = pageTitle(html);
    if (!title.isEmpty()) {
        WebFieldFind f;
        f.key = QStringLiteral("__description");
        f.label = QStringLiteral("Description");
        f.value = title;
        f.evidence = QStringLiteral("the page's own title");
        f.exists = !part.description.isEmpty();
        finds.insert(finds.begin(), f);
    }
    return finds;
}

int applyWebFinds(LibraryPart& part, const std::vector<WebFieldFind>& finds) {
    int changed = 0;
    for (const WebFieldFind& f : finds) {
        if (f.value.isEmpty()) continue;
        if (f.key == QStringLiteral("__description")) {
            if (part.description != f.value) {
                part.description = f.value;
                ++changed;
            }
            continue;
        }
        if (part.params.value(f.key) == f.value) continue;
        part.params.insert(f.key, f.value);
        ++changed;
    }
    return changed;
}

} // namespace Edraw_Desktop
