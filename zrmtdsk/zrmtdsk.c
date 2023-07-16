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

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <x68k/dos.h>
#include <x68k/iocs.h>
#include <setjmp.h>
#include "zrmtdsk.h"

//#define DEBUG

struct reqh *reqheader;
jmp_buf jenv;
bool recovery = false;

/****************************************************************************/
/* for debugging                                                            */
/****************************************************************************/

#ifdef DEBUG
char heap[1024];                // temporary heap for debug print
void *_HSTA = heap;
void *_HEND = heap + 1024;
void *_PSP;

void debugf(char *fmt, ...)
{
  char buf[256];
  va_list ap;

  va_start(ap, fmt);
  vsiprintf(buf, fmt, ap);
  va_end(ap);
  _iocs_b_print(buf);
}

void namests(struct dos_namestbuf *b)
{
  int i;
  debugf("name: 0x%x %c:", b->flg, b->drive + 'A');
  for (i = 0; i < 65 && b->path[i]; i++) {
      debugf("%c", (uint8_t)b->path[i] == 9 ? '\\' : (uint8_t)b->path[i]);
  }
  debugf("%.8s%.10s.%.3s\r\n", b->name1, b->name2, b->ext);
}

#else
#define debugf(fmt, ...)
#define namests(buf)
#endif

/****************************************************************************/
/* Communication                                                            */
/****************************************************************************/

void out232c(uint8_t c)
{
  while (_iocs_osns232c() == 0)
    ;
  _iocs_out232c(c);
}

void serout(void *buf, size_t len)
{
  uint8_t *p = buf;

  if (recovery) {
    debugf("error recovery\r\n");
    for (int i = 0; i < 1030; i++) {
      if (_iocs_isns232c()) {
        _iocs_inp232c();
      }
      out232c('Z');
    }
    while (_iocs_isns232c())
      _iocs_inp232c();
    recovery = false;
  }

  out232c('Z');
  out232c('Z');
  out232c('X');
  out232c(len >> 8);
  out232c(len & 0xff);
  while (len-- > 0) {
    // debugf("%02x ", *(uint8_t *)p);
    out232c(*p++);
  }
  // debugf("\r\n");
}

int inp232c(void)
{
  struct iocs_time tim;
  int sec;
  tim = _iocs_ontime();
  sec = tim.sec;

  while (_iocs_isns232c() == 0) {
    tim = _iocs_ontime();
    if (((tim.sec - sec) % 8640000) > 300)
      longjmp(jenv, -1);
  }
  return _iocs_inp232c() & 0xff;
}

void serin(void *buf, size_t len)
{
  uint8_t *p = buf;
  uint8_t c;
  size_t size;

  do {
      c = inp232c();
  } while (c != 'Z');
  do {
      c = inp232c();
  } while (c == 'Z');
  if (c != 'X')
    debugf("error %02x\r\n", c);

  size = inp232c() << 8;
  size += inp232c();
//    debugf("receive size=%d %d\r\n", size, len);
  len = size;

  while (len-- > 0) {
    c = inp232c();
//        debugf("%02x ", c);
    *p++ = c;
  }
//    debugf("\r\n");

#if 0
  int i;
  debugf("[");
  for (i = 0; i < size; i++) {
    debugf("%02x ", ((uint8_t *)buf)[i]);
  }
  debugf("]\r\n");
#endif
}

/****************************************************************************/
/* Utility routine                                                          */
/****************************************************************************/

ssize_t send_read(int cmd, uint32_t fcb, char *buf, size_t len)
{
  struct read f;
  f.command = cmd;
  f.fcb = (uint32_t)fcb;
  f.len = (uint32_t)len;

  debugf("fcb: 0x%08x\r\n", (uint32_t)fcb);
  debugf("addr: 0x%08x\r\n", (uint32_t)buf);
  debugf("len: 0x%08x\r\n", (uint32_t)len);

  serout(&f, sizeof(f));
  struct read_res fr;
  uint8_t *p = (uint8_t *)buf;
  uint32_t l = 0;
  while (1) {
    serin(&fr, sizeof(fr));
    if (fr.len == 0)
      break;
  //    debugf("copy: 0x%08x %d\r\n", (int)p, fr.len);
    memcpy(p, fr.data, fr.len);
    p += fr.len;
    l += fr.len;
    serout(&f, 1);
  }
  debugf("read len: 0x%08x\r\n", l);
  return l;
}

