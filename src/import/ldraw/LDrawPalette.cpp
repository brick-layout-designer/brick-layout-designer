#include "LDrawPalette.h"

#include "LDrawColors.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

namespace bld::import {

namespace {

// Parse a "#RRGGBB" or "#AARRGGBB" hex literal. Returns invalid color
// on malformed input so the caller can skip the line.
QColor parseHex(QString s) {
    s = s.trimmed();
    if (!s.startsWith(QChar('#'))) return {};
    bool ok = false;
    if (s.size() == 7) {
        const uint v = s.mid(1).toUInt(&ok, 16);
        if (!ok) return {};
        return QColor::fromRgb(v);
    }
    if (s.size() == 9) {
        const uint v = s.mid(1).toUInt(&ok, 16);
        if (!ok) return {};
        // #AARRGGBB layout.
        return QColor::fromRgba(v);
    }
    return {};
}

}  // namespace

bool LDrawPalette::loadFromLDConfig(const QString& ldconfigPath) {
    QFile f(ldconfigPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    QTextStream ts(&f);
    QHash<int, Entry> next;

    // Tokenised lookup so we don't choke on `LEGEND` lines or trailing
    // optional flags. LDConfig.ldr is line-oriented and whitespace-
    // separated; comments start with `0 //`. We only act on lines that
    // start `0 !COLOUR`.
    while (!ts.atEnd()) {
        const QString line = ts.readLine();
        const QStringList toks = line.split(QRegularExpression(QStringLiteral("\\s+")),
                                              Qt::SkipEmptyParts);
        if (toks.size() < 2) continue;
        if (toks[0] != QStringLiteral("0")) continue;
        if (toks[1].compare(QStringLiteral("!COLOUR"), Qt::CaseInsensitive) != 0) continue;

        // Walk the rest of the line picking out CODE / VALUE / ALPHA
        // by name. Order isn't guaranteed across LDConfig revisions,
        // and we need to skip the color name (which can contain
        // spaces in older configs but is single-token in LDraw 2014+).
        int code = -1;
        QColor rgb;
        int alpha = 255;
        bool transparent = false;
        for (int i = 2; i + 1 < toks.size(); ++i) {
            const QString& key = toks[i];
            const QString& val = toks[i + 1];
            if (key.compare(QStringLiteral("CODE"), Qt::CaseInsensitive) == 0) {
                bool ok = false;
                const int c = val.toInt(&ok);
                if (ok) code = c;
            } else if (key.compare(QStringLiteral("VALUE"), Qt::CaseInsensitive) == 0) {
                rgb = parseHex(val);
            } else if (key.compare(QStringLiteral("ALPHA"), Qt::CaseInsensitive) == 0) {
                bool ok = false;
                const int a = val.toInt(&ok);
                if (ok) {
                    alpha = std::clamp(a, 0, 255);
                    transparent = alpha < 255;
                }
            }
        }
        if (code < 0 || !rgb.isValid()) continue;
        rgb.setAlpha(alpha);
        next.insert(code, Entry{ rgb, transparent });
    }
    if (next.isEmpty()) return false;
    entries_ = std::move(next);
    return true;
}

QColor LDrawPalette::color(int code) const {
    if (auto it = entries_.constFind(code); it != entries_.constEnd()) {
        return it.value().color;
    }
    // Fall back to the bundled palette — covers the universally-
    // authored 0..512 range for users who pointed us at a stripped
    // LDraw install.
    return ldrawColor(code);
}

bool LDrawPalette::isTransparent(int code) const {
    if (auto it = entries_.constFind(code); it != entries_.constEnd()) {
        return it.value().transparent;
    }
    return ldrawColorIsTransparent(code);
}

}  // namespace bld::import
