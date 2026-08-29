#include "store/DataStore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonParseError>

namespace DataStore
{

QString dir()
{
    static const QString d = [] {
        // Cho phép trỏ dữ liệu đi nơi khác (dùng cho test cách ly dữ liệu thật).
        const QByteArray overrideDir = qgetenv("AUTOPOST_DATA_DIR");
        if (!overrideDir.isEmpty()) {
            const QString p = QString::fromLocal8Bit(overrideDir);
            QDir().mkpath(p);
            return p;
        }
        // Chạy từ thư mục build (vd: cpp/build) -> dùng cpp/src/data.
        const QString appDir = QCoreApplication::applicationDirPath();
        const QString srcData = QDir(appDir).filePath(QStringLiteral("../src/data"));
        if (QDir().mkpath(srcData))
            return QDir(srcData).absolutePath();
        // Dự phòng: thư mục data ngay cạnh chương trình.
        return QDir(appDir).filePath(QStringLiteral("data"));
    }();
    return d;
}

QString filePath(const QString &fileName)
{
    return QDir(dir()).filePath(fileName);
}

QMutex &mutex()
{
    static QMutex m;
    return m;
}

bool writeJson(const QString &fileName, const QJsonDocument &doc)
{
    if (!QDir().mkpath(dir()))
        return false;

    const QString path = filePath(fileName);
    const QString tmpPath = path + QStringLiteral(".tmp");
    {
        QFile f(tmpPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        f.write(doc.toJson(QJsonDocument::Indented));
        f.flush();
    }
    QFile::remove(path);
    if (!QFile::rename(tmpPath, path)) {
        QFile::remove(tmpPath);
        return false;
    }
    return true;
}

bool writeJson(const QString &fileName, const QJsonValue &value)
{
    if (value.isObject())
        return writeJson(fileName, QJsonDocument(value.toObject()));
    if (value.isArray())
        return writeJson(fileName, QJsonDocument(value.toArray()));
    return false;
}

QJsonDocument readJson(const QString &fileName)
{
    QFile f(filePath(fileName));
    if (!f.open(QIODevice::ReadOnly))
        return {};

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError)
        return {};
    return doc;
}

QJsonObject readObject(const QString &fileName)
{
    return readJson(fileName).object();
}

QJsonArray readArray(const QString &fileName)
{
    return readJson(fileName).array();
}

std::string readText(const QString &fileName)
{
    QFile f(filePath(fileName));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray raw = f.readAll();
    return {raw.begin(), raw.end()};
}

bool writeText(const QString &fileName, const std::string &text)
{
    if (!QDir().mkpath(dir()))
        return false;

    const QString path = filePath(fileName);
    const QString tmpPath = path + QStringLiteral(".tmp");
    {
        QFile f(tmpPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return false;
        f.write(QByteArray::fromStdString(text));
        f.flush();
    }
    QFile::remove(path);
    if (!QFile::rename(tmpPath, path)) {
        QFile::remove(tmpPath);
        return false;
    }
    return true;
}

} // namespace DataStore