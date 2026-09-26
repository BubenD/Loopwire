#include "io/PdfImport.h"

#include "io/Dxf.h"
#include "io/PdfText.h"
#include "model/Document.h"

#include <QFile>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <vector>

namespace Edraw_Desktop::pdf {
namespace {

struct Matrix {
    double a = 1.0, b = 0.0, c = 0.0, d = 1.0, e = 0.0, f = 0.0;

    Vec2 map(const Vec2& p) const { return {a * p.x + c * p.y + e, b * p.x + d * p.y + f}; }
};

Matrix multiply(const Matrix& x, const Matrix& y) {
    return {x.a * y.a + x.c * y.b, x.b * y.a + x.d * y.b,
            x.a * y.c + x.c * y.d, x.b * y.c + x.d * y.d,
            x.a * y.e + x.c * y.f + x.e, x.b * y.e + x.d * y.f + x.f};
}

struct Token {
    QByteArray text;
    bool number = false;
    double value = 0.0;
};

bool delimiter(char c) {
    return c == 0 || c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '(' || c == ')' ||
           c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}';
}

std::vector<Token> tokenize(const QByteArray& bytes) {
    std::vector<Token> out;
    qsizetype i = 0;
    while (i < bytes.size()) {
        const char ch = bytes.at(i);
        if (ch == '%') {
            while (i < bytes.size() && bytes.at(i) != '\n') ++i;
            continue;
        }
        if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
            ++i;
            continue;
        }
        if (ch == '(') {
            int depth = 1;
            ++i;
            while (i < bytes.size() && depth > 0) {
                if (bytes.at(i) == '\\') i += 2;
                else {
                    if (bytes.at(i) == '(') ++depth;
                    if (bytes.at(i) == ')') --depth;
                    ++i;
                }
            }
            continue;
        }
        if (ch == '<') {
            if (i + 1 < bytes.size() && bytes.at(i + 1) == '<') {
                out.push_back({"<<", false, 0.0});
                i += 2;
            } else {
                ++i;
                while (i < bytes.size() && bytes.at(i) != '>') ++i;
                if (i < bytes.size()) ++i;
            }
            continue;
        }
        if (ch == '[' || ch == ']') {
            out.push_back({QByteArray(1, ch), false, 0.0});
            ++i;
            continue;
        }
        qsizetype start = i;
        while (i < bytes.size() && !delimiter(bytes.at(i))) ++i;
        if (i == start) {
            // A delimiter that nothing above consumed: a stray ')', the '>' that ends a hex string, a
            // '{' or '}' from a PostScript function, a NUL in a stream. Taken as one character and
            // passed over.
            //
            // Without this the loop makes NO PROGRESS on such a byte - the character is a delimiter, so
            // the run-of-non-delimiters is empty, so `i` never moves - and it appends an empty token for
            // ever until the process runs out of memory. Which is what happened: a 23 kB PDF took the
            // application down with a fail-fast instead of being read, or being refused politely.
            ++i;
            continue;
        }
        const QByteArray text = bytes.mid(start, i - start);
        bool ok = false;
        const double value = text.toDouble(&ok);
        out.push_back({text, ok, value});
    }
    return out;
}

struct Subpath {
    std::vector<Vec2> points;
    bool closed = false;
};

void appendEntity(Document& doc, const std::vector<Vec2>& points, bool closed,
                  const QString& color) {
    if (points.size() < 2) return;
    Entity e;
    e.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
    e.layer = QStringLiteral("0");
    e.color = color.isEmpty() ? QStringLiteral("#000000") : color;
    if (points.size() == 2 && !closed) {
        e.type = EntityType::Line;
        e.a = points[0];
        e.b = points[1];
    } else {
        e.type = EntityType::Polyline;
        e.points = points;
        if (closed && e.points.front() != e.points.back()) e.points.push_back(e.points.front());
    }
    doc.entities().push_back(std::move(e));
}

void paintPath(Document& doc, std::vector<Subpath>& path, const QString& color) {
    for (const Subpath& sub : path) appendEntity(doc, sub.points, sub.closed, color);
    path.clear();
}

QString rgbColor(double r, double g, double b) {
    const auto byte = [](double value) {
        return std::clamp(static_cast<int>(std::lround(value * 255.0)), 0, 255);
    };
    return QStringLiteral("#%1%2%3")
        .arg(byte(r), 2, 16, QLatin1Char('0'))
        .arg(byte(g), 2, 16, QLatin1Char('0'))
        .arg(byte(b), 2, 16, QLatin1Char('0'));
}

QString cmykColor(double c, double m, double y, double k) {
    return rgbColor((1.0 - std::clamp(c + k, 0.0, 1.0)),
                    (1.0 - std::clamp(m + k, 0.0, 1.0)),
                    (1.0 - std::clamp(y + k, 0.0, 1.0)));
}

