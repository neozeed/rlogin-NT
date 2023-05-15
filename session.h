/*
 * Copyright (C) 1994 Nathaniel W. Mishkin
 * All rights reserved.
 */

#include <windows.h>
#include <stdio.h>
#include <assert.h>
#include <io.h>

//
// Function protoypes
//

BOOL
SessionRun(
    SOCKET  ClientSocket
    );

VOID
SessionLog(
    WORD EventType,
    char *fmt,
    ...
);
