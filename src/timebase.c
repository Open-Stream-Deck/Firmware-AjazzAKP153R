#include "timebase.h"
#ifdef TIMEBASE_HOST_TEST
extern uint32_t timebase_test_read(uint32_t address);
extern void timebase_test_write(uint32_t address, uint32_t value);
#define READ(a) timebase_test_read(a)
#define WRITE(a, v) timebase_test_write(a, v)
#define NOP() ((void)0)
#else
#define READ(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define WRITE(a, v) (READ(a) = (v))
#define NOP() __asm__ volatile("nop")
#endif
#define DEMCR UINT32_C(0xe000edfc)
#define DWT_CTRL UINT32_C(0xe0001000)
#define DWT_CYCCNT UINT32_C(0xe0001004)
#define SYST_CSR UINT32_C(0xe000e010)
#define SYST_RVR UINT32_C(0xe000e014)
#define SYST_CVR UINT32_C(0xe000e018)
static uint32_t previous, remainder, milliseconds, cycles_per_ms;
static bool dwt, initialized;

bool timebase_init(uint32_t hz)
{
    initialized = false;
    milliseconds = remainder = 0;
    cycles_per_ms = hz / 1000u;
    if (hz < 1000u || hz % 1000u) {
        return false;
    }
    WRITE(DEMCR, READ(DEMCR) | UINT32_C(0x01000000));
    uint32_t ctrl = READ(DWT_CTRL);
    dwt = false;
    if (!(ctrl & UINT32_C(0x02000000))) {
        WRITE(DWT_CYCCNT, 0);
        WRITE(DWT_CTRL, ctrl | 1u);
        uint32_t before = READ(DWT_CYCCNT);
        for (unsigned i = 0; i < 32; ++i) {
            NOP();
        }
        dwt = READ(DWT_CYCCNT) != before;
    }
    if (dwt) {
        previous = READ(DWT_CYCCNT);
    } else {
        WRITE(SYST_CSR, 0);
        WRITE(SYST_RVR, UINT32_C(0x00ffffff));
        WRITE(SYST_CVR, 0);
        WRITE(SYST_CSR, 5);
        previous = READ(SYST_CVR);
    }
    initialized = true;
    return true;
}

uint32_t timebase_now_ms(void)
{
    if (!initialized) {
        return 0;
    }
    uint32_t current = dwt ? READ(DWT_CYCCNT) : READ(SYST_CVR);
    uint32_t delta = dwt ? current - previous : (previous - current) & UINT32_C(0x00ffffff);
    previous = current;
    milliseconds += delta / cycles_per_ms;
    remainder += delta % cycles_per_ms;
    if (remainder >= cycles_per_ms) {
        remainder -= cycles_per_ms;
        ++milliseconds;
    }
    return milliseconds;
}

bool timebase_uses_dwt(void)
{
    return dwt;
}
