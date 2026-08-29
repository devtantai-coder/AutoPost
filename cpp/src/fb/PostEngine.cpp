#include "fb/PostEngine.h"

#include "cdp/WebDriver.h"
#include "utils/Logger.h"
#include "utils/Utils.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

PostEngine::PostEngine(WebDriver *driver)
    : m_driver(driver)
{
}

QString PostEngine::countCondition(const QString &xpath)
{
    return QStringLiteral("window.__apxpath(%1).length>0").arg(WebDriver::jsStr(xpath));
}

QString PostEngine::visibleCondition(const QString &xpath)
{
    return QStringLiteral("window.__apxpath(%1).filter(function(n){return n.offsetParent!==null&&!n.disabled;}).length>0")
        .arg(WebDriver::jsStr(xpath));
}

PostResult PostEngine::postToGroupWithRetry(const QString &groupId, const QString &content,
                                            const QStringList &images, int maxRetries,
                                            std::atomic<bool> *stop)
{
    for (int attempt = 1; attempt <= maxRetries; ++attempt) {
        if (stop && stop->load())
            return PostResult::Failed;
        Logger::instance().log(
            QStringLiteral("Lần thử %1/%2").arg(attempt).arg(maxRetries));
        const PostResult r = postToGroup(groupId, content, images);
        if (r == PostResult::Ok || r == PostResult::Banned)
            return r;
        // Tạm dừng giữa các lần thử theo phân phối exponential — nhân dần theo số
        // lần thử để không dồn dập (bài thất bại là tín hiệu cần đi chậm lại,
        // nhịp cố định ngắn là hành vi máy dễ bị chặn thêm).
        Utils::exponentialPause(3000 * attempt, 8000 * attempt);
    }
    return PostResult::Failed;
}

PostResult PostEngine::postToGroup(const QString &groupId, const QString &content,
                                   const QStringList &images)
{
    const QString groupUrl = QStringLiteral("https://www.facebook.com/groups/") + groupId;
    if (!m_driver->navigate(groupUrl, 15000))
        return isCheckpoint() ? PostResult::Banned : PostResult::Failed;
    if (isCheckpoint())
        return PostResult::Banned;

    // Hành vi giống người: cuộn ngẫu nhiên, tạm dừng trước khi thao tác.
    Utils::humanPause(800, 2500);
    m_driver->randomScroll();
    Utils::humanPause(500, 1500);

    // Mở khung soạn bài.
    const QString composerXpath =
        QStringLiteral("(//div[@role='button' and @tabindex='0']["
                       ".//span[contains(normalize-space(.),'Bạn') or "
                       "contains(normalize-space(.),'Write') or "
                       "contains(normalize-space(.),'thinking')] or "
                       ".//div[@role='textbox']])[1]");
    if (!m_driver->waitForCondition(countCondition(composerXpath), 30000))
        return isCheckpoint() ? PostResult::Banned : PostResult::Failed;
    // Click bằng chuột thật (di chuyển Bezier + Input.dispatchMouseEvent); nếu
    // không lấy được tọa độ thì quay về click JS như cũ.
    if (!m_driver->realClick(composerXpath, 0))
        m_driver->clickNth(composerXpath, 0);

    // Nhập nội dung — dán bằng Ctrl+V như người dùng (giữ đúng số dòng).
    const QString textboxXpath =
        QStringLiteral("//div[@role='dialog']//div[@role='textbox' and @contenteditable='true']");
    if (!m_driver->waitForCondition(countCondition(textboxXpath), 30000))
        return isCheckpoint() ? PostResult::Banned : PostResult::Failed;
    Utils::humanPause(400, 1200);
    // Ưu tiên dán Ctrl+V (giữ đúng số dòng); nếu clipboard không khả dụng thì
    // quay về gõ từng ký tự như người.
    bool textOk = m_driver->pasteText(textboxXpath, content);
    if (!textOk)
        textOk = m_driver->typeText(textboxXpath, content);
    if (!textOk)
        return isCheckpoint() ? PostResult::Banned : PostResult::Failed;

    // Đính kèm ảnh (tối đa 5 ảnh).
    if (!images.isEmpty()) {
        const QString photoXpath =
            QStringLiteral("//div[@role='dialog']//div[@aria-label='Photo/Video' or "
                           "@aria-label='Ảnh/video' or @aria-label='Add Photo/Video']");
        if (m_driver->waitForCondition(visibleCondition(photoXpath), 10000)) {
            Utils::humanPause(500, 1500);
            m_driver->clickNth(photoXpath, 0);

            if (m_driver->waitForCondition(
                    QStringLiteral("document.querySelector('div[role=\"dialog\"] input[type=\"file\"]')!==null"),
                    10000)) {
                Utils::humanPause(600, 2000);
                m_driver->uploadFiles(QStringLiteral("div[role='dialog'] input[type='file']"), images);

                const QString previewXpath =
                    QStringLiteral("//div[@role='dialog']//img[contains(@src,'scontent') or contains(@src,'fbcdn')]");
                m_driver->waitForCondition(countCondition(previewXpath), 30000);
            }
        }
    }

    // Tạm dừng như người đọc lại bài trước khi đăng.
    Utils::humanPause(1000, 3000);
    m_driver->randomScroll();

    // Bấm nút đăng, chờ đóng dialog.
    bool posted = false;
    bool rateLimitedOnce = false;
    for (int attempt = 0; attempt < 3 && !posted; ++attempt) {
        if (clickSubmitButton() &&
            m_driver->waitForCondition(
                QStringLiteral("window.__apxpath(\"//div[@role='dialog']\").length===0"), 10000)) {
            posted = true;
        } else if (isRateLimited()) {
            // Facebook báo chặn tốc độ ngay trong dialog — dừng retry tại đây.
            rateLimitedOnce = true;
            break;
        }
    }
    if (!posted) {
        if (rateLimitedOnce || isRateLimited())
            return PostResult::Banned; // chặn tốc độ = tính Banned để xoay tài khoản
        return isCheckpoint() ? PostResult::Banned : PostResult::Failed;
    }

    // Không gọi isCheckpoint() thêm lần nữa: hàm này đọc tới 12KB body text
    // (đắt); nếu tài khoản bị chặn, lần navigate đầu của nhóm SAU sẽ phát hiện
    // qua URL/title và trả Banned ở đó. Tiết kiệm 1 round-trip lớn mỗi bài.
    return PostResult::Ok;
}

