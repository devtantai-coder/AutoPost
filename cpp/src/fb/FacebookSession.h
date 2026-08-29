#pragma once

#include <QString>

#include "cdp/ChromeLauncher.h"
#include "cdp/CdpClient.h"
#include "cdp/WebDriver.h"
#include "cdp/Fingerprint.h"

// Quản lý vòng đời Chrome + kết nối CDP + tiêm cookie (tương đương initializeDriver trong Java).
class FacebookSession
{
public:
    FacebookSession() = default;
    ~FacebookSession();

    bool start(const QString &cookieRaw, bool headless, QString *error);
    void stop();
    void setProfileSuffix(const QString &suffix) { m_launcher.setProfileSuffix(suffix); }
    // Gắn proxy che IP cho phiên này (rỗng = không dùng).
    void setProxy(const QString &proxy) { m_launcher.setProxy(proxy); }
    // Chọn persona (vân tay) cố định theo tài khoản — gọi TRƯỚC start().
    // Cùng accountId luôn ra cùng vân tay; accountId khác ra vân tay khác.
    void setFingerprintSeed(const QString &seed)
    {
        m_fingerprint = Fingerprints::forAccount(seed);
        m_fingerprintSet = !seed.isEmpty();
    }
    WebDriver *driver() { return &m_driver; }
    QString userAgent() const
    {
        return m_fingerprintSet ? m_fingerprint.userAgent : m_launcher.lastUserAgent();
    }
    int windowWidth() const
    {
        return m_fingerprintSet ? m_fingerprint.screenWidth : m_launcher.windowWidth();
    }
    int windowHeight() const
    {
        return m_fingerprintSet ? m_fingerprint.screenHeight : m_launcher.windowHeight();
    }
    const Fingerprint &fingerprint() const { return m_fingerprint; }
    bool hasFingerprint() const { return m_fingerprintSet; }

    // Tạo một tab mới trong cùng trình duyệt, trả về WebSocket URL của tab đó
    // (dùng để kết nối thêm CdpClient/WebDriver từ thread khác).
    QString openNewTab();
    // Mở N tab hàng loạt (1 lệnh CDP/tab, resolve wsUrl theo lô 1 lần HTTP) —
    // nhanh hơn hẳn việc gọi openNewTab N lần liên tiếp.
    QStringList openNewTabs(int count);
    // Xuất toàn bộ cookie facebook.com đang có thành chuỗi "name=value; ..."
    // (định dạng giống cookie dán tay — dùng sau khi tự đăng ký acc).
    QString exportCookies();

private:
    bool injectCookies(const QString &cookieRaw);

    ChromeLauncher m_launcher;
    CdpClient m_cdp;
    WebDriver m_driver;
    Fingerprint m_fingerprint;
    bool m_fingerprintSet = false;
    bool m_started = false;
};
