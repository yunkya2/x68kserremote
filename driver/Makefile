#
# Copyright (c) 2023,2026 Yuichi Nakamura (@yunkya2)
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

CROSS = m68k-xelf-
CC = $(CROSS)gcc
AS = $(CROSS)gcc
LD = $(CROSS)gcc
AR = $(CROSS)ar
RANLIB = $(CROSS)ranlib
OBJCOPY = $(CROSS)objcopy

GIT_REPO_VERSION=$(shell git describe --tags --always)

LIBTSR = ../libtsr
CFLAGS = -g -m68000 -I. -I../include -I$(LIBTSR)/include -Os -DGIT_REPO_VERSION=\"$(GIT_REPO_VERSION)\"
ASFLAGS = -m68000 -I.

ifneq ($(DEBUG),)
CFLAGS += -DDEBUG
endif

all: serremote.x

serremote.x: main.o tsr.o $(LIBTSR)/lib/libx68ktsr.a
	$(LD) -o $@ main.o tsr.o -L$(LIBTSR)/lib -lx68ktsr -s -specs=nano.specs

tsr.o: serremote.o remotedrv.o $(LIBTSR)/lib/libx68ktsr.a
	$(LIBTSR)/bin/m68k-xelf-tsrgen -o $@ serremote.o remotedrv.o -u com_init

$(LIBTSR)/lib/libx68ktsr.a:
	$(MAKE) -C $(LIBTSR)

vpath %.h ../include

main.o:      config.h x68kremote.h remotedrv.h serremote.h
serremote.o: config.h x68kremote.h remotedrv.h serremote.h
remotedrv.o: config.h x68kremote.h remotedrv.h

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.S
	$(AS) $(ASFLAGS) -c -o $@ $<

# リモートドライブを起動ドライブとするディスクイメージファイルを生成する
#   make bootimage [HUMANSYS=<HUMAN.SYSのパス>]  -> serboot.hds
HUMANSYS ?= HUMAN.SYS

bootimage: serboot.hds

serboot.hds: bootloader.bin serboot.bin
	@test -f "$(HUMANSYS)" || { echo "$(HUMANSYS) が見つかりません (HUMANSYS=<HUMAN.SYSのパス> で指定できます)"; exit 1; }
	./mkscsiimg.py $^ $(HUMANSYS) $@

serboot.bin: serboot.sys
	./fixupsys.py $< $@

serboot.sys: bootdrv.o boot_main.o boot_serremote.o boot_remotedrv.o
	$(LD) -o $@ $^ -nostartfiles -s

bootdrv.o:        config.h
boot_main.o:      config.h x68kremote.h remotedrv.h serremote.h
boot_serremote.o: config.h x68kremote.h remotedrv.h serremote.h
boot_remotedrv.o: config.h x68kremote.h remotedrv.h

boot_%.o: %.c
	$(CC) $(CFLAGS) -DCONFIG_BOOTDRIVER -c -o $@ $<

bootloader.bin: bootloader.o
	$(OBJCOPY) -O binary $< $@

clean:
	-rm -f *.o *.sys *.x *.elf* *.bin *.hds

.PHONY: all clean bootimage
