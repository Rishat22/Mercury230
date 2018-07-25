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
signals:

public slots:
};
extern "C" QString GetInfoDll(QString &data);
extern "C" QString GetData(CDispatchThread* pServerObject);
extern "C" QString GetDataArg(CDispatchThread* pServerObject, QString args);
#endif // MERCURY230_H
