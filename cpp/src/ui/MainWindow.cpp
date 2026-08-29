#include "ui/MainWindow.h"

#include "ui/CookieDialog.h"
#include "ui/RentedDialog.h"
#include "ui/Theme.h"
#include "ui/BarChart.h"
#include "ui/DonutChart.h"
#include "store/DailyPostLog.h"
#include "store/DashboardStore.h"
#include "store/DataStore.h"
#include "store/GroupStore.h"
#include "utils/Logger.h"
#include "utils/Notifier.h"
#include "store/PostedStore.h"
#include "utils/Utils.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMetaObject>
#include <QProgressBar>
#include <QPushButton>
#include <QPainter>
#include <QPixmap>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSet>
#include <QSpinBox>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QTableWidget>
#include <QTextCursor>
#include <QTextEdit>
#include <QThread>
#include <QTime>
#include <QTimeEdit>
#include <QTimer>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_config.load();

    m_workerThread = new QThread(this);
    m_worker = new FbWorker();
    m_worker->moveToThread(m_workerThread);
    m_workerThread->start();

    connect(m_worker, &FbWorker::logMessage, this, &MainWindow::appendLog);
    connect(m_worker, &FbWorker::groupsReady, this, &MainWindow::onGroupsReady);
    connect(m_worker, &FbWorker::progressUpdated, this, &MainWindow::onProgress);
    connect(m_worker, &FbWorker::accountStatusChanged, this, &MainWindow::onAccountStatusChanged);
    connect(m_worker, &FbWorker::rentedQuotaUsed, this, &MainWindow::onRentedQuotaUsed);
    connect(m_worker, &FbWorker::postingDone, this, &MainWindow::onPostingDone);
    connect(m_worker, &FbWorker::joiningDone, this, &MainWindow::onJoiningDone);
    connect(m_worker, &FbWorker::proxyTestResult, this,
            [this](int, bool ok, const QString &proxy) {
                auto *item = new QListWidgetItem(
                    QStringLiteral("%1 %2").arg(ok ? QStringLiteral("✓") : QStringLiteral("✕")).arg(proxy));
                item->setForeground(ok ? QColor(QStringLiteral("#188038"))
                                       : QColor(QStringLiteral("#d93025")));
                if (m_proxyTestResults)
                    m_proxyTestResults->insertItem(0, item);
            });
    connect(&Logger::instance(), &Logger::messageLogged, this, &MainWindow::appendLog);

    buildUi();
    updateCookieStatusLabel();
    loadGroupsFile();
    loadRentedFile();
    refreshDashboard();

    // Nạp các cấu hình mới (lịch nhiều mốc giờ, proxy pool, thông báo, sao lưu).
    if (m_timeList) {
        m_timeList->clear();
        for (const QString &t : m_config.scheduleTimes) {
            if (!t.trimmed().isEmpty())
                m_timeList->addItem(t.trimmed());
        }
        if (m_timeList->count() == 0)
            m_timeList->addItem(QStringLiteral("08:00"));
    }
    if (m_txtProxyPool)
        m_txtProxyPool->setPlainText(m_config.proxyPool.join(QLatin1Char('\n')));
    if (m_chkRotateProxy)
        m_chkRotateProxy->setChecked(m_config.rotateProxyPerPost);
    if (m_chkNotify)
        m_chkNotify->setChecked(m_config.notifyDone);
    if (m_cmbNotifyMethod)
        m_cmbNotifyMethod->setCurrentIndex(qBound(0, m_config.notifyMethod, 2));
    if (m_txtTgToken)
        m_txtTgToken->setText(m_config.telegramToken);
    if (m_txtTgChatId)
        m_txtTgChatId->setText(m_config.telegramChatId);
    if (m_txtDiscord)
        m_txtDiscord->setText(m_config.discordWebhook);
    if (m_txtJitter)
        m_txtJitter->setValue(m_config.jitterSec);
    if (m_chkAutoBackup)
        m_chkAutoBackup->setChecked(m_config.autoBackup);

    // Thông báo hệ thống (tray) khi hoàn thành.
    setupTray();

    // Sao lưu tự động mỗi giờ.
    m_backupTimer = new QTimer(this);
    m_backupTimer->setInterval(3600000);
    connect(m_backupTimer, &QTimer::timeout, this, &MainWindow::doBackup);
    if (m_config.autoBackup)
        m_backupTimer->start();
    if (m_chkAutoBackup) {
        connect(m_chkAutoBackup, &QCheckBox::toggled, this, [this](bool on) {
            if (on)
                m_backupTimer->start();
            else
                m_backupTimer->stop();
        });
    }

    // Tự động lưu toàn bộ dữ liệu ra dashboard.json định kỳ.
    m_autoSaveTimer = new QTimer(this);
    m_autoSaveTimer->setInterval(30000);
    connect(m_autoSaveTimer, &QTimer::timeout, this, &MainWindow::autoSaveDashboard);
    m_autoSaveTimer->start();
    autoSaveDashboard();
}

MainWindow::~MainWindow()
{
    saveGroupsFile();
    saveRentedFile();
    if (m_worker)
        m_worker->requestStop();
    if (m_workerThread) {
        m_workerThread->quit();
        if (!m_workerThread->wait(6000))
            m_workerThread->terminate();
        delete m_worker;
        m_worker = nullptr;
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    saveConfig();
    saveGroupsFile();
    saveRentedFile();
    autoSaveDashboard();
    if (m_worker)
        m_worker->requestStop();
    event->accept();
}

void MainWindow::saveConfig()
{
    m_config.delaySec = m_txtDelay ? m_txtDelay->value() : m_config.delaySec;
    m_config.delayType = m_cmbDelayType ? m_cmbDelayType->currentText() : m_config.delayType;
    m_config.retryCount = m_txtRetryCount ? m_txtRetryCount->value() : m_config.retryCount;
    m_config.threadCount = m_txtThreadCount ? m_txtThreadCount->value() : m_config.threadCount;
    m_config.tabCount = m_txtTabCount ? m_txtTabCount->value() : m_config.tabCount;
    m_config.dailyPostLimit = m_txtDailyLimit ? m_txtDailyLimit->value() : m_config.dailyPostLimit;
    m_config.joinDelaySec = m_txtJoinDelay ? m_txtJoinDelay->value() : m_config.joinDelaySec;
    m_config.maxGroups = m_txtMaxGroups ? m_txtMaxGroups->value() : m_config.maxGroups;
    m_config.joinAction = m_cmbJoinAction ? m_cmbJoinAction->currentText() : m_config.joinAction;
    m_config.randomDelay = m_chkRandomDelay && m_chkRandomDelay->isChecked();
    m_config.skipPostedToday = m_chkSkipPosted && m_chkSkipPosted->isChecked();
    m_config.scheduleEnabled = m_chkSchedule && m_chkSchedule->isChecked();
    m_config.scheduleTime = m_timeSchedule ? m_timeSchedule->time().toString(QStringLiteral("HH:mm"))
                                           : m_config.scheduleTime;
    m_config.scheduleTimes.clear();
    if (m_timeList) {
        for (int i = 0; i < m_timeList->count(); ++i) {
            const QString t = m_timeList->item(i)->text().trimmed();
            if (!t.isEmpty())
                m_config.scheduleTimes.append(t);
        }
    }
    if (m_config.scheduleTimes.isEmpty())
        m_config.scheduleTimes << m_config.scheduleTime;
    m_config.proxyPool.clear();
    if (m_txtProxyPool) {
        const QStringList lines =
            m_txtProxyPool->toPlainText().split(QRegularExpression(QStringLiteral("[\\n\\r]")),
                                                 Qt::SkipEmptyParts);
        for (const QString &l : lines) {
            const QString t = l.trimmed();
            if (!t.isEmpty())
                m_config.proxyPool.append(t);
        }
    }
    m_config.rotateProxyPerPost = m_chkRotateProxy && m_chkRotateProxy->isChecked();
    m_config.notifyDone = m_chkNotify && m_chkNotify->isChecked();
    m_config.notifyMethod = m_cmbNotifyMethod ? m_cmbNotifyMethod->currentIndex() : m_config.notifyMethod;
    m_config.telegramToken = m_txtTgToken ? m_txtTgToken->text().trimmed() : m_config.telegramToken;
    m_config.telegramChatId = m_txtTgChatId ? m_txtTgChatId->text().trimmed() : m_config.telegramChatId;
    m_config.discordWebhook = m_txtDiscord ? m_txtDiscord->text().trimmed() : m_config.discordWebhook;
    m_config.jitterSec = m_txtJitter ? m_txtJitter->value() : m_config.jitterSec;
    m_config.autoBackup = m_chkAutoBackup && m_chkAutoBackup->isChecked();
    m_config.rotateAccounts = m_chkRotateAccounts && m_chkRotateAccounts->isChecked();
    m_config.rotateFailThreshold = m_txtRotateFail ? m_txtRotateFail->value() : m_config.rotateFailThreshold;
    m_config.headless = m_chkHeadless && m_chkHeadless->isChecked();
    m_config.saveSession = m_chkSaveSession && m_chkSaveSession->isChecked();
    m_config.autoJoin = m_chkAutoJoin && m_chkAutoJoin->isChecked();
    m_config.skipPrivate = m_chkSkipPrivate && m_chkSkipPrivate->isChecked();
    m_config.skipPending = m_chkSkipPending && m_chkSkipPending->isChecked();
    m_config.save();
    autoSaveDashboard();
}

// ============================ GIAO DIỆN ============================

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("Facebook Auto Poster"));
    resize(1400, 880);
    setStyleSheet(Theme::qss());

    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(12);

    // ----- Header -----
    auto *header = new QFrame(central);
    header->setObjectName(QStringLiteral("headerFrame"));
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(18, 12, 18, 12);
    headerLayout->setSpacing(16);

    auto *logo = new QLabel(QStringLiteral("G"), header);
    logo->setObjectName(QStringLiteral("appLogo"));
    logo->setFixedSize(40, 40);
    logo->setAlignment(Qt::AlignCenter);
    headerLayout->addWidget(logo);

    auto *titleBox = new QVBoxLayout();
    titleBox->setSpacing(2);
    auto *lblTitle = new QLabel(QStringLiteral("Facebook Auto Poster"), header);
    lblTitle->setObjectName(QStringLiteral("appTitle"));
    auto *lblSubtitle = new QLabel(QStringLiteral("Tự động đăng bài · quản lý nhóm · C++/CDP"), header);
    lblSubtitle->setObjectName(QStringLiteral("appSubtitle"));
    titleBox->addWidget(lblTitle);
    titleBox->addWidget(lblSubtitle);

    auto *lblAuthor = new QLabel(QStringLiteral("Nguyễn Tấn Tài"), header);
    lblAuthor->setObjectName(QStringLiteral("appAuthor"));

    headerLayout->addLayout(titleBox);
    headerLayout->addStretch();
    headerLayout->addWidget(lblAuthor);
    root->addWidget(header);

    // ----- Body: sidebar + nội dung -----
    auto *bodyRow = new QHBoxLayout();
    bodyRow->setSpacing(12);

    auto *sidebar = new QFrame(central);
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(224);
    auto *sideLay = new QVBoxLayout(sidebar);
    sideLay->setContentsMargins(10, 12, 10, 12);
    sideLay->setSpacing(8);

    auto *brand = new QLabel(QStringLiteral("AUTOPOST"), sidebar);
    brand->setObjectName(QStringLiteral("sideBrand"));
    sideLay->addWidget(brand);

    m_nav = new QListWidget(sidebar);
    m_nav->setObjectName(QStringLiteral("navList"));
    m_nav->addItem(QStringLiteral("◈  Tổng quan"));
    m_nav->addItem(QStringLiteral("✎  Đăng bài"));
    m_nav->addItem(QStringLiteral("▦  Quản lý nhóm"));
    m_nav->addItem(QStringLiteral("⇄  Tự động tham gia"));
    m_nav->addItem(QStringLiteral("☰  Tài khoản thuê"));
    m_nav->addItem(QStringLiteral("☂  Proxy"));
    m_nav->addItem(QStringLiteral("☀  Nuôi acc"));
    m_nav->addItem(QStringLiteral("⚙  Cài đặt"));
    sideLay->addWidget(m_nav, 1);

    auto *ver = new QLabel(QStringLiteral("Facebook Auto Poster · v1.0"), sidebar);
    ver->setObjectName(QStringLiteral("sideVersion"));
    sideLay->addWidget(ver);

    auto *rightCol = new QVBoxLayout();
    rightCol->setSpacing(12);

    m_pages = new QStackedWidget(central);
    m_pages->addWidget(buildDashboardTab());
    m_pages->addWidget(buildPostTab());
    m_pages->addWidget(buildGroupsTab());
    m_pages->addWidget(buildJoinTab());
    m_pages->addWidget(buildRentedTab());
    m_pages->addWidget(buildProxyTab());
    m_pages->addWidget(buildNurtureTab());
    m_pages->addWidget(buildSettingsTab());
    rightCol->addWidget(m_pages, 1);

    // ----- Nhật ký -----
    rightCol->addWidget(buildLogPanel());

    // ----- Thanh điều khiển -----
    rightCol->addWidget(buildControlPanel());

    bodyRow->addWidget(sidebar);
    bodyRow->addLayout(rightCol, 1);
    root->addLayout(bodyRow, 1);

    connect(m_nav, &QListWidget::currentRowChanged, m_pages, &QStackedWidget::setCurrentIndex);
    connect(m_nav, &QListWidget::currentRowChanged, this, [this](int index) {
        if (index == 0)
            refreshDashboard();
    });
    m_nav->setCurrentRow(0);

    // Gom log thành từng đợt nhỏ: khi đăng bài, log dồn dập sẽ làm UI bận layout
    // QTextEdit liên tục → rớt phím gõ ("nuốt chữ"). Flush theo lô giữ UI nhẹ.
    m_logFlushTimer = new QTimer(this);
    m_logFlushTimer->setInterval(120);
    connect(m_logFlushTimer, &QTimer::timeout, this, &MainWindow::flushLog);

    setCentralWidget(central);

    // Nạp cấu hình vào giao diện
    m_txtDelay->setValue(m_config.delaySec);
    m_cmbDelayType->setCurrentText(m_config.delayType);
    m_txtRetryCount->setValue(m_config.retryCount);
    m_txtThreadCount->setValue(m_config.threadCount);
    m_txtTabCount->setValue(m_config.tabCount);
    m_txtDailyLimit->setValue(m_config.dailyPostLimit);
    m_txtJoinDelay->setValue(m_config.joinDelaySec);
    m_txtMaxGroups->setValue(m_config.maxGroups);
    m_cmbJoinAction->setCurrentText(m_config.joinAction);
    m_chkRandomDelay->setChecked(m_config.randomDelay);
    m_chkSkipPosted->setChecked(m_config.skipPostedToday);
    m_chkSchedule->setChecked(m_config.scheduleEnabled);
    m_timeSchedule->setTime(QTime::fromString(m_config.scheduleTime, QStringLiteral("HH:mm")));
    m_chkRotateAccounts->setChecked(m_config.rotateAccounts);
    m_txtRotateFail->setValue(m_config.rotateFailThreshold);
    m_chkHeadless->setChecked(m_config.headless);
    m_chkSaveSession->setChecked(m_config.saveSession);
    m_chkAutoJoin->setChecked(m_config.autoJoin);
    m_chkSkipPrivate->setChecked(m_config.skipPrivate);
    m_chkSkipPending->setChecked(m_config.skipPending);
}

