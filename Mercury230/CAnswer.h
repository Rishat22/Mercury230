#ifndef CANSWER
#define CANSWER

#include <QThread>
#include <QtSql>
#include <QTcpServer>
#include <QTcpSocket>
#include <QLibrary>
#include "CControlData.h"
#include "CCommand.h"
#include "defs.h"
#include "global.h"
#include "General.h"

// Возможно данный класс не нужно переопределять
class Thread : public QThread
{
    Q_OBJECT
public:
    Thread(QObject* parent =NULL) : QThread(parent){}
    void run()
    {
        exec();
    }
    void quit()
    {
        this->QThread::quit();
        this->deleteLater();
    }
};

class CAnswer : public QObject
{
    Q_OBJECT
public:
    // 23.02.2017 Указатель на старый сокет
    QTcpSocket* OldSocket = NULL;
    // 06.02.2017 Флаг отправки пинга
    bool* pSendPing;
    // 01.12.2016 Флаг обработки команды
    bool* pCommandInWork;
    bool IsExiting;                                  // Флаг выхода из потока
    explicit CAnswer(void* p);
    void* par;                                       // Указатель на родителя
    quint32 ObjectID;                                // Идентификатор объекта
    QTcpSocket* pClientSocket =NULL;                 // Указатель на сокет
    // Данные системной БД
    QSqlDatabase SysDB;
    QString Host;
    QString Port;
    QString Login;
    QString Password;
    // Данные объектной БД
    bool InitObjectDB();                             // Инициализация и открытие БД
    QSqlDatabase ObjectDB;
    QString ObjHost;
    QString ObjPort;
    QString ObjDBName;
    // Данные даты и временм
    QMutex* pBusyDateTime;
    QDate* pDate;
    QTime* pTime;
    // Уплавляющие данные
    QMutex* pBusyControlData;
    CControlData* pControlData;
    QString GetEquipmentTableName(CCommand command); // Получение имени таблицы прибора в которой следует записать данные
    bool TestEqTables(CCommand command);             // Проверка наличия в БД объекта таблицы прибора
    bool GetNumberLastRow(CVarDB param,
                          CCommand command,
                          int& number,
                          bool& lastrow);            // Получение последнего номера записи
    // Таблица команд
    QMutex* pBusyCommandTable;
    QList<CCommand>* pCommandTable;
    bool GetProcessingCommand(CCommand* command);    // Получение команды со статусом PROCESSING из перечня команд
    bool SetProcessingCommand(QByteArray str);       // Запись полученных данных в таблицу команд
    // Данные для работы драйвера
    QByteArray* pDriverData;                         // Массив данных для обработки драйвером
    QMutex* pBusyDriverData;                         // Мьютекс массива
    // Функции обработки входных данных
    bool SaveTerminalN(QByteArray str,
                       CCommand command);            // Запись в TerminalCommand ответа на прямую терминальную команду
    bool SaveTerminalY(QByteArray str,
                       CCommand command);            // Запись в TerminalCommand ответа на ПЛК - терминальную команду
    void SaveDriver(bool NeedToRead = true,
                    QByteArray data ="");            // Запись данных из соединения в массив для обработки драйвером
    bool CVarDBEngine(QByteArray str,
                      CCommand command);             // Основная функция обработки данных
    QByteArray GetDataPLC(QByteArray data,
                          bool* IsOk,
                          quint8 &funct,
                          CCommand* command =NULL);  // Проверка длины, CRC, ID и т.д. при получении пакета от ПЛК
    bool TestPacketCRC16(QByteArray packet);         // Проверка CRC16 пакета данных
    bool SetStatus(QString status,
                   CCommand& command);               // Запись статуса в перечень команд
    bool SaveCode(CCommand& command, QString error); // Запись кода завершения ПЛК-команды в TCommandStat
    bool SetDateTime(bool Start, CCommand command);  // Запись даты и времени в команду
    bool UpdateCommand(QString status,
                       CCommand command);            // Запись статуса и времени в таблицу команд и текущую команду
    bool GetVarData(CVarDB param,
                    QByteArray& data,
                    CCommand command);               // Получение переменной из пакета
    bool InsertData(CVarDB param,
                    QByteArray& data,
                    CCommand command);               // Перестановка данных в массиве согласно данных настроек
    bool GetSensorVar(CVarDB param, QByteArray& data,
                 QString& Var, CCommand command);    // Преобразование бинарных данных в строку
    void ConvertToSaveType(CVarDB param,
                           QString& var);            // Преобразование полученного типа в указанный для записи в БД
    bool TestSensorRange(CVarDB param,
                         QByteArray& packet,
                         QString& Var,
                         QString& ConvertedVar,
                         CCommand command);          // Проверка диапазона датчика, контрольной суммы
    bool SaveParameter(CVarDB param,
                       QString& ConvertedVar,
                       CCommand command);            // Запись параметра в таблицу прибора
    bool SaveVar(CVarDB param, QString data,
                 CCommand command,
                 int number,
                 bool lastrow);                      // Запись данных в таблицу прибора
    bool IsAlarm(CVarDB param, QString ConvertedVar,
                 CCommand command,
                 bool FullTest = true,
                 QString AText = "");                // Проверка аварийного состояния параметра
    bool IsAlarmParameter(CVarDB param, quint64 var,
                     bool& IsAlarm,
                     QStringList& AlarmText);        // Функция проверки UINT параметров
    bool IsAlarmParameter(CVarDB param, qint64 var,
                    bool& IsAlarm,
                    QStringList& AlarmText);         // Функция проверки INT параметров
    bool IsAlarmParameter(CVarDB param, double var,
                    bool& IsAlarm,
                    QStringList& AlarmText);         // Функция проверки DOUBLE параметров
    bool IsAlarmParameter(CVarDB param, float var,
                    bool& IsAlarm,
                    QStringList& AlarmText);         // Функция проверки FLOAT параметров
    bool IsAlarmParameter(CVarDB param, QString var,
                    bool& IsAlarm,
                    QStringList& AlarmText);         // Функция проверки QSTRING параметров
    bool IsNotFlagSetted(QString ParamName,
                         bool& Flag);                // Функция проверки установки флага тревоги по переданному параметру
    bool FlagSkip(QString ParamName);                // Сброс флага тревоги
    bool SaveAlarm(QString ParamName,
                   QString ConvertedVar,
                   QStringList AlarmText);           // Запись тревоги в системную БД
    void CreateTAlarm(QSqlQuery* mQuery);            // Создание таблицы тревог
    void DeleteOldSocket();                          // Удалить старый сокет-дубликат из родительского потока
public slots:
    void GetSocket(QTcpSocket* sock,
                   QByteArray data, quint32 ID);     // Получение нового соединения потоком
    void DelSocket();                                // Уничтожить ненужный сокет
    void ReadAnswer(bool NeedToRead = true,
                    QByteArray data = "");           // Чтение данных из сокета, либо обработка уже полученных
    void DelThis();                                  // Удаление данного объекта
    void OnQuit(QObject* p);                         // Проверка удаленного объекта
    void sendToClient(QByteArray data, int timeout); // Отправка данных
signals:
// 23.02    void SendToThreadSocket(QByteArray);
    void ResetSilence();                             // Сброс счетчика молчания сокета
    void SendLastErrorDB(QString);                   // Отправка ошибки
    void SendDeleteTextLastErrorDB(QString);         // Удаление ошибки
    void Stop();                                     // Сигнал на остановку потока
    void Print(QString, bool);                       // Сигнал печати
    void SetObjectStatus(QString, int);              // Запись статуса в БД через главный поток
};

#endif // CANSWER

