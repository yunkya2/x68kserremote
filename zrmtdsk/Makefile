CROSS = m68k-xelf-
CC = $(CROSS)gcc
AS = $(CROSS)gcc
LD = $(CROSS)gcc
AR = $(CROSS)ar
RANLIB = $(CROSS)ranlib

CFLAGS = -m68000 -I. -I../include -O3
ASFLAGS = -m68000 -I.

all: zrmtdsk.sys

zrmtdsk.sys: head.o zrmtdsk.o
	$(LD) -o $@ $^ -nostartfiles

zrmtdsk.o: ../include/x68kremote.h

%.o: %.c
	$(CC) $(CFLAGS) -c $<

%.o: %.S
	$(AS) $(ASFLAGS) -c $<

clean:
	-rm -f *.o *.sys *.elf*
