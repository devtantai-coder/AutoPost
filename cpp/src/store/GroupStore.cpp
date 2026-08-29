#include "store/GroupStore.h"

#include "store/DataStore.h"
#include "utils/Utils.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QStringList>
#include <QTextStream>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace GroupStore
{

bool importFromCsv(const QString &path, QVector<FacebookGroup> &out)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;

        const QStringList parts = line.split(QLatin1Char(','));
        if (parts.size() < 2)
            continue;

        FacebookGroup g;
        g.id = parts.at(0).trimmed();
        g.name = parts.at(1).trimmed();
        g.url = QStringLiteral("https://www.facebook.com/groups/") + g.id;
        if (parts.size() >= 3)
            g.privacy = Utils::normalizePrivacy(parts.at(2).trimmed());
        if (parts.size() >= 4) {
            bool ok = false;
            const qint64 v = parts.at(3).trimmed().remove(QLatin1Char(',')).toLongLong(&ok);
            if (ok)
                g.memberCount = v;
        }
        out.append(g);
    }
    return true;
}

bool exportToCsv(const QString &path, const QVector<FacebookGroup> &groups)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);
    out << QStringLiteral("ID,Tên,Quyền riêng tư,Thành viên,Trạng thái,URL\n");
    for (const FacebookGroup &g : groups) {
        if (!g.selected)
            continue;
        QString status = QStringLiteral("Không phải thành viên");
        if (g.isMember)
            status = QStringLiteral("Thành viên");
        else if (g.pending)
            status = QStringLiteral("Chờ");
        QString name = g.name;
        name.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        out << g.id << ",\"" << name << "\"," << g.privacy << ',' << g.memberCount
            << ',' << status << ',' << g.url << '\n';
    }
    return true;
}

namespace
{
QString jstr(const json &o, const char *key, const QString &def = {})
{
    if (auto it = o.find(key); it != o.end() && it->is_string())
        return QString::fromStdString(it->get<std::string>());
    return def;
}

qint64 jnum(const json &o, const char *key, qint64 def = 0)
{
    if (auto it = o.find(key); it != o.end() && it->is_number())
        return qint64(it->get<double>());
    return def;
}

bool jbool(const json &o, const char *key, bool def = false)
{
    if (auto it = o.find(key); it != o.end() && it->is_boolean())
        return it->get<bool>();
    return def;
}

// Ghi text an toàn vào đường dẫn bất kỳ (file tạm + đổi tên).
bool writeTextPath(const QString &path, const std::string &text)
{
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
} // namespace

bool saveToJson(const QString &path, const QVector<FacebookGroup> &groups)
{
    json arr = json::array();
    for (const FacebookGroup &g : groups) {
        json o = json::object();
        o["id"] = g.id.toStdString();
        o["name"] = g.name.toStdString();
        o["url"] = g.url.toStdString();
        o["privacy"] = g.privacy.toStdString();
        o["member_count"] = qint64(g.memberCount);
        o["selected"] = g.selected;
        o["is_member"] = g.isMember;
        o["pending"] = g.pending;
        arr.push_back(o);
    }
    return writeTextPath(path, arr.dump(4));
}

bool loadFromJson(const QString &path, QVector<FacebookGroup> &out)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    const QByteArray raw = file.readAll();
    json doc = json::parse(raw.begin(), raw.end(), nullptr, false);
    if (doc.is_discarded() || (!doc.is_array() && !doc.is_object()))
        return false;

    // Chấp nhận: mảng nhóm, mảng ID, hoặc object có khóa "groups" (kiểu dashboard.json).
    json arr = doc.is_array() ? doc : doc.value("groups", json::array());

    out.clear();
    out.reserve(int(arr.size()));
    for (const auto &v : arr) {
        FacebookGroup g;
        if (v.is_object()) {
            const json &o = v;
            g.id = jstr(o, "id");
            if (g.id.isEmpty())
                g.id = jstr(o, "group_id");
            if (g.id.isEmpty())
                continue;
            g.name = jstr(o, "name");
            g.url = jstr(o, "url");
            if (g.url.isEmpty())
                g.url = QStringLiteral("https://www.facebook.com/groups/") + g.id;
            g.privacy = Utils::normalizePrivacy(jstr(o, "privacy", "chưa rõ"));
            g.memberCount = qint64(jnum(o, "member_count", 0));
            g.selected = jbool(o, "selected", true);
            g.isMember = jbool(o, "is_member", false);
            g.pending = jbool(o, "pending", false);
        } else if (v.is_string()) {
            // Chỉ là ID trần.
            g.id = QString::fromStdString(v.get<std::string>());
            if (g.id.isEmpty())
                continue;
            g.name = QStringLiteral("Nhóm ") + g.id;
            g.url = QStringLiteral("https://www.facebook.com/groups/") + g.id;
            g.privacy = QStringLiteral("chưa rõ");
            g.selected = true;
        } else if (v.is_number()) {
            g.id = QString::number(v.get<qint64>());
            if (g.id.isEmpty())
                continue;
            g.name = QStringLiteral("Nhóm ") + g.id;
            g.url = QStringLiteral("https://www.facebook.com/groups/") + g.id;
            g.privacy = QStringLiteral("chưa rõ");
            g.selected = true;
        } else {
            continue;
        }
        out.append(g);
    }
    return true;
}

bool saveAll(const QVector<FacebookGroup> &groups)
{
    QMutexLocker lock(&DataStore::mutex());
    return saveToJson(DataStore::filePath(QStringLiteral("groups.json")), groups);
}

bool loadAll(QVector<FacebookGroup> &out)
{
    {
        QMutexLocker lock(&DataStore::mutex());
        if (loadFromJson(DataStore::filePath(QStringLiteral("groups.json")), out))
            return true;
    }

    // Nâng cấp từ bản cũ: groups.json ở thư mục chạy chương trình.
    if (loadFromJson(QStringLiteral("groups.json"), out)) {
        saveAll(out);
        return true;
    }
    return false;
}

} // namespace GroupStore
