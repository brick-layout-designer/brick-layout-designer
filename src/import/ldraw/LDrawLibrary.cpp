#include "LDrawLibrary.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace bld::import {

LDrawLibrary::LDrawLibrary(QString root) : root_(std::move(root)) {}

bool LDrawLibrary::looksValid() const {
    if (root_.isEmpty()) return false;
    QDir d(root_);
    if (!d.exists()) return false;
    // LDConfig.ldr is the colour palette and is present in every
    // distribution we care about. Without it our colour resolution
    // would just hard-code the bundled palette table — pointing at a
    // root without it suggests the user picked the wrong directory.
    if (!QFileInfo::exists(d.absoluteFilePath(QStringLiteral("LDConfig.ldr")))) return false;
    if (!QFileInfo(d.absoluteFilePath(QStringLiteral("parts"))).isDir()) return false;
    return true;
}

const LDrawLibrary::FileIndex& LDrawLibrary::indexForSubdir(const QString& subdir) const {
    auto it = indexBySubdir_.constFind(subdir);
    if (it != indexBySubdir_.constEnd()) return it.value();

    FileIndex index;
    QDir d(subdir.isEmpty() ? root_ : QDir(root_).absoluteFilePath(subdir));
    if (d.exists()) {
        // entryInfoList returns FileInfos for every file in this
        // directory once. We bucket them by lowercased filename so
        // subsequent resolve() calls are O(1) hash lookups regardless
        // of the directory's size — LDraw's parts/ has ~22 000 entries
        // and the previous "scan on every miss" path was the dominant
        // cost of importing anything non-trivial.
        const QFileInfoList entries = d.entryInfoList(QDir::Files);
        index.reserve(entries.size());
        for (const QFileInfo& fi : entries) {
            index.insert(fi.fileName().toLower(), fi.absoluteFilePath());
        }
    }
    return *indexBySubdir_.insert(subdir, std::move(index));
}

QString LDrawLibrary::resolve(const QString& filename) const {
    if (root_.isEmpty() || filename.isEmpty()) return {};

    // Memoised result cache. Hits cover the "stud.dat referenced
    // 50 000 times" case at O(1) per call.
    const QString cacheKey = filename.toLower();
    if (auto it = resolveCache_.constFind(cacheKey); it != resolveCache_.constEnd()) {
        return it.value();
    }

    // Normalise the reference: LDraw `.dat` lines often write paths
    // with backslashes (Windows-era convention; many parts are still
    // shipped that way). Convert to forward slashes so we can split
    // out an optional subdirectory hint like "s/3001s01.dat" or
    // "48/box5.dat".
    QString rel = filename;
    rel.replace(QChar('\\'), QChar('/'));

    auto saveAndReturn = [&, this](const QString& v) -> QString {
        resolveCache_.insert(cacheKey, v);
        return v;
    };

    // 1) If the filename already specifies a subdir prefix, resolve
    //    that exact relative path against root, parts/, and p/. The
    //    subdir-rooted variant uses the per-directory index so it's
    //    still O(1) instead of O(dir-size).
    if (rel.contains(QChar('/'))) {
        const QString tail = rel.section(QChar('/'), -1);
        const QString head = rel.section(QChar('/'), 0, -2);
        const QStringList prefixes = {
            QStringLiteral("parts/") + head,
            QStringLiteral("p/") + head,
            head,
        };
        for (const QString& p : prefixes) {
            const auto& idx = indexForSubdir(p);
            const auto hit = idx.constFind(tail.toLower());
            if (hit != idx.constEnd()) return saveAndReturn(hit.value());
        }
        // Fall through to bare-name lookup using the trailing component.
        rel = tail;
    }

    // 2) Bare filename: walk LDraw's stock search order. parts/ first
    //    so a top-level part (e.g. "3001.dat") wins over a like-named
    //    primitive in p/. p/48/ and p/8/ are resolution variants we
    //    treat at the same priority as p/ — author tools have always
    //    been free to reference either depending on intended fidelity.
    static const QStringList kDirs = {
        QStringLiteral("parts"),
        QStringLiteral("p"),
        QStringLiteral("p/48"),
        QStringLiteral("p/8"),
        QStringLiteral("parts/s"),
        QStringLiteral(""),  // root itself, last
    };
    const QString needle = rel.toLower();
    for (const QString& sub : kDirs) {
        const auto& idx = indexForSubdir(sub);
        const auto it = idx.constFind(needle);
        if (it != idx.constEnd()) return saveAndReturn(it.value());
    }
    return saveAndReturn(QString());
}

namespace {

QString stemLower(QString name) {
    name = name.trimmed().toLower();
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    name = name.mid(name.lastIndexOf(QLatin1Char('/')) + 1);
    if (name.endsWith(QStringLiteral(".dat"))) name.chop(4);
    return name;
}

}  // namespace

QStringList LDrawLibrary::formerNames(const QString& partName) const {
    if (!movedFromBuilt_) {
        movedFromBuilt_ = true;
        static const QRegularExpression moved(
            QStringLiteral("^0\\s+~Moved\\s+to\\s+(\\S+)"),
            QRegularExpression::CaseInsensitiveOption);
        for (auto it = indexForSubdir(QStringLiteral("parts")).cbegin(),
                  end = indexForSubdir(QStringLiteral("parts")).cend(); it != end; ++it) {
            QFile f(it.value());
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
            const auto m = moved.match(QString::fromUtf8(f.readLine(256)));
            if (m.hasMatch()) movedFrom_[stemLower(m.captured(1))].append(stemLower(it.key()));
        }
    }
    QStringList out;
    QStringList pending{ stemLower(partName) };
    while (!pending.isEmpty()) {
        for (const QString& old : movedFrom_.value(pending.takeFirst())) {
            if (out.contains(old)) continue;
            out.append(old);
            pending.append(old);
        }
    }
    return out;
}

}  // namespace bld::import
