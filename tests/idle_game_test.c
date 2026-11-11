#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/idle_game.c"

static uint16_t pixels[854][480];
static uint16_t presented[854][480];
static uint32_t canvas, draws, presents;
static bool fail_draw, fail_present;
static uint32_t fail_rect_after;

bool display_set_canvas(uint32_t address)
{
    assert(address == GAME_CANVAS);
    canvas = address;
    return !fail_draw;
}

bool display_draw_rect(
    uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color, bool fill)
{
    assert(canvas == GAME_CANVAS && fill);
    assert(x0 <= x1 && y0 <= y1 && x1 < 480 && y1 < 854);
    for (unsigned y = y0; y <= y1; ++y) {
        for (unsigned x = x0; x <= x1; ++x) {
            pixels[y][x] = color;
        }
    }
    draws++;
    return !fail_draw && (!fail_rect_after || draws < fail_rect_after);
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
    assert(src == GAME_CANVAS && dst == BOOT_ANIM_CANVAS);
    assert(sx == 0 && sy == 0 && dx == 0 && dy == 0 && w == 480 && h == 854);
    if (fail_present) {
        return false;
    }
    memcpy(presented, pixels, sizeof presented);
    presents++;
    return true;
}

static void test_activation_and_restart(void)
{
    idle_game_reset();
    draws = 0;
    assert(!idle_game_step(0, 1));
    assert(!idle_game_step(100, 1));
    assert(!active && draws == 0);
    assert(!idle_game_step(120, 0));
    assert(idle_game_step(160, 2));
    assert(active && !dead && draws > 0 && height == 0);
    for (unsigned t = 200; t < 6000; t += FRAME_MS) {
        (void)idle_game_step(t, 2);
    }
    assert(dead && score > 0 && best == score);
    uint32_t high = best;
    uint32_t before = draws;
    assert(!idle_game_step(6100, 2));
    assert(draws == before);
    assert(!idle_game_step(6140, 0));
    assert(idle_game_step(6180, 4));
    assert(!dead && score == 0 && best == high);
    idle_game_suspend();
    before = draws;
    assert(!idle_game_step(6220, 4));
    assert(!idle_game_step(6260, 4));
    assert(!active && draws == before);
}

static void test_jump_and_obstacles(void)
{
    idle_game_reset();
    restart(1);
    obstacle_x = 150;
    pending_jump = true;
    for (unsigned i = 0; i < 23; ++i) {
        tick();
        assert(!dead);
        assert(height >= 0 && height <= 176);
    }
    assert(height == 0 && obstacle_x + obstacle_w < PLAYER_X);
    unsigned passed = 0;
    int previous_x = obstacle_x;
    for (unsigned i = 0; i < 6000 && !dead; ++i) {
        int speed = (int)((speed_q4() + 15u) >> 4);
        if (height == 0 && obstacle_x > PLAYER_X && obstacle_x < PLAYER_X + PLAYER_W + speed * 8) {
            pending_jump = true;
        }
        tick();
        if (obstacle_x > previous_x) {
            passed++;
        }
        previous_x = obstacle_x;
        assert(obstacle_h >= 24 && obstacle_h <= 48);
        assert(obstacle_w >= 16 && obstacle_w <= 44);
    }
    assert(!dead && passed > 40 && score >= 1000);
    height = velocity = 0;
    obstacle_x = PLAYER_X + PLAYER_W;
    tick();
    assert(dead);
}

static void test_timing_and_retry(void)
{
    idle_game_reset();
    assert(!idle_game_step(0xfffffff0u, 0));
    assert(idle_game_step(0xfffffff1u, 1));
    uint32_t before = ticks;
    assert(!idle_game_step(5, 0));
    assert(idle_game_step(25, 1));
    assert(ticks == before + 1 && height > 0);
    assert(idle_game_step(1000000, 0));
    assert(ticks == before + 2);
    dead = true;
    fail_draw = true;
    redraw = true;
    assert(idle_game_step(1000040, 0) && redraw);
    fail_draw = false;
    fail_present = true;
    assert(idle_game_step(1000080, 0) && redraw);
    fail_present = false;
    assert(idle_game_step(1000120, 0) && !redraw);
    assert(!idle_game_step(1000160, 0));
}

