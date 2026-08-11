#include "model/PageSetup.h"

#include <QStringList>

#include <algorithm>
#include <cmath>

namespace Edraw_Desktop {

namespace {

// The margin a size gets when it is not a full-bleed one: what a desktop printer will not reach. Five
// millimetres all round is the usual worst case, and being generous by a millimetre costs nothing while
// being mean by one puts the border of a sheet outside the printable area.
constexpr double kMargin = 5.0;

QString sizeName(const char* series, const char* label, double w, double h, bool bleed) {
    return QStringLiteral("%1%2 %3 (%4 x %5 MM)")
        .arg(QString::fromLatin1(series))
        .arg(bleed ? QStringLiteral(" full bleed") : QString())
        .arg(QString::fromLatin1(label))
        .arg(w, 0, 'f', 2)
        .arg(h, 0, 'f', 2);
}

QString inchName(const char* series, const char* label, double wIn, double hIn, bool bleed) {
    return QStringLiteral("%1%2 %3 (%4 x %5 Inches)")
        .arg(QString::fromLatin1(series))
        .arg(bleed ? QStringLiteral(" full bleed") : QString())
        .arg(QString::fromLatin1(label))
        .arg(wIn, 0, 'f', 2)
        .arg(hIn, 0, 'f', 2);
}

std::vector<PaperSize> buildPaperSizes() {
    struct Metric { const char* label; double w, h; };
    // The ISO A series, and the oversize 2A0/4A0 a wide harness sometimes needs.
    const Metric aSeries[] = {{"A4", 297.0, 210.0},   {"A3", 420.0, 297.0},   {"A2", 594.0, 420.0},
                              {"A1", 841.0, 594.0},   {"A0", 1189.0, 841.0},  {"2A0", 1682.0, 1189.0},
                              {"4A0", 2378.0, 1682.0}};
    const Metric bSeries[] = {{"B5", 250.0, 176.0},  {"B4", 353.0, 250.0},  {"B3", 500.0, 353.0},
                              {"B2", 707.0, 500.0},  {"B1", 1000.0, 707.0}};
    const Metric cSeries[] = {{"C5", 229.0, 162.0},  {"C4", 324.0, 229.0},  {"C3", 458.0, 324.0}};
    struct Imperial { const char* label; double wIn, hIn; };
    const Imperial ansi[] = {{"A", 11.0, 8.5},   {"B", 17.0, 11.0},  {"C", 22.0, 17.0},
                             {"D", 34.0, 22.0},  {"E", 44.0, 34.0}};
    const Imperial arch[] = {{"A", 12.0, 9.0},   {"B", 18.0, 12.0},  {"C", 24.0, 18.0},
                             {"D", 36.0, 24.0},  {"E", 48.0, 36.0}};

    std::vector<PaperSize> out;
    // Both twins of every size, the plain one first: the list reads A4, full bleed A4, A3, full bleed A3,
    // so the pair is found together rather than the bleed sizes being a separate list further down.
    for (const Metric& m : aSeries) {
        out.push_back({sizeName("ISO", m.label, m.w, m.h, false), m.w, m.h, kMargin});
        out.push_back({sizeName("ISO", m.label, m.w, m.h, true), m.w, m.h, 0.0});
    }
    for (const Metric& m : bSeries) {
        out.push_back({sizeName("ISO", m.label, m.w, m.h, false), m.w, m.h, kMargin});
        out.push_back({sizeName("ISO", m.label, m.w, m.h, true), m.w, m.h, 0.0});
    }
    for (const Metric& m : cSeries) {
        out.push_back({sizeName("ISO", m.label, m.w, m.h, false), m.w, m.h, kMargin});
        out.push_back({sizeName("ISO", m.label, m.w, m.h, true), m.w, m.h, 0.0});
    }
    for (const Imperial& i : ansi) {
        const double w = i.wIn * 25.4, h = i.hIn * 25.4;
        out.push_back({inchName("ANSI", i.label, i.wIn, i.hIn, false), w, h, kMargin});
        out.push_back({inchName("ANSI", i.label, i.wIn, i.hIn, true), w, h, 0.0});
    }
    for (const Imperial& i : arch) {
        const double w = i.wIn * 25.4, h = i.hIn * 25.4;
        out.push_back({inchName("ARCH", i.label, i.wIn, i.hIn, false), w, h, kMargin});
        out.push_back({inchName("ARCH", i.label, i.wIn, i.hIn, true), w, h, 0.0});
    }
    // The one every office printer has, under the name it is sold as.
    out.push_back({QStringLiteral("Letter (11.00 x 8.50 Inches)"), 279.4, 215.9, kMargin});
    out.push_back({QStringLiteral("Letter full bleed (11.00 x 8.50 Inches)"), 279.4, 215.9, 0.0});
    out.push_back({QStringLiteral("Legal (14.00 x 8.50 Inches)"), 355.6, 215.9, kMargin});
    out.push_back({QStringLiteral("Legal full bleed (14.00 x 8.50 Inches)"), 355.6, 215.9, 0.0});
    return out;
}

} // namespace

const std::vector<PaperSize>& paperSizes() {
    static const std::vector<PaperSize> sizes = buildPaperSizes();
    return sizes;
}

const PaperSize& paperSizeByName(const QString& name) {
    const std::vector<PaperSize>& all = paperSizes();
    for (const PaperSize& size : all)
        if (size.name == name) return size;
    // Not a name we know - an older drawing, or one written by hand. A3 full bleed, which is what a new
    // layout is: better a sheet that plots than a drawing that refuses to.
    for (const PaperSize& size : all)
        if (size.name.contains(QStringLiteral("full bleed A3"))) return size;
    return all.front();
}

QString paperSizeFor(double widthMm, double heightMm, bool preferFullBleed) {
    const double w = std::max(widthMm, heightMm), h = std::min(widthMm, heightMm);
    const std::vector<PaperSize>& all = paperSizes();
    const PaperSize* best = nullptr;
    double bestWaste = 1e300;
    for (const PaperSize& size : all) {
        const bool bleed = size.marginMm <= 0.0;
        if (bleed != preferFullBleed) continue;
        // What can actually be printed on: a plain A3 is 420 x 297 of paper but only 410 x 287 of ink,
        // and a 420 x 297 sheet does not fit on it. Full bleed has no margin, so its whole face counts.
        const double sw = std::max(size.widthMm, size.heightMm) - 2.0 * size.marginMm;
        const double sh = std::min(size.widthMm, size.heightMm) - 2.0 * size.marginMm;
        if (sw + 0.5 < w || sh + 0.5 < h) continue;   // too small for it
        const double waste = sw * sh - w * h;
        if (waste < bestWaste) {
            bestWaste = waste;
            best = &size;
        }
    }
    if (best) return best->name;
    return paperSizeByName(QString()).name;   // nothing is big enough: fall back to the default
}

double PageSetup::marginMm() const { return paperSizeByName(paper).marginMm; }

double PageSetup::pageWidthMm() const {
    const PaperSize& size = paperSizeByName(paper);
    const double longEdge = std::max(size.widthMm, size.heightMm);
    const double shortEdge = std::min(size.widthMm, size.heightMm);
    return landscape ? longEdge : shortEdge;
}

double PageSetup::pageHeightMm() const {
    const PaperSize& size = paperSizeByName(paper);
    const double longEdge = std::max(size.widthMm, size.heightMm);
    const double shortEdge = std::min(size.widthMm, size.heightMm);
    return landscape ? shortEdge : longEdge;
}

double PageSetup::printableWidthMm() const {
    return std::max(1.0, pageWidthMm() - 2.0 * marginMm());
}

double PageSetup::printableHeightMm() const {
    return std::max(1.0, pageHeightMm() - 2.0 * marginMm());
}

const std::vector<std::pair<QString, double>>& PageSetup::scaleChoices() {
    static const std::vector<std::pair<QString, double>> choices = {
        {QStringLiteral("1:1"), 1.0},      {QStringLiteral("1:2"), 0.5},
        {QStringLiteral("1:4"), 0.25},     {QStringLiteral("1:5"), 0.2},
        {QStringLiteral("1:8"), 0.125},    {QStringLiteral("1:10"), 0.1},
        {QStringLiteral("1:16"), 0.0625},  {QStringLiteral("1:20"), 0.05},
        {QStringLiteral("1:50"), 0.02},    {QStringLiteral("1:100"), 0.01},
        {QStringLiteral("2:1"), 2.0},      {QStringLiteral("4:1"), 4.0},
        {QStringLiteral("10:1"), 10.0}};
    return choices;
}

QString PageSetup::scaleLabel() const {
    if (fitToPaper) return QStringLiteral("Fit to paper");
    for (const auto& choice : scaleChoices())
        if (std::abs(choice.second - scale) < 1e-9) return choice.first;
    if (scale >= 1.0) return QStringLiteral("%1:1").arg(scale, 0, 'g', 4);
    return QStringLiteral("1:%1").arg(1.0 / std::max(1e-9, scale), 0, 'g', 4);
}

QJsonObject PageSetup::toJson() const {
    return {{QStringLiteral("name"), name},
            {QStringLiteral("device"), device},
            {QStringLiteral("paper"), paper},
            {QStringLiteral("landscape"), landscape},
            {QStringLiteral("upsideDown"), upsideDown},
            {QStringLiteral("area"), area == PlotArea::Extents ? QStringLiteral("extents")
                                                              : QStringLiteral("layout")},
            {QStringLiteral("fitToPaper"), fitToPaper},
            {QStringLiteral("scale"), scale},
            {QStringLiteral("offsetX"), offsetXMm},
            {QStringLiteral("offsetY"), offsetYMm},
            {QStringLiteral("centred"), centred},
            {QStringLiteral("monochrome"), monochrome},
            {QStringLiteral("plotLineweights"), plotLineweights},
            {QStringLiteral("plotPaperSpaceLast"), plotPaperSpaceLast}};
}

PageSetup PageSetup::fromJson(const QJsonObject& obj) {
    PageSetup setup;
    setup.name = obj.value(QStringLiteral("name")).toString();
    setup.device = obj.value(QStringLiteral("device")).toString();
    setup.paper = obj.value(QStringLiteral("paper")).toString(setup.paper);
    setup.landscape = obj.value(QStringLiteral("landscape")).toBool(true);
    setup.upsideDown = obj.value(QStringLiteral("upsideDown")).toBool(false);
    setup.area = obj.value(QStringLiteral("area")).toString() == QLatin1String("extents")
                     ? PlotArea::Extents
                     : PlotArea::Layout;
    setup.fitToPaper = obj.value(QStringLiteral("fitToPaper")).toBool(false);
    setup.scale = obj.value(QStringLiteral("scale")).toDouble(1.0);
    if (setup.scale <= 0.0) setup.scale = 1.0;
    setup.offsetXMm = obj.value(QStringLiteral("offsetX")).toDouble(0.0);
    setup.offsetYMm = obj.value(QStringLiteral("offsetY")).toDouble(0.0);
    setup.centred = obj.value(QStringLiteral("centred")).toBool(true);
    setup.monochrome = obj.value(QStringLiteral("monochrome")).toBool(true);
    setup.plotLineweights = obj.value(QStringLiteral("plotLineweights")).toBool(true);
    setup.plotPaperSpaceLast = obj.value(QStringLiteral("plotPaperSpaceLast")).toBool(true);
    return setup;
}

} // namespace Edraw_Desktop
