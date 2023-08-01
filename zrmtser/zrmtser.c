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

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
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

#ifndef O_BINARY
#define O_BINARY 0
#endif

#ifdef WINNT
static __inline unsigned short
__bswap_16 (unsigned short __x)
{
  return (__x >> 8) | (__x << 8);
}
static __inline unsigned int
__bswap_32 (unsigned int __x)
{
  return (__bswap_16 (__x & 0xffff) << 16) | (__bswap_16 (__x >> 16));
}
#define htobe16(x) __bswap_16(x)
#define htobe32(x) __bswap_32(x)
#define be16toh(x) __bswap_16(x)
#define be32toh(x) __bswap_32(x)
#endif

#define SERIAL_PORT "/dev/ttyS8"
//#define SERIAL_PORT "/dev/ttyS2"

/****************************************************************************/
/* Communication                                                            */
/****************************************************************************/

void serout(int fd, void *buf, size_t len)
{
    uint8_t lenbuf[2];
    write(fd, "ZZZX", 4);
    lenbuf[0] = len >> 8;
    lenbuf[1] = len & 0xff;
    write(fd, lenbuf, 2);
    write(fd, buf, len);

#if 0
    for (int i = 0; i < len; i++)
    {
        printf("%02X ", ((uint8_t *)buf)[i]);
    }
    printf("\n");
#endif

    printf("send %d bytes\n", len);
}

void serin(int fd, void *buf, size_t len)
{
  uint8_t *p = buf;
  uint8_t c;
  size_t size;
  int i;

  do { 
    read(fd, &c, 1);
  } while (c != 'Z');
  do {
    read(fd, &c, 1);
  } while (c == 'Z');
  if (c != 'X') {
    printf("error %02\n", c);
    return;
  }

  read(fd, p, 1);
  read(fd, p + 1, 1);
  size = p[0] * 256 + p[1];
  printf("receive size=%d %d\n", size, len);

  int s = size;
  i = 0;
  while (s > 0) {
    len = read(fd, p + i, s);
    s -= len;
    i += len;
  }
}

/****************************************************************************/


typedef char HfsPathName[256];  // ホストパス名
typedef uint8_t HfsDosName[23]; // SJISファイル名 (8+10+1+3+1)


/****************************************************************************/

//char *hfuRootPath = "/c";
char *hfuRootPath = ".";


//hfuFileInfo (file, b)
//  ファイルの情報の取得(ホスト側)
//  属性、時刻、日付、ファイルサイズを読み取る
//  ファイル名は設定済み
//    b[0]     属性。eladvshr
//    b[1..2]  時刻。時<<11|分<<5|秒/2
//    b[3..4]  日付。(西暦年-1980)<<9|月<<5|月通日
//    b[5..8]  ファイルサイズ
//    b[9..31]  ファイル名
void hfuFileInfo(struct stat *st, HfsFilesinfo *f)
{
  struct tm *tm;

  //属性
  f->atr = (st->st_mode & S_IFREG ? 0x20 : 0) |
           (st->st_mode & S_IFDIR ? 0x10 : 0);
           // TBD hidden, readonly
  //更新日時
  tm = localtime(&st->st_mtime);
  //時刻
  int time = tm->tm_hour << 11 | tm->tm_min << 5 | tm->tm_sec >> 1;  //時<<11|分<<5|秒/2
  f->time = htobe16(time);
  //日付
  int date = (tm->tm_year - 80) << 9 | (tm->tm_mon + 1) << 5 | tm->tm_mday;  //(西暦年-1980)<<9|月<<5|月通日
  f->date = htobe16(date);
  //ファイルサイズ
  f->filelen = htobe32(st->st_size);
}

