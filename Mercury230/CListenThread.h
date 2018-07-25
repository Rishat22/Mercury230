#ifndef CLISTENTHREAD_H
#define CLISTENTHREAD_H
#include <QThread>
#include <QtSql>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTextEdit>
#include "CCommand.h"
#include "CControlData.h"
#include "CDispatchThread.h"
#include "CAnswer.h"

// Класс телефонов для установки в модем
class CPhone
{
public:
    // Очистка данных при создании
    CPhone()
    {
        for(int i =0; i < 10; i++)
        {
            SetSMS[i] =0;
            Phone[i]  ="";
            Login[i]  ="";
        }
        Records =0;
    }
    // Номера телефонов операторов, и флаги установки
    int SetSMS[10];
    QString Phone[10];
    QString Login[10];
    int Records;
    // Очистка данных по требованию
    void ClearData()
    {
        for(int i =0; i < 10; i++)
        {
            SetSMS[i] =0;
            Phone[i]  ="";
            Login[i]  ="";
        }
        Records =0;
    }
};

// Класс последнего состояния параметра
class CParam
{
public:
    QDateTime DateTime;   // Время получения параметра
    QString ParamName;    // Наименование параметра (например AI1)
    unsigned int IdObject;// Идентификатор объекта
    float ParamCurrent;   // Значение параметра
    float AlarmMIN, AlarmMAX, SensorMIN, SensorMAX, ParamMIN, ParamMAX;
    quint8 IsAlarm;       // Нужно ли выводить сообщение (0 - нет,1 - да, 2 - не известно)
    quint8 AlarmFlag;     // Флаг аварии (0 - нет,1 - да, 2 - не известно)
};

class CListenThread : public QThread
{
    Q_OBJECT
public:
    // 06.02.2017
    bool SendPing;
    // 01.12.2016
    bool CommandInWork;
    // 29.11.2016 Флаг запуска терминала, чтобы не очищать все время перечень команд
    bool TerminalStarted;
    // 28.11.2016 Поток приема данных
    Thread* thread;
    CAnswer* answer;
    CDispatchThread *DispatchThread;
    bool IsConnected;                       // Флаг устанавливается при установке соединения и сбрасывается при потере
    // Флаг разрешения удаления потока
    bool ThreadDelete;
    // Флаги записи данных в массивы для обработки потоком
    QMutex BusyCRC;
    QMutex ATMutex;
    QMutex ClearMutex;       // Функция очистки данных
    QMutex IsReset;          // Флаг проверки сброса модема
    QMutex MTimeOut;         // Флаг занятости счетчика - таймаута
    // Данные времени и даты
    QMutex* DateTime;
    QTime* mTime;
    QDate* mDate;
    // Строки необходимые для подключения к БД
    QMutex BusySysDB;
    QSqlDatabase SysDB;
    QMutex PortHost;
    QString Host;
    QString Port;
    QString Login;
    QString Password;
    QString DBLastError;
    QMutex DBLastErrorMutex;
    // Объектная БД
    QMutex BusyObjectDB;
    QSqlDatabase ObjectDB;
    bool FlagODB;                       // Флаг подключения объектной БД
    // Массивы входных и выходных данных
    QMutex BusyFlag_in;
    QMutex BusyFlag_out;
    QByteArray Data_in;                 // Входные данные/запрос
    QByteArray Data_out;                // Выходная команда
    // Данные управления потоком
    CControlData* ControlData;
    QMutex BusyControlData;
    // 15.09.2016 Таблица команд (поток отправки команд имеет доступ к данному объекту)
    QList<CCommand> CommandTable;
    QMutex BusyCommandTable;
    bool GetCommands(quint32 ID);        // Получение команд в таблицу из данных объекта
    bool GetTerminalCommands(quint32 ID);// Получение терминальных команд в таблицу из данных объекта
    bool CreateCommandStatusTable();     // Создание таблицы статусов команд

