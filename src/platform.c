#include "platform.h"

#define REG32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define REG16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define REG8(a) (*(volatile uint8_t *)(uintptr_t)(a))
#define CLOCK_BASE UINT32_C(0x40004000)
#define FLASH_PROTECTION UINT32_C(0x40003800)
#define WATCHDOG_BASE UINT32_C(0x40005000)
#define RESET_BASE UINT32_C(0x40002000)
#define ROM_FLASH_INIT UINT32_C(0x04005c51)
#define ROM_FLASH_ERASE UINT32_C(0x04005af5)
#define ROM_FLASH_PROGRAM UINT32_C(0x04005acd)
#define FLASH_UPDATE_KEY UINT32_C(0x9786ac03)
#define UPDATE_FLAG_SECTOR UINT32_C(0x0807e000)
#define UPDATE_FLAG_MARKER UINT32_C(0x21436587)
#define POLL_LIMIT UINT32_C(2000000)
#define WATCHDOG_STEP UINT32_C(4096)
#define INFO_GATE_MODE UINT32_C(0x082000a4)
#define INFO_CALIBRATION UINT32_C(0x082000c8)
#define INFO_MAGIC UINT32_C(0x4f464e49)
#define INFO_FALLBACK_GATE_MODE UINT32_C(0)
#define INFO_FALLBACK_CALIBRATION UINT32_C(0)

static volatile platform_status_t status;
static platform_info_t info __attribute__((section(".noinit")));
volatile uint8_t platform_probe_active, platform_probe_failed;

static void bits_set(uint32_t address, uint32_t mask)
{
    REG32(address) = REG32(address) | mask;
}

static void bits_clear(uint32_t address, uint32_t mask)
{
    REG32(address) = REG32(address) & ~mask;
}

static void barriers(void)
{
    __asm__ volatile("dsb\n\tisb" ::: "memory");
}

static bool read_info(uint32_t address, uint32_t *value)
{
    platform_probe_failed = 0;
    platform_probe_active = 1;
    uint32_t v = REG32(address);
    platform_probe_active = 0;
    if (platform_probe_failed) {
        return false;
    }
    *value = v;
    return true;
}

static void load_info(void)
{
    uint32_t gate_mode;
    if (read_info(INFO_GATE_MODE, &gate_mode)) {
        if (info.magic != INFO_MAGIC) {
            info.calibration = INFO_FALLBACK_CALIBRATION;
        }
        info.magic = INFO_MAGIC;
        info.gate_mode = gate_mode;
        return;
    }
    if (info.magic == INFO_MAGIC) {
        return;
    }
    info.magic = INFO_MAGIC;
    info.gate_mode = INFO_FALLBACK_GATE_MODE;
    info.calibration = INFO_FALLBACK_CALIBRATION;
}

static void delay_with_watchdog(uint32_t iterations)
{
    while (iterations != 0) {
        uint32_t step = iterations < WATCHDOG_STEP ? iterations : WATCHDOG_STEP;
        platform_delay_iterations(step);
        iterations -= step;
        platform_watchdog_feed();
    }
}

static bool poll(uint32_t address, uint32_t mask, uint32_t expected)
{
    for (uint32_t remaining = POLL_LIMIT; remaining != 0; --remaining) {
        if ((remaining & (WATCHDOG_STEP - 1u)) == 0) {
            platform_watchdog_feed();
        }
        uint32_t value = REG32(address);
        if ((value & mask) == expected) {
            return true;
        }
        status.last_register_value = value;
    }
    status.failed_stage = status.stage;
    status.stage = PLATFORM_STAGE_FAILED;
    status.error = PLATFORM_ERROR_POLL_TIMEOUT;
    status.failed_register = address;
    return false;
}

void platform_delay_iterations(uint32_t iterations)
{
    while (iterations-- != 0) {
        __asm__ volatile("nop" ::: "memory");
    }
}

void platform_delay_ms(uint32_t reference_delay_units)
{
    uint32_t iterations = status.core_hz / UINT32_C(6000);
    if (iterations == 0) {
        iterations = 1;
    }
    while (reference_delay_units-- != 0) {
        platform_delay_iterations(iterations);
    }
}

