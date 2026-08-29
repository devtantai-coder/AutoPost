#include "fb/FacebookSession.h"

#include "utils/Logger.h"
#include "utils/Utils.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QStringList>

#include <QJsonDocument>

FacebookSession::~FacebookSession()
{
    stop();
}

bool FacebookSession::start(const QString &cookieRaw, bool headless, QString *error)
{
    QStringList args;
    if (headless)
        args << QStringLiteral("--headless=new");

    // Áp persona cố định (nếu có) cho launcher TRƯỚC khi khởi chạy, để UA /
    // kích thước cửa sổ / ngôn ngữ của trình duyệt khớp với vân tay của acc.
    if (m_fingerprintSet)
        m_launcher.setFingerprint(m_fingerprint);

    if (!m_launcher.launch(args, error))
        return false;

    const QString wsUrl = m_launcher.pageTargetWebSocketUrl();
    if (wsUrl.isEmpty()) {
        *error = QStringLiteral("Không tìm thấy tab Chrome");
        stop();
        return false;
    }
    if (!m_cdp.connectToUrl(wsUrl)) {
        *error = QStringLiteral("Không kết nối được Chrome DevTools");
        stop();
        return false;
    }

    m_driver.init(&m_cdp);
    m_driver.enableDomains();
    m_driver.initPageHooks();

    // --- Lớp chống phát hiện / chống ban ---
    // Lấy vân tay từ persona (nếu đã set theo tài khoản), nếu không thì tạo persona
    // determinist từ UA của phiên: mọi tab trong trình duyệt này tính ra CÙNG vân
    // tay (WebGL/cores/memory nhất quán giữa các tab), vẫn dùng giá trị màn hình
    // và ngôn ngữ thật của trình duyệt (tương thích acc "mặc định").
    const Fingerprint fp = m_fingerprintSet
                               ? m_fingerprint
                               : Fingerprints::forDefaultUa(m_launcher.lastUserAgent(),
                                                            m_launcher.windowWidth(),
                                                            m_launcher.windowHeight());
    const QString ua = fp.userAgent;
    const QString platform = fp.platform;
    const int fpW = m_fingerprintSet ? fp.screenWidth : m_launcher.windowWidth();
    const int fpH = m_fingerprintSet ? fp.screenHeight : m_launcher.windowHeight();
    const QString acceptLang = fp.acceptLanguage;
    const QString locale = fp.locale;
    const QString tz = fp.timezone;
    const QStringList langs = fp.languages;
    const QString webglVendor = fp.webglVendor;
    const QString webglRenderer = fp.webglRenderer;
    const int cores = fp.hardwareConcurrency;
    const int memory = fp.deviceMemory;

    m_driver.setScreenMetrics(fpW, fpH);
    // Lệnh thiết lập không cần chờ phản hồi (CDP xử lý theo thứ tự FIFO) —
    // giảm chuỗi round-trip tuần tự khi dựng phiên.
    m_driver.addAntiDetectionScript(ua, platform, fpW, fpH, langs, webglVendor, webglRenderer,
                                    cores, memory, fp.seed);
    m_driver.setUserAgentOverride(ua, acceptLang, false);
    m_driver.setTimezone(tz, false);
    m_driver.setLocale(locale, false);

    // Từ chối quyền thông báo với Facebook (tránh popup quyền).
    QJsonObject perm;
    perm.insert(QStringLiteral("permission"), QJsonObject{{QStringLiteral("name"), QStringLiteral("notifications")}});
    perm.insert(QStringLiteral("setting"), QStringLiteral("denied"));
    perm.insert(QStringLiteral("origin"), QStringLiteral("https://www.facebook.com"));
    m_cdp.sendCommand(QStringLiteral("Browser.setPermission"), perm, 30000, false);

    if (!cookieRaw.isEmpty())
        injectCookies(cookieRaw);

    m_started = true;
    return true;
}

void FacebookSession::stop()
{
    if (m_cdp.isConnected())
        m_cdp.disconnectSocket();
    m_launcher.kill();
    m_started = false;
}

