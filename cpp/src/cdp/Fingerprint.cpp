#include "Fingerprint.h"

#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QVector>

namespace
{
// Băm accountId thành seed 64-bit ổn định giữa các lần chạy.
quint64 seedFromId(const QString &accountId)
{
    if (accountId.isEmpty())
        return 0;
    const QByteArray h =
        QCryptographicHash::hash(accountId.toUtf8(), QCryptographicHash::Sha256);
    quint64 seed = 0;
    for (int i = 0; i < 8; ++i)
        seed = (seed << 8) | quint8(h.at(i));
    return seed;
}

// Một persona cơ sở: UA + platform + ngôn ngữ + múi giờ + màn hình ĐỒNG BỘ.
struct BasePersona
{
    QString userAgent;
    QString platform;
    QString acceptLanguage;
    QStringList languages;
    QString locale;
    QString timezone;
    int screenWidth;
    int screenHeight;
};

const QVector<BasePersona> &basePersonas()
{
    // UA Chrome hiện hành (giữa 2026); bản cũ 123–126 đã lỗi thời là tín hiệu máy.
    static const QVector<BasePersona> list = {
        {QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                        "(KHTML, like Gecko) Chrome/152.0.0.0 Safari/537.36"),
         QStringLiteral("Win32"), QStringLiteral("en-US,en;q=0.9"),
         QStringList{QStringLiteral("en-US"), QStringLiteral("en")},
         QStringLiteral("en-US"), QStringLiteral("America/Chicago"), 1920, 1080},
        {QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                        "(KHTML, like Gecko) Chrome/151.0.0.0 Safari/537.36"),
         QStringLiteral("Win32"), QStringLiteral("en-US,en;q=0.9"),
         QStringList{QStringLiteral("en-US"), QStringLiteral("en")},
         QStringLiteral("en-US"), QStringLiteral("America/New_York"), 1536, 864},
        {QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                        "(KHTML, like Gecko) Chrome/152.0.0.0 Safari/537.36"),
         QStringLiteral("Linux x86_64"),
         QStringLiteral("vi-VN,vi;q=0.9,en-US;q=0.8,en;q=0.7"),
         QStringList{QStringLiteral("vi-VN"), QStringLiteral("vi"), QStringLiteral("en-US"),
                     QStringLiteral("en")},
         QStringLiteral("vi-VN"), QStringLiteral("Asia/Ho_Chi_Minh"), 1366, 768},
        {QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
                        "(KHTML, like Gecko) Chrome/150.0.0.0 Safari/537.36"),
         QStringLiteral("Win32"), QStringLiteral("en-GB,en;q=0.9"),
         QStringList{QStringLiteral("en-GB"), QStringLiteral("en")},
         QStringLiteral("en-GB"), QStringLiteral("Europe/London"), 1440, 900},
        {QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 "
                        "(KHTML, like Gecko) Chrome/151.0.0.0 Safari/537.36"),
         QStringLiteral("Linux x86_64"),
         QStringLiteral("vi-VN,vi;q=0.9,en-US;q=0.8,en;q=0.7"),
         QStringList{QStringLiteral("vi-VN"), QStringLiteral("vi"), QStringLiteral("en-US"),
                     QStringLiteral("en")},
         QStringLiteral("vi-VN"), QStringLiteral("Asia/Ho_Chi_Minh"), 1600, 900},
    };
    return list;
}

const QVector<int> &concurrencyChoices()
{
    static const QVector<int> c = {4, 4, 8, 8, 8, 16};
    return c;
}

const QVector<int> &memoryChoices()
{
    // Chrome chỉ báo deviceMemory tối đa 8 — giá trị 16 KHÔNG tồn tại ngoài đời
    // thực, là tín hiệu phát hiện vân tay giả.
    static const QVector<int> m = {4, 4, 8, 8};
    return m;
}

const QVector<QString> &vendorChoices()
{
    static const QVector<QString> v = {
        QStringLiteral("Google Inc. (NVIDIA)"), QStringLiteral("Google Inc. (AMD)"),
        QStringLiteral("Google Inc. (Intel)")};
    return v;
}

