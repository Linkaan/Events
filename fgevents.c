/*
 *  fagelmatare-serializer
 *    Library used for event-based communication between master and slave
 *  fgevents.c
 *    Routines for sending/receiving events between client and server The data
 *    is sent via events which are serialized and delimited with STX/ETX
 *    control characters. Preceding the ETX delimiter is a 2-byte checksum to
 *    verify the integrity of an event.

 *    This is the layout of a serialized event:
 *    
 *      1 - STX
 *      4 - id
 *      1 - writeback
 *      4 - length
 *      ? - payload
 *      2 - CRC-9
 *      1 - ETX
 *      
 *****************************************************************************
 *  This file is part of Fågelmataren, an embedded project created to learn
 *  Linux and C. See <https://github.com/Linkaan/Fagelmatare>
 *  Copyright (C) 2015-2017 Linus Styrén
 *
 *  Fågelmataren is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 3 of the Licence, or
 *  (at your option) any later version.
 *
 *  Fågelmataren is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public Licence for more details.
 *
 *  You should have received a copy of the GNU General Public Licence
 *  along with Fågelmataren.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************
 */

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/event-config.h>
#include <event2/thread.h>

#include <serializer.h>

#include "fgevents.h"

/* Temporary ugly log error macros before fgutil library is done */
#define report_error(etdata, msg)\
        do\
          {\
            etdata->save_errno = errno;\
            snprintf (etdata->error, sizeof etdata->error,\
                      "fgevents: %s: %d: %s: %s\n",\
                      __FILE__, __LINE__, msg, strerror (errno));\
            etdata->cb (itdata->user_data, NULL, NULL);\
          } while (0)

#define report_error_noen(etdata, msg)\
        do\
          {\
            etdata->save_errno = 0;\
            snprintf (etdata->error, sizeof etdata->error,\
                      "fgevents: %s: %d: %s\n",\
                      __FILE__, __LINE__, msg);\
            itdata->cb (itdata->user_data, NULL, NULL);\
          } while (0)

#define report_error_en(etdata, en, msg)\
        do { errno = en;report_error (etdata, msg); } while (0)

/* Forward declarations used in this file. */
static void add_bev (struct fg_events_data *, struct bufferevent *);
static void remove_bev (struct fg_events_data *, struct bufferevent *);

/* Helper function to set tcp no delay on socket to disable
   packet-accumulation delay */
