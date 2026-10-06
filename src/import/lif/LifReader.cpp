#include "LifReader.h"

#include <QFile>

#include <functional>

namespace bld::import {

namespace {

quint32 beU32(const QByteArray& d, qint64 off) {
    if (off < 0 || off + 4 > d.size()) return 0;
    const auto* p = reinterpret_cast<const quint8*>(d.constData() + off);
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) |
           (quint32(p[2]) << 8)  |  quint32(p[3]);
}

quint16 beU16(const QByteArray& d, qint64 off) {
    if (off < 0 || off + 2 > d.size()) return 0;
    const auto* p = reinterpret_cast<const quint8*>(d.constData() + off);
    return (quint16(p[0]) << 8) | quint16(p[1]);
}

}  // namespace

bool LifReader::open(const QString& path) {
    errorString_.clear();
    entries_.clear();
    data_.clear();
    file_ = std::make_unique<QFile>(path);
    if (!file_->open(QIODevice::ReadOnly)) {
        errorString_ = QStringLiteral("Could not open %1: %2").arg(path, file_->errorString());
        file_.reset();
        return false;
    }
    if (uchar* mapped = file_->map(0, file_->size())) {
        data_ = QByteArray::fromRawData(reinterpret_cast<const char*>(mapped),
                                        static_cast<qsizetype>(file_->size()));
    } else {
        data_ = file_->readAll();
    }
    return parseArchive();
}

bool LifReader::openNested(const LifReader& outer, const QString& lifPath) {
    errorString_.clear();
    entries_.clear();
    data_.clear();
    file_.reset();
    const auto it = outer.entries_.constFind(lifPath);
    if (it == outer.entries_.constEnd()) {
        errorString_ = QStringLiteral("%1 not found in archive").arg(lifPath);
        return false;
    }
    data_ = QByteArray::fromRawData(outer.data_.constData() + it->offset,
                                    static_cast<qsizetype>(it->size));
    return parseArchive();
}

bool LifReader::parseArchive() {
    if (data_.size() < 84) {
        errorString_ = QStringLiteral("Truncated file (need 84 byte header, got %1)")
                            .arg(data_.size());
        return false;
    }
    if (data_.left(4) != QByteArray("LIFF", 4)) {
        errorString_ = QStringLiteral("Bad magic — not a LIF archive");
        return false;
    }
    if (!parseTableOfContents()) {
        // parseTableOfContents fills errorString_ on failure.
        return false;
    }
    return true;
}

bool LifReader::parseTableOfContents() {
    // The Python reference treats the TOC as a recursive walk of
    // directory blocks rooted at offset (uint32_be(72) + 64). Each
    // directory has a 36-byte header then a uint32 child count; each
    // child is a "block" of the form:
    //
    //   u32  block-size        // size including the inner payload (file size + 20)
    //   u16  type              // 1 = dir, 2 = file
    //   6 bytes skipped
    //   wchar* name           // UTF-16BE, null-terminated, bytes at +1 are ASCII
    //   6 bytes skipped after name
    //   if type == 2:
    //     u32  size+20         // already at the same position; size = block-size - 20
    //     24 bytes skipped
    //
    // packedFilesOffset starts at 84 (after the file header) and grows
    // by 20 for each entry header walked + the file's payload size.
    // That counter is what tells us where the next *file's* bytes
    // start within the archive.
    qint64 packedOffset = 84;

    // Lambda is recursive via std::function so we can capture by
    // reference and call ourselves.
    using Recurse = std::function<qint64(const QString&, qint64)>;
    Recurse recurse = [&](const QString& prefix, qint64 offset) -> qint64 {
        if (prefix.isEmpty()) {
            offset += 36;
        } else {
            offset += 4;
        }
        const quint32 count = beU32(data_, offset);
        // The python reference reads uint32(offset) inside the
        // `range()` call without advancing offset. Match that: don't
        // consume the 4 bytes of the count here; the per-entry
        // `offset += 4` below covers it.
        for (quint32 i = 0; i < count; ++i) {
            // Each entry starts with a 4-byte word (size hint?) the
            // python reference skips via `offset += 4` before reading
            // entryType. Track meaning later if needed; for now we
            // don't read it.
            offset += 4;
            const quint16 entryType = beU16(data_, offset);
            offset += 2;
            offset += 4;  // 4 reserved bytes after type

            // Read the wchar name. Python treats it as 2 bytes per
            // char, accessing fileData[offset+1] as the ASCII byte.
            // That's UTF-16BE: high byte at +0 is 0 for ASCII, low
            // byte at +1 is the printable char. We read until the
            // ASCII byte is 0.
            QString name;
            while (offset + 1 < data_.size()) {
                const auto low = static_cast<quint8>(data_[offset + 1]);
                if (low == 0) break;
                name.append(QChar(static_cast<unsigned char>(low)));
                offset += 2;
            }
            offset += 6;  // null terminator (2) + 4 trailing bytes

            const QString fullPath = prefix + QChar('/') + name;
            if (entryType == 1) {
                // Directory: walk its children. The python increments
                // packedOffset by 20 for the dir header itself.
                packedOffset += 20;
                offset = recurse(fullPath, offset);
            } else if (entryType == 2) {
                // File entry. The python reads the file size from a
                // u32 at the CURRENT offset (post-name) — the value
                // is `actualSize + 20`. Then skips past it + 20 trail
                // bytes (24 total), per the reference implementation.
                packedOffset += 20;
                const quint32 sizePlus20 = beU32(data_, offset);
                FileEntry e;
                e.offset = packedOffset;
                e.size   = qint64(sizePlus20) - 20;
                if (e.size < 0 || e.offset + e.size > data_.size()) {
                    errorString_ = QStringLiteral(
                        "Corrupt file entry %1 (offset %2, size %3, archive size %4)")
                        .arg(fullPath).arg(e.offset).arg(e.size).arg(data_.size());
                    return offset;
                }
                entries_.insert(fullPath, e);
                packedOffset += e.size;
                offset += 24;
            } else {
                // The reference python implementation walks until the
                // recursive count is exhausted; some archives include
                // a few "type 0/N" trailing junk entries past the real
                // TOC and python silently exits when the count was
                // wrong-but-bounded. Match that tolerance: stop the
                // current directory's loop on unknown type rather than
                // failing the whole open. Recorded in a debug-only
                // note (errorString_ stays empty so callers don't see
                // a partial-success file as failed).
                return offset;
            }
        }
        return offset;
    };

    const qint64 tocStart = qint64(beU32(data_, 72)) + 64;
    if (tocStart < 0 || tocStart >= data_.size()) {
        errorString_ = QStringLiteral("Bad TOC offset (%1) for archive size %2")
                            .arg(tocStart).arg(data_.size());
        return false;
    }
    recurse(QString(), tocStart);
    return errorString_.isEmpty();
}

QStringList LifReader::fileList() const {
    QStringList out;
    out.reserve(entries_.size());
    for (auto it = entries_.constBegin(); it != entries_.constEnd(); ++it) {
        out.append(it.key());
    }
    out.sort();
    return out;
}

QByteArray LifReader::read(const QString& lifPath) const {
    auto it = entries_.constFind(lifPath);
    if (it == entries_.constEnd()) return {};
    const auto& e = it.value();
    if (e.offset < 0 || e.size < 0 || e.offset + e.size > data_.size()) return {};
    // Deep copy: data_ may alias a memory map that dies with this reader,
    // and mid() on raw data can return a view onto it.
    return QByteArray(data_.constData() + e.offset, static_cast<qsizetype>(e.size));
}

}  // namespace bld::import
