#include "usb_device.h"
#include "platform.h"
#include "profile.h"
#include <stddef.h>

#ifdef USB_HOST_TEST
extern uint8_t usb_test_read8(uint32_t offset);
extern uint16_t usb_test_read16(uint32_t offset);
extern void usb_test_write8(uint32_t offset, uint8_t value);
extern void usb_test_write16(uint32_t offset, uint16_t value);
#define R8(o) usb_test_read8(o)
#define R16(o) usb_test_read16(o)
#define W8(o, v) usb_test_write8(o, (uint8_t)(v))
#define W16(o, v) usb_test_write16(o, (uint16_t)(v))
#else
#define R8(o) (*(volatile uint8_t *)(UINT32_C(0x4004c000) + (o)))
#define R16(o) (*(volatile uint16_t *)(UINT32_C(0x4004c000) + (o)))
#define W8(o, v) (R8(o) = (uint8_t)(v))
#define W16(o, v) (R16(o) = (uint16_t)(v))
#endif
enum {
    IDLE,
    DATA_IN,
    DATA_OUT,
    STATUS_IN,
    STATUS_OUT
};

enum {
    RX_READY = 1,
    TX_READY = 2,
    SENT_STALL = 4,
    DATA_END = 8,
    SETUP_END = 16,
    SEND_STALL = 32,
    SERVICE_RX = 64,
    SERVICE_SETUP = 128
};

#define FIFO_DOUBLE 0x10u

static uint8_t state, pending_address, configuration, idle_rate;
static uint8_t halt_in, halt_out, tx_busy;
static uint8_t out_type, out_id;
static uint16_t in_length, in_offset, out_expected, out_offset, rx_offset, tx_offset, tx_length;
static bool in_zlp;
static uint8_t control[USB_CONTROL_BYTES], tx_report[USB_INPUT_REPORT_BYTES];
static uint8_t rx_report[USB_OUTPUT_REPORT_BYTES] __attribute__((aligned(4)));
volatile usb_diagnostics_t usb_diagnostics;

__attribute__((weak)) uint8_t *usb_device_output_slot(void)
{
    return rx_report;
}

__attribute__((weak)) bool usb_device_output_commit(const uint8_t *data, uint16_t length)
{
    return usb_device_set_report(2, 2, data, length);
}

__attribute__((weak)) uint16_t usb_device_get_report(uint8_t type, uint8_t id, uint8_t *out)
{
    (void)type;
    (void)id;
    (void)out;
    return 0;
}

__attribute__((weak)) bool
usb_device_set_report(uint8_t type, uint8_t id, const uint8_t *data, uint16_t length)
{
    (void)type;
    (void)id;
    (void)data;
    (void)length;
    return false;
}

static uint16_t min16(uint16_t a, uint16_t b)
{
    return a < b ? a : b;
}

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void fifo_read(uint8_t ep, uint8_t *p, uint16_t n)
{
    for (uint16_t i = 0; i < n; i++) {
        p[i] = R8(0x20u + 4u * ep);
    }
}

static void fifo_read_words(uint8_t ep, uint8_t *p, uint16_t n)
{
#ifdef USB_HOST_TEST
    fifo_read(ep, p, n);
#else
    volatile uint32_t *fifo = (volatile uint32_t *)(UINT32_C(0x4004c020) + 4u * ep);
    uint32_t *words = (uint32_t *)(void *)p;
    uint16_t count = n / 4u;
    for (uint16_t i = 0; i < count; i++) {
        words[i] = *fifo;
    }
    fifo_read(ep, p + 4u * count, (uint16_t)(n - 4u * count));
#endif
}

static void fifo_write(uint8_t ep, const uint8_t *p, uint16_t n)
{
    for (uint16_t i = 0; i < n; i++) {
        W8(0x20u + 4u * ep, p[i]);
    }
}

static uint16_t packet_size(void)
{
    return usb_diagnostics.high_speed ? 512 : 64;
}

static void endpoint_configure(uint8_t ep, uint16_t tx_base, uint16_t rx_base, uint8_t rx_flags)
{
    W8(0xe, ep);
    uint16_t n = packet_size();
    uint8_t size = usb_diagnostics.high_speed ? 6 : 3;
    W8(0x62, size);
    W8(0x63, size | rx_flags);
    W16(0x64, tx_base >> 3);
    W16(0x66, rx_base >> 3);
    W16(0x10, n);
    W16(0x14, n);
    W16(0x12, 0x48);
    W16(0x12, 0x48);
    W16(0x16, 0x90);
    W16(0x16, 0x90);
}

