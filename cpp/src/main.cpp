#include <QApplication>
#include <QLockFile>
#include <QMessageBox>
#include <QVector>

#include <atomic>
#include <memory>

#include "store/DataStore.h"
#include "model/FacebookAccount.h"
#include "model/FacebookGroup.h"
#include "utils/Logger.h"
#include "model/RentedAccount.h"
#include "cdp/ChromeLauncher.h"
#include "fb/FbWorker.h"
#include "fb/TabWorker.h"
#include "ui/MainWindow.h"

// Chặn mọi cảnh báo/lỗi của Qt (qWarning/qCritical/qFatal...) đưa vào hệ thống
// log tập trung thay vì trôi ra stdout — phát hiện lỗi nền tảng sớm khi chạy.
static void qtMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    switch (type) {
    case QtDebugMsg:
        Logger::instance().debug(msg);
        break;
    case QtInfoMsg:
        Logger::instance().info(msg);
        break;
    case QtWarningMsg:
        Logger::instance().warn(msg);
        break;
    case QtCriticalMsg:
    case QtFatalMsg:
        Logger::instance().error(msg);
        break;
    }
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName("AutoPost");
    QApplication::setApplicationName("FacebookAutoPoster");

    qInstallMessageHandler(qtMessageHandler);

    // Chỉ cho chạy MỘT phiên duy nhất: phiên thứ hai báo lỗi rồi thoát ngay.
    // QLockFile tự dọn khóa khi process chết đột ngột (stale lock = 0).
    QLockFile instanceLock(DataStore::filePath(QStringLiteral("app.lock")));
    instanceLock.setStaleLockTime(0);
    if (!instanceLock.tryLock(100)) {
        QMessageBox::warning(nullptr, QStringLiteral("AutoPost"),
                             QStringLiteral("Ứng dụng đã đang chạy. Chỉ được mở một phiên duy nhất."));
        return 1;
    }

    Logger::instance().info(QStringLiteral("Khởi động AutoPost v1.0 · log: %1")
                                .arg(Logger::instance().logDir()));

    // Dọn các profile Chrome còn sót từ lần chạy trước (thoát đột ngột).
    ChromeLauncher::cleanupAllProfiles();

    qRegisterMetaType<QVector<FacebookGroup>>("QVector<FacebookGroup>");
    qRegisterMetaType<GroupBatch>("GroupBatch");
    qRegisterMetaType<IndexBatch>("IndexBatch");
    qRegisterMetaType<QVector<int>>("QVector<int>");
    qRegisterMetaType<std::shared_ptr<std::atomic<int>>>("std::shared_ptr<std::atomic<int>>");
    qRegisterMetaType<FacebookAccount>();
    qRegisterMetaType<QVector<FacebookAccount>>("QVector<FacebookAccount>");
    qRegisterMetaType<RentedAccount>();
    qRegisterMetaType<QVector<RentedAccount>>("QVector<RentedAccount>");
    qRegisterMetaType<PostRequest>();
    qRegisterMetaType<JoinRequest>();
    qRegisterMetaType<NurtureRequest>();

    MainWindow w;
    w.show();
    return app.exec();
}
