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

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <time.h>
#ifndef WINNT
#include <iconv.h>
#include <sys/statfs.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <endian.h>
#else
#include <windows.h>
#endif
#include "x68kremote.h"

//****************************************************************************
// Global type and variables
//****************************************************************************

typedef char hostpath_t[256];

char *rootpath = ".";
int debuglevel = 0;

//****************************************************************************
// for MinGW
//****************************************************************************

#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifdef WINNT
static inline uint16_t bswap16 (uint16_t x)
{
  return (x >> 8) | (x << 8);
}
static inline uint32_t bswap32 (uint32_t x)
{
  return (bswap16(x & 0xffff) << 16) | (bswap16(x >> 16));
}
#define htobe16(x) bswap16(x)
#define htobe32(x) bswap32(x)
#define be16toh(x) bswap16(x)
#define be32toh(x) bswap32(x)
#endif

//****************************************************************************
// for debugging
//****************************************************************************

void DPRINTF(int level, char *fmt, ...)
{
  if (debuglevel >= level) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
  }
}

#define DPRINTF1(...)  DPRINTF(1, __VA_ARGS__)
#define DPRINTF2(...)  DPRINTF(2, __VA_ARGS__)
#define DPRINTF3(...)  DPRINTF(3, __VA_ARGS__)

//****************************************************************************
// Communication
//****************************************************************************

int serout(int fd, void *buf, size_t len)
{
  uint8_t lenbuf[2];
  lenbuf[0] = len >> 8;
  lenbuf[1] = len & 0xff;

  if (write(fd, "ZZZX", 4) < 0 ||
      write(fd, lenbuf, 2) < 0 ||
      write(fd, buf, len) < 0) {
    return -1;
  }
  DPRINTF3("%02X %02X %02X %02X ", 'Z', 'Z', 'Z', 'X');
  DPRINTF3("%02X %02X\n", lenbuf[0], lenbuf[1]);
  for (int i = 0; i < len; i++) {
    if ((i % 16) == 0) DPRINTF3("%03X: ", i);
    DPRINTF3("%02X ", ((uint8_t *)buf)[i]);
    if ((i % 16) == 15) DPRINTF3("\n");
  }
  DPRINTF3("\n");
  DPRINTF2("send %d bytes\n", len);
}

int serin(int fd, void *buf, size_t len)
{
  uint8_t c;
  int l;

  // 同期バイトをチェック:  ZZZ...ZZZX でデータ転送開始
  do { 
    l = read(fd, &c, 1);
    DPRINTF3("%02X %d ", c, l);
  } while (l < 1 || c != 'Z');
  do {
    l = read(fd, &c, 1);
    DPRINTF3("%02X ", c);
  } while (l < 1 || c == 'Z');
  if (c != 'X') {
    return -1;
  }

  // データサイズを取得
  size_t size;
  l = read(fd, &c, 1);
  DPRINTF3("%02X ", c);
  size = c << 8;
  l = read(fd, &c, 1);
  DPRINTF3("%02X ", c);
  size += c;
  if (size > len) {
    return -1;
  }
  DPRINTF3("\n");

  // データを読み込み
  uint8_t *p = buf;
  for (size_t s = size; s > 0; ) {
    l = read(fd, p, s);
    p += l;
    s -= l;
  }

  for (int i = 0; i < size; i++) {
    if ((i % 16) == 0) DPRINTF3("%03X: ", i);
    DPRINTF3("%02X ", ((uint8_t *)buf)[i]);
    if ((i % 16) == 15) DPRINTF3("\n");
  }
  DPRINTF3("\n");
  DPRINTF2("recv %d bytes\n", size);
  return 0;
}

int seropen(char *port, int baudrate)
{
  int fd = open(port, O_RDWR|O_BINARY);
  if (fd < 0) {
      return -1;
  }

#ifndef WINNT
  /* POSIX API */
  static const int bauddef[] = { B38400, B19200, B9600, B4800, B2400, B1200, B600, B300, B150, B75 };
  int bbrate = 38400;
  for (int i = 0; i < sizeof(bauddef) / sizeof(bauddef[0]); i++) {
    if (baudrate == bbrate) {
      bbrate = bauddef[i];
      break;
    }
    bbrate /= 2;
  }

  struct termios tio;
  if (tcgetattr(fd, &tio) < 0 ||
      cfsetispeed(&tio, bbrate) < 0 ||
      cfsetospeed(&tio, bbrate) < 0) {
    return -1;
  }

  cfmakeraw(&tio);
  tio.c_iflag &= ~IXOFF;
  tio.c_iflag |= IGNPAR;
  tio.c_oflag &= ~(ONLCR | OCRNL);
  tio.c_cflag &= ~CSTOPB;
  tio.c_cflag |= CREAD | CS8 | CLOCAL;
  tio.c_cc[VMIN] = 1;

  if (tcsetattr(fd, TCSANOW, &tio) < 0) {
    return -1;
  }
#else
  /* Windows API */
  HANDLE hComm = (HANDLE)_get_osfhandle(fd);

  DCB dcb;
  if (!GetCommState(hComm, &dcb)) {
    return -1;
  }
  dcb.BaudRate = baudrate;
  dcb.ByteSize = 8;
  dcb.StopBits = ONESTOPBIT;
  dcb.Parity = NOPARITY;
  dcb.fBinary = TRUE;
  dcb.EofChar = 0;
  dcb.fNull = FALSE;
  dcb.fParity = FALSE;
  dcb.fErrorChar = FALSE;
  dcb.ErrorChar = 0;
  dcb.fTXContinueOnXoff = TRUE;
  dcb.fOutX = FALSE;
  dcb.fInX = FALSE;
  dcb.fOutxDsrFlow = FALSE;
  dcb.fOutxCtsFlow = FALSE;
  dcb.fRtsControl = 0;
  dcb.fDtrControl = 0;
  if (!SetCommState(hComm, &dcb)) {
    return -1;
  }
  COMMTIMEOUTS timeout = {
    MAXDWORD, MAXDWORD, MAXDWORD - 1, 0, 0
  };
  if (!SetCommTimeouts(hComm, &timeout)) {
    return -1;
  }
#endif

  return fd;
}

