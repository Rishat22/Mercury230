/****************************************************************************
** Meta object code from reading C++ file 'CAnswer.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.3.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../Mercury230/CAnswer.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'CAnswer.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.3.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
struct qt_meta_stringdata_Thread_t {
    QByteArrayData data[1];
    char stringdata[7];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_Thread_t, stringdata) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_Thread_t qt_meta_stringdata_Thread = {
    {
QT_MOC_LITERAL(0, 0, 6)
    },
    "Thread"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_Thread[] = {

 // content:
       7,       // revision
       0,       // classname
       0,    0, // classinfo
       0,    0, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

       0        // eod
};

void Thread::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    Q_UNUSED(_o);
    Q_UNUSED(_id);
    Q_UNUSED(_c);
    Q_UNUSED(_a);
}

const QMetaObject Thread::staticMetaObject = {
    { &QThread::staticMetaObject, qt_meta_stringdata_Thread.data,
      qt_meta_data_Thread,  qt_static_metacall, 0, 0}
};


const QMetaObject *Thread::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Thread::qt_metacast(const char *_clname)
{
    if (!_clname) return 0;
    if (!strcmp(_clname, qt_meta_stringdata_Thread.stringdata))
        return static_cast<void*>(const_cast< Thread*>(this));
    return QThread::qt_metacast(_clname);
}

int Thread::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QThread::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    return _id;
}
struct qt_meta_stringdata_CAnswer_t {
    QByteArrayData data[21];
    char stringdata[196];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_CAnswer_t, stringdata) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_CAnswer_t qt_meta_stringdata_CAnswer = {
    {
QT_MOC_LITERAL(0, 0, 7),
QT_MOC_LITERAL(1, 8, 12),
QT_MOC_LITERAL(2, 21, 0),
QT_MOC_LITERAL(3, 22, 15),
QT_MOC_LITERAL(4, 38, 25),
QT_MOC_LITERAL(5, 64, 4),
QT_MOC_LITERAL(6, 69, 5),
QT_MOC_LITERAL(7, 75, 15),
QT_MOC_LITERAL(8, 91, 9),
QT_MOC_LITERAL(9, 101, 11),
QT_MOC_LITERAL(10, 113, 4),
QT_MOC_LITERAL(11, 118, 4),
QT_MOC_LITERAL(12, 123, 2),
QT_MOC_LITERAL(13, 126, 9),
QT_MOC_LITERAL(14, 136, 10),
QT_MOC_LITERAL(15, 147, 10),
QT_MOC_LITERAL(16, 158, 7),
QT_MOC_LITERAL(17, 166, 6),
QT_MOC_LITERAL(18, 173, 1),
QT_MOC_LITERAL(19, 175, 12),
QT_MOC_LITERAL(20, 188, 7)
    },
    "CAnswer\0ResetSilence\0\0SendLastErrorDB\0"
    "SendDeleteTextLastErrorDB\0Stop\0Print\0"
    "SetObjectStatus\0GetSocket\0QTcpSocket*\0"
    "sock\0data\0ID\0DelSocket\0ReadAnswer\0"
    "NeedToRead\0DelThis\0OnQuit\0p\0sendToClient\0"
    "timeout"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_CAnswer[] = {

 // content:
       7,       // revision
       0,       // classname
       0,    0, // classinfo
      14,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       6,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    0,   84,    2, 0x06 /* Public */,
       3,    1,   85,    2, 0x06 /* Public */,
       4,    1,   88,    2, 0x06 /* Public */,
       5,    0,   91,    2, 0x06 /* Public */,
       6,    2,   92,    2, 0x06 /* Public */,
       7,    2,   97,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       8,    3,  102,    2, 0x0a /* Public */,
      13,    0,  109,    2, 0x0a /* Public */,
      14,    2,  110,    2, 0x0a /* Public */,
      14,    1,  115,    2, 0x2a /* Public | MethodCloned */,
      14,    0,  118,    2, 0x2a /* Public | MethodCloned */,
      16,    0,  119,    2, 0x0a /* Public */,
      17,    1,  120,    2, 0x0a /* Public */,
      19,    2,  123,    2, 0x0a /* Public */,

 // signals: parameters
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,    2,
    QMetaType::Void, QMetaType::QString,    2,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString, QMetaType::Bool,    2,    2,
    QMetaType::Void, QMetaType::QString, QMetaType::Int,    2,    2,

 // slots: parameters
    QMetaType::Void, 0x80000000 | 9, QMetaType::QByteArray, QMetaType::UInt,   10,   11,   12,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool, QMetaType::QByteArray,   15,   11,
    QMetaType::Void, QMetaType::Bool,   15,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QObjectStar,   18,
    QMetaType::Void, QMetaType::QByteArray, QMetaType::Int,   11,   20,

       0        // eod
};

