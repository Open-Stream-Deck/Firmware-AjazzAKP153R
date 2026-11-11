#include "boot_anim.h"
#include "boot_glyphs.h"
#include "display.h"

#define COLUMNS 6u
#define ROWS 3u
#define CELLS (COLUMNS * ROWS)
#define LETTERS 14u
#define PANEL_LAST 853u
#define KEY_U 21u
#define KEY_V 38u
#define KEY_PITCH_U 148u
#define KEY_PITCH_V 149u
#define KEY_SIZE 103u
#define SIDE_U 759u
#define SIDE_V 35u
#define SIDE_WIDTH 85u
#define SIDE_HEIGHT 406u
#define TILE_RGB UINT32_C(0x222222)
#define INK_RGB UINT32_C(0xffffff)
#define LEVELS 16u
#define ALPHA_MAX 15u
#define ONE 1024u
#define FRAME_MS 15u
#define FILL_STEP_MS 55u
#define FILL_MS 260u
#define SIDE_FILL_MS ((CELLS - 1u) * FILL_STEP_MS + FILL_MS)
#define TEXT_START_MS 1200u
#define TEXT_STEP_MS 85u
#define TEXT_MS 320u
#define HOLD_MS 400u
#define INTRO_MS (TEXT_START_MS + (LETTERS - 1u) * TEXT_STEP_MS + TEXT_MS + HOLD_MS)
#define SETTLE_MS 150u
#define HURRY 4u
#define OUTRO_STEP_MS 50u
#define IRIS_MS 300u
#define OUTRO_MS ((CELLS - 1u) * OUTRO_STEP_MS + IRIS_MS)

typedef enum {
    PHASE_IDLE = 0,
    PHASE_INTRO,
    PHASE_HOLD,
    PHASE_OUTRO,
    PHASE_DONE
} phase_t;

typedef struct {
    uint16_t u, v, w, h;
} box_t;

enum {
    G_O,
    G_P,
    G_E,
    G_N,
    G_S,
    G_T,
    G_R,
    G_A,
    G_M,
    G_D,
    G_C,
    G_K
};

static const struct {
    uint8_t glyph, cell;
} letters[LETTERS] = {
    {G_O, 0},
    {G_P, 1},
    {G_E, 2},
    {G_N, 3},
    {G_S, 6},
    {G_T, 7},
    {G_R, 8},
    {G_E, 9},
    {G_A, 10},
    {G_M, 11},
    {G_D, 12},
    {G_E, 13},
    {G_C, 14},
    {G_K, 15},
};

static phase_t phase;
static uint32_t phase_start, ready_ms, last_frame;
static bool host_ready;
static uint16_t grown[CELLS];
static uint16_t side_grown;
static uint8_t level[LETTERS];
static uint16_t letter_pixels[BOOT_GLYPH_HEIGHT * BOOT_GLYPH_MAX_WIDTH];

static uint32_t progress(uint32_t t, uint32_t start, uint32_t duration)
{
    if (t <= start) {
        return 0;
    }
    if (t - start >= duration) {
        return ONE;
    }
    return (t - start) * ONE / duration;
}

static uint32_t ease_out(uint32_t p)
{
    uint32_t q = ONE - p;
    return ONE - ((q * q * q) >> 20);
}

static uint32_t ease_in_out(uint32_t p)
{
    if (p < ONE / 2u) {
        return (4u * p * p * p) >> 20;
    }
    uint32_t q = 2u * ONE - 2u * p;
    return ONE - ((q * q * q) >> 21);
}

static uint16_t blend(uint32_t from, uint32_t to, uint32_t k)
{
    uint32_t rgb = 0;
    for (unsigned shift = 0; shift < 24; shift += 8) {
        int32_t a = (int32_t)((from >> shift) & 0xffu);
        int32_t b = (int32_t)((to >> shift) & 0xffu);
        uint32_t channel = (uint32_t)(a + (b - a) * (int32_t)k / (int32_t)LEVELS);
        rgb |= channel << shift;
    }
    return (uint16_t)(((rgb >> 8) & 0xf800u) | ((rgb >> 5) & 0x07e0u) | ((rgb >> 3) & 0x001fu));
}