//****************************************************************************
// Utility functions
//****************************************************************************

// struct statのファイルモードを変換する
static inline int conv_filemode(struct stat *st)
{
  int atr = S_ISREG(st->st_mode) ? 0x20 : 0;  // regular file
  atr |= S_ISDIR(st->st_mode) ? 0x10 : 0;     // directory
  atr |= (st->st_mode & S_IWUSR) ? 0 : 0x01;  // read only
  return atr;
}

// struct statのファイル情報を変換する
static void conv_statinfo(struct stat *st, void *v)
{
  struct dos_filesinfo *f = (struct dos_filesinfo *)v;

  f->atr = conv_filemode(st);
  f->filelen = htobe32(st->st_size);
  struct tm *tm = localtime(&st->st_mtime);
  f->time = htobe16(tm->tm_hour << 11 | tm->tm_min << 5 | tm->tm_sec >> 1);
  f->date = htobe16((tm->tm_year - 80) << 9 | (tm->tm_mon + 1) << 5 | tm->tm_mday);
}

// namestsのパスをホストのパスに変換する
// (derived from HFS.java by Makoto Kamada)
static int conv_namebuf(dos_namebuf *ns, bool full, hostpath_t *path)
{
  uint8_t bb[88];   // SJISでのパス名
  int k = 0;

  // パスの区切りを 0x09 -> '/' に変更
  for (int i = 0; i < 65; ) {
    for (; i < 65 && ns->path[i] == 0x09; i++)  //0x09の並びを読み飛ばす
      ;
    if (i >= 65 || ns->path[i] == 0x00)   //ディレクトリ名がなかった
      break;
    bb[k++] = 0x2f;  //ディレクトリ名の手前の'/'
    for (; i < 65 && ns->path[i] != 0x00 && ns->path[i] != 0x09; i++)
      bb[k++] = ns->path[i];  //ディレクトリ名
  }
  // 主ファイル名を展開する
  if (full) {
    bb[k++] = 0x2f;  //主ファイル名の手前の'/'
    memcpy(&bb[k], ns->name1, sizeof(ns->name1));   //主ファイル名1
    k += sizeof(ns->name1);
    memcpy(&bb[k], ns->name2, sizeof(ns->name2));   //主ファイル名2
    k += sizeof(ns->name2);
    for (; k > 0 && bb[k - 1] == 0x00; k--)   //主ファイル名2の末尾の0x00を切り捨てる
      ;
    for (; k > 0 && bb[k - 1] == 0x20; k--)   //主ファイル名1の末尾の0x20を切り捨てる
      ;
    bb[k++] = 0x2e;  //拡張子の手前の'.'
    memcpy(&bb[k], ns->ext, sizeof(ns->ext));   //拡張子
    k += sizeof(ns->ext);
    for (; k > 0 && bb[k - 1] == 0x20; k--)   //拡張子の末尾の0x20を切り捨てる
      ;
    for (; k > 0 && bb[k - 1] == 0x2e; k--)   //主ファイル名の末尾の0x2eを切り捨てる
      ;
  }

  char *dst_buf = (char *)path;
  strncpy(dst_buf, rootpath, sizeof(*path) - 1);
  dst_buf += strlen(rootpath);    //マウント先パス名を前置
#ifndef WINNT
  // SJIS -> UTF-8に変換
  size_t dst_len = sizeof(*path) - 1 - strlen(rootpath);  //パス名バッファ残りサイズ
  char *src_buf = bb;
  size_t src_len = k;
  iconv_t cd = iconv_open("UTF-8", "CP932");
  int r = iconv(cd, &src_buf, &src_len, &dst_buf, &dst_len);
  iconv_close(cd);
  if (r < 0) {
    return -1;  //変換できなかった
  }
#else
  // MinGWではSJISのまま使う
  memcpy(dst_buf, bb, k);
  dst_buf += k;
#endif
  *dst_buf = '\0';
  return 0;
}

