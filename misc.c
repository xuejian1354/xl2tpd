/*
 * Layer Two Tunnelling Protocol Daemon
 * Copyright (C) 1998 Adtran, Inc.
 * Copyright (C) 2002 Jeff McAdams
 *
 * Mark Spencer
 *
 * This software is distributed under the terms
 * of the GPL, which you should have received
 * along with this source.
 *
 * Miscellaneous but important functions
 *
 */

#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <sys/time.h>
#if defined(SOLARIS)
# include <varargs.h>
#endif
#include <net/if.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <linux/sockios.h>


#include "l2tp.h"

/* prevent deadlock that occurs when a signal handler, which interrupted a
 * call to syslog(), attempts to call syslog(). */
static int syslog_nesting = 0;
#define SYSLOG_CALL(code) do {      \
    if (++syslog_nesting < 2) {     \
        code;                       \
    }                               \
    --syslog_nesting;               \
} while(0)

#define UNUSED(x) (void)(x)

void init_log()
{
    static int logopen=0;
    
    if(!logopen) {
	SYSLOG_CALL( openlog (BINARY, LOG_PID, LOG_DAEMON) );
	logopen=1;
    }
}

char *get_current_time() {
    struct timeval tv; 
    time_t time;
    static char current_time[64];
 
    gettimeofday (&tv, NULL);
 
    time = tv.tv_sec;
    bzero(current_time, sizeof(current_time));
    struct tm* p_time = localtime(&time); 
    strftime(current_time, 100, "%Y-%m-%dT%H:%M:%S", p_time);  
    sprintf(current_time + strlen(current_time), ".%ld", tv.tv_usec/1000);
 
    return current_time;
}

char *get_mac_addr(char *macdev) {
    static char macaddr[64];
    struct ifreq ifreq;
    int sock;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        return NULL;
    }

    strcpy(ifreq.ifr_name, macdev);

    if (ioctl(sock, SIOCGIFHWADDR, &ifreq) < 0) {
        return NULL;
    }

    memset(macaddr, 0, sizeof(macaddr));
    sprintf(macaddr, "%02X%02X%02X%02X%02X%02X",
                (_u8) ifreq.ifr_hwaddr.sa_data[0],
                (_u8) ifreq.ifr_hwaddr.sa_data[1],
                (_u8) ifreq.ifr_hwaddr.sa_data[2],
                (_u8) ifreq.ifr_hwaddr.sa_data[3],
                (_u8) ifreq.ifr_hwaddr.sa_data[4],
                (_u8) ifreq.ifr_hwaddr.sa_data[5]);
    if (!strcmp(macaddr, "000000000000")) {
        return NULL;
    }

    return macaddr;
}

void logrecord(const char *fmt, ...)
{
    char buf[2048];
    va_list args;
    va_start (args, fmt);
    vsnprintf (buf, sizeof (buf), fmt, args);
    va_end (args);

    //TLPRINT("%s", buf);
}

void l2tp_log (int level, const char *fmt, ...)
{
    char buf[2048];
    va_list args;
    va_start (args, fmt);
    vsnprintf (buf, sizeof (buf), fmt, args);
    va_end (args);
    
    if(gconfig.syslog) {
	init_log();
	SYSLOG_CALL( syslog (level, "%s", buf) );
    } else {
	fprintf(stderr, "ml2tpd[%d]: %s", getpid(), buf);
    }

    if(gconfig.connect_lns)
    {
        logrecord("[%s] [ml2tpd] - gw %s %s",
                        get_current_time(),
                        get_mac_addr(gconfig.macdev),
                        buf);
    }
}

void set_error (struct call *c, int error, const char *fmt, ...)
{
    va_list args;
    va_start (args, fmt);
    c->error = error;
    c->result = RESULT_ERROR;
    c->needclose = -1;
    vsnprintf (c->errormsg, sizeof (c->errormsg), fmt, args);
    if (c->errormsg[strlen (c->errormsg) - 1] == '\n')
        c->errormsg[strlen (c->errormsg) - 1] = 0;
    va_end (args);
}

struct buffer *new_buf (int size)
{
    struct buffer *b = NULL;

    if (!size || size < 0)
        return NULL;

    b = malloc (sizeof (struct buffer));
    if (!b)
        return NULL;

    b->rstart = malloc (size);
    if (!b->rstart)
    {
        free (b);
        return NULL;
    }
    b->start = b->rstart;
    b->rend = b->rstart + size - 1;
    b->len = size;
    b->maxlen = size;
    return b;
}

inline void recycle_buf (struct buffer *b)
{
    b->start = b->rstart;
    b->len = b->maxlen;
}

#define bufferDumpWIDTH 16
void bufferDump (unsigned char *buf, int buflen)
{
    int i = 0, j = 0;
    /* we need TWO characters to DISPLAY ONE byte */
    char line[2 * bufferDumpWIDTH + 1], *c;

    for (i = 0; i < buflen / bufferDumpWIDTH; i++)
    {
        c = line;
        for (j = 0; j < bufferDumpWIDTH; j++)
        {
	  sprintf (c, "%02x", (buf[i * bufferDumpWIDTH + j]) & 0xff);
            c++;
            c++;                /* again two characters to display ONE byte */
        }
        *c = '\0';
        l2tp_log (LOG_WARNING,
		  "%s: buflen=%d, buffer[%d]: *%s*\n", __FUNCTION__,
		  buflen, i, line);
    }

    c = line;
    for (j = 0; j < buflen % bufferDumpWIDTH; j++)
    {
        sprintf (c, "%02x",
                 buf[(buflen / bufferDumpWIDTH) * bufferDumpWIDTH +
                     j] & 0xff);
        c++;
        c++;
    }
    if (c != line)
    {
        *c = '\0';
        l2tp_log (LOG_WARNING,
		  "%s:             buffer[%d]: *%s*\n", __FUNCTION__, i,
		  line);
    }
}

