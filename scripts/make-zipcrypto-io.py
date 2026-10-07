#!/usr/bin/env python3
"""Write a small BrickLink Studio .io test file, encrypted the way Studio does it.

Modern Studio .io files are ZIPs whose entries are encrypted with traditional
PKWARE "ZipCrypto" (general-purpose flag bit 0) under Studio's fixed password,
soho0909. Python's zipfile can read these but not write them, so this script
writes the archive itself. The output is deterministic (fixed "random" header
bytes and timestamps), so re-running it reproduces the committed fixture.

Usage:
  scripts/make-zipcrypto-io.py [out.io] [--password PW] [--stored]

Default output: fixtures/studio/zipcrypto-small.io. Both the desktop and the
web repo use this script; copy it as-is.

The model has what a real Studio file has: model.ldr as the main model with an
MPD submodel, a custom part under CustomParts/ (with the .conn and .col files
Studio puts next to it), modelv1.ldr / model2.ldr variants and .info.
"""
import struct
import sys
import zlib

PASSWORD = b"soho0909"

# 1/1/2020 12:00:00 in DOS format.
DOS_TIME = (12 << 11) | (0 << 5) | 0
DOS_DATE = ((2020 - 1980) << 9) | (1 << 5) | 1

MODEL_LDR = (
    "﻿0 FILE zipcrypto-small.io\r\n"
    "0 Untitled Model\r\n"
    "0 Name:  zipcrypto-small\r\n"
    "0 Author:  \r\n"
    "0 CustomBrick\r\n"
    "1 4 0.000000 -24.000000 0.000000 1 0 0 0 1 0 0 0 1 3001.dat\r\n"
    "1 1 80.000000 -24.000000 0.000000 1 0 0 0 1 0 0 0 1 testcustom.dat\r\n"
    "1 2 0.000000 0.000000 100.000000 0 0 1 0 1 0 -1 0 0 submodel group 1\r\n"
    "0 STEP\r\n"
    "0 NOFILE\r\n"
    "0 FILE SubModel Group 1\r\n"
    "0 SubModel Group 1\r\n"
    "0 Name:  SubModel Group 1\r\n"
    "0 Author:  \r\n"
    "0 CustomBrick\r\n"
    "0 NumOfBricks:  2\r\n"
    "1 14 0.000000 -8.000000 0.000000 1 0 0 0 1 0 0 0 1 3023.dat\r\n"
    "1 16 40.000000 -8.000000 0.000000 1 0 0 0 1 0 0 0 1 3023.dat\r\n"
    "0 NOFILE\r\n"
).encode("utf-8")

# Studio's legacy line format: "10 <colour> False 0 <x y z> <matrix> <file>".
MODELV1_LDR = MODEL_LDR.replace(b"\r\n1 ", b"\r\n10 ").replace(b"\r\n10 4 ", b"\r\n10 4 False 0 ")

# model2.ldr: BrickLink colour numbers and every custom part inlined.
MODEL2_LDR = MODEL_LDR.replace(b"1 4 0.000000", b"1 5 0.000000")

CUSTOM_DAT = (
    "0 FILE testcustom.dat\r\n"
    "0 Custom test part\r\n"
    "0 Name:  testcustom.dat\r\n"
    "0 Author:  \r\n"
    "0 CustomBrick\r\n"
    "0 PE_TEX_PATH 0\r\n"
    "4 16 -20 0 -20 20 0 -20 20 0 20 -20 0 20\r\n"
    "4 16 -20 -24 -20 20 -24 -20 20 -24 20 -20 -24 20\r\n"
    "0 NOFILE\r\n"
).encode("utf-8")

ENTRIES = [
    ("model.ldr", MODEL_LDR),
    ("modelv1.ldr", MODELV1_LDR),
    ("model2.ldr", MODEL2_LDR),
    ("CustomParts/testcustom.dat", CUSTOM_DAT),
    ("CustomParts/connectivity/testcustom.conn", b"<Connectivity/>\r\n"),
    ("CustomParts/collider/testcustom.col", b"<Collider/>\r\n"),
    ("thumbnail.png", b"\x89PNG\r\n\x1a\n"),
    ("errorPartList.err", b"[]"),
    (".info", b'{"version":"2.1.5_10\\r","total_parts":4}'),
]


def crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


class ZipCrypto:
    """Traditional PKWARE encryption (APPNOTE 6.1)."""

    def __init__(self, password):
        self.k0, self.k1, self.k2 = 0x12345678, 0x23456789, 0x34567890
        for b in password:
            self._update(b)

    def _update(self, b):
        self.k0 = (zlib.crc32(bytes([b]), self.k0 ^ 0xFFFFFFFF) ^ 0xFFFFFFFF) & 0xFFFFFFFF
        self.k1 = (self.k1 + (self.k0 & 0xFF)) & 0xFFFFFFFF
        self.k1 = (self.k1 * 134775813 + 1) & 0xFFFFFFFF
        self.k2 = (zlib.crc32(bytes([self.k1 >> 24]), self.k2 ^ 0xFFFFFFFF) ^ 0xFFFFFFFF) & 0xFFFFFFFF

    def encrypt(self, data):
        out = bytearray()
        for b in data:
            t = (self.k2 | 2) & 0xFFFF
            out.append(b ^ (((t * (t ^ 1)) >> 8) & 0xFF))
            self._update(b)
        return bytes(out)


def build(password, stored):
    body = bytearray()
    central = bytearray()
    for i, (name, data) in enumerate(ENTRIES):
        crc = crc32(data)
        if stored:
            method, packed = 0, data
        else:
            c = zlib.compressobj(9, zlib.DEFLATED, -15)
            method, packed = 8, c.compress(data) + c.flush()
        # 11 fixed "random" bytes, then the CRC's high byte as the check byte.
        header = bytes((0x5A + 7 * i + k * 13) & 0xFF for k in range(11)) + bytes([crc >> 24])
        enc = ZipCrypto(password).encrypt(header + packed)
        flags = 0x0001
        offset = len(body)
        nm = name.encode("utf-8")
        body += struct.pack("<IHHHHHIIIHH", 0x04034B50, 20, flags, method, DOS_TIME, DOS_DATE,
                            crc, len(enc), len(data), len(nm), 0) + nm + enc
        central += struct.pack("<IHHHHHHIIIHHHHHII", 0x02014B50, 20, 20, flags, method, DOS_TIME,
                               DOS_DATE, crc, len(enc), len(data), len(nm), 0, 0, 0, 0, 0,
                               offset) + nm
    eocd = struct.pack("<IHHHHIIH", 0x06054B50, 0, 0, len(ENTRIES), len(ENTRIES), len(central),
                       len(body), 0)
    return bytes(body + central + eocd)


def main(argv):
    out = "fixtures/studio/zipcrypto-small.io"
    password = PASSWORD
    stored = False
    args = iter(argv)
    for a in args:
        if a == "--password":
            password = next(args).encode("utf-8")
        elif a == "--stored":
            stored = True
        else:
            out = a
    with open(out, "wb") as f:
        f.write(build(password, stored))


if __name__ == "__main__":
    main(sys.argv[1:])
