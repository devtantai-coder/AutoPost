#include "fb/FbWorker.h"

#include "fb/FacebookParser.h"
#include "fb/FacebookSession.h"
#include "fb/JoinEngine.h"
#include "fb/NurtureEngine.h"
#include "fb/PostEngine.h"
#include "store/DailyPostLog.h"
#include "utils/Logger.h"
#include "store/PostedStore.h"
#include "proxy/Proxy.h"
#include "proxy/ProxyHealth.h"
#include "store/ReportExporter.h"
#include "utils/Utils.h"

#include <QDateTime>
#include <QEventLoop>
#include <QFuture>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QThread>
#include <QTime>
#include <QTimer>
#include <QUrl>

#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>

namespace
{
// QNetworkAccessManager KHÔNG thread-safe -> dùng bản riêng theo thread
// (thread_local: tạo 1 lần lục dùng chung, không new gộp từng lần gọi, tiết
// kiệm việc sinh/chết QNAM liên tục khi lấy danh sách proxy).
QNetworkAccessManager &sharedNam()
{
    thread_local QNetworkAccessManager nam;
    return nam;
}

// Tải nội dung URL đơn giản (chờ tối đa timeoutMs). Dùng cho việc lấy proxy miễn phí.
QByteArray fetchUrl(const QString &url, int timeoutMs)
{
    QNetworkAccessManager &nam = sharedNam();
    QEventLoop loop;
    QByteArray data;
    QNetworkReply *reply = nam.get(QNetworkRequest(QUrl(url)));
    QObject::connect(reply, &QNetworkReply::finished, &loop,
                     [&]() { data = reply->readAll(); loop.quit(); });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    if (reply->isRunning())
        reply->abort(); // Ngắt kết nối thật khi chờ hết hạn, không để chạy ngầm.
    reply->deleteLater();
    return data;
}

// Tải proxy từ một nguồn cụ thể. Trả danh sách host:port đã lọc trùng trong nguồn;
// errMsg chứa thông báo lỗi nếu không lấy được.
QStringList fetchProxiesFrom(const QString &source, QString *errMsg)
{
    QString url;
    if (source == QStringLiteral("proxyscrape")) {
        url = QStringLiteral(
            "https://api.proxyscrape.com/v2/?request=displayproxies&protocol=http&timeout=5000"
            "&country=all&ssl=all&anonymity=all");
    } else if (source == QStringLiteral("geonode")) {
        url = QStringLiteral(
            "https://proxylist.geonode.com/api/proxy-list?limit=100&page=1"
            "&sort_by=lastChecked&sort_type=desc&protocols=http");
    } else if (source == QStringLiteral("proxy-list.download")) {
        url = QStringLiteral("https://www.proxy-list.download/api/v1/get?type=http");
    } else if (source == QStringLiteral("thespeedx")) {
        url = QStringLiteral("https://raw.githubusercontent.com/TheSpeedX/PROXY-List/master/http.txt");
    } else if (source == QStringLiteral("clarketm")) {
        url = QStringLiteral("https://raw.githubusercontent.com/clarketm/proxy-list/master/proxy-list-raw.txt");
    } else if (source == QStringLiteral("monosans")) {
        url = QStringLiteral("https://raw.githubusercontent.com/monosans/proxy-list/main/proxies/http.txt");
    } else if (source == QStringLiteral("shiftytr")) {
        url = QStringLiteral("https://raw.githubusercontent.com/ShiftyTR/Proxy-List/master/http.txt");
    } else if (source == QStringLiteral("proxy4parsing")) {
        url = QStringLiteral("https://raw.githubusercontent.com/proxy4parsing/proxy-list/main/http.txt");
    } else if (source == QStringLiteral("roosterkid")) {
        url = QStringLiteral("https://raw.githubusercontent.com/roosterkid/openproxylist/main/HTTPS_RAW.txt");
    } else if (source == QStringLiteral("vakhov")) {
        url = QStringLiteral("https://raw.githubusercontent.com/vakhov/fresh-proxy-list/master/http.txt");
    } else if (source == QStringLiteral("jetkai")) {
        url = QStringLiteral("https://raw.githubusercontent.com/jetkai/proxy-list/main/online-proxies/txt/proxies-http.txt");
    } else if (source == QStringLiteral("proxyspace")) {
        url = QStringLiteral("https://proxyspace.pro/http.txt");
    } else {
        *errMsg = QStringLiteral("Nguồn proxy không hợp lệ: ") + source;
        return {};
    }

    const QByteArray data = fetchUrl(url, 15000);
    if (data.isEmpty()) {
        *errMsg = QStringLiteral("hết thời gian chờ / lỗi mạng");
        return {};
    }

    QStringList proxies;
    if (source == QStringLiteral("geonode")) {
        // API trả JSON: {"data":[{"ip":"1.2.3.4","port":"8080",...},...]}
        const QJsonArray arr =
            QJsonDocument::fromJson(data).object().value(QStringLiteral("data")).toArray();
        for (const QJsonValue &v : arr) {
            const QJsonObject o = v.toObject();
            const QString ip = o.value(QStringLiteral("ip")).toString();
            const int port = o.value(QStringLiteral("port")).toInt();
            if (!ip.isEmpty() && port > 0)
                proxies.append(ip + QLatin1Char(':') + QString::number(port));
        }
    } else {
        // Các nguồn còn lại trả văn bản, mỗi dòng một host:port.
        static const QRegularExpression re(
            QStringLiteral("(\\d{1,3}(?:\\.\\d{1,3}){3}):(\\d{2,5})"));
        const QStringList lines =
            QString::fromUtf8(data).split(QRegularExpression(QStringLiteral("[\\n\\r]+")),
                                           Qt::SkipEmptyParts);
        QSet<QString> seen;
        for (const QString &l : lines) {
            const QRegularExpressionMatch m = re.match(l);
            if (m.hasMatch()) {
                const QString p = m.captured(1) + QLatin1Char(':') + m.captured(2);
                if (!seen.contains(p)) {
                    seen.insert(p);
                    proxies.append(p);
                }
            }
        }
    }
    return proxies;
}
} // namespace

