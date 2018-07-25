#include "clamp.h"

CLamp::CLamp(quint16 mID, QWidget* pwgt, Qt::WindowFlags f) : QWidget(pwgt, f)
{
    this->setFixedSize(15,15); // Установка постоянного размера лампочки
    IsSet = false;             // По данному флагу определяется цвет лампочки
    ID = mID;                  // Присвоение идентификатора лампочке
}
void CLamp::paintEvent(QPaintEvent*)
{
    // Рисование лампочки
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    if(!IsSet)painter.setBrush(QBrush(Qt::red));
    else painter.setBrush(QBrush(Qt::green));
    QPen pen(Qt::black);
    pen.setWidth(1);
    painter.setPen(pen);
    // Делается блик
    QColor color;
    if(!IsSet)color = QColor::fromRgb(253, 108, 108);
    else color = QColor::fromRgb(108, 253, 108);
    painter.drawEllipse(QRect(1, 1, 13, 13));
    painter.setBrush(color);
    pen.setColor(color);
    painter.setPen(pen);
    painter.drawEllipse(QRect(5, 3, 4, 4));
    // Белый отблеск
    painter.setBrush(Qt::white);
    pen.setColor(Qt::white);
    painter.setPen(pen);
    painter.drawEllipse(QRect(7, 3, 2, 2));
    painter.end();
}
void CLamp::ReDraw(quint16 mID, bool Flag)
{
    if(mID != ID)return; // Идентификатор чужой
    IsSet = Flag;        // Установка полученного флага
    this->update();
}

