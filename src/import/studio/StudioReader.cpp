#include "StudioReader.h"

#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>

#include "../zip/SafeZip.h"

namespace bld::import {

namespace {

// Models are text; the cap keeps a hostile archive from asking for more.
constexpr qint64 kMaxModelBytes = qint64(64) << 20;

// The model: model.ldr at the root (any case), else the first .ldr.
QByteArray readModelLdr(const SafeZip& zip) {
    const SafeZip::Entry* entry = zip.find(QStringLiteral("model.ldr"), Qt::CaseInsensitive);
    if (!entry) {
        for (const auto& e : zip.entries())
            if (!e.isDir && e.name.endsWith(QStringLiteral(".ldr"), Qt::CaseInsensitive)) { entry = &e; break; }
    }
    if (!entry) return {};
    QByteArray data = zip.read(*entry, kMaxModelBytes).value_or(QByteArray());
    // LDraw is text: nothing after a NUL belongs to the model.
    const qsizetype nul = data.indexOf('\0');
    if (nul >= 0) data.truncate(nul);
    return data;
}

}  // namespace

LDrawReadResult readStudioIo(const QString& path) {
    LDrawReadResult out;
    QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) {
        out.error = QStringLiteral("File not found: %1").arg(path);
        return out;
    }
    const auto zip = SafeZip::open(path);
    if (!zip) {
        out.error = QStringLiteral(
            "Not a readable Studio archive (expected a ZIP containing model.ldr): %1")
            .arg(path);
        return out;
    }
    const QByteArray ldr = readModelLdr(*zip);
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
}

}  // namespace bld::import
