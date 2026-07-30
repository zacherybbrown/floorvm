#pragma once
#include "machine.h"

byte renderer_init(int gui_scale);

// Latches the current VRAM into the display buffer (double-buffer present).
// Called when a program signals a completed frame via the PRESENT register.
void renderer_present(const Machine *machine);

// Forgets any prior present, so the next frames show live VRAM again. Call
// when switching programs (or entering the boot menu).
void renderer_reset_present(void);

void renderer_frame(const Machine *machine);

void renderer_cleanup();
