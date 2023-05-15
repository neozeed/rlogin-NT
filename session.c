/*
 * Copyright (C) 1994 Nathaniel W. Mishkin.
 * Copyright (C) 1991 Microsoft Corporation.
 * All rights reserved.
 */

#include <stdlib.h>

#include "session.h"

#define BUFFER_SIZE 200

//
// Shell command line
//
#define SHELL_COMMAND_LINE  TEXT("cmd /q")

//
// Structure used to describe each session
//
typedef struct {

    //
    // These fields are filled in at session creation time
    //
    HANDLE  ReadPipeHandle;         // Handle to shell stdout pipe
    HANDLE  WritePipeHandle;        // Handle to shell stdin pipe
    HANDLE  ProcessHandle;          // Handle to shell process

    //
    //
    // These fields are filled in at session connect time and are only
    // valid when the session is connected
    //
    SOCKET  ClientSocket;
    HANDLE  ReadShellThreadHandle;  // Handle to session shell-read thread
    HANDLE  WriteShellThreadHandle; // Handle to session shell-read thread

} SESSION_DATA, *PSESSION_DATA;


//
// Private prototypes
//

static HANDLE
StartShell(
    HANDLE StdinPipeHandle,
    HANDLE StdoutPipeHandle
    );

static VOID
SessionReadShellThreadFn(
    LPVOID Parameter
    );

static VOID
SessionWriteShellThreadFn(
    LPVOID Parameter
    );


// **********************************************************************
// SessionLog
//
// Wrapper over WIN32 event logging functions.
//

VOID
SessionLog(
    WORD EventType,
    char *Fmt,
    ...
)
{
    HANDLE  hEventSource;
    LPTSTR  lpszStrings[1];
    char Msg[200];
    va_list Marker;

    va_start(Marker, Fmt);
    _vsnprintf(Msg, sizeof Msg, Fmt, Marker);

    puts(Msg);

    //
    // Use event logging to log the error.
    //
    hEventSource = RegisterEventSource(NULL, TEXT("RloginService"));

    lpszStrings[0] = Msg;

    if (hEventSource != NULL) {
        ReportEvent(hEventSource, EventType, 0, 0, NULL, 1, 0, lpszStrings, NULL);
        DeregisterEventSource(hEventSource);
    }
}


// **********************************************************************
//
// CreateSession
//
// Creates a new session. Involves creating the shell process and establishing
// pipes for communication with it.
//
// Returns a handle to the session or NULL on failure.
//

static PSESSION_DATA
CreateSession(
    VOID
    )
{
    PSESSION_DATA Session = NULL;
    BOOL Result;
    SECURITY_ATTRIBUTES SecurityAttributes;
    HANDLE ShellStdinPipe = NULL;
    HANDLE ShellStdoutPipe = NULL;

    //
    // Allocate space for the session data
    //
    Session = (PSESSION_DATA) malloc(sizeof(SESSION_DATA));
    if (Session == NULL) {
        return(NULL);
    }

    //
    // Reset fields in preparation for failure
    //
    Session->ReadPipeHandle  = NULL;
    Session->WritePipeHandle = NULL;


    //
    // Create the I/O pipes for the shell
    //
    SecurityAttributes.nLength = sizeof(SecurityAttributes);
    SecurityAttributes.lpSecurityDescriptor = NULL; // Use default ACL
    SecurityAttributes.bInheritHandle = TRUE; // Shell will inherit handles

    Result = CreatePipe(&Session->ReadPipeHandle, &ShellStdoutPipe,
                          &SecurityAttributes, 0);
    if (!Result) {
        SessionLog(EVENTLOG_ERROR_TYPE, "Failed to create shell stdout pipe, error = %d", 
                   GetLastError());
        goto Failure;
    }

    Result = CreatePipe(&ShellStdinPipe, &Session->WritePipeHandle,
                        &SecurityAttributes, 0);
    if (!Result) {
        SessionLog(EVENTLOG_ERROR_TYPE, "Failed to create shell stdin pipe, error = %d", 
                   GetLastError());
        goto Failure;
    }

    //
    // Start the shell
    //
    Session->ProcessHandle = StartShell(ShellStdinPipe, ShellStdoutPipe);

    //
    // We're finished with our copy of the shell pipe handles
    // Closing the runtime handles will close the pipe handles for us.
    //
    CloseHandle(ShellStdinPipe);
    CloseHandle(ShellStdoutPipe);

    //
    // Check result of shell start
    //
    if (Session->ProcessHandle == NULL) {
        SessionLog(EVENTLOG_ERROR_TYPE, "Failed to execute shell");
        goto Failure;
    }

    //
    // The session is not connected, initialize variables to indicate that
    //
    Session->ClientSocket = INVALID_SOCKET;

    //
    // Success, return the session pointer as a handle
    //
    return(Session);

Failure:

    //
    // We get here for any failure case.
    // Free up any resources and exit
    //

    if (ShellStdinPipe != NULL) 
        CloseHandle(ShellStdinPipe);
    if (ShellStdoutPipe != NULL) 
        CloseHandle(ShellStdoutPipe);
    if (Session->ReadPipeHandle != NULL) 
        CloseHandle(Session->ReadPipeHandle);
    if (Session->WritePipeHandle != NULL) 
        CloseHandle(Session->WritePipeHandle);

    free(Session);

    return(NULL);
}


