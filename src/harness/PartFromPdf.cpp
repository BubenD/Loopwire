#include "harness/PartFromPdf.h"

#include "harness/PartsLibrary.h"
#include "tools/EntityTransforms.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <map>

namespace Edraw_Desktop {

namespace {

// One piece of placed text, as the table reader sees it.
struct Cell {
    QString text;
    Vec2 at;          // the anchor, bottom left
    double size = 0;  // the letter height
};

// THE PLACE A PIN HAS IN ITS COUNT: 1, 2, 3 for numbers; A, B, C for letters, where the letters a
// connector leaves out (I, O and Q, too like 1, 0 and O) may be missing without breaking the run. -1 for
// anything that is not a pin.
int pinOrdinal(const QString& raw) {
    static const QRegularExpression number(QStringLiteral("^(?:pin\\s*)?(\\d{1,3})$"),
                                           QRegularExpression::CaseInsensitiveOption);
    const QString t = raw.trimmed();
    const QRegularExpressionMatch m = number.match(t);
    if (m.hasMatch()) {
        const int n = m.captured(1).toInt();
        return n >= 1 ? n : -1;
    }
    if (t.size() == 1 && t[0] >= QLatin1Char('A') && t[0] <= QLatin1Char('Z')) return 1000 + (t[0].unicode() - 'A');
    return -1;
}

bool isLetterPin(int ordinal) { return ordinal >= 1000; }

// Whether `next` follows `prev` in a count of pins.
bool follows(int prev, int next) {
    if (isLetterPin(prev) != isLetterPin(next)) return false;
    if (!isLetterPin(prev)) return next == prev + 1;
    for (int k = prev + 1; k < next; ++k) {
        const QChar skipped(static_cast<char16_t>('A' + (k - 1000)));
        if (skipped != QLatin1Char('I') && skipped != QLatin1Char('O') && skipped != QLatin1Char('Q')) return false;
    }
    return next > prev;
}

bool isFirstPin(int ordinal) { return ordinal == 1 || ordinal == 1000; }

bool bareNumber(const QString& t) {
    static const QRegularExpression re(QStringLiteral("^[-+]?\\d+([.,]\\d+)?$"));
    return re.match(t.trimmed()).hasMatch();
}

// A pin found in the text, with the column it stands in.
struct Found {
    int ordinal = -1;
    QString pin, name;
    Vec2 at;
    double size = 0;
};

} // namespace

std::vector<PdfPin> pinoutFromText(const std::vector<Entity>& ents, const Bounds* within) {
    std::vector<Cell> cells;
    for (const Entity& e : ents) {
        if (e.type != EntityType::Text) continue;
        if (std::abs(std::remainder(e.rotation, 2.0 * M_PI)) > 0.02) continue;   // a table is read level
        const QString t = e.meta.value(QStringLiteral("text")).toString().trimmed();
        if (t.isEmpty()) continue;
        if (within && within->valid &&
            (e.a.x < within->min.x || e.a.x > within->max.x || e.a.y < within->min.y || e.a.y > within->max.y))
            continue;
        double size = e.meta.value(QStringLiteral("size")).toDouble();
        if (size <= 0.0) size = e.meta.value(QStringLiteral("height")).toDouble();
        if (size <= 0.0) size = 8.0;
        cells.push_back({t, e.a, size});
    }

    // EVERY PIN WITH ITS SIGNAL. A pin standing alone takes the nearest text to its right on the same line;
    // one written with its signal in the same run ("1 GND", "Pin 1 GND") is split where the pin ends.
    static const QRegularExpression inRun(QStringLiteral("^(?:pin\\s*)?(\\d{1,3}|[A-Z])\\s+(\\S.*)$"),
                                          QRegularExpression::CaseInsensitiveOption);
    std::vector<Found> found;
    for (size_t i = 0; i < cells.size(); ++i) {
        const Cell& c = cells[i];
        Found f;
        f.at = c.at;
        f.size = c.size;
        f.ordinal = pinOrdinal(c.text);
        if (f.ordinal >= 0) {
            f.pin = c.text.trimmed().section(QRegularExpression(QStringLiteral("\\s+")), -1);
            double best = 30.0 * c.size;
            for (size_t j = 0; j < cells.size(); ++j) {
                if (j == i) continue;
                const Cell& o = cells[j];
                const double dx = o.at.x - c.at.x;
                if (dx <= 0.0 || std::abs(o.at.y - c.at.y) > 0.5 * std::max(c.size, o.size)) continue;
                if (dx < best) {
                    best = dx;
                    f.name = o.text;
                }
            }
            // A number beside a pin is another column of figures - a wire size, a second pin - not a signal.
            if (bareNumber(f.name)) f.name.clear();
        } else {
            const QRegularExpressionMatch m = inRun.match(c.text);
            if (!m.hasMatch()) continue;
            f.ordinal = pinOrdinal(m.captured(1));
            if (f.ordinal < 0 || bareNumber(m.captured(2))) continue;
            // A lone capital in running text ("A connector ...") is a word, not a pin.
            if (isLetterPin(f.ordinal) && m.captured(2).at(0).isLower()) continue;
            f.pin = m.captured(1).toUpper();
            f.name = m.captured(2).trimmed();
        }
        if (f.ordinal >= 0) found.push_back(f);
    }
    if (found.empty()) return {};

    // INTO COLUMNS: pins standing one under another, their anchors at the same left edge.
    std::vector<std::vector<Found>> columns;
    for (const Found& f : found) {
        bool placed = false;
        for (auto& col : columns) {
            if (std::abs(col.front().at.x - f.at.x) < 1.5 * f.size &&
                isLetterPin(col.front().ordinal) == isLetterPin(f.ordinal)) {
                col.push_back(f);
                placed = true;
                break;
            }
        }
        if (!placed) columns.push_back({f});
    }
    // Each column read down the page, and kept only as the run it counts from where it starts.
    struct Run {
        std::vector<Found> pins;
        int first = -1, last = -1;
    };
    std::vector<Run> runs;
    for (auto& col : columns) {
        std::sort(col.begin(), col.end(), [](const Found& a, const Found& b) { return a.at.y > b.at.y; });
        Run run;
        for (const Found& f : col) {
            if (run.pins.empty() || follows(run.last, f.ordinal)) {
                if (run.pins.empty()) run.first = f.ordinal;
                run.pins.push_back(f);
                run.last = f.ordinal;
            } else if (run.pins.size() < 2) {
                run = Run{{f}, f.ordinal, f.ordinal};   // a stray number above the table starts nothing
            } else if (f.ordinal == run.last) {
                continue;   // the same pin again, wrapped onto a second line: the first is the one
            } else {
                runs.push_back(run);
                run = Run{{f}, f.ordinal, f.ordinal};
            }
        }
        if (!run.pins.empty()) runs.push_back(run);
    }

    // THE TABLE: the longest run counting from the first pin, carried on by any run that picks up where it
    // stops - the second column of a two-column table, the rest of it over the page.
    const Run* best = nullptr;
    for (const Run& r : runs)
        if (isFirstPin(r.first) && r.pins.size() >= 2 && (!best || r.pins.size() > best->pins.size())) best = &r;
    if (!best) return {};
    std::vector<Found> table = best->pins;
    int last = best->last;
    std::vector<const Run*> used{best};
    for (bool more = true; more;) {
        more = false;
        const Run* next = nullptr;
        for (const Run& r : runs) {
            if (std::find(used.begin(), used.end(), &r) != used.end()) continue;
            if (follows(last, r.first) && (!next || r.pins.size() > next->pins.size())) next = &r;
        }
        if (next) {
            table.insert(table.end(), next->pins.begin(), next->pins.end());
            last = next->last;
            used.push_back(next);
            more = true;
        }
    }
    std::vector<PdfPin> out;
    out.reserve(table.size());
    for (const Found& f : table) out.push_back({f.pin, f.name});
    return out;
}

std::vector<Entity> entitiesInBox(const std::vector<Entity>& ents, const Bounds& box, bool withText) {
    std::vector<Entity> out;
    if (!box.valid) return out;
    const double tol = 1e-6 * std::max(1.0, std::max(box.width(), box.height()));
    for (const Entity& e : ents) {
        if (e.type == EntityType::Text && !withText) continue;
        const Bounds b = e.type == EntityType::Text ? Bounds{e.a, e.a, true} : e.bounds();
        if (!b.valid) continue;
        if (b.min.x < box.min.x - tol || b.max.x > box.max.x + tol || b.min.y < box.min.y - tol ||
            b.max.y > box.max.y + tol)
            continue;
        out.push_back(e);
    }
    return out;
}

QJsonArray partDrawingFromPdf(const std::vector<Entity>& picked, double mmWide) {
    Bounds all;
    for (const Entity& e : picked) {
        if (e.type == EntityType::Text) continue;   // the outline sets the size, not the lettering
        const Bounds b = e.bounds();
        if (b.valid) all.expand(b);
    }
    if (!all.valid || all.width() <= 0.0 || mmWide <= 0.0) return {};
    const double k = mmWide / all.width();
    const Vec2 middle = all.center();
    std::vector<Entity> ents = picked;
    for (Entity& e : ents) {
        translateEntity(e, Vec2{-middle.x, -middle.y});
        scaleEntity(e, Vec2{0.0, 0.0}, k);
        e.color.clear();
        e.weight = 0.0;
    }
    return partDrawingFromEntities(ents);
}

double pdfPointsToMm(double points) { return points * 25.4 / 72.0; }

std::vector<PdfPin> partPinout(const LibraryPart& part) {
    std::vector<PdfPin> out;
    const QJsonDocument doc = QJsonDocument::fromJson(part.value(QStringLiteral("pinout")).toUtf8());
    for (const auto& v : doc.array()) {
        const QJsonObject o = v.toObject();
        PdfPin p{o.value(QStringLiteral("pin")).toString().trimmed(), o.value(QStringLiteral("name")).toString().trimmed()};
        if (!p.pin.isEmpty()) out.push_back(p);
    }
    return out;
}

void setPartPinout(LibraryPart& part, const std::vector<PdfPin>& pins) {
    QJsonArray arr;
    for (const PdfPin& p : pins) {
        if (p.pin.trimmed().isEmpty()) continue;
        QJsonObject o;
        o.insert(QStringLiteral("pin"), p.pin.trimmed());
        if (!p.name.trimmed().isEmpty()) o.insert(QStringLiteral("name"), p.name.trimmed());
        arr.append(o);
    }
    if (arr.isEmpty()) {
        part.params.remove(QStringLiteral("pinout"));
        return;
    }
    part.params.insert(QStringLiteral("pinout"), QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
    if (arr.size() > part.wayCount() || part.value(QStringLiteral("pins")).toInt() <= 0)
        part.params.insert(QStringLiteral("pins"), QString::number(std::max<qsizetype>(arr.size(), part.wayCount())));
}

QString pinLabelFor(const PdfPin& pin) {
    return pin.name.trimmed().isEmpty() ? pin.pin : QStringLiteral("%1 %2").arg(pin.pin, pin.name.trimmed());
}

} // namespace Edraw_Desktop
