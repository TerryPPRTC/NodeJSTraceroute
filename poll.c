/*
    Copyright (c)  2006, 2007		Dmitry Butskoy
					<buc@citadel.stu.neva.ru>
    License:  GPL v2 or any later

    See COPYING for the status of this software.
*/

#include <stdlib.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

double get_time (void) {
	struct timeval tv;
	double d;

	gettimeofday (&tv, NULL);
	d = ((double) tv.tv_usec) / 1000000. + (unsigned long) tv.tv_sec;
	return d;
}

static struct pollfd *pfd = NULL;
static unsigned int num_polls = 0;

void add_poll (int fd, int events) {
	int i;

	for (i = 0; i < num_polls && pfd[i].fd > 0; i++) ;

	if (i == num_polls) {
	    pfd = realloc (pfd, ++num_polls * sizeof (*pfd));
	    if (!pfd)
			return;
	}

	pfd[i].fd = fd;
	pfd[i].events = events;
}


void del_poll (int fd) {
	int i;

	for (i = 0; i < num_polls && pfd[i].fd != fd; i++) ;

	if (i < num_polls)  pfd[i].fd = -1;    /*  or just zero it...  */
}

static int cleanup_polls (void) {
	int i;

	for (i = 0; i < num_polls && pfd[i].fd > 0; i++) ;

	if (i < num_polls) {	/*  a hole have found   */
	    int j;

	    for (j = i + 1; j < num_polls; j++) {
		if (pfd[j].fd > 0) {
		    pfd[i++] = pfd[j];
		    pfd[j].fd = -1;
		}
	    }
	}

	return i;
}

void close_polls (void)
{
	int i;

	for (i = 0; i < num_polls ; i++)
    {
        if(pfd[i].fd > 0)
            close(pfd[i].fd);
    }
}

void do_poll (double timeout, void (*callback) (int fd, int revents, void* data), void* data) {
	int nfds;
	int msecs = ceil (timeout * 1000/5);
    double t2;
    double t1 = get_time();
	while ((nfds = cleanup_polls ()) > 0) {
	    int i, n;

//printf("will wait for %d\n", msecs);

 		t2 = get_time();
        if(t2-t1 > timeout)
            break;//total time exceed timeout, just end the poll

	    n = poll (pfd, nfds, msecs);
//printf("poll return ==> %d/%d\n", n, nfds);

	    if (n <= 0) {
            if ( errno == EINTR) //n == 0 ||
                return;
			continue;
	    }

	    for (i = 0; n && i < num_polls; i++) {
            if (pfd[i].revents) {
                callback (pfd[i].fd, pfd[i].revents, data);
                n--;
            }
	    }

	}

	return;
}


