#include "CCommand.h"
#include "General.h"

// Инициализация объекта
bool CCommand::initCommand(quint32 ID, QString RequestType, QSqlQuery* mQuery)
{
    bool IsOk = true;
    // Выборка и проверка данных
    QSqlRecord rec = mQuery->record();

    // 1. Идентификатор объекта
    quint32 oID = mQuery->value(rec.indexOf("ObjectID")).toUInt();
    if(oID!= ID)
    {
        qDebug() << "CCommand::initCommand: Идентификатор объекта полученной команды не совпал с проверяемым\r\n"
                    "Идентификатор объекта: " + QString::number(ID) + " Идентификатор полученный: " + QString::number(oID);
        return false;
    }
    DBData.ObjectID = oID;
    if(DBData.ObjectID <=1)return false;

    // 2. Тип запроса
    QString rType = mQuery->value(rec.indexOf("RequestType")).toString();
    if(rType!= RequestType)
    {
        qDebug() << "CCommand::initCommand: Тип запроса не совпал с проверяемым\r\n"
                    "Тип запроса: " + RequestType + " Тип запроса полученный: " + rType;
        return false;
    }
    DBData.RequestType = rType;

    // 3. Тип команды
    QString TypeCommand = mQuery->value(rec.indexOf("TypeCommand")).toString();
    if(!IsTypeCommand(TypeCommand))
    {
        qDebug() << "CCommand::initCommand: Неверное значение TypeCommand получено из БД\r\n";
        return false;
    }
    DBData.TypeCommand = TypeCommand;

    // 4. Идентификатор команды
    quint32 CID = mQuery->value(rec.indexOf("CID")).toUInt(&IsOk);
    if(!IsOk)
    {
        qDebug() << "CCommand::initCommand: Неверное значение CID получено из БД\r\n";
        return false;
    }
    // Если идентификатор нулевой и тип команды не терминальный, на выход
    if((CID == 0)&(DBData.TypeCommand != "TERMINAL(Y)")&(DBData.TypeCommand != "TERMINAL(N)"))
    {
        qDebug() << "CCommand::initCommand: Неверное значение CID получено из БД\r\n";
        return false;
    }
    // Если тип команды терминальный, тогда идентификатор команды нулевой
    if((DBData.TypeCommand == "TERMINAL(Y)")|(DBData.TypeCommand == "TERMINAL(N)"))
        DBData.CID =0;
    else
        DBData.CID = CID;

    // 5. Максимальное время ожидания ответа от оборудования после чего будет изменен статус на "TIMEOUT"
    quint32 Delay = mQuery->value(rec.indexOf("Delay")).toUInt(&IsOk);
    if((!IsOk)|(Delay < 100)|(Delay > 300000))
    {
        qDebug() << "CCommand::initCommand: Неверное значение Delay получено из БД\r\n";
        return false;
    }
    DBData.Delay = Delay;

    // 6. Данные настроек порта
    quint8 Funct = mQuery->value(rec.indexOf("Funct")).toUInt(&IsOk);
    if(!IsOk)
    {
        qDebug() << "CCommand::initCommand: Неверное значение Funct получено из БД\r\n";
        if((TypeCommand!="TERMINAL(N)")&(TypeCommand!="STRIGHT"))
            return false;
    }
    DBData.Funct = (unsigned char)Funct;
    quint8 PortNum = mQuery->value(rec.indexOf("PortNum")).toUInt(&IsOk);
    if(!IsOk)
    {
        qDebug() << "CCommand::initCommand: Неверное значение PortNum получено из БД\r\n";
        if((TypeCommand!="TERMINAL(N)")&(TypeCommand!="STRIGHT"))
            return false;
    }
    DBData.PortNum = (unsigned char)PortNum;
    quint8 DataCom = mQuery->value(rec.indexOf("DataCom")).toUInt(&IsOk);
    if((!IsOk)|(DataCom <6)|(DataCom >8))
    {
        qDebug() << "CCommand::initCommand: Неверное значение DataCom получено из БД\r\n";
        if((TypeCommand!="TERMINAL(N)")&(TypeCommand!="STRIGHT"))
            return false;
    }
    DBData.DataCom = (unsigned char)DataCom;
    quint8 StopBits = mQuery->value(rec.indexOf("StopBits")).toUInt(&IsOk);
    if((!IsOk)|(StopBits <1)|(StopBits >2))
    {
        qDebug() << "CCommand::initCommand: Неверное значение StopBits получено из БД\r\n";
        if((TypeCommand!="TERMINAL(N)")&(TypeCommand!="STRIGHT"))
            return false;
    }
    DBData.StopBits = (unsigned char)StopBits;
    quint8 ParityBits = mQuery->value(rec.indexOf("ParityBits")).toUInt(&IsOk);
    if((!IsOk)|(ParityBits <0)|(ParityBits >4))
    {
        qDebug() << "CCommand::initCommand: Неверное значение ParityBits получено из БД\r\n";
        if((TypeCommand!="TERMINAL(N)")&(TypeCommand!="STRIGHT"))
            return false;
    }
    DBData.ParityBits = (unsigned char)ParityBits;
    quint32 SpeedCom = mQuery->value(rec.indexOf("SpeedCom")).toUInt(&IsOk);
    if(!IsOk)
    {
        qDebug() << "CCommand::initCommand: Неверное значение SpeedCom получено из БД\r\n";
        if((TypeCommand!="TERMINAL(N)")&(TypeCommand!="STRIGHT"))
            return false;
    }
    DBData.SpeedCom = SpeedCom;

    // 7. Получение строки команды
    if(!mQuery->value(rec.indexOf("Command")).canConvert<QByteArray>())
    {
        qDebug() << "CCommand::initCommand: Неверное значение Command получено из БД\r\n";
        return false;
    }
    QByteArray Command = mQuery->value(rec.indexOf("Command")).toByteArray();
    if(Command.isEmpty())
    {
        qDebug() << "CCommand::initCommand: Пустое значение Command получено из БД\r\n";
        return false;
    }
    DBData.Command = Command;

    // Если команда терминальная, то дальше проверять не стоит
    if((TypeCommand =="TERMINAL(Y)")|(TypeCommand =="TERMINAL(N)"))
        return true;

    // 8. Наименование оборудования
    QString EqName = mQuery->value(rec.indexOf("EqName")).toString();
    DBData.EqName = EqName;
    if(DBData.EqName.isEmpty())return false;

    // 9. Номер оборудования
    QString EqNumber = mQuery->value(rec.indexOf("EqNumber")).toString();
    DBData.EqNumber = EqNumber;
    if(DBData.EqNumber.isEmpty())return false;

    // 10. Тип номера оборудования
    QString EqNumberType = mQuery->value(rec.indexOf("EqNumberType")).toString();
    DBData.EqNumberType = EqNumberType;
    if((DBData.EqNumberType!="quint8")&
       (DBData.EqNumberType!="quint16")&
       (DBData.EqNumberType!="quint32")&
       (DBData.EqNumberType!="quint64")&
       (DBData.EqNumberType!="QString"))return false;

    // 11. Число попыток при неудачных ответах либо молчании оборудования
    quint32 Attempts = mQuery->value(rec.indexOf("Attempts")).toUInt(&IsOk);
    if(!IsOk)
    {
        qDebug() << "CCommand::initCommand: Неверное значение Attempts получено из БД\r\n";
        return false;
    }
    DBData.Attempts = Attempts;

    // 12. Сколько раз требуется отправить команду оборудованию до ее удаления из БД
    quint32 CountsCommand = mQuery->value(rec.indexOf("CountsCommand")).toUInt(&IsOk);
    if(!IsOk)
    {
        qDebug() << "CCommand::initCommand: Неверное значение CountsCommand получено из БД\r\n";
        return false;
    }
    DBData.CountCommand = CountsCommand;

    // 13. Приоритет команды
    quint8 Priority = mQuery->value(rec.indexOf("Priority")).toUInt(&IsOk);
    if(!IsOk)
    {
        qDebug() << "CCommand::initCommand: Неверное значение Priority получено из БД\r\n";
        return false;
    }
    DBData.Priority = Priority;

    // 14. Получение перечня параметров
    if(!mQuery->value(rec.indexOf("Parameters")).canConvert<QByteArray>())
    {
        qDebug() << "CCommand::initCommand: Неверное значение Parameters получено из БД\r\n";
        return false;
    }
    QByteArray Params = mQuery->value(rec.indexOf("Parameters")).toByteArray();
    QList<CVarDB> Parameters;
    Parameters.clear();
    if(!Params.isEmpty())
        Parameters = deserializec(Params);
    if(Parameters.isEmpty())
    {
        qDebug() << "CCommand::initCommand: Неверное значение Parameters получено из БД\r\n";
        return false;
    }
    DBData.Params = Parameters;
    // Проверка перечня параметров
    if(!initParameters())
    {
        qDebug() << "Тест переменной не пройден";
        return false;
    }
    qDebug() << "Тест переменной пройден";
    return true;
}
// Проверка типа команды
bool CCommand::IsTypeCommand(QString TypeCommand)
{
    // Если тип команды не равен ни одному из предопределенных
    if((TypeCommand!="PLC")&(TypeCommand!="STRIGHT")&(TypeCommand!="TERMINAL(Y)")&(TypeCommand!="TERMINAL(N)"))
    {
        int index = TypeCommand.indexOf("DRIVER(", 0);
        if(index < 0)
            return false;
        index = TypeCommand.indexOf(")", index);
        if(index < 0)
            return false;
    }
    return true;
}
// Проверка настроек параметров
bool CCommand::initParameters()
{
    for(int i=0; i < DBData.Params.size(); i++)
    {
        // Если индекс переменной неверный
        if(DBData.Params.at(i).VarIndex!= (quint32)i)
            return false;
        // Если разрешение типа записи, то проверяется только имя и индекс т.к. в БД настройки данной переменной
        // уже записаны в таблице прибора CURRENT или STATISTIC
        if(DBData.Params.at(i).VarPermit == "WRITE")
        {
            //Имя переменной в любом случае не может быть пустым
            if(DBData.Params.at(i).VarName.isEmpty())
                return false;
        }
        // Любые другие разрешения проверяются полностью
        else
        {
            if(!TestParameterSetup(DBData.Params.at(i)))
                return false;
        }
    }
    return true;
}
// Проверка порядка вставки байтов пакета в переменную 1,2,3,4,5,...I
bool CCommand::TestVarInsert(QList<QChar> list, QString str)
{
    int lenght = str.size();
    bool num =true;
    if(lenght < ((list.size()*2)-1))
        return false;
    for(int j=0; j < lenght; j++)
    {
        if(num ==true)
        {
            num =false;
            int listsize = list.size();
            for(int a=0; a < list.size(); a++)
            {
                if(list.at(a) == str.at(j))
                {
                    list.removeAt(a);
                    break;
                }
            }
            if(listsize == list.size())
                return false;
        }
        else
        {
            if(str.at(j)!=',')
                return false;
            num =true;
        }
    }
    return true;
}
// Тестирование значений параметра занесенного в текстовую строку по его типу обозначенному в переменной типа
// Переменные типа MOD2, CRC16, CRC32 должны иметь символ "-" в параметрах, QString - любые значения
bool CCommand::TestVar(QString VarType, QString Var)
{
    bool IsOk = true;
    // Проверка сразу всех целочисленных типов
    if((VarType =="quint8")|(VarType =="quint16")|(VarType =="quint32")|(VarType =="quint64")|
       (VarType =="bit"))
    {
        Var.toULongLong(&IsOk);
        if(!IsOk)
            return false;
    }
    else if((VarType =="qint8")|(VarType =="qint16")|(VarType =="qint32")|(VarType =="qint64"))
    {
        Var.toLongLong(&IsOk);
        if(!IsOk)
            return false;
    }
    else if((VarType =="float")|(VarType == "double"))
    {
        Var.toDouble(&IsOk);
        if(!IsOk)
            return false;
    }
    else if((VarType =="CRC16")|(VarType == "CRC32")|(VarType =="MOD2"))
    {
        if(Var!="-")
            return false;
    }
    else if((VarType != "QString"))
        return false;
    return true;
}
bool CCommand::TestVar(QString VarType, QByteArray Var)
{
    bool IsOk = true;
    // Проверка сразу всех целочисленных типов
    if((VarType =="quint8")|(VarType =="quint16")|(VarType =="quint32")|(VarType =="quint64")|
       (VarType =="bit"))
    {
        Var.toULongLong(&IsOk);
        if(!IsOk)
            return false;
    }
    if((VarType =="qint8")|(VarType =="qint16")|(VarType =="qint32")|(VarType =="qint64"))
    {
        Var.toLongLong(&IsOk);
        if(!IsOk)
            return false;
    }
    else if((VarType =="float")|(VarType == "double"))
    {
        Var.toDouble(&IsOk);
        if(!IsOk)
            return false;
    }
    else if((VarType =="CRC16")|(VarType == "CRC32")|(VarType =="MOD2"))
    {
        if(Var!="-")
            return false;
    }
    else if((VarType != "QString"))
        return false;
    return true;
}
// Тест нескольких переменных разделенных "/"
bool CCommand::TestVarAlarm(QString VarType, QString Var)
{
    QStringList list;
    list = Var.split("/");
    for(int i=0; i < list.size(); i++)
    {
        if(!TestVar(VarType, list.at(i)))
            return false;
    }
    return true;
}
// Тест одной переменной
bool CCommand::TestParameterSetup(CVarDB var)
{
    // Имя переменной в любом случае не может быть пустым
    if(var.VarName.isEmpty())
        return false;
    // Разрешения могут быть только предопределенные
    if((var.VarPermit!="READ")&
       (var.VarPermit!="CREATE")&
       (var.VarPermit!="WRITE")&
       (var.VarPermit!="COMMAND"))
        return false;
    // Тип переменной к которому приводится значение ответа должен быть предопределенный
    if((var.VarType!="quint8")&
       (var.VarType!="quint16")&
       (var.VarType!="quint32")&
       (var.VarType!="quint64")&
       (var.VarType!="qint8")&
       (var.VarType!="qint16")&
       (var.VarType!="qint32")&
       (var.VarType!="qint64")&
       (var.VarType!="float")&
       (var.VarType!="double")&
       (var.VarType!="CRC16")&
       (var.VarType!="CRC32")&
       (var.VarType!="MOD2")&
       (var.VarType!="bit")&
       (var.VarType!="QString"))
        return false;
    // ----
    if((var.VarTypeSave!="quint8")&
       (var.VarTypeSave!="quint16")&
       (var.VarTypeSave!="quint32")&
       (var.VarTypeSave!="quint64")&
       (var.VarTypeSave!="qint8")&
       (var.VarTypeSave!="qint16")&
       (var.VarTypeSave!="qint32")&
       (var.VarTypeSave!="qint64")&
       (var.VarTypeSave!="float")&
       (var.VarTypeSave!="double")&
       (var.VarTypeSave!="CRC16")&
       (var.VarTypeSave!="CRC32")&
       (var.VarTypeSave!="MOD2")&
       (var.VarTypeSave!="bit")&
       (var.VarTypeSave!="QString"))
        return false;
    //quint32 VarOffset;     // Сдвиг от начала во входном массиве (может быть абсолютно любой)
    // Количество считываемых байт из входного массива должен соответствовать типу данных
    if(var.VarType == "bit")
    {
        if((var.VarData <1)|(var.VarData >8))
            return false;
    }
    else if(var.VarData ==1)
    {
        // Должен тип быть quint8 или qint8 или QString или MOD2
        if((var.VarType!="quint8")&
           (var.VarType!="qint8")&
           (var.VarType!="MOD2")&
           (var.VarType!="QString"))
            return false;
    }
    else if(var.VarData ==2)
    {
        // Должен тип быть quint16 или qint16 или QString или CRC16
        if((var.VarType!="quint16")&
           (var.VarType!="qint16")&
           (var.VarType!="CRC16")&
           (var.VarType!="QString"))
            return false;
    }
    else if(var.VarData ==4)
    {
        // Должен тип быть quint32 или qint32 или QString или float или CRC32
        if((var.VarType!="quint32")&
           (var.VarType!="qint32")&
           (var.VarType!="float")&
           (var.VarType!="CRC32")&
           (var.VarType!="QString"))
            return false;
    }
    else if(var.VarData ==8)
    {
        // Должен тип быть quint64 или qint64 или QString или double
        if((var.VarType!="quint64")&
           (var.VarType!="qint64")&
           (var.VarType!="double")&
           (var.VarType!="QString"))
            return false;
    }
    else
    {
        // Произвольное количество или ноль - только строковый тип
        if(var.VarType!="QString")
            return false;
    }
    // Последовательность вставки байтов в переменную VarType
    if(var.VarType =="QString")
    {
        if(var.VarInsert!="-")
            return false;
    }
    else if((var.VarType=="quint8")|(var.VarType=="qint8")|(var.VarType=="MOD2")|(var.VarType == "bit"))
    {
        if(var.VarInsert!="1")
            return false;
    }
    else if((var.VarType=="quint16")|(var.VarType=="qint16")|(var.VarType=="CRC16"))
    {
        if((var.VarInsert!="1,2")&(var.VarInsert!="2,1"))
            return false;
    }
    else if((var.VarType=="quint32")|(var.VarType=="qint32")|(var.VarType=="float")|(var.VarType=="CRC32"))
    {
        QList<QChar> list;
        list << '1' << '2' << '3' << '4';
        if(!TestVarInsert(list, var.VarInsert))
            return false;
    }
    else if((var.VarType=="quint64")|(var.VarType=="qint64")|(var.VarType=="double"))
    {
        QList<QChar> list;
        list << '1' << '2' << '3' << '4' << '5' << '6' << '7' << '8';
        if(!TestVarInsert(list, var.VarInsert))
            return false;
    }
    // Минимальное значение датчика типа VarTypeSave должно преобразовываться
    if(!TestVar(var.VarTypeSave, var.VarSensor_MIN))
        return false;
    // Максимальное значение датчика типа VarTypeSave должно преобразовываться
    if(!TestVar(var.VarTypeSave, var.VarSensor_MAX))
        return false;
    // Минимальное значение параметра типа VarTypeSave должно преобразовываться
    if(!TestVar(var.VarTypeSave, var.VarParam_MIN))
        return false;
    // Максимальное значение параметра типа VarTypeSave должно преобразовываться
    if(!TestVar(var.VarTypeSave, var.VarParam_MAX))
        return false;
    // Минимальный порог срабатывания типа VarTypeSave разделенный "/" при нескольких значениях
    if(!TestVarAlarm(var.VarTypeSave, var.VarBorder_MIN))
        return false;
    // Максимальный порог срабатывания типа VarTypeSave разделенный "/" при нескольких значениях
    if(!TestVarAlarm(var.VarTypeSave, var.VarBorder_MAX))
        return false;
    // Флаг включения сигнализации
    if((var.AlarmSet!=0)&(var.AlarmSet!=1))
        return false;
    //QString AlarmMin;      // Текст сообщения при тревоги по низкому уровню разделенный "/" при нескольких значениях
    //QString AlarmMax;      // Текст сообщения при тревоги по высокому уровню разделенный "/" при нескольких значениях
    // Включение отправки СМС-при аварийном состоянии параметра
    if((var.SMSSet!=0)&(var.SMSSet!=1))
        return false;
    // Номера телефонов на которые следует отправлять СМС в формате /*/*/12/12... - где первая * - (1 - отправлять, 0 - нет)
    //отправка на номера ответственных, вторая на номера операторов, дальше 12-значные номера дополнительных телефонов
    if((!var.Telephones.contains("/0/0"))&
        (!var.Telephones.contains("/1/0"))&
        (!var.Telephones.contains("/0/1"))&
        (!var.Telephones.contains("/1/1")))
            return false;
    // Флаг остановки обработки ответа при ошибке параметра
    if((var.StopFlag!=0)&(var.StopFlag!=1))
        return false;
    return true;
}

// --------------------------------------------------------------------------------------------------------------------------------
// Конструктор заполняет значениями по умолчанию воизбежание случайных значений
CVarDB::CVarDB()
{
    VarIndex =0;
    VarOffset =0;
    VarData =1;
    AlarmSet =0;
    SMSSet =0;
    StopFlag =0;
}
//---------------------------------------------------------------------------------------------------------------------------------
// Конструктор элемента из таблицы команд
CCommand::CCommand()
{
    BadAttempts =0;            // Число неудачных попыток
    QTime midnight(0,0,0);
    qsrand(midnight.secsTo(QTime::currentTime()));
    Number = (quint32)qrand(); // Сеансовый номер
    Status = "READY";          // Готова к отправке
    Answer ="";
}
