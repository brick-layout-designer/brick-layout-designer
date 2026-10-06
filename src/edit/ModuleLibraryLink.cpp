#include "ModuleLibraryLink.h"
#include "ModuleSheets.h"

#include "../core/Groups.h"
#include "../core/Ids.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../core/Module.h"
#include "../parts/BrickPlacement.h"

#include <QCoreApplication>
#include <QHash>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
#include <map>

namespace bld::edit {

namespace {

double norm360(double d) {
    const double r = std::fmod(d, 360.0);
    return r < 0 ? r + 360.0 : r;
}
double bucket(double v, double step) { return std::round(v / step) * step; }
double turnGap(double a, double b) {
    const double d = std::abs(a - b);
    return std::min(d, 360.0 - d);
}

// At most this many part pairs are compared per part number (big modules stay quick).
constexpr long long kMaxPairs = 40000;

struct Item {
    QString key;
    QPointF p;
    double o = 0.0;
};

std::vector<Item> items(const std::vector<const core::Brick*>& bricks, parts::PartsLibrary& parts) {
    std::vector<Item> out;
    out.reserve(bricks.size());
    for (const core::Brick* b : bricks)
        out.push_back({ b->partNumber.toLower(), parts::placement::imageCentre(*b, parts), norm360(b->orientation) });
    return out;
}

std::vector<const core::Brick*> refs(const LayerBatches& batches) {
    std::vector<const core::Brick*> out;
    for (const auto& batch : batches)
        for (const auto& b : batch.bricks) out.push_back(&b);
    return out;
}

std::vector<const core::Brick*> refs(const std::vector<core::Brick>& bricks) {
    std::vector<const core::Brick*> out;
    for (const auto& b : bricks) out.push_back(&b);
    return out;
}

// Each part number's pairs (library part, placed part), thinned evenly past kMaxPairs.
template <typename F>
void forPairs(const std::vector<Item>& lib, const std::vector<Item>& placed, F&& f) {
    std::map<QString, std::vector<const Item*>> libBy, hereBy;
    for (const auto& i : lib) libBy[i.key].push_back(&i);
    for (const auto& i : placed) hereBy[i.key].push_back(&i);
    // In library order, as the web walks them.
    QStringList order;
    for (const auto& i : lib)
        if (!order.contains(i.key)) order << i.key;
    for (const QString& key : order) {
        const auto h = hereBy.find(key);
        if (h == hereBy.end()) continue;
        const auto& a = libBy[key];
        const auto& b = h->second;
        const long long total = static_cast<long long>(a.size()) * static_cast<long long>(b.size());
        const long long stride = std::max<long long>(1, (total + kMaxPairs - 1) / kMaxPairs);
        long long n = 0;
        for (const Item* x : a)
            for (const Item* y : b)
                if (n++ % stride == 0) f(*x, *y);
    }
}

}  // namespace

std::optional<LibraryLink> libraryLink(const core::Module& m) {
    if (!m.libraryModuleId.isEmpty()) return LibraryLink{ m.libraryModuleId, std::max(0, m.libraryVersion) };
    static const QRegularExpression page(QStringLiteral("^https?://.+/modules/([^/?#]+)/?$"));
    const auto match = page.match(m.sourceFile);
    if (match.hasMatch()) return LibraryLink{ match.captured(1), 0 };
    return std::nullopt;
}

LayerBatches batchesOfModule(const core::Map& module) {
    LayerBatches out;
    for (const auto& L : module.layers()) {
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        const auto& layer = static_cast<const core::LayerBrick&>(*L);
        ImportBbmAsModuleCommand::LayerBatch batch;
        batch.layerName = L->name.isEmpty() ? QStringLiteral("Module") : L->name;
        for (const auto& b : layer.bricks) {
            core::Brick copy = b;
            copy.guid.clear();
            batch.bricks.push_back(std::move(copy));
        }
        // Its sets stay sets, under new ids.
        batch.groups = core::cloneGroups(layer, batch.bricks, [] { return core::newBbmId(); });
        if (!batch.bricks.empty()) out.push_back(std::move(batch));
    }
    return out;
}

Placement alignToPlaced(const LayerBatches& version, const std::vector<core::Brick>& placed, parts::PartsLibrary& parts) {
    const auto lib = items(refs(version), parts);
    const auto here = items(refs(placed), parts);
    // The turn most pairs agree on (the smallest on a tie).
    std::map<long long, int> turns;
    forPairs(lib, here, [&](const Item& a, const Item& b) {
        const double d = std::fmod(bucket(norm360(b.o - a.o), 0.1), 360.0);
        ++turns[std::llround(d * 10.0)];
    });
    long long bestTurn = -1;
    int bestTurnN = 0;
    for (const auto& [k, n] : turns)
        if (n > bestTurnN) bestTurn = k, bestTurnN = n;
    if (bestTurn >= 0) {
        const double deg = static_cast<double>(bestTurn) / 10.0;
        const double r = deg * M_PI / 180.0;
        const double c = std::cos(r), s = std::sin(r);
        // The shift most pairs agree on, in 0.05-stud steps; then exactly:
        // the mean of the pairs in that step.
        struct Shift { int n = 0; QPointF sum; };
        std::map<std::pair<long long, long long>, Shift> shifts;
        forPairs(lib, here, [&](const Item& a, const Item& b) {
            if (turnGap(norm360(b.o - a.o), deg) > 0.1) return;
            const QPointF d(b.p.x() - (a.p.x() * c - a.p.y() * s), b.p.y() - (a.p.x() * s + a.p.y() * c));
            Shift& e = shifts[{ std::llround(bucket(d.x(), 0.05) * 100.0), std::llround(bucket(d.y(), 0.05) * 100.0) }];
            ++e.n;
            e.sum += d;
        });
        const Shift* best = nullptr;
        for (const auto& [k, e] : shifts)
            if (!best || e.n > best->n) best = &e;
        if (best) return { deg, best->sum / best->n, best->n };
    }
    // Nothing alike: the Module library version's middle on the module's middle.
    const auto mid = [](const std::vector<Item>& xs) {
        QPointF m;
        for (const auto& i : xs) m += i.p;
        return xs.empty() ? m : m / static_cast<double>(xs.size());
    };
    return { 0.0, mid(here) - mid(lib), 0 };
}

LayerBatches placeVersion(LayerBatches version, const Placement& placement, parts::PartsLibrary& parts) {
    const double r = placement.degrees * M_PI / 180.0;
    const double c = std::cos(r), s = std::sin(r);
    for (auto& batch : version) {
        for (auto& b : batch.bricks) {
            const QPointF p = parts::placement::imageCentre(b, parts);
            const QPointF to(placement.to.x() + p.x() * c - p.y() * s, placement.to.y() + p.x() * s + p.y() * c);
            b.orientation = static_cast<float>(norm360(b.orientation + placement.degrees));
            parts::placement::placeByImageCentre(b, to, parts);
        }
    }
    return version;
}

bool matchesVersion(const LayerBatches& version, const std::vector<core::Brick>& placed, parts::PartsLibrary& parts) {
    const LayerBatches at = placeVersion(version, alignToPlaced(version, placed, parts), parts);
    const auto lib = items(refs(at), parts);
    const auto here = items(refs(placed), parts);
    if (lib.size() != here.size()) return false;
    std::vector<bool> used(lib.size(), false);
    for (const auto& h : here) {
        bool found = false;
        for (std::size_t k = 0; k < lib.size(); ++k) {
            const auto& l = lib[k];
            if (used[k] || l.key != h.key) continue;
            if (std::hypot(l.p.x() - h.p.x(), l.p.y() - h.p.y()) >= 0.1 || turnGap(l.o, h.o) >= 0.5) continue;
            used[k] = true;
            found = true;
            break;
        }
        if (!found) return false;
    }
    return true;
}

std::vector<core::Brick> placedParts(const core::Map& map, const QSet<QString>& members) {
    std::vector<core::Brick> out;
    for (const auto& L : map.layers()) {
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks)
            if (members.contains(b.guid)) out.push_back(b);
    }
    return out;
}

