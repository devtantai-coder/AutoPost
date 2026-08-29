#pragma once

#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>
#include <memory>

#include "cdp/Fingerprint.h"
#include "model/FacebookGroup.h"

// Lô nhóm chỉ đọc, CHUNG cho mọi tab của một trình duyệt: một vùng nhớ duy
// nhất (shared_ptr<const>) thay vì sao chép toàn bộ danh sách nhóm vào hàng đợi
// invokeMethod của TỪNG tab — với 5 trình duyệt × 10 tab, việc sao chép vector
// nhóm (mỗi phần tử chứa nhiều QString) không còn nhân lên 50 lần.
using GroupBatch = std::shared_ptr<const QVector<FacebookGroup>>;
using IndexBatch = std::shared_ptr<const QVector<int>>;
Q_DECLARE_METATYPE(GroupBatch)
Q_DECLARE_METATYPE(IndexBatch)

// Chạy trên một thread riêng, quản lý MỘT tab trong trình duyệt (một
// CdpClient + WebDriver kết nối tới tab đó). Đăng bài cho danh sách nhóm bằng
// cơ chế work-stealing: các tab anh em cùng bốc nhóm từ một chỉ số chung, nên
// không có tab nào rảnh khi tab khác còn việc.
// Nếu phát hiện tài khoản bị chặn: đặt cờ chia sẻ để các tab anh em dừng,
// phát tabBanned rồi thoát.
class TabWorker : public QObject
{
    Q_OBJECT
public:
    TabWorker(int workerId, int tabId, const std::shared_ptr<std::atomic<bool>> &stop,
              QObject *parent = nullptr);
    ~TabWorker() override;

public slots:
    void run(const GroupBatch &pending, const IndexBatch &pendingOrig,
             const std::shared_ptr<std::atomic<int>> &nextIdx, const QString &wsUrl,
             const QString &userAgent, const QStringList &contents, const QString &accountId,
             const QString &accountName, const QStringList &images, int retryCount, int delaySec,
             int jitterSec, int winWidth, int winHeight);
    void requestStop();

signals:
    void groupFinished(int globalIndex, bool ok);
    void tabBanned();
    void logMessage(const QString &line);
    void runFinished();

private:
    int m_workerId;
    int m_tabId;
    std::shared_ptr<std::atomic<bool>> m_sharedStop;
    std::atomic<bool> m_stop{false};
};