// errnoをHuman68kのエラーコードに変換する
static int conv_errno(int err)
{
  switch (err) {
  case ENOENT:
    return _DOSE_NOENT;
  case ENOTDIR:
    return _DOSE_NODIR;
  case EMFILE:
    return _DOSE_MFILE;
  case EISDIR:
    return _DOSE_ISDIR;
  case EBADF:
    return _DOSE_BADF;
  case ENOMEM:
    return _DOSE_NOMEM;
  case EFAULT:
    return _DOSE_ILGMPTR;
  case ENOEXEC:
    return _DOSE_ILGFMT;
  /* case EINVAL:       // open
    return _DOSE_ILGARG; */
  case ENAMETOOLONG:
    return _DOSE_ILGFNAME;
  case EINVAL:
    return _DOSE_ILGPARM;
  case EXDEV:
    return _DOSE_ILGDRV;
  /* case EINVAL:       // rmdir
    return _DOSE_ISCURDIR; */
  case EACCES:
  case EPERM:
  case EROFS:
    return _DOSE_RDONLY;
  /* case EEXIST:       // mkdir
    return _DOSE_EXISTDIR; */
  case ENOTEMPTY:
    return _DOSE_NOTEMPTY;
  /* case ENOTEMPTY:    // rename
    return _DOSE_CANTREN; */
  case ENOSPC:
    return _DOSE_DISKFULL;
  /* case ENOSPC:       // create, open
    return _DOSE_DIRFULL; */
  case EOVERFLOW:
    return _DOSE_CANTSEEK;
  case EEXIST:
    return _DOSE_EXISTFILE;
  default:
    return _DOSE_ILGPARM;
  }
}

//****************************************************************************
// Filesystem operations
//****************************************************************************

