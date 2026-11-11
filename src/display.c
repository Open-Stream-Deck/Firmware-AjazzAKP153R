#include "display.h"
#include <stddef.h>
#include "memory.h"
#include "platform.h"
#include "profile.h"
#include "timebase.h"

extern void board_service(void);
#ifdef DISPLAY_HOST_TEST
extern volatile uint8_t *display_test_mmio8(uint32_t address);
extern volatile uint16_t *display_test_mmio16(uint32_t address);
extern volatile uint32_t *display_test_mmio32(uint32_t address);
extern bool display_test_byte(uint8_t out, uint8_t *in);
extern void display_test_cs(bool selected);
#define R8(a) (*display_test_mmio8(a))
#define R16(a) (*display_test_mmio16(a))
#define R32(a) (*display_test_mmio32(a))
#else
#define R8(a) (*(volatile uint8_t *)(uintptr_t)(a))
#define R16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define R32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#endif
#define SPI UINT32_C(0x40011000)
#define EPORT0 UINT32_C(0x40019000)
#define EPORT1 UINT32_C(0x4001a000)
#define PANEL_GPIO UINT32_C(0x40012000)
#define PANEL_SERIAL UINT32_C(0x40017000)
#define WIDTH 480u
#define HEIGHT 854u
#define SPI_LIMIT 65534u
#define CONTROLLER_LIMIT 20000u

#define DMAC UINT32_C(0x40046000)
#define DMA_CH0 (DMAC + 0x00)
#define DMA_CH1 (DMAC + 0x58)
#define DMA_RAW_TFR (DMAC + 0x2c0)
#define DMA_MASK_TFR (DMAC + 0x310)
#define DMA_CLEAR_TFR (DMAC + 0x338)
#define DMA_CFG (DMAC + 0x398)
#define DMA_CHEN (DMAC + 0x3a0)
#define DMA_LIMIT 4000000u

#define STAGE_BYTES 1440u

#define REG_INTEN 0x0bu
#define REG_INTF 0x0cu
#define VSYNC_EVENT 0x10u
#define VSYNC_TIMEOUT_MS 40u

static volatile display_status_t state;
static uint8_t requested_brightness = 50;
#ifdef DISPLAY_HOST_TEST
static struct {
    uint8_t engine, depth, bpp16;
} tuning = {DISPLAY_ENGINE_CPU, 1, 0};
#else

static struct {
    uint8_t engine, depth, bpp16;
} tuning = {DISPLAY_ENGINE_DMA, 2, 1};
#endif
static uint8_t staging[2][STAGE_BYTES] __attribute__((aligned(4)));
#ifndef DISPLAY_HOST_TEST
static volatile uint16_t dma_sink;
static uint32_t dma_started;
#endif
static bool dma_inflight, stream_open, applied_bpp16, color_valid;
static bool vsync_enabled, vsync_missing;
static bool spi_wide = true, dma_wide;
static uint32_t spi_busy;

static const uint16_t panel_initialization[] = {
    0x00ff, 0x0177, 0x0101, 0x0100, 0x0100, 0x0113, 0x00ef, 0x0108, 0x00ff, 0x0177, 0x0101, 0x0100,
    0x0100, 0x0110, 0x00c0, 0x01e9, 0x0103, 0x00c1, 0x010c, 0x0102, 0x00c2, 0x0110, 0x0106, 0x00c7,
    0x0100, 0x00cc, 0x0118, 0x00b0, 0x0140, 0x010b, 0x0158, 0x010c, 0x0111, 0x0106, 0x010b, 0x0108,
    0x0109, 0x0126, 0x0106, 0x0115, 0x0118, 0x016a, 0x016e, 0x014f, 0x00b1, 0x0140, 0x0111, 0x0157,
    0x010d, 0x0111, 0x0107, 0x010b, 0x0109, 0x0108, 0x0126, 0x0105, 0x01d3, 0x010d, 0x016b, 0x016e,
    0x014f, 0x00ff, 0x0177, 0x0101, 0x0100, 0x0100, 0x0111, 0x00b0, 0x0156, 0x00b1, 0x013a, 0x00b2,
    0x0187, 0x00b3, 0x0180, 0x00b5, 0x014e, 0x00b7, 0x0185, 0x00b8, 0x0110, 0x00b9, 0x0110, 0x00bc,
    0x0103, 0x00c0, 0x0189, 0x00c1, 0x0178, 0x00c2, 0x0178, 0x00d0, 0x0188, 0x00e0, 0x0100, 0x0100,
    0x0102, 0x00e1, 0x0104, 0x0100, 0x0100, 0x0100, 0x0105, 0x0100, 0x0100, 0x0100, 0x0100, 0x0120,
    0x0120, 0x00e2, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x0100,
    0x0100, 0x0100, 0x0100, 0x00e3, 0x0100, 0x0100, 0x0133, 0x0100, 0x00e4, 0x0122, 0x0100, 0x00e5,
    0x0104, 0x015c, 0x01a0, 0x01a0, 0x0106, 0x015c, 0x01a0, 0x01a0, 0x0100, 0x0100, 0x0100, 0x0100,
    0x0100, 0x0100, 0x0100, 0x0100, 0x00e6, 0x0100, 0x0100, 0x0133, 0x0100, 0x00e7, 0x0122, 0x0100,
    0x00e8, 0x0105, 0x015c, 0x01a0, 0x01a0, 0x0107, 0x015c, 0x01a0, 0x01a0, 0x0100, 0x0100, 0x0100,
    0x0100, 0x0100, 0x0100, 0x0100, 0x0100, 0x00eb, 0x0102, 0x0100, 0x0140, 0x0140, 0x0100, 0x0100,
    0x0100, 0x00ec, 0x0100, 0x0100, 0x00ed, 0x01aa, 0x0145, 0x010b, 0x01ff, 0x01ff, 0x01ff, 0x01ff,
    0x01ff, 0x01ff, 0x01ff, 0x01ff, 0x01ff, 0x01ff, 0x01b0, 0x0154, 0x01aa, 0x00ef, 0x0108, 0x0108,
    0x0108, 0x0145, 0x013f, 0x0154, 0x00ff, 0x0177, 0x0101, 0x0100, 0x0100, 0x0113, 0x00e8, 0x0100,
    0x010e, 0x0011, 0x8078, 0x00ff, 0x0177, 0x0101, 0x0100, 0x0100, 0x0113, 0x00e8, 0x0100, 0x010c,
    0x800a, 0x00e8, 0x0100, 0x0100, 0x00e6, 0x0116, 0x017c, 0x0036, 0x0100, 0x0029, 0x8014,
};

