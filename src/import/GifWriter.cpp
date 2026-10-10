#include "GifWriter.h"

#include <QHash>
#include <QImage>
#include <QSaveFile>

#include <algorithm>
#include <climits>
#include <vector>

namespace bld::import {

namespace {

constexpr int kTransparentIndex = 0;
constexpr int kMaxColors = 255;  // index 0 is reserved for transparency

struct Box {
    std::vector<std::pair<QRgb, int>> colors;  // color, pixel count
    int range(int channel) const {
        int lo = 255, hi = 0;
        for (const auto& [c, n] : colors) {
            const int v = channel == 0 ? qRed(c) : channel == 1 ? qGreen(c) : qBlue(c);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        return hi - lo;
    }
};

// Median-cut palette over the opaque colors' histogram.
std::vector<QRgb> buildPalette(const QHash<QRgb, int>& histogram) {
    std::vector<QRgb> palette;
    if (histogram.size() <= kMaxColors) {
        for (auto it = histogram.cbegin(); it != histogram.cend(); ++it) palette.push_back(it.key());
        return palette;
    }
    std::vector<Box> boxes(1);
    for (auto it = histogram.cbegin(); it != histogram.cend(); ++it) {
        boxes[0].colors.emplace_back(it.key(), it.value());
    }
    while (static_cast<int>(boxes.size()) < kMaxColors) {
        // Split the box with the widest channel range.
        int best = -1, bestChannel = 0, bestRange = 0;
        for (int i = 0; i < static_cast<int>(boxes.size()); ++i) {
            if (boxes[i].colors.size() < 2) continue;
            for (int ch = 0; ch < 3; ++ch) {
                const int r = boxes[i].range(ch);
                if (r > bestRange) { bestRange = r; best = i; bestChannel = ch; }
            }
        }
        if (best < 0) break;
        auto& cs = boxes[best].colors;
        std::sort(cs.begin(), cs.end(), [bestChannel](const auto& a, const auto& b) {
            const auto key = [bestChannel](QRgb c) {
                return bestChannel == 0 ? qRed(c) : bestChannel == 1 ? qGreen(c) : qBlue(c);
            };
            return key(a.first) < key(b.first);
        });
        long long total = 0;
        for (const auto& [c, n] : cs) total += n;
        long long running = 0;
        size_t split = 1;
        for (size_t i = 0; i + 1 < cs.size(); ++i) {
            running += cs[i].second;
            if (running * 2 >= total) { split = i + 1; break; }
            split = i + 1;
        }
        Box upper;
        upper.colors.assign(cs.begin() + static_cast<long>(split), cs.end());
        cs.resize(split);
        boxes.push_back(std::move(upper));
    }
    for (const Box& b : boxes) {
        long long r = 0, g = 0, bl = 0, n = 0;
        for (const auto& [c, k] : b.colors) {
            r += qRed(c) * k; g += qGreen(c) * k; bl += qBlue(c) * k; n += k;
        }
        if (n == 0) continue;  // an empty box has no average
        palette.push_back(qRgb(int(r / n), int(g / n), int(bl / n)));
    }
    return palette;
}

class BitWriter {
public:
    explicit BitWriter(QByteArray& out) : out_(out) {}
    void write(int code, int bits) {
        acc_ |= static_cast<quint32>(code) << nbits_;
        nbits_ += bits;
        while (nbits_ >= 8) { push(static_cast<char>(acc_ & 0xff)); acc_ >>= 8; nbits_ -= 8; }
    }
    void finish() {
        if (nbits_ > 0) push(static_cast<char>(acc_ & 0xff));
        flushBlock();
        out_.append('\0');  // block terminator
    }

private:
    void push(char b) {
        block_.append(b);
        if (block_.size() == 255) flushBlock();
    }
    void flushBlock() {
        if (block_.isEmpty()) return;
        out_.append(static_cast<char>(block_.size()));
        out_.append(block_);
        block_.clear();
    }
    QByteArray& out_;
    QByteArray  block_;
    quint32     acc_ = 0;
    int         nbits_ = 0;
};

// GIF LZW with 8-bit minimum code size.
void lzwEncode(const std::vector<quint8>& indices, QByteArray& out) {
    constexpr int kMinCodeSize = 8;
    constexpr int kClear = 1 << kMinCodeSize;
    constexpr int kEnd = kClear + 1;
    out.append(static_cast<char>(kMinCodeSize));
    BitWriter bits(out);

    QHash<int, int> dict;  // (prefix << 8 | byte) -> code
    int codeSize = kMinCodeSize + 1;
    int next = kEnd + 1;
    bits.write(kClear, codeSize);
    if (indices.empty()) { bits.write(kEnd, codeSize); bits.finish(); return; }

    int prefix = indices[0];
    for (size_t i = 1; i < indices.size(); ++i) {
        const int k = indices[i];
        const int key = (prefix << 8) | k;
        const auto it = dict.constFind(key);
        if (it != dict.constEnd()) { prefix = it.value(); continue; }
        bits.write(prefix, codeSize);
        dict.insert(key, next++);
        // The decoder learns each entry one code later, so widen once the
        // entry just added no longer fits the current width.
        if (next > (1 << codeSize) && codeSize < 12) ++codeSize;
        if (next == 4096) {
            bits.write(kClear, codeSize);
            dict.clear();
            codeSize = kMinCodeSize + 1;
            next = kEnd + 1;
        }
        prefix = k;
    }
    bits.write(prefix, codeSize);
    bits.write(kEnd, codeSize);
    bits.finish();
}

void appendU16(QByteArray& out, int v) {
    out.append(static_cast<char>(v & 0xff));
    out.append(static_cast<char>((v >> 8) & 0xff));
}

}  // namespace

bool writeGif(const QImage& image, const QString& path, QString* error) {
    if (image.isNull() || image.width() > 0xffff || image.height() > 0xffff) {
        if (error) *error = QStringLiteral("Image is empty or too large for GIF");
        return false;
    }
    const QImage img = image.convertToFormat(QImage::Format_ARGB32);
    const int w = img.width(), h = img.height();

    QHash<QRgb, int> histogram;
    for (int y = 0; y < h; ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            if (qAlpha(row[x]) >= 128) ++histogram[row[x] | 0xff000000u];
        }
    }
    const std::vector<QRgb> palette = buildPalette(histogram);

