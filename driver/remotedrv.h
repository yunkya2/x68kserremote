/*
 * Copyright (c) 2023,2026 Yuichi Nakamura (@yunkya2)
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

#ifndef _REMOTEDRV_H_
#define _REMOTEDRV_H_

#include <stdbool.h>
#include <stdint.h>
#include <setjmp.h>
#include <x68k/dos.h>
#include <x68kremote.h>

// Human68k device driver request header
#ifndef _DOS_INTERNAL_H_
struct dos_req_header {
    uint8_t magic;              // +0x00.b Constant
    uint8_t unit;               // +0x01.b Unit number
    uint8_t command;            // +0x02.b Command code
    uint8_t errl;               // +0x03.b Error code low
    uint8_t errh;               // +0x04.b Error code high
    uint8_t reserved[8];        // +0x05 .. +0x0c not used
    uint8_t attr;               // +0x0d.b Attribute / Seek mode
    void *addr;                 // +0x0e.l Buffer address
    uint32_t status;            // +0x12.l Bytes / Buffer / Result status
    void *fcb;                  // +0x16.l FCB
} __attribute__((__packed__, __aligned__(2)));
#endif

#define DPRINTF1(...)  DPRINTF(1, __VA_ARGS__)
#define DPRINTF2(...)  DPRINTF(2, __VA_ARGS__)
#define DPRINTF3(...)  DPRINTF(3, __VA_ARGS__)

#ifdef DEBUG
void DPRINTF(int level, char *fmt, ...);
void DNAMEPRINT(void *n, bool full, char *head);
#else
#define DPRINTF(...)
#define DNAMEPRINT(n, full, head)
#endif

extern jmp_buf jenv;
extern int unit_base;

int interrupt(struct dos_req_header *req, void *dev);

void com_cmdres(void *wbuf, size_t wsize, void *rbuf, size_t rsize);
int com_timeout(struct dos_req_header *req);
int com_init(struct dos_req_header *req);

#endif /* _REMOTEDRV_H_ */