static bool fail(display_error_t error)
{
    state.failed_stage = state.stage;
    state.error = error;
    state.stage = DISPLAY_STAGE_FAILED;
    state.failures++;
    stream_open = false;
    R8(EPORT1 + 5) |= 0x10;
    R8(PANEL_GPIO + 0x0e) |= 8;
    return false;
}

static void service_delay(uint32_t units)
{
    while (units-- != 0) {
        platform_delay_ms(1);
        board_service();
    }
}

static bool transfer(uint8_t out, uint8_t *in)
{
#ifdef DISPLAY_HOST_TEST
    if (!display_test_byte(out, in)) {
        return fail(DISPLAY_ERROR_SPI_RX);
    }
#else
    uint32_t remaining = SPI_LIMIT;
    while ((R16(SPI + 0x16) & 8) != 0) {
        if (--remaining == 0) {
            return fail(DISPLAY_ERROR_SPI_TX);
        }
    }
    R8(SPI + 0x12) = out;
    remaining = SPI_LIMIT;
    while ((R16(SPI + 0x16) & 1) != 0) {
        if (--remaining == 0) {
            return fail(DISPLAY_ERROR_SPI_RX);
        }
    }
    uint8_t value = R8(SPI + 0x12);
    if (in != 0) {
        *in = value;
    }
#endif
    state.spi_bytes++;
    return true;
}

static void chip_select(bool select)
{
#ifdef DISPLAY_HOST_TEST
    display_test_cs(select);
#endif
    if (select) {
        R8(EPORT1 + 5) &= (uint8_t)~0x10u;
    } else {
        R8(EPORT1 + 5) |= 0x10;
    }
}

static void select_controller(bool select)
{
    if (select) {
        (void)display_flush();
    }
    chip_select(select);
}

static bool controller_command(uint8_t reg)
{
    select_controller(true);
    bool ok = transfer(0, 0) && transfer(reg, 0);
    select_controller(false);
    return ok;
}

static bool controller_data(uint8_t data)
{
    select_controller(true);
    bool ok = transfer(0x80, 0) && transfer(data, 0);
    select_controller(false);
    return ok;
}

static bool controller_read(uint8_t *value)
{
    select_controller(true);
    bool ok = transfer(0xc0, 0) && transfer(0xff, value);
    select_controller(false);
    return ok;
}

static bool controller_status(uint8_t *value)
{
    select_controller(true);
    bool ok = transfer(0x40, 0) && transfer(0xff, value);
    select_controller(false);
    if (ok) {
        state.last_status = *value;
    }
    return ok;
}

static bool write_register(uint8_t reg, uint8_t value)
{
    return controller_command(reg) && controller_data(value);
}

static bool modify_register(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t old;
    return controller_command(reg) && controller_read(&old) &&
           controller_data((old & mask) | value);
}

static bool word_register(uint8_t reg, uint16_t value)
{
    return write_register(reg, (uint8_t)value) && write_register(reg + 1, (uint8_t)(value >> 8));
}

static bool dword_register(uint8_t reg, uint32_t value)
{
    return word_register(reg, (uint16_t)value) && word_register(reg + 2, (uint16_t)(value >> 16));
}

static bool wait_controller(uint8_t mask, uint8_t expected)
{
    for (uint32_t n = 0; n < CONTROLLER_LIMIT; n++) {
        uint8_t value;
        if (!controller_status(&value)) {
            return false;
        }
        if ((value & mask) == expected) {
            return true;
        }
        if ((n & 63) == 0) {
            board_service();
        }
    }
    return fail(DISPLAY_ERROR_CONTROLLER_STATUS);
}

static void reset_controller(void)
{
    R32(UINT32_C(0x4000001c)) &= ~UINT32_C(0x10);
    R8(EPORT0 + 3) |= 0x80;
    R8(EPORT0 + 5) &= (uint8_t)~0x80u;
    service_delay(100);
    R8(EPORT0 + 5) |= 0x80;
    service_delay(100);
}

