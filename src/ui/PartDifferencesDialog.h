#pragma once

#include <QByteArray>
#include <QDialog>
#include <QList>
#include <QMap>
#include <QString>

class QComboBox;

namespace bld::ui {

// Opening a layout whose parts share a number with parts of yours but are
// defined differently: one row per part with both sprites, descriptions and
// authors side by side, and a choice for each. Apply hands back the choices;
// "Keep All Mine" closes with nothing changed.
class PartDifferencesDialog : public QDialog {
    Q_OBJECT
public:
    enum class Choice { KeepMine, UseLayouts, KeepBoth };

    struct Row {
        QString key;                       // e.g. "MINE.1"
        QMap<QString, QByteArray> mine;    // your XML and sprites, by file name
        QMap<QString, QByteArray> layouts; // the layout's
    };

    explicit PartDifferencesDialog(const QList<Row>& rows, QWidget* parent = nullptr);

    // The choice for each row, by part key.
    QMap<QString, Choice> choices() const;

    // The description and author in a part's XML (the English description,
    // else the first).
    struct Info {
        QString description;
        QString author;
    };
    static Info partInfo(const QByteArray& xml);

private:
    QMap<QString, QComboBox*> combos_;
};

}  // namespace bld::ui
