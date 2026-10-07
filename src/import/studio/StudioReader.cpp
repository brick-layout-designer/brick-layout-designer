#include "StudioReader.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QString>
#include <QTemporaryDir>

#include <memory>

#include "../zip/SafeZip.h"

namespace bld::import {

namespace {

// Limits for untrusted archives. A big set's model.ldr is under a megabyte;
// the cap leaves room for the model2.ldr fallback (72 MB in LEGO 70620).
constexpr qint64 kMaxModelBytes = qint64(128) << 20;
constexpr qint64 kMaxCustomPartBytes = qint64(64) << 20;
constexpr qint64 kMaxCustomPartsTotal = qint64(512) << 20;
constexpr int kMaxCustomParts = 5000;

QString friendly(SafeZip::ReadError why, const QString& entry) {
    switch (why) {
    case SafeZip::ReadError::AesEncrypted:
        return QStringLiteral("This Studio file is locked with a password (AES encryption), so it can't be opened. "
                              "Save it again in Studio without a password and import that copy.");
    case SafeZip::ReadError::WrongPassword:
        return QStringLiteral("This Studio file is encrypted in a way we don't recognise. "
                              "Open it in a current version of Studio, save it again and import that copy.");
    case SafeZip::ReadError::TooBig:
        return QStringLiteral("The model in this Studio file (%1) is too large to import.").arg(entry);
    case SafeZip::ReadError::Unsupported:
        return QStringLiteral("This Studio file uses a compression method we can't read (%1). "
                              "Save it again in Studio and import that copy.").arg(entry);
    case SafeZip::ReadError::Damaged:
    case SafeZip::ReadError::None:
        break;
    }
    return QStringLiteral("The model in this Studio file is damaged (%1 failed its check).").arg(entry);
}

// The entry holding the model: model.ldr at the root (any case), else any
// other .ldr but the legacy modelv1.ldr, with model2.ldr last.
QList<const SafeZip::Entry*> modelCandidates(const SafeZip& zip) {
    QList<const SafeZip::Entry*> out;
    if (const auto* e = zip.find(QStringLiteral("model.ldr"), Qt::CaseInsensitive)) out << e;
    const SafeZip::Entry* model2 = nullptr;
    for (const auto& e : zip.entries()) {
        if (e.isDir || out.contains(&e) || !e.name.endsWith(QStringLiteral(".ldr"), Qt::CaseInsensitive)) continue;
        const QString base = e.name.section(QLatin1Char('/'), -1).toLower();
        if (base == QLatin1String("modelv1.ldr")) continue;
        if (base == QLatin1String("model2.ldr")) { model2 = &e; continue; }
        out << &e;
    }
    if (model2) out << model2;
    return out;
}

// LDraw is text: nothing after a NUL belongs to the model.
QByteArray cutAtNul(QByteArray data) {
    const qsizetype nul = data.indexOf('\0');
    if (nul >= 0) data.truncate(nul);
    return data;
}

bool hasContent(const QByteArray& ldr) {
    // Anything beyond comments: a reference or geometry line.
    for (const QByteArray& line : ldr.split('\n')) {
        const QByteArray t = line.trimmed();
        if (!t.isEmpty() && t.at(0) >= '1' && t.at(0) <= '5') return true;
    }
    return false;
}

// CustomParts/<name>.dat, directly in that folder. The name is used as a
// file name, so only plain ones.
QString customPartName(const SafeZip::Entry& e) {
    static const QRegularExpression kSafe(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_ .+()-]{0,200}\\.dat$"),
                                          QRegularExpression::CaseInsensitiveOption);
    if (e.isDir || e.isSymLink) return {};
    const QString name = QString(e.name).replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (!name.startsWith(QStringLiteral("CustomParts/"), Qt::CaseInsensitive)) return {};
    QString rest = name.mid(12);
    if (rest.contains(QLatin1Char('/')) || rest.contains(QStringLiteral("..")) || !kSafe.match(rest).hasMatch())
        return {};
    return rest;
}

}  // namespace

QList<QByteArray> studioPasswords() {
    // Studio's own (the same in every copy) and an empty one, which some
    // tools write.
    return { QByteArrayLiteral("soho0909"), QByteArray() };
}

