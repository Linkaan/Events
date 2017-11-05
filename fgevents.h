/*
 *  fgevents.h
 *    The names of functions callable from within fgevents
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

#ifndef _FGEVENTS_H_
#define _FGEVENTS_H_

#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <semaphore.h>
#include <pthread.h>

#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>

#ifdef FG_RELINC
#include "../serializer/serializer.h"
#else
#include <serializer.h>
#endif

#include "list.h"

/* macro to supress unused parameter warnings */
#ifdef UNUSED
#elif defined(__GNUC__)
# define UNUSED(x) UNUSED_ ## x __attribute__((unused))
#else
# define UNUSED(x) x
#endif

typedef int (*fg_handle_event_cb)(void *, struct fgevent *, struct fgevent *);
typedef void (*fg_handle_read_cb)(unsigned char *, size_t, void *);

enum client_status {
    UNITIALIZED,
    CONNECTED,
    DISCONNECTED,
    DROPPED
};

/* Struct to carry around connection (client)-specific data. */
struct client_t {
    int status;    
    int8_t conn_id;
    int8_t user_id;
    uint8_t failed;
    struct bufferevent *bev;
    struct fg_events_data *itdata;    
};

/* Struct to carry around fg events library data. */
struct fg_events_data { 
    struct event_base     *base;
    struct evconnlistener *listener_inet;
    struct evconnlistener *listener_unix;
    struct bufferevent    *bev;
    struct event          *exev;
    struct event          *pingev;
    pthread_t             events_t;
    llist                 clients;
    fg_handle_event_cb    cb;
    fg_handle_read_cb     read_cb;
    sem_t                 init_flag;
    bool                  is_server;
    bool                  running;
    bool                  sigpipe_pending;
    bool                  sigpipe_unblock;
    void                  *user_data;
    char                  *addr;
    uint16_t              port;
    int8_t                conn_id;
    int8_t                user_id;
    int                   save_errno;
    char                  error[512];     
};

/* Initialize libevent and add asynchronous event listener, register cb */
extern int fg_events_server_init (struct fg_events_data *, fg_handle_event_cb,
                                  void *, uint16_t, char *, int8_t);

extern int fg_events_client_init_inet (struct fg_events_data *,
                                       fg_handle_event_cb, fg_handle_read_cb,
                                       void *, char *, uint16_t, int8_t);
extern int fg_events_client_init_unix (struct fg_events_data *,
                                       fg_handle_event_cb, fg_handle_read_cb,
                                       void *, char *, int8_t);

/* Function to send event to server from client */
extern int fg_send_event (struct fg_events_data *, struct fgevent *);
extern int fg_send_data (struct fg_events_data *etdata, unsigned char *buf,
                         size_t len);

/* Tear down event loop and cleanup */
extern void fg_events_server_shutdown (struct fg_events_data *);
extern void fg_events_client_shutdown (struct fg_events_data *);

/* Helper function to parse fgevent delimitted with STX and ETX
   control characters */
extern int fg_parse_fgevent (struct fgevent *, unsigned char *, size_t,
                             unsigned char **);

/* Helper function to buffer fgevent struct to memory that can be sent
   over network */
extern int create_serialized_fgevent_buffer (unsigned char **,
                                             struct fgevent *);

#endif /* _FGEVENTS_H_ */