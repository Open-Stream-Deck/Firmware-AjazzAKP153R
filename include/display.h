#ifndef AKP153R_DISPLAY_H
#define AKP153R_DISPLAY_H
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    DISPLAY_STAGE_OFF = 0,
    DISPLAY_STAGE_GPIO,
    DISPLAY_STAGE_CONTROLLER_RESET,
    DISPLAY_STAGE_CONTROLLER_CLOCK,
    DISPLAY_STAGE_SDRAM,
    DISPLAY_STAGE_PANEL,
    DISPLAY_STAGE_FRAMEBUFFER,
    DISPLAY_STAGE_READY,
    DISPLAY_STAGE_FAILED
} display_stage_t;

typedef enum {
    DISPLAY_ERROR_NONE = 0,
    DISPLAY_ERROR_SPI_TX,
    DISPLAY_ERROR_SPI_RX,
    DISPLAY_ERROR_CONTROLLER_RESET,
    DISPLAY_ERROR_CONTROLLER_STATUS,
    DISPLAY_ERROR_ARGUMENT,
    DISPLAY_ERROR_BUSY
} display_error_t;

typedef struct {
    display_stage_t stage;
    display_stage_t failed_stage;
    display_error_t error;
    uint32_t last_status;
    uint32_t spi_bytes;
    uint32_t pixels_written;
    uint32_t blits;
    uint32_t failures;
    uint8_t brightness;
    bool busy;
} display_status_t;

enum {
    DISPLAY_ENGINE_CPU = 0,
    DISPLAY_ENGINE_DMA = 1
};

bool display_flush(void);

bool display_set_canvas(uint32_t address);
bool display_show(uint32_t address);
bool display_wait_vsync(void);
bool display_draw_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
bool display_draw_rect(
    uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color, bool fill);
bool display_draw_round_rect(
    uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t radius, uint16_t color);
bool display_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);
bool display_draw_circle(uint16_t cx, uint16_t cy, uint16_t radius, uint16_t color, bool fill);

bool display_raw_begin(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
uint32_t display_raw_write(const uint8_t *p, uint32_t n);
void display_raw_abort(void);
bool display_raw_active(void);
bool display_copy(uint32_t src,
                  uint16_t sx,
                  uint16_t sy,
                  uint32_t dst,
                  uint16_t dx,
                  uint16_t dy,
                  uint16_t w,
                  uint16_t h);
bool display_strips_begin(uint32_t src, uint32_t dst);
bool display_strip(uint16_t sx, uint16_t sy, uint16_t dx, uint16_t dy, uint16_t w, uint16_t h);
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
                   uint8_t alpha);
bool display_init(void);
bool display_ready(void);
uint16_t display_width(void);
uint16_t display_height(void);
bool display_blit_rgb565(
    uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *pixels, uint16_t stride);
void display_set_brightness(uint8_t percentage);
void display_sync_brightness(void);
bool display_fill_rgb565(uint16_t color);
const volatile display_status_t *display_status(void);
void display_spi_registers(uint8_t *out, unsigned count);
void display_set_spi_wide(bool wide);
bool display_spi_wide(void);
uint32_t display_spi_busy(void);
#endif