LDrawReadResult readStudioIo(const QString& path) {
    LDrawReadResult out;
    QFileInfo fi(path);
    if (!fi.exists() || !fi.isFile()) {
        out.error = QStringLiteral("File not found: %1").arg(path);
        return out;
    }
    const auto zip = SafeZip::open(path, studioPasswords());
    if (!zip) {
        out.error = QStringLiteral("This isn't a Studio model, or the file is damaged: "
                                   "Studio .io files are ZIP archives and this one can't be opened.");
        return out;
    }

    QByteArray ldr;
    SafeZip::ReadError firstError = SafeZip::ReadError::None;
    QString firstEntry;
    const auto candidates = modelCandidates(*zip);
    for (const SafeZip::Entry* entry : candidates) {
        SafeZip::ReadError why = SafeZip::ReadError::None;
        auto data = zip->read(*entry, kMaxModelBytes, &why);
        if (!data) {
            if (firstError == SafeZip::ReadError::None) { firstError = why; firstEntry = entry->name; }
            // Every entry of an AES file is AES; no point trying the rest.
            if (why == SafeZip::ReadError::AesEncrypted || why == SafeZip::ReadError::WrongPassword) break;
            continue;
        }
        QByteArray text = cutAtNul(std::move(*data));
        if (hasContent(text)) { ldr = std::move(text); break; }
    }
    if (ldr.isEmpty()) {
        if (firstError != SafeZip::ReadError::None) out.error = friendly(firstError, firstEntry);
        else if (candidates.isEmpty())
            out.error = QStringLiteral("This Studio file has no model in it (model.ldr is missing).");
        else
            out.error = QStringLiteral("The model in this Studio file is empty.");
        return out;
    }

    auto dir = std::make_shared<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/bld-studio-XXXXXX"));
    if (!dir->isValid()) {
        out.error = QStringLiteral("Can't unpack the Studio file: no room in the temporary folder (%1).")
                        .arg(dir->errorString());
        return out;
    }

    // The model's own parts.
    const QString partsDir = dir->filePath(QStringLiteral("CustomParts"));
    QDir().mkpath(partsDir);
    int customParts = 0;
    qint64 customBytes = 0;
    QStringList unreadable;
    for (const auto& e : zip->entries()) {
        const QString name = customPartName(e);
        if (name.isEmpty()) continue;
        if (++customParts > kMaxCustomParts || customBytes + e.size > kMaxCustomPartsTotal) {
            out.warnings << QStringLiteral("This Studio file has more custom parts than we unpack; some may be missing.");
            break;
        }
        auto data = zip->read(e, kMaxCustomPartBytes);
        if (!data) { unreadable << name; continue; }
        customBytes += data->size();
        QFile f(QDir(partsDir).filePath(name));
        if (!f.open(QIODevice::WriteOnly) || f.write(*data) != data->size()) {
            out.error = QStringLiteral("Can't unpack the Studio file's custom parts: %1").arg(f.errorString());
            return out;
        }
    }
    if (!unreadable.isEmpty())
        out.warnings << QStringLiteral("Some custom parts in this Studio file couldn't be unpacked and won't show: %1")
                            .arg(unreadable.join(QStringLiteral(", ")));

    // The model goes through readLDraw() so it shares every line handler.
    const QString modelPath = dir->filePath(QStringLiteral("model.ldr"));
    {
        QFile tmp(modelPath);
        if (!tmp.open(QIODevice::WriteOnly) || tmp.write(ldr) != ldr.size()) {
            out.error = QStringLiteral("Can't unpack the Studio file: %1").arg(tmp.errorString());
            return out;
        }
    }
    LDrawReadResult nested = readLDraw(modelPath);
    QFile::remove(modelPath);
    if (!nested.ok) {
        if (nested.error.isEmpty()) nested.error = QStringLiteral("The model in this Studio file can't be read.");
        return nested;
    }
    nested.warnings = out.warnings + nested.warnings;
    if (customParts > 0) {
        nested.extraPartDirs << partsDir;
        nested.extracted = std::move(dir);
    }
    return nested;
}

}  // namespace bld::import
