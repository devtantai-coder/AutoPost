#include "fb/TabWorker.h"

#include "cdp/CdpClient.h"
#include "cdp/WebDriver.h"
#include "utils/Logger.h"
#include "utils/TemplateEngine.h"
#include "utils/Utils.h"
#include "cdp/Fingerprint.h"
#include "store/DailyPostLog.h"
#include "fb/PostEngine.h"

#include <QDateTime>
#include <QJsonObject>
#include <QThread>

TabWorker::TabWorker(int workerId, int tabId,
                     const std::shared_ptr<std::atomic<bool>> &stop, QObject *parent)
    : QObject(parent)
    , m_workerId(workerId)
    , m_tabId(tabId)
    , m_sharedStop(stop)
{
}

TabWorker::~TabWorker() = default;

void TabWorker::requestStop()
{
    m_stop.store(true);
}

void TabWorker::run(const GroupBatch &pendingPtr, const IndexBatch &pendingOrigPtr,
                    const std::shared_ptr<std::atomic<int>> &nextIdx, const QString &wsUrl,
                    const QString &userAgent, const QStringList &contents,
                    const QString &accountId, const QString &accountName, const QStringList &images,
                    int retryCount, int delaySec, int jitterSec, int winWidth, int winHeight)
{
    m_stop.store(false);

    // Lô nhóm là bản chỉ đọc dùng chung (shared_ptr<const>) giữa mọi tab — không
    // sao chép dữ liệu khi qua hàng đợi tín hiệu.
    const QVector<FacebookGroup> &pending = *pendingPtr;
    const QVector<int> &pendingOrig = *pendingOrigPtr;

    auto log = [this](const QString &msg) {
        emit logMessage(QStringLiteral("[Luồng %1 · Tab %2] %3")
                            .arg(m_workerId)
                            .arg(m_tabId)
                            .arg(msg));
    };

    // CdpClient + WebDriver được tạo ngay trong thread này để WebSocket/CDP
    // có đúng thread affinity của tab.
    CdpClient cdp;
    WebDriver driver;

    if (wsUrl.isEmpty() || !cdp.connectToUrl(wsUrl, 10000)) {
        log(QStringLiteral("Không kết nối được tab"));
        for (int idx : pendingOrig)
            emit groupFinished(idx, false);
        emit runFinished();
        return;
    }

    driver.init(&cdp);
    driver.enableDomains();
    driver.initPageHooks();
    // Vân tay của tab PHẢI khớp chính xác với phiên chính:
    //  - Tài khoản thật: persona determinist từ accountId (cùng acc mọi tab cùng vân tay).
    //  - Acc "mặc định": persona determinist từ UA của phiên — trước đây mỗi tab tự
    //    random WebGL/cores/memory riêng: cùng một máy mà GPU/CPU đổi theo từng tab
    //    là điểm machine rõ ràng với Facebook.
    const bool hasRealAccount = !accountId.isEmpty() && accountId != QStringLiteral("mặc định");
    const Fingerprint fp = hasRealAccount
                               ? Fingerprints::forAccount(accountId)
                               : Fingerprints::forDefaultUa(userAgent, winWidth, winHeight);
    const bool fpValid = !fp.userAgent.isEmpty();
    const int fpW = fpValid && fp.screenWidth > 0 ? fp.screenWidth : winWidth;
    const int fpH = fpValid && fp.screenHeight > 0 ? fp.screenHeight : winHeight;
    const QString fpUa = fpValid ? fp.userAgent : userAgent;
    const QString fpPlatform = fpValid && !fp.platform.isEmpty()
                                    ? fp.platform
                                    : Utils::platformForUserAgent(fpUa);
    const QStringList fpLangs = fpValid && !fp.languages.isEmpty()
                                    ? fp.languages
                                    : QStringList{QStringLiteral("vi-VN"), QStringLiteral("vi"),
                                                 QStringLiteral("en-US"), QStringLiteral("en")};
    const QString fpVendor = fpValid && !fp.webglVendor.isEmpty() ? fp.webglVendor
                                                                  : Utils::randomWebglVendor();
    const QString fpRenderer = fpValid && !fp.webglRenderer.isEmpty() ? fp.webglRenderer
                                                                      : Utils::randomWebglRenderer();
    const int fpCores = fpValid && fp.hardwareConcurrency > 0 ? fp.hardwareConcurrency
                                                              : Utils::randomHardwareConcurrency();
    const int fpMemory = fpValid && fp.deviceMemory > 0 ? fp.deviceMemory
                                                        : Utils::randomDeviceMemory();
    const QString fpAccept = fpValid && !fp.acceptLanguage.isEmpty()
                                 ? fp.acceptLanguage
                                 : QStringLiteral("vi-VN,vi;q=0.9,en-US;q=0.8,en;q=0.7");
    const QString fpLocale = fpValid && !fp.locale.isEmpty() ? fp.locale : QStringLiteral("vi-VN");
    const QString fpTz = fpValid && !fp.timezone.isEmpty() ? fp.timezone
                                                           : QStringLiteral("Asia/Ho_Chi_Minh");
    // Khởi tạo tab: setScreenMetrics phải giữ chờ phản hồi vì tọa độ chuột
    // (viết/click) phụ thuộc viewport đã áp; các override còn lại không cần chờ
    // (CDP FIFO) -> tiếp tục dựng tab nhanh hơn.
    driver.setScreenMetrics(fpW, fpH);
    driver.addAntiDetectionScript(fpUa, fpPlatform, fpW, fpH, fpLangs, fpVendor, fpRenderer, fpCores,
                                  fpMemory, fp.seed);
    driver.setUserAgentOverride(fpUa, fpAccept, false);
    driver.setTimezone(fpTz, false);
    driver.setLocale(fpLocale, false);

    QJsonObject perm;
    perm.insert(QStringLiteral("permission"),
                QJsonObject{{QStringLiteral("name"), QStringLiteral("notifications")}});
    perm.insert(QStringLiteral("setting"), QStringLiteral("denied"));
    perm.insert(QStringLiteral("origin"), QStringLiteral("https://www.facebook.com"));
    cdp.sendCommand(QStringLiteral("Browser.setPermission"), perm, 30000, false);

    // Cookie đã được tiêm vào trình duyệt (cookie jar dùng chung cho mọi tab),
    // không cần tải facebook.com trước: PostEngine tự điều hướng thẳng tới nhóm
    // đầu tiên, tiết kiệm 1 lần tải trang + chờ mỗi tab.
    // (Bỏ humanPause khởi đầu 1-2s/tab: CDP đã sẵn sàng, PostEngine đã có đủ
    // nhịp "người" trong từng bước; với 10 tab đây là 1-2s chết trên MỌI bài.)

    PostEngine engine(&driver);

    // Work-stealing: mọi tab cùng bốc nhóm từ một chỉ số chung -> luôn bận rộn.
    for (;;) {
        const int i = nextIdx->fetch_add(1);
        if (i >= pending.size())
            break;
        if (m_stop.load() || m_sharedStop->load()) {
            emit groupFinished(pendingOrig.at(i), false);
            continue;
        }

        const FacebookGroup &g = pending.at(i);
        log(QStringLiteral("Đang xử lý %1: %2").arg(g.name));

        // Xoay vòng nhiều nội dung: 1 nội dung -> dùng luôn; nhiều nội dung ->
        // chọn ngẫu nhiên mỗi bài để tránh chuỗi trùng lặp.
        QString content = contents.isEmpty() ? QString()
                            : (contents.size() == 1 ? contents.first()
                                                    : contents.at(Utils::randomInt(0, contents.size() - 1)));
        // Thay biến {{ten}}, {{group}}, {{ngay}}, {{gio}}... trước khi đăng.
        content = TemplateEngine::expand(content, g.name, accountName, QDateTime::currentDateTime());

        const PostResult r = engine.postToGroupWithRetry(g.id, content, images, retryCount, &m_stop);
        if (r == PostResult::Banned) {
            m_sharedStop->store(true);
            log(QStringLiteral("Tài khoản bị chặn, dừng tab và báo xoay tài khoản"));
            emit tabBanned();
            // Các nhóm còn lại (chưa bốc hoặc đang chờ) sẽ được thử lại ở vòng sau.
            emit groupFinished(pendingOrig.at(i), false);
            break;
        }

        const bool ok = (r == PostResult::Ok);
        emit groupFinished(pendingOrig.at(i), ok);
        // Nhật ký riêng từng bài theo tên (nhat_ky_<tên tài khoản>_ngày.log).
        DailyPostLog::logPost(accountId, accountName, g.name, ok);
        log(ok ? QStringLiteral("Thành công: %1").arg(g.name)
               : QStringLiteral("Thất bại: %1").arg(g.name));

        if (nextIdx->load() < (int)pending.size() && !m_stop.load() && !m_sharedStop->load()) {
            // Nhịp chờ giữa các bài theo phân phối exponential quanh delay cấu
            // hình (tập trung quanh giá trị đặt nhưng đôi khi dài hơn — giống
            // người thật, không phải khoảng đồng đều máy móc). Thêm độ lệch
            // ngẫu nhiên (jitter) để mỗi bài / mỗi tab rơi vào thời điểm khác
            // nhau, tránh mọi tài khoản đăng cùng một giây.
            if (jitterSec > 0)
                Utils::humanPause(0, Utils::randomInt(0, jitterSec * 1000));
            const int totalMs = Utils::exponentialDelayMs(delaySec * 1000, delaySec * 3000);
            int waited = 0;
            while (waited < totalMs && !m_stop.load() && !m_sharedStop->load()) {
                Utils::sleepMs(200);
                waited += 200;
            }
        }
    }

    // Nhả trang Facebook nặng (DOM + JS timer) khi tab xong việc: navigate về
    // about:blank trước khi ngắt kết nối để RAM của tab không bị giữ lại trong
    // lúc tab rảnh giữa các vòng thử lại (kết hợp tắt BackForwardCache ở
    // ChromeLauncher để trang cũ không bị cache giữ trong bộ nhớ).
    if (cdp.isConnected()) {
        QJsonObject blank{{QStringLiteral("url"), QStringLiteral("about:blank")}};
        cdp.sendCommand(QStringLiteral("Page.navigate"), blank, 3000);
    }
    cdp.disconnectSocket();
    log(QStringLiteral("Hoàn tất"));
    emit runFinished();
}
