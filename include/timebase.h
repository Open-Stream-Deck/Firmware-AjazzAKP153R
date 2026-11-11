#ifndef BOARD_TIMEBASE_H
#define BOARD_TIMEBASE_H
#include <stdbool.h>
#include <stdint.h>

bool timebase_init(uint32_t core_hz);
uint32_t timebase_now_ms(void);
bool timebase_uses_dwt(void);
#endif