    // 06.10.2016 Объектные данные приборов
    // Открытие объектной базы данных
    bool OpenObjectDB();                // Открытие БД данного объекта
    QString GetEquipmentTableName(
            CCommand command);          // Получение строки с именем таблицы прибора указанного в команде
    void TestEqTables(QList<CCommand>* NewCommandTable); // Поиск таблиц приборов указанных в команде
    // Проверка настроек переменных указанных в командах по следующему алгоритму:
    //       - если COMMAND - изменение настроек переменной, либо создание новой
    //       - если WRITE - поиск переменной в таблице прибора и ее настроек, перезапись в таблице команд, если не найдена либо некорректна, то удаление команды
    //       - если CREATE - поиск переменной в таблице прибора и ее настроек, перезапись в таблице команд, если не найдена либо некорректна, то создание новой переменной и ее настроек
    //       - если READ - ничего не проиводится т.к. переменная не записывается
    void TestEqVariables(QList<CCommand>* NewCommandTable);

    QMutex BusyObject;
    quint32 ObjectID;                   // Идентификатор объекта для сверки с БД и для вставки в команды
    // Сокет и данные для его работы
// ИСПРАВИТЬ с данном классе сокет не нужен
    QMutex BusySocket;
    QTcpSocket* pClientSocket;          // Сокет для работы
    // Данные для работы через драйвер
    QByteArray DriverData;              // Массив данных для обработки драйвером
    QMutex BusyDriverData;              // Мьютекс массива
    //
    QString ObjectName;                 // Имя сокета и объекта
    int TimerId;                        // Идентификатор текущего таймера
    int TimerCommand;                   // Идентификатор таймера команд
    unsigned long TimeWait;             // Время прекращения опроса сервером (таймаут оборудования)
    QByteArray AtData;                  // Запись АТ-команды, либо ответа
    int AtStatus;                       // Статус AT-команды
    bool IsModemReset;                  // Флаг сброса модема выставляется функцией GetAT
    QByteArray SMSHead;                 // Первая команда СМС (заголовок)
    QByteArray SMSData;                 // Данные СМС и конечный символ
    int IsSmsGet;                       // Статус СМС
    int SilenceOfSocket;                // Молчание объекта
    quint32 PingTimeOut;                // Время ожидания пинга (по умолчанию 30000 мс)
// 18.11.2015 Текущие актуальные параметры
    QList<CParam> mParams;
//

    CListenThread(QObject* parent, QString objName, QString host, QString port,
                  QString login, QString password, quint32 ID, quint32 PTimeOut);
    QByteArray* ClearAT(QByteArray *data); // Удаление символов "OK", '\R', '\N' из посылки
    bool GetAT(QByteArray *data);       // Проверка AT-команды
    void SmsCommand(QByteArray *str);   // Обработка команд СМС полученных от ПЛК (в связи с недоработкой в Fargo Maestro, смс не будут отправлены)
    QByteArray ControlPing();           // Пакет пинга
    void run();                         // Работа с данными БД и подготовкой команд
    void PlcSave(QByteArray* GetsData, QSqlDatabase* db);                              // Обработчик ответов
    void PlcData(QSqlDatabase* db);                                                    // Обработка поступивших данных
    void PlcCommand(QSqlDatabase* db, int* Number, unsigned long *CommandTimer);       // Чтение комманд ПЛК из БД и отправка
    bool GetPhone(QSqlDatabase *db, CPhone* phone);                                    // Получение данных текущих операторов
// Изменение от 29.12.2015 обработка данных баланса теперь на сервере
    void GetBalance(QByteArray* data);  // Получение баланса
// Изменение от 18.11.2015 Обработка ответов перенесена на сервер
    bool SetCurrentData(QSqlDatabase*db, QDate date, QTime time, quint8 Funct, quint32 ObjectID,
                     quint32 CommandID, QByteArray PlcData, quint8 ErrorCode);         // Обработка ответов
    bool SetObjectDataCurrent(QSqlDatabase *db, int ObjectID);                         // Создание таблицы текущих параметров
    bool SetObjectDataTable(QSqlDatabase *db, int ObjectID);                           // Создание таблицы архивных параметров
    bool SetSetupDataTable(QSqlDatabase *db, int ObjectID);                            // Таблица настройки параметров
    bool IsAlarm(QSqlDatabase *db, unsigned int ObjectID, float CurrentParam,
                 QString NameParam,QDate date, QTime time);                            // Проверка и запуск тревоги
    bool IsNotFlagSetted(QSqlDatabase *db, QString NameParam, unsigned int ObjectID);  // Проверка наличия флага данного сообщения
    bool FlagSkip(QSqlDatabase* db, QString NameParam,
                  unsigned int ObjectID, QDate DateMessage,
                  QTime TimeMessage);                                                  // Сброс флага тревоги, если он установлен

