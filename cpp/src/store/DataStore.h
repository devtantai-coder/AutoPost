#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMutex>
#include <QString>

#include <string>

// Quản lý thư mục dữ liệu JSON (mặc định: thư mục "data" trong src của project).
// Toàn bộ dữ liệu của app (cấu hình, tài khoản, nhóm, nhật ký bài đăng, posted...)
// đều lưu dưới dạng file JSON tại đây — không dùng database.
namespace DataStore
{
// Đường dẫn tuyệt đối tới thư mục data (tự tạo nếu chưa có).
QString dir();

// Đường dẫn tuyệt đối tới một file dữ liệu trong thư mục data.
QString filePath(const QString &fileName);

// Mutex chung cho mọi thao tác đọc/ghi JSON (an toàn đa luồng).
QMutex &mutex();

// Ghi JSON an toàn: ghi file tạm rồi đổi tên, tránh hỏng dữ liệu khi mất điện
// hoặc thoát đột ngột. Trả về true nếu ghi thành công.
bool writeJson(const QString &fileName, const QJsonDocument &doc);
bool writeJson(const QString &fileName, const QJsonValue &value);

// Đọc JSON từ thư mục data. Trả về document rỗng nếu file không tồn tại/lỗi.
QJsonDocument readJson(const QString &fileName);
QJsonObject readObject(const QString &fileName);
QJsonArray readArray(const QString &fileName);

// --- API text chung (dùng cho nlohmann::json và các định dạng khác) ---
// Đọc toàn bộ nội dung file dưới dạng UTF-8. Trả về rỗng nếu không tồn tại/lỗi.
std::string readText(const QString &fileName);

// Ghi text an toàn (ghi file tạm rồi đổi tên). Trả về true nếu thành công.
bool writeText(const QString &fileName, const std::string &text);
} // namespace DataStore
