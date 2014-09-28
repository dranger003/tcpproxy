// tcpproxy.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

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
    HANDLE hEvent;
    SOCKET hAccept;
    LPCTSTR pszDstHost;
    USHORT usDstPort;
} ACCEPTSTATE, *PACCEPTSTATE;

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

DWORD WINAPI AcceptThread(PVOID pv)
{
    PACCEPTSTATE pState = (PACCEPTSTATE)pv;
    DWORD dwRes = 0;

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
        HANDLE hEvents[] = { pState->hEvent, WSACreateEvent(), WSACreateEvent() };
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

    return 0;
}

void ThreadCleanup(CAtlMap<HANDLE, PACCEPTSTATE> *pThreads, BOOL bWait)
{
    CAtlMap<HANDLE, PACCEPTSTATE>::CPair *pPair;
    DWORD dwRes;
    CAtlList<HANDLE> list;

    POSITION pos = pThreads->GetStartPosition();
    while (pos)
    {
        pPair = pThreads->GetNext(pos);
        PACCEPTSTATE pState = pPair->m_value;

        if (bWait)
            SetEvent(pState->hEvent);

        dwRes = WaitForSingleObject(pPair->m_key, bWait ? INFINITE : 0);
        if (dwRes == WSA_WAIT_EVENT_0)
        {
            WSAShutdown(pState->hAccept, SD_BOTH);
            WSACloseSocket(pState->hAccept);
            WSACloseEvent(pState->hEvent);
            delete pState;

            list.AddTail(pPair->m_key);
        }
    }

    pos = list.GetHeadPosition();
    while (pos)
        pThreads->RemoveKey(list.GetNext(pos));
}

DWORD WINAPI ListenThread(PVOID pv)
{
    DBG1(_T("ListenThread()"), _T("IN"));

    PLISTENSTATE pState = (PLISTENSTATE)pv;
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

    CAtlMap<HANDLE, PACCEPTSTATE> threads;

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

            ThreadCleanup(&threads, FALSE);

            {
                PACCEPTSTATE pAcceptState = new ACCEPTSTATE;
                pAcceptState->hEvent = WSACreateEvent();
                pAcceptState->hAccept = hAccept;
                pAcceptState->pszDstHost = pState->pszDstHost;
                pAcceptState->usDstPort = pState->usDstPort;

                HANDLE hThread = CreateThread(NULL, 0, AcceptThread, pAcceptState, 0, NULL);
                threads[hThread] = pAcceptState;
            }
        }
    }

    ThreadCleanup(&threads, TRUE);

    WSACloseEvent(hEvents[1]);

    WSAShutdown(hListen, SD_BOTH);
    WSACloseSocket(hListen);

    DBG1(_T("ListenThread()"), _T("OUT"));

    return 0;
}

INT _tmain(int argc, _TCHAR *argv[])
{
    WSAInit();

    _ASSERT(argc == 4);
    LISTENSTATE state = { CreateEvent(NULL, TRUE, FALSE, NULL), _tstoi(argv[1]), argv[2], _tstoi(argv[3]) };
    HANDLE hThread = CreateThread(NULL, 0, ListenThread, &state, 0, NULL);

    _gettch();
    SetEvent(state.hEvent);

    WaitForSingleObject(hThread, INFINITE);

    WSAUninit();

    return 0;
}
