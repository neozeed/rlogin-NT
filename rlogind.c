/*
 * Copyright (C) 1994 Nathaniel W. Mishkin.
 * Copyright (C) 1991 Microsoft Corporation.
 * All rights reserved.
 */

#include <windows.h>
#include <winsock.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <assert.h>
#include <time.h>
#include <stdarg.h>

#include "session.h"

#define LOGIN_PORT 513
#define HOSTS_EQUIV "hosts.eqv"
#define PERMISSION_DENIED_MSG "Permission denied\n"

BOOL DebugFlag = FALSE;
BOOL InsecureFlag = FALSE;

// **********************************************************************
// GetStr
//
// Read a string from the socket.  Used as part of rlogin protocol processing.
//

static BOOL 
GetStr(
    SOCKET Socket,
    char *buf,
    int cnt
)
{
    char c;
    
    do {
        if (recv(Socket, &c, 1, 0) != 1)
            return(FALSE);
        *buf++ = c;
        if (--cnt == 0) {
            SessionLog(EVENTLOG_ERROR_TYPE, "protocol string too long");
            return(FALSE);
        }
    } while (c != 0);

    return(TRUE);
}


// **********************************************************************
// CtrlHandler
//
// Ctrl-C handler routine.
//

static BOOL
CtrlHandler(
    DWORD CtrlType
    )
{
    //
    // We'll handle Ctrl-C events
    //

    return (CtrlType == CTRL_C_EVENT);
}

// **********************************************************************
// CheckLogin
//
// Make sure the remote user/host is one we want to let in.
//

static BOOL
CheckLogin(
    char *RemoteUser,
    char *RemoteHost,
    char *LocalUser,
    u_long ClientAddr
)
{
    struct hostent *HostEnt;
    BYTE HostName[100];
    BOOL HostOk = FALSE;
    FILE *Fp;
    char *LoggedInUser = getenv("USERNAME");

    if (strcmp(LocalUser, LoggedInUser) != 0) {
        SessionLog(EVENTLOG_AUDIT_FAILURE, 
                  "Login rejected -- remote user: %s@%s, local user name mismatch (\"%s\" != \"%s\")",
                  RemoteUser, RemoteHost, LocalUser, LoggedInUser);
        return FALSE;
    }

    Fp = fopen(HOSTS_EQUIV, "r");
    if (Fp == NULL) {
        SessionLog(EVENTLOG_ERROR_TYPE, "Can't open \"%s\" -- exiting", HOSTS_EQUIV);
        return FALSE;
    }

    while (fgets(HostName, sizeof HostName, Fp) != NULL) {
        HostName[strlen(HostName) - 1] = '\0';     // Strip LF
        if ((HostEnt = gethostbyname(HostName)) != NULL) 
            if (ClientAddr == * (u_long *) HostEnt->h_addr) {
                HostOk = TRUE;
                break;
            }
    }

    fclose(Fp);

    if (! HostOk) 
        SessionLog(EVENTLOG_AUDIT_FAILURE, 
                  "Login rejected -- Remote user: %s@%s, illegal host",
                  RemoteUser, RemoteHost);

    return HostOk;
}

// **********************************************************************
// RloginThreadFn
//
// Thread base function for each concurrent rlogin session.
//

VOID
RloginThreadFn(
    PVOID Parameter
    )
{
    SOCKET ClientSocket = (SOCKET) Parameter;
    BYTE LocalUser[16], RemoteUser[16], TerminalType[64];
    struct hostent *HostEnt;
    BYTE HostName[100];
    BYTE Buffer[16];
    struct sockaddr_in ClientSockAddr;
    int ClientLen;

    //
    // Get leading null byte
    //
    recv(ClientSocket, (char *) &Buffer, 1, 0);

    //
    // Get remote and local user names and terminal type
    //
    GetStr(ClientSocket, RemoteUser, sizeof(RemoteUser));
    GetStr(ClientSocket, LocalUser, sizeof(LocalUser));
    GetStr(ClientSocket, TerminalType, sizeof(TerminalType));

    //
    // Get name of peer host
    //

    ClientLen = sizeof (ClientSockAddr);
    if (getpeername(ClientSocket, (struct sockaddr *) &ClientSockAddr, &ClientLen) != 0) {
        SessionLog(EVENTLOG_ERROR_TYPE, "getpeername: %d", WSAGetLastError());
        return;
    }

    if ((HostEnt = gethostbyaddr((char *) &ClientSockAddr.sin_addr, 
                                 sizeof ClientSockAddr.sin_addr, PF_INET)) == NULL) 
    {
        sprintf(HostName, inet_ntoa(ClientSockAddr.sin_addr));
    }
    else {
        strcpy(HostName, HostEnt->h_name);
    }

    send(ClientSocket, "", 1, 0);

    if (! InsecureFlag && 
        ! CheckLogin(RemoteUser, HostName, 
                     LocalUser[0] == '\0' ? RemoteUser : LocalUser,
                     ClientSockAddr.sin_addr.s_addr))
    {
        send(ClientSocket, PERMISSION_DENIED_MSG, sizeof PERMISSION_DENIED_MSG, 0);
        closesocket(ClientSocket);
        return;
    }

    SessionLog(EVENTLOG_AUDIT_SUCCESS, "LOGIN  -- remote user: %s@%s, local user: %s",
               RemoteUser, HostName, LocalUser);

    //
    // Do the real work of running the connection.
    //
    if (! SessionRun(ClientSocket)) {
        SessionLog(EVENTLOG_ERROR_TYPE, "SessionRun: %d", GetLastError());
        return;
    }

    SessionLog(EVENTLOG_AUDIT_SUCCESS, "LOGOUT  -- remote user: %s@%s, local user: %s",
               RemoteUser, HostName, LocalUser);

    ExitThread(0);
}


