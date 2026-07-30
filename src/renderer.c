#include "renderer.h"
#include <SDL3/SDL.h>

static SDL_Window *window = NULL;
static SDL_Renderer *sdl_renderer = NULL;
static SDL_Texture *texture = NULL;

static int scale = 1;

// Expands a 3-bit channel (0-7) to 8 bits by bit-replication.
static inline byte expand3(byte v3)
{
    v3 &= 0x07;
    return (byte)((v3 << 5) | (v3 << 2) | (v3 >> 1));
}

// Expands a 2-bit channel (0-3) to 8 bits by bit-replication.
static inline byte expand2(byte v2)
{
    v2 &= 0x03;
    return (byte)((v2 << 6) | (v2 << 4) | (v2 << 2) | v2);
}

byte renderer_init(int gui_scale)
{
    scale = gui_scale;
    if (!SDL_Init(SDL_INIT_VIDEO))
        return MS_KO;

    window = SDL_CreateWindow("FloorVM", SCREEN_WIDTH * scale, SCREEN_HEIGHT * scale, 0);
    if (!window)
        return MS_KO;

    sdl_renderer = SDL_CreateRenderer(window, NULL);
    if (!sdl_renderer)
        return MS_KO;

    SDL_SetRenderVSync(sdl_renderer, 1);
    texture = SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);
    if (!texture)
        return MS_KO;

    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    return MS_OK;
}

void renderer_frame(const Machine *machine)
{
    static uint32_t pixels[SCREEN_WIDTH * SCREEN_HEIGHT];
    byte mode = machine->ram[VMODE_START];

    for (int i = 0; i < SCREEN_WIDTH * SCREEN_HEIGHT; i++)
    {
        byte value = machine->ram[VRAM_START + i];
        byte r, g, b;

        if (mode == VMODE_PALETTE)
        {
            const byte *entry = &machine->ram[PALETTE_START + (u32)value * 3];
            r = entry[0];
            g = entry[1];
            b = entry[2];
        }
        else // VMODE_DIRECT: RGB332 (RRRGGGBB)
        {
            r = expand3(value >> 5);
            g = expand3(value >> 2);
            b = expand2(value);
        }

        pixels[i] = ((u32)r << 24) | // Red
                    ((u32)g << 16) | // Green
                    ((u32)b << 8) |  // Blue
                    0xFF;            // Alpha
    }

    SDL_UpdateTexture(texture, NULL, pixels, SCREEN_WIDTH * sizeof(uint32_t));
    SDL_RenderClear(sdl_renderer);
    SDL_RenderTexture(sdl_renderer, texture, NULL, NULL);
    SDL_RenderPresent(sdl_renderer);
}

void renderer_cleanup()
{
    if (texture)
        SDL_DestroyTexture(texture);

    if (sdl_renderer)
        SDL_DestroyRenderer(sdl_renderer);

    if (window)
        SDL_DestroyWindow(window);

    SDL_Quit();
}
