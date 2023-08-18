CROSS = m68k-xelf-
CC = $(CROSS)gcc
AS = $(CROSS)gcc
LD = $(CROSS)gcc
AR = $(CROSS)ar
RANLIB = $(CROSS)ranlib

GIT_REPO_VERSION=$(shell git describe --tags --always)

CFLAGS = -m68000 -I. -I../include -O3 -DGIT_REPO_VERSION=\"$(GIT_REPO_VERSION)\"
CFLAGS += -finput-charset=utf-8 -fexec-charset=cp932
ASFLAGS = -m68000 -I.

all: zrmtdsk.sys

zrmtdsk.sys: head.o zrmtdsk.o
	$(LD) -o $@ $^ -nostartfiles -s

zrmtdsk.o: ../include/x68kremote.h

%.o: %.c
	$(CC) $(CFLAGS) -c $<

%.o: %.S
	$(AS) $(ASFLAGS) -c $<

clean:
	-rm -f *.o *.sys *.elf*
