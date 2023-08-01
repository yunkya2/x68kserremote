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

#ifndef _X68KREMOTE_H_
#define _X68KREMOTE_H_

#include <stdint.h>

/****************************************************************************/
/* Human68k error code                                                      */
/****************************************************************************/
#define DOS_INVALID_FUNCTION       -1 //無効なファンクションコード
#define DOS_FILE_NOT_FOUND         -2 //ファイルが見つからない
#define DOS_DIRECTORY_NOT_FOUND    -3 //ディレクトリが見つからない
#define DOS_TOO_MANY_HANDLES       -4 //オープンしているファイルが多すぎる
#define DOS_NOT_A_FILE             -5 //ディレクトリやボリュームラベルをアクセスしようとした
#define DOS_HANDLE_IS_NOT_OPENED   -6 //指定したハンドラがオープンされていない
#define DOS_BROKEN_MEMORY_CHAIN    -7 //メモリ管理領域が壊れている(実際に-7が返されることはない)
#define DOS_NOT_ENOUGH_MEMORY      -8 //メモリが足りない
#define DOS_INVALID_MEMORY_CHAIN   -9 //無効なメモリ管理テーブルを指定した
#define DOS_INVALID_ENVIRONMENT   -10 //不正な環境を指定した(実際に-10が返されることはない)
#define DOS_ABNORMAL_X_FILE       -11 //実行ファイルのフォーマットが異常
#define DOS_INVALID_ACCESS_MODE   -12 //オープンのアクセスモードが異常
#define DOS_ILLEGAL_FILE_NAME     -13 //ファイル名の指定が間違っている
#define DOS_INVALID_PARAMETER     -14 //パラメータが無効
#define DOS_ILLEGAL_DRIVE_NUMBER  -15 //ドライブの指定が間違っている
#define DOS_CURRENT_DIRECTORY     -16 //カレントディレクトリを削除しようとした
#define DOS_CANNOT_IOCTRL         -17 //_IOCTRLできないデバイス
#define DOS_NO_MORE_FILES         -18 //該当するファイルがもうない(_FILES,_NFILES)
#define DOS_CANNOT_WRITE          -19 //ファイルに書き込めない(主に属性R,Sのファイルに対する書き込みや削除)
#define DOS_DIRECTORY_EXISTS      -20 //同一名のディレクトリを作ろうとした
#define DOS_RM_NONEMPTY_DIRECTORY -21 //空でないディレクトリを削除しようとした
#define DOS_MV_NONEMPTY_DIRECTORY -22 //空でないディレクトリを移動しようとした
#define DOS_DISK_FULL             -23 //ディスクフル
#define DOS_DIRECTORY_FULL        -24 //ディレクトリフル
#define DOS_SEEK_OVER_EOF         -25 //EOFを越えてシークしようとした
#define DOS_ALREADY_SUPERVISOR    -26 //既にスーパーバイザ状態になっている
#define DOS_THREAD_EXISTS         -27 //同じスレッド名が存在する
#define DOS_COMMUNICATION_FAILED  -28 //スレッド間通信バッファに書き込めない(ビジーまたはオーバーフロー)
#define DOS_TOO_MANY_THREADS      -29 //これ以上バックグラウンドでスレッドを起動できない
#define DOS_NOT_ENOUGH_LOCK_AREA  -32 //ロック領域が足りない
#define DOS_FILE_IS_LOCKED        -33 //ロックされていてアクセスできない
#define DOS_OPENED_HANDLE_EXISTS  -34 //指定のドライブはハンドラがオープンされている
#define DOS_FILE_EXISTS           -80 //ファイルが存在している(_NEWFILE,_MAKETMP)

/****************************************************************************/
/* Human68k structures                                                      */
/****************************************************************************/

/* Device driver request header */

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


#if 0
struct HfsFilesbuf {
  uint8_t searchatr;
  uint8_t driveno;
  uint32_t dirsec;
  uint16_t dirlft;
  uint16_t dirpos;
  char filename[8];
  char ext[3];
  uint8_t atr;
  uint16_t time;
  uint16_t date;
  uint32_t filelen;
  char name[23];
} __attribute__((packed, aligned(2)));
typedef struct HfsFilesbuf HfsFilesbuf;
#endif

struct HfsFilesinfo {
  uint8_t dummy;
  uint8_t atr;
  uint16_t time;
  uint16_t date;
  uint32_t filelen;
  char name[23];
} __attribute__((packed, aligned(2)));
typedef struct HfsFilesinfo HfsFilesinfo;

typedef struct HfsNamests {     // namests 形式パス名
  uint8_t flag;       //  0   フラグまたはパスの長さ
  uint8_t drive;      //  1   内部ドライブ番号(0=A:)
  uint8_t path[65];   //  2   パス(区切りは'\'または$09)
  uint8_t name1[8];   // 67   ファイル名1
  uint8_t ext[3];     // 75   拡張子
  uint8_t name2[10];  // 78   ファイル名2
} HfsNamests;         // 88 bytes
typedef struct HfsNamests HfsNamests;

/****************************************************************************/
/* ZRMTDSK serial communication protocol defintion                          */
/****************************************************************************/

struct dirop {
  uint8_t command;
  HfsNamests path;
};
struct dirop_res {
  int8_t res;
};

struct rename {
  uint8_t command;
  HfsNamests pathOld;
  HfsNamests pathNew;
};
struct rename_res {
  int8_t res;
};

struct chmod {
  uint8_t command;
  uint8_t attr;
  HfsNamests path;
};
struct chmod_res {
  int8_t res;
};

struct files {
  uint8_t command;
  uint8_t attr;
  uint8_t dummy[2];
  uint32_t filep;
  HfsNamests path;
};
struct files_res {
  int8_t res;
  HfsFilesinfo file;
};

struct nfiles {
  uint8_t command;
  uint8_t dummy[3];
  uint32_t filep;
};
struct nfiles_res {
  int8_t res;
  HfsFilesinfo file;
};

struct create {
  uint8_t command;
  uint8_t attr;
  uint8_t mode;
  uint8_t dummy;
  uint32_t fcbp;
  HfsNamests path;
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
  HfsNamests path;
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

struct filedate
{
  uint8_t command;
  uint8_t dummy[3];
  uint32_t fcb;
  uint16_t time;
  uint16_t date;
};
struct filedate_res
{
  uint16_t time;
  uint16_t date;
};

struct dskfre
{
  uint8_t command;
};
struct dskfre_res
{
  uint32_t res;
  uint16_t freeclu;
  uint16_t totalclu;
  uint16_t clusect;
  uint16_t sectsize;
};


#endif /* _X68KREMOTE_H_ */
