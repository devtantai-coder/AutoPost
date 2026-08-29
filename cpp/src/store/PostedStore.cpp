#include "store/PostedStore.h"

#include "store/DataStore.h"

#include <QDate>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QTimer>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace PostedStore
{

namespace
{
// Cache ngày hiện tại: đọc file 1 lần/ngày thay vì mỗi lần gọi isPostedToday
// (đăng 200+ nhóm = cũ đọc/parse JSON 200+ lần nối tiếp nhau trên 1 mutex).
QString todayKey()
{
    return QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
}

// Cache ngày hiện tại + các ngày khác truy cập gần đây (DashboardStore đọc
// ngày hôm qua...). Giới hạn 8 ngày.
QMutex g_mutex;
QHash<QString, QSet<QString>> g_cache;
bool g_dirty = false;
QTimer *g_flushTimer = nullptr;

QSet<QString> loadDay(const QString &day)
{
    QSet<QString> out;
    const std::string text = DataStore::readText(QStringLiteral("posted_today.json"));
    json root = json::parse(text, nullptr, false);
    if (!root.is_object())
        return out;
    const json &arr = root.value(day.toStdString(), json::array());
    for (const json &v : arr)
        if (v.is_string())
            out.insert(QString::fromStdString(v.get<std::string>()));
    return out;
}

void writeAll()
{
    json root = json::object();
    for (auto it = g_cache.cbegin(); it != g_cache.cend(); ++it) {
        json arr = json::array();
        for (const QString &id : it.value())
            arr.push_back(id.toStdString());
        root[it.key().toStdString()] = arr;
    }
    DataStore::writeText(QStringLiteral("posted_today.json"), root.dump(4));
}

QSet<QString> &dayEntry(const QString &day)
{
    auto it = g_cache.find(day);
    if (it == g_cache.end())
        it = g_cache.insert(day, loadDay(day));
    if (g_cache.size() > 8) {
        // Bỏ ngày cũ nhất khác hôm nay để cache không phình.
        const QString today = todayKey();
        for (auto vit = g_cache.begin(); vit != g_cache.end();) {
            if (vit.key() != today && vit.key() != day)
                vit = g_cache.erase(vit);
            else
                ++vit;
        }
    }
    return it.value();
}

void scheduleFlush()
{
    if (!g_dirty)
        return;
    g_dirty = false;
    writeAll();
}
} // namespace

bool isPostedToday(const QString &groupId)
{
    if (groupId.isEmpty())
        return false;

    QMutexLocker lock(&g_mutex);
    return dayEntry(todayKey()).contains(groupId);
}

void markPosted(const QString &groupId)
{
    if (groupId.isEmpty())
        return;

    QMutexLocker lock(&g_mutex);
    const QString day = todayKey();
    QSet<QString> &ids = dayEntry(day);
    if (ids.contains(groupId))
        return;
    ids.insert(groupId);
    g_dirty = true;

    // Ghi ra đĩa giãn cách 5s: hàng trăm nhóm dồn dập cũng chỉ vài lần ghi.
    // File được ghi tuần tự trong cùng thread (FbWorker) nên an toàn; lần ghi
    // cuối luôn đảm bảo trước khi phiên kết thúc.
    if (!g_flushTimer) {
        g_flushTimer = new QTimer();
        g_flushTimer->setSingleShot(true);
        QObject::connect(g_flushTimer, &QTimer::timeout, &scheduleFlush);
    }
    if (!g_flushTimer->isActive())
        g_flushTimer->start(5000);
}

// Gọi khi đóng app để đảm bảo ghi hết xuống đĩa trước khi thoát.
void flush()
{
    QMutexLocker lock(&g_mutex);
    scheduleFlush();
}

} // namespace PostedStore
