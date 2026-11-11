#include "keys.h"

#ifdef KEYS_HOST_TEST
extern uint8_t keys_test_read8(uint32_t address);
extern uint32_t keys_test_read32(uint32_t address);
extern void keys_test_write8(uint32_t address, uint8_t value);
extern void keys_test_write32(uint32_t address, uint32_t value);
#define R8(a) keys_test_read8(a)
#define R32(a) keys_test_read32(a)
#define W8(a, v) keys_test_write8(a, (uint8_t)(v))
#define W32(a, v) keys_test_write32(a, (uint32_t)(v))
#else
#define R8(a) (*(volatile uint8_t *)(uintptr_t)(a))
#define R32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define W8(a, v) (R8(a) = (uint8_t)(v))
#define W32(a, v) (R32(a) = (uint32_t)(v))
#endif

#define ROW_AB UINT32_C(0x40014000)
#define ROW_C UINT32_C(0x4001d000)
#define COL_GPIO UINT32_C(0x40012000)
#define COL_EPORT UINT32_C(0x40019000)
#define CLOCK_ENABLE UINT32_C(0x4000000c)
#define CLOCK_MASK UINT32_C(0x2f000000)
static volatile keys_status_t status;
static uint16_t candidate;
static uint32_t toggled_at[KEYS_COUNT], last_poll;
static bool have_poll;

static void update8(uint32_t address, uint8_t clear, uint8_t set)
{
    W8(address, (R8(address) & (uint8_t)~clear) | set);
}

static void idle_rows(void)
{
    update8(ROW_AB + 8, 0, 3);
    update8(ROW_C + 8, 0, 2);
}

static void settle(void)
{
    for (unsigned i = 0; i < 128; i++) {
        __asm volatile("nop");
    }
}

static uint16_t scan(void)
{
    static const uint8_t ids[3][5] = {{2, 5, 8, 11, 14}, {1, 4, 7, 10, 13}, {3, 6, 9, 12, 15}};
    uint16_t result = 0;
    idle_rows();
    for (uint8_t row = 0; row < 3; row++) {
        uint32_t address = row < 2 ? ROW_AB + 8 : ROW_C + 8;
        uint8_t mask = row == 0 ? 1 : 2;
        update8(address, mask, 0);
        settle();
        uint8_t e = R8(COL_EPORT + 4), g = R8(COL_GPIO + 14);
        uint8_t columns = (uint8_t)(((e >> 4) & 1) | ((g & 7) << 1) | ((e & 1) << 4));
        for (uint8_t col = 0; col < 5; col++) {
            if (!(columns & (1u << col))) {
                unsigned id = ids[row][col] - 1u;
                unsigned host_key = (id % 3u) * 5u + 4u - id / 3u;
                result |= (uint16_t)(1u << host_key);
            }
        }
        update8(address, 0, mask);
    }
    return result;
}

bool keys_init(void)
{
    status = (keys_status_t){0};
    candidate = 0;
    last_poll = 0;
    have_poll = false;
    for (unsigned i = 0; i < KEYS_COUNT; i++) {
        toggled_at[i] = 0u - KEYS_DEBOUNCE_MS;
    }
    W32(CLOCK_ENABLE, R32(CLOCK_ENABLE) | CLOCK_MASK);

    idle_rows();

    W8(ROW_AB + 3, 0x80);
    update8(ROW_AB + 11, 0, 3);
    W8(ROW_C + 3, 0x80);
    update8(ROW_C + 11, 0, 2);

    update8(COL_GPIO + 10, 7, 0);
    update8(COL_EPORT + 3, 0x11, 0);
    bool ready = (R32(CLOCK_ENABLE) & CLOCK_MASK) == CLOCK_MASK && (R8(ROW_AB + 11) & 3) == 3 &&
                 (R8(ROW_C + 11) & 2) == 2 && (R8(COL_GPIO + 10) & 7) == 0 &&
                 (R8(COL_EPORT + 3) & 0x11) == 0;
    idle_rows();
    status.initialized = ready;
    return ready;
}

void keys_poll(uint32_t now_ms)
{
    if (!status.initialized || (have_poll && now_ms == last_poll)) {
        return;
    }
    have_poll = true;
    last_poll = now_ms;
    status.polls++;
    uint16_t raw = scan();
    status.raw = raw;
    uint16_t changed = (uint16_t)(candidate ^ raw);
    candidate = raw;
    for (unsigned key = 0; key < KEYS_COUNT; key++) {
        uint16_t mask = (uint16_t)(1u << key);
        if (((status.pressed ^ candidate) & mask) && !(changed & mask) &&
            (uint32_t)(now_ms - toggled_at[key]) >= KEYS_DEBOUNCE_MS) {
            status.pressed ^= mask;
            status.transitions++;
            toggled_at[key] = now_ms;
        }
    }
}

uint16_t keys_pressed(void)
{
    return status.pressed;
}

uint16_t keys_raw(void)
{
    return status.raw;
}

const volatile keys_status_t *keys_status(void)
{
    return &status;
}
