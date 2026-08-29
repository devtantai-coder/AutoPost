#pragma once

#include <QObject>
#include <QRecursiveMutex>
#include <QString>

class QFile;

// Hệ thống log chuyên nghiệp: nhiều mức (DEBUG/INFO/WARN/ERROR), ghi đồng thời
// ra file log (xoay vòng theo ngày + giới hạn kích thước, giữ 1 bản backup) và
// phát tín hiệu messageLogged để UI hiển thị trực tiếp.
class Logger : public QObject
{
    Q_OBJECT
public:
    enum class Level { Debug, Info, Warn, Error };

    static Logger &instance();

    // Tương thích ngược: log mức INFO.
    void log(const QString &message);
    void log(Level level, const QString &message);
    void debug(const QString &message);
    void info(const QString &message);
    void warn(const QString &message);
    void error(const QString &message);

    // Đường dẫn thư mục log (tự tạo nếu chưa có).
    QString logDir() const;

    static QString levelName(Level level);

signals:
    void messageLogged(const QString &line);

private:
    explicit Logger(QObject *parent = nullptr);
    ~Logger() override;

    QString currentLogFilePath();
    void rotateIfNeeded(const QString &path);

    // Khóa đệ quy: Qt message handler có thể gọi lại Logger trong lúc đang ghi
    // log (cùng thread) — khóa thường không đệ quy sẽ deadlock ngay tại đó.
    QRecursiveMutex m_mutex;
    QString m_dayStamp;
    QString m_filePath;
    QFile *m_file = nullptr;
    qint64 m_fileBytes = 0;
    // Ghi đệm: chỉ flush xuống đĩa tối đa 4 lần/giây (trừ ERROR flush ngay) để
    // không tốn syscall/fsync mỗi dòng khi log dồn dập lúc đăng bài.
    qint64 m_lastFlushMs = 0;
};