FbWorker::FbWorker(QObject *parent)
    : QObject(parent)
{
}

FbWorker::~FbWorker()
{
    shutdownWorkers();
}

void FbWorker::requestStop()
{
    m_stop.store(true);
    for (PostWorker *w : m_workers)
        w->requestStop();
}

void FbWorker::startNurture(NurtureRequest req, bool headless,
                            const QVector<FacebookAccount> &accounts)
{
    m_stop.store(false);
    int ok = 0;
    int failed = 0;

    emit logMessage(QStringLiteral("Bắt đầu nuôi %1 tài khoản (tối đa %2 like, %3 video, %4 giây/acc)...")
                        .arg(accounts.size())
                        .arg(req.maxLikes)
                        .arg(req.maxVideos)
                        .arg(req.maxDurationSec));

    for (const FacebookAccount &acc : accounts) {
        if (m_stop.load())
            break;
        if (acc.cookieRaw.trimmed().isEmpty())
            continue;

        emit logMessage(QStringLiteral("  Nuôi acc: %1").arg(acc.name));

        FacebookSession session;
        session.setProfileSuffix(QStringLiteral("-nurture"));
        // Persona cố định theo tài khoản (seed = id) để nuôi acc giữ nguyên
        // vân tay qua các lần chạy, tránh Facebook nghi ngờ đổi thiết bị.
        session.setFingerprintSeed(acc.id);
        if (!acc.proxy.trimmed().isEmpty())
            session.setProxy(acc.proxy.trimmed());

        QString error;
        if (!session.start(acc.cookieRaw, headless, &error)) {
            emit logMessage(QStringLiteral("    Lỗi khởi tạo trình duyệt: %1").arg(error));
            ++failed;
            continue;
        }

        NurtureEngine::Settings s;
        s.maxLikes = req.maxLikes;
        s.maxVideos = req.maxVideos;
        s.maxDurationSec = req.maxDurationSec;
        NurtureEngine engine(session.driver());
        const bool good = engine.nurture(s, &m_stop);
        session.stop();

        if (good) {
            ++ok;
            emit logMessage(QStringLiteral("    ✓ Đã nuôi xong acc %1").arg(acc.name));
        } else {
            ++failed;
            emit logMessage(QStringLiteral("    ✕ Nuôi acc %1 thất bại (checkpoint/lỗi)").arg(acc.name));
        }

        // Nghỉ giữa các tài khoản để không đăng nhập dồn dập.
        if (!m_stop.load())
            Utils::sleepMs(Utils::randomInt(5000, 12000));
    }

    emit logMessage(QStringLiteral("Hoàn tất nuôi acc: %1 thành công, %2 thất bại.").arg(ok).arg(failed));
    emit nurtureDone(ok, failed);
}

