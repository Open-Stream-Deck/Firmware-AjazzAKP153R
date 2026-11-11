#ifndef AKP_KEYS_H
#define AKP_KEYS_H
#include <stdbool.h>
#include <stdint.h>
#define KEYS_COUNT 15u
#define KEYS_DEBOUNCE_MS 8u

typedef struct {
    bool initialized;
    uint32_t polls;
    uint32_t transitions;
    uint16_t raw;
    uint16_t pressed;
} keys_status_t;

bool keys_init(void);
void keys_poll(uint32_t now_ms);
uint16_t keys_pressed(void);
uint16_t keys_raw(void);
const volatile keys_status_t *keys_status(void);
#endif