QString FacebookSession::openNewTab()
{
    if (!m_cdp.isConnected())
        return QString();

    // Tạo target mới qua CDP (thay cho HTTP /json/new bị Chrome dần bỏ).
    QJsonObject params{{QStringLiteral("url"), QStringLiteral("about:blank")}};
    const QJsonObject res = m_cdp.sendCommand(QStringLiteral("Target.createTarget"), params);
    const QString targetId = res.value(QStringLiteral("result"))
                                 .toObject()
                                 .value(QStringLiteral("targetId"))
                                 .toString();
    if (targetId.isEmpty())
        return QString();

    return m_launcher.pageTargetWebSocketUrlById(targetId);
}

QStringList FacebookSession::openNewTabs(int count)
{
    // Tốc độ: tạo toàn bộ target trước (mỗi lệnh trả targetId ngay), rồi resolve
    // wsUrl của tất cả bằng MỘT đợt gọi /json/list (xem
    // ChromeLauncher::targetWebSocketUrls). Trước đây mỗi tab poll HTTP riêng tới
    // 8 lần -> N tab tốn tới N*8 HTTP GET; giờ chỉ còn 1-2 HTTP GET cho cả đợt.
    QStringList urls;
    if (!m_cdp.isConnected() || count <= 0)
        return urls;

    QStringList ids;
    for (int i = 0; i < count; ++i) {
        QJsonObject params{{QStringLiteral("url"), QStringLiteral("about:blank")}};
        const QJsonObject res = m_cdp.sendCommand(QStringLiteral("Target.createTarget"), params);
        const QString id = res.value(QStringLiteral("result"))
                              .toObject()
                              .value(QStringLiteral("targetId"))
                              .toString();
        if (id.isEmpty())
            break;
        ids.append(id);
    }
    const QHash<QString, QString> wsMap = m_launcher.targetWebSocketUrls(ids);
    for (const QString &id : ids) {
        const QString ws = wsMap.value(id);
        if (!ws.isEmpty())
            urls.append(ws);
    }
    return urls;
}

QString FacebookSession::exportCookies()
{
    if (!m_cdp.isConnected())
        return QString();

    const QJsonObject res = m_cdp.sendCommand(QStringLiteral("Network.getAllCookies"));
    const QJsonArray arr = res.value(QStringLiteral("result"))
                               .toObject()
                               .value(QStringLiteral("cookies"))
                               .toArray();
    QStringList pairs;
    for (const QJsonValue &v : arr) {
        const QJsonObject c = v.toObject();
        const QString domain = c.value(QStringLiteral("domain")).toString();
        if (!domain.contains(QStringLiteral("facebook.com")))
            continue;
        const QString name = c.value(QStringLiteral("name")).toString();
        const QString value = c.value(QStringLiteral("value")).toString();
        if (!name.isEmpty() && !value.isEmpty())
            pairs.append(name + QLatin1Char('=') + value);
    }
    return pairs.join(QLatin1String("; "));
}

bool FacebookSession::injectCookies(const QString &cookieRaw)
{
    Logger::instance().log(QStringLiteral("Đang tải cookie vào trình duyệt..."));

    // Dọn cookie cũ rồi gộp toàn bộ cookie tài khoản vào 1 lệnh Network.setCookies
    // (trước đây gửi từng cái một -> tốn ~20 round-trip CDP).
    m_cdp.sendCommand(QStringLiteral("Network.clearBrowserCookies"));

    QJsonArray cookies;
    const QStringList pairs = cookieRaw.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &pair : pairs) {
        const int idx = pair.indexOf(QLatin1Char('='));
        if (idx <= 0)
            continue;
        const QString name = pair.left(idx).trimmed();
        const QString value = pair.mid(idx + 1).trimmed();
        if (name.isEmpty() || value.isEmpty())
            continue;

        QJsonObject c;
        c.insert(QStringLiteral("name"), name);
        c.insert(QStringLiteral("value"), value);
        c.insert(QStringLiteral("url"), QStringLiteral("https://www.facebook.com/"));
        c.insert(QStringLiteral("secure"), true);
        cookies.append(c);
    }
    if (!cookies.isEmpty())
        m_cdp.sendCommand(QStringLiteral("Network.setCookies"),
                          QJsonObject{{QStringLiteral("cookies"), cookies}});

    // Chỉ cần tải facebook.com MỘT lần để áp dụng session (navigate đã chờ trang xong).
    m_driver.navigate(QStringLiteral("https://www.facebook.com/"), 20000);
    m_driver.initPageHooks();
    Logger::instance().log(QStringLiteral("Đã tải cookie thành công"));
    return true;
}
