#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include "bsp/board.h"
#include "tusb.h"

#include "badusb2_protocol.h"

// --- Configuration ---
#define SPI_PORT spi0
#define PIN_MISO 16
#define PIN_CS   17
#define PIN_SCK  18
#define PIN_MOSI 19

// Handshake Pin: High = Request Attention from Flipper
#define PIN_HANDSHAKE 20

// We need a buffer to hold the packet
static SpiPacket spi_packet;
static bool pending_usb_read = false;
static bool pending_usb_write = false;

// TinyUSB Descriptors (Minimal placeholders for logic demonstration)
// In a real project, usb_descriptors.c would define the Composite HID + MSC device

// --- SPI Helpers ---

void spi_slave_init() {
    spi_init(SPI_PORT, 1000 * 1000); // 1 MHz
    spi_set_slave(SPI_PORT, true);
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS,   GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK,  GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    
    // Handshake
    gpio_init(PIN_HANDSHAKE);
    gpio_set_dir(PIN_HANDSHAKE, GPIO_OUT);
    gpio_put(PIN_HANDSHAKE, 0);
}

void notify_flipper() {
    gpio_put(PIN_HANDSHAKE, 1);
    sleep_us(10); // Pulse
    gpio_put(PIN_HANDSHAKE, 0);
}

void spi_transfer_packet(SpiPacket* tx, SpiPacket* rx) {
    // Blocking transfer of the whole structure size
    spi_write_read_blocking(SPI_PORT, (uint8_t*)tx, (uint8_t*)rx, sizeof(SpiPacket));
}

// --- MSC Handlers (TinyUSB Callbacks) ---

// Invoked when received SCSI_CMD_READ_10
int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    (void)lun; (void)offset;

    // Prepare Request
    spi_packet.magic = BADUSB2_PROTOCOL_MAGIC;
    spi_packet.type = CMD_MSC_READ;
    spi_packet.address = lba;
    
    // 1. Wake Flipper
    notify_flipper();
    
    // 2. Transmit Request & Receive Data
    // Note: In real SPI Slave mode, we might need to wait for the Master (Flipper) to start clocking.
    // Ideally, the Master sees the Handshake, asserts CS, and clocks out the data.
    // For simplicity here, we assume one large transfer transaction initiated by Master after Handshake.
    
    SpiPacket rx_packet;
    spi_transfer_packet(&spi_packet, &rx_packet);
    
    // 3. Copy received data to USB buffer
    if (rx_packet.magic == BADUSB2_PROTOCOL_MAGIC) {
        memcpy(buffer, rx_packet.data, bufsize);
        return bufsize;
    }
    
    return -1; // Error
}

// Invoked when received SCSI_CMD_WRITE_10
int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
    (void)lun; (void)offset;

    // Prepare Write Packet
    spi_packet.magic = BADUSB2_PROTOCOL_MAGIC;
    spi_packet.type = CMD_MSC_WRITE;
    spi_packet.address = lba;
    memcpy(spi_packet.data, buffer, bufsize);

    // 1. Wake Flipper
    notify_flipper();

    // 2. Send Data to Flipper
    SpiPacket rx_packet;
    spi_transfer_packet(&spi_packet, &rx_packet);
    
    return bufsize;
}

// --- HID Logic ---
// Usually polled or event driven. Here we might receive generic keycodes via SPI if Flipper initiates it.
// However, as an SPI Slave, we can only talk when Flipper talks to us. 
// So Flipper will push HID commands unpredictably? 
// No, Flipper acts as Master. So Flipper *Sends* us HID commands.
// But we are in a blocking SPI wait inside MSC callbacks... 
// We need a non-blocking main loop that handles SPI if CS is asserted, or waits for interrupts.
//
// Refined Logic:
// The RP2040 main loop monitors the SPI bus.
// If not doing MSC, it checks for incoming HID packets from Flipper.
// *Wait*, Flipper is the Master. It initiates everything.
// The RP2040 is the Slave. It waits for the Master to SELECT it.
//
// Problem: If Flipper wants to send a HID press, it just asserts CS and sends it.
// RP2040 must be ready to receive.

// --- Main ---

int main() {
    board_init();
    tusb_init();
    spi_slave_init();

    while (1) {
        tud_task(); // USB Device Task

        // Check if SPI is active (CS Low)
        // In a robust implementation, we'd use interrupts (irq_set_enabled IO_IRQ_BANK0).
        // For this proof of concept, we assume the hardware buffer or blocking call handles it if we are fast enough,
        // or we use `spi_is_readable` to check for incoming data.
        
        if (spi_is_readable(SPI_PORT)) {
            SpiPacket rx_packet;
            // Read header first or full packet? 
            // We define fixed size packets for simplicity.
            spi_read_blocking(SPI_PORT, 0, (uint8_t*)&rx_packet, sizeof(SpiPacket));

            if (rx_packet.magic == BADUSB2_PROTOCOL_MAGIC) {
                if (rx_packet.type == CMD_HID_PRESS) {
                    // Send HID Report
                     uint8_t keycode[6] = {0};
                     keycode[0] = (uint8_t)rx_packet.address; // Using address field for keycode
                     tud_hid_keyboard_report(0, 0, keycode);
                }
                else if (rx_packet.type == CMD_HID_RELEASE) {
                    tud_hid_keyboard_report(0, 0, NULL);
                }
                // MSC commands are usually initiated by Host (PC), so they trigger the callbacks above.
                // However, the callbacks need to talk to Flipper.
                // This creates a bi-directional complexity:
                // 1. PC asks RP2040 (Master) -> RP2040 asks Flipper (Slave? No Flipper is Master).
                //
                // ARCHITECTURE CORRECTION:
                // RP2040 cannot easily "Ask" Flipper if Flipper is SPI Master.
                // RP2040 can only signal "Attention" via GPIO.
                // Flipper sees GPIO IRQ -> Initiates SPI Transfer.
                //
                // So inside `tud_msc_read10_cb`:
                // 1. Set global state "WAITING_FOR_FLIPPER_DATA".
                // 2. Assert Signal Pin.
                // 3. Busy wait (or yield) until SPI transfer completes (filled by Flipper).
                // 4. Return data.
            }
        }
    }

    return 0;
}