static bool controller_start(void)
{
    for (uint32_t attempt = 0; attempt < 5; attempt++) {
        for (uint32_t n = 0; n < 128; n++) {
            uint8_t status, reg;
            if (!controller_status(&status)) {
                return false;
            }
            if ((status & 2) == 0) {
                service_delay(1);
                if (!controller_command(1) || !controller_read(&reg)) {
                    return false;
                }
                if ((reg & 0x80) != 0) {
                    return true;
                }
                if (!write_register(1, 0x80)) {
                    return false;
                }
            }
            service_delay(1);
        }
        reset_controller();
    }
    return fail(DISPLAY_ERROR_CONTROLLER_RESET);
}

static void gpio_spi_init(void)
{
    R32(UINT32_C(0x4000001c)) &= ~UINT32_C(0x10);
    R8(EPORT0 + 3) |= 0x40;
    R8(EPORT0 + 5) &= (uint8_t)~0x40u;
    R32(UINT32_C(0x40001014)) |= 2;
    R8(UINT32_C(0x40018009)) &= (uint8_t)~1u;
    R32(UINT32_C(0x40018004)) &= ~UINT32_C(0x70);
    R32(UINT32_C(0x40018004)) |= UINT32_C(0x40);
    R8(UINT32_C(0x40018000)) = 2;
    R8(UINT32_C(0x40018009)) |= 8;
    R32(UINT32_C(0x4001801c)) = 2;
    R32(UINT32_C(0x40018018)) = 4;
    R8(UINT32_C(0x4001806e)) |= 2;
    R8(UINT32_C(0x4001806d)) |= 2;
    R8(UINT32_C(0x4001806c)) |= 2;
    R8(UINT32_C(0x40018009)) |= 1;

    R16(SPI + 0x16) = 0xffff;
    R8(SPI + 6) = 0x80;
    R8(SPI + 7) = 0x80;
    R8(SPI + 1) = (R8(SPI + 1) & 0xf0) | 7;
    R8(SPI + 2) = 0x1c;
    R8(SPI) = 0;
    R8(SPI + 0x0b) = 0;
    R8(SPI + 0x0a) = 0x0e;
    R8(SPI + 0x0e) |= 8;
    R8(SPI + 3) = 0;
    R8(SPI + 2) |= 0x40;
    R8(SPI + 2) |= 0x40;

    R8(EPORT1 + 3) |= 0x10;
    select_controller(false);
}

static void gpio_panel_init(void)
{
    R32(UINT32_C(0x40023074)) &= ~UINT32_C(0x60);
    R8(PANEL_SERIAL + 8) |= 0xc0;
    R8(PANEL_SERIAL + 0x0a) |= 3;

    R8(PANEL_GPIO + 0x0a) |= 8;
    R8(PANEL_GPIO + 0x0e) &= (uint8_t)~8u;
    service_delay(20);
    R8(PANEL_GPIO + 0x0e) |= 8;
    service_delay(50);
}

static void panel_clock(bool high)
{
    if (high) {
        R8(PANEL_SERIAL + 9) |= 1;
    } else {
        R8(PANEL_SERIAL + 9) &= (uint8_t)~1u;
    }
}

static void panel_data(bool high)
{
    if (high) {
        R8(PANEL_SERIAL + 9) |= 2;
    } else {
        R8(PANEL_SERIAL + 9) &= (uint8_t)~2u;
    }
}

static void panel_bit_delay(void)
{
    platform_delay_iterations((platform_core_hz() / UINT32_C(6000000)) * 10u);
}

static void panel_byte(bool is_data, uint8_t value)
{
    R8(PANEL_GPIO + 0x0e) &= (uint8_t)~8u;
    panel_clock(false);
    panel_data(is_data);
    panel_bit_delay();
    panel_clock(true);
    panel_bit_delay();
    for (unsigned bit = 0; bit < 8; bit++) {
        panel_clock(false);
        panel_data((value & 0x80) != 0);
        panel_bit_delay();
        panel_clock(true);
        panel_bit_delay();
        value = (uint8_t)(value << 1);
    }
    R8(PANEL_GPIO + 0x0e) |= 8;
}

static bool initialize_controller_registers(void)
{
    if (!write_register(5, 0xca) || !write_register(7, 0xcb) || !write_register(9, 0xcb) ||
        !write_register(6, 0x7c) || !write_register(8, 0x90) || !write_register(10, 0x90) ||
        !write_register(0, 0x80)) {
        return false;
    }
    service_delay(1);
    state.stage = DISPLAY_STAGE_SDRAM;

    if (!write_register(0xe0, 0x29) || !write_register(0xe1, 3) || !word_register(0xe2, 779) ||
        !write_register(0xe4, 1) || !wait_controller(4, 4)) {
        return false;
    }
    service_delay(1);

    if (!modify_register(1, 0xe7, 0) || !modify_register(1, 0xff, 1) ||
        !modify_register(2, 0x3f, 0) || !modify_register(2, 0xf9, 0) ||
        !modify_register(3, 0xfb, 0) || !modify_register(3, 0xfc, 0) ||
        !modify_register(0x12, 0x7f, 0) || !modify_register(0x12, 0xf7, 0) ||
        !modify_register(0x12, 0xf8, 0) || !modify_register(0x13, 0x7f, 0) ||
        !modify_register(0x13, 0xbf, 0) || !modify_register(0x13, 0xdf, 0)) {
        return false;
    }
    if (!write_register(0x14, 59) || !write_register(0x15, 0) || !word_register(0x1a, 853) ||
        !write_register(0x16, 9) || !write_register(0x17, 0) || !write_register(0x18, 2) ||
        !write_register(0x19, 0) || !word_register(0x1c, 9) || !write_register(0x1e, 4) ||
        !write_register(0x1f, 4) || !modify_register(0x5e, 0xfb, 0) ||
        !modify_register(0x5e, 0xff, 3) || !modify_register(0x10, 0xff, 8)) {
        return false;
    }
    return true;
}

