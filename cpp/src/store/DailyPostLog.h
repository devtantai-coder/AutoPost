#pragma once

#include <QString>

namespace DailyPostLog
{
// Đếm số bài ĐĂNG THÀNH CÔNG của một tài khoản trong ngày hôm nay.
// Dùng để giới hạn số bài/ngày THEO TỪNG TÀI KHOẢN (tách riêng theo khách thuê).
int countToday(const QString &accountId);

// Ghi nhật ký từng bài đăng riêng theo tên tài khoản:
//   - data/nhat_ky_bai_YYYY-MM-DD.log        (tổng hợp theo ngày)
//   - data/nhat_ky_<tên tài khoản>_YYYY-MM-DD.log  (tách riêng từng tài khoản/khách thuê)
// Số liệu cũng lưu vào data/posts.json để đếm chính xác hạn mức.
void logPost(const QString &accountId, const QString &accountName, const QString &groupName,
             bool ok);

// Ghi ngay mọi thay đổi đang chờ ra posts.json (gọi khi kết thúc phiên/đóng app).
void flush();
} // namespace DailyPostLog
