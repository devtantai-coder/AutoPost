cmake_minimum_required(VERSION 3.16)
project(AutoPostCPP VERSION 1.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# Chuẩn C++23 + cảnh báo hiện đại
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Bật tự động chạy moc cho các lớp dùng Q_OBJECT (MainWindow, tests...).
set(CMAKE_AUTOMOC ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# Thư viện ngoài (mới nhất) qua FetchContent: fmt, spdlog, nlohmann_json
# ---------------------------------------------------------------------------
include(FetchContent)

# fmt — định dạng chuỗi kiểu C++23 std::format (nhanh, an toàn kiểu)
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 11.0.2
    GIT_SHALLOW ON
)
set(FMT_TEST OFF CACHE BOOL "" FORCE)
set(FMT_DOC OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(fmt)

# nlohmann/json — thư viện JSON header-only chuẩn công nghiệp
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW ON
)
FetchContent_MakeAvailable(nlohmann_json)

# ---------------------------------------------------------------------------
# Qt6 (hoặc fallback Qt5)
# ---------------------------------------------------------------------------
find_package(Qt6 QUIET COMPONENTS Widgets Network WebSockets Concurrent)
if(NOT Qt6_FOUND)
    find_package(Qt5 REQUIRED COMPONENTS Widgets Network WebSockets Concurrent)
endif()
find_package(Qt6 QUIET COMPONENTS Test)

set(SOURCES
    src/main.cpp
    src/store/Config.cpp
    src/store/DataStore.cpp
    src/store/GroupStore.cpp
    src/store/PostedStore.cpp
    src/store/DailyPostLog.cpp
    src/store/DashboardStore.cpp
    src/store/ReportExporter.cpp
    src/utils/Logger.cpp
    src/utils/Utils.cpp
    src/utils/TemplateEngine.cpp
    src/utils/Notifier.cpp
    src/proxy/Proxy.cpp
    src/proxy/ProxyHealth.cpp
    src/cdp/ChromeLauncher.cpp
    src/cdp/CdpClient.cpp
    src/cdp/WebDriver.cpp
    src/cdp/Fingerprint.cpp
    src/fb/FacebookSession.cpp
    src/fb/FacebookParser.cpp
    src/fb/PostEngine.cpp
    src/fb/JoinEngine.cpp
    src/fb/NurtureEngine.cpp
    src/fb/FbWorker.cpp
    src/fb/PostWorker.cpp
    src/fb/TabWorker.cpp
    src/ui/MainWindow.cpp
    src/ui/CookieDialog.cpp
    src/ui/RentedDialog.cpp
    src/ui/BarChart.cpp
    src/ui/DonutChart.cpp
)

set(HEADERS
    src/model/FacebookAccount.h
    src/model/FacebookGroup.h
    src/model/RentedAccount.h
    src/store/Config.h
    src/store/DataStore.h
    src/store/GroupStore.h
    src/store/PostedStore.h
    src/store/DailyPostLog.h
    src/store/DashboardStore.h
    src/store/ReportExporter.h
    src/utils/Logger.h
    src/utils/Utils.h
    src/utils/TemplateEngine.h
    src/utils/Notifier.h
    src/proxy/Proxy.h
    src/proxy/ProxyHealth.h
    src/cdp/ChromeLauncher.h
    src/cdp/CdpClient.h
    src/cdp/WebDriver.h
    src/cdp/Fingerprint.h
    src/fb/FacebookSession.h
    src/fb/FacebookParser.h
    src/fb/PostEngine.h
    src/fb/JoinEngine.h
    src/fb/NurtureEngine.h
    src/fb/FbWorker.h
    src/fb/PostWorker.h
    src/fb/TabWorker.h
    src/ui/MainWindow.h
    src/ui/CookieDialog.h
    src/ui/RentedDialog.h
    src/ui/BarChart.h
    src/ui/DonutChart.h
    src/ui/Theme.h
)

add_executable(autopostcpp ${SOURCES} ${HEADERS})

target_include_directories(autopostcpp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(autopostcpp PRIVATE
    fmt::fmt
    nlohmann_json::nlohmann_json
)

if(Qt6_FOUND)
    target_link_libraries(autopostcpp PRIVATE Qt6::Widgets Qt6::Network Qt6::WebSockets Qt6::Concurrent)
else()
    target_link_libraries(autopostcpp PRIVATE Qt5::Widgets Qt5::Network Qt5::WebSockets Qt5::Concurrent)
endif()

# Bật LTO cho bản Release: mã nhiều template/QObject hưởng lợi rõ, binary nhỏ hơn
# và nhanh hơn ở vòng lặp/JSON. Kiểm tra IPO được hỗ trợ trước khi bật.
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_ok OUTPUT ipo_out)
    if(ipo_ok)
        set_target_properties(autopostcpp PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()
endif()

# Cảnh báo hiện đại chỉ áp dụng cho mã của dự án (không ảnh hưởng thư viện ngoài).
target_compile_options(autopostcpp PRIVATE -Wall -Wextra -Wpedantic)
if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(autopostcpp PRIVATE /W4)
endif()

# --- Unit test cho phần lõi (chỉ build khi có Qt Test) ---
if(Qt6_FOUND AND Qt6Test_FOUND)
    enable_testing()
    add_executable(autopostcpp_tests
        tests/tests.cpp
        src/utils/Utils.cpp
        src/utils/TemplateEngine.cpp
        src/store/ReportExporter.cpp
        src/store/DataStore.cpp
        src/store/PostedStore.cpp
        src/store/DailyPostLog.cpp
    )
    target_include_directories(autopostcpp_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(autopostcpp_tests PRIVATE
        Qt6::Test Qt6::Core Qt6::Network
        fmt::fmt
        nlohmann_json::nlohmann_json
    )
    target_compile_options(autopostcpp_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME autopostcpp_tests COMMAND autopostcpp_tests)
endif()