static bool brightness_registers(uint8_t percent)
{
    return modify_register(0x85, 0xfb, 8) && write_register(0x84, 199) &&
           modify_register(0x85, 0x3f, 0) && word_register(0x8e, 100) &&
           word_register(0x8c, percent) && modify_register(0x86, 0xff, 0x10);
}

static bool apply_bpp(void)
{
    bool ok = tuning.bpp16 ? modify_register(0x10, 0xf3, 0x04) && modify_register(0x5e, 0xfc, 0x01)
                           : modify_register(0x10, 0xf3, 0x08) && modify_register(0x5e, 0xfc, 0x03);
    if (ok) {
        applied_bpp16 = tuning.bpp16 != 0;
    }
    return ok;
}

bool display_init(void)
{
    (void)display_flush();
    state = (display_status_t){0};
    state.busy = true;
    state.stage = DISPLAY_STAGE_GPIO;
    applied_bpp16 = false;
    color_valid = false;
    vsync_enabled = false;
    vsync_missing = false;
    gpio_spi_init();
    state.stage = DISPLAY_STAGE_CONTROLLER_RESET;
    reset_controller();
    if (!controller_start()) {
        goto failed;
    }
    if (!wait_controller(2, 0)) {
        goto failed;
    }
    state.stage = DISPLAY_STAGE_CONTROLLER_CLOCK;
    if (!initialize_controller_registers()) {
        goto failed;
    }
    gpio_panel_init();
    state.stage = DISPLAY_STAGE_PANEL;
    for (unsigned i = 0; i < sizeof(panel_initialization) / sizeof(panel_initialization[0]); i++) {
        uint16_t op = panel_initialization[i];
        if ((op & 0x8000) != 0) {
            service_delay(op & 0x7fff);
        } else {
            panel_byte((op & 0x100) != 0, (uint8_t)op);
        }
        if ((i & 15) == 0) {
            board_service();
        }
    }
    state.stage = DISPLAY_STAGE_FRAMEBUFFER;
    if (!brightness_registers(requested_brightness) || !modify_register(0x12, 0xff, 0x40) ||
        !modify_register(0xb7, 0xff, 0x80) || !modify_register(0xb9, 0xff, 0x20) ||
        !write_register(0xbb, 1) || !modify_register(1, 0xff, 2) ||
        !modify_register(0x10, 0xff, 8) || !dword_register(0x20, 0) ||
        !word_register(0x24, WIDTH) || !word_register(0x26, 0) || !word_register(0x28, 0) ||
        !dword_register(0x50, 0) || !word_register(0x54, WIDTH) || !word_register(0x56, 0) ||
        !word_register(0x58, 0) || !word_register(0x5a, WIDTH) || !word_register(0x5c, HEIGHT)) {
        goto failed;
    }
    if (tuning.bpp16 && !apply_bpp()) {
        goto failed;
    }
    state.brightness = requested_brightness;
    state.stage = DISPLAY_STAGE_READY;
    state.busy = false;
    return true;
failed:
    state.busy = false;
    return false;
}

bool display_ready(void)
{
    return state.stage == DISPLAY_STAGE_READY;
}

uint16_t display_width(void)
{
    return WIDTH;
}

uint16_t display_height(void)
{
    return HEIGHT;
}

const volatile display_status_t *display_status(void)
{
    return &state;
}

void display_set_spi_wide(bool wide)
{
    spi_wide = wide;
}

bool display_spi_wide(void)
{
    return spi_wide;
}

uint32_t display_spi_busy(void)
{
    return spi_busy;
}

void display_spi_registers(uint8_t *out, unsigned count)
{
    for (unsigned i = 0; i < count; i++) {
#ifdef DISPLAY_HOST_TEST
        out[i] = 0;
#else
        out[i] = i == 0x12 ? 0 : R8(SPI + i);
#endif
    }
}

#ifdef DISPLAY_HOST_TEST
void display_test_set_ready(void)
{
    state = (display_status_t){0};
    state.stage = DISPLAY_STAGE_READY;
    requested_brightness = 0;
    vsync_enabled = false;
    vsync_missing = false;
}
#endif

void display_set_brightness(uint8_t percentage)
{
    if (percentage > 100) {
        percentage = 100;
    }
    requested_brightness = percentage;
    if (!display_ready() || state.busy) {
        return;
    }
    state.busy = true;
    if (brightness_registers(percentage)) {
        state.brightness = percentage;
    }
    state.busy = false;
}

void display_sync_brightness(void)
{
    if (requested_brightness != state.brightness) {
        display_set_brightness(requested_brightness);
    }
}

