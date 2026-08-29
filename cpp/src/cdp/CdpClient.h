#pragma once

#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QSet>
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
    // Chờ sự kiện và chốt params của sự kiện đó (tránh evaluate thêm 1 lần
    // sau khi chờ xong — ví dụ chờ Page.domContentEventFired rồi lấy URL).
    bool waitForEventParams(const QString &method, int timeoutMs, QJsonObject *params);
    // Đăng ký method để sự kiện của nó LUÔN được parse + buffer kể cả khi chưa
    // có ai chờ (đóng cửa sổ drop: sự kiện nổ trong lúc sendCommand khác đang
    // chạy sẽ vẫn vào buffer 1s cho lần waitForEvent kế tiếp).
    void watchEvent(const QString &method) { m_bufferedEvents.insert(method); }
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
    // method -> params của sự kiện gần nhất (cho waitForEventParams lấy lại).
    QHash<QString, QJsonObject> m_lastEventParams;
    // Các method được theo dõi vĩnh viễn trong kết nối (từng được waitForEvent
    // chờ): sự kiện của chúng luôn được parse + buffer kể cả khi không ai chờ —
    // chống mất sự kiện nổ giữa các lệnh (chỉ vài method, chi phí không đáng kể).
    QSet<QString> m_bufferedEvents;
};
