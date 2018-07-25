#include "CAnswer.h"
#include "CListenThread.h"
#include "CheckSum.h"
// Конструктор
CAnswer::CAnswer(void* p)
{
    IsExiting = false;
    par = p;
    CListenThread* Parent = (CListenThread*)p;
    pSendPing = &(Parent->SendPing);
    pCommandInWork = &(((CListenThread*)par)->CommandInWork);
    Parent->BusyObject.lock();
    ObjectID = Parent->ObjectID;
    Parent->BusyObject.unlock();
    Parent->PortHost.lock();
    Host = Parent->Host;
    Port = Parent->Port;
    Login = Parent->Login;
    Password = Parent->Password;
    Parent->PortHost.unlock();
    pBusyControlData = &(Parent->BusyControlData);
    pBusyControlData->lock();
    pControlData = Parent->ControlData;
    pBusyControlData->unlock();
    pBusyCommandTable = &(Parent->BusyCommandTable);
    pBusyCommandTable->lock();
    pCommandTable = &(Parent->CommandTable);
    pBusyCommandTable->unlock();
    pBusyDateTime = Parent->DateTime;
    pBusyDateTime->lock();
    pDate = Parent->mDate;
    pTime = Parent->mTime;
    pBusyDateTime->unlock();
    pBusyDriverData = &(Parent->BusyDriverData);
    pBusyDriverData->lock();
    pDriverData = &(Parent->DriverData);
    pBusyDriverData->unlock();

    SysDB = QSqlDatabase::addDatabase("QMYSQL", "AnswerSys_" + QString::number(ObjectID));
    SysDB.setDatabaseName(DBName);                                      // Постоянное имя БД
    SysDB.setUserName(Login);                                           // Работа с правами разработчика обязательна
    SysDB.setHostName(Host);                                            // Адрес или имя хоста из текстового поля
    SysDB.setPort(Port.toInt());                                        // Порт из текстовго поля
    SysDB.setPassword(Password);                                        // Пароль сервера
}
// 29.11.2016
// Чтение данных из сокета
void CAnswer::ReadAnswer(bool NeedToRead, QByteArray data)
{
    // 1. Получение текущего режима работы потока
    pBusyControlData->lock();
    QString WorkMode = pControlData->CurrentMode;
    pBusyControlData->unlock();
    // 2. Поиск команды со статусом PROCESSING
    CCommand command;
    QByteArray str;
    if(!NeedToRead)str = data; // Если данные не нужно читать из сокета, то будет обработан переданный массив
    if(!GetProcessingCommand(&command))
    {
        // Если команды в режиме PROCESSING нет, то следует проверить не пинг ли это
        if(NeedToRead)
            str = pClientSocket->readAll();
        bool IsOk = false;
        quint8 funct =0;
        // Что-то пришло, хоть команд в процессе и нет
        GetDataPLC(str, &IsOk, funct);
        return;
    }
    // Если установлен режим терминала
    if(WorkMode == "TERMINAL")
    {
        if(NeedToRead)
            str = pClientSocket->readAll();
        // Если команда типа TERMINAL(N), тогда просто запись массива в TerminalCommand
        if(command.DBData.TypeCommand == "TERMINAL(N)")
        {
            command.Answer = str;
            SaveTerminalN(str, command);
        }
        // Если команда TERMINAL(Y), тогда проверка данных ответа и запись в TerminalCommand
        // В экземпляр команды, полученный ответ будет записан внутри функции
        else if(command.DBData.TypeCommand == "TERMINAL(Y)")
            SaveTerminalY(str, command);
        return;
    }
    // Если установлен режим WHILE, REQUEST или TIME
    if(command.DBData.TypeCommand.contains("DRIVER("))
    {
        // Данные в экземпляр команды будут записаны внутри функции
        SaveDriver(NeedToRead, data);
        return;
    }
    // Если ответ пришел от ПЛК - следует проверить данные протокола
    // если команда типа STRIGHT, то обработка будет произведена сразу без предварительной проверки
    else if(command.DBData.TypeCommand == "PLC")
    {
        ((CListenThread*)par)->CommandInWork = true;
        if(NeedToRead)
            str = pClientSocket->readAll();
        bool IsOk = false;
        quint8 funct =0;
        quint8 code;
        QString error;
        code = UNKNOWNERROR;
        str = GetDataPLC(str, &IsOk, funct, &command);
        SetProcessingCommand(str);
        command.Answer = str;
        if(!IsOk)
        {
            error = GetErrorString(code);
            SaveCode(command, error);
            emit Print(" > Ошибка ответа на ПЛК - команду\r\n", false);
            UpdateCommand("ERROR", command);
            ((CListenThread*)par)->CommandInWork = false;
            return;
        }
        // Вернулся код ошибки
        if(funct == 0x3)
        {
            if(!str.isEmpty())
                code = (quint8)str.at(0);
            error = GetErrorString(code);
            SaveCode(command, error);
            emit Print(" > Команда CID:" + QString::number(command.DBData.CID) + " завершилась с ошибкой\r\n", false);
            emit Print(" > Ошибка (код:" + QString::number(code, 16) + ") - " + error +"\r\n", false);
            //command.Answer = GetErrorArray(code);
            UpdateCommand("ERROR", command);
            ((CListenThread*)par)->CommandInWork = false;
            return;
        }
        // Получен пинг
        if(str.isEmpty())
        {
            ((CListenThread*)par)->CommandInWork = false;
            return;
        }
        // 25.05.2017 Ошибки в данных нет, запись кода (нет ошибок - т.е. ответ от оборудования получен, но не проверен)
        error = GetErrorString(0);
        SaveCode(command, error);
    }
    else if(command.DBData.TypeCommand == "STRIGHT")
    {
        command.Answer = str;
        SetProcessingCommand(str);
    }
    // Обработка полученного ответа CVarDB
    CVarDBEngine(str, command);
// ТЕСТ
//    emit Print(" ТЕСТИРОВАНИЕ Answer CID " + QString::number(command.DBData.CID) +
//               " SESSION " + QString::number(command.Number) + "\r\n", false);
//    if(((CListenThread*)par)->CommandInWork == false)
//        emit Print(" ТЕСИРОВАНИЕ флаг обработки команды где-то уже сброшен - ошибка\r\n", false);
//    else
//        emit Print(" ТЕСТИРОВАНИЕ флаг обработки команды не сброшен - норма\r\n", false);
// ТЕСТ
    ((CListenThread*)par)->CommandInWork = false;
}
// 30.11.2016
// Запись данных ответа на прямую терминальную команду
bool CAnswer::SaveTerminalN(QByteArray str, CCommand command)
{
    ((CListenThread*)par)->CommandInWork = true;
    emit ResetSilence();     // Сброс счетчика тишины
    if(SysDB.open())
    {
        QSqlQuery* mQuery = new QSqlQuery(SysDB);
        mQuery->prepare("UPDATE " + DBName + ".TObject SET TerminalCommand =:A WHERE ObjectID =:B AND number >0;");
        mQuery->bindValue(":A", str, QSql::Binary | QSql::In);
        mQuery->bindValue(":B", ObjectID);
        if(mQuery->exec())
        {
            UpdateCommand("COMPLETED", command);
            ((CListenThread*)par)->CommandInWork = false;
            delete mQuery;
            return true;
        }
        delete mQuery;
    }
    // Запись была неудачной
    emit Print(" > Ошибка записи ответа на терманальную команду в БД\r\n", false);
    UpdateCommand("ERROR", command);
    ((CListenThread*)par)->CommandInWork = false;
    return false;
}
// 30.11.2016
// Запись данных ответа на ПЛК - терминальную команду
bool CAnswer::SaveTerminalY(QByteArray str, CCommand command)
{
    ((CListenThread*)par)->CommandInWork = true;
    bool IsOk = false;
    quint8 funct =0;
    quint8 code;
    QString error;
    code = UNKNOWNERROR;
    // Проверка данных ответа
    str = GetDataPLC(str, &IsOk, funct, &command);
    command.Answer = str;
    SetProcessingCommand(str);
    // Данные не прошли проверку
    if(!IsOk)
    {
        error = GetErrorString(code);
        SaveCode(command, error);
        emit Print(" > Ошибка ответа на ПЛК - терминальную команду\r\n", false);
        UpdateCommand("ERROR", command);
        ((CListenThread*)par)->CommandInWork = false;
        return false;
    }
    // 25.05.2017 Вернулся код ошибки
    if(funct == 0x3)
    {
        if(!str.isEmpty())
            code = (quint8)str.at(0);
        error = GetErrorString(code);
        emit Print(" > Команда CID:" + QString::number(command.DBData.CID) + " завершилась с ошибкой\r\n", false);
        emit Print(" > Ошибка (код:" + QString::number(code, 16) + ") - " + error +"\r\n", false);
    }
    // Получен пинг
    if(str.isEmpty())
    {
        ((CListenThread*)par)->CommandInWork = false;
        return true;
    }
    if(SysDB.open())
    {
        QSqlQuery* mQuery = new QSqlQuery(SysDB);
        mQuery->prepare("UPDATE " + DBName + ".TObject SET TerminalCommand =:A WHERE ObjectID =:B AND number >0;");
        if(funct!=0x3)
            mQuery->bindValue(":A", str, QSql::Binary | QSql::In);
        else
            // 25.05.2017 При возврате кода ошибки будет записан текст ошибки
            mQuery->bindValue(":A", GetErrorArray(code), QSql::Binary | QSql::In);
        mQuery->bindValue(":B", ObjectID);
        if(mQuery->exec())
        {
            if(funct!=0x3)
            {
                // 25.05.2017 Ошибки в данных нет, запись кода (нет ошибок - т.е. ответ от оборудования получен, но не проверен)
                error = GetErrorString(0);
                SaveCode(command, error);
                UpdateCommand("COMPLETED", command);
            }
            else
            {
                SaveCode(command, error);
                UpdateCommand("ERROR", command);
            }
            ((CListenThread*)par)->CommandInWork = false;
            delete mQuery;
            return true;
        }
        delete mQuery;
    }
    // Запись была неудачной
    emit Print(" > Ошибка записи ответа на терманальную команду в БД\r\n", false);
    UpdateCommand("ERROR", command);
    ((CListenThread*)par)->CommandInWork = false;
    return false;
}
// 30.11.2016
// Запись данных для обработки драйвером
void CAnswer::SaveDriver(bool NeedToRead, QByteArray data)
{
    ((CListenThread*)par)->CommandInWork = true;
    emit ResetSilence();     // Сброс счетчика тишины
    QByteArray str;
    if(NeedToRead)
        str = pClientSocket->readAll();
    else
        str = data;
    pBusyDriverData->lock();
    pDriverData->append(str);
    pBusyDriverData->unlock();
    ((CListenThread*)par)->CommandInWork = false;
    return;
}
// 30.11.2016
// Получение данных из полученного пакета от ПЛК
QByteArray CAnswer::GetDataPLC(QByteArray data, bool* IsOk, quint8 &funct, CCommand* command)
{
    *IsOk = false;
    // Массив для получения сообщений
    QByteArray str;
    str.clear();
    quint32 length = data.size();  // Длина всего пакета
    quint32 ID =0;                 // Идентификатор объекта
    quint32 IDS =0;                // Идентификатор сервера
    quint32 CID =0;                // Идентификатор команды
    quint32 NUMBER=0;              // Сеансовый номер команды
    quint8 function =0;            // Функция
    quint32 datalength =0;         // Длина данных
    // Если длина полученных данных менее 24 байта - значит данные не полные или пришло что-то не то
    if (length < 24)
    {
        emit Print(" > Получены ошибочные PLC данные\r\n", false);
        emit Print(" > Длина пакета: " + QString::number(length) + " байт, минимум: 24 байт\r\n", false);
        return str;
    }
    // Если получены данные и от другого пакета, тогда берутся только первые 21
    str = data.left(21); // Получение заголовка из пакета
    data.remove(0,21);   // Удаление заголовка из пакета
    // Получение длины пакета данных
    datalength =0;
    datalength += (((quint32)((unsigned char)*(str.data() + 17))) << 24);
    datalength += (((quint32)((unsigned char)*(str.data() + 18))) << 16);
    datalength += (((quint32)((unsigned char)*(str.data() + 19))) << 8);
    datalength += ((quint32)((unsigned char)*(str.data() + 20)));
    // Если длина равна нулю
    if(datalength ==0)
    {
        emit Print(" > Получены ошибочные PLC данные\r\n", false);
        emit Print(" > Длина сообщения: 0 байт, минимум: 1 байт\r\n", false);
        str.clear();
        return str;
    }
    // Длина данных короче длины указанной в заголовке
    else if((data.size()-2) < datalength)
    {
        emit Print(" > Получены ошибочные PLC данные\r\n", false);
        emit Print(" > Длина сообщения указана: " + QString::number(datalength) +" байт\r\n", false);
        emit Print(" > Длина сообщения фактическая: " + QString::number((data.size()-2)) + " байт\r\n", false);
        str.clear();
        return str;
    }
    // Проверка CRC16 сообщение
    QByteArray packet;
    packet.clear();
    packet.append(str);                        // Заголовок
    packet.append(data.left((datalength + 2)));// Данные и CRC
    if(!TestPacketCRC16(packet))               // Проверка CRC пакета
    {
        str.clear();
        return str;
    }
    // Проверка идентификатора объекта
    ID ^=(((quint32)((unsigned char)*(str.data()))) << 24);
    ID ^=(((quint32)((unsigned char)*(str.data() + 1))) << 16);
    ID ^=(((quint32)((unsigned char)*(str.data() + 2))) << 8);
    ID ^=((quint32) ((unsigned char)*(str.data() + 3)));
    // Если не совпали идентификаторы объекта
    if(ObjectID!= ID)
    {
        emit Print(" > Получены ошибочные PLC данные\r\n", false);
        emit Print(" > ID данных: " + QString::number(ID) +
                   ", ID объекта: " + QString::number(ObjectID) + "\r\n", false);
        str.clear();
        return str;
    }
    // Проверка идентификатора сервера
    IDS +=(((quint32)((unsigned char)*(str.data() + 4))) << 24);
    IDS +=(((quint32)((unsigned char)*(str.data() + 5))) << 16);
    IDS +=(((quint32)((unsigned char)*(str.data() + 6))) << 8);
    IDS +=((quint32) ((unsigned char)*(str.data() + 7)));
    // Если не совпал идентификатор сервера
    if(IDS!=1)
    {
        emit Print(" > Получены ошибочные PLC данные\r\n", false);
        emit Print(" > IDS данных: " + QString::number(IDS) +
                   ", IDS сервера: 1\r\n", false);
        str.clear();
        return str;
    }
    // Проверка function
    function +=((quint8)(*(str.data() + 16)));
    funct = function;
    // Если функция 0х4 - это пинг
    // Если функция 0х1 - это ответ на запрос с ожиданием ответа от оборудования
    // Если функция 0х2 - это ответ на запрос без ожидания ответа (в одну сторону)
    // Если функция 0х3 - это ответ на запрос с кодом ошибки
    if(function == 0x4)
    {
        // Возвращение пустого массива и флага успешной обработки
        *IsOk = true;
        str.clear();
        emit ResetSilence();
        emit Print(" > Получен пинг от объекта\r\n", false);
        // Необходимо ответить на пинг (флаг для диспетчера отправки)
        *pSendPing = true;
        return str;
    }
    // Если команды в процессе нет и это не пинг
    else if(command ==NULL)
    {
        emit Print(" > Полученные данные не могут быть обработаны\r\n", false);
        emit Print(" > Нет команд в обработке\r\n", false);
        str.clear();
        return str;
    }
    // Функция полученная отличается от той, что в команде и не равна функции возврата кода ошибки 0х3
    else if((command->DBData.Funct!= ((unsigned char)function))&(function!=0x3))
    {
        emit Print(" > Полученные данные не могут быть обработаны\r\n", false);
        emit Print(" > Код функции полученный: "+ QString::number(function, 16) + "\r\n", false);
        emit Print(" > Код функции каманды в обработке: " +
                   QString::number((quint8)command->DBData.Funct, 16) + ", 0х3 для ошибки\r\n", false);
        str.clear();
        return str;
    }
    // Проверка CID
    CID +=(((quint32)((unsigned char)*(str.data() + 8))) << 24);
    CID +=(((quint32)((unsigned char)*(str.data() + 9))) << 16);
    CID +=(((quint32)((unsigned char)*(str.data() + 10))) << 8);
    CID +=((quint32) ((unsigned char)*(str.data() + 11)));
    // Если не совпал идентификатор команды
    if(CID!=command->DBData.CID)
    {
        emit Print(" > Полученные данные не могут быть обработаны\r\n", false);
        emit Print(" > CID команды в ответе: " + QString::number(CID) + "\r\n", false);
        emit Print(" > CID команды в обработке: " + QString::number(command->DBData.CID) + "\r\n", false);
        str.clear();
        return str;
    }
    // Проверка SESSION
    NUMBER +=(((quint32)((unsigned char)*(str.data() + 12))) << 24);
    NUMBER +=(((quint32)((unsigned char)*(str.data() + 13))) << 16);
    NUMBER +=(((quint32)((unsigned char)*(str.data() + 14))) << 8);
    NUMBER +=((quint32) ((unsigned char)*(str.data() + 15)));
    if(NUMBER!=command->Number)
    {
        emit Print(" > Полученные данные не могут быть обработаны\r\n", false);
        emit Print(" > NUMBER команды в ответе: " + QString::number(NUMBER) + "\r\n", false);
        emit Print(" > NUMBER команды в обработке: " + QString::number(command->Number) + "\r\n", false);
        str.clear();
        return str;
    }
    QByteArray pr = data.left(datalength);
    pr = pr.toHex();
    pr = pr.toUpper();
    QByteArray ps;
    for(quint32 i=0; i<datalength;i++)
    {
        ps.append(pr.at(i*2));
        ps.append(pr.at(i*2+1));
        if(i!= datalength-1)
            ps.append(',');
    }
    emit Print(" > Получены данные CID " + QString::number(CID) +
               " SESSION " + QString::number(NUMBER) +
               " функция " + QString::number(function) +
               " длина: " + QString::number(datalength) +" байт\r\n", false);
    emit Print(" > Данные: " + ps + "\r\n", false);
    // Все данные проверены, возврат данных без заголовка и CRC
    *IsOk = true;
    emit ResetSilence();
    SetProcessingCommand(data.left(datalength));
    return data.left(datalength);
}
// 30.11.2016
// Установка статуса в перечне команд
// 01.03.2017 Передача в функцию команды изменено с CCommand command на CCommand& command из-за перезаписи статуса
//            на предыдущий при следующем вызове SetDateTime (статус передавался старый)
bool CAnswer::SetStatus(QString status, CCommand& command)
{
    quint32 SessionNumber = command.Number;
    quint32 CID = command.DBData.CID;
    pBusyCommandTable->lock();
    bool IsFinded = false;
    for(int i=0; i < pCommandTable->size(); i++)
    {
        // Будут меняться только команды с таким же CID и сеансовым номером т.к. могут быть и отработанные команды
        // в зависимости от частоты обработки перечня команд родительским потоком
        if((CID == pCommandTable->at(i).DBData.CID)&(SessionNumber == pCommandTable->at(i).Number))
        {

            command.Status = status;
            pCommandTable->replace(i, command);
//            (*pCommandTable)[i].Status = status;
//            emit Print(" ТЕСТИРОВАНИЕ Answer статус " + pCommandTable->at(i).Status +
//                       " CID " + QString::number(pCommandTable->at(i).DBData.CID) +
//                       " SESSION " + QString::number(pCommandTable->at(i).Number) + "\r\n", false);
            IsFinded = true;
            break;
        }
    }
    pBusyCommandTable->unlock();
    return IsFinded;
}
// 30.11.2016
// Запись времени отправки и времени окончания
// Внимание, при записи времени начала, запись окончания не будет произведена
bool CAnswer::SetDateTime(bool Start, CCommand command)
{
    // Сеансовый номер команды
    quint32 SessionNumber = command.Number;
    quint32 CID = command.DBData.CID;
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
            // Если производится запись времени начала
            if(Start)
                command.StartTime = dt;
            // Времени окончания
            else
                command.FinishTime = dt;
            pCommandTable->replace(i, command);
            IsFinded =true;
            break;
        }
    }
    pBusyCommandTable->unlock();
    return IsFinded;
}
// 30.11.2016
// Обработчик таблицы команд по полученному статусу
bool CAnswer::UpdateCommand(QString status, CCommand command)
{
    bool IsFinded = false;
    // Если статус какой-то случайный, то не обрабатывается
    if((status!="READY")&(status!="PROCESSING")&(status!="ERROR")&(status!="TIMEOUT")&(status!="COMPLETED"))
        return IsFinded;
    // Если статус PROCESSING, тогда запись статуса и метки времени отправки
    if(status == "PROCESSING")
    {
        if(SetStatus(status, command))
            IsFinded = SetDateTime(true, command);
        return IsFinded;
    }
    // Если статус TIMEOUT, ERROR или COMPLETED то запись статуса времени окончания
    else if((status =="TIMEOUT")|(status =="ERROR")|(status =="COMPLETED"))
    {
        if(SetStatus(status, command))
            IsFinded = SetDateTime(false, command);
        return IsFinded;
    }
    // Статус READY устанавливается в родительском потоке
    IsFinded = true;
    return IsFinded;
}
// 28.11.2016
// Получение команды со статусом PROCESSING
bool CAnswer::GetProcessingCommand(CCommand* command)
{
    bool IsFinded = false;
    pBusyCommandTable->lock();
    for(int i =0; i < pCommandTable->size(); i++)
    {
        if(pCommandTable->at(i).Status == "PROCESSING")
        {
            *command = pCommandTable->at(i);
            IsFinded = true;
            break;
        }
    }
    pBusyCommandTable->unlock();
    return IsFinded;
}
// 13.04.2017 Запись полученного ответа в команду
bool CAnswer::SetProcessingCommand(QByteArray str)
{
    bool IsFinded = false;
    pBusyCommandTable->lock();
    for(int i =0; i < pCommandTable->size(); i++)
    {
        if(pCommandTable->at(i).Status == "PROCESSING")
        {
            (*(pCommandTable))[i].Answer = str;
 //           emit Print(" > Полученные данные: " + pCommandTable->at(i).Answer.toHex() + "\r\n", true);
            IsFinded = true;
            break;
        }
    }
    pBusyCommandTable->unlock();
    return IsFinded;
}
// 30.11.2016
// Проверка CRC16 ПЛК пакета
bool CAnswer::TestPacketCRC16(QByteArray packet)
{
    // Если длина пакета меньше 3 (1 байт данных и 2 байта CRC)
    if(packet.size() < 3)return false;
    unsigned char KS_H, KS_L, DS_H, DS_L;
    DS_H = ((unsigned char) *(packet.data() + (packet.length() - 2)));
    DS_L = ((unsigned char) *(packet.data() + (packet.length() - 1)));
    GetCRC16((unsigned char *)(packet.data()), &KS_H, &KS_L,(unsigned long) (packet.length()));
    if((KS_H != DS_H)|(KS_L != DS_L))
    {
        emit Print(" > Получены ошибочные PLC данные\r\n", false);
        emit Print(" > CRC данных: " +
                   QString::number(((quint8)DS_H), 16) +
                   QString::number(((quint8)DS_L), 16) +
                   "\r\n", false);
        emit Print(" > CRC расчетное: " +
                   QString::number(((quint8)KS_H), 16) +
                   QString::number(((quint8)KS_L), 16) +
                   "\r\n", false);
        return false;
    }
    return true;
}