static void protected_gate(uint32_t mask, bool enable)
{
    bits_set(FLASH_PROTECTION, UINT32_C(0x40000000));
    uint32_t saved = (uint32_t)REG8(FLASH_PROTECTION + 4) | UINT32_C(0xd8d89900);
    REG32(FLASH_PROTECTION + 4) = UINT32_C(0xd8d89930);
    uint32_t value = REG32(CLOCK_BASE + 0x30);
    if (info.gate_mode == UINT32_C(0x55aa)) {
        value = enable ? value | mask : value & ~mask;
        if ((mask & 4) != 0) {
            value = enable ? value & ~UINT32_C(4) : value | UINT32_C(4);
        } else {
            value ^= UINT32_C(4);
        }
    } else {
        value = enable ? value | mask : value & ~mask;
    }
    REG32(CLOCK_BASE + 0x30) = value;
    REG32(FLASH_PROTECTION + 4) = saved;
    bits_clear(FLASH_PROTECTION, UINT32_C(0x40000000));
}

static void clock_write_sequence(void)
{
    uint32_t value = REG32(CLOCK_BASE + 0x5c) & UINT32_C(0x3fffffff);
    REG32(CLOCK_BASE + 0x5c) = value;
    REG32(CLOCK_BASE + 0x5c) = value | UINT32_C(0x40000000);
    REG32(CLOCK_BASE + 0x5c) = value | UINT32_C(0x80000000);
    REG32(CLOCK_BASE + 0x5c) = value | UINT32_C(0xc0000000);
}

static void clock_adjustment(uint32_t adjustment)
{
    clock_write_sequence();
    bits_set(CLOCK_BASE + 0x5c, UINT32_C(0x800000));
    if (adjustment == UINT32_C(0x10)) {
        bits_set(CLOCK_BASE + 0x50, UINT32_C(0x8000));
    } else {
        bits_clear(CLOCK_BASE + 0x50, UINT32_C(0x8000));
        REG32(CLOCK_BASE + 0x50) = (REG32(CLOCK_BASE + 0x50) & ~UINT32_C(0xf)) | (adjustment & 0xf);
    }
}

static uint32_t observed_core_frequency(uint32_t pll_selection)
{
    static const uint32_t pll_hz[4] = {108000000, 120000000, 150000000, 160000000};
    uint32_t source = REG32(CLOCK_BASE + 0x24) & 3;
    uint32_t numerator = source == 0   ? UINT32_C(8000000)
                         : source == 1 ? pll_hz[pll_selection & 3]
                         : source == 2 ? UINT32_C(240000000)
                                       : UINT32_C(12000000);
    return numerator / ((REG32(CLOCK_BASE + 8) & 0xff) + 1);
}

static bool configure_pll_150(void)
{
    status.stage = PLATFORM_STAGE_OSCILLATOR_SAFE;
    bits_set(CLOCK_BASE + 0x20, 1);
    if (!poll(CLOCK_BASE + 0x20, UINT32_C(0x100), UINT32_C(0x100))) {
        return false;
    }
    bits_clear(CLOCK_BASE + 0x24, 3);
    REG32(CLOCK_BASE + 0x24) = REG32(CLOCK_BASE + 0x24);
    if (!poll(CLOCK_BASE + 0x24, UINT32_C(0x100), UINT32_C(0x100))) {
        return false;
    }
    REG32(FLASH_PROTECTION + 4) = UINT32_C(0xd8d89930);
    bits_clear(FLASH_PROTECTION, UINT32_C(0x40000000));
    REG32(FLASH_PROTECTION + 8) = UINT32_C(0x630000);
    REG32(FLASH_PROTECTION + 8) = UINT32_C(0x590000);
    REG32(FLASH_PROTECTION + 8) = UINT32_C(0x230000);
    REG32(FLASH_PROTECTION + 8) = UINT32_C(0xd80000);
    REG32(FLASH_PROTECTION + 8) = UINT32_C(0xd80000);
    REG32(CLOCK_BASE + 0x5c) = UINT32_C(0x40000000);
    REG32(CLOCK_BASE + 0x5c) = UINT32_C(0x80000000);
    REG32(CLOCK_BASE + 0x5c) = UINT32_C(0xc0000000);
    REG32(CLOCK_BASE + 0x5c) = UINT32_C(0x80000);
    status.stage = PLATFORM_STAGE_CALIBRATION;
    uint32_t calibration = info.calibration;
    if (read_info(INFO_CALIBRATION, &calibration)) {
        info.calibration = calibration;
    }
    status.calibration_present = (calibration & UINT32_C(0xff000000)) == UINT32_C(0x92000000);
    if (status.calibration_present) {
        REG32(CLOCK_BASE + 0x68) = calibration & UINT32_C(0x00ffffff);
    }

    REG32(FLASH_PROTECTION + 8) = 0;
    bits_clear(FLASH_PROTECTION, UINT32_C(0x40000000));
    REG32(FLASH_PROTECTION + 4) = UINT32_C(0xd8d89900);
    status.stage = PLATFORM_STAGE_OSCILLATOR_PLL;
    bits_set(CLOCK_BASE + 0x20, 8);
    if (!poll(CLOCK_BASE + 0x20, UINT32_C(0x800), UINT32_C(0x800))) {
        return false;
    }
    bits_clear(CLOCK_BASE + 0x24, 3);
    bits_set(CLOCK_BASE + 0x24, 1);
    return poll(CLOCK_BASE + 0x24, UINT32_C(0x200), UINT32_C(0x200));
}