static bool begin_pixels(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    return wait_controller(8, 0) && modify_register(3, 0xfb, 0) && word_register(0x56, x) &&
           word_register(0x58, y) && word_register(0x5a, w) && word_register(0x5c, h) &&
           word_register(0x5f, x) && word_register(0x61, y) && controller_command(4);
}

static bool cpu_send(const uint8_t *p, uint32_t n)
{
#ifdef DISPLAY_HOST_TEST
    for (uint32_t i = 0; i < n; i++) {
        if (!display_test_byte(p[i], 0)) {
            return fail(DISPLAY_ERROR_SPI_RX);
        }
    }
    return true;
#else
    uint32_t inflight = 0, depth = tuning.depth ? tuning.depth : 1;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t remaining = SPI_LIMIT;
        while ((R16(SPI + 0x16) & 8) != 0) {
            if ((R16(SPI + 0x16) & 1) == 0 && inflight) {
                (void)R8(SPI + 0x12);
                inflight--;
            }
            if (--remaining == 0) {
                return fail(DISPLAY_ERROR_SPI_TX);
            }
        }
        R8(SPI + 0x12) = p[i];
        inflight++;
        while (inflight >= depth) {
            remaining = SPI_LIMIT;
            while ((R16(SPI + 0x16) & 1) != 0) {
                if (--remaining == 0) {
                    return fail(DISPLAY_ERROR_SPI_RX);
                }
            }
            (void)R8(SPI + 0x12);
            inflight--;
        }
    }
    while (inflight) {
        uint32_t remaining = SPI_LIMIT;
        while ((R16(SPI + 0x16) & 1) != 0) {
            if (--remaining == 0) {
                return fail(DISPLAY_ERROR_SPI_RX);
            }
        }
        (void)R8(SPI + 0x12);
        inflight--;
    }
    return true;
#endif
}

#ifndef DISPLAY_HOST_TEST

static void spi_frame(bool wide)
{
    R8(SPI + 1) = (uint8_t)((R8(SPI + 1) & 0xf0) | (wide ? 0x0f : 0x07));
    dma_wide = wide;
}

static void dma_setup(bool wide)
{
    uint32_t widths = wide ? UINT32_C(0x12) : 0;
    spi_frame(wide);
    R32(DMA_CFG) = 1;
    R32(DMA_CH0 + 0x00) = SPI + 0x12;
    R32(DMA_CH0 + 0x18) = UINT32_C(0x200500) | widths;
    R32(DMA_CH0 + 0x40) = 0;
    R32(DMA_CH0 + 0x44) = UINT32_C(0x200);
    R32(DMA_MASK_TFR) |= UINT32_C(0x101);
    R32(DMA_CH1 + 0x08) = SPI + 0x12;
    R32(DMA_CH1 + 0x18) = UINT32_C(0x100100) | widths;
    R32(DMA_CH1 + 0x40) = 0;
    R32(DMA_CH1 + 0x44) = UINT32_C(0x800);
    R32(DMA_MASK_TFR) |= UINT32_C(0x202);
    R8(SPI + 0x1b) = 0x70;
    R32(DMA_CLEAR_TFR) = 3;
}

static void dma_abort(void)
{
    R32(DMA_CHEN) = UINT32_C(0x300);
    R8(SPI + 0x1a) = 0;
    R32(DMA_CLEAR_TFR) = 3;
    for (uint32_t n = 0; n < 64 && (R16(SPI + 0x16) & 1) == 0; n++) {
        (void)R8(SPI + 0x12);
    }
    dma_inflight = false;
    tuning.engine = DISPLAY_ENGINE_CPU;
}

static bool dma_wait(void)
{
    if (!dma_inflight) {
        return true;
    }
    uint32_t since = profile_now();
    uint32_t n = 0;
    while ((R32(DMA_RAW_TFR) & 1) == 0) {
        if (++n >= DMA_LIMIT) {
            goto timeout;
        }
    }
    R32(DMA_CLEAR_TFR) = 1;
    while ((R32(DMA_RAW_TFR) & 2) == 0) {
        if (++n >= DMA_LIMIT) {
            goto timeout;
        }
    }
    R32(DMA_CLEAR_TFR) = 2;
    while ((R8(SPI + 0x16) & 0x20) == 0) {
        if (++n >= DMA_LIMIT) {
            goto timeout;
        }
    }
    R8(SPI + 0x1a) = 0;
    dma_inflight = false;
    spi_busy += profile_now() - dma_started;
    profile_add(PROFILE_DMA_WAIT, since);
    return true;
timeout:
    dma_abort();
    return false;
}

static void dma_start(const uint8_t *p, uint32_t n)
{
    n >>= dma_wide ? 1u : 0u;
    dma_started = profile_now();
    R32(DMA_CH0 + 0x08) = (uint32_t)(uintptr_t)&dma_sink;
    R32(DMA_CH0 + 0x1c) = n;
    R32(DMA_CHEN) |= UINT32_C(0x101);
    R32(DMA_CH1 + 0x00) = (uint32_t)(uintptr_t)p;
    R32(DMA_CH1 + 0x1c) = n;
    R32(DMA_CHEN) |= UINT32_C(0x202);
    R8(SPI + 0x1a) = 3;
    dma_inflight = true;
}
#endif

