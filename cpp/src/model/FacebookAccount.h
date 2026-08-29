#pragma once

#include <QMetaType>
#include <QString>

// Một tài khoản Facebook (được nhận diện bằng cookie).
struct FacebookAccount
{
    QString id;
    QString name;
    QString cookieRaw;
    // Proxy riêng cho tài khoản (để che IP): http://host:port, socks5://host:port,
    // host:port, hoặc có user/pass: user:pass@host:port. Rỗng = không dùng proxy.
    QString proxy;
    QString status = QStringLiteral("hoạt động"); // "hoạt động" | "bị chặn" | "tạm dừng"
    int failCount = 0;
    bool selected = true;
};
Q_DECLARE_METATYPE(FacebookAccount)