int hfuNamestsToPath(HfsNamests *ns, int full, HfsPathName *path)
{
  uint8_t bb[88];   // SJISでのパス名
  int k = 0;

  for (int i = 0; i < 65; ) {
    for (; i < 65 && ns->path[i] == 0x09; i++) {    //0x09の並びを読み飛ばす
    }
    if (i >= 65 || ns->path[i] == 0x00) {   //ディレクトリ名がなかった
      break;
    }
    bb[k++] = 0x2f;  //ディレクトリ名の手前の'/'
    for (; i < 65 && ns->path[i] != 0x00 && ns->path[i] != 0x09; i++) {
      bb[k++] = ns->path[i];  //ディレクトリ名
    }
  }
  if (full) {  //主ファイル名を展開する
    bb[k++] = 0x2f;  //主ファイル名の手前の'/'
    for (int i = 0; i < 8; i++) {
      bb[k++] = ns->name1[i];  //主ファイル名1
    }
    for (int i = 0; i < 10; i++) {
      bb[k++] = ns->name2[i];  //主ファイル名2
    }
    for (; k > 0 && bb[k - 1] == 0x00; k--) {  //主ファイル名2の末尾の0x00を切り捨てる
    }
    for (; k > 0 && bb[k - 1] == 0x20; k--) {  //主ファイル名1の末尾の0x20を切り捨てる
    }
    bb[k++] = 0x2e;  //拡張子の手前の'.'
    for (int i = 0; i < 3; i++) {
      bb[k++] = ns->ext[i];  //拡張子
    }
    for (; k > 0 && bb[k - 1] == 0x20; k--) {  //拡張子の末尾の0x20を切り捨てる
    }
    for (; k > 0 && bb[k - 1] == 0x2e; k--) {  //主ファイル名の末尾の0x2eを切り捨てる
    }
  }
  // SJIS -> UTF-8に変換
  char *dst_buf = (char *)path;
  strncpy(dst_buf, hfuRootPath, sizeof(*path) - 1);
  dst_buf += strlen(hfuRootPath);
  size_t dst_len = sizeof(*path) - 1 - strlen(hfuRootPath);
  char *src_buf = bb;
  size_t src_len = k;
#ifndef WINNT
  iconv_t cd = iconv_open("UTF-8", "CP932");
  int res = iconv(cd, &src_buf, &src_len, &dst_buf, &dst_len);
  iconv_close(cd);
#else
    memcpy(dst_buf, src_buf, src_len);
    dst_buf += src_len;
    src_buf += src_len;
    int res = 0;
#endif
  if (res < 0) {
    return -1;  //変換できなかった
  }
  *dst_buf = '\0';
  return 0;
}

/****************************************************************************/
/* Filesystem operations                                                    */
/****************************************************************************/

