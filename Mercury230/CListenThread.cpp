#include "CListenThread.h"
#include <QMessageBox>
#include "mainwindow.h"
#include "CheckSum.h"
#include "General.h"
#include "global.h"
#include "cdispatchthread.h"

extern QString DBName;
// 01.09.2016 Из конструктора убрано получение сокета. Сокет теперь передается сигналом из главного потока
// 01.09.2016 Добавлено получение имени объекта (имя объекта и имя сокета который будет получен, совпадут)
// 01.09.2016 Убран порядковый номер (счетчик) объекта, соединение с БД будет именоваться именем объекта
// 01.09.2016 Идентификатор объекта получается теперь сразу после создания потока, проверка соединения производится в основном потоке
// 01.09.2016 Добавлена передача логина и пароля в конструктор для отвязки от статического логина "root"
// 01.09.2016 Убрано соединение сигналов сокета со слотами обработки. Теперь установка происходит при получении сокета
// 02.09.2016 Соединение сигнала удаления потока убрано, теперь сигнал поступает в основное окно, где производится
//            удаление объекта из дерева, декремент в перечне, вкладки, а затем посылается сигнал удаления в поток
// 06.02.2016 В конструктор передается частота пинга PingTimeOut которая используется для проверки молчания объекта
CListenThread::CListenThread(QObject* parent, QString objName, QString host,
                             QString port, QString login, QString password, quint32 ID,
                             quint32 PTimeOut): QThread(parent)
{
    // Флаг необходимости отправки пинга
    SendPing = false;
    // Частота пинга
    PingTimeOut = PTimeOut;
    // Флаг обработки команды
    CommandInWork =false;
    // Данные даты и времени не блокируется т.к. объект создается в основном потоке
    DateTime = &(((MainWindow*)parent)->MDateTime);
    mTime = &(((MainWindow*)parent)->mTime);        // Запись указателя на объект времени
    mDate = &(((MainWindow*)parent)->mDate);        // Запись указателя на объект даты
    // Флаг запуска терминала, чтобы не очищать все время перечень команд
    TerminalStarted = false;
    // Флаг соединения
    IsConnected = false;  // При отсутствии флага, команды не будут отправляться
    DriverData.clear();
    FlagODB = false;
    // Строки необходимые для подключения к БД
    PortHost.lock();
    Host = host;
    Port = port;
    Login = login;
    Password = password;
    PortHost.unlock();
    DBLastError = "";
    // Флаг разрешения удаления потока
    ThreadDelete = false;
    // Данные управления потоком
    ControlData = new CControlData();//(this);
    // Очистка массивов данных
    Data_in.clear();
    Data_out.clear();
    ObjectID = ID;
    AtStatus = 0;
    IsModemReset =false;
    IsSmsGet = 0;
    SilenceOfSocket = 0;
    TimeWait =0;
    pClientSocket = NULL;
    ObjectName = objName;
    TimerId = startTimer(PingTimeOut);// Таймер проверки времени молчания (после 3-х интервалов тишины удаляется соединение)
    TimerCommand = startTimer(100);   // Таймер отправки команд объекту
    connect(this, SIGNAL(Print(QString, bool)), this, SLOT(PrintString(QString, bool)), Qt::QueuedConnection);
    connect(this, SIGNAL(ATSignal(QByteArray)), this, SLOT(ATCommand(QByteArray)), Qt::QueuedConnection);
    connect(this, SIGNAL(SendLastErrorDB(QString)), this, SLOT(SetLastErrorDB(QString)), Qt::QueuedConnection);
    connect(this, SIGNAL(SendDeleteTextLastErrorDB(QString)), this, SLOT(DeleteTextLastErrorDB(QString)), Qt::QueuedConnection);
    // Поток для работы с получаемыми данными
    //28.11.2016 ТЕСТ
    thread = new Thread(0);
    answer = new CAnswer(this);
    answer->moveToThread(thread);
    connect(this, SIGNAL(StopAnswer()), answer, SLOT(DelThis()), Qt::QueuedConnection);
    connect(answer, SIGNAL(Stop()), thread, SLOT(quit()), Qt::QueuedConnection);
    connect(this, SIGNAL(DeleteSocket()), answer, SLOT(DelSocket()), Qt::QueuedConnection);
    connect(answer, SIGNAL(Print(QString,bool)), this, SLOT(PrintString(QString,bool)), Qt::QueuedConnection);
    connect(answer, SIGNAL(SetObjectStatus(QString,int)), this, SLOT(SetObjectStatusSlot(QString,int)), Qt::QueuedConnection);
    connect(answer, SIGNAL(SendLastErrorDB(QString)), this, SLOT(SetLastErrorDB(QString)), Qt::QueuedConnection);
    connect(answer, SIGNAL(SendDeleteTextLastErrorDB(QString)), this, SLOT(DeleteTextLastErrorDB(QString)), Qt::QueuedConnection);
    connect(answer, SIGNAL(ResetSilence()), this, SLOT(ResetSilenceOfSocket()), Qt::QueuedConnection);
    connect(this, SIGNAL(SendDataToClient(QByteArray,int)), answer, SLOT(sendToClient(QByteArray,int)), Qt::QueuedConnection);
    // Передача объекта в поток, чтобы не блокировать события
    thread->moveToThread(thread);
    thread->start();
    //28.11.2016 ТЕСТ
}
// Функция потока (Менеджер команд)
// 01.09.2016 Открытие соединения в потоке с БД добавлены логин и пароль, полученные от главного потока
// 01.09.2016 Thread + objectNumber убрано из названия соединения, название теперь по имени объекта ObjectName
// 01.09.2016 Открытие БД перенесено в цикл на случай если БД не открывается, тогда будет циклически пытаться с ней связаться
// 01.09.2016 При ошибке связи с БД поток больше не удаляется, а будет циклически пытаться установить связь
// 01.09.2016 При удачном соединении с БД записывается флаг работы потока сразу т.к. теперь поток работает независимо от
//            наличия соединения с объектом
// 11.09.2016 Задержка обновления данных сделана перед исполнением команд потока и разбита на части для быстрого удаления потока
// 11.09.2016 Запись статуса объекта производится циклически при каждом обновлении данных
// 25.11.2016 Системная БД в родительском потоке сделана общей с защитой мьютексом для асинхронного доступа
void CListenThread::run()
{
// ИСПРАВИТЬ данные 2 строки имеют отношение к старым функциям опроса
//    int CommandNumber =0;
//    unsigned long CommandTimer = 10000;
    PortHost.lock();
    SysDB = QSqlDatabase::addDatabase("QMYSQL", ObjectName);   // Сборка по инструкции
    SysDB.setDatabaseName(DBName);                                      // Постоянное имя БД
    SysDB.setUserName(Login);                                           // Работа с правами разработчика обязательна
    SysDB.setHostName(Host);                                            // Адрес или имя хоста из текстового поля
    SysDB.setPort(Port.toInt());                                        // Порт из текстовго поля
    SysDB.setPassword(Password);                                        // Пароль сервера
    PortHost.unlock();
    int BadCount =0;                                                    // Счетчик неудачных соединений с БД
    BusyObject.lock();
    quint32 ID = ObjectID;
    BusyObject.unlock();
    // Создание потока отправки данных
    // 07.11.2016
    // 14.11.2016 В поток данные передаются по указателю на объект родитель
    DispatchThread = new CDispatchThread(this, "D" + ObjectName, PingTimeOut);
    connect(DispatchThread, SIGNAL(Print(QString,bool)), this, SLOT(PrintString(QString,bool)), Qt::QueuedConnection);
    connect(DispatchThread, SIGNAL(SendLastErrorDB(QString)), this, SLOT(SetLastErrorDB(QString)), Qt::QueuedConnection);
    connect(DispatchThread, SIGNAL(SendDeleteTextLastErrorDB(QString)), this, SLOT(DeleteTextLastErrorDB(QString)), Qt::QueuedConnection);
    connect(DispatchThread, SIGNAL(SendDataToClient(QByteArray,int)), answer, SLOT(sendToClient(QByteArray,int)), Qt::QueuedConnection);
  //  connect(DispatchThread, SIGNAL(SendCopyFile(QString,QString,QString,bool)),
  //         ((MainWindow*)this->parent()), SLOT(CopyFiles(QString,QString,QString,bool)), Qt::QueuedConnection);
    DispatchThread->start();

    while(!ThreadDelete)
    {
        // Задержка обновления данных (при старте потока 10сек), в дальнейшем по данным из БД
        quint32 minidelay =0, delay =0;
        BusyControlData.lock();
        delay = ControlData->ObjectDelay;
        BusyControlData.unlock();
        while(minidelay < (delay))
        {
            msleep(100);
            minidelay +=100;
            // Если происходит остановка потока, тогда сделует выйти сразу
            if(ThreadDelete)break;
        }
        // Если БД будет открыта, а потом произойдет сбой соединения, то данная проверка не будет проходить т.к. isOpen()
        // всегда возвращает положительный результат после открытия БД, даже если соединение пропало
        if((!SysDB.isOpen())|(!GetLastErrorDB().isEmpty()))   // База данных открыта? Если нет, то попытка открыть
        {
            // В случае если БД не открыта
            if(!SysDB.open())
            {
                // Вывод сообщения об ошибке открытия БД производится 1 раз
                if(BadCount <1)
                {
                    // БД не открыта, отправка сигнала на удаление
                    QString ErrorString = " > Открыть соединение потоку №" +QString::number(ID) +" с базой данных не удалось\r\n";
                    emit Print(ErrorString, false);
                    emit SendLastErrorDB("ОШИБКА ОТКРЫТИЯ СИСТЕМНОЙ БД");
                    BadCount++;  // Увеличение счетчика для блокировки сообщений
                }
                continue;        // Попытаться еще раз
            }
            // Убрать ошибки которые могут быть
            emit SendDeleteTextLastErrorDB("ОШИБКА ОТКРЫТИЯ СИСТЕМНОЙ БД");
        }
        // Запись статуса объекта
        if(!SetObjectStatus(&SysDB, "ONLINE" + GetLastErrorDB(), 2))
            continue;
        QString DataPrint = " > Запись статуса соединения с БД №" + QString::number(ID) + " произведена\r\n";
        emit Print(DataPrint, false);
        BadCount =0; // Обнуление счетчика для разблокировки
        // Обновление управляющих данных объекта
        UpdateControlData();
        // Обработчик отработанных команд
        CommandHandler();
// ИСПРАВИТЬ обработка полученных данных должна реализовываться по сигналу сокета а не в цикле
//        PlcData(&db);
// ИСПРАВИТЬ отправку команд перенести в отдельный поток - менеджер
//        PlcCommand(&db, &CommandNumber, &CommandTimer);
    }
    // Остановка потока получения
    emit StopAnswer();
    thread->wait();
    delete thread;
    // Остановка потока отправки
    DispatchThread->ThreadDelete = true;
    DispatchThread->wait();
    delete DispatchThread;
    // Закрытие соединения с объектной БД
    if(FlagODB)
    {
        {
            ObjectDB.close();
        }
        QSqlDatabase::removeDatabase("Equipment" + QString::number(ID));
    }
    // Закрытие соединения с системной БД
    SetObjectStatus(&SysDB, "OFFLINE", 2);
    {
        SysDB.close();
    }
    QSqlDatabase::removeDatabase(ObjectName);
    delete ControlData;
    return;
}

// Не протестировано

// Получение нового соединения потоком
// При получении сокета проверяется есть ли уже сокет в данном потоке и если есть он удаляется
// новый сокет присваивается потоку и устанавливаются сигнально-слотовые соединения
// 02.09.2016 Сигнал от сокета на дисконнект теперь не удаляет поток, а удаляет только сам сокет
// 25.11.2016 Функция работает с системной БД данного потока через мьютекс
// 28.11.2016 Внимание данная функция работает в родительском потоке
/*void CListenThread::GetSocket(QTcpSocket* sock, QByteArray data, quint32 ID)
{
    // 1. Проверка какому объекту предназначается сокет
    BusyObject.lock();
    // Если не данному объекту то на выход
    if(ObjectID!=ID)
    {
        BusyObject.unlock();
        return;
    }
    BusyObject.unlock();
    // 2. Установка сокета
    BusySocket.lock();
    // Если сокет уже есть
    if(pClientSocket!=NULL)
    {
        // Производится отключение сигналов
        disconnect(pClientSocket, SIGNAL(disconnected()), this, SLOT(DelSocket()));
        disconnect(pClientSocket, SIGNAL(readyRead()), this, SLOT(ReadAnswer()));
        // Удаление отключенного сокета тогда, когда это будет возможным
        pClientSocket->waitForBytesWritten(); // Если еще идет запись, будет ожидание пока все данные не уйдут
        pClientSocket->deleteLater();
    }
    // Устновка нового сокета в данном потоке
    pClientSocket = sock;
    connect(pClientSocket, SIGNAL(disconnected()), this, SLOT(DelSocket()), Qt::QueuedConnection);
    connect(pClientSocket, SIGNAL(readyRead()), this, SLOT(ReadAnswer()), Qt::QueuedConnection);
    BusySocket.unlock();
    // 3. Запись пакета пинга в обработку
    BusyFlag_in.lock();  // До тех пор пока не освободиться массив
    // Запись массива для обработки потоком
    Data_in.clear();     // Очистка
    Data_in = data;      // Запись
    BusyFlag_in.unlock();// Освобождение данных
    // 27.11.2016 Доступ к БД через мьютекс
    ((MainWindow*)this->parent())->BusyDB.lock();
    QSqlDatabase db = QSqlDatabase::database("connection");
    SetObjectStatus(&db, "ONLINE", 1);
    ((MainWindow*)this->parent())->BusyDB.unlock();
    //
    QString DataPrint = " > Получено новое соединение с объектом\r\n";
    emit Print(DataPrint, false);
}
// 02.09.2016 Удаление ненужного сокета
// 28.11.2016 Внимание данная функция вызывается в родительском потоке
void CListenThread::DelSocket()
{
    BusySocket.lock();
    if(pClientSocket!=NULL)
    {
        // Производится отключение сигналов
        disconnect(pClientSocket, SIGNAL(disconnected()), this, SLOT(DelSocket()));
        disconnect(pClientSocket, SIGNAL(readyRead()), this, SLOT(ReadAnswer()));
        // Удаление отключенного сокета тогда, когда это будет возможным
        pClientSocket->waitForBytesWritten(); // Если еще идет запись, будет ожидание пока все данные не уйдут
        pClientSocket->deleteLater();
    }
    pClientSocket =NULL; // Отвязать указатель обязательно!
    BusySocket.unlock();
    // 27.11.2016 Доступ к БД через мьютекс
    ((MainWindow*)this->parent())->BusyDB.lock();
    QSqlDatabase db = QSqlDatabase::database("connection");
    SetObjectStatus(&db, "OFFLINE", 1);
    ((MainWindow*)this->parent())->BusyDB.unlock();
    //
    QString DataPrint = " > Соединение с объектом потеряно. Превышен таймаут ожидания.\r\n";
    emit Print(DataPrint, false);
}
*/
// Чтение данных от клиента
void CListenThread::slotReadClient()
{
    // Массив для получения сообщений
    QByteArray str;
    for (;;)
    {
        // Начинается если массив пуст
        if (str.isEmpty())
        {
            // Пока сокет занят ничего читать нельзя
            BusySocket.lock();
            // Заголовок стандартного запроса либо ответа от контроллера
            if (pClientSocket->bytesAvailable() < 2)
            {
                BusySocket.unlock();
                break;
            }
            // Получение данных
            str = pClientSocket->readAll();
            BusySocket.unlock();
            // Сброс счетчика молчания при поступлении данных
            SilenceOfSocket =0;
        }
        // Получены данные СМС - переход на обработку СМС
        if(IsSmsGet)
        {
            SmsCommand(&str);
            return;
        }
        // Если есть флаг обработки АТ-команды
        ATMutex.lock();
        if(AtStatus)
        {
            ATMutex.unlock();
            emit ATSignal(str);
            return;
        }
        // Проверка AT-команды
        ATMutex.unlock();
        if(GetAT(&str))
        {
            if(IsSmsGet ==1)
            {
                qDebug() << "Получена AT-команда передчи СМС сообщения, ожидается тело сообщения";
                SmsCommand(&str); // Обработка поступишей команды СМС
                return;            // На выход
            }
            emit ATSignal(str);
            return;
        }

        // Получение длины пакета данных
        unsigned long DataLenght =0;

        DataLenght += (((unsigned long)(*(str.data() + 14))) << 24);
        DataLenght += (((unsigned long)(*(str.data() + 15))) << 16);
        DataLenght += (((unsigned long)(*(str.data() + 16))) << 8);
        DataLenght += ((unsigned long)(*(str.data() + 17)));
// Либо полученные данные ответ на АТ-команду либо данные "мусор" может просто вывод модема
// Доработать сборку пакета из нескольких сообщений
        if((unsigned long)str.length() != (14 + 4 + DataLenght + 2))
        {
            ClearMutex.lock();
            str = *(ClearAT(&str)); // Очистка от лишних знаков
            ClearMutex.unlock();
            if((str.isEmpty())|(str.length() <=1))
            {
                qDebug() << "Полученная информация транслироваться не будет";
                break;
            }
            qDebug() << "Полученные данные опоздавший ответ на АТ-команду либо данные \"мусор\", либо вывод модема";
            str.prepend('<');
            str.append('>');
            QString DataPrint = " Транзит: <" + str + ">\r\n";
            emit Print(DataPrint, false);
            emit SendData(str, 1);
            break;
        }
        // Проверка CRC16 полученного сообщения
        unsigned char KS_H, KS_L;
        // Возможно 2-е вхождение в функцию установка флага занятости
        BusyCRC.lock();
        GetCRC16((unsigned char *)(str.data()), &KS_H, &KS_L,(unsigned long) (str.length()));
        BusyCRC.unlock();
        if((KS_H !=((unsigned char) *(str.data() + (str.length() - 2))))|
                (KS_L !=((unsigned char) *(str.data() + (str.length() - 1)))))
        {
            qDebug() << "CRC16 не совпали, ошибка полученного сообщения";
            break;
        }
        // Вывод в окно объекта сообщения с данными полученными от объекта
        QString DataPrint = " >" + str.toHex() + "\r\n"; 
        emit Print(DataPrint, false);
        BusyFlag_in.lock();  // До тех пор пока не освободиться массив
        // Запись массива для обработки потоком
        Data_in.clear();     // Очистка
        Data_in = str;       // Запись
        BusyFlag_in.unlock();// Освобождение данных
        break;
    }
}

// 02.09.2016 Удаление вкладки и декремент счетчика объектов производится основным окном при получении команды остановки данного потока
//            и после отправки сигнала уничтожения потока сюда
// 28.11.2016 Внимание данная функция работает в родительском потоке
void CListenThread::deleteThread(quint32 IDObject)
{
    // 1. Проверка этому ли объекту пришел сигнал или сигнал для всех сразу
    BusyObject.lock();
    quint32 ID = ObjectID;
    BusyObject.unlock();
    if((ID!=IDObject)&(IDObject!=0))return;
    // 2. Запись статуса
    // 27.11.2016 Доступ к БД через мьютекс
    ((MainWindow*)this->parent())->BusyDB.lock();
    QSqlDatabase db = QSqlDatabase::database("connection");
    SetObjectStatus(&db, "OFFLINE", 1);
    ((MainWindow*)this->parent())->BusyDB.unlock();
    //
    // 3. Удаление потока
    qDebug() << "Удаление потока объекта:" + QString::number(ID);
    // Удаление потока команд (поток отправки будет удален при выходе из потока команд)
    ThreadDelete = true; // Флаг остановки потока
    this->wait();

    //BusySocket.lock();
    //if(pClientSocket!=NULL)
    //{
    //    pClientSocket->waitForBytesWritten(); // Если еще идет запись, будет ожидание пока все данные не уйдут
    //    pClientSocket->deleteLater();
    //}
    //BusySocket.unlock();
    killTimer(TimerId);
    killTimer(TimerCommand);
    emit SendStop();
    delete this;
}
// Проверка 10с
void CListenThread::timerEvent(QTimerEvent *event)
{
    // Какой из таймеров
    int IdTimer = event->timerId();
    if(IdTimer == TimerCommand)
    {
        MTimeOut.lock();
        TimeWait +=100;
        MTimeOut.unlock();
    }
    if(IdTimer != TimerId)return;
    // Проверка соединения, при первом вхождении, возможно в любой момент времени после обнуления счетчика
    if(SilenceOfSocket ==0)
    {
        SilenceOfSocket ++;
        return;
    }
    // Если флаг обработки AT-команды или СМС, производится отправка смены режима по таймауту
    ATMutex.lock();
    if(AtStatus!=0)
    {
        qDebug() << "Нет ответа по АТ-команде, сброс ожидания ответа";
        QByteArray a("ATO\r"); // Для перехода в режим соединения
        IsReset.lock();
        IsModemReset = false;  // Возможно при не обработанной команде сбросить соединение по флагу
        IsReset.unlock();
        emit SendData(a, 2);
        AtStatus =0;
        SilenceOfSocket ++;
        ATMutex.unlock();
        return;
    }
    ATMutex.unlock();
    if(IsSmsGet!=0)
    {
        qDebug() << "Нет ответа по отправке СМС - сброс отправки";
        QByteArray a; // Для оправки СМС, если не было.
        a.append('\x1a');
        emit SendData(a, 2);
        a.clear();
        a.append("ATO\r");
        emit SendData(a, 2);
        IsSmsGet =0;
        SilenceOfSocket ++;
        return;
    }
    // Счетчик
    if(SilenceOfSocket < 3)
    {
        SilenceOfSocket ++;
    }
    else
    {
        SilenceOfSocket =0;
        emit DeleteSocket();
    }
}

// Проверка AT-команды
bool CListenThread::GetAT(QByteArray *data)
{
    // Опасный транзит
    QByteArray CommandMode_1("+++");
    QByteArray CommandMode_2("ATO");
    // Ожидание освобождения идентификатора
    BusyObject.lock();
    if(ObjectID ==0)
    {
        QString DataPrint = " > ID данного объекта не подтвержден, данные AT-команд не обрабатываются\r\n";
        emit Print(DataPrint, false);
        // Если идет транзит "+++", следует его очистить, чтобы не засорять эфир
        if((data->indexOf(CommandMode_1)!= -1)|(data->indexOf(CommandMode_2)!= -1))
        {
            data->clear();
            data->append("PLC change mode of modem");
        }
        BusyObject.unlock();
        return false;
    }
    BusyObject.unlock();
    // Если идет транзит "+++", следует его очистить, чтобы не засорять эфир
    if((data->indexOf(CommandMode_1)!= -1)|(data->indexOf(CommandMode_2)!= -1))
    {
        data->clear();
        data->append("PLC change mode of modem");
        return false;
    }
    // Если идут данные о балансе
    if(data->indexOf("+BAL: ")!=-1)
    {
        GetBalance(data); // Получение баланса
        data->clear();
        data->append("PLC data of balance");
        return false;
    }

    if(((*(data->data()) =='A')|(*(data->data()) =='a'))&((*(data->data() + 1) =='T')|(*(data->data() + 1) =='t')))
    {
        // Проверка флага занятости AT и установка
        ATMutex.lock();
        if(AtStatus)
        {
            ATMutex.unlock();
            return false;
        }

        QByteArray CopyData;
        CopyData.clear();
        CopyData.append(*data);
        CopyData = CopyData.toUpper();
        if(CopyData.indexOf("AT+CFUN=1")!= -1)
        {
            IsReset.lock();
            IsModemReset = true;
            IsReset.unlock();
            AtStatus =1;
            ATMutex.unlock();
            return true;
        }
        // Если поступают команды на отправку СМС
        if(CopyData.indexOf("AT+CMGS=")!= -1)
        {
            IsSmsGet = 1;
            ATMutex.unlock();
            return true;
        }
        AtStatus =1;
        ATMutex.unlock();
        return true;
    }
    return false;
}
// Очистка данных от мусора
QByteArray* CListenThread::ClearAT(QByteArray *data)
{
    // Подчистка данных от дополнительных символов
    int index =0;
    while(index!=-1)
    {
        index = data->indexOf("OK");
        if(index!= -1)data->remove(index, 2);
    }
    index =0;
    while(index!=-1)
    {
        index = data->indexOf('\r');
        if(index!= -1)data->remove(index, 1);
    }
    index =0;
    while(index!=-1)
    {
        index = data->indexOf('\n');
        if(index!= -1)data->remove(index, 1);
    }
    return data;
}

