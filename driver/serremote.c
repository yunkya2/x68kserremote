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
#include <stdarg.h>
#include <string.h>
#include <x68k/dos.h>
#include <x68k/iocs.h>
#include <setjmp.h>
#include "x68kremote.h"

//#define DEBUG

//****************************************************************************
// Global variables
//****************************************************************************

struct dos_req_header *reqheader;   // Human68kからのリクエストヘッダ
jmp_buf jenv;           //タイムアウト時のジャンプ先
bool recovery = false;  //エラー回復モードフラグ
int timeout = 500;      //コマンド受信タイムアウト(3sec)
int resmode = 0;        //登録モード (0:常に登録 / 1:起動時にサーバと通信できたら登録)

#ifdef DEBUG
int debuglevel = 0;
#endif

//****************************************************************************
// for debugging
//****************************************************************************

#define DPRINTF1(...)  DPRINTF(1, __VA_ARGS__)
#define DPRINTF2(...)  DPRINTF(2, __VA_ARGS__)
#define DPRINTF3(...)  DPRINTF(3, __VA_ARGS__)

#ifdef DEBUG
char heap[1024];                // temporary heap for debug print
void *_HSTA = heap;
void *_HEND = heap + 1024;
void *_PSP;

void DPRINTF(int level, char *fmt, ...)
{
  char buf[256];
  va_list ap;

  if (debuglevel < level)
    return;

  va_start(ap, fmt);
  vsiprintf(buf, fmt, ap);
  va_end(ap);
  _iocs_b_print(buf);
}

void DNAMEPRINT(void *n, bool full, char *head)
{
  struct dos_namestbuf *b = (struct dos_namestbuf *)n;

  DPRINTF1("%s%c:", head, b->drive + 'A');
  for (int i = 0; i < 65 && b->path[i]; i++) {
      DPRINTF1("%c", (uint8_t)b->path[i] == 9 ? '\\' : (uint8_t)b->path[i]);
  }
  if (full)
    DPRINTF1("%.8s%.10s.%.3s", b->name1, b->name2, b->ext);
}

#else
#define DPRINTF(...)
#define DNAMEPRINT(n, full, head)
#endif

//****************************************************************************
// Communication
//****************************************************************************

void out232c(uint8_t c)
{
  while (_iocs_osns232c() == 0)
    ;
  _iocs_out232c(c);
  DPRINTF3("%02X ", c);
}

