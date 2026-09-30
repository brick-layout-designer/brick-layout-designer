#include "PartDifferencesDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QXmlStreamReader>

namespace bld::ui {

namespace {

constexpr int kPreview = 96;

// The part's sprite: the hi-res .png first, then the others.
QImage spriteOf(const QMap<QString, QByteArray>& files) {
    for (const char* suffix : { "png", "gif", "jpg", "jpeg" }) {
        for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
            if (QFileInfo(it.key()).suffix().compare(QLatin1String(suffix), Qt::CaseInsensitive) != 0) continue;
            const QImage image = QImage::fromData(it.value());
            if (!image.isNull()) return image;
        }
    }
    return {};
}

QByteArray xmlOf(const QMap<QString, QByteArray>& files) {
    for (auto it = files.constBegin(); it != files.constEnd(); ++it)
        if (it.key().endsWith(QLatin1String(".xml"), Qt::CaseInsensitive)) return it.value();
    return {};
}

// One side of a row: the sprite over its description and author.
QWidget* sideOf(const QMap<QString, QByteArray>& files, const QString& name) {
    auto* box = new QWidget;
    box->setObjectName(name);
    auto* v = new QVBoxLayout(box);
    v->setContentsMargins(0, 0, 0, 0);
    auto* preview = new QLabel;
    preview->setObjectName(QStringLiteral("preview"));
    preview->setFixedSize(kPreview, kPreview);
    preview->setAlignment(Qt::AlignCenter);
    preview->setFrameShape(QFrame::StyledPanel);
    const QImage image = spriteOf(files);
    if (image.isNull()) {
        preview->setText(QObject::tr("No image"));
    } else {
        preview->setPixmap(QPixmap::fromImage(
            image.scaled(kPreview - 4, kPreview - 4, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    }
    v->addWidget(preview);
    const auto info = PartDifferencesDialog::partInfo(xmlOf(files));
    auto* text = new QLabel;
    text->setObjectName(QStringLiteral("info"));
    text->setWordWrap(true);
    text->setMaximumWidth(180);
    QStringList lines;
    lines << (info.description.isEmpty() ? QObject::tr("(no description)") : info.description);
    if (!info.author.isEmpty()) lines << QObject::tr("by %1").arg(info.author);
    text->setText(lines.join(QLatin1Char('\n')));
    v->addWidget(text);
    return box;
}

}  // namespace

PartDifferencesDialog::Info PartDifferencesDialog::partInfo(const QByteArray& xml) {
    Info info;
    QXmlStreamReader r(xml);
    if (!r.readNextStartElement()) return info;
    while (r.readNextStartElement()) {
        if (r.name() == QLatin1String("Author")) {
            info.author = r.readElementText().trimmed();
        } else if (r.name() == QLatin1String("Description")) {
            while (r.readNextStartElement()) {
                const bool english = r.name() == QLatin1String("en");
                const QString text = r.readElementText().trimmed();
                if (english || info.description.isEmpty()) info.description = text;
            }
        } else {
            r.skipCurrentElement();
        }
    }
    return info;
}

PartDifferencesDialog::PartDifferencesDialog(const QList<Row>& rows, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Parts That Differ"));
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("This layout has its own version of %n part(s) you already have. "
                                "Choose which to use for each.", nullptr, static_cast<int>(rows.size())));
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* grid = new QGridLayout;
    grid->addWidget(new QLabel(tr("<b>Part</b>")), 0, 0);
    grid->addWidget(new QLabel(tr("<b>Yours</b>")), 0, 1);
    grid->addWidget(new QLabel(tr("<b>The layout's</b>")), 0, 2);
    grid->addWidget(new QLabel(tr("<b>Use</b>")), 0, 3);
    int line = 1;
    for (const auto& row : rows) {
        grid->addWidget(new QLabel(row.key), line, 0, Qt::AlignTop);
        grid->addWidget(sideOf(row.mine, QStringLiteral("mine-") + row.key), line, 1, Qt::AlignTop);
        grid->addWidget(sideOf(row.layouts, QStringLiteral("layouts-") + row.key), line, 2, Qt::AlignTop);
        auto* choice = new QComboBox;
        choice->setObjectName(QStringLiteral("choice-") + row.key);
        choice->addItem(tr("Keep mine"), static_cast<int>(Choice::KeepMine));
        choice->addItem(tr("Use the layout's"), static_cast<int>(Choice::UseLayouts));
        choice->addItem(tr("Keep both"), static_cast<int>(Choice::KeepBoth));
        choice->setToolTip(tr("Use the layout's: your files are backed up, then replaced.\n"
                              "Keep both: the layout's is added under a new part number, "
                              "and this layout uses it."));
        grid->addWidget(choice, line, 3, Qt::AlignTop);
        combos_.insert(row.key, choice);
        ++line;
    }
    grid->setRowStretch(line, 1);
    auto* inner = new QWidget;
    inner->setLayout(grid);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(inner);
    layout->addWidget(scroll, 1);

    auto* buttons = new QDialogButtonBox;
    auto* apply = buttons->addButton(tr("Apply"), QDialogButtonBox::AcceptRole);
    buttons->addButton(tr("Keep All Mine"), QDialogButtonBox::RejectRole);
    apply->setDefault(true);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    resize(640, qMin(160 + 170 * static_cast<int>(rows.size()), 640));
}

QMap<QString, PartDifferencesDialog::Choice> PartDifferencesDialog::choices() const {
    QMap<QString, Choice> out;
    for (auto it = combos_.constBegin(); it != combos_.constEnd(); ++it)
        out.insert(it.key(), static_cast<Choice>(it.value()->currentData().toInt()));
    return out;
}

}  // namespace bld::ui
