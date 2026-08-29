#pragma once

#include <QDateTime>
#include <QString>

namespace TemplateEngine
{
// Thay các biến trong nội dung bài: {{ten}}, {{group}}/{{nhom}}, {{ngay}},
// {{gio}}, {{ngay-gio}}, {{thang}}, {{nam}}.
// Giữ nguyên văn bản nếu không khớp biến nào.
QString expand(const QString &tmpl, const QString &groupName, const QString &accountName,
               const QDateTime &now);
} // namespace TemplateEngine
