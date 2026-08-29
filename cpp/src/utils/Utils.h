#pragma once

#include <QRandomGenerator>
#include <QString>
#include <QVector>

#include <random>

namespace Utils
{
QString formatNumber(qint64 n);
void sleepMs(int ms);
// Thời gian chờ ngẫu nhiên theo phân phối exponential quanh baseMs (nhịp hành vi
// tự nhiên giống người, kẹp trong [30% baseMs, maxMs]).
int exponentialDelayMs(int baseMs, int maxMs);
void exponentialPause(int baseMs, int maxMs);
QString extractGroupId(const QString &href);
QString normalizePrivacy(const QString &raw);
int randomInt(int min, int max);
void humanPause(int minMs, int maxMs);
QString randomUserAgent();
QString platformForUserAgent(const QString &ua);

// Tổng RAM vật lý của máy (MB). Trả về 0 nếu không đọc được.
qint64 physicalMemoryMB();
// Tự động gợi ý số luồng (trình duyệt) và số tab tối ưu dựa trên số luồng CPU
// và dung lượng RAM của máy (nằm trong giới hạn 1..5 trình duyệt, 1..10 tab).
void recommendParallelism(int *outThreads, int *outTabs);

// Các giá trị "vân tay" ngẫu nhiên nhưng thực tế, dùng để làm nhất quán
// fingerprint của từng phiên trình duyệt (chống phát hiện tự động).
int randomHardwareConcurrency();
int randomDeviceMemory();
QString randomWebglVendor();
QString randomWebglRenderer();

// Xáo trộn ngẫu nhiên (Fisher–Yates qua std::ranges) — dùng để đăng nhóm theo
// thứ tự ngẫu nhiên.
template <typename T>
void shuffle(QVector<T> &v)
{
    std::mt19937_64 rng(QRandomGenerator::global()->generate64());
    std::ranges::shuffle(v, rng);
}
} // namespace Utils