QWidget *MainWindow::buildDashboardTab()
{
    auto *panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("pagePanel"));
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(14);

    // ----- Hero banner (điểm nhấn đầu trang) -----
    layout->addWidget(buildHeroBanner());

    // ----- Tiêu đề trang + hành động (Google Workspace style) -----
    auto *headRow = new QHBoxLayout();
    auto *titleBox = new QVBoxLayout();
    titleBox->setSpacing(2);
    auto *pageTitle = new QLabel(QStringLiteral("Tổng quan"), panel);
    pageTitle->setObjectName(QStringLiteral("pageTitle"));
    auto *pageSub = new QLabel(
        QStringLiteral("Hiệu suất đăng bài và tài khoản hôm nay · ")
            + QDate::currentDate().toString(QStringLiteral("dd/MM/yyyy")),
        panel);
    pageSub->setObjectName(QStringLiteral("pageSubtitle"));
    titleBox->addWidget(pageTitle);
    titleBox->addWidget(pageSub);
    headRow->addLayout(titleBox);
    headRow->addStretch();

    auto *chipOnline = new QLabel(QStringLiteral("● Hoạt động"), panel);
    chipOnline->setObjectName(QStringLiteral("chipOnline"));
    headRow->addWidget(chipOnline);

    auto *btnQuickPost = new QPushButton(QStringLiteral("＋ Bài mới"), panel);
    btnQuickPost->setObjectName(QStringLiteral("outlinedButton"));
    btnQuickPost->setToolTip(QStringLiteral("Đi tới trang soạn bài đăng"));
    auto *btnQuickGroups = new QPushButton(QStringLiteral("＋ Nhóm"), panel);
    btnQuickGroups->setObjectName(QStringLiteral("outlinedButton"));
    btnQuickGroups->setToolTip(QStringLiteral("Đi tới trang quản lý nhóm"));
    auto *btnSave = new QPushButton(QStringLiteral("Lưu JSON"), panel);
    btnSave->setObjectName(QStringLiteral("outlinedButton"));
    btnSave->setToolTip(QStringLiteral("Gộp toàn bộ dữ liệu (nhóm, tài khoản, nhật ký) vào một file JSON."));
    auto *btnLoad = new QPushButton(QStringLiteral("Tải JSON"), panel);
    btnLoad->setObjectName(QStringLiteral("outlinedButton"));
    btnLoad->setToolTip(QStringLiteral("Nạp lại nhóm và tài khoản từ file JSON đã lưu."));
    auto *btnRefresh = new QPushButton(QStringLiteral("⟳  Làm mới"), panel);
    btnRefresh->setObjectName(QStringLiteral("primaryButton"));
    headRow->addWidget(btnQuickPost);
    headRow->addWidget(btnQuickGroups);
    headRow->addWidget(btnSave);
    headRow->addWidget(btnLoad);
    headRow->addWidget(btnRefresh);
    layout->addLayout(headRow);

    connect(btnSave, &QPushButton::clicked, this, &MainWindow::saveDashboardJson);
    connect(btnLoad, &QPushButton::clicked, this, &MainWindow::loadDashboardJson);
    connect(btnRefresh, &QPushButton::clicked, this, &MainWindow::refreshDashboard);
    connect(btnQuickPost, &QPushButton::clicked, this, [this] { m_nav->setCurrentRow(1); });
    connect(btnQuickGroups, &QPushButton::clicked, this, [this] { m_nav->setCurrentRow(2); });

    // ----- KPI cards (Google Analytics style) -----
    auto *cardRow = new QHBoxLayout();
    cardRow->setSpacing(14);

    auto addCard = [this, cardRow](const QString &icon, const QString &tintStyle,
                                   const QString &title, const QString &caption,
                                   const QString &valueName) {
        auto *card = new QFrame();
        card->setObjectName(QStringLiteral("statCard"));
        auto *cardLay = new QVBoxLayout(card);
        cardLay->setContentsMargins(18, 16, 18, 16);
        cardLay->setSpacing(6);

        auto *topRow = new QHBoxLayout();
        topRow->setSpacing(10);
        auto *iconLbl = new QLabel(icon);
        iconLbl->setObjectName(QStringLiteral("cardIcon"));
        iconLbl->setFixedSize(40, 40);
        iconLbl->setAlignment(Qt::AlignCenter);
        iconLbl->setStyleSheet(tintStyle);
        topRow->addWidget(iconLbl);
        auto *lbl = new QLabel(title);
        lbl->setObjectName(QStringLiteral("cardLabel"));
        lbl->setWordWrap(true);
        topRow->addWidget(lbl, 1);
        cardLay->addLayout(topRow);

        auto *val = new QLabel(QStringLiteral("0"));
        val->setObjectName(valueName);
        val->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        cardLay->addWidget(val);

        auto *cap = new QLabel(caption);
        cap->setObjectName(QStringLiteral("cardCaption"));
        cap->setWordWrap(true);
        cardLay->addWidget(cap);
        m_dashCaptions.append(cap);

        cardRow->addWidget(card, 1);
        return val;
    };

    m_dashPosts = addCard(QStringLiteral("P"), QStringLiteral(
        "background: #e8f0fe; color: #1a73e8; border-radius: 20px; font-weight: 700; font-size: 15px;"),
        QStringLiteral("Bài đăng hôm nay"), QStringLiteral("Tổng lượt thử đăng"),
        QStringLiteral("cardValue"));
    m_dashSuccess = addCard(QStringLiteral("✓"), QStringLiteral(
        "background: #e6f4ea; color: #188038; border-radius: 20px; font-weight: 700; font-size: 15px;"),
        QStringLiteral("Thành công"), QStringLiteral("Tỷ lệ thành công theo dữ liệu hôm nay"),
        QStringLiteral("cardValueGreen"));
    m_dashFailed = addCard(QStringLiteral("✕"), QStringLiteral(
        "background: #fce8e6; color: #d93025; border-radius: 20px; font-weight: 700; font-size: 15px;"),
        QStringLiteral("Thất bại"), QStringLiteral("Cần kiểm tra proxy / cookie"),
        QStringLiteral("cardValueRed"));
    m_dashAccounts = addCard(QStringLiteral("A"), QStringLiteral(
        "background: #fef7e0; color: #f9ab00; border-radius: 20px; font-weight: 700; font-size: 15px;"),
        QStringLiteral("Tài khoản"), QStringLiteral("Hoạt động / tổng số"),
        QStringLiteral("cardValue"));
    m_dashGroups = addCard(QStringLiteral("G"), QStringLiteral(
        "background: #f3e8fd; color: #9334e6; border-radius: 20px; font-weight: 700; font-size: 15px;"),
        QStringLiteral("Nhóm đã chọn"), QStringLiteral("Đã chọn / tổng số nhóm"),
        QStringLiteral("cardValue"));
    layout->addLayout(cardRow);

    // ----- Biểu đồ 7 ngày -----
    auto *chartCard = new QGroupBox(QStringLiteral("Hoạt động 7 ngày gần nhất"), panel);
    chartCard->setObjectName(QStringLiteral("panelCard"));
    auto *chartLay = new QVBoxLayout(chartCard);
    chartLay->setSpacing(8);

    auto *chartHead = new QHBoxLayout();
    auto *legendOk = new QLabel(QStringLiteral("● Thành công"), chartCard);
    legendOk->setObjectName(QStringLiteral("legendOk"));
    auto *legendFail = new QLabel(QStringLiteral("● Thất bại"), chartCard);
    legendFail->setObjectName(QStringLiteral("legendFail"));
    chartHead->addWidget(legendOk);
    chartHead->addWidget(legendFail);
    chartHead->addStretch();
    chartLay->addLayout(chartHead);

    m_dashChart = new BarChart(chartCard);
    chartLay->addWidget(m_dashChart);
    layout->addWidget(chartCard);

    // ----- Bảng tài khoản + hoạt động gần đây -----
    auto *midRow = new QHBoxLayout();
    midRow->setSpacing(14);

    auto *accBox = new QGroupBox(QStringLiteral("Tài khoản hôm nay"), panel);
    accBox->setObjectName(QStringLiteral("panelCard"));
    auto *accLay = new QVBoxLayout(accBox);
    accLay->setSpacing(6);
    m_dashAccountTable = new QTableWidget(0, 5, accBox);
    m_dashAccountTable->setHorizontalHeaderLabels({
        QStringLiteral("Tài khoản"), QStringLiteral("Trạng thái"),
        QStringLiteral("Đã đăng"), QStringLiteral("Hạn mức/ngày"),
        QStringLiteral("Tiến độ")});
    m_dashAccountTable->verticalHeader()->setVisible(false);
    m_dashAccountTable->verticalHeader()->setDefaultSectionSize(36);
    m_dashAccountTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_dashAccountTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_dashAccountTable->setShowGrid(false);
    m_dashAccountTable->setObjectName(QStringLiteral("dataTable"));
    m_dashAccountTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_dashAccountTable->horizontalHeader()->resizeSection(1, 110);
    m_dashAccountTable->horizontalHeader()->resizeSection(2, 90);
    m_dashAccountTable->horizontalHeader()->resizeSection(3, 110);
    m_dashAccountTable->horizontalHeader()->resizeSection(4, 170);
    accLay->addWidget(m_dashAccountTable);
    midRow->addWidget(accBox, 3);

    auto *recentBox = new QGroupBox(QStringLiteral("Hoạt động gần đây"), panel);
    recentBox->setObjectName(QStringLiteral("panelCard"));
    auto *recentLay = new QVBoxLayout(recentBox);
    recentLay->setSpacing(6);
    m_dashRecent = new QListWidget(recentBox);
    m_dashRecent->setObjectName(QStringLiteral("recentList"));
    m_dashRecent->setMinimumWidth(340);
    recentLay->addWidget(m_dashRecent);
    midRow->addWidget(recentBox, 2);
    layout->addLayout(midRow, 1);

    // ----- Lịch sử theo ngày -----
    auto *histBox = new QGroupBox(QStringLiteral("Lịch sử theo ngày"), panel);
    histBox->setObjectName(QStringLiteral("panelCard"));
    auto *histLay = new QVBoxLayout(histBox);
    histLay->setSpacing(6);
    m_dashHistoryTable = new QTableWidget(0, 5, histBox);
    m_dashHistoryTable->setHorizontalHeaderLabels({
        QStringLiteral("Ngày"), QStringLiteral("Tổng"),
        QStringLiteral("Thành công"), QStringLiteral("Thất bại"),
        QStringLiteral("Tỉ lệ")});
    m_dashHistoryTable->verticalHeader()->setVisible(false);
    m_dashHistoryTable->verticalHeader()->setDefaultSectionSize(32);
    m_dashHistoryTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_dashHistoryTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_dashHistoryTable->setShowGrid(false);
    m_dashHistoryTable->setObjectName(QStringLiteral("dataTable"));
    m_dashHistoryTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_dashHistoryTable->horizontalHeader()->resizeSection(1, 70);
    m_dashHistoryTable->horizontalHeader()->resizeSection(2, 90);
    m_dashHistoryTable->horizontalHeader()->resizeSection(3, 90);
    m_dashHistoryTable->horizontalHeader()->resizeSection(4, 70);
    histLay->addWidget(m_dashHistoryTable);
    layout->addWidget(histBox);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(panel);
    return scroll;
}

QWidget *MainWindow::buildHeroBanner()
{
    auto *hero = new QFrame(this);
    hero->setObjectName(QStringLiteral("heroBanner"));
    auto *heroLay = new QHBoxLayout(hero);
    heroLay->setContentsMargins(26, 20, 20, 20);
    heroLay->setSpacing(24);

    auto *leftBox = new QVBoxLayout();
    leftBox->setSpacing(2);
    auto *kicker = new QLabel(QStringLiteral("HÔM NAY · TỔNG QUAN"), hero);
    kicker->setObjectName(QStringLiteral("heroKicker"));
    leftBox->addWidget(kicker);
    m_heroTotal = new QLabel(QStringLiteral("0"), hero);
    m_heroTotal->setObjectName(QStringLiteral("heroValue"));
    m_heroTotal->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    leftBox->addWidget(m_heroTotal);
    m_heroTrend = new QLabel(QStringLiteral("Chưa có dữ liệu hôm qua"), hero);
    m_heroTrend->setObjectName(QStringLiteral("heroCaption"));
    leftBox->addWidget(m_heroTrend);
    heroLay->addLayout(leftBox);

    heroLay->addStretch();

    auto *midBox = new QVBoxLayout();
    midBox->setSpacing(6);
    auto *barLbl = new QLabel(QStringLiteral("TỶ LỆ THÀNH CÔNG HÔM NAY"), hero);
    barLbl->setObjectName(QStringLiteral("heroKicker"));
    midBox->addWidget(barLbl);
    m_heroBar = new QProgressBar(hero);
    m_heroBar->setObjectName(QStringLiteral("heroProgress"));
    m_heroBar->setRange(0, 100);
    m_heroBar->setFixedWidth(200);
    midBox->addWidget(m_heroBar);
    m_heroRate = new QLabel(QStringLiteral("0%"), hero);
    m_heroRate->setObjectName(QStringLiteral("heroRate"));
    midBox->addWidget(m_heroRate);
    heroLay->addLayout(midBox);

    auto *rightBox = new QVBoxLayout();
    rightBox->setSpacing(10);
    m_dashDonut = new DonutChart(hero);
    rightBox->addWidget(m_dashDonut, 0, Qt::AlignRight);
    auto *btnHeroStart = new QPushButton(QStringLiteral("＋  Bắt đầu đăng bài"), hero);
    btnHeroStart->setObjectName(QStringLiteral("heroButton"));
    btnHeroStart->setToolTip(QStringLiteral("Đi tới trang soạn bài đăng để bắt đầu."));
    rightBox->addWidget(btnHeroStart, 0, Qt::AlignRight);
    heroLay->addLayout(rightBox);

    connect(btnHeroStart, &QPushButton::clicked, this, [this] { m_nav->setCurrentRow(1); });
    return hero;
}

QWidget *MainWindow::buildPostTab()
{
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(12);

    auto *card = new QGroupBox(QStringLiteral("Nội dung bài đăng"), panel);
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setSpacing(10);

    m_txtContent = new QTextEdit(card);
    m_txtContent->setAcceptRichText(false);
    m_txtContent->setPlaceholderText(QStringLiteral("Nhập nội dung bài đăng...\n\n"
        "Xoay vòng nội dung: ngăn cách nhiều bài bằng một dòng chỉ chứa --- (3 dấu gạch).\n"
        "Biến tự động: {{ten}}, {{group}}/{{nhom}}, {{ngay}}, {{gio}}, {{thang}}, {{nam}}"));
    m_txtContent->setMinimumHeight(240);
    cardLayout->addWidget(m_txtContent);

    auto *imgRow = new QHBoxLayout();
    imgRow->setSpacing(10);
    m_btnImg = new QPushButton(QStringLiteral("+ Chọn ảnh"), card);
    m_lblImageCount = new QLabel(QStringLiteral("Chưa có ảnh"), card);
    m_lblImageCount->setObjectName(QStringLiteral("chip"));
    imgRow->addWidget(m_btnImg);
    imgRow->addWidget(m_lblImageCount);
    imgRow->addStretch();
    cardLayout->addLayout(imgRow);

    auto *optRow = new QHBoxLayout();
    optRow->setSpacing(14);
    m_chkInterleave =
        new QCheckBox(QStringLiteral("Rải chéo: đan xen tài khoản - nhóm"), card);
    m_chkInterleave->setChecked(true);
    m_chkInterleave->setToolTip(
        QStringLiteral("Sắp lại thứ tự nhóm theo vòng tròn trước khi chia luồng: "
                       "nhóm liên tiếp do các tài khoản khác nhau đăng "
                       "(acc A → nhóm 1, acc B → nhóm 2, acc A → nhóm 3...), "
                       "không để một acc đăng dồn nhiều nhóm liền nhau."));
    optRow->addWidget(m_chkInterleave);
    optRow->addStretch();
    cardLayout->addLayout(optRow);
    layout->addWidget(card, 1);

    auto *hint = new QLabel(
        QStringLiteral("Mẹo: tối đa 5 ảnh kèm bài. Mỗi dòng --- chia ra một nội dung riêng, "
                       "chương trình tự chọn ngẫu nhiên cho từng nhóm. "
                       "{{group}} sẽ thay bằng tên nhóm, {{ten}} bằng tên tài khoản."),
        panel);
    hint->setStyleSheet(QStringLiteral("color: #5f6368; font-size: 12px;"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    connect(m_btnImg, &QPushButton::clicked, this, &MainWindow::chooseImages);
    return panel;
}

QWidget *MainWindow::buildGroupsTab()
{
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(12);

    auto *toolRow = new QHBoxLayout();
    toolRow->setSpacing(8);
    m_btnFetchGroups = new QPushButton(QStringLiteral("Lấy nhóm của tôi"), panel);
    auto *btnImport = new QPushButton(QStringLiteral("Nhập nhóm"), panel);
    auto *btnExport = new QPushButton(QStringLiteral("Xuất nhóm"), panel);
    m_btnAddGroupsById = new QPushButton(QStringLiteral("+ Thêm bằng ID"), panel);
    auto *btnSelectAll = new QPushButton(QStringLiteral("Chọn tất cả"), panel);
    auto *btnDeselectAll = new QPushButton(QStringLiteral("Bỏ chọn"), panel);
    m_lblGroupStats = new QLabel(panel);
    m_lblGroupStats->setObjectName(QStringLiteral("chipGreen"));

    auto *filterLbl = new QLabel(QStringLiteral("Lọc:"), panel);
    m_cmbPrivacyFilter = new QComboBox(panel);
    m_cmbPrivacyFilter->addItems({QStringLiteral("Tất cả"), QStringLiteral("công khai"),
                                  QStringLiteral("riêng tư"), QStringLiteral("chia sẻ"),
                                  QStringLiteral("ẩn"), QStringLiteral("chưa rõ")});

    toolRow->addWidget(m_btnFetchGroups);
    toolRow->addWidget(btnImport);
    toolRow->addWidget(btnExport);
    toolRow->addWidget(m_btnAddGroupsById);
    toolRow->addStretch();
    toolRow->addWidget(filterLbl);
    toolRow->addWidget(m_cmbPrivacyFilter);
    toolRow->addWidget(btnSelectAll);
    toolRow->addWidget(btnDeselectAll);
    toolRow->addWidget(m_lblGroupStats);
    layout->addLayout(toolRow);

    m_groupTable = new QTableWidget(0, 6, panel);
    m_groupTable->setHorizontalHeaderLabels({
        QStringLiteral("Đăng"), QStringLiteral("Tên nhóm"), QStringLiteral("ID"),
        QStringLiteral("Quyền riêng tư"), QStringLiteral("Thành viên"), QStringLiteral("Trạng thái")});
    m_groupTable->verticalHeader()->setVisible(false);
    m_groupTable->verticalHeader()->setDefaultSectionSize(32);
    m_groupTable->setAlternatingRowColors(true);
    m_groupTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_groupTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_groupTable->setShowGrid(false);
    m_groupTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_groupTable->horizontalHeader()->resizeSection(0, 60);
    m_groupTable->horizontalHeader()->resizeSection(2, 110);
    m_groupTable->horizontalHeader()->resizeSection(3, 110);
    m_groupTable->horizontalHeader()->resizeSection(4, 90);
    m_groupTable->horizontalHeader()->resizeSection(5, 90);
    layout->addWidget(m_groupTable, 1);

    connect(m_btnFetchGroups, &QPushButton::clicked, this, &MainWindow::fetchMyGroups);
    connect(btnImport, &QPushButton::clicked, this, &MainWindow::importGroups);
    connect(btnExport, &QPushButton::clicked, this, &MainWindow::exportGroups);
    connect(m_btnAddGroupsById, &QPushButton::clicked, this, &MainWindow::addGroupsById);
    connect(m_cmbPrivacyFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) { applyPrivacyFilter(); });
    connect(btnSelectAll, &QPushButton::clicked, this, &MainWindow::selectAllGroups);
    connect(btnDeselectAll, &QPushButton::clicked, this, &MainWindow::deselectAllGroups);
    connect(m_groupTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *) {
        updateGroupStats();
    });
    return panel;
}

