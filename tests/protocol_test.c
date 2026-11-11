#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "osd_protocol.h"

static uint32_t canvas, fence, blit_bytes, aborts, copies, fills, lines, circles, frames;
static uint16_t blit_w, blit_h, fill_color, copy_args[6], blend_args[8];
static uint8_t blend_alpha;
static unsigned transitions;
static uint8_t bright, last_byte;
static bool blit_open, fail_fill, fail_write;

static bool s_canvas(uint32_t a)
{
    canvas = a;
    return true;
}

static bool s_blit_begin(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    (void)x;
    (void)y;
    assert(!blit_open);
    blit_open = true;
    blit_w = w;
    blit_h = h;
    blit_bytes = 0;
    return true;
}

static uint32_t s_blit_write(const uint8_t *p, uint32_t n)
{
    assert(blit_open);
    if (fail_write) {
        return 0;
    }
    for (uint32_t i = 0; i < n; i++) {
        assert(p[i] == (uint8_t)(blit_bytes + i));
    }
    blit_bytes += n;
    last_byte = p[n - 1];
    if (blit_bytes == (uint32_t)blit_w * blit_h * 2u) {
        blit_open = false;
    }
    return n;
}

static void s_abort(void)
{
    blit_open = false;
    aborts++;
}

static bool s_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    fill_color = c;
    fills++;
    return !fail_fill;
}

static bool s_line(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint16_t e)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    lines++;
    return true;
}

static bool s_circle(uint16_t a, uint16_t b, uint16_t r, uint16_t c, bool f)
{
    (void)a;
    (void)b;
    (void)r;
    (void)c;
    (void)f;
    circles++;
    return true;
}

static bool s_frame(uint16_t a, uint16_t b, uint16_t c, uint16_t d, uint16_t e)
{
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
    frames++;
    return true;
}

static bool s_copy(uint32_t src,
                   uint16_t sx,
                   uint16_t sy,
                   uint32_t dst,
                   uint16_t dx,
                   uint16_t dy,
                   uint16_t w,
                   uint16_t h)
{
    copies++;
    copy_args[0] = sx;
    copy_args[1] = sy;
    copy_args[2] = dx;
    copy_args[3] = dy;
    copy_args[4] = w;
    copy_args[5] = h;
    assert(src == 0x100000u && dst == 0);
    return true;
}

static bool s_blend(uint32_t s0,
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
    blend_args[0] = s0x;
    blend_args[1] = s0y;
    blend_args[2] = s1x;
    blend_args[3] = s1y;
    blend_args[4] = dx;
    blend_args[5] = dy;
    blend_args[6] = w;
    blend_args[7] = h;
    blend_alpha = alpha;
    assert(s0 == 0x100000u && s1 == 0x200000u && dst == 0);
    return true;
}

static bool
s_transition(uint32_t next, uint8_t effect, uint16_t duration_ms, uint16_t ox, uint16_t oy)
{
    transitions++;
    assert(next == 0xc00000u && effect == 1 && duration_ms == 420 && ox == 89 && oy == 485);
    return true;
}

static void s_bright(uint8_t p)
{
    bright = p;
}

static unsigned boots;

static void s_boot(void)
{
    boots++;
}

static unsigned vsyncs;

static bool s_vsync(void)
{
    vsyncs++;
    return true;
}

static bool s_blit_active(void)
{
    return blit_open && !fail_write;
}

static void s_fence(uint32_t id)
{
    fence = id;
}

static const osd_sink_t sink = {s_canvas,
                                s_blit_begin,
                                s_blit_write,
                                s_abort,
                                s_blit_active,
                                s_fill,
                                s_line,
                                s_circle,
                                s_frame,
                                s_copy,
                                s_blend,
                                s_transition,
                                s_bright,
                                s_fence,
                                s_boot,
                                s_vsync};

static uint8_t report[1024];

static bool send(const uint8_t *payload, uint16_t n, bool sync)
{
    memset(report, 0, sizeof report);
    report[0] = 2;
    uint16_t len = (uint16_t)(n | (sync ? 0x8000u : 0));
    report[1] = (uint8_t)len;
    report[2] = (uint8_t)(len >> 8);
    memcpy(report + 3, payload, n);
    return osd_report(report, sizeof report);
}

static uint16_t put16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    return 2;
}

static uint16_t put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        p[i] = (uint8_t)(v >> (8 * i));
    }
    return 4;
}

