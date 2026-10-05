#pragma once

// File › Connect to Server… (sync phase P4): pick one of your servers (or
// type a new address; recent layouts are listed under the server they're
// on), a check that it speaks our protocol, device sign-in when there is no saved token
// (the code is shown and the browser opened), then a layout picker. The
// token goes to the TokenStore; the chosen layout is result().
//
// The same steps serve File › Download Venues from Server…: then the list
// is the server's venue library, several can be picked, and they are
// downloaded as .bld-venue files (venues()). And File › Publish to
// Server…: a title and where it's saved (you, or one of your clubs), and
// the layout is created on the server; result() is then the new layout.
//
// Purpose::SignIn (from File › Servers…) only signs in to the address set
// and hands back the token: result() then has no layout.

#include "ServerApi.h"

#include <QDialog>
#include <QUrl>

#include <functional>
#include <optional>
#include <utility>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTreeWidget;

namespace bld::sync {

class TokenStore;

struct ConnectResult {
    QUrl server;
    QString token;
    QString layoutId;
    QString title;
    bool readOnly = false; // view-only access
    ServerInfo info;       // what the server said about itself
};

struct DownloadedVenue {
    QString name;
    QByteArray file; // .bld-venue bytes
};

class ConnectDialog : public QDialog {
    Q_OBJECT
public:
    // `openUrl` opens the sign-in page (QDesktopServices in the app).
    enum class Purpose { OpenLayout, DownloadVenues, Publish, SignIn };

    ConnectDialog(ServerApi& api, TokenStore& tokens, std::function<void(const QUrl&)> openUrl,
                  QWidget* parent = nullptr, Purpose purpose = Purpose::OpenLayout);

    std::optional<ConnectResult> result() const { return result_; }
    QList<DownloadedVenue> venues() const { return venues_; }

    void setAddress(const QString& address);
    // Publish: what to publish (the .bbm and its sidecar JSON) and the title offered.
    void setPublishContent(const QByteArray& bbm, const QByteArray& sidecarJson, const QString& title);
    // The same as clicking Publish.
    void publishNow();
    // The same as clicking Connect.
    void connectToServer();
    // Forget the saved token for this server and sign in again.
    void signOut();
    // Connect to the server in the list and open its recent layout `layoutId`
    // once the list has it (the same as double-clicking it under the server).
    void openRecent(const QUrl& server, const QString& layoutId);
    // Fill the server list again (after File › Servers… changed it).
    void refreshServers();
    // Ask the server for the listed layouts or venues again, keeping what
    // is picked (done when you come back to the window, see RefreshOnFocus).
    void refreshList();
    // How to open File › Servers… from here (the app's; tests replace it).
    void setManageServers(std::function<void()> manage) { manageServers_ = std::move(manage); }

    // Delete the picked layout (you own it) or venue on the server, after
    // asking. How it asks (tests answer instead): the name, and whether
    // it's a layout.
    void deleteSelected();
    void setConfirmDelete(std::function<bool(const QString& name, bool layout)> f) { confirmDelete_ = std::move(f); }

private:
    void showMessage(const QString& text);
    // The server needs a newer app: say so, with a button to download it.
    void showUpdateNeeded(const QString& text, const QString& downloadUrl);
    void onVersion(const ServerInfo& info);
    void haveToken(const QString& token);
    void onFailed(const QString& what, const QString& message, bool unauthorized);
    void showLayouts(const QList<LayoutEntry>& layouts);
    void showVenues(const QList<VenueEntry>& venues);
    void showOrgs(const QList<OrgEntry>& orgs);
    void signInAgain();
    void openSelected();
    void filterLayouts(const QString& text);
    // Fill the Show filter with All, Mine and the clubs in the list (key, name).
    void setShowChoices(const QList<std::pair<QString, QString>>& clubs);
    void onServerPicked();
    // The ids of the picked rows, and picking them again after a refill.
    QStringList pickedIds() const;
    void pickAgain(const QStringList& ids);

    ServerApi& api_;
    TokenStore& tokens_;
    std::function<void(const QUrl&)> openUrl_;
    QUrl server_;
    QString token_;
    std::optional<ConnectResult> result_;
    Purpose purpose_;
    QList<DownloadedVenue> venues_;
    int venuesPending_ = 0;

    QString pendingLayout_;  // a recent layout to open once listed
    std::function<void()> manageServers_;
    std::function<bool(const QString&, bool)> confirmDelete_;
    void updateDeleteButton();

    QStackedWidget* pages_ = nullptr;
    QTreeWidget* servers_ = nullptr;  // your servers, each with its recent layouts
    QLabel* publishServer_ = nullptr;
    QLineEdit* address_ = nullptr;
    QPushButton* connectBtn_ = nullptr;
    QLabel* message_ = nullptr;
    // "Download the new version": shown when the server needs a newer app.
    QPushButton* updateBtn_ = nullptr;
    QUrl downloadUrl_;
    ServerInfo info_;
    QLabel* code_ = nullptr;
    QLabel* codeHint_ = nullptr;
    QLineEdit* filter_ = nullptr;
    QComboBox* show_ = nullptr;  // All / Mine / each club
    QTreeWidget* layouts_ = nullptr;
    QPushButton* openBtn_ = nullptr;
    QPushButton* deleteBtn_ = nullptr;
    QString deleting_;  // the name being deleted
    QString afterList_; // said once the list comes back ("Deleted …")
    QByteArray publishBbm_, publishSidecar_;
    QComboBox* owner_ = nullptr;
    QLineEdit* publishTitle_ = nullptr;
    QPushButton* publishBtn_ = nullptr;
};

} // namespace bld::sync
