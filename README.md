<h1 align="center">FloorVM - Fantasy Virtual Game Console</h1>

A lightweight, high-efficiency virtual machine with a variable-length byte-stream ISA, built for deterministic execution and low-level fantasy hardware/game development. Even though it's called a virtual machine, it's technically emulating a fantasy game console never put into production due to my brokeness and lateness.

---

## Architecture

**FloorVM** operates on a unified memory map with 16 general-purpose 32-bit registers, big-endian multi-byte serialization, and a compact variable-length instruction set architecture.

### System specs

| Component              | Value / Specification                                |
|------------------------|------------------------------------------------------|
| **Target frame rate**  | 60 FPS (CYCLES_PER_FRAME = 200,000)                  |
| **Display resolution** | 100 × 100 pixels (10 KB VRAM, 1 byte per pixel)      |
| **Color**              | 256-entry RGB palette, or direct RGB332 (see below)  |
| **Registers**          | 16 general-purpose 32-bit registers (r0 – r15)       |
| **Program counter**    | Unsigned 16-bit integer (pc)                         |
| **Endianness**         | Big-endian                                           |
| **Persistent save**    | 4 KB SRAM (battery-backed, per program)              |
| **Total memory**       | 27,155 bytes (≈26.5 KiB)                             |

---

## Unified memory map

Memory is mapped into linear segments across a continuous byte array:

| Region             | Start (dec) | Start (hex) | Size    | Description                                            |
|--------------------|-------------|-------------|---------|--------------------------------------------------------|
| **Program ROM**    | 0           | `0x0000`    | 8 KiB   | Executable bytecode & embedded data assets             |
| **VRAM buffer**    | 8192        | `0x2000`    | 10 KB   | 100 × 100 byte-addressable display buffer              |
| **Palette**        | 18192       | `0x4710`    | 768 B   | 256 entries × 3 bytes (R, G, B)                        |
| **Video mode**     | 18960       | `0x4A10`    | 1 byte  | `0` = direct RGB332, `1` = palette                     |
| **Input**          | 18961       | `0x4A11`    | 1 byte  | Memory-mapped controller button bitmask                |
| **Save control**   | 18962       | `0x4A12`    | 1 byte  | Write non-zero to flush SRAM to persistent storage     |
| **SRAM (save)**    | 18963       | `0x4A13`    | 4 KiB   | Persistent per-program save data                       |
| **User work RAM**  | 23059       | `0x5A13`    | 4 KiB   | General-purpose user-controlled free RAM               |

---

## Color modes

Each VRAM byte is one pixel. How the byte becomes a color depends on the **video-mode register** (address 18960):

- **Direct mode (`0`, default):** the byte is a packed **RGB332** color — bits `RRRGGGBB`. No setup required; 256 fixed colors.
- **Palette mode (`1`):** the byte is an **index** into the 256-entry palette at address 18192. Each entry is 3 bytes (R, G, B). Write your colors into the palette, set the mode to `1`, and every pixel value is looked up there.

On reset the palette is initialized to the RGB332 identity ramp, so an untouched palette in palette mode looks identical to direct mode until you overwrite entries.

```assembly
; Palette mode: make color index 1 = pure red, then draw it at pixel (0,0)
set8 r0 1
store8 r0 18960     ; video mode = palette
set8 r0 255
store8 r0 18195     ; palette[1].R
set8 r0 0
store8 r0 18196     ; palette[1].G
store8 r0 18197     ; palette[1].B
set8 r0 1
store8 r0 8192      ; VRAM pixel 0 -> palette index 1 (red)
```

---

## Persistent saving (SRAM & disks)

Programs persist data by writing to the **SRAM** region (address 18963, 4 KiB) and then writing a non-zero byte to the **save-control register** (address 18962). The emulator flushes SRAM to storage and clears the register. SRAM is also flushed automatically when the program exits.

Where the save goes depends on how the program was launched:

- **From a cartridge (`.from`):** saved to a sidecar file `<rom>.from.sav` next to the ROM.
- **From a disk image (`.fvd`):** saved into that program's dedicated save area inside the image, like a real disk-based console.

