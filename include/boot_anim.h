#ifndef AKP_BOOT_ANIM_H
#define AKP_BOOT_ANIM_H
#include <stdbool.h>
#include <stdint.h>

#define BOOT_ANIM_CANVAS UINT32_C(0x600000)

bool boot_anim_start(uint32_t now_ms);
bool boot_anim_step(uint32_t now_ms);
bool boot_anim_due(uint32_t now_ms);
void boot_anim_host_drew(uint32_t now_ms);
bool boot_anim_active(void);
bool boot_anim_waiting(void);
#endif
