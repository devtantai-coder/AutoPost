#pragma once

#include <QDateTime>
#include <QHash>
#include <QMap>
#include <QMetaType>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>

#include "model/FacebookAccount.h"
#include "model/FacebookGroup.h"
#include "model/RentedAccount.h"
#include "fb/PostWorker.h"

struct JoinRequest
{
    QString keywords;
    QStringList joinGroupIds;
    int maxGroups = 50;
    int joinDelaySec = 10;
    QString joinAction;
    bool autoJoin = false;
    bool skipPrivate = true;
    bool skipPending = true;
};
Q_DECLARE_METATYPE(JoinRequest)

struct NurtureRequest
{
    int maxLikes = 12;
    int maxVideos = 3;
    int maxDurationSec = 300;
};
Q_DECLARE_METATYPE(NurtureRequest)

// Chạy trong QThread riêng để không làm treo giao diện.
// Đăng bài được chia nhỏ và xử lý song song bằng nhiều PostWorker.
class FbWorker : public QObject
{
    Q_OBJECT
public:
    explicit FbWorker(QObject *parent = nullptr);
    ~FbWorker() override;

public slots:
    void fetchMyGroups(const QString &cookieRaw, bool headless);
    void searchAndJoin(const JoinRequest &req, const QString &cookieRaw, bool headless);
    void startPosting(PostRequest req, const QString &cookieRaw, bool headless,
                      const QVector<FacebookAccount> &accounts, bool rotateAccounts,
                      int rotateFailThreshold);
    // Đăng bài bằng tài khoản thuê: mỗi tài khoản có cookie + nội dung bài riêng,
    // bị chặn/hết bài tự chuyển sang tài khoản kế tiếp.
    void startRented(PostRequest req, bool headless, const QVector<RentedAccount> &rented);
    // Kiểm tra khả năng kết nối của từng proxy trong danh sách (chạy nền).
    void testProxies(const QStringList &proxyLines);
    // Tải proxy miễn phí từ nguồn công khai (proxyscrape | geonode | proxy-list.download)
    // rồi trả danh sách đã lọc trùng (chạy nền, không chặn UI).
    void fetchFreeProxies(const QString &source);
    // Nuôi acc (account warming): từng tài khoản đăng nhập, lướt feed tự nhiên,
    // like ngẫu nhiên, xem video ngắn để tránh bị chặn acc mới.
    void startNurture(NurtureRequest req, bool headless,
                      const QVector<FacebookAccount> &accounts);
    void requestStop();

signals:
    void logMessage(const QString &line);
    void groupsReady(const QVector<FacebookGroup> &groups);
    void progressUpdated(int current, int total, int success, int failed);
    void accountStatusChanged(const QString &accountId, const QString &status);
    // Số bài đã dùng của tài khoản thuê tăng lên sau mỗi bài đăng thành công.
    void rentedQuotaUsed(const QString &rentedId, int used);
    void postingDone();
    void joiningDone();
    // Kết quả kiểm tra proxy: (index, ok, chuỗi proxy gốc).
    void proxyTestResult(int index, bool ok, const QString &proxy);
    // Danh sách proxy vừa tải từ web (đã lọc trùng).
    void proxiesFetched(const QStringList &proxies, const QString &source);
    // Nuôi acc hoàn tất (đã xử lý xong tất cả tài khoản).
    void nurtureDone(int ok, int failed);

private slots:
    void onWorkerGroupFinished(int globalIndex, bool ok);
    void onWorkerGroupDeferred(int globalIndex);
    void onWorkerRunFinished();

private:
    void shutdownWorkers();
    // Phần lõi dùng chung cho đăng thường và đăng bằng tài khoản thuê.
    void startPostingImpl(PostRequest req, const QString &cookieRaw, bool headless,
                          const QVector<FacebookAccount> &accounts, bool rotateAccounts,
                          int rotateFailThreshold, const QVector<RentedAccount> &rented);
    std::atomic<bool> m_stop{false};

    QVector<QThread *> m_workerThreads;
    QVector<PostWorker *> m_workers;
    int m_activeWorkers = 0;
    int m_totalGroups = 0;
    int m_done = 0;
    int m_success = 0;
    int m_failed = 0;
    // 1 = thành công, 0 = thất bại, -1 = hoãn (đủ hạn mức bài/ngày của tài khoản).
    QMap<int, int> m_groupStatus;
    QVector<FacebookGroup> m_runGroups;
    QHash<int, QDateTime> m_groupTimes;
    // Số lượng bài đã rải theo từng tài khoản: id -> (thành công, thất bại).
    QHash<QString, QPair<int, int>> m_accountStats;
    QHash<QString, QString> m_accountNames;
    // Các tài khoản đã bị chặn/checkpoint trong phiên chạy hiện tại (cầu chì
    // chống ban: 3+ acc chết trong 1 phiên -> tự dừng bảo vệ dàn acc còn lại).
    QSet<QString> m_bannedAccounts;
};
