#pragma once
#include "machine.h"

// Runs the boot menu over the currently mounted media, drawing into the
// machine's VRAM. Returns the selected entry index, or -1 if the user quit.
// The renderer must already be initialized.
int bootloader_run(Machine *machine);
