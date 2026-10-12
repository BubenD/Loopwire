#include "model/DrawingTemplate.h"

#include "io/AppPaths.h"
#include "model/Document.h"
#include "model/Picture.h"

#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QUuid>

#include <algorithm>
#include <cmath>

namespace Loopwire {

const std::vector<TemplateSheet>& templateSheets() {
    // ISO A sizes, plus the two ANSI ones a harness drawing is sometimes asked for.
    static const std::vector<TemplateSheet> sheets = {
        {QStringLiteral("A0"), 1189.0, 841.0, true}, {QStringLiteral("A1"), 841.0, 594.0, true},
        {QStringLiteral("A2"), 594.0, 420.0, true},  {QStringLiteral("A3"), 420.0, 297.0, true},
        {QStringLiteral("A4"), 297.0, 210.0, true},  {QStringLiteral("ANSI D"), 863.6, 558.8, true},
        {QStringLiteral("ANSI B"), 431.8, 279.4, true},
    };
    return sheets;
}

const TemplateSheet* templateSheetByName(const QString& name) {
    for (const TemplateSheet& s : templateSheets())
        if (s.name.compare(name, Qt::CaseInsensitive) == 0) return &s;
    return nullptr;
}

// ---- The revision table's columns ----------------------------------------------------------------

namespace {
constexpr auto kTemplateGroupKey = "templateGroup";
constexpr auto kTemplateNameKey = "templateName";
constexpr auto kFieldKey = "field";
constexpr auto kFieldLabelKey = "fieldLabel";
} // namespace

QStringList TemplateRevisions::headings() const {
    QStringList out;
    if (showRev) out << QStringLiteral("REV");
    if (showNote) out << QStringLiteral("CHANGE NOTE");
    if (showEco) out << QStringLiteral("ECO");
    if (showDrawn) out << QStringLiteral("DRAWN");
    if (showChecked) out << QStringLiteral("CHECKED");
    if (showApproved) out << QStringLiteral("APPROVED");
    if (showDate) out << QStringLiteral("DATE");
    return out;
}

std::vector<double> TemplateRevisions::widths() const {
    std::vector<double> out;
    if (showRev) out.push_back(wRev);
    if (showNote) out.push_back(wNote);
    if (showEco) out.push_back(wEco);
    if (showDrawn) out.push_back(wDrawn);
    if (showChecked) out.push_back(wChecked);
    if (showApproved) out.push_back(wApproved);
    if (showDate) out.push_back(wDate);
    return out;
}

double TemplateRevisions::totalWidthMm() const {
    double w = 0.0;
    for (double c : widths()) w += c;
    return w;
}

std::vector<TemplateField> DrawingTemplate::defaultFields() {
    // What every drawing fills in for itself. The signatures are the TEMPLATE's (see drawn/checked/
    // approved) - these are the drawing's own.
    return {
        {QStringLiteral("title"), QStringLiteral("TITLE"), {}, 4.5, true},
        {QStringLiteral("drawingNo"), QStringLiteral("DRAWING No."), {}, 4.0, true},
        {QStringLiteral("sheet"), QStringLiteral("SHEET"), {}, 3.0, true},
        {QStringLiteral("scale"), QStringLiteral("SCALE"), {}, 3.0, true},
    };
}

QString entityTemplateGroup(const Entity& e) {
    return e.meta.value(QLatin1String(kTemplateGroupKey)).toString();
}

QString entityTemplateName(const Entity& e) {
    return e.meta.value(QLatin1String(kTemplateNameKey)).toString();
}

QString entityFieldKey(const Entity& e) {
    return e.meta.value(QLatin1String(kFieldKey)).toString();
}

bool entityIsTemplateField(const Entity& e) { return !entityFieldKey(e).isEmpty(); }

// ---- The grid reference frame --------------------------------------------------------------------

namespace {

int divisions(double span, double nominal) {
    if (span <= 0.0) return 1;
    const double aim = nominal > 1.0 ? nominal : 50.0;
    // Equal divisions, as near the nominal as a whole number allows, and at least one.
    return std::max(1, static_cast<int>(std::lround(span / aim)));
}

} // namespace

double templateZoneBandMm(const TemplateSheet& sheet, const TemplateZones& z) {
    const double small = std::min(sheet.drawnWidthMm(), sheet.drawnHeightMm());
    return std::clamp(z.bandMm, 3.0, small * 0.1);
}

int templateZoneColumns(const TemplateSheet& sheet, const TemplateZones& zones) {
    return divisions(sheet.drawnWidthMm() - 2.0 * templateZoneBandMm(sheet, zones), zones.sizeMm);
}

int templateZoneRows(const TemplateSheet& sheet, const TemplateZones& zones) {
    return divisions(sheet.drawnHeightMm() - 2.0 * templateZoneBandMm(sheet, zones), zones.sizeMm);
}

QString templateZoneColumnLabel(int index) {
    return QString::number(index + 1);
}

QString templateZoneRowLabel(int index) {
    // I and O are left out: printed small on a border they read as 1 and 0.
    static const char kLetters[] = "ABCDEFGHJKLMNPQRSTUVWXYZ";
    const int base = static_cast<int>(sizeof(kLetters)) - 1;   // 24
    if (index < 0) return {};
    if (index < base) return QString(QLatin1Char(kLetters[index]));
    // Past the alphabet: AA, AB, ... using the same reduced alphabet.
    const int hi = index / base - 1;
    const int lo = index % base;
    if (hi >= base) return {};
    return QString(QLatin1Char(kLetters[hi])) + QLatin1Char(kLetters[lo]);
}

QString templateZoneAt(const DrawingTemplate& tpl, const Vec2& at) {
    const TemplateZones& z = tpl.zones;
    if (!z.enabled) return {};
    const double w = tpl.sheet.drawnWidthMm();
    const double h = tpl.sheet.drawnHeightMm();
    const double band = templateZoneBandMm(tpl.sheet, z);
    // Inside the border, which is where the zones are counted from.
    if (at.x < band || at.x > w - band || at.y < band || at.y > h - band) return {};
    const int cols = templateZoneColumns(tpl.sheet, z);
    const int rows = templateZoneRows(tpl.sheet, z);
    const double cw = (w - 2.0 * band) / cols;
    const double ch = (h - 2.0 * band) / rows;
    int col = static_cast<int>((at.x - band) / cw);
    int row = static_cast<int>((at.y - band) / ch);
    col = std::clamp(col, 0, cols - 1);
    row = std::clamp(row, 0, rows - 1);
    // Row 0 of the drawing is the BOTTOM one; the letters may run either way.
    const int letterIndex = z.lettersFromTop ? (rows - 1 - row) : row;
    return templateZoneRowLabel(letterIndex) + templateZoneColumnLabel(col);
}

QString DrawingTemplate::extension() { return QStringLiteral("etp"); }

QString DrawingTemplate::fileFilter() {
    return QStringLiteral("Loopwire template (*.etp);;All files (*)");
}

QString DrawingTemplate::pathFor(const QString& name) {
    QString safe = name.trimmed();
    if (safe.isEmpty()) safe = QStringLiteral("template");
    // A name is typed by a person, and a person will type a slash in it.
    for (QChar bad : {QLatin1Char('/'), QLatin1Char('\\'), QLatin1Char(':'), QLatin1Char('*'),
                      QLatin1Char('?'), QLatin1Char('"'), QLatin1Char('<'), QLatin1Char('>'),
                      QLatin1Char('|')})
        safe.replace(bad, QLatin1Char('-'));
    return QStringLiteral("%1/%2.%3").arg(AppPaths::templateDir(), safe, extension());
}

QStringList DrawingTemplate::installed() {
    QDir dir(AppPaths::templateDir());
    QStringList out;
    for (const QString& f :
         dir.entryList({QStringLiteral("*.%1").arg(extension())}, QDir::Files, QDir::Name))
        out << dir.absoluteFilePath(f);
    return out;
}

QJsonObject DrawingTemplate::toJson() const {
    QJsonObject o;
    o[QStringLiteral("kind")] = QStringLiteral("edraw-template");
    o[QStringLiteral("version")] = 1;
    o[QStringLiteral("name")] = name;
    o[QStringLiteral("description")] = description;
    o[QStringLiteral("author")] = author;
    o[QStringLiteral("revision")] = revision;
    o[QStringLiteral("created")] = created;

    QJsonObject sigs;
    auto writeSig = [&sigs](const char* key, const TemplateSignature& s) {
        QJsonObject o;
        o[QStringLiteral("name")] = s.name;
        o[QStringLiteral("date")] = s.date;
        sigs[QString::fromLatin1(key)] = o;
    };
    writeSig("drawn", drawn);
    writeSig("checked", checked);
    writeSig("approved", approved);
    o[QStringLiteral("signatures")] = sigs;
    o[QStringLiteral("scale")] = scale;
    if (!company.isEmpty()) o[QStringLiteral("company")] = company;
    if (!logo.isEmpty()) {
        o[QStringLiteral("logo")] = QString::fromLatin1(logo.toBase64());
        o[QStringLiteral("logoFormat")] = logoFormat;
    }

    QJsonObject z;
    z[QStringLiteral("enabled")] = zones.enabled;
    z[QStringLiteral("sizeMm")] = zones.sizeMm;
    z[QStringLiteral("bandMm")] = zones.bandMm;
    z[QStringLiteral("textHeight")] = zones.textHeight;
    z[QStringLiteral("lettersFromTop")] = zones.lettersFromTop;
    o[QStringLiteral("zones")] = z;

    QJsonObject rv;
    rv[QStringLiteral("enabled")] = revisions.enabled;
    rv[QStringLiteral("rows")] = revisions.rows;
    rv[QStringLiteral("rowHeightMm")] = revisions.rowHeightMm;
    rv[QStringLiteral("textHeight")] = revisions.textHeight;
    const char* flags[] = {"showRev", "showNote", "showEco", "showDrawn", "showChecked",
                           "showApproved", "showDate"};
    const bool flagVals[] = {revisions.showRev, revisions.showNote, revisions.showEco,
                             revisions.showDrawn, revisions.showChecked, revisions.showApproved,
                             revisions.showDate};
    for (int i = 0; i < 7; ++i) rv[QString::fromLatin1(flags[i])] = flagVals[i];
    const char* wKeys[] = {"wRev", "wNote", "wEco", "wDrawn", "wChecked", "wApproved", "wDate"};
    const double wVals[] = {revisions.wRev, revisions.wNote, revisions.wEco, revisions.wDrawn,
                            revisions.wChecked, revisions.wApproved, revisions.wDate};
    for (int i = 0; i < 7; ++i) rv[QString::fromLatin1(wKeys[i])] = wVals[i];
    o[QStringLiteral("revisions")] = rv;

    QJsonObject tb;
    tb[QStringLiteral("enabled")] = titleBlock.enabled;
    tb[QStringLiteral("widthMm")] = titleBlock.widthMm;
    tb[QStringLiteral("heightMm")] = titleBlock.heightMm;
    tb[QStringLiteral("captionHeight")] = titleBlock.captionHeight;
    tb[QStringLiteral("valueHeight")] = titleBlock.valueHeight;
    tb[QStringLiteral("companyBand")] = titleBlock.companyBand;
    tb[QStringLiteral("companyHeightMm")] = titleBlock.companyHeightMm;
    o[QStringLiteral("titleBlock")] = tb;

    QJsonArray fs;
    for (const TemplateField& f : fields) {
        QJsonObject fo;
        fo[QStringLiteral("key")] = f.key;
        fo[QStringLiteral("label")] = f.label;
        fo[QStringLiteral("value")] = f.value;
        fo[QStringLiteral("textHeight")] = f.textHeight;
        fo[QStringLiteral("builtIn")] = f.builtIn;
        fs.append(fo);
    }
    o[QStringLiteral("fields")] = fs;

    QJsonObject sh;
    sh[QStringLiteral("name")] = sheet.name;
    sh[QStringLiteral("widthMm")] = sheet.widthMm;
    sh[QStringLiteral("heightMm")] = sheet.heightMm;
    sh[QStringLiteral("landscape")] = sheet.landscape;
    o[QStringLiteral("sheet")] = sh;

    QJsonArray ls;
    for (const Layer& l : layers) ls.append(l.toJson());
    o[QStringLiteral("layers")] = ls;
    o[QStringLiteral("activeLayer")] = activeLayer;

    QJsonObject d;
    d[QStringLiteral("unit")] = unit;
    d[QStringLiteral("textHeight")] = textHeight;
    d[QStringLiteral("dimTextHeight")] = dimTextHeight;
    d[QStringLiteral("dimArrowSize")] = dimArrowSize;
    d[QStringLiteral("dimDecimals")] = dimDecimals;
    d[QStringLiteral("gridSize")] = gridSize;
    d[QStringLiteral("gridOn")] = gridOn;
    d[QStringLiteral("snapOn")] = snapOn;
    d[QStringLiteral("gaugeAwg")] = gaugeAwg;
    o[QStringLiteral("defaults")] = d;

    o[QStringLiteral("content")] = content;
    return o;
}

void DrawingTemplate::fromJson(const QJsonObject& root) {
    name = root.value(QStringLiteral("name")).toString();
    description = root.value(QStringLiteral("description")).toString();
    author = root.value(QStringLiteral("author")).toString();
    revision = root.value(QStringLiteral("revision")).toString();
    created = root.value(QStringLiteral("created")).toString();

    const QJsonObject sigs = root.value(QStringLiteral("signatures")).toObject();
    auto readSig = [&sigs](const char* key) {
        const QJsonObject o = sigs.value(QString::fromLatin1(key)).toObject();
        TemplateSignature s;
        s.name = o.value(QStringLiteral("name")).toString();
        s.date = o.value(QStringLiteral("date")).toString();
        return s;
    };
    drawn = readSig("drawn");
    checked = readSig("checked");
    approved = readSig("approved");
    // A TEMPLATE FROM BEFORE: no scale said, no company, no logo.
    scale = root.value(QStringLiteral("scale")).toString();
    company = root.value(QStringLiteral("company")).toString();
    logo = QByteArray::fromBase64(root.value(QStringLiteral("logo")).toString().toLatin1());
    logoFormat = root.value(QStringLiteral("logoFormat")).toString(QStringLiteral("png"));

    const QJsonObject z = root.value(QStringLiteral("zones")).toObject();
    zones.enabled = z.value(QStringLiteral("enabled")).toBool(true);
    zones.sizeMm = z.value(QStringLiteral("sizeMm")).toDouble(50.0);
    zones.bandMm = z.value(QStringLiteral("bandMm")).toDouble(8.0);
    zones.textHeight = z.value(QStringLiteral("textHeight")).toDouble(3.5);
    zones.lettersFromTop = z.value(QStringLiteral("lettersFromTop")).toBool(true);

    const QJsonObject rv = root.value(QStringLiteral("revisions")).toObject();
    revisions.enabled = rv.value(QStringLiteral("enabled")).toBool(true);
    revisions.rows = rv.value(QStringLiteral("rows")).toInt(5);
    revisions.rowHeightMm = rv.value(QStringLiteral("rowHeightMm")).toDouble(7.0);
    revisions.textHeight = rv.value(QStringLiteral("textHeight")).toDouble(2.6);
    revisions.showRev = rv.value(QStringLiteral("showRev")).toBool(true);
    revisions.showNote = rv.value(QStringLiteral("showNote")).toBool(true);
    revisions.showEco = rv.value(QStringLiteral("showEco")).toBool(true);
    revisions.showDrawn = rv.value(QStringLiteral("showDrawn")).toBool(true);
    revisions.showChecked = rv.value(QStringLiteral("showChecked")).toBool(true);
    revisions.showApproved = rv.value(QStringLiteral("showApproved")).toBool(true);
    revisions.showDate = rv.value(QStringLiteral("showDate")).toBool(true);
    revisions.wRev = rv.value(QStringLiteral("wRev")).toDouble(12.0);
    revisions.wNote = rv.value(QStringLiteral("wNote")).toDouble(58.0);
    revisions.wEco = rv.value(QStringLiteral("wEco")).toDouble(22.0);
    revisions.wDrawn = rv.value(QStringLiteral("wDrawn")).toDouble(18.0);
    revisions.wChecked = rv.value(QStringLiteral("wChecked")).toDouble(18.0);
    revisions.wApproved = rv.value(QStringLiteral("wApproved")).toDouble(20.0);
    revisions.wDate = rv.value(QStringLiteral("wDate")).toDouble(22.0);

    const QJsonObject tb = root.value(QStringLiteral("titleBlock")).toObject();
    titleBlock.enabled = tb.value(QStringLiteral("enabled")).toBool(true);
    titleBlock.widthMm = tb.value(QStringLiteral("widthMm")).toDouble(190.0);
    titleBlock.heightMm = tb.value(QStringLiteral("heightMm")).toDouble(56.0);
    titleBlock.captionHeight = tb.value(QStringLiteral("captionHeight")).toDouble(2.6);
    titleBlock.valueHeight = tb.value(QStringLiteral("valueHeight")).toDouble(3.2);
    titleBlock.companyBand = tb.value(QStringLiteral("companyBand")).toBool(false);
    titleBlock.companyHeightMm = tb.value(QStringLiteral("companyHeightMm")).toDouble(20.0);

    fields.clear();
    for (const auto v : root.value(QStringLiteral("fields")).toArray()) {
        const QJsonObject fo = v.toObject();
        TemplateField f;
        f.key = fo.value(QStringLiteral("key")).toString();
        f.label = fo.value(QStringLiteral("label")).toString();
        f.value = fo.value(QStringLiteral("value")).toString();
        f.textHeight = fo.value(QStringLiteral("textHeight")).toDouble(4.0);
        f.builtIn = fo.value(QStringLiteral("builtIn")).toBool(false);
        if (!f.key.isEmpty()) fields.push_back(f);
    }

    const QJsonObject sh = root.value(QStringLiteral("sheet")).toObject();
    sheet.name = sh.value(QStringLiteral("name")).toString(QStringLiteral("A3"));
    sheet.widthMm = sh.value(QStringLiteral("widthMm")).toDouble(420.0);
    sheet.heightMm = sh.value(QStringLiteral("heightMm")).toDouble(297.0);
    sheet.landscape = sh.value(QStringLiteral("landscape")).toBool(true);

    layers.clear();
    for (const auto v : root.value(QStringLiteral("layers")).toArray())
        layers.push_back(Layer::fromJson(v.toObject()));
    activeLayer = root.value(QStringLiteral("activeLayer")).toString(QStringLiteral("0"));

    const QJsonObject d = root.value(QStringLiteral("defaults")).toObject();
    unit = d.value(QStringLiteral("unit")).toString(QStringLiteral("mm"));
    textHeight = d.value(QStringLiteral("textHeight")).toDouble(5.0);
    dimTextHeight = d.value(QStringLiteral("dimTextHeight")).toDouble(5.0);
    dimArrowSize = d.value(QStringLiteral("dimArrowSize")).toDouble(3.3);
    dimDecimals = d.value(QStringLiteral("dimDecimals")).toInt(2);
    gridSize = d.value(QStringLiteral("gridSize")).toDouble(10.0);
    gridOn = d.value(QStringLiteral("gridOn")).toBool(true);
    snapOn = d.value(QStringLiteral("snapOn")).toBool(true);
    gaugeAwg = d.value(QStringLiteral("gaugeAwg")).toBool(false);

    content = root.value(QStringLiteral("content")).toArray();
}

bool DrawingTemplate::save(const QString& path) const {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    DrawingTemplate stamped = *this;
    stamped.created = QDate::currentDate().toString(Qt::ISODate);
    const qint64 wrote = f.write(QJsonDocument(stamped.toJson()).toJson(QJsonDocument::Indented));
    f.close();
    return wrote > 0;
}

bool DrawingTemplate::load(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonParseError err{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;
    const QJsonObject root = doc.object();
    // Only what this application wrote: a JSON file that happens to be lying about is not a template.
    if (root.value(QStringLiteral("kind")).toString() != QLatin1String("edraw-template"))
        return false;
    fromJson(root);
    if (name.isEmpty()) name = QFileInfo(path).completeBaseName();
    return true;
}

// ---- Applying and capturing ---------------------------------------------------------------------

void applyTemplate(const DrawingTemplate& tpl, Document& doc) {
    // The layers first, so the content lands on layers that exist. A layer of the same name is
    // overwritten: the template is the house style, and that is the point of applying it. What is
    // NOT touched is the drawing's own entities - see below.
    for (const Layer& l : tpl.layers) {
        if (Layer* existing = doc.findLayer(l.name)) {
            *existing = l;
            continue;
        }
        doc.layers().push_back(l);
    }
    if (!tpl.activeLayer.isEmpty() && doc.findLayer(tpl.activeLayer))
        doc.setActiveLayer(tpl.activeLayer);
    if (!tpl.unit.isEmpty()) doc.unit() = tpl.unit;

    // Then the content, every object of it stamped with ONE group key so the whole frame is picked
    // and moved as one thing rather than as eighty lines and labels.
    const QString group = QUuid::createUuid().toString(QUuid::WithoutBraces);
    for (const auto v : tpl.content) {
        Entity e = Entity::fromJson(v.toObject());
        // An id of its own, here and now. Clearing it and hoping something downstream fills it in left
        // every object of the frame with an empty id, which quietly broke everything that works by id -
        // picking it, moving it, selecting the frame as one thing.
        e.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
        e.meta.insert(QLatin1String(kTemplateGroupKey), group);
        if (!tpl.name.isEmpty()) e.meta.insert(QLatin1String(kTemplateNameKey), tpl.name);
        doc.entities().push_back(e);
    }
    doc.setDirty(true);
}

DrawingTemplate templateFromDocument(const Document& doc, const QString& name) {
    DrawingTemplate tpl;
    tpl.name = name;
    tpl.layers = doc.layers();
    tpl.activeLayer = doc.activeLayer();
    tpl.unit = doc.unit();
    for (const Entity& e : doc.entities()) {
        Entity copy = e;
        // The group key belongs to a placed copy, not to the template itself.
        copy.meta.remove(QLatin1String(kTemplateGroupKey));
        copy.meta.remove(QLatin1String(kTemplateNameKey));
        QJsonObject o = copy.toJson();
        o.remove(QStringLiteral("id"));
        tpl.content.append(o);
    }
    return tpl;
}

// ---- A sheet to start from ----------------------------------------------------------------------

QJsonArray defaultSheetContent(const TemplateSheet& sheet) {
    DrawingTemplate bare;
    bare.sheet = sheet;
    return defaultSheetContent(bare);
}

QJsonArray defaultSheetContent(const DrawingTemplate& tpl) {
    const TemplateSheet& sheet = tpl.sheet;
    const double w = sheet.drawnWidthMm();
    const double h = sheet.drawnHeightMm();
    // The border is inset from the sheet edge, as a printed one is; the title block sits in the
    // bottom-right corner where every drawing office puts it. With the grid reference frame on, the
    // inset IS the band the zone labels sit in, so the border and the frame are the same rectangle.
    const TemplateZones& z = tpl.zones;
    const double frameBand = std::clamp(z.bandMm, 3.0, std::min(w, h) * 0.1);
    const double inset = z.enabled ? frameBand : 10.0;
    const double tbW = std::min(tpl.titleBlock.widthMm, w * 0.6);
    const double tbH = tpl.titleBlock.heightMm;

    QJsonArray out;
    auto add = [&out](Entity e) {
        e.layer = QStringLiteral("0");
        // Marked as the generator's own work. That mark is what lets a template being designed be
        // regenerated when a parameter changes WITHOUT throwing away anything drawn by hand - and it
        // has to be put on here, at the one place a generated frame is made, rather than by whoever
        // happens to call this.
        e.meta.insert(QStringLiteral("generated"), QStringLiteral("sheet"));
        out.append(e.toJson());
    };
    auto rect = [](double x0, double y0, double x1, double y1) {
        Entity e;
        e.type = EntityType::Rect;
        e.points = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
        return e;
    };
    auto line = [](double x0, double y0, double x1, double y1) {
        Entity e;
        e.type = EntityType::Line;
        e.a = Vec2{x0, y0};
        e.b = Vec2{x1, y1};
        return e;
    };
    // A box a DRAWING fills in: drawn as whatever the template starts it at (usually nothing) and
    // marked with the field it is, so the drawing knows which box a piece of text belongs to and
    // filling it in survives the template being applied again.
    auto field = [](double x, double y, const TemplateField& f) {
        Entity e;
        e.type = EntityType::Text;
        e.a = Vec2{x, y};
        e.meta.insert(QStringLiteral("text"), f.value);
        e.meta.insert(QStringLiteral("height"), f.textHeight);
        e.meta.insert(QStringLiteral("field"), f.key);
        e.meta.insert(QStringLiteral("fieldLabel"), f.label);
        return e;
    };
    auto text = [](double x, double y, const QString& s, double height) {
        Entity e;
        e.type = EntityType::Text;
        e.a = Vec2{x, y};
        e.meta.insert(QStringLiteral("text"), s);
        e.meta.insert(QStringLiteral("height"), height);
        return e;
    };

    add(rect(0.0, 0.0, w, h));                                  // the sheet
    add(rect(inset, inset, w - inset, h - inset));              // the border

    // The grid reference frame: numbers along the top and the bottom, letters down both sides, so
    // anything can be called out by the zone it is in. Divisions are equal, so the size asked for is
    // a target and the count is what comes nearest to it.
    if (z.enabled) {
        const int cols = templateZoneColumns(sheet, z);
        const int rows = templateZoneRows(sheet, z);
        const double cw = (w - 2.0 * inset) / cols;
        const double ch = (h - 2.0 * inset) / rows;
        const double th = std::max(1.0, z.textHeight);
        // Text hangs down and to the right of its anchor, so a label is nudged by half its own size
        // to sit in the middle of its cell. A character is about six tenths as wide as it is tall.
        auto centred = [&](double cx, double cy, const QString& s) {
            return text(cx - s.size() * th * 0.3, cy + th * 0.5, s, th);
        };
        for (int i = 0; i < cols; ++i) {
            const double x0 = inset + i * cw;
            if (i > 0) {                       // the ticks between the divisions
                add(line(x0, 0.0, x0, inset));
                add(line(x0, h - inset, x0, h));
            }
            const QString label = templateZoneColumnLabel(i);
            add(centred(x0 + cw / 2.0, inset / 2.0, label));          // along the bottom
            add(centred(x0 + cw / 2.0, h - inset / 2.0, label));      // and the top
        }
        for (int i = 0; i < rows; ++i) {
            const double y0 = inset + i * ch;
            if (i > 0) {
                add(line(0.0, y0, inset, y0));
                add(line(w - inset, y0, w, y0));
            }
            // Row 0 is the bottom of the sheet; the letters may run down from the top or up from it.
            const QString label = templateZoneRowLabel(z.lettersFromTop ? (rows - 1 - i) : i);
            add(centred(inset / 2.0, y0 + ch / 2.0, label));          // down the left
            add(centred(w - inset / 2.0, y0 + ch / 2.0, label));      // and the right
        }
    }

    // The title block: the drawing's name and number, and the three signatures. Four bands high,
    // split into a signature column and a numbering column, which is how a drawing office reads it.
    const double tbX = w - inset - tbW, tbY = inset;
    const double band = tbH / 4.0;
    const double sigW = tbW * 0.62;          // the signature column
    add(rect(tbX, tbY, w - inset, tbY + tbH));
    for (int i = 1; i < 4; ++i)
        add(line(tbX, tbY + i * band, tbX + sigW, tbY + i * band));
    add(line(tbX + sigW, tbY, tbX + sigW, tbY + tbH));           // signatures | numbering
    // THE NUMBERING COLUMN: the drawing number over a row of very small boxes - only as tall as a caption and a value.
    const double smallH = std::min(tbH / 2.0, tpl.titleBlock.captionHeight + tpl.titleBlock.valueHeight + 4.0);
    add(line(tbX + sigW, tbY + smallH, w - inset, tbY + smallH));
    // Within a signature band: what it is, who, and when.
    const double nameX = tbX + sigW * 0.34;
    const double dateX = tbX + sigW * 0.72;
    for (int i = 0; i < 3; ++i) {
        add(line(nameX, tbY + i * band, nameX, tbY + (i + 1) * band));
        add(line(dateX, tbY + i * band, dateX, tbY + (i + 1) * band));
    }

    // Text hangs DOWN from its anchor (the default justification is top-left), so each caption is
    // anchored just below the top of its own cell. Anchoring on a cell's mid-line instead put every
    // caption across the rule it was meant to sit inside.
    const double pad = 2.5;
    const double capH = tpl.titleBlock.captionHeight;
    const double valH = tpl.titleBlock.valueHeight;
    // The boxes this template offers, by key, so the title block can place the built-in ones.
    const std::vector<TemplateField> useFields =
        tpl.fields.empty() ? DrawingTemplate::defaultFields() : tpl.fields;
    auto fieldByKey = [&useFields](const char* key) -> const TemplateField* {
        for (const TemplateField& f : useFields)
            if (f.key == QLatin1String(key)) return &f;
        return nullptr;
    };
    struct Row { const char* label; const TemplateSignature* sig; };
    // Bottom band first, so approval sits at the bottom the way it is signed.
    const Row rows[] = {{"APPROVED", &tpl.approved}, {"CHECKED", &tpl.checked},
                        {"DRAWN", &tpl.drawn}};
    // EACH SIGNATURE ITS NAME AND ITS OWN DATE, boxes to fill in ("drawnBy", "drawnDate"...): the template's name and date
    // to start with, where it says them; otherwise left empty to be filled in, as an unapproved drawing should look.
    const char* sigKeys[] = {"approved", "checked", "drawn"};
    for (int i = 0; i < 3; ++i) {
        const double top = tbY + (i + 1) * band - pad;
        add(text(tbX + pad, top, QString::fromLatin1(rows[i].label), capH));
        add(text(dateX + pad, top, QStringLiteral("DATE"), capH));
        const QString key = QString::fromLatin1(sigKeys[i]);
        add(field(nameX + pad, top - 1.0, {key + QStringLiteral("By"), QString::fromLatin1(rows[i].label), rows[i].sig->name, valH, true}));
        add(field(dateX + pad, top - capH - 1.5, {key + QStringLiteral("Date"), QString::fromLatin1(rows[i].label) + QStringLiteral(" DATE"),
                                                  rows[i].sig->date, valH * 0.9, true}));
    }
    // The top band, and the numbering column beside it.
    const double titleTop = tbY + tbH - pad;
    if (const TemplateField* f = fieldByKey("title")) {
        add(text(tbX + pad, titleTop, f->label, capH));
        // The box itself, under its caption, for the drawing to type in.
        add(field(tbX + pad + 2.0, titleTop - capH - 1.5, *f));
    }
    if (const TemplateField* f = fieldByKey("drawingNo")) {
        add(text(tbX + sigW + pad, titleTop, f->label, capH));
        add(field(tbX + sigW + pad + 2.0, titleTop - capH - 1.5, *f));
    }
    // THE NUMBERING COLUMN'S FOOT: three very small boxes of their own - REV, SCALE (the template's drawing scale, to
    // start with) and SHEET (n of m) - each its caption over its value.
    {
        const double numX = tbX + sigW, numW = w - inset - numX, cellW = numW / 3.0, cellTop = tbY + smallH;
        add(line(numX + cellW, tbY, numX + cellW, cellTop));
        add(line(numX + 2.0 * cellW, tbY, numX + 2.0 * cellW, cellTop));
        TemplateField rev{QStringLiteral("revision"), QStringLiteral("REV"), QString(), valH, true};
        TemplateField scaleField{QStringLiteral("scale"), QStringLiteral("SCALE"), tpl.scale, valH, true};
        if (const TemplateField* f = fieldByKey("scale")) {
            scaleField.label = f->label;
            if (!f->value.isEmpty()) scaleField.value = f->value;
        }
        TemplateField sheetField{QStringLiteral("sheet"), QStringLiteral("SHEET"), QString(), valH, true};
        if (const TemplateField* f = fieldByKey("sheet")) sheetField.value = f->value;
        const TemplateField* cells[] = {&rev, &scaleField, &sheetField};
        for (int i = 0; i < 3; ++i) {
            if (!cells[i]) continue;
            const double x = numX + i * cellW;
            add(text(x + 1.5, cellTop - 1.2, cells[i]->label, capH * 0.85));
            TemplateField value = *cells[i];
            value.textHeight = valH * 0.9;
            add(field(x + 2.5, cellTop - 1.2 - capH - 1.0, value));
        }
    }
    // THE COMPANY BAND over the title block: the logo's box on the left - the template's logo in it, where it has one -
    // and the company's details, a free text of as many lines as it takes, beside it.
    double above = tbY + tbH;
    if (tpl.titleBlock.companyBand) {
        const double cH = std::clamp(tpl.titleBlock.companyHeightMm, 8.0, h * 0.3);
        const double logoW = std::min(cH * 1.8, tbW * 0.3);
        add(rect(tbX, above, w - inset, above + cH));
        Entity box = rect(tbX, above, tbX + logoW, above + cH);
        box.meta.insert(QStringLiteral("logoBox"), true);   // so the logo can be put in it, the template's or a sheet's
        add(box);
        if (!tpl.logo.isEmpty()) {
            Entity logo = makePictureEntity(tpl.logo, tpl.logoFormat.isEmpty() ? QStringLiteral("png") : tpl.logoFormat,
                                            QStringLiteral("Logo"));
            if (logo.type == EntityType::Image) {
                const double pw = logo.meta.value(QStringLiteral("imagePixW")).toDouble(1.0);
                const double ph = logo.meta.value(QStringLiteral("imagePixH")).toDouble(1.0);
                const double room = 1.5, bw = logoW - 2 * room, bh = cH - 2 * room;
                const double k = std::min(bw / std::max(1.0, pw), bh / std::max(1.0, ph));
                const double lw = pw * k, lh = ph * k, lx = tbX + room + (bw - lw) / 2.0, ly = above + room + (bh - lh) / 2.0;
                logo.points = {{lx, ly}, {lx + lw, ly}, {lx + lw, ly + lh}, {lx, ly + lh}};
                logo.meta.insert(QStringLiteral("logo"), true);
                add(logo);
            }
        }
        TemplateField details{QStringLiteral("company"), QStringLiteral("COMPANY"), tpl.company, valH * 0.85, true};
        Entity text = field(tbX + logoW + pad, above + cH - pad, details);
        text.meta.insert(QStringLiteral("boxWidth"), tbW - logoW - 2.0 * pad);
        text.meta.insert(QStringLiteral("boxHeight"), cH - 2.0 * pad);
        add(text);
        above += cH;
    }
    // Anything the template adds beyond the built-in boxes is stacked up the left of the title block,
    // so a field added by hand still lands somewhere sensible rather than at the origin.
    {
        double y = above + 4.0;
        for (const TemplateField& f : useFields) {
            if (f.builtIn) continue;
            add(text(tbX + pad, y + f.textHeight + 1.0, f.label, capH));
            add(field(tbX + pad + 2.0, y, f));
            y += f.textHeight + capH + 4.0;
        }
    }
    // The revision table, in the top-right corner: the heading row, then a blank row per issue. Only
    // the columns switched on are drawn, and the table is exactly as wide as those columns - a table
    // with three columns is not a seven-column one with four left blank.
    if (tpl.revisions.enabled) {
        const TemplateRevisions& rv = tpl.revisions;
        const QStringList heads = rv.headings();
        const std::vector<double> cols2 = rv.widths();
        if (!heads.isEmpty()) {
            const double rowH = std::max(3.0, rv.rowHeightMm);
            const double tw = rv.totalWidthMm();
            const int nRows = std::max(0, rv.rows) + 1;      // the heading row plus a row per issue
            const double th2 = rowH * nRows;
            const double rx = w - inset - tw;      // right-aligned inside the border
            const double ryTop = h - inset;        // grows downward from the top border
            const double ry = ryTop - th2;
            // ONE table object, not a rectangle with lines drawn over it: it behaves like every other
            // table in the application - double-click a cell to type in it, drag a grid line to resize
            // the columns or rows either side of it, and the headings are real cell text.
            Entity t = rect(rx, ry, w - inset, ryTop);
            QJsonArray colFrac, rowFrac, cells, cellAlign;
            for (int c = 0; c < heads.size(); ++c)
                colFrac.append(cols2[static_cast<size_t>(c)] / tw);
            for (int i = 0; i < nRows; ++i) rowFrac.append(1.0 / nRows);
            // Row 0 is the heading row (the top one); the rest are blank, to be filled in per issue.
            for (int i = 0; i < nRows * heads.size(); ++i) {
                cells.append(i < heads.size() ? heads.at(i) : QString());
                cellAlign.append(QStringLiteral("MC"));
            }
            t.meta.insert(QStringLiteral("table"), true);
            t.meta.insert(QStringLiteral("rows"), nRows);
            t.meta.insert(QStringLiteral("cols"), static_cast<int>(heads.size()));
            t.meta.insert(QStringLiteral("colFrac"), colFrac);
            t.meta.insert(QStringLiteral("rowFrac"), rowFrac);
            t.meta.insert(QStringLiteral("cells"), cells);
            t.meta.insert(QStringLiteral("cellAlign"), cellAlign);
            t.meta.insert(QStringLiteral("headerRow"), true);
            t.meta.insert(QStringLiteral("textSize"), rv.textHeight);
            t.meta.insert(QStringLiteral("revisionTable"), true);   // so it can be found again
            add(t);
        }
    }

    // What size it is, on the sheet, so a print says what it was drawn for. Inside the border, not
    // sitting on it.
    add(text(inset + 2.0, inset + 6.0, QStringLiteral("%1  %2 × %3 mm")
                                           .arg(sheet.name)
                                           .arg(w, 0, 'f', 0)
                                           .arg(h, 0, 'f', 0),
             3.0));
    return out;
}

} // namespace Loopwire
