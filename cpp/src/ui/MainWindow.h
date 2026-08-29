#pragma once

#include <QMainWindow>
#include <QStringList>
#include <QVector>

#include "store/Config.h"
#include "model/FacebookGroup.h"
#include "model/RentedAccount.h"
#include "fb/FbWorker.h"

class QTabWidget;
class QTextEdit;
class QTableWidget;
class QPushButton;
class QComboBox;
class QCheckBox;
class QSpinBox;
class QLineEdit;
class QLabel;
class QThread;
class QProgressBar;
class QTimeEdit;
class QListWidget;
class QStackedWidget;
class BarChart;
class DonutChart;
class QSystemTrayIcon;

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void chooseImages();
    void fetchMyGroups();
    void importGroups();
    void exportGroups();
    void addGroupsById();
    void applyPrivacyFilter();
    void selectAllGroups();
    void deselectAllGroups();
    void searchGroups();
    void loadJoinIdsJson();
    void startJoin();
    void startPosting();
    void stopPosting();
    void stopJoin();
    void openCookieManager();
    void clearLog();
    void appendLog(const QString &line);
    void flushLog();
    void onGroupsReady(const QVector<FacebookGroup> &groups);
    void onProgress(int current, int total, int success, int failed);
    void onAccountStatusChanged(const QString &accountId, const QString &status);
    void onRentedQuotaUsed(const QString &rentedId, int used);
    void onPostingDone();
    void onJoiningDone();

private slots:
    void refreshDashboard();
    void saveDashboardJson();
    void loadDashboardJson();

