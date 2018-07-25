#include "CDispatchThread.h"
#include "CListenThread.h"
#include "CheckSum.h"
#include "global.h"
#include "General.h"

typedef QString (*Fct) (CDispatchThread*);
typedef QString (*GetDllInfo) (QString &data);
// Конструктор потока отправки данных
CDispatchThread::CDispatchThread(QObject* parent, QString DObjectName , quint32 PTimeOut): QThread(0)
{
    DllFlag = true;
    PingTimeOut = PTimeOut;
    ThreadDelete =false;
    par = (void*)parent;
    CListenThread* Parent = (CListenThread*)parent;
    // Установка указателей на необходимые данные объекта "родителя"
    pSendPing = &(Parent->SendPing);
    pCommandInWork = &(Parent->CommandInWork);
    pCommandTable = &(Parent->CommandTable);
    pBusyCommandTable = &(Parent->BusyCommandTable);
    pControlData = Parent->ControlData;
    pBusyControlData = &(Parent->BusyControlData);
    pDriverData = &(Parent->DriverData);
    pBusyDriverData = &(Parent->BusyDriverData);
    Parent->PortHost.lock();
    port = Parent->Port;
    host = Parent->Host;
    login = Parent->Login;
    password = Parent->Password;
    Parent->PortHost.unlock();
    Parent->BusyObject.lock();
    ObjectID = Parent->ObjectID;
    Parent->BusyObject.unlock();
    ObjectName = DObjectName;
    BusyDriverData.lock();
    DriverObjectName = DObjectName;
    DriverObjectID = ObjectID;
    DriverLogin = login;
    DriverPassword = password;
    BusyDriverData.unlock();
    pBusyDateTime = Parent->DateTime;
    pDate = Parent->mDate;
    pTime = Parent->mTime;
    Packet.clear();                   // Первоначальная отчистка отформатированной команды
    pIsConnected = &(Parent->IsConnected);
    // 03.03.2017 Для удаления библиотеки следует проверить указатель
    lib = NULL;

}
// Поток объекта
void CDispatchThread::run()
{
    db = QSqlDatabase::addDatabase("QMYSQL", ObjectName);
    db.setDatabaseName(DBName);                                      // Постоянное имя БД
    db.setUserName(login);                                           // Работа с правами разработчика обязательна
    db.setHostName(host);                                            // Адрес или имя хоста из текстового поля
    db.setPort(port.toInt());                                        // Порт из текстовго поля
    db.setPassword(password);                                        // Пароль сервера
    while(!ThreadDelete)
    {
        msleep(100);
        // Если системная БД не открывается, то не следует продолжать
        if(!db.open())
            continue;
        // Если установлен флаг отправки пинга, то сначала пинг
        if((*pIsConnected != false)&(*pSendPing == true))
        {
            BusyPacket.lock();
            Packet = ControlPing();
            emit SendDataToClient(Packet, 0);
            BusyPacket.unlock();
            *pSendPing = false;
            msleep(100);
            continue;
        }
        // Можно ли начинать работу отправки
        if(!OnWork())continue;
        // Выборка команды для отправки
        BusyCurrentCommand.lock();
        if(!GetCommandFromTable(&CurrentCommand))
        {
            BusyCurrentCommand.unlock();
            continue;
        }
        // Отправка данных объекту
        QString type = CurrentCommand.DBData.TypeCommand;
        quint32 CommandDelay = CurrentCommand.DBData.Delay;
        CCommand command;
        BusyCurrentCommand.unlock();
        if((type == "STRIGHT")|(type == "TERMINAL(N)")|(type == "TERMINAL(Y)")|(type == "PLC"))
        {
            // 1. Подготовка пакета для отправки
            GetPacket();
            // 2. Отправка
            BusyPacket.lock();
            emit SendDataToClient(Packet, 0);
            BusyPacket.unlock();
            UpdateCommand("PROCESSING");
            quint32 minDelay =0;
            QString status;
            bool IsFinded = false;
            while(minDelay < (CommandDelay + 5000))
            {
                // Если вдруг завершается работа потока, то сразу выход без ожидания проверки
                if(ThreadDelete)break;
                status.clear();
                // Если статус не получен, то данная команда обработана и удалена из перечня команд родительским потоком
                if(!GetCurrentStatus(&status))
                {
// ТЕСТ
//                  emit Print(" ТЕСТИРОВАНИЕ статус команды не получен\r\n", false);
                    IsFinded = true;
                    break;
                }
                // Статус обработанной команды получен
                else if((status == "COMPLETED")|(status == "ERROR"))
                {
// ТЕСТ
//                    emit Print(" ТЕСТИРОВАНИЕ CDispatcher статус команды " + status + "\r\n", false);
                    IsFinded = true;
                    break;
                }
/* ТЕСТ      else
                {
                    if(((CListenThread*)par)->CommandInWork == true)
                        emit Print(" ТЕСТИРОВАНИЕ идет обработка в CAnswer\r\n", false);
                }*/
                msleep(100);
                if(((CListenThread*)par)->CommandInWork != true) // Если идет обработка в CAnswer то не изменять счетчик
                    minDelay +=100;
            }
            // Статус не менялся, либо не был корректно изменен
            if(!IsFinded)
            {
//                BusyCurrentCommand.lock();
//                emit Print(" ТЕСТИРОВАНИЕ Dispather CID " + QString::number(CurrentCommand.DBData.CID) +
//                           " SESSION " + QString::number(CurrentCommand.Number) + "\r\n", false);
//                BusyCurrentCommand.unlock();
//                GetCurrentStatus(&status);
//                if(status == "PROCESSING")
                UpdateCommand("TIMEOUT");
                BusyCurrentCommand.lock();
                command = CurrentCommand;
                BusyCurrentCommand.unlock();
                SaveCode(command, "Таймаут ожидания");
/* ТЕСТ */  //    GetCurrentStatus(&status);
/* ТЕСТ */  //    emit Print(" ТЕСТИРОВАНИЕ статус команды " + status + "\r\n", false);
            }
        }
        else if(type.contains("DRIVER("))
        {
            QString errorcode = GetErrorString(GENERALERROR);
            if(lib == NULL)
                lib = new QLibrary(0);
            // 1. Поиск DLL
            if(!OpenDLL())
            {
                // При неудачном поиске драйвера запись статусов и времени, для обработчика
                UpdateCommand("PROCESSING"); // Для записи метки времени начала
                UpdateCommand("ERROR");
                BusyCurrentCommand.lock();
                command = CurrentCommand;
                BusyCurrentCommand.unlock();
                SaveCode(command, errorcode);
                continue;
            }
            // 2. Передача в функцию данных
            UpdateCommand("PROCESSING");
            Fct fct = (Fct)(lib->resolve("GetData"));
            if(fct)
            {
                // Драйвер возвращает статус завершения и изменяет CurrentCommand в случае если данные ПЛК корректны
                // в тоже время данные полученные от оборудования могут быть некорректны и код завершения будет ошибочным
                errorcode = fct(this);
                BusyCurrentCommand.lock();
                command = CurrentCommand;
                BusyCurrentCommand.unlock();
                if(errorcode!= "Нет ошибок")
                    UpdateCommand("ERROR");
                else
                    UpdateCommand("COMPLETED");
                SaveCode(command, errorcode);
            }
            else
            {
                // При неудачном поиске функции запись статусов и времени, для обработчика
                UpdateCommand("ERROR");
                BusyCurrentCommand.lock();
                command = CurrentCommand;
                BusyCurrentCommand.unlock();
                SaveCode(command, errorcode);
            }
            // 3. Выгрузка драйвера
            lib->unload();
        }
        // Если тип команды некорректен(?) то запись времени и статусов для обработчика команд, а также
        // для исключения повторения отправки и обработки данной команды
        else
        {
            UpdateCommand("PROCESSING");
            UpdateCommand("ERROR");
            BusyCurrentCommand.lock();
            command = CurrentCommand;
            BusyCurrentCommand.unlock();
            SaveCode(command, "Неизвестный тип команды");
        }
    }
    // 03.03.2017 Если производится выход из потока а библиотека открыта, то следует ее сначала удалить
    if(lib!=NULL)
    {
        lib->unload();
        {delete lib;}
    }
    // Закрытие базы данных
    {
        db.close();
    }
    QSqlDatabase::removeDatabase(ObjectName);
}
// Выполняет проверку условий для начала работы отправки команд
bool CDispatchThread::OnWork()
{
    bool work = false;
#ifndef TESTDISPATCH
    // 1. Если нет соединения с объектом
    if(*pIsConnected == false)
        return false;
#endif
    // 2. Если перечень команд пуст
    pBusyCommandTable->lock();
    if(pCommandTable->isEmpty())
    {
        pBusyCommandTable->unlock();
        return work;
    }
    // 3. Проверка наличия в перечне команд которые следует выполнить
    for(int i=0; i < pCommandTable->size(); i++)
    {
        if(pCommandTable->at(i).Status == "READY")
        {
            work = true;
            break;
        }
    }
    pBusyCommandTable->unlock();
    return work;
}
// Производит выборку одной команды из перечня по приоритету, либо по индексу, если приоритет одинаковый
bool CDispatchThread::GetCommandFromTable(CCommand* command)
{
    quint8 pr =0;             // Приоритет
    bool findcommand = false; // Найдена ли команда
    pBusyControlData->lock();
    pBusyControlData->unlock();
    pBusyCommandTable->lock();
    // Поиск команд READY с наибольшим приоритетом
    for(int i=0; i < pCommandTable->size(); i++)
    {
        if(pCommandTable->at(i).Status == "READY")
        {
            if(pCommandTable->at(i).DBData.Priority >pr)
            {
                *command = pCommandTable->at(i);
                pr = pCommandTable->at(i).DBData.Priority;
                findcommand =true;
            }
        }
    }
    pBusyCommandTable->unlock();
    return findcommand;
}
// Подготовка пакета для отправки команды
void CDispatchThread::GetPacket()
{
    // Создание пакета для отправки
    BusyCurrentCommand.lock();
    BusyPacket.lock();
    Packet.clear();
    // Команда должна транслироваться прямо оборудованию
    if((CurrentCommand.DBData.TypeCommand == "STRIGHT")|(CurrentCommand.DBData.TypeCommand == "TERMINAL(N)"))
        Packet = CurrentCommand.DBData.Command;
    // Команда отправляется через контроллер
    else if((CurrentCommand.DBData.TypeCommand == "PLC")|(CurrentCommand.DBData.TypeCommand == "TERMINAL(Y)"))
    {
        // Идентификатор сервера
        unsigned long IDS =1;                               // Идентификатор сервера (всегда равен 1)
        unsigned char var =0;                               // Рабочая переменная для вставки в массив
        var = (unsigned char) (IDS >>24);                   // Старший байт
        Packet.append(var);
        var = (unsigned char) ((IDS << 8)>>24);             // Средний старший байт
        Packet.append(var);
        var = (unsigned char) ((IDS << 16)>>24);            // Средний младший байт
        Packet.append(var);
        var = (unsigned char) ((IDS << 24)>>24);            // Младший байт
        Packet.append(var);

        // Идентификатор объекта
        unsigned long ID = (unsigned long) CurrentCommand.DBData.ObjectID;
        var = (unsigned char) (ID >>24);                   // Старший байт
        Packet.append(var);
        var = (unsigned char) ((ID << 8)>>24);             // Средний старший байт
        Packet.append(var);
        var = (unsigned char) ((ID << 16)>>24);            // Средний младший байт
        Packet.append(var);
        var = (unsigned char) ((ID << 24)>>24);            // Младший байт
        Packet.append(var);

        // Идентификатор команды
        unsigned long CID = (unsigned long) CurrentCommand.DBData.CID;
        var = (unsigned char) (CID >>24);                  // Старший байт
        Packet.append(var);
        var = (unsigned char) ((CID << 8)>>24);            // Средний старший байт
        Packet.append(var);
        var = (unsigned char) ((CID << 16)>>24);           // Средний младший байт
        Packet.append(var);
        var = (unsigned char) ((CID << 24)>>24);           // Младший байт
        Packet.append(var);

        // Сеансовый идентификатор команды
        unsigned long NUMBER = (unsigned long) CurrentCommand.Number;
        var = (unsigned char) (NUMBER >>24);               // Старший байт
        Packet.append(var);
        var = (unsigned char) ((NUMBER << 8)>>24);         // Средний старший байт
        Packet.append(var);
        var = (unsigned char) ((NUMBER << 16)>>24);        // Средний младший байт
        Packet.append(var);
        var = (unsigned char) ((NUMBER << 24)>>24);        // Младший байт
        Packet.append(var);

        // Код функции
        Packet.append(CurrentCommand.DBData.Funct);

        // Номер порта
        Packet.append(CurrentCommand.DBData.PortNum);

        // Скорость работы порта
        unsigned long SPEED = CurrentCommand.DBData.SpeedCom;
        var = (unsigned char) (SPEED >>24);                  // Старший байт
        Packet.append(var);
        var = (unsigned char) ((SPEED << 8)>>24);            // Средний старший байт
        Packet.append(var);
        var = (unsigned char) ((SPEED << 16)>>24);           // Средний младший байт
        Packet.append(var);
        var = (unsigned char) ((SPEED << 24)>>24);           // Младший байт
        Packet.append(var);

        // Количество бит в байте
        Packet.append(CurrentCommand.DBData.DataCom);

        // Стоповые биты
        Packet.append(CurrentCommand.DBData.StopBits);

        // Биты паритета
        Packet.append(CurrentCommand.DBData.ParityBits);

        // Длина команды передаваемой оборудованию
        unsigned long BUFFLENGTH = CurrentCommand.DBData.Command.size();
        var = (unsigned char) (BUFFLENGTH >>24);            // Старший байт
        Packet.append(var);
        var = (unsigned char) ((BUFFLENGTH << 8)>>24);      // Средний старший байт
        Packet.append(var);
        var = (unsigned char) ((BUFFLENGTH << 16)>>24);     // Средний младший байт
        Packet.append(var);
        var = (unsigned char) ((BUFFLENGTH << 24)>>24);     // Младший байт
        Packet.append(var);

        // Задержка ответа от оборудования
        unsigned long EQDELAY = CurrentCommand.DBData.Delay;
        var = (unsigned char) (EQDELAY >>24);                // Старший байт
        Packet.append(var);
        var = (unsigned char) ((EQDELAY << 8)>>24);          // Средний старший байт
        Packet.append(var);
        var = (unsigned char) ((EQDELAY << 16)>>24);         // Средний младший байт
        Packet.append(var);
        var = (unsigned char) ((EQDELAY << 24)>>24);         // Младший байт
        Packet.append(var);

        // Команда оборудованию
        Packet.append(CurrentCommand.DBData.Command);

        // CRC16 сообщения
        unsigned char KS_H, KS_L;
        GetCRC16((unsigned char *)(Packet.data()),
                 &KS_H, &KS_L,
                 (unsigned long) (Packet.length()+2));  // Расчет CRC16
        Packet.append(KS_H);
        Packet.append(KS_L);
    }
    BusyCurrentCommand.unlock();
    BusyPacket.unlock();
    return;
}
// Поиск и открытие драйвера прибора
bool CDispatchThread::OpenDLL()
{
    bool ExitCode =false;
    BusyCurrentCommand.lock();
    QString DriverName = CurrentCommand.DBData.TypeCommand;
    quint32 CID = CurrentCommand.DBData.CID;
    BusyCurrentCommand.unlock();
    DriverName.remove("DRIVER(");
    DriverName.remove(")");
    // Проверяется есть ли имя файла т.к. при проверке типа команды имя драйвера не проверяется
    if(DriverName.isEmpty())
    {
        emit SendLastErrorDB("ОШИБКА ИМЕНИ ДРАЙВЕРА В КОМАНДЕ CID:" + QString::number(CID));
        return ExitCode;
    }
    emit SendDeleteTextLastErrorDB("ОШИБКА ИМЕНИ ДРАЙВЕРА В КОМАНДЕ CID:" + QString::number(CID));
    // Если объект библиотеки создан
    if(lib!=NULL)
    {
        bool unload = true;
        // Если загружено что-то, то сначала следует выгрузить
        if(lib->isLoaded())
        {
            unload = lib->unload();
            if(!unload)
                emit Print(" > Драйвер прибора не выгружен\r\n", false);
            else
                emit Print(" > Драйвер прибора выгружен успешно\r\n", false);
        }
        // Если выгружена
        if(unload)
        {
            /*if(!QFile::exists(QCoreApplication::applicationDirPath() + "/drivers/"+ QString::number(ObjectID) + DriverName + ".dll"))
                QFile::copy(QCoreApplication::applicationDirPath() + "/drivers/" + DriverName + ".dll",
                            QCoreApplication::applicationDirPath() + "/drivers/"+ QString::number(ObjectID) + DriverName + ".dll");*/
            QString source, destination;
            source = QCoreApplication::applicationDirPath() + "/drivers/" + DriverName + ".dll";
            destination = QCoreApplication::applicationDirPath() + "/drivers/" +
                          QString::number(ObjectID) + DriverName;

            CopyFiles(source, destination + ".dll", DllFlag);
//            emit SendCopyFile(source, destination + ".txt", "object" + QString::number(ObjectID), DllFlag);
            DllFlag = true;

            lib->setFileName(destination);
            if(lib->load())
            {
                GetDllInfo fct = (GetDllInfo)(lib->resolve("GetInfoDll"));
                if(fct)
                {
                    QString type;
                    QString info = fct(type);
                    if((!info.isEmpty())&(!type.isEmpty()))
                    {
                        emit Print(" > Драйвер прибора: " + info + "\r\n", false);
                        emit Print(" > Тип драйвера : " + type + "\r\n", false);
                        ExitCode =true;
                    }
                }
            }
            else
            {
                emit Print(" > Драйвер " + destination + " не открыт\r\n", false);
                emit Print(" > Ошибка " +  lib->errorString() + "\r\n", false);
            }
        }
    }
    // Попытка найти функцию неудалась, либо загрузка драйвера неудачна
    if(!ExitCode)
        emit SendLastErrorDB("ОШИБКА ОТКРЫТИЯ ДРАЙВЕРА " + DriverName);
    else
        emit SendDeleteTextLastErrorDB("ОШИБКА ОТКРЫТИЯ ДРАЙВЕРА " + DriverName);
    return ExitCode;
}
// Запись статуса в перечень команд и в текущую команду
// Если CID или SessionNumber не найден, то возвращает false
bool CDispatchThread::SetStatus(QString status)
{
    BusyCurrentCommand.lock();
    quint32 SessionNumber = CurrentCommand.Number;
    quint32 CID = CurrentCommand.DBData.CID;
    BusyCurrentCommand.unlock();
    pBusyCommandTable->lock();
    bool IsFinded = false;
    for(int i=0; i < pCommandTable->size(); i++)
    {
        // Будут меняться только команды с таким же CID и сеансовым номером т.к. могут быть и отработанные команды
        // в зависимости от частоты обработки перечня команд родительским потоком
        if((CID == pCommandTable->at(i).DBData.CID)&(SessionNumber == pCommandTable->at(i).Number))
        {
            BusyCurrentCommand.lock();
            CurrentCommand.Status = status;
            pCommandTable->replace(i, CurrentCommand);
            BusyCurrentCommand.unlock();
            IsFinded = true;
            break;
        }
    }
    pBusyCommandTable->unlock();
    return IsFinded;
}
// Получение текущего статуса
// Если CID или SessionNumber не найден, то возвращает false
bool CDispatchThread::GetCurrentStatus(QString* status)
{
    BusyCurrentCommand.lock();
    quint32 SessionNumber = CurrentCommand.Number;
    quint32 CID = CurrentCommand.DBData.CID;
    BusyCurrentCommand.unlock();
    pBusyCommandTable->lock();
    bool IsFinded = false;
    for(int i=0; i < pCommandTable->size(); i++)
    {
        // Ищется только команда с указанным CID и сеансовым номером
        if((CID == pCommandTable->at(i).DBData.CID)&(pCommandTable->at(i).Number ==SessionNumber))
        {
            status->clear();
            status->append(pCommandTable->at(i).Status);
            IsFinded = true;
            break;
        }
    }
    pBusyCommandTable->unlock();
    return IsFinded;
}
// Запись времени отправки и времени окончания
// Внимание, при записи времени начала, запись окончания не будет произведена
bool CDispatchThread::SetDateTime(bool Start)
{
    // Сеансовый номер команды
    BusyCurrentCommand.lock();
    quint32 SessionNumber = CurrentCommand.Number;
    quint32 CID = CurrentCommand.DBData.CID;
    BusyCurrentCommand.unlock();
    // Получение даты и времени из родительского потока
    QDateTime dt;
    pBusyDateTime->lock();
    dt.setDate(*pDate);
    dt.setTime(*pTime);
    pBusyDateTime->unlock();
    // Поиск необходимой команды в перечне, в которую следует произвести запись
    pBusyCommandTable->lock();
    bool IsFinded = false;
    for(int i=0; i < pCommandTable->size(); i++)
    {
        // Ищется только команда с указанным CID и сеансовым номером
        if((CID == pCommandTable->at(i).DBData.CID)&(pCommandTable->at(i).Number ==SessionNumber))
        {
            BusyCurrentCommand.lock();
            // Если производится запись времени начала
            if(Start)
                CurrentCommand.StartTime = dt;
            // Времени окончания
            else
                CurrentCommand.FinishTime = dt;
            pCommandTable->replace(i, CurrentCommand);
            IsFinded =true;
            BusyCurrentCommand.unlock();
            break;
        }
    }
    pBusyCommandTable->unlock();
    return IsFinded;
}
// Обработчик таблицы команд по полученному статусу
bool CDispatchThread::UpdateCommand(QString status)
{
    bool IsFinded = false;
    // Если статус какой-то случайный, то не обрабатывается
    if((status!="READY")&(status!="PROCESSING")&(status!="ERROR")&(status!="TIMEOUT")&(status!="COMPLETED"))
        return IsFinded;
    // Если статус PROCESSING, тогда запись статуса и метки времени отправки
    if(status == "PROCESSING")
    {
        if(SetStatus(status))
            IsFinded = SetDateTime();
        // 12.04.2017 Запись статуса команды в таблицу статусов
        if((IsFinded == true)&(CreateCommandStatusTable() == true))
        {
            BusyCurrentCommand.lock();
            CCommand command = CurrentCommand;
            BusyCurrentCommand.unlock();
            QSqlQuery *mSQuery = new QSqlQuery(db);
            mSQuery->prepare("SELECT number FROM " + db.databaseName() + ".TCommandStat "
                             "WHERE ObjectID =:A AND CID =:B;");
            mSQuery->bindValue(":A", command.DBData.ObjectID);
            mSQuery->bindValue(":B", command.DBData.CID);
            // Данных по этой команде нет или ошибка запроса, будет попытка добавить строку с данными
            if((!mSQuery->exec())|(mSQuery->size() <=0))
            {
                mSQuery->prepare("INSERT INTO " + db.databaseName() + ".TCommandStat "
                                 "(ObjectID, CID, SESSION, LastTime, Status) VALUES(:A,:B,:C,:D,:E);");
                mSQuery->bindValue(":A", command.DBData.ObjectID);
                mSQuery->bindValue(":B", command.DBData.CID);
                mSQuery->bindValue(":C", command.Number);
                mSQuery->bindValue(":D", command.StartTime);
                mSQuery->bindValue(":E", command.Status);
                if(!mSQuery->exec())
                    emit Print(" > Ошибка записи статуса команды CID:" +
                               QString::number(command.DBData.CID) + "\r\n", false);
                else
                    SaveCode(command, "В процессе обработки");
            }
            else
            {
                mSQuery->prepare("UPDATE " + db.databaseName() + ".TCommandStat "
                                 "SET LastTime =:A, Status =:B WHERE ObjectID =:C AND CID =:D AND number >0;");
                mSQuery->bindValue(":A", command.StartTime);
                mSQuery->bindValue(":B", command.Status);
                mSQuery->bindValue(":C", command.DBData.ObjectID);
                mSQuery->bindValue(":D", command.DBData.CID);
                if(!mSQuery->exec())
                    emit Print(" > Ошибка записи статуса команды CID:" +
                               QString::number(command.DBData.CID) + "\r\n", false);
                else
                    SaveCode(command, "В процессе обработки");
            }
            delete mSQuery;
        }
        return IsFinded;
    }
    // Если статус TIMEOUT, ERROR или COMPLETED то запись статуса времени окончания
    else if((status =="TIMEOUT")|(status =="ERROR")|(status =="COMPLETED"))
    {
        if(SetStatus(status))
            IsFinded = SetDateTime(false);
        return IsFinded;
    }
    // Все остальные статусы обрабатываются в родительском потоке
    IsFinded = true;
    return IsFinded;
}
// Подготовка контрольного пинга
QByteArray CDispatchThread::ControlPing()
{
    // Подготовка пакета для отправки
    QByteArray PingString;
    // IDS - сервера
    quint32 Data = 0x1;
    PingString.append((char)((unsigned char)(Data >>24)));           // H
    PingString.append((char)((unsigned char)((Data <<8) >>24)));
    PingString.append((char)((unsigned char)((Data <<16) >>24)));
    PingString.append((char)((unsigned char)((Data <<24) >>24)));    // L
    // ID - объекта
    Data = ObjectID;
    PingString.append((char)((unsigned char)(Data >>24)));           // H
    PingString.append((char)((unsigned char)((Data <<8) >>24)));
    PingString.append((char)((unsigned char)((Data <<16) >>24)));
    PingString.append((char)((unsigned char)((Data <<24) >>24)));    // L
    // CID - идентификатор команды для пинда 0
    PingString.append('\x0');
    PingString.append('\x0');
    PingString.append('\x0');
    PingString.append('\x0');
    // SESSION(NUMBER команды) - для пинга всегда 0
    PingString.append('\x0');
    PingString.append('\x0');
    PingString.append('\x0');
    PingString.append('\x0');
    // Функция контроля соединения №4 (пинг)
    PingString.append('\x4');
     // Номер порта 1-4 - можно любой т.к. не используется
    PingString.append('\x4');
    // Скорость работы порта - не используется
    Data = 115200;
    PingString.append((char)((unsigned char)(Data >>24)));           // H
    PingString.append((char)((unsigned char)((Data <<8) >>24)));
    PingString.append((char)((unsigned char)((Data <<16) >>24)));
    PingString.append((char)((unsigned char)((Data <<24) >>24)));    // L
    // Бит в байте
    PingString.append('\x8');
    // Стоповый бит
    PingString.append('\x1');
    // Биты паритета
    PingString.append('\x0');
    // Длина данных в пакете (4 байта - размер таймаута)
    Data = 0x4;
    PingString.append((char)((unsigned char)(Data >>24)));           // H
    PingString.append((char)((unsigned char)((Data <<8) >>24)));
    PingString.append((char)((unsigned char)((Data <<16) >>24)));
    PingString.append((char)((unsigned char)((Data <<24) >>24)));    // L
    // Таймаут оборудования - не используется при пингах
    Data = 500;
    PingString.append((char)((unsigned char)(Data >>24)));           // H
    PingString.append((char)((unsigned char)((Data <<8) >>24)));
    PingString.append((char)((unsigned char)((Data <<16) >>24)));
    PingString.append((char)((unsigned char)((Data <<24) >>24)));    // L
    // Частота пинга (в версии 1.02 передается сервером к ПЛК)
    Data = PingTimeOut;
    PingString.append((char)((unsigned char)(Data >>24)));           // H
    PingString.append((char)((unsigned char)((Data <<8) >>24)));
    PingString.append((char)((unsigned char)((Data <<16) >>24)));
    PingString.append((char)((unsigned char)((Data <<24) >>24)));    // L
    // CRC16 сообщения
    unsigned char KS_H, KS_L;
    // Возможно 2-е вхождение в функцию установка флага занятости
    GetCRC16((unsigned char *)(PingString.data()), &KS_H, &KS_L,(unsigned long) (PingString.length()+2));
    PingString.append(KS_H);
    PingString.append(KS_L);
    return PingString;
}
// 12.04.2017 Создание таблицы статистики команд (данная функция также содержится в драйверах, в зависимости от
//            того какая часть ПО обращается к таблице статуса команды
bool CDispatchThread::CreateCommandStatusTable()
{
    QStringList mTableList = db.tables();
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
        QSqlQuery* mQuery = new QSqlQuery(db);
        QString str = "CREATE TABLE IF NOT EXISTS " + db.databaseName() + ".TCommandStat ( "
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
// 11.06.2017 Запись кода завершения ПЛК-команды в TCommandStat
bool CDispatchThread::SaveCode(CCommand& command, QString error)
{
    if(!db.open())
    {
        qDebug() << " > CDispatchThread::SaveCode: База данных не открыта, код ошибки команды записан не будет : " << db.lastError().text();
        emit Print(" > Код ошибки команды CID:" + QString::number(command.DBData.CID) + " не записан\r\n", false);
        emit Print(" > Системная база данных не открыта\r\n", false);
        return false;
    }
    QSqlQuery* mQuery = new QSqlQuery(db);
    mQuery->prepare("UPDATE " + DBName + ".TCommandStat SET ErrorCode =:A WHERE ObjectID =:B AND CID =:C AND number >0;");
    mQuery->bindValue(":A", error);
    mQuery->bindValue(":B", ObjectID);
    mQuery->bindValue(":C", command.DBData.CID);
    if(!mQuery->exec())
    {
        qDebug() << " > CDispatchThread::SaveCode: Запрос записи кода ошибки не был выполнен :" << mQuery->lastError().text();
        emit Print(" > Запись кода ошибки команды CID:" + QString::number(command.DBData.CID) + " не выполнена\r\n", false);
        emit Print(" > Ошибка выполнения запроса: \"" + mQuery->lastError().text() + "\"\r\n", false);
        emit SendLastErrorDB("ОШИБКА ЗАПИСИ КОДА ОШИБКИ");
        if(mQuery->lastError().text().contains("doesn't exist",Qt::CaseInsensitive)==true)
        delete mQuery;
        return false;
    }
    emit SendDeleteTextLastErrorDB("ОШИБКА ЗАПИСИ КОДА ОШИБКИ");
    qDebug() << " > CDispatchThread::SaveCode: Запись кода ошибки выполнена";
    delete mQuery;
    return true;
}
bool CDispatchThread::CopyFiles(QString from, QString to, bool dllflag)
{
    QFile sourceFile;
    QFile destFile;
    sourceFile.setFileName( from );
    destFile.setFileName( to );
    bool success = true;
    if((!destFile.exists())|(dllflag == false))
    {
        success = sourceFile.open( QFile::ReadOnly );
        if(!success)
        {
            emit Print(" > Файл исходного драйвера не открыт " + sourceFile.fileName() + " \r\n", false);
            emit Print(" >  Код ошибки: " + QString::number(sourceFile.error()) + "\r\n", false);
            sourceFile.close();
            return false;
        }
        QByteArray array = sourceFile.readAll();
        sourceFile.close();
        //success = destFile.open( QFile::WriteOnly | QFile::Truncate );
        success = destFile.open(QIODevice::WriteOnly);
        if(!success)
        {
            emit Print(" > Файл целевого драйвера не открыт " + destFile.fileName() + " \r\n", false);
            emit Print(" >  Код ошибки: " + QString::number(destFile.error()) + "\r\n", false);
            destFile.close();
            return false;
        }
        destFile.resize(0);
        destFile.write( array );
        if(!success)
        {
            emit Print(" > Ошибка записи в целевой файл " + destFile.fileName() + "\r\n", false);
            emit Print(" >  Код ошибки: " + QString::number(destFile.error()) + "\r\n", false);
            destFile.close();
            return false;
        }
        destFile.close();
    }
    return true;
}
