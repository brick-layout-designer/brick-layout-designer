#include "ZipWriter.h"

#include "SafeZip.h"

#include <QtEndian>

namespace bld::import {

namespace {

// Raw deflate: qCompress gives a 4-byte length, then a zlib stream (2-byte
// header, the deflate data, 4-byte Adler-32).
QByteArray rawDeflate(const QByteArray& data) {
    const QByteArray z = qCompress(data, 9);
    return z.size() > 10 ? z.mid(6, z.size() - 10) : QByteArray();
}

void put16(QByteArray& out, quint32 v) {
    char b[2];
    qToLittleEndian<quint16>(static_cast<quint16>(v), b);
    out.append(b, 2);
}

void put32(QByteArray& out, quint32 v) {
    char b[4];
    qToLittleEndian<quint32>(v, b);
    out.append(b, 4);
}

}  // namespace

void ZipWriter::add(const QString& name, const QByteArray& data, Method method) {
    Entry e;
    e.name = name.toUtf8();
    e.crc = zipCrc32(data);
    e.size = static_cast<quint32>(data.size());
    e.data = data;
    if (method == Method::Deflated && !data.isEmpty()) {
        QByteArray packed = rawDeflate(data);
        if (!packed.isEmpty() && packed.size() < data.size()) {
            e.data = std::move(packed);
            e.method = 8;
        }
    }
    entries_.push_back(std::move(e));
}

QByteArray ZipWriter::finish() const {
    QByteArray out, central;
    for (const auto& e : entries_) {
        const auto offset = static_cast<quint32>(out.size());
        // Local file header. Bit 11: the name is UTF-8.
        put32(out, 0x04034b50u);
        put16(out, 20);
        put16(out, 0x0800);
        put16(out, e.method);
        put16(out, 0);  // time
        put16(out, 0x21);  // date: 1980-01-01, so the same content gives the same bytes
        put32(out, e.crc);
        put32(out, static_cast<quint32>(e.data.size()));
        put32(out, e.size);
        put16(out, static_cast<quint32>(e.name.size()));
        put16(out, 0);
        out += e.name;
        out += e.data;
        // Central directory record.
        put32(central, 0x02014b50u);
        put16(central, 20);
        put16(central, 20);
        put16(central, 0x0800);
        put16(central, e.method);
        put16(central, 0);
        put16(central, 0x21);
        put32(central, e.crc);
        put32(central, static_cast<quint32>(e.data.size()));
        put32(central, e.size);
        put16(central, static_cast<quint32>(e.name.size()));
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put32(central, 0);
        put32(central, offset);
        central += e.name;
    }
    const auto centralOffset = static_cast<quint32>(out.size());
    out += central;
    put32(out, 0x06054b50u);
    put16(out, 0);
    put16(out, 0);
    put16(out, static_cast<quint32>(entries_.size()));
    put16(out, static_cast<quint32>(entries_.size()));
    put32(out, static_cast<quint32>(central.size()));
    put32(out, centralOffset);
    put16(out, 0);
    return out;
}

}  // namespace bld::import
