#pragma once

#include <QString>

namespace PostedStore
{
// Ghi nhớ các nhóm đã đăng thành công "hôm nay" (lưu file posted_today.json)
// để các lần chạy sau không đăng trùng. Khóa theo ngày; các ngày cũ tự bị bỏ.
bool isPostedToday(const QString &groupId);
void markPosted(const QString &groupId);
// Ghi ngay các thay đổi đợi ra đĩa (gọi khi đóng app).
void flush();
} // namespace PostedStore