    std::vector<quint8> indices(static_cast<size_t>(w) * h, kTransparentIndex);
    QHash<QRgb, quint8> nearest;
    for (int y = 0; y < h; ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            if (qAlpha(row[x]) < 128) continue;
            const QRgb c = row[x] | 0xff000000u;
            auto it = nearest.constFind(c);
            if (it == nearest.constEnd()) {
                int best = 0, bestD = INT_MAX;
                for (int i = 0; i < static_cast<int>(palette.size()); ++i) {
                    const int dr = qRed(c) - qRed(palette[i]);
                    const int dg = qGreen(c) - qGreen(palette[i]);
                    const int db = qBlue(c) - qBlue(palette[i]);
                    const int d = dr * dr + dg * dg + db * db;
                    if (d < bestD) { bestD = d; best = i; }
                }
                it = nearest.insert(c, static_cast<quint8>(best + 1));
            }
            indices[static_cast<size_t>(y) * w + x] = it.value();
        }
    }

    QByteArray out("GIF89a");
    appendU16(out, w);
    appendU16(out, h);
    out.append(static_cast<char>(0xF7));  // global color table, 8 bpp, 256 entries
    out.append('\0');                      // background color index
    out.append('\0');                      // pixel aspect ratio
    for (int i = 0; i < 256; ++i) {
        const QRgb c = (i >= 1 && i - 1 < static_cast<int>(palette.size())) ? palette[i - 1] : 0;
        out.append(static_cast<char>(qRed(c)));
        out.append(static_cast<char>(qGreen(c)));
        out.append(static_cast<char>(qBlue(c)));
    }
    // Graphic control extension: transparency on, index kTransparentIndex.
    out.append("\x21\xF9\x04\x01\x00\x00", 6);
    out.append(static_cast<char>(kTransparentIndex));
    out.append('\0');
    // Image descriptor covering the whole canvas, no local color table.
    out.append(',');
    appendU16(out, 0);
    appendU16(out, 0);
    appendU16(out, w);
    appendU16(out, h);
    out.append('\0');
    lzwEncode(indices, out);
    out.append(';');

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(out) != out.size() || !f.commit()) {
        if (error) *error = QStringLiteral("Could not write %1: %2").arg(path, f.errorString());
        return false;
    }
    return true;
}

}  // namespace bld::import
