#include "boot_anim.h"
#include "display.h"
#include "idle_game.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define W 480u
#define H 854u
#define TILE 0x2104u
#define INK 0xffffu

static uint16_t screen[H][W], anim[H][W], game[H][W];
static uint32_t canvas, shown, rects, copies, blits;
static unsigned fail_canvas_every;
static unsigned canvas_calls;

static uint16_t (*buffer(uint32_t address))[W]
{
    if (address == 0) {
        return screen;
    }
    if (address == 0x400000u) {
        return game;
    }
    assert(address == BOOT_ANIM_CANVAS);
    return anim;
}

uint16_t display_width(void)
{
    return W;
}

uint16_t display_height(void)
{
    return H;
}

bool display_set_canvas(uint32_t address)
{
    if (fail_canvas_every && ++canvas_calls % fail_canvas_every == 0) {
        return false;
    }
    (void)buffer(address);
    canvas = address;
    return true;
}

bool display_show(uint32_t address)
{
    (void)buffer(address);
    shown = address;
    return true;
}

bool display_draw_rect(
    uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color, bool fill)
{
    assert(fill);
    assert(x0 <= x1 && y0 <= y1 && x1 < W && y1 < H);
    uint16_t(*b)[W] = buffer(canvas);
    for (unsigned y = y0; y <= y1; ++y) {
        for (unsigned x = x0; x <= x1; ++x) {
            b[y][x] = color;
        }
    }
    rects++;
    return true;
}

bool display_blit_rgb565(
    uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *pixels, uint16_t stride)
{
    assert(pixels && w && h && stride >= w && x + w <= W && y + h <= H);
    uint16_t(*b)[W] = buffer(canvas);
    for (unsigned row = 0; row < h; ++row) {
        memcpy(&b[y + row][x], pixels + row * stride, w * 2u);
    }
    blits++;
    return true;
}

bool display_copy(uint32_t src,
                  uint16_t sx,
                  uint16_t sy,
                  uint32_t dst,
                  uint16_t dx,
                  uint16_t dy,
                  uint16_t w,
                  uint16_t h)
{
    assert(w && h && sx + w <= W && sy + h <= H && dx + w <= W && dy + h <= H);
    uint16_t(*s)[W] = buffer(src), (*d)[W] = buffer(dst);
    for (unsigned y = 0; y < h; ++y) {
        memmove(&d[dy + y][dx], &s[sy + y][sx], w * 2u);
    }
    copies++;
    return true;
}

static uint16_t phys(uint16_t (*b)[W], unsigned u, unsigned v)
{
    return b[853u - u][v];
}

static uint16_t visible(unsigned u, unsigned v)
{
    return phys(buffer(shown), u, v);
}

static unsigned cell_u(unsigned cell)
{
    return cell % 6u == 5u ? 759u : 21u + (cell % 6u) * 148u;
}

static unsigned cell_v(unsigned cell)
{
    return 38u + (cell / 6u) * 149u;
}

static unsigned cell_w(unsigned cell)
{
    return cell % 6u == 5u ? 85u : 103u;
}

static void center(unsigned cell, unsigned *u, unsigned *v)
{
    *u = cell_u(cell) + cell_w(cell) / 2u;
    *v = cell_v(cell) + 51u;
}

static unsigned ink_count(unsigned cell)
{
    unsigned n = 0;
    for (unsigned dv = 0; dv < 103u; ++dv) {
        for (unsigned du = 0; du < cell_w(cell); ++du) {
            n += visible(cell_u(cell) + du, cell_v(cell) + dv) == INK;
        }
    }
    return n;
}

static void reset(void)
{
    memset(screen, 0x5a, sizeof screen);
    memset(anim, 0xa5, sizeof anim);
    canvas = shown = 0;
    rects = copies = blits = 0;
    fail_canvas_every = canvas_calls = 0;
}

static void host_draws(void)
{
    for (unsigned y = 0; y < H; ++y) {
        for (unsigned x = 0; x < W; ++x) {
            screen[y][x] = 0;
        }
    }
    for (unsigned cell = 0; cell < 18; ++cell) {
        for (unsigned v = cell_v(cell); v < cell_v(cell) + 103u; ++v) {
            for (unsigned u = cell_u(cell); u < cell_u(cell) + cell_w(cell); ++u) {
                screen[853u - u][v] = (uint16_t)(0x0400u + cell);
            }
        }
    }
}

