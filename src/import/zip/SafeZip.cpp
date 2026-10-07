#include "SafeZip.h"

extern "C" {
#include "puff.h"
}

#include <QFile>
#include <QtEndian>

#include <array>

namespace bld::import {

namespace {

const std::array<quint32, 256>& crcTable() {
    static const auto table = [] {
        std::array<quint32, 256> t{};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    return table;
}

quint32 crc32Of(const QByteArray& data) {
    const auto& table = crcTable();
    quint32 c = 0xFFFFFFFFu;
    for (const char ch : data) c = table[(c ^ static_cast<quint8>(ch)) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// Traditional PKWARE encryption (APPNOTE.TXT 6.1): three keys stirred by
// every plain byte.
class ZipCryptoKeys {
public:
    explicit ZipCryptoKeys(const QByteArray& password) {
        for (const char c : password) update(static_cast<quint8>(c));
    }
    quint8 decryptByte(quint8 c) {
        const quint32 t = (k2_ | 2u) & 0xFFFFu;
        const auto plain = static_cast<quint8>(c ^ static_cast<quint8>((t * (t ^ 1u)) >> 8));
        update(plain);
        return plain;
    }

private:
    static quint32 crcByte(quint32 crc, quint8 b) {
        // One step of CRC-32 without the final inversion, as the spec's crc32(crc, b).
        return crcTable()[(crc ^ b) & 0xFFu] ^ (crc >> 8);
    }
    void update(quint8 b) {
        k0_ = crcByte(k0_, b);
        k1_ = (k1_ + (k0_ & 0xFFu)) * 134775813u + 1u;
        k2_ = crcByte(k2_, static_cast<quint8>(k1_ >> 24));
    }
    quint32 k0_ = 0x12345678u, k1_ = 0x23456789u, k2_ = 0x34567890u;
};

constexpr qint64 kZipCryptoHeader = 12;

}  // namespace

quint32 zipCrc32(const QByteArray& data) { return crc32Of(data); }

std::optional<SafeZip> SafeZip::open(const QString& path, QList<QByteArray> passwords) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
    SafeZip zip(f.readAll(), std::move(passwords));
    if (!zip.isValid()) return std::nullopt;
    return zip;
}

SafeZip::SafeZip(QByteArray archive, QList<QByteArray> passwords)
    : data_(std::move(archive)), passwords_(std::move(passwords)) {
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
        e.flags = static_cast<quint16>(flags);
        e.method = static_cast<quint16>(u16(at + 10));
        e.modTime = static_cast<quint16>(u16(at + 12));
        e.encrypted = (flags & 0x1) != 0;
        e.aes = e.method == 99;
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

std::optional<QByteArray> SafeZip::read(const Entry& e, qint64 maxSize, ReadError* why) const {
    const auto fail = [why](ReadError r) -> std::optional<QByteArray> {
        if (why) *why = r;
        return std::nullopt;
    };
    if (why) *why = ReadError::None;
    if (e.isDir) return fail(ReadError::Damaged);
    if (e.aes) return fail(ReadError::AesEncrypted);
    if (e.size > maxSize) return fail(ReadError::TooBig);
    if (e.method != 0 && e.method != 8) return fail(ReadError::Unsupported);
    const qint64 n = data_.size();
    const qint64 lh = e.localHeaderOffset;
    if (lh + 30 > n || qFromLittleEndian<quint32>(data_.constData() + lh) != 0x04034b50u) return fail(ReadError::Damaged);
    const qint64 start = lh + 30 + qFromLittleEndian<quint16>(data_.constData() + lh + 26)
                                 + qFromLittleEndian<quint16>(data_.constData() + lh + 28);
    if (start > n || e.compressedSize > n - start) return fail(ReadError::Damaged);
    const auto* raw = reinterpret_cast<const unsigned char*>(data_.constData() + start);

    // Plain (or decrypted) entry bytes -> data, checked against the CRC.
    const auto unpack = [&e](const unsigned char* src, qint64 srcSize) -> std::optional<QByteArray> {
        QByteArray out;
        if (e.method == 0) {
            if (srcSize != e.size) return std::nullopt;
            out = QByteArray(reinterpret_cast<const char*>(src), e.size);
        } else {
            out.resize(e.size);
            unsigned long destLen = static_cast<unsigned long>(e.size);
            unsigned long srcLen = static_cast<unsigned long>(srcSize);
            // puff never writes past destLen: extra output is an error.
            if (puff(reinterpret_cast<unsigned char*>(out.data()), &destLen, src, &srcLen) != 0
                || destLen != static_cast<unsigned long>(e.size))
                return std::nullopt;
        }
        if (crc32Of(out) != e.crc) return std::nullopt;
        return out;
    };

    if (!e.encrypted) {
        auto out = unpack(raw, e.compressedSize);
        return out ? out : fail(ReadError::Damaged);
    }

    // ZipCrypto: a 12-byte header, whose last byte must match the CRC's high
    // byte (or the time's high byte when the sizes follow the data, flag bit 3).
    if (e.compressedSize < kZipCryptoHeader) return fail(ReadError::Damaged);
    const auto check = static_cast<quint8>((e.flags & 0x8) ? (e.modTime >> 8) : (e.crc >> 24));
    bool headerFit = false;
    for (const QByteArray& password : passwords_) {
        ZipCryptoKeys keys(password);
        quint8 last = 0;
        for (qint64 i = 0; i < kZipCryptoHeader; ++i) last = keys.decryptByte(raw[i]);
        // One byte only: 1 in 256 wrong passwords get past it, the CRC catches those.
        if (last != check) continue;
        headerFit = true;
        const qint64 bodySize = e.compressedSize - kZipCryptoHeader;
        QByteArray plain(bodySize, Qt::Uninitialized);
        auto* dst = reinterpret_cast<quint8*>(plain.data());
        const unsigned char* body = raw + kZipCryptoHeader;
        for (qint64 i = 0; i < bodySize; ++i) dst[i] = keys.decryptByte(body[i]);
        if (auto out = unpack(reinterpret_cast<const unsigned char*>(plain.constData()), bodySize)) return out;
    }
    return fail(headerFit ? ReadError::Damaged : ReadError::WrongPassword);
}

}  // namespace bld::import
