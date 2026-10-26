#include "PartsZip.h"

#include "ArchivePath.h"
#include "zip/SafeZip.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

#include <utility>
#include <vector>

namespace bld::import {

namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("PartsZip", text);
}

// The key a part or set file is listed under: its name without .xml, or
// .set.xml for a set.
QString partKeyOf(const QString& fileName) {
    QString stem = fileName;
    if (stem.endsWith(QLatin1String(".set.xml"), Qt::CaseInsensitive)) stem.chop(8);
    else if (stem.endsWith(QLatin1String(".xml"), Qt::CaseInsensitive)) stem.chop(4);
    else return {};
    return stem;
}

} // namespace

PartsZipResult addPartsFromZip(const QString& zipPath, const QString& destRoot,
                               const PartsZipLimits& limits) {
    PartsZipResult out;
    const QFileInfo info(zipPath);
    if (!info.isFile()) {
        out.error = tr("The file isn't there any more.");
        return out;
    }
    if (info.size() > limits.maxArchiveBytes) {
        out.error = tr("The zip is too big to be a parts folder (over %1 MB).")
                        .arg(limits.maxArchiveBytes / (qint64{ 1024 } * 1024));
        return out;
    }
    const auto zip = SafeZip::open(zipPath);
    if (!zip || !zip->isValid()) {
        out.error = tr("This file isn't a zip, or it is damaged.");
        return out;
    }
    if (zip->entries().size() > limits.maxEntries) {
        out.error = tr("The zip has too many files to be a parts folder (over %1).").arg(limits.maxEntries);
        return out;
    }

    // Which entries go where. Names use "/" (SafeZip keeps them as written;
    // a "\" from a Windows tool counts as a folder too).
    bool hasFolder = false;
    for (const auto& e : zip->entries()) {
        const QString name = QString(e.name).replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (name.contains(QLatin1Char('/')) && !name.startsWith(QLatin1Char('/'))) hasFolder = true;
    }
    const QString ownFolder = info.completeBaseName();
    struct Item {
        const SafeZip::Entry* entry;
        QString rel; // relative to destRoot
    };
    std::vector<Item> items;
    qint64 total = 0;
    QStringList folders;
    for (const auto& e : zip->entries()) {
        QString name = QString(e.name).replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (e.isSymLink) {
            out.error = tr("The zip holds a link (%1), which parts folders never do.").arg(e.name);
            return out;
        }
        // Every name, kept or not, must stay inside the destination.
        if (resolveArchiveEntryPath(destRoot, name).isEmpty()) {
            out.error = tr("The zip holds a file that would land outside the parts folder (%1).").arg(e.name);
            return out;
        }
        if (e.isDir) continue;
        if (!name.contains(QLatin1Char('/'))) {
            if (hasFolder) continue; // README.txt beside the design folder
            name = ownFolder + QLatin1Char('/') + name;
        }
        if (e.size > limits.maxEntryBytes) {
            out.error = tr("%1 in the zip is too big for a part.").arg(e.name);
            return out;
        }
        total += e.size;
        if (total > limits.maxTotalBytes) {
            out.error = tr("The zip unpacks to more than %1 MB, too much for a parts folder.")
                            .arg(limits.maxTotalBytes / (qint64{ 1024 } * 1024));
            return out;
        }
        const QString abs = resolveArchiveEntryPath(destRoot, name);
        if (abs.isEmpty()) {
            out.error = tr("The zip holds a file that would land outside the parts folder (%1).").arg(e.name);
            return out;
        }
        const QString folder = name.section(QLatin1Char('/'), 0, 0);
        if (!folders.contains(folder)) folders << folder;
        const QString key = partKeyOf(name.section(QLatin1Char('/'), -1));
        if (!key.isEmpty() && !name.contains(QLatin1String("/config/"))) out.partKeys << key;
        items.push_back({ &e, name });
    }
    if (out.partKeys.isEmpty()) {
        out.error = tr("There are no parts in this zip (no part .xml files).");
        return out;
    }

    // Read everything before writing anything: a damaged entry leaves the
    // parts folder as it was.
    std::vector<std::pair<QString, QByteArray>> files;
    files.reserve(items.size());
    for (const Item& it : items) {
        SafeZip::ReadError why = SafeZip::ReadError::None;
        auto data = zip->read(*it.entry, limits.maxEntryBytes, &why);
        if (!data) {
            out.error = why == SafeZip::ReadError::TooBig
                            ? tr("%1 in the zip is too big for a part.").arg(it.entry->name)
                            : tr("%1 in the zip is damaged.").arg(it.entry->name);
            return out;
        }
        files.emplace_back(resolveArchiveEntryPath(destRoot, it.rel), std::move(*data));
    }
    for (const auto& [abs, data] : files) {
        QDir().mkpath(QFileInfo(abs).absolutePath());
        QSaveFile f(abs);
        if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
            out.error = tr("Couldn't write %1.").arg(QDir::toNativeSeparators(abs));
            return out;
        }
    }
    out.folders = folders;
    out.ok = true;
    return out;
}

} // namespace bld::import
