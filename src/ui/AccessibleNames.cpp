#include "AccessibleNames.h"

#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QLabel>
#include <QPointer>
#include <QString>
#include <QTextDocumentFragment>

namespace bld::ui::a11y {

namespace {

// A help row (help::withHelp) wraps the field: its label names the row.
bool namedBy(const QLabel* label, const QWidget* w) {
    const QWidget* buddy = label->buddy();
    if (!buddy) return false;
    if (buddy == w) return true;
    return buddy->objectName() == QLatin1String("WithHelp") && buddy->isAncestorOf(w);
}

class Namer : public QObject {
public:
    using QObject::QObject;

protected:
    bool eventFilter(QObject* watched, QEvent* e) override {
        if (e->type() == QEvent::Show) {
            if (auto* combo = qobject_cast<QComboBox*>(watched); combo && combo->accessibleName().isEmpty()) {
                const QString name = labelNameFor(combo);
                if (!name.isEmpty()) combo->setAccessibleName(name);
            }
        }
        return QObject::eventFilter(watched, e);
    }
};

}  // namespace

QString labelNameFor(const QWidget* w) {
    if (!w) return {};
    const QWidget* window = w->window();
    for (const QLabel* label : window->findChildren<QLabel*>()) {
        if (!namedBy(label, w)) continue;
        QString text = label->text();
        if (Qt::mightBeRichText(text)) text = QTextDocumentFragment::fromHtml(text).toPlainText();
        text.replace(QLatin1String("&&"), QStringLiteral("\x01")).remove(QLatin1Char('&')).replace(QChar(1), QLatin1Char('&'));
        text = text.trimmed();
        while (text.endsWith(QLatin1Char(':'))) text.chop(1);
        return text.trimmed();
    }
    return {};
}

void install() {
    static QPointer<Namer> namer;
    if (namer || !qApp) return;
    namer = new Namer(qApp);
    qApp->installEventFilter(namer);
}

}  // namespace bld::ui::a11y