bool PostEngine::clickSubmitButton()
{
    // Trước đây mỗi selector thăm dò 1 visibleCount (4 round-trip CDP) trước khi
    // bấm. Nay gộp: CHỈ 1 evaluate thử cả 4 xpath một lượt, trả index selector đầu
    // tiên có nút hiển thị — rồi bấm đúng selector đó.
    static const QStringList selectors = {
        QStringLiteral("//div[@role='dialog']//div[@role='button']//span[text()='Đăng' or text()='Post']"),
        QStringLiteral("//div[@role='dialog']//div[@aria-label='Post' or @aria-label='Đăng']"),
        QStringLiteral("//div[@role='dialog']//div[contains(@aria-label,'Đăng bài')]"),
        QStringLiteral("//div[@role='dialog']//div[@role='button' and .//span[text()='Đăng' or text()='Post']]"),
    };
    QString js = QStringLiteral("(function(){");
    for (int i = 0; i < selectors.size(); ++i) {
        js += QStringLiteral(
                  "if(window.__apxpath(%1).some(function(n){return n.offsetParent!==null&&!n.disabled;}))return %2;")
                  .arg(WebDriver::jsStr(selectors.at(i)))
                  .arg(i);
    }
    js += QStringLiteral("return -1;})()");
    const int pick = m_driver->evaluate(js).toInt(-1);
    if (pick < 0 || pick >= selectors.size())
        return false;
    if (!m_driver->realClick(selectors.at(pick), 0))
        m_driver->clickNth(selectors.at(pick), 0);
    return true;
}

bool PostEngine::isRateLimited()
{
    // Đọc văn bản đang hiển thị trong dialog (dialog chặn tốc độ KHÔNG có URL
    // riêng — isCheckpoint() qua URL không thấy). Chỉ quét text của dialog nên rẻ.
    // Combine tiếng Việt + tiếng Anh — Facebook dùng 2 cụm khác nhau tùy ngôn ngữ:
    //   vi: "Bạn đang đăng quá nhanh" / "Vui lòng thử lại sau" / "tạm thời bị chặn"
    //   en: "too fast" / "try again later" / "temporarily blocked"
    const QJsonValue v = m_driver->evaluate(QStringLiteral(
        "(function(){"
        "var ns=document.querySelectorAll('div[role=dialog]');"
        "for(var i=0;i<ns.length;i++){"
        "var t=(ns[i].innerText||'').toLowerCase();"
        "if(!t)continue;"
        "if(t.indexOf('qu\u00e1 nhanh')>=0 ||"
        "   t.indexOf('th\u1eed l\u1ea1i sau')>=0 ||"
        "   t.indexOf('t\u1ea1m th\u1eddi')>=0 ||"
        "   t.indexOf('posting too fast')>=0 ||"
        "   t.indexOf('changing too quickly')>=0 ||"
        "   t.indexOf('go too fast')>=0 ||"
        "   t.indexOf('temporarily blocked')>=0 ||"
        "   t.indexOf('try again later')>=0 ||"
        "   t.indexOf('unusual activity')>=0 ||"
        "   t.indexOf('too many requests')>=0)"
        "return true;"
        "}"
        "return false;})()"));
    return v.isBool() && v.toBool();
}

