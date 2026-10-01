#pragma once

// What a collaborative server meant when it said no, in words for a
// person. The server sends a ready-made sentence with the refusals people
// can act on: a usage limit reached ("Your club has used its 10 GB. Ask
// the site admin for more room."), a read-only account, too many requests,
// or an email that needs confirming. Those are shown as they come and
// never mean "sign in again"; other 401/403 answers still do.

#include <QByteArray>
#include <QString>

namespace bld::sync {

struct ServerRefusal {
    int     status = 0;  // HTTP status; 0 when the server wasn't reached
    QString code;        // the body's "error", e.g. limit_reached
    QString message;     // the body's "message", when the server sent one
    QString limit;       // which limit (limit_reached only), e.g. storagePerClub
    QString networkError;
};

// Read a failed reply: its status, JSON body and Qt's network error text.
ServerRefusal readRefusal(int status, const QByteArray& body, const QString& networkError = {});

// A limit, a read-only account, a rate limit or an unconfirmed email.
bool isLimitRefusal(const ServerRefusal& r);

// The failure means this sign-in can't do it: sign in again.
bool needsSignIn(const ServerRefusal& r);

// One sentence to show.
QString describe(const ServerRefusal& r);

// Why the server closed a live layout, for the "Live layout closed" note.
// Empty when there's nothing better than the raw reason.
QString liveCloseText(int code, const QString& reason);

}  // namespace bld::sync