static void set_tcp_no_delay (evutil_socket_t fd)
{
    int one = 1;
    setsockopt (fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
}

/* Helper functions to suppress and restore SIGPIPE */

static inline void
suppress_sigpipe (struct fg_events_data *etdata)
{
    sigset_t pending;

    sigemptyset (&pending);
    sigpending (&pending);
    etdata->sigpipe_pending = sigismember (&pending, SIGPIPE);
    if (!etdata->sigpipe_pending)
      {
        sigset_t blocked;
        sigemptyset (&blocked);
        pthread_sigmask (SIG_BLOCK, 0, &blocked);

        /* Maybe is was blocked already?  */
        etdata->sigpipe_unblock = !sigismember (&blocked, SIGPIPE);
      }
}

static inline void
restore_sigpipe (struct fg_events_data *etdata)
{
    if (!etdata->sigpipe_pending)
      {
        sigset_t pending;

        sigemptyset (&pending);
        sigpending (&pending);
        if (sigismember (&pending, SIGPIPE))
          {          
            static const struct timespec nowait = { 0, 0 };

            sigtimedwait (0, NULL, &nowait);
          }

        if (etdata->sigpipe_unblock)
            pthread_sigmask (SIG_UNBLOCK, 0, NULL);
      }
}


/* Helper function to allocate memory for buffer and copy evbuffer to it */
static ssize_t
copy_evbuffer_into_buffer (struct evbuffer *evbuf, unsigned char **buf)
{
    size_t len;
    unsigned char *buffer;

    len = evbuffer_get_length (evbuf);
    buffer = malloc (len);
    if (buffer == NULL)
        return -1;

    evbuffer_copyout (evbuf, buffer, len);
    *buf = buffer;

    return len;
}

int
fg_parse_fgevent (struct fgevent *fgev, unsigned char *buffer,
               size_t len, unsigned char **p)
{
    int s;
    unsigned char *ptr = *p;

    while ((size_t)(ptr - buffer) < len && ptr[0] != 0x02) // STX
            ptr++;

    // check if buffer is empty
    if ((size_t)(ptr - buffer) >= len)
      {
        *p = ptr;
        return 0;
      }

    /* Deserialize fgevent to fgev struct. If it fails to allocate memory we
       increment by payload length */
    ptr = deserialize_fgevent (++ptr, fgev);
    s = fgev->length > 0 && !fgev->payload;
    if (s)
        ptr += fgev->length * sizeof (fgev->payload[0]);

    while ((size_t)(ptr - buffer) < len && ptr[0] != 0x03) // ETX
        ptr++;

    *p = ptr;

    if (s)
        return -1;

    return ptr - buffer;
}

static int
create_serialized_fgevent_buffer (unsigned char **buf, struct fgevent *fgev)
{
    unsigned char *buffer;    
    size_t nbytes;

    nbytes = 2; // for STX and ETX delimiter
    nbytes += sizeof (fgev->id);
    nbytes += sizeof (fgev->writeback);
    nbytes += sizeof (fgev->length);
    if (fgev->length > 0)
        nbytes += fgev->length * sizeof (fgev->payload[0]);

    buffer = malloc (nbytes);
    if (!buffer)
        return -1;

    buffer[0] = 0x02; // STX
    serialize_fgevent (buffer+1, fgev);
    buffer[nbytes-1] = 0x03; // ETX

    *buf = buffer;

    return nbytes;
}


static void
fg_read_cb (struct bufferevent *bev, void *arg)
{
    ssize_t s;
    size_t len;
    unsigned char *buffer, *ptr;
    struct fg_events_data *itdata = arg;

    s = copy_evbuffer_into_buffer (bufferevent_get_input (bev), &buffer);
    if (s < 0)
      {
        report_error (itdata, "in function fg_read_cb malloc failed");
        return;
      }
    len = s;
    
    ptr = buffer;
    while ((size_t)(ptr - buffer) < len)
      {        
        struct fgevent fgev, ansev;
        int writeback;

        s = fg_parse_fgevent (&fgev, buffer, len, &ptr);
        if (s < 0)
          {
            report_error (itdata,
                          "in function fg_read_cb parse_fgevent failed");
            continue;
          }
        else if (s == 0) // empty event
          {
            continue;
          }
        
        writeback = itdata->cb (itdata->user_data, &fgev, &ansev);
        if (writeback)
          {
            unsigned char *fgbuf;

            s = create_serialized_fgevent_buffer (&fgbuf, &ansev);
            if (s < 0)
                report_error (itdata,
                              "create_serialized_fgevent_buffer failed");
            else
              {
                struct evbuffer *output;

                output = bufferevent_get_output (bev);
                evbuffer_lock (output);
                suppress_sigpipe (itdata);
                s = bufferevent_write (bev, fgbuf, s);                
                itdata->save_errno = errno;
                restore_sigpipe (itdata);
                evbuffer_unlock (output);

                free (fgbuf);

                if (s < 0)
                    report_error (itdata, "bufferevent_write failed");
              }        
          }

        if (fgev.length > 0)
            free (fgev.payload);
      }

    free (buffer);
}

static void
fg_write_cb (struct bufferevent *bev, void *arg)
{
    struct evbuffer *output = bufferevent_get_output (bev);

    /* writeback flushed */
    if (evbuffer_get_length (output) == 0) {
        //bufferevent_free (bev);
        fprintf(stdout, "[DEBUG] in function fg_write_cb: writeback flushed\n");
    }
}

static void
fg_event_client_cb (struct bufferevent *bev, short events, void *arg)
{
    struct fg_events_data *itdata = arg;

    if (events & BEV_EVENT_CONNECTED)
      {
        fprintf(stdout, "[DEBUG] in function fg_event_client_cb: BEV_EVENT_CONNECTED\n");
        evutil_socket_t fd = bufferevent_getfd (bev);
        set_tcp_no_delay (fd);
      }
    if (events & BEV_EVENT_ERROR)
        report_error (itdata, "in function fg_event_client_cb");

    if (events & BEV_EVENT_EOF)
        fprintf (stdout, "[DEBUG] in function fg_event_client_cb: BEV_EVENT_EOF\n");

    fprintf (stdout, "[DEBUG] client events is %d\n", events);
}

static void
fg_event_server_cb (struct bufferevent *bev, short events, void *arg)
{
    struct fg_events_data *itdata = arg;

    if (events & BEV_EVENT_ERROR)
      {     
        report_error (itdata, "in function fg_event_server_cb");
        bufferevent_free (bev);
      }
    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR))
      {
        fprintf (stdout, "[DEBUG] in function fg_event_server_cb: freeing event (not actually)\n");
        //bufferevent_free (bev);
      }

    fprintf (stdout, "[DEBUG] server events is %d\n", events);
}