// Обработка СМС
// Изменить функцию отправки СМС на AT+SMS="TEST",6
void CListenThread::SmsCommand(QByteArray *str)
{
    // Если установлен флаг получения команды СМС
    if(IsSmsGet ==1)
    {
        // Переход на ожидание данных тела сообщения
        IsSmsGet ++;
        SMSHead.clear();
        ClearMutex.lock();
        SMSHead = *(ClearAT(str)); // Заголовок СМС
        ClearMutex.unlock();
        QString DataPrint = " > Получен заголовок SMS: <" + SMSHead + ">\r\n";
        emit Print(DataPrint, false);
        SMSHead.append('\r');
        return;
    }
    if(IsSmsGet ==2)
    {
        // Данные СМС
        SMSData.clear();
        ClearMutex.lock();
        SMSData = *(ClearAT(str));
        ClearMutex.unlock();
        QString DataPrint = " > Получены данные SMS: <" + SMSData + ">\r\n";
        emit Print(DataPrint, false);
        DataPrint = " > Смс отправляться не будет!\r\n";
        emit Print(DataPrint, false);
        // Поиск символа Ctrl_Z
        int index =0;
        while(index!=-1)
        {
            index = SMSData.indexOf('\x1A');
            if(index!= -1)
            {
                SMSData.resize(index+1); // Уменьшение массива до указанного символа
            }
            else
                SMSData.append('\x1A');  // Добавление Ctrl-Z
        }
// ВВЕДЕНО ОГРАНИЧЕНИЯ ВВИДУ ПРОБЛЕМ С МОДЕМОМ!
        IsSmsGet =0;
//            IsSmsGet++;
        // Переход в командный режим
        // Передача команды модему на отработку
//            QByteArray a("+++");
//            sendToClient(pClientSocket, &a);
//            newTextEdit->append(((MainWindow*)(this->parent()))->uTime.toString("hh:mm:ss") + " >Смена режима на командный: <" + a + ">\r\n");
//            sleep(2);
        return;
    }
    // Получен ответ на команду перехода в командный режим
    if(IsSmsGet ==3)
    {
        ClearMutex.lock();
        *str = *(ClearAT(str));
        ClearMutex.unlock();
        if(str->isEmpty())
            str->append("nothing");
        QString DataPrint = " < Смена режима на командный: <" + *str + ">\r\n";
        emit Print(DataPrint, false);
        DataPrint = " > Заголовок сообщения: <" + SMSHead + ">\r\n";
        emit Print(DataPrint, false);
        emit SendData(SMSHead, 2);
        IsSmsGet++;
        return;
    }
    // Получен ответ на заголовок сообщения
    if(IsSmsGet ==4)
    {
        ClearMutex.lock();
        *str = *(ClearAT(str));
        ClearMutex.unlock();
        if(str->isEmpty())
            str->append("nothing");
        QString DataPrint = " < Заголовок сообщения: <" + *str + ">\r\n";
        emit Print(DataPrint, false);
        str->clear();
        *str = SMSData;
        DataPrint = " > Отправка сообщения: <" + *str + ">\r\n";
        emit Print(DataPrint, false);
        emit SendData(SMSData, 5);
        IsSmsGet++;
        SMSData.clear();
        return;
    }
    // Получен ответ на отправку сообщения
    if(IsSmsGet ==5)
    {
        ClearMutex.lock();
        *str = *(ClearAT(str));
        ClearMutex.unlock();
        if(str->isEmpty())
            str->append("nothing");
        // Смена режима на соединение
        QByteArray b("ATO\r");
        // Отправка AT-команды модему
        emit SendData(b, 2);
        QString DataPrint = " < Конец ввода: <" + *str + ">\r\n";
        emit Print(DataPrint, false);
        // Убрать последний символ
        b.resize(b.size()-1);
        DataPrint = " > Переход в режим соединения: <" + b + ">\r\n";
        emit Print(DataPrint, false);
        IsSmsGet++;
        return;
    }
    // Получен ответ на переход в режим соединения
    if(IsSmsGet ==6)
    {
        ClearMutex.lock();
        *str = *(ClearAT(str));
        ClearMutex.unlock();
        if(str->isEmpty())
            str->append("nothing");
        // Вывод текстовой информации, отправка завершена
        QString DataPrint = " < Переход в режим соединения: <" + *str + ">\r\n";
        emit Print(DataPrint, false);
        IsSmsGet =0;
        return;
    }
}

// Отправка контрольного пинга
QByteArray CListenThread::ControlPing()
{
    // Подготовка пакета для отправки
    QByteArray PingString;
    unsigned char Data = '\x4';
    PingString.append('\x1');          // ID - сервера
    PingString.append((char)ObjectID); // ID - объекта
    PingString.append('\x0');          // ID -команды ( для пингов 0, для остальных по БД)
    PingString.append('\x4');          // Функция контроля №4
    PingString.append('\x1');          // Номер порта 1-4
    char Lenght = (unsigned char)(((quint32)115200)>>24); // Скорость работы порта
    PingString.append(Lenght);
    Lenght = (unsigned char)((((quint32)115200)<<8)>>24);
    PingString.append(Lenght);
    Lenght = (unsigned char)((((quint32)115200)<<16)>>24);
    PingString.append(Lenght);
    Lenght = (unsigned char)((((quint32)115200)<<24)>>24);
    PingString.append(Lenght);
    PingString.append('\x8');           // Бит в байте
    PingString.append('\x1');           // Стоповый бит
    PingString.append('\x0');           // Биты паритета
    Lenght = (unsigned char)(((quint32)sizeof(Data))>>24); // Длина пакета
    PingString.append(Lenght);
    Lenght = (unsigned char)((((quint32)sizeof(Data))<<8)>>24);
    PingString.append(Lenght);
    Lenght = (unsigned char)((((quint32)sizeof(Data))<<16)>>24);
    PingString.append(Lenght);
    Lenght = (unsigned char)((((quint32)sizeof(Data))<<24)>>24);
    PingString.append(Lenght);
    Lenght = (unsigned char)(((quint16)500)>>8); // Таймаут оборудования
    PingString.append(Lenght);
    Lenght = (unsigned char)((((quint16)500)<<8)>>8);
    PingString.append(Lenght);
    PingString.append(Data);
    // CRC16 сообщения
    unsigned char KS_H, KS_L;
    // Возможно 2-е вхождение в функцию установка флага занятости
    BusyCRC.lock();
    GetCRC16((unsigned char *)(PingString.data()), &KS_H, &KS_L,(unsigned long) (PingString.length()+2));
    BusyCRC.unlock();
    PingString.append(KS_H);
    PingString.append(KS_L);
    return PingString;
}
// Обработка ответов от ПЛК
void CListenThread::PlcSave(QByteArray* GetsData, QSqlDatabase* db)
{

// 18.11.2015 При удачном тестировании блок удалить
/*    // Запись ответов в БД
    QStringList mTableList = db->tables();
    // Поиск таблицы пользователей
    bool IsTable = false;
    foreach (const QString &mTable, mTableList)
    {
        if(mTable.contains("tplcresult")){ IsTable = true; break; }
    }
    QSqlQuery *mQuery = new QSqlQuery(*db);
    // Данной таблицы не существует, создается новая
    if(!IsTable)
    {
        QString str = "CREATE TABLE tplcresult ( "
                                           "number INTEGER PRIMARY KEY AUTO_INCREMENT, "
                                           "ObjectID   TINYINT UNSIGNED, "
                                           "CommandID  TINYINT UNSIGNED, "
                                           "Date DATE,"
                                           "Time TIME,"
                                           "Function  TINYINT UNSIGNED, "
                                           "PLCData  BLOB, "
                                           "ErrorCode TINYINT UNSIGNED "
                      ");";
        if (!mQuery->exec(str))
        {
//              qDebug() << "Unable to create a table PLC data";
              delete mQuery;
              return;
        }
    }
*/
    // Подготовка данных
    unsigned long ID =0;          // Идентификатор объекта
    ID  += (((unsigned long)(*(GetsData->data()))) <<24);
    ID  += (((unsigned long)(*(GetsData->data() +1))) <<16);
    ID  += (((unsigned long)(*(GetsData->data() +2))) <<8);
    ID  += ((unsigned long)(*(GetsData->data() +3)));
    unsigned long CID =0;         // Идентификатор команды
    CID  += (((unsigned long)(*(GetsData->data()+5))) <<24);
    CID  += (((unsigned long)(*(GetsData->data() +6))) <<16);
    CID  += (((unsigned long)(*(GetsData->data() +7))) <<8);
    CID  += ((unsigned long)(*(GetsData->data() +8)));
    DateTime->lock();
    QDate SaveDate = *mDate;      // Текущая дата
    QTime SaveTime = *mTime;      // Текущее время
    DateTime->unlock();
    unsigned char Function =      (unsigned char)*(GetsData->data()+9);  // Номер функции
    unsigned long DataLenght =0;  // Длина полученного пакета данных 
    DataLenght += (((unsigned long)(*(GetsData->data() + 14))) << 24);
    DataLenght += (((unsigned long)(*(GetsData->data() + 15))) << 16);
    DataLenght += (((unsigned long)(*(GetsData->data() + 16))) << 8);
    DataLenght += ((unsigned long)(*(GetsData->data() + 17)));
    unsigned char ErrorCode =0;
    if(DataLenght ==1)ErrorCode = (unsigned char)*(GetsData->data()+18); // Номер кода ошибки
    QByteArray PLCData;           // Данные полученные от ПЛК
    if(DataLenght >1)PLCData.insert(0, (GetsData->data() +18), DataLenght);
// Изменение от 18.11.2015 Данные с объекта обрабатываются на сервере
    SetCurrentData(db, SaveDate, SaveTime, (quint8)Function, (quint32)ID, (quint32)CID, PLCData,
                   (quint8)ErrorCode);
//
// 18.11.2015 При удачном тестировании блок удалить
    // Запись полученного ответа в БД
/*    mQuery->prepare("INSERT INTO " + DBName + ".tplcresult (ObjectID, CommandID, Date, Time, Function, PLCData, ErrorCode)"
                        "VALUES (?, ?, ?, ?, ?, ?, ?);");
    mQuery->bindValue(0, (unsigned char)ID);
    mQuery->bindValue(1, (unsigned char)CID);
    mQuery->bindValue(2, SaveDate);
    mQuery->bindValue(3, SaveTime);
    mQuery->bindValue(4, (unsigned char)Function);
    mQuery->bindValue(5, PLCData, QSql::Binary | QSql::In);
    mQuery->bindValue(6, (unsigned char)ErrorCode);
    if (!mQuery->exec())
    {
          qDebug() << "Не выполнен запрос записи данных ПЛК\r\n" << mQuery->lastError();
    }
    delete mQuery;*/
    return;
}

// Функция обработки данных
void CListenThread::PlcData(QSqlDatabase* db)
{
    // Открытие соединения с БД если оно не открыто
    // Чтение массива для обработки потоком
    BusyFlag_in.lock(); // Ожидание освобождения данных
    // Считается, что данные уже проверены и должны быть только обработаны
    QByteArray GetsData; // Копия массива
    GetsData.clear();
    GetsData = Data_in;
    Data_in.clear();     // Очистка
    BusyFlag_in.unlock();// Освобождение данных
    // Данных нет - на выход
    if(GetsData.isEmpty())return;
    // Проверка идентификатора команды при первом получении данных
    BusyObject.lock();
// 02.09.2016 Данный блок проверки ID объекта, записи его статуса более не актуален
/*    if(!ObjectID)
    {
        int ThisId = (unsigned char)*(GetsData.data());
        // Поиск идентификатора в БД
        // Получение строки адреса NTP - сервера или локального времени
        if(db->open())
        {
            QSqlQuery *mQuery = new QSqlQuery(*db);
            QString mVer = "SELECT * FROM tobjects WHERE objectid = %1;", mV;
            mV = mVer.arg(QString::number(ThisId));
            if (!mQuery->exec(mV))
            {
//                qDebug() << "Unable to execute query — exiting:" << mQuery->lastError();
                // Запрос идентификатора объекта не выполнен
                QString DataPrint = " > Ошибка запроса к базе данных\r\n"
                                    " > Запрос идентификатора объекта не выполнен\r\n"
                                    " > Проверьте настройки либо обратитесь к администратору\r\n";
                emit Print(DataPrint, true); // На вывод
                ThreadDelete = true;
                delete mQuery;
                BusyObject.unlock();
                emit OnDelete();
                return;
            }
            // Запрос выполнен
            else
            {
                int Count = mQuery->size();
                // Запись с таким идентификатором есть, производится установка идентификатора
                // Данный поток теперь является потоком данного объекта
                if(Count > 0)
                {
                    ObjectID = ThisId;
                    QString DataPrint = " > Идентификатор нового соединения подтвержден\r\n";
                    emit Print(DataPrint, true);
                    // Запись флага в tobjects
                    mVer = "UPDATE " + DBName + ".tobjects SET IsServerSet = 1 WHERE objectid =%1 AND number > 0;";
                    mV = mVer.arg(QString::number(ObjectID));
                    if (!mQuery->exec(mV))
                    {
                        DataPrint = " > Запись флага соединения не удалась\r\n";
                        emit Print(DataPrint, false);
                    }
                    else
                    {
                        DataPrint = " > Запись флага соединения произведена\r\n";
                        emit Print(DataPrint, false);
                    }
                }
                // Данного идентификатора в БД не обнаружено
                else
                {
                    // Вывод в основное окно сообщения о разрыве соединения с объектом
                    QString DataPrint = " > Ошибка идентификатора\r\n"
                                        " > Идентификатор текщего объекта в базе не найден\r\n";
                    emit Print(DataPrint, true);
                    // На удаление
                    ThreadDelete = true;
                    delete mQuery;
                    BusyObject.unlock();
                    emit OnDelete();
                    return;
                }
            }
            delete mQuery;
        }
    }
    else
    {
        // Запись флага в tobjects
        // При некоррекном завершении работы модема и быстром установлении соединения, возможно появление 2-х потоков одного объетка
        // Первый завершится по таймауту и уберет флаг соединения, этот блок его восстановит
        QSqlQuery *mQuery = new QSqlQuery(*db);
        QString mVer = "UPDATE " + DBName + ".tobjects SET IsServerSet = 1 WHERE objectid =%1 AND number > 0;", mV;
        mV = mVer.arg(QString::number(ObjectID));
        if (!mQuery->exec(mV))
        {
            QString DataPrint = " > Запись флага соединения не удалась\r\n";
            emit Print(DataPrint, false);
        }
 //       else
 //       {
 //           QString DataPrint = " > Запись флага соединения произведена\r\n";
 //           emit Print(DataPrint, false);
 //       }
        delete mQuery;
    }
*/
    // Проверка команды и отправка ответов
    unsigned int ThisId = (unsigned char)*(GetsData.data()); // Получение ID объекта
    if(ObjectID != ThisId)
    {
        QString DataPrint = " > ID - объекта не совпадает\r\n";
        emit Print(DataPrint, false);
        DataPrint =   " > Команда будет не будет транслирована, без обработки\r\n";
        emit Print(DataPrint, false);
        DataPrint = " > ERROR: ID = " + QString::number(ObjectID) + " THISID = " + QString::number(ThisId) + "\r\n";
        emit Print(DataPrint, false);
/*        while(1) //Ожидание когда освободится сокет для отправки сообщения
        {
            BusyAT.lock();
            BusySMS.lock();
            if((AtStatus==0)&(IsSmsGet==0))break;
            BusyAT.unlock();
            BusySMS.unlock();
        }*/

       // emit SendData(GetsData, 2);               // Трансляция пакета данных без изменений
    }
    else
    {
// ИСПРАВИТЬ, смещение байта функции относительно начала пакета произвести по новому формату протокола
        char Funct = (char)*((GetsData.data()+3));
        // Отправка контрольного пинга
        if(Funct ==4)
        {
            QByteArray Control;
            Control = ControlPing();
/*            while(1) //Ожидание когда освободится сокет для отправки сообщения
            {
                BusyAT.lock();
                BusySMS.unlock();
                if((AtStatus==0)&(IsSmsGet==0))break;
                BusyAT.unlock();
                BusySMS.unlock();
            }*/
            BusyObject.unlock();
            emit SendData(Control, 5);
            return;
        }
        // На обработчик команд
        else PlcSave(&GetsData, db);
    }
    BusyObject.unlock();
}
// Чтение данных от клиента
// 03.09.2016 Добавлен блок записи ошибок при запросе к БД
void CListenThread::PlcCommand(QSqlDatabase* db, int *Number, unsigned long *CommandTimer)
{
    int IdObject =0; // Идентификатор объекта
    // Если объект не идентифицирован - на выход
    BusyObject.lock();
    if(ObjectID ==0)
    {
        BusyObject.unlock();
        MTimeOut.lock();
        TimeWait =0;                         // Сброс таймера
        MTimeOut.unlock();
        return;
    }
    IdObject = ObjectID;
    BusyObject.unlock();

    // Проверка таймера молчания (равно 2-м таймаутам оборудования)
    MTimeOut.lock();
    unsigned long Timer = TimeWait;
    MTimeOut.unlock();
    if(Timer < *CommandTimer)return;          // Выход если еще рано отправлять следующую команду

    // Первоначальная проверка освобождения канала данных от АТ-обработки команд
    ATMutex.lock();
    if(AtStatus)
    {
        ATMutex.unlock();
        MTimeOut.lock();
        TimeWait =0;                         // Сброс таймера
        MTimeOut.unlock();
        return;
    }
    ATMutex.unlock();

    MTimeOut.lock();
    TimeWait =0;                            // Сброс таймера
    MTimeOut.unlock();

    int Count =0;    // Число записей в базе команд
    QString TableName = "tcommands_" + QString::number(IdObject);
    // Поиск таблицы в БД с идентификатором объекта
    QStringList mTableList = db->tables();
    // Поиск таблицы пользователей
    bool IsTable = false;
    foreach (const QString &mTable, mTableList)
    {
        if(mTable.contains(TableName)){ IsTable = true; break; }
    }
    // Таблица команд не найдена
    if(IsTable ==false)
    {
//        qDebug() << "Таблица команд данного объекта не найдена в БД\r\n"
//                 << "Настройте таблицу объекта для начала опроса";
        // Таблица команд не найдена
        QString DataPrint = " > Таблица команд данного объекта не найдена\r\n";
        emit Print(DataPrint, false);
        DataPrint = " > Команды объекту не отправляются\r\n";
        emit Print(DataPrint, false);
        *CommandTimer = 20000;
        emit SendLastErrorDB("ОШИБКА ПОИСКА ТАБЛИЦЫ КОМАНД");
        return;
    }
    emit SendDeleteTextLastErrorDB("ОШИБКА ПОИСКА ТАБЛИЦЫ КОМАНД");
    // Определение числа записей в таблице. Номер записи используется для очередности отправки данных
    QSqlQuery* mQuery = new QSqlQuery(*db);
    QString mVer = "SELECT * FROM " + DBName + "." + TableName + ";"; // Выбрать все записи которые есть
    // Запрос не выполнен
    if (!mQuery->exec(mVer))
    {
        // Запрос команд объекта не выполнен
        QString DataPrint = " > Ошибка запроса к базе данных\r\n";
        emit Print(DataPrint, false);
        DataPrint = " > Проверьте настройки либо обратитесь к администратору\r\n";
        emit Print(DataPrint, false);
        // Ошибку следует обработать т.к. получение перечня команд обязательно для работы системы
        emit SendLastErrorDB("ОШИБКА ЧТЕНИЯ ТАБЛИЦЫ КОМАНД");
        delete mQuery;
        return;
    }
    emit SendDeleteTextLastErrorDB("ОШИБКА ЧТЕНИЯ ТАБЛИЦЫ КОМАНД");
    // Запрос выполнен
    Count = mQuery->size();
    // Команды текущему объекту не заданы
    if(Count <= 0)
    {
        QString DataPrint = " > В базе данных нет ни одной команды для данного объекта\r\n";
        emit Print(DataPrint, false);
        DataPrint = " > Задайте команды для объекта\r\n";
        emit Print(DataPrint, false); // На вывод
        *Number =0;              // Увеличение счетчика записи команды
        Count =0;                // Будет переход на отправку настроечной СМС
    }
    // Все команды отправлены, обнуление счетчика и отправка АТ-команды настройки СМС
    if((*Number - Count) >=0)
    {
        // Команда AT
        CPhone phone;
        phone.ClearData();
        QByteArray UserData;
        if(GetPhone(db, &phone))
        {
            // Формирование запроса, только если есть хотя бы одна запись
            if(phone.Records!= 0)
            {
                QString data = "AT+PHONE=1,\"" + phone.Phone[0] +"\",6";
                int i=1;
                while(i < phone.Records)
                {
                    data += ";";
                    data += "AT+PHONE=" + QString::number((i+1)) + ",\"" + phone.Phone[i] +"\",6";
                    i++;
                    if(i ==9)break; // Больше 9 нельзя т.к. уже занята запись 0
                }
                data += "\r";
                UserData.append(data); // Запись AT-команды (добавление номера)
            }
            else
                UserData.append("AT+PHONE=1,\"\"\r");               // Запись AT-команды (удаление номера)
        }
        else
        {
            UserData.append("AT+PHONE=1,\"\"\r");               // Запись AT-команды (удаление номера)
        }
        // Отправка команды
        // Основная проверка освобождения канала данных от АТ-обработки команд
        ATMutex.lock();
        if(AtStatus)
        {
            ATMutex.unlock();
            delete mQuery;
            return;
        }
        AtStatus =1;
        emit ATSignal(UserData); // Отправка АТ-команды, далее выполнение переходит в другой поток
        ATMutex.unlock();
        *Number =0;              // Увеличение счетчика записи команды
        *CommandTimer = 180000;  // После отработки всех команд - пауза 3 мин.
        delete mQuery;
        return;
    }

    // Команды заданы, начало обработки
    // Определение необходимой записи
    QSqlRecord rec = mQuery->record();
    for(int i=0; i <= *Number; i++)
    {
        mQuery->next(); // Установка на текущую команду
    }
    // Получение необходимых данных

    int number = mQuery->value(rec.indexOf("number")).toInt();                    // Номер записи
    quint16 TimeOut     = mQuery->value(rec.indexOf("TimeOut")).toInt();          // Время ожидания ответа от оборудования
    int Equipment   = mQuery->value(rec.indexOf("Equipment")).toInt();            // Вид оборудования (если не код ПЛК, то команда будет отправленя напрямую)
    unsigned char CommandID = mQuery->value(rec.indexOf("CID")).toUInt();         // Идентификатор команды, для определения ответов
    unsigned char Funct = mQuery->value(rec.indexOf("Function")).toUInt();        // Номер функции для передачи оборудованию
    unsigned char PortNum = mQuery->value(rec.indexOf("ComPort")).toUInt();       // Номер последовательного порта
    unsigned char DataCom = mQuery->value(rec.indexOf("DataPort")).toUInt();      // Размера байта
    unsigned char StopBits = mQuery->value(rec.indexOf("StopBits")).toUInt();     // Число стоповых бит
    unsigned char ParityBits = mQuery->value(rec.indexOf("ParityBits")).toUInt(); // Число стоповых бит
    quint32 CountCommand = mQuery->value(rec.indexOf("CountCommand")).toUInt();   // Количество опросов
    quint32 SpeedCom = mQuery->value(rec.indexOf("ComSpeed")).toUInt();           // Скорость работы последовательного порта
    QByteArray DataSend = mQuery->value(rec.indexOf("Command")).toByteArray();    // Команда оборудованию
    quint32 DataLenght = DataSend.length();                                       // Длина команды оборудованию
    delete mQuery;

    // Подготовка пакета данных для ПЛК
    if(Equipment ==1)
    {
        // Оправка команды ПЛК
        QByteArray Packet;
        Packet.append('\x1');          // ID - сервера
        Packet.append((char)IdObject); // ID - объекта
        Packet.append(CommandID);      // ID -команды
        Packet.append(Funct);          // Функция
        Packet.append(PortNum);        // Номер порта 1-4
        char Lenght = (unsigned char)((SpeedCom)>>24); // Скорость работы порта
        Packet.append(Lenght);
        Lenght = (unsigned char)(((SpeedCom)<<8)>>24);
        Packet.append(Lenght);
        Lenght = (unsigned char)(((SpeedCom)<<16)>>24);
        Packet.append(Lenght);
        Lenght = (unsigned char)(((SpeedCom)<<24)>>24);
        Packet.append(Lenght);
        Packet.append(DataCom);         // Бит в байте
        Packet.append(StopBits);        // Стоповый бит
        Packet.append(ParityBits);      // Биты паритета
        Lenght = (unsigned char)((DataLenght)>>24); // Длина пакета
        Packet.append(Lenght);
        Lenght = (unsigned char)(((DataLenght)<<8)>>24);
        Packet.append(Lenght);
        Lenght = (unsigned char)(((DataLenght)<<16)>>24);
        Packet.append(Lenght);
        Lenght = (unsigned char)(((DataLenght)<<24)>>24);
        Packet.append(Lenght);
        Lenght = (unsigned char)((TimeOut)>>8); // Таймаут оборудования
        Packet.append(Lenght);
        Lenght = (unsigned char)(((TimeOut)<<8)>>8);
        Packet.append(Lenght);
        Packet.append(DataSend);
        // CRC16 сообщения
        unsigned char KS_H, KS_L;
        // Возможно 2-е вхождение в функцию установка флага занятости
        BusyCRC.lock();
        GetCRC16((unsigned char *)(Packet.data()), &KS_H, &KS_L,(unsigned long) (Packet.length()+2));
        BusyCRC.unlock();
        Packet.append(KS_H);
        Packet.append(KS_L);
        // Основная проверка освобождения канала данных от АТ-обработки команд
        ATMutex.lock();
        if(AtStatus)
        {
            ATMutex.unlock();
            return;
        }
        ATMutex.unlock();

        // Отправка команды
        emit SendData(Packet, 5);
        // Проверка числа повторов команды
        if(CountCommand!=0)
        {
            // Удаление команды, т.к. необходимое число запросов выполнено
            if(CountCommand ==1)
            {
                QSqlQuery* mQuery = new QSqlQuery(*db);
                QString mVer = "DELETE FROM " + DBName + "." + TableName + " WHERE number ="
                               + QString::number(number) + " AND number >0;"; // Выбрать нужную запись
                // Запрос не выполнен
                if (!mQuery->exec(mVer))
                {
                    // Запрос команд объекта не выполнен
                    QString DataPrint = " > Ошибка запроса к базе данных\r\n";
                    emit Print(DataPrint, false);
                    DataPrint = " > Проверьте настройки либо обратитесь к администратору\r\n";
                    emit Print(DataPrint, false);
                    emit SendLastErrorDB("ОШИБКА УДАЛЕНИЯ КОМАНДЫ ИЗ БД");
                }
                else
                {
                    *Number -=1; // Команд стало меньше, следующая команда под тем же номером
                    emit SendDeleteTextLastErrorDB("ОШИБКА УДАЛЕНИЯ КОМАНДЫ ИЗ БД");
                }
                delete mQuery;
            }
            // Уменьшение счетчика запросов
            else
            {
                CountCommand--;
                QSqlQuery* mQuery = new QSqlQuery(*db);
                QString mVer = "UPDATE " + DBName + "." + TableName + " SET CountCommand ="
                               + QString::number(CountCommand) + " WHERE number ="
                               + QString::number(number) + " AND number >0;"; // Выбрать нужную запись
                // Запрос не выполнен
                if (!mQuery->exec(mVer))
                {
                    // Запрос команд объекта не выполнен
                    QString DataPrint = " > Ошибка запроса к базе данных\r\n";
                    emit Print(DataPrint, false);
                    DataPrint =" > Проверьте настройки либо обратитесь к администратору\r\n";
                    emit Print(DataPrint, false);
                    emit SendLastErrorDB("ОШИБКА ЗАПИСИ СЧЕТЧИКА КОМАНД В БД");
                }
                else emit SendDeleteTextLastErrorDB("ОШИБКА ЗАПИСИ СЧЕТЧИКА КОМАНД В БД");
                delete mQuery;
            }
        }
        *Number +=1;                                                                  // Увеличение счетчика записи команды
        *CommandTimer = (TimeOut * 10);                                               // Обновление счетчика времени
        return;
    }
    // Подготовка пакета данных для модема
    if(Equipment ==0)
    {
        // Основная проверка освобождения канала данных от АТ-обработки команд
        ATMutex.lock();
        if(AtStatus)
        {
            ATMutex.unlock();
            return;
        }
        AtStatus =1;
        emit ATSignal(DataSend); // Отправка АТ-команды, далее выполнение переходит в другой поток
        ATMutex.unlock();
        // Проверка числа повторов команды
        if(CountCommand!=0)
        {
            // Удаление команды, т.к. необходимое число запросов выполнено
            if(CountCommand ==1)
            {
                QSqlQuery* mQuery = new QSqlQuery(*db);
                QString mVer = "DELETE FROM " + DBName + "." + TableName + " WHERE number ="
                               + QString::number(number) + " AND number >0;"; // Выбрать нужную запись
                // Запрос не выполнен
                if (!mQuery->exec(mVer))
                {
                    // Запрос команд объекта не выполнен
                    QString DataPrint = " > Ошибка запроса к базе данных\r\n";
                    emit Print(DataPrint, false);
                    DataPrint = " > Проверьте настройки либо обратитесь к администратору\r\n";
                    emit Print(DataPrint, false);
                    emit SendLastErrorDB("ОШИБКА УДАЛЕНИЯ КОМАНДЫ ИЗ БД");
                }
                else
                {
                    *Number -=1; // Команд стало меньше, следующая команда под тем же номером
                    emit SendDeleteTextLastErrorDB("ОШИБКА УДАЛЕНИЯ КОМАНДЫ ИЗ БД");
                }
                delete mQuery;
            }
            // Уменьшение счетчика запросов
            else
            {
                CountCommand--;
                QSqlQuery* mQuery = new QSqlQuery(*db);
                QString mVer = "UPDATE " + DBName + "." + TableName + " SET CountCommand ="
                               + QString::number(CountCommand) + " WHERE number ="
                               + QString::number(number) + " AND number >0;"; // Выбрать нужную запись
                // Запрос не выполнен
                if (!mQuery->exec(mVer))
                {
                    // Запрос команд объекта не выполнен
                    QString DataPrint = " > Ошибка запроса к базе данных\r\n";
                    emit Print(DataPrint, false);
                    DataPrint = " > Проверьте настройки либо обратитесь к администратору\r\n";
                    emit Print(DataPrint, false);
                    emit SendLastErrorDB("ОШИБКА ЗАПИСИ СЧЕТЧИКА КОМАНД В БД");
                }
                else emit SendDeleteTextLastErrorDB("ОШИБКА ЗАПИСИ СЧЕТЧИКА КОМАНД В БД");
                delete mQuery;
            }
        }
        *Number +=1;                                                                  // Увеличение счетчика записи команды
        *CommandTimer = 5000;            // В случае AT-команд
        return;
    }
    // Прямая команда устройству
    if(Equipment >1)
    {
        // Основная проверка освобождения канала данных от АТ-обработки команд
        ATMutex.lock();
        if(AtStatus)
        {
            ATMutex.unlock();
            return;
        }
        ATMutex.unlock();

        emit SendData(DataSend, 5); // Отправка АТ-команды, далее выполнение переходит в другой поток
        // Проверка числа повторов команды
        if(CountCommand!=0)
        {
            // Удаление команды, т.к. необходимое число запросов выполнено
            if(CountCommand ==1)
            {
                QSqlQuery* mQuery = new QSqlQuery(*db);
                QString mVer = "DELETE FROM " + DBName + "." + TableName + " WHERE number ="
                               + QString::number(number) + " AND number >0;"; // Выбрать нужную запись
                // Запрос не выполнен
                if (!mQuery->exec(mVer))
                {
                    // Запрос команд объекта не выполнен
                    QString DataPrint = " > Ошибка запроса к базе данных\r\n";
                    emit Print(DataPrint, false);
                    DataPrint = " > Проверьте настройки либо обратитесь к администратору\r\n";
                    emit Print(DataPrint, false);
                    emit SendLastErrorDB("ОШИБКА УДАЛЕНИЯ КОМАНДЫ ИЗ БД");
                }
                else
                {
                    *Number -=1; // Команд стало меньше, следующая команда под тем же номером
                    emit SendDeleteTextLastErrorDB("ОШИБКА УДАЛЕНИЯ КОМАНДЫ ИЗ БД");
                }
                delete mQuery;
            }
            // Уменьшение счетчика запросов
            else
            {
                CountCommand--;
                QSqlQuery* mQuery = new QSqlQuery(*db);
                QString mVer = "UPDATE " + DBName + "." + TableName + " SET CountCommand ="
                               + QString::number(CountCommand) + " WHERE number ="
                               + QString::number(number) + " AND number >0;"; // Выбрать нужную запись
                // Запрос не выполнен
                if (!mQuery->exec(mVer))
                {
                    // Запрос команд объекта не выполнен
                    QString DataPrint = " > Ошибка запроса к базе данных\r\n";
                    emit Print(DataPrint, false);
                    DataPrint = " > Проверьте настройки либо обратитесь к администратору\r\n";
                    emit Print(DataPrint, false);
                    emit SendLastErrorDB("ОШИБКА ЗАПИСИ СЧЕТЧИКА КОМАНД В БД");
                }
                else emit SendDeleteTextLastErrorDB("ОШИБКА ЗАПИСИ СЧЕТЧИКА КОМАНД В БД");
                delete mQuery;
            }
        }
        *Number +=1;                                                                  // Увеличение счетчика записи команды
        *CommandTimer = TimeOut * 10;              // Обновление счетчика времени
        return;
    }
}

