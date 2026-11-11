#include "idle_game.h"
#include "boot_anim.h"
#include "display.h"

#define FRAME_MS 40u
#define WIDTH 515
#define FLOOR 281
#define PLAYER_X 36
#define PLAYER_W 28
#define PLAYER_H 32
#define BG 0x0000u
#define FG 0xffffu
#define GAME_CANVAS UINT32_C(0x400000)

static bool armed, active, dead, pending_jump, redraw;
static uint16_t previous_keys;
static uint32_t last_frame, ticks, score, best, random_state;
static int32_t height, velocity, obstacle_x, obstacle_h, obstacle_w;
static uint8_t obstacle_kind, motion_fraction;

static bool rect(int x, int y, int w, int h, uint16_t color)
{
    bool ok = true;
    for (int row = 0; row < 3; ++row) {
        int top = y > row * 103 ? y : row * 103;
        int bottom = y + h < (row + 1) * 103 ? y + h : (row + 1) * 103;
        if (top >= bottom) {
            continue;
        }
        for (int col = 0; col < 5; ++col) {
            int left = x > col * 103 ? x : col * 103;
            int right = x + w < (col + 1) * 103 ? x + w : (col + 1) * 103;
            if (left >= right) {
                continue;
            }
            int u = 21 + col * 148 + left - col * 103;
            int v = 38 + row * 149 + top - row * 103;
            ok = display_draw_rect((uint16_t)v,
                                   (uint16_t)(854 - u - (right - left)),
                                   (uint16_t)(v + bottom - top - 1),
                                   (uint16_t)(853 - u),
                                   color,
                                   true) &&
                 ok;
        }
    }
    return ok;
}

static bool side_rect(int x, int y, int w, int h)
{
    int u = 759 + x, v = 35 + y;
    return display_draw_rect(
        (uint16_t)v, (uint16_t)(854 - u - w), (uint16_t)(v + h - 1), (uint16_t)(853 - u), FG, true);
}

static uint16_t glyph(char c)
{
    static const char chars[] = "0123456789AGINH";
    static const uint16_t bits[] = {0x7b6f,
                                    0x2492,
                                    0x73e7,
                                    0x73cf,
                                    0x5bc9,
                                    0x79cf,
                                    0x79ef,
                                    0x7249,
                                    0x7bef,
                                    0x7bcf,
                                    0x2bed,
                                    0x796f,
                                    0x7497,
                                    0x5f6d,
                                    0x5bed};
    for (unsigned i = 0; i < sizeof chars - 1u; ++i) {
        if (chars[i] == c) {
            return bits[i];
        }
    }
    return 0;
}

static bool character(int x, int y, char c, int scale)
{
    bool ok = true;
    uint16_t bits = glyph(c);
    for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 3; ++col) {
            if (bits & (1u << (14 - row * 3 - col))) {
                if (x >= WIDTH) {
                    ok = side_rect(x - WIDTH + col * scale, y + row * scale, scale, scale) && ok;
                } else {
                    ok = rect(x + col * scale, y + row * scale, scale, scale, FG) && ok;
                }
            }
        }
    }
    return ok;
}

static unsigned speed_q4(void)
{
    unsigned speed = 112u + ticks / 12u;
    return speed < 256u ? speed : 256u;
}

static void spawn(void)
{
    static const uint8_t widths[] = {16, 32, 44, 20};
    static const uint8_t heights[] = {36, 24, 30, 48};
    random_state = random_state * 1664525u + 1013904223u;
    obstacle_kind = (uint8_t)(random_state >> 30);
    obstacle_x = WIDTH + (int)((random_state >> 8) % 281u);
    obstacle_w = widths[obstacle_kind];
    obstacle_h = heights[obstacle_kind];
}

static void restart(uint32_t now_ms)
{
    dead = false;
    ticks = score = 0;
    height = velocity = 0;
    random_state = now_ms | 1u;
    motion_fraction = 0;
    spawn();
}

