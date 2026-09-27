#include "ColorNames.h"

#include <QFile>
#include <QHash>
#include <QXmlStreamReader>

namespace bld::parts {

namespace {

using Names = QHash<int, QHash<QString, QString>>;  // code -> language -> name

Names load() {
    Names names;
    QFile f(QStringLiteral(":/bld/ColorTable.xml"));
    if (!f.open(QIODevice::ReadOnly)) return names;
    QXmlStreamReader r(&f);
    while (!r.atEnd()) {
        if (r.readNext() != QXmlStreamReader::StartElement || r.name() != QStringLiteral("color")) continue;
        bool ok = false;
        const int id = r.attributes().value(QStringLiteral("id")).toInt(&ok);
        if (!ok) continue;
        auto& byLanguage = names[id];
        while (r.readNextStartElement()) byLanguage.insert(r.name().toString(), r.readElementText().trimmed());
    }
    return names;
}

}  // namespace

QString colorName(const QString& code, const QString& language) {
    static const Names names = load();
    bool ok = false;
    const int id = code.toInt(&ok);
    if (!ok) return {};
    const auto it = names.constFind(id);
    if (it == names.constEnd()) return {};
    const QString name = it->value(language);
    return name.isEmpty() ? it->value(QStringLiteral("en")) : name;
}

}  // namespace bld::parts
