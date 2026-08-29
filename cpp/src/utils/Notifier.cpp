#include "utils/Notifier.h"

#include "store/Config.h"
#include "utils/Logger.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace Notifier
{
namespace
{
void postJson(const QString &url, const QJsonObject &payload)
{
    if (url.isEmpty())
        return;

    // QNetworkAccessManager sống trên heap: tự hủy sau khi nhận phản hồi.
    auto *nam = new QNetworkAccessManager;
    auto *reply = nam->post(
        [url] {
            QNetworkRequest req{QUrl(url)};
            req.setHeader(QNetworkRequest::ContentTypeHeader,
                          QStringLiteral("application/json"));
            return req;
        }(),
        QJsonDocument(payload).toJson(QJsonDocument::Compact));

    QObject::connect(reply, &QNetworkReply::finished, nam, [reply, nam]() {
        const QByteArray body = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            Logger::instance().warn(QStringLiteral("[Notifier] Gửi thất bại: %1")
                                        .arg(reply->errorString()));
        } else if (!body.isEmpty()) {
            // Một số API trả về trường "ok":false khi sai token — ghi log nhẹ.
            const QJsonObject o = QJsonDocument::fromJson(body).object();
            if (o.contains(QStringLiteral("ok")) && !o.value(QStringLiteral("ok")).toBool())
                Logger::instance().warn(
                    QStringLiteral("[Notifier] API báo lỗi: %1")
                        .arg(o.value(QStringLiteral("description")).toString()));
        }
        reply->deleteLater();
        nam->deleteLater();
    });
}
} // namespace

void send(const Config &cfg, const QString &title, const QString &message)
{
    if (!cfg.notifyDone)
        return;

    if (cfg.notifyMethod == 1) {
        // Telegram Bot API.
        if (cfg.telegramToken.isEmpty() || cfg.telegramChatId.isEmpty())
            return;
        QJsonObject o;
        o.insert(QStringLiteral("chat_id"), cfg.telegramChatId);
        o.insert(QStringLiteral("text"), title + QStringLiteral("\n\n") + message);
        o.insert(QStringLiteral("parse_mode"), QStringLiteral("HTML"));
        postJson(QStringLiteral("https://api.telegram.org/bot") + cfg.telegramToken +
                     QStringLiteral("/sendMessage"),
                 o);
    } else if (cfg.notifyMethod == 2) {
        // Discord webhook.
        if (cfg.discordWebhook.isEmpty())
            return;
        QJsonObject o;
        o.insert(QStringLiteral("content"), title + QStringLiteral(" — ") + message);
        postJson(cfg.discordWebhook, o);
    }
}
} // namespace Notifier
