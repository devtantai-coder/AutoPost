#include "cdp/CdpClient.h"

#include <QDateTime>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QTimer>
#include <QWebSocket>

namespace
{
// Bộ lọc chuỗi thô kiểu "chứa một trong các key": so sánh từng key bằng
// indexOf trên cùng chuỗi message — rẻ hơn nhiều so với parse JSON đầy đủ.
// Dùng cho việc quyết định có đáng parse một message CDP hay không.
bool messageMentionsAnyKey(const QString &message, const QSet<QString> &keys)
{
    for (const QString &k : keys)
        if (message.contains(k, Qt::CaseSensitive))
            return true;
    return false;
}
} // namespace

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
    return waitForEventParams(method, timeoutMs, nullptr);
}

bool CdpClient::waitForEventParams(const QString &method, int timeoutMs, QJsonObject *params)
{
    if (!isConnected())
        return false;

    // Sự kiện vừa xảy ra gần đây (<=1s) thì coi như đã có — tránh miss do race
    // giữa lúc sự kiện được gửi và lúc ta bắt đầu chờ. KHÔNG xóa entry: đây là
    // dấu "sự kiện đã xảy ra", không phải hàng đợi — các lần chờ kế tiếp trong
    // cửa sổ 1s (vd navigate() xong rồi waitForReady() đối chiếu ngay) cũng
    // được hit, đúng ngữ nghĩa buffer của thiết kế gốc.
    const auto it = m_lastEvents.constFind(method);
    if (it != m_lastEvents.constEnd() &&
        QDateTime::currentMSecsSinceEpoch() - it.value() <= 1000) {
        if (params) {
            const auto pit = m_lastEventParams.constFind(method);
            if (pit != m_lastEventParams.constEnd())
                *params = pit.value();
        }
        return true;
    }

    QEventLoop loop;
    bool found = false;
    // Đăng ký method vào danh sách "luôn parse + buffer": duy trì vĩnh viễn trong
    // kết nối (tập hợp thực tế chỉ chứa "Page.domContentEventFired") để sự kiện
    // đến LÚC KHÔNG AI CHỜ (vd trong lúc sendCommand khác đang chờ phản hồi)
    // vẫn được ghi vào buffer 1s — nếu xóa sau mỗi lần chờ, sự kiện nổ trong
    // khoảng trống giữa các lệnh sẽ bị drop ở mức chuỗi thô và waitForReady
    // sau đó phải chờ hết timeout mới fallback (mất ngữ nghĩa chống-miss gốc).
    m_bufferedEvents.insert(method);
    QMetaObject::Connection conn =
        connect(this, &CdpClient::eventReceived, this,
                [&](const QString &m, const QJsonObject &p) {
                    if (m == method) {
                        found = true;
                        if (params)
                            *params = p;
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
    // Bộ lọc nhanh CHUẨN TRƯỚC KHI PARSE: chỉ parse 2 loại message —
    // (a) phản hồi lệnh đang chờ (có "id"), (b) sự kiện thuộc nhóm được theo
    // dõi (m_bufferedEvents — tập các method từng được waitForEvent chờ,
    // thực tế chỉ là Page.domContentEventFired). Mọi event ồn ào khác của
    // Chrome (Network.requestWillBeSent, Runtime.consoleAPICalled,
    // Log.entryAdded, Timeline.*...) bị bỏ ngay ở mức chuỗi thô, không tốn
    // QJsonDocument::fromJson — trước đây mỗi event đều bị parse đầy đủ dù
    // không ai dùng, chiếm CPU lớn khi nhiều tab cùng bật Network domain.
    // Ghi chú: sự kiện KHÔNG nằm trong buffered set bị bỏ cả khi đang chờ
    // QEventLoop — nhưng mọi chỗ chờ đều qua waitForEvent nên không thể sảy ra.
    const bool hasId = message.indexOf(QLatin1String("\"id")) >= 0;
    if (!hasId &&
        (m_bufferedEvents.isEmpty() || !messageMentionsAnyKey(message, m_bufferedEvents)))
        return;

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
        if (!method.isEmpty()) {
            m_lastEvents.insert(method, QDateTime::currentMSecsSinceEpoch());
            // Giữ params của sự kiện gần nhất cho waitForEventParams lấy lại.
            m_lastEventParams.insert(method, obj.value(QStringLiteral("params")).toObject());
            emit eventReceived(method, obj.value(QStringLiteral("params")).toObject());
        }
    }
}
