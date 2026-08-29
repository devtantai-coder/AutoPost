#include "ui/CookieDialog.h"

#include "store/DashboardStore.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSet>
#include <QTableWidget>
#include <QTextEdit>
#include <QUuid>
#include <QVBoxLayout>

CookieDialog::CookieDialog(const QVector<FacebookAccount> &accounts, const QString &activeCookie,
                           QWidget *parent)
    : QDialog(parent)
    , m_accounts(accounts)
{
    setWindowTitle(QStringLiteral("Quản lý tài khoản"));
    resize(760, 460);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(10);

    auto *instructions = new QLabel(
        QStringLiteral("Cách lấy Facebook Cookie:\n"
                       "1. Đăng nhập vào Facebook trên Chrome\n"
                       "2. Nhấn F12 mở Developer Tools → Tab Application\n"
                       "3. Chọn Cookies > https://www.facebook.com\n"
                       "4. Sao chép giá trị của: c_user, xs, fr\n"
                       "5. Dán cookie theo định dạng: c_user=...; xs=...; fr=...\n\n"
                       "Mỗi tài khoản là một cookie khác nhau. Khi một tài khoản bị chặn, "
                       "chương trình tự động xoay sang tài khoản kế tiếp (nếu bật Xoay tài khoản).\n\n"
                       "Che IP: mỗi tài khoản có thể gắn riêng một proxy (http/https/socks4/socks5).\n"
                       "Ví dụ: http://user:pass@host:port hoặc socks5://host:port. Để trống = không dùng proxy."),
        this);
    instructions->setWordWrap(true);
    layout->addWidget(instructions);

    m_table = new QTableWidget(0, 5, this);
    m_table->setHorizontalHeaderLabels({
        QStringLiteral("Dùng"), QStringLiteral("Tên"), QStringLiteral("Trạng thái"),
        QStringLiteral("Proxy"), QStringLiteral("Cookie")});
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(32);
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->resizeSection(0, 60);
    m_table->horizontalHeader()->resizeSection(2, 90);
    m_table->horizontalHeader()->resizeSection(3, 150);
    m_table->horizontalHeader()->resizeSection(4, 300);
    layout->addWidget(m_table, 1);

    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(8);
    auto *btnAdd = new QPushButton(QStringLiteral("+ Thêm tài khoản"), this);
    auto *btnEdit = new QPushButton(QStringLiteral("Sửa"), this);
    auto *btnRemove = new QPushButton(QStringLiteral("Xóa"), this);
    btnRemove->setObjectName(QStringLiteral("dangerButton"));
    auto *btnAddMany = new QPushButton(QStringLiteral("Thêm nhanh nhiều tài khoản"), this);
    btnAddMany->setToolTip(QStringLiteral("Dán nhiều cookie, mỗi tài khoản một dòng."));
    auto *btnImport = new QPushButton(QStringLiteral("Nhập từ file..."), this);
    btnImport->setToolTip(QStringLiteral("Mỗi dòng một tài khoản (có thể có tên trước, cách nhau bằng dấu |)."));
    auto *btnImportJson = new QPushButton(QStringLiteral("Nhập từ JSON"), this);
    btnImportJson->setToolTip(QStringLiteral("Nạp danh sách tài khoản từ file JSON."));
    auto *btnExportJson = new QPushButton(QStringLiteral("Xuất JSON"), this);
    btnExportJson->setToolTip(QStringLiteral("Xuất toàn bộ tài khoản ra file JSON."));
    m_lblStatus = new QLabel(this);
    m_lblStatus->setObjectName(QStringLiteral("chip"));
    btnRow->addWidget(btnAdd);
    btnRow->addWidget(btnEdit);
    btnRow->addWidget(btnRemove);
    btnRow->addWidget(btnAddMany);
    btnRow->addWidget(btnImport);
    btnRow->addWidget(btnImportJson);
    btnRow->addWidget(btnExportJson);
    btnRow->addStretch();
    btnRow->addWidget(m_lblStatus);
    layout->addLayout(btnRow);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Close, this);
    bb->button(QDialogButtonBox::Close)->setText(QStringLiteral("Đóng"));
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::accept);
    layout->addWidget(bb);

    connect(btnAdd, &QPushButton::clicked, this, &CookieDialog::addAccount);
    connect(btnEdit, &QPushButton::clicked, this, &CookieDialog::editSelected);
    connect(btnRemove, &QPushButton::clicked, this, &CookieDialog::removeSelected);
    connect(btnAddMany, &QPushButton::clicked, this, &CookieDialog::addMany);
    connect(btnImport, &QPushButton::clicked, this, &CookieDialog::importFromFile);
    connect(btnImportJson, &QPushButton::clicked, this, &CookieDialog::importFromJson);
    connect(btnExportJson, &QPushButton::clicked, this, &CookieDialog::exportJson);

    // Đồng bộ ngay khi người dùng tick/bỏ tick cột "Dùng".
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        const int row = item->row();
        if (row >= 0 && row < m_accounts.size() && item->column() == 0)
            m_accounts[row].selected = item->checkState() == Qt::Checked;
    });

    Q_UNUSED(activeCookie);
    refreshTable();
}