// **********************************************************************
// SessionRun
//
// 
// 

BOOL
SessionRun(
    SOCKET  ClientSocket
    )
{
    PSESSION_DATA   Session = CreateSession();
    SECURITY_ATTRIBUTES SecurityAttributes;
    DWORD ThreadId;
    HANDLE HandleArray[3];

    assert(ClientSocket != INVALID_SOCKET);

    SecurityAttributes.nLength = sizeof(SecurityAttributes);
    SecurityAttributes.lpSecurityDescriptor = NULL; // Use default ACL
    SecurityAttributes.bInheritHandle = FALSE; // No inheritance

    //
    // Store the client socket handle in the session structure so the thread
    // can get at it. This also signals that the session is connected.
    //
    Session->ClientSocket = ClientSocket;

    //
    // Create the session threads
    //
    Session->ReadShellThreadHandle = 
        CreateThread(&SecurityAttributes, 0,
                     (LPTHREAD_START_ROUTINE) SessionReadShellThreadFn, 
                     (LPVOID) Session, 0, &ThreadId);

    if (Session->ReadShellThreadHandle == NULL) {
        SessionLog(EVENTLOG_ERROR_TYPE, 
                   "Failed to create ReadShell session thread, error = %d", 
                   GetLastError());

        //
        // Reset the client pipe handle to indicate this session is disconnected
        //
        Session->ClientSocket = INVALID_SOCKET;
        return(FALSE);
    }

    Session->WriteShellThreadHandle = 
        CreateThread(&SecurityAttributes, 0, 
                     (LPTHREAD_START_ROUTINE) SessionWriteShellThreadFn, 
                     (LPVOID) Session, 0, &ThreadId);

    if (Session->WriteShellThreadHandle == NULL) {
        SessionLog(EVENTLOG_ERROR_TYPE, 
                   "Failed to create ReadShell session thread, error = %d", 
                    GetLastError());

        //
        // Reset the client pipe handle to indicate this session is disconnected
        //
        Session->ClientSocket = INVALID_SOCKET;

        TerminateThread(Session->WriteShellThreadHandle, 0);
        return(FALSE);
    }

    //
    // Wait for either thread or the shell process to finish
    //

    HandleArray[0] = Session->ReadShellThreadHandle;
    HandleArray[1] = Session->WriteShellThreadHandle;
    HandleArray[2] = Session->ProcessHandle;

    switch (WaitForMultipleObjects(3, HandleArray, FALSE, 0xffffffff)) {
      case WAIT_OBJECT_0 + 0:
        TerminateThread(Session->WriteShellThreadHandle, 0);
        TerminateProcess(Session->ProcessHandle, 1);
        break;

      case WAIT_OBJECT_0 + 1:
        TerminateThread(Session->ReadShellThreadHandle, 0);
        TerminateProcess(Session->ProcessHandle, 1);
        break;

      case WAIT_OBJECT_0 + 2:
        TerminateThread(Session->WriteShellThreadHandle, 0);
        TerminateThread(Session->ReadShellThreadHandle, 0);
        break;

      default:
        SessionLog(EVENTLOG_ERROR_TYPE, "WaitForMultipleObjects error: %d", 
                   GetLastError());
        break;
    }

    //
    // Close my handles to the threads, the shell process, and the shell pipes

    CloseHandle(Session->ReadShellThreadHandle);
    CloseHandle(Session->WriteShellThreadHandle);
    CloseHandle(Session->ProcessHandle);
    CloseHandle(Session->ReadPipeHandle);
    CloseHandle(Session->WritePipeHandle);

    closesocket(Session->ClientSocket);

    free(Session);

    return(TRUE);
}


