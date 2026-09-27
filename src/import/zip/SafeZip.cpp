#include "SafeZip.h"

extern "C" {
#include "puff.h"
}

#include <QFile>
#include <QtEndian>

#include <array>

namespace bld::import {

namespace {

quint32 crc32Of(const QByteArray& data) {
    static const auto table = [] {
        std::array<quint32, 256> t{};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    quint32 c = 0xFFFFFFFFu;
    for (const char ch : data) c = table[(c ^ static_cast<quint8>(ch)) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

}  // namespace

std::optional<SafeZip> SafeZip::open(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
    SafeZip zip(f.readAll());
    if (!zip.isValid()) return std::nullopt;
    return zip;
}

SafeZip::SafeZip(QByteArray archive) : data_(std::move(archive)) {
    const qint64 n = data_.size();
    const auto u16 = [&](qint64 at) -> quint32 { return at >= 0 && at + 2 <= n ? qFromLittleEndian<quint16>(data_.constData() + at) : 0; };
    const auto u32 = [&](qint64 at) -> quint32 { return at >= 0 && at + 4 <= n ? qFromLittleEndian<quint32>(data_.constData() + at) : 0; };
    // End of central directory record: the last one within 64 KiB + 22 bytes of the end.
    const qint64 eocd = data_.lastIndexOf(QByteArrayLiteral("PK\x05\x06"));
    if (eocd < 0 || eocd + 22 > n || eocd < n - 65557) return;
    const quint32 count = u16(eocd + 10);
    qint64 at = u32(eocd + 16);
    for (quint32 i = 0; i < count; ++i) {
        if (at < 0 || at + 46 > n || u32(at) != 0x02014b50u) return;
        Entry e;
        const quint32 flags = u16(at + 8);
        e.method = static_cast<quint16>(u16(at + 10));
        e.crc = u32(at + 16);
        e.compressedSize = u32(at + 20);
        e.size = u32(at + 24);
        const qint64 nameLen = u16(at + 28), extraLen = u16(at + 30), commentLen = u16(at + 32);
        const quint32 externalAttributes = u32(at + 38);
        e.localHeaderOffset = u32(at + 42);
        if (at + 46 + nameLen > n) return;
        const QByteArray rawName = data_.mid(at + 46, nameLen);
        e.name = (flags & 0x800) ? QString::fromUtf8(rawName) : QString::fromLatin1(rawName);
        e.isDir = e.name.endsWith(QLatin1Char('/'));
        const quint32 unixMode = externalAttributes >> 16;
        e.isSymLink = (unixMode & 0170000) == 0120000;
        // Nothing may claim more than the file holds (no ZIP64 here).
        if (e.compressedSize > n || e.localHeaderOffset > n || e.size > n * 1100 + 65536) return;
        entries_ << e;
        at += 46 + nameLen + extraLen + commentLen;
    }
    valid_ = true;
}

const SafeZip::Entry* SafeZip::find(const QString& name, Qt::CaseSensitivity cs) const {
    for (const Entry& e : entries_)
        if (e.name.compare(name, cs) == 0) return &e;
    return nullptr;
}

std::optional<QByteArray> SafeZip::read(const Entry& e, qint64 maxSize) const {
    if (e.isDir || e.size > maxSize) return std::nullopt;
    const qint64 n = data_.size();
    const qint64 lh = e.localHeaderOffset;
    if (lh + 30 > n || qFromLittleEndian<quint32>(data_.constData() + lh) != 0x04034b50u) return std::nullopt;
    const qint64 start = lh + 30 + qFromLittleEndian<quint16>(data_.constData() + lh + 26)
                                 + qFromLittleEndian<quint16>(data_.constData() + lh + 28);
    if (start > n || e.compressedSize > n - start) return std::nullopt;
    const auto* src = reinterpret_cast<const unsigned char*>(data_.constData() + start);

    QByteArray out;
    if (e.method == 0) {
        if (e.compressedSize != e.size) return std::nullopt;
        out = QByteArray(reinterpret_cast<const char*>(src), e.size);
    } else if (e.method == 8) {
        out.resize(e.size);
        unsigned long destLen = static_cast<unsigned long>(e.size);
        unsigned long srcLen = static_cast<unsigned long>(e.compressedSize);
        // puff never writes past destLen: extra output is an error.
        if (puff(reinterpret_cast<unsigned char*>(out.data()), &destLen, src, &srcLen) != 0
            || destLen != static_cast<unsigned long>(e.size))
            return std::nullopt;
    } else {
        return std::nullopt;
    }
    if (crc32Of(out) != e.crc) return std::nullopt;
    return out;
}

}  // namespace bld::import