void op_chdir(int fd, char *buf)
{
  struct cmd_dirop *cmd = (struct cmd_dirop *)buf;
  struct res_dirop res = { .res = 0 };
  hostpath_t path;

  if (conv_namebuf(&cmd->path, false, &path) < 0) {
    res.res = _DOSE_NODIR;
    goto errout;
  }

  struct stat st;
  int r = stat(path, &st);
  if (r != 0 || !S_ISDIR(st.st_mode)) {
    res.res = _DOSE_NODIR;
  }
errout:
  DPRINTF1("CHDIR: %s -> %d\n", path, res.res);
  serout(fd, &res, sizeof(res));
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void op_mkdir(int fd, char *buf)
{
  struct cmd_dirop *cmd = (struct cmd_dirop *)buf;
  struct res_dirop res = { .res = 0 };
  hostpath_t path;

  if (conv_namebuf(&cmd->path, true, &path) < 0) {
    res.res = _DOSE_NODIR;
    goto errout;
  }

#ifndef WINNT
  if (mkdir(path, 0777) < 0)
#else
  if (mkdir(path) < 0)
#endif
  {
    switch (errno) {
    case EEXIST:
      res.res = _DOSE_EXISTDIR;
      break;
    default:
      res.res = conv_errno(errno);
      break;
    }
  }
errout:
  DPRINTF1("MKDIR: %s -> %d\n", path, res.res);
  serout(fd, &res, sizeof(res));
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void op_rmdir(int fd, char *buf)
{
  struct cmd_dirop *cmd = (struct cmd_dirop *)buf;
  struct res_dirop res = { .res = 0 };
  hostpath_t path;

  if (conv_namebuf(&cmd->path, true, &path) < 0) {
    res.res = _DOSE_NODIR;
    goto errout;
  }

  if (rmdir(path) < 0) {
    switch (errno) {
    case EINVAL:
      res.res = _DOSE_ISCURDIR;
      break;
    default:
      res.res = conv_errno(errno);
      break;
    }
  }
errout:
  DPRINTF1("RMDIR: %s -> %d\n", path, res.res);
  serout(fd, &res, sizeof(res));
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void op_rename(int fd, char *buf)
{
  struct cmd_rename *cmd = (struct cmd_rename *)buf;
  struct res_rename res = { .res = 0 };
  hostpath_t pathold;
  hostpath_t pathnew;

  if (conv_namebuf(&cmd->path_old, true, &pathold) < 0) {
    res.res = _DOSE_NODIR;
    goto errout;
  }
  if (conv_namebuf(&cmd->path_new, true, &pathnew) < 0) {
    res.res = _DOSE_NODIR;
    goto errout;
  }

  if (rename(pathold, pathnew) < 0) {
    switch (errno) {
    case ENOTEMPTY:
      res.res = _DOSE_CANTREN;
      break;
    default:
      res.res = conv_errno(errno);
      break;
    }
  }
errout:
  DPRINTF1("RENAME: %s to %s  -> %d\n", pathold, pathnew, res.res);
  serout(fd, &res, sizeof(res));
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void op_delete(int fd, char *buf)
{
  struct cmd_dirop *cmd = (struct cmd_dirop *)buf;
  struct res_dirop res = { .res = 0 };
  hostpath_t path;

  if (conv_namebuf(&cmd->path, true, &path) < 0) {
    res.res = _DOSE_NODIR;
    goto errout;
  }

  if (unlink(path) < 0) {
    res.res = conv_errno(errno);
  }
errout:
  DPRINTF1("DELETE: %s -> %d\n", path, res.res);
  serout(fd, &res, sizeof(res));
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void op_chmod(int fd, char *buf)
{
  struct cmd_chmod *cmd = (struct cmd_chmod *)buf;
  struct res_chmod res = { .res = 0 };
  hostpath_t path;
  struct stat st;

  if (conv_namebuf(&cmd->path, true, &path) < 0) {
    res.res = _DOSE_NODIR;
    goto errout;
  }

  if (stat(path, &st) < 0) {
    res.res = conv_errno(errno);
  } else {
    res.res = conv_filemode(&st);
  }
  if (cmd->attr != 0xff) {
    if (cmd->attr & 0x01) {
      st.st_mode &= ~(S_IWUSR|S_IWGRP|S_IWOTH); // read only
    } else {
      st.st_mode |= (S_IWUSR|S_IWGRP|S_IWOTH);  // read/write
    }
    if (chmod(path, st.st_mode) < 0) {
      res.res = conv_errno(errno);
    } else {
      res.res = 0;
    }
  }
errout:
  if (res.res < 0)
    DPRINTF1("CHMOD: %s 0x%02x -> %d\n", path, cmd->attr, res.res);
  else
    DPRINTF1("CHMOD: %s 0x%02x -> 0x%02x\n", path, cmd->attr, res.res);
  serout(fd, &res, sizeof(res));
}

//****************************************************************************
// Directory operations
//****************************************************************************

// directory list management structure
// Human68kから渡されるFILBUFのアドレスをキーとしてディレクトリリストを管理する
typedef struct {
  uint32_t files;
  struct dos_filesinfo *dirbuf;
  int buflen;
  int bufcnt;
} dirlist_t;

static dirlist_t *dl_store;
static int dl_size = 0;

// FILBUFに対応するバッファを探す
static dirlist_t *dl_alloc(uint32_t files, bool create)
{
  for (int i = 0; i < dl_size; i++) {
    dirlist_t *dl = &dl_store[i];
    if (dl->files == files) {
      if (create) {         // 新規作成で同じFILBUFを見つけたらバッファを再利用
        free(dl->dirbuf);
        dl->dirbuf = NULL;
        dl->buflen = 0;
        dl->bufcnt = 0;
      }
      return dl;
    }
  }
  if (!create)
    return NULL;

  for (int i = 0; i < dl_size; i++) {
    dirlist_t *dl = &dl_store[i];
    if (dl->files == 0) {   // 新規作成で未使用のバッファを見つけた
      dl->files = files;
      return dl;
    }
  }
  dl_size++;                // バッファが不足しているので拡張する
  dl_store = realloc(dl_store, sizeof(dirlist_t) * dl_size);
  dirlist_t *dl = &dl_store[dl_size - 1];
  dl->files = files;
  dl->dirbuf = NULL;
  dl->buflen = 0;
  dl->bufcnt = 0;
  return dl;
}

// 不要になったバッファを解放する
static void dl_free(uint32_t files)
{
  for (int i = 0; i < dl_size; i++) {
    dirlist_t *dl = &dl_store[i];
    if (dl->files == files) {
      dl->files = 0;
      free(dl->dirbuf);
      dl->dirbuf = NULL;
      dl->buflen = 0;
      dl->bufcnt = 0;
      return;
    }
  }
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void op_files(int fd, char *buf)
{
  struct cmd_files *cmd = (struct cmd_files *)buf;
  struct res_files res = { .res = _DOSE_NOMORE };
  hostpath_t path;
  DIR *dir;
  struct dirent *d;
  dirlist_t *dl;
  bool isroot;

  dl = dl_alloc(cmd->filep, true);

  if (conv_namebuf(&cmd->path, false, &path) < 0) {
    res.res = _DOSE_NODIR;
    goto errout;
  }
  isroot = strcmp(cmd->path.path, "\t") == 0;   /* TBD */

  // (derived from HFS.java by Makoto Kamada)
  //検索するファイル名の順序を入れ替える
  //  主ファイル名1の末尾が'?'で主ファイル名2の先頭が'\0'のときは主ファイル名2を'?'で充填する
  uint8_t w[21] = { 0 };
  memcpy(&w[0], cmd->path.name1, 8);    //主ファイル名1
  if (cmd->path.name1[7] == '?' && cmd->path.name2[0] == '\0') {  //主ファイル名1の末尾が'?'で主ファイル名2の先頭が'\0'
    memset(&w[8], '?', 10);           //主ファイル名2
  } else {
    memcpy(&w[8], cmd->path.name2, 10); //主ファイル名2
  }
  for (int i = 17; i >= 0 && (w[i] == '\0' || w[i] == ' '); i--) {  //主ファイル名1+主ファイル名2の空き
    w[i] = '\0';
  }
  memcpy(&w[18], cmd->path.ext, 3);     //拡張子
  for (int i = 20; i >= 18 && (w[i] == ' '); i--) { //拡張子の空き
    w[i] = '\0';
  }
  //検索するファイル名を小文字化する
  for (int i = 0; i < 21; i++) {
    int c = w[i];
    if (0x81 <= c && c <= 0x9f || 0xe0 <= c && c <= 0xef) {  //SJISの1バイト目
      i++;
    } else {
      w[i] = tolower(w[i]);
    }
  }

  //検索するディレクトリの一覧を取得する
  if ((dir = opendir(path)) == NULL) {
    switch (errno) {
    case ENOENT:
      res.res = _DOSE_NODIR;    //ディレクトリが存在しない場合に_DOSE_NOENTを返すと正常動作しない
      break;
    default:
      res.res = conv_errno(errno);
      break;
    }
    goto errout;
  }
#ifndef WINNT
  iconv_t cd = iconv_open("CP932", "UTF-8");
#endif

  //ルートディレクトリかつボリューム名が必要な場合
  if (isroot && (cmd->attr & 0x08) != 0 &&
      w[0] == '?' && w[18] == '?') {    //検索するファイル名が*.*のとき
    //ボリューム名を作る
    dl->dirbuf = malloc(sizeof(struct dos_filesinfo));
    dl->buflen = 1;
    dl->dirbuf[0].atr = 0x08;   //ボリューム名
    dl->dirbuf[0].time = dl->dirbuf[0].date = 0;
    dl->dirbuf[0].filelen = 0;
#ifndef WINNT
    // ファイル名をSJISに変換する
    char *dst_buf = dl->dirbuf[0].name;
    size_t dst_len = sizeof(dl->dirbuf[0].name) - 2;
    char *src_buf = path;
    size_t src_len = strlen(path);
    iconv(cd, &src_buf, &src_len, &dst_buf, &dst_len);
    *dst_buf = '\0';
#else
    // MinGWではSJISのまま
    int i;
    for (i = 0; i < 21; i++) {
      uint8_t c = path[i];
      dl->dirbuf[0].name[i] = c;
      if (0x81 <= c && c <= 0x9f || 0xe0 <= c && c <= 0xef) {  //SJISの1バイト目
        if (i >= 20)
          break;      //2バイト目がバッファに収まらない
        i++;
        dl->dirbuf[0].name[i] = path[i];  //SJISの2バイト目
      }
    }
    dl->dirbuf[0].name[i] = '\0';
#endif
  }

  //ディレクトリの一覧から属性とファイル名の条件に合うものを選ぶ
  while (d = readdir(dir)) {
    char *childName = d->d_name;

    if (isroot) {  //ルートディレクトリのとき
      if (strcmp(childName, ".") == 0 || strcmp(childName, "..") == 0) {  //.と..を除く
        continue;
      }
    }

#ifndef WINNT
    // ファイル名をSJISに変換する
    char *dst_buf = res.file.name;
    size_t dst_len = sizeof(res.file.name) - 1;
    char *src_buf = childName;
    size_t src_len = strlen(childName);
    if (iconv(cd, &src_buf, &src_len, &dst_buf, &dst_len) < 0) {
      continue;
    }
    *dst_buf = '\0';
#else
    // MinGWではSJISのまま
    if (strlen(childName) > sizeof(res.file.name) - 1) {
      continue;
    }
    strcpy(res.file.name, childName);
#endif
    uint8_t c;
    for (int i = 0; i < sizeof(res.file.name); i++) {
      if (!(c = res.file.name[i]))
        break;
      if (0x81 <= c && c <= 0x9f || 0xe0 <= c && c <= 0xef) {  //SJISの1バイト目
        i++;
        continue;
      }
      if (c <= 0x1f ||  //変換できない文字または制御コード
          (c == '-' && i == 0) ||  //ファイル名の先頭に使えない文字
          strchr("/\\,;<=>[]|", c) != NULL) {  //ファイル名に使えない文字
        break;
      }
    }
    if (c) {  //ファイル名に使えない文字がある
      continue;
    }

    //ファイル名を分解する
    char *b = res.file.name;
    int k = strlen(b);
    int m = (b[k - 1] == '.' ? k :  //name.
             k >= 3 && b[k - 2] == '.' ? k - 2 :  //name.e
             k >= 4 && b[k - 3] == '.' ? k - 3 :  //name.ex
             k >= 5 && b[k - 4] == '.' ? k - 4 :  //name.ext
             k);  //主ファイル名の直後。拡張子があるときは'.'の位置、ないときはk
    if (m > 18) {  //主ファイル名が長すぎる
      continue;
    }
    uint8_t w2[21] = { 0 };
    memcpy(&w2[0], &b[0], m);         //主ファイル名
    if (b[m] == '.')
      strncpy(&w2[18], &b[m + 1], 3); //拡張子

    for (int i = 0; i < 21; i++)
      DPRINTF2("%c", w2[i] == 0 ? '_' : w2[i]);
    DPRINTF2("\n");

    //ファイル名を比較する
    {
      int f = 0x20;  //0x00=次のバイトはSJISの2バイト目,0x20=次のバイトはSJISの2バイト目ではない
      int i;
      for (i = 0; i <= 20; i++) {
        int c = w2[i];
        int d = w[i];
        if (d != '?' && ('A' <= c && c <= 'Z' ? c | f : c) != d) {  //検索するファイル名の'?'以外の部分がマッチしない。SJISの2バイト目でなければ小文字化してから比較する
          break;
        }
        f = f != 0x00 && (0x81 <= c && c <= 0x9f || 0xe0 <= c && c <= 0xef) ? 0x00 : 0x20;  //このバイトがSJISの2バイト目ではなくてSJISの1バイト目ならば次のバイトはSJISの2バイト目
      }
      if (i < 20) { //ファイル名がマッチしなかった
        continue;
      }
    }

    //属性、時刻、日付、ファイルサイズを取得する
    hostpath_t fullpath;
    strcpy(fullpath, path);
    if (strcmp(fullpath, "/") != 0)
      strncat(fullpath, "/", sizeof(fullpath) - 1);
    strncat(fullpath, childName, sizeof(fullpath) - 1);
    struct stat st;
    if (stat(fullpath, &st) < 0) {  // ファイル情報を取得できなかった
      continue;
    }
    if (0xffffffffL < st.st_size) {  //4GB以上のファイルは検索できないことにする
      continue;
    }
    conv_statinfo(&st, &res.file);
    if ((res.file.atr & cmd->attr) == 0) {  //属性がマッチしない
      continue;
    }

    //ファイル名リストに追加する
    dl->dirbuf = realloc(dl->dirbuf, sizeof(struct dos_filesinfo) * (dl->buflen + 1));
    memcpy(&dl->dirbuf[dl->buflen], &res.file, sizeof(struct dos_filesinfo));
    dl->buflen++;
  }

#ifndef WINNT
  iconv_close(cd);
#endif
  closedir(dir);

  for (int i = 0; i < dl->buflen; i++) {
    DPRINTF2("%d %s\n", i, dl->dirbuf[i].name);
  }

  //ファイル名リストの最初のエントリを返す
  if (dl->bufcnt < dl->buflen) {
    memcpy(&res.file, &dl->dirbuf[dl->bufcnt], sizeof(res.file));
    dl->bufcnt++;
    res.res = 0;
  }

errout:
  DPRINTF1("FILES: 0x%08x 0x%02x %s -> ", cmd->filep, cmd->attr, path);
  if (res.res)
    DPRINTF1("%d\n", res.res);
  else
    DPRINTF1("(%d/%d) %s\n", dl->bufcnt, dl->buflen, res.file.name);

  if (dl->bufcnt == dl->buflen) {   //ファイル名リストが空
    dl_free(cmd->filep);
  }
  serout(fd, &res, sizeof(res));
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void op_nfiles(int fd, char *buf)
{
  struct cmd_nfiles *cmd = (struct cmd_nfiles *)buf;
  struct res_nfiles res = { .res = _DOSE_NOMORE };
  dirlist_t *dl;

  DPRINTF1("NFILES: 0x%08x -> ", cmd->filep);

  if (dl = dl_alloc(cmd->filep, false)) {
    memcpy(&res.file, &dl->dirbuf[dl->bufcnt], sizeof(res.file));
    dl->bufcnt++;
    res.res = 0;
    DPRINTF1("(%d/%d) %s\n", dl->bufcnt, dl->buflen, res.file.name);
    if (dl->bufcnt == dl->buflen) {   //もう残っているファイルがない
      dl_free(cmd->filep);
    }
  } else {
    DPRINTF1("%d\n", res.res);
  }

  serout(fd, &res, sizeof(res));
}

//****************************************************************************
// File operations
//****************************************************************************

// file descriptor management structure
// Human68kから渡されるFCBのアドレスをキーとしてfdを管理する
typedef struct {
  uint32_t fcb;
  int fd;
} fdinfo_t;

static fdinfo_t *fi_store;
static int fi_size = 0;

// FCBに対応するバッファを探す
static fdinfo_t *fi_alloc(uint32_t fcb, bool alloc)
{
  for (int i = 0; i < fi_size; i++) {
    if (fi_store[i].fcb == fcb) {
      if (alloc) {              // 新規作成で同じFCBを見つけたらバッファを再利用
        close(fi_store[i].fd);
        fi_store[i].fd = -1;
      }
      return &fi_store[i];
    }
  }
  if (!alloc)
    return NULL;

  for (int i = 0; i < fi_size; i++) {
    if (fi_store[i].fcb == 0) { // 新規作成で未使用のバッファを見つけた
      fi_store[i].fcb = fcb;
      return &fi_store[i];
    }
  }
  fi_size++;                    // バッファが不足しているので拡張する
  fi_store = realloc(fi_store, sizeof(fdinfo_t) * fi_size);
  fi_store[fi_size - 1].fcb = fcb;
  fi_store[fi_size - 1].fd = -1;
  return &fi_store[fi_size - 1];
}

// FCBに対応するfdを返す
static int fi_getfd(uint32_t fcb)
{
  fdinfo_t *fi = fi_alloc(fcb, false);
  return fi == NULL ? -1 : fi->fd;
}

// 不要になったバッファを解放する
static void fi_free(uint32_t fcb)
{
  for (int i = 0; i < fi_size; i++) {
    if (fi_store[i].fcb == fcb) {
      fi_store[i].fcb = 0;
      fi_store[i].fd = -1;
      return;
    }
  }
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void op_create(int fd, char *buf)
{
  struct cmd_create *cmd = (struct cmd_create *)buf;
  struct res_create res = { .res = 0 };
  hostpath_t path;
  int filefd;

  if (conv_namebuf(&cmd->path, true, &path) < 0) {
    res.res = _DOSE_NODIR;
    goto errout;
  }

  int mode = O_CREAT|O_RDWR|O_TRUNC|O_BINARY;
  mode |= cmd->mode ? 0 : O_EXCL;
  if ((filefd = open(path, mode, 0777)) < 0) {
    switch (errno) {
    case ENOSPC:
      res.res = _DOSE_DIRFULL;
      break;
    default:
      res.res = conv_errno(errno);
      break;
    }
  } else {
    fdinfo_t *fi = fi_alloc(cmd->fcb, true);
    fi->fd = filefd;
  }
errout:
  DPRINTF1("CREATE: fcb=0x%08x attr=0x%02x mode=%d %s -> %d\n", cmd->fcb, cmd->attr, cmd->mode, path, res.res);
  serout(fd, &res, sizeof(res));
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void op_open(int fd, char *buf)
{
  struct cmd_open *cmd = (struct cmd_open *)buf;
  struct res_open res = { .res = 0 };
  hostpath_t path;
  int mode;
  int filefd;

  if (conv_namebuf(&cmd->path, true, &path) < 0) {
    res.res = _DOSE_NODIR;
    goto errout;
  }

  switch (cmd->mode) {
  case 0:
    mode = O_RDONLY|O_BINARY;
    break;
  case 1:
    mode = O_WRONLY|O_BINARY;
    break;
  case 2:
    mode = O_RDWR|O_BINARY;
    break;
  default:
    res.res = _DOSE_ILGARG;
    goto errout;
  }

  if ((filefd = open(path, mode)) < 0) {
    switch (errno) {
    case EINVAL:
      res.res = _DOSE_ILGARG;
      break;
    default:
      res.res = conv_errno(errno);
      break;
    }
  } else {
    fdinfo_t *fi = fi_alloc(cmd->fcb, true);
    fi->fd = filefd;
    uint32_t len = lseek(filefd, 0, SEEK_END);
    lseek(filefd, 0, SEEK_SET);
    res.size = htobe32(len);
  }
errout:
  DPRINTF1("OPEN: fcb=x%08x mode=%d %s -> %d %d\n", cmd->fcb, cmd->mode, path, res.res, be32toh(res.size));
  serout(fd, &res, sizeof(res));
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void op_close(int fd, char *buf)
{
  struct cmd_close *cmd = (struct cmd_close *)buf;
  struct res_close res = { .res = 0 };
  int filefd = fi_getfd(cmd->fcb);

  fi_free(cmd->fcb);
  close(filefd);

  DPRINTF1("CLOSE: fcb=0x%08x\n", cmd->fcb);
  serout(fd, &res, sizeof(res));
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void op_read(int fd, char *buf)
{
  struct cmd_read *cmd = (struct cmd_read *)buf;
  struct cmd_read_ack ack;
  struct res_read res;
  int filefd = fi_getfd(cmd->fcb);
  size_t len = be32toh(cmd->len);
  size_t size = 0;

  while (1) {
    ssize_t bytes = len > sizeof(res.data) ? sizeof(res.data): len;
    bytes = read(filefd, res.data, bytes);
    if (bytes < 0) {
      res.len = conv_errno(errno);
      bytes = 0;
    } else {
      res.len = htobe16(bytes);
    }
    len -= bytes;
    size += bytes;
    DPRINTF1(" read %d %u %u\n", bytes, len, size);
    serout(fd, &res, bytes + 2);
    if (bytes <= 0)   // ファイルが終了orエラー
      break;
    if (serin(fd, &ack, sizeof(ack)) < 0 || ack.ack != 0)
      break;
  }

  DPRINTF1("READ: fcb=0x%08x %d -> %d\n", cmd->fcb, be32toh(cmd->len), size);
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void op_write(int fd, char *buf)
{
  struct cmd_write *cmd = (struct cmd_write *)buf;
  struct cmd_write_body body;
  struct res_write res;
  int filefd = fi_getfd(cmd->fcb);
  size_t len = be32toh(cmd->len);
  size_t size = 0;

  if (len == 0) {     // 0バイトのwriteはファイル長を0に切り詰める
    if (ftruncate(filefd, lseek(filefd, 0, SEEK_CUR)) < 0) {
      res.len = conv_errno(errno);
    } else {
      res.len = 0;
    }
  } else {
    res.len = 1;      // これから書き込みデータを受信する
  }
  serout(fd, &res, sizeof(res));
  while (len > 0 && res.len > 0) {
    if (serin(fd, &body, sizeof(body)) < 0)
      break;
    ssize_t bytes = write(filefd, body.data, be16toh(body.len));
    if (bytes < 0) {
      res.len = conv_errno(errno);
      bytes = 0;
    } else {
      res.len = htobe16(bytes);
    }
    len -= bytes;
    size += bytes;
    DPRINTF1(" write %d %u %u\n", bytes, len, size);
    serout(fd, &res, sizeof(res));
  }

  DPRINTF1("WRITE: fcb=0x%08x %d -> %d\n", cmd->fcb, be32toh(cmd->len), size);
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void op_seek(int fd, char *buf)
{
  struct cmd_seek *cmd = (struct cmd_seek *)buf;
  struct res_seek res = { .res = 0 };
  int filefd = fi_getfd(cmd->fcb);

  off_t off = lseek(filefd, (int32_t)be32toh(cmd->offset), cmd->whence);
  if (off == (off_t)-1) {
    res.res = conv_errno(errno);
  } else {
    res.pos = htobe32(off);
  }
  DPRINTF1("SEEK: fcb=0x%x offset=%d whence=%d ->", cmd->fcb, be32toh(cmd->offset), cmd->whence);
  if (res.res)
    DPRINTF1("%d\n", res.res);
  else
    DPRINTF1("%d\n", off);
  serout(fd, &res, sizeof(res));
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void op_filedate(int fd, char *buf)
{
  struct cmd_filedate *cmd = (struct cmd_filedate *)buf;
  struct res_filedate res;
  int filefd = fi_getfd(cmd->fcb);

  if (cmd->time == 0 && cmd->date == 0) {   // 更新日時取得
    struct stat st;
    if (fstat(filefd, &st) < 0) {
      res.time = res.date = 0xffff;
    } else {
      struct dos_filesinfo fi;
      conv_statinfo(&st, &fi);
      res.time = fi.time;
      res.date = fi.date;
    }
  } else {                                  // 更新日時設定
    uint16_t time = be16toh(cmd->time);
    uint16_t date = be16toh(cmd->date);
#ifndef WINNT
    struct tm tm;
    tm.tm_sec = (time << 1) & 0x3f;
    tm.tm_min = (time >> 5) & 0x3f;
    tm.tm_hour = (time >> 11) & 0x1f;
    tm.tm_mday = date & 0x1f;
    tm.tm_mon = ((date >> 5) & 0xf) - 1;
    tm.tm_year = ((date >> 9) & 0x7f) + 80;
    tm.tm_isdst = 0;
    time_t tt = mktime(&tm);
    struct timespec tv[2];
    tv[0].tv_sec = tv[1].tv_sec = tt;
    tv[0].tv_nsec = tv[1].tv_nsec = 0;
    futimens(filefd, tv);
#else
    FILETIME lt;
    DosDateTimeToFileTime(date, time, &lt);
    FILETIME ft;
    LocalFileTimeToFileTime(&lt, &ft);
    SetFileTime((HANDLE)_get_osfhandle(filefd), NULL, &ft, &ft);
#endif
    res.time = 0;
    res.date = 0;
  }

  DPRINTF1("FILEDATE: fcb=0x%08x 0x%04x 0x%04x -> 0x%04x 0x%04x\n", cmd->fcb, be16toh(cmd->date), be16toh(cmd->time), be16toh(res.date), be16toh(res.time));
  serout(fd, &res, sizeof(res));
}

//****************************************************************************
// Misc functions
//****************************************************************************

void op_dskfre(int fd, char *buf)
{
  struct cmd_dskfre *cmd = (struct cmd_dskfre *)buf;
  struct res_dskfre res = { .res = -1 };
  uint64_t total;
  uint64_t free;

#ifndef WINNT
  struct statfs sf;
  statfs(rootpath, &sf);
  total = sf.f_blocks * sf.f_bsize;
  free = sf.f_bfree * sf.f_bsize;
#else
  ULARGE_INTEGER ultotal;
  ULARGE_INTEGER ulfree;
  ULARGE_INTEGER ultotalfree;
  GetDiskFreeSpaceEx(rootpath, &ulfree, &ultotal, &ultotalfree);
  total = ultotal.QuadPart;
  free = ulfree.QuadPart;
#endif

  total = total > 0x7fffffff ? 0x7fffffff : total;
  free = free > 0x7fffffff ? 0x7fffffff : free;
  res.freeclu = htobe16(free / 32768);
  res.totalclu = htobe16(total /32768);
  res.clusect = htobe16(128);
  res.sectsize = htobe16(1024);
  res.res = htobe32(free);

  DPRINTF1("DSKFRE: free=%u total=%u clusect=%u sectsz=%u res=%d\n", be16toh(res.freeclu), be16toh(res.totalclu), be16toh(res.clusect), be16toh(res.sectsize), be32toh(res.res));
  serout(fd, &res, sizeof(res));
}

//****************************************************************************
// main
//****************************************************************************

int main(int argc, char **argv)
{
  char *device = NULL;
  int baudrate = 38400;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-D") == 0) {
      debuglevel++;
    } else if (strcmp(argv[i], "-s") == 0) {
      if (i + 1 < argc) {
        i++;
        baudrate = atoi(argv[i]);
      }
    } else if (device == NULL) {
      device = argv[i];
    } else {
      rootpath = argv[i];
    }
  }

  if (device == NULL) {
    printf("Usage: %s [-D|-s <speed>] <COM port> [<base directory>]\n", argv[0]);
    return 1;
  }

  int fd = seropen(device, baudrate);
  if (fd < 0) {
    printf("COM port open error\n");
    return 1;
  }

  printf("X68000 Serial Remote Drive Service (version %s)\n", GIT_REPO_VERSION);

  while (1) {
    uint8_t buf[1024];
    if (serin(fd, buf, sizeof(buf)) < 0) {
      continue;
    }

    DPRINTF2("----Command: 0x%02x\n", buf[0]);

    switch (buf[0]) {
    case 0x41: /* chdir */
      op_chdir(fd, buf);
      break;
    case 0x42: /* mkdir */
      op_mkdir(fd, buf);
      break;
    case 0x43: /* rmdir */
      op_rmdir(fd, buf);
      break;
    case 0x44: /* rename */
      op_rename(fd, buf);
      break;
    case 0x45: /* remove */
      op_delete(fd, buf);
      break;
    case 0x46: /* chmod */
      op_chmod(fd, buf);
      break;
    case 0x47: /* files */
      op_files(fd, buf);
      break;
    case 0x48: /* nfiles */
      op_nfiles(fd, buf);
      break;
    case 0x49: /* create */
      op_create(fd, buf);
      break;
    case 0x4a: /* open */
      op_open(fd, buf);
      break;
    case 0x4b: /* close */
      op_close(fd, buf);
      break;
    case 0x4c: /* read */
      op_read(fd, buf);
      break;
    case 0x4d: /* write */
      op_write(fd, buf);
      break;
    case 0x4e: /* seek */
      op_seek(fd, buf);
      break;
    case 0x4f: /* filedate */
      op_filedate(fd, buf);
      break;
    case 0x50: /* dskfre */
      op_dskfre(fd, buf);
      break;

    case 0x51: /* drvctrl */
    case 0x52: /* getdbp */
    case 0x53: /* diskred */
    case 0x54: /* diskwrt */
    case 0x55: /* ioctl */
    case 0x56: /* abort */
    case 0x57: /* mediacheck */
    case 0x58: /* lock */
    default:
      DPRINTF1("error: %02x\n", buf[0]);
    }
  }

  close(fd);
  return 0;
}