On boot, the matching save is loaded back into SRAM automatically.

---

## Bootloader & media

FloorVM boots whatever you point it at:

- **A `.from` file** — a single cartridge. Boots straight into the program.
- **A directory** — every `.from` inside becomes a menu entry (saves go to `.sav` sidecars).
- **A `.fvd` disk image** — multiple programs plus a per-program save area.

For a directory or a disk, a simple **bootloader menu** lists the programs. Navigate with **Up/Down** and launch with **A** (or Return). Press **ESC** in a running program to return to the menu (or to quit a single cartridge).

### Building a disk image

`tools/mkdisk.py` packs ROMs into a `.fvd`:

```bash
python tools/mkdisk.py demo.fvd "programs/flappy.from=FLAPPY BIRD" programs/colorbars.from
```

An optional `"path=NAME"` sets the menu label (otherwise the filename stem is used).

---

## Controller input map

Input is polled by reading the single memory-mapped byte at address 18961 (0x4A11). Bits represent button states (1=pressed, 0=released):

| Bit       | Mask (hex) | Flag constant | Button      | Emulator key |
|-----------|------------|---------------|-------------|--------------|
| **Bit 0** | `0x01`     | BTN_UP        | D-Pad Up    | Up arrow     |
| **Bit 1** | `0x02`     | BTN_DOWN      | D-Pad Down  | Down arrow   |
| **Bit 2** | `0x04`     | BTN_LEFT      | D-Pad Left  | Left arrow   |
| **Bit 3** | `0x08`     | BTN_RIGHT     | D-Pad Right | Right arrow  |
| **Bit 4** | `0x10`     | BTN_A         | Button A    | A            |
| **Bit 5** | `0x20`     | BTN_D         | Button D    | D            |
| **Bit 6** | `0x40`     | BTN_SELECT    | Select      | Enter/Return |
| **Bit 7** | `0x80`     | BTN_START     | Start       | Backspace    |

**ESC** is handled by the console itself (return to boot menu / quit), not exposed to programs.

---

## Instruction set architecture

The ISA uses single-byte opcodes followed by inline variable-length operands.

### Control instructions

| Opcode  | Hex    | Operands           | Description                               |
|---------|--------|--------------------|-------------------------------------------|
| `HALT`  | `0x00` | *None*             | Halts execution                           |
| `JMP`   | `0x01` | `target[4]`        | Unconditionally jumps to a 32-bit address |
| `JMPIZ` | `0x02` | `target[4] reg`    | Jumps to target if `reg` == 0             |
| `JMPNZ` | `0x03` | `target[4] reg`    | Jumps to target if `reg` != 0             |

### Memory instructions

| Opcode     | Hex    | Operands         | Description                                                        |
|------------|--------|------------------|--------------------------------------------------------------------|
| `LOAD32`   | `0x10` | `addr[4] reg`    | Reads 32-bit value from literal address into register              |
| `LOAD8`    | `0x11` | `addr[4] reg`    | Reads byte from literal address into register                      |
| `LOAD32P`  | `0x12` | `addr_reg reg`   | Reads 32-bit value from pointer register into register             |
| `LOAD8P`   | `0x13` | `addr_reg reg`   | Reads byte from pointer register into register                     |
| `STORE32`  | `0x14` | `reg addr[4]`    | Writes 32-bit value from register into literal address             |
| `STORE8`   | `0x15` | `reg addr[4]`    | Writes byte from register into literal address                     |
| `STORE32P` | `0x16` | `reg addr_reg`   | Writes 32-bit value from register into address in pointer register |
| `STORE8P`  | `0x17` | `reg addr_reg`   | Writes byte from register into address in pointer register         |
| `SET32`    | `0x18` | `reg value[4]`   | Sets register to immediate 32-bit value                            |
| `SET8`     | `0x19` | `reg value`      | Sets register to immediate byte value                              |
| `COPY`     | `0x1A` | `reg target_reg` | Copies value from `reg` into `target_reg`                          |

