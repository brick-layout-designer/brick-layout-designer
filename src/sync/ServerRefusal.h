#pragma once

// What a collaborative server meant when it said no, in words for a
// person. The server sends a ready-made sentence with the refusals people
// can act on: a usage limit reached ("Your club has used its 10 GB. Ask
// the site admin for more room."), a read-only account, too many requests,
// or an email that needs confirming. Those are shown as they come and
// never mean "sign in again"; other 401/403 answers still do. The same goes
// for an account that is being deleted or is on hold (isAccountState).

#include <QByteArray>
#include <QString>

namespace bld::sync {

struct ServerRefusal {
    int     status = 0;  // HTTP status; 0 when the server wasn't reached
    QString code;        // the body's "error", e.g. limit_reached
    QString message;     // the body's "message", when the server sent one
    QString limit;       // which limit (limit_reached only), e.g. storagePerClub
    QString networkError;
    // update_required (426) only: the oldest version the server accepts, and where to get it.
    QString minimum;
    QString downloadUrl;
    // The body was a JSON object: the app itself answered.
    bool jsonBody = false;
};

// Read a failed reply: its status, JSON body and Qt's network error text.
ServerRefusal readRefusal(int status, const QByteArray& body, const QString& networkError = {});

// A limit, a read-only account, a rate limit or an unconfirmed email.
bool isLimitRefusal(const ServerRefusal& r);

// The account itself is on hold: it's being deleted (the person asked;
// signing in on the website keeps it) or restricted while a privacy
// request is looked at (read only). Shown as the server words it, and
// never "sign in again": signing in here can't change it.
bool isAccountState(const ServerRefusal& r);

// This app is older than the server accepts (426 update_required).
bool isUpdateRequired(const ServerRefusal& r);

// A 403 the app never saw: an empty or non-JSON body means the site's
// firewall (WAF) answered, not the server. Not a permissions problem, and
// not a reason to sign in again.
bool isFirewallBlock(const ServerRefusal& r);

// The failure means this sign-in can't do it: sign in again.
bool needsSignIn(const ServerRefusal& r);

// What to show for a failed request, in plain words: the firewall message,
// a limit's own sentence, "The server answered 500", or Qt's network error.
QString failureText(const ServerRefusal& r);

// One sentence to show.
QString describe(const ServerRefusal& r);

// Why the server closed a live layout, for the "Live layout closed" note.
// Empty when there's nothing better than the raw reason.
QString liveCloseText(int code, const QString& reason);

}  // namespace bld::sync
