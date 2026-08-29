#include "utils/TemplateEngine.h"

#include <QString>
#include <QDateTime>

namespace TemplateEngine
{

namespace
{
// Thay an toàn cho chuỗi template: bỏ hoàn toàn QHash tạm (trước đây mỗi bài
// đăng dựng bảng băm 9 phần tử rồi quét TOÀN VĂN template 9 lần). Chuỗi
// template thường chỉ chứa 0-2 biến — các phép replace trực tiếp chỉ quét
// khi thật sự gặp "{{" trong văn bản.
QString safeReplace(const QString &tmpl, const QString &key, const QString &value)
{
    return tmpl.contains(key) ? QString(tmpl).replace(key, value) : tmpl;
}
} // namespace

QString expand(const QString &tmpl, const QString &groupName, const QString &accountName,
               const QDateTime &now)
{
    if (tmpl.isEmpty())
        return tmpl;

    // Fast-path: không có dấu hiệu "{{" thì không biến nào để thay — trả nguyên
    // bản, không copy chuỗi (bài đăng thường không chứa biến).
    if (!tmpl.contains(QLatin1String("{{")))
        return tmpl;

    const QString ngay = now.toString(QStringLiteral("dd/MM/yyyy"));
    const QString gio = now.toString(QStringLiteral("HH:mm"));

    QString out = tmpl;
    out = safeReplace(out, QStringLiteral("{{ngay-gio}}"), ngay + QLatin1Char(' ') + gio);
    out = safeReplace(out, QStringLiteral("{{thoi-gian}}"), ngay + QLatin1Char(' ') + gio);
    out = safeReplace(out, QStringLiteral("{{group}}"), groupName);
    out = safeReplace(out, QStringLiteral("{{nhom}}"), groupName);
    out = safeReplace(out, QStringLiteral("{{ten}}"), accountName);
    out = safeReplace(out, QStringLiteral("{{ngay}}"), ngay);
    out = safeReplace(out, QStringLiteral("{{gio}}"), gio);
    out = safeReplace(out, QStringLiteral("{{thang}}"), now.toString(QStringLiteral("MM/yyyy")));
    out = safeReplace(out, QStringLiteral("{{nam}}"), now.toString(QStringLiteral("yyyy")));
    return out;
}

} // namespace TemplateEngine
