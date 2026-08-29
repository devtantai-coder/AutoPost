#include "fb/NurtureEngine.h"

#include "cdp/WebDriver.h"
#include "utils/Logger.h"
#include "utils/Utils.h"

#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>

NurtureEngine::NurtureEngine(WebDriver *driver)
    : m_driver(driver)
{
}

bool NurtureEngine::isCheckpoint() const
{
    const QString url = m_driver->currentUrl().toLower();
    return url.contains(QStringLiteral("checkpoint")) ||
           url.contains(QStringLiteral("recover")) ||
           url.contains(QStringLiteral("login_identifier"));
}

bool NurtureEngine::likeRandomPost()
{
    // Nút like chưa được bấm trên feed: aria-label "Thích" (vi) / "Like" (en).
    // Nút đã like có nhãn "Đã thích"/"Liked" — không chọn nhầm.
    const QString likeXpath =
        QStringLiteral("//div[@role='button' and "
                       "(@aria-label='Thích' or @aria-label='Like')]");
    const int n = m_driver->visibleCount(likeXpath);
    if (n <= 0)
        return false;

    // Chọn ngẫu nhiên một bài trong số các nút like đang hiển thị.
    const int pick = Utils::randomInt(0, n - 1);
    Utils::humanPause(600, 1800);
    if (!m_driver->realClick(likeXpath, pick)) {
        m_driver->clickNth(likeXpath, pick);
        return false;
    }
    Logger::instance().log(QStringLiteral("Đã like 1 bài viết trên feed."));
    return true;
}

bool NurtureEngine::watchRandomVideo()
{
    // Video trong feed: phần tử <video> đang hiển thị. Click mở player, xem
    // vài giây rồi thoát (Escape) — hành vi giống người xem video ngắn.
    const QString videoXpath = QStringLiteral("//video");
    const int n = m_driver->visibleCount(videoXpath);
    if (n <= 0)
        return false;

    const int pick = Utils::randomInt(0, n - 1);
    Utils::humanPause(500, 1500);
    if (!m_driver->realClick(videoXpath, pick))
        return false;
    // Xem một lúc rồi thoát.
    Utils::sleepMs(Utils::randomInt(4000, 9000));
    m_driver->pressEscape();
    Utils::humanPause(800, 2000);
    Logger::instance().log(QStringLiteral("Đã xem 1 video ngắn."));
    return true;
}

bool NurtureEngine::nurture(const Settings &s, std::atomic<bool> *stop)
{
    Logger::instance().log(
        QStringLiteral("Bắt đầu phiên nuôi acc: tối đa %1 like, %2 video, %3 giây.")
            .arg(s.maxLikes)
            .arg(s.maxVideos)
            .arg(s.maxDurationSec));

    // Về trang chủ (news feed).
    if (!m_driver->navigate(QStringLiteral("https://www.facebook.com/"), 30000)) {
        Logger::instance().log(QStringLiteral("Không tải được news feed."));
        return false;
    }
    if (!m_driver->waitForReady(15000)) {
        Logger::instance().log(QStringLiteral("Trang feed chưa sẵn sàng."));
        return false;
    }
    Utils::humanPause(1500, 4000);

    int liked = 0;
    int watched = 0;
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < s.maxDurationSec * 1000 && !(stop && stop->load())) {
        if (isCheckpoint()) {
            Logger::instance().log(
                QStringLiteral("⚠ Gặp checkpoint/chặn khi nuôi — dừng phiên này."));
            return false;
        }

        // Lướt feed: cuộn xuống theo nhịp ngẫu nhiên, đôi khi cuộn lên lại.
        m_driver->randomScroll();
        Utils::humanPause(1500, 4500);

        // Đôi khi dừng "đọc" bài lâu hơn.
        if (Utils::randomInt(0, 99) < s.readPauseChance)
            Utils::humanPause(3000, 8000);

        // Like ngẫu nhiên (xác suất ~35% mỗi vòng, không vượt maxLikes).
        if (liked < s.maxLikes && Utils::randomInt(0, 99) < 35) {
            if (likeRandomPost())
                ++liked;
        }

        // Mở video ngắn (xác suất ~20% mỗi vòng, không vượt maxVideos).
        if (watched < s.maxVideos && Utils::randomInt(0, 99) < 20) {
            if (watchRandomVideo())
                ++watched;
        }

        // Nếu đã đủ like và xem video thì dừng sớm (tiết kiệm thời gian).
        if (liked >= s.maxLikes && watched >= s.maxVideos)
            break;
    }

    Logger::instance().log(
        QStringLiteral("Phiên nuôi hoàn tất: %1 like, %2 video, %3 giây.")
            .arg(liked)
            .arg(watched)
            .arg(timer.elapsed() / 1000));
    return true;
}
