#include "mercury230.h"

// -------------------------------- Стандартный блок для всех драйверов -----------------------------------------
Mercury230::Mercury230(QObject *parent) : QObject(parent)
{
    IsObjectDBOpened  = false;
    IsSysQueryCreated = false;
    IsObjQueryCreated = false;
    qDebug() << "Mercury230";
}
// Получение данных от прибора
QString GetData(CDispatchThread* pServerObject)
{
    QString result;
    Mercury230 *pMercury = new Mercury230(nullptr);
    pMercury->argstr.clear();
    result = pMercury->GetData(pServerObject);
    delete pMercury;
    return result;
}
// 12.12.2017 Получение данных от прибора со строкой параметров
QString GetDataArg(CDispatchThread* pServerObject, QString args)
{
    QString result;
    Mercury230 *pMercury = new Mercury230(nullptr);
    pMercury->argstr = args;
    result = pMercury->GetData(pServerObject);
    delete pMercury;
    return result;
}

// Чтение информации о драйвере
QString GetInfoDll(QString &data)
{
    QString info;
    info = "Драйвер опроса электросчетчика \"Энергомера\" mercury230\r\n"
                   "Версия драйвера 1.00\r\n"
                   "Разработчик ООО \"НПК\"СКАРТ\"\r\n"
                   "Дата 25.07.2018\r\n"
                   "Драйвер предназначен для получения данных с прибора учета\r\n"
                   "Изменения:\r\n";
    data = "текущие параметры";
    return info;
}
// -------------------------------- Окончание стандартного блока ------------------------------------------------

QString Mercury230::GetData(CDispatchThread *pServerObject)
{
    bool IsOk;
    if(!argstr.isEmpty())
    {
        quint64 num = argstr.toUInt(&IsOk, 10);
        if((!IsOk)|(num ==0))
            argstr.clear();
    }
    QString exitcode = GetErrorString(GENERALERROR);
    pDT = pServerObject;
    pLT = ((CListenThread*)(pDT->par));
    connect(this, SIGNAL(Print(QString,bool)), pLT, SLOT(PrintString(QString,bool)), Qt::QueuedConnection);
    connect(this, SIGNAL(SendDataToClient(QByteArray,int)), pLT, SLOT(SendDataFromDLL(QByteArray,int)), Qt::QueuedConnection);


    // Разъединение сигналов
    disconnect(this, SIGNAL(Print(QString,bool)), pLT, SLOT(PrintString(QString,bool)));
    disconnect(this, SIGNAL(SendDataToClient(QByteArray,int)), pLT, SLOT(SendDataFromDLL(QByteArray,int)));

    if(IsObjQueryCreated)
        delete mOQuery;
    if(IsSysQueryCreated)
        delete mSQuery;
    if(IsObjectDBOpened)
    {
        { odb.close(); }
        QSqlDatabase::removeDatabase("CE303:" + QString::number(ObjectID));
    }
    return exitcode;
}
