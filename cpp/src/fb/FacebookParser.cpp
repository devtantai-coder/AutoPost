#include "fb/FacebookParser.h"

#include "cdp/WebDriver.h"
#include "utils/Logger.h"
#include "utils/Utils.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSet>

namespace FacebookParser
{

static FacebookGroup groupFromAnchor(const QJsonObject &o, bool onlyMember)
{
    const QString href = o.value(QStringLiteral("href")).toString();
    FacebookGroup g;
    g.url = href;
    g.id = Utils::extractGroupId(href);

    g.name = o.value(QStringLiteral("text")).toString();
    if (g.name.isEmpty())
        g.name = o.value(QStringLiteral("aria")).toString();
    if (g.name.isEmpty())
        g.name = o.value(QStringLiteral("auto")).toString();
    if (g.name.contains(QStringLiteral("Public group")) ||
        g.name.contains(QStringLiteral("Private group")))
        g.name = g.name.section(QStringLiteral("·"), 0, 0).trimmed();

    g.isMember = onlyMember;

    const QString raw = o.value(QStringLiteral("text")).toString() + QLatin1Char(' ') +
                        o.value(QStringLiteral("parent")).toString();
    g.privacy = Utils::normalizePrivacy(raw);

    return g;
}

QVector<FacebookGroup> extractFromPage(WebDriver *d, bool onlyMember)
{
    QVector<FacebookGroup> out;
    QSet<QString> seen;

    const QJsonArray items = d->queryAll(QStringLiteral("//a[contains(@href, '/groups/')]"));
    for (const QJsonValue &v : items) {
        const QJsonObject o = v.toObject();
        const QString href = o.value(QStringLiteral("href")).toString();
        if (!href.contains(QStringLiteral("/groups/")))
            continue;

        FacebookGroup g = groupFromAnchor(o, onlyMember);
        if (g.id.isEmpty() || g.name.isEmpty())
            continue;
        if (seen.contains(g.id))
            continue;

        out.append(g);
        seen.insert(g.id);
    }
    return out;
}

QVector<FacebookGroup> extractMyGroups(WebDriver *d)
{
    QVector<FacebookGroup> groups;
    QSet<QString> seen;

    Logger::instance().log(QStringLiteral("Đang truy cập trang nhóm của bạn..."));
    if (!d->navigate(QStringLiteral("https://www.facebook.com/groups/feed/?nav=groups"), 30000))
        return groups;

    for (int i = 0; i < 3; ++i) {
        d->scrollToBottom();
        Utils::sleepMs(800);

        const QVector<FacebookGroup> page = extractFromPage(d, true);
        for (const FacebookGroup &g : page) {
            if (seen.contains(g.id))
                continue;
            groups.append(g);
            seen.insert(g.id);
        }
    }

    Logger::instance().log(QStringLiteral("Tìm thấy %1 nhóm tổng cộng").arg(groups.size()));
    return groups;
}

} // namespace FacebookParser
