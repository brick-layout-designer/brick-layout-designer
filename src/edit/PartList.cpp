#include "PartList.h"

#include "Budget.h"

#include "../core/Layer.h"
#include "../core/LayerBrick.h"
#include "../core/Map.h"
#include "../parts/ColorNames.h"
#include "../parts/PartsLibrary.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QHash>
#include <QLocale>

#include <algorithm>
#include <cmath>

namespace bld::edit {

namespace {

// lupdate reads the context from Q_DECLARE_TR_FUNCTIONS; call Text::tr().
struct Text { Q_DECLARE_TR_FUNCTIONS(bld::edit::PartList) };

QStringList columnTitles() {
    return { Text::tr("Part"), Text::tr("In Use"), Text::tr("Color"), Text::tr("Description"),
             Text::tr("Budgeted"), Text::tr("Missing"), Text::tr("Part Usage %") };
}

QString descriptionOf(const parts::PartMetadata& meta, const QString& language) {
    for (const auto& d : meta.descriptions)
        if (d.language == language) return d.text;
    for (const auto& d : meta.descriptions)
        if (d.language == QLatin1String("en")) return d.text;
    return meta.descriptions.isEmpty() ? QString() : meta.descriptions.front().text;
}

// One row's cells in column order (BlueBrick's ListViewItem texts).
QStringList cells(const PartListRow& r, bool isTotal, bool hasBudget) {
    QStringList out{ isTotal ? Text::tr("Total") : r.part, QString::number(r.count), r.colorName, r.description };
    if (!hasBudget) {
        out << Text::tr("N/A") << Text::tr("N/A") << Text::tr("N/A");
    } else if (!isTotal && r.usage < 0) {
        out << Text::tr("Unbudgeted") << QString::number(r.missing) << Text::tr("Unbudgeted");
    } else {
        out << QString::number(r.budget) << QString::number(r.missing) << percentageBar(r.usage);
    }
    return out;
}

// Budget.getUsagePercentage.
double usageOf(int count, int budget) {
    if (budget < 0) return -1.0;
    if (budget == 0) return (count + 1) * 100.0;
    return count * 100.0 / budget;
}

QString escapeHtml(const QString& s) { return s.toHtmlEscaped(); }

}  // namespace

QString percentageBar(double percent) {
    QString bar;
    int tenth = static_cast<int>(percent * 0.1);
    if (tenth > 10) tenth = 10;
    if (tenth < 0) tenth = 0;
    bar += QString(tenth, QChar(0x2588));
    if (tenth < 10) {
        const double remain = percent - tenth * 10;
        char16_t c = 0x258F;
        if (remain >= 8.75) c = 0x2588;
        else if (remain >= 7.5) c = 0x2589;
        else if (remain >= 6.25) c = 0x258A;
        else if (remain >= 5.0) c = 0x258B;
        else if (remain >= 3.75) c = 0x258C;
        else if (remain >= 2.5) c = 0x258D;
        else if (remain >= 1.25) c = 0x258E;
        bar += QChar(c);
        ++tenth;
    }
    bar += QString(10 - tenth, QChar(0x254C));
    // .NET "N0": rounded, with thousands separators.
    return bar + QLatin1Char(' ') + QLocale(QLocale::English).toString(std::round(percent), 'f', 0) + QLatin1Char('%');
}

std::vector<PartListGroup> buildPartList(const core::Map& map, const parts::PartsLibrary& lib,
                                         const PartListOptions& options) {
    struct Counts { QString name; QHash<QString, int> byPart; QStringList order; };
    std::vector<Counts> counts;
    if (!options.splitPerLayer) counts.push_back({});
    for (const auto& L : map.layers()) {
        if (!L || L->kind() != core::LayerKind::Brick) continue;
        if (!options.includeHiddenLayers && !L->visible) continue;
        if (options.splitPerLayer) counts.push_back({ L->name, {}, {} });
        Counts& c = counts.back();
        for (const auto& b : static_cast<const core::LayerBrick&>(*L).bricks) {
            const QString id = b.partNumber.toUpper();
            if (!c.byPart.contains(id)) c.order << id;
            ++c.byPart[id];
        }
    }

    std::vector<PartListGroup> out;
    for (Counts& c : counts) {
        if (options.splitPerLayer && c.order.isEmpty()) continue;
        std::sort(c.order.begin(), c.order.end());
        PartListGroup g;
        g.name = c.name;
        int budgetOfUsed = 0, budgetedCount = 0;
        for (const QString& id : std::as_const(c.order)) {
            PartListRow r;
            r.partNumber = id;
            const int dot = id.lastIndexOf(QLatin1Char('.'));
            r.part = dot > 0 ? id.left(dot) : id;
            const QString colour = dot > 0 ? id.mid(dot + 1) : QString();
            r.colorName = parts::colorName(colour, options.language);
            if (const auto meta = lib.metadata(id)) r.description = descriptionOf(*meta, options.language);
            r.count = c.byPart.value(id);
            if (options.budget) {
                r.budget = options.budget->limit(id, options.defaultBudgetIsInfinite);
                r.usage = usageOf(r.count, r.budget);
                r.missing = r.budget < 0 ? r.count : std::max(0, r.count - r.budget);
                if (options.budget->hasLimit(id)) {
                    budgetOfUsed += r.budget;
                    budgetedCount += r.count;
                }
            }
            g.total.count += r.count;
            g.total.missing += r.missing;
            g.rows.push_back(r);
        }
        if (options.budget) {
            // Budget.getTotalUsagePercentage / getUsagePercentageForLayer.
            g.total.budget = budgetOfUsed;
            g.total.usage = budgetOfUsed == 0 ? 0.0 : budgetedCount * 100.0 / budgetOfUsed;
        }
        out.push_back(std::move(g));
    }
    return out;
}

QString partListText(const core::Map& map, const std::vector<PartListGroup>& groups,
                     const QString& title, bool hasBudget) {
    const QStringList titles = columnTitles();
    std::vector<int> width;
    for (const QString& t : titles) width.push_back(t.size());
    const auto widen = [&](const QStringList& row) {
        for (int i = 0; i < row.size(); ++i) width[i] = std::max<int>(width[i], row[i].size());
    };
    for (const auto& g : groups) {
        for (const auto& r : g.rows) widen(cells(r, false, hasBudget));
        widen(cells(g.total, true, hasBudget));
    }
    int ruleLength = -1;
    for (int w : width) ruleLength += w + 3;
    const QString rule = QLatin1Char('+') + QString(ruleLength, QLatin1Char('-')) + QLatin1Char('+');
    const auto line = [&](const QStringList& row) {
        QString s = QStringLiteral("| ");
        for (int i = 0; i < row.size(); ++i)
            s += row[i] + QString(width[i] - row[i].size() + 1, QLatin1Char(' ')) + (i + 1 == row.size() ? QStringLiteral("|\n") : QStringLiteral("| "));
        return s;
    };

    const QString indent(20, QLatin1Char(' '));
    const QString frame = QStringLiteral("+=") + QString(title.size(), QLatin1Char('=')) + QStringLiteral("=+");
    QString out = indent + frame + QLatin1Char('\n')
                + indent + QStringLiteral("| ") + title + QStringLiteral(" |\n")
                + indent + frame + QStringLiteral("\n\n");
    out += Text::tr("Author:") + QLatin1Char(' ') + map.author + QLatin1Char('\n');
    out += Text::tr("LUG:") + QLatin1Char(' ') + map.lug + QLatin1Char('\n');
    out += Text::tr("Event:") + QLatin1Char(' ') + map.event + QLatin1Char('\n');
    out += Text::tr("Date:") + QLatin1Char(' ') + QLocale(QLocale::English, QLocale::UnitedStates).toString(map.date, QLocale::LongFormat) + QLatin1Char('\n');
    out += Text::tr("Comment:") + QLatin1Char('\n') + map.comment + QStringLiteral("\n\n\n");
    for (const auto& g : groups) {
        if (!g.name.isEmpty()) out += QStringLiteral("| ") + g.name + QLatin1Char('\n');
        out += rule + QLatin1Char('\n') + line(titles) + rule + QLatin1Char('\n');
        for (const auto& r : g.rows) out += line(cells(r, false, hasBudget));
        out += rule + QLatin1Char('\n') + line(cells(g.total, true, hasBudget));
        out += rule + QLatin1Char('\n');
        if (!g.name.isEmpty()) out += QLatin1Char('\n');
    }
    return out;
}

QString partListCsv(const std::vector<PartListGroup>& groups, bool hasBudget) {
    const auto row = [&](const PartListRow& r, bool isTotal) {
        QStringList c = cells(r, isTotal, hasBudget);
        c[3].replace(QLatin1Char(','), QLatin1Char(' '));
        c[0].replace(QLatin1Char(','), QLatin1Char(' '));
        if (hasBudget && (isTotal || r.usage >= 0))
            c[6] = c[6].mid(11).chopped(1);  // just the number, as BlueBrick
        return c.join(QLatin1Char(',')) + QLatin1Char('\n');
    };
    QString out;
    for (const auto& g : groups) {
        if (!g.name.isEmpty()) out += g.name + QLatin1Char('\n');
        out += columnTitles().join(QLatin1Char(',')) + QLatin1Char('\n');
        for (const auto& r : g.rows) out += row(r, false);
        out += row(g.total, true);
        if (!g.name.isEmpty()) out += QLatin1Char('\n');
    }
    return out;
}

QString partListHtml(const core::Map& map, const std::vector<PartListGroup>& groups,
                     const QString& title, bool hasBudget,
                     const std::function<QImage(const QString&)>& picture) {
    static const char* headerClass[] = { "partIdHeader", "partCountHeader", "colorHeader", "descriptionHeader",
                                         "budgetHeader", "missingHeader", "partUsageHeader" };
    static const char* cellClass[] = { "partId", "partCount", "color", "description", "budget", "missing", "partUsage" };
    const QStringList titles = columnTitles();
    QString out;
    out += QStringLiteral("<html>\n<head>\n\t<meta charset=\"utf-8\">\n\t<title>%1</title>\n").arg(escapeHtml(title));
    out += QStringLiteral(
        "\t<style type=\"text/css\" >\n"
        "\th2.title { text-align: center; font-weight: bold; font-variant: small-caps; margin: 2.5%; background-color: #bdffc0; border: 2px solid black; padding: 10px;}\n"
        "\ttd.info { text-align: right; vertical-align: top; font-weight: bold; }\n"
        "\ttr.groupName { background-color: #90d7ff; font-weight: bold; }\n"
        "\ttr.header { background-color: #bfe8ff; font-style: italic; }\n"
        "\ttd.partIdHeader { text-align: center; width: 20% }\n"
        "\ttd.partCountHeader { text-align: center; width: 5% }\n"
        "\ttd.budgetHeader { text-align: center; width: 5% }\n"
        "\ttd.missingHeader { text-align: center; width: 5% }\n"
        "\ttd.partUsageHeader { text-align: center; width: 10% }\n"
        "\ttd.colorHeader { text-align: center; width: 10% }\n"
        "\ttd.descriptionHeader { text-align: center; width: 46% }\n"
        "\ttd.partId { text-align: center; }\n"
        "\ttd.partId img { max-width: 100%; max-height: 8em; }\n"
        "\ttd.partCount { text-align: center; }\n"
        "\ttd.budget { text-align: center; }\n"
        "\ttd.missing { text-align: center; }\n"
        "\ttd.partUsage { }\n"
        "\ttd.color { text-align: center; }\n"
        "\ttd.description { }\n"
        "\ttr.total { background-color: #feffea; }\n"
        "\t</style>\n");
    out += QStringLiteral("</head>\n<body>\n<h2 class=\"title\">%1</h2>\n").arg(escapeHtml(title));
    out += QStringLiteral("<table border=\"0\" style=\"margin-left: 3%\">\n");
    const auto info = [&](const QString& label, const QString& value) {
        out += QStringLiteral("\t<tr><td class=\"info\">%1</td><td>%2</td></tr>\n").arg(escapeHtml(label), value);
    };
    info(Text::tr("Author:"), escapeHtml(map.author));
    info(Text::tr("LUG:"), escapeHtml(map.lug));
    info(Text::tr("Event:"), escapeHtml(map.event));
    info(Text::tr("Date:"), escapeHtml(QLocale(QLocale::English, QLocale::UnitedStates).toString(map.date, QLocale::LongFormat)));
    info(Text::tr("Comment:"), escapeHtml(map.comment).replace(QLatin1Char('\n'), QStringLiteral("<br/>")));
    out += QStringLiteral("</table>\n<br/>\n<br/>\n\n");

    const auto headerRow = [&] {
        out += QStringLiteral("<tr class=\"header\">\n");
        for (int i = 0; i < titles.size(); ++i)
            out += QStringLiteral("\t<td class=\"%1\">%2</td>\n").arg(QLatin1String(headerClass[i]), escapeHtml(titles[i]));
        out += QStringLiteral("</tr>\n");
    };
    const auto row = [&](const PartListRow& r, bool isTotal) {
        out += isTotal ? QStringLiteral("<tr class=\"total\">\n") : QStringLiteral("<tr>\n");
        const QStringList c = cells(r, isTotal, hasBudget);
        for (int i = 0; i < c.size(); ++i) {
            QString text = escapeHtml(c[i]);
            if (i == 0 && !isTotal && picture) {
                const QImage img = picture(r.partNumber);
                if (!img.isNull()) {
                    QByteArray png;
                    QBuffer buf(&png);
                    buf.open(QIODevice::WriteOnly);
                    img.save(&buf, "PNG");
                    text = QStringLiteral("<img src=\"data:image/png;base64,%1\"><br/>").arg(QString::fromLatin1(png.toBase64())) + text;
                }
            }
            out += QStringLiteral("\t<td class=\"%1\">%2</td>\n").arg(QLatin1String(cellClass[i]), text);
        }
        out += QStringLiteral("</tr>\n");
    };
    for (const auto& g : groups) {
        out += QStringLiteral("<table border=\"1\" width=\"95%\" cellpadding=\"10\" style=\"margin: auto\">\n");
        if (!g.name.isEmpty())
            out += QStringLiteral("<tr class=\"groupName\"><td colspan=\"%1\"><b>%2</b></td></tr>\n").arg(titles.size()).arg(escapeHtml(g.name));
        headerRow();
        for (const auto& r : g.rows) row(r, false);
        row(g.total, true);
        out += QStringLiteral("</table>\n");
        if (!g.name.isEmpty()) out += QStringLiteral("<br/>\n");
    }
    out += QStringLiteral("</body>\n</html>\n");
    return out;
}

}  // namespace bld::edit
