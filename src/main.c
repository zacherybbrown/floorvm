#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <SDL3/SDL.h>
#include "util.h"
#include "machine.h"
#include "input.h"
#include "renderer.h"
#include "bootloader.h"

#define GUI_SCALE 6

// Runs a loaded program until it halts or the window is closed.
// Returns 1 if the program halted (return to bootloader), 0 if the window
// was closed (quit the emulator).
static int run_program(Machine *machine)
{
    Uint64 next_frame_time = SDL_GetTicksNS();
    while (machine->status == MS_OK)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            if (event.type == SDL_EVENT_QUIT)
                return 0;

        // ESC halts the current program: returns to the boot menu on
        // multi-program media, or exits for a single cartridge.
        const bool *keys = SDL_GetKeyboardState(NULL);
        if (keys[SDL_SCANCODE_ESCAPE])
            machine->status = MS_KO;

        for (int i = 0; i < CYCLES_PER_FRAME; i++)
        {
            input_poll(machine);
            machine_step(machine);
            if (machine->status != MS_OK)
                break;

            // Latch a completed frame the instant the program presents it, so
            // the display never shows a half-drawn framebuffer (no tearing).
            if (machine->ram[PRESENT_START])
            {
                renderer_present(machine);
                machine->ram[PRESENT_START] = 0;
            }
        }

        // A program requests a persistent save by writing a non-zero byte to
        // the save-control register; we flush SRAM and acknowledge by clearing.
        if (machine->ram[SAVECTL_START])
        {
            media_flush_save(machine);
            machine->ram[SAVECTL_START] = 0;
        }

        renderer_frame(machine);
        next_frame_time += NS_PER_FRAME;
        Uint64 current_time = SDL_GetTicksNS();
        if (next_frame_time > current_time)
            SDL_DelayNS(next_frame_time - current_time);
        else
            next_frame_time = current_time;
    }

    return 1;
}

int main(const int argc, const char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr,
                "Please specify what to boot!\n"
                "\tUsage: %s <file.from | disk.fvd | directory>\n",
                argv[0]);
        return 1;
    }

    if (strcmp(argv[1], ":-dbgfo") == 0)
    {
        printf("Target FPS = %i\n", TARGET_FPS);
        printf("Cycles per frame = %i\n", CYCLES_PER_FRAME);
        printf("Screen = %ix%i\n", SCREEN_WIDTH, SCREEN_HEIGHT);
        printf("Registers count = %i\n", REGISTERS_COUNT);
        printf("RAM section sizes = Program:%i, VRAM:%i, Palette:%i, VMode:%i, Input:%i, SaveCtl:%i, SRAM:%i, User:%i, Present:%i\n",
               PROGRAM_SIZE, VRAM_SIZE, PALETTE_SIZE, VMODE_SIZE, INPUT_SIZE, SAVECTL_SIZE, SRAM_SIZE, USER_SIZE, PRESENT_SIZE);
        printf("RAM start positions = Program:%i, VRAM:%i, Palette:%i, VMode:%i, Input:%i, SaveCtl:%i, SRAM:%i, User:%i, Present:%i\n",
               PROGRAM_START, VRAM_START, PALETTE_START, VMODE_START, INPUT_START, SAVECTL_START, SRAM_START, USER_START, PRESENT_START);
        printf("Total RAM = %i bytes\n", RAM_SIZE);
        return 0;
    }

    Machine machine = machine_init();
    machine_reset(&machine);
    if (renderer_init(GUI_SCALE) != MS_OK)
    {
        fprintf(stderr, "Unable to initialize renderer!\n");
        return 1;
    }

    if (media_mount(argv[1]) != MS_OK)
    {
        renderer_cleanup();
        return 1;
    }

    byte running = 1;
    while (running)
    {
        int index;
        if (media_kind() == MEDIA_CART)
            index = 0; // A single cartridge boots straight into the program.
        else
        {
            renderer_reset_present(); // Menu draws live to VRAM.
            index = bootloader_run(&machine);
            if (index < 0)
                break; // User quit the boot menu.
        }

        if (media_load_entry(&machine, index) != MS_OK)
        {
            fprintf(stderr, "Failed to load selected program!\n");
            break;
        }

        renderer_reset_present(); // Fresh program: show live VRAM until it presents.
        int halted = run_program(&machine);
        media_flush_save(&machine); // Persist final save state on exit.

        if (!halted)
            break; // Window closed -> quit emulator.
        if (media_kind() == MEDIA_CART)
            break; // Nothing to return to for a single cartridge.
        // Otherwise fall through to redisplay the boot menu.
    }

    media_cleanup();
    renderer_cleanup();
    return 0;
}
