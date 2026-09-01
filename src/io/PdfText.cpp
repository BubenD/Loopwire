#include "io/PdfText.h"

#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>
#include <vector>

namespace Edraw_Desktop {
namespace PdfText {

// ==================================================================================================
// DEFLATE (RFC 1951), and the zlib wrapper around it (RFC 1950)
//
// Written out here rather than linked, so this works whatever a build has to hand: Qt's qUncompress
// needs the uncompressed length in front of the data, which a PDF stream does not carry, and zlib's own
// headers are not part of a Qt install. It is the whole format - stored, fixed-Huffman and
// dynamic-Huffman blocks - because a PDF producer may use any of them.
//
// ATTRIBUTION. The canonical-Huffman decoder below follows the approach of "puff.c", the reference
// DEFLATE decoder written by Mark Adler and distributed with zlib (Copyright (C) 2002-2013 Mark Adler).
// This is an ALTERED version and is marked as such here, as the zlib licence requires: it is a fresh
// C++ implementation against RFC 1951 rather than a copy, and no zlib code ships with Edraw. The
// requirement the licence puts on us is that the origin not be misrepresented - hence this paragraph,
// which is repeated for the user in THIRD-PARTY-NOTICES.txt. Do not delete it.
// ==================================================================================================

namespace {

class BitReader {
public:
    explicit BitReader(const QByteArray& in) : d_(in) {}

    // One bit, LSB first within each byte, as DEFLATE requires.
    int bit() {
        if (pos_ >= d_.size()) { bad_ = true; return 0; }
        const int b = (static_cast<unsigned char>(d_.at(pos_)) >> nbit_) & 1;
        if (++nbit_ == 8) { nbit_ = 0; ++pos_; }
        return b;
    }
    // `n` bits, the first read being the least significant.
    unsigned bits(int n) {
        unsigned v = 0;
        for (int i = 0; i < n; ++i) v |= static_cast<unsigned>(bit()) << i;
        return v;
    }
    void alignToByte() {
        if (nbit_ != 0) { nbit_ = 0; ++pos_; }
    }
    bool bad() const { return bad_; }
    qsizetype bytePos() const { return pos_; }
    void skipBytes(qsizetype n) { pos_ += n; }
    const QByteArray& data() const { return d_; }

private:
    const QByteArray& d_;
    qsizetype pos_ = 0;
    int nbit_ = 0;
    bool bad_ = false;
};

// A canonical Huffman decoder built from code lengths, per RFC 1951 3.2.2. Counts-and-symbols decoding
// after puff.c - see the attribution at the top of this file.
class Huffman {
public:
    bool build(const std::vector<int>& lengths) {
        counts_.assign(16, 0);
        symbols_.assign(lengths.size(), 0);
        for (int len : lengths) {
            if (len < 0 || len > 15) return false;
            ++counts_[static_cast<size_t>(len)];
        }
        counts_[0] = 0;   // a length of zero means "this symbol is not used"
        std::vector<int> offsets(16, 0);
        for (int i = 1; i < 16; ++i) offsets[static_cast<size_t>(i)] =
            offsets[static_cast<size_t>(i - 1)] + counts_[static_cast<size_t>(i - 1)];
        for (size_t s = 0; s < lengths.size(); ++s)
            if (lengths[s] != 0)
                symbols_[static_cast<size_t>(offsets[static_cast<size_t>(lengths[s])]++)] =
                    static_cast<int>(s);
        return true;
    }