void FbWorker::fetchFreeProxies(const QString &source)
{
    // "all" = gộp từ nhiều nguồn; ngược lại tải một nguồn cụ thể.
    QStringList sources;
    if (source == QStringLiteral("all")) {
        sources = {QStringLiteral("proxyscrape"), QStringLiteral("geonode"),
                   QStringLiteral("proxy-list.download"), QStringLiteral("thespeedx"),
                   QStringLiteral("clarketm"), QStringLiteral("monosans"),
                   QStringLiteral("shiftytr"), QStringLiteral("proxy4parsing"),
                   QStringLiteral("roosterkid"), QStringLiteral("vakhov"),
                   QStringLiteral("jetkai"), QStringLiteral("proxyspace")};
    } else {
        sources = {source};
    }

    // Tải song song mọi nguồn qua QtConcurrent (trước đây nối tiếp: chờ 12 nguồn
    // lần lượt ~12*15s tối đa; nay tất cả chạy cùng lúc, chỉ chờ nguồn chậm nhất).
    // Mỗi nguồn trả về (danh sách proxy, chuỗi lỗi) độc lập nên không cần khóa.
    QVector<QFuture<QPair<QStringList, QString>>> futures;
    futures.reserve(sources.size());
    for (const QString &s : sources)
        futures.append(QtConcurrent::run([s]() {
            QString err;
            return qMakePair(fetchProxiesFrom(s, &err), err);
        }));

    QStringList merged;
    QSet<QString> seen;
    for (int i = 0; i < sources.size(); ++i) {
        if (m_stop.load())
            break;
        emit logMessage(QStringLiteral("Đang tải proxy từ %1...").arg(sources.at(i)));
        const auto [list, err] = futures.at(i).result();
        if (list.isEmpty()) {
            emit logMessage(err.isEmpty()
                                ? QStringLiteral("  %1: không tải được proxy nào").arg(sources.at(i))
                                : QStringLiteral("  %1: %2").arg(sources.at(i), err));
            continue;
        }
        int addedHere = 0;
        for (const QString &p : list) {
            if (!seen.contains(p)) {
                seen.insert(p);
                merged.append(p);
                ++addedHere;
            }
        }
        emit logMessage(QStringLiteral("  %1: tải %2 proxy, thêm mới %3")
                            .arg(sources.at(i))
                            .arg(list.size())
                            .arg(addedHere));
    }

    emit logMessage(QStringLiteral("Gộp được tổng %1 proxy từ %2 nguồn")
                        .arg(merged.size())
                        .arg(sources.size()));
    emit proxiesFetched(merged, source);
}

void FbWorker::testProxies(const QStringList &proxyLines)
{
    // Lọc dòng trống, giữ index gốc để UI cập nhật đúng vị trí.
    QStringList lines;
    QVector<int> origIdx;
    for (int i = 0; i < proxyLines.size(); ++i) {
        const QString t = proxyLines.at(i).trimmed();
        if (!t.isEmpty()) {
            lines.append(t);
            origIdx.append(i);
        }
    }

    emit logMessage(QStringLiteral("Bắt đầu kiểm tra %1 proxy (song song)...").arg(lines.size()));

    // Kiểm tra TCP song song qua QtConcurrent: với nhiều proxy, tổng thời gian
    // chỉ bằng proxy chậm nhất thay vì tổng của tất cả (trước đây chạy tuần tự).
    QVector<QFuture<bool>> futures;
    futures.reserve(lines.size());
    for (const QString &line : lines)
        futures.append(QtConcurrent::run([line]() { return testProxyReachability(line, 3000); }));

    for (int i = 0; i < lines.size(); ++i) {
        if (m_stop.load())
            break;
        const bool ok = futures.at(i).result();
        emit logMessage(QStringLiteral("  Kiểm tra %1 → %2")
                            .arg(lines.at(i))
                            .arg(ok ? QStringLiteral("Kết nối OK (TCP)")
                                    : QStringLiteral("KHÔNG kết nối được")));
        emit proxyTestResult(origIdx.at(i), ok, lines.at(i));
    }
    emit logMessage(QStringLiteral("Đã kiểm tra xong proxy."));
    m_stop.store(false);
}

