#include <assert.h>
#include <stdio.h>
#include <string.h>

#define main firmware_main
#include "../src/main.c"
#undef main

static unsigned rectangles;

uint16_t display_width(void)
{
    return 480;
}

bool display_draw_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    assert(x == 0 && y == 0 && w == 480 && h == 2048);
    return true;
}

bool display_draw_rect(
    uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color, bool fill)
{
    assert(x0 <= x1 && x1 < 480 && y0 <= y1 && y1 < 2048);
    assert(color == 0x1234);
    (void)fill;
    rectangles++;
    return true;
}

bool boot_anim_active(void)
{
    return false;
}

void boot_anim_host_drew(uint32_t now_ms)
{
    (void)now_ms;
    assert(false);
}

uint32_t timebase_now_ms(void)
{
    return 0;
}

static bool spi_wide;

void display_set_spi_wide(bool wide)
{
    spi_wide = wide;
}

int main(void)
{
    assert(sink_fill(0, 0, 480, 2048, 0x1234));
    assert(sink_frame(479, 2047, 1, 1, 0x1234));
    assert(rectangles == 2);
    assert(!sink_fill(0, 0, 0, 1, 0x1234));
    assert(!sink_frame(0, 0, 1, 0, 0x1234));
    assert(!sink_fill(479, 0, 2, 1, 0x1234));
    assert(!sink_frame(0, 2047, 1, 2, 0x1234));
    assert(!sink_fill(UINT16_MAX, UINT16_MAX, 2, 2, 0x1234));
    assert(!sink_frame(1, 1, UINT16_MAX, UINT16_MAX, 0x1234));
    assert(rectangles == 2);

    uint8_t out[USB_OUTPUT_REPORT_BYTES] = {2, 1, 0, 8, 50};
    reset_requested = true;
    assert(!usb_device_set_report(2, 2, out, sizeof out));
    assert(ring_used() == 0);
    reset_requested = false;
    for (unsigned i = 0; i < SLOTS - 1u; ++i) {
        assert(usb_device_set_report(2, 2, out, sizeof out));
        assert(memcmp(ring[i], out, sizeof out) == 0);
    }
    assert(!usb_device_output_ready());
    assert(!usb_device_set_report(2, 2, out, sizeof out));
    ring_tail = 1;
    assert(usb_device_output_ready());
    assert(usb_device_set_report(2, 2, out, sizeof out));
    assert(!usb_device_output_ready());

    ring_head = ring_tail = 0;
    uint8_t *slot = usb_device_output_slot();
    assert(slot == ring[0]);
    assert(!usb_device_output_commit(out, sizeof out));
    assert(!usb_device_output_commit(slot, sizeof out - 1u));
    assert(usb_device_output_commit(slot, sizeof out));
    assert(ring_used() == 1 && usb_device_output_slot() == ring[1]);

    uint8_t feature[USB_FEATURE_REPORT_BYTES] = {0x7f, 'O', 'S', 'D', 'B', 'O', 'O', 'T', '?'};
    assert(!usb_device_set_report(3, 0x7f, feature, sizeof feature));
    assert(!bootloader_requested);
    feature[8] = '!';
    assert(!usb_device_set_report(3, 0x7f, feature, sizeof feature - 1u));
    assert(usb_device_set_report(3, 0x7f, feature, sizeof feature));
    assert(bootloader_requested);

    uint8_t tuning[USB_FEATURE_REPORT_BYTES] = {4, 1, 0};
    assert(usb_device_set_report(3, 4, tuning, sizeof tuning));
    assert(spi_wide);
    tuning[1] = 0;
    assert(usb_device_set_report(3, 4, tuning, sizeof tuning));
    assert(!spi_wide);
    puts("Main integration tests passed: drawing bounds, reset, queue capacity, output slots and "
         "feature commands");
    return 0;
}
