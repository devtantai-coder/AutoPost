#pragma once

#include <QString>

struct FacebookGroup
{
    QString id;
    QString name;
    QString url;
    QString privacy = QStringLiteral("unknown");
    qint64 memberCount = 0;
    bool isMember = false;
    bool pending = false;
    bool selected = true;
};
