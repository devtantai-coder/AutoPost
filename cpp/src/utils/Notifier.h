#pragma once

#include <QString>

class Config;

// Gửi thông báo hoàn thành / dừng sang kênh ngoài (Telegram hoặc Discord webhook)
// dựa trên cấu hình notify của người dùng. Chỉ gửi khi notifyDone bật và đã cấu
// hình kênh tương ứng. Gửi bất đồng bộ (fire-and-forget) trên event loop của thread
// gọi — không chặn UI.
namespace Notifier
{
// Gửi thông báo. title + message được ghép thành nội dung. Không throw.
void send(const Config &cfg, const QString &title, const QString &message);
} // namespace Notifier
