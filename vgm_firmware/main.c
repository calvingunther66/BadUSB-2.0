#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "tusb.h"

#define SPI_PORT spi0
#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19
#define PIN_HANDSHAKE 20 

tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t), .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200, .bDeviceClass = 0x00, .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00, .bMaxPacketSize0 = 64,
    .idVendor = 0xCAFE, .idProduct = 0x4000, .bcdDevice = 0x0100,
    .iManufacturer = 0x01, .iProduct = 0x02, .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01
};
uint8_t const desc_hid_report[] = { TUD_HID_REPORT_DESC_KEYBOARD() };
enum { ITF_NUM_HID, ITF_NUM_MSC, ITF_NUM_TOTAL };
uint8_t const desc_configuration[] = {
    0x09, TUSB_DESC_CONFIGURATION, 0x4A, 0x00, ITF_NUM_TOTAL, 0x01, 0x00, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 0x32,
    0x09, TUSB_DESC_INTERFACE, ITF_NUM_HID, 0x00, 0x01, TUSB_CLASS_HID, 0x01, 0x01, 0x00,
    0x09, 0x21, 0x11, 0x01, 0x00, 0x01, 0x22, sizeof(desc_hid_report), 0x00,
    0x07, TUSB_DESC_ENDPOINT, 0x81, TUSB_XFER_INTERRUPT, 16, 0x0A,
    0x09, TUSB_DESC_INTERFACE, ITF_NUM_MSC, 0x00, 0x02, TUSB_CLASS_MSC, 0x06, 0x50, 0x00,
    0x07, TUSB_DESC_ENDPOINT, 0x02, TUSB_XFER_BULK, 64, 0x00,
    0x07, TUSB_DESC_ENDPOINT, 0x82, TUSB_XFER_BULK, 64, 0x00
};
uint8_t const * tud_descriptor_device_cb(void) { return (uint8_t const *) &desc_device; }
uint8_t const * tud_descriptor_configuration_cb(uint8_t index) { return desc_configuration; }
uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) { return NULL; }
uint8_t const * tud_hid_descriptor_report_cb(uint8_t itf) { return desc_hid_report; }

void spi_init_slave() {
    spi_init(SPI_PORT, 4000000); 
    spi_set_slave(SPI_PORT, true);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS,   GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_init(PIN_HANDSHAKE); gpio_set_dir(PIN_HANDSHAKE, GPIO_OUT); gpio_put(PIN_HANDSHAKE, 0);
}

void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, uint8_t report_type, uint8_t const* buffer, uint16_t bufsize) { }
uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, uint8_t report_type, uint8_t* buffer, uint16_t reqlen) { return 0; }
int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void* buffer, uint16_t bufsize) { return -1; }
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    gpio_put(PIN_HANDSHAKE, 1); spi_read_blocking(SPI_PORT, 0, buffer, bufsize); gpio_put(PIN_HANDSHAKE, 0); return bufsize;
}
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
    gpio_put(PIN_HANDSHAKE, 1); spi_write_blocking(SPI_PORT, buffer, bufsize); gpio_put(PIN_HANDSHAKE, 0); return bufsize;
}
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4]) {
    memcpy(vendor_id, "Flipper", 7); memcpy(product_id, "BadUSB2", 7); memcpy(product_rev, "1.0", 3);
}
bool tud_msc_test_unit_ready_cb(uint8_t lun) { return true; }
void tud_msc_capacity_cb(uint8_t lun, uint32_t* block_count, uint16_t* block_size) { *block_count = 1024*1024*1024; *block_size = 512; }

int main() {
    stdio_init_all(); spi_init_slave(); tusb_init();
    while (1) { tud_task(); }
    return 0;
}
