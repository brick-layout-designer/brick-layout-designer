#include "Version.h"

#include <QRegularExpression>
#include <QStringList>
#include <QVector>

namespace bld::core {

namespace {

struct Parsed {
    QVector<qlonglong> nums;
    QString pre;
};

std::optional<Parsed> parse(const QString& v) {
    static const QRegularExpression re(
        QStringLiteral("^[vV]?(\\d+(?:\\.\\d+)*)(?:-([0-9A-Za-z.-]+))?(?:\\+.*)?$"));
    const auto m = re.match(v.trimmed());
    if (!m.hasMatch()) return std::nullopt;
    Parsed p;
    for (const QString& n : m.captured(1).split(QLatin1Char('.'))) p.nums << n.toLongLong();
    p.pre = m.captured(2);
    return p;
}

}  // namespace

std::optional<int> compareVersions(const QString& a, const QString& b) {
    const auto pa = parse(a), pb = parse(b);
    if (!pa || !pb) return std::nullopt;
    const qsizetype n = std::max(pa->nums.size(), pb->nums.size());
    for (qsizetype i = 0; i < n; ++i) {
        const qlonglong x = i < pa->nums.size() ? pa->nums[i] : 0;
        const qlonglong y = i < pb->nums.size() ? pb->nums[i] : 0;
        if (x != y) return x > y ? 1 : -1;
    }
    if (pa->pre.isEmpty() != pb->pre.isEmpty()) return pa->pre.isEmpty() ? 1 : -1;
    return 0;
}

QString desktopDownloadUrl() {
    return QStringLiteral("https://github.com/brick-layout-designer/brick-layout-designer/releases/latest");
}

}  // namespace bld::core
