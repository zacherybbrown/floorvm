#include "machine.h"
#include <string.h>

// Private

u32 read_u32(Machine *machine, u32 addr)
{
    return ((u32)machine->ram[addr] << 24) |
           ((u32)machine->ram[addr + 1] << 16) |
           ((u32)machine->ram[addr + 2] << 8) |
           (u32)machine->ram[addr + 3];
}

void write_u32(Machine *machine, u32 addr, u32 value)
{
    machine->ram[addr] = value >> 24;
    machine->ram[addr + 1] = value >> 16;
    machine->ram[addr + 2] = value >> 8;
    machine->ram[addr + 3] = value;
}

byte cp_byte(Machine *machine) // Consume program byte
{
    return machine->ram[machine->pc++];
}

u32 cp_u32(Machine *machine) // Consume program u32
{
    return ((u32)cp_byte(machine) << 24) |
           ((u32)cp_byte(machine) << 16) |
           ((u32)cp_byte(machine) << 8) |
           (u32)cp_byte(machine);
}

// Expands a 3-bit channel (0-7) to a full 8-bit value by bit-replication.
static byte expand3(byte v3)
{
    v3 &= 0x07;
    return (byte)((v3 << 5) | (v3 << 2) | (v3 >> 1));
}

// Expands a 2-bit channel (0-3) to a full 8-bit value by bit-replication.
static byte expand2(byte v2)
{
    v2 &= 0x03;
    return (byte)((v2 << 6) | (v2 << 4) | (v2 << 2) | v2);
}

// Public

Machine machine_init()
{
    return (Machine){0};
}

void machine_load_default_palette(Machine *machine)
{
    // Default palette == RGB332 identity: index i decodes exactly as it would
    // in VMODE_DIRECT (RRRGGGBB). Programs overwrite entries as needed.
    for (int i = 0; i < PALETTE_ENTRIES; i++)
    {
        u32 base = PALETTE_START + (u32)i * 3;
        machine->ram[base + 0] = expand3((byte)(i >> 5));       // Red   (bits 7-5)
        machine->ram[base + 1] = expand3((byte)(i >> 2));       // Green (bits 4-2)
        machine->ram[base + 2] = expand2((byte)i);              // Blue  (bits 1-0)
    }
}

void machine_reset(Machine *machine)
{
    machine->status = MS_OK;
    memset(machine->ram, 0, RAM_SIZE);
    machine->pc = 0;
    memset(machine->registers, 0, sizeof(machine->registers));
    machine_load_default_palette(machine);
}

