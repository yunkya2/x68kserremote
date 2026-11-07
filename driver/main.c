/*
 * Copyright (c) 2026 Yuichi Nakamura (@yunkya2)
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
#include <setjmp.h>
#include <x68k/dos.h>
#include <x68k/iocs.h>
#ifndef CONFIG_BOOTDRIVER
#include <x68k/tsr.h>
#endif

#include <config.h>
#include <x68kremote.h>
#include "serremote.h"
#include "remotedrv.h"

#ifndef CONFIG_BOOTDRIVER
TSR_NO_ENVIRON();
#endif

  //****************************************************************************
// Global variables
//****************************************************************************

#define BANNER  "X68000 Serial Remote Drive Driver (version " GIT_REPO_VERSION ")\r\n"

static int baudrate = 38400;    //通信速度
static int units = 1;           //ユニット数

//****************************************************************************
// Utility routine
//****************************************************************************

#ifndef CONFIG_BOOTDRIVER
static int my_atoi(char *p)
{
  int res = 0;
  while (*p >= '0' && *p <= '9') {
    res = res * 10 + *p++ - '0';
  }
  return res;
}

// 起動パラメータを解析する (argv[0]は読み飛ばす)
//  戻り値: 0 通常 / 1 常駐解除要求 / -1 エラー
static int parse_args(int argc, char **argv)
{
  int result = 0;

  for (int i = 1; i < argc; i++) {
    char *p = argv[i];
    if (*p == '/' || *p == '-') {
      p++;
      switch (*p | 0x20) {
#ifdef DEBUG
      case 'd':         // /D .. デバッグレベル増加
        debuglevel++;
        break;
#endif
      case 's':         // /s<speed> .. 通信速度設定
        baudrate = my_atoi(p + 1);
        break;
      case 'm':         // /m<mode> .. 登録モード
        resmode = my_atoi(p + 1);
        break;
      case 't':         // /t<timeout> .. タイムアウト設定
        timeout = my_atoi(p + 1) * 100;
        if (timeout == 0)
          timeout = 500;
        break;
      case 'u':         // /u<units> .. ユニット数設定
        units = my_atoi(p + 1);
        if (units < 1 || units > 8)
          units = 1;
        break;
      case 'r':         // /r .. 常駐解除
        result = 1;
        break;
      default:
        return -1;
      }
    } else if (*p >= '0' && *p <= '9') {
      baudrate = my_atoi(p);
    }
  }
  return result;
}

#endif

// RS-232Cの通信速度を設定する
//  戻り値: 設定した通信速度の文字列
static const char *setup_rs232c(void)
{
  static const int bauddef[] = { 75, 150, 300, 600, 1200, 2400, 4800, 9600, 19200, 38400 };
  static const char * const baudstr[] = {
    "75", "150", "300", "600", "1200", "2400", "4800", "9600", "19200", "38400"
  };
  int bdset = 9;
  for (int i = 0; i < sizeof(bauddef) / sizeof(bauddef[0]); i++) {
    if (baudrate == bauddef[i]) {
      bdset = i;
      break;
    }
  }

  // stop 1 / nonparity / 8bit / nonxoff
  _iocs_set232c(0x4c00 | bdset);
  return baudstr[bdset];
}

// サーバが応答するか確認する (応答しない場合はcom_timeout経由でlongjmpする)
static void check_server(void)
{
  if (resmode != 0) {
    struct cmd_init cmd;
    struct res_init res;
    cmd.command = 0x00; /* init */
    com_cmdres(&cmd, sizeof(cmd), &res, sizeof(res));
    DPRINTF1("CHECK:\r\n");
  }
  resmode = 0;  // 応答を確認できたのでモードを戻す
}

#ifndef CONFIG_BOOTDRIVER
static void print_connected(int first, int last, const char *baudstr)
{
  _dos_print("ドライブ");
  _dos_putchar('A' + first);
  if (last != first) {
    _dos_print(":-");
    _dos_putchar('A' + last);
  }
  _dos_print(":でRS-232Cに接続したリモートドライブが利用可能です (");
  _dos_print(baudstr);
  _dos_print("bps)\r\n");
}

