#ifndef USB_DEVICE_H
#define USB_DEVICE_H
#include <stdbool.h>
#include <stdint.h>
#include "usb_descriptors.h"

typedef struct {
    uint32_t resets, setups, stalls, configured, reports_received, reports_sent;
    uint32_t malformed_packets;
    uint16_t last_report_length;
    uint8_t address, configuration, high_speed, suspended;
    uint8_t last_report_type, last_report_id;
} usb_diagnostics_t;

extern volatile usb_diagnostics_t usb_diagnostics;
void usb_device_init(void);
void usb_device_poll(void);
bool usb_device_configured(void);

bool usb_device_output_ready(void);
uint8_t *usb_device_output_slot(void);
bool usb_device_output_commit(const uint8_t *data, uint16_t length);

bool usb_device_send_report(const uint8_t *report, uint16_t length);

uint16_t usb_device_get_report(uint8_t type, uint8_t id, uint8_t *out);
bool usb_device_set_report(uint8_t type, uint8_t id, const uint8_t *data, uint16_t length);
#endif
