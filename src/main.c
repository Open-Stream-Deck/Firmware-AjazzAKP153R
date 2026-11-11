#include <stdint.h>

#include "boot_anim.h"
#include "display.h"
#include "firmware_version.h"
#include "idle_game.h"
#include "keys.h"
#include "memory.h"
#include "osd_protocol.h"
#include "platform.h"
#include "profile.h"
#include "timebase.h"
#include "transition.h"
#include "usb_device.h"

#define CAPABILITY_BOOT 1u
#define CAPABILITY_VSYNC 4u
#define CAPABILITY_BLEND 8u
#define CAPABILITY_TRANSITION 16u
#define SLOTS 24u
#define STREAM_TIMEOUT_MS 1000u
#define DRAW_HEIGHT 2048u

volatile uint32_t osd_boot_stage;
uint32_t profile_cycles[PROFILE_COUNT];
uint32_t profile_loops;
static uint8_t ring[SLOTS][USB_OUTPUT_REPORT_BYTES] __attribute__((aligned(4)));
static volatile uint8_t ring_head, ring_tail;
static uint8_t ring_peak;
static bool usb_ready, keys_ready, service_busy, reset_requested, window_full;
static bool bootloader_requested;
static uint32_t last_reset, sent_configuration, sent_generation, generation, last_fence;
static uint32_t last_progress_ms, host_canvas;
static uint16_t reported_keys;
static uint8_t event_sequence, stream_errors, brightness = 80;
static bool asleep;

static void put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i) {
        p[i] = (uint8_t)(v >> (i * 8));
    }
}

static uint8_t ring_used(void)
{
    return (uint8_t)((ring_head + SLOTS - ring_tail) % SLOTS);
}

bool usb_device_output_ready(void)
{
    return !reset_requested && ring_used() < SLOTS - 1u;
}

static void ring_push(void)
{
    ring_head = (uint8_t)((ring_head + 1u) % SLOTS);
    uint8_t used = ring_used();
    if (used > ring_peak) {
        ring_peak = used;
    }
}

uint8_t *usb_device_output_slot(void)
{
    return ring[ring_head];
}

bool usb_device_output_commit(const uint8_t *data, uint16_t n)
{
    if (data != ring[ring_head] || n != USB_OUTPUT_REPORT_BYTES || !usb_device_output_ready()) {
        return false;
    }
    ring_push();
    return true;
}

static bool boot_request(const uint8_t *data, uint16_t n)
{
    static const char magic[] = "OSDBOOT!";
    if (n != USB_FEATURE_REPORT_BYTES) {
        return false;
    }
    for (unsigned i = 0; i < 8; ++i) {
        if (data[1 + i] != (uint8_t)magic[i]) {
            return false;
        }
    }
    return true;
}

bool usb_device_set_report(uint8_t type, uint8_t id, const uint8_t *data, uint16_t n)
{
    if (type == 3 && id == 0x7f && boot_request(data, n)) {
        bootloader_requested = true;
        return true;
    }
    if (type == 3 && id == 4 && n == USB_FEATURE_REPORT_BYTES) {
        display_set_spi_wide(data[1] != 0);
        return true;
    }
    if (type != 2 || id != 2 || n != USB_OUTPUT_REPORT_BYTES || !usb_device_output_ready()) {
        return false;
    }
    memcpy(ring[ring_head], data, n);
    ring_push();
    return true;
}

static void input_report(uint8_t *out)
{
    for (unsigned i = 0; i < USB_INPUT_REPORT_BYTES; ++i) {
        out[i] = 0;
    }
    out[0] = 1;
    put16(out + 1, reported_keys);
    out[3] = event_sequence;
    put32(out + 4, last_fence);
    out[8] = stream_errors;
}