bool streamBytes(const QByteArray& pdf, qsizetype at, QByteArray& out) {
    qsizetype start = at + 6;
    while (start < pdf.size() && (pdf.at(start) == '\r' || pdf.at(start) == '\n')) ++start;
    const qsizetype end = pdf.indexOf("endstream", start);
    if (end < 0) return false;
    out = pdf.mid(start, end - start);
    while (!out.isEmpty() && (out.endsWith('\n') || out.endsWith('\r'))) out.chop(1);
    return true;
}

void parseStream(const QByteArray& stream, Document& doc) {
    const std::vector<Token> tokens = tokenize(stream);
    std::vector<double> nums;
    struct GraphicsState {
        Matrix matrix;
        QString strokeColor = QStringLiteral("#000000");
        QString fillColor = QStringLiteral("#000000");
    };
    std::vector<GraphicsState> states;
    GraphicsState state;
    std::vector<Subpath> path;
    Vec2 current{};
    bool haveCurrent = false;

    auto take = [&](int n, std::vector<double>& values) {
        if (static_cast<int>(nums.size()) < n) return false;
        values.assign(nums.end() - n, nums.end());
        nums.clear();
        return true;
    };
    auto point = [&](double x, double y) { return state.matrix.map({x, y}); };
    auto start = [&](const Vec2& p) {
        path.push_back({{p}, false});
        current = p;
        haveCurrent = true;
    };

    for (const Token& token : tokens) {
        if (token.number) {
            nums.push_back(token.value);
            continue;
        }
        const QByteArray& op = token.text;
        if (op == "q") { states.push_back(state); nums.clear(); continue; }
        if (op == "Q") {
            if (!states.empty()) { state = states.back(); states.pop_back(); }
            nums.clear();
            continue;
        }
        std::vector<double> v;
        if (op == "RG" && take(3, v)) {
            state.strokeColor = rgbColor(v[0], v[1], v[2]);
        } else if (op == "rg" && take(3, v)) {
            state.fillColor = rgbColor(v[0], v[1], v[2]);
        } else if ((op == "G" || op == "g") && take(1, v)) {
            const QString color = rgbColor(v[0], v[0], v[0]);
            if (op == "G") state.strokeColor = color;
            else state.fillColor = color;
        } else if (op == "K" && take(4, v)) {
            state.strokeColor = cmykColor(v[0], v[1], v[2], v[3]);
        } else if (op == "k" && take(4, v)) {
            state.fillColor = cmykColor(v[0], v[1], v[2], v[3]);
        } else if (op == "cm" && take(6, v)) {
            state.matrix = multiply(state.matrix, {v[0], v[1], v[2], v[3], v[4], v[5]});
        } else if (op == "m" && take(2, v)) {
            start(point(v[0], v[1]));
        } else if (op == "l" && take(2, v)) {
            const Vec2 p = point(v[0], v[1]);
            if (!haveCurrent) start(p);
            else { path.back().points.push_back(p); current = p; }
        } else if (op == "c" && take(6, v)) {
            const Vec2 p0 = haveCurrent ? current : point(v[0], v[1]);
            const Vec2 p1 = point(v[0], v[1]);
            const Vec2 p2 = point(v[2], v[3]);
            const Vec2 p3 = point(v[4], v[5]);
            if (!haveCurrent) start(p0);
            for (int i = 1; i <= 16; ++i) {
                const double t = i / 16.0, u = 1.0 - t;
                path.back().points.push_back({u * u * u * p0.x + 3 * u * u * t * p1.x +
                                                   3 * u * t * t * p2.x + t * t * t * p3.x,
                                               u * u * u * p0.y + 3 * u * u * t * p1.y +
                                                   3 * u * t * t * p2.y + t * t * t * p3.y});
            }
            current = p3;
        } else if (op == "v" && take(4, v)) {
            if (!haveCurrent) start(point(v[0], v[1]));
            const Vec2 p0 = current, p1 = p0, p2 = point(v[0], v[1]), p3 = point(v[2], v[3]);
            for (int i = 1; i <= 16; ++i) {
                const double t = i / 16.0, u = 1.0 - t;
                path.back().points.push_back({u * u * u * p0.x + 3 * u * u * t * p1.x +
                                                   3 * u * t * t * p2.x + t * t * t * p3.x,
                                               u * u * u * p0.y + 3 * u * u * t * p1.y +
                                                   3 * u * t * t * p2.y + t * t * t * p3.y});
            }
            current = p3;
        } else if (op == "y" && take(4, v)) {
            if (!haveCurrent) start(point(v[0], v[1]));
            const Vec2 p0 = current, p1 = point(v[0], v[1]), p2 = point(v[2], v[3]), p3 = p2;
            for (int i = 1; i <= 16; ++i) {
                const double t = i / 16.0, u = 1.0 - t;
                path.back().points.push_back({u * u * u * p0.x + 3 * u * u * t * p1.x +
                                                   3 * u * t * t * p2.x + t * t * t * p3.x,
                                               u * u * u * p0.y + 3 * u * u * t * p1.y +
                                                   3 * u * t * t * p2.y + t * t * t * p3.y});
            }
            current = p3;
        } else if (op == "re" && take(4, v)) {
            const Vec2 p0 = point(v[0], v[1]);
            const Vec2 p1 = point(v[0] + v[2], v[1]);
            const Vec2 p2 = point(v[0] + v[2], v[1] + v[3]);
            const Vec2 p3 = point(v[0], v[1] + v[3]);
            path.push_back({{p0, p1, p2, p3}, true});
            current = p0;
            haveCurrent = true;
        } else if (op == "h") {
            if (!path.empty()) {
                path.back().closed = true;
                if (!path.back().points.empty()) current = path.back().points.front();
            }
            nums.clear();
        } else if (op == "S" || op == "s" || op == "f" || op == "F" || op == "f*" ||
                   op == "B" || op == "B*" || op == "b" || op == "b*") {
            const bool closes = op == "s" || op == "b" || op == "b*" || op == "f" ||
                                op == "F" || op == "f*" || op == "B" || op == "B*";
            if (closes)
                for (Subpath& sub : path) {
                    sub.closed = true;
                    if (!sub.points.empty() && sub.points.front() != sub.points.back())
                        sub.points.push_back(sub.points.front());
                }
            const bool stroke = op == "S" || op == "s" || op == "B" || op == "B*" ||
                                op == "b" || op == "b*";
            paintPath(doc, path, stroke ? state.strokeColor : state.fillColor);
            haveCurrent = false;
        } else {
            nums.clear();
        }
    }
}

} // namespace