    // -1 on malformed input, so a corrupt stream stops rather than looping.
    int decode(BitReader& br) const {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= 15; ++len) {
            code |= br.bit();
            const int count = counts_[static_cast<size_t>(len)];
            if (code - first < count) return symbols_[static_cast<size_t>(index + (code - first))];
            index += count;
            first = (first + count) << 1;
            code <<= 1;
            if (br.bad()) return -1;
        }
        return -1;
    }

private:
    std::vector<int> counts_;
    std::vector<int> symbols_;
};

// The length/distance tables of RFC 1951 3.2.5.
const unsigned kLenBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                               31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const int kLenExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                           2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const unsigned kDistBase[30] = {1,    2,    3,    4,    5,    7,     9,     13,    17,   25,
                                33,   49,   65,   97,   129,  193,   257,   385,   513,  769,
                                1025, 1537, 2049, 3073, 4097, 6145,  8193,  12289, 16385, 24577};
const int kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                            6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool inflateBlocks(BitReader& br, QByteArray& out) {
    Huffman fixedLit, fixedDist;
    {
        std::vector<int> lit(288);
        for (int i = 0; i < 144; ++i) lit[static_cast<size_t>(i)] = 8;
        for (int i = 144; i < 256; ++i) lit[static_cast<size_t>(i)] = 9;
        for (int i = 256; i < 280; ++i) lit[static_cast<size_t>(i)] = 7;
        for (int i = 280; i < 288; ++i) lit[static_cast<size_t>(i)] = 8;
        fixedLit.build(lit);
        fixedDist.build(std::vector<int>(30, 5));
    }
    for (;;) {
        const int last = br.bit();
        const unsigned type = br.bits(2);
        if (br.bad()) return false;
        if (type == 0) {
            // Stored: a length, its complement, then the bytes themselves.
            br.alignToByte();
            const unsigned len = br.bits(16);
            br.bits(16);   // ~len, not checked: a wrong one is no reason to lose the text
            if (br.bad()) return false;
            const qsizetype at = br.bytePos();
            if (at + static_cast<qsizetype>(len) > br.data().size()) return false;
            out.append(br.data().mid(at, static_cast<qsizetype>(len)));
            br.skipBytes(static_cast<qsizetype>(len));
        } else if (type == 1 || type == 2) {
            Huffman dynLit, dynDist;
            const Huffman* lit = &fixedLit;
            const Huffman* dist = &fixedDist;
            if (type == 2) {
                // The two code tables are themselves Huffman-coded, in this order.
                static const int kOrder[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                               11, 4,  12, 3, 13, 2, 14, 1, 15};
                const unsigned nlit = br.bits(5) + 257;
                const unsigned ndist = br.bits(5) + 1;
                const unsigned ncode = br.bits(4) + 4;
                if (br.bad() || nlit > 288 || ndist > 32) return false;
                std::vector<int> codeLens(19, 0);
                for (unsigned i = 0; i < ncode; ++i)
                    codeLens[static_cast<size_t>(kOrder[i])] = static_cast<int>(br.bits(3));
                Huffman codeHuff;
                if (!codeHuff.build(codeLens)) return false;
                std::vector<int> lens;
                lens.reserve(nlit + ndist);
                while (lens.size() < nlit + ndist) {
                    const int sym = codeHuff.decode(br);
                    if (sym < 0) return false;
                    if (sym < 16) {
                        lens.push_back(sym);
                    } else if (sym == 16) {   // repeat the previous length 3..6 times
                        if (lens.empty()) return false;
                        const int prev = lens.back();
                        const unsigned n = 3 + br.bits(2);
                        for (unsigned i = 0; i < n; ++i) lens.push_back(prev);
                    } else if (sym == 17) {   // 3..10 zeros
                        const unsigned n = 3 + br.bits(3);
                        for (unsigned i = 0; i < n; ++i) lens.push_back(0);
                    } else {                  // 11..138 zeros
                        const unsigned n = 11 + br.bits(7);
                        for (unsigned i = 0; i < n; ++i) lens.push_back(0);
                    }
                    if (br.bad()) return false;
                }
                if (lens.size() > nlit + ndist) return false;
                if (!dynLit.build(std::vector<int>(lens.begin(), lens.begin() + nlit))) return false;
                if (!dynDist.build(std::vector<int>(lens.begin() + nlit, lens.end()))) return false;
                lit = &dynLit;
                dist = &dynDist;
            }
            for (;;) {
                const int sym = lit->decode(br);
                if (sym < 0) return false;
                if (sym < 256) {
                    out.append(static_cast<char>(sym));
                    continue;
                }
                if (sym == 256) break;   // end of block
                const int li = sym - 257;
                if (li >= 29) return false;
                const unsigned length =
                    kLenBase[li] + br.bits(kLenExtra[li]);
                const int di = dist->decode(br);
                if (di < 0 || di >= 30) return false;
                const unsigned back = kDistBase[di] + br.bits(kDistExtra[di]);
                if (br.bad() || back == 0 || static_cast<qsizetype>(back) > out.size()) return false;
                // Overlapping copies are legal and common, so this must be byte by byte.
                const qsizetype from = out.size() - static_cast<qsizetype>(back);
                for (unsigned i = 0; i < length; ++i) out.append(out.at(from + static_cast<qsizetype>(i)));
                if (out.size() > (1 << 28)) return false;   // a runaway stream, not a datasheet
            }
        } else {
            return false;   // type 3 is not a thing
        }
        if (last) return true;
        if (br.bad()) return false;
    }
}

} // namespace

QByteArray inflate(const QByteArray& in, bool zlibHeader) {
    if (in.isEmpty()) return {};
    QByteArray body = in;
    if (zlibHeader) {
        if (body.size() < 2) return {};
        const unsigned char cmf = static_cast<unsigned char>(body.at(0));
        const unsigned char flg = static_cast<unsigned char>(body.at(1));
        // A zlib header is a deflate method (8) whose two bytes check out. Anything else is taken to
        // be a raw stream, because some producers write one where a zlib stream is declared.
        if ((cmf & 0x0f) == 8 && ((cmf << 8) | flg) % 31 == 0)
            body = body.mid((flg & 0x20) ? 6 : 2);   // FDICT adds four more bytes
    }
    QByteArray out;
    BitReader br(body);
    if (!inflateBlocks(br, out)) {
        // Whatever was recovered before it went wrong is still worth having: a truncated stream is
        // common in the wild, and half a datasheet beats none.
        return out;
    }
    return out;
}

// ==================================================================================================
// The PDF itself
// ==================================================================================================

namespace {

// The value of a simple /Key in a dictionary fragment: a name, a number or a reference.
QString dictValue(const QByteArray& dict, const char* key) {
    const int at = dict.indexOf(key);
    if (at < 0) return {};
    int i = at + static_cast<int>(qstrlen(key));
    while (i < dict.size() && (dict.at(i) == ' ' || dict.at(i) == '\r' || dict.at(i) == '\n')) ++i;
    const int start = i;
    while (i < dict.size() && dict.at(i) != '/' && dict.at(i) != '>' && dict.at(i) != '\r' &&
           dict.at(i) != '\n' && dict.at(i) != ']')
        ++i;
    return QString::fromLatin1(dict.mid(start, i - start)).trimmed();
}

// One stream object of the file: the dictionary in front of it and the raw bytes between the stream
// and endstream keywords.
struct RawStream {
    QByteArray dict;
    QByteArray data;
    int objNum = -1;
};

// Every stream in the file, found by scanning rather than by following the cross-reference table: a
// datasheet is often produced by a tool that leaves the xref slightly wrong, and a linear scan finds
// the streams anyway.
std::vector<RawStream> streamsOf(const QByteArray& pdf) {
    std::vector<RawStream> out;
    int at = 0;
    while ((at = pdf.indexOf("stream", at)) >= 0) {
        // "endstream" contains "stream": skip those.
        if (at >= 3 && pdf.mid(at - 3, 3) == "end") { at += 6; continue; }
        // The dictionary is what precedes it, back to the object header.
        int dictFrom = pdf.lastIndexOf("obj", at);
        if (dictFrom < 0) dictFrom = std::max(0, at - 2048);
        RawStream s;
        s.dict = pdf.mid(dictFrom, at - dictFrom);
        // Which object it is, for /Length given as a reference and for ToUnicode lookups.
        const int hdr = pdf.lastIndexOf("obj", at);
        if (hdr > 0) {
            int i = hdr - 1;
            while (i >= 0 && (pdf.at(i) == ' ' || pdf.at(i) == '\r' || pdf.at(i) == '\n')) --i;
            while (i >= 0 && pdf.at(i) >= '0' && pdf.at(i) <= '9') --i;   // the generation
            while (i >= 0 && (pdf.at(i) == ' ' || pdf.at(i) == '\r' || pdf.at(i) == '\n')) --i;
            const int numEnd = i + 1;
            while (i >= 0 && pdf.at(i) >= '0' && pdf.at(i) <= '9') --i;
            s.objNum = pdf.mid(i + 1, numEnd - i - 1).trimmed().toInt();
        }
        // The data starts after the keyword and its end-of-line.
        int from = at + 6;
        if (from < pdf.size() && pdf.at(from) == '\r') ++from;
        if (from < pdf.size() && pdf.at(from) == '\n') ++from;
        const int end = pdf.indexOf("endstream", from);
        if (end < 0) break;
        int to = end;
        // Trim the end-of-line that belongs to the keyword, not to the data.
        if (to > from && pdf.at(to - 1) == '\n') --to;
        if (to > from && pdf.at(to - 1) == '\r') --to;
        s.data = pdf.mid(from, to - from);
        out.push_back(std::move(s));
        at = end + 9;
    }
    return out;
}

// A stream's bytes, decompressed if they are Flate. Anything else (JPEG image data, LZW, JBIG2) is
// left alone - it is not text and the text scan will simply find nothing in it.
QByteArray streamContent(const RawStream& s) {
    const bool flate = s.dict.contains("FlateDecode");
    if (!flate) return s.data;
    return inflate(s.data, true);
}

// A PDF literal string: escapes per the specification, including \\ooo octal and a backslash at a line
// end meaning "no break here".
QByteArray decodeLiteral(const QByteArray& in) {
    QByteArray out;
    out.reserve(in.size());
    for (int i = 0; i < in.size(); ++i) {
        const char c = in.at(i);
        if (c != '\\') { out.append(c); continue; }
        if (++i >= in.size()) break;
        const char e = in.at(i);
        switch (e) {
        case 'n': out.append('\n'); break;
        case 'r': out.append('\r'); break;
        case 't': out.append('\t'); break;
        case 'b': out.append('\b'); break;
        case 'f': out.append('\f'); break;
        case '(': out.append('('); break;
        case ')': out.append(')'); break;
        case '\\': out.append('\\'); break;
        case '\r':
            if (i + 1 < in.size() && in.at(i + 1) == '\n') ++i;
            break;   // a continued line
        case '\n': break;
        default:
            if (e >= '0' && e <= '7') {
                int v = e - '0';
                for (int k = 0; k < 2 && i + 1 < in.size() && in.at(i + 1) >= '0' &&
                                in.at(i + 1) <= '7';
                     ++k)
                    v = v * 8 + (in.at(++i) - '0');
                out.append(static_cast<char>(v & 0xff));
            } else {
                out.append(e);   // an escape of something that needs none
            }
        }
    }
    return out;
}

QByteArray decodeHexString(const QByteArray& in) {
    QByteArray hex;
    for (char c : in)
        if (isxdigit(static_cast<unsigned char>(c))) hex.append(c);
    if (hex.size() % 2) hex.append('0');   // an odd digit count means a trailing zero
    return QByteArray::fromHex(hex);
}

// A font's /ToUnicode CMap, in the form producers actually write: runs of beginbfchar/endbfchar and
// beginbfrange/endbfrange. This is what makes a subset-encoded font readable - without it the bytes
// are glyph numbers and the text comes out as nonsense.
struct CMap {
    QHash<quint32, QString> single;
    int codeBytes = 1;   // 1 for a simple font, 2 for Identity-H and friends
    bool empty() const { return single.isEmpty(); }
};

QString utf16beToString(const QByteArray& be) {
    QString s;
    for (int i = 0; i + 1 < be.size(); i += 2) {
        const ushort u = static_cast<ushort>((static_cast<unsigned char>(be.at(i)) << 8) |
                                             static_cast<unsigned char>(be.at(i + 1)));
        s.append(QChar(u));
    }
    return s;
}

// A PostScript glyph name as the character it draws.
//
// This is the other way a PDF says what its bytes mean, and on a datasheet it is the COMMON way: a
// subset font with /Encoding << /Differences [0 /space /percent /quoteright /parenleft ... ] >>, so byte
// 0 is a space, byte 1 a per-cent sign, and so on. There is no /ToUnicode to be had - the names are the
// only record of what the glyphs are - and read as plain bytes the text comes out as "$ 2 ' 0 (" which
// is what a reader without this does with such a file.
QString glyphNameToChar(const QString& name) {
    static const QHash<QString, QChar> kNames = {
        {QStringLiteral("space"), QLatin1Char(' ')},
        {QStringLiteral("exclam"), QLatin1Char('!')},
        {QStringLiteral("quotedbl"), QLatin1Char('"')},
        {QStringLiteral("numbersign"), QLatin1Char('#')},
        {QStringLiteral("dollar"), QLatin1Char('$')},
        {QStringLiteral("percent"), QLatin1Char('%')},
        {QStringLiteral("ampersand"), QLatin1Char('&')},
        {QStringLiteral("quotesingle"), QLatin1Char('\'')},
        {QStringLiteral("quoteright"), QLatin1Char('\'')},
        {QStringLiteral("quoteleft"), QLatin1Char('\'')},
        {QStringLiteral("parenleft"), QLatin1Char('(')},
        {QStringLiteral("parenright"), QLatin1Char(')')},
        {QStringLiteral("asterisk"), QLatin1Char('*')},
        {QStringLiteral("plus"), QLatin1Char('+')},
        {QStringLiteral("comma"), QLatin1Char(',')},
        {QStringLiteral("hyphen"), QLatin1Char('-')},
        {QStringLiteral("period"), QLatin1Char('.')},
        {QStringLiteral("slash"), QLatin1Char('/')},
        {QStringLiteral("zero"), QLatin1Char('0')},
        {QStringLiteral("one"), QLatin1Char('1')},
        {QStringLiteral("two"), QLatin1Char('2')},
        {QStringLiteral("three"), QLatin1Char('3')},
        {QStringLiteral("four"), QLatin1Char('4')},
        {QStringLiteral("five"), QLatin1Char('5')},
        {QStringLiteral("six"), QLatin1Char('6')},
        {QStringLiteral("seven"), QLatin1Char('7')},
        {QStringLiteral("eight"), QLatin1Char('8')},
        {QStringLiteral("nine"), QLatin1Char('9')},
        {QStringLiteral("colon"), QLatin1Char(':')},
        {QStringLiteral("semicolon"), QLatin1Char(';')},
        {QStringLiteral("less"), QLatin1Char('<')},
        {QStringLiteral("equal"), QLatin1Char('=')},
        {QStringLiteral("greater"), QLatin1Char('>')},
        {QStringLiteral("question"), QLatin1Char('?')},
        {QStringLiteral("at"), QLatin1Char('@')},
        {QStringLiteral("bracketleft"), QLatin1Char('[')},
        {QStringLiteral("backslash"), QLatin1Char('\\')},
        {QStringLiteral("bracketright"), QLatin1Char(']')},
        {QStringLiteral("underscore"), QLatin1Char('_')},
        {QStringLiteral("braceleft"), QLatin1Char('{')},
        {QStringLiteral("bar"), QLatin1Char('|')},
        {QStringLiteral("braceright"), QLatin1Char('}')},
        {QStringLiteral("quotedblleft"), QChar(0x201C)},
        {QStringLiteral("quotedblright"), QChar(0x201D)},
        {QStringLiteral("emdash"), QChar(0x2014)},
        {QStringLiteral("endash"), QChar(0x2013)},
        {QStringLiteral("plusminus"), QChar(0x00B1)},
        {QStringLiteral("degree"), QChar(0x00B0)},
        {QStringLiteral("bullet"), QChar(0x2022)},
        {QStringLiteral("mu"), QChar(0x00B5)},
        {QStringLiteral("Omega"), QChar(0x03A9)},
        {QStringLiteral("ohm"), QChar(0x2126)},
        {QStringLiteral("multiply"), QChar(0x00D7)},
        {QStringLiteral("divide"), QChar(0x00F7)},
        {QStringLiteral("fi"), QLatin1Char('f')},   // a ligature: better one letter than none
        {QStringLiteral("fl"), QLatin1Char('f')},
    };
    if (name.isEmpty()) return {};
    const auto it = kNames.constFind(name);
    if (it != kNames.constEnd()) return QString(*it);
    // A single letter is its own name: /A, /b.
    if (name.size() == 1) return name;
    // uniXXXX, and the uXXXX form.
    if (name.startsWith(QLatin1String("uni")) && name.size() >= 7) {
        bool ok = false;
        const uint v = name.mid(3, 4).toUInt(&ok, 16);
        if (ok) return QString(QChar(static_cast<ushort>(v)));
    }
    if (name.startsWith(QLatin1Char('u')) && name.size() >= 5) {
        bool ok = false;
        const uint v = name.mid(1).toUInt(&ok, 16);
        if (ok && v < 0x10000) return QString(QChar(static_cast<ushort>(v)));
    }
    return {};
}

// An /Encoding object's /Differences array as a code -> character map.
CMap parseDifferences(const QByteArray& dict) {
    CMap out;
    out.codeBytes = 1;
    const int at = dict.indexOf("/Differences");
    if (at < 0) return out;
    const int open = dict.indexOf('[', at);
    const int close = dict.indexOf(']', open);
    if (open < 0 || close < 0) return out;
    const QString body = QString::fromLatin1(dict.mid(open + 1, close - open - 1));
    // "code /name /name /name code /name ..." - a number sets the code, each name takes the next one.
    static const QRegularExpression token(QStringLiteral("(\\d+)|/([^\\s/\\]]+)"));
    quint32 code = 0;
    auto it = token.globalMatch(body);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        if (!m.captured(1).isEmpty()) {
            code = m.captured(1).toUInt();
            continue;
        }
        const QString ch = glyphNameToChar(m.captured(2));
        if (!ch.isEmpty()) out.single.insert(code, ch);
        ++code;
    }
    return out;
}

CMap parseCMap(const QByteArray& cmap) {
    CMap out;
    // The code space tells us how many bytes a code is. <0000> <FFFF> means two.
    static const QRegularExpression codespace(
        QStringLiteral("begincodespacerange\\s*<([0-9A-Fa-f]+)>"));
    const QRegularExpressionMatch cs = codespace.match(QString::fromLatin1(cmap));
    if (cs.hasMatch())
        out.codeBytes = std::max<int>(1, static_cast<int>(cs.captured(1).size() / 2));

    // <src> <dst>
    static const QRegularExpression bfchar(
        QStringLiteral("<([0-9A-Fa-f]+)>\\s*<([0-9A-Fa-f]*)>"));
    int at = 0;
    const QString text = QString::fromLatin1(cmap);
    while ((at = text.indexOf(QStringLiteral("beginbfchar"), at)) >= 0) {
        const int end = text.indexOf(QStringLiteral("endbfchar"), at);
        if (end < 0) break;
        auto it = bfchar.globalMatch(text.mid(at, end - at));
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            const quint32 src = m.captured(1).toUInt(nullptr, 16);
            const QString dst = utf16beToString(QByteArray::fromHex(m.captured(2).toLatin1()));
            if (!dst.isEmpty()) out.single.insert(src, dst);
        }
        at = end;
    }
    // <lo> <hi> <dstStart>, or <lo> <hi> [ <d1> <d2> ... ]
    static const QRegularExpression bfrange(
        QStringLiteral("<([0-9A-Fa-f]+)>\\s*<([0-9A-Fa-f]+)>\\s*(<[0-9A-Fa-f]*>|\\[[^\\]]*\\])"));
    at = 0;
    while ((at = text.indexOf(QStringLiteral("beginbfrange"), at)) >= 0) {
        const int end = text.indexOf(QStringLiteral("endbfrange"), at);
        if (end < 0) break;
        auto it = bfrange.globalMatch(text.mid(at, end - at));
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            const quint32 lo = m.captured(1).toUInt(nullptr, 16);
            const quint32 hi = m.captured(2).toUInt(nullptr, 16);
            const QString dst = m.captured(3);
            if (hi < lo || hi - lo > 65535) continue;
            if (dst.startsWith(QLatin1Char('['))) {
                static const QRegularExpression one(QStringLiteral("<([0-9A-Fa-f]*)>"));
                auto jt = one.globalMatch(dst);
                quint32 code = lo;
                while (jt.hasNext() && code <= hi) {
                    const QString s =
                        utf16beToString(QByteArray::fromHex(jt.next().captured(1).toLatin1()));
                    if (!s.isEmpty()) out.single.insert(code, s);
                    ++code;
                }
            } else {
                const QByteArray start = QByteArray::fromHex(
                    dst.mid(1, dst.size() - 2).toLatin1());
                const QString base = utf16beToString(start);
                if (base.isEmpty()) continue;
                // The last character of the destination counts up with the source code.
                for (quint32 code = lo; code <= hi; ++code) {
                    QString s = base;
                    s[s.size() - 1] = QChar(static_cast<ushort>(s.at(s.size() - 1).unicode() +
                                                                (code - lo)));
                    out.single.insert(code, s);
                }
            }
        }
        at = end;
    }
    return out;
}