QWidget *MainWindow::buildJoinTab()
{
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(12);

    // Tìm kiếm
    auto *searchCard = new QGroupBox(QStringLiteral("Tìm kiếm nhóm"), panel);
    auto *searchLayout = new QVBoxLayout(searchCard);
    searchLayout->setSpacing(10);

    m_txtSearchKeywords = new QTextEdit(searchCard);
    m_txtSearchKeywords->setAcceptRichText(false);
    m_txtSearchKeywords->setPlaceholderText(QStringLiteral("Ví dụ: công nghệ, kinh doanh, marketing (cách nhau bằng dấu phẩy)"));
    m_txtSearchKeywords->setMaximumHeight(70);
    searchLayout->addWidget(m_txtSearchKeywords);

    auto *searchRow = new QHBoxLayout();
    m_btnSearchGroups = new QPushButton(QStringLiteral("Tìm kiếm nhóm"), searchCard);
    auto *btnLoadJoinIds = new QPushButton(QStringLiteral("Nạp ID từ JSON"), searchCard);
    btnLoadJoinIds->setToolTip(QStringLiteral("Nạp danh sách ID nhóm từ file JSON vào ô bên dưới."));
    searchRow->addWidget(m_btnSearchGroups);
    searchRow->addWidget(btnLoadJoinIds);
    searchRow->addStretch();
    searchLayout->addLayout(searchRow);
    layout->addWidget(searchCard);

    // Tham gia trực tiếp theo ID (hỗ trợ nhóm riêng tư/chia sẻ)
    auto *byIdCard = new QGroupBox(QStringLiteral("Hoặc tham gia trực tiếp theo ID"), panel);
    auto *byIdLayout = new QVBoxLayout(byIdCard);
    byIdLayout->setSpacing(8);
    m_txtJoinGroupIds = new QTextEdit(byIdCard);
    m_txtJoinGroupIds->setAcceptRichText(false);
    m_txtJoinGroupIds->setPlaceholderText(
        QStringLiteral("Mỗi ID nhóm một dòng - dùng cho nhóm riêng tư/chia sẻ\nVí dụ: 123456789012345"));
    m_txtJoinGroupIds->setMaximumHeight(64);
    byIdLayout->addWidget(m_txtJoinGroupIds);
    layout->addWidget(byIdCard);

    // Cài đặt tham gia
    auto *settingsCard = new QGroupBox(QStringLiteral("Cài đặt tham gia"), panel);
    auto *form = new QFormLayout(settingsCard);
    form->setSpacing(10);
    form->setContentsMargins(14, 18, 14, 14);

    m_txtMaxGroups = new QSpinBox(settingsCard);
    m_txtMaxGroups->setRange(1, 100000);
    m_txtMaxGroups->setValue(50);
    m_txtJoinDelay = new QSpinBox(settingsCard);
    m_txtJoinDelay->setRange(0, 3600);
    m_txtJoinDelay->setValue(10);
    m_cmbJoinAction = new QComboBox(settingsCard);
    m_cmbJoinAction->addItems({QStringLiteral("Chỉ tham gia công khai"),
                               QStringLiteral("Tham gia tất cả"),
                               QStringLiteral("Gửi yêu cầu")});
    form->addRow(QStringLiteral("Số nhóm tham gia tối đa:"), m_txtMaxGroups);
    form->addRow(QStringLiteral("Thời gian chờ (giây):"), m_txtJoinDelay);
    form->addRow(QStringLiteral("Hành động tham gia:"), m_cmbJoinAction);

    m_chkAutoJoin = new QCheckBox(QStringLiteral("Tự động tham gia nhóm đã tìm kiếm"), settingsCard);
    m_chkSkipPrivate = new QCheckBox(QStringLiteral("Bỏ qua nhóm riêng tư"), settingsCard);
    m_chkSkipPrivate->setChecked(true);
    m_chkSkipPending = new QCheckBox(QStringLiteral("Bỏ qua nhóm đang chờ duyệt"), settingsCard);
    m_chkSkipPending->setChecked(true);
    form->addRow(QString(), m_chkAutoJoin);
    form->addRow(QString(), m_chkSkipPrivate);
    form->addRow(QString(), m_chkSkipPending);
    layout->addWidget(settingsCard);

    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(10);
    m_btnStopJoin = new QPushButton(QStringLiteral("Dừng"), panel);
    m_btnStopJoin->setObjectName(QStringLiteral("dangerButton"));
    m_btnStopJoin->setEnabled(false);
    m_btnStartJoin = new QPushButton(QStringLiteral("Bắt đầu tham gia"), panel);
    m_btnStartJoin->setObjectName(QStringLiteral("primaryButton"));
    m_btnStartJoin->setMinimumWidth(180);
    btnRow->addStretch();
    btnRow->addWidget(m_btnStopJoin);
    btnRow->addWidget(m_btnStartJoin);
    layout->addLayout(btnRow);
    layout->addStretch();

    connect(m_btnSearchGroups, &QPushButton::clicked, this, &MainWindow::searchGroups);
    connect(btnLoadJoinIds, &QPushButton::clicked, this, &MainWindow::loadJoinIdsJson);
    connect(m_btnStartJoin, &QPushButton::clicked, this, &MainWindow::startJoin);
    connect(m_btnStopJoin, &QPushButton::clicked, this, &MainWindow::stopJoin);

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(panel);
    return scroll;
}

QWidget *MainWindow::buildProxyTab()
{
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(12);

    // Đếm số proxy đang có trong ô danh sách.
    auto updateProxyCount = [this]() {
        if (!m_lblProxyCount)
            return;
        int n = 0;
        if (m_txtProxyPool) {
            const QStringList raw = m_txtProxyPool->toPlainText().split(
                QRegularExpression(QStringLiteral("[\\n\\r]")), Qt::SkipEmptyParts);
            for (const QString &l : raw) {
                if (!l.trimmed().isEmpty())
                    ++n;
            }
        }
        m_lblProxyCount->setText(QStringLiteral("Tổng: %1 proxy").arg(n));
    };

    // ----- Tải proxy miễn phí từ web -----
    auto *fetchBox = new QGroupBox(QStringLiteral("Tải proxy miễn phí"), panel);
    auto *fform = new QFormLayout(fetchBox);
    fform->setSpacing(10);
    fform->setContentsMargins(14, 18, 14, 14);

    m_cmbProxySource = new QComboBox(fetchBox);
    m_cmbProxySource->addItem(QStringLiteral("Tất cả nguồn (gộp)"), QStringLiteral("all"));
    m_cmbProxySource->addItem(QStringLiteral("proxyscrape.com"), QStringLiteral("proxyscrape"));
    m_cmbProxySource->addItem(QStringLiteral("geonode.com"), QStringLiteral("geonode"));
    m_cmbProxySource->addItem(QStringLiteral("proxy-list.download"),
                              QStringLiteral("proxy-list.download"));
    m_cmbProxySource->addItem(QStringLiteral("TheSpeedX (GitHub)"), QStringLiteral("thespeedx"));
    m_cmbProxySource->addItem(QStringLiteral("clarketm (GitHub)"), QStringLiteral("clarketm"));
    m_cmbProxySource->addItem(QStringLiteral("monosans (GitHub)"), QStringLiteral("monosans"));
    m_cmbProxySource->addItem(QStringLiteral("ShiftyTR (GitHub)"), QStringLiteral("shiftytr"));
    m_cmbProxySource->addItem(QStringLiteral("proxy4parsing (GitHub)"), QStringLiteral("proxy4parsing"));
    m_cmbProxySource->addItem(QStringLiteral("roosterkid (GitHub)"), QStringLiteral("roosterkid"));
    m_cmbProxySource->addItem(QStringLiteral("vakhov (GitHub)"), QStringLiteral("vakhov"));
    m_cmbProxySource->addItem(QStringLiteral("jetkai (GitHub)"), QStringLiteral("jetkai"));
    m_cmbProxySource->addItem(QStringLiteral("proxyspace.pro"), QStringLiteral("proxyspace"));
    m_btnFetchProxies = new QPushButton(QStringLiteral("Tải proxy miễn phí"), fetchBox);
    m_chkAutoTestFetched =
        new QCheckBox(QStringLiteral("Tự động kiểm tra (TCP) proxy vừa tải — chạy song song"), fetchBox);
    m_chkAutoTestFetched->setChecked(true);
    m_lblProxyCount = new QLabel(fetchBox);
    m_lblProxyCount->setObjectName(QStringLiteral("chip"));
    updateProxyCount();

    auto *fetchRow = new QHBoxLayout();
    fetchRow->setSpacing(8);
    fetchRow->addWidget(m_cmbProxySource, 1);
    fetchRow->addWidget(m_btnFetchProxies);
    fform->addRow(QStringLiteral("Nguồn proxy:"), fetchRow);
    fform->addRow(QString(), m_chkAutoTestFetched);
    fform->addRow(QStringLiteral("Tổng trong danh sách:"), m_lblProxyCount);
    layout->addWidget(fetchBox);

    connect(m_btnFetchProxies, &QPushButton::clicked, this, [this] {
        if (!m_worker)
            return;
        const QString source = m_cmbProxySource ? m_cmbProxySource->currentData().toString()
                                                : QStringLiteral("proxyscrape");
        m_btnFetchProxies->setText(QStringLiteral("Đang tải..."));
        QMetaObject::invokeMethod(m_worker, "fetchFreeProxies", Qt::QueuedConnection,
                                  Q_ARG(QString, source));
    });

    // ----- Danh sách proxy chung + xoay vòng -----
    auto *poolBox = new QGroupBox(QStringLiteral("Danh sách proxy chung (xoay vòng)"), panel);
    auto *pform = new QFormLayout(poolBox);
    pform->setSpacing(10);
    pform->setContentsMargins(14, 18, 14, 14);

    m_txtProxyPool = new QTextEdit(poolBox);
    m_txtProxyPool->setPlaceholderText(
        QStringLiteral("Mỗi proxy một dòng:\nhttp://user:pass@host:port\nsocks5://host:1080\nhost:8080"));
    m_txtProxyPool->setMaximumHeight(130);
    connect(m_txtProxyPool, &QTextEdit::textChanged, this,
            [this, updateProxyCount] { updateProxyCount(); });

    m_chkRotateProxy = new QCheckBox(QStringLiteral("Xoay proxy sau mỗi bài đăng"), poolBox);
    auto *lblRotateHint = new QLabel(
        QStringLiteral("Mỗi bài đăng dùng proxy kế tiếp trong danh sách (xoay vòng tròn). "
                       "Chỉ áp dụng cho tài khoản chưa gắn proxy riêng. "
                       "Nên tải và kiểm tra proxy trước khi bắt đầu đăng bài."),
        poolBox);
    lblRotateHint->setWordWrap(true);
    lblRotateHint->setStyleSheet(QStringLiteral("color: #8892b0; font-size: 11px;"));

    m_btnTestProxies = new QPushButton(QStringLiteral("Kiểm tra tất cả proxy"), poolBox);
    m_proxyTestResults = new QListWidget(poolBox);
    m_proxyTestResults->setMaximumHeight(110);

    pform->addRow(QStringLiteral("Danh sách proxy:"), m_txtProxyPool);
    pform->addRow(QString(), m_chkRotateProxy);
    pform->addRow(QString(), lblRotateHint);
    pform->addRow(QString(), m_btnTestProxies);
    pform->addRow(QStringLiteral("Kết quả kiểm tra:"), m_proxyTestResults);
    layout->addWidget(poolBox);

    connect(m_btnTestProxies, &QPushButton::clicked, this, [this] {
        if (!m_worker)
            return;
        QStringList lines;
        if (m_txtProxyPool) {
            const QStringList raw = m_txtProxyPool->toPlainText().split(
                QRegularExpression(QStringLiteral("[\\n\\r]")), Qt::SkipEmptyParts);
            for (const QString &l : raw) {
                const QString t = l.trimmed();
                if (!t.isEmpty())
                    lines.append(t);
            }
        }
        if (lines.isEmpty()) {
            m_btnTestProxies->setText(QStringLiteral("Danh sách proxy đang trống"));
            QTimer::singleShot(2500, this, [this] {
                if (m_btnTestProxies)
                    m_btnTestProxies->setText(QStringLiteral("Kiểm tra tất cả proxy"));
            });
            return;
        }
        m_btnTestProxies->setText(QStringLiteral("Đang kiểm tra %1 proxy...").arg(lines.size()));
        QMetaObject::invokeMethod(m_worker, "testProxies", Qt::QueuedConnection,
                                  Q_ARG(QStringList, lines));
    });

    // Proxy vừa tải từ web: thêm vào danh sách chung (bỏ trùng).
    connect(m_worker, &FbWorker::proxiesFetched, this,
            [this, updateProxyCount](const QStringList &proxies, const QString &source) {
                if (m_btnFetchProxies)
                    m_btnFetchProxies->setText(QStringLiteral("Tải proxy miễn phí"));
                if (!m_txtProxyPool)
                    return;
                if (proxies.isEmpty()) {
                    appendLog(QStringLiteral("Không thêm proxy nào từ %1.").arg(source));
                    return;
                }
                QStringList existing;
                const QStringList raw = m_txtProxyPool->toPlainText().split(
                    QRegularExpression(QStringLiteral("[\\n\\r]")), Qt::SkipEmptyParts);
                for (const QString &l : raw) {
                    const QString t = l.trimmed();
                    if (!t.isEmpty())
                        existing.append(t);
                }
                QSet<QString> seen(existing.cbegin(), existing.cend());
                int added = 0;
                QStringList toAppend;
                for (const QString &p : proxies) {
                    if (!seen.contains(p)) {
                        seen.insert(p);
                        toAppend.append(p);
                        ++added;
                    }
                }
                if (!toAppend.isEmpty()) {
                    QString text = m_txtProxyPool->toPlainText().trimmed();
                    if (!text.isEmpty())
                        text += QLatin1Char('\n');
                    text += toAppend.join(QLatin1Char('\n'));
                    m_txtProxyPool->setPlainText(text);
                }
                appendLog(QStringLiteral("Đã thêm %1 proxy mới từ %2 (bỏ %3 trùng).")
                              .arg(added)
                              .arg(source)
                              .arg(proxies.size() - added));
                updateProxyCount();
                // Tự động kiểm tra nhanh các proxy vừa tải (song song) nếu được bật.
                if (m_chkAutoTestFetched && m_chkAutoTestFetched->isChecked() && !toAppend.isEmpty() && m_worker) {
                    m_btnTestProxies->setText(
                        QStringLiteral("Đang kiểm tra %1 proxy vừa tải...").arg(toAppend.size()));
                    QMetaObject::invokeMethod(m_worker, "testProxies", Qt::QueuedConnection,
                                              Q_ARG(QStringList, toAppend));
                }
            });

    layout->addStretch();
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(panel);
    return scroll;
}

