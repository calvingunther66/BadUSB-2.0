#include "bad_usb2_worker.h"
#include <furi.h>
#include <furi_hal.h>
#include <lib/toolbox/strint.h>

#define TAG "BadUsb2Worker"

// --- SPI Configuration ---
#define SPI_HANDLE &furi_hal_spi_bus_handle_external
#define SPI_TIMEOUT 100

// GPIO for Handshake (Assuming PC3 for now)
#define GPIO_HANDSHAKE &gpio_ext_pc3

// Events
typedef enum {
    WorkerEvtStop = (1 << 0),
    WorkerEvtStart = (1 << 1),
    WorkerEvtPauseResume = (1 << 2),
    WorkerEvtSpiIrq = (1 << 3), // Triggered by GPIO
} WorkerEvents;

struct BadUsb2Worker {
    FuriThread* thread;
    BadUsbState st;
    FuriString* file_path;
    FuriString* layout_path;
    
    // File Handles
    File* script_file;
    File* iso_file;
    
    // Buffers and Parsing
    FuriString* line;
    uint8_t file_buf[128];
    uint32_t buf_len;
    uint32_t buf_start;
    bool file_end;
    
    // Execution State
    uint32_t defdelay;
    uint32_t stringdelay;
    uint32_t repeat_cnt;
};

// --- IRQ Handler ---
static void worker_gpio_callback(void* context) {
    FuriThreadId thread_id = (FuriThreadId)context;
    furi_thread_flags_set(thread_id, WorkerEvtSpiIrq);
}

// --- SPI Operations ---

static void handle_spi_transaction(BadUsb2Worker* worker) {
    SpiPacket req;
    SpiPacket resp;
    
    furi_hal_spi_acquire(SPI_HANDLE);
    
    // 1. Read Request
    furi_hal_gpio_write(SPI_HANDLE->cs, false);
    furi_hal_spi_bus_rx(SPI_HANDLE, (uint8_t*)&req, sizeof(SpiPacket), SPI_TIMEOUT);
    furi_hal_gpio_write(SPI_HANDLE->cs, true);
    
    if (req.magic != BADUSB2_PROTOCOL_MAGIC) {
        furi_hal_spi_release(SPI_HANDLE);
        return;
    }
    
    // 2. Process
    memset(&resp, 0, sizeof(SpiPacket));
    resp.magic = BADUSB2_PROTOCOL_MAGIC;
    
    if (req.type == CMD_MSC_READ) {
        resp.type = CMD_MSC_READ;
        if (worker->iso_file && storage_file_is_open(worker->iso_file)) {
             storage_file_seek(worker->iso_file, req.address * 512, true);
             storage_file_read(worker->iso_file, resp.data, 512);
        } else {
             memset(resp.data, 0, 512);
        }
        
        furi_delay_us(50);
        
        furi_hal_gpio_write(SPI_HANDLE->cs, false);
        furi_hal_spi_bus_tx(SPI_HANDLE, (uint8_t*)&resp, sizeof(SpiPacket), SPI_TIMEOUT);
        furi_hal_gpio_write(SPI_HANDLE->cs, true);
        
    } else if (req.type == CMD_MSC_WRITE) {
         if (worker->iso_file && storage_file_is_open(worker->iso_file)) {
             storage_file_seek(worker->iso_file, req.address * 512, true);
             storage_file_write(worker->iso_file, req.data, 512);
        }
    }
    
    furi_hal_spi_release(SPI_HANDLE);
}

static void send_hid_command(BadUsb2Worker* worker, uint8_t type, uint8_t keycode) {
    SpiPacket pkt;
    memset(&pkt, 0, sizeof(SpiPacket));
    pkt.magic = BADUSB2_PROTOCOL_MAGIC;
    pkt.type = type;
    pkt.address = keycode;
    
    furi_hal_spi_acquire(SPI_HANDLE);
    furi_hal_gpio_write(SPI_HANDLE->cs, false);
    furi_hal_spi_bus_tx(SPI_HANDLE, (uint8_t*)&pkt, sizeof(SpiPacket), SPI_TIMEOUT);
    furi_hal_gpio_write(SPI_HANDLE->cs, true);
    furi_hal_spi_release(SPI_HANDLE);
}

// --- DuckyScript Interpreter Partial Implementation ---
static void execute_script_step(BadUsb2Worker* worker) {
    if (furi_string_empty(worker->line)) {
        if (storage_file_read(worker->script_file, worker->file_buf, 1) > 0) {
            char c = (char)worker->file_buf[0];
            if (c == '\n') {
                const char* cmd = furi_string_get_cstr(worker->line);
                if (strncmp(cmd, "STRING ", 7) == 0) {
                    const char* str = cmd + 7;
                    while (*str) {
                         send_hid_command(worker, CMD_HID_PRESS, (uint8_t)*str);
                         send_hid_command(worker, CMD_HID_RELEASE, 0);
                         str++;
                         furi_delay_ms(10);
                    }
                } else if (strncmp(cmd, "DELAY ", 6) == 0) {
                     uint32_t d;
                     if (strint_to_uint32(cmd + 6, NULL, &d, 10) == StrintParseNoError) {
                         furi_delay_ms(d);
                     }
                }
                furi_string_reset(worker->line);
                worker->st.line_cur++;
            } else {
                furi_string_push_back(worker->line, c);
            }
        } else {
            worker->st.state = BadUsbStateIdle;
            furi_thread_flags_set(furi_thread_get_id(worker->thread), WorkerEvtStop);
        }
    }
}

