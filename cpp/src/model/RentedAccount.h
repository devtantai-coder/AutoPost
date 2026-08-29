#pragma once

#include <QMetaType>
#include <QString>
#include <QStringList>

// Một tài khoản Facebook cho thuê: có cookie riêng, nội dung bài đăng riêng
// (chữ + nhiều ảnh) và một gói bài đã mua. Mặc định 1000 bài = 100.000đ;
// admin có thể sửa số bài và giá cho từng tài khoản.
struct RentedAccount
{
    QString id;
    QString name;         // tên khách thuê / tên tài khoản
    QString cookieRaw;    // cookie đăng nhập
    QString proxy;        // proxy riêng (rỗng = không dùng)
    QString postText;     // nội dung bài đăng riêng của tài khoản này
    QStringList images;   // danh sách ảnh (nhiều ảnh, xoay theo từng bài)
    int totalPosts = 1000;    // tổng số bài đã mua (mặc định 1000)
    int price = 100000;       // giá tiền VND (mặc định 1000 bài = 100k)
    int used = 0;             // số bài đã đăng thành công (trừ 1 khi thành công)
    QString status = QStringLiteral("hoạt động"); // "hoạt động" | "bị chặn" | "hết bài"
    bool selected = true;
};
Q_DECLARE_METATYPE(RentedAccount)
