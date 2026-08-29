#pragma once

#include <QDialog>
#include <QStringList>

#include "model/RentedAccount.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;
class QTextEdit;

// Thêm / sửa một tài khoản Facebook cho thuê: cookie riêng, nội dung chữ + nhiều
// ảnh, gói bài đã mua và giá. Dùng chung cho cả chức năng thêm mới và sửa.
class RentedDialog : public QDialog
{
    Q_OBJECT
public:
    RentedDialog(const RentedAccount &account, bool creating, QWidget *parent = nullptr);

    RentedAccount account() const;

private:
    void chooseImages();
    void updateImageLabel();

    RentedAccount m_account;
    bool m_creating;
    QStringList m_images;

    QLineEdit *m_txtName = nullptr;
    QTextEdit *m_txtCookie = nullptr;
    QLineEdit *m_txtProxy = nullptr;
    QTextEdit *m_txtPostText = nullptr;
    QSpinBox *m_spinTotal = nullptr;
    QSpinBox *m_spinPrice = nullptr;
    QListWidget *m_lstImages = nullptr;
    QLabel *m_lblImages = nullptr;
};
