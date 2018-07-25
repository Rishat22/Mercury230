// Поток отправки данных команд объекту
#ifndef CDISPATCHTHREAD
#define CDISPATCHTHREAD

#include <QThread>
#include <QtSql>
#include <QTcpServer>
#include <QTcpSocket>
#include <QLibrary>
#include "CControlData.h"
#include "CCommand.h"
#include "defs.h"

// Задержка при отправке
class CDispatchThread : public QThread
{
    Q_OBJECT
public:
    // 14.06.2017 Флаг перезаписи драйвера
    bool DllFlag;
    // 05.06.2017 Данные для драйвера
    QMutex BusyDriverData;
    QString DriverObjectName;
    quint32 DriverObjectID;
    QString DriverLogin;
    QString DriverPassword;
    // 01.03.2017 Указатель на родителя
    void* par;
    // 06.02.2017 Флаг отправки пинга
    bool* pSendPing;
    // 06.02.2017 Частота отправки пингов
    quint32 PingTimeOut;
    // 01.12.2016 Флаг обработки команды
    bool* pCommandInWork;
    bool* pIsConnected;
    // 20.11.2016 Данные системной БД
    QString port, host, login, password;
    QSqlDatabase db;
    QString ObjectName;
    //
    QList<CCommand>* pCommandTable;                      // Таблица команд родительского потока
    CCommand CurrentCommand;                             // Текущая команда
    QMutex BusyCurrentCommand;                           // Мьютекс текущей команды
    QByteArray Packet;                                   // Отформатированный массив для отправки
    QMutex BusyPacket;                                   // Мьютекс отформатированного массива для отправки
    CControlData* pControlData;                          // Объект управляющих данных
    QMutex* pBusyControlData;                            // Мьютекс управляющих данных
    QMutex* pBusyCommandTable;                           // Мьютекс таблицы команд родительского потока
    bool ThreadDelete;                                   // Флаг остановки потока из передается при остановке CListenThread
    quint32 ObjectID;                                    // Идентификатор объекта
    // Данные работы драйвера
    QLibrary* lib;                                       // Указатель на драйвер
    QByteArray* pDriverData;                             // Указатель на данные для обработки драйвером
    QMutex* pBusyDriverData;                             // Мьютекс данных
    //
    // Данные даты и времени (необходимо для записи времени отправки)
    QMutex* pBusyDateTime;
    QDate* pDate;
    QTime* pTime;
    //
    explicit CDispatchThread(QObject* parent, QString DObjectName, quint32 PTimeOut);
    bool OnWork();                                       // Разрешает выполнение функций отправки или производит задержку
    bool GetCommandFromTable(CCommand* command);         // Получение команды по индексу и приоритету из таблицы команд
    void GetPacket();                                    // Подготовка пакета для отправки оборудованию через ПЛК
    bool OpenDLL();                                      // Открытие драйвера прибора
    bool UpdateCommand(QString status);                  // Запись статуса и времени в таблицу команд и текущую команду
    bool SetStatus(QString status);                      // Установка статуса в перечне команд и в выбранной текущей команде
    bool GetCurrentStatus(QString* status);              // Получение текущего статуса из перечня команд
    bool SetDateTime(bool Start= true);                  // Запись в таблицу команд времени отправки и времени окончания
    bool CreateCommandStatusTable();                     // Создание и проверка наличия таблицы статусов команд
    bool SaveCode(CCommand& command, QString error);     // Запись кода завершения ПЛК-команды в TCommandStat
    void run();
    // Подготовка пакета пинга
    QByteArray ControlPing();                            // При получении пинга от ПЛК сервер отправляет ответный пинг
    bool CopyFiles(QString from, QString to, bool dllflag);
signals:
//    void SendCopyFile(QString, QString, QString, bool);  // Сигнал копирования файлов
    void Print(QString, bool);                           // Сигнал печати
    void SendLastErrorDB(QString);                       // Отправка ошибки
    void SendDeleteTextLastErrorDB(QString);             // Удаление ошибки
    void SendDataToClient(QByteArray, int);              // Отправка данных с временем задержки
    void onDelete();
public slots:
//    void sendToClientData(QByteArray data, int timeout); // Отправка данных
};

#endif // CDISPATCHTHREAD