// **********************************************************************
//
// StartShell
//
// Execs the shell with the specified handle as stdin, stdout/err
//
// Returns process handle or NULL on failure
//

static HANDLE
StartShell(
    HANDLE ShellStdinPipeHandle,
    HANDLE ShellStdoutPipeHandle
    )
{
    PROCESS_INFORMATION ProcessInformation;
    STARTUPINFO si;
    HANDLE ProcessHandle = NULL;

    //
    // Initialize process startup info
    //
    si.cb = sizeof(STARTUPINFO);
    si.lpReserved = NULL;
    si.lpTitle = NULL;
    si.lpDesktop = NULL;
    si.dwX = si.dwY = si.dwXSize = si.dwYSize = 0L;
    si.wShowWindow = SW_SHOW;
    si.lpReserved2 = NULL;
    si.cbReserved2 = 0;

    si.dwFlags = STARTF_USESTDHANDLES;

    si.hStdInput  = ShellStdinPipeHandle;
    si.hStdOutput = ShellStdoutPipeHandle;

    DuplicateHandle(GetCurrentProcess(), ShellStdoutPipeHandle, 
                    GetCurrentProcess(), &si.hStdError,
                    DUPLICATE_SAME_ACCESS, TRUE, 0);

    if (CreateProcess(NULL, SHELL_COMMAND_LINE, NULL, NULL, TRUE, 0, NULL, NULL,
                      &si, &ProcessInformation)) 
    {
        ProcessHandle = ProcessInformation.hProcess;
        CloseHandle(ProcessInformation.hThread);
    } 
    else 
        SessionLog(EVENTLOG_ERROR_TYPE, "Failed to execute shell, error = %d", 
                   GetLastError());

    return(ProcessHandle);
}


// **********************************************************************
// SessionReadShellThreadFn
//
// The read thread procedure. Reads from the pipe connected to the shell
// process, writes to the socket.
//

static VOID
SessionReadShellThreadFn(
    LPVOID Parameter
    )
{
    PSESSION_DATA Session = Parameter;
    BYTE    Buffer[BUFFER_SIZE];
    BYTE    Buffer2[BUFFER_SIZE+30];
    DWORD   BytesRead;

    while (ReadFile(Session->ReadPipeHandle, Buffer, sizeof(Buffer), 
                    &BytesRead, NULL)) 
    {
        DWORD BufferCnt, BytesToWrite;
        BYTE PrevChar = 0;

        //
        // Process the data we got from the shell:  replace any naked LF's
        // with CR-LF pairs.
        //
        for (BufferCnt = 0, BytesToWrite = 0; BufferCnt < BytesRead; BufferCnt++) {
            if (Buffer[BufferCnt] == '\n' && PrevChar != '\r')
                Buffer2[BytesToWrite++] = '\r';
            PrevChar = Buffer2[BytesToWrite++] = Buffer[BufferCnt];
            assert(BytesToWrite < sizeof Buffer2);
        }

        if (send(Session->ClientSocket, Buffer2, BytesToWrite, 0) <= 0) 
            break;
    }

    if (GetLastError() != ERROR_BROKEN_PIPE)
        SessionLog(EVENTLOG_ERROR_TYPE, "SessionReadShellThreadFn exitted, error = %ld", 
                   GetLastError());
}