// How wide a character is, in thousandths of the type size, as Helvetica sets it.
//
// The pen has to be tracked across a line to tell a space from a continued word, and that means knowing
// how far each character moves it. A flat guess of half the type size is not good enough: an "m" is 833
// and an "i" is 222, so "Diameter" set with a flat estimate leaves the pen well short of where it really
// is and the next piece of text looks like a new column - which is how "Diameter" came out as
// "Diam eter". Helvetica's own widths (and Arial's, which match) put every letter within a few per cent,
// and that is what a datasheet is set in.
int helveticaWidth(QChar c) {
    static const short kW[95] = {
        278, 278, 355, 556, 556, 889, 667, 191, 333, 333, 389, 584, 278, 333, 278, 278,  // space..
        556, 556, 556, 556, 556, 556, 556, 556, 556, 556,                                // 0-9
        278, 278, 584, 584, 584, 556, 1015,                                              // :;<=>?@
        667, 667, 722, 722, 667, 611, 778, 722, 278, 500, 667, 556, 833, 722, 778,        // A-O
        667, 778, 722, 667, 611, 722, 667, 944, 667, 667, 611,                            // P-Z
        278, 278, 278, 469, 556, 333,                                                     // [\]^_`
        556, 556, 500, 556, 556, 278, 556, 556, 222, 222, 500, 222, 833, 556, 556,        // a-o
        556, 556, 333, 500, 278, 556, 500, 722, 500, 500, 500,                            // p-z
        334, 260, 334, 584};                                                              // {|}~
    const ushort u = c.unicode();
    if (u >= 32 && u <= 126) return kW[u - 32];
    return 600;   // anything else: a middling width, which is all an estimate needs
}

