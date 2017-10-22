/*
 *  parse_fgevent.c
 *    Unit test to check if parse_fgevent correclty parses a serialized
 *    fgevent struct
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

#define UNIT_TEST
#include "test_common.h"

/* Garbage in the beginning and 5 entries in payload with STX/ETX */
unsigned char buf1[] =
{
	0x84, 0xb0, 0xfa, 0x02, /* 3 bytes garbage and STX 1 byte */
	0xc9, 0x07, 0xcc, 0x00, /* id 4 bytes big endian */

	0x00, 0x00, 0xff, /* sender 1 byte, receiver 1 byte and writeback 1 byte */
	0x05, 0x00, 0x00, 0x00, /* length 4 bytes big endian */

	0x7b, 0x00, 0x00, 0x00, /* payload[0] 4 bytes big endian */

	0xc8, 0x01, 0x00, 0x00, /* payload[1] 4 bytes big endian */

	0x15, 0x03, 0x00, 0x00, /* payload[2] 4 bytes big endian */

	0x7b, 0x00, 0x00, 0x00, /* payload[3] 4 bytes big endian */

	0xc8, 0x01, 0x00, 0x00, /* payload[4] 4 bytes big endian */
	0x03 /* 1 byte ETX */
};
int32_t payload1[] = {123, 456, 789, 123, 456};
struct fgevent expected1 = {13371337, 0, 0, 255, 5, &(payload1[0])};

/* Missing ETX after payload, payload itself contains multiple ETX values */
unsigned char buf2[] =
{
	0x02, /* 1 byte STX */
	0x00, 0x00, 0x00, 0x00, /* id 4 bytes big endian */

	0x00, 0x00, 0x00, /* sender 1 byte, receiver 1 byte and writeback 1 byte */

	0x06, 0x00, 0x00, 0x00, /* length 4 bytes big endian */

	0x02, 0x00, 0x00, 0x00, /* payload[0] 4 bytes big endian */

	0x03, 0x00, 0x00, 0x00, /* payload[1] 4 bytes big endian */

	0xff, 0xff, 0xff, 0xff, /* payload[2] 4 bytes big endian */

	0x02, 0x00, 0x00, 0x00, /* payload[3] 4 bytes big endian */

	0x03, 0x00, 0x00, 0x00, /* payload[4] 4 bytes big endian */

	0x03, 0x00, 0x00, 0x00  /* payload[5] 4 bytes big endian */
};
int32_t payload2[] = {0x02, 0x03, ~0, 0x02, 0x03, 0x03};
struct fgevent expected2 = {0, 0, 0, 0, 6, &(payload2[0])};

/* Garbage in the end and 0 entries in payload */
unsigned char buf3[] =
{
	0x02, /* 1 byte STX */
	0x00, 0x00, 0x00, 0x00, /* id 4 bytes big endian */

	0x00, 0x00, 0x00, /* sender 1 byte, receiver 1 byte and writeback 1 byte */

	0x00, 0x00, 0x00, 0x00, /* length 4 bytes big endian */

	0x03, /* 1 byte ETX */
	0x00, 0x00, 0x58, 0x67, /* 29 bytes garbage */
	0xf2, 0xb6, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00,
	0x00, 0x10, 0x69, 0xf2,
	0xb6, 0x58, 0x67, 0xf2,
	0xb6, 0x44, 0x05, 0x9e,
	0x04, 0x64, 0xf2, 0xb6,
	0xa0
};
struct fgevent expected3 = {0, 0, 0, 0, 0, NULL};

int test_parse (unsigned char *buffer, size_t len, struct fgevent *expected)
{
	unsigned char *ptr;
	struct fgevent fgev;

	ptr = buffer;
	fg_parse_fgevent (&fgev, buffer, len, &ptr);

	if (fgev.id != expected->id)
		return -1;
	else if (fgev.writeback != expected->writeback)
		return -1;
	else if (fgev.length != expected->length)
		return -1;
	else
	  {
	  	for (int i = 0; i < fgev.length; i++)
	  	  {
	  	  	if (fgev.payload[i] != expected->payload[i])
	  	  		return -1;
	  	  }
	  }
	return 0;
}

int
main (void)
{
	/* Test 1 */
	if (test_parse (&(buf1[0]), LEN(buf1), &expected1) < 0)
	  {
	  	PRINT_FAIL("test 1");
		return EXIT_FAILURE;
	  }

	/* Test 2 */
	if (test_parse (&(buf2[0]), LEN(buf2), &expected2) < 0)
	  {
	  	PRINT_FAIL("test 2");
		return EXIT_FAILURE;
	  }

	/* Test 3 */
	if (test_parse (&(buf3[0]), LEN(buf3), &expected3) < 0)
	  {
	  	PRINT_FAIL("test 3");
		return EXIT_FAILURE;
	  }

	PRINT_SUCCESS("all tests passed");
	return EXIT_SUCCESS;
}