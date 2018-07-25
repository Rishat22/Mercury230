#ifndef CTCPSERVER
#define CTCPSERVER
#include <QTcpServer>
#include <CSocketThread.h>
class CTcpServer: public QTcpServer
{
    Q_OBJECT
public:
    CTcpServer(QObject *parent = 0) : QTcpServer(parent){}

    void incomingConnection(qintptr handle) Q_DECL_OVERRIDE
    {
        CSocketThread *socket = new CSocketThread(handle);
 //       socket->moveToThread(socket);

        connect(socket, SIGNAL(finished()), socket, SLOT(deleteLater()));
        connect(socket, SIGNAL(AddConnection(QTcpSocket*)), this, SLOT(AddConnection(QTcpSocket*)), Qt::QueuedConnection);
        socket->start();
    }
public slots:
    void AddConnection(QTcpSocket* sock)
    {
        addPendingConnection(sock);
        emit SendSocket(sock);
    }
signals:
    void SendSocket(QTcpSocket*);
};
#endif // CTCPSERVER