> **Note:** `SET32`/`SET8` immediates must be numbers, not labels. To use a data
> label's address at runtime, place the data at a known fixed offset (see the
> font trick in `programs/flappy.txt`).

### ALU instructions

> **Note:** All ALU operations store the result into `target_reg`. `DIV` by zero yields `0`.

| Opcode | Hex    | Operands                 | Operation                         |
|--------|--------|--------------------------|-----------------------------------|
| `ADD`  | `0x20` | `target_reg a_reg b_reg` | `target_reg` = `a_reg` + `b_reg`  |
| `SUB`  | `0x21` | `target_reg a_reg b_reg` | `target_reg` = `a_reg` - `b_reg`  |
| `MUL`  | `0x22` | `target_reg a_reg b_reg` | `target_reg` = `a_reg` * `b_reg`  |
| `DIV`  | `0x23` | `target_reg a_reg b_reg` | `target_reg` = `a_reg` / `b_reg`  |
| `AND`  | `0x24` | `target_reg a_reg b_reg` | `target_reg` = `a_reg` & `b_reg`  |
| `OR`   | `0x25` | `target_reg a_reg b_reg` | `target_reg` = `a_reg` \| `b_reg` |
| `XOR`  | `0x26` | `target_reg a_reg b_reg` | `target_reg` = `a_reg` ^ `b_reg`  |
| `NOT`  | `0x27` | `target_reg reg`         | `target_reg` = `~reg`             |
| `SHL`  | `0x28` | `target_reg a_reg b_reg` | `target_reg` = `a_reg << b_reg`   |
| `SHR`  | `0x29` | `target_reg a_reg b_reg` | `target_reg` = `a_reg >> b_reg`   |

---

## Assembler directives and syntax

The Python assembler compiles `.asm`/`.txt` text files into ROMs padded to exactly 8 KiB. **One instruction per line.**

### Syntax Rules

* **Comments:** Anything following a semicolon `;` is treated as a comment.
* **Labels:** Terminated with a colon (e.g. `main_loop:`). Labels resolve to their absolute byte offsets during assembling.
* **Registers:** Prefixed with an `r` (e.g. `r0` `r1` `r15`). Numbers can be hex (`0x2000`) or decimal (`8192`).
* **Macros:** `#define NAME VALUE` replaces any occurrences of `${NAME}` in subsequent lines.

### Directives

| Directive | Syntax                 | Description                                                      |
|-----------|------------------------|------------------------------------------------------------------|
| `.BYTE`   | `.BYTE 0x01 0x02 255`  | Emits raw byte values                                            |
| `.HEX`    | `.HEX 48656C6C6F`      | Emits a raw hex byte stream (must be of even length)             |
| `.UTF8`   | `.UTF8 "Hello World"`  | Emits raw UTF-8 string bytes                                     |
| `.LUTF8`  | `.LUTF8 "Hello World"` | Emits 4-byte length prefix (big-endian) followed by string bytes |

---

## Example programs

* [`programs/flappy.txt`](programs/flappy.txt) — a full **Flappy Bird** in palette mode: gravity, scrolling pipes with pseudo-random gaps, scoring, and a high score saved to SRAM.
* [`programs/colorbars.txt`](programs/colorbars.txt) — a palette rainbow demo.

---

## Building and running

### Assembling code

```bash
python assembler/main.py programs/flappy.txt programs/flappy.from
```

### Testing with the VM

Prebuilt native executables for several platforms are published on the [Releases](https://github.com/boyninja1555/floorvm/releases) tab.

```bash
# Windows
.\FloorVM.exe programs\flappy.from          # boot a cartridge
.\FloorVM.exe demo.fvd                       # boot a disk (shows the menu)
.\FloorVM.exe programs                       # boot a folder of ROMs (menu)

# Linux/macOS
./FloorVM programs/flappy.from
```

### Development helpers

`tools/simvm.py` is a headless reference implementation of the machine used to test programs and render frames to PNG without SDL — handy for validating games during development.

-moddified by 8BitZac.
