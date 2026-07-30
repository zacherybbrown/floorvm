#include "util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <SDL3/SDL.h>

char *fv_strdup(const char *string)
{
    if (!string)
        return NULL;

    size_t len = strlen(string) + 1;
    char *dest = malloc(len);
    if (dest)
        memcpy(dest, string, len);

    return dest;
}

// ---- Media state ---------------------------------------------------------

static MediaKind kind = MEDIA_NONE;
static char *disk_path = NULL; // Backing .fvd path (MEDIA_DISK only)

static int entry_count = 0;
static char entry_names[MEDIA_MAX_ENTRIES][MEDIA_NAME_MAX];
static char *entry_files[MEDIA_MAX_ENTRIES]; // ROM file path (cart/dir); NULL for disk
static u32 entry_rom_off[MEDIA_MAX_ENTRIES];
static u32 entry_rom_size[MEDIA_MAX_ENTRIES];
static u32 entry_save_off[MEDIA_MAX_ENTRIES];
static u32 entry_save_size[MEDIA_MAX_ENTRIES];

static int current_entry = -1;

// ---- Helpers -------------------------------------------------------------

static u32 read_be_u32(const byte *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

// Copies a display name out of an arbitrary source string (truncated to fit).
static void set_entry_name(int index, const char *name)
{
    strncpy(entry_names[index], name, MEDIA_NAME_MAX - 1);
    entry_names[index][MEDIA_NAME_MAX - 1] = '\0';
}

// Builds "<rom>.sav" for a cart/dir entry. Caller frees.
static char *sidecar_path(const char *rom_path)
{
    size_t len = strlen(rom_path);
    char *out = malloc(len + 5);
    if (!out)
        return NULL;

    memcpy(out, rom_path, len);
    memcpy(out + len, ".sav", 5);
    return out;
}

static void reset_state(void)
{
    for (int i = 0; i < entry_count; i++)
    {
        free(entry_files[i]);
        entry_files[i] = NULL;
    }

    free(disk_path);
    disk_path = NULL;
    kind = MEDIA_NONE;
    entry_count = 0;
    current_entry = -1;
}

// ---- Mounting ------------------------------------------------------------

static byte mount_cart(const char *path)
{
    kind = MEDIA_CART;
    entry_count = 1;
    entry_files[0] = fv_strdup(path);

    // Display name = filename without directory prefix.
    const char *slash = strrchr(path, '/');
    const char *back = strrchr(path, '\\');
    if (back > slash)
        slash = back;

    set_entry_name(0, slash ? slash + 1 : path);
    return MS_OK;
}

static byte mount_dir(const char *path)
{
    int count = 0;
    char **files = SDL_GlobDirectory(path, "*.from", 0, &count);
    if (!files)
    {
        fprintf(stderr, "Could not read directory: %s\n", path);
        return MS_KO;
    }

    kind = MEDIA_DIR;
    for (int i = 0; i < count && entry_count < MEDIA_MAX_ENTRIES; i++)
    {
        size_t need = strlen(path) + 1 + strlen(files[i]) + 1;
        char *full = malloc(need);
        if (!full)
            continue;

        snprintf(full, need, "%s/%s", path, files[i]);
        entry_files[entry_count] = full;
        set_entry_name(entry_count, files[i]);
        entry_count++;
    }

    SDL_free(files);
    if (entry_count == 0)
    {
        fprintf(stderr, "No .from ROMs found in directory: %s\n", path);
        return MS_KO;
    }

    return MS_OK;
}

static byte mount_disk(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (!file)
    {
        fprintf(stderr, "Disk image does not exist: %s\n", path);
        return MS_KO;
    }

    byte header[8];
    if (fread(header, 1, 8, file) != 8 || memcmp(header, "FVD1", 4) != 0)
    {
        fprintf(stderr, "Not a valid FloorVM disk image (bad magic): %s\n", path);
        fclose(file);
        return MS_KO;
    }

    u32 count = read_be_u32(header + 4);
    if (count > MEDIA_MAX_ENTRIES)
        count = MEDIA_MAX_ENTRIES;

    kind = MEDIA_DISK;
    disk_path = fv_strdup(path);
    for (u32 i = 0; i < count; i++)
    {
        byte record[48];
        if (fread(record, 1, 48, file) != 48)
        {
            fprintf(stderr, "Disk image truncated in directory table: %s\n", path);
            fclose(file);
            reset_state();
            return MS_KO;
        }

        char name[33];
        memcpy(name, record, 32);
        name[32] = '\0';
        set_entry_name((int)i, name);
        entry_files[i] = NULL;
        entry_rom_off[i] = read_be_u32(record + 32);
        entry_rom_size[i] = read_be_u32(record + 36);
        entry_save_off[i] = read_be_u32(record + 40);
        entry_save_size[i] = read_be_u32(record + 44);
        entry_count++;
    }

    fclose(file);
    if (entry_count == 0)
    {
        fprintf(stderr, "Disk image contains no programs: %s\n", path);
        return MS_KO;
    }

    return MS_OK;
}

byte media_mount(const char *path)
{
    reset_state();
    if (!path)
        return MS_KO;

    SDL_PathInfo info;
    if (SDL_GetPathInfo(path, &info) && info.type == SDL_PATHTYPE_DIRECTORY)
        return mount_dir(path);

    const char *dot = strrchr(path, '.');
    if (dot && SDL_strcasecmp(dot, ".fvd") == 0)
        return mount_disk(path);

    return mount_cart(path);
}

// ---- Queries -------------------------------------------------------------

MediaKind media_kind(void) { return kind; }

int media_entry_count(void) { return entry_count; }

const char *media_entry_name(int index)
{
    if (index < 0 || index >= entry_count)
        return NULL;

    return entry_names[index];
}

// ---- Loading & saving ----------------------------------------------------

static byte load_rom(Machine *machine, int index)
{
    if (entry_files[index]) // Cart / directory: whole file is the ROM
    {
        FILE *file = fopen(entry_files[index], "rb");
        if (!file)
        {
            fprintf(stderr, "ROM does not exist: %s\n", entry_files[index]);
            return MS_KO;
        }

        fseek(file, 0, SEEK_END);
        long size = ftell(file);
        rewind(file);
        if (size > PROGRAM_SIZE)
        {
            fprintf(stderr, "ROM too large (%ld bytes), max %i!\n", size, PROGRAM_SIZE);
            fclose(file);
            return MS_KO;
        }

        fread(machine->ram, sizeof(byte), PROGRAM_SIZE, file);
        fclose(file);
        return MS_OK;
    }

    // Disk: read the entry's ROM blob out of the image.
    FILE *file = fopen(disk_path, "rb");
    if (!file)
        return MS_KO;

    u32 size = entry_rom_size[index];
    if (size > PROGRAM_SIZE)
        size = PROGRAM_SIZE;

    fseek(file, (long)entry_rom_off[index], SEEK_SET);
    fread(machine->ram, sizeof(byte), size, file);
    fclose(file);
    return MS_OK;
}

static void load_save(Machine *machine, int index)
{
    if (entry_files[index]) // Cart / directory: sidecar file
    {
        char *sav = sidecar_path(entry_files[index]);
        if (!sav)
            return;

        FILE *file = fopen(sav, "rb");
        if (file)
        {
            fread(&machine->ram[SRAM_START], sizeof(byte), SRAM_SIZE, file);
            fclose(file);
        }

        free(sav);
        return;
    }

    // Disk: read the entry's save area.
    FILE *file = fopen(disk_path, "rb");
    if (!file)
        return;

    u32 size = entry_save_size[index];
    if (size > SRAM_SIZE)
        size = SRAM_SIZE;

    fseek(file, (long)entry_save_off[index], SEEK_SET);
    fread(&machine->ram[SRAM_START], sizeof(byte), size, file);
    fclose(file);
}

byte media_load_entry(Machine *machine, int index)
{
    if (index < 0 || index >= entry_count)
        return MS_KO;

    machine_reset(machine);
    if (load_rom(machine, index) != MS_OK)
        return MS_KO;

    load_save(machine, index);
    current_entry = index;
    return MS_OK;
}

byte media_reload(Machine *machine)
{
    if (current_entry < 0)
        return MS_KO;

    return media_load_entry(machine, current_entry);
}

void media_flush_save(Machine *machine)
{
    if (current_entry < 0)
        return;

    if (entry_files[current_entry]) // Cart / directory: write sidecar
    {
        char *sav = sidecar_path(entry_files[current_entry]);
        if (!sav)
            return;

        FILE *file = fopen(sav, "wb");
        if (file)
        {
            fwrite(&machine->ram[SRAM_START], sizeof(byte), SRAM_SIZE, file);
            fclose(file);
        }
        else
            fprintf(stderr, "Could not write save file: %s\n", sav);

        free(sav);
        return;
    }

    // Disk: write back into the image's save area for this entry.
    u32 size = entry_save_size[current_entry];
    if (size > SRAM_SIZE)
        size = SRAM_SIZE;

    if (size == 0)
        return;

    FILE *file = fopen(disk_path, "r+b");
    if (!file)
    {
        fprintf(stderr, "Could not open disk image for saving: %s\n", disk_path);
        return;
    }

    fseek(file, (long)entry_save_off[current_entry], SEEK_SET);
    fwrite(&machine->ram[SRAM_START], sizeof(byte), size, file);
    fclose(file);
}

void media_cleanup(void)
{
    reset_state();
}
