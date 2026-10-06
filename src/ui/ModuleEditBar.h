#pragma once

// The bar over the top of the map while a module is edited: "Editing
// module ‹name›", who else is in it ("Sam is here too", from presence),
// a help button and Done, as the web's ModuleEditBar.tsx.

#include <QWidget>

class QLabel;
class QPushButton;

namespace bld::ui {

class ModuleEditBar : public QWidget {
    Q_OBJECT
public:
    explicit ModuleEditBar(QWidget* parent);

    void setModuleName(const QString& name);
    // "Sam is here too", or empty for nobody else.
    void setOthers(const QString& text);
    // Under the name, for a module on several sheets or with hidden ones:
    // "This module uses 2 sheets. New parts go on Track (the picked
    // sheet)." and "1 of its sheets is hidden." with a Show button
    // (`showLabel`, empty for none). Empty text hides the line.
    void setSheetsHint(const QString& text, const QString& showLabel);
    QString sheetsHint() const;
    QPushButton* showSheetsButton() const { return showSheets_; }
    // Keeps the bar centred along the top of `area` (parent coords).
    void place(const QRect& area);

    QString text() const;
    QString others() const;
    QPushButton* doneButton() const { return done_; }

signals:
    void done();
    void showHiddenSheets();

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    QLabel* text_ = nullptr;
    QLabel* others_ = nullptr;
    QPushButton* done_ = nullptr;
    QWidget* sheetsRow_ = nullptr;
    QLabel* sheets_ = nullptr;
    QPushButton* showSheets_ = nullptr;
};

}  // namespace bld::ui
