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

#ifndef _FILEOP_H_
#define _FILEOP_H_

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#ifndef WINNT
#include <iconv.h>
#include <sys/statfs.h>
#include <endian.h>
#else
#include <windows.h>
#endif

//****************************************************************************
// Data types
//****************************************************************************

typedef struct stat TYPE_STAT;
#define STAT_SIZE(st)     ((st)->st_size)
#define STAT_MTIME(st)    ((st)->st_mtime)
#define STAT_ISDIR(st)    (S_ISDIR((st)->st_mode))

typedef DIR TYPE_DIR;
typedef struct dirent TYPE_DIRENT;
typedef int TYPE_FD;
#define FD_BADFD -1
#define DIRENT_NAME(d)    ((d)->d_name);

//****************************************************************************
// for MinGW
//****************************************************************************

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
// SJIS <-> UTF-8 conversion
//****************************************************************************

static inline int FUNC_ICONV_S2U(char **src_buf, size_t *src_len, char **dst_buf, size_t *dst_len)
{
#ifndef WINNT
  // SJIS -> UTF-8に変換
  iconv_t cd = iconv_open("UTF-8", "CP932");
  int r = iconv(cd, src_buf, src_len, dst_buf, dst_len);
  iconv_close(cd);
  return r;
#else
  // MinGWではSJISのまま使う
  memcpy(*dst_buf, *src_buf, *src_len);
  *dst_buf += *src_len;
  return 0;
#endif
}
static inline int FUNC_ICONV_U2S(char **src_buf, size_t *src_len, char **dst_buf, size_t *dst_len)
{
#ifndef WINNT
  // SJIS -> UTF-8に変換
  iconv_t cd = iconv_open("CP932", "UTF-8");
  int r = iconv(cd, src_buf, src_len, dst_buf, dst_len);
  iconv_close(cd);
  return r;
#else
  // MinGWではSJISのまま使う
  while ((*src_len) > 0) {
    uint8_t c = *(*(uint8_t **)src_buf)++;
    (*src_len)--;
    if ((c >= 0x80 && c <= 0x9f) || (c >= 0xe0 && c <= 0xff)) {
      if ((*src_len) <= 0)
        return -1;
      uint8_t c2 = *(*(uint8_t **)src_buf)++;
      (*src_len)--;
      if ((*dst_len) < 2)
        return -1;
      *(*dst_buf)++ = c;
      (*dst_len)--;
      *(*dst_buf)++ = c2;
      (*dst_len)--;
    } else {
      if ((*dst_len) < 1)
        return -1;
      *(*dst_buf)++ = c;
      (*dst_len)--;
    }
  }
  return 0;
#endif
}

//****************************************************************************
// File attributes
//****************************************************************************

static inline int FUNC_FILEMODE_ATTR(TYPE_STAT *st)
{
  int attr = S_ISREG(st->st_mode) ? 0x20 : 0;   // regular file
  attr |= S_ISDIR(st->st_mode) ? 0x10 : 0;      // directory
  attr |= (st->st_mode & S_IWUSR) ? 0 : 0x01;   // read only
  return attr;
}
static inline int FUNC_ATTR_FILEMODE(int attr, TYPE_STAT *st)
{
  int mode;
  if (attr & 0x01) {
    mode = st->st_mode & ~(S_IWUSR|S_IWGRP|S_IWOTH);  // read only
  } else {
    mode = st->st_mode |= (S_IWUSR|S_IWGRP|S_IWOTH);  // read/write
  }
  return mode;
}

static inline int FUNC_CHMOD(int *err, const char *path, int mode)
{
  int r = chmod(path, mode);
  if (err)
    *err = -errno;
  return r;
}

//****************************************************************************
// Filesystem operations
//****************************************************************************

