#pragma once

#include <QDialog>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>

#include "../import/ImportToPart.h"

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QLineEdit;
class QSpinBox;

namespace bld::ui {

// Preview dialog shown after parsing + rasterizing an LDraw / Studio /
// LDD model but BEFORE writing it as a library part. Lets the user
// confirm the import looks right (sprite, dimensions, errors), name
// the resulting part, and either accept or cancel. Cancelling aborts
// the import cleanly without disturbing the parts library.
//
// Stats panel mirrors what the importer reports — translated /
// rendered / unmapped / skipped counts plus any per-part error
// strings the bake collected. Helps diagnose why a model came out
// half-rendered without round-tripping through the file system.
class ImportPreviewDialog : public QDialog {
    Q_OBJECT
public:
    struct Stats {
        int    ldrawResolved = 0;     // refs the LDraw library returned geometry for
        int    lddRendered   = 0;     // refs the LDD .g fallback rendered
        int    translated    = 0;     // LDD refs we re-mapped to LDraw
        int    unmapped      = 0;     // LDD refs that had no LDraw equivalent
        int    skipped       = 0;     // LDD refs the .g lookup couldn't find
    };

    ImportPreviewDialog(const QString& sourceFile,
                        const QString& kindLabel,
                        const QImage& sprite,
                        int widthStuds,
                        int heightStuds,
                        const Stats& stats,
                        const QStringList& errors,
                        const QVector<import::ImportedConnection>& connections,
                        QWidget* parent = nullptr);

    // Final values after exec() returns Accepted.
    QString partName() const;


private:
    QLineEdit*     nameEdit_   = nullptr;
};

}  // namespace bld::ui
