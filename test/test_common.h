/*
 *  test_common.h
 *    Common header file for unit tests to share macros and defs
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

#include <fgevents.h>

#define LEN(x) sizeof(x)/sizeof(x[0])

/* Print to console with colored output using ANSI escape codes */
#ifdef INTEGRATION_TEST
#define TEST_STR "integration test"
#else
#define TEST_STR "unit test"
#endif

#define PRINT_FAIL(m, ...)\
		fprintf (stderr,\
				 TEST_STR": %s : \x1b[31m"m" failed\x1b[0m\n", __FILE__, ##__VA_ARGS__)

#define PRINT_SUCCESS(m, ...)\
		fprintf (stdout,\
				 TEST_STR": %s : \x1b[32m"m"\x1b[0m\n", __FILE__, ##__VA_ARGS__)
