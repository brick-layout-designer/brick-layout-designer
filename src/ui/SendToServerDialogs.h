#pragma once

// Sending things to a server, in the web's words:
// - SaveToServerDialog: a name and where it's saved ("Save to": Me, or one
//   of your clubs that takes new things), asking first before a club gets
//   it (the club owns it, you stay credited). Used for venues, Module
//   library files and single parts; layouts use File › Save to Server…
//   (ConnectDialog's publish page, with the same chooser and question).
// - ShareToCatalogDialog: "Share to the public catalog" / "Publish this
//   update" (the web's ShareToCatalogDialog): name, description, tags,
//   what changed, the "anyone can copy this" box, then whether it's public
//   or waiting for review. Its card shows the drawn picture; a cover of
//   your own is chosen on the web.

#include "../sync/LibraryApi.h"

#include <QDialog>
#include <QUrl>

#include <functional>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;

namespace bld::ui {

class SaveToServerDialog : public QDialog {
    Q_OBJECT
public:
    // `what`: "venue", "module", "part". `server`: what you call it.
    // `initialOwner`: a club's slug to start at (the Show filter's), or empty for Me.
    SaveToServerDialog(const QString& what, const QString& name, const QString& server,
                       const QList<sync::OrgEntry>& orgs, const QString& initialOwner, QWidget* parent = nullptr);

    QString name() const;
    // Empty for you, else the club's slug.
    QString orgSlug() const;
    // The same as clicking Save to server.
    void save();

    QLineEdit* nameEdit() const { return name_; }
    QComboBox* owner() const { return owner_; }
    QLabel* error() const { return error_; }
    // Asked before saving into a club (tests answer instead).
    std::function<bool(const QString& club)> confirmClub;

private:
    QLineEdit* name_ = nullptr;
    QComboBox* owner_ = nullptr;
    QLabel* error_ = nullptr;
};

class ShareToCatalogDialog : public QDialog {
    Q_OBJECT
public:
    // `share`: its kind, source and the name offered. `update`: it's in the
    // catalog already. `review`: a moderator looks first. `clubReview`: a
    // trusted club's name when its own admins and managers review it.
    ShareToCatalogDialog(sync::LibraryApi& api, const sync::CatalogShare& share, bool update, bool review,
                         const QString& clubReview, QWidget* parent = nullptr);

    // A layout's picture for its card, made when Share is pressed (empty: none).
    std::function<void(std::function<void(const QByteArray& png)>)> makeThumbnail;
    // Opens a page on the website (QDesktopServices in the app; tests replace it).
    std::function<void(const QUrl&)> openUrl;

    // The same as clicking Share.
    void share();
    QString itemId() const { return itemId_; }
    QString status() const { return status_; }

    QLineEdit* nameEdit() const { return name_; }
    QPlainTextEdit* description() const { return description_; }
    QLineEdit* tags() const { return tags_; }
    QLineEdit* note() const { return note_; }
    QCheckBox* consent() const { return consent_; }
    QLabel* error() const { return error_; }
    QLabel* result() const { return result_; }

signals:
    // It went: its catalog item's id and status (public / in_review).
    void shared(const QString& itemId, const QString& status);

private:
    void send(const QByteArray& png);

    sync::LibraryApi& api_;
    sync::CatalogShare base_;
    bool update_ = false;
    QString clubReview_;
    QStackedWidget* pages_ = nullptr;
    QLineEdit* name_ = nullptr;
    QPlainTextEdit* description_ = nullptr;
    QLineEdit* tags_ = nullptr;
    QLineEdit* note_ = nullptr;
    QCheckBox* consent_ = nullptr;
    QLabel* error_ = nullptr;
    QLabel* result_ = nullptr;
    QPushButton* shareBtn_ = nullptr;
    QString itemId_;
    QString status_;
};

}  // namespace bld::ui
