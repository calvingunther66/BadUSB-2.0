#ifndef BADUSB2_PROTOCOL_H
#define BADUSB2_PROTOCOL_H

#include <stdint.h>

// Protocol Magic Byte for Sync
#define BADUSB2_PROTOCOL_MAGIC 0xBD

// Command Types
typedef enum {
    CMD_HID_PRESS = 0x01,
    CMD_HID_RELEASE = 0x02,
    CMD_MSC_READ = 0x10,
    CMD_MSC_WRITE = 0x11,
} BadUsb2CommandType;

// Payload Size (512 bytes for 1 sector)
#define BADUSB2_PAYLOAD_SIZE 512

// Address Alignment
#pragma pack(push, 1)

typedef struct {
    uint8_t magic;           // BADUSB2_PROTOCOL_MAGIC
    uint8_t type;            // BadUsb2CommandType
    uint32_t address;        // MSC Sector Address (LBA) or HID Modifier/Keycode
    uint8_t data[BADUSB2_PAYLOAD_SIZE]; // Data Payload
} SpiPacket;

#pragma pack(pop)

#endif // BADUSB2_PROTOCOL_H
