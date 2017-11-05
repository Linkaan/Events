/*
 *  fagelmatare-events
 *    Library used for event-based communication between master and slave
 *  fgevents.c
 *    Routines for sending/receiving events between client and server The data
 *    is sent via events which are serialized and delimited with STX/ETX
 *    control characters.

 *    This is the layout of a serialized event:
 *    
 *      1 - STX
 *      4 - id
 *      1 - sender
 *      1 - receiver
 *      1 - writeback
 *      4 - length
 *      ? - payload      
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <event2/event-config.h>
#include <event2/thread.h>

#include "fgevents.h"
#include "list.h"

/* Temporary ugly log error macros before fgutil library is done */
/* TODO: write fgutil library */
#define report_error(etdata, msg)\
        do\
          {\
            etdata->save_errno = errno;\
            snprintf (etdata->error, sizeof etdata->error,\
                      "fgevents: %s: %d: %s: %s\n",\
                      __FILE__, __LINE__, msg, strerror (errno));\
            etdata->cb (etdata->user_data, NULL, NULL);\
          } while (0)

#define report_error_noen(etdata, msg)\
        do\
          {\
            etdata->save_errno = 0;\
            snprintf (etdata->error, sizeof etdata->error,\
                      "fgevents: %s: %d: %s\n",\
                      __FILE__, __LINE__, msg);\
            etdata->cb (etdata->user_data, NULL, NULL);\
          } while (0)

#define report_error_en(etdata, en, msg)\
        do { errno = en;report_error (etdata, msg); } while (0)

/* Forward declarations used in this file. */
static void fg_dispatch_event (struct fg_events_data *itdata,
                               struct bufferevent *bev, struct fgevent *fgev);
static void fg_handle_new_event (struct fg_events_data *,
                                 struct bufferevent *, struct fgevent *);
static void fg_handle_new_conn_event (struct fg_events_data *,
                                      struct bufferevent *, struct fgevent *);
static void fg_handle_conn_confirm_event (struct fg_events_data *itdata,
                                          struct bufferevent *bev,
                                          struct fgevent *fgev);
static void fg_send_offline_event (struct fg_events_data *,
                                   struct bufferevent *, struct fgevent *);
static void fg_handle_ping_event (struct fg_events_data *,
                                  struct bufferevent *, struct fgevent *fgev);
static void fg_handle_ping_confirmed_event (struct fg_events_data *,
                                            struct fgevent *);
static int fg_send_event_bev (struct fg_events_data *, struct bufferevent *,
                              struct fgevent *);
static int fg_send_data_bev (struct fg_events_data *, struct bufferevent *,
                             unsigned char *, size_t);

static int fg_send_connected_event (struct fg_events_data *);
static int fg_send_disconnected_event (struct fg_events_data *);
static int fg_send_confirmed_event (struct fg_events_data *,
                                    struct bufferevent *, int8_t);

static struct client_t *get_client_by_user_id (struct fg_events_data *,
                                               int8_t);
static struct client_t *get_client_by_conn_id (struct fg_events_data *,
                                               int8_t);

static int add_client (struct fg_events_data *, struct bufferevent *,
                       struct client_t **, int8_t);
static void remove_client (struct client_t *);

static void client_event_loop (struct fg_events_data *);

static int fg_events_server_setup_inet (struct fg_events_data *,
                                        struct evconnlistener **, uint16_t);
static int fg_events_server_setup_unix (struct fg_events_data *,
                                        struct evconnlistener **, char *);

static void fg_exit_cb (evutil_socket_t, short, void *);
static void fg_ping_cb (evutil_socket_t, short, void *);

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

        /* Maybe is was blocked already? */
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
            sigset_t mask;    
            static const struct timespec nowait = { 0, 0 };

            sigemptyset (&mask);
            sigtimedwait (&mask, NULL, &nowait);
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

    evbuffer_drain (evbuf, len);

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

