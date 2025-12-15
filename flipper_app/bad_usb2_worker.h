#pragma once

#include <furi.h>
#include <furi_hal.h>
#include <storage/storage.h>
#include <gui/gui.h>
#include "badusb2_protocol.h"

// Define opaque types to match existing app structure references where possible
// The app uses 'BadUsbScript' as the handle name
typedef struct BadUsb2Worker BadUsbScript;

typedef enum {
    BadUsbStateInit,
    BadUsbStateNotConnected,
    BadUsbStateIdle,
    BadUsbStateWillRun,
    BadUsbStateRunning,
    BadUsbStatePaused,
    BadUsbStateDelay,
    BadUsbStateDone,
    BadUsbStateStringDelay,
    BadUsbStateWaitForBtn,
    BadUsbStateScriptError,
    BadUsbStateFileError,
} BadUsbWorkerState;

typedef struct {
    BadUsbWorkerState state;
    char error[64];
    size_t line_cur;
    size_t line_nb;
    size_t error_line;
    uint32_t delay_remain;
} BadUsbState; // Must match the name used in bad_usb_view

BadUsbScript* bad_usb2_worker_open(FuriString* file_path);
void bad_usb2_worker_close(BadUsbScript* worker);
void bad_usb2_worker_start_stop(BadUsbScript* worker);
void bad_usb2_worker_pause_resume(BadUsbScript* worker);
BadUsbState* bad_usb2_worker_get_state(BadUsbScript* worker);

void bad_usb2_worker_set_keyboard_layout(BadUsbScript* worker, FuriString* layout_path);
