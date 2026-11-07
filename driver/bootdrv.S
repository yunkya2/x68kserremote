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

#include <config.h>

/* Human68k device driver header */

    .text

    .extern interrupt

devheader:
    .long   -1                  // link pointer
    .word   0x2000              // device type (remote disk)
    .long   strategy            // strategy routine entry point
    .long   interrupt_asm       // interrupt routine entry point

    .ascii  "\x01SCHDISK"       // (SCSI BOOTROMはこの名前のドライバでないと認識しない)

    .global bootpart            // SCSI ID0から数えて何番目のパーティションから起動するか
bootpart:                       // Human68kが起動ドライブを決定するために使用される
    .byte   0                   // (SCSI BOOTROMから書き込まれる)

    .global scsiidd2
scsiidd2:
    .byte   0                   // (Human68kのSCSIデバイス初期化処理からd2レジスタで渡される)

    .word   0

// fixupsys.pyによって設定される
reloffset:
    .long   0                   // 再配置データへのオフセット
relsize:
    .long   0                   // 再配置データのサイズ
bsssize:
    .long   0                   // bssセクションのサイズ

devname:
    .ascii  CONFIG_DEVNAME      // device name

strategy:
    movem.l %d0-%d1/%a0-%a3,%sp@-   // 初回実行時のみ再配置処理を行う

    lea.l   %pc@(devheader),%a0     // a0: デバイスドライバ先頭アドレス
    move.l  %a0,%a2                 // a2: 再配置処理を行うアドレス
    move.l  %a0,%d1                 // d1: デバイスドライバ先頭アドレス
    movea.l %pc@(reloffset),%a1
    adda.l  %a0,%a1                 // a1: 再配置データのアドレス
    movea.l %a1,%a3                 // a3: bssセクション先頭アドレス
    adda.w  %a1@+,%a2               // 最初の2エントリ(ストラテジルーチン、割り込みルーチン)は
    adda.w  %a1@+,%a2               // SCSI BOOT ROMによって既に再配置済みなのでスキップ
    move.l  %pc@(relsize),%d0       // d0: 再配置データのサイズ
    subq.l  #4,%d0                  // 最初の2エントリ分スキップ
    lsr.w   #1,%d0                  // (ワード数)
    bra     1f

loop:
    adda.w  %a1@+,%a2               // 再配置データを取得
    add.l   %d1,%a2@                // 再配置処理
1:
    dbra    %d0,loop

    move.l  %pc@(bsssize),%d0       // d0: bssセクションのサイズ
    lsr.w   #1,%d0                  // (ワード数)
    bra     2f

loop1:
    clr.l   %a3@+                   // bssセクションをクリア
2:
    dbra    %d0,loop1

    lea.l   %pc@(3f),%a1            // 次回以降の呼び出しでは再配置処理は行わない
    move.l  %a1,%a0@(6)
    lea.l   %pc@(devname),%a1       // デバイス名を変更
    move.l  %a1@+,%a0@(14)
    move.l  %a1@+,%a0@(18)

    movem.l %sp@+,%d0-%d1/%a0-%a3
3:
    move.l  %a5,reqheader
    rts

reqheader:
    .long   0

interrupt_asm:
    movem.l %d0-%d2/%a0-%a2/%a5,%sp@-
    lea.l   %pc@(scsiidd2),%a0
    move.b  %d2,%a0@                // ドライバ初期化時に渡されるSCSI IDを保存
    pea.l   %pc@(devheader)
    move.l  reqheader,%sp@-
    bsr     interrupt               // interrupt(req, dev)
    movea.l %sp@+,%a5
    addq.l  #4,%sp
    ror.w   #8,%d0
    move.b  %d0,%a5@(4)             // errh エラーコードを設定
    rol.w   #8,%d0
    move.b  %d0,%a5@(3)             // errl エラーコードを設定
    movem.l %sp@+,%d0-%d2/%a0-%a2/%a5
    rts


/* Dummy program entry for Human68k device driver */

    .global _start
_start:
    rts

    .end
