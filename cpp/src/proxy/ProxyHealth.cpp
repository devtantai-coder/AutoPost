#include "proxy/ProxyHealth.h"

#include "proxy/Proxy.h"

#include <QThreadPool>

ProxyHealth::ProxyHealth(QObject *parent)
    : QObject(parent)
{
}

void ProxyHealth::setAlive(const QString &proxy, bool alive)
{
    const QString p = proxy.trimmed();
    if (p.isEmpty())
        return;
    QMutexLocker lock(&m_mutex);
    m_known.insert(p);
    m_alive[p] = alive;
}

bool ProxyHealth::isAlive(const QString &proxy) const
{
    const QString p = proxy.trimmed();
    if (p.isEmpty())
        return false;
    QMutexLocker lock(&m_mutex);
    auto it = m_alive.find(p);
    return it == m_alive.end() ? true : it.value();
}

QString ProxyHealth::nextAlive(const QStringList &pool, int cursor) const
{
    if (pool.isEmpty())
        return QString();
    const int n = pool.size();
    const int start = ((cursor % n) + n) % n;
    QMutexLocker lock(&m_mutex);
    for (int k = 0; k < n; ++k) {
        const QString p = pool.at((start + k) % n).trimmed();
        if (p.isEmpty())
            continue;
        auto it = m_alive.find(p);
        if (it == m_alive.end() || it.value())
            return p;
    }
    return QString();
}

void ProxyHealth::startHeartbeat(int intervalMs)
{
    if (!m_timer) {
        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, &ProxyHealth::onHeartbeat);
    }
    m_timer->start(intervalMs);
}

void ProxyHealth::onHeartbeat()
{
    QSet<QString> known;
    {
        QMutexLocker lock(&m_mutex);
        known = m_known;
    }
    // Kiểm tra song song từng proxy, cập nhật kết quả qua invokeMethod để
    // đảm bảo chạy trên thread của đối tượng (thread-safe với m_mutex).
    for (const QString &p : known) {
        QThreadPool::globalInstance()->start([this, p] {
            const bool ok = testProxyReachability(p, 2000);
            QMetaObject::invokeMethod(this, "setAlive", Qt::QueuedConnection,
                                      Q_ARG(QString, p), Q_ARG(bool, ok));
        });
    }
}
