#
# Makefile:
#   defines rules to build the fagelmatare events module. Setup for cross
#   compilation with crosstool-ng
##############################################################################
#  This file is part of Fågelmataren, an embedded project created to learn
#  Linux and C. See <https://github.com/Linkaan/Fagelmatare>
#  Copyright (C) 2015-2017 Linus Styrén
#
#  Fågelmataren is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 3 of the Licence, or
#  (at your option) any later version.
#
#  Fågelmataren is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public Licence for more details.
#
#  You should have received a copy of the GNU General Public Licence
#  along with Fågelmataren.  If not, see <http://www.gnu.org/licenses/>.
##############################################################################

MAJOR := 0
MINOR := 0
NAME := fg-events
VERSION := $(MAJOR).$(MINOR)

INCLUDE ?= -I.
LINKS ?= -L.
CFLAGS := $(INCLUDE) -std=gnu11 -g -Wall -Wextra -D _GNU_SOURCE
LIBS := -lfg-serializer -levent -levent_pthreads
LDFLAGS := $(LINKS) $(LIBS) -shared -Wl,-soname,lib$(NAME).so.$(MAJOR)
SOURCES := fgevents.c
HEADERS := fgevents.h
OBJECTS = $(SOURCES:.c=.o)

TESTS = $(patsubst test/%.c, test/%_test, $(wildcard test/*.c))

all: $(SOURCES) lib$(NAME).so.$(VERSION) $(TESTS)

lib$(NAME).so.$(VERSION): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

%.o: %.c $(HEADERS)
    ifndef CC
	$(error CC not set, please invoke with CC set to path of arm-rpi-linux-gnueabihf-gcc)
    endif
	$(CC) -c $< -o $@ $(CFLAGS) -fPIC

$(TESTS): test/%_test : test/%.c
	$(CC) -o $@ $^ $(CFLAGS) $(LINKS) $(LIBS) -lcrypto -lz -l$(NAME)

.PHONY: clean

clean:
	rm -f lib$(NAME).so* $(OBJECTS)
