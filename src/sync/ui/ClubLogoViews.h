#pragma once

// Club logos (ClubLogos) in the club pickers and lists: each club shows its
// logo beside its name, and one that comes, changes or goes later shows at
// once. Clubs without a logo keep their plain look.

#include <QString>
#include <QUrl>

#include <functional>

class QComboBox;
class QLabel;
class QWidget;

namespace bld::sync {

// Gives each club in the picker (an item whose data is the club's slug) its
// logo on `server`, now and whenever one comes or changes. Call it again
// after filling the picker anew.
void showClubLogos(QComboBox* box, std::function<QUrl()> server);

// A small picture of the club's logo for beside its name: hidden while the
// club has none, and following the cache.
QLabel* clubLogoLabel(const QUrl& server, const QString& slug, int size, QWidget* parent);

}  // namespace bld::sync
