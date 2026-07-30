#!/usr/bin/env python3
"""Headless reference implementation of the FloorVM v2 machine.

This mirrors src/machine.c exactly (same opcodes, same big-endian layout) plus
the v2 memory map from src/machine.h. It exists to test/validate programs
(especially the Flappy Bird game) without needing the SDL build, and to dump
frames as PNG images for visual inspection.

Not used by the emulator itself -- purely a development/testing aid.
"""
from pathlib import Path
import struct
import zlib
import sys

# ---- Memory map (must match src/machine.h) --------------------------------
SCREEN_W = 100
SCREEN_H = 100
CYCLES_PER_FRAME = 200000
PALETTE_ENTRIES = 256

PROGRAM_SIZE = 8192
VRAM_SIZE = SCREEN_W * SCREEN_H
PALETTE_SIZE = PALETTE_ENTRIES * 3
VMODE_SIZE = 1
INPUT_SIZE = 1
SAVECTL_SIZE = 1
SRAM_SIZE = 4096
USER_SIZE = 4096
PRESENT_SIZE = 1
RAM_SIZE = (PROGRAM_SIZE + VRAM_SIZE + PALETTE_SIZE + VMODE_SIZE +
            INPUT_SIZE + SAVECTL_SIZE + SRAM_SIZE + USER_SIZE + PRESENT_SIZE)

PROGRAM_START = 0
VRAM_START = PROGRAM_SIZE
PALETTE_START = VRAM_START + VRAM_SIZE
VMODE_START = PALETTE_START + PALETTE_SIZE
INPUT_START = VMODE_START + VMODE_SIZE
SAVECTL_START = INPUT_START + INPUT_SIZE
SRAM_START = SAVECTL_START + SAVECTL_SIZE
USER_START = SRAM_START + SRAM_SIZE
PRESENT_START = USER_START + USER_SIZE

MASK = 0xFFFFFFFF


def expand3(v):
    v &= 0x07
    return ((v << 5) | (v << 2) | (v >> 1)) & 0xFF


def expand2(v):
    v &= 0x03
    return ((v << 6) | (v << 4) | (v << 2) | v) & 0xFF