// Удаление ненужного сокета
void CAnswer::DelSocket()
{
    ((CListenThread*)par)->IsConnected = false; // Запрет отправки команд
    if(pClientSocket!=NULL)
    {
        // Производится отключение сигналов
        disconnect(pClientSocket, SIGNAL(disconnected()), this, SLOT(DelSocket()));
        disconnect(pClientSocket, SIGNAL(readyRead()), this, SLOT(ReadAnswer()));
        // Удаление отключенного сокета тогда, когда это будет возможным
        pClientSocket->waitForBytesWritten(); // Если еще идет запись, будет ожидание пока все данные не уйдут
        pClientSocket->deleteLater();
        DeleteOldSocket();
        QString DataPrint = " > Соединение с объектом разорвано\r\n";
        emit Print(DataPrint, false);
    }
    pClientSocket =NULL; // Отвязать указатель обязательно!
    emit SetObjectStatus("OFFLINE", 1);
}

// Удаление данного объекта
void CAnswer::DelThis()
{
    ((CListenThread*)par)->IsConnected = false; // Запрет отправки команд
    IsExiting = true;
    if(pClientSocket!=NULL)
    {
        connect(pClientSocket, SIGNAL(destroyed(QObject*)), this, SLOT(OnQuit(QObject*)));
        // Производится отключение сигналов
        disconnect(pClientSocket, SIGNAL(disconnected()), this, SLOT(DelSocket()));
        disconnect(pClientSocket, SIGNAL(readyRead()), this, SLOT(ReadAnswer()));
        // Удаление отключенного сокета тогда, когда это будет возможным
        pClientSocket->waitForBytesWritten(); // Если еще идет запись, будет ожидание пока все данные не уйдут
        pClientSocket->deleteLater();
        DeleteOldSocket();
    }
    else
    {
        if(SysDB.isOpen())
        {
            {SysDB.close();}
        }
        QSqlDatabase::removeDatabase("AnswerSys_" + QString::number(ObjectID));
        if(ObjectDB.isOpen())
        {
            {ObjectDB.close();}
        }
        QSqlDatabase::removeDatabase("AnswerData_" + QString::number(ObjectID));
        emit Stop();
        delete this;
    }
}
// Получение нового соединения потоком
void CAnswer::GetSocket(QTcpSocket* sock, QByteArray data, quint32 ID)
{
    // 1. Проверка какому объекту предназначается сокет
    if(ObjectID!=ID)
        return;
    // 2. Если происходит выход из потока, и приходит новый сокет, то следует завершить новый сокет и выйти
    // иначе поток повиснет
    if(IsExiting)
    {
        sock->deleteLater();
        return;
    }
    // 3. Установка сокета
    // 3.1 Если был установлен сокет, то следует сначала его удалить
    if(pClientSocket!=NULL)
    {
        // Производится отключение сигналов
        disconnect(pClientSocket, SIGNAL(disconnected()), this, SLOT(DelSocket()));
        disconnect(pClientSocket, SIGNAL(readyRead()), this, SLOT(ReadAnswer()));
        // Удаление отключенного сокета тогда, когда это будет возможным
        pClientSocket->waitForBytesWritten(); // Если еще идет запись, будет ожидание пока все данные не уйдут
        pClientSocket->deleteLater();
        pClientSocket = NULL;
        DeleteOldSocket();
    }
    emit SetObjectStatus("OFFLINE", 1);
    ((CListenThread*)par)->IsConnected = false;
    // 3.2 Установка нового сокета в данном потоке
    pClientSocket = new QTcpSocket();
    //pClientSocket = new QTcpSocket(this);
    pClientSocket->setParent(0);
    qintptr descriptor = sock->socketDescriptor();
    pClientSocket->setReadBufferSize(20480);
    OldSocket = sock;
    // 3.3 Удаление сокета главного потока
//    sock->waitForBytesWritten();
//    sock->deleteLater();

    // 3.5 Замена сокета на новый
    bool f = pClientSocket->setSocketDescriptor(descriptor);
    if(f == false)
    {
        emit Print(" > Соединение установить не удалось, ошибка установки дескриптора\r\n", false);
        pClientSocket->waitForBytesWritten();
        pClientSocket->deleteLater();
        pClientSocket = NULL;
        DeleteOldSocket();
        return;
    }
    connect(pClientSocket, SIGNAL(disconnected()), this, SLOT(DelSocket()), Qt::QueuedConnection);
    connect(pClientSocket, SIGNAL(readyRead()), this, SLOT(ReadAnswer()), Qt::QueuedConnection);
    // 4. Запись пакета в обработку
    emit SetObjectStatus("ONLINE", 1);
    ((CListenThread*)par)->IsConnected = true; // Разрешение отправки команд
    ReadAnswer(false, data);
    QString DataPrint = " > Получено новое соединение с объектом\r\n";
    emit Print(DataPrint, false);
}
// 23.02.2017 Удаление старого сокета-дубликата из главного потока
void CAnswer::DeleteOldSocket()
{
    if(OldSocket!=NULL)
    {
        OldSocket->waitForBytesWritten();
        OldSocket->deleteLater();
        OldSocket = NULL;
    }
}

