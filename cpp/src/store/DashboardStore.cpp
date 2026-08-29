#include "store/DashboardStore.h"

#include "store/DailyPostLog.h"
#include "store/DataStore.h"
#include "store/PostedStore.h"

#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMutexLocker>
#include <QVariant>

#include <algorithm>

namespace DashboardStore
{

namespace
{
// Kích thước tối đa của khối "log" (nhật ký màn hình) lưu vào dashboard.json:
// nhật ký đầy đủ có thể hàng trăm KB; cắt tại mốc này để file không phình và
// mỗi lần auto-save (30s) không phải tuần tự hóa/triết ra khối văn bản khổng lồ.
constexpr int kMaxDashboardLogChars = 20000;
// posts.json: { "ngày": { "accountId": [ {time, account, group, ok} ... ] } }
QJsonObject loadPosts()
{
    // Không còn flush-then-read (2 lần khóa DataStore::mutex + 2 lần I/O đĩa
    // mỗi lần dashboard được vẽ): đọc file TRỰC TIẾP trong đúng 1 khóa. Dữ
    // liệu RAM cache của DailyPostLog chỉ lệch tối đa 64 bài / 5 giây so với
    // đĩa — dashboard là thống kê, không cần thời gian thực chính xác đến mức
    // phải trả giá 2 lần khóa mutex trên UI thread.
    QMutexLocker lock(&DataStore::mutex());
    return DataStore::readObject(QStringLiteral("posts.json"));
}
} // namespace

DashboardStats loadStats()
{
    DashboardStats s;
    const QString today = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    const QJsonObject posts = loadPosts();

    QVariantMap dayTotals;
    QVariantMap dayOks;

    const auto keys = posts.keys();
    for (const QString &day : keys) {
        const QJsonObject dayObj = posts.value(day).toObject();
        int total = 0;
        int ok = 0;
        QMap<QString, int> accTodayTotal;
        QMap<QString, int> accTodayOk;
        QVector<PostEntry> entries;

        const auto accIds = dayObj.keys();
        for (const QString &accId : accIds) {
            const QJsonArray arr = dayObj.value(accId).toArray();
            for (const QJsonValue &v : arr) {
                const QJsonObject e = v.toObject();
                ++total;
                const bool okFlag = e.value(QStringLiteral("ok")).toBool();
                if (okFlag)
                    ++ok;
                if (day == today) {
                    accTodayTotal[accId] = accTodayTotal.value(accId) + 1;
                    if (okFlag)
                        accTodayOk[accId] = accTodayOk.value(accId) + 1;
                    PostEntry pe;
                    pe.time = e.value(QStringLiteral("time")).toString();
                    pe.account = e.value(QStringLiteral("account")).toString();
                    pe.group = e.value(QStringLiteral("group")).toString();
                    pe.ok = okFlag;
                    entries.append(pe);
                }
            }
        }

        dayTotals.insert(day, total);
        dayOks.insert(day, ok);

        if (day == today) {
            auto it = accTodayTotal.constBegin();
            for (; it != accTodayTotal.constEnd(); ++it) {
                AccountStat st;
                st.accountId = it.key();
                st.total = it.value();
                st.ok = accTodayOk.value(it.key());
                s.accountToday.append(st);
            }

            std::sort(entries.begin(), entries.end(),
                      [](const PostEntry &a, const PostEntry &b) {
                          return a.time > b.time;
                      });
            const int take = qMin(40, entries.size());
            for (int i = 0; i < take; ++i)
                s.recentToday.append(entries.at(i));
        }
    }

    // Sắp xếp các ngày: mới nhất trước.
    QStringList dayList = dayTotals.keys();
    std::sort(dayList.begin(), dayList.end(), std::greater<QString>());
    for (const QString &day : dayList) {
        DailyStat st;
        st.day = day;
        st.total = dayTotals.value(day).toInt();
        st.ok = dayOks.value(day).toInt();
        st.fail = st.total - st.ok;
        s.days.append(st);
    }

    return s;
}

QJsonObject loadDailyLog()
{
    return loadPosts();
}

QJsonObject loadPostedToday()
{
    // Tương tự loadPosts: đọc trong 1 khóa, không xả cache PostedStore (5s)
    // chỉ để lấy số liệu thống kê — tránh spike I/O trên UI thread.
    QMutexLocker lock(&DataStore::mutex());
    return DataStore::readObject(QStringLiteral("posted_today.json"));
}

bool saveAll(const QString &path, const QVector<FacebookGroup> &groups,
             const QVector<FacebookAccount> &accounts, const Config &config,
             const QString &logText)
{
    QJsonObject root;

    QJsonArray groupsArr;
    for (const FacebookGroup &g : groups) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), g.id);
        o.insert(QStringLiteral("name"), g.name);
        o.insert(QStringLiteral("url"), g.url);
        o.insert(QStringLiteral("privacy"), g.privacy);
        o.insert(QStringLiteral("members"), double(g.memberCount));
        o.insert(QStringLiteral("isMember"), g.isMember);
        o.insert(QStringLiteral("pending"), g.pending);
        o.insert(QStringLiteral("selected"), g.selected);
        groupsArr.append(o);
    }
    root.insert(QStringLiteral("groups"), groupsArr);

    root.insert(QStringLiteral("accounts"), accountsToJson(accounts));

    QJsonObject cfg;
    cfg.insert(QStringLiteral("delaySec"), config.delaySec);
    cfg.insert(QStringLiteral("delayType"), config.delayType);
    cfg.insert(QStringLiteral("retryCount"), config.retryCount);
    cfg.insert(QStringLiteral("threadCount"), config.threadCount);
    cfg.insert(QStringLiteral("tabCount"), config.tabCount);
    cfg.insert(QStringLiteral("dailyPostLimit"), config.dailyPostLimit);
    cfg.insert(QStringLiteral("joinDelaySec"), config.joinDelaySec);
    cfg.insert(QStringLiteral("maxGroups"), config.maxGroups);
    cfg.insert(QStringLiteral("joinAction"), config.joinAction);
    cfg.insert(QStringLiteral("randomDelay"), config.randomDelay);
    cfg.insert(QStringLiteral("headless"), config.headless);
    cfg.insert(QStringLiteral("saveSession"), config.saveSession);
    cfg.insert(QStringLiteral("autoJoin"), config.autoJoin);
    cfg.insert(QStringLiteral("skipPrivate"), config.skipPrivate);
    cfg.insert(QStringLiteral("skipPending"), config.skipPending);
    cfg.insert(QStringLiteral("skipPostedToday"), config.skipPostedToday);
    cfg.insert(QStringLiteral("scheduleEnabled"), config.scheduleEnabled);
    cfg.insert(QStringLiteral("scheduleTime"), config.scheduleTime);
    cfg.insert(QStringLiteral("rotateAccounts"), config.rotateAccounts);
    cfg.insert(QStringLiteral("rotateFailThreshold"), config.rotateFailThreshold);
    root.insert(QStringLiteral("config"), cfg);

    root.insert(QStringLiteral("daily_log"), loadDailyLog());
    root.insert(QStringLiteral("posted_today"), loadPostedToday());
    // Cắt nhật ký màn hình tại mốc an toàn: khối này chỉ để xem lại, không cần
    // nguyên văn — giữ bản mới nhất (cuối chuỗi) vì đó là hoạt động gần nhất.
    QString trimmedLog = logText;
    if (trimmedLog.size() > kMaxDashboardLogChars) {
        const int cut = trimmedLog.size() - kMaxDashboardLogChars;
        const int nl = trimmedLog.indexOf(QLatin1Char('\n'), cut);
        trimmedLog = trimmedLog.mid(nl >= 0 ? nl + 1 : cut);
    }
    root.insert(QStringLiteral("log"), trimmedLog);
    root.insert(QStringLiteral("saved_at"),
                QDateTime::currentDateTime().toString(Qt::ISODate));

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

