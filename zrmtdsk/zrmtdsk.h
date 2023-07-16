/*
 * Copyright (c) 2023 Yuichi Nakamura
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

#ifndef _ZRMTDSK_H_
#define _ZRMTDSK_H_

#include <stdint.h>
#include <x68k/dos.h>

/****************************************************************************/
/* Request header structure                                                 */
/****************************************************************************/

struct reqh {
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


/****************************************************************************/
/* ZRMTDSK serial communication protocol defintion                          */
/****************************************************************************/

struct dirop {
  uint8_t command;
  struct dos_namestbuf path;
};

struct dirop_res {
  int8_t res;
};

struct rename {
  uint8_t command;
  struct dos_namestbuf pathold;
  struct dos_namestbuf pathnew;
};

struct rename_res {
  int8_t res;
};

struct getsetattr {
  uint8_t command;
  uint8_t attr;
  struct dos_namestbuf path;
};

struct getsetattr_res {
  int8_t res;
};

struct filesinfo {
  uint8_t     dummy;
  uint8_t     atr;
  uint16_t    time;
  uint16_t    date;
  uint32_t    filelen;
  char        name[23];
} __attribute__((packed,aligned(2)));

struct files {
  uint8_t command;
  uint8_t attr;
  uint8_t dummy[2];
  uint32_t filep;
  struct dos_namestbuf path;
};

struct files_res {
  int8_t res;
  struct filesinfo file;
};

struct nfiles {
  uint8_t command;
  uint8_t dummy[3];
  uint32_t filep;
};

struct nfiles_res {
  int8_t res;
  struct filesinfo file;
};

struct create {
  uint8_t command;
  uint8_t attr;
  uint8_t mode;
  uint8_t dummy;
  uint32_t fcbp;
  struct dos_namestbuf path;
  uint8_t fcb[68];
};
struct create_res {
  int8_t res;
  uint8_t fcb[68];
};

struct open {
  uint8_t command;
  uint8_t dummy[3];
  uint32_t fcbp;
  struct dos_namestbuf path;
  uint8_t fcb[68];
};
struct open_res {
  int8_t res;
  uint8_t fcb[68];
};

struct close {
  uint8_t command;
  uint8_t dummy[3];
  uint32_t fcb;
};
struct close_res {
  int8_t res;
};

struct read {
  uint8_t command;
  uint8_t dummy[3];
  uint32_t fcb;
  uint32_t len;
};
struct read_res {
  uint16_t len;
  uint8_t data[1024];
};

struct write {
  uint8_t command;
  uint8_t dummy[3];
  uint32_t fcb;
  uint32_t len;
};
struct write1 {
  uint16_t len;
  uint8_t data[1024];
};
struct write_res {
  uint16_t len;
};

struct seek {
  uint8_t command;
  uint8_t whence;
  uint8_t dummy[2];
  uint32_t fcb;
  int32_t offset;
};
struct seek_res {
  int8_t res;
  uint8_t dummy[3];
  uint32_t pos;
};

struct filedate {
  uint8_t command;
  uint8_t dummy[3];
  uint32_t fcb;
  uint16_t time;
  uint16_t date;
};
struct filedate_res {
  uint16_t time;
  uint16_t date;
};

struct dskfre {
  uint8_t command;
};
struct dskfre_res {
  uint32_t res;
  uint16_t freeclu;
  uint16_t totalclu;
  uint16_t clusect;
  uint16_t sectsize;
};

#endif /* _ZRMTDKSK_H_ */
