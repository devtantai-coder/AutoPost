#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QStringList>

class CdpClient;

// Lớp tương đương Selenium WebDriver, chạy trên Chrome DevTools Protocol.
class WebDriver : public QObject
{
    Q_OBJECT
public:
    explicit WebDriver(QObject *parent = nullptr);

    void init(CdpClient *cdp);

    bool enableDomains();
    void initPageHooks();
    // Tiêm lớp chống phát hiện với "vân tay" nhất quán với phiên này:
    // ua/platform khớp UA thật, kích thước màn hình theo window, ngôn ngữ/locale
    // đồng bộ, và các giá trị hardware/WebGL LẤY TỪ persona (cố định theo tài
    // khoản) — kèm chặn WebRTC lộ IP thật qua proxy.
    void addAntiDetectionScript(const QString &ua, const QString &platform, int screenWidth,
                                int screenHeight, const QStringList &languages,
                                const QString &webglVendor, const QString &webglRenderer,
                                int hardwareConcurrency, int deviceMemory,
                                quint64 personaSeed = 0);
    // waitForResponse=false: lệnh thiết lập không cần kết quả -> gửi không chờ,
    // CDP xử lý FIFO nên vẫn xong trước mọi lệnh đọc trạng thái gửi sau đó.
    void setUserAgentOverride(const QString &ua, const QString &lang, bool waitForResponse = true);
    void setTimezone(const QString &timezoneId, bool waitForResponse = true);
    void setLocale(const QString &locale, bool waitForResponse = true);
    // Khóa kích thước màn hình (screen.*, viewport) nhất quán với window thật.
    void setScreenMetrics(int width, int height, bool waitForResponse = true);

    bool navigate(const QString &url, int timeoutMs = 20000);
    bool waitForReady(int timeoutMs = 20000);
    bool waitForCondition(const QString &jsCondition, int timeoutMs = 10000);

    QJsonValue evaluate(const QString &js, int timeoutMs = 10000);

    QString currentUrl();
    QString pageSource();
    void scrollToBottom();
    void randomScroll();

    QJsonArray queryAll(const QString &xpath, int timeoutMs = 10000);
    int count(const QString &xpath);
    int visibleCount(const QString &xpath);
    bool clickNth(const QString &xpath, int index);
    // Click bằng sự kiện chuột THẬT qua CDP: di chuột theo đường cong Bezier rồi
    // bấm, thay vì sự kiện JS giả (dấu hiệu automation).
    bool realClick(const QString &xpath, int index);
    // Dropdown tùy chỉnh của Facebook (r.php mới): mở [role=combobox] thứ
    // comboboxIndex (0=Ngày, 1=Tháng, 2=Năm, 3=Giới tính) bằng chuột thật,
    // cuộn option có text == value vào giữa rồi bấm chọn. Trả false nếu không
    // mở được hoặc không tìm thấy option (dropdown chưa hiển thị).
    bool selectCombobox(int comboboxIndex, const QString &value);
    bool setContentEditableText(const QString &xpath, const QString &text);
    bool typeText(const QString &xpath, const QString &text);
    bool pasteText(const QString &xpath, const QString &text);

    bool uploadFiles(const QString &cssSelector, const QStringList &paths);

    // Gửi phím Escape thật qua CDP — đóng dropdown/listbox đang mở (dùng trước
    // khi mở dropdown khác) hoặc thoát khỏi trình xem video/ảnh phóng to.
    bool pressEscape();

    static QString jsStr(const QString &text);

private:
    // Gửi sự kiện phím Enter thật qua CDP (Input.dispatchKeyEvent) vào phần tử
    // đang focus. Facebook/Lexical xử lý như người bấm Enter: đúng 1 ngắt dòng,
    // tránh lỗi bài đăng bị cách dòng đôi.
    bool pressEnter();

    // Tọa độ viewport của phần tử (tâm + một điểm ngẫu nhiên bên trong).
    QJsonObject elementRect(const QString &xpath, int index);
    // Di chuyển chuột tới (x,y) theo đường cong Bezier ngẫu nhiên, từng bước nhỏ.
    void humanMouseMove(double toX, double toY);

    CdpClient *m_cdp = nullptr;
};
