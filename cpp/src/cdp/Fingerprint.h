#pragma once

#include <QString>
#include <QStringList>

// "Persona" — bộ vân tay trình duyệt nhất quán của MỘT tài khoản.
//
// Nguyên tắc chống phát hiện khi 2 tài khoản chung 1 IP / 1 thiết bị:
//   - Cùng accountId  -> luôn ra cùng một persona (Facebook không thấy thiết bị
//     đổi liên tục giữa các lần chạy -> tránh nghi ngờ).
//   - accountId khác  -> persona khác (2 acc trông như 2 máy khác nhau, dù chung
//     IP/thiết bị vật lý).
//   - Mọi thành phần (UA, platform, ngôn ngữ, múi giờ, màn hình, phần cứng,
//     WebGL) được chọn ĐỒNG BỘ với nhau (Win + en-US + America/..., Linux + vi-VN
//     + Asia/...) để không có giá trị nào mâu thuẫn.
struct Fingerprint
{
    // Seed gốc sinh ra persona này (băm từ accountId) — dùng làm seed nhiễu
    // canvas/audio/chữ ký chuột để vân tay LUÔN ỔN ĐỊNH giữa các lần chạy
    // (thay vì random mỗi lần nạp trang — chính sự bất nhất là tín hiệu máy).
    quint64 seed = 0;
    QString userAgent;
    QString platform; // "Win32" | "Linux x86_64"
    QString acceptLanguage; // "vi-VN,vi;q=0.9,en-US;q=0.8,en;q=0.7"
    QStringList languages; // ["vi-VN","vi","en-US","en"]
    QString locale; // "vi-VN"
    QString timezone; // "Asia/Ho_Chi_Minh"
    int screenWidth = 0;
    int screenHeight = 0;
    int hardwareConcurrency = 8;
    int deviceMemory = 8;
    QString webglVendor;
    QString webglRenderer;
};

namespace Fingerprints
{
// Tạo persona ỔN ĐỊNH, KHÁC BIỆT theo accountId (seed). Trả persona mặc định
// hợp lý nếu seed rỗng.
Fingerprint forAccount(const QString &accountId);

// Persona cho trường hợp KHÔNG CÓ tài khoản ("mặc định"): deterministic theo UA
// chuỗi của phiên — mọi tab trong cùng trình duyệt tính ra CÙNG vân tay (khớp
// với nhau và khớp phiên chính), thay vì mỗi tab tự random một giá trị khác nhau
// (WebGL/cores/memory lệch nhau giữa các tab là tín hiệu máy rõ ràng).
// Ngôn ngữ/múi giờ mặc định vi-VN (người dùng Việt Nam), đồng bộ với UA thật.
Fingerprint forDefaultUa(const QString &ua, int winWidth, int winHeight);
} // namespace Fingerprints
