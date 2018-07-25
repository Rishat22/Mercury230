#include <QCoreApplication>
#include "mercury230.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    setlocale(LC_ALL, "Russian");
    Mercury230* merc = new Mercury230();
    QString data = " ";
    qDebug() << GetInfoDll(data);
    return a.exec();
}