QWidget *MainWindow::buildNurtureTab()
{
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(12);

    // Mô tả ngắn.
    auto *desc = new QLabel(
        QStringLiteral("Nuôi acc giúp tài khoản mới trông tự nhiên: đăng nhập, lướt feed, "
                       "like bài viết và xem video ngắn — giảm nguy cơ bị Facebook chặn. "
                       "Mỗi acc dùng trình duyệt riêng (kèm proxy nếu có)."),
        panel);
    desc->setWordWrap(true);
    desc->setStyleSheet(QStringLiteral("color: #8892b0; font-size: 12px;"));
    layout->addWidget(desc);

    auto *optBox = new QGroupBox(QStringLiteral("Cài đặt phiên nuôi"), panel);
    auto *form = new QFormLayout(optBox);
    form->setSpacing(10);
    form->setContentsMargins(14, 18, 14, 14);

    m_spinNurtureLikes = new QSpinBox(optBox);
    m_spinNurtureLikes->setRange(0, 100);
    m_spinNurtureLikes->setValue(12);
    m_spinNurtureLikes->setToolTip(QStringLiteral("Tối đa số bài được like trong một phiên."));

    m_spinNurtureVideos = new QSpinBox(optBox);
    m_spinNurtureVideos->setRange(0, 30);
    m_spinNurtureVideos->setValue(3);
    m_spinNurtureVideos->setToolTip(QStringLiteral("Tối đa số video được mở/xem trong một phiên."));

    m_spinNurtureMinutes = new QSpinBox(optBox);
    m_spinNurtureMinutes->setRange(1, 60);
    m_spinNurtureMinutes->setValue(5);
    m_spinNurtureMinutes->setToolTip(QStringLiteral("Thời lượng tối đa một phiên (phút) — sau đó tự dừng."));

    form->addRow(QStringLiteral("Tối đa like / phiên:"), m_spinNurtureLikes);
    form->addRow(QStringLiteral("Tối đa video / phiên:"), m_spinNurtureVideos);
    form->addRow(QStringLiteral("Thời lượng phiên (phút):"), m_spinNurtureMinutes);
    layout->addWidget(optBox);

    // Nút điều khiển + trạng thái.
    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(10);
    m_btnStopNurture = new QPushButton(QStringLiteral("Dừng"), panel);
    m_btnStopNurture->setObjectName(QStringLiteral("dangerButton"));
    m_btnStopNurture->setEnabled(false);
    m_btnStartNurture = new QPushButton(QStringLiteral("Bắt đầu nuôi acc"), panel);
    m_btnStartNurture->setObjectName(QStringLiteral("primaryButton"));
    m_btnStartNurture->setMinimumWidth(180);
    m_lblNurtureAcc = new QLabel(panel);
    m_lblNurtureAcc->setObjectName(QStringLiteral("chip"));
    btnRow->addWidget(m_btnStartNurture);
    btnRow->addWidget(m_btnStopNurture);
    btnRow->addStretch();
    btnRow->addWidget(m_lblNurtureAcc);
    layout->addLayout(btnRow);

    m_listNurtureResults = new QListWidget(panel);
    m_listNurtureResults->setMaximumHeight(180);
    layout->addWidget(new QLabel(QStringLiteral("Kết quả phiên nuôi:"), panel));
    layout->addWidget(m_listNurtureResults);
    layout->addStretch();

    auto updateNurtureAccLabel = [this]() {
        if (!m_lblNurtureAcc)
            return;
        int n = 0;
        for (const FacebookAccount &a : m_config.accounts) {
            if (!a.cookieRaw.trimmed().isEmpty())
                ++n;
        }
        m_lblNurtureAcc->setText(QStringLiteral("Sẽ nuôi %1 acc có cookie").arg(n));
    };
    updateNurtureAccLabel();

    connect(m_btnStartNurture, &QPushButton::clicked, this, [this, updateNurtureAccLabel] {
        if (!m_worker)
            return;
        // Chỉ nuôi các acc có cookie (bỏ acc trống).
        QVector<FacebookAccount> accs;
        for (const FacebookAccount &a : m_config.accounts) {
            if (!a.cookieRaw.trimmed().isEmpty())
                accs.append(a);
        }
        if (accs.isEmpty()) {
            appendLog(QStringLiteral("Không có tài khoản nào có cookie để nuôi — hãy thêm tài khoản trước."));
            return;
        }
        NurtureRequest req;
        req.maxLikes = m_spinNurtureLikes ? m_spinNurtureLikes->value() : 12;
        req.maxVideos = m_spinNurtureVideos ? m_spinNurtureVideos->value() : 3;
        req.maxDurationSec = (m_spinNurtureMinutes ? m_spinNurtureMinutes->value() : 5) * 60;
        m_btnStartNurture->setEnabled(false);
        m_btnStopNurture->setEnabled(true);
        m_listNurtureResults->clear();
        appendLog(QStringLiteral("Bắt đầu nuôi %1 tài khoản...").arg(accs.size()));
        QMetaObject::invokeMethod(m_worker, "startNurture", Qt::QueuedConnection,
                                  Q_ARG(NurtureRequest, req),
                                  Q_ARG(bool, m_chkHeadless ? m_chkHeadless->isChecked() : false),
                                  Q_ARG(QVector<FacebookAccount>, accs));
    });
    connect(m_btnStopNurture, &QPushButton::clicked, this, [this] {
        if (m_worker)
            m_worker->requestStop();
    });

    connect(m_worker, &FbWorker::nurtureDone, this, [this](int ok, int failed) {
        if (m_btnStartNurture)
            m_btnStartNurture->setEnabled(true);
        if (m_btnStopNurture)
            m_btnStopNurture->setEnabled(false);
        if (m_listNurtureResults) {
            auto *item = new QListWidgetItem(
                QStringLiteral("✓ %1 thành công · ✕ %2 thất bại").arg(ok).arg(failed));
            item->setForeground(ok > 0 ? QColor(QStringLiteral("#188038"))
                                       : QColor(QStringLiteral("#d93025")));
            m_listNurtureResults->insertItem(0, item);
        }
        appendLog(QStringLiteral("Kết thúc nuôi acc: %1 thành công, %2 thất bại.").arg(ok).arg(failed));
    });

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(panel);
    return scroll;
}

QWidget *MainWindow::buildSettingsTab()
{
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(12);

    auto *posting = new QGroupBox(QStringLiteral("Đăng bài"), panel);
    auto *pform = new QFormLayout(posting);
    pform->setSpacing(10);
    pform->setContentsMargins(14, 18, 14, 14);

    m_txtDelay = new QSpinBox(posting);
    m_txtDelay->setRange(1, 3600);
    m_cmbDelayType = new QComboBox(posting);
    m_cmbDelayType->addItems({QStringLiteral("Giây"), QStringLiteral("Phút")});
    auto *delayRow = new QHBoxLayout();
    delayRow->setSpacing(8);
    delayRow->addWidget(m_txtDelay);
    delayRow->addWidget(m_cmbDelayType);
    pform->addRow(QStringLiteral("Thời gian chờ giữa các bài đăng:"), delayRow);

    m_txtRetryCount = new QSpinBox(posting);
    m_txtRetryCount->setRange(0, 20);
    pform->addRow(QStringLiteral("Số lần thử lại khi thất bại:"), m_txtRetryCount);

    m_txtThreadCount = new QSpinBox(posting);
    m_txtThreadCount->setRange(1, 5);
    m_txtThreadCount->setSuffix(QStringLiteral(" trình duyệt"));
    m_txtThreadCount->setToolTip(QStringLiteral("Số phiên Chrome chạy song song, mỗi phiên dùng cho một tài khoản. "
                                                "Ở chế độ tài khoản thuê, chương trình luôn chạy 1 trình duyệt riêng "
                                                "cho mỗi tài khoản thuê được chọn (bỏ qua giới hạn này)."));
    pform->addRow(QStringLiteral("Số luồng đăng song song:"), m_txtThreadCount);

    m_txtTabCount = new QSpinBox(posting);
    m_txtTabCount->setRange(1, 10);
    m_txtTabCount->setValue(3);
    m_txtTabCount->setSuffix(QStringLiteral(" tab"));
    m_txtTabCount->setToolTip(QStringLiteral("Trong một trình duyệt, mở nhiều tab để đăng cùng lúc nhiều nhóm (ví dụ 5 bài cùng lúc cho 1 tài khoản)."));
    pform->addRow(QStringLiteral("Số tab đăng song song mỗi trình duyệt:"), m_txtTabCount);

    m_btnAutoTune = new QPushButton(QStringLiteral("⚡ Dò cấu hình tự động (theo CPU & RAM)"), posting);
    m_btnAutoTune->setToolTip(QStringLiteral("Tự động tính số trình duyệt (luồng) và số tab tối ưu dựa trên số luồng CPU "
                                              "và dung lượng RAM của máy, rồi điền vào hai ô bên trên."));
    pform->addRow(QString(), m_btnAutoTune);
    connect(m_btnAutoTune, &QPushButton::clicked, this, [this] {
        int threads = 1;
        int tabs = 1;
        Utils::recommendParallelism(&threads, &tabs);
        m_txtThreadCount->setValue(threads);
        m_txtTabCount->setValue(tabs);
        const int cores = qMax(1, QThread::idealThreadCount());
        const qint64 ramMB = Utils::physicalMemoryMB();
        appendLog(QStringLiteral("Dò cấu hình: máy có %1 luồng CPU, %2 RAM → gợi ý %3 trình duyệt × %4 tab.")
                      .arg(cores)
                      .arg(ramMB > 0
                               ? QString::number(ramMB / 1024.0, 'f', 1) + QStringLiteral(" GB")
                               : QStringLiteral("không xác định"))
                      .arg(threads)
                      .arg(tabs));
    });

    m_txtDailyLimit = new QSpinBox(posting);
    m_txtDailyLimit->setRange(0, 100000);
    m_txtDailyLimit->setValue(300);
    m_txtDailyLimit->setSuffix(QStringLiteral(" bài"));
    m_txtDailyLimit->setToolTip(QStringLiteral("Đăng tối đa số bài này mỗi ngày cho TỪNG TÀI KHOẢN (tách riêng theo tên khách thuê). Đủ hạn mức của tài khoản nào là dừng tài khoản đó, các nhóm còn lại để hôm sau đăng tiếp. 0 = không giới hạn."));
    pform->addRow(QStringLiteral("Số bài tối đa mỗi ngày:"), m_txtDailyLimit);

    m_chkRandomDelay = new QCheckBox(QStringLiteral("Thêm thời gian chờ ngẫu nhiên (giảm bị phát hiện)"), posting);
    pform->addRow(QString(), m_chkRandomDelay);

    m_chkSkipPosted = new QCheckBox(QStringLiteral("Bỏ qua nhóm đã đăng thành công hôm nay (chống đăng trùng)"), posting);
    m_chkSkipPosted->setToolTip(QStringLiteral("Nhóm đã đăng thành công trong ngày sẽ không bị đăng lại ở lần chạy sau."));
    pform->addRow(QString(), m_chkSkipPosted);

    m_chkSchedule = new QCheckBox(QStringLiteral("Đăng theo lịch (chờ đến mốc giờ rồi mới bắt đầu)"), posting);
    m_timeSchedule = new QTimeEdit(posting);
    m_timeSchedule->setDisplayFormat(QStringLiteral("HH:mm"));
    m_timeSchedule->setTime(QTime::fromString(QStringLiteral("08:00"), QStringLiteral("HH:mm")));
    m_btnAddTime = new QPushButton(QStringLiteral("＋ Thêm mốc giờ"), posting);
    m_btnRemoveTime = new QPushButton(QStringLiteral("Xóa mốc đã chọn"), posting);
    m_btnRemoveTime->setObjectName(QStringLiteral("dangerButton"));
    auto *scheduleRow = new QHBoxLayout();
    scheduleRow->setSpacing(8);
    scheduleRow->addWidget(m_timeSchedule);
    scheduleRow->addWidget(m_btnAddTime);
    scheduleRow->addStretch();
    m_timeList = new QListWidget(posting);
    m_timeList->setMaximumHeight(92);
    m_timeList->setToolTip(QStringLiteral("Các mốc giờ đăng. Nếu giờ hiện tại chưa tới mốc nào, "
                                          "chương trình chờ đến mốc gần nhất; nếu đã qua hết, chờ đến mốc "
                                          "sớm nhất ngày mai."));
    auto *timeListRow = new QHBoxLayout();
    timeListRow->setSpacing(8);
    timeListRow->addWidget(m_timeList, 1);
    auto *timeBtnCol = new QVBoxLayout();
    timeBtnCol->setSpacing(6);
    timeBtnCol->addWidget(m_btnRemoveTime);
    timeBtnCol->addStretch();
    timeListRow->addLayout(timeBtnCol);
    connect(m_btnAddTime, &QPushButton::clicked, this, [this] {
        const QString t = m_timeSchedule->time().toString(QStringLiteral("HH:mm"));
        for (int i = 0; i < m_timeList->count(); ++i) {
            if (m_timeList->item(i)->text() == t) {
                m_timeList->setCurrentRow(i);
                return;
            }
        }
        m_timeList->addItem(t);
        m_timeList->setCurrentRow(m_timeList->count() - 1);
    });
    connect(m_btnRemoveTime, &QPushButton::clicked, this, [this] {
        const int row = m_timeList->currentRow();
        if (row >= 0)
            delete m_timeList->takeItem(row);
    });
    pform->addRow(m_chkSchedule, scheduleRow);
    pform->addRow(QString(), timeListRow);
    layout->addWidget(posting);

    auto *rotation = new QGroupBox(QStringLiteral("Xoay tài khoản (chống bị chặn)"), panel);
    auto *rform = new QFormLayout(rotation);
    rform->setSpacing(10);
    rform->setContentsMargins(14, 18, 14, 14);

    m_chkRotateAccounts =
        new QCheckBox(QStringLiteral("Tự động chuyển tài khoản khi bị Facebook chặn"), rotation);
    rform->addRow(QString(), m_chkRotateAccounts);

    m_txtRotateFail = new QSpinBox(rotation);
    m_txtRotateFail->setRange(1, 50);
    m_txtRotateFail->setValue(2);
    m_txtRotateFail->setToolTip(QStringLiteral("Nếu đăng thất bại nhiều lần liên tiếp, chương trình sẽ chuyển sang tài khoản khác."));
    rform->addRow(QStringLiteral("Số lần thất bại trước khi xoay tài khoản:"), m_txtRotateFail);
    layout->addWidget(rotation);

    auto *browser = new QGroupBox(QStringLiteral("Trình duyệt"), panel);
    auto *bform = new QFormLayout(browser);
    bform->setSpacing(10);
    bform->setContentsMargins(14, 18, 14, 14);
    m_chkHeadless = new QCheckBox(QStringLiteral("Chế độ không giao diện (Headless)"), browser);
    m_chkSaveSession = new QCheckBox(QStringLiteral("Lưu phiên đăng nhập và cookie"), browser);
    m_chkSaveSession->setChecked(true);
    bform->addRow(QString(), m_chkHeadless);
    bform->addRow(QString(), m_chkSaveSession);
    layout->addWidget(browser);

    auto *cookie = new QGroupBox(QStringLiteral("Cookie / Tài khoản"), panel);
    auto *crow = new QHBoxLayout(cookie);
    crow->setContentsMargins(14, 18, 14, 14);
    crow->setSpacing(12);
    m_btnCookie = new QPushButton(QStringLiteral("Quản lý tài khoản"), cookie);
    m_lblCookieStatus = new QLabel(cookie);
    m_lblCookieStatus->setObjectName(QStringLiteral("chip"));
    crow->addWidget(m_btnCookie);
    crow->addWidget(m_lblCookieStatus);
    crow->addStretch();
    layout->addWidget(cookie);
    layout->addStretch();

    connect(m_btnCookie, &QPushButton::clicked, this, &MainWindow::openCookieManager);

    auto *notifyBox = new QGroupBox(QStringLiteral("Thông báo & sao lưu"), panel);
    auto *nform = new QFormLayout(notifyBox);
    nform->setSpacing(10);
    nform->setContentsMargins(14, 18, 14, 14);
    m_chkNotify =
        new QCheckBox(QStringLiteral("Hiện thông báo hệ thống (tray) khi hoàn thành / dừng"), notifyBox);
    m_chkAutoBackup =
        new QCheckBox(QStringLiteral("Tự động sao lưu dữ liệu mỗi giờ vào thư mục backups"), notifyBox);
    nform->addRow(QString(), m_chkNotify);
    nform->addRow(QString(), m_chkAutoBackup);

    // Kênh thông báo ngoài (Telegram / Discord).
    m_cmbNotifyMethod = new QComboBox(notifyBox);
    m_cmbNotifyMethod->addItems(
        {QStringLiteral("Tắt thông báo ngoài"),
         QStringLiteral("Telegram Bot"),
         QStringLiteral("Discord Webhook")});
    nform->addRow(QStringLiteral("Kênh thông báo:"), m_cmbNotifyMethod);

    m_txtTgToken = new QLineEdit(notifyBox);
    m_txtTgToken->setPlaceholderText(QStringLiteral("Bot Token (dạng 123456:ABC-DEF...)"));
    m_txtTgChatId = new QLineEdit(notifyBox);
    m_txtTgChatId->setPlaceholderText(QStringLiteral("Chat ID (vd: 123456789)"));
    nform->addRow(QStringLiteral("Telegram Token:"), m_txtTgToken);
    nform->addRow(QStringLiteral("Telegram Chat ID:"), m_txtTgChatId);

    m_txtDiscord = new QLineEdit(notifyBox);
    m_txtDiscord->setPlaceholderText(QStringLiteral("https://discord.com/api/webhooks/..."));
    nform->addRow(QStringLiteral("Discord Webhook:"), m_txtDiscord);

    // Ẩn/hiện các trường theo kênh được chọn.
    auto updateNotifyFields = [this]() {
        const bool tg = m_cmbNotifyMethod && m_cmbNotifyMethod->currentIndex() == 1;
        const bool dc = m_cmbNotifyMethod && m_cmbNotifyMethod->currentIndex() == 2;
        if (m_txtTgToken)
            m_txtTgToken->setVisible(tg);
        if (m_txtTgChatId)
            m_txtTgChatId->setVisible(tg);
        if (m_txtDiscord)
            m_txtDiscord->setVisible(dc);
    };
    if (m_cmbNotifyMethod)
        connect(m_cmbNotifyMethod, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, updateNotifyFields);
    updateNotifyFields();

    // Jitter per-account.
    m_txtJitter = new QSpinBox(notifyBox);
    m_txtJitter->setRange(0, 600);
    m_txtJitter->setSuffix(QStringLiteral(" giây"));
    m_txtJitter->setToolTip(QStringLiteral("Thêm độ lệch ngẫu nhiên vào nhịp chờ mỗi bài, "
                                           "giúp các tài khoản không đăng cùng một giây."));
    nform->addRow(QStringLiteral("Jitter mỗi tài khoản:"), m_txtJitter);

    layout->addWidget(notifyBox);
    layout->addStretch();

    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(panel);
    return scroll;
}