void do_packet_dump (struct buffer *buf)
{
    size_t x;
    unsigned char *c = buf->start;
    printf ("packet dump: \nHEX: { ");
    for (x = 0; x < buf->len; x++)
    {
        printf ("%.2X ", *c);
        c++;
    };
    printf ("}\nASCII: { ");
    c = buf->start;
    for (x = 0; x < buf->len; x++)
    {
        if (*c > 31 && *c < 127)
        {
            putchar (*c);
        }
        else
        {
            putchar (' ');
        }
        c++;
    }
    printf ("}\n");
}

void swaps (void *buf_v, int len)
{
#ifdef __alpha
    /* Reverse byte order alpha is little endian so lest save a step.
       to make things work out easier */
    int x;
    unsigned char t1;
    unsigned char *tmp = (_u16 *) buf_v;
    for (x = 0; x < len; x += 2)
    {
        t1 = tmp[x];
        tmp[x] = tmp[x + 1];
        tmp[x + 1] = t1;
    }
#else

    /* Reverse byte order (if proper to do so) 
       to make things work out easier */
    int x;
	struct hw { _u16 s; } __attribute__ ((packed)) *p = (struct hw *) buf_v;
	for (x = 0; x < len / 2; x++, p++)
		p->s = ntohs(p->s); 
#endif
}



inline void toss (struct buffer *buf)
{
    /*
     * Toss a frame and free up the buffer that contained it
     */

    free (buf->rstart);
    free (buf);
}

inline void safe_copy (char *a, char *b, int size)
{
    /* Copies B into A (assuming A holds MAXSTRLEN bytes)
       safely */
    strncpy (a, b, MIN (size, MAXSTRLEN - 1));
    a[MIN (size, MAXSTRLEN - 1)] = '\000';
}

struct ppp_opts *add_opt (struct ppp_opts *option, char *fmt, ...)
{
    va_list args;
    struct ppp_opts *new, *last;
    new = malloc (sizeof (struct ppp_opts));
    if (!new)
    {
        l2tp_log (LOG_WARNING,
		  "%s : Unable to allocate ppp option memory.  Expect a crash\n",
		  __FUNCTION__);
        return option;
    }
    new->next = NULL;
    va_start (args, fmt);
    vsnprintf (new->option, sizeof (new->option), fmt, args);
    va_end (args);
    if (option)
    {
        last = option;
        while (last->next)
            last = last->next;
        last->next = new;
        return option;
    }
    else
        return new;
}
void opt_destroy (struct ppp_opts *option)
{
    struct ppp_opts *tmp;
    while (option)
    {
        tmp = option->next;
        free (option);
        option = tmp;
    };
}

int get_egd_entropy(char *buf, int count)
{
    UNUSED(buf);
    UNUSED(count);
    return -1;
}

int get_sys_entropy(unsigned char *buf, int count)
{
    /*
     * This way of filling buf with rand() generated data is really
     * fairly inefficient from a function call point of view...rand()
     * returns four bytes of data (on most systems, sizeof(int))
     * and we end up only using 1 byte of it (sizeof(char))...ah
     * well...it was a *whole* lot easier to code this way...suggestions
     * for improvements are, of course, welcome
     */
    int counter;
    for (counter = 0; counter < count; counter++)
    {
        buf[counter] = (char)rand();
    }
#ifdef DEBUG_ENTROPY
    bufferDump (buf, count);
#endif
    return count;
}

int get_dev_entropy(unsigned char *buf, int count)
{
    int devrandom;
    ssize_t entropy_amount;

    devrandom = open ("/dev/urandom", O_RDONLY | O_NONBLOCK);
    if (devrandom == -1)
    {
#ifdef DEBUG_ENTROPY
        l2tp_log(LOG_WARNING, "%s: couldn't open /dev/urandom,"
                      "falling back to rand()\n",
                      __FUNCTION__);
#endif
        return get_sys_entropy(buf, count);
    }
    entropy_amount = read(devrandom, buf, count);
    close(devrandom);
    return entropy_amount;
}

int get_entropy (unsigned char *buf, int count)
{
    if (rand_source == RAND_SYS)
    {
        return get_sys_entropy(buf, count);
    }
    else if (rand_source == RAND_DEV)
    {
        return get_dev_entropy(buf, count);
    }
    else if (rand_source == RAND_EGD)
    {
        l2tp_log(LOG_WARNING,
		 "%s: EGD Randomness source not yet implemented\n",
                __FUNCTION__);
        return -1;
    }
    else
    {
	    l2tp_log(LOG_WARNING,
		     "%s: Invalid Randomness source specified (%d)\n",
		     __FUNCTION__, rand_source);
	    return -1;
    }
}
