#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "usb_device.h"

static uint8_t regs[128], indexed[3][32], fifo[3][2048], sent[3][2048];
static uint16_t rp[3], wp[3], count[3];
static uint8_t index;
static unsigned set_calls, get_calls;
static uint8_t set_type, set_id, set_data[USB_CONTROL_BYTES];
static uint16_t set_length;
static bool reject_set, bad_get;
static bool output_ready = true;

bool usb_device_output_ready(void)
{
    return output_ready;
}

static uint8_t *reg(uint32_t o)
{
    return o >= 0x10 && o < 0x20 ? &indexed[index][o - 0x10] : &regs[o];
}

uint8_t usb_test_read8(uint32_t o)
{
    if (o >= 0x20 && o <= 0x28 && o % 4 == 0) {
        return fifo[(o - 0x20) / 4][rp[(o - 0x20) / 4]++];
    }
    uint8_t r = *reg(o);
    if (o == 0xa) {
        *reg(o) = 0;
    }
    return r;
}

uint16_t usb_test_read16(uint32_t o)
{
    if (o == 0x18) {
        return count[index];
    }
    uint16_t r = (uint16_t)*reg(o) | ((uint16_t)*reg(o + 1) << 8);
    if (o == 2 || o == 4) {
        *reg(o) = 0;
        *reg(o + 1) = 0;
    }
    return r;
}

void usb_test_write8(uint32_t o, uint8_t v)
{
    if (o == 0xe) {
        index = v;
        return;
    }
    if (o >= 0x20 && o <= 0x28 && o % 4 == 0) {
        unsigned ep = (o - 0x20) / 4;
        assert(wp[ep] < sizeof sent[ep]);
        sent[ep][wp[ep]++] = v;
        return;
    }
    if (o == 0x12 && index == 0) {
        uint8_t old = *reg(o);
        if (v & 64) {
            old &= (uint8_t)~1;
        }
        if (v & 128) {
            old &= (uint8_t)~16;
        }
        *reg(o) = (old & 0x11) | (v & 2);
        return;
    }
    *reg(o) = v;
}

void usb_test_write16(uint32_t o, uint16_t v)
{
    usb_test_write8(o, (uint8_t)v);
    usb_test_write8(o + 1, (uint8_t)(v >> 8));
}

uint16_t usb_device_get_report(uint8_t type, uint8_t id, uint8_t *out)
{
    get_calls++;
    uint16_t n = type == 1 ? USB_INPUT_REPORT_BYTES : USB_FEATURE_REPORT_BYTES;
    memset(out, 0x5a, n);
    out[0] = id;
    return bad_get ? 0 : n;
}

bool usb_device_set_report(uint8_t type, uint8_t id, const uint8_t *data, uint16_t length)
{
    set_calls++;
    set_type = type;
    set_id = id;
    set_length = length;
    assert(length <= sizeof set_data);
    memcpy(set_data, data, length);
    return !reject_set;
}

static void packet(uint8_t ep, const uint8_t *data, uint16_t n)
{
    assert(n <= sizeof fifo[ep]);
    memcpy(fifo[ep], data, n);
    rp[ep] = 0;
    count[ep] = n;
    indexed[ep][ep == 0 ? 2 : 6] |= 1;
    if (ep == 0) {
        regs[2] |= 1;
    }
    usb_device_poll();
}

static void setup(uint8_t bm, uint8_t r, uint16_t v, uint16_t idx, uint16_t n)
{
    uint8_t s[] = {bm,
                   r,
                   (uint8_t)v,
                   (uint8_t)(v >> 8),
                   (uint8_t)idx,
                   (uint8_t)(idx >> 8),
                   (uint8_t)n,
                   (uint8_t)(n >> 8)};
    wp[0] = 0;
    indexed[0][2] |= 16;
    packet(0, s, 8);
}

static void complete(void)
{
    indexed[0][2] = 0;
    regs[2] |= 1;
    usb_device_poll();
}

static void fresh(bool hs)
{
    memset(regs, 0, sizeof regs);
    memset(indexed, 0, sizeof indexed);
    memset(rp, 0, sizeof rp);
    memset(wp, 0, sizeof wp);
    index = 0;
    set_calls = get_calls = 0;
    reject_set = bad_get = false;
    output_ready = true;
    usb_device_init();
    regs[1] = hs ? 0x70 : 0x60;
    regs[0xa] = 4;
    usb_device_poll();
    setup(0, 9, 1, 0, 0);
    complete();
    assert(usb_device_configured());
}