ssize_t send_write(int cmd, uint32_t fcb, char *buf, size_t len)
{
  struct write f;
  f.command = cmd;
  f.fcb = (uint32_t)fcb;
  f.len = (uint32_t)len;

  debugf("fcb: 0x%08x\r\n", (uint32_t)fcb);
  debugf("addr: 0x%08x\r\n", (uint32_t)buf);
  debugf("len: 0x%08x\r\n", (uint32_t)len);

  serout(&f, sizeof(f));
  struct write1 f1;
  struct write_res fr;
  uint8_t *p = (uint8_t *)buf;
  uint32_t l = 0;

  serin(&fr, sizeof(fr));
  while (len > 0 && fr.len > 0) {
    size_t s = len > sizeof(f1.data) ? sizeof(f1.data) : len;
    debugf("copy: 0x%08x %d\r\n", (int)p, s);
    memcpy(f1.data, p, s);
    f1.len = s;
    serout(&f1, s + 2);
    serin(&fr, sizeof(fr));
    p += fr.len;
    len -= fr.len;
    l += fr.len;
  }
  debugf("write len: 0x%08x\r\n", l);
  return l;
}

struct dcache {
  uint32_t fcb;
  uint32_t offset;
  uint32_t len;
  bool dirty;
  uint8_t cache[1024];
} dcache;

void dcache_flash(uint32_t fcb, bool clean)
{
    if (dcache.fcb == fcb && dcache.dirty) {
      send_write(0x4d, dcache.fcb, dcache.cache, dcache.len);
      dcache.fcb = 0;
    }
    if (clean)
      dcache.fcb = 0;
}

/****************************************************************************/
/* Device driver interrupt rountine                                         */
/****************************************************************************/

