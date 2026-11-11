#include "transition.h"
#include "display.h"
#include "profile.h"

#define SCREEN UINT32_C(0)
#define PANEL_W 480u
#define PANEL_H 854u
#define KEY TRANSITION_SIZE
#define SIDE_H 85u
#define ONE 1024u

static struct {
    bool active;
    uint32_t next, start, duration, cell_ms;
    uint32_t front;
    uint16_t delay[TRANSITION_CELLS];
    uint8_t drawn[2][TRANSITION_CELLS];
} t;

static transition_stats_t stats;

static const uint16_t side_x[TRANSITION_CELLS - TRANSITION_KEYS] = {38, 187, 336};

static uint16_t cell_x(unsigned i)
{
    return i < TRANSITION_KEYS ? (uint16_t)(38u + (i / 5u) * 149u) : side_x[i - TRANSITION_KEYS];
}

static uint16_t cell_y(unsigned i)
{
    return i < TRANSITION_KEYS ? (uint16_t)(730u - (i % 5u) * 148u) : 10u;
}

static uint16_t cell_h(unsigned i)
{
    return i < TRANSITION_KEYS ? KEY : SIDE_H;
}

static uint32_t isqrt(uint32_t v)
{
    uint32_t r = 0;
    for (uint32_t bit = UINT32_C(1) << 30; bit; bit >>= 2) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
    }
    return r;
}

static uint32_t distance(unsigned i, uint16_t ox, uint16_t oy)
{
    int32_t dx = (int32_t)(cell_x(i) + KEY / 2u) - ox,
            dy = (int32_t)(cell_y(i) + cell_h(i) / 2u) - oy;
    return isqrt((uint32_t)(dx * dx + dy * dy));
}

static bool cut(void)
{
    bool ok = true;
    for (unsigned i = 0; i < TRANSITION_CELLS; ++i) {
        ok = display_copy(
                 t.next, cell_x(i), cell_y(i), SCREEN, cell_x(i), cell_y(i), KEY, cell_h(i)) &&
             ok;
    }
    return ok;
}

static bool finish(void)
{
    t.active = false;
    bool ok = cut();
    if (t.front) {
        t.front = 0;
        (void)display_wait_vsync();
        ok = display_show(SCREEN) && ok;
    }
    return ok;
}

uint8_t transition_size(unsigned cell, uint32_t elapsed_ms)
{
    if (elapsed_ms <= t.delay[cell]) {
        return 0;
    }
    uint32_t local = elapsed_ms - t.delay[cell];
    if (local >= t.cell_ms) {
        return KEY;
    }
    uint32_t rest = ONE - local * ONE / t.cell_ms;
    uint32_t eased = ONE - rest * rest / ONE * rest / ONE;
    return (uint8_t)((eased * KEY + ONE / 2u) / ONE);
}

bool transition_start(
    uint32_t next, uint8_t effect, uint16_t duration_ms, uint16_t ox, uint16_t oy, uint32_t now_ms)
{
    t.next = next;
    if (effect != TRANSITION_WAVE || duration_ms < 60u) {
        return finish();
    }
    uint32_t far = 0, d[TRANSITION_CELLS];
    for (unsigned i = 0; i < TRANSITION_CELLS; ++i) {
        d[i] = distance(i, ox, oy);
        far = d[i] > far ? d[i] : far;
    }
    t.duration = duration_ms;
    t.cell_ms = duration_ms * 3u / 5u;
    for (unsigned i = 0; i < TRANSITION_CELLS; ++i) {
        t.delay[i] = (uint16_t)(far ? (t.duration - t.cell_ms) * d[i] / far : 0);
        t.drawn[0][i] = 0;
        t.drawn[1][i] = 0;
    }
    if (!display_copy(SCREEN, 0, 0, TRANSITION_FRONT_A, 0, 0, PANEL_W, PANEL_H) ||
        !display_copy(SCREEN, 0, 0, TRANSITION_FRONT_B, 0, 0, PANEL_W, PANEL_H)) {
        return finish();
    }
    t.front = TRANSITION_FRONT_A;
    if (!display_show(TRANSITION_FRONT_A)) {
        return finish();
    }
    t.start = now_ms;
    t.active = true;
    stats.frames = 0;
    stats.worst_cycles = 0;
    return true;
}

bool transition_step(uint32_t now_ms)
{
    if (!t.active) {
        return false;
    }
    uint32_t elapsed = now_ms - t.start;
    if (elapsed >= t.duration) {
        return finish();
    }
    unsigned b = t.front == TRANSITION_FRONT_A ? 1u : 0u;
    uint32_t back = b ? TRANSITION_FRONT_B : TRANSITION_FRONT_A;
    uint32_t since = profile_now();
    bool ok = true;
    for (unsigned i = 0; i < TRANSITION_CELLS && ok; ++i) {
        uint8_t s = transition_size(i, elapsed);
        if (s == t.drawn[b][i]) {
            continue;
        }
        t.drawn[b][i] = s;
        uint16_t h = (uint16_t)(s * cell_h(i) / KEY);
        if (!h) {
            continue;
        }
        uint16_t x = (uint16_t)(cell_x(i) + (KEY - s) / 2u),
                 y = (uint16_t)(cell_y(i) + (cell_h(i) - h) / 2u);
        ok = display_copy(t.next, x, y, back, x, y, s, h);
    }
    uint32_t cycles = profile_now() - since;
    stats.worst_cycles = cycles > stats.worst_cycles ? cycles : stats.worst_cycles;
    if (!ok) {
        return finish();
    }
    (void)display_wait_vsync();
    if (!display_show(back)) {
        return finish();
    }
    t.front = back;
    stats.frames++;
    return true;
}

bool transition_active(void)
{
    return t.active;
}

const transition_stats_t *transition_stats(void)
{
    return &stats;
}
