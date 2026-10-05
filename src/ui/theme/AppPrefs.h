#pragma once

// The app's look and help settings (Edit > Settings...): the same keys the
// web keeps on the account (GET/PUT /api/me/preferences). Stored in
// QSettings; while connected to a server they sync with the account there
// (PrefsSync), and the newest `updatedAt` wins.

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

namespace bld::ui::theme {

enum class ThemeChoice { Light, Dark, System };

QString themeChoiceId(ThemeChoice t);                 // light / dark / system
ThemeChoice themeChoiceFromId(const QString& id, ThemeChoice fallback = ThemeChoice::System);

// The Parts list's picture size, in pixels (the web's slider has the same range).
constexpr int kPartsIconMin = 32;
constexpr int kPartsIconMax = 160;
constexpr int kPartsIconDefault = 96;
int clampPartsIconSize(int px);

struct AppPrefs {
    ThemeChoice theme = ThemeChoice::System;
    QString accent = QStringLiteral("brick");
    bool largeText = false;
    bool expertMode = false;
    bool helpIcons = true;
    QStringList toursSeen;
    int partsIconSize = kPartsIconDefault;
    // When these were last changed (here or on the server); invalid when
    // they never were.
    QDateTime updatedAt;

    // The server's `prefs` object (no updatedAt).
    QJsonObject toJson() const;
    // `json`'s valid keys laid over `base`; unknown keys and wrong types
    // are skipped, as the server refuses them.
    static AppPrefs fromJson(const QJsonObject& json, const AppPrefs& base);
    static AppPrefs fromJson(const QJsonObject& json);

    // The settings themselves, ignoring updatedAt.
    bool sameSettings(const AppPrefs& o) const;
};

// The app's current settings: loads them from QSettings (group `group`),
// saves every change back, and says when they change.
class PrefsStore : public QObject {
    Q_OBJECT
public:
    explicit PrefsStore(const QString& group = QStringLiteral("appearance"), QObject* parent = nullptr);

    // The app's store (group "appearance").
    static PrefsStore& instance();

    const AppPrefs& prefs() const { return prefs_; }

    // A change made in this app: saved with updatedAt = now, then
    // changed(true) (PrefsSync pushes those).
    void update(const AppPrefs& p);
    // Settings taken from a server, kept with the server's updatedAt:
    // changed(false).
    void adopt(const AppPrefs& p);
    // The server confirmed these settings at `at`: keep its time so the
    // next pull doesn't bounce them back. No signal.
    void confirm(const QDateTime& at);

signals:
    void changed(bool local);

private:
    void save();
    QString group_;
    AppPrefs prefs_;
};

}  // namespace bld::ui::theme
