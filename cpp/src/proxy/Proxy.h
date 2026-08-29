#pragma once

#include <QString>

// Cấu hình proxy đã được phân tích từ chuỗi người dùng nhập.
struct ProxyInfo
{
    bool valid = false;
    QString scheme; // http | https | socks4 | socks5
    QString host;
    int port = 0;
    QString username;
    QString password;
};

// Phân tích chuỗi proxy. Hỗ trợ định dạng:
//   host:port
//   http://host:port
//   https://host:port
//   socks4://host:port
//   socks5://host:port
//   user:pass@host:port (có thể kèm scheme như trên)
ProxyInfo parseProxy(const QString &proxy);

// Tham số --proxy-server cho Chrome (bỏ phần user/pass vì Chrome không đọc
// thông tin đăng nhập từ URL proxy).
QString proxyServerArgument(const ProxyInfo &info);

// Kiểm tra nhanh khả năng kết nối TCP tới host:port của proxy.
// Trả false nếu proxy sai định dạng hoặc không kết nối được trong timeoutMs.
bool testProxyReachability(const QString &proxy, int timeoutMs = 3000);

// Viết extension tối thiểu (MV3) trả lời lời mời xác thực proxy
// (webRequest.onAuthRequired). Trả về đường dẫn thư mục extension,
// rỗng nếu lỗi. Gọi khi proxy có tên/pass.
QString writeProxyAuthExtension(const QString &baseDir, const ProxyInfo &info);
