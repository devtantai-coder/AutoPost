#include "utils/TemplateEngine.h"

#include <QHash>

namespace TemplateEngine
{

QString expand(const QString &tmpl, const QString &groupName, const QString &accountName,
               const QDateTime &now)
{
    if (tmpl.isEmpty())
        return tmpl;

    const QString ngay = now.toString(QStringLiteral("dd/MM/yyyy"));
    const QString gio = now.toString(QStringLiteral("HH:mm"));
    const QHash<QString, QString> vars = {
        {QStringLiteral("{{ten}}"), accountName},
        {QStringLiteral("{{group}}"), groupName},
        {QStringLiteral("{{nhom}}"), groupName},
        {QStringLiteral("{{ngay}}"), ngay},
        {QStringLiteral("{{gio}}"), gio},
        {QStringLiteral("{{ngay-gio}}"), ngay + QLatin1Char(' ') + gio},
        {QStringLiteral("{{thoi-gian}}"), ngay + QLatin1Char(' ') + gio},
        {QStringLiteral("{{thang}}"), now.toString(QStringLiteral("MM/yyyy"))},
        {QStringLiteral("{{nam}}"), now.toString(QStringLiteral("yyyy"))},
    };

    QString out = tmpl;
    for (auto it = vars.cbegin(); it != vars.cend(); ++it)
        out.replace(it.key(), it.value());
    return out;
}

} // namespace TemplateEngine
