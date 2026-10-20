#include "DocShape.h"

#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>

namespace bld::sync {

std::optional<double> zOf(const QJsonValue& item) {
    const QJsonValue z = item.toObject().value(QLatin1String("z"));
    if (!z.isDouble() || !std::isfinite(z.toDouble())) return std::nullopt;
    return z.toDouble();
}

QList<qsizetype> orderByZ(const QList<std::optional<double>>& zs) {
    double last = -std::numeric_limits<double>::infinity();
    QList<std::pair<double, qsizetype>> keys;
    keys.reserve(zs.size());
    for (qsizetype i = 0; i < zs.size(); ++i) {
        if (zs[i]) last = *zs[i];
        keys.append({ zs[i] ? *zs[i] : last, i });
    }
    std::stable_sort(keys.begin(), keys.end(), [](const auto& a, const auto& b) {
        return a.first < b.first || (a.first == b.first && a.second < b.second);
    });
    QList<qsizetype> out;
    out.reserve(keys.size());
    for (const auto& k : keys) out.append(k.second);
    return out;
}

bool hasZOrder(const QJsonArray& items) {
    for (const auto& v : items) if (zOf(v)) return true;
    return false;
}

QJsonArray inZOrder(const QJsonArray& items) {
    if (!hasZOrder(items)) return items;
    QList<std::optional<double>> zs;
    for (const auto& v : items) zs.append(zOf(v));
    QJsonArray out;
    for (const qsizetype i : orderByZ(zs)) out.append(items[i]);
    return out;
}

QList<std::pair<QString, double>> assignZ(const QList<ZEntry>& cur, const QStringList& want) {
    QHash<QString, qsizetype> pos;
    for (qsizetype i = 0; i < want.size(); ++i) pos.insert(want[i], i);
    QHash<QString, std::optional<double>> zById;
    for (const auto& e : cur) zById.insert(e.id, e.z);

    // Entries with a z, in the order now, by their place in `want`; keep the
    // longest increasing run (patience sort, as the web's).
    QStringList seqIds;
    QList<qsizetype> seq;
    for (const auto& e : cur)
        if (e.z && pos.contains(e.id)) {
            seqIds << e.id;
            seq << pos.value(e.id);
        }
    QList<qsizetype> tails;
    QList<qsizetype> prev(seq.size(), -1);
    for (qsizetype i = 0; i < seq.size(); ++i) {
        qsizetype lo = 0, hi = tails.size();
        while (lo < hi) {
            const qsizetype mid = (lo + hi) / 2;
            if (seq[tails[mid]] < seq[i]) lo = mid + 1;
            else hi = mid;
        }
        if (lo > 0) prev[i] = tails[lo - 1];
        if (lo == tails.size()) tails << i;
        else tails[lo] = i;
    }
    QSet<QString> keep;
    for (qsizetype k = tails.isEmpty() ? -1 : tails.last(); k >= 0; k = prev[k]) keep.insert(seqIds[k]);

    QList<std::pair<QString, double>> out;
    std::optional<double> below;
    qsizetype i = 0;
    while (i < want.size()) {
        if (keep.contains(want[i])) {
            below = zById.value(want[i]);
            ++i;
            continue;
        }
        qsizetype j = i;
        while (j < want.size() && !keep.contains(want[j])) ++j;
        const std::optional<double> above = j < want.size() ? zById.value(want[j]) : std::nullopt;
        const qsizetype n = j - i;
        for (qsizetype k = 0; k < n; ++k) {
            double z;
            if (below && above) z = *below + (*above - *below) * static_cast<double>(k + 1) / static_cast<double>(n + 1);
            else if (below) z = std::floor(*below) + static_cast<double>(k + 1);
            else if (above) z = std::ceil(*above) - static_cast<double>(n - k);
            else z = static_cast<double>(k);
            const auto now = zById.value(want[i + k]);
            if (!now || *now != z) out.append({ want[i + k], z });
        }
        i = j;
    }
    return out;
}

}  // namespace bld::sync
