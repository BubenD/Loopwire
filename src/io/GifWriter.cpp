#include "io/GifWriter.h"

#include <QFile>

#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_map>

namespace Loopwire::gif {

namespace {

// The palette's spare entry: in a picture after the first, a pixel that has not changed.
constexpr int kUnchanged = 255;

int key15(QRgb c) { return ((qRed(c) >> 3) << 10) | ((qGreen(c) >> 3) << 5) | (qBlue(c) >> 3); }

QRgb fromKey(int k) {
    return qRgb(((k >> 10) & 31) << 3 | 4, ((k >> 5) & 31) << 3 | 4, (k & 31) << 3 | 4);
}

int dist2(QRgb a, QRgb b) {
    const int dr = qRed(a) - qRed(b), dg = qGreen(a) - qGreen(b), db = qBlue(a) - qBlue(b);
    return dr * dr + dg * dg + db * db;
}

// THE PALETTE: the colours used most, but no two too alike while there are others to choose - a cursor's red or a
// selection's blue covers few pixels and would never make it in on numbers alone.
std::vector<QRgb> makePalette(const std::vector<Frame>& frames) {
    std::vector<uint32_t> count(32768, 0);
    for (const Frame& f : frames) {
        const QImage img = f.image.convertToFormat(QImage::Format_RGB32);
        for (int y = 0; y < img.height(); ++y) {
            const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            for (int x = 0; x < img.width(); ++x) ++count[static_cast<size_t>(key15(row[x]))];
        }
    }
    std::vector<int> used;
    for (int k = 0; k < 32768; ++k)
        if (count[static_cast<size_t>(k)]) used.push_back(k);
    std::sort(used.begin(), used.end(), [&](int a, int b) { return count[static_cast<size_t>(a)] > count[static_cast<size_t>(b)]; });
    std::vector<QRgb> pal;
    if (used.size() <= kUnchanged) {
        for (int k : used) pal.push_back(fromKey(k));
        return pal;
    }
    std::vector<bool> taken(used.size(), false);
    for (int apart : {48 * 48, 24 * 24, 12 * 12, 0}) {
        for (size_t i = 0; i < used.size() && pal.size() < kUnchanged; ++i) {
            if (taken[i]) continue;
            const QRgb c = fromKey(used[i]);
            bool near = false;
            for (QRgb p : pal)
                if (dist2(c, p) < apart) {
                    near = true;
                    break;
                }
            if (near) continue;
            pal.push_back(c);
            taken[i] = true;
        }
        if (pal.size() >= kUnchanged) break;
    }
    return pal;
}

// THE BITS OF THE LZW CODES, least significant first, in blocks of up to 255 bytes.
struct BitOut {
    QByteArray& out;
    QByteArray block;
    uint32_t acc = 0;
    int bits = 0;
    void put(int code, int size) {
        acc |= static_cast<uint32_t>(code) << bits;
        bits += size;
        while (bits >= 8) {
            byte(static_cast<char>(acc & 0xFF));
            acc >>= 8;
            bits -= 8;
        }
    }
    void byte(char c) {
        block.append(c);
        if (block.size() == 255) flushBlock();
    }
    void flushBlock() {
        if (block.isEmpty()) return;
        out.append(static_cast<char>(block.size()));
        out.append(block);
        block.clear();
    }
    void finish() {
        if (bits > 0) byte(static_cast<char>(acc & 0xFF));
        acc = 0;
        bits = 0;
        flushBlock();
        out.append('\0');
    }
};

void lzw(const std::vector<uint8_t>& idx, QByteArray& out) {
    const int minCode = 8;
    const int clear = 1 << minCode, eoi = clear + 1;
    out.append(static_cast<char>(minCode));
    BitOut bo{out};
    std::unordered_map<uint32_t, int> dict;
    dict.reserve(8192);
    int codeSize = minCode + 1;
    int maxCode = eoi;
    bo.put(clear, codeSize);
    if (idx.empty()) {
        bo.put(eoi, codeSize);
        bo.finish();
        return;
    }
    int cur = idx[0];
    for (size_t i = 1; i < idx.size(); ++i) {
        const int c = idx[i];
        const uint32_t k = (static_cast<uint32_t>(cur) << 8) | static_cast<uint32_t>(c);
        const auto it = dict.find(k);
        if (it != dict.end()) {
            cur = it->second;
            continue;
        }
        bo.put(cur, codeSize);
        dict.emplace(k, ++maxCode);
        if (maxCode >= (1 << codeSize)) ++codeSize;
        if (maxCode == 4095) {
            bo.put(clear, codeSize);
            dict.clear();
            codeSize = minCode + 1;
            maxCode = eoi;
        }
        cur = c;
    }
    bo.put(cur, codeSize);
    bo.put(eoi, codeSize);
    bo.finish();
}

void le16(QByteArray& out, int v) {
    out.append(static_cast<char>(v & 0xFF));
    out.append(static_cast<char>((v >> 8) & 0xFF));
}

} // namespace

QByteArray encode(const std::vector<Frame>& frames) {
    if (frames.empty() || frames.front().image.isNull()) return {};
    const int w = frames.front().image.width(), h = frames.front().image.height();
    const std::vector<QRgb> pal = makePalette(frames);
    // Every colour there could be, to the palette entry nearest it.
    std::vector<uint8_t> nearest(32768, 0);
    for (int k = 0; k < 32768; ++k) {
        const QRgb c = fromKey(k);
        int best = 0, bestD = 1 << 30;
        for (size_t i = 0; i < pal.size(); ++i) {
            const int d = dist2(c, pal[i]);
            if (d < bestD) {
                bestD = d;
                best = static_cast<int>(i);
            }
        }
        nearest[static_cast<size_t>(k)] = static_cast<uint8_t>(best);
    }

    QByteArray out("GIF89a");
    le16(out, w);
    le16(out, h);
    out.append(static_cast<char>(0xF7));   // a global palette of 256, 8 bits a colour
    out.append('\0');
    out.append('\0');
    for (int i = 0; i < 256; ++i) {
        const QRgb c = i < static_cast<int>(pal.size()) ? pal[static_cast<size_t>(i)] : qRgb(0, 0, 0);
        out.append(static_cast<char>(qRed(c)));
        out.append(static_cast<char>(qGreen(c)));
        out.append(static_cast<char>(qBlue(c)));
    }
    // Round and round.
    out.append("\x21\xFF\x0B" "NETSCAPE2.0" "\x03\x01\x00\x00\x00", 19);

    // The pictures as palette entries; one that changes nothing lengthens the one before.
    struct Out {
        std::vector<uint8_t> idx;
        int delay;
    };
    std::vector<Out> shown;
    for (const Frame& f : frames) {
        const QImage img = f.image.convertToFormat(QImage::Format_RGB32).copy(0, 0, w, h);
        std::vector<uint8_t> idx(static_cast<size_t>(w) * h);
        for (int y = 0; y < h; ++y) {
            const QRgb* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
            for (int x = 0; x < w; ++x) idx[static_cast<size_t>(y) * w + x] = nearest[static_cast<size_t>(key15(row[x]))];
        }
        const int delay = std::clamp(f.delayCs, 2, 65535);
        if (!shown.empty() && shown.back().idx == idx) {
            shown.back().delay = std::min(65535, shown.back().delay + delay);
            continue;
        }
        shown.push_back({std::move(idx), delay});
    }

    for (size_t n = 0; n < shown.size(); ++n) {
        const std::vector<uint8_t>& idx = shown[n].idx;
        // ONLY WHAT CHANGED: the box round it, and in the box what did not change left see-through.
        int x0 = 0, y0 = 0, x1 = w - 1, y1 = h - 1;
        const std::vector<uint8_t>* before = n > 0 ? &shown[n - 1].idx : nullptr;
        if (before) {
            x0 = w;
            y0 = h;
            x1 = -1;
            y1 = -1;
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    if (idx[static_cast<size_t>(y) * w + x] != (*before)[static_cast<size_t>(y) * w + x]) {
                        x0 = std::min(x0, x);
                        x1 = std::max(x1, x);
                        y0 = std::min(y0, y);
                        y1 = std::max(y1, y);
                    }
        }
        const int rw = x1 - x0 + 1, rh = y1 - y0 + 1;
        std::vector<uint8_t> part(static_cast<size_t>(rw) * rh);
        for (int y = 0; y < rh; ++y)
            for (int x = 0; x < rw; ++x) {
                const size_t at = static_cast<size_t>(y + y0) * w + (x + x0);
                part[static_cast<size_t>(y) * rw + x] = before && idx[at] == (*before)[at] ? kUnchanged : idx[at];
            }
        // How long it stays up; left in place under the next; the spare entry see-through.
        out.append("\x21\xF9\x04", 3);
        out.append(static_cast<char>((1 << 2) | (before ? 1 : 0)));
        le16(out, shown[n].delay);
        out.append(static_cast<char>(kUnchanged));
        out.append('\0');
        out.append('\x2C');
        le16(out, x0);
        le16(out, y0);
        le16(out, rw);
        le16(out, rh);
        out.append('\0');
        lzw(part, out);
    }
    out.append('\x3B');
    return out;
}

bool write(const QString& path, const std::vector<Frame>& frames, QString* error) {
    const QByteArray bytes = encode(frames);
    if (bytes.isEmpty()) {
        if (error) *error = QStringLiteral("No pictures to write");
        return false;
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) {
        if (error) *error = f.errorString();
        return false;
    }
    return true;
}

} // namespace Loopwire::gif