static bool configure_clock(void)
{
    uint32_t before = observed_core_frequency(3);
    if (before != UINT32_C(150000000)) {
        clock_adjustment(3);
    }
    if (!configure_pll_150()) {
        return false;
    }
    status.stage = PLATFORM_STAGE_CLOCK_DIVIDERS;

    REG32(FLASH_PROTECTION) = (REG32(FLASH_PROTECTION) & ~UINT32_C(0xf)) | 7;
    bits_set(CLOCK_BASE + 0x20, 8);
    if (!poll(CLOCK_BASE + 0x20, UINT32_C(0x800), UINT32_C(0x800))) {
        return false;
    }
    bits_clear(CLOCK_BASE + 0x24, 3);
    bits_set(CLOCK_BASE + 0x24, 1);
    if (!poll(CLOCK_BASE + 0x24, UINT32_C(0x200), UINT32_C(0x200))) {
        return false;
    }

    bits_clear(CLOCK_BASE + 8, UINT32_C(0xff));
    REG32(CLOCK_BASE + 8) = REG32(CLOCK_BASE + 8) | 0;
    bits_set(CLOCK_BASE + 0x18, 2);
    bits_set(CLOCK_BASE + 0x1c, 1);
    REG32(CLOCK_BASE + 0x0c) = (REG32(CLOCK_BASE + 0x0c) & ~UINT32_C(0xf)) | 1;
    REG32(CLOCK_BASE + 0x18) = 1;
    status.core_hz = observed_core_frequency(2);
    status.bus_hz = status.core_hz / ((REG32(CLOCK_BASE + 0x0c) & 0xf) + 1);
    if (status.core_hz != UINT32_C(150000000) || status.bus_hz != UINT32_C(75000000)) {
        status.failed_stage = status.stage;
        status.stage = PLATFORM_STAGE_FAILED;
        status.error = PLATFORM_ERROR_CLOCK_READBACK;
        return false;
    }
    return true;
}

static bool configure_cache(uint32_t base, platform_stage_t stage)
{
    status.stage = stage;
    bits_clear(base + 0x24, UINT32_C(0xff00));
    bits_clear(base + 0x20, UINT32_C(0x30000));
    bits_clear(base + 0x24, UINT32_C(0xff0000));
    REG32(base + 0x80) = UINT32_C(0x1fffff);
    bits_clear(base + 0x24, UINT32_C(0xff));
    bits_set(base + 0x24, UINT32_C(0xaa));
    bits_set(base, UINT32_C(0x85000000));
    if (!poll(base, UINT32_C(0x80000000), 0)) {
        return false;
    }
    bits_set(base, 1);
    barriers();
    return true;
}

