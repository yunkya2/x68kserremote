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
#include <stddef.h>
#include <ctype.h>
#include <string.h>
#include <errno.h>

#include <config.h>
#include <fileop.h>
#include <x68kremote.h>
#include "remoteserv.h"

//****************************************************************************
// Global type and variables
//****************************************************************************

typedef char hostpath_t[256];

//****************************************************************************
// Utility functions
//****************************************************************************

// struct statのファイル情報を変換する
static void conv_statinfo(TYPE_STAT *st, void *v)
{
  struct dos_filesinfo *f = (struct dos_filesinfo *)v;

  f->atr = FUNC_FILEMODE_ATTR(st);
  f->filelen = htobe32(STAT_SIZE(st));
  struct tm *tm = localtime(&STAT_MTIME(st));
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
  // SJIS -> UTF-8に変換
  size_t dst_len = sizeof(*path) - 1 - strlen(rootpath);  //パス名バッファ残りサイズ
  char *src_buf = bb;
  size_t src_len = k;
  if (FUNC_ICONV_S2U(&src_buf, &src_len, &dst_buf, &dst_len) < 0) {
    return -1;  //変換できなかった
  }
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

int op_check(uint8_t *cbuf, uint8_t *rbuf)
{
  struct cmd_check *cmd = (struct cmd_check *)cbuf;
  struct res_check *res = (struct res_check *)rbuf;

  res->res = 0;
  DPRINTF1("CHECK:\n");
  return sizeof(*res);
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

int op_chdir(uint8_t *cbuf, uint8_t *rbuf)
{
  struct cmd_dirop *cmd = (struct cmd_dirop *)cbuf;
  struct res_dirop *res = (struct res_dirop *)rbuf;
  hostpath_t path;

  res->res = 0;

  if (conv_namebuf(&cmd->path, false, &path) < 0) {
    res->res = _DOSE_NODIR;
    goto errout;
  }

  TYPE_STAT st;
  int r = FUNC_STAT(NULL, path, &st);
  if (r != 0 || !STAT_ISDIR(&st)) {
    res->res = _DOSE_NODIR;
  }
errout:
  DPRINTF1("CHDIR: %s -> %d\n", path, res->res);
  return sizeof(*res);
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

int op_mkdir(uint8_t *cbuf, uint8_t *rbuf)
{
  struct cmd_dirop *cmd = (struct cmd_dirop *)cbuf;
  struct res_dirop *res = (struct res_dirop *)rbuf;
  hostpath_t path;

  res->res = 0;

  if (conv_namebuf(&cmd->path, true, &path) < 0) {
    res->res = _DOSE_NODIR;
    goto errout;
  }

  int err;
  if (FUNC_MKDIR(&err, path) < 0) {
    switch (err) {
    case EEXIST:
      res->res = _DOSE_EXISTDIR;
      break;
    default:
      res->res = conv_errno(err);
      break;
    }
  }
errout:
  DPRINTF1("MKDIR: %s -> %d\n", path, res->res);
  return sizeof(*res);
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

int op_rmdir(uint8_t *cbuf, uint8_t *rbuf)
{
  struct cmd_dirop *cmd = (struct cmd_dirop *)cbuf;
  struct res_dirop *res = (struct res_dirop *)rbuf;
  hostpath_t path;

  res->res = 0;

  if (conv_namebuf(&cmd->path, true, &path) < 0) {
    res->res = _DOSE_NODIR;
    goto errout;
  }

  int err;
  if (FUNC_RMDIR(&err, path) < 0) {
    switch (err) {
    case EINVAL:
      res->res = _DOSE_ISCURDIR;
      break;
    default:
      res->res = conv_errno(err);
      break;
    }
  }
errout:
  DPRINTF1("RMDIR: %s -> %d\n", path, res->res);
  return sizeof(*res);
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

int op_rename(uint8_t *cbuf, uint8_t *rbuf)
{
  struct cmd_rename *cmd = (struct cmd_rename *)cbuf;
  struct res_rename *res = (struct res_rename *)rbuf;
  hostpath_t pathold;
  hostpath_t pathnew;

  res->res = 0;

  if (conv_namebuf(&cmd->path_old, true, &pathold) < 0) {
    res->res = _DOSE_NODIR;
    goto errout;
  }
  if (conv_namebuf(&cmd->path_new, true, &pathnew) < 0) {
    res->res = _DOSE_NODIR;
    goto errout;
  }

  int err;
  if (FUNC_RENAME(&err, pathold, pathnew) < 0) {
    switch (err) {
    case ENOTEMPTY:
      res->res = _DOSE_CANTREN;
      break;
    default:
      res->res = conv_errno(err);
      break;
    }
  }
errout:
  DPRINTF1("RENAME: %s to %s  -> %d\n", pathold, pathnew, res->res);
  return sizeof(*res);
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

int op_delete(uint8_t *cbuf, uint8_t *rbuf)
{
  struct cmd_dirop *cmd = (struct cmd_dirop *)cbuf;
  struct res_dirop *res = (struct res_dirop *)rbuf;
  hostpath_t path;

  res->res = 0;

  if (conv_namebuf(&cmd->path, true, &path) < 0) {
    res->res = _DOSE_NODIR;
    goto errout;
  }

  int err;
  if (FUNC_UNLINK(&err, path) < 0) {
    res->res = conv_errno(err);
  }
errout:
  DPRINTF1("DELETE: %s -> %d\n", path, res->res);
  return sizeof(*res);
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

int op_chmod(uint8_t *cbuf, uint8_t *rbuf)
{
  struct cmd_chmod *cmd = (struct cmd_chmod *)cbuf;
  struct res_chmod *res = (struct res_chmod *)rbuf;
  hostpath_t path;
  TYPE_STAT st;

  res->res = 0;

  if (conv_namebuf(&cmd->path, true, &path) < 0) {
    res->res = _DOSE_NODIR;
    goto errout;
  }

  int err;
  if (FUNC_STAT(&err, path, &st) < 0) {
    res->res = conv_errno(err);
  } else {
    res->res = FUNC_FILEMODE_ATTR(&st);
  }
  if (cmd->attr != 0xff) {
    if (FUNC_CHMOD(&err, path, FUNC_ATTR_FILEMODE(cmd->attr, &st)) < 0) {
      res->res = conv_errno(err);
    } else {
      res->res = 0;
    }
  }
errout:
  if (res->res < 0)
    DPRINTF1("CHMOD: %s 0x%02x -> %d\n", path, cmd->attr, res->res);
  else
    DPRINTF1("CHMOD: %s 0x%02x -> 0x%02x\n", path, cmd->attr, res->res);
  return sizeof(*res);
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

int op_files(uint8_t *cbuf, uint8_t *rbuf)
{
  struct cmd_files *cmd = (struct cmd_files *)cbuf;
  struct res_files *res = (struct res_files *)rbuf;
  hostpath_t path;
  TYPE_DIR *dir;
  TYPE_DIRENT *d;
  dirlist_t *dl;
  bool isroot;

  res->res = _DOSE_NOMORE;
#if CONFIG_NFILEINFO > 1
  res->num = 0;
#endif

  dl = dl_alloc(cmd->filep, true);

  if (conv_namebuf(&cmd->path, false, &path) < 0) {
    res->res = _DOSE_NODIR;
    goto errout;
  }
  isroot = strcmp(cmd->path.path, "\t") == 0;

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
  int err;
  if ((dir = FUNC_OPENDIR(&err, path)) == NULL) {
    switch (err) {
    case ENOENT:
      res->res = _DOSE_NODIR;    //ディレクトリが存在しない場合に_DOSE_NOENTを返すと正常動作しない
      break;
    default:
      res->res = conv_errno(err);
      break;
    }
    goto errout;
  }

  //ルートディレクトリかつボリューム名が必要な場合
  if (isroot && (cmd->attr & 0x08) != 0 &&
      w[0] == '?' && w[18] == '?') {    //検索するファイル名が*.*のとき
    //ボリューム名を作る
    dl->dirbuf = malloc(sizeof(struct dos_filesinfo));
    dl->buflen = 1;
    dl->dirbuf[0].atr = 0x08;   //ボリューム名
    dl->dirbuf[0].time = dl->dirbuf[0].date = 0;
    dl->dirbuf[0].filelen = 0;
    // ファイル名をSJISに変換する
    char *dst_buf = dl->dirbuf[0].name;
    size_t dst_len = sizeof(dl->dirbuf[0].name) - 2;
    char *src_buf = path;
    size_t src_len = strlen(path);
    FUNC_ICONV_U2S(&src_buf, &src_len, &dst_buf, &dst_len);
    *dst_buf = '\0';
  }

  //ディレクトリの一覧から属性とファイル名の条件に合うものを選ぶ
  while (d = FUNC_READDIR(NULL, dir)) {
    char *childName = DIRENT_NAME(d);

    if (isroot) {  //ルートディレクトリのとき
      if (strcmp(childName, ".") == 0 || strcmp(childName, "..") == 0) {  //.と..を除く
        continue;
      }
    }

    // ファイル名をSJISに変換する
    char *dst_buf = res->file[0].name;
    size_t dst_len = sizeof(res->file[0].name) - 1;
    char *src_buf = childName;
    size_t src_len = strlen(childName);
    if (FUNC_ICONV_U2S(&src_buf, &src_len, &dst_buf, &dst_len) < 0) {
      continue;
    }
    *dst_buf = '\0';
    uint8_t c;
    for (int i = 0; i < sizeof(res->file[0].name); i++) {
      if (!(c = res->file[0].name[i]))
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
    char *b = res->file[0].name;
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
      for (i = 0; i < 21; i++) {
        int c = w2[i];
        int d = w[i];
        if (d != '?' && ('A' <= c && c <= 'Z' ? c | f : c) != d) {  //検索するファイル名の'?'以外の部分がマッチしない。SJISの2バイト目でなければ小文字化してから比較する
          break;
        }
        f = f != 0x00 && (0x81 <= c && c <= 0x9f || 0xe0 <= c && c <= 0xef) ? 0x00 : 0x20;  //このバイトがSJISの2バイト目ではなくてSJISの1バイト目ならば次のバイトはSJISの2バイト目
      }
      if (i < 21) { //ファイル名がマッチしなかった
        continue;
      }
    }

    //属性、時刻、日付、ファイルサイズを取得する
    hostpath_t fullpath;
    strcpy(fullpath, path);
    if (strcmp(fullpath, "/") != 0)
      strncat(fullpath, "/", sizeof(fullpath) - 1);
    strncat(fullpath, childName, sizeof(fullpath) - 1);
    TYPE_STAT st;
    if (FUNC_STAT(NULL, fullpath, &st) < 0) {  // ファイル情報を取得できなかった
      continue;
    }
    if (0xffffffffL < STAT_SIZE(&st)) {  //4GB以上のファイルは検索できないことにする
      continue;
    }
    conv_statinfo(&st, &res->file[0]);
    if ((res->file[0].atr & cmd->attr) == 0) {  //属性がマッチしない
      continue;
    }

    //ファイル名リストに追加する
    dl->dirbuf = realloc(dl->dirbuf, sizeof(struct dos_filesinfo) * (dl->buflen + 1));
    memcpy(&dl->dirbuf[dl->buflen], &res->file[0], sizeof(struct dos_filesinfo));
    dl->buflen++;
  }

  FUNC_CLOSEDIR(NULL, dir);

#ifdef CONFIG_DIRREVERSE
  dl->bufcnt = dl->buflen;
#endif

  for (int i = 0; i < dl->buflen; i++) {
    DPRINTF2("%d %s\n", i, dl->dirbuf[i].name);
  }

  //ファイル名リストの最初のエントリを返す
#if CONFIG_NFILEINFO > 1
  int n = sizeof(res->file) / sizeof(res->file[0]);
  n = n > cmd->num ? cmd->num : n;
  for (int i = 0; i < n; i++) {
#ifdef CONFIG_DIRREVERSE
    if (dl->bufcnt > 0) {
      memcpy(&res->file[i], &dl->dirbuf[--dl->bufcnt], sizeof(res->file));
      res->num++;
      res->res = 0;
    }
#else
    if (dl->bufcnt < dl->buflen) {
      memcpy(&res->file[i], &dl->dirbuf[dl->bufcnt++], sizeof(res->file));
      res->num++;
      res->res = 0;
    }
#endif
      else {
      break;
    }
  }
#else
#ifdef CONFIG_DIRREVERSE
  if (dl->bufcnt > 0) {
    memcpy(&res->file[0], &dl->dirbuf[--dl->bufcnt], sizeof(res->file[0]));
    res->res = 0;
  }
#else
  if (dl->bufcnt < dl->buflen) {
    memcpy(&res->file[0], &dl->dirbuf[dl->bufcnt++], sizeof(res->file[0]));
    res->res = 0;
  }
#endif
#endif

errout:
#if CONFIG_NFILEINFO > 1
  DPRINTF1("FILES: 0x%08x 0x%02x %d %s -> ", cmd->filep, cmd->attr, cmd->num, path);
#else
  DPRINTF1("FILES: 0x%08x 0x%02x %s -> ", cmd->filep, cmd->attr, path);
#endif
  if (res->res)
    DPRINTF1("%d\n", res->res);
  else
    DPRINTF1("(%d/%d) %s\n", dl->bufcnt, dl->buflen, res->file[0].name);

#ifdef CONFIG_DIRREVERSE
  if (dl->bufcnt <= 0)   //ファイル名リストが空
#else
  if (dl->bufcnt == dl->buflen)   //ファイル名リストが空
#endif
  {
    dl_free(cmd->filep);
  }
  return sizeof(*res);
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

int op_nfiles(uint8_t *cbuf, uint8_t *rbuf)
{
  struct cmd_nfiles *cmd = (struct cmd_nfiles *)cbuf;
  struct res_nfiles *res = (struct res_nfiles *)rbuf;
  dirlist_t *dl;

  res->res = _DOSE_NOMORE;
#if CONFIG_NFILEINFO > 1
  res->num = 0;
#endif

#if CONFIG_NFILEINFO > 1
  DPRINTF1("NFILES: 0x%08x %d -> ", cmd->filep, cmd->num);
#else
  DPRINTF1("NFILES: 0x%08x -> ", cmd->filep);
#endif

  if (dl = dl_alloc(cmd->filep, false)) {
#if CONFIG_NFILEINFO > 1
    int n = sizeof(res->file) / sizeof(res->file[0]);
    n = n > cmd->num ? cmd->num : n;
    for (int i = 0; i < n; i++) {
#ifdef CONFIG_DIRREVERSE
      memcpy(&res->file[i], &dl->dirbuf[--dl->bufcnt], sizeof(res->file));
#else
      memcpy(&res->file[i], &dl->dirbuf[dl->bufcnt++], sizeof(res->file));
#endif
      res->num++;
      res->res = 0;
      DPRINTF1("(%d/%d) %s\n", dl->bufcnt, dl->buflen, res->file[i].name);
#ifdef CONFIG_DIRREVERSE
      if (dl->bufcnt <= 0)   //もう残っているファイルがない
#else
      if (dl->bufcnt == dl->buflen)   //もう残っているファイルがない
#endif
      {
        dl_free(cmd->filep);
        break;
      }
    }
#else
#ifdef CONFIG_DIRREVERSE
    memcpy(&res->file[0], &dl->dirbuf[--dl->bufcnt], sizeof(res->file[0]));
#else
    memcpy(&res->file[0], &dl->dirbuf[dl->bufcnt++], sizeof(res->file[0]));
#endif
    res->res = 0;
    DPRINTF1("(%d/%d) %s\n", dl->bufcnt, dl->buflen, res->file[0].name);
#ifdef CONFIG_DIRREVERSE
    if (dl->bufcnt <= 0)   //もう残っているファイルがない
#else
    if (dl->bufcnt == dl->buflen)   //もう残っているファイルがない
#endif
    {
      dl_free(cmd->filep);
    }
#endif
  } else {
    DPRINTF1("%d\n", res->res);
  }

  return sizeof(*res);
}

//****************************************************************************
// File operations
//****************************************************************************

// file descriptor management structure
// Human68kから渡されるFCBのアドレスをキーとしてfdを管理する
typedef struct {
  uint32_t fcb;
  TYPE_FD fd;
  off_t pos;
} fdinfo_t;

static fdinfo_t *fi_store;
static int fi_size = 0;

// FCBに対応するバッファを探す
static fdinfo_t *fi_alloc(uint32_t fcb, bool alloc)
{
  for (int i = 0; i < fi_size; i++) {
    if (fi_store[i].fcb == fcb) {
      if (alloc) {              // 新規作成で同じFCBを見つけたらバッファを再利用
        FUNC_CLOSE(NULL, fi_store[i].fd);
        fi_store[i].fd = FD_BADFD;
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
  fi_store[fi_size - 1].fd = FD_BADFD;
  return &fi_store[fi_size - 1];
}

// 不要になったバッファを解放する
static void fi_free(uint32_t fcb)
{
  for (int i = 0; i < fi_size; i++) {
    if (fi_store[i].fcb == fcb) {
      fi_store[i].fcb = 0;
      fi_store[i].fd = FD_BADFD;
      return;
    }
  }
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

int op_create(uint8_t *cbuf, uint8_t *rbuf)
{
  struct cmd_create *cmd = (struct cmd_create *)cbuf;
  struct res_create *res = (struct res_create *)rbuf;
  hostpath_t path;
  TYPE_FD filefd;

  res->res = 0;

  if (conv_namebuf(&cmd->path, true, &path) < 0) {
    res->res = _DOSE_NODIR;
    goto errout;
  }

  int mode = O_CREAT|O_RDWR|O_TRUNC|O_BINARY;
  mode |= cmd->mode ? 0 : O_EXCL;
  int err;
  if ((filefd = FUNC_OPEN(&err, path, mode)) == FD_BADFD) {
    switch (err) {
    case ENOSPC:
      res->res = _DOSE_DIRFULL;
      break;
    default:
      res->res = conv_errno(err);
      break;
    }
  } else {
    fdinfo_t *fi = fi_alloc(cmd->fcb, true);
    fi->fd = filefd;
    fi->pos = 0;
  }
errout:
  DPRINTF1("CREATE: fcb=0x%08x attr=0x%02x mode=%d %s -> %d\n", cmd->fcb, cmd->attr, cmd->mode, path, res->res);
  return sizeof(*res);
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

int op_open(uint8_t *cbuf, uint8_t *rbuf)
{
  struct cmd_open *cmd = (struct cmd_open *)cbuf;
  struct res_open *res = (struct res_open *)rbuf;
  hostpath_t path;
  int mode;
  TYPE_FD filefd;

  res->res = 0;

  if (conv_namebuf(&cmd->path, true, &path) < 0) {
    res->res = _DOSE_NODIR;
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
    res->res = _DOSE_ILGARG;
    goto errout;
  }

  int err;
  if ((filefd = FUNC_OPEN(&err, path, mode)) == FD_BADFD) {
    switch (err) {
    case EINVAL:
      res->res = _DOSE_ILGARG;
      break;
    default:
      res->res = conv_errno(err);
      break;
    }
  } else {
    fdinfo_t *fi = fi_alloc(cmd->fcb, true);
    fi->fd = filefd;
    fi->pos = 0;
    uint32_t len = FUNC_LSEEK(NULL, filefd, 0, SEEK_END);
    FUNC_LSEEK(NULL, filefd, 0, SEEK_SET);
    res->size = htobe32(len);
  }
errout:
  DPRINTF1("OPEN: fcb=0x%08x mode=%d %s -> %d %d\n", cmd->fcb, cmd->mode, path, res->res, be32toh(res->size));
  return sizeof(*res);
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

int op_close(uint8_t *cbuf, uint8_t *rbuf)
{
  struct cmd_close *cmd = (struct cmd_close *)cbuf;
  struct res_close *res = (struct res_close *)rbuf;
  fdinfo_t *fi = fi_alloc(cmd->fcb, false);
  res->res = 0;

  if (!fi) {
    res->res = _DOSE_BADF;
    goto errout;
  }

  int err;
  if (FUNC_CLOSE(&err, fi->fd) < 0) {
    res->res = conv_errno(err);
  }

errout:
  fi_free(cmd->fcb);
  DPRINTF1("CLOSE: fcb=0x%08x\n", cmd->fcb);
  return sizeof(*res);
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

int op_read(uint8_t *cbuf, uint8_t *rbuf)
{
  struct cmd_read *cmd = (struct cmd_read *)cbuf;
  struct res_read *res = (struct res_read *)rbuf;
  fdinfo_t *fi = fi_alloc(cmd->fcb, false);
  uint32_t pos = be32toh(cmd->pos);
  size_t len = be16toh(cmd->len);
  ssize_t bytes = 0;

  if (!fi) {
    res->len = _DOSE_BADF;
    goto errout;
  }

  int err;
  if (fi->pos != pos) {
    if (FUNC_LSEEK(&err, fi->fd, pos, SEEK_SET) < 0) {
      res->len = htobe32(conv_errno(err));
      goto errout;
    }
  }
  bytes = FUNC_READ(&err, fi->fd, res->data, len);
  if (bytes < 0) {
    res->len = htobe16(conv_errno(err));
    bytes = 0;
  } else {
    res->len = htobe16(bytes);
    fi->pos += bytes;
  }

errout:
  DPRINTF1("READ: fcb=0x%08x %d %d -> %d\n", cmd->fcb, pos, len, bytes);
  return offsetof(struct res_read, data) + bytes;
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

int op_write(uint8_t *cbuf, uint8_t *rbuf)
{
  struct cmd_write *cmd = (struct cmd_write *)cbuf;
  struct res_write *res = (struct res_write *)rbuf;
  fdinfo_t *fi = fi_alloc(cmd->fcb, false);
  uint32_t pos = be32toh(cmd->pos);
  size_t len = be16toh(cmd->len);
  ssize_t bytes;

  if (!fi) {
    res->len = _DOSE_BADF;
    goto errout;
  }

  int err;
  if (len == 0) {     // 0バイトのwriteはファイル長を切り詰める
    if (FUNC_FTRUNCATE(&err, fi->fd, pos) < 0) {
      res->len = htobe16(conv_errno(err));
    } else {
      res->len = 0;
    }
  } else {
    if (fi->pos != pos) {
      if (FUNC_LSEEK(&err, fi->fd, pos, SEEK_SET) < 0) {
        res->len = htobe32(conv_errno(err));
        goto errout;
      }
    }
    bytes = FUNC_WRITE(&err, fi->fd, cmd->data, len);
    if (bytes < 0) {
      res->len = htobe16(conv_errno(err));
    } else {
      res->len = htobe16(bytes);
      fi->pos += bytes;
    }
  }

errout:
  DPRINTF1("WRITE: fcb=0x%08x %d %d -> %d\n", cmd->fcb, pos, len, bytes);
  return sizeof(*res);
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

int op_filedate(uint8_t *cbuf, uint8_t *rbuf)
{
  struct cmd_filedate *cmd = (struct cmd_filedate *)cbuf;
  struct res_filedate *res = (struct res_filedate *)rbuf;
  fdinfo_t *fi = fi_alloc(cmd->fcb, false);

  if (!fi) {
    res->date = 0xffff;
    res->time = _DOSE_BADF;
    goto errout;
  }

  int err;
  if (cmd->time == 0 && cmd->date == 0) {   // 更新日時取得
    TYPE_STAT st;
    if (FUNC_FSTAT(&err, fi->fd, &st) < 0) {
      res->date = 0xffff;
      res->time = htobe32(conv_errno(err));
    } else {
      struct dos_filesinfo fi;
      conv_statinfo(&st, &fi);
      res->time = fi.time;
      res->date = fi.date;
    }
  } else {                                  // 更新日時設定
    uint16_t time = be16toh(cmd->time);
    uint16_t date = be16toh(cmd->date);
    if (FUNC_FILEDATE(&err, fi->fd, time, date) < 0) {
      res->date = 0xffff;
      res->time = htobe32(conv_errno(err));
    } else {
      res->date = 0;
      res->time = 0;
    }
  }

errout:
  DPRINTF1("FILEDATE: fcb=0x%08x 0x%04x 0x%04x -> 0x%04x 0x%04x\n", cmd->fcb, be16toh(cmd->date), be16toh(cmd->time), be16toh(res->date), be16toh(res->time));
  return sizeof(*res);
}

//****************************************************************************
// Misc functions
//****************************************************************************

int op_dskfre(uint8_t *cbuf, uint8_t *rbuf)
{
  struct cmd_dskfre *cmd = (struct cmd_dskfre *)cbuf;
  struct res_dskfre *res = (struct res_dskfre *)rbuf;
  uint64_t total;
  uint64_t free;

  res->res = -1;

  FUNC_STATFS(NULL, rootpath, &total, &free);
  total = total > 0x7fffffff ? 0x7fffffff : total;
  free = free > 0x7fffffff ? 0x7fffffff : free;
  res->freeclu = htobe16(free / 32768);
  res->totalclu = htobe16(total /32768);
  res->clusect = htobe16(128);
  res->sectsize = htobe16(1024);
  res->res = htobe32(free);

  DPRINTF1("DSKFRE: free=%u total=%u clusect=%u sectsz=%u res=%d\n", be16toh(res->freeclu), be16toh(res->totalclu), be16toh(res->clusect), be16toh(res->sectsize), be32toh(res->res));
  return sizeof(*res);
}

//****************************************************************************
// main
//****************************************************************************

int remote_serv(uint8_t *cbuf, uint8_t *rbuf)
{
  printf("----Command: 0x%02x\n", cbuf[0]);
  int rsize = -1;

  switch (cbuf[0]) {
  case 0x40: /* check */
    rsize = op_check(cbuf, rbuf);
    break;
  case 0x41: /* chdir */
    rsize = op_chdir(cbuf, rbuf);
    break;
  case 0x42: /* mkdir */
    rsize = op_mkdir(cbuf, rbuf);
    break;
  case 0x43: /* rmdir */
    rsize = op_rmdir(cbuf, rbuf);
    break;
  case 0x44: /* rename */
    rsize = op_rename(cbuf, rbuf);
    break;
  case 0x45: /* remove */
    rsize = op_delete(cbuf, rbuf);
    break;
  case 0x46: /* chmod */
    rsize = op_chmod(cbuf, rbuf);
    break;
  case 0x47: /* files */
    rsize = op_files(cbuf, rbuf);
    break;
  case 0x48: /* nfiles */
    rsize = op_nfiles(cbuf, rbuf);
    break;
  case 0x49: /* create */
    rsize = op_create(cbuf, rbuf);
    break;
  case 0x4a: /* open */
    rsize = op_open(cbuf, rbuf);
    break;
  case 0x4b: /* close */
    rsize = op_close(cbuf, rbuf);
    break;
  case 0x4c: /* read */
    rsize = op_read(cbuf, rbuf);
    break;
  case 0x4d: /* write */
    rsize = op_write(cbuf, rbuf);
    break;
  case 0x4f: /* filedate */
    rsize = op_filedate(cbuf, rbuf);
    break;
  case 0x50: /* dskfre */
    rsize = op_dskfre(cbuf, rbuf);
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
    DPRINTF1("error: %02x\n", cbuf[0]);
  }

  return rsize;
}
