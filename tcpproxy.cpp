// tcpproxy.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#define MAX_CONNECT             64

LARGE_INTEGER _ulFreq, _ulStart;

typedef struct _LISTENSTATE
{
    HANDLE hEvent;
    USHORT usSrcPort;
    LPCTSTR pszDstHost;
    USHORT usDstPort;
} LISTENSTATE, *PLISTENSTATE;

typedef struct _ACCEPTSTATE
{
    SOCKET hAccept;
    HANDLE hEvent;
    USHORT usDstPort;
    LPCTSTR pszDstHost;
} ACCEPTSTATE, *PACCEPTSTATE;

class CTaskMgr;

class ITask
{
public:
    virtual void Run(CTaskMgr *pMgr, PVOID pv) = 0;
};

class CTaskMgr
{
public:
    typedef ITask * RequestType;

    CTaskMgr() :
        _hEvent(CreateEvent(NULL, TRUE, FALSE, NULL))
    { }

    BOOL Initialize(PVOID pv) throw() { return TRUE; }

    void Execute(RequestType request, PVOID pv, LPOVERLAPPED pOvl)
    {
        InterlockedIncrement(&_dwCount);
        request->Run(this, pv);
        InterlockedDecrement(&_dwCount);
    }

    void Terminate(PVOID pv) throw()
    {
        SetEvent(_hEvent);
        CloseHandle(_hEvent);
    }

    DWORD GetCount() { return _dwCount; }
    HANDLE GetWaitHandle() { return _hEvent; }

private:
    static DWORD _dwCount;
    HANDLE _hEvent;
};

DWORD CTaskMgr::_dwCount = 0;

class CAcceptTask : public ITask
{
public:
    CAcceptTask(PVOID pv) :
        _pv(pv)
    { }

    void Run(CTaskMgr *pMgr, PVOID pv)
    {
        DBG1(_T("CAcceptTask::Run()"), _T("IN"));

        CThreadPool<CTaskMgr> *pPool = (CThreadPool<CTaskMgr> *)pv;
        PACCEPTSTATE pState = (PACCEPTSTATE)_pv;

        DWORD dwRes = 0;

        {
            SOCKET hConnect = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, 0);
            _ASSERT(hConnect != INVALID_SOCKET);

            SOCKADDR_IN client = { 0 };
            client.sin_family = AF_INET;
            dwRes = WSAHtons(hConnect, pState->usDstPort, &client.sin_port);
            _ASSERT(dwRes != SOCKET_ERROR);
            InetPton(AF_INET, pState->pszDstHost, &client.sin_addr);

            dwRes = WSAConnect(hConnect, (PSOCKADDR)&client, sizeof(client), NULL, NULL, (LPQOS)NULL, (LPQOS)NULL);
            _tprintf(_T("WSAGetLastError() = %ld\n"), WSAGetLastError());
            _ASSERT(dwRes != SOCKET_ERROR);

            DBG1(_T("WSAConnect()"), _T("%s:%d"), pState->pszDstHost, pState->usDstPort);

            {
                CHAR bytes[65536] = { 0 };
                WSABUF buf;
                buf.buf = &bytes[0];
                DWORD rcb = 0, scb = 0, dwFlags = 0;
                HANDLE hEvents[] = { pState->hEvent, WSACreateEvent(), WSACreateEvent() };
                WSANETWORKEVENTS wne = { 0 };

                DWORD dwRes = WSAEventSelect(pState->hAccept, hEvents[1], FD_READ | FD_CLOSE);
                _ASSERT(dwRes != SOCKET_ERROR);

                dwRes = WSAEventSelect(hConnect, hEvents[2], FD_READ | FD_CLOSE);
                _ASSERT(dwRes != SOCKET_ERROR);

                while (TRUE)
                {
                    dwRes = WSAWaitForMultipleEvents(sizeof(hEvents) / sizeof(HANDLE), hEvents, FALSE, INFINITE, FALSE);
                    if (dwRes == WSA_WAIT_EVENT_0)
                        break;

                    SOCKET hSocket1, hSocket2;
                    if (dwRes == WSA_WAIT_EVENT_0 + 1)
                    {
                        dwRes = WSAEnumNetworkEvents(pState->hAccept, hEvents[1], &wne);
                        _ASSERT(dwRes != SOCKET_ERROR);

                        hSocket1 = pState->hAccept;
                        hSocket2 = hConnect;
                    }
                    else if (dwRes == WSA_WAIT_EVENT_0 + 2)
                    {
                        dwRes = WSAEnumNetworkEvents(hConnect, hEvents[2], &wne);
                        _ASSERT(dwRes != SOCKET_ERROR);

                        hSocket1 = hConnect;
                        hSocket2 = pState->hAccept;
                    }

                    if (wne.lNetworkEvents & FD_READ)
                    {
                        buf.len = sizeof(bytes);
                        dwRes = WSARecv(hSocket1, &buf, 1, &rcb, &dwFlags, NULL, NULL);

                        if (rcb > 0)
                        {
                            buf.len = rcb;
                            dwRes = WSASend(hSocket2, &buf, 1, &scb, 0, NULL, NULL);
                            _ASSERT(dwRes != SOCKET_ERROR);
                        }
                    }

                    if (wne.lNetworkEvents & FD_CLOSE)
                        break;
                }

                WSACloseEvent(hEvents[1]);
            }

            WSAShutdown(hConnect, SD_BOTH);
            WSACloseSocket(hConnect);
        }