static void test_controls(void)
{
    fresh(false);
    for (uint8_t id = 3; id <= 8; id++) {
        setup(0xa1, 1, (uint16_t)(0x300 | id), 0, 32);
        assert(wp[0] == 32 && sent[0][0] == id);
        complete();
    }
    setup(0xa1, 1, 0x30a, 0, 64);
    assert(wp[0] == 32 && sent[0][0] == 10);
    complete();
    setup(0xa1, 1, 0x37f, 0, 4);
    assert(wp[0] == 4 && sent[0][0] == 0x7f);
    complete();

    for (uint8_t id = 0x7d; id <= 0x7e; id++) {
        setup(0xa1, 1, (uint16_t)(0x300 | id), 0, 32);
        assert(wp[0] == 32 && sent[0][0] == id);
        complete();
    }
    setup(0xa1, 1, 0x101, 0, 19);
    assert(wp[0] == 19 && sent[0][0] == 1);
    complete();
    uint32_t stalls = usb_diagnostics.stalls;
    unsigned gets = get_calls;
    setup(0xa1, 1, 0x309, 0, 32);
    assert(usb_diagnostics.stalls == ++stalls && get_calls == gets);
    bad_get = true;
    setup(0xa1, 1, 0x303, 0, 32);
    assert(usb_diagnostics.stalls == ++stalls);
    bad_get = false;
    setup(0x21, 9, 0x303, 0, 31);
    assert(usb_diagnostics.stalls == ++stalls);
    setup(0x21, 9, 0x305, 0, 32);
    assert(usb_diagnostics.stalls == ++stalls);
    setup(0x21, 9, 0x202, 0, 512);
    assert(usb_diagnostics.stalls == ++stalls);
    uint8_t feature[32] = {3, 0x80};
    setup(0x21, 9, 0x303, 0, 32);
    packet(0, feature, 32);
    assert(set_calls == 1 && set_type == 3 && set_id == 3 && set_length == 32);
    complete();
    feature[0] = 0x7f;
    setup(0x21, 9, 0x37f, 0, 32);
    packet(0, feature, 32);
    assert(set_calls == 2);
    complete();
    feature[0] = 4;
    setup(0x21, 9, 0x304, 0, 32);
    packet(0, feature, 32);
    assert(set_calls == 3 && set_id == 4);
    complete();
    setup(0x21, 9, 0x303, 0, 32);
    packet(0, feature, 32);
    assert(set_calls == 3 && usb_diagnostics.stalls == ++stalls);
    feature[0] = 3;
    reject_set = true;
    setup(0x21, 9, 0x303, 0, 32);
    packet(0, feature, 32);
    assert(set_calls == 4 && usb_diagnostics.stalls == ++stalls);
    reject_set = false;
    setup(0x21, 9, 0x303, 0, 32);
    packet(0, feature, 31);
    assert(set_calls == 4 && usb_diagnostics.stalls == ++stalls);
    setup(0x21, 10, 0x3701, 0, 0);
    complete();
    setup(0xa1, 2, 1, 0, 1);
    assert(sent[0][0] == 0x37);
    complete();
    setup(0xa1, 2, 2, 0, 1);
    assert(usb_diagnostics.stalls == ++stalls);
}

