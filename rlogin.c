/*
 * Copyright (C) 1994 Nathaniel W. Mishkin
 * All rights reserved.
 */

/*
 * Copyright (C) 1983, 1990 The Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */


/*
 * rlogin - remote login
 */

#include <windows.h>
#include <winsock.h>
#include <process.h>
#include <io.h>
#include <fcntl.h>

#include <stdlib.h>
#include <setjmp.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

int eight, litout, rem;

int noescape;
u_char escapechar = '~';

rcmd(char **ahost, 
     u_short rport, 
     const char *locuser, 
     const char *remuser, 
     const char *cmd,
     int *fd2p);

void usage(void);
void doit(void);
void msg(char *);
void writer(void);
void done(int);
void echo(register char);

#define STDIN_FILENO 0
#define STDOUT_FILENO 1

main(argc, argv)
	int argc;
	char **argv;
{
	struct servent *sp;
	char *host, *user;
	u_char getescape();
	char *getenv();
        WSADATA wsa_data;
        DWORD err;

        user = getenv("USERNAME");
	host = argv[1];

        if ((err = WSAStartup(MAKEWORD(1, 1), &wsa_data)) != 0) {
            fprintf(stderr, "WSAStartup\n");
            exit(1);            
        }

	sp = NULL;
	if (sp == NULL)
		sp = getservbyname("login", "tcp");
	if (sp == NULL) {
		(void)fprintf(stderr, "rlogin: login/tcp: unknown service.\n");
		exit(1);
	}

	rem = rcmd(&host, sp->s_port, user, user, "dumb", 0);

	if (rem < 0)
		exit(1);

	doit();
        return 0;
}

void 
doit(void)
{
        int child;
        HANDLE coutput, cinput;
        void reader(void *);

        coutput = GetStdHandle(STD_OUTPUT_HANDLE);
        cinput  = GetStdHandle(STD_INPUT_HANDLE);

        SetConsoleMode(cinput, 0);
        SetConsoleMode(coutput, ENABLE_PROCESSED_OUTPUT);
        _setmode(0, _O_BINARY);

        if ((child = _beginthread(reader, 0, NULL)) == -1) {
		(void)fprintf(stderr, "rlogin: _beginthread: %s.\n", strerror(errno));
		done(1);
	}
	writer();
	msg("closed connection.");
	done(0);
}

void
done(status)
	int status;
{
	exit(status);
}


/*
 * writer: write to remote: 0 -> line.
 * ~.				terminate
 * ~^Z				suspend rlogin process.
 * ~<delayed-suspend char>	suspend rlogin process, but leave reader alone.
 */
void 
writer()
{
	register int bol, local, n;
	char c;

	bol = 1;			/* beginning of line */
	local = 0;
	for (;;) {
		n = read(STDIN_FILENO, &c, 1);
		if (n <= 0) {
			if (n < 0 && errno == EINTR)
				continue;
			break;
		}
		/*
		 * If we're at the beginning of the line and recognize a
		 * command character, then we echo locally.  Otherwise,
		 * characters are echo'd remotely.  If the command character
		 * is doubled, this acts as a force and local echo is
		 * suppressed.
		 */
		if (bol) {
			bol = 0;
			if (!noescape && c == escapechar) {
				local = 1;
				continue;
			}
		} else if (local) {
			local = 0;
			if (c == '.') {
				echo(c);
				break;
			}
			if (c != escapechar)
					(void)send(rem, &escapechar, 1, 0);
		}

		if (send(rem, &c, 1, 0) == 0) {
			msg("line gone");
			break;
		}
		bol = c == '\r' || c == '\n';
	}
}

void 
echo(c)
register char c;
{
	register char *p;
	char buf[8];

	p = buf;
	c &= 0177;
	*p++ = escapechar;
	if (c < ' ') {
		*p++ = '^';
		*p++ = c + '@';
	} else if (c == 0177) {
		*p++ = '^';
		*p++ = '?';
	} else
		*p++ = c;
	*p++ = '\r';
	*p++ = '\n';
	(void)write(STDOUT_FILENO, buf, p - buf);
}

#if 0
/*
 * Send the window size to the server via the magic escape
 */
sendwindow()
{
	struct winsize *wp;
	char obuf[4 + sizeof (struct winsize)];

	wp = (struct winsize *)(obuf+4);
	obuf[0] = 0377;
	obuf[1] = 0377;
	obuf[2] = 's';
	obuf[3] = 's';
	wp->ws_row = htons(winsize.ws_row);
	wp->ws_col = htons(winsize.ws_col);
	wp->ws_xpixel = htons(winsize.ws_xpixel);
	wp->ws_ypixel = htons(winsize.ws_ypixel);

	(void)send(rem, obuf, sizeof(obuf), 0);
}

#endif

/*
 * reader: read from remote: line -> 1
 */
#define	READING	1
#define	WRITING	2

jmp_buf rcvtop;
int ppid, rcvcnt, rcvstate;
char rcvbuf[8 * 1024];