int
create_serialized_fgevent_buffer (unsigned char **buf, struct fgevent *fgev)
{
    unsigned char *buffer;    
    size_t nbytes;

    nbytes = 2; // for STX and ETX delimiter
    nbytes += FGEVENT_HEADER_SIZE;
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
    struct client_t *holder = arg;
    struct fg_events_data *itdata = holder->itdata;

    s = copy_evbuffer_into_buffer (bufferevent_get_input (bev), &buffer);
    if (s < 0)
      {
        report_error (itdata, "in function fg_read_cb malloc failed");
        return;
      }
    len = s;
    
    if (itdata->read_cb != NULL)
      {
        itdata->read_cb (buffer, s, itdata->user_data);
      }
    else
      {
        ptr = buffer;
        while ((size_t)(ptr - buffer) < len)
          {        
            struct fgevent fgev;

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

            fg_handle_new_event (itdata, bev, &fgev);
            
            if (fgev.length > 0)
                free (fgev.payload);
          }
      }

    free (buffer);
}

static void
fg_handle_new_event (struct fg_events_data *itdata, struct bufferevent *bev,
                     struct fgevent *fgev)
{
    struct fgevent ansev;
    int writeback;

    if (!itdata->is_server || fgev->receiver == itdata->user_id)
      {
        writeback = itdata->cb (itdata->user_data, fgev, &ansev);
        if (writeback)
          {
            if (itdata->is_server)
              {
                fg_dispatch_event (itdata, NULL, &ansev);
              }
            else if (fg_send_event_bev (itdata, bev, &ansev) < 0)
              {
                report_error (itdata, "fg_send_event_bev failed");
              }
          }        
        if (!itdata->is_server && fgev->id == FG_CONFIRMED)
          {
            fg_handle_conn_confirm_event (itdata, bev, fgev);
          }
        else if (!itdata->is_server && fgev->id == FG_ALIVE)
          {
            fg_handle_ping_event (itdata, bev, fgev);            
          }
      }
    else
      {
        itdata->cb (itdata->user_data, fgev, &ansev);

        // TODO: also check status of sender
        
        if (fgev->id == FG_ALIVE_CONFRIM)
          {
            fg_handle_ping_confirmed_event (itdata, fgev);
          }
        else if (fgev->id == FG_CONNECTED || fgev->id == FG_DISCONNECTED)
          {
            fg_handle_new_conn_event (itdata, bev, fgev);
          }
        else
          {
            fg_dispatch_event (itdata, bev, fgev);                        
          }
      }
}

static void
fg_dispatch_event (struct fg_events_data *itdata, struct bufferevent *bev,
                   struct fgevent *fgev)
{
    struct client_t *client = get_client_by_user_id (itdata,
                                                     fgev->receiver);
    if (client == NULL)
      {
        /* TODO: if sender requires writeback, send back a FG_NO_SUCH_USER
          event */
        return;
      }

    if (client->status != CONNECTED)
      {
        if (bev == NULL)
          {
            // TODO: this is the server dispatching from writeback, send
            // the host program a callback with offline event
          }
        else
          {
            fg_send_offline_event (itdata, bev, fgev);
          }        
        return;
      }

    if (fg_send_event_bev (itdata, client->bev, fgev) < 0)
      {
        report_error (itdata, "fg_send_event_bev failed");
      }
}

static void
fg_handle_new_conn_event (struct fg_events_data *itdata,
                          struct bufferevent * UNUSED(bev),
                          struct fgevent *fgev)
{
    if (fgev->id == FG_CONNECTED)
      {
        struct client_t *client = get_client_by_user_id (itdata,
                                                         fgev->sender);
        if (client != NULL)
          {
            if (client->status != CONNECTED)
              {
                remove_client (client);
              }
            else
              {
                report_error_noen (client->itdata,
                                   "in function fg_handle_new_conn_event connection denied");
                /* TODO: if sender requires writeback, send back a
                   FG_CONN_DENIED event */
                return;
              }            
          }

        client = get_client_by_conn_id (itdata, fgev->payload[0]);
        if (client == NULL)
          {
            /* TODO: if sender requires writeback, send back a
               FG_NO_SUCH_USER event */
            return;
          }

        client->user_id = fgev->sender;
        client->conn_id = -1;
        client->status = CONNECTED;
      }
    else if (fgev->id == FG_DISCONNECTED)
      {
        struct client_t *client = get_client_by_user_id (itdata,
                                                         fgev->sender);
        if (client == NULL)
          {
            /* TODO: if sender requires writeback, send back a
               FG_NO_SUCH_USER event */
            return;
          }

        client->status = DISCONNECTED;
      }
}

