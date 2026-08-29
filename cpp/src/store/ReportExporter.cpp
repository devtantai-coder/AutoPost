#include "store/ReportExporter.h"

#include <QFile>
#include <QTextStream>

namespace ReportExporter
{

namespace
{
QString csvField(const QString &raw)
{
    QString s = raw;
    s.replace(QLatin1Char('"'), QStringLiteral("\"\""));
    return QLatin1Char('"') + s + QLatin1Char('"');
}
} // namespace

QString writeCsv(const QString &path, const QVector<FacebookGroup> &groups,
                 const QHash<int, int> &status, const QHash<int, QDateTime> &times)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return QString();

    QTextStream out(&file);
    out << QChar(0xFEFF); // BOM cho Excel
    out << QStringLiteral("STT,Tên nhóm,ID,URL,Kết quả,Thời gian\n");

    for (int i = 0; i < groups.size(); ++i) {
        const FacebookGroup &g = groups.at(i);
        const int st = status.value(i, 0);
        QString result;
        if (st > 0)
            result = QStringLiteral("Thành công");
        else if (st < 0)
            result = QStringLiteral("Hoãn (hết bài hôm nay)");
        else
            result = QStringLiteral("Thất bại");
        const QString time = times.value(i).toString(QStringLiteral("dd/MM/yyyy HH:mm:ss"));
        out << i + 1 << ',' << csvField(g.name) << ',' << csvField(g.id) << ','
            << csvField(g.url) << ',' << csvField(result) << ',' << csvField(time) << '\n';
    }
    return path;
}

QString writeAccountCsv(const QString &path,
                        const QHash<QString, QPair<int, int>> &stats,
                        const QHash<QString, QString> &names)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return QString();

    QTextStream out(&file);
    out << QChar(0xFEFF); // BOM cho Excel
    out << QStringLiteral("STT,Tài khoản,ID,Đã rải thành công,Thất bại,Tổng\n");

    int row = 0;
    int totalOk = 0;
    int totalFail = 0;
    for (auto it = stats.cbegin(); it != stats.cend(); ++it) {
        ++row;
        const int ok = it.value().first;
        const int fail = it.value().second;
        totalOk += ok;
        totalFail += fail;
        out << row << ',' << csvField(names.value(it.key(), it.key())) << ','
            << csvField(it.key()) << ',' << ok << ',' << fail << ',' << ok + fail << '\n';
    }
    out << QStringLiteral("Tổng,,,,%1,%2,%3\n").arg(totalOk).arg(totalFail).arg(totalOk + totalFail);
    return path;
}

} // namespace ReportExporter
