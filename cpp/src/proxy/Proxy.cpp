#include "proxy/Proxy.h"

#include <QTcpSocket>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace
{
// Escape chuỗi thành một hằng chuỗi hợp lệ trong JavaScript.
QString jsString(const QString &s)
{
    QString out = QStringLiteral("\"");
    for (const QChar &c : s) {
        switch (c.unicode()) {
        case '\\': out += QStringLiteral("\\\\"); break;
        case '"': out += QStringLiteral("\\\""); break;
        case '\n': out += QStringLiteral("\\n"); break;
        case '\r': out += QStringLiteral("\\r"); break;
        case '\t': out += QStringLiteral("\\t"); break;
        default: out += c; break;
        }
    }
    out += QLatin1Char('"');
    return out;
}
} // namespace

ProxyInfo parseProxy(const QString &proxy)
{
    ProxyInfo info;
    QString s = proxy.trimmed();
    if (s.isEmpty())
        return info;

    // Tách scheme nếu có.
    const int schemeEnd = s.indexOf(QStringLiteral("://"));
    if (schemeEnd > 0) {
        const QString sc = s.left(schemeEnd).toLower();
        if (sc != QStringLiteral("http") && sc != QStringLiteral("https") &&
            sc != QStringLiteral("socks4") && sc != QStringLiteral("socks5"))
            return info;
        info.scheme = sc;
        s = s.mid(schemeEnd + 3);
    } else {
        info.scheme = QStringLiteral("http");
    }

    // Tách user:pass@ nếu có (lấy @ cuối cùng).
    const int at = s.lastIndexOf(QLatin1Char('@'));
    if (at > 0) {
        const QString userinfo = s.left(at);
        s = s.mid(at + 1);
        const int colon = userinfo.indexOf(QLatin1Char(':'));
        if (colon < 0)
            return info;
        info.username = userinfo.left(colon);
        info.password = userinfo.mid(colon + 1);
    }

    // host:port
    const int colon = s.lastIndexOf(QLatin1Char(':'));
    if (colon < 0)
        return info;
    info.host = s.left(colon);
    const int port = s.mid(colon + 1).toInt();
    if (info.host.isEmpty() || port <= 0 || port > 65535)
        return info;
    info.port = port;

    info.valid = true;
    return info;
}

QString proxyServerArgument(const ProxyInfo &info)
{
    if (!info.valid)
        return QString();
    return info.scheme + QStringLiteral("://") + info.host + QLatin1Char(':') +
           QString::number(info.port);
}

// Kiểm tra nhanh xem proxy có kết nối được không (TCP connect tới host:port).
// Không kiểm tra được tính hợp lệ của user/pass — chỉ phát hiện proxy chết/không mở.
bool testProxyReachability(const QString &proxy, int timeoutMs)
{
    const ProxyInfo info = parseProxy(proxy);
    if (!info.valid)
        return false;

    QTcpSocket sock;
    sock.connectToHost(info.host, info.port);
    return sock.waitForConnected(qMax(500, timeoutMs));
}

QString writeProxyAuthExtension(const QString &baseDir, const ProxyInfo &info)
{
    if (!info.valid || info.username.isEmpty())
        return QString();

    const QString dir = QDir(baseDir).filePath(QStringLiteral("proxy-auth-ext"));
    QDir().mkpath(dir);

    // background.js — trả lời mọi lời mời xác thực proxy bằng tên/pass đã cấu hình.
    const QString backgroundJs =
        QStringLiteral("chrome.webRequest.onAuthRequired.addListener(\n"
                       "  (details) => ({ authCredentials: { username: %1, password: %2 } }),\n"
                       "  { urls: [\"<all_urls>\"] },\n"
                       "  [\"blocking\"]\n"
                       ");\n")
            .arg(jsString(info.username), jsString(info.password));

    QFile bg(dir + QStringLiteral("/background.js"));
    if (!bg.open(QIODevice::WriteOnly | QIODevice::Text))
        return QString();
    bg.write(backgroundJs.toUtf8());
    bg.close();

    QJsonObject manifest;
    manifest.insert(QStringLiteral("manifest_version"), 3);
    manifest.insert(QStringLiteral("name"), QStringLiteral("AutoPost Proxy Auth"));
    manifest.insert(QStringLiteral("version"), QStringLiteral("1.0"));
    manifest.insert(
        QStringLiteral("permissions"),
        QJsonArray{QStringLiteral("webRequest"), QStringLiteral("webRequestAuthProvider")});
    manifest.insert(QStringLiteral("host_permissions"), QJsonArray{QStringLiteral("<all_urls>")});
    manifest.insert(QStringLiteral("background"),
                    QJsonObject{{QStringLiteral("service_worker"), QStringLiteral("background.js")}});

    QFile mf(dir + QStringLiteral("/manifest.json"));
    if (!mf.open(QIODevice::WriteOnly | QIODevice::Text))
        return QString();
    mf.write(QJsonDocument(manifest).toJson(QJsonDocument::Compact));
    mf.close();

    return dir;
}
