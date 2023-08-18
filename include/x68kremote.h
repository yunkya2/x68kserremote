/*
 * Copyright (c) 2023 Yuichi Nakamura (@yunkya2)
 *
 * The MIT License (MIT)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef _X68KREMOTE_H_
#define _X68KREMOTE_H_

#include <stdint.h>

//****************************************************************************
// unaligned big-endian variable
//****************************************************************************

typedef uint8_t uabe16_t[2];
typedef uint8_t uabe32_t[4];

#define get_uabe16(x)       (((x)[0] << 8) | ((x)[1]))
#define get_uabe32(x)       (((x)[0] << 24) | ((x)[1] << 16) | ((x)[2] << 8) | ((x)[3]))
#define set_uabe16(x, v)    do { (x)[0] = ((v) >> 8) & 0xff; \
                                 (x)[1] = (v) & 0xff; } while (0)
#define set_uabe32(x, v)    do { (x)[0] = ((v) >> 24) & 0xff; \
                                 (x)[1] = ((v) >> 16) & 0xff; \
                                 (x)[2] = ((v) >> 8) & 0xff; \
                                 (x)[3] = (v) & 0xff; } while (0)

//****************************************************************************
// Human68k error code
//****************************************************************************
#ifndef _DOSE_ILGFNC
#define _DOSE_ILGFNC    -1
#define _DOSE_NOENT     -2
#define _DOSE_NODIR     -3
#define _DOSE_MFILE     -4
#define _DOSE_ISDIR     -5
#define _DOSE_BADF      -6
#define _DOSE_BROKNMEM  -7
#define _DOSE_NOMEM     -8
#define _DOSE_ILGMPTR   -9
#define _DOSE_ILGENV    -10
#define _DOSE_ILGFMT    -11
#define _DOSE_ILGARG    -12
#define _DOSE_ILGFNAME  -13
#define _DOSE_ILGPARM   -14
#define _DOSE_ILGDRV    -15
#define _DOSE_ISCURDIR  -16
#define _DOSE_CANTIOC   -17
#define _DOSE_NOMORE    -18
#define _DOSE_RDONLY    -19
#define _DOSE_EXISTDIR  -20
#define _DOSE_NOTEMPTY  -21
#define _DOSE_CANTREN   -22
#define _DOSE_DISKFULL  -23
#define _DOSE_DIRFULL   -24
#define _DOSE_CANTSEEK  -25
#define _DOSE_SUPER     -26
#define _DOSE_DUPTHNAM  -27
#define _DOSE_CANTSEND  -28
#define _DOSE_THFULL    -29
#define _DOSE_LCKFULL   -32
#define _DOSE_LCKERR    -33
#define _DOSE_BUSYDRV   -34
#define _DOSE_SYMLOOP   -35
#define _DOSE_EXISTFILE -80
#endif

//****************************************************************************
// Human68k structures
//****************************************************************************

struct dos_req_header {
  uint8_t magic;       // +0x00.b  Constant (26)
  uint8_t unit;        // +0x01.b  Unit number
  uint8_t command;     // +0x02.b  Command code
  uint8_t errl;        // +0x03.b  Error code low
  uint8_t errh;        // +0x04.b  Error code high
  uint8_t reserved[8]; // +0x05 .. +0x0c  not used
  uint8_t attr;        // +0x0d.b  Attribute / Seek mode
  void *addr;          // +0x0e.l  Buffer address
  uint32_t status;     // +0x12.l  Bytes / Buffer / Result status
  void *fcb;           // +0x16.l  FCB
} __attribute__((packed, aligned(2)));

struct dos_filesinfo {
  uint8_t dummy;
  uint8_t atr;
  uint16_t time;
  uint16_t date;
  uint32_t filelen;
  char name[23];
} __attribute__((packed, aligned(2)));

typedef struct {
  uint8_t flag;
  uint8_t drive;
  uint8_t path[65];
  uint8_t name1[8];
  uint8_t ext[3];
  uint8_t name2[10];
} dos_namebuf;

//****************************************************************************
// ZRMTDSK serial communication protocol definition
//****************************************************************************

struct cmd_dirop {
  uint8_t command;
  dos_namebuf path;
} __attribute__((packed, aligned(2)));
struct res_dirop {
  int8_t res;
} __attribute__((packed, aligned(2)));

struct cmd_rename {
  uint8_t command;
  dos_namebuf path_old;
  dos_namebuf path_new;
} __attribute__((packed, aligned(2)));
struct res_rename {
  int8_t res;
} __attribute__((packed, aligned(2)));

struct cmd_chmod {
  uint8_t command;
  uint8_t attr;
  dos_namebuf path;
} __attribute__((packed, aligned(2)));
struct res_chmod {
  int8_t res;
} __attribute__((packed, aligned(2)));

struct cmd_files {
  uint8_t command;
  uint8_t attr;
  uint32_t filep;
  dos_namebuf path;
} __attribute__((packed, aligned(2)));
struct res_files {
  int8_t res;
  struct dos_filesinfo file;
} __attribute__((packed, aligned(2)));

struct cmd_nfiles {
  uint8_t command;
  uint32_t filep;
} __attribute__((packed, aligned(2)));
struct res_nfiles {
  int8_t res;
  struct dos_filesinfo file;
} __attribute__((packed, aligned(2)));

struct cmd_create {
  uint8_t command;
  uint8_t attr;
  uint8_t mode;
  uint32_t fcb;
  dos_namebuf path;
} __attribute__((packed, aligned(2)));
struct res_create {
  int8_t res;
} __attribute__((packed, aligned(2)));

struct cmd_open {
  uint8_t command;
  uint8_t mode;
  uint32_t fcb;
  dos_namebuf path;
} __attribute__((packed, aligned(2)));
struct res_open {
  int8_t res;
  uint32_t size;
} __attribute__((packed, aligned(2)));

struct cmd_close {
  uint8_t command;
  uint32_t fcb;
} __attribute__((packed, aligned(2)));
struct res_close {
  int8_t res;
} __attribute__((packed, aligned(2)));

struct cmd_read {
  uint8_t command;
  uint32_t fcb;
  uint32_t len;
} __attribute__((packed, aligned(2)));
struct cmd_read_ack {
  uint8_t ack;
} __attribute__((packed, aligned(2)));
struct res_read {
  int16_t len;
  uint8_t data[1024];
} __attribute__((packed, aligned(2)));

struct cmd_write {
  uint8_t command;
  uint32_t fcb;
  uint32_t len;
} __attribute__((packed, aligned(2)));
struct cmd_write_body {
  int16_t len;
  uint8_t data[1024];
} __attribute__((packed, aligned(2)));
struct res_write {
  int16_t len;
} __attribute__((packed, aligned(2)));

struct cmd_seek {
  uint8_t command;
  uint8_t whence;
  uint32_t fcb;
  int32_t offset;
} __attribute__((packed, aligned(2)));
struct res_seek {
  int8_t res;
  uint32_t pos;
} __attribute__((packed, aligned(2)));

struct cmd_filedate {
  uint8_t command;
  uint32_t fcb;
  uint16_t time;
  uint16_t date;
} __attribute__((packed, aligned(2)));
struct res_filedate {
  uint16_t time;
  uint16_t date;
} __attribute__((packed, aligned(2)));

struct cmd_dskfre {
  uint8_t command;
} __attribute__((packed, aligned(2)));
struct res_dskfre {
  int32_t res;
  uint16_t freeclu;
  uint16_t totalclu;
  uint16_t clusect;
  uint16_t sectsize;
} __attribute__((packed, aligned(2)));

#endif /* _X68KREMOTE_H_ */
