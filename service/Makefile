#
# Copyright (c) 2023 Yuichi Nakamura (@yunkya2)
#
# The MIT License (MIT)
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

GIT_REPO_VERSION=$(shell git describe --tags --always)

CFLAGS = -g -I. -I../include -O -DGIT_REPO_VERSION=\"$(GIT_REPO_VERSION)\"
ifeq ($(MSYSTEM),MSYS)
LDFLAGS += -liconv
else ifeq  ($(shell uname),Darwin)
LDFLAGS += -liconv
endif

all: x68kremote

x68kremote: x68kremote.o remoteserv.o
	$(CC) -o $@ $^ $(LDFLAGS)

vpath %.h ../include

x68kremote.o: config.h x68kremote.h remoteserv.h
remoteserv.o: config.h x68kremote.h remoteserv.h fileop.h

clean:
	-rm -f *.o *.exe x68kremote

.PHONY: all clean