QWidget *MainWindow::buildLogPanel()
{
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto *topRow = new QHBoxLayout();
    auto *lblTitle = new QLabel(QStringLiteral("NHẬT KÝ HOẠT ĐỘNG"), panel);
    lblTitle->setObjectName(QStringLiteral("logTitle"));
    m_lblCounter = new QLabel(QStringLiteral("Tiến độ: 0/0 · Thành công: 0 · Thất bại: 0"), panel);
    m_lblCounter->setObjectName(QStringLiteral("chip"));
    topRow->addWidget(lblTitle);
    topRow->addStretch();
    topRow->addWidget(m_lblCounter);
    layout->addLayout(topRow);

    m_txtLog = new QTextEdit(panel);
    m_txtLog->setObjectName(QStringLiteral("logView"));
    m_txtLog->setReadOnly(true);
    m_txtLog->setLineWrapMode(QTextEdit::NoWrap);
    m_txtLog->setMaximumHeight(170);
    layout->addWidget(m_txtLog);

    auto *row = new QHBoxLayout();
    row->setSpacing(8);
    auto *btnClear = new QPushButton(QStringLiteral("Xóa nhật ký"), panel);
    auto *btnExport = new QPushButton(QStringLiteral("Xuất nhật ký"), panel);
    row->addWidget(btnClear);
    row->addWidget(btnExport);
    row->addStretch();
    layout->addLayout(row);

    connect(btnClear, &QPushButton::clicked, this, &MainWindow::clearLog);
    connect(btnExport, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("Xuất nhật ký"),
            QStringLiteral("facebook_poster_log.txt"), QStringLiteral("Tệp văn bản (*.txt)"));
        if (path.isEmpty())
            return;
        QFile file(path);
        if (file.open(QIODevice::WriteOnly))
            file.write(m_txtLog->toPlainText().toUtf8());
    });
    return panel;
}

QWidget *MainWindow::buildControlPanel()
{
    auto *panel = new QWidget(this);
    auto *layout = new QHBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(12);

    m_progressBar = new QProgressBar(panel);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    layout->addWidget(m_progressBar, 1);

    auto *stop = new QPushButton(QStringLiteral("Dừng đăng bài"), panel);
    stop->setObjectName(QStringLiteral("dangerButton"));
    m_btnStart = new QPushButton(QStringLiteral("Bắt đầu đăng bài"), panel);
    m_btnStart->setObjectName(QStringLiteral("successButton"));
    m_btnStart->setMinimumWidth(190);
    layout->addWidget(stop);
    layout->addWidget(m_btnStart);

    connect(stop, &QPushButton::clicked, this, &MainWindow::stopPosting);
    connect(m_btnStart, &QPushButton::clicked, this, &MainWindow::startPosting);
    return panel;
}

// ============================ HÀNH ĐỘNG ============================

void MainWindow::chooseImages()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Chọn ảnh"), QString(),
        QStringLiteral("Tệp ảnh (*.png *.jpg *.jpeg *.gif *.bmp *.webp)"));
    if (files.isEmpty())
        return;
    m_images = files;
    m_lblImageCount->setText(QStringLiteral("%1 ảnh đã chọn").arg(m_images.size()));
    m_lblImageCount->setObjectName(QStringLiteral("chipGreen"));
    m_lblImageCount->setStyleSheet(QString());
    Logger::instance().log(QStringLiteral("Đã chọn %1 ảnh").arg(m_images.size()));
}

void MainWindow::fetchMyGroups()
{
    const QString cookie = m_config.cookieRaw.trimmed();
    if (cookie.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Cần tài khoản"),
                             QStringLiteral("Vui lòng thêm ít nhất một tài khoản (nút \"Quản lý tài khoản\")."));
        return;
    }
    m_btnFetchGroups->setEnabled(false);
    m_btnFetchGroups->setText(QStringLiteral("Đang xử lý..."));
    QMetaObject::invokeMethod(m_worker, "fetchMyGroups", Qt::QueuedConnection,
                              Q_ARG(QString, cookie), Q_ARG(bool, m_config.headless));
}

void MainWindow::importGroups()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Nhập nhóm"), QString(),
        QStringLiteral("Tệp nhóm (*.txt *.csv *.json)"));
    if (path.isEmpty())
        return;

    QVector<FacebookGroup> imported;
    bool ok = false;
    if (path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive))
        ok = GroupStore::loadFromJson(path, imported);
    else
        ok = GroupStore::importFromCsv(path, imported);
    if (!ok) {
        Logger::instance().log(QStringLiteral("Lỗi khi nhập nhóm từ tệp"));
        return;
    }

    // Gộp thêm (không thay thế): bỏ qua nhóm trùng ID.
    QSet<QString> existing;
    for (const FacebookGroup &g : m_groups)
        existing.insert(g.id);
    int added = 0;
    for (const FacebookGroup &g : imported) {
        if (existing.contains(g.id))
            continue;
        existing.insert(g.id);
        m_groups.append(g);
        ++added;
    }
    if (added > 0)
        updateGroupsTable(m_groups);
    Logger::instance().log(QStringLiteral("Đã thêm %1 nhóm từ tệp (tổng %2)")
                               .arg(added)
                               .arg(m_groups.size()));
}

void MainWindow::exportGroups()
{
    QFileDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Xuất nhóm"));
    dlg.setAcceptMode(QFileDialog::AcceptSave);
    dlg.setDefaultSuffix(QStringLiteral("csv"));
    dlg.setDirectory(DataStore::dir());
    dlg.selectFile(QStringLiteral("facebook_groups.csv"));
    QStringList filters;
    filters << QStringLiteral("CSV (*.csv)") << QStringLiteral("JSON (*.json)");
    dlg.setNameFilters(filters);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString path = dlg.selectedFiles().value(0);
    if (path.isEmpty())
        return;

    bool ok = false;
    if (dlg.selectedNameFilter().contains(QStringLiteral("JSON")) ||
        path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
        ok = GroupStore::saveToJson(path, m_groups);
    } else {
        ok = GroupStore::exportToCsv(path, m_groups);
    }
    if (ok)
        Logger::instance().log(QStringLiteral("Đã xuất các nhóm vào: ") + path);
}

void MainWindow::addGroupsById()
{
    QDialog dlg(this);
    dlg.setWindowTitle(QStringLiteral("Thêm nhóm bằng ID"));
    dlg.resize(440, 320);

    auto *lay = new QVBoxLayout(&dlg);
    lay->setSpacing(10);
    lay->addWidget(new QLabel(QStringLiteral("Dán một hoặc nhiều ID nhóm (mỗi ID một dòng):"), &dlg));

    auto *txt = new QTextEdit(&dlg);
    txt->setAcceptRichText(false);
    txt->setPlaceholderText(QStringLiteral("Ví dụ:\n123456789012345\n987654321098765"));
    lay->addWidget(txt, 1);

    lay->addWidget(new QLabel(QStringLiteral("Loại nhóm (dùng cho cột Quyền riêng tư):"), &dlg));
    auto *type = new QComboBox(&dlg);
    type->addItems({QStringLiteral("công khai"), QStringLiteral("riêng tư"),
                    QStringLiteral("chia sẻ"), QStringLiteral("ẩn"), QStringLiteral("chưa rõ")});
    lay->addWidget(type);

    auto *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    bb->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Thêm"));
    bb->button(QDialogButtonBox::Cancel)->setText(QStringLiteral("Hủy"));
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(bb);

    if (dlg.exec() != QDialog::Accepted)
        return;

    const QStringList lines =
        txt->toPlainText().split(QRegularExpression(QStringLiteral("[\\n\\r,;]")), Qt::SkipEmptyParts);
    const QString privacy = type->currentText();

    QSet<QString> existing;
    for (const FacebookGroup &g : m_groups)
        existing.insert(g.id);

    int added = 0;
    for (const QString &line : lines) {
        const QString id = Utils::extractGroupId(line.trimmed());
        if (id.isEmpty() || existing.contains(id))
            continue;
        FacebookGroup g;
        g.id = id;
        g.name = QStringLiteral("Nhóm ") + id;
        g.url = QStringLiteral("https://www.facebook.com/groups/") + id;
        g.privacy = privacy;
        g.selected = true;
        m_groups.append(g);
        existing.insert(id);
        ++added;
    }

    if (added > 0) {
        updateGroupsTable(m_groups);
        Logger::instance().log(QStringLiteral("Đã thêm %1 nhóm bằng ID").arg(added));
    } else {
        Logger::instance().log(QStringLiteral("Không có ID hợp lệ để thêm"));
    }
}

void MainWindow::applyPrivacyFilter()
{
    if (!m_groupTable || !m_cmbPrivacyFilter)
        return;
    const QString filter = m_cmbPrivacyFilter->currentText();
    for (int i = 0; i < m_groupTable->rowCount(); ++i) {
        bool show = true;
        if (filter != QStringLiteral("Tất cả")) {
            if (auto *it = m_groupTable->item(i, 3))
                show = it->text() == filter;
            else
                show = false;
        }
        m_groupTable->setRowHidden(i, !show);
    }
}

void MainWindow::saveGroupsFile()
{
    GroupStore::saveAll(m_groups);
}

void MainWindow::loadGroupsFile()
{
    QVector<FacebookGroup> groups;
    if (GroupStore::loadAll(groups) && !groups.isEmpty()) {
        m_groups = groups;
        updateGroupsTable(groups);
        return;
    }
    // Nâng cấp từ file groups.json cũ (DB chưa có dữ liệu).
    if (GroupStore::loadFromJson(QStringLiteral("groups.json"), groups) && !groups.isEmpty()) {
        m_groups = groups;
        GroupStore::saveAll(groups);
        updateGroupsTable(groups);
    }
}

void MainWindow::selectAllGroups()
{
    for (int i = 0; i < m_groupTable->rowCount(); ++i) {
        if (auto *item = m_groupTable->item(i, 0))
            item->setCheckState(Qt::Checked);
    }
    updateGroupStats();
}

void MainWindow::deselectAllGroups()
{
    for (int i = 0; i < m_groupTable->rowCount(); ++i) {
        if (auto *item = m_groupTable->item(i, 0))
            item->setCheckState(Qt::Unchecked);
    }
    updateGroupStats();
}

void MainWindow::searchGroups()
{
    runJoin(false);
}

void MainWindow::loadJoinIdsJson()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Nạp ID nhóm từ JSON"), QDir::current().filePath(QStringLiteral("groups.json")),
        QStringLiteral("JSON (*.json)"));
    if (path.isEmpty())
        return;

    QVector<FacebookGroup> groups;
    if (!GroupStore::loadFromJson(path, groups)) {
        Logger::instance().log(QStringLiteral("Lỗi: không đọc được ID từ file JSON"));
        return;
    }

    QStringList existing;
    if (m_txtJoinGroupIds)
        existing = m_txtJoinGroupIds->toPlainText()
                       .split(QRegularExpression(QStringLiteral("[\\n\\r,;]")), Qt::SkipEmptyParts);

    QSet<QString> seen;
    for (const QString &e : existing)
        seen.insert(Utils::extractGroupId(e));

    int added = 0;
    for (const FacebookGroup &g : groups) {
        if (g.id.isEmpty() || seen.contains(g.id))
            continue;
        seen.insert(g.id);
        existing << g.id;
        ++added;
    }

    if (m_txtJoinGroupIds)
        m_txtJoinGroupIds->setPlainText(existing.join(QLatin1Char('\n')));
    Logger::instance().log(QStringLiteral("Đã nạp %1 ID nhóm từ JSON").arg(added));
}

void MainWindow::startJoin()
{
    runJoin(true);
}

