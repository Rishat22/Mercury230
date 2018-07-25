#include "General.h"
QDataStream &operator<<(QDataStream &out, const CVarDB &myObj)
{
    out   << myObj.VarIndex      // Индекс переменной
          << myObj.VarPermit
          << myObj.VarName       // Имя столбца переменной в БД
          << myObj.VarType       // Тип данных к которым следует преобразовать байты из входного массива
          << myObj.VarTypeSave   // Формат в котором следует записать переменную в БД
          << myObj.VarOffset     // Сдвиг от начала во входном массиве
          << myObj.VarData       // Количество считываемых байт из входного массива
          << myObj.VarInsert     // Последовательность вставки байтов в переменную VarType
          << myObj.VarSensor_MIN // Минимальное значение датчика типа VarTypeSave
          << myObj.VarSensor_MAX // Максимальное значение датчика типа VarTypeSave
          << myObj.VarParam_MIN  // Минимальное значение параметра типа VarTypeSave
          << myObj.VarParam_MAX  // Максимальное значение параметра типа VarTypeSave
          << myObj.VarBorder_MIN // Минимальный порог срабатывания типа VarTypeSave разделенный "/" при нескольких значениях
          << myObj.VarBorder_MAX // Максимальный порог срабатывания типа VarTypeSave разделенный "/" при нескольких значениях
          << myObj.AlarmSet      // Флаг включения сигнализации
          << myObj.AlarmMin      // Текст сообщения при тревоги по низкому уровню разделенный "/" при нескольких значениях
          << myObj.AlarmMax      // Текст сообщения при тревоги по высокому уровню разделенный "/" при нескольких значениях
          << myObj.SMSSet        // Включение отправки СМС-при аварийном состоянии параметра
          << myObj.Telephones
          << myObj.StopFlag;     // Флаг остановки обработки ответа при ошибке параметра
    return out;
}
QDataStream &operator>>(QDataStream &in, CVarDB &myObj)
{ 
    in >> myObj.VarIndex      // Индекс переменной
       >> myObj.VarPermit
       >> myObj.VarName       // Имя столбца переменной в БД
       >> myObj.VarType       // Тип данных к которым следует преобразовать байты из входного массива
       >> myObj.VarTypeSave   // Формат в котором следует записать переменную в БД
       >> myObj.VarOffset     // Сдвиг от начала во входном массиве
       >> myObj.VarData       // Количество считываемых байт из входного массива
       >> myObj.VarInsert     // Последовательность вставки байтов в переменную VarType
       >> myObj.VarSensor_MIN // Минимальное значение датчика типа VarTypeSave
       >> myObj.VarSensor_MAX // Максимальное значение датчика типа VarTypeSave
       >> myObj.VarParam_MIN  // Минимальное значение параметра типа VarTypeSave
       >> myObj.VarParam_MAX  // Максимальное значение параметра типа VarTypeSave
       >> myObj.VarBorder_MIN // Минимальный порог срабатывания типа VarTypeSave разделенный "/" при нескольких значениях
       >> myObj.VarBorder_MAX // Максимальный порог срабатывания типа VarTypeSave разделенный "/" при нескольких значениях
       >> myObj.AlarmSet      // Флаг включения сигнализации
       >> myObj.AlarmMin      // Текст сообщения при тревоги по низкому уровню разделенный "/" при нескольких значениях
       >> myObj.AlarmMax      // Текст сообщения при тревоги по высокому уровню разделенный "/" при нескольких значениях
       >> myObj.SMSSet        // Включение отправки СМС-при аварийном состоянии параметра
       >> myObj.Telephones
       >> myObj.StopFlag;     // Флаг остановки обработки ответа при ошибке параметра
    return in;
}
QDataStream& operator<<(QDataStream& s, const QList<CVarDB>& l)
{
    s << quint32(l.size());
    for (int i = 0; i < l.size(); ++i)
        s << l.at(i);
    return s;
}

