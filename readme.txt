"rlogind" and "rlogin" for Windows NT / Version 1.0
Nathaniel Mishkin <mishkin@atria.com>
18 February 1994

This directory contains implementations of a client and server for the
TCP/IP "rlogin" facility (called "rlogin" and "rlogind") for Windows NT.
Both source and executables (the latter for the x86 platform only) are
provided. 

Both "rlogin" and "rlogind" are hack jobs (esp. the latter), but they
get me by.  I'm distributing the source code in case anyone wants to
improve them.  At least for now, I'll act as a clearinghouse for any
changes people want to pass back to me. 


rlogind
-------

Needless to say (I'd hope), all you can run is console mode apps.
(Actually, you can run GUI apps, but I'll give you two guesses on whose
the display the GUI presentation appears.) Further, the console mode
apps must run in default (cooked) mode.  Apps see input a line at a
time.  "rlogind" implements echoing and line editing--it handles ^U,
DEL, and ^H.  "rlogind" attempts to turn ^C's it sees in the TCP stream
into quits on the process group running under its control. 

"rlogind" must itself be run in a console.  I spent some time trying to
make it run as a service but gave up.  There are some unfortunate
properties of console mode that make use of a service unworkable.

You can't invoke DOS apps inside of a "rlogin" session.  I don't know
what the problems are, but I can imagine. 

Some programs explicitly open "the console" (the file named "con").  If
they do this, they'll end up talking to the console in which "rlogind"
was invoked.  Probably not what you want.  An example of this is the
Windows NT FTP client program, which appears to open "con" for the
purposes of reading passwords in no-echo mode. 

"rlogind" does incredibly primitive authentication.  It checks the name
of the remote host against a list of authorized hosts in a file named
"hosts.eqv".  Further, the "local user name" passed by the "rlogin"
client must match the name of the user that started "rlogind".  You can
defeat all this by giving the "-i" command line switch to "rlogind".  In
this case, "rlogind" lets anyone in. 


rlogin
------

There are actually a number of good "rlogin" clients for Windows NT.
The one I use most often is QPC Software's WinQVT/Net for Windows NT.
However, I thought I might sometime have occasion to want a console-mode
"rlogin" client (e.g., to get the benefit of the long console output
scrollback), so I ported the standard Berkeley one.  I haven't used it
much, but it seems to work OK. 

