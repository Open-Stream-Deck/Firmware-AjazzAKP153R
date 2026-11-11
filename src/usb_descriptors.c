#include "usb_descriptors.h"

static const uint8_t device[] = {
    18, 1, 0, 2, 0, 0, 0, 64, 0x09, 0x12, 0x01, 0x00, 0x00, 2, 1, 2, 3, 1};
static const uint8_t qualifier[] = {10, 6, 0, 2, 0, 0, 0, 64, 1, 0};
static const uint8_t report[] = {
    0x06, 0x00, 0xff, 0x09, 1,    0xa1, 1,    0x15, 0,    0x26, 0xff, 0,    0x75, 8,    0x85, 1,
    0x09, 1,    0x95, 18,   0x81, 2,    0x85, 2,    0x09, 2,    0x96, 0xff, 3,    0x91, 2,    0x95,
    31,   0x85, 3,    0x09, 3,    0xb1, 2,    0x85, 0x7e, 0x09, 0x7e, 0xb1, 2,    0x85, 0x7d, 0x09,
    0x7d, 0xb1, 2,    0x85, 4,    0x09, 4,    0xb1, 2,    0x85, 0x7f, 0x09, 0x7f, 0xb1, 2,    0xc0};

static void copy(uint8_t *out, const uint8_t *in, uint16_t n)
{
    for (uint16_t i = 0; i < n; ++i) {
        out[i] = in[i];
    }
}

uint16_t usb_descriptor(uint8_t type, uint8_t index, bool high_speed, uint8_t *out)
{
    if (type == 1 && index == 0) {
        copy(out, device, sizeof device);
        return sizeof device;
    }
    if (type == 6 && index == 0) {
        copy(out, qualifier, sizeof qualifier);
        return sizeof qualifier;
    }
    if ((type == 2 || type == 7) && index == 0) {
        bool hs = type == 7 ? !high_speed : high_speed;
        const uint8_t cfg[] = {9,
                               2,
                               41,
                               0,
                               1,
                               1,
                               0,
                               0x80,
                               50,
                               9,
                               4,
                               0,
                               0,
                               2,
                               3,
                               0,
                               0,
                               0,
                               9,
                               0x21,
                               0x11,
                               1,
                               0,
                               1,
                               0x22,
                               sizeof report,
                               0,
                               7,
                               5,
                               0x82,
                               3,
                               hs ? 0 : 64,
                               hs ? 2 : 0,
                               1,
                               7,
                               5,
                               1,
                               3,
                               hs ? 0 : 64,
                               hs ? 2 : 0,
                               1};
        copy(out, cfg, sizeof cfg);
        out[1] = type;
        return sizeof cfg;
    }
    if (type == 0x21 && index == 0) {
        const uint8_t hid[] = {9, 0x21, 0x11, 1, 0, 1, 0x22, sizeof report, 0};
        copy(out, hid, sizeof hid);
        return sizeof hid;
    }
    if (type == 0x22 && index == 0) {
        copy(out, report, sizeof report);
        return sizeof report;
    }
    if (type == 3) {
        if (index == 0) {
            out[0] = 4;
            out[1] = 3;
            out[2] = 9;
            out[3] = 4;
            return 4;
        }
        const char *s;
        switch (index) {
        case 1:
            s = "Open Stream Deck";
            break;
        case 2:
            s = "OSD AKP153R";
            break;
        case 3:
            s = "OSD153R00001";
            break;
        default:
            return 0;
        }
        uint16_t n = 2;
        out[1] = 3;
        while (*s) {
            out[n++] = (uint8_t)*s++;
            out[n++] = 0;
        }
        out[0] = (uint8_t)n;
        return n;
    }
    return 0;
}
