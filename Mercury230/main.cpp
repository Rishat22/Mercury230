#include <QCoreApplication>
#include "mercury230.h"

int main(int argc, char *argv[])
{
    QCoreApplication a(argc, argv);
    setlocale(LC_ALL, "Russian");
    Mercury230* merc = new Mercury230();
    QString data = " asdasdas";
    qDebug() << GetInfoDll(data) << data;
    return a.exec();
}
