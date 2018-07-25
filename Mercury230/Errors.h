#ifndef ERRORS_H
#define ERRORS_H

// Убрать если dll не будет использован
//#define SNTPDLL

#define MSECONDS_ERROR      5000
#define YEAR_ERROR          5001
#define MINUTE_ERROR        5002
#define SECONDS_ERROR       5003
#define MSOCKET_ERROR       5100
#define MWSASTARTUP_ERROR   5101
#define MSOCKVERSION_ERROR  5102
#define NSERVER_ERROR       5200
#define SERVERADDR_ERROR    5201
#define ANOTHER_ERROR       5300

#define MNO_ERRORS          5500

extern unsigned int MErrorcode;
#endif // ERRORS_H