void FbWorker::shutdownWorkers()
{
    // Đảm bảo các dữ liệu trì hoãn ghi (cache 5s/64 bài) xuống đĩa trước khi
    // phiên dừng/kết thúc — không mất dấu "đã đăng hôm nay" hay số bài đã rải.
    PostedStore::flush();
    DailyPostLog::flush();
    for (PostWorker *w : m_workers)
        w->requestStop();
    for (QThread *t : m_workerThreads) {
        t->quit();
        if (!t->wait(10000)) {
            // Thread bị kẹt (đang chờ CDP/Chrome, ví dụ tab treo). QThread::terminate
            // KHÔNG chạy destructor của FacebookSession trên stack thread đó -> trình
            // duyệt Chrome con bị bỏ lại chạy (rò rỉ RAM/CPU). Phải dọn sạch toàn bộ
            // Chrome của tool TRƯỚC khi terminate thread.
            ChromeLauncher::cleanupAllProfiles();
            t->terminate();
            t->wait(1000);
        }
    }
    // Backstop: dọn lần nữa phòng Chrome chưa kịp chết hẳn sau terminate.
    ChromeLauncher::cleanupAllProfiles();
    qDeleteAll(m_workers);
    qDeleteAll(m_workerThreads);
    m_workers.clear();
    m_workerThreads.clear();
    m_activeWorkers = 0;
}

void FbWorker::fetchMyGroups(const QString &cookieRaw, bool headless)
{
    m_stop.store(false);

    FacebookSession session;
    QString error;
    // Seed từ cookie -> vân tay ổn định theo cookie (không có id acc ở đây).
    session.setFingerprintSeed(cookieRaw);
    if (!session.start(cookieRaw, headless, &error)) {
        emit logMessage(QStringLiteral("Lỗi khởi tạo trình duyệt: ") + error);
        emit groupsReady(QVector<FacebookGroup>());
        return;
    }

    const QVector<FacebookGroup> groups = FacebookParser::extractMyGroups(session.driver());
    emit groupsReady(groups);
    emit logMessage(QStringLiteral("Đã lấy %1 nhóm từ hồ sơ của bạn").arg(groups.size()));
}

void FbWorker::searchAndJoin(const JoinRequest &req, const QString &cookieRaw, bool headless)
{
    m_stop.store(false);

    FacebookSession session;
    QString error;
    // Seed từ cookie -> vân tay ổn định theo cookie (không có id acc ở đây).
    session.setFingerprintSeed(cookieRaw);
    if (!session.start(cookieRaw, headless, &error)) {
        emit logMessage(QStringLiteral("Lỗi khởi tạo trình duyệt: ") + error);
        emit joiningDone();
        return;
    }

    JoinEngine engine(session.driver());
    QVector<FacebookGroup> found;

    if (!req.joinGroupIds.isEmpty()) {
        // Tham gia trực tiếp theo danh sách ID (kể cả nhóm riêng tư/chia sẻ).
        QSet<QString> seen;
        for (const QString &id : req.joinGroupIds) {
            if (id.isEmpty() || seen.contains(id))
                continue;
            seen.insert(id);
            FacebookGroup g;
            g.id = id;
            g.name = QStringLiteral("Nhóm ") + id;
            g.url = QStringLiteral("https://www.facebook.com/groups/") + id;
            g.privacy = QStringLiteral("chưa rõ");
            found.append(g);
        }
        emit logMessage(QStringLiteral("Đã nạp %1 nhóm theo ID").arg(found.size()));
    } else {
        Utils::humanPause(2000, 5000);
        found = engine.searchGroups(req.keywords);
        emit logMessage(QStringLiteral("Tìm thấy %1 nhóm từ tìm kiếm").arg(found.size()));
    }
    emit groupsReady(found);

    if (req.autoJoin) {
        JoinEngine::Settings s;
        s.maxGroups = req.maxGroups;
        s.joinDelaySec = req.joinDelaySec;
        s.joinAction = req.joinAction;
        s.skipPrivate = req.skipPrivate;
        s.skipPending = req.skipPending;
        engine.joinGroups(found, s, &m_stop);
    }

    emit joiningDone();
}

void FbWorker::startPosting(PostRequest req, const QString &cookieRaw, bool headless,
                            const QVector<FacebookAccount> &accounts, bool rotateAccounts,
                            int rotateFailThreshold)
{
    startPostingImpl(req, cookieRaw, headless, accounts, rotateAccounts,
                     rotateFailThreshold, QVector<RentedAccount>());
}

void FbWorker::startRented(PostRequest req, bool headless, const QVector<RentedAccount> &rented)
{
    startPostingImpl(req, QString(), headless, QVector<FacebookAccount>(), false, 2, rented);
}