// **********************************************************************
// SessionWriteShellThreadFn
//
// The write thread procedure. Reads from socket, writes to pipe connected
// to shell process.  We process certain special character that arrive on
// the socket: backspace (BS), delete (DEL), and ^U are take to be
// line editing controls; ^C is interrupt.
//

#define CHAR_BS		0010
#define CHAR_DEL	0177
#define CHAR_CTRL_C	0003
#define CHAR_CTRL_U	0025

static VOID
SessionWriteShellThreadFn(
    LPVOID Parameter
    )
{
    PSESSION_DATA Session = Parameter;
    BYTE    RecvBuffer[1];
    BYTE    Buffer[BUFFER_SIZE];
    BYTE    EchoBuffer[5];
    DWORD   BytesWritten;
    DWORD   BufferCnt, EchoCnt;
    DWORD   TossCnt = 0;
    BOOL    PrevWasFF = FALSE;

    BufferCnt = 0;

    //
    // Loop, reading one byte at a time from the socket.    
    //
    while (recv(Session->ClientSocket, RecvBuffer, sizeof(RecvBuffer), 0) > 0) {
        //
        // If tossing, then toss.  This is to ignore window size control
        // messages.
        //
        if (TossCnt > 0) {
            TossCnt -= 1;
            continue;
        }

        //
        // Check for window size control message (12 bytes: FF FF ...).
        // We just want to ignore it.  See RFC 1282. 
        //
        if (RecvBuffer[0] != 0xff)
            PrevWasFF = FALSE;
        else {
            if (! PrevWasFF) 
                PrevWasFF = TRUE;
            else {
                TossCnt = 10;
                PrevWasFF = FALSE;
            }
            continue;
        }

        EchoCnt = 0;

        //
        // See if the byte is special
        //

        if (RecvBuffer[0] == CHAR_BS || RecvBuffer[0] == CHAR_DEL) {
            if (BufferCnt > 0) {
                BufferCnt -= 1;
                EchoBuffer[EchoCnt++] = CHAR_BS;
                EchoBuffer[EchoCnt++] = ' ';
                EchoBuffer[EchoCnt++] = CHAR_BS;
            }
        }
        else if (RecvBuffer[0] == CHAR_CTRL_C) {
            GenerateConsoleCtrlEvent(CTRL_C_EVENT, 0);
        }
        else if (RecvBuffer[0] == CHAR_CTRL_U) {
            BufferCnt = 0;
            EchoBuffer[EchoCnt++] = ' ';
            EchoBuffer[EchoCnt++] = 'X';
            EchoBuffer[EchoCnt++] = 'X';
            EchoBuffer[EchoCnt++] = 'X';
            EchoBuffer[EchoCnt++] = '\r';
            EchoBuffer[EchoCnt++] = '\n';
        }
        else {
            Buffer[BufferCnt++] = EchoBuffer[EchoCnt++] = RecvBuffer[0];
            if (RecvBuffer[0] == '\r')
                Buffer[BufferCnt++] = EchoBuffer[EchoCnt++] = '\n';
        }

        //
        // If there's anything to echo back over the socket, do it now.
        //
        if (EchoCnt > 0 && send(Session->ClientSocket, EchoBuffer, EchoCnt, 0) <= 0) 
            break;
        
        //
        // If we got a CR, it's time to send what we've buffered up down to the
        // shell process.
        //
        if (RecvBuffer[0] == '\r') {
            if (! WriteFile(Session->WritePipeHandle, Buffer, BufferCnt, 
                            &BytesWritten, NULL))
            {
                break;
            }
            BufferCnt = 0;
        }
    }
}