// Отправка AT-команды
void CListenThread::ATCommand(QByteArray DataSend)
{
    ATMutex.lock();
    // Первое вхождение в режим
    if(AtStatus ==1)
    {
        // Очистка AT- команды от лишних знаков
        ClearMutex.lock();
        DataSend = *(ClearAT(&DataSend)); // Убрать все знаки \r\n команда сама добавит все что необходимо
        ClearMutex.unlock();
        QString DataPrint = " Получена AT-команда: <" + DataSend + ">\r\n";
        qDebug() << DataPrint;
        emit Print(DataPrint, false);
        DataSend.append('\r');
        // Передача команды модему на отработку
        QByteArray a("+++");
        emit SendData(a, 3);
        DataPrint = " Смена режима на командный: <" + a + ">\r\n";
        emit Print(DataPrint, false);
        // Запись AT-команды
        AtData.clear();
        AtData = DataSend;
        // Флаг статуса запроса командного режима
        AtStatus++;
        ATMutex.unlock();
        return;
    }
    if(AtStatus ==2)
    {
        // Проверка ответа на переход в командный режим, если ответ не "OK", значит получен не тот ответ
        if(DataSend.indexOf("OK\r") ==-1) // Получен не тот ответ, либо асинхронные данные
        {
            qDebug() << "Ответ на команну \"+++\" некорректен: " << DataSend;
            QString DataPrint = " Ответ на команну \"+++\" некорректен\r\n";
            emit Print(DataPrint, false);
            AtStatus = 0;
            AtData.clear();
            ATMutex.unlock();
            IsReset.lock();
            IsModemReset = false;
            IsReset.unlock();
            // На всякий случай отправка команды "ATO"
            QByteArray b("ATO\r");
            DataPrint = " Возврат режима на соединение по ошибке\r\n";
            emit Print(DataPrint, false);
            // Отправка AT-команды модему
            emit SendData(b, 2);
            return;
        }
        // Очистка AT- команды от лишних знаков
        ClearMutex.lock();
        DataSend = *(ClearAT(&DataSend));
        ClearMutex.unlock();
        if((DataSend.isEmpty() | (DataSend.length() <2)))
        {
            DataSend.clear();
            DataSend.append("nothing");
        }
        QString DataPrint = " Смена режима произведена: <" + DataSend + ">\r\n";
        emit Print(DataPrint, false);
        DataSend.clear();
        DataSend = AtData;
        DataSend.resize(DataSend.size()-1);
        DataPrint = " Отправка AT-команды: <" + DataSend + ">\r\n";
        // Отправка AT-команды модему
        emit SendData(AtData, 2);
        emit Print(DataPrint, false);
        // Был сброс модема, дальше работать нет смысла
        IsReset.lock();
        if(IsModemReset)
        {
            // Сброс будет вызван из потока отправки, иначе не работает
            qDebug() << "Сброс модема AT-командой: " << AtData;
            AtStatus = 0;
            ATMutex.unlock();
            IsReset.unlock();
            return;
        }
        IsReset.unlock();
        AtStatus++; // Состояние отправки AT-команды модему
        ATMutex.unlock();
        return;
    }
    // Получен ответ на AT-команду
    else if(AtStatus ==3)
    {
        ClearMutex.lock();
        DataSend = *(ClearAT(&DataSend));
        ClearMutex.unlock();
        if((DataSend.isEmpty() | (DataSend.length() <2)))
        {
            DataSend.clear();
            DataSend.append("nothing");
        }
        QString DataPrint = " Результат AT-команды: <" + DataSend + ">\r\n";
        emit Print(DataPrint, false);
        // Запись AT-команды
        AtData.clear();
        AtData = DataSend;
        // Проверка не пришел ли баланс
        if(DataSend.indexOf("+BAL: ")!=-1)
        {
            GetBalance(&DataSend); // Получение баланса
        }
        // Смена режима на соединение
        QByteArray b("ATO\r");
        DataSend.clear();
        DataSend = b;
        DataSend.resize(DataSend.size()-1);
        DataPrint = " Смена режима на соединение: <" + DataSend + ">\r\n";
        emit Print(DataPrint, false);
        // Отправка AT-команды модему
        emit SendData(b, 2);
        AtStatus++;
        ATMutex.unlock();
        return;
    }
    else if(AtStatus ==4)
    {
        ClearMutex.lock();
        DataSend = *(ClearAT(&DataSend));
        ClearMutex.unlock();
        if((DataSend.isEmpty() | (DataSend.length() <2)))
        {
            DataSend.clear();
            DataSend.append("nothing");
        }
        QString DataPrint = " Смена режима произведена: <" + DataSend + ">\r\n";
        emit Print(DataPrint, false);
        // Если ответ от модема был и не ОК, тогда производится отправка данных
        if((AtData!="nothing")&(AtData.size() >1))
        {
            DataPrint = " Отправка результата AT-команды: <" + AtData + ">\r\n";
            emit Print(DataPrint, false);
            emit SendData(AtData, 1);
        }
        // Если ответ пришел с задержкой
        if((DataSend!="nothing")&(DataSend.size() >1))
        {
            DataPrint = " Отправка результата AT-команды: <" + DataSend + ">\r\n";
            emit Print(DataPrint, false);
            emit SendData(DataSend, 1);
            // Проверка не пришел ли баланс
            if(DataSend.indexOf("+BAL: ")!=-1)
            {
                GetBalance(&DataSend); // Получение баланса
            }
        }
        AtStatus =0;
        ATMutex.unlock();
        return;
    }
    ATMutex.unlock();
}

// Функция отображения текста
void CListenThread::PrintString(QString string, bool IsMain)
{
    // Отправка сигнала в главное окно
    emit PrintToMain(string, IsMain, ObjectName);
}

// Получение рабочего номера и установки СМС основного оператора
bool CListenThread::GetPhone(QSqlDatabase *db, CPhone* phone)
{
    if(!db->open())return false; // Если БД не открыта
    QSqlQuery *mQuery = new QSqlQuery(*db);
    QString mUString = "SELECT * FROM " + DBName + ".tsystem WHERE UserName !='root' AND IsServerSet =1;";
    if (!mQuery->exec(mUString))
    {
        qDebug() << "Unable to execute query — exiting:" << mQuery->lastError();
        QString DataPrint = " Выполнить запрос текщего клиента не удалось\r\n";
        emit Print(DataPrint, false);
        emit SendLastErrorDB("ОШИБКА ПОЛУЧЕНИЯ УЧЕТНЫХ ЗАПИСЕЙ КЛИЕНТА");
        delete mQuery;
        // Очистка данных, СМС уведомлений больше не будет
        phone->ClearData();
        return false;
    }
    emit SendDeleteTextLastErrorDB("ОШИБКА ПОЛУЧЕНИЯ УЧЕТНЫХ ЗАПИСЕЙ КЛИЕНТА");
    QSqlRecord rec = mQuery->record();
    int Count = mQuery->size();     // Счетчик записей
    if(Count > 0)
    {
        int i =0;
        while (mQuery->next())
        {
            // Флаг СМС и номер рабочего телефона
            phone->Login[i] = mQuery->value(rec.indexOf("UserName")).toString();
            phone->Records++;
            i++;
            if(phone->Records ==10)break; // Больше 10 нельзя
        }
    }
    else // Активных клиентов нет
    {
        qDebug() << "Нет активных клиентов";
        QString DataPrint = " Нет подключенного оператора\r\n";
        emit Print(DataPrint, false);
        delete mQuery;
        // СМС отправлений не будет
        phone->ClearData();
        return false;
    }
    // Сборка запроса из разных логинов
    mUString = "SELECT * FROM " + DBName + ".tusers WHERE userlogin ='" + phone->Login[0] +"' ";
    for(int i =1; i < phone->Records; i++)
    {
        mUString += " OR userlogin ='" + phone->Login[i] +"' ";
    }
    mUString += ";";
    if (!mQuery->exec(mUString))
    {
        qDebug() << "Unable to execute query — exiting:" << mQuery->lastError();
        QString DataPrint = " Получить личные данные оператора не удалось\r\n";
        emit Print(DataPrint, false);
        emit SendLastErrorDB("ОШИБКА ПОЛУЧЕНИЯ ДАННЫХ ОПЕРАТОРОВ ИЗ БД");
        delete mQuery;
        // Запрос не выполнен, СМС отправка будет заблокирована
        phone->ClearData();
        return false;
    }
    emit SendDeleteTextLastErrorDB("ОШИБКА ПОЛУЧЕНИЯ ДАННЫХ ОПЕРАТОРОВ ИЗ БД");
    rec.clear();
    rec     = mQuery->record();
    Count = mQuery->size();     // Счетчик записей
    // Если получено число данных пользователей равное числу работающих в системе
    if(Count == phone->Records)
    {
        int i =0;
        phone->Records =0;
        while (mQuery->next())
        {
            // Флаг СМС и номер рабочего телефона
            phone->SetSMS[i] = mQuery->value(rec.indexOf("smsset")).toInt();
            phone->Phone[i]  = mQuery->value(rec.indexOf("worktelephone")).toString();
            phone->Records++;
            i++;
            if(phone->Records ==10)break; // Больше 10 нельзя
        }
        delete mQuery;
        return true;
    }
    QString DataPrint = " Нет данных текущего оператора\r\n";
    emit Print(DataPrint, false);
    // Запрос не выполнен, СМС отправка будет заблокирована
    phone->ClearData();
    delete mQuery;
    return false;
}

// Получение баланса
// 28.11.2016 Внимание данная функция работает в родительском потоке
void CListenThread::GetBalance(QByteArray *data)
{
    data->remove(0,6); // Удаление текстовой части
    float balance = 0;
    balance = data->toFloat();
    // 27.11.2016 Доступ к БД через мьютекс (блокируемые данные даты и времени вынесены во избежание конфликта)
    DateTime->lock();
    QDate SaveDate = *mDate;      // Текущая дата
    QTime SaveTime = *mTime;      // Текущее время
    DateTime->unlock();
    ((MainWindow*)this->parent())->BusyDB.lock();
    QSqlDatabase db = QSqlDatabase::database("connection");
    if(!db.open())
    {
        ((MainWindow*)this->parent())->BusyDB.unlock();
        qDebug() << "Не открыта база данных\r\n" << db.lastError().databaseText();
        return;
    }
// 29.12.2015 Обработка данных производится на сервере
/*
    // Запись в БД результата команды
    QStringList mTableList = db.tables();
    // Поиск таблицы пользователей
    bool IsTable = false;
    foreach (const QString &mTable, mTableList)
    {
        if(mTable.contains("tplcresult")){ IsTable = true; break; }
    }
    QSqlQuery *mQuery = new QSqlQuery(db);
    // Данной таблицы не существует, создается новая
    if(!IsTable)
    {
        QString str = "CREATE TABLE tplcresult ( "
                                           "number INTEGER PRIMARY KEY AUTO_INCREMENT, "
                                           "ObjectID   TINYINT UNSIGNED, "
                                           "CommandID  TINYINT UNSIGNED, "
                                           "Date DATE,"
                                           "Time TIME,"
                                           "Function  TINYINT UNSIGNED, "
                                           "PLCData  BLOB, "
                                           "ErrorCode TINYINT UNSIGNED "
                      ");";
        if (!mQuery->exec(str))
        {
//              qDebug() << "Unable to create a table PLC data";
              delete mQuery;
              return;
        }
    }
*/
    // Подготовка данных
    BusyObject.lock();
    quint32 ID  =    ObjectID;    // Идентификатор объекта
    BusyObject.unlock();
    quint32 CID =    0;           // Идентификатор команды (для AT- всегда 0)
    unsigned char Function =0;    // Номер функции (для AT- всегда 0)
    unsigned char ErrorCode =0;
    QByteArray ModemData;         // Данные полученные от модема
    ModemData.clear();
    ModemData.append(QByteArray::number(balance));
    // Запись данных
    SetCurrentData(&db, SaveDate, SaveTime, (quint8)Function, (quint32)ID, (quint32)CID, ModemData,
                   (quint8)ErrorCode);
    // Запись полученного ответа в БД
/*    mQuery->prepare("INSERT INTO " + DBName + ".tplcresult (ObjectID, CommandID, Date, Time, Function, PLCData, ErrorCode)"
                        "VALUES (?, ?, ?, ?, ?, ?, ?);");
    mQuery->bindValue(0, (unsigned char)ID);
    mQuery->bindValue(1, (unsigned char)CID);
    mQuery->bindValue(2, SaveDate);
    mQuery->bindValue(3, SaveTime);
    mQuery->bindValue(4, (unsigned char)Function);
    mQuery->bindValue(5, ModemData, QSql::Binary | QSql::In);
    mQuery->bindValue(6, (unsigned char)ErrorCode);
    if (!mQuery->exec())
    {
          qDebug() << "Не выполнен запрос записи данных баланса\r\n" << mQuery->lastError();
    }
    delete mQuery;*/
    ((MainWindow*)this->parent())->BusyDB.unlock();
    return;
}

