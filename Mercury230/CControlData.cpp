#include "CControlData.h"
// Конструктор
CControlData::CControlData(/*QObject* parent): QObject(parent*/)
{
    // Данные управления потоком
    ObjectDelay = 10000;                // Задержка обновления данных команд, статуса, режима по умолчанию
    TerminalDelay = 2000;               // Максимально время ожидания ответа в режиме "терминал" по умолчанию
    FlagRequest = false;                // Флаг разрешения опроса в режиме "запрос" сброшен
    CurrentMode = "";                   // Режим работы потока опроса по умолчанию неизвестен (не на что не влияет)
    UserCommand = "";                   // Пользовательская команда неизвестна (не на что не влияет)
    Equipment.clear();                  // Перечень приборов неизвестен (не на что не влияет)
    TimeTable.clear();                  // Расписание
    TerminalCommand = "";               // Терминальная команда (не на что не вилияет)
    ObjectDB = "";                      // БД для записи данных полученных от приборов неизвестна
    RequestCommands.clear();            // Тип команд опроса неизвестен
}
// Копирование всех данных в объект (следует соблюдать безопасность и устанавливать мьютекс до запуска функции)
void CControlData::CopyControlData(CControlData* data)
{
    // Копирование всех данных
    ObjectDelay = data->ObjectDelay;
    TerminalDelay = data->TerminalDelay;
    FlagRequest = data->FlagRequest;
    CurrentMode = data->CurrentMode;
    UserCommand = data->UserCommand;
    Equipment = data->Equipment;
    TimeTable = data->TimeTable;
    ObjectDB = data->ObjectDB;
    RequestCommands = data->RequestCommands;
    TerminalCommand = data->TerminalCommand;
}
// Проверка корректности данных
// 28.09.2016 Исправлена ошибка мьютекса
bool CControlData::CorrectData(int year)
{
    quint32 delay = ObjectDelay;
    // Если задержки нет или более 10 минут, тогда ошибка данных
    if((delay < 100)|(delay > 600000))
        return false;
    delay = TerminalDelay;
    // Если задержки нет или более 1 минуты, тогда ошибка данных
    if((delay < 100)|(delay > 60000))
        return false;
    QString str =CurrentMode;
    // Если режим не получен
    if((str != "WHILE")&(str != "TIME")&(str != "TERMINAL")&(str != "REQUEST"))
        return false;
    str =UserCommand;
    // Если пользовательская команда не получена
    if((str != "EDIT")&(str != "EDIT+OK")&(str != "DEL")&(str != "DEL+OK")&(str != "STOP")&(str != "STOP+OK")
            &(str != "START")&(str != "START+OK")&(str != "USTOP")&(str != "USTOP+OK"))
        return false;
// 07.01.2017 Неверное расписание влияет только на режим работы по расписанию
    if(CurrentMode == "TIME")
        return TestTimeTable(year);
    return true;
}
// Тест расписания
bool CControlData::TestTimeTable(int year)
{
    // Проверка расписания, если разобрать данные не получается, то выход с флагом ошибки
    for(int i=0; i < TimeTable.size(); i++)
    {
        QString str = TimeTable.at(i);
        // Формат данных следующий: номер/месяц/неделя/день/время начала/время конца
        QStringList t = str.split("/");
        // Записей не 6
        if(t.size() != 6)
            return false;
        bool IsOk = false;
        // Переменные начала и конца расписания
        qint16 startMonth =0, startDay =0, stopMonth =0, stopDay =0;
        // 1. Проверка месяца начала
        startMonth = t.at(0).toInt(&IsOk);
        // Если ошибка данных или номер месяца некорректен, то выход с ошибкой
        if((IsOk == false)|(startMonth >12)|(startMonth <0))
            return false;
        // Если месяц любой, то минимальный
        if(startMonth ==0)
            startMonth =1;
        // 2. Проверка месяца окончания
        stopMonth = t.at(3).toInt(&IsOk);
        // Если ошибка данных или номер месяца некорректен, то выход с ошибкой
        if((IsOk == false)|(stopMonth >12)|(stopMonth <0))
            return false;
        // Если месяц любой то максимальный
        if(stopMonth ==0)
            stopMonth =12;
        // 3. Проверка дня начала
        startDay = t.at(1).toUInt(&IsOk);
        // Если ошибка данных или номер дня некорректен, то выход с ошибкой
        if((IsOk == false)|(startDay >31)|(startDay <0))
            return false;
        // Если день любой, то минимальный
        if(startDay ==0)
            startDay =1;
        // 4. Проверка дня окончания
        stopDay = t.at(4).toUInt(&IsOk);
        // Если ошибка данных или номер дня некорректен, то выход с ошибкой
        if((IsOk == false)|(stopDay >31)|(stopDay <0))
            return false;
        // Если день любой, то максимальный
        if(stopDay ==0)
        {
            QDate d;
            d.setDate(year, stopMonth, 1);
            stopDay =d.daysInMonth();
            if(stopDay ==0)
                return false;
        }
        // 5. Проверка времени начала
        QTime startTime = QTime::fromString(t.at(2), "hh:mm:ss");
        if(!startTime.isValid())
            return false;
        // 6. Проверка времени окончания
        QTime stopTime = QTime::fromString(t.at(5), "hh:mm:ss");
        if(!stopTime.isValid())
            return false;
        // 7. Проверка даты начала
        QDate startDate;
        startDate.setDate(year, startMonth, startDay);
        if(!startDate.isValid())
            return false;
        // 8. Проверка даты окончания
        QDate stopDate;
        stopDate.setDate(year, stopMonth, stopDay);
        if(!stopDate.isValid())
            return false;
        // 9. Сверка начала и окончания
        QDateTime startDateTime, stopDateTime;
        startDateTime.setDate(startDate);
        startDateTime.setTime(startTime);
        stopDateTime.setDate(stopDate);
        stopDateTime.setTime(stopTime);
        if(startDateTime >= stopDateTime)
            return false;
    }
    return true;
}
// Получение расписания
bool CControlData::GetTimeTable(QDateTime* startDT, QDateTime* stopDT,
                                QDateTime* currDT, int index)
{
    // Проверка индекса
    int sz = TimeTable.size();
    // Если индекс в интервале записей, то следует
    if(!((sz > 0)&(index >=0)&(index < sz)))
        return false;
    // Проверка расписания, если разобрать данные не получается, то выход с флагом ошибки
    QString str = TimeTable.at(index);
    // Формат данных следующий: номер/месяц/неделя/день/время начала/время конца
    QStringList t = str.split("/");
    // Записей не 6
    if(t.size() != 6)
        return false;
    bool IsOk = false;
    // Переменные начала и конца расписания
    qint16 startMonth =0, startDay =0, stopMonth =0, stopDay =0;
    // 1. Проверка месяца начала
    startMonth = t.at(0).toInt(&IsOk);
    // Если ошибка данных или номер месяца некорректен, то выход с ошибкой
    if((IsOk == false)|(startMonth >12)|(startMonth <0))
        return false;
    // Если месяц любой, то минимальный
    if(startMonth ==0)
        startMonth =1;
    // 2. Проверка месяца окончания
    stopMonth = t.at(3).toInt(&IsOk);
    // Если ошибка данных или номер месяца некорректен, то выход с ошибкой
    if((IsOk == false)|(stopMonth >12)|(stopMonth <0))
        return false;
    // Если месяц любой то максимальный
    if(stopMonth ==0)
        stopMonth =12;
    // 3. Проверка дня начала
    startDay = t.at(1).toUInt(&IsOk);
    // Если ошибка данных или номер дня некорректен, то выход с ошибкой
    if((IsOk == false)|(startDay >31)|(startDay <0))
        return false;
    // Если день любой, то минимальный
    if(startDay ==0)
        startDay =1;
    // 4. Проверка дня окончания
    stopDay = t.at(4).toUInt(&IsOk);
    // Если ошибка данных или номер дня некорректен, то выход с ошибкой
    if((IsOk == false)|(stopDay >31)|(stopDay <0))
        return false;
    // Если день любой, то максимальный
    if(stopDay ==0)
    {
        QDate d;
        d.setDate(currDT->date().year(), stopMonth, 1);
        stopDay =d.daysInMonth();
        if(stopDay ==0)
            return false;
    }
    // 5. Проверка времени начала
    QTime startTime = QTime::fromString(t.at(2), "hh:mm:ss");
    if(!startTime.isValid())
        return false;
    // 6. Проверка времени окончания
    QTime stopTime = QTime::fromString(t.at(5), "hh:mm:ss");
    if(!stopTime.isValid())
        return false;
    // 7. Проверка даты начала
    QDate startDate;
    startDate.setDate(currDT->date().year(), startMonth, startDay);
    if(!startDate.isValid())
        return false;
    // 8. Проверка даты окончания
    QDate stopDate;
    stopDate.setDate(currDT->date().year(), stopMonth, stopDay);
    if(!stopDate.isValid())
        return false;
    // 9. Сверка начала и окончания
    QDateTime startDateTime, stopDateTime;
    startDateTime.setDate(startDate);
    startDateTime.setTime(startTime);
    stopDateTime.setDate(stopDate);
    stopDateTime.setTime(stopTime);
    if(startDateTime >= stopDateTime)
        return false;
    // Данные корректны
    *startDT = startDateTime;
    *stopDT = stopDateTime;
    return true;
}
