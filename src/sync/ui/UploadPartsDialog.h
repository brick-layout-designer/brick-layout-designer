#pragma once

// Your parts the server doesn't know yet (sync phase P4b): a checklist of
// them, an owner (you, or one of your organisations), and Upload. Nothing
// is uploaded until the user confirms.

#include "PartsUpload.h"

#include <QDialog>

#include <functional>

class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;

namespace bld::sync {

class ServerApi;

class UploadPartsDialog : public QDialog {
    Q_OBJECT
public:
    UploadPartsDialog(ServerApi& api, PartsUpload& upload, const QList<LocalPart>& missing,
                      QWidget* parent = nullptr);

    // The same as clicking Upload.
    void uploadChecked();
    int uploadedCount() const { return uploaded_; }
    // Asked before parts go to a club (tests answer instead).
    std::function<bool(const QString& club)> confirmClub;

private:
    ServerApi& api_;
    PartsUpload& upload_;
    QList<LocalPart> missing_;
    QListWidget* list_ = nullptr;
    QComboBox* owner_ = nullptr;
    QLabel* message_ = nullptr;
    QPushButton* uploadBtn_ = nullptr;
    int uploaded_ = 0;
};

} // namespace bld::sync