// Запись данных в БД
bool CListenThread::SetCurrentData(QSqlDatabase*db, QDate date, QTime time, quint8 Funct,
                              quint32 ObjectID, quint32 CommandID, QByteArray PlcData, quint8 ErrorCode)
{
    QString mVer, mV;
    int Count =0;
    // Поиск таблицы данных по объекту
    if(!db->open())return false;          // Если БД не открыта
    QSqlQuery *mQuery = new QSqlQuery(*db);
// ------------------------------------------------------------------------------------------------------------------------
// Примечание, таблицы которые создаются для хранения данных объекта будут настраиваться и создаваться в редакторе объектов
// После реализации редактора функции следует удалить
    // Добавление таблицы данных текущего объекта, либо поиск имеющейся
    if(!SetObjectDataCurrent(db, ObjectID))
    {
        delete mQuery;
        return false;
    }
    // Добавление таблицы архивных данных, либо поиск имеющейся
    if(!SetObjectDataTable(db, ObjectID))
    {
        delete mQuery;
        return false;
    }
    // Добавление таблицы настроек параметров и аварийных сообщений
    if(!SetSetupDataTable(db, ObjectID))
    {
        delete mQuery;
        return false;
    }
// -------------------------------------------------------------------------------------------------------------------------
    // Пришли данные баланса
    if((CommandID ==0)|(Funct ==0))
    {
        // Проверка наличия столбца в таблице объектов и если таковых нет, то данные игнорируются
        mVer = "SELECT GSMPeriod FROM " + DBName + ".tobjects WHERE objectid =" + QString::number(ObjectID) + ";";
        if (!mQuery->exec(mVer))
        {
            qDebug() << "Данные периода опроса отсутствуют, информация игнорируется.\r\n"
                        "Настройка не произведена. Запоздалый ответ на команду";
            emit SendLastErrorDB("ОШИБКА ПОЛУЧЕНИЯ ЧАСТОТЫ ПРОВЕРКИ БАЛАНСА");
            delete mQuery;
            return false;
        }
        emit SendDeleteTextLastErrorDB("ОШИБКА ПОЛУЧЕНИЯ ЧАСТОТЫ ПРОВЕРКИ БАЛАНСА");
        Count = mQuery->size();
        // Столбец найден
        if(Count >0)
        {
            // Проверка значения периода
            QSqlRecord rec = mQuery->record();
            mQuery->next();
            quint16 Period = mQuery->value(rec.indexOf("GSMPeriod")).toUInt();
            if(Period ==0)
            {
                qDebug() << "Период опроса равен нулю, значения не обрабатываются\r\n";
                delete mQuery;
                return false;
            }
        }
        else // Столбец не найден
        {
            delete mQuery;
            return false;
        }
        // Проверка наличия столбца в текущих данных
        mVer = "SELECT Balance FROM " + DBName + ".tdata_current_" + QString::number(ObjectID) + ";";
        if (!mQuery->exec(mVer))
        {
            qDebug() << "Данные баланса отсутствуют.\r\n"
                        "Будет добавлен столбец данных";
        }
        Count = mQuery->size();
        if(Count <=0)
        {
            mVer = "ALTER TABLE " + DBName + ".tdata_current_" + QString::number(ObjectID) + " ADD Balance FLOAT, ADD BalDate DATE, ADD BalTime TIME;";
            if (!mQuery->exec(mVer))
            {
                qDebug() << "Не удалось добавить столбец баланса в таблицу текущих параметров" << mQuery->lastError();
                emit SendLastErrorDB("ОШИБКА ДОБАВЛЕНИЯ СТОЛБЦА БАЛАНСА В БД");
                delete mQuery;
                return false;
            }
            emit SendDeleteTextLastErrorDB("ОШИБКА ДОБАВЛЕНИЯ СТОЛБЦА БАЛАНСА В БД");
            qDebug() << "Столбец баланса добавлен в таблицу текущих параметров";
        }
        // Запись значения баланса в таблицу
        float Balance = PlcData.toFloat();
        mVer = "UPDATE " + DBName + ".tdata_current_" + QString::number(ObjectID) + " SET Balance = " + QString::number(Balance) +
                " WHERE number >0;";
        if (!mQuery->exec(mVer))
        {
           qDebug() << "Не удалось записать текущий баланс" << mQuery->lastError();
           emit SendLastErrorDB("ОШИБКА ЗАПИСИ БАЛАНСА В БД");
           delete mQuery;
           return false;
        }
        emit SendDeleteTextLastErrorDB("ОШИБКА ЗАПИСИ БАЛАНСА В БД");
        // Запись ID - объекта и временных показателей в таблицу текущих параметров
        mQuery->prepare("UPDATE " + DBName + ".tdata_current_" + QString::number(ObjectID) +
                        " SET BalDate = :A, BalTime = :B, ObjectID = :C WHERE number >0;");
        mQuery->bindValue(":A", date);
        mQuery->bindValue(":B", time);
        mQuery->bindValue(":C", ObjectID);
        if (!mQuery->exec())
        {
           qDebug() << "Не удалось записать дату и время текущего баланса" << mQuery->lastError();
           emit SendLastErrorDB("ОШИБКА ЗАПИСИ ДАТЫ И ВРЕМЕНИ БАЛАНСА");
           delete mQuery;
           return false;
        }
        emit SendDeleteTextLastErrorDB("ОШИБКА ЗАПИСИ ДАТЫ И ВРЕМЕНИ БАЛАНСА");
    }
    // Заполнение данными только в данный момент только по 1-й функции
    if(Funct ==1)
    {
        if(CommandID ==1) // I-7017
        {
            // Проверка наличия столбцов в таблице текущих параметров и если таковых нет, то добавление
            mVer = "SELECT AI1, AI2, AI3, AI4, AI5, AI6, AI7, AI8 FROM " + DBName + ".tdata_current_" +
                    QString::number(ObjectID) + ";";
            if (!mQuery->exec(mVer))
            {
                qDebug() << "Не удалось выполнить запрос столбцов в текущих значениях" << mQuery->lastError();
             //   delete mQuery;
             //   return false;
            }
            Count = mQuery->size();
            // Столбцов не найдено
            if(Count <=0)
            {
                mVer = "ALTER TABLE " + DBName + ".tdata_current_" + QString::number(ObjectID) + " ADD AI1 FLOAT, ADD AI2 FLOAT, "
                       "ADD AI3 FLOAT, ADD AI4 FLOAT, ADD AI5 FLOAT, ADD AI6 FLOAT, ADD AI7 FLOAT, ADD AI8 FLOAT;";
                if (!mQuery->exec(mVer))
                {
                    qDebug() << "Не удалось добавить столбцы в таблицу текущих параметров (I-7017)" << mQuery->lastError();
                    emit SendLastErrorDB("ОШИБКА ДОБАВЛЕНИЯ СТОЛБЦОВ В ТАБЛИЦУ ТЕКУЩИХ ПАРАМЕТРОВ");
                    delete mQuery;
                    return false;
                }
                emit SendDeleteTextLastErrorDB("ОШИБКА ДОБАВЛЕНИЯ СТОЛБЦОВ В ТАБЛИЦУ ТЕКУЩИХ ПАРАМЕТРОВ");
                qDebug() << "Столбцы добавлены в таблицу текущих параметров (I-7017)";
            }
            // Проверка наличия столбцов в таблице архивных параметров и если таковых нет, то добавление
            mVer = "SELECT AI1, AI2, AI3, AI4, AI5, AI6, AI7, AI8 FROM " + DBName + ".tdata_" +
                    QString::number(ObjectID) + ";";
            if (!mQuery->exec(mVer))
            {
                qDebug() << "Не удалось выполнить запрос столбцов в архивных значениях" << mQuery->lastError();
             //   delete mQuery;
             //   return false;
            }
            Count = mQuery->size();
            // Столбцов не найдено
            if(Count <=0)
            {
                mVer = "ALTER TABLE " + DBName + ".tdata_" + QString::number(ObjectID) + " ADD AI1 FLOAT, ADD AI2 FLOAT, "
                       "ADD AI3 FLOAT, ADD AI4 FLOAT, ADD AI5 FLOAT, ADD AI6 FLOAT, ADD AI7 FLOAT, ADD AI8 FLOAT;";
                if (!mQuery->exec(mVer))
                {
                    qDebug() << "Не удалось добавить столбцы в таблицу архива (I-7017)" << mQuery->lastError();
                    emit SendLastErrorDB("ОШИБКА ДОБАВЛЕНИЯ СТОЛБЦОВ В ТАБЛИЦУ АРХИВНЫХ ПАРАМЕТРОВ");
                    delete mQuery;
                    return false;
                }
                emit SendDeleteTextLastErrorDB("ОШИБКА ДОБАВЛЕНИЯ СТОЛБЦОВ В ТАБЛИЦУ АРХИВНЫХ ПАРАМЕТРОВ");
                qDebug() << "Столбцы добавлены в таблицу архива (I-7017)";
            }
            // Проверка наличия строк настройки параметров
            mVer = "SELECT * FROM " + DBName + ".tsetup_" + QString::number(ObjectID) + " WHERE parameter = 'AI1' OR "
                   " parameter = 'AI2' OR parameter = 'AI3' OR parameter = 'AI4' OR parameter = 'AI5' OR parameter = 'AI6' "
                   " OR parameter = 'AI7' OR parameter = 'AI8';";
            if (!mQuery->exec(mVer))
            {
                qDebug() << "Не удалось выполнить запрос строк в таблице настройки параметров I-7017" << mQuery->lastError();
             //   delete mQuery;
             //   return false;
            }
            Count = mQuery->size();
            // Строк не найдено, добавление строк AI сигналов
            if(Count <=0)
            {
                for(int i=0; i < 8; i++)
                {
                    mVer = "INSERT INTO " + DBName + ".tsetup_" + QString::number(ObjectID) + " (parameter, Ed_Izm, Minimum_sensor, "
                        "Maximum_sensor, Minimum_param, Maximum_param, Alarm_border_MIN, Alarm_border_MAX, Alarm_set, Alarm_text) "
                        "VALUES ('AI" + QString::number(i+1) + "','не задано', 0, 0, 0, 0, 0, 0, 0,'Сообщение не задано');";
                    if (!mQuery->exec(mVer))
                    {
                        qDebug() << "Не удалось добавить строки настройки параметров (I-7017)" << mQuery->lastError();
                        emit SendLastErrorDB("ОШИБКА ЗАПИСИ СТРОК НАСТРОЕЧНЫХ ПАРАМЕТРОВ");
                        delete mQuery;
                        return false;
                    }
                    emit SendDeleteTextLastErrorDB("ОШИБКА ЗАПИСИ СТРОК НАСТРОЕЧНЫХ ПАРАМЕТРОВ");
                    qDebug() << "Строки добавлены в таблицу настройки параметров (I-7017)";
                }
            }

            // Переменные для записи
            float AI1 =0, AI2=0, AI3=0, AI4=0, AI5=0, AI6=0, AI7=0, AI8=0;
            if(ErrorCode==(quint8)NOERRORS)
            {
                // Начинается с '>' и длина 58 байт
                if((PlcData.indexOf(">")!=-1)&(PlcData.size() ==58))
                {
                    QByteArray takeOne;
                    takeOne.clear();
                    PlcData.remove(0,1);
                    takeOne = PlcData.left(7);
                    PlcData.remove(0,7);
                    AI1 = takeOne.toFloat();
                    float AI = AI1;
                    // Проверка параметра
                    IsAlarm(db, ObjectID, AI, "AI1", date, time);
                    takeOne.clear();
                    takeOne = PlcData.left(7);
                    PlcData.remove(0,7);
                    AI2 = takeOne.toFloat();
                    AI = AI2;
                    // Проверка параметра
                    IsAlarm(db, ObjectID, AI, "AI2", date, time);
                    takeOne.clear();
                    takeOne = PlcData.left(7);
                    PlcData.remove(0,7);
                    AI3 = takeOne.toFloat();
                    AI = AI3;
                    // Проверка параметра
                    IsAlarm(db, ObjectID, AI, "AI3", date, time);
                    takeOne.clear();
                    takeOne = PlcData.left(7);
                    PlcData.remove(0,7);
                    AI4 = takeOne.toFloat();
                    AI = AI4;
                    // Проверка параметра
                    IsAlarm(db, ObjectID, AI, "AI4", date, time);
                    takeOne.clear();
                    takeOne = PlcData.left(7);
                    PlcData.remove(0,7);
                    AI5 = takeOne.toFloat();
                    AI = AI5;
                    // Проверка параметра
                    IsAlarm(db, ObjectID, AI, "AI5", date, time);
                    takeOne.clear();
                    takeOne = PlcData.left(7);
                    PlcData.remove(0,7);
                    AI6 = takeOne.toFloat();
                    AI = AI6;
                    // Проверка параметра
                    IsAlarm(db, ObjectID, AI, "AI6", date, time);
                    takeOne.clear();
                    takeOne = PlcData.left(7);
                    PlcData.remove(0,7);
                    AI7 = takeOne.toFloat();
                    AI = AI7;
                    // Проверка параметра
                    IsAlarm(db, ObjectID, AI, "AI7", date, time);
                    takeOne.clear();
                    takeOne = PlcData.left(7);
                    AI8 = takeOne.toFloat();
                    AI = AI8;
                    // Проверка параметра
                    IsAlarm(db, ObjectID, AI, "AI8", date, time);
                }
                else
                {
                    qDebug() << "Данные I-7017 не распознаны";
                }
            }
            // Проверка обновлены ли данные другим оператором (ИЗМЕНЕНИЕ ОТ 24.05.2015)
            // Если такая запись есть, то обновление таблицы не производится
            mQuery->prepare("SELECT number FROM " + DBName + ".tdata_current_" + QString::number(ObjectID) +
                            " WHERE datemessage = :A AND timemessage = :B AND ObjectID = :C AND "
                            " ROUND(AI1) = :D AND ROUND(AI2) = :F AND ROUND(AI3) = :E AND ROUND(AI4) = :J "
                            " AND ROUND(AI5) = :K AND ROUND(AI6) = :L "
                            " AND ROUND(AI7) = :M AND ROUND(AI8) = :N AND number >0;");
            mQuery->bindValue(":A", date);
            mQuery->bindValue(":B", time);
            mQuery->bindValue(":C", ObjectID);
            mQuery->bindValue(":D", qRound(AI1));
            mQuery->bindValue(":F", qRound(AI2));
            mQuery->bindValue(":E", qRound(AI3));
            mQuery->bindValue(":J", qRound(AI4));
            mQuery->bindValue(":K", qRound(AI5));
            mQuery->bindValue(":L", qRound(AI6));
            mQuery->bindValue(":M", qRound(AI7));
            mQuery->bindValue(":N", qRound(AI8));
            if(!mQuery->exec())
            {
                // Проверка не удалась, считается что обновление не производилось
                qDebug() << "CLampThread:SetCurrentData: Не удалось проверить наличие обновления данных 7017:"
                         << mQuery->lastError();
            }
            // Запись не найдена, т.е. другими операторами не производилась
            if(mQuery->size() <= 0)
            {
                mQuery->prepare("UPDATE " + DBName + ".tdata_current_" + QString::number(ObjectID) + " SET AI1 = :A, AI2 = :B, "
                    "AI3 = :C, AI4 = :D, AI5 = :E, AI6 = :F, AI7 = :G, AI8 = :H, ErrorCode = :I, CommandID =:J "
                    "WHERE (number > 0 AND datemessage < :K) OR (number > 0 AND datemessage = :K AND timemessage <= :L);");
                mQuery->bindValue(":A", AI1);
                mQuery->bindValue(":B", AI2);
                mQuery->bindValue(":C", AI3);
                mQuery->bindValue(":D", AI4);
                mQuery->bindValue(":E", AI5);
                mQuery->bindValue(":F", AI6);
                mQuery->bindValue(":G", AI7);
                mQuery->bindValue(":H", AI8);
                mQuery->bindValue(":I", ErrorCode);
                mQuery->bindValue(":J", CommandID);
                mQuery->bindValue(":K", date);
                mQuery->bindValue(":L", time);
                if (!mQuery->exec())
                {
                    qDebug() << "CLampThread:SetCurrentData: Не удалось записать текущие параметры (I-7017)" << mQuery->lastError();
                    emit SendLastErrorDB("ОШИБКА ЗАПИСИ ТЕКУЩИХ ПАРАМЕТРОВ");
                    delete mQuery;
                    return false;
                }
                emit SendDeleteTextLastErrorDB("ОШИБКА ЗАПИСИ ТЕКУЩИХ ПАРАМЕТРОВ");
                // Запись ID - объекта и временных показателей в таблицу текущих параметров
                mQuery->prepare("UPDATE " + DBName + ".tdata_current_" + QString::number(ObjectID) +
                                " SET datemessage = :A, timemessage = :B, ObjectID = :C WHERE "
                                "(number > 0 AND datemessage < :A) OR (number > 0 AND datemessage = :A AND timemessage <= :B);");
                mQuery->bindValue(":A", date);
                mQuery->bindValue(":B", time);
                mQuery->bindValue(":C", ObjectID);
                if (!mQuery->exec())
                {
                    qDebug() << "CLampThread:SetCurrentData: Не удалось записать текущие параметры (I-7017)" << mQuery->lastError();
                    emit SendLastErrorDB("ОШИБКА ЗАПИСИ ВРЕМЕНИ ТЕКУЩИХ ПАРАМЕТРОВ");
                    delete mQuery;
                    return false;
                }
                emit SendDeleteTextLastErrorDB("ОШИБКА ЗАПИСИ ВРЕМЕНИ ТЕКУЩИХ ПАРАМЕТРОВ");
                // Запись данных и ErrorCode в таблицу архивных параметров
                mVer = "INSERT INTO " + DBName + ".tdata_" + QString::number(ObjectID) + " (AI1, AI2, "
                    "AI3, AI4, AI5, AI6, AI7, AI8, ErrorCode, CommandID) VALUES (%1, %2, %3, %4, %5, %6, %7, %8, %9, %10);";
                mV = mVer.arg(QString::number(AI1))
                        .arg(QString::number(AI2))
                        .arg(QString::number(AI3))
                        .arg(QString::number(AI4))
                        .arg(QString::number(AI5))
                        .arg(QString::number(AI6))
                        .arg(QString::number(AI7))
                        .arg(QString::number(AI8))
                        .arg(QString::number(ErrorCode))
                        .arg(QString::number(CommandID));
                if (!mQuery->exec(mV))
                {
                    qDebug() << "CLampThread:SetCurrentData: Не удалось записать архивные параметры (I-7017)" << mQuery->lastError();
                    emit SendLastErrorDB("ОШИБКА ЗАПИСИ АРХИВНЫХ ПАРАМЕТРОВ");
                    delete mQuery;
                    return false;
                }
                emit SendDeleteTextLastErrorDB("ОШИБКА ЗАПИСИ АРХИВНЫХ ПАРАМЕТРОВ");
                // Запись ID - объекта и временных показателей
                mQuery->prepare("UPDATE " + DBName + ".tdata_" + QString::number(ObjectID) +
                                " SET datemessage = :A, timemessage = :B, ObjectID = :C WHERE "
                                "number = (SELECT * FROM (SELECT MAX(number) FROM " + DBName + ".tdata_" + QString::number(ObjectID) + ") AS t);");
                mQuery->bindValue(":A", date);
                mQuery->bindValue(":B", time);
                mQuery->bindValue(":C", ObjectID);
                if (!mQuery->exec())
                {
                    qDebug() << "CLampThread:SetCurrentData: Не удалось записать архивные параметры (I-7017)" << mQuery->lastError();
                    emit SendLastErrorDB("ОШИБКА ЗАПИСИ ВРЕМЕНИ АРХИВНЫХ ПАРАМЕТРОВ");
                    delete mQuery;
                    return false;
                }
                emit SendDeleteTextLastErrorDB("ОШИБКА ЗАПИСИ ВРЕМЕНИ АРХИВНЫХ ПАРАМЕТРОВ");
                delete mQuery;
                return true;
            }
            else
            {
                qDebug() << "CLampThread:SetCurrentData: Найдена запись аналогичных параметров (7017).\r\n"
                            "Запись не производится т.к. записана другим оператором.";
            }
            delete mQuery;
            return true;
        }

        if(CommandID ==2) // I-7041
        {
            // Проверка наличия столбцов в таблице текущих значений и если таковых нет, то добавление
            mVer = "SELECT DI14_7041 FROM " + DBName + ".tdata_current_" + QString::number(ObjectID) + ";";
            if (!mQuery->exec(mVer))
            {
                qDebug() << "Не удалось выполнить запрос столбцов в текущих значениях" << mQuery->lastError();
             //   delete mQuery;
             //   return false;
            }
            Count = mQuery->size();
            // Столбцов не найдено
            if(Count <=0)
            {
                mVer = "ALTER TABLE " + DBName + ".tdata_current_" + QString::number(ObjectID) + " ADD DI14_7041 SMALLINT UNSIGNED;";
                if (!mQuery->exec(mVer))
                {
                    qDebug() << "Не удалось добавить столбцы в таблицу текущих значений (I-7041)" << mQuery->lastError();
                    emit SendLastErrorDB("ОШИБКА ДОБАВЛЕНИЯ СТОЛБЦОВ В ТАБЛИЦУ ТЕКУЩИХ ПАРАМЕТРОВ");
                    delete mQuery;
                    return false;
                }
                emit SendDeleteTextLastErrorDB("ОШИБКА ДОБАВЛЕНИЯ СТОЛБЦОВ В ТАБЛИЦУ ТЕКУЩИХ ПАРАМЕТРОВ");
                qDebug() << "Столбцы добавлены в таблицу текущих параметров (I-7041)";
            }
            // От 01.06.2015 - нужны значения поименно каждого параметра
            // Проверка наличия столбцов в таблице текущих значений и если таковых нет, то добавление
            mVer = "SELECT DI1, DI2, DI3, DI4, DI5, DI6, DI7, DI8, DI9, DI10, DI11, DI12"
                   " DI13, DI14  FROM " + DBName + ".tdata_current_" + QString::number(ObjectID) + ";";
            if (!mQuery->exec(mVer))
            {
                qDebug() << "Не удалось выполнить запрос столбцов DI1-DI14 в текущих значениях" << mQuery->lastError();
             //   delete mQuery;
             //   return false;
            }
            Count = mQuery->size();
            // Столбцов не найдено
            if(Count <=0)
            {
                mVer = "ALTER TABLE " + DBName + ".tdata_current_" + QString::number(ObjectID) + " ADD DI1 TINYINT UNSIGNED, "
                        "ADD DI2 TINYINT UNSIGNED, ADD DI3 TINYINT UNSIGNED, ADD DI4 TINYINT UNSIGNED, ADD DI5 TINYINT UNSIGNED, "
                        "ADD DI6 TINYINT UNSIGNED, ADD DI7 TINYINT UNSIGNED, ADD DI8 TINYINT UNSIGNED, ADD DI9 TINYINT UNSIGNED, "
                        "ADD DI10 TINYINT UNSIGNED, ADD DI11 TINYINT UNSIGNED, ADD DI12 TINYINT UNSIGNED, ADD DI13 TINYINT UNSIGNED, "
                        "ADD DI14 TINYINT UNSIGNED;";
                if (!mQuery->exec(mVer))
                {
                    qDebug() << "Не удалось добавить столбцы в таблицу текущих значений (I-7041)" << mQuery->lastError();
                    emit SendLastErrorDB("ОШИБКА ДОБАВЛЕНИЯ СТОЛБЦОВ В ТАБЛИЦУ ТЕКУЩИХ ПАРАМЕТРОВ");
                    delete mQuery;
                    return false;
                }
                emit SendDeleteTextLastErrorDB("ОШИБКА ДОБАВЛЕНИЯ СТОЛБЦОВ В ТАБЛИЦУ ТЕКУЩИХ ПАРАМЕТРОВ");
                qDebug() << "Столбцы DI1-DI14 добавлены в таблицу текущих параметров (I-7041)";
            }
            // Проверка наличия столбцов в таблице архива и если таковых нет, то добавление
            mVer = "SELECT DI14_7041 FROM " + DBName + ".tdata_" + QString::number(ObjectID) + ";";
            if (!mQuery->exec(mVer))
            {
                qDebug() << "Не удалось выполнить запрос столбцов в архивных значениях" << mQuery->lastError();
             //   delete mQuery;
             //   return false;
            }
            Count = mQuery->size();
            // Столбцов не найдено
            if(Count <=0)
            {
                mVer = "ALTER TABLE " + DBName + ".tdata_" + QString::number(ObjectID) + " ADD DI14_7041 SMALLINT UNSIGNED;";
                if (!mQuery->exec(mVer))
                {
                    qDebug() << "Не удалось добавить столбцы в таблицу архива (I-7041)" << mQuery->lastError();
                    emit SendLastErrorDB("ОШИБКА ДОБАВЛЕНИЯ СТОЛБЦОВ В ТАБЛИЦУ АРХИВНЫХ ПАРАМЕТРОВ");
                    delete mQuery;
                    return false;
                }
                emit SendDeleteTextLastErrorDB("ОШИБКА ДОБАВЛЕНИЯ СТОЛБЦОВ В ТАБЛИЦУ АРХИВНЫХ ПАРАМЕТРОВ");
                qDebug() << "Столбцы добавлены в таблицу архива (I-7041)";
            }
            // От 01.06.2015 добавлены DI1-DI14 столбцы для каждого параметра по отдельности
            // Проверка наличия столбцов в таблице архива и если таковых нет, то добавление
            mVer = "SELECT DI1, DI2, DI3, DI4, DI5, DI6, DI7, DI8, DI9, DI10, DI11, DI12"
                   " DI13, DI14 FROM " + DBName + ".tdata_" + QString::number(ObjectID) + ";";
            if (!mQuery->exec(mVer))
            {
                qDebug() << "Не удалось выполнить запрос столбцов DI1-DI14 в архивных значениях" << mQuery->lastError();
             //   delete mQuery;
             //   return false;
            }
            Count = mQuery->size();
            // Столбцов не найдено
            if(Count <=0)
            {
                mVer = "ALTER TABLE " + DBName + ".tdata_" + QString::number(ObjectID) + " ADD DI1 TINYINT UNSIGNED, "
                       "ADD DI2 TINYINT UNSIGNED, ADD DI3 TINYINT UNSIGNED, ADD DI4 TINYINT UNSIGNED, ADD DI5 TINYINT UNSIGNED, "
                       "ADD DI6 TINYINT UNSIGNED, ADD DI7 TINYINT UNSIGNED, ADD DI8 TINYINT UNSIGNED, ADD DI9 TINYINT UNSIGNED, "
                       "ADD DI10 TINYINT UNSIGNED, ADD DI11 TINYINT UNSIGNED, ADD DI12 TINYINT UNSIGNED, ADD DI13 TINYINT UNSIGNED, "
                       "ADD DI14 TINYINT UNSIGNED;";
                if (!mQuery->exec(mVer))
                {
                    qDebug() << "Не удалось добавить столбцы DI1-DI14 в таблицу архива (I-7041)" << mQuery->lastError();
                    emit SendLastErrorDB("ОШИБКА ДОБАВЛЕНИЯ СТОЛБЦОВ В ТАБЛИЦУ АРХИВНЫХ ПАРАМЕТРОВ");
                    delete mQuery;
                    return false;
                }
                emit SendDeleteTextLastErrorDB("ОШИБКА ДОБАВЛЕНИЯ СТОЛБЦОВ В ТАБЛИЦУ АРХИВНЫХ ПАРАМЕТРОВ");
                qDebug() << "Столбцы добавлены в таблицу архива (I-7041)";
            }
            // Проверка наличия строк настройки параметров
            mVer = "SELECT * FROM " + DBName + ".tsetup_" + QString::number(ObjectID) + " WHERE parameter = 'DI1' OR "
                   " parameter = 'DI2' OR parameter = 'DI3' OR parameter = 'DI4' OR parameter = 'DI5' OR parameter = 'DI6' "
                   " OR parameter = 'DI7' OR parameter = 'DI8' OR parameter = 'DI9' OR parameter = 'DI10' OR parameter = 'DI11' "
                   " OR parameter = 'DI12' OR parameter = 'DI13' OR parameter = 'DI14';";
            if (!mQuery->exec(mVer))
            {
                qDebug() << "Не удалось выполнить запрос строк в таблице настройки параметров I-7041" << mQuery->lastError();
             //   delete mQuery;
             //   return false;
            }
            Count = mQuery->size();
            // Строк не найдено, добавление строк DI сигналов
            if(Count <=0)
            {
                for(int i=0; i < 14; i++)
                {
                    mVer = "INSERT INTO " + DBName + ".tsetup_" + QString::number(ObjectID) + " (parameter, Ed_Izm, Minimum_sensor, "
                        "Maximum_sensor, Minimum_param, Maximum_param, Alarm_border_MIN, Alarm_border_MAX, Alarm_set, Alarm_text) "
                        "VALUES ('DI" + QString::number(i+1) + "','да/нет' , 0, 1, 0, 1, 0, 0, 0,'Сообщение не задано');";
                    if (!mQuery->exec(mVer))
                    {
                        qDebug() << "Не удалось добавить строки настройки параметров (I-7041)" << mQuery->lastError();
                        emit SendLastErrorDB("ОШИБКА ЗАПИСИ СТРОК НАСТРОЕЧНЫХ ПАРАМЕТРОВ");
                        delete mQuery;
                        return false;
                    }
                    emit SendDeleteTextLastErrorDB("ОШИБКА ЗАПИСИ СТРОК НАСТРОЕЧНЫХ ПАРАМЕТРОВ");
                    qDebug() << "Строки добавлены в таблицу настройки параметров (I-7041)";
                }
            }

            // Переменные для записи
            quint16 DI14_7041 =0;
            quint8 DI1 =0, DI2 =0, DI3 =0, DI4 =0, DI5 =0, DI6 =0, DI7 =0, DI8 =0,
                   DI9 =0, DI10 =0, DI11 =0, DI12 =0, DI13 =0, DI14 =0;
            if(ErrorCode==(quint8)NOERRORS)
            {
                // Начинается с '!' и длина 8 байт
                if((PlcData.indexOf("!")!=-1)&(PlcData.size() == 8))
                {
                    QByteArray takeOne;
                    takeOne.clear();
                    PlcData.remove(0,1);
                    takeOne = PlcData.left(2);
                    PlcData.remove(0,2);
                    quint16 data = takeOne.toUInt(0,16); // 8-13 входы 0-3F
                    takeOne.clear();
                    takeOne = PlcData.left(2);
                    DI14_7041 = takeOne.toUInt(0,16);    // 0-7 входы 0-FF
                    DI14_7041 <<=8;
                    DI14_7041 += data;                   // Первый байт числа 0-7, второй 8-13
                    quint16 DI = DI14_7041;
                    // Проверка параметра
                    IsAlarm(db, ObjectID, (DI & 256)/256,     "DI1",  date, time);
                    IsAlarm(db, ObjectID, (DI & 512)/512,     "DI2",  date, time);
                    IsAlarm(db, ObjectID, (DI & 1024)/1024,   "DI3",  date, time);
                    IsAlarm(db, ObjectID, (DI & 2048)/2048,   "DI4",  date, time);
                    IsAlarm(db, ObjectID, (DI & 4096)/4096,   "DI5",  date, time);
                    IsAlarm(db, ObjectID, (DI & 8192)/8192,   "DI6",  date, time);
                    IsAlarm(db, ObjectID, (DI & 16384)/16384, "DI7",  date, time);
                    IsAlarm(db, ObjectID, (DI & 32768)/32768, "DI8",  date, time);
                    IsAlarm(db, ObjectID, (DI & 1)/1,         "DI9",  date, time);
                    IsAlarm(db, ObjectID, (DI & 2)/2,         "DI10", date, time);
                    IsAlarm(db, ObjectID, (DI & 4)/4,         "DI11", date, time);
                    IsAlarm(db, ObjectID, (DI & 8)/8,         "DI12", date, time);
                    IsAlarm(db, ObjectID, (DI & 16)/16,       "DI13", date, time);
                    IsAlarm(db, ObjectID, (DI & 32)/32,       "DI14", date, time);
                    // Установка параметров для записи
                    DI1 = (DI & 256)/256;
                    DI2 = (DI & 512)/512;
                    DI3 = (DI & 1024)/1024;
                    DI4 = (DI & 2048)/2048;
                    DI5 = (DI & 4096)/4096;
                    DI6 = (DI & 8192)/8192;
                    DI7 = (DI & 16384)/16384;
                    DI8 = (DI & 32768)/32768;
                    DI9 = (DI & 1)/1;
                    DI10= (DI & 2)/2;
                    DI11= (DI & 4)/4;
                    DI12= (DI & 8)/8;
                    DI13= (DI & 16)/16;
                    DI14= (DI & 32)/32;
                }
                else
                {
                    qDebug() << "Данные I-7041 не распознаны";
                }
            }
            // Проверка обновлены ли данные другим оператором (ИЗМЕНЕНИЕ ОТ 24.05.2015)
            // Если такая запись есть, то обновление таблицы не производится
            mQuery->prepare("SELECT number FROM " + DBName + ".tdata_current_" + QString::number(ObjectID) +
                            " WHERE datemessage = :A AND timemessage = :B AND ObjectID = :C AND DI14_7041 = :D AND number >0;");
            mQuery->bindValue(":A", date);
            mQuery->bindValue(":B", time);
            mQuery->bindValue(":C", ObjectID);
            mQuery->bindValue(":D", DI14_7041);
            if(!mQuery->exec())
            {
                // Проверка не удалась, считается что обновление не производилось
                qDebug() << "CLampThread:SetCurrentData: Не удалось проверить наличие обновления данных 7041:"
                         << mQuery->lastError();
            }
            // Запись не найдена, т.е. другими операторами не производилась
            if(mQuery->size() <= 0)
            {
                // Запись данных и ErrorCode в таблицу текщих параметров
                mQuery->prepare("UPDATE " + DBName + ".tdata_current_" + QString::number(ObjectID) + " SET DI14_7041 = :A, ErrorCode = :B, CommandID = :C "
                       "WHERE (number > 0 AND datemessage < :D) OR (number > 0 AND datemessage = :D AND timemessage <= :E);");
                mQuery->bindValue(":A", DI14_7041);
                mQuery->bindValue(":B", ErrorCode);
                mQuery->bindValue(":C", CommandID);
                mQuery->bindValue(":D", date);
                mQuery->bindValue(":E", time);
                if (!mQuery->exec())
                {
                    qDebug() << "Не удалось записать текущие параметры (I-7041)" << mQuery->lastError();
                    emit SendLastErrorDB("ОШИБКА ЗАПИСИ ТЕКУЩИХ ПАРАМЕТРОВ");
                    delete mQuery;
                    return false;
                }
                emit SendDeleteTextLastErrorDB("ОШИБКА ЗАПИСИ ТЕКУЩИХ ПАРАМЕТРОВ");
                // Запись ID - объекта, временных показателей текущих параметров
                mQuery->prepare("UPDATE " + DBName + ".tdata_current_" + QString::number(ObjectID) +
                                " SET datemessage = :A, timemessage = :B, ObjectID = :C WHERE "
                                "(number > 0 AND datemessage < :A) OR (number > 0 AND datemessage = :A AND timemessage <= :B);");
                mQuery->bindValue(":A", date);
                mQuery->bindValue(":B", time);
                mQuery->bindValue(":C", ObjectID);
                mQuery->bindValue(":D", date);
                mQuery->bindValue(":E", time);
                if (!mQuery->exec())
                {
                    qDebug() << "Не удалось записать текущие параметры (I-7041)" << mQuery->lastError();
                    emit SendLastErrorDB("ОШИБКА ЗАПИСИ ВРЕМЕНИ ТЕКУЩИХ ПАРАМЕТРОВ");
                    delete mQuery;
                    return false;
                }
                emit SendDeleteTextLastErrorDB("ОШИБКА ЗАПИСИ ВРЕМЕНИ ТЕКУЩИХ ПАРАМЕТРОВ");
                // От 01.06.2015 запись каждого DI1-DI14 параметра по отдельности
                // Запись параметров
                mQuery->prepare("UPDATE " + DBName + ".tdata_current_" + QString::number(ObjectID) +
                                " SET DI1 = :A, DI2 = :B, DI3 = :C, DI4 = :D, DI5 = :E, DI6 = :F,"
                                " DI7 = :G, DI8 = :H, DI9 = :I, DI10 = :J, DI11 = :K, DI12 = :L,"
                                " DI13 = :M, DI14 = :N WHERE (number > 0 AND datemessage < :O)"
                                " OR (number > 0 AND datemessage = :O AND timemessage <= :P);");
                mQuery->bindValue(":A", DI1);
                mQuery->bindValue(":B", DI2);
                mQuery->bindValue(":C", DI3);
                mQuery->bindValue(":D", DI4);
                mQuery->bindValue(":E", DI5);
                mQuery->bindValue(":F", DI6);
                mQuery->bindValue(":G", DI7);
                mQuery->bindValue(":H", DI8);
                mQuery->bindValue(":I", DI9);
                mQuery->bindValue(":J", DI10);
                mQuery->bindValue(":K", DI11);
                mQuery->bindValue(":L", DI12);
                mQuery->bindValue(":M", DI13);
                mQuery->bindValue(":N", DI14);
                mQuery->bindValue(":O", date);
                mQuery->bindValue(":P", time);
                if (!mQuery->exec())
                {
                    qDebug() << "Не удалось записать текущие параметры DI1-DI14 (I-7041)" << mQuery->lastError();
                    emit SendLastErrorDB("ОШИБКА ЗАПИСИ ТЕКУЩИХ ПАРАМЕТРОВ");
                    delete mQuery;
                    return false;
                }
                emit SendDeleteTextLastErrorDB("ОШИБКА ЗАПИСИ ТЕКУЩИХ ПАРАМЕТРОВ");
                // Запись данных и ErrorCode в таблицу архива
                mVer = "INSERT INTO " + DBName + ".tdata_" + QString::number(ObjectID) + " (DI14_7041, ErrorCode, CommandID) "
                       "VALUES (%1, %2, %3);";
                mV = mVer.arg(QString::number(DI14_7041))
                        .arg(QString::number(ErrorCode))
                        .arg(QString::number(CommandID));
                if (!mQuery->exec(mV))
                {
                    qDebug() << "Не удалось записать архивные параметры (I-7041)" << mQuery->lastError();
                    emit SendLastErrorDB("ОШИБКА ЗАПИСИ АРХИВНЫХ ПАРАМЕТРОВ");
                    delete mQuery;
                    return false;
                }
                emit SendDeleteTextLastErrorDB("ОШИБКА ЗАПИСИ АРХИВНЫХ ПАРАМЕТРОВ");
                // Запись ID - объекта, временных показателей архивных параметров
                mQuery->prepare("UPDATE " + DBName + ".tdata_" + QString::number(ObjectID) +
                                " SET datemessage = :A, timemessage = :B, ObjectID = :C WHERE "
                                "number = (SELECT * FROM (SELECT MAX(number) FROM " + DBName + ".tdata_" + QString::number(ObjectID) + " ) AS t);");
                mQuery->bindValue(":A", date);
                mQuery->bindValue(":B", time);
                mQuery->bindValue(":C", ObjectID);
                if (!mQuery->exec())
                {
                    qDebug() << "Не удалось записать архивные параметры (I-7041)" << mQuery->lastError();
                    emit SendLastErrorDB("ОШИБКА ЗАПИСИ ВРЕМЕНИ АРХИВНЫХ ПАРАМЕТРОВ");
                    delete mQuery;
                    return false;
                }
                emit SendDeleteTextLastErrorDB("ОШИБКА ЗАПИСИ ВРЕМЕНИ АРХИВНЫХ ПАРАМЕТРОВ");
                // От 01.06.2015 запись каждого DI1-DI14 параметра по отдельности
                // Запись параметров
                mQuery->prepare("UPDATE " + DBName + ".tdata_" + QString::number(ObjectID) +
                                " SET DI1 = :A, DI2 = :B, DI3 = :C, DI4 = :D, DI5 = :E, DI6 = :F,"
                                " DI7 = :G, DI8 = :H, DI9 = :I, DI10 = :J, DI11 = :K, DI12 = :L,"
                                " DI13 = :M, DI14 = :N WHERE number = (SELECT * FROM (SELECT MAX(number)"
                                " FROM " + DBName + ".tdata_" + QString::number(ObjectID) + " ) AS t);");
                mQuery->bindValue(":A", DI1);
                mQuery->bindValue(":B", DI2);
                mQuery->bindValue(":C", DI3);
                mQuery->bindValue(":D", DI4);
                mQuery->bindValue(":E", DI5);
                mQuery->bindValue(":F", DI6);
                mQuery->bindValue(":G", DI7);
                mQuery->bindValue(":H", DI8);
                mQuery->bindValue(":I", DI9);
                mQuery->bindValue(":J", DI10);
                mQuery->bindValue(":K", DI11);
                mQuery->bindValue(":L", DI12);
                mQuery->bindValue(":M", DI13);
                mQuery->bindValue(":N", DI14);
                if (!mQuery->exec())
                {
                    qDebug() << "Не удалось записать архивные параметры DI1-DI14 (I-7041)" << mQuery->lastError();
                    emit SendLastErrorDB("ОШИБКА ЗАПИСИ АРХИВНЫХ ПАРАМЕТРОВ");
                    delete mQuery;
                    return false;
                }
                emit SendDeleteTextLastErrorDB("ОШИБКА ЗАПИСИ АРХИВНЫХ ПАРАМЕТРОВ");
                delete mQuery;
                return true;
            }
            else
            {
                qDebug() << "CLampThread:SetCurrentData: Найдена запись аналогичных параметров (7041).\r\n"
                            "Запись не производится т.к. записана другим оператором.";
            }
            delete mQuery;
            return true;
        }
    }
    // Запись в таблицу ошибок (служит только для диагностики правильности составления команд, а также для проверки канала связи)
    else if(Funct ==3)
    {

    }
    delete mQuery;
    return true;
}
// Добавление таблицы текущих данных объекта
bool CListenThread::SetObjectDataCurrent(QSqlDatabase *db, int ObjectID)
{
    if(!db->open())return false; // Если БД не открыта
    QSqlQuery *mQuery = new QSqlQuery(*db);
    // Получаются имена таблиц в БД
    QStringList mTableList = db->tables();
    bool IsObjects = false;
    foreach (const QString &mObjects, mTableList)
    {
        if(mObjects.contains("tdata_current_" + QString::number(ObjectID))){ IsObjects = true; break; }
    }
    if(IsObjects)
    {
    //    qDebug() << "Таблица текущих данных объекта уже создана\r\n";
        delete mQuery;
        return IsObjects;
    }
    // Добавление таблицы текущих параметров в БД
    QString str = "CREATE TABLE tdata_current_" + QString::number(ObjectID) + " ( "
                  "number INTEGER PRIMARY KEY AUTO_INCREMENT, "
                  "ObjectID   TINYINT UNSIGNED, "
                  "CommandID TINYINT UNSIGNED,"
                  "datemessage DATE, "
                  "timemessage TIME, "
                  "ErrorCode TINYINT UNSIGNED "
                                    ");";
    if (!mQuery->exec(str))
    {
        qDebug() << "Не удалось создать таблицу текущих данных объекта" << mQuery->lastError();
        emit SendLastErrorDB("ОШИБКА СОЗДАНИЯ ТАБЛИЦЫ ТЕКУЩИХ ПАРАМЕТРОВ");
        delete mQuery;
        return false;
    }
    emit SendDeleteTextLastErrorDB("ОШИБКА СОЗДАНИЯ ТАБЛИЦЫ ТЕКУЩИХ ПАРАМЕТРОВ");
    str = "INSERT INTO " + DBName + ".tdata_current_" + QString::number(ObjectID) + " (ObjectID, CommandID, datemessage, timemessage) "
          "VALUES (NULL, NULL, NULL, NULL);";
    if (!mQuery->exec(str))
    {
        qDebug() << "Не удалось записать рабочую строку в таблицу текущих параметров" << mQuery->lastError();
        emit SendLastErrorDB("ОШИБКА ЗАПИСИ ОСНОВНОЙ СТРОКИ В ТАБЛИЦУ ТЕКУЩИХ ПАРАМЕТРОВ");
        delete mQuery;
        return false;
    }
    emit SendDeleteTextLastErrorDB("ОШИБКА ЗАПИСИ ОСНОВНОЙ СТРОКИ В ТАБЛИЦУ ТЕКУЩИХ ПАРАМЕТРОВ");
    delete mQuery;
    return true;
}

