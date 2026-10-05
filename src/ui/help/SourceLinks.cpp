#include "SourceLinks.h"

#include <QCoreApplication>
#include <QLabel>
#include <QPalette>

namespace bld::ui::help {

QString sourceLinksHtml() {
    return QCoreApplication::translate("bld::ui::help::SourceLinks",
                                       "Source on GitHub: <a href=\"%1\">desktop app</a> · <a href=\"%2\">web app</a>")
        .arg(QLatin1String(kDesktopSourceUrl), QLatin1String(kWebSourceUrl));
}

QLabel* makeSourceLinksLabel(QWidget* parent) {
    auto* label = new QLabel(sourceLinksHtml(), parent);
    label->setObjectName(QStringLiteral("sourceLinks"));
    label->setTextFormat(Qt::RichText);
    label->setOpenExternalLinks(true);
    label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    label->setForegroundRole(QPalette::PlaceholderText);
    QFont small = label->font();
    small.setPointSizeF(small.pointSizeF() * 0.9);
    label->setFont(small);
    return label;
}

}  // namespace bld::ui::help