static void test_reports(bool hs)
{
    fresh(hs);
    uint8_t out[1024];
    for (unsigned i = 0; i < sizeof out; i++) {
        out[i] = (uint8_t)i;
    }
    out[0] = 2;
    uint16_t mps = hs ? 512 : 64;
    for (uint16_t off = 0; off < 1024; off += mps) {
        packet(1, out + off, mps);
        if (off + mps < 1024) {
            assert(set_calls == 0);
        }
    }
    assert(set_calls == 1 && set_type == 2 && set_id == 2 && set_length == 1024 &&
           memcmp(set_data, out, 1024) == 0);
    packet(1, out, 17);
    assert(set_calls == 1 && usb_diagnostics.malformed_packets == 1);
    out[0] = 3;
    for (uint16_t off = 0; off < 1024; off += mps) {
        packet(1, out + off, mps);
    }
    assert(set_calls == 1 && usb_diagnostics.malformed_packets == 2);
    out[0] = 2;
    setup(0x21, 9, 0x202, 0, 1024);
    for (uint16_t off = 0; off < 1024; off += 64) {
        packet(0, out + off, 64);
        if (off < 960) {
            assert(set_calls == 1);
        }
    }
    assert(set_calls == 2 && set_length == 1024);
    complete();
    setup(0x21, 9, 0x202, 0, 1024);
    packet(0, out, 64);
    setup(0xa1, 1, 0x303, 0, 32);
    complete();
    assert(set_calls == 2);
    setup(0x21, 9, 0x202, 0, 1024);
    packet(0, out, 64);
    packet(0, out + 64, 32);
    assert(set_calls == 2);
    uint8_t in[19] = {1, 0, 15, 0};
    assert(!usb_device_send_report(in, 18));
    in[0] = 2;
    assert(!usb_device_send_report(in, 19));
    in[0] = 1;
    assert(usb_device_send_report(in, 19));
    assert(!usb_device_send_report(in, 19));
    usb_device_poll();
    assert(wp[2] == 19 && memcmp(sent[2], in, 19) == 0);
    indexed[2][2] = 0;
    usb_device_poll();
    assert(usb_diagnostics.reports_sent == 1);
    for (int i = 0; i < 5; i++) {
        usb_device_poll();
    }
    assert(wp[2] == 19);
    setup(2, 3, 0, 1, 0);
    complete();
    setup(0x82, 0, 0, 1, 2);
    assert(sent[0][0] == 1);
    complete();
    setup(2, 1, 0, 1, 0);
    complete();
    setup(0x82, 0, 0, 1, 2);
    assert(sent[0][0] == 0);
    complete();

    packet(1, out, mps);
    assert(usb_device_send_report(in, 19));
    regs[0xa] = 4;
    usb_device_poll();
    assert(!usb_device_configured() && usb_diagnostics.address == 0);
    setup(0, 9, 1, 0, 0);
    complete();
    unsigned calls = set_calls;
    for (uint16_t off = mps; off < 1024; off += mps) {
        packet(1, out + off, mps);
    }
    assert(set_calls == calls);
}

static void test_address(void)
{
    fresh(false);
    setup(0, 9, 0, 0, 0);
    complete();
    setup(0, 5, 7, 0, 0);
    assert(usb_diagnostics.address == 0);
    complete();
    assert(usb_diagnostics.address == 7);
    setup(0, 5, 11, 0, 0);
    setup(0x80, 6, 0x100, 0, 18);
    complete();
    assert(usb_diagnostics.address == 7);
}

static void test_backpressure(void)
{
    fresh(true);
    uint8_t out[1024] = {2};
    output_ready = false;
    packet(1, out, 512);
    assert(rp[1] == 0 && set_calls == 0 && (indexed[1][6] & 1));
    setup(0xa1, 1, 0x37e, 0, 32);
    assert(wp[0] == 32);
    complete();
    assert(rp[1] == 0 && set_calls == 0);
    uint8_t in[19] = {1, 0, 15, 0};
    assert(usb_device_send_report(in, 19));
    usb_device_poll();
    assert(wp[2] == 19 && rp[1] == 0);
    output_ready = true;
    usb_device_poll();
    assert(rp[1] == 512 && set_calls == 0);
    packet(1, out + 512, 512);
    assert(set_calls == 1 && set_length == 1024);
}

static void test_suspend(void)
{
    fresh(false);
    regs[0xa] = 1;
    usb_device_poll();
    assert(usb_diagnostics.suspended);
    usb_device_poll();
    assert(usb_diagnostics.suspended);
    regs[0xa] = 2;
    usb_device_poll();
    assert(!usb_diagnostics.suspended);
    regs[0xa] = 1;
    usb_device_poll();
    regs[0xa] = 4;
    usb_device_poll();
    assert(!usb_diagnostics.suspended && !usb_device_configured());
    regs[0xa] = 1;
    usb_device_poll();
    assert(!usb_diagnostics.suspended);
}

int main(void)
{
    test_controls();
    test_reports(false);
    test_reports(true);
    test_address();
    test_backpressure();
    test_suspend();
    puts("USB transport tests passed");
}
