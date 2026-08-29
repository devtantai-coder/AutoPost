#include "cdp/CdpClient.h"

#include <QDateTime>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QTimer>
#include <QWebSocket>

CdpClient::CdpClient(QObject *parent)
    : QObject(parent)
    , m_socket(new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this))
{
    connect(m_socket, &QWebSocket::textMessageReceived,
            this, &CdpClient::onTextMessageReceived);
}

CdpClient::~CdpClient()
{
    disconnectSocket();
}

bool CdpClient::connectToUrl(const QString &wsUrl, int timeoutMs)
{
    if (m_socket->state() == QAbstractSocket::ConnectedState)
        return true;

    QEventLoop loop;
    bool ok = false;
    connect(m_socket, &QWebSocket::connected, &loop, [&]() { ok = true; loop.quit(); });
    connect(m_socket, &QWebSocket::errorOccurred, &loop, &QEventLoop::quit);
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);

    m_socket->open(QUrl(wsUrl));
    loop.exec();
    return ok;
}

void CdpClient::disconnectSocket()
{
    m_socket->close();
}

bool CdpClient::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

QJsonObject CdpClient::sendCommand(const QString &method, const QJsonObject &params,
                                    int timeoutMs, bool waitForResponse)
{
    if (!isConnected())
        return QJsonObject();

    const int id = ++m_nextId;
    QJsonObject msg;
    msg.insert(QStringLiteral("id"), id);
    msg.insert(QStringLiteral("method"), method);
    if (!params.isEmpty())
        msg.insert(QStringLiteral("params"), params);

    const QByteArray data = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    // Không chờ phản hồi: dùng cho các lệnh thiết lập (enable domain, override
    // Emulation...) mà ta không cần kết quả. CDP xử lý theo thứ tự trên cùng một
    // kết nối nên lệnh gửi trước luôn được thực thi trước lệnh sau -> an toàn,
    // đồng thời tiết kiệm hàng loạt round-trip chờ đợi mỗi tab.
    if (!waitForResponse) {
        m_socket->sendTextMessage(QString::fromUtf8(data));
        return QJsonObject();
    }

    QEventLoop loop;
    m_waiters.insert(id, &loop);
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    m_socket->sendTextMessage(QString::fromUtf8(data));
    loop.exec();
    m_waiters.remove(id);
    return m_responses.take(id);
}

bool CdpClient::waitForEvent(const QString &method, int timeoutMs)
{
    if (!isConnected())
        return false;

    // Sự kiện vừa xảy ra gần đây (<=1s) thì coi như đã có — tránh miss do race
    // giữa lúc sự kiện được gửi và lúc ta bắt đầu chờ.
    const auto it = m_lastEvents.constFind(method);
    if (it != m_lastEvents.constEnd() &&
        QDateTime::currentMSecsSinceEpoch() - it.value() <= 1000)
        return true;

    QEventLoop loop;
    bool found = false;
    QMetaObject::Connection conn =
        connect(this, &CdpClient::eventReceived, this,
                [&](const QString &m, const QJsonObject &) {
                    if (m == method) {
                        found = true;
                        loop.quit();
                    }
                });
    if (timeoutMs > 0)
        QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    else
        QTimer::singleShot(0, &loop, &QEventLoop::quit);
    loop.exec();
    disconnect(conn);
    return found;
}

void CdpClient::onTextMessageReceived(const QString &message)
{
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    const QJsonObject obj = doc.object();

    if (obj.contains(QStringLiteral("id"))) {
        const int id = obj.value(QStringLiteral("id")).toInt();
        // Chỉ giữ phản hồi còn ai đang chờ. Phản hồi muộn (sau timeout) hoặc lạ
        // bị bỏ ngay — trước đây tích vào m_responses mãi không ai lấy, RAM tăng
        // dần khi chạy lâu / nhiều lần timeout.
        if (m_waiters.contains(id)) {
            m_responses.insert(id, obj);
            m_waiters.value(id)->quit();
        }
    } else {
        const QString method = obj.value(QStringLiteral("method")).toString();
        if (!method.isEmpty())
            m_lastEvents.insert(method, QDateTime::currentMSecsSinceEpoch());
        emit eventReceived(method, obj.value(QStringLiteral("params")).toObject());
    }
}