static void
fg_handle_conn_confirm_event (struct fg_events_data *itdata,
                              struct bufferevent *bev, struct fgevent *fgev)
{
    ssize_t s;

    if (fgev->id != FG_CONFIRMED)
      {
        report_error_noen (itdata,
                   "in function fg_handle_conn_confirm_event invalid event id");
        return;
      }

    if (bev != itdata->bev)
      {
        report_error_noen (itdata,
                   "in function fg_handle_conn_confirm_event invalid bev");
        return; 
      }

    itdata->conn_id = fgev->payload[0];

    s = fg_send_connected_event (itdata);
    if (s != 0)
      {
        report_error (itdata, "fg_send_connected_event failed");
      }

    sem_post (&itdata->init_flag); /* TODO: set status to CONNECTED */
}

static void
fg_handle_ping_confirmed_event (struct fg_events_data *itdata,
                                struct fgevent *fgev)
{
  struct client_t *sender = get_client_by_user_id (itdata, fgev->sender);
  if (sender == NULL)
    {
      fprintf(stdout, "[DEBUG] in function fg_handle_new_event: no such user\n");
      return;
    }

  sender->failed = 0;
}

static void
fg_send_offline_event (struct fg_events_data *itdata, struct bufferevent *bev,
                       struct fgevent *fgev)
{
    struct fgevent ansev;

    ansev.id = FG_USER_OFFLINE;
    ansev.sender = itdata->user_id;
    ansev.receiver = fgev->sender;
    ansev.length = 0;
    if (fg_send_event_bev (itdata, bev, &ansev) < 0)
      {
        report_error (itdata, "fg_send_event_bev failed");
      }
}                            

static void
fg_handle_ping_event (struct fg_events_data *itdata, struct bufferevent *bev,
                      struct fgevent *fgev)
{
    struct fgevent ansev;

    ansev.id = FG_ALIVE_CONFRIM;
    ansev.sender = fgev->receiver;
    ansev.receiver = 0;
    ansev.length = 0;
    if (fg_send_event_bev (itdata, bev, &ansev) < 0)
      {
        report_error (itdata, "fg_send_event_bev failed");
      }
}                      

static void
fg_write_cb (struct bufferevent *bev, void * UNUSED(arg))
{
    //struct evbuffer *output = bufferevent_get_output (bev);

    bufferevent_flush (bev, EV_WRITE, BEV_FLUSH);

    /* writeback flushed */
    /*
    if (evbuffer_get_length (output) == 0)
      {
        //bufferevent_free (bev);
        //fprintf(stdout, "[DEBUG] in function fg_write_cb: writeback flushed\n");
      }
      */
}

static void
fg_event_client_cb (struct bufferevent *bev, short events, void *arg)
{
    struct client_t *holder = arg;
    struct fg_events_data *itdata = holder->itdata;

    if (events & BEV_EVENT_CONNECTED)
      {
        fprintf(stdout, "[DEBUG] in function fg_event_client_cb: BEV_EVENT_CONNECTED\n");
        evutil_socket_t fd = bufferevent_getfd (bev);
        set_tcp_no_delay (fd);
      }
    if (events & BEV_EVENT_ERROR)
        report_error (itdata, "in function fg_event_client_cb");

    if (events & (BEV_EVENT_ERROR | BEV_EVENT_EOF))
      {
        fprintf (stdout, "[DEBUG] in function fg_event_client_cb: freeing event\n");
        event_base_loopexit (itdata->base, NULL);
      }

    fprintf (stdout, "[DEBUG] client events is %d\n", events);
}

static void
fg_event_server_cb (struct bufferevent * UNUSED(bev), short events, void *arg)
{
    struct client_t *client = arg;

    if (events & BEV_EVENT_ERROR)
      {
        report_error (client->itdata, "in function fg_event_server_cb");        
        remove_client (client);
      }
    if (events & BEV_EVENT_EOF)
      {
        fprintf (stdout, "[DEBUG] in function fg_event_server_cb: got eof from %d\n", client->user_id);        
        remove_client (client);
      }

    fprintf (stdout, "[DEBUG] server events is %d\n", events);
}

