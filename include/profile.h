#ifndef BOARD_PROFILE_H
#define BOARD_PROFILE_H
#include <stdint.h>

typedef enum {
    PROFILE_USB_FIFO,
    PROFILE_RING_COPY,
    PROFILE_PROCESS,
    PROFILE_DMA_WAIT,
    PROFILE_FLUSH,
    PROFILE_LOOP,
    PROFILE_COUNT
} profile_stage_t;

extern uint32_t profile_cycles[PROFILE_COUNT];
extern uint32_t profile_loops;

static inline uint32_t profile_now(void)
{
#if defined(__arm__)
    return *(volatile uint32_t *)UINT32_C(0xe0001004);
#else
    return 0;
#endif
}

static inline void profile_add(profile_stage_t stage, uint32_t since)
{
#if defined(__arm__)
    profile_cycles[stage] += profile_now() - since;
#else
    (void)stage;
    (void)since;
#endif
}
#endif
