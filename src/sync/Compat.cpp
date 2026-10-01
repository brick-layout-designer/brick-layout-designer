#include "Compat.h"

#include "core/Version.h"

#include <QCoreApplication>

namespace bld::sync {

namespace {
QString tr(const char* s) { return QCoreApplication::translate("Compat", s); }
}  // namespace

Standing desktopStanding(const QString& app, const QString& minimum, const QString& recommended) {
    if (!minimum.isEmpty() && core::compareVersions(app, minimum) == -1) return Standing::UpdateRequired;
    if (!recommended.isEmpty() && core::compareVersions(app, recommended) == -1) return Standing::UpdateSuggested;
    return Standing::Ok;
}

bool canReadDoc(std::optional<int> docSchema, int schemaVersion, int minReadable) {
    if (!docSchema) return true;
    return *docSchema >= minReadable && *docSchema <= schemaVersion;
}

QStringList desktopFeatures() {
    return { QStringLiteral("liveSync"),      QStringLiteral("signIn"),
             QStringLiteral("publish"),       QStringLiteral("clubs"),
             QStringLiteral("venues"),        QStringLiteral("partsManifest"),
             QStringLiteral("partFilesWithToken"), QStringLiteral("uploadParts"),
             QStringLiteral("preferences"),   QStringLiteral("ownerTags") };
}

QStringList assumedWhenUnlisted() { return { QStringLiteral("liveSync"), QStringLiteral("signIn") }; }

QStringList missingFeatures(const std::optional<QStringList>& serverFeatures) {
    const QStringList has = serverFeatures ? *serverFeatures : assumedWhenUnlisted();
    QStringList out;
    for (const QString& f : desktopFeatures())
        if (!has.contains(f)) out << f;
    return out;
}

QString featureLabel(const QString& id) {
    if (id == QLatin1String("liveSync")) return tr("Editing a layout together, live");
    if (id == QLatin1String("signIn")) return tr("Signing in from the desktop app");
    if (id == QLatin1String("publish")) return tr("Putting a layout on the server");
    if (id == QLatin1String("clubs")) return tr("Your clubs");
    if (id == QLatin1String("venues")) return tr("The venue library");
    if (id == QLatin1String("partsManifest")) return tr("Getting the server's parts");
    if (id == QLatin1String("partFilesWithToken")) return tr("Downloading the server's part pictures");
    if (id == QLatin1String("uploadParts")) return tr("Sending your own parts to the server");
    if (id == QLatin1String("preferences")) return tr("Your settings on every computer");
    if (id == QLatin1String("ownerTags")) return tr("Showing whose layouts and venues they are");
    return id;
}

}  // namespace bld::sync