static struct client_t *
get_client_by_user_id (struct fg_events_data *itdata, int8_t user_id)
{
    /* NOTE: for a more scalable implementation, an associative array with the
       user_id as the key would be preferable */
    for (struct node *cur_head = itdata->clients;
         cur_head != NULL;
         cur_head = cur_head->next)
      {
        struct client_t *client = cur_head->value;
        if (client->user_id == user_id)
          {
            return client;
          }
      }
    return NULL;
}

static struct client_t *
get_client_by_conn_id (struct fg_events_data *itdata, int8_t conn_id)
{
    for (struct node *cur_head = itdata->clients;
         cur_head != NULL;
         cur_head = cur_head->next)
      {
        struct client_t *client = cur_head->value;
        if (client->conn_id == conn_id)
          {
            return cur_head->value;
          }
      }
    return NULL;
}

static int
add_client (struct fg_events_data *itdata, struct bufferevent *bev,
            struct client_t **holder, int8_t conn_id)
{
    int s;

    struct client_t *client = malloc (sizeof (struct client_t));
    if (client == NULL)
      {
        report_error (itdata, "in function add_client malloc failed");
        return -1;
      }

    memset (client, 0, sizeof (struct client_t));

    client->status = UNITIALIZED;
    client->conn_id = conn_id;
    client->user_id = -1;
    client->itdata = itdata;
    client->bev = bev;

    s = list_insert (&itdata->clients, client);
    if (s != 0)
      {
        report_error (itdata, "in function add_client");
        return -1;
      }

    *holder = client;

    return 0;
}

static void
remove_client (struct client_t *client)
{
    int s;

    s = list_remove (&client->itdata->clients, client);
    if (s != 0)
      {
        report_error (client->itdata, "in function remove_client");
      }

    bufferevent_free (client->bev);
}