// Отрабатывается корректно (адрес указателя равен указателю на сокет)
void CAnswer::OnQuit(QObject *p)
{
    if(pClientSocket == ((QTcpSocket*)p))
    {
        if(SysDB.isOpen())
        {
            {SysDB.close();}
        }
        QSqlDatabase::removeDatabase("AnswerSys_" + QString::number(ObjectID));
        if(ObjectDB.isOpen())
        {
            {ObjectDB.close();}
        }
        QSqlDatabase::removeDatabase("AnswerData_" + QString::number(ObjectID));
        emit Stop();
        delete this;
    }
}
// Отправка команды объекту
// 02.09.2016 Если сокет отсутствует, тогда не производится отправка (защита программы)
// 02.09.2016 Если соединение долго простаивает, то посылается сигнал на удаление сокета
// 29.11.2016 Отправка перенесена из CListenThread в CAnswer где расположен сокет объекта
void CAnswer::sendToClient(QByteArray data, int timeout)
{
    if(pClientSocket==NULL)
        return;
// 23.02    emit SendToThreadSocket(data);
    pClientSocket->waitForBytesWritten();
    pClientSocket->write(data);
    pClientSocket->waitForBytesWritten();
    this->thread()->sleep(timeout);       // Таймаут после отправки. Поток будет простаивать.
}
// 02.12.2016
// Обработка полученных данных по настройкам Params
bool CAnswer::CVarDBEngine(QByteArray str, CCommand command)
{
    QByteArray packet;
    CVarDB Param;
    QString Var = "";          // Значение датчика
    QString ConvertedVar = ""; // Значение параметра
    // Цикл обработки параметров
    for(int i=0; i < command.DBData.Params.size(); i++)
    {
        packet = str;
        Param = command.DBData.Params.at(i);
        // 1. Проверка длины, смещения, конца строки и получение данных
        // ПРИМЕЧАНИЕ: функция изменяет входной массив и сама производит вывод и запись ошибок
        if(!GetVarData(Param, packet, command))
            return false;
        // 2. Если не строковые данные, то следует упорядочить данные в массиве
        if(Param.VarType!="QString")
            if(!InsertData(Param, packet, command))
                return false;
        // 3. Преобразование к указанному типу
        // 3.1 Проверка возможности преобразования
        // ПРИМЕЧАНИЕ: функция сама записывает данные ошибок и производит вывод
        if(!GetSensorVar(Param, packet, Var, command))
            return false;
        // 3.2 01.05.2017 Преобразование из полученного типа к типу хранения
        ConvertToSaveType(Param, Var);
        // 3.3 Проверка максимального и минимального значения и преобразование в значение параметра
        // ПРИМЕЧАНИЕ: функция сама записывает данные ошибок и производит вывод
        if(!TestSensorRange(Param, str, Var, ConvertedVar, command))
            return false;
//        emit Print( " ТЕСТИРОВАНИЕ датчик " + Var + " параметр " + ConvertedVar + "\r\n", false);
        // 5 Если переменная типа WRITE, то следует записать значение таблицу прибора
        // ПРИМЕЧАНИЕ: функция сама записывает данные ошибок и производит вывод
        // 05.02.2017 При неудачной записи параметра логика обработки не меняется, а только выводятся сообщения
        //            и записывается статус
        if(Param.VarPermit == "WRITE")
        {
            if(SaveParameter(Param, ConvertedVar, command))
            {
                emit Print(" > Запись " + command.DBData.EqName + "/" + command.DBData.EqNumber +
                           " " + Param.VarName + " "  + ConvertedVar + "\r\n", false);
                emit Print(" > Тип записи " + command.DBData.RequestType + "\r\n", false);
            }
        }
        // 6 Проверка не является ли значение аварийным, если же это CRC то проверка была уже проведена
        if(IsAlarm(Param, ConvertedVar, command))
        {
            // Если установлен флаг остановки опроса, выход из обработки
            if(Param.StopFlag == 1) 
                break;
        }
    }
//    emit Print(" ТЕСТИРОВАНИЕ команда завершена\r\n", false);
    UpdateCommand("COMPLETED", command);
    return true;
}
// 15.12.2016 Проверка длины пакета относительно сдвига и длины параметра,
// а также строкового типа с неопределенной длиной
// 05.01.2016 Удалена функция TestDataLocation т.к. проверка и выборка производится одновременно
bool CAnswer::GetVarData(CVarDB param, QByteArray& data, CCommand command)
{
    // 1 Проверка смещения и длины
    // 1.1 Если смещение + длина больше чем размер массива
    if(param.VarType !="bit")
    {
        if((param.VarOffset + param.VarData) > (quint32)data.size())
        {
            emit Print(" > Полученные данные не могут быть обработаны\r\n", false);
            emit Print(" > Размер полученных данных: " + QString::number(data.size()) + " байт\r\n", false);
            emit Print(" > Размер необходимый для обработки: " +
                       QString::number((param.VarOffset + param.VarData)) + " байт\r\n", false);
// 24.04.2017 Убрана запись тревоги при ошибке данных
/*            if(param.AlarmSet)
            {
                QString AText = "Ошибка размера данных";
                IsAlarm(param, "ERROR", command, false, AText);
            }*/
            UpdateCommand("ERROR", command);
            ((CListenThread*)par)->CommandInWork = false;
            return false;
        }
    }
    // Если бит, обрабатывается только 1 байт
    else
    {
        if((param.VarOffset + 1) > (quint32)data.size())
        {
            emit Print(" > Полученные данные не могут быть обработаны\r\n", false);
            emit Print(" > Размер полученных данных: " + QString::number(data.size()) + " байт\r\n", false);
            emit Print(" > Размер необходимый для обработки: " +
                       QString::number((param.VarOffset + 1)) + " байт\r\n", false);
// 24.04.2017 Убрана запись тревоги при ошибке данных
/*            if(param.AlarmSet)
            {
                QString AText = "Ошибка размера данных";
                IsAlarm(param, "ERROR", command, false, AText);
            }*/
            UpdateCommand("ERROR", command);
            ((CListenThread*)par)->CommandInWork = false;
            return false;
        }
    }
    // 1.2 Если длина нулевая и это не строка
    if((param.VarData == 0)&(param.VarType !="QString"))
    {
        emit Print(" > Полученные данные не могут быть обработаны\r\n", false);
        emit Print(" > Размер параметра для обработки: 0 байт\r\n", false);
// 24.04.2017 Убрана запись тревоги при ошибке данных
/*        if(param.AlarmSet)
        {
            QString AText = "Ошибка размера данных";
            IsAlarm(param, "ERROR", command, false, AText);
        }*/
        UpdateCommand("ERROR", command);
        ((CListenThread*)par)->CommandInWork = false;
        return false;
    }
    // 2 Выборка данных из массива
    // 2.1 Переменная строковая и длина неопределена
    if((param.VarType == "QString")&(param.VarData == 0))
    {
        int i = data.indexOf('\0', param.VarOffset);
        if((i < 0)|(i >= data.size()))
        {
            emit Print(" > Полученные данные не могут быть обработаны\r\n", false);
            emit Print(" > В полученных данных не найден конец строки\r\n", false);
// 24.04.2017 Убрана запись тревоги при ошибке данных
/*            if(param.AlarmSet)
            {
                QString AText = "Ошибка поиска конца строки";
                IsAlarm(param, "ERROR", command, false, AText);
            }*/
            UpdateCommand("ERROR", command);
            ((CListenThread*)par)->CommandInWork = false;
            return false;
        }
        else
        {
            data = data.mid(param.VarOffset, (i - param.VarOffset +1));
            return true;
        }
    }
    // 2.2 Если нужно прочитать бит
    if(param.VarType =="bit")
    {
        if((param.VarData < 1)|(param.VarData > 8))
        {
            emit Print(" > Полученные данные не могут быть обработаны\r\n", false);
            emit Print(" > Указан неверный индекс бита: " + QString::number(param.VarData) + "\r\n", false);
// 24.04.2017 Убрана запись тревоги при ошибке данных
/*            if(param.AlarmSet)
            {
                QString AText = "Ошибка индекса бита";
                IsAlarm(param, "ERROR", command, false, AText);
            }*/
            UpdateCommand("ERROR", command);
            ((CListenThread*)par)->CommandInWork = false;
            return false;
        }
        else
        {
            data = data.mid(param.VarOffset, 1);
            return true;
        }
    }
    // 2.3 При остальных вариантах
    data = data.mid(param.VarOffset, param.VarData);
    return true;
}
// 08.12.2016
// 15.12.2016 Запись ошибок теперь производится внутри данной функции
// Упорядочевание данных в массиве согласно настроек переменной
// ПРИМЕЧАНИЕ индекс данных хранимый в настройках начинается с 1
bool CAnswer::InsertData(CVarDB param, QByteArray& data, CCommand command)
{
    QByteArray str;
    str.clear();
    QStringList di = param.VarInsert.split(",");
//    emit Print(" ТЕСТИРОВАНИЕ длина данных: " + QString::number(data.size()) + "\r\n", false);
//    emit Print(" ТЕСТИРОВАНИЕ порядок данных: " + param.VarInsert + "\r\n", false);
//    emit Print(" ТЕСТИРОВАНИЕ разбивка: " + QString::number(di.size()) + "\r\n", false);
    quint32 currentIndex;
    bool IsOk;
    for(int i=0; i < di.size(); i++)
    {
        currentIndex = di.at(i).toULong(&IsOk);
//        emit Print(" ТЕСТИРОВАНИЕ текущий индекс: " + di.at(i) + "\r\n", false);
        // Если не удалось преобразовать индекс, либо размер данных меньше индекса
        if((IsOk == false)|(data.size() < currentIndex))
        {
            emit Print(" > Полученные данные не могут быть обработаны\r\n", false);
            emit Print(" > Упорядочить полученные данные невозможно\r\n", false);
// 24.04.2017 Убрана запись тревоги при ошибке данных
/*            if(param.AlarmSet)
            {
                QString AText = "Упорядочить данные параметра невозможно";
                IsAlarm(param, "ERROR", command, false, AText);
            }*/
            UpdateCommand("ERROR", command);
            ((CListenThread*)par)->CommandInWork = false;
            return false;
        }
        // Вставка данных
        str.append(data.at(currentIndex-1));
//        emit Print(" ТЕСТИРОВАНИЕ текущий байт: " + QByteArray(1,data.at(currentIndex-1)).toHex() + "\r\n", false);
    }
    data = str;
    emit Print(" > Данные вставки: " + data.toHex().toUpper() + "\r\n", false);
//    emit Print(" ТЕСТИРОВАНИЕ полученные данные:" + str.toHex() + "\r\n", false);
    return true;
}
// 05.01.2017 Преобразование двоичных данных массива в строковое значение
bool CAnswer::GetSensorVar(CVarDB param, QByteArray &data, QString& var, CCommand command)
{
    QString VarType = param.VarType;
    int size;
    // Выборка длины
    if((VarType == "quint8")|(VarType == "qint8")|(VarType =="MOD2")|(VarType =="bit"))
        size =1;
    else if((VarType == "quint16")|(VarType == "qint16")|(VarType == "CRC16"))
        size =2;
    else if((VarType == "quint32")|(VarType == "qint32")|(VarType == "float")|(VarType == "CRC32"))
        size =4;
    else if((VarType == "quint64")|(VarType == "qint64")|(VarType == "double"))
        size =8;
    else if(VarType == "QString")
        size = data.size();
    // Если тип неизвестен
    else
    {
        emit Print(" > Полученные данные не могут быть обработаны\r\n", false);
        emit Print(" > Тип данных не распознан\r\n", false);
// 24.04.2017 Убрана запись тревоги при ошибке данных
/*        if(param.AlarmSet)
        {
            QString AText = "Тип данных не распознан";
            IsAlarm(param, "ERROR", command, false, AText);
        }*/
        UpdateCommand("ERROR", command);
        ((CListenThread*)par)->CommandInWork = false;
        return false;
    }
    // Если длины не совпадают
    if(size != data.size())
    {
        emit Print(" > Полученные данные не могут быть обработаны\r\n", false);
        emit Print(" > Размер типа данных и размер данных не совпадают\r\n", false);
// 24.04.2017 Убрана запись тревоги при ошибке данных
/*        if(param.AlarmSet)
        {
            QString AText = "Ошибка длины полученного параметра";
            IsAlarm(param, "ERROR", command, false, AText);
        }*/
        UpdateCommand("ERROR", command);
        ((CListenThread*)par)->CommandInWork = false;
        return false;
    }
    // Загрузка данных в массив
    // 06.02.2017
    // Примечание по правилам протокола данные передаются от старшего байта к младшему [H]..[L]
    // в памяти же данные хранятся как [L]..[H], поэтому работа с упорядочиванием байтов производится
    // с переменной а не с памятью (для того чтобы не зависеть от платформы)
    char array[size];
    for(int i =0; i < size; i++)
        array[i] = *(data.data() + i);
    // Преобразование к нужному типу
    if(VarType == "bit")
    {
        quint8 a =0;
        a ^= (quint8)array[0];
        if(param.VarData == 1){a <<= 7; a >>=7;} // Очистка всех битов кроме нужного
        else if(param.VarData == 2){a <<= 6; a >>= 7;}
        else if(param.VarData == 3){a <<= 5; a >>= 7;}
        else if(param.VarData == 4){a <<= 4; a >>= 7;}
        else if(param.VarData == 5){a <<= 3; a >>= 7;}
        else if(param.VarData == 6){a <<= 2; a >>= 7;}
        else if(param.VarData == 7){a <<= 1; a >>= 7;}
        else if(param.VarData == 8){a >>= 7;}
        var = QString::number(a);
        return true;
    }
    else if(VarType == "quint8")
    {
        quint8 a =0;
        a ^= (quint8)array[0];
        var = QString::number(a);
        return true;
    }
    else if(VarType == "qint8")
    {
        qint8 a =0;
        quint8 b =0;
        b ^= (qint8)array[0];
        a ^= b;
        var = QString::number(a);
        return true;
    }
    else if(VarType == "quint16")
    {
        quint16 a =0;
        a ^= (((quint16)((unsigned char)array[0])) << 8);
        a ^= (quint16)((unsigned char)array[1]);
        var = QString::number(a);
        return true;
    }
    else if(VarType == "qint16")
    {
        qint16 a =0;
        quint16 b =0;
        b ^= (((quint16)((unsigned char)array[0])) << 8);
        b ^= (quint16)((unsigned char)array[1]);
        a ^= b;
        var = QString::number(a);
        return true;
    }
    else if(VarType == "quint32")
    {
        quint32 a =0;
        a ^= (((quint32)((unsigned char)array[0])) << 24);
        a ^= (((quint32)((unsigned char)array[1])) << 16);
        a ^= (((quint32)((unsigned char)array[2])) << 8);
        a ^= (quint32)((unsigned char)array[3]);
        var = QString::number(a);
        return true;
    }
    else if(VarType == "qint32")
    {
        qint32 a =0;
        quint32 b =0;
        b ^= (((quint32)((unsigned char)array[0])) << 24);
        b ^= (((quint32)((unsigned char)array[1])) << 16);
        b ^= (((quint32)((unsigned char)array[2])) << 8);
        b ^= (quint32)((unsigned char)array[3]);
        a ^= b;
        var = QString::number(a);
        return true;
    }
    else if(VarType == "quint64")
    {
        quint64 a =0;
        a ^= (((quint64)((unsigned char)array[0])) << 56);
        a ^= (((quint64)((unsigned char)array[1])) << 48);
        a ^= (((quint64)((unsigned char)array[2])) << 40);
        a ^= (((quint64)((unsigned char)array[3])) << 32);
        a ^= (((quint64)((unsigned char)array[4])) << 24);
        a ^= (((quint64)((unsigned char)array[5])) << 16);
        a ^= (((quint64)((unsigned char)array[6])) << 8);
        a ^= (quint64)((unsigned char)array[7]);
        var = QString::number(a);
        return true;
    }
    else if(VarType == "qint64")
    {
        qint64 a =0;
        quint64 b =0;
        b ^= (((quint64)((unsigned char)array[0])) << 56);
        b ^= (((quint64)((unsigned char)array[1])) << 48);
        b ^= (((quint64)((unsigned char)array[2])) << 40);
        b ^= (((quint64)((unsigned char)array[3])) << 32);
        b ^= (((quint64)((unsigned char)array[4])) << 24);
        b ^= (((quint64)((unsigned char)array[5])) << 16);
        b ^= (((quint64)((unsigned char)array[6])) << 8);
        b ^= (quint64)((unsigned char)array[7]);
        a ^= b;
        var = QString::number(a);
        return true;
    }
    else if(VarType == "float")
    {
        float a =0;
        quint32 b =0;
        b ^= (((quint32)((unsigned char)array[0])) << 24);
        b ^= (((quint32)((unsigned char)array[1])) << 16);
        b ^= (((quint32)((unsigned char)array[2])) << 8);
        b ^= (quint32)((unsigned char)array[3]);
        // Порядок байт в памяти будет верный т.к. операции проводятся с переменной,
        // а в памяти порядок байт формируется автоматически
        a = *((float*)&b);
        var = QString::number(a, 'f', 6);
        return true;
    }
    else if(VarType == "double")
    {
        double a =0;
        quint64 b =0;
        b ^= (((quint64)((unsigned char)array[0])) << 56);
        b ^= (((quint64)((unsigned char)array[1])) << 48);
        b ^= (((quint64)((unsigned char)array[2])) << 40);
        b ^= (((quint64)((unsigned char)array[3])) << 32);
        b ^= (((quint64)((unsigned char)array[4])) << 24);
        b ^= (((quint64)((unsigned char)array[5])) << 16);
        b ^= (((quint64)((unsigned char)array[6])) << 8);
        b ^= (quint64)((unsigned char)array[7]);
        a = *((double*)&b);
        var = QString::number(a, 'f', 6);
        return true;
    }
    // Запись контрольной суммы производится в шестнадцатиричном формате
    else if(VarType =="MOD2")
    {
        quint8 a =0;
        a ^= (quint8)array[0];
        var = QString::number(a, 16);
        return true;
    }
    else if(VarType =="CRC16")
    {
        quint16 a =0;
        a ^= (((quint16)((unsigned char)array[0])) << 8);
        a ^= (quint16)((unsigned char)array[1]);
        var = QString::number(a, 16);
        return true;
    }
    else if(VarType == "CRC32")
    {
        quint32 a =0;
        a ^= (((quint32)((unsigned char)array[0])) << 24);
        a ^= (((quint32)((unsigned char)array[1])) << 16);
        a ^= (((quint32)((unsigned char)array[2])) << 8);
        a ^= (quint32)((unsigned char)array[3]);
        var = QString::number(a, 16);
        return true;
    }    
    var = QString::fromUtf8(data);
    return true;
}
// 01.05.2017 Преобразование полученного типа в указанный для записи в БД
void CAnswer::ConvertToSaveType(CVarDB param, QString& var)
{
    QString VarType = param.VarType;
    QString VarTypeSave = param.VarTypeSave;
    // Преобразование целочисленных беззнаковых в указанный тип
    if((VarType =="quint8")|(VarType =="quint16")|(VarType =="quint32")|(VarType =="quint64")|(VarType =="bit")|
       (VarType =="MOD2")|(VarType == "CRC16")|(VarType == "CRC32"))
    {
        quint64 v =0;
        if((VarType == "MOD2")|(VarType =="CRC16")|(VarType =="CRC32"))
            v = var.toULongLong(0, 16);
        else
            v = var.toULongLong();
        // Преобразование в аналогичный тип
        if(VarTypeSave =="quint8")
        {
            quint8 uv =0;
            uv ^= v;
            var = QString::number(uv);
            // Полученное значение меньше того что было
            if( uv < v )
            {
                emit Print(" > Полученное значение: " + QString::number(v) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(uv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave =="quint16")
        {
            quint16 uv =0;
            uv ^= v;
            var = QString::number(uv);
            // Полученное значение меньше того что было
            if( uv < v )
            {
                emit Print(" > Полученное значение: " + QString::number(v) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(uv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave =="quint32")
        {
            quint32 uv =0;
            uv ^= v;
            var = QString::number(uv);
            // Полученное значение меньше того что было
            if( uv < v )
            {
                emit Print(" > Полученное значение: " + QString::number(v) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(uv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if((VarTypeSave =="quint64")|(VarTypeSave =="QString"))
        {
            var = QString::number(v);
            return;
        }
        if(VarTypeSave =="bit")
        {
            quint8 uv =0;
            uv ^= v;
            uv <<= 7; uv >>=7;
            var = QString::number(uv);
            // Полученное значение меньше того что было
            if((uv < v )|(uv > v))
            {
                emit Print(" > Полученное значение: " + QString::number(v) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(uv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        // Преобразование в знаковый тип
        if(VarTypeSave =="qint8")
        {
            qint8 iv =0;
            iv ^= v;
            var = QString::number(iv);
            // Полученное значение меньше того что было
            if((iv < v )|(iv > v))
            {
                emit Print(" > Полученное значение: " + QString::number(v) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(iv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave =="qint16")
        {
            qint16 iv =0;
            iv ^= v;
            var = QString::number(iv);
            // Полученное значение меньше того что было
            if((iv < v )|(iv > v))
            {
                emit Print(" > Полученное значение: " + QString::number(v) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(iv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave =="qint32")
        {
            qint32 iv =0;
            iv ^= v;
            var = QString::number(iv);
            // Полученное значение меньше того что было
            if((iv < v )|(iv > v))
            {
                emit Print(" > Полученное значение: " + QString::number(v) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(iv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave =="qint64")
        {
            qint64 iv =0;
            iv ^= v;
            var = QString::number(iv);
            // Полученное значение меньше того что было
            if((iv < v )|(iv > v))
            {
                emit Print(" > Полученное значение: " + QString::number(v) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(iv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        // Преобразование в вещественный тип
        if(VarTypeSave =="float")
        {
            float fv =0;
            fv += v;
            var = QString::number(fv, 'f', 6);
            // Полученное значение меньше того что было
            if(( fv < v )|( fv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(fv, 'f', 6) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave =="double")
        {
            double dv =0;
            dv += v;
            var = QString::number(dv, 'f', 6);
            // Полученное значение меньше того что было
            if(( dv < v )|( dv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(dv, 'f', 6) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        // Преобразование в типы контрольной суммы
        if(VarTypeSave == "MOD2")
        {
            quint8 mv =0;
            mv ^= v;
            var = QString::number(mv, 16);
            // Полученное значение меньше того что было
            if(( mv < v )|( mv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(mv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave == "CRC16")
        {
            quint16 cv =0;
            cv ^= v;
            var = QString::number(cv, 16);
            // Полученное значение меньше того что было
            if(( cv < v )|( cv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(cv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave == "CRC32")
        {
            quint32 cv =0;
            cv ^= v;
            var = QString::number(cv, 16);
            // Полученное значение меньше того что было
            if(( cv < v )|( cv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(cv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
    }
    // Преобразование целочисленных со знаком
    else if((VarType =="qint8")|(VarType =="qint16")|(VarType =="qint32")|(VarType =="qint64"))
    {
        qint64 v =0;
        v = var.toLongLong();
        // Преобразование в беззнаковый тип
        if(VarTypeSave =="quint8")
        {
            quint8 uv =0;
            uv ^= v;
            var = QString::number(uv);
            // Полученное значение меньше того что было
            if(( uv < v )|( uv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(uv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave =="quint16")
        {
            quint16 uv =0;
            uv ^= v;
            var = QString::number(uv);
            // Полученное значение меньше того что было
            if(( uv < v )|( uv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(uv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave =="quint32")
        {
            quint32 uv =0;
            uv ^= v;
            var = QString::number(uv);
            // Полученное значение меньше того что было
            if(( uv < v )|( uv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(uv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave =="quint64")
        {
            quint64 uv =0;
            uv ^= v;
            var = QString::number(uv);
            // Полученное значение меньше того что было
            if(( uv < v )|( uv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(uv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave =="bit")
        {
            quint8 uv =0;
            uv ^= v;
            uv <<= 7; uv >>=7;
            var = QString::number(uv);
            // Полученное значение меньше того что было
            if((uv < v )|(uv > v))
            {
                emit Print(" > Полученное значение: " + QString::number(v) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(uv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        // Преобразование в аналогичный тип
        if(VarTypeSave =="qint8")
        {
            qint8 iv =0;
            iv ^= v;
            var = QString::number(iv);
            // Полученное значение меньше того что было
            if((iv < v )|(iv > v))
            {
                emit Print(" > Полученное значение: " + QString::number(v) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(iv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave =="qint16")
        {
            qint16 iv =0;
            iv ^= v;
            var = QString::number(iv);
            // Полученное значение меньше того что было
            if((iv < v )|(iv > v))
            {
                emit Print(" > Полученное значение: " + QString::number(v) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(iv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave =="qint32")
        {
            qint32 iv =0;
            iv ^= v;
            var = QString::number(iv);
            // Полученное значение меньше того что было
            if((iv < v )|(iv > v))
            {
                emit Print(" > Полученное значение: " + QString::number(v) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(iv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if((VarTypeSave =="qint64")|(VarTypeSave == "QString"))
        {
            var = QString::number(v);
            return;
        }
        // Преобразование в вещественный тип
        if(VarTypeSave =="float")
        {
            float fv =0;
            fv += v;
            var = QString::number(fv, 'f', 6);
            // Полученное значение меньше того что было
            if(( fv < v )|( fv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(fv, 'f', 6) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave =="double")
        {
            double dv =0;
            dv += v;
            var = QString::number(dv, 'f', 6);
            // Полученное значение меньше того что было
            if(( dv < v )|( dv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(dv, 'f', 6) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        // Преобразование в типы контрольной суммы
        if(VarTypeSave == "MOD2")
        {
            quint8 mv =0;
            mv ^= v;
            var = QString::number(mv, 16);
            // Полученное значение меньше того что было
            if(( mv < v )|( mv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(mv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave == "CRC16")
        {
            quint16 cv =0;
            cv ^= v;
            var = QString::number(cv, 16);
            // Полученное значение меньше того что было
            if(( cv < v )|( cv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(cv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave == "CRC32")
        {
            quint32 cv =0;
            cv ^= v;
            var = QString::number(cv, 16);
            // Полученное значение меньше того что было
            if(( cv < v )|( cv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(cv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
    }
    else if(VarType =="float")
    {
        float v = var.toFloat();
        // Преобразование в беззнаковый целый тип
        if(VarTypeSave == "quint8")
        {
            quint8 uv =0;
            uv += v;
            var = QString::number(uv);
            if(( uv < v )|( uv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v, 'f', 6) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(uv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave == "quint16")
        {
            quint16 uv =0;
            uv += v;
            var = QString::number(uv);
            if(( uv < v )|( uv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v, 'f', 6) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(uv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave == "quint32")
        {
            quint32 uv =0;
            uv += v;
            var = QString::number(uv);
            if(( uv < v )|( uv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v, 'f', 6) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(uv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave == "quint64")
        {
            quint64 uv =0;
            uv += v;
            var = QString::number(uv);
            if(( uv < v )|( uv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v, 'f', 6) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(uv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave == "bit")
        {
            quint8 uv =0;
            uv += v;
            uv <<= 7; uv >>= 7;
            var = QString::number(uv);
            if(( uv < v )|( uv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v, 'f', 6) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(uv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        // Преобразование в целочисленный со знаком
        if(VarTypeSave == "qint8")
        {
            qint8 iv =0;
            iv += v;
            var = QString::number(iv);
            if(( iv < v )|( iv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v, 'f', 6) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(iv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave == "qint16")
        {
            qint16 iv =0;
            iv += v;
            var = QString::number(iv);
            if(( iv < v )|( iv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v, 'f', 6) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(iv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave == "qint32")
        {
            qint32 iv =0;
            iv += v;
            var = QString::number(iv);
            if(( iv < v )|( iv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v, 'f', 6) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(iv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave == "qint64")
        {
            qint64 iv =0;
            iv += v;
            var = QString::number(iv);
            if(( iv < v )|( iv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v, 'f', 6) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(iv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        // Аналогичныц тип или строка
        if((VarTypeSave == "float")|(VarTypeSave == "QString"))
        {
            var = QString::number(v, 'f', 6);
            return;
        }
        // Вещественный тип повышенной точности
        if(VarTypeSave == "double")
        {
            double dv =0;
            dv += v;
            var = QString::number(dv);
            if(( dv < v )|( dv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v, 'f', 6) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(dv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        // Преобразование в типы контрольной суммы
        if(VarTypeSave == "MOD2")
        {
            quint8 mv =0;
            mv += v;
            var = QString::number(mv, 16);
            // Полученное значение меньше того что было
            if(( mv < v )|( mv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v, 'f', 6) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(mv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave == "CRC16")
        {
            quint16 cv =0;
            cv += v;
            var = QString::number(cv, 16);
            // Полученное значение меньше того что было
            if(( cv < v )|( cv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v, 'f', 6) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(cv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave == "CRC32")
        {
            quint32 cv =0;
            cv += v;
            var = QString::number(cv, 16);
            // Полученное значение меньше того что было
            if(( cv < v )|( cv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v, 'f', 6) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(cv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
    }
    else if(VarType =="double")
    {
        double v = var.toDouble();
        // Преобразование в беззнаковый целый тип
        if(VarTypeSave == "quint8")
        {
            quint8 uv =0;
            uv += v;
            var = QString::number(uv);
            if(( uv < v )|( uv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v, 'f', 6) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(uv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave == "quint16")
        {
            quint16 uv =0;
            uv += v;
            var = QString::number(uv);
            if(( uv < v )|( uv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v, 'f', 6) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(uv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave == "quint32")
        {
            quint32 uv =0;
            uv += v;
            var = QString::number(uv);
            if(( uv < v )|( uv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v, 'f', 6) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(uv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave == "quint64")
        {
            quint64 uv =0;
            uv += v;
            var = QString::number(uv);
            if(( uv < v )|( uv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v, 'f', 6) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(uv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave == "bit")
        {
            quint8 uv =0;
            uv += v;
            uv <<= 7; uv >>= 7;
            var = QString::number(uv);
            if(( uv < v )|( uv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v, 'f', 6) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(uv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        // Преобразование в целочисленный со знаком
        if(VarTypeSave == "qint8")
        {
            qint8 iv =0;
            iv += v;
            var = QString::number(iv);
            if(( iv < v )|( iv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v, 'f', 6) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(iv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave == "qint16")
        {
            qint16 iv =0;
            iv += v;
            var = QString::number(iv);
            if(( iv < v )|( iv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v, 'f', 6) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(iv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave == "qint32")
        {
            qint32 iv =0;
            iv += v;
            var = QString::number(iv);
            if(( iv < v )|( iv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v, 'f', 6) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(iv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave == "qint64")
        {
            qint64 iv =0;
            iv += v;
            var = QString::number(iv);
            if(( iv < v )|( iv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v, 'f', 6) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(iv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        // Аналогичныц тип или строка
        if((VarTypeSave == "double")|(VarTypeSave == "QString"))
        {
            var = QString::number(v, 'f', 6);
            return;
        }
        // Вещественный тип 
        if(VarTypeSave == "float")
        {
            float fv =0;
            fv += v;
            var = QString::number(fv, 'f', 6);
            if(( fv < v )|( fv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v, 'f', 6) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(fv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        // Преобразование в типы контрольной суммы
        if(VarTypeSave == "MOD2")
        {
            quint8 mv =0;
            mv += v;
            var = QString::number(mv, 16);
            // Полученное значение меньше того что было
            if(( mv < v )|( mv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v, 'f', 6) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(mv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave == "CRC16")
        {
            quint16 cv =0;
            cv += v;
            var = QString::number(cv, 16);
            // Полученное значение меньше того что было
            if(( cv < v )|( cv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v, 'f', 6) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(cv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
        if(VarTypeSave == "CRC32")
        {
            quint32 cv =0;
            cv += v;
            var = QString::number(cv, 16);
            // Полученное значение меньше того что было
            if(( cv < v )|( cv > v ))
            {
                emit Print(" > Полученное значение: " + QString::number(v, 'f', 6) + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(cv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            return;
        }
    }
    // Преобразование строкового типа
    // При неудачном преобразовании будет возвращен ноль
    else if(VarType =="QString")
    {
        QString v = var;
        bool IsOk = true;
        // Целочисленные беззнаковые типы
        if(VarTypeSave =="quint8")
        {
            quint8 uv =0;
            uv = v.toULongLong(&IsOk);
            if(!IsOk)
            {
                uv =0;
                emit Print(" > Полученное значение: " + v + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(uv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            var = QString::number(uv);
            return;
        }
        if(VarTypeSave =="quint16")
        {
            quint16 uv =0;
            uv = v.toULongLong(&IsOk);
            if(!IsOk)
            {
                uv =0;
                emit Print(" > Полученное значение: " + v + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(uv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            var = QString::number(uv);
            return;
        }
        if(VarTypeSave =="quint32")
        {
            quint32 uv =0;
            uv = v.toULongLong(&IsOk);
            if(!IsOk)
            {
                uv =0;
                emit Print(" > Полученное значение: " + v + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(uv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            var = QString::number(uv);
            return;
        }
        if(VarTypeSave =="quint64")
        {
            quint64 uv =0;
            uv = v.toULongLong(&IsOk);
            if(!IsOk)
            {
                uv =0;
                emit Print(" > Полученное значение: " + v + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(uv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            var = QString::number(uv);
            return;
        }
        if(VarTypeSave =="bit")
        {
            quint8 uv =0;
            uv = v.toULongLong(&IsOk);
            if(!IsOk)
            {
                uv =0;
                emit Print(" > Полученное значение: " + v + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(uv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            uv <<= 7; uv >>= 7;
            var = QString::number(uv);
            return;
        }
        // Целочисленные со знаком
        if(VarTypeSave =="qint8")
        {
            qint8 iv =0;
            iv = v.toLongLong(&IsOk);
            if(!IsOk)
            {
                iv =0;
                emit Print(" > Полученное значение: " + v + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(iv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            var = QString::number(iv);
            return;
        }
        if(VarTypeSave =="qint16")
        {
            qint16 iv =0;
            iv = v.toLongLong(&IsOk);
            if(!IsOk)
            {
                iv =0;
                emit Print(" > Полученное значение: " + v + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(iv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            var = QString::number(iv);
            return;
        }
        if(VarTypeSave =="qint32")
        {
            qint32 iv =0;
            iv = v.toLongLong(&IsOk);
            if(!IsOk)
            {
                iv =0;
                emit Print(" > Полученное значение: " + v + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(iv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            var = QString::number(iv);
            return;
        }
        if(VarTypeSave =="qint64")
        {
            qint64 iv =0;
            iv = v.toLongLong(&IsOk);
            if(!IsOk)
            {
                iv =0;
                emit Print(" > Полученное значение: " + v + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(iv) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            var = QString::number(iv);
            return;
        }
        // Преобразование к вещественным типам
        if(VarTypeSave =="float")
        {
            float fv =0;
            fv = v.toFloat(&IsOk);
            if(!IsOk)
            {
                fv =0;
                emit Print(" > Полученное значение: " + v + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(fv, 'f', 6) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            var = QString::number(fv, 'f', 6);
            return;
        }
        if(VarTypeSave =="double")
        {
            double dv =0;
            dv = v.toDouble(&IsOk);
            if(!IsOk)
            {
                dv =0;
                emit Print(" > Полученное значение: " + v + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(dv, 'f', 6) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            var = QString::number(dv, 'f', 6);
            return;
        }
        // К строке
        if(VarTypeSave == "QString")
            return;
        // К контрольным суммам
        if(VarTypeSave == "MOD2")
        {
            quint8 mv =0;
            mv = v.toULongLong(&IsOk);
            if(!IsOk)
            {
                mv =0;
                emit Print(" > Полученное значение: " + v + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(mv, 16) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            var = QString::number(mv, 16);
            return;
        }
        if(VarTypeSave == "CRC16")
        {
            quint16 cv =0;
            cv = v.toULongLong(&IsOk);
            if(!IsOk)
            {
                cv =0;
                emit Print(" > Полученное значение: " + v + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(cv, 16) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            var = QString::number(cv, 16);
            return;
        }
        if(VarTypeSave == "CRC32")
        {
            quint32 cv =0;
            cv = v.toULongLong(&IsOk);
            if(!IsOk)
            {
                cv =0;
                emit Print(" > Полученное значение: " + v + "\r\n", false);
                emit Print(" > Результат преобразования: " + QString::number(cv, 16) + "\r\n", false);
                emit Print(" > Значения не равны\r\n", false);
            }
            var = QString::number(cv, 16);
            return;
        }
    }
}
// 04.12.2016
// 23.01.2017 При успешной проверке диапазона производится конвертация значения датчика в значение параметра
// Функция проверки диапазона значения полученного парамета
// ПРИМЕЧАНИЕ полностью пакет передается на случай если нужно проверить контрольную сумму
// 01.05.2017 Изменен на тип хранения
// 03.05.2017 Уменьшена разрядность типов float и double до 2-х
bool CAnswer::TestSensorRange(CVarDB param, QByteArray& packet,
                              QString& Var, QString& ConvertedVar, CCommand command)
{
    QString VarType = param.VarTypeSave;
    QString SensorMin = param.VarSensor_MIN;
    QString SensorMax = param.VarSensor_MAX;
    QString ParamMin = param.VarParam_MIN;
    QString ParamMax = param.VarParam_MAX;
    bool IsOk = false, IsOn = false, IsOm = false, IsOf = false, IsOt = false, IsOs = true;
    // Проверка сразу всех целочисленных типов
    if((VarType =="quint8")|(VarType =="quint16")|(VarType =="quint32")|(VarType =="quint64")|(VarType =="bit"))
    {
        quint64 var = Var.toULongLong(&IsOk);
        quint64 min = SensorMin.toULongLong(&IsOn);
        quint64 max = SensorMax.toULongLong(&IsOm);
        quint64 minP = ParamMin.toULongLong(&IsOf);
        quint64 maxP = ParamMax.toULongLong(&IsOt);
        // Вне диапазона
        if((var > max)|(var < min)|(min == max)|(minP == maxP))
            IsOs = false;
        else
            ConvertedVar = QString::number((((var - min) * (maxP - minP)) / (max - min)) + minP);
    }
    else if((VarType =="qint8")|(VarType =="qint16")|(VarType =="qint32")|(VarType =="qint64"))
    {
        qint64 var = Var.toLongLong(&IsOk);
        qint64 min = SensorMin.toLongLong(&IsOn);
        qint64 max = SensorMax.toLongLong(&IsOm);
        qint64 minP = ParamMin.toLongLong(&IsOf);
        qint64 maxP = ParamMax.toLongLong(&IsOt);
        // Вне диапазона
        if((var > max)|(var < min)|(min == max)|(minP == maxP))
            IsOs = false;
        else
        {
            //qint64 a = (((var - min) * (maxP - minP)) / (max - min)) + minP;
 /*           emit Print(" ТЕСТИРОВАНИЕ датчик " + QString::number(var) + " " + Var + "\r\n", false);
            emit Print(" ТЕСТИРОВАНИЕ датчик min " + QString::number(min) + " " + SensorMin + "\r\n", false);
            emit Print(" ТЕСТИРОВАНИЕ датчик max " + QString::number(max) + " " + SensorMax + "\r\n", false);
            emit Print(" ТЕСТИРОВАНИЕ параметр min " + QString::number(minP) + " " + ParamMin + "\r\n", false);
            emit Print(" ТЕСТИРОВАНИЕ параметр max " + QString::number(maxP) + " " + ParamMax + "\r\n", false);*/
            ConvertedVar = QString::number((((var - min) * (maxP - minP)) / (max - min)) + minP);
            //ConvertedVar = QString::number(a, 10);
 //           emit Print(" ТЕСТИРОВАНИЕ результат " + ConvertedVar + "\r\n", false);
        }
    }
    else if((VarType =="float")|(VarType == "double"))
    {
        double var = Var.toDouble(&IsOk);
        double min = SensorMin.toDouble(&IsOn);
        double max = SensorMax.toDouble(&IsOm);
        double minP = ParamMin.toDouble(&IsOf);
        double maxP = ParamMax.toDouble(&IsOt);
        // Вне диапазона
        if((var > max)|(var < min)|(min == max)|(minP == maxP))
            IsOs = false;
        else
            ConvertedVar = QString::number(((((var - min) * (maxP - minP)) / (max - min)) + minP), 'f', 2);
    }
    else if(VarType == "MOD2")
    {
        quint8 crc =0, a =0;
        crc += Var.toULongLong(&IsOk, 16);
        unsigned char array[param.VarOffset + 1];
        unsigned char KS;
        for(unsigned int i =0; i < sizeof(array); i++)
            array[i] = *(((unsigned char*)packet.data()) + i);
        GetMOD2_8bit(array, &KS, sizeof(array));
        array[0] = KS;
        a ^= (quint8)((unsigned char)array[0]);
        SensorMin = QString::number(a,16);
        if(crc != a)
            IsOs = false;
        else
        {
            IsOn = IsOm = IsOs = IsOf = IsOt = true;
            ConvertedVar = Var;
        }
    }
    else if(VarType == "CRC16")
    {
        quint16 crc =0, a =0;
        crc += Var.toULongLong(&IsOk, 16);
        unsigned char array[param.VarOffset + 2];
        unsigned char KS_H, KS_L;
        for(unsigned int i =0; i < sizeof(array); i++)
            array[i] = *(((unsigned char*)packet.data()) + i);
        GetCRC16(array, &KS_H, &KS_L, sizeof(array));
        array[0] = KS_H; array[1] = KS_L;
        a ^= (((quint16)((unsigned char)array[0])) << 8);
        a ^= (quint16)((unsigned char)array[1]);
        SensorMin = QString::number(a,16);
        if(crc != a)
            IsOs = false;
        else
        {
            IsOk = IsOn = IsOm = IsOs = IsOf = IsOt = true;
            ConvertedVar = Var;
        }
    }
    else if(VarType == "CRC32")
    {
        quint32 crc =0, a =0;
        crc += Var.toULongLong(&IsOk, 16);
        unsigned char array[param.VarOffset + 4];
        unsigned char KS_H1, KS_H2, KS_L1, KS_L2;
        for(unsigned int i =0; i < sizeof(array); i++)
            array[i] = *(((unsigned char*)packet.data()) + i);
        GetCRC32(array, &KS_H1, &KS_H2, &KS_L1, &KS_L2, sizeof(array));
        array[0] = KS_H1; array[1] = KS_H2; array[2] = KS_L1; array[3] = KS_L2;
        a ^= (((quint32)((unsigned char)array[0])) << 24);
        a ^= (((quint32)((unsigned char)array[1])) << 16);
        a ^= (((quint32)((unsigned char)array[2])) << 8);
        a ^= (quint32)((unsigned char)array[3]);
        SensorMin = QString::number(a,16);
        if(crc != a)
            IsOs = false;
        else
        {
            IsOk = IsOn = IsOm = IsOs = IsOf = IsOt = true;
            ConvertedVar = Var;
        }
    }
    // Если переменная типа строка то не проверяется
    else if(VarType == "QString")
    {
        IsOk = IsOn = IsOm = IsOs = IsOf = IsOt = true;
        ConvertedVar = Var;
    }
    // Все в норме
    if((IsOk)&(IsOn)&(IsOm)&(IsOs)&(IsOf)&(IsOt))
        return true;
    if((!VarType.contains("CRC"))&(!VarType.contains("MOD")))
    {
        emit Print(" > Полученное значение вне установленного диапазона\r\n", false);
        emit Print(" > Полученное значение: \"" + Var + " \"\r\n", false);
        emit Print(" > Диапазон сигнала: \"" + SensorMin + " \\" + SensorMax + " \"\r\n", false);
        emit Print(" > Диапазон параметра: \"" + ParamMin + " \\" + ParamMax + " \"\r\n", false);
    }
    else
    {
        emit Print(" > Ошибка контрольной суммы\r\n", false);
        emit Print(" > Контрольная сумма полученная: \"" + Var.toUpper() + " \"\r\n", false);
        emit Print(" > Контрольная сумма ожидаемая: \"" + SensorMin.toUpper() + " \"\r\n", false);
    }
// 25.04.2017 Отключена сигнализация при ошибке параметра
    /*if(param.AlarmSet)
    {
        QString AText;
        if((VarType.contains("CRC"))|(VarType.contains("MOD")))
            AText = "Ошибка контрольной суммы";
        else
            AText = "Полученное значение вне установленного диапазона";
        IsAlarm(param, ConvertedVar, command, false, AText);
    }*/
    UpdateCommand("ERROR", command);
    ((CListenThread*)par)->CommandInWork = false;
    return false;
}
// 04.01.2017 Функция проверки аварийного состояния параметра
// Возвращает флаг тревоги:
// false - тревоги нет
// true - тревога есть
// 01.05.2017 Проверка производится по типу хранения
bool CAnswer::IsAlarm(CVarDB param, QString ConvertedVar, CCommand command, bool FullTest, QString AText)
{
    QString VarType = param.VarTypeSave;
    QStringList AlarmText;
    bool IsAlarm = false;
    // Если полная проверка параметра
    if(FullTest)
    {
        // Проверка конвертации не производится т.к. уже проверялась
        if((VarType =="bit")|(VarType =="quint8")|(VarType =="quint16")|(VarType =="quint32")|(VarType =="quint64"))
            IsAlarmParameter(param, ConvertedVar.toULongLong(), IsAlarm, AlarmText);
        else if((VarType =="qint8")|(VarType =="qint16")|(VarType =="qint32")|(VarType =="qint64"))
            IsAlarmParameter(param, ConvertedVar.toLongLong(), IsAlarm, AlarmText);
        else if(VarType =="float")
            IsAlarmParameter(param, ConvertedVar.toFloat(), IsAlarm, AlarmText);
        else if(VarType =="double")
            IsAlarmParameter(param, ConvertedVar.toDouble(), IsAlarm, AlarmText);
        else if(VarType =="QString")
            IsAlarmParameter(param, ConvertedVar, IsAlarm, AlarmText);
        else if((VarType == "CRC16")|(VarType == "CRC32")|(VarType == "MOD2"))
            IsAlarm = false; // Если CRC неверная, то TestSensorRange() данную ошибку отработала
    }
    // Или если проверка не нужна
    else
    {
        IsAlarm = true;
        AlarmText << AText;
    }
    QString ParamName = command.DBData.EqName + "_" + command.DBData.EqNumber + "_" + param.VarName;
    // Запись тревоги в БД производится только если установлен флаг сигнализации
    if((IsAlarm)&(param.AlarmSet == 1))
    {
        bool Flag = false;
        // Проверка флага успешна?
        if(IsNotFlagSetted(ParamName, Flag))
        {
            // Если вернулся выставленный флаг - значит следует записать тревогу
            if(Flag)
            {
                // Записать строку с тревогой
                SaveAlarm(ParamName, ConvertedVar, AlarmText);
            }
        }
    }
    // 05.02.2017 Если параметр находится за границами нормы, но флаг сигнализации не установлен, будет произведен
    //            сброс тревоги, если она установлена
    else
    {
        // Сбросить флаг тревоги, если установлен
        FlagSkip(ParamName);
    }
    return IsAlarm;
}

// 15.12.2016 Запись сообщений в журнал тревог
// Проверка наличия тревоги по данному текущему параметру
bool CAnswer::IsAlarmParameter(CVarDB param, quint64 var, bool& IsAlarm, QStringList& AlarmText)
{
    QStringList minBorder = param.VarBorder_MIN.split("/");
    QStringList maxBorder = param.VarBorder_MAX.split("/");
    // Если границы не заданы, или заданы некорректно тогда просто выход без ошибок и без тревоги
    if((minBorder.size() <=0)|(maxBorder.size() <=0)|(minBorder.size()!= maxBorder.size()))
    {
        emit Print(" > Границы сигнализации не заданы, либо некорректны\r\n", false);
        IsAlarm = false;
        return true;
    }
    // Шаблон текста сообщений будет выведен, только если будет соответствовать количеству границ
    // иначе будет записан стандартный текст
    QStringList AlarmMin = param.AlarmMin.split("/");
    QStringList AlarmMax = param.AlarmMax.split("/");
    AlarmText.clear();
    AlarmText  << "Аварийная величина параметра";    // Значение по умолчанию будет изменено
    quint64 minB[minBorder.size()], maxB[maxBorder.size()];
    int Size = minBorder.size();
    bool IsOk, IsAlarmText = false;
    IsAlarm = false;
    // Если число границ соответствует числу сообщений
    if((minBorder.size() == AlarmMin.size())&(maxBorder.size() == AlarmMax.size()))
        IsAlarmText = true;
    // 1. Преобразование значений границ
    for(int i=0; i < minBorder.size(); i++)
    {
        // Индекс границ
        minB[i] = minBorder.at(i).toULongLong(&IsOk);
        if(IsOk)
            maxB[i] = maxBorder.at(i).toULongLong(&IsOk);
        // Если значение не преобразуется, то границы удаляются и текст удаляются
        if(!IsOk)
        {
            minBorder.removeAt(i);
            maxBorder.removeAt(i);
            if(IsAlarmText)
            {
                AlarmMin.removeAt(i);
                AlarmMax.removeAt(i);
            }
            i--;
            continue;
        }
        // 2. Проверка в диапазоне ли параметр
        // 2.1 Проверка до первой границы
        if(i == 0)
        {
            // 2.1.1 Вариант в пределах - норма
            if(minB[i] < maxB[i])
            {
                if(var < minB[i])
                {
                    IsAlarm = true;
                    // Установка текстовой строки на минимальное значение
                    if(IsAlarmText)
                    {
                        AlarmText.clear();
                        AlarmText << AlarmMin.at(i);
                    }
                    // Запись значения параметра
                    AlarmText << "Значение параметра: " + QString::number(var);
                    // Запись аварийной границы
                    AlarmText << "Параметр меньше минимальной границы: " + QString::number(minB[i]);
                    break;
                }
            }
            // 2.1.2 Вариант в пределах - авария
            else if(minB[i] > maxB[i])
            {
                if(var < maxB[i])
                {
                    IsAlarm = false;
                    break;
                }
            }
        }
        // 2.2 Проверка в границах
        // 2.2.1 Вариант в пределах - норма
        if(minB[i] < maxB[i])
        {
            if((var > minB[i])&(var < maxB[i]))
            {
                IsAlarm = false;
                break;
            }
        }
        // Вариант в пределах - авария
        else if(minB[i] > maxB[i])
        {
            if((var > maxB[i])&(var < minB[i]))
            {
                IsAlarm = true;
                // Установка аварийного сообщения согласно проекта
                if(IsAlarmText)
                {
                    AlarmText.clear();
                    AlarmText << AlarmMin.at(i);
                }
                // Запись значения параметра
                AlarmText << "Значение параметра: " + QString::number(var);
                // Запись аварийного диапазона
                AlarmText << "Параметр в аварийном диапазоне: от " + QString::number(maxB[i]) +
                             " до " + QString::number(minB[i]);
                break;
            }
        }
        // 2.3 Проверка между границ
        if(i > 0)
        {
            // 2.3.1 Вариант в пределах - норма
            if(minB[i] < maxB[i])
            {
                // 2.3.1a Если предыдущий вариант был такой же
                if(minB[i-1] < maxB[i-1])
                {
                    if((var < minB[i])&(var > maxB[i-1]))
                    {
                        IsAlarm = true;
                        // Установка аварийного сообщения согласно проекта
                        if(IsAlarmText)
                        {
                            AlarmText.clear();
                            AlarmText << AlarmMin.at(i);
                        }
                        // Запись значения параметра
                        AlarmText << "Значение параметра: " + QString::number(var);
                        // Запись аварийного диапазона
                        AlarmText << "Параметр в аварийном диапазоне: от " + QString::number(maxB[i-1]) +
                                " до " + QString::number(minB[i]);
                        break;
                    }
                }
                // 2.3.1б Если предыдущий вариант был другой
                else if(minB[i-1] > maxB[i-1])
                {
                    if((var < minB[i])&(var > minB[i-1]))
                    {
                        IsAlarm = true;
                        // Установка аварийного сообщения согласно проекта
                        if(IsAlarmText)
                        {
                            AlarmText.clear();
                            AlarmText << AlarmMin.at(i);
                        }
                        // Запись значения параметра
                        AlarmText << "Значение параметра: " + QString::number(var);
                        // Запись аварийного диапазона
                        AlarmText << "Параметр в аварийном диапазоне: от " + QString::number(minB[i-1]) +
                                " до " + QString::number(minB[i]);
                        break;
                    }
                }
            }
            // 2.3.2 Вариант в пределах - авария
            else if(minB[i] > maxB[i])
            {
                // 2.3.2а Если предыдущий вариант был такой же
                if(minB[i-1] > maxB[i-1])
                {
                    if((var < maxB[i])&(var > minB[i-1]))
                    {
                        IsAlarm = false;
                        break;
                    }
                }
                // 2.3.2б Если предыдущий вариант был другой
                else if(minB[i-1] < maxB[i-1])
                {
                    if((var < maxB[i])&(var > maxB[i-1]))
                    {
                        IsAlarm = true;
                        // Установка аварийного сообщения согласно проекта
                        if(IsAlarmText)
                        {
                            AlarmText.clear();
                            AlarmText << AlarmMin.at(i);
                        }
                        // Запись значения параметра
                        AlarmText << "Значение параметра: " + QString::number(var);
                        // Запись аварийного диапазона
                        AlarmText << "Параметр в аварийном диапазоне: от " + QString::number(maxB[i-1]) +
                                " до " + QString::number(maxB[i]);
                        break;
                    }
                }
            }
        }
        // 2.4 Проверка последней границы
        if(i == (Size-1))
        {
            // 2.4.1 Вариант в пределах - норма
            if(minB[i] < maxB[i])
            {
                if(var > maxB[i])
                {
                    IsAlarm = true;
                    // Установка аварийного сообщения согласно проекта
                    if(IsAlarmText)
                    {
                        AlarmText.clear();
                        AlarmText << AlarmMax.at(i);
                    }
                    // Запись значения параметра
                    AlarmText << "Значение параметра: " + QString::number(var);
                    // Запись аварийной границы
                    AlarmText << "Параметр больше максимальной границы: " + QString::number(maxB[i]);
                    break;
                }
            }
            // 2.4.2 Вариант в пределах - авария
            if(minB[i] > maxB[i])
            {
                if(var > minB[i])
                {
                    IsAlarm = false;
                    break;
                }
            }
        }
    }
    return true;
}
// 26.01.2017
// Проверка наличия тревоги по данному текущему параметру
bool CAnswer::IsAlarmParameter(CVarDB param, qint64 var, bool& IsAlarm, QStringList& AlarmText)
{
    QStringList minBorder = param.VarBorder_MIN.split("/");
    QStringList maxBorder = param.VarBorder_MAX.split("/");
    // Если границы не заданы, или заданы некорректно тогда просто выход без ошибок и без тревоги
    if((minBorder.size() <=0)|(maxBorder.size() <=0)|(minBorder.size()!= maxBorder.size()))
    {
        emit Print(" > Границы сигнализации не заданы, либо некорректны\r\n", false);
        IsAlarm = false;
        return true;
    }
    // Шаблон текста сообщений будет выведен, только если будет соответствовать количеству границ
    // иначе будет записан стандартный текст
    QStringList AlarmMin = param.AlarmMin.split("/");
    QStringList AlarmMax = param.AlarmMax.split("/");
    AlarmText.clear();
    AlarmText  << "Аварийная величина параметра";    // Значение по умолчанию будет изменено
    qint64 minB[minBorder.size()], maxB[maxBorder.size()];
    int Size = minBorder.size();
    bool IsOk, IsAlarmText = false;
    IsAlarm = false;
    // Если число границ соответствует числу сообщений
    if((minBorder.size() == AlarmMin.size())&(maxBorder.size() == AlarmMax.size()))
        IsAlarmText = true;
    // 1. Преобразование значений границ
    for(int i=0; i < minBorder.size(); i++)
    {
        // Индекс границ
        minB[i] = minBorder.at(i).toLongLong(&IsOk);
        if(IsOk)
            maxB[i] = maxBorder.at(i).toLongLong(&IsOk);
        // Если значение не преобразуется, то границы удаляются и текст удаляются
        if(!IsOk)
        {
            minBorder.removeAt(i);
            maxBorder.removeAt(i);
            if(IsAlarmText)
            {
                AlarmMin.removeAt(i);
                AlarmMax.removeAt(i);
            }
            i--;
            continue;
        }
        // 2. Проверка в диапазоне ли параметр
        // 2.1 Проверка до первой границы
        if(i == 0)
        {
            // 2.1.1 Вариант в пределах - норма
            if(minB[i] < maxB[i])
            {
                if(var < minB[i])
                {
                    IsAlarm = true;
                    // Установка текстовой строки на минимальное значение
                    if(IsAlarmText)
                    {
                        AlarmText.clear();
                        AlarmText << AlarmMin.at(i);
                    }
                    // Запись значения параметра
                    AlarmText << "Значение параметра: " + QString::number(var);
                    // Запись аварийной границы
                    AlarmText << "Параметр меньше минимальной границы: " + QString::number(minB[i]);
                    break;
                }
            }
            // 2.1.2 Вариант в пределах - авария
            else if(minB[i] > maxB[i])
            {
                if(var < maxB[i])
                {
                    IsAlarm = false;
                    break;
                }
            }
        }
        // 2.2 Проверка в границах
        // 2.2.1 Вариант в пределах - норма
        if(minB[i] < maxB[i])
        {
            if((var > minB[i])&(var < maxB[i]))
            {
                IsAlarm = false;
                break;
            }
        }
        // Вариант в пределах - авария
        else if(minB[i] > maxB[i])
        {
            if((var > maxB[i])&(var < minB[i]))
            {
                IsAlarm = true;
                // Установка аварийного сообщения согласно проекта
                if(IsAlarmText)
                {
                    AlarmText.clear();
                    AlarmText << AlarmMin.at(i);
                }
                // Запись значения параметра
                AlarmText << "Значение параметра: " + QString::number(var);
                // Запись аварийного диапазона
                AlarmText << "Параметр в аварийном диапазоне: от " + QString::number(maxB[i]) +
                             " до " + QString::number(minB[i]);
                break;
            }
        }
        // 2.3 Проверка между границ
        if(i > 0)
        {
            // 2.3.1 Вариант в пределах - норма
            if(minB[i] < maxB[i])
            {
                // 2.3.1a Если предыдущий вариант был такой же
                if(minB[i-1] < maxB[i-1])
                {
                    if((var < minB[i])&(var > maxB[i-1]))
                    {
                        IsAlarm = true;
                        // Установка аварийного сообщения согласно проекта
                        if(IsAlarmText)
                        {
                            AlarmText.clear();
                            AlarmText << AlarmMin.at(i);
                        }
                        // Запись значения параметра
                        AlarmText << "Значение параметра: " + QString::number(var);
                        // Запись аварийного диапазона
                        AlarmText << "Параметр в аварийном диапазоне: от " + QString::number(maxB[i-1]) +
                                " до " + QString::number(minB[i]);
                        break;
                    }
                }
                // 2.3.1б Если предыдущий вариант был другой
                else if(minB[i-1] > maxB[i-1])
                {
                    if((var < minB[i])&(var > minB[i-1]))
                    {
                        IsAlarm = true;
                        // Установка аварийного сообщения согласно проекта
                        if(IsAlarmText)
                        {
                            AlarmText.clear();
                            AlarmText << AlarmMin.at(i);
                        }
                        // Запись значения параметра
                        AlarmText << "Значение параметра: " + QString::number(var);
                        // Запись аварийного диапазона
                        AlarmText << "Параметр в аварийном диапазоне: от " + QString::number(minB[i-1]) +
                                " до " + QString::number(minB[i]);
                        break;
                    }
                }
            }
            // 2.3.2 Вариант в пределах - авария
            else if(minB[i] > maxB[i])
            {
                // 2.3.2а Если предыдущий вариант был такой же
                if(minB[i-1] > maxB[i-1])
                {
                    if((var < maxB[i])&(var > minB[i-1]))
                    {
                        IsAlarm = false;
                        break;
                    }
                }
                // 2.3.2б Если предыдущий вариант был другой
                else if(minB[i-1] < maxB[i-1])
                {
                    if((var < maxB[i])&(var > maxB[i-1]))
                    {
                        IsAlarm = true;
                        // Установка аварийного сообщения согласно проекта
                        if(IsAlarmText)
                        {
                            AlarmText.clear();
                            AlarmText << AlarmMin.at(i);
                        }
                        // Запись значения параметра
                        AlarmText << "Значение параметра: " + QString::number(var);
                        // Запись аварийного диапазона
                        AlarmText << "Параметр в аварийном диапазоне: от " + QString::number(maxB[i-1]) +
                                " до " + QString::number(maxB[i]);
                        break;
                    }
                }
            }
        }
        // 2.4 Проверка последней границы
        if(i == (Size-1))
        {
            // 2.4.1 Вариант в пределах - норма
            if(minB[i] < maxB[i])
            {
                if(var > maxB[i])
                {
                    IsAlarm = true;
                    // Установка аварийного сообщения согласно проекта
                    if(IsAlarmText)
                    {
                        AlarmText.clear();
                        AlarmText << AlarmMax.at(i);
                    }
                    // Запись значения параметра
                    AlarmText << "Значение параметра: " + QString::number(var);
                    // Запись аварийной границы
                    AlarmText << "Параметр больше максимальной границы: " + QString::number(maxB[i]);
                    break;
                }
            }
            // 2.4.2 Вариант в пределах - авария
            if(minB[i] > maxB[i])
            {
                if(var > minB[i])
                {
                    IsAlarm = false;
                    break;
                }
            }
        }
    }
    return true;
}
// 26.01.2017
// Проверка наличия тревоги по данному текущему параметру
bool CAnswer::IsAlarmParameter(CVarDB param, double var, bool& IsAlarm, QStringList& AlarmText)
{
    QStringList minBorder = param.VarBorder_MIN.split("/");
    QStringList maxBorder = param.VarBorder_MAX.split("/");
    // Если границы не заданы, или заданы некорректно тогда просто выход без ошибок и без тревоги
    if((minBorder.size() <=0)|(maxBorder.size() <=0)|(minBorder.size()!= maxBorder.size()))
    {
        emit Print(" > Границы сигнализации не заданы, либо некорректны\r\n", false);
        IsAlarm = false;
        return true;
    }
    // Шаблон текста сообщений будет выведен, только если будет соответствовать количеству границ
    // иначе будет записан стандартный текст
    QStringList AlarmMin = param.AlarmMin.split("/");
    QStringList AlarmMax = param.AlarmMax.split("/");
    AlarmText.clear();
    AlarmText  << "Аварийная величина параметра";    // Значение по умолчанию будет изменено
    double minB[minBorder.size()], maxB[maxBorder.size()];
    int Size = minBorder.size();
    bool IsOk, IsAlarmText = false;
    IsAlarm = false;
    // Если число границ соответствует числу сообщений
    if((minBorder.size() == AlarmMin.size())&(maxBorder.size() == AlarmMax.size()))
        IsAlarmText = true;
    // 1. Преобразование значений границ
    for(int i=0; i < minBorder.size(); i++)
    {
        // Индекс границ
        minB[i] = minBorder.at(i).toDouble(&IsOk);
        if(IsOk)
            maxB[i] = maxBorder.at(i).toDouble(&IsOk);
        // Если значение не преобразуется, то границы удаляются и текст удаляются
        if(!IsOk)
        {
            minBorder.removeAt(i);
            maxBorder.removeAt(i);
            if(IsAlarmText)
            {
                AlarmMin.removeAt(i);
                AlarmMax.removeAt(i);
            }
            i--;
            continue;
        }
        // 2. Проверка в диапазоне ли параметр
        // 2.1 Проверка до первой границы
        if(i == 0)
        {
            // 2.1.1 Вариант в пределах - норма
            if(minB[i] < maxB[i])
            {
                if(var < minB[i])
                {
                    IsAlarm = true;
                    // Установка текстовой строки на минимальное значение
                    if(IsAlarmText)
                    {
                        AlarmText.clear();
                        AlarmText << AlarmMin.at(i);
                    }
                    // Запись значения параметра
                    AlarmText << "Значение параметра: " + QString::number(var, 'f', 6);
                    // Запись аварийной границы
                    AlarmText << "Параметр меньше минимальной границы: " + QString::number(minB[i], 'f', 6);
                    break;
                }
            }
            // 2.1.2 Вариант в пределах - авария
            else if(minB[i] > maxB[i])
            {
                if(var < maxB[i])
                {
                    IsAlarm = false;
                    break;
                }
            }
        }
        // 2.2 Проверка в границах
        // 2.2.1 Вариант в пределах - норма
        if(minB[i] < maxB[i])
        {
            if((var > minB[i])&(var < maxB[i]))
            {
                IsAlarm = false;
                break;
            }
        }
        // Вариант в пределах - авария
        else if(minB[i] > maxB[i])
        {
            if((var > maxB[i])&(var < minB[i]))
            {
                IsAlarm = true;
                // Установка аварийного сообщения согласно проекта
                if(IsAlarmText)
                {
                    AlarmText.clear();
                    AlarmText << AlarmMin.at(i);
                }
                // Запись значения параметра
                AlarmText << "Значение параметра: " + QString::number(var, 'f', 6);
                // Запись аварийного диапазона
                AlarmText << "Параметр в аварийном диапазоне: от " + QString::number(maxB[i], 'f', 6) +
                             " до " + QString::number(minB[i], 'f', 6);
                break;
            }
        }
        // 2.3 Проверка между границ
        if(i > 0)
        {
            // 2.3.1 Вариант в пределах - норма
            if(minB[i] < maxB[i])
            {
                // 2.3.1a Если предыдущий вариант был такой же
                if(minB[i-1] < maxB[i-1])
                {
                    if((var < minB[i])&(var > maxB[i-1]))
                    {
                        IsAlarm = true;
                        // Установка аварийного сообщения согласно проекта
                        if(IsAlarmText)
                        {
                            AlarmText.clear();
                            AlarmText << AlarmMin.at(i);
                        }
                        // Запись значения параметра
                        AlarmText << "Значение параметра: " + QString::number(var, 'f', 6);
                        // Запись аварийного диапазона
                        AlarmText << "Параметр в аварийном диапазоне: от " + QString::number(maxB[i-1], 'f', 6) +
                                " до " + QString::number(minB[i], 'f', 6);
                        break;
                    }
                }
                // 2.3.1б Если предыдущий вариант был другой
                else if(minB[i-1] > maxB[i-1])
                {
                    if((var < minB[i])&(var > minB[i-1]))
                    {
                        IsAlarm = true;
                        // Установка аварийного сообщения согласно проекта
                        if(IsAlarmText)
                        {
                            AlarmText.clear();
                            AlarmText << AlarmMin.at(i);
                        }
                        // Запись значения параметра
                        AlarmText << "Значение параметра: " + QString::number(var, 'f', 6);
                        // Запись аварийного диапазона
                        AlarmText << "Параметр в аварийном диапазоне: от " + QString::number(minB[i-1], 'f', 6) +
                                " до " + QString::number(minB[i], 'f', 6);
                        break;
                    }
                }
            }
            // 2.3.2 Вариант в пределах - авария
            else if(minB[i] > maxB[i])
            {
                // 2.3.2а Если предыдущий вариант был такой же
                if(minB[i-1] > maxB[i-1])
                {
                    if((var < maxB[i])&(var > minB[i-1]))
                    {
                        IsAlarm = false;
                        break;
                    }
                }
                // 2.3.2б Если предыдущий вариант был другой
                else if(minB[i-1] < maxB[i-1])
                {
                    if((var < maxB[i])&(var > maxB[i-1]))
                    {
                        IsAlarm = true;
                        // Установка аварийного сообщения согласно проекта
                        if(IsAlarmText)
                        {
                            AlarmText.clear();
                            AlarmText << AlarmMin.at(i);
                        }
                        // Запись значения параметра
                        AlarmText << "Значение параметра: " + QString::number(var, 'f', 6);
                        // Запись аварийного диапазона
                        AlarmText << "Параметр в аварийном диапазоне: от " + QString::number(maxB[i-1], 'f', 6) +
                                " до " + QString::number(maxB[i], 'f', 6);
                        break;
                    }
                }
            }
        }
        // 2.4 Проверка последней границы
        if(i == (Size-1))
        {
            // 2.4.1 Вариант в пределах - норма
            if(minB[i] < maxB[i])
            {
                if(var > maxB[i])
                {
                    IsAlarm = true;
                    // Установка аварийного сообщения согласно проекта
                    if(IsAlarmText)
                    {
                        AlarmText.clear();
                        AlarmText << AlarmMax.at(i);
                    }
                    // Запись значения параметра
                    AlarmText << "Значение параметра: " + QString::number(var, 'f', 6);
                    // Запись аварийной границы
                    AlarmText << "Параметр больше максимальной границы: " + QString::number(maxB[i], 'f', 6);
                    break;
                }
            }
            // 2.4.2 Вариант в пределах - авария
            if(minB[i] > maxB[i])
            {
                if(var > minB[i])
                {
                    IsAlarm = false;
                    break;
                }
            }
        }
    }
    return true;
}
// 26.01.2017
// Проверка наличия тревоги по данному текущему параметру
bool CAnswer::IsAlarmParameter(CVarDB param, float var, bool& IsAlarm, QStringList& AlarmText)
{
    QStringList minBorder = param.VarBorder_MIN.split("/");
    QStringList maxBorder = param.VarBorder_MAX.split("/");
    // Если границы не заданы, или заданы некорректно тогда просто выход без ошибок и без тревоги
    if((minBorder.size() <=0)|(maxBorder.size() <=0)|(minBorder.size()!= maxBorder.size()))
    {
        emit Print(" > Границы сигнализации не заданы, либо некорректны\r\n", false);
        IsAlarm = false;
        return true;
    }
    // Шаблон текста сообщений будет выведен, только если будет соответствовать количеству границ
    // иначе будет записан стандартный текст
    QStringList AlarmMin = param.AlarmMin.split("/");
    QStringList AlarmMax = param.AlarmMax.split("/");
    AlarmText.clear();
    AlarmText  << "Аварийная величина параметра";    // Значение по умолчанию будет изменено
    float minB[minBorder.size()], maxB[maxBorder.size()];
    int Size = minBorder.size();
    bool IsOk, IsAlarmText = false;
    IsAlarm = false;
    // Если число границ соответствует числу сообщений
    if((minBorder.size() == AlarmMin.size())&(maxBorder.size() == AlarmMax.size()))
        IsAlarmText = true;
    // 1. Преобразование значений границ
    for(int i=0; i < minBorder.size(); i++)
    {
        // Индекс границ
        minB[i] = minBorder.at(i).toFloat(&IsOk);
        if(IsOk)
            maxB[i] = maxBorder.at(i).toFloat(&IsOk);
        // Если значение не преобразуется, то границы удаляются и текст удаляются
        if(!IsOk)
        {
            minBorder.removeAt(i);
            maxBorder.removeAt(i);
            if(IsAlarmText)
            {
                AlarmMin.removeAt(i);
                AlarmMax.removeAt(i);
            }
            i--;
            continue;
        }
        // 2. Проверка в диапазоне ли параметр
        // 2.1 Проверка до первой границы
        if(i == 0)
        {
            // 2.1.1 Вариант в пределах - норма
            if(minB[i] < maxB[i])
            {
                if(var < minB[i])
                {
                    IsAlarm = true;
                    // Установка текстовой строки на минимальное значение
                    if(IsAlarmText)
                    {
                        AlarmText.clear();
                        AlarmText << AlarmMin.at(i);
                    }
                    // Запись значения параметра
                    AlarmText << "Значение параметра: " + QString::number(var, 'f', 6);
                    // Запись аварийной границы
                    AlarmText << "Параметр меньше минимальной границы: " + QString::number(minB[i], 'f', 6);
                    break;
                }
            }
            // 2.1.2 Вариант в пределах - авария
            else if(minB[i] > maxB[i])
            {
                if(var < maxB[i])
                {
                    IsAlarm = false;
                    break;
                }
            }
        }
        // 2.2 Проверка в границах
        // 2.2.1 Вариант в пределах - норма
        if(minB[i] < maxB[i])
        {
            if((var > minB[i])&(var < maxB[i]))
            {
                IsAlarm = false;
                break;
            }
        }
        // Вариант в пределах - авария
        else if(minB[i] > maxB[i])
        {
            if((var > maxB[i])&(var < minB[i]))
            {
                IsAlarm = true;
                // Установка аварийного сообщения согласно проекта
                if(IsAlarmText)
                {
                    AlarmText.clear();
                    AlarmText << AlarmMin.at(i);
                }
                // Запись значения параметра
                AlarmText << "Значение параметра: " + QString::number(var, 'f', 6);
                // Запись аварийного диапазона
                AlarmText << "Параметр в аварийном диапазоне: от " + QString::number(maxB[i], 'f', 6) +
                             " до " + QString::number(minB[i], 'f', 6);
                break;
            }
        }
        // 2.3 Проверка между границ
        if(i > 0)
        {
            // 2.3.1 Вариант в пределах - норма
            if(minB[i] < maxB[i])
            {
                // 2.3.1a Если предыдущий вариант был такой же
                if(minB[i-1] < maxB[i-1])
                {
                    if((var < minB[i])&(var > maxB[i-1]))
                    {
                        IsAlarm = true;
                        // Установка аварийного сообщения согласно проекта
                        if(IsAlarmText)
                        {
                            AlarmText.clear();
                            AlarmText << AlarmMin.at(i);
                        }
                        // Запись значения параметра
                        AlarmText << "Значение параметра: " + QString::number(var, 'f', 6);
                        // Запись аварийного диапазона
                        AlarmText << "Параметр в аварийном диапазоне: от " + QString::number(maxB[i-1], 'f', 6) +
                                " до " + QString::number(minB[i], 'f', 6);
                        break;
                    }
                }
                // 2.3.1б Если предыдущий вариант был другой
                else if(minB[i-1] > maxB[i-1])
                {
                    if((var < minB[i])&(var > minB[i-1]))
                    {
                        IsAlarm = true;
                        // Установка аварийного сообщения согласно проекта
                        if(IsAlarmText)
                        {
                            AlarmText.clear();
                            AlarmText << AlarmMin.at(i);
                        }
                        // Запись значения параметра
                        AlarmText << "Значение параметра: " + QString::number(var, 'f', 6);
                        // Запись аварийного диапазона
                        AlarmText << "Параметр в аварийном диапазоне: от " + QString::number(minB[i-1], 'f', 6) +
                                " до " + QString::number(minB[i], 'f', 6);
                        break;
                    }
                }
            }
            // 2.3.2 Вариант в пределах - авария
            else if(minB[i] > maxB[i])
            {
                // 2.3.2а Если предыдущий вариант был такой же
                if(minB[i-1] > maxB[i-1])
                {
                    if((var < maxB[i])&(var > minB[i-1]))
                    {
                        IsAlarm = false;
                        break;
                    }
                }
                // 2.3.2б Если предыдущий вариант был другой
                else if(minB[i-1] < maxB[i-1])
                {
                    if((var < maxB[i])&(var > maxB[i-1]))
                    {
                        IsAlarm = true;
                        // Установка аварийного сообщения согласно проекта
                        if(IsAlarmText)
                        {
                            AlarmText.clear();
                            AlarmText << AlarmMin.at(i);
                        }
                        // Запись значения параметра
                        AlarmText << "Значение параметра: " + QString::number(var, 'f', 6);
                        // Запись аварийного диапазона
                        AlarmText << "Параметр в аварийном диапазоне: от " + QString::number(maxB[i-1], 'f', 6) +
                                " до " + QString::number(maxB[i], 'f', 6);
                        break;
                    }
                }
            }
        }
        // 2.4 Проверка последней границы
        if(i == (Size-1))
        {
            // 2.4.1 Вариант в пределах - норма
            if(minB[i] < maxB[i])
            {
                if(var > maxB[i])
                {
                    IsAlarm = true;
                    // Установка аварийного сообщения согласно проекта
                    if(IsAlarmText)
                    {
                        AlarmText.clear();
                        AlarmText << AlarmMax.at(i);
                    }
                    // Запись значения параметра
                    AlarmText << "Значение параметра: " + QString::number(var, 'f', 6);
                    // Запись аварийной границы
                    AlarmText << "Параметр больше максимальной границы: " + QString::number(maxB[i], 'f', 6);
                    break;
                }
            }
            // 2.4.2 Вариант в пределах - авария
            if(minB[i] > maxB[i])
            {
                if(var > minB[i])
                {
                    IsAlarm = false;
                    break;
                }
            }
        }
    }
    return true;
}
// 27.01.2017
// Проверка наличия тревоги по данному текущему параметру
bool CAnswer::IsAlarmParameter(CVarDB param, QString var, bool& IsAlarm, QStringList& AlarmText)
{
    // 1. Значение текста границ должно соответствовать тексту полученного параметра
    // 2. Максимальная граница и минимальная - условные значения и не зависят друг от друга
    // 3. Сначала проверяются "минимальные" границы
    // 4. Количество границ должно соответствовать количеству сообщений иначе будет выведено сообщение по умолчанию
    QStringList minBorder = param.VarBorder_MIN.split("/");
    QStringList maxBorder = param.VarBorder_MAX.split("/");
    QStringList AlarmMin = param.AlarmMin.split("/");
    QStringList AlarmMax = param.AlarmMax.split("/");
    int minSize = minBorder.size();
    int maxSize = maxBorder.size();
    // Если границы не заданы
    if((minSize <=0)&(maxSize <=0))
    {
        emit Print(" > Границы сигнализации не заданы\r\n", false);
        IsAlarm = false;
        return true;
    }
    bool IsAlarmTextMin = false, IsAlarmTextMax = false;
    AlarmText.clear();
    AlarmText << "Получено аварийное сообщение\r\n";
    // Проверка заданы ли сообщения
    if(minSize ==AlarmMin.size())
        IsAlarmTextMin = true;
    if(maxSize ==AlarmMax.size())
        IsAlarmTextMax = true;
    // Проверка по минимальным границам
    for(int i =0; i < minSize; i++)
    {
        if(var == minBorder.at(i))
        {
            IsAlarm = true;
            if(IsAlarmTextMin)
            {
                AlarmText.clear();
                AlarmText << AlarmMin.at(i);
            }
            AlarmText << "(" + var + ")\r\n";
            return true;
        }
    }
    // Проверка по максимальным границам
    for(int i =0; i < maxSize; i++)
    {
        if(var == maxBorder.at(i))
        {
            IsAlarm = true;
            if(IsAlarmTextMax)
            {
                AlarmText.clear();
                AlarmText << AlarmMax.at(i);
            }
            AlarmText << "(" + var + ")\r\n";
            break;
        }
    }
    return true;
}
// 16.12.2016
// 23.01.2017 Убрано линейное преобразование значений. Передаются уже готовые к записи параметры
// Преобразование полученной переменной в параметр и запись в таблицу прибора
bool CAnswer::SaveParameter(CVarDB param, QString& ConvertedVar, CCommand command)
{
    // 1 Подготовка или проверка соединения с БД, наличие таблицы и строки
    int number;      // Номер строки с пустыми значениями или последней строки
    bool lastrow;    // Писать данные следует в строку с полученным номером или в новую
    if(!GetNumberLastRow(param, command, number, lastrow))
        return false;
    // 2 Запись параметра
    if(!SaveVar(param, ConvertedVar, command, number, lastrow))
        return false;
    return true;
}
// 16.12.2016
// Инициализация объектной БД
bool CAnswer::InitObjectDB()
{
    // Проверка открыта ли БД и если открыта, то повтроное открытие, иначе на инициализацию сначала
    if(ObjectDB.isOpen())
        if(ObjectDB.open())
            return true;
    // Данные БД
    pBusyControlData->lock();
    QString ODB = pControlData->ObjectDB;
    pBusyControlData->unlock();
    QStringList ODBData;
    if(!ODB.isEmpty())
        ODBData = ODB.split("/");
    if(ODBData.size()!=3)
    {
        QString DataPrint = " > ОШИБКА: база данных объекта " + QString::number(ObjectID) + " не открыта: ошибка данных подключения\r\n";
        emit Print(DataPrint, false);
        emit SendLastErrorDB("ОШИБКА ОТКРЫТИЯ ОБЪЕКТНОЙ БД");
        return false;
    }
    ObjectDB = QSqlDatabase::addDatabase("QMYSQL", "AnswerData_" + QString::number(ObjectID));         // Сборка по инструкции
    ObjectDB.setDatabaseName(ODBData.at(2));
    ObjectDB.setUserName(Login);                        // Логин сервера
    ObjectDB.setHostName(ODBData.at(0));                // Адрес или имя хоста из текстового поля
    ObjectDB.setPort(ODBData.at(1).toInt());            // Порт из текстовго поля
    ObjectDB.setPassword(Password);                     // Пароль сервера

    if (!ObjectDB.open())
    {
        {
            ObjectDB.close();
        }
        qDebug() << " > База данных объекта " << QString::number(ObjectID) << " не открыта: " << ObjectDB.lastError().databaseText();
        QString DataPrint = " > ОШИБКА: база данных объекта " + QString::number(ObjectID) + " не открыта: " + ObjectDB.lastError().databaseText() + "\r\n";
        emit Print(DataPrint, false);
        emit SendLastErrorDB("ОШИБКА ОТКРЫТИЯ ОБЪЕКТНОЙ БД");
        QSqlDatabase::removeDatabase("AnswerData_" + QString::number(ObjectID));
        return false;
    }
    emit SendDeleteTextLastErrorDB("ОШИБКА ОТКРЫТИЯ ОБЪЕКТНОЙ БД");
    qDebug() << " > База данных объекта " << QString::number(ObjectID) << " открыта: " << ObjectDB.databaseName();
    return true;
}
// 16.12.2016
// Получение имени таблицы прибора
QString CAnswer::GetEquipmentTableName(CCommand command)
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
// 16.12.2016
// Проверка наличия таблицы прибора
bool CAnswer::TestEqTables(CCommand command)
{
    QStringList tables = ObjectDB.tables();
    bool IsObjects = false;
    QString t = GetEquipmentTableName(command);

    foreach (const QString &mObjects, tables)
    {
        if(mObjects.contains(t))
        { IsObjects = true; break; }
    }
    // Если таблица не найдена, то удаление данной команды
    if(!IsObjects)
    {
        qDebug() << " > Таблица прибора (" << command.DBData.EqName << "/"
                 << command.DBData.EqNumber << ") не найдена";
        qDebug() << " > Данные команды CID:"
                 << QString::number(command.DBData.CID)
                 << " не обрабатываются";
        QString DataPrint = " > Таблица прибора (" + command.DBData.EqName + "/"
                            + command.DBData.EqNumber + ") не найдена\r\n";
        emit Print(DataPrint, false);
        DataPrint = " > Данные команды CID:" + QString::number(command.DBData.CID)
                    + " не обрабатываются\r\n";
        emit Print(DataPrint, false);
        return false;
    }
    return true;
}
// 16.12.2016
// Проверка наличия столбца в таблице прибора и передача номера строки в которую следует записать результат
bool CAnswer::GetNumberLastRow(CVarDB param, CCommand command, int& number, bool& lastrow)
{
    // 1.1 Инициализация БД объекта
    if(!InitObjectDB())
    {
        emit Print(" > Значение параметра не может быть записано\r\n", false);
        emit SendLastErrorDB("ОШИБКА ЗАПИСИ ПАРАМЕТРА");
        return false;
    }
    // 1.2 Поиск таблицы прибора
    if(!TestEqTables(command))
    {
        emit Print(" > Значение параметра не может быть записано\r\n", false);
        emit SendLastErrorDB("ОШИБКА ЗАПИСИ ПАРАМЕТРА");
        return false;
    }
    QString t = GetEquipmentTableName(command), a;
    QSqlQuery* mQuery = new QSqlQuery(ObjectDB);
    bool IsOk = false;
    bool lr = false;      // Указывает следует ли писать в данную строку или надо произвести запись в новую
    // Будет выбрана самая ранняя строка для записи текущих значений
    if(t.contains("current"))
    {
        mQuery->prepare("SELECT number, " + param.VarName + " FROM " + ObjectDB.databaseName() + "." + t +
                        " WHERE number =1;");
        if((mQuery->exec() == false)|(mQuery->size() <=0))
            IsOk = false;
        else
        {
            IsOk = true;
            lr = true;     // Запись будет проведена в данную строку
        }
    }
    else
    {
        // Будет выбрана самая первая пустая строка для записи статистики
        a = " WHERE " + param.VarName + " IS NULL ORDER BY number ASC LIMIT 1;";
        mQuery->prepare("SELECT number, " + param.VarName + " FROM " + ObjectDB.databaseName() + "." + t + a);
        // Будет выбрана самая последняя строка т.к. пустые строки не найдены
        if((mQuery->exec() == false)|(mQuery->size() <=0))
        {
            a = " ORDER BY number DESC LIMIT 1;";
            mQuery->prepare("SELECT number, " + param.VarName + " FROM " + ObjectDB.databaseName() + "." + t + a);
            if((mQuery->exec() == false)|(mQuery->size() <=0))
                IsOk = false;
            else
            {
                IsOk = true;
                lr = false;    // Запись будет произведена в данную строку т.к. она не заполнена
            }
        }
        else
        {
            IsOk = true;
            lr = true;
        }
    }
    // Если запрос не удался или не получено записей
    if(IsOk == false)
    {
        emit Print(" > Полученные данные не могут быть записаны\r\n", false);
        emit Print(" > Нет строки для записи в таблице прибора\r\n", false);
        emit SendLastErrorDB("ОШИБКА ЗАПИСИ ПАРАМЕТРА");
        delete mQuery;
        return false;
    }
    QSqlRecord rec = mQuery->record();
    mQuery->next();
    int n = mQuery->value(rec.indexOf("number")).toInt(&IsOk);
    if((!IsOk)|(n <=0))
    {
        emit Print(" > Полученные данные не могут быть записаны\r\n", false);
        emit Print(" > Нет строки для записи в таблице прибора\r\n", false);
        emit SendLastErrorDB("ОШИБКА ЗАПИСИ ПАРАМЕТРА");
        delete mQuery;
        return false;
    }
    delete mQuery;
    emit SendDeleteTextLastErrorDB("ОШИБКА ЗАПИСИ ПАРАМЕТРА");
    number = n;    // Номер строки
    lastrow = lr;  // Следует ли проводить запись в нее или нужно записать в новую
    return true;
}
// 22.12.2016
// Запись параметра в указанную строку
bool CAnswer::SaveVar(CVarDB param, QString data, CCommand command, int number, bool lastrow)
{
    QString t = GetEquipmentTableName(command);
    QSqlQuery* mQuery = new QSqlQuery(ObjectDB);
    QDateTime dt;
    pBusyDateTime->lock();
    dt.setDate(*pDate);
    dt.setTime(*pTime);
    pBusyDateTime->unlock();
    // 1. Формирование запроса
    // 1.А. Будет запись в указанную строку
    if(lastrow == true)
    {
        mQuery->prepare("UPDATE " + ObjectDB.databaseName() + "." + t +
                        " SET " + param.VarName + " = :A, " +
                        param.VarName + "_DateTime = :B WHERE number = :C;");
        mQuery->bindValue(":A", data);
        mQuery->bindValue(":B", dt);
        mQuery->bindValue(":C", number);
    }
    // 1.Б. Будет добавлена новая
    else
    {
        mQuery->prepare("INSERT INTO " + ObjectDB.databaseName() + "." + t +
                        " (" + param.VarName + ", " + param.VarName + "_DateTime) VALUES(:A, :B);");
        mQuery->bindValue(":A", data);
        mQuery->bindValue(":B", dt);
    }
    // 2. Выполнение запроса
    if(!mQuery->exec())
    {
        emit Print(" > Параметр не записан в БД - ошибка выполнения запроса\r\n", false);
        emit SendLastErrorDB("ОШИБКА ЗАПИСИ ПАРАМЕТРА");
        delete mQuery;
        return false;
    }
    delete mQuery;
    emit SendDeleteTextLastErrorDB("ОШИБКА ЗАПИСИ ПАРАМЕТРА");
    return true;
}
// 01.07.2017
// Проверка наличия флага сообщений
// Возвращает false - если функция не выполнена
//            true - если функция выполнена успешно
// Возвращает false - если флаг установлен т.е. уже есть наличие тревоги либо данные не получены
// Возвращает true - если флаг тревоги не установлен, либо записи не найдены
// NameParam = EqName_EqNumber_VarName
bool CAnswer::IsNotFlagSetted(QString ParamName, bool& Flag)
{
    if(!SysDB.open())
    {
        qDebug() << " > CAnswer:IsNotFlagSetted: База данных не открыта, сообщение выводиться не будет:" << SysDB.lastError().text();
        emit Print(" > Проверка установки флага тревоги параметра \"" + ParamName + "\" не выполнена\r\n", false);
        emit Print(" > Системная база данных не открыта\r\n", false);
        return false;
    }
    QSqlQuery* mQuery = new QSqlQuery(SysDB);
    mQuery->prepare("SELECT AlarmSetted FROM " + DBName + ".TAlarms WHERE Parameter = :A AND ObjectID = :B ORDER BY AlarmTime DESC LIMIT 1;");
    mQuery->bindValue(":A", ParamName);
    mQuery->bindValue(":B", ObjectID);
    if(!mQuery->exec())
    {
        qDebug() << " > CAnswer:IsNotFlagSetted: Запрос не выполнен, записей не обнаружено:" << mQuery->lastError().text();
        emit Print(" > Проверка установки флага тревоги параметра \"" + ParamName + "\" не выполнена\r\n", false);
        emit Print(" > Ошибка выполнения запроса: \"" + mQuery->lastError().text() + "\"\r\n", false);
        emit SendLastErrorDB("ОШИБКА ЧТЕНИЯ ТРЕВОГ");
        if(mQuery->lastError().text().contains("doesn't exist",Qt::CaseInsensitive)==true)
            CreateTAlarm(mQuery);
        delete mQuery;
        return false; // Функция не выполнена
    }
    emit SendDeleteTextLastErrorDB("ОШИБКА ЧТЕНИЯ ТРЕВОГ");
    // Проверка флага
    int Count = mQuery->size();
    if(Count <= 0) // Записей нет по данному параметру
    {
        qDebug() << " > CAnswer:IsNotFlagSetted: Записей по данному параметру не найдено";
        delete mQuery;
        Flag = true;   // Будет произведена запись тревоги
        return true;
    }
    QSqlRecord rec = mQuery->record();
    mQuery->next();
    quint8 Fl = mQuery->value(rec.indexOf("AlarmSetted")).toUInt();
    delete mQuery;
    if(Fl ==1)Flag =false;    // Флаг уже установлен, значит сообщение уже выводилось
    else
        Flag = true;          // Флаг сброшен, значит будет произведена запись
    return true;
}
// 02.01.2017
// Сброс флага сообщений параметра, если установлен
bool CAnswer::FlagSkip(QString ParamName)
{
    if(!SysDB.open())
    {
        qDebug() << " > База данных не открыта, сброс флага сообщений производиться не будет: " << SysDB.lastError().text();
        emit Print(" > Сброс флага тревоги параметра \"" + ParamName + "\" не выполнен\r\n", false);
        emit Print(" > Системная база данных не открыта\r\n", false);
        return false;
    }
    // Будут сброшены только флаги где дата и время раньше чем у данного сообщения
    QDateTime dt;
    pBusyDateTime->lock();
    dt.setDate(*pDate);
    dt.setTime(*pTime);
    pBusyDateTime->unlock();
    QSqlQuery* mQuery = new QSqlQuery(SysDB);
    // От 01.06.2015 (проверка наличия флагов не производится т.к. нет смысла делать лишний запрос на SELECT)
    // Сначала обновление только по дате и времени
    mQuery->prepare("UPDATE " + DBName + ".TAlarms SET AlarmSetted =0 WHERE Parameter = :A AND ObjectID = :B AND AlarmTime <= :C AND number >0;");
    mQuery->bindValue(":A", ParamName);
    mQuery->bindValue(":B", ObjectID);
    mQuery->bindValue(":C", dt);
    if(!mQuery->exec())
    {
        qDebug() << " > CAnswer:FlagSkip: Запрос сброса флагов параметра не выполнен: " << mQuery->lastError().text();
        emit Print(" > Сброс флага тревоги параметра \"" + ParamName + "\" не выполнен\r\n", false);
        emit Print(" > Ошибка выполнения запроса: \"" + mQuery->lastError().text() + "\"\r\n", false);
        if(mQuery->lastError().text().contains("doesn't exist",Qt::CaseInsensitive)==true)
            CreateTAlarm(mQuery);
        delete mQuery;
        return false;
    }
    delete mQuery;
    qDebug() << " > CAnswer:FlagSkip: Флаги параметра которые были раньше полученной даты и времени сброшены";
    return true; // Флаг был установлен, теперь сброшен
}
// 03.02.2017
// Запись аварии в системный журнал аварийных сообщений
bool CAnswer::SaveAlarm(QString ParamName, QString ConvertedVar, QStringList AlarmText)
{
    if(!SysDB.open())
    {
        qDebug() << " > База данных не открыта, запись аварии не произведена: " << SysDB.lastError().text();
        emit Print(" > Запись тревоги параметра \"" + ParamName + "\" не выполнена\r\n", false);
        emit Print(" > Системная база данных не открыта\r\n", false);
        return false;
    }
    // Будут сброшены только флаги где дата и время раньше чем у данного сообщения
    QDateTime dt;
    pBusyDateTime->lock();
    dt.setDate(*pDate);
    dt.setTime(*pTime);
    pBusyDateTime->unlock();
    QString AText = AlarmText.join("/");
    QSqlQuery* mQuery = new QSqlQuery(SysDB);
    mQuery->prepare("INSERT INTO " + DBName + ".TAlarms (ObjectID, AlarmText, AlarmTime, Parameter, CurrValue, "
                    "AlarmSetted) VALUES(:A, :B, :C, :D, :E, :F);");
    mQuery->bindValue(":A", ObjectID);
    mQuery->bindValue(":B", AText);
    mQuery->bindValue(":C", dt);
    mQuery->bindValue(":D", ParamName);
    mQuery->bindValue(":E", ConvertedVar);
    mQuery->bindValue(":F", 1);
    if(!mQuery->exec())
    {
        qDebug() << " > CAnswer:SaveAlarm: Запрос записи тревоги не был выполнен :" << mQuery->lastError().text();
        emit Print(" > Запись тревоги параметра \"" + ParamName + "\" не выполнена\r\n", false);
        emit Print(" > Ошибка выполнения запроса: \"" + mQuery->lastError().text() + "\"\r\n", false);
        emit SendLastErrorDB("ОШИБКА ЗАПИСИ ТРЕВОГИ");
        if(mQuery->lastError().text().contains("doesn't exist",Qt::CaseInsensitive)==true)
            CreateTAlarm(mQuery);
        delete mQuery;
        return false;
    }
    emit SendDeleteTextLastErrorDB("ОШИБКА ЗАПИСИ ТРЕВОГИ");
    qDebug() << " > CAnswer:SaveAlarm: Запись тревоги выполнена (неподтвержденная запись)";
    delete mQuery;
    return true;
}
void CAnswer::CreateTAlarm(QSqlQuery* mQuery)
{
    emit Print(" > Попытка создать таблицу тревог:\r\n", false);
    mQuery->prepare("CREATE TABLE IF NOT EXISTS "+ DBName + ".TAlarms (number INTEGER PRIMARY KEY AUTO_INCREMENT,"
                    " ObjectID INT UNSIGNED, "
                    " AlarmText VARCHAR(1000), "
                    " AlarmTime DATETIME DEFAULT NOW(), "
                    " Parameter VARCHAR(100), "
                    " CurrValue VARCHAR(100), "
                    " AlarmSetted SMALLINT UNSIGNED DEFAULT 1, "
                    " ReadTime DATETIME, "
                    " UserName VARCHAR(100), "
                    " UserLastname VARCHAR(100), "
                    " UserPatronymic VARCHAR(100), "
                    " UserJobtitle VARCHAR(100), "
                    " UserAction BLOB, "
                    " UserLogin VARCHAR(100));");
    if(mQuery->exec())
        emit Print(" > Успешно\r\n", false);
    else
        emit Print(" > Ошибка\r\n", false);
}
// 25.05.2017 Запись кода завершения ПЛК-команды в TCommandStat
bool CAnswer::SaveCode(CCommand& command, QString error)
{
    if(!SysDB.open())
    {
        qDebug() << " > База данных не открыта, код ошибки команды записан не будет : " << SysDB.lastError().text();
        emit Print(" > Код ошибки команды CID:" + QString::number(command.DBData.CID) + " не записан\r\n", false);
        emit Print(" > Системная база данных не открыта\r\n", false);
        return false;
    }
    QSqlQuery* mQuery = new QSqlQuery(SysDB);
    mQuery->prepare("UPDATE " + DBName + ".TCommandStat SET ErrorCode =:A WHERE ObjectID =:B AND CID =:C AND number >0;");
    mQuery->bindValue(":A", error);
    mQuery->bindValue(":B", ObjectID);
    mQuery->bindValue(":C", command.DBData.CID);
    if(!mQuery->exec())
    {
        qDebug() << " > CAnswer:SaveCode: Запрос записи кода ошибки не был выполнен :" << mQuery->lastError().text();
        emit Print(" > Запись кода ошибки команды CID:" + QString::number(command.DBData.CID) + " не выполнена\r\n", false);
        emit Print(" > Ошибка выполнения запроса: \"" + mQuery->lastError().text() + "\"\r\n", false);
        emit SendLastErrorDB("ОШИБКА ЗАПИСИ КОДА ОШИБКИ");
        if(mQuery->lastError().text().contains("doesn't exist",Qt::CaseInsensitive)==true)
        delete mQuery;
        return false;
    }
    emit SendDeleteTextLastErrorDB("ОШИБКА ЗАПИСИ КОДА ОШИБКИ");
    qDebug() << " > CAnswer:SaveCode: Запись кода ошибки выполнена";
    delete mQuery;
    return true;
}
