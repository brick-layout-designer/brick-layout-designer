#include "AppPrefs.h"

#include "Tokens.h"

#include <QJsonArray>
#include <QRegularExpression>
#include <QSettings>

namespace bld::ui::theme {

QString themeChoiceId(ThemeChoice t) {
    switch (t) {
    case ThemeChoice::Light: return QStringLiteral("light");
    case ThemeChoice::Dark: return QStringLiteral("dark");
    case ThemeChoice::System: break;
    }
    return QStringLiteral("system");
}

ThemeChoice themeChoiceFromId(const QString& id, ThemeChoice fallback) {
    if (id == QLatin1String("light")) return ThemeChoice::Light;
    if (id == QLatin1String("dark")) return ThemeChoice::Dark;
    if (id == QLatin1String("system")) return ThemeChoice::System;
    return fallback;
}

namespace {

bool knownAccent(const QString& id) {
    for (const Accent& a : accents())
        if (a.id == id) return true;
    return false;
}

// The server's tour id rule, so a list it would refuse is never sent.
QStringList cleanTours(const QStringList& in) {
    static const QRegularExpression ok(QStringLiteral("^[A-Za-z0-9._-]{1,64}$"));
    QStringList out;
    for (const QString& t : in)
        if (ok.match(t).hasMatch() && !out.contains(t) && out.size() < 200) out << t;
    return out;
}

}  // namespace

QJsonObject AppPrefs::toJson() const {
    return QJsonObject{
        { QStringLiteral("theme"), themeChoiceId(theme) },
        { QStringLiteral("accent"), accent },
        { QStringLiteral("largeText"), largeText },
        { QStringLiteral("expertMode"), expertMode },
        { QStringLiteral("helpIcons"), helpIcons },
        { QStringLiteral("toursSeen"), QJsonArray::fromStringList(toursSeen) },
    };
}

AppPrefs AppPrefs::fromJson(const QJsonObject& json, const AppPrefs& base) {
    AppPrefs p = base;
    const QJsonValue theme = json.value(QLatin1String("theme"));
    if (theme.isString()) p.theme = themeChoiceFromId(theme.toString(), p.theme);
    const QJsonValue acc = json.value(QLatin1String("accent"));
    if (acc.isString() && knownAccent(acc.toString())) p.accent = acc.toString();
    const auto flag = [&json](const char* key, bool& out) {
        const QJsonValue v = json.value(QLatin1String(key));
        if (v.isBool()) out = v.toBool();
    };
    flag("largeText", p.largeText);
    flag("expertMode", p.expertMode);
    flag("helpIcons", p.helpIcons);
    const QJsonValue tours = json.value(QLatin1String("toursSeen"));
    if (tours.isArray()) {
        QStringList ids;
        for (const auto& v : tours.toArray())
            if (v.isString()) ids << v.toString();
        p.toursSeen = cleanTours(ids);
    }
    return p;
}

bool AppPrefs::sameSettings(const AppPrefs& o) const {
    return theme == o.theme && accent == o.accent && largeText == o.largeText && expertMode == o.expertMode
           && helpIcons == o.helpIcons && toursSeen == o.toursSeen;
}

PrefsStore::PrefsStore(const QString& group, QObject* parent) : QObject(parent), group_(group) {
    QSettings s;
    s.beginGroup(group_);
    prefs_.theme = themeChoiceFromId(s.value(QStringLiteral("theme")).toString());
    const QString acc = s.value(QStringLiteral("accent")).toString();
    if (knownAccent(acc)) prefs_.accent = acc;
    prefs_.largeText = s.value(QStringLiteral("largeText"), false).toBool();
    prefs_.expertMode = s.value(QStringLiteral("expertMode"), false).toBool();
    prefs_.helpIcons = s.value(QStringLiteral("helpIcons"), true).toBool();
    prefs_.toursSeen = cleanTours(s.value(QStringLiteral("toursSeen")).toStringList());
    prefs_.updatedAt = QDateTime::fromString(s.value(QStringLiteral("updatedAt")).toString(), Qt::ISODateWithMs);
}

PrefsStore& PrefsStore::instance() {
    static PrefsStore* store = new PrefsStore();  // lives as long as the app
    return *store;
}

void PrefsStore::update(const AppPrefs& p) {
    prefs_ = AppPrefs::fromJson(p.toJson());  // drops anything invalid
    prefs_.updatedAt = QDateTime::currentDateTimeUtc();
    save();
    emit changed(true);
}

void PrefsStore::adopt(const AppPrefs& p) {
    const QDateTime at = p.updatedAt;
    prefs_ = AppPrefs::fromJson(p.toJson());
    prefs_.updatedAt = at;
    save();
    emit changed(false);
}

void PrefsStore::confirm(const QDateTime& at) {
    prefs_.updatedAt = at;
    save();
}

void PrefsStore::save() {
    QSettings s;
    s.beginGroup(group_);
    s.setValue(QStringLiteral("theme"), themeChoiceId(prefs_.theme));
    s.setValue(QStringLiteral("accent"), prefs_.accent);
    s.setValue(QStringLiteral("largeText"), prefs_.largeText);
    s.setValue(QStringLiteral("expertMode"), prefs_.expertMode);
    s.setValue(QStringLiteral("helpIcons"), prefs_.helpIcons);
    s.setValue(QStringLiteral("toursSeen"), prefs_.toursSeen);
    s.setValue(QStringLiteral("updatedAt"), prefs_.updatedAt.toUTC().toString(Qt::ISODateWithMs));
}

}  // namespace bld::ui::theme

namespace bld::ui::theme {
AppPrefs AppPrefs::fromJson(const QJsonObject& json) { return fromJson(json, AppPrefs()); }
}  // namespace bld::ui::theme