// --- Worker Thread ---

static int32_t bad_usb2_worker_task(void* context) {
    BadUsb2Worker* worker = context;
    
    // Init SPI GPIO
    furi_hal_gpio_init(GPIO_HANDSHAKE, GpioModeInterruptRise, GpioPullDown, GpioSpeedVeryHigh);
    furi_hal_gpio_add_int_callback(GPIO_HANDSHAKE, worker_gpio_callback, furi_thread_get_id(worker->thread));
    furi_hal_gpio_enable_int_callback(GPIO_HANDSHAKE);
    
    // Init Storage
    Storage* storage = furi_record_open(RECORD_STORAGE);
    worker->script_file = storage_file_alloc(storage);
    worker->iso_file = storage_file_alloc(storage);
    worker->line = furi_string_alloc();
    
    if (storage_file_open(worker->script_file, furi_string_get_cstr(worker->file_path), FSAM_READ, FSOM_OPEN_EXISTING)) {
        worker->st.state = BadUsbStateIdle;
    } else {
        worker->st.state = BadUsbStateFileError;
    }

    if (storage_file_open(worker->iso_file, EXT_PATH("disk.img"), FSAM_READ_WRITE, FSOM_OPEN_EXISTING)) {
        FURI_LOG_I(TAG, "Opened disk.img");
    }

    while(1) {
        uint32_t flags = furi_thread_flags_wait(WorkerEvtStop | WorkerEvtStart | WorkerEvtSpiIrq, FuriFlagWaitAny, 10);
        
        if (flags & WorkerEvtStop) {
             if (worker->st.state == BadUsbStateRunning) {
                 worker->st.state = BadUsbStateIdle;
                 continue;
             }
        }
        
        if (flags & WorkerEvtSpiIrq) {
            handle_spi_transaction(worker);
            furi_thread_flags_clear(WorkerEvtSpiIrq);
        }
        
        if (flags & WorkerEvtStart) {
            worker->st.state = BadUsbStateRunning;
            storage_file_seek(worker->script_file, 0, true);
            worker->st.line_cur = 0;
            furi_thread_flags_clear(WorkerEvtStart);
        }

        if (worker->st.state == BadUsbStateRunning) {
            execute_script_step(worker);
        }
    }
    
    furi_hal_gpio_remove_int_callback(GPIO_HANDSHAKE);
    
    storage_file_close(worker->script_file);
    storage_file_close(worker->iso_file);
    storage_file_free(worker->script_file);
    storage_file_free(worker->iso_file);
    furi_string_free(worker->line);
    furi_record_close(RECORD_STORAGE);
    
    return 0;
}

// --- API ---

BadUsbScript* bad_usb2_worker_open(FuriString* file_path) {
    BadUsb2Worker* worker = malloc(sizeof(BadUsb2Worker));
    worker->file_path = furi_string_alloc();
    furi_string_set(worker->file_path, file_path);
    worker->layout_path = furi_string_alloc();
    
    worker->thread = furi_thread_alloc_ex("BadUsb2Worker", 4096, bad_usb2_worker_task, worker);
    furi_thread_start(worker->thread);
    return worker;
}

void bad_usb2_worker_close(BadUsbScript* worker) {
     furi_thread_terminate(worker->thread);
     furi_thread_free(worker->thread);
     furi_string_free(worker->file_path);
     furi_string_free(worker->layout_path);
     free(worker);
}

void bad_usb2_worker_start_stop(BadUsbScript* worker) {
    if (worker->st.state == BadUsbStateRunning) {
        furi_thread_flags_set(furi_thread_get_id(worker->thread), WorkerEvtStop);
    } else {
        furi_thread_flags_set(furi_thread_get_id(worker->thread), WorkerEvtStart);
    }
}

void bad_usb2_worker_pause_resume(BadUsbScript* worker) {
    furi_thread_flags_set(furi_thread_get_id(worker->thread), WorkerEvtPauseResume);
}

BadUsbState* bad_usb2_worker_get_state(BadUsbScript* worker) {
    return &worker->st;
}

void bad_usb2_worker_set_keyboard_layout(BadUsbScript* worker, FuriString* layout_path) {
    if (layout_path) {
        furi_string_set(worker->layout_path, layout_path);
    }
}
