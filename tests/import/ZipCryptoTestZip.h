#pragma once
// Builds ZIPs the way BrickLink Studio writes .io files: each entry
// encrypted with traditional PKWARE "ZipCrypto". Tests only; the app never
// writes encrypted archives.

#include "import/zip/SafeZip.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QtEndian>

namespace bld::test {

struct CryptEntry {
    QString name;
    QByteArray data;
    bool deflate = true;
    bool encrypt = true;
    QByteArray password = QByteArrayLiteral("soho0909");
    bool aes = false;             // method 99 (the payload is just noise)
    bool dataDescriptor = false;  // flag bit 3: sizes after the data, time-byte check
    quint32 claimedSize = 0;      // non-zero: the uncompressed size the headers claim
};

class ZipCryptoKeys {
public:
    explicit ZipCryptoKeys(const QByteArray& password) {
        for (const char c : password) update(static_cast<quint8>(c));
    }
    quint8 encrypt(quint8 plain) {
        const quint32 t = (k2_ | 2u) & 0xFFFFu;
        const auto c = static_cast<quint8>(plain ^ static_cast<quint8>((t * (t ^ 1u)) >> 8));
        update(plain);
        return c;
    }

private:
    static quint32 crcByte(quint32 crc, quint8 b) {
        crc ^= b;
        for (int k = 0; k < 8; ++k) crc = (crc & 1) ? 0xEDB88320u ^ (crc >> 1) : crc >> 1;
        return crc;
    }
    void update(quint8 b) {
        k0_ = crcByte(k0_, b);
        k1_ = (k1_ + (k0_ & 0xFFu)) * 134775813u + 1u;
        k2_ = crcByte(k2_, static_cast<quint8>(k1_ >> 24));
    }
    quint32 k0_ = 0x12345678u, k1_ = 0x23456789u, k2_ = 0x34567890u;
};

inline QByteArray rawDeflate(const QByteArray& data) {
    // qCompress: 4-byte size, 2-byte zlib header, deflate, 4-byte Adler-32.
    const QByteArray z = qCompress(data, 9);
    return z.mid(6, z.size() - 10);
}

inline void put16(QByteArray& out, quint32 v) {
    char b[2];
    qToLittleEndian<quint16>(static_cast<quint16>(v), b);
    out.append(b, 2);
}
inline void put32(QByteArray& out, quint32 v) {
    char b[4];
    qToLittleEndian<quint32>(v, b);
    out.append(b, 4);
}

inline QByteArray buildZip(const QList<CryptEntry>& entries) {
    constexpr quint32 kTime = (12u << 11), kDate = ((2020u - 1980u) << 9) | (1u << 5) | 1u;
    QByteArray body, central;
    for (const CryptEntry& e : entries) {
        const quint32 crc = import::zipCrc32(e.data);
        const quint32 size = e.claimedSize ? e.claimedSize : static_cast<quint32>(e.data.size());
        quint16 method = e.deflate ? 8 : 0;
        QByteArray payload = e.deflate ? rawDeflate(e.data) : e.data;
        quint16 flags = 0;
        if (e.aes) {
            method = 99;
            flags |= 1;
        } else if (e.encrypt) {
            flags |= 1;
            if (e.dataDescriptor) flags |= 8;
            QByteArray enc;
            ZipCryptoKeys keys(e.password);
            for (int i = 0; i < 11; ++i) enc.append(static_cast<char>(keys.encrypt(static_cast<quint8>(0x5A + 13 * i))));
            const auto check = static_cast<quint8>(e.dataDescriptor ? (kTime >> 8) : (crc >> 24));
            enc.append(static_cast<char>(keys.encrypt(check)));
            for (const char c : payload) enc.append(static_cast<char>(keys.encrypt(static_cast<quint8>(c))));
            payload = enc;
        }
        const QByteArray name = e.name.toUtf8();
        const quint32 offset = static_cast<quint32>(body.size());
        const bool late = flags & 8;
        put32(body, 0x04034b50u); put16(body, 20); put16(body, flags); put16(body, method);
        put16(body, kTime); put16(body, kDate);
        put32(body, late ? 0 : crc); put32(body, late ? 0 : static_cast<quint32>(payload.size()));
        put32(body, late ? 0 : size);
        put16(body, static_cast<quint32>(name.size())); put16(body, 0);
        body += name;
        body += payload;
        if (late) {
            put32(body, 0x08074b50u); put32(body, crc); put32(body, static_cast<quint32>(payload.size())); put32(body, size);
        }
        put32(central, 0x02014b50u); put16(central, 20); put16(central, 20); put16(central, flags);
        put16(central, method); put16(central, kTime); put16(central, kDate);
        put32(central, crc); put32(central, static_cast<quint32>(payload.size())); put32(central, size);
        put16(central, static_cast<quint32>(name.size())); put16(central, 0); put16(central, 0);
        put16(central, 0); put16(central, 0); put32(central, 0); put32(central, offset);
        central += name;
    }
    QByteArray out = body + central;
    put32(out, 0x06054b50u); put16(out, 0); put16(out, 0);
    put16(out, static_cast<quint32>(entries.size())); put16(out, static_cast<quint32>(entries.size()));
    put32(out, static_cast<quint32>(central.size())); put32(out, static_cast<quint32>(body.size())); put16(out, 0);
    return out;
}

}  // namespace bld::test
