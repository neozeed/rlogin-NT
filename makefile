#
# Copyright (C) 1994 Nathaniel W. Mishkin
# All rights reserved.
#

!include <ntwin32.mak>

#XCDEBUG=$(cdebug)
#XLDEBUG=$(ldebug)

.c.obj:
        $(cc) $(cflags) $(cvarsdll) $(XCDEBUG) /nologo $<

all: rlogind.exe rlogin.exe

rlogind.exe: rlogind.obj session.obj 
        $(link) $(XLDEBUG) $(conlflags) $** wsock32.lib $(conlibsdll) advapi32.lib -out:rlogind.exe

rlogin.exe: rcmd.obj rlogin.obj 
        $(link) $(XLDEBUG) $(conlflags) $** wsock32.lib $(conlibsdll) -out:rlogin.exe

