#include "Presence.h"

#include <QJsonArray>

#include <cstdint>
#include <cstdlib>

namespace bld::sync::presence {

QString colorFor(const QString& userId, const QString& layoutId) {
    static const char* kPalette[] = { "#f87171", "#fb923c", "#facc15", "#4ade80",
                                      "#22d3ee", "#60a5fa", "#a78bfa", "#f472b6" };
    const QString seed = userId + QLatin1Char(':') + layoutId;
    // (h * 31 + code unit) | 0 in JavaScript: 32-bit wrap-around.
    std::int32_t h = 0;
    for (const QChar c : seed)
        h = static_cast<std::int32_t>(static_cast<std::uint32_t>(h) * 31u + c.unicode());
    const std::int64_t a = std::llabs(static_cast<std::int64_t>(h));
    return QString::fromLatin1(kPalette[a % 8]);
}

QJsonObject state(const User& user, std::optional<QPointF> cursor, const QStringList& brickIds,
                  qint64 nowMs, const QString& editingModule) {
    QJsonArray ids;
    for (const auto& id : brickIds) ids.append(id);
    return {
        { QStringLiteral("user"), QJsonObject{ { QStringLiteral("id"), user.id },
                                               { QStringLiteral("displayName"), user.displayName },
                                               { QStringLiteral("avatarUrl"), QJsonValue::Null },
                                               { QStringLiteral("color"), user.color } } },
        { QStringLiteral("cursor"),
          cursor ? QJsonValue(QJsonObject{ { QStringLiteral("x"), cursor->x() },
                                           { QStringLiteral("y"), cursor->y() },
                                           { QStringLiteral("layerId"), QJsonValue::Null } })
                 : QJsonValue(QJsonValue::Null) },
        { QStringLiteral("selection"), QJsonObject{ { QStringLiteral("brickIds"), ids } } },
        { QStringLiteral("tool"), QStringLiteral("select") },
        { QStringLiteral("editingModule"), editingModule.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(editingModule) },
        { QStringLiteral("lastActivityMs"), static_cast<double>(nowMs) },
    };
}

Peer peerFrom(const QJsonObject& s) {
    Peer p;
    const QJsonObject user = s.value(QLatin1String("user")).toObject();
    p.name = user.value(QLatin1String("displayName")).toString();
    p.color = user.value(QLatin1String("color")).toString(QStringLiteral("#888888"));
    const QJsonValue c = s.value(QLatin1String("cursor"));
    if (c.isObject()) {
        const QJsonObject o = c.toObject();
        const QJsonValue x = o.value(QLatin1String("x")), y = o.value(QLatin1String("y"));
        if (x.isDouble() && y.isDouble()) p.cursor = QPointF(x.toDouble(), y.toDouble());
    }
    for (const auto& v :
         s.value(QLatin1String("selection")).toObject().value(QLatin1String("brickIds")).toArray())
        if (v.isString()) p.brickIds << v.toString();
    p.editingModule = s.value(QLatin1String("editingModule")).toString();
    return p;
}

QString label(const QString& name) {
    constexpr int kMax = 20;
    return name.size() > kMax ? name.left(kMax - 1) + QStringLiteral("…") : name;
}

} // namespace bld::sync::presence
