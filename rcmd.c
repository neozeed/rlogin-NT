/*
 * Copyright (C) 1994 Nathaniel W. Mishkin.
 * All rights reserved.
 */

/*
 * Copyright (C) 1983 Regents of the University of California.
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

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)rcmd.c	5.24 (Berkeley) 2/24/91";
#endif /* LIBC_SCCS and not lint */

#include <winsock.h>
#include <windows.h>
#include <io.h>

#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <ctype.h>
#include <string.h>

rresvport(int *alport);

rcmd(ahost, rport, locuser, remuser, cmd, fd2p)
	char **ahost;
	u_short rport;
	const char *locuser, *remuser, *cmd;
	int *fd2p;
{
	int s, timo = 1;
	struct sockaddr_in sin, from;
	char c;
	int lport = IPPORT_RESERVED - 1;
	struct hostent *hp;
	fd_set reads;

	hp = gethostbyname(*ahost);
	if (hp == 0) {
		fprintf(stderr, "gethostbyname: %s\n", *ahost);
		return (-1);
	}
	*ahost = hp->h_name;
	for (;;) {
		s = rresvport(&lport);
		if (s < 0) {
			if (WSAGetLastError() == EAGAIN)
				fprintf(stderr, "socket: All ports in use\n");
			else
				perror("rcmd: socket");
			return (-1);
		}
		sin.sin_family = hp->h_addrtype;
		memcpy((void *)&sin.sin_addr, hp->h_addr_list[0], hp->h_length);
		sin.sin_port = rport;
		if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) >= 0)
			break;
		(void) closesocket(s);
		if (WSAGetLastError() == WSAEADDRINUSE) {
			lport--;
			continue;
		}
		if (WSAGetLastError() == WSAECONNREFUSED && timo <= 16) {
			Sleep(timo * 1000);
			timo *= 2;
			continue;
		}
		if (hp->h_addr_list[1] != NULL) {
			int oerrno = WSAGetLastError();

			fprintf(stderr,
			    "connect to address %s: ", inet_ntoa(sin.sin_addr));
			WSASetLastError(oerrno);
			perror(0);
			hp->h_addr_list++;
			memcpy((void *)&sin.sin_addr, hp->h_addr_list[0], 
			    hp->h_length);
			fprintf(stderr, "Trying %s...\n",
				inet_ntoa(sin.sin_addr));
			continue;
		}
		perror(hp->h_name);
		return (-1);
	}
	lport--;
	if (fd2p == 0) {
		send(s, "", 1, 0);
		lport = 0;
	} else {
		char num[8];
		int s2 = rresvport(&lport), s3;
		int len = sizeof (from);

		if (s2 < 0)
			goto bad;
		listen(s2, 1);
		(void) sprintf(num, "%d", lport);
		if (send(s, num, strlen(num)+1, 0) != (int) strlen(num)+1, 0) {
			perror("write: setting up stderr");
			(void) closesocket(s2);
			goto bad;
		}
		FD_ZERO(&reads);
		FD_SET(s, &reads);
		FD_SET(s2, &reads);
		WSASetLastError(0);
		if (select(32, &reads, 0, 0, 0) < 1 ||
		    !FD_ISSET(s2, &reads)) {
			if (WSAGetLastError() != 0)
				perror("select: setting up stderr");
			else
			    fprintf(stderr,
				"select: protocol failure in circuit setup.\n");
			(void) closesocket(s2);
			goto bad;
		}
		s3 = accept(s2, (struct sockaddr *)&from, &len);
		(void) closesocket(s2);
		if (s3 < 0) {
			perror("accept");
			lport = 0;
			goto bad;
		}
		*fd2p = s3;
		from.sin_port = ntohs((u_short)from.sin_port);
		if (from.sin_family != AF_INET ||
		    from.sin_port >= IPPORT_RESERVED ||
		    from.sin_port < IPPORT_RESERVED / 2) {
			fprintf(stderr,
			    "socket: protocol failure in circuit setup.\n");
			goto bad2;
		}
	}
	(void) send(s, locuser, strlen(locuser)+1, 0);
	(void) send(s, remuser, strlen(remuser)+1, 0);
	(void) send(s, cmd, strlen(cmd)+1, 0);
	if (recv(s, &c, 1, 0) != 1) {
		perror(*ahost);
		goto bad2;
	}
	if (c != 0) {
		while (recv(s, &c, 1, 0) == 1) {
			(void) write(2, &c, 1);
			if (c == '\n')
				break;
		}
		goto bad2;
	}
	return (s);
bad2:
	if (lport)
		(void) closesocket(*fd2p);
bad:
	(void) closesocket(s);
	return (-1);
}

rresvport(alport)
	int *alport;
{
	struct sockaddr_in sin;
	int s;

	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = INADDR_ANY;
	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0)
		return (-1);
	for (;;) {
		sin.sin_port = htons((u_short)*alport);
		if (bind(s, (struct sockaddr *)&sin, sizeof (sin)) >= 0)
			return (s);
		if (WSAGetLastError() != WSAEADDRINUSE) {
			(void) closesocket(s);
			return (-1);
		}
		(*alport)--;
		if (*alport == IPPORT_RESERVED/2) {
			(void) closesocket(s);
			WSASetLastError(EAGAIN);		/* close */
			return (-1);
		}
	}
}
