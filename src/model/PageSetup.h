#pragma once

#include <QJsonObject>
#include <QString>

#include <vector>

namespace Edraw_Desktop {

// A sheet of paper to plot on: what it is called, how big it is, and how much of it can be printed on.
//
// FULL BLEED is the second thing. A printer cannot usually reach the edges of the paper, so a page has a
// margin it will not put ink in, and a 420 x 297 sheet plotted on A3 comes out slightly reduced to fit
// inside that margin. A full-bleed size says: no margin, ink right up to the edges. It is the only way to
// get a sheet out at true size, and it is what a plotter or a PDF (which has no mechanical edge at all)
// can actually do - so both kinds are offered, and named as such.
struct PaperSize {
    QString name;          // "ISO A3 (420.00 x 297.00 MM)"
    double widthMm = 420.0;
    double heightMm = 297.0;
    double marginMm = 5.0;   // 0 for a full-bleed size: the printable area IS the page
};

// Every size offered, in the order a plot dialog offers them: the ISO A series, its full-bleed twins, the
// ISO B and C series, then the ANSI sizes and their full-bleed twins.
const std::vector<PaperSize>& paperSizes();
// The one by that name, or the nearest by size, or A3. Never null: a drawing must have somewhere to plot.
const PaperSize& paperSizeByName(const QString& name);
// The size that fits a sheet of these millimetres, preferring an exact match and then the smallest one
// that will hold it. `preferFullBleed` picks the bleed twin, for a sheet that must come out at true size.
QString paperSizeFor(double widthMm, double heightMm, bool preferFullBleed);

// What the plot area is taken from.
enum class PlotArea {
    Layout,    // the sheet itself, at its own size - what a laid-out sheet wants
    Extents,   // everything drawn, fitted to the page
};

// How a layout is plotted: the paper, which way round, at what scale, and how the ink comes out.
//
// It belongs to the layout, and it is SAVED with the drawing, because a sheet that plots correctly today
// must plot the same way for whoever opens the file next. Named setups live on the document beside the
// layouts, so one can be made once and set on every sheet.
struct PageSetup {
    QString name;                    // empty for a layout's own setup; else the named setup's name
    QString device;                  // printer name, or empty for "None" (a PDF, or not yet chosen)
    QString paper = QStringLiteral("ISO full bleed A3 (420.00 x 297.00 MM)");
    bool landscape = true;
    bool upsideDown = false;
    PlotArea area = PlotArea::Layout;
    bool fitToPaper = false;         // else `scale` is used
    double scale = 1.0;              // paper mm per drawing mm: 1 = 1:1, 0.5 = 1:2
    double offsetXMm = 0.0;
    double offsetYMm = 0.0;
    bool centred = true;
    bool monochrome = true;          // black lines; else the drawing's own colours
    bool plotLineweights = true;
    bool plotPaperSpaceLast = true;

    // The page in millimetres, the way round it is set: width and height swap for portrait.
    double pageWidthMm() const;
    double pageHeightMm() const;
    // What can be printed on, inside the margin. Equal to the page for a full-bleed size.
    double printableWidthMm() const;
    double printableHeightMm() const;
    double marginMm() const;

    // "1:1", "1:2", "2:1", "Fit to paper" - what the scale is called.
    QString scaleLabel() const;
    // The ratios a plot dialog offers, as (label, paper mm per drawing mm).
    static const std::vector<std::pair<QString, double>>& scaleChoices();

    QJsonObject toJson() const;
    static PageSetup fromJson(const QJsonObject& obj);
};

} // namespace Edraw_Desktop