class Machine:
    def __init__(self):
        self.ram = bytearray(RAM_SIZE)
        self.reg = [0] * 16
        self.pc = 0
        self.halted = False
        self.load_default_palette()

    def load_default_palette(self):
        for i in range(PALETTE_ENTRIES):
            base = PALETTE_START + i * 3
            self.ram[base + 0] = expand3(i >> 5)
            self.ram[base + 1] = expand3(i >> 2)
            self.ram[base + 2] = expand2(i)

    def load_rom(self, data):
        assert len(data) <= PROGRAM_SIZE
        self.ram[0:len(data)] = data

    # -- byte helpers -------------------------------------------------------
    def cp_byte(self):
        b = self.ram[self.pc]
        self.pc = (self.pc + 1) & 0xFFFF
        return b

    def cp_u32(self):
        return ((self.cp_byte() << 24) | (self.cp_byte() << 16) |
                (self.cp_byte() << 8) | self.cp_byte())

    def read_u32(self, addr):
        return ((self.ram[addr] << 24) | (self.ram[addr + 1] << 16) |
                (self.ram[addr + 2] << 8) | self.ram[addr + 3])

    def write_u32(self, addr, value):
        value &= MASK
        self.ram[addr] = (value >> 24) & 0xFF
        self.ram[addr + 1] = (value >> 16) & 0xFF
        self.ram[addr + 2] = (value >> 8) & 0xFF
        self.ram[addr + 3] = value & 0xFF

    # -- one instruction ----------------------------------------------------
    def step(self):
        op = self.cp_byte()
        r = self.reg
        if op == 0x00:  # HALT
            self.halted = True
        elif op == 0x01:  # JMP
            self.pc = self.cp_u32() & 0xFFFF
        elif op == 0x02:  # JMPIZ
            t = self.cp_u32()
            if r[self.cp_byte()] == 0:
                self.pc = t & 0xFFFF
        elif op == 0x03:  # JMPNZ
            t = self.cp_u32()
            if r[self.cp_byte()] != 0:
                self.pc = t & 0xFFFF
        elif op == 0x10:  # LOAD32
            a = self.cp_u32(); reg = self.cp_byte(); r[reg] = self.read_u32(a)
        elif op == 0x11:  # LOAD8
            a = self.cp_u32(); reg = self.cp_byte(); r[reg] = self.ram[a]
        elif op == 0x12:  # LOAD32P
            ar = self.cp_byte(); reg = self.cp_byte(); r[reg] = self.read_u32(r[ar])
        elif op == 0x13:  # LOAD8P
            ar = self.cp_byte(); reg = self.cp_byte(); r[reg] = self.ram[r[ar]]
        elif op == 0x14:  # STORE32
            reg = self.cp_byte(); a = self.cp_u32(); self.write_u32(a, r[reg])
        elif op == 0x15:  # STORE8
            reg = self.cp_byte(); a = self.cp_u32(); self.ram[a] = r[reg] & 0xFF
        elif op == 0x16:  # STORE32P
            reg = self.cp_byte(); ar = self.cp_byte(); self.write_u32(r[ar], r[reg])
        elif op == 0x17:  # STORE8P
            reg = self.cp_byte(); ar = self.cp_byte(); self.ram[r[ar]] = r[reg] & 0xFF
        elif op == 0x18:  # SET32
            reg = self.cp_byte(); r[reg] = self.cp_u32()
        elif op == 0x19:  # SET8
            reg = self.cp_byte(); r[reg] = self.cp_byte()
        elif op == 0x1A:  # COPY
            a = self.cp_byte(); b = self.cp_byte(); r[b] = r[a]
        elif op == 0x20:  # ADD
            t = self.cp_byte(); a = self.cp_byte(); b = self.cp_byte(); r[t] = (r[a] + r[b]) & MASK
        elif op == 0x21:  # SUB
            t = self.cp_byte(); a = self.cp_byte(); b = self.cp_byte(); r[t] = (r[a] - r[b]) & MASK
        elif op == 0x22:  # MUL
            t = self.cp_byte(); a = self.cp_byte(); b = self.cp_byte(); r[t] = (r[a] * r[b]) & MASK
        elif op == 0x23:  # DIV
            t = self.cp_byte(); a = self.cp_byte(); b = self.cp_byte()
            r[t] = 0 if r[b] == 0 else (r[a] // r[b]) & MASK
        elif op == 0x24:  # AND
            t = self.cp_byte(); a = self.cp_byte(); b = self.cp_byte(); r[t] = r[a] & r[b]
        elif op == 0x25:  # OR
            t = self.cp_byte(); a = self.cp_byte(); b = self.cp_byte(); r[t] = r[a] | r[b]
        elif op == 0x26:  # XOR
            t = self.cp_byte(); a = self.cp_byte(); b = self.cp_byte(); r[t] = r[a] ^ r[b]
        elif op == 0x27:  # NOT
            t = self.cp_byte(); a = self.cp_byte(); r[t] = (~r[a]) & MASK
        elif op == 0x28:  # SHL
            t = self.cp_byte(); a = self.cp_byte(); b = self.cp_byte(); r[t] = (r[a] << (r[b] & 31)) & MASK
        elif op == 0x29:  # SHR
            t = self.cp_byte(); a = self.cp_byte(); b = self.cp_byte(); r[t] = (r[a] >> (r[b] & 31)) & MASK
        else:
            raise ValueError(f"Unknown opcode 0x{op:02X} at pc={self.pc-1}")

    def run_cycles(self, n, input_byte=0):
        self.ram[INPUT_START] = input_byte & 0xFF
        for _ in range(n):
            self.ram[INPUT_START] = input_byte & 0xFF
            self.step()
            if self.halted:
                break

    # -- rendering ----------------------------------------------------------
    def frame_rgb(self):
        mode = self.ram[VMODE_START]
        out = bytearray(VRAM_SIZE * 3)
        for i in range(VRAM_SIZE):
            v = self.ram[VRAM_START + i]
            if mode == 1:
                e = PALETTE_START + v * 3
                rr, gg, bb = self.ram[e], self.ram[e + 1], self.ram[e + 2]
            else:
                rr, gg, bb = expand3(v >> 5), expand3(v >> 2), expand2(v)
            out[i * 3] = rr
            out[i * 3 + 1] = gg
            out[i * 3 + 2] = bb
        return out


def write_png(path, rgb, w=SCREEN_W, h=SCREEN_H, scale=4):
    # Nearest-neighbor upscale then encode as a minimal RGB PNG.
    sw, sh = w * scale, h * scale
    raw = bytearray()
    for y in range(sh):
        raw.append(0)  # filter type 0
        srow = (y // scale)
        for x in range(sw):
            sx = x // scale
            idx = (srow * w + sx) * 3
            raw += rgb[idx:idx + 3]

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & MASK)

    sig = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", sw, sh, 8, 2, 0, 0, 0)
    idat = zlib.compress(bytes(raw), 9)
    Path(path).write_bytes(sig + chunk(b"IHDR", ihdr) +
                           chunk(b"IDAT", idat) + chunk(b"IEND", b""))


if __name__ == "__main__":
    # Quick smoke test: load a ROM, run 1 frame, save a PNG.
    rom = Path(sys.argv[1]).read_bytes()
    m = Machine()
    m.load_rom(rom)
    m.run_cycles(CYCLES_PER_FRAME)
    write_png("frame.png", m.frame_rgb())
    print("wrote frame.png")
