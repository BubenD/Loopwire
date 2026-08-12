// Reading a part's technical data off its web page.
//
// The reader is a pure function of the page's text, so it is tested against pages rather than against the
// internet: a wire listing, a heatshrink listing, a connector listing, and a page that has nothing on it. The
// point being made in each is the same - what is found is found in the words a catalogue actually uses, and
// what is not there yields NOTHING rather than a guess.
#include "harness/PartDataFromWeb.h"
#include "harness/PartsLibrary.h"

#include <QUrl>

#include <cstdio>

using namespace Edraw_Desktop;

namespace {

int webFailures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        std::printf("  [FAIL] %s\n", what);
        ++webFailures;
    }
}

QString found(const std::vector<WebFieldFind>& finds, const QString& key) {
    for (const WebFieldFind& f : finds)
        if (f.key == key) return f.value;
    return QString();
}

bool has(const std::vector<WebFieldFind>& finds, const QString& key) {
    for (const WebFieldFind& f : finds)
        if (f.key == key) return true;
    return false;
}

} // namespace

int run_partweb_tests() {
    std::printf("part web data tests:\n");

    // ---- the page's text ---------------------------------------------------------------------------
    {
        const QString html = QStringLiteral(
            "<html><head><title>3050 BK005 hook-up wire | Acme Distribution</title>"
            "<style>.x{color:red}</style><script>var awg='99 AWG';</script></head>"
            "<body><table><tr><td>Gauge</td><td>24 AWG</td></tr></table></body></html>");
        const QString text = htmlToText(html);
        check(!text.contains(QStringLiteral("<td>")), "text: the tags are gone");
        check(!text.contains(QStringLiteral("99 AWG")),
              "text: and so is the script - a page's own code is full of words that look like data");
        check(text.contains(QStringLiteral("Gauge")) && text.contains(QStringLiteral("24 AWG")),
              "text: a table cell boundary is a space, so two cells are two words rather than one");
        check(pageTitle(html) == QStringLiteral("3050 BK005 hook-up wire"),
              "title: the part, without the shop's name hung on the end of it");
    }

    // ---- a wire listing ---------------------------------------------------------------------------
    {
        LibraryPart wire;
        wire.kind = PartKind::Wire;
        const QString html = QStringLiteral(
            "<html><head><title>Hook-up wire, 22 AWG, PTFE — Acme</title></head><body>"
            "<table>"
            "<tr><td>Conductor</td><td>22 AWG, 19/34 tinned copper</td></tr>"
            "<tr><td>Insulation</td><td>PTFE, 0.25 mm wall</td></tr>"
            "<tr><td>Nominal diameter</td><td>1.52 mm</td></tr>"
            "<tr><td>Rating</td><td>600 V rms, -55 C to +200 C</td></tr>"
            "<tr><td>Specification</td><td>MIL-W-22759/11</td></tr>"
            "</table></body></html>");
        const std::vector<WebFieldFind> finds = partFieldsFromPage(html, wire);
        std::printf("  wire page: %zu finds\n", finds.size());
        check(found(finds, QStringLiteral("gauge")) == QStringLiteral("22"),
              "wire: the gauge, as a catalogue writes it");
        check(found(finds, QStringLiteral("strands")) == QStringLiteral("19/34"),
              "wire: the stranding");
        check(found(finds, QStringLiteral("od")) == QStringLiteral("1.52"),
              "wire: the diameter, in millimetres");
        check(found(finds, QStringLiteral("voltage")) == QStringLiteral("600"), "wire: the voltage");
        check(found(finds, QStringLiteral("insulation")) == QStringLiteral("PTFE"),
              "wire: what it is insulated with");
        check(found(finds, QStringLiteral("spec")).startsWith(QStringLiteral("MIL-W-22759")),
              "wire: and the specification it is built to");
        check(found(finds, QStringLiteral("temp")).contains(QStringLiteral("200")),
              "wire: the temperature range, both ends of it");
        // Every find carries the words it was read from: that is what makes it checkable.
        bool everyEvidence = !finds.empty();
        for (const WebFieldFind& f : finds)
            if (f.evidence.trimmed().isEmpty()) everyEvidence = false;
        check(everyEvidence, "wire: each reading says where on the page it was read");
        check(found(finds, QStringLiteral("__description")).contains(QStringLiteral("Hook-up wire")),
              "wire: and the page's title is offered as the description");
    }

    // ---- heatshrink ------------------------------------------------------------------------------
    {
        LibraryPart shrink;
        shrink.kind = PartKind::Heatshrink;
        const QString html = QStringLiteral(
            "<html><body><h1>Heatshrink tubing</h1>"
            "<p>Shrink ratio 3:1. As supplied 12.0 mm, recovered 4.0 mm.</p>"
            "<p>Recovered wall 0.65 mm. Minimum shrink temperature 120 C.</p>"
            "<p>Operating range -55 C to +135 C. Material: polyolefin, adhesive lined.</p>"
            "</body></html>");
        const std::vector<WebFieldFind> finds = partFieldsFromPage(html, shrink);
        check(found(finds, QStringLiteral("ratio")) == QStringLiteral("3:1"),
              "shrink: the ratio, which is the first thing anyone asks of heatshrink");
        check(found(finds, QStringLiteral("entryMax")) == QStringLiteral("12.0") &&
                  found(finds, QStringLiteral("entryMin")) == QStringLiteral("4.0"),
              "shrink: as supplied and recovered, which way round they belong");
        check(found(finds, QStringLiteral("wall")) == QStringLiteral("0.65"),
              "shrink: the recovered wall - the one figure the starter library leaves blank");
        check(found(finds, QStringLiteral("shrinkTemp")) == QStringLiteral("120"),
              "shrink: and what it takes to shrink it");
        check(found(finds, QStringLiteral("material")).compare(QStringLiteral("Polyolefin"),
                                                              Qt::CaseInsensitive) == 0,
              "shrink: what it is made of");
    }

    // ---- a connector -----------------------------------------------------------------------------
    {
        LibraryPart conn;
        conn.kind = PartKind::Connector;
        const QString html = QStringLiteral(
            "<html><body><h1>Circular connector, 19 way receptacle</h1>"
            "<p>Shell size 15, pitch 2.54 mm, gold-plated contacts.</p>"
            "<p>MIL-DTL-38999 Series III. Operating temperature -65 C to +175 C.</p>"
            "</body></html>");
        const std::vector<WebFieldFind> finds = partFieldsFromPage(html, conn);
        check(found(finds, QStringLiteral("pins")) == QStringLiteral("19"),
              "connector: the way count - the number the whole symbol is built from");
        check(found(finds, QStringLiteral("pitch")) == QStringLiteral("2.54"), "connector: the pitch");
        check(found(finds, QStringLiteral("shellSize")) == QStringLiteral("15"),
              "connector: the shell size");
        check(found(finds, QStringLiteral("gender")).compare(QStringLiteral("receptacle"),
                                                            Qt::CaseInsensitive) == 0,
              "connector: which half of the pair it is");
        check(found(finds, QStringLiteral("plating")).compare(QStringLiteral("gold"),
                                                             Qt::CaseInsensitive) == 0,
              "connector: what the contacts are plated with");
        check(found(finds, QStringLiteral("spec")).startsWith(QStringLiteral("MIL-DTL-38999")),
              "connector: and the specification");
    }

    // ---- a page with nothing on it ---------------------------------------------------------------
    {
        LibraryPart wire;
        wire.kind = PartKind::Wire;
        const std::vector<WebFieldFind> finds = partFieldsFromPage(
            QStringLiteral("<html><body><p>Our company was founded in 1971. Contact us for a quote.</p>"
                           "</body></html>"),
            wire);
        check(finds.empty(),
              "nothing: a page that says nothing technical yields nothing - not a guess, and not a "
              "half-read figure out of a sentence about the company");
    }

    // ---- what the part already says --------------------------------------------------------------
    {
        LibraryPart wire;
        wire.kind = PartKind::Wire;
        wire.params.insert(QStringLiteral("gauge"), QStringLiteral("20"));
        wire.description = QStringLiteral("What somebody typed");
        const std::vector<WebFieldFind> finds =
            partFieldsFromPage(QStringLiteral("<html><head><title>Wire, 22 AWG</title></head>"
                                              "<body><p>Conductor 22 AWG</p></body></html>"),
                               wire);
        bool gaugeMarked = false, descMarked = false;
        for (const WebFieldFind& f : finds) {
            if (f.key == QStringLiteral("gauge") && f.exists) gaugeMarked = true;
            if (f.key == QStringLiteral("__description") && f.exists) descMarked = true;
        }
        check(gaugeMarked && descMarked,
              "already: a field the part fills in is marked as such, so replacing it is a decision - the "
              "dialog leaves those unticked");

        // Applying writes only what it is given.
        const std::vector<WebFieldFind> onlyGauge = {
            WebFieldFind{QStringLiteral("gauge"), QStringLiteral("Gauge"), QStringLiteral("22"),
                         QStringLiteral("Conductor 22 AWG"), true}};
        const int changed = applyWebFinds(wire, onlyGauge);
        check(changed == 1 && wire.params.value(QStringLiteral("gauge")) == QStringLiteral("22"),
              "apply: the ticked field is written");
        check(wire.description == QStringLiteral("What somebody typed"),
              "apply: and nothing else is touched");
        check(applyWebFinds(wire, onlyGauge) == 0, "apply: writing the same value again changes nothing");
    }

    // ---- the datasheet on the page ---------------------------------------------------------------
    {
        const QString html = QStringLiteral(
            "<html><body><a href=\"/img/logo.png\">logo</a>"
            "<a href=\"/docs/1234.pdf\">Datasheet (PDF)</a>"
            "<a href=\"https://other.example/sheet.pdf\">another sheet</a></body></html>");
        const QStringList sheets = datasheetLinks(html, QUrl(QStringLiteral("https://acme.example/p/1")));
        check(sheets.size() == 2, "datasheet: the PDFs on the page, and not the pictures");
        check(!sheets.isEmpty() && sheets.front() == QStringLiteral("https://acme.example/docs/1234.pdf"),
              "datasheet: made absolute, so the link works away from the page it was found on");
    }

    // ---- the link is part of the part -------------------------------------------------------------
    {
        PartsLibrary lib;
        LibraryPart& part = lib.add(PartKind::Wire);
        part.link = QStringLiteral("https://acme.example/p/3050");
        PartsLibrary back;
        back.fromJson(lib.toJson());
        check(!back.parts.empty() && back.parts.front().link == part.link,
              "link: it is written to the library file and read back - a part's page is part of the part");
    }

    // ---- a site that refuses to be read says so, and is not reported as "no data" ------------------
    {
        QString why;
        check(pageWasRefused(QStringLiteral("<html><body><h1>Access to this page has been denied.</h1>"
                                            "</body></html>"),
                             403, &why),
              "refused: an access-denied page is a refusal, not an empty part");
        check(why.contains(QStringLiteral("403")),
              "refused: and the status is in what the user is told, so it can be recognised for what it is");
        why.clear();
        check(pageWasRefused(
                  QStringLiteral("<html><body>Powered and protected by somebody Privacy</body></html>"), 200,
                  &why),
              "refused: so is the holding page a bot protector serves with a 200 — which is exactly what the "
              "distributor sent back, and it read as 'nothing found'");
        why.clear();
        const QString real =
            QStringLiteral("<html><body><table><tr><td>Number of Positions</td><td>10</td></tr>"
                           "<tr><td>Pitch</td><td>2.54 mm</td></tr></table>%1</body></html>")
                .arg(QString(500, QLatin1Char('x')));
        check(!pageWasRefused(real, 200, &why), "refused: a real page is not mistaken for a refusal");
    }

    // ---- the table on a distributor's page, which is where the data actually is ---------------------
    {
        LibraryPart conn;
        conn.kind = PartKind::Connector;
        const QString html = QStringLiteral(
            "<html><head><title>43030-0010 Molex — Crimp housing</title></head><body>"
            "<table>"
            "<tr><td>Manufacturer</td><td>Molex</td></tr>"
            "<tr><td>Number of Positions</td><td>10</td></tr>"
            "<tr><td>Pitch</td><td>3.00 mm</td></tr>"
            "<tr><td>Gender</td><td>Receptacle</td></tr>"
            "<tr><td>Contact Plating</td><td>Tin</td></tr>"
            "<tr><td>Operating Temperature</td><td>-40 C to +105 C</td></tr>"
            "<tr><td>Wire Gauge</td><td>22 AWG</td></tr>"
            "</table></body></html>");
        const std::vector<WebFieldFind> finds = partFieldsFromPage(html, conn);
        std::printf("  a distributor's table: %zu finds\n", finds.size());
        check(found(finds, QStringLiteral("pins")) == QStringLiteral("10"),
              "table: the way count off a table ROW — which is how a product page writes it, and what the "
              "sentence patterns alone could not read");
        check(found(finds, QStringLiteral("pitch")) == QStringLiteral("3.00"), "table: the pitch");
        check(found(finds, QStringLiteral("gender"))
                  .compare(QStringLiteral("Receptacle"), Qt::CaseInsensitive) == 0,
              "table: the gender");
        check(found(finds, QStringLiteral("plating"))
                  .compare(QStringLiteral("Tin"), Qt::CaseInsensitive) == 0,
              "table: the plating");
        check(found(finds, QStringLiteral("temp")).contains(QStringLiteral("105")),
              "table: and the temperature range");
    }

    // ---- the same reading over pasted text, and over a datasheet's text -----------------------------
    {
        LibraryPart shrink;
        shrink.kind = PartKind::Heatshrink;
        // What a person gets by selecting a page and copying it, or what a PDF gives: lines, not markup.
        const QString pasted = QStringLiteral(
            "DR-25 heat-shrinkable tubing\n"
            "Shrink Ratio: 2:1\n"
            "Expanded Diameter: 12.7 mm\n"
            "Recovered Diameter: 6.4 mm\n"
            "Wall Thickness: 0.64 mm\n"
            "Operating Temperature: -75 C to +150 C\n"
            "Material: Elastomer\n");
        const std::vector<WebFieldFind> finds =
            partFieldsFromText(pasted, shrink, QStringLiteral("the pasted page"));
        std::printf("  pasted text: %zu finds\n", finds.size());
        check(found(finds, QStringLiteral("ratio")) == QStringLiteral("2:1"),
              "pasted: the ratio, off a plain line — which is what a page that refuses to be fetched still "
              "gives when a person copies it");
        check(found(finds, QStringLiteral("entryMax")) == QStringLiteral("12.7") &&
                  found(finds, QStringLiteral("entryMin")) == QStringLiteral("6.4"),
              "pasted: as supplied and recovered");
        check(found(finds, QStringLiteral("wall")) == QStringLiteral("0.64"), "pasted: the wall");
        bool sourced = !finds.empty();
        for (const WebFieldFind& f : finds)
            if (!f.evidence.contains(QStringLiteral("the pasted page"))) sourced = false;
        check(sourced, "pasted: and every reading says it came from the pasted page");
    }

    if (webFailures == 0) std::printf("  -> ok\n");
    else std::printf("  -> %d failed\n", webFailures);
    return webFailures;
}
