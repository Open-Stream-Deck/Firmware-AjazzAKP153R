#ifndef OSD_USB_DESCRIPTORS_H
#define OSD_USB_DESCRIPTORS_H
#include <stdint.h>
#include <stdbool.h>
#define USB_INPUT_REPORT_BYTES 19u
#define USB_OUTPUT_REPORT_BYTES 1024u
#define USB_FEATURE_REPORT_BYTES 32u
#define USB_CONTROL_BYTES 1024u
uint16_t usb_descriptor(uint8_t type, uint8_t index, bool high_speed, uint8_t *out);
#endif