static void
accept_conn_cb (struct evconnlistener *listener, evutil_socket_t fd,
                struct sockaddr * UNUSED(address), int UNUSED(socklen),
                void *arg)
{
    static int8_t conn_tot = 0;
    int s;
    struct bufferevent *bev;
    struct event_base *base;
    struct client_t *client;
    struct fg_events_data *itdata = arg;

    base = evconnlistener_get_base (listener);
    bev = bufferevent_socket_new (base, fd, BEV_OPT_CLOSE_ON_FREE);
    s = add_client (itdata, bev, &client, conn_tot);
    set_tcp_no_delay (fd);
    evbuffer_enable_locking (bufferevent_get_output (bev), NULL);

    if (s == 0)
      {
        bufferevent_setcb (bev, fg_read_cb, fg_write_cb, fg_event_server_cb, client);
        bufferevent_enable (bev, EV_READ | EV_WRITE);

        fg_send_confirmed_event (itdata, bev, conn_tot++);
      }
    else
      {
        // TODO: send connection failed event
      }
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

static int
fg_send_connected_event (struct fg_events_data *etdata)
{
    struct fgevent fgev;

    int32_t conn_id_payload = (int32_t) etdata->conn_id;
    fgev.id = FG_CONNECTED;
    fgev.sender = etdata->user_id;
    fgev.receiver = 0;
    fgev.writeback = 0;
    fgev.length = 1;
    fgev.payload = &conn_id_payload;

    if (fg_send_event (etdata, &fgev) < 0)
      {
        report_error (etdata, "fg_send_connected_event failed");
        return -1;
      }

    return 0;
}

static int
fg_send_confirmed_event (struct fg_events_data *etdata,
                         struct bufferevent *bev, int8_t conn_id)
{
    struct fgevent fgev;

    int32_t conn_id_payload = (int32_t) conn_id;
    fgev.id = FG_CONFIRMED;
    fgev.sender = etdata->user_id;
    fgev.receiver = 0;
    fgev.writeback = 1;
    fgev.length = 1;
    fgev.payload = &conn_id_payload;

    if (fg_send_event_bev (etdata, bev, &fgev) < 0)
      {
        report_error (etdata, "fg_send_confirmed_event failed");
        return -1;
      }

    return 0;
}

static int
fg_send_disconnected_event (struct fg_events_data *etdata)
{
    struct fgevent fgev;

    fgev.id = FG_DISCONNECTED;
    fgev.sender = etdata->user_id;
    fgev.receiver = 0;
    fgev.writeback = 0;
    fgev.length = 0;

    if (fg_send_event (etdata, &fgev) < 0)
      {
        report_error (etdata, "fg_send_disconnected_event failed");
        return -1;
      }

    return 0;
}

static int
fg_send_event_bev (struct fg_events_data *etdata, struct bufferevent *bev,
                   struct fgevent *fgev)
{
    ssize_t s;
    unsigned char *fgbuf;    
    
    s = create_serialized_fgevent_buffer (&fgbuf, fgev);
    if (s < 0)
        return -1;

    s = fg_send_data_bev (etdata, bev, fgbuf, s);    

    free (fgbuf);

    return s;
}

static int
fg_send_data_bev (struct fg_events_data *itdata, struct bufferevent *bev,
                  unsigned char *buf, size_t len)
{
    ssize_t s;
    struct evbuffer *output;

    output = bufferevent_get_output (bev);
    evbuffer_lock (output);
    suppress_sigpipe (itdata);
    s = bufferevent_write (bev, buf, len);
    itdata->save_errno = errno;
    restore_sigpipe (itdata);
    evbuffer_unlock (output);

    return s;
}

int
fg_send_event (struct fg_events_data *etdata, struct fgevent *fgev)
{
    static int temp_count = 0;
    // TODO: if we are the server, send the event to ourself
    fgev->sender = etdata->user_id;
    if (fgev->sender == 2 && fgev->id == 2 && ++temp_count == 2) {
      printf("should not get here\n");
    }
    //printf("%d sent %d\n", fgev->sender, fgev->id);
    return fg_send_event_bev (etdata, etdata->bev, fgev);
}

int
fg_send_data (struct fg_events_data *etdata, unsigned char *buf, size_t len)
{
    return fg_send_data_bev (etdata, etdata->bev, buf, len);
}

static int
fg_events_server_setup_inet (struct fg_events_data *itdata,
                             struct evconnlistener **listener, uint16_t port)
{
    socklen_t len;
    struct sockaddr_in sin, sss;
    struct evconnlistener *_listener;

    memset (&sin, 0, sizeof (sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons (port);

    _listener = evconnlistener_new_bind (itdata->base, &accept_conn_cb, itdata,
                                         LEV_OPT_REUSEABLE |
                                         LEV_OPT_CLOSE_ON_FREE, -1,
                                         (struct sockaddr *) &sin,
                                         sizeof (sin));
    if (_listener == NULL)
      {
        report_error_en (itdata, EAGAIN, "Could not create inet listener");
        return -1;
      }

    evconnlistener_set_error_cb (_listener, &accept_error_cb);

    len = sizeof (sss);
    if (getsockname (evconnlistener_get_fd (_listener),
        (struct sockaddr *) &sss, &len) < 0)
        report_error (itdata, "getsockname failed");
    else
        itdata->port = ntohs (sss.sin_port);

    *listener = _listener;

    return 0;
}

static int
fg_events_server_setup_unix (struct fg_events_data *itdata,
                             struct evconnlistener **listener, char *unix_path)
{
    struct sockaddr_un sun;
    struct evconnlistener *_listener;    

    if (unix_path == NULL)
        return 0;    

    memset (&sun, 0, sizeof (sun));
    sun.sun_family = AF_LOCAL;
    strncpy (sun.sun_path, unix_path, sizeof (sun.sun_path) - 1);

    unlink (unix_path);

    _listener = evconnlistener_new_bind (itdata->base, &accept_conn_cb, itdata,
                                         LEV_OPT_REUSEABLE |
                                         LEV_OPT_CLOSE_ON_FREE, -1,
                                         (struct sockaddr *) &sun,
                                         sizeof (sun));
    if (_listener == NULL)
      {
        report_error_en (itdata, EAGAIN, "Could not create unix listener");
        return -1;
      }

    evconnlistener_set_error_cb (_listener, &accept_error_cb);

    *listener = _listener;

    return 0;
}

static void *
events_thread_server_start (void *param)
{
    int s;
    struct timeval pinginterval = { 1, 0 }; // ping clients every 1 seconds
    struct fg_events_data *itdata = param;
    struct client_t *client;
    void *client_pointer;

    evthread_use_pthreads ();
    itdata->base = event_base_new ();
    if (itdata->base == NULL)
      {
        report_error_en (itdata, EAGAIN, "Could not create event base");
        sem_post (&itdata->init_flag);
        return NULL;
      }

    s = fg_events_server_setup_inet (itdata, &itdata->listener_inet,
                                     itdata->port);
    if (s != 0)
      {
        event_base_free (itdata->base);
        sem_post (&itdata->init_flag);
        return NULL;      
      }

    s = fg_events_server_setup_unix (itdata, &itdata->listener_unix,
                                     itdata->addr);
    if (s != 0)
      {
        evconnlistener_free (itdata->listener_inet);        
        event_base_free (itdata->base);
        sem_post (&itdata->init_flag);
        return NULL;
      }

    /* Register event to be able to break out of event loop when raised */
    itdata->exev = event_new (itdata->base, -1, 0, fg_exit_cb, itdata);
    if (!itdata->exev || event_add (itdata->exev, NULL) < 0)
        report_error_noen (itdata, "Could not create/add exit event");

    itdata->pingev = event_new (itdata->base, -1, EV_PERSIST, fg_ping_cb,
                                itdata);
    if (!itdata->pingev || event_add (itdata->pingev, &pinginterval) < 0)
        report_error_noen (itdata, "Could not create/add ping event");

    sem_post (&itdata->init_flag);
    event_base_dispatch (itdata->base);

    while (list_pop (&itdata->clients, &client_pointer) != -1)
      {
        client = client_pointer;
        bufferevent_free (client->bev);
      }

    evconnlistener_free (itdata->listener_inet);
    evconnlistener_free (itdata->listener_unix);
    if (itdata->exev)
        event_free (itdata->exev);
    if (itdata->pingev)
        event_free (itdata->pingev);
    event_base_free (itdata->base);

    return NULL;
}

static void
client_event_loop (struct fg_events_data *itdata)
{
    while (itdata->running)
      {
        ssize_t s;
        socklen_t len;
        struct sockaddr_in sin;
        struct sockaddr_un sun;
        struct sockaddr *saddr;
        struct client_t holder;

        memset (&holder, 0, sizeof (struct client_t));
        holder.itdata = itdata;

        itdata->bev = bufferevent_socket_new (itdata->base, -1,
                                              BEV_OPT_CLOSE_ON_FREE);
        evbuffer_enable_locking (bufferevent_get_output (itdata->bev), NULL);
        bufferevent_setcb (itdata->bev, fg_read_cb, NULL, fg_event_client_cb,
                           &holder);
        bufferevent_enable (itdata->bev, EV_READ | EV_PERSIST | EV_WRITE);
        /* TODO: set status to DISCONNECTED */
        if (itdata->port > 0)
          {
            /* connect via inet sockets */
            len = sizeof (sin);
            memset(&sin, 0, len);
            sin.sin_family = AF_INET;
            sin.sin_addr.s_addr = inet_addr (itdata->addr); // htonl (0x7f000001);
            sin.sin_port = htons (itdata->port);

            saddr = (struct sockaddr *) &sin;
          }
        else
          {
            /* connect via unix domain sockets */
            len = sizeof (sun);
            memset (&sun, 0, len);
            sun.sun_family = AF_LOCAL;
            strncpy (sun.sun_path, itdata->addr, sizeof (sun.sun_path) - 1);
            
            saddr = (struct sockaddr *) &sun;
          }

        s = bufferevent_socket_connect (itdata->bev, saddr, len);
        if (s < 0)
          {
            bufferevent_free (itdata->bev);
            report_error (itdata, "bufferevent_socket_connect failed");

            sem_post (&itdata->init_flag);

            /* attempt to reconnect after 10 seconds */
            usleep (10 * 1000 * 1000);
            continue;
          }

        event_base_dispatch (itdata->base);

        bufferevent_free (itdata->bev);
        /* TODO: set status to DISCONNECTED */
        // wait 10 seconds before attempting to re-connect unless exitting
        if (itdata->running)        
            usleep (10 * 1000 * 1000);
      }
}

static void *
events_thread_client_start (void *param)
{    
    struct fg_events_data *itdata = param;

    evthread_use_pthreads ();
    itdata->base = event_base_new ();
    if (itdata->base == NULL)
      {
        report_error_noen (itdata, "Could not create event base");
        return NULL;
      }

    /* Register event to be able to break out of event loop when raised */
    itdata->exev = event_new (itdata->base, -1, 0, fg_exit_cb, itdata);
    if (!itdata->exev || event_add (itdata->exev, NULL) < 0)
        report_error_noen (itdata, "Could not create/add exit event");

    itdata->running = true;
    client_event_loop (itdata);

    if (itdata->exev)
        event_free (itdata->exev);
    event_base_free (itdata->base);

    return NULL;
}

int
fg_events_server_init (struct fg_events_data *etdata, fg_handle_event_cb cb,
                       void *arg, uint16_t port, char *unix_path,
                       int8_t user_id)
{
    ssize_t s;

    memset (etdata, 0, sizeof (struct fg_events_data));
    etdata->cb = cb;
    etdata->user_data = arg;
    etdata->addr = unix_path;
    etdata->port = port;
    etdata->is_server = true;
    etdata->user_id = user_id;

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

    return etdata->save_errno;
}

int
fg_events_client_init_inet (struct fg_events_data *etdata,
                            fg_handle_event_cb cb, fg_handle_read_cb read_cb,
                            void *arg, char *inet_addr, uint16_t port,
                            int8_t user_id)
{
    ssize_t s;

    memset (etdata, 0, sizeof (struct fg_events_data));
    etdata->cb = cb;
    etdata->read_cb = read_cb;
    etdata->user_data = arg;
    etdata->addr = inet_addr;
    etdata->port = port;
    etdata->is_server = false;
    etdata->user_id = user_id;

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

    return etdata->save_errno;
}

int
fg_events_client_init_unix (struct fg_events_data *etdata,
                            fg_handle_event_cb cb, fg_handle_read_cb read_cb,
                            void *arg, char *unix_path, int8_t user_id)
{
    ssize_t s;

    memset (etdata, 0, sizeof (struct fg_events_data));
    etdata->cb = cb;
    etdata->read_cb = read_cb;
    etdata->user_data = arg;
    etdata->addr = unix_path;
    etdata->port = 0;
    etdata->is_server = false;
    etdata->user_id = user_id;

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

    return etdata->save_errno;
}

static void
fg_exit_cb (evutil_socket_t UNUSED(sig), short UNUSED(events), void *arg)
{
    struct fg_events_data *itdata = arg;

    itdata->running = false;
    event_base_loopexit (itdata->base, NULL);
}

static void
fg_ping_cb (evutil_socket_t UNUSED(sig), short UNUSED(events), void *arg)
{
    struct fg_events_data *itdata = arg;
    struct fgevent fgev;
    
    fgev.id = FG_ALIVE;
    fgev.sender = itdata->user_id;    
    fgev.length = 0;

    for (struct node *cur_head = itdata->clients;
      cur_head != NULL;
      cur_head = cur_head->next)
    {
      struct client_t *client = cur_head->value;
      if (client->status != CONNECTED) continue;      

      if (++client->failed > 5)
        {
          client->status = DROPPED;
        }

      fgev.receiver = client->user_id;
      if (fg_send_event_bev (itdata, client->bev, &fgev) < 0)
        {
          report_error (itdata, "fg_send_event_bev failed");
        }
    }
}

void
fg_events_server_shutdown (struct fg_events_data *itdata)
{    
    if (itdata->exev)
      {
        event_active (itdata->exev, EV_WRITE, 0);
        pthread_join (itdata->events_t, NULL);
      }
    else
        pthread_cancel (itdata->events_t);
}

void
fg_events_client_shutdown (struct fg_events_data *itdata)
{
    fg_send_disconnected_event (itdata);
    fg_events_server_shutdown (itdata);
}