bool parse(const QByteArray& bytes, Document& doc, QString* error, const Progress& progress) {
    if (!PdfText::looksLikePdf(bytes)) {
        if (error) *error = QStringLiteral("The selected file is not a PDF.");
        return false;
    }

    // Parse into a temporary document. Importing a bad or unsupported PDF must
    // not destroy the drawing that was already open in the caller.
    Document extracted;
    extracted.entities().clear();
    qsizetype cursor = 0;
    int streams = 0;
    int lastSaid = -1;
    while ((cursor = bytes.indexOf("stream", cursor)) >= 0) {
        // HOW FAR THROUGH THE FILE: the reading is most of the work, so it is most of the bar.
        if (progress && !bytes.isEmpty()) {
            const int pct = static_cast<int>(70.0 * double(cursor) / double(bytes.size()));
            if (pct != lastSaid) {
                lastSaid = pct;
                progress(pct, QStringLiteral("Reading the PDF's paths"));
            }
        }
        QByteArray stream;
        if (!streamBytes(bytes, cursor, stream)) break;
        const qsizetype objectStart = bytes.lastIndexOf("obj", cursor);
        const QByteArray dict = objectStart >= 0 ? bytes.mid(objectStart, cursor - objectStart) : QByteArray();
        if (dict.contains("/FlateDecode")) stream = PdfText::inflate(stream, true);
        if (!stream.isEmpty()) {
            parseStream(stream, extracted);
            ++streams;
        }
        cursor += 6;
    }
    if (extracted.entities().empty()) {
        if (error) *error = streams == 0
                                  ? QStringLiteral("The PDF contains no readable content streams.")
                                  : QStringLiteral("The PDF contains no supported vector paths.");
        return false;
    }

    // Keep the same conversion boundary as a normal PDF -> DXF -> drawing
    // import. That way PDF geometry gets the application's established DXF
    // normalization, IDs, layers, and entity representations before it is
    // appended to the active document by Editor::appendEntities().
    if (progress) progress(72, QStringLiteral("Normalising %1 object(s)").arg(extracted.entities().size()));
    const QString dxf = dxf::toDxf(extracted);
    if (progress) progress(86, QStringLiteral("Normalising %1 object(s)").arg(extracted.entities().size()));
    Document converted;
    QString dxfError;
    if (!dxf::parse(converted, dxf, &dxfError)) {
        if (error) *error = dxfError;
        return false;
    }
    doc = std::move(converted);
    if (progress) progress(100, QStringLiteral("Read"));
    return true;
}

bool importFile(Document& doc, const QString& path, QString* error, const Progress& progress) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    if (progress) progress(0, QStringLiteral("Reading the file"));
    return parse(file.readAll(), doc, error, progress);
}

} // namespace Edraw_Desktop::pdf