uint16_t usb_device_get_report(uint8_t type, uint8_t id, uint8_t *out)
{
    if (type == 1 && id == 1) {
        input_report(out);
        return USB_INPUT_REPORT_BYTES;
    }
    if (type != 3) {
        return 0;
    }
    for (unsigned i = 0; i < USB_FEATURE_REPORT_BYTES; ++i) {
        out[i] = 0;
    }
    out[0] = id;
    if (id == 3) {
        out[1] = 'O';
        out[2] = 'S';
        out[3] = 'D';
        out[4] = '1';
        out[5] = OSD_PROTOCOL_VERSION;
        out[6] = FIRMWARE_VERSION_MAJOR;
        out[7] = FIRMWARE_VERSION_MINOR;
        out[8] = FIRMWARE_VERSION_PATCH;
        put16(out + 9, display_width());
        put16(out + 11, display_height());
        out[13] = 16;
        out[14] = 15;
        out[15] = 16;
        out[16] = SLOTS;
        out[17] = CAPABILITY_BOOT | CAPABILITY_VSYNC | CAPABILITY_BLEND | CAPABILITY_TRANSITION;
        return USB_FEATURE_REPORT_BYTES;
    }
    if (id == 0x7e) {
        const volatile display_status_t *d = display_status();
        const osd_stats_t *s = osd_stats();
        out[1] = 'O';
        out[2] = 'S';
        out[3] = 'D';
        out[4] = 'D';
        out[5] = (uint8_t)d->stage;
        out[6] = (uint8_t)d->error;
        out[7] = (keys_ready ? 1 : 0) | (display_ready() ? 2 : 0) |
                 (usb_diagnostics.high_speed ? 4 : 0) | (osd_streaming() ? 8 : 0);
        out[8] = ring_used();
        out[9] = ring_peak;
        put16(out + 10, keys_raw());
        put32(out + 12, s->reports);
        put32(out + 16, s->commands);
        put32(out + 20, s->errors);
        out[24] = s->last_error_opcode;
        out[25] = s->last_error_reason;
        out[26] = (uint8_t)osd_boot_stage;
        put32(out + 28, timebase_now_ms());
        return USB_FEATURE_REPORT_BYTES;
    }
    if (id == 0x7d) {
        for (unsigned i = 0; i < PROFILE_COUNT; ++i) {
            put32(out + 1 + 4 * i, profile_cycles[i]);
        }
        put32(out + 1 + 4 * PROFILE_COUNT, profile_loops);
        const transition_stats_t *t = transition_stats();
        uint32_t mhz = platform_core_hz() / 1000000u;
        uint32_t us = mhz ? t->worst_cycles / mhz : 0;
        out[29] = (uint8_t)(t->frames > 255u ? 255u : t->frames);
        put16(out + 30, (uint16_t)(us > UINT16_MAX ? UINT16_MAX : us));
        return USB_FEATURE_REPORT_BYTES;
    }
    if (id == 4) {
        put32(out + 1, platform_core_hz());
        out[5] = timebase_uses_dwt() ? 1 : 0;
        out[6] = display_spi_wide() ? 1 : 0;
        display_spi_registers(out + 8, 16);
        put32(out + 24, display_spi_busy());
        put32(out + 28, display_status()->spi_bytes);
        return USB_FEATURE_REPORT_BYTES;
    }
    return 0;
}

void board_service(void)
{
    if (service_busy) {
        return;
    }
    service_busy = true;
    platform_watchdog_feed();
    uint32_t now = timebase_now_ms();
    if (keys_ready) {
        keys_poll(now);
        uint16_t pressed = keys_pressed();
        if (pressed != reported_keys) {
            reported_keys = pressed;
            event_sequence++;
            generation++;
        }
    }
    if (usb_ready) {
        usb_device_poll();
        if (usb_diagnostics.resets != last_reset) {
            last_reset = usb_diagnostics.resets;
            reset_requested = true;
        }
        if (usb_device_configured() && !reset_requested &&
            (sent_configuration != usb_diagnostics.configured || sent_generation != generation)) {
            uint8_t report[USB_INPUT_REPORT_BYTES];
            input_report(report);
            if (usb_device_send_report(report, sizeof report)) {
                sent_configuration = usb_diagnostics.configured;
                sent_generation = generation;
            }
        }
    }
    service_busy = false;
}

static bool ensure_window(void)
{
    if (window_full) {
        return true;
    }
    window_full = display_draw_window(0, 0, display_width(), DRAW_HEIGHT);
    return window_full;
}

static void screen_drawn(uint32_t canvas)
{
    if (canvas == 0 && boot_anim_active()) {
        boot_anim_host_drew(timebase_now_ms());
    }
}

static bool sink_canvas(uint32_t a)
{
    if (!display_set_canvas(a)) {
        return false;
    }
    host_canvas = a;
    return true;
}

static bool sink_blit_begin(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    window_full = false;
    screen_drawn(host_canvas);
    return display_raw_begin(x, y, w, h);
}

static bool sink_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c)
{
    if (!w || !h || (uint32_t)x + w > display_width() || (uint32_t)y + h > DRAW_HEIGHT) {
        return false;
    }
    screen_drawn(host_canvas);
    return ensure_window() &&
           display_draw_rect(x, y, (uint16_t)(x + w - 1u), (uint16_t)(y + h - 1u), c, true);
}

static bool sink_frame(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c)
{
    if (!w || !h || (uint32_t)x + w > display_width() || (uint32_t)y + h > DRAW_HEIGHT) {
        return false;
    }
    screen_drawn(host_canvas);
    return ensure_window() &&
           display_draw_rect(x, y, (uint16_t)(x + w - 1u), (uint16_t)(y + h - 1u), c, false);
}

static bool sink_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t c)
{
    screen_drawn(host_canvas);
    return ensure_window() && display_draw_line(x0, y0, x1, y1, c);
}

static bool sink_circle(uint16_t x, uint16_t y, uint16_t r, uint16_t c, bool fill)
{
    screen_drawn(host_canvas);
    return ensure_window() && display_draw_circle(x, y, r, c, fill);
}

static bool sink_copy(uint32_t src,
                      uint16_t sx,
                      uint16_t sy,
                      uint32_t dst,
                      uint16_t dx,
                      uint16_t dy,
                      uint16_t w,
                      uint16_t h)
{
    screen_drawn(dst);
    return display_copy(src, sx, sy, dst, dx, dy, w, h);
}

