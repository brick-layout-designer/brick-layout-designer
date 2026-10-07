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
// No ZIP64 or methods other than stored and deflate. Entries encrypted with
// traditional PKWARE "ZipCrypto" (as BrickLink Studio writes .io files) are
// read when the caller passes the password; AES-encrypted entries never are.
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
        bool    encrypted = false;  // general-purpose flag bit 0
        bool    aes = false;        // WinZip AES (method 99): never readable here
        quint16 flags = 0;
        quint16 modTime = 0;        // DOS time; ZipCrypto's check byte with flag bit 3
    };

    // Why read() gave nothing.
    enum class ReadError {
        None,
        Damaged,        // bad header, failed inflation or CRC
        TooBig,         // bigger than the caller's maxSize
        Unsupported,    // a method other than stored / deflate
        WrongPassword,  // ZipCrypto, and none of the passwords fit
        AesEncrypted,   // AES: refused
    };

    // `passwords`: tried in order on ZipCrypto entries. Without any,
    // encrypted entries can't be read.
    explicit SafeZip(QByteArray archive, QList<QByteArray> passwords = {});
    static std::optional<SafeZip> open(const QString& path, QList<QByteArray> passwords = {});

    // False when the central directory is missing or damaged.
    bool isValid() const { return valid_; }
    const QList<Entry>& entries() const { return entries_; }
    const Entry* find(const QString& name, Qt::CaseSensitivity cs = Qt::CaseSensitive) const;

    // The entry's data, or nothing if it is damaged, bigger than `maxSize`,
    // not stored / deflated, or encrypted with no matching password. `why`
    // says which.
    std::optional<QByteArray> read(const Entry& entry, qint64 maxSize = qint64(1) << 30,
                                   ReadError* why = nullptr) const;

private:
    QByteArray data_;
    QList<QByteArray> passwords_;
    QList<Entry> entries_;
    bool valid_ = false;
};

// The CRC-32 a ZIP entry carries.
quint32 zipCrc32(const QByteArray& data);

}  // namespace bld::import
