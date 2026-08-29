#include "fb/JoinEngine.h"

#include "cdp/WebDriver.h"
#include "utils/Logger.h"
#include "utils/Utils.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>

JoinEngine::JoinEngine(WebDriver *driver)
    : m_driver(driver)
{
}

QString JoinEngine::visibleCondition(const QString &xpath)
{
    return QStringLiteral("window.__apxpath(%1).filter(function(n){return n.offsetParent!==null&&!n.disabled;}).length>0")
        .arg(WebDriver::jsStr(xpath));
}

QVector<FacebookGroup> JoinEngine::searchGroups(const QString &keywords)
{
    QVector<FacebookGroup> out;
    const QStringList kws = keywords.split(QLatin1Char(','));
    for (int ki = 0; ki < kws.size(); ++ki) {
        const QString keyword = kws.at(ki).trimmed();
        if (keyword.isEmpty())
            continue;

        if (ki > 0)
            Utils::humanPause(2000, 5000);

        Logger::instance().log(QStringLiteral("Đang tìm kiếm: ") + keyword);
        const QString url = QStringLiteral("https://www.facebook.com/search/groups?q=") +
                            QString::fromUtf8(QUrl::toPercentEncoding(keyword));
        m_driver->navigate(url, 30000);
        m_driver->waitForReady(10000);
        m_driver->scrollToBottom();
        Utils::sleepMs(500);
        m_driver->scrollToBottom();

        const QJsonArray items = m_driver->queryAll(QStringLiteral("//a[contains(@href, '/groups/')]"));
        for (const QJsonValue &v : items) {
            const QJsonObject o = v.toObject();
            const QString href = o.value(QStringLiteral("href")).toString();
            if (!href.contains(QStringLiteral("/groups/")))
                continue;

            FacebookGroup g;
            g.url = href;
            g.id = Utils::extractGroupId(href);
            if (g.id.isEmpty())
                continue;

            g.name = o.value(QStringLiteral("text")).toString();
            if (g.name.isEmpty())
                g.name = o.value(QStringLiteral("auto")).toString();
            if (g.name.isEmpty())
                continue;

            const QString parent = o.value(QStringLiteral("parent")).toString();
            g.privacy = Utils::normalizePrivacy(parent);

            out.append(g);
        }
    }
    return out;
}

int JoinEngine::joinGroups(const QVector<FacebookGroup> &groups, const Settings &s,
                           std::atomic<bool> *stop)
{
    int success = 0;
    int failed = 0;
    const int limit = qMin(int(groups.size()), s.maxGroups);
    Logger::instance().log(QStringLiteral("Bắt đầu tham gia %1 nhóm...").arg(limit));

    int done = 0;
    for (const FacebookGroup &group : groups) {
        if (stop && stop->load())
            break;
        if (done >= limit)
            break;

        Logger::instance().log(QStringLiteral("Đang tham gia nhóm: ") + group.name);
        m_driver->navigate(group.url, 30000);
        // Trang mới -> tín hiệu thành viên/chờ duyệt của trang trước hết hạn.
        m_signalsLoaded = false;
        Utils::humanPause(800, 2000);
        m_driver->randomScroll();
        Utils::humanPause(400, 1200);

        if (isAlreadyMember()) {
            Logger::instance().log(QStringLiteral("Đã là thành viên của: ") + group.name);
            continue;
        }
        if (isPendingApproval()) {
            Logger::instance().log(QStringLiteral("Đang chờ duyệt cho: ") + group.name);
            if (s.skipPending)
                continue;
        }
        if (s.skipPrivate && group.privacy == QStringLiteral("riêng tư")) {
            Logger::instance().log(QStringLiteral("Bỏ qua nhóm riêng tư: ") + group.name);
            continue;
        }

        if (tryJoinGroup(s.joinAction)) {
            Logger::instance().log(QStringLiteral("Đã tham gia thành công: ") + group.name);
            ++success;
            ++done;
        } else {
            Logger::instance().log(QStringLiteral("Tham gia thất bại: ") + group.name);
            ++failed;
        }

        if (done < limit && s.joinDelaySec > 0 && !(stop && stop->load())) {
            const int totalMs = s.joinDelaySec * 1000;
            int waited = 0;
            while (waited < totalMs && !(stop && stop->load())) {
                Utils::sleepMs(200);
                waited += 200;
            }
        }
    }

    Logger::instance().log(QStringLiteral("Quá trình tham gia hoàn tất. Thành công: %1, Thất bại: %2")
                               .arg(success)
                               .arg(failed));
    return success;
}

