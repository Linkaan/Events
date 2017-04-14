/*
 *  clumped_events.c
 *    Integration test to check if fgevents can handle multiple events
 *    arriving at the same time
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

#define EVENT1 1 << 1 
int32_t payload1[] = {123, 456, 789, 123, 456};
struct fgevent event1 = {EVENT1, 1, 5, &(payload1[0])};
int32_t payload1_expected[] = {-123, -456, -789, -123, -456};
struct fgevent event1_expected = {EVENT1, 0, 5, &(payload1_expected[0])};

#define EVENT2 1 << 2
int32_t payload2[] = {0, 0, 0, 0, 0, 0};
struct fgevent event2 = {EVENT2, 1, 6, &(payload2[0])};
int32_t payload2_expected[] = {-1, 1, -1, 1, -1};
struct fgevent event2_expected = {EVENT2, 0, 5, &(payload2_expected[0])};

#define EVENT3 1 << 3
int32_t payload3[] = {0x01, 0x02, 0x03, 0x04, 0x05};
struct fgevent event3 = {EVENT3, 1, 5, &(payload3[0])};
int32_t payload3_expected[] = {-0x01, -0x02, -0x03, -0x04, -0x05};
struct fgevent event3_expected = {EVENT3, 0, 5, &(payload3_expected[0])};

#define EVENT4 1 << 4
int32_t payload4[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
struct fgevent event4 = {EVENT4, 1, 10, &(payload4[0])};
int32_t payload4_expected[] = {~0};
struct fgevent event4_expected = {EVENT4, 0, 1, &(payload4_expected[0])};

#define EVENT5 1 << 5
int32_t payload5[] = {5, 5, 5, 5, 5};
struct fgevent event5 = {EVENT5, 1, 5, &(payload5[0])};
struct fgevent event5_expected = {EVENT5, 0, 0, NULL};

#define EVENT6 1 << 6
int32_t payload6[] = {-123, -456, -789, -123, -456, -789};
struct fgevent event6 = {EVENT6, 1, 5, &(payload6[0])};
int32_t payload6_expected[] = {-123, -456, -789, -123, -456};
struct fgevent event6_expected = {EVENT6, 0, 5, &(payload6_expected[0])};

#define EVENT7 1 << 7
int32_t payload7[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
struct fgevent event7 = {EVENT7, 1, 10, &(payload7[0])};
int32_t payload7_expected[] = {2, 3, 4, 5};
struct fgevent event7_expected = {EVENT7, 0, 4, &(payload7_expected[0])};

#define EVENT8 1 << 8
int32_t payload8[] = {0x02, 0x02, 0x02, 0x02, 0x02};
struct fgevent event8 = {EVENT8, 1, 5, &(payload8[0])};
struct fgevent event8_expected = {EVENT8, 0, 0, NULL};

#define EVENT9 1 << 9
int32_t payload9[] = {0x02, 0x02, 0x02, 0x02, 0x02, 0x02};
struct fgevent event9 = {EVENT9, 1, 5, &(payload9[0])};
int32_t payload9_expected[] = {0x02, 0x03, -0x03, 0x03, -0x03};
struct fgevent event9_expected = {EVENT9, 0, 5, &(payload9_expected[0])};


static int
server_callback (void *arg, struct fgevent *fgev, struct fgevent *ansev)
{
    static int counter = 0;
    int i;

    counter++;

    if (fgev == NULL)
      {
        PRINT_FAIL("fgevent error test %d", counter);
        exit(EXIT_FAILURE);
      }

    switch (fgev->id)
      {
        case EVENT1:
            if (fgev->length != event1.length)
                goto FAIL;
            for (i = 0; i < fgev->length; i++)
              {
                if (fgev->payload[i] != event1.payload[i])
                    goto FAIL;
              }
            if (fgev->writeback)
              {
                memcpy (ansev, &event1_expected, sizeof (struct fgevent));
                return 1;
              }   
            break;
        case EVENT2:
            if (fgev->length != event2.length)
                goto FAIL;
            for (i = 0; i < fgev->length; i++)
              {
                if (fgev->payload[i] != event2.payload[i])
                    goto FAIL;
              }
            if (fgev->writeback)
              {
                memcpy (ansev, &event2_expected, sizeof (struct fgevent));
                return 1;
              }                         
            break;
        case EVENT3:
            if (fgev->length != event3.length)
                goto FAIL;
            for (i = 0; i < fgev->length; i++)
              {
                if (fgev->payload[i] != event3.payload[i])
                    goto FAIL;
              }
            if (fgev->writeback)
              {
                memcpy (ansev, &event3_expected, sizeof (struct fgevent));
                return 1;
              }                         
            break;
        case EVENT4:
            if (fgev->length != event4.length)
                goto FAIL;
            for (i = 0; i < fgev->length; i++)
              {
                if (fgev->payload[i] != event4.payload[i])
                    goto FAIL;
              }
            if (fgev->writeback)
              {
                memcpy (ansev, &event4_expected, sizeof (struct fgevent));
                return 1;
              }                         
            break;
        case EVENT5:
            if (fgev->length != event5.length)
                goto FAIL;
            for (i = 0; i < fgev->length; i++)
              {
                if (fgev->payload[i] != event5.payload[i])
                    goto FAIL;
              }
            if (fgev->writeback)
              {
                memcpy (ansev, &event5_expected, sizeof (struct fgevent));
                return 1;
              }                         
            break;
        case EVENT6:
            if (fgev->length != event6.length)
                goto FAIL;
            for (i = 0; i < fgev->length; i++)
              {
                if (fgev->payload[i] != event6.payload[i])
                    goto FAIL;
              }
            if (fgev->writeback)
              {
                memcpy (ansev, &event6_expected, sizeof (struct fgevent));
                return 1;
              }                         
            break;
        case EVENT7:
            if (fgev->length != event7.length)
                goto FAIL;
            for (i = 0; i < fgev->length; i++)
              {
                if (fgev->payload[i] != event7.payload[i])
                    goto FAIL;
              }
            if (fgev->writeback)
              {
                memcpy (ansev, &event7_expected, sizeof (struct fgevent));
                return 1;
              }                         
            break;
        case EVENT8:
            if (fgev->length != event8.length)
                goto FAIL;
            for (i = 0; i < fgev->length; i++)
              {
                if (fgev->payload[i] != event8.payload[i])
                    goto FAIL;
              }
            if (fgev->writeback)
              {
                memcpy (ansev, &event8_expected, sizeof (struct fgevent));
                return 1;
              }                         
            break;
        case EVENT9:
            if (fgev->length != event9.length)
                goto FAIL;
            for (i = 0; i < fgev->length; i++)
              {
                if (fgev->payload[i] != event9.payload[i])
                    goto FAIL;
              }
            if (fgev->writeback)
              {
                memcpy (ansev, &event9_expected, sizeof (struct fgevent));
                return 1;
              }                         
            break;             
        default:
            goto FAIL;
            break;                                                          
      }
      
    return 0;

    FAIL:
    PRINT_FAIL("test %d", counter);
    exit(EXIT_FAILURE);
}

static int
client_callback (void *arg, struct fgevent *fgev, struct fgevent *ansev)
{
    static int counter = 0;
    int i;
    sem_t *sem = arg;

    counter++;

    if (fgev == NULL)
      {
        PRINT_FAIL ("fgevent error test %d", counter);
        exit (EXIT_FAILURE);
      }

    switch (fgev->id)
      {
        case EVENT1:
            if (fgev->length != event1_expected.length)
                goto FAIL;
            for (i = 0; i < fgev->length; i++)
              {
                if (fgev->payload[i] != event1_expected.payload[i])
                    goto FAIL;
              }
            break;
        case EVENT2:
            if (fgev->length != event2_expected.length)
                goto FAIL;
            for (i = 0; i < fgev->length; i++)
              {
                if (fgev->payload[i] != event2_expected.payload[i])
                    goto FAIL;
              }         
            break;
        case EVENT3:
            if (fgev->length != event3_expected.length)
                goto FAIL;
            for (i = 0; i < fgev->length; i++)
              {
                if (fgev->payload[i] != event3_expected.payload[i])
                    goto FAIL;
              }         
            break;
        case EVENT4:
            if (fgev->length != event4_expected.length)
                goto FAIL;
            for (i = 0; i < fgev->length; i++)
              {
                if (fgev->payload[i] != event4_expected.payload[i])
                    goto FAIL;
              }         
            break;
        case EVENT5:
            if (fgev->length != event5_expected.length)
                goto FAIL;
            for (i = 0; i < fgev->length; i++)
              {
                if (fgev->payload[i] != event5_expected.payload[i])
                    goto FAIL;
              }         
            break;
        case EVENT6:
            if (fgev->length != event6_expected.length)
                goto FAIL;
            for (i = 0; i < fgev->length; i++)
              {
                if (fgev->payload[i] != event6_expected.payload[i])
                    goto FAIL;
              }
            break; 
        default:
            goto FAIL;
            break;                                                          
      }

    if (counter == 6)
        sem_post (sem);

    return 0;

    FAIL:
    PRINT_FAIL ("test %d", counter);
    exit (EXIT_FAILURE);
}

static int
client_callback_unix (void *arg, struct fgevent *fgev, struct fgevent *ansev)
{
    static int counter = 6;
    int i;
    sem_t *sem = arg;

    counter++;

    if (fgev == NULL)
      {
        PRINT_FAIL ("fgevent error test %d", counter);
        exit (EXIT_FAILURE);
      }

    switch (fgev->id)
      {
        case EVENT7:
            if (fgev->length != event7_expected.length)
                goto FAIL;
            for (i = 0; i < fgev->length; i++)
              {
                if (fgev->payload[i] != event7_expected.payload[i])
                    goto FAIL;
              }         
            break;
        case EVENT8:
            if (fgev->length != event8_expected.length)
                goto FAIL;
            for (i = 0; i < fgev->length; i++)
              {
                if (fgev->payload[i] != event8_expected.payload[i])
                    goto FAIL;
              }         
            break;
        case EVENT9:
            if (fgev->length != event9_expected.length)
                goto FAIL;
            for (i = 0; i < fgev->length; i++)
              {
                if (fgev->payload[i] != event9_expected.payload[i])
                    goto FAIL;
              }              
            break; 
        default:
            goto FAIL;
            break;                                                          
      }

    if (counter == 9)
        sem_post (sem);

    return 0;

    FAIL:
    PRINT_FAIL ("test %d", counter);
    exit (EXIT_FAILURE);
}

int
main (void)
{
    int s;
    sem_t pass_test_sem, pass_test_sem_unix;
    struct timespec ts;
    struct fg_events_data server, client, client_unix;

    sem_init (&pass_test_sem, 0, 0);
    sem_init (&pass_test_sem_unix, 0, 0);
    fg_events_server_init (&server, &server_callback, NULL, 0, "/tmp/clumped_events.sock");

    fg_events_client_init_inet (&client, &client_callback, &pass_test_sem, "127.0.0.1", server.port);

    fg_send_event (&client, &event1);
    fg_send_event (&client, &event2);
    fg_send_event (&client, &event3);

    fg_events_client_init_unix (&client_unix, &client_callback_unix, &pass_test_sem_unix, server.addr);

    fg_send_event (&client_unix, &event7);
    fg_send_event (&client, &event4);
    fg_send_event (&client_unix, &event8);
    fg_send_event (&client, &event5);
    fg_send_event (&client_unix, &event9);
    fg_send_event (&client, &event6);

    clock_gettime (CLOCK_REALTIME, &ts);

    ts.tv_sec += 1;
    s = sem_timedwait (&pass_test_sem, &ts);
    if (s >= 0)
      {
        ts.tv_sec += 1;
        s = sem_timedwait (&pass_test_sem_unix, &ts);
      }    
    if (s < 0)
    {
        if (errno == ETIMEDOUT)
            PRINT_FAIL ("test timeout");
        else
            PRINT_FAIL ("unknown error");
        exit (EXIT_FAILURE);
    }
    sem_destroy (&pass_test_sem);
    sem_destroy (&pass_test_sem_unix);

    fg_events_client_shutdown (&client);
    fg_events_client_shutdown (&client_unix);
    fg_events_server_shutdown (&server);

    PRINT_SUCCESS ("all tests passed");
    return EXIT_SUCCESS;
}