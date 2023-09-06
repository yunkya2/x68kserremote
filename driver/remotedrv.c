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

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <x68k/dos.h>

#include "x68kremote.h"
#include "remotedrv.h"

//****************************************************************************
// Global variables
//****************************************************************************

struct dos_req_header *reqheader;   // Human68kからのリクエストヘッダ
jmp_buf jenv;                       //タイムアウト時のジャンプ先

//****************************************************************************
// Utility routine
//****************************************************************************

ssize_t send_read(uint32_t fcb, char *buf, size_t len)
{
  struct cmd_read cmd;
  struct res_read res;
  cmd.command = 0x4c; /* read */
  cmd.fcb = (uint32_t)fcb;
  ssize_t total = 0;

  while (len > 0) {
    size_t size = len > sizeof(res.data) ? sizeof(res.data) : len;
    cmd.len = size;

    com_cmdres(&cmd, sizeof(cmd), &res, sizeof(res));

    DPRINTF1(" read: addr=0x%08x len=%d size=%d\r\n", (uint32_t)buf, len, res.len);
    if (res.len < 0)
      return res.len;
    if (res.len == 0)
      break;

    memcpy(buf, res.data, res.len);
    buf += res.len;
    total += res.len;
    len -= res.len;
  }
  DPRINTF1(" read: total=%d\r\n", total);
  return total;
}

ssize_t send_write(uint32_t fcb, char *buf, size_t len)
{
  static struct cmd_write cmd;
  static struct res_write res;
  cmd.command = 0x4d; /* write */
  cmd.fcb = (uint32_t)fcb;
  ssize_t total = 0;

  if (len == 0) {
    cmd.len = len;
    com_cmdres(&cmd, sizeof(cmd), &res, sizeof(res));
  } else {
    while (len > 0) {
      size_t s = len > sizeof(cmd.data) ? sizeof(cmd.data) : len;
      memcpy(cmd.data, buf, s);
      cmd.len = s;

//      com_cmdres(&cmd, sizeof(cmd), &res, sizeof(res));
      com_cmdres(&cmd, 7 + s, &res, sizeof(res));

      DPRINTF1(" write: addr=0x%08x len=%d size=%d\r\n", (uint32_t)buf, len, res.len);
      if (res.len < 0)
        return res.len;
      buf += res.len;
      len -= res.len;
      total += res.len;
    }
  }
  DPRINTF1(" write: total=%d\r\n", total);
  return total;
}

struct dcache {
  uint32_t fcb;
  uint32_t offset;
  int16_t len;
  bool dirty;
  uint8_t cache[1024];
} dcache;

int dcache_flash(uint32_t fcb, bool clean)
{
  int res = 0;
  if (dcache.fcb == fcb) {
    if (dcache.dirty) {
      if (send_write(dcache.fcb, dcache.cache, dcache.len) < 0)
        res = -1;
      dcache.dirty = false;
    }
    if (clean)
      dcache.fcb = 0;
  }
  return res;
}

//****************************************************************************
// Device driver interrupt rountine
//****************************************************************************

