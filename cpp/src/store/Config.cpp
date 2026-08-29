#include "store/Config.h"

#include "store/DataStore.h"
#include "utils/Logger.h"
#include "utils/Utils.h"

#include <QDir>
#include <QFile>
#include <QMutexLocker>
#include <QUuid>
#include <QStringList>
#include <QTime>

#include <fstream>
#include <string>

#include <fmt/format.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace
{
QString getString(const json &j, const char *key, const QString &def)
{
    if (auto it = j.find(key); it != j.end() && it->is_string())
        return QString::fromStdString(it->get<std::string>());
    return def;
}

int getInt(const json &j, const char *key, int def)
{
    if (auto it = j.find(key); it != j.end() && it->is_number())
        return it->get<int>();
    return def;
}

bool getBool(const json &j, const char *key, bool def)
{
    if (auto it = j.find(key); it != j.end() && it->is_boolean())
        return it->get<bool>();
    return def;
}

// Ghi JSON an toàn: ghi file tạm rồi đổi tên, tránh hỏng dữ liệu khi mất điện
// hoặc thoát đột ngột.
bool writeJsonFile(const QString &path, const json &j)
{
    const QString tmpPath = path + QStringLiteral(".tmp");
    {
        QFile f(tmpPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        const std::string text = j.dump(4, ' ', false, nlohmann::json::error_handler_t::replace);
        f.write(QByteArray::fromStdString(text));
        f.flush();
    }
    QFile::remove(path);
    if (!QFile::rename(tmpPath, path)) {
        QFile::remove(tmpPath);
        return false;
    }
    return true;
}
} // namespace

QString Config::configFilePath() const
{
    return DataStore::filePath(QStringLiteral("config.json"));
}

QString Config::cookieFilePath() const
{
    return DataStore::filePath(QStringLiteral("facebook_cookies.txt"));
}

void Config::sanitize()
{
    // Kẹp các giá trị số về khoảng hợp lệ (chống file config hỏng làm app chạy sai).
    const auto clampWarn = [this](int &v, int lo, int hi, const char *name) {
        if (v < lo || v > hi) {
            Logger::instance().warn(QString::fromStdString(fmt::format(
                "Cấu hình {} = {} nằm ngoài khoảng [{}, {}], tự chỉnh về {}",
                name, v, lo, hi, qBound(lo, v, hi))));
            v = qBound(lo, v, hi);
        }
    };
    clampWarn(threadCount, 1, 5, "thread_count");
    clampWarn(tabCount, 1, 10, "tab_count");
    clampWarn(retryCount, 0, 20, "retry_count");
    clampWarn(delaySec, 1, 3600, "delay_sec");
    clampWarn(joinDelaySec, 1, 3600, "join_delay_sec");
    clampWarn(maxGroups, 1, 100000, "max_groups");
    clampWarn(rotateFailThreshold, 1, 50, "rotate_fail_threshold");
    clampWarn(dailyPostLimit, 0, 1000000, "daily_post_limit");

    // Chỉ giữ mốc giờ đúng định dạng HH:mm.
    QStringList valid;
    for (const QString &t : scheduleTimes) {
        if (QTime::fromString(t.trimmed(), QStringLiteral("HH:mm")).isValid())
            valid.append(t.trimmed());
    }
    if (valid.size() != scheduleTimes.size()) {
        Logger::instance().warn(QString::fromStdString(fmt::format(
            "Có {} mốc giờ đăng không hợp lệ (đúng định dạng HH:mm), đã bỏ qua",
            scheduleTimes.size() - valid.size())));
        scheduleTimes = valid;
    }
    if (scheduleTimes.isEmpty()) {
        if (QTime::fromString(scheduleTime.trimmed(), QStringLiteral("HH:mm")).isValid())
            scheduleTimes.append(scheduleTime.trimmed());
        else
            scheduleTimes.append(QStringLiteral("08:00"));
    }
}

void Config::load()
{
    loadFromJson();

    // Nâng cấp từ bản cũ (config.json + facebook_cookies.txt ở thư mục chạy chương trình)
    // nếu chưa có dữ liệu nào trong data/config.json.
    const bool hasData = !accounts.isEmpty() || !cookieRaw.isEmpty();
    if (!hasData) {
        const QString legacyPath = QDir::current().filePath(QStringLiteral("config.json"));
        if (QFile::exists(legacyPath)) {
            loadFromJsonFile(legacyPath);

            const QString legacyCookie =
                QDir::current().filePath(QStringLiteral("facebook_cookies.txt"));
            QFile fc(legacyCookie);
            if (fc.open(QIODevice::ReadOnly))
                cookieRaw = QString::fromUtf8(fc.readAll()).trimmed();

            if (accounts.isEmpty() && !cookieRaw.isEmpty()) {
                FacebookAccount a;
                a.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                a.name = QStringLiteral("Tài khoản 1");
                a.cookieRaw = cookieRaw;
                a.selected = true;
                accounts.append(a);
            }
            if (!accounts.isEmpty() || !cookieRaw.isEmpty())
                save();
        }
    }

    ensureActiveCookie();
    sanitize();
}

void Config::save() const
{
    saveToJson();
}

void Config::loadFromJson()
{
    loadFromJsonFile(configFilePath());
    loadCookieFromFile();
    ensureActiveCookie();
}

void Config::loadFromJsonFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;

    const QByteArray raw = file.readAll();
    json obj;
    try {
        obj = json::parse(raw.constData(), raw.constData() + raw.size(), nullptr, true);
    } catch (const json::parse_error &) {
        Logger::instance().warn(QStringLiteral("config.json bị hỏng, bỏ qua và dùng mặc định"));
        return;
    }
    if (!obj.is_object())
        return;

    delaySec = getInt(obj, "delay_sec", delaySec);
    delayType = getString(obj, "delay_type", delayType);
    retryCount = getInt(obj, "retry_count", retryCount);
    threadCount = getInt(obj, "thread_count", threadCount);
    tabCount = getInt(obj, "tab_count", tabCount);
    joinDelaySec = getInt(obj, "join_delay_sec", joinDelaySec);
    maxGroups = getInt(obj, "max_groups", maxGroups);
    joinAction = getString(obj, "join_action", joinAction);
    randomDelay = getBool(obj, "random_delay", randomDelay);
    headless = getBool(obj, "headless", headless);
    saveSession = getBool(obj, "save_session", saveSession);
    autoJoin = getBool(obj, "auto_join", autoJoin);
    skipPrivate = getBool(obj, "skip_private", skipPrivate);
    skipPending = getBool(obj, "skip_pending", skipPending);
    rotateAccounts = getBool(obj, "rotate_accounts", rotateAccounts);
    rotateFailThreshold = getInt(obj, "rotate_fail_threshold", rotateFailThreshold);
    skipPostedToday = getBool(obj, "skip_posted_today", skipPostedToday);
    scheduleEnabled = getBool(obj, "schedule_enabled", scheduleEnabled);
    scheduleTime = getString(obj, "schedule_time", scheduleTime);

    scheduleTimes.clear();
    if (auto it = obj.find("schedule_times"); it != obj.end() && it->is_array()) {
        for (const auto &v : *it) {
            if (v.is_string()) {
                const QString t = QString::fromStdString(v.get<std::string>()).trimmed();
                if (!t.isEmpty())
                    scheduleTimes.append(t);
            }
        }
    }
    if (scheduleTimes.isEmpty()) {
        // Tương thích ngược: file cũ chỉ có schedule_time.
        if (!scheduleTime.trimmed().isEmpty())
            scheduleTimes.append(scheduleTime.trimmed());
    }

    proxyPool.clear();
    if (auto it = obj.find("proxy_pool"); it != obj.end() && it->is_array()) {
        for (const auto &v : *it) {
            if (v.is_string()) {
                const QString p = QString::fromStdString(v.get<std::string>()).trimmed();
                if (!p.isEmpty())
                    proxyPool.append(p);
            }
        }
    }

    rotateProxyPerPost = getBool(obj, "rotate_proxy_per_post", rotateProxyPerPost);
    notifyDone = getBool(obj, "notify_done", notifyDone);
    notifyMethod = getInt(obj, "notify_method", notifyMethod);
    telegramToken = getString(obj, "telegram_token", telegramToken);
    telegramChatId = getString(obj, "telegram_chat_id", telegramChatId);
    discordWebhook = getString(obj, "discord_webhook", discordWebhook);
    jitterSec = getInt(obj, "jitter_sec", jitterSec);
    autoBackup = getBool(obj, "auto_backup", autoBackup);
    dailyPostLimit = getInt(obj, "daily_post_limit", dailyPostLimit);

    accounts.clear();
    if (auto it = obj.find("accounts"); it != obj.end() && it->is_array()) {
        for (const auto &o : *it) {
            if (!o.is_object())
                continue;
            FacebookAccount a;
            a.id = getString(o, "id", {});
            a.name = getString(o, "name", {});
            a.cookieRaw = getString(o, "cookie", {});
            a.proxy = getString(o, "proxy", {});
            a.status = getString(o, "status", a.status);
            a.failCount = getInt(o, "fail_count", a.failCount);
            a.selected = getBool(o, "selected", true);
            // Chỉ bỏ account hoàn toàn rỗng; giữ lại account mới nhập tên/proxy
            // (cookie có thể điền sau) để không mất dữ liệu người dùng.
            if (a.name.trimmed().isEmpty() && a.proxy.trimmed().isEmpty() &&
                a.cookieRaw.trimmed().isEmpty())
                continue;
            if (a.id.isEmpty())
                a.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
            if (a.name.isEmpty())
                a.name = QStringLiteral("Tài khoản ") + QString::number(accounts.size() + 1);
            accounts.append(a);
        }
    }
}