static void configure(uint8_t c)
{
    configuration = c;
    usb_diagnostics.configuration = c;
    rx_offset = tx_offset = 0;
    tx_busy = halt_in = halt_out = 0;
    endpoint_configure(1, 0x200, 0x400, FIFO_DOUBLE);
    endpoint_configure(2, 0x200, 0x400, 0);
    W8(0xe, 0);
    if (c) {
        usb_diagnostics.configured++;
    }
}

static void reset(void)
{
    usb_diagnostics.resets++;
    usb_diagnostics.high_speed = (R8(1) & 0x10) != 0;
    usb_diagnostics.suspended = 0;
    state = IDLE;
    pending_address = 0;
    idle_rate = 0;
    W8(0, 0);
    usb_diagnostics.address = 0;
    configure(0);
}

static void stall(void)
{
    state = IDLE;
    pending_address = 0;
    W8(0x12, SERVICE_RX | SEND_STALL);
    usb_diagnostics.stalls++;
}

static void status_in(void)
{
    state = STATUS_IN;
    W8(0x12, SERVICE_RX | DATA_END);
}

static void next_in(void)
{
    uint16_t n = min16(64, (uint16_t)(in_length - in_offset));
    fifo_write(0, control + in_offset, n);
    in_offset += n;
    bool last = in_offset == in_length && (!in_zlp || n == 0);
    if (n == 0) {
        in_zlp = false;
    }
    W8(0x12, TX_READY | (last ? DATA_END : 0));
    if (last) {
        state = STATUS_OUT;
    }
}

static void reply(uint16_t n, uint16_t requested)
{
    if (requested == 0) {
        status_in();
        return;
    }
    in_length = min16(n, requested);
    in_offset = 0;
    in_zlp = n < requested && (in_length % 64) == 0;
    W8(0x12, SERVICE_RX);
    state = DATA_IN;
    next_in();
}

static bool endpoint_valid(uint16_t ep)
{
    return ep == 0 || (configuration && (ep == 1 || ep == 0x82));
}

static uint16_t get_report_size(uint8_t type, uint8_t id)
{
    if (type == 1 && id == 1) {
        return USB_INPUT_REPORT_BYTES;
    }
    if (type == 3 && ((id >= 3 && id <= 8) || id == 10 || (id >= 0x7d && id <= 0x7f))) {
        return USB_FEATURE_REPORT_BYTES;
    }
    return 0;
}

static uint16_t set_report_size(uint8_t type, uint8_t id)
{
    if (type == 2 && id == 2) {
        return USB_OUTPUT_REPORT_BYTES;
    }
    if (type == 3 && (id == 3 || id == 4 || id == 0x7f)) {
        return USB_FEATURE_REPORT_BYTES;
    }
    return 0;
}

static bool receive_report(uint8_t type, uint8_t id, const uint8_t *data, uint16_t length)
{
    if (length != set_report_size(type, id) || !length || data[0] != id ||
        !usb_device_set_report(type, id, data, length)) {
        return false;
    }
    usb_diagnostics.last_report_length = length;
    usb_diagnostics.last_report_type = type;
    usb_diagnostics.last_report_id = id;
    usb_diagnostics.reports_received++;
    return true;
}

static bool receive_output(const uint8_t *data)
{
    if (data[0] != 2 || !usb_device_output_commit(data, USB_OUTPUT_REPORT_BYTES)) {
        return false;
    }
    usb_diagnostics.last_report_length = USB_OUTPUT_REPORT_BYTES;
    usb_diagnostics.last_report_type = 2;
    usb_diagnostics.last_report_id = 2;
    usb_diagnostics.reports_received++;
    return true;
}