const QVector<QString> &rendererChoices()
{
    static const QVector<QString> r = {
        QStringLiteral("ANGLE (NVIDIA, NVIDIA GeForce GTX 1650 Direct3D11 vs_5_0 ps_5_0, D3D11)"),
        QStringLiteral("ANGLE (NVIDIA, NVIDIA GeForce RTX 3060 Direct3D11 vs_5_0 ps_5_0, D3D11)"),
        QStringLiteral("ANGLE (NVIDIA, NVIDIA GeForce GT 1030 Direct3D11 vs_5_0 ps_5_0, D3D11)"),
        QStringLiteral("ANGLE (AMD, AMD Radeon RX 580 Graphics Direct3D11 vs_5_0 ps_5_0, D3D11)"),
        QStringLiteral("ANGLE (Intel, Intel(R) UHD Graphics 630 Direct3D11 vs_5_0 ps_5_0, D3D11)"),
        QStringLiteral("ANGLE (Intel, Intel(R) Iris(R) Xe Graphics Direct3D11 vs_5_0 ps_5_0, D3D11)")};
    return r;
}
} // namespace

Fingerprint Fingerprints::forAccount(const QString &accountId)
{
    Fingerprint fp;
    const quint64 seed = seedFromId(accountId);
    QRandomGenerator rng(seed);
    fp.seed = seed;

    const QVector<BasePersona> &bases = basePersonas();
    const BasePersona &base = bases.at(int(rng.bounded(bases.size())));
    fp.userAgent = base.userAgent;
    fp.platform = base.platform;
    fp.acceptLanguage = base.acceptLanguage;
    fp.languages = base.languages;
    fp.locale = base.locale;
    fp.timezone = base.timezone;
    fp.screenWidth = base.screenWidth;
    fp.screenHeight = base.screenHeight;

    const QVector<int> &cc = concurrencyChoices();
    fp.hardwareConcurrency = cc.at(int(rng.bounded(cc.size())));
    const QVector<int> &mc = memoryChoices();
    fp.deviceMemory = mc.at(int(rng.bounded(mc.size())));
    const QVector<QString> &vc = vendorChoices();
    fp.webglVendor = vc.at(int(rng.bounded(vc.size())));
    const QVector<QString> &rc = rendererChoices();
    fp.webglRenderer = rc.at(int(rng.bounded(rc.size())));

    return fp;
}

Fingerprint Fingerprints::forDefaultUa(const QString &ua, int winWidth, int winHeight)
{
    Fingerprint fp;
    // Seed cố định theo UA của phiên — các tab trong trình duyệt tính ra CÙNG
    // giá trị phần cứng/WebGL (trước đây mỗi tab random WebGL/vendor/cores lệch
    // nhau — xem như cùng 1 máy mà GPU/CPU thay đổi liên tục = điểm machine).
    const quint64 seed = seedFromId(ua);
    QRandomGenerator rng(seed);
    fp.seed = seed;

    const bool linuxUa = ua.contains(QLatin1String("Linux"));
    fp.userAgent = ua;
    fp.platform = linuxUa ? QStringLiteral("Linux x86_64") : QStringLiteral("Win32");
    fp.acceptLanguage = linuxUa
                            ? QStringLiteral("vi-VN,vi;q=0.9,en-US;q=0.8,en;q=0.7")
                            : QStringLiteral("en-US,en;q=0.9");
    fp.languages = linuxUa ? QStringList{QStringLiteral("vi-VN"), QStringLiteral("vi"),
                                          QStringLiteral("en-US"), QStringLiteral("en")}
                            : QStringList{QStringLiteral("en-US"), QStringLiteral("en")};
    fp.locale = linuxUa ? QStringLiteral("vi-VN") : QStringLiteral("en-US");
    fp.timezone = linuxUa ? QStringLiteral("Asia/Ho_Chi_Minh") : QStringLiteral("America/New_York");
    fp.screenWidth = winWidth;
    fp.screenHeight = winHeight;

    const QVector<int> &cc = concurrencyChoices();
    fp.hardwareConcurrency = cc.at(int(rng.bounded(cc.size())));
    const QVector<int> &mc = memoryChoices();
    fp.deviceMemory = mc.at(int(rng.bounded(mc.size())));
    const QVector<QString> &vc = vendorChoices();
    fp.webglVendor = vc.at(int(rng.bounded(vc.size())));
    const QVector<QString> &rc = rendererChoices();
    fp.webglRenderer = rc.at(int(rng.bounded(rc.size())));

    return fp;
}
