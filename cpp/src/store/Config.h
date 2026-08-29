#pragma once

#include <QString>
#include <QVector>

#include "model/FacebookAccount.h"

class Config
{
public:
    QString cookieRaw;
    QVector<FacebookAccount> accounts;
    bool rotateAccounts = false;
    int rotateFailThreshold = 2;
    int delaySec = 30;
    QString delayType = QStringLiteral("Giây");
    int retryCount = 2;
    int threadCount = 1;
    int tabCount = 3;
    int joinDelaySec = 10;
    int maxGroups = 50;
    QString joinAction = QStringLiteral("Chỉ tham gia công khai");
    bool randomDelay = false;
    bool headless = false;
    bool saveSession = true;
    bool autoJoin = false;
    bool skipPrivate = true;
    bool skipPending = true;
    bool skipPostedToday = false;
    bool scheduleEnabled = false;
    // Nhiều mốc giờ đăng trong ngày (HH:mm). Lịch cũ 1 giờ vẫn đọc được qua
    // scheduleTime (tương thích ngược).
    QStringList scheduleTimes{QStringLiteral("08:00")};
    QString scheduleTime = QStringLiteral("08:00");
    int dailyPostLimit = 300;
    // Danh sách proxy dùng để xoay vòng mỗi bài (một proxy mỗi dòng).
    QStringList proxyPool;
    bool rotateProxyPerPost = false;
    bool notifyDone = true;
    // Kênh thông báo ngoài: 0 = không gửi, 1 = Telegram, 2 = Discord webhook.
    int notifyMethod = 0;
    QString telegramToken;
    QString telegramChatId;
    QString discordWebhook;
    // Độ lệch ngẫu nhiên (giây) thêm vào nhịp chờ mỗi bài, giúp các tài khoản
    // không đăng cùng một giây (chống bị Facebook phát hiện pattern máy móc).
    int jitterSec = 0;
    bool autoBackup = true;

    void load();
    void save() const;

    // Rà soát và chỉnh lại các giá trị cấu hình về khoảng hợp lệ khi tải
    // (chặn giá trị hỏng gây lỗi khi chạy), cảnh báo vào log khi phải sửa.
    void sanitize();

    void loadCookieFromFile();
    void saveCookieToFile() const;

    int effectiveDelaySec() const;

private:
    QString configFilePath() const;
    QString cookieFilePath() const;
    void loadFromJson();
    void saveToJson() const;
    void loadFromJsonFile(const QString &path);
    void ensureActiveCookie();
};