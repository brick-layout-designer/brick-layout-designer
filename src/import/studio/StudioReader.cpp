#include "StudioReader.h"

#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QtEndian>

// Qt's QZipReader is in the private-header area, but it's a stable
// internal API Qt ships everywhere we care about. The CMake for this
// target locates the private include dir and adds it; when the
// headers aren't available (stripped Qt install) we compile with
// BLD_NO_QZIPREADER and emit a runtime error instead of build-breaking.
#ifndef BLD_NO_QZIPREADER
#  include <private/qzipreader_p.h>
#endif

namespace bld::import {

namespace {

// Case-insensitive search for a candidate "model.ldr" entry anywhere
// in the archive. Studio usually uses `model.ldr` at the root, but
// older / variant exports might nest it.
// QZipReader allocates the sizes an archive's central directory claims
// before reading anything, so a damaged or hostile file can make it ask
// for gigabytes. Reject archives whose entries claim more compressed data
// than the file holds, or more expansion than deflate can give (~1032:1).
bool archiveSizesPlausible(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray d = f.readAll();
    const qint64 n = d.size();
    const auto u16 = [&](qint64 at) { return at + 2 <= n ? qFromLittleEndian<quint16>(d.constData() + at) : 0u; };
    const auto u32 = [&](qint64 at) { return at + 4 <= n ? qFromLittleEndian<quint32>(d.constData() + at) : 0u; };
    // End of central directory: the last "PK\5\6" within the final 64 KiB + 22 bytes.
    const qint64 eocd = d.lastIndexOf(QByteArrayLiteral("PK\x05\x06"));
    if (eocd < 0 || eocd < n - 65557) return false;
    const quint16 entries = u16(eocd + 10);
    qint64 at = u32(eocd + 16);
    for (int i = 0; i < entries; ++i) {
        if (at < 0 || at + 46 > n || u32(at) != 0x02014b50u) return false;
        const qint64 compressed = u32(at + 20), uncompressed = u32(at + 24);
        if (compressed > n || uncompressed > n * 1100 + 65536) return false;
        at += 46 + u16(at + 28) + u16(at + 30) + u16(at + 32);
    }
    return true;
}

// An entry's data. A damaged or hostile archive can claim any size, and
// the reader allocates what it claims: skip impossible sizes, and cut the
// text at the first NUL (LDraw is text; what follows is padding).
QByteArray entryText(QZipReader& zr, const QZipReader::FileInfo& info, qint64 archiveBytes) {
    // Deflate expands at most ~1032:1, so no real entry claims more.
    if (info.size > archiveBytes * 1100 + 65536) return {};
    QByteArray data = zr.fileData(info.filePath);
    const qsizetype nul = data.indexOf('\0');
    if (nul >= 0) data.truncate(nul);
    return data;
}

QByteArray readModelLdr(QZipReader& zr, qint64 archiveBytes) {
    const auto entries = zr.fileInfoList();
    // Prefer root-level model.ldr (case-insensitive match).
    for (const auto& info : entries) {
        if (!info.isFile) continue;
        if (info.filePath.compare(QStringLiteral("model.ldr"),
                                  Qt::CaseInsensitive) == 0) {
            return entryText(zr, info, archiveBytes);
        }
    }
    // Fallback: any .ldr anywhere. Studio sometimes wraps inside
    // subfolders; take the first match.
    for (const auto& info : entries) {
        if (!info.isFile) continue;
        if (info.filePath.endsWith(QStringLiteral(".ldr"), Qt::CaseInsensitive)) {
            return entryText(zr, info, archiveBytes);
        }
    }
    return {};
}

}  // namespace

LDrawReadResult readStudioIo(const QString& path) {
    LDrawReadResult out;
#ifdef BLD_NO_QZIPREADER
    out.error = QStringLiteral(
        "This build was compiled without QZipReader; Studio .io import is "
        "unavailable. Rebuild against a Qt install that includes the private "
        "headers package.");
    return out;
#else
    QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) {
        out.error = QStringLiteral("File not found: %1").arg(path);
        return out;
    }
    QZipReader zr(path);
    if (!zr.isReadable() || !archiveSizesPlausible(path)) {
        out.error = QStringLiteral(
            "Not a readable Studio archive (expected a ZIP containing model.ldr): %1")
            .arg(path);
        return out;
    }
    const QByteArray ldr = readModelLdr(zr, fi.size());
    if (ldr.isEmpty()) {
        out.error = QStringLiteral(
            "Studio archive has no model.ldr entry: %1").arg(path);
        return out;
    }
    // Dump the extracted LDR to a temp path and feed it through the
    // existing readLDraw() so we share every line-handler and title
    // extraction. The file is tiny relative to disk; the extra write
    // is cheap.
    QString tmpPath = QDir::tempPath() + QStringLiteral("/bld-studio-extract-")
                    + fi.completeBaseName()
                    + QStringLiteral(".ldr");
    {
        QFile tmp(tmpPath);
        if (!tmp.open(QIODevice::WriteOnly)) {
            out.error = QStringLiteral("Cannot write temp extract: %1").arg(tmp.errorString());
            return out;
        }
        tmp.write(ldr);
    }
    LDrawReadResult nested = readLDraw(tmpPath);
    QFile::remove(tmpPath);
    return nested;
#endif  // BLD_NO_QZIPREADER
}

}  // namespace bld::import