void FbWorker::startPostingImpl(PostRequest req, const QString &cookieRaw, bool headless,
                                const QVector<FacebookAccount> &accounts, bool rotateAccounts,
                                int rotateFailThreshold, const QVector<RentedAccount> &rented)
{
    m_stop.store(false);
    // Reset cầu chì chống ban: danh sách acc bị chặn được theo dõi theo TỪNG phiên
    // chạy (phiên mới bắt đầu lại từ 0).
    m_bannedAccounts.clear();
    shutdownWorkers();

    const bool rentedMode = !rented.isEmpty();

    // Pre-check proxy trước khi đăng: kiểm tra nhanh TCP từng proxy của các tài
    // khoản được chọn (+ pool xoay vòng), chỉ cảnh báo — không chặn đăng.
    QSet<QString> toCheck;
    for (const FacebookAccount &a : accounts) {
        const QString p = a.proxy.trimmed();
        if (!p.isEmpty())
            toCheck.insert(p);
    }
    for (const RentedAccount &r : rented) {
        const QString p = r.proxy.trimmed();
        if (!p.isEmpty())
            toCheck.insert(p);
    }
    const QStringList pool = req.rotateProxyPerPost ? req.proxyPool : QStringList();
    for (const QString &p : pool) {
        const QString t = p.trimmed();
        if (!t.isEmpty())
            toCheck.insert(t);
    }
    // Bảng sức khỏe proxy: kiểm tra nhanh từng proxy, lưu kết quả và tự động
    // kiểm tra định kỳ (heartbeat) trong suốt quá trình đăng để tự bỏ qua /
    // xoay proxy chết. Được chia sẻ cho mọi PostWorker qua req.proxyHealth.
    auto proxyHealth = std::make_shared<ProxyHealth>(this);
    if (!toCheck.isEmpty()) {
        emit logMessage(QStringLiteral("Đang kiểm tra nhanh %1 proxy trước khi đăng (song song)...")
                            .arg(toCheck.size()));
        const QStringList list = toCheck.values();
        QVector<QFuture<bool>> futures;
        futures.reserve(list.size());
        for (const QString &p : list)
            futures.append(QtConcurrent::run([p]() { return testProxyReachability(p, 2000); }));
        int dead = 0;
        for (int i = 0; i < list.size(); ++i) {
            if (m_stop.load())
                break;
            const bool ok = futures.at(i).result();
            proxyHealth->setAlive(list.at(i), ok);
            if (!ok)
                ++dead;
            emit logMessage(QStringLiteral("  %1 %2")
                                .arg(ok ? QStringLiteral("✓") : QStringLiteral("✕"))
                                .arg(list.at(i)));
        }
        if (dead > 0)
            emit logMessage(QStringLiteral("Cảnh báo: %1 proxy không kết nối được. "
                                           "Đăng bài vẫn tiếp tục, nhưng khả năng thất bại cao.")
                                .arg(dead));
        // Tự kiểm tra lại mỗi 5 phút để proxy hồi phục / chết mới đều được cập nhật.
        proxyHealth->startHeartbeat(300'000);
    }
    req.proxyHealth = proxyHealth;

    // Lịch đăng theo giờ: nếu bật, chờ đến mốc giờ gần nhất rồi mới bắt đầu.
    if (req.scheduleEnabled) {
        QList<QTime> targets;
        for (const QString &s : req.scheduleTimes) {
            const QTime t = QTime::fromString(s.trimmed(), QStringLiteral("HH:mm"));
            if (t.isValid())
                targets.append(t);
        }
        if (targets.isEmpty()) {
            const QTime t = QTime::fromString(req.scheduleTime, QStringLiteral("HH:mm"));
            if (t.isValid())
                targets.append(t);
        }
        if (!targets.isEmpty()) {
            std::sort(targets.begin(), targets.end());
            const QTime now = QTime::currentTime();
            int ms = -1;
            QString picked;
            bool tomorrow = false;
            for (const QTime &t : targets) {
                if (t >= now) {
                    ms = now.msecsTo(t);
                    picked = t.toString(QStringLiteral("HH:mm"));
                    break;
                }
            }
            if (ms < 0) {
                // Mọi mốc đã qua trong hôm nay: chờ đến mốc sớm nhất ngày mai.
                tomorrow = true;
                picked = targets.first().toString(QStringLiteral("HH:mm"));
                ms = 24 * 60 * 60 * 1000 + now.msecsTo(targets.first());
            }
            emit logMessage(QStringLiteral("Lịch đăng: chờ đến %1%2 rồi mới bắt đầu (còn ~%3 phút)...")
                                .arg(picked)
                                .arg(tomorrow ? QStringLiteral(" ngày mai") : QString())
                                .arg(qMax(1, ms / 60000)));
            int waited = 0;
            while (waited < ms && !m_stop.load()) {
                QThread::msleep(qMin(5000, ms - waited));
                waited = qMin(ms, waited + 5000);
            }
            if (m_stop.load()) {
                emit postingDone();
                return;
            }
            emit logMessage(QStringLiteral("Đến giờ đăng, bắt đầu..."));
        }
    }

    // Chống đăng trùng hôm nay: bỏ qua nhóm đã đăng thành công trong ngày.
    // Cache kết quả theo groupId: danh sách thường chứa nhiều nhóm trùng
    // (lấy từ nhiều nguồn) — tránh đọc JSON lặp lại cho cùng một id.
    if (req.skipPostedToday) {
        QVector<FacebookGroup> keep;
        keep.reserve(req.groups.size());
        QHash<QString, bool> postedCache;
        for (const FacebookGroup &g : req.groups) {
            bool posted = false;
            auto it = postedCache.constFind(g.id);
            if (it != postedCache.constEnd()) {
                posted = it.value();
            } else {
                posted = PostedStore::isPostedToday(g.id);
                postedCache.insert(g.id, posted);
            }
            if (!posted)
                keep.append(g);
        }
        const int skipped = req.groups.size() - keep.size();
        if (skipped > 0)
            emit logMessage(QStringLiteral("Bỏ qua %1 nhóm đã đăng thành công hôm nay").arg(skipped));
        req.groups = keep;
    }

    const int total = req.groups.size();
    if (total <= 0) {
        emit logMessage(QStringLiteral("Không có nhóm nào để đăng (có thể đã đăng hết hôm nay)"));
        emit postingDone();
        return;
    }

    // Xáo trộn thứ tự trước khi chia phiên: các trình duyệt đăng nhóm ngẫu nhiên.
    Utils::shuffle(req.groups);

    // Mỗi trình duyệt dùng đúng một tài khoản; không để 2 trình duyệt dùng chung
    // tài khoản (tránh Facebook phát hiện đăng nhập cùng lúc -> tiết kiệm tài khoản).
    int threads = qBound(1, req.threadCount, total);
    const int nAccounts = qMax(1, rentedMode ? rented.size() : accounts.size());
    if (rentedMode) {
        // Tối ưu hiệu năng chế độ tài khoản thuê: MỖI tài khoản thuê 1 trình duyệt
        // riêng chạy song song — mọi tài khoản đều chạy cùng lúc, không phải đợi
        // xoay vòng nối tiếp như trước.
        threads = qMin(nAccounts, total);
        emit logMessage(
            QStringLiteral("Tối ưu: chạy %1 tài khoản thuê song song — mỗi tài khoản 1 trình "
                           "duyệt riêng (mỗi trình duyệt ~%2 nhóm, %3 tab)")
                .arg(threads)
                .arg(qMax(1, (total + threads - 1) / threads))
                .arg(req.tabCount));
    } else if (nAccounts > 0) {
        threads = qMin(threads, nAccounts);
    }

    // Rải chéo (interleave): sắp lại nhóm theo vòng tròn theo số luồng — nhóm
    // i, i+threads, i+2*threads... về cùng một luồng. Nhờ đó hai nhóm LIÊN
    // TIẾP trong lịch trình rơi vào hai luồng (hai tài khoản) khác nhau: acc A
    // đăng nhóm 1, acc B đăng nhóm 2, acc A đăng nhóm 3... không acc nào đăng
    // dồn nhiều nhóm liền nhau (tránh pattern máy móc dễ bị Facebook soi).
    if (req.interleaveAccounts && threads > 1) {
        QVector<FacebookGroup> interleaved;
        interleaved.reserve(req.groups.size());
        for (int t = 0; t < threads; ++t) {
            for (int i = t; i < req.groups.size(); i += threads)
                interleaved.append(req.groups.at(i));
        }
        req.groups = interleaved;
        emit logMessage(QStringLiteral("Rải chéo: đã đan xen %1 nhóm qua %2 luồng — nhóm liên tiếp "
                                       "do tài khoản khác nhau đăng.")
                            .arg(req.groups.size())
                            .arg(threads));
    }

    m_runGroups = req.groups;
    m_groupTimes.clear();

    m_totalGroups = total;
    m_done = 0;
    m_success = 0;
    m_failed = 0;
    m_groupStatus.clear();
    m_accountStats.clear();
    m_accountNames.clear();
    m_activeWorkers = threads;

    emit logMessage(QStringLiteral("Tổng số nhóm cần đăng: %1 · Số trình duyệt: %2 · Tab mỗi trình duyệt: %3 · Tài khoản: %4%5%6")
                        .arg(total)
                        .arg(threads)
                        .arg(req.tabCount)
                        .arg(nAccounts)
                        .arg(rotateAccounts ? QStringLiteral(" (xoay khi bị chặn)") : QString())
                        .arg(rentedMode ? QStringLiteral(" (chế độ tài khoản thuê)") : QString()));

    // Bảng cờ chống 2 luồng đăng nhập cùng 1 tài khoản thuê cùng lúc (chia sẻ
    // qua shared_ptr giữa mọi PostWorker) + đếm số bài đã dùng dùng chung.
    auto claims = std::make_shared<AccountClaimTable>(nAccounts);
    if (rentedMode) {
        for (int i = 0; i < rented.size() && i < nAccounts; ++i)
            claims->setUsed(i, rented.at(i).used);
    }

    const int per = (total + threads - 1) / threads;
    for (int t = 0; t < threads; ++t) {
        const int from = t * per;
        const int to = qMin(total, from + per);
        if (from >= to) {
            --m_activeWorkers;
            continue;
        }

        QVector<FacebookGroup> slice(req.groups.begin() + from, req.groups.begin() + to);

        auto *thread = new QThread(this);
        auto *worker = new PostWorker(t, claims);
        worker->moveToThread(thread);

        connect(worker, &PostWorker::groupFinished, this, &FbWorker::onWorkerGroupFinished);
        connect(worker, &PostWorker::groupDeferred, this, &FbWorker::onWorkerGroupDeferred);
        connect(worker, &PostWorker::accountPosted, this,
                [this](const QString &accountId, const QString &accountName, bool ok) {
                    auto &v = m_accountStats[accountId];
                    if (ok)
                        ++v.first;
                    else
                        ++v.second;
                    m_accountNames[accountId] = accountName;
                });
        connect(worker, &PostWorker::logMessage, this, &FbWorker::logMessage);
        connect(worker, &PostWorker::accountStatusChanged, this,
                [this](const QString &accountId, const QString &status) {
                    emit accountStatusChanged(accountId, status);
                    // CẦU CHÌ CHỐNG BAN: khi 3+ tài khoản bị chặn/checkpoint trong
                    // MỘT phiên chạy, gần như chắc chắn IP/thiết bị đã bị Facebook
                    // đánh dấu -> tiếp tục chỉ làm chết hết dàn acc còn lại. Tự
                    // dừng toàn bộ và báo cảnh báo.
                    const bool bad = status == QStringLiteral("banned") ||
                                     status == QStringLiteral("checkpoint") ||
                                     status.contains(QStringLiteral("chặn")) ||
                                     status.contains(QStringLiteral("checkpoint"));
                    if (bad && accountId != QStringLiteral("default") &&
                        accountId != QStringLiteral("mặc định")) {
                        m_bannedAccounts.insert(accountId);
                        constexpr int kCircuitBreaker = 3;
                        if (int(m_bannedAccounts.size()) >= kCircuitBreaker && !m_stop.load()) {
                            emit logMessage(QStringLiteral(
                                "⚠ %1 tài khoản bị chặn trong phiên này — dừng toàn bộ "
                                "để bảo vệ các tài khoản còn lại (dấu hiệu IP/thiết bị "
                                "đã bị Facebook theo dõi).")
                                .arg(m_bannedAccounts.size()));
                            m_stop.store(true);
                        }
                    }
                });
        connect(worker, &PostWorker::rentedQuotaUsed, this, &FbWorker::rentedQuotaUsed);
        connect(worker, &PostWorker::runFinished, this, &FbWorker::onWorkerRunFinished);

        m_workers.append(worker);
        m_workerThreads.append(thread);

        thread->start();
        QMetaObject::invokeMethod(worker, "run", Qt::QueuedConnection,
                                  Q_ARG(PostRequest, req), Q_ARG(QString, cookieRaw),
                                  Q_ARG(bool, headless), Q_ARG(QVector<FacebookGroup>, slice),
                                  Q_ARG(int, from), Q_ARG(QVector<FacebookAccount>, accounts),
                                  Q_ARG(bool, rotateAccounts), Q_ARG(int, rotateFailThreshold),
                                  Q_ARG(int, t % nAccounts), Q_ARG(QVector<RentedAccount>, rented));
    }

    if (m_activeWorkers <= 0)
        emit postingDone();
}

void FbWorker::onWorkerGroupFinished(int globalIndex, bool ok)
{
    const int newVal = ok ? 1 : 0;
    bool newlyOk = false;
    auto it = m_groupStatus.find(globalIndex);
    if (it == m_groupStatus.end()) {
        m_groupStatus.insert(globalIndex, newVal);
        newlyOk = ok;
        ++m_done;
        if (ok)
            ++m_success;
        else
            ++m_failed;
    } else if (it.value() != newVal) {
        const int old = it.value();
        it.value() = newVal;
        if (old < 0) {
            // Trước đó bị hoãn (chưa tính thành công/thất bại).
            if (ok)
                ++m_success;
            else
                ++m_failed;
            newlyOk = ok;
        } else {
            if (ok) {
                --m_failed;
                ++m_success;
            } else {
                --m_success;
                ++m_failed;
            }
            newlyOk = ok;
        }
    } else {
        return; // kết quả trùng vòng trước, không đếm lại
    }

    // Ghi nhớ nhóm đã đăng thành công hôm nay (để lần chạy sau không đăng trùng).
    if (newlyOk && globalIndex >= 0 && globalIndex < m_runGroups.size())
        PostedStore::markPosted(m_runGroups.at(globalIndex).id);

    // Thời điểm báo kết quả đầu tiên — dùng cho báo cáo CSV.
    if (!m_groupTimes.contains(globalIndex))
        m_groupTimes.insert(globalIndex, QDateTime::currentDateTime());

    emit progressUpdated(m_done, m_totalGroups, m_success, m_failed);
}

void FbWorker::onWorkerGroupDeferred(int globalIndex)
{
    // Nhóm bị hoãn do tài khoản đủ hạn mức bài/ngày: đếm là đã xử lý nhưng
    // không tính thành công cũng không tính thất bại.
    auto it = m_groupStatus.find(globalIndex);
    if (it != m_groupStatus.end())
        return; // đã có kết quả thực (hoặc đã hoãn), bỏ qua
    m_groupStatus.insert(globalIndex, -1);
    ++m_done;
    emit progressUpdated(m_done, m_totalGroups, m_success, m_failed);
}

void FbWorker::onWorkerRunFinished()
{
    --m_activeWorkers;
    if (m_activeWorkers <= 0) {
        // Xuất báo cáo CSV kết quả toàn bộ nhóm.
        if (!m_runGroups.isEmpty()) {
            QHash<int, int> st;
            for (auto sit = m_groupStatus.cbegin(); sit != m_groupStatus.cend(); ++sit)
                st.insert(sit.key(), sit.value());
            const QString path =
                QStringLiteral("bao_cao_%1.csv")
                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
            const QString written = ReportExporter::writeCsv(path, m_runGroups, st, m_groupTimes);
            if (!written.isEmpty())
                emit logMessage(QStringLiteral("Đã xuất báo cáo kết quả: ") + written);
        }

        // Báo cáo SỐ LƯỢNG BÀI ĐÃ RẢI THEO TỪNG TÀI KHOẢN.
        if (!m_accountStats.isEmpty()) {
            const QString path =
                QStringLiteral("bao_cao_acc_%1.csv")
                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
            const QString written =
                ReportExporter::writeAccountCsv(path, m_accountStats, m_accountNames);
            if (!written.isEmpty())
                emit logMessage(QStringLiteral("Đã xuất báo cáo số lượng theo tài khoản: ") + written);

            // Tóm tắt nhanh trong log: từng tài khoản đã rải bao nhiêu bài.
            QStringList summary;
            int totOk = 0;
            int totFail = 0;
            for (auto it = m_accountStats.cbegin(); it != m_accountStats.cend(); ++it) {
                summary.append(QStringLiteral("%1: %2 bài").arg(m_accountNames.value(it.key(), it.key()))
                                   .arg(it.value().first));
                totOk += it.value().first;
                totFail += it.value().second;
            }
            emit logMessage(QStringLiteral("Số lượng rải theo acc: %1 (tổng %2 thành công, %3 thất bại)")
                                .arg(summary.join(QStringLiteral(" · ")))
                                .arg(totOk)
                                .arg(totFail));
        }
        emit postingDone();
    }
}
