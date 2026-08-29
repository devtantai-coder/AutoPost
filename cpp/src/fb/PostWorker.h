#pragma once

#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include "model/FacebookAccount.h"
#include "model/FacebookGroup.h"
#include "proxy/ProxyHealth.h"
#include "model/RentedAccount.h"

struct PostRequest
{
    QString content;
    QStringList contents;
    QStringList images;
    QVector<FacebookGroup> groups;
    int retryCount = 2;
    int delaySec = 30;
    int threadCount = 1;
    int tabCount = 1;
    bool skipPostedToday = false;
    bool scheduleEnabled = false;
    QString scheduleTime;
    // Nhiều mốc giờ đăng (HH:mm); ưu tiên hơn scheduleTime khi bật lịch.
    QStringList scheduleTimes;
    // Pool proxy dùng để xoay vòng mỗi bài (áp dụng cho tài khoản chưa gắn proxy riêng).
    QStringList proxyPool;
    bool rotateProxyPerPost = false;
    int dailyLimit = 300;
    // Độ lệch ngẫu nhiên (giây) thêm vào nhịp chờ mỗi bài — tránh các tài khoản
    // đăng cùng một giây (tùy chọn).
    int jitterSec = 0;
    // Bảng sức khỏe proxy (kiểm tra định kỳ, tự bỏ qua proxy chết khi xoay
    // vòng). Có thể null — khi đó dùng logic proxy cũ.
    std::shared_ptr<ProxyHealth> proxyHealth;
    // Rải chéo: sắp lại thứ tự nhóm theo vòng tròn trước khi chia luồng để
    // nhóm kế tiếp do tài khoản khác đăng (acc A → nhóm 1, acc B → nhóm 2,
    // acc A → nhóm 3...), tránh một acc đăng liên tiếp nhiều nhóm.
    bool interleaveAccounts = true;
};
Q_DECLARE_METATYPE(PostRequest)

// Bảng cờ chia sẻ giữa các PostWorker: đánh dấu tài khoản nào đang được một luồng
// dùng, chống 2 trình duyệt đăng nhập cùng 1 tài khoản thuê song song.
struct AccountClaimTable
{
    mutable std::mutex mutex;
    std::vector<char> flags;
    // Số bài đã dùng của từng tài khoản — dùng chung giữa các luồng để đếm chính
    // xác khi nhiều luồng cùng xoay sang 1 tài khoản (không vượt gói, không ghi đè).
    std::vector<int> used;

    explicit AccountClaimTable(int n) : flags(n, 0), used(n, 0) {}

    bool claim(int idx)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (idx < 0 || idx >= static_cast<int>(flags.size()) || flags.at(idx) != 0)
            return false;
        flags[idx] = 1;
        return true;
    }

    void release(int idx)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (idx >= 0 && idx < static_cast<int>(flags.size()))
            flags[idx] = 0;
    }

    bool isClaimed(int idx) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return idx >= 0 && idx < static_cast<int>(flags.size()) && flags.at(idx) != 0;
    }

    void setUsed(int idx, int value)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (idx >= 0 && idx < static_cast<int>(used.size()))
            used[idx] = value;
    }

    int usedCount(int idx) const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return idx >= 0 && idx < static_cast<int>(used.size()) ? used.at(idx) : 0;
    }

    int incrementUsed(int idx)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (idx >= 0 && idx < static_cast<int>(used.size()))
            ++used[idx];
        return idx >= 0 && idx < static_cast<int>(used.size()) ? used.at(idx) : 0;
    }
};

// Chạy trên QThread riêng, tự quản lý một phiên Chrome (profile riêng).
// Mỗi tài khoản mở một trình duyệt; trong trình duyệt mở nhiều tab, mỗi tab
// chạy trên một thread con (TabWorker) để đăng song song nhiều nhóm cùng lúc.
// Khi tài khoản bị chặn, tự động đóng trình duyệt và xoay sang tài khoản khác.
class PostWorker : public QObject
{
    Q_OBJECT
public:
    using AccountClaims = std::shared_ptr<AccountClaimTable>;
    explicit PostWorker(int id, const AccountClaims &claims = {}, QObject *parent = nullptr);
    ~PostWorker() override;

public slots:
    void run(const PostRequest &req, const QString &cookieRaw, bool headless,
             const QVector<FacebookGroup> &slice, int baseIndex,
             const QVector<FacebookAccount> &accounts, bool rotateAccounts,
             int rotateFailThreshold, int startAccountIndex,
             const QVector<RentedAccount> &rented = {});
    void requestStop();

signals:
    void groupFinished(int globalIndex, bool ok);
    // Nhóm bị hoãn vì tài khoản đã đủ hạn mức bài/ngày (hôm sau đăng tiếp).
    void groupDeferred(int globalIndex);
    // Một bài đã xử lý với tài khoản cụ thể (đếm số lượng rải theo từng acc):
    // (accountId, tên hiển thị, thành công?).
    void accountPosted(const QString &accountId, const QString &accountName, bool ok);
    void accountStatusChanged(const QString &accountId, const QString &status);
    // Số bài đã dùng của tài khoản thuê tăng lên sau mỗi bài đăng thành công.
    void rentedQuotaUsed(const QString &rentedId, int used);
    void logMessage(const QString &line);
    void runFinished();

private:
    int m_id;
    std::atomic<bool> m_stop{false};
    std::shared_ptr<std::atomic<bool>> m_sharedStop;
    // Con trỏ xoay vòng proxy mỗi bài (thread-safe cho đa luồng).
    std::atomic<int> m_proxyCursor{0};
    AccountClaims m_claims;

    bool claimAccount(int idx);
    void releaseAccount(int idx);
    bool accountClaimed(int idx) const;
};