void hfuCallChdir(int fd, char *buf)
{
  struct dirop *f = (struct dirop *)buf;
  struct dirop_res fr;
  HfsPathName path;
  struct stat st;
  int res;

  res = hfuNamestsToPath(&f->path, false, &path);
  if (res < 0) {
    fr.res = DOS_DIRECTORY_NOT_FOUND;
    serout(fd, &fr, sizeof(fr));
    return;
  }

  res = stat(path, &st);
  if (res != 0 || (st.st_mode & S_IFDIR) == 0) {
    fr.res = DOS_DIRECTORY_NOT_FOUND;
  } else {
    fr.res = 0;
  }
  serout(fd, &fr, sizeof(fr));
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void hfuCallMkdir(int fd, char *buf)
{
  struct dirop *f = (struct dirop *)buf;
  struct dirop_res fr;
  HfsPathName path;
  int res;

  res = hfuNamestsToPath(&f->path, true, &path);
  if (res < 0) {
    fr.res = DOS_DIRECTORY_NOT_FOUND;
    serout(fd, &fr, sizeof(fr));
    return;
  }

#ifndef WINNT
  res = mkdir(path, 0777);
#else
  res = mkdir(path);
#endif
  if (res != 0) {
    fr.res = DOS_DIRECTORY_EXISTS;
  } else {
    fr.res = 0;
  }
  serout(fd, &fr, sizeof(fr));
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void hfuCallRmdir(int fd, char *buf)
{
  struct dirop *f = (struct dirop *)buf;
  struct dirop_res fr;
  HfsPathName path;
  int res;

  res = hfuNamestsToPath(&f->path, true, &path);
  if (res < 0) {
    fr.res = DOS_DIRECTORY_NOT_FOUND;
    serout(fd, &fr, sizeof(fr));
    return;
  }

  res = rmdir(path);
  if (res < 0) {
    switch (errno) {
    case ENOTDIR:
      fr.res = DOS_DIRECTORY_NOT_FOUND;
      break;
    case ENOTEMPTY:
      fr.res = DOS_RM_NONEMPTY_DIRECTORY;
      break;
    default:
      fr.res = DOS_CANNOT_WRITE;
      break;
    }
  } else {
    fr.res = 0;
  }
  serout(fd, &fr, sizeof(fr));
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void hfuCallRename(int fd, char *buf)
{
  struct rename *f = (struct rename *)buf;
  struct rename_res fr;
  HfsPathName pathold;
  HfsPathName pathnew;
  int res;

  res = hfuNamestsToPath(&f->pathOld, true, &pathold);
  if (res < 0) {
    fr.res = DOS_DIRECTORY_NOT_FOUND;
    serout(fd, &fr, sizeof(fr));
    return;
  }
  res = hfuNamestsToPath(&f->pathNew, true, &pathnew);
  if (res < 0) {
    fr.res = DOS_DIRECTORY_NOT_FOUND;
    serout(fd, &fr, sizeof(fr));
    return;
  }

  res = rename(pathold, pathnew);
  if (res < 0) {
    switch (errno) {
    case ENOENT:
      fr.res = DOS_FILE_NOT_FOUND;
      break;
    case EBUSY:
      fr.res = DOS_MV_NONEMPTY_DIRECTORY;
      break;
    default:
      fr.res = DOS_CANNOT_WRITE;
      break;
    }
  } else {
    fr.res = 0;
  }
  serout(fd, &fr, sizeof(fr));
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void hfuCallDelete(int fd, char *buf)
{
  struct dirop *f = (struct dirop *)buf;
  struct dirop_res fr;
  HfsPathName path;
  int res;

  res = hfuNamestsToPath(&f->path, true, &path);
  if (res < 0) {
    fr.res = DOS_DIRECTORY_NOT_FOUND;
    serout(fd, &fr, sizeof(fr));
    return;
  }

  res = unlink(path);
  if (res < 0) {
    switch (errno) {
    case ENOTDIR:
      fr.res = DOS_DIRECTORY_NOT_FOUND;
      break;
    case ENOENT:
      fr.res = DOS_FILE_NOT_FOUND;
      break;
    default:
      fr.res = DOS_CANNOT_WRITE;
      break;
    }
  } else {
    fr.res = 0;
  }
  serout(fd, &fr, sizeof(fr));
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void hfuCallChmod(int fd, char *buf)
{
  struct chmod *f = (struct chmod *)buf;
  struct chmod_res fr;
  HfsPathName path;
  struct stat st;
  int res;

  res = hfuNamestsToPath(&f->path, true, &path);
  if (res < 0) {
    fr.res = DOS_DIRECTORY_NOT_FOUND;
    serout(fd, &fr, sizeof(fr));
    return;
  }

  printf("Chmod: %s 0x%02x\n", path, f->attr);

  res = stat(path, &st);
  if (res < 0) {
    switch (errno) {
    case ENOENT:
    case ENOTDIR:
      fr.res = DOS_FILE_NOT_FOUND;
      break;
    default:
      fr.res = DOS_CANNOT_WRITE;
      break;
    }
  } else if (st.st_mode & S_IFDIR) {
    fr.res = 0x10;
  } else {
    fr.res = 0x20;
  }
  serout(fd, &fr, sizeof(fr));
}

/****************************************************************************/
/* Directory operations                                                     */
/****************************************************************************/

typedef struct {
  uint32_t files;
  HfsFilesinfo *dirbuf;
  int bufsize;
  int bufcnt;
} HfsDirList;

HfsDirList *dl;
int dlsize = 0;

HfsDirList *hfuGetDirList(uint32_t files, bool create)
{
  for (int i = 0; i < dlsize; i++) {
    if (dl[i].files == files) {
      if (create) {
        free(dl[i].dirbuf);
        dl[i].dirbuf = NULL;
        dl[i].bufsize = 0;
        dl[i].bufcnt = 0;
      }
      return &dl[i];
    }
  }
  if (!create)
    return NULL;
  for (int i = 0; i < dlsize; i++) {
    if (dl[i].files == 0) {
      dl[i].files = files;
      return &dl[i];
    }
  }
  dlsize++;
  dl = realloc(dl, sizeof(HfsDirList) * dlsize);
  dl[dlsize -1].files = files;
  dl[dlsize -1].dirbuf = NULL;
  dl[dlsize -1].bufsize = 0;
  dl[dlsize -1].bufcnt = 0;
  return &dl[dlsize -1];
}
void hfuFreeDirList(uint32_t files)
{
  for (int i = 0; i < dlsize; i++) {
    if (dl[i].files == files) {
      dl[i].files = 0;
      free(dl[i].dirbuf);
      dl[i].dirbuf = NULL;
      dl[i].bufsize = 0;
      dl[i].bufcnt = 0;
      return;
    }
  }
}

void hfuCallFiles(int fd, char *buf)
{
  struct files *f = (struct files *)buf;
  struct files_res fr;
  HfsPathName path;
  DIR *dir;
  struct dirent *d;
  int res;
  HfsDirList *dl;

  fr.res = 0;

#if 1 // TBD
  if (f->attr == 0x08)
  {
    fr.file.atr = 0x08;
    strcpy(fr.file.name, "Human68k");
    serout(fd, &fr, sizeof(fr));
    return;
  }
#endif

  res = hfuNamestsToPath(&f->path, false, &path);
  if (res < 0) {
    fr.res = DOS_DIRECTORY_NOT_FOUND;
    serout(fd, &fr, sizeof(fr));
    return;
  }


  //検索するファイル名の順序を入れ替える
  //  主ファイル名1の末尾が'?'で主ファイル名2の先頭が'\0'のときは主ファイル名2を'?'で充填する
  //    1234567?.Xと1234567*.Xが同じになってしまうのは仕様
  //    TwentyOne.x +Tのときは*.*で主ファイル名2も'?'で充填されている
  HfsDosName w;
  memset(w, 0, sizeof(HfsDosName));
  memcpy(&w[0], f->path.name1, 8);    //主ファイル名1
  if (f->path.name1[7] == '?' && f->path.name2[0] == '\0') {  //主ファイル名1の末尾が'?'で主ファイル名2の先頭が'\0'
    memset(&w[8], '?', 10);           //主ファイル名2
  } else {
    memcpy(&w[8], f->path.name2, 10); //主ファイル名2
  }
  for (int i = 17; i >= 0 && (w[i] == '\0' || w[i] == ' '); i--) {  //主ファイル名1+主ファイル名2の空き
    w[i] = '\0';
  }
  memcpy(&w[18], f->path.ext, 3);     //拡張子
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

  for (int i = 0; i < 21; i++) putchar(w[i] == 0 ? '_' : w[i]);
  printf("\n");




  //検索するディレクトリの一覧を取得する
  dir = opendir(path);
  if (dir == NULL) {
    fr.res = DOS_DIRECTORY_NOT_FOUND;
    serout(fd, &fr, sizeof(fr));
    return;
  }

  dl = hfuGetDirList(f->filep, true);

#ifndef WINNT
  iconv_t cd = iconv_open("CP932", "UTF-8");
#endif

  //ディレクトリの一覧から属性とファイル名の条件に合うものを選ぶ
  while (d = readdir(dir)) {
    char *childName = d->d_name;

//    printf("%s\n", childName);

    //ルートディレクトリの処理
//    if (isRoot) {  //ルートディレクトリのとき
    if (true) {  //ルートディレクトリのとき
      if (strcmp(childName, ".") == 0 || strcmp(childName, "..") == 0) {  //.と..を除く
        continue;
      }
    }
    //ファイル名をSJISに変換する
    //  ソース
    //    childName
    //  デスティネーション
    //    b[9..31]  ファイル名(主ファイル名+'.'+拡張子+'\0')
    HfsDosName b;
    char *dst_buf = (char *)b;
    size_t dst_len = sizeof(b) - 1;
    char *src_buf = childName;
    size_t src_len = strlen(childName);
#ifndef WINNT
    int res = iconv(cd, &src_buf, &src_len, &dst_buf, &dst_len);
#else
    if (src_len > dst_len) {
      res = -1;
    } else {
      memcpy(dst_buf, src_buf, src_len);
      dst_buf += src_len;
      src_buf += src_len;
      res = 0;
    }
#endif
    if (res < 0) {
      continue;  //変換できなかった
    }
    *dst_buf = '\0';
    {
      int i;
      int c;
      for (i = 0; c = b[i]; i++) {
        if (c <= 0x1f ||  //変換できない文字または制御コード
            (c == '-' && i == 0) ||  //ファイル名の先頭に使えない文字
            strchr("/\\,;<=>[]|", c) != NULL) {  //ファイル名に使えない文字
          break;
        }
      }
      if (c) {
        continue;
      }
    }
    strcpy(fr.file.name, b);


    //ファイル名を分解する
    int k = strlen(b);
    HfsDosName w2;
    int m = (b[k - 1] == '.' ? k :  //name.
             k >= 3 && b[k - 2] == '.' ? k - 2 :  //name.e
             k >= 4 && b[k - 3] == '.' ? k - 3 :  //name.ex
             k >= 5 && b[k - 4] == '.' ? k - 4 :  //name.ext
             k);  //主ファイル名の直後。拡張子があるときは'.'の位置、ないときはk
    if (m > 18) {  //主ファイル名が長すぎる
      continue;
    }
    {
      int i = 0;
      memset(w2, 0, sizeof(HfsDosName));
      memcpy(&w2[0], &b[0], m);         //主ファイル名
      if (b[m] == '.')
        strncpy(&w2[18], &b[m + 1], 3); //拡張子
    }

{
  for (int i = 0; i < 21; i++) putchar(w2[i] == 0 ? '_' : w2[i]);
  printf("\n");
}

    //ファイル名を比較する
    //  ソース
    //    w[0..20]  ファイル名
    //      w[0..7]  主ファイル名1。残りは'\0'
    //      w[8..17]  主ファイル名2。残りは'\0'
    //      w[18..20]  拡張子。残りは'\0'
    //  デスティネーション
    //    w[21..41]  検索するファイル名
    //      w[21..28]  主ファイル名1。残りは'\0'
    //      w[29..38]  主ファイル名2。残りは'\0'
    //      w[39..41]  拡張子。残りは'\0'
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
      if (i < 20)
        continue;
    }

    char fullPath[512];
    strcpy(fullPath, path);
    strcat(fullPath, "/");
    strcat(fullPath, childName);

//    printf("full: %s\n", fullPath);

    struct stat st;

    //属性、時刻、日付、ファイルサイズを取得する
    if (stat(fullPath, &st) < 0) {
      continue; // ファイル情報を取得できなかった
    }
    if (0xffffffffL < st.st_size) {  //4GB以上のファイルは検索できないことにする
//      st.st_size = 0xffffffffL;
      continue;
    }
    hfuFileInfo(&st, &fr.file);

    dl->dirbuf = realloc(dl->dirbuf, sizeof(fr.file) * (dl->bufsize + 1));
    memcpy(&dl->dirbuf[dl->bufsize], &fr.file, sizeof(fr.file));
    dl->bufsize++;

#if 0
    if (isHumansys) {  //HUMAN.SYSにシステム属性を追加する
      b[0] |= HumanMedia.HUM_SYSTEM;
    }
    if (HFS_DEBUG_FILE_INFO) {
      System.out.print ("FILES   ");
      hfuPrintFileInfo (b);
    }
    if ((b[0] & hfsRequest13Mode) == 0) {  //属性がマッチしない
      continue childrenLoop;
    }
    //リストに追加する
    deque.addLast (b);
  }
#endif

#if 0
{
  for (int i = 0; i < 21; i++) putchar(w2[i] == 0 ? '_' : w2[i]);
  printf("\n");
}
#endif
  }

  closedir(dir);
#ifndef WINNT
  iconv_close(cd);
#endif

  if (d == NULL)
    fr.res = DOS_NO_MORE_FILES;


  for (int i = 0; i < dl->bufsize; i++) {
    printf("%d %s\n", i, dl->dirbuf[i].name);
  }

  if (dl->bufcnt < dl->bufsize) {
    printf("Files: 0x%08x %d %d\t", f->filep, dl->bufcnt, dl->bufsize);
    memcpy(&fr.file, &dl->dirbuf[dl->bufcnt], sizeof(fr.file));
    printf("%s\n", fr.file.name);
    dl->bufcnt++;
    fr.res = 0;
  }
  if (dl->bufcnt == dl->bufsize) {
    hfuFreeDirList(f->filep);
  }

  serout(fd, &fr, sizeof(fr));
}

/****************************************************************************/
/* File operations                                                          */
/****************************************************************************/

void hfuCallNfiles(int fd, char *buf)
{
  struct nfiles *f = (struct nfiles *)buf;
  struct nfiles_res fr;
  HfsDirList *dl;

  fr.res = DOS_NO_MORE_FILES;
  dl = hfuGetDirList(f->filep, false);

  if (dl) {
    printf("Nfiles: 0x%08x %d %d\t", f->filep, dl->bufcnt, dl->bufsize);
    memcpy(&fr.file, &dl->dirbuf[dl->bufcnt], sizeof(fr.file));
    printf("%s\n", fr.file.name);
    dl->bufcnt++;
    fr.res = 0;

    if (dl->bufcnt == dl->bufsize) {
      hfuFreeDirList(f->filep);
    }
  } else {
    printf("Nfiles: 0x%08x END\n", f->filep);
  }

  serout(fd, &fr, sizeof(fr));
}

/****************************************************************************/
/* File operations                                                          */
/****************************************************************************/

typedef struct {
  uint32_t fcb;
  int fd;
} HfsFdInfo;

HfsFdInfo *fl;
int flsize = 0;

HfsFdInfo *hfuGetFdInfo(uint32_t fcb, bool alloc)
{
  for (int i = 0; i < flsize; i++) {
    if (fl[i].fcb == fcb) {
      if (alloc) {
        close(fl[i].fd);
        fl[i].fd = -1;
      }
      return &fl[i];
    }
  }
  if (!alloc)
    return NULL;
  for (int i = 0; i < flsize; i++) {
    if (fl[i].fcb == 0) {
      fl[i].fcb = fcb;
      return &fl[i];
    }
  }
  flsize++;
  fl = realloc(fl, sizeof(HfsFdInfo) * flsize);
  fl[flsize - 1].fcb = fcb;
  fl[flsize - 1].fd = -1;
  return &fl[flsize - 1];
}
void hfuFreeFdInfo(uint32_t fcb)
{
  for (int i = 0; i < flsize; i++) {
    if (fl[i].fcb == fcb) {
      fl[i].fcb = 0;
      fl[i].fd = -1;
      return;
    }
  }
}

void hfuCallCreate(int fd, char *buf)
{
  struct create *f = (struct create *)buf;
  struct create_res fr;
  HfsPathName path;
  HfsFdInfo *fl;
  int filefd;
  int res;

  res = hfuNamestsToPath(&f->path, true, &path);
  if (res < 0) {
    fr.res = DOS_DIRECTORY_NOT_FOUND;
    serout(fd, &fr, sizeof(fr));
    return;
  }

  printf("Create: fcb:0x%x attr:0x%02x mode:%d %s\n", f->fcbp, f->attr, f->mode, path);

  int mode = O_CREAT|O_RDWR|O_TRUNC|O_BINARY;
  mode |= f->mode ? 0 : O_EXCL;
  filefd = open(path, mode, 0777);
  if (filefd < 0) {
    fr.res = DOS_FILE_NOT_FOUND;
  } else {
    fl = hfuGetFdInfo(f->fcbp, true);
    fl->fd = filefd;
    printf("fd=%d\n", filefd);
    *(uint32_t *)&f->fcb[64] = 0;
    memcpy(fr.fcb, f->fcb, 68);
    fr.res = 0;
  }

#if 0
  for (int i = 0; i < sizeof(f->fcb); i++) {
    if ((i % 16) == 0) printf("%02x: ", i);
    printf("%02x ", ((uint8_t *)f->fcb)[i]);
    if ((i % 16) == 15) printf("\n");
  }
  printf("\n");
#endif

  serout(fd, &fr, sizeof(fr));
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void hfuCallOpen(int fd, char *buf)
{
  struct open *f = (struct open *)buf;
  struct open_res fr;
  HfsPathName path;
  int mode;
  HfsFdInfo *fl;
  int filefd;
  int res;

  res = hfuNamestsToPath(&f->path, true, &path);
  if (res < 0) {
    fr.res = DOS_DIRECTORY_NOT_FOUND;
    serout(fd, &fr, sizeof(fr));
    return;
  }
  mode = f->fcb[14];

  printf("Open: fcb:0x%x mode:%d %s\n", f->fcbp, mode, path);

  filefd = open(path, (mode == 0 ? O_RDONLY : (mode == 1 ? O_WRONLY : O_RDWR))|O_BINARY);
  if (filefd < 0) {
    fr.res = DOS_FILE_NOT_FOUND;
  } else {
    fl = hfuGetFdInfo(f->fcbp, true);
    fl->fd = filefd;
    printf("fd=%d\n", filefd);
    uint32_t len = lseek(filefd, 0, SEEK_END);
    printf("len=%d\n", len);
    lseek(filefd, 0, SEEK_SET);
    *(uint32_t *)&f->fcb[64] = htobe32(len);
    memcpy(fr.fcb, f->fcb, 68);
    fr.res = 0;
  }

#if 0
  for (int i = 0; i < sizeof(f->fcb); i++) {
    if ((i % 16) == 0) printf("%02x: ", i);
    printf("%02x ", ((uint8_t *)f->fcb)[i]);
    if ((i % 16) == 15) printf("\n");
  }
  printf("\n");
#endif

  serout(fd, &fr, sizeof(fr));
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void hfuCallClose(int fd, char *buf)
{
  struct close *f = (struct close *)buf;
  struct close_res fr;
  HfsFdInfo *fl;
  int filefd;
  int res;

  printf("Close: fcb:0x%x\n", f->fcb);
  fl = hfuGetFdInfo(f->fcb, false);
  filefd = fl->fd;
  printf("fd=%d\n", filefd);
  hfuFreeFdInfo(f->fcb);
  close(filefd);

  fr.res = 0;
  serout(fd, &fr, sizeof(fr));
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void hfuCallRead(int fd, char *buf)
{
  struct read *f = (struct read *)buf;
  struct read_res fr;
  HfsFdInfo *fl;
  int filefd;
  int res;

  size_t len = be32toh(f->len);
  printf("Read: fcb:0x%x len:%d\n", f->fcb, len);
  fl = hfuGetFdInfo(f->fcb, false);
  filefd = fl->fd;
  printf("fd=%d\n", filefd);

  size_t size = 0;

  while (1) {
    int bytes = len > sizeof(fr.data) ? sizeof(fr.data): len;
    bytes = read(filefd, fr.data, bytes);
    fr.len = htobe16(bytes);
    len -= bytes;
    size += bytes;
    printf("read %d %d %d\n", bytes, len, size);
    serout(fd, &fr, bytes + 2);
    if (bytes <= 0)
      break;
    serin(fd, f, sizeof(*f));
  }
  printf("size=%d\n", size);
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void hfuCallWrite(int fd, char *buf)
{
  struct write *f = (struct write *)buf;
  struct write1 f1;
  struct write_res fr;
  HfsFdInfo *fl;
  int filefd;
  int res;

  size_t len = be32toh(f->len);
  printf("Write: fcb:0x%x len:%d\n", f->fcb, len);
  fl = hfuGetFdInfo(f->fcb, false);
  filefd = fl->fd;
  printf("fd=%d\n", filefd);

  size_t size = 0;

  fr.len = htobe16(len != 0);
  serout(fd, &fr, sizeof(fr));
  while (len > 0 && fr.len > 0) {
    serin(fd, &f1, sizeof(f1));
    int bytes = write(filefd, f1.data, be16toh(f1.len));
    len -= bytes;
    size += bytes;
    printf("write %d %d %d\n", bytes, len, size);
    fr.len = htobe16(bytes);
    serout(fd, &fr, sizeof(fr));
  }
  printf("size=%d\n", size);
  if (size == 0) {
    ftruncate(filefd, lseek(filefd, 0, SEEK_CUR));
  }
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void hfuCallSeek(int fd, char *buf)
{
  struct seek *f = (struct seek *)buf;
  struct seek_res fr;
  HfsFdInfo *fl;
  int filefd;
  off_t res;

  printf("Seek: fcb:0x%x offset:%d whence:%d\n", f->fcb, be32toh(f->offset), f->whence);
  fl = hfuGetFdInfo(f->fcb, false);
  filefd = fl->fd;
  printf("fd=%d\n", filefd);

  res = lseek(filefd, (int32_t)be32toh(f->offset), f->whence);

  fr.pos = htobe32(res);
  fr.res = 0;
  serout(fd, &fr, sizeof(fr));
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

void hfuCallFiledate(int fd, char *buf)
{
  struct filedate *f = (struct filedate *)buf;
  struct filedate_res fr;
  HfsFdInfo *fl;
  int filefd;
  off_t res;

  printf("Filedate: fcb:0x%x date:0x%04x time:0x%04x\n", f->fcb, be16toh(f->date), be16toh(f->time));
  fl = hfuGetFdInfo(f->fcb, false);
  filefd = fl->fd;
  printf("fd=%d\n", filefd);

  if (f->time == 0 && f->date == 0) {
    struct stat st;
    if (fstat(filefd, &st) < 0) {
      fr.time = fr.date = 0xffff;
    } else {
      HfsFilesinfo fi;
      hfuFileInfo(&st, &fi);
      fr.time = fi.time;
      fr.date = fi.date;
    }
  } else {
    uint16_t time = be16toh(f->time);
    uint16_t date = be16toh(f->date);
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
    fr.time = 0;
    fr.date = 0;
  }
  serout(fd, &fr, sizeof(fr));
}

/****************************************************************************/
/* Misc functions                                                           */
/****************************************************************************/

void hfuCallDskfre(int fd, char *buf)
{
  struct dskfre *f = (struct dskfre *)buf;
  struct dskfre_res fr;
  off_t res;

#ifndef WINNT
  struct statfs sf;
  statfs(hfuRootPath, &sf);
  printf("Dskfre: bs=%d all=%d free=%d\n", sf.f_bsize, sf.f_blocks, sf.f_bfree);
  uint64_t total = sf.f_blocks * sf.f_bsize;
  uint64_t free = sf.f_bfree * sf.f_bsize;
  fr.freeclu = 32767 * free / total;
#else
  fr.freeclu = 0;
#endif
  fr.totalclu = htobe16(32767);
  fr.clusect = htobe16(16);
  fr.sectsize = htobe16(1024);
  fr.res = htobe32(fr.freeclu);
  serout(fd, &fr, sizeof(fr));
}


/****************************************************************************/

int main(int argc, char **argv)
{
  unsigned char buf[1024]; // バッファ
  int fd;                  // ファイルディスクリプタ
#ifndef WINNT
  struct termios tio;      // シリアル通信設定
  int baudRate = B38400;
#else
  HANDLE hComm;
#endif
  int i;
  int len;
  int ret;
  int size;

  DIR *dir;
  char basedir[256];
  int isroot;

  if (argc < 2) {
    printf("Usage: %s <COM port> [<base directory>]\n", argv[0]);
    return 1;
  }
  if (argc > 2) {
    hfuRootPath = argv[2];
  }

//  fd = open(SERIAL_PORT, O_RDWR);
//  fd = open("COM9", O_RDWR|O_BINARY);
  fd = open(argv[1], O_RDWR|O_BINARY);
  if (fd < 0)
  {
      printf("COM port open error\n");
      return -1;
  }

#ifndef WINNT
  tcgetattr(fd, &tio);
  cfsetispeed(&tio, baudRate);
  cfsetospeed(&tio, baudRate);
  cfmakeraw(&tio);

  tio.c_iflag &= ~IXOFF;
  tio.c_iflag |= IGNPAR;
  tio.c_oflag &= ~(ONLCR | OCRNL);
  tio.c_cflag &= ~CSTOPB;
  tio.c_cflag |= CREAD | CS8 | CLOCAL;
  tio.c_cc[VMIN] = 1;

  tcsetattr(fd, TCSANOW, &tio);
#else
  hComm = (HANDLE)_get_osfhandle(fd);

  DCB lpDCB;
  if (!GetCommState(hComm, &lpDCB)) {
    printf("error GetCommState\n");
  }

  lpDCB.BaudRate = 38400;      // 転送速度の指定 (38400bps)
  lpDCB.ByteSize = 8;          // ビット長の指定 (8bit)
  lpDCB.Parity = NOPARITY;     // パリティ方式 (パリティなし)
  lpDCB.StopBits = ONESTOPBIT; // ストップビット数の指定 (1bit)


  lpDCB.fOutxCtsFlow = 0;      // 以下 フロー制御
  lpDCB.fOutxDsrFlow = 0;
  lpDCB.fDtrControl = 0;
  lpDCB.fOutX = 0;
  lpDCB.fInX = 0;
  lpDCB.fRtsControl = 0;
  if (!SetCommState(hComm, &lpDCB)) {
    printf("error SetCommState\n");
  }

#if 0
  // タイムアウトの設定
  COMMTIMEOUTS lpCommTimeouts;

  lpCommTimeouts.ReadIntervalTimeout = 500;       // 全文字の読み込み時間
  lpCommTimeouts.ReadTotalTimeoutMultiplier = 0;  // 読み込みの１文字あたりの待ち時間
  lpCommTimeouts.ReadTotalTimeoutConstant = 500;  // 読み込みエラー検出用のタイムアウト時間
  lpCommTimeouts.WriteTotalTimeoutMultiplier = 0; // 書き込みの１文字あたりの待ち時間
  lpCommTimeouts.WriteTotalTimeoutConstant = 0;   // 書き込みエラー検出用のタイムアウト時間

  if (!SetCommTimeouts( hComm, &lpCommTimeouts )) {
    printf("error SetCommTimeouts\n");
  }
#endif
#endif

  printf("X68000 Remote Disk Server test\n");

  while (1) {
    do { 
      read(fd, buf, 1);
    } while (buf[0] != 'Z');
    do {
      read(fd, buf, 1);
    } while (buf[0] == 'Z');
    if (buf[0] != 'X')
      continue;

    read(fd, buf, 1);
    read(fd, &buf[1], 1);
    size = buf[0] * 256 + buf[1];
    printf("recv %d bytes\n", size);

    int s = size;
    i = 0;
    while (s > 0) {
      len = read(fd, &buf[i], s);
      s -= len;
      i += len;
    }

#if 0
    for(i = 0; i < size; i++) {
      printf("%02X ", buf[i]);
    }
    printf("\n");
#endif

    printf("----Command: 0x%02x\n", buf[0]);

    switch (buf[0]) {
    case 0x41: /* dir search */
      hfuCallChdir(fd, buf);
      break;
    case 0x42: /* mkdir */
      hfuCallMkdir(fd, buf);
      break;
    case 0x43: /* rmdir */
      hfuCallRmdir(fd, buf);
      break;
    case 0x44: /* rename */
      hfuCallRename(fd, buf);
      break;
    case 0x45: /* remove */
      hfuCallDelete(fd, buf);
      break;
    case 0x46: /* getsetattr */
      hfuCallChmod(fd, buf);
      break;
    case 0x47: /* files */
      hfuCallFiles(fd, buf);
      break;
    case 0x48: /* nfiles */
      hfuCallNfiles(fd, buf);
      break;
    case 0x49: /* create */
      hfuCallCreate(fd, buf);
      break;
    case 0x4a: /* open */
      hfuCallOpen(fd, buf);
      break;
    case 0x4b: /* close */
      hfuCallClose(fd, buf);
      break;
    case 0x4c: /* read */
      hfuCallRead(fd, buf);
      break;
    case 0x4d: /* write */
      hfuCallWrite(fd, buf);
      break;
    case 0x4e: /* seek */
      hfuCallSeek(fd, buf);
      break;
    case 0x4f: /* filedate */
      hfuCallFiledate(fd, buf);
      break;
    case 0x50: /* dskfre */
      hfuCallDskfre(fd, buf);
      break;

    default:
      printf("error: %02x\n", buf[0]);
    }
  }

  close(fd);
  return 0;
}