void Config::saveToJson() const
{
    json obj = json::object();
    obj["delay_sec"] = delaySec;
    obj["delay_type"] = delayType.toStdString();
    obj["retry_count"] = retryCount;
    obj["thread_count"] = threadCount;
    obj["tab_count"] = tabCount;
    obj["join_delay_sec"] = joinDelaySec;
    obj["max_groups"] = maxGroups;
    obj["join_action"] = joinAction.toStdString();
    obj["random_delay"] = randomDelay;
    obj["headless"] = headless;
    obj["save_session"] = saveSession;
    obj["auto_join"] = autoJoin;
    obj["skip_private"] = skipPrivate;
    obj["skip_pending"] = skipPending;
    obj["rotate_accounts"] = rotateAccounts;
    obj["rotate_fail_threshold"] = rotateFailThreshold;
    obj["skip_posted_today"] = skipPostedToday;
    obj["schedule_enabled"] = scheduleEnabled;
    obj["schedule_time"] = scheduleTime.toStdString();

    json timesArr = json::array();
    for (const QString &t : scheduleTimes)
        if (!t.trimmed().isEmpty())
            timesArr.push_back(t.trimmed().toStdString());
    if (timesArr.empty() && !scheduleTime.trimmed().isEmpty())
        timesArr.push_back(scheduleTime.trimmed().toStdString());
    obj["schedule_times"] = timesArr;

    json poolArr = json::array();
    for (const QString &p : proxyPool)
        if (!p.trimmed().isEmpty())
            poolArr.push_back(p.trimmed().toStdString());
    obj["proxy_pool"] = poolArr;

    obj["rotate_proxy_per_post"] = rotateProxyPerPost;
    obj["notify_done"] = notifyDone;
    obj["notify_method"] = notifyMethod;
    obj["telegram_token"] = telegramToken.toStdString();
    obj["telegram_chat_id"] = telegramChatId.toStdString();
    obj["discord_webhook"] = discordWebhook.toStdString();
    obj["jitter_sec"] = jitterSec;
    obj["auto_backup"] = autoBackup;
    obj["daily_post_limit"] = dailyPostLimit;

    json accArr = json::array();
    for (const FacebookAccount &a : accounts) {
        if (a.name.trimmed().isEmpty() && a.proxy.trimmed().isEmpty() &&
            a.cookieRaw.trimmed().isEmpty())
            continue;
        json o = json::object();
        o["id"] = a.id.toStdString();
        o["name"] = a.name.toStdString();
        o["cookie"] = a.cookieRaw.toStdString();
        o["proxy"] = a.proxy.toStdString();
        o["status"] = a.status.toStdString();
        o["fail_count"] = a.failCount;
        o["selected"] = a.selected;
        accArr.push_back(o);
    }
    obj["accounts"] = accArr;

    {
        QMutexLocker lock(&DataStore::mutex());
        writeJsonFile(DataStore::filePath(QStringLiteral("config.json")), obj);
    }

    if (saveSession)
        saveCookieToFile();
}

void Config::loadCookieFromFile()
{
    QFile file(cookieFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    cookieRaw = QString::fromUtf8(file.readAll()).trimmed();
}

void Config::saveCookieToFile() const
{
    QFile file(cookieFilePath());
    if (file.open(QIODevice::WriteOnly))
        file.write(cookieRaw.toUtf8());
}

void Config::ensureActiveCookie()
{
    if (cookieRaw.isEmpty()) {
        for (const FacebookAccount &a : accounts) {
            if (a.selected) {
                cookieRaw = a.cookieRaw;
                break;
            }
        }
        if (cookieRaw.isEmpty() && !accounts.isEmpty())
            cookieRaw = accounts.first().cookieRaw;
    }
}

int Config::effectiveDelaySec() const
{
    int delay = delaySec;
    if (delayType == QStringLiteral("Phút"))
        delay *= 60;
    if (randomDelay)
        delay += Utils::randomInt(0, 10);
    return qMax(5, delay);
}
