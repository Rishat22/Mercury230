#ifndef CTESTCONNECTION
#define CTESTCONNECTION
#include <QThread>
#include <QTcpSocket>
#include <QTimer>
#include "OInfo.h"
// Тест полученного соединения
// 22.02.2017 Убрано перенесение тестового сокета из родительского потока
class CTestConnection : public QThread
{
    Q_OBJECT
public:
    explicit CTestConnection(QTcpSocket* sock, quint32 timeout, QList<OInfo> obj) : QThread(0)
    {
// 22.02        this->moveToThread(this);
        pTestSocket = pClientSocket = sock;
// 22.02        pClientSocket->moveToThread(this);
// 22.02        connect(this, SIGNAL(DelSocket()), pClientSocket, SLOT(deleteLater()), Qt::QueuedConnection);
        connect(this, SIGNAL(TimeOut()), this, SLOT(quit()), Qt::QueuedConnection);

        connect(pClientSocket, SIGNAL(readyRead()), this, SLOT(ReadPing()), Qt::QueuedConnection);
        connect(pClientSocket, SIGNAL(disconnected()), this, SIGNAL(TimeOut()), Qt::QueuedConnection);
/* 22.02 */   //     connect(this, SIGNAL(finished()), this, SLOT(deleteLater()), Qt::QueuedConnection);
        Delay = timeout;
        Objects = obj;
    }
    void run()
    {
        IsExiting = false;
        timer = new QTimer(this);
        connect(timer, SIGNAL(timeout()), this, SLOT(quit()), Qt::QueuedConnection);
        timer->setInterval(Delay);
        timer->start();
 //       IdTimer = startTimer(Delay); // Запуск таймера после которого будет произведено завершение потока
        exec();
    }
    QTimer *timer;
    bool IsExiting;
//    int IdTimer;
    QTcpSocket* pClientSocket, *pTestSocket;
    quint32 Delay;
    QList<OInfo> Objects;
    bool TestPacketCRC16(QByteArray packet); // Проверка CRC16 полученного пакета
public slots:
    void quit()
    {
        if(IsExiting ==true)return;
        {
            IsExiting = true;
            timer->stop();
            delete timer;
        }
//        killTimer(IdTimer);
        // Удаление сокета, если он есть (т.е. если не отправлялся)
        if(pTestSocket!=NULL)
// 23.02            emit DelSocket();
            pClientSocket->deleteLater();
        qDebug() << " > Удаление тестового потока\r\n";
        // Отложенное удаление потока
        {
            this->exit(0);
        }
        //delete this;
        //this->QThread::quit();
        emit ExitAndDelete(this);
    }
    void ReadPing();                    // Чтение пинга
signals:
// 23.02    void DelSocket();
    void ExitAndDelete(CTestConnection*);
    void Print(QString, bool);          // Вывод в главное окно информации
    void TimeOut();                     // Идентификатор объекта для которого пакет
    void SendSocketToThread(QTcpSocket* sock,
                            QByteArray ping,
                            quint32 ObjectID);          // Сигнал передачи в поток сокета нового соединения
//protected:
//    virtual void timerEvent(QTimerEvent *)
//    {
//        emit TimeOut();
//    }
};

#endif // CTESTCONNECTION

