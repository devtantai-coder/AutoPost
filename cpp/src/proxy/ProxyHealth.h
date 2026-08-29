#pragma once

#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <memory>

// Theo dõi "sức khỏe" của từng proxy theo thời gian thực: kiểm tra định kỳ
// (heartbeat) xem proxy còn kết nối được không, và cung cấp hàm lấy proxy
// "còn sống" tiếp theo khi xoay vòng — giúp tự động bỏ qua / xoay proxy chết
// thay vì để bài đăng thất bại. Thread-safe (dùng chung giữa các luồng đăng).
class ProxyHealth : public QObject
{
    Q_OBJECT
public:
    explicit ProxyHealth(QObject *parent = nullptr);

    // Ghi nhận kết quả kiểm tra của một proxy (true = còn sống).
    void setAlive(const QString &proxy, bool alive);

    // Proxy có đang được coi là còn sống không? Mặc định true (lạc quan) nếu
    // chưa từng kiểm tra — tránh sai sót khi chưa có dữ liệu.
    bool isAlive(const QString &proxy) const;

    // Trả proxy "còn sống" đầu tiên trong pool, bắt đầu quanh vị trí cursor
    // (xoay vòng). Trả chuỗi rỗng nếu không có proxy nào sống.
    QString nextAlive(const QStringList &pool, int cursor) const;

    // Bắt đầu kiểm tra định kỳ mỗi intervalMs (mặc định 5 phút). Chỉ kiểm tra
    // các proxy đã biết (từ setPool / setAlive).
    void startHeartbeat(int intervalMs = 300'000);

private slots:
    void onHeartbeat();

private:
    mutable QMutex m_mutex;
    QSet<QString> m_known;
    QHash<QString, bool> m_alive;
    QTimer *m_timer = nullptr;
};