int main(void)
{
    osd_init(&sink);
    uint8_t s[4096];
    uint16_t n = 0;
    s[n++] = 0x01;
    n += put32(s + n, 0x00123456);
    s[n++] = 0x03;
    n += put16(s + n, 1);
    n += put16(s + n, 2);
    n += put16(s + n, 3);
    n += put16(s + n, 4);
    n += put16(s + n, 0xf81f);
    s[n++] = 0x07;
    n += put32(s + n, 0x100000);
    n += put16(s + n, 10);
    n += put16(s + n, 20);
    n += put32(s + n, 0);
    n += put16(s + n, 30);
    n += put16(s + n, 40);
    n += put16(s + n, 103);
    n += put16(s + n, 104);
    s[n++] = 0x0d;
    n += put32(s + n, 0x100000);
    n += put16(s + n, 1);
    n += put16(s + n, 2);
    n += put32(s + n, 0x200000);
    n += put16(s + n, 3);
    n += put16(s + n, 4);
    n += put32(s + n, 0);
    n += put16(s + n, 5);
    n += put16(s + n, 6);
    n += put16(s + n, 103);
    n += put16(s + n, 104);
    s[n++] = 16;
    s[n++] = 0x0e;
    n += put32(s + n, 0xc00000);
    s[n++] = 1;
    n += put16(s + n, 420);
    n += put16(s + n, 89);
    n += put16(s + n, 485);
    s[n++] = 0x04;
    for (int i = 0; i < 5; i++) {
        n += put16(s + n, (uint16_t)i);
    }
    s[n++] = 0x05;
    for (int i = 0; i < 4; i++) {
        n += put16(s + n, (uint16_t)i);
    }
    s[n++] = 1;
    s[n++] = 0x06;
    for (int i = 0; i < 5; i++) {
        n += put16(s + n, (uint16_t)i);
    }
    s[n++] = 0x08;
    s[n++] = 250;
    s[n++] = 0x09;
    n += put32(s + n, 0xdeadbeef);
    s[n++] = 0x00;
    s[n++] = 0x0c;
    assert(send(s, n, true));
    assert(canvas == 0x123456 && fill_color == 0xf81f && copies == 1 && lines == 1 &&
           circles == 1 && frames == 1 && vsyncs == 1);
    assert(copy_args[0] == 10 && copy_args[3] == 40 && copy_args[4] == 103 && copy_args[5] == 104);
    assert(blend_args[0] == 1 && blend_args[1] == 2 && blend_args[2] == 3 && blend_args[3] == 4 &&
           blend_args[4] == 5 && blend_args[5] == 6 && blend_args[6] == 103 &&
           blend_args[7] == 104 && blend_alpha == 16 && transitions == 1);
    assert(bright == 100 && fence == 0xdeadbeef && osd_stats()->commands == 12 &&
           osd_stats()->errors == 0);

    n = 0;
    s[n++] = 0x02;
    n += put16(s + n, 5);
    n += put16(s + n, 6);
    n += put16(s + n, 30);
    n += put16(s + n, 30);
    for (uint32_t i = 0; i < 1800; i++) {
        s[n++] = (uint8_t)i;
    }
    s[n++] = 0x09;
    n += put32(s + n, 7);
    assert(send(s, 4, true));
    assert(!blit_open);
    assert(send(s + 4, 700, false));
    assert(blit_open && blit_bytes == 700 - 5);
    assert(send(s + 704, 1021, false));
    assert(send(s + 1725, (uint16_t)(n - 1725), false));
    assert(!blit_open && blit_bytes == 1800 && fence == 7 && last_byte == (uint8_t)1799);

    uint32_t errors = osd_stats()->errors;
    uint8_t bad[] = {0x77, 0x03};
    assert(!send(bad, 2, true));
    assert(osd_stats()->errors == errors + 1 && osd_stats()->last_error_reason == 1);
    bad[0] = 0x0b;
    assert(!send(bad, 2, true));
    assert(osd_stats()->errors == errors + 2 && osd_stats()->last_error_reason == 1);
    n = 0;
    s[n++] = 0x08;
    s[n++] = 42;
    assert(send(s, n, true) && bright == 42);

    n = 0;
    s[n++] = 0x02;
    n += put16(s + n, 0);
    n += put16(s + n, 0);
    n += put16(s + n, 100);
    n += put16(s + n, 100);
    for (int i = 0; i < 100; i++) {
        s[n++] = (uint8_t)i;
    }
    assert(send(s, n, true) && blit_open && osd_streaming());
    uint32_t aborted = aborts;
    n = 0;
    s[n++] = 0x00;
    assert(send(s, n, true));
    assert(aborts == aborted + 1 && !osd_streaming());

    fail_fill = true;
    n = 0;
    s[n++] = 0x03;
    for (int i = 0; i < 5; i++) {
        n += put16(s + n, 1);
    }
    assert(!send(s, n, true));
    fail_fill = false;
    memset(report, 0, sizeof report);
    report[0] = 2;
    report[1] = 0xfe;
    report[2] = 0x03;
    assert(!osd_report(report, 1024));
    report[0] = 1;
    report[1] = 0;
    report[2] = 0;
    assert(!osd_report(report, 1024));
    n = 0;
    s[n++] = 0x02;
    n += put16(s + n, 0);
    n += put16(s + n, 0);
    n += put16(s + n, 0);
    n += put16(s + n, 5);
    assert(!send(s, n, true));

    uint32_t filled = fills;
    n = 0;
    for (int k = 0; k < 2; k++) {
        s[n++] = 0x03;
        for (int i = 0; i < 5; i++) {
            n += put16(s + n, 1);
        }
    }
    osd_hold(true);
    assert(send(s, n, true) && osd_pending() && fills == filled);
    assert(osd_resume() && osd_pending() && fills == filled);
    osd_hold(false);
    assert(osd_resume() && !osd_pending() && fills == filled + 2);

    n = 0;
    s[n++] = 0x02;
    n += put16(s + n, 0);
    n += put16(s + n, 0);
    n += put16(s + n, 30);
    n += put16(s + n, 30);
    for (uint32_t i = 0; i < 1800; i++) {
        s[n++] = (uint8_t)i;
    }
    s[n++] = 0x03;
    for (int i = 0; i < 5; i++) {
        n += put16(s + n, 1);
    }
    assert(send(s, 500, true) && blit_open && !osd_pending());
    osd_hold(true);
    assert(send(s + 500, 1021, false) && blit_open && !osd_pending());
    assert(send(s + 1521, (uint16_t)(n - 1521), false));
    assert(!blit_open && blit_bytes == 1800 && osd_pending() && !osd_streaming());
    assert(fills == filled + 2);
    osd_hold(false);
    assert(osd_resume() && !osd_pending() && fills == filled + 3);

    osd_hold(true);
    assert(send(s + 1809, 11, true) && osd_pending());
    osd_reset();
    assert(!osd_pending());
    osd_hold(false);

    n = 0;
    s[n++] = 0x0a;
    memcpy(s + n, "OSDBOOT?", 8);
    n += 8;
    assert(!send(s, n, true) && boots == 0 && osd_stats()->last_error_reason == 3);
    memcpy(s + 1, OSD_BOOT_MAGIC, 8);
    assert(send(s, n, true) && boots == 1);

    uint8_t blit[] = {2, 0, 0, 0, 0, 1, 0, 1, 0};
    assert(send(blit, sizeof blit, true) && osd_streaming());
    aborted = aborts;
    fail_write = true;
    uint8_t pixel[] = {0, 1};
    assert(!send(pixel, sizeof pixel, false));
    assert(aborts == aborted + 1 && !blit_open && !osd_streaming());
    assert(osd_stats()->last_error_reason == 5);
    fail_write = false;

    assert(send(blit, sizeof blit, true) && osd_streaming());
    aborted = aborts;
    report[0] = 2;
    report[1] = 0xfe;
    report[2] = 3;
    assert(!osd_report(report, sizeof report));
    assert(aborts == aborted + 1 && !blit_open && !osd_streaming());
    assert(send(blit, sizeof blit, true));
    assert(send(pixel, sizeof pixel, false) && !osd_streaming());

    assert(send(blit, sizeof blit, true) && osd_streaming());
    fail_write = true;
    assert(!send(pixel, sizeof pixel, false));
    fail_write = false;
    errors = osd_stats()->errors;
    uint32_t before = fills;
    n = 0;
    s[n++] = 0x03;
    for (int i = 0; i < 5; i++) {
        n += put16(s + n, 1);
    }
    s[n++] = 0x09;
    n += put32(s + n, 99);
    assert(send(s, n, false) && fills == before && fence != 99);
    assert(osd_stats()->errors == errors);
    assert(send(s, n, true) && fills == before + 1 && fence == 99);

    assert(send(blit, sizeof blit, true) && osd_streaming());
    osd_abort(7);
    assert(!osd_streaming() && osd_stats()->last_error_reason == 7);
    assert(send(s, n, false) && fills == before + 1);
    assert(send(s, n, true) && fills == before + 2);

    blit[5] = blit[6] = blit[7] = blit[8] = 0xff;
    assert(!send(blit, sizeof blit, true));
    assert(!blit_open && !osd_streaming() && osd_stats()->last_error_reason == 3);
    puts("OSD protocol tests passed: batching, split args, multi-report pixels, errors, SYNC "
         "recovery, resync after error, hold, boot");
    return 0;
}
