#pragma once

// Author credit for a server's layouts, modules, venues and parts (the
// server's routes/credits.ts; the web's owners.ts creditText): who made it
// ("by Sam · in ArkLUG"), what a copy was based on ("based on Yard by
// Sam"), and whether you may take it back from the club ("Take back to
// mine", its author) or give it back to its author (the club's admins and
// managers). The club keeps its own copy either way. Older servers send no
// credit: everything here is then empty and false.

#include <QCoreApplication>
#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace bld::sync {

struct Credit {
    QString by;           // "you", a name, "a former member" or "Builder #…"; empty: nobody recorded
    QString authorName;   // the author's name while their account exists
    QString club;         // the club that holds it; empty for a person's
    QString basedOnTitle; // a copy: what it was copied from
    QString basedOnBy;
    bool canTakeBack = false;
    bool canGiveBack = false;

    bool any() const { return !by.isEmpty() || !club.isEmpty() || !basedOnTitle.isEmpty(); }
};

inline Credit creditFromJson(const QJsonObject& item) {
    Credit c;
    const QJsonObject o = item.value(QLatin1String("credit")).toObject();
    if (o.isEmpty()) return c;
    c.by = o.value(QLatin1String("by")).toString();
    c.authorName = o.value(QLatin1String("authorName")).toString();
    c.club = o.value(QLatin1String("club")).toString();
    const QJsonObject b = o.value(QLatin1String("basedOn")).toObject();
    c.basedOnTitle = b.value(QLatin1String("title")).toString();
    c.basedOnBy = b.value(QLatin1String("by")).toString();
    c.canTakeBack = o.value(QLatin1String("canTakeBack")).toBool();
    c.canGiveBack = o.value(QLatin1String("canGiveBack")).toBool();
    return c;
}

// "by Sam · in ArkLUG · based on Yard by Sam" (the web shows the same words).
inline QString creditLine(const Credit& c) {
    const auto tr = [](const char* s) { return QCoreApplication::translate("bld::sync::Credit", s); };
    QStringList parts;
    if (!c.by.isEmpty()) parts << tr("by %1").arg(c.by);
    if (!c.club.isEmpty()) parts << tr("in %1").arg(c.club);
    if (!c.basedOnTitle.isEmpty())
        parts << (c.basedOnBy.isEmpty() ? tr("based on %1").arg(c.basedOnTitle)
                                        : tr("based on %1 by %2").arg(c.basedOnTitle, c.basedOnBy));
    return parts.join(QStringLiteral(" · "));
}

// What a club is told before something is saved or moved into it (the web's moveToClubWording).
struct MoveToClubWording {
    QString title;
    QString removes;
    QString keeps;
};

// `saving`: a new thing saved straight into the club ("Save it to ‹club›?").
inline MoveToClubWording moveToClubWording(const QString& club, bool saving = false) {
    const auto tr = [](const char* s) { return QCoreApplication::translate("bld::sync::Credit", s); };
    return { saving ? tr("Save it to %1?").arg(club) : tr("Move it to %1?").arg(club),
             tr("%1 will own this. Its admins and managers can change or delete it.").arg(club),
             tr("You stay credited as the author, and you can take it back while you’re a member.") };
}

} // namespace bld::sync
