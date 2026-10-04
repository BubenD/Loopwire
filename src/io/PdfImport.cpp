#include "io/PdfImport.h"

#include "io/Dxf.h"
#include "io/PdfText.h"
#include "model/Document.h"

#include <QFile>
#include <QHash>
#include <QUuid>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
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
    int kind = 0;   // 0 a word (operator, name, number), 1 a (literal) string, 2 a <hex> string
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
            // A STRING, kept (escapes and all) for the text it shows.
            int depth = 1;
            ++i;
            const qsizetype s0 = i;
            while (i < bytes.size() && depth > 0) {
                if (bytes.at(i) == '\\') i += 2;
                else {
                    if (bytes.at(i) == '(') ++depth;
                    if (bytes.at(i) == ')') --depth;
                    ++i;
                }
            }
            out.push_back({bytes.mid(s0, std::max<qsizetype>(0, i - 1 - s0)), false, 0.0, 1});
            continue;
        }
        if (ch == '<') {
            if (i + 1 < bytes.size() && bytes.at(i + 1) == '<') {
                out.push_back({"<<", false, 0.0});
                i += 2;
            } else {
                ++i;
                const qsizetype s0 = i;
                while (i < bytes.size() && bytes.at(i) != '>') ++i;
                out.push_back({bytes.mid(s0, i - s0), false, 0.0, 2});
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

// ---- The file's objects ----------------------------------------------------------------------------
//
// A PDF is a set of numbered objects: dictionaries, arrays, streams. A page names its content streams
// and its resources by number, and a modern PDF packs most of its small objects into compressed OBJECT
// STREAMS. Reading the drawing correctly means following those numbers - reading every stream in the
// file on its own, as this once did, draws the page's content in pieces with the drawing state thrown
// away between them, and draws the embedded fonts' binary data as though it were geometry.

// A dictionary's top-level entries, keys without their slash, values as raw text.
QHash<QByteArray, QByteArray> dictEntries(const QByteArray& text) {
    QHash<QByteArray, QByteArray> out;
    const qsizetype open = text.indexOf("<<");
    if (open < 0) return out;
    qsizetype i = open + 2;
    const qsizetype n = text.size();
    auto skipSpace = [&] {
        while (i < n && (text[i] == ' ' || text[i] == '\r' || text[i] == '\n' || text[i] == '\t' || text[i] == '\f' ||
                         text[i] == '\0'))
            ++i;
    };
    // One value from i: a balanced <<>> or [], a (string), a <hex>, a /name, or a run of words up to the
    // next delimiter - which for "12 0 R" means taking the three words together.
    auto value = [&]() -> QByteArray {
        skipSpace();
        const qsizetype s = i;
        if (i + 1 < n && text[i] == '<' && text[i + 1] == '<') {
            int depth = 0;
            while (i < n) {
                if (i + 1 < n && text[i] == '<' && text[i + 1] == '<') { depth++; i += 2; continue; }
                if (i + 1 < n && text[i] == '>' && text[i + 1] == '>') { depth--; i += 2; if (depth == 0) break; continue; }
                ++i;
            }
            return text.mid(s, i - s);
        }
        if (text[i] == '[') {
            int depth = 0;
            while (i < n) {
                if (text[i] == '[') depth++;
                else if (text[i] == ']') { depth--; if (depth == 0) { ++i; break; } }
                else if (text[i] == '(') { int d = 1; ++i; while (i < n && d > 0) { if (text[i] == '\\') ++i; else if (text[i] == '(') ++d; else if (text[i] == ')') --d; ++i; } continue; }
                ++i;
            }
            return text.mid(s, i - s);
        }
        if (text[i] == '(') {
            int d = 1;
            ++i;
            while (i < n && d > 0) {
                if (text[i] == '\\') ++i;
                else if (text[i] == '(') ++d;
                else if (text[i] == ')') --d;
                ++i;
            }
            return text.mid(s, i - s);
        }
        if (text[i] == '<') {
            while (i < n && text[i] != '>') ++i;
            if (i < n) ++i;
            return text.mid(s, i - s);
        }
        if (text[i] == '/') {
            ++i;
            while (i < n && !delimiter(text[i]) && text[i] != '/') ++i;
            return text.mid(s, i - s);
        }
        // Words: a number, "true", or an "N G R" reference.
        auto word = [&] {
            while (i < n && !delimiter(text[i]) && text[i] != '/') ++i;
        };
        word();
        const qsizetype afterFirst = i;
        skipSpace();
        const qsizetype secondStart = i;
        word();
        if (i > secondStart) {
            skipSpace();
            if (i < n && text[i] == 'R' && (i + 1 >= n || delimiter(text[i + 1]) || text[i + 1] == '/')) {
                ++i;
                return text.mid(s, i - s);
            }
        }
        i = afterFirst;
        return text.mid(s, i - s);
    };
    while (i < n) {
        skipSpace();
        if (i + 1 < n && text[i] == '>' && text[i + 1] == '>') break;
        if (text[i] != '/') { ++i; continue; }
        ++i;
        const qsizetype ks = i;
        while (i < n && !delimiter(text[i]) && text[i] != '/') ++i;
        const QByteArray key = text.mid(ks, i - ks);
        out.insert(key, value().trimmed());
    }
    return out;
}

// "12 0 R" -> 12, anything else -> -1.
int refNumber(const QByteArray& v) {
    const QList<QByteArray> w = v.simplified().split(' ');
    if (w.size() == 3 && w[2] == "R") {
        bool ok = false;
        const int n = w[0].toInt(&ok);
        if (ok) return n;
    }
    return -1;
}

// The numbers of an array value: "[1 0 0 1 20 30]" -> the six numbers.
std::vector<double> numbers(const QByteArray& v) {
    std::vector<double> out;
    for (const Token& t : tokenize(v))
        if (t.number) out.push_back(t.value);
    return out;
}

// The references of an array value: "[14 0 R 15 0 R]" -> 14, 15.
std::vector<int> references(const QByteArray& v) {
    std::vector<int> out;
    const QList<QByteArray> w = QByteArray(v).replace('[', ' ').replace(']', ' ').simplified().split(' ');
    for (qsizetype k = 0; k + 2 < w.size(); ++k)
        if (w[k + 2] == "R") {
            bool ok = false;
            const int n = w[k].toInt(&ok);
            if (ok) out.push_back(n);
            k += 2;
        }
    return out;
}

struct PdfObjects {
    QHash<int, QByteArray> text;       // each object's own text: a dictionary, an array, a value
    QHash<int, QByteArray> streamRaw;  // a stream object's bytes, as stored

    // A value, followed if it is a reference.
    QByteArray resolve(const QByteArray& v) const {
        const int r = refNumber(v);
        return r >= 0 ? text.value(r).trimmed() : v;
    }
    // A stream object's bytes, decoded - or empty when it cannot be (a filter this does not read).
    QByteArray stream(int n) const {
        if (!streamRaw.contains(n)) return {};
        const auto d = dictEntries(text.value(n));
        const QByteArray filter = resolve(d.value("Filter"));
        const QByteArray raw = streamRaw.value(n);
        if (filter.isEmpty()) return raw;
        if (filter.contains("FlateDecode") && !filter.contains("DCT") && !filter.contains("ASCII") &&
            !filter.contains("LZW"))
            return PdfText::inflate(raw, true);
        return {};
    }
};

PdfObjects readObjects(const QByteArray& bytes) {
    PdfObjects objs;
    // Every "N G obj ... endobj" in the file. A later definition of a number replaces an earlier one:
    // that is what an incremental update is.
    qsizetype pos = 0;
    while ((pos = bytes.indexOf("obj", pos)) >= 0) {
        const qsizetype at = pos;
        pos += 3;
        if (at >= 3 && bytes.mid(at - 3, 3) == "end") continue;
        if (pos < bytes.size() && !delimiter(bytes.at(pos))) continue;
        // Back over " G " and "N".
        qsizetype k = at - 1;
        auto back = [&](bool digits) {
            if (digits) {
                const qsizetype e = k;
                while (k >= 0 && std::isdigit(static_cast<unsigned char>(bytes.at(k)))) --k;
                return e - k;
            }
            const qsizetype e = k;
            while (k >= 0 && (bytes.at(k) == ' ' || bytes.at(k) == '\r' || bytes.at(k) == '\n' || bytes.at(k) == '\t')) --k;
            return e - k;
        };
        if (back(false) == 0) continue;
        if (back(true) == 0) continue;
        if (back(false) == 0) continue;
        const qsizetype numEnd = k;
        if (back(true) == 0) continue;
        bool ok = false;
        const int num = bytes.mid(k + 1, numEnd - k).toInt(&ok);
        if (!ok) continue;
        const qsizetype end = bytes.indexOf("endobj", pos);
        if (end < 0) break;
        QByteArray body = bytes.mid(pos, end - pos);
        // A stream: its dictionary, then its bytes.
        const qsizetype st = body.indexOf("stream");
        if (st >= 0 && (st < 9 || body.mid(st - 3, 3) != "end")) {
            QByteArray raw;
            if (streamBytes(bytes, pos + st, raw)) {
                objs.streamRaw.insert(num, raw);
                body = body.left(st);
            }
        }
        objs.text.insert(num, body.trimmed());
        pos = end + 6;
    }
    // The objects packed into object streams.
    const QList<int> keys = objs.text.keys();
    for (const int n : keys) {
        const auto d = dictEntries(objs.text.value(n));
        if (d.value("Type") != "/ObjStm") continue;
        const QByteArray s = objs.stream(n);
        if (s.isEmpty()) continue;
        const int count = objs.resolve(d.value("N")).toInt();
        const int first = objs.resolve(d.value("First")).toInt();
        const std::vector<Token> head = tokenize(s.left(first));
        std::vector<std::pair<int, int>> entries;   // number, offset
        for (size_t t = 0; t + 1 < head.size() && static_cast<int>(entries.size()) < count; t += 2)
            entries.emplace_back(static_cast<int>(head[t].value), static_cast<int>(head[t + 1].value));
        for (size_t e = 0; e < entries.size(); ++e) {
            const int from = first + entries[e].second;
            const int to = e + 1 < entries.size() ? first + entries[e + 1].second : static_cast<int>(s.size());
            if (!objs.text.contains(entries[e].first))
                objs.text.insert(entries[e].first, s.mid(from, to - from).trimmed());
        }
    }
    return objs;
}

// ---- Fonts: what a code says, and how far it moves the pen ------------------------------------------

struct FontInfo {
    PdfText::GlyphMap map;
    bool twoByte = false;                 // Identity-H and the other two-byte CID encodings
    double defaultWidth = 1000.0;         // a CID font's /DW
    QHash<quint32, double> widths;        // by code, in thousandths of the type size
    bool known = false;                   // the font was found at all
};

FontInfo loadFont(const PdfObjects& objs, int ref) {
    FontInfo f;
    const QByteArray text = objs.text.value(ref);
    if (text.isEmpty()) return f;
    f.known = true;
    const auto d = dictEntries(text);
    const int toUni = refNumber(d.value("ToUnicode"));
    if (toUni >= 0) f.map = PdfText::readToUnicode(objs.stream(toUni));
    const QByteArray sub = d.value("Subtype");
    if (sub == "/Type0") {
        f.twoByte = true;
        const std::vector<int> desc = references(objs.resolve(d.value("DescendantFonts")));
        if (!desc.empty()) {
            const auto cid = dictEntries(objs.text.value(desc.front()));
            if (cid.contains("DW")) f.defaultWidth = objs.resolve(cid.value("DW")).toDouble();
            // /W: "c [w1 w2 ...]" gives widths from c on; "c1 c2 w" gives one width to a range.
            const std::vector<Token> w = tokenize(objs.resolve(cid.value("W")));
            size_t k = 0;
            if (!w.empty() && w[0].text == "[") k = 1;
            while (k < w.size()) {
                if (!w[k].number) { ++k; continue; }
                const quint32 c = static_cast<quint32>(w[k].value);
                ++k;
                if (k < w.size() && w[k].text == "[") {
                    ++k;
                    quint32 code = c;
                    while (k < w.size() && w[k].text != "]") {
                        if (w[k].number) f.widths.insert(code++, w[k].value);
                        ++k;
                    }
                    ++k;
                } else if (k + 1 < w.size() && w[k].number && w[k + 1].number) {
                    const quint32 c2 = static_cast<quint32>(w[k].value);
                    const double width = w[k + 1].value;
                    for (quint32 code = c; code <= c2 && code - c < 65536; ++code) f.widths.insert(code, width);
                    k += 2;
                }
            }
        }
    } else {
        const int first = objs.resolve(d.value("FirstChar")).toInt();
        const std::vector<double> ws = numbers(objs.resolve(d.value("Widths")));
        for (size_t k = 0; k < ws.size(); ++k) f.widths.insert(static_cast<quint32>(first + static_cast<int>(k)), ws[k]);
        if (f.map.empty()) {
            const QByteArray enc = objs.resolve(d.value("Encoding"));
            if (enc.contains("Differences")) f.map = PdfText::readDifferences(enc);
        }
    }
    return f;
}

// A run of shown text: where it starts and ends on its baseline, what it says, how tall and how turned.
struct TextPiece {
    Vec2 start, end;
    QString text;
    double height = 0.0;
    double rotation = 0.0;   // radians
    QString color;
};

// ---- One content stream -----------------------------------------------------------------------------

struct Context {
    const PdfObjects* objs = nullptr;
    int depth = 0;
    QHash<int, FontInfo>* fonts = nullptr;     // loaded once per file, by object number
    std::vector<TextPiece>* texts = nullptr;   // where the shown text goes
};

void parseStream(const QByteArray& stream, Document& doc, const Matrix& base = {}, const Context& ctx = {},
                 const QByteArray& resources = {}) {
    const std::vector<Token> tokens = tokenize(stream);
    std::vector<double> nums;
    struct GraphicsState {
        Matrix matrix;
        QString strokeColor = QStringLiteral("#000000");
        QString fillColor = QStringLiteral("#000000");
        // The text state, which q and Q keep with the rest.
        double charSpace = 0.0, wordSpace = 0.0, hScale = 1.0, leading = 0.0, rise = 0.0, fontSize = 12.0;
        int fontRef = -1;
    };
    // The text and text-line matrices, which BT starts afresh.
    Matrix tm, tlm;
    // A TJ array being gathered: its strings and its numbers, in order.
    struct ArrayItem {
        QByteArray bytes;
        bool isString = false;
        double number = 0.0;
    };
    std::vector<ArrayItem> array;
    bool inArray = false;
    std::vector<QByteArray> strings;   // strings since the last operator
    std::vector<GraphicsState> states;
    GraphicsState state;
    state.matrix = base;
    std::vector<Subpath> path;
    Vec2 current{};
    bool haveCurrent = false;
    QByteArray lastName;

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

    // THE FONT in force, looked up in the resources by the name Tf gave.
    auto fontByName = [&](const QByteArray& name) -> int {
        if (!ctx.objs) return -1;
        const auto res = dictEntries(ctx.objs->resolve(resources));
        const auto fonts = dictEntries(ctx.objs->resolve(res.value("Font")));
        return refNumber(fonts.value(name));
    };
    auto fontInfo = [&]() -> const FontInfo* {
        if (!ctx.fonts || !ctx.objs || state.fontRef < 0) return nullptr;
        auto it = ctx.fonts->find(state.fontRef);
        if (it == ctx.fonts->end()) it = ctx.fonts->insert(state.fontRef, loadFont(*ctx.objs, state.fontRef));
        return &it.value();
    };
    // SHOWING TEXT: each code set at the pen, the pen moved on by the code's width, and the run kept as
    // one piece - where it starts and ends, what it says, how tall and which way it reads.
    auto show = [&](const QByteArray& bytes) {
        if (!ctx.texts || bytes.isEmpty()) return;
        const FontInfo* font = fontInfo();
        const bool two = font && (font->twoByte || font->map.codeBytes >= 2);
        const int step = two ? 2 : 1;
        const Matrix m = multiply(state.matrix, tm);
        const Vec2 origin = m.map({0.0, state.rise});
        QString said;
        double advance = 0.0;   // in text space
        for (qsizetype k = 0; k + step <= bytes.size(); k += step) {
            quint32 code = 0;
            for (int b = 0; b < step; ++b) code = (code << 8) | static_cast<unsigned char>(bytes.at(k + b));
            const QString ch = PdfText::mapCode(code, font ? &font->map : nullptr);
            said += ch;
            double w = 0.0;
            if (font && font->widths.contains(code)) w = font->widths.value(code);
            else if (font && font->twoByte) w = font->defaultWidth;
            else w = PdfText::estimateWidth(ch.isEmpty() ? QChar(' ') : ch.at(0));
            double dx = w / 1000.0 * state.fontSize + state.charSpace;
            if (step == 1 && code == 32) dx += state.wordSpace;
            advance += dx * state.hScale;
        }
        tm = multiply(tm, {1, 0, 0, 1, advance, 0});
        const Vec2 right = m.map({1.0, 0.0}) - m.map({0.0, 0.0});
        const Vec2 up = m.map({0.0, 1.0}) - m.map({0.0, 0.0});
        TextPiece piece;
        piece.start = origin;
        piece.end = m.map({advance, state.rise});
        piece.text = said;
        piece.height = state.fontSize * up.length();
        piece.rotation = std::atan2(right.y, right.x);
        piece.color = state.fillColor;
        if (piece.height > 1e-6) ctx.texts->push_back(std::move(piece));
    };
    auto nextLine = [&](double tx, double ty) {
        tlm = multiply(tlm, {1, 0, 0, 1, tx, ty});
        tm = tlm;
    };

    for (const Token& token : tokens) {
        if (token.number) {
            nums.push_back(token.value);
            if (inArray) array.push_back({QByteArray(), false, token.value});
            continue;
        }
        if (token.kind != 0) {
            const QByteArray bytes = token.kind == 1 ? PdfText::unescapeLiteral(token.text) : PdfText::unhex(token.text);
            if (inArray) array.push_back({bytes, true, 0.0});
            else strings.push_back(bytes);
            continue;
        }
        const QByteArray& op = token.text;
        if (op == "[") { inArray = true; array.clear(); continue; }
        if (op == "]") { inArray = false; continue; }
        if (op.startsWith('/')) {
            lastName = op.mid(1);
            nums.clear();
            continue;
        }
        // THE TEXT OPERATORS.
        if (op == "BT") { tm = tlm = Matrix{}; nums.clear(); strings.clear(); continue; }
        if (op == "ET") { nums.clear(); strings.clear(); continue; }
        {
            std::vector<double> t;
            bool handled = true;
            if (op == "Tf" && take(1, t)) { state.fontSize = t[0]; state.fontRef = fontByName(lastName); }
            else if (op == "Tc" && take(1, t)) state.charSpace = t[0];
            else if (op == "Tw" && take(1, t)) state.wordSpace = t[0];
            else if (op == "Tz" && take(1, t)) state.hScale = t[0] / 100.0;
            else if (op == "TL" && take(1, t)) state.leading = t[0];
            else if (op == "Ts" && take(1, t)) state.rise = t[0];
            else if (op == "Td" && take(2, t)) nextLine(t[0], t[1]);
            else if (op == "TD" && take(2, t)) { state.leading = -t[1]; nextLine(t[0], t[1]); }
            else if (op == "Tm" && take(6, t)) { tlm = {t[0], t[1], t[2], t[3], t[4], t[5]}; tm = tlm; }
            else if (op == "T*") nextLine(0.0, -state.leading);
            else if (op == "Tj") { for (const QByteArray& s : strings) show(s); }
            else if (op == "'") { nextLine(0.0, -state.leading); for (const QByteArray& s : strings) show(s); }
            else if (op == "\"") {
                if (take(2, t)) { state.wordSpace = t[0]; state.charSpace = t[1]; }
                nextLine(0.0, -state.leading);
                for (const QByteArray& s : strings) show(s);
            } else if (op == "TJ") {
                for (const ArrayItem& item : array) {
                    if (item.isString) show(item.bytes);
                    else tm = multiply(tm, {1, 0, 0, 1, -item.number / 1000.0 * state.fontSize * state.hScale, 0});
                }
                array.clear();
            } else handled = false;
            if (handled) { nums.clear(); strings.clear(); continue; }
        }
        strings.clear();
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
        } else if (op == "n") {
            // THE PATH ENDS UNDRAWN - it was a clipping boundary ("... re W n"). Left standing, it was
            // drawn by whatever painted next, and clip boxes turned up on the drawing as rectangles.
            path.clear();
            haveCurrent = false;
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
        } else if (op == "Do") {
            // A FORM XOBJECT: a piece of drawing kept once and placed here, through its own matrix on top
            // of the one in force. Pictures are passed over.
            nums.clear();
            if (!ctx.objs || ctx.depth >= 8 || lastName.isEmpty()) continue;
            const auto res = dictEntries(ctx.objs->resolve(resources));
            const auto xobjects = dictEntries(ctx.objs->resolve(res.value("XObject")));
            const int ref = refNumber(xobjects.value(lastName));
            if (ref < 0) continue;
            const auto form = dictEntries(ctx.objs->text.value(ref));
            if (form.value("Subtype") != "/Form") continue;
            Matrix m;
            const std::vector<double> mv = numbers(ctx.objs->resolve(form.value("Matrix")));
            if (mv.size() == 6) m = {mv[0], mv[1], mv[2], mv[3], mv[4], mv[5]};
            const QByteArray formRes = form.contains("Resources") ? form.value("Resources") : resources;
            Context inner = ctx;
            inner.depth = ctx.depth + 1;
            parseStream(ctx.objs->stream(ref), doc, multiply(state.matrix, m), inner, formRes);
        } else {
            nums.clear();
        }
    }
}

// ---- The pages --------------------------------------------------------------------------------------

struct Page {
    QByteArray contents;     // the /Contents value
    QByteArray resources;    // the /Resources value, inherited when the page has none of its own
    std::vector<double> box; // CropBox, else MediaBox: x0 y0 x1 y1
    int rotate = 0;
};

void collectPages(const PdfObjects& objs, int node, Page inherited, std::vector<Page>& out, int depth) {
    if (depth > 32) return;
    const auto d = dictEntries(objs.text.value(node));
    if (d.contains("Resources")) inherited.resources = d.value("Resources");
    if (d.contains("MediaBox")) inherited.box = numbers(objs.resolve(d.value("MediaBox")));
    if (d.contains("CropBox")) {
        const std::vector<double> crop = numbers(objs.resolve(d.value("CropBox")));
        if (crop.size() == 4) inherited.box = crop;
    }
    if (d.contains("Rotate")) inherited.rotate = objs.resolve(d.value("Rotate")).toInt();
    if (d.value("Type") == "/Pages" || d.contains("Kids")) {
        for (const int kid : references(objs.resolve(d.value("Kids")))) collectPages(objs, kid, inherited, out, depth + 1);
        return;
    }
    inherited.contents = d.value("Contents");
    out.push_back(inherited);
}

std::vector<Page> findPages(const PdfObjects& objs) {
    std::vector<Page> pages;
    // From the catalog down, in the order the pages are in.
    for (auto it = objs.text.cbegin(); it != objs.text.cend(); ++it) {
        const auto d = dictEntries(it.value());
        if (d.value("Type") != "/Catalog") continue;
        const int root = refNumber(d.value("Pages"));
        if (root >= 0) collectPages(objs, root, Page{}, pages, 0);
        if (!pages.empty()) return pages;
    }
    // No catalog to be found: every page object, by number.
    QList<int> nums = objs.text.keys();
    std::sort(nums.begin(), nums.end());
    for (const int n : nums) {
        const auto d = dictEntries(objs.text.value(n));
        if (d.value("Type") == "/Page") collectPages(objs, n, Page{}, pages, 0);
    }
    return pages;
}

// The page's content, all its streams joined: they are ONE stream cut into pieces, often in the middle of
// an operation, and the drawing state carries across the cuts.
QByteArray pageContent(const PdfObjects& objs, const Page& page) {
    QByteArray all;
    std::vector<int> parts;
    const int single = refNumber(page.contents);
    if (single >= 0) {
        const QByteArray target = objs.text.value(single).trimmed();
        if (target.startsWith('[')) parts = references(target);   // a reference to an array of streams
        else parts.push_back(single);
    } else {
        parts = references(page.contents);
    }
    for (const int n : parts) {
        all += objs.stream(n);
        all += '\n';
    }
    return all;
}

// Where a page's point is on the sheet: the page box's corner at the origin, and turned the way the page
// is SHOWN - a page stored upright with /Rotate 90 is read landscape, and so is its drawing.
Vec2 pageToSheet(const Vec2& p, const Page& page) {
    const double x0 = page.box.size() == 4 ? std::min(page.box[0], page.box[2]) : 0.0;
    const double y0 = page.box.size() == 4 ? std::min(page.box[1], page.box[3]) : 0.0;
    const double w = page.box.size() == 4 ? std::abs(page.box[2] - page.box[0]) : 0.0;
    const double h = page.box.size() == 4 ? std::abs(page.box[3] - page.box[1]) : 0.0;
    const double x = p.x - x0, y = p.y - y0;
    switch (((page.rotate % 360) + 360) % 360) {
    case 90: return {y, w - x};
    case 180: return {w - x, h - y};
    case 270: return {h - y, x};
    default: return {x, y};
    }
}

// THE PIECES OF TEXT JOINED into words and lines. A PDF sets text a word or a letter at a time - and a
// kerned line comes as a string per pair of letters - so each piece on its own is useless on a drawing.
// Pieces that follow one another along the same baseline, the same size and turn, are one text; a gap
// the width of a space between them is a space.
std::vector<TextPiece> joinPieces(const std::vector<TextPiece>& pieces) {
    // First the runs - pieces that follow on along one baseline - each with the gap before every piece.
    struct Run {
        std::vector<const TextPiece*> parts;
        std::vector<double> gaps;   // gaps[k] is the gap before parts[k], k >= 1
    };
    std::vector<Run> runs;
    for (const TextPiece& p : pieces) {
        if (!runs.empty()) {
            Run& run = runs.back();
            const TextPiece& last = *run.parts.back();
            const double h = last.height;
            const Vec2 dir{std::cos(last.rotation), std::sin(last.rotation)};
            const Vec2 d = p.start - last.end;
            const double along = d.x * dir.x + d.y * dir.y;
            const double across = -d.x * dir.y + d.y * dir.x;
            const bool same = std::abs(p.height - h) <= 0.08 * h && std::abs(p.rotation - last.rotation) < 0.02 &&
                              p.color == last.color;
            if (same && std::abs(across) < 0.25 * h && along > -0.6 * h && along < 1.4 * h) {
                run.parts.push_back(&p);
                run.gaps.push_back(along);
                continue;
            }
        }
        if (p.text.trimmed().isEmpty()) continue;   // a space on its own starts nothing
        runs.push_back({{&p}, {0.0}});
    }
    // Then the words. A gap is a space when it is wider than a space's worth of the type - and wider than
    // what the run does between every letter anyway: a line set letter by letter with the letters spread
    // has a gap after each one, and those are tracking, not spaces.
    std::vector<TextPiece> out;
    for (const Run& run : runs) {
        TextPiece t = *run.parts.front();
        const double h = t.height;
        std::vector<double> inner(run.gaps.begin() + 1, run.gaps.end());
        double usual = 0.0;
        if (inner.size() >= 3) {
            std::vector<double> sorted = inner;
            std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
            usual = std::max(0.0, sorted[sorted.size() / 2]);
        }
        const double spaceAt = std::max(0.18 * h, usual + 0.15 * h);
        for (size_t k = 1; k < run.parts.size(); ++k) {
            const TextPiece& p = *run.parts[k];
            if (run.gaps[k] > spaceAt && !t.text.endsWith(QLatin1Char(' ')) && !p.text.startsWith(QLatin1Char(' ')))
                t.text += QLatin1Char(' ');
            t.text += p.text;
            t.end = p.end;
        }
        t.text = t.text.trimmed();
        if (!t.text.isEmpty()) out.push_back(std::move(t));
    }
    return out;
}

// A piece of text as a text object: its anchor the bottom-left of its letters, a little under the
// baseline where the descenders reach, so the baseline lands where the PDF put it.
Entity textEntity(const TextPiece& t) {
    Entity e;
    e.type = EntityType::Text;
    e.layer = QStringLiteral("0");
    e.color = t.color.isEmpty() ? QStringLiteral("#000000") : t.color;
    const Vec2 up{-std::sin(t.rotation), std::cos(t.rotation)};
    e.a = t.start - up * (0.21 * t.height);
    e.rotation = t.rotation;
    e.meta.insert(QStringLiteral("text"), t.text);
    e.meta.insert(QStringLiteral("size"), t.height);
    e.meta.insert(QStringLiteral("justify"), QStringLiteral("BL"));
    return e;
}

// A stream that is not a drawing at all, for the reading of a file with no page tree to follow: fonts,
// colour profiles, metadata, pictures, the file's own indexes.
bool notDrawing(const QHash<QByteArray, QByteArray>& d) {
    if (d.contains("Length1") || d.contains("Length2") || d.contains("Length3")) return true;   // font programs
    const QByteArray type = d.value("Type"), sub = d.value("Subtype");
    if (type == "/ObjStm" || type == "/XRef" || type == "/Metadata" || type == "/EmbeddedFile") return true;
    if (sub == "/XML" || sub == "/Image" || sub == "/Type1C" || sub == "/CIDFontType0C" || sub == "/OpenType")
        return true;
    if (d.contains("N") && d.contains("Alternate")) return true;   // an ICC profile
    return false;
}

} // namespace