void serout(void *buf, size_t len)
{
  uint8_t *p = buf;

  if (recovery) {
    // エラー状態からの回復
    // パケットサイズ以上の同期バイトを送ってサーバ側をコマンド受信待ち状態に戻す
    // その間に受信したデータはすべて捨てる
    DPRINTF1("error recovery\r\n");
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
  DPRINTF3("\r\n");
  for (int i = 0; i < len; i++) {
    if ((i % 16) == 0) DPRINTF3("%03X: ", i);
    out232c(*p++);
    if ((i % 16) == 15) DPRINTF3("\r\n");
  }
  DPRINTF3("\r\n");
  DPRINTF2("send %d bytes\r\n", len);
}

int inp232c(void)
{
  struct iocs_time tim;
  int sec;
  tim = _iocs_ontime();
  sec = tim.sec;

  while (_iocs_isns232c() == 0) {
    //データ受信がタイムアウトしたらエラー回復モードに移行
    tim = _iocs_ontime();
    if (((tim.sec - sec) % 8640000) > timeout)
      longjmp(jenv, -1);
  }
  int c = _iocs_inp232c() & 0xff;
  DPRINTF3("%02X ", c);
  return c;
}

void serin(void *buf, size_t len)
{
  uint8_t *p = buf;
  uint8_t c;
  size_t size;

  // 同期バイトをチェック:  ZZZ...ZZZX でデータ転送開始
  do {
      c = inp232c();
  } while (c != 'Z');
  do {
      c = inp232c();
  } while (c == 'Z');
  if (c != 'X') {
    longjmp(jenv, -1);
  }

  // データサイズを取得
  size = inp232c() << 8;
  size += inp232c();
  DPRINTF3("\r\n");
  if (size > len) {
    longjmp(jenv, -1);
  }

  // データを読み込み
  for (int i = 0; i < size; i++) {
    if ((i % 16) == 0) DPRINTF3("%03X: ", i);
    *p++ = inp232c();
    if ((i % 16) == 15) DPRINTF3("\r\n");
  }
  DPRINTF3("\r\n");
  DPRINTF2("recv %d bytes\r\n", size);
}

//****************************************************************************
// Utility routine
//****************************************************************************

ssize_t send_read(uint32_t fcb, char *buf, size_t len)
{
  struct cmd_read cmd;
  cmd.command = 0x4c; /* read */
  cmd.fcb = (uint32_t)fcb;
  cmd.len = len;
  serout(&cmd, sizeof(cmd));

  DPRINTF1(" read: addr=0x%08x len=%d\r\n", (uint32_t)buf, len);  

  struct res_read res;
  struct cmd_read_ack ack = { .ack = 0 };
  uint8_t *ptr = (uint8_t *)buf;
  ssize_t total = 0;
  while (1) {
    serin(&res, sizeof(res));
    DPRINTF1(" read: len=%d\r\n", res.len);
    if (res.len < 0)
      return res.len;
    if (res.len == 0)
      break;
    memcpy(ptr, res.data, res.len);
    ptr += res.len;
    total += res.len;
    serout(&ack, sizeof(ack));
  }
  DPRINTF1(" read: total=%d\r\n", total);
  return total;
}

ssize_t send_write(uint32_t fcb, char *buf, size_t len)
{
  struct cmd_write cmd;
  cmd.command = 0x4d; /* write */
  cmd.fcb = (uint32_t)fcb;
  cmd.len = len;
  serout(&cmd, sizeof(cmd));

  DPRINTF1(" write: addr=0x%08x len=%d\r\n", (uint32_t)buf, len);  

  struct cmd_write_body body;
  struct res_write res;
  uint8_t *ptr = (uint8_t *)buf;
  ssize_t total = 0;

  serin(&res, sizeof(res));
  while (len > 0 && res.len > 0) {
    size_t s = len > sizeof(body.data) ? sizeof(body.data) : len;
    memcpy(body.data, ptr, s);
    body.len = s;
    serout(&body, s + 2);
    serin(&res, sizeof(res));
    DPRINTF1(" write: len=%d -> %d\r\n", s, res.len);
     if (res.len < 0)
      break;
    ptr += res.len;
    len -= res.len;
    total += res.len;
  }
  if (res.len < 0)
    total = res.len;
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

static int my_atoi(char *p)
{
  int res = 0;
  while (*p >= '0' && *p <= '9') {
    res = res * 10 + *p++ - '0';
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
    if (resmode == 1) {     // 起動時にサーバが応答しなかった
      _dos_print("リモートドライブサービスが応答しないため組み込みません\r\n");
    }
    DPRINTF1("command timeout\r\n");
    req->errh = 0x10;
    req->errl = 0x02;
    req->status = -1;
    recovery = true;
    return;
  }

  DPRINTF2("----Command: 0x%02x\r\n", req->command);

  switch ((req->command) & 0x7f) {
  case 0x40: /* init */
  {
    _dos_print("\r\nX68000 Serial Remote Drive Driver (version ");
    _dos_print(GIT_REPO_VERSION);
    _dos_print(")\r\n");

    int baudrate = 38400;
    char *baudstr = "38400";
    char *p = (char *)req->status;
    p += strlen(p) + 1;
    while (*p != '\0') {
      if (*p == '/' || *p =='-') {
        p++;
        switch (*p | 0x20) {
#ifdef DEBUG
        case 'd':         // /D .. デバッグレベル増加
          debuglevel++;
          break;
#endif
        case 's':         // /s<speed> .. 通信速度設定
          p++;
          baudrate = my_atoi(p);
          baudstr = p;
          break;
        case 'r':         // /r<mode> .. 登録モード
          p++;
          resmode = my_atoi(p);
          break;
        case 't':         // /t<timeout> .. タイムアウト設定
          p++;
          timeout = my_atoi(p) * 100;
          if (timeout == 0)
            timeout = 500;
          break;
        }
      } else if (*p >= '0' && *p <= '9') {
        baudrate = my_atoi(p);
        baudstr = p;
      }
      p += strlen(p) + 1;
    }

    static const int bauddef[] = { 75, 150, 300, 600, 1200, 2400, 4800, 9600, 19200, 38400 };
    int bdset = -1;
    for (int i = 0; i < sizeof(bauddef) / sizeof(bauddef[0]); i++) {
      if (baudrate == bauddef[i]) {
        bdset = i;
        break;
      }
    }
    if (bdset < 0) {
      bdset = 9;
      baudstr = "38400";
    }

    if (resmode != 0) {     // サーバが応答するか確認する
      struct cmd_check cmd;
      cmd.command = req->command;
      serout(&cmd, sizeof(cmd));

      struct res_check res;
      serin(&res, sizeof(res));
      DPRINTF1("CHECK:\r\n");
    }
    resmode = 0;  // 応答を確認できたのでモードを戻す

    _dos_print("ドライブ");
    _dos_putchar('A' + *(char *)&req->fcb);
    _dos_print(":でRS-232Cに接続したリモートドライブが利用可能です (");
    _dos_print(baudstr);
    _dos_print("bps)\r\n");
    DPRINTF1("Debug level: %d\r\n", debuglevel);

    extern char _end;
    req->attr = 1; /* Number of units */
    req->addr = &_end;

    // stop 1 / nonparity / 8bit / nonxoff
    _iocs_set232c(0x4c00 | bdset);
    break;
  }

  case 0x41: /* chdir */
  {
    struct cmd_dirop cmd;
    cmd.command = req->command;
    memcpy(&cmd.path, req->addr, sizeof(struct dos_namestbuf));
    serout(&cmd, sizeof(cmd));

    struct res_dirop res;
    serin(&res, sizeof(res));
    req->status = res.res;
    DNAMEPRINT(req->addr, false, "CHDIR: ");
    DPRINTF1(" -> %d\r\n", res.res);
    break;
  }

  case 0x42: /* mkdir */
  {
    struct cmd_dirop cmd;
    cmd.command = req->command;
    memcpy(&cmd.path, req->addr, sizeof(struct dos_namestbuf));
    serout(&cmd, sizeof(cmd));

    struct res_dirop res;
    serin(&res, sizeof(res));
    req->status = res.res;
    DNAMEPRINT(req->addr, true, "MKDIR: ");
    DPRINTF1(" -> %d\r\n", res.res);
    break;
  }

  case 0x43: /* rmdir */
  {
    struct cmd_dirop cmd;
    cmd.command = req->command;
    memcpy(&cmd.path, req->addr, sizeof(struct dos_namestbuf));
    serout(&cmd, sizeof(cmd));

    struct res_dirop res;
    serin(&res, sizeof(res));
    req->status = res.res;
    DNAMEPRINT(req->addr, true, "RMDIR: ");
    DPRINTF1(" -> %d\r\n", res.res);
    break;
  }

  case 0x44: /* rename */
  {
    struct cmd_rename cmd;
    cmd.command = req->command;
    memcpy(&cmd.path_old, req->addr, sizeof(struct dos_namestbuf));
    memcpy(&cmd.path_new, (void *)req->status, sizeof(struct dos_namestbuf));
    serout(&cmd, sizeof(cmd));

    struct res_rename res;
    serin(&res, sizeof(res));
    DNAMEPRINT(req->addr, true, "RENAME: ");
    DNAMEPRINT((void *)req->status, true, " to ");
    DPRINTF1(" -> %d\r\n", res.res);
    req->status = res.res;
    break;
  }

  case 0x45: /* delete */
  {
    struct cmd_dirop cmd;
    cmd.command = req->command;
    memcpy(&cmd.path, req->addr, sizeof(struct dos_namestbuf));
    serout(&cmd, sizeof(cmd));

    struct res_dirop res;
    serin(&res, sizeof(res));
    req->status = res.res;
    DNAMEPRINT(req->addr, true, "DELETE: ");
    DPRINTF1(" -> %d\r\n", res.res);
    break;
  }

  case 0x46: /* chmod */
  {
    struct cmd_chmod cmd;
    cmd.command = req->command;
    cmd.attr = req->attr;
    memcpy(&cmd.path, req->addr, sizeof(struct dos_namestbuf));
    serout(&cmd, sizeof(cmd));

    struct res_chmod res;
    serin(&res, sizeof(res));
    req->status = res.res;
    DNAMEPRINT(req->addr, true, "CHMOD: ");
    DPRINTF1(" 0x%02x -> 0x%02x\r\n", cmd.attr, res.res);
    break;
  }

  case 0x47: /* files */
  {
    struct cmd_files cmd;
    cmd.command = req->command;
    cmd.attr = req->attr;
    cmd.filep = (uint32_t)req->status;
    memcpy(&cmd.path, req->addr, sizeof(struct dos_namestbuf));
    serout(&cmd, sizeof(cmd));

    struct res_files res;
    struct dos_filbuf *fb = (struct dos_filbuf *)req->status;
    serin(&res, sizeof(res));
    memcpy(&fb->atr, &res.file.atr, sizeof(res.file) - 1);
    req->status = res.res;
    DNAMEPRINT(req->addr, false, "FILES: ");
    DPRINTF1(" attr=0x%02x filep=0x%08x -> %d %s\r\n", cmd.attr, cmd.filep, res.res, res.file.name);
    break;
  }

  case 0x48: /* nfiles */
  {
    struct cmd_nfiles cmd;
    cmd.command = req->command;
    cmd.filep = (uint32_t)req->status;
    serout(&cmd, sizeof(cmd));

    struct res_nfiles res;
    struct dos_filbuf *fb = (struct dos_filbuf *)req->status;
    serin(&res, sizeof(res));
    memcpy(&fb->atr, &res.file.atr, sizeof(res.file) - 1);
    req->status = res.res;
    DPRINTF1("NFILES: filep=0x%08x -> %d %s\r\n", cmd.filep, res.res, res.file.name);
    break;
  }

  case 0x49: /* create */
  {
    struct cmd_create cmd;
    cmd.command = req->command;
    cmd.attr = req->attr;
    cmd.mode = req->status;
    cmd.fcb = (uint32_t)req->fcb;
    memcpy(&cmd.path, req->addr, sizeof(struct dos_namestbuf));
    serout(&cmd, sizeof(cmd));

    struct res_create res;
    serin(&res, sizeof(res));
    *(uint32_t *)(&((uint8_t *)req->fcb)[64]) = 0;
    req->status = res.res;
    DNAMEPRINT(req->addr, true, "CREATE: ");
    DPRINTF1(" fcb=0x%08x attr=0x%02x mode=%d -> %d\r\n", cmd.fcb, cmd.attr, cmd.mode, res.res);
    break;
  }

  case 0x4a: /* open */
  {
    struct cmd_open cmd;
    cmd.command = req->command;
    cmd.mode = ((uint8_t *)req->fcb)[14];
    cmd.fcb = (uint32_t)req->fcb;
    memcpy(&cmd.path, req->addr, sizeof(struct dos_namestbuf));
    serout(&cmd, sizeof(cmd));

    struct res_open res;
    serin(&res, sizeof(res));
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
    cmd.command = req->command;
    cmd.fcb = (uint32_t)req->fcb;
    serout(&cmd, sizeof(cmd));

    struct res_close res;
    serin(&res, sizeof(res));
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
    serout(&cmd, sizeof(cmd));

    struct res_seek res;
    serin(&res, sizeof(res));
    *(uint32_t *)(&((uint8_t *)req->fcb)[6]) = res.res;
    req->status = res.res;
errout_seek:
    DPRINTF1("SEEK: fcb=0x%x offset=%d whence=%d -> %d\r\n", cmd.fcb, cmd.offset, cmd.whence, res.res);
    break;
  }

  case 0x4f: /* filedate */
  {
    struct cmd_filedate cmd;
    cmd.command = req->command;
    cmd.fcb = (uint32_t)req->fcb;
    cmd.time = req->status & 0xffff;
    cmd.date = req->status >> 16;
    serout(&cmd, sizeof(cmd));

    struct res_filedate res;
    serin(&res, sizeof(res));
    req->status = res.time + (res.date << 16);
    DPRINTF1("FILEDATE: fcb=0x%08x 0x%04x 0x%04x -> 0x%04x 0x%04x\r\n", cmd.fcb, cmd.date, cmd.time, res.date, res.time);
    break;
  }

  case 0x50: /* dskfre */
  {
    struct cmd_dskfre cmd;
    cmd.command = req->command;
    serout(&cmd, sizeof(cmd));

    struct res_dskfre res;
    serin(&res, sizeof(res));
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
