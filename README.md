<div align="center">

# 🚀 AutoPost C++

**Công cụ đăng bài Facebook Groups tự động — bản C++ gốc, không Selenium, không deprecated API.**

*Điều khiển Chrome trực tiếp qua Chrome DevTools Protocol · Song song đa trình duyệt · Chống phát hiện ở cấp "persona"*

![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C?logo=cplusplus&logoColor=white)
![Qt6](https://img.shields.io/badge/Qt-6%20%2F%205-41CD52?logo=qt&logoColor=white)
![Platform](https://img.shields.io/badge/N%E1%BB%81n%20t%E1%BA%A3ng-Linux%20%C2%B7%20Windows%20%C2%B7%20macOS-blue)
![License](https://img.shields.io/badge/Tr%E1%BA%A1ng%20th%C3%A1i-S%E1%BA%B3n%20s%C3%A0ng%20d%C3%B9ng-success)

</div>

---

## 📖 Mục lục

1. [AutoPost là gì?](#-autopost-là-gì)
2. [Điểm nổi bật](#-điểm-nổi-bật)
3. [Vì sao chọn bản C++ này?](#-vì-sao-chọn-bản-c-này)
4. [Kiến trúc tổng quan](#-kiến-trúc-tổng-quan)
5. [Công nghệ sử dụng](#-công-nghệ-sử-dụng)
6. [Chuẩn bị & Build](#-chuẩn-bị--build)
7. [Hướng dẫn sử dụng theo từng Tab](#-hướng-dẫn-sử-dụng-theo-từng-tab)
8. [Cơ chế chống phát hiện (Anti-Detection)](#-cơ-chế-chống-phát-hiện-anti-detection)
9. [Nhớ cấu hình quan trọng](#-nhớ-cấu-hình-quan-trọng)
10. [Dữ liệu & quyền riêng tư](#-dữ-liệu--quyền-riêng-tư)
11. [Kiểm thử tự động](#-kiểm-thử-tự-động)
12. [Xử lý sự cố thường gặp](#-xử-lý-sự-cố-thường-gặp)
13. [Cấu trúc mã nguồn](#-cấu-trúc-mã-nguồn)
14. [Đóng góp & Giấy phép](#-đóng-góp--giấy-phép)

---

## 🔍 AutoPost là gì?

AutoPost là **ứng dụng desktop** (không phải web, không cần server) giúp bạn:

- 📝 **Đăng bài hàng loạt** vào Facebook Groups — một nội dung hoặc nhiều nội dung xoay vòng, kèm ảnh.
- 👥 **Quản lý nhiều tài khoản** Facebook cùng lúc, tự xoay vòng khi một tài khoản bị chặn.
- 🌐 **Che IP bằng proxy** — xoay vòng proxy mỗi bài, kiểm tra sức khỏe proxy, thậm chí **tự tải proxy miễn phí** từ nguồn công khai.
- 🤖 **Tự động tham gia nhóm** theo từ khóa hoặc danh sách ID, có lọc nhóm riêng tư / đang chờ duyệt.
- 🌱 **"Nuôi" tài khoản mới** (account warming): lướt feed, thả like, xem video như người thật trước khi đăng.
- 💰 **Quản lý tài khoản cho thuê**: mỗi khách có cookie, nội dung, gói bài và giá riêng — hết bài tự nhảy sang khách kế tiếp.
- 📊 **Dashboard thống kê thời gian thực**: số bài đăng, tỉ lệ thành công, biểu đồ cột & bánh, lịch sử từng phiên.

> **Cốt lõi khác biệt:** AutoPost **không dùng Selenium hay WebDriver** — nó nói chuyện trực tiếp với Chrome qua **Chrome DevTools Protocol (CDP)** qua WebSocket, tự viết tay driver, tự tiêm script chống phát hiện. Nhanh hơn, nhẹ hơn, và kiểm soát từng pixel hành vi.

---

## ✨ Điểm nổi bật

### ⚡ Hiệu năng thật sự
| Cơ chế | Mô tả |
|---|---|
| **Đa trình duyệt × đa tab** | Tối đa **5 trình duyệt × 10 tab = 50 luồng đăng** chạy song song thật sự (mỗi tab một thread riêng). |
| **Work-stealing** | Các tab cùng bốc nhóm từ một chỉ số chung — không có tab nào rảnh khi tab khác còn việc. |
| **Auto-Tune** | Nút "Tự động tinh chỉnh" đo **số core CPU + dung lượng RAM** của máy rồi gợi ý số trình duyệt/tab tối ưu — không để máy chết đứng vì mở quá tay. |
| **Batch dùng chung** | Danh sách nhóm được truyền cho mọi tab bằng `shared_ptr` — với 50 tab, không nhân bản vector 50 lần. |
| **LTO Release** | Build Release bật Link-Time Optimization — binary nhỏ hơn, vòng lặp JSON/teleport nhanh hơn. |

### 🕵️ Chống phát hiện ở mức sâu
- **Persona fingerprint ổn định**: mỗi tài khoản luôn có **cùng một** User-Agent, màn hình, ngôn ngữ, múi giờ, WebGL qua các lần chạy — vì *sự bất nhất mới là tín hiệu máy móc*.
- **Click chuột thật**: di chuyển theo **đường cong Bezier** + `Input.dispatchMouseEvent`, không phải `element.click()`.
- **Dán Ctrl+V** thay vì gõ robot — giữ đúng số dòng, giống người copy-paste.
- **Nhịp người thật**: tạm dừng ngẫu nhiên theo **phân phối exponential**, cuộn trang ngẫu nhiên, jitter trộn vào delay mỗi bài.
- **Cầu chì chống ban**: nếu **3+ tài khoản bị chặn trong một phiên**, toàn bộ dàn máy **tự dừng** để bảo vệ các acc còn lại.

### 🧠 Thông minh theo cách của người vận hành
- **Lịch nhiều mốc giờ** trong ngày (08:00, 12:30, 20:00...) + **giới hạn bài/ngày** cho từng tài khoản.
- **Template engine**: `{{ten}}`, `{{group}}`/`{{nhom}}`, `{{ngay}}`, `{{gio}}`, `{{ngay-gio}}`, `{{thang}}`, `{{nam}}` — tự thay trong nội dung bài theo từng nhóm/tài khoản/thời điểm.
- **Bỏ qua nhóm đã đăng hôm nay** — không spam trùng.
- **Thông báo khi xong**: toast trên desktop, **Telegram bot**, hoặc **Discord webhook**.
- **Sao lưu tự động** định kỳ toàn bộ dữ liệu JSON.

---

## 💎 Vì sao chọn bản C++ này?

Dự án từng có bản Java (Selenium). Bản C++ này viết lại **hoàn toàn từ đầu** với triết lý khác:

| | Bản Java cũ | **AutoPost C++** |
|---|---|---|
| Điều khiển trình duyệt | Selenium WebDriver | **CDP thô qua QWebSocket** — không middleware |
| Kích thước runtime | JVM ~200MB+ | Binary native, khởi động tức thì |
| Đa tab | Mở nhiều driver | **1 trình duyệt, nhiều tab, nhiều thread** |
| Fingerprint | Không có | **Persona ổn định theo accountId** |
| Retry | Cố định | **Exponential backoff + nhận diện rate-limit** |
| Khi bị "đăng quá nhanh" | Vẫn retry dồn dập | **Dừng ngay, xoay tài khoản** — không dồn acc vào chỗ chết |
| Dữ liệu | Database | **JSON thuần, ghi atomic** (temp + rename) — không bao giờ hỏng file |

---

## 🏗 Kiến trúc tổng quan

```
┌─────────────────────────────────────────────────────────────────┐
│                        MainWindow (UI Qt)                       │
│   Dashboard │ Đăng bài │ Nhóm │ Thuê │ Tham gia │ Proxy │ ...  │
└──────────────────────────────┬──────────────────────────────────┘
                               │ Qt signals/slots (cross-thread)
┌──────────────────────────────▼──────────────────────────────────┐
│                           FbWorker                               │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌────────────┐ │
│  │ PostWorker │  │ PostWorker │  │ PostWorker │  │  ...×N     │ │  ← mỗi trình duyệt
│  │  (Browser) │  │  (Browser) │  │  (Browser) │  │            │ │
│  └─────┬──────┘  └─────┬──────┘  └─────┬──────┘  └─────┬──────┘ │
│    ┌───▼───┐     ┌───▼───┐     ┌───▼───┐        ┌───▼───┐    │
│    │TabWork│     │TabWork│     │TabWork│        │TabWork│    │  ← mỗi tab một thread
│    │ er #1 │     │ er #2 │     │ er #3 │        │ er #M │    │     (work-stealing)
│    └───┬───┘     └───┬───┘     └───┬───┘        └───┬───┘    │
└────────┼─────────────┼─────────────┼────────────────┼────────┘
         │   WebDriver (tiêm anti-detect, navigate, click, type)
         ▼
   ┌──────────┐  WebSocket  ┌───────────────┐  spawn  ┌────────────┐
   │ CdpClient│◄────────────►│ ChromeLauncher │───────►│ Chrome/    │
   └──────────┘   CDP JSON   └───────────────┘  proxy  │ Chromium   │
                                                   └────────────┘
```

**Luồng dữ liệu:**
- `store/` — mọi thứ lưu **JSON file** trong `src/data/`, ghi an toàn (temp → rename), mutex đa luồng.
- `proxy/` — parse proxy, kiểm tra TCP reachability, sinh extension MV3 cho proxy có user/pass.
- `cdp/` — hạt nhân kỹ thuật: launch Chrome, WebSocket CDP, WebDriver, Fingerprint persona.
- `fb/` — "não" nghiệp vụ: đăng bài, tham gia nhóm, nuôi acc, parser.
- `ui/` — giao diện Qt Widgets, chủ đề Google Material, biểu đồ tự vẽ (BarChart, DonutChart).

---

## 🧰 Công nghệ sử dụng

| Thành phần | Công nghệ | Lý do chọn |
|---|---|---|
| Ngôn ngữ | **C++23** | `std::ranges`, `std::mt19937_64`, structured bindings |
| GUI & network | **Qt 6** (Widgets, Network, WebSockets, Concurrent) — fallback Qt5 | QWebSocket cho CDP, QThread cho song song |
| Định dạng chuỗi | **{fmt} 11.0.2** | Nhanh, an toàn kiểu |
| JSON | **nlohmann/json 3.11.3** | Chuẩn công nghiệp, header-only |
| Build | **CMake 3.16+** + FetchContent | Tự kéo dependencies, không cài tay |
| Trình duyệt | Chrome/Chromium có sẵn trên máy | Không bundle, dùng bản người dùng đang có |
| Ghi log | spdlog (qua FetchContent) + Logger custom | Log tập trung vào file |

---

## 🚀 Chuẩn bị & Build

### Yêu cầu

- **Compiler C++23**: GCC 13+ / Clang 16+ / MSVC 2022+ (kiểm tra bằng `g++ --version`)
- **CMake ≥ 3.16**
- **Qt 6** (khuyến nghị) hoặc Qt 5 — gói: `Widgets`, `Network`, `WebSockets`, `Concurrent`, `Test`
- **Google Chrome / Chromium** đã cài đặt (app tự dò binary trong PATH và vị trí chuẩn)

### Cài Qt nhanh (Ubuntu/Debian)

```bash
sudo apt update
sudo apt install qt6-base-dev qt6-websockets-dev cmake g++
# Chrome:
wget -q https://dl.google.com/linux/direct/google-chrome-stable_current_amd64.deb
sudo apt install ./google-chrome-stable_current_amd64.deb
```

### Build

```bash
cd cpp
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j          # -j = dùng hết core
./build/autopostcpp              # chạy app
```

> **Mẹo:** Lần build đầu sẽ tải `fmt` + `nlohmann_json` từ GitHub (FetchContent) — cần mạng. Các lần sau dùng cache, build chỉ mất vài giây.

### Build kèm unit test

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cd build && ctest --output-on-failure
```

---

## 📘 Hướng dẫn sử dụng theo từng Tab

### 1️⃣ 📊 Dashboard
Trang chủ — **hero banner** hiển thị tổng bài, tỉ lệ thành công, xu hướng; biểu đồ cột theo ngày (BarChart) và bánh tỉ lệ thành công/thất bại (DonutChart); bảng trạng thái từng tài khoản; lịch sử phiên chạy; dòng thời gian hoạt động gần nhất. **Tự lưu** dashboard định kỳ — mở lại app là số liệu vẫn còn.

### 2️⃣ 📝 Đăng bài
1. Nhập **nội dung** — hỗ trợ biến template: `{{ten}}`, `{{group}}`, `{{ngay}}`, `{{gio}}`...
2. Chọn **ảnh** (tối đa 5 ảnh/bài) — bật "Xen kẽ ảnh" để mỗi bài dùng bộ ảnh khác nhau.
3. Chọn **nhóm** (tick từ bảng Groups) và **tài khoản**.
4. Cấu hình: delay giữa bài, số lần retry, số trình duyệt/tab (hoặc bấm **Auto-Tune**), giới hạn bài/ngày.
5. Bấm **Bắt đầu** — theo dõi log realtime, progress bar, và trạng thái từng acc. Bấm **Dừng** bất cứ lúc nào (dừng mềm — chờ tab xong bài hiện tại).

### 3️⃣ 👥 Nhóm (Groups)
- **Lấy nhóm của tôi** — đăng nhập bằng cookie rồi kéo toàn bộ group bạn đã tham gia.
- **Thêm bằng ID** — dán URL hoặc ID nhóm hàng loạt.
- **Import/Export JSON** — chia sẻ danh sách nhóm giữa các máy.
- **Lọc theo quyền riêng tư**: công khai / riêng tư / ẩn; **tìm kiếm** theo tên; chọn tất cả / bỏ tất cả.

### 4️⃣ 🤝 Tham gia nhóm (Join)
- Dán **từ khóa** (app sẽ tìm nhóm công khai khớp) hoặc **danh sách ID nhóm**.
- Cấu hình: số nhóm tối đa / phiên, delay giữa 2 lần join, hành động khi gặp nhóm riêng tư (bỏ qua / vẫn gửi yêu cầu).
- Bật **Auto-Join** để chạy nền theo lịch.

### 5️⃣ 🌱 Nuôi tài khoản (Nurture)
Chọn tài khoản → số like tối đa, số video, thời lượng phiên → **Bắt đầu nuôi**. Mỗi phiên: lướt feed với cuộn ngẫu nhiên, dừng "đọc" bài (xác suất cấu hình), like ngẫu nhiên, mở vài video ngắn rồi thoát. Tất cả thao tác dùng nhịp `humanPause` + `realClick` — giống một người buồn chán lướt Facebook. **Nên nuôi acc mới 2–3 ngày trước khi đăng.**

### 6️⃣ 🌐 Proxy
- Dán danh sách proxy (mỗi dòng một cái). Định dạng hỗ trợ:
  ```
  host:port
  http://host:port
  socks5://user:pass@host:port
  ```
- **Kiểm tra proxy**: test TCP reachability từng cái, hiện kết quả OK/timeout ngay trong danh sách.
- **Tải proxy miễn phí**: chọn nguồn (proxyscrape / geonode / proxy-list.download) → app tải, lọc trùng, (tùy chọn) tự test rồi đổ vào pool.
- **Xoay proxy mỗi bài** — mỗi bài đăng đi từ một IP khác nhau.
- Proxy có user/pass được cấp xác thực qua **extension MV3 tự sinh** (Chrome không đọc credential từ URL).

### 7️⃣ 💰 Tài khoản thuê (Rented)
Bảng quản lý khách thuê: tên, cookie, proxy riêng, nội dung bài + ảnh riêng, **tổng gói bài / đã dùng / giá (VND)**. Khi đăng bằng acc thuê:
- Mỗi acc dùng **nội dung và ảnh của riêng nó**.
- Mỗi bài thành công **trừ 1 vào quota** — hết bài (hoặc bị chặn) **tự chuyển sang acc kế tiếp**.
- Trạng thái hiển thị realtime: `hoạt động` / `bị chặn` / `hết bài`.

### 8️⃣ ⚙️ Cài đặt
Delay & jitter, retry, xoay tài khoản (ngưỡng fail để xoay), headless bật/tắt, **lịch đăng nhiều mốc giờ**, giới hạn bài/ngày, **thông báo Telegram/Discord**, tự backup, sessions...

### 🍪 Quản lý Cookie (nút riêng)
Dán cookie `c_user=...; xs=...;...` — app tự dò cookie active, lưu vào file (chỉ bạn đọc được), và có thể **lưu session** để tái sử dụng. Mỗi tài khoản có thể có cookie riêng.

---

## 🕶 Cơ chế chống phát hiện (Anti-Detection)

Đây là phần **kỹ thuật nhất** của AutoPost — đọc kỹ nếu bạn muốn hiểu vì sao nó "khác biệt":

### 1. Persona — fingerprint ổn định theo tài khoản
Hàm `Fingerprints::forAccount(accountId)` **băm accountId thành seed**, từ seed sinh ra toàn bộ vân tay:
- User-Agent + platform (Win32 / Linux x86_64)
- Ngôn ngữ + locale + múi giờ **đồng bộ** (Win → en-US + timezone Mỹ; Linux → vi-VN + Asia/Ho_Chi_Minh — không bao giờ có cặp mâu thuẫn)
- Kích thước màn hình, `hardwareConcurrency`, `deviceMemory`
- WebGL vendor/renderer

**Quy tắc vàng:** cùng một accountId ⇒ **luôn cùng persona** qua mọi lần chạy (Facebook không thấy "thiết bị" thay đổi liên tục). accountId khác ⇒ persona khác (2 acc chung IP trông như 2 máy vật lý riêng). Seed còn làm nhiễu cho canvas/audio/chuột — **ổn định, không random mỗi trang**.

### 2. WebDriver — tiêm script qua CDP
Trước khi vào trang, `addAntiDetectionScript()` tiêm script:
- Khóa `navigator.userAgent`, `platform`, `languages`, `hardwareConcurrency`, `deviceMemory`, `deviceMemory`
- Giả WebGL `UNMASKED_VENDOR_WEBGL` / `RENDERER` khớp persona
- **Chặn WebRTC lộ IP thật** khi đi proxy
- `screen.*` khóa khớp kích thước window thật (random mỗi phiên nhưng đồng bộ mọi tab cùng trình duyệt)

### 3. Hành vi người thật
| Hành động | Cách AutoPost làm |
|---|---|
| Click | `realClick`: di chuột theo **Bezier** + `Input.dispatchMouseEvent` ở tọa độ thật |
| Nhập chữ | **Ctrl+V paste** (giữ nguyên xuống dòng); fallback: gõ từng ký tự với nhịp ngẫu nhiên |
| Chờ đợi | `humanPause(min, max)` — phân phối exponential, không đều đặn |
| Cuộn | `randomScroll()` — lên/xuống/không cuộn, ngẫu nhiên |
| Delay giữa bài | Delay gốc + **jitter ngẫu nhiên** — 2 tài khoản không bao giờ đăng cùng giây |
| Thứ tự nhóm | **Fisher–Yates shuffle** — mỗi phiên một thứ tự khác |

### 4. Nhận diện và né rate-limit
Khi Facebook hiện "Bạn đang đăng quá nhanh" / "Try again later":
- `PostEngine::isRateLimited()` nhận diện dialog → **không retry** mà trả `PostResult::Banned` ngay.
- Worker **xoay tài khoản** — vì retry dồn dập vào lúc bị ghim chỉ khiến acc chết hẳn.
- Retry thông thường dùng **exponential backoff** (3s→6s→9s... + random) — đi chậm lại khi có tín hiệu xấu.

### 5. Cầu chì toàn dàn (circuit breaker)
FbWorker theo dõi `m_bannedAccounts`. Khi **≥3 acc bị checkpoint/chặn trong một phiên** → **dừng toàn bộ**, ghi log, thông báo. Một tai nạn nhỏ không biến thành mất cả dàn tài khoản.

---

## 🧾 Nhớ cấu hình quan trọng

File `src/data/config.json` (tự sinh, mở app lần đầu là có):

| Khóa | Mặc định | Ý nghĩa |
|---|---|---|
| `delaySec` + `delayType` | 30 giây | Nhịp chờ giữa 2 bài |
| `jitterSec` | 0 | Độ lệch ngẫu nhiên cộng thêm (khuyến nghị 5–15) |
| `retryCount` | 2 | Số lần thử lại khi một bài fail |
| `threadCount` / `tabCount` | 1 / 3 | Số trình duyệt / tab mỗi trình duyệt (tối đa 5/10) |
| `rotateAccounts` + `rotateFailThreshold` | tắt / 2 | Xoay acc khi một acc fail ≥ ngưỡng |
| `dailyPostLimit` | 300 | Trần bài/ngày cho **mỗi tài khoản** |
| `scheduleTimes` | `["08:00"]` | Nhiều mốc `HH:mm` — đăng theo lịch cả ngày |
| `proxyPool` + `rotateProxyPerPost` | rỗng / tắt | Danh sách proxy & xoay mỗi bài |
| `notifyMethod` + `telegramToken`... | 0 | 1=Telegram, 2=Discord webhook |
| `headless` | tắt | Bật để Chrome chạy ẩn (debug khó hơn) |
| `autoBackup` | bật | Tự backup data định kỳ |

> **Khuyến nghị an toàn cho acc mới:** 5–10 bài/ngày trong tuần đầu, nuôi acc trước, dùng proxy sạch cùng khu vực với IP gốc.

---

## 🔐 Dữ liệu & quyền riêng tư

- **Mọi dữ liệu nằm trên máy bạn** — thư mục `src/data/`: `config.json`, `groups.json`, `rented.json`, `dashboard.json`, `facebook_cookies.txt`, `posted.json`, `daily_post_log.json`.
- **Không có server, không telemetry, không gửi dữ liệu đi đâu** — chỉ kết nối ra ngoài khi: mở trang Facebook (qua Chrome), kiểm tra/tải proxy, hoặc gửi thông báo Telegram/Discord **nếu bạn tự cấu hình**.
- **Ghi JSON atomic**: mọi lần ghi đều "viết file tạm → rename" — cúp điện giữa chừng không làm hỏng dữ liệu.
- **Cookie lưu local** trong `facebook_cookies.txt` — đừng commit thư mục `data/` nếu repo của bạn công khai.
- **Single-instance**: `QLockFile` đảm bảo chỉ một phiên chạy — tránh 2 phiên tranh giành Chrome profile.

---

## 🧪 Kiểm thử tự động

Bộ test lõi (không cần Chrome — chạy nhanh trong CI):

```bash
cd build && ctest --output-on-failure
```

Che phủ:
- `Utils::extractGroupId` — bóc tách ID từ đủ loại URL Facebook
- `Utils::normalizePrivacy` — chuẩn hóa công khai/riêng tư/ẩn (VI + EN)
- `Utils::recommendParallelism` — luôn trong khoảng UI cho phép (1..5 browser, 1..10 tab)
- `TemplateEngine::expand` — thay biến `{{ten}}`, `{{group}}`, `{{ngay-gio}}`...
- `Utils::formatNumber`, `PostedStore` roundtrip, `DailyPostLog` giới hạn, xuất CSV tài khoản

```bash
# Kết quả mong đợi:
Test #1: autopostcpp_tests ....... Passed (12 tests)
```

---

## 🛠 Xử lý sự cố thường gặp

| Triệu chứng | Nguyên nhân & cách xử lý |
|---|---|
| **"Không tìm thấy Chrome"** | Cài Chrome/Chromium hoặc đặt `CHROME_PATH` vào PATH. |
| **Build lỗi thiếu Qt** | Kiểm tra `qt6-base-dev qt6-websockets-dev`. Qt5 fallback tự kích hoạt nếu Qt6 không thấy. |
| **Kết nối CDP timeout** | Chrome bị khóa bởi phiên trước — xóa profile sót: app tự dọn khi khởi động (`cleanupAllProfiles`), hoặc xóa tay `data/app.lock`. |
| **Bài đăng fail liên tục** | Cookie hết hạn → mở Cookie Manager dán lại; hoặc nhóm yêu cầu duyệt bài (không phải lỗi app). |
| **"Bạn đang đăng quá nhanh"** | Rate-limit — AutoPost tự xoay acc. Tăng `delaySec`, bật `jitterSec`, giảm thread/tab, nuôi acc thêm vài ngày. |
| **Proxy không kết nối** | Bấm **Kiểm tra proxy** — timeout 3s là proxy chết; dùng nguồn khác hoặc proxy có user/pass. |
| **App không mở lần 2** | Đang có phiên chạy (check system tray) — single-instance là chủ đích. |

---

## 📂 Cấu trúc mã nguồn

```
cpp/
├── CMakeLists.txt            # Build + FetchContent (fmt, nlohmann_json)
├── src/
│   ├── main.cpp              # Entry: single-instance lock, qtMessageHandler
│   ├── model/                # FacebookAccount, FacebookGroup, RentedAccount
│   ├── store/                # DataStore (JSON atomic), Config, GroupStore,
│   │                         # PostedStore, DailyPostLog, DashboardStore, ReportExporter
│   ├── utils/                # Logger, Utils (humanPause...), TemplateEngine, Notifier
│   ├── proxy/                # parseProxy, health check, MV3 auth extension
│   ├── cdp/                  # ChromeLauncher, CdpClient (WS), WebDriver, Fingerprint
│   ├── fb/                   # Session, Parser, PostEngine, JoinEngine, NurtureEngine,
│   │                         # FbWorker, PostWorker, TabWorker
│   ├── ui/                   # MainWindow, CookieDialog, RentedDialog, BarChart, DonutChart, Theme
│   └── data/                 # Toàn bộ JSON dữ liệu (runtime)
└── tests/
    └── tests.cpp             # QtTest cho phần lõi thuần
```

---

## 🤝 Đóng góp & Giấy phép

### Đóng góp
PR luôn chào đón! Quy ước trong codebase:
- Comment tiếng Việt, giải thích **lý do** chứ không chỉ mô tả dòng lệnh.
- Header `#pragma once`, include theo nhóm (Qt → std → local).
- Mọi thao tác I/O JSON đi qua `DataStore` (để được mutex + atomic).
- Test cho mọi hàm thuần mới.

### ⚠️ Miễn trừ trách nhiệm (quan trọng — hãy đọc)

AutoPost là công cụ tự động hóa trình duyệt mạnh, nhưng **cách bạn dùng nó quyết định kết quả**:

- **Facebook Terms of Service cấm hành vi tự động** trên trang của họ. Dùng AutoPost có thể khiến tài khoản bị hạn chế, checkpoint hoặc **ban vĩnh viễn**.
- Đừng spam. Đừng dùng cho nội dung lừa đảo. Kết hợp limit hợp lý + nuôi acc + proxy sạch — hãy đối xử với tài khoản như tài sản.
- Tác giả **không chịu trách nhiệm** cho bất kỳ thiệt hại tài khoản, kinh doanh, hay pháp lý nào phát sinh từ việc sử dụng công cụ này.
- **Dùng cho dữ liệu/nhóm mà bạn có quyền thao tác** và luôn tôn trọng quyền riêng tư người khác.

### Giấy phép
Dự án cá nhân — liên hệ tác giả nếu muốn tái sử dụng thương mại.

---

<div align="center">

**⭐ Nếu AutoPost tiết kiệm được hàng giờ đăng tay mỗi ngày của bạn — hãy cho repo này một star! ⭐**

*AutoPost · C++23 · Qt6 · CDP — đăng bài như người, nhanh như máy.*

</div>
