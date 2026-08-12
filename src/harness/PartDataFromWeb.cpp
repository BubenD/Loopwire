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

} // namespace

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
    std::vector<WebFieldFind> finds;
    for (const Pattern& p : patternsFor(part.kind)) {
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
