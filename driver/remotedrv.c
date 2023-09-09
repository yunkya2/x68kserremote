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
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <x68k/dos.h>

#include <config.h>
#include <x68kremote.h>
#include "remotedrv.h"

//****************************************************************************
// Global variables
//****************************************************************************

struct dos_req_header *reqheader;   // Human68kからのリクエストヘッダ
jmp_buf jenv;                       //タイムアウト時のジャンプ先

static union {
  struct cmd_check    cmd_check;
  struct res_check    res_check;
  struct cmd_dirop    cmd_dirop;
  struct res_dirop    res_dirop;
  struct cmd_rename   cmd_rename;
  struct res_rename   res_rename;
  struct cmd_chmod    cmd_chmod;
  struct res_chmod    res_chmod;
  struct cmd_files    cmd_files;
  struct res_files    res_files;
  struct cmd_nfiles   cmd_nfiles;
  struct res_nfiles   res_nfiles;
  struct cmd_create   cmd_create;
  struct res_create   res_create;
  struct cmd_open     cmd_open;
  struct res_open     res_open;
  struct cmd_close    cmd_close;
  struct res_close    res_close;
  struct cmd_read     cmd_read;
  struct res_read     res_read;
  struct cmd_write    cmd_write;
  struct res_write    res_write;
  struct cmd_seek     cmd_seek;
  struct res_seek     res_seek;
  struct cmd_filedate cmd_filedate;
  struct res_filedate res_filedate;
  struct cmd_dskfre   cmd_dskfre;
  struct res_dskfre   res_dskfre;
} b;

//****************************************************************************
// Utility routine
//****************************************************************************

ssize_t send_read(uint32_t fcb, char *buf, size_t len)
{
  struct cmd_read *cmd = &b.cmd_read;
  struct res_read *res = &b.res_read;
  ssize_t total = 0;

  while (len > 0) {
    size_t size = len > sizeof(res->data) ? sizeof(res->data) : len;
    cmd->command = 0x4c; /* read */
    cmd->fcb = (uint32_t)fcb;
    cmd->len = size;

    com_cmdres(cmd, sizeof(*cmd), res, sizeof(*res));

    DPRINTF1(" read: addr=0x%08x len=%d size=%d\r\n", (uint32_t)buf, len, res->len);
    if (res->len < 0)
      return res->len;
    if (res->len == 0)
      break;

    memcpy(buf, res->data, res->len);
    buf += res->len;
    total += res->len;
    len -= res->len;
  }
  DPRINTF1(" read: total=%d\r\n", total);
  return total;
}

ssize_t send_write(uint32_t fcb, char *buf, size_t len)
{
  struct cmd_write *cmd = &b.cmd_write;
  struct res_write *res = &b.res_write;
  ssize_t total = 0;

  do {
    size_t s = len > sizeof(cmd->data) ? sizeof(cmd->data) : len;
    cmd->command = 0x4d; /* write */
    cmd->fcb = (uint32_t)fcb;
    cmd->len = s;
    memcpy(cmd->data, buf, s);

    com_cmdres(cmd, offsetof(struct cmd_write, data) + s, res, sizeof(*res));

    DPRINTF1(" write: addr=0x%08x len=%d size=%d\r\n", (uint32_t)buf, len, res->len);
    if (res->len < 0)
      return res->len;
    buf += res->len;
    len -= res->len;
    total += res->len;
  } while (len > 0);
  DPRINTF1(" write: total=%d\r\n", total);
  return total;
}

struct dcache {
  uint32_t fcb;
  uint32_t offset;
  int16_t len;
  bool dirty;
  uint8_t cache[CONFIG_DATASIZE];
} dcache[CONFIG_NDCACHE];

int dcache_flash(uint32_t fcb, bool clean)
{
  int res = 0;
  for (int i = 0; i < CONFIG_NDCACHE; i++) {
    if (dcache[i].fcb == fcb) {
      if (dcache[i].dirty) {
        if (send_write(dcache[i].fcb, dcache[i].cache, dcache[i].len) < 0)
          res = -1;
        dcache[i].dirty = false;
      }
      if (clean)
        dcache[i].fcb = 0;
    }
  }
  return res;
}

#if CONFIG_NFILEINFO > 1
struct fcache {
  uint32_t filep;
  int cnt;
  struct res_files files;
} fcache[CONFIG_NFCACHE];

