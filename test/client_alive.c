/*
 *  client_alive.c
 *    Integration test to check if fgevents sends FG_ALIVE to all clients.
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

#define NUM_CLIENTS 10
#define NUM_FGALIVE_QUERIES 4

struct test_struct {
    int disconnected_count;
    int connected_count;
    int server_fgalive_count;
    int client_fgalive_count;
    int client_confirmed_count;
    sem_t *sem;
    pthread_mutex_t *mutex;
};

static int
server_callback (void *arg, struct fgevent *fgev,
                 struct fgevent * UNUSED(ansev))
{
    struct test_struct *counters = (struct test_struct *) arg;

    pthread_mutex_lock (counters->mutex);
    if (fgev == NULL)
      {
        PRINT_FAIL("fgevent error test %d", counters->server_fgalive_count);
        exit(EXIT_FAILURE);
      }
  
    switch (fgev->id)
      {
        case FG_CONNECTED:
            counters->connected_count++;
            break;
        case FG_ALIVE_CONFRIM:
            counters->server_fgalive_count++;                        
            break;
        case FG_DISCONNECTED:
            counters->disconnected_count++;
            break;
        default:
            goto FAIL;
            break;                                                          
      }

    if (counters->server_fgalive_count >= NUM_FGALIVE_QUERIES * NUM_CLIENTS)
        sem_post (counters->sem);

    pthread_mutex_unlock (counters->mutex);
      
    return 0;

    FAIL:
    PRINT_FAIL("test %d", counters->server_fgalive_count);
    exit(EXIT_FAILURE);
}

static int
client_callback (void *arg, struct fgevent *fgev,
                 struct fgevent * UNUSED(ansev))
{
    struct test_struct *counters = (struct test_struct *) arg;

    pthread_mutex_lock (counters->mutex);
    if (fgev == NULL)
      {
        PRINT_FAIL ("fgevent error test %d", counters->client_fgalive_count);
        exit (EXIT_FAILURE);
      }

    switch (fgev->id)
      {
        case FG_CONFIRMED:
            counters->client_confirmed_count++;
            break;
        case FG_ALIVE:
            counters->client_fgalive_count++;
            break;
        default:
            goto FAIL;
            break;                                                          
      }

    pthread_mutex_unlock (counters->mutex);

    return 0;

    FAIL:
    PRINT_FAIL ("test %d", counters->client_fgalive_count);
    exit (EXIT_FAILURE);
}

int
main (void)
{
    int s;
    int i;
    sem_t pass_test_sem;
    pthread_mutex_t mutex;
    struct timespec ts;
    struct test_struct test_data;
    struct fg_events_data server;
    struct fg_events_data clients[NUM_CLIENTS];

    sem_init (&pass_test_sem, 0, 0);

    memset (&test_data, 0, sizeof (test_data));
    test_data.sem = &pass_test_sem;

    pthread_mutex_init (&mutex, NULL);
    test_data.mutex = &mutex;

    fg_events_server_init (&server, &server_callback, &test_data, 0, "/tmp/client_alive.sock", 1);

    for (i = 0; i < NUM_CLIENTS / 2; i++)
      {
        fg_events_client_init_inet (&clients[i], &client_callback, NULL, &test_data, "127.0.0.1", server.port, 2 + i);
        printf("spawned %d client\n", 2 + i);
      }

    for (; i < NUM_CLIENTS; i++)
      {
        fg_events_client_init_unix (&clients[i], &client_callback, NULL, &test_data, server.addr, 2 + i);
        printf("spawned %d client\n", 2 + i);
      }

    clock_gettime (CLOCK_REALTIME, &ts);

    ts.tv_sec += 5;
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
    
    for (i = 0; i < NUM_CLIENTS; i++)
      {
        fg_events_client_shutdown (&clients[i]);
      }
    sleep (1);
    fg_events_server_shutdown (&server);

    if (test_data.connected_count != test_data.disconnected_count ||
        test_data.server_fgalive_count != test_data.client_fgalive_count ||
        test_data.client_confirmed_count != test_data.connected_count)
      {
        PRINT_FAIL ("some events missed ([%d, %d, %d], [%d, %d])",
                    test_data.connected_count,
                    test_data.disconnected_count,
                    test_data.client_confirmed_count,
                    test_data.server_fgalive_count,
                    test_data.client_fgalive_count);
        exit (EXIT_FAILURE);
      }

    PRINT_SUCCESS ("all tests passed");
    return EXIT_SUCCESS;
}