bool platform_init(void)
{
    __asm__ volatile("cpsid i" ::: "memory");
    status = (platform_status_t){0};
    status.stage = PLATFORM_STAGE_RESET_SEQUENCE;
    load_info();
    platform_watchdog_feed();
    protected_gate(UINT32_C(0x0c), true);
    REG32(CLOCK_BASE + 0x5c) = UINT32_C(0x40000000);
    REG32(CLOCK_BASE + 0x5c) = UINT32_C(0x80000000);
    REG32(CLOCK_BASE + 0x5c) = UINT32_C(0xc0000000);
    bits_set(CLOCK_BASE + 0x5c, UINT32_C(0x400000));
    REG32(CLOCK_BASE + 0x54) = UINT32_C(0x81001000);
    bits_clear(CLOCK_BASE + 0x5c, UINT32_C(0xc0000000));
    protected_gate(UINT32_C(0x40000000), false);
    protected_gate(UINT32_C(0x80000000), true);
    protected_gate(UINT32_C(0x80000000), false);
    REG16(UINT32_C(0x40006000)) = 0;
    bits_set(UINT32_C(0xe000ed88), UINT32_C(0x00f00000));
    REG32(UINT32_C(0xe000ed08)) = UINT32_C(0x08004000);
    barriers();
    delay_with_watchdog(UINT32_C(250001));
    if (!configure_clock()) {
        return false;
    }
    if (!configure_cache(UINT32_C(0x40055000), PLATFORM_STAGE_DATA_CACHE)) {
        return false;
    }
    if (!configure_cache(UINT32_C(0x40051000), PLATFORM_STAGE_INSTRUCTION_CACHE)) {
        return false;
    }
    status.stage = PLATFORM_STAGE_READY;

    return true;
}

void platform_watchdog_init(void)
{
    REG16(WATCHDOG_BASE + 2) = UINT16_C(0xd692);
    REG16(WATCHDOG_BASE) = 1;
    status.watchdog_initialized = true;
    platform_watchdog_feed();
}

void platform_watchdog_feed(void)
{
    REG16(WATCHDOG_BASE + 6) = UINT16_C(0x5555);
    REG16(WATCHDOG_BASE + 6) = UINT16_C(0xaaaa);
}

uint32_t platform_core_hz(void)
{
    return status.core_hz;
}

typedef void (*rom_flash_init_t)(uint32_t khz);
typedef uint32_t (*rom_flash_erase_t)(uint32_t address, uint32_t count);
typedef uint32_t (*rom_flash_program_t)(uint32_t address,
                                        uint32_t count,
                                        const uint32_t *words,
                                        uint32_t mode);

static void flash_word(uint32_t address, uint32_t value)
{
    (void)((rom_flash_program_t)ROM_FLASH_PROGRAM)(address, 1, &value, 1);
}

void platform_enter_bootloader(void)
{
    uint32_t khz = status.core_hz / UINT32_C(1000);
    uint32_t wait = khz * UINT32_C(50) / UINT32_C(1000000) + 1u;
    uint32_t words[4];
    platform_watchdog_feed();
    ((rom_flash_init_t)ROM_FLASH_INIT)(khz);
    REG32(FLASH_PROTECTION + 0x2c) &= ~UINT32_C(0x01f00000);
    REG32(FLASH_PROTECTION + 0x2c) |= UINT32_C(0x1f) << 20;
    wait = wait < 2u ? 2u : wait;
    REG32(FLASH_PROTECTION) = (REG32(FLASH_PROTECTION) & ~UINT32_C(0xf)) | (wait & 0xfu);
    REG32(FLASH_PROTECTION + 4) = FLASH_UPDATE_KEY;
    for (unsigned i = 0; i < 4; ++i) {
        words[i] = REG32(UPDATE_FLAG_SECTOR + 4u * i);
    }
    platform_watchdog_feed();
    (void)((rom_flash_erase_t)ROM_FLASH_ERASE)(UPDATE_FLAG_SECTOR, 1);
    flash_word(UPDATE_FLAG_SECTOR, words[0]);
    flash_word(UPDATE_FLAG_SECTOR + 4u, ~words[1]);
    flash_word(UPDATE_FLAG_SECTOR + 8u, words[2]);
    flash_word(UPDATE_FLAG_SECTOR + 12u, ~words[3]);
    flash_word(UPDATE_FLAG_SECTOR + 16u, UPDATE_FLAG_MARKER);
    REG32(RESET_BASE) |= UINT32_C(0x80000000);
    for (;;) {
        __asm__ volatile("nop");
    }
}