static bool sink_blend(uint32_t s0,
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
    screen_drawn(dst);
    return display_blend(s0, s0x, s0y, s1, s1x, s1y, dst, dx, dy, w, h, alpha);
}

static bool
sink_transition(uint32_t next, uint8_t effect, uint16_t duration_ms, uint16_t ox, uint16_t oy)
{
    screen_drawn(0);
    if (boot_anim_active()) {
        effect = TRANSITION_CUT;
    }
    bool ok = transition_start(next, effect, duration_ms, ox, oy, timebase_now_ms());
    if (transition_active()) {
        osd_hold(true);
    }
    return ok;
}

static void sink_brightness(uint8_t p)
{
    brightness = p;
    if (!asleep) {
        display_set_brightness(p);
    }
}

static void follow_suspend(void)
{
    if (asleep == (usb_diagnostics.suspended != 0)) {
        return;
    }
    asleep = !asleep;
    display_set_brightness(asleep ? 0 : brightness);
}

static void sink_fence(uint32_t id)
{
    last_fence = id;
    generation++;
}

static void sink_bootloader(void)
{
    bootloader_requested = true;
}

static void boot_screen(void)
{
    idle_game_reset();
    window_full = false;
    if (!ensure_window() || !boot_anim_start(timebase_now_ms())) {
        (void)display_fill_rgb565(0);
    }
    host_canvas = 0;
}

static void boot_animation(void)
{
    if (!boot_anim_waiting()) {
        idle_game_suspend();
    }
    if (!boot_anim_active() || osd_streaming() || display_raw_active() || !ensure_window()) {
        return;
    }
    bool drew = boot_anim_step(timebase_now_ms());
    if (boot_anim_waiting()) {
        drew = idle_game_step(timebase_now_ms(), keys_pressed()) || drew;
    }
    if (drew) {
        window_full = false;
        (void)display_set_canvas(host_canvas);
    }
}

static void stream_error(void)
{
    stream_errors++;
    generation++;
}

static void process_ring(void)
{
    for (unsigned n = 0; n < 4 && ring_tail != ring_head; ++n) {
        uint32_t since = profile_now();
        bool ok =
            osd_pending() ? osd_resume() : osd_report(ring[ring_tail], USB_OUTPUT_REPORT_BYTES);
        profile_add(PROFILE_PROCESS, since);
        if (!ok) {
            stream_error();
        }
        last_progress_ms = timebase_now_ms();
        if (osd_pending()) {
            board_service();
            return;
        }
        ring_tail = (uint8_t)((ring_tail + 1u) % SLOTS);
        board_service();
    }
}

int main(void)
{
    const osd_sink_t sink = {
        .canvas = sink_canvas,
        .blit_begin = sink_blit_begin,
        .blit_write = display_raw_write,
        .blit_abort = display_raw_abort,
        .blit_active = display_raw_active,
        .fill = sink_fill,
        .line = sink_line,
        .circle = sink_circle,
        .frame = sink_frame,
        .copy = sink_copy,
        .blend = sink_blend,
        .transition = sink_transition,
        .brightness = sink_brightness,
        .fence = sink_fence,
        .bootloader = sink_bootloader,
        .vsync = display_wait_vsync,
    };
    osd_boot_stage = 1;
    if (!platform_init()) {
        for (;;) {
            __asm__ volatile("nop");
        }
    }
    (void)timebase_init(platform_core_hz());
    platform_watchdog_init();
    osd_init(&sink);
    osd_boot_stage = 2;
    keys_ready = keys_init();
    osd_boot_stage = 3;
    if (display_init()) {
        display_set_brightness(brightness);
        boot_screen();
    }
    osd_boot_stage = 4;
    usb_device_init();
    last_reset = usb_diagnostics.resets;
    usb_ready = true;
    for (;;) {
        uint32_t loop_start = profile_now();
        board_service();
        follow_suspend();
        if (reset_requested) {
            osd_reset();
            ring_tail = ring_head;
            reset_requested = false;
            sent_configuration = 0;
            sent_generation = 0;
        }
        osd_boot_stage = usb_device_configured() ? 6 : 5;
        (void)transition_step(timebase_now_ms());
        osd_hold(boot_anim_due(timebase_now_ms()) || transition_active());
        process_ring();
        if (osd_streaming() && timebase_now_ms() - last_progress_ms > STREAM_TIMEOUT_MS) {
            osd_abort(7);
            stream_error();
        }
        if (bootloader_requested) {
            display_set_brightness(0);
            (void)display_flush();
            platform_enter_bootloader();
        }
        boot_animation();
        if (!display_raw_active()) {
            display_sync_brightness();
        }
        uint32_t flush_start = profile_now();
        (void)display_flush();
        profile_add(PROFILE_FLUSH, flush_start);
        profile_add(PROFILE_LOOP, loop_start);
        profile_loops++;
    }
}
