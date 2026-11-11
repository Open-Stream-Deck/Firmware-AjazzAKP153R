#include <assert.h>
#include <stdio.h>
#include "timebase.h"
static uint32_t cycle, systick, demcr, ctrl;
static bool advance;

uint32_t timebase_test_read(uint32_t a)
{
    switch (a) {
    case 0xe0001004:
        if (advance) {
            cycle += 10;
        }
        return cycle;
    case 0xe0001000:
        return ctrl;
    case 0xe000e018:
        return systick;
    case 0xe000edfc:
        return demcr;
    default:
        return 0;
    }
}

void timebase_test_write(uint32_t a, uint32_t v)
{
    switch (a) {
    case 0xe0001004:
        cycle = v;
        break;
    case 0xe0001000:
        ctrl = v;
        break;
    case 0xe000edfc:
        demcr = v;
        break;
    case 0xe000e018:
        systick = 0xffffff;
        break;
    default:
        break;
    }
}

int main(void)
{
    assert(!timebase_init(0));
    assert(!timebase_init(150000001));
    advance = true;
    assert(timebase_init(150000000));
    assert(timebase_uses_dwt());
    advance = false;
    uint32_t base = timebase_now_ms();
    cycle += 149999;
    assert(timebase_now_ms() == base);
    cycle += 1;
    assert(timebase_now_ms() == base + 1);
    cycle += 300000;
    assert(timebase_now_ms() == base + 3);

    cycle = 0xffff0000;
    uint32_t before = timebase_now_ms();
    cycle += 300000;
    assert(timebase_now_ms() == before + 2);
    ctrl = 0x02000000;
    assert(timebase_init(150000000));
    assert(!timebase_uses_dwt());
    systick -= 150000;
    assert(timebase_now_ms() == 1);
    systick = 50000;
    (void)timebase_now_ms();
    before = timebase_now_ms();
    systick = (systick - 300000) & 0xffffff;
    assert(timebase_now_ms() == before + 2);
    puts("Polled timebase tests passed: fraction, DWT wrap, SysTick fallback/wrap");
}
