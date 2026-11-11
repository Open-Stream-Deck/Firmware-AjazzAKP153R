#include "osd_protocol.h"

static const osd_sink_t *sink;
static osd_stats_t stats;
static uint8_t opcode, args[32], have, need;
static uint32_t pixel_bytes;
static bool in_command, hold, lost;
static const uint8_t *pending;
static uint16_t pending_n;

static uint8_t arg_length(uint8_t op)
{
    switch (op) {
    case 0x00:
        return 0;
    case 0x01:
        return 4;
    case 0x02:
        return 8;
    case 0x03:
        return 10;
    case 0x04:
        return 10;
    case 0x05:
        return 9;
    case 0x06:
        return 10;
    case 0x07:
        return 20;
    case 0x08:
        return 1;
    case 0x09:
        return 4;
    case 0x0a:
        return 8;
    case 0x0c:
        return 0;
    case 0x0d:
        return 29;
    case 0x0e:
        return 11;
    default:
        return 0xff;
    }
}

static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool error(uint8_t reason)
{
    if (pixel_bytes && sink) {
        sink->blit_abort();
    }
    pixel_bytes = 0;
    in_command = false;
    have = 0;
    need = 0;
    pending = 0;
    pending_n = 0;
    lost = true;
    stats.errors++;
    stats.last_error_opcode = opcode;
    stats.last_error_reason = reason;
    return false;
}

void osd_init(const osd_sink_t *s)
{
    sink = s;
    stats = (osd_stats_t){0};
    lost = false;
    osd_reset();
}

void osd_reset(void)
{
    if (pixel_bytes && sink) {
        sink->blit_abort();
    }
    pixel_bytes = 0;
    in_command = false;
    have = need = 0;
    opcode = 0;
    pending = 0;
    pending_n = 0;
}

void osd_abort(uint8_t reason)
{
    (void)error(reason);
}

bool osd_streaming(void)
{
    return pixel_bytes != 0;
}

const osd_stats_t *osd_stats(void)
{
    return &stats;
}

static bool execute(void)
{
    const uint8_t *a = args;
    stats.commands++;
    switch (opcode) {
    case 0x00:
        return true;
    case 0x01:
        return sink->canvas(u32(a)) || error(2);
    case 0x02: {
        uint16_t w = u16(a + 4), h = u16(a + 6);
        if (!w || !h || (uint32_t)w * h > UINT32_MAX / 2u) {
            return error(3);
        }
        if (!sink->blit_begin(u16(a), u16(a + 2), w, h)) {
            return error(2);
        }
        pixel_bytes = (uint32_t)w * h * 2u;
        stats.pixels += (uint32_t)w * h;
        return true;
    }
    case 0x03:
        return sink->fill(u16(a), u16(a + 2), u16(a + 4), u16(a + 6), u16(a + 8)) || error(2);
    case 0x04:
        return sink->line(u16(a), u16(a + 2), u16(a + 4), u16(a + 6), u16(a + 8)) || error(2);
    case 0x05:
        return sink->circle(u16(a), u16(a + 2), u16(a + 4), u16(a + 6), a[8] != 0) || error(2);
    case 0x06:
        return sink->frame(u16(a), u16(a + 2), u16(a + 4), u16(a + 6), u16(a + 8)) || error(2);
    case 0x07:
        return sink->copy(u32(a),
                          u16(a + 4),
                          u16(a + 6),
                          u32(a + 8),
                          u16(a + 12),
                          u16(a + 14),
                          u16(a + 16),
                          u16(a + 18)) ||
               error(2);
    case 0x0d:
        return sink->blend(u32(a),
                           u16(a + 4),
                           u16(a + 6),
                           u32(a + 8),
                           u16(a + 12),
                           u16(a + 14),
                           u32(a + 16),
                           u16(a + 20),
                           u16(a + 22),
                           u16(a + 24),
                           u16(a + 26),
                           a[28]) ||
               error(2);
    case 0x0e:
        return sink->transition(u32(a), a[4], u16(a + 5), u16(a + 7), u16(a + 9)) || error(2);
    case 0x08:
        sink->brightness(a[0] > 100 ? 100 : a[0]);
        return true;
    case 0x09:
        sink->fence(u32(a));
        return true;
    case 0x0a:
        for (unsigned i = 0; i < 8; ++i) {
            if (a[i] != (uint8_t)OSD_BOOT_MAGIC[i]) {
                return error(3);
            }
        }
        sink->bootloader();
        return true;
    case 0x0c:
        return sink->vsync() || error(2);
    default:
        return error(1);
    }
}

static bool feed(const uint8_t *p, uint16_t n)
{
    pending = 0;
    pending_n = 0;
    while (n) {
        if (pixel_bytes) {
            uint32_t take = pixel_bytes < n ? pixel_bytes : n;
            uint32_t used = sink->blit_write(p, take);
            pixel_bytes -= used;
            p += used;
            n = (uint16_t)(n - used);
            if (used != take) {
                if (!sink->blit_active()) {
                    return error(5);
                }
                pending = p;
                pending_n = n;
                return true;
            }
            continue;
        }
        if (!in_command) {
            if (hold) {
                pending = p;
                pending_n = n;
                return true;
            }
            opcode = *p++;
            n--;
            need = arg_length(opcode);
            if (need == 0xff) {
                return error(1);
            }
            have = 0;
            in_command = true;
        }
        while (have < need && n) {
            args[have++] = *p++;
            n--;
        }
        if (have < need) {
            break;
        }
        in_command = false;
        if (!execute()) {
            return false;
        }
    }
    return true;
}

bool osd_report(const uint8_t *r, uint16_t length)
{
    if (!sink || !r || length < 3 || r[0] != 2) {
        return error(4);
    }
    uint16_t n = u16(r + 1);
    if (n & 0x8000u) {
        if (pixel_bytes || in_command) {
            stats.errors++;
            stats.last_error_reason = 6;
        }
        osd_reset();
        lost = false;
        n &= 0x7fffu;
    }
    if (n > OSD_PAYLOAD_MAX || n > length - 3u) {
        return error(4);
    }
    if (lost) {
        return true;
    }
    stats.reports++;
    stats.bytes += n;
    return feed(r + 3, n);
}

bool osd_resume(void)
{
    return pending_n == 0 || feed(pending, pending_n);
}

bool osd_pending(void)
{
    return pending_n != 0;
}

void osd_hold(bool enabled)
{
    hold = enabled;
}