static void dump(FILE *out)
{
    static uint8_t row[854 * 3];
    for (unsigned v = 0; v < W; ++v) {
        for (unsigned u = 0; u < H; ++u) {
            uint16_t c = visible(u, v);
            row[u * 3] = (uint8_t)(((c >> 11) & 31u) << 3);
            row[u * 3 + 1] = (uint8_t)(((c >> 5) & 63u) << 2);
            row[u * 3 + 2] = (uint8_t)((c & 31u) << 3);
        }
        fwrite(row, 1, sizeof row, out);
    }
}

static void run(uint32_t from, uint32_t to, uint32_t step, FILE *out)
{
    for (uint32_t d = 0; d < to - from; d += step) {
        uint32_t t = from + d;
        if (boot_anim_step(t)) {
            assert(canvas == BOOT_ANIM_CANVAS);
            canvas = 0;
        }
        if (out && t % 40u == 0) {
            dump(out);
        }
    }
}

static void test_intro_order_and_text(void)
{
    reset();
    const uint32_t t0 = 0xfffff000u;
    assert(boot_anim_start(t0));
    assert(!boot_anim_waiting());
    assert(shown == BOOT_ANIM_CANVAS && canvas == 0 && boot_anim_active());
    assert(visible(0, 0) == 0 && screen[0][0] == 0);
    unsigned u, v;
    run(t0, t0 + 370u, 4, 0);
    center(11, &u, &v);
    assert(visible(u, v) == TILE);
    center(10, &u, &v);
    assert(visible(u, v) == 0);
    center(5, &u, &v);
    assert(visible(u, v) == TILE);
    run(t0 + 370u, t0 + 3500u, 4, 0);
    assert(boot_anim_waiting());
    assert(boot_anim_active() && shown == BOOT_ANIM_CANVAS);
    for (unsigned cell = 16; cell < 18; ++cell) {
        for (unsigned dv = 0; dv < 103; ++dv) {
            for (unsigned du = 0; du < cell_w(cell); ++du) {
                assert(visible(cell_u(cell) + du, cell_v(cell) + dv) == TILE);
            }
        }
    }
    static const unsigned text_cells[] = {0, 1, 2, 3, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    for (unsigned i = 0; i < sizeof text_cells / sizeof text_cells[0]; ++i) {
        assert(ink_count(text_cells[i]) > 150u);
    }
    assert(ink_count(4) == 0 && ink_count(5) == 0);
    center(0, &u, &v);
    assert(visible(u, v) == TILE);
    center(6, &u, &v);
    assert(visible(cell_u(6), cell_v(6)) == TILE);
    run(t0 + 3500u, t0 + 20000u, 7, 0);
    assert(boot_anim_active() && shown == BOOT_ANIM_CANVAS);
}

static void test_continuous_side(void)
{
    reset();
    assert(boot_anim_start(0));
    run(0, 400, 4, 0);
    unsigned filled = 0;
    bool reached_bottom = false;
    for (unsigned v = 35; v < 441; ++v) {
        bool row_filled = visible(759, v) == TILE;
        if (!row_filled) {
            reached_bottom = true;
        } else {
            assert(!reached_bottom);
            filled++;
        }
        for (unsigned u = 759; u < 844; ++u) {
            assert(visible(u, v) == (row_filled ? TILE : 0));
        }
    }
    assert(filled > 152 && filled < 301);
    run(400, 3500, 4, 0);
    static uint16_t side_before[406][85];
    for (unsigned v = 35; v < 441; ++v) {
        assert(visible(759, v) == TILE && visible(843, v) == TILE);
        for (unsigned u = 759; u < 844; ++u) {
            side_before[v - 35][u - 759] = visible(u, v);
        }
    }
    host_draws();
    for (unsigned v = 35; v < 441; ++v) {
        for (unsigned u = 759; u < 844; ++u) {
            screen[853u - u][v] = 0x1234u;
        }
    }
    boot_anim_host_drew(3500);
    run(3500, 4100, 4, 0);
    filled = 0;
    reached_bottom = false;
    for (unsigned v = 35; v < 441; ++v) {
        bool row_revealed = visible(759, v) == 0x1234u;
        if (!row_revealed) {
            reached_bottom = true;
        } else {
            assert(!reached_bottom);
            filled++;
        }
        for (unsigned u = 759; u < 844; ++u) {
            assert(visible(u, v) == (row_revealed ? 0x1234u : side_before[v - 35][u - 759]));
        }
    }
    assert(filled > 0 && filled < 152);
    run(4100, 6000, 4, 0);
    assert(!boot_anim_active());
    for (unsigned v = 35; v < 441; ++v) {
        for (unsigned u = 759; u < 844; ++u) {
            assert(phys(anim, u, v) == 0x1234u);
        }
    }
}

static void test_host_reveal(FILE *out)
{
    reset();
    assert(boot_anim_start(0));
    run(0, 1500, 4, out);
    host_draws();
    boot_anim_host_drew(1500);
    run(1500, 2000, 4, out);
    assert(shown == BOOT_ANIM_CANVAS && boot_anim_active());
    run(2000, 3300, 4, out);
    assert(!boot_anim_active() && shown == 0 && canvas == 0);
    run(3300, 6000, 4, out);
    for (unsigned cell = 0; cell < 18; ++cell) {
        unsigned u, v;
        center(cell, &u, &v);
        assert(phys(anim, u, v) == 0x0400u + cell);
        assert(phys(anim, cell_u(cell), cell_v(cell)) == 0x0400u + cell);
        assert(phys(anim, cell_u(cell) + cell_w(cell) - 1u, cell_v(cell) + 102u) == 0x0400u + cell);
    }
    uint32_t before = rects + copies + blits;
    run(6000, 7000, 4, 0);
    assert(rects + copies + blits == before);
    boot_anim_host_drew(7000);
    assert(!boot_anim_active());
}

static void test_waits_for_intro(void)
{
    reset();
    assert(boot_anim_start(100));
    assert(boot_anim_due(100));
    assert(boot_anim_step(100) && !boot_anim_due(105) && boot_anim_due(115));
    canvas = 0;
    host_draws();
    boot_anim_host_drew(100);
    run(100, 700, 4, 0);
    assert(shown == BOOT_ANIM_CANVAS && boot_anim_active() && !boot_anim_waiting());
    run(700, 2400, 4, 0);
    assert(!boot_anim_active() && shown == 0);
}

static void test_game_host_handoff(void)
{
    reset();
    idle_game_reset();
    assert(boot_anim_start(0));
    run(0, 3500, 4, 0);
    assert(boot_anim_waiting());
    host_draws();
    assert(!idle_game_step(3500, 0));
    assert(idle_game_step(3540, 1));
    assert(canvas == 0x400000u && shown == BOOT_ANIM_CANVAS);
    for (unsigned cell = 0; cell < 18; ++cell) {
        unsigned u, v;
        center(cell, &u, &v);
        assert(phys(screen, u, v) == 0x0400u + cell);
    }
    canvas = 0;
    boot_anim_host_drew(3600);
    assert(!boot_anim_waiting());
    idle_game_suspend();
    run(3600, 6000, 4, 0);
    assert(!boot_anim_active() && shown == 0);
    for (unsigned cell = 0; cell < 18; ++cell) {
        unsigned u, v;
        center(cell, &u, &v);
        assert(phys(anim, u, v) == 0x0400u + cell);
    }
}

static void test_survives_busy_display(void)
{
    reset();
    assert(boot_anim_start(0));
    fail_canvas_every = 3;
    run(0, 4000, 5, 0);
    host_draws();
    boot_anim_host_drew(4000);
    run(4000, 8000, 5, 0);
    assert(!boot_anim_active() && shown == 0);
    unsigned u, v;
    center(7, &u, &v);
    assert(phys(anim, u, v) == 0x0407u);
}

int main(int argc, char **argv)
{
    FILE *out = argc > 1 ? fopen(argv[1], "wb") : 0;
    test_intro_order_and_text();
    test_continuous_side();
    test_host_reveal(out);
    test_waits_for_intro();
    test_game_host_handoff();
    test_survives_busy_display();
    if (out) {
        fclose(out);
    }
    puts("boot_anim_test: ok");
    return 0;
}
