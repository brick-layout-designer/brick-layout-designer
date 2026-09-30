#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <optional>

namespace bld::import {

// A ZIP reader for untrusted archives (Studio .io, LDD .lxf, downloaded part
// and LDraw packages). Qt's QZipReader trusts the sizes an archive claims
// and, on a truncated deflate stream, keeps doubling its buffer until
// memory runs out. Here every size is bounded by the file's own size, data
// is inflated into a fixed buffer (Mark Adler's puff), and CRCs are checked.
// No ZIP64, encryption or methods other than stored and deflate.
class SafeZip {
public:
    struct Entry {
        QString name;
        bool    isDir = false;
        bool    isSymLink = false;
        quint16 method = 0;
        quint32 crc = 0;
        qint64  compressedSize = 0;
        qint64  size = 0;
        qint64  localHeaderOffset = 0;
    };

    explicit SafeZip(QByteArray archive);
    static std::optional<SafeZip> open(const QString& path);

    // False when the central directory is missing or damaged.
    bool isValid() const { return valid_; }
    const QList<Entry>& entries() const { return entries_; }
    const Entry* find(const QString& name, Qt::CaseSensitivity cs = Qt::CaseSensitive) const;

    // The entry's data, or nothing if it is damaged, bigger than `maxSize`,
    // or not stored / deflated.
    std::optional<QByteArray> read(const Entry& entry, qint64 maxSize = qint64(1) << 30) const;

private:
    QByteArray data_;
    QList<Entry> entries_;
    bool valid_ = false;
};

// The CRC-32 a ZIP entry carries.
quint32 zipCrc32(const QByteArray& data);

}  // namespace bld::import
