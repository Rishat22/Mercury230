#ifndef GENERAL_H
#define GENERAL_H
#include <QStringList>
#include <QByteArray>
#include <CCommand.h>
#include <defs.h>

QByteArray serialize(QStringList stringList);
QByteArray serializec(QList<CVarDB> List);
QByteArray serializev(CVarDB Parameter);
// Преобразование массива байт с "\0" терминальным символом в строковый лист с
QStringList deserialize(QByteArray byteArray);
QList<CVarDB> deserializec(QByteArray byteArray);
CVarDB deserializev(QByteArray byteArray);
//QStringList deserialize(QByteArray byteArray, QString SplitOld, QString SplitNew ="\r\n");

QString GetErrorString(quint8 code);
QByteArray GetErrorArray(quint8 code);
#endif // GENERAL_H