QJsonArray accountsToJson(const QVector<FacebookAccount> &accounts)
{
    QJsonArray arr;
    for (const FacebookAccount &a : accounts) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), a.id);
        o.insert(QStringLiteral("name"), a.name);
        o.insert(QStringLiteral("cookie"), a.cookieRaw);
        o.insert(QStringLiteral("proxy"), a.proxy);
        o.insert(QStringLiteral("status"), a.status);
        o.insert(QStringLiteral("failCount"), a.failCount);
        o.insert(QStringLiteral("selected"), a.selected);
        arr.append(o);
    }
    return arr;
}

bool accountsFromJsonFile(const QString &path, QVector<FacebookAccount> *out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError)
        return false;

    QJsonArray arr;
    if (doc.isArray()) {
        arr = doc.array();
    } else if (doc.isObject()) {
        arr = doc.object().value(QStringLiteral("accounts")).toArray();
    } else {
        return false;
    }

    out->clear();
    out->reserve(arr.size());
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        FacebookAccount a;
        a.id = o.value(QStringLiteral("id")).toString();
        if (a.id.isEmpty())
            a.id = o.value(QStringLiteral("c_user")).toString();
        a.name = o.value(QStringLiteral("name")).toString();
        a.cookieRaw = o.value(QStringLiteral("cookie")).toString();
        if (a.cookieRaw.isEmpty())
            a.cookieRaw = o.value(QStringLiteral("cookie_raw")).toString();
        if (a.cookieRaw.isEmpty())
            a.cookieRaw = o.value(QStringLiteral("raw")).toString();
        if (a.cookieRaw.isEmpty())
            continue;
        a.proxy = o.value(QStringLiteral("proxy")).toString();
        a.status = o.value(QStringLiteral("status")).toString();
        if (a.status.isEmpty())
            a.status = QStringLiteral("hoạt động");
        a.failCount = o.value(QStringLiteral("failCount")).toInt();
        a.selected = o.value(QStringLiteral("selected")).toBool(true);
        out->append(a);
    }
    return true;
}

