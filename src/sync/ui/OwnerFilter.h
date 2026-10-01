#pragma once

// Yours and your clubs' things together (the web's home page does the
// same): every server list marks who owns each item, "Me" or the club, and
// a Show filter narrows it to All, Mine or one club. The filter is kept
// between runs, and Publish starts at the club it shows.

#include <QCoreApplication>
#include <QString>

namespace bld::sync {

// The Show filter's values: everything, only yours, or a club's key.
inline const QString kShowAll = QStringLiteral("all");
inline const QString kShowMine = QStringLiteral("me");

struct ItemOwner {
    QString key;    // kShowMine, or the club's slug (its id or name from older servers)
    QString label;  // "Me", or the club's name
};

inline ItemOwner itemOwner(const QString& orgSlug, const QString& orgId, const QString& orgName) {
    if (orgSlug.isEmpty() && orgId.isEmpty() && orgName.isEmpty())
        return { kShowMine, QCoreApplication::translate("bld::sync::ConnectDialog", "Me") };
    const QString key = !orgSlug.isEmpty() ? orgSlug : !orgId.isEmpty() ? orgId : orgName;
    return { key, orgName.isEmpty() ? QCoreApplication::translate("bld::sync::ConnectDialog", "Club") : orgName };
}

inline bool ownerMatches(const QString& filter, const QString& ownerKey) {
    return filter.isEmpty() || filter == kShowAll || filter == ownerKey;
}

} // namespace bld::sync
