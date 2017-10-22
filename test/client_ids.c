/*
 *  client_ids.c
 *    Integration test to check if fgevents can handle sending to specified
 *    clients and having multiple clients connected.
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

int32_t payload[] = {0x01, 0x02, 0x03, 0x04, 0x05};
int32_t payload_expected[] = {-0x01, -0x02, -0x03, -0x04, -0x05};
struct fgevent event = {0, 0, 0, 1, 5, &(payload[0])};
struct fgevent event_expected = {0, 0, 0, 0, 5, &(payload_expected[0])};
#define payload_size 5

#define NUM_CLIENTS 5
#define EVENT1 ABI + 1
#define EVENT1_RECEIVER 3
#define EVENT1_BACK ABI + 2
#define EVENT1_BACK_RECEIVER 2

#define EVENT2_RECEIVER 4
#define EVENT2 ABI + 3
#define EVENT2_BACK ABI + 4
#define EVENT2_BACK_RECEIVER 2

#define EVENT3_RECEIVER 5
#define EVENT3 ABI + 5
#define EVENT3_BACK ABI + 6
#define EVENT3_BACK_RECEIVER 2

#define EVENT4_RECEIVER 6
#define EVENT4 ABI + 7
#define EVENT4_BACK ABI + 8
#define EVENT4_BACK_RECEIVER 2

#define EVENT5_RECEIVER 3
#define EVENT5 ABI + 9
#define EVENT5_BACK ABI + 10
#define EVENT5_BACK_RECEIVER 2

struct fgevent init_event = {EVENT1, 0, EVENT1_RECEIVER, 1, 5, &(payload[0])};

struct test_struct {
    int id;
    sem_t *sem;
};

static int
server_callback (void * UNUSED(arg), struct fgevent * UNUSED(fgev),
                 struct fgevent *UNUSED(ansev))
{
    return 0;
}

static int
client_callback (void *arg, struct fgevent *fgev, struct fgevent *ansev)
{
    static int counter = 0;
    struct test_struct *me = (struct test_struct *) arg;
    int i;

    if (fgev == NULL)
      {
        PRINT_FAIL ("fgevent error test %d", counter);
        exit (EXIT_FAILURE);
      }

    switch (fgev->id)
      {
        case EVENT1:
            counter++;
            if (fgev->length != payload_size ||
                fgev->receiver != EVENT1_RECEIVER)
                goto FAIL;
            for (i = 0; i < fgev->length; i++)
              {
                if (fgev->payload[i] != event.payload[i])
                    goto FAIL;
              }
            if (fgev->writeback)
              {
                memcpy (ansev, &event_expected, sizeof (struct fgevent));
                ansev->id = EVENT1_BACK;
                ansev->sender = me->id;
                ansev->receiver = fgev->sender;
                return 1;
              }
            break;
        case EVENT1_BACK:
            counter++;
            if (fgev->length != payload_size ||
                fgev->receiver != EVENT1_BACK_RECEIVER)
                goto FAIL;
            for (i = 0; i < fgev->length; i++)
              {
                if (fgev->payload[i] != event_expected.payload[i])
                    goto FAIL;
              }
            memcpy (ansev, &event, sizeof (struct fgevent));
            ansev->id = EVENT2;
            ansev->sender = me->id;
            ansev->receiver = EVENT2_RECEIVER;
            return 1;
        case EVENT2:
            counter++;
            if (fgev->length != payload_size ||
                fgev->receiver != EVENT2_RECEIVER)
                goto FAIL;
            for (i = 0; i < fgev->length; i++)
              {
                if (fgev->payload[i] != event.payload[i])
                    goto FAIL;
              }
            if (fgev->writeback)
              {
                memcpy (ansev, &event_expected, sizeof (struct fgevent));
                ansev->id = EVENT2_BACK;
                ansev->sender = me->id;
                ansev->receiver = fgev->sender;
                return 1;
              }
            break;
        case EVENT2_BACK:
            counter++;
            if (fgev->length != payload_size ||
                fgev->receiver != EVENT2_BACK_RECEIVER)
                goto FAIL;
            for (i = 0; i < fgev->length; i++)
              {
                if (fgev->payload[i] != event_expected.payload[i])
                    goto FAIL;
              }
            memcpy (ansev, &event, sizeof (struct fgevent));
            ansev->id = EVENT3;
            ansev->sender = me->id;
            ansev->receiver = EVENT3_RECEIVER;
            return 1;
        case EVENT3:
            counter++;
            if (fgev->length != payload_size ||
                fgev->receiver != EVENT3_RECEIVER)
                goto FAIL;
            for (i = 0; i < fgev->length; i++)
              {
                if (fgev->payload[i] != event.payload[i])
                    goto FAIL;
              }
            if (fgev->writeback)
              {
                memcpy (ansev, &event_expected, sizeof (struct fgevent));
                ansev->id = EVENT3_BACK;
                ansev->sender = me->id;
                ansev->receiver = fgev->sender;
                return 1;
              }
            break;
        case EVENT3_BACK:
            counter++;
            if (fgev->length != payload_size ||
                fgev->receiver != EVENT3_BACK_RECEIVER)
                goto FAIL;
            for (i = 0; i < fgev->length; i++)
              {
                if (fgev->payload[i] != event_expected.payload[i])
                    goto FAIL;
              }
            memcpy (ansev, &event, sizeof (struct fgevent));
            ansev->id = EVENT4;
            ansev->sender = me->id;
            ansev->receiver = EVENT4_RECEIVER;
            return 1;
        case EVENT4:
            counter++;
            if (fgev->length != payload_size ||
                fgev->receiver != EVENT4_RECEIVER)
                goto FAIL;
            for (i = 0; i < fgev->length; i++)
              {
                if (fgev->payload[i] != event.payload[i])
                    goto FAIL;
              }
            if (fgev->writeback)
              {
                memcpy (ansev, &event_expected, sizeof (struct fgevent));
                ansev->id = EVENT4_BACK;
                ansev->sender = me->id;
                ansev->receiver = fgev->sender;
                return 1;
              }
            break;
        case EVENT4_BACK:
            counter++;
            if (fgev->length != payload_size ||
                fgev->receiver != EVENT4_BACK_RECEIVER)
                goto FAIL;
            for (i = 0; i < fgev->length; i++)
              {
                if (fgev->payload[i] != event_expected.payload[i])
                    goto FAIL;
              }
            memcpy (ansev, &event, sizeof (struct fgevent));
            ansev->id = EVENT5;
            ansev->sender = me->id;
            ansev->receiver = EVENT5_RECEIVER;
            return 1;
        case EVENT5:
            counter++;
            if (fgev->length != payload_size ||
                fgev->receiver != EVENT5_RECEIVER)
                goto FAIL;
            for (i = 0; i < fgev->length; i++)
              {
                if (fgev->payload[i] != event.payload[i])
                    goto FAIL;
              }
            if (fgev->writeback)
              {
                memcpy (ansev, &event_expected, sizeof (struct fgevent));
                ansev->id = EVENT5_BACK;
                ansev->sender = me->id;
                ansev->receiver = fgev->sender;
                return 1;
              }
            break;
        case EVENT5_BACK:
            counter++;
            if (fgev->length != payload_size ||
                fgev->receiver != EVENT5_BACK_RECEIVER)
                goto FAIL;
            for (i = 0; i < fgev->length; i++)
              {
                if (fgev->payload[i] != event_expected.payload[i])
                    goto FAIL;
              }
            sem_post (me->sem);
        case FG_CONFIRMED:
        case FG_ALIVE:
            break;
        default:
            goto FAIL;
            break;                                                          
      }

    return 0;

    FAIL:
    PRINT_FAIL ("test %d", counter);
    exit (EXIT_FAILURE);
}

int
main (void)
{
  int s;
  sem_t pass_test_sem;    
  struct timespec ts;
  struct fg_events_data server;
  struct fg_events_data clients[NUM_CLIENTS];
  struct test_struct clients_data[NUM_CLIENTS];

  sem_init (&pass_test_sem, 0, 0);
  fg_events_server_init (&server, &server_callback, NULL, 0, "/tmp/clumped_events.sock", 1);

  clients_data[0].id = 2;
  clients_data[0].sem = &pass_test_sem;
  fg_events_client_init_inet (&clients[0], &client_callback, NULL, &clients_data[0], "127.0.0.1", server.port, 2);

  clients_data[1].id = 3;
  clients_data[1].sem = &pass_test_sem;
  fg_events_client_init_inet (&clients[1], &client_callback, NULL, &clients_data[1], "127.0.0.1", server.port, 3);

  clients_data[2].id = 4;
  clients_data[2].sem = &pass_test_sem;
  fg_events_client_init_inet (&clients[2], &client_callback, NULL, &clients_data[2], "127.0.0.1", server.port, 4);

  clients_data[3].id = 5;
  clients_data[3].sem = &pass_test_sem;
  fg_events_client_init_unix (&clients[3], &client_callback, NULL, &clients_data[3], server.addr, 5);

  clients_data[4].id = 6;
  clients_data[4].sem = &pass_test_sem;
  fg_events_client_init_unix (&clients[4], &client_callback, NULL, &clients_data[4], server.addr, 6);

  fg_send_event (&clients[0], &init_event);

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

  for (int i = 0; i < NUM_CLIENTS; i++)
    {
      fg_events_client_shutdown (&clients[i]);
    }  
  fg_events_server_shutdown (&server);

  PRINT_SUCCESS ("all tests passed");
  return EXIT_SUCCESS;
}