bool display_flush(void)
{
    if (!stream_open) {
        return true;
    }
    bool ok = true;
#ifndef DISPLAY_HOST_TEST
    ok = dma_wait();
    if (dma_wide) {
        spi_frame(false);
    }
#endif
    stream_open = false;
    chip_select(false);
    return ok;
}

static bool send_chunk(const uint8_t *p, uint32_t n)
{
#ifndef DISPLAY_HOST_TEST
    if (tuning.engine == DISPLAY_ENGINE_DMA) {
        if (!dma_wait()) {
            return false;
        }
        dma_start(p, n);
        return true;
    }
#endif
    return cpu_send(p, n);
}

bool display_blit_rgb565(
    uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *pixels, uint16_t stride)
{
    if (!display_ready() || state.busy) {
        state.error = DISPLAY_ERROR_BUSY;
        return false;
    }
    if (pixels == 0 || w == 0 || h == 0 || stride < w || (uint32_t)x + w > WIDTH ||
        (uint32_t)y + h > HEIGHT) {
        state.error = DISPLAY_ERROR_ARGUMENT;
        return false;
    }
    state.busy = true;

    if (!begin_pixels(x, y, w, h)) {
        goto failed;
    }
    select_controller(true);
    if (!transfer(0x80, 0)) {
        goto failed;
    }
#ifndef DISPLAY_HOST_TEST
    if (tuning.engine == DISPLAY_ENGINE_DMA) {
        dma_setup(false);
    }
#endif
    bool dma = tuning.engine == DISPLAY_ENGINE_DMA;
    uint32_t pixel_bytes = applied_bpp16 ? 2u : 3u, used = 0, since_service = 0;
    unsigned which = 0;
    uint8_t *out = staging[0];
    for (uint16_t row = 0; row < h; row++) {
        const uint16_t *source = pixels + (uint32_t)row * stride;
        for (uint16_t col = 0; col < w; col++) {
            uint16_t p = source[col];
            if (applied_bpp16) {
                out[used] = (uint8_t)p;
                out[used + 1] = (uint8_t)(p >> 8);
            } else {
                uint8_t b = (uint8_t)((p & 31u) << 3);
                b |= b >> 5;
                uint8_t g = (uint8_t)(((p >> 5) & 63u) << 2);
                g |= g >> 6;
                uint8_t r = (uint8_t)(((p >> 11) & 31u) << 3);
                r |= r >> 5;

                out[used] = b;
                out[used + 1] = g;
                out[used + 2] = r;
            }
            used += pixel_bytes;
            if (used + pixel_bytes > STAGE_BYTES) {
                if (!send_chunk(out, used)) {
                    goto failed;
                }
                since_service += used / pixel_bytes;
                used = 0;
                if (dma) {
                    which ^= 1u;
                    out = staging[which];
                }

                if (since_service >= 384) {
                    since_service = 0;
                    board_service();
                }
            }
        }
    }
    if (used && !send_chunk(out, used)) {
        goto failed;
    }
    state.spi_bytes += (uint32_t)w * h * pixel_bytes;
    state.pixels_written += (uint32_t)w * h;
    if (dma) {
        stream_open = true;
    } else {
        chip_select(false);
    }
    board_service();
    state.busy = false;
    if (requested_brightness != state.brightness) {
        state.busy = true;
        if (!brightness_registers(requested_brightness)) {
            goto failed;
        }
        state.brightness = requested_brightness;
        state.busy = false;
    }
    state.blits++;
    state.error = DISPLAY_ERROR_NONE;
    return true;
failed:
    if (!dma_inflight) {
        stream_open = false;
        chip_select(false);
    } else {
        (void)display_flush();
    }
    state.busy = false;
    return false;
}

bool display_fill_rgb565(uint16_t color)
{
    uint16_t row[WIDTH];
    for (unsigned x = 0; x < WIDTH; x++) {
        row[x] = color;
    }
    for (uint16_t y = 0; y < HEIGHT; y++) {
        if (!display_blit_rgb565(0, y, WIDTH, 1, row, WIDTH)) {
            return false;
        }
    }
    return true;
}

bool display_set_canvas(uint32_t address)
{
    if (!display_ready() || state.busy) {
        return false;
    }
    state.busy = true;
    bool ok = wait_controller(8, 0) && dword_register(0x50, address);
    state.busy = false;
    return ok;
}

static bool bte_idle(void)
{
    return wait_controller(8, 0);
}

static uint16_t last_color = 0xffff;

static bool draw_color(uint16_t c)
{
    if (color_valid && last_color == c) {
        return true;
    }
    uint8_t r = (uint8_t)(((c >> 11) & 31u) << 3), g = (uint8_t)(((c >> 5) & 63u) << 2),
            b = (uint8_t)((c & 31u) << 3);
    if (!write_register(0xd2, r) || !write_register(0xd3, g) || !write_register(0xd4, b)) {
        return false;
    }
    last_color = c;
    color_valid = true;
    return true;
}

static bool draw_begin(void)
{
    if (!display_ready() || state.busy) {
        return false;
    }
    state.busy = true;
    return true;
}

static bool draw_end(bool ok)
{
    state.busy = false;
    return ok;
}