// Добавление таблицы архивных данных объекта
bool CListenThread::SetObjectDataTable(QSqlDatabase *db, int ObjectID)
{
    if(!db->open())return false; // Если БД не открыта
    QSqlQuery *mQuery = new QSqlQuery(*db);
    // Получаются имена таблиц в БД
    QStringList mTableList = db->tables();
    bool IsObjects = false;
    foreach (const QString &mObjects, mTableList)
    {
        if(mObjects.contains("tdata_" + QString::number(ObjectID))){ IsObjects = true; break; }
    }
    if(IsObjects)
    {
    //    qDebug() << "Таблица архива данных текущего объекта уже создана\r\n";
        delete mQuery;
        return IsObjects;
    }
    // Добавление таблицы данных в БД
    QString str = "CREATE TABLE tdata_" + QString::number(ObjectID) + " ( "
                  "number INTEGER PRIMARY KEY AUTO_INCREMENT, "
                  "ObjectID   TINYINT UNSIGNED, "
                  "CommandID TINYINT UNSIGNED,"
                  "datemessage DATE, "
                  "timemessage TIME, "
                  "ErrorCode TINYINT UNSIGNED "
                                                ");";
    if (!mQuery->exec(str))
    {
        qDebug() << "Не удалось создать таблицу данных объекта" << mQuery->lastError();
        emit SendLastErrorDB("ОШИБКА СОЗДАНИЯ ТАБЛИЦЫ АРХИВНЫХ ПАРАМЕТРОВ");
        delete mQuery;
        return false;
    }
    emit SendDeleteTextLastErrorDB("ОШИБКА СОЗДАНИЯ ТАБЛИЦЫ АРХИВНЫХ ПАРАМЕТРОВ");
    delete mQuery;
    return true;
}

// Добавление таблицы настроечных данных объекта
bool CListenThread::SetSetupDataTable(QSqlDatabase *db, int ObjectID)
{
    if(!db->open())return false; // Если БД не открыта
    QSqlQuery *mQuery = new QSqlQuery(*db);
    // Получаются имена таблиц в БД
    QStringList mTableList = db->tables();
    // Поиск таблицы пользователей
    bool IsTable = false;
    foreach (const QString &mUsers, mTableList)
    {
        if(mUsers.contains("tsetup_" + QString::number(ObjectID))){ IsTable = true; break; }
    }
    //Если таблицы нет, тогда создание таблицы
    if(!IsTable)
    {
        QString mVer = "CREATE TABLE " + DBName + ".tsetup_" + QString::number(ObjectID) + " ( "
               "number INTEGER PRIMARY KEY AUTO_INCREMENT, "
               "parameter   VARCHAR(100), "
               "Ed_Izm VARCHAR(10), "
               "Minimum_sensor FLOAT, "
               "Maximum_sensor FLOAT, "
               "Minimum_param FLOAT, "
               "Maximum_param FLOAT, "
               "Alarm_border_MIN FLOAT,"
               "Alarm_border_MAX FLOAT,"
               "Alarm_set TINYINT UNSIGNED,"
               "Alarm_text VARCHAR(200)"
               ");";
        if (!mQuery->exec(mVer))
        {
            qDebug() << "Таблица настроек параметров объекта не создана" << mQuery->lastError().text();
            emit SendLastErrorDB("ОШИБКА СОЗДАНИЯ ТАБЛИЦЫ НАСТРОЙКИ ПАРАМЕТРОВ");
            delete mQuery;
            return false;
        }
        emit SendDeleteTextLastErrorDB("ОШИБКА СОЗДАНИЯ ТАБЛИЦЫ НАСТРОЙКИ ПАРАМЕТРОВ");
    }
    delete mQuery;
    return true;
}
// Проверка наличия тревоги по данному текущему параметру
bool CListenThread::IsAlarm(QSqlDatabase *db, unsigned int ObjectID, float CurrentParam, QString NameParam, QDate date, QTime time)
{
    if(!db->open())return false; // Если БД не открыта
    QSqlQuery *mQuery = new QSqlQuery(*db);
    // Получаются имена таблиц в БД
    QStringList mTableList = db->tables();
    // Поиск таблицы настройки параметров
    bool IsTable = false;
    foreach (const QString &mUsers, mTableList)
    {
        if(mUsers.contains("tsetup_" + QString::number(ObjectID))){ IsTable = true; break; }
    }
    // Таблица настройки параметров отсутствует
    if(!IsTable)
    {
        qDebug() << "CListenThread::IsAlarm: Параметры сообщений не настроены, таблица настроек отсутствует";
        delete mQuery;
        return false;
    }
    QString mVer = "SELECT * FROM " + DBName + ".tsetup_" + QString::number(ObjectID) + " WHERE parameter = '" + NameParam + "';";
    if(!mQuery->exec(mVer))
    {
        qDebug() << "CListenThread::IsAlarm: Параметры сообщений не настроены, запрос настроек не выполнен";
        emit SendLastErrorDB("ОШИБКА ЧТЕНИЯ НАСТРОЕЧНЫХ ПАРАМЕТРОВ");
        delete mQuery;
        return false;
    }
    emit SendDeleteTextLastErrorDB("ОШИБКА ЧТЕНИЯ НАСТРОЕЧНЫХ ПАРАМЕТРОВ");
    int Count = mQuery->size();
    QSqlRecord rec = mQuery->record();
    if(Count <= 0)
    {
        qDebug() << "Параметры сообщений не настроены, в таблице отсутствуют настройки параметров";
        delete mQuery;
        return false;
    }
    mQuery->next();
    CParam LastParam;
    LastParam.AlarmMIN = mQuery->value(rec.indexOf("Alarm_border_MIN")).toFloat();
    LastParam.AlarmMAX = mQuery->value(rec.indexOf("Alarm_border_MAX")).toFloat();
    LastParam.ParamName = NameParam;
    LastParam.SensorMIN = mQuery->value(rec.indexOf("Minimum_sensor")).toFloat();
    LastParam.SensorMAX = mQuery->value(rec.indexOf("Maximum_sensor")).toFloat();
    LastParam.ParamMIN = mQuery->value(rec.indexOf("Minimum_param")).toFloat();
    LastParam.ParamMAX = mQuery->value(rec.indexOf("Maximum_param")).toFloat();
    LastParam.IsAlarm = mQuery->value(rec.indexOf("Alarm_set")).toUInt();          // Нужен ли вывод сообщений из настроек
    LastParam.IdObject = ObjectID;
    LastParam.AlarmFlag = 2;                                                       // Состояние неизвестно
    QString AlarmText = mQuery->value(rec.indexOf("Alarm_text")).toString();
    // Вычисление невозможно
    if((LastParam.ParamMAX == LastParam.ParamMIN)|(LastParam.SensorMAX == LastParam.SensorMIN))
    {
        qDebug() << "Параметры сообщений не настроены, вычислить значение невозможно";
        delete mQuery;
        return false;
    }
    // Преобразование параметра датчика в значение
    CurrentParam = (((CurrentParam - LastParam.SensorMIN) * (LastParam.ParamMAX - LastParam.ParamMIN)) / (LastParam.SensorMAX - LastParam.SensorMIN)) + LastParam.ParamMIN;
    // Если за пределами и выставлен флаг сообщений тогда на вывод диалога
    if(((CurrentParam >= LastParam.AlarmMAX)|(CurrentParam <= LastParam.AlarmMIN)) & (LastParam.AlarmFlag!=0))
    {
// 18.11.2015 - установка флага тревоги
        // Если в базе данных выставлен флаг срабатывания (уже сработал до этого) то возвращается 0  и второй раз не ставится
        // если же в базе данных нет информации о срабатывании, то нужно выполнить установку флага с учетом, что текущие данные
        // являются наиболее свежими так как на сервере не копятся больше необработанные данные в tplcresult
        if(IsNotFlagSetted(db, LastParam.ParamName, LastParam.IdObject))
        {
            // Проверка того установленно ли данное сообщение не производится
            // 1. Выписка последнего актуального параметра
// 23.11.2015 - убрано получение последнего параметра
            //bool IsOk = GetActualParam(db, &LastParam);
            // 2. Запись флага тревоги
            // Данные сервера для записи (т.к. могут работать сразу несколько операторов, то смысла писать одного из них нет)
            QString Name = "Сервер";
            QString LastName = "Сервер";
            QString Patronymic = "Сервер";
            QString ServerLogin = "root";
            quint8 Flag = 1;
// 23.11.2015 Убрано добавление столбца DateTime
            QDateTime getDateTime;
            getDateTime.setDate(date);
            getDateTime.setTime(time);

            mQuery->prepare("INSERT INTO " + DBName + ".talarmmessages (ObjectID, AlarmText, Parameter, Current, Border_MIN, Border_MAX, "
                            "DateAlarm, TimeAlarm, OperatorLogin, Name, Lastname, Patronymic, AlarmSetted, DateTime) "
                            "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?);");
            mQuery->bindValue(0, LastParam.IdObject);
            mQuery->bindValue(1, AlarmText);
            mQuery->bindValue(2, LastParam.ParamName);
            mQuery->bindValue(3, CurrentParam);
            mQuery->bindValue(4, LastParam.AlarmMIN);
            mQuery->bindValue(5, LastParam.AlarmMAX);
            mQuery->bindValue(6, date);
            mQuery->bindValue(7, time);
            mQuery->bindValue(8, ServerLogin);
            mQuery->bindValue(9, Name);
            mQuery->bindValue(10, LastName);
            mQuery->bindValue(11, Patronymic);
            mQuery->bindValue(12, Flag);            // Установка флага
            mQuery->bindValue(13, getDateTime);
            if(!mQuery->exec())
            {
                qDebug() << "CListenThread:IsAlarm: Запрос записи не был выполнен\r\n" << mQuery->lastError().text();
                emit SendLastErrorDB("ОШИБКА ЗАПИСИ ТРЕВОГИ");
                delete mQuery;
                return false;
            }
            emit SendDeleteTextLastErrorDB("ОШИБКА ЗАПИСИ ТРЕВОГИ");
            // При отсутствии колонки будет добавлена DateTime, также будет произведено заполнение
            //IsMessageDateTime();
// 23.11.2015 Убрано добавление столбца DateTime
            qDebug() << "CAlarm:show: Запрос записи был выполнен (неподтвержденная запись)";
        }
    }
    else // Если параметр в норме тогда очиста флагов тревоги, если таковые есть
    {
        FlagSkip(db, NameParam, ObjectID, date, time);
    }
    delete mQuery;
    return true;
}

// Проверка наличия флага сообщений
// Возвращает 0 - если флаг установлен т.е. есть наличие тревоги либо данные не получены
// Возвращает 1 - если флаг тревоги не установлен
bool CListenThread::IsNotFlagSetted(QSqlDatabase *db, QString NameParam, unsigned int ObjectID)
{
    if(!db->open())
    {
        qDebug() << "CListenThread:IsNotFlagSetted: База данных не открыта, сообщение выводиться не будет\r\n" << db->lastError().text();
        return false;
    }
    QSqlQuery* mQuery = new QSqlQuery(*db);
    QString mVer = "SELECT AlarmSetted FROM " + DBName + ".talarmmessages WHERE Parameter = '%1' AND ObjectID = %2 ORDER BY DateAlarm DESC, TimeAlarm DESC LIMIT 1;",mV;
    mV = mVer.arg(NameParam)
             .arg(QString::number(ObjectID));
    if(!mQuery->exec(mV))
    {
        qDebug() << "CListenThread:IsNotFlagSetted: Запрос не выполнен, записей не обнаружено" << mQuery->lastError().text();
        emit SendLastErrorDB("ОШИБКА ЧТЕНИЯ ТРЕВОГ");
        delete mQuery;
        return false;
    }
    emit SendDeleteTextLastErrorDB("ОШИБКА ЧТЕНИЯ ТРЕВОГ");
    // Проверка флага
    int Count = mQuery->size();
    if(Count <= 0) // Записей нет по данному параметру
    {
        qDebug() << "CListenThread:IsNotFlagSetted: Записей по данному параметру не найдено";
        delete mQuery;
        return true;
    }
    QSqlRecord rec = mQuery->record();
    mQuery->next();
    quint8 Flag = mQuery->value(rec.indexOf("AlarmSetted")).toUInt();
    delete mQuery;
    if(Flag ==1)return false; // Флаг уже установлен,значит сообщение уже выводилось
    return true;              // Флаг не установлен
}
// Проверка наличия флага сообщений
bool CListenThread::FlagSkip(QSqlDatabase* db, QString NameParam, unsigned int ObjectID, QDate DateMessage, QTime TimeMessage)
{
    if(!db->open())
    {
        qDebug() << "База данных не открыта, сброс флага сообщений производиться не будет\r\n" << db->lastError().text();
        return false;
    }
    // Будут сброшены только флаги где дата и время раньше чем у данного сообщения
    QDateTime MessageDateTime;
    MessageDateTime.setDate(DateMessage);
    MessageDateTime.setTime(TimeMessage);
    QSqlQuery* mQuery = new QSqlQuery(*db);
    // От 01.06.2015 (проверка наличия флагов не производится т.к. нет смысла делать лишний запрос на SELECT)
    // Сначала обновление только по дате и времени
    mQuery->prepare("UPDATE " + DBName + ".talarmmessages SET AlarmSetted =0 WHERE Parameter = :A AND ObjectID = :B AND DateTime <= :C AND number >0;");
    mQuery->bindValue(":A", NameParam);
    mQuery->bindValue(":B", ObjectID);
    mQuery->bindValue(":C", MessageDateTime);
    if(!mQuery->exec())
    {
        qDebug() << "CListenThread:FlagSkip: Запрос сброса флагов параметра не выполнен\r\n" << mQuery->lastError().text();
        qDebug() << "CListenThread:FlagSkip: Будет произведена попытка обработки по старому образцу \r\n";

    }
    else
    {
        delete mQuery;
        qDebug() << "CListenThread:FlagSkip: Флаги параметра которые были раньше полученной даты и времени сброшены.";
        return true; // Флаг был установлен, теперь сброшен
    }

    // Старый вариант сброса флага
    QString mVer = "SELECT AlarmSetted FROM " + DBName + ".talarmmessages WHERE Parameter = '%1' AND ObjectID = %2 ORDER BY DateAlarm DESC, TimeAlarm DESC LIMIT 1;",mV;
    mV = mVer.arg(NameParam)
             .arg(QString::number(ObjectID));
    if(!mQuery->exec(mV))
    {
        qDebug() << "CListenThread: Запрос не выполнен, записей не обнаружено" << mQuery->lastError().text();
        emit SendLastErrorDB("ОШИБКА ЧТЕНИЯ ТРЕВОГ");
        delete mQuery;
        return false;
    }
    emit SendDeleteTextLastErrorDB("ОШИБКА ЧТЕНИЯ ТРЕВОГ");
    // Проверка флага
    int Count = mQuery->size();
    if(Count <= 0) // Записей нет по данному параметру
    {
        qDebug() << "CListenThread:FlagSkip: Записей по данному параметру не найдено";
        delete mQuery;
        return false;
    }
    QSqlRecord rec = mQuery->record();
    mQuery->next();
    quint8 Flag = mQuery->value(rec.indexOf("AlarmSetted")).toUInt();
    if(Flag ==1)
    {
        // Сброс данного флага у всех параметров
        mVer = "UPDATE " + DBName + ".talarmmessages SET AlarmSetted =0 WHERE Parameter = '%1' AND ObjectID = '%2' AND number >0;";
        mV = mVer.arg(NameParam)
                 .arg(QString::number(ObjectID));
        if(!mQuery->exec(mV))
        {
            qDebug() << "CListenThread:FlagSkip: Запрос сброса флагов параметра не выполнен\r\n" << mQuery->lastError().text();
            emit SendLastErrorDB("ОШИБКА СБРОСА ТРЕВОГИ");
            delete mQuery;
            return false;
        }
        emit SendDeleteTextLastErrorDB("ОШИБКА СБРОСА ТРЕВОГИ");
        delete mQuery;
        qDebug() << "CListenThread:FlagSkip: Флаг параметра был установлен, теперь сброшен.";
        return true; // Флаг был установлен, теперь сброшен
    }
    delete mQuery;
    qDebug() << "CListenThread:FlagSkip: Флаг параметра не установлен.";
    return false;            // Флаг не установлен
}

