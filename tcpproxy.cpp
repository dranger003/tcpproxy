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
    USHORT usDstPort;
    LPCTSTR pszDstHost;
} ACCEPTSTATE, *PACCEPTSTATE;

class CTaskMgr;

class ITask
{
public:
    virtual LPCTSTR GetType() = 0;
    virtual void Execute(CTaskMgr *pTaskMgr, PVOID pv) = 0;
    virtual void Terminate() = 0;
};

class CTaskMgr
{
public:
    typedef ITask * RequestType;

    CTaskMgr()
    {
        HRESULT hr = _cs.Init();
        _ASSERT(SUCCEEDED(hr));
    }

    virtual ~CTaskMgr()
    {
        HRESULT hr = _cs.Term();
        _ASSERT(SUCCEEDED(hr));
    }

    // Pool -> Threads
    BOOL Initialize(PVOID pv) throw() { return TRUE; }
    void Terminate(PVOID pv) throw() { }

    // Thread -> Tasks
    void Execute(ITask *pTask, PVOID pv, LPOVERLAPPED pOvl)
    {
        SIZE_T nIndex = 0;

        if (!_tcscmp(pTask->GetType(), _T("CAcceptTask")))
        {
            {
                CComCritSecLock<CComCriticalSection> lock(_cs, false);
                HRESULT hr = lock.Lock();
                _ASSERT(SUCCEEDED(hr));

                nIndex = _AcceptTasks.Add(pTask);
            }

            pTask->Execute(this, pv);

            {
                CComCritSecLock<CComCriticalSection> lock(_cs, false);
                HRESULT hr = lock.Lock();
                _ASSERT(SUCCEEDED(hr));

                _AcceptTasks.RemoveAt(nIndex);
            }
        }
        else
        {
            pTask->Execute(this, pv);
        }
    }

    void TerminateAcceptTasks()
    {
        CComCritSecLock<CComCriticalSection> lock(_cs, false);
        HRESULT hr = lock.Lock();
        _ASSERT(SUCCEEDED(hr));

        for (SIZE_T i = 0; i < _AcceptTasks.GetCount(); i++)
            _AcceptTasks[i]->Terminate();
    }

private:
    CComCriticalSection _cs;
    static CAtlArray<ITask *> _AcceptTasks;
};

CAtlArray<ITask *> CTaskMgr::_AcceptTasks;

class CAcceptTask : public ITask
{
public:
    CAcceptTask(PVOID pv) :
        _pv(pv),
        _hEvent(NULL)
    {
        _hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
        _ASSERT(_hEvent);
    }

    virtual ~CAcceptTask()
    {
        CloseHandle(_hEvent);
    }

    LPCTSTR GetType()
    {
        static const _TCHAR szType[] = _T("CAcceptTask");
        return szType;
    }

    void Execute(CTaskMgr *pTaskMgr, PVOID pv)
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
            _ASSERT(dwRes != SOCKET_ERROR);

            DBG1(_T("WSAConnect()"), _T("%s:%d"), pState->pszDstHost, pState->usDstPort);

            {
                DWORD dwRes = 0;

                CHAR bytes[65536] = { 0 };
                WSABUF buf;
                buf.buf = &bytes[0];
                DWORD rcb = 0, scb = 0, dwFlags = 0;
                HANDLE hEvents[] = { _hEvent, WSACreateEvent(), WSACreateEvent() };
                WSANETWORKEVENTS wne = { 0 };

                dwRes = WSAEventSelect(pState->hAccept, hEvents[1], FD_READ | FD_CLOSE);
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

                WSACloseEvent(hEvents[2]);
                WSACloseEvent(hEvents[1]);
            }

            WSAShutdown(hConnect, SD_BOTH);
            WSACloseSocket(hConnect);
        }

        WSAShutdown(pState->hAccept, SD_BOTH);
        WSACloseSocket(pState->hAccept);

        delete pState;

        DBG1(_T("CAcceptTask::Run()"), _T("OUT"));
    }

    void Terminate() { SetEvent(_hEvent); }

private:
    PVOID _pv;
    HANDLE _hEvent;
};

class CListenTask : public ITask
{
public:
    CListenTask(PVOID pv) :
        _pv(pv)
    { }

    LPCTSTR GetType()
    {
        static const _TCHAR szType[] = _T("CListenTask");
        return szType;
    }

    void Execute(CTaskMgr *pTaskMgr, PVOID pv)
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

        HANDLE hEvents[] = { pState->hEvent, WSACreateEvent() };
        WSANETWORKEVENTS wne = { 0 };

        dwRes = WSAEventSelect(hListen, hEvents[1], FD_ACCEPT);
        _ASSERT(dwRes != SOCKET_ERROR);

        while (TRUE)
        {
            dwRes = WSAWaitForMultipleEvents(sizeof(hEvents) / sizeof(HANDLE), hEvents, FALSE, INFINITE, FALSE);
            if (dwRes == WSA_WAIT_EVENT_0)
                break;

            _ASSERT(dwRes == WSA_WAIT_EVENT_0 + 1);

            dwRes = WSAEnumNetworkEvents(hListen, hEvents[1], &wne);
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
                    pAcceptState->pszDstHost = pState->pszDstHost;
                    pAcceptState->usDstPort = pState->usDstPort;
                    pPool->QueueRequest(new CAcceptTask(pAcceptState));
                }
            }
        }

        pTaskMgr->TerminateAcceptTasks();

        WSACloseEvent(hEvents[1]);

        WSAShutdown(hListen, SD_BOTH);
        WSACloseSocket(hListen);

        DBG1(_T("CListenTask::Run()"), _T("OUT"));
    }

    void Terminate() { }

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