bool parse(const QByteArray& bytes, Document& doc, QString* error, const Progress& progress,
           std::vector<Bounds>* pageBoxes) {
    if (pageBoxes) pageBoxes->clear();
    if (!PdfText::looksLikePdf(bytes)) {
        if (error) *error = QStringLiteral("The selected file is not a PDF.");
        return false;
    }

    // Parse into a temporary document. Importing a bad or unsupported PDF must
    // not destroy the drawing that was already open in the caller.
    Document extracted;
    extracted.entities().clear();
    std::vector<Entity> texts;   // the text, placed; added after the lines have been normalised
    QHash<int, FontInfo> fonts;
    int streams = 0;
    if (progress) progress(5, QStringLiteral("Reading the PDF's objects"));
    const PdfObjects objs = readObjects(bytes);
    const std::vector<Page> pages = findPages(objs);
    if (!pages.empty()) {
        // PAGE BY PAGE, as a viewer draws them: each page's content read as the one stream it is, its
        // pieces placed where the page puts them, turned the way the page is shown, and nothing kept
        // that lies wholly outside the page - which a viewer would not show either. Further pages are
        // laid to the right of the first.
        double offsetX = 0.0;
        for (size_t pi = 0; pi < pages.size(); ++pi) {
            const Page& page = pages[pi];
            if (progress)
                progress(10 + static_cast<int>(60.0 * pi / pages.size()),
                         QStringLiteral("Reading page %1 of %2").arg(pi + 1).arg(pages.size()));
            Document one;
            one.entities().clear();
            Context ctx;
            ctx.objs = &objs;
            ctx.fonts = &fonts;
            std::vector<TextPiece> pieces;
            ctx.texts = &pieces;
            parseStream(pageContent(objs, page), one, Matrix{}, ctx, page.resources);
            ++streams;
            Bounds box;
            if (page.box.size() == 4) {
                box.expand({page.box[0], page.box[1]});
                box.expand({page.box[2], page.box[3]});
                const double tol = 1.0;
                box.min.x -= tol; box.min.y -= tol; box.max.x += tol; box.max.y += tol;
            }
            double pageWidth = 0.0;
            if (page.box.size() == 4) {
                const bool sideways = ((page.rotate % 180) + 180) % 180 == 90;
                pageWidth = sideways ? std::abs(page.box[3] - page.box[1]) : std::abs(page.box[2] - page.box[0]);
            }
            for (Entity& e : one.entities()) {
                if (box.valid) {
                    const Bounds eb = e.bounds();
                    if (eb.valid && (eb.max.x < box.min.x || eb.min.x > box.max.x || eb.max.y < box.min.y ||
                                     eb.min.y > box.max.y))
                        continue;   // off the page: not part of what the page shows
                }
                if (page.box.size() == 4) {
                    auto place = [&](Vec2& p) { p = pageToSheet(p, page) + Vec2{offsetX, 0.0}; };
                    place(e.a);
                    place(e.b);
                    for (Vec2& p : e.points) place(p);
                }
                extracted.entities().push_back(std::move(e));
            }
            // THE TEXT, on the same terms: on the page, turned with it.
            const int turns = page.box.size() == 4 ? (((page.rotate % 360) + 360) % 360) / 90 : 0;
            for (const TextPiece& t : joinPieces(pieces)) {
                if (box.valid && (t.start.x < box.min.x || t.start.x > box.max.x || t.start.y < box.min.y ||
                                  t.start.y > box.max.y))
                    continue;
                Entity e = textEntity(t);
                if (page.box.size() == 4) {
                    e.a = pageToSheet(e.a, page) + Vec2{offsetX, 0.0};
                    e.rotation -= turns * M_PI / 2.0;
                }
                texts.push_back(std::move(e));
            }
            if (pageBoxes) {
                Bounds onSheet;
                if (page.box.size() == 4)
                    for (const Vec2& corner : {Vec2{page.box[0], page.box[1]}, Vec2{page.box[2], page.box[1]},
                                               Vec2{page.box[2], page.box[3]}, Vec2{page.box[0], page.box[3]}})
                        onSheet.expand(pageToSheet(corner, page) + Vec2{offsetX, 0.0});
                pageBoxes->push_back(onSheet);
            }
            offsetX += pageWidth > 0.0 ? pageWidth + 20.0 : 0.0;
        }
    } else {
        // NO PAGE TREE to follow - a fragment, or a file this cannot index. Every stream that could be a
        // drawing is read, which is how this always worked; the ones that certainly are not (fonts,
        // metadata, pictures, indexes) are left alone, since read as geometry they draw nonsense.
        qsizetype cursor = 0;
        int lastSaid = -1;
        while ((cursor = bytes.indexOf("stream", cursor)) >= 0) {
            if (progress && !bytes.isEmpty()) {
                const int pct = static_cast<int>(70.0 * double(cursor) / double(bytes.size()));
                if (pct != lastSaid) {
                    lastSaid = pct;
                    progress(pct, QStringLiteral("Reading the PDF's paths"));
                }
            }
            if (cursor >= 3 && bytes.mid(cursor - 3, 3) == "end") { cursor += 6; continue; }
            QByteArray stream;
            if (!streamBytes(bytes, cursor, stream)) break;
            const qsizetype objectStart = bytes.lastIndexOf("obj", cursor);
            const QByteArray dict = objectStart >= 0 ? bytes.mid(objectStart, cursor - objectStart) : QByteArray();
            const auto entries = dictEntries(dict);
            if (!notDrawing(entries)) {
                if (dict.contains("/FlateDecode")) stream = PdfText::inflate(stream, true);
                if (!stream.isEmpty() && !stream.startsWith("/CIDInit")) {
                    parseStream(stream, extracted);
                    ++streams;
                }
            }
            cursor += 6;
        }
    }
    if (extracted.entities().empty() && texts.empty()) {
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
    if (!extracted.entities().empty() && !dxf::parse(converted, dxf, &dxfError)) {
        if (error) *error = dxfError;
        return false;
    }
    if (extracted.entities().empty()) converted.entities().clear();
    // The text goes straight in: through a DXF it would be pressed into the file's older character set.
    for (Entity& t : texts) {
        t.id = QUuid::createUuid().toString(QUuid::WithoutBraces).left(8);
        converted.entities().push_back(std::move(t));
    }
    doc = std::move(converted);
    if (progress) progress(100, QStringLiteral("Read"));
    return true;
}

bool importFile(Document& doc, const QString& path, QString* error, const Progress& progress,
                std::vector<Bounds>* pages) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return false;
    }
    if (progress) progress(0, QStringLiteral("Reading the file"));
    return parse(file.readAll(), doc, error, progress, pages);
}

} // namespace Edraw_Desktop::pdf