// Получение актуального параметра текущего объекта из данных полученных из БД
/*
bool CListenThread::GetActualParam(QSqlDatabase* db, CParam* parameter)
{
    if(!db->open())
    {
        qDebug() << "CListenThread:GetActualParam: База данных не открыта";
        parameter->ParamCurrent = 0;
        return false;
    }
// 19.11.2015 Убран блок запроса ID и запрос всех параметров объектов
    // Выбирается последнее записанное значение из текущих (в данный момент оно первое и последнее)
    QSqlQuery* mQuery = new QSqlQuery(*db);
    mQuery->prepare("SELECT "+ parameter->ParamName + ", datemessage, timemessage FROM " + DBName + ".tdata_current_" +
                    QString::number(parameter->IdObject) +
                    " WHERE :A IS NOT NULL ORDER BY datemessage DESC, timemessage DESC LIMIT 1;");
    mQuery->bindValue(":A", parameter->ParamName);
    // Запрос параметров объектов не выполнен
    if(!mQuery->exec())
    {
        qDebug() << "CListenThread:GetActualParam: Текущее значение параметра не получено:" << mQuery->lastError();
        parameter->ParamCurrent = 0;
        delete mQuery;
        return false;
    }
    // Нет данных о параметрах объектов
    if(mQuery->size() <=0)
    {
        qDebug() << "CListenThread:GetActualParam: Текущего значения параметра нет в БД";
        parameter->ParamCurrent = 0;
        delete mQuery;
        return false;
    }
    // Данные получены
    QSqlRecord rec = mQuery->record();
    mQuery->next();
    bool IsOK = false;
    parameter->DateTime.setDate(mQuery->value(rec.indexOf("datemessage")).toDate());
    parameter->DateTime.setTime(mQuery->value(rec.indexOf("timemessage")).toTime());
    parameter->ParamCurrent = mQuery->value(rec.indexOf(parameter.ParamName)).toFloat(&IsOK);
    // Проверка корректности времени
    if(!parameter->DateTime.isValid())
    {
        qDebug() << "CListenThread:GetActualParam: Дата и время полученного параметра некорректы";
        delete mQuery;
        return false;
    }
    // Вычисление невозможно
    // Данные обработать невозможно, соответственно по данному параметру будет установлен флаг 2
    // - не известно было ли срабатывание, сам параметр будет обнулен
    if((parameter->ParamMAX == parameter->ParamMIN)|(parameter->SensorMAX == parameter->SensorMIN)|(IsOK == false))
    {
        qDebug() << "CListenThread:GetActualParam: Параметры сообщений не настроены, вычислить значение невозможно";
        parameter->ParamCurrent = 0;
    }
    else
    {
        // Преобразование параметра датчика в значение
        parameter->ParamCurrent = (((parameter->ParamCurrent - parameter->SensorMIN) *
                                 (parameter->ParamMAX - parameter->ParamMIN)) /
                                 (parameter->SensorMAX - parameter->SensorMIN)) + parameter->ParamMIN;
        // Если за пределами и выставлен флаг сообщений тогда на вывод диалога
        if(((parameter->ParamCurrent >= parameter->AlarmMAX)|
           (parameter->ParamCurrent <= parameter->AlarmMIN)) & (parameter->IsAlarm!=0))
        {
            parameter->AlarmFlag = 1;
        }
        else
        {
            parameter->AlarmFlag = 0;
        }
    }
    delete mQuery;
    return true;
}*/

// Получение глубины вывода сообщений
// 0- значит глубина вывода не установлена
/*quint32 CListenThread::GetMessageDeep(QSqlDatabase* db)
{
    quint32 deep =0;
    if(!db->open())
    {
        qDebug() << "CListenThread:GetMessageDeep: База данных не открыта";
        return deep;
    }
    // Поиск данных
    QString mVer = "SELECT MessageDeep FROM " + DBName + ".tsystem WHERE UserName = 'root';";
    QSqlQuery* mQuery = new QSqlQuery(*db);
    if(!mQuery->exec(mVer)) // Столбец не найден, либо ошибка связи с БД
    {
        qDebug() << "CListenThread:GetMessageDeep: Запрос глубины вывода сообщений не выполнен:" << mQuery->lastError()
                 << "Попытка создать столбец с данными по умолчанию:";
        QString DataPrint = " Запрос границы вывода сообщений из базы данных не выполнен, ошибка:\r\n" +
                            mQuery->lastError().databaseText() +
                            "Будет произведена попытка добавить данные в базу";
        emit Print(DataPrint, false);

        // Добавление столбца в таблицу системных настроек по умолчанию 2 дня
        mVer = "ALTER TABLE " + DBName + ".tsystem ADD MessageDeep INT UNSIGNED DEFAULT 2880;";
        if(!mQuery->exec(mVer))
        {
            qDebug() << "CAlarm:GetMessageDeep: Запрос добавления столбца глубины вывода сообщений не выполнен:" << mQuery->lastError();
            DataPrint = " Запрос добавления столбца границы вывода сообщений в базу данных не выполнен, ошибка:\r\n" +
                         mQuery->lastError().databaseText();
            emit Print(DataPrint, false);
            delete mQuery;
            return deep;
        }
        qDebug() << "CAlarm:GetMessageDeep: Запрос добавления столбца границы вывода сообщений выполнен.";
        DataPrint = " Запрос добавления столбца границы вывода сообщений в базу данных выполнен";
        emit Print(DataPrint, false);

        delete mQuery;
        deep =2880;
        return deep;
    }
    // Пустое значение
    if(mQuery->size() <=0)
    {
        qDebug() << "CAlarm:GetMessageDeep: Запрос добавления столбца границы вывода сообщений выполнен, но данных нет.";
        deep = 2880;
        return deep;
    }
    // Данные есть
    QSqlRecord rec = mQuery->record();
    mQuery->next();
    bool isOK = false;
    deep = mQuery->value(rec.indexOf("MessageDeep")).toUInt(&isOK);
    if(!isOK)deep =0;
    delete mQuery;
    return deep;
}*/
// Слот записи статуса через соединение основного потока
void CListenThread::SetObjectStatusSlot(QString status, int column)
{
    ((MainWindow*)this->parent())->BusyDB.lock();
    QSqlDatabase db = QSqlDatabase::database("connection");
    SetObjectStatus(&db, status, column);
    ((MainWindow*)this->parent())->BusyDB.unlock();
}

