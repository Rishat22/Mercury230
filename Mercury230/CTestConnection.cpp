#include "mainwindow.h"
#include "CheckSum.h"

// 01.12.2016
// Проверяет полученный пакет CRC16, длину данных, ID, IDS
void CTestConnection::ReadPing()
{
    QByteArray str, data;
    str.clear();
    data.clear();
    quint32 ID =0;                 // Идентификатор объекта
    quint32 IDS =0;                // Идентификатор сервера
    quint32 datalength =0;         // Длина данных
    // Список объектов пуст проверять смысла нет - отправка сигнала на удаление
    if(Objects.isEmpty())
    {
        disconnect(pClientSocket, SIGNAL(readyRead()), this, SLOT(ReadPing()));
        disconnect(pClientSocket, SIGNAL(disconnected()), this, SIGNAL(TimeOut()));
        emit Print(" > ПРОВЕРКА ВХОДЯЩЕГО СОЕДИНЕНИЯ:\r\n", true);
        emit Print(" > Нет объектов для входящего соединения\r\n", true);
        emit TimeOut();
        return;
    }
    // Длина стандартного пинга от контроллера
    if (pClientSocket->bytesAvailable() < 24)
        return;  // Длина некорректна, выход
    // Если получены данные и от другого пакета, тогда берутся только первые 21
    data = pClientSocket->readAll();
    if(data.size() < 24)
    {
        disconnect(pClientSocket, SIGNAL(readyRead()), this, SLOT(ReadPing()));
        disconnect(pClientSocket, SIGNAL(disconnected()), this, SIGNAL(TimeOut()));
        return;
    }
    str = data.left(21);
    data.remove(0,21);
    // Получение длины пакета данных
    datalength =0;
    datalength += (((quint32)((unsigned char)*(str.data() + 17))) << 24);
    datalength += (((quint32)((unsigned char)*(str.data() + 18))) << 16);
    datalength += (((quint32)((unsigned char)*(str.data() + 19))) << 8);
    datalength += ((quint32)((unsigned char)*(str.data() + 20)));
    // Если длина равна нулю
    if(datalength ==0)
    {
        disconnect(pClientSocket, SIGNAL(readyRead()), this, SLOT(ReadPing()));
        disconnect(pClientSocket, SIGNAL(disconnected()), this, SIGNAL(TimeOut()));
        emit Print(" > ПРОВЕРКА ВХОДЯЩЕГО СОЕДИНЕНИЯ:\r\n", true);
        emit Print(" > Получены ошибочные PLC данные\r\n", true);
        emit Print(" > Длина сообщения: 0 байт, минимум: 1 байт\r\n", true);
        emit TimeOut();
        return;
    }
    // Длина данных короче длины указанной в заголовке
    else if((data.size()-2) < datalength)
    {
        disconnect(pClientSocket, SIGNAL(readyRead()), this, SLOT(ReadPing()));
        disconnect(pClientSocket, SIGNAL(disconnected()), this, SIGNAL(TimeOut()));
        emit Print(" > ПРОВЕРКА ВХОДЯЩЕГО СОЕДИНЕНИЯ:\r\n", true);
        emit Print(" > Получены ошибочные PLC данные\r\n", true);
        emit Print(" > Длина сообщения указана: " + QString::number(datalength) +" байт\r\n", true);
        emit Print(" > Длина сообщения фактическая: " + QString::number((data.size()-2)) + " байт\r\n", true);
        emit TimeOut();
        return;
    }
    // Проверка CRC16 сообщение
    QByteArray packet;
    packet.clear();
    packet.append(str);                        // Заголовок
    packet.append(data.left((datalength + 2)));// Данные и CRC
    if(!TestPacketCRC16(packet))               // Проверка CRC пакета
    {
        disconnect(pClientSocket, SIGNAL(readyRead()), this, SLOT(ReadPing()));
        disconnect(pClientSocket, SIGNAL(disconnected()), this, SIGNAL(TimeOut()));
        emit TimeOut();
        return;
    }
    // Проверка идентификатора объекта
    ID ^=(((quint32)((unsigned char)*(str.data()))) << 24);
    ID ^=(((quint32)((unsigned char)*(str.data() + 1))) << 16);
    ID ^=(((quint32)((unsigned char)*(str.data() + 2))) << 8);
    ID ^=((quint32) ((unsigned char)*(str.data() + 3)));
    // Поиск объекта в перечне потоков
    bool IsOk = false;
    for(quint32 i=0; i < (quint32)Objects.size(); i++)
    {
        if(Objects.at(i).ID == ID)
        {
            IsOk = true;
            break;
        }
    }
    if(!IsOk)
    {
        disconnect(pClientSocket, SIGNAL(readyRead()), this, SLOT(ReadPing()));
        disconnect(pClientSocket, SIGNAL(disconnected()), this, SIGNAL(TimeOut()));
        emit Print(" > ПРОВЕРКА ВХОДЯЩЕГО СОЕДИНЕНИЯ:\r\n", true);
        emit Print(" > Получены ошибочные PLC данные\r\n", true);
        emit Print(" > ID данных: " + QString::number(ID) +
                   ", объектов с таким ID не найдено\r\n", true);
        emit TimeOut();
        return;
    }
    // Проверка идентификатора сервера
    IDS +=(((quint32)((unsigned char)*(str.data() + 4))) << 24);
    IDS +=(((quint32)((unsigned char)*(str.data() + 5))) << 16);
    IDS +=(((quint32)((unsigned char)*(str.data() + 6))) << 8);
    IDS +=((quint32) ((unsigned char)*(str.data() + 7)));
    // Если не совпал идентификатор сервера
    if(IDS!=1)
    {
        disconnect(pClientSocket, SIGNAL(readyRead()), this, SLOT(ReadPing()));
        disconnect(pClientSocket, SIGNAL(disconnected()), this, SIGNAL(TimeOut()));
        emit Print(" > ПРОВЕРКА ВХОДЯЩЕГО СОЕДИНЕНИЯ:\r\n", true);
        emit Print(" > Получены ошибочные PLC данные\r\n", true);
        emit Print(" > IDS данных: " + QString::number(IDS) +
                   ", IDS сервера: 1\r\n", true);
        emit TimeOut();
        return;
    }
    // Вывод в главное окно сообщения с данными полученными от объекта
    emit Print(" > ПРОВЕРКА ВХОДЯЩЕГО СОЕДИНЕНИЯ:\r\n", true);
    emit Print(" > Получено соединение от объекта №" + QString::number(ID) + "\r\n", true);
    disconnect(pClientSocket, SIGNAL(readyRead()), this, SLOT(ReadPing()));
    disconnect(pClientSocket, SIGNAL(disconnected()), this, SIGNAL(TimeOut()));
// 22.02    pClientSocket->setParent(0);
    emit SendSocketToThread(pClientSocket, packet, ID);
// 14.04 Поиск ошибки (производится отправка адреса, а проверка производится по другому адресу
    pTestSocket = NULL;
    emit TimeOut();
    return;
}
// 01.12.2016
// Проверка CRC16 ПЛК пакета
bool CTestConnection::TestPacketCRC16(QByteArray packet)
{
    // Если длина пакета меньше 3 (1 байт данных и 2 байта CRC)
    if(packet.size() < 3)return false;
    unsigned char KS_H, KS_L, DS_H, DS_L;
    DS_H = ((unsigned char) *(packet.data() + (packet.length() - 2)));
    DS_L = ((unsigned char) *(packet.data() + (packet.length() - 1)));
    GetCRC16((unsigned char *)(packet.data()), &KS_H, &KS_L,(unsigned long) (packet.length()));
    if((KS_H != DS_H)|(KS_L != DS_L))
    {
        emit Print(" > ПРОВЕРКА ВХОДЯЩЕГО СОЕДИНЕНИЯ:\r\n", true);
        emit Print(" > Получены ошибочные PLC данные\r\n", true);
        emit Print(" > CRC данных: " +
                   QString::number(((quint8)DS_H), 16) +
                   QString::number(((quint8)DS_L), 16) +
                   "\r\n", true);
        emit Print(" > CRC расчетное: " +
                   QString::number(((quint8)KS_H), 16) +
                   QString::number(((quint8)KS_L), 16) +
                   "\r\n", true);
        return false;
    }
    return true;
}
