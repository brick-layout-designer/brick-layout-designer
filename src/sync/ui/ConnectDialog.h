#pragma once

// File › Connect to Server… (sync phase P4): the server's address, a check
// that it speaks our protocol, device sign-in when there is no saved token
// (the code is shown and the browser opened), then a layout picker. The
// token goes to the TokenStore; the chosen layout is result().

#include "ServerApi.h"

#include <QDialog>
#include <QUrl>

#include <functional>
#include <optional>

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
};

class ConnectDialog : public QDialog {
    Q_OBJECT
public:
    // `openUrl` opens the sign-in page (QDesktopServices in the app).
    ConnectDialog(ServerApi& api, TokenStore& tokens, std::function<void(const QUrl&)> openUrl,
                  QWidget* parent = nullptr);

    std::optional<ConnectResult> result() const { return result_; }

    void setAddress(const QString& address);
    // The same as clicking Connect.
    void connectToServer();
    // Forget the saved token for this server and sign in again.
    void signOut();

private:
    void showMessage(const QString& text);
    void onVersion(const ServerInfo& info);
    void haveToken(const QString& token);
    void onFailed(const QString& what, const QString& message, bool unauthorized);
    void showLayouts(const QList<LayoutEntry>& layouts);
    void openSelected();
    void filterLayouts(const QString& text);

    ServerApi& api_;
    TokenStore& tokens_;
    std::function<void(const QUrl&)> openUrl_;
    QUrl server_;
    QString token_;
    std::optional<ConnectResult> result_;

    QStackedWidget* pages_ = nullptr;
    QLineEdit* address_ = nullptr;
    QPushButton* connectBtn_ = nullptr;
    QLabel* message_ = nullptr;
    QLabel* code_ = nullptr;
    QLabel* codeHint_ = nullptr;
    QLineEdit* filter_ = nullptr;
    QTreeWidget* layouts_ = nullptr;
    QPushButton* openBtn_ = nullptr;
};

} // namespace bld::sync
