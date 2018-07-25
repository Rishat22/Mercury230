#include "mercury230.h"

// -------------------------------- Стандартный блок для всех драйверов -----------------------------------------
Mercury230::Mercury230(QObject *parent) : QObject(parent)
{
    qDebug() << "Mercury230";
}
// Получение данных от прибора
QString GetData(CDispatchThread* pServerObject)
{
    QString result;
//    CE303 *pCE303 = new CE303(0);
//    pCE303->argstr.clear();
//    result = pCE303->GetData(pServerObject);
//    delete pCE303;
    return result;
}
// 12.12.2017 Получение данных от прибора со строкой параметров
QString GetDataArg(CDispatchThread* pServerObject, QString args)
{
    QString result;
//    CE303 *pCE303 = new CE303(0);
//    pCE303->argstr = args;
//    result = pCE303->GetData(pServerObject);
//    delete pCE303;
    return result;
}

// Чтение информации о драйвере
QString GetInfoDll(QString &data)
{
    QString info;
    info = "Драйвер опроса электросчетчика \"Энергомера\" mercury230\r\n"
                   "Версия драйвера 1.01\r\n"
                   "Разработчик ООО \"НПК\"СКАРТ\"\r\n"
                   "Дата 25.07.2018\r\n"
                   "Драйвер предназначен для получения данных с прибора учета\r\n"
                   "Изменения:\r\n"
                   "Добавлена передача адреса прибора для опроса по шине RS485";
//    data = "текущие параметры";
    return info;
}
// -------------------------------- Окончание стандартного блока ------------------------------------------------

