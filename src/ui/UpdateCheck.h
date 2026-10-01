#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QWidget;

namespace bld::ui {

// Whether release `candidate` ("v1.3.0", "1.3.0-beta.1") is newer than
// `current`. Numeric parts compare as numbers; a pre-release is older than
// its release. Unparsable versions are never newer.
bool isNewerVersion(const QString& candidate, const QString& current);

// A release on GitHub, as the update notice shows it.
struct ReleaseInfo {
    QString version;  // "1.3.0" (no leading v)
    QString url;      // its page, to download from
    QString notes;    // what's new (Markdown), trimmed to a readable length
    bool important = false;  // the notes carry an [important] or [security] marker
};

// A GitHub "latest release" answer, read.
ReleaseInfo readRelease(const QJsonObject& release);
// Release notes mark an important (security) update with a line or tag
// "[important]" or "[security]", in any case.
bool isImportantRelease(const QString& notes);

// Help > Check for Updates, and the daily check at startup (on unless
// turned off in Settings): asks GitHub for the desktop's latest release.
// From the menu, the answer comes in a box as before; at startup a newer
// release raises updateAvailable() for a friendly notice instead, unless
// the user chose to skip that version. Nothing is downloaded or installed
// automatically. The update source is always the desktop's GitHub
// releases, never a collaborative server.
class UpdateCheck : public QObject {
    Q_OBJECT
public:
    explicit UpdateCheck(QWidget* parent);

    // Asked from the menu: also reports "up to date" and failures.
    void checkNow();
    // At startup: at most once a day, when enabled, silent unless a newer
    // release (not skipped) exists.
    void checkAtStartupIfDue();
    // Handle a startup answer (also what tests drive).
    void offer(const ReleaseInfo& release, const QString& current);

    static bool checkAtStartupEnabled();
    static void setCheckAtStartupEnabled(bool on);
    // "Skip this version": no startup notice for it again (newer ones still come).
    static QString skippedVersion();
    static void skipVersion(const QString& version);

signals:
    void updateAvailable(const bld::ui::ReleaseInfo& release);

private:
    void check(bool interactive);

    QWidget* parent_;
    QNetworkAccessManager* network_;
};

}  // namespace bld::ui
