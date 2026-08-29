#pragma once

#include <QDialog>
#include <QVector>

#include "model/FacebookAccount.h"

class QTableWidget;
class QLabel;

// Quản lý nhiều tài khoản Facebook (mỗi tài khoản tương ứng một cookie).
class CookieDialog : public QDialog
{
    Q_OBJECT
public:
    CookieDialog(const QVector<FacebookAccount> &accounts, const QString &activeCookie,
                 QWidget *parent = nullptr);

    QVector<FacebookAccount> accounts() const;

private:
    void addAccount();
    void editSelected();
    void removeSelected();
    void addMany();
    void importFromFile();
    void importFromJson();
    void exportJson();
    void parseAndAppend(const QStringList &lines);
    void refreshTable();
    bool execAccountDialog(QString *name, QString *proxy, QString *cookie, bool creating);

    QTableWidget *m_table = nullptr;
    QLabel *m_lblStatus = nullptr;
    QVector<FacebookAccount> m_accounts;
};
