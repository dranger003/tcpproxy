// stdafx.h : include file for standard system include files,
// or project specific include files that are used frequently, but
// are changed infrequently
//

#pragma once

#include "targetver.h"

#include <stdio.h>
#include <tchar.h>


#define _ATL_CSTRING_EXPLICIT_CONSTRUCTORS      // some CString constructors will be explicit

#include <atlbase.h>
#include <atlstr.h>

// TODO: reference additional headers your program requires here
#include <atlcoll.h>
#include <WS2tcpip.h>
#include <conio.h>

#define WSABind             bind
#define WSAListen           listen
#define WSACloseSocket      closesocket
#define WSAShutdown         shutdown

#ifdef _WIN64
#pragma comment(lib, "C:\\Program Files (x86)\\Windows Kits\\8.1\\Lib\\winv6.3\\um\\x64\\WS2_32.Lib")
#else
#pragma comment(lib, "C:\\Program Files (x86)\\Windows Kits\\8.1\\Lib\\winv6.3\\um\\x86\\WS2_32.Lib")
#endif

#define NSEC        1000000000
#define USEC        1000000
#define MSEC        1000

#define _DBG(f)             {                                                                                       \
                                LARGE_INTEGER ulEnd;                                                                \
                                QueryPerformanceCounter(&ulEnd);                                                    \
                                _tprintf(                                                                           \
                                    _T("[%23.11Lf] [%5ld] %s"),                                                     \
                                    (ulEnd.QuadPart - _ulStart.QuadPart) * USEC / (long double)_ulFreq.QuadPart,    \
                                    GetCurrentThreadId(),                                                           \
                                    (f)                                                                             \
                                );                                                                                  \
                                fflush(stdout);                                                                     \
                                                        }
#define DBG0(f)             _DBG((f))                                                                               \
                            _tprintf(_T("\n"));                                                                     \
                            fflush(stdout)
#define DBG1(f, ...)        _DBG((f))                   \
                            _tprintf(_T(": "));         \
                            _tprintf(__VA_ARGS__);      \
                            _tprintf(_T("\n"));         \
                            fflush(stdout)

extern LARGE_INTEGER _ulFreq, _ulStart;

struct _INITTIMING
{
    _INITTIMING()
    {
        QueryPerformanceFrequency(&_ulFreq);
        QueryPerformanceCounter(&_ulStart);
    }
};

static struct _INITTIMING __INITTIMING;