static unsigned snake_cell(unsigned rank)
{
    unsigned row = rank / COLUMNS, i = rank % COLUMNS;
    return row * COLUMNS + ((row & 1u) ? COLUMNS - 1u - i : i);
}

static unsigned snake_rank(unsigned cell)
{
    unsigned row = cell / COLUMNS, col = cell % COLUMNS;
    return row * COLUMNS + ((row & 1u) ? COLUMNS - 1u - col : col);
}

static box_t cell_box(unsigned cell)
{
    unsigned row = cell / COLUMNS, col = cell % COLUMNS;
    uint16_t v = (uint16_t)(KEY_V + row * KEY_PITCH_V);
    if (col == COLUMNS - 1u) {
        return (box_t){SIDE_U, v, SIDE_WIDTH, KEY_SIZE};
    }
    return (box_t){(uint16_t)(KEY_U + col * KEY_PITCH_U), v, KEY_SIZE, KEY_SIZE};
}

static box_t centered(box_t c, uint32_t k)
{
    uint16_t w = (uint16_t)(c.w * k / ONE), h = (uint16_t)(c.h * k / ONE);
    return (box_t){(uint16_t)(c.u + (c.w - w) / 2u), (uint16_t)(c.v + (c.h - h) / 2u), w, h};
}

static bool fill(box_t b, uint16_t color)
{
    if (b.w == 0 || b.h == 0) {
        return true;
    }
    return display_draw_rect(b.v,
                             (uint16_t)(PANEL_LAST - (b.u + b.w - 1u)),
                             (uint16_t)(b.v + b.h - 1u),
                             (uint16_t)(PANEL_LAST - b.u),
                             color,
                             true);
}

static bool reveal(box_t b)
{
    if (b.w == 0 || b.h == 0) {
        return true;
    }
    uint16_t x = b.v, y = (uint16_t)(PANEL_LAST - (b.u + b.w - 1u));
    return display_copy(0, x, y, BOOT_ANIM_CANVAS, x, y, b.h, b.w);
}

static bool draw_letter(unsigned i, uint32_t k)
{
    uint8_t glyph = letters[i].glyph;
    uint16_t gw = boot_glyph_width[glyph], gh = BOOT_GLYPH_HEIGHT;
    box_t c = cell_box(letters[i].cell);
    uint16_t u = (uint16_t)(c.u + (c.w - gw) / 2u), v = (uint16_t)(c.v + (c.h - gh) / 2u);
    uint16_t palette[ALPHA_MAX + 1u];
    for (unsigned a = 0; a <= ALPHA_MAX; ++a) {
        palette[a] = blend(TILE_RGB, INK_RGB, k * a / ALPHA_MAX);
    }
    const uint8_t *alpha = boot_glyph_alpha;
    uint32_t base = boot_glyph_offset[glyph];
    for (uint16_t ny = 0; ny < gw; ++ny) {
        for (uint16_t nx = 0; nx < gh; ++nx) {
            uint32_t n = base + (uint32_t)nx * gw + (gw - 1u - ny);
            uint8_t a = (n & 1u) ? alpha[n >> 1] & 0x0fu : alpha[n >> 1] >> 4;
            letter_pixels[(uint32_t)ny * gh + nx] = palette[a];
        }
    }
    return display_blit_rgb565(
        v, (uint16_t)(PANEL_LAST - (u + gw - 1u)), gh, gw, letter_pixels, gh);
}

static bool set_level(unsigned i, uint32_t k)
{
    if (k == level[i]) {
        return true;
    }
    if (!draw_letter(i, k)) {
        return false;
    }
    level[i] = (uint8_t)k;
    return true;
}