void MainWindow::runJoin(bool forceAutoJoin)
{
    const QString cookie = m_config.cookieRaw.trimmed();
    if (cookie.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Cần tài khoản"),
                             QStringLiteral("Vui lòng thêm ít nhất một tài khoản (nút \"Quản lý tài khoản\")."));
        return;
    }
    if (!forceAutoJoin && m_txtSearchKeywords->toPlainText().trimmed().isEmpty() &&
        m_txtJoinGroupIds->toPlainText().trimmed().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Cần dữ liệu"),
                             QStringLiteral("Vui lòng nhập từ khóa tìm kiếm hoặc ID nhóm."));
        return;
    }

    saveConfig();
    JoinRequest req;
    req.keywords = m_txtSearchKeywords->toPlainText().trimmed();

    // Đọc danh sách ID nhập trực tiếp (hỗ trợ cả URL hoặc ID trần).
    const QStringList rawIds =
        m_txtJoinGroupIds->toPlainText().split(QRegularExpression(QStringLiteral("[\\n\\r,;]")), Qt::SkipEmptyParts);
    for (const QString &r : rawIds) {
        const QString id = Utils::extractGroupId(r.trimmed());
        if (!id.isEmpty())
            req.joinGroupIds.append(id);
    }
    req.maxGroups = m_txtMaxGroups->value();
    req.joinDelaySec = m_txtJoinDelay->value();
    req.joinAction = m_cmbJoinAction->currentText();
    req.autoJoin = forceAutoJoin || m_chkAutoJoin->isChecked();
    req.skipPrivate = m_chkSkipPrivate->isChecked();
    req.skipPending = m_chkSkipPending->isChecked();

    m_btnSearchGroups->setEnabled(false);
    m_btnStartJoin->setEnabled(false);
    m_btnStopJoin->setEnabled(true);
    QMetaObject::invokeMethod(m_worker, "searchAndJoin", Qt::QueuedConnection,
                              Q_ARG(JoinRequest, req), Q_ARG(QString, cookie),
                              Q_ARG(bool, m_config.headless));
}

void MainWindow::stopJoin()
{
    if (m_worker)
        m_worker->requestStop();
    Logger::instance().log(QStringLiteral("Tự động tham gia đã dừng bởi người dùng"));
}

void MainWindow::startPosting()
{
    // Chế độ tài khoản thuê: mỗi tài khoản có cookie, nội dung chữ + ảnh (tùy chọn) và
    // gói bài riêng; bị chặn hoặc hết bài trong gói thì tự chuyển tài khoản thuê kế tiếp,
    // chỉ trừ 1 bài khi đăng thành công.
    // Quy tắc ảnh: ảnh KHÔNG bắt buộc — tài khoản không có ảnh thì đăng nội dung chữ;
    // tài khoản có ảnh thì đăng cả nội dung + ảnh.
    QVector<RentedAccount> rentedSel;
    if (m_chkUseRented && m_chkUseRented->isChecked()) {
        for (const RentedAccount &r : m_rented) {
            if (r.selected && !r.cookieRaw.trimmed().isEmpty())
                rentedSel.append(r);
        }
    }
    const bool rentedMode = !rentedSel.isEmpty();
    if (m_chkUseRented && m_chkUseRented->isChecked() && !m_rented.isEmpty() && !rentedMode) {
        QMessageBox::warning(
            this, QStringLiteral("Không có tài khoản thuê khả dụng"),
            QStringLiteral("Không có tài khoản thuê nào đủ điều kiện đăng bài (chưa chọn hoặc "
                           "thiếu cookie).\nHãy kiểm tra lại danh sách tài khoản thuê hoặc bỏ "
                           "chọn \"Sử dụng tài khoản thuê khi đăng bài\"."));
        return;
    }

    const QString cookie = m_config.cookieRaw.trimmed();
    if (!rentedMode && cookie.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Cần tài khoản"),
                             QStringLiteral("Vui lòng thêm ít nhất một tài khoản (nút \"Quản lý tài khoản\")."));
        return;
    }
    const QString content = m_txtContent->toPlainText().trimmed();
    if (content.isEmpty() && !rentedMode) {
        QMessageBox::warning(this, QStringLiteral("Cần nội dung"),
                             QStringLiteral("Vui lòng nhập nội dung bài đăng."));
        return;
    }
    // Tách nhiều nội dung: mỗi đoạn ngăn cách bằng một dòng chỉ chứa --- .
    QStringList blocks;
    if (!content.isEmpty()) {
        const QStringList parts = m_txtContent->toPlainText().split(
            QRegularExpression(QStringLiteral("^\\s*-{3,}\\s*$")), Qt::SkipEmptyParts);
        for (const QString &p : parts) {
            const QString t = p.trimmed();
            if (!t.isEmpty())
                blocks.append(t);
        }
    }
    if (blocks.isEmpty()) {
        if (!rentedMode) {
            QMessageBox::warning(this, QStringLiteral("Cần nội dung"),
                                 QStringLiteral("Nội dung rỗng. Vui lòng nhập ít nhất một bài đăng."));
            return;
        }
        // Chế độ tài khoản thuê: nội dung chung có thể để trống nếu mỗi tài khoản đều có
        // nội dung riêng, hoặc có ảnh riêng (bài chỉ ảnh, không cần chữ).
        for (const RentedAccount &r : rentedSel) {
            if (r.postText.trimmed().isEmpty() && r.images.isEmpty()) {
                QMessageBox::warning(
                    this, QStringLiteral("Cần nội dung"),
                    QStringLiteral("Tài khoản thuê \"%1\" chưa có nội dung cũng chưa có ảnh "
                                   "riêng. Hãy nhập nội dung chung, hoặc thêm nội dung/ảnh riêng "
                                   "cho tài khoản này.")
                        .arg(r.name));
                return;
            }
        }
        blocks.append(QString());
    }
    const QVector<FacebookGroup> sel = selectedGroups();
    if (sel.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Chưa chọn nhóm"),
                             QStringLiteral("Vui lòng chọn ít nhất một nhóm để đăng bài."));
        return;
    }

    saveConfig();
    PostRequest req;
    req.content = blocks.first();
    req.contents = blocks;
    for (const QString &f : m_images) {
        const QFileInfo fi(f);
        if (fi.exists() && fi.size() > 0)
            req.images.append(f);
        if (req.images.size() >= 5)
            break;
    }
    req.groups = sel;
    req.retryCount = m_txtRetryCount->value();
    req.threadCount = m_txtThreadCount->value();
    req.tabCount = m_txtTabCount->value();
    req.delaySec = m_config.effectiveDelaySec();
    req.skipPostedToday = m_chkSkipPosted && m_chkSkipPosted->isChecked();
    req.scheduleEnabled = m_chkSchedule && m_chkSchedule->isChecked();
    req.scheduleTime = m_timeSchedule->time().toString(QStringLiteral("HH:mm"));
    req.scheduleTimes.clear();
    if (m_timeList) {
        for (int i = 0; i < m_timeList->count(); ++i) {
            const QString t = m_timeList->item(i)->text().trimmed();
            if (!t.isEmpty())
                req.scheduleTimes.append(t);
        }
    }
    if (req.scheduleTimes.isEmpty())
        req.scheduleTimes << req.scheduleTime;
    req.proxyPool = m_config.proxyPool;
    req.rotateProxyPerPost = m_chkRotateProxy && m_chkRotateProxy->isChecked();
    req.dailyLimit = m_config.dailyPostLimit;
    req.jitterSec = m_config.jitterSec;
    req.interleaveAccounts = m_chkInterleave ? m_chkInterleave->isChecked() : true;

    m_btnStart->setEnabled(false);
    m_btnStart->setText(QStringLiteral("Đang đăng bài..."));
    m_progressBar->setValue(0);
    m_lblCounter->setText(QStringLiteral("Tiến độ: 0/%1 · Thành công: 0 · Thất bại: 0").arg(sel.size()));

    if (rentedMode) {
        Logger::instance().log(QStringLiteral("Bắt đầu đăng bài bằng %1 tài khoản thuê (tự chuyển khi hết bài / bị chặn)")
                                   .arg(rentedSel.size()));
        QMetaObject::invokeMethod(m_worker, "startRented", Qt::QueuedConnection,
                                  Q_ARG(PostRequest, req), Q_ARG(bool, m_config.headless),
                                  Q_ARG(QVector<RentedAccount>, rentedSel));
        return;
    }

    QMetaObject::invokeMethod(m_worker, "startPosting", Qt::QueuedConnection,
                              Q_ARG(PostRequest, req), Q_ARG(QString, cookie),
                              Q_ARG(bool, m_config.headless),
                              Q_ARG(QVector<FacebookAccount>, m_config.accounts),
                              Q_ARG(bool, m_config.rotateAccounts),
                              Q_ARG(int, m_config.rotateFailThreshold));
}

void MainWindow::stopPosting()
{
    if (m_worker)
        m_worker->requestStop();
    Logger::instance().log(QStringLiteral("Đã dừng đăng bài bởi người dùng"));
    notify(QStringLiteral("Đã dừng đăng bài"),
           QStringLiteral("Bạn đã dừng: %1 thành công · %2 thất bại.")
               .arg(m_lastSuccess)
               .arg(m_lastFailed));
}

void MainWindow::openCookieManager()
{
    CookieDialog dialog(m_config.accounts, m_config.cookieRaw, this);
    dialog.exec();

    // Cửa sổ quản lý tài khoản chỉ có nút "Đóng" (đã tự sửa dữ liệu trực tiếp mỗi
    // lần thêm/sửa/xóa), nên LUÔN lưu kết quả dù đóng bằng "Đóng", nút X hay phím
    // Esc — tránh tình trạng nhập xong đóng cửa sổ là tài khoản biến mất.
    m_config.accounts = dialog.accounts();

    // Cập nhật cookie chủ động cho các chức năng đơn tài khoản
    // (lấy nhóm, tự động tham gia) từ tài khoản được chọn đầu tiên.
    m_config.cookieRaw.clear();
    for (const FacebookAccount &a : m_config.accounts) {
        if (a.selected) {
            m_config.cookieRaw = a.cookieRaw;
            break;
        }
    }
    if (m_config.cookieRaw.isEmpty() && !m_config.accounts.isEmpty())
        m_config.cookieRaw = m_config.accounts.first().cookieRaw;

    if (m_config.saveSession)
        m_config.saveCookieToFile();
    m_config.save();
    updateCookieStatusLabel();
    autoSaveDashboard();
    Logger::instance().log(
        QStringLiteral("Đã cập nhật %1 tài khoản thành công").arg(m_config.accounts.size()));
}

void MainWindow::clearLog()
{
    m_pendingLog.clear();
    if (m_logFlushTimer)
        m_logFlushTimer->stop();
    m_txtLog->clear();
}

void MainWindow::refreshDashboard()
{
    if (!m_dashPosts)
        return;

    // Đọc số liệu tổng hợp trực tiếp từ DB (không quét toàn bộ bảng posts).
    const DashboardStore::DashboardStats stats = DashboardStore::loadStats();

    QHash<QString, int> accOk;
    QHash<QString, int> accFail;
    int okToday = 0;
    int failToday = 0;
    for (const DashboardStore::AccountStat &st : stats.accountToday) {
        accOk[st.accountId] = st.ok;
        accFail[st.accountId] = st.total - st.ok;
        okToday += st.ok;
        failToday += st.total - st.ok;
    }

    int selected = 0;
    for (const FacebookGroup &g : m_groups) {
        if (g.selected)
            ++selected;
    }

    int activeAcc = 0;
    int bannedAcc = 0;
    for (const FacebookAccount &a : m_config.accounts) {
        if (!a.cookieRaw.trimmed().isEmpty())
            ++activeAcc;
        if (a.status == QStringLiteral("bị chặn"))
            ++bannedAcc;
    }

    m_dashPosts->setText(QString::number(okToday + failToday));
    m_dashSuccess->setText(QString::number(okToday));
    m_dashFailed->setText(QString::number(failToday));
    m_dashAccounts->setText(
        bannedAcc > 0 ? QStringLiteral("%1/%2").arg(activeAcc - bannedAcc).arg(activeAcc)
                      : QStringLiteral("%1/%2").arg(activeAcc).arg(m_config.accounts.size()));
    m_dashGroups->setText(QStringLiteral("%1/%2").arg(selected).arg(m_groups.size()));

    // ----- Hero banner -----
    const int totalToday = okToday + failToday;
    if (m_heroTotal)
        m_heroTotal->setText(totalToday > 0 ? QString::number(totalToday)
                                            : QStringLiteral("0"));
    if (m_heroBar)
        m_heroBar->setValue(totalToday > 0 ? okToday * 100 / totalToday : 0);
    if (m_heroRate)
        m_heroRate->setText(totalToday > 0
                                ? QStringLiteral("%1%  ·  %2 thành công / %3 thất bại")
                                      .arg(okToday * 100 / totalToday)
                                      .arg(okToday)
                                      .arg(failToday)
                                : QStringLiteral("Chưa đăng bài hôm nay"));
    if (m_dashDonut)
        m_dashDonut->setData(okToday, totalToday);

    // ----- Caption KPI: so sánh với hôm qua (trend) -----
    const QString todayKey = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    const QString yestKey = QDate::currentDate().addDays(-1).toString(QStringLiteral("yyyy-MM-dd"));
    int tToday = 0, tYest = 0, okTodayD = 0, okYestD = 0;
    for (const DashboardStore::DailyStat &st : stats.days) {
        if (st.day == todayKey) {
            tToday = st.total;
            okTodayD = st.ok;
        } else if (st.day == yestKey) {
            tYest = st.total;
            okYestD = st.ok;
        }
    }
    auto setTrend = [](QLabel *cap, const QString &label, int today, int yest) {
        if (yest <= 0 && today <= 0) {
            cap->setText(QStringLiteral("Chưa có dữ liệu hôm qua"));
            cap->setStyleSheet(QString());
            return;
        }
        if (yest <= 0) {
            cap->setText(QStringLiteral("Bài đầu tiên hôm nay"));
            cap->setStyleSheet(QString());
            return;
        }
        const int delta = today - yest;
        const int pct = delta * 100 / yest;
        const QString arrow = delta >= 0 ? QStringLiteral("▲") : QStringLiteral("▼");
        cap->setText(QStringLiteral("%1 %2% so với %3").arg(arrow).arg(qAbs(pct)).arg(label));
        cap->setStyleSheet(delta >= 0
                               ? QStringLiteral("color: #188038; font-weight: 600;")
                               : QStringLiteral("color: #d93025; font-weight: 600;"));
    };
    if (m_dashCaptions.size() >= 3) {
        setTrend(m_dashCaptions.at(0), QStringLiteral("hôm qua"), tToday, tYest);
        setTrend(m_dashCaptions.at(1), QStringLiteral("hôm qua"), okTodayD, okYestD);
        setTrend(m_dashCaptions.at(2), QStringLiteral("hôm qua"),
                 okTodayD < tToday ? tToday - okTodayD : 0, okYestD < tYest ? tYest - okYestD : 0);
    }
    if (m_dashCaptions.size() >= 4) {
        m_dashCaptions.at(3)->setText(
            bannedAcc > 0 ? QStringLiteral("%1 hoạt động · %2 bị chặn")
                                 .arg(activeAcc - bannedAcc)
                                 .arg(bannedAcc)
                          : QStringLiteral("Tất cả tài khoản sẵn sàng"));
        m_dashCaptions.at(3)->setStyleSheet(bannedAcc > 0 ? QStringLiteral("color: #d93025;")
                                                          : QStringLiteral("color: #188038;"));
    }
    if (m_dashCaptions.size() >= 5)
        m_dashCaptions.at(4)->setText(QStringLiteral("Chọn nhóm trong trang quản lý"));

    // Hero: trend so với hôm qua.
    if (m_heroTrend) {
        if (tYest <= 0 && tToday <= 0) {
            m_heroTrend->setText(QStringLiteral("Chưa có dữ liệu hôm qua"));
            m_heroTrend->setStyleSheet(QString());
        } else if (tYest <= 0) {
            m_heroTrend->setText(QStringLiteral("Bài đăng đầu tiên hôm nay"));
            m_heroTrend->setStyleSheet(QString());
        } else {
            const int delta = tToday - tYest;
            const int pct = qAbs(delta) * 100 / tYest;
            const QString arrow = delta >= 0 ? QStringLiteral("▲") : QStringLiteral("▼");
            m_heroTrend->setText(QStringLiteral("%1 %2% bài đăng so với hôm qua")
                                     .arg(arrow)
                                     .arg(pct));
            m_heroTrend->setStyleSheet(delta >= 0 ? QStringLiteral("color: #188038; font-weight: 600;")
                                                  : QStringLiteral("color: #d93025; font-weight: 600;"));
        }
    }

    // ----- Biểu đồ 7 ngày -----
    if (m_dashChart) {
        QHash<QString, const DashboardStore::DailyStat *> byDay;
        for (const DashboardStore::DailyStat &st : stats.days)
            byDay.insert(st.day, &st);

        QStringList labels;
        QVector<int> totals;
        QVector<int> oks;
        const QDate todayDate = QDate::currentDate();
        for (int d = 6; d >= 0; --d) {
            const QDate day = todayDate.addDays(-d);
            const QString key = day.toString(QStringLiteral("yyyy-MM-dd"));
            labels << day.toString(QStringLiteral("dd/MM"));
            const DashboardStore::DailyStat *st = byDay.value(key, nullptr);
            totals.append(st ? st->total : 0);
            oks.append(st ? st->ok : 0);
        }
        m_dashChart->setData(labels, totals, oks);
    }

    // ----- Bảng tài khoản -----
    // Xóa cell widgets CŨ trước khi dựng lại: setRowCount(0) chỉ xóa item (QTableWidgetItem),
    // KHÔNG xóa widget đã gắn bằng setCellWidget — chúng vẫn là con của bảng nên nếu không
    // xóa tay thì mỗi lần refresh lại tích thêm một lứa widget vào RAM.
    const int oldRows = m_dashAccountTable->rowCount();
    for (int r = 0; r < oldRows; ++r) {
        for (int c = 0; c < m_dashAccountTable->columnCount(); ++c) {
            if (QWidget *w = m_dashAccountTable->cellWidget(r, c)) {
                m_dashAccountTable->removeCellWidget(r, c);
                delete w;
            }
        }
    }
    m_dashAccountTable->setRowCount(0);
    m_dashAccountTable->setRowCount(m_config.accounts.size());
    const int limit = m_config.dailyPostLimit;
    for (int i = 0; i < m_config.accounts.size(); ++i) {
        const FacebookAccount &a = m_config.accounts.at(i);

        // Avatar tròn với chữ cái đầu của tên tài khoản.
        static const QColor avatarColors[] = {
            QColor(QStringLiteral("#1a73e8")), QColor(QStringLiteral("#188038")),
            QColor(QStringLiteral("#f9ab00")), QColor(QStringLiteral("#9334e6")),
            QColor(QStringLiteral("#d93025")), QColor(QStringLiteral("#007b83")),
        };
        const QColor avColor = avatarColors[i % 6];
        const QString initial = a.name.trimmed().isEmpty()
                                    ? QStringLiteral("?")
                                    : QString(a.name.trimmed().at(0)).toUpper();
        auto *avatar = new QLabel(initial, m_dashAccountTable);
        avatar->setFixedSize(30, 30);
        avatar->setAlignment(Qt::AlignCenter);
        avatar->setStyleSheet(
            QStringLiteral("background-color: %1; color: white; border-radius: 15px; "
                           "font-weight: 700; font-size: 13px;")
                .arg(avColor.name()));
        auto *cell = new QWidget(m_dashAccountTable);
        auto *cellLay = new QHBoxLayout(cell);
        cellLay->setContentsMargins(6, 2, 6, 2);
        cellLay->setSpacing(8);
        cellLay->addWidget(avatar);
        auto *nameLbl = new QLabel(a.name, cell);
        nameLbl->setStyleSheet(QStringLiteral("background: transparent; color: #202124; "
                                              "font-weight: 500;"));
        cellLay->addWidget(nameLbl, 1);
        cellLay->addStretch();
        m_dashAccountTable->setCellWidget(i, 0, cell);
        m_dashAccountTable->setRowHeight(i, 40);
        auto *statusItem = new QTableWidgetItem(a.status);
        statusItem->setTextAlignment(Qt::AlignCenter);
        if (a.status == QStringLiteral("bị chặn"))
            statusItem->setForeground(QColor(QStringLiteral("#d93025")));
        else if (a.status == QStringLiteral("hoạt động"))
            statusItem->setForeground(QColor(QStringLiteral("#188038")));
        m_dashAccountTable->setItem(i, 1, statusItem);
        const int posted = accOk.value(a.id, 0);
        auto *postedItem = new QTableWidgetItem(QString::number(posted));
        postedItem->setTextAlignment(Qt::AlignCenter);
        m_dashAccountTable->setItem(i, 2, postedItem);
        auto *limitItem = new QTableWidgetItem(limit > 0 ? QString::number(limit)
                                                         : QStringLiteral("∞"));
        limitItem->setTextAlignment(Qt::AlignCenter);
        m_dashAccountTable->setItem(i, 3, limitItem);
        auto *bar = new QProgressBar(m_dashAccountTable);
        bar->setRange(0, limit > 0 ? limit : 100);
        bar->setValue(limit > 0 ? qMin(posted, limit) : 0);
        bar->setFormat(limit > 0 ? QStringLiteral("%1%").arg(limit > 0 ? posted * 100 / limit : 0)
                                 : QStringLiteral("—"));
        m_dashAccountTable->setCellWidget(i, 4, bar);
    }

    // ----- Lịch sử theo ngày -----
    m_dashHistoryTable->setRowCount(0);
    m_dashHistoryTable->setRowCount(stats.days.size());
    for (int i = 0; i < stats.days.size(); ++i) {
        const DashboardStore::DailyStat &st = stats.days.at(i);
        auto *dateItem = new QTableWidgetItem(st.day);
        dateItem->setTextAlignment(Qt::AlignCenter);
        m_dashHistoryTable->setItem(i, 0, dateItem);
        m_dashHistoryTable->setItem(i, 1, new QTableWidgetItem(QString::number(st.total)));
        auto *okItem = new QTableWidgetItem(QString::number(st.ok));
        okItem->setTextAlignment(Qt::AlignCenter);
        okItem->setForeground(QColor(QStringLiteral("#188038")));
        m_dashHistoryTable->setItem(i, 2, okItem);
        auto *failItem = new QTableWidgetItem(QString::number(st.fail));
        failItem->setTextAlignment(Qt::AlignCenter);
        failItem->setForeground(QColor(QStringLiteral("#d93025")));
        m_dashHistoryTable->setItem(i, 3, failItem);
        auto *rateItem = new QTableWidgetItem(st.total > 0 ? QStringLiteral("%1%").arg(st.ok * 100 / st.total)
                                                           : QStringLiteral("—"));
        rateItem->setTextAlignment(Qt::AlignCenter);
        if (st.total > 0)
            rateItem->setForeground(st.ok * 100 / st.total >= 70
                                        ? QColor(QStringLiteral("#188038"))
                                        : QColor(QStringLiteral("#d93025")));
        m_dashHistoryTable->setItem(i, 4, rateItem);
    }

    // ----- Hoạt động gần đây -----
    m_dashRecent->clear();
    for (const DashboardStore::PostEntry &e : stats.recentToday) {
        auto *item = new QListWidgetItem(
            QStringLiteral("● ") + e.time + QStringLiteral("  ") + e.account +
            QStringLiteral(" → ") + e.group + QStringLiteral("  ·  ") +
            (e.ok ? QStringLiteral("Thành công") : QStringLiteral("Thất bại")));
        item->setForeground(e.ok ? QColor(QStringLiteral("#188038"))
                                 : QColor(QStringLiteral("#d93025")));
        m_dashRecent->addItem(item);
    }
}