static void test_clipping_and_presentation(void)
{
    memset(pixels, 0, sizeof pixels);
    memset(presented, 0x12, sizeof presented);
    canvas = GAME_CANVAS;
    uint32_t before = draws;
    assert(rect(WIDTH + 80, 200, 20, 40, FG));
    assert(rect(-40, 200, 20, 40, FG));
    assert(draws == before);
    assert(rect(WIDTH - 4, 255, 16, 26, FG));
    for (unsigned v = 0; v < 480; ++v) {
        for (unsigned u = 759; u < 844; ++u) {
            assert(pixels[853 - u][v] == 0);
        }
    }
    idle_game_reset();
    assert(!idle_game_step(0, 0));
    fail_draw = true;
    before = presents;
    assert(idle_game_step(40, 1));
    assert(presents == before && presented[0][0] == 0x1212);
    fail_draw = false;
    fail_rect_after = draws + 5;
    assert(idle_game_step(80, 0));
    assert(presents == before && presented[0][0] == 0x1212);
    fail_rect_after = 0;
    assert(idle_game_step(120, 0));
    assert(presents == before + 1);
    assert(memcmp(presented, pixels, sizeof pixels) == 0);
    for (unsigned col = 0; col < 5; ++col) {
        for (unsigned u = 21 + col * 148; u < 124 + col * 148; ++u) {
            assert(presented[853 - u][289] == BG);
        }
    }
}

static void test_randomness_and_playability(void)
{
    unsigned kinds = 0;
    int min_gap = 1000, max_gap = 0;
    for (unsigned seed = 1; seed < 80; ++seed) {
        restart(seed * 1543u);
        for (unsigned n = 0; n < 20; ++n) {
            spawn();
            kinds |= 1u << obstacle_kind;
            int gap = (int)obstacle_x - WIDTH;
            if (gap < min_gap) {
                min_gap = gap;
            }
            if (gap > max_gap) {
                max_gap = gap;
            }
        }
        for (unsigned i = 0; i < 3500; ++i) {
            int speed = (int)((speed_q4() + 15u) >> 4);
            if (height == 0 && obstacle_x > PLAYER_X &&
                obstacle_x < PLAYER_X + PLAYER_W + speed * 8) {
                pending_jump = true;
            }
            tick();
            assert(!dead);
        }
        assert(speed_q4() == 256u);
    }
    assert(kinds == 15u && min_gap < 20 && max_gap > 260);
}

static void dump(const char *path)
{
    FILE *out = fopen(path, "wb");
    assert(out);
    fprintf(out, "P6\n854 480\n255\n");
    for (unsigned v = 0; v < 480; ++v) {
        for (unsigned u = 0; u < 854; ++u) {
            uint16_t c = pixels[853 - u][v];
            unsigned char rgb[] = {(unsigned char)(((c >> 11) & 31) * 255 / 31),
                                   (unsigned char)(((c >> 5) & 63) * 255 / 63),
                                   (unsigned char)((c & 31) * 255 / 31)};
            fwrite(rgb, 1, 3, out);
        }
    }
    fclose(out);
}

int main(int argc, char **argv)
{
    test_activation_and_restart();
    test_jump_and_obstacles();
    test_timing_and_retry();
    test_clipping_and_presentation();
    test_randomness_and_playability();
    if (argc > 1) {
        memset(pixels, 0, sizeof pixels);
        restart(1);
        score = 123;
        best = 456;
        height = 90;
        obstacle_x = 260;
        canvas = GAME_CANVAS;
        assert(render());
        dump(argv[1]);
    }
    puts("idle_game_test: clipping, hidden rendering, retry, acceleration, 79 playable seeds "
         "passed");
    return 0;
}