private:
    void autoSaveDashboard();
    void doBackup();
    void notify(const QString &title, const QString &msg);
    void setupTray();
    void buildUi();
    QWidget *buildDashboardTab();
    QWidget *buildHeroBanner();
    QWidget *buildPostTab();
    QWidget *buildGroupsTab();
    QWidget *buildRentedTab();
    QWidget *buildJoinTab();
    QWidget *buildProxyTab();
    QWidget *buildNurtureTab();
    QWidget *buildSettingsTab();
    QWidget *buildLogPanel();
    QWidget *buildControlPanel();

    void updateGroupsTable(const QVector<FacebookGroup> &groups);
    void updateGroupStats();
    QVector<FacebookGroup> selectedGroups() const;
    void updateCookieStatusLabel();
    void saveConfig();
    void runJoin(bool forceAutoJoin);
    void saveGroupsFile();
    void loadGroupsFile();
    void addRentedAccount();
    void editRentedAccount();
    void removeRentedAccount();
    void refreshRentedTable();
    void saveRentedFile();
    void loadRentedFile();

    QListWidget *m_nav = nullptr;
    QStackedWidget *m_pages = nullptr;
    QTextEdit *m_txtContent = nullptr;
    QPushButton *m_btnImg = nullptr;
    QLabel *m_lblImageCount = nullptr;
    QCheckBox *m_chkInterleave = nullptr;
    QStringList m_images;

    QTableWidget *m_groupTable = nullptr;
    QLabel *m_lblGroupStats = nullptr;
    QPushButton *m_btnFetchGroups = nullptr;
    QPushButton *m_btnAddGroupsById = nullptr;
    QComboBox *m_cmbPrivacyFilter = nullptr;
    QPushButton *m_btnSearchGroups = nullptr;
    QPushButton *m_btnStartJoin = nullptr;
    QPushButton *m_btnStopJoin = nullptr;
    QVector<FacebookGroup> m_groups;

    QTextEdit *m_txtSearchKeywords = nullptr;
    QTextEdit *m_txtJoinGroupIds = nullptr;
    QSpinBox *m_txtMaxGroups = nullptr;
    QSpinBox *m_txtJoinDelay = nullptr;
    QComboBox *m_cmbJoinAction = nullptr;
    QCheckBox *m_chkAutoJoin = nullptr;
    QCheckBox *m_chkSkipPrivate = nullptr;
    QCheckBox *m_chkSkipPending = nullptr;

    QSpinBox *m_txtDelay = nullptr;
    QComboBox *m_cmbDelayType = nullptr;
    QSpinBox *m_txtRetryCount = nullptr;
    QSpinBox *m_txtThreadCount = nullptr;
    QSpinBox *m_txtTabCount = nullptr;
    QPushButton *m_btnAutoTune = nullptr;
    QSpinBox *m_txtDailyLimit = nullptr;
    QCheckBox *m_chkRandomDelay = nullptr;
    QCheckBox *m_chkSkipPosted = nullptr;
    QCheckBox *m_chkSchedule = nullptr;
    QTimeEdit *m_timeSchedule = nullptr;
    QListWidget *m_timeList = nullptr;
    QPushButton *m_btnAddTime = nullptr;
    QPushButton *m_btnRemoveTime = nullptr;
    QCheckBox *m_chkRotateAccounts = nullptr;
    QSpinBox *m_txtRotateFail = nullptr;
    QCheckBox *m_chkHeadless = nullptr;
    QCheckBox *m_chkSaveSession = nullptr;
    QCheckBox *m_chkNotify = nullptr;
    QCheckBox *m_chkAutoBackup = nullptr;
    QComboBox *m_cmbNotifyMethod = nullptr;
    QLineEdit *m_txtTgToken = nullptr;
    QLineEdit *m_txtTgChatId = nullptr;
    QLineEdit *m_txtDiscord = nullptr;
    QSpinBox *m_txtJitter = nullptr;
    QTextEdit *m_txtProxyPool = nullptr;
    QCheckBox *m_chkRotateProxy = nullptr;
    QPushButton *m_btnTestProxies = nullptr;
    QListWidget *m_proxyTestResults = nullptr;

    QSpinBox *m_spinNurtureLikes = nullptr;
    QSpinBox *m_spinNurtureVideos = nullptr;
    QSpinBox *m_spinNurtureMinutes = nullptr;
    QPushButton *m_btnStartNurture = nullptr;
    QPushButton *m_btnStopNurture = nullptr;
    QLabel *m_lblNurtureAcc = nullptr;
    QListWidget *m_listNurtureResults = nullptr;
    QComboBox *m_cmbProxySource = nullptr;
    QPushButton *m_btnFetchProxies = nullptr;
    QLabel *m_lblProxyCount = nullptr;
    QCheckBox *m_chkAutoTestFetched = nullptr;
    QPushButton *m_btnCookie = nullptr;
    QLabel *m_lblCookieStatus = nullptr;

    QTableWidget *m_rentedTable = nullptr;
    QLabel *m_lblRentedStats = nullptr;
    QVector<RentedAccount> m_rented;
    QCheckBox *m_chkUseRented = nullptr;


    QTextEdit *m_txtLog = nullptr;
    QLabel *m_lblCounter = nullptr;
    QPushButton *m_btnStart = nullptr;
    QProgressBar *m_progressBar = nullptr;
    QTimer *m_logFlushTimer = nullptr;
    QVector<QString> m_pendingLog;

    QLabel *m_dashPosts = nullptr;
    QLabel *m_dashSuccess = nullptr;
    QLabel *m_dashFailed = nullptr;
    QLabel *m_dashAccounts = nullptr;
    QLabel *m_dashGroups = nullptr;
    QVector<QLabel *> m_dashCaptions;
    QTableWidget *m_dashAccountTable = nullptr;
    QTableWidget *m_dashHistoryTable = nullptr;
    QListWidget *m_dashRecent = nullptr;
    BarChart *m_dashChart = nullptr;
    DonutChart *m_dashDonut = nullptr;
    QLabel *m_heroTotal = nullptr;
    QLabel *m_heroRate = nullptr;
    QLabel *m_heroTrend = nullptr;
    QProgressBar *m_heroBar = nullptr;
    QTimer *m_autoSaveTimer = nullptr;
    QTimer *m_backupTimer = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    int m_lastTotal = 0;
    int m_lastSuccess = 0;
    int m_lastFailed = 0;

    Config m_config;
    QThread *m_workerThread = nullptr;
    FbWorker *m_worker = nullptr;
};