void machine_step(Machine *machine)
{
    byte opcode = cp_byte(machine);
    switch (opcode)
    {
        // Control

    case OP_HALT: // HALT
    {
        machine->status = MS_KO;
        break;
    }

    case OP_JMP: // JMP target[4]
    {
        machine->pc = cp_u32(machine);
        break;
    }

    case OP_JMPIZ: // JMPIZ target[4] reg
    {
        u32 target = cp_u32(machine);
        if (machine->registers[cp_byte(machine)] == 0)
            machine->pc = target;

        break;
    }

    case OP_JMPNZ: // JMPNZ target[4] reg
    {
        u32 target = cp_u32(machine);
        if (machine->registers[cp_byte(machine)] != 0)
            machine->pc = target;

        break;
    }

        // Memory

    case OP_LOAD32: // LOAD32 addr[4] reg
    {
        u32 addr = cp_u32(machine);
        byte reg = cp_byte(machine);
        machine->registers[reg] = read_u32(machine, addr);
        break;
    }

    case OP_LOAD8: // LOAD8 addr[4] reg
    {
        u32 addr = cp_u32(machine);
        byte reg = cp_byte(machine);
        machine->registers[reg] = machine->ram[addr];
        break;
    }

    case OP_LOAD32P: // LOAD32P addr_reg reg
    {
        byte addr_reg = cp_byte(machine);
        byte reg = cp_byte(machine);
        machine->registers[reg] = read_u32(machine, machine->registers[addr_reg]);
        break;
    }

    case OP_LOAD8P: // LOAD8P addr_reg reg
    {
        byte addr_reg = cp_byte(machine);
        byte reg = cp_byte(machine);
        machine->registers[reg] = machine->ram[machine->registers[addr_reg]];
        break;
    }

    case OP_STORE32: // STORE32 reg addr[4]
    {
        byte reg = cp_byte(machine);
        u32 addr = cp_u32(machine);
        write_u32(machine, addr, machine->registers[reg]);
        break;
    }

    case OP_STORE8: // STORE8 reg addr[4]
    {
        byte reg = cp_byte(machine);
        u32 addr = cp_u32(machine);
        machine->ram[addr] = machine->registers[reg] & 0xFF;
        break;
    }

    case OP_STORE32P: // STORE32P reg addr_reg
    {
        byte reg = cp_byte(machine);
        byte addr_reg = cp_byte(machine);
        write_u32(machine, machine->registers[addr_reg], machine->registers[reg]);
        break;
    }

    case OP_STORE8P: // STORE8P reg addr_reg
    {
        byte reg = cp_byte(machine);
        byte addr_reg = cp_byte(machine);
        machine->ram[machine->registers[addr_reg]] = machine->registers[reg] & 0xFF;
        break;
    }

    case OP_SET32: // SET32 reg value[4]
    {
        machine->registers[cp_byte(machine)] = cp_u32(machine);
        break;
    }

    case OP_SET8: // SET8 reg value
    {
        byte reg = cp_byte(machine);
        byte value = cp_byte(machine);
        machine->registers[reg] = value;
        break;
    }

    case OP_COPY: // COPY reg target_reg
    {
        byte reg = cp_byte(machine);
        byte target_reg = cp_byte(machine);
        machine->registers[target_reg] = machine->registers[reg];
        break;
    }

        // ALU

    case OP_ADD: // ADD target_reg a_reg b_reg
    {
        byte target_reg = cp_byte(machine);
        byte a_reg = cp_byte(machine);
        byte b_reg = cp_byte(machine);
        machine->registers[target_reg] = machine->registers[a_reg] + machine->registers[b_reg];
        break;
    }

    case OP_SUB: // SUB target_reg a_reg b_reg
    {
        byte target_reg = cp_byte(machine);
        byte a_reg = cp_byte(machine);
        byte b_reg = cp_byte(machine);
        machine->registers[target_reg] = machine->registers[a_reg] - machine->registers[b_reg];
        break;
    }

    case OP_MUL: // MUL target_reg a_reg b_reg
    {
        byte target_reg = cp_byte(machine);
        byte a_reg = cp_byte(machine);
        byte b_reg = cp_byte(machine);
        machine->registers[target_reg] = machine->registers[a_reg] * machine->registers[b_reg];
        break;
    }

    case OP_DIV: // DIV target_reg a_reg b_reg
    {
        byte target_reg = cp_byte(machine);
        byte a_reg = cp_byte(machine);
        byte b_reg = cp_byte(machine);
        machine->registers[target_reg] = machine->registers[a_reg] / machine->registers[b_reg];
        break;
    }

    case OP_AND: // AND target_reg a_reg b_reg
    {
        byte target_reg = cp_byte(machine);
        byte a_reg = cp_byte(machine);
        byte b_reg = cp_byte(machine);
        machine->registers[target_reg] = machine->registers[a_reg] & machine->registers[b_reg];
        break;
    }

    case OP_OR: // OR target_reg a_reg b_reg
    {
        byte target_reg = cp_byte(machine);
        byte a_reg = cp_byte(machine);
        byte b_reg = cp_byte(machine);
        machine->registers[target_reg] = machine->registers[a_reg] | machine->registers[b_reg];
        break;
    }

    case OP_XOR: // XOR target_reg a_reg b_reg
    {
        byte target_reg = cp_byte(machine);
        byte a_reg = cp_byte(machine);
        byte b_reg = cp_byte(machine);
        machine->registers[target_reg] = machine->registers[a_reg] ^ machine->registers[b_reg];
        break;
    }

    case OP_NOT: // NOT target_reg reg
    {
        byte target_reg = cp_byte(machine);
        byte reg = cp_byte(machine);
        machine->registers[target_reg] = ~machine->registers[reg];
        break;
    }

    case OP_SHL: // SHL target_reg a_reg b_reg
    {
        byte target_reg = cp_byte(machine);
        byte a_reg = cp_byte(machine);
        byte b_reg = cp_byte(machine);
        machine->registers[target_reg] = machine->registers[a_reg] << machine->registers[b_reg];
        break;
    }

    case OP_SHR: // SHR target_reg a_reg b_reg
    {
        byte target_reg = cp_byte(machine);
        byte a_reg = cp_byte(machine);
        byte b_reg = cp_byte(machine);
        machine->registers[target_reg] = machine->registers[a_reg] >> machine->registers[b_reg];
        break;
    }
    }
}