bool display_wait_vsync(void)
{
    if (vsync_missing) {
        return true;
    }
    if (!draw_begin()) {
        return false;
    }
    bool ok = vsync_enabled || modify_register(REG_INTEN, 0xff, VSYNC_EVENT);
    vsync_enabled = ok;
    ok = ok && write_register(REG_INTF, VSYNC_EVENT);
    uint32_t start = timebase_now_ms();
    uint8_t flags = 0;
    for (uint32_t n = 1; ok && !(flags & VSYNC_EVENT); n++) {
        if (timebase_now_ms() - start > VSYNC_TIMEOUT_MS) {
            vsync_missing = true;
            break;
        }
        ok = controller_command(REG_INTF) && controller_read(&flags);
        if ((n & 63) == 0) {
            board_service();
        }
    }
    return draw_end(ok);
}

bool display_show(uint32_t address)
{
    if (!draw_begin()) {
        return false;
    }
    return draw_end(wait_controller(8, 0) && dword_register(0x20, address));
}

bool display_draw_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    if (!draw_begin()) {
        return false;
    }
    return draw_end(wait_controller(8, 0) && word_register(0x56, x) && word_register(0x58, y) &&
                    word_register(0x5a, w) && word_register(0x5c, h));
}

bool display_draw_rect(
    uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color, bool fill)
{
    if (!draw_begin()) {
        return false;
    }
    return draw_end(bte_idle() && draw_color(color) && word_register(0x68, x0) &&
                    word_register(0x6a, y0) && word_register(0x6c, x1) && word_register(0x6e, y1) &&
                    write_register(0x76, (uint8_t)(0xa0 | (fill ? 0x40 : 0))));
}

bool display_draw_round_rect(
    uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t radius, uint16_t color)
{
    if (!draw_begin()) {
        return false;
    }
    return draw_end(bte_idle() && draw_color(color) && word_register(0x68, x0) &&
                    word_register(0x6a, y0) && word_register(0x6c, x1) && word_register(0x6e, y1) &&
                    word_register(0x77, radius) && word_register(0x79, radius) &&
                    write_register(0x76, 0xb0));
}

bool display_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    if (!draw_begin()) {
        return false;
    }
    return draw_end(bte_idle() && draw_color(color) && word_register(0x68, x0) &&
                    word_register(0x6a, y0) && word_register(0x6c, x1) && word_register(0x6e, y1) &&
                    write_register(0x67, 0x80));
}

bool display_draw_circle(uint16_t cx, uint16_t cy, uint16_t radius, uint16_t color, bool fill)
{
    if (!draw_begin()) {
        return false;
    }
    return draw_end(bte_idle() && draw_color(color) && word_register(0x7b, cx) &&
                    word_register(0x7d, cy) && word_register(0x77, radius) &&
                    word_register(0x79, radius) &&
                    write_register(0x76, (uint8_t)(0x80 | (fill ? 0x40 : 0))));
}

#define RAW_MAX_Y 2048u
static uint32_t raw_remaining, raw_used;
static unsigned raw_which;
static bool raw_open, raw_full;

bool display_raw_begin(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    if (!display_ready() || state.busy || raw_open) {
        state.error = DISPLAY_ERROR_BUSY;
        return false;
    }
    if (w == 0 || h == 0 || (uint32_t)x + w > WIDTH || (uint32_t)y + h > RAW_MAX_Y) {
        state.error = DISPLAY_ERROR_ARGUMENT;
        return false;
    }
    state.busy = true;
    if (!begin_pixels(x, y, w, h)) {
        goto failed;
    }
    select_controller(true);
    if (!transfer(0x80, 0)) {
        goto failed;
    }
#ifndef DISPLAY_HOST_TEST
    if (tuning.engine == DISPLAY_ENGINE_DMA) {
        dma_setup(spi_wide);
    }
#endif
    raw_remaining = (uint32_t)w * h * (applied_bpp16 ? 2u : 3u);
    raw_used = 0;
    raw_which = 0;
    raw_full = false;
    raw_open = true;
    return true;
failed:
    if (!dma_inflight) {
        stream_open = false;
        chip_select(false);
    }
    state.busy = false;
    return false;
}

static void raw_finish(void)
{
    raw_open = false;
    if (tuning.engine == DISPLAY_ENGINE_DMA) {
        stream_open = true;
    } else {
        chip_select(false);
    }
    state.busy = false;
    state.blits++;
    if (requested_brightness != state.brightness) {
        state.busy = true;
        if (brightness_registers(requested_brightness)) {
            state.brightness = requested_brightness;
        }
        state.busy = false;
    }
}

static bool dma_ready(void)
{
#ifndef DISPLAY_HOST_TEST
    if (dma_inflight && ((R32(DMA_RAW_TFR) & 3) != 3 || (R8(SPI + 0x16) & 0x20) == 0)) {
        return false;
    }
#endif
    return true;
}

static void swap_pixels(uint8_t *p, uint32_t n)
{
    uint32_t *words = (uint32_t *)(void *)p;
    for (uint32_t i = 0; i < n / 4u; i++) {
        uint32_t v = words[i];
        words[i] = ((v >> 8) & UINT32_C(0x00ff00ff)) | ((v << 8) & UINT32_C(0xff00ff00));
    }
    if (n & 2u) {
        uint8_t t = p[n - 2];
        p[n - 2] = p[n - 1];
        p[n - 1] = t;
    }
}