QDataStream& operator>>(QDataStream& s, QList<CVarDB>& l)
{
    l.clear();
    quint32 c;
    s >> c;
    l.reserve(c);
    for(quint32 i = 0; i < c; ++i)
    {
        CVarDB t;
        s >> t;
        l.append(t);
        if (s.atEnd())
            break;
    }
    return s;
}
// Разбор на массив байт
QByteArray serialize(QStringList stringList)
{
  QByteArray byteArray;
  // Если пустая то на выход
  byteArray.clear();
  if(stringList.isEmpty())return byteArray;
  byteArray.constData(); // Для подстраховки от отсутствия символа
  QDataStream out(&byteArray, QIODevice::WriteOnly);
  out << stringList;
  return byteArray;
}
QByteArray serializec(QList<CVarDB> List)
{
  QByteArray byteArray;
  // Если пустая то на выход
  byteArray.clear();
  if(List.isEmpty())return byteArray;
  byteArray.constData(); // Для подстраховки от отсутствия символа
  QDataStream out(&byteArray, QIODevice::WriteOnly);
  out << List;
  return byteArray;
}
QByteArray serializev(CVarDB Parameter)
{
  QByteArray byteArray;
  // Если пустая то на выход
  byteArray.clear();
  if(Parameter.VarName.isEmpty())return byteArray;
  byteArray.constData(); // Для подстраховки от отсутствия символа
  QDataStream out(&byteArray, QIODevice::WriteOnly);
  out << Parameter;
  return byteArray;
}
// Разбор на стандартные строки
QStringList deserialize(QByteArray byteArray)
{
  QStringList result;
  // Если пустая то на выход
  result.clear();
  if(byteArray.isEmpty())return result;
  byteArray.constData(); // Для подстраховки от отсутствия символа
  QDataStream in(&byteArray, QIODevice::ReadOnly);
  in >> result;
  return result;
}
QList<CVarDB> deserializec(QByteArray byteArray)
{
  QList<CVarDB> result;
  // Если пустая то на выход
  result.clear();
  if(byteArray.isEmpty())return result;
  byteArray.constData(); // Для подстраховки от отсутствия символа
  QDataStream in(&byteArray, QIODevice::ReadOnly);
  in >> result;
  return result;
}
CVarDB deserializev(QByteArray byteArray)
{
  CVarDB result;
  if(byteArray.isEmpty())return result;
  byteArray.constData(); // Для подстраховки от отсутствия символа
  QDataStream in(&byteArray, QIODevice::ReadOnly);
  in >> result;
  return result;
}
// 29.09.2016 Бесполезная функция - данные в массиве не конвертированы!
// Разбор на стандартные строки имещие нестандартные разделители (для листа строк стандарный разделитель \0)
/*QStringList deserialize(QByteArray byteArray, QString SplitOld, QString SplitNew)
{
  QStringList result;
  QString res;
  result.clear();
  // Еслм массив пуст
  if(byteArray.isEmpty())
      return result;
  byteArray.constData(); // Для подстраховки от отсутствия символа
  res.clear();
  // Преобразование в строку
  QDataStream in1(&byteArray, QIODevice::ReadOnly);
  in1 >> result;
  // Если ничего не пришло
  if(result.isEmpty())
      return result;
  // Сборка строки из листа
  res = result.join(SplitOld);
  result.clear();
  // Если старый и новый разделители разные, то следует изменить строку
  if(SplitNew != SplitOld)
  {
      // Замена разделителей
      int index = res.indexOf(SplitOld, 0);       // Индекс первого разделителя
      int sizeOld = SplitOld.length();            // Длина разделителя
      int sizeArray = res.size();                 // Длина строки
      while(index!=-1)
      {
          if((index + sizeOld) >= sizeArray -1)   // Увеличиваем длину массива на необходимое число байт
              res.resize(index + sizeOld + 1);
          res.insert(index + sizeOld, SplitNew);  // Добавляем новый разделитель
          res.remove(index, sizeOld);             // Удаление старого разделителя
          index = res.indexOf(SplitOld, 0);       // Поиск нового разделителя
      }
  }
  // Преобразование в лист
  result = res.split(SplitNew);
  return result;
}*/
// 25.05.2017 Получение строки с кодом ошибки
QString GetErrorString(quint8 code)
{
    QString str = "Неизвестная ошибка";
    switch (code) {
    case 0:
    {
        str= "Нет ошибок";
        break;
    }
    case 1:
    {
        str= "Общая ошибка";
        break;
    }
    case 2:
    {
        str= "Ошибка скорости порта";
        break;
    }
    case 3:
    {
        str= "Ошибка битности данных";
        break;
    }
    case 4:
    {
        str= "Ошибка стоповых бит";
        break;
    }
    case 5:
    {
        str= "Ошибка битов паритета";
        break;
    }
    case 6:
    {
        str= "Ошибка функции";
        break;
    }
    case 7:
    {
        str= "Ошибка ID контроллера";
        break;
    }
    case 8:
    {
        str= "Ошибка ID сервера";
        break;
    }
    case 9:
    {
        str= "Ошибка версии"; // Не используется
        break;
    }
    case 10:
    {
        str= "Ошибка контрольной суммы";
        break;
    }
    case 11:
    {
        str= "Ошибка номера порта";
        break;
    }
    case 12:
    {
        str= "Ошибка длины сообщения от сервера";
        break;
    }
    case 13:
    {
        str= "Баланс на нуле";
        break;
    }
    case 14:
    {
        str= "Ошибка соединения";
        break;
    }
    case 15:
    {
        str= "Ошибка величины таймаута";
        break;
    }
    case 16:
    {
        str= "Превышено ожидание ответа от оборудования";
        break;
    }
    case 17:
    {
        str= "Опрос прерван трансляцией";
        break;
    }
    case 51:
    {
        str= "Ошибка системной базы данных";
        break;
    }
    case 52:
    {
        str= "Ошибка запроса к системной базе данных";
        break;
    }
    case 53:
    {
        str= "Ошибка объектной базы данных";
        break;
    }
    case 54:
    {
        str= "Ошибка запроса к объектной базе данных";
        break;
    }
    case 55:
    {
        str= "Ошибка адреса прибора";
        break;
    }
    case 101:
    {
        str= "Ошибка контрольной суммы в данных оборудования";
        break;
    }
    case 102:
    {
        str= "Ошибка данных оборудования";
        break;
    }
    default:
        break;

    }
    return str;
}
QByteArray GetErrorArray(quint8 code)
{
    QByteArray str = "Unknown error";
    switch (code) {
    case 0:
    {
        str= "No errors";
        break;
    }
    case 1:
    {
        str= "General error";
        break;
    }
    case 2:
    {
        str= "Error of COM-port speed";
        break;
    }
    case 3:
    {
        str= "Error of COM-port data bits";
        break;
    }
    case 4:
    {
        str= "Error of COM-port stop bits";
        break;
    }
    case 5:
    {
        str= "Error of COM-port parity bits";
        break;
    }
    case 6:
    {
        str= "Error of function code";
        break;
    }
    case 7:
    {
        str= "Error ID of PLC";
        break;
    }
    case 8:
    {
        str= "Error ID of server";
        break;
    }
    case 9:
    {
        str= "Version error"; // Не используется
        break;
    }
    case 10:
    {
        str= "CRC16 error";
        break;
    }
    case 11:
    {
        str= "Error of COM-port number";
        break;
    }
    case 12:
    {
        str= "Error data length from server message";
        break;
    }
    case 13:
    {
        str= "Balance is null";
        break;
    }
    case 14:
    {
        str= "Connection error";
        break;
    }
    case 15:
    {
        str= "Error of timeout value";
        break;
    }
    case 16:
    {
        str= "Timeout of equipment";
        break;
    }
    case 17:
    {
        str= "Priority translation is interrupted";
        break;
    }
    case 51:
    {
        str= "System database error";
        break;
    }
    case 52:
    {
        str= "Query error to system database";
        break;
    }
    case 53:
    {
        str= "Object database error";
        break;
    }
    case 54:
    {
        str= "Query error to object database";
        break;
    }
    case 55:
    {
        str= "Error equipment address";
        break;
    }
    case 101:
    {
        str= "Equipment error CRC";
        break;
    }
    case 102:
    {
        str= "Equipment error data";
        break;
    }
    default:
        break;

    }
    return str;
}