bool PostEngine::isCheckpoint()
{
    const QString url = m_driver->currentUrl().toLower();

    // 1) Tín hiệu URL mạnh nhất: trạng thái checkpoint/bảo mật của tài khoản.
    if (url.contains(QStringLiteral("checkpoint")) ||
        url.contains(QStringLiteral("security_check")) ||
        url.contains(QStringLiteral("/recover/")) ||
        url.contains(QStringLiteral("/blocked/")) ||
        url.contains(QStringLiteral("account_quality")) ||
        url.contains(QStringLiteral("help/105981904992768")))
        return true;

    // 2) Trên trang nhóm KHÔNG đọc body.innerText (12KB) — body check phía dưới
    //    bị bỏ qua trên trang nhóm, nên chỉ lấy tiêu đề + heading: nhẹ hơn nhiều
    //    (không gây reflow toàn trang Facebook, nơi body có thể rất lớn).
    const bool onGroupPage = url.contains(QStringLiteral("/groups/"));

    // 3) Lấy tiêu đề, các tiêu đề đoạn và văn bản hiển thị (không quét mã nguồn
    //    để tránh nhầm lẫn với chuỗi nhúng trong script/JSON của Facebook).
    const QJsonValue v = m_driver->evaluate(
        onGroupPage ? QStringLiteral(
                          "(function(){"
                          "var t=(document.title||'').toLowerCase();"
                          "var h='';"
                          "var ns=document.querySelectorAll('h1,h2,h3,[role=heading],strong');"
                          "for(var i=0;i<ns.length&&i<8;i++){var tx=(ns[i].innerText||'').toLowerCase();if(tx)h+=tx+' ';}"
                          "return JSON.stringify({t:t,h:h,b:''});})()")
                    : QStringLiteral(
                          "(function(){"
                          "var t=(document.title||'').toLowerCase();"
                          "var h='';"
                          "var ns=document.querySelectorAll('h1,h2,h3,[role=heading],strong');"
                          "for(var i=0;i<ns.length&&i<8;i++){var tx=(ns[i].innerText||'').toLowerCase();if(tx)h+=tx+' ';}"
                          "var b=(document.body&&document.body.innerText)?document.body.innerText.toLowerCase():'';"
                          "return JSON.stringify({t:t,h:h,b:b.substring(0,12000)});"
                          "})()"),
        5000);
    if (!v.isString())
        return false;

    const QJsonObject o = QJsonDocument::fromJson(v.toString().toUtf8()).object();
    const QString title = o.value(QStringLiteral("t")).toString();
    const QString headings = o.value(QStringLiteral("h")).toString();
    const QString body = o.value(QStringLiteral("b")).toString();

    // 3) Cụm từ khẳng định trong tiêu đề trang hoặc tiêu đề đoạn (h1/h2/heading):
    //    chắc chắn thuộc về trang chặn, không phải nội dung nhóm.
    static const QStringList strong = {
        QStringLiteral("security check"),       QStringLiteral("verify your identity"),
        QStringLiteral("xác minh danh tính"),   QStringLiteral("confirm your identity"),
        QStringLiteral("your account has been locked"), QStringLiteral("tài khoản của bạn đã bị khóa"),
        QStringLiteral("account has been locked"), QStringLiteral("help us secure your account"),
        QStringLiteral("giúp chúng tôi bảo mật"), QStringLiteral("account quality"),
        QStringLiteral("tài khoản của bạn tạm thời bị hạn chế"), QStringLiteral("temporarily blocked"),
        // Trang giải CAPTCHA — Facebook chặn thao tác tự động: coi như checkpoint.
        QStringLiteral("nhập các ký tự"),       QStringLiteral("enter the characters you see"),
        QStringLiteral("check your security"),   QStringLiteral("captcha"),
    };
    for (const QString &s : strong) {
        if (title.contains(s) || headings.contains(s))
            return true;
    }

    // 4) Văn bản hiển thị trên thân trang. Chỉ tin tưởng khi KHÔNG còn ở trang
    //    nhóm, vì trên trang nhóm cụm từ như "bị chặn" có thể mô tả nhóm chứ
    //    không phải tài khoản (tránh đánh dấu nhầm).
    if (onGroupPage)
        return false;

    static const QStringList bodySigns = {
        QStringLiteral("unusual activity"),     QStringLiteral("suspicious login"),
        QStringLiteral("we detected unusual"),  QStringLiteral("bị chặn"),
        QStringLiteral("tạm thời bị hạn chế"),  QStringLiteral("blocked"),
        QStringLiteral("verify your identity"), QStringLiteral("xác minh danh tính"),
        QStringLiteral("security check"),
    };
    for (const QString &s : bodySigns) {
        if (body.contains(s))
            return true;
    }
    return false;
}
