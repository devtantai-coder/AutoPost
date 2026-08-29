#pragma once

#include <QString>
#include <QVector>

#include <atomic>

#include "model/FacebookGroup.h"

class WebDriver;

// Tương đương searchGroups/joinGroups/tryJoinGroup trong Java.
class JoinEngine
{
public:
    struct Settings
    {
        int maxGroups = 50;
        int joinDelaySec = 10;
        QString joinAction;
        bool skipPrivate = true;
        bool skipPending = true;
    };

    explicit JoinEngine(WebDriver *driver);

    QVector<FacebookGroup> searchGroups(const QString &keywords);
    int joinGroups(const QVector<FacebookGroup> &groups, const Settings &s,
                   std::atomic<bool> *stop);

private:
    bool isAlreadyMember();
    bool isPendingApproval();
    bool isCheckpoint();
    bool tryJoinGroup(const QString &joinAction);
    // Quét một lần các tín hiệu "đã thành viên"/"chờ duyệt" trên trang — cache theo
    // URL để cả hai hàm kiểm tra chỉ tốn 1 round-trip CDP cho mỗi lần navigate.
    QString pageSignals();
    static QString visibleCondition(const QString &xpath);

    WebDriver *m_driver;
    QString m_signalsCache;
    bool m_signalsLoaded = false;
};