static void tick(void)
{
    if (dead) {
        return;
    }
    ticks++;
    score = ticks / 5u;
    if (score > 99999u) {
        score = 99999u;
    }
    if (score > best) {
        best = score;
    }
    if (pending_jump && height == 0) {
        velocity = 31;
    }
    pending_jump = false;
    height += velocity;
    velocity -= 3;
    if (height <= 0) {
        height = velocity = 0;
    }
    unsigned motion = speed_q4() + motion_fraction;
    obstacle_x -= (int)(motion >> 4);
    motion_fraction = (uint8_t)(motion & 15u);
    if (obstacle_x + obstacle_w < 0) {
        spawn();
    }
    if (obstacle_x < PLAYER_X + PLAYER_W - 3 && obstacle_x + obstacle_w > PLAYER_X + 3 &&
        height < obstacle_h - 3) {
        dead = true;
    }
}

static bool render(void)
{
    bool ok = display_draw_rect(0, 0, 479, 853, BG, true);
    ok = character(WIDTH + 17, 18, 'H', 6) && ok;
    ok = character(WIDTH + 47, 18, 'I', 6) && ok;
    uint32_t high = best;
    for (int digit = 4; digit >= 0; --digit) {
        ok = character(WIDTH + 30, 76 + digit * 56, (char)('0' + high % 10u), 8) && ok;
        high /= 10u;
    }
    uint32_t value = score;
    for (int col = 4; col >= 0; --col) {
        ok = character(col * 103 + 39, 30, (char)('0' + value % 10u), 8) && ok;
        value /= 10u;
    }
    int y = FLOOR - PLAYER_H - (int)height;
    ok = rect(PLAYER_X, y + 12, 20, 16, FG) && ok;
    ok = rect(PLAYER_X + 12, y, 16, 15, FG) && ok;
    ok = rect(PLAYER_X + 22, y + 4, 3, 3, BG) && ok;
    ok = rect(PLAYER_X - 6, y + 10, 8, 6, FG) && ok;
    int leg = !dead && height == 0 && (ticks & 2u) ? 4 : 0;
    ok = rect(PLAYER_X + 2, y + 26, 5, 6 - leg, FG) && ok;
    ok = rect(PLAYER_X + 13, y + 26, 5, 2 + leg, FG) && ok;
    int x = (int)obstacle_x, h = (int)obstacle_h, w = (int)obstacle_w;
    if (obstacle_kind == 1) {
        ok = rect(x + 5, FLOOR - h, w - 10, 5, FG) && ok;
        ok = rect(x, FLOOR - h + 5, w, h - 5, FG) && ok;
    } else if (obstacle_kind == 2) {
        ok = rect(x, FLOOR - h, 16, h, FG) && ok;
        ok = rect(x + 28, FLOOR - h + 6, 16, h - 6, FG) && ok;
    } else {
        ok = rect(x + 5, FLOOR - h, w - 10, h, FG) && ok;
        ok = rect(x, FLOOR - h + 9, w, 6, FG) && ok;
    }
    if (dead) {
        static const char again[] = "AGAIN";
        for (int col = 0; col < 5; ++col) {
            ok = character(col * 103 + 39, 119, again[col], 8) && ok;
        }
    }
    return ok;
}

void idle_game_reset(void)
{
    armed = active = dead = pending_jump = redraw = false;
    best = 0;
}

void idle_game_suspend(void)
{
    armed = active = pending_jump = redraw = false;
}

bool idle_game_step(uint32_t now_ms, uint16_t keys)
{
    if (!armed) {
        armed = true;
        previous_keys = keys;
        return false;
    }
    bool pressed = (keys & (uint16_t)~previous_keys) != 0;
    previous_keys = keys;
    if (pressed) {
        if (!active || dead) {
            active = true;
            restart(now_ms);
            last_frame = now_ms - FRAME_MS;
        } else {
            pending_jump = true;
        }
    }
    if (!active || now_ms - last_frame < FRAME_MS || (dead && !pressed && !redraw)) {
        return false;
    }
    last_frame = now_ms;
    tick();
    redraw = true;
    if (display_set_canvas(GAME_CANVAS) && render()) {
        redraw = !display_copy(GAME_CANVAS, 0, 0, BOOT_ANIM_CANVAS, 0, 0, 480, 854);
    }
    return true;
}
