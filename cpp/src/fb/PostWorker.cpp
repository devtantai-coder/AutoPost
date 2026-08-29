#include "fb/PostWorker.h"

#include "fb/FacebookSession.h"
#include "fb/TabWorker.h"
#include "store/DailyPostLog.h"
#include "utils/Logger.h"
#include "proxy/Proxy.h"
#include "utils/Utils.h"

#include <QEventLoop>
#include <QSet>
#include <QThread>

PostWorker::PostWorker(int id, const AccountClaims &claims, QObject *parent)
    : QObject(parent)
    , m_id(id)
    , m_sharedStop(std::make_shared<std::atomic<bool>>(false))
    , m_claims(claims)
{
}

PostWorker::~PostWorker() = default;

bool PostWorker::claimAccount(int idx)
{
    // Không có bảng cờ (chạy đơn lẻ) thì cho phép dùng.
    return !m_claims || m_claims->claim(idx);
}

void PostWorker::releaseAccount(int idx)
{
    if (m_claims)
        m_claims->release(idx);
}

bool PostWorker::accountClaimed(int idx) const
{
    return m_claims && m_claims->isClaimed(idx);
}

void PostWorker::requestStop()
{
    m_stop.store(true);
    m_sharedStop->store(true);
}

void PostWorker::run(const PostRequest &req, const QString &cookieRaw, bool headless,
                     const QVector<FacebookGroup> &slice, int baseIndex,
                     const QVector<FacebookAccount> &accounts, bool rotateAccounts,
                     int rotateFailThreshold, int startAccountIndex,
                     const QVector<RentedAccount> &rented)
{
    m_stop.store(false);
    m_sharedStop->store(false);
    m_proxyCursor.store(0);

    // Chế độ tài khoản thuê: mỗi tài khoản có cookie, nội dung bài (chữ + ảnh)
    // và gói bài riêng. Bị chặn hoặc hết bài -> tự chuyển sang tài khoản kế tiếp.
    const bool rentedMode = !rented.isEmpty();
    const int threshold = qMax(1, rotateFailThreshold);
    QVector<FacebookAccount> accs = accounts;
    QVector<int> usedPosts;
    if (rentedMode) {
        accs.clear();
        usedPosts.reserve(rented.size());
        for (const RentedAccount &r : rented) {
            FacebookAccount a;
            a.id = r.id;
            a.name = r.name;
            a.cookieRaw = r.cookieRaw;
            a.proxy = r.proxy;
            a.status = r.status;
            a.failCount = 0;
            a.selected = r.selected;
            accs.append(a);
            usedPosts.append(r.used);
        }
    }
    const int nAccounts = accs.size();
    int accountIdx = nAccounts > 0 ? qBound(0, startAccountIndex, nAccounts - 1) : 0;

    auto accountCookie = [&](int idx) -> QString {
        if (nAccounts > 0)
            return accs.at(idx).cookieRaw.trimmed();
        return cookieRaw.trimmed();
    };

    // Số bài tài khoản còn được đăng hôm nay (tách riêng theo từng tài khoản/khách thuê).
    auto accountIdOf = [&](int idx) -> QString {
        return nAccounts > 0 ? accs.at(idx).id : QStringLiteral("default");
    };
    // Số bài đã dùng: đọc từ bảng dùng chung (nếu có) để nhiều luồng đếm chính xác.
    auto usedOf = [&](int idx) -> int {
        if (m_claims)
            return m_claims->usedCount(idx);
        return idx >= 0 && idx < usedPosts.size() ? usedPosts.at(idx) : 0;
    };
    auto remainingFor = [&](int idx) -> int {
        if (rentedMode) {
            if (idx < 0 || idx >= rented.size())
                return 0;
            return qMax(0, rented.at(idx).totalPosts - usedOf(idx));
        }
        if (req.dailyLimit <= 0)
            return 1000000000;
        return qMax(0, req.dailyLimit - DailyPostLog::countToday(accountIdOf(idx)));
    };
    // Nội dung bài đăng dùng cho tài khoản idx (chế độ thuê: nội dung riêng của
    // tài khoản, nếu trống thì dùng nội dung chung từ trang Đăng bài).
    auto contentFor = [&](int idx) -> QStringList {
        if (rentedMode) {
            const QString t = rented.at(idx).postText.trimmed();
            if (!t.isEmpty())
                return QStringList{t};
        }
        return req.contents;
    };
    auto imagesFor = [&](int idx) -> QStringList {
        return rentedMode ? rented.at(idx).images : req.images;
    };
    // Proxy đã cấu hình hợp lệ (hoặc để trống). Proxy sai định dạng -> coi như không dùng được.
    auto proxyUsable = [&](int idx) -> bool {
        const QString &p = accs.at(idx).proxy;
        if (p.trimmed().isEmpty())
            return true;
        return parseProxy(p).valid;
    };

    // Tài khoản tiếp theo còn dùng được (bỏ qua tài khoản đã bị chặn, cookie trống,
    // hoặc đã đủ hạn mức bài/ngày).
    auto nextUsableAccount = [&](int from) -> int {
        for (int step = 1; step < nAccounts; ++step) {
            const int idx = (from + step) % nAccounts;
            if (accs.at(idx).status != QStringLiteral("bị chặn") &&
                !accs.at(idx).cookieRaw.trimmed().isEmpty() &&
                proxyUsable(idx) && remainingFor(idx) > 0 && !accountClaimed(idx))
                return idx;
        }
        return from;
    };

    if (nAccounts > 1 &&
        (accs.at(accountIdx).status == QStringLiteral("bị chặn") ||
         accs.at(accountIdx).cookieRaw.trimmed().isEmpty() || !proxyUsable(accountIdx) ||
         remainingFor(accountIdx) <= 0))
        accountIdx = nextUsableAccount(accountIdx);

    // results[i]: 1 = thành công, 0 = thất bại, -1 = chưa thử.
    // Mỗi vòng đều gửi kết quả của từng nhóm cho FbWorker; FbWorker giữ kết quả
    // cuối cùng (vòng sau có thể thử lại nhóm thất bại ở vòng trước).
    QVector<int> results(slice.size(), -1);
    QVector<int> groupFails(slice.size(), 0);

    const int maxRounds = nAccounts > 1 && (rotateAccounts || rentedMode) ? nAccounts : 1;
    int rounds = 0;

    // Tái sử dụng 1 trình duyệt (và các tab) cho mọi vòng thử lại cùng một tài
    // khoản; chỉ khởi động trình duyệt mới khi xoay sang tài khoản khác.
    FacebookSession session;
    int sessionAccount = -1;
    QVector<QString> tabUrls;

    auto closeSession = [&]() {
        if (sessionAccount >= 0) {
            releaseAccount(sessionAccount);
            session.stop();
        }
        sessionAccount = -1;
        tabUrls.clear();
    };

    auto startSessionFor = [&](int idx) -> bool {
        QString error;
        session.setProfileSuffix(QStringLiteral("-%1-%2").arg(m_id).arg(idx));

        // Giữ cờ "đang dùng" để không luồng khác đăng nhập cùng tài khoản này.
        if (!claimAccount(idx)) {
            emit logMessage(QStringLiteral("[Luồng %1] Tài khoản \"%2\" đang được luồng khác dùng, bỏ qua")
                                .arg(m_id)
                                .arg(accs.at(idx).name));
            return false;
        }

        // Xoay proxy mỗi bài: nếu bật và tài khoản này chưa gắn proxy riêng,
        // lấy proxy "còn sống" kế tiếp trong pool (bỏ qua proxy chết nếu có
        // bảng sức khỏe proxy).
        QString proxy = nAccounts > 0 ? accs.at(idx).proxy.trimmed() : QString();
        if (proxy.isEmpty() && req.rotateProxyPerPost && !req.proxyPool.isEmpty()) {
            const int pi = m_proxyCursor.fetch_add(1);
            if (req.proxyHealth)
                proxy = req.proxyHealth->nextAlive(req.proxyPool, pi);
            else {
                const QString candidate = req.proxyPool.at(pi % req.proxyPool.size()).trimmed();
                proxy = parseProxy(candidate).valid ? candidate : QString();
            }
        }
        // Nếu proxy riêng của tài khoản bị phát hiện chết, thử lấy proxy khác
        // từ pool trước khi chấp nhận thất bại.
        if (req.proxyHealth && !proxy.isEmpty() && !req.proxyHealth->isAlive(proxy) &&
            !req.proxyPool.isEmpty()) {
            proxy = req.proxyHealth->nextAlive(req.proxyPool, m_proxyCursor.fetch_add(1));
        }
        session.setProxy(proxy);
        // Persona cố định theo tài khoản: 2 acc cùng IP/thiết bị -> 2 vân tay
        // khác nhau, nhưng mỗi acc giữ nguyên vân tay qua các lần chạy.
        if (nAccounts > 0)
            session.setFingerprintSeed(accs.at(idx).id);
        if (!proxy.isEmpty())
            emit logMessage(QStringLiteral("[Luồng %1] Tài khoản \"%2\" đang dùng proxy %3")
                                .arg(m_id)
                                .arg(accs.at(idx).name)
                                .arg(proxy));
        if (!session.start(accountCookie(idx), headless, &error)) {
            releaseAccount(idx);
            emit logMessage(QStringLiteral("[Luồng %1] Lỗi khởi tạo trình duyệt: %2")
                                .arg(m_id).arg(error));
            return false;
        }
        sessionAccount = idx;
        return true;
    };

    while (rounds < maxRounds && !m_stop.load()) {
        ++rounds;
        m_sharedStop->store(false);

        // Các nhóm chưa thành công được đưa vào vòng này.
        QVector<FacebookGroup> pending;
        QVector<int> pendingOrig;
        for (int i = 0; i < slice.size(); ++i) {
            if (results.at(i) != 1) {
                pending.append(slice.at(i));
                pendingOrig.append(i);
            }
        }
        if (pending.isEmpty())
            break;
        // Xáo trộn thứ tự mỗi vòng: các tab đăng nhóm theo thứ tự ngẫu nhiên,
        // tránh pattern tuần tự giống nhau qua mọi lần chạy.
        Utils::shuffle(pendingOrig);
        pending.clear();
        for (int idx : pendingOrig)
            pending.append(slice.at(idx));

        // Hạn mức bài theo TỪNG TÀI KHOẢN (tách riêng theo tên khách thuê):
        // chỉ đăng số nhóm còn trong hạn mức của tài khoản, nhóm thừa để hôm sau.
        QVector<int> deferredOrig;
        if (req.dailyLimit > 0 || rentedMode) {
            const int remaining = remainingFor(accountIdx);
            if (remaining <= 0) {
                emit logMessage(QStringLiteral("[Luồng %1] Tài khoản \"%2\" đã hết hạn mức bài (%3), đang chuyển tài khoản kế tiếp...")
                                    .arg(m_id)
                                    .arg(nAccounts > 0 ? accs.at(accountIdx).name
                                                       : QStringLiteral("mặc định"))
                                    .arg(rentedMode ? QString::number(rented.at(accountIdx).totalPosts)
                                                    : QString::number(req.dailyLimit)));
                if (nAccounts <= 1)
                    break;
                const int next = nextUsableAccount(accountIdx);
                if (next == accountIdx)
                    break;
                accountIdx = next;
                Utils::humanPause(3000, 6000);
                continue;
            }
            if (pending.size() > remaining) {
                deferredOrig = pendingOrig.mid(remaining);
                pendingOrig.resize(remaining);
                pending.resize(remaining);
                emit logMessage(rentedMode
                                    ? QStringLiteral("[Luồng %1] Tài khoản thuê \"%2\" chỉ còn %3 bài trong gói, %4 nhóm để tài khoản khác đăng tiếp.")
                                          .arg(m_id)
                                          .arg(nAccounts > 0 ? accs.at(accountIdx).name
                                                             : QStringLiteral("mặc định"))
                                          .arg(remaining)
                                          .arg(deferredOrig.size())
                                    : QStringLiteral("[Luồng %1] Tài khoản \"%2\" chỉ còn %3 bài hôm nay, %4 nhóm để hôm sau đăng tiếp.")
                                          .arg(m_id)
                                          .arg(nAccounts > 0 ? accs.at(accountIdx).name
                                                             : QStringLiteral("mặc định"))
                                          .arg(remaining)
                                          .arg(deferredOrig.size()));
            }
        }

        // Khởi tạo lại trình duyệt khi chưa có hoặc khi tài khoản vừa xoay.
        if (sessionAccount != accountIdx) {
            closeSession();
            if (!startSessionFor(accountIdx)) {
                for (int i = 0; i < pending.size(); ++i) {
                    emit groupFinished(baseIndex + pendingOrig.at(i), false);
                    emit accountPosted(
                        nAccounts > 0 ? accs.at(accountIdx).id : QStringLiteral("default"),
                        nAccounts > 0 ? accs.at(accountIdx).name : QStringLiteral("mặc định"),
                        false);
                }
                break;
            }
            Utils::humanPause(1500, 3000);
            // Stagger khởi đầu theo chỉ số tài khoản: mỗi tài khoản bắt đầu trễ
            // thêm (chỉ số * jitterSec) giây để không đăng cùng một giây (tùy chọn).
            if (req.jitterSec > 0 && accountIdx >= 0)
                Utils::humanPause(0, qBound(0, accountIdx, 30) * req.jitterSec * 1000);
        }

        emit logMessage(QStringLiteral("[Luồng %1] Tài khoản \"%2\" · %3 nhóm còn lại · %4 tab")
                            .arg(m_id)
                            .arg(nAccounts > 0 ? accs.at(accountIdx).name
                                               : QStringLiteral("mặc định"))
                            .arg(pending.size())
                            .arg(tabUrls.isEmpty() ? qMin(req.tabCount, pending.size())
                                                   : tabUrls.size()));

        // Mở tab hàng loạt (1 đợt duy nhất) + resolve wsUrl gộp 1 lần HTTP; vòng
        // sau tái dùng tab đã có. Trước đây dùng chuỗi openNewTab(tm) = chờ HTTP
        // N lần liên tiếp -> mất nhiều giây khi mở 3-10 tab.
        const int tabCount = qBound(1, req.tabCount, pending.size());
        if (tabUrls.size() < tabCount) {
            const QStringList ws = session.openNewTabs(tabCount - tabUrls.size());
            tabUrls.append(ws);
        }
        if (tabUrls.isEmpty()) {
            emit logMessage(QStringLiteral("[Luồng %1] Không mở được tab nào, nhóm còn lại thất bại")
                                .arg(m_id));
            for (int i = 0; i < pending.size(); ++i) {
                const int gi = pendingOrig.at(i);
                results[gi] = 0;
                emit groupFinished(baseIndex + gi, false);
                emit accountPosted(
                    nAccounts > 0 ? accs.at(accountIdx).id : QStringLiteral("default"),
                    nAccounts > 0 ? accs.at(accountIdx).name : QStringLiteral("mặc định"),
                    false);
            }
            closeSession();
            break;
        }
        const int useTabs = qMin(tabCount, tabUrls.size());

        // Work-stealing: mọi tab bốc nhóm từ một chỉ số chung -> cân bằng tải.
        // Lô nhóm được đóng gói MỘT lần dưới dạng chỉ đọc chia sẻ (shared_ptr<const>)
        // và truyền tham chiếu tới TỪNG tab — tránh sao chép toàn bộ vector nhóm
        // vào hàng đợi invokeMethod của từng tab (5 browser × 10 tab = 50 bản sao).
        auto nextIdx = std::make_shared<std::atomic<int>>(0);
        const auto pendingBatch =
            std::make_shared<const QVector<FacebookGroup>>(std::move(pending));
        const auto pendingOrigBatch =
            std::make_shared<const QVector<int>>(std::move(pendingOrig));
        QVector<QThread *> tabThreads;
        QVector<TabWorker *> tabs;
        QEventLoop loop;
        int activeTabs = 0;
        bool bannedRound = false;

        for (int t = 0; t < useTabs; ++t) {
            ++activeTabs;

            auto *thread = new QThread(this);
            auto *tab = new TabWorker(m_id, t, m_sharedStop);
            tab->moveToThread(thread);

            connect(tab, &TabWorker::groupFinished, this,
                    [this, &results, &usedPosts, &accs, baseIndex, accountIdx, nAccounts,
                     rentedMode](int gi, bool ok) {
                        results[gi] = ok ? 1 : 0;
                        // Trừ 1 bài khi đăng thành công: đếm dùng chung giữa các luồng
                        // để không vượt gói và không ghi đè sai số đã dùng.
                        if (ok && rentedMode && accountIdx >= 0 && accountIdx < accs.size()) {
                            int newUsed = 0;
                            if (m_claims)
                                newUsed = m_claims->incrementUsed(accountIdx);
                            else if (accountIdx < usedPosts.size())
                                newUsed = ++usedPosts[accountIdx];
                            emit this->rentedQuotaUsed(accs.at(accountIdx).id, newUsed);
                        }
                        emit this->groupFinished(baseIndex + gi, ok);
                        // Đếm số lượng rải theo từng tài khoản (báo cáo cuối phiên).
                        emit this->accountPosted(
                            nAccounts > 0 && accountIdx >= 0 && accountIdx < accs.size()
                                ? accs.at(accountIdx).id
                                : QStringLiteral("default"),
                            nAccounts > 0 && accountIdx >= 0 && accountIdx < accs.size()
                                ? accs.at(accountIdx).name
                                : QStringLiteral("mặc định"),
                            ok);
                    });
            connect(tab, &TabWorker::tabBanned, this, [&bannedRound]() {
                bannedRound = true;
            });
            connect(tab, &TabWorker::logMessage, this, &PostWorker::logMessage);
            connect(tab, &TabWorker::runFinished, this,
                    [&activeTabs, &loop]() {
                        --activeTabs;
                        if (activeTabs <= 0)
                            loop.quit();
                    });

            tabs.append(tab);
            tabThreads.append(thread);
            thread->start();
            QMetaObject::invokeMethod(
                tab, "run", Qt::QueuedConnection,
                Q_ARG(GroupBatch, pendingBatch),
                Q_ARG(IndexBatch, pendingOrigBatch),
                Q_ARG(std::shared_ptr<std::atomic<int>>, nextIdx),
                Q_ARG(QString, tabUrls.at(t)),
                Q_ARG(QString, session.userAgent()),
                Q_ARG(QStringList, contentFor(accountIdx)),
                Q_ARG(QString, accountIdOf(accountIdx)),
                Q_ARG(QString, nAccounts > 0 ? accs.at(accountIdx).name
                                             : QStringLiteral("mặc định")),
                Q_ARG(QStringList, imagesFor(accountIdx)),
                 Q_ARG(int, req.retryCount),
                 Q_ARG(int, req.delaySec),
                 Q_ARG(int, req.jitterSec),
                 Q_ARG(int, session.windowWidth()),
                 Q_ARG(int, session.windowHeight()));
        }

        // Chạy event loop để nhận tín hiệu từ các tab (log, kết quả, dừng, ban).
        if (activeTabs > 0)
            loop.exec();

        for (QThread *t : tabThreads) {
            t->quit();
            if (!t->wait(8000)) {
                t->terminate();
                t->wait(1000);
            }
        }
        qDeleteAll(tabs);
        qDeleteAll(tabThreads);

        // Phòng ngừa: nhóm nào chưa được tab nào báo (thread chết đột xuất) thì
        // đánh thất bại để FbWorker không treo tiến độ. Nhóm bị hoãn do hạn mức
        // bài/ngày của tài khoản thì bỏ qua — để hôm sau đăng tiếp.
        QSet<int> deferredSet(deferredOrig.cbegin(), deferredOrig.cend());
        for (int i = 0; i < slice.size(); ++i) {
            if (results.at(i) < 0 && !deferredSet.contains(i)) {
                results[i] = 0;
                emit groupFinished(baseIndex + i, false);
                emit accountPosted(
                    nAccounts > 0 ? accs.at(accountIdx).id : QStringLiteral("default"),
                    nAccounts > 0 ? accs.at(accountIdx).name : QStringLiteral("mặc định"),
                    false);
            }
        }

        // Đếm các nhóm thất bại trong vòng này (chưa tính khi bị chặn giữa chừng).
        bool anyFailed = false;
        if (!bannedRound) {
            for (int i = 0; i < slice.size(); ++i) {
                if (results.at(i) == 0 && !deferredSet.contains(i)) {
                    anyFailed = true;
                    ++groupFails[i];
                }
            }
        }

        if (bannedRound) {
            const QString id = accs.at(accountIdx).id;
            const QString name = accs.at(accountIdx).name;
            accs[accountIdx].status = QStringLiteral("bị chặn");
            accs[accountIdx].failCount = 0;
            emit accountStatusChanged(id, QStringLiteral("bị chặn"));
            emit logMessage(QStringLiteral("[Luồng %1] Tài khoản \"%2\" bị chặn, đang xoay tài khoản...")
                                .arg(m_id).arg(name));
        }

        // Xoay tài khoản khi: bị chặn, hoặc một nhóm thất bại >= ngưỡng vòng liên tiếp.
        bool rotateNow = bannedRound;
        if (!rotateNow && nAccounts > 1 && rotateAccounts) {
            for (int i = 0; i < slice.size(); ++i) {
                if (groupFails.at(i) >= threshold) {
                    rotateNow = true;
                    emit logMessage(QStringLiteral("[Luồng %1] Một số nhóm thất bại liên tục (%2 lần), đang xoay tài khoản...")
                                        .arg(m_id).arg(threshold));
                    break;
                }
            }
        }

        if (rotateNow) {
            if (!(nAccounts > 1 && (rotateAccounts || rentedMode)))
                break;
            const int next = nextUsableAccount(accountIdx);
            if (next == accountIdx)
                break;
            accountIdx = next;
            Utils::humanPause(3000, 6000);
            continue;
        }

        // Không bị chặn, chưa đủ ngưỡng xoay: nếu còn nhóm thất bại thì thử lại
        // vòng tiếp theo với cùng tài khoản, nếu không còn thất bại thì hoàn tất.
        if (anyFailed && rounds < maxRounds) {
            Utils::humanPause(3000, 6000);
            continue;
        }
        break;
    }

    closeSession();

    // Nhóm chưa được đăng lần nào (kẹt do mọi tài khoản đã đủ hạn mức bài/ngày)
    // thì báo hoãn — không tính thất bại, hôm sau đăng tiếp.
    // Ở chế độ tài khoản thuê: nhóm chưa đăng được tính thất bại (khách hết bài).
    for (int i = 0; i < slice.size(); ++i) {
        if (results.at(i) < 0) {
            if (rentedMode)
                emit groupFinished(baseIndex + i, false);
            else
                emit groupDeferred(baseIndex + i);
        }
    }

    emit logMessage(QStringLiteral("[Luồng %1] Hoàn tất %2 nhóm").arg(m_id).arg(slice.size()));
    emit runFinished();
}
