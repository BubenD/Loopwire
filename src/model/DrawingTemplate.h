#pragma once

#include "model/Entity.h"
#include "model/Layer.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <vector>

namespace Loopwire {

class Document;

// ---- A drawing template ------------------------------------------------------------------------
// What a new drawing starts as: the sheet and its border, the layers, and the defaults a drawing is
// worked in. Everything a house style consists of, in one file that can be handed to someone else.
//
// The extension is .etp. NOT .tpl: that one is taken several times over (Smarty and other PHP
// templating, a handful of code generators, more than one CMS), and on Windows an extension already
// claimed means a template opening in someone's text editor.
struct TemplateSheet {
    QString name = QStringLiteral("A3");
    double widthMm = 420.0;
    double heightMm = 297.0;
    bool landscape = true;

    double drawnWidthMm() const { return landscape ? std::max(widthMm, heightMm)
                                                   : std::min(widthMm, heightMm); }
    double drawnHeightMm() const { return landscape ? std::min(widthMm, heightMm)
                                                    : std::max(widthMm, heightMm); }
};

// The standard sheets offered, largest first. A name nothing matches is a custom size.
const std::vector<TemplateSheet>& templateSheets();
const TemplateSheet* templateSheetByName(const QString& name);

// A signature on a drawing: who, and when. Free text rather than a date type, because the date on
// an approval is blank until it is approved, and a drawing office writes "TBC" in the meantime - a
// date control with no empty state would force a date that is not true.
struct TemplateSignature {
    QString name;
    QString date;
};

// The grid reference frame round the border: numbers along the top and bottom, letters down both
// sides, so anything on the sheet can be called out by the zone it sits in ("see B3").
//
// Divisions are EQUAL and the frame is symmetric, so the nominal size is a target rather than a
// promise: the count is chosen to come closest to it and the true division follows from the sheet.
// The letters I and O are skipped, because on a printed drawing they read as 1 and 0.
struct TemplateZones {
    bool enabled = true;
    double sizeMm = 50.0;        // the division aimed for
    double bandMm = 8.0;         // the band the labels sit in, between the sheet edge and the border
    double textHeight = 3.5;
    // Letters running A downwards from the top (ISO 5457). False starts A at the bottom, which is
    // what some ASME-style sheets do.
    bool lettersFromTop = true;
};

// How many divisions a sheet gets, and what each one is called. The column label is a number, the row
// label a letter (AA, AB... past the alphabet, and never I or O).
// The width of the band the zone labels sit in - the border inset, in other words.
double templateZoneBandMm(const TemplateSheet& sheet, const TemplateZones& zones);
int templateZoneColumns(const TemplateSheet& sheet, const TemplateZones& zones);
int templateZoneRows(const TemplateSheet& sheet, const TemplateZones& zones);
QString templateZoneColumnLabel(int index);   // 0 -> "1"
QString templateZoneRowLabel(int index);      // 0 -> "A"

// A box on the template that each DRAWING fills in - the title, the drawing number, the sheet count.
// Not a template-level value like a signature: the template says the box exists and what it is called,
// and every drawing made from it types its own answer in.
//
// A field is carried on the text entity itself (meta "field"), so a drawing knows which box a piece of
// text IS - and so filling one in survives the template being re-applied.
struct TemplateField {
    QString key;                 // "title", "drawingNo", "sheet", "scale"
    QString label;               // what the box is called on the sheet
    QString value;               // what it starts at, usually empty
    double textHeight = 4.0;
    bool builtIn = false;        // placed by the generated title block rather than drawn by hand
};

// The revision table, in the top-right corner: one row per issue of the drawing. Every column can be
// switched off, because no two drawing offices want the same set - and the widths go with them, so a
// table with three columns is not a table with seven and four of them blank.
struct TemplateRevisions {
    bool enabled = true;
    int rows = 4;                // blank rows to start with
    double rowHeightMm = 7.0;
    double textHeight = 2.6;

    bool showRev = true;
    bool showNote = true;
    bool showEco = true;
    bool showDrawn = true;
    bool showChecked = true;
    bool showApproved = true;
    bool showDate = true;

    double wRev = 12.0;
    double wNote = 58.0;
    double wEco = 22.0;
    double wDrawn = 18.0;
    double wChecked = 18.0;
    double wApproved = 20.0;
    double wDate = 22.0;

