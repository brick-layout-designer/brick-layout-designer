#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

namespace bld::import {

// Writes a plain ZIP (PKZIP 2.0: no ZIP64, no encryption) in memory, with
// each entry stored or deflated. SafeZip reads what it writes.
class ZipWriter {
public:
    enum class Method { Stored, Deflated };

    // Deflated entries that don't get smaller are stored instead.
    void add(const QString& name, const QByteArray& data, Method method = Method::Deflated);
    QByteArray finish() const;

private:
    struct Entry {
        QByteArray name;
        QByteArray data;  // as written
        quint16 method = 0;
        quint32 crc = 0;
        quint32 size = 0;
    };
    QList<Entry> entries_;
};

}  // namespace bld::import
