#pragma once

#include <QString>
#include <QStringList>

#include <atomic>

class WebDriver;

// Kết quả đăng bài: thành công / thất bại thông thường / tài khoản bị chặn.
enum class PostResult
{
    Ok,
    Failed,
    Banned
};

// Tương đương postToGroup/postToGroupWithRetry trong Java.
class PostEngine
{
public:
    explicit PostEngine(WebDriver *driver);

    PostResult postToGroupWithRetry(const QString &groupId, const QString &content,
                                    const QStringList &images, int maxRetries,
                                    std::atomic<bool> *stop);
    PostResult postToGroup(const QString &groupId, const QString &content,
                           const QStringList &images);

    bool isCheckpoint();

private:
    bool clickSubmitButton();
    // Kiểm tra dialog đang mở có phải thông báo chặn-giới-hạn tốc độ đăng
    // ("Bạn đang đăng quá nhanh", "Try again later"...). Khi gặp phải KHÔNG retry
    // mà báo Banned ngay để xoay tài khoản — retry dồn dập chỉ làm acc chết thêm.
    bool isRateLimited();
    static QString countCondition(const QString &xpath);
    static QString visibleCondition(const QString &xpath);

    WebDriver *m_driver;
};
