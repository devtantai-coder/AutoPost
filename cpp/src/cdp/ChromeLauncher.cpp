#include "cdp/ChromeLauncher.h"

#include "proxy/Proxy.h"
#include "utils/Utils.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QDir>
#include <QSet>

ChromeLauncher::ChromeLauncher(QObject *parent)
    : QObject(parent)
{
}

ChromeLauncher::~ChromeLauncher()
{
    kill();
}

QString ChromeLauncher::findChromeBinary()
{
    const QByteArray env = qgetenv("CHROME_BIN");
    if (!env.isEmpty() && QFileInfo::exists(QString::fromLocal8Bit(env)))
        return QString::fromLocal8Bit(env);

    const QStringList candidates = {
        QStringLiteral("/usr/bin/google-chrome"),
        QStringLiteral("/opt/google/chrome/chrome"),
        QStringLiteral("/usr/bin/chromium"),
        QStringLiteral("/usr/bin/chromium-browser"),
        QStringLiteral("/usr/bin/google-chrome-stable"),
    };
    for (const QString &c : candidates) {
        if (QFileInfo::exists(c))
            return c;
    }
    return QString();
}

QStringList ChromeLauncher::buildProxyArgs(const QString &profileDir)
{
    QStringList args;
    if (m_proxy.trimmed().isEmpty())
        return args;

    const ProxyInfo info = parseProxy(m_proxy);
    if (!info.valid)
        return args; // proxy sai định dạng -> chạy không proxy (đã được lọc trước đó)

    args << QStringLiteral("--proxy-server=") + proxyServerArgument(info);
    // Không proxy traffic nội bộ: giữ DevTools/CDP trên localhost hoạt động bình thường.
    args << QStringLiteral("--proxy-bypass-list=<-loopback>");

    if (!info.username.isEmpty()) {
        const QString extDir = writeProxyAuthExtension(profileDir, info);
        if (!extDir.isEmpty()) {
            args << QStringLiteral("--load-extension=") + extDir
                 << QStringLiteral("--disable-extensions-except=") + extDir;
        }
    }
    return args;
}

void ChromeLauncher::parsePort()
{
    if (m_port > 0)
        return;
    static const QRegularExpression re(
        QStringLiteral("DevTools listening on ws://127\\.0\\.0\\.1:(\\d+)/"));
    const QRegularExpressionMatch m = re.match(QString::fromLocal8Bit(m_outputBuffer));
    if (m.hasMatch())
        m_port = m.captured(1).toInt();
}