std::function<QString(const QString&)> sheetForUpdate(const core::Map& map, const QSet<QString>& members) {
    auto uses = moduleSheetsUsed(map, members);
    std::stable_sort(uses.begin(), uses.end(), [](const ModuleSheetUse& a, const ModuleSheetUse& b) { return a.parts > b.parts; });
    int fallback = uses.empty() ? pickedPartsSheet(map) : uses.front().index;
    QHash<QString, QString> byName;
    for (const auto& L : map.layers())
        if (L && L->kind() == core::LayerKind::Brick && !byName.contains(sheetKey(L->name))) byName.insert(sheetKey(L->name), L->guid);
    const QString fallbackGuid = fallback >= 0 && fallback < static_cast<int>(map.layers().size()) ? map.layers()[fallback]->guid : QString();
    return [byName, fallbackGuid](const QString& name) { return byName.value(sheetKey(name), fallbackGuid); };
}

QString libraryNote(int version, int latest) {
    const auto tr = [](const char* s) { return QCoreApplication::translate("bld::edit::ModuleLibraryLink", s); };
    if (latest < 0) return tr("its Module library copy isn't on this server");
    const QString v = version > 0 ? QStringLiteral(" v%1").arg(version) : QString();
    if (latest > 0 && (version == 0 || latest > version)) return tr("in the Module library%1 · v%2 is newer").arg(v).arg(latest);
    return tr("in the Module library%1").arg(v);
}

}  // namespace bld::edit