void interrupt(void)
{
  uint16_t err = 0;
  struct reqh *r = reqheader;

  debugf("command: 0x%02x\r\n", r->command);

  if (setjmp(jenv)) {
    debugf("command timeout\r\n");
    r->errh = 0x10;
    r->errl = 0x02;
    r->status = -1;
    recovery = true;
    return;
  }

  switch ((r->command) & 0x7f)
  {
  case 0x40:
    _dos_print("\r\nX68000 Remote Disk Driver test -- Drive ");
    _dos_putchar('A' + *(char *)&r->fcb);
    _dos_print(":\r\n");
    extern char _end;
    r->attr = 1; /* Number of units */
    r->addr = &_end;

    // stop 1 / nonparity / 8bit / nonxoff / 38400
    _iocs_set232c(0x4c09);
    break;

  case 0x41: /* dir search */
  {
    namests((struct dos_namestbuf *)r->addr);
    struct dirop f;
    f.command = r->command;
    memcpy(&f.path, r->addr, sizeof(struct dos_namestbuf));
    serout(&f, sizeof(f));

    struct dirop_res fr;
    serin(&fr, sizeof(fr));
    r->status = fr.res;
    break;
  }

  case 0x42: /* mkdir */
  {
    namests((struct dos_namestbuf *)r->addr);
    struct dirop f;
    f.command = r->command;
    memcpy(&f.path, r->addr, sizeof(struct dos_namestbuf));
    serout(&f, sizeof(f));

    struct dirop_res fr;
    serin(&fr, sizeof(fr));
    r->status = fr.res;
    break;
  }

  case 0x43: /* rmdir */
  {
    namests((struct dos_namestbuf *)r->addr);
    struct dirop f;
    f.command = r->command;
    memcpy(&f.path, r->addr, sizeof(struct dos_namestbuf));
    serout(&f, sizeof(f));

    struct dirop_res fr;
    serin(&fr, sizeof(fr));
    r->status = fr.res;
    break;
  }

  case 0x44: /* rename */
  {
    namests((struct dos_namestbuf *)r->addr);
    namests((struct dos_namestbuf *)r->status);
    struct rename f;
    f.command = r->command;
    memcpy(&f.pathold, r->addr, sizeof(struct dos_namestbuf));
    memcpy(&f.pathnew, (void *)r->status, sizeof(struct dos_namestbuf));
    serout(&f, sizeof(f));

    struct dirop_res fr;
    serin(&fr, sizeof(fr));
    r->status = fr.res;
    break;
  }

  case 0x45: /* remove */
  {
    namests((struct dos_namestbuf *)r->addr);
    struct dirop f;
    f.command = r->command;
    memcpy(&f.path, r->addr, sizeof(struct dos_namestbuf));
    serout(&f, sizeof(f));

    struct dirop_res fr;
    serin(&fr, sizeof(fr));
    r->status = fr.res;
    break;
  }

  case 0x46: /* getsetattr */
  {
//        namests((struct dos_namestbuf *)r->addr);
    struct getsetattr f;
    f.command = r->command;
    f.attr = r->attr;
    memcpy(&f.path, r->addr, sizeof(struct dos_namestbuf));
    serout(&f, sizeof(f));

    struct getsetattr_res fr;
    serin(&fr, sizeof(fr));
    r->status = fr.res;
    debugf("attr: %02x res %d\r\n", r->attr, r->status);
    break;
  }

  case 0x47: /* files */
  {
    //        namests((struct dos_namestbuf *)r->addr);
    //        debugf("attr: %02x name: %08x buf: %08x\r\n", r->attr, r->addr, r->status);
    struct files f;
    f.command = r->command;
    f.attr = r->attr;
    f.filep = (uint32_t)r->status;
    memcpy(&f.path, r->addr, sizeof(struct dos_namestbuf));
    serout(&f, sizeof(f));

    struct files_res fr;
    struct dos_filbuf *fb = (struct dos_filbuf *)r->status;
    serin(&fr, sizeof(fr));
    memcpy(&fb->atr, &fr.file.atr, sizeof(fr.file) - 1);
    r->status = fr.res;
    break;
  }

  case 0x48: /* nfiles */
  {
    struct nfiles f;
    f.command = r->command;
    f.filep = (uint32_t)r->status;
    serout(&f, sizeof(f));

    struct nfiles_res fr;
    struct dos_filbuf *fb = (struct dos_filbuf *)r->status;
    serin(&fr, sizeof(fr));
    memcpy(&fb->atr, &fr.file.atr, sizeof(fr.file) - 1);
    r->status = fr.res;
    break;
  }

  case 0x49: /* create */
  {
    namests((struct dos_namestbuf *)r->addr);

    struct create f;
    f.command = r->command;
    f.attr = r->attr;
    f.mode = r->status;
    f.fcbp = (uint32_t)r->fcb;
    memcpy(&f.path, r->addr, sizeof(struct dos_namestbuf));
    memcpy(&f.fcb, r->fcb, 68);
    serout(&f, sizeof(f));

    struct create_res fr;
    serin(&fr, sizeof(fr));
    memcpy(r->fcb, &f.fcb, 68);

    debugf("fcb:0x%08x\r\n", (uint32_t)r->fcb);
    for (int i = 0; i < 68; i++) {
      if ((i % 16) == 0) debugf("%02x: ", i);
      debugf("%02x ", fr.fcb[i]);
      if ((i % 16) == 15) debugf("\r\n");
    }
    debugf("\r\n");

    r->status = fr.res;
    break;
  }

  case 0x4a: /* open */
  {
    namests((struct dos_namestbuf *)r->addr);

    struct open f;
    f.command = r->command;
    f.fcbp = (uint32_t)r->fcb;
    memcpy(&f.path, r->addr, sizeof(struct dos_namestbuf));
    memcpy(&f.fcb, r->fcb, 68);
    serout(&f, sizeof(f));

    struct open_res fr;
    serin(&fr, sizeof(fr));
    memcpy(r->fcb, &f.fcb, 68);

    debugf("fcb:0x%08x\r\n", (uint32_t)r->fcb);
    for (int i = 0; i < 68; i++) {
      if ((i % 16) == 0) debugf("%02x: ", i);
      debugf("%02x ", fr.fcb[i]);
      if ((i % 16) == 15) debugf("\r\n");
    }
    debugf("\r\n");

    r->status = fr.res;
    break;
  }

  case 0x4b: /* close */
  {
    dcache_flash((uint32_t)r->fcb, true);

    struct close f;
    f.command = r->command;
    f.fcb = (uint32_t)r->fcb;
    serout(&f, sizeof(f));

    struct close_res fr;
    serin(&fr, sizeof(fr));

    r->status = fr.res;
    break;
  }

  case 0x4c: /* read */
  {
    dcache_flash((uint32_t)r->fcb, false);

    uint32_t *pp = (uint32_t *)(&((uint8_t *)r->fcb)[6]);
    uint32_t l;



    debugf("pos: 0x%08x\r\n", *pp);
    l = send_read(r->command, (uint32_t)r->fcb, (char *)r->addr, (uint32_t)r->status);
    *pp += l;
    debugf("pos: 0x%08x\r\n", *pp);
    r->status = l;
    break;
  }

  case 0x4d: /* write */
  {
    uint32_t *pp = (uint32_t *)(&((uint8_t *)r->fcb)[6]);
    uint32_t *sp = (uint32_t *)(&((uint8_t *)r->fcb)[64]);
    uint32_t l;

    uint32_t len = (uint32_t)r->status;

    if (len < sizeof(dcache.cache)) {
      if (dcache.fcb == (uint32_t)r->fcb) {
        if ((*pp = dcache.offset + dcache.len) &&
            ((*pp + len) <= (dcache.offset + sizeof(dcache.cache)))) {
          memcpy(dcache.cache + dcache.len, (char *)r->addr, len);
          dcache.len += len;
          *pp += len;
          if (*pp > *sp)
            *sp = *pp;
          break;
        } else {
          dcache_flash((uint32_t)r->fcb, true);
        }
      }
      if (dcache.fcb == 0) {
        dcache.fcb = (uint32_t)r->fcb;
        dcache.offset = *pp;
        memcpy(dcache.cache, (char *)r->addr, len);
        dcache.len = len;
        dcache.dirty = true;
        *pp += len;
        if (*pp > *sp)
          *sp = *pp;
        break;
      }
    }

    dcache_flash((uint32_t)r->fcb, false);

    debugf("pos: 0x%08x\r\n", *pp);
    l = send_write(r->command, (uint32_t)r->fcb, (char *)r->addr, (uint32_t)r->status);
    if (l == 0) {
      *sp = *pp;
    } else {
      *pp += l;
      if (*pp > *sp)
        *sp = *pp;
    }
    debugf("pos: 0x%08x\r\n", *pp);
    debugf("len: 0x%08x\r\n", *sp);
    r->status = l;
    break;
  }

  case 0x4e: /* seek */
  {
    dcache_flash((uint32_t)r->fcb, false);

    struct seek f;
    f.command = r->command;
    f.fcb = (uint32_t)r->fcb;
    f.whence = r->attr;
    f.offset = r->status;

    debugf("fcb: 0x%08x\r\n", (uint32_t)r->fcb);
    debugf("whence: 0x%02x\r\n", r->attr);
    debugf("offset: 0x%08x\r\n", (uint32_t)r->status);

    serout(&f, sizeof(f));

    struct seek_res fr;
    serin(&fr, sizeof(fr));
    debugf("pos: 0x%08x\r\n", fr.pos);
    *(uint32_t *)(&((uint8_t *)r->fcb)[6]) = fr.pos;

    r->status = fr.res;
    break;
  }

  case 0x4f: /* filedate */
  {
    struct filedate f;
    f.command = r->command;
    f.fcb = (uint32_t)r->fcb;
    f.time = r->status & 0xffff;
    f.date = r->status >> 16;

    debugf("fcb: 0x%08x\r\n", (uint32_t)r->fcb);
    debugf("datetime: 0x%08x\r\n", r->status);

    serout(&f, sizeof(f));

    struct filedate_res fr;
    serin(&fr, sizeof(fr));
    r->status = fr.time + (fr.date << 16);
    break;
  }

  case 0x50: /* dskfre */
  {
    struct dskfre f;
    f.command = r->command;
    serout(&f, sizeof(f));

    struct dskfre_res fr;
    serin(&fr, sizeof(fr));
    uint32_t *p = (uint32_t *)r->addr;
    p[0] = fr.freeclu;
    p[1] = fr.totalclu;
    p[2] = fr.clusect;
    p[3] = fr.sectsize;
    r->status = fr.res;
    break;
  }

  case 0x51: /* drvctrl */
    r->attr = 2;
    r->status = 0;
    break;

  case 0x52: /* getdbp */
  {
    uint32_t *p = (uint32_t *)r->addr;
    p[0] = 0;
    p[4] = 0;
    p[8] = 0;
    p[12] = 0;
    r->status = 0;
    break;
  }

  case 0x53: /* ioctl in */
    r->status = 0;
    break;

  case 0x54: /* ioctl out */
    r->status = 0;
    break;

  case 0x55: /* ioctl */
    r->status = 0;
    break;

  case 0x56: /* abort */
    r->status = 0;
    break;

  case 0x57: /* check */
    r->status = 0;
    break;

  case 0x58: /* excl */
    r->status = 0;
    break;

  default:
    break;
  }

  r->errl = err & 0xff;
  r->errh = err >> 8;
}

/****************************************************************************/
/* Dummy program entry                                                      */
/****************************************************************************/

void _start(void)
{}
