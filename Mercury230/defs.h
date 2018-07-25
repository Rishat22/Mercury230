#ifndef DEFS
#define DEFS
#define PC_TEST                   // Тестовый режим, добавление кода для отладки
//#define WINDOWTEST              // Тестовый режим, создание множества вкладок
//#define TREETEST                // Тестовый режим, создание множества дерева
//#define CRC16TEST               // Тестовый режим, проверка функции и конфликтов в потоке
//#define REQUESTTEST             // Запись переменной RequestType
//#define PARAMETERSTEST            // Запись настройки переменных (внимание, неправильный формат может привести к краху приложения)
//#define EQUIPMENT                 // Тестовая запись перечня приборов
//#define TIMETABLE                 // Запись переменных расписания
//#define TESTDISPATCH              // Разрешает тест отправки команд и обработчика отработанных без сокета (отправка данных будет блокирована)

// Ошибки на стороне контроллера
#define NOERRORS 0           // Ошибок нет
#define GENERALERROR 1       // Общая ошибка
#define SPEEDERROR 2         // Ошибка скорости работы порта
#define DATAPORTERROR 3      // Ошибка битности данных
#define STOPBITERROR 4       // Ошибка стоповых бит
#define PARITYBITERROR 5     // Ошибка битов паритета
#define FUNCTIONERROR 6      // Ошибка функции
#define IDERROR 7            // Ошибка id контроллера
#define IDSERROR 8           // Ошибка id сервера
#define VERERROR 9           // Ошибка версии
#define CRCERROR 10          // Ошибка контрольной суммы
#define COMERROR 11          // Ошибка номера порта
#define LENGHTERROR 12       // Ошибка длины сообщения от сервера
#define BALANCENULL 13       // Баланс на нуле
#define CONNECTGSMERROR 14   // Ошибка соединения
#define TIMEOUTVARERROR 15   // Ошибка величины таймаута
#define TIMEOUT 16           // Превышено ожидание ответа от оборудования
#define ASSVTRANSLATION 17   // Прервано трансляцией данных АССВ

// Ошибки на стороне сервера
#define ERRORSYSDB 51        // Ошибка системной базы данных
#define ERRORSYSQUERY 52     // Ошибка запроса к системной БД
#define ERROROBJDB 53        // Ошибка объектной БД
#define ERROROBJQUERY 54     // Ошибка запроса к объектной БД
#define ERROREQADDRESS 55    // Ошибка адреса прибора

// Ошибки найденные при обработке данных полученных от оборудования
#define EQCRCERROR 101       // Ошибка контрольной суммы в данных полученных от оборудования
#define EQDATAERROR 102      // Ошибка данных полученных от оборудования


#define UNKNOWNERROR 99      // Неизвестная ошибка
#endif // DEFS

