#!/usr/bin/env python3
"""Pack FloorVM ROMs into a .fvd disk image.

A .fvd disk image bundles several programs plus a writable save area for each
one. FloorVM's bootloader lists the programs on the disk, and saves made while
running from the disk are written back into the image (unlike a bare .from
cartridge, whose saves go to a "<rom>.sav" sidecar file).

Disk format (all multi-byte integers big-endian, matching the VM):

    offset 0   : magic "FVD1"                        (4 bytes)
    offset 4   : u32 entry_count
    offset 8   : directory table, entry_count records of 48 bytes each:
                     char name[32]   (UTF-8, null-padded)
                     u32  rom_offset
                     u32  rom_size
                     u32  save_offset
                     u32  save_size
    then       : ROM blobs, then per-entry save areas

Usage:
    python tools/mkdisk.py out.fvd game1.from game2.from "boss.from=BOSS FIGHT"

A display name may be supplied per ROM with "path=NAME"; otherwise the file
stem (uppercased) is used.
"""
from pathlib import Path
import struct
import sys


MAGIC = b"FVD1"
PROGRAM_SIZE = 8192
SRAM_SIZE = 4096  # Must match SRAM_SIZE in src/machine.h
NAME_LEN = 32
RECORD_SIZE = 48


def load_entries(specs: list[str]) -> list[tuple[str, bytes]]:
    entries: list[tuple[str, bytes]] = []
    for spec in specs:
        if "=" in spec:
            path_str, name = spec.split("=", 1)
        else:
            path_str, name = spec, None

        path = Path(path_str)
        rom = path.read_bytes()
        if len(rom) > PROGRAM_SIZE:
            print(f"ROM {path} is {len(rom)} bytes, max is {PROGRAM_SIZE}!")
            sys.exit(1)

        if name is None:
            name = path.stem.upper()

        entries.append((name, rom))

    return entries


def build(entries: list[tuple[str, bytes]]) -> bytes:
    count = len(entries)
    cursor = 8 + RECORD_SIZE * count

    rom_offsets: list[int] = []
    for _, rom in entries:
        rom_offsets.append(cursor)
        cursor += len(rom)

    save_offsets: list[int] = []
    for _ in entries:
        save_offsets.append(cursor)
        cursor += SRAM_SIZE

    out = bytearray()
    out += MAGIC
    out += struct.pack(">I", count)
    for i, (name, rom) in enumerate(entries):
        name_bytes = name.encode("utf-8")[:NAME_LEN].ljust(NAME_LEN, b"\0")
        out += name_bytes
        out += struct.pack(">I", rom_offsets[i])
        out += struct.pack(">I", len(rom))
        out += struct.pack(">I", save_offsets[i])
        out += struct.pack(">I", SRAM_SIZE)

    for _, rom in entries:
        out += rom

    out += b"\0" * (SRAM_SIZE * count)
    return bytes(out)


def main():
    if len(sys.argv) < 3:
        print("Usage: python tools/mkdisk.py out.fvd rom1.from [rom2.from ...]")
        print('       (optionally "rom.from=DISPLAY NAME" to set a menu label)')
        sys.exit(1)

    output_path = Path(sys.argv[1])
    entries = load_entries(sys.argv[2:])
    output_path.write_bytes(build(entries))
    print(f"{output_path} is ready! ({len(entries)} program(s))")
    for name, rom in entries:
        print(f"  - {name}  ({len(rom)} bytes)")


if __name__ == "__main__":
    main()
