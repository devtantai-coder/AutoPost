#include <QtTest>

#include "store/DailyPostLog.h"
#include "store/PostedStore.h"
#include "store/ReportExporter.h"
#include "utils/TemplateEngine.h"
#include "utils/Utils.h"

#include <QTemporaryDir>

// Test tự động cho phần lõi: các hàm thuần (không phụ thuộc môi trường Chrome),
// chạy nhanh trong CI / khi build.
class CoreTests : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void extractGroupId_data();
    void extractGroupId();
    void normalizePrivacy_data();
    void normalizePrivacy();
    void recommendParallelismRange();
    void templateExpand();
    void formatNumber_data();
    void formatNumber();
    void accountCsv();
    void postedStoreRoundtrip();
    void dailyPostLogLimit();

private:
    QTemporaryDir m_dataDir;
};

void CoreTests::initTestCase()
{
    // data/ phải trỏ về thư mục tạm để test không đụng dữ liệu thật.
    QVERIFY2(m_dataDir.isValid(), "Không tạo được thư mục tạm cho test");
    qputenv("AUTOPOST_DATA_DIR", m_dataDir.path().toUtf8());
}

void CoreTests::extractGroupId_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");
    QTest::newRow("group url") << QStringLiteral("https://www.facebook.com/groups/1234567890123")
                               << QStringLiteral("1234567890123");
    QTest::newRow("group url with query") << QStringLiteral("https://www.facebook.com/groups/9876543210987/?ref=share")
                                          << QStringLiteral("9876543210987");
    QTest::newRow("raw digits") << QStringLiteral("9876543210987") << QStringLiteral("9876543210987");
    QTest::newRow("no group id") << QStringLiteral("https://www.facebook.com/foo/bar") << QString();
    QTest::newRow("too short digits") << QStringLiteral("abc123") << QString();
    QTest::newRow("empty") << QString() << QString();
}

void CoreTests::extractGroupId()
{
    QFETCH(QString, input);
    QFETCH(QString, expected);
    QCOMPARE(Utils::extractGroupId(input), expected);
}

void CoreTests::normalizePrivacy_data()
{
    QTest::addColumn<QString>("input");
    QTest::addColumn<QString>("expected");
    QTest::newRow("private viet") << QStringLiteral("Riêng tư") << QStringLiteral("riêng tư");
    QTest::newRow("private eng") << QStringLiteral("Private group") << QStringLiteral("riêng tư");
    QTest::newRow("public viet") << QStringLiteral("Công khai") << QStringLiteral("công khai");
    QTest::newRow("public eng") << QStringLiteral("Public") << QStringLiteral("công khai");
    QTest::newRow("shared") << QStringLiteral("Chia sẻ") << QStringLiteral("chia sẻ");
    QTest::newRow("hidden") << QStringLiteral("Ẩn") << QStringLiteral("ẩn");
    QTest::newRow("unknown") << QStringLiteral("xyz") << QStringLiteral("chưa rõ");
}

void CoreTests::normalizePrivacy()
{
    QFETCH(QString, input);
    QFETCH(QString, expected);
    QCOMPARE(Utils::normalizePrivacy(input), expected);
}

void CoreTests::recommendParallelismRange()
{
    // Kết quả phải luôn nằm trong giới hạn UI bất kể cấu hình máy thế nào.
    int threads = 0;
    int tabs = 0;
    Utils::recommendParallelism(&threads, &tabs);
    QVERIFY2(threads >= 1 && threads <= 5, qPrintable(QStringLiteral("threads=%1").arg(threads)));
    QVERIFY2(tabs >= 1 && tabs <= 10, qPrintable(QStringLiteral("tabs=%1").arg(tabs)));
}