QVector<FacebookAccount> CookieDialog::accounts() const
{
    return m_accounts;
}

void CookieDialog::refreshTable()
{
    m_table->setRowCount(m_accounts.size());
    int active = 0;
    for (int i = 0; i < m_accounts.size(); ++i) {
        const FacebookAccount &a = m_accounts.at(i);

        auto *check = new QTableWidgetItem();
        check->setFlags(check->flags() | Qt::ItemIsUserCheckable);
        check->setCheckState(a.selected ? Qt::Checked : Qt::Unchecked);
        check->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(i, 0, check);

        m_table->setItem(i, 1, new QTableWidgetItem(a.name));

        auto *statusItem = new QTableWidgetItem(a.status);
        statusItem->setTextAlignment(Qt::AlignCenter);
        m_table->setItem(i, 2, statusItem);

        auto *proxyItem = new QTableWidgetItem(
            a.proxy.trimmed().isEmpty() ? QStringLiteral("—") : a.proxy.trimmed());
        proxyItem->setToolTip(a.proxy.trimmed());
        m_table->setItem(i, 3, proxyItem);

        QString cookie = a.cookieRaw;
        cookie = cookie.left(80) + (cookie.size() > 80 ? QStringLiteral("…") : QString());
        auto *cookieItem = new QTableWidgetItem(cookie);
        cookieItem->setToolTip(a.cookieRaw);
        m_table->setItem(i, 4, cookieItem);

        if (!a.cookieRaw.trimmed().isEmpty())
            ++active;
    }

    // Đồng bộ lựa chọn "Dùng" trong bảng về dữ liệu.
    for (int i = 0; i < m_table->rowCount(); ++i) {
        if (auto *item = m_table->item(i, 0))
            m_accounts[i].selected = item->checkState() == Qt::Checked;
    }

    m_lblStatus->setText(QStringLiteral("%1 tài khoản").arg(m_accounts.size()));
}

bool CookieDialog::execAccountDialog(QString *name, QString *proxy, QString *cookie, bool creating)
{
    QDialog dlg(this);
    dlg.setWindowTitle(creating ? QStringLiteral("Thêm tài khoản")
                                : QStringLiteral("Sửa tài khoản"));
    dlg.resize(520, 360);

    auto *form = new QFormLayout(&dlg);
    form->setSpacing(10);
    form->setContentsMargins(16, 18, 16, 16);

    auto *txtName = new QLineEdit(*name, &dlg);
    txtName->setPlaceholderText(QStringLiteral("Tên hiển thị, ví dụ: Tài khoản 1"));
    form->addRow(QStringLiteral("Tên:"), txtName);

    auto *txtProxy = new QLineEdit(*proxy, &dlg);
    txtProxy->setPlaceholderText(
        QStringLiteral("Ví dụ: socks5://user:pass@host:port — để trống nếu không dùng"));
    txtProxy->setToolTip(QStringLiteral("Che IP cho tài khoản này. Hỗ trợ http://, https://, "
                                        "socks4://, socks5://, có thể kèm user:pass@"));
    form->addRow(QStringLiteral("Proxy (tùy chọn):"), txtProxy);

    auto *txtCookie = new QTextEdit(&dlg);
    txtCookie->setAcceptRichText(false);
    txtCookie->setPlainText(*cookie);
    txtCookie->setPlaceholderText(
        QStringLiteral("c_user=...; xs=...; fr=..."));
    form->addRow(QStringLiteral("Cookie:"), txtCookie);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
    bb->button(QDialogButtonBox::Save)->setText(QStringLiteral("Lưu"));
    bb->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Hủy"));
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);

    if (dlg.exec() != QDialog::Accepted)
        return false;

    *name = txtName->text().trimmed();
    *proxy = txtProxy->text().trimmed();
    *cookie = txtCookie->toPlainText().trimmed();
    return true;
}

