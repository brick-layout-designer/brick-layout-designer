#include "HelpPages.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStringList>

namespace bld::ui::help {

namespace {

UrlOpener& opener() {
    static UrlOpener open = [](const QUrl& url) { QDesktopServices::openUrl(url); };
    return open;
}

// The sections of Getting_Started.htm (the web's /help page has the same).
const QStringList& sections() {
    static const QStringList ids{ QStringLiteral("getting-started"), QStringLiteral("sheets"),
                                  QStringLiteral("room"),            QStringLiteral("sharing"),
                                  QStringLiteral("files"),           QStringLiteral("shortcuts") };
    return ids;
}

}  // namespace

QString helpPagePath(const QString& file) {
    const QString exeDir = QCoreApplication::applicationDirPath();
    const QString lang = QSettings().value(QStringLiteral("general/language")).toString().left(2);
    QStringList roots;
    for (const char* rel : { "/../Resources/help", "/../share/brick-layout-designer/help", "/help" })
        roots << exeDir + QLatin1String(rel);
    roots << QStringLiteral(BLD_HELP_SOURCE_DIR);
    for (const QString& root : std::as_const(roots)) {
        for (const QString& l : { lang, QStringLiteral("en") }) {
            if (l.isEmpty()) continue;
            const QString path = QDir(root).filePath(l + QLatin1Char('/') + file);
            if (QFileInfo::exists(path)) return QFileInfo(path).absoluteFilePath();
        }
    }
    return {};
}

QUrl helpPageFor(const QString& webPath) {
    const QString prefix = QStringLiteral("/help#");
    if (!webPath.startsWith(prefix)) return {};
    const QString section = webPath.mid(prefix.size());
    if (!sections().contains(section)) return {};
    const QString page = helpPagePath(QLatin1String(kGettingStartedPage));
    if (page.isEmpty()) return {};
    QUrl url = QUrl::fromLocalFile(page);
    url.setFragment(section);
    return url;
}

void openHelpPage(const QUrl& url) {
    if (url.isValid() && opener()) opener()(url);
}

UrlOpener setHelpPageOpener(UrlOpener open) {
    UrlOpener previous = std::move(opener());
    opener() = std::move(open);
    return previous;
}

}  // namespace bld::ui::help