void CoreTests::templateExpand()
{
    const QDateTime now(QDate(2026, 8, 14), QTime(9, 5));
    const QString group = QStringLiteral("Nhóm ABC");
    const QString account = QStringLiteral("Nguyễn Văn A");

    QCOMPARE(TemplateEngine::expand(QStringLiteral("Xin chào {{ten}}!"), group, account, now),
             QStringLiteral("Xin chào Nguyễn Văn A!"));
    QCOMPARE(TemplateEngine::expand(QStringLiteral("{{group}} - {{nhom}}"), group, account, now),
             QStringLiteral("Nhóm ABC - Nhóm ABC"));
    QCOMPARE(TemplateEngine::expand(QStringLiteral("Hôm nay {{ngay}}, lúc {{gio}}"), group, account, now),
             QStringLiteral("Hôm nay 14/08/2026, lúc 09:05"));
    QCOMPARE(TemplateEngine::expand(QStringLiteral("{{ngay-gio}}"), group, account, now),
             QStringLiteral("14/08/2026 09:05"));
    QCOMPARE(TemplateEngine::expand(QStringLiteral("Tháng {{thang}}, năm {{nam}}"), group, account, now),
             QStringLiteral("Tháng 08/2026, năm 2026"));
    // Văn bản không có biến giữ nguyên; biến không tồn tại giữ nguyên.
    QCOMPARE(TemplateEngine::expand(QStringLiteral("không có biến"), group, account, now),
             QStringLiteral("không có biến"));
    QCOMPARE(TemplateEngine::expand(QStringLiteral("{{khong-ton-tai}}"), group, account, now),
             QStringLiteral("{{khong-ton-tai}}"));
    QCOMPARE(TemplateEngine::expand(QString(), group, account, now), QString());
}

void CoreTests::formatNumber_data()
{
    QTest::addColumn<qint64>("input");
    QTest::addColumn<QString>("expected");
    QTest::newRow("small") << qint64(999) << QStringLiteral("999");
    QTest::newRow("k") << qint64(12345) << QStringLiteral("12.3K");
    QTest::newRow("m") << qint64(2500000) << QStringLiteral("2.5M");
}

void CoreTests::formatNumber()
{
    QFETCH(qint64, input);
    QFETCH(QString, expected);
    QCOMPARE(Utils::formatNumber(input), expected);
}

void CoreTests::accountCsv()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("acc.csv"));

    QHash<QString, QPair<int, int>> stats;
    stats.insert(QStringLiteral("acc1"), qMakePair(5, 1));
    stats.insert(QStringLiteral("acc2"), qMakePair(3, 0));
    QHash<QString, QString> names;
    names.insert(QStringLiteral("acc1"), QStringLiteral("Nguyễn Văn A"));
    names.insert(QStringLiteral("acc2"), QStringLiteral("Trần Thị B"));

    const QString written = ReportExporter::writeAccountCsv(path, stats, names);
    QCOMPARE(written, path);

    QFile f(path);
    QVERIFY(f.open(QIODevice::ReadOnly));
    const QByteArray raw = f.readAll();
    // BOM UTF-8 (EF BB BF) ở đầu file cho Excel đọc tiếng Việt đúng.
    QVERIFY(raw.startsWith(QByteArray::fromHex("efbbbf")));
    // QString::fromUtf8 tự bỏ BOM khi chuyển sang chuỗi.
    const QString content = QString::fromUtf8(raw);
    QVERIFY(content.contains(QStringLiteral("Tài khoản,ID,Đã rải thành công,Thất bại,Tổng")));
    QVERIFY(content.contains(QStringLiteral("\"Nguyễn Văn A\",\"acc1\",5,1,6")));
    QVERIFY(content.contains(QStringLiteral("\"Trần Thị B\",\"acc2\",3,0,3")));
    QVERIFY(content.contains(QStringLiteral("Tổng,,,,8,1,9")));
}

void CoreTests::postedStoreRoundtrip()
{
    // Đánh dấu đã đăng -> flush -> đọc lại file thấy đúng id.
    PostedStore::markPosted(QStringLiteral("1111111111"));
    PostedStore::flush();
    const bool sameDay = PostedStore::isPostedToday(QStringLiteral("1111111111"));
    const bool other = PostedStore::isPostedToday(QStringLiteral("2222222222"));
    QCOMPARE(sameDay, true);
    QCOMPARE(other, false);
}

void CoreTests::dailyPostLogLimit()
{
    // Ghi vài bài: countToday đếm đúng số bài thành công, flush tồn tại.
    DailyPostLog::logPost(QStringLiteral("accT"), QStringLiteral("TK Test"),
                          QStringLiteral("Nhóm 1"), true);
    DailyPostLog::logPost(QStringLiteral("accT"), QStringLiteral("TK Test"),
                          QStringLiteral("Nhóm 2"), false);
    QCOMPARE(DailyPostLog::countToday(QStringLiteral("accT")), 1);
    DailyPostLog::flush();
    QCOMPARE(DailyPostLog::countToday(QStringLiteral("accT")), 1);
}

QTEST_MAIN(CoreTests)
#include "tests.moc"