static inline int FUNC_STAT(int *err, const char *path, TYPE_STAT *st)
{
  int r = stat(path, st);
  if (err)
    *err = errno;
  return r;
}
static inline int FUNC_MKDIR(int *err, const char *path)
{
#ifndef WINNT
  int r = mkdir(path, 0777);
#else
  int r = mkdir(path);
#endif
  if (err)
    *err = errno;
  return r;
}
static inline int FUNC_RMDIR(int *err, const char *path)
{
  int r = rmdir(path);
  if (err)
    *err = errno;
  return r;
}
static inline int FUNC_RENAME(int *err, const char *pathold, const char *pathnew)
{
  int r = rename(pathold, pathnew);
  if (err)
    *err = errno;
  return r;
}
static inline int FUNC_UNLINK(int *err, const char *path)
{
  int r = unlink(path);
  if (err)
    *err = errno;
  return r;
}

//****************************************************************************
// Directory operations
//****************************************************************************

static inline TYPE_DIR *FUNC_OPENDIR(int *err, const char *path)
{
  TYPE_DIR *dir = opendir(path);
  if (err)
    *err = errno;
  return dir;
}
static inline TYPE_DIRENT *FUNC_READDIR(int *err, TYPE_DIR *dir)
{
  TYPE_DIRENT *d = readdir(dir);
  if (err)
    *err = errno;
  return d;
}
static inline int FUNC_CLOSEDIR(int *err, TYPE_DIR *dir)
{
  int r = closedir(dir);
  if (err)
    *err = errno;
  return r;
}

//****************************************************************************
// File operations
//****************************************************************************

static inline TYPE_FD FUNC_OPEN(int *err, const char *path, int flags)
{
  TYPE_FD fd = open(path, flags, 0777);
  if (err)
    *err = errno;
  return fd;
}
static inline int FUNC_CLOSE(int *err, TYPE_FD fd)
{
  int r = close(fd);
  if (err)
    *err = errno;
  return r;
}
static inline ssize_t FUNC_READ(int *err, TYPE_FD fd, void *buf, size_t count)
{
  ssize_t r = read(fd, buf, count);
  if (err)
    *err = errno;
  return r;
}
static inline ssize_t FUNC_WRITE(int *err, TYPE_FD fd, const void *buf, size_t count)
{
  ssize_t r = write(fd, buf, count);
  if (err)
    *err = errno;
  return r;
}
static inline int FUNC_FTRUNCATE(int *err, TYPE_FD fd, off_t length)
{
  int r = ftruncate(fd, length);
  if (err)
    *err = errno;
  return r;
}
static inline off_t FUNC_LSEEK(int *err, TYPE_FD fd, off_t offset, int whence)
{
  off_t r = lseek(fd, offset, whence);
  if (err)
    *err = errno;
  return r;
}

static inline int FUNC_FSTAT(int *err, TYPE_FD fd, TYPE_STAT *st)
{
  int r = fstat(fd, st);
  if (err)
    *err = errno;
  return r;
}

static inline int FUNC_FILEDATE(int *err, TYPE_FD fd, uint16_t time, uint16_t date)
{
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
  futimens(fd, tv);
#else
  FILETIME lt;
  DosDateTimeToFileTime(date, time, &lt);
  FILETIME ft;
  LocalFileTimeToFileTime(&lt, &ft);
  SetFileTime((HANDLE)_get_osfhandle(fd), NULL, &ft, &ft);
#endif
  return 0;
}

//****************************************************************************
// Misc functions
//****************************************************************************

static inline int FUNC_STATFS(int *err, char *path, uint64_t *total, uint64_t *free)
{
#ifndef WINNT
  struct statfs sf;
  statfs(path, &sf);
  *total = sf.f_blocks * sf.f_bsize;
  *free = sf.f_bfree * sf.f_bsize;
#else
  ULARGE_INTEGER ultotal;
  ULARGE_INTEGER ulfree;
  ULARGE_INTEGER ultotalfree;
  GetDiskFreeSpaceEx(path, &ulfree, &ultotal, &ultotalfree);
  *total = ultotal.QuadPart;
  *free = ulfree.QuadPart;
#endif
  return 0;
}

#endif /* _FILEOP_H_ */