void CookieDialog::addAccount()
{
    QString name;
    QString proxy;
    QString cookie;
    if (!execAccountDialog(&name, &proxy, &cookie, true))
        return;
    if (name.isEmpty() && proxy.isEmpty() && cookie.isEmpty())
        return;
    if (cookie.isEmpty())
        m_lblStatus->setText(QStringLiteral("Chưa có cookie - bổ sung sau khi sửa tài khoản"));
    if (name.isEmpty())
        name = QStringLiteral("Tài khoản ") + QString::number(m_accounts.size() + 1);

    FacebookAccount a;
    a.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    a.name = name;
    a.proxy = proxy;
    a.cookieRaw = cookie;
    a.selected = true;
    m_accounts.append(a);
    refreshTable();
}

void CookieDialog::editSelected()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_accounts.size())
        return;

    QString name = m_accounts.at(row).name;
    QString proxy = m_accounts.at(row).proxy;
    QString cookie = m_accounts.at(row).cookieRaw;
    if (!execAccountDialog(&name, &proxy, &cookie, false))
        return;
    if (name.isEmpty() && proxy.isEmpty() && cookie.isEmpty())
        return;
    if (cookie.isEmpty())
        m_lblStatus->setText(QStringLiteral("Chưa có cookie - bổ sung sau khi sửa tài khoản"));

    m_accounts[row].name = name;
    m_accounts[row].proxy = proxy;
    m_accounts[row].cookieRaw = cookie;
    refreshTable();
}

void CookieDialog::removeSelected()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_accounts.size())
        return;
    m_accounts.removeAt(row);
    refreshTable();
}

void CookieDialog::addMany()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Thêm nhanh nhiều tài khoản"));
    dlg.resize(560, 360);

    auto *lay = new QVBoxLayout(&dlg);
    lay->setSpacing(10);
    lay->addWidget(new QLabel(
        QStringLiteral("Mỗi dòng là một tài khoản. Có thể đặt tên và proxy trước, cách nhau bằng dấu |\n"
                       "Ví dụ:\n"
                       "Tài khoản A|socks5://user:pass@host:1080|c_user=100; xs=abc; fr=xyz\n"
                       "Tài khoản B|c_user=200; xs=def; fr=123\n"
                       "c_user=300; xs=ghi; fr=789\n\n"
                       "Định dạng: Tên|Proxy|Cookie  hoặc  Tên|Cookie  hoặc  chỉ Cookie."), &dlg));

    auto *txt = new QTextEdit(&dlg);
    txt->setAcceptRichText(false);
    txt->setPlaceholderText(QStringLiteral("Dán nhiều cookie vào đây..."));
    lay->addWidget(txt, 1);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dlg);
    bb->button(QDialogButtonBox::Save)->setText(QStringLiteral("Thêm tất cả"));
    bb->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Hủy"));
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(bb);

    if (dlg.exec() != QDialog::Accepted)
        return;

    const QStringList lines =
        txt->toPlainText().split(QRegularExpression(QStringLiteral("[\\n\\r]")), Qt::SkipEmptyParts);
    const int before = m_accounts.size();
    parseAndAppend(lines);
    refreshTable();
    m_lblStatus->setText(QStringLiteral("Đã thêm %1 tài khoản").arg(m_accounts.size() - before));
}

