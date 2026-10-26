#include "NameMigration.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

namespace bld::core {

AppName currentAppName() {
    return { QStringLiteral("CollaborativeBrickLayoutDesigner"), QStringLiteral("Collaborative Brick Layout Designer") };
}

AppName legacyAppName() {
    return { QStringLiteral("BrickLayoutDesigner"), QStringLiteral("Brick Layout Designer") };
}

namespace {

#ifdef Q_OS_WIN
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity kPathCase = Qt::CaseSensitive;
#endif

bool isSeparator(QChar c) { return c == QLatin1Char('/') || c == QLatin1Char('\\'); }

QString rewriteString(const QString& s, const QString& from, const QString& to) {
    // As QStandardPaths gives it ('/'), and as the platform writes it.
    const QString forms[2][2] = { { from, to }, { QDir::toNativeSeparators(from), QDir::toNativeSeparators(to) } };
    for (const auto& form : forms) {
        const QString& f = form[0];
        if (f.isEmpty() || !s.startsWith(f, kPathCase)) continue;
        if (s.size() == f.size() || isSeparator(s.at(f.size()))) return form[1] + s.mid(f.size());
    }
    return s;
}

// Every `from` in `bytes` that ends at a folder boundary (a separator or the
// closing quote of a JSON string) becomes `to`.
QByteArray rewriteBytes(QByteArray bytes, const QByteArray& from, const QByteArray& to) {
    if (from.isEmpty()) return bytes;
    qsizetype at = 0;
    while ((at = bytes.indexOf(from, at)) >= 0) {
        const qsizetype end = at + from.size();
        const char next = end < bytes.size() ? bytes.at(end) : '"';
        if (next == '/' || next == '\\' || next == '"') {
            bytes.replace(at, from.size(), to);
            at += to.size();
        } else {
            at = end;
        }
    }
    return bytes;
}

QByteArray jsonEscaped(const QString& s) {
    return s.toUtf8().replace("\\", "\\\\");
}

// Where `name` keeps `location`: QStandardPaths reads the names from
// QCoreApplication, so they are swapped in for the call.
QString locationFor(const AppName& name, QStandardPaths::StandardLocation location) {
    const QString org = QCoreApplication::organizationName();
    const QString app = QCoreApplication::applicationName();
    QCoreApplication::setOrganizationName(name.organization);
    QCoreApplication::setApplicationName(name.application);
    const QString path = QStandardPaths::writableLocation(location);
    QCoreApplication::setOrganizationName(org);
    QCoreApplication::setApplicationName(app);
    return path;
}

bool copyTree(const QString& from, const QString& to) {
    if (!QDir().mkpath(to)) return false;
    bool ok = true;
    QDirIterator it(from, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo info = it.fileInfo();
        const QString target = to + QLatin1Char('/') + QDir(from).relativeFilePath(path);
        if (info.isDir() && !info.isSymLink()) {
            ok = QDir().mkpath(target) && ok;
        } else {
            QDir().mkpath(QFileInfo(target).absolutePath());
            ok = QFile::copy(path, target) && ok;
        }
    }
    return ok;
}

void moveData(const QString& oldData, const QString& newData, NameMigrationReport& report) {
    if (oldData.isEmpty() || newData.isEmpty() || QDir::cleanPath(oldData) == QDir::cleanPath(newData)) return;
    const QFileInfo old(oldData);
    // Nothing to carry over, or an earlier start already left a link here.
    if (!old.isDir() || old.isSymLink()) return;
    QDir fresh(newData);
    if (fresh.exists() && !fresh.isEmpty()) return;  // the new names have data of their own: keep it
    if (fresh.exists()) QDir().rmdir(newData);
    QDir().mkpath(QFileInfo(newData).absolutePath());
#ifndef Q_OS_WIN
    // One rename on the same disk (server parts and the LDraw library can be
    // gigabytes), and a link at the old path so an older version still finds it.
    if (QDir().rename(oldData, newData)) {
        report.dataMoved = true;
        if (!QFile::link(newData, oldData))
            report.error = QStringLiteral("Moved the data folder, but could not leave a link at %1").arg(oldData);
        return;
    }
#endif
    // Windows (a .lnk is no folder link), or another disk: copy, and keep the old folder.
    if (copyTree(oldData, newData)) {
        report.dataCopied = true;
    } else {
        report.error = QStringLiteral("Could not copy everything from %1 to %2").arg(oldData, newData);
    }
}

}  // namespace

QVariant rewritePathPrefix(const QVariant& value, const QString& from, const QString& to) {
    if (from.isEmpty()) return value;
    switch (value.typeId()) {
    case QMetaType::QString:
        return rewriteString(value.toString(), from, to);
    case QMetaType::QStringList: {
        QStringList list = value.toStringList();
        for (QString& s : list) s = rewriteString(s, from, to);
        return list;
    }
    case QMetaType::QVariantList: {
        QVariantList list = value.toList();
        for (QVariant& v : list) v = rewritePathPrefix(v, from, to);
        return list;
    }
    case QMetaType::QByteArray: {
        // JSON kept as bytes (the server list, recent layouts); never binary
        // state such as window geometry.
        const QByteArray bytes = value.toByteArray();
        const QByteArray head = bytes.trimmed().left(1);
        if (head != "[" && head != "{") return value;
        QByteArray out = rewriteBytes(bytes, from.toUtf8(), to.toUtf8());
        out = rewriteBytes(out, jsonEscaped(QDir::toNativeSeparators(from)), jsonEscaped(QDir::toNativeSeparators(to)));
        return out;
    }
    default:
        return value;
    }
}

NameMigrationReport migrateFromName(const AppName& from) {
    NameMigrationReport report;
    const AppName to{ QCoreApplication::organizationName(), QCoreApplication::applicationName() };
    const QString oldData = locationFor(from, QStandardPaths::AppDataLocation);
    const QString oldCache = locationFor(from, QStandardPaths::CacheLocation);
    const QString newData = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString newCache = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);

    // Only these names' own keys (macOS would add the system-wide ones).
    QSettings now;
    now.setFallbacksEnabled(false);
    if (now.contains(QLatin1String(kNameMigrationDoneKey))) {
        report.alreadyDone = true;
        return report;
    }
    if (from.organization != to.organization || from.application != to.application) {
        // Settings: only into an empty store, and the old one stays as it is.
        QSettings old(from.organization, from.application);
        old.setFallbacksEnabled(false);
        const QStringList keys = old.allKeys();
        if (now.allKeys().isEmpty() && !keys.isEmpty()) {
            for (const QString& key : keys) {
                QVariant v = rewritePathPrefix(old.value(key), oldData, newData);
                v = rewritePathPrefix(v, oldCache, newCache);
                now.setValue(key, v);
            }
            report.settingsCopied = true;
        }
        // The cache rebuilds by itself; only the data folder moves.
        moveData(oldData, newData, report);
    }
    const QString version = QCoreApplication::applicationVersion();
    now.setValue(QLatin1String(kNameMigrationDoneKey), version.isEmpty() ? QStringLiteral("dev") : version);
    now.sync();
    return report;
}

}  // namespace bld::core