    // The columns actually shown, left to right: their headings and their widths.
    QStringList headings() const;
    std::vector<double> widths() const;
    double totalWidthMm() const;
    double totalHeightMm() const { return rowHeightMm * (rows + 1); }   // + the heading row
};

// The title block's own geometry, so it can be sized to a house standard rather than to mine.
struct TemplateTitleBlock {
    bool enabled = true;
    double widthMm = 190.0;
    double heightMm = 56.0;
    double captionHeight = 2.6;   // the small printed captions (TITLE, DRAWN, ...)
    double valueHeight = 3.2;     // what is written in the boxes
    // THE COMPANY BAND across the top of the title block: the logo's box on its left, the company's details - a long
    // free text - beside it. A template saved before it had one keeps none.
    bool companyBand = true;
    double companyHeightMm = 20.0;
};

struct DrawingTemplate {
    QString name;
    QString description;
    QString author;
    QString revision;
    QString created;              // ISO date, stamped when saved

    // The three signatures every drawing carries, and the dates against them. They go in the title
    // block, so a sheet made from this template already says who is responsible for it.
    TemplateSignature drawn;
    TemplateSignature checked;
    TemplateSignature approved;

    // THE DRAWING SCALE written in the title block's SCALE box ("1:1", "1:2", "NTS"), THE COMPANY'S DETAILS for its band
    // (lines of free text), and THE LOGO in the band's box (the picture's bytes, as they came, and their format).
    QString scale = QStringLiteral("1:1");
    QString company;
    QByteArray logo;
    QString logoFormat;

    // The grid reference frame round the border.
    TemplateZones zones;
    // The revision table, the title block's geometry, and the boxes each drawing fills in.
    TemplateRevisions revisions;
    TemplateTitleBlock titleBlock;
    std::vector<TemplateField> fields;

    // The fields a generated title block places, when `fields` has not been set to something else.
    static std::vector<TemplateField> defaultFields();

    TemplateSheet sheet;
    // The layers a drawing made from this starts with. Empty means leave the document's own.
    std::vector<Layer> layers;
    QString activeLayer = QStringLiteral("0");

    // The defaults a drawing is worked in.
    QString unit = QStringLiteral("mm");
    double textHeight = 5.0;
    double dimTextHeight = 5.0;
    double dimArrowSize = 3.3;
    int dimDecimals = 2;
    double gridSize = 10.0;
    bool gridOn = true;
    bool snapOn = true;
    // Cross-sections written in AWG rather than mm2 - a house decision, so it belongs here.
    bool gaugeAwg = false;

    // What is drawn on the sheet to begin with: the border, the title block, standard notes. Stored
    // as entities, so a template is drawn with the ordinary tools rather than described in a form.
    QJsonArray content;

    bool hasContent() const { return !content.isEmpty(); }

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& root);
    bool save(const QString& path) const;
    bool load(const QString& path);

    static QString extension();     // "etp"
    static QString fileFilter();    // "Loopwire template (*.etp);;All files (*)"
    // <app data>/template/<name>.etp - where Save with no path puts it.
    static QString pathFor(const QString& name);
    // The templates in the template folder, by full path.
    static QStringList installed();
};

// Put a template into a document: its layers, its defaults, and its content. Anything already drawn
// is kept - a template is applied to a NEW drawing, and applying one to a drawing with work in it
// must not throw that work away.
void applyTemplate(const DrawingTemplate& tpl, Document& doc);

// The other way: everything about this drawing that a template can carry. The entities come across
// as the template's content, so a border drawn once becomes the border every drawing starts with.
DrawingTemplate templateFromDocument(const Document& doc, const QString& name);

// ---- Template content on a drawing --------------------------------------------------------------
// Applying a template stamps every one of its objects with one group key, so the whole frame is picked
// and moved as ONE thing rather than as eighty lines and labels. A field keeps its own mark as well, so
// it can still be filled in.
QString entityTemplateGroup(const Entity& e);
QString entityTemplateName(const Entity& e);
QString entityFieldKey(const Entity& e);
bool entityIsTemplateField(const Entity& e);

// Which zone a point on the sheet falls in ("B3"), or empty when it is outside the frame or the frame
// is switched off. The point is in sheet coordinates, the same ones defaultSheetContent draws in.
QString templateZoneAt(const DrawingTemplate& tpl, const Vec2& at);

// A plain bordered sheet with a title block, so a new template is not an empty form. The entities
// are in millimetres with the sheet's bottom-left at the origin. The overload taking the whole
// template writes its signatures and dates into the title block; the bare one leaves them as
// captions to be filled in on the sheet.
QJsonArray defaultSheetContent(const TemplateSheet& sheet);
QJsonArray defaultSheetContent(const DrawingTemplate& tpl);

} // namespace Loopwire
