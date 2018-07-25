#ifndef CCONTROLDATA
#define CCONTROLDATA
#include <QStringList>
#include <QDateTime>
// Класс управляющих данных объекта
class CControlData
{
public:
    CControlData();
    void CopyControlData(CControlData* data);// Копирование всех данных в объект
    bool CorrectData(int year);         // Проверка корректности данных
    bool TestTimeTable(int year);       // Тест расписания
    bool GetTimeTable(QDateTime* startDT, QDateTime* stopDT,
                      QDateTime* currDT,
                      int index);       // Получение расписания, если строка некорректна или индекс отсутствует, то возврат false
    quint32 ObjectDelay;                // Задержка обновления данных команд, статусов и т.д.
    quint32 TerminalDelay;              // Максимально время ожидания ответа в режиме "терминал"
    bool FlagRequest;                   // Флаг разрешения опроса в режиме "запрос"
    QString CurrentMode;                // Режим работы потока опроса
    QString UserCommand;                // Пользовательская команда
    QStringList TimeTable;              // Расписание
    QStringList Equipment;              // Перечень приборов
    QStringList RequestCommands;        // Тип команд опроса
    QString ObjectDB;                   // БД для записи данных полученных от приборов
    QString TerminalCommand;            // Терминальная команда
};
#endif // CCONTROLDATA

