#include "ServerRefusal.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>

namespace bld::sync {

namespace {
QString tr(const char* s) { return QCoreApplication::translate("ServerRefusal", s); }
}  // namespace

ServerRefusal readRefusal(int status, const QByteArray& body, const QString& networkError) {
    ServerRefusal r;
    r.status = status;
    r.networkError = networkError;
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    r.jsonBody = doc.isObject();
    const QJsonObject o = doc.object();
    r.code = o.value(QLatin1String("error")).toString();
    r.message = o.value(QLatin1String("message")).toString().trimmed();
    r.limit = o.value(QLatin1String("limit")).toString();
    r.downloadUrl = o.value(QLatin1String("downloadUrl")).toString();
    r.minimum = o.value(QLatin1String("minimum")).toString();
    return r;
}

bool isLimitRefusal(const ServerRefusal& r) {
    return r.code == QLatin1String("limit_reached") || r.code == QLatin1String("suspended") ||
           r.code == QLatin1String("rate_limited") || r.code == QLatin1String("verify_email_first");
}

bool isAccountState(const ServerRefusal& r) {
    return r.code == QLatin1String("account_pending_deletion") || r.code == QLatin1String("account_restricted");
}

bool isUpdateRequired(const ServerRefusal& r) {
    return r.status == 426 || r.code == QLatin1String("update_required");
}

bool isFirewallBlock(const ServerRefusal& r) {
    return r.status == 403 && !r.jsonBody;
}

bool needsSignIn(const ServerRefusal& r) {
    if (isLimitRefusal(r) || isFirewallBlock(r) || isAccountState(r)) return false;
    return r.status == 401 || r.status == 403;
}

QString describe(const ServerRefusal& r) {
    if (isFirewallBlock(r))
        return tr("The site's firewall blocked this request. Please tell the site admin (what you were doing, and "
                  "the time).");
    if (isLimitRefusal(r)) {
        // The server's own sentence names the limit; keep it short and plain.
        if (!r.message.isEmpty()) return r.message.left(400);
        if (r.code == QLatin1String("suspended")) return tr("This account is read-only for now. Ask the site admin why.");
        if (r.code == QLatin1String("rate_limited")) return tr("Too many requests at once. Please wait a minute and try again.");
        if (r.code == QLatin1String("verify_email_first")) return tr("Please confirm your email address first.");
        return tr("You have reached a limit on this server. Ask the site admin for more room.");
    }
    if (isAccountState(r)) {
        if (!r.message.isEmpty()) return r.message.left(400);
        if (r.code == QLatin1String("account_pending_deletion"))
            return tr("This account is being deleted. To keep it, sign in on the website before then; then this app "
                      "works again.");
        return tr("Your account is on hold (read only) while a privacy request is looked at. You can still see and "
                  "download your things. Ask the site admin.");
    }
    if (isUpdateRequired(r)) {
        if (!r.message.isEmpty()) return r.message.left(400);
        return tr("This server needs a newer Brick Layout Designer. Please download the new version.");
    }
    if (!r.code.isEmpty()) return r.code;
    if (r.status == 0) return r.networkError.isEmpty() ? tr("The server could not be reached.") : r.networkError;
    return tr("The server answered %1").arg(r.status);
}

QString failureText(const ServerRefusal& r) {
    if (isFirewallBlock(r) || isLimitRefusal(r) || isAccountState(r) || isUpdateRequired(r) || r.status == 0) return describe(r);
    return tr("The server answered %1").arg(r.status);
}

QString liveCloseText(int code, const QString& reason) {
    if (code == 4415 && reason == QLatin1String("unreadable_doc"))
        return tr("this layout was saved by a newer version of Brick Layout Designer. Please download the new "
                  "version (Help > Check for Updates) to keep working on it");
    if (code == 4426)
        return tr("this version of Brick Layout Designer is too old for this server. Please download the new "
                  "version (Help > Check for Updates)");
    if (code == 1008 && reason == QLatin1String("account_pending_deletion"))
        return tr("this account is being deleted. To keep it, sign in on the website before the date in your email; "
                  "then open the layout again");
    if (code == 1008 && reason == QLatin1String("account_restricted"))
        return tr("your account is on hold (read only) while a privacy request is looked at. Ask the site admin");
    if (code == 4429 && reason == QLatin1String("limit_reached"))
        return tr("too many people have this layout open right now; try again in a little while");
    return {};
}

}  // namespace bld::sync
