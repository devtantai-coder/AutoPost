#pragma once

#include <QString>

#include <atomic>

class WebDriver;

// Nuôi tài khoản (account warming): đăng nhập và thực hiện hành vi giống người
// trên news feed để làm giảm rủi ro bị Facebook chặn acc mới. Mỗi phiên:
//   - Lướt feed tự nhiên: cuộn lên/xuống ngẫu nhiên, dừng xem từng bài.
//   - Like một số bài viết (tần suất + số lượng ngẫu nhiên, có giới hạn).
//   - Mở và xem vài video ngắn trong feed rồi thoát.
// Mọi thao tác đều có độ trễ ngẫu nhiên giống người (Utils::humanPause) và
// click chuột thật (WebDriver::realClick) để không lộ dấu hiệu automation.
class NurtureEngine
{
public:
    struct Settings
    {
        // Số bài tối đa được like trong một phiên.
        int maxLikes = 12;
        // Số video tối đa được mở/xem trong một phiên.
        int maxVideos = 3;
        // Thời lượng tối đa của phiên (giây). Sau đó tự dừng dù chưa đủ like.
        int maxDurationSec = 300;
        // Xác suất dừng lại "đọc" một bài khi lướt qua (0..100).
        int readPauseChance = 40;
    };

    explicit NurtureEngine(WebDriver *driver);

    // Chạy một phiên nuôi: trả true nếu phiên hoàn tất bình thường (đủ like /
    // hết thời gian), false nếu gặp checkpoint/chặn. Dừng sớm khi stop bật.
    bool nurture(const Settings &s, std::atomic<bool> *stop);

private:
    bool likeRandomPost();
    bool watchRandomVideo();
    bool isCheckpoint() const;

    WebDriver *m_driver;
};