// Bytes of a shown string turned into characters, through the font's map when there is one.
QString mapString(const QByteArray& bytes, const CMap* cmap) {
    if (!cmap || cmap->empty()) {
        // No map: the bytes are Latin-1-ish character codes, which is what a standard font gives.
        QString s;
        for (char c : bytes) {
            const unsigned char u = static_cast<unsigned char>(c);
            s.append(u >= 32 || u == '\n' || u == '\t' ? QChar(u) : QChar(' '));
        }
        return s;
    }
    QString out;
    const int step = std::clamp(cmap->codeBytes, 1, 2);
    for (int i = 0; i + step <= bytes.size(); i += step) {
        quint32 code = 0;
        for (int k = 0; k < step; ++k)
            code = (code << 8) | static_cast<unsigned char>(bytes.at(i + k));
        const auto it = cmap->single.constFind(code);
        if (it != cmap->single.constEnd()) out.append(*it);
        else if (step == 1 && code >= 32) out.append(QChar(static_cast<ushort>(code)));
    }
    return out;
}

// The text of one content stream. Only the text-showing operators are of interest; the rest of the
// language - paths, images, colours - is skipped past.
//
// Line breaks: a PDF has no lines, only text placed at coordinates. Td/TD/T*/TL and a Tm with a new
// vertical position all start a new line in practice, and treating them so is what makes the output
// readable and what lets a "Rated power: 250 mW" survive as one line.
QString textOfContent(const QByteArray& content, const QHash<QString, CMap>& fonts) {
    QString out;
    const CMap* font = nullptr;
    // Where the text is being written, so a line break can be told from a space.
    //
    // A PDF has no lines: every piece of text is placed at a coordinate, and a producer may well place
    // every single GLYPH separately - which is what this datasheet does. Treating each placement as a
    // new line turns the page into one character per line and no words at all; treating none of them as
    // a line break runs the whole page together. So the position is tracked: a change in y is a new
    // line, a jump in x on the same line is a space, and text that carries on where the last piece
    // ended is the same word.
    double lineX = 0.0, lineY = 0.0;   // the start of the current line (the text line matrix)
    double curX = 0.0, curY = 0.0;     // where the next piece of text goes
    double size = 10.0, leading = 0.0;
    double penX = 0.0, penY = 0.0;     // where the last piece of text ended
    bool wrote = false;
    auto show = [&](const QByteArray& raw, bool hex) {
        const QString s = mapString(hex ? decodeHexString(raw) : decodeLiteral(raw), font);
        if (s.isEmpty()) return;
        if (wrote) {
            if (std::abs(curY - penY) > std::max(0.6, size * 0.4)) {
                out += QLatin1Char('\n');
            } else if (curX - penX > size * 0.32) {
                // A gap wider than a third of the type size is a space - between two columns of a
                // table it is a wide one, but one space is enough to keep the figures apart.
                if (!out.endsWith(QLatin1Char(' '))) out += QLatin1Char(' ');
            }
        }
        out += s;
        wrote = true;
        // Where the pen is now, by the widths of the characters just drawn.
        double advance = 0.0;
        for (QChar c : s) advance += helveticaWidth(c) / 1000.0;
        penX = curX + advance * size;
        penY = curY;
        curX = penX;
    };
    // The numbers an operator was given, most recent last.
    auto numbersOnStack = [](const QList<QByteArray>& stack, int howMany) {
        QList<double> out;
        for (int i = stack.size() - 1; i >= 0 && out.size() < howMany; --i) {
            const QByteArray& s = stack.at(i);
            if (s.startsWith('(') || s.startsWith('<') || s.startsWith('/') || s == "[" || s == "]")
                break;
            bool ok = false;
            const double v = s.toDouble(&ok);
            if (!ok) break;
            out.prepend(v);
        }
        return out;
    };
    // A tiny operand stack: the last few tokens, so an operator can look back at its arguments.
    QList<QByteArray> stack;
    int i = 0;
    const int n = content.size();
    auto isDelim = [](char c) {
        return c == ' ' || c == '\r' || c == '\n' || c == '\t' || c == '\f' || c == 0;
    };
    while (i < n) {
        const char c = content.at(i);
        if (c == '(') {
            // A literal string, to its matching close paren, minding escapes and nesting.
            int depth = 1, j = i + 1;
            QByteArray raw;
            while (j < n && depth > 0) {
                const char d = content.at(j);
                if (d == '\\' && j + 1 < n) { raw.append(d).append(content.at(j + 1)); j += 2; continue; }
                if (d == '(') ++depth;
                if (d == ')') { if (--depth == 0) break; }
                raw.append(d);
                ++j;
            }
            stack.append(QByteArray("(") + raw);
            i = j + 1;
            continue;
        }
        if (c == '<' && i + 1 < n && content.at(i + 1) != '<') {
            const int j = content.indexOf('>', i);
            if (j < 0) break;
            stack.append(QByteArray("<") + content.mid(i + 1, j - i - 1));
            i = j + 1;
            continue;
        }
        if (c == '[' || c == ']' || c == '{' || c == '}') {
            stack.append(QByteArray(1, c));
            ++i;
            continue;
        }
        if (c == '<' || (c == '>' && i + 1 < n && content.at(i + 1) == '>')) {
            // A dictionary inside the content stream (a BDC property list, say): skip it.
            i += 2;
            continue;
        }
        if (isDelim(c)) { ++i; continue; }
        int j = i;
        while (j < n && !isDelim(content.at(j)) && content.at(j) != '(' && content.at(j) != '[' &&
               content.at(j) != ']' && content.at(j) != '<' && content.at(j) != '/')
            ++j;
        if (j == i) {   // a name (/F1) or a stray delimiter
            if (c == '/') {
                int k = i + 1;
                while (k < n && !isDelim(content.at(k)) && content.at(k) != '/' &&
                       content.at(k) != '[' && content.at(k) != '(' && content.at(k) != '<')
                    ++k;
                stack.append(content.mid(i, k - i));
                i = k;
                continue;
            }
            ++i;
            continue;
        }
        const QByteArray tok = content.mid(i, j - i);
        i = j;

        if (tok == "Tj" || tok == "'" || tok == "\"") {
            // ' and " move to the next line first, then show.
            if (tok != "Tj") {
                lineY -= leading;
                curX = lineX;
                curY = lineY;
            }
            for (int k = stack.size() - 1; k >= 0; --k) {
                const QByteArray& s = stack.at(k);
                if (s.startsWith('(')) { show(s.mid(1), false); break; }
                if (s.startsWith('<')) { show(s.mid(1), true); break; }
            }
        } else if (tok == "TJ") {
            // An array of strings and numbers; a big negative number is a wide gap, so a space.
            int from = stack.size() - 1;
            while (from >= 0 && stack.at(from) != "[") --from;
            for (int k = std::max(0, from + 1); k < stack.size(); ++k) {
                const QByteArray& s = stack.at(k);
                if (s.startsWith('(')) show(s.mid(1), false);
                else if (s.startsWith('<')) show(s.mid(1), true);
                else {
                    // A number inside a TJ array moves the pen back by that many thousandths of the
                    // type size; a big one is a gap wide enough to be a space.
                    bool ok = false;
                    const double adj = s.toDouble(&ok);
                    if (ok) {
                        curX -= adj / 1000.0 * size;
                        penX = curX;   // the array's own moves are not gaps between words
                        if (adj < -180.0 && !out.endsWith(QLatin1Char(' '))) out += QLatin1Char(' ');
                    }
                }
            }
        } else if (tok == "Td" || tok == "TD") {
            const QList<double> n = numbersOnStack(stack, 2);
            if (n.size() == 2) {
                lineX += n.at(0);
                lineY += n.at(1);
                if (tok == "TD") leading = -n.at(1);
            }
            curX = lineX;
            curY = lineY;
        } else if (tok == "Tm") {
            // The whole text matrix: e and f are where it puts the pen.
            const QList<double> n = numbersOnStack(stack, 6);
            if (n.size() == 6) {
                lineX = n.at(4);
                lineY = n.at(5);
                curX = lineX;
                curY = lineY;
            }
        } else if (tok == "T*") {
            lineY -= leading;
            curX = lineX;
            curY = lineY;
        } else if (tok == "TL") {
            const QList<double> n = numbersOnStack(stack, 1);
            if (n.size() == 1) leading = n.at(0);
        } else if (tok == "BT") {
            lineX = lineY = curX = curY = 0.0;
            if (wrote) out += QLatin1Char('\n');
            wrote = false;
        } else if (tok == "Tf") {
            // /F3 9 Tf - the font name, then the size it is set at. The size is what tells a space
            // from a continued word, so it is taken as well.
            font = nullptr;
            for (int k = stack.size() - 1; k >= 0; --k)
                if (stack.at(k).startsWith('/')) {
                    const auto it = fonts.constFind(QString::fromLatin1(stack.at(k).mid(1)));
                    if (it != fonts.constEnd()) font = &*it;
                    break;
                }
            const QList<double> n = numbersOnStack(stack, 1);
            if (n.size() == 1 && n.at(0) > 0.01) size = n.at(0);
        }
        if (tok == "Tj" || tok == "TJ" || tok == "'" || tok == "\"" || tok == "Td" || tok == "TD" ||
            tok == "T*" || tok == "Tf" || tok == "BT" || tok == "ET" || tok == "TL" || tok == "cm" ||
            tok == "Tm" || tok == "re" || tok == "m" || tok == "l" || tok == "c" || tok == "g" ||
            tok == "rg" || tok == "gs" || tok == "Do" || tok == "BDC" || tok == "EMC")
            stack.clear();
        else {
            // A number is an OPERAND: it belongs on the stack for whichever operator comes next.
            // Without this the stack held only strings and names, so Td, Tm, TL and Tf never saw the
            // numbers they are given - and the text's position, which is what tells a new line from a
            // space, was never read at all.
            bool isNumber = false;
            tok.toDouble(&isNumber);
            if (isNumber) stack.append(tok);
        }
        if (stack.size() > 4096) stack.clear();   // a pathological stream must not grow without end
        if (out.size() > (1 << 24)) break;        // and neither must the text
    }
    return out;
}

