#include "ServerRefusal.h"

#include "ServerApi.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>

#include <utility>

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
    r.permission = o.value(QLatin1String("permission")).toString();
    r.downloadUrl = ServerApi::webLink(o.value(QLatin1String("downloadUrl")).toString()).toString();
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
    if (r.status == 401) return true;
    // A token without the rights this needs: a new sign-in asks for them.
    return r.status == 403 && r.code == QLatin1String("insufficient_scope");
}

QString permissionLabel(const QString& permission) {
    // The web's role editor (apps/web/src/orgs/clubPermissions.ts).
    static const std::pair<const char*, const char*> labels[] = {
        { "view_items", QT_TRANSLATE_NOOP("ServerRefusal", "View club items") },
        { "edit_layouts", QT_TRANSLATE_NOOP("ServerRefusal", "Edit club layouts") },
        { "start_layouts", QT_TRANSLATE_NOOP("ServerRefusal", "Start new club layouts") },
        { "edit_modules", QT_TRANSLATE_NOOP("ServerRefusal", "Add or edit modules") },
        { "add_parts", QT_TRANSLATE_NOOP("ServerRefusal", "Add parts") },
        { "manage_venues", QT_TRANSLATE_NOOP("ServerRefusal", "Manage venues") },
        { "delete_items", QT_TRANSLATE_NOOP("ServerRefusal", "Delete club items") },
        { "send_catalog", QT_TRANSLATE_NOOP("ServerRefusal", "Send to the catalog") },
        { "invite", QT_TRANSLATE_NOOP("ServerRefusal", "Invite people") },
        { "manage_members", QT_TRANSLATE_NOOP("ServerRefusal", "Manage members and roles") },
        { "export_data", QT_TRANSLATE_NOOP("ServerRefusal", "Download the club's data") },
    };
    for (const auto& [key, label] : labels)
        if (permission == QLatin1String(key)) return tr(label);
    return permission;
}

QString askForPermission(const QString& permission) {
    return tr("Ask a club admin for the %1 permission.").arg(permissionLabel(permission));
}

namespace {
// A 403 the server explained: who you are here (your role on the thing, a
// club's permissions), not your sign-in.
bool isPermissionRefusal(const ServerRefusal& r) {
    return r.status == 403 && r.jsonBody && !needsSignIn(r) && !isLimitRefusal(r) && !isAccountState(r);
}
}  // namespace

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
    if (r.status == 401) return tr("Your sign-in has run out. Sign in again (File › Servers…).");
    if (r.code == QLatin1String("insufficient_scope") || r.code == QLatin1String("token_not_allowed"))
        return tr("This sign-in can't do that. Sign in again (File › Servers…) to give the app the rights it "
                  "needs.");
    if (r.code == QLatin1String("not_an_org_member")) return tr("You're not a member of that club.");
    if (r.code.startsWith(QLatin1String("demo_account"))) return tr("The demo account can't do that.");
    if (!r.permission.isEmpty()) {
        const QString why = r.code == QLatin1String("only_club_admins_can_add")
                                ? tr("Your roles in this club can't add that.")
                                : tr("Your roles in this club don't allow that.");
        return why + QLatin1Char(' ') + askForPermission(r.permission);
    }
    // Servers before club roles name no permission.
    if (r.code == QLatin1String("only_club_admins_can_add"))
        return tr("This club only lets some of its members add things. Ask a club admin.");
    if (isPermissionRefusal(r))
        return tr("You don't have permission to do that. Ask its owner, or a club admin, for access.");
    if (!r.code.isEmpty()) return r.code;
    if (r.status == 0) return r.networkError.isEmpty() ? tr("The server could not be reached.") : r.networkError;
    return tr("The server answered %1").arg(r.status);
}

QString failureText(const ServerRefusal& r) {
    if (isFirewallBlock(r) || isLimitRefusal(r) || isAccountState(r) || isUpdateRequired(r) || isPermissionRefusal(r) ||
        r.status == 0)
        return describe(r);
    return tr("The server answered %1").arg(r.status);
}

QString liveCloseText(int code, const QString& reason) {
    if (code == 4415 && reason == QLatin1String("unreadable_doc"))
        return tr("this layout was saved by a newer version of Brick Layout Designer. Please download the new "
                  "version (Help > Check for Updates) to keep working on it");
    if (code == 4416 && reason == QLatin1String("unreadable_update"))
        return tr("this layout's live update couldn't be read. Please reopen the layout to keep working on it live");
    if (code == 4426)
        return tr("this version of Brick Layout Designer is too old for this server. Please download the new "
                  "version (Help > Check for Updates)");
    if (code == 1008 && reason == QLatin1String("account_pending_deletion"))
        return tr("this account is being deleted. To keep it, sign in on the website before the date in your email; "
                  "then open the layout again");
    if (code == 1008 && reason == QLatin1String("account_restricted"))
        return tr("your account is on hold (read only) while a privacy request is looked at. Ask the site admin");
    // The server's merge guard (apps/server/src/ws/mergeGuard.ts): an update
    // that would bring another layout's layers into this one.
    if (code == 1008 && reason == QLatin1String("layout_merge_refused"))
        return tr("your changes weren't taken because they looked like another layout's parts being merged into "
                  "this one. Nothing is lost: your copy stays open here, so save it (File > Save As) and tell the "
                  "site admin");
    // The layout went away, or who may open it changed (apps/server/src/ws/handler.ts, routes/ws.ts).
    if (code == 4404 && reason == QLatin1String("layout_deleted")) return tr("this layout was deleted on the server");
    if (code == 4404 && (reason == QLatin1String("access_revoked") || reason == QLatin1String("not_found")))
        return tr("you can't open this layout any more: it was deleted, or its owner or a club admin changed who can "
                  "open it");
    // The sign-in ended: signed out elsewhere, or the sign-in was removed on the website.
    if (code == 1008 && (reason == QLatin1String("token_revoked") || reason == QLatin1String("session_revoked") ||
                         reason == QLatin1String("unauthorized")))
        return tr("your sign-in on this server ended (signed out elsewhere, or removed on the website). Sign in again "
                  "(File › Servers…), then open the layout again");
    if (code == 4429 && reason == QLatin1String("limit_reached"))
        return tr("too many people have this layout open right now; try again in a little while");
    return {};
}

}  // namespace bld::sync
