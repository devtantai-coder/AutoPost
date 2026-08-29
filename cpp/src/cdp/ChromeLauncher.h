#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>

#include "cdp/Fingerprint.h"

class QProcess;

class ChromeLauncher : public QObject
{
    Q_OBJECT
public:
    explicit ChromeLauncher(QObject *parent = nullptr);
    ~ChromeLauncher() override;

    bool launch(const QStringList &extraArgs, QString *error);
    void kill();

    // Không giữ profile trên đĩa: xóa ngay khi đóng trình duyệt để tránh ngốn disk.
    void cleanProfileDir();
    // Dọn các profile sót lại từ lần chạy trước (khi app thoát đột ngột).
    static void cleanupAllProfiles();

    int devtoolsPort() const { return m_port; }
    QString lastUserAgent() const { return m_userAgent; }
    // Kích thước window được chọn ngẫu nhiên cho phiên này (để khóa screen.* cho khớp).
    int windowWidth() const { return m_winWidth; }
    int windowHeight() const { return m_winHeight; }
    QString pageTargetWebSocketUrl();
    QString pageTargetWebSocketUrlById(const QString &targetId);
    // Trả về {targetId -> webSocketDebuggerUrl} cho MỘT danh sách target chỉ với
    // 1 lần gọi /json/list (mở tab hàng loạt dùng phương pháp này - không cần
    // query từng target một như trước đây).
    QHash<QString, QString> targetWebSocketUrls(const QStringList &targetIds);
    void setProfileSuffix(const QString &suffix) { m_profileSuffix = suffix; }
    // Proxy cho phiên trình duyệt này (che IP). Rỗng = không dùng.
    void setProxy(const QString &proxy) { m_proxy = proxy; }
    // Persona (vân tay) cố định theo tài khoản — quyết định UA/màn hình/ngôn ngữ
    // của phiên này. Nếu chưa set, ChromeLauncher tự random mỗi lần chạy.
    void setFingerprint(const Fingerprint &fp) { m_fingerprint = fp; m_hasFingerprint = true; }

private:
    static QString findChromeBinary();
    void parsePort();
    QByteArray httpGet(const QString &url);
    // Danh sách tham số liên quan proxy (--proxy-server, extension auth...).
    // profileDir đã sẵn sàng để viết extension.
    QStringList buildProxyArgs(const QString &profileDir);

    QProcess *m_process = nullptr;
    QByteArray m_outputBuffer;
    QString m_profileDir;
    QString m_profileSuffix;
    QString m_proxy;
    QString m_userAgent;
    Fingerprint m_fingerprint;
    bool m_hasFingerprint = false;
    int m_port = 0;
    int m_winWidth = 0;
    int m_winHeight = 0;
};