void CookieDialog::importFromFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Nhập tài khoản từ file"), QString(),
        QStringLiteral("Tệp văn bản/CSV (*.txt *.csv)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        m_lblStatus->setText(QStringLiteral("Không mở được tệp"));
        return;
    }

    QStringList lines;
    const QString data = QString::fromUtf8(file.readAll());
    lines = data.split(QRegularExpression(QStringLiteral("[\\n\\r]")), Qt::SkipEmptyParts);

    const int before = m_accounts.size();
    parseAndAppend(lines);
    refreshTable();
    m_lblStatus->setText(QStringLiteral("Đã nhập %1 tài khoản từ file").arg(m_accounts.size() - before));
}

void CookieDialog::importFromJson()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Nhập tài khoản từ JSON"),
        QDir::current().filePath(QStringLiteral("dashboard.json")),
        QStringLiteral("JSON (*.json)"));
    if (path.isEmpty())
        return;

    QVector<FacebookAccount> loaded;
    if (!DashboardStore::accountsFromJsonFile(path, &loaded)) {
        m_lblStatus->setText(QStringLiteral("Không đọc được file JSON"));
        return;
    }

    QSet<QString> existing;
    for (const FacebookAccount &a : m_accounts)
        existing.insert(a.cookieRaw.trimmed());

    int added = 0;
    for (const FacebookAccount &na : loaded) {
        if (existing.contains(na.cookieRaw.trimmed()))
            continue;
        existing.insert(na.cookieRaw.trimmed());
        FacebookAccount a = na;
        if (a.name.isEmpty())
            a.name = QStringLiteral("Tài khoản ") + QString::number(m_accounts.size() + added + 1);
        m_accounts.append(a);
        ++added;
    }
    refreshTable();
    m_lblStatus->setText(QStringLiteral("Đã thêm %1 tài khoản từ JSON").arg(added));
}

void CookieDialog::exportJson()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Xuất tài khoản ra JSON"),
        QDir::current().filePath(QStringLiteral("accounts.json")),
        QStringLiteral("JSON (*.json)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        m_lblStatus->setText(QStringLiteral("Không ghi được file"));
        return;
    }
    file.write(QJsonDocument(DashboardStore::accountsToJson(m_accounts))
                   .toJson(QJsonDocument::Indented));
    m_lblStatus->setText(QStringLiteral("Đã xuất %1 tài khoản").arg(m_accounts.size()));
}

void CookieDialog::parseAndAppend(const QStringList &lines)
{
    static const QRegularExpression cUserRe(QStringLiteral("c_user=(\\d+)"));
    QSet<QString> existing;
    for (const FacebookAccount &a : m_accounts)
        existing.insert(a.cookieRaw.trimmed());

    int added = 0;
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#')))
            continue;
        QString name;
        QString proxy;
        QString cookie = trimmed;
        const QStringList parts = trimmed.split(QLatin1Char('|'));
        if (parts.size() == 3) {
            // Tên|Proxy|Cookie
            name = parts.at(0).trimmed();
            proxy = parts.at(1).trimmed();
            cookie = parts.at(2).trimmed();
        } else if (parts.size() == 2) {
            // Tên|Cookie (proxy để trống)
            name = parts.at(0).trimmed();
            cookie = parts.at(1).trimmed();
        }
        if (cookie.isEmpty())
            continue;
        // Bỏ qua tài khoản trùng cookie đã có.
        if (existing.contains(cookie))
            continue;
        existing.insert(cookie);
        if (name.isEmpty())
            name = QStringLiteral("Tài khoản ") + QString::number(m_accounts.size() + added + 1);

        FacebookAccount a;
        const QRegularExpressionMatch m = cUserRe.match(cookie);
        a.id = m.hasMatch() ? m.captured(1) : QUuid::createUuid().toString(QUuid::WithoutBraces);
        a.name = name;
        a.proxy = proxy;
        a.cookieRaw = cookie;
        a.selected = true;
        m_accounts.append(a);
        ++added;
    }
}
