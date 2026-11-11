#ifndef AKP_IDLE_GAME_H
#define AKP_IDLE_GAME_H
#include <stdbool.h>
#include <stdint.h>

void idle_game_reset(void);
bool idle_game_step(uint32_t now_ms, uint16_t keys);
void idle_game_suspend(void);
#endif
