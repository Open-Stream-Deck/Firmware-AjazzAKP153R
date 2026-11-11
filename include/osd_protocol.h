#ifndef OSD_PROTOCOL_H
#define OSD_PROTOCOL_H
#include <stdbool.h>
#include <stdint.h>

#define OSD_PROTOCOL_VERSION 1u
#define OSD_PAYLOAD_MAX 1021u
#define OSD_BOOT_MAGIC "OSDBOOT!"

typedef struct {
    bool (*canvas)(uint32_t address);
    bool (*blit_begin)(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
    uint32_t (*blit_write)(const uint8_t *p, uint32_t n);
    void (*blit_abort)(void);
    bool (*blit_active)(void);
    bool (*fill)(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
    bool (*line)(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);
    bool (*circle)(uint16_t cx, uint16_t cy, uint16_t r, uint16_t color, bool fill);
    bool (*frame)(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
    bool (*copy)(uint32_t src,
                 uint16_t sx,
                 uint16_t sy,
                 uint32_t dst,
                 uint16_t dx,
                 uint16_t dy,
                 uint16_t w,
                 uint16_t h);
    bool (*blend)(uint32_t s0,
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
                  uint8_t alpha);
    bool (*transition)(
        uint32_t next, uint8_t effect, uint16_t duration_ms, uint16_t ox, uint16_t oy);
    void (*brightness)(uint8_t percent);
    void (*fence)(uint32_t id);
    void (*bootloader)(void);
    bool (*vsync)(void);
} osd_sink_t;

typedef struct {
    uint32_t reports, bytes, commands, errors, pixels;
    uint8_t last_error_opcode, last_error_reason;
} osd_stats_t;

void osd_init(const osd_sink_t *sink);

void osd_reset(void);
void osd_abort(uint8_t reason);

bool osd_report(const uint8_t *report, uint16_t length);
bool osd_streaming(void);
bool osd_resume(void);
bool osd_pending(void);
void osd_hold(bool enabled);
const osd_stats_t *osd_stats(void);
#endif
