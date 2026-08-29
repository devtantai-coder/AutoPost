#include "utils/Utils.h"

#include <QRegularExpression>
#include <QThread>
#include <QRandomGenerator>

#include <algorithm>
#include <array>
#include <cmath>
#include <ranges>

#include <fmt/format.h>

#if defined(Q_OS_LINUX)
#include <unistd.h>
#elif defined(Q_OS_WIN)
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include <sys/sysctl.h>
#include <sys/types.h>
#endif

QString Utils::formatNumber(qint64 n)
{
    if (n >= 1'000'000)
        return QString::fromStdString(fmt::format("{:.1f}M", n / 1'000'000.0));
    if (n >= 1'000)
        return QString::fromStdString(fmt::format("{:.1f}K", n / 1'000.0));
    return QString::number(n);
}

void Utils::sleepMs(int ms)
{
    QThread::msleep(qMax(0, ms));
}

int Utils::exponentialDelayMs(int baseMs, int maxMs)
{
    if (baseMs <= 0)
        return 0;
    // -ln(1-U)/lambda với lambda = 1/baseMs: giá trị tập trung quanh baseMs nhưng
    // thỉnh thoảng dài hơn nhiều — giống thời gian phản hồi của con người.
    const double u = QRandomGenerator::global()->generateDouble();
    const double v = -std::log(1.0 - qMin(0.999999, u)) * baseMs;
    const int lo = qMax(1, int(baseMs * 0.3));
    return qBound(lo, int(v), qMax(lo, maxMs));
}

void Utils::exponentialPause(int baseMs, int maxMs)
{
    sleepMs(exponentialDelayMs(baseMs, maxMs));
}

QString Utils::extractGroupId(const QString &href)
{
    static const QRegularExpression groupRe(QStringLiteral("(?:/groups/)(\\d+)"));
    const QRegularExpressionMatch gm = groupRe.match(href);
    if (gm.hasMatch())
        return gm.captured(1);

    // Chấp nhận cả chuỗi ID trần khi người dùng dán trực tiếp.
    static const QRegularExpression digitsRe(QStringLiteral("(\\d{8,})"));
    const QRegularExpressionMatch dm = digitsRe.match(href);
    return dm.hasMatch() ? dm.captured(1) : QString();
}

QString Utils::normalizePrivacy(const QString &raw)
{
    const QString t = raw.toLower();
    if (t.contains(QStringLiteral("riêng tư")) || t.contains(QStringLiteral("private")) ||
        t.contains(QStringLiteral("kín")) || t.contains(QStringLiteral("closed")))
        return QStringLiteral("riêng tư");
    if (t.contains(QStringLiteral("công khai")) || t.contains(QStringLiteral("public")))
        return QStringLiteral("công khai");
    if (t.contains(QStringLiteral("chia sẻ")) || t.contains(QStringLiteral("shared")) ||
        t.contains(QStringLiteral("hiển thị")) || t.contains(QStringLiteral("visible")))
        return QStringLiteral("chia sẻ");
    if (t.contains(QStringLiteral("ẩn")) || t.contains(QStringLiteral("hidden")))
        return QStringLiteral("ẩn");
    return QStringLiteral("chưa rõ");
}

int Utils::randomInt(int min, int max)
{
    if (max <= min)
        return min;
    return QRandomGenerator::global()->bounded(min, max + 1);
}

void Utils::humanPause(int minMs, int maxMs)
{
    sleepMs(randomInt(minMs, maxMs));
}

QString Utils::randomUserAgent()
{
    // UA theo Chrome hiện hành (giữa 2026): bản cũ (123–126, năm 2024) đã là
    // tín hiệu automation rõ ràng với Facebook vì không khớp dòng thời gian IP.
    static const QStringList uas = {
        QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/152.0.0.0 Safari/537.36"),
        QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/151.0.0.0 Safari/537.36"),
        QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/152.0.0.0 Safari/537.36"),
        QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/150.0.0.0 Safari/537.36"),
    };
    return uas.at(QRandomGenerator::global()->bounded(uas.size()));
}

QString Utils::platformForUserAgent(const QString &ua)
{
    if (ua.contains(QStringLiteral("Linux")))
        return QStringLiteral("Linux x86_64");
    return QStringLiteral("Win32");
}

qint64 Utils::physicalMemoryMB()
{
#if defined(Q_OS_WIN)
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);
    if (GlobalMemoryStatusEx(&statex))
        return static_cast<qint64>(statex.ullTotalPhys) / (1024 * 1024);
#elif defined(Q_OS_LINUX)
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGESIZE);
    if (pages > 0 && pageSize > 0)
        return static_cast<qint64>(pages) * pageSize / (1024 * 1024);
#elif defined(Q_OS_MACOS)
    int mib[2] = {CTL_HW, HW_MEMSIZE};
    uint64_t mem = 0;
    size_t len = sizeof(mem);
    if (sysctl(mib, 2, &mem, &len, nullptr, 0) == 0)
        return static_cast<qint64>(mem) / (1024 * 1024);
#endif
    return 0;
}

void Utils::recommendParallelism(int *outThreads, int *outTabs)
{
    const int cores = qMax(1, QThread::idealThreadCount());
    const qint64 ramMB = physicalMemoryMB();

    // Khởi điểm theo CPU: 1 trình duyệt cho ~2 luồng CPU, mỗi trình duyệt mở
    // bằng số luồng CPU tab (giới hạn theo UI: 5 trình duyệt × 10 tab).
    int threads = qBound(1, (cores + 1) / 2, 5);
    int tabs = qBound(1, cores, 10);

    if (ramMB > 0) {
        // Dự trù ~2GB cho hệ điều hành + bản thân ứng dụng.
        const qint64 usable = qMax<qint64>(512, ramMB - 2048);
        // Mỗi trình duyệt Chrome ngốn ~450MB nền + ~150MB cho mỗi tab mở.
        const auto estimate = [](int t, int b) { return qint64(t) * (450 + 150 * b); };
        // Cắt giảm dần (tab trước, rồi trình duyệt) cho tới khi vừa RAM.
        while (estimate(threads, tabs) > usable && (threads > 1 || tabs > 1)) {
            if (tabs > 1)
                --tabs;
            else if (threads > 1)
                --threads;
            else
                break;
        }
    }

    if (outThreads)
        *outThreads = qBound(1, threads, 5);
    if (outTabs)
        *outTabs = qBound(1, tabs, 10);
}

int Utils::randomHardwareConcurrency()
{
    static constexpr std::array choices = {4, 4, 8, 8, 8, 16};
    return choices.at(QRandomGenerator::global()->bounded(int(choices.size())));
}

int Utils::randomDeviceMemory()
{
    static constexpr std::array choices = {4, 8, 8, 16};
    return choices.at(QRandomGenerator::global()->bounded(int(choices.size())));
}

QString Utils::randomWebglVendor()
{
    static const QStringList vendors = {
        QStringLiteral("Google Inc. (NVIDIA)"),
        QStringLiteral("Google Inc. (AMD)"),
        QStringLiteral("Google Inc. (Intel)"),
        QStringLiteral("Google Inc. (Apple)"),
    };
    return vendors.at(QRandomGenerator::global()->bounded(vendors.size()));
}

QString Utils::randomWebglRenderer()
{
    static const QStringList renderers = {
        QStringLiteral("ANGLE (NVIDIA, NVIDIA GeForce GTX 1650 Direct3D11 vs_5_0 ps_5_0, D3D11)"),
        QStringLiteral("ANGLE (NVIDIA, NVIDIA GeForce RTX 3060 Direct3D11 vs_5_0 ps_5_0, D3D11)"),
        QStringLiteral("ANGLE (NVIDIA, NVIDIA GeForce GT 1030 Direct3D11 vs_5_0 ps_5_0, D3D11)"),
        QStringLiteral("ANGLE (AMD, AMD Radeon RX 580 Graphics Direct3D11 vs_5_0 ps_5_0, D3D11)"),
        QStringLiteral("ANGLE (Intel, Intel(R) UHD Graphics 630 Direct3D11 vs_5_0 ps_5_0, D3D11)"),
        QStringLiteral("ANGLE (Intel, Intel(R) Iris(R) Xe Graphics Direct3D11 vs_5_0 ps_5_0, D3D11)"),
    };
    return renderers.at(QRandomGenerator::global()->bounded(renderers.size()));
}