bool ChromeLauncher::launch(const QStringList &extraArgs, QString *error)
{
    const QString chrome = findChromeBinary();
    if (chrome.isEmpty()) {
        *error = QStringLiteral("Không tìm thấy trình duyệt Chrome. Đặt biến CHROME_BIN hoặc cài Chrome.");
        return false;
    }

    // Profile dùng tạm cho phiên chạy: mỗi luồng dùng profile riêng để tránh xung đột.
    // Profile sẽ bị xóa khi đóng trình duyệt (cleanProfileDir) — không để ngốn disk.
    m_profileDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                   QStringLiteral("/chrome-profile") + m_profileSuffix;
    QDir().mkpath(m_profileDir);

    // Dấu vân tay: nếu có persona cố định (theo tài khoản) thì dùng nguyên bản
    // để 2 lần chạy của cùng 1 acc ra cùng vân tay; không có thì random.
    if (m_hasFingerprint) {
        m_userAgent = m_fingerprint.userAgent;
        m_winWidth = m_fingerprint.screenWidth;
        m_winHeight = m_fingerprint.screenHeight;
    } else {
        m_userAgent = Utils::randomUserAgent();
        static const QStringList windowSizes = {
            QStringLiteral("1920,1080"), QStringLiteral("1536,864"),
            QStringLiteral("1440,900"), QStringLiteral("1366,768"),
            QStringLiteral("1280,800"), QStringLiteral("1600,900")};
        const QString winSize = windowSizes.at(Utils::randomInt(0, windowSizes.size() - 1));
        const QStringList winParts = winSize.split(QLatin1Char(','));
        m_winWidth = winParts.value(0).toInt();
        m_winHeight = winParts.value(1).toInt();
    }
    const QString winSize = QString::number(m_winWidth) + QLatin1Char(',') +
                            QString::number(m_winHeight);

    QStringList args;
    args << QStringLiteral("--remote-debugging-port=0")
         << QStringLiteral("--user-data-dir=") + m_profileDir
         << QStringLiteral("--no-first-run")
         << QStringLiteral("--no-default-browser-check")
         << QStringLiteral("--disable-notifications")
         << QStringLiteral("--disable-infobars")
         << QStringLiteral("--no-sandbox")
         << QStringLiteral("--disable-dev-shm-usage")
         << QStringLiteral("--window-size=") + winSize
         << QStringLiteral("--lang=") + (m_hasFingerprint ? m_fingerprint.locale : QStringLiteral("vi-VN"))
         << QStringLiteral("--disable-gpu")
         << QStringLiteral("--disable-background-timer-throttling")
         << QStringLiteral("--disable-sync")
         << QStringLiteral("--mute-audio")
         << QStringLiteral("--disable-background-networking")
         << QStringLiteral("--disable-component-update")
         << QStringLiteral("--disable-domain-reliability")
         << QStringLiteral("--disable-breakpad")
         << QStringLiteral("--no-pings")
         << QStringLiteral("--disable-blink-features=AutomationControlled")
         << QStringLiteral("--force-webrtc-ip-handling-policy=default_public_interface_only")
         << QStringLiteral("--disable-features=WebRtcHideLocalIpsWithMdns,IsolateOrigins,site-per-process,UseProfileShortcutMenu,CalculateNativeWinOcclusion,msWebOOUI,msPdfOOUI,Translate,LazyFrameLoading,BackForwardCache")
         // Giảm throttle renderer khi tab ở nền: tab sau vẫn tải song song với tab
         // đang thao tác -> tăng thông lượng đăng bài đa tab.
         << QStringLiteral("--disable-renderer-backgrounding")
         << QStringLiteral("--disable-backgrounding-occluded-windows")
         << QStringLiteral("--user-agent=") + m_userAgent;
    args += extraArgs;

    // Che IP bằng proxy: nếu có tên/pass thì dùng extension MV3 trả lời xác thực
    // proxy (--load-extension), nên không bật --disable-extensions.
    const QStringList proxyArgs = buildProxyArgs(m_profileDir);
    if (proxyArgs.isEmpty()) {
        args << QStringLiteral("--disable-extensions");
    } else {
        args += proxyArgs;
    }
    args << QStringLiteral("--disk-cache-dir=") + QDir::temp().filePath(QStringLiteral("autopost-cache") + m_profileSuffix)
         << QStringLiteral("--disk-cache-size=20971520")
         << QStringLiteral("--media-cache-size=1048576")
         << QStringLiteral("--disable-application-cache")
         << QStringLiteral("about:blank");

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        m_outputBuffer += m_process->readAllStandardOutput();
        parsePort();
        // Giới hạn bộ đệm stdout của Chrome (đủ để bắt dòng "DevTools listening"),
        // tránh tích dần làm đầy RAM khi Chrome in log liên tục.
        if (m_outputBuffer.size() > 65536)
            m_outputBuffer = m_outputBuffer.right(8192);
    });

    m_process->start(chrome, args);
    if (!m_process->waitForStarted(10000)) {
        *error = QStringLiteral("Không khởi động được Chrome");
        return false;
    }

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 15000) {
        parsePort();
        if (m_port > 0)
            return true;
        m_process->waitForReadyRead(100);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }

    *error = QStringLiteral("Chrome không mở được DevTools port");
    return false;
}

void ChromeLauncher::kill()
{
    if (m_process) {
        // Dừng nhẹ (SIGTERM) rồi mới hạ (SIGKILL) để Chrome đóng các tab gọn.
        m_process->terminate();
        if (!m_process->waitForFinished(3000)) {
            m_process->kill();
            m_process->waitForFinished(3000);
        }
        delete m_process;
        m_process = nullptr;
    }

    // Chrome tách nhiều process con (renderer, zygote, GPU...) — giết cả những
    // process còn sót dùng đúng profile này, nếu không chúng mồ côi và ăn RAM mãi.
    if (!m_profileDir.isEmpty()) {
        QProcess::execute(QStringLiteral("pkill"), QStringList()
                              << QStringLiteral("-9") << QStringLiteral("-f") << m_profileDir);
        // Profile chỉ dùng tạm cho phiên chạy; cookie luôn được tiêm lại nên
        // xóa ngay sau khi trình duyệt đóng để không tích tụ trên đĩa.
        cleanProfileDir();
    }
}

void ChromeLauncher::cleanProfileDir()
{
    if (m_profileDir.isEmpty())
        return;
    QDir(m_profileDir).removeRecursively();
    QDir(QDir::temp().filePath(QStringLiteral("autopost-cache") + m_profileSuffix)).removeRecursively();
    m_profileDir.clear();
}