void MainWindow::autoSaveDashboard()
{
    if (!m_txtLog)
        return;
    DashboardStore::saveAll(DataStore::filePath(QStringLiteral("dashboard.json")),
                            m_groups, m_config.accounts, m_config,
                            m_txtLog->toPlainText());
}

void MainWindow::doBackup()
{
    // Xả các cache trì hoãn ghi (5s/64 bài) xuống đĩa để bản sao lưu đủ số liệu
    // mới nhất — backup sao chép FILE chứ không đọc cache trong RAM.
    PostedStore::flush();
    DailyPostLog::flush();
    const QString dataDir = DataStore::dir();
    const QString bakDir = QDir(dataDir).filePath(QStringLiteral("backups"));
    if (!QDir().mkpath(bakDir))
        return;
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString dest = QDir(bakDir).filePath(QStringLiteral("backup-") + stamp);
    if (!QDir().mkpath(dest))
        return;

    const QStringList files = {QStringLiteral("config.json"),
                               QStringLiteral("dashboard.json"),
                               QStringLiteral("groups.json"),
                               QStringLiteral("posted_today.json"),
                               QStringLiteral("posts.json"),
                               QStringLiteral("rented.json")};
    int copied = 0;
    for (const QString &f : files) {
        const QString src = QDir(dataDir).filePath(f);
        if (QFile::exists(src) && QFile::copy(src, QDir(dest).filePath(f)))
            ++copied;
    }

    // Chỉ giữ 5 bản sao lưu gần nhất.
    QStringList entries =
        QDir(bakDir).entryList(QStringList{QStringLiteral("backup-*")},
                               QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    while (entries.size() > 5) {
        QDir(QDir(bakDir).filePath(entries.first())).removeRecursively();
        entries.removeFirst();
    }

    Logger::instance().log(QStringLiteral("Đã sao lưu %1 tệp dữ liệu vào %2").arg(copied).arg(dest));
}

void MainWindow::notify(const QString &title, const QString &msg)
{
    if (!m_config.notifyDone)
        return;
    QApplication::beep();
    if (m_tray && QSystemTrayIcon::supportsMessages())
        m_tray->showMessage(title, msg, QSystemTrayIcon::Information, 6000);
    // Gửi sang kênh ngoài (Telegram / Discord) nếu đã cấu hình.
    Notifier::send(m_config, title, msg);
}

void MainWindow::setupTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    QPixmap pm(64, 64);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(QStringLiteral("#1a73e8")));
    p.drawRoundedRect(QRectF(4, 4, 56, 56), 14, 14);
    QFont f = p.font();
    f.setBold(true);
    f.setPointSizeF(26);
    p.setFont(f);
    p.setPen(Qt::white);
    p.drawText(QRect(4, 4, 56, 56), Qt::AlignCenter, QStringLiteral("A"));
    p.end();
    m_tray = new QSystemTrayIcon(pm, this);
    m_tray->setToolTip(QStringLiteral("Facebook Auto Poster"));
    m_tray->show();
}

void MainWindow::saveDashboardJson()
{
    const QString path = QFileDialog::getSaveFileName(
        this, QStringLiteral("Lưu JSON đầy đủ"),
        DataStore::filePath(QStringLiteral("dashboard.json")), QStringLiteral("JSON (*.json)"));
    if (path.isEmpty())
        return;
    if (DashboardStore::saveAll(path, m_groups, m_config.accounts, m_config,
                                m_txtLog ? m_txtLog->toPlainText() : QString())) {
        Logger::instance().log(QStringLiteral("Đã lưu dữ liệu đầy đủ vào: ") + path);
        m_nav->setCurrentRow(0);
        refreshDashboard();
    } else {
        Logger::instance().log(QStringLiteral("Lỗi khi lưu JSON: ") + path);
    }
}

void MainWindow::loadDashboardJson()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Tải JSON đầy đủ"),
        DataStore::filePath(QStringLiteral("dashboard.json")), QStringLiteral("JSON (*.json)"));
    if (path.isEmpty())
        return;

    QVector<FacebookGroup> groups;
    QVector<FacebookAccount> accounts;
    if (!DashboardStore::loadAll(path, &groups, &accounts)) {
        Logger::instance().log(QStringLiteral("Lỗi: không đọc được file JSON: ") + path);
        return;
    }

    if (!groups.isEmpty()) {
        m_groups = groups;
        updateGroupsTable(groups);
    }
    if (!accounts.isEmpty()) {
        m_config.accounts = accounts;
        m_config.cookieRaw.clear();
        for (const FacebookAccount &a : m_config.accounts) {
            if (a.selected) {
                m_config.cookieRaw = a.cookieRaw;
                break;
            }
        }
        if (m_config.cookieRaw.isEmpty() && !m_config.accounts.isEmpty())
            m_config.cookieRaw = m_config.accounts.first().cookieRaw;
        m_config.save();
        updateCookieStatusLabel();
    }
    m_nav->setCurrentRow(0);
    refreshDashboard();
    autoSaveDashboard();
    Logger::instance().log(QStringLiteral("Đã tải dữ liệu từ: ") + path);
}

void MainWindow::appendLog(const QString &line)
{
    // Chỉ gom vào hàng đợi; không chạm QTextEdit ngay lập tức để không làm
    // nghẽn UI thread khi log dồn dập trong lúc đăng bài.
    m_pendingLog.append(line);
    // Phòng khi log dồn dập nhanh hơn tốc độ flush (25 dòng / 120ms): nếu không
    // giới hạn, hàng đợi sẽ lớn vô hạn, mỗi dòng là một QString -> RAM tăng mãi
    // trong lúc chạy lâu. Giữ tối đa 2000 dòng chờ, bỏ bớt phần cũ nhất.
    if (m_pendingLog.size() > 2000)
        m_pendingLog.remove(0, m_pendingLog.size() - 2000);
    if (m_logFlushTimer && !m_logFlushTimer->isActive())
        m_logFlushTimer->start();
}

void MainWindow::flushLog()
{
    if (!m_txtLog)
        return;

    // Nối cả lô (tối đa 25 dòng) thành MỘT chuỗi rồi append 1 lần duy nhất:
    // QTextEdit::append() mỗi dòng đều tính lại layout (1000 khối) — nối trước
    // giúp chỉ trả layout một lần, UI nhẹ hơn khi đăng dồn dập.
    const int batch = 25;
    QStringList pending;
    pending.reserve(qMin(batch, m_pendingLog.size()));
    for (int i = 0; i < batch && !m_pendingLog.isEmpty(); ++i)
        pending.append(m_pendingLog.takeFirst());
    if (!pending.isEmpty())
        m_txtLog->append(pending.join(QLatin1Char('\n')));
    // Giới hạn số dòng nhật ký để tránh chậm khi chạy lâu.
    const int maxLines = 1000;
    while (m_txtLog->document()->blockCount() > maxLines) {
        QTextCursor c(m_txtLog->document());
        c.movePosition(QTextCursor::Start);
        c.select(QTextCursor::BlockUnderCursor);
        c.removeSelectedText();
        c.deleteChar();
    }

    if (!m_pendingLog.isEmpty()) {
        m_logFlushTimer->start();
    } else if (m_logFlushTimer) {
        m_logFlushTimer->stop();
    }
}

void MainWindow::onGroupsReady(const QVector<FacebookGroup> &groups)
{
    updateGroupsTable(groups);
    m_btnFetchGroups->setEnabled(true);
    m_btnFetchGroups->setText(QStringLiteral("Lấy nhóm của tôi"));
    autoSaveDashboard();
}

void MainWindow::onProgress(int current, int total, int success, int failed)
{
    m_lastTotal = total;
    m_lastSuccess = success;
    m_lastFailed = failed;
    m_lblCounter->setText(QStringLiteral("Tiến độ: %1/%2 · Thành công: %3 · Thất bại: %4")
                              .arg(current)
                              .arg(total)
                              .arg(success)
                              .arg(failed));
    if (total > 0)
        m_progressBar->setValue(int(current * 100.0 / total));
}

void MainWindow::onAccountStatusChanged(const QString &accountId, const QString &status)
{
    // Tài khoản thuê cũng có thể bị chặn khi đăng (chế độ tài khoản thuê).
    bool rentedChanged = false;
    for (RentedAccount &r : m_rented) {
        if (r.id == accountId) {
            r.status = status;
            rentedChanged = true;
            break;
        }
    }
    if (rentedChanged) {
        refreshRentedTable();
        saveRentedFile();
    }

    for (FacebookAccount &a : m_config.accounts) {
        if (a.id == accountId) {
            a.status = status;
            break;
        }
    }
    m_config.save();
    updateCookieStatusLabel();
    autoSaveDashboard();
    Logger::instance().log(QStringLiteral("Tài khoản [%1] có trạng thái: %2")
                               .arg(accountId, status));
}

void MainWindow::onPostingDone()
{
    m_btnStart->setEnabled(true);
    m_btnStart->setText(QStringLiteral("Bắt đầu đăng bài"));
    m_progressBar->setValue(m_progressBar->maximum());
    autoSaveDashboard();
    refreshDashboard();
    notify(QStringLiteral("Hoàn tất đăng bài"),
           QStringLiteral("Đã xử lý %1 nhóm: %2 thành công · %3 thất bại.")
               .arg(m_lastTotal)
               .arg(m_lastSuccess)
               .arg(m_lastFailed));
}

void MainWindow::onJoiningDone()
{
    m_btnSearchGroups->setEnabled(true);
    m_btnStartJoin->setEnabled(true);
    m_btnStopJoin->setEnabled(false);
    notify(QStringLiteral("Hoàn tất"),
           QStringLiteral("Đã xong quá trình tìm kiếm / tham gia nhóm."));
}

// ============================ TÀI KHOẢN THUÊ ============================

