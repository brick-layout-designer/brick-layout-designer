#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QWidget;

namespace bld::ui {

// Whether release `candidate` ("v1.3.0", "1.3.0-beta.1") is newer than
// `current`. Numeric parts compare as numbers; a pre-release is older than
// its release. Unparsable versions are never newer.
bool isNewerVersion(const QString& candidate, const QString& current);

// Help > Check for Updates, and the optional daily check at startup:
// asks GitHub for the latest release and, if it is newer than this build,
// shows its notes with a link to download it. Nothing is downloaded or
// installed automatically.
class UpdateCheck : public QObject {
    Q_OBJECT
public:
    explicit UpdateCheck(QWidget* parent);

    // Asked from the menu: also reports "up to date" and failures.
    void checkNow();
    // At startup: at most once a day, when enabled, silent unless a newer
    // release exists.
    void checkAtStartupIfDue();

    static bool checkAtStartupEnabled();
    static void setCheckAtStartupEnabled(bool on);

private:
    void check(bool interactive);

    QWidget* parent_;
    QNetworkAccessManager* network_;
};

}  // namespace bld::ui