static bool intro_frame(uint32_t t)
{
    bool ok = true;
    for (unsigned rank = 0; rank < CELLS; ++rank) {
        unsigned cell = snake_cell(rank);
        if (cell % COLUMNS == COLUMNS - 1u) {
            continue;
        }
        uint32_t k = ease_out(progress(t, rank * FILL_STEP_MS, FILL_MS));
        if (k > grown[cell]) {
            if (fill(centered(cell_box(cell), k), blend(TILE_RGB, INK_RGB, 0))) {
                grown[cell] = (uint16_t)k;
            } else {
                ok = false;
            }
        }
    }
    uint16_t h = (uint16_t)(SIDE_HEIGHT * ease_out(progress(t, 0, SIDE_FILL_MS)) / ONE);
    if (h > side_grown) {
        box_t strip = {
            SIDE_U, (uint16_t)(SIDE_V + side_grown), SIDE_WIDTH, (uint16_t)(h - side_grown)};
        if (fill(strip, blend(TILE_RGB, INK_RGB, 0))) {
            side_grown = h;
        } else {
            ok = false;
        }
    }
    for (unsigned i = 0; i < LETTERS; ++i) {
        uint32_t k = ease_out(progress(t, TEXT_START_MS + i * TEXT_STEP_MS, TEXT_MS));
        ok = set_level(i, k * LEVELS / ONE) && ok;
    }
    return ok;
}

static bool outro_frame(uint32_t t)
{
    bool ok = true;
    for (unsigned cell = 0; cell < CELLS; ++cell) {
        if (cell % COLUMNS == COLUMNS - 1u) {
            continue;
        }
        uint32_t k = ease_in_out(progress(t, snake_rank(cell) * OUTRO_STEP_MS, IRIS_MS));
        if (k != 0) {
            ok = reveal(centered(cell_box(cell), k)) && ok;
        }
    }
    uint32_t k = ease_in_out(progress(t, 0, OUTRO_MS));
    ok = reveal((box_t){SIDE_U, SIDE_V, SIDE_WIDTH, (uint16_t)(SIDE_HEIGHT * k / ONE)}) && ok;
    return ok;
}

bool boot_anim_start(uint32_t now_ms)
{
    uint16_t x1 = (uint16_t)(display_width() - 1u), y1 = (uint16_t)(display_height() - 1u);
    phase = PHASE_DONE;
    host_ready = false;
    bool ok = display_set_canvas(0) && display_draw_rect(0, 0, x1, y1, 0, true) &&
              display_set_canvas(BOOT_ANIM_CANVAS) && display_draw_rect(0, 0, x1, y1, 0, true) &&
              display_show(BOOT_ANIM_CANVAS);
    (void)display_set_canvas(0);
    if (!ok) {
        return false;
    }
    for (unsigned i = 0; i < CELLS; ++i) {
        grown[i] = 0;
    }
    side_grown = 0;
    for (unsigned i = 0; i < LETTERS; ++i) {
        level[i] = 0;
    }
    phase = PHASE_INTRO;
    phase_start = now_ms;
    last_frame = now_ms - FRAME_MS;
    return true;
}

bool boot_anim_step(uint32_t now_ms)
{
    if (phase == PHASE_HOLD && host_ready && now_ms - ready_ms >= SETTLE_MS) {
        phase = PHASE_OUTRO;
        phase_start = now_ms;
        last_frame = now_ms - FRAME_MS;
    }
    if ((phase != PHASE_INTRO && phase != PHASE_OUTRO) || now_ms - last_frame < FRAME_MS) {
        return false;
    }
    if (phase == PHASE_INTRO && host_ready) {
        phase_start -= (HURRY - 1u) * (now_ms - last_frame);
    }
    last_frame = now_ms;
    if (!display_set_canvas(BOOT_ANIM_CANVAS)) {
        return false;
    }
    uint32_t t = now_ms - phase_start;
    if (phase == PHASE_INTRO) {
        if (intro_frame(t) && t >= INTRO_MS) {
            phase = PHASE_HOLD;
        }
    } else if (outro_frame(t) && t >= OUTRO_MS && display_show(0)) {
        phase = PHASE_DONE;
    }
    return true;
}

bool boot_anim_due(uint32_t now_ms)
{
    if (phase == PHASE_HOLD) {
        return host_ready && now_ms - ready_ms >= SETTLE_MS;
    }
    return (phase == PHASE_INTRO || phase == PHASE_OUTRO) && now_ms - last_frame >= FRAME_MS;
}

void boot_anim_host_drew(uint32_t now_ms)
{
    if (!host_ready && phase != PHASE_DONE) {
        host_ready = true;
        ready_ms = now_ms;
    }
}

bool boot_anim_active(void)
{
    return phase == PHASE_INTRO || phase == PHASE_HOLD || phase == PHASE_OUTRO;
}

bool boot_anim_waiting(void)
{
    return phase == PHASE_HOLD && !host_ready;
}
