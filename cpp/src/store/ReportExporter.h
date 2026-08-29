#pragma once

#include <QDateTime>
#include <QHash>
#include <QPair>
#include <QString>
#include <QVector>

#include "model/FacebookGroup.h"

namespace ReportExporter
{
// Ghi báo cáo kết quả từng nhóm ra CSV (kèm BOM để Excel mở tiếng Việt đúng).
// status: chỉ số nhóm (trong groups) -> 1 = thành công, 0 = thất bại, -1 = hoãn
//         (tài khoản đã đủ hạn mức bài/ngày, để hôm sau đăng tiếp).
// times:  chỉ số nhóm -> thời điểm báo kết quả đầu tiên.
QString writeCsv(const QString &path, const QVector<FacebookGroup> &groups,
                 const QHash<int, int> &status, const QHash<int, QDateTime> &times);

// Ghi báo cáo SỐ LƯỢNG BÀI ĐÃ RẢI THEO TỪNG TÀI KHOẢN ra CSV.
// stats: id tài khoản -> (số bài thành công, số bài thất bại).
// names: id tài khoản -> tên hiển thị (để đọc dễ hơn).
QString writeAccountCsv(const QString &path,
                        const QHash<QString, QPair<int, int>> &stats,
                        const QHash<QString, QString> &names);
} // namespace ReportExporter
