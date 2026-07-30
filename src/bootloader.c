#include "bootloader.h"
#include "renderer.h"
#include "util.h"
#include <SDL3/SDL.h>
#include <string.h>

// ---- Tiny 3x5 bitmap font -------------------------------------------------
// Each glyph is 5 rows; the low 3 bits of each row are the pixels (bit2=left).

static const char FONT_CHARS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.-_:/";

static const u8 FONT_ROWS[][5] = {
    {0, 0, 0, 0, 0}, // (space)
    {2, 5, 7, 5, 5}, // A
    {6, 5, 6, 5, 6}, // B
    {3, 4, 4, 4, 3}, // C
    {6, 5, 5, 5, 6}, // D
    {7, 4, 6, 4, 7}, // E
    {7, 4, 6, 4, 4}, // F
    {3, 4, 5, 5, 3}, // G
    {5, 5, 7, 5, 5}, // H
    {7, 2, 2, 2, 7}, // I
    {1, 1, 1, 5, 2}, // J
    {5, 6, 4, 6, 5}, // K
    {4, 4, 4, 4, 7}, // L
    {5, 7, 7, 5, 5}, // M
    {5, 7, 7, 7, 5}, // N
    {2, 5, 5, 5, 2}, // O
    {6, 5, 6, 4, 4}, // P
    {2, 5, 5, 6, 3}, // Q
    {6, 5, 6, 5, 5}, // R
    {3, 4, 2, 1, 6}, // S
    {7, 2, 2, 2, 2}, // T
    {5, 5, 5, 5, 7}, // U
    {5, 5, 5, 5, 2}, // V
    {5, 5, 7, 7, 5}, // W
    {5, 5, 2, 5, 5}, // X
    {5, 5, 2, 2, 2}, // Y
    {7, 1, 2, 4, 7}, // Z
    {7, 5, 5, 5, 7}, // 0
    {2, 6, 2, 2, 7}, // 1
    {6, 1, 2, 4, 7}, // 2
    {7, 1, 3, 1, 7}, // 3
    {5, 5, 7, 1, 1}, // 4
    {7, 4, 7, 1, 7}, // 5
    {3, 4, 7, 5, 7}, // 6
    {7, 1, 2, 2, 2}, // 7
    {7, 5, 7, 5, 7}, // 8
    {7, 5, 7, 1, 6}, // 9
    {0, 0, 0, 0, 2}, // .
    {0, 0, 7, 0, 0}, // -
    {0, 0, 0, 0, 7}, // _
    {0, 2, 0, 2, 0}, // :
    {1, 1, 2, 4, 4}, // /
};

// Direct-mode (RGB332) colors used by the menu.
#define COL_BG 0x05     // dark teal
#define COL_TITLE 0xFF  // white
#define COL_HINT 0x92   // muted blue-gray
#define COL_ITEM 0xB6   // light gray
#define COL_SEL 0xFC    // bright yellow
#define COL_SELBG 0x28  // dim highlight bar

#define CHAR_W 4 // 3px glyph + 1px gap
#define ROW_STEP 8
#define LIST_TOP 24
#define VISIBLE_ROWS 9

static void put_pixel(Machine *m, int x, int y, byte color)
{
    if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT)
        m->ram[VRAM_START + y * SCREEN_WIDTH + x] = color;
}

static void fill_rect(Machine *m, int x0, int y0, int w, int h, byte color)
{
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++)
            put_pixel(m, x, y, color);
}

static void draw_char(Machine *m, int px, int py, char c, byte color)
{
    if (c >= 'a' && c <= 'z')
        c -= 32;

    int glyph = 0; // default: space
    for (int i = 0; FONT_CHARS[i]; i++)
        if (FONT_CHARS[i] == c)
        {
            glyph = i;
            break;
        }

    const u8 *rows = FONT_ROWS[glyph];
    for (int ry = 0; ry < 5; ry++)
        for (int rx = 0; rx < 3; rx++)
            if (rows[ry] & (1 << (2 - rx)))
                put_pixel(m, px + rx, py + ry, color);
}

static void draw_text(Machine *m, int px, int py, const char *text, byte color)
{
    for (int i = 0; text[i]; i++)
        draw_char(m, px + i * CHAR_W, py, text[i], color);
}

static const char *kind_label(void)
{
    switch (media_kind())
    {
    case MEDIA_DISK:
        return "DISK";
    case MEDIA_DIR:
        return "FOLDER";
    case MEDIA_CART:
        return "CARTRIDGE";
    default:
        return "";
    }
}

static void draw_menu(Machine *m, int selection, int count)
{
    fill_rect(m, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COL_BG);

    draw_text(m, 24, 4, "FLOORVM", COL_TITLE);
    draw_text(m, 2, 13, kind_label(), COL_HINT);
    draw_text(m, 2, SCREEN_HEIGHT - 6, "UP DN  A-RUN", COL_HINT);

    int first = 0;
    if (count > VISIBLE_ROWS)
    {
        first = selection - VISIBLE_ROWS / 2;
        if (first < 0)
            first = 0;
        if (first > count - VISIBLE_ROWS)
            first = count - VISIBLE_ROWS;
    }

    int last = first + VISIBLE_ROWS;
    if (last > count)
        last = count;

    for (int i = first; i < last; i++)
    {
        int y = LIST_TOP + (i - first) * ROW_STEP;
        byte color = COL_ITEM;
        if (i == selection)
        {
            fill_rect(m, 0, y - 1, SCREEN_WIDTH, 7, COL_SELBG);
            color = COL_SEL;
            draw_char(m, 1, y, '>', color); // '>' falls back to space (harmless marker)
        }

        const char *name = media_entry_name(i);
        if (name)
            draw_text(m, 6, y, name, color);
    }
}

int bootloader_run(Machine *machine)
{
    int count = media_entry_count();
    if (count <= 0)
        return -1;

    machine_reset(machine);
    machine->ram[VMODE_START] = VMODE_DIRECT;

    int selection = 0;
    bool prev_up = false, prev_down = false, prev_sel = false, prev_esc = true;
    Uint64 next_frame_time = SDL_GetTicksNS();

    for (;;)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            if (event.type == SDL_EVENT_QUIT)
                return -1;

        const bool *ks = SDL_GetKeyboardState(NULL);
        bool esc = ks[SDL_SCANCODE_ESCAPE];
        if (esc && !prev_esc) // Edge-detected so a held ESC from a game doesn't bounce out
            return -1;
        prev_esc = esc;

        bool up = ks[SDL_SCANCODE_UP];
        bool down = ks[SDL_SCANCODE_DOWN];
        bool sel = ks[SDL_SCANCODE_RETURN] || ks[SDL_SCANCODE_A];

        if (up && !prev_up)
            selection = (selection - 1 + count) % count;
        if (down && !prev_down)
            selection = (selection + 1) % count;
        if (sel && !prev_sel)
            return selection;

        prev_up = up;
        prev_down = down;
        prev_sel = sel;

        draw_menu(machine, selection, count);
        renderer_frame(machine);

        next_frame_time += NS_PER_FRAME;
        Uint64 now = SDL_GetTicksNS();
        if (next_frame_time > now)
            SDL_DelayNS(next_frame_time - now);
        else
            next_frame_time = now;
    }
}