bool JoinEngine::isAlreadyMember()
{
    // Trước đây đọc TOÀN BỘ outerHTML (1-5MB) + lowercase phía C++, tới 2 lần mỗi
    // nhóm. Nay: 1 round-trip duy nhất (gộp cache với isPendingApproval), chỉ quét
    // văn bản hiển thị của các nút và heading — nơi Facebook hiển thị trạng thái
    // thành viên.
    return pageSignals().contains(QStringLiteral("member"));
}

bool JoinEngine::isPendingApproval()
{
    const QString s = pageSignals();
    return s.contains(QStringLiteral("pending")) || s.contains(QStringLiteral("requested")) ||
           s.contains(QStringLiteral("\u0111ang ch\u1edd"));
}

QString JoinEngine::pageSignals()
{
    // Gộp cả hai trạng thái "đã thành viên" và "chờ duyệt" vào MỘT lần evaluate,
    // cache kết quả cho vòng check hiện tại (joinGroups xóa cờ sau mỗi navigate,
    // nên không tốn thêm round-trip currentUrl() nào để so sánh).
    if (m_signalsLoaded)
        return m_signalsCache;

    const QJsonValue v = m_driver->evaluate(QStringLiteral(
        "(function(){"
        "var out='';"
        "var ns=document.querySelectorAll('[role=button]');"
        "for(var i=0;i<ns.length&&i<40;i++){"
        "var t=((ns[i].getAttribute&&ns[i].getAttribute('aria-label'))||ns[i].innerText||'').toLowerCase();"
        "if(t.indexOf('joined')>=0||t.indexOf('member')>=0||t.indexOf('\u0111\u00e3 tham gia')>=0)out+=' member ';"
        "if(t.indexOf('pending')>=0||t.indexOf('requested')>=0||t.indexOf('\u0111ang ch\u1edd')>=0)out+=' pending ';"
        "if(t.indexOf('request')>=0)out+=' requested ';"
        "}"
        "var hs=document.querySelectorAll('h1,h2,h3,[role=heading]');"
        "for(var j=0;j<hs.length&&j<8;j++){"
        "var ht=(hs[j].innerText||'').toLowerCase();"
        "if(ht.indexOf('joined')>=0||ht.indexOf('\u0111\u00e3 tham gia')>=0)out+=' member ';"
        "if(ht.indexOf('pending')>=0||ht.indexOf('\u0111ang ch\u1edd')>=0)out+=' pending ';"
        "}"
        "return out;})()"));
    m_signalsCache = v.isString() ? v.toString() : QString();
    m_signalsLoaded = true;
    return m_signalsCache;
}

bool JoinEngine::isCheckpoint()
{
    const QString url = m_driver->currentUrl().toLower();
    return url.contains(QStringLiteral("checkpoint")) ||
           url.contains(QStringLiteral("recover"));
}

bool JoinEngine::tryJoinGroup(const QString &joinAction)
{
    const QString btnXpath =
        QStringLiteral("//div[@role='button'][contains(., 'Join') or contains(., 'Tham gia') or "
                       "contains(., 'Request') or contains(., 'Yêu cầu')]");
    const QJsonArray buttons = m_driver->queryAll(btnXpath);
    for (int i = 0; i < buttons.size(); ++i) {
        const QString text = buttons.at(i).toObject()
                                 .value(QStringLiteral("text"))
                                 .toString()
                                 .toLower();
        if (!(text.contains(QStringLiteral("join")) ||
              text.contains(QStringLiteral("tham gia")) ||
              text.contains(QStringLiteral("request")) ||
              text.contains(QStringLiteral("yêu cầu"))))
            continue;

        if (joinAction == QStringLiteral("Chỉ tham gia công khai") && text.contains(QStringLiteral("request")))
            continue;

        Utils::humanPause(300, 900);
        m_driver->clickNth(btnXpath, i);

        const QString confirmXpath =
            QStringLiteral("//div[@role='dialog']//div[@role='button'][contains(., 'Join') or contains(., 'Confirm')]");
        if (m_driver->waitForCondition(visibleCondition(confirmXpath), 3000))
            m_driver->clickNth(confirmXpath, 0);
        return true;
    }
    return false;
}