bool loadAll(const QString &path, QVector<FacebookGroup> *groups,
             QVector<FacebookAccount> *accounts)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    const QJsonObject root = doc.object();

    if (groups) {
        groups->clear();
        const QJsonArray arr = root.value(QStringLiteral("groups")).toArray();
        for (const QJsonValue &v : arr) {
            const QJsonObject o = v.toObject();
            FacebookGroup g;
            g.id = o.value(QStringLiteral("id")).toString();
            g.name = o.value(QStringLiteral("name")).toString();
            g.url = o.value(QStringLiteral("url")).toString();
            g.privacy = o.value(QStringLiteral("privacy")).toString();
            g.memberCount = qint64(o.value(QStringLiteral("members")).toDouble());
            g.isMember = o.value(QStringLiteral("isMember")).toBool();
            g.pending = o.value(QStringLiteral("pending")).toBool();
            g.selected = o.value(QStringLiteral("selected")).toBool(true);
            groups->append(g);
        }
    }

    if (accounts) {
        accounts->clear();
        const QJsonArray arr = root.value(QStringLiteral("accounts")).toArray();
        for (const QJsonValue &v : arr) {
            const QJsonObject o = v.toObject();
            FacebookAccount a;
            a.id = o.value(QStringLiteral("id")).toString();
            a.name = o.value(QStringLiteral("name")).toString();
            a.cookieRaw = o.value(QStringLiteral("cookie")).toString();
            a.proxy = o.value(QStringLiteral("proxy")).toString();
            a.status = o.value(QStringLiteral("status")).toString();
            a.failCount = o.value(QStringLiteral("failCount")).toInt();
            a.selected = o.value(QStringLiteral("selected")).toBool(true);
            accounts->append(a);
        }
    }
    return true;
}

QJsonArray rentedToJson(const QVector<RentedAccount> &rented)
{
    QJsonArray arr;
    for (const RentedAccount &a : rented) {
        QJsonObject o;
        o.insert(QStringLiteral("id"), a.id);
        o.insert(QStringLiteral("name"), a.name);
        o.insert(QStringLiteral("cookie"), a.cookieRaw);
        o.insert(QStringLiteral("proxy"), a.proxy);
        o.insert(QStringLiteral("post_text"), a.postText);
        o.insert(QStringLiteral("images"), QJsonArray::fromStringList(a.images));
        o.insert(QStringLiteral("total_posts"), a.totalPosts);
        o.insert(QStringLiteral("price"), a.price);
        o.insert(QStringLiteral("used"), a.used);
        o.insert(QStringLiteral("status"), a.status);
        o.insert(QStringLiteral("selected"), a.selected);
        arr.append(o);
    }
    return arr;
}

bool rentedFromJsonFile(const QString &path, QVector<RentedAccount> *out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError)
        return false;

    QJsonArray arr;
    if (doc.isArray()) {
        arr = doc.array();
    } else if (doc.isObject()) {
        arr = doc.object().value(QStringLiteral("rented")).toArray();
    } else {
        return false;
    }

    out->clear();
    out->reserve(arr.size());
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        RentedAccount a;
        a.id = o.value(QStringLiteral("id")).toString();
        a.name = o.value(QStringLiteral("name")).toString();
        a.cookieRaw = o.value(QStringLiteral("cookie")).toString();
        if (a.cookieRaw.isEmpty())
            continue;
        a.proxy = o.value(QStringLiteral("proxy")).toString();
        a.postText = o.value(QStringLiteral("post_text")).toString();
        const QJsonArray imgs = o.value(QStringLiteral("images")).toArray();
        for (const QJsonValue &iv : imgs)
            a.images.append(iv.toString());
        a.totalPosts = qMax(1, o.value(QStringLiteral("total_posts")).toInt(1000));
        a.price = qMax(0, o.value(QStringLiteral("price")).toInt(100000));
        a.used = qMax(0, o.value(QStringLiteral("used")).toInt());
        a.status = o.value(QStringLiteral("status")).toString();
        if (a.status.isEmpty())
            a.status = QStringLiteral("hoạt động");
        a.selected = o.value(QStringLiteral("selected")).toBool(true);
        out->append(a);
    }
    return true;
}

bool saveRented(const QString &path, const QVector<RentedAccount> &rented)
{
    QJsonObject root;
    root.insert(QStringLiteral("rented"), rentedToJson(rented));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

bool loadRented(const QString &path, QVector<RentedAccount> *out)
{
    return rentedFromJsonFile(path, out);
}

} // namespace DashboardStore