// Запись текущего статуса объекта в БД
// 04.09.2016 Запись статусов соединения производится при:
//            1 - Запуске потока
//            2 - Ошибках связи с БД
//            3 - Получении соединения, либо удалении соединения
// 25.11.2016 Системная БД теперь создается общей для всего объекта
bool CListenThread::SetObjectStatus(QSqlDatabase* db, QString status, int column)
{
    QSqlQuery* mQuery = new QSqlQuery(*db);
    QString condition ="", stat ="";
    quint32 ID;
    BusyObject.lock();
    ID = ObjectID;
    BusyObject.unlock();
    // Выборка всех строки статуса из БД
    condition = " WHERE ObjectID = " + QString::number(ID) + ";";
    mQuery->prepare("SELECT ObjectStatus FROM " + DBName + ".TObject" + condition);
    if(!mQuery->exec())
    {
        emit SendLastErrorDB("ОШИБКА ЧТЕНИЯ СТАТУСА ОБЪЕКТА");
        emit Print(" > Ошибка чтения статуса работы потока №" + QString::number(ID), false);
        delete mQuery;
        return false;
    }
    emit SendDeleteTextLastErrorDB("ОШИБКА ЧТЕНИЯ СТАТУСА ОБЪЕКТА");
    QSqlRecord rec = mQuery->record();
    int reccount = mQuery->size();
    // Есть статус в ответе на запрос
    if(reccount > 0)
    {
        mQuery->next();
        stat  = mQuery->value(rec.indexOf("ObjectStatus")).toString();
    }
    // Если запрос был отработан, а статус не получен
    if(stat.isEmpty())
    {
        emit Print(" > В ответе на запрос статуса нет корректных данных.\r\n", false);
    }
    int indexA =-1, indexB =-1;
    // 04.09.2016 ПРОТЕСТИРОВАНО. РАБОТАЕТ ВЕРНО
    //            Если строка пустая, то будут добавляться */ столбцы до нужного, а в нужный запишется статус
    //            Если в строке есть данные, но нет разделителя после первого столбца - строка будет очищена
    //            Если в строке есть часть столбцов и не хватает до нужного, то будут добавлены */
    //            Если в строке есть столбцы, а дальше идет "мусор" будут добавлены необходимые данные в мусоре,
    //            который будет отделен последним столбцом знаком /
    // Обработка всей строки до нужной колонки
    for(int i=1; i <= column; i++)
    {
        // Поиск правого разделителя в i-колонке
        indexA = stat.indexOf("/", indexB +1);
        // Если нет даже первого столбца либо записано что-то без разделителей, то очистка строки
        if((i ==1)&(indexA ==-1))stat.clear();
        // Разделитель найден, предыдущий разделитель увеличивается
        if(indexA >0)
        {
            // Если нужный столбец, то произвести запись статуса и выход из цикла
            // Если столбец не нужный, то ничего делать не надо
            if(i==column)
            {
                stat.remove(indexB +1, indexA - (indexB +1));
                stat.insert(indexB +1, status);
                break;
            }
            // Левый разделитель равен предыдущему правому
            indexB = indexA;
        }
        // Разделитель не найден, производится запись
        else
        {
            // Если нужный столбец, то произвести запись статуса и выход из цикла
            // Если столбец не нужный, то проставляется "*"
            if(i==column)
            {
                stat.insert(indexB +1, status + "/");
                break;
            }
            else
            {
                stat.insert(indexB +1, "*/");
            }
            indexB = stat.indexOf("/", indexB +1);
        }
    }
// 11.09.2016 Метка времени ставится при записи статуса
    // Метка времени
    QDateTime time;
    DateTime->lock();
    time.setDate(*mDate);
    time.setTime(*mTime);
    DateTime->unlock();
//
    mQuery->prepare("UPDATE " + DBName + ".TObject SET ObjectStatus = :A, ControlTime = :B WHERE ObjectID = :C;");
    mQuery->bindValue(":A",stat);
    mQuery->bindValue(":B",time);
    mQuery->bindValue(":C",ID);
    if(!mQuery->exec())
    {
        emit SendLastErrorDB("ОШИБКА ЗАПИСИ СТАТУСА");
        emit Print(" > Ошибка записи статуса работы потока №" + QString::number(ID), false);
        delete mQuery;
        return false;
    }
    emit SendDeleteTextLastErrorDB("ОШИБКА ЗАПИСИ СТАТУСА");
    delete mQuery;
    return true;
}
// 03.09.2016 Добавлен блок записи ошибок при запросе к БД
// 02.11.2016 Запись и удаление данных ошибок организовано в виде сигналов и слотов т.к. используется другими потоками
void CListenThread::SetLastErrorDB(QString ErrorText/*QSqlQuery* mQuery*/)
{
    DBLastErrorMutex.lock();
    //DBLastError = mQuery->lastError().text();
    if(DBLastError.indexOf(ErrorText)==-1)
        DBLastError += ("\\" + ErrorText);
    DBLastErrorMutex.unlock();
}
// 07.09.2016 Удаление из строки ошибок текста ошибки
// 02.11.2016 Запись и удаление данных ошибок организовано в виде сигналов и слотов т.к. используется другими потоками
void CListenThread::DeleteTextLastErrorDB(QString ErrorText)
{
    DBLastErrorMutex.lock();
    int index = DBLastError.indexOf(ErrorText);
    // Найдена подстрока, следует ее удалить
    if(index !=-1)
    {
        DBLastError.remove(index-1, 1);
        DBLastError.remove(ErrorText, Qt::CaseInsensitive);
    }
    DBLastErrorMutex.unlock();
}
// 07.09.2016 Получение перечня ошибок (служит для сокращения текста)
QString CListenThread::GetLastErrorDB()
{
    QString LE ="";
    DBLastErrorMutex.lock();
    LE = DBLastError;
    DBLastErrorMutex.unlock();
    return LE;
}
// Обновление управляющих данных
bool CListenThread::UpdateControlData()
{
    QSqlQuery* mQuery = new QSqlQuery(SysDB);
    QString condition ="";
    quint32 ID;
    BusyObject.lock();
    ID = ObjectID;
    BusyObject.unlock();
    // 1. Выборка всех данных необходимых для функций управления
    condition = " WHERE ObjectID = " + QString::number(ID) + ";";
#ifdef REQUESTTEST
    QStringList Requests;
    Requests << "CURRENT" << "STATISTIC";
    QByteArray A = serialize(Requests);
    mQuery->prepare("UPDATE "+ DBName + ".TObject SET RequestCommands = :A " + condition);
    mQuery->bindValue(":A", A, QSql::Binary | QSql::In );
    mQuery->exec();
#endif
#ifdef EQUIPMENT
    QStringList Equip;
    Equip << "TSRV024/1" << "IVK102/2" << "ECL301/1828";
    QByteArray B = serialize(Equip);
    mQuery->prepare("UPDATE "+ DBName + ".TObject SET Equipment = :A WHERE ObjectID = 1828 AND number >0;"); //" + condition);
    mQuery->bindValue(":A", B, QSql::Binary | QSql::In );
    mQuery->exec();
#endif
#ifdef TIMETABLE
    QStringList time;
    // Неверный формат записей расписания
    time << "0/0/00:00:00/0/0/23:59:59" << "1/20/08:00:00/2/29/17:00:00";
    QByteArray C = serialize(time);
    mQuery->prepare("UPDATE "+ DBName + ".TObject SET TimeTable = :A " + condition);
    mQuery->bindValue(":A", C, QSql::Binary | QSql::In );
    mQuery->exec();
#endif
#ifdef PARAMETERSTEST
    CVarDB Var;
    QList<CVarDB> V;
// Контрольная сумма
    Var.VarIndex =0;            // Индекс переменной
    // Разрешения параметра:
    //            "READ" - только обработка переменной без записи значения в БД, но при этом допускается запись тревог и сообщений
    //            "CREATE" - разрешается запись переменной в таблицу даже если столбец отсутствует, т.е. будет создана заново со столбцами настроек, в случае если переменная существует данные настроек будут считаны из БД
    //            "WRITE" - запись только в уже созданный столбец, данные настроек скопируются из БД
    //            "COMMAND" - команда конфигурирования или управления (аналогична "WRITE" и "CREATE" с перезаписью данных настроек)
    Var.VarPermit ="READ";
    Var.VarName = "CRC16";   // Имя столбца переменной в БД
    Var.VarType = "CRC16";     // Тип данных к которым следует преобразовать байты из входного массива
    Var.VarTypeSave ="CRC16";  // Формат в котором следует записать переменную в БД
    Var.VarOffset =7;           // Сдвиг от начала во входном массиве
    Var.VarData =2;             // Количество считываемых байт из входного массива
    Var.VarInsert ="2,1";       // Последовательность вставки байтов в переменную VarType
    Var.VarSensor_MIN ="-";     // Минимальное значение датчика типа VarTypeSave
    Var.VarSensor_MAX ="-";    // Максимальное значение датчика типа VarTypeSave
    Var.VarParam_MIN ="-";      // Минимальное значение параметра типа VarTypeSave
    Var.VarParam_MAX ="-";    // Максимальное значение параметра типа VarTypeSave
    Var.VarBorder_MIN ="-";   // Минимальный порог срабатывания типа VarTypeSave разделенный "/" при нескольких значениях
    Var.VarBorder_MAX ="-";     // Максимальный порог срабатывания типа VarTypeSave разделенный "/" при нескольких значениях
    Var.AlarmSet =0;            // Флаг включения сигнализации
    Var.AlarmMin = "Тревога по нижней границе";      // Текст сообщения при тревоги по низкому уровню разделенный "/" при нескольких значениях
    Var.AlarmMax = "Тревога по верхней границе";     // Текст сообщения при тревоги по высокому уровню разделенный "/" при нескольких значениях
    Var.SMSSet =0;              // Включение отправки СМС-при аварийном состоянии параметра
    // Номера телефонов на которые следует отправлять СМС в формате /*/*/12/12... - где первая * - (1 - отправлять, 0 - нет)
    //отправка на номера ответственных, вторая на номера операторов, дальше 12-значные номера дополнительных телефонов
    Var.Telephones ="/0/0";
    Var.StopFlag =1;       // Флаг остановки обработки ответа при ошибке параметра
    V << Var;
// 2. Параметр
    Var.VarIndex=1;
    // Разрешения параметра:
    //            "READ" - только обработка переменной без записи значения в БД, но при этом допускается запись тревог и сообщений
    //            "CREATE" - разрешается запись переменной в таблицу даже если столбец отсутствует, т.е. будет создана заново со столбцами настроек, в случае если переменная существует данные настроек будут считаны из БД
    //            "WRITE" - запись только в уже созданный столбец, данные настроек скопируются из БД
    //            "COMMAND" - команда конфигурирования или управления (аналогична "WRITE" и "CREATE" с перезаписью данных настроек)
    Var.VarPermit ="CREATE";
    Var.VarName = "Номер_прибора";   // Имя столбца переменной в БД
    Var.VarType = "quint32";     // Тип данных к которым следует преобразовать байты из входного массива
    Var.VarTypeSave ="quint32";  // Формат в котором следует записать переменную в БД
    Var.VarOffset =3;           // Сдвиг от начала во входном массиве
    Var.VarData =4;             // Количество считываемых байт из входного массива
    Var.VarInsert ="1,2,3,4";       // Последовательность вставки байтов в переменную VarType
    Var.VarSensor_MIN ="0";     // Минимальное значение датчика типа VarTypeSave
    Var.VarSensor_MAX ="10000000";    // Максимальное значение датчика типа VarTypeSave
    Var.VarParam_MIN ="0";      // Минимальное значение параметра типа VarTypeSave
    Var.VarParam_MAX ="10000000";    // Максимальное значение параметра типа VarTypeSave
    Var.VarBorder_MIN ="0";   // Минимальный порог срабатывания типа VarTypeSave разделенный "/" при нескольких значениях
    Var.VarBorder_MAX ="10000000";     // Максимальный порог срабатывания типа VarTypeSave разделенный "/" при нескольких значениях
    Var.AlarmSet =0;            // Флаг включения сигнализации
    Var.AlarmMin = "Заводской номер вне диапазона";      // Текст сообщения при тревоги по низкому уровню разделенный "/" при нескольких значениях
    Var.AlarmMax = "Заводской номер вне диапазона";      // Текст сообщения при тревоги по высокому уровню разделенный "/" при нескольких значениях
    Var.SMSSet =0;              // Включение отправки СМС-при аварийном состоянии параметра
    // Номера телефонов на которые следует отправлять СМС в формате /*/*/12/12... - где первая * - (1 - отправлять, 0 - нет)
    //отправка на номера ответственных, вторая на номера операторов, дальше 12-значные номера дополнительных телефонов
    Var.Telephones ="/0/0";
    Var.StopFlag =1;       // Флаг остановки обработки ответа при ошибке параметра
    V << Var;

    QByteArray va = serializec(V);
    mQuery->prepare("UPDATE "+ DBName + ".TCommand SET Parameters = :A WHERE ObjectID = 501;");// + condition);
    mQuery->bindValue(":A", va, QSql::Binary | QSql::In );
    if(!mQuery->exec())
    {
        qDebug() << "Ошибка ввода тестовых параметров" << mQuery->lastError().databaseText();
    }
#endif
    mQuery->prepare("SELECT UserCommand, "
                           "TerminalCommand,"
                           "ControlTime,"
                           "ObjectDB,"
                           "TimeTable, "
                           "WorkMode, "
                           "RequestCommands, "
                           "ObjectDelay, "
                           "FlagRequest, "
                           "TerminalDelay, "
                           "Equipment "
                           "FROM " + DBName + ".TObject" + condition);
    if(!mQuery->exec())
    {
        emit SendLastErrorDB("ОШИБКА ОБНОВЛЕНИЯ УПРАВЛЯЮЩИХ ДАННЫХ ОБЪЕКТА");
        emit Print(" > Ошибка обновления управляющих данных объекта №" + QString::number(ID), false);
        delete mQuery;
        return false;
    }
    emit SendDeleteTextLastErrorDB("ОШИБКА ОБНОВЛЕНИЯ УПРАВЛЯЮЩИХ ДАННЫХ ОБЪЕКТА");
    // 2. Обработка полученных данных
    QSqlRecord rec = mQuery->record();
    int reccount = mQuery->size();
    // Есть статус в ответе на запрос
    if(reccount <= 0)
    {
        emit SendLastErrorDB("ОШИБКА НЕ ПОЛУЧЕНЫ ЗАПИСИ УПРАВЛЯЮЩИХ ДАННЫХ");
        emit Print(" > Ошибка: нет записей управляющих данных объекта №" + QString::number(ID), false);
        delete mQuery;
        return false;
    }
    emit SendDeleteTextLastErrorDB("ОШИБКА НЕ ПОЛУЧЕНЫ ЗАПИСИ УПРАВЛЯЮЩИХ ДАННЫХ");
    mQuery->next();
    CControlData control;
    control.CurrentMode  = mQuery->value(rec.indexOf("WorkMode")).toString();
    if(mQuery->value(rec.indexOf("Equipment")).canConvert<QByteArray>())
    {
        QByteArray Eq    = mQuery->value(rec.indexOf("Equipment")).toByteArray();
        control.Equipment = deserialize(Eq);
    }
    else
    {
        delete mQuery;
        return false;
    }
    if(mQuery->value(rec.indexOf("TimeTable")).canConvert<QByteArray>())
    {
        QByteArray Eq    = mQuery->value(rec.indexOf("TimeTable")).toByteArray();
        control.TimeTable = deserialize(Eq);
    }
    else
    {
        delete mQuery;
        return false;
    }
    control.FlagRequest  = mQuery->value(rec.indexOf("FlagRequest")).toBool();
    control.ObjectDB     = mQuery->value(rec.indexOf("ObjectDB")).toString();
    control.ObjectDelay  = mQuery->value(rec.indexOf("ObjectDelay")).toUInt();
    if(mQuery->value(rec.indexOf("RequestCommands")).canConvert<QByteArray>())
    {
        QByteArray Rc    = mQuery->value(rec.indexOf("RequestCommands")).toByteArray();
        control.RequestCommands = deserialize(Rc);
    }
    else
    {
        delete mQuery;
        return false;
    }
    control.TerminalDelay = mQuery->value(rec.indexOf("TerminalDelay")).toUInt();
    control.UserCommand  = mQuery->value(rec.indexOf("UserCommand")).toString();
    control.TerminalCommand = mQuery->value(rec.indexOf("TerminalCommand")).toString();
    // Проверка данных
    DateTime->lock();
    if(!control.CorrectData(mDate->year()))
    {
        DateTime->unlock();
        emit SendLastErrorDB("ОШИБКА НЕТ ОСНОВНЫХ УПРАВЛЯЮЩИХ ДАННЫХ ДЛЯ РАБОТЫ");
        emit Print(" > Ошибка: нет основных управляющих данных объекта №" + QString::number(ID) + "\r\n", false);
        delete mQuery;
        return false;
    }
    DateTime->unlock();
    emit SendDeleteTextLastErrorDB("ОШИБКА НЕТ ОСНОВНЫХ УПРАВЛЯЮЩИХ ДАННЫХ ДЛЯ РАБОТЫ");
    // 3. Если произошла смена режима, следует очистить перечень команд на выполнение, кроме той что выполняется
    BusyControlData.lock();
    if(ControlData->CurrentMode!=control.CurrentMode)
    {
        TerminalStarted = false; // Сброс флага
        ClearCommandsTable();
    }
    ControlData->CopyControlData(&control);
    BusyControlData.unlock();
    // 4. Обработка пользовательских команд
    // Команда запуска потока (производится только запись в БД результата)
    if(control.UserCommand == "START+OK")
    {
        // Ничего не делается
    }
    else if(control.UserCommand == "START")
    {
        mQuery->prepare("UPDATE "+ DBName + ".TObject SET UserCommand = 'START+OK'" + condition);
        if(!mQuery->exec())
        {
            emit SendLastErrorDB("ОШИБКА ЗАПИСИ РЕЗУЛЬТАТА ПОЛЬЗОВАТЕЛЬСКОЙ КОМАНДЫ");
            emit Print(" > Ошибка записи результата пользовательской команды объекта №" + QString::number(ID), false);
            delete mQuery;
            return false;
        }
        emit SendDeleteTextLastErrorDB("ОШИБКА ЗАПИСИ РЕЗУЛЬТАТА ПОЛЬЗОВАТЕЛЬСКОЙ КОМАНДЫ");
        emit Print(" > Запись результата пользовательской команды объекта №" + QString::number(ID) +
                   " " + control.UserCommand + " - выполнена", false);
        control.UserCommand = "START+OK";
        BusyControlData.lock();
        ControlData->UserCommand = "START+OK";
        BusyControlData.unlock();
    }
    // Отработанная команда остановки опроса, удаления, редактирования или выключения - только очищает таблицу команд
    else if((control.UserCommand == "STOP+OK")|(control.UserCommand == "DEL+OK")|(control.UserCommand == "EDIT+OK")|
            (control.UserCommand == "USTOP+OK"))
    {
        ClearCommandsTable(); // По идее данная таблица не может уже содержать команд для выполнения
        delete mQuery;
        return false;
    }
    // Команда остановки опроса, производится запись результата и очистка таблицы команд, кроме той что выполняется
    else if(control.UserCommand == "STOP")
    {
        mQuery->prepare("UPDATE "+ DBName + ".TObject SET UserCommand = 'STOP+OK'" + condition);
        if(!mQuery->exec())
        {
            emit SendLastErrorDB("ОШИБКА ЗАПИСИ РЕЗУЛЬТАТА ПОЛЬЗОВАТЕЛЬСКОЙ КОМАНДЫ");
            emit Print(" > Ошибка записи результата пользовательской команды объекта №" + QString::number(ID), false);
            delete mQuery;
            return false;
        }
        emit SendDeleteTextLastErrorDB("ОШИБКА ЗАПИСИ РЕЗУЛЬТАТА ПОЛЬЗОВАТЕЛЬСКОЙ КОМАНДЫ");
        emit Print(" > Запись результата пользовательской команды объекта №" + QString::number(ID) +
                   " " + control.UserCommand + " - выполнена", false);
        control.UserCommand = "STOP+OK";
        BusyControlData.lock();
        ControlData->UserCommand = "STOP+OK";
        BusyControlData.unlock();
        ClearCommandsTable();
        delete mQuery;
        return false;
    }
    // Команда на удаление, редактирования или отключение объекта
    else if((control.UserCommand == "DEL")|(control.UserCommand == "EDIT")|(control.UserCommand == "USTOP"))
    {
        // Если еще происходит выполнение команды или ожидается ответ, то следует подождать прежде чем производить запись
        // результата и разрешать манипуляции с данными в БД
        if(!ClearCommandsTable())
        {
            delete mQuery;
            return false;
        }
        // Все команды выполнены, перечень очищен, производится запись отработанной команды
        mQuery->prepare("UPDATE "+ DBName + ".TObject SET UserCommand = :A " + condition);
        mQuery->bindValue(":A",control.UserCommand +"+OK");
        if(!mQuery->exec())
        {
            emit SendLastErrorDB("ОШИБКА ЗАПИСИ РЕЗУЛЬТАТА ПОЛЬЗОВАТЕЛЬСКОЙ КОМАНДЫ");
            emit Print(" > Ошибка записи результата пользовательской команды объекта №" + QString::number(ID), false);
            delete mQuery;
            return false;
        }
        emit SendDeleteTextLastErrorDB("ОШИБКА ЗАПИСИ РЕЗУЛЬТАТА ПОЛЬЗОВАТЕЛЬСКОЙ КОМАНДЫ");
        emit Print(" > Запись результата пользовательской команды объекта №" + QString::number(ID) +
                   " " + control.UserCommand + " - выполнена", false);
        control.UserCommand += "+OK";
        BusyControlData.lock();
        ControlData->UserCommand += "+OK";
        BusyControlData.unlock();
        // Отправка сигнала на удаление потока
        emit OnDelete(ID);
        delete mQuery;
        return false;
    }
    // 5. 25.11.2016 Открытие объектной БД на все время работы потока
    if(!ObjectDB.open())
    {
        if(!OpenObjectDB())
        {
            delete mQuery;
            return false;
        }
    }
    // 6. Обработка режима работы
    // Циклический режим работы
    if(control.CurrentMode =="WHILE")
    {
        GetCommands(ID);
    }
    // Режим по расписанию
    else if(control.CurrentMode =="TIME")
    {
        QDateTime startDateTime, stopDateTime, currentDateTime;
        DateTime->lock();
        currentDateTime.setDate(*mDate);
        currentDateTime.setTime(*mTime);
        DateTime->unlock();
        // Сравнение текущего времени и даты с теми что указаны в расписании
        for(int i=0; i < control.TimeTable.size(); i++)
        {
           if(control.GetTimeTable(&startDateTime, &stopDateTime, &currentDateTime, i))
           {
               if((startDateTime <= currentDateTime)&(stopDateTime >=currentDateTime))
               {
                   // Добавление команд производится один раз если какое-то из расписаний активно
                   GetCommands(ID);
                   break;
               }
           }
        }
    }
    // Режим по запросу
    else if(control.CurrentMode =="REQUEST")
    {
        if(control.FlagRequest ==true)
            GetCommands(ID);
    }
    // Терминальный режим
    else if(control.CurrentMode == "TERMINAL")
    {
        // 1. Если в перечне команд нет активных команд то добавление в перечень команд терминала иначе ожидание выполнения команд
        if(!TerminalStarted)
            if(!ClearCommandsTable())
            {
                delete mQuery;
                return false;
            }
        // Блокировка очищения команд после первого раза
        TerminalStarted = true;
        // 2. Установить задержку обновления данных ObjectDelay равную задержке оборудования
        BusyControlData.lock();
        ControlData->ObjectDelay = control.TerminalDelay;
        ControlData->RequestCommands.clear();              // Очистка типов запросов
        ControlData->RequestCommands << "CURRENT";         // Все терминальные команды - текущие
        BusyControlData.unlock();
        // Примечание: В перечень команд терминальные добавляются без проверки настроек переменных, перечня приборов и т.д.
        // 3. Добавление в перечень терминальной команды
        GetTerminalCommands(ID);
    }
    delete mQuery;
    return true;
}
// Очистка таблицы команд возвращает true - если все команды выполнены или false - если еще может прийти ответ от какой-то
bool CListenThread::ClearCommandsTable()
{
    bool InProcessing = true;
    BusyCommandTable.lock();
    for(int i=0; i < CommandTable.size(); i++)
    {
        // Проверка есть ли исполняющиеся команды, если есть возвращается флаг
        if(CommandTable.at(i).Status == "PROCESSING")
            InProcessing = false;
        // Удаляются все команды со статусом "READY" -готова к исполнению
        if(CommandTable.at(i).Status == "READY")
        {
            CommandTable.removeAt(i);
            i--;
        }
    }
    BusyCommandTable.unlock();
    return InProcessing;
}
// 28.09.2016 Запись в таблицу команд необходимых команд из
bool CListenThread::GetCommands(quint32 ID)
{
    BusyControlData.lock();
    QStringList RequestCommands = ControlData->RequestCommands;
    QStringList Equipment = ControlData->Equipment;
    QString WorkMode = ControlData->CurrentMode;
    BusyControlData.unlock();
    // 13.10.2016 Если режим терминала
    if(WorkMode =="TERMINAL")
    {
        return GetTerminalCommands(ID);
    }
    // 1. Если перечень приборов пуст, то команды не выбираются
    if(Equipment.isEmpty())return false;
    // 2. Если БД объекта не открыта
    if(!FlagODB)
        return false;
    // 3. Выборка всех команд объекта по разрешенным типам опроса
    QSqlQuery* mQuery = new QSqlQuery(SysDB);
    QList<CCommand> NewCommandTable;
    for(int i=0; i < RequestCommands.size(); i++)
    {
        mQuery->prepare("SELECT * FROM " + DBName + ".TCommand WHERE RequestType =:A AND ObjectID =:B AND TypeCommand != 'TERMINAL(Y)' AND TypeCommand != 'TERMINAL(N)';"); // Выбрать все записи которые есть
        mQuery->bindValue(":A", RequestCommands.at(i));
        mQuery->bindValue(":B", ID);
        // Запрос не выполнен
        if (!mQuery->exec())
        {
            qDebug() << "CCommand::initCommand: Ошибка получения команды из БД\r\n" << mQuery->lastError().databaseText();
            emit SendLastErrorDB("ОШИБКА ПОЛУЧЕНИЯ КОМАНД ИЗ БД");
            continue;
        }
        else
            emit SendDeleteTextLastErrorDB("ОШИБКА ПОЛУЧЕНИЯ КОМАНД ИЗ БД");
        // Получена ли запись
        if(mQuery->size() <=0)
            continue;
        // Выборка и проверка данных
        while(mQuery->next())
        {
            // Объявляется тут, чтобы при каждой итерации вызывался конструктор и менял сеансовый номер команды
            CCommand command;
            // Если данные распознаны корректно
            if(command.initCommand(ID, RequestCommands.at(i), mQuery))
            {
                // Проверка прибора указанного в команде по перечню приборов в TObject
                // Если данного прибора нет, то команда не обрабатывается
                for( int j=0; j < Equipment.size(); j++)
                {
                    // Если есть такой прибор в перечне приборов объекта
                    if(Equipment.at(j) == (command.DBData.EqName +"/" + command.DBData.EqNumber))
                    {
                        NewCommandTable << command;
                        break;
                    }
                }
            }
        }
    }
    // 4. Сравнение уже имеющихся необработанных команд и тех что отобраны в новом списке
    BusyCommandTable.lock();
    QList<CCommand> Command = CommandTable;
    BusyCommandTable.unlock();
    for(int i=0; i < Command.size(); i++)
    {
        // Сверка всех команд нового перечня с каждой командой старого перечня только если команды еще в обработке или
        // не обрабатывались, остальные отработанные команды для обработчика данных
        if((Command.at(i).Status == "READY")|(Command.at(i).Status == "PROCESSING")|
                (Command.at(i).Status == "ERROR")|(Command.at(i).Status =="TIMEOUT"))
        {
            for(int j=0; j < NewCommandTable.size(); j++)
            {
                if(Command.at(i).DBData.CID == NewCommandTable.at(j).DBData.CID)
                {
                    NewCommandTable.removeAt(j);
                    j--;
                }
            }
        }
    }
    // 5. Поиск таблицы прибора в БД объекта, если нет, то удаление команды и вывод соответствующей информации
    TestEqTables(&NewCommandTable);
    // 6. Если разрешение переменной:
    //       - если COMMAND - изменение настроек переменной, либо создание новой переменной и настроек
    //       - если WRITE - поиск переменной в таблице прибора и ее настроек, перезапись в таблице команд, если не найдена или некорректна, то удаление команды
    //       - если CREATE - поиск переменной в таблице прибора и ее настроек, перезапись в таблице команд, если не найдена или некорректна, то создание новой переменной и ее настроек
    //       - если READ - ничего не проиводится т.к. переменная не записывается
    TestEqVariables(&NewCommandTable);
    // 7. Заполнение недостающих команд в таблицу команд
    BusyCommandTable.lock();
    for(int i =0; i < NewCommandTable.size(); i++)
    {
        CommandTable << NewCommandTable.at(i);
    }
    BusyCommandTable.unlock();
    delete mQuery;
    return true;
}
// 13.10.2016 Получение терминальных команд
bool CListenThread::GetTerminalCommands(quint32 ID)
{
    // Если есть одна команда в работе или готовая к исполнению, то выход
    BusyCommandTable.lock();
    for(int i=0; i < CommandTable.size(); i++)
    {
        if((CommandTable.at(i).Status == "READY")|(CommandTable.at(i).Status == "PROCESSING"))
        {
            BusyCommandTable.unlock();
            return false;
        }
    }
    BusyCommandTable.unlock();
    QSqlQuery* mQuery = new QSqlQuery(SysDB);
    CCommand NewCommand;
    mQuery->prepare("SELECT * FROM " + DBName + ".TCommand WHERE RequestType ='CURRENT' AND ObjectID =:A AND (TypeCommand = 'TERMINAL(N)' OR TypeCommand = 'TERMINAL(Y)');"); // Выбрать все записи которые есть
    mQuery->bindValue(":A", ID);
    // Запрос не выполнен
    if (!mQuery->exec())
    {
        qDebug() << "CCommand::initCommand: Ошибка получения команды из БД\r\n" << mQuery->lastError().databaseText();
        emit SendLastErrorDB("ОШИБКА ПОЛУЧЕНИЯ КОМАНД ИЗ БД");
        delete mQuery;
        return false;
    }
    else
        emit SendDeleteTextLastErrorDB("ОШИБКА ПОЛУЧЕНИЯ КОМАНД ИЗ БД");
    // Нет записей - просто выход
    if(mQuery->size() <=0)
    {
        delete mQuery;
        return false;
    }
    // Выборка одной команды (группы команд не обрабатываются)
    mQuery->next();
    // Объявляется тут, чтобы при каждой итерации вызывался конструктор и менял сеансовый номер команды
    // Если данные распознаны корректно
    if(!NewCommand.initCommand(ID, "CURRENT", mQuery))
    {
        // Запись в TObject - TerminalCommands информации об ошибке
        mQuery->prepare("UPDATE " + DBName + ".TObject SET TerminalCommand ='Сервер: Ошибка проверки терминальной команды.' WHERE ObjectID =:A AND number >0;");
        mQuery->bindValue(":A", ID);
        mQuery->exec();
        // Удаление команд из БД
        mQuery->prepare("DELETE FROM " + DBName + ".TCommand WHERE ObjectID =:A AND (TypeCommand = 'TERMINAL(N)' OR TypeCommand = 'TERMINAL(Y)');"); // Удалить все записи которые есть
        mQuery->bindValue(":A", ID);
        mQuery->exec();
        // Вывод сообщения в окно потока
        QString DataPrint = " > ОШИБКА: Терминальная команда не распознана и будет удалена\r\n";
        emit Print(DataPrint, false);
        delete mQuery;
        return false;
    }
    // Удаление терминальных команд из таблицы команд БД
    mQuery->prepare("DELETE FROM " + DBName + ".TCommand WHERE ObjectID =:A AND (TypeCommand = 'TERMINAL(N)' OR TypeCommand = 'TERMINAL(Y)');"); // Удалить все записи которые есть
    mQuery->bindValue(":A", ID);
    mQuery->exec();
    // Запись команды в таблицу команд
    BusyCommandTable.lock();
    CommandTable << NewCommand;
    BusyCommandTable.unlock();
    // Запись в ControlData задержки из команды
    BusyControlData.lock();
    ControlData->ObjectDelay = NewCommand.DBData.Delay;
    BusyControlData.unlock();
    delete mQuery;
    return true;
}
// Открытие базы данных
bool CListenThread::OpenObjectDB()
{
    BusyObject.lock();
    quint32 ID = ObjectID;
    BusyObject.unlock();
    // Если установлен флаг БД, то сначала следует закрыть БД
    if(FlagODB)
    {
        FlagODB = false;
        {
            ObjectDB.close();
        }
        QSqlDatabase::removeDatabase("Equipment" + QString::number(ID));
    }
    // Данные БД
    BusyControlData.lock();
    QString ODB = ControlData->ObjectDB;
    BusyControlData.unlock();
    QStringList ODBData;
    if(!ODB.isEmpty())
        ODBData = ODB.split("/");
    if(ODBData.size()!=3)
    {
        QString DataPrint = " > ОШИБКА: база данных объекта " + QString::number(ID) + " не открыта: ошибка данных подключения\r\n";
        emit Print(DataPrint, false);
        emit SendLastErrorDB("ОШИБКА ОТКРЫТИЯ ОБЪЕКТНОЙ БД");
        return false;
    }
    ObjectDB = QSqlDatabase::addDatabase("QMYSQL", "Equipment" + QString::number(ID));         // Сборка по инструкции
    ObjectDB.setDatabaseName(ODBData.at(2));
    PortHost.lock();
    QString login = Login;
    QString password = Password;
    PortHost.unlock();
    ObjectDB.setUserName(login);                        // Логин сервера
    ObjectDB.setHostName(ODBData.at(0));                // Адрес или имя хоста из текстового поля
    ObjectDB.setPort(ODBData.at(1).toInt());            // Порт из текстовго поля
    ObjectDB.setPassword(password);                     // Пароль сервера

    if (!ObjectDB.open())
    {
        {
            ObjectDB.close();
        }
        qDebug() << " > База данных объекта " << QString::number(ID) << " не открыта: " << ObjectDB.lastError().databaseText();
        QString DataPrint = " > ОШИБКА: база данных объекта " + QString::number(ID) + " не открыта: " + ObjectDB.lastError().databaseText() + "\r\n";
        emit Print(DataPrint, false);
        emit SendLastErrorDB("ОШИБКА ОТКРЫТИЯ ОБЪЕКТНОЙ БД");
        QSqlDatabase::removeDatabase("Equipment" + QString::number(ID));
        return false;
    }
    emit SendDeleteTextLastErrorDB("ОШИБКА ОТКРЫТИЯ ОБЪЕКТНОЙ БД");
    qDebug() << " > База данных объекта " << QString::number(ID) << " открыта: " << ObjectDB.databaseName();
    FlagODB = true;
    return true;
}
// Получение из строки команды имени таблицы прибора
// 11.10.2016 Заменены разделители "/" на "_" т.к. MyQSL не обрабатывает как целые имена такие заголовки и названия
// 13.11 2016 При передаче имени таблицы перевод в нижний регистр всех символов - так идет имя с БД
QString CListenThread::GetEquipmentTableName(CCommand command)
{
    QString t = "t_" + QString::number(command.DBData.ObjectID) + "_" + command.DBData.EqName + "_"
                + command.DBData.EqNumber + "_";
    // Если тип команды - текущие или настройка/управление с записью в текущие, то ищется таблица текущих
    if((command.DBData.RequestType == "CURRENT")|
       (command.DBData.RequestType == "CONFIG_C"))
        t += "CURRENT";
    else if((command.DBData.RequestType == "STATISTIC")|
            (command.DBData.RequestType == "CONFIG_S"))
        t += "STATISTIC";
    t= t.toLower();
    return t;
}
// Поиск таблиц приборов, указанных в командах и если таких таблиц нет удаление команд из перечня команд
void CListenThread::TestEqTables(QList<CCommand>* NewCommandTable)
{
    QStringList tables = ObjectDB.tables();
    bool IsObjects = false;
    for(int i =0; i < NewCommandTable->size(); i++)
    {
        QString t = GetEquipmentTableName(NewCommandTable->at(i));
        IsObjects = false;
        foreach (const QString &mObjects, tables)
        {
            if(mObjects.contains(t))
            { IsObjects = true; break; }
        }
        // Если таблица не найдена, то удаление данной команды
        if(!IsObjects)
        {
            qDebug() << " > Таблица прибора (" << NewCommandTable->at(i).DBData.EqName << "/"
                     << NewCommandTable->at(i).DBData.EqNumber << ") не найдена";
            qDebug() << " > Команда CID:"
                     << QString::number(NewCommandTable->at(i).DBData.CID)
                     << " не обрабатывается";
            QString DataPrint = " > Таблица прибора (" + NewCommandTable->at(i).DBData.EqName + "/"
                                + NewCommandTable->at(i).DBData.EqNumber + ") не найдена\r\n";
            emit Print(DataPrint, false);
            DataPrint = " > Команда CID:" + QString::number(NewCommandTable->at(i).DBData.CID)
                        + " не обрабатывается\r\n";
            emit Print(DataPrint, false);
            NewCommandTable->removeAt(i);
            i--;
        }
    }
}
// Проверка настроек переменных указанных в командах по следующему алгоритму:
//       - если COMMAND - изменение настроек переменной, либо создание новой
//       - если WRITE - поиск переменной в таблице прибора и ее настроек, перезапись в таблице команд, если не найдена либо некорректна, то удаление команды
//       - если CREATE - поиск переменной в таблице прибора и ее настроек, перезапись в таблице команд, если не найдена либо некорректна, то создание новой переменной и ее настроек
//       - если READ - ничего не проиводится т.к. переменная не записывается
// 11.10.2016 Заменены разделители "/" на "_" т.к. MyQSL не обрабатывает как целые имена такие заголовки и названия
void CListenThread::TestEqVariables(QList<CCommand>* NewCommandTable)
{
    QSqlQuery* mEQuery = new QSqlQuery(ObjectDB);
    for(int i =0; i < NewCommandTable->size(); i++)
    {
        for(int j =0; j < NewCommandTable->at(i).DBData.Params.size(); j++)
        {
            QString Permit = NewCommandTable->at(i).DBData.Params.at(j).VarPermit;
            // Имя таблицы данного прибора
            QString t = GetEquipmentTableName(NewCommandTable->at(i));
            CVarDB var;
            CCommand command;
            // Проверка переменных с правами записи и создания
            if((Permit == "WRITE")|(Permit == "CREATE"))
            {
                // Запрос настроек параметра (всегда из первой строки записи)
                mEQuery->prepare("SELECT " + NewCommandTable->at(i).DBData.Params.at(j).VarName +
                                 "_Config FROM " + ObjectDB.databaseName() + "." + t + " WHERE number =1;");
                // Запрос не удался и разрешение только на чтение
                if(!mEQuery->exec())
                {
                    if(Permit == "WRITE")
                    {
                        qDebug() << " > Запрос настроек переменной " << NewCommandTable->at(i).DBData.Params.at(j).VarName
                                << " из " << ObjectDB.databaseName() << "." << t << " не удался: " << mEQuery->lastError().databaseText();
                        qDebug() << " > Команда CID:"
                                << QString::number(NewCommandTable->at(i).DBData.CID)
                                << " не обрабатывается";
                        QString DataPrint = " > Запрос настроек переменной " + NewCommandTable->at(i).DBData.Params.at(j).VarName
                                            + " из " + ObjectDB.databaseName() + "." + t + " не удался: ошибка запроса\r\n";
                        emit Print(DataPrint, false);
                        DataPrint = " > Команда CID:" + QString::number(NewCommandTable->at(i).DBData.CID)
                                    + " не обрабатывается\r\n";
                        emit Print(DataPrint, false);
                        NewCommandTable->removeAt(i);
                        i--;
                        break;
                    }
                    // Если разрешение CREATE то надо будет данную переменную перезаписать для этого разрешение меняется на COMMAND
                    Permit = "COMMAND";                               // Для обработки
                    var = NewCommandTable->at(i).DBData.Params.at(j); // Запись существующей переменной
                    var.VarPermit = "COMMAND";                        // Замена разрешения
                    command = NewCommandTable->at(i);                 // Получение команды
                    command.DBData.Params.replace(j, var);            // Замена переменной в перечне переменных команды
                    NewCommandTable->replace(i, command);             // Замена измененной команды в перечне команд
                }
                // Запрос удался, но не полученно никаких данных
                else if(mEQuery->size() <=0)
                {
                    if(Permit == "WRITE")
                    {
                        qDebug() << " > Запрос настроек переменной " << NewCommandTable->at(i).DBData.Params.at(j).VarName
                                << " из " << ObjectDB.databaseName() << "." << t << " не удался: нет записей ";
                        qDebug() << " > Команда CID:"
                                << QString::number(NewCommandTable->at(i).DBData.CID)
                                << " не обрабатывается";
                        QString DataPrint = " > Запрос настроек переменной " + NewCommandTable->at(i).DBData.Params.at(j).VarName
                                            + " из " + ObjectDB.databaseName() + "." + t + " не удался: нет записей\r\n";
                        emit Print(DataPrint, false);
                        DataPrint = " > Команда CID:" + QString::number(NewCommandTable->at(i).DBData.CID)
                                    + " не обрабатывается\r\n";
                        emit Print(DataPrint, false);
                        NewCommandTable->removeAt(i);
                        i--;
                        break;
                    }
                    // Если разрешение CREATE то надо будет данную переменную перезаписать для этого разрешение меняется на COMMAND
                    Permit = "COMMAND";
                    var = NewCommandTable->at(i).DBData.Params.at(j); // Запись существующей переменной
                    var.VarPermit = "COMMAND";                        // Замена разрешения
                    command = NewCommandTable->at(i);                 // Получение команды
                    command.DBData.Params.replace(j, var);            // Замена переменной в перечне переменных команды
                    NewCommandTable->replace(i, command);             // Замена измененной команды в перечне команд
                }
                else
                {
                    // Запрос удался
                    QSqlRecord rec = mEQuery->record();
                    mEQuery->next();
                    if(mEQuery->value(rec.indexOf(NewCommandTable->at(i).DBData.Params.at(j).VarName + "_Config")).canConvert<QByteArray>())
                    {
                        QByteArray Rc = mEQuery->value(NewCommandTable->at(i).DBData.Params.at(j).VarName + "_Config").toByteArray();
                        if(!Rc.isEmpty())
                            var = deserializev(Rc);
                        // Полученный параметр некорректен
                        if(!command.TestParameterSetup(var))
                        {
                            if(Permit == "WRITE")
                            {
                                qDebug() << " > Настройки переменной " << NewCommandTable->at(i).DBData.Params.at(j).VarName
                                        << " , полученные из БД некорректны";
                                qDebug() << " > Команда CID:"
                                        << QString::number(NewCommandTable->at(i).DBData.CID)
                                        << " не обрабатывается";
                                QString DataPrint = " > Настройки переменной " + NewCommandTable->at(i).DBData.Params.at(j).VarName
                                                    + " из " + ObjectDB.databaseName() + "." + t + " некорректны\r\n";
                                emit Print(DataPrint, false);
                                DataPrint = " > Команда CID:" + QString::number(NewCommandTable->at(i).DBData.CID)
                                            + " не обрабатывается\r\n";
                                emit Print(DataPrint, false);
                                NewCommandTable->removeAt(i);
                                i--;
                                break;
                            }
                            // Если разрешение CREATE то надо будет данную переменную перезаписать для этого разрешение меняется на COMMAND
                            Permit = "COMMAND";
                            var = NewCommandTable->at(i).DBData.Params.at(j); // Запись существующей переменной
                            var.VarPermit = "COMMAND";                        // Замена разрешения
                            command = NewCommandTable->at(i);                 // Получение команды
                            command.DBData.Params.replace(j, var);            // Замена переменной в перечне переменных команды
                            NewCommandTable->replace(i, command);             // Замена измененной команды в перечне команд
                        }
                        else
                        {
                            // Параметр в норме запись параметра в команду
                            var.VarPermit = "WRITE";               // Если параметр CREATE, но уже создан и данные корректны
                            var.VarIndex = (quint32)j;             // Вставка индекса т.к. в настройках БД индекс всегда 0
                            command = NewCommandTable->at(i);
                            command.DBData.Params.replace(j, var); // Замена переменной в перечне переменных команды
                            NewCommandTable->replace(i, command);  // Замена измененной команды в перечне команд
                        }
                    }
                    // Данные конвертировать невозможно
                    else
                    {
                        if(Permit == "WRITE")
                        {
                            qDebug() << " > Настройки переменной " << NewCommandTable->at(i).DBData.Params.at(j).VarName
                                    << " , полученные из БД некорректны";
                            qDebug() << " > Команда CID:"
                                    << QString::number(NewCommandTable->at(i).DBData.CID)
                                    << " не обрабатывается";
                            QString DataPrint = " > Настройки переменной " + NewCommandTable->at(i).DBData.Params.at(j).VarName
                                                + " из " + ObjectDB.databaseName() + "." + t + " некорректны\r\n";
                            emit Print(DataPrint, false);
                            DataPrint = " > Команда CID:" + QString::number(NewCommandTable->at(i).DBData.CID)
                                        + " не обрабатывается\r\n";
                            emit Print(DataPrint, false);
                            NewCommandTable->removeAt(i);
                            i--;
                            break;
                        }
                        // Если разрешение CREATE то надо будет данную переменную перезаписать для этого разрешение меняется на COMMAND
                        Permit = "COMMAND";
                        var = NewCommandTable->at(i).DBData.Params.at(j); // Запись существующей переменной
                        var.VarPermit = "COMMAND";                        // Замена разрешения
                        command = NewCommandTable->at(i);                 // Получение команды
                        command.DBData.Params.replace(j, var);            // Замена переменной в перечне переменных команды
                        NewCommandTable->replace(i, command);             // Замена измененной команды в перечне команд
                    }
                }
            }
            if(Permit == "COMMAND")
            {
                // Если переменная с разрешением COMMAND следует перезаписать настройки и саму переменную в таблице прибора объекта
                // 1. Замена или добавление столбца переменной (с пустым значением) и столбца настройки переменной
                // А. Проверка если вообще строки с таблице
                mEQuery->prepare("SELECT number FROM " + ObjectDB.databaseName() + "." + t + " ;");
                int rows =0; // Если нет строк то будет произведена вставка строк, если есть то изменение
                if(mEQuery->exec())
                {
                    rows = mEQuery->size();
                }
                QString name = NewCommandTable->at(i).DBData.Params.at(j).VarName;
                var = NewCommandTable->at(i).DBData.Params.at(j);
                var.VarIndex =0;
                var.VarPermit ="WRITE";
                QByteArray Rc = serializev(var);
                bool IsUpdate = false;
                // Если строки есть, то меняется первая строка
                if(rows > 0)
                {
                    mEQuery->prepare("UPDATE " + ObjectDB.databaseName() + "." + t + " SET " +
                                    name + "_Config = :A WHERE number =1;");
                    mEQuery->bindValue(":A", Rc, QSql::Binary | QSql::In);
                    // Если не удался запрос, то будет изменена таблица
                    if(mEQuery->exec())
                        IsUpdate =true;
                }
                // Если изменить данные не удалось, то будет изменена таблица
                if(IsUpdate == false)
                {
                    // Удаление столбцов VarName_DateTime, VarName и VarName_Config (неважно есть они или нет)
                    mEQuery->prepare("ALTER TABLE " + ObjectDB.databaseName() + "." + t +
                                     " DROP " + name +"_DateTime, " +                  // Удаление метки времени
                                     "DROP " + name + ", " +                          // Удаление переменной
                                     "DROP " + name + "_Config;");                    // Удаление настройки
                    mEQuery->exec();
                    // Создание столбцов VarName_DateTime, VarName и VarName_Config
                    mEQuery->prepare("ALTER TABLE " + ObjectDB.databaseName() + "." + t +
                                     " ADD " + name +"_DateTime DATETIME DEFAULT NOW(), " +
                                     "ADD " + name + " VARCHAR(200) DEFAULT NULL, " +
                                     "ADD " + name + "_Config BLOB;");
                    mEQuery->exec();
                    // Вставка строки данных (VarName_DateTime, VarName, VarName_Config)
                    if(rows <=0)
                    {
                        mEQuery->prepare("INSERT INTO " + ObjectDB.databaseName() + "." + t + " ( " +
                                        name +"_DateTime, " +
                                        name + ", " +
                                        name + "_Config ) " +
                                        " VALUES (NOW(),NULL,?);");
                        mEQuery->bindValue(0, Rc, QSql::Binary | QSql::In);
                        if(mEQuery->exec())
                            IsUpdate =true;
                    }
                    else
                    {
                        mEQuery->prepare("UPDATE " + ObjectDB.databaseName() + "." + t + " SET " +
                                        name + "_Config = :A WHERE number =1;");
                        mEQuery->bindValue(":A", Rc, QSql::Binary | QSql::In);
                        // Если неудался запрос, то будет изменена таблица
                        if(mEQuery->exec())
                            IsUpdate =true;
                    }
                }
                // Если обновить данные настройки переменной не удалось, то следует вывести сообщение об ошибке и удалить
                // такую команду из перечня
                if(IsUpdate ==false)
                {
                    qDebug() << " > Настройки переменной " << NewCommandTable->at(i).DBData.Params.at(j).VarName
                            << " , записать в БД прибора не удалось";
                    qDebug() << " > Команда CID:"
                            << QString::number(NewCommandTable->at(i).DBData.CID)
                            << " не обрабатывается";
                    QString DataPrint = " > Настройки переменной " + NewCommandTable->at(i).DBData.Params.at(j).VarName
                                        + " из " + ObjectDB.databaseName() + "." + t + " записать в БД не удалось\r\n";
                    emit Print(DataPrint, false);
                    DataPrint = " > Команда CID:" + QString::number(NewCommandTable->at(i).DBData.CID)
                                + " не обрабатывается\r\n";
                    emit Print(DataPrint, false);
                    NewCommandTable->removeAt(i);
                    i--;
                    break;
                }
                qDebug() << " > Настройки переменной " << NewCommandTable->at(i).DBData.Params.at(j).VarName
                        << " , записаны в БД прибора";
                QString DataPrint = " > Настройки переменной " + NewCommandTable->at(i).DBData.Params.at(j).VarName
                                    + " из " + ObjectDB.databaseName() + "." + t + " записаны в БД прибора\r\n";
                emit Print(DataPrint, false);
                // После изменения настроек, переменная в таблице команд имеет разрешение WRITE
                var = NewCommandTable->at(i).DBData.Params.at(j);
                var.VarPermit ="WRITE";
                command = NewCommandTable->at(i);
                command.DBData.Params.replace(j, var); // Замена переменной в перечне переменных команды
                NewCommandTable->replace(i, command);  // Замена измененной команды в перечне команд
                // 18.03.2017 Следует сбросить тревоги по данному параметру если они есть
                QSqlQuery* mQuery = new QSqlQuery(SysDB);
                mQuery->prepare("UPDATE " + DBName + ".TAlarms SET AlarmSetted =0 WHERE ObjectID =" + QString::number(command.DBData.ObjectID) +
                                " AND Parameter ='" + command.DBData.EqName + "_" +
                                command.DBData.EqNumber + "_" + command.DBData.Params.at(j).VarName +
                                "' AND AlarmSetted = 1 AND number >0;");
                mQuery->exec();
                delete mQuery;
            }
        // Если разрешение типа "READ", то проверка не производится т.к. не пишется в БД
        }
    }
    delete mEQuery;
}
// Протестировано
// 23.11.2016
// Обработчик отработанных команд (удаляет отработанные команды из CommandTable, изменяет счетчики, данные в БД
// также записывает информацию об ошибках в таблицу ошибок
// 10.06.2017 База данных таблицы ошибок заменена на объектную т.к. таблица ошибок - объектная
bool CListenThread::CommandHandler()
{
    // 1. Проверка перечня команд
    BusyCommandTable.lock();
    if(CommandTable.isEmpty())    // Перечень пуст, выход
    {
        BusyCommandTable.unlock();
        return true;
    }
    BusyCommandTable.unlock();
    BusyObject.lock();
    quint32 ID = ObjectID;
    BusyObject.unlock();
    BusyControlData.lock();
    QString mode = ControlData->CurrentMode;
    BusyControlData.unlock();
    // 2. Поиск таблицы ошибок в БД объекта
    QString TableName = "t_" + QString::number(ID) + "_errors";
    QSqlQuery* mQuery = NULL; // Создается нулевой указатель специально для определения открыта ли БД
    QSqlQuery* mSQuery = new QSqlQuery(SysDB);
    if(FlagODB)
    {
        QStringList mTableList = ObjectDB.tables();
        mQuery = new QSqlQuery(ObjectDB);
        // Поиск таблицы ошибок
        bool IsTable = false;
        foreach (const QString &mTable, mTableList)
        {
            if(mTable.contains(TableName)){ IsTable = true; break; }
        }
        // Таблица ошибок не найдена - следует создать
        if(IsTable ==false)
        {
            QString str = "CREATE TABLE IF NOT EXISTS " + TableName + " ( "
                          "number INTEGER PRIMARY KEY AUTO_INCREMENT, "
                          "Equipment VARCHAR(100) DEFAULT NULL, "
                          "LastTime DATETIME DEFAULT NOW(), "
                          "LastError VARCHAR(100) DEFAULT NULL, "
                          "CurrentTime DATETIME DEFAULT NOW(), "
                          "GeneralCount INT UNSIGNED DEFAULT 0, "
                          "ErrorCount INT UNSIGNED DEFAULT 0 "
                                            ");";
            if (!mQuery->exec(str))
            {
                qDebug() << " > Не удалось создать таблицу ошибок объекта" << mQuery->lastError();
                emit SendLastErrorDB("ОШИБКА СОЗДАНИЯ ТАБЛИЦЫ ОШИБОК ОБЪЕКТА");
                delete mQuery;
                mQuery = NULL;
            }
            else
                emit SendDeleteTextLastErrorDB("ОШИБКА СОЗДАНИЯ ТАБЛИЦЫ ОШИБОК ОБЪЕКТА");
        }
    }
    // 3. Обработка команд
    BusyCommandTable.lock();
    for(int i =0; i < CommandTable.size(); i++)
    {
        CCommand command = CommandTable.at(i);
        if(command.DBData.CountCommand == 1)
            DeleteCommand(mSQuery, command);
//        emit Print(" > Полученные данные: " + command.Answer.toHex() + "\r\n", true);
        // Если статус команды READY или PROCESSING то обработка команд не производится
        if((command.Status == "READY")|(command.Status == "PROCESSING"))
            continue;
        // Если статус некорректен, то команда удаляется из перечня
        else if((command.Status!="ERROR")&(command.Status!="TIMEOUT")&(command.Status!="COMPLETED"))
        {
            emit Print(" > Статус команды CID:" +
                       QString::number(command.DBData.CID) +
                       " некорректен\r\n", false);
            CommandTable.removeAt(i);
            i--;
            continue;
        }
        // Если статус ошибки
        else if((command.Status == "ERROR")|(command.Status == "TIMEOUT"))
        {
            // Если установлен режим терминала, то запись в TerminalCommand информации об ошибке и
            // удаление команды из перечня команд
            if(mode == "TERMINAL")
            {
                // Функция сама проверит указатель запроса
                SetTerminalError(mSQuery, command);
                CommandTable.removeAt(i);
                i--;
                continue;
            }
            command.BadAttempts++;
            // Если соединение с БД установлено
            if(mQuery!=NULL)
            {
                QString eqname = command.DBData.EqName + "/" + command.DBData.EqNumber;
                // Получение строки данных из таблицы ошибок
                mQuery->prepare("SELECT GeneralCount, ErrorCount FROM " +
                                ObjectDB.databaseName() + "." + TableName +
                                " WHERE Equipment = :A;");
                mQuery->bindValue(":A", eqname);
                // Если запрос не выполнен или записей не получено, то следует попробовать создать запись и
                // установить значения счетчиков и времени
                if((!mQuery->exec())|
                    (mQuery->size() <= 0))
                {
                    emit Print(" > В таблице ошибок нет записей по прибору:" + eqname + "\r\n", false);
                    emit Print(" > Производится попытка создания записи:\r\n", false);
                    CreateErrorRow(mQuery, command, 1, 1);
                }
                // Если запрос выполнен, получение текущих значений
                else
                {
                    bool IsOk = false;
                    quint32 GeneralCount =0, ErrorCount =0;
                    QSqlRecord rec = mQuery->record();
                    mQuery->next();
                    GeneralCount = mQuery->value(rec.indexOf("GeneralCount")).toUInt(&IsOk);
                    if(!IsOk)
                    {
                        GeneralCount =0; // Если значение ошибочное, то начать заново с 1
                        ErrorCount   =0; // Счетчик вподряд идущих ошибок также
                    }
                    else
                    {
                        ErrorCount = mQuery->value(rec.indexOf("ErrorCount")).toUInt(&IsOk);
                        if(!IsOk)
                            ErrorCount   =0;
                    }
                    // Увеличение счетчиков
                    GeneralCount++;
                    ErrorCount++;
                    // Обновление счетчиков
                    mQuery->prepare("UPDATE " +
                                    ObjectDB.databaseName() + "." + TableName +
                                    " SET LastTime = :A, LastError = :B, CurrentTime = :C,"
                                    " GeneralCount = :D, ErrorCount = :E WHERE Equipment = :F AND number >0;");
                    mQuery->bindValue(":A", command.FinishTime);
                    mQuery->bindValue(":B", command.Status);
                    mQuery->bindValue(":C", command.FinishTime);
                    mQuery->bindValue(":D", GeneralCount);
                    mQuery->bindValue(":E", ErrorCount);
                    mQuery->bindValue(":F", eqname);
                    if(!mQuery->exec())
                    {
                        emit Print(" > Ошибка записи счетчиков ошибок CID:" +
                                   QString::number(command.DBData.CID) + "\r\n", false);
                        delete mQuery;
                        mQuery = NULL;
                    }
                }
            }
            // Если число неудачных попыток меньше необходимого, то на повтор
            if(command.BadAttempts < command.DBData.Attempts)
            {
                command.Answer.clear(); // Очистка массива ответных данных
                command.StartTime = QDateTime();
                command.FinishTime = QDateTime();
                command.Status = "READY";
                CommandTable.replace(i, command);
            }
            // Иначе уменьшение счетчика команд
            else
            {
                // 12.04.2017 Запись статуса команды в таблицу статусов
                if(CreateCommandStatusTable())
                {
                    mSQuery->prepare("SELECT number FROM " + SysDB.databaseName() + ".TCommandStat "
                                     "WHERE ObjectID =:A AND CID =:B;");
                    mSQuery->bindValue(":A", command.DBData.ObjectID);
                    mSQuery->bindValue(":B", command.DBData.CID);
                    // Данных по этой команде нет или ошибка запроса, будет попытка добавить строку с данными
                    if((!mSQuery->exec())|(mSQuery->size() <=0))
                    {
                        mSQuery->prepare("INSERT INTO " + SysDB.databaseName() + ".TCommandStat "
                                         "(ObjectID, CID, SESSION, LastTime, Status, Answer) VALUES(:A,:B,:C,:D,:E,:F);");
                        mSQuery->bindValue(":A", command.DBData.ObjectID);
                        mSQuery->bindValue(":B", command.DBData.CID);
                        mSQuery->bindValue(":C", command.Number);
                        mSQuery->bindValue(":D", command.FinishTime);
                        mSQuery->bindValue(":E", command.Status);
                        mSQuery->bindValue(":F", command.Answer, QSql::Binary | QSql::In);
                        if(!mSQuery->exec())
                            emit Print(" > Ошибка записи статуса команды CID:" +
                                       QString::number(command.DBData.CID) + "\r\n", false);
                    }
                    else
                    {
                        mSQuery->prepare("UPDATE " + SysDB.databaseName() + ".TCommandStat "
                                         "SET LastTime =:A, Status =:B, Answer =:E WHERE ObjectID =:C AND CID =:D AND number >0;");
                        mSQuery->bindValue(":A", command.FinishTime);
                        mSQuery->bindValue(":B", command.Status);
                        mSQuery->bindValue(":C", command.DBData.ObjectID);
                        mSQuery->bindValue(":D", command.DBData.CID);
                        mSQuery->bindValue(":E", command.Answer, QSql::Binary | QSql::In);
                        if(!mSQuery->exec())
                            emit Print(" > Ошибка записи статуса команды CID:" +
                                       QString::number(command.DBData.CID) + "\r\n", false);
                    }
                }
                // Если число счетчика равно единице, то следует удалить команду из БД
                // функция сама проверит указатель запроса
                if(command.DBData.CountCommand == 1)
                    DeleteCommand(mSQuery, command);
                // Если счетчик больше единицы, то следует уменьшить его
                // функция сама проверит указатель запроса
                else if(command.DBData.CountCommand > 1)
                    DecCommand(mSQuery, command);
                // Удаление отработанной команды из перечня команл
                CommandTable.removeAt(i);
                i--;
            }
        }
        // Если статус команды COMPLETED, то сброс счетчика ErrorCount в таблице ошибок и удаление команды из
        // перечня команд
        else if(command.Status == "COMPLETED")
        {
            // Если установлен режим терминала, то удаление команды из перечня команд
            if(mode == "TERMINAL")
            {
                CommandTable.removeAt(i);
                i--;
                continue;
            }
            // 12.04.2017 Запись статуса команды в таблицу статусов
            if(CreateCommandStatusTable())
            {
                mSQuery->prepare("SELECT number FROM " + SysDB.databaseName() + ".TCommandStat "
                                 "WHERE ObjectID =:A AND CID =:B;");
                mSQuery->bindValue(":A", command.DBData.ObjectID);
                mSQuery->bindValue(":B", command.DBData.CID);
                // Данных по этой команде нет или ошибка запроса, будет попытка добавить строку с данными
                if((!mSQuery->exec())|(mSQuery->size() <=0))
                {
                    mSQuery->prepare("INSERT INTO " + SysDB.databaseName() + ".TCommandStat "
                                     "(ObjectID, CID, SESSION, LastTime, Status, Answer) VALUES(:A,:B,:C,:D,:E,:F);");
                    mSQuery->bindValue(":A", command.DBData.ObjectID);
                    mSQuery->bindValue(":B", command.DBData.CID);
                    mSQuery->bindValue(":C", command.Number);
                    mSQuery->bindValue(":D", command.FinishTime);
                    mSQuery->bindValue(":E", command.Status);
                    mSQuery->bindValue(":F", command.Answer, QSql::Binary | QSql::In);
                    if(!mSQuery->exec())
                        emit Print(" > Ошибка записи статуса команды CID:" +
                                   QString::number(command.DBData.CID) + "\r\n", false);
                }
                else
                {
                    mSQuery->prepare("UPDATE " + SysDB.databaseName() + ".TCommandStat "
                                     "SET LastTime =:A, Status =:B, Answer =:E WHERE ObjectID =:C AND CID =:D AND number >0;");
                    mSQuery->bindValue(":A", command.FinishTime);
                    mSQuery->bindValue(":B", command.Status);
                    mSQuery->bindValue(":C", command.DBData.ObjectID);
                    mSQuery->bindValue(":D", command.DBData.CID);
                    mSQuery->bindValue(":E", command.Answer, QSql::Binary | QSql::In);
                    if(!mSQuery->exec())
                        emit Print(" > Ошибка записи статуса команды CID:" +
                                   QString::number(command.DBData.CID) + "\r\n", false);
                }
            }
            if(mQuery!=NULL)
            {
                QString eqname = command.DBData.EqName + "/" + command.DBData.EqNumber;
                // Получение строки данных из таблицы ошибок
                mQuery->prepare("SELECT ErrorCount FROM " +
                                ObjectDB.databaseName() + "." + TableName +
                                " WHERE Equipment = :A;");
                mQuery->bindValue(":A", eqname);
                // Если запрос не выполнен или записей не получено, то следует попробовать создать запись и
                // установить значения счетчиков и времени
                if((!mQuery->exec())|
                    (mQuery->size() <= 0))
                {
                    emit Print(" > В таблице ошибок нет записей по прибору:" + eqname + "\r\n", false);
                    emit Print(" > Производится попытка создания записи:\r\n", false);
                    CreateErrorRow(mQuery, command, 0, 0);
                }
                // Если запрос выполнен, то просто обнуление счетчика вподряд идущих ошибок и запись текущего
                // времени
                else
                {
                    mQuery->prepare("UPDATE " +
                                    ObjectDB.databaseName() + "." + TableName +
                                    " SET CurrentTime = :A, ErrorCount = :B WHERE Equipment = :C AND number >0;");
                    mQuery->bindValue(":A", command.FinishTime);
                    mQuery->bindValue(":B", 0);
                    mQuery->bindValue(":C", eqname);
                    if(!mQuery->exec())
                    {
                        emit Print(" > Ошибка записи счетчиков ошибок CID:" +
                                   QString::number(command.DBData.CID) + "\r\n", false);
                        delete mQuery;
                        mQuery = NULL;
                    }
                }
            }
            // Если число счетчика равно единице, то следует удалить команду из БД
            // функция сама проверит указатель запроса
            if(command.DBData.CountCommand == 1)
                DeleteCommand(mSQuery, command);
            // Если счетчик больше единицы, то следует уменьшить его
            // функция сама проверит указатель запроса
            else if(command.DBData.CountCommand > 1)
                DecCommand(mSQuery, command);
            // Удаление отработанной команды из перечня команл
            CommandTable.removeAt(i);
            i--;
        }
    }
    BusyCommandTable.unlock();
    delete mSQuery;
    // Если соединение с БД установлено
    if(mQuery!=NULL)
    {
        delete mQuery;
        return true;
    }
    return false;
}
// Удаление команды из TCommand
bool CListenThread::DeleteCommand(QSqlQuery* mQuery, CCommand command)
{
    if(mQuery ==NULL)return false;
    mQuery->prepare("DELETE FROM " +
                    DBName + ".TCommand WHERE ObjectID = :A AND CID = :B AND number >0;");
    mQuery->bindValue(":A", command.DBData.ObjectID);
    mQuery->bindValue(":B", command.DBData.CID);
    if(!mQuery->exec())
    {
        emit Print(" > Ошибка удаления команды из TCommand CID:" +
                   QString::number(command.DBData.CID) + "\r\n", false);
        return false;
    }
    return true;
}
// Декремент счетчика команды в TCommand
bool CListenThread::DecCommand(QSqlQuery* mQuery, CCommand command)
{
    if(mQuery ==NULL)return false;
    mQuery->prepare("UPDATE " +
                    DBName + ".TCommand SET CountsCommand =:A WHERE ObjectID = :B AND CID = :C AND number >0;");
    mQuery->bindValue(":A", (command.DBData.CountCommand-1));
    mQuery->bindValue(":B", command.DBData.ObjectID);
    mQuery->bindValue(":C", command.DBData.CID);
    if(!mQuery->exec())
    {
        emit Print(" > Ошибка изменения CountsCommand в TCommand CID:" +
                   QString::number(command.DBData.CID) + "\r\n", false);
        return false;
    }
    return true;
}
// Создание записи в таблице ошибок
// 10.06.2017 База данных заменена на объектную т.к. таблица ошибок - объектная
bool CListenThread::CreateErrorRow(QSqlQuery* mQuery, CCommand command,
                                   quint32 GeneralCount, quint32 ErrorCount)
{
    if(mQuery == NULL)return false;
    QString TableName = "t_" + QString::number(command.DBData.ObjectID) + "_errors";
    QString eqname = command.DBData.EqName + "/" + command.DBData.EqNumber;
    mQuery->prepare("INSERT INTO " +
                    ObjectDB.databaseName() + "." + TableName +
                    " (Equipment, LastTime, LastError, CurrentTime, GeneralCount, ErrorCount)"
                    " VALUES (:A, :B, :C, :D, :E, :F);");
    mQuery->bindValue(":A", eqname);
    mQuery->bindValue(":B", command.FinishTime);
    mQuery->bindValue(":C", command.Status);
    mQuery->bindValue(":D", command.FinishTime);
    mQuery->bindValue(":E", GeneralCount);
    mQuery->bindValue(":F", ErrorCount);
    if(!mQuery->exec())
    {
        emit Print(" > Ошибка записи. Данные в БД не изменены\r\n", false);
        delete mQuery;
        mQuery = NULL;
        return false;
    }
    else
        emit Print(" > Запись произведена\r\n", false);
    return true;
}
// Запись ошибки для клиента терминальной команды
bool CListenThread::SetTerminalError(QSqlQuery* mQuery, CCommand command)
{
    if(mQuery == NULL)return false;
    QStringList str;
    BusyObject.lock();
    quint32 ID = ObjectID;
    BusyObject.unlock();
    if(command.Status == "TIMEOUT")
        str << "Нет ответа";
    else
        str << "Ошибка";
    QByteArray A = serialize(str);
    mQuery->prepare("UPDATE " + DBName +".TObject SET TerminalCommand = :A WHERE ObjectID ="
                    + QString::number(ID) + " ;");
    mQuery->bindValue(":A", A, QSql::Binary | QSql::In );
    if(!mQuery->exec())
    {
        emit Print(" > Ошибка изменения TerminalCommand в TObject, статус командны:" +
                   command.Status + "\r\n", false);
        return false;
    }
    return true;
}
// Сброс счетчика молчания сокета
void CListenThread::ResetSilenceOfSocket()
{
    SilenceOfSocket = 0;
}
// 12.04.2017 Создание таблицы статистики команд (данная функция также содержится в драйверах, в зависимости от
//            того какая часть ПО обращается к таблице статуса команды
bool CListenThread::CreateCommandStatusTable()
{
    QStringList mTableList = SysDB.tables();
    // Поиск таблицы статусов команд
    bool IsTable = false;
    foreach (const QString &mTable, mTableList)
    {
        if(mTable == "tcommandstat"){ IsTable = true; break; }
    }
    // Таблица статусов не найдена - следует создать
    if(IsTable ==false)
    {
        emit Print(" > Таблица статусов TCommandStat не найдена\r\n", false);
        emit Print(" > Попытка создания таблицы\r\n", false);
        QSqlQuery* mQuery = new QSqlQuery(SysDB);
        QString str = "CREATE TABLE IF NOT EXISTS " + SysDB.databaseName() + ".TCommandStat ( "
                      "number INTEGER PRIMARY KEY AUTO_INCREMENT, "
                      "ObjectID INT UNSIGNED DEFAULT NULL, "
                      "LastTime DATETIME DEFAULT NOW(), "
                      "CID INT UNSIGNED DEFAULT NULL, "
                      "SESSION INT UNSIGNED DEFAULT NULL, "
                      "Status VARCHAR(100) DEFAULT NULL, "
                      "Answer BLOB, "
                      "ErrorCode VARCHAR(100)"
                                        ");";
        if (!mQuery->exec(str))
        {
            emit Print(" > Таблица статусов не создана, ошибка запроса\r\n", false);
            qDebug() << " > Не удалось создать таблицу статусов команд" << mQuery->lastError();
            emit SendLastErrorDB("ОШИБКА СОЗДАНИЯ ТАБЛИЦЫ СТАТУСОВ КОМАНД");
            delete mQuery;
            return false;
        }
        else
        {
            emit Print(" > Таблица статусов успешно создана\r\n", false);
            emit SendDeleteTextLastErrorDB("ОШИБКА СОЗДАНИЯ ТАБЛИЦЫ СТАТУСОВ КОМАНД");
            delete mQuery;
        }
    }
    return true;
}
// 13.06.2017 Отправка данных из DLL объекту
void CListenThread::SendDataFromDLL(QByteArray data, int delay)
{
    emit Print(" > Отправка данных драйвера оборудованию\r\n", false);
    emit SendDataToClient(data, delay);
}
