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
#include <semaphore.h>
#include <pthread.h>

#include <sys/eventfd.h>

#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>

#include <serializer.h>

typedef int (*fg_handle_event_cb)(void *, struct fgevent *, struct fgevent *);

struct fg_events_data { 
    struct event_base     *base;
    struct evconnlistener *listener;
    struct bufferevent    *bev;
    pthread_t             events_t;
    fg_handle_event_cb    cb;
    lstack_t              events_stack;
    sem_t                 init_flag;
    void                  *user_data;
    char                  *addr;
    uint16_t              port;
    int                   efd;
    int                   save_errno;
    char                  error[512];     
};

/* Initialize libevent and add asynchronous event listener, register cb */
extern int fg_events_server_init (struct fg_events_data *, fg_handle_event_cb,
                                  void *, uint16_t, char *);
extern int fg_events_client_init_inet (struct fg_events_data *,
                                       fg_handle_event_cb, void *, char *,
                                       uint16_t);
extern int fg_events_client_init_unix (struct fg_events_data *,
                                       fg_handle_event_cb, void *, char *);

/* Function to send event to server from client */
extern int fg_send_event (struct bufferevent *, struct fgevent *);

/* Tear down event loop and cleanup */
extern void fg_events_server_shutdown (struct fg_events_data *);
extern void fg_events_client_shutdown (struct fg_events_data *);

#endif /* _FGEVENTS_H_ */