static void
add_bev (struct fg_events_data *itdata, struct bufferevent *bev)
{
    int s;

    s = list_insert (itdata->bevs, bev);
    if (s != 0)
        report_error (itdata, "in function add_bev");
}

static void
remove_bev (struct fg_events_data *itdata, struct bufferevent *bev)
{
    list_remove (itdata->bevs, bev);
}

static void
accept_conn_cb (struct evconnlistener *listener, evutil_socket_t fd,
    struct sockaddr *address, int socklen, void *arg)
{
    struct bufferevent *bev;
    struct event_base *base;
    struct fg_events_data *itdata = arg;

    base = evconnlistener_get_base (listener);
    bev = bufferevent_socket_new (base, fd, BEV_OPT_CLOSE_ON_FREE);
    add_bev (itdata, bev);
    set_tcp_no_delay (fd);
    evbuffer_enable_locking (bufferevent_get_output (bev), NULL);

    bufferevent_setcb (bev, fg_read_cb, fg_write_cb, fg_event_server_cb, arg);
    bufferevent_enable (bev, EV_READ | EV_WRITE);
}

static void
accept_error_cb (struct evconnlistener *listener, void *arg)
{
    int err;
    struct event_base *base;
    struct fg_events_data *itdata = arg;

    base = evconnlistener_get_base (listener);
    err = EVUTIL_SOCKET_ERROR ();
    itdata->save_errno = 0;
    snprintf (itdata->error, sizeof itdata->error,
              "fgevents: %s: %d: Error when listening on events (%d): %s\n",
              __FILE__, __LINE__, err, evutil_socket_error_to_string (err));
    itdata->cb (itdata->user_data, NULL, NULL);
    event_base_loopexit (base, NULL);
}

int
fg_send_event (struct fg_events_data *etdata, struct fgevent *fgev)
{
    ssize_t s;
    evutil_socket_t fd;
    unsigned char *fgbuf;
    struct evbuffer *output;
    
    s = create_serialized_fgevent_buffer (&fgbuf, fgev);
    if (s < 0)
        return -1;

    output = bufferevent_get_output (etdata->bev);
    evbuffer_lock (output);
    suppress_sigpipe (etdata);
    s = bufferevent_write (etdata->bev, fgbuf, s);
    restore_sigpipe (etdata);
    evbuffer_unlock (output);

    free (fgbuf);

    return s;
}

