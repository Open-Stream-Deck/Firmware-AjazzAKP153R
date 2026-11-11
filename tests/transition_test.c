#include "display.h"
#include "transition.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

#define NEXT 0xc00000u

static uint32_t shown, shows, vsyncs, copies, reveals;
static uint8_t level[2][TRANSITION_CELLS];

static int buffer(uint32_t address)
{
    assert(address == TRANSITION_FRONT_A || address == TRANSITION_FRONT_B);
    return address == TRANSITION_FRONT_B;
}

static unsigned cell_x(unsigned i)
{
    static const unsigned side[] = {38, 187, 336};
    return i < TRANSITION_KEYS ? 38u + i / 5u * 149u : side[i - TRANSITION_KEYS];
}

static unsigned cell_y(unsigned i)
{
    return i < TRANSITION_KEYS ? 730u - i % 5u * 148u : 10u;
}

static unsigned cell_h(unsigned i)
{
    return i < TRANSITION_KEYS ? TRANSITION_SIZE : 85u;
}

static unsigned cell(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    for (unsigned i = 0; i < TRANSITION_CELLS; ++i) {
        if (x >= cell_x(i) && x + w <= cell_x(i) + TRANSITION_SIZE && y >= cell_y(i) &&
            y + h <= cell_y(i) + cell_h(i)) {
            assert(x - cell_x(i) == (TRANSITION_SIZE - w) / 2u);
            assert(y - cell_y(i) == (cell_h(i) - h) / 2u);
            assert(h == w * cell_h(i) / TRANSITION_SIZE);
            return i;
        }
    }
    assert(false);
    return 0;
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
    copies++;
    if (src == 0) {
        assert(sx == 0 && sy == 0 && dx == 0 && dy == 0 && w == 480 && h == 854);
        memset(level[buffer(dst)], 0, TRANSITION_CELLS);
        return true;
    }
    assert(src == NEXT && sx == dx && sy == dy && w > 0 && h > 0 && w <= TRANSITION_SIZE);
    if (dst == 0) {
        assert(w == TRANSITION_SIZE);
        (void)cell(dx, dy, w, h);
        return true;
    }
    reveals++;
    unsigned i = cell(dx, dy, w, h);
    assert(w > level[buffer(dst)][i]);
    level[buffer(dst)][i] = (uint8_t)w;
    return true;
}

bool display_wait_vsync(void)
{
    vsyncs++;
    return true;
}

bool display_show(uint32_t address)
{
    shows++;
    shown = address;
    return true;
}

int main(void)
{
    assert(transition_start(NEXT, TRANSITION_CUT, 400, 0, 0, 0));
    assert(!transition_active() && copies == TRANSITION_CELLS && shows == 0);

    uint16_t ox = 38 + 51, oy = 730 - 2 * 148 + 51;
    assert(transition_start(NEXT, TRANSITION_WAVE, 400, ox, oy, 1000));
    assert(transition_active() && shown == TRANSITION_FRONT_A);

    uint8_t prev[TRANSITION_CELLS] = {0};
    uint32_t origin_done = 0;
    for (uint32_t ms = 1000; transition_active(); ms += 16) {
        assert(transition_step(ms) || !transition_active());
        if (!transition_active()) {
            break;
        }
        int b = buffer(shown);
        for (unsigned i = 0; i < TRANSITION_CELLS; ++i) {
            uint8_t a = transition_size(i, ms - 1000);
            assert(a >= prev[i]);
            assert(level[b][i] == a || (level[b][i] == 0 && a * cell_h(i) / TRANSITION_SIZE == 0));
            prev[i] = a;
        }
        if (!origin_done && prev[2] == TRANSITION_SIZE) {
            origin_done = ms;
        }
    }
    assert(shown == 0 && origin_done && origin_done < 1400 && prev[15] > 0 && prev[17] > 0);
    assert(transition_size(2, 200) > transition_size(14, 200));
    assert(transition_size(2, 0) == 0 && transition_size(2, 240) == TRANSITION_SIZE);
    assert(transition_stats()->frames > 20 && reveals > 0);
    assert(!transition_step(5000));
    return 0;
}