    QString GetLastErrorDB();                                                          // Получение перечня ошибок
    bool SetObjectStatus(QSqlDatabase* db,QString status, int column);                 // Запись статусов
    bool UpdateControlData();                                                          // Обновление управляющих данных потока
    bool CommandHandler();                                                             // Обработчик отработанных команд
    bool ClearCommandsTable();                                                         // Очистка перечня команд для отправки, кроме той что выполняется в данный момент
    bool DeleteCommand(QSqlQuery* mQuery, CCommand command);                           // Удаление команды из TCommand
    bool DecCommand(QSqlQuery* mQuery, CCommand command);                              // Декремент счетчика команды в TCommand
    bool CreateErrorRow(QSqlQuery* mQuery, CCommand command,
                        quint32 GeneralCount, quint32 ErrorCount);                     // Создание записи в таблице ошибок
    bool SetTerminalError(QSqlQuery* mQuery, CCommand command);                        // Запись в TObject TerminalCommand данных об ошибке для клиента
//    bool GetActualParam(QSqlDatabase* db, CParam* parameter);                          // Создание списка актуальных параметров
//    quint32 GetMessageDeep(QSqlDatabase* db);                                          // Получение глубины вывода сообщений
//
public slots:
    void SendDataFromDLL(QByteArray data,
                         int delay);                 // Отправка данных из DLL объекту
    void ResetSilenceOfSocket();                     // Сброс счетчика молчания
    void SetObjectStatusSlot(QString status,
                             int column);            // Запись статуса через главный поток
    void SetLastErrorDB(QString ErrorText);          // Запись ошибки запроса к БД
    void DeleteTextLastErrorDB(QString ErrorText);   // Удаление текста ошибки из строки ошибок
    void deleteThread(quint32 IDObject =0);          // Уничтожить поток
    void slotReadClient();
    void PrintString(QString string, bool IsMain);   // Вывод данных в текстовое окно основного потока
// ИСПРАВИТЬ данная функция не нужна
    void ATCommand(QByteArray DataSend);             // Обработка AT-команд
protected:
    virtual void timerEvent(QTimerEvent *event);
signals:
    void SendDataToClient(QByteArray, int);    // Отправка данных с временем задержки
    void StopAnswer();                         // Остановить поток получения данных, разорвать соединение
    void SendStop();                           // Отправка сигнала о том что поток завершился
    void SendLastErrorDB(QString);             // Отправка ошибки
    void SendDeleteTextLastErrorDB(QString);   // Удаление ошибки
    void DeleteSocket();                       // Удаление ненужного сокета
    void Wait(int);                            // Задержка
    void OnDelete(quint32);                    // На удаление потока
    void Print(QString, bool);                 // Отправка сигнала со строкой
    void PrintToMain(QString, bool, QString);  // Отправка сигнала в основное окно
    void SendData(QByteArray, int);            // Отправка данных с временем задержки
    void ATSignal(QByteArray);                 // Отправка сигнала передачи данных АТ-команды
};


#endif // CLISTENTHREAD_H
