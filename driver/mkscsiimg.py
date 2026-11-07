#!/usr/bin/env python3
# CONFIG_BOOTDRIVER テスト用のSCSIディスクイメージを生成する
#   usage: mkscsiimg.py <bootloader.bin> <driver.bin> <HUMAN.SYS> <output.hds>
import sys

IMAGESIZE = 0x100000

with open(sys.argv[1], 'rb') as f:
    boot = f.read()
with open(sys.argv[2], 'rb') as f:
    driver = f.read()
with open(sys.argv[3], 'rb') as f:
    human = f.read()

if len(driver) > 0x4000 - 0xc00:
    sys.exit('device driver too large')
if len(human) > 0x20000 - 0x8000:
    sys.exit('HUMAN.SYS too large')

img = bytearray(IMAGESIZE)
record_size = 512
img[0x000:0x008] = b'X68SCSI1'              # SCSI disk signature
img[0x008:0x00a] = record_size.to_bytes(2, 'big')
img[0x00a:0x00e] = (IMAGESIZE // record_size).to_bytes(4, 'big')
img[0x00e:0x010] = b'\x01\x00'
img[0x010:0x020] = b'X68000SerRemote'.ljust(16, b'\0')
img[0x400:0x400 + len(boot)] = boot         # IPL
img[0x800:0x804] = b'X68K'                  # SCSI partition signature
img[0x810:0x818] = b'Human68k'
img[0xc00:0xc00 + len(driver)] = driver     # SCSI device driver
img[0x8000:0x8000 + len(human)] = human     # HUMAN.SYS

with open(sys.argv[4], 'wb') as g:
    g.write(img)
