#ifndef AKP_TRANSITION_H
#define AKP_TRANSITION_H
#include <stdbool.h>
#include <stdint.h>

#define TRANSITION_FRONT_A UINT32_C(0xd00000)
#define TRANSITION_FRONT_B UINT32_C(0xe00000)
#define TRANSITION_KEYS 15u
#define TRANSITION_CELLS 18u
#define TRANSITION_SIZE 103u

enum {
    TRANSITION_CUT = 0,
    TRANSITION_WAVE = 1
};

typedef struct {
    uint32_t frames, worst_cycles;
} transition_stats_t;

bool transition_start(
    uint32_t next, uint8_t effect, uint16_t duration_ms, uint16_t ox, uint16_t oy, uint32_t now_ms);
bool transition_step(uint32_t now_ms);
bool transition_active(void);
uint8_t transition_size(unsigned cell, uint32_t elapsed_ms);
const transition_stats_t *transition_stats(void);
#endif