void ChromeLauncher::cleanupAllProfiles()
{
    // Giết cả Chrome còn sót từ lần chạy trước (thoát đột ngột / crash): chúng
    // giữ profile + RAM mãi không nhả nếu chỉ xóa thư mục. Chỉ chạy lúc khởi
    // động nên không đụng tới Chrome của phiên hiện tại.
    QProcess::execute(QStringLiteral("pkill"), QStringList()
                          << QStringLiteral("-9") << QStringLiteral("-f")
                          << QStringLiteral("chrome-profile"));

    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QStringList entries = QDir(base).entryList(QStringList() << QStringLiteral("chrome-profile*"),
                                                     QDir::Dirs);
    for (const QString &e : entries)
        QDir(QDir(base).filePath(e)).removeRecursively();
    const QStringList caches = QDir::temp().entryList(QStringList() << QStringLiteral("autopost-cache*"),
                                                      QDir::Dirs);
    for (const QString &c : caches)
        QDir(QDir::temp().filePath(c)).removeRecursively();
}
QByteArray ChromeLauncher::httpGet(const QString &url)
{
    // QNAM thread-local (ChromeLauncher::httpGet có thể được gọi từ thread của
    // session) — tránh lệ thuộc QEventLoop chạy trên thread lạ khi QNAM là global.
    thread_local QNetworkAccessManager nam;
    QEventLoop loop;
    QByteArray data;
    QNetworkReply *reply = nam.get(QNetworkRequest(QUrl(url)));
    connect(reply, &QNetworkReply::finished, &loop, [&]() {
        data = reply->readAll();
        loop.quit();
    });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit);
    loop.exec();
    if (reply->isRunning())
        reply->abort(); // Timeout: ngắt kết nối thật thay vì để chạy ngầm.
    reply->deleteLater();
    return data;
}

QString ChromeLauncher::pageTargetWebSocketUrl()
{
    const QByteArray json = httpGet(QStringLiteral("http://127.0.0.1:%1/json/list").arg(m_port));
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    const QJsonArray targets = doc.array();
    for (const QJsonValue &v : targets) {
        const QJsonObject o = v.toObject();
        if (o.value(QStringLiteral("type")).toString() == QStringLiteral("page"))
            return o.value(QStringLiteral("webSocketDebuggerUrl")).toString();
    }
    return QString();
}

QHash<QString, QString> ChromeLauncher::targetWebSocketUrls(const QStringList &targetIds)
{
    QHash<QString, QString> out;
    if (targetIds.isEmpty() || m_port <= 0)
        return out;
    QSet<QString> wanted(targetIds.cbegin(), targetIds.cend());
    // Vài vòng thử ngắn (mỗi vòng 1 HTTP GET) cho tới khi đủ target — nhanh hơn
    // nhiều so với poll riêng lẻ từng tab. Ngủ 50ms/vòng: đủ cho Chrome cập nhật
    // danh sách target mà không kéo dài mỗi đợt mở tab hàng trăm ms.
    for (int attempt = 0; attempt < 24; ++attempt) {
        const QByteArray json = httpGet(QStringLiteral("http://127.0.0.1:%1/json/list").arg(m_port));
        const QJsonDocument doc = QJsonDocument::fromJson(json);
        const QJsonArray targets = doc.array();
        for (const QJsonValue &v : targets) {
            const QJsonObject o = v.toObject();
            const QString id = o.value(QStringLiteral("id")).toString();
            if (wanted.contains(id) && !out.contains(id))
                out.insert(id, o.value(QStringLiteral("webSocketDebuggerUrl")).toString());
        }
        if (out.size() >= wanted.size())
            break;
        Utils::sleepMs(50);
    }
    return out;
}

QString ChromeLauncher::pageTargetWebSocketUrlById(const QString &targetId)
{
    if (targetId.isEmpty())
        return QString();
    // Đợi ngắn để Chrome cập nhật danh sách target sau khi tạo tab mới.
    for (int attempt = 0; attempt < 16; ++attempt) {
        const QByteArray json = httpGet(QStringLiteral("http://127.0.0.1:%1/json/list").arg(m_port));
        const QJsonDocument doc = QJsonDocument::fromJson(json);
        const QJsonArray targets = doc.array();
        for (const QJsonValue &v : targets) {
            const QJsonObject o = v.toObject();
            if (o.value(QStringLiteral("id")).toString() == targetId)
                return o.value(QStringLiteral("webSocketDebuggerUrl")).toString();
        }
        Utils::sleepMs(50);
    }
    return QString();
}
