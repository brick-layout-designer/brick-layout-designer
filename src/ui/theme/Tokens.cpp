#include "Tokens.h"

namespace bld::ui::theme {

namespace {
QColor c(const char* hex) { return QColor(QLatin1String(hex)); }
}  // namespace

const Neutrals& neutrals(Mode mode) {
    static const Neutrals light{
        c("#F6F4EF"), c("#FFFFFF"), c("#E4E0D8"), c("#D6D1C7"), c("#1E2124"), c("#5B6168"),
        c("#F1EEE8"), c("#EDEAE3"), c("#E2DED5"), c("#9A948A"), c("#1F6B3A"), c("#E8F3EC"),
        c("#A8331F"), c("#1E2124"), c("#FFFFFF"), c("#C9CDD2"),
    };
    static const Neutrals dark{
        c("#16181B"), c("#1F2226"), c("#30343A"), c("#3A3F45"), c("#ECEDEF"), c("#A3A9B0"),
        c("#2A2E33"), c("#1A1C1F"), c("#25282C"), c("#6B7078"), c("#7FD39B"), c("#1D3326"),
        c("#F2A08F"), c("#F3F1EC"), c("#1E2124"), c("#4A5057"),
    };
    return mode == Mode::Dark ? dark : light;
}

// Contrast (WCAG 2.1), as the web's tokens.ts notes: onMain on main is at
// least 4.5:1 for every color. Sunny's #B8860B only reaches 3.25:1 with
// white, so sunny buttons use dark ink (4.97:1) instead.
const QList<Accent>& accents() {
    static const QList<Accent> all{
        { QStringLiteral("brick"), QStringLiteral("Brick red"), c("#C2412D"), c("#FFFFFF"), c("#FBE9E5"),
          c("#3A221E"), c("#A8331F"), c("#F2A08F") },
        { QStringLiteral("ocean"), QStringLiteral("Ocean blue"), c("#2459C4"), c("#FFFFFF"), c("#E6EEFB"),
          c("#1C2940"), c("#1D4AA6"), c("#93B4F0") },
        { QStringLiteral("forest"), QStringLiteral("Forest green"), c("#2E7D4F"), c("#FFFFFF"), c("#E4F2E9"),
          c("#1C3326"), c("#256942"), c("#8FD1A8") },
        { QStringLiteral("plum"), QStringLiteral("Plum"), c("#8A4FBF"), c("#FFFFFF"), c("#F1E8F9"),
          c("#2E2238"), c("#7340A3"), c("#C9A6EA") },
        { QStringLiteral("sunny"), QStringLiteral("Sunny yellow"), c("#B8860B"), c("#1E2124"), c("#FBF1D9"),
          c("#3A2F14"), c("#8A6508"), c("#E9C86A") },
        { QStringLiteral("teal"), QStringLiteral("Lagoon teal"), c("#0F7C80"), c("#FFFFFF"), c("#E0F2F2"),
          c("#16302F"), c("#0B6569"), c("#7FD0CF") },
        { QStringLiteral("orange"), QStringLiteral("Pumpkin orange"), c("#B9520B"), c("#FFFFFF"), c("#FCEBDD"),
          c("#3A2616"), c("#A0470A"), c("#F4B183") },
        { QStringLiteral("rose"), QStringLiteral("Rose pink"), c("#C2335F"), c("#FFFFFF"), c("#FBE6EC"),
          c("#3A1E27"), c("#A6284F"), c("#F2A3BA") },
        { QStringLiteral("indigo"), QStringLiteral("Indigo"), c("#4B4FC4"), c("#FFFFFF"), c("#EAEAFB"),
          c("#23243F"), c("#3E42A8"), c("#B0B3F2") },
        { QStringLiteral("slate"), QStringLiteral("Slate gray"), c("#4F5B6B"), c("#FFFFFF"), c("#EBEEF2"),
          c("#262B32"), c("#46505E"), c("#B6C0CC") },
    };
    return all;
}

const Accent& accent(const QString& id) {
    for (const Accent& a : accents())
        if (a.id == id) return a;
    return accents().first();
}

}  // namespace bld::ui::theme
