#pragma once

// File › Servers… ("Your servers"): the servers this desktop knows, each
// with who is signed in there, its version and anything it can't do yet.
// Add a server by its web address, rename or remove one, sign in or out,
// and mark one as Main (your settings follow your account there).
//
// Each server is asked about itself with its own ServerApi, and only ever
// with its own token from the TokenStore.

#include "ServerList.h"

#include <QDialog>
#include <QHash>
#include <QUrl>

#include <functional>
#include <memory>

class QLabel;
class QListWidget;
class QPushButton;

namespace bld::sync {

class TokenStore;

class ServersDialog : public QDialog {
    Q_OBJECT
public:
    ServersDialog(TokenStore& tokens, std::function<void(const QUrl&)> openUrl, QWidget* parent = nullptr);
    ~ServersDialog() override;

    // What each button does, without the questions (tests call these).
    // False, with `error` set, when the address isn't a server address.
    bool addServer(const QString& address, const QString& name, QString* error = nullptr);
    void renameServer(const QUrl& url, const QString& name);
    void removeServer(const QUrl& url);
    void makeMain(const QUrl& url);
    void signOut(const QUrl& url);
    // Opens the sign-in window for `url` (once the current signal returns).
    void signIn(const QUrl& url);
    // How signing in is done (the app's ConnectDialog; tests replace it).
    // It gets the server and returns its new token, or empty.
    void setSignInHandler(std::function<QString(const QUrl&)> handler) { signInHandler_ = std::move(handler); }

    // Ask every server about itself again and fill the list.
    void refresh();

    // The row's status line for a server: "Signed in as Ann · version 2.1.0".
    QString statusText(const QUrl& url) const;
    // The row's "can't do yet" line, empty when it can do everything.
    QString missingText(const QUrl& url) const;
    bool isSignedIn(const QUrl& url) const;
    // The server picked in the list (invalid when none).
    QUrl selectedServer() const;
    void selectServer(const QUrl& url);

private:
    struct Status {
        enum class Sign { Checking, In, Out, Expired } sign = Sign::Checking;
        bool unreachable = false;
    };
    void rebuild();
    void check(const ServerEntry& e);
    void updateRow(const QUrl& url);
    void updateButtons();
    void onAdd();
    void onRename();
    void onRemove();
    void onSignInOut();

    TokenStore& tokens_;
    std::function<void(const QUrl&)> openUrl_;
    std::function<QString(const QUrl&)> signInHandler_;
    ServerList list_;
    QHash<QString, Status> status_;  // by ServerEntry::key()
    // Callbacks that may finish after the dialog closed check this first.
    std::shared_ptr<int> alive_ = std::make_shared<int>(0);

    QListWidget* rows_ = nullptr;
    QLabel* empty_ = nullptr;
    QPushButton* renameBtn_ = nullptr;
    QPushButton* removeBtn_ = nullptr;
    QPushButton* signBtn_ = nullptr;
    QPushButton* mainBtn_ = nullptr;
};

}  // namespace bld::sync
