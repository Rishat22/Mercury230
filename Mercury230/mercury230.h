#ifndef MERCURY230_H
#define MERCURY230_H

#include <QObject>

//-------------------------
#include "CListenThread.h"
//-------------------------

class Mercury230 : QObject
{
    Q_OBJECT
public:
    explicit Mercury230(QObject* parent  = nullptr);
    bool IsObjectDBOpened;
    bool IsSysQueryCreated;
    bool IsObjQueryCreated;
    QString argstr;                                  // 12.12.2017 Вводные аргументы DRIVER(Mercury230<*>)
    CDispatchThread* pDT;
    CListenThread* pLT;
    QSqlDatabase sdb;
    QSqlDatabase odb;
    QSqlQuery *mSQuery;                              // Запрос к системной БД
    QSqlQuery *mOQuery;                              // Запрос к объектной БД

    quint32 ObjectID;
    QString GetData(CDispatchThread* pServerObject); // Общее чтение
signals:

public slots:
};
extern "C" QString GetInfoDll(QString &data);
extern "C" QString GetData(CDispatchThread* pServerObject);
extern "C" QString GetDataArg(CDispatchThread* pServerObject, QString args);
#endif // MERCURY230_H
