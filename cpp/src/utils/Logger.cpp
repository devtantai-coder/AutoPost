#include "utils/Logger.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include <mutex>

#include <fmt/format.h>

namespace
{
// Dung lượng tối đa 1 file log (5 MB). Đầy thì xoay sang bản backup .1.
constexpr qint64 kMaxLogBytes = 5 * 1024 * 1024;
} // namespace

Logger &Logger::instance()
{
    static Logger logger;
    return logger;
}

Logger::Logger(QObject *parent)
    : QObject(parent)
{
}

Logger::~Logger()
{
    QMutexLocker lock(&m_mutex);
    if (m_file) {
        m_file->flush();
        delete m_file;
        m_file = nullptr;
    }
}

QString Logger::levelName(Logger::Level level)
{
    using enum Level;
    switch (level) {
    case Debug:
        return QStringLiteral("DEBUG");
    case Info:
        return QStringLiteral("INFO");
    case Warn:
        return QStringLiteral("WARN");
    case Error:
        return QStringLiteral("ERROR");
    }
    return QStringLiteral("INFO");
}

QString Logger::logDir() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
                        QStringLiteral("/logs");
    QDir().mkpath(dir);
    return dir;
}

QString Logger::currentLogFilePath()
{
    const QString day = QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
    if (m_dayStamp != day) {
        m_dayStamp = day;
        m_filePath = logDir() + QStringLiteral("/autopost_%1.log").arg(day);
    }
    return m_filePath;
}

void Logger::rotateIfNeeded(const QString &path)
{
    if (m_fileBytes < kMaxLogBytes)
        return;
    m_file->flush();
    delete m_file;
    m_file = nullptr;
    m_fileBytes = 0;
    // Giữ 1 bản backup .1 của file đã đầy, xóa bản cũ hơn nếu có.
    QFile::remove(path + QStringLiteral(".1"));
    QFile::rename(path, path + QStringLiteral(".1"));
}

void Logger::log(Level level, const QString &message)
{
    const QString line = QString::fromStdString(fmt::format(
        "[{}] [{}] {}",
        QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")).toStdString(),
        levelName(level).toStdString(),
        message.toStdString()));
    {
        std::lock_guard<QRecursiveMutex> lock(m_mutex);
        const QString path = currentLogFilePath();
        if (!m_file || m_file->fileName() != path) {
            // Ngày mới / file chưa mở: mở lại file log của hôm nay.
            if (m_file) {
                m_file->flush();
                delete m_file;
                m_file = nullptr;
            }
            m_file = new QFile(path);
            if (m_file->open(QIODevice::Append | QIODevice::Text))
                m_fileBytes = m_file->size();
        }
        if (m_file && m_file->isOpen()) {
            m_file->write(line.toUtf8());
            m_file->write("\n");
            m_fileBytes += line.size() + 1;
            // Trước đây flush MỖI dòng -> hàng trăm syscall/giây khi đăng bài dồn
            // dập (mỗi dòng fsync đệm kernel). Nay: flush giãn cách 250ms, chỉ lỗi
            // (ERROR) flush ngay để bằng chứng sự cố không mất. Khi đóng app,
            // destructor flush phần còn lại.
            const bool needNow = level == Level::Error ||
                                 (QDateTime::currentMSecsSinceEpoch() - m_lastFlushMs) >= 250;
            if (needNow) {
                m_file->flush();
                m_lastFlushMs = QDateTime::currentMSecsSinceEpoch();
            }
            rotateIfNeeded(path);
        }
    }
    // Phát tín hiệu ngoài khóa mutex để slot (UI) có thể gọi log lại không bị deadlock.
    emit messageLogged(line);
}

void Logger::log(const QString &message)
{
    log(Level::Info, message);
}

void Logger::debug(const QString &message)
{
    log(Level::Debug, message);
}

void Logger::info(const QString &message)
{
    log(Level::Info, message);
}

void Logger::warn(const QString &message)
{
    log(Level::Warn, message);
}

void Logger::error(const QString &message)
{
    log(Level::Error, message);
}