        WSACloseEvent(pState->hEvent);

        WSAShutdown(pState->hAccept, SD_BOTH);
        WSACloseSocket(pState->hAccept);

        delete pState;

        DBG1(_T("CAcceptTask::Run()"), _T("OUT"));
    }

private:
    PVOID _pv;
};

class CListenTask : public ITask
{
public:
    CListenTask(PVOID pv) :
        _pv(pv)
    { }

    void Run(CTaskMgr *pMgr, PVOID pv)
    {
        DBG1(_T("CListenTask::Run()"), _T("IN"));

        CThreadPool<CTaskMgr> *pPool = (CThreadPool<CTaskMgr> *)pv;
        PLISTENSTATE pState = (PLISTENSTATE)_pv;

        DWORD dwRes = 0;

        SOCKET hListen = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, 0);
        _ASSERT(hListen != INVALID_SOCKET);

        SOCKADDR_IN server = { 0 };
        server.sin_family = AF_INET;
        dwRes = WSAHtons(hListen, pState->usSrcPort, &server.sin_port);
        _ASSERT(dwRes != SOCKET_ERROR);
        InetPton(AF_INET, _T("127.0.0.1"), &server.sin_addr);

        dwRes = WSABind(hListen, (PSOCKADDR)&server, sizeof(server));
        _ASSERT(dwRes != SOCKET_ERROR);

        dwRes = WSAListen(hListen, SOMAXCONN);
        _ASSERT(dwRes != SOCKET_ERROR);

        DBG1(_T("WSAListen()"), _T("127.0.0.1:%d"), pState->usSrcPort);

        HANDLE hEvent = WSACreateEvent();
        dwRes = WSAEventSelect(hListen, hEvent, FD_ACCEPT);
        _ASSERT(dwRes != SOCKET_ERROR);

        HANDLE hEvents[] = { pState->hEvent, hEvent };

        while (TRUE)
        {
            dwRes = WSAWaitForMultipleEvents(sizeof(hEvents) / sizeof(HANDLE), hEvents, FALSE, INFINITE, FALSE);
            if (dwRes == WSA_WAIT_EVENT_0)
                break;

            _ASSERT(dwRes == WSA_WAIT_EVENT_0 + 1);

            WSANETWORKEVENTS wne = { 0 };
            dwRes = WSAEnumNetworkEvents(hListen, hEvent, &wne);
            _ASSERT(dwRes != SOCKET_ERROR);

            if (wne.lNetworkEvents & FD_ACCEPT)
            {
                SOCKADDR_IN client = { 0 };
                INT nLen = sizeof(client);
                SOCKET hAccept = WSAAccept(hListen, (PSOCKADDR)&client, &nLen, NULL, NULL);
                _ASSERT(hAccept != INVALID_SOCKET);

                _TCHAR szAddr[INET_ADDRSTRLEN] = { 0 };
                dwRes = (DWORD)InetNtop(AF_INET, (PIN_ADDR)&client.sin_addr, &szAddr[0], INET_ADDRSTRLEN);
                _ASSERT(dwRes);

                USHORT usPort = 0;
                dwRes = (DWORD)WSANtohs(hAccept, client.sin_port, &usPort);
                _ASSERT(dwRes != SOCKET_ERROR);
                DBG1(_T("WSAAccept()"), _T("%s:%d"), szAddr, usPort);

                {
                    PACCEPTSTATE pAcceptState = new ACCEPTSTATE;
                    pAcceptState->hAccept = hAccept;
                    pAcceptState->hEvent = WSACreateEvent();
                    pAcceptState->pszDstHost = pState->pszDstHost;
                    pAcceptState->usDstPort = pState->usDstPort;
                    pPool->QueueRequest(new CAcceptTask(pAcceptState));
                }
            }
        }

        WSACloseEvent(hEvent);

        WSAShutdown(hListen, SD_BOTH);
        WSACloseSocket(hListen);

        DBG1(_T("CListenTask::Run()"), _T("OUT"));
    }

private:
    PVOID _pv;
};

void WSAInit()
{
    WSADATA data = { 0 };
    INT nRes = WSAStartup(MAKEWORD(2, 2), &data);
    _ASSERT(nRes == 0);
}

void WSAUninit()
{
    INT nRes = WSACleanup();
    _ASSERT(nRes != SOCKET_ERROR);
}

INT _tmain(INT argc, _TCHAR *argv[])
{
    DBG1(_T("_tmain()"), _T("IN"));

    WSAInit();

    CThreadPool<CTaskMgr> pool;
    pool.Initialize(&pool, 1 + MAX_CONNECT);

    _ASSERT(argc == 4);
    LISTENSTATE state = { CreateEvent(NULL, TRUE, FALSE, NULL), _tstoi(argv[1]), argv[2], _tstoi(argv[3]) };
    pool.QueueRequest(new CListenTask(&state));

    {
        _gettch();
        SetEvent(state.hEvent);
    }

    pool.Shutdown();

    CloseHandle(state.hEvent);

    WSAUninit();

    DBG1(_T("_tmain()"), _T("OUT"));

    return 0;
}
