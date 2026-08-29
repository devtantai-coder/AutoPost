#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

class QWebSocket;
class QEventLoop;

class CdpClient : public QObject
{
    Q_OBJECT
public:
    explicit CdpClient(QObject *parent = nullptr);
    ~CdpClient() override;

    bool connectToUrl(const QString &wsUrl, int timeoutMs = 10000);
    void disconnectSocket();
    bool isConnected() const;

    QJsonObject sendCommand(const QString &method, const QJsonObject &params = {},
                            int timeoutMs = 30000, bool waitForResponse = true);

    // Chờ sự kiện CDP không cần polling. timeoutMs<=0 chỉ kiểm tra nhanh xem
    // sự kiện có vừa xảy ra gần đây không (buffer chống miss sự kiện).
    bool waitForEvent(const QString &method, int timeoutMs);
    void clearRecentEvent(const QString &method) { m_lastEvents.remove(method); }

signals:
    void eventReceived(const QString &method, const QJsonObject &params);

private slots:
    void onTextMessageReceived(const QString &message);

private:
    QWebSocket *m_socket;
    int m_nextId = 0;
    QHash<int, QEventLoop *> m_waiters;
    QHash<int, QJsonObject> m_responses;
    // method -> thời điểm sự kiện gần nhất (ms) để nhận diện sự kiện vừa xảy ra.
    QHash<QString, qint64> m_lastEvents;
};
