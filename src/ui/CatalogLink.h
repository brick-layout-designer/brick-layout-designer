#pragma once

// "Browse the catalog on the web…": the server's public catalog page, on the
// Modules or the Parts tab.

#include <QUrl>
#include <QUrlQuery>

namespace bld::ui {

inline QUrl catalogWebUrl(const QUrl& server, bool parts) {
    QUrl u = server;
    QString path = u.path();
    while (path.endsWith(QLatin1Char('/'))) path.chop(1);
    u.setPath(path + QStringLiteral("/catalog"));
    QUrlQuery q;
    if (parts) q.addQueryItem(QStringLiteral("kind"), QStringLiteral("part"));
    u.setQuery(q);
    u.setFragment({});
    return u;
}

}  // namespace bld::ui
