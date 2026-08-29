#pragma once

#include <QJsonObject>
#include <QString>
#include <QVector>

#include "store/Config.h"
#include "model/FacebookAccount.h"
#include "model/FacebookGroup.h"
#include "model/RentedAccount.h"

namespace DashboardStore
{
// Tổng hợp nhanh cho dashboard, đọc trực tiếp từ posts.json / posted_today.json
// trong thư mục data (không dùng database).
struct DailyStat
{
    QString day;
    int total = 0;
    int ok = 0;
    int fail = 0;
};

struct AccountStat
{
    QString accountId;
    int total = 0;
    int ok = 0;
};

struct PostEntry
{
    QString time;
    QString account;
    QString group;
    bool ok = false;
};

struct DashboardStats
{
    QVector<DailyStat> days;         // mọi ngày, mới nhất trước
    QVector<AccountStat> accountToday;
    QVector<PostEntry> recentToday;  // tối đa 40 bài của hôm nay, mới nhất trước
};

DashboardStats loadStats();

// Đọc toàn bộ nhật ký bài đăng (data/posts.json).
QJsonObject loadDailyLog();

// Đọc các nhóm đã đăng hôm nay (data/posted_today.json).
QJsonObject loadPostedToday();

// Lưu "full JSON": bó toàn bộ dữ liệu (nhóm, tài khoản, cấu hình, nhật ký bài đăng,
// nhật ký màn hình, posted) vào một file.
bool saveAll(const QString &path, const QVector<FacebookGroup> &groups,
             const QVector<FacebookAccount> &accounts, const Config &config,
             const QString &logText);

// Nạp lại từ file JSON đầy đủ. Trả về false nếu lỗi.
bool loadAll(const QString &path, QVector<FacebookGroup> *groups,
             QVector<FacebookAccount> *accounts);

// Xuất danh sách tài khoản ra mảng JSON.
QJsonArray accountsToJson(const QVector<FacebookAccount> &accounts);

// Nạp tài khoản từ file JSON: nhận mảng tài khoản, hoặc object có khóa "accounts".
bool accountsFromJsonFile(const QString &path, QVector<FacebookAccount> *out);

// Xuất danh sách tài khoản thuê ra mảng JSON.
QJsonArray rentedToJson(const QVector<RentedAccount> &rented);

// Nạp tài khoản thuê từ file JSON (mảng trực tiếp hoặc khóa "rented").
bool rentedFromJsonFile(const QString &path, QVector<RentedAccount> *out);

// Lưu / nạp toàn bộ tài khoản thuê vào data/rented.json.
bool saveRented(const QString &path, const QVector<RentedAccount> &rented);
bool loadRented(const QString &path, QVector<RentedAccount> *out);
} // namespace DashboardStore