void interrupt(void)
{
  uint16_t err = 0;
  struct dos_req_header *req = reqheader;

  if (setjmp(jenv)) {
    com_timeout(req);
    return;
  }

  DPRINTF2("----Command: 0x%02x\r\n", req->command);

  switch ((req->command) & 0x7f) {
  case 0x40: /* init */
  {
    req->command = 0; /* for Human68k bug workaround */
    com_init(req);

    extern char _end;
    req->attr = 1; /* Number of units */
    req->addr = &_end;
    break;
  }

  case 0x41: /* chdir */
  {
    struct cmd_dirop cmd;
    struct res_dirop res;
    cmd.command = req->command;
    memcpy(&cmd.path, req->addr, sizeof(struct dos_namestbuf));
    com_cmdres(&cmd, sizeof(cmd), &res, sizeof(res));
    req->status = res.res;
    DNAMEPRINT(req->addr, false, "CHDIR: ");
    DPRINTF1(" -> %d\r\n", res.res);
    break;
  }

  case 0x42: /* mkdir */
  {
    struct cmd_dirop cmd;
    struct res_dirop res;
    cmd.command = req->command;
    memcpy(&cmd.path, req->addr, sizeof(struct dos_namestbuf));
    com_cmdres(&cmd, sizeof(cmd), &res, sizeof(res));
    req->status = res.res;
    DNAMEPRINT(req->addr, true, "MKDIR: ");
    DPRINTF1(" -> %d\r\n", res.res);
    break;
  }

  case 0x43: /* rmdir */
  {
    struct cmd_dirop cmd;
    struct res_dirop res;
    cmd.command = req->command;
    memcpy(&cmd.path, req->addr, sizeof(struct dos_namestbuf));
    com_cmdres(&cmd, sizeof(cmd), &res, sizeof(res));
    req->status = res.res;
    DNAMEPRINT(req->addr, true, "RMDIR: ");
    DPRINTF1(" -> %d\r\n", res.res);
    break;
  }

  case 0x44: /* rename */
  {
    struct cmd_rename cmd;
    struct res_rename res;
    cmd.command = req->command;
    memcpy(&cmd.path_old, req->addr, sizeof(struct dos_namestbuf));
    memcpy(&cmd.path_new, (void *)req->status, sizeof(struct dos_namestbuf));
    com_cmdres(&cmd, sizeof(cmd), &res, sizeof(res));
    DNAMEPRINT(req->addr, true, "RENAME: ");
    DNAMEPRINT((void *)req->status, true, " to ");
    DPRINTF1(" -> %d\r\n", res.res);
    req->status = res.res;
    break;
  }

  case 0x45: /* delete */
  {
    struct cmd_dirop cmd;
    struct res_dirop res;
    cmd.command = req->command;
    memcpy(&cmd.path, req->addr, sizeof(struct dos_namestbuf));
    com_cmdres(&cmd, sizeof(cmd), &res, sizeof(res));
    req->status = res.res;
    DNAMEPRINT(req->addr, true, "DELETE: ");
    DPRINTF1(" -> %d\r\n", res.res);
    break;
  }

  case 0x46: /* chmod */
  {
    struct cmd_chmod cmd;
    struct res_chmod res;
    cmd.command = req->command;
    cmd.attr = req->attr;
    memcpy(&cmd.path, req->addr, sizeof(struct dos_namestbuf));
    com_cmdres(&cmd, sizeof(cmd), &res, sizeof(res));
    req->status = res.res;
    DNAMEPRINT(req->addr, true, "CHMOD: ");
    DPRINTF1(" 0x%02x -> 0x%02x\r\n", cmd.attr, res.res);
    break;
  }

  case 0x47: /* files */
  {
    struct cmd_files cmd;
    struct res_files res;
    cmd.command = req->command;
    cmd.attr = req->attr;
    cmd.filep = (uint32_t)req->status;
    memcpy(&cmd.path, req->addr, sizeof(struct dos_namestbuf));
    com_cmdres(&cmd, sizeof(cmd), &res, sizeof(res));

    struct dos_filbuf *fb = (struct dos_filbuf *)req->status;
    memcpy(&fb->atr, &res.file[0].atr, sizeof(res.file[0]) - 1);
    req->status = res.res;
    DNAMEPRINT(req->addr, false, "FILES: ");
    DPRINTF1(" attr=0x%02x filep=0x%08x -> %d %s\r\n", cmd.attr, cmd.filep, res.res, res.file[0].name);
    break;
  }

  case 0x48: /* nfiles */
  {
    struct cmd_nfiles cmd;
    struct res_nfiles res;
    cmd.command = req->command;
    cmd.filep = (uint32_t)req->status;
    com_cmdres(&cmd, sizeof(cmd), &res, sizeof(res));

    struct dos_filbuf *fb = (struct dos_filbuf *)req->status;
    memcpy(&fb->atr, &res.file[0].atr, sizeof(res.file[0]) - 1);
    req->status = res.res;
    DPRINTF1("NFILES: filep=0x%08x -> %d %s\r\n", cmd.filep, res.res, res.file[0].name);
    break;
  }

  case 0x49: /* create */
  {
    struct cmd_create cmd;
    struct res_create res;
    cmd.command = req->command;
    cmd.attr = req->attr;
    cmd.mode = req->status;
    cmd.fcb = (uint32_t)req->fcb;
    memcpy(&cmd.path, req->addr, sizeof(struct dos_namestbuf));
    com_cmdres(&cmd, sizeof(cmd), &res, sizeof(res));
    *(uint32_t *)(&((uint8_t *)req->fcb)[64]) = 0;
    req->status = res.res;
    DNAMEPRINT(req->addr, true, "CREATE: ");
    DPRINTF1(" fcb=0x%08x attr=0x%02x mode=%d -> %d\r\n", cmd.fcb, cmd.attr, cmd.mode, res.res);
    break;
  }

  case 0x4a: /* open */
  {
    struct cmd_open cmd;
    struct res_open res;
    cmd.command = req->command;
    cmd.mode = ((uint8_t *)req->fcb)[14];
    cmd.fcb = (uint32_t)req->fcb;
    memcpy(&cmd.path, req->addr, sizeof(struct dos_namestbuf));
    com_cmdres(&cmd, sizeof(cmd), &res, sizeof(res));
    *(uint32_t *)(&((uint8_t *)req->fcb)[64]) = res.size;
    req->status = res.res;
    DNAMEPRINT(req->addr, true, "OPEN: ");
    DPRINTF1(" fcb=0x%08x mode=%d -> %d\r\n", cmd.fcb, cmd.mode, res.res);
    break;
  }

  case 0x4b: /* close */
  {
    dcache_flash((uint32_t)req->fcb, true);

    struct cmd_close cmd;
    struct res_close res;
    cmd.command = req->command;
    cmd.fcb = (uint32_t)req->fcb;
    com_cmdres(&cmd, sizeof(cmd), &res, sizeof(res));
    req->status = res.res;
    DPRINTF1("CLOSE: fcb=0x%08x\r\n", cmd.fcb);
    break;
  }

  case 0x4c: /* read */
  {
    dcache_flash((uint32_t)req->fcb, false);

    uint32_t *pp = (uint32_t *)(&((uint8_t *)req->fcb)[6]);

    char *buf = (char *)req->addr;
    size_t len = (size_t)req->status;
    ssize_t size = 0;

    if (dcache.fcb == 0 || dcache.fcb == (uint32_t)req->fcb) {
      // キャッシュが未使用または自分のデータが入っている場合
      do {
        if (dcache.fcb == (uint32_t)req->fcb &&
            *pp >= dcache.offset && *pp < dcache.offset + dcache.len) {
          // これから読むデータがキャッシュに入っている場合、キャッシュから読めるだけ読む
          size_t clen = dcache.offset + dcache.len - *pp;
          clen = clen < len ? clen : len;

          memcpy(buf, dcache.cache + (*pp - dcache.offset), clen);
          buf += clen;
          len -= clen;
          size += clen;
          *pp += clen;    // FCBのファイルポインタを進める
        }
        if (len == 0 || len >= sizeof(dcache.cache))
          break;
        // キャッシュサイズ未満の読み込みならキャッシュを充填
        dcache_flash((uint32_t)req->fcb, true);
        dcache.len = send_read((uint32_t)req->fcb, dcache.cache, sizeof(dcache.cache));
        if (dcache.len < 0) {
          size = -1;
          goto errout_read;
        }
        dcache.fcb = (uint32_t)req->fcb;
        dcache.offset = *pp;
        dcache.dirty = false;
      } while (dcache.len > 0);
    }

    if (len > 0) {
      size_t rlen;
      rlen = send_read((uint32_t)req->fcb, buf, len);
      if (rlen < 0) {
        size = -1;
        goto errout_read;
      }
      size += rlen;
      *pp += rlen;    // FCBのファイルポインタを進める
    }

errout_read:
    DPRINTF1("READ: fcb=0x%08x %d -> %d\r\n", (uint32_t)req->fcb, req->status, size);
    req->status = size;
    break;
  }

  case 0x4d: /* write */
  {
    uint32_t *pp = (uint32_t *)(&((uint8_t *)req->fcb)[6]);
    uint32_t *sp = (uint32_t *)(&((uint8_t *)req->fcb)[64]);
    size_t len = (uint32_t)req->status;

    if (len > 0 && len < sizeof(dcache.cache)) {  // 書き込みサイズがキャッシュサイズ未満
      if (dcache.fcb == (uint32_t)req->fcb) {     //キャッシュに自分のデータが入っている
        if ((*pp = dcache.offset + dcache.len) &&
            ((*pp + len) <= (dcache.offset + sizeof(dcache.cache)))) {
          // 書き込みデータがキャッシュに収まる場合はキャッシュに書く
          memcpy(dcache.cache + dcache.len, (char *)req->addr, len);
          dcache.len += len;
          goto okout_write;
        } else {    //キャッシュに収まらないのでフラッシュ
          dcache_flash((uint32_t)req->fcb, true);
        }
      }
      if (dcache.fcb == 0) {    //キャッシュが未使用
        // 書き込みデータをキャッシュに書く
        dcache.fcb = (uint32_t)req->fcb;
        dcache.offset = *pp;
        memcpy(dcache.cache, (char *)req->addr, len);
        dcache.len = len;
        dcache.dirty = true;
        goto okout_write;
      }
    }

    dcache_flash((uint32_t)req->fcb, false);
    len = send_write((uint32_t)req->fcb, (char *)req->addr, (uint32_t)req->status);
    if (len == 0) {
      *sp = *pp;      //0バイト書き込み=truncateなのでFCBのファイルサイズをポインタ位置にする
    }

okout_write:
    if (len > 0) {
      *pp += len;     //FCBのファイルポインタを進める
      if (*pp > *sp)
        *sp = *pp;    //FCBのファイルサイズを増やす
    }
    DPRINTF1("WRITE: fcb=0x%08x %d -> %d\r\n", (uint32_t)req->fcb, req->status, len);
    req->status = len;
    break;
  }

  case 0x4e: /* seek */
  {
    dcache_flash((uint32_t)req->fcb, false);

    struct cmd_seek cmd;
    struct res_seek res;
    cmd.command = req->command;
    cmd.fcb = (uint32_t)req->fcb;
    cmd.whence = req->attr;
    cmd.offset = req->status;

    uint32_t pos = *(uint32_t *)(&((uint8_t *)req->fcb)[6]);
    uint32_t size = *(uint32_t *)(&((uint8_t *)req->fcb)[64]);
    pos = (cmd.whence == 0 ? 0 : (cmd.whence == 1 ? pos : size)) + cmd.offset;
    if (pos > size) {   // ファイル末尾を越えてseekしようとした
      req->status = _DOSE_CANTSEEK;
      goto errout_seek;
    }

    com_cmdres(&cmd, sizeof(cmd), &res, sizeof(res));
    *(uint32_t *)(&((uint8_t *)req->fcb)[6]) = res.res;
    req->status = res.res;
errout_seek:
    DPRINTF1("SEEK: fcb=0x%x offset=%d whence=%d -> %d\r\n", cmd.fcb, cmd.offset, cmd.whence, res.res);
    break;
  }

  case 0x4f: /* filedate */
  {
    struct cmd_filedate cmd;
    struct res_filedate res;
    cmd.command = req->command;
    cmd.fcb = (uint32_t)req->fcb;
    cmd.time = req->status & 0xffff;
    cmd.date = req->status >> 16;
    com_cmdres(&cmd, sizeof(cmd), &res, sizeof(res));
    req->status = res.time + (res.date << 16);
    DPRINTF1("FILEDATE: fcb=0x%08x 0x%04x 0x%04x -> 0x%04x 0x%04x\r\n", cmd.fcb, cmd.date, cmd.time, res.date, res.time);
    break;
  }

  case 0x50: /* dskfre */
  {
    struct cmd_dskfre cmd;
    struct res_dskfre res;
    cmd.command = req->command;
    com_cmdres(&cmd, sizeof(cmd), &res, sizeof(res));

    uint16_t *p = (uint16_t *)req->addr;
    p[0] = res.freeclu;
    p[1] = res.totalclu;
    p[2] = res.clusect;
    p[3] = res.sectsize;
    req->status = res.res;
    DPRINTF1("DSKFRE: free=%u total=%u clusect=%u sectsz=%u res=%d\r\n", res.freeclu, res.totalclu, res.clusect, res.sectsize, res.res);

    break;
  }

  case 0x51: /* drvctrl */
  {
    DPRINTF1("DRVCTRL:\r\n");
    req->attr = 2;
    req->status = 0;
    break;
  }

  case 0x52: /* getdbp */
  {
    DPRINTF1("GETDPB:\r\n");
    uint8_t *p = (uint8_t *)req->addr;
    memset(p, 0, 16);
    *(uint16_t *)&p[0] = 512;   // 一部のアプリがエラーになるので仮のセクタ長を設定しておく
    p[2] = 1;
    req->status = 0;
    break;
  }

  case 0x53: /* diskred */
    DPRINTF1("DISKRED:\r\n");
    req->status = 0;
    break;

  case 0x54: /* diskwrt */
    DPRINTF1("DISKWRT:\r\n");
    req->status = 0;
    break;

  case 0x55: /* ioctl */
    DPRINTF1("IOCTL:\r\n");
    req->status = 0;
    break;

  case 0x56: /* abort */
    DPRINTF1("ABORT:\r\n");
    req->status = 0;
    break;

  case 0x57: /* mediacheck */
    DPRINTF1("MEDIACHECK:\r\n");
    req->status = 0;
    break;

  case 0x58: /* lock */
    DPRINTF1("LOCK:\r\n");
    req->status = 0;
    break;

  default:
    break;
  }

  req->errl = err & 0xff;
  req->errh = err >> 8;
}

//****************************************************************************
// Dummy program entry
//****************************************************************************

void _start(void)
{}
