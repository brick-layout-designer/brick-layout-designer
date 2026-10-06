#include "Budget.h"

#include "../core/Groups.h"

#include "../core/Layer.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../parts/PartsLibrary.h"

#include <QFile>
#include <QSaveFile>
#include <QXmlStreamReader>

namespace bld::edit {

namespace {

// The current part number for an old one (BlueBrick's getActualPartNumber).
QString actualPartNumber(const QString& id, const parts::PartsLibrary* lib) {
    if (!lib) return id;
    const QString key = lib->canonicalKey(id);
    if (key.isEmpty() || key == id.toLower()) return id;
    const auto meta = lib->metadata(key);
    if (!meta) return id;
    return (meta->colorCode.isEmpty() ? meta->partNumber
                                      : meta->partNumber + QLatin1Char('.') + meta->colorCode).toUpper();
}

}  // namespace

std::optional<Budget> Budget::read(const QString& path, const parts::PartsLibrary* lib) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return std::nullopt;
    QXmlStreamReader r(&f);
    if (!r.readNextStartElement() || r.name() != QStringLiteral("Budget")) return std::nullopt;
    Budget budget;
    while (r.readNextStartElement()) {
        if (r.name() != QStringLiteral("PartList")) { r.skipCurrentElement(); continue; }
        while (r.readNextStartElement()) {
            if (r.name() != QStringLiteral("Part")) { r.skipCurrentElement(); continue; }
            const QString id = actualPartNumber(r.attributes().value(QStringLiteral("id")).toString(), lib);
            bool ok = false;
            const int value = r.readElementText().trimmed().toInt(&ok);
            if (!ok || id.isEmpty()) return std::nullopt;  // BlueBrick rejects the file too
            if (budget.hasLimit(id)) continue;             // (its dictionary throws on duplicates)
            budget.entries_.append({ id, value });
            budget.index_.insert(id.toUpper(), budget.entries_.size() - 1);
        }
    }
    if (r.hasError()) return std::nullopt;
    return budget;
}

bool Budget::write(const QString& path) const {
    // Byte for byte what BlueBrick's XmlSerializer writes.
    const QString nl = QStringLiteral("\r\n");
    QString out = QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>") + nl
                + QStringLiteral("<Budget>") + nl
                + QStringLiteral("  <Version>1</Version>") + nl;
    if (entries_.isEmpty()) {
        out += QStringLiteral("  <PartList />") + nl;
    } else {
        out += QStringLiteral("  <PartList>") + nl;
        for (const auto& [id, value] : entries_) {
            QString escaped = id;
            escaped.replace(QLatin1Char('&'), QStringLiteral("&amp;"))
                   .replace(QLatin1Char('<'), QStringLiteral("&lt;"))
                   .replace(QLatin1Char('>'), QStringLiteral("&gt;"))
                   .replace(QLatin1Char('"'), QStringLiteral("&quot;"));
            out += QStringLiteral("    <Part id=\"%1\">%2</Part>").arg(escaped).arg(value) + nl;
        }
        out += QStringLiteral("  </PartList>") + nl;
    }
    out += QStringLiteral("</Budget>");
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    const QByteArray bytes = out.toUtf8();
    return f.write(bytes) == bytes.size() && f.commit();
}

int Budget::limit(const QString& part, bool defaultInfinite) const {
    const auto it = index_.constFind(part.toUpper());
    if (it == index_.constEnd()) return defaultInfinite ? -1 : 0;
    return entries_[it.value()].second;
}

bool Budget::setLimit(const QString& part, int limit) {
    const auto it = index_.constFind(part.toUpper());
    if (limit < 0) {
        if (it == index_.constEnd()) return false;
        entries_.removeAt(it.value());
        reindex();
        return true;
    }
    if (it != index_.constEnd()) {
        if (entries_[it.value()].second == limit) return false;
        entries_[it.value()].second = limit;
        return true;
    }
    entries_.append({ part, limit });
    index_.insert(part.toUpper(), entries_.size() - 1);
    return true;
}

void Budget::mergeWith(const Budget& other) {
    for (const auto& [id, value] : other.entries_) {
        const auto it = index_.constFind(id.toUpper());
        if (it == index_.constEnd()) {
            entries_.append({ id, value });
            index_.insert(id.toUpper(), entries_.size() - 1);
        } else {
            // BlueBrick removes and re-adds the entry, moving it to the end.
            const int sum = entries_[it.value()].second + value;
            const QString kept = entries_[it.value()].first;
            entries_.removeAt(it.value());
            entries_.append({ kept, sum });
            reindex();
        }
    }
}

void Budget::reindex() {
    index_.clear();
    for (int i = 0; i < entries_.size(); ++i) index_.insert(entries_[i].first.toUpper(), i);
}

QHash<QString, int> countPartUsage(const core::Map& map, bool includeHiddenLayers) {
    QHash<QString, int> usage;
    for (const auto& L : map.layers()) {
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        if (!includeHiddenLayers && !L->visible) continue;
        // As BlueBrick's budget: a set counts once (LibraryBrickList).
        for (const QString& part : core::libraryItems(static_cast<const core::LayerBrick&>(*L))) ++usage[part.toUpper()];
    }
    return usage;
}

QVector<BudgetViolation> checkBudget(const core::Map& map, const Budget& budget) {
    QVector<BudgetViolation> out;
    if (budget.isEmpty()) return out;
    const auto usage = countPartUsage(map);
    for (const auto& [id, limit] : budget.entries()) {
        const int used = usage.value(id.toUpper(), 0);
        if (limit >= 0 && used > limit) out.append({ id, used, limit, used - limit });
    }
    return out;
}

bool canAddToBudget(const Budget& budget, const QHash<QString, int>& usage, const QString& part,
                    int quantity, bool defaultInfinite,
                    const std::function<QHash<QString, int>(const QString&)>& subparts) {
    const auto fits = [&](const QString& id, int n) {
        const int limit = budget.limit(id, defaultInfinite);
        return limit < 0 || usage.value(id.toUpper(), 0) + n <= limit;
    };
    if (!fits(part, quantity)) return false;
    if (subparts) {
        const auto leaves = subparts(part);
        for (auto it = leaves.constBegin(); it != leaves.constEnd(); ++it)
            if (!fits(it.key(), it.value())) return false;
    }
    return true;
}

QHash<QString, int> setLeafParts(const parts::PartsLibrary& lib, const QString& part) {
    QHash<QString, int> out;
    const auto meta = lib.metadata(part);
    if (!meta || meta->kind != parts::PartKind::Group) return out;
    for (const auto& sp : meta->subparts) {
        const auto sub = lib.metadata(sp.subKey);
        if (sub && sub->kind == parts::PartKind::Group) {
            const auto nested = setLeafParts(lib, sp.subKey);
            for (auto it = nested.constBegin(); it != nested.constEnd(); ++it) out[it.key()] += it.value();
        } else {
            ++out[sp.subKey.toUpper()];
        }
    }
    return out;
}

}  // namespace bld::edit
