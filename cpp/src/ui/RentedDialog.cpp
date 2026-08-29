#include "ui/RentedDialog.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTextEdit>
#include <QVBoxLayout>

RentedDialog::RentedDialog(const RentedAccount &account, bool creating, QWidget *parent)
    : QDialog(parent)
    , m_account(account)
    , m_creating(creating)
    , m_images(account.images)
{
    setWindowTitle(creating ? QStringLiteral("Thêm tài khoản thuê")
                            : QStringLiteral("Sửa tài khoản thuê"));
    resize(620, 560);

    auto *form = new QFormLayout(this);
    form->setSpacing(10);
    form->setContentsMargins(16, 18, 16, 16);

    m_txtName = new QLineEdit(account.name, this);
    m_txtName->setPlaceholderText(QStringLiteral("Tên khách thuê / tài khoản, ví dụ: Khách A"));
    form->addRow(QStringLiteral("Tên khách thuê:"), m_txtName);

    m_txtCookie = new QTextEdit(this);
    m_txtCookie->setAcceptRichText(false);
    m_txtCookie->setPlainText(account.cookieRaw);
    m_txtCookie->setPlaceholderText(QStringLiteral("c_user=...; xs=...; fr=..."));
    m_txtCookie->setMaximumHeight(90);
    form->addRow(QStringLiteral("Cookie:"), m_txtCookie);

    m_txtProxy = new QLineEdit(account.proxy, this);
    m_txtProxy->setPlaceholderText(
        QStringLiteral("Ví dụ: socks5://user:pass@host:port — để trống nếu không dùng"));
    m_txtProxy->setToolTip(QStringLiteral("Che IP cho tài khoản thuê này. Hỗ trợ http://, "
                                          "https://, socks4://, socks5://, có thể kèm user:pass@"));
    form->addRow(QStringLiteral("Proxy (tùy chọn):"), m_txtProxy);

    m_txtPostText = new QTextEdit(this);
    m_txtPostText->setAcceptRichText(false);
    m_txtPostText->setPlainText(account.postText);
    m_txtPostText->setPlaceholderText(
        QStringLiteral("Nội dung bài đăng riêng của tài khoản này (có thể dùng {{ten}}, "
                       "{{group}}, {{ngay}}, {{gio}}...). Để trống = dùng nội dung chung ở trang Đăng bài."));
    m_txtPostText->setMaximumHeight(110);
    form->addRow(QStringLiteral("Nội dung bài đăng:"), m_txtPostText);

    // Nhiều ảnh: nút chọn + danh sách đã chọn.
    auto *imgBox = new QVBoxLayout();
    imgBox->setSpacing(6);
    auto *imgRow = new QHBoxLayout();
    imgRow->setSpacing(8);
    auto *btnImages = new QPushButton(QStringLiteral("+ Chọn ảnh"), this);
    m_lblImages = new QLabel(this);
    m_lblImages->setObjectName(QStringLiteral("chip"));
    imgRow->addWidget(btnImages);
    imgRow->addWidget(m_lblImages, 1);
    imgBox->addLayout(imgRow);
    m_lstImages = new QListWidget(this);
    m_lstImages->setMaximumHeight(90);
    m_lstImages->setSelectionMode(QAbstractItemView::NoSelection);
    imgBox->addWidget(m_lstImages);
    form->addRow(QStringLiteral("Ảnh (nhiều ảnh):"), imgBox);
    connect(btnImages, &QPushButton::clicked, this, &RentedDialog::chooseImages);
    updateImageLabel();

    m_spinTotal = new QSpinBox(this);
    m_spinTotal->setRange(1, 1000000);
    m_spinTotal->setValue(qMax(1, account.totalPosts));
    m_spinTotal->setSuffix(QStringLiteral(" bài"));
    m_spinTotal->setToolTip(QStringLiteral("Tổng số bài khách đã mua trong gói."));
    form->addRow(QStringLiteral("Tổng số bài trong gói:"), m_spinTotal);

    m_spinPrice = new QSpinBox(this);
    m_spinPrice->setRange(0, 1000000000);
    m_spinPrice->setSingleStep(10000);
    m_spinPrice->setValue(qMax(0, account.price));
    m_spinPrice->setSuffix(QStringLiteral(" đ"));
    m_spinPrice->setToolTip(QStringLiteral("Giá tiền (VND) của gói bài."));
    form->addRow(QStringLiteral("Giá gói:"), m_spinPrice);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    bb->button(QDialogButtonBox::Save)->setText(QStringLiteral("Lưu"));
    bb->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Hủy"));
    // Ảnh là TÙY CHỌN: không có ảnh thì đăng nội dung chữ, có ảnh thì đăng cả hai.
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(bb);
}

RentedAccount RentedDialog::account() const
{
    RentedAccount a = m_account;
    a.name = m_txtName->text().trimmed();
    a.cookieRaw = m_txtCookie->toPlainText().trimmed();
    a.proxy = m_txtProxy->text().trimmed();
    a.postText = m_txtPostText->toPlainText().trimmed();
    a.images = m_images;
    a.totalPosts = m_spinTotal->value();
    a.price = m_spinPrice->value();
    return a;
}

void RentedDialog::chooseImages()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Chọn ảnh cho tài khoản thuê"), QString(),
        QStringLiteral("Tệp ảnh (*.png *.jpg *.jpeg *.gif *.bmp *.webp)"));
    if (files.isEmpty())
        return;
    m_images = files;
    updateImageLabel();
}

void RentedDialog::updateImageLabel()
{
    m_lstImages->clear();
    for (const QString &f : m_images)
        m_lstImages->addItem(new QListWidgetItem(QFileInfo(f).fileName()));
    m_lblImages->setText(m_images.isEmpty()
                             ? QStringLiteral("Chưa có ảnh — sẽ đăng nội dung chữ")
                             : QStringLiteral("%1 ảnh đã chọn").arg(m_images.size()));
}
