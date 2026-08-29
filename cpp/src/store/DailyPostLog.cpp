#include "store/DailyPostLog.h"

#include "store/DataStore.h"

#include <QDate>
#include <QFile>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QTextStream>
#include <QTime>

#include <algorithm>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace DailyPostLog
{

namespace
{
QString dayKey()
{
    return QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
}

// Toàn bộ posts.json được GIỮ TRONG RAM (m_root). Trước đây MỖI bài đăng:
// đọc file + parse toàn bộ + append + dump + ghi file (4-5 thao tác I/O trên
// mutex). Nay: append vào RAM, chỉ GHI GỘP khi đủ 64 bài chờ (hoặc flush khi
// kết thúc phiên) — hàng trăm bài dồn dập cũng chỉ vài lần ghi đĩa.
// Không dùng QTimer: logPost được gọi từ nhiều tab thread khác nhau qua từng
// vòng; timer toàn cục sang thread chết là nguồn treo tiềm ẩn.
json g_root;
bool g_loaded = false;
bool g_dirty = false;
int g_pending = 0;
constexpr int kFlushEveryPosts = 64;

void ensureLoaded()
{
    if (g_loaded)
        return;
    g_loaded = true;
    const std::string text = DataStore::readText(QStringLiteral("posts.json"));
    g_root = json::parse(text, nullptr, false);
    if (!g_root.is_object())
        g_root = json::object();

    // Giới hạn dữ liệu 30 ngày gần nhất để file không phình vô hạn khi dùng lâu.
    if (g_root.size() > 30) {
        QStringList days;
        for (auto it = g_root.begin(); it != g_root.end(); ++it)
            days.append(QString::fromStdString(it.key()));
        std::sort(days.begin(), days.end());
        const int removeCount = days.size() - 30;
        for (int i = 0; i < removeCount; ++i)
            g_root.erase(days.at(i).toStdString());
        g_dirty = true;
    }
}

void writeAll()
{
    if (!g_dirty)
        return;
    g_dirty = false;
    DataStore::writeText(QStringLiteral("posts.json"), g_root.dump(4));
}

// Tên file an toàn từ tên tài khoản (khách thuê).
QString safeName(const QString &raw)
{
    QString s = raw.trimmed();
    s.replace(QLatin1Char(' '), QLatin1Char('_'));
    static const QRegularExpression bad(QStringLiteral("[^\\w._-]"),
                                        QRegularExpression::UseUnicodePropertiesOption);
    s.remove(bad);
    if (s.isEmpty())
        s = QStringLiteral("Tk");
    return s;
}
} // namespace

int countToday(const QString &accountId)
{
    if (accountId.isEmpty())
        return 0;

    QMutexLocker lock(&DataStore::mutex());
    ensureLoaded();
    const std::string day = dayKey().toStdString();
    int ok = 0;
    if (auto dit = g_root.find(day); dit != g_root.end() && dit->is_object()) {
        if (auto ait = dit->find(accountId.toStdString()); ait != dit->end() && ait->is_array()) {
            for (const json &e : ait.value())
                if (e.is_object() && e.value("ok", false))
                    ++ok;
        }
    }
    return ok;
}

void logPost(const QString &accountId, const QString &accountName, const QString &groupName,
             bool ok)
{
    const QString day = dayKey();
    const QString now = QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
    bool flushNow = false;

    // Cộng dồn vào RAM; chỉ ghi đĩa khi đủ 64 bài chờ (gộp nhiều bài thành 1
    // lần ghi) hoặc khi flush() được gọi cuối phiên.
    {
        QMutexLocker lock(&DataStore::mutex());
        ensureLoaded();

        const std::string dayStr = day.toStdString();
        json &dayObj = g_root[dayStr];
        if (!dayObj.is_object())
            dayObj = json::object();
        json &arr = dayObj[accountId.toStdString()];
        if (!arr.is_array())
            arr = json::array();

        json e = json::object();
        e["time"] = now.toStdString();
        e["account"] = accountName.toStdString();
        e["group"] = groupName.toStdString();
        e["ok"] = ok;
        arr.push_back(e);
        g_dirty = true;
        flushNow = (++g_pending >= kFlushEveryPosts);
        if (flushNow)
            g_pending = 0;
    }

    if (flushNow) {
        QMutexLocker lock(&DataStore::mutex());
        writeAll();
    }

    const QString result = ok ? QStringLiteral("Thành công") : QStringLiteral("Thất bại");
    const QString line = now + QStringLiteral(" | ") + accountName + QStringLiteral(" | ") +
                         groupName + QStringLiteral(" | ") + result + QLatin1Char('\n');

    // Nhật ký tổng hợp theo ngày (cho người xem trực tiếp) — lưu trong thư mục data.
    QFile allLog(DataStore::filePath(QStringLiteral("nhat_ky_bai_") + day +
                                     QStringLiteral(".log")));
    if (allLog.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream(&allLog) << line;
        allLog.close();
    }

    // Nhật ký tách riêng theo tên tài khoản (khách thuê) — lưu trong thư mục data.
    QFile accLog(DataStore::filePath(QStringLiteral("nhat_ky_") + safeName(accountName) +
                                     QStringLiteral("_") + day + QStringLiteral(".log")));
    if (accLog.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream(&accLog) << line;
        accLog.close();
    }
}

void flush()
{
    QMutexLocker lock(&DataStore::mutex());
    writeAll();
}

} // namespace DailyPostLog