// **********************************************************************
// main
//
// Main program
//

VOID
main(
    int argc,
    char *argv[]
)
{
    SOCKET AcceptSocket, ClientSocket;
    struct sockaddr_in FromSockAddr, SockAddr;
    int FromLen;
    int Err;
    WSADATA WsaData;
    SECURITY_ATTRIBUTES SecurityAttributes;
    HANDLE ThreadHandle;
    DWORD ThreadId;
    char *p;

    if (argc > 1 && argv[1][0] == '-') 
        for (p = &argv[1][1]; *p != '\0'; p++)
            switch (*p) {
                case 'i':
                    InsecureFlag = TRUE;
                    break;

                case 'd':
                    DebugFlag = TRUE;
                    break;

                default:
                    SessionLog(EVENTLOG_ERROR_TYPE, "Unrecognized option: %c", *p);
                    exit(1);
            }

    if ((Err = WSAStartup(MAKEWORD(1, 1), &WsaData)) != 0) {
        SessionLog(EVENTLOG_ERROR_TYPE, "WSAStartup: %d", Err);
        return;
    }
    
    //
    // Install a handler for Ctrl-C
    //
    if (!SetConsoleCtrlHandler((PHANDLER_ROUTINE) &CtrlHandler, TRUE)) {
        SessionLog(EVENTLOG_ERROR_TYPE, 
            "Failed to install control-C handler, error = %d\n", GetLastError());
        return;
    }

    //
    // Setup a socket to listen on
    //

    if ((AcceptSocket = socket(PF_INET, SOCK_STREAM, 0)) == INVALID_SOCKET) {
        SessionLog(EVENTLOG_ERROR_TYPE, "socket: %d\n", WSAGetLastError());
        return;
    }
    
    SockAddr.sin_family = PF_INET;
    SockAddr.sin_port = htons(LOGIN_PORT);
    SockAddr.sin_addr.s_addr = 0;

    if (bind(AcceptSocket, (struct sockaddr *) &SockAddr, sizeof(SockAddr)) != 0) {
        SessionLog(EVENTLOG_ERROR_TYPE, "bind: %d", WSAGetLastError());
        return;
    }

    if (listen(AcceptSocket, 1) != 0) {
        SessionLog(EVENTLOG_ERROR_TYPE, "listen: %d", WSAGetLastError());
        return;
    }

    SecurityAttributes.nLength = sizeof(SecurityAttributes);
    SecurityAttributes.lpSecurityDescriptor = NULL; // Use default ACL
    SecurityAttributes.bInheritHandle = FALSE; // No inheritance

    //
    // Loop forever, waiting for incoming connections.
    //

    SessionLog(EVENTLOG_AUDIT_SUCCESS, "Waiting for incoming connections...");

    while (TRUE) {
        FromLen = sizeof (FromSockAddr);
        if ((ClientSocket = accept(AcceptSocket, (struct sockaddr *) &FromSockAddr, &FromLen)) == 
            INVALID_SOCKET) 
            {
                SessionLog(EVENTLOG_ERROR_TYPE, "accept: %d", WSAGetLastError());
                return;
            }
        
        ThreadHandle = 
            CreateThread(&SecurityAttributes, 0, 
                         (LPTHREAD_START_ROUTINE) RloginThreadFn,
                         (LPVOID) ClientSocket, 0, &ThreadId);
        if (ThreadHandle == NULL) {
            SessionLog(EVENTLOG_ERROR_TYPE, "CreateThread: %d", GetLastError());
            continue;
        }

        CloseHandle(ThreadHandle);
    }
}