static void setup(const uint8_t *s)
{
    uint8_t bm = s[0], req = s[1];
    uint16_t val = le16(s + 2), idx = le16(s + 4), len = le16(s + 6);
    usb_diagnostics.setups++;
    pending_address = 0;
    if (bm == 0x80 && req == 6 && (val >> 8) == 3 && (idx == 0 || idx == 0x409)) {
        uint16_t n = usb_descriptor(3, (uint8_t)val, usb_diagnostics.high_speed, control);
        if (n) {
            reply(n, len);
            return;
        }
    } else if (req == 6 &&
               ((bm == 0x80 && idx == 0 && (val >> 8) != 3) ||
                (bm == 0x81 && idx == 0 && ((val >> 8) == 0x21 || (val >> 8) == 0x22)))) {
        uint8_t t = (uint8_t)(val >> 8);
        if ((bm == 0x80 && (t == 1 || t == 2 || t == 6 || t == 7)) || bm == 0x81) {
            uint16_t n = usb_descriptor(t, (uint8_t)val, usb_diagnostics.high_speed, control);
            if (n) {
                reply(n, len);
                return;
            }
        }
    } else if (bm == 0 && req == 5 && idx == 0 && len == 0 && val < 128 && !configuration) {
        pending_address = (uint8_t)(val | 0x80);
        status_in();
        return;
    } else if (bm == 0 && req == 9 && idx == 0 && len == 0 && val <= 1) {
        configure((uint8_t)val);
        status_in();
        return;
    } else if (bm == 0x80 && req == 8 && val == 0 && idx == 0 && len == 1) {
        control[0] = configuration;
        reply(1, len);
        return;
    } else if (req == 0 && val == 0 && len == 2 &&
               ((bm == 0x80 && idx == 0) || (bm == 0x81 && idx == 0 && configuration) ||
                (bm == 0x82 && endpoint_valid(idx)))) {
        control[0] = bm == 0x82 ? (idx == 1 ? halt_out : idx == 0x82 ? halt_in : 0) : 0;
        control[1] = 0;
        reply(2, len);
        return;
    } else if ((bm == 2) && (req == 1 || req == 3) && val == 0 && len == 0 && configuration &&
               (idx == 1 || idx == 0x82)) {
        bool set = req == 3;
        uint8_t ep = (uint8_t)(idx & 15);
        W8(0xe, ep);
        if (idx == 1) {
            halt_out = set;
            rx_offset = 0;
            W16(0x16, 0x90);
            W16(0x16, 0x90);
            if (set) {
                W16(0x16, 0x20);
            }
        } else {
            halt_in = set;
            tx_busy = 0;
            W16(0x12, 0x48);
            W16(0x12, 0x48);
            if (set) {
                W16(0x12, 0x10);
            }
        }
        W8(0xe, 0);
        status_in();
        return;
    } else if (bm == 0x81 && req == 10 && idx == 0 && val == 0 && len == 1 && configuration) {
        control[0] = 0;
        reply(1, len);
        return;
    } else if (bm == 1 && req == 11 && idx == 0 && val == 0 && len == 0 && configuration) {
        status_in();
        return;
    } else if (configuration && idx == 0) {
        if (bm == 0xa1 && req == 2 && val <= 1 && len == 1) {
            control[0] = idle_rate;
            reply(1, len);
            return;
        }
        if (bm == 0x21 && req == 10 && (val & 255) <= 1 && len == 0) {
            idle_rate = (uint8_t)(val >> 8);
            status_in();
            return;
        }

        if (bm == 0xa1 && req == 1) {
            uint8_t type = (uint8_t)(val >> 8), id = (uint8_t)val;
            uint16_t expected = get_report_size(type, id);
            if (expected) {
                uint16_t actual = usb_device_get_report(type, id, control);
                if (actual == expected && control[0] == id) {
                    reply(actual, len);
                    return;
                }
            }
        }
        if (bm == 0x21 && req == 9) {
            uint8_t type = (uint8_t)(val >> 8), id = (uint8_t)val;
            uint16_t expected = set_report_size(type, id);
            if (expected && len == expected) {
                out_type = type;
                out_id = id;
                state = DATA_OUT;
                out_expected = len;
                out_offset = 0;
                W8(0x12, SERVICE_RX);
                return;
            }
        }
    }
    stall();
}

static void ep0(bool irq)
{
    W8(0xe, 0);
    uint8_t csr = R8(0x12);
    if (csr & SENT_STALL) {
        W8(0x12, 0);
        state = IDLE;
        pending_address = 0;
    }
    if (csr & SETUP_END) {
        W8(0x12, SERVICE_SETUP);
        state = IDLE;
        pending_address = 0;
    }
    if (state == STATUS_IN && irq && !(csr & TX_READY) && !(csr & RX_READY)) {
        if (pending_address & 0x80) {
            W8(0, pending_address & 127);
            usb_diagnostics.address = pending_address & 127;
        }
        pending_address = 0;
        state = IDLE;
    }
    if (csr & RX_READY) {
        uint16_t n = R16(0x18);
        if (state == DATA_OUT) {
            if (n > 64 || n > out_expected - out_offset) {
                for (uint16_t i = 0; i < n; i++) {
                    (void)R8(0x20);
                }
                usb_diagnostics.malformed_packets++;
                stall();
                return;
            }
            fifo_read(0, control + out_offset, n);
            out_offset += n;
            if (out_offset == out_expected) {
                if (receive_report(out_type, out_id, control, out_offset)) {
                    status_in();
                } else {
                    usb_diagnostics.malformed_packets++;
                    stall();
                }
            } else if (n < 64) {
                usb_diagnostics.malformed_packets++;
                stall();
            } else {
                W8(0x12, SERVICE_RX);
            }
        } else if (n == 8) {
            uint8_t s[8];
            fifo_read(0, s, 8);
            state = IDLE;
            setup(s);
        } else if (state == STATUS_OUT && n == 0) {
            W8(0x12, SERVICE_RX);
            state = IDLE;
        } else {
            for (uint16_t i = 0; i < n; i++) {
                (void)R8(0x20);
            }
            stall();
        }
    } else if (state == DATA_IN && irq && !(csr & TX_READY)) {
        next_in();
    }
}

