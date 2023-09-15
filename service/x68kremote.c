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
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#ifndef WINNT
#include <sys/ioctl.h>
#include <termios.h>
#else
#include <windows.h>
#endif

#include <config.h>
#include <x68kremote.h>
#include "remoteserv.h"

//****************************************************************************
// Global type and variables
//****************************************************************************

const char *rootpath = ".";
int debuglevel = 0;

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
    DPRINTF3("%02X ", c);
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
      rootpath = (const char *)argv[i];
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
    uint8_t cbuf[1024 + 8];
    uint8_t rbuf[1024 + 8];
    int rsize;

    if (serin(fd, cbuf, sizeof(cbuf)) < 0) {
      continue;
    }
    if ((rsize = remote_serv(cbuf, rbuf)) < 0) {
      continue;
    }
    serout(fd, rbuf, rsize);
  }

  close(fd);
  return 0;
}
