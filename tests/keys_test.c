#include "keys.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint8_t ab[16], c[16], g[16], e[16];
static uint32_t clock_value;
static bool deny_direction;
static uint16_t physical;
static unsigned column_reads;

static uint16_t host_mask(uint16_t stock_mask)
{
    static const uint8_t host_index[15] = {4, 9, 14, 3, 8, 13, 2, 7, 12, 1, 6, 11, 0, 5, 10};
    uint16_t out = 0;
    for (unsigned i = 0; i < 15; i++) {
        if (stock_mask & (1u << i)) {
            out |= (uint16_t)(1u << host_index[i]);
        }
    }
    return out;
}

static uint8_t *location(uint32_t a)
{
    if (a >= 0x40014000 && a < 0x40014010) {
        return &ab[a - 0x40014000];
    }
    if (a >= 0x4001d000 && a < 0x4001d010) {
        return &c[a - 0x4001d000];
    }
    if (a >= 0x40012000 && a < 0x40012010) {
        return &g[a - 0x40012000];
    }
    if (a >= 0x40019000 && a < 0x40019010) {
        return &e[a - 0x40019000];
    }
    assert(!"Unexpected key MMIO byte address");
    return 0;
}

static uint8_t columns(void)
{
    static const uint8_t ids[3][5] = {{2, 5, 8, 11, 14}, {1, 4, 7, 10, 13}, {3, 6, 9, 12, 15}};
    bool active[3] = {!(ab[8] & 1), !(ab[8] & 2), !(c[8] & 2)};
    assert((unsigned)active[0] + active[1] + active[2] == 1);
    assert((ab[11] & 3) == 3 && (c[11] & 2) == 2 && (g[10] & 7) == 0 && (e[3] & 0x11) == 0);
    uint8_t value = 31;
    for (unsigned row = 0; row < 3; row++) {
        if (active[row]) {
            for (unsigned col = 0; col < 5; col++) {
                if (physical & (1u << (ids[row][col] - 1))) {
                    value &= (uint8_t) ~(1u << col);
                }
            }
        }
    }
    return value;
}

uint8_t keys_test_read8(uint32_t a)
{
    if (a == 0x40019004) {
        column_reads++;
        uint8_t v = columns();
        return (uint8_t)((e[4] & ~0x11) | ((v & 1) << 4) | ((v >> 4) & 1));
    }
    if (a == 0x4001200e) {
        column_reads++;
        return (uint8_t)((g[14] & ~7) | ((columns() >> 1) & 7));
    }
    return *location(a);
}

uint32_t keys_test_read32(uint32_t a)
{
    assert(a == 0x4000000c);
    return clock_value;
}

void keys_test_write8(uint32_t a, uint8_t v)
{
    if (a == 0x4001400b) {
        assert((ab[8] & 3) == 3);
    }
    if (a == 0x4001d00b) {
        assert((c[8] & 2) == 2);
    }
    if (deny_direction && (a == 0x4001400b || a == 0x4001d00b)) {
        return;
    }
    *location(a) = v;
}

void keys_test_write32(uint32_t a, uint32_t v)
{
    assert(a == 0x4000000c);
    clock_value = v;
}

static void idle(void)
{
    assert((ab[8] & 3) == 3 && (c[8] & 2) == 2);
}

static void tick(uint32_t t)
{
    keys_poll(t);
    idle();
}

static void fixture(void)
{
    memset(ab, 0xa8, sizeof ab);
    memset(c, 0xa8, sizeof c);
    memset(g, 0xff, sizeof g);
    memset(e, 0xff, sizeof e);
    clock_value = 0x41;
    physical = 0;
    deny_direction = false;
    column_reads = 0;
    assert(keys_init());
    idle();
    assert(keys_status()->initialized);
    assert(clock_value == 0x2f000041);
    assert(ab[11] == 0xab && c[11] == 0xaa);
    assert(g[10] == 0xf8 && e[3] == 0xee);
    assert((ab[8] & ~3) == 0xa8 && (c[8] & ~2) == 0xa8);
}

int main(void)
{
    fixture();
    tick(0);
    assert(keys_raw() == 0 && keys_pressed() == 0 && column_reads == 6);
    tick(0);
    assert(column_reads == 6);
    uint32_t t = 10;
    for (unsigned key = 0; key < 15; key++) {
        physical = (uint16_t)(1u << key);
        tick(t);
        assert(keys_raw() == host_mask(physical) && keys_pressed() == 0);
        tick(t + 1);
        assert(keys_pressed() == host_mask(physical));
        physical = 0;
        tick(t + 2);
        tick(t + 8);
        assert(keys_pressed() == host_mask((uint16_t)(1u << key)));
        tick(t + 9);
        assert(keys_pressed() == 0);
        t += 30;
    }
    assert(keys_status()->transitions == 30);
    physical = 3;
    tick(t);
    physical = 0;
    tick(t + 2);
    physical = 3;
    tick(t + 3);
    assert(keys_pressed() == 0);
    tick(t + 4);
    assert(keys_pressed() == host_mask(3));
    physical = 0;
    tick(t + 5);
    physical = 3;
    tick(t + 6);
    tick(t + 7);
    assert(keys_pressed() == host_mask(3));
    physical = 0x7fff;
    tick(t + 12);
    tick(t + 13);
    assert(keys_pressed() == 0x7fff);
    physical = 0;
    tick(t + 14);
    tick(t + 20);
    assert(keys_pressed() == (0x7fff ^ host_mask(3)));
    tick(t + 21);
    assert(keys_pressed() == 0);

    physical = 1;
    tick(t + 30);
    physical = 3;
    tick(t + 34);
    assert(keys_pressed() == host_mask(1));
    physical = 1;
    tick(t + 36);
    tick(t + 38);
    assert(keys_pressed() == host_mask(1));
    physical = 0;
    tick(t + 39);
    tick(t + 47);
    assert(keys_pressed() == 0);
    fixture();
    physical = 0x4000;
    tick(UINT32_MAX - 4);
    assert(keys_pressed() == 0);
    tick(3);
    assert(keys_pressed() == host_mask(0x4000));
    physical = 0;
    tick(4);
    tick(10);
    assert(keys_pressed() == host_mask(0x4000));
    tick(11);
    assert(keys_pressed() == 0);

    ab[11] &= (uint8_t)~3;
    c[11] &= (uint8_t)~2;
    deny_direction = true;
    assert(!keys_init());
    unsigned reads = column_reads;
    tick(100);
    assert(column_reads == reads);
    puts("Key matrix tests passed (GPIO model; physical orientation unverified)");
}