static void *
events_thread_server_start (void *param)
{
    socklen_t len;
    struct fg_events_data *itdata = param;
    struct sockaddr_in sin, sss;

    evthread_use_pthreads ();
    itdata->base = event_base_new ();
    if (itdata->base == NULL)
      {
        report_error_noen (itdata, "Could not create event base");
        return NULL;
      }

    /* TODO: also listen on UNIX socket */
    memset (&sin, 0, sizeof (sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl (0);//INADDR_ANY;
    sin.sin_port = htons (itdata->port);

    itdata->listener = evconnlistener_new_bind (itdata->base, &accept_conn_cb,
                                                param, LEV_OPT_REUSEABLE |
                                                LEV_OPT_CLOSE_ON_FREE, -1,
                                                (struct sockaddr *) &sin,
                                                sizeof (sin));
    if (itdata->listener == NULL)
      {
        report_error_noen (itdata, "Could not create inet listener");
        return NULL;
      }

    evconnlistener_set_error_cb (itdata->listener, &accept_error_cb);

    len = sizeof (sss);
    if (getsockname (evconnlistener_get_fd (itdata->listener),
        (struct sockaddr *) &sss, &len) < 0)
        report_error (itdata, "getsockname failed");
    else
        itdata->port = ntohs (sss.sin_port);

    sem_post (&itdata->init_flag);
    event_base_dispatch (itdata->base);

    return NULL;
}

static void *
events_thread_client_start (void *param)
{
    ssize_t s;
    struct fg_events_data *itdata = param;
    struct sockaddr_in sin;

    evthread_use_pthreads ();
    itdata->base = event_base_new ();
    if (itdata->base == NULL)
      {
        report_error_noen (itdata, "Could not create event base");
        return NULL;
      }

    itdata->bev = bufferevent_socket_new (itdata->base, -1, BEV_OPT_CLOSE_ON_FREE);
    evbuffer_enable_locking (bufferevent_get_output (itdata->bev), NULL);
    bufferevent_setcb (itdata->bev, fg_read_cb, NULL, fg_event_client_cb, param);
    bufferevent_enable (itdata->bev, EV_READ | EV_PERSIST | EV_WRITE);

    if (itdata->port > 0)
      {
        memset(&sin, 0, sizeof (sin));
        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = htonl(0x7f000001);//inet_addr (itdata->addr);
        sin.sin_port = htons (itdata->port);

        s = bufferevent_socket_connect (itdata->bev, (struct sockaddr *) &sin,
                                        sizeof (sin));

        /*
         * If bufferevent_socket_connect fails, the connection will not
         * automatically be retired by libevent. We must tear down and
         * recreate the socket. Libevent has already released the resources
         *
         * We could block this thread for five minutes and then retry again
         * but this should not happen unless the core module on the master
         * is not running.
         */
        if (s < 0)
          {
            bufferevent_free (itdata->bev);
            report_error (itdata, "bufferevent_socket_connect failed");
            return NULL;
          }
      } else
      {
        // TODO add support for unix sockets
      }

    sem_post (&itdata->init_flag);
    event_base_dispatch (itdata->base);

    return NULL;
}

int
fg_events_server_init (struct fg_events_data *etdata, fg_handle_event_cb cb,
                       void *arg, uint16_t port, char *unix_path)
{
    ssize_t s;

    memset (etdata, 0, sizeof (struct fg_events_data));
    etdata->cb = cb;
    etdata->bevs = {0};
    etdata->user_data = arg;
    etdata->addr = unix_path;
    etdata->port = port;

    sem_init (&etdata->init_flag, 0, 0);
    s = pthread_create (&etdata->events_t, NULL, &events_thread_server_start,
                        etdata);
    if (s != 0)
      {
        errno = s;
        return -1;
      }
    sem_wait (&etdata->init_flag);
    sem_destroy (&etdata->init_flag);

    return 0;
}

int
fg_events_client_init_inet (struct fg_events_data *etdata,
                            fg_handle_event_cb cb, void *arg, char *inet_addr,
                            uint16_t port)
{
    ssize_t s;

    memset (etdata, 0, sizeof (struct fg_events_data));
    etdata->cb = cb;
    etdata->user_data = arg;
    etdata->addr = inet_addr;
    etdata->port = port;

    sem_init (&etdata->init_flag, 0, 0);
    s = pthread_create (&etdata->events_t, NULL, &events_thread_client_start,
                        etdata);
    if (s != 0)
      {
        errno = s;
        return -1;
      }
    sem_wait (&etdata->init_flag);
    sem_destroy (&etdata->init_flag);

    return 0;
}

int
fg_events_client_init_unix (struct fg_events_data *etdata,
                            fg_handle_event_cb cb, void *arg, char *unix_path)
{
    ssize_t s;

    memset (etdata, 0, sizeof (struct fg_events_data));
    etdata->cb = cb;
    etdata->user_data = arg;
    etdata->addr = unix_path;
    etdata->port = 0;

    sem_init (&etdata->init_flag, 0, 0);
    s = pthread_create (&etdata->events_t, NULL, &events_thread_client_start,
                        etdata);
    if (s != 0)
      {
        errno = s;
        return -1;
      }
    sem_wait (&etdata->init_flag);
    sem_destroy (&etdata->init_flag);

    return 0;
}

void
fg_events_server_shutdown (struct fg_events_data *itdata)
{
    struct bufferevent *bev;
    while (list_pop (itdata->bevs, &bev) != -1)
        bufferevent_free (bev);
    evconnlistener_free (itdata->listener);
    event_base_loopexit (itdata->base, NULL);    
    event_base_free (itdata->base);    
    pthread_join (itdata->events_t, NULL);
}

void
fg_events_client_shutdown (struct fg_events_data *itdata)
{
    bufferevent_free (itdata->bev);
    event_base_loopexit (itdata->base, NULL); 
    event_base_free (itdata->base);    
    pthread_join (itdata->events_t, NULL);
}