#if 0

void
oob()
{
	struct sgttyb sb;
	int atmark, n, out, rcvd;
	char waste[BUFSIZ], mark;

	out = O_RDWR;
	rcvd = 0;
	while (recv(rem, &mark, 1, MSG_OOB) < 0)
		switch (WSAGetLastError()) {
		case WSAEWOULDBLOCK:
			/*
			 * Urgent data not here yet.  It may not be possible
			 * to send it yet if we are blocked for output and
			 * our input buffer is full.
			 */
			if (rcvcnt < sizeof(rcvbuf)) {
				n = recv(rem, rcvbuf + rcvcnt,
				    sizeof(rcvbuf) - rcvcnt, 0);
				if (n <= 0)
					return;
				rcvd += n;
			} else {
				n = recv(rem, waste, sizeof(waste), 0);
				if (n <= 0)
					return;
			}
			continue;
		default:
			return;
	}
	if (mark & TIOCPKT_WINDOW) {
		/* Let server know about window size changes */
		(void)kill(ppid, SIGUSR1);
	}
	if (!eight && (mark & TIOCPKT_NOSTOP)) {
		(void)ioctl(0, TIOCGETP, (char *)&sb);
		sb.sg_flags &= ~CBREAK;
		sb.sg_flags |= RAW;
		(void)ioctl(0, TIOCSETN, (char *)&sb);
		notc.t_stopc = -1;
		notc.t_startc = -1;
		(void)ioctl(0, TIOCSETC, (char *)&notc);
	}
	if (!eight && (mark & TIOCPKT_DOSTOP)) {
		(void)ioctl(0, TIOCGETP, (char *)&sb);
		sb.sg_flags &= ~RAW;
		sb.sg_flags |= CBREAK;
		(void)ioctl(0, TIOCSETN, (char *)&sb);
		notc.t_stopc = deftc.t_stopc;
		notc.t_startc = deftc.t_startc;
		(void)ioctl(0, TIOCSETC, (char *)&notc);
	}
	if (mark & TIOCPKT_FLUSHWRITE) {
		(void)ioctl(1, TIOCFLUSH, (char *)&out);
		for (;;) {
			if (ioctl(rem, SIOCATMARK, &atmark) < 0) {
				(void)fprintf(stderr, "rlogin: ioctl: %s.\n",
				    strerror(errno));
				break;
			}
			if (atmark)
				break;
			n = recv(rem, waste, sizeof (waste), 0);
			if (n <= 0)
				break;
		}
		/*
		 * Don't want any pending data to be output, so clear the recv
		 * buffer.  If we were hanging on a write when interrupted,
		 * don't want it to restart.  If we were reading, restart
		 * anyway.
		 */
		rcvcnt = 0;
		longjmp(rcvtop, 1);
	}

	/* oob does not do FLUSHREAD (alas!) */

	/*
	 * If we filled the receive buffer while a read was pending, longjmp
	 * to the top to restart appropriately.  Don't abort a pending write,
	 * however, or we won't know how much was written.
	 */
	if (rcvd && rcvstate == READING)
		longjmp(rcvtop, 1);
}

#endif

/* reader: read from remote: line -> 1 */
void
reader(void *p)
{
	void oob();

	int pid = getpid();
	int n, remaining;
	char *bufp = rcvbuf;

	(void)setjmp(rcvtop);
	for (;;) {
		while ((remaining = rcvcnt - (bufp - rcvbuf)) > 0) {
			rcvstate = WRITING;
			n = write(STDOUT_FILENO, bufp, remaining);
			if (n < 0) {
				if (errno != EINTR)
					return;
				continue;
			}
			bufp += n;
		}
		bufp = rcvbuf;
		rcvcnt = 0;
		rcvstate = READING;

		rcvcnt = recv(rem, rcvbuf, sizeof (rcvbuf), 0);
		if (rcvcnt == 0)
			return;
		if (rcvcnt < 0) {
			(void)fprintf(stderr, "rlogin: read: %d.\n",
			              WSAGetLastError());
			return;
		}
	}
}

void
msg(str)
	char *str;
{
	(void)fprintf(stderr, "rlogin: %s\r\n", str);
}


void
usage()
{
	(void)fprintf(stderr,
	    "usage: rlogin [ -%s]%s[-e char] [ -l username ] host\n",
	    "8EL", " ");
	exit(1);
}


u_char
getescape(p)
	register char *p;
{
	long val;
	int len;

	if ((len = strlen(p)) == 1)	/* use any single char, including '\' */
		return((u_char)*p);
					/* otherwise, \nnn */
	if (*p == '\\' && len >= 2 && len <= 4) {
		val = strtol(++p, (char **)NULL, 8);
		for (;;) {
			if (!*++p)
				return((u_char)val);
			if (*p < '0' || *p > '8')
				break;
		}
	}
	msg("illegal option value -- e");
	usage();
	/* NOTREACHED */
}
