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

#define report_error(str, msg)\
        snprintf (str, sizeof str, "fgevents: %s: %d: %s: %s\n",\
                  __FILE__, __LINE__, msg, strerror (errno))

#define report_error_en(str, en, msg)\
        do { errno = en;report_error (str, msg); } while(0)

static void set_tcp_no_delay (evutil_socket_t fd)
{
    int one = 1;
    setsockopt (fd, IPPROTO_TCP, TCP_NODELAY,
                &one, sizeof one);
}


static void
fg_read_cb (struct bufferevent *bev, void *arg)
{
    size_t len;
    unsigned char *buffer, *ptr;
    struct fg_events_data *itdata = arg;
    struct evbuffer *input;

    input = bufferevent_get_input (bev);

    len = evbuffer_get_length (input);
    buffer = malloc (len);
    if (!buffer)
      {
        itdata->save_errno = errno;
        report_error (itdata->error, "in function fg_read_cb malloc failed");
        itdata->cb (itdata->user_data, NULL, NULL);
        return;
      }
    evbuffer_copyout (input, buffer, len);

    ptr = buffer;
    while ((size_t)(ptr - buffer) < len)
      {      
        struct fgevent fgev, ansev;
        int writeback;

        while ((size_t)(ptr - buffer) < len && ptr[0] != 0x02) // STX
            ptr++;
        if ((size_t)(ptr - buffer) >= len)
            break;
        ptr = deserialize_fgevent (++ptr, &fgev);

        /* Check if event was successfully parsed, if not we must increment
           ptr by payload length to prevent any events next to be incorrectly
           parsed */
        if (fgev.length > 0 && !fgev.payload)
          {
            itdata->save_errno = ENOMEM;
            report_error_en (itdata->error, itdata->save_errno,
                             "in function fg_read_cb deserialize_fgevent failed");
            itdata->cb (itdata->user_data, NULL, NULL);             
            ptr += fgev.length;
            continue;
          }
        while ((size_t)(ptr - buffer) < len && ptr[0] != 0x03) // ETX
            ptr++;
        writeback = itdata->cb (itdata->user_data, &fgev, &ansev);
        if (writeback)
          {
            unsigned char *buffer2;
            size_t nbytes;

            nbytes = 2; // for STX and ETX delimiter
            nbytes += sizeof (ansev.id);
            nbytes += sizeof (ansev.writeback);
            nbytes += sizeof (ansev.length);
            if (ansev.length > 0)
                nbytes += ansev.length * sizeof (ansev.payload[0]);
            buffer2 = malloc (nbytes);
            if (!buffer2)
              {
                itdata->save_errno = errno;
                report_error (itdata->error,
                              "in function fg_read_cb malloc failed");
                itdata->cb (itdata->user_data, NULL, NULL);
                continue;
              }

            buffer2[0] = 0x02; // STX
            serialize_fgevent (buffer2 + 1, &ansev);
            buffer2[nbytes-1] = 0x03; // ETX

            evbuffer_lock (bufferevent_get_output (bev));
            bufferevent_write (bev, buffer2, nbytes);
            evbuffer_unlock (bufferevent_get_output (bev));

            free (buffer2);
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
      } else if (events & BEV_EVENT_ERROR)
      {
        itdata->save_errno = errno;
        report_error (itdata->error, "in function fg_event_client_cb");
        itdata->cb (itdata->user_data, NULL, NULL);
      }
}

static void
fg_event_server_cb (struct bufferevent *bev, short events, void *arg)
{
    struct fg_events_data *itdata = arg;

    if (events & BEV_EVENT_ERROR)
      {     
        itdata->save_errno = errno;
        report_error (itdata->error, "in function fg_event_server_cb");
        itdata->cb (itdata->user_data, NULL, NULL);
      }
    if (events & (BEV_EVENT_EOF | BEV_EVENT_ERROR))
      {
        fprintf(stdout, "[DEBUG] in function fg_event_client_cb: freeing event (not actually)\n");
        //bufferevent_free (bev);
      }
}

static void
accept_conn_cb (struct evconnlistener *listener, evutil_socket_t fd,
    struct sockaddr *address, int socklen, void *arg)
{
    struct event_base *base;
    struct bufferevent *bev;

    base = evconnlistener_get_base (listener);
    bev = bufferevent_socket_new (base, fd, 0); // BEV_OPT_CLOSE_ON_FREE
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
fg_send_event (struct bufferevent *bev, struct fgevent *fgev)
{
    evutil_socket_t fd;
    unsigned char *buffer;
    struct evbuffer *output;
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

    output = bufferevent_get_output (bev);
    evbuffer_lock (output);
    evbuffer_add (output, buffer, nbytes);
    fd = bufferevent_getfd (bev);
    evbuffer_write (output, fd);
    evbuffer_unlock (output);

    free (buffer);

    return 0;
}

static void *
events_thread_server_start (void *param)
{
    struct fg_events_data *itdata = param;
    struct sockaddr_in sin;

    evthread_use_pthreads ();
    itdata->base = event_base_new ();
    if (itdata->base == NULL)
      {
        itdata->save_errno = 0;
        snprintf (itdata->error, sizeof itdata->error,
                  "fgevents: %s: %d: Could not create event base\n",
                  __FILE__, __LINE__);
        itdata->cb (itdata->user_data, NULL, NULL);     
        return NULL;
      }

    /* TODO: also listen on UNIX socket */
    memset (&sin, 0, sizeof (sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons (itdata->port);

    itdata->listener = evconnlistener_new_bind (itdata->base, &accept_conn_cb,
                                                param, LEV_OPT_REUSEABLE, -1,
                                                (struct sockaddr *) &sin,
                                                sizeof (sin)); // LEV_OPT_CLOSE_ON_FREE
    if (itdata->listener == NULL)
      {
        itdata->save_errno = 0;
        snprintf (itdata->error, sizeof itdata->error,
                  "fgevents: %s: %d: Could not create INET listener\n",
                  __FILE__, __LINE__);
        itdata->cb (itdata->user_data, NULL, NULL);   
        return NULL;
      }
    evconnlistener_set_error_cb (itdata->listener, &accept_error_cb);

    sem_post (&itdata->init_flag);
    event_base_dispatch (itdata->base);

    return NULL;
}

static void *
events_thread_client_start (void *param)
{
    struct fg_events_data *itdata = param;
    struct sockaddr_in sin;

    evthread_use_pthreads ();
    itdata->base = event_base_new ();
    if (itdata->base == NULL)
      {
        itdata->save_errno = 0;
        snprintf (itdata->error, sizeof itdata->error,
                  "fgevents: %s: %d: Could not create event base\n",
                  __FILE__, __LINE__);
        itdata->cb (itdata->user_data, NULL, NULL);     
        return NULL;
      }

    itdata->bev = bufferevent_socket_new (itdata->base, -1, 0); // BEV_OPT_CLOSE_ON_FREE
    evbuffer_enable_locking (bufferevent_get_output (itdata->bev), NULL);
    bufferevent_setcb (itdata->bev, fg_read_cb, NULL, fg_event_client_cb, param);
    bufferevent_enable (itdata->bev, EV_READ | EV_WRITE);

    if (itdata->port > 0)
      {
        memset(&sin, 0, sizeof (sin));
        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = inet_addr (itdata->addr);
        sin.sin_port = htons (itdata->port);

        if (bufferevent_socket_connect (itdata->bev, (struct sockaddr *) &sin,
            sizeof (sin)) < 0)
          {
            bufferevent_free (itdata->bev);
            itdata->save_errno = 0;
            snprintf (itdata->error, sizeof itdata->error,
                      "fgevents: %s: %d: bufferevent_socket_connect failed\n",
                      __FILE__, __LINE__);
            itdata->cb (itdata->user_data, NULL, NULL);     
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
    event_base_loopexit (itdata->base, NULL);
    evconnlistener_free (itdata->listener);
    event_base_free (itdata->base);    
    pthread_join (itdata->events_t, NULL);
}

void
fg_events_client_shutdown (struct fg_events_data *itdata)
{
    event_base_loopexit (itdata->base, NULL); 
    event_base_free (itdata->base);
    bufferevent_free (itdata->bev);
    pthread_join (itdata->events_t, NULL);
}