static bool raw_send(void)
{
    if (dma_wide) {
        swap_pixels(staging[raw_which], raw_used);
    }
    if (!send_chunk(staging[raw_which], raw_used)) {
        display_raw_abort();
        return false;
    }
    state.spi_bytes += raw_used;
    raw_used = 0;
    raw_full = false;
    if (tuning.engine == DISPLAY_ENGINE_DMA) {
        raw_which ^= 1u;
    }
    return true;
}

uint32_t display_raw_write(const uint8_t *p, uint32_t n)
{
    if (!raw_open) {
        return 0;
    }
    if (raw_full) {
        if (!dma_ready()) {
            return 0;
        }
        if (!raw_send()) {
            return 0;
        }
    }
    if (n > raw_remaining) {
        n = raw_remaining;
    }
    uint32_t done = 0;
    while (done < n) {
        uint32_t take = STAGE_BYTES - raw_used;
        if (take > n - done) {
            take = n - done;
        }
        memcpy(staging[raw_which] + raw_used, p + done, take);
        raw_used += take;
        done += take;
        raw_remaining -= take;
        if (raw_used == STAGE_BYTES || raw_remaining == 0) {
            if (raw_remaining != 0 && !dma_ready()) {
                raw_full = true;
                break;
            }
            if (!raw_send()) {
                return 0;
            }
        }
    }
    if (raw_remaining == 0) {
        raw_finish();
    }
    return done;
}

void display_raw_abort(void)
{
    if (!raw_open) {
        return;
    }
    raw_open = false;
    raw_full = false;
#ifndef DISPLAY_HOST_TEST
    (void)dma_wait();
    if (dma_wide) {
        spi_frame(false);
    }
#endif
    stream_open = false;
    chip_select(false);
    state.busy = false;
}

bool display_raw_active(void)
{
    return raw_open;
}

static struct {
    uint16_t sx, sy, dx, dy, w, h;
    bool valid;
} strip;

static uint8_t bte_depth(void)
{
    uint8_t d = applied_bpp16 ? 1 : 2;
    return (uint8_t)((d << 5) | (d << 2) | d);
}

static bool strip_word(uint8_t reg, uint16_t *cached, uint16_t value)
{
    if (strip.valid && *cached == value) {
        return true;
    }
    *cached = value;
    return word_register(reg, value);
}

bool display_strips_begin(uint32_t src, uint32_t dst)
{
    if (!draw_begin()) {
        return false;
    }
    strip.valid = false;
    return draw_end(bte_idle() && write_register(0x91, 0xc2) && write_register(0x92, bte_depth()) &&
                    dword_register(0x93, src) && word_register(0x97, WIDTH) &&
                    dword_register(0xa7, dst) && word_register(0xab, WIDTH));
}

bool display_strip(uint16_t sx, uint16_t sy, uint16_t dx, uint16_t dy, uint16_t w, uint16_t h)
{
    if (!draw_begin()) {
        return false;
    }
    bool ok = bte_idle() && strip_word(0x99, &strip.sx, sx) && strip_word(0x9b, &strip.sy, sy) &&
              strip_word(0xad, &strip.dx, dx) && strip_word(0xaf, &strip.dy, dy) &&
              strip_word(0xb1, &strip.w, w) && strip_word(0xb3, &strip.h, h) &&
              modify_register(0x90, 0xff, 0x10);
    strip.valid = ok;
    return draw_end(ok);
}

bool display_blend(uint32_t s0,
                   uint16_t s0x,
                   uint16_t s0y,
                   uint32_t s1,
                   uint16_t s1x,
                   uint16_t s1y,
                   uint32_t dst,
                   uint16_t dx,
                   uint16_t dy,
                   uint16_t w,
                   uint16_t h,
                   uint8_t alpha)
{
    if (!draw_begin()) {
        return false;
    }
    strip.valid = false;
    return draw_end(
        bte_idle() && write_register(0x91, 0x0a) && write_register(0x92, bte_depth()) &&
        dword_register(0x93, s0) && word_register(0x97, WIDTH) && word_register(0x99, s0x) &&
        word_register(0x9b, s0y) && dword_register(0x9d, s1) && word_register(0xa1, WIDTH) &&
        word_register(0xa3, s1x) && word_register(0xa5, s1y) && dword_register(0xa7, dst) &&
        word_register(0xab, WIDTH) && word_register(0xad, dx) && word_register(0xaf, dy) &&
        word_register(0xb1, w) && word_register(0xb3, h) &&
        write_register(0xb5, alpha > 32 ? 32 : alpha) && modify_register(0x90, 0xff, 0x10));
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
    if (!draw_begin()) {
        return false;
    }
    strip.valid = false;
    uint8_t d = applied_bpp16 ? 1 : 2;
    return draw_end(bte_idle() && write_register(0x91, 0xc2) &&
                    write_register(0x92, (uint8_t)((d << 5) | (d << 2) | d)) &&
                    dword_register(0x93, src) && word_register(0x97, WIDTH) &&
                    word_register(0x99, sx) && word_register(0x9b, sy) &&
                    dword_register(0xa7, dst) && word_register(0xab, WIDTH) &&
                    word_register(0xad, dx) && word_register(0xaf, dy) && word_register(0xb1, w) &&
                    word_register(0xb3, h) && modify_register(0x90, 0xff, 0x10));
}
