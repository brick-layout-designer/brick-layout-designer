#pragma once

// A club's members, read only (the Server window's Members… for the club
// picked in Show), as the website's members list shows them: each member's
// name, the roles they hold (several, in the club's order) and roughly when
// they were last active (today, this week, this month, a month or more).
// It shows only what the server sends to you: last active comes only for
// the people who run the club, and for yourself.
//
// Managing members stays on the website: people whose roles may invite or
// manage members get a "Manage members on the website" link. The list asks
// the server again on its club hints (a role given or taken, someone
// joining or leaving) and when the club list changes, so a change shows
// without a restart. The club's logo (ClubLogos) heads it.

#include "../sync/LibraryApi.h"

#include <QString>
#include <QUrl>
#include <QWidget>

#include <functional>

class QLabel;
class QTimer;
class QVBoxLayout;

namespace bld::ui {

class ServerLibrary;

class ClubMembersDialog : public QWidget {
    Q_OBJECT
public:
    ClubMembersDialog(ServerLibrary& library, const QString& slug, QWidget* parent = nullptr);

    QString slug() const { return slug_; }
    // Asks the server for the members again (now, or after a short wait so a burst of hints asks once).
    void reload();
    void reloadSoon();

    // Opens a page of the website (QDesktopServices; tests replace it).
    std::function<void(const QUrl&)> openLink;
    // Where "Manage members on the website" goes; empty when not offered.
    QUrl managePage() const;

    // For tests: a member's row ("memberRow:<userId>"), the status line, the link.
    QWidget* row(const QString& userId) const;
    QLabel* statusLine() const { return status_; }
    QLabel* manageLink() const { return manage_; }
    QLabel* logo() const { return logo_; }
    // How many times the members were asked for.
    int loads() const { return loads_; }

private:
    const sync::OrgEntry* club() const;
    void updateHeader();
    void rebuild();

    ServerLibrary& library_;
    QString slug_;
    QUrl server_;
    QLabel* logo_ = nullptr;
    QLabel* name_ = nullptr;
    QLabel* count_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* manage_ = nullptr;
    QWidget* host_ = nullptr;
    QVBoxLayout* list_ = nullptr;
    QTimer* soon_ = nullptr;
    sync::ClubMembers members_;
    bool loaded_ = false;
    int generation_ = 0;
    int loads_ = 0;
};

} // namespace bld::ui
