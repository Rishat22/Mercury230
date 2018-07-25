#ifndef CLAMP_H
#define CLAMP_H

#include <QWidget>
#include <qpainter.h>

// Класс маленькой лампочки
class CLamp : public QWidget
{
    Q_OBJECT
public:
    CLamp(quint16 mID =0, QWidget* pwgt =0, Qt::WindowFlags f=0);
    bool IsSet;
    quint16 ID;
protected:
    void paintEvent(QPaintEvent*);
public slots:
    void ReDraw(quint16 mID, bool Flag);
};

#endif // CLAMP_H
