#pragma once

#include <QString>
#include <QVector>

#include "model/FacebookGroup.h"

namespace GroupStore
{
bool importFromCsv(const QString &path, QVector<FacebookGroup> &out);
bool exportToCsv(const QString &path, const QVector<FacebookGroup> &groups);
bool saveToJson(const QString &path, const QVector<FacebookGroup> &groups);
bool loadFromJson(const QString &path, QVector<FacebookGroup> &out);

// Lưu / đọc toàn bộ nhóm từ file JSON trong thư mục data.
bool saveAll(const QVector<FacebookGroup> &groups);
bool loadAll(QVector<FacebookGroup> &out);
} // namespace GroupStore
