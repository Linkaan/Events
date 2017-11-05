/*
 *  client_reject.c
 *    Integration test to check if fgevents rejects attempt to connect to
 *    already established connection using specific client id unless 
 *    client dropped.
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
#include <string.h>
#include <time.h>
#include <errno.h>
#include <semaphore.h>

#define INTEGRATION_TEST
#include "test_common.h"

#define EVENT_ID ABI
#define RECEIVER_ID 2
int32_t payload[] = {0x01, 0x02, 0x03, 0x04, 0x05};
struct fgevent event = {EVENT_ID, 0, 0, 1, 5, &(payload[0])};

struct test_struct {
    int has_been_rejected;
    int has_answered;
    sem_t *sem;
    pthread_mutex_t *mutex;
    struct fg_events_data *client_to_disable;
};

static int
server_callback (void * UNUSED(arg), struct fgevent * UNUSED(fgev),
                 struct fgevent * UNUSED(ansev))
{
    return 0;
}

static int
client_callback (void *arg, struct fgevent *fgev,
                struct fgevent * UNUSED(ansev))
{
    struct test_struct *test_data = (struct test_struct *) arg;

    pthread_mutex_lock (test_data->mutex);
    if (fgev == NULL)
      {
        PRINT_FAIL ("fgevent error");
        exit (EXIT_FAILURE);
      }

    switch (fgev->id)
      {
        case FG_CONFIRMED:
            break;
        case FG_ALIVE:
            break;
        case FG_USER_OFFLINE:
            test_data->has_been_rejected = 1;
            break;
        case EVENT_ID:
            if (fgev->receiver != RECEIVER_ID)
                goto FAIL;

            pthread_cancel (test_data->client_to_disable->events_t);
            test_data->has_answered = 1;
            break;
        default:
            goto FAIL;
            break;                                                          
      }

    if (test_data->has_answered && test_data->has_been_rejected)
        sem_post (test_data->sem);

    pthread_mutex_unlock (test_data->mutex);    

    return 0;

    FAIL:
    PRINT_FAIL ("test");
    exit (EXIT_FAILURE);
}

int
main (void)
{
    int s;
    sem_t pass_test_sem;
    pthread_mutex_t mutex;
    struct timespec ts;
    struct test_struct test_data;
    struct fg_events_data server;
    struct fg_events_data clients[2];
    struct fgevent fgev;

    sem_init (&pass_test_sem, 0, 0);

    memset (&test_data, 0, sizeof (test_data));
    test_data.sem = &pass_test_sem;

    pthread_mutex_init (&mutex, NULL);
    test_data.mutex = &mutex;

    fg_events_server_init (&server, &server_callback, &test_data, 0, "/tmp/client_alive.sock", 1);

    fg_events_client_init_inet (&clients[0], &client_callback, NULL, &test_data, "127.0.0.1", server.port, RECEIVER_ID);
    fg_events_client_init_unix (&clients[1], &client_callback, NULL, &test_data, server.addr, RECEIVER_ID + 1);

    sleep (1); // make sure all clients are connected

    pthread_mutex_lock (test_data.mutex);
    memcpy (&fgev, &event, sizeof (struct fgevent));
    fgev.id = EVENT_ID;
    fgev.receiver = RECEIVER_ID;
    test_data.client_to_disable = &clients[1];
    fg_send_event (&clients[1], &fgev);
    pthread_mutex_unlock (test_data.mutex);

    sleep (6);

    pthread_mutex_lock (test_data.mutex);
    memcpy (&fgev, &event, sizeof (struct fgevent));
    fgev.id = EVENT_ID;
    fgev.receiver = RECEIVER_ID + 1;
    fg_send_event (&clients[0], &fgev);
    pthread_mutex_unlock (test_data.mutex);

    clock_gettime (CLOCK_REALTIME, &ts);

    ts.tv_sec += 1;
    s = sem_timedwait (&pass_test_sem, &ts);
    if (s < 0)
      {
        if (errno == ETIMEDOUT)
            PRINT_FAIL ("test timeout");
        else
            PRINT_FAIL ("unknown error");
        exit (EXIT_FAILURE);
      }
    sem_destroy (&pass_test_sem);
    pthread_mutex_destroy (&mutex);
    
    fg_events_client_shutdown (&clients[0]);
    fg_events_server_shutdown (&server);

    PRINT_SUCCESS ("all tests passed");
    return EXIT_SUCCESS;
}