void CAnswer::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        CAnswer *_t = static_cast<CAnswer *>(_o);
        switch (_id) {
        case 0: _t->ResetSilence(); break;
        case 1: _t->SendLastErrorDB((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 2: _t->SendDeleteTextLastErrorDB((*reinterpret_cast< QString(*)>(_a[1]))); break;
        case 3: _t->Stop(); break;
        case 4: _t->Print((*reinterpret_cast< QString(*)>(_a[1])),(*reinterpret_cast< bool(*)>(_a[2]))); break;
        case 5: _t->SetObjectStatus((*reinterpret_cast< QString(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2]))); break;
        case 6: _t->GetSocket((*reinterpret_cast< QTcpSocket*(*)>(_a[1])),(*reinterpret_cast< QByteArray(*)>(_a[2])),(*reinterpret_cast< quint32(*)>(_a[3]))); break;
        case 7: _t->DelSocket(); break;
        case 8: _t->ReadAnswer((*reinterpret_cast< bool(*)>(_a[1])),(*reinterpret_cast< QByteArray(*)>(_a[2]))); break;
        case 9: _t->ReadAnswer((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 10: _t->ReadAnswer(); break;
        case 11: _t->DelThis(); break;
        case 12: _t->OnQuit((*reinterpret_cast< QObject*(*)>(_a[1]))); break;
        case 13: _t->sendToClient((*reinterpret_cast< QByteArray(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<int*>(_a[0]) = -1; break;
        case 6:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< QTcpSocket* >(); break;
            }
            break;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        void **func = reinterpret_cast<void **>(_a[1]);
        {
            typedef void (CAnswer::*_t)();
            if (*reinterpret_cast<_t *>(func) == static_cast<_t>(&CAnswer::ResetSilence)) {
                *result = 0;
            }
        }
        {
            typedef void (CAnswer::*_t)(QString );
            if (*reinterpret_cast<_t *>(func) == static_cast<_t>(&CAnswer::SendLastErrorDB)) {
                *result = 1;
            }
        }
        {
            typedef void (CAnswer::*_t)(QString );
            if (*reinterpret_cast<_t *>(func) == static_cast<_t>(&CAnswer::SendDeleteTextLastErrorDB)) {
                *result = 2;
            }
        }
        {
            typedef void (CAnswer::*_t)();
            if (*reinterpret_cast<_t *>(func) == static_cast<_t>(&CAnswer::Stop)) {
                *result = 3;
            }
        }
        {
            typedef void (CAnswer::*_t)(QString , bool );
            if (*reinterpret_cast<_t *>(func) == static_cast<_t>(&CAnswer::Print)) {
                *result = 4;
            }
        }
        {
            typedef void (CAnswer::*_t)(QString , int );
            if (*reinterpret_cast<_t *>(func) == static_cast<_t>(&CAnswer::SetObjectStatus)) {
                *result = 5;
            }
        }
    }
}

const QMetaObject CAnswer::staticMetaObject = {
    { &QObject::staticMetaObject, qt_meta_stringdata_CAnswer.data,
      qt_meta_data_CAnswer,  qt_static_metacall, 0, 0}
};


const QMetaObject *CAnswer::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *CAnswer::qt_metacast(const char *_clname)
{
    if (!_clname) return 0;
    if (!strcmp(_clname, qt_meta_stringdata_CAnswer.stringdata))
        return static_cast<void*>(const_cast< CAnswer*>(this));
    return QObject::qt_metacast(_clname);
}

int CAnswer::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 14)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 14;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 14)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 14;
    }
    return _id;
}

// SIGNAL 0
void CAnswer::ResetSilence()
{
    QMetaObject::activate(this, &staticMetaObject, 0, 0);
}

// SIGNAL 1
void CAnswer::SendLastErrorDB(QString _t1)
{
    void *_a[] = { 0, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void CAnswer::SendDeleteTextLastErrorDB(QString _t1)
{
    void *_a[] = { 0, const_cast<void*>(reinterpret_cast<const void*>(&_t1)) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void CAnswer::Stop()
{
    QMetaObject::activate(this, &staticMetaObject, 3, 0);
}

// SIGNAL 4
void CAnswer::Print(QString _t1, bool _t2)
{
    void *_a[] = { 0, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)) };
    QMetaObject::activate(this, &staticMetaObject, 4, _a);
}

// SIGNAL 5
void CAnswer::SetObjectStatus(QString _t1, int _t2)
{
    void *_a[] = { 0, const_cast<void*>(reinterpret_cast<const void*>(&_t1)), const_cast<void*>(reinterpret_cast<const void*>(&_t2)) };
    QMetaObject::activate(this, &staticMetaObject, 5, _a);
}
QT_END_MOC_NAMESPACE