QWidget *MainWindow::buildRentedTab()
{
    auto *panel = new QWidget(this);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(12);

    // Tiêu đề trang + hành động.
    auto *headRow = new QHBoxLayout();
    auto *titleBox = new QVBoxLayout();
    titleBox->setSpacing(2);
    auto *pageTitle = new QLabel(QStringLiteral("Tài khoản thuê"), panel);
    pageTitle->setObjectName(QStringLiteral("pageTitle"));
    auto *pageSub = new QLabel(
        QStringLiteral("Tài khoản Facebook cho thuê: cookie riêng, nội dung chữ + ảnh riêng "
                       "và gói bài đã mua cho từng khách."),
        panel);
    pageSub->setObjectName(QStringLiteral("pageSubtitle"));
    titleBox->addWidget(pageTitle);
    titleBox->addWidget(pageSub);
    headRow->addLayout(titleBox);
    headRow->addStretch();

    auto *btnAdd = new QPushButton(QStringLiteral("＋ Thêm tài khoản thuê"), panel);
    btnAdd->setObjectName(QStringLiteral("primaryButton"));
    auto *btnEdit = new QPushButton(QStringLiteral("Sửa"), panel);
    btnEdit->setObjectName(QStringLiteral("outlinedButton"));
    auto *btnRemove = new QPushButton(QStringLiteral("Xóa"), panel);
    btnRemove->setObjectName(QStringLiteral("dangerButton"));
    m_lblRentedStats = new QLabel(panel);
    m_lblRentedStats->setObjectName(QStringLiteral("chip"));
    headRow->addWidget(btnAdd);
    headRow->addWidget(btnEdit);
    headRow->addWidget(btnRemove);
    headRow->addWidget(m_lblRentedStats);
    layout->addLayout(headRow);

    m_chkUseRented = new QCheckBox(
        QStringLiteral("Sử dụng tài khoản thuê khi đăng bài — mỗi tài khoản 1 trình duyệt riêng "
                       "chạy song song, tự đăng nội dung + ảnh riêng (ảnh tùy chọn: không có ảnh "
                       "thì đăng chữ, có ảnh thì đăng cả hai); bị chặn hoặc hết bài trong gói sẽ "
                       "tự chuyển tài khoản thuê kế tiếp (chỉ trừ 1 bài khi đăng thành công)"),
        panel);
    m_chkUseRented->setToolTip(QStringLiteral(
        "Khi bật, chương trình dùng các tài khoản thuê đang được chọn thay cho tài khoản "
        "chính. Mỗi tài khoản thuê chạy 1 trình duyệt riêng song song. Ảnh không bắt buộc: "
        "tài khoản không có ảnh thì đăng nội dung chữ; có ảnh thì đăng cả nội dung + ảnh. "
        "Mỗi bài đăng thành công trừ 1 bài trong gói của tài khoản đó."));
    m_chkUseRented->setChecked(true);
    layout->addWidget(m_chkUseRented);

    m_rentedTable = new QTableWidget(0, 6, panel);
    m_rentedTable->setHorizontalHeaderLabels({
        QStringLiteral("Dùng"), QStringLiteral("Tên khách thuê"),
        QStringLiteral("Trạng thái"), QStringLiteral("Đã dùng / Gói"),
        QStringLiteral("Ảnh"), QStringLiteral("Giá (đ)")});
    m_rentedTable->verticalHeader()->setVisible(false);
    m_rentedTable->verticalHeader()->setDefaultSectionSize(36);
    m_rentedTable->setAlternatingRowColors(true);
    m_rentedTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_rentedTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_rentedTable->setShowGrid(false);
    m_rentedTable->setObjectName(QStringLiteral("dataTable"));
    m_rentedTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_rentedTable->horizontalHeader()->resizeSection(0, 60);
    m_rentedTable->horizontalHeader()->resizeSection(2, 110);
    m_rentedTable->horizontalHeader()->resizeSection(3, 130);
    m_rentedTable->horizontalHeader()->resizeSection(4, 70);
    m_rentedTable->horizontalHeader()->resizeSection(5, 110);
    layout->addWidget(m_rentedTable, 1);

    auto *hint = new QLabel(
        QStringLiteral("Mỗi tài khoản thuê nhập: cookie riêng (bắt buộc), nội dung chữ + nhiều "
                       "ảnh (tùy chọn), gói bài đã mua và giá. Quy tắc đăng: tài khoản không có "
                       "ảnh thì đăng nội dung chữ; có ảnh thì đăng cả nội dung + ảnh. Số bài chỉ "
                       "trừ 1 khi đăng thành công; hết bài hoặc bị chặn, chương trình tự chuyển "
                       "tài khoản thuê kế tiếp. Nhấn đúp vào một dòng để sửa nhanh."),
        panel);
    hint->setStyleSheet(QStringLiteral("color: #5f6368; font-size: 12px;"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    connect(btnAdd, &QPushButton::clicked, this, &MainWindow::addRentedAccount);
    connect(btnEdit, &QPushButton::clicked, this, &MainWindow::editRentedAccount);
    connect(btnRemove, &QPushButton::clicked, this, &MainWindow::removeRentedAccount);
    connect(m_rentedTable, &QTableWidget::itemDoubleClicked, this,
            [this](QTableWidgetItem *) { editRentedAccount(); });
    connect(m_rentedTable, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (item && item->column() == 0) {
            const int row = item->row();
            if (row >= 0 && row < m_rented.size()) {
                m_rented[row].selected = item->checkState() == Qt::Checked;
                saveRentedFile();
                if (m_lblRentedStats) {
                    int sel = 0;
                    int out = 0;
                    for (const RentedAccount &r : m_rented) {
                        if (r.selected)
                            ++sel;
                        if (r.used >= r.totalPosts)
                            ++out;
                    }
                    m_lblRentedStats->setText(
                        QStringLiteral("Tổng %1 · Chọn %2 · Hết bài %3")
                            .arg(m_rented.size())
                            .arg(sel)
                            .arg(out));
                }
            }
        }
    });
    return panel;
}

void MainWindow::addRentedAccount()
{
    RentedAccount a;
    a.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    a.selected = true;
    RentedDialog dlg(a, true, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    m_rented.append(dlg.account());
    refreshRentedTable();
    saveRentedFile();
    Logger::instance().log(QStringLiteral("Đã thêm tài khoản thuê mới (tổng %1)").arg(m_rented.size()));
}

void MainWindow::editRentedAccount()
{
    const int row = m_rentedTable ? m_rentedTable->currentRow() : -1;
    if (row < 0 || row >= m_rented.size()) {
        QMessageBox::warning(this, QStringLiteral("Chưa chọn tài khoản"),
                             QStringLiteral("Vui lòng chọn một tài khoản thuê để sửa."));
        return;
    }
    RentedDialog dlg(m_rented.at(row), false, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    m_rented[row] = dlg.account();
    refreshRentedTable();
    saveRentedFile();
    Logger::instance().log(
        QStringLiteral("Đã cập nhật tài khoản thuê \"%1\"").arg(m_rented.at(row).name));
}

void MainWindow::removeRentedAccount()
{
    const int row = m_rentedTable ? m_rentedTable->currentRow() : -1;
    if (row < 0 || row >= m_rented.size())
        return;
    const RentedAccount &r = m_rented.at(row);
    const auto ret = QMessageBox::question(
        this, QStringLiteral("Xóa tài khoản thuê"),
        QStringLiteral("Xóa tài khoản thuê \"%1\"? Số bài đã dùng (%2/%3) sẽ bị mất.")
            .arg(r.name)
            .arg(r.used)
            .arg(r.totalPosts));
    if (ret != QMessageBox::Yes)
        return;
    m_rented.removeAt(row);
    refreshRentedTable();
    saveRentedFile();
    Logger::instance().log(QStringLiteral("Đã xóa tài khoản thuê \"%1\"").arg(r.name));
}

void MainWindow::refreshRentedTable()
{
    if (!m_rentedTable)
        return;
    // Chặn tín hiệu để việc tô bảng không kích hoạt lại itemChanged.
    m_rentedTable->blockSignals(true);
    m_rentedTable->setRowCount(m_rented.size());
    for (int i = 0; i < m_rented.size(); ++i) {
        const RentedAccount &r = m_rented.at(i);

        auto *check = new QTableWidgetItem();
        check->setFlags(check->flags() | Qt::ItemIsUserCheckable);
        check->setCheckState(r.selected ? Qt::Checked : Qt::Unchecked);
        check->setTextAlignment(Qt::AlignCenter);
        m_rentedTable->setItem(i, 0, check);

        auto *nameItem = new QTableWidgetItem(r.name);
        nameItem->setToolTip(QStringLiteral("Cookie: có%1 · Proxy: %2 · Nội dung riêng: %3")
                                 .arg(r.cookieRaw.trimmed().isEmpty() ? QStringLiteral(" (thiếu)")
                                                                      : QString())
                                 .arg(r.proxy.trimmed().isEmpty() ? QStringLiteral("không")
                                                                  : r.proxy)
                                 .arg(r.postText.trimmed().isEmpty()
                                          ? QStringLiteral("dùng nội dung chung")
                                          : QStringLiteral("có")));
        m_rentedTable->setItem(i, 1, nameItem);

        const QString status = r.status == QStringLiteral("bị chặn")
                                   ? QStringLiteral("bị chặn")
                                   : (r.used >= r.totalPosts ? QStringLiteral("hết bài")
                                                             : QStringLiteral("hoạt động"));
        auto *statusItem = new QTableWidgetItem(status);
        statusItem->setTextAlignment(Qt::AlignCenter);
        if (status == QStringLiteral("bị chặn"))
            statusItem->setForeground(QColor(QStringLiteral("#d93025")));
        else if (status == QStringLiteral("hết bài"))
            statusItem->setForeground(QColor(QStringLiteral("#f9ab00")));
        else
            statusItem->setForeground(QColor(QStringLiteral("#188038")));
        m_rentedTable->setItem(i, 2, statusItem);

        auto *usedItem = new QTableWidgetItem(
            QStringLiteral("%1 / %2").arg(r.used).arg(r.totalPosts));
        usedItem->setTextAlignment(Qt::AlignCenter);
        usedItem->setToolTip(
            QStringLiteral("Còn %1 bài trong gói. Chỉ trừ 1 khi đăng thành công.")
                .arg(qMax(0, r.totalPosts - r.used)));
        m_rentedTable->setItem(i, 3, usedItem);

        auto *imgItem = new QTableWidgetItem(QString::number(r.images.size()));
        imgItem->setTextAlignment(Qt::AlignCenter);
        imgItem->setToolTip(r.images.isEmpty()
                                ? QStringLiteral("Chưa có ảnh riêng — sẽ đăng nội dung chữ")
                                : r.images.join(QLatin1Char('\n')));
        m_rentedTable->setItem(i, 4, imgItem);

        auto *priceItem = new QTableWidgetItem(Utils::formatNumber(r.price));
        priceItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_rentedTable->setItem(i, 5, priceItem);
    }
    m_rentedTable->blockSignals(false);

    if (m_lblRentedStats) {
        int sel = 0;
        int out = 0;
        for (const RentedAccount &r : m_rented) {
            if (r.selected)
                ++sel;
            if (r.used >= r.totalPosts)
                ++out;
        }
        m_lblRentedStats->setText(QStringLiteral("Tổng %1 · Chọn %2 · Hết bài %3")
                                      .arg(m_rented.size())
                                      .arg(sel)
                                      .arg(out));
    }
}

void MainWindow::saveRentedFile()
{
    DashboardStore::saveRented(DataStore::filePath(QStringLiteral("rented.json")), m_rented);
}

void MainWindow::loadRentedFile()
{
    QVector<RentedAccount> rented;
    if (DashboardStore::loadRented(DataStore::filePath(QStringLiteral("rented.json")), &rented)) {
        m_rented = rented;
        refreshRentedTable();
    }
}

void MainWindow::onRentedQuotaUsed(const QString &rentedId, int used)
{
    // Trừ 1 bài khi đăng thành công: worker báo số bài đã dùng mới nhất.
    for (RentedAccount &r : m_rented) {
        if (r.id == rentedId) {
            r.used = used;
            if (used >= r.totalPosts && r.status != QStringLiteral("bị chặn"))
                r.status = QStringLiteral("hết bài");
            break;
        }
    }
    refreshRentedTable();
    saveRentedFile();
    autoSaveDashboard();
}

// ============================ BẢNG NHÓM ============================

void MainWindow::updateGroupsTable(const QVector<FacebookGroup> &groups)
{
    m_groups = groups;
    m_groupTable->setRowCount(groups.size());
    for (int i = 0; i < groups.size(); ++i) {
        const FacebookGroup &g = groups.at(i);

        auto *check = new QTableWidgetItem();
        check->setFlags(check->flags() | Qt::ItemIsUserCheckable);
        check->setCheckState(g.selected ? Qt::Checked : Qt::Unchecked);
        check->setTextAlignment(Qt::AlignCenter);
        m_groupTable->setItem(i, 0, check);

        m_groupTable->setItem(i, 1, new QTableWidgetItem(g.name));
        auto *idItem = new QTableWidgetItem(g.id);
        idItem->setTextAlignment(Qt::AlignCenter);
        m_groupTable->setItem(i, 2, idItem);

        auto *privacyItem = new QTableWidgetItem(Utils::normalizePrivacy(g.privacy));
        privacyItem->setTextAlignment(Qt::AlignCenter);
        m_groupTable->setItem(i, 3, privacyItem);

        auto *memberItem = new QTableWidgetItem(
            g.memberCount > 0 ? Utils::formatNumber(g.memberCount) : QStringLiteral("N/A"));
        memberItem->setTextAlignment(Qt::AlignCenter);
        m_groupTable->setItem(i, 4, memberItem);

        auto *statusItem = new QTableWidgetItem(
            g.isMember ? QStringLiteral("Thành viên")
                       : (g.pending ? QStringLiteral("Chờ") : QStringLiteral("—")));
        statusItem->setTextAlignment(Qt::AlignCenter);
        m_groupTable->setItem(i, 5, statusItem);
    }
    updateGroupStats();
    applyPrivacyFilter();
    autoSaveDashboard();
}

void MainWindow::updateGroupStats()
{
    int total = m_groups.size();
    int selected = 0;
    int members = 0;
    int pending = 0;

    for (int i = 0; i < m_groups.size() && i < m_groupTable->rowCount(); ++i) {
        FacebookGroup &g = m_groups[i];
        bool isSelected = false;
        if (auto *item = m_groupTable->item(i, 0))
            isSelected = item->checkState() == Qt::Checked;
        g.selected = isSelected;
        if (isSelected)
            ++selected;
        if (g.isMember)
            ++members;
        if (g.pending)
            ++pending;
    }

    m_lblGroupStats->setText(QStringLiteral("Tổng %1 · Đã chọn %2 · Thành viên %3 · Chờ duyệt %4")
                                 .arg(total)
                                 .arg(selected)
                                 .arg(members)
                                 .arg(pending));
    saveGroupsFile();
}

QVector<FacebookGroup> MainWindow::selectedGroups() const
{
    QVector<FacebookGroup> sel;
    for (int i = 0; i < m_groups.size() && i < m_groupTable->rowCount(); ++i) {
        bool checked = false;
        if (auto *item = m_groupTable->item(i, 0))
            checked = item->checkState() == Qt::Checked;
        if (checked)
            sel.append(m_groups.at(i));
    }
    return sel;
}

void MainWindow::updateCookieStatusLabel()
{
    int active = 0;
    int banned = 0;
    for (const FacebookAccount &a : m_config.accounts) {
        if (!a.cookieRaw.trimmed().isEmpty())
            ++active;
        if (a.status == QStringLiteral("bị chặn"))
            ++banned;
    }

    if (active == 0) {
        m_lblCookieStatus->setText(QStringLiteral("Tài khoản: Chưa có"));
        m_lblCookieStatus->setStyleSheet(QStringLiteral(
            "background-color: #ffffff; color: #d93025; border: 1px solid #f5c2c0; border-radius: 12px; padding: 3px 12px; font-weight: 600;"));
    } else {
        m_lblCookieStatus->setText(banned > 0
                                       ? QStringLiteral("Tài khoản: %1 (bị chặn %2)")
                                             .arg(active)
                                             .arg(banned)
                                       : QStringLiteral("Tài khoản: %1 hoạt động").arg(active));
        m_lblCookieStatus->setObjectName(banned > 0 ? QStringLiteral("chipRed")
                                                    : QStringLiteral("chipGreen"));
        m_lblCookieStatus->setStyleSheet(QString());
    }
}
