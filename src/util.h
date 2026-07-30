#pragma once
#include "machine.h"

#define MEDIA_MAX_ENTRIES 64
#define MEDIA_NAME_MAX 33 // 32 stored chars + null terminator

char *fv_strdup(const char *string);

// Media kinds FloorVM can boot from.
typedef enum
{
    MEDIA_NONE = 0,
    MEDIA_CART = 1, // A single .from ROM (save -> "<path>.sav" sidecar / SRAM)
    MEDIA_DIR = 2,  // A directory of .from ROMs (each save -> its own sidecar)
    MEDIA_DISK = 3, // A .fvd disk image: many ROMs + per-entry save areas
} MediaKind;

// Mounts a media source given a filesystem path. Auto-detects:
//   *.fvd            -> disk image
//   a directory      -> directory of .from ROMs
//   anything else    -> single cartridge ROM
// Returns MS_OK on success. Populates the entry list for the bootloader.
byte media_mount(const char *path);

MediaKind media_kind(void);

// Number of selectable programs on the mounted media.
int media_entry_count(void);

// Display name of entry `index` (NULL if out of range).
const char *media_entry_name(int index);

// Resets the machine, loads entry `index`'s ROM into the program region, and
// loads that entry's persistent save into SRAM. Returns MS_OK on success.
byte media_load_entry(Machine *machine, int index);

// Reloads the currently booted entry (soft reset). Returns MS_OK on success.
byte media_reload(Machine *machine);

// Flushes the current entry's SRAM back to its backing store (sidecar file for
// cart/dir, in-image save area for a disk). No-op if nothing is booted.
void media_flush_save(Machine *machine);

// Releases memory held by the mounted media.
void media_cleanup(void);
