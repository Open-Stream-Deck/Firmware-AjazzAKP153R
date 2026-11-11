#ifndef AKP_PLATFORM_H
#define AKP_PLATFORM_H
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    PLATFORM_STAGE_NOT_STARTED = 0,
    PLATFORM_STAGE_RESET_SEQUENCE,
    PLATFORM_STAGE_OSCILLATOR_SAFE,
    PLATFORM_STAGE_CALIBRATION,
    PLATFORM_STAGE_OSCILLATOR_PLL,
    PLATFORM_STAGE_CLOCK_DIVIDERS,
    PLATFORM_STAGE_DATA_CACHE,
    PLATFORM_STAGE_INSTRUCTION_CACHE,
    PLATFORM_STAGE_READY,
    PLATFORM_STAGE_FAILED
} platform_stage_t;

typedef enum {
    PLATFORM_ERROR_NONE = 0,
    PLATFORM_ERROR_POLL_TIMEOUT,
    PLATFORM_ERROR_CLOCK_READBACK
} platform_error_t;

typedef struct {
    platform_stage_t stage;
    platform_stage_t failed_stage;
    platform_error_t error;
    uint32_t failed_register;
    uint32_t last_register_value;
    uint32_t core_hz;
    uint32_t bus_hz;
    bool calibration_present;
    bool watchdog_initialized;
} platform_status_t;

bool platform_init(void);
uint32_t platform_core_hz(void);
void platform_watchdog_init(void);
void platform_watchdog_feed(void);

void platform_delay_iterations(uint32_t iterations);
void platform_delay_ms(uint32_t reference_delay_units);
__attribute__((noreturn)) void platform_enter_bootloader(void);

typedef struct {
    uint32_t magic, gate_mode, calibration;
} platform_info_t;
#endif
