#include "display.h"
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

void display_test_set_ready(void);
static volatile uint8_t mmio8;
static volatile uint16_t mmio16;
static volatile uint32_t mmio32;

volatile uint8_t *display_test_mmio8(uint32_t a)
{
    (void)a;
    return &mmio8;
}

volatile uint16_t *display_test_mmio16(uint32_t a)
{
    (void)a;
    return &mmio16;
}

volatile uint32_t *display_test_mmio32(uint32_t a)
{
    (void)a;
    return &mmio32;
}

void platform_delay_iterations(uint32_t n)
{
    (void)n;
}

void platform_delay_ms(uint32_t n)
{
    (void)n;
}

uint32_t platform_core_hz(void)
{
    return 150000000;
}

static uint32_t clock_ms, clock_step;

uint32_t timebase_now_ms(void)
{
    clock_ms += clock_step;
    return clock_ms;
}

static uint8_t transaction[4096], pixel_bytes[4096], reg, intf;
static size_t transaction_n, pixel_n, total_bytes;
static unsigned window_writes[256], pixel_transactions, services;
static size_t fail_after = SIZE_MAX;
static bool cs_selected, brightness_during_service;
static bool verify_busy_reentry;

bool display_test_byte(uint8_t out, uint8_t *in)
{
    assert(cs_selected);
    if (total_bytes == fail_after) {
        return false;
    }
    total_bytes++;
    assert(transaction_n < sizeof transaction);
    transaction[transaction_n++] = out;
    if (in) {
        *in = transaction_n == 2 && transaction[0] == 0xc0 && reg == 0x0c ? intf : 0;
    }
    return true;
}

void display_test_cs(bool selected)
{
    if (selected) {
        assert(!cs_selected);
        cs_selected = true;
        transaction_n = 0;
        return;
    }
    if (!cs_selected) {
        return;
    }
    if (transaction_n == 2 && transaction[0] == 0) {
        reg = transaction[1];
    } else if (transaction_n >= 2 && transaction[0] == 0x80) {
        if (reg == 4) {
            assert(pixel_n + transaction_n - 1 <= sizeof pixel_bytes);
            memcpy(pixel_bytes + pixel_n, transaction + 1, transaction_n - 1);
            pixel_n += transaction_n - 1;
            pixel_transactions++;
        } else {
            window_writes[reg]++;
        }
    }
    cs_selected = false;
}

void board_service(void)
{
    services++;
    if (verify_busy_reentry) {
        uint16_t pixel = 0;
        size_t before = total_bytes;
        assert(!display_blit_rgb565(0, 0, 1, 1, &pixel, 1));
        assert(total_bytes == before);
    }
    if (brightness_during_service) {
        size_t before = total_bytes;
        display_set_brightness(17);
        assert(total_bytes == before);
    }
}

static void reset(void)
{
    mmio8 = 0;
    mmio16 = 0;
    mmio32 = 0;
    transaction_n = pixel_n = total_bytes = 0;
    reg = intf = 0;
    clock_ms = clock_step = 0;
    services = pixel_transactions = 0;
    memset(window_writes, 0, sizeof window_writes);
    fail_after = SIZE_MAX;
    cs_selected = false;
    brightness_during_service = false;
    verify_busy_reentry = false;
    display_test_set_ready();
}

int main(void)
{
    reset();
    const uint16_t pixels[] = {0xf800, 0x07e0, 0xdead, 0x001f, 0xffff, 0xdead, 0, 0x8410, 0xdead};
    assert(display_blit_rgb565(49, 146, 2, 3, pixels, 3));
    const uint8_t expected[] = {
        0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 0, 0, 0, 132, 130, 132};
    assert(pixel_transactions == 1 && pixel_n == sizeof expected);
    assert(memcmp(pixel_bytes, expected, sizeof expected) == 0);
    for (unsigned a = 0x56; a <= 0x5d; a++) {
        assert(window_writes[a] == 1);
    }
    for (unsigned a = 0x5f; a <= 0x62; a++) {
        assert(window_writes[a] == 1);
    }
    assert(display_status()->pixels_written == 6 && display_status()->blits == 1);
    assert(!cs_selected && services == 1);
    size_t before = total_bytes;
    assert(!display_blit_rgb565(479, 0, 2, 1, pixels, 3));
    assert(total_bytes == before && display_ready());

    reset();
    uint16_t block[300];
    for (unsigned i = 0; i < 300; i++) {
        block[i] = 0xffff;
    }
    brightness_during_service = true;
    verify_busy_reentry = true;
    assert(display_blit_rgb565(0, 0, 10, 30, block, 10));
    assert(pixel_transactions == 1 && pixel_n == 900 && services == 1);
    assert(display_status()->brightness == 17 && !cs_selected);

    reset();
    fail_after = 3;
    assert(!display_blit_rgb565(0, 0, 2, 3, pixels, 3));
    assert(!display_ready() && !display_status()->busy && !cs_selected);
    assert(display_status()->error == DISPLAY_ERROR_SPI_RX);
    reset();
    fail_after = 62;
    assert(!display_blit_rgb565(0, 0, 2, 3, pixels, 3));
    assert(pixel_transactions == 1 && pixel_n == 3);
    assert(!display_ready() && !display_status()->busy && !cs_selected);
    assert(display_status()->error == DISPLAY_ERROR_SPI_RX);

    reset();
    intf = 0x10;
    assert(display_wait_vsync() && display_wait_vsync());
    assert(window_writes[0x0b] == 1 && window_writes[0x0c] == 2 && !cs_selected);

    reset();
    clock_step = 1;
    assert(display_wait_vsync());
    before = total_bytes;
    assert(display_wait_vsync() && total_bytes == before && display_ready());
    puts("display: one rectangle/window/stream, BGR565 expansion, stride, bounds, service, "
         "failure cleanup and vsync wait passed");
    return 0;
}