struct fcache *fcache_alloc(uint32_t filep, bool new)
{
  for (int i = 0; i < CONFIG_NFCACHE; i++) {
    if (fcache[i].filep == filep) {
      return &fcache[i];
    }
  }
  if (!new)
    return NULL;
  for (int i = 0; i < CONFIG_NFCACHE; i++) {
    if (fcache[i].filep == 0) {
      return &fcache[i];
    }
  }
  return NULL;
}
#endif

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
    struct cmd_dirop *cmd = &b.cmd_dirop;
    struct res_dirop *res = &b.res_dirop;
    cmd->command = req->command;
    memcpy(&cmd->path, req->addr, sizeof(struct dos_namestbuf));
    com_cmdres(cmd, sizeof(*cmd), res, sizeof(*res));
    DNAMEPRINT(req->addr, false, "CHDIR: ");
    DPRINTF1(" -> %d\r\n", res->res);
    req->status = res->res;
    break;
  }

  case 0x42: /* mkdir */
  {
    struct cmd_dirop *cmd = &b.cmd_dirop;
    struct res_dirop *res = &b.res_dirop;
    cmd->command = req->command;
    memcpy(&cmd->path, req->addr, sizeof(struct dos_namestbuf));
    com_cmdres(cmd, sizeof(*cmd), res, sizeof(*res));
    DNAMEPRINT(req->addr, true, "MKDIR: ");
    DPRINTF1(" -> %d\r\n", res->res);
    req->status = res->res;
    break;
  }

  case 0x43: /* rmdir */
  {
    struct cmd_dirop *cmd = &b.cmd_dirop;
    struct res_dirop *res = &b.res_dirop;
    cmd->command = req->command;
    memcpy(&cmd->path, req->addr, sizeof(struct dos_namestbuf));
    com_cmdres(cmd, sizeof(*cmd), res, sizeof(*res));
    DNAMEPRINT(req->addr, true, "RMDIR: ");
    DPRINTF1(" -> %d\r\n", res->res);
    req->status = res->res;
    break;
  }

  case 0x44: /* rename */
  {
    struct cmd_rename *cmd = &b.cmd_rename;
    struct res_rename *res = &b.res_rename;
    cmd->command = req->command;
    memcpy(&cmd->path_old, req->addr, sizeof(struct dos_namestbuf));
    memcpy(&cmd->path_new, (void *)req->status, sizeof(struct dos_namestbuf));
    com_cmdres(cmd, sizeof(*cmd), res, sizeof(*res));
    DNAMEPRINT(req->addr, true, "RENAME: ");
    DNAMEPRINT((void *)req->status, true, " to ");
    DPRINTF1(" -> %d\r\n", res->res);
    req->status = res->res;
    break;
  }

  case 0x45: /* delete */
  {
    struct cmd_dirop *cmd = &b.cmd_dirop;
    struct res_dirop *res = &b.res_dirop;
    cmd->command = req->command;
    memcpy(&cmd->path, req->addr, sizeof(struct dos_namestbuf));
    com_cmdres(cmd, sizeof(*cmd), res, sizeof(*res));
    DNAMEPRINT(req->addr, true, "DELETE: ");
    DPRINTF1(" -> %d\r\n", res->res);
    req->status = res->res;
    break;
  }

  case 0x46: /* chmod */
  {
    struct cmd_chmod *cmd = &b.cmd_chmod;
    struct res_chmod *res = &b.res_chmod;
    cmd->command = req->command;
    cmd->attr = req->attr;
    memcpy(&cmd->path, req->addr, sizeof(struct dos_namestbuf));
    com_cmdres(cmd, sizeof(*cmd), res, sizeof(*res));
    DNAMEPRINT(req->addr, true, "CHMOD: ");
    DPRINTF1(" 0x%02x -> 0x%02x\r\n", req->attr, res->res);
    req->status = res->res;
    break;
  }

  case 0x47: /* files */
  {
    struct cmd_files *cmd = &b.cmd_files;
    struct res_files *res = &b.res_files;
    cmd->command = req->command;
    cmd->attr = req->attr;
    cmd->filep = req->status;
    memcpy(&cmd->path, req->addr, sizeof(struct dos_namestbuf));
#if CONFIG_NFILEINFO > 1
    struct fcache *fc = fcache_alloc(cmd->filep, true);
    if (fc) {
      fc->filep = cmd->filep;
      fc->cnt = 0;
      cmd->num = CONFIG_NFILEINFO;
    } else {
      cmd->num = 1;
    }
#endif

    com_cmdres(cmd, sizeof(*cmd), res, sizeof(*res));

    struct dos_filbuf *fb = (struct dos_filbuf *)req->status;
#if CONFIG_NFILEINFO > 1
    if (fc) {
      if (res->res == 0 && res->num > 1) {
        fc->files = *res;
        fc->cnt = 1;
      } else {
        fc->filep = 0;
      }
    }
#endif
    if (res->res == 0)
      memcpy(&fb->atr, &res->file[0].atr, sizeof(res->file[0]) - 1);
    DNAMEPRINT(req->addr, false, "FILES: ");
    DPRINTF1(" attr=0x%02x filep=0x%08x -> %d %s\r\n", req->attr, req->status, res->res, res->file[0].name);
    req->status = res->res;
    break;
  }

  case 0x48: /* nfiles */
  {
    struct cmd_nfiles *cmd = &b.cmd_nfiles;
    struct res_nfiles *res = &b.res_nfiles;
    cmd->command = req->command;
    cmd->filep = req->status;

    struct dos_filbuf *fb = (struct dos_filbuf *)req->status;
#if CONFIG_NFILEINFO > 1
    struct fcache *fc;
    if (fc = fcache_alloc(cmd->filep, false)) {
      memcpy(&fb->atr, &fc->files.file[fc->cnt].atr, sizeof(fc->files.file[0]) - 1);
      fc->cnt++;
      res->res = 0;
      if (fc->cnt >= fc->files.num) {
        fc->filep = 0;
      }
      goto out_nfiles;
    }
    if (fc = fcache_alloc(cmd->filep, true)) {
      fc->filep = cmd->filep;
      fc->cnt = 0;
      cmd->num = CONFIG_NFILEINFO;
    } else {
      cmd->num = 1;
    }
#endif

    com_cmdres(cmd, sizeof(*cmd), res, sizeof(*res));

#if CONFIG_NFILEINFO > 1
    if (fc) {
      if (res->res == 0 && res->num > 1) {
        fc->files = *(struct res_files *)res;
        fc->cnt = 1;
      } else {
        fc->filep = 0;
      }
    }
#endif
    if (res->res == 0)
      memcpy(&fb->atr, &res->file[0].atr, sizeof(res->file[0]) - 1);
out_nfiles:
    DPRINTF1("NFILES: filep=0x%08x -> %d %s\r\n", req->status, res->res, fb->name);
    req->status = res->res;
    break;
  }

  case 0x49: /* create */
  {
    struct cmd_create *cmd = &b.cmd_create;
    struct res_create *res = &b.res_create;
    cmd->command = req->command;
    cmd->attr = req->attr;
    cmd->mode = req->status;
    cmd->fcb = (uint32_t)req->fcb;
    memcpy(&cmd->path, req->addr, sizeof(struct dos_namestbuf));
    com_cmdres(cmd, sizeof(*cmd), res, sizeof(*res));
    *(uint32_t *)(&((uint8_t *)req->fcb)[64]) = 0;
    DNAMEPRINT(req->addr, true, "CREATE: ");
    DPRINTF1(" fcb=0x%08x attr=0x%02x mode=%d -> %d\r\n", (uint32_t)req->fcb, req->attr, req->status, res->res);
    req->status = res->res;
    break;
  }

  case 0x4a: /* open */
  {
    struct cmd_open *cmd = &b.cmd_open;
    struct res_open *res = &b.res_open;
    int mode = ((uint8_t *)req->fcb)[14];
    cmd->command = req->command;
    cmd->mode = mode;
    cmd->fcb = (uint32_t)req->fcb;
    memcpy(&cmd->path, req->addr, sizeof(struct dos_namestbuf));
    com_cmdres(cmd, sizeof(*cmd), res, sizeof(*res));
    *(uint32_t *)(&((uint8_t *)req->fcb)[64]) = res->size;
    DNAMEPRINT(req->addr, true, "OPEN: ");
    DPRINTF1(" fcb=0x%08x mode=%d -> %d %d\r\n", (uint32_t)req->fcb, mode, res->res, res->size);
    req->status = res->res;
    break;
  }

  case 0x4b: /* close */
  {
    dcache_flash((uint32_t)req->fcb, true);

    struct cmd_close *cmd = &b.cmd_close;
    struct res_close *res = &b.res_close;
    cmd->command = req->command;
    cmd->fcb = (uint32_t)req->fcb;
    com_cmdres(cmd, sizeof(*cmd), res, sizeof(*res));
    DPRINTF1("CLOSE: fcb=0x%08x\r\n", (uint32_t)req->fcb);
    req->status = res->res;
    break;
  }

  case 0x4c: /* read */
  {
    dcache_flash((uint32_t)req->fcb, false);

    uint32_t *pp = (uint32_t *)(&((uint8_t *)req->fcb)[6]);

    char *buf = (char *)req->addr;
    size_t len = (size_t)req->status;
    ssize_t size = 0;

    for (int i = 0; i < CONFIG_NDCACHE; i++) {
      if (dcache[i].fcb == 0 || dcache[i].fcb == (uint32_t)req->fcb) {
        // キャッシュが未使用または自分のデータが入っている場合
        do {
          if (dcache[i].fcb == (uint32_t)req->fcb &&
              *pp >= dcache[i].offset && *pp < dcache[i].offset + dcache[i].len) {
            // これから読むデータがキャッシュに入っている場合、キャッシュから読めるだけ読む
            size_t clen = dcache[i].offset + dcache[i].len - *pp;
            clen = clen < len ? clen : len;

            memcpy(buf, dcache[i].cache + (*pp - dcache[i].offset), clen);
            buf += clen;
            len -= clen;
            size += clen;
            *pp += clen;    // FCBのファイルポインタを進める
          }
          if (len == 0 || len >= sizeof(dcache[i].cache))
            break;
          // キャッシュサイズ未満の読み込みならキャッシュを充填
          dcache_flash((uint32_t)req->fcb, true);
          dcache[i].len = send_read((uint32_t)req->fcb, dcache[i].cache, sizeof(dcache[i].cache));
          if (dcache[i].len < 0) {
            size = -1;
            goto errout_read;
          }
          dcache[i].fcb = (uint32_t)req->fcb;
          dcache[i].offset = *pp;
          dcache[i].dirty = false;
        } while (dcache[i].len > 0);
        break;
      }
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

    if (len > 0 && len < sizeof(dcache[0].cache)) {  // 書き込みサイズがキャッシュサイズ未満
      for (int i = 0; i < CONFIG_NDCACHE; i++) {
        if (dcache[i].fcb == (uint32_t)req->fcb) {     //キャッシュに自分のデータが入っている
          if ((*pp = dcache[i].offset + dcache[i].len) &&
              ((*pp + len) <= (dcache[i].offset + sizeof(dcache[i].cache)))) {
            // 書き込みデータがキャッシュに収まる場合はキャッシュに書く
            memcpy(dcache[i].cache + dcache[i].len, (char *)req->addr, len);
            dcache[i].len += len;
            goto okout_write;
          } else {    //キャッシュに収まらないのでフラッシュ
            dcache_flash((uint32_t)req->fcb, true);
          }
        }
        if (dcache[i].fcb == 0) {    //キャッシュが未使用
          // 書き込みデータをキャッシュに書く
          dcache[i].fcb = (uint32_t)req->fcb;
          dcache[i].offset = *pp;
          memcpy(dcache[i].cache, (char *)req->addr, len);
          dcache[i].len = len;
          dcache[i].dirty = true;
          goto okout_write;
        }
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

    struct cmd_seek *cmd = &b.cmd_seek;
    struct res_seek *res = &b.res_seek;
    cmd->command = req->command;
    cmd->fcb = (uint32_t)req->fcb;
    cmd->whence = req->attr;
    cmd->offset = req->status;

    uint32_t pos = *(uint32_t *)(&((uint8_t *)req->fcb)[6]);
    uint32_t size = *(uint32_t *)(&((uint8_t *)req->fcb)[64]);
    pos = (cmd->whence == 0 ? 0 : (cmd->whence == 1 ? pos : size)) + cmd->offset;
    if (pos > size) {   // ファイル末尾を越えてseekしようとした
      res->res = _DOSE_CANTSEEK;
      goto errout_seek;
    }

    com_cmdres(cmd, sizeof(*cmd), res, sizeof(*res));
    *(uint32_t *)(&((uint8_t *)req->fcb)[6]) = res->res;
errout_seek:
    DPRINTF1("SEEK: fcb=0x%x offset=%d whence=%d -> %d\r\n", (uint32_t)req->fcb, req->status, req->attr, res->res);
    req->status = res->res;
    break;
  }

  case 0x4f: /* filedate */
  {
    struct cmd_filedate *cmd = &b.cmd_filedate;
    struct res_filedate *res = &b.res_filedate;
    cmd->command = req->command;
    cmd->fcb = (uint32_t)req->fcb;
    cmd->time = req->status & 0xffff;
    cmd->date = req->status >> 16;
    com_cmdres(cmd, sizeof(*cmd), res, sizeof(*res));
    DPRINTF1("FILEDATE: fcb=0x%08x 0x%04x 0x%04x -> 0x%04x 0x%04x\r\n", (uint32_t)req->fcb, req->status >> 16, req->status & 0xffff, res->date, res->time);
    req->status = res->time + (res->date << 16);
    break;
  }

  case 0x50: /* dskfre */
  {
    struct cmd_dskfre *cmd = &b.cmd_dskfre;
    struct res_dskfre *res = &b.res_dskfre;
    cmd->command = req->command;
    com_cmdres(cmd, sizeof(*cmd), res, sizeof(*res));

    uint16_t *p = (uint16_t *)req->addr;
    p[0] = res->freeclu;
    p[1] = res->totalclu;
    p[2] = res->clusect;
    p[3] = res->sectsize;
    DPRINTF1("DSKFRE: free=%u total=%u clusect=%u sectsz=%u res=%d\r\n", res->freeclu, res->totalclu, res->clusect, res->sectsize, res->res);
    req->status = res->res;

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