// 常駐中のデバイスに割り当てられた論理ドライブを表示する
static void print_resident_drives(tsr_device_t dev)
{
  uint32_t drives = 0;
  int ssp = tsr_super(0);
  struct dos_curdir *curdirs = *(struct dos_curdir **)0x1c38;
  uint8_t *drvxtbl = (uint8_t *)0x1c7e;

  for (int i = 0; i < 26; i++) {
    struct dos_curdir *curdir = &curdirs[drvxtbl[i]];
    if (curdir->type == 0x40 && curdir->dpb != NULL &&
        curdir->dpb->devheader == &dev->header)
      drives |= (uint32_t)1 << i;
  }
  tsr_super(ssp);

  _dos_print("リモートドライブは既に組み込まれています");
  if (drives != 0) {
    _dos_print(" (ドライブ");
    bool first = true;
    for (int i = 0; i < 26; i++) {
      if (drives & ((uint32_t)1 << i)) {
        if (!first)
          _dos_print(", ");
        _dos_putchar('A' + i);
        _dos_putchar(':');
        first = false;
      }
    }
    _dos_putchar(')');
  }
  _dos_print("\r\n");
}

#endif

#ifdef CONFIG_BOOTDRIVER

// SCSI BOOT ROMからの起動時の初期化処理
int com_init(struct dos_req_header *req)
{
  extern char _end;
  extern uint8_t bootpart;

  setup_rs232c();
  check_server();

  // SCSI BOOT ROMから渡される「何番目のパーティションから起動するか」を
  // Human68kのドライバ初期化処理に返す (これで起動ドライブが決まる)
  *(char *)&req->fcb = bootpart;
  req->addr = &_end;
  return units;
}

#else

// CONFIG.SYSからの組み込み時の初期化処理 (デバイス初期化コマンドから呼ばれる)
int com_init(struct dos_req_header *req)
{
  char *argv[16];
  int argc = tsr_parseinitparam((void *)req->status, argv, 16);

  _dos_print("\r\n" BANNER);

  parse_args(argc, argv);
  const char *baudstr = setup_rs232c();
  check_server();
  print_connected(*(char *)&req->fcb, *(char *)&req->fcb + units - 1, baudstr);
  DPRINTF1("Debug level: %d\r\n", debuglevel);

  req->addr = tsr_getresidentend();
  return units;
}

#endif

#ifndef CONFIG_BOOTDRIVER
//****************************************************************************
// Command line entry
//****************************************************************************

int main(int argc, char **argv)
{
  tsr_device_t dev = tsr_finddev(CONFIG_DEVNAME);
  int result = parse_args(argc, argv);

  _dos_print(BANNER);

  if (result < 0) {           // 引数解析エラー
    _dos_print("Usage: serremote.x [-r][-s<baud>][-m<mode>][-t<timeout>][-u<units>]\r\n");
    return 1;
  } else if (result > 0) {    // 常駐解除要求
    if (dev == NULL) {
      _dos_print("リモートドライブは組み込まれていません\r\n");
      return 1;
    }
    if (!tsr_isfreeable(dev) ||
        tsr_detachdrive(dev) < 0 ||
        tsr_freedev(dev) < 0) {
      _dos_print("リモートドライブを解除できません\r\n");
      return 1;
    }
    _dos_print("リモートドライブを解除しました\r\n");
    return 0;
  }

  if (dev != NULL) {
    print_resident_drives(dev);
    return 1;
  }

  const char *baudstr = setup_rs232c();
  if (setjmp(jenv)) {
    com_timeout(&(struct dos_req_header){ 0 });
    return 1;
  }
  check_server();

  struct dos_dpb *dpb_table[8];
  for (int i = 0; i < units; i++) {
    dpbs[i].sectbytes = 0;
    dpb_table[i] = (struct dos_dpb *)&dpbs[i];
  }
  if (tsr_attachdrive(-1, units, dpb_table, TSR_THISDEV) < 0) {
    _dos_print("リモートドライブを割り当てられません\r\n");
    return 1;
  }
  print_connected(tsr_getlogicaldrive(dpbs[0].drive),
                  tsr_getlogicaldrive(dpbs[units - 1].drive), baudstr);

  tsr_keepdev(NULL, 0);
}
#endif