static void transmit(void)
{
    if (!tx_busy || halt_in) {
        return;
    }
    W8(0xe, 2);
    if (R8(0x12) & 1) {
        return;
    }
    if (tx_offset == tx_length) {
        tx_busy = 0;
        usb_diagnostics.reports_sent++;
        return;
    }
    uint16_t n = min16(packet_size(), (uint16_t)(tx_length - tx_offset));
    fifo_write(2, tx_report + tx_offset, n);
    tx_offset += n;
    W8(0x12, 1);
}

bool usb_device_send_report(const uint8_t *report, uint16_t length)
{
    if (!report || length != USB_INPUT_REPORT_BYTES || report[0] != 1 || !configuration ||
        tx_busy || halt_in) {
        return false;
    }
    for (uint16_t i = 0; i < length; i++) {
        tx_report[i] = report[i];
    }
    tx_length = length;
    tx_offset = 0;
    tx_busy = 1;
    return true;
}

bool usb_device_configured(void)
{
    return configuration != 0;
}

__attribute__((weak)) bool usb_device_output_ready(void)
{
    return true;
}

void usb_device_poll(void)
{
    uint8_t bus = R8(0xa);
    uint16_t tx = R16(2);
    (void)R16(4);
    if ((bus & 1) && configuration) {
        usb_diagnostics.suspended = 1;
    }
    if ((bus & 2) || (tx & 1)) {
        usb_diagnostics.suspended = 0;
    }
    if (bus & 4) {
        reset();
    }
    ep0((tx & 1) != 0);
    if (configuration && !halt_out && usb_device_output_ready()) {
        W8(0xe, 1);
        uint8_t csr = R8(0x16);
        if (csr & 1) {
            uint16_t n = R16(0x18);
            if (n > packet_size() || n > USB_OUTPUT_REPORT_BYTES - rx_offset) {
                for (uint16_t i = 0; i < n; i++) {
                    (void)R8(0x24);
                }
                rx_offset = 0;
                usb_diagnostics.malformed_packets++;
            } else {
                uint8_t *rx = usb_device_output_slot();
                uint32_t since = profile_now();
                fifo_read_words(1, rx + rx_offset, n);
                profile_add(PROFILE_USB_FIFO, since);
                rx_offset += n;
                if (rx_offset == USB_OUTPUT_REPORT_BYTES) {
                    since = profile_now();
                    if (!receive_output(rx)) {
                        usb_diagnostics.malformed_packets++;
                    }
                    profile_add(PROFILE_RING_COPY, since);
                    rx_offset = 0;
                } else if (n < packet_size()) {
                    usb_diagnostics.malformed_packets++;
                    rx_offset = 0;
                }
            }
            W8(0x16, csr & ~1u);
        }
    }
    if (configuration) {
        transmit();
    }
    W8(0xe, 0);
}

void usb_device_init(void)
{
    usb_diagnostics = (usb_diagnostics_t){0};
#ifndef USB_HOST_TEST
    volatile uint32_t *phy = (volatile uint32_t *)UINT32_C(0x4000402c);
    volatile uint16_t *io = (volatile uint16_t *)UINT32_C(0x40001006);
    *phy |= UINT32_C(0x01000000);
    platform_delay_ms(11);
    *io &= UINT16_C(0xff00);

    volatile uint32_t wait = 300;
    do {
        uint32_t old = wait;
        wait = old - 1;
        if (old == 0) {
            break;
        }
    } while (1);
    *phy &= UINT32_C(0xf3ffffff);
    platform_delay_ms(10);
    *phy &= UINT32_C(0xdfffffff);
    platform_delay_ms(1);
    *phy &= UINT32_C(0xefffffff);
    platform_delay_ms(1);
    *phy &= UINT32_C(0xfdffffff);
    platform_delay_ms(1);
    *io |= UINT16_C(0x0e00);
#endif
    W8(1, 0);
    W8(0xb, 0x37);
    W16(6, 5);
    W16(8, 2);
    W8(0xe, 0);
    W16(0x64, 0);
    W16(0x66, 64);
    reset();

    W8(1, 0x60);
}