// Tidy up: the runs of blank lines a coordinate-by-coordinate layout produces, and the trailing
// spaces, are not information.
QString tidy(const QString& raw) {
    QStringList lines;
    for (QString line : raw.split(QLatin1Char('\n'))) {
        line.replace(QRegularExpression(QStringLiteral("[ \\t]+")), QStringLiteral(" "));
        line = line.trimmed();
        if (line.isEmpty() && (lines.isEmpty() || lines.last().isEmpty())) continue;
        lines << line;
    }
    while (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
    return lines.join(QLatin1Char('\n'));
}

} // namespace

bool looksLikePdf(const QByteArray& bytes) {
    // The header should be at the start, but a few producers put junk in front of it, and readers
    // tolerate that, so this looks in the first kilobyte.
    return bytes.left(1024).contains("%PDF-");
}

QString extract(const QByteArray& pdf, QString* why) {
    auto note = [why](const QString& what) {
        if (why) *why = what;
    };
    if (pdf.isEmpty()) {
        note(QStringLiteral("the file is empty"));
        return {};
    }
    if (!looksLikePdf(pdf)) {
        note(QStringLiteral("that is not a PDF"));
        return {};
    }
    // An encrypted PDF has an /Encrypt entry in its trailer. Its streams are ciphertext, so say so
    // instead of returning the rubbish that inflating them would give.
    if (pdf.contains("/Encrypt")) {
        note(QStringLiteral("the PDF is encrypted, so its text cannot be read out of it"));
        return {};
    }

    const std::vector<RawStream> streams = streamsOf(pdf);
    if (streams.empty()) {
        note(QStringLiteral("the PDF holds no streams to read"));
        return {};
    }

    // The /ToUnicode maps first, by the object number they are, then the font names that point at
    // them. A content stream names a font (/F1); the page's /Font dictionary says which object that
    // is; the object is a CMap.
    QHash<int, CMap> cmapsByObject;
    for (const RawStream& s : streams) {
        if (!s.dict.contains("/ToUnicode") && !s.data.contains("beginbfchar") &&
            !s.dict.contains("/CMap") && !s.data.contains("beginbfrange")) {
            // Compressed ones have to be looked at before they can be ruled out.
            if (!s.dict.contains("FlateDecode")) continue;
        }
        const QByteArray content = streamContent(s);
        if (!content.contains("beginbfchar") && !content.contains("beginbfrange")) continue;
        CMap m = parseCMap(content);
        if (!m.empty() && s.objNum >= 0) cmapsByObject.insert(s.objNum, m);
    }
    // The other kind of map, and on a datasheet the more common one: an /Encoding object with a
    // /Differences array, which says what each byte draws by naming the glyph. A subset font has no
    // /ToUnicode at all, and without the names its bytes are meaningless.
    QHash<int, CMap> encodingsByObject;
    {
        static const QRegularExpression obj(
            QStringLiteral("(\\d+)\\s+\\d+\\s+obj([\\s\\S]{0,8000}?)endobj"));
        auto it = obj.globalMatch(QString::fromLatin1(pdf));
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            if (!m.captured(2).contains(QLatin1String("/Differences"))) continue;
            CMap enc = parseDifferences(m.captured(2).toLatin1());
            if (!enc.empty()) encodingsByObject.insert(m.captured(1).toInt(), enc);
        }
    }

    // Font name -> map, from every /Font << /F1 5 0 R >> dictionary in the file. The names repeat
    // across pages and generally mean the same font, which is good enough to read text by.
    QHash<QString, CMap> fonts;
    if (!encodingsByObject.isEmpty()) {
        // /F1 -> the font object -> its /Encoding object -> the Differences map.
        static const QRegularExpression fontObj(
            QStringLiteral("(\\d+)\\s+\\d+\\s+obj([\\s\\S]{0,3000}?)endobj"));
        static const QRegularExpression encRef(QStringLiteral("/Encoding\\s+(\\d+)\\s+\\d+\\s+R"));
        QHash<int, int> fontObjToEncoding;
        auto objs = fontObj.globalMatch(QString::fromLatin1(pdf));
        while (objs.hasNext()) {
            const QRegularExpressionMatch m = objs.next();
            if (!m.captured(2).contains(QLatin1String("/Font"))) continue;
            const QRegularExpressionMatch e = encRef.match(m.captured(2));
            if (e.hasMatch()) fontObjToEncoding.insert(m.captured(1).toInt(), e.captured(1).toInt());
        }
        static const QRegularExpression fontRef(
            QStringLiteral("/([A-Za-z0-9_.+-]+)\\s+(\\d+)\\s+\\d+\\s+R"));
        auto refs = fontRef.globalMatch(QString::fromLatin1(pdf));
        while (refs.hasNext()) {
            const QRegularExpressionMatch m = refs.next();
            const auto viaFont = fontObjToEncoding.constFind(m.captured(2).toInt());
            if (viaFont == fontObjToEncoding.constEnd()) continue;
            const auto enc = encodingsByObject.constFind(*viaFont);
            if (enc != encodingsByObject.constEnd()) fonts.insert(m.captured(1), *enc);
        }
    }
    if (!cmapsByObject.isEmpty()) {
        static const QRegularExpression fontRef(
            QStringLiteral("/([A-Za-z0-9_.+-]+)\\s+(\\d+)\\s+\\d+\\s+R"));
        // /Font dictionaries live in page objects, which are not streams; scan the whole file for the
        // pattern and pair each name with a CMap when that object (or its font descendant) is one.
        auto it = fontRef.globalMatch(QString::fromLatin1(pdf));
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            const int obj = m.captured(2).toInt();
            const auto found = cmapsByObject.constFind(obj);
            if (found != cmapsByObject.constEnd()) fonts.insert(m.captured(1), *found);
        }
        // A font object referencing its own ToUnicode: /F1 -> font object -> /ToUnicode N 0 R.
        static const QRegularExpression toUni(QStringLiteral("/ToUnicode\\s+(\\d+)\\s+\\d+\\s+R"));
        static const QRegularExpression fontObj(
            QStringLiteral("(\\d+)\\s+\\d+\\s+obj([\\s\\S]{0,2000}?)endobj"));
        auto objs = fontObj.globalMatch(QString::fromLatin1(pdf));
        QHash<int, int> fontObjToCMap;
        while (objs.hasNext()) {
            const QRegularExpressionMatch m = objs.next();
            const QRegularExpressionMatch u = toUni.match(m.captured(2));
            if (u.hasMatch()) fontObjToCMap.insert(m.captured(1).toInt(), u.captured(1).toInt());
        }
        auto refs = fontRef.globalMatch(QString::fromLatin1(pdf));
        while (refs.hasNext()) {
            const QRegularExpressionMatch m = refs.next();
            const auto viaFont = fontObjToCMap.constFind(m.captured(2).toInt());
            if (viaFont == fontObjToCMap.constEnd()) continue;
            const auto cm = cmapsByObject.constFind(*viaFont);
            if (cm != cmapsByObject.constEnd()) fonts.insert(m.captured(1), *cm);
        }
    }

    // Now the content: every stream that reads like a content stream (it has text operators in it).
    QStringList pages;
    for (const RawStream& s : streams) {
        const QByteArray content = streamContent(s);
        if (content.isEmpty()) continue;
        if (!content.contains("BT") && !content.contains("Tj") && !content.contains("TJ")) continue;
        const QString text = tidy(textOfContent(content, fonts));
        if (!text.trimmed().isEmpty()) pages << text;
    }
    if (pages.isEmpty()) {
        note(QStringLiteral("the PDF gave no text - it is most likely a scan, which would need "
                            "character recognition"));
        return {};
    }
    note(QStringLiteral("read from the PDF (%1 page(s) of text)").arg(pages.size()));
    return pages.join(QStringLiteral("\n\n"));
}

QString extractFromFile(const QString& path, QString* why) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (why) *why = QStringLiteral("that file cannot be read");
        return {};
    }
    const QByteArray bytes = f.readAll();
    f.close();
    return extract(bytes, why);
}

} // namespace PdfText
} // namespace Edraw_Desktop
